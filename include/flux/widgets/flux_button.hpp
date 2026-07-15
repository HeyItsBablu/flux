#ifndef FLUX_ACTION_HPP
#define FLUX_ACTION_HPP

#include "../flux_core.hpp"
#include "flux_display.hpp"
#include "flux_icons.hpp"
#include "flux/flux_dom_adapter.hpp"
#include <chrono>

// ============================================================================
// BUTTON WIDGET
// ============================================================================

class ButtonWidget : public Widget
{
public:
  bool handleMouseDown(int mx, int my) override
  {
    if (!_hit(mx, my))
      return false;
    _pressed = true;
    markNeedsPaint();
    if (auto *ui = FluxUI::getCurrentInstance())
      ui->captureMouseInput();
    return true;
  }

  bool handleMouseUp(int mx, int my) override
  {
    if (!_pressed)
      return false;
    _pressed = false;
    markNeedsPaint();
    if (auto *ui = FluxUI::getCurrentInstance())
      ui->releaseMouseInput();
    if (_hit(mx, my) && onClick)
      onClick();
    return true;
  }

  bool handleMouseMove(int mx, int my) override
  {
    bool nowOver = _hit(mx, my);
    if (nowOver != isHovered)
    {
      isHovered = nowOver;
      if (onHover)
        onHover(isHovered);
      markNeedsPaint();
    }
    return _pressed; // consume during press/drag
  }

  bool handleMouseLeave() override
  {
    if (isHovered)
    {
      isHovered = false;
      if (onHover)
        onHover(false);
      markNeedsPaint();
    }
    return false;
  }

