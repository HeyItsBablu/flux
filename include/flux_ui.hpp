#ifndef FLUX_HPP
#define FLUX_HPP

#include <windows.h>
#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <map>
#include <tuple>
#include <algorithm>
#include <type_traits>

// ============================================================================
// FORWARD DECLARATIONS
// ============================================================================

class Widget;
class FluxUI;
template <typename T>
class State;
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
// FONT CACHE
// ============================================================================

class FontCache
{
private:
    std::map<std::tuple<int, FontWeight>, HFONT> cache;

public:
    ~FontCache()
    {
        for (auto &pair : cache)
        {
            DeleteObject(pair.second);
        }
        cache.clear();
    }

    HFONT getFont(int size, FontWeight weight)
    {
        auto key = std::make_tuple(size, weight);
        auto it = cache.find(key);

        if (it != cache.end())
        {
            return it->second;
        }

        HFONT hFont = CreateFont(
            size, 0, 0, 0, static_cast<int>(weight),
            FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");

        cache[key] = hFont;
        return hFont;
    }

    void clear()
    {
        for (auto &pair : cache)
        {
            DeleteObject(pair.second);
        }
        cache.clear();
    }
};

// ============================================================================
// WIDGET BASE CLASS (NOW ABSTRACT WITH VIRTUAL METHODS)
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

    // ✅ VIRTUAL METHODS - Override these in subclasses instead of switch/case!
    virtual void computeLayout(HDC hdc, int availableWidth, int availableHeight, FontCache &fontCache);
    virtual void positionChildren(int contentX, int contentY, int contentWidth, int contentHeight);
    virtual void render(HDC hdc, FontCache &fontCache);

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

    // Builder pattern methods (same as before)
    WidgetPtr setWidth(int w)
    {
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

    void measureText(HDC hdc, FontCache &fontCache)
    {
        if (text.empty())
        {
            width = 0;
            height = 0;
            return;
        }

        HFONT hFont = fontCache.getFont(fontSize, fontWeight);
        HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);

        SIZE size;
        GetTextExtentPoint32(hdc, text.c_str(), (int)text.length(), &size);

        if (autoWidth)
            width = size.cx;
        if (autoHeight)
            height = size.cy;

        SelectObject(hdc, hOldFont);
    }

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

    void renderText(HDC hdc, FontCache &fontCache, UINT format = DT_LEFT | DT_VCENTER | DT_SINGLELINE)
    {
        if (text.empty())
            return;

        SetTextColor(hdc, textColor);
        SetBkMode(hdc, TRANSPARENT);

        HFONT hFont = fontCache.getFont(fontSize, fontWeight);
        HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);

        RECT textRect = {
            x + paddingLeft,
            y + paddingTop,
            x + width - paddingRight,
            y + height - paddingBottom};

        DrawText(hdc, text.c_str(), -1, &textRect, format);

        SelectObject(hdc, hOldFont);
    }
};

// ============================================================================
// CONCRETE WIDGET CLASSES
// ============================================================================

// --- Text Widget ---
class TextWidget : public Widget
{
public:
    void computeLayout(HDC hdc, int availableWidth, int availableHeight, FontCache &fontCache) override
    {
        measureText(hdc, fontCache);
        width += paddingLeft + paddingRight;
        height += paddingTop + paddingBottom;
        applyConstraints();
        needsLayout = false;
    }

    void render(HDC hdc, FontCache &fontCache) override
    {
        if (hasBackground)
        {
            drawRoundedRectangle(hdc);
        }
        renderText(hdc, fontCache);
        needsPaint = false;
    }
};

// --- Button Widget ---
class ButtonWidget : public Widget
{
public:
    void computeLayout(HDC hdc, int availableWidth, int availableHeight, FontCache &fontCache) override
    {
        measureText(hdc, fontCache);
        width += paddingLeft + paddingRight;
        height += paddingTop + paddingBottom;
        applyConstraints();
        needsLayout = false;
    }

