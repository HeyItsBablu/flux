#include "flux/flux.hpp"
#include "flux/flux_audio_engine.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>

// ============================================================================
// Data model
// ============================================================================

struct StepData
{
  bool on = false;
  float velocity = 1.0f;      // 0..1, scales this hit's gain
  float pitchSemitones = 0.f; // -24..+24, shifts freqHz for synth tracks only
                              // (see the note on StepHit::sampleId below)
};

struct StepHit
{
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
struct ArrangementEntry
{
  uint64_t id;
  int patternSlot;
};

struct Pattern
{
  std::string name;
  bool active = false;
  std::vector<std::vector<StepData>> steps; // [track][step]
};

// ============================================================================
// StepScheduler — engine-agnostic sequencing logic.
// Ticks periodically, looks a fixed window into the future, and schedules
// any due steps as sample-accurate playStream()/play() starts.
// ============================================================================

class StepScheduler
{
public:
  static constexpr int kSteps = 16;
  static constexpr int kMaxPatterns = 8; // fixed pool — same tradeoff as the
                                         // engine's 64-voice pool: simple,
                                         // real-time-safe, bounded memory.
                                         // Bump this if 8 patterns isn't enough.

  double bpm = 120.0;
  bool playing = false;
  bool songMode = false; // false = loop editingSlot forever; true = play
                         // through `arrangement`

  std::array<Pattern, kMaxPatterns> patternSlots;
  std::vector<int> activeSlots;              // ordered list of in-use slot indices —
                                             // this is the "pattern list" the UI enumerates
  std::vector<ArrangementEntry> arrangement; // song sequence, in play order

  int arrangementPos = 0; // index into `arrangement`, valid only in song mode
  int editingSlot = -1;   // which pattern the grid currently shows/edits
  int playingSlot = -1;   // which pattern is currently sounding
  int currentStep = 0;

  std::vector<StepHit> trackVoice; // one timbre per track, shared across all patterns

  StepScheduler(int numTracks) : _numTracks(numTracks)
  {
    trackVoice.resize(numTracks);
    editingSlot = addPattern("Pattern 1");
    playingSlot = editingSlot;
  }

  // Returns the new slot index, or -1 if the fixed pool (kMaxPatterns) is full.
  int addPattern(const std::string &name)
  {
    for (int i = 0; i < kMaxPatterns; i++)
    {
      if (!patternSlots[i].active)
      {
        patternSlots[i].active = true;
        patternSlots[i].name = name;
        patternSlots[i].steps.assign(_numTracks, std::vector<StepData>(kSteps));
        activeSlots.push_back(i);
        return i;
      }
    }
    return -1;
  }

  int duplicatePattern(int srcSlot)
  {
    if (srcSlot < 0 || srcSlot >= kMaxPatterns || !patternSlots[srcSlot].active)
      return -1;
    int dst = addPattern(patternSlots[srcSlot].name + " copy");
    if (dst >= 0)
      patternSlots[dst].steps = patternSlots[srcSlot].steps; // deep copy of step data
    return dst;
  }

  void deletePattern(int slot)
  {
    if (activeSlots.size() <= 1)
      return; // always keep at least one pattern around
    if (slot < 0 || slot >= kMaxPatterns || !patternSlots[slot].active)
      return;

    patternSlots[slot].active = false;
    activeSlots.erase(std::remove(activeSlots.begin(), activeSlots.end(), slot),
                      activeSlots.end());

    // Drop any arrangement entries that referenced the deleted pattern.
    arrangement.erase(
        std::remove_if(arrangement.begin(), arrangement.end(),
                       [slot](const ArrangementEntry &e)
                       { return e.patternSlot == slot; }),
        arrangement.end());
    if (arrangementPos >= (int)arrangement.size())
      arrangementPos = 0;

    if (editingSlot == slot)
      editingSlot = activeSlots.front();
    if (playingSlot == slot)
      playingSlot = editingSlot;
  }

  void setSongMode(bool v)
  {
    songMode = v;
    if (!songMode)
      playingSlot = editingSlot;
    else if (!arrangement.empty())
    {
      arrangementPos = 0;
      playingSlot = arrangement[0].patternSlot;
    }
  }

  // Switches which pattern the grid edits. In loop mode this also switches
  // playback live, so auditioning a pattern while it plays feels immediate,
  // like most trackers. In song mode, playback keeps following the
  // arrangement regardless of what you're looking at.
  void setEditingPattern(int slot)
  {
    editingSlot = slot;
    if (!songMode)
      playingSlot = slot;
  }

