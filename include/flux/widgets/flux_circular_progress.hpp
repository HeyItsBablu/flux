#pragma once
#ifndef FLUX_CIRCULAR_PROGRESS_HPP
#define FLUX_CIRCULAR_PROGRESS_HPP

#include "../flux_core.hpp"
#include "../flux_state.hpp"

#include <algorithm>
#include <cmath>
#include <memory>

class CircularProgressIndicatorWidget : public Widget {
public:

    float value       = -1.0f;   // < 0 = indeterminate (spinning)
    int   diameter    = 36;
    int   strokeWidth = 4;

    Color progressColor = Color::fromRGB(37, 99, 235);
    Color trackColor    = Color::fromRGB(226, 232, 240);
    bool  hasTrack      = true;
    bool  roundedCaps   = true;

    // ── Lifecycle ────────────────────────────────────────────────────────────

    void onDetach() override {
        stopSpinner_();
        Widget::onDetach();
    }

    // ── Layout ───────────────────────────────────────────────────────────────

    void computeLayout(GraphicsContext&,
                       const BoxConstraints& constraints,
                       FontCache&) override {
        width  = constraints.clampWidth(diameter);
        height = constraints.clampHeight(diameter);
        needsLayout = false;
    }

    // ── Render ───────────────────────────────────────────────────────────────

    void render(GraphicsContext& ctx, FontCache&) override {
        if (!visible) return;

        // Start spinner here — guaranteed to be called after the window and
        // FluxUI instance are fully initialised, and after layout has run so
        // x/y/width/height are all valid for invalidation rects.
        if (value < 0.0f)
            startSpinner_();

        Painter painter(ctx);

        const float cx = float(x) + float(width)  * 0.5f;
        const float cy = float(y) + float(height) * 0.5f;
        const float r  = float(std::min(width, height)) * 0.5f
                         - float(strokeWidth) * 0.5f - 1.0f;

        if (hasTrack)
            painter.drawArc(cx, cy, r, strokeWidth,
                            0.0f, kTwoPi, trackColor, false);

        if (value >= 0.0f) {
            float sweep = kTwoPi * std::clamp(value, 0.0f, 1.0f);
            painter.drawArc(cx, cy, r, strokeWidth,
                            -kHalfPi, sweep, progressColor, roundedCaps);
        } else {
            painter.drawArc(cx, cy, r, strokeWidth,
                            spinAngle_, kTwoPi * 0.75f,
                            progressColor, roundedCaps);
        }

        needsPaint = false;
    }

    // ── Fluent setters ───────────────────────────────────────────────────────

    std::shared_ptr<CircularProgressIndicatorWidget> setValue(float v) {
        value = v;
        if (v >= 0.0f) stopSpinner_();
        // Don't call startSpinner_ here — render() will do it safely
        markNeedsPaint();
        return ptr_();
    }

    std::shared_ptr<CircularProgressIndicatorWidget> setSize(int d) {
        diameter = d;
        width = height = d;
        markNeedsLayout();
        return ptr_();
    }

    std::shared_ptr<CircularProgressIndicatorWidget> setStrokeWidth(int sw) {
        strokeWidth = sw;
        markNeedsPaint();
        return ptr_();
    }

    std::shared_ptr<CircularProgressIndicatorWidget> setColor(Color c) {
        progressColor = c;
        markNeedsPaint();
        return ptr_();
    }

    std::shared_ptr<CircularProgressIndicatorWidget> setTrackColor(Color c) {
        trackColor = c;
        hasTrack   = true;
        markNeedsPaint();
        return ptr_();
    }

    std::shared_ptr<CircularProgressIndicatorWidget> setShowTrack(bool show) {
        hasTrack = show;
        markNeedsPaint();
        return ptr_();
    }

    std::shared_ptr<CircularProgressIndicatorWidget> setRoundedCaps(bool rc) {
        roundedCaps = rc;
        markNeedsPaint();
        return ptr_();
    }

    template <typename T, typename F>
    std::shared_ptr<CircularProgressIndicatorWidget>
    setValue(State<T>& state, F transform) {
        std::function<float(const T&)> fn = transform;
        value = fn(state.get());
        if (value >= 0.0f) stopSpinner_();

        state.bindProperty(
            shared_from_this(),
            [fn](Widget* w, const T& val) {
                auto* self = static_cast<CircularProgressIndicatorWidget*>(w);
                float newVal = fn(val);
                self->value = newVal;
                if (newVal >= 0.0f) self->stopSpinner_();
                else                self->spinTimerId_ = 0; // let render() restart
                self->markNeedsPaint();
            },
            true);
        return ptr_();
    }

private:

    static constexpr float kTwoPi    = 6.28318530f;
    static constexpr float kHalfPi   = 1.57079632f;
    static constexpr float kSpinStep = 0.07f;
    static constexpr int   kSpinMs   = 16;

    float   spinAngle_   = -kHalfPi;
    TimerID spinTimerId_ = 0;

    std::shared_ptr<CircularProgressIndicatorWidget> ptr_() {
        return std::static_pointer_cast<CircularProgressIndicatorWidget>(
            shared_from_this());
    }

    void startSpinner_() {
        if (spinTimerId_ != 0) return;  // already running, nothing to do
        auto* ui = FluxUI::getCurrentInstance();
        if (!ui) return;

        std::weak_ptr<Widget> weak = shared_from_this();
        spinTimerId_ = ui->setInterval(kSpinMs, [weak]() {
            if (auto sp = weak.lock())
                static_cast<CircularProgressIndicatorWidget*>(sp.get())->onSpinTick_();
        });
    }

    void stopSpinner_() {
        if (spinTimerId_ == 0) return;
        if (auto* ui = FluxUI::getCurrentInstance())
            ui->clearInterval(spinTimerId_);
        spinTimerId_ = 0;
    }

    void onSpinTick_() {
        spinAngle_ += kSpinStep;
        if (spinAngle_ >= kTwoPi) spinAngle_ -= kTwoPi;

        markNeedsPaint();

        auto* ui = FluxUI::getCurrentInstance();
        if (!ui) return;

        // width/height are valid here because layout has already run before
        // the first render() call that armed the timer.
        ui->invalidateWidget(x - 1, y - 1, width + 2, height + 2);
    }
};



using CircularProgressPtr = std::shared_ptr<CircularProgressIndicatorWidget>;

inline CircularProgressPtr CircularProgress() {
    return std::make_shared<CircularProgressIndicatorWidget>();
}

#endif // FLUX_CIRCULAR_PROGRESS_HPP