    void render(HDC hdc, FontCache &fontCache) override
    {
        if (hasBackground)
        {
            drawRoundedRectangle(hdc);
        }
        renderText(hdc, fontCache, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        needsPaint = false;
    }
};

// --- Column Widget ---
class ColumnWidget : public Widget
{
public:
    void computeLayout(HDC hdc, int availableWidth, int availableHeight, FontCache &fontCache) override
    {
        int contentWidth = availableWidth - paddingLeft - paddingRight;
        int contentHeight = availableHeight - paddingTop - paddingBottom;

        int totalFlex = 0;
        int fixedHeight = 0;

        // ✅ FIXED: Use virtual method instead of dynamic_cast
        for (auto &child : children)
        {
            if (child->isExpanded())
            {
                totalFlex += child->flex;
            }
            else
            {
                child->computeLayout(hdc, contentWidth, contentHeight, fontCache);
                fixedHeight += child->height;
            }
        }

        if (!children.empty())
        {
            fixedHeight += spacing * (children.size() - 1);
        }

        // Second pass: distribute remaining space to flex children
        int remainingHeight = contentHeight - fixedHeight;
        if (totalFlex > 0 && remainingHeight > 0)
        {
            for (auto &child : children)
            {
                if (child->isExpanded())
                {
                    int expandedHeight = (remainingHeight * child->flex) / totalFlex;
                    child->height = expandedHeight;
                    child->autoHeight = false;
                    child->computeLayout(hdc, contentWidth, expandedHeight, fontCache);
                }
            }
        }

        // Calculate final size
        int totalHeight = 0;
        int maxWidth = 0;

        for (size_t i = 0; i < children.size(); i++)
        {
            auto &child = children[i];
            if (child->width > maxWidth)
                maxWidth = child->width;
            totalHeight += child->height;
            if (i < children.size() - 1)
                totalHeight += spacing;
        }

        if (autoWidth)
            width = maxWidth + paddingLeft + paddingRight;
        if (autoHeight)
            height = totalHeight + paddingTop + paddingBottom;

        applyConstraints();
        needsLayout = false;
    }

    void positionChildren(int contentX, int contentY, int contentWidth, int contentHeight) override
    {
        int totalChildHeight = 0;
        for (auto &child : children)
        {
            totalChildHeight += child->height;
        }
        totalChildHeight += spacing * (children.empty() ? 0 : children.size() - 1);

        int currentY = contentY;

        if (mainAxisAlignment == MainAxisAlignment::Center)
        {
            currentY += (contentHeight - totalChildHeight) / 2;
        }
        else if (mainAxisAlignment == MainAxisAlignment::End)
        {
            currentY += contentHeight - totalChildHeight;
        }

        for (size_t i = 0; i < children.size(); i++)
        {
            auto &child = children[i];
            int childX = contentX;

            if (crossAlignment == Alignment::Center)
            {
                childX = contentX + (contentWidth - child->width) / 2;
            }
            else if (crossAlignment == Alignment::End)
            {
                childX = contentX + contentWidth - child->width;
            }
            else if (crossAlignment == Alignment::Stretch)
            {
                child->width = contentWidth;
            }

            child->x = childX + child->marginLeft;
            child->y = currentY + child->marginTop;

            child->positionChildren(
                child->x + child->paddingLeft,
                child->y + child->paddingTop,
                child->width - child->paddingLeft - child->paddingRight,
                child->height - child->paddingTop - child->paddingBottom);

            currentY += child->height + (i < children.size() - 1 ? spacing : 0);
        }
    }
};

// --- Row Widget ---
class RowWidget : public Widget
{
public:
    void computeLayout(HDC hdc, int availableWidth, int availableHeight, FontCache &fontCache) override
    {
        int contentWidth = availableWidth - paddingLeft - paddingRight;
        int contentHeight = availableHeight - paddingTop - paddingBottom;

        int totalFlex = 0;
        int fixedWidth = 0;

        // ✅ FIXED: Use virtual method
        for (auto &child : children)
        {
            if (child->isExpanded())
            {
                totalFlex += child->flex;
            }
            else
            {
                child->computeLayout(hdc, contentWidth, contentHeight, fontCache);
                fixedWidth += child->width;
            }
        }

        if (!children.empty())
        {
            fixedWidth += spacing * (children.size() - 1);
        }

        int remainingWidth = contentWidth - fixedWidth;
        if (totalFlex > 0 && remainingWidth > 0)
        {
            for (auto &child : children)
            {
                if (child->isExpanded())
                {
                    int expandedWidth = (remainingWidth * child->flex) / totalFlex;
                    child->width = expandedWidth;
                    child->autoWidth = false;
                    child->computeLayout(hdc, expandedWidth, contentHeight, fontCache);
                }
            }
        }

        int totalWidth = 0;
        int maxHeight = 0;

        for (size_t i = 0; i < children.size(); i++)
        {
            auto &child = children[i];
            totalWidth += child->width;
            if (child->height > maxHeight)
                maxHeight = child->height;
            if (i < children.size() - 1)
                totalWidth += spacing;
        }

        if (autoWidth)
            width = totalWidth + paddingLeft + paddingRight;
        if (autoHeight)
            height = maxHeight + paddingTop + paddingBottom;

        applyConstraints();
        needsLayout = false;
    }