  void render(GraphicsContext &ctx, FontCache &fontCache) override
  {
    if (!visible)
      return;

    Painter painter(ctx, this);

    if (hasBackground)
    {
      Color base = isHovered
                       ? (hasHoverBackground ? hoverBackgroundColor : backgroundColor.darken(15))
                       : backgroundColor;
      Color body = _pressed ? base.darken(20) : base;

      painter.fillRoundedRect(x, y, width, height, borderRadius, body);
    }

    if (hasBorder)
      painter.drawBorder(x, y, width, height, borderRadius,
                         getCurrentBorderColor(), borderWidth);

    if (!children.empty())
      children[0]->render(ctx, fontCache);
    else if (!text.empty())
    {
#if (defined(__EMSCRIPTEN__) && defined(FLUX_WEB_RENDERER_DOM)) || defined(FLUX_SSR)
      // renderText()'s underlying drawText() call targets the SAME
      // default-slot node fillRoundedRect/drawBorder above already
      // configured for this widget — text and background would collide
      // on one shared DOM node otherwise (visible as the doubled/
      // misaligned label the SSR/DOM renderers were producing). Give
      // the label an explicit separate slot, same pattern already used
      // for CheckBoxWidget's box+label split.
      if (IDomAdapter *adapter = getActiveDomAdapter())
      {
        DomNodeHandle label = fluxDomEnsureNode(this, "div", "label");
        fluxDomApplyRect(this, x, y, width, height, "label");
        adapter->setStyle(label, "display", "flex");
        adapter->setStyle(label, "align-items", "center");
        adapter->setStyle(label, "justify-content", "center");
        adapter->setStyle(label, "color", "rgb(" + std::to_string(getCurrentTextColor().r) + "," + std::to_string(getCurrentTextColor().g) + "," + std::to_string(getCurrentTextColor().b) + ")");
        adapter->setStyle(label, "pointer-events", "none");
        adapter->setText(label, text);
      }
      else
#endif
        renderText(ctx, fontCache, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    needsPaint = false;
  }

  void computeLayout(GraphicsContext &ctx, const BoxConstraints &constraints,
                     FontCache &fontCache) override
  {
    if (!children.empty())
    {
      auto &child = children[0];
      child->computeLayout(ctx,
                           constraints.deflate(paddingLeft + paddingRight,
                                               paddingTop + paddingBottom),
                           fontCache);
      if (autoWidth)
        width = child->width + child->marginLeft + child->marginRight + paddingLeft + paddingRight;
      if (autoHeight)
        height = child->height + child->marginTop + child->marginBottom + paddingTop + paddingBottom;
    }
    else if (!text.empty())
    {

      NativeFont font = fontCache.getFont(fontFamily, fontSize, fontWeight);
      Painter p(ctx, this);
      int tw = 0, th = 0;
      p.measureText(toWideString(text), font, tw, th);

      if (autoWidth)
        width = tw + paddingLeft + paddingRight;
      if (autoHeight)
        height = std::max(32, th + paddingTop + paddingBottom);
    }
    else
    {
      if (autoWidth)
        width = paddingLeft + paddingRight;
      if (autoHeight)
        height = paddingTop + paddingBottom;
    }
    applyConstraints();
    needsLayout = false;
  }

  void positionChildren(int contentX, int contentY,
                        int contentWidth, int contentHeight) override
  {
    if (!children.empty())
    {
      auto &child = children[0];
      int cx = contentX + (contentWidth - child->width - child->marginLeft - child->marginRight) / 2;
      int cy = contentY + (contentHeight - child->height - child->marginTop - child->marginBottom) / 2;
      child->x = cx + child->marginLeft;
      child->y = cy + child->marginTop;
      child->positionChildren(
          child->x + child->paddingLeft, child->y + child->paddingTop,
          child->width - child->paddingLeft - child->paddingRight,
          child->height - child->paddingTop - child->paddingBottom);
    }
  }

  // ── Fluent API ────────────────────────────────────────────────────────────

  std::shared_ptr<ButtonWidget> setChild(WidgetPtr c)
  {
    children.clear();
    addChild(c);
    return _self();
  }
  std::shared_ptr<ButtonWidget> setBackgroundColor(Color c)
  {
    backgroundColor = c;
    hasBackground = true;
    markNeedsPaint();
    return _self();
  }
  template <typename T, typename F>
  std::shared_ptr<ButtonWidget> setBackgroundColor(State<T> &state, F transform)
  {
    std::function<Color(const T &)> fn = transform;
    backgroundColor = fn(state.get());
    hasBackground = true;
    state.bindProperty(shared_from_this(), [fn](Widget *w, const T &val)
                       { w->backgroundColor = fn(val); }, false);
    return _self();
  }
  std::shared_ptr<ButtonWidget> setHoverBackgroundColor(Color c)
  {
    hoverBackgroundColor = c;
    hasHoverBackground = true;
    markNeedsPaint();
    return _self();
  }
  std::shared_ptr<ButtonWidget> setBorderRadius(int r)
  {
    borderRadius = r;
    markNeedsPaint();
    return _self();
  }
  std::shared_ptr<ButtonWidget> setPadding(int p)
  {
    paddingLeft = paddingRight = paddingTop = paddingBottom = p;
    markNeedsLayout();
    return _self();
  }
  std::shared_ptr<ButtonWidget> setPaddingAll(int l, int t, int r, int b)
  {
    paddingLeft = l;
    paddingTop = t;
    paddingRight = r;
    paddingBottom = b;
    markNeedsLayout();
    return _self();
  }
  std::shared_ptr<ButtonWidget> setWidth(int w)
  {
    width = w;
    autoWidth = false;
    markNeedsLayout();
    return _self();
  }
  std::shared_ptr<ButtonWidget> setHeight(int h)
  {
    height = h;
    autoHeight = false;
    markNeedsLayout();
    return _self();
  }
  std::shared_ptr<ButtonWidget> setTextColor(Color c)
  {
    textColor = c;
    markNeedsPaint();
    return _self();
  }
  std::shared_ptr<ButtonWidget> setOnClick(ClickHandler h)
  {
    onClick = h;
    return _self();
  }

private:
  bool _pressed = false;

  bool _hit(int mx, int my) const
  {
    return mx >= x && mx < x + width && my >= y && my < y + height;
  }
  std::shared_ptr<ButtonWidget> _self()
  {
    return std::static_pointer_cast<ButtonWidget>(shared_from_this());
  }
};

// ============================================================================
// IconButtonWidget
//
// A circular tap target wrapping a single icon glyph.
// Follows Material Design specs:
//   • Minimum touch target: 48×48 logical pixels
//   • Visual icon size:     24px default (independently settable)
//   • Hover: filled circle at ~8% opacity
//   • Press: filled circle at ~16% opacity
//   • Disabled: icon at 38% opacity, no interaction
//   • Optional filled/tonal/outlined variants (iconButtonVariant)
// ============================================================================

enum class IconButtonVariant
{
  Standard, // transparent background, tinted hover circle  (default)
  Filled,   // always-filled circle, white icon
  Tonal,    // secondary container fill
  Outlined, // border ring, tinted hover
};

class IconButtonWidget : public Widget
{
public:
  // ── Public config ─────────────────────────────────────────────────────────

  FluxIcons::IconGlyph glyph = FluxIcons::Add; // default glyph
  int iconSize = 24;
  int touchTargetSize = 48; // outer widget size
  bool enabled = true;

  IconButtonVariant variant = IconButtonVariant::Standard;

  // Icon tint (used when enabled)
  Color iconColor = Color::fromRGB(60, 60, 60);
  // Colour of the hover/press circle overlay
  Color splashColor = Color::fromRGB(120, 120, 120);
  // Fill color for Filled variant body / Outlined border
  Color fillColor = Color::fromRGB(33, 150, 243);
  // Icon color for Filled variant (usually white)
  Color filledIconColor = Color::fromRGB(255, 255, 255);
  // Disabled icon tint
  Color disabledColor = Color::fromRGB(160, 160, 160);

  std::string tooltip;

  std::function<void()> onPressed;

  // ── Lifecycle ─────────────────────────────────────────────────────────────

  IconButtonWidget()
  {
    isFocusable = false;
    autoWidth = false;
    autoHeight = false;
    width = touchTargetSize;
    height = touchTargetSize;
  }

  // ── Layout ───────────────────────────────────────────────────────────────

  void computeLayout(GraphicsContext &,
                     const BoxConstraints &constraints,
                     FontCache &) override
  {
    // Always honour the touch target size, clamped by constraints
    width = constraints.clampWidth(touchTargetSize);
    height = constraints.clampHeight(touchTargetSize);
    needsLayout = false;
  }

  void positionChildren(int, int, int, int) override {}

  // ── Render ───────────────────────────────────────────────────────────────

  void render(GraphicsContext &ctx, FontCache &fontCache) override
  {
    if (!visible)
      return;
    Painter painter(ctx, this);

    const int cx = x + width / 2;
    const int cy = y + height / 2;
    // Splash circle radius: just large enough to contain the icon with padding
    const int splashR = iconSize / 2 + 8;
    const int splashD = splashR * 2;
    const int splashX = cx - splashR;
    const int splashY = cy - splashR;

    // ── Background (variant-specific) ────────────────────────────────────

    switch (variant)
    {

    case IconButtonVariant::Filled:
    {
      // Always-filled circle
      Color body = !enabled    ? disabledColor.withAlpha(30)
                   : _pressed  ? fillColor.darken(20)
                   : isHovered ? fillColor.darken(10)
                               : fillColor;
      painter.drawEllipse(splashX, splashY, splashD, splashD,
                          body, Color::fromRGBA(0, 0, 0, 0), 0);
      break;
    }

    case IconButtonVariant::Tonal:
    {
      // Secondary-container-like fill (lighter tint of fillColor)
      Color tonal = fillColor.withAlpha(40);
      Color body = !enabled    ? tonal.withAlpha(15)
                   : _pressed  ? fillColor.withAlpha(60)
                   : isHovered ? fillColor.withAlpha(50)
                               : tonal;
      painter.drawEllipse(splashX, splashY, splashD, splashD,
                          body, Color::fromRGBA(0, 0, 0, 0), 0);
      break;
    }

    case IconButtonVariant::Outlined:
    {
      // Transparent by default, border ring, hover tints interior
      Color body = !enabled    ? Color::fromRGBA(0, 0, 0, 0)
                   : _pressed  ? splashColor.withAlpha(40)
                   : isHovered ? splashColor.withAlpha(20)
                               : Color::fromRGBA(0, 0, 0, 0);
      Color border = !enabled ? disabledColor.withAlpha(60)
                              : iconColor.withAlpha(120);
      painter.drawEllipse(splashX, splashY, splashD, splashD,
                          body, border, 1);
      break;
    }

    case IconButtonVariant::Standard:
    default:
    {
      if (enabled && isHovered)
      {
        Color splash = splashColor.withAlpha(18);
        painter.drawEllipse(splashX, splashY, splashD, splashD,
                            splash, Color::fromRGBA(0, 0, 0, 0), 0);
      }
      break;
    }
    }

    // Focus ring (keyboard navigation)
    if (isFocused && enabled)
    {
      painter.drawEllipse(splashX - 2, splashY - 2,
                          splashD + 4, splashD + 4,
                          Color::fromRGBA(0, 0, 0, 0),
                          fillColor.withAlpha(180), 2);
    }

    // ── Icon glyph ───────────────────────────────────────────────────────

    Color tint = !enabled                               ? disabledColor
                 : variant == IconButtonVariant::Filled ? filledIconColor
                                                        : iconColor;

    NativeFont font = fontCache.getFont(kIconFont, iconSize, FontWeight::Normal);

    // Draw the glyph centred in the touch target
#ifdef _WIN32
    std::wstring glyphStr(1, glyph.win);
#else
    // Build UTF-8 / wide string from the uint32_t codepoint
    std::wstring glyphStr = _codepointToWstring(FluxIcons::glyph(glyph));
#endif
    painter.drawText(glyphStr, x, y, width, height, font, tint,
                     DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    needsPaint = false;
  }

  // ── Mouse / keyboard events ───────────────────────────────────────────────

  bool handleMouseDown(int mx, int my) override
  {
    if (!enabled || !_hit(mx, my))
      return false;
    _pressed = true;
    markNeedsPaint();
    if (auto *ui = FluxUI::getCurrentInstance())
      ui->captureMouseInput();
    return true;
  }

  bool handleMouseUp(int mx, int my) override
  {
    if (!_pressed)
      return false;
    _pressed = false;
    markNeedsPaint();
    if (auto *ui = FluxUI::getCurrentInstance())
      ui->releaseMouseInput();
    if (enabled && _hit(mx, my) && onPressed)
      onPressed();
    return true;
  }

  bool handleMouseMove(int mx, int my) override
  {
    if (!enabled)
      return false;
    bool nowOver = _hit(mx, my);
    if (nowOver != isHovered)
    {
      isHovered = nowOver;
      if (onHover)
        onHover(isHovered);
      markNeedsPaint();
    }
    return _pressed;
  }

  bool handleMouseLeave() override
  {
    if (isHovered)
    {
      isHovered = false;
      if (onHover)
        onHover(false);
      markNeedsPaint();
    }
    if (_pressed)
    {
      _pressed = false;
      if (auto *ui = FluxUI::getCurrentInstance())
        ui->releaseMouseInput();
      markNeedsPaint();
    }
    return false;
  }

  bool handleFocus(bool focused) override
  {
    isFocused = focused;
    markNeedsPaint();
    return true;
  }

  bool handleKeyDown(int keyCode) override
  {
    if (!enabled)
      return false;
    if (keyCode == Key::Return || keyCode == Key::Space)
    {
      if (onPressed)
        onPressed();
      return true;
    }
    return false;
  }

  // ── Fluent API ────────────────────────────────────────────────────────────

  std::shared_ptr<IconButtonWidget> setGlyph(const FluxIcons::IconGlyph &g)
  {
    glyph = g;
    markNeedsPaint();
    return _self();
  }

  // Reactive glyph binding
  template <typename T>
  std::shared_ptr<IconButtonWidget>
  setGlyph(State<T> &state,
           std::function<FluxIcons::IconGlyph(const T &)> transform)
  {
    glyph = transform(state.get());
    state.bindProperty(shared_from_this(), [transform](Widget *w, const T &val)
                       {
                auto* self = static_cast<IconButtonWidget*>(w);
                self->glyph = transform(val);
                self->markNeedsPaint(); }, false);
    return _self();
  }

  std::shared_ptr<IconButtonWidget> setIconSize(int size)
  {
    iconSize = size;
    markNeedsPaint();
    return _self();
  }

  // Set the outer touch target size (never below 40px per Material spec)
  std::shared_ptr<IconButtonWidget> setSize(int size)
  {
    touchTargetSize = std::max(40, size);
    width = height = touchTargetSize;
    markNeedsLayout();
    return _self();
  }

  std::shared_ptr<IconButtonWidget> setEnabled(bool e)
  {
    enabled = e;
    markNeedsPaint();
    return _self();
  }

  // Reactive enabled binding
  template <typename T>
  std::shared_ptr<IconButtonWidget>
  setEnabled(State<T> &state, std::function<bool(const T &)> transform)
  {
    enabled = transform(state.get());
    state.bindProperty(shared_from_this(), [transform](Widget *w, const T &val)
                       {
                auto* self = static_cast<IconButtonWidget*>(w);
                self->enabled = transform(val);
                self->markNeedsPaint(); }, false);
    return _self();
  }

  std::shared_ptr<IconButtonWidget> setOnPressed(std::function<void()> cb)
  {
    onPressed = std::move(cb);
    return _self();
  }

  std::shared_ptr<IconButtonWidget> setIconColor(Color c)
  {
    iconColor = c;
    markNeedsPaint();
    return _self();
  }

  std::shared_ptr<IconButtonWidget> setSplashColor(Color c)
  {
    splashColor = c;
    markNeedsPaint();
    return _self();
  }

  std::shared_ptr<IconButtonWidget> setFillColor(Color c)
  {
    fillColor = c;
    markNeedsPaint();
    return _self();
  }

  std::shared_ptr<IconButtonWidget> setFilledIconColor(Color c)
  {
    filledIconColor = c;
    markNeedsPaint();
    return _self();
  }

  std::shared_ptr<IconButtonWidget> setDisabledColor(Color c)
  {
    disabledColor = c;
    markNeedsPaint();
    return _self();
  }

  std::shared_ptr<IconButtonWidget> setVariant(IconButtonVariant v)
  {
    variant = v;
    markNeedsPaint();
    return _self();
  }

  std::shared_ptr<IconButtonWidget> setTooltip(const std::string &t)
  {
    tooltip = t;
    return _self();
  }

  // onClick alias — mirrors Widget::onClick convention used elsewhere
  std::shared_ptr<IconButtonWidget> setOnClick(std::function<void()> cb)
  {
    onPressed = std::move(cb);
    onClick = onPressed; // keep base-class onClick in sync
    return _self();
  }

private:
  bool _pressed = false;

  bool _hit(int mx, int my) const
  {
    return mx >= x && mx < x + width && my >= y && my < y + height;
  }

  std::shared_ptr<IconButtonWidget> _self()
  {
    return std::static_pointer_cast<IconButtonWidget>(shared_from_this());
  }

#ifndef _WIN32
  // Convert a Unicode codepoint (may be > 0xFFFF) to wstring.
  // On Linux/Android wchar_t is 32-bit so no surrogates needed.
  static std::wstring _codepointToWstring(uint32_t cp)
  {
    if (cp <= 0xFFFF)
      return std::wstring(1, static_cast<wchar_t>(cp));
    // Supplementary plane — encode as surrogate pair for 16-bit wchar_t
    // (Android NDK where wchar_t may be 16-bit)
    if constexpr (sizeof(wchar_t) == 2)
    {
      cp -= 0x10000;
      wchar_t high = static_cast<wchar_t>(0xD800 + (cp >> 10));
      wchar_t low = static_cast<wchar_t>(0xDC00 + (cp & 0x3FF));
      return std::wstring{high, low};
    }
    else
    {
      return std::wstring(1, static_cast<wchar_t>(cp));
    }
  }
#endif
};
// ============================================================================
// FACTORIES
// ============================================================================

using IconButtonPtr = std::shared_ptr<IconButtonWidget>;
using ButtonWidgetPtr = std::shared_ptr<ButtonWidget>;

// Standard (default) icon button
inline IconButtonPtr IconButton(
    const FluxIcons::IconGlyph &glyph,
    std::function<void()> onPressed = nullptr,
    int iconSize = 24)
{
  auto w = std::make_shared<IconButtonWidget>();
  w->glyph = glyph;
  w->iconSize = iconSize;
  w->onPressed = std::move(onPressed);
  return w;
}

// Filled variant — solid circle background
inline IconButtonPtr FilledIconButton(
    const FluxIcons::IconGlyph &glyph,
    std::function<void()> onPressed = nullptr,
    Color fillColor = Color::fromRGB(33, 150, 243))
{
  auto w = std::make_shared<IconButtonWidget>();
  w->glyph = glyph;
  w->variant = IconButtonVariant::Filled;
  w->fillColor = fillColor;
  w->onPressed = std::move(onPressed);
  return w;
}

// Tonal variant — light fill, darker icon
inline IconButtonPtr TonalIconButton(
    const FluxIcons::IconGlyph &glyph,
    std::function<void()> onPressed = nullptr,
    Color fillColor = Color::fromRGB(33, 150, 243))
{
  auto w = std::make_shared<IconButtonWidget>();
  w->glyph = glyph;
  w->variant = IconButtonVariant::Tonal;
  w->fillColor = fillColor;
  w->onPressed = std::move(onPressed);
  return w;
}

// Outlined variant — border ring, no fill at rest
inline IconButtonPtr OutlinedIconButton(
    const FluxIcons::IconGlyph &glyph,
    std::function<void()> onPressed = nullptr)
{
  auto w = std::make_shared<IconButtonWidget>();
  w->glyph = glyph;
  w->variant = IconButtonVariant::Outlined;
  w->onPressed = std::move(onPressed);
  return w;
}

inline ButtonWidgetPtr Button(const std::string &text,
                              ClickHandler onClick = nullptr)
{
  auto w = std::make_shared<ButtonWidget>();
  w->text = text;
  w->onClick = onClick;
  w->hasBackground = true;
  w->backgroundColor = Color::fromRGB(55, 55, 65);
  w->textColor = Color::fromRGB(220, 220, 220);
  w->paddingLeft = w->paddingRight = 12;
  w->paddingTop = w->paddingBottom = 0;
  w->borderRadius = 6;
  w->fontWeight = FontWeight::Normal;
  return w;
}

inline ButtonWidgetPtr Button(WidgetPtr child,
                              ClickHandler onClick = nullptr)
{
  auto w = std::make_shared<ButtonWidget>();
  w->addChild(child);
  w->onClick = onClick;
  w->hasBackground = true;
  w->backgroundColor = Color::fromRGB(76, 175, 80);
  w->paddingLeft = w->paddingRight = 20;
  w->paddingTop = w->paddingBottom = 10;
  w->borderRadius = 4;
  return w;
}

#endif // FLUX_ACTION_HPP