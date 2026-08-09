#include "flux/flux.hpp"
#include "flux/flux_audio_engine.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <string>

#include <vector>

#include <cctype>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <memory>
#include <optional>
#include <sstream>
#include <unordered_map>

// ============================================================================
// Data model
// ============================================================================

struct StepData {
  bool on = false;
  float velocity = 1.0f;      // 0..1, scales this hit's gain
  float pitchSemitones = 0.f; // -24..+24, shifts freqHz for synth tracks only
                              // (see the note on StepHit::sampleId below)
  float probability = 1.0f;   // 0..1, chance this step fires when its turn
                              // comes up; 1.0 = always fires (default,
                              // matches old behavior exactly)

  float microTiming = 0.f; // -0.5..+0.5, fraction of ONE STEP's duration
                           // to nudge this hit early(-)/late(+). 0 =
                           // exactly on the grid (default, matches old
                           // behavior exactly). Combined additively with
                           // the pattern's `swing` below at fire time —
                           // see StepScheduler::tick()/
                           // TimelineScheduler::_tickPatternClip().
};

// Absolute cap every pattern's step vectors are allocated to, regardless of
// that pattern's current logical length (Pattern::numSteps). This means
// changing a pattern's length is just an int assignment — no resize, no
// out-of-bounds risk, and shrinking-then-growing a pattern doesn't lose
// steps programmed beyond the shorter length; they're just inert while
// hidden. See StepScheduler::kMaxSteps for the same constant, scoped there
// for code that already includes this header transitively.
static constexpr int kSeqMaxSteps = 64;

// One track's synth timbre — oscillator shape plus a one-shot ADSR
// envelope. Every step fired on this track uses the same instrument
// settings; per-step variation is still just velocity/pitch/probability/
// microTiming on StepData, same as before this change.
enum class OscWaveform { Sine, Saw, Square, Triangle };

struct StepHit {
  float freqHz;
  float gain;
  float pan;

  // synth test tone. Per-step pitch (StepData::pitchSemitones) applies to
  // sample playback too, via AudioEngine::play()'s pitchRatio argument.
  SampleID sampleId = kInvalidSample;

  // ── Synth voice (used only when sampleId is invalid — see fireStep) ──
  OscWaveform waveform = OscWaveform::Sine;

  // One-shot ADSR, all in seconds except sustainLevel (0..1, the level
  // held during the sustain phase). Defaults reproduce the ORIGINAL
  // fixed envelope exactly: 0s attack (jump straight to full volume),
  // 0.15s linear decay down to sustainLevel=0, 0s sustain hold, 0s
  // release (nothing left to release once decay already reached 0) —
  // i.e. every existing project's synth tracks sound identical after
  // this change unless the person explicitly opens the new Synth panel.
  float attackSec = 0.f;
  float decaySec = 0.15f;
  float sustainLevel = 0.f;
  float sustainSec = 0.f; // how long to HOLD at sustainLevel before
                          // releasing — there's no separate "note off"
                          // event in a one-shot step fire, so sustain
                          // duration has to be an explicit parameter
                          // rather than "however long the key is held".
  float releaseSec = 0.f;
};

// One entry in the song arrangement. `id` is a stable identity used for
// widget/list-key purposes in the UI (see SequencerApp::_arrangementState) —
// it has no meaning to playback, which only cares about `patternSlot`.
struct ArrangementEntry {
  uint64_t id;
  int patternSlot;
  int repeatCount = 1; // number of times this entry loops before the
                       // arrangement advances to the next entry

  bool operator==(const ArrangementEntry &other) const {
    return id == other.id && patternSlot == other.patternSlot &&
           repeatCount == other.repeatCount;
  }
};

struct Pattern {
  std::string name;
  bool active = false;
  int numSteps = 16;    // logical length in steps — <= kSeqMaxSteps.
                        // Acts as this pattern's "time signature" length.
  int stepsPerBeat = 4; // subdivision: 4 = 16th notes, 3 = triplet feel,
                        // etc. Together with numSteps this is the
                        // per-pattern stand-in for a full time signature.

  float swing = 0.f; // 0..75, percent. Classic groovebox swing: every
                     // odd-indexed step (the "off" 16th/8th/etc.) is
                     // delayed by (swing/100 * 0.5) of a step's
                     // duration. 0 = straight timing (default, matches
                     // old behavior exactly). 75 is roughly the
                     // triplet-feel ceiling most grooveboxes cap at —
                     // past that the off-beat starts colliding with
                     // the following on-beat step.
  std::vector<std::vector<StepData>> steps; // [track][step]
};

// ============================================================================
// StepScheduler — engine-agnostic sequencing logic.
// Ticks periodically, looks a fixed window into the future, and schedules
// any due steps as sample-accurate playStream()/play() starts.
// ============================================================================

class StepScheduler {
public:
  // kSteps is gone — pattern length is now per-pattern (Pattern::numSteps).
  // kMaxSteps is the fixed allocation size every pattern's step vectors use
  // (see the comment on kSeqMaxSteps above); kDefaultSteps is what a newly
  // created pattern starts at.
  static constexpr int kMaxSteps = kSeqMaxSteps;
  static constexpr int kDefaultSteps = 16;
  static constexpr int kMaxPatterns =
      8; // fixed pool — same tradeoff as the
         // engine's 64-voice pool: simple,
         // real-time-safe, bounded memory.
         // Bump this if 8 patterns isn't enough.

  double bpm = 120.0;
  bool playing = false;
  bool songMode = false; // false = loop editingSlot forever; true = play
                         // through `arrangement`

  std::array<Pattern, kMaxPatterns> patternSlots;
  std::vector<int> activeSlots; // ordered list of in-use slot indices —
                                // this is the "pattern list" the UI enumerates
  std::vector<ArrangementEntry> arrangement; // song sequence, in play order

  // Per-track mixer state. Indexed in parallel with trackVoice — not baked
  // into StepHit because mute/solo are playback-time overrides, not
  // instrument properties (StepHit::gain stays the "base" volume the
  // track fader controls).
  std::vector<bool> trackMuted;
  std::vector<bool> trackSoloed;

  int arrangementPos = 0; // index into `arrangement`, valid only in song mode
  int editingSlot = -1;   // which pattern the grid currently shows/edits
  int playingSlot = -1;   // which pattern is currently sounding
  int currentStep = 0;

  std::vector<StepHit>
      trackVoice; // one timbre per track, shared across all patterns

  StepScheduler(int numTracks) : _numTracks(numTracks) {
    trackVoice.resize(numTracks);
    trackMuted.assign(numTracks, false);
    trackSoloed.assign(numTracks, false);
    editingSlot = addPattern("Pattern 1");
    playingSlot = editingSlot;
  }

  // Returns the new slot index, or -1 if the fixed pool (kMaxPatterns) is full.
  int addPattern(const std::string &name) {
    for (int i = 0; i < kMaxPatterns; i++) {
      if (!patternSlots[i].active) {
        patternSlots[i].active = true;
        patternSlots[i].name = name;
        patternSlots[i].numSteps = kDefaultSteps;
        patternSlots[i].stepsPerBeat = 4;
        // Always allocate at kMaxSteps — see the comment on kSeqMaxSteps.
        patternSlots[i].steps.assign(_numTracks,
                                     std::vector<StepData>(kMaxSteps));
        activeSlots.push_back(i);
        return i;
      }
    }
    return -1;
  }

  int duplicatePattern(int srcSlot) {
    if (srcSlot < 0 || srcSlot >= kMaxPatterns || !patternSlots[srcSlot].active)
      return -1;
    int dst = addPattern(patternSlots[srcSlot].name + " copy");
    if (dst >= 0) {
      patternSlots[dst].steps =
          patternSlots[srcSlot].steps; // deep copy of step data
      patternSlots[dst].numSteps = patternSlots[srcSlot].numSteps;
      patternSlots[dst].stepsPerBeat = patternSlots[srcSlot].stepsPerBeat;
    }

    return dst;
  }

  // Changes the logical length of `slot`. No data is moved or dropped —
  // steps beyond newLen just stop being played/edited until the pattern is
  // lengthened again. Clamped to [1, kMaxSteps].
  void setPatternLength(int slot, int newLen) {
    if (slot < 0 || slot >= kMaxPatterns || !patternSlots[slot].active)
      return;
    newLen = std::max(1, std::min(kMaxSteps, newLen));
    patternSlots[slot].numSteps = newLen;
    if (playingSlot == slot && currentStep >= newLen)
      currentStep = 0; // playhead was past the new (shorter) end
  }

  void setPatternStepsPerBeat(int slot, int spb) {
    if (slot < 0 || slot >= kMaxPatterns || !patternSlots[slot].active)
      return;
    patternSlots[slot].stepsPerBeat = std::max(1, spb);
  }

  void setPatternSwing(int slot, float swingPercent) {
    if (slot < 0 || slot >= kMaxPatterns || !patternSlots[slot].active)
      return;
    patternSlots[slot].swing = std::max(0.f, std::min(75.f, swingPercent));
  }

  void deletePattern(int slot) {
    if (activeSlots.size() <= 1)
      return; // always keep at least one pattern around
    if (slot < 0 || slot >= kMaxPatterns || !patternSlots[slot].active)
      return;

    patternSlots[slot].active = false;
    activeSlots.erase(std::remove(activeSlots.begin(), activeSlots.end(), slot),
                      activeSlots.end());

    // Drop any arrangement entries that referenced the deleted pattern.
    arrangement.erase(std::remove_if(arrangement.begin(), arrangement.end(),
                                     [slot](const ArrangementEntry &e) {
                                       return e.patternSlot == slot;
                                     }),
                      arrangement.end());
    if (arrangementPos >= (int)arrangement.size())
      arrangementPos = 0;

    if (editingSlot == slot)
      editingSlot = activeSlots.front();
    if (playingSlot == slot)
      playingSlot = editingSlot;
  }

  void setSongMode(bool v) {
    songMode = v;
    if (!songMode)
      playingSlot = editingSlot;
    else if (!arrangement.empty()) {
      arrangementPos = 0;
      playingSlot = arrangement[0].patternSlot;
      _repeatsRemaining = std::max(1, arrangement[0].repeatCount);
    }
  }

  // Switches which pattern the grid edits. In loop mode this also switches
  // playback live, so auditioning a pattern while it plays feels immediate,
  // like most trackers. In song mode, playback keeps following the
  // arrangement regardless of what you're looking at.
  void setEditingPattern(int slot) {
    editingSlot = slot;
    if (!songMode)
      playingSlot = slot;
  }

  void start() {
    if (playing)
      return;
    playing = true;
    currentStep = 0;
    arrangementPos = 0;
    if (songMode && !arrangement.empty()) {
      playingSlot = arrangement[0].patternSlot;
      _repeatsRemaining = std::max(1, arrangement[0].repeatCount);
    } else {
      playingSlot = editingSlot;
    }
    _nextStepFrame = AudioEngine::get().currentSampleTime();
  }

  void stop() { playing = false; }

  // Call this often (e.g. every 25ms) from a UI-thread timer.
  void tick() {
    if (!playing)
      return;

    auto &engine = AudioEngine::get();
    uint64_t now = engine.currentSampleTime();
    uint64_t lookahead = (uint64_t)(engine.sampleRate() * 0.1); // 100ms window

    while (_nextStepFrame < now + lookahead) {
      Pattern &pat = patternSlots[playingSlot];
      int patternLen = std::max(1, pat.numSteps);
      bool anySoloed = std::any_of(trackSoloed.begin(), trackSoloed.end(),
                                   [](bool b) { return b; });
      uint64_t stepFrames = _framesPerStep(pat.stepsPerBeat);
      // Classic groovebox swing: only odd-indexed steps (the "off" hit of
      // each pair) get delayed. Even/on-beat steps are never swung, which
      // is what keeps swing from drifting the downbeat itself.
      int64_t swingOffsetFrames =
          (currentStep % 2 == 1)
              ? (int64_t)((pat.swing / 100.0) * 0.5 * (double)stepFrames)
              : 0;
      for (size_t t = 0; t < pat.steps.size(); t++) {
        const StepData &step = pat.steps[t][currentStep];
        if (!step.on)
          continue;
        bool audible = !trackMuted[t] && (!anySoloed || trackSoloed[t]);
        if (!audible)
          continue;
        if (step.probability < 1.0f &&
            (float)std::rand() / (float)RAND_MAX > step.probability)
          continue; // dice roll missed — step is silently skipped this pass

        // Per-step micro-timing nudge, plus this step's share of the
        // pattern's swing (0 for even steps). Clamped so a large negative
        // nudge can never push the target frame before frame 0 — matters
        // mainly right at transport start.
        int64_t microFrames =
            (int64_t)((double)step.microTiming * (double)stepFrames);
        int64_t totalOffset = microFrames + swingOffsetFrames;
        uint64_t targetFrame =
            (totalOffset < 0 && (uint64_t)(-totalOffset) > _nextStepFrame)
                ? 0
                : (uint64_t)((int64_t)_nextStepFrame + totalOffset);

        fireStep(trackVoice[t], step, targetFrame);
      }

      currentStep++;
      if (currentStep >= patternLen) {
        currentStep = 0;
        _advancePlayback(); // pattern boundary — advance the song if in song
                            // mode
      }
      _nextStepFrame += stepFrames;
    }
  }

private:
  int _numTracks;
  uint64_t _nextStepFrame = 0;

  int _repeatsRemaining = 1; // how many more times playingSlot repeats
                             // before _advancePlayback moves to the next
                             // arrangement entry

  void _advancePlayback() {
    if (!songMode || arrangement.empty())
      return; // loop mode: keep looping playingSlot forever
    if (_repeatsRemaining > 1) {
      _repeatsRemaining--;
      return; // same entry repeats again before advancing
    }
    arrangementPos = (arrangementPos + 1) % (int)arrangement.size();
    playingSlot = arrangement[arrangementPos].patternSlot;
    _repeatsRemaining = std::max(1, arrangement[arrangementPos].repeatCount);
  }

  uint64_t _framesPerStep(int stepsPerBeat) const {
    double secondsPerStep = 60.0 / bpm / std::max(1, stepsPerBeat);
    uint64_t frames =
        (uint64_t)(secondsPerStep * AudioEngine::get().sampleRate());
    // Guard against tick()'s while-loop spinning forever if this ever
    // truncates to 0 (extreme bpm and/or very low sample rate).
    return std::max<uint64_t>(1, frames);
  }

public:
  // Exposed so TimelineScheduler (Phase 3) fires pattern-clip steps through
  // the exact same envelope/pitch/velocity/sample logic the classic
  // pattern-chain scheduler uses — one firing code path, two schedulers.
  static void fireStep(const StepHit &hit, const StepData &step,
                       uint64_t targetFrame, TrackID track = kInvalidTrack) {

    auto &engine = AudioEngine::get();
    float velocity = std::max(0.f, std::min(1.f, step.velocity));
    float effectiveGain = hit.gain * velocity;

    if (engine.isSampleValid(hit.sampleId)) {
      // Sample-backed track: sample-accurate one-shot, velocity applied
      // as gain, pitch via pitchRatio.
      float pitchRatio = std::pow(2.0f, step.pitchSemitones / 12.0f);
      engine.play(hit.sampleId, effectiveGain, hit.pan, /*loop=*/false,
                  pitchRatio, targetFrame, track);
      return;
    }

    _fireSynthStep(hit, step, effectiveGain, targetFrame, track);
  }

private:
  // Bundles the two pieces of per-note state the envelope callback needs.
  // A single make_shared<> here replaces what would otherwise be a pair of
  // separate shared_ptr<float>/shared_ptr<int> allocations per note fired.
  struct SynthNoteState {
    float phase = 0.f;
    int samplesElapsed = 0;
  };

  // Single-cycle waveform lookup at a given phase (radians, unwrapped —
  // may exceed 2*pi, this function wraps it). Sine matches the original
  // exactly; saw/square/triangle are unit-amplitude, DC-free, computed
  // from the same phase accumulator so switching waveform on a track
  // doesn't change pitch tracking or phase-continuity behavior at all.
  static float _oscSample(OscWaveform wf, float phase) {
    constexpr float kTwoPi = 2.0f * 3.14159265f;
    if (wf == OscWaveform::Sine)
      return std::sin(phase);

    float p = std::fmod(phase, kTwoPi);
    if (p < 0.f)
      p += kTwoPi;
    float norm = p / kTwoPi; // 0..1 across one cycle

    switch (wf) {
    case OscWaveform::Saw:
      return 2.0f * norm - 1.0f;
    case OscWaveform::Square:
      return (norm < 0.5f) ? 1.0f : -1.0f;
    case OscWaveform::Triangle:
      return (norm < 0.5f) ? (4.0f * norm - 1.0f) : (3.0f - 4.0f * norm);
    default:
      return std::sin(phase);
    }
  }

  // Piecewise-linear one-shot ADSR, evaluated from a sample count elapsed
  // since note start. `total*Samples` are precomputed once per note (see
  // _fireSynthStep) rather than recomputed per sample.
  static float _adsrEnvelope(int samplesElapsed, int attackSamples,
                             int decaySamples, int sustainSamples,
                             int releaseSamples, float sustainLevel) {
    int n = samplesElapsed;
    if (n < attackSamples)
      return attackSamples > 0 ? (float)n / (float)attackSamples : 1.0f;
    n -= attackSamples;

    if (n < decaySamples) {
      float t = decaySamples > 0 ? (float)n / (float)decaySamples : 1.0f;
      return 1.0f + (sustainLevel - 1.0f) * t; // 1 -> sustainLevel
    }
    n -= decaySamples;

    if (n < sustainSamples)
      return sustainLevel;
    n -= sustainSamples;

    if (n < releaseSamples) {
      float t = releaseSamples > 0 ? (float)n / (float)releaseSamples : 1.0f;
      return sustainLevel * (1.0f - t); // sustainLevel -> 0
    }
    return 0.0f; // fully released
  }

  static void _fireSynthStep(const StepHit &hit, const StepData &step,
                             float effectiveGain, uint64_t targetFrame,
                             TrackID track = kInvalidTrack) {
    auto &engine = AudioEngine::get();
    auto state = std::make_shared<SynthNoteState>();

    // Pitch shift: each semitone is a factor of 2^(1/12).
    float pitchedFreq =
        hit.freqHz * std::pow(2.0f, step.pitchSemitones / 12.0f);
    float phaseInc =
        (2.0f * 3.14159265f * pitchedFreq) / (float)engine.sampleRate();

    float sr = (float)engine.sampleRate();
    int attackSamples = std::max(0, (int)(hit.attackSec * sr));
    int decaySamples = std::max(0, (int)(hit.decaySec * sr));
    int sustainSamples = std::max(0, (int)(hit.sustainSec * sr));
    int releaseSamples = std::max(0, (int)(hit.releaseSec * sr));
    int totalSamples =
        attackSamples + decaySamples + sustainSamples + releaseSamples;
    // Degenerate case: every phase is 0 seconds (e.g. a project loaded
    // with all-zero ADSR fields). Give it one sample of silence instead
    // of a note that never ends — matches the old code's implicit
    // guarantee that a fired step always eventually frees its voice.
    if (totalSamples <= 0)
      totalSamples = 1;

    float sustainLevel = std::max(0.f, std::min(1.f, hit.sustainLevel));
    OscWaveform waveform = hit.waveform;

    AudioEngine::StreamCallback cb =
        [state, phaseInc, attackSamples, decaySamples, sustainSamples,
         releaseSamples, totalSamples, sustainLevel,
         waveform](float *buf, int frames) -> int {
      if (state->samplesElapsed >= totalSamples)
        return -1; // fully decayed — engine frees this voice slot

      for (int i = 0; i < frames; i++) {
        float env =
            _adsrEnvelope(state->samplesElapsed, attackSamples, decaySamples,
                          sustainSamples, releaseSamples, sustainLevel);
        buf[i] = _oscSample(waveform, state->phase) * env * 0.3f;
        state->phase += phaseInc;
        state->samplesElapsed++;
      }
      return frames;
    };

    // Fire-and-forget: the envelope signals its own completion (negative
    // return), so the engine frees the voice slot once the decay finishes.
    engine.playStream(cb, engine.sampleRate(), effectiveGain, hit.pan,
                      targetFrame, track);
  }
};

// ============================================================================
// Timeline model — Phase 3.
//
// A TimelineTrack (NOT the same concept as StepScheduler's 4 fixed
// instrument rows, and NOT AudioEngine::TrackID, though each one owns an
// AudioEngine Track for future per-clip routing — see the TODO on
// TimelineScheduler::tick()) holds an ordered set of Clips positioned at
// arbitrary points on a linear beats timeline, instead of the old
// pattern-chain "song mode" order. Per the roadmap: "a step-pattern becomes
// one kind of clip (PatternClip)".
//
// AudioClip fields exist on Clip already so the struct doesn't need a
// breaking shape change later, but AudioClip playback, waveform display,
// recording, and non-destructive trim/fade are NOT implemented yet — see
// the roadmap notes at the bottom of this file's diff summary.
// ============================================================================

using ClipID = uint64_t;
using TimelineTrackID = uint32_t;

enum class ClipType { Pattern, Audio };

// startBeat/lengthBeats live on a timeline-global beat axis, independent of
// any individual pattern's own stepsPerBeat/numSteps — that's what lets
// clips built from differently-subdivided patterns sit on one timeline.
// TimelineScheduler maps global position to each clip's own pattern grid
// internally (see kPPQ).
struct Clip {
  ClipID id = 0;
  ClipType type = ClipType::Pattern;
  double startBeat = 0.0;
  double lengthBeats = 4.0;

  // ── PatternClip ──────────────────────────────────────────────────────
  int patternSlot = -1; // index into StepScheduler::patternSlots

  // ── AudioClip (stub — not yet played back or editable; see notes) ────
  std::string audioFilePath;
  double audioStartOffsetSec = 0.0;
  float gain = 1.0f;
  float fadeInBeats = 0.0f;
  float fadeOutBeats = 0.0f;
};

enum class AutomationParam { TrackVolume, TrackPan };

struct AutomationPoint {
  double beat = 0.0;
  float value = 1.0f; // TrackVolume: 0..1.5 gain. TrackPan: -1..1.
};

struct AutomationLane {
  AutomationParam param = AutomationParam::TrackVolume;
  std::vector<AutomationPoint> points; // MUST stay sorted by beat — every
                                       // inserter below maintains this
                                       // invariant rather than sorting on
                                       // every evaluate() call.
  bool enabled = true;
};

struct TimelineTrack {
  TimelineTrackID id = 0;
  std::string name;
  TrackID engineTrack = kInvalidTrack; // reserved for per-clip engine
                                       // routing — not wired yet, see
                                       // TimelineScheduler::tick()
  std::vector<Clip> clips;
  bool muted = false;
  bool soloed = false;

  // ── Automation (Phase 4) ─────────────────────────────────────────────
  // A lane is a sorted list of (beat, value) points; TimelineScheduler
  // linearly interpolates between them and writes the result straight to
  // the AudioEngine Track each tick(), same "coarse sampled-and-held"
  // philosophy _updateAudioClipFades() already uses for clip fades — no
  // sample-accurate ramp command yet (that's still a Phase 4 stretch/
  // Phase 5 item), just a per-~25ms update.
  std::vector<AutomationLane> automationLanes;

  // ── Automation lane display (UI-only, not persisted) ─────────────────
  // Which lane, if any, TimelineSurface draws below this track's clips.
  // Lives on the track rather than as a parallel vector in SequencerApp
  // so TimelineSurface — which only ever sees a Timeline*, not the app's
  // own State vectors — can read it directly during render()/hit-testing,
  // the same way it already reads muted/soloed.
  bool automationVisible = false;
  AutomationParam automationDisplayParam = AutomationParam::TrackVolume;
};

struct Timeline {
  std::vector<TimelineTrack> tracks;
};

// ============================================================================
// TimelineScheduler — walks Timeline clips and fires PatternClip steps at
// the right sample-accurate time, using StepScheduler::fireStep() so the
// actual sound produced is identical to pattern/song mode.
//
// Uses a fixed PPQN (pulses per quarter-note/beat) tick instead of a
// per-clip "next step" cursor, because different clips can be built from
// patterns with different stepsPerBeat — a single shared pulse grid is
// what makes "does this clip have a step due right now" a cheap modulo
// check instead of N independent float-drift-prone timers. kPPQ=96 divides
// evenly by every subdivision StepScheduler currently exposes (1-8) plus
// common tuplet values, so pattern step boundaries always land exactly on
// a pulse with no rounding.
// ============================================================================

