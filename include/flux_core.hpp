#ifndef FLUX_CORE_HPP
#define FLUX_CORE_HPP

#include <windows.h>
#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <map>
#include <tuple>
#include <algorithm>
#include <type_traits>
#include <iostream>

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
// WIDGET BASE CLASS (ABSTRACT WITH VIRTUAL METHODS)
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
// DEFAULT IMPLEMENTATIONS FOR BASE WIDGET
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
// LAYOUT ENGINE
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
// RENDERER
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
// FLUXUI CLASS
// ============================================================================

// Forward declare static member
class FluxUI;

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

    // Global instance for State to use
    static FluxUI* currentInstance;

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
    FluxUI(HINSTANCE hInst) : hInstance(hInst) 
    {
        currentInstance = this;
    }

    ~FluxUI()
    {
        destroyBackBuffer();
        fontCache.clear();
        if (currentInstance == this)
            currentInstance = nullptr;
    }

    // Create a state bound to this FluxUI instance
    template<typename T>
    State<T> useState(T initialValue)
    {
        return State<T>(initialValue, this);
    }

    // Get the current FluxUI instance (for State to use)
    static FluxUI* getCurrentInstance()
    {
        return currentInstance;
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

// Define static member
FluxUI* FluxUI::currentInstance = nullptr;

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
    // Constructor with explicit app (for class members)
    State(T initial, FluxUI *app) : value(initial), ui(app) {}
    
    // Constructor without app (auto-detect from global instance)
    State(T initial) : value(initial), ui(nullptr) 
    {
        ui = FluxUI::getCurrentInstance();
    }

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

#endif // FLUX_CORE_HPP