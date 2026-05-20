#ifndef FLUX_DISPLAY_HPP
#define FLUX_DISPLAY_HPP

#include "../flux_core.hpp"
#include "../flux_state.hpp"
#include "../flux_text_style.hpp"
#include "../flux_widget.hpp"
#include "flux_icons.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

// ============================================================================
// TEXT WIDGET
// Text widget with full TextStyle, TextAlign, TextOverflow,
// TextAlignVertical, softWrap, maxLines, letterSpacing, lineHeight,
// textDecoration, shadows, textScaleFactor, textDirection support.
// ============================================================================

class TextWidget : public Widget {
public:
  // ── Layout / overflow params ──────────────────────────────────────────
  TextAlign textAlign = TextAlign::Left;
  TextAlignVertical textAlignVertical = TextAlignVertical::Top;
  TextOverflow textOverflow = TextOverflow::Visible;
  TextDirection textDirection = TextDirection::LTR;
  bool softWrap = true;
  int maxLines = 0; // 0 = unlimited

  // ── Typography style ──────────────────────────────────────────────────

  TextStyle style;



  TextWidget() {
    // Sync the base-class defaults into style
    style.fontFamily = "Segoe UI";
    style.fontSize = 14;
    style.fontWeight = FontWeight::Normal;
    style.color = Color::fromRGB(0, 0, 0);
  }

  // -----------------------------------------------------------------------
  // computeLayout
  // -----------------------------------------------------------------------

  // In flux_display.hpp — TextWidget::computeLayout
  void computeLayout(GraphicsContext &ctx, const BoxConstraints &constraints,
                     FontCache &fontCache) override {
    int hPad = paddingLeft + paddingRight;
    int vPad = paddingTop + paddingBottom;

    // ── Width ─────────────────────────────────────────────────────────────
    if (autoWidth) {
      width = constraints.maxWidth; // take all available width
    }
    width = constraints.clampWidth(width);

    // ── Height ────────────────────────────────────────────────────────────
    if (autoHeight) {
      int innerW = width - hPad;
      if (innerW < 0)
        innerW = 0;

      int measW = 0, measH = 0;
      Painter(ctx).measureRichText(toWideString(text), style, fontCache,
                                   softWrap ? innerW : 0, softWrap, maxLines,
                                   measW, measH);

      height = measH + vPad;
    }

    height = constraints.clampHeight(height);
    applyConstraints();
    needsLayout = false;
  }

  // -----------------------------------------------------------------------
  // render
  // -----------------------------------------------------------------------

  // In flux_display.hpp — TextWidget::render
  void render(GraphicsContext &ctx, FontCache &fontCache) override {
    if (hasBackground)
      drawRoundedRectangle(ctx);

    if (!text.empty()) {
      Painter::RichTextParams params;
      params.x = x + paddingLeft;
      params.y = y + paddingTop;

      params.w = width - paddingLeft - paddingRight;
      params.h = height - paddingTop - paddingBottom;

      if (params.w < 0)
        params.w = 0;
      if (params.h < 0)
        params.h = 0;

      params.textAlign = textAlign;
      params.textAlignVertical = textAlignVertical;
      params.overflow = textOverflow;
      params.direction = textDirection;
      params.softWrap = softWrap;
      params.maxLines = maxLines;
      params.style = style;

      if (isHovered && hasHoverTextColor)
        params.style.color = hoverTextColor;

      Painter(ctx).drawRichText(toWideString(text), params, fontCache);
    }

    needsPaint = false;
  }

  // =========================================================================
  // FLUENT SETTERS — All return shared_ptr<TextWidget> for chaining.
  // =========================================================================

  // ── Text content ─────────────────────────────────────────────────────────

  std::shared_ptr<TextWidget> setText(const std::string &t) {
    if (text != t) {
      text = t;
      markNeedsLayout();
    }
    return self();
  }

  template <typename T, typename F>
  std::shared_ptr<TextWidget> setText(State<T> &state, F transform) {
    std::function<std::string(const T &)> fn = transform;
    text = fn(state.get());
    state.bindProperty(
        shared_from_this(),
        [fn](Widget *w, const T &val) { w->text = fn(val); }, true);
    return self();
  }

