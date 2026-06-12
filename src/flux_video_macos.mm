// flux_video_macos.mm
// AVFoundation + AVSampleBufferDisplayLayer A/V decode engine for FluxUI on macOS.
//
// Pipeline:
//   AVAsset → AVAssetReader → AVAssetReaderTrackOutput (video) → CVPixelBuffer → RGB24
//                           → AVAssetReaderTrackOutput (audio) → PCM float    → FluxAudio
//
// All decoding runs on a single background thread spun up by open().
// The UI thread calls lockFrame() after hasNewFrame() to blit the RGB24 pixels
// via CoreGraphics inside VideoPlayerWidget::render().
//
// Frame format: kCVPixelFormatType_24RGB → 3 bytes/pixel, no padding on macOS.
// The widget blits using CGBitmapContext / CGImageRef exactly like the Win32
// StretchDIBits path but through CoreGraphics.
//
// Dependencies (add to CMakeLists macOS block):
//   find_library(AV_FOUNDATION  AVFoundation)
//   find_library(CORE_MEDIA     CoreMedia)
//   find_library(CORE_VIDEO     CoreVideo)
//   find_library(AUDIO_TOOLBOX  AudioToolbox)
//   target_link_libraries(${TARGET} PRIVATE
//       ${AV_FOUNDATION} ${CORE_MEDIA} ${CORE_VIDEO} ${AUDIO_TOOLBOX})
//
#if defined(__APPLE__)
#include <TargetConditionals.h>
#if TARGET_OS_OSX

#include "flux_video.hpp"
#include "flux_audio.hpp"

#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// ============================================================================
// Internal state
// ============================================================================

namespace {

struct VideoState {
    // ── Playback ──────────────────────────────────────────────────────────
    std::atomic<FluxVideo::State> state{FluxVideo::State::Idle};
    std::atomic<int64_t> positionUs{0};
    std::atomic<int64_t> durationUs{0};
    std::atomic<int>     videoWidth{0};
    std::atomic<int>     videoHeight{0};
    std::atomic<bool>    newFrame{false};

    // ── Frame double-buffer (RGB24) ───────────────────────────────────────
    std::mutex           frameMutex;
    std::vector<uint8_t> frameData;
    int                  frameWidth  = 0;
    int                  frameHeight = 0;
    int                  frameStride = 0;

    // ── Audio ring buffer (~4 s at 48 kHz) ───────────────────────────────
    static constexpr int kAudioRing = 48000 * 4;
    std::vector<float>   audioBuf;
    int                  audioWrite      = 0;
    int                  audioRead       = 0;
    std::mutex           audioMutex;
    int                  audioChannels   = 2;
    int                  audioSampleRate = 44100;

    // ── Command channel ───────────────────────────────────────────────────
    std::mutex              cmdMutex;
    std::condition_variable pauseCV;
    bool pauseRequested  = false;
    bool resumeRequested = false;
    bool stopDecode      = false;
    bool seekPending     = false;
    int64_t seekTargetUs = 0;

    // ── Thread ────────────────────────────────────────────────────────────
    std::thread       decodeThread;
    std::atomic<bool> didFireFinish{false};

    // ── Callbacks ─────────────────────────────────────────────────────────
    FluxVideo::FinishCallback         finishCallback;
    std::function<void(int w, int h)> readyCallback;

    // ── Clock ─────────────────────────────────────────────────────────────
    std::chrono::steady_clock::time_point clockBase;
    int64_t clockBaseUs = 0;
};

} // anonymous namespace

static VideoState& impl() {
    static VideoState s;
    return s;
}

// ============================================================================
// FluxVideo — public API
// ============================================================================

FluxVideo& FluxVideo::get() {
    static FluxVideo instance;
    return instance;
}

FluxVideo::~FluxVideo() { close(); }

FluxVideo::State FluxVideo::getState()           const { return impl().state.load(); }
bool             FluxVideo::isPlaying()          const { return impl().state == State::Playing; }
bool             FluxVideo::isPaused()           const { return impl().state == State::Paused; }
bool             FluxVideo::isFinished()         const { return impl().state == State::Finished; }
int              FluxVideo::getVideoWidth()      const { return impl().videoWidth.load(); }
int              FluxVideo::getVideoHeight()     const { return impl().videoHeight.load(); }
bool             FluxVideo::hasNewFrame()        const { return impl().newFrame.load(); }

