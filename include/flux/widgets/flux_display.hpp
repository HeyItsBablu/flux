#ifndef FLUX_DISPLAY_HPP
#define FLUX_DISPLAY_HPP

#include "flux_core.hpp"
#include "flux_state.hpp"
#include "flux_widget.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

// ============================================================================
// TEXT WIDGET
// ============================================================================

class TextWidget : public Widget {
public:
  std::string fontFamily = "Segoe UI";

  void computeLayout(GraphicsContext &ctx, const BoxConstraints &constraints,
                     FontCache &fontCache) override {
    measureText(ctx, fontCache);
    if (autoWidth)
      width += paddingLeft + paddingRight;
    if (autoHeight)
      height += paddingTop + paddingBottom;
    width = constraints.clampWidth(width);
    height = constraints.clampHeight(height);
    applyConstraints();
    needsLayout = false;
  }

  void render(GraphicsContext &ctx, FontCache &fontCache) override {
    if (hasBackground)
      drawRoundedRectangle(ctx);
    renderText(ctx, fontCache);
    needsPaint = false;
  }

  std::shared_ptr<TextWidget> setFontSize(int size) {
    if (fontSize != size) {
      fontSize = size;
      markNeedsLayout();
    }
    return std::static_pointer_cast<TextWidget>(shared_from_this());
  }
  std::shared_ptr<TextWidget> setFontWeight(FontWeight weight) {
    if (fontWeight != weight) {
      fontWeight = weight;
      markNeedsLayout();
    }
    return std::static_pointer_cast<TextWidget>(shared_from_this());
  }
  std::shared_ptr<TextWidget> setText(const std::string &t) {
    if (text != t) {
      text = t;
      markNeedsLayout();
    }
    return std::static_pointer_cast<TextWidget>(shared_from_this());
  }

  template <typename T, typename F>
  std::shared_ptr<TextWidget> setText(State<T> &state, F transform) {
    std::function<std::string(const T &)> fn = transform;
    text = fn(state.get());
    state.bindProperty(
        shared_from_this(),
        [fn](Widget *w, const T &val) { w->text = fn(val); }, true);
    return std::static_pointer_cast<TextWidget>(shared_from_this());
  }

  template <typename T> std::shared_ptr<TextWidget> setText(State<T> &state) {
    text = valueToString(state.get());
    state.bindProperty(
        shared_from_this(),
        [](Widget *w, const T &val) { w->text = valueToString(val); }, true);
    return std::static_pointer_cast<TextWidget>(shared_from_this());
  }

  std::shared_ptr<TextWidget> setTextColor(COLORREF color) {
    if (textColor != color) {
      textColor = color;
      markNeedsPaint();
    }
    return std::static_pointer_cast<TextWidget>(shared_from_this());
  }

  template <typename T, typename F>
  std::shared_ptr<TextWidget> setTextColor(State<T> &state, F transform) {
    std::function<COLORREF(const T &)> fn = transform;
    textColor = fn(state.get());
    state.bindProperty(
        shared_from_this(),
        [fn](Widget *w, const T &val) { w->textColor = fn(val); }, false);
    return std::static_pointer_cast<TextWidget>(shared_from_this());
  }

  std::shared_ptr<TextWidget> setHoverTextColor(COLORREF color) {
    hoverTextColor = color;
    hasHoverTextColor = true;
    markNeedsPaint();
    return std::static_pointer_cast<TextWidget>(shared_from_this());
  }
  std::shared_ptr<TextWidget> setPadding(int p) {
    padding = p;
    paddingLeft = paddingRight = paddingTop = paddingBottom = p;
    markNeedsLayout();
    return std::static_pointer_cast<TextWidget>(shared_from_this());
  }
  std::shared_ptr<TextWidget> setBackgroundColor(COLORREF color) {
    backgroundColor = color;
    hasBackground = true;
    markNeedsPaint();
    return std::static_pointer_cast<TextWidget>(shared_from_this());
  }
  std::shared_ptr<TextWidget> setBorderRadius(int r) {
    borderRadius = r;
    markNeedsPaint();
    return std::static_pointer_cast<TextWidget>(shared_from_this());
  }
  std::shared_ptr<TextWidget> setMinWidth(int w) {
    minWidth = w;
    markNeedsLayout();
    return std::static_pointer_cast<TextWidget>(shared_from_this());
  }
  std::shared_ptr<TextWidget> setWidth(int w) {
    width = w;
    autoWidth = false;
    markNeedsLayout();
    return std::static_pointer_cast<TextWidget>(shared_from_this());
  }
  std::shared_ptr<TextWidget> setHeight(int h) {
    height = h;
    autoHeight = false;
    markNeedsLayout();
    return std::static_pointer_cast<TextWidget>(shared_from_this());
  }