class TimelineScheduler {
public:
  static constexpr int kPPQ = 96;
  // Matches the ~25ms cadence tick() is actually driven at (see
  // SequencerApp's setInterval(25, ...)). Each automation poll
  // schedules a ramp covering the gap until the next one, so playback
  // hears a smooth interpolation instead of a stair-step.
  static constexpr double kAutomationPollSeconds = 0.025;

  Timeline &timeline;
  bool playing = false;
  int64_t currentPulse = 0;

  // References StepScheduler's pattern storage (patternSlots) and
  // instrument tracks (trackVoice/trackMuted/trackSoloed) directly rather
  // than duplicating them — patterns are shared data between the classic
  // pattern-chain transport and the timeline transport, per the roadmap's
  // "keep the old step-grid behavior working as PatternClip".
  TimelineScheduler(StepScheduler &seq, Timeline &tl)
      : _seq(seq), timeline(tl) {}

  void start() {
    playing = true;
    currentPulse = 0;
    _nextPulseFrame = AudioEngine::get().currentSampleTime();
  }
  void stop() { playing = false; }

  double playheadBeats() const { return (double)currentPulse / kPPQ; }

  void tick() {
    if (!playing)
      return;

    auto &engine = AudioEngine::get();
    uint64_t now = engine.currentSampleTime();
    uint64_t lookahead = (uint64_t)(engine.sampleRate() * 0.1);

    while (_nextPulseFrame < now + lookahead) {
      bool anyTrackSoloed =
          std::any_of(timeline.tracks.begin(), timeline.tracks.end(),
                      [](const TimelineTrack &t) { return t.soloed; });

      for (auto &track : timeline.tracks) {
        bool audible = !track.muted && (!anyTrackSoloed || track.soloed);
        if (!audible)
          continue;
        for (auto &clip : track.clips) {
          if (clip.type == ClipType::Pattern)
            _tickPatternClip(track, clip, _nextPulseFrame);
          else if (clip.type == ClipType::Audio)
            _tickAudioClipBoundary(track, clip, _nextPulseFrame, audible);
        }
      }

      currentPulse++;
      _nextPulseFrame += _framesPerPulse();
    }

    // Fades are recomputed once per tick() call, not once per pulse above —
    // see _updateAudioClipFades()'s comment for why.
    _updateAudioClipFades();

    // Same cadence/reasoning as fades — automation values are a coarse
    // sampled-and-held approximation, recomputed once per tick() (~25ms),
    // not per pulse.
    _updateAutomation();
  }

private:
  StepScheduler &_seq;
  uint64_t _nextPulseFrame = 0;

  // ── Audio clip playback state ──────────────────────────────────────
  // Sample bank lookups are cached by path so re-entering the same
  // AudioClip (loop mode, or just playing the timeline more than once)
  // doesn't re-decode the file every time the playhead crosses it.
  std::unordered_map<std::string, SampleID> _audioSampleCache;

  struct ActiveAudioClip {
    VoiceHandle voice = kInvalidVoice;
    double startBeat = 0.0;
    double lengthBeats = 4.0;
    float baseGain = 1.0f;
    float fadeInBeats = 0.f;
    float fadeOutBeats = 0.f;
  };
  // Keyed by Clip::id rather than voice handle — lets
  // _tickAudioClipBoundary detect "already started" / "time to stop"
  // without a second lookup into the Timeline's track/clip vectors.
  std::unordered_map<ClipID, ActiveAudioClip> _activeAudioClips;

  uint64_t _framesPerPulse() const {
    double secondsPerBeat = 60.0 / _seq.bpm; // shares StepScheduler's bpm —
                                             // one tempo for the whole app
    double secondsPerPulse = secondsPerBeat / kPPQ;
    uint64_t frames =
        (uint64_t)(secondsPerPulse * AudioEngine::get().sampleRate());
    return std::max<uint64_t>(1, frames);
  }

  void _tickPatternClip(const TimelineTrack &track, const Clip &clip,
                        uint64_t targetFrame) {
    int64_t clipStartPulse = (int64_t)std::llround(clip.startBeat * kPPQ);
    int64_t clipLenPulses = (int64_t)std::llround(clip.lengthBeats * kPPQ);
    int64_t rel = currentPulse - clipStartPulse;
    if (rel < 0 || rel >= clipLenPulses)
      return; // playhead isn't inside this clip's span

    if (clip.patternSlot < 0 || clip.patternSlot >= StepScheduler::kMaxPatterns)
      return;
    Pattern &pat = _seq.patternSlots[clip.patternSlot];
    if (!pat.active || pat.numSteps <= 0)
      return;

    int pulsesPerStep = std::max(1, kPPQ / std::max(1, pat.stepsPerBeat));
    if (rel % pulsesPerStep != 0)
      return; // not a step boundary for this pattern's subdivision

    int stepIndex = (int)((rel / pulsesPerStep) % pat.numSteps);

    uint64_t stepFrames = (uint64_t)pulsesPerStep * _framesPerPulse();
    int64_t swingOffsetFrames =
        (stepIndex % 2 == 1)
            ? (int64_t)((pat.swing / 100.0) * 0.5 * (double)stepFrames)
            : 0;

    bool anySoloed =
        std::any_of(_seq.trackSoloed.begin(), _seq.trackSoloed.end(),
                    [](bool b) { return b; });
    for (size_t t = 0; t < pat.steps.size() && t < _seq.trackVoice.size();
         t++) {
      const StepData &step = pat.steps[t][stepIndex];
      if (!step.on)
        continue;
      bool audible = !_seq.trackMuted[t] && (!anySoloed || _seq.trackSoloed[t]);
      if (!audible)
        continue;
      if (step.probability < 1.0f &&
          (float)std::rand() / (float)RAND_MAX > step.probability)
        continue;

      int64_t microFrames =
          (int64_t)((double)step.microTiming * (double)stepFrames);
      int64_t totalOffset = microFrames + swingOffsetFrames;
      uint64_t adjustedTarget =
          (totalOffset < 0 && (uint64_t)(-totalOffset) > targetFrame)
              ? 0
              : (uint64_t)((int64_t)targetFrame + totalOffset);

      StepScheduler::fireStep(_seq.trackVoice[t], step, adjustedTarget,
                              track.engineTrack);
    }
  }

  // Starts/stops AudioClip playback as the playhead crosses each clip's
  // boundaries. Called once per pulse per clip, same cadence as
  // _tickPatternClip — cheap integer comparisons dominate the common case
  // where the playhead isn't at a boundary.
  void _tickAudioClipBoundary(const TimelineTrack &track, const Clip &clip,
                              uint64_t targetFrame, bool audible) {
    int64_t clipStartPulse = (int64_t)std::llround(clip.startBeat * kPPQ);
    int64_t clipLenPulses = (int64_t)std::llround(clip.lengthBeats * kPPQ);
    if (clipLenPulses <= 0)
      return;

    int64_t rel = currentPulse - clipStartPulse;
    if (rel == 0) {
      // Known simplification: a track muted/soloed-out at the exact
      // instant a clip would start simply never starts it — unmuting
      // later in the same clip's span doesn't retroactively start
      // playback, matching how pattern-clip steps are gated at fire time
      // rather than continuously.
      if (audible)
        _startAudioClip(track, clip, targetFrame);
    } else if (rel == clipLenPulses) {
      _stopAudioClip(clip.id);
    }
  }

  void _startAudioClip(const TimelineTrack &track, const Clip &clip,
                       uint64_t targetFrame) {

    if (_activeAudioClips.count(clip.id))
      return;

    auto &engine = AudioEngine::get();
    SampleID sampleId = kInvalidSample;
    auto cacheIt = _audioSampleCache.find(clip.audioFilePath);
    if (cacheIt != _audioSampleCache.end()) {
      sampleId = cacheIt->second;
    } else {
      sampleId = engine.loadSample(clip.audioFilePath);
      if (sampleId != kInvalidSample)
        _audioSampleCache[clip.audioFilePath] = sampleId;
    }
    if (sampleId == kInvalidSample) {
      return;
    }

    float initialGain = (clip.fadeInBeats > 0.f) ? 0.f : clip.gain;
    VoiceHandle v = engine.play(sampleId, initialGain, 0.f, false, 1.f,
                                targetFrame, track.engineTrack);

    if (v == kInvalidVoice)
      return;

    if (clip.audioStartOffsetSec > 0.0) {
      float durSec = engine.getSampleDurationSeconds(sampleId);
      // Known simplification: seekVoice() is a separately-queued command,
      // not part of the same sample-accurate StartVoice command as play()
      // above, so the seek can land a block or two after the voice
      // actually starts — a few ms of audible pre-roll from frame 0 on
      // trims with a nonzero start offset. Fixing it means adding a
      // start-offset field to StartVoice itself.
      if (durSec > 0.f)
        engine.seekVoice(
            v, (float)std::min(1.0, clip.audioStartOffsetSec / (double)durSec));
    }

    _activeAudioClips[clip.id] = {
        v,         clip.startBeat,   clip.lengthBeats,
        clip.gain, clip.fadeInBeats, clip.fadeOutBeats};
  }

  void _stopAudioClip(ClipID id) {
    auto it = _activeAudioClips.find(id);
    if (it == _activeAudioClips.end())
      return;
    AudioEngine::get().stopVoice(it->second.voice);
    _activeAudioClips.erase(it);
  }

  // Applies per-clip fade in/out as a plain gain ramp, recomputed once per
  // tick() call (~25ms, the same cadence the transport UI polls at) rather
  // than per pulse — the engine has no gain-ramp command yet (that's a
  // Phase 4 automation-lane concern), so this is a coarse
  // sampled-and-held approximation of a fade, not a sample-accurate one.
  void _updateAudioClipFades() {
    auto &engine = AudioEngine::get();
    double posBeats = playheadBeats();
    for (auto it = _activeAudioClips.begin(); it != _activeAudioClips.end();) {
      ActiveAudioClip &av = it->second;
      if (!engine.isVoiceActive(av.voice)) {
        it = _activeAudioClips.erase(it); // finished naturally (shorter
                                          // than the clip's box)
        continue;
      }
      double elapsed = posBeats - av.startBeat;
      double remaining = (av.startBeat + av.lengthBeats) - posBeats;
      float fadeGain = 1.f;
      if (av.fadeInBeats > 0.f && elapsed < av.fadeInBeats)
        fadeGain =
            std::min(fadeGain, (float)std::max(0.0, elapsed / av.fadeInBeats));
      if (av.fadeOutBeats > 0.f && remaining < av.fadeOutBeats)
        fadeGain = std::min(fadeGain,
                            (float)std::max(0.0, remaining / av.fadeOutBeats));
      engine.setVoiceGain(av.voice, av.baseGain * fadeGain);
      ++it;
    }
  }
  // Linear interpolation between the two points straddling `beat`;
  // clamps to the first/last point outside the lane's own span (holds
  // flat before the first point and after the last, standard DAW
  // automation-lane behavior).
  static float _evaluateLane(const AutomationLane &lane, double beat) {
    const auto &pts = lane.points;
    if (pts.empty())
      return lane.param == AutomationParam::TrackVolume ? 1.0f : 0.0f;
    if (beat <= pts.front().beat)
      return pts.front().value;
    if (beat >= pts.back().beat)
      return pts.back().value;
    for (size_t i = 0; i + 1 < pts.size(); i++) {
      if (beat >= pts[i].beat && beat <= pts[i + 1].beat) {
        double span = pts[i + 1].beat - pts[i].beat;
        double t = span > 0.0 ? (beat - pts[i].beat) / span : 0.0;
        return pts[i].value + (float)t * (pts[i + 1].value - pts[i].value);
      }
    }
    return pts.back().value; // unreachable given the clamps above
  }

  void _updateAutomation() {
    double posBeats = playheadBeats();
    auto &engine = AudioEngine::get();
    uint64_t now = engine.currentSampleTime();
    uint64_t rampFrames = std::max<uint64_t>(
        1, (uint64_t)((double)engine.sampleRate() * kAutomationPollSeconds));
    uint64_t endFrame = now + rampFrames;

    for (auto &track : timeline.tracks) {
      if (track.engineTrack == kInvalidTrack)
        continue;
      for (auto &lane : track.automationLanes) {
        if (!lane.enabled || lane.points.empty())
          continue;
        float v = _evaluateLane(lane, posBeats);
        if (lane.param == AutomationParam::TrackVolume)
          engine.rampTrackGain(track.engineTrack, v, now, endFrame);
        else
          engine.rampTrackPan(track.engineTrack, v, now, endFrame);
      }
    }
  }
};

// ============================================================================
// Minimal embedded JSON — project save/load only.
//
// Deliberately NOT a general-purpose JSON library: no unicode escapes, no
// exponent edge cases beyond what strtod handles, no streaming. It exists so
// project files don't take on an undocumented dependency on whatever
// JsonValue backs JsonBuilder/JsonStreamBuilder elsewhere in the framework
// (that type's write-side API isn't visible from here). Don't reuse this
// outside sequencer project I/O without hardening it first.
// ============================================================================
namespace SeqJson {

inline std::string esc(const std::string &s) {
  std::string out;
  out.reserve(s.size());
  for (char c : s) {
    switch (c) {
    case '"':
      out += "\\\"";
      break;
    case '\\':
      out += "\\\\";
      break;
    case '\n':
      out += "\\n";
      break;
    case '\r':
      out += "\\r";
      break;
    case '\t':
      out += "\\t";
      break;
    default:
      out += c;
    }
  }
  return out;
}

struct JVal {
  enum class Type {
    Null,
    Bool,
    Number,
    String,
    Array,
    Object
  } type = Type::Null;
  double num = 0;
  bool boolean = false;
  std::string str;
  std::vector<JVal> arr;
  std::vector<std::pair<std::string, JVal>> obj;

  double asDouble(double def = 0) const {
    return type == Type::Number ? num : def;
  }
  int asInt(int def = 0) const { return type == Type::Number ? (int)num : def; }
  bool asBool(bool def = false) const {
    return type == Type::Bool ? boolean : def;
  }
  std::string asString(const std::string &def = "") const {
    return type == Type::String ? str : def;
  }

  const JVal *find(const std::string &key) const {
    for (auto &kv : obj)
      if (kv.first == key)
        return &kv.second;
    return nullptr;
  }
  // Never returns null — a missing key just yields a Null JVal, so chained
  // access like val["tracks"].arr doesn't need a null check at each step.
  const JVal &operator[](const std::string &key) const {
    static const JVal kNull{};
    const JVal *v = find(key);
    return v ? *v : kNull;
  }
};

class Parser {
public:
  explicit Parser(const std::string &s) : _s(s) {}
  bool parse(JVal &out) {
    _skipWs();
    return _parseValue(out);
  }

private:
  const std::string &_s;
  size_t _i = 0;

  void _skipWs() {
    while (_i < _s.size() && std::isspace((unsigned char)_s[_i]))
      _i++;
  }
  char _peek() const { return _i < _s.size() ? _s[_i] : '\0'; }
  bool _consume(char c) {
    _skipWs();
    if (_peek() != c)
      return false;
    _i++;
    return true;
  }

  bool _parseValue(JVal &out) {
    _skipWs();
    char c = _peek();
    if (c == '{')
      return _parseObject(out);
    if (c == '[')
      return _parseArray(out);
    if (c == '"')
      return _parseString(out);
    if (c == 't' || c == 'f')
      return _parseBool(out);
    if (c == 'n') {
      _i += 4;
      out.type = JVal::Type::Null;
      return true;
    }
    return _parseNumber(out);
  }

  bool _parseObject(JVal &out) {
    if (!_consume('{'))
      return false;
    out.type = JVal::Type::Object;
    _skipWs();
    if (_peek() == '}') {
      _i++;
      return true;
    }
    while (true) {
      _skipWs();
      JVal key;
      if (!_parseString(key))
        return false;
      if (!_consume(':'))
        return false;
      JVal val;
      if (!_parseValue(val))
        return false;
      out.obj.emplace_back(key.str, std::move(val));
      _skipWs();
      if (_peek() == ',') {
        _i++;
        continue;
      }
      if (_peek() == '}') {
        _i++;
        break;
      }
      return false;
    }
    return true;
  }

  bool _parseArray(JVal &out) {
    if (!_consume('['))
      return false;
    out.type = JVal::Type::Array;
    _skipWs();
    if (_peek() == ']') {
      _i++;
      return true;
    }
    while (true) {
      JVal val;
      if (!_parseValue(val))
        return false;
      out.arr.push_back(std::move(val));
      _skipWs();
      if (_peek() == ',') {
        _i++;
        continue;
      }
      if (_peek() == ']') {
        _i++;
        break;
      }
      return false;
    }
    return true;
  }

  bool _parseString(JVal &out) {
    if (!_consume('"'))
      return false;
    out.type = JVal::Type::String;
    std::string s;
    while (_i < _s.size() && _s[_i] != '"') {
      char c = _s[_i++];
      if (c == '\\' && _i < _s.size()) {
        char e = _s[_i++];
        switch (e) {
        case 'n':
          s += '\n';
          break;
        case 't':
          s += '\t';
          break;
        case 'r':
          s += '\r';
          break;
        case '"':
          s += '"';
          break;
        case '\\':
          s += '\\';
          break;
        case '/':
          s += '/';
          break;
        default:
          s += e;
        }
      } else {
        s += c;
      }
    }
    if (_i >= _s.size())
      return false; // unterminated
    _i++;
    out.str = std::move(s);
    return true;
  }

  bool _parseBool(JVal &out) {
    if (_s.compare(_i, 4, "true") == 0) {
      out.type = JVal::Type::Bool;
      out.boolean = true;
      _i += 4;
      return true;
    }
    if (_s.compare(_i, 5, "false") == 0) {
      out.type = JVal::Type::Bool;
      out.boolean = false;
      _i += 5;
      return true;
    }
    return false;
  }

  bool _parseNumber(JVal &out) {
    size_t start = _i;
    if (_peek() == '-')
      _i++;
    while (_i < _s.size() &&
           (std::isdigit((unsigned char)_s[_i]) || _s[_i] == '.' ||
            _s[_i] == 'e' || _s[_i] == 'E' || _s[_i] == '+' || _s[_i] == '-'))
      _i++;
    if (_i == start)
      return false;
    out.type = JVal::Type::Number;
    out.num = std::atof(_s.substr(start, _i - start).c_str());
    return true;
  }
};

inline bool parse(const std::string &text, JVal &out) {
  return Parser(text).parse(out);
}

} // namespace SeqJson

// ============================================================================
// Minimal mono 16-bit PCM WAV writer — recording output only.
//
// Deliberately not routed through AudioEngine: bounceToWav() there writes
// the engine's realtime mix output via a private ma_encoder, not an
// arbitrary in-memory buffer, so it can't serialize a captured take.
// Standard 44-byte RIFF/WAVE header, no extension chunks — sufficient
// for round-tripping through AudioEngine::loadSample() afterward, which
// is the only consumer.
// ============================================================================
inline bool writeWavMono16(const std::string &path,
                           const std::vector<float> &samples,
                           uint32_t sampleRate) {
  std::ofstream f(path, std::ios::binary);
  if (!f)
    return false;

  uint32_t dataBytes = (uint32_t)(samples.size() * sizeof(int16_t));
  uint32_t byteRate = sampleRate * 1 /*channel*/ * sizeof(int16_t);
  uint16_t blockAlign = (uint16_t)sizeof(int16_t);
  uint32_t riffSize = 36 + dataBytes;
  auto writeU32 = [&](uint32_t v) {
    f.write(reinterpret_cast<const char *>(&v), 4);
  };
  auto writeU16 = [&](uint16_t v) {
    f.write(reinterpret_cast<const char *>(&v), 2);
  };

  f.write("RIFF", 4);
  writeU32(riffSize);
  f.write("WAVE", 4);
  f.write("fmt ", 4);
  writeU32(16); // fmt chunk size (PCM)
  writeU16(1);  // format tag: PCM
  writeU16(1);  // channels: mono
  writeU32(sampleRate);
  writeU32(byteRate);
  writeU16(blockAlign);
  writeU16(16); // bits per sample
  f.write("data", 4);
  writeU32(dataBytes);

  for (float s : samples) {
    float clamped = std::max(-1.f, std::min(1.f, s));
    int16_t pcm = (int16_t)std::lround(clamped * 32767.f);
    f.write(reinterpret_cast<const char *>(&pcm), 2);
  }
  return (bool)f;
}

// ============================================================================
// TimelineSurface — draws Timeline tracks/clips and handles click input.
// Pan/zoom/scrollbars come free from CanvasWidget's built-in viewport (see
// the Canvas docs — middle-drag or space+drag to pan, ctrl+scroll to zoom);
// mouse coordinates arrive already in canvas-space, so beat math below
// doesn't need to account for scroll/zoom manually.
//
// Deliberately does NOT mutate `timeline` directly — every interaction goes
// through onEmptyClick/onClipClick so SequencerApp remains the single
// source of truth (same call-back-into-owner shape FilePickerWidget uses),
// which keeps the door open for undo support on timeline edits later.
// ============================================================================

class TimelineSurface : public RenderSurface {
public:
  static constexpr float kPxPerBeat = 40.f;
  static constexpr float kTrackHeight = 108.f;

  // Hit-test radius for grabbing a fade handle — larger than the 4px dot
  // actually drawn, so it's easy to grab without pixel-perfect aim.
  static constexpr float kHandleHitRadius = 8.f;

  // Trim handle geometry — narrower than the fade dot's hit radius since
  // trim handles sit flush against the clip edge instead of floating
  // inward, and a small hit-pad is added separately in onMouseDown so
  // they're still easy to grab without visually ballooning the bar.
  static constexpr float kTrimHandleWidth = 5.f;
  static constexpr float kTrimHandleHitPad = 4.f;

  // Bars rendered per clip, independent of the clip's current pixel
  // width — bars just stretch across whatever width the clip occupies,
  // so a future zoom (kPxPerBeat change) doesn't require re-fetching
  // peaks at a different resolution.
  static constexpr int kWaveformBuckets = 128;

  // Resolution the FULL file's waveform is cached at, before any
  // per-clip cropping. Higher than kWaveformBuckets so a heavily
  // trimmed clip (showing a small slice of a long file) still has
  // enough source detail to max-reduce from instead of just repeating
  // a handful of hi-res samples — same reasoning as the engine's own
  // DecodedSample::peaksHiRes cache.
  static constexpr int kSourceWaveformBuckets = 512;

  // Automation lane geometry/behavior. Drag overflow/delete thresholds
  // are expressed as a fraction of the lane's own value range rather
  // than raw pixels, so they behave consistently regardless of lane
  // height or which parameter (Volume vs Pan, different ranges) is
  // showing.
  static constexpr float kAutomationLaneHeight = 56.f;
  static constexpr float kAutomationPointRadius = 4.f;
  static constexpr float kAutomationPadding = 8.f;
  static constexpr float kAutomationOverflowFrac = 0.4f; // how far past
                                                         // [lo,hi] a
                                                         // dragged point
                                                         // may stray
                                                         // before being
                                                         // clamped
  static constexpr float kAutomationDeleteFrac = 0.12f;  // how far past
                                                         // [lo,hi] before
                                                         // release marks
                                                         // the point for
                                                         // deletion

  Timeline *timeline = nullptr;
  StepScheduler *seq = nullptr;           // for pattern name lookups only
  TimelineScheduler *scheduler = nullptr; // for playhead position only
  ClipID selectedClip = 0;

  std::function<void(int trackIndex, double beat)> onEmptyClick;
  std::function<void(ClipID)> onClipClick;

  // Fade-handle drag lifecycle — mirrors mouseDown/Move/Up 1:1 rather than
  // a single "onFadeChanged" callback, so SequencerApp can snapshot the
  // pre-drag value on Start and coalesce the whole drag into one undo
  // entry on End, the same way step-cell undo entries are pushed once per
  // discrete edit rather than once per intermediate value.
  std::function<void(ClipID, bool isFadeIn)> onFadeDragStart;
  std::function<void(ClipID, bool isFadeIn, double beats)> onFadeDrag;
  std::function<void(ClipID, bool isFadeIn)> onFadeDragEnd;

  // Trim-handle drag lifecycle — same start/move/end shape as the fade
  // handles above. Unlike fades (which report a relative beats-from-edge
  // offset), trim reports the dragged edge's ABSOLUTE timeline beat
  // position: trimming the left edge moves the clip's startBeat itself,
  // so SequencerApp needs to know where the mouse actually is, not just
  // an offset from a moving reference point.
  std::function<void(ClipID, bool isLeftEdge)> onTrimDragStart;
  std::function<void(ClipID, bool isLeftEdge, double absoluteBeat)> onTrimDrag;
  std::function<void(ClipID, bool isLeftEdge)> onTrimDragEnd;

