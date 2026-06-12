// flux_video_android.cpp
// MediaCodec + SurfaceTexture + AAudio A/V sync engine for FluxUI on Android.
//
// Pipeline:
//   MediaExtractor → MediaCodec (video) → SurfaceTexture → GL_TEXTURE_EXTERNAL_OES
//                 └→ MediaCodec (audio) → PCM float      → AAudio (via FluxAudio)
//
// All decoding runs on s_decodeThread.
// The main GL thread calls updateFrame() once per vsync to latch a new texture
// and retrieve the current A/V-synced position.
//
// Dependencies (add to CMakeLists Android block):
//   mediandk   libmediandk.so
//   OpenSLES   libOpenSLES.so
//   EGL        libEGL.so
//   GLESv2     libGLESv2.so
//
#ifdef __ANDROID__

#include "flux/flux_video.hpp"
#include "flux/flux_audio.hpp"

#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#include <media/NdkMediaCodec.h>
#include <media/NdkMediaExtractor.h>
#include <media/NdkMediaFormat.h>

#include <android/asset_manager.h>
#include <android/log.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// ── External helpers provided by native-lib.cpp ───────────────────────────────
extern EGLDisplay      FluxAndroid_getEGLDisplay();
extern EGLContext      FluxAndroid_getEGLContext();
extern EGLSurface      FluxAndroid_getEGLSurface();
extern AAssetManager*  FluxAndroid_getAssetManager();

// JNI shims implemented in native-lib.cpp
extern void*           FluxVideo_createSurfaceTexture(GLuint texId);
extern void            FluxVideo_updateTexImage(void* surfaceTexture);
extern ANativeWindow*  FluxVideo_getNativeWindow(void* surfaceTexture);
extern void            FluxVideo_destroySurfaceTexture(void* surfaceTexture);

// ============================================================================
// Internal state (pimpl-in-anonymous-namespace pattern)
// All fields live here; FluxVideo methods delegate into this struct via
// the singleton held inside FluxVideo::get().
// ============================================================================

namespace {

struct VideoState {
    // ── Path ──────────────────────────────────────────────────────────────
    std::string path;

    // ── Playback ──────────────────────────────────────────────────────────
    std::atomic<FluxVideo::State> state{FluxVideo::State::Idle};
    std::atomic<int64_t> positionUs{0};
    std::atomic<int64_t> durationUs{0};
    std::atomic<int>     videoWidth{0};
    std::atomic<int>     videoHeight{0};

    // ── Frame sync ────────────────────────────────────────────────────────
    std::atomic<bool> pendingFrame{false};
    std::atomic<bool> newFrame{false};

    // ── Seek ──────────────────────────────────────────────────────────────
    std::atomic<bool>    seekPending{false};
    std::atomic<int64_t> seekTargetUs{0};

    // ── Decode thread ─────────────────────────────────────────────────────
    std::thread       decodeThread;
    std::atomic<bool> stopDecode{false};

    // ── Pause / resume ────────────────────────────────────────────────────
    std::mutex              pauseMutex;
    std::condition_variable pauseCV;
    std::atomic<bool>       resumeRequested{false};

    // ── MediaCodec ────────────────────────────────────────────────────────
    AMediaExtractor* extractor    = nullptr;
    AMediaCodec*     videoCodec   = nullptr;
    AMediaCodec*     audioCodec   = nullptr;
    int              videoTrackIdx = -1;
    int              audioTrackIdx = -1;

    // ── GL / EGL ──────────────────────────────────────────────────────────
    GLuint oesTexture     = 0;
    void*  surfaceTexture = nullptr;

    // ── Callbacks ─────────────────────────────────────────────────────────
    FluxVideo::FinishCallback          finishCallback;
    std::function<void(int w, int h)>  readyCallback;

    // ── PCM ring buffer ───────────────────────────────────────────────────
    static constexpr int kAudioRingSize = 48000 * 2; // ~2 s at 48 kHz
    std::vector<float>   audioBuf;
    std::atomic<int>     audioWrite{0};
    std::atomic<int>     audioRead{0};
    std::mutex           audioMutex;

    // ── Clock ─────────────────────────────────────────────────────────────
    std::chrono::steady_clock::time_point clockBase;
    int64_t clockBaseUs = 0;
};

} // anonymous namespace

