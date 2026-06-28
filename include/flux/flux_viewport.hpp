#pragma once
// ============================================================================
// flux_viewport.hpp
// Viewport / ScrollbarInfo — pure data + logic, no platform headers, no GL.
// ============================================================================

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

// ============================================================================
// SCROLLBAR
// ============================================================================

struct ScrollbarInfo
{
  float thumbMin = 0.f;
  float thumbMax = 1.f;
  bool visible = false;
};

// ============================================================================
// VIEWPORT
// ============================================================================

static constexpr float kMinZoom = 1.f / 16.f;
static constexpr float kMaxZoom = 32.f;
static constexpr float kSnapTolerance = 0.04f;

static constexpr std::array<float, 12> kSnapZooms = {
    0.0625f, 0.125f, 0.25f, 0.5f, 0.75f, 1.0f,
    1.5f, 2.0f, 4.0f, 8.0f, 16.0f, 32.0f};

class Viewport
{
public:
  Viewport() = default;

  void init(int viewW, int viewH, int canvasW, int canvasH)
  {
    vw_ = float(viewW);
    vh_ = float(viewH);
    cw_ = float(canvasW);
    ch_ = float(canvasH);
    zoom_ = 1.f;
    offsetX_ = 0.f;
    offsetY_ = 0.f;
  }

  void setViewSize(int w, int h)
  {
    vw_ = float(w);
    vh_ = float(h);
    clampOffset();
  }
  void setCanvasSize(int w, int h)
  {
    cw_ = float(w);
    ch_ = float(h);
    clampOffset();
  }

  void zoomToward(float sx, float sy, float factor)
  {
    float newZoom = snapZoom(std::clamp(zoom_ * factor, kMinZoom, kMaxZoom));
    if (newZoom == zoom_)
      return;
    auto [cpx, cpy] = screenToCanvas(sx, sy);
    zoom_ = newZoom;
    offsetX_ = cpx - sx / zoom_;
    offsetY_ = cpy - (vh_ - sy) / zoom_;
    clampOffset();
  }

  void zoomIn() { zoomToward(vw_ * .5f, vh_ * .5f, 1.25f); }
  void zoomOut() { zoomToward(vw_ * .5f, vh_ * .5f, 0.8f); }
  void resetZoom()
  {
    zoom_ = 1.f;
    centerCanvas();
  }
  void fitToView()
  {
    float f = std::min(vw_ / cw_, vh_ / ch_);
    zoom_ = snapZoom(std::clamp(f, kMinZoom, kMaxZoom));
    centerCanvas();
  }

  void panByScreen(float dsx, float dsy)
  {
    float screenOX = offsetX_ * zoom_ - dsx;
    float screenOY = offsetY_ * zoom_ + dsy;
    offsetX_ = std::roundf(screenOX) / zoom_;
    offsetY_ = std::roundf(screenOY) / zoom_;
    clampOffset();
  }

  void setOffsetX(float cx)
  {
    offsetX_ = cx;
    clampOffset();
  }
  void setOffsetY(float cy)
  {
    offsetY_ = cy;
    clampOffset();
  }
  void setOffset(float cx, float cy)
  {
    offsetX_ = std::roundf(cx * zoom_) / zoom_;
    offsetY_ = std::roundf(cy * zoom_) / zoom_;
    clampOffset();
  }

  std::pair<float, float> screenToCanvas(float sx, float sy) const
  {
    return {offsetX_ + sx / zoom_, offsetY_ + sy / zoom_};
  }

  ScrollbarInfo scrollbarH() const
  {
    float view = vw_ / zoom_;
    if (view >= cw_)
      return {0, 1, false};
    return {std::clamp(offsetX_ / cw_, 0.f, 1.f),
            std::clamp((offsetX_ + view) / cw_, 0.f, 1.f), true};
  }

  ScrollbarInfo scrollbarV() const
  {
    float view = vh_ / zoom_;
    if (view >= ch_)
      return {0, 1, false};
    float end = offsetY_ + view;
    return {std::clamp(1.f - end / ch_, 0.f, 1.f),
            std::clamp(1.f - offsetY_ / ch_, 0.f, 1.f), true};
  }


  void buildMVP(float out[16]) const;

  float zoom() const { return zoom_; }
  float offsetX() const { return offsetX_; }
  float offsetY() const { return offsetY_; }
  float viewW() const { return vw_; }
  float viewH() const { return vh_; }
  float canvasW() const { return cw_; }
  float canvasH() const { return ch_; }

private:
  float vw_ = 1, vh_ = 1, cw_ = 1, ch_ = 1;
  float zoom_ = 1.f, offsetX_ = 0.f, offsetY_ = 0.f;

  static float snapZoom(float z)
  {
    for (float s : kSnapZooms)
      if (std::abs(z - s) / s < kSnapTolerance)
        return s;
    return z;
  }

  void centerCanvas()
  {
    offsetX_ = (cw_ - vw_ / zoom_) * .5f;
    offsetY_ = (ch_ - vh_ / zoom_) * .5f;
    clampOffset();
  }

  void clampOffset()
  {
    float viewW = vw_ / zoom_, viewH = vh_ / zoom_;
    constexpr float kPanSlack = 0.5f;
    float slackX = viewW * kPanSlack, slackY = viewH * kPanSlack;
    if (viewW >= cw_)
      offsetX_ = std::clamp(offsetX_, -slackX, cw_ - viewW + slackX);
    else
      offsetX_ = std::clamp(offsetX_, 0.f, cw_ - viewW);
    if (viewH >= ch_)
      offsetY_ = std::clamp(offsetY_, -slackY, ch_ - viewH + slackY);
    else
      offsetY_ = std::clamp(offsetY_, 0.f, ch_ - viewH);
  }
};