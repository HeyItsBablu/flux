#ifndef FLUX_INPUT_HPP
#define FLUX_INPUT_HPP

#include "flux_core.hpp"
#include "flux_state.hpp"
#include <algorithm>
#include <iostream>

template <typename T> class State;

class Widget;
class RadioGroupWidget;

using WidgetPtr = std::shared_ptr<Widget>;
using ClickHandler = std::function<void()>;
using HoverHandler = std::function<void(bool)>;

class ToggleWidget : public Widget {
public:
  bool toggled = false;

  // Dimensions
  int toggleWidth = 44;
  int toggleHeight = 24;
  int thumbSize = 18;

  // Colors
  COLORREF trackOffColor = RGB(200, 200, 200);
  COLORREF trackOnColor = RGB(76, 175, 80);
  COLORREF thumbColor = RGB(255, 255, 255);
  COLORREF thumbHoverColor = RGB(245, 245, 245);
  COLORREF thumbPressedColor = RGB(235, 235, 235);
  COLORREF shadowColor =
      RGB(180, 180,
          180); // was hardcoded RGB(0,0,0) at ~15% — replaced with a real color

  // Interaction states
  bool isThumbHovered = false;
  bool isPressed = false;

  // Animation state
  double animationProgress = 0.0;

  std::function<void(bool)> onToggleChanged;

  ToggleWidget() {
    width = toggleWidth;
    height = toggleHeight;
    autoWidth = false;
    autoHeight = false;
    paddingLeft = paddingRight = 4;
    paddingTop = paddingBottom = 4;
  }

  // ── Layout ──────────────────────────────────────────────────────────────
  void computeLayout(GraphicsContext &ctx, const BoxConstraints &constraints,
                     FontCache &fontCache) override {
    if (!text.empty()) {
      // Convert label to wide once
      int wlen = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
      std::wstring wtext(wlen, L'\0');
      MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, wtext.data(), wlen);

      NativeFont font = fontCache.getFont(fontSize, fontWeight);

      int tw = 0, th = 0;
      Painter(ctx).measureText(wtext, font, tw, th);

      width = toggleWidth + 12 + tw;
      height = max(toggleHeight, th);
    } else {
      width = toggleWidth;
      height = toggleHeight;
    }

    width = constraints.clampWidth(width + paddingLeft + paddingRight);
    height = constraints.clampHeight(height + paddingTop + paddingBottom);

    applyConstraints();
    needsLayout = false;
  }

  // ── Render ───────────────────────────────────────────────────────────────
  void render(GraphicsContext &ctx, FontCache &fontCache) override {
    Painter painter(ctx);

    int toggleX = x + paddingLeft;
    int toggleY = y + paddingTop +
                  (height - paddingTop - paddingBottom - toggleHeight) / 2;

    animationProgress = toggled ? 1.0 : 0.0;

    // ── Track ──────────────────────────────────────────────────────────────
    COLORREF trackColor =
        interpolateColor(trackOffColor, trackOnColor, animationProgress);
    int trackRadius = toggleHeight; // diameter passed to fillRoundedRectGDI
    painter.fillRoundedRectGDI(toggleX, toggleY, toggleWidth, toggleHeight,
                               trackRadius, trackColor, trackColor, 0);

    // ── Thumb position ─────────────────────────────────────────────────────
    int thumbPadding = (toggleHeight - thumbSize) / 2;
    int thumbOffX = toggleX + thumbPadding;
    int thumbOnX = toggleX + toggleWidth - thumbSize - thumbPadding;
    int thumbX = thumbOffX + (int)((thumbOnX - thumbOffX) * animationProgress);
    int thumbY = toggleY + thumbPadding;

    // ── Shadow (offset ellipse, muted gray) ───────────────────────────────
    painter.drawEllipse(thumbX - 1, thumbY + 2, thumbSize + 2, thumbSize,
                        shadowColor, shadowColor, 0);

    // ── Thumb ──────────────────────────────────────────────────────────────
    COLORREF currentThumbColor = isPressed        ? thumbPressedColor
                                 : isThumbHovered ? thumbHoverColor
                                                  : thumbColor;

    painter.drawEllipse(thumbX, thumbY, thumbSize, thumbSize, currentThumbColor,
                        RGB(230, 230, 230), 1);

    // ── Label text ─────────────────────────────────────────────────────────
    if (!text.empty()) {
      int wlen = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
      std::wstring wtext(wlen, L'\0');
      MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, wtext.data(), wlen);

      NativeFont font = fontCache.getFont(fontSize, fontWeight);

      int textX = toggleX + toggleWidth + 12;
      int textW = (x + width - paddingRight) - textX;

      painter.drawText(wtext, textX, y + paddingTop, textW,
                       height - paddingTop - paddingBottom, font,
                       getCurrentTextColor(),
                       DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }

    needsPaint = false;
  }

  // ── Event handlers (unchanged) ──────────────────────────────────────────

  bool handleMouseDown(int mx, int my) override {
    if (mx >= x && mx < x + width && my >= y && my < y + height) {
      isPressed = true;
      markNeedsPaint();
      return true;
    }
    return false;
  }

  bool handleMouseUp(int mx, int my) override {
    if (isPressed) {
      isPressed = false;
      if (mx >= x && mx < x + width && my >= y && my < y + height) {
        toggled = !toggled;
        notifyToggleChanged();
      }
      markNeedsPaint();
      return true;
    }
    return false;
  }

  bool handleMouseMove(int mx, int my) override {
    int toggleX = x + paddingLeft;
    int toggleY = y + paddingTop +
                  (height - paddingTop - paddingBottom - toggleHeight) / 2;

    bool nowHovered = (mx >= toggleX && mx < toggleX + toggleWidth &&
                       my >= toggleY && my < toggleY + toggleHeight);

    if (nowHovered != isThumbHovered) {
      isThumbHovered = nowHovered;
      markNeedsPaint();
      return true;
    }
    return false;
  }

  bool handleMouseLeave() override {
    if (isThumbHovered) {
      isThumbHovered = false;
      markNeedsPaint();
      return true;
    }
    return false;
  }

  bool handleKeyDown(int keyCode) override {
    if (keyCode == VK_SPACE || keyCode == VK_RETURN) {
      toggled = !toggled;
      notifyToggleChanged();
      markNeedsPaint();
      return true;
    }
    return false;
  }

  // ── Fluent setters (unchanged) ──────────────────────────────────────────

  std::shared_ptr<ToggleWidget> setToggled(bool value) {
    toggled = value;
    markNeedsPaint();
    return std::static_pointer_cast<ToggleWidget>(shared_from_this());
  }
  std::shared_ptr<ToggleWidget> setTrackOffColor(COLORREF color) {
    trackOffColor = color;
    markNeedsPaint();
    return std::static_pointer_cast<ToggleWidget>(shared_from_this());
  }
  std::shared_ptr<ToggleWidget> setTrackOnColor(COLORREF color) {
    trackOnColor = color;
    markNeedsPaint();
    return std::static_pointer_cast<ToggleWidget>(shared_from_this());
  }
  std::shared_ptr<ToggleWidget> setThumbColor(COLORREF color) {
    thumbColor = color;
    markNeedsPaint();
    return std::static_pointer_cast<ToggleWidget>(shared_from_this());
  }
  std::shared_ptr<ToggleWidget> setShadowColor(COLORREF color) {
    shadowColor = color;
    markNeedsPaint();
    return std::static_pointer_cast<ToggleWidget>(shared_from_this());
  }
  std::shared_ptr<ToggleWidget>
  setOnToggleChanged(std::function<void(bool)> cb) {
    onToggleChanged = cb;
    return std::static_pointer_cast<ToggleWidget>(shared_from_this());
  }
  std::shared_ptr<ToggleWidget> setValue(State<bool> &state) {
    toggled = state.get();
    state.bindProperty(
        shared_from_this(),
        [](Widget *w, const bool &val) {
          static_cast<ToggleWidget *>(w)->toggled = val;
        },
        false);
    boundBoolState = &state;
    return std::static_pointer_cast<ToggleWidget>(shared_from_this());
  }
  std::shared_ptr<ToggleWidget> setLabel(const std::string &label) {
    text = label;
    markNeedsLayout();
    return std::static_pointer_cast<ToggleWidget>(shared_from_this());
  }