  std::shared_ptr<TextWidget> setFontFamily(const std::string &family) {
    if (fontFamily != family) {
      fontFamily = family;
      markNeedsLayout();
    }
    return std::static_pointer_cast<TextWidget>(shared_from_this());
  }
};

// ============================================================================
// ICON WIDGET
// ============================================================================

class IconWidget : public TextWidget {
public:
  IconWidget() { fontFamily = "Segoe MDL2 Assets"; }

  void computeLayout(GraphicsContext & /*ctx*/,
                     const BoxConstraints &constraints,
                     FontCache & /*fontCache*/) override {
    if (autoWidth)
      width = fontSize + paddingLeft + paddingRight;
    if (autoHeight)
      height = fontSize + paddingTop + paddingBottom;
    width = constraints.clampWidth(width);
    height = constraints.clampHeight(height);
    applyConstraints();
    needsLayout = false;
  }

  void render(GraphicsContext &ctx, FontCache &fontCache) override {
    if (hasBackground)
      drawRoundedRectangle(ctx);

    if (text.empty()) {
      needsPaint = false;
      return;
    }

    int wlen = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    std::wstring wtext(wlen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, wtext.data(), wlen);

    NativeFont font = fontCache.getFont(fontFamily, fontSize, fontWeight);

    Painter(ctx).drawText(wtext, x, y, width, height, font,
                          getCurrentTextColor(),
                          DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    needsPaint = false;
  }

  std::shared_ptr<IconWidget> setSize(int size) {
    setFontSize(size);
    return std::static_pointer_cast<IconWidget>(shared_from_this());
  }
  std::shared_ptr<IconWidget> setColor(COLORREF color) {
    setTextColor(color);
    return std::static_pointer_cast<IconWidget>(shared_from_this());
  }
  std::shared_ptr<IconWidget> setHoverColor(COLORREF color) {
    setHoverTextColor(color);
    return std::static_pointer_cast<IconWidget>(shared_from_this());
  }
  std::shared_ptr<IconWidget> setIconFontFamily(const std::string &family) {
    if (fontFamily != family) {
      fontFamily = family;
      markNeedsLayout();
    }
    return std::static_pointer_cast<IconWidget>(shared_from_this());
  }

  template <typename T>
  std::shared_ptr<IconWidget>
  setGlyph(State<T> &state, std::function<wchar_t(const T &)> transform) {
    auto fn = [transform](const T &val) -> std::string {
      return wcharToUtf8(transform(val));
    };
    setText(state, fn);
    return std::static_pointer_cast<IconWidget>(shared_from_this());
  }

  static std::string wcharToUtf8(wchar_t glyph) {
    wchar_t buf[2] = {glyph, L'\0'};
    int len =
        WideCharToMultiByte(CP_UTF8, 0, buf, 1, nullptr, 0, nullptr, nullptr);
    std::string result(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, buf, 1, result.data(), len, nullptr,
                        nullptr);
    return result;
  }
};

using IconWidgetPtr = std::shared_ptr<IconWidget>;

inline IconWidgetPtr Icon(wchar_t glyph,
                          const std::string &fontFamily = "Segoe MDL2 Assets",
                          int size = 16) {
  auto w = std::make_shared<IconWidget>();
  w->fontFamily = fontFamily;
  w->fontSize = size;
  w->text = IconWidget::wcharToUtf8(glyph);
  return w;
}

template <typename T>
inline IconWidgetPtr
Icon(State<T> &state, std::function<wchar_t(const T &)> transform,
     const std::string &fontFamily = "Segoe MDL2 Assets", int size = 16) {
  auto w = std::make_shared<IconWidget>();
  w->fontFamily = fontFamily;
  w->setFontSize(size);
  w->setGlyph(state, transform);
  return w;
}

// ============================================================================
// PROGRESS BAR WIDGET  (GDI only — no GL)
// ============================================================================

class ProgressBarWidget : public Widget {
public:
  double value = 0.0;
  int trackBorderRadius = 4;
  COLORREF trackColor = RGB(220, 220, 220);
  COLORREF trackBorderColor = RGB(0, 0, 0);
  bool hasTrackBorder = false;
  int trackBorderWidth = 1;
  std::vector<COLORREF> progressColors = {RGB(33, 150, 243)};

  ProgressBarWidget() {
    height = 12;
    autoHeight = false;
  }

