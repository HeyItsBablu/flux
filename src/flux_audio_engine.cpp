// flux_audio_engine.cpp
//
// miniaudio-backed implementation of AudioEngine. See flux_audio_engine.hpp
// for the design summary.
//
// Build note: define MINIAUDIO_IMPLEMENTATION in exactly one translation
// unit in the whole program. This file is that unit.
//
#define MINIAUDIO_IMPLEMENTATION
#include "../external/miniaudio/miniaudio.h"

#include "flux/flux_audio_engine.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

// ============================================================================
// Decoded sample storage
// ============================================================================

struct DecodedSample {
  std::vector<float>
      interleaved; // engine format: float32, channelCount interleaved
  uint32_t channels = 0;
  uint32_t sampleRate = 0;
  uint64_t frameCount =
      0; // frames, not samples (frame = one sample per channel)


  // Precomputed once, right after decode (see Impl::decode()) — one
  // max-abs peak value per bucket, downmixed across channels, spanning
  // the sample's full length at a fixed resolution. Exists purely for
  // AudioEngine::getSamplePeaks() (UI waveform previews); never read by
  // the audio callback. Computing it once here means a UI redrawing
  // every frame (e.g. while dragging a fade handle over a clip) never
  // re-scans raw sample data — getSamplePeaks() just downsamples this
  // array further if the caller asks for a coarser resolution.
  std::vector<float> peaksHiRes;
};


// Resolution DecodedSample::peaksHiRes is computed at. High enough that
// getSamplePeaks() downsampling to any UI-reasonable bucket count (a
// clip a few hundred px wide) still has plenty of source detail to
// max-reduce from, without scanning the full sample array per call.
static constexpr int kPeakCacheBuckets = 512;

// ============================================================================
// Voice pool
// ============================================================================
//
// A Voice's playback fields (samplePtr, framePos, gain, pan, looping) are
// touched ONLY by the audio thread once a StartVoice command has been
// consumed. UI-thread reads of `active`/`progress` are safe because they're
// std::atomic and are only ever written by the audio thread after that point.
//
struct Voice {
  std::atomic<bool> active{false};
  std::atomic<uint32_t> generation{1}; // bumped every time the slot is reused
  std::atomic<float> progress{
      0.f}; // 0..1, updated by audio thread each callback
  std::atomic<bool> paused{false};

  // UI-thread-only bookkeeping for voice stealing: monotonically
  // increasing "when did this voice start" stamp, so the pool can find
  // the single oldest active voice to steal when it's full. Never
  // touched by the audio thread.
  std::atomic<uint64_t> startSeq{0};

  // Audio-thread-only fields below (never touched by UI thread directly)
  std::shared_ptr<DecodedSample> sample;
  double framePos = 0.0; // fractional — advances by pitchRatio per sample so
                         // non-integer playback rates (pitch shift) work
  float gain = 1.f;
  float pan = 0.f;
  bool loop = false;
  float pitchRatio = 1.f; // 1.0 = normal speed/pitch

  TrackID track = kInvalidTrack; // routes this voice's render into a
                                 // Track buffer instead of straight to
                                 // master. Audio-thread-only, set from
                                 // Command::trackArg in applyCommand().

  // Audio-thread-only. Set by applyCommand() when a StartVoice/
  // StartStreamVoice command's targetFrame lands mid-block; consumed
  // (and reset to 0) the next time mix() processes this voice.
  ma_uint32 pendingOffset = 0;

  // ── Streaming voice state (audio-thread-only) ────────────────────────────
  bool isStream = false;
  AudioEngine::StreamCallback streamCb;
  uint32_t streamSampleRate = 0;
  float prevSample = 0.f; // carries continuity across callback blocks
  std::vector<float> streamScratch;
};


// ============================================================================
// Insert effects
// ============================================================================
//
// Params (type/cutoff/Q) are atomics written directly from the UI thread —
// same "coarse set and forget" contract as Track::gain/pan. `active`
// gates whether process() does anything so a disabled slot is a cheap
// atomic load, not a silent no-op filter still running its math.
//
// Coefficients and the two-channel biquad history (x1/x2/y1/y2 per
// channel — up to stereo) are audio-thread-only, recomputed only when
// the cached params no longer match the live atomics, so a UI slider
// that isn't currently moving costs nothing beyond three atomic loads
// and a compare per block.
struct BiquadFilterEffect {
  std::atomic<bool> active{false};
  std::atomic<uint8_t> filterType{(uint8_t)FilterType::LowPass};
  std::atomic<float> cutoffHz{1000.f};
  std::atomic<float> resonanceQ{0.707f}; // ~0.707 = Butterworth (no peak)

  struct ChannelState {
    float x1 = 0.f, x2 = 0.f, y1 = 0.f, y2 = 0.f;
  };
  std::array<ChannelState, 2> state; // audio-thread-only

  // Cached normalized coefficients + the params they were computed
  // from — recomputed only on change (see process()).
  float b0 = 1.f, b1 = 0.f, b2 = 0.f, a1 = 0.f, a2 = 0.f;
  float cachedCutoff = -1.f;
  float cachedQ = -1.f;
  uint8_t cachedType = 255;

  // Reset audio-thread-only state to silence — called when a slot is
  // freshly (re)claimed so stale history from a previous effect on this
  // slot doesn't leak into new audio.
  void resetState() {
    state[0] = ChannelState{};
    state[1] = ChannelState{};
    cachedCutoff = -1.f; // forces coefficient recompute next process()
  }

  void recomputeCoeffsIfNeeded(uint32_t sampleRate) {
    float cutoff = cutoffHz.load(std::memory_order_relaxed);
    float q = resonanceQ.load(std::memory_order_relaxed);
    uint8_t type = filterType.load(std::memory_order_relaxed);
    if (cutoff == cachedCutoff && q == cachedQ && type == cachedType)
      return;
    cachedCutoff = cutoff;
    cachedQ = q;
    cachedType = type;

    // Standard RBJ (Robert Bristow-Johnson) biquad cookbook formulas.
    float freq = std::max(20.f, std::min(cutoff, (float)sampleRate * 0.49f));
    float w0 = 2.f * 3.14159265f * freq / (float)sampleRate;
    float cosw0 = std::cos(w0), sinw0 = std::sin(w0);
    float qq = std::max(0.1f, q);
    float alpha = sinw0 / (2.f * qq);

    float b0n, b1n, b2n, a0n, a1n, a2n;
    switch ((FilterType)type) {
    case FilterType::HighPass:
      b0n = (1.f + cosw0) / 2.f;
      b1n = -(1.f + cosw0);
      b2n = (1.f + cosw0) / 2.f;
      a0n = 1.f + alpha;
      a1n = -2.f * cosw0;
      a2n = 1.f - alpha;
      break;
    case FilterType::BandPass:
      b0n = alpha;
      b1n = 0.f;
      b2n = -alpha;
      a0n = 1.f + alpha;
      a1n = -2.f * cosw0;
      a2n = 1.f - alpha;
      break;
    case FilterType::Notch:
      b0n = 1.f;
      b1n = -2.f * cosw0;
      b2n = 1.f;
      a0n = 1.f + alpha;
      a1n = -2.f * cosw0;
      a2n = 1.f - alpha;
      break;
    case FilterType::LowPass:
    default:
      b0n = (1.f - cosw0) / 2.f;
      b1n = 1.f - cosw0;
      b2n = (1.f - cosw0) / 2.f;
      a0n = 1.f + alpha;
      a1n = -2.f * cosw0;
      a2n = 1.f - alpha;
      break;
    }
    b0 = b0n / a0n;
    b1 = b1n / a0n;
    b2 = b2n / a0n;
    a1 = a1n / a0n;
    a2 = a2n / a0n;
  }

  // In-place, per-channel Direct Form I biquad. channels is clamped to 2
  // (this engine has no >2-channel path elsewhere either).
  void process(float *buf, ma_uint32 frames, uint32_t channels,
              uint32_t sampleRate) {
    if (!active.load(std::memory_order_relaxed))
      return;
    recomputeCoeffsIfNeeded(sampleRate);
    uint32_t ch = std::min<uint32_t>(channels, 2);
    for (uint32_t c = 0; c < ch; c++) {
      ChannelState &st = state[c];
      for (ma_uint32 i = 0; i < frames; i++) {
        float x0 = buf[i * channels + c];
        float y0 = b0 * x0 + b1 * st.x1 + b2 * st.x2 - a1 * st.y1 - a2 * st.y2;
        st.x2 = st.x1;
        st.x1 = x0;
        st.y2 = st.y1;
        st.y1 = y0;
        buf[i * channels + c] = y0;
      }
    }
  }
};

