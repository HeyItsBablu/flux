#pragma once
// ============================================================================
// flux_scrollbar.hpp — cross-platform, renderer-agnostic geometry and logic
//
// Rendering is handled per-platform by Painter::drawScrollbar().
// This header contains only:
//   - Animation state (alpha, width expansion)
//   - Geometry queries (thumbRect, trackRect)
//   - Mouse hit testing and drag logic
// ============================================================================

#include <algorithm>
#include <cmath>
#include <functional>
#include <tuple>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

class CustomScrollbar
{
public:
    static constexpr float kTrackThick = 12.f;
    static constexpr float kThumbThin = 5.f;
    static constexpr float kThumbFat = 8.f;
    static constexpr float kArrowSize = 12.f;
    static constexpr float kThumbMinLen = 24.f;
    static constexpr float kCornerR = 3.f;
    static constexpr int kCapSegs = 8;
    static constexpr float kIdleAlpha = 0.15f;
    static constexpr float kActiveAlpha = 0.90f;
    static constexpr float kIdleDelay = 1.5f;
    static constexpr float kFadeSpeed = 3.5f;
    static constexpr float kExpandSpeed = 10.f;

    enum class Axis
    {
        Horizontal,
        Vertical
    };
    enum class Zone
    {
        None,
        ArrowStart,
        TrackBefore,
        Thumb,
        TrackAfter,
        ArrowEnd
    };

    explicit CustomScrollbar(Axis axis) : axis_(axis) {}

    // ── Current alpha (used by Painter::drawScrollbar) ────────────────────
    float alpha() const { return alpha_; }

    // ── Geometry query helpers (platform-neutral) ─────────────────────────
    std::tuple<float, float, float, float> thumbRect(int /*glW*/, int /*glH*/) const
    {
        float ts, tl;
        thumbPixels(ts, tl);
        float halfW = currentW_ * 0.5f;
        float crossCenter = (axis_ == Axis::Horizontal)
                                ? stripY0_ + kTrackThick * 0.5f
                                : stripX0_ + kTrackThick * 0.5f;
        if (axis_ == Axis::Horizontal)
            return {stripX0_ + ts, crossCenter - halfW, tl, currentW_};
        else
            return {crossCenter - halfW, stripY0_ + ts, currentW_, tl};
    }

    std::tuple<float, float, float, float> trackRect(int glW, int glH) const
    {
        if (axis_ == Axis::Horizontal)
            return {stripX0_, float(glH) - kTrackThick, stripLen_, kTrackThick};
        else
            return {float(glW) - kTrackThick, stripY0_, kTrackThick, stripLen_};
    }

    // ── Geometry ──────────────────────────────────────────────────────────
    void setGeometry(float stripX0, float stripY0, float stripLen)
    {
        stripX0_ = stripX0;
        stripY0_ = stripY0;
        stripLen_ = stripLen;
    }

    void setThumb(float thumbMin, float thumbMax, bool visible)
    {
        visible_ = visible;
        thumbMin_ = std::clamp(thumbMin, 0.f, 1.f);
        thumbMax_ = std::clamp(thumbMax, 0.f, 1.f);
        if (thumbMin_ > thumbMax_)
            std::swap(thumbMin_, thumbMax_);
    }

    // ── Animation tick ────────────────────────────────────────────────────
    bool tick(double dt)
    {
        float f = float(dt);
        float targetW = (hovered_ || dragging_) ? kThumbFat : kThumbThin;
        currentW_ += (targetW - currentW_) * std::min(1.f, kExpandSpeed * f);

        if (!visible_)
        {
            alpha_ += (0.f - alpha_) * std::min(1.f, kFadeSpeed * f);
            idleTimer_ = 0.f;
            return std::abs(alpha_) > 0.005f;
        }

        float targetAlpha;
        if (hovered_ || dragging_)
        {
            targetAlpha = kActiveAlpha;
            idleTimer_ = 0.f;
        }
        else
        {
            idleTimer_ += f;
            targetAlpha = (idleTimer_ < kIdleDelay) ? kActiveAlpha : kIdleAlpha;
        }
        float prev = alpha_;
        alpha_ += (targetAlpha - alpha_) * std::min(1.f, kFadeSpeed * f);
        return std::abs(alpha_ - prev) > 0.002f;
    }

    bool needsRedraw() const { return visible_ && alpha_ > 0.005f; }
    bool isVisible() const { return visible_; }
    void poke() { idleTimer_ = 0.f; }

