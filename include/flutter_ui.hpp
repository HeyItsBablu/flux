#ifndef FLUTTERUI_HPP
#define FLUTTERUI_HPP

#include <windows.h>
#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <map>
#include <tuple>
#include <algorithm>
#include <type_traits>
#include <unordered_set>

// ============================================================================
// FORWARD DECLARATIONS
// ============================================================================

class Widget;
class FlutterUI;
template <typename T>
class State;

using WidgetPtr = std::shared_ptr<Widget>;
using ClickHandler = std::function<void()>;

// ============================================================================
// ENUMS
// ============================================================================

enum class WidgetType
{
    Container,
    Text,
    Button,
    Row,
    Column,
    Padding,
    Center,
    SizedBox,
    Card,
    Divider,
    Expanded
};

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
// WIDGET CLASS
// ============================================================================

class Widget : public std::enable_shared_from_this<Widget>
{
public:
    WidgetType type;
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

    // Dirty flags for optimization
    bool needsLayout = true;
    bool needsPaint = true;

    // Children
    std::vector<WidgetPtr> children;
    Widget *parent = nullptr;

    // State binding (for reactive updates)
    void *boundState = nullptr; // Pointer to State<T> object

    // Constructor
    explicit Widget(WidgetType t) : type(t) {}

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

    // Mark this widget as needing repaint (without layout)
    void markNeedsPaint()
    {
        needsPaint = true;
    }

    // Builder pattern methods
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
};

// ============================================================================
// LAYOUT ENGINE
// ============================================================================

class LayoutEngine
{
public:
    static void measureText(HDC hdc, Widget *w, FontCache &fontCache)
    {
        if (w->text.empty())
        {
            w->width = 0;
            w->height = 0;
            return;
        }

        HFONT hFont = fontCache.getFont(w->fontSize, w->fontWeight);
        HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);

        SIZE size;
        GetTextExtentPoint32(hdc, w->text.c_str(), (int)w->text.length(), &size);

        if (w->autoWidth)
            w->width = size.cx;
        if (w->autoHeight)
            w->height = size.cy;