    void positionChildren(int contentX, int contentY, int contentWidth, int contentHeight) override
    {
        int totalChildWidth = 0;
        for (auto &child : children)
        {
            totalChildWidth += child->width;
        }
        totalChildWidth += spacing * (children.empty() ? 0 : children.size() - 1);

        int currentX = contentX;

        if (mainAxisAlignment == MainAxisAlignment::Center)
        {
            currentX += (contentWidth - totalChildWidth) / 2;
        }
        else if (mainAxisAlignment == MainAxisAlignment::End)
        {
            currentX += contentWidth - totalChildWidth;
        }

        for (size_t i = 0; i < children.size(); i++)
        {
            auto &child = children[i];
            int childY = contentY;

            if (crossAlignment == Alignment::Center)
            {
                childY = contentY + (contentHeight - child->height) / 2;
            }
            else if (crossAlignment == Alignment::End)
            {
                childY = contentY + contentHeight - child->height;
            }
            else if (crossAlignment == Alignment::Stretch)
            {
                child->height = contentHeight;
            }

            child->x = currentX + child->marginLeft;
            child->y = childY + child->marginTop;

            child->positionChildren(
                child->x + child->paddingLeft,
                child->y + child->paddingTop,
                child->width - child->paddingLeft - child->paddingRight,
                child->height - child->paddingTop - child->paddingBottom);

            currentX += child->width + (i < children.size() - 1 ? spacing : 0);
        }
    }
};

// --- Container/Padding/Card Widgets ---
class ContainerWidget : public Widget
{
public:
    void computeLayout(HDC hdc, int availableWidth, int availableHeight, FontCache &fontCache) override
    {
        int contentWidth = availableWidth - paddingLeft - paddingRight;
        int contentHeight = availableHeight - paddingTop - paddingBottom;

        if (!children.empty())
        {
            children[0]->computeLayout(hdc, contentWidth, contentHeight, fontCache);
            if (autoWidth)
                width = children[0]->width + paddingLeft + paddingRight;
            if (autoHeight)
                height = children[0]->height + paddingTop + paddingBottom;
        }

        applyConstraints();
        needsLayout = false;
    }
};

// --- Center Widget ---
class CenterWidget : public Widget
{
public:
    void computeLayout(HDC hdc, int availableWidth, int availableHeight, FontCache &fontCache) override
    {
        int contentWidth = availableWidth - paddingLeft - paddingRight;
        int contentHeight = availableHeight - paddingTop - paddingBottom;

        if (!children.empty())
        {
            children[0]->computeLayout(hdc, contentWidth, contentHeight, fontCache);
        }

        if (autoWidth)
            width = availableWidth;
        if (autoHeight)
            height = availableHeight;

        applyConstraints();
        needsLayout = false;
    }

