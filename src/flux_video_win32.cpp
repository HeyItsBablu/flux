// flux_video_win32.cpp
// Media Foundation A/V decode engine for FluxUI on Windows.
//
// Pipeline:
//   IMFSourceReader (any_stream) → video: RGB24 → CPU frame buffer
//                               → audio: PCM float → FluxAudio ring buffer
//
// The decode thread reads one sample per iteration; the UI thread blits the
// frame buffer via StretchDIBits inside VideoPlayerWidget.
//
// Dependencies (linked automatically via #pragma comment):
//   mfplat.lib  mfreadwrite.lib  mfuuid.lib  ole32.lib  propsys.lib
//
#ifdef _WIN32

#include "flux/flux_video.hpp"
#include "flux/flux_audio.hpp"

#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mftransform.h>
#include <windows.h>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "propsys.lib")

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// ── One-time COM / MF initialisation ──────────────────────────────────────────
namespace
{
    struct MFInit
    {
        MFInit()
        {
            HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            if (FAILED(hr) && hr != RPC_E_CHANGED_MODE)
                fprintf(stderr, "[FluxVideo] CoInitializeEx failed: 0x%08X\n", (unsigned)hr);
            hr = MFStartup(MF_VERSION);
            if (FAILED(hr))
                fprintf(stderr, "[FluxVideo] MFStartup failed: 0x%08X\n", (unsigned)hr);
        }
        ~MFInit()
        {
            MFShutdown();
            CoUninitialize();
        }
    };
    static MFInit s_mfInit;
} // namespace

// ============================================================================
// Internal state
// ============================================================================

namespace
{
    struct VideoState
    {
        // ── Playback ──────────────────────────────────────────────────────────
        std::atomic<FluxVideo::State> state{FluxVideo::State::Idle};
        std::atomic<int64_t> positionHns{0}; // 100-ns units (HNS)
        std::atomic<int64_t> durationHns{0};
        std::atomic<int> videoWidth{0};
        std::atomic<int> videoHeight{0};

        // ── Frame double-buffer ───────────────────────────────────────────────
        std::mutex frameMutex;
        std::vector<uint8_t> frameData;
        int frameWidth = 0;
        int frameHeight = 0;
        int frameStride = 0;
        std::atomic<bool> newFrame{false};

        // ── EOS tracking ──────────────────────────────────────────────────────
        bool videoEOS = false;
        bool audioEOS = false;

        // ── Audio ring buffer (~4 s at 48 kHz mono) ───────────────────────────
        static constexpr int kAudioRing = 48000 * 4;
        std::vector<float> audioBuf;
        int audioWrite = 0;
        int audioRead = 0;
        std::mutex audioMutex;
        int audioChannels = 1;
        int audioSampleRate = 44100;

        // ── Command channel ───────────────────────────────────────────────────
        std::mutex cmdMutex;
        std::condition_variable cmdCV;
        std::condition_variable pauseCV;
        bool pauseRequested = false;
        bool resumeRequested = false;
        bool stopDecode = false;
        bool seekPending = false;
        int64_t seekTargetHns = 0;

        // ── Thread ────────────────────────────────────────────────────────────
        std::thread decodeThread;
        std::atomic<bool> didFireFinish{false};

        // ── Callbacks ─────────────────────────────────────────────────────────
        FluxVideo::FinishCallback finishCallback;
        std::function<void(int w, int h)> readyCallback;

