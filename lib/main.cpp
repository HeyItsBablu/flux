#include "flux/flux.hpp"
#include "flux/flux_audio_engine.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

#include <cctype>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <memory>

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
};

// Absolute cap every pattern's step vectors are allocated to, regardless of
// that pattern's current logical length (Pattern::numSteps). This means
// changing a pattern's length is just an int assignment — no resize, no
// out-of-bounds risk, and shrinking-then-growing a pattern doesn't lose
// steps programmed beyond the shorter length; they're just inert while
// hidden. See StepScheduler::kMaxSteps for the same constant, scoped there
// for code that already includes this header transitively.
static constexpr int kSeqMaxSteps = 64;


struct StepHit {
  float freqHz;
  float gain;
  float pan;

  // synth test tone. Per-step pitch (StepData::pitchSemitones) applies to
  // sample playback too, via AudioEngine::play()'s pitchRatio argument.
  SampleID sampleId = kInvalidSample;
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
  int numSteps = 16;      // logical length in steps — <= kSeqMaxSteps.
                           // Acts as this pattern's "time signature" length.
  int stepsPerBeat = 4;    // subdivision: 4 = 16th notes, 3 = triplet feel,
                           // etc. Together with numSteps this is the
                           // per-pattern stand-in for a full time signature.
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
        patternSlots[i].steps.assign(_numTracks, std::vector<StepData>(kMaxSteps));
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
      patternSlots[dst].steps = patternSlots[srcSlot].steps; // deep copy of step data
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
        fireStep(trackVoice[t], step, _nextStepFrame);
      }

      currentStep++;
      if (currentStep >= patternLen) {
        currentStep = 0;
        _advancePlayback(); // pattern boundary — advance the song if in song
                            // mode
      }
      _nextStepFrame += _framesPerStep(pat.stepsPerBeat);
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
                       uint64_t targetFrame) {

    auto &engine = AudioEngine::get();
    float velocity = std::max(0.f, std::min(1.f, step.velocity));
    float effectiveGain = hit.gain * velocity;

    if (engine.isSampleValid(hit.sampleId)) {
      // Sample-backed track: sample-accurate one-shot, velocity applied
      // as gain, pitch via pitchRatio.
      float pitchRatio = std::pow(2.0f, step.pitchSemitones / 12.0f);
      engine.play(hit.sampleId, effectiveGain, hit.pan, /*loop=*/false,
                  pitchRatio, targetFrame);
      return;
    }

    _fireSynthStep(hit, step, effectiveGain, targetFrame);
  }
