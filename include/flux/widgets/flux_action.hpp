#ifndef FLUX_ACTION_HPP
#define FLUX_ACTION_HPP

// v3 — hooks into Widget's existing virtual mouse methods.
// No InputDispatcher, no WndProc edits required.
#include "flux_core.hpp"

// ============================================================================
// HANDLER TYPES
// ============================================================================

using GestureHandler = std::function<void()>;
using PointerHandler = std::function<void(int x, int y)>;
using DragHandler = std::function<void(int dx, int dy)>;
using ScrollHandler = std::function<void(int delta)>;

// ============================================================================
// GESTURE DETECTOR WIDGET
// ============================================================================
//
// Overrides the same handleMouseDown / handleMouseUp / handleMouseMove /
// handleMouseWheel virtuals that FluxUI's WndProc already calls via
// findAndHandleMouseEvent() and broadcastMouseEvent().
//
// Nothing extra to wire up — just use GestureDetector(child) and set callbacks.

class GestureDetectorWidget : public Widget {
public:
  static constexpr DWORD LONG_PRESS_MS = 500;
  static constexpr DWORD DOUBLE_TAP_MS = 300;
  static constexpr int DRAG_THRESHOLD = 5;

  // ── Callbacks ─────────────────────────────────────────────────────────────
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

  void computeLayout(GraphicsContext &ctx, const BoxConstraints &constraints,
                     FontCache &fontCache) override {
    if (autoWidth)
      width = constraints.maxWidth;
    if (autoHeight)
      height = constraints.maxHeight;

    if (!children.empty()) {
      children[0]->computeLayout(ctx, constraints, fontCache);
      if (autoWidth)
        width = children[0]->width;
      if (autoHeight)
        height = children[0]->height;
    }

    applyConstraints();
    needsLayout = false;
  }

  void positionChildren(int contentX, int contentY, int /*contentWidth*/,
                        int /*contentHeight*/) override {
    if (!children.empty()) {
      auto &child = children[0];
      child->x = contentX;
      child->y = contentY;
      child->positionChildren(
          child->x + child->paddingLeft, child->y + child->paddingTop,
          child->width - child->paddingLeft - child->paddingRight,
          child->height - child->paddingTop - child->paddingBottom);
    }
  }

  void render(GraphicsContext &ctx, FontCache &fontCache) override {
    if (!children.empty())
      children[0]->render(ctx, fontCache);
    needsPaint = false;
  }

  // ── Mouse overrides — FluxUI WndProc calls these automatically ────────────

  // Called by WM_LBUTTONDOWN via findAndHandleMouseEvent()
  bool handleMouseDown(int mx, int my) override {
    if (!hitTest(mx, my))
      return false;

    _pressX = mx;
    _pressY = my;
    _pressed = true;
    _dragging = _dragStarted = false;
    _longPressArmed = true;
    _pressTime = GetTickCount();

    // Capture keeps WM_MOUSEMOVE + WM_LBUTTONUP flowing to us
    // even if the cursor leaves the widget bounds during a drag.
    if (HWND hwnd = getHWND())
      SetCapture(hwnd);

    return true;
  }

  // Called by WM_LBUTTONUP via broadcastMouseEvent() (captured) or
  // findAndHandleMouseEvent()
  bool handleMouseUp(int mx, int my) override {
    if (!_pressed)
      return false;

    _longPressArmed = false;
    ReleaseCapture();

    if (_dragging) {
      _dragging = false;
      if (onDragEnd)
        onDragEnd();
      _pressed = false;
      return true;
    }

    _pressed = false;

    // Released outside bounds after a non-drag press — no tap
    if (!hitTest(mx, my))
      return true;

    DWORD now = GetTickCount();
    if (_lastTapTime > 0 && (now - _lastTapTime) <= DOUBLE_TAP_MS) {
      if (onDoubleTap)
        onDoubleTap();
      _lastTapTime = 0;
    } else {
      if (onTap)
        onTap();
      _lastTapTime = now;
    }

    return true;
  }

  // Called by WM_RBUTTONDOWN via findAndHandleMouseEvent()
  bool handleRightClick(int mx, int my) override {
    if (!hitTest(mx, my))
      return false;
    if (onSecondaryTap)
      onSecondaryTap();
    return true;
  }

