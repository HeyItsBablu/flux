#ifndef FLUX_LISTVIEW_HPP
#define FLUX_LISTVIEW_HPP

#include "flux_core.hpp"
#include "flux_state.hpp"
#include "flux_widget_list.hpp"
#include <functional>
#include <vector>
#include <iostream>

// ============================================================================
// SCROLLABLE LISTVIEW BUILDER WIDGET
// ============================================================================

template <typename T>
class ListViewBuilder : public Widget
{
private:
    State<std::vector<T>> *boundState = nullptr;
    std::function<WidgetPtr(int, const T &)> builder;
    std::function<WidgetPtr()> separatorBuilder;
    int itemSpacing = 0;
    int lastItemCount = 0;
    std::shared_ptr<ListViewBuilder<T>> self;

    // Scroll state
    int scrollOffset = 0;
    int contentHeight = 0;
    int viewportHeight = 0;
    bool isScrollable = false;

    // Scrollbar properties
    int scrollbarWidth = 8;
    int scrollbarThumbHeight = 0;
    int scrollbarThumbY = 0;
    bool isDraggingScrollbar = false;
    int dragStartY = 0;
    int dragStartOffset = 0;
    
    // ✓ NEW: Hover state
    bool isHoveringScrollbar = false;
    
    // ✓ NEW: Colors with hover states
    COLORREF scrollbarColor = RGB(180, 180, 180);
    COLORREF scrollbarHoverColor = RGB(140, 140, 140);
    COLORREF scrollbarActiveColor = RGB(100, 100, 100);
    COLORREF scrollbarTrackColor = RGB(245, 245, 245);

    void rebuildList()
    {
        if (!boundState || !builder)
            return;

        const auto &items = boundState->get();

        if (items.size() == lastItemCount && !children.empty())
            return;

        lastItemCount = items.size();
        children.clear();

        for (size_t i = 0; i < items.size(); i++)
        {
            auto itemWidget = builder(i, items[i]);
            if (itemWidget)
            {
                addChild(itemWidget);
            }

            if (separatorBuilder && i < items.size() - 1)
            {
                auto separator = separatorBuilder();
                if (separator)
                {
                    addChild(separator);
                }
            }
        }

        markNeedsLayout();
    }

    void updateScrollbar()
    {
        if (!isScrollable)
        {
            scrollbarThumbHeight = 0;
            return;
        }

        float visibleRatio = (float)viewportHeight / (float)contentHeight;
        scrollbarThumbHeight = (int)(viewportHeight * visibleRatio);

        if (scrollbarThumbHeight < 30)
            scrollbarThumbHeight = 30;

        if (contentHeight > viewportHeight)
        {
            float scrollRatio = (float)scrollOffset / (float)(contentHeight - viewportHeight);
            scrollbarThumbY = (int)(scrollRatio * (viewportHeight - scrollbarThumbHeight));
        }
        else
        {
            scrollbarThumbY = 0;
        }
    }

    void clampScrollOffset()
    {
        int maxScroll = contentHeight - viewportHeight;
        if (maxScroll < 0)
            maxScroll = 0;

        if (scrollOffset < 0)
            scrollOffset = 0;
        if (scrollOffset > maxScroll)
            scrollOffset = maxScroll;
    }

    void renderScrollbar(HDC hdc)
    {
        if (!isScrollable)
            return;

        // Draw scrollbar track
        HBRUSH trackBrush = CreateSolidBrush(scrollbarTrackColor);
        RECT trackRect = {
            x + width - scrollbarWidth,
            y,
            x + width,
            y + height};
        FillRect(hdc, &trackRect, trackBrush);
        DeleteObject(trackBrush);

        // ✓ NEW: Choose color based on state
        COLORREF thumbColor = scrollbarColor;
        if (isDraggingScrollbar)
        {
            thumbColor = scrollbarActiveColor;
        }
        else if (isHoveringScrollbar)
        {
            thumbColor = scrollbarHoverColor;
        }

        // Draw scrollbar thumb with state-based color
        HBRUSH thumbBrush = CreateSolidBrush(thumbColor);
        RECT thumbRect = {
            x + width - scrollbarWidth,
            y + scrollbarThumbY,
            x + width,
            y + scrollbarThumbY + scrollbarThumbHeight};
        FillRect(hdc, &thumbRect, thumbBrush);
        DeleteObject(thumbBrush);
    }

