// flux_mic_web.cpp
// Web backend for FluxMic — getUserMedia + ScriptProcessorNode capture.
#ifdef __EMSCRIPTEN__

#include "flux/flux_mic.hpp"
#include <emscripten.h>
#include <atomic>
#include <cmath>
#include <vector>

struct FluxMic::Impl
{
    int sampleRate = FluxMic::kDefaultSampleRate;
    int channels = FluxMic::kDefaultChannels;

    std::atomic<bool> opened{false};
    std::atomic<bool> capturing{false};
    std::atomic<float> level{0.f};

    FrameCallback cb;
};

static FluxMic::Impl *g_implForJS = nullptr; // single-instance, mirrors FluxMic::get()

extern "C"
{

    EMSCRIPTEN_KEEPALIVE
    void fluxMicPushFrames(float *buf, int count)
    {
        if (!g_implForJS || !g_implForJS->capturing.load())
            return;

        float rms = 0.f;
        for (int i = 0; i < count; i++)
            rms += buf[i] * buf[i];
        g_implForJS->level.store(std::sqrt(rms / (float)std::max(1, count)));

        if (g_implForJS->cb)
            g_implForJS->cb(buf, (size_t)count);
    }

} // extern "C"

FluxMic &FluxMic::get()
{
    static FluxMic inst;
    return inst;
}
FluxMic::FluxMic() : m_impl(new Impl()) { g_implForJS = m_impl; }
FluxMic::~FluxMic()
{
    close();
    delete m_impl;
}

bool FluxMic::open(int sampleRate, int channels)
{
    m_impl->sampleRate = sampleRate;
    m_impl->channels = channels;
    m_impl->opened = true; // actual getUserMedia permission resolves async in start()
    return true;
}

bool FluxMic::start(FrameCallback cb)
{
    if (!m_impl->opened.load())
        open(m_impl->sampleRate, m_impl->channels);
    m_impl->cb = std::move(cb);
    m_impl->capturing = true;

    EM_ASM({
        var sr = $0;
        navigator.mediaDevices.getUserMedia({ audio: true, video: false })
            .then(function(stream) {
                if (!Module._fluxMicAC) {
                    var AC = window.AudioContext || window.webkitAudioContext;
                    Module._fluxMicAC = new AC({ sampleRate: sr });
                }
                var ac = Module._fluxMicAC;
                Module._fluxMicStream = stream;
                var src = ac.createMediaStreamSource(stream);

                var bufSz = 1024;
                var sp = ac.createScriptProcessor(bufSz, 1, 1);
                Module._fluxMicSP = sp;

                var heapBuf = Module._malloc(bufSz * 4);
                Module._fluxMicHeapBuf = heapBuf;

                sp.onaudioprocess = function(e) {
                    var input = e.inputBuffer.getChannelData(0);
                    Module.HEAPF32.set(input, heapBuf >> 2);
                    Module._fluxMicPushFrames(heapBuf, input.length);
                };

                src.connect(sp);
                // ScriptProcessorNode requires a destination connection to
                // fire onaudioprocess in most browsers — connect to a muted
                // gain node rather than speakers to avoid feedback.
                var sink = ac.createGain();
                sink.gain.value = 0;
                sp.connect(sink);
                sink.connect(ac.destination);
            })
            .catch(function(e) {
                console.error('[FluxMic] getUserMedia failed:', e);
            }); }, m_impl->sampleRate);

    return true; // async — caller should treat isRecording()/permission as eventual
}

void FluxMic::stop()
{
    m_impl->capturing = false;
    m_impl->cb = nullptr;
    m_impl->level = 0.f;

    EM_ASM({
        if (Module._fluxMicSP)
        {
            Module._fluxMicSP.onaudioprocess = null;
            Module._fluxMicSP.disconnect();
            Module._fluxMicSP = null;
        }
        if (Module._fluxMicHeapBuf)
        {
            Module._free(Module._fluxMicHeapBuf);
            Module._fluxMicHeapBuf = null;
        }
        if (Module._fluxMicStream)
        {
            Module._fluxMicStream.getTracks().forEach(function(t) { t.stop(); });
            Module._fluxMicStream = null;
        }
    });
}

void FluxMic::close()
{
    stop();
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

extern "C" void fluxMicWebInit()
{
    EM_ASM({
        Module._fluxMicPushFrames = Module.cwrap('fluxMicPushFrames', null, [ 'number', 'number' ]);
    });
}

#endif // __EMSCRIPTEN__