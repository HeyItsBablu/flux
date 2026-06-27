// flux_audio_linux.cpp
// Linux ALSA audio backend for FluxAudio.
//
// Features: PCM playback, stream playback (used by FluxVideo),
//           seek, pause/resume, volume. Recording not implemented (stubs in header).
//
// Link against: asound
//
#if defined(__linux__) && !defined(__ANDROID__)

#include "flux/flux_audio.hpp"

#include <alsa/asoundlib.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

#include "../../external/dr/dr_mp3.h"
#include "../../external/dr/dr_wav.h"

#define AUDIO_LOGI(fmt, ...) fprintf(stderr, "[FluxAudio] " fmt "\n", ##__VA_ARGS__)
#define AUDIO_LOGE(fmt, ...) AUDIO_LOGI(fmt, ##__VA_ARGS__)

// ============================================================================
// FluxAudio::Impl
// ============================================================================
struct FluxAudio::Impl
{
    // ── Playback state ────────────────────────────────────────────────────────
    std::vector<float> playBuffer;
    std::atomic<int> playPosition{0};
    int playSampleRate = 44100;
    std::atomic<float> volume{1.0f};
    std::atomic<bool> playing{false};
    std::atomic<bool> paused{false};
    std::atomic<bool> didFireFinish{false};
    FinishCallback finishCallback;

    // ── Stream callback ───────────────────────────────────────────────────────
    StreamCallback streamCallback;

    // ── Write thread + command channel ────────────────────────────────────────
    std::thread writeThread;
    std::mutex cmdMutex;
    std::condition_variable cmdCV;
    std::condition_variable pauseCV;

    bool pauseRequested = false;
    bool resumeRequested = false;
    bool stopRequested = false;
    bool seekPending = false;
    int seekTarget = 0;

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
        FILE *f = fopen(path.c_str(), "rb");
        if (!f)
            return {};
        fseek(f, 0, SEEK_END);
        size_t len = (size_t)ftell(f);
        rewind(f);
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

    // ── ALSA write loop ───────────────────────────────────────────────────────
    //
    // Opens "default" in non-blocking mode for hw params negotiation,
    // then switches to blocking for the write loop to avoid busy-spinning.
    //
    // Xrun recovery: EPIPE → underrun (recover + continue),
    //                EAGAIN → non-blocking would block (sleep 1ms + retry).
    //
    void writeLoop()
    {
        snd_pcm_t *pcm = nullptr;
        int err = snd_pcm_open(&pcm, "default", SND_PCM_STREAM_PLAYBACK,
                               SND_PCM_NONBLOCK);
        if (err < 0)
        {
            AUDIO_LOGE("snd_pcm_open failed: %s", snd_strerror(err));
            playing = false;
            return;
        }

        // ── Hardware params ───────────────────────────────────────────────────
        snd_pcm_hw_params_t *hw = nullptr;
        snd_pcm_hw_params_alloca(&hw);
        snd_pcm_hw_params_any(pcm, hw);
        snd_pcm_hw_params_set_access(pcm, hw, SND_PCM_ACCESS_RW_INTERLEAVED);
        snd_pcm_hw_params_set_format(pcm, hw, SND_PCM_FORMAT_FLOAT_LE);
        snd_pcm_hw_params_set_channels(pcm, hw, 1);

        unsigned int rate = (unsigned int)playSampleRate;
        snd_pcm_hw_params_set_rate_near(pcm, hw, &rate, nullptr);

        snd_pcm_uframes_t period = 512;
        snd_pcm_hw_params_set_period_size_near(pcm, hw, &period, nullptr);

        snd_pcm_uframes_t bufSize = period * 4;
        snd_pcm_hw_params_set_buffer_size_near(pcm, hw, &bufSize);

        err = snd_pcm_hw_params(pcm, hw);
        if (err < 0)
        {
            AUDIO_LOGE("snd_pcm_hw_params failed: %s", snd_strerror(err));
            snd_pcm_close(pcm);
            playing = false;
            return;
        }

        // Switch to blocking for the write loop
        snd_pcm_nonblock(pcm, 0);
        snd_pcm_prepare(pcm);

        AUDIO_LOGI("ALSA opened: rate=%uHz period=%lu frames", rate, period);

        std::vector<float> periodBuf(period);

        while (true)
        {
            // ── Command check (stop / seek / pause) ───────────────────────────
            {
                std::unique_lock<std::mutex> lk(cmdMutex);

                if (stopRequested)
                    break;

                if (seekPending)
                {
                    playPosition.store(seekTarget);
                    seekPending = false;
                    snd_pcm_drop(pcm);
                    snd_pcm_prepare(pcm);
                }

                if (pauseRequested)
                {
                    pauseRequested = false;
                    playing = false;
                    paused = true;
                    snd_pcm_drop(pcm);

                    pauseCV.wait(lk, [this]
                                 { return resumeRequested || stopRequested || seekPending; });

                    if (stopRequested)
                        break;

                    if (seekPending)
                    {
                        playPosition.store(seekTarget);
                        seekPending = false;
                    }

                    resumeRequested = false;
                    paused = false;
                    playing = true;
                    snd_pcm_prepare(pcm);
                }
            }

            // ── Fill one period ───────────────────────────────────────────────
            float vol = volume.load();
            int nFrames = (int)period;

            if (streamCallback)
            {
                int got = streamCallback(periodBuf.data(), nFrames);
                if (got == 0)
                {
                    snd_pcm_drain(pcm);
                    playing = false;
                    bool expected = false;
                    if (didFireFinish.compare_exchange_strong(expected, true))
                        if (finishCallback)
                            finishCallback();
                    break;
                }
                for (int i = 0; i < nFrames; i++)
                    periodBuf[i] *= vol;
            }
            else
            {
                int pos = playPosition.load();
                int total = (int)playBuffer.size();
                if (pos >= total)
                {
                    snd_pcm_drain(pcm);
                    playing = false;
                    bool expected = false;
                    if (didFireFinish.compare_exchange_strong(expected, true))
                        if (finishCallback)
                            finishCallback();
                    break;
                }
                nFrames = std::min((int)period, total - pos);
                for (int i = 0; i < nFrames; i++)
                    periodBuf[i] = playBuffer[pos + i] * vol;
                for (int i = nFrames; i < (int)period; i++)
                    periodBuf[i] = 0.f;
            }

            // ── Write ─────────────────────────────────────────────────────────
            snd_pcm_sframes_t written =
                snd_pcm_writei(pcm, periodBuf.data(), period);

            if (written == -EAGAIN)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }
            else if (written == -EPIPE)
            {
                AUDIO_LOGI("ALSA underrun, recovering");
                snd_pcm_recover(pcm, (int)written, 1);
                continue;
            }
            else if (written < 0)
            {
                AUDIO_LOGE("snd_pcm_writei error: %s", snd_strerror((int)written));
                snd_pcm_recover(pcm, (int)written, 1);
                continue;
            }

            s_playPosition_add((int)written);
        }

