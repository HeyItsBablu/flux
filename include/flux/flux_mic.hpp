// flux_mic.hpp
// AAudio microphone capture engine for FluxUI on Android.
//
// Pipeline:
//   AAudio Input Stream (float32, mono, 48kHz, polling thread)
//       → Ring buffer (5 min max)
//       → RMS amplitude tracker (for waveform widget)
//       → WAV file writer (timestamped, on stop)
//
// Usage:
//   FluxMic::get().start();          // begin capture
//   FluxMic::get().stop();           // stop + save WAV
//   FluxMic::get().getAmplitude();   // current RMS (0.0–1.0) for widget
//
// Requires RECORD_AUDIO permission in AndroidManifest.xml:
//   <uses-permission android:name="android.permission.RECORD_AUDIO"/>
// And runtime permission granted before calling start().
//
// Dependencies:
//   - aaudio (add to CMakeLists target_link_libraries)
//   - flux_audio.hpp is NOT required — FluxMic is standalone
//
#pragma once
#ifdef __ANDROID__

#include <aaudio/AAudio.h>
#include <android/log.h>

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

#define MIC_LOGI(...) __android_log_print(ANDROID_LOG_INFO,  "FluxMic", __VA_ARGS__)
#define MIC_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "FluxMic", __VA_ARGS__)

// ── External: provided by native-lib.cpp ─────────────────────────────────────
// Returns the app's internal files directory (e.g. /data/data/com.example/files)
// Implemented as a small JNI shim — see native-lib.cpp additions below.
extern std::string FluxAndroid_getFilesDir();

// ============================================================================
// WAV helpers
// ============================================================================
namespace WavWriter {

#pragma pack(push, 1)
    struct Header {
        char     riff[4]       = {'R','I','F','F'};
        uint32_t chunkSize     = 0;
        char     wave[4]       = {'W','A','V','E'};
        char     fmt[4]        = {'f','m','t',' '};
        uint32_t fmtSize       = 16;
        uint16_t audioFormat   = 1;         // PCM int16 — plays everywhere
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
        FILE* f = fopen(path.c_str(), "wb");
        if (!f) { MIC_LOGE("Cannot open WAV for write: %s", path.c_str()); return ""; }

        Header hdr;
        hdr.numChannels  = numChannels;
        hdr.sampleRate   = sampleRate;
        hdr.blockAlign   = numChannels * 2;
        hdr.byteRate     = sampleRate * hdr.blockAlign;

        uint32_t dataBytes = static_cast<uint32_t>(numSamples * 2);
        hdr.dataSize  = dataBytes;
        hdr.chunkSize = 4 + 8 + 16 + 8 + dataBytes;

        fwrite(&hdr, sizeof(hdr), 1, f);

        // Convert float32 → int16
        std::vector<int16_t> pcm16(numSamples);
        for (size_t i = 0; i < numSamples; i++) {
            float clamped = std::max(-1.f, std::min(1.f, samples[i]));
            pcm16[i] = static_cast<int16_t>(clamped * 32767.f);
        }
        fwrite(pcm16.data(), sizeof(int16_t), numSamples, f);
        fclose(f);

        MIC_LOGI("WAV written (PCM16): %s (%zu samples, %.2f s)",
                 path.c_str(), numSamples, (double)numSamples / sampleRate);
        return path;
    }

    inline std::string makeTimestampedPath(const std::string& dir) {
        std::time_t t  = std::time(nullptr);
        std::tm*    tm = std::localtime(&t);
        char buf[64];
        std::strftime(buf, sizeof(buf), "rec_%Y%m%d_%H%M%S.wav", tm);
        return dir + "/" + buf;
    }

} // namespace WavWriter

// ============================================================================
// FluxMic
// ============================================================================

class FluxMic {
public:
    // ── Singleton ─────────────────────────────────────────────────────────────
    static FluxMic& get() {
        static FluxMic instance;
        return instance;
    }

    // ── Constants ─────────────────────────────────────────────────────────────
    static constexpr int     kSampleRate   = 48000;
    static constexpr int     kChannels     = 1;
    static constexpr int     kMaxSeconds   = 300;          // 5 minutes
    static constexpr size_t  kRingSize     = kSampleRate * kMaxSeconds; // 14,400,000

    // ── State ─────────────────────────────────────────────────────────────────
    enum class State { Idle, Recording, Stopping, Error };

    State  getState()        const { return s_state.load(); }
    bool   isRecording()     const { return s_state == State::Recording; }

    // RMS amplitude over the last ~50ms window, range 0.0–1.0
    float  getAmplitude()    const { return s_amplitude.load(); }

    // Progress 0.0–1.0 of the 5-minute cap
    float  getProgress()     const {
        size_t written = s_writePos.load();
        return std::min(1.f, (float)written / (float)kRingSize);
    }