// ============================================================================
// Singleton storage
// ============================================================================

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

FluxVideo::State FluxVideo::getState()           const { return impl().state.load(); }
bool             FluxVideo::isPlaying()          const { return impl().state == State::Playing; }
bool             FluxVideo::isPaused()           const { return impl().state == State::Paused; }
bool             FluxVideo::isFinished()         const { return impl().state == State::Finished; }
int              FluxVideo::getVideoWidth()      const { return impl().videoWidth.load(); }
int              FluxVideo::getVideoHeight()     const { return impl().videoHeight.load(); }
bool             FluxVideo::hasNewFrame()        const { return impl().newFrame.load(); }
GLuint           FluxVideo::getTextureId()       const { return impl().oesTexture; }

float FluxVideo::getDurationSeconds() const { return impl().durationUs.load() / 1e6f; }
float FluxVideo::getPositionSeconds() const { return impl().positionUs.load() / 1e6f; }
float FluxVideo::getProgress() const {
    int64_t dur = impl().durationUs.load();
    return (dur > 0) ? std::min(1.f, (float)impl().positionUs.load() / dur) : 0.f;
}

void FluxVideo::setOnFinished(FinishCallback cb)                     { impl().finishCallback = std::move(cb); }
void FluxVideo::setOnReady(std::function<void(int w, int h)> cb)     { impl().readyCallback  = std::move(cb); }
void FluxVideo::setVolume(float v)                                   { FluxAudio::get().setVolume(v); }
float FluxVideo::getVolume() const                                   { return FluxAudio::get().getVolume(); }

// ── OES texture ───────────────────────────────────────────────────────────────

static bool createOESTexture() {
    auto& s = impl();
    glGenTextures(1, &s.oesTexture);
    if (!s.oesTexture) {
        __android_log_print(ANDROID_LOG_ERROR, "FluxVideo", "glGenTextures failed");
        return false;
    }
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, s.oesTexture);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, 0);

    s.surfaceTexture = FluxVideo_createSurfaceTexture(s.oesTexture);
    if (!s.surfaceTexture) {
        __android_log_print(ANDROID_LOG_ERROR, "FluxVideo", "createSurfaceTexture failed");
        return false;
    }
    return true;
}

// ── File / track loading ──────────────────────────────────────────────────────

static int openExtractor() {
    auto& s = impl();

    if (!s.path.empty() && s.path[0] == '/') {
        s.extractor = AMediaExtractor_new();
        if (AMediaExtractor_setDataSource(s.extractor, s.path.c_str()) != AMEDIA_OK) {
            __android_log_print(ANDROID_LOG_ERROR, "FluxVideo",
                                "setDataSource failed: %s", s.path.c_str());
            AMediaExtractor_delete(s.extractor);
            s.extractor = nullptr;
            return -1;
        }
    } else {
        AAssetManager* am = FluxAndroid_getAssetManager();
        if (!am) {
            __android_log_print(ANDROID_LOG_ERROR, "FluxVideo", "AAssetManager is null");
            return -1;
        }
        AAsset* asset = AAssetManager_open(am, s.path.c_str(), AASSET_MODE_STREAMING);
        if (!asset) {
            __android_log_print(ANDROID_LOG_ERROR, "FluxVideo",
                                "AAssetManager_open failed: %s", s.path.c_str());
            return -1;
        }
        off_t start = 0, len = 0;
        int fd = AAsset_openFileDescriptor(asset, &start, &len);
        AAsset_close(asset);
        if (fd < 0) {
            __android_log_print(ANDROID_LOG_ERROR, "FluxVideo",
                                "AAsset_openFileDescriptor failed");
            return -1;
        }
        s.extractor = AMediaExtractor_new();
        media_status_t st = AMediaExtractor_setDataSourceFd(s.extractor, fd, start, len);
        if (st != AMEDIA_OK) {
            __android_log_print(ANDROID_LOG_ERROR, "FluxVideo",
                                "setDataSourceFd failed: %d", (int)st);
            AMediaExtractor_delete(s.extractor);
            s.extractor = nullptr;
            return -1;
        }
    }

    int trackCount = (int)AMediaExtractor_getTrackCount(s.extractor);
    for (int i = 0; i < trackCount; i++) {
        AMediaFormat* fmt  = AMediaExtractor_getTrackFormat(s.extractor, i);
        const char*   mime = nullptr;
        AMediaFormat_getString(fmt, AMEDIAFORMAT_KEY_MIME, &mime);
        if (mime) {
            if (strncmp(mime, "video/", 6) == 0 && s.videoTrackIdx < 0)
                s.videoTrackIdx = i;
            if (strncmp(mime, "audio/", 6) == 0 && s.audioTrackIdx < 0)
                s.audioTrackIdx = i;
        }
        int64_t dur = 0;
        if (AMediaFormat_getInt64(fmt, AMEDIAFORMAT_KEY_DURATION, &dur) && dur > 0)
            s.durationUs.store(dur);
        AMediaFormat_delete(fmt);
    }

    if (s.videoTrackIdx < 0) {
        __android_log_print(ANDROID_LOG_ERROR, "FluxVideo", "No video track found");
        return -1;
    }
    return 0;
}

