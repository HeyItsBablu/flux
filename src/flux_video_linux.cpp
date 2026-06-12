// flux_video_linux.cpp
// FFmpeg A/V decode engine for FluxUI on Linux (non-Android).
//
// Pipeline:
//   AVFormatContext → AVCodecContext (video) → sws_scale → RGB24 frame buffer
//                  → AVCodecContext (audio)  → float PCM → FluxAudio ring buf
//
// CMakeLists: link libavformat libavcodec libavutil libswscale
//
#if defined(__linux__) && !defined(__ANDROID__)

#include "flux/flux_video.hpp"
#include "flux/flux_audio.hpp"

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

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

namespace
{
    struct VideoState
    {
        // ── Playback ──────────────────────────────────────────────────────────
        std::atomic<FluxVideo::State> state{FluxVideo::State::Idle};
        std::atomic<int64_t> positionUs{0};
        std::atomic<int64_t> durationUs{0};
        std::atomic<int> videoWidth{0};
        std::atomic<int> videoHeight{0};
        std::atomic<bool> newFrame{false};

        // ── Frame double-buffer ───────────────────────────────────────────────
        std::mutex frameMutex;
        std::vector<uint8_t> frameData;
        int frameWidth = 0;
        int frameHeight = 0;
        int frameStride = 0;

        // ── Audio ring buffer (~4 s at 48 kHz stereo worst-case) ──────────────
        static constexpr int kAudioRing = 48000 * 4 * 2;
        std::vector<float> audioBuf;
        int audioWrite = 0;
        int audioRead = 0;
        std::mutex audioMutex;
        int audioChannels = 1;
        int audioSampleRate = 44100;

        // ── Command channel ───────────────────────────────────────────────────
        std::mutex cmdMutex;
        std::condition_variable pauseCV;
        bool pauseRequested = false;
        bool resumeRequested = false;
        bool stopDecode = false;
        bool seekPending = false;
        int64_t seekTargetUs = 0;

        // ── Thread ────────────────────────────────────────────────────────────
        std::thread decodeThread;
        std::atomic<bool> didFireFinish{false};

        // ── Callbacks ─────────────────────────────────────────────────────────
        FluxVideo::FinishCallback finishCallback;
        std::function<void(int w, int h)> readyCallback;

        // ── Clock ─────────────────────────────────────────────────────────────
        std::chrono::steady_clock::time_point clockBase;
        int64_t clockBaseUs = 0;
    };
} // anonymous namespace

static VideoState &impl()
{
    static VideoState s;
    return s;
}

// ============================================================================
// FluxVideo — public API
// ============================================================================

FluxVideo &FluxVideo::get()
{
    static FluxVideo instance;
    return instance;
}
FluxVideo::~FluxVideo() { close(); }

FluxVideo::State FluxVideo::getState() const { return impl().state.load(); }
bool FluxVideo::isPlaying() const { return impl().state == State::Playing; }
bool FluxVideo::isPaused() const { return impl().state == State::Paused; }
bool FluxVideo::isFinished() const { return impl().state == State::Finished; }
int FluxVideo::getVideoWidth() const { return impl().videoWidth.load(); }
int FluxVideo::getVideoHeight() const { return impl().videoHeight.load(); }
bool FluxVideo::hasNewFrame() const { return impl().newFrame.load(); }

float FluxVideo::getDurationSeconds() const { return impl().durationUs.load() / 1e6f; }
float FluxVideo::getPositionSeconds() const { return impl().positionUs.load() / 1e6f; }
float FluxVideo::getProgress() const
{
    int64_t dur = impl().durationUs.load();
    return dur > 0 ? std::min(1.f, (float)impl().positionUs.load() / (float)dur) : 0.f;
}

void FluxVideo::setOnFinished(FinishCallback cb) { impl().finishCallback = std::move(cb); }
void FluxVideo::setOnReady(std::function<void(int w, int h)> cb) { impl().readyCallback = std::move(cb); }
void FluxVideo::setVolume(float v) { FluxAudio::get().setVolume(v); }
float FluxVideo::getVolume() const { return FluxAudio::get().getVolume(); }

