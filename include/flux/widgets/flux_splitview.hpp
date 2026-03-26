#ifndef FLUX_SPLIT_VIEW_HPP
#define FLUX_SPLIT_VIEW_HPP

#include "flux_core.hpp"
#include "flux_layout.hpp"
#include "flux_state.hpp"

// ============================================================================
// SPLITVIEW WIDGET
// ============================================================================
//
// A two-pane resizable container whose children[0] and children[1] are
// separated by a draggable divider.  Mirrors Windows-style SplitView:
//
//   ┌──────────────┬─┬──────────────┐
//   │              │▓│              │
//   │   Left pane  │▓│  Right pane  │
//   │  children[0] │▓│  children[1] │
//   │              │▓│              │
//   └──────────────┴─┴──────────────┘
//
// Usage:
//   auto sv = SplitView(leftWidget, rightWidget)
//                  ->setRatio(0.35f)
//                  ->setMinPaneWidth(120)
//                  ->setDividerWidth(6)
//                  ->setDividerColor(RGB(200, 200, 200));
//
// Reactive ratio — bind to a State<float>:
//   State<float> ratio(0.4f, app);
//   auto sv = SplitView(left, right)->setRatio(ratio);
//   ratio.set(0.6f); // panes re-layout automatically
//
// The widget supports both horizontal (default) and vertical splits.
// ============================================================================

class SplitViewWidget : public Widget {
public:
    // ── Configuration ────────────────────────────────────────────────────────
    float   ratio        = 0.5f;   // fraction of total width given to pane 0
    int     dividerWidth = 6;      // pixels wide (or tall for vertical split)
    int     minPaneWidth = 80;     // minimum size of either pane in pixels
    bool    vertical     = false;  // false = left/right, true = top/bottom
    bool    resizable    = true;   // allow drag-resize

    COLORREF dividerColor      = RGB(210, 210, 210);
    COLORREF dividerHoverColor = RGB(33,  150, 243);
    COLORREF dividerDragColor  = RGB(25,  118, 210);

    // Callback fired after every ratio change (e.g. to persist user's choice)
    std::function<void(float)> onRatioChanged;

    // ── computeLayout ────────────────────────────────────────────────────────
    void computeLayout(GraphicsContext &ctx, const BoxConstraints &constraints,
                       FontCache &fontCache) override {
        // Fill available space (like Scaffold/Column/ExpandedWidget)
        if (autoWidth)  width  = constraints.maxWidth;
        if (autoHeight) height = constraints.maxHeight;

        // Clamp stored ratio so neither pane is below minPaneWidth
        _clampRatio();

        // Compute divider rect in absolute widget-local coords
        if (!vertical) {
            int pane0W = _pane0Size();
            _dividerX = pane0W;
            _dividerY = 0;
            _dividerW = dividerWidth;
            _dividerH = height;
        } else {
            int pane0H = _pane0Size();
            _dividerX = 0;
            _dividerY = pane0H;
            _dividerW = width;
            _dividerH = dividerWidth;
        }

        // Layout pane 0 (left or top)
        if (children.size() >= 1) {
            auto &p0 = children[0];
            if (!vertical) {
                int p0W = _pane0Size();
                p0->computeLayout(ctx, BoxConstraints::tight(p0W, height), fontCache);
            } else {
                int p0H = _pane0Size();
                p0->computeLayout(ctx, BoxConstraints::tight(width, p0H), fontCache);
            }
        }

        // Layout pane 1 (right or bottom)
        if (children.size() >= 2) {
            auto &p1 = children[1];
            if (!vertical) {
                int p1W = _pane1Size();
                p1->computeLayout(ctx, BoxConstraints::tight(p1W, height), fontCache);
            } else {
                int p1H = _pane1Size();
                p1->computeLayout(ctx, BoxConstraints::tight(width, p1H), fontCache);
            }
        }

        applyConstraints();
        needsLayout = false;
    }