    // ✓ NEW: Helper to check if mouse is over scrollbar thumb
    bool isMouseOverScrollbarThumb(int mx, int my) const
    {
        if (!isScrollable)
            return false;

        int scrollbarX = x + width - scrollbarWidth;
        int thumbTop = y + scrollbarThumbY;
        int thumbBottom = thumbTop + scrollbarThumbHeight;

        return (mx >= scrollbarX && mx < x + width &&
                my >= thumbTop && my < thumbBottom);
    }

public:
    ListViewBuilder(State<std::vector<T>> &state) : boundState(&state)
    {
        lastItemCount = state.get().size();

        state.listen([this](const std::vector<T> &newValue)
                     {
            rebuildList();
            
            if (boundState && boundState->hasContext())
            {
                auto* ui = boundState->getContext();
                if (ui)
                {
                    ui->partialRebuild(this);
                }
            } });
    }

    void setSelf(std::shared_ptr<ListViewBuilder<T>> ptr)
    {
        self = ptr;
    }

    std::shared_ptr<ListViewBuilder<T>> itemBuilder(std::function<WidgetPtr(int, const T &)> builderFunc)
    {
        builder = builderFunc;
        rebuildList();
        return self;
    }

    std::shared_ptr<ListViewBuilder<T>> separator(std::function<WidgetPtr()> separatorFunc)
    {
        separatorBuilder = separatorFunc;
        return self;
    }

    std::shared_ptr<ListViewBuilder<T>> spacing(int space)
    {
        itemSpacing = space;
        return self;
    }

    bool handleMouseWheel(int delta) override
    {
        if (!isScrollable)
            return false;

        int scrollAmount = (delta / WHEEL_DELTA) * 40;
        scrollOffset -= scrollAmount;

        clampScrollOffset();
        updateScrollbar();

        positionChildren(
            x + paddingLeft,
            y + paddingTop,
            width - paddingLeft - paddingRight - scrollbarWidth,
            height - paddingTop - paddingBottom);

        markNeedsPaint();
        return true;
    }

    bool handleMouseDown(int mx, int my) override
    {
        if (!isScrollable)
            return false;

        int scrollbarX = x + width - scrollbarWidth;
        if (mx >= scrollbarX && mx < x + width)
        {
            int thumbTop = y + scrollbarThumbY;
            int thumbBottom = thumbTop + scrollbarThumbHeight;

            if (my >= thumbTop && my < thumbBottom)
            {
                isDraggingScrollbar = true;
                dragStartY = my;
                dragStartOffset = scrollOffset;
                markNeedsPaint(); // ✓ NEW: Repaint for active color
                return true;
            }

            float clickRatio = (float)(my - y) / (float)viewportHeight;
            scrollOffset = (int)(clickRatio * (contentHeight - viewportHeight));
            clampScrollOffset();
            updateScrollbar();

            positionChildren(
                x + paddingLeft,
                y + paddingTop,
                width - paddingLeft - paddingRight - scrollbarWidth,
                height - paddingTop - paddingBottom);

            markNeedsPaint();
            return true;
        }

        return false;
    }

    bool handleMouseUp(int mx, int my) override
    {
        if (isDraggingScrollbar)
        {
            isDraggingScrollbar = false;
            
            // ✓ NEW: Update hover state after releasing
            isHoveringScrollbar = isMouseOverScrollbarThumb(mx, my);
            
            markNeedsPaint(); // ✓ NEW: Repaint to show hover/normal color
            return true;
        }
        return false;
    }

