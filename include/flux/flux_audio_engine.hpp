// flux_audio_engine.hpp
//
// Cross-platform, multi-voice audio engine built on miniaudio.
//
// Design summary
// ──────────────
// - One ma_device drives one audio callback. All mixing happens there.
// - SampleBank holds fully-decoded, engine-format (float32, N channels,
//   engine sample rate) audio in memory. Decoding happens once, at load
//   time, on the calling (UI) thread — never inside the audio callback.
// - A fixed pool of Voices allows many overlapping playbacks of the same
//   or different samples ("polyphony"), each with its own gain/pan/loop
//   state and playhead.
// - VoiceHandle encodes a slot index + generation counter so stale
//   handles (from a finished/reused voice) are detected safely.
// - Commands (start/stop/gain/pan/seek) are pushed from the calling
//   thread into a single-producer/single-consumer ring buffer and
//   applied by the audio callback — this keeps the callback itself
//   free of locks and file/memory allocation.
//
// Known simplification (documented, not hidden): starting a voice hands
// the audio thread a std::shared_ptr<DecodedSample>, which does an atomic
// refcount increment on the audio thread. This is not perfectly
// allocation-free but is a common, safe compromise; it can be replaced
// later with a dedicated sample-pointer pool if profiling ever shows it
// matters.
//
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>

using SampleID = uint32_t;
using VoiceHandle = uint64_t;

constexpr SampleID kInvalidSample = 0;
constexpr VoiceHandle kInvalidVoice = 0;

class AudioEngine
{
public:
    using StreamCallback = std::function<int(float *buf, int frames)>;
    static AudioEngine &get();

    // ── Lifecycle ─────────────────────────────────────────────────────────────
    // Call once at startup. channels is almost always 2 (stereo) going forward —
    // there is no mono mixdown path in this engine.
    bool init(uint32_t sampleRate = 48000, uint32_t channels = 2, uint32_t maxVoices = 64);
    void shutdown();
    bool isInitialized() const;

    // ── Sample bank ───────────────────────────────────────────────────────────
    // Decodes fully into memory, resampled/reformatted to match the engine's
    // output format. Safe to call from the UI thread at any time.
    SampleID loadSample(const std::string &path);
    SampleID loadSampleFromMemory(const uint8_t *data, size_t len);

    // Marks a sample for removal. Actual memory is freed once no voice
    // references it any longer (voices hold their own shared_ptr).
    void unloadSample(SampleID id);

    float getSampleDurationSeconds(SampleID id) const;
    bool isSampleValid(SampleID id) const;

    // ── Voices ────────────────────────────────────────────────────────────────
    // Every call to play() is an independent, overlappable voice — calling it
    // twice in a row plays two overlapping instances of the same sample.
    VoiceHandle play(SampleID sample, float gain = 1.0f, float pan = 0.0f, bool loop = false);


    // Sample-accurate variant. targetSampleTime is an absolute value from
    // currentSampleTime()'s clock — the voice starts on that exact sample
    // instead of "next audio callback". 0 (default) preserves the original
    // "as soon as possible" behavior; if targetSampleTime has already
    // passed by the time the command is applied, it's clamped to now.
    VoiceHandle play(SampleID sample, float gain, float pan, bool loop,
                      uint64_t targetSampleTime);

    // ── Streaming voices ──────────────────────────────────────────────────────
    // For sources that can't be pre-decoded (e.g. a video file's audio track,
    // decoded frame-by-frame on another thread). `cb` is pulled from the audio
    // callback and must already be downmixed to mono — exactly what every
    // FluxVideo backend's pullAudio() produces. Internally linearly resampled
    // from sourceSampleRate to the engine's own sampleRate(). Behaves like any
    // other voice afterward — pauseVoice/resumeVoice/stopVoice/setVoiceGain/
    // setVoicePan all work on the returned handle. There's no "finished" state
    // for a stream voice; the caller owns lifetime and calls stopVoice() when done.
    VoiceHandle playStream(StreamCallback cb, uint32_t sourceSampleRate,
                           float gain = 1.0f, float pan = 0.0f);


    // Sample-accurate variant, same semantics as the play() overload above.
    VoiceHandle playStream(StreamCallback cb, uint32_t sourceSampleRate,
                           float gain, float pan, uint64_t targetSampleTime);

    void stopVoice(VoiceHandle voice);
    void setVoiceGain(VoiceHandle voice, float gain);
    void setVoicePan(VoiceHandle voice, float pan); // -1 (L) .. 0 (center) .. +1 (R)
    void seekVoice(VoiceHandle voice, float progress01);
    void pauseVoice(VoiceHandle voice);
    void resumeVoice(VoiceHandle voice);
    bool isVoicePaused(VoiceHandle voice) const;

    bool isVoiceActive(VoiceHandle voice) const;
    float getVoiceProgress(VoiceHandle voice) const; // 0..1

    // ── Master ────────────────────────────────────────────────────────────────
    void setMasterVolume(float v); // 0..1
    float getMasterVolume() const;

    // ── Clock ─────────────────────────────────────────────────────────────────
    // Monotonic sample count since init(); the foundation a future scheduler
    // will use to line up events to the sample instead of "play now".
    uint64_t currentSampleTime() const;
    uint32_t sampleRate() const;
    uint32_t channelCount() const;

    struct Impl;

private:
    AudioEngine();
    ~AudioEngine();
    AudioEngine(const AudioEngine &) = delete;
    AudioEngine &operator=(const AudioEngine &) = delete;

    // Called at the top of loadSample()/loadSampleFromMemory()/play() so a
    // caller who never calls init() explicitly still gets a working engine
    // on first use, with the defaults documented on init() above. If init()
    // was already called (with custom settings or not), this is a no-op.
    void ensureInitialized();

    Impl *m_impl;
};