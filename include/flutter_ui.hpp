#ifndef FLUTTERUI_HPP
#define FLUTTERUI_HPP

#include <windows.h>
#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <cstdio>

// ============================================================================
// ENUMS
// ============================================================================

enum class WidgetType {
    Container,
    Text,
    Button,
    Row,
    Column,
    Padding,
    Center,
    SizedBox,
    Card,
    Divider
};

enum class Alignment {
    Start,
    Center,
    End,
    Stretch
};

enum class FontWeight {
    Normal = FW_NORMAL,
    Bold = FW_BOLD,
    Light = FW_LIGHT
};

// ============================================================================
// FORWARD DECLARATIONS
// ============================================================================

class Widget;
class FlutterUI;

using ClickHandler = std::function<void()>;
using WidgetPtr = std::shared_ptr<Widget>;

// ============================================================================
// WIDGET CLASS
// ============================================================================

class Widget : public std::enable_shared_from_this<Widget> {
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
    
    // Spacing
    int padding = 0;
    int paddingLeft = 0, paddingRight = 0, paddingTop = 0, paddingBottom = 0;
    int margin = 0;
    int marginLeft = 0, marginRight = 0, marginTop = 0, marginBottom = 0;
    
    // Alignment
    Alignment alignment = Alignment::Start;
    Alignment crossAlignment = Alignment::Start;
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
    
    // Children
    std::vector<WidgetPtr> children;
    Widget* parent = nullptr;
    
    // Constructor
    explicit Widget(WidgetType t) : type(t) {}
    
    // Builder pattern methods (returns shared_ptr for chaining)
    WidgetPtr setWidth(int w) {
        width = w;
        autoWidth = false;
        return shared_from_this();
    }
    
    WidgetPtr setHeight(int h) {
        height = h;
        autoHeight = false;
        return shared_from_this();
    }
    
    WidgetPtr setMinWidth(int w) {
        minWidth = w;
        return shared_from_this();
    }
    
    WidgetPtr setMinHeight(int h) {
        minHeight = h;
        return shared_from_this();
    }
    
    WidgetPtr setMaxWidth(int w) {
        maxWidth = w;
        return shared_from_this();
    }
    
    WidgetPtr setMaxHeight(int h) {
        maxHeight = h;
        return shared_from_this();
    }
    
    WidgetPtr setPadding(int p) {
        padding = p;
        paddingLeft = paddingRight = paddingTop = paddingBottom = p;
        return shared_from_this();
    }
    
    WidgetPtr setPaddingAll(int left, int top, int right, int bottom) {
        paddingLeft = left;
        paddingTop = top;
        paddingRight = right;
        paddingBottom = bottom;
        padding = -1;
        return shared_from_this();
    }
    
    WidgetPtr setMargin(int m) {
        margin = m;
        marginLeft = marginRight = marginTop = marginBottom = m;
        return shared_from_this();
    }
    
    WidgetPtr setMarginAll(int left, int top, int right, int bottom) {
        marginLeft = left;
        marginTop = top;
        marginRight = right;
        marginBottom = bottom;
        margin = -1;
        return shared_from_this();
    }
    
    WidgetPtr setBackgroundColor(COLORREF color) {
        backgroundColor = color;
        hasBackground = true;
        return shared_from_this();
    }
    
    WidgetPtr setTextColor(COLORREF color) {
        textColor = color;
        return shared_from_this();
    }
    
    WidgetPtr setBorderColor(COLORREF color) {
        borderColor = color;
        hasBorder = true;
        return shared_from_this();
    }
    
    WidgetPtr setBorderWidth(int w) {
        borderWidth = w;
        hasBorder = true;
        return shared_from_this();
    }
    
    WidgetPtr setBorderRadius(int r) {
        borderRadius = r;
        return shared_from_this();
    }
    
    WidgetPtr setFontSize(int size) {
        fontSize = size;
        return shared_from_this();
    }
    
    WidgetPtr setFontWeight(FontWeight weight) {
        fontWeight = weight;
        return shared_from_this();
    }
    