    // ── positionChildren ─────────────────────────────────────────────────────
    void positionChildren(int contentX, int contentY,
                          int /*contentWidth*/, int /*contentHeight*/) override {
        if (!vertical) {
            // Pane 0: starts at contentX
            if (children.size() >= 1) {
                auto &p0 = children[0];
                p0->x = contentX;
                p0->y = contentY;
                p0->positionChildren(
                    p0->x + p0->paddingLeft, p0->y + p0->paddingTop,
                    p0->width  - p0->paddingLeft - p0->paddingRight,
                    p0->height - p0->paddingTop  - p0->paddingBottom);
            }
            // Pane 1: starts after the divider
            if (children.size() >= 2) {
                auto &p1 = children[1];
                p1->x = contentX + _pane0Size() + dividerWidth;
                p1->y = contentY;
                p1->positionChildren(
                    p1->x + p1->paddingLeft, p1->y + p1->paddingTop,
                    p1->width  - p1->paddingLeft - p1->paddingRight,
                    p1->height - p1->paddingTop  - p1->paddingBottom);
            }
        } else {
            // Pane 0: starts at contentY
            if (children.size() >= 1) {
                auto &p0 = children[0];
                p0->x = contentX;
                p0->y = contentY;
                p0->positionChildren(
                    p0->x + p0->paddingLeft, p0->y + p0->paddingTop,
                    p0->width  - p0->paddingLeft - p0->paddingRight,
                    p0->height - p0->paddingTop  - p0->paddingBottom);
            }
            // Pane 1: starts below the divider
            if (children.size() >= 2) {
                auto &p1 = children[1];
                p1->x = contentX;
                p1->y = contentY + _pane0Size() + dividerWidth;
                p1->positionChildren(
                    p1->x + p1->paddingLeft, p1->y + p1->paddingTop,
                    p1->width  - p1->paddingLeft - p1->paddingRight,
                    p1->height - p1->paddingTop  - p1->paddingBottom);
            }
        }
    }

    // ── render ───────────────────────────────────────────────────────────────
    void render(GraphicsContext &ctx, FontCache &fontCache) override {
        if (!visible) return;

        // Background (optional)
        if (hasBackground)
            drawRoundedRectangle(ctx);

        // Render pane 0
        if (children.size() >= 1)
            children[0]->render(ctx, fontCache);

        // Render divider
        _renderDivider(ctx);

        // Render pane 1
        if (children.size() >= 2)
            children[1]->render(ctx, fontCache);

        needsPaint = false;
    }

    // ── Mouse events ─────────────────────────────────────────────────────────

    bool handleMouseDown(int mx, int my) override {
        if (!resizable) return false;
        if (_hitDivider(mx, my)) {
            _dragging    = true;
            _dragStartMx = mx;
            _dragStartMy = my;
            _dragStartRatio = ratio;
            markNeedsPaint();

            // Capture so we keep receiving WM_MOUSEMOVE even outside the window
            if (HWND hwnd = _getHWND())
                SetCapture(hwnd);

            return true;
        }
        return false;
    }

    bool handleMouseMove(int mx, int my) override {
        if (_dragging) {
            _applyDrag(mx, my);
            return true; // consume during drag
        }

        // Update hover state for cursor feedback
        bool nowOver = _hitDivider(mx, my);
        if (nowOver != _dividerHovered) {
            _dividerHovered = nowOver;
            markNeedsPaint();
            _updateCursor();
        }
        return false;
    }

    bool handleMouseUp(int mx, int my) override {
        if (!_dragging) return false;
        _dragging = false;
        _applyDrag(mx, my); // final position
        ReleaseCapture();
        markNeedsPaint();

        if (onRatioChanged)
            onRatioChanged(ratio);

        return true;
    }

    bool handleMouseLeave() override {
        if (_dividerHovered) {
            _dividerHovered = false;
            markNeedsPaint();
            _restoreCursor();
        }
        return false;
    }

    // ── Fluent setters ────────────────────────────────────────────────────────

    std::shared_ptr<SplitViewWidget> setRatio(float r) {
        ratio = r;
        _clampRatio();
        markNeedsLayout();
        return self();
    }

    // Reactive ratio — re-layouts whenever the state changes
    template <typename T, typename F>
    std::shared_ptr<SplitViewWidget> setRatio(State<T> &state, F transform) {
        std::function<float(const T &)> fn = transform;
        ratio = fn(state.get());
        _clampRatio();
        markNeedsLayout();

        state.bindProperty(
            shared_from_this(),
            [fn](Widget *w, const T &val) {
                auto *sv = static_cast<SplitViewWidget *>(w);
                sv->ratio = fn(val);
                sv->_clampRatio();
                sv->markNeedsLayout();
            },
            true /* needsLayout */);

        return self();
    }

    // Convenience overload — bind directly to a State<float>
    std::shared_ptr<SplitViewWidget> setRatio(State<float> &state) {
        return setRatio(state, [](const float &v) { return v; });
    }

