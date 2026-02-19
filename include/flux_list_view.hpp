#ifndef FLUX_LISTVIEW_HPP
#define FLUX_LISTVIEW_HPP

#include "flux_core.hpp"
#include "flux_state.hpp"
#include "flux_widget_list.hpp"
#include <functional>
#include <vector>

// ============================================================================
// SCROLLABLE LISTVIEW BUILDER WIDGET
// ============================================================================
//
// Vertical (default):   ListView(state)->itemBuilder(...)
// Horizontal:           ListView(state)->setHorizontal(true)->itemBuilder(...)
//
// Scrollbar appears on the right  (vertical mode)
//                   or the bottom (horizontal mode)

template <typename T>
class ListViewBuilder : public Widget {
private:
    State<std::vector<T>> *boundState     = nullptr;
    std::function<WidgetPtr(int, const T &)> builder;
    std::function<WidgetPtr()>               separatorBuilder;
    int  itemSpacing   = 0;
    int  lastItemCount = 0;
    bool horizontal    = false;      // ← the one flag that changes everything
    std::shared_ptr<ListViewBuilder<T>> self;

    // ── Scroll state ──────────────────────────────────────────────────────
    int  scrollOffset    = 0;
    int  contentMain     = 0;   // total length along the scroll axis
    int  viewportMain    = 0;   // visible length along the scroll axis
    bool isScrollable    = false;

    // ── Scrollbar ─────────────────────────────────────────────────────────
    int  scrollbarSize        = 8;   // width (vertical) or height (horizontal)
    int  scrollbarThumbLength = 0;
    int  scrollbarThumbOffset = 0;
    bool isDraggingScrollbar  = false;
    int  dragStartPos         = 0;
    int  dragStartOffset      = 0;
    bool isHoveringScrollbar  = false;

    COLORREF scrollbarColor       = RGB(180, 180, 180);
    COLORREF scrollbarHoverColor  = RGB(140, 140, 140);
    COLORREF scrollbarActiveColor = RGB(100, 100, 100);
    COLORREF scrollbarTrackColor  = RGB(245, 245, 245);

    // ── Helpers ───────────────────────────────────────────────────────────

    void rebuildList() {
        if (!boundState || !builder) return;

        const auto &items = boundState->get();
        if ((int)items.size() == lastItemCount && !children.empty()) return;

        lastItemCount = (int)items.size();
        children.clear();

        for (size_t i = 0; i < items.size(); i++) {
            auto w = builder((int)i, items[i]);
            if (w) addChild(w);

            if (separatorBuilder && i < items.size() - 1) {
                auto sep = separatorBuilder();
                if (sep) addChild(sep);
            }
        }
        markNeedsLayout();
    }

    void clampScrollOffset() {
        int maxScroll =max(0, contentMain - viewportMain);
        scrollOffset  =max(0,min(scrollOffset, maxScroll));
    }

    void updateScrollbar() {
        if (!isScrollable) { scrollbarThumbLength = scrollbarThumbOffset = 0; return; }

        float visRatio        = (float)viewportMain / (float)contentMain;
        scrollbarThumbLength  =max(30, (int)(viewportMain * visRatio));

        float scrollRatio     = (float)scrollOffset /
                                (float)(contentMain - viewportMain);
        scrollbarThumbOffset  = (int)(scrollRatio *
                                (viewportMain - scrollbarThumbLength));
    }

    void releaseDragCapture() {
        if (isDraggingScrollbar) {
            isDraggingScrollbar = false;
            isHoveringScrollbar = false;
            ReleaseCapture();
        }
    }

    bool isMouseOverThumb(int mx, int my) const {
        if (!isScrollable) return false;
        if (horizontal) {
            int sbY = y + height - scrollbarSize;
            return mx >= x + scrollbarThumbOffset &&
                   mx <  x + scrollbarThumbOffset + scrollbarThumbLength &&
                   my >= sbY && my < y + height;
        } else {
            int sbX = x + width - scrollbarSize;
            return mx >= sbX && mx < x + width &&
                   my >= y + scrollbarThumbOffset &&
                   my <  y + scrollbarThumbOffset + scrollbarThumbLength;
        }
    }

    void renderScrollbar(HDC hdc) {
        COLORREF thumbColor = isDraggingScrollbar ? scrollbarActiveColor
                            : isHoveringScrollbar ? scrollbarHoverColor
                                                  : scrollbarColor;
        RECT trackRect, thumbRect;

        if (horizontal) {
            int sbY   = y + height - scrollbarSize;
            trackRect = { x,                         sbY, x + width,                              y + height };
            thumbRect = { x + scrollbarThumbOffset,  sbY, x + scrollbarThumbOffset + scrollbarThumbLength, y + height };
        } else {
            int sbX   = x + width - scrollbarSize;
            trackRect = { sbX, y,                          x + width, y + height                             };
            thumbRect = { sbX, y + scrollbarThumbOffset,   x + width, y + scrollbarThumbOffset + scrollbarThumbLength };
        }

        HBRUSH br = CreateSolidBrush(scrollbarTrackColor);
        FillRect(hdc, &trackRect, br);
        DeleteObject(br);

        br = CreateSolidBrush(thumbColor);
        FillRect(hdc, &thumbRect, br);
        DeleteObject(br);
    }

