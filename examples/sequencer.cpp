#include "flux/flux.hpp"
#include "flux/flux_audio_engine.hpp"
#include <cmath>
#include <vector>

// ============================================================================
// StepScheduler — engine-agnostic sequencing logic.
// Ticks periodically, looks a fixed window into the future, and schedules
// any due steps as sample-accurate playStream() starts.
// ============================================================================

struct StepHit
{
  float freqHz;
  float gain;
  float pan;
};

class StepScheduler
{
public:
  static constexpr int kSteps = 16;

  double bpm = 120.0;
  bool playing = false;

  // pattern[track][step] = true if that track fires on that step
  std::vector<std::vector<bool>> pattern;
  std::vector<StepHit> trackVoice; // one timbre per track

  int currentStep = 0;

  StepScheduler(int numTracks)
  {
    pattern.assign(numTracks, std::vector<bool>(kSteps, false));
    trackVoice.resize(numTracks);
  }

  void start()
  {
    if (playing)
      return;
    playing = true;
    currentStep = 0;
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
      for (size_t t = 0; t < pattern.size(); t++)
      {
        if (pattern[t][currentStep])
          _fireStep(trackVoice[t], _nextStepFrame);
      }

      currentStep = (currentStep + 1) % kSteps;
      _nextStepFrame += _framesPerStep();
    }
  }

private:
  uint64_t _nextStepFrame = 0;

  uint64_t _framesPerStep() const
  {
    double secondsPerStep = 60.0 / bpm / 4.0; // 16th notes, 4 per beat
    return (uint64_t)(secondsPerStep * AudioEngine::get().sampleRate());
  }

  static void _fireStep(const StepHit &hit, uint64_t targetFrame)
  {
    auto &engine = AudioEngine::get();
    auto phase = std::make_shared<float>(0.f);
    float phaseInc = (2.0f * 3.14159265f * hit.freqHz) / (float)engine.sampleRate();

    // Simple decaying envelope so each hit sounds like a "note" instead
    // of an infinite drone — decays over ~150ms then goes silent.
    auto samplesLeft = std::make_shared<int>((int)(engine.sampleRate() * 0.15));
    int totalSamples = *samplesLeft;

    AudioEngine::StreamCallback cb =
        [phase, phaseInc, samplesLeft, totalSamples](float *buf, int frames) -> int
    {
      for (int i = 0; i < frames; i++)
      {
        if (*samplesLeft <= 0)
        {
          buf[i] = 0.f;
          continue;
        }
        float env = (float)*samplesLeft / (float)totalSamples; // linear decay
        buf[i] = std::sin(*phase) * env * 0.3f;
        *phase += phaseInc;
        (*samplesLeft)--;
      }
      return frames;
    };

    // Fire-and-forget: we don't keep the VoiceHandle because this
    // engine has no concept of "step voices need cleanup" — the
    // envelope silences itself, and the voice just idles at 0 output
    // until something explicitly stops it. For a real project you'd
    // want stopVoice() called once you know it's finished, or extend
    // StreamCallback to signal completion — flagging this as a known
    // simplification, not hidden.
    engine.playStream(cb, engine.sampleRate(), hit.gain, hit.pan, targetFrame);
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

  // Tracks which step the playhead is currently on, so cells can react
  // and flash red as playback passes over them. -1 = not playing /
  // nothing highlighted.
  State<int> _currentStepState{-1};

  // One State<bool> per (track, step) cell so buttons repaint on toggle.
  std::vector<std::vector<State<bool>>> _cellState;

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

    _cellState.resize(_seq->pattern.size());
    for (auto &row : _cellState)
    {
      row.reserve(StepScheduler::kSteps);
      for (int s = 0; s < StepScheduler::kSteps; s++)
        row.emplace_back(false); // constructs State<bool> in place — no copy/assign needed
    }

    // Seed a basic four-on-the-floor + offbeat hat pattern.
    for (int s = 0; s < StepScheduler::kSteps; s += 4)
    {
      _seq->pattern[0][s] = true;
      _cellState[0][s].set(true);
    }
    for (int s = 2; s < StepScheduler::kSteps; s += 4)
    {
      _seq->pattern[3][s] = true;
      _cellState[3][s].set(true);
    }
  }

  ~SequencerApp()
  {
    if (_timerId)
      FluxUI::getCurrentInstance()->clearInterval(_timerId);
  }

  // Combined color logic for a single (track, step) cell — used by both
  // bindings below so on/off state and playhead state always agree on
  // the final color regardless of which one changed most recently.
  Color _colorForCell(size_t t, int s) const
  {
    bool isPlayhead = _seq->playing && (_seq->currentStep == s);
    if (isPlayhead)
      return Color::fromRGB(220, 50, 50); // red while the playhead is here

    bool on = _seq->pattern[t][s];
    return on ? Color::fromRGB(99, 102, 241) : Color::fromRGB(230, 230, 230);
  }

  WidgetPtr build() override
  {
    std::vector<WidgetPtr> trackRows;

    for (size_t t = 0; t < _seq->pattern.size(); t++)
    {
      std::vector<WidgetPtr> stepButtons;
      for (int s = 0; s < StepScheduler::kSteps; s++)
      {

        bool downbeatAccent = (s % 4 == 0);

        auto cell = Button("", [this, t, s]
                           {
                    bool newVal = !_seq->pattern[t][s];
                    _seq->pattern[t][s] = newVal;
                    _cellState[t][s].set(newVal); })
                        ->setWidth(28)
                        ->setHeight(28)
                        ->setBorderRadius(4)
                        ->setBackgroundColor(_cellState[t][s], [this, t, s](bool)
                                             { return _colorForCell(t, s); })
                        // Second binding — same button, different trigger. Whenever
                        // the playhead moves, every cell recomputes its color the
                        // same way, so red clears/appears correctly as it passes.
                        ->setBackgroundColor(_currentStepState, [this, t, s](int)
                                             { return _colorForCell(t, s); });

        if (downbeatAccent)
          cell->setBorderRadius(4); // (kept simple — real app might add a border here)

        stepButtons.push_back(cell);
      }

      trackRows.push_back(
          Row({stepButtons})->setGap(4));
    }

    auto transportRow = Row({
                                Button("▶ Play", [this]
                                       { _seq->start(); }),
                                Button("■ Stop", [this]
                                       {
                                         _seq->stop();
                                         _currentStepState.set(-1); // clear the red highlight immediately
                                       }),
                                Text("BPM:"),
                                NumberInput(40.0, 240.0, 1.0)->setValue(_bpmState)->setWidth(90)->setOnValueChanged([this](double v)
                                                                                                                    { _seq->bpm = v; }),
                            })
                            ->setGap(12)
                            ->setAlignItems(AlignItems::Center);

    // Poll the scheduler from a UI-thread timer — this is the
    // lookahead loop, decoupled from the audio callback entirely.
    if (!_timerId)
    {
      _timerId = FluxUI::getCurrentInstance()->setInterval(25, [this]
                                                           {
                _seq->tick();
                _currentStepState.set(_seq->currentStep); });
    }

    return Column({transportRow, Column({trackRows})->setGap(6)})
        ->setGap(16)
        ->setPadding(24);
  }
};

WidgetPtr createApp(FluxUI *app)
{
  return FluxApp().setTheme(AppTheme::light()).build(std::make_shared<SequencerApp>());
}