private:
  State<bool> *boundBoolState = nullptr;

  void notifyToggleChanged() {
    if (onToggleChanged)
      onToggleChanged(toggled);
    if (boundBoolState)
      boundBoolState->set(toggled);
  }

  // Stays private — pure color math, no platform types
  static COLORREF interpolateColor(COLORREF c1, COLORREF c2, double t) {
    t = max(0.0, min(1.0, t));
    int r = GetRValue(c1) + (int)((GetRValue(c2) - GetRValue(c1)) * t);
    int g = GetGValue(c1) + (int)((GetGValue(c2) - GetGValue(c1)) * t);
    int b = GetBValue(c1) + (int)((GetBValue(c2) - GetBValue(c1)) * t);
    return RGB(r, g, b);
  }
};

// ----------------------------------------------------------------
// Factory Function
// ----------------------------------------------------------------
using ToggleWidgetPtr = std::shared_ptr<ToggleWidget>;

inline ToggleWidgetPtr Toggle(const std::string &label = "") {
  auto w = std::make_shared<ToggleWidget>();
  if (!label.empty())
    w->setLabel(label);
  return w;
}

class SliderWidget : public Widget {
public:
  double value = 0.0;
  double minValue = 0.0;
  double maxValue = 100.0;
  double step = 1.0;

  int trackHeight = 4;
  int thumbRadius = 10;

  COLORREF trackColor = RGB(200, 200, 200);
  COLORREF trackFillColor = RGB(33, 150, 243);
  COLORREF thumbColor = RGB(33, 150, 243);
  COLORREF thumbHoverColor = RGB(25, 118, 210);
  COLORREF thumbDragColor = RGB(13, 71, 161);

  bool isDragging = false;
  bool isThumbHovered = false;

  std::function<void(double)> onValueChanged;

  SliderWidget() {
    height = 40;
    autoHeight = false;
    paddingLeft = paddingRight = thumbRadius;
    paddingTop = paddingBottom = 10;
  }

  void computeLayout(GraphicsContext & /*ctx*/,
                     const BoxConstraints &constraints,
                     FontCache & /*fontCache*/) override {
    if (autoWidth)
      width = constraints.maxWidth;
    applyConstraints();
    needsLayout = false;
  }