    void repositionChildren() {
        int sbSize = isScrollable ? scrollbarSize : 0;

        if (horizontal) {
            int curX     = x + paddingLeft - scrollOffset;
            int contentY = y + paddingTop;
            int availH   = height - paddingTop - paddingBottom - sbSize;

            for (size_t i = 0; i < children.size(); i++) {
                auto &child = children[i];
                child->x = curX;
                child->y = contentY;
                child->positionChildren(
                    child->x + child->paddingLeft, child->y + child->paddingTop,
                    child->width  - child->paddingLeft - child->paddingRight,
                    child->height - child->paddingTop  - child->paddingBottom);
                curX += child->width;
                if (itemSpacing > 0 && i < children.size() - 1)
                    curX += itemSpacing;
            }
        } else {
            int curY     = y + paddingTop - scrollOffset;
            int contentX = x + paddingLeft;

            for (size_t i = 0; i < children.size(); i++) {
                auto &child = children[i];
                child->x = contentX;
                child->y = curY;
                child->positionChildren(
                    child->x + child->paddingLeft, child->y + child->paddingTop,
                    child->width  - child->paddingLeft - child->paddingRight,
                    child->height - child->paddingTop  - child->paddingBottom);
                curY += child->height;
                if (itemSpacing > 0 && i < children.size() - 1)
                    curY += itemSpacing;
            }
        }
    }

public:
    explicit ListViewBuilder(State<std::vector<T>> &state)
        : boundState(&state)
    {
        lastItemCount = (int)state.get().size();

        state.listen([this](const std::vector<T> &) {
            rebuildList();
            if (boundState && boundState->hasContext())
                if (auto *ui = boundState->getContext())
                    ui->partialRebuild(this);
        });
    }

    ~ListViewBuilder() override { if (isDraggingScrollbar) ReleaseCapture(); }

    void setSelf(std::shared_ptr<ListViewBuilder<T>> ptr) { self = ptr; }

    // ── Configuration ─────────────────────────────────────────────────────

    std::shared_ptr<ListViewBuilder<T>>
    itemBuilder(std::function<WidgetPtr(int, const T &)> fn) {
        builder = fn; rebuildList(); return self;
    }

    std::shared_ptr<ListViewBuilder<T>>
    separator(std::function<WidgetPtr()> fn) {
        separatorBuilder = fn; return self;
    }

    std::shared_ptr<ListViewBuilder<T>> setSpacing(int s)
        { itemSpacing = s; return self; }

    std::shared_ptr<ListViewBuilder<T>> setHorizontal(bool h) {
        horizontal = h;
        scrollOffset = 0;         // reset scroll when direction switches
        markNeedsLayout();
        return self;
    }

    std::shared_ptr<ListViewBuilder<T>> setScrollbarSize(int s)
        { scrollbarSize = s; return self; }
    std::shared_ptr<ListViewBuilder<T>> setScrollbarColor(COLORREF c)
        { scrollbarColor = c; return self; }
    std::shared_ptr<ListViewBuilder<T>> setScrollbarHoverColor(COLORREF c)
        { scrollbarHoverColor = c; return self; }
    std::shared_ptr<ListViewBuilder<T>> setScrollbarActiveColor(COLORREF c)
        { scrollbarActiveColor = c; return self; }
    std::shared_ptr<ListViewBuilder<T>> setScrollbarTrackColor(COLORREF c)
        { scrollbarTrackColor = c; return self; }

    // ── Layout ────────────────────────────────────────────────────────────

    void computeLayout(HDC hdc, int availableWidth, int availableHeight,
                       FontCache &fontCache) override
    {
        rebuildList();

        if (autoWidth  || width  > availableWidth)  width  = availableWidth;
        if (autoHeight || height > availableHeight)  height = availableHeight;

        int sbSize = isScrollable ? scrollbarSize : 0;

        if (horizontal) {
            viewportMain = width - paddingLeft - paddingRight;
            int availH   = height - paddingTop - paddingBottom - sbSize;
            int total    = 0, maxCross = 0;

            for (size_t i = 0; i < children.size(); i++) {
                children[i]->computeLayout(hdc, availableWidth, availH, fontCache);
                total    += children[i]->width;
                maxCross  = max(maxCross, children[i]->height);
                if (itemSpacing > 0 && i < children.size() - 1)
                    total += itemSpacing;
            }
            contentMain = total;
        } else {
            viewportMain = height - paddingTop - paddingBottom;
            int availW   = width - paddingLeft - paddingRight - sbSize;
            int total    = 0, maxCross = 0;

            for (size_t i = 0; i < children.size(); i++) {
                children[i]->computeLayout(hdc, availW, availableHeight, fontCache);
                total    += children[i]->height;
                maxCross  = max(maxCross, children[i]->width);
                if (itemSpacing > 0 && i < children.size() - 1)
                    total += itemSpacing;
            }
            contentMain = total;
        }

        bool wasScrollable = isScrollable;
        isScrollable = (contentMain > viewportMain);

        if (wasScrollable && !isScrollable) {
            scrollOffset = 0;
            releaseDragCapture();
            isHoveringScrollbar = false;
        } else if (!wasScrollable && isScrollable) {
            clampScrollOffset();
        }

        updateScrollbar();
        applyConstraints();
        needsLayout = false;
    }

