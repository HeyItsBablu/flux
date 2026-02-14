#ifndef FLUX_WIDGET_HPP
#define FLUX_WIDGET_HPP

#include <windows.h>
#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <iostream>

// ============================================================================
// FORWARD DECLARATIONS
// ============================================================================

class Widget;
class FontCache;

using WidgetPtr = std::shared_ptr<Widget>;
using ClickHandler = std::function<void()>;

// ============================================================================
// ENUMS
// ============================================================================

enum class Alignment
{
    Start,
    Center,
    End,
    Stretch
};

enum class MainAxisAlignment
{
    Start,
    Center,
    End,
    SpaceBetween,
    SpaceAround,
    SpaceEvenly
};

enum class FontWeight
{
    Light = FW_LIGHT,
    Normal = FW_NORMAL,
    Bold = FW_BOLD
};

// ============================================================================
// WIDGET BASE CLASS
// ============================================================================

class Widget : public std::enable_shared_from_this<Widget>
{
public:
    std::string id;
    std::string text;

    // Layout properties
    int x = 0, y = 0;
    int width = 0, height = 0;
    int minWidth = 0, minHeight = 0;
    int maxWidth = 10000, maxHeight = 10000;
    bool autoWidth = true, autoHeight = true;

    // Flex property for Expanded widget
    int flex = 1;

    // Spacing
    int padding = 0;
    int paddingLeft = 0, paddingRight = 0, paddingTop = 0, paddingBottom = 0;
    int margin = 0;
    int marginLeft = 0, marginRight = 0, marginTop = 0, marginBottom = 0;

    // Alignment
    Alignment alignment = Alignment::Start;
    Alignment crossAlignment = Alignment::Start;
    MainAxisAlignment mainAxisAlignment = MainAxisAlignment::Start;
    int spacing = 0;

    // Colors
    COLORREF backgroundColor = RGB(255, 255, 255);
    COLORREF textColor = RGB(0, 0, 0);
    COLORREF borderColor = RGB(0, 0, 0);
    bool hasBackground = false;
    bool hasBorder = false;

    // Border
    int borderWidth = 1;
    int borderRadius = 0;

    // Text styling
    int fontSize = 14;
    FontWeight fontWeight = FontWeight::Normal;

    // Events
    ClickHandler onClick;

    // Dirty flags
    bool needsLayout = true;
    bool needsPaint = true;

    // Children
    std::vector<WidgetPtr> children;
    Widget *parent = nullptr;

    // State binding
    void *boundState = nullptr;

    virtual ~Widget() = default;

    virtual bool isExpanded() const { return false; }

    // Virtual methods - Override these in subclasses
    virtual void computeLayout(HDC hdc, int availableWidth, int availableHeight, FontCache &fontCache);
    virtual void positionChildren(int contentX, int contentY, int contentWidth, int contentHeight);
    virtual void render(HDC hdc, FontCache &fontCache);

    // Mouse event handlers - Override these for interactive widgets
    virtual bool handleMouseWheel(int delta) { return false; }
    virtual bool handleMouseDown(int mx, int my) { return false; }
    virtual bool handleMouseUp(int mx, int my) { return false; }
    virtual bool handleMouseMove(int mx, int my) { return false; }
    virtual bool handleMouseLeave() { return false; }

    // Mark this widget and all parents as needing layout
    void markNeedsLayout()
    {
        needsLayout = true;
        needsPaint = true;
        if (parent)
        {
            parent->markNeedsLayout();
        }
    }

    void markNeedsPaint()
    {
        needsPaint = true;
    }

    // Builder pattern methods
    WidgetPtr setWidth(int w)
    {
        std::cout << "The width changes " << w << std::endl;
        if (width != w)
        {
            width = w;
            autoWidth = false;
            markNeedsLayout();
        }
        return shared_from_this();
    }

    WidgetPtr setHeight(int h)
    {
        if (height != h)
        {
            height = h;
            autoHeight = false;
            markNeedsLayout();
        }
        return shared_from_this();
    }

    WidgetPtr setMinWidth(int w)
    {
        if (minWidth != w)
        {
            minWidth = w;
            markNeedsLayout();
        }
        return shared_from_this();
    }

    WidgetPtr setMinHeight(int h)
    {
        if (minHeight != h)
        {
            minHeight = h;
            markNeedsLayout();
        }
        return shared_from_this();
    }