// ── Codec helpers ─────────────────────────────────────────────────────────────

static bool openVideoCodec() {
    auto& s = impl();
    AMediaFormat* fmt  = AMediaExtractor_getTrackFormat(s.extractor, s.videoTrackIdx);
    const char*   mime = nullptr;
    AMediaFormat_getString(fmt, AMEDIAFORMAT_KEY_MIME, &mime);

    int32_t w = 0, h = 0;
    AMediaFormat_getInt32(fmt, AMEDIAFORMAT_KEY_WIDTH,  &w);
    AMediaFormat_getInt32(fmt, AMEDIAFORMAT_KEY_HEIGHT, &h);
    s.videoWidth  = w;
    s.videoHeight = h;

    s.videoCodec = AMediaCodec_createDecoderByType(mime);
    if (!s.videoCodec) {
        AMediaFormat_delete(fmt);
        return false;
    }
    ANativeWindow* window = FluxVideo_getNativeWindow(s.surfaceTexture);
    if (!window) {
        AMediaFormat_delete(fmt);
        return false;
    }
    media_status_t status = AMediaCodec_configure(s.videoCodec, fmt, window, nullptr, 0);
    AMediaFormat_delete(fmt);
    if (status != AMEDIA_OK) return false;

    AMediaExtractor_selectTrack(s.extractor, s.videoTrackIdx);
    AMediaCodec_start(s.videoCodec);
    return true;
}

static bool openAudioCodec(int& outSampleRate, int& outChannels) {
    auto& s = impl();
    if (s.audioTrackIdx < 0) return false;

    AMediaFormat* fmt  = AMediaExtractor_getTrackFormat(s.extractor, s.audioTrackIdx);
    const char*   mime = nullptr;
    AMediaFormat_getString(fmt, AMEDIAFORMAT_KEY_MIME, &mime);

    int32_t sr = 44100, ch = 1;
    AMediaFormat_getInt32(fmt, AMEDIAFORMAT_KEY_SAMPLE_RATE,   &sr);
    AMediaFormat_getInt32(fmt, AMEDIAFORMAT_KEY_CHANNEL_COUNT, &ch);
    outSampleRate = sr;
    outChannels   = ch;

    s.audioCodec = AMediaCodec_createDecoderByType(mime);
    if (!s.audioCodec) { AMediaFormat_delete(fmt); return false; }

    AMediaCodec_configure(s.audioCodec, fmt, nullptr, nullptr, 0);
    AMediaFormat_delete(fmt);

    AMediaExtractor_selectTrack(s.extractor, s.audioTrackIdx);
    AMediaCodec_start(s.audioCodec);

    s.audioBuf.assign(VideoState::kAudioRingSize, 0.f);
    s.audioWrite = 0;
    s.audioRead  = 0;
    return true;
}

static void destroyVideoCodec() {
    auto& s = impl();
    if (s.videoCodec) {
        AMediaCodec_stop(s.videoCodec);
        AMediaCodec_delete(s.videoCodec);
        s.videoCodec = nullptr;
    }
}
static void destroyAudioCodec() {
    auto& s = impl();
    if (s.audioCodec) {
        AMediaCodec_stop(s.audioCodec);
        AMediaCodec_delete(s.audioCodec);
        s.audioCodec = nullptr;
    }
}

// ── Seek ──────────────────────────────────────────────────────────────────────