  template <typename T> std::shared_ptr<TextWidget> setText(State<T> &state) {
    text = valueToString(state.get());
    state.bindProperty(
        shared_from_this(),
        [](Widget *w, const T &val) { w->text = valueToString(val); }, true);
    return self();
  }

  // ── Font ─────────────────────────────────────────────────────────────────

  std::shared_ptr<TextWidget> setFontSize(int size) {
    if (style.fontSize != size) {
      style.fontSize = size;
      markNeedsLayout();
    }
    fontSize = size; // keep base class in sync
    return self();
  }

  std::shared_ptr<TextWidget> setFontWeight(FontWeight weight) {
    if (style.fontWeight != weight) {
      style.fontWeight = weight;
      markNeedsLayout();
    }
    fontWeight = weight;
    return self();
  }

  std::shared_ptr<TextWidget> setFontFamily(const std::string &family) {
    if (style.fontFamily != family) {
      style.fontFamily = family;
      markNeedsLayout();
    }
    fontFamily = family;
    return self();
  }

  std::shared_ptr<TextWidget> setTextScaleFactor(float factor) {
    if (style.textScaleFactor != factor) {
      style.textScaleFactor = factor;
      markNeedsLayout();
    }
    return self();
  }

  // ── Color ─────────────────────────────────────────────────────────────────

  std::shared_ptr<TextWidget> setTextColor(Color color) {
    if (style.color != color) {
      style.color = color;
      markNeedsPaint();
    }
    textColor = color;
    return self();
  }

  template <typename T, typename F>
  std::shared_ptr<TextWidget> setTextColor(State<T> &state, F transform) {
    std::function<Color(const T &)> fn = transform;
    style.color = fn(state.get());
    state.bindProperty(
        shared_from_this(),
        [fn](Widget *w, const T &val) {
          auto *tw = static_cast<TextWidget *>(w);
          tw->style.color = fn(val);
          tw->markNeedsPaint();
        },
        false);
    return self();
  }

  std::shared_ptr<TextWidget> setHoverTextColor(Color color) {
    hoverTextColor = color;
    hasHoverTextColor = true;
    markNeedsPaint();
    return self();
  }

  // ── Spacing ───────────────────────────────────────────────────────────────

  std::shared_ptr<TextWidget> setLetterSpacing(float lineSpacing) {
    if (style.letterSpacing != lineSpacing) {
      style.letterSpacing = lineSpacing;
      markNeedsLayout();
    }
    return self();
  }

  std::shared_ptr<TextWidget> setWordSpacing(float wordSpacing) {
    if (style.wordSpacing != wordSpacing) {
      style.wordSpacing = wordSpacing;
      markNeedsLayout();
    }
    return self();
  }

  /// Height multiplier for line height (default 1.0 = natural).
  /// 1.5 = 50% extra leading between lines, like Flutter's height param.
  std::shared_ptr<TextWidget> setHeight(float h) {
    if (style.height != h) {
      style.height = h;
      markNeedsLayout();
    }
    return self();
  }

  // ── Alignment ─────────────────────────────────────────────────────────────

  /// Horizontal text alignment (Left, Center, Right, Justify, Start, End)
  std::shared_ptr<TextWidget> setTextAlign(TextAlign align) {
    if (textAlign != align) {
      textAlign = align;
      markNeedsPaint();
    }
    return self();
  }

  /// Vertical alignment of the text block within the widget (Top, Center,
  /// Bottom)
  std::shared_ptr<TextWidget> setTextAlignVertical(TextAlignVertical align) {
    if (textAlignVertical != align) {
      textAlignVertical = align;
      markNeedsPaint();
    }
    return self();
  }

  // ── Overflow ──────────────────────────────────────────────────────────────

  /// How text overflow is handled (Clip, Ellipsis, Fade, Visible)
  std::shared_ptr<TextWidget> setOverflow(TextOverflow newOverflow) {
    if (textOverflow != newOverflow) {
      textOverflow = newOverflow;
      markNeedsPaint();
    }
    return self();
  }