  void render(GraphicsContext &ctx, FontCache & /*fontCache*/) override {
    int trackY = y + height / 2;
    int trackLeft = x + paddingLeft;
    int trackRight = x + width - paddingRight;
    int trackWidth = trackRight - trackLeft;

    double normalizedValue = (value - minValue) / (maxValue - minValue);
    int thumbX = trackLeft + (int)(normalizedValue * trackWidth);

    HBRUSH trackBrush = CreateSolidBrush(trackColor);
    RECT trackRect = {trackLeft, trackY - trackHeight / 2, trackRight,
                      trackY + trackHeight / 2};
    FillRect(ctx.hdc, &trackRect, trackBrush);
    DeleteObject(trackBrush);

    HBRUSH fillBrush = CreateSolidBrush(trackFillColor);
    RECT fillRect = {trackLeft, trackY - trackHeight / 2, thumbX,
                     trackY + trackHeight / 2};
    FillRect(ctx.hdc, &fillRect, fillBrush);
    DeleteObject(fillBrush);

    COLORREF currentThumbColor = thumbColor;
    if (isDragging)
      currentThumbColor = thumbDragColor;
    else if (isThumbHovered)
      currentThumbColor = thumbHoverColor;

    HBRUSH thumbBrush = CreateSolidBrush(currentThumbColor);
    HPEN thumbPen = CreatePen(PS_SOLID, 1, RGB(255, 255, 255));

    HBRUSH oldBrush = (HBRUSH)SelectObject(ctx.hdc, thumbBrush);
    HPEN oldPen = (HPEN)SelectObject(ctx.hdc, thumbPen);

    Ellipse(ctx.hdc, thumbX - thumbRadius, trackY - thumbRadius,
            thumbX + thumbRadius, trackY + thumbRadius);

    SelectObject(ctx.hdc, oldBrush);
    SelectObject(ctx.hdc, oldPen);
    DeleteObject(thumbBrush);
    DeleteObject(thumbPen);

    needsPaint = false;
  }

  bool handleMouseDown(int mx, int my) override {
    if (mx >= x && mx < x + width && my >= y && my < y + height) {
      isDragging = true;
      FluxUI::getCurrentInstance()->captureMouseInput(); // ← no raw HWND
      updateValueFromMouseX(mx);
      return true;
    }
    return false;
  }

  bool handleMouseUp(int /*mx*/, int /*my*/) override {
    if (isDragging) {
      isDragging = false;
      FluxUI::getCurrentInstance()->releaseMouseInput(); // ← no raw HWND
      markNeedsPaint();
      return true;
    }
    return false;
  }

  bool handleMouseMove(int mx, int my) override {
    if (isDragging) {
      updateValueFromMouseX(mx);
      return true;
    }

    int trackY = y + height / 2;
    int trackLeft = x + paddingLeft;
    int trackRight = x + width - paddingRight;
    int trackWidth = trackRight - trackLeft;

    double normalizedValue = (value - minValue) / (maxValue - minValue);
    int thumbX = trackLeft + (int)(normalizedValue * trackWidth);

    bool nowHovered =
        (mx >= thumbX - thumbRadius && mx <= thumbX + thumbRadius &&
         my >= trackY - thumbRadius && my <= trackY + thumbRadius);

    if (nowHovered != isThumbHovered) {
      isThumbHovered = nowHovered;
      markNeedsPaint();
      return true;
    }

    return false;
  }

  bool handleMouseLeave() override {
    if (isThumbHovered) {
      isThumbHovered = false;
      markNeedsPaint();
      return true;
    }
    return false;
  }

  bool handleKeyDown(int keyCode) override {
    double oldValue = value;

    switch (keyCode) {
    case VK_LEFT:
    case VK_DOWN:
      value -= step;
      break;

    case VK_RIGHT:
    case VK_UP:
      value += step;
      break;

    case VK_HOME:
      value = minValue;
      break;

    case VK_END:
      value = maxValue;
      break;

    default:
      return false;
    }

    value = max(minValue, min(maxValue, value));

    if (value != oldValue) {
      notifyValueChanged();
      markNeedsPaint();
      return true;
    }

    return false;
  }

  std::shared_ptr<SliderWidget> setMinValue(double min) {
    minValue = min;
    if (value < minValue)
      value = minValue;
    markNeedsPaint();
    return std::static_pointer_cast<SliderWidget>(shared_from_this());
  }

  std::shared_ptr<SliderWidget> setMaxValue(double max) {
    maxValue = max;
    if (value > maxValue)
      value = maxValue;
    markNeedsPaint();
    return std::static_pointer_cast<SliderWidget>(shared_from_this());
  }

  std::shared_ptr<SliderWidget> setStep(double s) {
    step = s;
    return std::static_pointer_cast<SliderWidget>(shared_from_this());
  }

  std::shared_ptr<SliderWidget> setTrackColor(COLORREF color) {
    trackColor = color;
    markNeedsPaint();
    return std::static_pointer_cast<SliderWidget>(shared_from_this());
  }

  std::shared_ptr<SliderWidget> setTrackFillColor(COLORREF color) {
    trackFillColor = color;
    markNeedsPaint();
    return std::static_pointer_cast<SliderWidget>(shared_from_this());
  }

  std::shared_ptr<SliderWidget> setThumbColor(COLORREF color) {
    thumbColor = color;
    markNeedsPaint();
    return std::static_pointer_cast<SliderWidget>(shared_from_this());
  }

  std::shared_ptr<SliderWidget>
  setOnValueChanged(std::function<void(double)> callback) {
    onValueChanged = callback;
    return std::static_pointer_cast<SliderWidget>(shared_from_this());
  }

  std::shared_ptr<SliderWidget> setValue(State<double> &state) {
    value = state.get();
    value = max(minValue, min(maxValue, value));

    state.bindProperty(
        shared_from_this(),
        [](Widget *w, const double &val) {
          auto *slider = static_cast<SliderWidget *>(w);
          slider->value = max(slider->minValue, min(slider->maxValue, val));
        },
        false);

    boundDoubleState = &state;

    return std::static_pointer_cast<SliderWidget>(shared_from_this());
  }

  std::shared_ptr<SliderWidget> setValue(State<int> &state) {
    value = (double)state.get();
    value = max(minValue, min(maxValue, value));

    state.bindProperty(
        shared_from_this(),
        [](Widget *w, const int &val) {
          auto *slider = static_cast<SliderWidget *>(w);
          slider->value =
              max(slider->minValue, min(slider->maxValue, (double)val));
        },
        false);

    boundIntState = &state;

    return std::static_pointer_cast<SliderWidget>(shared_from_this());
  }