    void positionChildren(int, int, int, int) override {
        repositionChildren();
    }

    // ── Mouse events ──────────────────────────────────────────────────────

    bool handleMouseWheel(int delta) override {
        if (!isScrollable) return false;
        scrollOffset -= (delta / WHEEL_DELTA) * 40;
        clampScrollOffset();
        updateScrollbar();
        repositionChildren();
        markNeedsPaint();
        return true;
    }

    bool handleMouseDown(int mx, int my) override {
        if (!isScrollable) return false;

        // Check if click is inside the scrollbar strip
        bool inScrollbarStrip = horizontal
            ? (my >= y + height - scrollbarSize && my < y + height)
            : (mx >= x + width  - scrollbarSize && mx < x + width);

        if (!inScrollbarStrip) return false;

        int pos = horizontal ? mx - x : my - y;

        if (pos >= scrollbarThumbOffset &&
            pos <  scrollbarThumbOffset + scrollbarThumbLength)
        {
            // Drag thumb
            isDraggingScrollbar = true;
            dragStartPos        = horizontal ? mx : my;
            dragStartOffset     = scrollOffset;

            if (boundState && boundState->hasContext())
                if (auto *ui = boundState->getContext())
                    if (ui->getWindow())
                        SetCapture(ui->getWindow());
            markNeedsPaint();
            return true;
        }

        // Click on track — jump
        float ratio  = (float)pos / (float)viewportMain;
        scrollOffset = (int)(ratio * (contentMain - viewportMain));
        clampScrollOffset();
        updateScrollbar();
        repositionChildren();
        markNeedsPaint();
        return true;
    }

    bool handleMouseUp(int mx, int my) override {
        if (isDraggingScrollbar) {
            releaseDragCapture();
            markNeedsPaint();
            return true;
        }
        return false;
    }

    bool handleMouseMove(int mx, int my) override {
        if (isDraggingScrollbar) {
            if (!isScrollable) { releaseDragCapture(); markNeedsPaint(); return true; }

            int   curPos  = horizontal ? mx : my;
            int   delta   = curPos - dragStartPos;
            float ratio   = (float)delta / (float)(viewportMain - scrollbarThumbLength);
            scrollOffset  = dragStartOffset + (int)(ratio * (contentMain - viewportMain));
            clampScrollOffset();
            updateScrollbar();
            repositionChildren();
            markNeedsPaint();
            return true;
        }

        bool wasHovering    = isHoveringScrollbar;
        isHoveringScrollbar = isScrollable && isMouseOverThumb(mx, my);
        if (wasHovering != isHoveringScrollbar) { markNeedsPaint(); return true; }
        return false;
    }

    bool handleMouseLeave() override {
        bool changed        = isHoveringScrollbar;
        isHoveringScrollbar = false;
        if (changed) { markNeedsPaint(); return true; }
        return false;
    }

    // ── Render ────────────────────────────────────────────────────────────

    void render(HDC hdc, FontCache &fontCache) override {
        updateScrollbar();

        int sbSize = isScrollable ? scrollbarSize : 0;

        // Clip to viewport
        RECT clipRect;
        if (horizontal) {
            clipRect = { x + paddingLeft,  y + paddingTop,
                         x + width - paddingRight, y + height - paddingBottom - sbSize };
        } else {
            clipRect = { x + paddingLeft,  y + paddingTop,
                         x + width - paddingRight - sbSize, y + height - paddingBottom };
        }

        HRGN clip = CreateRectRgn(clipRect.left, clipRect.top,
                                  clipRect.right, clipRect.bottom);
        SelectClipRgn(hdc, clip);

        if (hasBackground) drawRoundedRectangle(hdc);

        for (auto &child : children) {
            bool visible = horizontal
                ? (child->x + child->width  >= clipRect.left && child->x < clipRect.right)
                : (child->y + child->height >= clipRect.top  && child->y < clipRect.bottom);
            if (visible) child->render(hdc, fontCache);
        }

        SelectClipRgn(hdc, NULL);
        DeleteObject(clip);

        if (isScrollable) renderScrollbar(hdc);

        needsPaint = false;
    }
};

// ============================================================================
// FACTORY
// ============================================================================

template <typename T>
inline std::shared_ptr<ListViewBuilder<T>>
ListView(State<std::vector<T>> &state) {
    auto w = std::make_shared<ListViewBuilder<T>>(state);
    w->setSelf(w);
    return w;
}

#endif // FLUX_LISTVIEW_HPP