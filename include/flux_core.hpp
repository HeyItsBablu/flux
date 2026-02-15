#ifndef FLUX_CORE_HPP
#define FLUX_CORE_HPP

#include "flux_widget.hpp"
#include "flux_font.hpp"
#include "flux_layout.hpp"
#include "flux_renderer.hpp"

#include <map> 
#include <tuple>
#include <windowsx.h>

// ============================================================================
// FORWARD DECLARATIONS
// ============================================================================

// Forward declarations
template <typename T>
class State;

// ============================================================================
// MOUSE EVENT BROADCAST HELPERS (for captured mouse events)
// ============================================================================

// Broadcast mouse event to all widgets (used when mouse capture is active)
inline bool broadcastMouseEvent(Widget *widget, int x, int y,
                                std::function<bool(Widget *, int, int)> handler)
{
    if (!widget)
        return false;

    // Try this widget first
    if (handler(widget, x, y))
        return true;

    // Then try all children
    for (auto &child : widget->children)
    {
        if (broadcastMouseEvent(child.get(), x, y, handler))
            return true;
    }

    return false;
}

// ============================================================================
// FLUXUI CLASS
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

    // Global instance for State to use
    static FluxUI *currentInstance;

    Widget *focusedWidget = nullptr;

    // Broadcast keyboard event to focused widget only
    static bool broadcastToFocused(Widget *focused, std::function<bool(Widget *)> handler)
    {
        if (!focused)
            return false;
        return handler(focused);
    }

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

        case WM_MOUSEWHEEL:
        {
            if (!instance || !instance->root)
                return 0;

            int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            int x = GET_X_LPARAM(lParam);
            int y = GET_Y_LPARAM(lParam);

            // Convert screen coordinates to client coordinates
            POINT pt = {x, y};
            ScreenToClient(hwnd, &pt);

            if (findAndHandleMouseEvent(instance->root.get(), pt.x, pt.y,
                                        [delta](Widget *w)
                                        { return w->handleMouseWheel(delta); }))
            {
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

            // Try new mouse event system first
            if (findAndHandleMouseEvent(instance->root.get(), mouseX, mouseY,
                                        [mouseX, mouseY, instance](Widget *w)
                                        {
                                            bool handled = w->handleMouseDown(mouseX, mouseY);
                                            if (handled && w->isFocusable)
                                                instance->setFocus(w); // ← focus on click
                                            return handled;
                                        }))
            {
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }

            // Clicked outside any focusable widget - clear focus
            instance->setFocus(nullptr);

            // Fall back to old onClick system
            Widget *clicked = findWidgetAt(instance->root.get(), mouseX, mouseY);
            if (clicked && clicked->onClick)
            {
                clicked->onClick();
            }
            return 0;
        }

        case WM_LBUTTONUP:
        {
            if (!instance || !instance->root)
                return 0;

            int mouseX = LOWORD(lParam);
            int mouseY = HIWORD(lParam);

            // ✅ FIX: Check if mouse is captured
            bool hasCapturedMouse = (GetCapture() == hwnd);

            if (hasCapturedMouse)
            {
                // Mouse is captured - broadcast to ALL widgets
                // (one of them has the capture and needs this event)
                if (broadcastMouseEvent(instance->root.get(), mouseX, mouseY,
                                        [](Widget *w, int mx, int my)
                                        { return w->handleMouseUp(mx, my); }))
                {
                    InvalidateRect(hwnd, NULL, FALSE);
                    return 0;
                }
            }

            // Normal path (no capture) - use bounds checking
            if (findAndHandleMouseEvent(instance->root.get(), mouseX, mouseY,
                                        [mouseX, mouseY](Widget *w)
                                        { return w->handleMouseUp(mouseX, mouseY); }))
            {
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        }

        case WM_MOUSEMOVE:
        {
            if (!instance || !instance->root)
                return 0;

            int mouseX = LOWORD(lParam);
            int mouseY = HIWORD(lParam);

            // Track mouse for leave detection
            TRACKMOUSEEVENT tme = {0};
            tme.cbSize = sizeof(TRACKMOUSEEVENT);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hwnd;
            TrackMouseEvent(&tme);

            // ✅ FIX: Check if mouse is captured
            bool hasCapturedMouse = (GetCapture() == hwnd);

            if (hasCapturedMouse)
            {
                // Mouse is captured - broadcast to ALL widgets
                if (broadcastMouseEvent(instance->root.get(), mouseX, mouseY,
                                        [](Widget *w, int mx, int my)
                                        { return w->handleMouseMove(mx, my); }))
                {
                    InvalidateRect(hwnd, NULL, FALSE);
                    return 0;
                }
            }

            // Normal path (no capture) - update hover states and custom handlers
            bool hoverChanged = updateHoverStates(instance->root.get(), mouseX, mouseY);

            // Handle custom mouse move events
            bool customHandled = findAndHandleMouseEvent(instance->root.get(), mouseX, mouseY,
                                                         [mouseX, mouseY](Widget *w)
                                                         { return w->handleMouseMove(mouseX, mouseY); });

            if (hoverChanged || customHandled)
            {
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        }

        case WM_MOUSELEAVE:
        {
            if (!instance || !instance->root)
                return 0;

            // Clear all hover states when mouse leaves window
            instance->root->clearHoverState();
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }

        case WM_CHAR:
        {
            if (!instance || !instance->focusedWidget)
                return 0;

            wchar_t ch = (wchar_t)wParam;

            if (instance->focusedWidget->handleChar(ch))
            {
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        }

        case WM_KEYDOWN:
        {
            if (!instance || !instance->focusedWidget)
                return 0;

            int keyCode = (int)wParam;

            if (instance->focusedWidget->handleKeyDown(keyCode))
            {
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        }

        case WM_TIMER:
        {
            // Cursor blink timer for focused text input
            if (!instance || !instance->focusedWidget)
                return 0;

            if (instance->focusedWidget->handleTimer((UINT)wParam))
            {
                instance->invalidateWidget(instance->focusedWidget);
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
    template <typename T>
    State<T> useState(T initialValue)
    {
        return State<T>(initialValue, this);
    }

    // Get the current FluxUI instance (for State to use)
    static FluxUI *getCurrentInstance()
    {
        return currentInstance;
    }

    void setFocus(Widget *widget)
    {
        if (focusedWidget == widget)
            return;

        // Blur old focused widget
        if (focusedWidget)
        {
            focusedWidget->handleFocus(false);
            invalidateWidget(focusedWidget);
        }

        focusedWidget = widget;

        // Focus new widget
        if (focusedWidget)
        {
            focusedWidget->handleFocus(true);
            invalidateWidget(focusedWidget);
        }
    }

    Widget *getFocusedWidget() const { return focusedWidget; }

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
};

// Define static member
inline FluxUI *FluxUI::currentInstance = nullptr;

#endif // FLUX_CORE_HPP