private:

  // Bundles the two pieces of per-note state the envelope callback needs.
  // A single make_shared<> here replaces the previous pair of separate
  // shared_ptr<float>/shared_ptr<int> allocations per note fired.
  struct SynthNoteState {
    float phase = 0.f;
    int samplesLeft = 0;
  };

  static void _fireSynthStep(const StepHit &hit, const StepData &step,
                             float effectiveGain, uint64_t targetFrame) {
    auto &engine = AudioEngine::get();
    auto state = std::make_shared<SynthNoteState>();

    // Pitch shift: each semitone is a factor of 2^(1/12).
    float pitchedFreq =
        hit.freqHz * std::pow(2.0f, step.pitchSemitones / 12.0f);
    float phaseInc =
        (2.0f * 3.14159265f * pitchedFreq) / (float)engine.sampleRate();

    // Simple decaying envelope so each hit sounds like a "note" instead
    // of an infinite drone — decays over ~150ms then goes silent.
    state->samplesLeft = (int)(engine.sampleRate() * 0.15);
    int totalSamples = state->samplesLeft;

    AudioEngine::StreamCallback cb =
        [state, phaseInc, totalSamples](float *buf, int frames) -> int {
      if (state->samplesLeft <= 0)
        return -1; // fully decayed — engine frees this voice slot

      for (int i = 0; i < frames; i++) {
        float env =
            (float)state->samplesLeft / (float)totalSamples; // linear decay
        buf[i] = std::sin(state->phase) * env * 0.3f;
        state->phase += phaseInc;
        state->samplesLeft--;
      }
      return frames;
    };

    // Fire-and-forget: the envelope signals its own completion (negative
    // return), so the engine frees the voice slot once the decay finishes.
    engine.playStream(cb, engine.sampleRate(), effectiveGain, hit.pan,
                      targetFrame);
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

struct TimelineTrack {
  TimelineTrackID id = 0;
  std::string name;
  TrackID engineTrack = kInvalidTrack; // reserved for per-clip engine
                                       // routing — not wired yet, see
                                       // TimelineScheduler::tick()
  std::vector<Clip> clips;
  bool muted = false;
  bool soloed = false;
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

  Timeline timeline;
  bool playing = false;
  int64_t currentPulse = 0;

  // References StepScheduler's pattern storage (patternSlots) and
  // instrument tracks (trackVoice/trackMuted/trackSoloed) directly rather
  // than duplicating them — patterns are shared data between the classic
  // pattern-chain transport and the timeline transport, per the roadmap's
  // "keep the old step-grid behavior working as PatternClip".
  explicit TimelineScheduler(StepScheduler &seq) : _seq(seq) {}

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
        // TODO(engine routing): steps fire via StepScheduler::fireStep(),
        // which always calls AudioEngine::play()/playStream() with
        // track=kInvalidTrack (straight to master) — track.engineTrack is
        // allocated but not yet threaded through. Per-instrument audio
        // (StepHit) has no TrackID of its own either, so wiring this up
        // means deciding whether routing is per-TimelineTrack or
        // per-instrument-row first; left as a follow-up rather than
        // guessing. Mute/solo above work regardless, since they gate
        // whether steps fire at all, not how they're mixed.
        for (auto &clip : track.clips) {
          if (clip.type != ClipType::Pattern)
            continue; // AudioClip playback not implemented yet
          _tickPatternClip(clip, _nextPulseFrame);
        }
      }

      currentPulse++;
      _nextPulseFrame += _framesPerPulse();
    }
  }

private:
  StepScheduler &_seq;
  uint64_t _nextPulseFrame = 0;

  uint64_t _framesPerPulse() const {
    double secondsPerBeat = 60.0 / _seq.bpm; // shares StepScheduler's bpm —
                                             // one tempo for the whole app
    double secondsPerPulse = secondsPerBeat / kPPQ;
    uint64_t frames =
        (uint64_t)(secondsPerPulse * AudioEngine::get().sampleRate());
    return std::max<uint64_t>(1, frames);
  }

  void _tickPatternClip(const Clip &clip, uint64_t targetFrame) {
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

    bool anySoloed = std::any_of(_seq.trackSoloed.begin(), _seq.trackSoloed.end(),
                                 [](bool b) { return b; });
    for (size_t t = 0; t < pat.steps.size() && t < _seq.trackVoice.size(); t++) {
      const StepData &step = pat.steps[t][stepIndex];
      if (!step.on)
        continue;
      bool audible = !_seq.trackMuted[t] && (!anySoloed || _seq.trackSoloed[t]);
      if (!audible)
        continue;
      if (step.probability < 1.0f &&
          (float)std::rand() / (float)RAND_MAX > step.probability)
        continue;
      StepScheduler::fireStep(_seq.trackVoice[t], step, targetFrame);
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
    case '"': out += "\\\""; break;
    case '\\': out += "\\\\"; break;
    case '\n': out += "\\n"; break;
    case '\r': out += "\\r"; break;
    case '\t': out += "\\t"; break;
    default: out += c;
    }
  }
  return out;
}

struct JVal {
  enum class Type { Null, Bool, Number, String, Array, Object } type = Type::Null;
  double num = 0;
  bool boolean = false;
  std::string str;
  std::vector<JVal> arr;
  std::vector<std::pair<std::string, JVal>> obj;

  double asDouble(double def = 0) const { return type == Type::Number ? num : def; }
  int asInt(int def = 0) const { return type == Type::Number ? (int)num : def; }
  bool asBool(bool def = false) const { return type == Type::Bool ? boolean : def; }
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
  bool parse(JVal &out) { _skipWs(); return _parseValue(out); }

private:
  const std::string &_s;
  size_t _i = 0;

