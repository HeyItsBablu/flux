// flux_mic_android.cpp
// Android backend for FluxMic — direct AAudio input stream. No dependency
// on FluxAudio; mirrors the standalone-per-platform approach flux_mic_win32.cpp
// (WASAPI) and flux_video_android.cpp (MediaCodec) already use.
//
// Link against: aaudio
#ifdef __ANDROID__

#include "flux/flux_mic.hpp"

#include <aaudio/AAudio.h>
#include <android/log.h>

#include <atomic>
#include <cmath>
#include <cstring>
#include <vector>

#define MIC_LOGE(fmt, ...) \
    __android_log_print(ANDROID_LOG_ERROR, "FluxMic", fmt, ##__VA_ARGS__)

struct FluxMic::Impl
{
    AAudioStream *stream = nullptr;

    int sampleRate = FluxMic::kDefaultSampleRate;
    int channels = FluxMic::kDefaultChannels;

    std::atomic<bool> opened{false};
    std::atomic<bool> capturing{false};
    std::atomic<float> level{0.f};

    FrameCallback cb;

    // Scratch mono-float buffer reused every callback — audioData handed
    // to us by AAudio is already PCM_FLOAT at the channel count we
    // requested, so this is only needed for the >1-channel downmix case.
    std::vector<float> monoBuf;

    // ── AAudio data callback ─────────────────────────────────────────────
    // Runs on AAudio's own internal high-priority realtime thread — no
    // separate capture thread needed here, unlike the WASAPI backend's
    // manual polling loop.
    static aaudio_data_callback_result_t onAudioData(
        AAudioStream * /*stream*/, void *userData,
        void *audioData, int32_t numFrames)
    {
        auto *impl = static_cast<Impl *>(userData);
        if (numFrames <= 0)
            return AAUDIO_CALLBACK_RESULT_CONTINUE;

        const float *src = static_cast<const float *>(audioData);
        int ch = impl->channels;

        const float *framesOut = src;
        if (ch > 1)
        {
            impl->monoBuf.resize((size_t)numFrames);
            for (int32_t i = 0; i < numFrames; i++)
            {
                float sum = 0.f;
                for (int c = 0; c < ch; c++)
                    sum += src[i * ch + c];
                impl->monoBuf[i] = sum / (float)ch;
            }
            framesOut = impl->monoBuf.data();
        }

        float rms = 0.f;
        for (int32_t i = 0; i < numFrames; i++)
            rms += framesOut[i] * framesOut[i];
        impl->level.store(std::sqrt(rms / (float)numFrames));

        if (impl->cb)
            impl->cb(framesOut, (size_t)numFrames);

        return AAUDIO_CALLBACK_RESULT_CONTINUE;
    }

    static void onError(AAudioStream * /*stream*/, void *userData,
                        aaudio_result_t error)
    {
        auto *impl = static_cast<Impl *>(userData);
        MIC_LOGE("stream error: %d", (int)error);
        impl->capturing.store(false);
    }

    bool doOpen(int sr, int ch)
    {
        sampleRate = sr;
        channels = ch;

        AAudioStreamBuilder *builder = nullptr;
        aaudio_result_t res = AAudio_createStreamBuilder(&builder);
        if (res != AAUDIO_OK || !builder)
        {
            MIC_LOGE("createStreamBuilder failed: %d", (int)res);
            return false;
        }

        AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_INPUT);
        AAudioStreamBuilder_setSampleRate(builder, sampleRate);
        AAudioStreamBuilder_setChannelCount(builder, channels);
        AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_FLOAT);
        AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
        AAudioStreamBuilder_setDataCallback(builder, onAudioData, this);
        AAudioStreamBuilder_setErrorCallback(builder, onError, this);

        res = AAudioStreamBuilder_openStream(builder, &stream);
        AAudioStreamBuilder_delete(builder);

        if (res != AAUDIO_OK || !stream)
        {
            MIC_LOGE("openStream failed: %d", (int)res);
            stream = nullptr;
            return false;
        }

        // Reflect what the device actually negotiated (may differ from request).
        sampleRate = AAudioStream_getSampleRate(stream);
        channels = AAudioStream_getChannelCount(stream);

        opened = true;
        return true;
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

    aaudio_result_t res = AAudioStream_requestStart(m_impl->stream);
    if (res != AAUDIO_OK)
    {
        MIC_LOGE("requestStart failed: %d", (int)res);
        m_impl->cb = nullptr;
        return false;
    }

    m_impl->capturing = true;
    return true;
}

void FluxMic::stop()
{
    if (!m_impl->capturing.load())
        return;
    if (m_impl->stream)
        AAudioStream_requestStop(m_impl->stream);
    m_impl->capturing = false;
    m_impl->cb = nullptr;
    m_impl->level = 0.f;
}

void FluxMic::close()
{
    stop();
    if (m_impl->stream)
    {
        AAudioStream_close(m_impl->stream);
        m_impl->stream = nullptr;
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

#endif // __ANDROID__