    // ── Mouse events ──────────────────────────────────────────────────────
    bool onMouseDown(int sx, int sy, std::function<void(float)> scrollTo)
    {
        Zone z = hitTest(sx, sy);
        if (z == Zone::None)
            return false;
        hovered_ = true;
        idleTimer_ = 0.f;
        scrollToFn_ = scrollTo;
        if (z == Zone::Thumb)
        {
            dragging_ = true;
            dragStartPx_ = axis_ == Axis::Horizontal ? float(sx) : float(sy);
            dragStartMin_ = thumbMin_;
            return true;
        }
        if (z == Zone::ArrowStart)
        {
            scrollBy(-kArrowStep, scrollTo);
            return true;
        }
        if (z == Zone::ArrowEnd)
        {
            scrollBy(+kArrowStep, scrollTo);
            return true;
        }
        if (z == Zone::TrackBefore)
        {
            scrollBy(-(thumbMax_ - thumbMin_), scrollTo);
            return true;
        }
        if (z == Zone::TrackAfter)
        {
            scrollBy(+(thumbMax_ - thumbMin_), scrollTo);
            return true;
        }
        return false;
    }

    bool onMouseMove(int sx, int sy)
    {
        Zone z = hitTest(sx, sy);
        bool wasHover = hovered_;
        hovered_ = (z != Zone::None);
        if (hovered_ != wasHover)
            idleTimer_ = 0.f;
        if (dragging_ && scrollToFn_)
        {
            float pos = axis_ == Axis::Horizontal ? float(sx) : float(sy);
            float delta = pos - dragStartPx_;
            float usable = usableLen();
            float thumbPx = (thumbMax_ - thumbMin_) * usable;
            float range = usable - thumbPx;
            if (range > 0.f)
            {
                float newMin = std::clamp(
                    dragStartMin_ + delta / range,
                    0.f, 1.f - (thumbMax_ - thumbMin_));
                scrollToFn_(newMin);
            }
            return true;
        }
        return false;
    }

    bool onMouseUp(int, int)
    {
        bool was = dragging_;
        dragging_ = false;
        idleTimer_ = 0.f;
        return was;
    }

    void onMouseLeave()
    {
        hovered_ = false;
        dragging_ = false;
    }

private:
    Axis axis_;
    float stripX0_ = 0, stripY0_ = 0, stripLen_ = 100.f;
    float thumbMin_ = 0.f, thumbMax_ = 1.f;
    bool visible_ = false;
    bool hovered_ = false;
    bool dragging_ = false;
    float alpha_ = kIdleAlpha;
    float idleTimer_ = 0.f;
    float currentW_ = kThumbThin;
    float dragStartPx_ = 0.f;
    float dragStartMin_ = 0.f;
    std::function<void(float)> scrollToFn_;
    static constexpr float kArrowStep = 0.05f;

    float usableLen() const { return std::max(1.f, stripLen_ - kArrowSize * 2.f); }

    void thumbPixels(float &pxStart, float &pxLen) const
    {
        float ul = usableLen();
        pxLen = std::max(kThumbMinLen, (thumbMax_ - thumbMin_) * ul);
        float range = ul - pxLen;
        pxStart = kArrowSize + thumbMin_ / (1.f - (thumbMax_ - thumbMin_)) * range;
        if (!std::isfinite(pxStart))
            pxStart = kArrowSize;
        pxStart = std::clamp(pxStart, kArrowSize, kArrowSize + range);
    }

    Zone hitTest(int sx, int sy) const
    {
        if (!visible_)
            return Zone::None;
        float px = axis_ == Axis::Horizontal ? float(sx) : float(sy);
        float py = axis_ == Axis::Horizontal ? float(sy) : float(sx);
        float trackCross = axis_ == Axis::Horizontal ? stripY0_ : stripX0_;
        if (py < trackCross || py >= trackCross + kTrackThick)
            return Zone::None;
        float along = axis_ == Axis::Horizontal ? px - stripX0_ : px - stripY0_;
        if (along < 0 || along > stripLen_)
            return Zone::None;
        if (along < kArrowSize)
            return Zone::ArrowStart;
        if (along > stripLen_ - kArrowSize)
            return Zone::ArrowEnd;
        float ts, tl;
        thumbPixels(ts, tl);
        if (along < ts)
            return Zone::TrackBefore;
        if (along < ts + tl)
            return Zone::Thumb;
        return Zone::TrackAfter;
    }

    void scrollBy(float delta, std::function<void(float)> fn)
    {
        if (!fn)
            return;
        float span = thumbMax_ - thumbMin_;
        fn(std::clamp(thumbMin_ + delta, 0.f, 1.f - span));
    }
};