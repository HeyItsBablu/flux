// flux_mic_android.cpp
// Android backend for FluxMic — delegates to FluxAudio's existing AAudio
// input stream rather than duplicating it.
#ifdef __ANDROID__

#include "flux/flux_mic.hpp"
#include "flux/flux_audio.hpp"

struct FluxMic::Impl
{
    int sampleRate = FluxMic::kDefaultSampleRate;
    int channels = FluxMic::kDefaultChannels;
    bool opened = false;
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
    m_impl->sampleRate = sampleRate;
    m_impl->channels = channels;
    m_impl->opened = FluxAudio::get().openRecord(sampleRate, channels);
    return m_impl->opened;
}

bool FluxMic::start(FrameCallback cb)
{
    if (!m_impl->opened && !open(m_impl->sampleRate, m_impl->channels))
        return false;
    return FluxAudio::get().startRecord(
        [cb](const float *s, int c)
        { if (cb) cb(s, (size_t)c); });
}

void FluxMic::stop() { FluxAudio::get().stopRecord(); }
void FluxMic::close()
{
    FluxAudio::get().closeRecord();
    m_impl->opened = false;
}

FluxMic::State FluxMic::getState() const
{
    if (!m_impl->opened)
        return State::Idle;
    return FluxAudio::get().isRecording() ? State::Recording : State::Open;
}
bool FluxMic::isRecording() const { return FluxAudio::get().isRecording(); }
float FluxMic::getInputLevel() const { return FluxAudio::get().getInputLevel(); }
int FluxMic::getSampleRate() const { return FluxAudio::get().getRecordSampleRate(); }
int FluxMic::getChannels() const { return m_impl->channels; }

#endif // __ANDROID__