  void start()
  {
    if (playing)
      return;
    playing = true;
    currentStep = 0;
    arrangementPos = 0;
    playingSlot = (songMode && !arrangement.empty()) ? arrangement[0].patternSlot
                                                     : editingSlot;
    _nextStepFrame = AudioEngine::get().currentSampleTime();
  }

  void stop() { playing = false; }

  // Call this often (e.g. every 25ms) from a UI-thread timer.
  void tick()
  {
    if (!playing)
      return;

    auto &engine = AudioEngine::get();
    uint64_t now = engine.currentSampleTime();
    uint64_t lookahead = (uint64_t)(engine.sampleRate() * 0.1); // 100ms window

    while (_nextStepFrame < now + lookahead)
    {
      Pattern &pat = patternSlots[playingSlot];
      for (size_t t = 0; t < pat.steps.size(); t++)
      {
        if (pat.steps[t][currentStep].on)
          _fireStep(trackVoice[t], pat.steps[t][currentStep], _nextStepFrame);
      }

      currentStep++;
      if (currentStep >= kSteps)
      {
        currentStep = 0;
        _advancePlayback(); // pattern boundary — advance the song if in song mode
      }
      _nextStepFrame += _framesPerStep();
    }
  }

private:
  int _numTracks;
  uint64_t _nextStepFrame = 0;

  void _advancePlayback()
  {
    if (!songMode || arrangement.empty())
      return; // loop mode: keep looping playingSlot forever
    arrangementPos = (arrangementPos + 1) % (int)arrangement.size();
    playingSlot = arrangement[arrangementPos].patternSlot;
  }

  uint64_t _framesPerStep() const
  {
    double secondsPerStep = 60.0 / bpm / 4.0; // 16th notes, 4 per beat
    uint64_t frames = (uint64_t)(secondsPerStep * AudioEngine::get().sampleRate());
    // Guard against tick()'s while-loop spinning forever if this ever
    // truncates to 0 (extreme bpm and/or very low sample rate).
    return std::max<uint64_t>(1, frames);
  }

  static void _fireStep(const StepHit &hit, const StepData &step, uint64_t targetFrame)
  {
    auto &engine = AudioEngine::get();
    float velocity = std::max(0.f, std::min(1.f, step.velocity));
    float effectiveGain = hit.gain * velocity;

    if (engine.isSampleValid(hit.sampleId))
    {
      // Sample-backed track: sample-accurate one-shot, velocity applied
      // as gain, pitch via pitchRatio.
      float pitchRatio = std::pow(2.0f, step.pitchSemitones / 12.0f);
      engine.play(hit.sampleId, effectiveGain, hit.pan, /*loop=*/false, pitchRatio, targetFrame);
      return;
    }

    _fireSynthStep(hit, step, effectiveGain, targetFrame);
  }

  // Bundles the two pieces of per-note state the envelope callback needs.
  // A single make_shared<> here replaces the previous pair of separate
  // shared_ptr<float>/shared_ptr<int> allocations per note fired.
  struct SynthNoteState
  {
    float phase = 0.f;
    int samplesLeft = 0;
  };

  static void _fireSynthStep(const StepHit &hit, const StepData &step,
                             float effectiveGain, uint64_t targetFrame)
  {
    auto &engine = AudioEngine::get();
    auto state = std::make_shared<SynthNoteState>();

    // Pitch shift: each semitone is a factor of 2^(1/12).
    float pitchedFreq = hit.freqHz * std::pow(2.0f, step.pitchSemitones / 12.0f);
    float phaseInc = (2.0f * 3.14159265f * pitchedFreq) / (float)engine.sampleRate();

    // Simple decaying envelope so each hit sounds like a "note" instead
    // of an infinite drone — decays over ~150ms then goes silent.
    state->samplesLeft = (int)(engine.sampleRate() * 0.15);
    int totalSamples = state->samplesLeft;

    AudioEngine::StreamCallback cb =
        [state, phaseInc, totalSamples](float *buf, int frames) -> int
    {
      if (state->samplesLeft <= 0)
        return -1; // fully decayed — engine frees this voice slot

      for (int i = 0; i < frames; i++)
      {
        float env = (float)state->samplesLeft / (float)totalSamples; // linear decay
        buf[i] = std::sin(state->phase) * env * 0.3f;
        state->phase += phaseInc;
        state->samplesLeft--;
      }
      return frames;
    };

    // Fire-and-forget: the envelope signals its own completion (negative
    // return), so the engine frees the voice slot once the decay finishes.
    engine.playStream(cb, engine.sampleRate(), effectiveGain, hit.pan, targetFrame);
  }
};

// ============================================================================
// UI
// ============================================================================

class SequencerApp : public Widget
{
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