  // Automation-point interaction. Click empty lane space to add a point;
  // drag an existing point to change its value (beat is fixed once
  // placed — only the vertical position is draggable); drag it well past
  // the lane's top/bottom edge and release to delete it. Same
  // start/move/end shape as the fade/trim handles above, and the same
  // "live-apply during drag, coalesce into one undo entry on end"
  // contract — SequencerApp mutates the real point on every
  // onAutomationPointDrag call with no undo push, then decides
  // commit-vs-delete in onAutomationPointDragEnd.
  std::function<void(int trackIndex, AutomationParam param, double beat,
                     float value)>
      onAutomationPointAdd;
  std::function<void(int trackIndex, AutomationParam param, int pointIndex)>
      onAutomationPointDragStart;
  std::function<void(int trackIndex, AutomationParam param, int pointIndex,
                     float value)>
      onAutomationPointDrag;
  std::function<void(int trackIndex, AutomationParam param, int pointIndex,
                     bool deleted)>
      onAutomationPointDragEnd;

  void initialize(int, int) override {}
  void resize(int, int) override {}
  void destroy() override {}
  void update(double) override {}

  void render(Canvas2D &ctx) override {
    float w = (float)ctx.width();
    float h = (float)ctx.height();

    ctx.setFillColor(Color::fromRGB(250, 250, 252));
    ctx.fillRect(0, 0, w, h);

    if (!timeline)
      return;

    _recomputeLaneLayout();

    // Beat grid — heavier line every bar (4 beats).
    int totalBeats = (int)(w / kPxPerBeat) + 1;
    for (int b = 0; b <= totalBeats; b++) {
      float x = b * kPxPerBeat;
      ctx.setStrokeColor((b % 4 == 0) ? Color::fromRGB(190, 190, 200)
                                      : Color::fromRGB(225, 225, 230));
      ctx.setLineWidth(1);
      ctx.beginPath();
      ctx.moveTo(x, 0);
      ctx.lineTo(x, h);
      ctx.stroke();
    }

    // Track lanes + clips.
    for (size_t ti = 0; ti < timeline->tracks.size(); ti++) {
      float laneY = _laneTops[ti];
      ctx.setStrokeColor(Color::fromRGB(210, 210, 215));
      ctx.beginPath();
      ctx.moveTo(0, laneY);
      ctx.lineTo(w, laneY);
      ctx.stroke();

      for (const Clip &clip : timeline->tracks[ti].clips) {
        float cx = (float)(clip.startBeat * kPxPerBeat);
        float cw = (float)(clip.lengthBeats * kPxPerBeat);
        bool selected = clip.id == selectedClip;
        bool isAudio = clip.type == ClipType::Audio;

        ctx.setFillColor(selected  ? Color::fromRGB(99, 179, 237)
                         : isAudio ? Color::fromRGB(235, 165, 95)
                                   : Color::fromRGB(150, 150, 235));
        ctx.fillRoundedRect(cx + 1, laneY + 4, std::max(4.f, cw - 2),
                            kTrackHeight - 8, 4);

        std::string label = "Clip";
        if (isAudio) {
          size_t slash = clip.audioFilePath.find_last_of("/\\");
          label = clip.audioFilePath.empty()
                      ? "Audio"
                      : (slash == std::string::npos
                             ? clip.audioFilePath
                             : clip.audioFilePath.substr(slash + 1));
        } else if (seq && clip.patternSlot >= 0 &&
                   clip.patternSlot < (int)seq->patternSlots.size())
          label = seq->patternSlots[clip.patternSlot].name;

        ctx.setFillColor(Color::fromRGB(255, 255, 255));
        ctx.setFont("12px sans");
        ctx.setTextAlign(CanvasTextAlign::Left);
        ctx.setTextBaseline(TextBaseline::Top);
        ctx.fillText(label, cx + 6, laneY + 8);

        // Waveform preview + fade ramps/handles — AudioClip only.
        //
        // The waveform shown is cropped to [audioStartOffsetSec,
        // audioStartOffsetSec + lengthBeats-in-seconds] of the source
        // file — see _peaksForClip() — so trimming visibly changes what
        // shape is drawn, not just the clip's box width.
        if (isAudio) {

          std::vector<float> peaks = _peaksForClip(clip);
          if (!peaks.empty()) {
            float midY = laneY + kTrackHeight * 0.5f;
            float maxHalfHeight = (kTrackHeight - 12.f) * 0.5f;
            ctx.setStrokeColor(Color::fromRGB(255, 255, 255));
            ctx.setLineWidth(1.5f);
            for (size_t i = 0; i < peaks.size(); i++) {
              float bx =
                  cx + 1 + (cw - 2) * ((float)i + 0.5f) / (float)peaks.size();
              // sqrt boosts quiet-but-present audio into a visible shape —
              // linear scaling leaves anything under ~0.3 peak (common for
              // normally-mastered material) reading as a near-flat line.
              float visualPeak = std::sqrt(std::max(0.f, peaks[i]));
              float half = std::max(1.f, visualPeak * maxHalfHeight);
              ctx.beginPath();
              ctx.moveTo(bx, midY - half);
              ctx.lineTo(bx, midY + half);
              ctx.stroke();
            }
          }

          float top = laneY + 4;
          float bottom = laneY + kTrackHeight - 4;
          float fadeInPx =
              std::min((float)(clip.fadeInBeats * kPxPerBeat), cw * 0.5f);
          float fadeOutPx =
              std::min((float)(clip.fadeOutBeats * kPxPerBeat), cw * 0.5f);

          ctx.setStrokeColor(Color::fromRGB(255, 255, 255));
          ctx.setLineWidth(2);
          ctx.beginPath();
          ctx.moveTo(cx + 1, bottom);
          ctx.lineTo(cx + 1 + fadeInPx, top);
          ctx.stroke();

          ctx.beginPath();
          ctx.moveTo(cx + cw - 1 - fadeOutPx, top);
          ctx.lineTo(cx + cw - 1, bottom);
          ctx.stroke();

          ctx.setFillColor(Color::fromRGB(255, 255, 255));
          ctx.fillCircle(cx + 1 + fadeInPx, top, 4);
          ctx.fillCircle(cx + cw - 1 - fadeOutPx, top, 4);

          // Trim handles — thin grab bars at the clip's TRUE left/right
          // edges, confined to the lower ~60% of the clip so they don't
          // overlap the fade handles living in the top strip above.
          float trimTop = laneY + kTrackHeight * 0.4f;
          float trimBottom = laneY + kTrackHeight - 4;
          bool draggingThis = (clip.id == _draggingTrimClipId);
          ctx.setFillColor(draggingThis ? Color::fromRGB(255, 205, 80)
                                        : Color::fromRGB(255, 255, 255));
          ctx.fillRect(cx, trimTop, kTrimHandleWidth, trimBottom - trimTop);
          ctx.fillRect(cx + cw - kTrimHandleWidth, trimTop, kTrimHandleWidth,
                       trimBottom - trimTop);
        }
      }

      if (timeline->tracks[ti].automationVisible) {
        float autoTop = _laneTops[ti] + kTrackHeight;
        float autoBottom = autoTop + kAutomationLaneHeight;

        ctx.setFillColor(Color::fromRGB(246, 246, 250));
        ctx.fillRect(0, autoTop, w, kAutomationLaneHeight);
        ctx.setStrokeColor(Color::fromRGB(210, 210, 215));
        ctx.beginPath();
        ctx.moveTo(0, autoTop);
        ctx.lineTo(w, autoTop);
        ctx.stroke();

        AutomationParam param = timeline->tracks[ti].automationDisplayParam;
        float lo, hi;
        _automationValueRange(param, lo, hi);
        float top = autoTop + kAutomationPadding;
        float bottom = autoBottom - kAutomationPadding;

        AutomationLane *lane = _findLane(timeline->tracks[ti], param);

        ctx.setFillColor(Color::fromRGB(90, 90, 100));
        ctx.setFont("10px sans");
        ctx.setTextAlign(CanvasTextAlign::Left);
        ctx.setTextBaseline(TextBaseline::Top);
        ctx.fillText(param == AutomationParam::TrackVolume ? "Volume" : "Pan",
                     6, autoTop + 3);

        if (!lane || lane->points.empty()) {
          // No points yet — a flat reference line at the default value,
          // so there's something visible to click on.
          float defaultValue =
              (param == AutomationParam::TrackVolume) ? 1.f : 0.f;
          float y = _automationValueToY(defaultValue, lo, hi, top, bottom);
          ctx.setStrokeColor(Color::fromRGB(190, 190, 205));
          ctx.setLineWidth(1);
          ctx.beginPath();
          ctx.moveTo(0, y);
          ctx.lineTo(w, y);
          ctx.stroke();
        } else {
          ctx.setStrokeColor(Color::fromRGB(120, 130, 220));
          ctx.setLineWidth(1.5f);
          ctx.beginPath();
          // Hold flat before the first point and after the last — matches
          // TimelineScheduler::_evaluateLane's own clamped-hold semantics
          // so the drawn curve is exactly what will play.
          float firstY = _automationValueToY(lane->points.front().value, lo, hi,
                                             top, bottom);
          ctx.moveTo(0, firstY);
          ctx.lineTo((float)(lane->points.front().beat * kPxPerBeat), firstY);
          for (const AutomationPoint &pt : lane->points)
            ctx.lineTo((float)(pt.beat * kPxPerBeat),
                       _automationValueToY(pt.value, lo, hi, top, bottom));
          float lastY = _automationValueToY(lane->points.back().value, lo, hi,
                                            top, bottom);
          ctx.lineTo(w, lastY);
          ctx.stroke();

          for (size_t i = 0; i < lane->points.size(); i++) {
            float px = (float)(lane->points[i].beat * kPxPerBeat);
            float py =
                _automationValueToY(lane->points[i].value, lo, hi, top, bottom);
            bool dragging =
                ((int)ti == _draggingAutoTrack && param == _draggingAutoParam &&
                 (int)i == _draggingAutoPointIndex);
            ctx.setFillColor(dragging && _draggingAutoMarkedDelete
                                 ? Color::fromRGB(220, 60, 60)
                             : dragging ? Color::fromRGB(255, 205, 80)
                                        : Color::fromRGB(120, 130, 220));
            ctx.fillCircle(px, py, kAutomationPointRadius);
          }
        }
      }
    }

    // Playhead.
    if (scheduler && scheduler->playing) {
      float px = (float)(scheduler->playheadBeats() * kPxPerBeat);
      ctx.setStrokeColor(Color::fromRGB(220, 50, 50));
      ctx.setLineWidth(2);
      ctx.beginPath();
      ctx.moveTo(px, 0);
      ctx.lineTo(px, h);
      ctx.stroke();
    }
  }

  void onMouseDown(float x, float y) override {
    if (!timeline || timeline->tracks.empty())
      return;
    _recomputeLaneLayout();
    LaneHit hit = _hitTestLane(y);
    if (hit.trackIndex < 0)
      return;

    if (hit.inAutomation) {
      _handleAutomationMouseDown(hit.trackIndex, hit.regionTop, x, y);
      return;
    }

    int ti = hit.trackIndex;
    double beat = x / kPxPerBeat;

    float top = hit.regionTop + 4;
    float laneTopY = hit.regionTop; // for trimBottom below, since ti no
                                    // longer implies a fixed row height

    // Fade handles take priority over clip-select/empty-click — they sit
    // right at a clip's edges, exactly where a body click would otherwise
    // fire instead.
    for (const Clip &clip : timeline->tracks[ti].clips) {
      if (clip.type != ClipType::Audio)
        continue;
      float cx = (float)(clip.startBeat * kPxPerBeat);
      float cw = (float)(clip.lengthBeats * kPxPerBeat);
      float fadeInPx =
          std::min((float)(clip.fadeInBeats * kPxPerBeat), cw * 0.5f);
      float fadeOutPx =
          std::min((float)(clip.fadeOutBeats * kPxPerBeat), cw * 0.5f);

      if (_withinHandle(x, y, cx + 1 + fadeInPx, top)) {
        _draggingFadeClipId = clip.id;
        _draggingFadeIsIn = true;
        if (onFadeDragStart)
          onFadeDragStart(clip.id, true);
        return;
      }
      if (_withinHandle(x, y, cx + cw - 1 - fadeOutPx, top)) {
        _draggingFadeClipId = clip.id;
        _draggingFadeIsIn = false;
        if (onFadeDragStart)
          onFadeDragStart(clip.id, false);
        return;
      }

      float trimTop = top + kTrackHeight * 0.4f - 4.f; // 'top' here is
                                                       // laneY+4; shift
                                                       // down past the
                                                       // fade-handle strip
      float trimBottom = laneTopY + kTrackHeight - 4;
      if (y >= trimTop && y <= trimBottom) {
        if (x >= cx - kTrimHandleHitPad &&
            x <= cx + kTrimHandleWidth + kTrimHandleHitPad) {
          _draggingTrimClipId = clip.id;
          _draggingTrimIsLeft = true;
          if (onTrimDragStart)
            onTrimDragStart(clip.id, true);
          return;
        }
        if (x >= cx + cw - kTrimHandleWidth - kTrimHandleHitPad &&
            x <= cx + cw + kTrimHandleHitPad) {
          _draggingTrimClipId = clip.id;
          _draggingTrimIsLeft = false;
          if (onTrimDragStart)
            onTrimDragStart(clip.id, false);
          return;
        }
      }
    }

    for (const Clip &clip : timeline->tracks[ti].clips) {
      if (beat >= clip.startBeat && beat < clip.startBeat + clip.lengthBeats) {
        if (onClipClick)
          onClipClick(clip.id);
        return;
      }
    }

    if (onEmptyClick)
      onEmptyClick(ti, beat);
  }

  void onMouseMove(float x, float y) override {
    if (!timeline)
      return;

    _recomputeLaneLayout();

    if (_draggingAutoTrack >= 0) {
      if (_draggingAutoTrack >= (int)timeline->tracks.size()) {
        _resetAutoDrag();
        return;
      }
      TimelineTrack &track = timeline->tracks[_draggingAutoTrack];
      AutomationLane *lane = _findLane(track, _draggingAutoParam);
      if (!lane || _draggingAutoPointIndex < 0 ||
          _draggingAutoPointIndex >= (int)lane->points.size()) {
        _resetAutoDrag(); // point deleted/lane cleared mid-drag — bail
        return;
      }

      float lo, hi;
      _automationValueRange(_draggingAutoParam, lo, hi);
      float autoTop = _laneTops[_draggingAutoTrack] + kTrackHeight;
      float top = autoTop + kAutomationPadding;
      float bottom = autoTop + kAutomationLaneHeight - kAutomationPadding;
      float range = hi - lo;

      float value = _automationYToValue(y, lo, hi, top, bottom);
      float lo2 = lo - range * kAutomationOverflowFrac;
      float hi2 = hi + range * kAutomationOverflowFrac;
      value = std::max(lo2, std::min(hi2, value));

      // Marked here, acted on at release — lets the dot keep following
      // the mouse right up to mouse-up instead of snapping back early.
      _draggingAutoMarkedDelete = value < lo - range * kAutomationDeleteFrac ||
                                  value > hi + range * kAutomationDeleteFrac;

      if (onAutomationPointDrag)
        onAutomationPointDrag(_draggingAutoTrack, _draggingAutoParam,
                              _draggingAutoPointIndex, value);
      return;
    }

    if (_draggingFadeClipId != 0) {
      Clip *clip = _findClip(_draggingFadeClipId);
      if (!clip) {
        _draggingFadeClipId = 0; // clip deleted mid-drag — bail cleanly
        return;
      }
      double mouseBeat = x / kPxPerBeat;
      double beats = _draggingFadeIsIn
                         ? (mouseBeat - clip->startBeat)
                         : ((clip->startBeat + clip->lengthBeats) - mouseBeat);
      beats = std::max(0.0, std::min(beats, clip->lengthBeats * 0.5));
      if (onFadeDrag)
        onFadeDrag(_draggingFadeClipId, _draggingFadeIsIn, beats);
      return;
    }

    if (_draggingTrimClipId != 0) {
      Clip *clip = _findClip(_draggingTrimClipId);
      if (!clip) {
        _draggingTrimClipId = 0; // clip deleted mid-drag — bail cleanly
        return;
      }
      double mouseBeat = x / kPxPerBeat;
      if (onTrimDrag)
        onTrimDrag(_draggingTrimClipId, _draggingTrimIsLeft, mouseBeat);
    }
  }

  void onMouseUp(float, float) override {
    if (_draggingAutoTrack >= 0) {
      if (onAutomationPointDragEnd)
        onAutomationPointDragEnd(_draggingAutoTrack, _draggingAutoParam,
                                 _draggingAutoPointIndex,
                                 _draggingAutoMarkedDelete);
      _resetAutoDrag();
    }
    if (_draggingFadeClipId != 0) {
      if (onFadeDragEnd)
        onFadeDragEnd(_draggingFadeClipId, _draggingFadeIsIn);
      _draggingFadeClipId = 0;
    }
    if (_draggingTrimClipId != 0) {
      if (onTrimDragEnd)
        onTrimDragEnd(_draggingTrimClipId, _draggingTrimIsLeft);
      _draggingTrimClipId = 0;
    }
  }

  // Only needed while something is actually playing (playhead sweep);
  // idle editing is fully event-driven via redraw() from the App side.
  bool needsContinuousRedraw() const override {
    return (scheduler && scheduler->playing) || _draggingFadeClipId != 0 ||
           _draggingTrimClipId != 0 || _draggingAutoTrack >= 0;
  }

private:
  ClipID _draggingFadeClipId = 0;
  bool _draggingFadeIsIn = false;

  ClipID _draggingTrimClipId = 0;
  bool _draggingTrimIsLeft = false;

  // Automation-point drag state — identifies the point being dragged by
  // (track, param, index) since points have no stable ID; see
  // SequencerApp::_timelineAutomationPointDragEnd for why beat is used
  // as the identity key once we're back on the app side.
  int _draggingAutoTrack = -1;
  AutomationParam _draggingAutoParam = AutomationParam::TrackVolume;
  int _draggingAutoPointIndex = -1;
  bool _draggingAutoMarkedDelete = false;

  void _resetAutoDrag() {
    _draggingAutoTrack = -1;
    _draggingAutoPointIndex = -1;
    _draggingAutoMarkedDelete = false;
  }

  // Per-track top Y, recomputed at the start of every render()/
  // onMouseDown()/onMouseMove() — a track's total height depends on
  // whether its automation lane is shown, so this can't stay a fixed
  // `ti * kTrackHeight` anymore. Cheap for the handful of tracks this
  // app supports.
  std::vector<float> _laneTops;

  void _recomputeLaneLayout() {
    _laneTops.assign(timeline->tracks.size(), 0.f);
    float y = 0.f;
    for (size_t i = 0; i < timeline->tracks.size(); i++) {
      _laneTops[i] = y;
      y += kTrackHeight;
      if (timeline->tracks[i].automationVisible)
        y += kAutomationLaneHeight;
    }
  }

  struct LaneHit {
    int trackIndex = -1;
    bool inAutomation = false;
    float regionTop = 0.f; // top Y of whichever region was hit
  };

  LaneHit _hitTestLane(float y) const {
    for (size_t i = 0; i < timeline->tracks.size(); i++) {
      float clipTop = _laneTops[i];
      float clipBottom = clipTop + kTrackHeight;
      if (y >= clipTop && y < clipBottom)
        return {(int)i, false, clipTop};
      if (timeline->tracks[i].automationVisible) {
        float autoTop = clipBottom;
        float autoBottom = autoTop + kAutomationLaneHeight;
        if (y >= autoTop && y < autoBottom)
          return {(int)i, true, autoTop};
      }
    }
    return {};
  }

  static AutomationLane *_findLane(TimelineTrack &track,
                                   AutomationParam param) {
    for (auto &l : track.automationLanes)
      if (l.param == param)
        return &l;
    return nullptr;
  }

  static void _automationValueRange(AutomationParam p, float &lo, float &hi) {
    if (p == AutomationParam::TrackVolume) {
      lo = 0.f;
      hi = 1.5f;
    } else {
      lo = -1.f;
      hi = 1.f;
    }
  }

  static float _automationValueToY(float value, float lo, float hi, float top,
                                   float bottom) {
    float t = (hi > lo) ? (value - lo) / (hi - lo) : 0.f;
    return bottom - t * (bottom - top);
  }

  static float _automationYToValue(float y, float lo, float hi, float top,
                                   float bottom) {
    float t = (bottom > top) ? (bottom - y) / (bottom - top) : 0.f;
    return lo + t * (hi - lo);
  }

  void _handleAutomationMouseDown(int ti, float laneTop, float x, float y) {
    TimelineTrack &track = timeline->tracks[ti];
    AutomationParam param = track.automationDisplayParam;
    AutomationLane *lane = _findLane(track, param);

    float lo, hi;
    _automationValueRange(param, lo, hi);
    float top = laneTop + kAutomationPadding;
    float bottom = laneTop + kAutomationLaneHeight - kAutomationPadding;

    if (lane) {
      for (size_t i = 0; i < lane->points.size(); i++) {
        float px = (float)(lane->points[i].beat * kPxPerBeat);
        float py =
            _automationValueToY(lane->points[i].value, lo, hi, top, bottom);
        float dx = x - px, dy = y - py;
        if (dx * dx + dy * dy <= kHandleHitRadius * kHandleHitRadius) {
          _draggingAutoTrack = ti;
          _draggingAutoParam = param;
          _draggingAutoPointIndex = (int)i;
          _draggingAutoMarkedDelete = false;
          if (onAutomationPointDragStart)
            onAutomationPointDragStart(ti, param, (int)i);
          return;
        }
      }
    }

    // Empty space in the lane — place a new point here. Beat is NOT
    // rounded (unlike clip placement) so a curve can be shaped with
    // sub-beat precision.
    double beat = std::max(0.0, (double)(x / kPxPerBeat));
    float value =
        std::max(lo, std::min(hi, _automationYToValue(y, lo, hi, top, bottom)));
    if (onAutomationPointAdd)
      onAutomationPointAdd(ti, param, beat, value);
  }

  // Whole-file waveform cache, keyed by file path. Deliberately separate
  // from TimelineScheduler::_audioSampleCache — different owner,
  // different lifetime (this only needs to live as long as the canvas
  // keeps redrawing, not as long as playback does) — even though it
  // means the same file can end up decoded twice into the engine's
  // sample bank if both caches want it. Not worth threading a shared
  // cache across the scheduler/surface boundary just for a read-only
  // peak lookup.
  //
  // Stores the FULL file's shape at kSourceWaveformBuckets resolution,
  // never the cropped/trimmed view — cropping depends on each clip's
  // live audioStartOffsetSec/lengthBeats (which change mid-drag), so it
  // has to be recomputed per clip per render, not cached per file.
  struct CachedWaveform {
    std::vector<float> peaks; // kSourceWaveformBuckets buckets across
                              // the file's full duration
    float durationSec = 0.f;
  };
  std::unordered_map<std::string, CachedWaveform> _waveformCache;

  const CachedWaveform &_fullWaveformForFile(const std::string &path) {
    auto it = _waveformCache.find(path);
    if (it != _waveformCache.end())
      return it->second;

    auto &engine = AudioEngine::get();
    SampleID id = engine.loadSample(path);
    CachedWaveform wf;
    if (id != kInvalidSample) {
      wf.peaks = engine.getSamplePeaks(id, kSourceWaveformBuckets);
      wf.durationSec = engine.getSampleDurationSeconds(id);
    }
    // Cache even on failure (empty peaks, 0 duration) so a missing/
    // undecodable file doesn't retry a doomed loadSample() call on
    // every redraw.
    return _waveformCache.emplace(path, std::move(wf)).first->second;
  }