  void _skipWs() { while (_i < _s.size() && std::isspace((unsigned char)_s[_i])) _i++; }
  char _peek() const { return _i < _s.size() ? _s[_i] : '\0'; }
  bool _consume(char c) { _skipWs(); if (_peek() != c) return false; _i++; return true; }

  bool _parseValue(JVal &out) {
    _skipWs();
    char c = _peek();
    if (c == '{') return _parseObject(out);
    if (c == '[') return _parseArray(out);
    if (c == '"') return _parseString(out);
    if (c == 't' || c == 'f') return _parseBool(out);
    if (c == 'n') { _i += 4; out.type = JVal::Type::Null; return true; }
    return _parseNumber(out);
  }

  bool _parseObject(JVal &out) {
    if (!_consume('{')) return false;
    out.type = JVal::Type::Object;
    _skipWs();
    if (_peek() == '}') { _i++; return true; }
    while (true) {
      _skipWs();
      JVal key;
      if (!_parseString(key)) return false;
      if (!_consume(':')) return false;
      JVal val;
      if (!_parseValue(val)) return false;
      out.obj.emplace_back(key.str, std::move(val));
      _skipWs();
      if (_peek() == ',') { _i++; continue; }
      if (_peek() == '}') { _i++; break; }
      return false;
    }
    return true;
  }

  bool _parseArray(JVal &out) {
    if (!_consume('[')) return false;
    out.type = JVal::Type::Array;
    _skipWs();
    if (_peek() == ']') { _i++; return true; }
    while (true) {
      JVal val;
      if (!_parseValue(val)) return false;
      out.arr.push_back(std::move(val));
      _skipWs();
      if (_peek() == ',') { _i++; continue; }
      if (_peek() == ']') { _i++; break; }
      return false;
    }
    return true;
  }

  bool _parseString(JVal &out) {
    if (!_consume('"')) return false;
    out.type = JVal::Type::String;
    std::string s;
    while (_i < _s.size() && _s[_i] != '"') {
      char c = _s[_i++];
      if (c == '\\' && _i < _s.size()) {
        char e = _s[_i++];
        switch (e) {
        case 'n': s += '\n'; break;
        case 't': s += '\t'; break;
        case 'r': s += '\r'; break;
        case '"': s += '"'; break;
        case '\\': s += '\\'; break;
        case '/': s += '/'; break;
        default: s += e;
        }
      } else {
        s += c;
      }
    }
    if (_i >= _s.size()) return false; // unterminated
    _i++;
    out.str = std::move(s);
    return true;
  }

  bool _parseBool(JVal &out) {
    if (_s.compare(_i, 4, "true") == 0) { out.type = JVal::Type::Bool; out.boolean = true; _i += 4; return true; }
    if (_s.compare(_i, 5, "false") == 0) { out.type = JVal::Type::Bool; out.boolean = false; _i += 5; return true; }
    return false;
  }

  bool _parseNumber(JVal &out) {
    size_t start = _i;
    if (_peek() == '-') _i++;
    while (_i < _s.size() && (std::isdigit((unsigned char)_s[_i]) || _s[_i] == '.' ||
                              _s[_i] == 'e' || _s[_i] == 'E' || _s[_i] == '+' || _s[_i] == '-'))
      _i++;
    if (_i == start) return false;
    out.type = JVal::Type::Number;
    out.num = std::atof(_s.substr(start, _i - start).c_str());
    return true;
  }
};