  // Called by WM_MOUSEMOVE via broadcastMouseEvent() (captured)
  // or findAndHandleMouseEvent()
  bool handleMouseMove(int mx, int my) override {
    if (_pressed) {
      if (!_dragStarted) {
        if (std::abs(mx - _pressX) > DRAG_THRESHOLD ||
            std::abs(my - _pressY) > DRAG_THRESHOLD) {
          _dragStarted = _dragging = true;
          _longPressArmed = false;
          _lastDragX = mx;
          _lastDragY = my;
          if (onDragStart)
            onDragStart();
        }
      } else if (_dragging) {
        if (onDragUpdate)
          onDragUpdate(mx - _lastDragX, my - _lastDragY);
        _lastDragX = mx;
        _lastDragY = my;
      }
    }

    if (hitTest(mx, my) && onPointerMove)
      onPointerMove(mx, my);

    return _pressed; // consume events during active drag
  }

  // Called by WM_MOUSEWHEEL via findAndHandleMouseEvent()
  bool handleMouseWheel(int delta) override {
    if (delta > 0) {
      if (onScrollUp)
        onScrollUp(delta);
      return true;
    } else {
      if (onScrollDown)
        onScrollDown(-delta);
      return true;
    }
    return false;
  }

  // // Called by updateHoverStates() in WM_MOUSEMOVE
  // void setHovered(bool h) override {
  //     if (h == _hovered) return;
  //     _hovered = h;
  //     if (h) { if (onHoverEnter) onHoverEnter(); }
  //     else   { if (onHoverExit)  onHoverExit();  }
  // }

  // ── Fluent setters ────────────────────────────────────────────────────────
  std::shared_ptr<GestureDetectorWidget> setOnTap(GestureHandler h) {
    onTap = std::move(h);
    return self();
  }
  std::shared_ptr<GestureDetectorWidget> setOnDoubleTap(GestureHandler h) {
    onDoubleTap = std::move(h);
    return self();
  }
  std::shared_ptr<GestureDetectorWidget> setOnLongPress(GestureHandler h) {
    onLongPress = std::move(h);
    return self();
  }
  std::shared_ptr<GestureDetectorWidget> setOnSecondaryTap(GestureHandler h) {
    onSecondaryTap = std::move(h);
    return self();
  }
  std::shared_ptr<GestureDetectorWidget> setOnHoverEnter(GestureHandler h) {
    onHoverEnter = std::move(h);
    return self();
  }
  std::shared_ptr<GestureDetectorWidget> setOnHoverExit(GestureHandler h) {
    onHoverExit = std::move(h);
    return self();
  }
  std::shared_ptr<GestureDetectorWidget> setOnPointerMove(PointerHandler h) {
    onPointerMove = std::move(h);
    return self();
  }
  std::shared_ptr<GestureDetectorWidget> setOnDragStart(GestureHandler h) {
    onDragStart = std::move(h);
    return self();
  }
  std::shared_ptr<GestureDetectorWidget> setOnDragUpdate(DragHandler h) {
    onDragUpdate = std::move(h);
    return self();
  }
  std::shared_ptr<GestureDetectorWidget> setOnDragEnd(GestureHandler h) {
    onDragEnd = std::move(h);
    return self();
  }
  std::shared_ptr<GestureDetectorWidget> setOnScrollUp(ScrollHandler h) {
    onScrollUp = std::move(h);
    return self();
  }
  std::shared_ptr<GestureDetectorWidget> setOnScrollDown(ScrollHandler h) {
    onScrollDown = std::move(h);
    return self();
  }
  std::shared_ptr<GestureDetectorWidget> setWidth(int w) {
    width = w;
    autoWidth = false;
    markNeedsLayout();
    return self();
  }
  std::shared_ptr<GestureDetectorWidget> setHeight(int h) {
    height = h;
    autoHeight = false;
    markNeedsLayout();
    return self();
  }

private:
  bool _pressed = false;
  bool _hovered = false;
  bool _dragging = false;
  bool _dragStarted = false;
  bool _longPressArmed = false;
  int _pressX = 0, _pressY = 0;
  int _lastDragX = 0, _lastDragY = 0;
  DWORD _pressTime = 0, _lastTapTime = 0;

