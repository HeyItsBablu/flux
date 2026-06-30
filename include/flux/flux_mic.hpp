// flux_mic.hpp
// Live microphone capture engine for FluxUI — declarations only.
//
// This is a pure capture source: open a device, get a stream of mono
// float32 frames via a callback, close it. No file I/O, no WAV writing,
// no "save" concept, no waveform-for-drawing helper, no recording-duration
// cap. What you do with the frames — encode + send over a network,
// accumulate into a buffer for later export, feed a meter — is entirely
// up to the caller.
//
// Platform backends (implementations live in separate .cpp / .mm files):
//   Android : flux_mic_android.cpp  — AAudio input stream
//   Windows : flux_mic_win32.cpp    — WASAPI capture client
//   Linux   : flux_mic_linux.cpp    — ALSA capture (FLOAT_LE)
//   macOS   : flux_mic_macos.mm     — AVAudioEngine input-node tap (ObjC++)
//   Web     : flux_mic_web.cpp      — getUserMedia + AudioWorklet/ScriptProcessor
//
// Usage (raw):
//   FluxMic::get().open(48000, 1);
//   FluxMic::get().start([](const float* samples, size_t count) {
//       opusEncodeAndSend(samples, count);     // or buffer locally, etc.
//   });
//   ...
//   FluxMic::get().stop();
//   FluxMic::get().close();
//
// Usage (reactive UI, see flux_mic_controller.hpp):
//   auto mic = Mic();
//   Conditional(mic->isRecording)->Then(...)->Else(...);
//
// IMPORTANT: FrameCallback fires on the platform's audio capture thread,
// NOT the UI thread. Keep it cheap (encode/enqueue only) — do not touch
// Widget/State objects from inside it directly.
//
// CMake linkage required:
//   Android : aaudio
//   Windows : ole32  mmdevapi
//   Linux   : asound
//   macOS   : AVFoundation  (already linked by the flux CMakeLists)
//
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

// ============================================================================
// macOS — C bridge declarations
//
// The implementation lives in flux_mic_macos.mm (ObjC++).
// Keeping these declarations here (not in the .mm) means any C++ TU can call
// FluxMicMacBridge_* without pulling in ObjC headers.
// ============================================================================
#if defined(__APPLE__)
#include <TargetConditionals.h>
#if TARGET_OS_OSX

extern "C"
{
    void *FluxMicMacBridge_open(int sampleRate, int channels, void *implPtr);
    void  FluxMicMacBridge_close(void *bridge);
    void  FluxMicMacBridge_destroy(void *bridge);
}

#endif // TARGET_OS_OSX
#endif // __APPLE__

// ============================================================================
// FluxMic class declaration
// ============================================================================

class FluxMic
{
public:
    static FluxMic &get();

    static constexpr int kDefaultSampleRate = 48000;
    static constexpr int kDefaultChannels   = 1;

    enum class State
    {
        Idle,
        Open,
        Recording,
        Error
    };

    // Fired on the capture thread for every buffer delivered by the OS.
    // samples: mono (or interleaved, if channels > 1) float32, range [-1, 1].
    // count: number of float values in `samples` (frames * channels).
    using FrameCallback = std::function<void(const float *samples, size_t count)>;

    // ── Lifecycle ─────────────────────────────────────────────────────────
    // open(): negotiate/create the input device at the given rate & channel
    //         count. Must be called (or implicitly called by start()) before
    //         capture begins.
    // start(): begin delivering frames via cb. Returns false on failure
    //          (device busy, permission denied, etc).
    // stop():  stop delivering frames; device stays open.
    // close(): release the device entirely.
    bool open(int sampleRate = kDefaultSampleRate, int channels = kDefaultChannels);
    bool start(FrameCallback cb);
    void stop();
    void close();

    // ── Status ────────────────────────────────────────────────────────────
    State getState() const;
    bool  isRecording() const;
    float getInputLevel() const;     // current RMS, 0..1 — poll for UI meters
    int   getSampleRate() const;
    int   getChannels() const;

private:
    FluxMic();
    ~FluxMic();

    // Each platform backend defines its own private members in its .cpp/.mm.
    // Forward-declared so this header stays free of platform-specific types.
    struct Impl;
    Impl *m_impl;
};