float FluxVideo::getDurationSeconds() const { return impl().durationUs.load() / 1e6f; }
float FluxVideo::getPositionSeconds() const { return impl().positionUs.load() / 1e6f; }
float FluxVideo::getProgress() const {
    int64_t dur = impl().durationUs.load();
    return dur > 0 ? std::min(1.f, (float)impl().positionUs.load() / (float)dur) : 0.f;
}

void  FluxVideo::setOnFinished(FinishCallback cb)                    { impl().finishCallback = std::move(cb); }
void  FluxVideo::setOnReady(std::function<void(int w, int h)> cb)    { impl().readyCallback  = std::move(cb); }
void  FluxVideo::setVolume(float v)                                  { FluxAudio::get().setVolume(v); }
float FluxVideo::getVolume() const                                   { return FluxAudio::get().getVolume(); }

FluxVideo::FrameLock FluxVideo::lockFrame() {
    FrameLock fl;
    fl.lock   = std::unique_lock<std::mutex>(impl().frameMutex);
    fl.data   = impl().frameData.data();
    fl.width  = impl().frameWidth;
    fl.height = impl().frameHeight;
    fl.stride = impl().frameStride;
    impl().newFrame = false;
    return fl;
}

// ── Audio helpers ─────────────────────────────────────────────────────────────

static int pullAudio(float* buf, int frames) {
    auto& s = impl();
    std::lock_guard<std::mutex> lk(s.audioMutex);
    int rd = s.audioRead, wr = s.audioWrite;
    int avail  = wr - rd;
    int toCopy = std::min(frames, avail);
    for (int i = 0; i < toCopy; i++)
        buf[i] = s.audioBuf[(size_t)((rd + i) % VideoState::kAudioRing)];
    for (int i = toCopy; i < frames; i++)
        buf[i] = 0.f;
    s.audioRead = rd + toCopy;
    return frames;
}

// ── A/V sync ──────────────────────────────────────────────────────────────────

static void syncToPresentation(int64_t framePtsUs) {
    auto& s = impl();
    auto now = std::chrono::steady_clock::now();
    int64_t wallUs =
        std::chrono::duration_cast<std::chrono::microseconds>(now - s.clockBase).count();
    int64_t targetUs = framePtsUs - s.clockBaseUs;
    int64_t sleepUs  = targetUs - wallUs;

    while (sleepUs > 2000 && !s.stopDecode && !s.pauseRequested) {
        int64_t chunk = std::min(sleepUs, (int64_t)5000);
        std::this_thread::sleep_for(std::chrono::microseconds(chunk));
        now     = std::chrono::steady_clock::now();
        wallUs  = std::chrono::duration_cast<std::chrono::microseconds>(
                      now - s.clockBase).count();
        sleepUs = targetUs - wallUs;
    }
}

// ── Seek helpers ──────────────────────────────────────────────────────────────