  std::shared_ptr<ProgressBarWidget> setValue(State<double> &state) {
    value = max(0.0, min(1.0, state.get()));
    state.bindProperty(
        shared_from_this(),
        [](Widget *w, const double &val) {
          auto *bar = static_cast<ProgressBarWidget *>(w);
          bar->value = max(0.0, min(1.0, val));
          bar->markNeedsPaint();
        },
        false);
    return std::static_pointer_cast<ProgressBarWidget>(shared_from_this());
  }
  std::shared_ptr<ProgressBarWidget> setValue(double v) {
    value = max(0.0, min(1.0, v));
    markNeedsPaint();
    return std::static_pointer_cast<ProgressBarWidget>(shared_from_this());
  }
  std::shared_ptr<ProgressBarWidget> setBackgroundColor(COLORREF color) {
    trackColor = color;
    markNeedsPaint();
    return std::static_pointer_cast<ProgressBarWidget>(shared_from_this());
  }
  std::shared_ptr<ProgressBarWidget>
  setProgressColors(std::vector<COLORREF> c) {
    if (!c.empty())
      progressColors = c;
    markNeedsPaint();
    return std::static_pointer_cast<ProgressBarWidget>(shared_from_this());
  }
  std::shared_ptr<ProgressBarWidget> setBorderColor(COLORREF color) {
    trackBorderColor = color;
    hasTrackBorder = true;
    markNeedsPaint();
    return std::static_pointer_cast<ProgressBarWidget>(shared_from_this());
  }
  std::shared_ptr<ProgressBarWidget> setBorderWidth(int w) {
    trackBorderWidth = w;
    markNeedsPaint();
    return std::static_pointer_cast<ProgressBarWidget>(shared_from_this());
  }
  std::shared_ptr<ProgressBarWidget> setBorderRadius(int r) {
    trackBorderRadius = r;
    markNeedsPaint();
    return std::static_pointer_cast<ProgressBarWidget>(shared_from_this());
  }
  std::shared_ptr<ProgressBarWidget> setHeight(int h) {
    height = h;
    autoHeight = false;
    markNeedsPaint();
    return std::static_pointer_cast<ProgressBarWidget>(shared_from_this());
  }
  std::shared_ptr<ProgressBarWidget> setWidth(int w) {
    width = w;
    autoWidth = false;
    markNeedsLayout();
    return std::static_pointer_cast<ProgressBarWidget>(shared_from_this());
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
    Painter painter(ctx);
    int rx = trackBorderRadius * 2;

    // Track
    painter.fillRoundedRectGDI(x, y, width, height, rx, trackColor,
                               trackBorderColor,
                               hasTrackBorder ? trackBorderWidth : 0);

    // Fill
    int fillWidth = (int)(value * width);
    if (fillWidth > 0) {
      painter.pushClipRoundedRect(x, y, width, height, rx);
      painter.fillGradientRect(x, y, fillWidth, height, progressColors);
      painter.popClipRect();

      if (hasTrackBorder)
        painter.drawRoundedRectOutline(x, y, fillWidth, height, rx,
                                       trackBorderColor, trackBorderWidth);
    }

    needsPaint = false;
  }
};

using ProgressBarWidgetPtr = std::shared_ptr<ProgressBarWidget>;

inline ProgressBarWidgetPtr ProgressBar(double value = 0.0) {
  auto w = std::make_shared<ProgressBarWidget>();
  w->setValue(value);
  return w;
}

// ============================================================================
// DIVIDER WIDGET
// ============================================================================

class DividerWidget : public Widget {
public:
  void computeLayout(GraphicsContext & /*ctx*/,
                     const BoxConstraints &constraints,
                     FontCache & /*fontCache*/) override {
    if (autoWidth)
      width = constraints.maxWidth;
    applyConstraints();
    needsLayout = false;
  }
  void render(GraphicsContext &ctx, FontCache & /*fontCache*/) override {
    if (hasBackground)
      drawRoundedRectangle(ctx);
    needsPaint = false;
  }
};

// ============================================================================
// FACTORY HELPERS
// ============================================================================

using TextWidgetPtr = std::shared_ptr<TextWidget>;

inline TextWidgetPtr Text(const std::string &text) {
  auto w = std::make_shared<TextWidget>();
  w->text = text;
  return w;
}

template <typename T> inline TextWidgetPtr Text(State<T> &state) {
  auto w = std::make_shared<TextWidget>();
  w->text = state.toString();
  state.addObserver(w);
  return w;
}

template <typename T, typename F>
inline TextWidgetPtr Text(State<T> &state, F transform) {
  auto w = std::make_shared<TextWidget>();
  w->setText(state, std::function<std::string(const T &)>(transform));
  return w;
}

inline WidgetPtr Divider() {
  auto w = std::make_shared<DividerWidget>();
  w->height = 1;
  w->autoHeight = false;
  w->hasBackground = true;
  w->backgroundColor = RGB(0, 0, 0);
  return w;
}

#endif // FLUX_DISPLAY_HPP