FluxVideo::FrameLock FluxVideo::lockFrame()
{
    FrameLock fl;
    fl.lock = std::unique_lock<std::mutex>(impl().frameMutex);
    fl.data = impl().frameData.data();
    fl.width = impl().frameWidth;
    fl.height = impl().frameHeight;
    fl.stride = impl().frameStride;
    impl().newFrame = false;
    return fl;
}

// ── A/V sync ──────────────────────────────────────────────────────────────────

static void syncToPresentation(int64_t framePtsUs)
{
    auto &s = impl();
    auto now = std::chrono::steady_clock::now();
    int64_t wallUs =
        std::chrono::duration_cast<std::chrono::microseconds>(now - s.clockBase).count();
    int64_t targetUs = framePtsUs - s.clockBaseUs;
    int64_t sleepUs = targetUs - wallUs;

    while (sleepUs > 2000 && !s.stopDecode)
    {
        int64_t chunk = std::min(sleepUs, (int64_t)5000);
        std::this_thread::sleep_for(std::chrono::microseconds(chunk));
        now = std::chrono::steady_clock::now();
        wallUs = std::chrono::duration_cast<std::chrono::microseconds>(
                     now - s.clockBase)
                     .count();
        sleepUs = targetUs - wallUs;
    }
}

// ── Seek ──────────────────────────────────────────────────────────────────────

static void doSeek(AVFormatContext *fmtCtx,
                   AVCodecContext *vCtx,
                   AVCodecContext *aCtx,
                   int videoStreamIdx,
                   int audioStreamIdx,
                   int64_t targetUs)
{
    auto &s = impl();
    int64_t ts = av_rescale_q(targetUs, {1, 1000000},
                              fmtCtx->streams[videoStreamIdx]->time_base);
    av_seek_frame(fmtCtx, videoStreamIdx, ts, AVSEEK_FLAG_BACKWARD);

    avcodec_flush_buffers(vCtx);
    if (aCtx)
        avcodec_flush_buffers(aCtx);

    {
        std::lock_guard<std::mutex> lk(s.audioMutex);
        s.audioWrite = 0;
        s.audioRead = 0;
    }
    FluxAudio::get().seekToSeconds((float)targetUs / 1e6f);

    s.clockBase = std::chrono::steady_clock::now();
    s.clockBaseUs = targetUs;
    s.positionUs = targetUs;
    s.didFireFinish = false;
}

// ── Audio helpers ─────────────────────────────────────────────────────────────

static void processAudioFrame(AVFrame *frame)
{
    auto &s = impl();
    int ch = s.audioChannels;
    int frames = frame->nb_samples;
    bool isPlanar = av_sample_fmt_is_planar((AVSampleFormat)frame->format) != 0;
    bool isFloat = (frame->format == AV_SAMPLE_FMT_FLTP ||
                    frame->format == AV_SAMPLE_FMT_FLT);
    bool isS16 = (frame->format == AV_SAMPLE_FMT_S16P ||
                  frame->format == AV_SAMPLE_FMT_S16);

    std::lock_guard<std::mutex> lk(s.audioMutex);
    int wr = s.audioWrite, rd = s.audioRead;
    int space = VideoState::kAudioRing - (wr - rd);
    frames = std::min(frames, space);

    for (int i = 0; i < frames; i++)
    {
        float sum = 0.f;
        for (int c = 0; c < ch; c++)
        {
            if (isFloat)
            {
                if (isPlanar)
                    sum += ((const float *)frame->data[c])[i];
                else
                    sum += ((const float *)frame->data[0])[i * ch + c];
            }
            else if (isS16)
            {
                if (isPlanar)
                    sum += ((const int16_t *)frame->data[c])[i] / 32768.f;
                else
                    sum += ((const int16_t *)frame->data[0])[i * ch + c] / 32768.f;
            }
        }
        float mono = std::max(-1.f, std::min(1.f, sum / (float)ch));
        s.audioBuf[wr % VideoState::kAudioRing] = mono;
        wr++;
    }
    s.audioWrite = wr;
}