  template <typename T, typename F>
  std::shared_ptr<SliderWidget> setValue(State<T> &state, F transform) {
    std::function<double(const T &)> fn = transform;

    // Initial value
    value = fn(state.get());
    value = max(minValue, min(maxValue, value));

    state.bindProperty(
        shared_from_this(),
        [fn](Widget *w, const T &val) {
          auto *slider = static_cast<SliderWidget *>(w);

          slider->value = fn(val);
          slider->value =
              max(slider->minValue, min(slider->maxValue, slider->value));
        },
        false);

    return std::static_pointer_cast<SliderWidget>(shared_from_this());
  }

  std::shared_ptr<SliderWidget> setWidth(int w) {
    width = w;
    autoWidth = false;

    return std::static_pointer_cast<SliderWidget>(shared_from_this());
  }

private:
  State<double> *boundDoubleState = nullptr;
  State<int> *boundIntState = nullptr;

  void updateValueFromMouseX(int mx) {
    int trackLeft = x + paddingLeft;
    int trackRight = x + width - paddingRight;
    int trackWidth = trackRight - trackLeft;

    int clampedX = max(trackLeft, min(trackRight, mx));

    double normalizedPos = (double)(clampedX - trackLeft) / trackWidth;

    double newValue = minValue + normalizedPos * (maxValue - minValue);

    if (step > 0) {
      newValue = round(newValue / step) * step;
    }

    newValue = max(minValue, min(maxValue, newValue));

    if (newValue != value) {
      value = newValue;
      notifyValueChanged();
      markNeedsPaint();
    }
  }

  void notifyValueChanged() {
    if (onValueChanged)
      onValueChanged(value);

    if (boundDoubleState)
      boundDoubleState->set(value);

    if (boundIntState)
      boundIntState->set((int)round(value));
  }
};

using SliderWidgetPtr = std::shared_ptr<SliderWidget>;

inline SliderWidgetPtr Slider(double minValue = 0.0, double maxValue = 100.0,
                              double step = 1.0) {
  auto w = std::make_shared<SliderWidget>();
  w->setMinValue(minValue);
  w->setMaxValue(maxValue);
  w->setStep(step);
  return w;
}

class CheckBoxWidget : public Widget {
public:
  bool checked = false;
  int boxSize = 16;

  void computeLayout(GraphicsContext &ctx, const BoxConstraints &constraints,
                     FontCache &fontCache) override {
    if (!text.empty()) {
      measureText(ctx, fontCache);
      width = boxSize + 8 + width;
    } else {
      width = boxSize;
    }

    height = max(boxSize, height);

    width = constraints.clampWidth(width + paddingLeft + paddingRight);
    height = constraints.clampHeight(height + paddingTop + paddingBottom);

    applyConstraints();
    needsLayout = false;
  }

  void render(GraphicsContext &ctx, FontCache &fontCache) override {
    int boxX = x + paddingLeft;
    int boxY =
        y + paddingTop + (height - paddingTop - paddingBottom - boxSize) / 2;

    HBRUSH boxBrush =
        CreateSolidBrush(checked ? RGB(76, 175, 80) : RGB(255, 255, 255));
    HPEN boxPen =
        CreatePen(PS_SOLID, 1, checked ? RGB(56, 155, 60) : RGB(150, 150, 150));

    HPEN oldPen = (HPEN)SelectObject(ctx.hdc, boxPen);
    HBRUSH oldBrush = (HBRUSH)SelectObject(ctx.hdc, boxBrush);

    Rectangle(ctx.hdc, boxX, boxY, boxX + boxSize, boxY + boxSize);

    SelectObject(ctx.hdc, oldBrush);
    SelectObject(ctx.hdc, oldPen);
    DeleteObject(boxBrush);
    DeleteObject(boxPen);

    if (checked) {
      HPEN checkPen = CreatePen(PS_SOLID, 2, RGB(255, 255, 255));
      HPEN oldCheckPen = (HPEN)SelectObject(ctx.hdc, checkPen);

      int cx = boxX + 3;
      int cy = boxY + boxSize / 2;

      MoveToEx(ctx.hdc, cx, cy, nullptr);
      LineTo(ctx.hdc, cx + 4, cy + 4);
      LineTo(ctx.hdc, cx + 9, cy - 4);

      SelectObject(ctx.hdc, oldCheckPen);
      DeleteObject(checkPen);
    }

    if (!text.empty()) {
      RECT textRect = {boxX + boxSize + 8, y + paddingTop,
                       x + width - paddingRight, y + height - paddingBottom};

      SetTextColor(ctx.hdc, getCurrentTextColor());
      SetBkMode(ctx.hdc, TRANSPARENT);

      HFONT hFont = fontCache.getFont(fontSize, fontWeight);
      HFONT hOldFont = (HFONT)SelectObject(ctx.hdc, hFont);

      DrawText(ctx.hdc, text.c_str(), -1, &textRect,
               DT_LEFT | DT_VCENTER | DT_SINGLELINE);

      SelectObject(ctx.hdc, hOldFont);
    }

    needsPaint = false;
  }

  bool handleMouseDown(int mx, int my) override {
    if (mx >= x && mx < x + width && my >= y && my < y + height) {
      checked = !checked;

      if (onClick)
        onClick();

      return true;
    }
    return false;
  }

  WidgetPtr setInputValue(State<bool> &state) {
    checked = state.get();

    state.bindProperty(
        shared_from_this(),
        [](Widget *w, const bool &val) {
          auto *cb = static_cast<CheckBoxWidget *>(w);
          cb->checked = val;
        },
        false);

    onClick = [&state, this]() { state.set(checked); };

    return shared_from_this();
  }
};