// AVAssetReader cannot seek; we restart it from the target position.
// Returns a new reader positioned at targetUs, or nil on failure.
// The caller is responsible for releasing the old reader first.
static AVAssetReader* _makeReader(AVAsset* asset, int64_t startUs,
                                  AVAssetReaderTrackOutput** outVideo,
                                  AVAssetReaderTrackOutput** outAudio)
{
    CMTime startTime = CMTimeMake(startUs, 1000000);
    CMTime duration  = asset.duration;
    CMTime remaining = CMTimeSubtract(duration, startTime);
    if (CMTIME_IS_INVALID(remaining) || remaining.value <= 0)
        remaining = kCMTimeZero;

    CMTimeRange range = CMTimeRangeMake(startTime, remaining);

    NSError* err = nil;
    AVAssetReader* reader = [AVAssetReader assetReaderWithAsset:asset error:&err];
    if (!reader) {
        NSLog(@"[FluxVideo] AVAssetReader create failed: %@", err);
        return nil;
    }
    reader.timeRange = range;

    // ── Video output ──────────────────────────────────────────────────────
    NSArray<AVAssetTrack*>* videoTracks = [asset tracksWithMediaType:AVMediaTypeVideo];
    *outVideo = nil;
    if (videoTracks.count > 0) {
        NSDictionary* videoSettings = @{
            (NSString*)kCVPixelBufferPixelFormatTypeKey :
                @(kCVPixelFormatType_24RGB)
        };
        *outVideo = [AVAssetReaderTrackOutput
                        assetReaderTrackOutputWithTrack:videoTracks[0]
                        outputSettings:videoSettings];
        (*outVideo).alwaysCopiesSampleData = NO;
        if ([reader canAddOutput:*outVideo])
            [reader addOutput:*outVideo];
        else
            *outVideo = nil;
    }

    // ── Audio output ──────────────────────────────────────────────────────
    NSArray<AVAssetTrack*>* audioTracks = [asset tracksWithMediaType:AVMediaTypeAudio];
    *outAudio = nil;
    if (audioTracks.count > 0) {
        // Request linear PCM float32, interleaved, native sample rate
        NSDictionary* audioSettings = @{
            AVFormatIDKey         : @(kAudioFormatLinearPCM),
            AVLinearPCMBitDepthKey: @32,
            AVLinearPCMIsFloatKey : @YES,
            AVLinearPCMIsBigEndianKey      : @NO,
            AVLinearPCMIsNonInterleaved    : @NO,
        };
        *outAudio = [AVAssetReaderTrackOutput
                        assetReaderTrackOutputWithTrack:audioTracks[0]
                        outputSettings:audioSettings];
        (*outAudio).alwaysCopiesSampleData = NO;
        if ([reader canAddOutput:*outAudio])
            [reader addOutput:*outAudio];
        else
            *outAudio = nil;
    }

    if (![reader startReading]) {
        NSLog(@"[FluxVideo] AVAssetReader startReading failed: %@", reader.error);
        return nil;
    }
    return reader;
}

// ── Decode loop ───────────────────────────────────────────────────────────────