    void positionChildren(int contentX, int contentY, int contentWidth, int contentHeight) override
    {
        if (!children.empty())
        {
            auto &child = children[0];
            int childX = contentX + (contentWidth - child->width) / 2;
            int childY = contentY + (contentHeight - child->height) / 2;

            child->x = childX + child->marginLeft;
            child->y = childY + child->marginTop;

            child->positionChildren(
                child->x + child->paddingLeft,
                child->y + child->paddingTop,
                child->width - child->paddingLeft - child->paddingRight,
                child->height - child->paddingTop - child->paddingBottom);
        }
    }
};

// --- SizedBox Widget ---
class SizedBoxWidget : public Widget
{
public:
    void computeLayout(HDC hdc, int availableWidth, int availableHeight, FontCache &fontCache) override
    {
        if (!children.empty())
        {
            children[0]->computeLayout(hdc,
                                       width - paddingLeft - paddingRight,
                                       height - paddingTop - paddingBottom,
                                       fontCache);
        }
        applyConstraints();
        needsLayout = false;
    }
};

// --- Expanded Widget ---
class ExpandedWidget : public Widget
{
public:
    // ✅ OVERRIDE: Mark this as an expanded widget
    bool isExpanded() const override { return true; }

    void computeLayout(HDC hdc, int availableWidth, int availableHeight, FontCache &fontCache) override
    {
        if (!children.empty())
        {
            children[0]->computeLayout(hdc,
                                       width - paddingLeft - paddingRight,
                                       height - paddingTop - paddingBottom,
                                       fontCache);
            if (autoWidth)
                width = children[0]->width + paddingLeft + paddingRight;
            if (autoHeight)
                height = children[0]->height + paddingTop + paddingBottom;
        }
        applyConstraints();
        needsLayout = false;
    }
};

// --- Divider Widget ---
class DividerWidget : public Widget
{
public:
    void computeLayout(HDC hdc, int availableWidth, int availableHeight, FontCache &fontCache) override
    {
        if (autoWidth)
            width = availableWidth;
        applyConstraints();
        needsLayout = false;
    }

    void render(HDC hdc, FontCache &fontCache) override
    {
        if (hasBackground)
        {
            drawRoundedRectangle(hdc);
        }
        needsPaint = false;
    }
};

// --- Scaffold Widget ---
class ScaffoldWidget : public Widget
{
public:
    void computeLayout(HDC hdc, int availableWidth, int availableHeight, FontCache &fontCache) override
    {
        if (autoWidth)
            width = availableWidth;
        if (autoHeight)
            height = availableHeight;

        if (!children.empty())
        {
            children[0]->computeLayout(hdc, width, height, fontCache);
        }

        applyConstraints();
        needsLayout = false;
    }
};

// --- AppBar Widget ---
class AppBarWidget : public Widget
{
public:
    void computeLayout(HDC hdc, int availableWidth, int availableHeight, FontCache &fontCache) override
    {
        if (autoWidth)
            width = availableWidth;

        if (!children.empty())
        {
            auto &title = children[0];
            title->computeLayout(hdc,
                                 width - paddingLeft - paddingRight,
                                 height - paddingTop - paddingBottom,
                                 fontCache);
        }

        applyConstraints();
        needsLayout = false;
    }
};
// ============================================================================
// DEFAULT IMPLEMENTATIONS FOR BASE WIDGET
// ============================================================================

void Widget::computeLayout(HDC hdc, int availableWidth, int availableHeight, FontCache &fontCache)
{
    // Default: just apply constraints
    applyConstraints();
    needsLayout = false;
}

void Widget::positionChildren(int contentX, int contentY, int contentWidth, int contentHeight)
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

void Widget::render(HDC hdc, FontCache &fontCache)
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
// LAYOUT ENGINE (MUCH SIMPLER NOW!)
// ============================================================================

class LayoutEngine
{
public:
    static void computeLayout(HDC hdc, Widget *w, int availableWidth, int availableHeight, FontCache &fontCache)
    {
        if (!w)
            return;
        w->computeLayout(hdc, availableWidth, availableHeight, fontCache);
    }