// One insert slot: currently only Biquad exists, so the tag just gates
// whether the embedded filter runs. Adding a second effect type later
// (compressor/delay/reverb) means adding its own embedded struct here
// and a case in whatever drives process() per slot — not a new
// allocation or a virtual call, keeping this consistent with the rest
// of the file's zero-alloc audio-callback discipline.
struct InsertSlot {
  std::atomic<InsertEffectType> type{InsertEffectType::None};
  BiquadFilterEffect biquad;

  void process(float *buf, ma_uint32 frames, uint32_t channels,
              uint32_t sampleRate) {
    if (type.load(std::memory_order_relaxed) == InsertEffectType::Biquad)
      biquad.process(buf, frames, channels, sampleRate);
  }

  void reset() {
    type.store(InsertEffectType::None, std::memory_order_relaxed);
    biquad.active.store(false, std::memory_order_relaxed);
    biquad.resetState();
  }
};


// ============================================================================
// Routing graph — Track / Bus
// ============================================================================
//
// Unlike Voice, every field here is atomic and written DIRECTLY from the UI
// thread — no command-queue round trip. These are coarse "set and forget"
// mixer parameters, not sample-accurate events like a voice start, so a
// relaxed atomic write/read pair is enough. The one place ordering matters
// is create: every other field is reset BEFORE `active` is published true
// (release), so mix() never observes a half-initialized slot.
//
// Known limitation: setBusSendBus() only guards against a bus sending
// directly to itself. A longer cycle (B -> C -> B) isn't rejected — it
// won't hang (mix() does one single pass per stage, not a graph walk), but
// it will build up gain across blocks into a runaway/feedback sound. Worth
// a real cycle check before this is exposed in end-user UI.
//
struct Track {
  std::atomic<bool> active{false};
  std::atomic<float> gain{1.f};
  std::atomic<float> pan{0.f}; // equal-power pan law, applied per-sample
                               // in mix() step 2 below.
  std::atomic<bool> muted{false};
  std::atomic<bool> soloed{false};
  std::atomic<BusID> sendBus{kMasterBus};
  std::atomic<float> peakLevel{0.f}; // audio-thread-written, UI-thread-read
  std::vector<float> buffer; // audio-thread-only; sized once at init()

 // ── Sample-accurate gain/pan ramps (audio-thread-only) ────────────────
 // Set by applyCommand() on RampTrackGain/RampTrackPan, consumed
 // per-sample in mix() step 2. `gain`/`pan` above always hold the most
 // recently *reached* value (updated at the end of every mix() block)
 // so a new ramp — or a plain instant setTrackGain/Pan from the UI
 // thread — always starts from a correct point, not a stale one.
 bool gainRampActive = false;
 float gainRampStart = 1.f;
 float gainRampTarget = 1.f;
 uint64_t gainRampStartFrame = 0;
 uint64_t gainRampEndFrame = 0;
 bool panRampActive = false;
 float panRampStart = 0.f;
 float panRampTarget = 0.f;
 uint64_t panRampStartFrame = 0;
 uint64_t panRampEndFrame = 0;

  // ── Insert effects ────────────────────────────────────────
  std::array<InsertSlot, kMaxInserts> inserts;
};

struct Bus {
  std::atomic<bool> active{false};
  std::atomic<float> gain{1.f};
  std::atomic<BusID> sendBus{kInvalidBus}; // master's is ignored (terminal)
  std::atomic<float> peakLevel{0.f};
  std::vector<float> buffer; // audio-thread-only; sized once at init()

  // ── Insert effects ────────────────────────────────────────
  std::array<InsertSlot, kMaxInserts> inserts;
};

// ============================================================================
// Command queue (single-producer / single-consumer ring buffer)
// ============================================================================

enum class CmdType : uint8_t {
  StartVoice,
  StopVoice,
  StartStreamVoice,
  SetGain,
  SetPan,
  Seek,
  SetMasterVolume,
  PauseVoice,
  ResumeVoice,
  RampTrackGain,
  RampTrackPan,

};

struct Command {
  CmdType type;
  uint32_t slot = 0;
  uint32_t generation = 0;
  std::shared_ptr<DecodedSample> sample; // only used by StartVoice
  AudioEngine::StreamCallback streamCb;  // only used by StartStreamVoice
  float floatArg = 0.f;
  uint32_t uintArg = 0; // sourceSampleRate for StartStreamVoice
  bool boolArg = false;

  // Absolute engine sample-time (AudioEngine::currentSampleTime() space)
  // this command should take effect at. 0 = apply as soon as possible,
  // matching every pre-existing call site.
  uint64_t targetFrame = 0;
  float pitchArg = 1.f;             // pitchRatio, only used by StartVoice
  TrackID trackArg = kInvalidTrack; // only used by StartVoice/StartStreamVoice

  // Only used by RampTrackGain/RampTrackPan — absolute engine
  // sample-time bounds of the ramp itself. Distinct from targetFrame
  // (which stays 0/"apply ASAP" for these commands): targetFrame
  // governs when applyCommand() *registers* the ramp, these govern
  // when the ramp actually starts/finishes interpolating.
  uint64_t rampStartFrame = 0;
  uint64_t rampEndFrame = 0;
};

class SpscCommandQueue {
public:
  explicit SpscCommandQueue(size_t capacity) : m_buf(capacity) {}

  // UI thread only
  bool push(Command &&cmd) {
    size_t head = m_head.load(std::memory_order_relaxed);
    size_t nextHead = (head + 1) % m_buf.size();
    if (nextHead == m_tail.load(std::memory_order_acquire))
      return false; // full — caller drops the command (rare with sane capacity)
    m_buf[head] = std::move(cmd);
    m_head.store(nextHead, std::memory_order_release);
    return true;
  }

  // Audio thread only
  bool pop(Command &out) {
    size_t tail = m_tail.load(std::memory_order_relaxed);
    if (tail == m_head.load(std::memory_order_acquire))
      return false; // empty
    out = std::move(m_buf[tail]);
    m_tail.store((tail + 1) % m_buf.size(), std::memory_order_release);
    return true;
  }

private:
  std::vector<Command> m_buf;
  std::atomic<size_t> m_head{0};
  std::atomic<size_t> m_tail{0};
};

// ============================================================================
// AudioEngine::Impl
// ============================================================================

struct AudioEngine::Impl {
  ma_device device{};
  bool deviceInitialized = false;

  // ── Capture (recording) ─────────────────────────────────────────────────
  ma_device captureDevice{};
  bool captureDeviceInitialized = false;
  std::atomic<bool> capturing{false};
  AudioEngine::CaptureCallback captureCb; // set before ma_device_start(),
                                          // never mutated while the
                                          // capture device is running —
                                          // start()/stop() bracket the
                                          // window the audio thread can
                                          // read it in, so this needs no
                                          // extra synchronization beyond
                                          // that ordering.
  uint32_t captureChannels = 1;

  uint32_t sampleRate = 48000;
  uint32_t channels = 2;

  std::vector<Voice> voices;
  SpscCommandQueue commands{1024};

  // ── Routing graph ─────────────────────────────────────────────────────
  static constexpr uint32_t kMaxTracks = 64;
  static constexpr uint32_t kMaxBuses = 16;
  // Upper bound on frames-per-callback, used to size Track/Bus scratch
  // buffers once at init() so mix() never allocates. Real device periods
  // are almost always a few hundred to a few thousand frames; mix()
  // clamps rather than reallocate if a backend ever exceeds this.
  static constexpr uint32_t kMaxBlockFrames = 8192;

  std::array<Track, kMaxTracks> tracks;
  std::array<Bus, kMaxBuses> buses;

  // Destination for voices with track == kInvalidTrack — the
  // pre-routing-graph "straight to master" path. Folded into the master
  // bus at the end of mix(), same effective result as before this change.
  std::vector<float> legacyScratch;

  // Hands out increasing start-order stamps for voice stealing. UI-thread
  // only (both play() and playStream() are called from the UI thread).
  std::atomic<uint64_t> nextStartSeq{1};

  // Commands popped off the ring buffer whose targetFrame is beyond the
  // current block. Re-checked every mix() call. Audio-thread-only —
  // never touched from the UI thread.
  std::vector<Command> pendingStarts;

  std::atomic<float> masterVolume{1.0f};
  std::atomic<uint64_t> clockFrames{0};

  // Guards lazy auto-init (ensureInitialized()) so two threads calling
  // loadSample()/play() for the first time simultaneously can't both
  // race into init(). Never touched by the audio callback.
  std::mutex initMutex;

  // Sample bank — protected by a mutex. Only touched from the UI thread
  // (loadSample/unloadSample/getSampleDurationSeconds), never from the
  // audio callback, so this mutex never contends with the audio thread.
  std::mutex bankMutex;
  std::unordered_map<SampleID, std::shared_ptr<DecodedSample>> bank;
  SampleID nextSampleId = 1;