    // Elapsed recording time in seconds
    float  getElapsedSeconds() const {
        return (float)s_writePos.load() / (float)kSampleRate;
    }

    // Path of the last successfully saved WAV file
    const std::string& getLastSavedPath() const { return s_lastSavedPath; }

    // ── Callbacks ─────────────────────────────────────────────────────────────
    using SaveCallback = std::function<void(const std::string& path)>;
    void setOnSaved(SaveCallback cb) { s_onSaved = std::move(cb); }

    // ── Start recording ───────────────────────────────────────────────────────
    bool start() {
        if (s_state == State::Recording) return true;
        if (s_state == State::Stopping)  return false;

        // Reset ring buffer
        s_ring.assign(kRingSize, 0.f);
        s_writePos  = 0;
        s_amplitude = 0.f;

        // Build AAudio input stream
        AAudioStreamBuilder* builder = nullptr;
        AAudio_createStreamBuilder(&builder);

        AAudioStreamBuilder_setDirection(builder,      AAUDIO_DIRECTION_INPUT);
        AAudioStreamBuilder_setSampleRate(builder,     kSampleRate);
        AAudioStreamBuilder_setChannelCount(builder,   kChannels);
        AAudioStreamBuilder_setFormat(builder,         AAUDIO_FORMAT_PCM_FLOAT);
        AAudioStreamBuilder_setInputPreset(builder,    AAUDIO_INPUT_PRESET_VOICE_RECOGNITION);
        AAudioStreamBuilder_setPerformanceMode(builder,AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
        AAudioStreamBuilder_setSharingMode(builder,    AAUDIO_SHARING_MODE_EXCLUSIVE);

        aaudio_result_t res = AAudioStreamBuilder_openStream(builder, &s_stream);
        AAudioStreamBuilder_delete(builder);

        if (res != AAUDIO_OK || !s_stream) {
            MIC_LOGE("AAudio openStream failed: %s", AAudio_convertResultToText(res));
            s_state = State::Error;
            return false;
        }

        res = AAudioStream_requestStart(s_stream);
        if (res != AAUDIO_OK) {
            MIC_LOGE("AAudioStream_requestStart failed: %s", AAudio_convertResultToText(res));
            AAudioStream_close(s_stream);
            s_stream = nullptr;
            s_state  = State::Error;
            return false;
        }

        s_state     = State::Recording;
        s_stopPoll  = false;

        // Spin up polling thread
        s_pollThread = std::thread(&FluxMic::_pollLoop, this);

        MIC_LOGI("Recording started — max %.0f s", (float)kMaxSeconds);
        return true;
    }

    // ── Stop recording + save WAV ──────────────────────────────────────────────
    // Non-blocking: sets flag, thread drains and saves, then calls onSaved.
    void stop() {
        if (s_state != State::Recording) return;
        s_state    = State::Stopping;
        s_stopPoll = true;

        if (s_pollThread.joinable()) s_pollThread.join();

        _closeStream();
        _saveWAV();

        s_state = State::Idle;
    }

    // ── Discard without saving ────────────────────────────────────────────────
    void cancel() {
        if (s_state != State::Recording) return;
        s_state    = State::Stopping;
        s_stopPoll = true;

        if (s_pollThread.joinable()) s_pollThread.join();

        _closeStream();

        s_writePos  = 0;
        s_amplitude = 0.f;
        s_state     = State::Idle;

        MIC_LOGI("Recording cancelled — discarded");
    }

    // ── Waveform snapshot for widget ──────────────────────────────────────────
    // Fills `out` with `count` RMS values evenly sampled across the recorded
    // buffer so far — suitable for drawing a scrolling waveform.
    void getWaveform(std::vector<float>& out, size_t count) const {
        size_t written = s_writePos.load();
        out.resize(count, 0.f);
        if (written == 0 || count == 0) return;

        // We want the last `count` windows worth of samples.
        // Window size = written / count (or minimum 64 samples).
        size_t windowSize = std::max<size_t>(64, written / count);
        size_t start      = (written > count * windowSize)
                            ? written - count * windowSize
                            : 0;

        for (size_t i = 0; i < count; i++) {
            size_t wStart = start + i * windowSize;
            size_t wEnd   = std::min(wStart + windowSize, written);
            float  sum    = 0.f;
            for (size_t j = wStart; j < wEnd; j++)
                sum += s_ring[j] * s_ring[j];
            out[i] = (wEnd > wStart)
                     ? std::sqrt(sum / (wEnd - wStart))
                     : 0.f;
        }
    }

private:
    FluxMic()  { s_ring.reserve(kRingSize); }
    ~FluxMic() { cancel(); }

    // ── Ring buffer ───────────────────────────────────────────────────────────
    std::vector<float>   s_ring;
    std::atomic<size_t>  s_writePos  { 0 };
    std::atomic<float>   s_amplitude { 0.f };

    // ── AAudio ────────────────────────────────────────────────────────────────
    AAudioStream*        s_stream    = nullptr;

    // ── Thread ────────────────────────────────────────────────────────────────
    std::thread          s_pollThread;
    std::atomic<bool>    s_stopPoll  { false };

    // ── State ─────────────────────────────────────────────────────────────────
    std::atomic<State>   s_state     { State::Idle };
    std::string          s_lastSavedPath;
    SaveCallback         s_onSaved;

    // ── Polling loop ──────────────────────────────────────────────────────────
    // Reads AAudio frames into the ring buffer at ~10ms intervals.
    // Keeps consistent with FluxVideo's _decodeLoop style.
    void _pollLoop() {
        MIC_LOGI("Poll loop started");
        // Read chunk size: 10ms worth of samples
        static constexpr int kChunkFrames = kSampleRate / 100; // 480 frames
        static constexpr int kTimeoutNs   = 10'000'000;        // 10ms

        std::vector<float> tmp(kChunkFrames);

        // RMS window accumulator
        float   rmsAccum    = 0.f;
        int     rmsSamples  = 0;
        static constexpr int kRmsWindow = kSampleRate / 20; // 50ms

        while (!s_stopPoll) {
            if (!s_stream) break;

            aaudio_result_t framesRead = AAudioStream_read(
                    s_stream, tmp.data(), kChunkFrames, kTimeoutNs);

            if (framesRead < 0) {
                // AAUDIO_ERROR_TIMEOUT is normal when mic is quiet — continue
                if (framesRead != AAUDIO_ERROR_TIMEOUT) {
                    MIC_LOGE("AAudioStream_read error: %s",
                             AAudio_convertResultToText(framesRead));
                }
                continue;
            }

            if (framesRead == 0) continue;

            size_t wp = s_writePos.load();

            // Stop if ring is full (5-minute cap reached)
            if (wp >= kRingSize) {
                MIC_LOGI("Max recording duration reached — stopping");
                s_stopPoll = true;
                s_state    = State::Stopping;
                break;
            }

            size_t toWrite = std::min<size_t>((size_t)framesRead,
                                              kRingSize - wp);

            // Write to ring
            memcpy(s_ring.data() + wp, tmp.data(), toWrite * sizeof(float));
            s_writePos.store(wp + toWrite);

            // Update RMS amplitude
            for (size_t i = 0; i < toWrite; i++) {
                float s  = tmp[i];
                rmsAccum += s * s;
                rmsSamples++;

                if (rmsSamples >= kRmsWindow) {
                    float rms = std::sqrt(rmsAccum / rmsSamples);
                    // Smooth with a simple IIR: 70% old, 30% new
                    float prev = s_amplitude.load();
                    s_amplitude.store(prev * 0.7f + rms * 0.3f);
                    rmsAccum   = 0.f;
                    rmsSamples = 0;
                }
            }
        }

        MIC_LOGI("Poll loop exited — %zu samples captured (%.2f s)",
                 s_writePos.load(),
                 (float)s_writePos.load() / kSampleRate);
    }

    // ── Close AAudio stream ───────────────────────────────────────────────────
    void _closeStream() {
        if (!s_stream) return;
        AAudioStream_requestStop(s_stream);
        AAudioStream_close(s_stream);
        s_stream = nullptr;
    }

    // ── Save WAV ──────────────────────────────────────────────────────────────
    void _saveWAV() {
        size_t numSamples = s_writePos.load();
        if (numSamples == 0) {
            MIC_LOGE("Nothing captured — no WAV written");  // change to LOGE so it's visible
            return;
        }

        std::string dir  = FluxAndroid_getFilesDir();
        MIC_LOGI("Files dir: '%s'", dir.c_str());          // ← add this
        MIC_LOGI("Samples to write: %zu (%.2f s)",
                 numSamples, (float)numSamples / kSampleRate);

        std::string path   = WavWriter::makeTimestampedPath(dir);
        MIC_LOGI("WAV path: '%s'", path.c_str());          // ← add this

        std::string result = WavWriter::write(
                path, s_ring.data(), numSamples, kSampleRate, kChannels);

        if (!result.empty()) {
            s_lastSavedPath = result;
            if (s_onSaved) s_onSaved(result);
        } else {
            MIC_LOGE("WAV write FAILED for path: '%s'", path.c_str()); // ← add this
        }
    }
};

#endif // __ANDROID__

//saves to accessable not to private folder
// Instead of FluxAndroid_getFilesDir()
// Returns a public URI + opens a write stream directly
// Used by FluxMic, FluxCamera for all media saves
//std::string FluxAndroid_saveToMediaStore(
//        const std::string& filename,   // e.g. "rec_20260404.wav"
//        const std::string& mimeType,   // e.g. "audio/wav"
//        const std::string& subFolder,  // e.g. "Music/FluxMic"
//        MediaType type                 // Audio / Image / Video
//);