    static void positionWidget(Widget *w, int x, int y)
    {
        if (!w)
            return;

        w->x = x + w->marginLeft;
        w->y = y + w->marginTop;

        int contentX = w->x + w->paddingLeft;
        int contentY = w->y + w->paddingTop;
        int contentWidth = w->width - w->paddingLeft - w->paddingRight;
        int contentHeight = w->height - w->paddingTop - w->paddingBottom;

        w->positionChildren(contentX, contentY, contentWidth, contentHeight);
    }
};

// ============================================================================
// RENDERER (MUCH SIMPLER NOW!)
// ============================================================================

class Renderer
{
public:
    static void renderWidget(HDC hdc, Widget *w, FontCache &fontCache)
    {
        if (!w)
            return;
        w->render(hdc, fontCache);
    }
};

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
// FLUXUI CLASS (UNCHANGED)
// ============================================================================

class FluxUI
{
private:
    WidgetPtr root;
    std::function<WidgetPtr()> builder;
    HWND hwnd = nullptr;
    HINSTANCE hInstance;
    FontCache fontCache;

    HDC hdcMem = nullptr;
    HBITMAP hbmMem = nullptr;
    HBITMAP hbmOld = nullptr;
    int bufferWidth = 0;
    int bufferHeight = 0;

    void createBackBuffer(int width, int height)
    {
        if (hdcMem && (width != bufferWidth || height != bufferHeight))
        {
            destroyBackBuffer();
        }

        if (!hdcMem)
        {
            HDC hdc = GetDC(hwnd);
            hdcMem = CreateCompatibleDC(hdc);
            hbmMem = CreateCompatibleBitmap(hdc, width, height);
            hbmOld = (HBITMAP)SelectObject(hdcMem, hbmMem);
            ReleaseDC(hwnd, hdc);

            bufferWidth = width;
            bufferHeight = height;
        }
    }

    void destroyBackBuffer()
    {
        if (hdcMem)
        {
            SelectObject(hdcMem, hbmOld);
            DeleteObject(hbmMem);
            DeleteDC(hdcMem);
            hdcMem = nullptr;
            hbmMem = nullptr;
            hbmOld = nullptr;
        }
    }

    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
    {
        FluxUI *instance = reinterpret_cast<FluxUI *>(GetWindowLongPtr(hwnd, GWLP_USERDATA));

        switch (uMsg)
        {
        case WM_CREATE:
        {
            CREATESTRUCT *pCreate = reinterpret_cast<CREATESTRUCT *>(lParam);
            instance = reinterpret_cast<FluxUI *>(pCreate->lpCreateParams);
            SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(instance));
            return 0;
        }

        case WM_PAINT:
        {
            if (!instance || !instance->root)
            {
                PAINTSTRUCT ps;
                HDC hdc = BeginPaint(hwnd, &ps);
                EndPaint(hwnd, &ps);
                return 0;
            }

            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);

            RECT clientRect;
            GetClientRect(hwnd, &clientRect);
            int width = clientRect.right - clientRect.left;
            int height = clientRect.bottom - clientRect.top;

            instance->createBackBuffer(width, height);

            HBRUSH bgBrush = CreateSolidBrush(RGB(250, 250, 250));
            FillRect(instance->hdcMem, &clientRect, bgBrush);
            DeleteObject(bgBrush);

            Renderer::renderWidget(instance->hdcMem, instance->root.get(), instance->fontCache);

            BitBlt(hdc, 0, 0, width, height, instance->hdcMem, 0, 0, SRCCOPY);

            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_SIZE:
        {
            if (instance && instance->root)
            {
                RECT rect;
                GetClientRect(hwnd, &rect);
                int width = rect.right - rect.left;
                int height = rect.bottom - rect.top;

                HDC hdc = GetDC(hwnd);
                LayoutEngine::computeLayout(hdc, instance->root.get(), width, height, instance->fontCache);
                LayoutEngine::positionWidget(instance->root.get(), 0, 0);
                ReleaseDC(hwnd, hdc);

                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        }

        case WM_LBUTTONDOWN:
        {
            if (!instance || !instance->root)
                return 0;

            int mouseX = LOWORD(lParam);
            int mouseY = HIWORD(lParam);

            Widget *clicked = findWidgetAt(instance->root.get(), mouseX, mouseY);
            if (clicked && clicked->onClick)
            {
                clicked->onClick();
            }
            return 0;
        }

        case WM_DESTROY:
            if (instance)
            {
                instance->destroyBackBuffer();
            }
            PostQuitMessage(0);
            return 0;
        }

        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }

public:
    FluxUI(HINSTANCE hInst) : hInstance(hInst) {}