        SelectObject(hdc, hOldFont);
    }

    static void computeLayout(HDC hdc, Widget *w, int availableWidth, int availableHeight, FontCache &fontCache)
    {
        if (!w)
            return;

        int contentWidth = availableWidth - w->paddingLeft - w->paddingRight;
        int contentHeight = availableHeight - w->paddingTop - w->paddingBottom;

        switch (w->type)
        {
        case WidgetType::Text:
        case WidgetType::Button:
        {
            measureText(hdc, w, fontCache);
            w->width += w->paddingLeft + w->paddingRight;
            w->height += w->paddingTop + w->paddingBottom;
            break;
        }

        case WidgetType::Column:
        {
            int totalFlex = 0;
            int fixedHeight = 0;

            for (auto &child : w->children)
            {
                if (child->type == WidgetType::Expanded)
                {
                    totalFlex += child->flex;
                }
                else
                {
                    computeLayout(hdc, child.get(), contentWidth, contentHeight, fontCache);
                    fixedHeight += child->height;
                }
            }

            if (!w->children.empty())
            {
                fixedHeight += w->spacing * (w->children.size() - 1);
            }

            int remainingHeight = contentHeight - fixedHeight;
            if (totalFlex > 0 && remainingHeight > 0)
            {
                for (auto &child : w->children)
                {
                    if (child->type == WidgetType::Expanded)
                    {
                        int expandedHeight = (remainingHeight * child->flex) / totalFlex;
                        child->height = expandedHeight;
                        child->autoHeight = false;

                        if (!child->children.empty())
                        {
                            computeLayout(hdc, child->children[0].get(), contentWidth, expandedHeight, fontCache);
                        }
                    }
                }
            }

            int totalHeight = 0;
            int maxWidth = 0;

            for (size_t i = 0; i < w->children.size(); i++)
            {
                auto child = w->children[i].get();
                if (child->width > maxWidth)
                    maxWidth = child->width;
                totalHeight += child->height;
                if (i < w->children.size() - 1)
                    totalHeight += w->spacing;
            }

            if (w->autoWidth)
                w->width = maxWidth + w->paddingLeft + w->paddingRight;
            if (w->autoHeight)
                w->height = totalHeight + w->paddingTop + w->paddingBottom;
            break;
        }

        case WidgetType::Row:
        {
            int totalFlex = 0;
            int fixedWidth = 0;

            for (auto &child : w->children)
            {
                if (child->type == WidgetType::Expanded)
                {
                    totalFlex += child->flex;
                }
                else
                {
                    computeLayout(hdc, child.get(), contentWidth, contentHeight, fontCache);
                    fixedWidth += child->width;
                }
            }

            if (!w->children.empty())
            {
                fixedWidth += w->spacing * (w->children.size() - 1);
            }

            int remainingWidth = contentWidth - fixedWidth;
            if (totalFlex > 0 && remainingWidth > 0)
            {
                for (auto &child : w->children)
                {
                    if (child->type == WidgetType::Expanded)
                    {
                        int expandedWidth = (remainingWidth * child->flex) / totalFlex;
                        child->width = expandedWidth;
                        child->autoWidth = false;

                        if (!child->children.empty())
                        {
                            computeLayout(hdc, child->children[0].get(), expandedWidth, contentHeight, fontCache);
                        }
                    }
                }
            }

            int totalWidth = 0;
            int maxHeight = 0;

            for (size_t i = 0; i < w->children.size(); i++)
            {
                auto child = w->children[i].get();
                totalWidth += child->width;
                if (child->height > maxHeight)
                    maxHeight = child->height;
                if (i < w->children.size() - 1)
                    totalWidth += w->spacing;
            }

            if (w->autoWidth)
                w->width = totalWidth + w->paddingLeft + w->paddingRight;
            if (w->autoHeight)
                w->height = maxHeight + w->paddingTop + w->paddingBottom;
            break;
        }

        case WidgetType::Container:
        case WidgetType::Padding:
        case WidgetType::Center:
        case WidgetType::Card:
        {
            if (!w->children.empty())
            {
                computeLayout(hdc, w->children[0].get(), contentWidth, contentHeight, fontCache);
                if (w->autoWidth)
                    w->width = w->children[0]->width + w->paddingLeft + w->paddingRight;
                if (w->autoHeight)
                    w->height = w->children[0]->height + w->paddingTop + w->paddingBottom;
            }
            break;
        }

        case WidgetType::SizedBox:
        {
            if (!w->children.empty())
            {
                computeLayout(hdc, w->children[0].get(),
                              w->width - w->paddingLeft - w->paddingRight,
                              w->height - w->paddingTop - w->paddingBottom,
                              fontCache);
            }
            break;
        }

        case WidgetType::Expanded:
        {
            if (!w->children.empty())
            {
                computeLayout(hdc, w->children[0].get(),
                              w->width - w->paddingLeft - w->paddingRight,
                              w->height - w->paddingTop - w->paddingBottom,
                              fontCache);
                if (w->autoWidth)
                    w->width = w->children[0]->width + w->paddingLeft + w->paddingRight;
                if (w->autoHeight)
                    w->height = w->children[0]->height + w->paddingTop + w->paddingBottom;
            }
            break;
        }

        case WidgetType::Divider:
        {
            if (w->autoWidth)
                w->width = availableWidth;
            break;
        }
        }

        if (w->width < w->minWidth)
            w->width = w->minWidth;
        if (w->height < w->minHeight)
            w->height = w->minHeight;
        if (w->width > w->maxWidth)
            w->width = w->maxWidth;
        if (w->height > w->maxHeight)
            w->height = w->maxHeight;

        w->needsLayout = false;
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

        switch (w->type)
        {
        case WidgetType::Column:
        {
            int totalChildHeight = 0;
            for (auto &child : w->children)
            {
                totalChildHeight += child->height;
            }
            totalChildHeight += w->spacing * (w->children.empty() ? 0 : w->children.size() - 1);

            int currentY = contentY;

            if (w->mainAxisAlignment == MainAxisAlignment::Center)
            {
                currentY += (contentHeight - totalChildHeight) / 2;
            }
            else if (w->mainAxisAlignment == MainAxisAlignment::End)
            {
                currentY += contentHeight - totalChildHeight;
            }
            else if (w->mainAxisAlignment == MainAxisAlignment::SpaceAround)
            {
                int extraSpace = contentHeight - totalChildHeight;
                int gap = w->children.empty() ? 0 : extraSpace / w->children.size();
                currentY += gap / 2;
            }
            else if (w->mainAxisAlignment == MainAxisAlignment::SpaceEvenly)
            {
                int extraSpace = contentHeight - totalChildHeight;
                int gap = w->children.empty() ? 0 : extraSpace / (w->children.size() + 1);
                currentY += gap;
            }

            for (size_t i = 0; i < w->children.size(); i++)
            {
                auto &child = w->children[i];
                int childX = contentX;

                if (w->crossAlignment == Alignment::Center)
                {
                    childX = contentX + (contentWidth - child->width) / 2;
                }
                else if (w->crossAlignment == Alignment::End)
                {
                    childX = contentX + contentWidth - child->width;
                }
                else if (w->crossAlignment == Alignment::Stretch)
                {
                    child->width = contentWidth;
                }

                positionWidget(child.get(), childX, currentY);
                currentY += child->height;

                if (w->mainAxisAlignment == MainAxisAlignment::SpaceBetween && i < w->children.size() - 1)
                {
                    int extraSpace = contentHeight - totalChildHeight;
                    int gap = w->children.size() <= 1 ? 0 : extraSpace / (w->children.size() - 1);
                    currentY += gap;
                }
                else if (w->mainAxisAlignment == MainAxisAlignment::SpaceEvenly && i < w->children.size() - 1)
                {
                    int extraSpace = contentHeight - totalChildHeight;
                    int gap = w->children.empty() ? 0 : extraSpace / (w->children.size() + 1);
                    currentY += gap;
                }
                else if (w->mainAxisAlignment == MainAxisAlignment::SpaceAround && i < w->children.size() - 1)
                {
                    int extraSpace = contentHeight - totalChildHeight;
                    int gap = w->children.empty() ? 0 : extraSpace / w->children.size();
                    currentY += gap;
                }
                else if (i < w->children.size() - 1)
                {
                    currentY += w->spacing;
                }
            }
            break;
        }

        case WidgetType::Row:
        {
            int totalChildWidth = 0;
            for (auto &child : w->children)
            {
                totalChildWidth += child->width;
            }
            totalChildWidth += w->spacing * (w->children.empty() ? 0 : w->children.size() - 1);

            int currentX = contentX;

            if (w->mainAxisAlignment == MainAxisAlignment::Center)
            {
                currentX += (contentWidth - totalChildWidth) / 2;
            }
            else if (w->mainAxisAlignment == MainAxisAlignment::End)
            {
                currentX += contentWidth - totalChildWidth;
            }
            else if (w->mainAxisAlignment == MainAxisAlignment::SpaceAround)
            {
                int extraSpace = contentWidth - totalChildWidth;
                int gap = w->children.empty() ? 0 : extraSpace / w->children.size();
                currentX += gap / 2;
            }
            else if (w->mainAxisAlignment == MainAxisAlignment::SpaceEvenly)
            {
                int extraSpace = contentWidth - totalChildWidth;
                int gap = w->children.empty() ? 0 : extraSpace / (w->children.size() + 1);
                currentX += gap;
            }

            for (size_t i = 0; i < w->children.size(); i++)
            {
                auto &child = w->children[i];
                int childY = contentY;

                if (w->crossAlignment == Alignment::Center)
                {
                    childY = contentY + (contentHeight - child->height) / 2;
                }
                else if (w->crossAlignment == Alignment::End)
                {
                    childY = contentY + contentHeight - child->height;
                }
                else if (w->crossAlignment == Alignment::Stretch)
                {
                    child->height = contentHeight;
                }

                positionWidget(child.get(), currentX, childY);
                currentX += child->width;

                if (w->mainAxisAlignment == MainAxisAlignment::SpaceBetween && i < w->children.size() - 1)
                {
                    int extraSpace = contentWidth - totalChildWidth;
                    int gap = w->children.size() <= 1 ? 0 : extraSpace / (w->children.size() - 1);
                    currentX += gap;
                }
                else if (w->mainAxisAlignment == MainAxisAlignment::SpaceEvenly && i < w->children.size() - 1)
                {
                    int extraSpace = contentWidth - totalChildWidth;
                    int gap = w->children.empty() ? 0 : extraSpace / (w->children.size() + 1);
                    currentX += gap;
                }
                else if (w->mainAxisAlignment == MainAxisAlignment::SpaceAround && i < w->children.size() - 1)
                {
                    int extraSpace = contentWidth - totalChildWidth;
                    int gap = w->children.empty() ? 0 : extraSpace / w->children.size();
                    currentX += gap;
                }
                else if (i < w->children.size() - 1)
                {
                    currentX += w->spacing;
                }
            }
            break;
        }

        case WidgetType::Center:
        {
            if (!w->children.empty())
            {
                auto child = w->children[0].get();
                int childX = contentX + (contentWidth - child->width) / 2;
                int childY = contentY + (contentHeight - child->height) / 2;
                positionWidget(child, childX, childY);
            }
            break;
        }

        default:
        {
            for (auto &child : w->children)
            {
                positionWidget(child.get(), contentX, contentY);
            }
            break;
        }
        }
    }
};