// ----------------------------------------------------------------
// Factory Functions
// ----------------------------------------------------------------

using CheckBoxWidgetPtr = std::shared_ptr<CheckBoxWidget>;

inline CheckBoxWidgetPtr CheckBox(const std::string &label = "") {
  auto w = std::make_shared<CheckBoxWidget>();
  w->text = label;
  w->textColor = RGB(30, 30, 30);
  w->paddingLeft = w->paddingRight = 4;
  w->paddingTop = w->paddingBottom = 4;
  return w;
}

// ============================================================================
// RADIO BUTTON WIDGET (Individual radio button)
// ============================================================================

class RadioButtonWidget : public Widget {
public:
  bool selected = false;
  int circleSize = 16;
  int innerCircleSize = 8;
  std::string value; // The value this radio represents

  COLORREF circleColor = RGB(150, 150, 150);
  COLORREF selectedCircleColor = RGB(33, 150, 243);
  COLORREF innerCircleColor = RGB(33, 150, 243);
  COLORREF hoverCircleColor = RGB(100, 100, 100);

  RadioGroupWidget *parentGroup = nullptr;

  RadioButtonWidget(const std::string &val = "") : value(val) {
    paddingLeft = paddingRight = 4;
    paddingTop = paddingBottom = 4;
  }

  void computeLayout(GraphicsContext &ctx, const BoxConstraints &constraints,
                     FontCache &fontCache) override {
    if (!text.empty()) {
      measureText(ctx, fontCache);
      width = circleSize + 8 + width;
    } else {
      width = circleSize;
    }

    height = max(circleSize, height);

    width = constraints.clampWidth(width + paddingLeft + paddingRight);
    height = constraints.clampHeight(height + paddingTop + paddingBottom);

    applyConstraints();
    needsLayout = false;
  }

  void render(GraphicsContext &ctx, FontCache &fontCache) override {
    int circleX = x + paddingLeft + circleSize / 2;
    int circleY = y + paddingTop + (height - paddingTop - paddingBottom) / 2;

    // Determine circle color
    COLORREF currentCircleColor =
        selected ? selectedCircleColor
                 : (isHovered ? hoverCircleColor : circleColor);

    // Draw outer circle
    HBRUSH circleBrush = CreateSolidBrush(RGB(255, 255, 255));
    HPEN circlePen = CreatePen(PS_SOLID, 2, currentCircleColor);

    HPEN oldPen = (HPEN)SelectObject(ctx.hdc, circlePen);
    HBRUSH oldBrush = (HBRUSH)SelectObject(ctx.hdc, circleBrush);

    Ellipse(ctx.hdc, circleX - circleSize / 2, circleY - circleSize / 2,
            circleX + circleSize / 2, circleY + circleSize / 2);

    SelectObject(ctx.hdc, oldBrush);
    SelectObject(ctx.hdc, oldPen);
    DeleteObject(circleBrush);
    DeleteObject(circlePen);

    // Draw inner circle if selected
    if (selected) {
      HBRUSH innerBrush = CreateSolidBrush(innerCircleColor);
      HPEN innerPen = CreatePen(PS_NULL, 0, 0);

      oldPen = (HPEN)SelectObject(ctx.hdc, innerPen);
      oldBrush = (HBRUSH)SelectObject(ctx.hdc, innerBrush);

      Ellipse(ctx.hdc, circleX - innerCircleSize / 2,
              circleY - innerCircleSize / 2, circleX + innerCircleSize / 2,
              circleY + innerCircleSize / 2);

      SelectObject(ctx.hdc, oldBrush);
      SelectObject(ctx.hdc, oldPen);
      DeleteObject(innerBrush);
      DeleteObject(innerPen);
    }

    // Draw label text
    if (!text.empty()) {
      RECT textRect = {x + paddingLeft + circleSize + 8, y + paddingTop,
                       x + width - paddingRight, y + height - paddingBottom};

      SetTextColor(ctx.hdc, getCurrentTextColor());
      SetBkMode(ctx.hdc, TRANSPARENT);

      HFONT hFont = fontCache.getFont(fontSize, fontWeight);
      HFONT hOldFont = (HFONT)SelectObject(ctx.hdc, hFont);

      DrawText(ctx.hdc, text.c_str(), -1, &textRect,
               DT_LEFT | DT_VCENTER | DT_SINGLELINE);

      SelectObject(ctx.hdc, hOldFont);
    }

    needsPaint = false;
  }

  bool handleMouseDown(int mx, int my) override {
    if (mx >= x && mx < x + width && my >= y && my < y + height) {
      selectThis();
      return true;
    }
    return false;
  }

  bool handleKeyDown(int keyCode) override {
    // Space or Enter to select
    if (keyCode == VK_SPACE || keyCode == VK_RETURN) {
      selectThis();
      return true;
    }
    return false;
  }

  void selectThis();

  std::shared_ptr<RadioButtonWidget> setSelected(bool sel) {
    selected = sel;
    markNeedsPaint();
    return std::static_pointer_cast<RadioButtonWidget>(shared_from_this());
  }

  std::shared_ptr<RadioButtonWidget> setValue(const std::string &val) {
    value = val;
    return std::static_pointer_cast<RadioButtonWidget>(shared_from_this());
  }

  std::shared_ptr<RadioButtonWidget> setCircleColor(COLORREF color) {
    circleColor = color;
    markNeedsPaint();
    return std::static_pointer_cast<RadioButtonWidget>(shared_from_this());
  }

  std::shared_ptr<RadioButtonWidget> setSelectedCircleColor(COLORREF color) {
    selectedCircleColor = color;
    innerCircleColor = color;
    markNeedsPaint();
    return std::static_pointer_cast<RadioButtonWidget>(shared_from_this());
  }
};