    bool handleMouseMove(int mx, int my) override
    {
        // ✓ NEW: Track hover state
        bool wasHovering = isHoveringScrollbar;
        isHoveringScrollbar = isMouseOverScrollbarThumb(mx, my);
        
        if (isDraggingScrollbar)
        {
            int deltaY = my - dragStartY;
            float scrollRatio = (float)deltaY / (float)(viewportHeight - scrollbarThumbHeight);
            scrollOffset = dragStartOffset + (int)(scrollRatio * (contentHeight - viewportHeight));

            clampScrollOffset();
            updateScrollbar();

            positionChildren(
                x + paddingLeft,
                y + paddingTop,
                width - paddingLeft - paddingRight - scrollbarWidth,
                height - paddingTop - paddingBottom);

            markNeedsPaint();
            return true;
        }
        
        // ✓ NEW: Repaint if hover state changed
        if (wasHovering != isHoveringScrollbar)
        {
            markNeedsPaint();
            return true;
        }
        
        return false;
    }

    void computeLayout(HDC hdc, int availableWidth, int availableHeight, FontCache &fontCache) override
    {
        rebuildList();

        viewportHeight = availableHeight - paddingTop - paddingBottom;
        int contentWidth = availableWidth - paddingLeft - paddingRight - scrollbarWidth;

        int totalHeight = 0;
        int maxWidth = 0;

        for (size_t i = 0; i < children.size(); i++)
        {
            auto &child = children[i];
            child->computeLayout(hdc, contentWidth, viewportHeight, fontCache);

            totalHeight += child->height;
            if (child->width > maxWidth)
                maxWidth = child->width;

            if (itemSpacing > 0 && i < children.size() - 1)
            {
                totalHeight += itemSpacing;
            }
        }

        contentHeight = totalHeight;
        isScrollable = (contentHeight > viewportHeight);

        if (autoWidth)
            width = maxWidth + paddingLeft + paddingRight + scrollbarWidth;
        if (autoHeight)
            height = viewportHeight + paddingTop + paddingBottom;

        updateScrollbar();
        applyConstraints();
        needsLayout = false;
    }

    void positionChildren(int contentX, int contentY, int contentWidth, int contentHeight) override
    {
        int currentY = contentY - scrollOffset;

        for (size_t i = 0; i < children.size(); i++)
        {
            auto &child = children[i];

            child->x = contentX;
            child->y = currentY;

            child->positionChildren(
                child->x + child->paddingLeft,
                child->y + child->paddingTop,
                child->width - child->paddingLeft - child->paddingRight,
                child->height - child->paddingTop - child->paddingBottom);

            currentY += child->height;

            if (itemSpacing > 0 && i < children.size() - 1)
            {
                currentY += itemSpacing;
            }
        }
    }

    void render(HDC hdc, FontCache &fontCache) override
    {
        updateScrollbar();

        HRGN clipRegion = CreateRectRgn(
            x + paddingLeft,
            y + paddingTop,
            x + width - scrollbarWidth - paddingRight,
            y + height - paddingBottom);
        SelectClipRgn(hdc, clipRegion);

        if (hasBackground)
        {
            drawRoundedRectangle(hdc);
        }

        for (auto &child : children)
        {
            int childTop = child->y;
            int childBottom = child->y + child->height;
            int viewportTop = y + paddingTop;
            int viewportBottom = y + height - paddingBottom;

            if (childBottom >= viewportTop && childTop < viewportBottom)
            {
                child->render(hdc, fontCache);
            }
        }

        SelectClipRgn(hdc, NULL);
        DeleteObject(clipRegion);

        renderScrollbar(hdc);

        needsPaint = false;
    }
    
    // ✓ NEW: Optional customization methods
    std::shared_ptr<ListViewBuilder<T>> setScrollbarColor(COLORREF color)
    {
        scrollbarColor = color;
        return self;
    }
    
    std::shared_ptr<ListViewBuilder<T>> setScrollbarHoverColor(COLORREF color)
    {
        scrollbarHoverColor = color;
        return self;
    }
    
    std::shared_ptr<ListViewBuilder<T>> setScrollbarActiveColor(COLORREF color)
    {
        scrollbarActiveColor = color;
        return self;
    }
};

// ============================================================================
// FACTORY FUNCTION
// ============================================================================

template <typename T>
inline std::shared_ptr<ListViewBuilder<T>> ListView(State<std::vector<T>> &state)
{
    auto widget = std::make_shared<ListViewBuilder<T>>(state);
    widget->setSelf(widget);
    return widget;
}

#endif // FLUX_LISTVIEW_HPP