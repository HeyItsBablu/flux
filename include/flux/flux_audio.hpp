// flux_audio.hpp
// Audio engine for FluxUI — routing header only.
//
// Implementations live in platform-specific source files:
//   Android : flux_audio_android.cpp  — AAudio playback + recording
//   Windows : flux_audio_win32.cpp    — XAudio2 playback
//   Linux   : flux_audio_linux.cpp    — ALSA playback
//   macOS   : flux_audio_macos.mm     — AVAudioEngine playback (stub)
//
// Common interface (all platforms):
//   FluxAudio::get().playFromPath(path)
//   FluxAudio::get().playPCM(samples, sampleRate)
//   FluxAudio::get().playStream(callback, sampleRate)
//   FluxAudio::get().pause() / resume() / stopPlayback() / closePlayback()
//   FluxAudio::get().seekToProgress(t) / seekToSeconds(s)
//   FluxAudio::get().setVolume(v) / getVolume()
//   FluxAudio::get().getProgress() / getPositionSeconds() / getDurationSeconds()
//   FluxAudio::get().isPlaying() / isPaused()
//   FluxAudio::get().setOnFinished(cb)
//   FluxAudio::get().shutdown()
//   FluxAudio::buildWaveform(...)
//
// Android-only:
//   FluxAudio::get().openRecord(sampleRate, channels)
//   FluxAudio::get().startRecord(cb) / startRecordToBuffer()
//   FluxAudio::get().stopRecord() / closeRecord()
//   FluxAudio::get().isRecording() / getInputLevel()
//   FluxAudio::get().getRecording() / clearRecording()
//
#pragma once

#include <atomic>
#include <cmath>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

// ============================================================================
// Shared waveform utility  (header-only, all platforms)
// ============================================================================
namespace FluxAudioDetail {

inline std::vector<std::pair<int,int>> buildWaveform(
        const float* samples, int count,
        int x, int y, int w, int h, int points = 200)
{
    std::vector<std::pair<int,int>> result;
    if (count == 0 || points <= 0) return result;
    result.reserve(points);
    float step  = (float)count / points;
    int   halfH = h / 2;
    int   midY  = y + halfH;
    for (int i = 0; i < points; i++) {
        int   start = (int)(i * step);
        int   end   = std::min((int)((i + 1) * step), count);
        float peak  = 0.f;
        for (int j = start; j < end; j++)
            peak = std::max(peak, std::abs(samples[j]));
        result.emplace_back(x + (i * w / points), midY - (int)(peak * halfH));
    }
    return result;
}

} // namespace FluxAudioDetail


// ============================================================================
// FluxAudio — forward declaration of the class.
// The full definition is in the platform implementation files.
// flux_audioplayer.hpp and other widgets include this header and call
// FluxAudio::get(), so the class must be declared here.
// ============================================================================

class FluxAudio {
public:
    using FinishCallback = std::function<void()>;
    using StreamCallback = std::function<int(float* buf, int frames)>;

#ifdef __ANDROID__
    using RecordCallback = std::function<void(const float* samples, int count)>;
    using PlayCallback   = std::function<int(float* buffer, int maxFrames)>;
#endif

    static FluxAudio& get();

    // ── Volume ────────────────────────────────────────────────────────────────
    void  setVolume(float v);
    float getVolume() const;

    // ── Progress ──────────────────────────────────────────────────────────────
    float getProgress()        const;
    float getPositionSeconds() const;
    float getDurationSeconds() const;

    // ── Seek ──────────────────────────────────────────────────────────────────
    void seekToProgress(float progress);
    void seekToSeconds(float seconds);

    // ── Callbacks ─────────────────────────────────────────────────────────────
    void setOnFinished(FinishCallback cb);

    // ── File playback ─────────────────────────────────────────────────────────
    bool playFromPath(const std::string& path);

    // ── Pause / Resume ────────────────────────────────────────────────────────
    void pause();
    void resume();
    bool isPaused()  const;
    bool isPlaying() const;

    // ── PCM / stream playback ─────────────────────────────────────────────────
    bool playPCM(const std::vector<float>& samples, int sampleRate = 44100);
    bool playStream(StreamCallback cb, int sampleRate = 44100);
    bool startPlayback();
    void stopPlayback();
    void closePlayback();

    void shutdown();

    // ── Waveform (static helpers — delegate to FluxAudioDetail) ───────────────
    static std::vector<std::pair<int,int>> buildWaveform(
            const float* samples, int count,
            int x, int y, int w, int h, int points = 200)
    {
        return FluxAudioDetail::buildWaveform(samples, count, x, y, w, h, points);
    }

    static std::vector<std::pair<int,int>> buildWaveform(
            const std::vector<float>& samples,
            int x, int y, int w, int h, int points = 200)
    {
        return FluxAudioDetail::buildWaveform(
                samples.data(), (int)samples.size(), x, y, w, h, points);
    }

    // ── Recording (Android only; stubs on other platforms) ────────────────────
#ifdef __ANDROID__
    bool openRecord(int sampleRate = 44100, int channels = 1);
    bool startRecord(RecordCallback cb);
    bool startRecordToBuffer();
    void stopRecord();
    void closeRecord();
    bool isRecording()  const;
    float getInputLevel() const;
    const std::vector<float>& getRecording() const;
    void  clearRecording();
    int   getRecordSampleRate() const;

    bool openPlayback(int sampleRate = 44100, int channels = 1);
#else
    bool  isRecording()   const { return false; }
    float getInputLevel() const { return 0.f; }
#endif

private:
    FluxAudio();
    ~FluxAudio();

    struct Impl;
    Impl* m_impl;
};