static int pullAudio(float *buf, int frames)
{
    auto &s = impl();
    std::lock_guard<std::mutex> lk(s.audioMutex);
    int rd = s.audioRead, wr = s.audioWrite;
    int avail = wr - rd;
    int toCopy = std::min(frames, avail);
    for (int i = 0; i < toCopy; i++)
        buf[i] = s.audioBuf[(size_t)((rd + i) % VideoState::kAudioRing)];
    for (int i = toCopy; i < frames; i++)
        buf[i] = 0.f;
    s.audioRead = rd + toCopy;
    return frames;
}

// ── Video frame processing ────────────────────────────────────────────────────

static void processVideoFrame(AVFrame *frame,
                              AVCodecContext *vCtx,
                              SwsContext *swsCtx,
                              uint8_t **rgbData,
                              int *rgbLinesize,
                              AVStream *vStream)
{
    auto &s = impl();
    int64_t pts = frame->best_effort_timestamp;
    if (pts == AV_NOPTS_VALUE)
        pts = frame->pts;
    int64_t ptsUs = av_rescale_q(pts, vStream->time_base, {1, 1000000});

    syncToPresentation(ptsUs);
    s.positionUs = ptsUs;

    sws_scale(swsCtx, frame->data, frame->linesize, 0, vCtx->height,
              rgbData, rgbLinesize);

    int w = s.frameWidth, h = s.frameHeight;
    int expected = w * h * 3;
    if (expected > 0)
    {
        std::vector<uint8_t> local(rgbData[0], rgbData[0] + expected);
        std::lock_guard<std::mutex> lk(s.frameMutex);
        s.frameData = std::move(local);
        s.newFrame = true;
    }
}

// ── Decode loop ───────────────────────────────────────────────────────────────