        // ── Clock ─────────────────────────────────────────────────────────────
        std::chrono::steady_clock::time_point clockBase;
        int64_t clockBaseHns = 0;
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

FluxVideo::State FluxVideo::getState() const { return impl().state.load(); }
bool FluxVideo::isPlaying() const { return impl().state == State::Playing; }
bool FluxVideo::isPaused() const { return impl().state == State::Paused; }
bool FluxVideo::isFinished() const { return impl().state == State::Finished; }
int FluxVideo::getVideoWidth() const { return impl().videoWidth.load(); }
int FluxVideo::getVideoHeight() const { return impl().videoHeight.load(); }
bool FluxVideo::hasNewFrame() const { return impl().newFrame.load(); }

float FluxVideo::getDurationSeconds() const { return (float)impl().durationHns.load() / 1e7f; }
float FluxVideo::getPositionSeconds() const { return (float)impl().positionHns.load() / 1e7f; }
float FluxVideo::getProgress() const
{
    int64_t dur = impl().durationHns.load();
    return dur > 0 ? std::min(1.f, (float)impl().positionHns.load() / (float)dur) : 0.f;
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

// ── Audio helper ─────────────────────────────────────────────────────────────

static bool configureAudio(IMFSourceReader *reader)
{
    auto &s = impl();
    reader->SetStreamSelection((DWORD)MF_SOURCE_READER_ALL_STREAMS, FALSE);
    reader->SetStreamSelection((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, TRUE);
    reader->SetStreamSelection((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, TRUE);

    IMFMediaType *audioType = nullptr;
    MFCreateMediaType(&audioType);
    audioType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    audioType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_Float);
    audioType->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 32);

    HRESULT hr = reader->SetCurrentMediaType(
        (DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, audioType);
    audioType->Release();

    if (FAILED(hr))
    {
        reader->SetStreamSelection((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, FALSE);
        return false;
    }

    IMFMediaType *actual = nullptr;
    reader->GetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, &actual);
    if (actual)
    {
        UINT32 sr = 44100, ch = 2;
        actual->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &sr);
        actual->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &ch);
        s.audioSampleRate = (int)sr;
        s.audioChannels = (int)ch;
        actual->Release();
    }
    return true;
}

// ── Seek ──────────────────────────────────────────────────────────────────────

static void doSeek(IMFSourceReader *reader, int64_t targetHns)
{
    auto &s = impl();
    PROPVARIANT pv;
    PropVariantInit(&pv);
    pv.vt = VT_I8;
    pv.hVal.QuadPart = targetHns;
    reader->SetCurrentPosition(GUID_NULL, pv);
    PropVariantClear(&pv);

    {
        std::lock_guard<std::mutex> lk(s.audioMutex);
        s.audioWrite = 0;
        s.audioRead = 0;
    }
    FluxAudio::get().seekToSeconds((float)targetHns / 1e7f);

    s.clockBase = std::chrono::steady_clock::now();
    s.clockBaseHns = targetHns;
    s.positionHns = targetHns;
    s.videoEOS = false;
    s.audioEOS = false;
    s.didFireFinish = false;
}

// ── A/V sync ──────────────────────────────────────────────────────────────────

static void syncToPresentation(int64_t framePtsHns)
{
    auto &s = impl();
    auto now = std::chrono::steady_clock::now();
    int64_t wallHns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(now - s.clockBase).count() / 100;
    int64_t targetHns = framePtsHns - s.clockBaseHns;
    int64_t sleepHns = targetHns - wallHns;

    // 5 ms chunks (50 000 HNS) — keeps stop/seek responsive
    while (sleepHns > 20000 && !s.stopDecode && !s.pauseRequested)
    {
        int64_t chunk = std::min(sleepHns, (int64_t)50000);
        std::this_thread::sleep_for(std::chrono::nanoseconds(chunk * 100));
        now = std::chrono::steady_clock::now();
        wallHns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                      now - s.clockBase)
                      .count() /
                  100;
        sleepHns = targetHns - wallHns;
    }
}

// ── Sample processing ─────────────────────────────────────────────────────────

static void processVideoSample(IMFSample *sample, LONGLONG timestamp)
{
    auto &s = impl();
    syncToPresentation(timestamp);
    s.positionHns = timestamp;

    IMFMediaBuffer *buf = nullptr;
    if (FAILED(sample->ConvertToContiguousBuffer(&buf)))
        return;

    BYTE *data = nullptr;
    DWORD maxLen = 0, curLen = 0;
    if (SUCCEEDED(buf->Lock(&data, &maxLen, &curLen)))
    {
        int w = s.frameWidth, h = s.frameHeight;
        int expected = w * h * 3;
        if ((int)curLen >= expected && expected > 0)
        {
            std::vector<uint8_t> local(expected);
            memcpy(local.data(), data, expected);
            buf->Unlock();
            buf->Release();
            std::lock_guard<std::mutex> lk(s.frameMutex);
            s.frameData = std::move(local);
            s.newFrame = true;
            return;
        }
        buf->Unlock();
    }
    buf->Release();
}

static void processAudioSample(IMFSample *sample)
{
    auto &s = impl();
    IMFMediaBuffer *buf = nullptr;
    if (FAILED(sample->ConvertToContiguousBuffer(&buf)))
        return;

    BYTE *data = nullptr;
    DWORD maxLen = 0, curLen = 0;
    if (SUCCEEDED(buf->Lock(&data, &maxLen, &curLen)))
    {
        int floatCount = (int)(curLen / sizeof(float));
        const float *pcm = reinterpret_cast<const float *>(data);

        std::lock_guard<std::mutex> lk(s.audioMutex);
        int ch = s.audioChannels;
        int frames = floatCount / ch;
        int wr = s.audioWrite, rd = s.audioRead;
        int space = VideoState::kAudioRing - (wr - rd);
        frames = std::min(frames, space);

        for (int i = 0; i < frames; i++)
        {
            float sum = 0.f;
            for (int c = 0; c < ch; c++)
                sum += pcm[i * ch + c];
            float mono = std::max(-1.f, std::min(1.f, sum / (float)ch));
            s.audioBuf[wr % VideoState::kAudioRing] = mono;
            wr++;
        }
        s.audioWrite = wr;
        buf->Unlock();
    }
    buf->Release();
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

// ── Decode loop ───────────────────────────────────────────────────────────────

static void decodeLoop(std::string path)
{
    auto &s = impl();

    // Create source reader
    IMFAttributes *attrs = nullptr;
    MFCreateAttributes(&attrs, 2);
    attrs->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
    attrs->SetUINT32(MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING, TRUE);

    std::wstring wpath(path.begin(), path.end());
    IMFSourceReader *reader = nullptr;
    HRESULT hr = MFCreateSourceReaderFromURL(wpath.c_str(), attrs, &reader);
    attrs->Release();
    if (FAILED(hr))
    {
        s.state = FluxVideo::State::Error;
        return;
    }

    // Configure video output: RGB24
    IMFMediaType *videoType = nullptr;
    MFCreateMediaType(&videoType);
    videoType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    videoType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB24);
    reader->SetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                                nullptr, videoType);
    videoType->Release();