// ============================================================================
// RADIO GROUP WIDGET (Container for radio buttons)
// ============================================================================

class RadioGroupWidget : public Widget {
public:
  std::string selectedValue;
  std::vector<RadioButtonWidget *> radioButtons;

  bool isVertical = true;

  std::function<void(const std::string &)> onSelectionChanged;

  RadioGroupWidget() { spacing = 8; }

  void computeLayout(GraphicsContext &ctx, const BoxConstraints &constraints,
                     FontCache &fontCache) override {
    int totalWidth = 0;
    int totalHeight = 0;

    for (auto &child : children) {
      child->computeLayout(ctx, constraints, fontCache);

      if (isVertical) {
        totalHeight += child->height + child->marginTop + child->marginBottom;
        maxWidth = max(maxWidth,
                       child->width + child->marginLeft + child->marginRight);
      } else {
        totalWidth += child->width + child->marginLeft + child->marginRight;
        maxHeight = max(maxHeight,
                        child->height + child->marginTop + child->marginBottom);
      }
    }

    int spacingTotal =
        children.empty() ? 0 : (int)(children.size() - 1) * spacing;

    if (isVertical) {
      totalHeight += spacingTotal;
      width = constraints.clampWidth(
          autoWidth ? maxWidth + paddingLeft + paddingRight : width);
      height = constraints.clampHeight(
          autoHeight ? totalHeight + paddingTop + paddingBottom : height);
    } else {
      totalWidth += spacingTotal;
      width = constraints.clampWidth(
          autoWidth ? totalWidth + paddingLeft + paddingRight : width);
      height = constraints.clampHeight(
          autoHeight ? maxHeight + paddingTop + paddingBottom : height);
    }

    applyConstraints();
    needsLayout = false;
  }

  void positionChildren(int contentX, int contentY, int /*contentWidth*/,
                        int /*contentHeight*/) override {
    int currentX = contentX;
    int currentY = contentY;

    for (auto &child : children) {
      child->x = currentX + child->marginLeft;
      child->y = currentY + child->marginTop;

      if (isVertical) {
        currentY +=
            child->height + child->marginTop + child->marginBottom + spacing;
      } else {
        currentX +=
            child->width + child->marginLeft + child->marginRight + spacing;
      }

      child->positionChildren(
          child->x + child->paddingLeft, child->y + child->paddingTop,
          child->width - child->paddingLeft - child->paddingRight,
          child->height - child->paddingTop - child->paddingBottom);
    }
  }

  void render(GraphicsContext &ctx, FontCache &fontCache) override {
    // Render background if needed
    if (hasBackground) {
      drawRoundedRectangle(ctx);
    }

    // Render all children
    for (auto &child : children) {
      child->render(ctx, fontCache);
    }

    needsPaint = false;
  }

  void addRadioButton(std::shared_ptr<RadioButtonWidget> radio) {
    radio->parentGroup = this;
    radioButtons.push_back(radio.get());
    addChild(radio);

    // If this is the first button or it matches selectedValue, select it
    if (radioButtons.size() == 1 || radio->value == selectedValue) {
      radio->selected = true;
      selectedValue = radio->value;
    }
  }

  void selectRadioButton(RadioButtonWidget *selectedRadio) {
    if (!selectedRadio)
      return;

    // Deselect all radio buttons
    for (auto *radio : radioButtons) {
      if (radio != selectedRadio && radio->selected) {
        radio->selected = false;
        radio->markNeedsPaint();
      }
    }

    // Select the clicked one
    if (!selectedRadio->selected) {
      selectedRadio->selected = true;
      selectedRadio->markNeedsPaint();
    }

    // Update selected value
    selectedValue = selectedRadio->value;

    // Notify callback
    if (onSelectionChanged) {
      onSelectionChanged(selectedValue);
    }

    // Notify state binding
    if (boundStringState) {
      boundStringState->set(selectedValue);
    }
  }

  std::shared_ptr<RadioGroupWidget> setOrientation(bool vertical) {
    isVertical = vertical;
    markNeedsLayout();
    return std::static_pointer_cast<RadioGroupWidget>(shared_from_this());
  }

  std::shared_ptr<RadioGroupWidget> setHorizontal() {
    return setOrientation(false);
  }

  std::shared_ptr<RadioGroupWidget> setVertical() {
    return setOrientation(true);
  }

  std::shared_ptr<RadioGroupWidget> setSelectedValue(const std::string &value) {
    selectedValue = value;

    // Update radio buttons to match
    for (auto *radio : radioButtons) {
      radio->selected = (radio->value == value);
      radio->markNeedsPaint();
    }

    return std::static_pointer_cast<RadioGroupWidget>(shared_from_this());
  }

  std::shared_ptr<RadioGroupWidget>
  setOnSelectionChanged(std::function<void(const std::string &)> callback) {
    onSelectionChanged = callback;
    return std::static_pointer_cast<RadioGroupWidget>(shared_from_this());
  }

  std::shared_ptr<RadioGroupWidget> bindValue(State<std::string> &state) {
    selectedValue = state.get();

    // Update radio buttons to match initial state
    for (auto *radio : radioButtons) {
      radio->selected = (radio->value == selectedValue);
      radio->markNeedsPaint();
    }

    state.bindProperty(
        shared_from_this(),
        [](Widget *w, const std::string &val) {
          auto *group = static_cast<RadioGroupWidget *>(w);
          group->selectedValue = val;

          // Update radio buttons
          for (auto *radio : group->radioButtons) {
            radio->selected = (radio->value == val);
            radio->markNeedsPaint();
          }
        },
        false);

    boundStringState = &state;

    return std::static_pointer_cast<RadioGroupWidget>(shared_from_this());
  }