  // Display name for whatever instrument is loaded on each track.
  std::vector<State<std::string>> _instrumentNameState;

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
  State<std::vector<ArrangementEntry>> _arrangementState{std::vector<ArrangementEntry>{}};
  uint64_t _nextArrangementEntryId = 1;

public:
  SequencerApp()
  {
    AudioEngine::get().init();

    _seq = std::make_shared<StepScheduler>(4);
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
    for (size_t t = 0; t < _seq->trackVoice.size(); t++)
    {
      _cellState[t].reserve(StepScheduler::kSteps);
      _velocitySliderState[t].reserve(StepScheduler::kSteps);
      _pitchSliderState[t].reserve(StepScheduler::kSteps);
      for (int s = 0; s < StepScheduler::kSteps; s++)
      {
        _cellState[t].emplace_back(false);
        _velocitySliderState[t].emplace_back(1.0);
        _pitchSliderState[t].emplace_back(0.0);
      }
    }

    // Seed the initial pattern (slot 0, created by StepScheduler's ctor)
    // with a basic four-on-the-floor + offbeat hat pattern.
    Pattern &initial = _seq->patternSlots[_seq->editingSlot];
    for (int s = 0; s < StepScheduler::kSteps; s += 4)
      initial.steps[0][s].on = true;
    for (int s = 2; s < StepScheduler::kSteps; s += 4)
      initial.steps[3][s].on = true;

    _refreshGridFromPattern(); // pull the seeded pattern into the bound cell states
  }