  // ── Wrapping ──────────────────────────────────────────────────────────────

  /// Whether text wraps at word boundaries (default true)
  std::shared_ptr<TextWidget> setSoftWrap(bool wrap) {
    if (softWrap != wrap) {
      softWrap = wrap;
      markNeedsLayout();
    }
    return self();
  }

  /// Maximum number of visible lines. 0 = unlimited.
  std::shared_ptr<TextWidget> setMaxLines(int lines) {
    if (maxLines != lines) {
      maxLines = lines;
      markNeedsLayout();
    }
    return self();
  }

  // ── Text direction ────────────────────────────────────────────────────────

  std::shared_ptr<TextWidget> setTextDirection(TextDirection dir) {
    if (textDirection != dir) {
      textDirection = dir;
      markNeedsPaint();
    }
    return self();
  }

  // ── Decoration ────────────────────────────────────────────────────────────

  std::shared_ptr<TextWidget> setDecoration(TextDecoration decoration) {
    if (style.decoration != decoration) {
      style.decoration = decoration;
      markNeedsLayout(); // font key changes (underline/strikeout in HFONT)
    }
    return self();
  }

  std::shared_ptr<TextWidget> setDecorationColor(Color color) {
    if (style.decorationColor != color) {
      style.decorationColor = color;
      markNeedsPaint();
    }
    return self();
  }

  std::shared_ptr<TextWidget> setDecorationStyle(TextDecorationStyle ds) {
    if (style.decorationStyle != ds) {
      style.decorationStyle = ds;
      markNeedsPaint();
    }
    return self();
  }

  std::shared_ptr<TextWidget> setDecorationThickness(int thickness) {
    if (style.decorationThickness != thickness) {
      style.decorationThickness = thickness;
      markNeedsPaint();
    }
    return self();
  }

  // ── Shadows ───────────────────────────────────────────────────────────────

  /// Replace the shadow list with a single shadow
  std::shared_ptr<TextWidget> setShadow(const TextShadow &shadow) {
    style.shadows = {shadow};
    markNeedsPaint();
    return self();
  }

  /// Replace the shadow list with multiple shadows
  std::shared_ptr<TextWidget>
  setShadows(const std::vector<TextShadow> &shadows) {
    style.shadows = shadows;
    markNeedsPaint();
    return self();
  }

  std::shared_ptr<TextWidget> clearShadows() {
    style.shadows.clear();
    markNeedsPaint();
    return self();
  }

  // ── Per-run background ────────────────────────────────────────────────────

  /// Background color painted behind each line of text (not the widget bg)
  std::shared_ptr<TextWidget> setTextBackground(Color color) {
    style.backgroundColor = color;
    markNeedsPaint();
    return self();
  }

  std::shared_ptr<TextWidget> clearTextBackground() {
    style.backgroundColor.reset();
    markNeedsPaint();
    return self();
  }

  // ── Whole TextStyle at once ───────────────────────────────────────────────

  std::shared_ptr<TextWidget> setTextStyle(const TextStyle &s) {
    style = s;
    // Sync base-class fields
    fontSize = s.scaledFontSize();
    fontWeight = s.fontWeight;
    fontFamily = s.fontFamily;
    textColor = s.color;
    markNeedsLayout();
    return self();
  }

  // ── Widget size setters (unchanged from original) ─────────────────────────

  std::shared_ptr<TextWidget> setBackgroundColor(Color color) {
    backgroundColor = color;
    hasBackground = true;
    markNeedsPaint();
    return self();
  }

  std::shared_ptr<TextWidget> setBorderRadius(int r) {
    borderRadius = r;
    markNeedsPaint();
    return self();
  }

  std::shared_ptr<TextWidget> setPadding(int p) {
    padding = p;
    paddingLeft = paddingRight = paddingTop = paddingBottom = p;
    markNeedsLayout();
    return self();
  }

