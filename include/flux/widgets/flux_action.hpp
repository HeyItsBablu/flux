#ifndef FLUX_ACTION_HPP
#define FLUX_ACTION_HPP

#include "../flux_core.hpp"
#include "flux_display.hpp"
#include "flux_icons.hpp"
#include <chrono>

// ============================================================================
// HANDLER TYPES
// ============================================================================

using GestureHandler = std::function<void()>;
using PointerHandler = std::function<void(int x, int y)>;
using DragHandler = std::function<void(int dx, int dy)>;
using ScrollHandler = std::function<void(int delta)>;

// ============================================================================
// GESTURE DETECTOR
// ============================================================================

class GestureDetectorWidget : public Widget
{
public:
  static constexpr uint32_t LONG_PRESS_MS = 500;
  static constexpr uint32_t DOUBLE_TAP_MS = 300;
  static constexpr int DRAG_THRESHOLD = 5;

  GestureHandler onTap;
  GestureHandler onDoubleTap;
  GestureHandler onLongPress;
  GestureHandler onSecondaryTap;
  GestureHandler onHoverEnter;
  GestureHandler onHoverExit;
  PointerHandler onPointerMove;
  GestureHandler onDragStart;
  DragHandler onDragUpdate;
  GestureHandler onDragEnd;
  ScrollHandler onScrollUp;
  ScrollHandler onScrollDown;

  // ── Layout ───────────────────────────────────────────────────────────────

  void computeLayout(GraphicsContext &ctx, const BoxConstraints &constraints,
                     FontCache &fontCache) override
  {
    if (!children.empty())
    {
      children[0]->computeLayout(ctx, constraints, fontCache);
      if (autoWidth)
        width = children[0]->width;
      if (autoHeight)
        height = children[0]->height;
    }
    else
    {
      if (autoWidth)
        width = constraints.maxWidth;
      if (autoHeight)
        height = constraints.maxHeight;
    }
    applyConstraints();
    needsLayout = false;
  }

  void positionChildren(int cx, int cy, int, int) override
  {
    if (!children.empty())
    {
      auto &child = children[0];
      child->x = cx;
      child->y = cy;
      child->positionChildren(
          child->x + child->paddingLeft, child->y + child->paddingTop,
          child->width - child->paddingLeft - child->paddingRight,
          child->height - child->paddingTop - child->paddingBottom);
    }
  }

  void render(GraphicsContext &ctx, FontCache &fontCache) override
  {
    if (!children.empty())
      children[0]->render(ctx, fontCache);

    // Poll long-press every paint (cheap, ~16 ms resolution via timer)
    if (_longPressArmed && !_longPressFired)
    {
      if (_getTimeMs() - _pressTime >= LONG_PRESS_MS)
      {
        _longPressFired = true;
        _longPressArmed = false;
        if (onLongPress)
          onLongPress();
      }
    }
    needsPaint = false;
  }

  // ── Mouse events ─────────────────────────────────────────────────────────

  bool handleMouseDown(int mx, int my) override
  {
    if (!_hit(mx, my))
      return false;

    _pressX = mx;
    _pressY = my;
    _pressed = true;
    _dragging = _dragStarted = false;
    _longPressArmed = true;
    _longPressFired = false;
    _pressTime = _getTimeMs();

    // Request continuous repaints so long-press polling works
    markNeedsPaint();

    if (auto *ui = FluxUI::getCurrentInstance())
      ui->captureMouseInput();

    return true;
  }

  bool handleMouseUp(int mx, int my) override
  {
    if (!_pressed)
      return false;

    _longPressArmed = false;

    if (auto *ui = FluxUI::getCurrentInstance())
      ui->releaseMouseInput();

    if (_dragging)
    {
      _dragging = _pressed = false;
      if (onDragEnd)
        onDragEnd();
      return true;
    }

    bool wasLongPress = _longPressFired;
    _pressed = _longPressFired = false;

    // Long-press already fired — don't also fire tap
    if (wasLongPress)
      return true;

    if (!_hit(mx, my))
      return true;

    uint32_t now = _getTimeMs();
    if (_pendingTap && (now - _lastTapTime) <= DOUBLE_TAP_MS)
    {
      // Second tap within window → double tap
      // Cancel the pending single-tap timer equivalent
      _pendingTap = false;
      _lastTapTime = 0;
      if (onDoubleTap)
        onDoubleTap();
    }
    else
    {
      // Record this tap; if no second tap arrives within DOUBLE_TAP_MS
      // the single-tap fires via the timer callback set below.
      _pendingTap = true;
      _lastTapTime = now;

      if (onDoubleTap)
      {
        // We have a double-tap handler — defer single tap
        _scheduleSingleTap();
      }
      else
      {
        // No double-tap handler — fire immediately (Flutter behaviour)
        _pendingTap = false;
        if (onTap)
          onTap();
      }
    }

    return true;
  }