  // Crops the cached full-file waveform down to the span this clip
  // actually plays — [audioStartOffsetSec, audioStartOffsetSec +
  // lengthBeats-in-seconds] — then max-reduces that span down to at
  // most kWaveformBuckets output bars. Recomputed on every render()
  // call (not cached per clip) since trim/fade dragging changes the
  // inputs continuously; the source scan is at most
  // kSourceWaveformBuckets elements, cheap enough for the ~25ms
  // redraw cadence dragging already runs at (see needsContinuousRedraw).
  std::vector<float> _peaksForClip(const Clip &clip) {
    if (clip.audioFilePath.empty())
      return {};
    const CachedWaveform &wf = _fullWaveformForFile(clip.audioFilePath);
    if (wf.peaks.empty() || wf.durationSec <= 0.f)
      return wf.peaks; // duration unknown — fall back to the whole shape
                       // rather than showing nothing

    double bpm = (seq && seq->bpm > 0.0) ? seq->bpm : 120.0;
    double lengthSec = clip.lengthBeats * (60.0 / bpm);
    double startSec = std::max(0.0, clip.audioStartOffsetSec);
    double endSec = std::min((double)wf.durationSec, startSec + lengthSec);
    if (endSec <= startSec)
      return {};
    size_t srcCount = wf.peaks.size();
    size_t startIdx = (size_t)std::min<double>(
        (double)srcCount, (startSec / wf.durationSec) * srcCount);
    size_t endIdx = (size_t)std::min<double>(
        (double)srcCount, (endSec / wf.durationSec) * srcCount);
    if (endIdx <= startIdx)
      endIdx = std::min(srcCount, startIdx + 1);
    size_t croppedCount = endIdx - startIdx;

    size_t outBucketsSz =
        std::max<size_t>(1, std::min((size_t)kWaveformBuckets, croppedCount));
    int outBuckets = (int)outBucketsSz;
    std::vector<float> out(outBuckets, 0.f);
    for (int i = 0; i < outBuckets; i++) {
      size_t s = startIdx + (size_t)((double)i * croppedCount / outBuckets);
      size_t e =
          startIdx + (size_t)((double)(i + 1) * croppedCount / outBuckets);
      e = std::max(e, s + 1);
      e = std::min(e, endIdx);
      float peak = 0.f;
      for (size_t j = s; j < e; j++)
        peak = std::max(peak, wf.peaks[j]);
      out[i] = peak;
    }
    return out;
  }

  static bool _withinHandle(float x, float y, float hx, float hy) {
    float dx = x - hx, dy = y - hy;
    return (dx * dx + dy * dy) <= (kHandleHitRadius * kHandleHitRadius);
  }

  Clip *_findClip(ClipID id) {
    if (!timeline)
      return nullptr;
    for (auto &track : timeline->tracks)
      for (auto &c : track.clips)
        if (c.id == id)
          return &c;
    return nullptr;
  }
};

// ============================================================================
// UI
// ============================================================================

class SequencerApp : public Widget {
  std::shared_ptr<StepScheduler> _seq;
  TimerID _timerId = 0;

  State<int> _bpmState{120};
  State<int> _currentStepState{-1}; // -1 = not playing / nothing highlighted
  State<bool> _songModeState{false};

  // Single fixed 4x16 grid of cell states, reused across patterns — refreshed
  // in place from patternSlots[editingSlot] whenever the edited pattern
  // changes, rather than building a new grid per pattern (build() only runs
  // once, so the widget tree can't grow new grids on demand).
  std::vector<std::vector<State<bool>>> _cellState;
  std::vector<std::vector<State<double>>> _velocitySliderState;
  std::vector<std::vector<State<double>>> _pitchSliderState;
  std::vector<std::vector<State<double>>> _probabilitySliderState;
  std::vector<std::vector<State<double>>> _microTimingSliderState;

  // Display name for whatever instrument is loaded on each track.
  std::vector<State<std::string>> _instrumentNameState;

  // Per-track mixer UI state, mirrors _seq->trackMuted/trackSoloed and
  // trackVoice[t].gain/pan the same way _cellState mirrors step data.
  std::vector<State<bool>> _muteState;
  std::vector<State<bool>> _soloState;
  std::vector<State<double>> _trackVolumeState;
  std::vector<State<double>> _trackPanState;

  // Synth voice UI state — mirrors trackVoice[t]'s waveform/ADSR fields
  // the same way _trackVolumeState/_trackPanState mirror gain/pan.
  // _waveformState holds an index into kWaveformNames, not the enum
  // itself, since Dropdown works in terms of option indices.
  std::vector<State<int>> _waveformState;
  std::vector<State<double>> _attackState;
  std::vector<State<double>> _decayState;
  std::vector<State<double>> _sustainLevelState;
  std::vector<State<double>> _sustainHoldState;
  std::vector<State<double>> _releaseState;

  // Pattern selector. NOTE: assumes Dropdown() returns a shared_ptr to a
  // concrete widget type exposing setOptions()/setSelectedIndex() — matches
  // the chained-method style used throughout this framework, but double
  // check the exact pointer typedef name against the real FluxUI headers.
  std::shared_ptr<DropdownWidget> _patternDropdown;
  State<int> _dropdownIndexState{0};

  // Song arrangement, mirrored into a State<vector> purely so Map() can
  // reactively rebuild the chip row. _seq->arrangement (plain vector, no
  // State) stays the actual playback source of truth; every mutation here
  // touches both in lockstep so we never need to read a State's value back
  // out — see _addToArrangement/_removeArrangementEntryById.
  State<std::vector<ArrangementEntry>> _arrangementState{
      std::vector<ArrangementEntry>{}};
  uint64_t _nextArrangementEntryId = 1;

  // Tracks which pattern the grid is currently showing, mirrored into a
  // State<int> purely so cell-color bindings recompute (grayed-out /
  // inert columns beyond the pattern's length) whenever it changes — same
  // mechanism as _currentStepState driving the playhead highlight.
  State<int> _patternLengthState{16};
  State<int> _patternSpbState{4};
  State<double> _patternSwingState{0.0};

  // Full file path per track's loaded sample, so Save Project can write it
  // back out. _instrumentNameState only keeps the display name (basename),
  // which isn't enough to reload from.
  std::vector<std::string> _trackSamplePath;

  // Generic undo/redo. See _undoStack's class comment for scope — it
  // covers discrete edits (step toggles, pattern/arrangement add-remove,
  // length/BPM changes) but deliberately not continuous slider drags
  // (velocity/pitch/probability/volume/pan), since Slider's API only
  // exposes onValueChanged with no drag-end hook to coalesce into one
  // undo entry — wiring it in as-is would flood the stack with one entry
  // per pixel of drag.
  class UndoStack {
  public:
    using Fn = std::function<void()>;

    void push(Fn undo, Fn redo) {
      _redoStack.clear(); // a fresh action invalidates old redo history
      _undoStack.push_back({std::move(undo), std::move(redo)});
      if (_undoStack.size() > kMaxDepth)
        _undoStack.erase(_undoStack.begin());
    }
    void undo() {
      if (_undoStack.empty())
        return;
      Entry e = std::move(_undoStack.back());
      _undoStack.pop_back();
      e.undo();
      _redoStack.push_back(std::move(e));
    }
    void redo() {
      if (_redoStack.empty())
        return;
      Entry e = std::move(_redoStack.back());
      _redoStack.pop_back();
      e.redo();
      _undoStack.push_back(std::move(e));
    }
    void clear() {
      _undoStack.clear();
      _redoStack.clear();
    }

  private:
    struct Entry {
      Fn undo, redo;
    };
    static constexpr size_t kMaxDepth = 200;
    std::vector<Entry> _undoStack;
    std::vector<Entry> _redoStack;
  };
  UndoStack _undoStack;

  // Single-step clipboard for the context menu's Copy/Paste buttons.
  // Deliberately NOT part of project save/load — this is transient
  // editing state, same as an OS clipboard, not project data. Empty
  // (nullopt) until the user copies a step for the first time.
  std::optional<StepData> _stepClipboard;

  // ── Timeline (Phase 3) ────────────────────────────────────────────────
  Timeline _timeline;
  std::unique_ptr<TimelineScheduler> _timelineScheduler; // built in ctor,
                                                         // after _seq exists
  uint64_t _nextClipId = 1;
  TimelineTrackID _nextTimelineTrackId = 1;
  ClipID _selectedClipId = 0;

  std::shared_ptr<CanvasWidget> _timelineCanvas;
  std::shared_ptr<TimelineSurface> _timelineSurface;

  // ── Mixer (Phase 4) ──────────────────────────────────────────────────
  // Parallel, index-matched with _timeline.tracks — same convention
  // _cellState/_velocitySliderState use for the step grid. Built
  // incrementally in _addTimelineTrack (never truncated — tracks aren't
  // removable yet, matching the existing timeline model) and fully
  // rebuilt by _rebuildMixerStrips() after a project load, since load
  // replaces _timeline.tracks wholesale.
  std::vector<State<double>> _timelineVolumeState;
  std::vector<State<double>> _timelinePanState;
  std::vector<State<bool>> _timelineMuteState;
  std::vector<State<bool>> _timelineSoloState;
  std::vector<State<double>> _timelinePeakState; // 0..~1.5, polled each tick
  std::vector<State<bool>> _timelineAutoLaneVisibleState; // per-track
                                                          // "show
                                                          // automation
                                                          // lane" toggle
  std::vector<std::shared_ptr<DropdownWidget>> _sendDropdowns;
  std::shared_ptr<Widget> _mixerStripsRow; // built once, children appended

  // Mirrors each channel strip's send-dropdown selection so it can be
  // programmatically restored on project load (Dropdown needs a bound
  // State to move selection from code, same as _dropdownIndexState
  // does for the pattern dropdown).
  std::vector<State<int>> _timelineSendIndexState;

  // ── Aux bus RETURN strips (Phase 4 completion) ──────────────────────
  std::vector<State<double>> _auxBusVolumeState;
  std::vector<State<double>> _auxBusPeakState;
  std::shared_ptr<Widget> _auxBusStripsRow;

  // ── Insert-effect UI state ─────────────────────────────────
  // One filter slot (engine insert slot 0) exposed per strip for now;
  // kMaxInserts leaves room for more slots' worth of UI later with no
  // engine-side changes. Index-parallel with _timeline.tracks / _auxBuses,
  // same convention every other per-strip vector in this file uses.
  static constexpr uint32_t kFilterInsertSlot = 0;
  static constexpr uint32_t kCompressorInsertSlot = 1;

  std::vector<State<bool>> _timelineFilterEnabledState;
  std::vector<State<int>> _timelineFilterTypeState;
  std::vector<State<double>> _timelineFilterCutoffState;
  std::vector<State<double>> _timelineFilterQState;

  std::vector<State<bool>> _auxFilterEnabledState;
  std::vector<State<int>> _auxFilterTypeState;
  std::vector<State<double>> _auxFilterCutoffState;
  std::vector<State<double>> _auxFilterQState;


  std::vector<State<bool>> _timelineCompEnabledState;
  std::vector<State<double>> _timelineCompThresholdState;
  std::vector<State<double>> _timelineCompRatioState;
  std::vector<State<double>> _timelineCompAttackState;
  std::vector<State<double>> _timelineCompReleaseState;
  std::vector<State<double>> _timelineCompMakeupState;

  std::vector<State<bool>> _auxCompEnabledState;
  std::vector<State<double>> _auxCompThresholdState;
  std::vector<State<double>> _auxCompRatioState;
  std::vector<State<double>> _auxCompAttackState;
  std::vector<State<double>> _auxCompReleaseState;
  std::vector<State<double>> _auxCompMakeupState;



  std::vector<BusID> _auxBuses;
  std::vector<std::string> _auxBusNames;

  // Shown next to the timeline so the user can see/delete the current
  // selection without a full side panel widget. Text() binds to this.
  State<std::string> _timelineSelectionLabel{"No clip selected"};

  // Set by the "Load Audio…" FilePicker's onChanged; the NEXT empty-space
  // click on the timeline consumes it and places an AudioClip there
  // instead of the default PatternClip, then clears it. Reuses
  // TimelineSurface::onEmptyClick's existing (trackIndex, beat) callback
  // rather than adding a second click-handling path.
  std::string _pendingAudioClipPath;

  // Fade-handle drag scratch state — the value captured at
  // onFadeDragStart, so onFadeDragEnd can push a single before/after undo
  // entry for the whole drag instead of one per onFadeDrag call. Not a
  // State<> since it's never bound to a widget, just plain scratch data
  // between the three callbacks.
  float _fadeDragOldValue = 0.f;

  // Trim-drag scratch state — snapshot of the three fields trimming can
  // touch together (startBeat/lengthBeats/audioStartOffsetSec), captured
  // at drag start so onTrimDragEnd can push ONE undo entry for the whole
  // drag, same coalescing reasoning as _fadeDragOldValue above. Also
  // caches the source sample's real duration for clamping — looked up
  // once at drag start rather than on every mouse-move.
  double _trimDragOldStartBeat = 0.0;
  double _trimDragOldLengthBeats = 0.0;
  double _trimDragOldAudioOffsetSec = 0.0;
  float _trimDragSampleDurationSec = 0.f;

  // ── Audio recording (Phase 3) ────────────────────────────────────────
  bool _isRecording = false;
  State<bool> _recordingState{false}; // drives the Record button's color

  // Mono float samples accumulated by the capture callback. Written ONLY
  // by AudioEngine's capture thread while recording is active, and read
  // back on the UI thread only after stopCapture() has joined that
  // thread (see _stopRecording) — that ordering is what makes this safe
  // without a mutex, same discipline the engine's own capture doc
  // describes for CaptureCallback.
  std::vector<float> _recordBuffer;
  int _recordTrackIndex = -1; // which _timeline.tracks[] slot the
                              // in-progress take will land on
  double _recordStartBeat = 0.0;
  uint32_t _recordSampleRate = 48000;
  uint64_t _nextRecordingIndex = 1; // suffixes generated .wav filenames

  int _recordTargetTrackIndex = 0; // UI selection from the dropdown below,
                                   // read at record-start time
  std::shared_ptr<DropdownWidget> _recordTrackDropdown;

public:
  SequencerApp() {
    AudioEngine::get().init();

    _seq = std::make_shared<StepScheduler>(4);
    _timelineScheduler = std::make_unique<TimelineScheduler>(*_seq, _timeline);
    _seq->trackVoice = {
        {110.f, 1.0f, -0.6f}, // low tom-ish
        {220.f, 0.9f, -0.2f},
        {440.f, 0.8f, 0.2f},
        {880.f, 0.6f, 0.6f}, // hi-hat-ish
    };

    _instrumentNameState.reserve(_seq->trackVoice.size());
    static const char *kDefaultNames[] = {"Low Tom (sine)", "Mid Tom (sine)",
                                          "Clap (sine)", "Hi-Hat (sine)"};
    for (size_t t = 0; t < _seq->trackVoice.size(); t++)
      _instrumentNameState.emplace_back(
          t < 4 ? kDefaultNames[t] : "Track " + std::to_string(t) + " (sine)");

    _cellState.resize(_seq->trackVoice.size());
    _velocitySliderState.resize(_seq->trackVoice.size());
    _pitchSliderState.resize(_seq->trackVoice.size());
    _probabilitySliderState.resize(_seq->trackVoice.size());
    _microTimingSliderState.resize(_seq->trackVoice.size());
    _muteState.reserve(_seq->trackVoice.size());
    _soloState.reserve(_seq->trackVoice.size());
    _trackVolumeState.reserve(_seq->trackVoice.size());
    _trackPanState.reserve(_seq->trackVoice.size());
    _waveformState.reserve(_seq->trackVoice.size());
    _attackState.reserve(_seq->trackVoice.size());
    _decayState.reserve(_seq->trackVoice.size());
    _sustainLevelState.reserve(_seq->trackVoice.size());
    _sustainHoldState.reserve(_seq->trackVoice.size());
    _releaseState.reserve(_seq->trackVoice.size());
    for (size_t t = 0; t < _seq->trackVoice.size(); t++) {
      _cellState[t].reserve(StepScheduler::kMaxSteps);
      _velocitySliderState[t].reserve(StepScheduler::kMaxSteps);
      _pitchSliderState[t].reserve(StepScheduler::kMaxSteps);
      _microTimingSliderState[t].reserve(StepScheduler::kMaxSteps);
      _probabilitySliderState[t].reserve(StepScheduler::kMaxSteps);
      for (int s = 0; s < StepScheduler::kMaxSteps; s++) {
        _cellState[t].emplace_back(false);
        _velocitySliderState[t].emplace_back(1.0);
        _pitchSliderState[t].emplace_back(0.0);
        _probabilitySliderState[t].emplace_back(1.0);
        _microTimingSliderState[t].emplace_back(0.0);
      }
      // Seed mixer UI state from the track's actual initial gain/pan
      // (set just above via the trackVoice = {...} assignment) so the
      // sliders open already showing the real values instead of 0.
      _muteState.emplace_back(false);
      _soloState.emplace_back(false);
      _trackVolumeState.emplace_back((double)_seq->trackVoice[t].gain);
      _trackPanState.emplace_back((double)_seq->trackVoice[t].pan);
      _waveformState.emplace_back((int)_seq->trackVoice[t].waveform);
      _attackState.emplace_back((double)_seq->trackVoice[t].attackSec);
      _decayState.emplace_back((double)_seq->trackVoice[t].decaySec);
      _sustainLevelState.emplace_back((double)_seq->trackVoice[t].sustainLevel);
      _sustainHoldState.emplace_back((double)_seq->trackVoice[t].sustainSec);
      _releaseState.emplace_back((double)_seq->trackVoice[t].releaseSec);
    }

    // Seed the initial pattern (slot 0, created by StepScheduler's ctor)
    // with a basic four-on-the-floor + offbeat hat pattern.
    Pattern &initial = _seq->patternSlots[_seq->editingSlot];
    for (int s = 0; s < initial.numSteps; s += 4)
      initial.steps[0][s].on = true;
    for (int s = 2; s < initial.numSteps; s += 4)
      initial.steps[3][s].on = true;

    _refreshGridFromPattern(); // pull the seeded pattern into the bound cell
                               // states
  }

  ~SequencerApp() {
    if (_timerId)
      FluxUI::getCurrentInstance()->clearInterval(_timerId);

    // Don't leave the capture device running if the widget is destroyed
    // mid-recording.
    if (_isRecording)
      AudioEngine::get().stopCapture();

    // Release any samples this instance loaded onto tracks.
    for (auto &track : _seq->trackVoice)
      if (track.sampleId != kInvalidSample)
        AudioEngine::get().unloadSample(track.sampleId);
  }

  // Combined color logic for a single (track, step) cell in the CURRENTLY
  // EDITED pattern. Red playhead only shows when the pattern you're looking
  // at is also the one actually playing (editingSlot == playingSlot) — if
  // you're viewing a different pattern than what's sounding in song mode,
  // no highlight appears here; a known simplification rather than an
  // auto-follow feature.
  Color _colorForCell(size_t t, int s) const {

    const Pattern &editingPat = _seq->patternSlots[_seq->editingSlot];
    bool withinLength = s < editingPat.numSteps;

    bool viewingLivePattern = (_seq->editingSlot == _seq->playingSlot);
    bool isPlayhead = _seq->playing && viewingLivePattern && withinLength &&
                      (_seq->currentStep == s);
    if (isPlayhead)
      return Color::fromRGB(220, 50, 50);

    if (!withinLength)
      return Color::fromRGB(245, 245,
                            245); // beyond current pattern length — inert

    const StepData &step = editingPat.steps[t][s];
    if (!step.on)
      return Color::fromRGB(230, 230, 230);

    float v = std::max(0.f, std::min(1.f, step.velocity));
    auto lerp = [v](int onChan, int offChan) {
      return (int)(offChan + (onChan - offChan) * v);
    };
    return Color::fromRGB(lerp(99, 230), lerp(102, 230), lerp(241, 230));
  }

  // Pulls patternSlots[editingSlot]'s step data into the bound cell/slider
  // states, so the grid visually reflects whichever pattern is now being
  // edited. Call after any operation that changes editingSlot.
  void _refreshGridFromPattern() {
    const Pattern &pat = _seq->patternSlots[_seq->editingSlot];
    for (size_t t = 0; t < pat.steps.size(); t++) {
      for (int s = 0; s < StepScheduler::kMaxSteps; s++) {
        const StepData &step = pat.steps[t][s];
        _cellState[t][s].set(step.on);
        _velocitySliderState[t][s].set(step.velocity);
        _pitchSliderState[t][s].set((double)step.pitchSemitones);
        _probabilitySliderState[t][s].set((double)step.probability);
        _microTimingSliderState[t][s].set((double)step.microTiming);
      }
    }
    _patternLengthState.set(
        pat.numSteps); // triggers grid recolor (grayed-out inert columns)
    _patternSpbState.set(pat.stepsPerBeat);
    _patternSwingState.set((double)pat.swing);
  }

  void _loadTrackSample(size_t t, const std::string &path) {
    if (path.empty())
      return;

    SampleID newId = AudioEngine::get().loadSample(path);
    if (newId == kInvalidSample)
      return; // decode failed — leave the existing instrument in place

    SampleID old = _seq->trackVoice[t].sampleId;
    _seq->trackVoice[t].sampleId = newId;
    if (old != kInvalidSample)
      AudioEngine::get().unloadSample(old);

    if (t >= _trackSamplePath.size())
      _trackSamplePath.resize(t + 1);
    _trackSamplePath[t] = path;

    size_t slash = path.find_last_of("/\\");
    _instrumentNameState[t].set(
        slash == std::string::npos ? path : path.substr(slash + 1));
  }

  // Core creation logic with no undo-stack side effects — shared by the
  // initial user action and by redo().
  int _createPatternCore(const std::string &name) {
    int slot = _seq->addPattern(name);
    if (slot < 0)
      return -1; // fixed pool (kMaxPatterns) exhausted
    _seq->setEditingPattern(slot);
    _refreshGridFromPattern();
    _syncPatternDropdown();
    return slot;
  }

  void _createPattern() {
    int prevEditing = _seq->editingSlot;
    std::string name =
        "Pattern " + std::to_string(_seq->activeSlots.size() + 1);
    int newSlot = _createPatternCore(name);
    if (newSlot < 0)
      return;

    _undoStack.push(
        [this, newSlot, prevEditing] {
          _seq->deletePattern(newSlot);
          _seq->setEditingPattern(prevEditing);
          _refreshGridFromPattern();
          _syncPatternDropdown();
        },
        [this, name] { _createPatternCore(name); });
  }

  int _duplicatePatternCore(int srcSlot) {
    int dup = _seq->duplicatePattern(srcSlot);
    if (dup < 0)
      return -1;
    _seq->setEditingPattern(dup);
    _refreshGridFromPattern();
    _syncPatternDropdown();
    return dup;
  }

  void _duplicateCurrentPattern() {
    int srcSlot = _seq->editingSlot;
    int dup = _duplicatePatternCore(srcSlot);
    if (dup < 0)
      return;

    _undoStack.push(
        [this, dup, srcSlot] {
          _seq->deletePattern(dup);
          _seq->setEditingPattern(srcSlot);
          _refreshGridFromPattern();
          _syncPatternDropdown();
        },
        // Best-effort: if srcSlot has since been deleted/reused by other
        // edits between undo and redo, this silently duplicates whatever
        // now occupies that slot. Full fidelity would need a deep
        // snapshot of srcSlot at push-time; not worth the memory cost for
        // a redo of a duplicate.
        [this, srcSlot] { _duplicatePatternCore(srcSlot); });
  }

  void _deleteCurrentPattern() {
    int slot = _seq->editingSlot;
    if (_seq->activeSlots.size() <= 1)
      return; // StepScheduler refuses this too; bail before pushing a no-op
              // undo entry

    Pattern snapshot = _seq->patternSlots[slot]; // deep copy, incl. step data
    std::vector<ArrangementEntry> removedEntries;
    std::vector<size_t> removedPositions;
    for (size_t i = 0; i < _seq->arrangement.size(); i++)
      if (_seq->arrangement[i].patternSlot == slot) {
        removedEntries.push_back(_seq->arrangement[i]);
        removedPositions.push_back(i);
      }

    _seq->deletePattern(slot);
    _refreshGridFromPattern();
    _syncPatternDropdown();
    _rebuildArrangementStateFromScheduler();

    _undoStack.push(
        [this, slot, snapshot, removedEntries, removedPositions] {
          _seq->patternSlots[slot] = snapshot;
          if (std::find(_seq->activeSlots.begin(), _seq->activeSlots.end(),
                        slot) == _seq->activeSlots.end())
            _seq->activeSlots.push_back(slot);
          for (size_t i = 0; i < removedEntries.size(); i++) {
            size_t pos =
                std::min(removedPositions[i], _seq->arrangement.size());
            _seq->arrangement.insert(_seq->arrangement.begin() + pos,
                                     removedEntries[i]);
          }
          _seq->setEditingPattern(slot);
          _refreshGridFromPattern();
          _syncPatternDropdown();
          _rebuildArrangementStateFromScheduler();
        },
        [this, slot] {
          _seq->deletePattern(slot);
          _refreshGridFromPattern();
          _syncPatternDropdown();
          _rebuildArrangementStateFromScheduler();
        });
  }

  void _rebuildArrangementStateFromScheduler() {
    _arrangementState.set(_seq->arrangement);
  }