    ~FluxUI()
    {
        destroyBackBuffer();
        fontCache.clear();
    }

    void build(std::function<WidgetPtr()> buildFunc)
    {
        builder = buildFunc;
        rebuild();
    }

    void rebuild()
    {
        if (!builder)
            return;

        root = builder();

        if (hwnd)
        {
            RECT rect;
            GetClientRect(hwnd, &rect);
            int width = rect.right - rect.left;
            int height = rect.bottom - rect.top;

            HDC hdc = GetDC(hwnd);
            LayoutEngine::computeLayout(hdc, root.get(), width, height, fontCache);
            LayoutEngine::positionWidget(root.get(), 0, 0);
            ReleaseDC(hwnd, hdc);

            InvalidateRect(hwnd, NULL, FALSE);
        }
    }

    void updateWidget(Widget *widget)
    {
        if (!widget || !hwnd)
            return;

        int oldWidth = widget->width;
        int oldHeight = widget->height;

        HDC hdc = GetDC(hwnd);
        widget->measureText(hdc, fontCache);
        widget->width += widget->paddingLeft + widget->paddingRight;
        widget->height += widget->paddingTop + widget->paddingBottom;
        ReleaseDC(hwnd, hdc);

        bool sizeChanged = (oldWidth != widget->width || oldHeight != widget->height);

        if (sizeChanged)
        {
            partialRebuild(widget);
        }
        else
        {
            invalidateWidget(widget);
        }
    }

    void invalidateWidget(Widget *widget)
    {
        if (!widget || !hwnd)
            return;

        RECT rect = {
            widget->x,
            widget->y,
            widget->x + widget->width,
            widget->y + widget->height};

        InvalidateRect(hwnd, &rect, FALSE);
    }

    void partialRebuild(Widget *widget)
    {
        if (!widget || !hwnd)
            return;

        RECT rect;
        GetClientRect(hwnd, &rect);
        int width = rect.right - rect.left;
        int height = rect.bottom - rect.top;

        Widget *current = widget;
        while (current)
        {
            current->markNeedsLayout();
            current = current->parent;
        }

        HDC hdc = GetDC(hwnd);
        LayoutEngine::computeLayout(hdc, root.get(), width, height, fontCache);
        LayoutEngine::positionWidget(root.get(), 0, 0);
        ReleaseDC(hwnd, hdc);

        InvalidateRect(hwnd, NULL, FALSE);
    }

    HWND createWindow(const std::string &title, int width, int height)
    {
        WNDCLASSEX wc = {0};
        wc.cbSize = sizeof(WNDCLASSEX);
        wc.lpfnWndProc = WindowProc;
        wc.hInstance = hInstance;
        wc.lpszClassName = "FluxUI";
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.style = CS_HREDRAW | CS_VREDRAW;

        RegisterClassEx(&wc);

        RECT windowRect = {0, 0, width, height};
        AdjustWindowRectEx(&windowRect, WS_OVERLAPPEDWINDOW, FALSE, 0);

        hwnd = CreateWindowEx(
            0,
            "FluxUI",
            title.c_str(),
            WS_OVERLAPPEDWINDOW | WS_VISIBLE,
            CW_USEDEFAULT, CW_USEDEFAULT,
            windowRect.right - windowRect.left,
            windowRect.bottom - windowRect.top,
            NULL, NULL,
            hInstance,
            this);

        if (hwnd && root)
        {
            RECT rect;
            GetClientRect(hwnd, &rect);
            int clientWidth = rect.right - rect.left;
            int clientHeight = rect.bottom - rect.top;

            HDC hdc = GetDC(hwnd);
            LayoutEngine::computeLayout(hdc, root.get(), clientWidth, clientHeight, fontCache);
            LayoutEngine::positionWidget(root.get(), 0, 0);
            ReleaseDC(hwnd, hdc);
        }

        return hwnd;
    }

