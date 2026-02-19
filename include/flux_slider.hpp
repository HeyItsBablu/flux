#ifndef FLUX_SLIDER_HPP
#define FLUX_SLIDER_HPP

#include "flux_core.hpp"
#include "flux_state.hpp"
#include <iostream>

template <typename T> class State;

class Widget;

using WidgetPtr = std::shared_ptr<Widget>;
using ClickHandler = std::function<void()>;
using HoverHandler = std::function<void(bool)>;

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

  void computeLayout(HDC hdc, int availableWidth, int availableHeight,
                     FontCache &fontCache) override {
    if (autoWidth)
      width = availableWidth;

    applyConstraints();
    needsLayout = false;
  }

  void render(HDC hdc, FontCache &fontCache) override {
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

  bool handleMouseUp(int mx, int my) override {
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

#endif