  // ── Handle packing ────────────────────────────────────────────────────────
  static VoiceHandle makeHandle(uint32_t slot, uint32_t generation) {
    return (static_cast<uint64_t>(generation) << 32) | slot;
  }
  static uint32_t handleSlot(VoiceHandle h) {
    return static_cast<uint32_t>(h & 0xFFFFFFFFu);
  }
  static uint32_t handleGen(VoiceHandle h) {
    return static_cast<uint32_t>(h >> 32);
  }

  // Finds a free voice slot; if none is free, picks the oldest active
  // voice (smallest startSeq) to steal instead. Returns {slot, wasFree}.
  // slot == UINT32_MAX only when voices.size() == 0 (engine has no pool
  // at all — e.g. init() was called with maxVoices = 0).
  //
  // Single-producer only (play()/playStream() are both called from the
  // UI thread), so this plain scan-then-pick is race-free without extra
  // locking — nothing else can claim/steal concurrently.
  std::pair<uint32_t, bool> reserveVoiceSlot() {
    for (uint32_t i = 0; i < voices.size(); i++) {
      Voice &v = voices[i];
      bool expected = false;
      if (v.active.compare_exchange_strong(expected, true,
                                           std::memory_order_acq_rel))
        return {i, true};
    }

    // Pool exhausted — steal whichever active voice has been playing
    // longest. This mirrors standard sampler/synth voice-stealing
    // policy: a dense pattern keeps sounding instead of silently
    // dropping newer notes once every voice is busy.
    uint32_t stealSlot = UINT32_MAX;
    uint64_t oldestSeq = UINT64_MAX;
    for (uint32_t i = 0; i < voices.size(); i++) {
      uint64_t seq = voices[i].startSeq.load(std::memory_order_relaxed);
      if (seq < oldestSeq) {
        oldestSeq = seq;
        stealSlot = i;
      }
    }
    return {stealSlot, false};
  }

  // ── Decode helper (UI thread) ─────────────────────────────────────────────
  std::shared_ptr<DecodedSample> decode(ma_decoder_config cfg,
                                        ma_uint64 *outFrameCount,
                                        const void *memData, size_t memLen,
                                        const char *path) {
    ma_decoder decoder;
    ma_result result =
        memData ? ma_decoder_init_memory(memData, memLen, &cfg, &decoder)
                : ma_decoder_init_file(path, &cfg, &decoder);
    if (result != MA_SUCCESS)
      return nullptr;

    ma_uint64 frameCount = 0;
    ma_decoder_get_length_in_pcm_frames(&decoder, &frameCount);

    auto out = std::make_shared<DecodedSample>();
    out->channels = cfg.channels;
    out->sampleRate = cfg.sampleRate;
    out->interleaved.resize(static_cast<size_t>(frameCount) * cfg.channels);

    ma_uint64 framesRead = 0;
    ma_decoder_read_pcm_frames(&decoder, out->interleaved.data(), frameCount,
                               &framesRead);
    out->frameCount = framesRead;
    out->interleaved.resize(static_cast<size_t>(framesRead) * cfg.channels);

    // Precompute waveform peaks once, right after decode — see the
    // comment on DecodedSample::peaksHiRes for why this happens here
    // rather than lazily on first UI request.
    if (out->frameCount > 0 && out->channels > 0) {
      out->peaksHiRes.assign(kPeakCacheBuckets, 0.f);
      for (int b = 0; b < kPeakCacheBuckets; b++) {
        uint64_t startFrame =
            (uint64_t)b * out->frameCount / kPeakCacheBuckets;
        uint64_t endFrame =
            std::min(out->frameCount,
                     (uint64_t)(b + 1) * out->frameCount / kPeakCacheBuckets);
        float peak = 0.f;
        for (uint64_t f = startFrame; f < endFrame; f++) {
          const float *frame = &out->interleaved[f * out->channels];
          for (uint32_t c = 0; c < out->channels; c++)
            peak = std::max(peak, std::fabs(frame[c]));
        }
        out->peaksHiRes[b] = peak;
      }
    }


    ma_decoder_uninit(&decoder);
    if (outFrameCount)
      *outFrameCount = framesRead;
    return (framesRead > 0) ? out : nullptr;
  }

  // ── Audio callback ────────────────────────────────────────────────────────
  static void dataCallback(ma_device *pDevice, void *pOutput, const void *,
                           ma_uint32 frameCount) {
    auto *impl = static_cast<Impl *>(pDevice->pUserData);
    impl->mix(static_cast<float *>(pOutput), frameCount);
  }

  // Capture device's callback — pushes incoming input frames straight to
  // the caller-supplied CaptureCallback. No mixing, no routing graph
  // involvement; this is a completely separate audio thread from the
  // playback device's.
  static void captureDataCallback(ma_device *pDevice, void *,
                                  const void *pInput, ma_uint32 frameCount) {
    auto *impl = static_cast<Impl *>(pDevice->pUserData);
    if (impl->captureCb)
      impl->captureCb(static_cast<const float *>(pInput), frameCount,
                      impl->captureChannels);
  }

  // Drives mix() directly, bypassing ma_device entirely. Used by
  // bounceToWav(). Caller (bounceToWav) is responsible for ensuring the
  // realtime device isn't also calling mix() concurrently.
  void mixOffline(float *output, ma_uint32 frameCount) {
    mix(output, frameCount);
  }