  void _refreshSynthStateFromTrack(size_t t) {
    _waveformState[t].set((int)_seq->trackVoice[t].waveform);
    _attackState[t].set((double)_seq->trackVoice[t].attackSec);
    _decayState[t].set((double)_seq->trackVoice[t].decaySec);
    _sustainLevelState[t].set((double)_seq->trackVoice[t].sustainLevel);
    _sustainHoldState[t].set((double)_seq->trackVoice[t].sustainSec);
    _releaseState[t].set((double)_seq->trackVoice[t].releaseSec);
  }

  void _syncPatternDropdown() {
    std::vector<std::string> labels;
    for (int slot : _seq->activeSlots)
      labels.push_back(_seq->patternSlots[slot].name);
    _patternDropdown->setOptions(labels);

    int idx = 0;
    for (size_t i = 0; i < _seq->activeSlots.size(); i++)
      if (_seq->activeSlots[i] == _seq->editingSlot) {
        idx = (int)i;
        break;
      }
    _dropdownIndexState.set(idx);
  }

  void _addToArrangement(int patternSlot) {
    ArrangementEntry e{_nextArrangementEntryId++, patternSlot};
    _seq->arrangement.push_back(e);

    _arrangementState.push_back(e);

    _undoStack.push([this, id = e.id] { _removeArrangementEntryByIdCore(id); },
                    [this, e] {
                      _seq->arrangement.push_back(e);
                      _arrangementState.push_back(e);
                    });
  }

  void _removeArrangementEntryByIdCore(uint64_t id) {
    for (size_t i = 0; i < _seq->arrangement.size(); i++) {
      if (_seq->arrangement[i].id == id) {
        _seq->arrangement.erase(_seq->arrangement.begin() + i);

        _arrangementState.erase(i);

        break;
      }
    }
  }

  void _removeArrangementEntryById(uint64_t id) {
    ArrangementEntry removed{};
    size_t pos = _seq->arrangement.size();
    for (size_t i = 0; i < _seq->arrangement.size(); i++)
      if (_seq->arrangement[i].id == id) {
        removed = _seq->arrangement[i];
        pos = i;
        break;
      }
    if (pos == _seq->arrangement.size())
      return; // not found

    _removeArrangementEntryByIdCore(id);

    _undoStack.push(
        [this, removed, pos] {
          size_t insertAt = std::min(pos, _seq->arrangement.size());
          _seq->arrangement.insert(_seq->arrangement.begin() + insertAt,
                                   removed);
          _rebuildArrangementStateFromScheduler();
        },
        [this, id] { _removeArrangementEntryByIdCore(id); });
  }

  void _adjustArrangementRepeat(uint64_t id, int delta) {
    int oldCount = 1, newCount = 1;
    for (auto &e : _seq->arrangement)
      if (e.id == id) {
        oldCount = e.repeatCount;
        e.repeatCount = std::max(1, e.repeatCount + delta);
        newCount = e.repeatCount;
        break;
      }
    if (oldCount == newCount)
      return; // clamped at the floor — nothing actually changed

    _arrangementState.set(_seq->arrangement);

    _undoStack.push(
        [this, id, oldCount] { _setArrangementRepeatCore(id, oldCount); },
        [this, id, newCount] { _setArrangementRepeatCore(id, newCount); });
  }

  void _copyStep(size_t t, int s) {
    const Pattern &pat = _seq->patternSlots[_seq->editingSlot];
    _stepClipboard = pat.steps[t][s]; // copies on/velocity/pitch/probability/
                                      // microTiming as a single snapshot
  }

  void _pasteStep(size_t t, int s) {
    if (!_stepClipboard.has_value())
      return; // nothing copied yet

    int slot = _seq->editingSlot;
    StepData oldVal = _seq->patternSlots[slot].steps[t][s];
    StepData newVal = *_stepClipboard;
    if (oldVal.on == newVal.on && oldVal.velocity == newVal.velocity &&
        oldVal.pitchSemitones == newVal.pitchSemitones &&
        oldVal.probability == newVal.probability &&
        oldVal.microTiming == newVal.microTiming)
      return; // pasting an identical step onto itself — no-op, no undo entry

    _seq->patternSlots[slot].steps[t][s] = newVal;
    _applyStepToUI(t, s, newVal);

    _undoStack.push(
        [this, slot, t, s, oldVal] {
          _seq->patternSlots[slot].steps[t][s] = oldVal;
          if (_seq->editingSlot == slot)
            _applyStepToUI(t, s, oldVal);
        },
        [this, slot, t, s, newVal] {
          _seq->patternSlots[slot].steps[t][s] = newVal;
          if (_seq->editingSlot == slot)
            _applyStepToUI(t, s, newVal);
        });
  }

  // Pushes a StepData's fields into every bound UI slider/cell for (t, s)
  // in one place — factored out of _pasteStep's undo/redo lambdas so a
  // paste updates all five sliders (on, velocity, pitch, probability,
  // microTiming) the same way _refreshGridFromPattern does for a whole
  // pattern switch, just for a single cell.
  void _applyStepToUI(size_t t, int s, const StepData &step) {
    _cellState[t][s].set(step.on);
    _velocitySliderState[t][s].set((double)step.velocity);
    _pitchSliderState[t][s].set((double)step.pitchSemitones);
    _probabilitySliderState[t][s].set((double)step.probability);
    _microTimingSliderState[t][s].set((double)step.microTiming);
  }
  int _timelineCanvasWidthPx() const {
    return (int)(64 * TimelineSurface::kPxPerBeat); // 16-bar initial extent;
                                                    // grows are a follow-up
  }
  int _timelineCanvasHeightPx() const {
    int total = 0;
    for (auto &t : _timeline.tracks) {
      total += (int)TimelineSurface::kTrackHeight;
      if (t.automationVisible)
        total += (int)TimelineSurface::kAutomationLaneHeight;
    }
    return std::max((int)TimelineSurface::kTrackHeight, total);
  }

  void _addTimelineTrack() {
    TimelineTrack tt;
    tt.id = _nextTimelineTrackId++;
    tt.name = "Track " + std::to_string(_timeline.tracks.size() + 1);
    tt.engineTrack = AudioEngine::get().createTrack(); // reserved for
                                                       // future per-clip
                                                       // routing — see
                                                       // TimelineScheduler
    _timeline.tracks.push_back(std::move(tt));

    if (_timelineCanvas)
      _timelineCanvas->setCanvasSize(_timelineCanvasWidthPx(),
                                     _timelineCanvasHeightPx());
    if (_timelineCanvas)
      _timelineCanvas->redraw();
    _syncRecordTrackDropdown();
    _appendChannelStrip(_timeline.tracks.size() - 1);
  }

  std::vector<std::string> _sendBusLabels() const {
    std::vector<std::string> labels = {"Master"};
    labels.insert(labels.end(), _auxBusNames.begin(), _auxBusNames.end());
    return labels;
  }

  // Builds one channel strip for _timeline.tracks[idx] and appends it —
  // never rebuilds existing strips, so per-track slider drag state
  // (untracked by undo, same reasoning as the step-grid's velocity
  // sliders) survives adding more tracks.
  void _appendChannelStrip(size_t idx) {
    TimelineTrack &tt = _timeline.tracks[idx];

    _timelineVolumeState.emplace_back(1.0);
    _timelinePanState.emplace_back(0.0);
    _timelineMuteState.emplace_back(false);
    _timelineSoloState.emplace_back(false);
    _timelinePeakState.emplace_back(0.0);
    _timelineAutoLaneVisibleState.emplace_back(false);
    _timelineSendIndexState.emplace_back(0);

    _timelineFilterEnabledState.emplace_back(false);
    _timelineFilterTypeState.emplace_back(0);
    _timelineFilterCutoffState.emplace_back(1000.0);
    _timelineFilterQState.emplace_back(0.707);

    _timelineCompEnabledState.emplace_back(false);
    _timelineCompThresholdState.emplace_back(-18.0);
    _timelineCompRatioState.emplace_back(4.0);
    _timelineCompAttackState.emplace_back(10.0);
    _timelineCompReleaseState.emplace_back(100.0);
    _timelineCompMakeupState.emplace_back(0.0);

    auto compEnabledToggle =
        Toggle("Comp")
            ->setValue(_timelineCompEnabledState[idx])
            ->setOnToggleChanged([this, idx](bool v) {
              _updateTrackCompressor(idx, &v, nullptr, nullptr, nullptr,
                                     nullptr, nullptr);
            });

    auto compThresholdSlider =
        Slider(-60.0, 0.0, 1.0)
            ->setValue(_timelineCompThresholdState[idx])
            ->setWidth(80)
            ->setOnValueChanged([this, idx](double v) {
              _timelineCompThresholdState[idx].set(v);
              float f = (float)v;
              _updateTrackCompressor(idx, nullptr, &f, nullptr, nullptr,
                                     nullptr, nullptr);
            });

    auto compRatioSlider =
        Slider(1.0, 20.0, 0.5)
            ->setValue(_timelineCompRatioState[idx])
            ->setWidth(80)
            ->setOnValueChanged([this, idx](double v) {
              _timelineCompRatioState[idx].set(v);
              float f = (float)v;
              _updateTrackCompressor(idx, nullptr, nullptr, &f, nullptr,
                                     nullptr, nullptr);
            });

    auto compAttackSlider =
        Slider(0.1, 200.0, 0.5)
            ->setValue(_timelineCompAttackState[idx])
            ->setWidth(80)
            ->setOnValueChanged([this, idx](double v) {
              _timelineCompAttackState[idx].set(v);
              float f = (float)v;
              _updateTrackCompressor(idx, nullptr, nullptr, nullptr, &f,
                                     nullptr, nullptr);
            });

    auto compReleaseSlider =
        Slider(10.0, 1000.0, 5.0)
            ->setValue(_timelineCompReleaseState[idx])
            ->setWidth(80)
            ->setOnValueChanged([this, idx](double v) {
              _timelineCompReleaseState[idx].set(v);
              float f = (float)v;
              _updateTrackCompressor(idx, nullptr, nullptr, nullptr, nullptr,
                                     &f, nullptr);
            });

    auto compMakeupSlider =
        Slider(0.0, 24.0, 0.5)
            ->setValue(_timelineCompMakeupState[idx])
            ->setWidth(80)
            ->setOnValueChanged([this, idx](double v) {
              _timelineCompMakeupState[idx].set(v);
              float f = (float)v;
              _updateTrackCompressor(idx, nullptr, nullptr, nullptr, nullptr,
                                     nullptr, &f);
            });


    auto filterEnabledToggle =
        Toggle("Filter")
            ->setValue(_timelineFilterEnabledState[idx])
            ->setOnToggleChanged([this, idx](bool v) {
              _updateTrackFilter(idx, &v, nullptr, nullptr, nullptr);
            });

    auto filterTypeDropdown =
        Dropdown({"LP", "HP", "BP", "Notch"})
            ->setSelectedIndex(_timelineFilterTypeState[idx])
            ->setWidth(80)
            ->setOnSelectionChanged([this, idx](int optIdx,
                                                const std::string &) {
              _timelineFilterTypeState[idx].set(optIdx);
              FilterType t = (FilterType)optIdx;
              _updateTrackFilter(idx, nullptr, &t, nullptr, nullptr);
            });

    // Linear 20Hz-20kHz for now — a log-scaled slider would feel more
    // natural but this UI framework's Slider is linear-only; a proper
    // log-taper control is a follow-up, not a blocker for the filter
    // being usable.
    auto filterCutoffSlider =
        Slider(20.0, 20000.0, 10.0)
            ->setValue(_timelineFilterCutoffState[idx])
            ->setWidth(80)
            ->setOnValueChanged([this, idx](double v) {
              _timelineFilterCutoffState[idx].set(v);
              float c = (float)v;
              _updateTrackFilter(idx, nullptr, nullptr, &c, nullptr);
            });

    auto filterQSlider =
        Slider(0.1, 10.0, 0.1)
            ->setValue(_timelineFilterQState[idx])
            ->setWidth(80)
            ->setOnValueChanged([this, idx](double v) {
              _timelineFilterQState[idx].set(v);
              float q = (float)v;
              _updateTrackFilter(idx, nullptr, nullptr, nullptr, &q);
            });


    auto sendDropdown =
        Dropdown(_sendBusLabels())
        ->setSelectedIndex(_timelineSendIndexState[idx])
            ->setOnSelectionChanged([this, idx](int optIdx,
                                                const std::string &) {
                                                  _timelineSendIndexState[idx].set(optIdx);
              BusID bus = (optIdx <= 0 || optIdx - 1 >= (int)_auxBuses.size())
                              ? kMasterBus
                              : _auxBuses[optIdx - 1];
              AudioEngine::get().setTrackSendBus(
                  _timeline.tracks[idx].engineTrack, bus);
            })
            ->setWidth(90);
    _sendDropdowns.push_back(sendDropdown);

    auto meter = Box({})
                     ->setWidth(14)
                     ->setHeight(56)
                     ->setBorderRadius(2)
                     ->setBackgroundColor(Color::fromRGB(90, 220, 60));

    // BoxWidget has no State-bound setBackgroundColor overload (that's a
    // ButtonWidget convenience) — bind directly via State::bindProperty
    // instead, same mechanism, works on any Widget.
    _timelinePeakState[idx].bindProperty(meter, [](Widget *w, const double &v) {
      // Green -> amber as level approaches/exceeds 1.0 — cheap
      // two-stop gradient, good enough for a first pass; a proper
      // multi-segment LED-bar meter is a follow-up.
      int g = (int)std::min(220.0, 90 + 160 * v);
      int r = (int)std::min(230.0, 40 + 200 * std::max(0.0, v - 0.7));
      w->backgroundColor = Color::fromRGB(r, g, 60);
      w->hasBackground = true;
    });

    auto autoParamDropdown =
        Dropdown({"Volume", "Pan"})
            ->setWidth(80)
            ->setOnSelectionChanged(
                [this, idx](int optIdx, const std::string &) {
                  _timeline.tracks[idx].automationDisplayParam =
                      (optIdx == 1) ? AutomationParam::TrackPan
                                    : AutomationParam::TrackVolume;
                  if (_timelineCanvas)
                    _timelineCanvas->redraw();
                });

    auto autoLaneToggle =
        Toggle("Lane")
            ->setValue(_timelineAutoLaneVisibleState[idx])
            ->setOnToggleChanged([this, idx](bool v) {
              _timeline.tracks[idx].automationVisible = v;
              // Lane visibility changes each track's total height, so the
              // canvas has to be resized, same as adding/removing a track.
              if (_timelineCanvas) {
                _timelineCanvas->setCanvasSize(_timelineCanvasWidthPx(),
                                               _timelineCanvasHeightPx());
                _timelineCanvas->redraw();
              }
            });

    auto strip =
        Column({
                   Text(tt.name)->setFontSize(11)->setWidth(80),
                   meter,
                   Slider(0.0, 1.5, 0.01)
                       ->setValue(_timelineVolumeState[idx])
                       ->setWidth(80)
                       ->setOnValueChanged([this, idx](double v) {
                         AudioEngine::get().setTrackGain(
                             _timeline.tracks[idx].engineTrack, (float)v);
                       }),
                   Slider(-1.0, 1.0, 0.1)
                       ->setValue(_timelinePanState[idx])
                       ->setWidth(80)
                       ->setOnValueChanged([this, idx](double v) {
                         AudioEngine::get().setTrackPan(
                             _timeline.tracks[idx].engineTrack, (float)v);
                       }),
                   Row({
                           Toggle("M")
                               ->setValue(_timelineMuteState[idx])
                               ->setOnToggleChanged([this, idx](bool v) {
                                 // Gates clip firing at the SCHEDULER level
                                 // (TimelineScheduler::tick already reads
                                 // track.muted/soloed) — deliberately not
                                 // also calling AudioEngine::setTrackMute,
                                 // so there's exactly one source of truth
                                 // for "is this track audible" instead of
                                 // two that could disagree.
                                 _timeline.tracks[idx].muted = v;
                               }),
                           Toggle("S")
                               ->setValue(_timelineSoloState[idx])
                               ->setOnToggleChanged([this, idx](bool v) {
                                 _timeline.tracks[idx].soloed = v;
                               }),
                       })
                       ->setGap(4),
                   Text("Send")->setFontSize(10),
                   sendDropdown,
                   Row({autoParamDropdown, autoLaneToggle})->setGap(4),
                   Text("Insert")->setFontSize(10),
                   filterEnabledToggle,
                   filterTypeDropdown,
                   filterCutoffSlider,
                   filterQSlider,
                   compEnabledToggle,
                   compThresholdSlider,
                   compRatioSlider,
                   compAttackSlider,
                   compReleaseSlider,
                   compMakeupSlider,
               })
            ->setGap(4)
            ->setWidth(90)
            ->setPadding(6)
            ->setBackgroundColor(Color::fromRGB(238, 238, 244))
            ->setBorderRadius(6);

    _mixerStripsRow->addChild(strip);
  }

  // Full rebuild after a project load replaces _timeline.tracks wholesale
  // — same "clear and re-append" shape _loadProject already uses for the
  // timeline canvas itself.
  void _rebuildMixerStrips() {
    _timelineVolumeState.clear();
    _timelinePanState.clear();
    _timelineMuteState.clear();
    _timelineSoloState.clear();
    _timelinePeakState.clear();
    _timelineAutoLaneVisibleState.clear();
    _timelineSendIndexState.clear();
    _timelineFilterEnabledState.clear();
    _timelineFilterTypeState.clear();
    _timelineFilterCutoffState.clear();
    _timelineFilterQState.clear();
    _timelineCompEnabledState.clear();
    _timelineCompThresholdState.clear();
    _timelineCompRatioState.clear();
    _timelineCompAttackState.clear();
    _timelineCompReleaseState.clear();
    _timelineCompMakeupState.clear();
    _sendDropdowns.clear();
    if (_mixerStripsRow)
      _mixerStripsRow->children.clear();
    for (size_t i = 0; i < _timeline.tracks.size(); i++)
      _appendChannelStrip(i);
    if (_mixerStripsRow)
      _mixerStripsRow->markNeedsLayout();
  }


  // Bus RETURN channel strip — name, meter, and a volume fader feeding
  // AudioEngine::setBusGain(). Aux buses in this model always send to
  // master, so unlike a track strip there's no send dropdown here.
  void _appendAuxBusStrip(size_t idx) {
     _auxBusVolumeState.emplace_back(1.0);
     _auxBusPeakState.emplace_back(0.0);
    

    _auxFilterEnabledState.emplace_back(false);
    _auxFilterTypeState.emplace_back(0);
    _auxFilterCutoffState.emplace_back(1000.0);
    _auxFilterQState.emplace_back(0.707);

    _auxCompEnabledState.emplace_back(false);
    _auxCompThresholdState.emplace_back(-18.0);
    _auxCompRatioState.emplace_back(4.0);
    _auxCompAttackState.emplace_back(10.0);
    _auxCompReleaseState.emplace_back(100.0);
    _auxCompMakeupState.emplace_back(0.0);

    auto compEnabledToggle =
        Toggle("Comp")
            ->setValue(_auxCompEnabledState[idx])
            ->setOnToggleChanged([this, idx](bool v) {
              _updateAuxCompressor(idx, &v, nullptr, nullptr, nullptr,
                                   nullptr, nullptr);
            });

    auto compThresholdSlider =
        Slider(-60.0, 0.0, 1.0)
            ->setValue(_auxCompThresholdState[idx])
            ->setWidth(80)
            ->setOnValueChanged([this, idx](double v) {
              _auxCompThresholdState[idx].set(v);
              float f = (float)v;
              _updateAuxCompressor(idx, nullptr, &f, nullptr, nullptr,
                                   nullptr, nullptr);
            });

    auto compRatioSlider =
        Slider(1.0, 20.0, 0.5)
            ->setValue(_auxCompRatioState[idx])
            ->setWidth(80)
            ->setOnValueChanged([this, idx](double v) {
              _auxCompRatioState[idx].set(v);
              float f = (float)v;
              _updateAuxCompressor(idx, nullptr, nullptr, &f, nullptr,
                                   nullptr, nullptr);
            });

    auto compAttackSlider =
        Slider(0.1, 200.0, 0.5)
            ->setValue(_auxCompAttackState[idx])
            ->setWidth(80)
            ->setOnValueChanged([this, idx](double v) {
              _auxCompAttackState[idx].set(v);
              float f = (float)v;
              _updateAuxCompressor(idx, nullptr, nullptr, nullptr, &f,
                                   nullptr, nullptr);
            });

    auto compReleaseSlider =
        Slider(10.0, 1000.0, 5.0)
            ->setValue(_auxCompReleaseState[idx])
            ->setWidth(80)
            ->setOnValueChanged([this, idx](double v) {
              _auxCompReleaseState[idx].set(v);
              float f = (float)v;
              _updateAuxCompressor(idx, nullptr, nullptr, nullptr, nullptr,
                                   &f, nullptr);
            });

    auto compMakeupSlider =
        Slider(0.0, 24.0, 0.5)
            ->setValue(_auxCompMakeupState[idx])
            ->setWidth(80)
            ->setOnValueChanged([this, idx](double v) {
              _auxCompMakeupState[idx].set(v);
              float f = (float)v;
              _updateAuxCompressor(idx, nullptr, nullptr, nullptr, nullptr,
                                   nullptr, &f);
            });


    auto filterEnabledToggle =
        Toggle("Filter")
            ->setValue(_auxFilterEnabledState[idx])
            ->setOnToggleChanged([this, idx](bool v) {
              _updateAuxFilter(idx, &v, nullptr, nullptr, nullptr);
            });

    auto filterTypeDropdown =
        Dropdown({"LP", "HP", "BP", "Notch"})
            ->setSelectedIndex(_auxFilterTypeState[idx])
            ->setWidth(80)
            ->setOnSelectionChanged([this, idx](int optIdx,
                                                const std::string &) {
              _auxFilterTypeState[idx].set(optIdx);
              FilterType t = (FilterType)optIdx;
              _updateAuxFilter(idx, nullptr, &t, nullptr, nullptr);
            });

    auto filterCutoffSlider =
        Slider(20.0, 20000.0, 10.0)
            ->setValue(_auxFilterCutoffState[idx])
            ->setWidth(80)
            ->setOnValueChanged([this, idx](double v) {
              _auxFilterCutoffState[idx].set(v);
              float c = (float)v;
              _updateAuxFilter(idx, nullptr, nullptr, &c, nullptr);
            });

    auto filterQSlider =
        Slider(0.1, 10.0, 0.1)
            ->setValue(_auxFilterQState[idx])
            ->setWidth(80)
            ->setOnValueChanged([this, idx](double v) {
              _auxFilterQState[idx].set(v);
              float q = (float)v;
              _updateAuxFilter(idx, nullptr, nullptr, nullptr, &q);
            });


    auto meter = Box({})
                     ->setWidth(14)
                     ->setHeight(56)
                     ->setBorderRadius(2)
                     ->setBackgroundColor(Color::fromRGB(90, 220, 60));
    _auxBusPeakState[idx].bindProperty(meter, [](Widget *w, const double &v) {
      int g = (int)std::min(220.0, 90 + 160 * v);
      int r = (int)std::min(230.0, 40 + 200 * std::max(0.0, v - 0.7));
      w->backgroundColor = Color::fromRGB(r, g, 60);
      w->hasBackground = true;
    });

    BusID busId = _auxBuses[idx];
    auto strip =
        Column({
                   Text(_auxBusNames[idx])->setFontSize(11)->setWidth(80),
                   meter,
                   Slider(0.0, 1.5, 0.01)
                       ->setValue(_auxBusVolumeState[idx])
                       ->setWidth(80)
                       ->setOnValueChanged([busId](double v) {
                         AudioEngine::get().setBusGain(busId, (float)v);
                       }),
                   Text("Insert")->setFontSize(10),
                   filterEnabledToggle,
                   filterTypeDropdown,
                   filterCutoffSlider,
                   filterQSlider,
                   compEnabledToggle,
                   compThresholdSlider,
                   compRatioSlider,
                   compAttackSlider,
                   compReleaseSlider,
                   compMakeupSlider,

               })
            ->setGap(4)
            ->setWidth(90)
            ->setPadding(6)
            ->setBackgroundColor(Color::fromRGB(232, 240, 236))
            ->setBorderRadius(6);

    _auxBusStripsRow->addChild(strip);
  }