  std::string getSelectedValue() const { return selectedValue; }

private:
  State<std::string> *boundStringState = nullptr;
};

// Now we can implement RadioButtonWidget::selectThis()
inline void RadioButtonWidget::selectThis() {
  if (parentGroup) {
    parentGroup->selectRadioButton(this);
  } else {
    // Standalone radio button (not recommended, but handle it)
    selected = !selected;
    markNeedsPaint();

    if (onClick)
      onClick();
  }
}

// ============================================================================
// FACTORY FUNCTIONS
// ============================================================================

using RadioButtonWidgetPtr = std::shared_ptr<RadioButtonWidget>;
using RadioGroupWidgetPtr = std::shared_ptr<RadioGroupWidget>;

inline RadioButtonWidgetPtr RadioButton(const std::string &value,
                                        const std::string &label = "") {
  auto w = std::make_shared<RadioButtonWidget>(value);
  w->text = label.empty() ? value : label;
  w->textColor = RGB(30, 30, 30);
  return w;
}

inline RadioGroupWidgetPtr RadioGroup() {
  return std::make_shared<RadioGroupWidget>();
}

// ============================================================================
// CONVENIENCE BUILDER FOR RADIO GROUP WITH OPTIONS
// ============================================================================

// Helper struct to distinguish between value-label pairs and simple options
struct RadioOption {
  std::string value;
  std::string label;

  RadioOption(const std::string &v, const std::string &l)
      : value(v), label(l) {}
  RadioOption(const char *v, const char *l) : value(v), label(l) {}
};

inline RadioGroupWidgetPtr
RadioGroupWithOptions(const std::initializer_list<RadioOption> &options) {
  auto group = std::make_shared<RadioGroupWidget>();

  for (const auto &option : options) {
    auto radio = RadioButton(option.value, option.label);
    group->addRadioButton(radio);
  }

  return group;
}

// Overload for simple string options (value = label)
inline RadioGroupWidgetPtr
RadioGroupWithOptions(const std::initializer_list<std::string> &options) {
  auto group = std::make_shared<RadioGroupWidget>();

  for (const auto &option : options) {
    auto radio = RadioButton(option, option);
    group->addRadioButton(radio);
  }

  return group;
}

class TextInputWidget : public Widget {
public:
  std::string inputValue;
  std::string placeholder;
  int cursorPos = 0;
  bool cursorVisible = true;
  TimerID cursorTimerId = 0;
  int scrollOffset = 0;

  COLORREF focusedBorderColor = RGB(33, 150, 243);
  COLORREF unfocusedBorderColor = RGB(180, 180, 180);
  COLORREF placeholderColor = RGB(180, 180, 180);
  COLORREF inputTextColor = RGB(30, 30, 30);

  TextInputWidget() {
    isFocusable = true;
    hasBorder = true;
    hasBackground = true;
    backgroundColor = RGB(255, 255, 255);
    borderColor = unfocusedBorderColor;
    borderWidth = 1;
    borderRadius = 4;
    paddingLeft = paddingRight = 10;
    paddingTop = paddingBottom = 8;
    height = 36;
    autoHeight = false;
  }

  void computeLayout(GraphicsContext & /*ctx*/,
                     const BoxConstraints &constraints,
                     FontCache & /*fontCache*/) override {
    if (autoWidth)
      width = constraints.maxWidth;
    applyConstraints();
    needsLayout = false;
  }

