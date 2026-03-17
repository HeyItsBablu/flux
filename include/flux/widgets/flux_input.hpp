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

  // Interaction states
  bool isThumbHovered = false;
  bool isPressed = false;

  // Animation state (for future smooth transitions)
  double animationProgress = 0.0; // 0.0 = off, 1.0 = on

  std::function<void(bool)> onToggleChanged;

  ToggleWidget() {
    width = toggleWidth;
    height = toggleHeight;
    autoWidth = false;
    autoHeight = false;
    paddingLeft = paddingRight = 4;
    paddingTop = paddingBottom = 4;
  }

  void computeLayout(HDC hdc, const BoxConstraints &constraints,
                     FontCache &fontCache) override {
    if (!text.empty()) {
      HFONT hFont = fontCache.getFont(fontSize, fontWeight);
      HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);

      SIZE textSize;
      GetTextExtentPoint32(hdc, text.c_str(), (int)text.length(), &textSize);

      width = toggleWidth + 12 + textSize.cx;
      height = max(toggleHeight, (int)textSize.cy);

      SelectObject(hdc, hOldFont);
    } else {
      width = toggleWidth;
      height = toggleHeight;
    }

    width = constraints.clampWidth(width + paddingLeft + paddingRight);
    height = constraints.clampHeight(height + paddingTop + paddingBottom);

    applyConstraints();
    needsLayout = false;
  }

  void render(HDC hdc, FontCache &fontCache) override {
    int toggleX = x + paddingLeft;
    int toggleY = y + paddingTop +
                  (height - paddingTop - paddingBottom - toggleHeight) / 2;

    // Update animation progress to match toggle state (instant for now)
    animationProgress = toggled ? 1.0 : 0.0;

    // Interpolate track color based on animation progress
    COLORREF currentTrackColor =
        interpolateColor(trackOffColor, trackOnColor, animationProgress);

    // Draw track (rounded rectangle)
    HBRUSH trackBrush = CreateSolidBrush(currentTrackColor);
    HPEN trackPen = CreatePen(PS_NULL, 0, 0);

    HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, trackBrush);
    HPEN oldPen = (HPEN)SelectObject(hdc, trackPen);

    int trackRadius = toggleHeight / 2;
    RoundRect(hdc, toggleX, toggleY, toggleX + toggleWidth,
              toggleY + toggleHeight, trackRadius * 2, trackRadius * 2);

    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(trackBrush);
    DeleteObject(trackPen);

    // Calculate thumb position based on animation progress
    int thumbPadding = (toggleHeight - thumbSize) / 2;
    int thumbOffX = toggleX + thumbPadding;
    int thumbOnX = toggleX + toggleWidth - thumbSize - thumbPadding;
    int thumbX = thumbOffX + (int)((thumbOnX - thumbOffX) * animationProgress);
    int thumbY = toggleY + thumbPadding;

    // Determine thumb color based on interaction state
    COLORREF currentThumbColor = thumbColor;
    if (isPressed)
      currentThumbColor = thumbPressedColor;
    else if (isThumbHovered)
      currentThumbColor = thumbHoverColor;

    // Draw thumb shadow (subtle effect)
    HBRUSH shadowBrush = CreateSolidBrush(RGB(0, 0, 0));
    SetBkMode(hdc, TRANSPARENT);

    BLENDFUNCTION blend = {0};
    blend.BlendOp = AC_SRC_OVER;
    blend.SourceConstantAlpha = 40; // ~15% opacity

    // Simple shadow by drawing slightly offset circle
    HBRUSH oldShadowBrush = (HBRUSH)SelectObject(hdc, shadowBrush);
    HPEN shadowPen = CreatePen(PS_NULL, 0, 0);
    HPEN oldShadowPen = (HPEN)SelectObject(hdc, shadowPen);

    // Draw shadow (slightly below thumb)
    Ellipse(hdc, thumbX - 1, thumbY + 2, thumbX + thumbSize + 1,
            thumbY + thumbSize + 2);

    SelectObject(hdc, oldShadowPen);
    SelectObject(hdc, oldShadowBrush);
    DeleteObject(shadowPen);
    DeleteObject(shadowBrush);

    // Draw thumb
    HBRUSH thumbBrush = CreateSolidBrush(currentThumbColor);
    HPEN thumbPen = CreatePen(PS_SOLID, 1, RGB(230, 230, 230));

    oldBrush = (HBRUSH)SelectObject(hdc, thumbBrush);
    oldPen = (HPEN)SelectObject(hdc, thumbPen);

    Ellipse(hdc, thumbX, thumbY, thumbX + thumbSize, thumbY + thumbSize);

    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(thumbBrush);
    DeleteObject(thumbPen);

    // Draw label text if present
    if (!text.empty()) {
      RECT textRect = {toggleX + toggleWidth + 12, y + paddingTop,
                       x + width - paddingRight, y + height - paddingBottom};

      SetTextColor(hdc, getCurrentTextColor());
      SetBkMode(hdc, TRANSPARENT);

      HFONT hFont = fontCache.getFont(fontSize, fontWeight);
      HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);

      DrawText(hdc, text.c_str(), -1, &textRect,
               DT_LEFT | DT_VCENTER | DT_SINGLELINE);

      SelectObject(hdc, hOldFont);
    }

    needsPaint = false;
  }

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

      // Only toggle if mouse is still over the widget
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
    // Allow space or enter to toggle when focused
    if (keyCode == VK_SPACE || keyCode == VK_RETURN) {
      toggled = !toggled;
      notifyToggleChanged();
      markNeedsPaint();
      return true;
    }
    return false;
  }

  // Builder methods
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

  std::shared_ptr<ToggleWidget>
  setOnToggleChanged(std::function<void(bool)> callback) {
    onToggleChanged = callback;
    return std::static_pointer_cast<ToggleWidget>(shared_from_this());
  }

  std::shared_ptr<ToggleWidget> setValue(State<bool> &state) {
    toggled = state.get();

    state.bindProperty(
        shared_from_this(),
        [](Widget *w, const bool &val) {
          auto *toggle = static_cast<ToggleWidget *>(w);
          toggle->toggled = val;
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

  // Helper function to interpolate between two colors
  COLORREF interpolateColor(COLORREF color1, COLORREF color2, double t) {
    t = max(0.0, min(1.0, t)); // Clamp t to [0, 1]

    int r1 = GetRValue(color1);
    int g1 = GetGValue(color1);
    int b1 = GetBValue(color1);

    int r2 = GetRValue(color2);
    int g2 = GetGValue(color2);
    int b2 = GetBValue(color2);

    int r = r1 + (int)((r2 - r1) * t);
    int g = g1 + (int)((g2 - g1) * t);
    int b = b1 + (int)((b2 - b1) * t);

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

  void computeLayout(HDC /*hdc*/, const BoxConstraints &constraints,
                     FontCache &/*fontCache*/) override {
    if (autoWidth)
      width = constraints.maxWidth;
    applyConstraints();
    needsLayout = false;
  }

  void render(HDC hdc, FontCache &/*fontCache*/) override {
    int trackY = y + height / 2;
    int trackLeft = x + paddingLeft;
    int trackRight = x + width - paddingRight;
    int trackWidth = trackRight - trackLeft;

    double normalizedValue = (value - minValue) / (maxValue - minValue);
    int thumbX = trackLeft + (int)(normalizedValue * trackWidth);

    HBRUSH trackBrush = CreateSolidBrush(trackColor);
    RECT trackRect = {trackLeft, trackY - trackHeight / 2, trackRight,
                      trackY + trackHeight / 2};
    FillRect(hdc, &trackRect, trackBrush);
    DeleteObject(trackBrush);

    HBRUSH fillBrush = CreateSolidBrush(trackFillColor);
    RECT fillRect = {trackLeft, trackY - trackHeight / 2, thumbX,
                     trackY + trackHeight / 2};
    FillRect(hdc, &fillRect, fillBrush);
    DeleteObject(fillBrush);

    COLORREF currentThumbColor = thumbColor;
    if (isDragging)
      currentThumbColor = thumbDragColor;
    else if (isThumbHovered)
      currentThumbColor = thumbHoverColor;

    HBRUSH thumbBrush = CreateSolidBrush(currentThumbColor);
    HPEN thumbPen = CreatePen(PS_SOLID, 1, RGB(255, 255, 255));

    HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, thumbBrush);
    HPEN oldPen = (HPEN)SelectObject(hdc, thumbPen);

    Ellipse(hdc, thumbX - thumbRadius, trackY - thumbRadius,
            thumbX + thumbRadius, trackY + thumbRadius);

    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(thumbBrush);
    DeleteObject(thumbPen);

    needsPaint = false;
  }

  bool handleMouseDown(int mx, int my) override {
    if (mx >= x && mx < x + width && my >= y && my < y + height) {
      isDragging = true;

      HWND hwnd = FluxUI::getCurrentInstance()->getWindow();
      SetCapture(hwnd);

      updateValueFromMouseX(mx);
      return true;
    }
    return false;
  }

  bool handleMouseUp(int /*mx*/, int /*my*/) override {
    if (isDragging) {
      isDragging = false;
      ReleaseCapture();
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

  void computeLayout(HDC hdc, const BoxConstraints &constraints,
                     FontCache &fontCache) override {
    if (!text.empty()) {
      measureText(hdc, fontCache);
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

  void render(HDC hdc, FontCache &fontCache) override {
    int boxX = x + paddingLeft;
    int boxY =
        y + paddingTop + (height - paddingTop - paddingBottom - boxSize) / 2;

    HBRUSH boxBrush =
        CreateSolidBrush(checked ? RGB(76, 175, 80) : RGB(255, 255, 255));
    HPEN boxPen =
        CreatePen(PS_SOLID, 1, checked ? RGB(56, 155, 60) : RGB(150, 150, 150));

    HPEN oldPen = (HPEN)SelectObject(hdc, boxPen);
    HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, boxBrush);

    Rectangle(hdc, boxX, boxY, boxX + boxSize, boxY + boxSize);

    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(boxBrush);
    DeleteObject(boxPen);

    if (checked) {
      HPEN checkPen = CreatePen(PS_SOLID, 2, RGB(255, 255, 255));
      HPEN oldCheckPen = (HPEN)SelectObject(hdc, checkPen);

      int cx = boxX + 3;
      int cy = boxY + boxSize / 2;

      MoveToEx(hdc, cx, cy, nullptr);
      LineTo(hdc, cx + 4, cy + 4);
      LineTo(hdc, cx + 9, cy - 4);

      SelectObject(hdc, oldCheckPen);
      DeleteObject(checkPen);
    }

    if (!text.empty()) {
      RECT textRect = {boxX + boxSize + 8, y + paddingTop,
                       x + width - paddingRight, y + height - paddingBottom};

      SetTextColor(hdc, getCurrentTextColor());
      SetBkMode(hdc, TRANSPARENT);

      HFONT hFont = fontCache.getFont(fontSize, fontWeight);
      HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);

      DrawText(hdc, text.c_str(), -1, &textRect,
               DT_LEFT | DT_VCENTER | DT_SINGLELINE);

      SelectObject(hdc, hOldFont);
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

  void computeLayout(HDC hdc, const BoxConstraints &constraints,
                     FontCache &fontCache) override {
    if (!text.empty()) {
      measureText(hdc, fontCache);
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

  void render(HDC hdc, FontCache &fontCache) override {
    int circleX = x + paddingLeft + circleSize / 2;
    int circleY = y + paddingTop + (height - paddingTop - paddingBottom) / 2;

    // Determine circle color
    COLORREF currentCircleColor =
        selected ? selectedCircleColor
                 : (isHovered ? hoverCircleColor : circleColor);

    // Draw outer circle
    HBRUSH circleBrush = CreateSolidBrush(RGB(255, 255, 255));
    HPEN circlePen = CreatePen(PS_SOLID, 2, currentCircleColor);

    HPEN oldPen = (HPEN)SelectObject(hdc, circlePen);
    HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, circleBrush);

    Ellipse(hdc, circleX - circleSize / 2, circleY - circleSize / 2,
            circleX + circleSize / 2, circleY + circleSize / 2);

    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(circleBrush);
    DeleteObject(circlePen);

    // Draw inner circle if selected
    if (selected) {
      HBRUSH innerBrush = CreateSolidBrush(innerCircleColor);
      HPEN innerPen = CreatePen(PS_NULL, 0, 0);

      oldPen = (HPEN)SelectObject(hdc, innerPen);
      oldBrush = (HBRUSH)SelectObject(hdc, innerBrush);

      Ellipse(hdc, circleX - innerCircleSize / 2, circleY - innerCircleSize / 2,
              circleX + innerCircleSize / 2, circleY + innerCircleSize / 2);

      SelectObject(hdc, oldBrush);
      SelectObject(hdc, oldPen);
      DeleteObject(innerBrush);
      DeleteObject(innerPen);
    }

    // Draw label text
    if (!text.empty()) {
      RECT textRect = {x + paddingLeft + circleSize + 8, y + paddingTop,
                       x + width - paddingRight, y + height - paddingBottom};

      SetTextColor(hdc, getCurrentTextColor());
      SetBkMode(hdc, TRANSPARENT);

      HFONT hFont = fontCache.getFont(fontSize, fontWeight);
      HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);

      DrawText(hdc, text.c_str(), -1, &textRect,
               DT_LEFT | DT_VCENTER | DT_SINGLELINE);

      SelectObject(hdc, hOldFont);
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

void computeLayout(HDC hdc, const BoxConstraints &constraints,
                   FontCache &fontCache) override {
  int totalWidth  = 0;
  int totalHeight = 0;


  for (auto &child : children) {
    child->computeLayout(hdc, constraints, fontCache);

    if (isVertical) {
      totalHeight += child->height + child->marginTop  + child->marginBottom;
      maxWidth     = max(maxWidth,  child->width  + child->marginLeft + child->marginRight);
    } else {
      totalWidth  += child->width  + child->marginLeft + child->marginRight;
      maxHeight    = max(maxHeight, child->height + child->marginTop  + child->marginBottom);
    }
  }

  int spacingTotal = children.empty() ? 0 : (int)(children.size() - 1) * spacing;

  if (isVertical) {
    totalHeight += spacingTotal;
    width  = constraints.clampWidth(autoWidth   ? maxWidth    + paddingLeft + paddingRight  : width);
    height = constraints.clampHeight(autoHeight ? totalHeight + paddingTop  + paddingBottom : height);
  } else {
    totalWidth += spacingTotal;
    width  = constraints.clampWidth(autoWidth   ? totalWidth  + paddingLeft + paddingRight  : width);
    height = constraints.clampHeight(autoHeight ? maxHeight   + paddingTop  + paddingBottom : height);
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

  void render(HDC hdc, FontCache &fontCache) override {
    // Render background if needed
    if (hasBackground) {
      drawRoundedRectangle(hdc);
    }

    // Render all children
    for (auto &child : children) {
      child->render(hdc, fontCache);
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
  UINT cursorTimerId = 1;
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

void computeLayout(HDC /*hdc*/, const BoxConstraints &constraints,
                   FontCache &/*fontCache*/) override {
  if (autoWidth) width = constraints.maxWidth;
  applyConstraints();
  needsLayout = false;
}

  void render(HDC hdc, FontCache &fontCache) override {
    borderColor = isFocused ? focusedBorderColor : unfocusedBorderColor;
    drawRoundedRectangle(hdc);

    HFONT hFont = fontCache.getFont(fontSize, fontWeight);
    HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);

    int textX = x + paddingLeft;


    RECT clipRect = {x + paddingLeft, y + paddingTop, x + width - paddingRight,
                     y + height - paddingBottom};

    HRGN clipRgn = CreateRectRgn(clipRect.left, clipRect.top, clipRect.right,
                                 clipRect.bottom);
    SelectClipRgn(hdc, clipRgn);

    SetBkMode(hdc, TRANSPARENT);

    if (inputValue.empty() && !placeholder.empty()) {
      SetTextColor(hdc, placeholderColor);
      RECT pr = clipRect;
      DrawText(hdc, placeholder.c_str(), -1, &pr,
               DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    } else {
      SetTextColor(hdc, inputTextColor);
      RECT tr = clipRect;
      tr.left -= scrollOffset;
      DrawText(hdc, inputValue.c_str(), -1, &tr,
               DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);
    }

    if (isFocused && cursorVisible) {
      SIZE textSize = {0};
      if (cursorPos > 0) {
        GetTextExtentPoint32(hdc, inputValue.c_str(), cursorPos, &textSize);
      }

      int cursorX = textX + textSize.cx - scrollOffset;
      int cursorY1 = y + paddingTop + 2;
      int cursorY2 = y + height - paddingBottom - 2;

      HPEN cursorPen = CreatePen(PS_SOLID, 1, RGB(30, 30, 30));
      HPEN oldPen = (HPEN)SelectObject(hdc, cursorPen);

      MoveToEx(hdc, cursorX, cursorY1, nullptr);
      LineTo(hdc, cursorX, cursorY2);

      SelectObject(hdc, oldPen);
      DeleteObject(cursorPen);
    }

    SelectClipRgn(hdc, nullptr);
    DeleteObject(clipRgn);

    SelectObject(hdc, hOldFont);
    needsPaint = false;
  }

  bool handleFocus(bool focused) override {
    isFocused = focused;

    if (focused) {
      HWND hwnd = FluxUI::getCurrentInstance()->getWindow();
      SetTimer(hwnd, cursorTimerId, 530, nullptr);
      cursorVisible = true;
    } else {
      HWND hwnd = FluxUI::getCurrentInstance()->getWindow();
      KillTimer(hwnd, cursorTimerId);
      cursorVisible = false;
    }

    markNeedsPaint();
    return true;
  }

  bool handleTimer(UINT timerId) override {
    if (timerId == cursorTimerId) {
      cursorVisible = !cursorVisible;
      return true;
    }
    return false;
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

    HWND hwnd = FluxUI::getCurrentInstance()->getWindow();
    HDC hdc = GetDC(hwnd);
    FontCache &fc = FluxUI::getCurrentInstance()->getFontCache();

    HFONT hFont = fc.getFont(fontSize, fontWeight);
    HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);

    int bestPos = 0;
    int bestDist = abs(pixelX);

    for (int i = 1; i <= (int)inputValue.size(); i++) {
      SIZE sz;
      GetTextExtentPoint32(hdc, inputValue.c_str(), i, &sz);
      int dist = abs(sz.cx - pixelX);
      if (dist < bestDist) {
        bestDist = dist;
        bestPos = i;
      }
    }

    SelectObject(hdc, hOldFont);
    ReleaseDC(hwnd, hdc);
    return bestPos;
  }

  void updateScroll() {
    if (inputValue.empty()) {
      scrollOffset = 0;
      return;
    }

    HWND hwnd = FluxUI::getCurrentInstance()->getWindow();
    HDC hdc = GetDC(hwnd);
    FontCache &fc = FluxUI::getCurrentInstance()->getFontCache();

    HFONT hFont = fc.getFont(fontSize, fontWeight);
    HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);

    SIZE sz = {0};
    if (cursorPos > 0)
      GetTextExtentPoint32(hdc, inputValue.c_str(), cursorPos, &sz);

    SelectObject(hdc, hOldFont);
    ReleaseDC(hwnd, hdc);

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