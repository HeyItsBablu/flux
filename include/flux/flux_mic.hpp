// flux_mic.hpp
// Microphone capture engine for FluxUI — declarations only.
//
// Platform backends (implementations live in separate .cpp / .mm files):
//   Android : flux_mic_android.cpp  — AAudio (AAUDIO_INPUT_PRESET_VOICE_RECOGNITION)
//   Windows : flux_mic_win32.cpp    — WASAPI (shared mode, auto format-detect)
//   Linux   : flux_mic_linux.cpp    — ALSA   (FLOAT_LE → S16_LE fallback)
//   macOS   : flux_mic_macos.mm     — AVAudioEngine input tap (ObjC++)
//
// Common pipeline (all platforms):
//   Audio input  →  mono float32 ring buffer (5-min cap, 48 kHz)
//               →  RMS amplitude tracker   (for waveform widget)
//               →  WAV file writer          (timestamped, on stop)
//
// Usage:
//   FluxMic::get().start();            // begin capture
//   FluxMic::get().stop();             // stop + save WAV
//   FluxMic::get().getAmplitude();     // current RMS (0.0–1.0)
//   FluxMic::get().getWaveform(v, 80); // fill vector with 80 RMS bars
//
// CMake linkage required:
//   Android : aaudio
//   Windows : ole32  mmdevapi
//   Linux   : asound
//   macOS   : AVFoundation  (already linked by the flux CMakeLists)
//
#pragma once

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// ============================================================================
// WAV file writer  (header-only, compiled on every platform)
// ============================================================================
namespace WavWriter {

#pragma pack(push, 1)
struct Header {
    char     riff[4]       = {'R','I','F','F'};
    uint32_t chunkSize     = 0;
    char     wave[4]       = {'W','A','V','E'};
    char     fmt[4]        = {'f','m','t',' '};
    uint32_t fmtSize       = 16;
    uint16_t audioFormat   = 1;     // PCM int16
    uint16_t numChannels   = 1;
    uint32_t sampleRate    = 48000;
    uint32_t byteRate      = 0;
    uint16_t blockAlign    = 2;
    uint16_t bitsPerSample = 16;
    char     data[4]       = {'d','a','t','a'};
    uint32_t dataSize      = 0;
};
#pragma pack(pop)

inline std::string write(const std::string& path,
                         const float*       samples,
                         size_t             numSamples,
                         uint32_t           sampleRate  = 48000,
                         uint16_t           numChannels = 1)
{
#ifdef _WIN32
    FILE* f = nullptr;
    if (fopen_s(&f, path.c_str(), "wb") != 0 || !f) return "";
#else
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return "";
#endif

    Header hdr;
    hdr.numChannels  = numChannels;
    hdr.sampleRate   = sampleRate;
    hdr.blockAlign   = numChannels * 2;
    hdr.byteRate     = sampleRate * hdr.blockAlign;

    uint32_t dataBytes = static_cast<uint32_t>(numSamples * 2);
    hdr.dataSize  = dataBytes;
    hdr.chunkSize = 4 + 8 + 16 + 8 + dataBytes;

    fwrite(&hdr, sizeof(hdr), 1, f);

    std::vector<int16_t> pcm16(numSamples);
    for (size_t i = 0; i < numSamples; i++) {
        float c = std::max(-1.f, std::min(1.f, samples[i]));
        pcm16[i] = static_cast<int16_t>(c * 32767.f);
    }
    fwrite(pcm16.data(), sizeof(int16_t), numSamples, f);
    fclose(f);
    return path;
}

inline std::string makeTimestampedPath(const std::string& dir) {
    std::time_t t = std::time(nullptr);
    char buf[64];
#ifdef _WIN32
    std::tm tm{};
    localtime_s(&tm, &t);
    std::strftime(buf, sizeof(buf), "rec_%Y%m%d_%H%M%S.wav", &tm);
#else
    std::tm* tm = std::localtime(&t);
    std::strftime(buf, sizeof(buf), "rec_%Y%m%d_%H%M%S.wav", tm);
#endif
#ifdef _WIN32
    return dir + "\\" + buf;
#else
    return dir + "/" + buf;
#endif
}

} // namespace WavWriter


// ============================================================================
// Shared waveform helper
// ============================================================================
namespace FluxMicDetail {

inline void buildWaveform(const std::vector<float>& ring,
                          size_t                    written,
                          std::vector<float>&        out,
                          size_t                     count)
{
    out.resize(count, 0.f);
    if (written == 0 || count == 0) return;

    size_t windowSize = std::max<size_t>(64, written / count);
    size_t start      = (written > count * windowSize)
                        ? written - count * windowSize : 0;

    for (size_t i = 0; i < count; i++) {
        size_t wStart = start + i * windowSize;
        size_t wEnd   = std::min(wStart + windowSize, written);
        float  sum    = 0.f;
        for (size_t j = wStart; j < wEnd; j++)
            sum += ring[j] * ring[j];
        out[i] = (wEnd > wStart) ? std::sqrt(sum / (wEnd - wStart)) : 0.f;
    }
}

} // namespace FluxMicDetail


// ============================================================================
// macOS — C bridge declarations
//
// The implementation lives in flux_mic_macos.mm (ObjC++).
// Keeping these declarations here (not in the .mm) means any C++ TU can call
// FluxMacBridge_* without pulling in ObjC headers.
// ============================================================================
#if defined(__APPLE__)
#include <TargetConditionals.h>
#if TARGET_OS_OSX

extern "C" {
    void* FluxMacBridge_open(float*               ring,
                              size_t               ringSize,
                              std::atomic<size_t>* writePos,
                              std::atomic<float>*  amplitude,
                              std::atomic<bool>*   stopFlag,
                              std::atomic<int>*    state);
    void  FluxMacBridge_close(void* bridge);
    void  FluxMacBridge_destroy(void* bridge);
    // Caller must free() the returned string.
    char* FluxMacBridge_getSaveDir();
}

#endif // TARGET_OS_OSX
#endif // __APPLE__


// ============================================================================
// FluxMic class declaration
// ============================================================================

class FluxMic {
public:
    static FluxMic& get();

    static constexpr int    kSampleRate = 48000;
    static constexpr int    kChannels   = 1;
    static constexpr int    kMaxSeconds = 300;
    static constexpr size_t kRingSize   = static_cast<size_t>(kSampleRate) * kMaxSeconds;

    // macOS stores state as int so the ObjC++ bridge can write it atomically.
    // All other platforms use the enum directly.
    enum class State { Idle, Recording, Stopping, Error };

    State  getState()          const;
    bool   isRecording()       const;
    float  getAmplitude()      const;
    float  getProgress()       const;
    float  getElapsedSeconds() const;
    const std::string& getLastSavedPath() const;

    using SaveCallback = std::function<void(const std::string&)>;
    void setOnSaved(SaveCallback cb);

    bool start();
    void stop();
    void cancel();

    void getWaveform(std::vector<float>& out, size_t count) const;

private:
    FluxMic();
    ~FluxMic();

    // Each platform backend defines its own private members in its .cpp/.mm.
    // We forward-declare the Impl struct here and keep a pointer to it so
    // the header stays free of platform-specific types.
    struct Impl;
    Impl* m_impl;
};