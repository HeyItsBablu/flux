#ifndef FLUX_GESTURE_DETECTOR_HPP
#define FLUX_GESTURE_DETECTOR_HPP

#include "flux_core.hpp"

// ============================================================================
// GESTURE HANDLER TYPES
// ============================================================================

using GestureHandler      = std::function<void()>;
using PointerHandler      = std::function<void(int x, int y)>;
using DragHandler         = std::function<void(int dx, int dy)>;
using ScrollHandler       = std::function<void(int delta)>;
using DoubleTapHandler    = std::function<void()>;

// ============================================================================
// GESTURE DETECTOR WIDGET
// ============================================================================

// GestureDetector is a transparent wrapper that recognises common pointer
// gestures on its single child widget.  It never draws anything itself —
// all visual appearance comes from the child.
//
// Supported gestures (all optional):
//   onTap           – primary mouse button released inside the widget
//   onDoubleTap     – two clicks within DOUBLE_TAP_MS milliseconds
//   onLongPress     – button held for LONG_PRESS_MS milliseconds
//   onSecondaryTap  – right-click released inside the widget
//   onHoverEnter    – cursor moves into the widget bounds
//   onHoverExit     – cursor moves out of the widget bounds
//   onPointerMove   – cursor moves while inside the widget (x, y absolute)
//   onDragStart     – mouse pressed and moved beyond the drag threshold
//   onDragUpdate    – called each frame while dragging (dx, dy delta)
//   onDragEnd       – mouse released after a drag was active
//   onScrollUp      – mouse wheel scrolled up
//   onScrollDown    – mouse wheel scrolled down
//
// Usage:
//   GestureDetector(child)
//       ->onTap([](){ /* ... */ })
//       ->onDragUpdate([](int dx, int dy){ /* ... */ })
//       ->onHoverEnter([](){ /* highlight */ })
//       ->onHoverExit([](){ /* unhighlight */ });

class GestureDetectorWidget : public Widget {
public:

    // ── Tunable constants ────────────────────────────────────────────────────
    static constexpr UINT  LONG_PRESS_MS   = 500;   // ms until long-press fires
    static constexpr UINT  DOUBLE_TAP_MS   = 300;   // ms window for double-tap
    static constexpr int   DRAG_THRESHOLD  = 5;     // px movement to start drag

    // ── Gesture callbacks ────────────────────────────────────────────────────
    GestureHandler   onTap;
    DoubleTapHandler onDoubleTap;
    GestureHandler   onLongPress;
    GestureHandler   onSecondaryTap;
    GestureHandler   onHoverEnter;
    GestureHandler   onHoverExit;
    PointerHandler   onPointerMove;
    GestureHandler   onDragStart;
    DragHandler      onDragUpdate;
    GestureHandler   onDragEnd;
    ScrollHandler    onScrollUp;
    ScrollHandler    onScrollDown;