  ~SequencerApp()
  {
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
  Color _colorForCell(size_t t, int s) const
  {
    bool viewingLivePattern = (_seq->editingSlot == _seq->playingSlot);
    bool isPlayhead = _seq->playing && viewingLivePattern && (_seq->currentStep == s);
    if (isPlayhead)
      return Color::fromRGB(220, 50, 50);

    const StepData &step = _seq->patternSlots[_seq->editingSlot].steps[t][s];
    if (!step.on)
      return Color::fromRGB(230, 230, 230);

    float v = std::max(0.f, std::min(1.f, step.velocity));
    auto lerp = [v](int onChan, int offChan)
    { return (int)(offChan + (onChan - offChan) * v); };
    return Color::fromRGB(lerp(99, 230), lerp(102, 230), lerp(241, 230));
  }

  // Pulls patternSlots[editingSlot]'s step data into the bound cell/slider
  // states, so the grid visually reflects whichever pattern is now being
  // edited. Call after any operation that changes editingSlot.
  void _refreshGridFromPattern()
  {
    const Pattern &pat = _seq->patternSlots[_seq->editingSlot];
    for (size_t t = 0; t < pat.steps.size(); t++)
    {
      for (int s = 0; s < StepScheduler::kSteps; s++)
      {
        const StepData &step = pat.steps[t][s];
        _cellState[t][s].set(step.on);
        _velocitySliderState[t][s].set(step.velocity);
        _pitchSliderState[t][s].set((double)step.pitchSemitones);
      }
    }
  }

  void _loadTrackSample(size_t t, const std::string &path)
  {
    if (path.empty())
      return;

    SampleID newId = AudioEngine::get().loadSample(path);
    if (newId == kInvalidSample)
      return; // decode failed — leave the existing instrument in place

    SampleID old = _seq->trackVoice[t].sampleId;
    _seq->trackVoice[t].sampleId = newId;
    if (old != kInvalidSample)
      AudioEngine::get().unloadSample(old);

    size_t slash = path.find_last_of("/\\");
    _instrumentNameState[t].set(slash == std::string::npos ? path : path.substr(slash + 1));
  }

  void _createPattern()
  {
    int newSlot = _seq->addPattern("Pattern " + std::to_string(_seq->activeSlots.size() + 1));
    if (newSlot < 0)
      return; // fixed pool (kMaxPatterns) exhausted
    _seq->setEditingPattern(newSlot);
    _refreshGridFromPattern();
    _syncPatternDropdown();
  }

  void _duplicateCurrentPattern()
  {
    int dup = _seq->duplicatePattern(_seq->editingSlot);
    if (dup < 0)
      return;
    _seq->setEditingPattern(dup);
    _refreshGridFromPattern();
    _syncPatternDropdown();
  }

  void _deleteCurrentPattern()
  {
    _seq->deletePattern(_seq->editingSlot);
    _refreshGridFromPattern();
    _syncPatternDropdown();
  }

  void _syncPatternDropdown()
  {
    std::vector<std::string> labels;
    for (int slot : _seq->activeSlots)
      labels.push_back(_seq->patternSlots[slot].name);
    _patternDropdown->setOptions(labels);

    int idx = 0;
    for (size_t i = 0; i < _seq->activeSlots.size(); i++)
      if (_seq->activeSlots[i] == _seq->editingSlot)
      {
        idx = (int)i;
        break;
      }
    _dropdownIndexState.set(idx);
  }

  void _addToArrangement(int patternSlot)
  {
    ArrangementEntry e{_nextArrangementEntryId++, patternSlot};
    _seq->arrangement.push_back(e);

    _arrangementState.push_back(e);
    printf("arrangementState size after add: %zu\n", _arrangementState.size());
  }

  void _removeArrangementEntryById(uint64_t id)
  {
    for (size_t i = 0; i < _seq->arrangement.size(); i++)
    {
      if (_seq->arrangement[i].id == id)
      {
        _seq->arrangement.erase(_seq->arrangement.begin() + i);

        _arrangementState.erase(i);
        printf("arrangementState size after add: %zu\n", _arrangementState.size());
        break;
      }
    }
  }

  WidgetPtr _buildArrangementChip(const ArrangementEntry &entry)
  {
    return Row({
                   Text(_seq->patternSlots[entry.patternSlot].name)->setFontSize(12),
                   Button("x", [this, id = entry.id]
                          { _removeArrangementEntryById(id); })
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

  WidgetPtr build() override
  {
    std::vector<WidgetPtr> trackRows;

    for (size_t t = 0; t < _seq->trackVoice.size(); t++)
    {
      std::vector<WidgetPtr> stepButtons;
      for (int s = 0; s < StepScheduler::kSteps; s++)
      {
        bool downbeatAccent = (s % 4 == 0);

        auto cell = Button("", [this, t, s]
                           {
                    StepData &step = _seq->patternSlots[_seq->editingSlot].steps[t][s];
                    step.on = !step.on;
                    _cellState[t][s].set(step.on); })
                        ->setWidth(28)
                        ->setHeight(28)
                        ->setBorderRadius(4)
                        ->setBackgroundColor(_cellState[t][s], [this, t, s](bool)
                                             { return _colorForCell(t, s); })
                        ->setBackgroundColor(_velocitySliderState[t][s], [this, t, s](double)
                                             { return _colorForCell(t, s); })
                        // Whenever the playhead moves, every cell recomputes
                        // its color the same way, so red clears/appears
                        // correctly as it passes.
                        ->setBackgroundColor(_currentStepState, [this, t, s](int)
                                             { return _colorForCell(t, s); });

        if (downbeatAccent)
          cell->setBorderRadius(4);

        auto cellWithMenu = ContextMenu(cell, {
                                                  ContextMenuItem::Widget(
                                                      Column({
                                                                 Text("Velocity")->setFontSize(11),
                                                                 Slider(0.0, 1.0, 0.05)
                                                                     ->setValue(_velocitySliderState[t][s])
                                                                     ->setWidth(140)
                                                                     ->setOnValueChanged([this, t, s](double v)
                                                                                         {
                                              _seq->patternSlots[_seq->editingSlot]
                                                  .steps[t][s].velocity = (float)v;
                                              _velocitySliderState[t][s].set(v); }),
                                                                 Text("Pitch (semitones)")->setFontSize(11),
                                                                 Slider(-24.0, 24.0, 1.0)
                                                                     ->setValue(_pitchSliderState[t][s])
                                                                     ->setWidth(140)
                                                                     ->setOnValueChanged([this, t, s](double v)
                                                                                         {
                                              _seq->patternSlots[_seq->editingSlot]
                                                  .steps[t][s].pitchSemitones = (float)v;
                                              _pitchSliderState[t][s].set(v); }),
                                                             })
                                                          ->setGap(4)
                                                          ->setPadding(8)),
                                              });

        stepButtons.push_back(cellWithMenu);
      }

      auto trackHeader = Row({
                                 Text(_instrumentNameState[t])
                                     ->setFontSize(12)
                                     ->setWidth(120)
                                     ->setOverflow(TextOverflow::Ellipsis)
                                     ->setMaxLines(1),
                                 FilePicker()
                                     ->setMode(FilePickerMode::Open)
                                     ->setTitle("Load instrument sample")
                                     ->addFilter("Audio", {"*.wav", "*.mp3", "*.ogg", "*.flac"})

                                     ->setWidth(80)
                                     ->setOnChanged([this, t](const std::string &path)
                                                    { _loadTrackSample(t, path); }),
                             })
                             ->setGap(8)
                             ->setAlignItems(AlignItems::Center)
                             ->setWidth(210);

      trackRows.push_back(
          Row({trackHeader, Row({stepButtons})->setGap(4)})
              ->setGap(12)
              ->setAlignItems(AlignItems::Center));
    }

    // ── Pattern bar ──────────────────────────────────────────────────────
    std::vector<std::string> initialLabels;
    for (int slot : _seq->activeSlots)
      initialLabels.push_back(_seq->patternSlots[slot].name);

    _patternDropdown = Dropdown(initialLabels)
                           ->setSelectedIndex(_dropdownIndexState)
                           ->setOnSelectionChanged([this](int idx, const std::string &)
                                                   {
                                                     if (idx < 0 || idx >= (int)_seq->activeSlots.size())
                                                       return;
                                                     _seq->setEditingPattern(_seq->activeSlots[idx]);
                                                     _refreshGridFromPattern(); })
                           ->setWidth(160);

    auto patternBar = Row({
                              Text("Pattern:"),
                              _patternDropdown,
                              Button("+ New", [this]
                                     { _createPattern(); }),
                              Button("Duplicate", [this]
                                     { _duplicateCurrentPattern(); }),
                              Button("Delete", [this]
                                     { _deleteCurrentPattern(); }),
                              Text(_currentStepState, [this](int)
                                   { return "Playing: " + _seq->patternSlots[_seq->playingSlot].name; })
                                  ->setFontSize(12),
                          })
                          ->setGap(8)
                          ->setAlignItems(AlignItems::Center);

    // ── Arrangement bar (song mode) ─────────────────────────────────────
    auto arrangementList = Box({
                                   _buildArrangementChip(ArrangementEntry{1, 0}), // TEMP: hardcoded probe
                                   Map<ArrangementEntry>(
                                       _arrangementState,
                                       [](int, const ArrangementEntry &e)
                                       { return FlexItemKey::fromInt64((int64_t)e.id); },
                                       [this](int, const ArrangementEntry &e)
                                       { return _buildArrangementChip(e); }),
                               })
                               ->setDisplay(Display::Flex)
                               ->setDirection(FlexDirection::Row)
                               ->setWrap(FlexWrap::Wrap)
                               ->setGap(6);

    auto arrangementBar = Column({
                                     Row({
                                             Toggle("Song Mode")
                                                 ->setValue(_songModeState)
                                                 ->setOnToggleChanged([this](bool v)
                                                                      { _seq->setSongMode(v); }),
                                             Button("+ Add current pattern to song", [this]
                                                    { _addToArrangement(_seq->editingSlot); }),
                                         })
                                         ->setGap(12)
                                         ->setAlignItems(AlignItems::Center),
                                     arrangementList,
                                 })
                              ->setGap(8);

    auto transportRow = Row({
                                Button("▶ Play", [this]
                                       { _seq->start(); }),
                                Button("■ Stop", [this]
                                       {
                                         _seq->stop();
                                         _currentStepState.set(-1); }),
                                Text("BPM:"),
                                NumberInput(40.0, 240.0, 1.0)->setValue(_bpmState)->setWidth(90)->setOnValueChanged([this](double v)
                                                                                                                    { _seq->bpm = v; }),
                            })
                            ->setGap(12)
                            ->setAlignItems(AlignItems::Center);

    if (!_timerId)
    {
      _timerId = FluxUI::getCurrentInstance()->setInterval(25, [this]
                                                           {
                _seq->tick();
                _currentStepState.set(_seq->currentStep); });
    }

    return Column({transportRow, patternBar, arrangementBar, Column({trackRows})->setGap(6)})
        ->setGap(16)
        ->setPadding(24);
  }
};

WidgetPtr createApp(FluxUI *app)
{
  return FluxApp().setTheme(AppTheme::light()).build(std::make_shared<SequencerApp>());
}