    std::shared_ptr<SplitViewWidget> setMinPaneWidth(int px) {
        minPaneWidth = px;
        _clampRatio();
        markNeedsLayout();
        return self();
    }

    std::shared_ptr<SplitViewWidget> setDividerWidth(int px) {
        dividerWidth = px;
        markNeedsLayout();
        return self();
    }

    std::shared_ptr<SplitViewWidget> setDividerColor(COLORREF c) {
        dividerColor = c;
        markNeedsPaint();
        return self();
    }

    std::shared_ptr<SplitViewWidget> setDividerHoverColor(COLORREF c) {
        dividerHoverColor = c;
        markNeedsPaint();
        return self();
    }

    std::shared_ptr<SplitViewWidget> setDividerDragColor(COLORREF c) {
        dividerDragColor = c;
        markNeedsPaint();
        return self();
    }

    std::shared_ptr<SplitViewWidget> setVertical(bool v) {
        vertical = v;
        markNeedsLayout();
        return self();
    }

    std::shared_ptr<SplitViewWidget> setResizable(bool r) {
        resizable = r;
        return self();
    }

    std::shared_ptr<SplitViewWidget> setOnRatioChanged(std::function<void(float)> cb) {
        onRatioChanged = std::move(cb);
        return self();
    }

    std::shared_ptr<SplitViewWidget> setWidth(int w) {
        width = w; autoWidth = false; markNeedsLayout(); return self();
    }
    std::shared_ptr<SplitViewWidget> setHeight(int h) {
        height = h; autoHeight = false; markNeedsLayout(); return self();
    }
    std::shared_ptr<SplitViewWidget> setBackgroundColor(COLORREF c) {
        backgroundColor = c; hasBackground = true; markNeedsPaint(); return self();
    }
    std::shared_ptr<SplitViewWidget> setFlex(int f) {
        flex = f; markNeedsLayout(); return self();
    }

    // Access panes by index (convenience for runtime modifications)
    WidgetPtr pane(int index) {
        if (index >= 0 && index < (int)children.size())
            return children[index];
        return nullptr;
    }

    // Swap pane contents without rebuilding the tree
    void swapPanes() {
        if (children.size() >= 2) {
            std::swap(children[0], children[1]);
            ratio = 1.0f - ratio;
            markNeedsLayout();
        }
    }

    // Collapse a pane (ratio → 0 or 1)
    void collapsePane(int index) {
        if (index == 0)      ratio = 0.0f;
        else if (index == 1) ratio = 1.0f;
        markNeedsLayout();
    }

    // Get current ratio (e.g. to persist after drag)
    float getRatio() const { return ratio; }

private:
    // ── Drag state ───────────────────────────────────────────────────────────
    bool  _dragging        = false;
    bool  _dividerHovered  = false;
    int   _dragStartMx     = 0;
    int   _dragStartMy     = 0;
    float _dragStartRatio  = 0.5f;

    // Cached divider screen-space rect (set during computeLayout, used for hit-test)
    int _dividerX = 0, _dividerY = 0, _dividerW = 6, _dividerH = 0;

    // ── Helpers ──────────────────────────────────────────────────────────────

    std::shared_ptr<SplitViewWidget> self() {
        return std::static_pointer_cast<SplitViewWidget>(shared_from_this());
    }

    // Total available space in the split axis
    int _totalSpace() const {
        return (vertical ? height : width) - dividerWidth;
    }

    // Size of pane 0 in the split axis
    int _pane0Size() const {
        return (int)(_totalSpace() * ratio);
    }

    // Size of pane 1 in the split axis
    int _pane1Size() const {
        return _totalSpace() - _pane0Size();
    }

    // Ensure ratio keeps both panes at or above minPaneWidth
    void _clampRatio() {
        int total = _totalSpace();
        if (total <= 0) return;

        float minR = (float)minPaneWidth / (float)total;
        float maxR = 1.0f - minR;

        if (minR > maxR) {
            // Total space too small — give each pane 50 %
            ratio = 0.5f;
        } else {
            ratio = max(minR, min(maxR, ratio));
        }
    }