    // Read dimensions + duration
    {
        IMFMediaType *actual = nullptr;
        reader->GetCurrentMediaType(
            (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, &actual);
        if (actual)
        {
            UINT32 w = 0, h = 0;
            MFGetAttributeSize(actual, MF_MT_FRAME_SIZE, &w, &h);
            s.videoWidth = (int)w;
            s.videoHeight = (int)h;
            actual->Release();
        }
        PROPVARIANT pv;
        PropVariantInit(&pv);
        if (SUCCEEDED(reader->GetPresentationAttribute(
                (DWORD)MF_SOURCE_READER_MEDIASOURCE, MF_PD_DURATION, &pv)) &&
            pv.vt == VT_UI8)
            s.durationHns = (int64_t)pv.uhVal.QuadPart;
        PropVariantClear(&pv);
    }

    bool hasAudio = configureAudio(reader);

    // Discover actual stream indices
    DWORD videoStreamIdx = 0xFFFFFFFF, audioStreamIdx = 0xFFFFFFFF;
    for (DWORD i = 0; i < 8; i++)
    {
        IMFMediaType *mt = nullptr;
        if (SUCCEEDED(reader->GetCurrentMediaType(i, &mt)) && mt)
        {
            GUID major = GUID_NULL;
            mt->GetMajorType(&major);
            if (major == MFMediaType_Video && videoStreamIdx == 0xFFFFFFFF)
                videoStreamIdx = i;
            else if (major == MFMediaType_Audio && audioStreamIdx == 0xFFFFFFFF)
                audioStreamIdx = i;
            mt->Release();
        }
    }

    // Allocate frame buffer
    {
        std::lock_guard<std::mutex> lk(s.frameMutex);
        int w = s.videoWidth.load(), h = s.videoHeight.load();
        s.frameWidth = w;
        s.frameHeight = h;
        s.frameStride = w * 3;
        s.frameData.assign((size_t)(w * h * 3), 0);
    }

    // Allocate audio ring + start stream
    if (hasAudio)
    {
        {
            std::lock_guard<std::mutex> lk(s.audioMutex);
            s.audioBuf.assign(VideoState::kAudioRing, 0.f);
            s.audioWrite = 0;
            s.audioRead = 0;
        }
        FluxAudio::get().playStream(
            [](float *buf, int frames) -> int
            { return pullAudio(buf, frames); },
            s.audioSampleRate);
    }

    if (s.readyCallback)
        s.readyCallback(s.videoWidth.load(), s.videoHeight.load());

    s.state = FluxVideo::State::Paused;
    FluxAudio::get().pause();

    {
        std::unique_lock<std::mutex> lk(s.cmdMutex);
        s.pauseCV.wait(lk, [&s]
                       { return s.resumeRequested || s.stopDecode; });
        if (s.stopDecode)
        {
            reader->Release();
            return;
        }
        s.resumeRequested = false;
        s.state = FluxVideo::State::Playing;
        FluxAudio::get().resume();
    }

    s.clockBase = std::chrono::steady_clock::now();
    s.clockBaseHns = 0;

    bool videoEOS = false, audioEOS = false;

    while (true)
    {
        {
            std::unique_lock<std::mutex> lk(s.cmdMutex);
            if (s.stopDecode)
                break;

            if (s.seekPending)
            {
                int64_t target = s.seekTargetHns;
                s.seekPending = false;
                lk.unlock();
                doSeek(reader, target);
                videoEOS = audioEOS = false;
                continue;
            }

            if (s.pauseRequested)
            {
                s.pauseRequested = false;
                s.state = FluxVideo::State::Paused;
                FluxAudio::get().pause();

                s.pauseCV.wait(lk, [&s]
                               { return s.resumeRequested || s.stopDecode; });
                if (s.stopDecode)
                    break;
                s.resumeRequested = false;
                s.state = FluxVideo::State::Playing;
                FluxAudio::get().resume();
                s.clockBase = std::chrono::steady_clock::now();
                s.clockBaseHns = s.positionHns.load();
            }
        }

        DWORD streamIndex = 0, flags = 0;
        LONGLONG timestamp = 0;
        IMFSample *sample = nullptr;

        hr = reader->ReadSample((DWORD)MF_SOURCE_READER_ANY_STREAM, 0,
                                &streamIndex, &flags, &timestamp, &sample);
        if (FAILED(hr))
            break;

        if (flags & MF_SOURCE_READERF_ENDOFSTREAM)
        {
            if (sample)
            {
                sample->Release();
                sample = nullptr;
            }
            if (streamIndex == videoStreamIdx)
            {
                videoEOS = true;
                s.state = FluxVideo::State::Finished;
                bool expected = false;
                if (s.didFireFinish.compare_exchange_strong(expected, true))
                    if (s.finishCallback)
                        s.finishCallback();
                break;
            }
            else
            {
                audioEOS = true;
                reader->SetStreamSelection(streamIndex, FALSE);
                continue;
            }
        }

        if (flags & MF_SOURCE_READERF_STREAMTICK)
        {
            if (sample)
            {
                sample->Release();
                sample = nullptr;
            }
            continue;
        }
        if (!sample)
            continue;

        if (streamIndex == videoStreamIdx)
            processVideoSample(sample, timestamp);
        else if (streamIndex == audioStreamIdx && hasAudio && !audioEOS)
            processAudioSample(sample);

        sample->Release();
    }

    reader->Release();
}

// ============================================================================
// FluxVideo public methods
// ============================================================================

bool FluxVideo::open(const std::string &path)
{
    auto &s = impl();
    s.videoEOS = s.audioEOS = false;
    close();
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
    std::lock_guard<std::mutex> lk(s.cmdMutex);
    if (s.state == State::Paused || s.state == State::Loading ||
        (s.state == State::Playing && s.pauseRequested))
    {
        s.pauseRequested = false;
        s.resumeRequested = true;
        s.pauseCV.notify_all();
    }
}

void FluxVideo::pause()
{
    auto &s = impl();
    std::lock_guard<std::mutex> lk(s.cmdMutex);
    if (s.state == State::Playing && !s.pauseRequested)
        s.pauseRequested = true;
}

void FluxVideo::seekToProgress(float p)
{
    int64_t hns = (int64_t)(std::max(0.f, std::min(1.f, p)) *
                            (float)impl().durationHns.load());
    auto &s = impl();
    std::lock_guard<std::mutex> lk(s.cmdMutex);
    s.seekTargetHns = hns;
    s.seekPending = true;
    s.cmdCV.notify_one();
}

void FluxVideo::seekToSeconds(float secs)
{
    auto &s = impl();
    int64_t hns = (int64_t)(secs * 1e7f);
    hns = std::max<int64_t>(0, std::min(hns, s.durationHns.load()));
    std::lock_guard<std::mutex> lk(s.cmdMutex);
    s.seekTargetHns = hns;
    s.seekPending = true;
    s.cmdCV.notify_one();
}

void FluxVideo::close()
{
    auto &s = impl();
    {
        std::lock_guard<std::mutex> lk(s.cmdMutex);
        s.stopDecode = true;
        s.resumeRequested = true;
    }
    s.cmdCV.notify_all();
    s.pauseCV.notify_all();

    if (s.decodeThread.joinable())
        s.decodeThread.join();

    FluxAudio::get().stopPlayback();

    s.state = State::Idle;
    s.positionHns = 0;
    s.durationHns = 0;
    s.newFrame = false;
    s.videoWidth = 0;
    s.videoHeight = 0;
    s.stopDecode = false;
    s.videoEOS = false;
    s.audioEOS = false;
}

#endif // _WIN32