  bool handleRightClick(int mx, int my) override
  {
    if (!_hit(mx, my))
      return false;
    if (onSecondaryTap)
      onSecondaryTap();
    return true;
  }

  bool handleMouseMove(int mx, int my) override
  {
    // Hover tracking
    bool nowOver = _hit(mx, my);
    if (nowOver != _hovered)
    {
      _hovered = nowOver;
      if (_hovered)
      {
        if (onHoverEnter)
          onHoverEnter();
      }
      else
      {
        if (onHoverExit)
          onHoverExit();
      }
    }

    if (_pressed)
    {
      if (!_dragStarted)
      {
        if (std::abs(mx - _pressX) > DRAG_THRESHOLD ||
            std::abs(my - _pressY) > DRAG_THRESHOLD)
        {
          _dragStarted = _dragging = true;
          _longPressArmed = false;
          _lastDragX = mx;
          _lastDragY = my;
          if (onDragStart)
            onDragStart();
        }
      }
      else if (_dragging)
      {
        if (onDragUpdate)
          onDragUpdate(mx - _lastDragX, my - _lastDragY);
        _lastDragX = mx;
        _lastDragY = my;
      }
    }

    if (_hit(mx, my) && onPointerMove)
      onPointerMove(mx, my);

    // During active drag consume the event; otherwise let it propagate
    // (so hover on sibling widgets still works)
    return _dragging;
  }

  bool handleMouseLeave() override
  {
    if (_hovered)
    {
      _hovered = false;
      if (onHoverExit)
        onHoverExit();
    }
    return false;
  }

  bool handleMouseWheel(int delta) override
  {
    if (!_hit(0, 0))
    {
      // Wheel events don't carry position on all platforms; fall through
      // if neither scroll handler is set so parent can handle it.
      if (!onScrollUp && !onScrollDown)
        return false;
    }
    if (delta > 0)
    {
      if (onScrollUp)
        onScrollUp(delta);
      return true;
    }
    else
    {
      if (onScrollDown)
        onScrollDown(-delta);
      return true;
    }
    return false;
  }

  // Timer fires every 16 ms while we're in the tree — used to resolve the
  // deferred single-tap after DOUBLE_TAP_MS with no second tap.
  bool handleTimer(UINT /*id*/) override
  {
    if (_pendingTap && (_getTimeMs() - _lastTapTime) > DOUBLE_TAP_MS)
    {
      _pendingTap = false;
      if (onTap)
        onTap();
    }
    // Also repaint if long-press still armed (keeps polling going)
    if (_longPressArmed && !_longPressFired)
      markNeedsPaint();
    return false; // don't consume — let other widgets see it
  }

  // ── Fluent API ────────────────────────────────────────────────────────────