  std::shared_ptr<TextWidget> setPaddingH(int p) {
    paddingLeft = paddingRight = p;
    markNeedsLayout();
    return self();
  }

  std::shared_ptr<TextWidget> setPaddingV(int p) {
    paddingTop = paddingBottom = p;
    markNeedsLayout();
    return self();
  }

  std::shared_ptr<TextWidget> setPaddingLRTB(int l, int r, int t, int b) {
    paddingLeft = l;
    paddingRight = r;
    paddingTop = t;
    paddingBottom = b;
    markNeedsLayout();
    return self();
  }

  std::shared_ptr<TextWidget> setMinWidth(int w) {
    minWidth = w;
    markNeedsLayout();
    return self();
  }

  std::shared_ptr<TextWidget> setWidth(int w) {
    width = w;
    autoWidth = false;
    markNeedsLayout();
    return self();
  }

  std::shared_ptr<TextWidget> setWidgetHeight(int h) {
    height = h;
    autoHeight = false;
    markNeedsLayout();
    return self();
  }

private:
  std::shared_ptr<TextWidget> self() {
    return std::static_pointer_cast<TextWidget>(shared_from_this());
  }
};

// ============================================================================
// ICON WIDGET  (unchanged logic, updated to inherit correctly)
// ============================================================================

class IconWidget : public TextWidget {
public:
  IconWidget() {
    style.fontFamily = kIconFont;
    fontFamily = kIconFont;
  }

  void computeLayout(GraphicsContext & /*ctx*/,
                     const BoxConstraints &constraints,
                     FontCache & /*fontCache*/) override {
    if (autoWidth)
      width = style.scaledFontSize() + paddingLeft + paddingRight;
    if (autoHeight)
      height = style.scaledFontSize() + paddingTop + paddingBottom;

    width = constraints.clampWidth(width);
    height = constraints.clampHeight(height);
    applyConstraints();
    needsLayout = false;
  }

  void render(GraphicsContext &ctx, FontCache &fontCache) override {
    if (hasBackground)
      drawRoundedRectangle(ctx);

    if (glyphText.empty()) {
      needsPaint = false;
      return;
    }

    NativeFont font = fontCache.getFont(
        style.fontFamily, style.scaledFontSize(), style.fontWeight);
    Painter(ctx).drawText(glyphText, x, y, width, height, font,
                          getCurrentTextColor(),
                          DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    needsPaint = false;
  }

  std::shared_ptr<IconWidget> setSize(int size) {
    setFontSize(size);
    return selfIcon();
  }

  std::shared_ptr<IconWidget> setColor(Color color) {
    setTextColor(color);
    return selfIcon();
  }

  std::shared_ptr<IconWidget> setHoverColor(Color color) {
    setHoverTextColor(color);
    return selfIcon();
  }

  std::shared_ptr<IconWidget> setIconFontFamily(const std::string &family) {
    if (style.fontFamily != family) {
      style.fontFamily = family;
      fontFamily = family;
      markNeedsLayout();
    }
    return selfIcon();
  }

  std::shared_ptr<IconWidget> setGlyph(const FluxIcons::IconGlyph &glyph) {
    glyphText = std::wstring(1, FluxIcons::glyph(glyph));
    markNeedsPaint();
    return selfIcon();
  }

  template <typename T>
  std::shared_ptr<IconWidget>
  setGlyph(State<T> &state,
           std::function<FluxIcons::IconGlyph(const T &)> transform) {
    auto weakSelf = std::weak_ptr<IconWidget>(selfIcon());
    state.subscribe([weakSelf, transform](const T &val) {
      if (auto s = weakSelf.lock()) {
        s->glyphText = std::wstring(1, FluxIcons::glyph(transform(val)));
        s->markNeedsPaint();
      }
    });
    glyphText = std::wstring(1, FluxIcons::glyph(transform(state.get())));
    return selfIcon();
  }

private:
  std::wstring glyphText;

  std::shared_ptr<IconWidget> selfIcon() {
    return std::static_pointer_cast<IconWidget>(shared_from_this());
  }
};

using IconWidgetPtr = std::shared_ptr<IconWidget>;

inline IconWidgetPtr Icon(const FluxIcons::IconGlyph &glyph, int size = 16) {
  auto w = std::make_shared<IconWidget>();
  w->setFontSize(size);
  w->setGlyph(glyph);
  return w;
}

template <typename T>
inline IconWidgetPtr
Icon(State<T> &state, std::function<FluxIcons::IconGlyph(const T &)> transform,
     int size = 16) {
  auto w = std::make_shared<IconWidget>();
  w->setFontSize(size);
  w->setGlyph(state, transform);
  return w;
}

// ============================================================================
// PROGRESS BAR WIDGET  (unchanged)
// ============================================================================

class ProgressBarWidget : public Widget {
public:
  double value = 0.0;
  int trackBorderRadius = 4;
  Color trackColor = Color::fromRGB(220, 220, 220);
  Color trackBorderColor = Color::fromRGB(0, 0, 0);
  bool hasTrackBorder = false;
  int trackBorderWidth = 1;
  std::vector<Color> progressColors = {Color::fromRGB(33, 150, 243)};