    int run()
    {
        MSG msg;
        while (GetMessage(&msg, NULL, 0, 0))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        return (int)msg.wParam;
    }

    HWND getWindow() const { return hwnd; }
    WidgetPtr getRoot() const { return root; }

    WidgetPtr findById(const std::string &id)
    {
        return findByIdRecursive(root, id);
    }

    FontCache &getFontCache() { return fontCache; }

private:
    WidgetPtr findByIdRecursive(WidgetPtr widget, const std::string &id)
    {
        if (!widget)
            return nullptr;

        if (widget->getId() == id)
        {
            return widget;
        }

        for (auto &child : widget->children)
        {
            auto found = findByIdRecursive(child, id);
            if (found)
                return found;
        }

        return nullptr;
    }
};

// ============================================================================
// REACTIVE STATE CLASS
// ============================================================================

template <typename T>
class State
{
private:
    T value;
    FluxUI *ui;
    std::vector<std::weak_ptr<Widget>> observers;

    template <typename U = T>
    typename std::enable_if<std::is_arithmetic<U>::value, std::string>::type
    valueToString(const U &val)
    {
        return std::to_string(val);
    }

    template <typename U = T>
    typename std::enable_if<std::is_same<U, std::string>::value, std::string>::type
    valueToString(const U &val)
    {
        return val;
    }

public:
    State(T initial, FluxUI *app = nullptr) : value(initial), ui(app) {}

    T get() const { return value; }

    void set(T newValue)
    {
        if (value == newValue)
            return;

        value = newValue;
        std::string newText = valueToString(value);

        observers.erase(
            std::remove_if(observers.begin(), observers.end(),
                           [](const std::weak_ptr<Widget> &w)
                           { return w.expired(); }),
            observers.end());

        for (auto &weakWidget : observers)
        {
            if (auto widget = weakWidget.lock())
            {
                widget->text = newText;

                if (ui)
                {
                    ui->updateWidget(widget.get());
                }
            }
        }
    }

    void addObserver(std::shared_ptr<Widget> widget)
    {
        if (widget)
        {
            observers.push_back(widget);
            widget->boundState = this;
        }
    }

    void removeObserver(Widget *widget)
    {
        observers.erase(
            std::remove_if(observers.begin(), observers.end(),
                           [widget](const std::weak_ptr<Widget> &w)
                           {
                               if (auto locked = w.lock())
                               {
                                   return locked.get() == widget;
                               }
                               return true;
                           }),
            observers.end());
    }

    State &operator=(const T &newValue)
    {
        set(newValue);
        return *this;
    }

    operator T() const { return value; }