static void doSeek(int64_t targetUs) {
    auto& s = impl();
    AMediaCodec_flush(s.videoCodec);
    if (s.audioCodec) AMediaCodec_flush(s.audioCodec);

    AMediaExtractor_seekTo(s.extractor, targetUs,
                           AMEDIAEXTRACTOR_SEEK_PREVIOUS_SYNC);
    {
        std::lock_guard<std::mutex> lk(s.audioMutex);
        s.audioWrite = 0;
        s.audioRead  = 0;
    }
    s.clockBaseUs = targetUs;
    s.clockBase   = std::chrono::steady_clock::now();
    s.positionUs  = targetUs;
    FluxAudio::get().seekToSeconds(targetUs / 1e6f);
    s.seekPending = false;
}

// ── A/V sync ──────────────────────────────────────────────────────────────────

static void syncToPresentation(int64_t framePtsUs) {
    auto& s = impl();
    auto    now     = std::chrono::steady_clock::now();
    int64_t wallUs  = std::chrono::duration_cast<std::chrono::microseconds>(
                          now - s.clockBase).count();
    int64_t targetUs = framePtsUs - s.clockBaseUs;
    int64_t sleepUs  = targetUs - wallUs;

    while (sleepUs > 2000 && !s.stopDecode &&
           s.state != FluxVideo::State::Paused) {
        int64_t chunk = std::min(sleepUs, (int64_t)5000);
        std::this_thread::sleep_for(std::chrono::microseconds(chunk));
        now     = std::chrono::steady_clock::now();
        wallUs  = std::chrono::duration_cast<std::chrono::microseconds>(
                      now - s.clockBase).count();
        sleepUs = targetUs - wallUs;
    }
}

// ── Audio codec helpers ───────────────────────────────────────────────────────