    // ── Layout — transparent pass-through ───────────────────────────────────
    void computeLayout(HDC hdc, int availableWidth, int availableHeight,
                       FontCache &fontCache) override
    {
        if (autoWidth)  width  = availableWidth;
        if (autoHeight) height = availableHeight;

        if (!children.empty()) {
            children[0]->computeLayout(hdc, width, height, fontCache);

            // Shrink-wrap to child if still auto-sizing
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

    // ── Render — nothing to draw; just forward to child ─────────────────────
    void render(HDC hdc, FontCache &fontCache) override {
        if (!children.empty())
            children[0]->render(hdc, fontCache);
        needsPaint = false;
    }

    // ── Event dispatch ───────────────────────────────────────────────────────
    // Call these from your WndProc / event loop.  Each returns true when the
    // event was consumed so you can stop propagation if needed.

    bool handleMouseDown(int mouseX, int mouseY, bool secondary = false)
    {
        if (!hitTest(mouseX, mouseY)) return false;

        if (!secondary) {
            _pressX      = mouseX;
            _pressY      = mouseY;
            _pressed     = true;
            _dragging    = false;
            _dragStarted = false;

            // Begin long-press timer
            _longPressArmed = true;
            _pressTime      = GetTickCount();
        }
        return true;
    }

    bool handleMouseUp(int mouseX, int mouseY, bool secondary = false)
    {
        if (secondary) {
            if (hitTest(mouseX, mouseY) && onSecondaryTap)
                onSecondaryTap();
            return true;
        }

        _longPressArmed = false;

        if (_dragging) {
            _dragging = false;
            if (onDragEnd) onDragEnd();
            _pressed = false;
            return true;
        }

        if (_pressed && hitTest(mouseX, mouseY)) {
            DWORD now = GetTickCount();

            // Double-tap detection
            if (_lastTapTime > 0 && (now - _lastTapTime) <= DOUBLE_TAP_MS) {
                if (onDoubleTap) onDoubleTap();
                _lastTapTime = 0;  // reset so a 3rd click starts fresh
            } else {
                if (onTap) onTap();
                _lastTapTime = now;
            }
        }

        _pressed = false;
        return true;
    }

    bool handleMouseMove(int mouseX, int mouseY)
    {
        bool inside = hitTest(mouseX, mouseY);

        // Hover enter / exit
        if (inside && !_hovered) {
            _hovered = true;
            if (onHoverEnter) onHoverEnter();
        } else if (!inside && _hovered) {
            _hovered = false;
            if (onHoverExit) onHoverExit();
        }

        // Pointer move callback (only fires while inside)
        if (inside && onPointerMove)
            onPointerMove(mouseX, mouseY);

        // Drag detection
        if (_pressed) {
            int dx = mouseX - _pressX;
            int dy = mouseY - _pressY;

            if (!_dragStarted) {
                if (std::abs(dx) > DRAG_THRESHOLD ||
                    std::abs(dy) > DRAG_THRESHOLD)
                {
                    _dragStarted = true;
                    _dragging    = true;
                    _longPressArmed = false;
                    _lastDragX   = mouseX;
                    _lastDragY   = mouseY;
                    if (onDragStart) onDragStart();
                }
            } else if (_dragging) {
                int ddx = mouseX - _lastDragX;
                int ddy = mouseY - _lastDragY;
                _lastDragX = mouseX;
                _lastDragY = mouseY;
                if (onDragUpdate) onDragUpdate(ddx, ddy);
            }
        }

        return inside;
    }

    // Call this from a WM_TIMER or your update loop to fire long-press.
    // Timer ID and registration are left to the host window — just call
    // this method on each tick and it self-arms/disarms.
    void handleTick()
    {
        if (_longPressArmed && _pressed) {
            DWORD elapsed = GetTickCount() - _pressTime;
            if (elapsed >= LONG_PRESS_MS) {
                _longPressArmed = false;
                _pressed        = false;
                if (onLongPress) onLongPress();
            }
        }
    }

    bool handleScroll(int mouseX, int mouseY, int delta)
    {
        if (!hitTest(mouseX, mouseY)) return false;

        if (delta > 0) { if (onScrollUp)   onScrollUp(delta);   }
        else           { if (onScrollDown)  onScrollDown(-delta); }

        return true;
    }

    // ── Fluent builder API ───────────────────────────────────────────────────
    std::shared_ptr<GestureDetectorWidget>
    setOnTap(GestureHandler h) {
        onTap = std::move(h);
        return std::static_pointer_cast<GestureDetectorWidget>(shared_from_this());
    }

    std::shared_ptr<GestureDetectorWidget>
    setOnDoubleTap(DoubleTapHandler h) {
        onDoubleTap = std::move(h);
        return std::static_pointer_cast<GestureDetectorWidget>(shared_from_this());
    }

    std::shared_ptr<GestureDetectorWidget>
    setOnLongPress(GestureHandler h) {
        onLongPress = std::move(h);
        return std::static_pointer_cast<GestureDetectorWidget>(shared_from_this());
    }

    std::shared_ptr<GestureDetectorWidget>
    setOnSecondaryTap(GestureHandler h) {
        onSecondaryTap = std::move(h);
        return std::static_pointer_cast<GestureDetectorWidget>(shared_from_this());
    }

    std::shared_ptr<GestureDetectorWidget>
    setOnHoverEnter(GestureHandler h) {
        onHoverEnter = std::move(h);
        return std::static_pointer_cast<GestureDetectorWidget>(shared_from_this());
    }

    std::shared_ptr<GestureDetectorWidget>
    setOnHoverExit(GestureHandler h) {
        onHoverExit = std::move(h);
        return std::static_pointer_cast<GestureDetectorWidget>(shared_from_this());
    }

    std::shared_ptr<GestureDetectorWidget>
    setOnPointerMove(PointerHandler h) {
        onPointerMove = std::move(h);
        return std::static_pointer_cast<GestureDetectorWidget>(shared_from_this());
    }

    std::shared_ptr<GestureDetectorWidget>
    setOnDragStart(GestureHandler h) {
        onDragStart = std::move(h);
        return std::static_pointer_cast<GestureDetectorWidget>(shared_from_this());
    }

    std::shared_ptr<GestureDetectorWidget>
    setOnDragUpdate(DragHandler h) {
        onDragUpdate = std::move(h);
        return std::static_pointer_cast<GestureDetectorWidget>(shared_from_this());
    }

    std::shared_ptr<GestureDetectorWidget>
    setOnDragEnd(GestureHandler h) {
        onDragEnd = std::move(h);
        return std::static_pointer_cast<GestureDetectorWidget>(shared_from_this());
    }

    std::shared_ptr<GestureDetectorWidget>
    setOnScrollUp(ScrollHandler h) {
        onScrollUp = std::move(h);
        return std::static_pointer_cast<GestureDetectorWidget>(shared_from_this());
    }

    std::shared_ptr<GestureDetectorWidget>
    setOnScrollDown(ScrollHandler h) {
        onScrollDown = std::move(h);
        return std::static_pointer_cast<GestureDetectorWidget>(shared_from_this());
    }

    std::shared_ptr<GestureDetectorWidget>
    setWidth(int w) {
        width = w; autoWidth = false; markNeedsLayout();
        return std::static_pointer_cast<GestureDetectorWidget>(shared_from_this());
    }

    std::shared_ptr<GestureDetectorWidget>
    setHeight(int h) {
        height = h; autoHeight = false; markNeedsLayout();
        return std::static_pointer_cast<GestureDetectorWidget>(shared_from_this());
    }

private:
    // Hit test against this widget's screen rectangle
    bool hitTest(int mouseX, int mouseY) const {
        return mouseX >= x && mouseX < x + width &&
               mouseY >= y && mouseY < y + height;
    }

    // ── Internal gesture state ───────────────────────────────────────────────
    bool   _pressed         = false;
    bool   _hovered         = false;
    bool   _dragging        = false;
    bool   _dragStarted     = false;
    bool   _longPressArmed  = false;

    int    _pressX          = 0;
    int    _pressY          = 0;
    int    _lastDragX       = 0;
    int    _lastDragY       = 0;

    DWORD  _pressTime       = 0;
    DWORD  _lastTapTime     = 0;
};

// ============================================================================
// FACTORY FUNCTION
// ============================================================================

using GestureDetectorPtr = std::shared_ptr<GestureDetectorWidget>;

inline GestureDetectorPtr GestureDetector(WidgetPtr child = nullptr) {
    auto w = std::make_shared<GestureDetectorWidget>();
    if (child) w->addChild(child);
    return w;
}

#endif // FLUX_GESTURE_DETECTOR_HPP