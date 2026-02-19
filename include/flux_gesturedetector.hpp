#ifndef FLUX_GESTURE_DETECTOR_HPP
#define FLUX_GESTURE_DETECTOR_HPP

// v3 — hooks into Widget's existing virtual mouse methods.
// No InputDispatcher, no WndProc edits required.
#include "flux_core.hpp"

// ============================================================================
// HANDLER TYPES
// ============================================================================

using GestureHandler = std::function<void()>;
using PointerHandler = std::function<void(int x, int y)>;
using DragHandler    = std::function<void(int dx, int dy)>;
using ScrollHandler  = std::function<void(int delta)>;

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

    static constexpr DWORD LONG_PRESS_MS  = 500;
    static constexpr DWORD DOUBLE_TAP_MS  = 300;
    static constexpr int   DRAG_THRESHOLD = 5;

    // ── Callbacks ─────────────────────────────────────────────────────────────
    GestureHandler onTap;
    GestureHandler onDoubleTap;
    GestureHandler onLongPress;
    GestureHandler onSecondaryTap;
    GestureHandler onHoverEnter;
    GestureHandler onHoverExit;
    PointerHandler onPointerMove;
    GestureHandler onDragStart;
    DragHandler    onDragUpdate;
    GestureHandler onDragEnd;
    ScrollHandler  onScrollUp;
    ScrollHandler  onScrollDown;

    // ── Layout — transparent pass-through ─────────────────────────────────────
    void computeLayout(HDC hdc, int availableWidth, int availableHeight,
                       FontCache &fontCache) override
    {
        if (autoWidth)  width  = availableWidth;
        if (autoHeight) height = availableHeight;

        if (!children.empty()) {
            children[0]->computeLayout(hdc, width, height, fontCache);
            if (autoWidth)  width  = children[0]->width;
            if (autoHeight) height = children[0]->height;
        }

        applyConstraints();
        needsLayout = false;
    }

    void positionChildren(int contentX, int contentY,
                          int contentWidth, int contentHeight) override
    {
        if (!children.empty()) {
            auto &child = children[0];
            child->x = contentX;
            child->y = contentY;
            child->positionChildren(
                child->x + child->paddingLeft,
                child->y + child->paddingTop,
                child->width  - child->paddingLeft - child->paddingRight,
                child->height - child->paddingTop  - child->paddingBottom);
        }
    }

    void render(HDC hdc, FontCache &fontCache) override {
        if (!children.empty())
            children[0]->render(hdc, fontCache);
        needsPaint = false;
    }

    // ── Mouse overrides — FluxUI WndProc calls these automatically ────────────

    // Called by WM_LBUTTONDOWN via findAndHandleMouseEvent()
    bool handleMouseDown(int mx, int my) override {
        if (!hitTest(mx, my)) return false;

        _pressX = mx; _pressY = my;
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
        if (!_pressed) return false;

        _longPressArmed = false;
        ReleaseCapture();

        if (_dragging) {
            _dragging = false;
            if (onDragEnd) onDragEnd();
            _pressed = false;
            return true;
        }

        _pressed = false;

        // Released outside bounds after a non-drag press — no tap
        if (!hitTest(mx, my)) return true;

        DWORD now = GetTickCount();
        if (_lastTapTime > 0 && (now - _lastTapTime) <= DOUBLE_TAP_MS) {
            if (onDoubleTap) onDoubleTap();
            _lastTapTime = 0;
        } else {
            if (onTap) onTap();
            _lastTapTime = now;
        }

        return true;
    }

    // Called by WM_RBUTTONDOWN via findAndHandleMouseEvent()
    bool handleRightClick(int mx, int my) override {
        if (!hitTest(mx, my)) return false;
        if (onSecondaryTap) onSecondaryTap();
        return true;
    }

    // Called by WM_MOUSEMOVE via broadcastMouseEvent() (captured)
    // or findAndHandleMouseEvent()
    bool handleMouseMove(int mx, int my) override {
        if (_pressed) {
            if (!_dragStarted) {
                if (std::abs(mx - _pressX) > DRAG_THRESHOLD ||
                    std::abs(my - _pressY) > DRAG_THRESHOLD)
                {
                    _dragStarted = _dragging = true;
                    _longPressArmed = false;
                    _lastDragX = mx; _lastDragY = my;
                    if (onDragStart) onDragStart();
                }
            } else if (_dragging) {
                if (onDragUpdate) onDragUpdate(mx - _lastDragX, my - _lastDragY);
                _lastDragX = mx; _lastDragY = my;
            }
        }

        if (hitTest(mx, my) && onPointerMove)
            onPointerMove(mx, my);

        return _pressed; // consume events during active drag
    }

    // Called by WM_MOUSEWHEEL via findAndHandleMouseEvent()
    bool handleMouseWheel(int delta) override {
        if (delta > 0) { if (onScrollUp)   onScrollUp(delta);   return true; }
        else           { if (onScrollDown)  onScrollDown(-delta); return true; }
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
    std::shared_ptr<GestureDetectorWidget> setOnTap(GestureHandler h)
        { onTap = std::move(h); return self(); }
    std::shared_ptr<GestureDetectorWidget> setOnDoubleTap(GestureHandler h)
        { onDoubleTap = std::move(h); return self(); }
    std::shared_ptr<GestureDetectorWidget> setOnLongPress(GestureHandler h)
        { onLongPress = std::move(h); return self(); }
    std::shared_ptr<GestureDetectorWidget> setOnSecondaryTap(GestureHandler h)
        { onSecondaryTap = std::move(h); return self(); }
    std::shared_ptr<GestureDetectorWidget> setOnHoverEnter(GestureHandler h)
        { onHoverEnter = std::move(h); return self(); }
    std::shared_ptr<GestureDetectorWidget> setOnHoverExit(GestureHandler h)
        { onHoverExit = std::move(h); return self(); }
    std::shared_ptr<GestureDetectorWidget> setOnPointerMove(PointerHandler h)
        { onPointerMove = std::move(h); return self(); }
    std::shared_ptr<GestureDetectorWidget> setOnDragStart(GestureHandler h)
        { onDragStart = std::move(h); return self(); }
    std::shared_ptr<GestureDetectorWidget> setOnDragUpdate(DragHandler h)
        { onDragUpdate = std::move(h); return self(); }
    std::shared_ptr<GestureDetectorWidget> setOnDragEnd(GestureHandler h)
        { onDragEnd = std::move(h); return self(); }
    std::shared_ptr<GestureDetectorWidget> setOnScrollUp(ScrollHandler h)
        { onScrollUp = std::move(h); return self(); }
    std::shared_ptr<GestureDetectorWidget> setOnScrollDown(ScrollHandler h)
        { onScrollDown = std::move(h); return self(); }
    std::shared_ptr<GestureDetectorWidget> setWidth(int w)
        { width = w; autoWidth = false; markNeedsLayout(); return self(); }
    std::shared_ptr<GestureDetectorWidget> setHeight(int h)
        { height = h; autoHeight = false; markNeedsLayout(); return self(); }

private:
    bool  _pressed        = false;
    bool  _hovered        = false;
    bool  _dragging       = false;
    bool  _dragStarted    = false;
    bool  _longPressArmed = false;
    int   _pressX = 0, _pressY = 0;
    int   _lastDragX = 0, _lastDragY = 0;
    DWORD _pressTime = 0, _lastTapTime = 0;

    std::shared_ptr<GestureDetectorWidget> self() {
        return std::static_pointer_cast<GestureDetectorWidget>(shared_from_this());
    }

    bool hitTest(int mx, int my) const {
        return mx >= x && mx < x + width &&
               my >= y && my < y + height;
    }

    HWND getHWND() const {
        if (FluxUI *ui = FluxUI::getCurrentInstance())
            return ui->getWindow();
        return nullptr;
    }
};

// ============================================================================
// FACTORY
// ============================================================================

using GestureDetectorPtr = std::shared_ptr<GestureDetectorWidget>;

inline GestureDetectorPtr GestureDetector(WidgetPtr child = nullptr) {
    auto w = std::make_shared<GestureDetectorWidget>();
    if (child) w->addChild(child);
    return w;
}

#endif // FLUX_GESTURE_DETECTOR_HPP