  std::shared_ptr<GestureDetectorWidget> setOnTap(GestureHandler h)
  {
    onTap = std::move(h);
    return _self();
  }
  std::shared_ptr<GestureDetectorWidget> setOnDoubleTap(GestureHandler h)
  {
    onDoubleTap = std::move(h);
    return _self();
  }
  std::shared_ptr<GestureDetectorWidget> setOnLongPress(GestureHandler h)
  {
    onLongPress = std::move(h);
    return _self();
  }
  std::shared_ptr<GestureDetectorWidget> setOnSecondaryTap(GestureHandler h)
  {
    onSecondaryTap = std::move(h);
    return _self();
  }
  std::shared_ptr<GestureDetectorWidget> setOnHoverEnter(GestureHandler h)
  {
    onHoverEnter = std::move(h);
    return _self();
  }
  std::shared_ptr<GestureDetectorWidget> setOnHoverExit(GestureHandler h)
  {
    onHoverExit = std::move(h);
    return _self();
  }
  std::shared_ptr<GestureDetectorWidget> setOnPointerMove(PointerHandler h)
  {
    onPointerMove = std::move(h);
    return _self();
  }
  std::shared_ptr<GestureDetectorWidget> setOnDragStart(GestureHandler h)
  {
    onDragStart = std::move(h);
    return _self();
  }
  std::shared_ptr<GestureDetectorWidget> setOnDragUpdate(DragHandler h)
  {
    onDragUpdate = std::move(h);
    return _self();
  }
  std::shared_ptr<GestureDetectorWidget> setOnDragEnd(GestureHandler h)
  {
    onDragEnd = std::move(h);
    return _self();
  }
  std::shared_ptr<GestureDetectorWidget> setOnScrollUp(ScrollHandler h)
  {
    onScrollUp = std::move(h);
    return _self();
  }
  std::shared_ptr<GestureDetectorWidget> setOnScrollDown(ScrollHandler h)
  {
    onScrollDown = std::move(h);
    return _self();
  }

  std::shared_ptr<GestureDetectorWidget> setWidth(int w)
  {
    width = w;
    autoWidth = false;
    markNeedsLayout();
    return _self();
  }
  std::shared_ptr<GestureDetectorWidget> setHeight(int h)
  {
    height = h;
    autoHeight = false;
    markNeedsLayout();
    return _self();
  }

private:
  // Interaction state
  bool _pressed = false;
  bool _hovered = false;
  bool _dragging = false;
  bool _dragStarted = false;
  bool _longPressArmed = false;
  bool _longPressFired = false;
  bool _pendingTap = false;
  int _pressX = 0, _pressY = 0;
  int _lastDragX = 0, _lastDragY = 0;
  uint32_t _pressTime = 0;
  uint32_t _lastTapTime = 0;
  TimerID _tapTimerId = 0;

  static uint32_t _getTimeMs()
  {
#ifdef _WIN32
    return static_cast<uint32_t>(GetTickCount());
#else
    using namespace std::chrono;
    return static_cast<uint32_t>(
        duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
#endif
  }

  bool _hit(int mx, int my) const
  {
    return mx >= x && mx < x + width && my >= y && my < y + height;
  }

  // Schedule a timer so we can fire the deferred single-tap after
  // DOUBLE_TAP_MS if no second tap arrives.
  void _scheduleSingleTap()
  {
    if (_tapTimerId != 0)
      return;
    if (auto *ui = FluxUI::getCurrentInstance())
    {
      std::weak_ptr<Widget> weak = shared_from_this();
      _tapTimerId = ui->setInterval(DOUBLE_TAP_MS + 10, [weak, this]()
                                    {
                if (auto sp = weak.lock()) {
                    if (_pendingTap) {
                        _pendingTap = false;
                        if (onTap) onTap();
                    }
                    // Self-cancel — one-shot
                    if (auto* ui2 = FluxUI::getCurrentInstance())
                        ui2->clearInterval(_tapTimerId);
                    _tapTimerId = 0;
                } });
    }
  }

  std::shared_ptr<GestureDetectorWidget> _self()
  {
    return std::static_pointer_cast<GestureDetectorWidget>(shared_from_this());
  }
};

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