static void feedAudioCodec() {
    auto& s = impl();
    if (!s.audioCodec) return;

    ssize_t inBuf = AMediaCodec_dequeueInputBuffer(s.audioCodec, 1000);
    if (inBuf < 0) return;

    size_t   bufSize = 0;
    uint8_t* data    = AMediaCodec_getInputBuffer(s.audioCodec, (size_t)inBuf, &bufSize);

    ssize_t sampleSize = AMediaExtractor_readSampleData(s.extractor, data, bufSize);
    int64_t pts        = AMediaExtractor_getSampleTime(s.extractor);

    if (sampleSize > 0) {
        AMediaCodec_queueInputBuffer(s.audioCodec, (size_t)inBuf,
                                     0, (size_t)sampleSize, pts, 0);
        AMediaExtractor_advance(s.extractor);
    } else {
        AMediaCodec_queueInputBuffer(s.audioCodec, (size_t)inBuf, 0, 0, 0,
                                     AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
    }
}

static void drainAudioCodec(int channels) {
    auto& s = impl();
    if (!s.audioCodec) return;

    AMediaCodecBufferInfo info{};
    ssize_t outBuf = AMediaCodec_dequeueOutputBuffer(s.audioCodec, &info, 0);
    if (outBuf < 0) return;

    if (info.size > 0) {
        size_t   sz   = 0;
        uint8_t* data = AMediaCodec_getOutputBuffer(s.audioCodec, (size_t)outBuf, &sz);

        int            frameCount = info.size / (2 * channels);
        const int16_t* pcm16      = reinterpret_cast<const int16_t*>(data + info.offset);

        std::lock_guard<std::mutex> lk(s.audioMutex);
        int wr    = s.audioWrite.load();
        int rd    = s.audioRead.load();
        int space = VideoState::kAudioRingSize - (wr - rd);
        frameCount = std::min(frameCount, space);

        for (int i = 0; i < frameCount; i++) {
            float sum = 0.f;
            for (int c = 0; c < channels; c++)
                sum += pcm16[i * channels + c] / 32768.f;
            float mono = std::max(-1.f, std::min(1.f, sum / channels));
            s.audioBuf[wr % VideoState::kAudioRingSize] = mono;
            wr++;
        }
        s.audioWrite = wr;
    }
    AMediaCodec_releaseOutputBuffer(s.audioCodec, (size_t)outBuf, false);
}

static int pullAudio(float* buf, int frames, int /*channels*/) {
    auto& s = impl();
    std::lock_guard<std::mutex> lk(s.audioMutex);
    int rd    = s.audioRead.load();
    int wr    = s.audioWrite.load();
    int avail = wr - rd;
    int toCopy = std::min(frames, avail);

    for (int i = 0; i < toCopy; i++)
        buf[i] = s.audioBuf[(rd + i) % VideoState::kAudioRingSize];
    for (int i = toCopy; i < frames; i++)
        buf[i] = 0.f;

    s.audioRead = rd + toCopy;
    return frames;
}

// ── Decode loop ───────────────────────────────────────────────────────────────

static void decodeLoop() {
    auto& s = impl();

    if (openExtractor() < 0)  { s.state = FluxVideo::State::Error; return; }
    if (!openVideoCodec())    { s.state = FluxVideo::State::Error; return; }

    int  audioSR = 44100, audioCh = 1;
    bool hasAudio = openAudioCodec(audioSR, audioCh);

    if (hasAudio) {
        FluxAudio::get().playStream(
            [audioCh](float* buf, int frames) -> int {
                return pullAudio(buf, frames, audioCh);
            },
            audioSR);
    }

    if (s.readyCallback)
        s.readyCallback(s.videoWidth.load(), s.videoHeight.load());

    s.state = FluxVideo::State::Paused;
    s.clockBase   = std::chrono::steady_clock::now();
    s.clockBaseUs = 0;

    {
        std::unique_lock<std::mutex> lk(s.pauseMutex);
        s.pauseCV.wait(lk, [&s] {
            return s.resumeRequested.load() || s.stopDecode.load();
        });
        if (s.stopDecode) return;
        s.resumeRequested = false;
    }

    s.clockBase   = std::chrono::steady_clock::now();
    s.clockBaseUs = 0;

    bool inputEOS  = false;
    bool outputEOS = false;
    static constexpr int64_t kTimeoutUs = 5000;

    while (!s.stopDecode && !outputEOS) {

        if (s.seekPending.load()) {
            doSeek(s.seekTargetUs.load());
            inputEOS  = false;
            outputEOS = false;
        }

        if (s.state == FluxVideo::State::Paused) {
            std::unique_lock<std::mutex> lk(s.pauseMutex);
            s.pauseCV.wait(lk, [&s] {
                return s.resumeRequested.load() ||
                       s.stopDecode.load()      ||
                       s.seekPending.load();
            });
            if (s.stopDecode) break;
            if (s.seekPending.load()) continue;
            s.resumeRequested = false;
            s.clockBase   = std::chrono::steady_clock::now();
            s.clockBaseUs = s.positionUs.load();
            continue;
        }

        // Feed video codec input
        if (!inputEOS) {
            ssize_t inBuf = AMediaCodec_dequeueInputBuffer(s.videoCodec, kTimeoutUs);
            if (inBuf >= 0) {
                size_t   bufSize = 0;
                uint8_t* data    = AMediaCodec_getInputBuffer(
                                       s.videoCodec, (size_t)inBuf, &bufSize);
                int trackIdx = (int)AMediaExtractor_getSampleTrackIndex(s.extractor);

                if (trackIdx == s.videoTrackIdx) {
                    ssize_t sampleSize = AMediaExtractor_readSampleData(
                                             s.extractor, data, bufSize);
                    int64_t pts = AMediaExtractor_getSampleTime(s.extractor);
                    if (sampleSize > 0) {
                        AMediaCodec_queueInputBuffer(s.videoCodec, (size_t)inBuf,
                                                     0, (size_t)sampleSize, pts, 0);
                        AMediaExtractor_advance(s.extractor);
                    } else {
                        AMediaCodec_queueInputBuffer(s.videoCodec, (size_t)inBuf,
                                                     0, 0, 0,
                                                     AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
                        inputEOS = true;
                    }
                } else if (trackIdx == s.audioTrackIdx && hasAudio) {
                    feedAudioCodec();
                    AMediaCodec_queueInputBuffer(s.videoCodec, (size_t)inBuf, 0, 0, 0, 0);
                } else {
                    if (trackIdx >= 0) AMediaExtractor_advance(s.extractor);
                    AMediaCodec_queueInputBuffer(s.videoCodec, (size_t)inBuf, 0, 0, 0, 0);
                }
            }
        }

        // Drain video output
        AMediaCodecBufferInfo info{};
        ssize_t outBuf = AMediaCodec_dequeueOutputBuffer(s.videoCodec, &info, kTimeoutUs);
        if (outBuf >= 0) {
            bool isEOS = (info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) != 0;
            if (info.size > 0) {
                syncToPresentation(info.presentationTimeUs);
                s.positionUs = info.presentationTimeUs;
                AMediaCodec_releaseOutputBuffer(s.videoCodec, (size_t)outBuf, true);
                s.pendingFrame = true;
            } else {
                AMediaCodec_releaseOutputBuffer(s.videoCodec, (size_t)outBuf, false);
            }
            if (isEOS) {
                outputEOS = true;
                s.state   = FluxVideo::State::Finished;
                if (s.finishCallback) s.finishCallback();
            }
        } else if (outBuf == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
            AMediaFormat* fmt = AMediaCodec_getOutputFormat(s.videoCodec);
            int32_t w = 0, h = 0;
            AMediaFormat_getInt32(fmt, AMEDIAFORMAT_KEY_WIDTH,  &w);
            AMediaFormat_getInt32(fmt, AMEDIAFORMAT_KEY_HEIGHT, &h);
            if (w > 0 && h > 0) { s.videoWidth = w; s.videoHeight = h; }
            AMediaFormat_delete(fmt);
        }

        if (hasAudio) drainAudioCodec(audioCh);
    }
}

// ============================================================================
// FluxVideo public methods
// ============================================================================

bool FluxVideo::open(const std::string& path) {
    close();
    auto& s  = impl();
    s.state  = State::Loading;
    s.path   = path;

    if (!createOESTexture()) return false;

    s.stopDecode = false;
    s.decodeThread = std::thread(decodeLoop);
    return true;
}

void FluxVideo::play() {
    auto& s = impl();
    if (s.state == State::Paused || s.state == State::Loading) {
        {
            std::lock_guard<std::mutex> lk(s.pauseMutex);
            s.resumeRequested = true;
        }
        s.state = State::Playing;
        s.pauseCV.notify_all();
        FluxAudio::get().resume();
    }
}

void FluxVideo::pause() {
    auto& s = impl();
    if (s.state == State::Playing) {
        s.state = State::Paused;
        FluxAudio::get().pause();
    }
}

void FluxVideo::seekToProgress(float p) {
    int64_t us = (int64_t)(std::max(0.f, std::min(1.f, p)) * impl().durationUs.load());
    auto& s = impl();
    s.seekTargetUs = us;
    s.seekPending  = true;
    s.pauseCV.notify_all();
}

void FluxVideo::seekToSeconds(float secs) {
    auto& s = impl();
    int64_t us = (int64_t)(secs * 1e6f);
    us = std::max<int64_t>(0, std::min(us, s.durationUs.load()));
    s.seekTargetUs = us;
    s.seekPending  = true;
    s.pauseCV.notify_all();
}

bool FluxVideo::updateFrame() {
    auto& s = impl();
    if (!s.surfaceTexture || !s.pendingFrame.load()) {
        s.newFrame = false;
        return false;
    }
    FluxVideo_updateTexImage(s.surfaceTexture);
    s.pendingFrame = false;
    s.newFrame     = true;
    return true;
}

void FluxVideo::close() {
    auto& s = impl();
    {
        std::lock_guard<std::mutex> lk(s.pauseMutex);
        s.resumeRequested = true;
    }
    s.stopDecode = true;
    s.pauseCV.notify_all();

    if (s.decodeThread.joinable()) s.decodeThread.join();

    destroyAudioCodec();
    destroyVideoCodec();

    if (s.extractor) {
        AMediaExtractor_delete(s.extractor);
        s.extractor = nullptr;
    }
    if (s.oesTexture) {
        glDeleteTextures(1, &s.oesTexture);
        s.oesTexture = 0;
    }
    if (s.surfaceTexture) {
        FluxVideo_destroySurfaceTexture(s.surfaceTexture);
        s.surfaceTexture = nullptr;
    }

    s.state           = State::Idle;
    s.positionUs      = 0;
    s.durationUs      = 0;
    s.pendingFrame    = false;
    s.newFrame        = false;
    s.seekPending     = false;
    s.videoWidth      = 0;
    s.videoHeight     = 0;
    s.stopDecode      = false;
    s.resumeRequested = false;
    s.videoTrackIdx   = -1;
    s.audioTrackIdx   = -1;
}

#endif // __ANDROID__