static void decodeLoop(std::string path) {
    @autoreleasepool {

    auto& s = impl();

    // ── Open asset ────────────────────────────────────────────────────────
    NSURL* url = nil;
    if (path.size() >= 1 && path[0] == '/') {
        url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()]];
    } else {
        // Treat as bundle-relative resource
        NSString* nsPath = [NSString stringWithUTF8String:path.c_str()];
        NSString* full   = [[NSBundle mainBundle] pathForResource:
                                [nsPath stringByDeletingPathExtension]
                            ofType:[nsPath pathExtension]];
        if (full)
            url = [NSURL fileURLWithPath:full];
        else
            url = [NSURL fileURLWithPath:nsPath]; // absolute fallback
    }

    if (!url) {
        s.state = FluxVideo::State::Error;
        return;
    }

    NSDictionary* opts = @{ AVURLAssetPreferPreciseDurationAndTimingKey: @YES };
    AVURLAsset* asset  = [AVURLAsset URLAssetWithURL:url options:opts];
    if (!asset) {
        s.state = FluxVideo::State::Error;
        return;
    }

    // Synchronously load asset properties (fine on background thread)
    dispatch_semaphore_t sem = dispatch_semaphore_create(0);
    __block BOOL loadOK = NO;
    [asset loadValuesAsynchronouslyForKeys:@[@"tracks", @"duration"]
                        completionHandler:^{
        loadOK = YES;
        dispatch_semaphore_signal(sem);
    }];
    dispatch_semaphore_wait(sem, dispatch_time(DISPATCH_TIME_NOW,
                                               (int64_t)(10 * NSEC_PER_SEC)));
    if (!loadOK) { s.state = FluxVideo::State::Error; return; }

    // Duration
    CMTime dur = asset.duration;
    if (CMTIME_IS_VALID(dur) && dur.timescale > 0)
        s.durationUs = (int64_t)(CMTimeGetSeconds(dur) * 1e6);

    // Video dimensions
    NSArray<AVAssetTrack*>* videoTracks = [asset tracksWithMediaType:AVMediaTypeVideo];
    if (videoTracks.count == 0) {
        NSLog(@"[FluxVideo] No video track in: %@", url);
        s.state = FluxVideo::State::Error;
        return;
    }
    CGSize natSize  = videoTracks[0].naturalSize;
    CGAffineTransform t = videoTracks[0].preferredTransform;
    // Account for rotation (portrait video shot on iPhone etc.)
    CGSize displaySize = CGSizeApplyAffineTransform(natSize, t);
    int vidW = (int)fabs(displaySize.width);
    int vidH = (int)fabs(displaySize.height);
    if (vidW <= 0 || vidH <= 0) { vidW = (int)natSize.width; vidH = (int)natSize.height; }
    s.videoWidth  = vidW;
    s.videoHeight = vidH;

    // Audio properties
    NSArray<AVAssetTrack*>* audioTracks = [asset tracksWithMediaType:AVMediaTypeAudio];
    if (audioTracks.count > 0) {
        // Read native sample rate / channel count from format descriptions
        NSArray* fmts = audioTracks[0].formatDescriptions;
        if (fmts.count > 0) {
            CMAudioFormatDescriptionRef fmt =
                (__bridge CMAudioFormatDescriptionRef)fmts[0];
            const AudioStreamBasicDescription* asbd =
                CMAudioFormatDescriptionGetStreamBasicDescription(fmt);
            if (asbd) {
                s.audioSampleRate = (int)asbd->mSampleRate;
                s.audioChannels   = (int)asbd->mChannelsPerFrame;
            }
        }
    }

    // ── Allocate frame buffer ─────────────────────────────────────────────
    {
        std::lock_guard<std::mutex> lk(s.frameMutex);
        s.frameWidth  = vidW;
        s.frameHeight = vidH;
        s.frameStride = vidW * 3;
        s.frameData.assign((size_t)(vidW * vidH * 3), 0);
    }

    // ── Allocate audio ring ───────────────────────────────────────────────
    bool hasAudio = (audioTracks.count > 0);
    if (hasAudio) {
        std::lock_guard<std::mutex> lk(s.audioMutex);
        s.audioBuf.assign(VideoState::kAudioRing, 0.f);
        s.audioWrite = 0;
        s.audioRead  = 0;
    }

    // ── Start audio stream ────────────────────────────────────────────────
    if (hasAudio) {
        FluxAudio::get().playStream(
            [](float* buf, int frames) -> int { return pullAudio(buf, frames); },
            s.audioSampleRate);
    }

    // ── Signal ready ──────────────────────────────────────────────────────
    if (s.readyCallback) s.readyCallback(vidW, vidH);

    // ── Build initial reader ──────────────────────────────────────────────
    AVAssetReaderTrackOutput* videoOut = nil;
    AVAssetReaderTrackOutput* audioOut = nil;
    AVAssetReader* reader = _makeReader(asset, 0, &videoOut, &audioOut);
    if (!reader) { s.state = FluxVideo::State::Error; return; }

    // ── Wait for first play() ─────────────────────────────────────────────
    s.state = FluxVideo::State::Paused;
    FluxAudio::get().pause();
    {
        std::unique_lock<std::mutex> lk(s.cmdMutex);
        s.pauseCV.wait(lk, [&s]{ return s.resumeRequested || s.stopDecode; });
        if (s.stopDecode) return;
        s.resumeRequested = false;
        s.state = FluxVideo::State::Playing;
        FluxAudio::get().resume();
    }

    s.clockBase   = std::chrono::steady_clock::now();
    s.clockBaseUs = 0;

    // ── Main decode loop ──────────────────────────────────────────────────
    while (true) {
        @autoreleasepool {

        // ── Command processing ────────────────────────────────────────────
        {
            std::unique_lock<std::mutex> lk(s.cmdMutex);
            if (s.stopDecode) break;

            if (s.seekPending) {
                int64_t target   = s.seekTargetUs;
                s.seekPending    = false;
                lk.unlock();

                // Tear down old reader, build new one from target position
                reader   = nil;
                videoOut = nil;
                audioOut = nil;

                {
                    std::lock_guard<std::mutex> alk(s.audioMutex);
                    s.audioWrite = 0; s.audioRead = 0;
                }
                FluxAudio::get().seekToSeconds((float)target / 1e6f);

                reader = _makeReader(asset, target, &videoOut, &audioOut);
                if (!reader) break;

                s.clockBase   = std::chrono::steady_clock::now();
                s.clockBaseUs = target;
                s.positionUs  = target;
                s.didFireFinish = false;
                continue;
            }

            if (s.pauseRequested) {
                s.pauseRequested = false;
                s.state = FluxVideo::State::Paused;
                FluxAudio::get().pause();

                s.pauseCV.wait(lk, [&s]{
                    return s.resumeRequested || s.stopDecode || s.seekPending;
                });
                if (s.stopDecode) break;

                if (s.seekPending) {
                    int64_t target = s.seekTargetUs;
                    s.seekPending  = false;
                    lk.unlock();

                    reader = nil; videoOut = nil; audioOut = nil;
                    {
                        std::lock_guard<std::mutex> alk(s.audioMutex);
                        s.audioWrite = 0; s.audioRead = 0;
                    }
                    FluxAudio::get().seekToSeconds((float)target / 1e6f);
                    reader = _makeReader(asset, target, &videoOut, &audioOut);
                    if (!reader) break;

                    s.clockBase   = std::chrono::steady_clock::now();
                    s.clockBaseUs = target;
                    s.positionUs  = target;
                    s.didFireFinish = false;
                    s.resumeRequested = false;
                    s.state = FluxVideo::State::Playing;
                    FluxAudio::get().resume();
                    continue;
                }

                s.resumeRequested = false;
                s.state = FluxVideo::State::Playing;
                FluxAudio::get().resume();
                s.clockBase   = std::chrono::steady_clock::now();
                s.clockBaseUs = s.positionUs.load();
            }
        }

        // ── Read one video sample ─────────────────────────────────────────
        if (videoOut) {
            CMSampleBufferRef sb = [videoOut copyNextSampleBuffer];
            if (sb) {
                CMTime pts     = CMSampleBufferGetPresentationTimeStamp(sb);
                int64_t ptsUs  = (int64_t)(CMTimeGetSeconds(pts) * 1e6);

                CVPixelBufferRef pb = CMSampleBufferGetImageBuffer(sb);
                if (pb) {
                    CVPixelBufferLockBaseAddress(pb, kCVPixelBufferLock_ReadOnly);
                    size_t w      = CVPixelBufferGetWidth(pb);
                    size_t h      = CVPixelBufferGetHeight(pb);
                    size_t stride = CVPixelBufferGetBytesPerRow(pb);
                    void*  base   = CVPixelBufferGetBaseAddress(pb);

                    if ((int)w == s.frameWidth && (int)h == s.frameHeight && base) {
                        syncToPresentation(ptsUs);
                        s.positionUs = ptsUs;

                        // Copy row by row (stride may have padding)
                        int expectedStride = s.frameWidth * 3;
                        std::vector<uint8_t> local((size_t)(s.frameWidth * s.frameHeight * 3));
                        if (stride == (size_t)expectedStride) {
                            memcpy(local.data(), base, local.size());
                        } else {
                            const uint8_t* src = (const uint8_t*)base;
                            uint8_t*       dst = local.data();
                            for (int row = 0; row < s.frameHeight; ++row) {
                                memcpy(dst, src, (size_t)expectedStride);
                                src += stride;
                                dst += expectedStride;
                            }
                        }

                        std::lock_guard<std::mutex> lk(s.frameMutex);
                        s.frameData = std::move(local);
                        s.newFrame  = true;
                    }

                    CVPixelBufferUnlockBaseAddress(pb, kCVPixelBufferLock_ReadOnly);
                }
                CFRelease(sb);
            } else {
                // Video track exhausted
                videoOut = nil;
            }
        }

        // ── Read one audio sample ─────────────────────────────────────────
        if (audioOut && hasAudio) {
            CMSampleBufferRef sb = [audioOut copyNextSampleBuffer];
            if (sb) {
                CMBlockBufferRef bb = CMSampleBufferGetDataBuffer(sb);
                if (bb) {
                    size_t totalLen = 0;
                    char*  dataPtr  = nullptr;
                    CMBlockBufferGetDataPointer(bb, 0, nullptr, &totalLen, &dataPtr);

                    int ch     = s.audioChannels;
                    int frames = (int)(totalLen / (sizeof(float) * (size_t)ch));
                    const float* pcm = (const float*)dataPtr;

                    std::lock_guard<std::mutex> lk(s.audioMutex);
                    int wr    = s.audioWrite, rd = s.audioRead;
                    int space = VideoState::kAudioRing - (wr - rd);
                    frames    = std::min(frames, space);

                    for (int i = 0; i < frames; i++) {
                        float sum = 0.f;
                        for (int c = 0; c < ch; c++) sum += pcm[i * ch + c];
                        float mono = std::max(-1.f, std::min(1.f, sum / (float)ch));
                        s.audioBuf[wr % VideoState::kAudioRing] = mono;
                        wr++;
                    }
                    s.audioWrite = wr;
                }
                CFRelease(sb);
            } else {
                audioOut = nil;
            }
        }

        // ── EOS: both tracks exhausted ────────────────────────────────────
        if (!videoOut && reader.status == AVAssetReaderStatusCompleted) {
            s.state = FluxVideo::State::Finished;
            bool expected = false;
            if (s.didFireFinish.compare_exchange_strong(expected, true))
                if (s.finishCallback) s.finishCallback();
            break;
        }

        } // @autoreleasepool inner
    }

    } // @autoreleasepool outer
}