  void render(GraphicsContext &ctx, FontCache &fontCache) override {
    borderColor = isFocused ? focusedBorderColor : unfocusedBorderColor;
    drawRoundedRectangle(ctx);

    HFONT hFont = fontCache.getFont(fontSize, fontWeight);
    HFONT hOldFont = (HFONT)SelectObject(ctx.hdc, hFont);

    int textX = x + paddingLeft;

    RECT clipRect = {x + paddingLeft, y + paddingTop, x + width - paddingRight,
                     y + height - paddingBottom};

    HRGN clipRgn = CreateRectRgn(clipRect.left, clipRect.top, clipRect.right,
                                 clipRect.bottom);
    SelectClipRgn(ctx.hdc, clipRgn);

    SetBkMode(ctx.hdc, TRANSPARENT);

    if (inputValue.empty() && !placeholder.empty()) {
      SetTextColor(ctx.hdc, placeholderColor);
      RECT pr = clipRect;
      DrawText(ctx.hdc, placeholder.c_str(), -1, &pr,
               DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    } else {
      SetTextColor(ctx.hdc, inputTextColor);
      RECT tr = clipRect;
      tr.left -= scrollOffset;
      DrawText(ctx.hdc, inputValue.c_str(), -1, &tr,
               DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);
    }

    if (isFocused && cursorVisible) {
      SIZE textSize = {0};
      if (cursorPos > 0) {
        GetTextExtentPoint32(ctx.hdc, inputValue.c_str(), cursorPos, &textSize);
      }

      int cursorX = textX + textSize.cx - scrollOffset;
      int cursorY1 = y + paddingTop + 2;
      int cursorY2 = y + height - paddingBottom - 2;

      HPEN cursorPen = CreatePen(PS_SOLID, 1, RGB(30, 30, 30));
      HPEN oldPen = (HPEN)SelectObject(ctx.hdc, cursorPen);

      MoveToEx(ctx.hdc, cursorX, cursorY1, nullptr);
      LineTo(ctx.hdc, cursorX, cursorY2);

      SelectObject(ctx.hdc, oldPen);
      DeleteObject(cursorPen);
    }

    SelectClipRgn(ctx.hdc, nullptr);
    DeleteObject(clipRgn);

    SelectObject(ctx.hdc, hOldFont);
    needsPaint = false;
  }

  bool handleFocus(bool focused) override {
    isFocused = focused;

    auto *ui = FluxUI::getCurrentInstance();
    if (focused) {
      cursorVisible = true;
      cursorTimerId = ui->setInterval(530, [this]() {
        cursorVisible = !cursorVisible;
        markNeedsPaint();
      });
    } else {
      if (cursorTimerId) {
        ui->clearInterval(cursorTimerId);
        cursorTimerId = 0;
      }
      cursorVisible = false;
    }

    markNeedsPaint();
    return true;
  }

  bool handleMouseDown(int mx, int my) override {
    if (mx >= x && mx < x + width && my >= y && my < y + height) {
      cursorPos = getCursorPosFromX(mx - x - paddingLeft + scrollOffset);
      return true;
    }
    return false;
  }

  bool handleChar(wchar_t ch) override {
    if (ch < 32)
      return false;

    std::string charStr(1, (char)ch);
    inputValue.insert(cursorPos, charStr);
    cursorPos++;
    cursorVisible = true;

    updateScroll();
    notifyStateBinding();
    return true;
  }

  bool handleKeyDown(int keyCode) override {
    switch (keyCode) {
    case VK_BACK:
      if (cursorPos > 0) {
        inputValue.erase(cursorPos - 1, 1);
        cursorPos--;
        cursorVisible = true;
        updateScroll();
        notifyStateBinding();
        return true;
      }
      break;

    case VK_DELETE:
      if (cursorPos < (int)inputValue.size()) {
        inputValue.erase(cursorPos, 1);
        cursorVisible = true;
        updateScroll();
        notifyStateBinding();
        return true;
      }
      break;

    case VK_LEFT:
      if (cursorPos > 0) {
        cursorPos--;
        cursorVisible = true;
        updateScroll();
        return true;
      }
      break;

    case VK_RIGHT:
      if (cursorPos < (int)inputValue.size()) {
        cursorPos++;
        cursorVisible = true;
        updateScroll();
        return true;
      }
      break;

    case VK_HOME:
      cursorPos = 0;
      scrollOffset = 0;
      return true;

    case VK_END:
      cursorPos = (int)inputValue.size();
      updateScroll();
      return true;
    }
    return false;
  }

  std::shared_ptr<TextInputWidget> setInputValue(State<std::string> &state) {
    inputValue = state.get();
    cursorPos = (int)inputValue.size();
    scrollOffset = 0;

    state.bindProperty(
        shared_from_this(),
        [](Widget *w, const std::string &val) {
          auto *input = static_cast<TextInputWidget *>(w);
          input->inputValue = val;
          input->cursorPos = (int)val.size();
        },
        false);

    boundStringState = &state;

    return std::static_pointer_cast<TextInputWidget>(shared_from_this());
  }

  std::shared_ptr<TextInputWidget> setPlaceholder(const std::string &ph) {
    placeholder = ph;
    return std::static_pointer_cast<TextInputWidget>(shared_from_this());
  }
  std::shared_ptr<TextInputWidget> setWidth(int w) {
    width = w;
    autoWidth = false;

    return std::static_pointer_cast<TextInputWidget>(shared_from_this());
  }

private:
  State<std::string> *boundStringState = nullptr;

  void notifyStateBinding() {
    if (boundStringState)
      boundStringState->set(inputValue);
  }

  int getCursorPosFromX(int pixelX) {
    if (inputValue.empty())
      return 0;

    auto *ui = FluxUI::getCurrentInstance();
    MeasureContext mc = ui->getMeasureContext();
    FontCache &fc = ui->getFontCache();

    HFONT hFont = fc.getFont(fontSize, fontWeight);
    HFONT hOldFont = (HFONT)SelectObject(mc.ctx.hdc, hFont);

    int bestPos = 0;
    int bestDist = abs(pixelX);

    for (int i = 1; i <= (int)inputValue.size(); i++) {
      SIZE sz;
      GetTextExtentPoint32(mc.ctx.hdc, inputValue.c_str(), i, &sz);
      int dist = abs(sz.cx - pixelX);
      if (dist < bestDist) {
        bestDist = dist;
        bestPos = i;
      }
    }

    SelectObject(mc.ctx.hdc, hOldFont);
    return bestPos; // MeasureContext destructor releases DC
  }

  void updateScroll() {
    if (inputValue.empty()) {
      scrollOffset = 0;
      return;
    }

    auto *ui = FluxUI::getCurrentInstance();
    MeasureContext mc = ui->getMeasureContext();
    FontCache &fc = ui->getFontCache();

    HFONT hFont = fc.getFont(fontSize, fontWeight);
    HFONT hOldFont = (HFONT)SelectObject(mc.ctx.hdc, hFont);

    SIZE sz = {};
    if (cursorPos > 0)
      GetTextExtentPoint32(mc.ctx.hdc, inputValue.c_str(), cursorPos, &sz);

    SelectObject(mc.ctx.hdc, hOldFont);
    // MeasureContext destructor releases DC

    int textAreaWidth = width - paddingLeft - paddingRight;
    int cursorX = sz.cx - scrollOffset;

    if (cursorX < 10)
      scrollOffset = max(0, sz.cx - 10);
    else if (cursorX > textAreaWidth - 10)
      scrollOffset = sz.cx - textAreaWidth + 10;
  }
};

// ----------------------------------------------------------------
// Factory Functions
// ----------------------------------------------------------------
using TextInputWidgetPtr = std::shared_ptr<TextInputWidget>;

inline TextInputWidgetPtr TextInput(const std::string &placeholder = "") {
  auto w = std::make_shared<TextInputWidget>();
  if (!placeholder.empty())
    w->setPlaceholder(placeholder);
  return w;
}

#endif