// src/flux_mic_web.cpp
//
// Microphone capture backend for Emscripten / WebAssembly.
//
// Key constraint: FluxMic::Impl is a private nested type — the compiler
// rejects any reference to FluxMic::Impl outside of FluxMic's own member
// functions, even in the same .cpp.  Solution: define a plain file-local
// struct (MicImpl) that contains all the state, then make FluxMic::Impl
// inherit from it (an empty struct).  All helpers work with MicImpl* so
// they never touch the private name.  FluxMic member functions cast
// m_impl (which IS FluxMic::Impl*) to MicImpl* via the base pointer.

#ifdef __EMSCRIPTEN__

#include "flux/flux_mic.hpp"

#include <emscripten.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

// ============================================================================
// MicImpl — file-local struct that holds all the actual state.
// Not named FluxMic::Impl, so it is freely accessible everywhere in this TU.
// ============================================================================

struct MicImpl
{
    std::vector<float>    ring;
    std::atomic<size_t>   writePos{0};
    std::atomic<float>    amplitude{0.f};

    // 0=Idle  1=Recording  2=Stopping  3=Error
    std::atomic<int>      state{0};

    double                startTimeMs{0.0};

    FluxMic::SaveCallback onSaved;
    std::string           lastSavedPath;

    MicImpl() { ring.resize(FluxMic::kRingSize, 0.f); }

    void teardownJS()
    {
        EM_ASM({
            var sp = Module._fluxMicSP;
            if (sp) {
                sp.onaudioprocess = null;
                sp.disconnect();
                Module._fluxMicSP = null;
            }
            var src = Module._fluxMicSource;
            if (src) { src.disconnect(); Module._fluxMicSource = null; }
            var stream = Module._fluxMicStream;
            if (stream) {
                stream.getTracks().forEach(function(t){ t.stop(); });
                Module._fluxMicStream = null;
            }
            var ac = Module._fluxMicAC;
            if (ac) { ac.close(); Module._fluxMicAC = null; }
            if (Module._fluxMicHeapBuf) {
                Module._free(Module._fluxMicHeapBuf);
                Module._fluxMicHeapBuf = null;
                Module._fluxMicBufSize = 0;
            }
        });
    }

    std::string saveWav()
    {
        size_t captured = writePos.load();
        if (captured == 0) return "";

        int sr = EM_ASM_INT({ return Module._fluxMicSampleRate | 0; });
        if (sr <= 0) sr = FluxMic::kSampleRate;

        size_t count = std::min(captured, FluxMic::kRingSize);
        std::vector<float> linear(count);

        if (captured <= FluxMic::kRingSize) {
            std::memcpy(linear.data(), ring.data(), count * sizeof(float));
        } else {
            size_t oldest = captured % FluxMic::kRingSize;
            size_t tail   = FluxMic::kRingSize - oldest;
            std::memcpy(linear.data(),        ring.data() + oldest, tail   * sizeof(float));
            std::memcpy(linear.data() + tail, ring.data(),          oldest * sizeof(float));
        }

        EM_ASM({ try { FS.mkdir('/recordings'); } catch(e){} });

        std::string path  = WavWriter::makeTimestampedPath("/recordings");
        std::string saved = WavWriter::write(path, linear.data(), count,
                                             (uint32_t)sr, 1);
        return saved;
    }
};

// ============================================================================
// FluxMic::Impl — inherits MicImpl so FluxMic::m_impl IS-A MicImpl*.
// The body is empty; all real fields live in MicImpl.
// ============================================================================

struct FluxMic::Impl : MicImpl {};

// ============================================================================
// Convenience cast — used by every FluxMic member function.
// ============================================================================

static inline MicImpl *mi(void *p) { return static_cast<MicImpl *>(p); }

// ============================================================================
// File-local helpers — work with MicImpl*, never touch FluxMic::Impl.
// ============================================================================

