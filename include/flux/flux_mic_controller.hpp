// flux_mic_controller.hpp
// Reactive convenience layer over FluxMic — NOT a Widget, NOT added to the
// tree. It owns the FluxMic lifecycle and exposes State<bool>/State<float>
// so you can drive UI with Conditional/Switch instead of hand-rolling your
// own state + polling timer for every screen that needs a mic toggle.
//
// If you only need the mic in one place and want to manage state yourself,
// skip this and call FluxMic::get() directly — that's the lower-level
// primitive this wraps.
//
// Usage:
//   auto mic = Mic();
//   mic->setOnFrame([](const float* s, size_t c) {
//       opusEncodeAndSend(s, c);
//   });
//
//   // anywhere in your widget tree:
//   Conditional(mic->isRecording)
//       ->Then([mic]() { return Button("Stop", [mic]{ mic->toggle(); }); })
//       ->Else([mic]() { return Button("Talk", [mic]{ mic->toggle(); }); })
//
// Mic() is memoized per process (one mic == one hardware device == one
// controller). Calling Mic() multiple times returns the same instance, so
// multiple UI surfaces (toolbar icon, call screen, settings) can all bind
// to it safely without fighting over FluxMic::get()'s singleton state.
//
#pragma once

#include "flux/flux_state.hpp"
#include "flux/flux_mic.hpp"
#include "flux/flux_platform.hpp" // FluxUI::getCurrentInstance, TimerID

#include <memory>

class MicController : public std::enable_shared_from_this<MicController>
{
public:
    // Reactive — bind these to Conditional/Switch/bindProperty as needed.
    State<bool> isRecording;
    State<float> level; // 0..1 RMS, polled — safe to read from UI thread

    MicController()
        : isRecording(false, FluxUI::getCurrentInstance()),
          level(0.f, FluxUI::getCurrentInstance())
    {
    }

    ~MicController()
    {
        stop();
    }

    // Frame callback fires on FluxMic's capture thread — NOT the UI thread.
    // Keep it cheap (encode/enqueue only). Do not touch State or Widgets
    // from inside it.
    void setOnFrame(FluxMic::FrameCallback cb) { _onFrame = std::move(cb); }

    void setSampleRate(int sr) { _sampleRate = sr; }
    void setChannels(int ch) { _channels = ch; }

    void start()
    {
        if (isRecording.get())
            return;

        if (!FluxMic::get().open(_sampleRate, _channels))
            return; // device unavailable / permission denied — state stays false

        bool ok = FluxMic::get().start([this](const float *s, size_t c)
                                       {
                                           if (_onFrame)
                                               _onFrame(s, c);
                                           // _level is intentionally NOT updated here — see _startTimer().
                                       });

        if (!ok)
            return;

        isRecording.set(true);
        _startTimer();
    }

    void stop()
    {
        if (!isRecording.get())
            return;
        FluxMic::get().stop();
        _stopTimer();
        isRecording.set(false);
        level.set(0.f);
    }

    void toggle() { isRecording.get() ? stop() : start(); }

    // Convenience alias matching common mute-button semantics:
    // setMute(true) == stop capturing, setMute(false) == start capturing.
    void setMute(bool muted) { muted ? stop() : start(); }
    bool isMuted() const { return !isRecording.get(); }

private:
    FluxMic::FrameCallback _onFrame;
    int _sampleRate = FluxMic::kDefaultSampleRate;
    int _channels = FluxMic::kDefaultChannels;
    TimerID _levelTimer = 0;

    // Level is polled from the UI thread via FluxMic::getInputLevel(),
    // never written to from the capture-thread frame callback. This keeps
    // State::set() calls confined to the UI thread regardless of what
    // thread-safety guarantees State<T> does or doesn't provide.
    void _startTimer()
    {
        if (_levelTimer)
            return;
        auto *ui = FluxUI::getCurrentInstance();
        if (!ui)
            return;

        std::weak_ptr<MicController> weak = shared_from_this();
        _levelTimer = ui->setInterval(33, [weak]()
                                      {
            auto self = weak.lock();
            if (!self) return;
            if (self->isRecording.get())
                self->level.set(FluxMic::get().getInputLevel()); });
    }

    void _stopTimer()
    {
        auto *ui = FluxUI::getCurrentInstance();
        if (ui && _levelTimer)
        {
            ui->clearInterval(_levelTimer);
            _levelTimer = 0;
        }
    }
};

using MicControllerPtr = std::shared_ptr<MicController>;

// Memoized per process — FluxMic::get() is itself a singleton (one physical
// mic), so Mic() returns the same controller every time it's called,
// preventing two independent controllers from fighting over device state.
inline MicControllerPtr Mic()
{
    static MicControllerPtr instance = std::make_shared<MicController>();
    return instance;
}