        snd_pcm_close(pcm);
        playing = false;
    }

    // Thin wrapper so writeLoop can advance the position counter without
    // needing a pointer back to FluxAudio.
    void s_playPosition_add(int n) { playPosition.fetch_add(n); }

    void stopWriteThread()
    {
        if (writeThread.joinable())
        {
            {
                std::lock_guard<std::mutex> lk(cmdMutex);
                stopRequested = true;
            }
            cmdCV.notify_all();
            pauseCV.notify_all();
            writeThread.join();
        }
        streamCallback = nullptr;
        playing = false;
        paused = false;
    }
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
    int target = (int)(std::max(0.f, std::min(1.f, progress)) * (float)total);
    {
        std::lock_guard<std::mutex> lk(m_impl->cmdMutex);
        m_impl->seekTarget = target;
        m_impl->seekPending = true;
    }
    m_impl->cmdCV.notify_all();
    m_impl->pauseCV.notify_all();
}
void FluxAudio::seekToSeconds(float seconds)
{
    int target = (int)(seconds * (float)m_impl->playSampleRate);
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
    std::lock_guard<std::mutex> lk(m_impl->cmdMutex);
    m_impl->pauseRequested = true;
    m_impl->cmdCV.notify_all();
}
void FluxAudio::resume()
{
    if (!m_impl->paused.load())
        return;
    {
        std::lock_guard<std::mutex> lk(m_impl->cmdMutex);
        m_impl->resumeRequested = true;
    }
    m_impl->pauseCV.notify_all();
}
bool FluxAudio::isPaused() const { return m_impl->paused.load(); }
bool FluxAudio::isPlaying() const { return m_impl->playing.load(); }

bool FluxAudio::playPCM(const std::vector<float> &samples, int sampleRate)
{
    stopPlayback();
    {
        std::lock_guard<std::mutex> lk(m_impl->cmdMutex);
        m_impl->playBuffer = samples;
        m_impl->playSampleRate = sampleRate;
        m_impl->playPosition = 0;
        m_impl->didFireFinish = false;
        m_impl->paused = false;
        m_impl->pauseRequested = false;
        m_impl->resumeRequested = false;
        m_impl->seekPending = false;
        m_impl->stopRequested = false;
    }
    m_impl->playing = true;
    m_impl->writeThread = std::thread(&Impl::writeLoop, m_impl);
    return true;
}

bool FluxAudio::playStream(StreamCallback cb, int sampleRate)
{
    stopPlayback();
    {
        std::lock_guard<std::mutex> lk(m_impl->cmdMutex);
        m_impl->streamCallback = std::move(cb);
        m_impl->playSampleRate = sampleRate;
        m_impl->playBuffer.clear();
        m_impl->playPosition = 0;
        m_impl->didFireFinish = false;
        m_impl->paused = false;
        m_impl->pauseRequested = false;
        m_impl->resumeRequested = false;
        m_impl->seekPending = false;
        m_impl->stopRequested = false;
    }
    m_impl->playing = true;
    m_impl->writeThread = std::thread(&Impl::writeLoop, m_impl);
    return true;
}

bool FluxAudio::startPlayback() { return true; } // managed by writeThread

void FluxAudio::stopPlayback() { m_impl->stopWriteThread(); }
void FluxAudio::closePlayback() { m_impl->stopWriteThread(); }
void FluxAudio::shutdown() { m_impl->stopWriteThread(); }

#endif // __linux__ && !__ANDROID__