static void mic_write_frames(MicImpl *m, float *heapPtr, int frames)
{
    if (!m || !heapPtr || frames <= 0) return;
    if (m->state.load() != 1) return;   // not Recording

    size_t pos = m->writePos.load();
    for (int i = 0; i < frames; ++i) {
        m->ring[pos % FluxMic::kRingSize] = heapPtr[i];
        ++pos;
    }
    m->writePos.store(pos);

    float sum = 0.f;
    for (int i = 0; i < frames; ++i) sum += heapPtr[i] * heapPtr[i];
    float rms  = std::sqrt(sum / (float)frames);
    float prev = m->amplitude.load();
    m->amplitude.store(prev * 0.7f + rms * 0.3f);
}

static void mic_on_error(MicImpl *m)
{
    if (!m) return;
    m->state.store(3);
    m->amplitude.store(0.f);
}

static void mic_on_ready(MicImpl *m)
{
    if (!m) return;
    m->state.store(1);
    m->startTimeMs = emscripten_get_now();
}

// ============================================================================
// extern "C" thunks — JS calls these via cwrap().
// Receive plain ints (wasm32 pointers are 32-bit).
// Cast to MicImpl* — never mention FluxMic::Impl.
// ============================================================================

extern "C" {

EMSCRIPTEN_KEEPALIVE
void fluxMicWriteFrames(int implInt, int heapInt, int frames)
{
    mic_write_frames(
        reinterpret_cast<MicImpl *>(static_cast<uintptr_t>(implInt)),
        reinterpret_cast<float *>(static_cast<uintptr_t>(heapInt)),
        frames);
}

EMSCRIPTEN_KEEPALIVE
void fluxMicOnError(int implInt)
{
    mic_on_error(
        reinterpret_cast<MicImpl *>(static_cast<uintptr_t>(implInt)));
}

EMSCRIPTEN_KEEPALIVE
void fluxMicOnReady(int implInt)
{
    mic_on_ready(
        reinterpret_cast<MicImpl *>(static_cast<uintptr_t>(implInt)));
}

} // extern "C"

// ============================================================================
// One-time JS bootstrap
// ============================================================================

static void fluxMicWebInit()
{
    EM_ASM({
        if (Module._fluxMicWebInited) return;
        Module._fluxMicWebInited = true;

        Module._fluxMicWriteFrames = Module.cwrap(
            'fluxMicWriteFrames', null, ['number','number','number']);
        Module._fluxMicOnError = Module.cwrap(
            'fluxMicOnError', null, ['number']);
        Module._fluxMicOnReady = Module.cwrap(
            'fluxMicOnReady', null, ['number']);

        Module._fluxMicAC         = null;
        Module._fluxMicStream     = null;
        Module._fluxMicSource     = null;
        Module._fluxMicSP         = null;
        Module._fluxMicHeapBuf    = null;
        Module._fluxMicBufSize    = 0;
        Module._fluxMicSampleRate = 48000;
    });
}

// ============================================================================
// FluxMic public API
// ============================================================================

FluxMic &FluxMic::get()
{
    static FluxMic inst;
    return inst;
}

FluxMic::FluxMic() : m_impl(new Impl())
{
    fluxMicWebInit();
}

FluxMic::~FluxMic()
{
    cancel();
    delete m_impl;
}

FluxMic::State FluxMic::getState() const
{
    return static_cast<FluxMic::State>(mi(m_impl)->state.load());
}

bool FluxMic::isRecording() const { return mi(m_impl)->state.load() == 1; }

float FluxMic::getAmplitude() const { return mi(m_impl)->amplitude.load(); }

float FluxMic::getProgress() const
{
    return std::min(1.f, getElapsedSeconds() / (float)kMaxSeconds);
}

float FluxMic::getElapsedSeconds() const
{
    if (mi(m_impl)->state.load() != 1) return 0.f;
    return (float)((emscripten_get_now() - mi(m_impl)->startTimeMs) / 1000.0);
}