  void _rebuildAuxBusStrips() {
    _auxBusVolumeState.clear();
    _auxBusPeakState.clear();
    _auxFilterEnabledState.clear();
    _auxFilterTypeState.clear();
    _auxFilterCutoffState.clear();
    _auxFilterQState.clear();
    _auxCompEnabledState.clear();
    _auxCompThresholdState.clear();
    _auxCompRatioState.clear();
    _auxCompAttackState.clear();
    _auxCompReleaseState.clear();
    _auxCompMakeupState.clear();

    if (_auxBusStripsRow)
      _auxBusStripsRow->children.clear();
    for (size_t i = 0; i < _auxBuses.size(); i++)
      _appendAuxBusStrip(i);
    if (_auxBusStripsRow)
      _auxBusStripsRow->markNeedsLayout();
  }

  // Read-modify-write against the engine's own stored insert params —
  // the engine (not the UI State) is the source of truth for a filter's
  // four fields, same pattern getTrackGain/getTrackPan already use for
  // gain/pan. Only the field(s) passed non-null are overridden; the
  // others round-trip through unchanged. Passing all-null just re-applies
  // whatever's already there (used by load, see below).
  void _updateTrackFilter(size_t idx, const bool *newEnabled,
                          const FilterType *newType, const float *newCutoff,
                          const float *newQ) {
    TrackID et = _timeline.tracks[idx].engineTrack;
    bool enabled;
    FilterType type;
    float cutoff, q;
    AudioEngine::get().getTrackFilterInsert(et, kFilterInsertSlot, enabled,
                                            type, cutoff, q);
    if (newEnabled)
      enabled = *newEnabled;
    if (newType)
      type = *newType;
    if (newCutoff)
      cutoff = *newCutoff;
    if (newQ)
      q = *newQ;
    AudioEngine::get().setTrackFilterInsert(et, kFilterInsertSlot, enabled,
                                            type, cutoff, q);
  }

  void _updateAuxFilter(size_t idx, const bool *newEnabled,
                        const FilterType *newType, const float *newCutoff,
                        const float *newQ) {
    BusID bus = _auxBuses[idx];
    bool enabled;
    FilterType type;
    float cutoff, q;
    AudioEngine::get().getBusFilterInsert(bus, kFilterInsertSlot, enabled,
                                          type, cutoff, q);
    if (newEnabled)
      enabled = *newEnabled;
    if (newType)
      type = *newType;
    if (newCutoff)
      cutoff = *newCutoff;
    if (newQ)
      q = *newQ;
    AudioEngine::get().setBusFilterInsert(bus, kFilterInsertSlot, enabled,
                                          type, cutoff, q);
  }

  void _updateTrackCompressor(size_t idx, const bool *newEnabled,
                              const float *newThresh, const float *newRatio,
                              const float *newAttack, const float *newRelease,
                              const float *newMakeup) {
    TrackID et = _timeline.tracks[idx].engineTrack;
    bool enabled;
    float thresh, ratio, attack, release, makeup;
    AudioEngine::get().getTrackCompressorInsert(
        et, kCompressorInsertSlot, enabled, thresh, ratio, attack, release,
        makeup);
    if (newEnabled)
      enabled = *newEnabled;
    if (newThresh)
      thresh = *newThresh;
    if (newRatio)
      ratio = *newRatio;
    if (newAttack)
      attack = *newAttack;
    if (newRelease)
      release = *newRelease;
    if (newMakeup)
      makeup = *newMakeup;
    AudioEngine::get().setTrackCompressorInsert(
        et, kCompressorInsertSlot, enabled, thresh, ratio, attack, release,
        makeup);
  }

  void _updateAuxCompressor(size_t idx, const bool *newEnabled,
                            const float *newThresh, const float *newRatio,
                            const float *newAttack, const float *newRelease,
                            const float *newMakeup) {
    BusID bus = _auxBuses[idx];
    bool enabled;
    float thresh, ratio, attack, release, makeup;
    AudioEngine::get().getBusCompressorInsert(
        bus, kCompressorInsertSlot, enabled, thresh, ratio, attack, release,
        makeup);
    if (newEnabled)
      enabled = *newEnabled;
    if (newThresh)
      thresh = *newThresh;
    if (newRatio)
      ratio = *newRatio;
    if (newAttack)
      attack = *newAttack;
    if (newRelease)
      release = *newRelease;
    if (newMakeup)
      makeup = *newMakeup;
    AudioEngine::get().setBusCompressorInsert(
        bus, kCompressorInsertSlot, enabled, thresh, ratio, attack, release,
        makeup);
  }




  AutomationLane *_findAutomationLane(int trackIndex, AutomationParam param) {
    if (trackIndex < 0 || trackIndex >= (int)_timeline.tracks.size())
      return nullptr;
    for (auto &l : _timeline.tracks[trackIndex].automationLanes)
      if (l.param == param)
        return &l;
    return nullptr;
  }

  static void _automationRange(AutomationParam p, float &lo, float &hi) {
    if (p == AutomationParam::TrackVolume) {
      lo = 0.f;
      hi = 1.5f;
    } else {
      lo = -1.f;
      hi = 1.f;
    }
  }

  // Click-to-add from TimelineSurface. Creates the lane on first use, same
  // sorted-insert-or-replace-at-exact-beat logic the old playhead button
  // used. Undo-tracked (unlike the old button, which explicitly wasn't) —
  // clicking a specific point on the timeline is a deliberate, discrete
  // edit in a way that isn't true of rapid slider iteration.
  void _timelineAutomationPointAdd(int trackIndex, AutomationParam param,
                                   double beat, float value) {
    if (trackIndex < 0 || trackIndex >= (int)_timeline.tracks.size())
      return;
    TimelineTrack &tt = _timeline.tracks[trackIndex];
    AutomationLane *lane = nullptr;
    for (auto &l : tt.automationLanes)
      if (l.param == param) {
        lane = &l;
        break;
      }
    if (!lane) {
      tt.automationLanes.push_back({param, {}, true});
      lane = &tt.automationLanes.back();
    }

    AutomationPoint pt{beat, value};
    auto it = std::lower_bound(
        lane->points.begin(), lane->points.end(), pt,
        [](const AutomationPoint &a, const AutomationPoint &b) {
          return a.beat < b.beat;
        });
    size_t insertIdx = (size_t)(it - lane->points.begin());
    bool replaced = (it != lane->points.end() && it->beat == beat);
    AutomationPoint oldPt = replaced ? *it : AutomationPoint{};
    if (replaced)
      it->value = value;
    else
      lane->points.insert(it, pt);

    if (_timelineCanvas)
      _timelineCanvas->redraw();

    _undoStack.push(
        [this, trackIndex, param, insertIdx, replaced, oldPt] {
          AutomationLane *l = _findAutomationLane(trackIndex, param);
          if (!l || insertIdx >= l->points.size())
            return;
          if (replaced)
            l->points[insertIdx] = oldPt;
          else
            l->points.erase(l->points.begin() + insertIdx);
          if (_timelineCanvas)
            _timelineCanvas->redraw();
        },
        [this, trackIndex, param, insertIdx, replaced, pt] {
          AutomationLane *l = _findAutomationLane(trackIndex, param);
          if (!l)
            return;
          if (replaced && insertIdx < l->points.size())
            l->points[insertIdx] = pt;
          else
            l->points.insert(
                l->points.begin() + std::min(insertIdx, l->points.size()), pt);
          if (_timelineCanvas)
            _timelineCanvas->redraw();
        });
  }

  float _autoDragOldValue = 0.f; // scratch, mirrors _fadeDragOldValue

  void _timelineAutomationPointDragStart(int trackIndex, AutomationParam param,
                                         int pointIndex) {
    AutomationLane *lane = _findAutomationLane(trackIndex, param);
    if (!lane || pointIndex < 0 || pointIndex >= (int)lane->points.size())
      return;
    _autoDragOldValue = lane->points[pointIndex].value;
  }

  // Live-apply during drag — no undo push, same as _timelineSetFadeCore.
  // Deliberately left unclamped: TimelineSurface lets a dragged point
  // stray outside [lo, hi] as a "let go to delete" affordance, so the
  // underlying value has to be able to reflect that transiently.
  void _timelineAutomationPointDragCore(int trackIndex, AutomationParam param,
                                        int pointIndex, float value) {
    AutomationLane *lane = _findAutomationLane(trackIndex, param);
    if (!lane || pointIndex < 0 || pointIndex >= (int)lane->points.size())
      return;
    lane->points[pointIndex].value = value;
    if (_timelineCanvas)
      _timelineCanvas->redraw();
  }

  // `beat` (not `pointIndex`) is used as the identity key inside the undo/
  // redo lambdas below — indices shift if other points get added/removed
  // between now and an eventual undo, but a point's beat never changes
  // once placed (see the note on TimelineSurface's drag lifecycle), so
  // it's the stable handle. Same best-effort tradeoff the rest of this
  // file's undo entries make (e.g. duplicatePattern's redo comment).
  void _timelineAutomationPointDragEnd(int trackIndex, AutomationParam param,
                                       int pointIndex, bool deleted) {
    AutomationLane *lane = _findAutomationLane(trackIndex, param);
    if (!lane || pointIndex < 0 || pointIndex >= (int)lane->points.size())
      return;

    double beat = lane->points[pointIndex].beat;
    float oldValue = _autoDragOldValue;

    if (deleted) {
      AutomationPoint removed = lane->points[pointIndex];
      lane->points.erase(lane->points.begin() + pointIndex);
      if (_timelineCanvas)
        _timelineCanvas->redraw();

      _undoStack.push(
          [this, trackIndex, param, pointIndex, removed] {
            AutomationLane *l = _findAutomationLane(trackIndex, param);
            if (!l)
              return;
            size_t idx = std::min((size_t)pointIndex, l->points.size());
            l->points.insert(l->points.begin() + idx, removed);
            if (_timelineCanvas)
              _timelineCanvas->redraw();
          },
          [this, trackIndex, param, beat] {
            AutomationLane *l = _findAutomationLane(trackIndex, param);
            if (!l)
              return;
            for (size_t i = 0; i < l->points.size(); i++)
              if (l->points[i].beat == beat) {
                l->points.erase(l->points.begin() + i);
                break;
              }
            if (_timelineCanvas)
              _timelineCanvas->redraw();
          });
      return;
    }

    // Clamp the committed value back into range — only the live drag is
    // allowed to stray outside [lo, hi].
    float lo, hi;
    _automationRange(param, lo, hi);
    float newValue = std::max(lo, std::min(hi, lane->points[pointIndex].value));
    lane->points[pointIndex].value = newValue;
    if (_timelineCanvas)
      _timelineCanvas->redraw();

    if (oldValue == newValue)
      return; // a click with no real movement — no-op, no undo entry

    _undoStack.push(
        [this, trackIndex, param, beat, oldValue] {
          AutomationLane *l = _findAutomationLane(trackIndex, param);
          if (!l)
            return;
          for (auto &p : l->points)
            if (p.beat == beat) {
              p.value = oldValue;
              break;
            }
          if (_timelineCanvas)
            _timelineCanvas->redraw();
        },
        [this, trackIndex, param, beat, newValue] {
          AutomationLane *l = _findAutomationLane(trackIndex, param);
          if (!l)
            return;
          for (auto &p : l->points)
            if (p.beat == beat) {
              p.value = newValue;
              break;
            }
          if (_timelineCanvas)
            _timelineCanvas->redraw();
        });
  }

  void _timelineAddClipAt(int trackIndex, double beat) {
    if (trackIndex < 0 || trackIndex >= (int)_timeline.tracks.size())
      return;
    if (!_pendingAudioClipPath.empty()) {
      std::string path = _pendingAudioClipPath;
      _pendingAudioClipPath.clear();
      _timelineAddAudioClipAt(trackIndex, beat, path);
      return;
    }

    Clip clip;
    clip.id = _nextClipId++;
    clip.type = ClipType::Pattern;
    clip.patternSlot = _seq->editingSlot;

    const Pattern &pat = _seq->patternSlots[clip.patternSlot];
    clip.lengthBeats =
        std::max(0.25, (double)pat.numSteps / std::max(1, pat.stepsPerBeat));
    clip.startBeat = std::max(0.0, std::round(beat));

    _timeline.tracks[trackIndex].clips.push_back(clip);
    _timelineSelectClip(clip.id);
    if (_timelineCanvas)
      _timelineCanvas->redraw();
  }

  // Places a picked audio file as an AudioClip at (trackIndex, beat).
  // Length defaults to the sample's real duration converted to beats at
  // the project's current bpm — same "auto-size from content" convention
  // _timelineAddClipAt uses for PatternClip length.
  void _timelineAddAudioClipAt(int trackIndex, double beat,
                               const std::string &path) {
    if (trackIndex < 0 || trackIndex >= (int)_timeline.tracks.size())
      return;

    SampleID id = AudioEngine::get().loadSample(path);
    double lengthBeats = 4.0; // fallback if the file fails to decode
    if (id != kInvalidSample) {
      float durSec = AudioEngine::get().getSampleDurationSeconds(id);
      if (durSec > 0.f)
        lengthBeats = std::max(0.25, (double)durSec * (_seq->bpm / 60.0));
    }

    Clip clip;
    clip.id = _nextClipId++;
    clip.type = ClipType::Audio;
    clip.audioFilePath = path;
    clip.startBeat = std::max(0.0, std::round(beat));
    clip.lengthBeats = lengthBeats;
    clip.gain = 1.0f;

    _timeline.tracks[trackIndex].clips.push_back(clip);
    _timelineSelectClip(clip.id);
    if (_timelineCanvas)
      _timelineCanvas->redraw();
  }

  void _syncRecordTrackDropdown() {
    if (!_recordTrackDropdown)
      return;
    std::vector<std::string> labels;
    for (auto &t : _timeline.tracks)
      labels.push_back(t.name);
    _recordTrackDropdown->setOptions(labels);
    if (_recordTargetTrackIndex >= (int)_timeline.tracks.size())
      _recordTargetTrackIndex = std::max(0, (int)_timeline.tracks.size() - 1);
  }

  // Called DIRECTLY on AudioEngine's capture audio thread (see
  // AudioEngine::startCapture's doc comment) — no locks, no allocation
  // beyond the vector growth itself, which is the same tradeoff every
  // other realtime callback in this codebase (e.g. StreamCallback) makes.
  void _onCaptureFrames(const float *buf, uint32_t frames, uint32_t channels) {
    if (!_isRecording)
      return;
    size_t oldSize = _recordBuffer.size();
    _recordBuffer.resize(oldSize + frames);
    if (channels <= 1) {
      std::memcpy(_recordBuffer.data() + oldSize, buf, frames * sizeof(float));
    } else {
      // Downmix defensively even though _startRecording always requests
      // channels=1 — guards against a future caller changing that.
      for (uint32_t i = 0; i < frames; i++)
        _recordBuffer[oldSize + i] = buf[i * channels];
    }
  }

  void _startRecording() {
    if (_isRecording || _timeline.tracks.empty())
      return;
    _recordTrackIndex = std::max(
        0, std::min((int)_timeline.tracks.size() - 1, _recordTargetTrackIndex));
    _recordBuffer.clear();
    _recordSampleRate = AudioEngine::get().sampleRate();
    // Land the new clip wherever the timeline playhead currently is, so
    // recording along with playback places the take in sync; recording
    // with the transport stopped just starts the clip at beat 0, same
    // "auto-size from content" spirit as _timelineAddAudioClipAt.
    _recordStartBeat =
        _timelineScheduler->playing ? _timelineScheduler->playheadBeats() : 0.0;

    bool ok = AudioEngine::get().startCapture(
        [this](const float *buf, uint32_t frames, uint32_t channels) {
          _onCaptureFrames(buf, frames, channels);
        },
        /*channels=*/1, _recordSampleRate);
    if (!ok)
      return; // device busy/unavailable — silent no-op, same pattern
              // every other bool-returning AudioEngine call uses here

    _isRecording = true;
    _recordingState.set(true);
  }

  void _stopRecording() {
    if (!_isRecording)
      return;
    AudioEngine::get().stopCapture(); // joins the capture thread — safe to
                                      // read _recordBuffer from here on
    _isRecording = false;
    _recordingState.set(false);

    if (_recordBuffer.empty() || _recordTrackIndex < 0 ||
        _recordTrackIndex >= (int)_timeline.tracks.size())
      return; // nothing captured, or the target track vanished mid-take
              // (e.g. deleted) — drop the take rather than guess

    std::string path =
        "flux_recording_" + std::to_string(_nextRecordingIndex++) + ".wav";
    if (!writeWavMono16(path, _recordBuffer, _recordSampleRate)) {
      _recordBuffer.clear();
      return; // couldn't write the file — drop the take rather than add
              // a clip pointing at nothing
    }

    double lengthSec = (double)_recordBuffer.size() / (double)_recordSampleRate;
    double lengthBeats = std::max(0.05, lengthSec * (_seq->bpm / 60.0));

    Clip clip;
    clip.id = _nextClipId++;
    clip.type = ClipType::Audio;
    clip.audioFilePath = path;
    clip.startBeat = _recordStartBeat;
    clip.lengthBeats = lengthBeats;
    clip.gain = 1.0f;

    _timeline.tracks[_recordTrackIndex].clips.push_back(clip);
    _timelineSelectClip(clip.id);
    if (_timelineCanvas)
      _timelineCanvas->redraw();

    _recordBuffer.clear();
    _recordBuffer.shrink_to_fit(); // release the (possibly large) capture
                                   // buffer now that it's safely on disk
  }

  Clip *_findTimelineClip(ClipID id) {
    for (auto &track : _timeline.tracks)
      for (auto &c : track.clips)
        if (c.id == id)
          return &c;
    return nullptr;
  }

  void _timelineFadeDragStart(ClipID id, bool isFadeIn) {
    Clip *clip = _findTimelineClip(id);
    if (!clip)
      return;
    _fadeDragOldValue = isFadeIn ? clip->fadeInBeats : clip->fadeOutBeats;
  }

  // Applies the live value during a drag — called on every onFadeDrag,
  // deliberately with no undo-stack push (see _fadeDragOldValue's comment).
  void _timelineSetFadeCore(ClipID id, bool isFadeIn, float beats) {
    Clip *clip = _findTimelineClip(id);
    if (!clip)
      return;
    if (isFadeIn)
      clip->fadeInBeats = beats;
    else
      clip->fadeOutBeats = beats;
    if (_timelineCanvas)
      _timelineCanvas->redraw();
  }

  void _timelineFadeDragEnd(ClipID id, bool isFadeIn) {
    Clip *clip = _findTimelineClip(id);
    if (!clip)
      return;
    float oldVal = _fadeDragOldValue;
    float newVal = isFadeIn ? clip->fadeInBeats : clip->fadeOutBeats;
    if (oldVal == newVal)
      return; // a click with no actual drag movement — no-op, no undo entry

    _undoStack.push([this, id, isFadeIn,
                     oldVal] { _timelineSetFadeCore(id, isFadeIn, oldVal); },
                    [this, id, isFadeIn, newVal] {
                      _timelineSetFadeCore(id, isFadeIn, newVal);
                    });
  }

  void _timelineTrimDragStart(ClipID id, bool isLeftEdge) {
    Clip *clip = _findTimelineClip(id);
    if (!clip)
      return;
    _trimDragOldStartBeat = clip->startBeat;
    _trimDragOldLengthBeats = clip->lengthBeats;
    _trimDragOldAudioOffsetSec = clip->audioStartOffsetSec;

    // Cache the source's real duration once per drag so
    // _timelineSetTrimCore can clamp the right-edge handle without a
    // sample-bank lookup on every mouse-move.
    SampleID sid = AudioEngine::get().loadSample(clip->audioFilePath);
    _trimDragSampleDurationSec =
        (sid != kInvalidSample)
            ? AudioEngine::get().getSampleDurationSeconds(sid)
            : 0.f;
  }

  // Applies the live value during a drag — no undo-stack push per move,
  // same reasoning as _timelineSetFadeCore. `absoluteBeat` is the dragged
  // edge's position on the timeline's global beat axis (see the comment
  // on TimelineSurface::onTrimDrag for why this isn't a relative offset
  // the way fade's is).
  void _timelineSetTrimCore(ClipID id, bool isLeftEdge, double absoluteBeat) {
    Clip *clip = _findTimelineClip(id);
    if (!clip)
      return;

    double secPerBeat = 60.0 / _seq->bpm;
    // A trimmed clip always keeps at least this many beats — avoids a
    // zero/negative-length clip if the handles get dragged past
    // each other.
    constexpr double kMinLengthBeats = 0.05;

    if (isLeftEdge) {
      double rightEdge = clip->startBeat + clip->lengthBeats;
      double newStart =
          std::max(0.0, std::min(absoluteBeat, rightEdge - kMinLengthBeats));

      // Moving the left edge right trims audio off the front (and vice
      // versa) — the source's own start offset moves by the same delta,
      // clamped so it can never go negative (can't trim before the
      // sample's own frame 0). If that clamp bites, the edge itself is
      // pulled back in step so startBeat/offset stay in sync.
      double deltaBeats = newStart - clip->startBeat;
      double newOffsetSec =
          std::max(0.0, clip->audioStartOffsetSec + deltaBeats * secPerBeat);
      double actualDeltaSec = newOffsetSec - clip->audioStartOffsetSec;
      double actualDeltaBeats = actualDeltaSec / secPerBeat;

      clip->startBeat += actualDeltaBeats;
      clip->lengthBeats = rightEdge - clip->startBeat;
      clip->audioStartOffsetSec = newOffsetSec;
    } else {
      double newLen = std::max(kMinLengthBeats, absoluteBeat - clip->startBeat);
      if (_trimDragSampleDurationSec > 0.f) {
        double maxLenSec =
            (double)_trimDragSampleDurationSec - clip->audioStartOffsetSec;
        newLen =
            std::min(newLen, std::max(kMinLengthBeats, maxLenSec / secPerBeat));
      }
      clip->lengthBeats = newLen;
    }

    if (_timelineCanvas)
      _timelineCanvas->redraw();
  }

  void _timelineTrimDragEnd(ClipID id, bool /*isLeftEdge*/) {
    Clip *clip = _findTimelineClip(id);
    if (!clip)
      return;
    double oldStart = _trimDragOldStartBeat;
    double oldLen = _trimDragOldLengthBeats;
    double oldOffset = _trimDragOldAudioOffsetSec;
    double newStart = clip->startBeat;
    double newLen = clip->lengthBeats;
    double newOffset = clip->audioStartOffsetSec;
    if (oldStart == newStart && oldLen == newLen && oldOffset == newOffset)
      return; // a click with no real movement — no-op, no undo entry

    _undoStack.push(
        [this, id, oldStart, oldLen, oldOffset] {
          if (Clip *c = _findTimelineClip(id)) {
            c->startBeat = oldStart;
            c->lengthBeats = oldLen;
            c->audioStartOffsetSec = oldOffset;
            if (_timelineCanvas)
              _timelineCanvas->redraw();
          }
        },
        [this, id, newStart, newLen, newOffset] {
          if (Clip *c = _findTimelineClip(id)) {
            c->startBeat = newStart;
            c->lengthBeats = newLen;
            c->audioStartOffsetSec = newOffset;
            if (_timelineCanvas)
              _timelineCanvas->redraw();
          }
        });
  }

  void _timelineSelectClip(ClipID id) {
    _selectedClipId = id;
    if (_timelineSurface)
      _timelineSurface->selectedClip = id;

    std::string label = "No clip selected";
    for (const auto &track : _timeline.tracks)
      for (const auto &clip : track.clips)
        if (clip.id == id && clip.type == ClipType::Pattern) {
          std::string patName =
              (clip.patternSlot >= 0 &&
               clip.patternSlot < (int)_seq->patternSlots.size())
                  ? _seq->patternSlots[clip.patternSlot].name
                  : "?";
          label = patName + " @ beat " + std::to_string((int)clip.startBeat) +
                  ", " + std::to_string(clip.lengthBeats) + " beats long";

        } else if (clip.id == id && clip.type == ClipType::Audio) {
          // Previously fell through to "No clip selected" for AudioClips —
          // this branch is new, not just a fade-UI addition, but it's the
          // natural place to also surface the current fade values while
          // dragging a handle.
          size_t slash = clip.audioFilePath.find_last_of("/\\");
          std::string base = clip.audioFilePath.empty()
                                 ? "Audio"
                                 : (slash == std::string::npos
                                        ? clip.audioFilePath
                                        : clip.audioFilePath.substr(slash + 1));
          std::ostringstream lbl;
          lbl << base << " @ beat " << (int)clip.startBeat << " · fade "
              << std::fixed << std::setprecision(1) << clip.fadeInBeats
              << "b in / " << clip.fadeOutBeats << "b out";
          label = lbl.str();
        }
    _timelineSelectionLabel.set(label);

    if (_timelineCanvas)
      _timelineCanvas->redraw();
  }