  ProgressBarWidget() {
    height = 12;
    autoHeight = false;
  }

  std::shared_ptr<ProgressBarWidget> setValue(State<double> &state) {
    value = std::max(0.0, std::min(1.0, state.get()));
    state.bindProperty(
        shared_from_this(),
        [](Widget *w, const double &val) {
          auto *bar = static_cast<ProgressBarWidget *>(w);
          bar->value = std::max(0.0, std::min(1.0, val));
          bar->markNeedsPaint();
        },
        false);
    return std::static_pointer_cast<ProgressBarWidget>(shared_from_this());
  }

  std::shared_ptr<ProgressBarWidget> setValue(double v) {
    value = std::max(0.0, std::min(1.0, v));
    markNeedsPaint();
    return std::static_pointer_cast<ProgressBarWidget>(shared_from_this());
  }

  std::shared_ptr<ProgressBarWidget> setBackgroundColor(Color color) {
    trackColor = color;
    markNeedsPaint();
    return std::static_pointer_cast<ProgressBarWidget>(shared_from_this());
  }

  std::shared_ptr<ProgressBarWidget> setProgressColors(std::vector<Color> c) {
    if (!c.empty())
      progressColors = c;
    markNeedsPaint();
    return std::static_pointer_cast<ProgressBarWidget>(shared_from_this());
  }

  std::shared_ptr<ProgressBarWidget> setBorderColor(Color color) {
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

    painter.fillRoundedRectGDI(x, y, width, height, rx, trackColor,
                               trackBorderColor,
                               hasTrackBorder ? trackBorderWidth : 0);

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
// DIVIDER WIDGET  (unchanged)
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

/// Create a Text widget with static string content.
inline TextWidgetPtr Text(const std::string &text) {
  auto w = std::make_shared<TextWidget>();
  w->text = text;
  return w;
}

/// Create a Text widget bound to a State<T>.
template <typename T> inline TextWidgetPtr Text(State<T> &state) {
  auto w = std::make_shared<TextWidget>();
  w->setText(state);
  return w;
}

/// Create a Text widget bound to a State<T> with a transform.
template <typename T, typename F>
inline TextWidgetPtr Text(State<T> &state, F transform) {
  auto w = std::make_shared<TextWidget>();
  w->setText(state, std::function<std::string(const T &)>(transform));
  return w;
}

/// Create a Text widget with a full TextStyle applied.
inline TextWidgetPtr StyledText(const std::string &text,
                                const TextStyle &style) {
  auto w = std::make_shared<TextWidget>();
  w->text = text;
  w->setTextStyle(style);
  return w;
}

inline WidgetPtr Divider() {
  auto w = std::make_shared<DividerWidget>();
  w->height = 1;
  w->autoHeight = false;
  w->hasBackground = true;
  w->backgroundColor = Color::fromRGB(0, 0, 0);
  return w;
}

#endif // FLUX_DISPLAY_HPP