    WidgetPtr setOnClick(ClickHandler handler) {
        onClick = handler;
        return shared_from_this();
    }
    
    WidgetPtr setAlignment(Alignment align) {
        alignment = align;
        return shared_from_this();
    }
    
    WidgetPtr setCrossAlignment(Alignment align) {
        crossAlignment = align;
        return shared_from_this();
    }
    
    WidgetPtr setSpacing(int s) {
        spacing = s;
        return shared_from_this();
    }
    
    WidgetPtr setId(const std::string& i) {
        id = i;
        return shared_from_this();
    }
    
    void addChild(WidgetPtr child) {
        children.push_back(child);
        child->parent = this;
    }
};

// ============================================================================
// WIDGET FACTORY FUNCTIONS
// ============================================================================

inline WidgetPtr Container(WidgetPtr child = nullptr) {
    auto w = std::make_shared<Widget>(WidgetType::Container);
    if (child) w->addChild(child);
    return w;
}

inline WidgetPtr Text(const std::string& format, ...) {
    auto w = std::make_shared<Widget>(WidgetType::Text);
    
    char buffer[512];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format.c_str(), args);
    va_end(args);
    
    w->text = buffer;
    return w;
}

inline WidgetPtr Button(const std::string& text, ClickHandler onClick = nullptr) {
    auto w = std::make_shared<Widget>(WidgetType::Button);
    w->text = text;
    w->onClick = onClick;
    
    // Default button styling
    w->hasBackground = true;
    w->backgroundColor = RGB(76, 175, 80);
    w->textColor = RGB(255, 255, 255);
    w->paddingLeft = w->paddingRight = 20;
    w->paddingTop = w->paddingBottom = 10;
    w->borderRadius = 4;
    w->fontWeight = FontWeight::Bold;
    
    return w;
}

template<typename... Widgets>
WidgetPtr Row(Widgets... widgets) {
    auto w = std::make_shared<Widget>(WidgetType::Row);
    (w->addChild(widgets), ...);
    return w;
}

template<typename... Widgets>
WidgetPtr Column(Widgets... widgets) {
    auto w = std::make_shared<Widget>(WidgetType::Column);
    (w->addChild(widgets), ...);
    return w;
}

inline WidgetPtr Padding(int padding, WidgetPtr child) {
    auto w = std::make_shared<Widget>(WidgetType::Padding);
    w->padding = padding;
    w->paddingLeft = w->paddingRight = w->paddingTop = w->paddingBottom = padding;
    if (child) w->addChild(child);
    return w;
}

inline WidgetPtr Center(WidgetPtr child) {
    auto w = std::make_shared<Widget>(WidgetType::Center);
    w->alignment = Alignment::Center;
    if (child) w->addChild(child);
    return w;
}

inline WidgetPtr SizedBox(int width, int height, WidgetPtr child = nullptr) {
    auto w = std::make_shared<Widget>(WidgetType::SizedBox);
    w->width = width;
    w->height = height;
    w->autoWidth = false;
    w->autoHeight = false;
    if (child) w->addChild(child);
    return w;
}

inline WidgetPtr Card(WidgetPtr child) {
    auto w = std::make_shared<Widget>(WidgetType::Card);
    w->hasBackground = true;
    w->backgroundColor = RGB(255, 255, 255);
    w->hasBorder = true;
    w->borderColor = RGB(224, 224, 224);
    w->borderWidth = 1;
    w->borderRadius = 8;
    w->paddingLeft = w->paddingRight = w->paddingTop = w->paddingBottom = 16;
    if (child) w->addChild(child);
    return w;
}

inline WidgetPtr Divider() {
    auto w = std::make_shared<Widget>(WidgetType::Divider);
    w->height = 1;
    w->autoHeight = false;
    w->hasBackground = true;
    w->backgroundColor = RGB(224, 224, 224);
    return w;
}

// ============================================================================
// LAYOUT ENGINE
// ============================================================================