  void _deleteSelectedTimelineClip() {
    if (_selectedClipId == 0)
      return;
    for (auto &track : _timeline.tracks) {
      auto it = std::find_if(
          track.clips.begin(), track.clips.end(),
          [this](const Clip &c) { return c.id == _selectedClipId; });
      if (it != track.clips.end()) {
        track.clips.erase(it);
        break;
      }
    }
    _timelineSelectClip(0);
  }
  void _setArrangementRepeatCore(uint64_t id, int count) {
    for (auto &e : _seq->arrangement)
      if (e.id == id) {
        e.repeatCount = count;
        break;
      }
    _arrangementState.set(_seq->arrangement);
  }

  void _saveProject(const std::string &path) {
    std::ostringstream out;
    out << "{\n";
    out << "  \"version\": 1,\n";
    out << "  \"bpm\": " << _seq->bpm << ",\n";
    out << "  \"songMode\": " << (_seq->songMode ? "true" : "false") << ",\n";
    out << "  \"editingSlot\": " << _seq->editingSlot << ",\n";
    out << "  \"tracks\": [\n";
    for (size_t t = 0; t < _seq->trackVoice.size(); t++) {
      const StepHit &hit = _seq->trackVoice[t];
      out << "    {"
          << "\"freqHz\":" << hit.freqHz << ","
          << "\"gain\":" << hit.gain << ","
          << "\"pan\":" << hit.pan << ","
          << "\"waveform\":" << (int)hit.waveform << ","
          << "\"attackSec\":" << hit.attackSec << ","
          << "\"decaySec\":" << hit.decaySec << ","
          << "\"sustainLevel\":" << hit.sustainLevel << ","
          << "\"sustainSec\":" << hit.sustainSec << ","
          << "\"releaseSec\":" << hit.releaseSec << ","
          << "\"samplePath\":\""
          << SeqJson::esc(t < _trackSamplePath.size() ? _trackSamplePath[t]
                                                      : "")
          << "\","
          << "\"muted\":" << (_seq->trackMuted[t] ? "true" : "false") << ","
          << "\"soloed\":" << (_seq->trackSoloed[t] ? "true" : "false") << "}"
          << (t + 1 < _seq->trackVoice.size() ? "," : "") << "\n";
    }
    out << "  ],\n";
    out << "  \"patterns\": [\n";
    bool firstPat = true;
    for (int slot : _seq->activeSlots) {
      const Pattern &pat = _seq->patternSlots[slot];
      if (!firstPat)
        out << ",\n";
      firstPat = false;
      out << "    {\"slot\":" << slot << ",\"name\":\""
          << SeqJson::esc(pat.name) << "\",\"numSteps\":" << pat.numSteps
          << ",\"stepsPerBeat\":" << pat.stepsPerBeat
          << ",\"swing\":" << pat.swing << ",\"steps\":[";
      for (size_t t = 0; t < pat.steps.size(); t++) {
        out << "[";
        // Serialize all kMaxSteps, not just numSteps, so steps hidden by a
        // shorter length round-trip through save/load intact.
        for (int s = 0; s < StepScheduler::kMaxSteps; s++) {
          const StepData &st = pat.steps[t][s];
          out << "{\"on\":" << (st.on ? "true" : "false")
              << ",\"velocity\":" << st.velocity
              << ",\"pitch\":" << st.pitchSemitones
              << ",\"probability\":" << st.probability
              << ",\"microTiming\":" << st.microTiming << "}"

              << (s + 1 < StepScheduler::kMaxSteps ? "," : "");
        }
        out << "]" << (t + 1 < pat.steps.size() ? "," : "");
      }
      out << "]}";
    }
    out << "\n  ],\n";
    out << "  \"arrangement\": [\n";
    for (size_t i = 0; i < _seq->arrangement.size(); i++) {
      const ArrangementEntry &e = _seq->arrangement[i];
      out << "    {\"id\":" << e.id << ",\"patternSlot\":" << e.patternSlot
          << ",\"repeatCount\":" << e.repeatCount << "}"
          << (i + 1 < _seq->arrangement.size() ? "," : "") << "\n";
    }
    out << "  ],\n";
    out << "  \"timelineTracks\": [\n";
    for (size_t ti = 0; ti < _timeline.tracks.size(); ti++) {
      const TimelineTrack &tt = _timeline.tracks[ti];
      BusID sendBus = AudioEngine::get().getTrackSendBus(tt.engineTrack);
      int sendIndex = 0;
      for (size_t bi = 0; bi < _auxBuses.size(); bi++)
        if (_auxBuses[bi] == sendBus) {
          sendIndex = (int)bi + 1;
          break;
        }
     bool fEnabled;
     FilterType fType;
     float fCutoff, fQ;
     AudioEngine::get().getTrackFilterInsert(tt.engineTrack, kFilterInsertSlot,
                                             fEnabled, fType, fCutoff, fQ);
      bool cEnabled;
      float cThresh, cRatio, cAttack, cRelease, cMakeup;
      AudioEngine::get().getTrackCompressorInsert(
          tt.engineTrack, kCompressorInsertSlot, cEnabled, cThresh, cRatio,
          cAttack, cRelease, cMakeup);
      out << "    {\"id\":" << tt.id << ",\"name\":\"" << SeqJson::esc(tt.name)
          << "\",\"muted\":" << (tt.muted ? "true" : "false")
          << ",\"soloed\":" << (tt.soloed ? "true" : "false")
          << ",\"gain\":" << AudioEngine::get().getTrackGain(tt.engineTrack)
          << ",\"pan\":" << AudioEngine::get().getTrackPan(tt.engineTrack)
          << ",\"sendIndex\":" << sendIndex
          << ",\"filterEnabled\":" << (fEnabled ? "true" : "false")
          << ",\"filterType\":" << (int)fType << ",\"filterCutoff\":" << fCutoff
          << ",\"filterQ\":" << fQ
          << ",\"compEnabled\":" << (cEnabled ? "true" : "false")
          << ",\"compThreshold\":" << cThresh << ",\"compRatio\":" << cRatio
          << ",\"compAttack\":" << cAttack << ",\"compRelease\":" << cRelease
          << ",\"compMakeup\":" << cMakeup << ",\"clips\":[";
      for (size_t ci = 0; ci < tt.clips.size(); ci++) {
        const Clip &c = tt.clips[ci];
        out << "{\"id\":" << c.id << ",\"type\":" << (int)c.type
            << ",\"startBeat\":" << c.startBeat
            << ",\"lengthBeats\":" << c.lengthBeats
            << ",\"patternSlot\":" << c.patternSlot << ",\"audioFilePath\":\""
            << SeqJson::esc(c.audioFilePath) << "\""
            << ",\"audioStartOffsetSec\":" << c.audioStartOffsetSec
            << ",\"gain\":" << c.gain << ",\"fadeInBeats\":" << c.fadeInBeats
            << ",\"fadeOutBeats\":" << c.fadeOutBeats << "}"
            << (ci + 1 < tt.clips.size() ? "," : "");
      }
      out << "]}" << (ti + 1 < _timeline.tracks.size() ? "," : "") << "\n";
    }
    out << "  ],\n";
    out << "  \"auxBuses\": [\n";
    for (size_t i = 0; i < _auxBuses.size(); i++) {
      bool fEnabled;
      FilterType fType;
      float fCutoff, fQ;
      AudioEngine::get().getBusFilterInsert(_auxBuses[i], kFilterInsertSlot,
                                            fEnabled, fType, fCutoff, fQ);
      bool cEnabled;
      float cThresh, cRatio, cAttack, cRelease, cMakeup;
      AudioEngine::get().getBusCompressorInsert(
          _auxBuses[i], kCompressorInsertSlot, cEnabled, cThresh, cRatio,
          cAttack, cRelease, cMakeup);
      out << "    {\"name\":\"" << SeqJson::esc(_auxBusNames[i])
          << "\",\"gain\":" << AudioEngine::get().getBusGain(_auxBuses[i])
          << ",\"filterEnabled\":" << (fEnabled ? "true" : "false")
          << ",\"filterType\":" << (int)fType << ",\"filterCutoff\":" << fCutoff
          << ",\"filterQ\":" << fQ
          << ",\"compEnabled\":" << (cEnabled ? "true" : "false")
          << ",\"compThreshold\":" << cThresh << ",\"compRatio\":" << cRatio
          << ",\"compAttack\":" << cAttack << ",\"compRelease\":" << cRelease
          << ",\"compMakeup\":" << cMakeup << "}"
          << (i + 1 < _auxBuses.size() ? "," : "") << "\n";
    }
    out << "  ]\n}\n";
    std::ofstream f(path, std::ios::binary);
    if (f)
      f << out.str();
  }
  void _loadProject(const std::string &path) {
    std::ifstream f(path, std::ios::binary);
    if (!f)
      return;
    std::ostringstream ss;
    ss << f.rdbuf();
    SeqJson::JVal root;
    if (!SeqJson::parse(ss.str(), root) ||
        root.type != SeqJson::JVal::Type::Object)
      return; // malformed file — leave the current project untouched
    _seq->stop();
    _currentStepState.set(-1);
    _seq->bpm = root["bpm"].asDouble(_seq->bpm);
    _bpmState.set(_seq->bpm);
    // ── Tracks ──────────────────────────────────────────────────────────
    const auto &tracksArr = root["tracks"].arr;
    for (size_t t = 0; t < tracksArr.size() && t < _seq->trackVoice.size();
         t++) {
      const SeqJson::JVal &tj = tracksArr[t];
      _seq->trackVoice[t].freqHz =
          (float)tj["freqHz"].asDouble(_seq->trackVoice[t].freqHz);
      _seq->trackVoice[t].gain =
          (float)tj["gain"].asDouble(_seq->trackVoice[t].gain);
      _seq->trackVoice[t].pan =
          (float)tj["pan"].asDouble(_seq->trackVoice[t].pan);
      _seq->trackVoice[t].waveform =
          (OscWaveform)tj["waveform"].asInt((int)_seq->trackVoice[t].waveform);
      _seq->trackVoice[t].attackSec =
          (float)tj["attackSec"].asDouble(_seq->trackVoice[t].attackSec);
      _seq->trackVoice[t].decaySec =
          (float)tj["decaySec"].asDouble(_seq->trackVoice[t].decaySec);
      _seq->trackVoice[t].sustainLevel =
          (float)tj["sustainLevel"].asDouble(_seq->trackVoice[t].sustainLevel);
      _seq->trackVoice[t].sustainSec =
          (float)tj["sustainSec"].asDouble(_seq->trackVoice[t].sustainSec);
      _seq->trackVoice[t].releaseSec =
          (float)tj["releaseSec"].asDouble(_seq->trackVoice[t].releaseSec);
      _seq->trackMuted[t] = tj["muted"].asBool(false);
      _seq->trackSoloed[t] = tj["soloed"].asBool(false);
      _trackVolumeState[t].set((double)_seq->trackVoice[t].gain);
      _trackPanState[t].set((double)_seq->trackVoice[t].pan);
      _refreshSynthStateFromTrack(t);
      _muteState[t].set(_seq->trackMuted[t]);
      _soloState[t].set(_seq->trackSoloed[t]);
      std::string samplePath = tj["samplePath"].asString("");
      if (!samplePath.empty())
        _loadTrackSample(t, samplePath);
    }
    // ── Patterns ────────────────────────────────────────────────────────
    // deletePattern() refuses to remove the last remaining pattern, so
    // repeatedly deleting activeSlots.front() leaves exactly one slot
    // standing — that slot gets overwritten by the file's first pattern
    // below (or left as an empty default if the file has none).
    while (_seq->activeSlots.size() > 1)
      _seq->deletePattern(_seq->activeSlots.front());
    const auto &patternsArr = root["patterns"].arr;
    std::unordered_map<int, int> fileSlotToRealSlot;
    bool first = true;
    for (const auto &pj : patternsArr) {
      int wantedNumSteps = pj["numSteps"].asInt(16);
      int spb = pj["stepsPerBeat"].asInt(4);
      float swing = (float)pj["swing"].asDouble(0.0);
      std::string name = pj["name"].asString("Pattern");
      int realSlot;
      if (first) {
        realSlot = _seq->activeSlots.front();
        _seq->patternSlots[realSlot].name = name;
        first = false;
      } else {
        realSlot = _seq->addPattern(name);
        if (realSlot < 0)
          break; // kMaxPatterns exhausted — rest of file dropped
      }
      _seq->setPatternLength(realSlot, wantedNumSteps);
      _seq->setPatternStepsPerBeat(realSlot, spb);
      _seq->setPatternSwing(realSlot, swing);
      fileSlotToRealSlot[pj["slot"].asInt(-1)] = realSlot;
      Pattern &pat = _seq->patternSlots[realSlot];
      const auto &stepsArr = pj["steps"].arr;
      for (size_t t = 0; t < stepsArr.size() && t < pat.steps.size(); t++) {
        const auto &trackSteps = stepsArr[t].arr;
        for (size_t s = 0;
             s < trackSteps.size() && (int)s < StepScheduler::kMaxSteps; s++) {
          const SeqJson::JVal &sj = trackSteps[s];
          StepData &sd = pat.steps[t][s];
          sd.on = sj["on"].asBool(false);
          sd.velocity = (float)sj["velocity"].asDouble(1.0);
          sd.pitchSemitones = (float)sj["pitch"].asDouble(0.0);
          sd.probability = (float)sj["probability"].asDouble(1.0);
          sd.microTiming = (float)sj["microTiming"].asDouble(0.0);
        }
      }
    }
    // ── Arrangement ─────────────────────────────────────────────────────
    _seq->arrangement.clear();
    _arrangementState.clear();
    uint64_t maxId = 0;
    for (const auto &aj : root["arrangement"].arr) {
      auto it = fileSlotToRealSlot.find(aj["patternSlot"].asInt(-1));
      if (it == fileSlotToRealSlot.end())
        continue; // referenced a pattern slot the file never defined
      ArrangementEntry e;
      e.id = (uint64_t)aj["id"].asInt((int)(maxId + 1));
      e.patternSlot = it->second;
      e.repeatCount = std::max(1, aj["repeatCount"].asInt(1));
      maxId = std::max(maxId, e.id);
      _seq->arrangement.push_back(e);
      _arrangementState.push_back(e);
    }
    _nextArrangementEntryId = maxId + 1;
    bool wantSongMode = root["songMode"].asBool(false);
    _seq->setSongMode(wantSongMode);
    _songModeState.set(wantSongMode);
    int wantEditing = root["editingSlot"].asInt(-1);
    auto editIt = fileSlotToRealSlot.find(wantEditing);
    int realEditing = editIt != fileSlotToRealSlot.end()
                          ? editIt->second
                          : _seq->activeSlots.front();
    _seq->setEditingPattern(realEditing);
    _refreshGridFromPattern();
    _syncPatternDropdown();

    // ── Timeline (Phase 3 clips) ────────────────────────────────────────
    // Release engine tracks owned by whatever timeline was in memory
    // before this load, then rebuild from the file. Old files (version 1)
    // simply have no "timelineTracks" key, which SeqJson::JVal::operator[]
    // resolves to an empty array — _timeline.tracks just ends up empty,
    // same as a brand new project.
    for (auto &track : _timeline.tracks)
      if (track.engineTrack != kInvalidTrack)
        AudioEngine::get().destroyTrack(track.engineTrack);
    _timeline.tracks.clear();

    uint64_t maxClipId = 0;
    uint32_t maxTimelineTrackId = 0;
    struct LoadedFilter {
      bool enabled = false;
      FilterType type = FilterType::LowPass;
      float cutoff = 1000.f;
      float q = 0.707f;
      bool compEnabled = false;
      float compThreshold = -18.f;
      float compRatio = 4.f;
      float compAttack = 10.f;
      float compRelease = 100.f;
      float compMakeup = 0.f;

    };
    std::vector<int> loadedSendIndex;
    std::vector<LoadedFilter> loadedTrackFilter;
    for (const auto &ttj : root["timelineTracks"].arr) {
      TimelineTrack tt;
      tt.id = (TimelineTrackID)ttj["id"].asInt(0);
      tt.name = ttj["name"].asString("Track");
      tt.muted = ttj["muted"].asBool(false);
      tt.soloed = ttj["soloed"].asBool(false);
      tt.engineTrack = AudioEngine::get().createTrack(); // reserved for
                                                         // future per-clip
                                                         // routing, same
                                                         // as _addTimelineTrack
      maxTimelineTrackId = std::max(maxTimelineTrackId, (uint32_t)tt.id);
    
      AudioEngine::get().setTrackGain(tt.engineTrack,
                                      (float)ttj["gain"].asDouble(1.0));
      AudioEngine::get().setTrackPan(tt.engineTrack,
                                     (float)ttj["pan"].asDouble(0.0));
      loadedSendIndex.push_back(ttj["sendIndex"].asInt(0));


      LoadedFilter lf;
      lf.enabled = ttj["filterEnabled"].asBool(false);
      lf.type = (FilterType)ttj["filterType"].asInt(0);
      lf.cutoff = (float)ttj["filterCutoff"].asDouble(1000.0);
      lf.q = (float)ttj["filterQ"].asDouble(0.707);
      AudioEngine::get().setTrackFilterInsert(tt.engineTrack, kFilterInsertSlot,
                                              lf.enabled, lf.type, lf.cutoff,
                                              lf.q);
      lf.compEnabled = ttj["compEnabled"].asBool(false);
      lf.compThreshold = (float)ttj["compThreshold"].asDouble(-18.0);
      lf.compRatio = (float)ttj["compRatio"].asDouble(4.0);
      lf.compAttack = (float)ttj["compAttack"].asDouble(10.0);
      lf.compRelease = (float)ttj["compRelease"].asDouble(100.0);
      lf.compMakeup = (float)ttj["compMakeup"].asDouble(0.0);
      AudioEngine::get().setTrackCompressorInsert(
          tt.engineTrack, kCompressorInsertSlot, lf.compEnabled,
          lf.compThreshold, lf.compRatio, lf.compAttack, lf.compRelease,
          lf.compMakeup);
      loadedTrackFilter.push_back(lf);

      for (const auto &cj : ttj["clips"].arr) {
        Clip clip;
        clip.id = (ClipID)cj["id"].asInt(0);
        clip.type =
            (cj["type"].asInt(0) == 1) ? ClipType::Audio : ClipType::Pattern;
        clip.startBeat = cj["startBeat"].asDouble(0.0);
        clip.lengthBeats = cj["lengthBeats"].asDouble(4.0);
        clip.audioFilePath = cj["audioFilePath"].asString("");
        clip.audioStartOffsetSec = cj["audioStartOffsetSec"].asDouble(0.0);
        clip.gain = (float)cj["gain"].asDouble(1.0);
        clip.fadeInBeats = (float)cj["fadeInBeats"].asDouble(0.0);
        clip.fadeOutBeats = (float)cj["fadeOutBeats"].asDouble(0.0);

        if (clip.type == ClipType::Pattern) {
          // patternSlot was remapped once already when patterns were
          // loaded above (see fileSlotToRealSlot) — apply the same
          // remap here so a clip points at the right live slot, and
          // drop it if it referenced a pattern slot the file never
          // defined (mirrors the arrangement-loading behavior above).
          auto slotIt = fileSlotToRealSlot.find(cj["patternSlot"].asInt(-1));
          if (slotIt == fileSlotToRealSlot.end())
            continue;
          clip.patternSlot = slotIt->second;
        } else {
          clip.patternSlot = -1; // AudioClip — not pattern-backed
        }

        maxClipId = std::max(maxClipId, (uint64_t)clip.id);
        tt.clips.push_back(clip);
      }
      _timeline.tracks.push_back(std::move(tt));
    }
    _nextClipId = maxClipId + 1;
    _nextTimelineTrackId = maxTimelineTrackId + 1;

    // ── Aux buses ─────────────────────────────────────────────────────
    // Tear down whatever aux buses were in memory before this load, then
    // recreate from the file — must happen before track sends are
    // resolved below, since a saved sendIndex only makes sense against
    // the new (post-load) _auxBuses list.
    for (BusID b : _auxBuses)
      AudioEngine::get().destroyBus(b);
    _auxBuses.clear();
    _auxBusNames.clear();
    std::vector<double> loadedAuxGains;
    std::vector<LoadedFilter> loadedAuxFilter;
    for (const auto &bj : root["auxBuses"].arr) {
      BusID b = AudioEngine::get().createBus();
      if (b == kInvalidBus)
        break; // pool exhausted — rest of the file's buses are dropped
      double gain = bj["gain"].asDouble(1.0);
      AudioEngine::get().setBusGain(b, (float)gain);


      LoadedFilter lf;
      lf.enabled = bj["filterEnabled"].asBool(false);
      lf.type = (FilterType)bj["filterType"].asInt(0);
      lf.cutoff = (float)bj["filterCutoff"].asDouble(1000.0);
      lf.q = (float)bj["filterQ"].asDouble(0.707);
      AudioEngine::get().setBusFilterInsert(b, kFilterInsertSlot, lf.enabled,
                                            lf.type, lf.cutoff, lf.q);
      lf.compEnabled = bj["compEnabled"].asBool(false);
      lf.compThreshold = (float)bj["compThreshold"].asDouble(-18.0);
      lf.compRatio = (float)bj["compRatio"].asDouble(4.0);
      lf.compAttack = (float)bj["compAttack"].asDouble(10.0);
      lf.compRelease = (float)bj["compRelease"].asDouble(100.0);
      lf.compMakeup = (float)bj["compMakeup"].asDouble(0.0);
      AudioEngine::get().setBusCompressorInsert(
          b, kCompressorInsertSlot, lf.compEnabled, lf.compThreshold,
          lf.compRatio, lf.compAttack, lf.compRelease, lf.compMakeup);
      loadedAuxFilter.push_back(lf);

      _auxBuses.push_back(b);
      _auxBusNames.push_back(
          bj["name"].asString("Aux " + std::to_string(_auxBuses.size())));
      loadedAuxGains.push_back(gain);
    }
    _rebuildAuxBusStrips();
    for (size_t i = 0; i < loadedAuxGains.size(); i++) {
      _auxBusVolumeState[i].set(loadedAuxGains[i]);
      _auxFilterEnabledState[i].set(loadedAuxFilter[i].enabled);
      _auxFilterTypeState[i].set((int)loadedAuxFilter[i].type);
      _auxFilterCutoffState[i].set((double)loadedAuxFilter[i].cutoff);
      _auxFilterQState[i].set((double)loadedAuxFilter[i].q);
      _auxCompEnabledState[i].set(loadedAuxFilter[i].compEnabled);
      _auxCompThresholdState[i].set((double)loadedAuxFilter[i].compThreshold);
      _auxCompRatioState[i].set((double)loadedAuxFilter[i].compRatio);
      _auxCompAttackState[i].set((double)loadedAuxFilter[i].compAttack);
      _auxCompReleaseState[i].set((double)loadedAuxFilter[i].compRelease);
      _auxCompMakeupState[i].set((double)loadedAuxFilter[i].compMakeup);
    }

    _rebuildMixerStrips();
    for (size_t i = 0; i < _timeline.tracks.size(); i++) {
      TrackID et = _timeline.tracks[i].engineTrack;
      _timelineVolumeState[i].set((double)AudioEngine::get().getTrackGain(et));
      _timelinePanState[i].set((double)AudioEngine::get().getTrackPan(et));

      int sendIdx = (i < loadedSendIndex.size()) ? loadedSendIndex[i] : 0;
      BusID bus = (sendIdx <= 0 || sendIdx - 1 >= (int)_auxBuses.size())
                      ? kMasterBus
                      : _auxBuses[sendIdx - 1];
      AudioEngine::get().setTrackSendBus(et, bus);
      _timelineSendIndexState[i].set(sendIdx);

      if (i < loadedTrackFilter.size()) {
        _timelineFilterEnabledState[i].set(loadedTrackFilter[i].enabled);
        _timelineFilterTypeState[i].set((int)loadedTrackFilter[i].type);
        _timelineFilterCutoffState[i].set((double)loadedTrackFilter[i].cutoff);
        _timelineFilterQState[i].set((double)loadedTrackFilter[i].q);
        _timelineCompEnabledState[i].set(loadedTrackFilter[i].compEnabled);
        _timelineCompThresholdState[i].set(
            (double)loadedTrackFilter[i].compThreshold);
        _timelineCompRatioState[i].set((double)loadedTrackFilter[i].compRatio);
        _timelineCompAttackState[i].set(
            (double)loadedTrackFilter[i].compAttack);
        _timelineCompReleaseState[i].set(
            (double)loadedTrackFilter[i].compRelease);
        _timelineCompMakeupState[i].set(
            (double)loadedTrackFilter[i].compMakeup);
      }
    }
    _selectedClipId = 0;
    if (_timelineSurface)
      _timelineSurface->selectedClip = 0;
    _timelineSelectionLabel.set("No clip selected");
    if (_timelineCanvas) {
      _timelineCanvas->setCanvasSize(_timelineCanvasWidthPx(),
                                     _timelineCanvasHeightPx());
      _timelineCanvas->redraw();
    }

    _undoStack.clear(); // a freshly loaded project starts with a clean history
  }