    State &operator++()
    {
        set(value + 1);
        return *this;
    }
    State &operator--()
    {
        set(value - 1);
        return *this;
    }
    State operator++(int)
    {
        State temp = *this;
        set(value + 1);
        return temp;
    }
    State operator--(int)
    {
        State temp = *this;
        set(value - 1);
        return temp;
    }
};

// ============================================================================
// WIDGET FACTORY FUNCTIONS (UPDATED TO USE NEW CLASSES)
// ============================================================================

inline WidgetPtr Container(WidgetPtr child = nullptr)
{
    auto w = std::make_shared<ContainerWidget>();
    if (child)
        w->addChild(child);
    return w;
}

inline WidgetPtr Text(const std::string &text)
{
    auto w = std::make_shared<TextWidget>();
    w->text = text;
    return w;
}

template <typename T>
typename std::enable_if<std::is_arithmetic<T>::value, std::string>::type
convertStateToString(State<T> &state)
{
    return std::to_string(state.get());
}

template <typename T>
typename std::enable_if<std::is_same<T, std::string>::value, std::string>::type
convertStateToString(State<T> &state)
{
    return state.get();
}

template <typename T>
inline WidgetPtr Text(State<T> &state)
{
    auto w = std::make_shared<TextWidget>();
    w->text = convertStateToString(state);
    state.addObserver(w);
    return w;
}

inline WidgetPtr Button(const std::string &text, ClickHandler onClick = nullptr)
{
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

template <typename... Widgets>
WidgetPtr Row(Widgets... widgets)
{
    auto w = std::make_shared<RowWidget>();
    (w->addChild(widgets), ...);
    return w;
}

template <typename... Widgets>
WidgetPtr Column(Widgets... widgets)
{
    auto w = std::make_shared<ColumnWidget>();
    (w->addChild(widgets), ...);
    return w;
}

inline WidgetPtr Padding(int padding, WidgetPtr child)
{
    auto w = std::make_shared<ContainerWidget>();
    w->padding = padding;
    w->paddingLeft = w->paddingRight = w->paddingTop = w->paddingBottom = padding;
    if (child)
        w->addChild(child);
    return w;
}

inline WidgetPtr Center(WidgetPtr child)
{
    auto w = std::make_shared<CenterWidget>();
    w->alignment = Alignment::Center;
    if (child)
        w->addChild(child);
    return w;
}

inline WidgetPtr SizedBox(int width, int height, WidgetPtr child = nullptr)
{
    auto w = std::make_shared<SizedBoxWidget>();
    w->width = width;
    w->height = height;
    w->autoWidth = false;
    w->autoHeight = false;
    if (child)
        w->addChild(child);
    return w;
}

inline WidgetPtr Card(WidgetPtr child)
{
    auto w = std::make_shared<ContainerWidget>();
    w->hasBackground = true;
    w->backgroundColor = RGB(255, 255, 255);
    w->hasBorder = true;
    w->borderColor = RGB(224, 224, 224);
    w->borderWidth = 1;
    w->borderRadius = 8;
    w->paddingLeft = w->paddingRight = w->paddingTop = w->paddingBottom = 16;
    if (child)
        w->addChild(child);
    return w;
}

inline WidgetPtr Divider()
{
    auto w = std::make_shared<DividerWidget>();
    w->height = 1;
    w->autoHeight = false;
    w->hasBackground = true;
    w->backgroundColor = RGB(224, 224, 224);
    return w;
}

inline WidgetPtr Expanded(WidgetPtr child, int flex = 1)
{
    auto w = std::make_shared<ExpandedWidget>();
    w->flex = flex;
    if (child)
        w->addChild(child);
    return w;
}

inline WidgetPtr AppBar(const std::string &title)
{
    auto w = std::make_shared<AppBarWidget>();

    w->hasBackground = true;
    w->backgroundColor = RGB(33, 150, 243);
    w->height = 56;
    w->autoHeight = false;

    auto titleWidget = Text(title)
                           ->setFontSize(20)
                           ->setFontWeight(FontWeight::Bold)
                           ->setTextColor(RGB(255, 255, 255))
                           ->setPadding(16);

    w->addChild(titleWidget);

    return w;
}

inline WidgetPtr Scaffold(WidgetPtr appBar = nullptr, WidgetPtr body = nullptr)
{
    auto w = std::make_shared<ScaffoldWidget>();

    w->hasBackground = true;
    w->backgroundColor = RGB(250, 250, 250);

    auto column = std::make_shared<ColumnWidget>();
    column->setSpacing(0);

    if (appBar)
    {
        column->addChild(appBar);
    }

    if (body)
    {
        auto bodyContainer = Container(body);
        bodyContainer->autoWidth = true;
        bodyContainer->autoHeight = true;
        column->addChild(Expanded(bodyContainer));
    }

    w->addChild(column);

    return w;
}

#endif // FLUX_HPP