  void mix(float *output, ma_uint32 frameCount) {
    uint64_t blockStart = clockFrames.load(std::memory_order_relaxed);
    uint64_t blockEnd = blockStart + frameCount;

    // Drain queued commands. Anything whose targetFrame falls beyond
    // this block is held in pendingStarts instead of applied now.
    Command cmd;
    while (commands.pop(cmd)) {
      if (cmd.targetFrame >= blockEnd)
        pendingStarts.push_back(std::move(cmd));
      else
        applyCommand(cmd, blockStart);
    }

    // Sweep previously-held commands that are now due.
    for (size_t i = 0; i < pendingStarts.size();) {
      if (pendingStarts[i].targetFrame < blockEnd) {
        applyCommand(pendingStarts[i], blockStart);
        pendingStarts.erase(pendingStarts.begin() + i);
      } else {
        i++;
      }
    }

    // Routing-graph buffers are sized for kMaxBlockFrames; clamp
    // defensively rather than write past them if a backend ever
    // requests a larger callback than that (see the constant's comment
    // in Impl — should never happen in practice).
    ma_uint32 mixFrames = std::min(frameCount, kMaxBlockFrames);
    size_t n = (size_t)mixFrames * channels;

    std::memset(output, 0, sizeof(float) * frameCount * channels);
    for (auto &t : tracks)
      if (t.active.load(std::memory_order_relaxed))
        std::fill_n(t.buffer.data(), n, 0.f);
    for (auto &b : buses)
      if (b.active.load(std::memory_order_relaxed))
        std::fill_n(b.buffer.data(), n, 0.f);
    std::fill_n(legacyScratch.data(), n, 0.f);

    bool anySolo =
        std::any_of(tracks.begin(), tracks.end(), [](const Track &t) {
          return t.active.load(std::memory_order_relaxed) &&
                 t.soloed.load(std::memory_order_relaxed);
        });

    // 1) Voices render into their track's buffer, or legacyScratch if
    //    unrouted (track == kInvalidTrack — every pre-existing call
    //    site, via the new param's default).
    for (auto &v : voices) {
      if (!v.active.load(std::memory_order_relaxed))
        continue;
      if (v.paused.load(std::memory_order_relaxed))
        continue;

      float *dest = legacyScratch.data();
      if (v.track != kInvalidTrack && v.track <= kMaxTracks) {
        Track &vt = tracks[v.track - 1];
        if (vt.active.load(std::memory_order_relaxed))
          dest = vt.buffer.data();
      }

      // Consume this voice's start offset (0 for every voice except
      // one that was just started mid-block by applyCommand()).
      ma_uint32 startOffset = v.pendingOffset;
      v.pendingOffset = 0;

      // Equal-power pan law. Master volume is intentionally NOT
      // applied here anymore — it's applied once, to the summed
      // master bus, in step 4 below (also where a Phase 5 limiter
      // will eventually run).
      float panClamped = std::max(-1.f, std::min(1.f, v.pan));
      float angle = (panClamped + 1.f) * 0.25f * 3.14159265f; // 0..pi/2
      float gainL = std::cos(angle) * v.gain;
      float gainR = std::sin(angle) * v.gain;

      if (v.isStream) {
        if (!v.streamCb)
          continue; // reserved by UI thread, StartStreamVoice not yet applied

        uint32_t srcRate =
            v.streamSampleRate > 0 ? v.streamSampleRate : sampleRate;
        double ratio = (double)srcRate / (double)sampleRate;

        size_t neededSrc = (size_t)((double)frameCount * ratio) + 2;
        if (v.streamScratch.size() < neededSrc)
          v.streamScratch.resize(neededSrc);

        int got = v.streamCb(v.streamScratch.data(), (int)neededSrc);
        if (got < 0) {
          // Callback signaled completion — free the slot instead of
          // idling here forever consuming a voice from the pool.
          v.active.store(false, std::memory_order_release);
          v.streamCb = nullptr;
          continue;
        }
        for (size_t i = (size_t)std::max(got, 0); i < neededSrc; i++)
          v.streamScratch[i] = 0.f;

        // Linear resample from source rate to engine rate, sample-and-hold
        // continuity (prevSample) carried across callback block boundaries.
        double pos = 0.0;

        for (ma_uint32 i = startOffset; i < frameCount; i++) {
          size_t idx0 = (size_t)pos;
          float frac = (float)(pos - (double)idx0);
          float s0 = (idx0 == 0) ? v.prevSample : v.streamScratch[idx0 - 1];
          float s1 = (idx0 < neededSrc) ? v.streamScratch[idx0] : s0;
          float mono = s0 + (s1 - s0) * frac;

          dest[i * channels + 0] += mono * gainL;
          if (channels > 1)
            dest[i * channels + 1] += mono * gainR;

          pos += ratio;
        }

        size_t consumed = (size_t)pos;
        v.prevSample = (consumed > 0 && consumed <= neededSrc)
                           ? v.streamScratch[consumed - 1]
                           : v.streamScratch[neededSrc - 1];
        continue;
      }

      if (!v.sample)
        continue; // reserved by UI thread, StartVoice command not yet applied

      const DecodedSample &s = *v.sample;
      if (s.channels == 0 || s.frameCount == 0)
        continue;

      for (ma_uint32 i = startOffset; i < frameCount; i++) {
        if (v.framePos >= (double)s.frameCount) {
          if (v.loop) {
            v.framePos = std::fmod(v.framePos, (double)s.frameCount);
          } else {
            v.active.store(false, std::memory_order_release);
            v.sample.reset(); // release the shared_ptr on the audio thread —
                              // acceptable here since this happens at most
                              // once per voice lifetime, not per-sample.
            break;
          }
        }

        // Linear interpolation between the two nearest frames so
        // pitchRatio != 1.0 (non-integer stepping) doesn't alias.
        size_t idx0 = static_cast<size_t>(v.framePos);
        size_t idx1 =
            (idx0 + 1 < s.frameCount) ? idx0 + 1 : (v.loop ? 0 : idx0);
        float frac = static_cast<float>(v.framePos - static_cast<double>(idx0));

        const float *f0 = &s.interleaved[idx0 * s.channels];
        const float *f1 = &s.interleaved[idx1 * s.channels];

        float sampL = f0[0] + (f1[0] - f0[0]) * frac;
        float sampR = (s.channels > 1) ? f0[1] + (f1[1] - f0[1]) * frac : sampL;

        dest[i * channels + 0] += sampL * gainL;
        if (channels > 1)
          dest[i * channels + 1] += sampR * gainR;

        v.framePos += v.pitchRatio;
      }

      if (s.frameCount > 0)
        v.progress.store(
            std::min(1.f, static_cast<float>(
                              v.framePos / static_cast<double>(s.frameCount))),
            std::memory_order_relaxed);
    }


    // 1.5) Peak-meter each active track's buffer, post-voice-render,
    // pre-send — this is "what's actually happening on this track"
    // regardless of mute/solo, matching how a real channel strip meter
    // still shows signal on a muted channel.
    for (auto &t : tracks) {
      if (!t.active.load(std::memory_order_relaxed))
        continue;
      float peak = 0.f;
      for (size_t i = 0; i < n; i++)
        peak = std::max(peak, std::fabs(t.buffer[i]));
      float prev = t.peakLevel.load(std::memory_order_relaxed);
      t.peakLevel.store(std::max(peak, prev * 0.85f),
                        std::memory_order_relaxed);
    }

    // 2) Tracks -> their send bus (mute/solo applied here).
    // Sample-accurate per-frame so gain/pan ramps from automation lanes
    // (RampTrackGain/RampTrackPan) interpolate smoothly instead of
    // stepping once per block. Pan is now genuinely applied here too —
    // previously the field was stored but never mixed in.
    for (auto &t : tracks) {
      if (!t.active.load(std::memory_order_relaxed))
        continue;
      bool muted = t.muted.load(std::memory_order_relaxed);
      bool soloed = t.soloed.load(std::memory_order_relaxed);
      if (muted || (anySolo && !soloed))
        continue; 

      BusID sendId = t.sendBus.load(std::memory_order_relaxed);

      if (sendId == kInvalidBus || sendId > kMaxBuses)
        sendId = kMasterBus;
      Bus &dest = buses[sendId - 1];
      if (!dest.active.load(std::memory_order_relaxed))
        continue;

      // insert-effect chain, pre-fader, pre-send — processes
      // t.buffer in place before the gain/pan loop below reads it.
      for (auto &slot : t.inserts)
        slot.process(t.buffer.data(), mixFrames, channels, sampleRate);


      float lastGain = t.gain.load(std::memory_order_relaxed);
      float lastPan = t.pan.load(std::memory_order_relaxed);

      for (ma_uint32 f = 0; f < mixFrames; f++) {
        uint64_t frameTime = blockStart + f;

        float g = lastGain;
        if (t.gainRampActive) {
          if (frameTime >= t.gainRampEndFrame) {
            g = t.gainRampTarget;
          } else if (frameTime <= t.gainRampStartFrame ||
                     t.gainRampEndFrame <= t.gainRampStartFrame) {
            g = t.gainRampStart;
          } else {
            double frac = double(frameTime - t.gainRampStartFrame) /
                         double(t.gainRampEndFrame - t.gainRampStartFrame);
            g = t.gainRampStart +
                (t.gainRampTarget - t.gainRampStart) * (float)frac;
          }
        }

        float p = lastPan;
        if (t.panRampActive) {
          if (frameTime >= t.panRampEndFrame) {
            p = t.panRampTarget;
          } else if (frameTime <= t.panRampStartFrame ||
                     t.panRampEndFrame <= t.panRampStartFrame) {
            p = t.panRampStart;
          } else {
            double frac = double(frameTime - t.panRampStartFrame) /
                         double(t.panRampEndFrame - t.panRampStartFrame);
            p = t.panRampStart + (t.panRampTarget - t.panRampStart) * (float)frac;
          }
        }

        lastGain = g;
        lastPan = p;

        float panClamped = std::max(-1.f, std::min(1.f, p));
        float angle = (panClamped + 1.f) * 0.25f * 3.14159265f; // 0..pi/2
        float gL = std::cos(angle) * g;
        float gR = std::sin(angle) * g;

        dest.buffer[f * channels + 0] += t.buffer[f * channels + 0] * gL;
        if (channels > 1)
          dest.buffer[f * channels + 1] += t.buffer[f * channels + 1] * gR;
      }

      // Ramps that completed within this block are done; latch the
      // reached value so the next block — or the next ramp/instant
      // set's start-point read — sees the settled value, not a stale
      // target.
      if (t.gainRampActive && blockStart + mixFrames >= t.gainRampEndFrame)
        t.gainRampActive = false;
      if (t.panRampActive && blockStart + mixFrames >= t.panRampEndFrame)
        t.panRampActive = false;
      t.gain.store(lastGain, std::memory_order_relaxed);
      t.pan.store(lastPan, std::memory_order_relaxed);

    }

    // 3) Aux buses -> their send bus. Slot 0 is master (terminal, never
    //    sends anywhere) so the scan starts at index 1.
    for (uint32_t i = 1; i < kMaxBuses; i++) {
      Bus &b = buses[i];
      if (!b.active.load(std::memory_order_relaxed))
        continue;
      BusID sendId = b.sendBus.load(std::memory_order_relaxed);
      if (sendId == kInvalidBus || sendId > kMaxBuses)
        continue;

      for (auto &slot : b.inserts)
        slot.process(b.buffer.data(), mixFrames, channels, sampleRate);
      Bus &dest = buses[sendId - 1];
      if (!dest.active.load(std::memory_order_relaxed))
        continue;

      float g = b.gain.load(std::memory_order_relaxed);
      for (size_t j = 0; j < n; j++)
        dest.buffer[j] += b.buffer[j] * g;
    }

    // 3.5) Meter every bus (including master) now that all sends for
    // this block have landed — same peak-hold-and-decay as tracks.
    for (auto &b : buses) {
      if (!b.active.load(std::memory_order_relaxed))
        continue;
      float peak = 0.f;
      for (size_t i = 0; i < n; i++)
        peak = std::max(peak, std::fabs(b.buffer[i]));
      float prev = b.peakLevel.load(std::memory_order_relaxed);
      b.peakLevel.store(std::max(peak, prev * 0.85f),
                        std::memory_order_relaxed);
    }


    // 4) Master: fold in unrouted ("legacy") voices, apply master gain
    //    and volume, write the final interleaved frame to output.
    //    Phase 5: limiter runs on master.buffer before this final scale.
    {
      Bus &master = buses[kMasterBus - 1];
      float masterGain = master.gain.load(std::memory_order_relaxed);
      float mv = masterVolume.load(std::memory_order_relaxed);
      for (size_t i = 0; i < n; i++)
        output[i] = (master.buffer[i] + legacyScratch[i]) * masterGain * mv;
    }

    clockFrames.fetch_add(frameCount, std::memory_order_relaxed);
  }