const std::string &FluxMic::getLastSavedPath() const
{
    return mi(m_impl)->lastSavedPath;
}

void FluxMic::setOnSaved(SaveCallback cb)
{
    mi(m_impl)->onSaved = std::move(cb);
}

void FluxMic::getWaveform(std::vector<float> &out, size_t count) const
{
    FluxMicDetail::buildWaveform(mi(m_impl)->ring,
                                 mi(m_impl)->writePos.load(),
                                 out, count);
}

bool FluxMic::start()
{
    int s = mi(m_impl)->state.load();
    if (s == 1 || s == 2) return false;  // Recording or Stopping

    mi(m_impl)->writePos.store(0);
    mi(m_impl)->amplitude.store(0.f);
    mi(m_impl)->lastSavedPath.clear();
    std::fill(mi(m_impl)->ring.begin(), mi(m_impl)->ring.end(), 0.f);

    // Pass the MicImpl* as int — safe on wasm32 (pointers are 32-bit).
    int implInt = static_cast<int>(
        reinterpret_cast<uintptr_t>(static_cast<MicImpl *>(m_impl)));

    EM_ASM({
        var implInt = $0;
        var BUF_SZ  = 4096;

        navigator.mediaDevices.getUserMedia({ audio: true, video: false })
            .then(function(stream) {
                var AC = window.AudioContext || window.webkitAudioContext;
                var ac = new AC();
                Module._fluxMicAC         = ac;
                Module._fluxMicStream     = stream;
                Module._fluxMicSampleRate = ac.sampleRate;

                var heapBuf = Module._malloc(BUF_SZ * 4);
                Module._fluxMicHeapBuf = heapBuf;
                Module._fluxMicBufSize = BUF_SZ;

                var source = ac.createMediaStreamSource(stream);
                Module._fluxMicSource = source;

                var sp = ac.createScriptProcessor(BUF_SZ, 1, 1);
                Module._fluxMicSP = sp;

                sp.onaudioprocess = function(e) {
                    var inputData = e.inputBuffer.getChannelData(0);
                    var frames    = inputData.length;

                    if (frames > Module._fluxMicBufSize) {
                        Module._free(Module._fluxMicHeapBuf);
                        Module._fluxMicHeapBuf = Module._malloc(frames * 4);
                        Module._fluxMicBufSize = frames;
                    }

                    var heapView = new Float32Array(
                        Module.HEAPF32.buffer,
                        Module._fluxMicHeapBuf,
                        frames);
                    heapView.set(inputData);

                    Module._fluxMicWriteFrames(
                        implInt,
                        Module._fluxMicHeapBuf,
                        frames);
                };

                source.connect(sp);
                sp.connect(ac.destination);

                Module._fluxMicOnReady(implInt);
            })
            .catch(function(err) {
                console.error('[FluxMic] getUserMedia failed:', err);
                Module._fluxMicOnError(implInt);
            });
    }, implInt);

    return true;
}

void FluxMic::stop()
{
    if (mi(m_impl)->state.load() != 1) return;

    mi(m_impl)->state.store(2);
    mi(m_impl)->amplitude.store(0.f);
    mi(m_impl)->teardownJS();

    std::string path = mi(m_impl)->saveWav();
    mi(m_impl)->lastSavedPath = path;
    mi(m_impl)->state.store(0);

    if (!path.empty() && mi(m_impl)->onSaved)
        mi(m_impl)->onSaved(path);
}

void FluxMic::cancel()
{
    if (mi(m_impl)->state.load() == 0) return;

    mi(m_impl)->state.store(2);
    mi(m_impl)->teardownJS();
    mi(m_impl)->writePos.store(0);
    mi(m_impl)->amplitude.store(0.f);
    mi(m_impl)->state.store(0);
}

#endif // __EMSCRIPTEN__