    WidgetPtr setMaxWidth(int w)
    {
        if (maxWidth != w)
        {
            maxWidth = w;
            markNeedsLayout();
        }
        return shared_from_this();
    }

    WidgetPtr setMaxHeight(int h)
    {
        if (maxHeight != h)
        {
            maxHeight = h;
            markNeedsLayout();
        }
        return shared_from_this();
    }

    WidgetPtr setFlex(int f)
    {
        if (flex != f)
        {
            flex = f;
            markNeedsLayout();
        }
        return shared_from_this();
    }

    WidgetPtr setPadding(int p)
    {
        padding = p;
        paddingLeft = paddingRight = paddingTop = paddingBottom = p;
        markNeedsLayout();
        return shared_from_this();
    }

    WidgetPtr setPaddingAll(int left, int top, int right, int bottom)
    {
        paddingLeft = left;
        paddingTop = top;
        paddingRight = right;
        paddingBottom = bottom;
        padding = -1;
        markNeedsLayout();
        return shared_from_this();
    }

    WidgetPtr setMargin(int m)
    {
        margin = m;
        marginLeft = marginRight = marginTop = marginBottom = m;
        markNeedsLayout();
        return shared_from_this();
    }

    WidgetPtr setMarginAll(int left, int top, int right, int bottom)
    {
        marginLeft = left;
        marginTop = top;
        marginRight = right;
        marginBottom = bottom;
        margin = -1;
        markNeedsLayout();
        return shared_from_this();
    }

    WidgetPtr setBackgroundColor(COLORREF color)
    {
        backgroundColor = color;
        hasBackground = true;
        markNeedsPaint();
        return shared_from_this();
    }

    WidgetPtr setTextColor(COLORREF color)
    {
        if (textColor != color)
        {
            textColor = color;
            markNeedsPaint();
        }
        return shared_from_this();
    }

    WidgetPtr setBorderColor(COLORREF color)
    {
        borderColor = color;
        hasBorder = true;
        markNeedsPaint();
        return shared_from_this();
    }

    WidgetPtr setBorderWidth(int w)
    {
        if (borderWidth != w)
        {
            borderWidth = w;
            hasBorder = true;
            markNeedsLayout();
        }
        return shared_from_this();
    }

    WidgetPtr setBorderRadius(int r)
    {
        if (borderRadius != r)
        {
            borderRadius = r;
            markNeedsPaint();
        }
        return shared_from_this();
    }

    WidgetPtr setFontSize(int size)
    {
        if (fontSize != size)
        {
            fontSize = size;
            markNeedsLayout();
        }
        return shared_from_this();
    }

    WidgetPtr setFontWeight(FontWeight weight)
    {
        if (fontWeight != weight)
        {
            fontWeight = weight;
            markNeedsLayout();
        }
        return shared_from_this();
    }

    WidgetPtr setOnClick(ClickHandler handler)
    {
        onClick = handler;
        return shared_from_this();
    }

    WidgetPtr setAlignment(Alignment align)
    {
        if (alignment != align)
        {
            alignment = align;
            markNeedsLayout();
        }
        return shared_from_this();
    }

    WidgetPtr setCrossAlignment(Alignment align)
    {
        if (crossAlignment != align)
        {
            crossAlignment = align;
            markNeedsLayout();
        }
        return shared_from_this();
    }

    WidgetPtr setMainAxisAlignment(MainAxisAlignment align)
    {
        if (mainAxisAlignment != align)
        {
            mainAxisAlignment = align;
            markNeedsLayout();
        }
        return shared_from_this();
    }

    WidgetPtr setSpacing(int s)
    {
        if (spacing != s)
        {
            spacing = s;
            markNeedsLayout();
        }
        return shared_from_this();
    }

    WidgetPtr setId(const std::string &i)
    {
        id = i;
        return shared_from_this();
    }

    WidgetPtr setText(const std::string &t)
    {
        if (text != t)
        {
            text = t;
            markNeedsLayout();
        }
        return shared_from_this();
    }

    void addChild(WidgetPtr child)
    {
        children.push_back(child);
        child->parent = this;
        markNeedsLayout();
    }

    const std::string &getText() const { return text; }
    const std::string &getId() const { return id; }