inline bool parse(const std::string &text, JVal &out) { return Parser(text).parse(out); }

} // namespace SeqJson


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
  static constexpr float kTrackHeight = 56.f;

  Timeline *timeline = nullptr;
  StepScheduler *seq = nullptr;         // for pattern name lookups only
  TimelineScheduler *scheduler = nullptr; // for playhead position only
  ClipID selectedClip = 0;

  std::function<void(int trackIndex, double beat)> onEmptyClick;
  std::function<void(ClipID)> onClipClick;

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
      float laneY = ti * kTrackHeight;
      ctx.setStrokeColor(Color::fromRGB(210, 210, 215));
      ctx.beginPath();
      ctx.moveTo(0, laneY);
      ctx.lineTo(w, laneY);
      ctx.stroke();

      for (const Clip &clip : timeline->tracks[ti].clips) {
        if (clip.type != ClipType::Pattern)
          continue; // AudioClip rendering not implemented yet
        float cx = (float)(clip.startBeat * kPxPerBeat);
        float cw = (float)(clip.lengthBeats * kPxPerBeat);
        bool selected = clip.id == selectedClip;

        ctx.setFillColor(selected ? Color::fromRGB(99, 179, 237)
                                  : Color::fromRGB(150, 150, 235));
        ctx.fillRoundedRect(cx + 1, laneY + 4, std::max(4.f, cw - 2),
                            kTrackHeight - 8, 4);

        std::string label = "Clip";
        if (seq && clip.patternSlot >= 0 &&
            clip.patternSlot < (int)seq->patternSlots.size())
          label = seq->patternSlots[clip.patternSlot].name;

        ctx.setFillColor(Color::fromRGB(255, 255, 255));
        ctx.setFont("12px sans");
        ctx.setTextAlign(CanvasTextAlign::Left);
        ctx.setTextBaseline(TextBaseline::Top);
        ctx.fillText(label, cx + 6, laneY + 8);
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
    int ti = (int)(y / kTrackHeight);
    if (ti < 0 || ti >= (int)timeline->tracks.size())
      return;
    double beat = x / kPxPerBeat;

    for (const Clip &clip : timeline->tracks[ti].clips) {
      if (clip.type == ClipType::Pattern && beat >= clip.startBeat &&
          beat < clip.startBeat + clip.lengthBeats) {
        if (onClipClick)
          onClipClick(clip.id);
        return;
      }
    }

    if (onEmptyClick)
      onEmptyClick(ti, beat);
  }

  // Only needed while something is actually playing (playhead sweep);
  // idle editing is fully event-driven via redraw() from the App side.
  bool needsContinuousRedraw() const override {
    return scheduler && scheduler->playing;
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

  // Display name for whatever instrument is loaded on each track.
  std::vector<State<std::string>> _instrumentNameState;

  // Per-track mixer UI state, mirrors _seq->trackMuted/trackSoloed and
  // trackVoice[t].gain/pan the same way _cellState mirrors step data.
  std::vector<State<bool>> _muteState;
  std::vector<State<bool>> _soloState;
  std::vector<State<double>> _trackVolumeState;
  std::vector<State<double>> _trackPanState;

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
      if (_undoStack.empty()) return;
      Entry e = std::move(_undoStack.back());
      _undoStack.pop_back();
      e.undo();
      _redoStack.push_back(std::move(e));
    }
    void redo() {
      if (_redoStack.empty()) return;
      Entry e = std::move(_redoStack.back());
      _redoStack.pop_back();
      e.redo();
      _undoStack.push_back(std::move(e));
    }
    void clear() { _undoStack.clear(); _redoStack.clear(); }

  private:
    struct Entry { Fn undo, redo; };
    static constexpr size_t kMaxDepth = 200;
    std::vector<Entry> _undoStack;
    std::vector<Entry> _redoStack;
  };
  UndoStack _undoStack;


  // ── Timeline (Phase 3) ────────────────────────────────────────────────
  Timeline _timeline;
  std::unique_ptr<TimelineScheduler> _timelineScheduler; // built in ctor,
                                                         // after _seq exists
  uint64_t _nextClipId = 1;
  TimelineTrackID _nextTimelineTrackId = 1;
  ClipID _selectedClipId = 0;

  std::shared_ptr<CanvasWidget> _timelineCanvas;
  std::shared_ptr<TimelineSurface> _timelineSurface;

  // Shown next to the timeline so the user can see/delete the current
  // selection without a full side panel widget. Text() binds to this.
  State<std::string> _timelineSelectionLabel{"No clip selected"};