static void decodeLoop(std::string path)
{
    auto &s = impl();

    AVFormatContext *fmtCtx = nullptr;
    if (avformat_open_input(&fmtCtx, path.c_str(), nullptr, nullptr) < 0)
    {
        s.state = FluxVideo::State::Error;
        return;
    }
    if (avformat_find_stream_info(fmtCtx, nullptr) < 0)
    {
        avformat_close_input(&fmtCtx);
        s.state = FluxVideo::State::Error;
        return;
    }

    int videoStreamIdx = -1, audioStreamIdx = -1;
    for (unsigned i = 0; i < fmtCtx->nb_streams; i++)
    {
        AVMediaType type = fmtCtx->streams[i]->codecpar->codec_type;
        if (type == AVMEDIA_TYPE_VIDEO && videoStreamIdx < 0)
            videoStreamIdx = (int)i;
        if (type == AVMEDIA_TYPE_AUDIO && audioStreamIdx < 0)
            audioStreamIdx = (int)i;
    }
    if (videoStreamIdx < 0)
    {
        avformat_close_input(&fmtCtx);
        s.state = FluxVideo::State::Error;
        return;
    }

    if (fmtCtx->duration != AV_NOPTS_VALUE)
        s.durationUs = fmtCtx->duration;

    AVStream *vStream = fmtCtx->streams[videoStreamIdx];
    const AVCodec *vCodec = avcodec_find_decoder(vStream->codecpar->codec_id);
    AVCodecContext *vCtx = avcodec_alloc_context3(vCodec);
    avcodec_parameters_to_context(vCtx, vStream->codecpar);
    if (avcodec_open2(vCtx, vCodec, nullptr) < 0)
    {
        avcodec_free_context(&vCtx);
        avformat_close_input(&fmtCtx);
        s.state = FluxVideo::State::Error;
        return;
    }

    int vidW = vCtx->width, vidH = vCtx->height;
    s.videoWidth = vidW;
    s.videoHeight = vidH;

    SwsContext *swsCtx = sws_getContext(vidW, vidH, vCtx->pix_fmt,
                                        vidW, vidH, AV_PIX_FMT_RGB24,
                                        SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!swsCtx)
    {
        avcodec_free_context(&vCtx);
        avformat_close_input(&fmtCtx);
        s.state = FluxVideo::State::Error;
        return;
    }

    AVCodecContext *aCtx = nullptr;
    bool hasAudio = false;
    if (audioStreamIdx >= 0)
    {
        AVStream *aStream = fmtCtx->streams[audioStreamIdx];
        const AVCodec *aCodec = avcodec_find_decoder(aStream->codecpar->codec_id);
        aCtx = avcodec_alloc_context3(aCodec);
        avcodec_parameters_to_context(aCtx, aStream->codecpar);
        if (avcodec_open2(aCtx, aCodec, nullptr) == 0)
        {
            s.audioSampleRate = aCtx->sample_rate;
            s.audioChannels = aCtx->ch_layout.nb_channels;
            hasAudio = true;
        }
        else
        {
            avcodec_free_context(&aCtx);
            aCtx = nullptr;
        }
    }

    // Frame buffer
    {
        std::lock_guard<std::mutex> lk(s.frameMutex);
        s.frameWidth = vidW;
        s.frameHeight = vidH;
        s.frameStride = vidW * 3;
        s.frameData.assign((size_t)(vidW * vidH * 3), 0);
    }

    // Audio ring
    if (hasAudio)
    {
        std::lock_guard<std::mutex> lk(s.audioMutex);
        s.audioBuf.assign(VideoState::kAudioRing, 0.f);
        s.audioWrite = 0;
        s.audioRead = 0;
    }

    AVFrame *vFrame = av_frame_alloc();
    AVFrame *aFrame = av_frame_alloc();
    AVPacket *pkt = av_packet_alloc();

    std::vector<uint8_t> rgbBuf((size_t)(vidW * vidH * 3));
    uint8_t *rgbData[1] = {rgbBuf.data()};
    int rgbLinesize[1] = {vidW * 3};

    if (hasAudio)
    {
        FluxAudio::get().playStream(
            [](float *buf, int frames) -> int
            { return pullAudio(buf, frames); },
            s.audioSampleRate);
    }

    if (s.readyCallback)
        s.readyCallback(vidW, vidH);

    s.state = FluxVideo::State::Paused;
    FluxAudio::get().pause();

    {
        std::unique_lock<std::mutex> lk(s.cmdMutex);
        s.pauseCV.wait(lk, [&s]
                       { return s.resumeRequested || s.stopDecode; });
        if (s.stopDecode)
            goto cleanup;
        s.resumeRequested = false;
        s.state = FluxVideo::State::Playing;
        FluxAudio::get().resume();
    }

    s.clockBase = std::chrono::steady_clock::now();
    s.clockBaseUs = 0;

    while (true)
    {
        {
            std::unique_lock<std::mutex> lk(s.cmdMutex);
            if (s.stopDecode)
                break;

            if (s.seekPending)
            {
                int64_t target = s.seekTargetUs;
                s.seekPending = false;
                lk.unlock();
                doSeek(fmtCtx, vCtx, aCtx, videoStreamIdx, audioStreamIdx, target);
                continue;
            }

            if (s.pauseRequested)
            {
                s.pauseRequested = false;
                s.state = FluxVideo::State::Paused;
                FluxAudio::get().pause();

                s.pauseCV.wait(lk, [&s]
                               { return s.resumeRequested || s.stopDecode || s.seekPending; });
                if (s.stopDecode)
                    break;

                if (s.seekPending)
                {
                    int64_t target = s.seekTargetUs;
                    s.seekPending = false;
                    lk.unlock();
                    doSeek(fmtCtx, vCtx, aCtx, videoStreamIdx, audioStreamIdx, target);
                    continue;
                }

                s.resumeRequested = false;
                s.state = FluxVideo::State::Playing;
                FluxAudio::get().resume();
                s.clockBase = std::chrono::steady_clock::now();
                s.clockBaseUs = s.positionUs.load();
            }
        }

        int ret = av_read_frame(fmtCtx, pkt);
        if (ret == AVERROR_EOF)
        {
            avcodec_send_packet(vCtx, nullptr);
            while (avcodec_receive_frame(vCtx, vFrame) == 0)
                processVideoFrame(vFrame, vCtx, swsCtx, rgbData, rgbLinesize, vStream);

            s.state = FluxVideo::State::Finished;
            bool expected = false;
            if (s.didFireFinish.compare_exchange_strong(expected, true))
                if (s.finishCallback)
                    s.finishCallback();
            break;
        }
        if (ret < 0)
            break;

        if (pkt->stream_index == videoStreamIdx)
        {
            avcodec_send_packet(vCtx, pkt);
            while (avcodec_receive_frame(vCtx, vFrame) == 0)
                processVideoFrame(vFrame, vCtx, swsCtx, rgbData, rgbLinesize, vStream);
        }
        else if (pkt->stream_index == audioStreamIdx && hasAudio)
        {
            avcodec_send_packet(aCtx, pkt);
            while (avcodec_receive_frame(aCtx, aFrame) == 0)
                processAudioFrame(aFrame);
        }
        av_packet_unref(pkt);
    }

cleanup:
    av_packet_free(&pkt);
    av_frame_free(&vFrame);
    av_frame_free(&aFrame);
    sws_freeContext(swsCtx);
    avcodec_free_context(&vCtx);
    if (aCtx)
        avcodec_free_context(&aCtx);
    avformat_close_input(&fmtCtx);
}