  std::shared_ptr<GestureDetectorWidget> self() {
    return std::static_pointer_cast<GestureDetectorWidget>(shared_from_this());
  }

  bool hitTest(int mx, int my) const {
    return mx >= x && mx < x + width && my >= y && my < y + height;
  }

  HWND getHWND() const {
    if (FluxUI *ui = FluxUI::getCurrentInstance())
      return ui->getWindow();
    return nullptr;
  }
};

// --- Button Widget ---
class ButtonWidget : public Widget {
public:
  bool handleMouseDown(int mx, int my) override {
    if (mx >= x && mx < x + width && my >= y && my < y + height) {
      _pressed = true;
      markNeedsPaint(); // for press visual feedback
      return true;      // consumed — stops tree-walk going into child
    }
    return false;
  }

  bool handleMouseUp(int mx, int my) override {
    if (!_pressed)
      return false;
    _pressed = false;
    markNeedsPaint();
    if (mx >= x && mx < x + width && my >= y && my < y + height) {
      if (onClick)
        onClick();
    }
    return true;
  }

  // Optional: visual press state in render
  void render(GraphicsContext &ctx, FontCache &fontCache) override {
    if (hasBackground) {
      // Slightly darken when pressed
      if (_pressed) {
        COLORREF orig = backgroundColor;
        backgroundColor =
            RGB(max(0, GetRValue(orig) - 20), max(0, GetGValue(orig) - 20),
                max(0, GetBValue(orig) - 20));
        drawRoundedRectangle(ctx);
        backgroundColor = orig;
      } else {
        drawRoundedRectangle(ctx);
      }
    }

    if (!children.empty()) {
      children[0]->render(ctx, fontCache);
    } else if (!text.empty()) {
      renderText(ctx, fontCache, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    needsPaint = false;
  }

  void computeLayout(GraphicsContext &ctx, const BoxConstraints &constraints,
                     FontCache &fontCache) override {
    if (!children.empty()) {
      auto &child = children[0];

      child->computeLayout(ctx,
                           constraints.deflate(paddingLeft + paddingRight,
                                               paddingTop + paddingBottom),
                           fontCache);

      if (autoWidth)
        width = child->width + child->marginLeft + child->marginRight +
                paddingLeft + paddingRight;
      if (autoHeight)
        height = child->height + child->marginTop + child->marginBottom +
                 paddingTop + paddingBottom;
    } else if (!text.empty()) {
      measureText(ctx, fontCache);

      if (autoWidth)
        width += paddingLeft + paddingRight;
      if (autoHeight)
        height += paddingTop + paddingBottom;
    } else {
      if (autoWidth)
        width = paddingLeft + paddingRight;
      if (autoHeight)
        height = paddingTop + paddingBottom;
    }

    applyConstraints();
    needsLayout = false;
  }

  void positionChildren(int contentX, int contentY, int contentWidth,
                        int contentHeight) override {
    if (!children.empty()) {
      auto &child = children[0];

      // Center the child within the button
      int childX = contentX + (contentWidth - child->width - child->marginLeft -
                               child->marginRight) /
                                  2;
      int childY = contentY + (contentHeight - child->height -
                               child->marginTop - child->marginBottom) /
                                  2;

      child->x = childX + child->marginLeft;
      child->y = childY + child->marginTop;

      child->positionChildren(
          child->x + child->paddingLeft, child->y + child->paddingTop,
          child->width - child->paddingLeft - child->paddingRight,
          child->height - child->paddingTop - child->paddingBottom);
    }
  }

  // Helper method to set the child widget
  std::shared_ptr<ButtonWidget> setChild(WidgetPtr child) {
    children.clear();
    addChild(child);
    return std::static_pointer_cast<ButtonWidget>(shared_from_this());
  }

  std::shared_ptr<ButtonWidget> setBackgroundColor(COLORREF color) {
    backgroundColor = color;
    hasBackground = true;
    markNeedsPaint();
    return std::static_pointer_cast<ButtonWidget>(shared_from_this());
  }
  template <typename T, typename F>
  std::shared_ptr<ButtonWidget> setBackgroundColor(State<T> &state,
                                                   F transform) {
    std::function<COLORREF(const T &)> fn = transform;
    backgroundColor = fn(state.get());
    hasBackground = true;
    state.bindProperty(
        shared_from_this(),
        [fn](Widget *w, const T &val) { w->backgroundColor = fn(val); }, false);
    return std::static_pointer_cast<ButtonWidget>(shared_from_this());
  }

  std::shared_ptr<ButtonWidget> setHoverBackgroundColor(COLORREF color) {
    hoverBackgroundColor = color;
    hasHoverBackground = true;
    markNeedsPaint();
    return std::static_pointer_cast<ButtonWidget>(shared_from_this());
  }
  std::shared_ptr<ButtonWidget> setBorderRadius(int r) {
    borderRadius = r;
    markNeedsPaint();
    return std::static_pointer_cast<ButtonWidget>(shared_from_this());
  }
  std::shared_ptr<ButtonWidget> setPadding(int p) {
    paddingLeft = paddingRight = paddingTop = paddingBottom = p;
    markNeedsLayout();
    return std::static_pointer_cast<ButtonWidget>(shared_from_this());
  }
  std::shared_ptr<ButtonWidget> setWidth(int w) {
    width = w;
    autoWidth = false;
    markNeedsLayout();
    return std::static_pointer_cast<ButtonWidget>(shared_from_this());
  }
  std::shared_ptr<ButtonWidget> setHeight(int h) {
    height = h;
    autoHeight = false;
    markNeedsLayout();
    return std::static_pointer_cast<ButtonWidget>(shared_from_this());
  }
  std::shared_ptr<ButtonWidget> setTextColor(COLORREF color) {
    textColor = color;
    markNeedsPaint();
    return std::static_pointer_cast<ButtonWidget>(shared_from_this());
  }
  std::shared_ptr<ButtonWidget> setOnClick(ClickHandler handler) {
    onClick = handler;
    return std::static_pointer_cast<ButtonWidget>(shared_from_this());
  }

  std::shared_ptr<ButtonWidget> setPaddingAll(int left, int top, int right,
                                              int bottom) {
    paddingLeft = left;
    paddingTop = top;
    paddingRight = right;
    paddingBottom = bottom;
    padding = -1;
    markNeedsLayout();
    return std::static_pointer_cast<ButtonWidget>(shared_from_this());
  }

private:
  bool _pressed = false;
};

// ============================================================================
// FACTORY
// ============================================================================

using GestureDetectorPtr = std::shared_ptr<GestureDetectorWidget>;

inline GestureDetectorPtr GestureDetector(WidgetPtr child = nullptr) {
  auto w = std::make_shared<GestureDetectorWidget>();
  if (child)
    w->addChild(child);
  return w;
}
inline WidgetPtr GestureDetector(WidgetPtr child, DragHandler onDrag) {
  auto g = std::make_shared<GestureDetectorWidget>();
  g->addChild(child);
  g->onDragUpdate = std::move(onDrag);
  return g;
}

using ButtonWidgetPtr = std::shared_ptr<ButtonWidget>;

inline ButtonWidgetPtr Button(const std::string &text,
                              ClickHandler onClick = nullptr) {
  auto w = std::make_shared<ButtonWidget>();
  w->text = text;
  w->onClick = onClick;

  w->hasBackground = true;
  w->backgroundColor = RGB(76, 175, 80);
  w->textColor = RGB(255, 255, 255);
  w->paddingLeft = w->paddingRight = 20;
  w->paddingTop = w->paddingBottom = 10;
  w->borderRadius = 4;
  w->fontWeight = FontWeight::Bold;

  return w;
}

// New widget-based button
inline ButtonWidgetPtr Button(WidgetPtr child, ClickHandler onClick = nullptr) {
  auto w = std::make_shared<ButtonWidget>();
  w->addChild(child);
  w->onClick = onClick;

  w->hasBackground = true;
  w->backgroundColor = RGB(76, 175, 80);
  w->paddingLeft = w->paddingRight = 20;
  w->paddingTop = w->paddingBottom = 10;
  w->borderRadius = 4;

  return w;
}

#endif // FLUX_GESTURE_DETECTOR_HPP