  WidgetPtr _buildArrangementChip(const ArrangementEntry &entry) {
    return Row({
                   Text(_seq->patternSlots[entry.patternSlot].name)
                       ->setFontSize(12),
                   Button("-",
                          [this, id = entry.id] {
                            _adjustArrangementRepeat(id, -1);
                          })
                       ->setWidth(18)
                       ->setHeight(20)
                       ->setBorderRadius(4)
                       ->setBackgroundColor(Color::fromRGB(230, 230, 230)),
                   Text("×" + std::to_string(entry.repeatCount))
                       ->setFontSize(11),
                   Button("+",
                          [this, id = entry.id] {
                            _adjustArrangementRepeat(id, 1);
                          })
                       ->setWidth(18)
                       ->setHeight(20)
                       ->setBorderRadius(4)
                       ->setBackgroundColor(Color::fromRGB(230, 230, 230)),
                   Button("x",
                          [this, id = entry.id] {
                            _removeArrangementEntryById(id);
                          })
                       ->setWidth(20)
                       ->setHeight(20)
                       ->setBorderRadius(4)
                       ->setBackgroundColor(Color::fromRGB(230, 230, 230)),
               })
        ->setGap(6)
        ->setAlignItems(AlignItems::Center)
        ->setPaddingHV(8, 4)
        ->setBackgroundColor(Color::fromRGB(240, 240, 250))
        ->setBorderRadius(6);
  }

  WidgetPtr build() override {
    std::vector<WidgetPtr> trackRows;

    for (size_t t = 0; t < _seq->trackVoice.size(); t++) {
      std::vector<WidgetPtr> stepButtons;
      for (int s = 0; s < StepScheduler::kMaxSteps; s++) {
        bool downbeatAccent = (s % 4 == 0);

        auto cell =
            Button("",
                   [this, t, s] {
                     int slot = _seq->editingSlot;
                     Pattern &pat = _seq->patternSlots[slot];
                     if (s >= pat.numSteps)
                       return; // beyond the pattern's current length — inert
                     bool oldVal = pat.steps[t][s].on;
                     bool newVal = !oldVal;
                     pat.steps[t][s].on = newVal;
                     _cellState[t][s].set(newVal);
                     _undoStack.push(
                         [this, slot, t, s, oldVal] {
                           _seq->patternSlots[slot].steps[t][s].on = oldVal;
                           if (_seq->editingSlot == slot)
                             _cellState[t][s].set(oldVal);
                         },
                         [this, slot, t, s, newVal] {
                           _seq->patternSlots[slot].steps[t][s].on = newVal;
                           if (_seq->editingSlot == slot)
                             _cellState[t][s].set(newVal);
                         });
                   })
                ->setWidth(28)
                ->setHeight(28)
                ->setBorderRadius(4)
                ->setBackgroundColor(
                    _cellState[t][s],
                    [this, t, s](bool) { return _colorForCell(t, s); })
                ->setBackgroundColor(
                    _velocitySliderState[t][s],
                    [this, t, s](double) { return _colorForCell(t, s); })
                // Whenever the playhead moves, every cell recomputes
                // its color the same way, so red clears/appears
                // correctly as it passes.
                ->setBackgroundColor(_currentStepState, [this, t, s](int) {
                  return _colorForCell(t, s);
                });

        if (downbeatAccent)
          cell->setBorderRadius(4);

        auto cellWithMenu = ContextMenu(
            cell,
            {
                ContextMenuItem::Widget(
                    Column(
                        {
                            Row({
                                    Button("Copy",
                                           [this, t, s] { _copyStep(t, s); })
                                        ->setWidth(64)
                                        ->setHeight(22)
                                        ->setBorderRadius(4)
                                        ->setBackgroundColor(
                                            Color::fromRGB(230, 230, 230)),
                                    Button("Paste",
                                           [this, t, s] { _pasteStep(t, s); })
                                        ->setWidth(64)
                                        ->setHeight(22)
                                        ->setBorderRadius(4)
                                        ->setBackgroundColor(
                                            Color::fromRGB(230, 230, 230)),
                                })
                                ->setGap(6),
                            Text("Velocity")->setFontSize(11),
                            Slider(0.0, 1.0, 0.05)
                                ->setValue(_velocitySliderState[t][s])
                                ->setWidth(140)
                                ->setOnValueChanged([this, t, s](double v) {
                                  _seq->patternSlots[_seq->editingSlot]
                                      .steps[t][s]
                                      .velocity = (float)v;
                                  _velocitySliderState[t][s].set(v);
                                }),
                            Text("Pitch (semitones)")->setFontSize(11),
                            Slider(-24.0, 24.0, 1.0)
                                ->setValue(_pitchSliderState[t][s])
                                ->setWidth(140)
                                ->setOnValueChanged([this, t, s](double v) {
                                  _seq->patternSlots[_seq->editingSlot]
                                      .steps[t][s]
                                      .pitchSemitones = (float)v;
                                  _pitchSliderState[t][s].set(v);
                                }),
                            Text("Probability")->setFontSize(11),
                            Slider(0.0, 1.0, 0.05)
                                ->setValue(_probabilitySliderState[t][s])
                                ->setWidth(140)
                                ->setOnValueChanged([this, t, s](double v) {
                                  _seq->patternSlots[_seq->editingSlot]
                                      .steps[t][s]
                                      .probability = (float)v;
                                  _probabilitySliderState[t][s].set(v);
                                }),
                            Text("Micro-timing")->setFontSize(11),
                            Slider(-0.5, 0.5, 0.05)
                                ->setValue(_microTimingSliderState[t][s])
                                ->setWidth(140)
                                ->setOnValueChanged([this, t, s](double v) {
                                  _seq->patternSlots[_seq->editingSlot]
                                      .steps[t][s]
                                      .microTiming = (float)v;
                                  _microTimingSliderState[t][s].set(v);
                                }),
                        })
                        ->setGap(4)
                        ->setPadding(8)),
            });

        stepButtons.push_back(cellWithMenu);
      }

      auto trackHeaderTop =
          Row({
                  Text(_instrumentNameState[t])
                      ->setFontSize(12)
                      ->setWidth(120)
                      ->setOverflow(TextOverflow::Ellipsis)
                      ->setMaxLines(1),
                  FilePicker()
                      ->setMode(FilePickerMode::Open)
                      ->setTitle("Load instrument sample")
                      ->addFilter("Audio",
                                  {"*.wav", "*.mp3", "*.ogg", "*.flac"})
                      ->setWidth(80)
                      ->setOnChanged([this, t](const std::string &path) {
                        _loadTrackSample(t, path);
                      }),

                  ContextMenu(
                      Button("Synth", [] {})
                          ->setWidth(56)
                          ->setHeight(24)
                          ->setBorderRadius(4)
                          ->setBackgroundColor(Color::fromRGB(235, 235, 245)),
                      {
                          ContextMenuItem::Widget(
                              Column(
                                  {
                                      Text("Waveform")->setFontSize(11),
                                      Dropdown(
                                          {"Sine", "Saw", "Square", "Triangle"})
                                          ->setSelectedIndex(_waveformState[t])
                                          ->setWidth(120)
                                          ->setOnSelectionChanged(
                                              [this, t](int idx,
                                                        const std::string &) {
                                                _seq->trackVoice[t].waveform =
                                                    (OscWaveform)idx;
                                                _waveformState[t].set(idx);
                                              }),
                                      Text("Attack (s)")->setFontSize(11),
                                      Slider(0.0, 2.0, 0.01)
                                          ->setValue(_attackState[t])
                                          ->setWidth(140)
                                          ->setOnValueChanged(
                                              [this, t](double v) {
                                                _seq->trackVoice[t].attackSec =
                                                    (float)v;
                                                _attackState[t].set(v);
                                              }),
                                      Text("Decay (s)")->setFontSize(11),
                                      Slider(0.0, 2.0, 0.01)
                                          ->setValue(_decayState[t])
                                          ->setWidth(140)
                                          ->setOnValueChanged(
                                              [this, t](double v) {
                                                _seq->trackVoice[t].decaySec =
                                                    (float)v;
                                                _decayState[t].set(v);
                                              }),
                                      Text("Sustain level")->setFontSize(11),
                                      Slider(0.0, 1.0, 0.05)
                                          ->setValue(_sustainLevelState[t])
                                          ->setWidth(140)
                                          ->setOnValueChanged([this,
                                                               t](double v) {
                                            _seq->trackVoice[t].sustainLevel =
                                                (float)v;
                                            _sustainLevelState[t].set(v);
                                          }),
                                      Text("Sustain hold (s)")->setFontSize(11),
                                      Slider(0.0, 2.0, 0.01)
                                          ->setValue(_sustainHoldState[t])
                                          ->setWidth(140)
                                          ->setOnValueChanged(
                                              [this, t](double v) {
                                                _seq->trackVoice[t].sustainSec =
                                                    (float)v;
                                                _sustainHoldState[t].set(v);
                                              }),
                                      Text("Release (s)")->setFontSize(11),
                                      Slider(0.0, 2.0, 0.01)
                                          ->setValue(_releaseState[t])
                                          ->setWidth(140)
                                          ->setOnValueChanged(
                                              [this, t](double v) {
                                                _seq->trackVoice[t].releaseSec =
                                                    (float)v;
                                                _releaseState[t].set(v);
                                              }),
                                  })
                                  ->setGap(4)
                                  ->setPadding(8)),
                      }),
              })
              ->setGap(8)
              ->setAlignItems(AlignItems::Center);

      auto trackHeaderControls =
          Row({
                  Toggle("M")
                      ->setValue(_muteState[t])
                      ->setOnToggleChanged(
                          [this, t](bool v) { _seq->trackMuted[t] = v; }),
                  Toggle("S")
                      ->setValue(_soloState[t])
                      ->setOnToggleChanged(
                          [this, t](bool v) { _seq->trackSoloed[t] = v; }),
                  Slider(0.0, 1.5, 0.05)
                      ->setValue(_trackVolumeState[t])
                      ->setWidth(70)
                      ->setOnValueChanged([this, t](double v) {
                        _seq->trackVoice[t].gain = (float)v;
                      }),
                  Slider(-1.0, 1.0, 0.1)
                      ->setValue(_trackPanState[t])
                      ->setWidth(70)
                      ->setOnValueChanged([this, t](double v) {
                        _seq->trackVoice[t].pan = (float)v;
                      }),
              })
              ->setGap(6)
              ->setAlignItems(AlignItems::Center);

      auto trackHeader = Column({trackHeaderTop, trackHeaderControls})
                             ->setGap(4)
                             ->setWidth(230);

      trackRows.push_back(Row({trackHeader, Row({stepButtons})->setGap(4)})
                              ->setGap(12)
                              ->setAlignItems(AlignItems::Center));
    }

    // ── Pattern bar ──────────────────────────────────────────────────────
    std::vector<std::string> initialLabels;
    for (int slot : _seq->activeSlots)
      initialLabels.push_back(_seq->patternSlots[slot].name);

    _patternDropdown =
        Dropdown(initialLabels)
            ->setSelectedIndex(_dropdownIndexState)
            ->setOnSelectionChanged([this](int idx, const std::string &) {
              if (idx < 0 || idx >= (int)_seq->activeSlots.size())
                return;
              _seq->setEditingPattern(_seq->activeSlots[idx]);
              _refreshGridFromPattern();
            })
            ->setWidth(160);

    auto patternBar =
        Row({
                Text("Pattern:"),
                _patternDropdown,
                Button("+ New", [this] { _createPattern(); }),
                Button("Duplicate", [this] { _duplicateCurrentPattern(); }),
                Button("Delete", [this] { _deleteCurrentPattern(); }),
                Text("Len:"),
                NumberInput(1.0, (double)StepScheduler::kMaxSteps, 1.0)
                    ->setValue(_patternLengthState)
                    ->setWidth(70)
                    ->setOnValueChanged([this](double v) {
                      int slot = _seq->editingSlot;
                      int oldLen = _seq->patternSlots[slot].numSteps;
                      int newLen = (int)v;
                      if (newLen == oldLen)
                        return;
                      _seq->setPatternLength(slot, newLen);
                      _patternLengthState.set(newLen);
                      _undoStack.push(
                          [this, slot, oldLen] {
                            _seq->setPatternLength(slot, oldLen);
                            if (_seq->editingSlot == slot)
                              _patternLengthState.set(oldLen);
                          },
                          [this, slot, newLen] {
                            _seq->setPatternLength(slot, newLen);
                            if (_seq->editingSlot == slot)
                              _patternLengthState.set(newLen);
                          });
                    }),
                Text("Subdiv:"),
                NumberInput(1.0, 8.0, 1.0)
                    ->setValue(_patternSpbState)
                    ->setWidth(60)
                    ->setOnValueChanged([this](double v) {
                      int slot = _seq->editingSlot;
                      _seq->setPatternStepsPerBeat(slot, (int)v);
                      _patternSpbState.set((int)v);
                      // Not undo-tracked: subdivision is a "feel" tweak users
                      // iterate on rapidly via the spinner, same reasoning as
                      // sliders being excluded above.
                    }),
                Text("Swing:"),
                NumberInput(0.0, 75.0, 1.0)
                    ->setValue(_patternSwingState)
                    ->setWidth(70)
                    ->setOnValueChanged([this](double v) {
                      int slot = _seq->editingSlot;
                      _seq->setPatternSwing(slot, (float)v);
                      _patternSwingState.set(_seq->patternSlots[slot].swing);
                      // Not undo-tracked — same "feel" reasoning as Subdiv.
                    }),

                Text(_currentStepState,
                     [this](int) {
                       return "Playing: " +
                              _seq->patternSlots[_seq->playingSlot].name;
                     })
                    ->setFontSize(12),
            })
            ->setGap(8)
            ->setAlignItems(AlignItems::Center);

    // ── Arrangement bar (song mode) ─────────────────────────────────────
    auto arrangementList =
        Box({
                Map<ArrangementEntry>(
                    _arrangementState,
                    [](int, const ArrangementEntry &e) {
                      return FlexItemKey::fromInt64((int64_t)e.id);
                    },
                    [this](int, const ArrangementEntry &e) {
                      return _buildArrangementChip(e);
                    }),
            })
            ->setDisplay(Display::Flex)
            ->setDirection(FlexDirection::Row)
            ->setWrap(FlexWrap::Wrap)
            ->setGap(6);

    auto arrangementBar =
        Column({
                   Row({
                           Toggle("Song Mode")
                               ->setValue(_songModeState)
                               ->setOnToggleChanged(
                                   [this](bool v) { _seq->setSongMode(v); }),
                           Button("+ Add current pattern to song",
                                  [this] {
                                    _addToArrangement(_seq->editingSlot);
                                  }),
                       })
                       ->setGap(12)
                       ->setAlignItems(AlignItems::Center),
                   arrangementList,
               })
            ->setGap(8);

    // ── Timeline (Phase 3) ────────────────────────────────────────────
    if (!_timelineCanvas) {
      _timelineCanvas = Canvas(900, 260);
      _timelineCanvas->setScrollbarsEnabled(true);
      _timelineCanvas->setViewportEnabled(true);
      _timelineCanvas->setCanvasSize(_timelineCanvasWidthPx(),
                                     _timelineCanvasHeightPx());

      _timelineSurface = _timelineCanvas->setSurface<TimelineSurface>();
      _timelineSurface->timeline = &_timeline;
      _timelineSurface->seq = _seq.get();
      _timelineSurface->scheduler = _timelineScheduler.get();
      _timelineSurface->onEmptyClick = [this](int idx, double beat) {
        _timelineAddClipAt(idx, beat);
      };
      _timelineSurface->onClipClick = [this](ClipID id) {
        _timelineSelectClip(id);
      };

      _timelineSurface->onFadeDragStart = [this](ClipID id, bool isFadeIn) {
        _timelineFadeDragStart(id, isFadeIn);
      };
      _timelineSurface->onFadeDrag = [this](ClipID id, bool isFadeIn,
                                            double beats) {
        _timelineSetFadeCore(id, isFadeIn, (float)beats);
      };
      _timelineSurface->onFadeDragEnd = [this](ClipID id, bool isFadeIn) {
        _timelineFadeDragEnd(id, isFadeIn);
      };

      _timelineSurface->onTrimDragStart = [this](ClipID id, bool isLeft) {
        _timelineTrimDragStart(id, isLeft);
      };
      _timelineSurface->onTrimDrag = [this](ClipID id, bool isLeft,
                                            double absBeat) {
        _timelineSetTrimCore(id, isLeft, absBeat);
      };
      _timelineSurface->onTrimDragEnd = [this](ClipID id, bool isLeft) {
        _timelineTrimDragEnd(id, isLeft);
      };

      _timelineSurface->onAutomationPointAdd =
          [this](int ti, AutomationParam p, double beat, float value) {
            _timelineAutomationPointAdd(ti, p, beat, value);
          };
      _timelineSurface->onAutomationPointDragStart =
          [this](int ti, AutomationParam p, int idx) {
            _timelineAutomationPointDragStart(ti, p, idx);
          };
      _timelineSurface->onAutomationPointDrag =
          [this](int ti, AutomationParam p, int idx, float value) {
            _timelineAutomationPointDragCore(ti, p, idx, value);
          };
      _timelineSurface->onAutomationPointDragEnd =
          [this](int ti, AutomationParam p, int idx, bool deleted) {
            _timelineAutomationPointDragEnd(ti, p, idx, deleted);
          };
    }

    if (!_mixerStripsRow) {
      _mixerStripsRow = Row({})->setGap(8)->setAlignItems(AlignItems::Start);
      // Any tracks already added before this first build() call (there
      // aren't any yet — SequencerApp starts with zero timeline tracks —
      // but this keeps _appendChannelStrip/_mixerStripsRow construction
      // order-independent) get their strips built now.
      for (size_t i = 0; i < _timeline.tracks.size(); i++)
        _appendChannelStrip(i);
    }

    if (!_auxBusStripsRow) {
      _auxBusStripsRow = Row({})->setGap(8)->setAlignItems(AlignItems::Start);
      for (size_t i = 0; i < _auxBuses.size(); i++)
        _appendAuxBusStrip(i);
    }

    auto mixerSection =
        Column({
                   Row({
                           Text("Mixer")->setFontWeight(FontWeight::Bold),
                           Button("+ Aux Bus",
                                  [this] {
                                    BusID b = AudioEngine::get().createBus();
                                    if (b == kInvalidBus)
                                      return; // pool exhausted — silent
                                              // no-op, same policy every
                                              // other bool/ID-returning
                                              // engine call gets here
                                    _auxBuses.push_back(b);
                                    _auxBusNames.push_back(
                                        "Aux " +
                                        std::to_string(_auxBuses.size()));
                                    for (auto &dd : _sendDropdowns)
                                      dd->setOptions(_sendBusLabels());
                                      _appendAuxBusStrip(_auxBuses.size() - 1);
                                  }),
                       })
                       ->setGap(12)
                       ->setAlignItems(AlignItems::Center),
                   _mixerStripsRow,
                   Text("Aux Returns")
                       ->setFontSize(12)
                       ->setFontWeight(FontWeight::Bold),
                   _auxBusStripsRow,

               })
            ->setGap(8);

    // Built once alongside the rest of the timeline toolbar (build() only
    // runs once — see the comment on _cellState) so _recordTrackDropdown
    // is available for _syncRecordTrackDropdown() to update later.
    std::vector<std::string> recordTrackLabels;
    for (auto &t : _timeline.tracks)
      recordTrackLabels.push_back(t.name);
    _recordTrackDropdown =
        Dropdown(recordTrackLabels)
            ->setOnSelectionChanged([this](int idx, const std::string &) {
              _recordTargetTrackIndex = idx;
            })
            ->setWidth(140);

    auto timelineToolbar =
        Row({
                Text("Timeline:")->setFontWeight(FontWeight::Bold),
                Button("+ Add Track", [this] { _addTimelineTrack(); }),
                FilePicker("Load Audio…")
                    ->setMode(FilePickerMode::Open)
                    ->addFilter("Audio", {"*.wav", "*.mp3", "*.ogg", "*.flac"})
                    ->setWidth(120)
                    ->setOnChanged([this](const std::string &path) {
                      _pendingAudioClipPath = path;
                      size_t slash = path.find_last_of("/\\");
                      _timelineSelectionLabel.set(
                          "Click the timeline to place: " +
                          (slash == std::string::npos
                               ? path
                               : path.substr(slash + 1)));
                    }),
                Button("▶ Play", [this] { _timelineScheduler->start(); }),
                Button("■ Stop", [this] { _timelineScheduler->stop(); }),
                Button("Delete Clip",
                       [this] { _deleteSelectedTimelineClip(); }),
                Text("Record to:"),
                _recordTrackDropdown,
                Button("● Record", [this] { _startRecording(); })
                    ->setWidth(90)
                    ->setBackgroundColor(_recordingState,
                                         [](bool rec) {
                                           return rec ? Color::fromRGB(220, 50,
                                                                       50)
                                                      : Color::fromRGB(235, 235,
                                                                       245);
                                         }),
                Button("■ Stop Rec", [this] { _stopRecording(); }),

                Text(_timelineSelectionLabel)->setFontSize(12),
            })
            ->setGap(8)
            ->setAlignItems(AlignItems::Center);

    auto timelineSection =
        Column({timelineToolbar, _timelineCanvas})->setGap(8);

    auto transportRow =
        Row({
                Button("▶ Play",
                       [this] {
                         _seq->start();
                         _timelineScheduler->start();
                       }),
                Button("■ Stop",
                       [this] {
                         _seq->stop();
                         _timelineScheduler->stop();
                         _currentStepState.set(-1);
                       }),
                Text("BPM:"),
                NumberInput(40.0, 240.0, 1.0)
                    ->setValue(_bpmState)
                    ->setWidth(90)
                    ->setOnValueChanged([this](double v) {
                      double old = _seq->bpm;
                      if (v == old)
                        return;
                      _seq->bpm = v;
                      _undoStack.push(
                          [this, old] {
                            _seq->bpm = old;
                            _bpmState.set(old);
                          },
                          [this, v] {
                            _seq->bpm = v;
                            _bpmState.set(v);
                          });
                    }),
                Button("Undo", [this] { _undoStack.undo(); }),
                Button("Redo", [this] { _undoStack.redo(); }),
                FilePicker("Save Project")
                    ->setMode(FilePickerMode::Save)
                    ->setDefaultFilename("project.fluxseq")
                    ->setDefaultExtension("fluxseq")
                    ->addFilter("Flux Sequencer Project", {"*.fluxseq"})
                    ->setOnChanged([this](const std::string &path) {
                      _saveProject(path);
                    }),
                FilePicker("Load Project")
                    ->setMode(FilePickerMode::Open)
                    ->addFilter("Flux Sequencer Project", {"*.fluxseq"})
                    ->setOnChanged([this](const std::string &path) {
                      _loadProject(path);
                    }),
            })
            ->setGap(12)
            ->setAlignItems(AlignItems::Center);

    if (!_timerId) {
      _timerId = FluxUI::getCurrentInstance()->setInterval(25, [this] {
        _seq->tick();
        _currentStepState.set(_seq->currentStep);
        _timelineScheduler->tick();
        if (_timelineCanvas && _timelineScheduler->playing)
          _timelineCanvas->redraw();
        for (size_t i = 0; i < _timeline.tracks.size(); i++) {
          if (_timeline.tracks[i].engineTrack == kInvalidTrack)
            continue;
          _timelinePeakState[i].set(AudioEngine::get().getTrackPeakLevel(
              _timeline.tracks[i].engineTrack));
        }
        for (size_t i = 0; i < _auxBuses.size(); i++)
          _auxBusPeakState[i].set(
              AudioEngine::get().getBusPeakLevel(_auxBuses[i]));
      });
    }

    return Column({transportRow, patternBar, arrangementBar,
                   Column({trackRows})->setGap(6), timelineSection,
                   mixerSection})
        ->setGap(16)
        ->setPadding(24);
  }
};

WidgetPtr createApp(FluxUI *app) {
  return FluxApp()
      .setTheme(AppTheme::light())
      .build(std::make_shared<SequencerApp>());
}