  void applyCommand(Command &cmd, uint64_t blockStart) {
    if (cmd.type == CmdType::SetMasterVolume) {
      masterVolume.store(cmd.floatArg, std::memory_order_relaxed);
      return;
    }
    if (cmd.type == CmdType::RampTrackGain ||
        cmd.type == CmdType::RampTrackPan) {
      if (cmd.trackArg != kInvalidTrack && cmd.trackArg <= kMaxTracks) {
        Track &vt = tracks[cmd.trackArg - 1];
        if (cmd.type == CmdType::RampTrackGain) {
          vt.gainRampStart = vt.gain.load(std::memory_order_relaxed);
          vt.gainRampTarget = cmd.floatArg;
          vt.gainRampStartFrame = cmd.rampStartFrame;
          vt.gainRampEndFrame = cmd.rampEndFrame;
          vt.gainRampActive = true;
        } else {
          vt.panRampStart = vt.pan.load(std::memory_order_relaxed);
          vt.panRampTarget = cmd.floatArg;
          vt.panRampStartFrame = cmd.rampStartFrame;
          vt.panRampEndFrame = cmd.rampEndFrame;
          vt.panRampActive = true;
        }
      }
      return;
    }


    if (cmd.slot >= voices.size())
      return;
    Voice &v = voices[cmd.slot];
    if (v.generation.load(std::memory_order_relaxed) != cmd.generation)
      return; // stale — voice slot was reused for something else

    // Clamp: if targetFrame is at/before this block's start (already
    // passed, or "ASAP"/0), start immediately with no offset.
    ma_uint32 offset =
        (cmd.targetFrame > blockStart)
            ? static_cast<ma_uint32>(cmd.targetFrame - blockStart)
            : 0;

    switch (cmd.type) {
    case CmdType::StartVoice:
      v.isStream = false;
      v.streamCb = nullptr;
      v.sample = std::move(cmd.sample);
      v.framePos = 0.0;
      v.gain = cmd.floatArg;
      v.loop = cmd.boolArg;
      v.pitchRatio = cmd.pitchArg;
      v.track = cmd.trackArg;
      v.progress.store(0.f, std::memory_order_relaxed);
      v.pendingOffset = offset;
      // v.active was already set true by play() on the UI thread.
      break;
    case CmdType::StartStreamVoice:
      v.sample.reset();
      v.isStream = true;
      v.streamCb = std::move(cmd.streamCb);
      v.streamSampleRate = cmd.uintArg;
      v.prevSample = 0.f;
      v.gain = cmd.floatArg;
      v.loop = false;
      v.track = cmd.trackArg;
      v.progress.store(0.f, std::memory_order_relaxed);
      v.pendingOffset = offset;
      // v.active was already set true by playStream() on the UI thread.
      break;
    case CmdType::StopVoice:
      v.active.store(false, std::memory_order_release);
      v.sample.reset();
      v.isStream = false;
      v.streamCb = nullptr;
      break;
    case CmdType::SetGain:
      v.gain = cmd.floatArg;
      break;
    case CmdType::SetPan:
      v.pan = cmd.floatArg;
      break;
    case CmdType::Seek:
      if (v.sample)
        v.framePos = static_cast<double>(
            std::max(0.f, std::min(1.f, cmd.floatArg)) * v.sample->frameCount);
      break;
    case CmdType::PauseVoice:
      v.paused.store(true, std::memory_order_relaxed);
      break;
    case CmdType::ResumeVoice:
      v.paused.store(false, std::memory_order_relaxed);
      break;
    default:
      break;
    }
  }
};

// ============================================================================
// AudioEngine public API
// ============================================================================

AudioEngine &AudioEngine::get() {
  static AudioEngine inst;
  return inst;
}

AudioEngine::AudioEngine() : m_impl(new Impl()) {}
AudioEngine::~AudioEngine() {
  shutdown();
  delete m_impl;
}

bool AudioEngine::init(uint32_t sampleRate, uint32_t channels,
                       uint32_t maxVoices) {
  if (m_impl->deviceInitialized)
    return true;

  m_impl->sampleRate = sampleRate;
  m_impl->channels = channels;
  m_impl->voices =
      std::vector<Voice>(maxVoices); // fixed pool, no runtime growth

  // Size every routing-graph scratch buffer once, up front — mix() never
  // allocates. Buffers stay this size regardless of actual per-callback
  // frameCount; mix() only ever touches the leading frameCount*channels
  // floats of each.
  {
    size_t scratchFloats = (size_t)Impl::kMaxBlockFrames * channels;
    for (auto &t : m_impl->tracks)
      t.buffer.assign(scratchFloats, 0.f);
    for (auto &b : m_impl->buses)
      b.buffer.assign(scratchFloats, 0.f);
    m_impl->legacyScratch.assign(scratchFloats, 0.f);
  }

  // Master bus (id 1 / slot 0) always exists and is never destroyed.
  {
    Bus &master = m_impl->buses[kMasterBus - 1];
    master.gain.store(1.f, std::memory_order_relaxed);
    master.sendBus.store(kInvalidBus, std::memory_order_relaxed); // terminal
    master.active.store(true, std::memory_order_release);
  }

  ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
  cfg.playback.format = ma_format_f32;
  cfg.playback.channels = channels;
  cfg.sampleRate = sampleRate;
  cfg.dataCallback = Impl::dataCallback;
  cfg.pUserData = m_impl;

  if (ma_device_init(nullptr, &cfg, &m_impl->device) != MA_SUCCESS)
    return false;

  if (ma_device_start(&m_impl->device) != MA_SUCCESS) {
    ma_device_uninit(&m_impl->device);
    return false;
  }

  // Reflect the device's actual negotiated format/rate (may differ from
  // request).
  m_impl->sampleRate = m_impl->device.sampleRate;
  m_impl->channels = m_impl->device.playback.channels;

  m_impl->deviceInitialized = true;
  return true;
}

void AudioEngine::shutdown() {
  if (!m_impl->deviceInitialized)
    return;
  ma_device_uninit(&m_impl->device);
  m_impl->deviceInitialized = false;

  stopCapture();
  std::lock_guard<std::mutex> lk(m_impl->bankMutex);
  m_impl->bank.clear();
  m_impl->voices.clear();
  m_impl->pendingStarts.clear();
}

bool AudioEngine::isInitialized() const { return m_impl->deviceInitialized; }

void AudioEngine::ensureInitialized() {
  std::lock_guard<std::mutex> lk(m_impl->initMutex);
  if (!m_impl->deviceInitialized)
    init(); // falls back to the documented defaults (48kHz, stereo, 64 voices)
}

// ── Sample bank ────────────────────────────────────────────────────────────

SampleID AudioEngine::loadSample(const std::string &path) {
  ensureInitialized();
  if (!m_impl->deviceInitialized)
    return kInvalidSample;

  ma_decoder_config cfg = ma_decoder_config_init(
      ma_format_f32, m_impl->channels, m_impl->sampleRate);
  auto decoded = m_impl->decode(cfg, nullptr, nullptr, 0, path.c_str());
  if (!decoded)
    return kInvalidSample;

  std::lock_guard<std::mutex> lk(m_impl->bankMutex);
  SampleID id = m_impl->nextSampleId++;
  m_impl->bank[id] = decoded;
  return id;
}

SampleID AudioEngine::loadSampleFromMemory(const uint8_t *data, size_t len) {
  ensureInitialized();
  if (!m_impl->deviceInitialized || !data || len == 0)
    return kInvalidSample;

  ma_decoder_config cfg = ma_decoder_config_init(
      ma_format_f32, m_impl->channels, m_impl->sampleRate);
  auto decoded = m_impl->decode(cfg, nullptr, data, len, nullptr);
  if (!decoded)
    return kInvalidSample;

  std::lock_guard<std::mutex> lk(m_impl->bankMutex);
  SampleID id = m_impl->nextSampleId++;
  m_impl->bank[id] = decoded;
  return id;
}

void AudioEngine::unloadSample(SampleID id) {
  std::lock_guard<std::mutex> lk(m_impl->bankMutex);
  // Any voice currently playing this sample holds its own shared_ptr, so
  // erasing here just removes the bank's reference — playback in progress
  // is unaffected and the memory is freed once that voice finishes.
  m_impl->bank.erase(id);
}