    // Apply mouse-delta to ratio (called on every MOUSEMOVE while dragging)
    void _applyDrag(int mx, int my) {
        int total = _totalSpace();
        if (total <= 0) return;

        float delta;
        if (!vertical)
            delta = (float)(mx - _dragStartMx) / (float)total;
        else
            delta = (float)(my - _dragStartMy) / (float)total;

        ratio = _dragStartRatio + delta;
        _clampRatio();
        markNeedsLayout();

        // Force an immediate repaint — markNeedsLayout only sets a dirty flag,
        // it doesn't push a WM_PAINT to the window on its own.
        if (FluxUI *ui = FluxUI::getCurrentInstance())
            ui->updateWidget(this);
    }

    // Hit-test: is (mx, my) inside the divider strip?
    bool _hitDivider(int mx, int my) const {
        int dx = x + _dividerX;
        int dy = y + _dividerY;
        return (mx >= dx && mx < dx + _dividerW &&
                my >= dy && my < dy + _dividerH);
    }

    // Draw the divider strip with optional grip dots
    void _renderDivider(GraphicsContext &ctx) const {
        COLORREF col;
        if (_dragging)         col = dividerDragColor;
        else if (_dividerHovered) col = dividerHoverColor;
        else                   col = dividerColor;

        Gdiplus::Graphics g(ctx.hdc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

        // Divider background
        Gdiplus::Color fillCol(255,
            GetRValue(col), GetGValue(col), GetBValue(col));
        Gdiplus::SolidBrush brush(fillCol);

        int dx = x + _dividerX;
        int dy = y + _dividerY;
        g.FillRectangle(&brush, dx, dy, _dividerW, _dividerH);

        // Grip dots (three dots centred on the divider)
        if (resizable && dividerWidth >= 4) {
            COLORREF gripCol = _dragging ? dividerHoverColor : RGB(150, 150, 150);
            Gdiplus::Color gripColor(200,
                GetRValue(gripCol), GetGValue(gripCol), GetBValue(gripCol));
            Gdiplus::SolidBrush gripBrush(gripColor);

            const int dotR    = 2;
            const int dotGap  = 5; // gap between dot centres
            const int numDots = 3;

            if (!vertical) {
                // Horizontal split: dots stacked vertically in the centre
                int cx = dx + _dividerW / 2;
                int totalGripH = (numDots - 1) * dotGap;
                int startY = dy + _dividerH / 2 - totalGripH / 2;
                for (int i = 0; i < numDots; ++i) {
                    g.FillEllipse(&gripBrush,
                        cx - dotR, startY + i * dotGap - dotR,
                        dotR * 2, dotR * 2);
                }
            } else {
                // Vertical split: dots arranged horizontally
                int cy = dy + _dividerH / 2;
                int totalGripW = (numDots - 1) * dotGap;
                int startX = dx + _dividerW / 2 - totalGripW / 2;
                for (int i = 0; i < numDots; ++i) {
                    g.FillEllipse(&gripBrush,
                        startX + i * dotGap - dotR, cy - dotR,
                        dotR * 2, dotR * 2);
                }
            }
        }
    }

    // Change the system cursor to resize arrow while over the divider
    void _updateCursor() const {
        if (_getHWND()) {
            SetCursor(LoadCursor(nullptr, vertical ? IDC_SIZENS : IDC_SIZEWE));
        }
    }

    void _restoreCursor() const {
        SetCursor(LoadCursor(nullptr, IDC_ARROW));
    }

    HWND _getHWND() const {
        if (FluxUI *ui = FluxUI::getCurrentInstance())
            return ui->getWindow();
        return nullptr;
    }
};

// ============================================================================
// FACTORY FUNCTION
// ============================================================================

using SplitViewWidgetPtr = std::shared_ptr<SplitViewWidget>;

// Horizontal split (left | right)
inline SplitViewWidgetPtr SplitView(WidgetPtr left, WidgetPtr right,
                                    float ratio = 0.5f) {
    auto sv = std::make_shared<SplitViewWidget>();
    sv->ratio = ratio;
    if (left)  sv->addChild(left);
    if (right) sv->addChild(right);
    return sv;
}

// Vertical split (top / bottom)
inline SplitViewWidgetPtr SplitViewVertical(WidgetPtr top, WidgetPtr bottom,
                                            float ratio = 0.5f) {
    auto sv = std::make_shared<SplitViewWidget>();
    sv->ratio    = ratio;
    sv->vertical = true;
    if (top)    sv->addChild(top);
    if (bottom) sv->addChild(bottom);
    return sv;
}

#endif // FLUX_SPLIT_VIEW_HPP