public:
  SequencerApp() {
    AudioEngine::get().init();

    _seq = std::make_shared<StepScheduler>(4);
    _timelineScheduler = std::make_unique<TimelineScheduler>(*_seq);
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
    _muteState.reserve(_seq->trackVoice.size());
    _soloState.reserve(_seq->trackVoice.size());
    _trackVolumeState.reserve(_seq->trackVoice.size());
    _trackPanState.reserve(_seq->trackVoice.size());
    for (size_t t = 0; t < _seq->trackVoice.size(); t++) {
      _cellState[t].reserve(StepScheduler::kMaxSteps);
      _velocitySliderState[t].reserve(StepScheduler::kMaxSteps);
      _pitchSliderState[t].reserve(StepScheduler::kMaxSteps);
      _probabilitySliderState[t].reserve(StepScheduler::kMaxSteps);
      for (int s = 0; s < StepScheduler::kMaxSteps; s++) {
        _cellState[t].emplace_back(false);
        _velocitySliderState[t].emplace_back(1.0);
        _pitchSliderState[t].emplace_back(0.0);
        _probabilitySliderState[t].emplace_back(1.0);
      }
      // Seed mixer UI state from the track's actual initial gain/pan
      // (set just above via the trackVoice = {...} assignment) so the
      // sliders open already showing the real values instead of 0.
      _muteState.emplace_back(false);
      _soloState.emplace_back(false);
      _trackVolumeState.emplace_back((double)_seq->trackVoice[t].gain);
      _trackPanState.emplace_back((double)_seq->trackVoice[t].pan);
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
    bool isPlayhead =
        _seq->playing && viewingLivePattern && withinLength && (_seq->currentStep == s);
    if (isPlayhead)
      return Color::fromRGB(220, 50, 50);

    if (!withinLength)
      return Color::fromRGB(245, 245, 245); // beyond current pattern length — inert

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
      }
    }
    _patternLengthState.set(pat.numSteps); // triggers grid recolor (grayed-out inert columns)
    _patternSpbState.set(pat.stepsPerBeat);
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
    std::string name = "Pattern " + std::to_string(_seq->activeSlots.size() + 1);
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
      return; // StepScheduler refuses this too; bail before pushing a no-op undo entry

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
          if (std::find(_seq->activeSlots.begin(), _seq->activeSlots.end(), slot) ==
              _seq->activeSlots.end())
            _seq->activeSlots.push_back(slot);
          for (size_t i = 0; i < removedEntries.size(); i++) {
            size_t pos = std::min(removedPositions[i], _seq->arrangement.size());
            _seq->arrangement.insert(_seq->arrangement.begin() + pos, removedEntries[i]);
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


    _undoStack.push(
        [this, id = e.id] { _removeArrangementEntryByIdCore(id); },
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
      if (_seq->arrangement[i].id == id) { removed = _seq->arrangement[i]; pos = i; break; }
    if (pos == _seq->arrangement.size())
      return; // not found

    _removeArrangementEntryByIdCore(id);

    _undoStack.push(
        [this, removed, pos] {
          size_t insertAt = std::min(pos, _seq->arrangement.size());
          _seq->arrangement.insert(_seq->arrangement.begin() + insertAt, removed);
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
int _timelineCanvasWidthPx() const {
    return (int)(64 * TimelineSurface::kPxPerBeat); // 16-bar initial extent;
                                                     // grows are a follow-up
  }
  int _timelineCanvasHeightPx() const {
    return std::max(1, (int)_timeline.tracks.size()) *
           (int)TimelineSurface::kTrackHeight;
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
  }

  void _timelineAddClipAt(int trackIndex, double beat) {
    if (trackIndex < 0 || trackIndex >= (int)_timeline.tracks.size())
      return;

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
        }
    _timelineSelectionLabel.set(label);

    if (_timelineCanvas)
      _timelineCanvas->redraw();
  }

  void _deleteSelectedTimelineClip() {
    if (_selectedClipId == 0)
      return;
    for (auto &track : _timeline.tracks) {
      auto it = std::find_if(track.clips.begin(), track.clips.end(),
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
      if (e.id == id) { e.repeatCount = count; break; }
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
         << "\"samplePath\":\""
         << SeqJson::esc(t < _trackSamplePath.size() ? _trackSamplePath[t] : "") << "\","
         << "\"muted\":" << (_seq->trackMuted[t] ? "true" : "false") << ","
         << "\"soloed\":" << (_seq->trackSoloed[t] ? "true" : "false") << "}"
         << (t + 1 < _seq->trackVoice.size() ? "," : "") << "\n";
   }
   out << "  ],\n";
   out << "  \"patterns\": [\n";
   bool firstPat = true;
   for (int slot : _seq->activeSlots) {
     const Pattern &pat = _seq->patternSlots[slot];
     if (!firstPat) out << ",\n";
     firstPat = false;
     out << "    {\"slot\":" << slot << ",\"name\":\"" << SeqJson::esc(pat.name)
         << "\",\"numSteps\":" << pat.numSteps << ",\"stepsPerBeat\":" << pat.stepsPerBeat
         << ",\"steps\":[";
     for (size_t t = 0; t < pat.steps.size(); t++) {
       out << "[";
       // Serialize all kMaxSteps, not just numSteps, so steps hidden by a
       // shorter length round-trip through save/load intact.
       for (int s = 0; s < StepScheduler::kMaxSteps; s++) {
         const StepData &st = pat.steps[t][s];
         out << "{\"on\":" << (st.on ? "true" : "false")
             << ",\"velocity\":" << st.velocity
             << ",\"pitch\":" << st.pitchSemitones
             << ",\"probability\":" << st.probability << "}"
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
   out << "  ]\n}\n";
   std::ofstream f(path, std::ios::binary);
   if (f) f << out.str();
 }
 void _loadProject(const std::string &path) {
   std::ifstream f(path, std::ios::binary);
   if (!f) return;
   std::ostringstream ss;
   ss << f.rdbuf();
   SeqJson::JVal root;
   if (!SeqJson::parse(ss.str(), root) || root.type != SeqJson::JVal::Type::Object)
     return; // malformed file — leave the current project untouched
   _seq->stop();
   _currentStepState.set(-1);
   _seq->bpm = root["bpm"].asDouble(_seq->bpm);
   _bpmState.set(_seq->bpm);
   // ── Tracks ──────────────────────────────────────────────────────────
   const auto &tracksArr = root["tracks"].arr;
   for (size_t t = 0; t < tracksArr.size() && t < _seq->trackVoice.size(); t++) {
     const SeqJson::JVal &tj = tracksArr[t];
     _seq->trackVoice[t].freqHz = (float)tj["freqHz"].asDouble(_seq->trackVoice[t].freqHz);
     _seq->trackVoice[t].gain = (float)tj["gain"].asDouble(_seq->trackVoice[t].gain);
     _seq->trackVoice[t].pan = (float)tj["pan"].asDouble(_seq->trackVoice[t].pan);
     _seq->trackMuted[t] = tj["muted"].asBool(false);
     _seq->trackSoloed[t] = tj["soloed"].asBool(false);
     _trackVolumeState[t].set((double)_seq->trackVoice[t].gain);
     _trackPanState[t].set((double)_seq->trackVoice[t].pan);
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
     std::string name = pj["name"].asString("Pattern");
     int realSlot;
     if (first) {
       realSlot = _seq->activeSlots.front();
       _seq->patternSlots[realSlot].name = name;
       first = false;
     } else {
       realSlot = _seq->addPattern(name);
       if (realSlot < 0) break; // kMaxPatterns exhausted — rest of file dropped
     }
     _seq->setPatternLength(realSlot, wantedNumSteps);
     _seq->setPatternStepsPerBeat(realSlot, spb);
     fileSlotToRealSlot[pj["slot"].asInt(-1)] = realSlot;
     Pattern &pat = _seq->patternSlots[realSlot];
     const auto &stepsArr = pj["steps"].arr;
     for (size_t t = 0; t < stepsArr.size() && t < pat.steps.size(); t++) {
       const auto &trackSteps = stepsArr[t].arr;
       for (size_t s = 0; s < trackSteps.size() && (int)s < StepScheduler::kMaxSteps; s++) {
         const SeqJson::JVal &sj = trackSteps[s];
         StepData &sd = pat.steps[t][s];
         sd.on = sj["on"].asBool(false);
         sd.velocity = (float)sj["velocity"].asDouble(1.0);
         sd.pitchSemitones = (float)sj["pitch"].asDouble(0.0);
         sd.probability = (float)sj["probability"].asDouble(1.0);
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
   int realEditing = editIt != fileSlotToRealSlot.end() ? editIt->second : _seq->activeSlots.front();
   _seq->setEditingPattern(realEditing);
   _refreshGridFromPattern();
   _syncPatternDropdown();
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
                           if (_seq->editingSlot == slot) _cellState[t][s].set(oldVal);
                         },
                         [this, slot, t, s, newVal] {
                           _seq->patternSlots[slot].steps[t][s].on = newVal;
                           if (_seq->editingSlot == slot) _cellState[t][s].set(newVal);
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
                    Column({
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
                      if (newLen == oldLen) return;
                      _seq->setPatternLength(slot, newLen);
                      _patternLengthState.set(newLen);
                      _undoStack.push(
                          [this, slot, oldLen] {
                            _seq->setPatternLength(slot, oldLen);
                            if (_seq->editingSlot == slot) _patternLengthState.set(oldLen);
                          },
                          [this, slot, newLen] {
                            _seq->setPatternLength(slot, newLen);
                            if (_seq->editingSlot == slot) _patternLengthState.set(newLen);
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
    }

    auto timelineToolbar =
        Row({
                Text("Timeline:")->setFontWeight(FontWeight::Bold),
                Button("+ Add Track", [this] { _addTimelineTrack(); }),
                Button("▶ Play",
                       [this] { _timelineScheduler->start(); }),
                Button("■ Stop",
                       [this] { _timelineScheduler->stop(); }),
                Button("Delete Clip",
                       [this] { _deleteSelectedTimelineClip(); }),
                Text(_timelineSelectionLabel)->setFontSize(12),
            })
            ->setGap(8)
            ->setAlignItems(AlignItems::Center);

    auto timelineSection =
        Column({timelineToolbar, _timelineCanvas})
            ->setGap(8);


    auto transportRow =
        Row({
                Button("▶ Play", [this] { _seq->start(); }),
                Button("■ Stop",
                       [this] {
                         _seq->stop();
                         _currentStepState.set(-1);
                       }),
                Text("BPM:"),
                NumberInput(40.0, 240.0, 1.0)
                    ->setValue(_bpmState)
                    ->setWidth(90)
                    ->setOnValueChanged([this](double v) {
                      double old = _seq->bpm;
                      if (v == old) return;
                      _seq->bpm = v;
                      _undoStack.push(
                          [this, old] { _seq->bpm = old; _bpmState.set(old); },
                          [this, v] { _seq->bpm = v; _bpmState.set(v); });
                    }),
                Button("Undo", [this] { _undoStack.undo(); }),
                Button("Redo", [this] { _undoStack.redo(); }),
                FilePicker("Save Project")
                    ->setMode(FilePickerMode::Save)
                    ->setDefaultFilename("project.fluxseq")
                    ->setDefaultExtension("fluxseq")
                    ->addFilter("Flux Sequencer Project", {"*.fluxseq"})
                    ->setOnChanged([this](const std::string &path) { _saveProject(path); }),
                FilePicker("Load Project")
                    ->setMode(FilePickerMode::Open)
                    ->addFilter("Flux Sequencer Project", {"*.fluxseq"})
                    ->setOnChanged([this](const std::string &path) { _loadProject(path); }),
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
      });
    }

    return Column({transportRow, patternBar, arrangementBar,
                   Column({trackRows})->setGap(6), timelineSection})
        ->setGap(16)
        ->setPadding(24);
  }
};

WidgetPtr createApp(FluxUI *app) {
  return FluxApp()
      .setTheme(AppTheme::light())
      .build(std::make_shared<SequencerApp>());
}