float AudioEngine::getSampleDurationSeconds(SampleID id) const {
  std::lock_guard<std::mutex> lk(m_impl->bankMutex);
  auto it = m_impl->bank.find(id);
  if (it == m_impl->bank.end() || it->second->sampleRate == 0)
    return 0.f;
  return static_cast<float>(it->second->frameCount) /
         static_cast<float>(it->second->sampleRate);
}

bool AudioEngine::isSampleValid(SampleID id) const {
  std::lock_guard<std::mutex> lk(m_impl->bankMutex);
  return m_impl->bank.find(id) != m_impl->bank.end();
}

std::vector<float> AudioEngine::getSamplePeaks(SampleID id,
                                               int maxBuckets) const {
  std::lock_guard<std::mutex> lk(m_impl->bankMutex);
  auto it = m_impl->bank.find(id);
  if (it == m_impl->bank.end() || it->second->peaksHiRes.empty())
    return {};

  const std::vector<float> &hiRes = it->second->peaksHiRes;
  maxBuckets = std::max(1, maxBuckets);
  if ((int)hiRes.size() <= maxBuckets)
    return hiRes; // already at or below the requested resolution

  // Downsample by grouping consecutive hi-res buckets and taking their
  // max — max (not average) preserves transient peaks a waveform view
  // exists to show; averaging would just smear them into the noise floor.
  std::vector<float> out(maxBuckets, 0.f);
  for (int i = 0; i < maxBuckets; i++) {
    size_t start = (size_t)i * hiRes.size() / maxBuckets;
    size_t end =
        std::min(hiRes.size(), (size_t)(i + 1) * hiRes.size() / maxBuckets);
    float peak = 0.f;
    for (size_t j = start; j < end; j++)
      peak = std::max(peak, hiRes[j]);
    out[i] = peak;
  }
  return out;
}


// ── Voices ─────────────────────────────────────────────────────────────────

VoiceHandle AudioEngine::play(SampleID sample, float gain, float pan, bool loop,
                              float pitchRatio, TrackID track) {
  return play(sample, gain, pan, loop, pitchRatio, 0, track);
}

VoiceHandle AudioEngine::play(SampleID sample, float gain, float pan, bool loop,
                              float pitchRatio, uint64_t targetSampleTime,
                              TrackID track) {
  ensureInitialized();
  if (!m_impl->deviceInitialized)
    return kInvalidVoice;

  std::shared_ptr<DecodedSample> decoded;
  {
    std::lock_guard<std::mutex> lk(m_impl->bankMutex);
    auto it = m_impl->bank.find(sample);
    if (it == m_impl->bank.end())
      return kInvalidVoice;
    decoded = it->second;
  }

  auto [slot, wasFree] = m_impl->reserveVoiceSlot();
  if (slot == UINT32_MAX)
    return kInvalidVoice; // engine has zero voices configured

  Voice &v = m_impl->voices[slot];
  // Not yet committed to v.generation — only stored after the command is
  // confirmed queued, so a failed push never invalidates a still-valid
  // handle (steal case) or leaves generation out of sync (free case).
  uint32_t gen = v.generation.load(std::memory_order_relaxed) + 1;

  Command cmd;
  cmd.type = CmdType::StartVoice;
  cmd.slot = slot;
  cmd.generation = gen;
  cmd.sample = decoded;
  cmd.floatArg = gain;
  cmd.boolArg = loop;
  cmd.pitchArg = pitchRatio;
  cmd.targetFrame = targetSampleTime;
  cmd.trackArg = track;

  if (!m_impl->commands.push(std::move(cmd))) {
    // Queue full (shouldn't happen with sane capacity).
    if (wasFree)
      v.active.store(
          false, std::memory_order_release); // release the slot we just claimed
    // If we were stealing instead, the victim voice is untouched —
    // still playing under its own valid, unchanged generation.
    return kInvalidVoice;
  }

  // Commit now that the audio thread is guaranteed to see this command.
  // This is also the point where the previous owner's handle (if any)
  // becomes stale — isVoiceActive()/getVoiceProgress() on it will now
  // correctly report "gone" instead of someone else's voice.
  v.generation.store(gen, std::memory_order_relaxed);
  v.startSeq.store(m_impl->nextStartSeq.fetch_add(1, std::memory_order_relaxed),
                   std::memory_order_relaxed);

  setVoicePan(Impl::makeHandle(slot, gen), pan);
  return Impl::makeHandle(slot, gen);
}

VoiceHandle AudioEngine::playStream(StreamCallback cb,
                                    uint32_t sourceSampleRate, float gain,
                                    float pan, TrackID track) {
  return playStream(std::move(cb), sourceSampleRate, gain, pan, 0, track);
}

VoiceHandle AudioEngine::playStream(StreamCallback cb,
                                    uint32_t sourceSampleRate, float gain,
                                    float pan, uint64_t targetSampleTime,
                                    TrackID track) {
  ensureInitialized();
  if (!m_impl->deviceInitialized || !cb)
    return kInvalidVoice;

  auto [slot, wasFree] = m_impl->reserveVoiceSlot();
  if (slot == UINT32_MAX)
    return kInvalidVoice; // engine has zero voices configured

  Voice &v = m_impl->voices[slot];
  uint32_t gen = v.generation.load(std::memory_order_relaxed) + 1;

  Command cmd;
  cmd.type = CmdType::StartStreamVoice;
  cmd.slot = slot;
  cmd.generation = gen;
  cmd.streamCb = std::move(cb);
  cmd.uintArg = sourceSampleRate;
  cmd.floatArg = gain;
  cmd.targetFrame = targetSampleTime;
  cmd.trackArg = track;

  if (!m_impl->commands.push(std::move(cmd))) {
    if (wasFree)
      v.active.store(false, std::memory_order_release);
    return kInvalidVoice;
  }

  v.generation.store(gen, std::memory_order_relaxed);
  v.startSeq.store(m_impl->nextStartSeq.fetch_add(1, std::memory_order_relaxed),
                   std::memory_order_relaxed);

  setVoicePan(Impl::makeHandle(slot, gen), pan);
  return Impl::makeHandle(slot, gen);
}

void AudioEngine::stopVoice(VoiceHandle voice) {
  if (voice == kInvalidVoice)
    return;
  Command cmd;
  cmd.type = CmdType::StopVoice;
  cmd.slot = Impl::handleSlot(voice);
  cmd.generation = Impl::handleGen(voice);
  m_impl->commands.push(std::move(cmd));
}

void AudioEngine::setVoiceGain(VoiceHandle voice, float gain) {
  if (voice == kInvalidVoice)
    return;
  Command cmd;
  cmd.type = CmdType::SetGain;
  cmd.slot = Impl::handleSlot(voice);
  cmd.generation = Impl::handleGen(voice);
  cmd.floatArg = gain;
  m_impl->commands.push(std::move(cmd));
}

void AudioEngine::setVoicePan(VoiceHandle voice, float pan) {
  if (voice == kInvalidVoice)
    return;
  Command cmd;
  cmd.type = CmdType::SetPan;
  cmd.slot = Impl::handleSlot(voice);
  cmd.generation = Impl::handleGen(voice);
  cmd.floatArg = pan;
  m_impl->commands.push(std::move(cmd));
}

void AudioEngine::seekVoice(VoiceHandle voice, float progress01) {
  if (voice == kInvalidVoice)
    return;
  Command cmd;
  cmd.type = CmdType::Seek;
  cmd.slot = Impl::handleSlot(voice);
  cmd.generation = Impl::handleGen(voice);
  cmd.floatArg = progress01;
  m_impl->commands.push(std::move(cmd));
}

bool AudioEngine::isVoiceActive(VoiceHandle voice) const {
  if (voice == kInvalidVoice)
    return false;
  uint32_t slot = Impl::handleSlot(voice);
  if (slot >= m_impl->voices.size())
    return false;
  const Voice &v = m_impl->voices[slot];
  if (v.generation.load(std::memory_order_relaxed) != Impl::handleGen(voice))
    return false; // stale handle — slot reused since
  return v.active.load(std::memory_order_relaxed);
}

float AudioEngine::getVoiceProgress(VoiceHandle voice) const {
  if (voice == kInvalidVoice)
    return 0.f;
  uint32_t slot = Impl::handleSlot(voice);
  if (slot >= m_impl->voices.size())
    return 0.f;
  const Voice &v = m_impl->voices[slot];
  if (v.generation.load(std::memory_order_relaxed) != Impl::handleGen(voice))
    return 0.f;
  return v.progress.load(std::memory_order_relaxed);
}

