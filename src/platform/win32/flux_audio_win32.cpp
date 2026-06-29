// flux_audio_win32.cpp
// Windows XAudio2 audio backend for FluxAudio.
//
// Features: PCM playback, stream playback (used by FluxVideo),
//           seek, pause/resume, volume, progress polling.
// Recording is not implemented on Windows; stubs are in flux_audio.hpp.
//
// Link against: xaudio2  ole32
//
#ifdef _WIN32

#include "flux/flux_audio.hpp"

#include <windows.h>
#include <xaudio2.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <thread>
#include <vector>

#include "dr_mp3.h"
#include "dr_wav.h"

#define AUDIO_LOGI(fmt, ...)                                       \
    do                                                             \
    {                                                              \
        char _b[256];                                              \
        snprintf(_b, 256, "[FluxAudio] " fmt "\n", ##__VA_ARGS__); \
        OutputDebugStringA(_b);                                    \
    } while (0)
#define AUDIO_LOGE(fmt, ...) AUDIO_LOGI(fmt, ##__VA_ARGS__)

// ============================================================================
// VoiceCallback — fires OnStreamEnd when the source voice drains
// ============================================================================
class FLXVoiceCallback : public IXAudio2VoiceCallback
{
public:
    std::function<void()> onStreamEnd;

    void STDMETHODCALLTYPE OnStreamEnd() noexcept override
    {
        if (onStreamEnd)
            onStreamEnd();
    }
    void STDMETHODCALLTYPE OnVoiceProcessingPassStart(UINT32) noexcept override {}
    void STDMETHODCALLTYPE OnVoiceProcessingPassEnd() noexcept override {}
    void STDMETHODCALLTYPE OnBufferStart(void *) noexcept override {}
    void STDMETHODCALLTYPE OnBufferEnd(void *) noexcept override {}
    void STDMETHODCALLTYPE OnLoopEnd(void *) noexcept override {}
    void STDMETHODCALLTYPE OnVoiceError(void *, HRESULT) noexcept override {}
};

// ============================================================================
// FluxAudio::Impl
// ============================================================================
struct FluxAudio::Impl
{
    // ── XAudio2 ───────────────────────────────────────────────────────────────
    Microsoft::WRL::ComPtr<IXAudio2> xaudio2;
    IXAudio2MasteringVoice *masterVoice = nullptr;
    IXAudio2SourceVoice *sourceVoice = nullptr;
    FLXVoiceCallback voiceCB;

    // ── Playback state ────────────────────────────────────────────────────────
    std::vector<float> playBuffer;
    std::atomic<int> playPosition{0};
    int submitOffset = 0;
    int playSampleRate = 44100;
    std::atomic<bool> playing{false};
    std::atomic<bool> paused{false};
    std::atomic<bool> didFireFinish{false};
    std::atomic<float> volume{1.0f};
    FinishCallback finishCallback;

    // ── Stream feed ───────────────────────────────────────────────────────────
    StreamCallback streamCallback;
    std::thread streamThread;
    std::atomic<bool> streamRunning{false};

    static constexpr int kStreamFrames = 2048; // ~46 ms @ 44100 Hz
    float streamBufs[2][kStreamFrames]{};
    std::atomic<int> streamFront{0};

    // ── Progress polling ──────────────────────────────────────────────────────
    std::thread progressThread;
    std::atomic<bool> progressRunning{false};

    // ── File helpers ──────────────────────────────────────────────────────────
    static bool isMp3(const std::string &p)
    {
        if (p.size() < 4)
            return false;
        std::string e = p.substr(p.size() - 4);
        for (auto &c : e)
            c = (char)tolower(c);
        return e == ".mp3";
    }
    static bool isWav(const std::string &p)
    {
        if (p.size() < 4)
            return false;
        std::string e = p.substr(p.size() - 4);
        for (auto &c : e)
            c = (char)tolower(c);
        return e == ".wav";
    }

    static std::vector<uint8_t> loadBytes(const std::string &path)
    {
        FILE *f = nullptr;
        if (fopen_s(&f, path.c_str(), "rb") != 0 || !f)
            return {};
        fseek(f, 0, SEEK_END);
        size_t len = (size_t)ftell(f);
        fseek(f, 0, SEEK_SET);
        std::vector<uint8_t> buf(len);
        fread(buf.data(), 1, len, f);
        fclose(f);
        return buf;
    }

    static void mixdownToMono(const float *src, int frames, int channels,
                              std::vector<float> &out)
    {
        out.resize(frames);
        if (channels == 1)
        {
            memcpy(out.data(), src, frames * sizeof(float));
        }
        else
        {
            float inv = 1.f / (float)channels;
            for (int i = 0; i < frames; i++)
            {
                float sum = 0.f;
                for (int c = 0; c < channels; c++)
                    sum += src[i * channels + c];
                out[i] = sum * inv;
            }
        }
    }

    bool decodeFile(const std::string &path, std::vector<float> &out,
                    int &outSR)
    {
        auto raw = loadBytes(path);
        if (raw.empty())
            return false;
        if (isMp3(path))
        {
            drmp3_config cfg{};
            drmp3_uint64 frames = 0;
            float *pcm = drmp3_open_memory_and_read_pcm_frames_f32(
                raw.data(), raw.size(), &cfg, &frames, nullptr);
            if (!pcm)
                return false;
            outSR = (int)cfg.sampleRate;
            mixdownToMono(pcm, (int)frames, (int)cfg.channels, out);
            drmp3_free(pcm, nullptr);
            return true;
        }
        if (isWav(path))
        {
            drwav_uint64 frames = 0;
            unsigned ch = 0, sr = 0;
            float *pcm = drwav_open_memory_and_read_pcm_frames_f32(
                raw.data(), raw.size(), &ch, &sr, &frames, nullptr);
            if (!pcm)
                return false;
            outSR = (int)sr;
            mixdownToMono(pcm, (int)frames, (int)ch, out);
            drwav_free(pcm, nullptr);
            return true;
        }
        return false;
    }

    // ── XAudio2 init ──────────────────────────────────────────────────────────
    bool ensureEngine()
    {
        if (xaudio2)
            return true;
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        HRESULT hr = XAudio2Create(xaudio2.GetAddressOf(), 0);
        if (FAILED(hr))
        {
            AUDIO_LOGE("XAudio2Create: 0x%08X", hr);
            return false;
        }
        hr = xaudio2->CreateMasteringVoice(&masterVoice);
        if (FAILED(hr))
        {
            AUDIO_LOGE("CreateMasteringVoice: 0x%08X", hr);
            return false;
        }
        return true;
    }

    bool createSourceVoice(int sampleRate)
    {
        if (sourceVoice)
        {
            sourceVoice->DestroyVoice();
            sourceVoice = nullptr;
        }

        WAVEFORMATEX wfx{};
        wfx.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
        wfx.nChannels = 1;
        wfx.nSamplesPerSec = (DWORD)sampleRate;
        wfx.wBitsPerSample = 32;
        wfx.nBlockAlign = 4;
        wfx.nAvgBytesPerSec = (DWORD)sampleRate * 4;

        voiceCB.onStreamEnd = [this]()
        {
            playing = false;
            playPosition.store((int)playBuffer.size());
            bool expected = false;
            if (didFireFinish.compare_exchange_strong(expected, true))
                if (finishCallback)
                    finishCallback();
        };

        HRESULT hr = xaudio2->CreateSourceVoice(
            &sourceVoice, &wfx, 0, XAUDIO2_DEFAULT_FREQ_RATIO, &voiceCB);
        if (FAILED(hr))
        {
            AUDIO_LOGE("CreateSourceVoice: 0x%08X", hr);
            sourceVoice = nullptr;
            return false;
        }
        sourceVoice->SetVolume(volume.load());
        return true;
    }

    void submitAndStart()
    {
        if (!sourceVoice)
            return;
        sourceVoice->FlushSourceBuffers();

        int start = playPosition.load();
        int total = (int)playBuffer.size();
        if (start >= total)
            return;

        submitOffset = start;
        XAUDIO2_BUFFER buf{};
        buf.AudioBytes = (UINT32)((total - start) * sizeof(float));
        buf.pAudioData = reinterpret_cast<const BYTE *>(playBuffer.data() + start);
        buf.Flags = XAUDIO2_END_OF_STREAM;
        sourceVoice->SubmitSourceBuffer(&buf);
        sourceVoice->Start();
        startProgressThread();
    }

    void stopVoice()
    {
        if (!sourceVoice)
            return;
        XAUDIO2_VOICE_STATE vs{};
        sourceVoice->GetState(&vs);
        int newPos = std::min(submitOffset + (int)vs.SamplesPlayed,
                              (int)playBuffer.size());
        playPosition.store(newPos);
        sourceVoice->Stop();
        sourceVoice->FlushSourceBuffers();
    }

    // ── Stream feeder thread ──────────────────────────────────────────────────
    void submitStreamBuffer(int idx)
    {
        if (!sourceVoice)
            return;
        XAUDIO2_BUFFER buf{};
        buf.AudioBytes = kStreamFrames * sizeof(float);
        buf.pAudioData = reinterpret_cast<const BYTE *>(streamBufs[idx]);
        buf.Flags = 0;
        sourceVoice->SubmitSourceBuffer(&buf);
    }

    void streamFeedLoop()
    {
        for (int i = 0; i < 2; i++)
        {
            int got = streamCallback
                          ? streamCallback(streamBufs[i], kStreamFrames)
                          : 0;
            if (got == 0)
                memset(streamBufs[i], 0, sizeof(float) * kStreamFrames);
            submitStreamBuffer(i);
        }
        streamFront = 0;

        while (streamRunning.load())
        {
            XAUDIO2_VOICE_STATE vs{};
            for (;;)
            {
                if (!sourceVoice || !streamRunning.load())
                    goto done;
                sourceVoice->GetState(&vs);
                if (vs.BuffersQueued < 2)
                    break;
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            if (paused.load())
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            int back = 1 - streamFront.load();
            int got = streamCallback
                          ? streamCallback(streamBufs[back], kStreamFrames)
                          : 0;
            if (got == 0)
                memset(streamBufs[back], 0, sizeof(float) * kStreamFrames);
            submitStreamBuffer(back);
            streamFront = back;
        }
    done:;
    }

    // ── Progress polling thread ────────────────────────────────────────────────
    void startProgressThread()
    {
        if (progressRunning.load())
            return;
        progressRunning = true;
        progressThread = std::thread([this]()
                                     {
            while (progressRunning.load()) {
                if (sourceVoice && playing.load()) {
                    XAUDIO2_VOICE_STATE vs{};
                    sourceVoice->GetState(&vs);
                    int newPos = std::min(submitOffset + (int)vs.SamplesPlayed,
                                         (int)playBuffer.size());
                    playPosition.store(newPos);
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(33));
            } });
        progressThread.detach();
    }

    void stopProgressThread() { progressRunning = false; }
};

// ============================================================================
// FluxAudio public API
// ============================================================================

FluxAudio &FluxAudio::get()
{
    static FluxAudio inst;
    return inst;
}
FluxAudio::FluxAudio() : m_impl(new Impl()) {}
FluxAudio::~FluxAudio()
{
    shutdown();
    delete m_impl;
}

void FluxAudio::setVolume(float v)
{
    m_impl->volume.store(std::max(0.f, std::min(1.f, v)));
    if (m_impl->sourceVoice)
        m_impl->sourceVoice->SetVolume(m_impl->volume.load());
}
float FluxAudio::getVolume() const { return m_impl->volume.load(); }

float FluxAudio::getProgress() const
{
    int total = (int)m_impl->playBuffer.size();
    if (total == 0)
        return 0.f;
    return std::min(1.f, (float)m_impl->playPosition.load() / (float)total);
}
float FluxAudio::getPositionSeconds() const
{
    if (m_impl->playSampleRate == 0)
        return 0.f;
    return (float)m_impl->playPosition.load() / (float)m_impl->playSampleRate;
}
float FluxAudio::getDurationSeconds() const
{
    if (m_impl->playSampleRate == 0)
        return 0.f;
    return (float)m_impl->playBuffer.size() / (float)m_impl->playSampleRate;
}

void FluxAudio::seekToProgress(float progress)
{
    int total = (int)m_impl->playBuffer.size();
    int target = (int)(std::max(0.f, std::min(1.f, progress)) * total);
    bool wasPlaying = m_impl->playing.load();

    m_impl->stopVoice();
    m_impl->playPosition.store(target);

    if (wasPlaying || m_impl->paused.load())
    {
        m_impl->paused = false;
        m_impl->submitAndStart();
        m_impl->playing = true;
    }
}
void FluxAudio::seekToSeconds(float seconds)
{
    int target = (int)(seconds * m_impl->playSampleRate);
    target = std::max(0, std::min((int)m_impl->playBuffer.size(), target));
    seekToProgress((float)target /
                   (float)std::max(1, (int)m_impl->playBuffer.size()));
}

void FluxAudio::setOnFinished(FinishCallback cb)
{
    m_impl->finishCallback = std::move(cb);
}

bool FluxAudio::playFromPath(const std::string &path)
{
    std::vector<float> samples;
    int sr = 44100;
    if (!m_impl->decodeFile(path, samples, sr))
    {
        AUDIO_LOGE("playFromPath: failed to decode '%s'", path.c_str());
        return false;
    }
    return playPCM(samples, sr);
}

void FluxAudio::pause()
{
    if (!m_impl->playing.load())
        return;
    if (m_impl->sourceVoice)
        m_impl->sourceVoice->Stop();
    m_impl->playing = false;
    m_impl->paused = true;
}
void FluxAudio::resume()
{
    if (!m_impl->paused.load())
        return;
    m_impl->paused = false;
    m_impl->playing = true;
    if (m_impl->sourceVoice)
        m_impl->sourceVoice->Start();
}
bool FluxAudio::isPaused() const { return m_impl->paused.load(); }
bool FluxAudio::isPlaying() const { return m_impl->playing.load(); }

bool FluxAudio::playPCM(const std::vector<float> &samples, int sampleRate)
{
    closePlayback();
    m_impl->playBuffer = samples;
    m_impl->playPosition = 0;
    m_impl->submitOffset = 0;
    m_impl->playSampleRate = sampleRate;
    m_impl->didFireFinish = false;
    m_impl->paused = false;

    if (!m_impl->ensureEngine())
        return false;
    if (!m_impl->createSourceVoice(sampleRate))
        return false;
    m_impl->submitAndStart();
    m_impl->playing = true;
    return true;
}

bool FluxAudio::playStream(StreamCallback cb, int sampleRate)
{
    closePlayback();
    if (!m_impl->ensureEngine())
        return false;

    m_impl->streamCallback = std::move(cb);
    m_impl->playSampleRate = sampleRate;
    m_impl->playing = true;
    m_impl->paused = false;
    m_impl->didFireFinish = false;

    if (!m_impl->createSourceVoice(sampleRate))
    {
        m_impl->playing = false;
        return false;
    }
    m_impl->sourceVoice->SetVolume(m_impl->volume.load());
    m_impl->sourceVoice->Start();

    m_impl->streamRunning = true;
    m_impl->streamThread = std::thread(&Impl::streamFeedLoop, m_impl);
    return true;
}

bool FluxAudio::startPlayback() { return true; } // N/A on Win32 (handled in playPCM)

void FluxAudio::stopPlayback()
{
    m_impl->stopVoice();
    m_impl->playing = false;
    m_impl->paused = false;
}

void FluxAudio::closePlayback()
{
    m_impl->streamRunning = false;
    if (m_impl->streamThread.joinable())
        m_impl->streamThread.join();
    m_impl->streamCallback = nullptr;

    stopPlayback();
    m_impl->stopProgressThread();

    if (m_impl->sourceVoice)
    {
        m_impl->sourceVoice->DestroyVoice();
        m_impl->sourceVoice = nullptr;
    }
}

void FluxAudio::shutdown() { closePlayback(); }

#endif // _WIN32