// ============================================================================
// FluxVideo public methods
// ============================================================================

bool FluxVideo::open(const std::string& path) {
    close();
    auto& s = impl();
    s.state           = State::Loading;
    s.stopDecode      = false;
    s.seekPending     = false;
    s.didFireFinish   = false;
    s.pauseRequested  = false;
    s.resumeRequested = false;
    s.decodeThread    = std::thread(decodeLoop, path);
    return true;
}

void FluxVideo::play() {
    auto& s = impl();
    if (s.state == State::Paused || s.state == State::Loading) {
        {
            std::lock_guard<std::mutex> lk(s.cmdMutex);
            s.resumeRequested = true;
        }
        s.pauseCV.notify_all();
    }
}

void FluxVideo::pause() {
    auto& s = impl();
    if (s.state == State::Playing) {
        std::lock_guard<std::mutex> lk(s.cmdMutex);
        s.pauseRequested = true;
    }
}

void FluxVideo::seekToProgress(float p) {
    auto& s = impl();
    int64_t us = (int64_t)(std::max(0.f, std::min(1.f, p)) * s.durationUs.load());
    std::lock_guard<std::mutex> lk(s.cmdMutex);
    s.seekTargetUs = us; s.seekPending = true; s.pauseCV.notify_all();
}

void FluxVideo::seekToSeconds(float secs) {
    auto& s = impl();
    int64_t us = (int64_t)(secs * 1e6f);
    us = std::max<int64_t>(0, std::min(us, s.durationUs.load()));
    std::lock_guard<std::mutex> lk(s.cmdMutex);
    s.seekTargetUs = us; s.seekPending = true; s.pauseCV.notify_all();
}

void FluxVideo::close() {
    auto& s = impl();
    {
        std::lock_guard<std::mutex> lk(s.cmdMutex);
        s.stopDecode      = true;
        s.resumeRequested = true;
    }
    s.pauseCV.notify_all();

    if (s.decodeThread.joinable()) s.decodeThread.join();

    FluxAudio::get().stopPlayback();

    s.state       = State::Idle;
    s.positionUs  = 0;
    s.durationUs  = 0;
    s.newFrame    = false;
    s.videoWidth  = 0;
    s.videoHeight = 0;
    s.stopDecode  = false;
}

#endif // TARGET_OS_OSX
#endif // __APPLE__