    Painter painter(ctx);

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
      renderText(ctx, fontCache, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

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
      measureText(ctx, fontCache);
      if (autoWidth)
        width += paddingLeft + paddingRight;
      if (autoHeight)
        height += paddingTop + paddingBottom;
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
// FLOATING ACTION BUTTON
// ============================================================================

enum class FABPosition
{
  BottomRight,
  BottomLeft,
  BottomCenter,
  TopRight,
  TopLeft,
  TopCenter
};
enum class FABSize
{
  Small,
  Regular,
  Large
};

class FABWidget : public Widget
{
public:
  FABSize fabSize = FABSize::Regular;
  FABPosition position = FABPosition::BottomRight;
  int marginX = 24;
  int marginY = 24;

  std::string label; // empty = icon-only
  std::string icon = "+";

  Color fabColor = Color::fromRGB(33, 150, 243);
  Color fabHoverColor = Color::fromRGB(25, 118, 210);
  Color fabPressColor = Color::fromRGB(13, 71, 161);
  Color iconColor = Color::fromRGB(255, 255, 255);
  Color labelColor = Color::fromRGB(255, 255, 255);
  Color shadowColor = Color::fromRGBA(0, 0, 0, 60);

  int iconFontSize = 22;
  int labelFontSize = 14;

  std::function<void()> onPressed;

  FABWidget()
  {
    isFocusable = true;
    autoWidth = true;
    autoHeight = true;
  }

  // ── Layout ───────────────────────────────────────────────────────────────

  void computeLayout(GraphicsContext &ctx, const BoxConstraints &,
                     FontCache &fontCache) override
  {
    _computeSize(ctx, fontCache);
    needsLayout = false;
  }

  void positionChildren(int, int, int, int) override {}

  void positionInScaffold(int sx, int sy, int sw, int sh)
  {
    switch (position)
    {
    case FABPosition::BottomRight:
      x = sx + sw - width - marginX;
      y = sy + sh - height - marginY;
      break;
    case FABPosition::BottomLeft:
      x = sx + marginX;
      y = sy + sh - height - marginY;
      break;
    case FABPosition::BottomCenter:
      x = sx + (sw - width) / 2;
      y = sy + sh - height - marginY;
      break;
    case FABPosition::TopRight:
      x = sx + sw - width - marginX;
      y = sy + marginY;
      break;
    case FABPosition::TopLeft:
      x = sx + marginX;
      y = sy + marginY;
      break;
    case FABPosition::TopCenter:
      x = sx + (sw - width) / 2;
      y = sy + marginY;
      break;
    }
  }

  // ── Render ───────────────────────────────────────────────────────────────

  void render(GraphicsContext &ctx, FontCache &fontCache) override
  {
    if (!visible)
      return;
    Painter painter(ctx);

    // Shadow — drawn only around the outside (offset down-right, clipped
    // to avoid bleeding under the button body)
    static const int kShadowLayers = 3;
    for (int i = kShadowLayers; i >= 1; --i)
    {
      uint8_t a = static_cast<uint8_t>(
          shadowColor.a * (kShadowLayers - i + 1) / kShadowLayers);
      Color sc = Color::fromRGBA(shadowColor.r, shadowColor.g, shadowColor.b, a);
      // Offset shadow rect outward without overlapping the body
      painter.fillRoundedRect(
          x + i, y + i + kShadowLayers,
          width, height,
          _radius(), sc);
    }

    // Body
    Color body = _pressed    ? fabPressColor
                 : isHovered ? fabHoverColor
                             : fabColor;
    painter.fillRoundedRect(x, y, width, height, _radius(), body);

    // Icon
    NativeFont iconFont = fontCache.getFont(
        fontFamily.empty() ? "Segoe UI" : fontFamily,
        iconFontSize, FontWeight::Bold);
    std::wstring wicon = toWideString(icon);

    if (label.empty())
    {
      painter.drawText(wicon, x, y, width, height,
                       iconFont, iconColor,
                       DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    else
    {
      int iconArea = height;
      painter.drawText(wicon,
                       x + _paddingH(), y,
                       iconArea - _paddingH(), height,
                       iconFont, iconColor,
                       DT_CENTER | DT_VCENTER | DT_SINGLELINE);

      NativeFont lblFont = fontCache.getFont(
          fontFamily.empty() ? "Segoe UI" : fontFamily,
          labelFontSize, FontWeight::Bold);
      painter.drawText(toWideString(label),
                       x + iconArea, y,
                       width - iconArea - _paddingH(), height,
                       lblFont, labelColor,
                       DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }

    needsPaint = false;
  }

  // ── Mouse events ─────────────────────────────────────────────────────────

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
    if (_hit(mx, my) && onPressed)
      onPressed();
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
    _pressed = false;
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
    if (keyCode == Key::Return || keyCode == Key::Space)
    {
      if (onPressed)
        onPressed();
      return true;
    }
    return false;
  }

  // ── Fluent API ────────────────────────────────────────────────────────────

  std::shared_ptr<FABWidget> setOnPressed(std::function<void()> cb)
  {
    onPressed = cb;
    return _self();
  }
  std::shared_ptr<FABWidget> setIcon(const std::string &i)
  {
    icon = i;
    markNeedsLayout();
    return _self();
  }
  std::shared_ptr<FABWidget> setLabel(const std::string &l)
  {
    label = l;
    markNeedsLayout();
    return _self();
  }
  std::shared_ptr<FABWidget> setPosition(FABPosition pos)
  {
    position = pos;
    return _self();
  }
  std::shared_ptr<FABWidget> setMargin(int mx, int my)
  {
    marginX = mx;
    marginY = my;
    return _self();
  }
  std::shared_ptr<FABWidget> setColor(Color c)
  {
    fabColor = c;
    markNeedsPaint();
    return _self();
  }
  std::shared_ptr<FABWidget> setHoverColor(Color c)
  {
    fabHoverColor = c;
    markNeedsPaint();
    return _self();
  }
  std::shared_ptr<FABWidget> setPressColor(Color c)
  {
    fabPressColor = c;
    markNeedsPaint();
    return _self();
  }
  std::shared_ptr<FABWidget> setIconColor(Color c)
  {
    iconColor = c;
    markNeedsPaint();
    return _self();
  }
  std::shared_ptr<FABWidget> setShadowColor(Color c)
  {
    shadowColor = c;
    markNeedsPaint();
    return _self();
  }
  std::shared_ptr<FABWidget> setSize(FABSize s)
  {
    fabSize = s;
    markNeedsLayout();
    return _self();
  }

private:
  bool _pressed = false;

  int _diameter() const
  {
    switch (fabSize)
    {
    case FABSize::Small:
      return 40;
    case FABSize::Large:
      return 96;
    default:
      return 56;
    }
  }
  int _radius() const { return _diameter() / 2; }
  int _paddingH() const { return _diameter() / 4; }

  void _computeSize(GraphicsContext &ctx, FontCache &fontCache)
  {
    int d = _diameter();
    if (label.empty())
    {
      width = d;
      height = d;
    }
    else
    {
      Painter p(ctx);
      NativeFont f = fontCache.getFont(
          fontFamily.empty() ? "Segoe UI" : fontFamily,
          labelFontSize, FontWeight::Bold);
      int tw = 0, th = 0;
      p.measureText(toWideString(label), f, tw, th);
      width = d + tw + _paddingH() * 2;
      height = d;
    }
  }

  bool _hit(int mx, int my) const
  {
    return mx >= x && mx < x + width && my >= y && my < y + height;
  }

  std::shared_ptr<FABWidget> _self()
  {
    return std::static_pointer_cast<FABWidget>(shared_from_this());
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
    Painter painter(ctx);

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
using FABWidgetPtr = std::shared_ptr<FABWidget>;
using GestureDetectorPtr = std::shared_ptr<GestureDetectorWidget>;
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

inline FABWidgetPtr FAB(const std::string &icon = "+",
                        std::function<void()> onPressed = nullptr)
{
  auto w = std::make_shared<FABWidget>();
  w->icon = icon;
  w->onPressed = std::move(onPressed);
  return w;
}

inline FABWidgetPtr ExtendedFAB(const std::string &icon,
                                const std::string &label,
                                std::function<void()> onPressed = nullptr)
{
  auto w = std::make_shared<FABWidget>();
  w->icon = icon;
  w->label = label;
  w->onPressed = std::move(onPressed);
  return w;
}

inline GestureDetectorPtr GestureDetector(WidgetPtr child = nullptr)
{
  auto w = std::make_shared<GestureDetectorWidget>();
  if (child)
    w->addChild(child);
  return w;
}

inline WidgetPtr GestureDetector(WidgetPtr child, DragHandler onDrag)
{
  auto w = std::make_shared<GestureDetectorWidget>();
  w->addChild(child);
  w->onDragUpdate = std::move(onDrag);
  return w;
}

inline ButtonWidgetPtr Button(const std::string &text,
                              ClickHandler onClick = nullptr)
{
  auto w = std::make_shared<ButtonWidget>();
  w->text = text;
  w->onClick = onClick;
  w->hasBackground = true;
  w->backgroundColor = Color::fromRGB(76, 175, 80);
  w->textColor = Color::fromRGB(255, 255, 255);
  w->paddingLeft = w->paddingRight = 20;
  w->paddingTop = w->paddingBottom = 10;
  w->borderRadius = 4;
  w->fontWeight = FontWeight::Bold;
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