    void measureText(HDC hdc, FontCache &fontCache);

protected:
    void applyConstraints()
    {
        if (width < minWidth)
            width = minWidth;
        if (height < minHeight)
            height = minHeight;
        if (width > maxWidth)
            width = maxWidth;
        if (height > maxHeight)
            height = maxHeight;
    }

    void drawRoundedRectangle(HDC hdc)
    {
        if (borderRadius > 0)
        {
            HPEN pen = hasBorder ? CreatePen(PS_SOLID, borderWidth, borderColor) : CreatePen(PS_NULL, 0, 0);
            HBRUSH brush = CreateSolidBrush(backgroundColor);

            HPEN oldPen = (HPEN)SelectObject(hdc, pen);
            HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, brush);

            RoundRect(hdc, x, y, x + width, y + height, borderRadius * 2, borderRadius * 2);

            SelectObject(hdc, oldBrush);
            SelectObject(hdc, oldPen);
            DeleteObject(brush);
            DeleteObject(pen);
        }
        else
        {
            HBRUSH brush = CreateSolidBrush(backgroundColor);
            RECT rect = {x, y, x + width, y + height};
            FillRect(hdc, &rect, brush);
            DeleteObject(brush);

            if (hasBorder)
            {
                HPEN pen = CreatePen(PS_SOLID, borderWidth, borderColor);
                HPEN oldPen = (HPEN)SelectObject(hdc, pen);
                HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));

                Rectangle(hdc, x, y, x + width, y + height);

                SelectObject(hdc, oldBrush);
                SelectObject(hdc, oldPen);
                DeleteObject(pen);
            }
        }
    }

    void renderText(HDC hdc, FontCache &fontCache, UINT format = DT_LEFT | DT_VCENTER | DT_SINGLELINE);
};

// ============================================================================
// VIRTUAL METHOD IMPLEMENTATIONS (need FontCache declaration)
// ============================================================================

inline void Widget::computeLayout(HDC hdc, int availableWidth, int availableHeight, FontCache &fontCache)
{
    // Default: just apply constraints
    applyConstraints();
    needsLayout = false;
}

inline void Widget::positionChildren(int contentX, int contentY, int contentWidth, int contentHeight)
{
    // Default: position children at content origin
    for (auto &child : children)
    {
        child->x = contentX + child->marginLeft;
        child->y = contentY + child->marginTop;

        child->positionChildren(
            child->x + child->paddingLeft,
            child->y + child->paddingTop,
            child->width - child->paddingLeft - child->paddingRight,
            child->height - child->paddingTop - child->paddingBottom);
    }
}

inline void Widget::render(HDC hdc, FontCache &fontCache)
{
    // Default: draw background if has one
    if (hasBackground)
    {
        drawRoundedRectangle(hdc);
    }

    // Render all children
    for (auto &child : children)
    {
        child->render(hdc, fontCache);
    }

    needsPaint = false;
}

// ============================================================================
// HIT TESTING
// ============================================================================

inline Widget *findWidgetAt(Widget *w, int x, int y)
{
    if (!w)
        return nullptr;

    for (auto it = w->children.rbegin(); it != w->children.rend(); ++it)
    {
        Widget *found = findWidgetAt(it->get(), x, y);
        if (found)
            return found;
    }

    if (x >= w->x && x < w->x + w->width &&
        y >= w->y && y < w->y + w->height)
    {
        return w;
    }

    return nullptr;
}

// ============================================================================
// MOUSE EVENT HELPER FUNCTIONS
// ============================================================================

/**
 * Find widget at position and dispatch mouse event
 * Returns true if event was handled
 */
template<typename Handler>
inline bool findAndHandleMouseEvent(Widget* widget, int x, int y, Handler handler)
{
    if (!widget)
        return false;
    
    // Check if point is within widget bounds
    if (x >= widget->x && x < widget->x + widget->width &&
        y >= widget->y && y < widget->y + widget->height)
    {
        // Try children first (they're on top)
        for (auto it = widget->children.rbegin(); it != widget->children.rend(); ++it)
        {
            if (findAndHandleMouseEvent(it->get(), x, y, handler))
                return true;
        }
        
        // Then try this widget
        if (handler(widget))
            return true;
    }
    
    return false;
}

#endif // FLUX_WIDGET_HPP