void AudioEngine::pauseVoice(VoiceHandle voice) {
  if (voice == kInvalidVoice)
    return;
  Command cmd;
  cmd.type = CmdType::PauseVoice;
  cmd.slot = Impl::handleSlot(voice);
  cmd.generation = Impl::handleGen(voice);
  m_impl->commands.push(std::move(cmd));
}

void AudioEngine::resumeVoice(VoiceHandle voice) {
  if (voice == kInvalidVoice)
    return;
  Command cmd;
  cmd.type = CmdType::ResumeVoice;
  cmd.slot = Impl::handleSlot(voice);
  cmd.generation = Impl::handleGen(voice);
  m_impl->commands.push(std::move(cmd));
}

bool AudioEngine::isVoicePaused(VoiceHandle voice) const {
  if (voice == kInvalidVoice)
    return false;
  uint32_t slot = Impl::handleSlot(voice);
  if (slot >= m_impl->voices.size())
    return false;
  const Voice &v = m_impl->voices[slot];
  if (v.generation.load(std::memory_order_relaxed) != Impl::handleGen(voice))
    return false;
  return v.paused.load(std::memory_order_relaxed);
}

// ── Routing: Tracks & Buses ──────────────────────────────────────────────
// createTrack()/createBus() are UI-thread only (same as play()/
// playStream()), so plain scan-then-claim is race-free — no CAS needed,
// same reasoning as Impl::reserveVoiceSlot()'s comment.

TrackID AudioEngine::createTrack() {
  for (uint32_t i = 0; i < Impl::kMaxTracks; i++) {
    Track &t = m_impl->tracks[i];
    if (t.active.load(std::memory_order_relaxed))
      continue;

    // Reset every field BEFORE publishing active=true (release), so
    // mix() never observes a half-initialized slot.
    t.gain.store(1.f, std::memory_order_relaxed);
    t.pan.store(0.f, std::memory_order_relaxed);
    t.muted.store(false, std::memory_order_relaxed);
    t.soloed.store(false, std::memory_order_relaxed);
    t.sendBus.store(kMasterBus, std::memory_order_relaxed);
    t.peakLevel.store(0.f, std::memory_order_relaxed);
    // Ramp state is audio-thread-only and only ever touched while
    // active is true, so resetting plain (non-atomic) fields here,
    // before publish, is safe.
    t.gainRampActive = false;
    for (auto &slot : t.inserts)
      slot.reset();
    t.panRampActive = false;
    t.active.store(true, std::memory_order_release);
    return (TrackID)(i + 1);
  }
  return kInvalidTrack; // pool exhausted
}

void AudioEngine::destroyTrack(TrackID t) {
  if (t == kInvalidTrack || t > Impl::kMaxTracks)
    return;
  m_impl->tracks[t - 1].active.store(false, std::memory_order_release);
  // Buffer contents are left as-is — harmless; mix() skips inactive
  // tracks, and the buffer is fully cleared (fill_n) before any future
  // createTrack() reuses this slot.
}

void AudioEngine::setTrackGain(TrackID t, float gain) {
  if (t == kInvalidTrack || t > Impl::kMaxTracks)
    return;
  m_impl->tracks[t - 1].gain.store(gain, std::memory_order_relaxed);
}

void AudioEngine::setTrackPan(TrackID t, float pan) {
  if (t == kInvalidTrack || t > Impl::kMaxTracks)
    return;
  m_impl->tracks[t - 1].pan.store(pan, std::memory_order_relaxed);
}

void AudioEngine::setTrackMute(TrackID t, bool muted) {
  if (t == kInvalidTrack || t > Impl::kMaxTracks)
    return;
  m_impl->tracks[t - 1].muted.store(muted, std::memory_order_relaxed);
}

void AudioEngine::setTrackSolo(TrackID t, bool soloed) {
  if (t == kInvalidTrack || t > Impl::kMaxTracks)
    return;
  m_impl->tracks[t - 1].soloed.store(soloed, std::memory_order_relaxed);
}

void AudioEngine::setTrackSendBus(TrackID t, BusID bus) {
  if (t == kInvalidTrack || t > Impl::kMaxTracks)
    return;
  if (bus == kInvalidBus || bus > Impl::kMaxBuses)
    bus = kMasterBus;
  m_impl->tracks[t - 1].sendBus.store(bus, std::memory_order_relaxed);
}


void AudioEngine::rampTrackGain(TrackID t, float target, uint64_t startFrame,
                                uint64_t endFrame) {
  if (t == kInvalidTrack || t > Impl::kMaxTracks)
    return;
  Command cmd;
  cmd.type = CmdType::RampTrackGain;
  cmd.trackArg = t;
  cmd.floatArg = target;
  cmd.rampStartFrame = startFrame;
  cmd.rampEndFrame = std::max(startFrame, endFrame);
  m_impl->commands.push(std::move(cmd));
}

void AudioEngine::rampTrackPan(TrackID t, float target, uint64_t startFrame,
                               uint64_t endFrame) {
  if (t == kInvalidTrack || t > Impl::kMaxTracks)
    return;
  Command cmd;
  cmd.type = CmdType::RampTrackPan;
  cmd.trackArg = t;
  cmd.floatArg = target;
  cmd.rampStartFrame = startFrame;
  cmd.rampEndFrame = std::max(startFrame, endFrame);
  m_impl->commands.push(std::move(cmd));
}

float AudioEngine::getTrackGain(TrackID t) const {
  if (t == kInvalidTrack || t > Impl::kMaxTracks)
    return 1.f;
  return m_impl->tracks[t - 1].gain.load(std::memory_order_relaxed);
}

float AudioEngine::getTrackPan(TrackID t) const {
  if (t == kInvalidTrack || t > Impl::kMaxTracks)
    return 0.f;
  return m_impl->tracks[t - 1].pan.load(std::memory_order_relaxed);
}

BusID AudioEngine::getTrackSendBus(TrackID t) const {
  if (t == kInvalidTrack || t > Impl::kMaxTracks)
    return kMasterBus;
  return m_impl->tracks[t - 1].sendBus.load(std::memory_order_relaxed);
}

float AudioEngine::getBusGain(BusID b) const {
  if (b == kInvalidBus || b > Impl::kMaxBuses)
    return 1.f;
  return m_impl->buses[b - 1].gain.load(std::memory_order_relaxed);
}

void AudioEngine::setTrackFilterInsert(TrackID t, uint32_t slot,
                                       bool enabled, FilterType type,
                                       float cutoffHz, float resonanceQ) {
  if (t == kInvalidTrack || t > Impl::kMaxTracks || slot >= kMaxInserts)
    return;
  InsertSlot &s = m_impl->tracks[t - 1].inserts[slot];
  if (s.type.load(std::memory_order_relaxed) != InsertEffectType::Biquad) {
    s.biquad.resetState(); // freshly claiming this slot — clear stale
                           // history from whatever was here before
    s.type.store(InsertEffectType::Biquad, std::memory_order_relaxed);
  }
  s.biquad.filterType.store((uint8_t)type, std::memory_order_relaxed);
  s.biquad.cutoffHz.store(cutoffHz, std::memory_order_relaxed);
  s.biquad.resonanceQ.store(resonanceQ, std::memory_order_relaxed);
  s.biquad.active.store(enabled, std::memory_order_relaxed);
}

void AudioEngine::clearTrackInsert(TrackID t, uint32_t slot) {
  if (t == kInvalidTrack || t > Impl::kMaxTracks || slot >= kMaxInserts)
    return;
  m_impl->tracks[t - 1].inserts[slot].reset();
}

void AudioEngine::setBusFilterInsert(BusID b, uint32_t slot, bool enabled,
                                     FilterType type, float cutoffHz,
                                     float resonanceQ) {
  if (b == kInvalidBus || b > Impl::kMaxBuses || slot >= kMaxInserts)
    return;
  InsertSlot &s = m_impl->buses[b - 1].inserts[slot];
  if (s.type.load(std::memory_order_relaxed) != InsertEffectType::Biquad) {
    s.biquad.resetState();
    s.type.store(InsertEffectType::Biquad, std::memory_order_relaxed);
  }
  s.biquad.filterType.store((uint8_t)type, std::memory_order_relaxed);
  s.biquad.cutoffHz.store(cutoffHz, std::memory_order_relaxed);
  s.biquad.resonanceQ.store(resonanceQ, std::memory_order_relaxed);
  s.biquad.active.store(enabled, std::memory_order_relaxed);
}

void AudioEngine::clearBusInsert(BusID b, uint32_t slot) {
  if (b == kInvalidBus || b > Impl::kMaxBuses || slot >= kMaxInserts)
    return;
  m_impl->buses[b - 1].inserts[slot].reset();
}