// ============================================================================
// RENDERER
// ============================================================================

class Renderer
{
public:
    static void drawRoundedRectangle(HDC hdc, int x, int y, int width, int height, int radius,
                                     COLORREF fillColor, COLORREF borderColor, int borderWidth, bool hasBorder)
    {
        if (radius > 0)
        {
            HPEN pen = hasBorder ? CreatePen(PS_SOLID, borderWidth, borderColor) : CreatePen(PS_NULL, 0, 0);
            HBRUSH brush = CreateSolidBrush(fillColor);

            HPEN oldPen = (HPEN)SelectObject(hdc, pen);
            HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, brush);

            RoundRect(hdc, x, y, x + width, y + height, radius * 2, radius * 2);

            SelectObject(hdc, oldBrush);
            SelectObject(hdc, oldPen);
            DeleteObject(brush);
            DeleteObject(pen);
        }
        else
        {
            HBRUSH brush = CreateSolidBrush(fillColor);
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

    static void renderWidget(HDC hdc, Widget *w, FontCache &fontCache)
    {
        if (!w)
            return;

        if (w->hasBackground)
        {
            drawRoundedRectangle(hdc, w->x, w->y, w->width, w->height, w->borderRadius,
                                 w->backgroundColor, w->borderColor, w->borderWidth, w->hasBorder);
        }

        if ((w->type == WidgetType::Text || w->type == WidgetType::Button) && !w->text.empty())
        {
            SetTextColor(hdc, w->textColor);
            SetBkMode(hdc, TRANSPARENT);

            HFONT hFont = fontCache.getFont(w->fontSize, w->fontWeight);
            HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);

            RECT textRect = {
                w->x + w->paddingLeft,
                w->y + w->paddingTop,
                w->x + w->width - w->paddingRight,
                w->y + w->height - w->paddingBottom};

            UINT format = DT_LEFT | DT_VCENTER | DT_SINGLELINE;
            if (w->type == WidgetType::Button)
            {
                format = DT_CENTER | DT_VCENTER | DT_SINGLELINE;
            }

            DrawText(hdc, w->text.c_str(), -1, &textRect, format);

            SelectObject(hdc, hOldFont);
        }

        for (auto &child : w->children)
        {
            renderWidget(hdc, child.get(), fontCache);
        }

        w->needsPaint = false;
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
// FLUTTERUI CLASS
// ============================================================================

class FlutterUI
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
        FlutterUI *instance = reinterpret_cast<FlutterUI *>(GetWindowLongPtr(hwnd, GWLP_USERDATA));

        switch (uMsg)
        {
        case WM_CREATE:
        {
            CREATESTRUCT *pCreate = reinterpret_cast<CREATESTRUCT *>(lParam);
            instance = reinterpret_cast<FlutterUI *>(pCreate->lpCreateParams);
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
    FlutterUI(HINSTANCE hInst) : hInstance(hInst) {}

    ~FlutterUI()
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

    // ✅ NEW: Smart selective update - only updates specific widget
    void updateWidget(Widget *widget)
    {
        if (!widget || !hwnd)
            return;

        // Store old dimensions
        int oldWidth = widget->width;
        int oldHeight = widget->height;

        // Remeasure the widget
        HDC hdc = GetDC(hwnd);
        LayoutEngine::measureText(hdc, widget, fontCache);
        widget->width += widget->paddingLeft + widget->paddingRight;
        widget->height += widget->paddingTop + widget->paddingBottom;
        ReleaseDC(hwnd, hdc);

        // Check if size changed
        bool sizeChanged = (oldWidth != widget->width || oldHeight != widget->height);

        if (sizeChanged)
        {
            // Size changed - need to recalculate parent layouts
            partialRebuild(widget);
        }
        else
        {
            // Same size - just repaint this widget
            invalidateWidget(widget);
        }
    }

    // ✅ NEW: Invalidate only a specific widget (for same-size updates)
    void invalidateWidget(Widget *widget)
    {
        if (!widget || !hwnd)
            return;

        // Only invalidate this widget's rectangle
        RECT rect = {
            widget->x,
            widget->y,
            widget->x + widget->width,
            widget->y + widget->height};

        InvalidateRect(hwnd, &rect, FALSE);
    }

    // ✅ NEW: Partial rebuild (widget + parents only)
    void partialRebuild(Widget *widget)
    {
        if (!widget || !hwnd)
            return;

        RECT rect;
        GetClientRect(hwnd, &rect);
        int width = rect.right - rect.left;
        int height = rect.bottom - rect.top;

        // Recalculate from this widget up to root
        Widget *current = widget;
        while (current)
        {
            current->markNeedsLayout();
            current = current->parent;
        }

        // Recalculate entire tree (parents affect children)
        HDC hdc = GetDC(hwnd);
        LayoutEngine::computeLayout(hdc, root.get(), width, height, fontCache);
        LayoutEngine::positionWidget(root.get(), 0, 0);
        ReleaseDC(hwnd, hdc);

        // Invalidate entire window (since parent sizes might have changed)
        InvalidateRect(hwnd, NULL, FALSE);
    }

    HWND createWindow(const std::string &title, int width, int height)
    {
        WNDCLASSEX wc = {0};
        wc.cbSize = sizeof(WNDCLASSEX);
        wc.lpfnWndProc = WindowProc;
        wc.hInstance = hInstance;
        wc.lpszClassName = "FlutterUI";
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.style = CS_HREDRAW | CS_VREDRAW;

        RegisterClassEx(&wc);

        RECT windowRect = {0, 0, width, height};
        AdjustWindowRectEx(&windowRect, WS_OVERLAPPEDWINDOW, FALSE, 0);

        hwnd = CreateWindowEx(
            0,
            "FlutterUI",
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

// In FlutterUI.hpp - Update State class:

template <typename T>
class State
{
private:
    T value;
    FlutterUI *ui;
    std::vector<std::weak_ptr<Widget>> observers; // ← Changed from raw pointers!

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
    State(T initial, FlutterUI *app = nullptr) : value(initial), ui(app) {}

    T get() const { return value; }

    void set(T newValue)
    {
        if (value == newValue)
            return;

        value = newValue;
        std::string newText = valueToString(value);

        // Remove expired observers (cleanup)
        observers.erase(
            std::remove_if(observers.begin(), observers.end(),
                           [](const std::weak_ptr<Widget> &w)
                           { return w.expired(); }),
            observers.end());

        // Update all live observers
        for (auto &weakWidget : observers)
        {
            if (auto widget = weakWidget.lock())
            {
                widget->text = newText;

                if (ui)
                {
                    ui->updateWidget(widget.get()); // Smart update!
                }
            }
        }
    }

    // ✅ Changed to accept shared_ptr
    void addObserver(std::shared_ptr<Widget> widget)
    {
        if (widget)
        {
            observers.push_back(widget); // Store as weak_ptr
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
// WIDGET FACTORY FUNCTIONS
// ============================================================================

inline WidgetPtr Container(WidgetPtr child = nullptr)
{
    auto w = std::make_shared<Widget>(WidgetType::Container);
    if (child)
        w->addChild(child);
    return w;
}

inline WidgetPtr Text(const std::string &text)
{
    auto w = std::make_shared<Widget>(WidgetType::Text);
    w->text = text;
    return w;
}

// Helper functions for state to string conversion
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
    auto w = std::make_shared<Widget>(WidgetType::Text);

    // Convert state value to string
    w->text = convertStateToString(state);

    // ✅ Pass shared_ptr instead of raw pointer!
    state.addObserver(w); // Now accepts shared_ptr

    return w;
}

inline WidgetPtr Button(const std::string &text, ClickHandler onClick = nullptr)
{
    auto w = std::make_shared<Widget>(WidgetType::Button);
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
    auto w = std::make_shared<Widget>(WidgetType::Row);
    (w->addChild(widgets), ...);
    return w;
}

template <typename... Widgets>
WidgetPtr Column(Widgets... widgets)
{
    auto w = std::make_shared<Widget>(WidgetType::Column);
    (w->addChild(widgets), ...);
    return w;
}

inline WidgetPtr Padding(int padding, WidgetPtr child)
{
    auto w = std::make_shared<Widget>(WidgetType::Padding);
    w->padding = padding;
    w->paddingLeft = w->paddingRight = w->paddingTop = w->paddingBottom = padding;
    if (child)
        w->addChild(child);
    return w;
}

inline WidgetPtr Center(WidgetPtr child)
{
    auto w = std::make_shared<Widget>(WidgetType::Center);
    w->alignment = Alignment::Center;
    if (child)
        w->addChild(child);
    return w;
}

inline WidgetPtr SizedBox(int width, int height, WidgetPtr child = nullptr)
{
    auto w = std::make_shared<Widget>(WidgetType::SizedBox);
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
    auto w = std::make_shared<Widget>(WidgetType::Card);
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
    auto w = std::make_shared<Widget>(WidgetType::Divider);
    w->height = 1;
    w->autoHeight = false;
    w->hasBackground = true;
    w->backgroundColor = RGB(224, 224, 224);
    return w;
}

inline WidgetPtr Expanded(WidgetPtr child, int flex = 1)
{
    auto w = std::make_shared<Widget>(WidgetType::Expanded);
    w->flex = flex;
    if (child)
        w->addChild(child);
    return w;
}

#endif // FLUTTERUI_HPP