// ============================================================================
// FluxVideo public methods
// ============================================================================

bool FluxVideo::open(const std::string &path)
{
    close();
    auto &s = impl();
    s.state = State::Loading;
    s.stopDecode = false;
    s.seekPending = false;
    s.didFireFinish = false;
    s.pauseRequested = false;
    s.resumeRequested = false;
    s.decodeThread = std::thread(decodeLoop, path);
    return true;
}

void FluxVideo::play()
{
    auto &s = impl();
    if (s.state == State::Paused || s.state == State::Loading)
    {
        {
            std::lock_guard<std::mutex> lk(s.cmdMutex);
            s.resumeRequested = true;
        }
        s.pauseCV.notify_all();
    }
}

void FluxVideo::pause()
{
    auto &s = impl();
    if (s.state == State::Playing)
    {
        std::lock_guard<std::mutex> lk(s.cmdMutex);
        s.pauseRequested = true;
    }
}

void FluxVideo::seekToProgress(float p)
{
    auto &s = impl();
    int64_t us = (int64_t)(std::max(0.f, std::min(1.f, p)) * s.durationUs.load());
    std::lock_guard<std::mutex> lk(s.cmdMutex);
    s.seekTargetUs = us;
    s.seekPending = true;
    s.pauseCV.notify_all();
}

void FluxVideo::seekToSeconds(float secs)
{
    auto &s = impl();
    int64_t us = (int64_t)(secs * 1e6f);
    us = std::max<int64_t>(0, std::min(us, s.durationUs.load()));
    std::lock_guard<std::mutex> lk(s.cmdMutex);
    s.seekTargetUs = us;
    s.seekPending = true;
    s.pauseCV.notify_all();
}

void FluxVideo::close()
{
    auto &s = impl();
    {
        std::lock_guard<std::mutex> lk(s.cmdMutex);
        s.stopDecode = true;
        s.resumeRequested = true;
    }
    s.pauseCV.notify_all();

    if (s.decodeThread.joinable())
        s.decodeThread.join();

    FluxAudio::get().stopPlayback();

    s.state = State::Idle;
    s.positionUs = 0;
    s.durationUs = 0;
    s.newFrame = false;
    s.videoWidth = 0;
    s.videoHeight = 0;
    s.stopDecode = false;
}

#endif // __linux__ && !__ANDROID__