bool AudioEngine::getTrackFilterInsert(TrackID t, uint32_t slot,
                                       bool &enabled, FilterType &type,
                                       float &cutoffHz,
                                       float &resonanceQ) const {
  enabled = false;
  type = FilterType::LowPass;
  cutoffHz = 1000.f;
  resonanceQ = 0.707f;
  if (t == kInvalidTrack || t > Impl::kMaxTracks || slot >= kMaxInserts)
    return false;
  const InsertSlot &s = m_impl->tracks[t - 1].inserts[slot];
  if (s.type.load(std::memory_order_relaxed) != InsertEffectType::Biquad)
    return false;
  enabled = s.biquad.active.load(std::memory_order_relaxed);
  type = (FilterType)s.biquad.filterType.load(std::memory_order_relaxed);
  cutoffHz = s.biquad.cutoffHz.load(std::memory_order_relaxed);
  resonanceQ = s.biquad.resonanceQ.load(std::memory_order_relaxed);
  return true;
}

bool AudioEngine::getBusFilterInsert(BusID b, uint32_t slot, bool &enabled,
                                     FilterType &type, float &cutoffHz,
                                     float &resonanceQ) const {
  enabled = false;
  type = FilterType::LowPass;
  cutoffHz = 1000.f;
  resonanceQ = 0.707f;
  if (b == kInvalidBus || b > Impl::kMaxBuses || slot >= kMaxInserts)
    return false;
  const InsertSlot &s = m_impl->buses[b - 1].inserts[slot];
  if (s.type.load(std::memory_order_relaxed) != InsertEffectType::Biquad)
    return false;
  enabled = s.biquad.active.load(std::memory_order_relaxed);
  type = (FilterType)s.biquad.filterType.load(std::memory_order_relaxed);
  cutoffHz = s.biquad.cutoffHz.load(std::memory_order_relaxed);
  resonanceQ = s.biquad.resonanceQ.load(std::memory_order_relaxed);
  return true;
}



BusID AudioEngine::createBus() {
  // Slot 0 (id 1) is permanently reserved for kMasterBus — scan starts at 1.
  for (uint32_t i = 1; i < Impl::kMaxBuses; i++) {
    Bus &b = m_impl->buses[i];
    if (b.active.load(std::memory_order_relaxed))
      continue;

    b.gain.store(1.f, std::memory_order_relaxed);
    b.sendBus.store(kMasterBus, std::memory_order_relaxed);
    for (auto &slot : b.inserts)
      slot.reset();
    b.active.store(true, std::memory_order_release);
    return (BusID)(i + 1);
  }
  return kInvalidBus; // pool exhausted
}

void AudioEngine::destroyBus(BusID b) {
  if (b == kInvalidBus || b == kMasterBus || b > Impl::kMaxBuses)
    return; // master is permanent
  m_impl->buses[b - 1].active.store(false, std::memory_order_release);
}

void AudioEngine::setBusGain(BusID b, float gain) {
  if (b == kInvalidBus || b > Impl::kMaxBuses)
    return;
  m_impl->buses[b - 1].gain.store(gain, std::memory_order_relaxed);
}

void AudioEngine::setBusSendBus(BusID b, BusID dest) {
  if (b == kInvalidBus || b == kMasterBus || b > Impl::kMaxBuses)
    return; // master's send is terminal, not settable
  if (dest == kInvalidBus || dest > Impl::kMaxBuses || dest == b)
    dest = kMasterBus; // self-send guard only — see the cycle-limitation
                       // note on the Track/Bus struct definitions
  m_impl->buses[b - 1].sendBus.store(dest, std::memory_order_relaxed);
}


// ── Metering ─────────────────────────────────────────────────────────────

float AudioEngine::getTrackPeakLevel(TrackID t) const {
  if (t == kInvalidTrack || t > Impl::kMaxTracks)
    return 0.f;
  return m_impl->tracks[t - 1].peakLevel.load(std::memory_order_relaxed);
}

float AudioEngine::getBusPeakLevel(BusID b) const {
  if (b == kInvalidBus || b > Impl::kMaxBuses)
    return 0.f;
  return m_impl->buses[b - 1].peakLevel.load(std::memory_order_relaxed);
}

// ── Offline render ────────────────────────────────────────────────────────

void AudioEngine::stopRealtime() {
  if (!m_impl->deviceInitialized)
    return;
  ma_device_stop(&m_impl->device);
}

void AudioEngine::resumeRealtime() {
  if (!m_impl->deviceInitialized)
    return;
  ma_device_start(&m_impl->device);
}

bool AudioEngine::bounceToWav(const std::string &outPath, uint64_t numFrames) {
  if (!m_impl->deviceInitialized || numFrames == 0)
    return false;

  // Refuse to run if the device is still actively pulling audio — mix()
  // is not reentrant against itself, and ma_device_is_started() is the
  // one cheap, race-safe check miniaudio gives us for "is the callback
  // thread live right now". Caller must stopRealtime() first.
  if (ma_device_is_started(&m_impl->device))
    return false;

  ma_encoder_config encCfg =
      ma_encoder_config_init(ma_encoding_format_wav, ma_format_f32,
                             m_impl->channels, m_impl->sampleRate);

  ma_encoder encoder;
  if (ma_encoder_init_file(outPath.c_str(), &encCfg, &encoder) != MA_SUCCESS)
    return false;

  // Block size just needs to be <= Impl::kMaxBlockFrames (mix()'s
  // routing-graph scratch buffers are sized for that). 1024 keeps the
  // scratch buffer below allocated small and the loop count reasonable
  // for typical bounce lengths.
  constexpr ma_uint32 kBlock = 1024;
  std::vector<float> scratch((size_t)kBlock * m_impl->channels);

  uint64_t remaining = numFrames;
  bool ok = true;
  while (remaining > 0 && ok) {
    ma_uint32 thisBlock = (ma_uint32)std::min<uint64_t>(remaining, kBlock);
    m_impl->mixOffline(scratch.data(), thisBlock);

    ma_uint64 framesWritten = 0;
    if (ma_encoder_write_pcm_frames(&encoder, scratch.data(), thisBlock,
                                    &framesWritten) != MA_SUCCESS ||
        framesWritten != thisBlock)
      ok = false;

    remaining -= thisBlock;
  }

  ma_encoder_uninit(&encoder);
  return ok;
}

// ── Capture (recording) ───────────────────────────────────────────────────

bool AudioEngine::startCapture(CaptureCallback cb, uint32_t channels,
                               uint32_t sampleRate) {
  if (!cb || m_impl->capturing.load(std::memory_order_relaxed))
    return false; // no callback, or a capture stream is already running

  m_impl->captureChannels = channels;
  m_impl->captureCb = std::move(cb); // set BEFORE device start — see the
                                     // field comment in Impl for why
                                     // this ordering is what makes the
                                     // lack of extra locking safe.

  ma_device_config cfg = ma_device_config_init(ma_device_type_capture);
  cfg.capture.format = ma_format_f32;
  cfg.capture.channels = channels;
  cfg.sampleRate = sampleRate;
  cfg.dataCallback = Impl::captureDataCallback;
  cfg.pUserData = m_impl;

  if (ma_device_init(nullptr, &cfg, &m_impl->captureDevice) != MA_SUCCESS) {
    m_impl->captureCb = nullptr;
    return false;
  }

  if (ma_device_start(&m_impl->captureDevice) != MA_SUCCESS) {
    ma_device_uninit(&m_impl->captureDevice);
    m_impl->captureCb = nullptr;
    return false;
  }

  m_impl->captureDeviceInitialized = true;
  m_impl->capturing.store(true, std::memory_order_relaxed);
  return true;
}

void AudioEngine::stopCapture() {
  if (!m_impl->captureDeviceInitialized)
    return;
  // ma_device_uninit() stops the device (joining its audio thread)
  // before tearing it down, so by the time this returns the capture
  // thread is guaranteed to have exited — safe to clear captureCb right
  // after with no race against a still-running callback.
  ma_device_uninit(&m_impl->captureDevice);
  m_impl->captureDeviceInitialized = false;
  m_impl->capturing.store(false, std::memory_order_relaxed);
  m_impl->captureCb = nullptr;
}

bool AudioEngine::isCapturing() const {
  return m_impl->capturing.load(std::memory_order_relaxed);
}

// ── Master ─────────────────────────────────────────────────────────────────

void AudioEngine::setMasterVolume(float v) {
  Command cmd;
  cmd.type = CmdType::SetMasterVolume;
  cmd.floatArg = std::max(0.f, std::min(1.f, v));
  m_impl->commands.push(std::move(cmd));
}

float AudioEngine::getMasterVolume() const {
  return m_impl->masterVolume.load(std::memory_order_relaxed);
}

// ── Clock ──────────────────────────────────────────────────────────────────

uint64_t AudioEngine::currentSampleTime() const {
  return m_impl->clockFrames.load(std::memory_order_relaxed);
}
uint32_t AudioEngine::sampleRate() const { return m_impl->sampleRate; }
uint32_t AudioEngine::channelCount() const { return m_impl->channels; }