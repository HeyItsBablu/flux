// flux_mic_linux.cpp
// Linux ALSA capture backend for FluxMic.
// Link against: asound
#if defined(__linux__) && !defined(__ANDROID__)

#include "flux/flux_mic.hpp"

#include <alsa/asoundlib.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <thread>
#include <vector>

#define MIC_LOGE(fmt, ...) fprintf(stderr, "[FluxMic] " fmt "\n", ##__VA_ARGS__)

struct FluxMic::Impl
{
    snd_pcm_t *pcm = nullptr;
    int sampleRate = FluxMic::kDefaultSampleRate;
    int channels = FluxMic::kDefaultChannels;

    std::atomic<bool> opened{false};
    std::atomic<bool> capturing{false};
    std::atomic<float> level{0.f};

    FrameCallback cb;
    std::thread captureThread;
    std::atomic<bool> threadRunning{false};

    snd_pcm_uframes_t period = 512;
    std::vector<float> periodBuf;

    bool doOpen(int sr, int ch)
    {
        sampleRate = sr;
        channels = ch;

        int err = snd_pcm_open(&pcm, "default", SND_PCM_STREAM_CAPTURE, 0);
        if (err < 0)
        {
            MIC_LOGE("snd_pcm_open: %s", snd_strerror(err));
            return false;
        }

        snd_pcm_hw_params_t *hw = nullptr;
        snd_pcm_hw_params_alloca(&hw);
        snd_pcm_hw_params_any(pcm, hw);
        snd_pcm_hw_params_set_access(pcm, hw, SND_PCM_ACCESS_RW_INTERLEAVED);

        // Try float32 first; fall back to S16_LE if unsupported.
        int fmtErr = snd_pcm_hw_params_set_format(pcm, hw, SND_PCM_FORMAT_FLOAT_LE);
        bool isFloat = (fmtErr == 0);
        if (!isFloat)
            snd_pcm_hw_params_set_format(pcm, hw, SND_PCM_FORMAT_S16_LE);

        snd_pcm_hw_params_set_channels(pcm, hw, (unsigned)channels);
        unsigned int rate = (unsigned)sampleRate;
        snd_pcm_hw_params_set_rate_near(pcm, hw, &rate, nullptr);
        snd_pcm_hw_params_set_period_size_near(pcm, hw, &period, nullptr);

        err = snd_pcm_hw_params(pcm, hw);
        if (err < 0)
        {
            MIC_LOGE("hw_params: %s", snd_strerror(err));
            snd_pcm_close(pcm);
            pcm = nullptr;
            return false;
        }

        snd_pcm_prepare(pcm);
        periodBuf.resize(period * channels);
        _usingFloat = isFloat;
        opened = true;
        return true;
    }

    bool _usingFloat = true;

    void captureLoop()
    {
        std::vector<int16_t> s16Buf;
        if (!_usingFloat)
            s16Buf.resize(period * channels);

        while (threadRunning.load())
        {
            snd_pcm_sframes_t got;
            if (_usingFloat)
                got = snd_pcm_readi(pcm, periodBuf.data(), period);
            else
                got = snd_pcm_readi(pcm, s16Buf.data(), period);

            if (got == -EPIPE)
            {
                snd_pcm_prepare(pcm);
                continue;
            }
            else if (got < 0)
            {
                snd_pcm_recover(pcm, (int)got, 1);
                continue;
            }
            if (got == 0)
                continue;

            // Downmix interleaved -> mono float32 in place into periodBuf[0..got)
            int srcCh = channels;
            std::vector<float> mono(got);
            for (snd_pcm_sframes_t i = 0; i < got; i++)
            {
                float sum = 0.f;
                for (int c = 0; c < srcCh; c++)
                {
                    if (_usingFloat)
                        sum += periodBuf[i * srcCh + c];
                    else
                        sum += s16Buf[i * srcCh + c] / 32768.f;
                }
                mono[i] = sum / (float)srcCh;
            }

            float rms = 0.f;
            for (float s : mono)
                rms += s * s;
            level.store(std::sqrt(rms / (float)mono.size()));

            if (cb)
                cb(mono.data(), mono.size());
        }
    }
};

FluxMic &FluxMic::get()
{
    static FluxMic inst;
    return inst;
}
FluxMic::FluxMic() : m_impl(new Impl()) {}
FluxMic::~FluxMic()
{
    close();
    delete m_impl;
}

bool FluxMic::open(int sampleRate, int channels)
{
    if (m_impl->opened.load())
        return true;
    return m_impl->doOpen(sampleRate, channels);
}

bool FluxMic::start(FrameCallback cb)
{
    if (!m_impl->opened.load() && !open(m_impl->sampleRate, m_impl->channels))
        return false;
    if (m_impl->capturing.load())
        return true;

    m_impl->cb = std::move(cb);
    m_impl->capturing = true;
    m_impl->threadRunning = true;
    m_impl->captureThread = std::thread(&Impl::captureLoop, m_impl);
    return true;
}

void FluxMic::stop()
{
    if (!m_impl->capturing.load())
        return;
    m_impl->threadRunning = false;
    if (m_impl->captureThread.joinable())
        m_impl->captureThread.join();
    m_impl->capturing = false;
    m_impl->cb = nullptr;
    m_impl->level = 0.f;
}

void FluxMic::close()
{
    stop();
    if (m_impl->pcm)
    {
        snd_pcm_close(m_impl->pcm);
        m_impl->pcm = nullptr;
    }
    m_impl->opened = false;
}

FluxMic::State FluxMic::getState() const
{
    if (!m_impl->opened.load())
        return State::Idle;
    return m_impl->capturing.load() ? State::Recording : State::Open;
}
bool FluxMic::isRecording() const { return m_impl->capturing.load(); }
float FluxMic::getInputLevel() const { return m_impl->level.load(); }
int FluxMic::getSampleRate() const { return m_impl->sampleRate; }
int FluxMic::getChannels() const { return m_impl->channels; }

#endif // __linux__ && !__ANDROID__