class LayoutEngine {
public:
    static void measureText(HDC hdc, Widget* w) {
        if (w->text.empty()) {
            w->width = 0;
            w->height = 0;
            return;
        }
        
        HFONT hFont = CreateFont(
            w->fontSize, 0, 0, 0, static_cast<int>(w->fontWeight),
            FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE, "Arial"
        );
        
        HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);
        
        SIZE size;
        GetTextExtentPoint32(hdc, w->text.c_str(), (int)w->text.length(), &size);
        
        if (w->autoWidth) w->width = size.cx;
        if (w->autoHeight) w->height = size.cy;
        
        SelectObject(hdc, hOldFont);
        DeleteObject(hFont);
    }
    
    static void computeLayout(HDC hdc, Widget* w, int availableWidth, int availableHeight) {
        if (!w) return;
        
        int contentWidth = availableWidth - w->paddingLeft - w->paddingRight;
        int contentHeight = availableHeight - w->paddingTop - w->paddingBottom;
        
        switch (w->type) {
            case WidgetType::Text:
            case WidgetType::Button: {
                measureText(hdc, w);
                w->width += w->paddingLeft + w->paddingRight;
                w->height += w->paddingTop + w->paddingBottom;
                break;
            }
            
            case WidgetType::Column: {
                int totalHeight = 0;
                int maxWidth = 0;
                
                for (size_t i = 0; i < w->children.size(); i++) {
                    auto child = w->children[i].get();
                    computeLayout(hdc, child, contentWidth, contentHeight);
                    
                    if (child->width > maxWidth) maxWidth = child->width;
                    totalHeight += child->height;
                    if (i < w->children.size() - 1) totalHeight += w->spacing;
                }
                
                if (w->autoWidth) w->width = maxWidth + w->paddingLeft + w->paddingRight;
                if (w->autoHeight) w->height = totalHeight + w->paddingTop + w->paddingBottom;
                break;
            }
            
            case WidgetType::Row: {
                int totalWidth = 0;
                int maxHeight = 0;
                
                for (size_t i = 0; i < w->children.size(); i++) {
                    auto child = w->children[i].get();
                    computeLayout(hdc, child, contentWidth, contentHeight);
                    
                    totalWidth += child->width;
                    if (child->height > maxHeight) maxHeight = child->height;
                    if (i < w->children.size() - 1) totalWidth += w->spacing;
                }
                
                if (w->autoWidth) w->width = totalWidth + w->paddingLeft + w->paddingRight;
                if (w->autoHeight) w->height = maxHeight + w->paddingTop + w->paddingBottom;
                break;
            }
            
            case WidgetType::Container:
            case WidgetType::Padding:
            case WidgetType::Center:
            case WidgetType::Card: {
                if (!w->children.empty()) {
                    computeLayout(hdc, w->children[0].get(), contentWidth, contentHeight);
                    if (w->autoWidth) w->width = w->children[0]->width + w->paddingLeft + w->paddingRight;
                    if (w->autoHeight) w->height = w->children[0]->height + w->paddingTop + w->paddingBottom;
                }
                break;
            }
            
            case WidgetType::SizedBox: {
                if (!w->children.empty()) {
                    computeLayout(hdc, w->children[0].get(), 
                                w->width - w->paddingLeft - w->paddingRight,
                                w->height - w->paddingTop - w->paddingBottom);
                }
                break;
            }
            
            case WidgetType::Divider: {
                if (w->autoWidth) w->width = availableWidth;
                break;
            }
        }
        
        // Apply constraints
        if (w->width < w->minWidth) w->width = w->minWidth;
        if (w->height < w->minHeight) w->height = w->minHeight;
        if (w->width > w->maxWidth) w->width = w->maxWidth;
        if (w->height > w->maxHeight) w->height = w->maxHeight;
    }
    
    static void positionWidget(Widget* w, int x, int y) {
        if (!w) return;
        
        w->x = x + w->marginLeft;
        w->y = y + w->marginTop;
        
        int contentX = w->x + w->paddingLeft;
        int contentY = w->y + w->paddingTop;
        int contentWidth = w->width - w->paddingLeft - w->paddingRight;
        int contentHeight = w->height - w->paddingTop - w->paddingBottom;
        
        switch (w->type) {
            case WidgetType::Column: {
                int currentY = contentY;
                
                for (auto& child : w->children) {
                    int childX = contentX;
                    
                    if (w->crossAlignment == Alignment::Center) {
                        childX = contentX + (contentWidth - child->width) / 2;
                    } else if (w->crossAlignment == Alignment::End) {
                        childX = contentX + contentWidth - child->width;
                    } else if (w->crossAlignment == Alignment::Stretch) {
                        child->width = contentWidth;
                    }
                    
                    positionWidget(child.get(), childX, currentY);
                    currentY += child->height + w->spacing;
                }
                break;
            }
            
            case WidgetType::Row: {
                int currentX = contentX;
                
                for (auto& child : w->children) {
                    int childY = contentY;
                    
                    if (w->crossAlignment == Alignment::Center) {
                        childY = contentY + (contentHeight - child->height) / 2;
                    } else if (w->crossAlignment == Alignment::End) {
                        childY = contentY + contentHeight - child->height;
                    } else if (w->crossAlignment == Alignment::Stretch) {
                        child->height = contentHeight;
                    }
                    
                    positionWidget(child.get(), currentX, childY);
                    currentX += child->width + w->spacing;
                }
                break;
            }
            
            case WidgetType::Center: {
                if (!w->children.empty()) {
                    auto child = w->children[0].get();
                    int childX = contentX + (contentWidth - child->width) / 2;
                    int childY = contentY + (contentHeight - child->height) / 2;
                    positionWidget(child, childX, childY);
                }
                break;
            }
            
            default: {
                for (auto& child : w->children) {
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

class Renderer {
public:
    static void drawRoundedRectangle(HDC hdc, int x, int y, int width, int height, int radius, 
                                     COLORREF fillColor, COLORREF borderColor, int borderWidth, bool hasBorder) {
        if (radius > 0) {
            HPEN pen = hasBorder ? CreatePen(PS_SOLID, borderWidth, borderColor) : CreatePen(PS_NULL, 0, 0);
            HBRUSH brush = CreateSolidBrush(fillColor);
            
            HPEN oldPen = (HPEN)SelectObject(hdc, pen);
            HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, brush);
            
            RoundRect(hdc, x, y, x + width, y + height, radius * 2, radius * 2);
            
            SelectObject(hdc, oldBrush);
            SelectObject(hdc, oldPen);
            DeleteObject(brush);
            DeleteObject(pen);
        } else {
            HBRUSH brush = CreateSolidBrush(fillColor);
            RECT rect = {x, y, x + width, y + height};
            FillRect(hdc, &rect, brush);
            DeleteObject(brush);
            
            if (hasBorder) {
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
    
    static void renderWidget(HDC hdc, Widget* w) {
        if (!w) return;
        
        // Draw background
        if (w->hasBackground) {
            drawRoundedRectangle(hdc, w->x, w->y, w->width, w->height, w->borderRadius,
                               w->backgroundColor, w->borderColor, w->borderWidth, w->hasBorder);
        }
        
        // Draw text
        if ((w->type == WidgetType::Text || w->type == WidgetType::Button) && !w->text.empty()) {
            SetTextColor(hdc, w->textColor);
            SetBkMode(hdc, TRANSPARENT);
            
            HFONT hFont = CreateFont(
                w->fontSize, 0, 0, 0, static_cast<int>(w->fontWeight),
                FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                DEFAULT_PITCH | FF_DONTCARE, "Arial"
            );
            HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);
            
            RECT textRect = {
                w->x + w->paddingLeft,
                w->y + w->paddingTop,
                w->x + w->width - w->paddingRight,
                w->y + w->height - w->paddingBottom
            };
            
            UINT format = DT_LEFT | DT_VCENTER | DT_SINGLELINE;
            if (w->type == WidgetType::Button) {
                format = DT_CENTER | DT_VCENTER | DT_SINGLELINE;
            }
            
            DrawText(hdc, w->text.c_str(), -1, &textRect, format);
            
            SelectObject(hdc, hOldFont);
            DeleteObject(hFont);
        }
        
        // Render children
        for (auto& child : w->children) {
            renderWidget(hdc, child.get());
        }
    }
};

// ============================================================================
// HIT TESTING
// ============================================================================

inline Widget* findWidgetAt(Widget* w, int x, int y) {
    if (!w) return nullptr;
    
    // Check children first
    for (auto& child : w->children) {
        Widget* found = findWidgetAt(child.get(), x, y);
        if (found) return found;
    }
    
    // Check this widget
    if (x >= w->x && x < w->x + w->width &&
        y >= w->y && y < w->y + w->height) {
        return w;
    }
    
    return nullptr;
}

// ============================================================================
// FLUTTERUI CLASS
// ============================================================================

class FlutterUI {
private:
    WidgetPtr root;
    std::function<WidgetPtr()> builder;
    HWND hwnd = nullptr;
    HINSTANCE hInstance;
    
    static FlutterUI* instance;
    
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
        switch (uMsg) {
            case WM_PAINT: {
                PAINTSTRUCT ps;
                HDC hdc = BeginPaint(hwnd, &ps);
                
                RECT clientRect;
                GetClientRect(hwnd, &clientRect);
                HBRUSH bgBrush = CreateSolidBrush(RGB(250, 250, 250));
                FillRect(hdc, &clientRect, bgBrush);
                DeleteObject(bgBrush);
                
                if (instance && instance->root) {
                    Renderer::renderWidget(hdc, instance->root.get());
                }
                
                EndPaint(hwnd, &ps);
                return 0;
            }
            
            case WM_LBUTTONDOWN: {
                int mouseX = LOWORD(lParam);
                int mouseY = HIWORD(lParam);
                
                if (instance && instance->root) {
                    Widget* clicked = findWidgetAt(instance->root.get(), mouseX, mouseY);
                    if (clicked && clicked->onClick) {
                        clicked->onClick();
                    }
                }
                return 0;
            }
            
            case WM_DESTROY:
                PostQuitMessage(0);
                return 0;
        }
        
        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
    
public:
    FlutterUI(HINSTANCE hInst) : hInstance(hInst) {
        instance = this;
    }
    
    void build(std::function<WidgetPtr()> buildFunc) {
        builder = buildFunc;
        rebuild();
    }
    
    void rebuild() {
        if (!builder) return;
        
        root = builder();  // Call builder function to get fresh widget tree!
        
        if (hwnd) {
            RECT rect;
            GetClientRect(hwnd, &rect);
            int width = rect.right - rect.left;
            int height = rect.bottom - rect.top;
            
            HDC hdc = GetDC(hwnd);
            LayoutEngine::computeLayout(hdc, root.get(), width, height);
            LayoutEngine::positionWidget(root.get(), 0, 0);
            ReleaseDC(hwnd, hdc);
            
            InvalidateRect(hwnd, NULL, TRUE);
        }
    }
    
    HWND createWindow(const std::string& title, int width, int height) {
        WNDCLASS wc = {0};
        wc.lpfnWndProc = WindowProc;
        wc.hInstance = hInstance;
        wc.lpszClassName = "FlutterUI";
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        
        RegisterClass(&wc);
        
        hwnd = CreateWindowEx(
            0,
            "FlutterUI",
            title.c_str(),
            WS_OVERLAPPEDWINDOW | WS_VISIBLE,
            CW_USEDEFAULT, CW_USEDEFAULT,
            width, height,
            NULL, NULL,
            hInstance,
            NULL
        );
        
        return hwnd;
    }
    
    int run() {
        MSG msg;
        while (GetMessage(&msg, NULL, 0, 0)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        return (int)msg.wParam;
    }
};

FlutterUI* FlutterUI::instance = nullptr;

#endif // FLUTTERUI_HPP