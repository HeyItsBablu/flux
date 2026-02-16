#ifndef FLUX_TOGGLE_HPP
#define FLUX_TOGGLE_HPP

#include "flux_core.hpp"
#include "flux_state.hpp"

template <typename T> class State;

class Widget;

using WidgetPtr = std::shared_ptr<Widget>;

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

  void computeLayout(HDC hdc, int availableWidth, int availableHeight,
                     FontCache &fontCache) override {
    // If there's text (label), measure it and add to width
    if (!text.empty()) {
      HFONT hFont = fontCache.getFont(fontSize, fontWeight);
      HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);

      SIZE textSize;
      GetTextExtentPoint32(hdc, text.c_str(), (int)text.length(), &textSize);

      width = toggleWidth + 12 +
              textSize.cx; // 12px spacing between toggle and label
      height = max(toggleHeight, textSize.cy);

      SelectObject(hdc, hOldFont);
    } else {
      width = toggleWidth;
      height = toggleHeight;
    }

    width += paddingLeft + paddingRight;
    height += paddingTop + paddingBottom;

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

#endif // FLUX_TOGGLE_HPP