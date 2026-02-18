#ifndef FLUX_CORE_HPP
#define FLUX_CORE_HPP

#include "flux_font.hpp"
#include "flux_layout.hpp"
#include "flux_renderer.hpp"
#include "flux_widget.hpp"

#include <map>
#include <tuple>
#include <windowsx.h>

// ============================================================================
// FORWARD DECLARATIONS
// ============================================================================

template <typename T> class State;
class DropdownWidget;
class FluxAppWidget;
class DialogWidget;

// ============================================================================
// MOUSE EVENT BROADCAST HELPERS (for captured mouse events)
// ============================================================================

inline bool
broadcastMouseEvent(Widget *widget, int x, int y,
                    std::function<bool(Widget *, int, int)> handler) {
  if (!widget)
    return false;

  if (handler(widget, x, y))
    return true;

  for (auto &child : widget->children) {
    if (broadcastMouseEvent(child.get(), x, y, handler))
      return true;
  }

  return false;
}

// ============================================================================
// FLUXUI CLASS
// ============================================================================

class FluxUI {
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

  static FluxUI *currentInstance;

  Widget *focusedWidget = nullptr;

  // ----------------------------------------------------------------
  // Back-buffer management
  // ----------------------------------------------------------------

  void createBackBuffer(int width, int height) {
    if (hdcMem && (width != bufferWidth || height != bufferHeight))
      destroyBackBuffer();

    if (!hdcMem) {
      HDC hdc = GetDC(hwnd);
      hdcMem = CreateCompatibleDC(hdc);
      hbmMem = CreateCompatibleBitmap(hdc, width, height);
      hbmOld = (HBITMAP)SelectObject(hdcMem, hbmMem);
      ReleaseDC(hwnd, hdc);
      bufferWidth = width;
      bufferHeight = height;
    }
  }

  void destroyBackBuffer() {
    if (hdcMem) {
      SelectObject(hdcMem, hbmOld);
      DeleteObject(hbmMem);
      DeleteDC(hdcMem);
      hdcMem = nullptr;
      hbmMem = nullptr;
      hbmOld = nullptr;
    }
  }

  // ----------------------------------------------------------------
  // OVERLAY DISPATCHERS (declarations — defined in flux_core_impl.hpp)
  // ----------------------------------------------------------------

  // Unified mouse-down dispatcher: iterates overlay stack highest-zIndex first.
  bool handleDropdownOverlays(int mouseX, int mouseY);

  // Kept for linker compatibility; implementation is a no-op stub.
  bool handleDialogOverlays(int mouseX, int mouseY);

  // Overlay routing for move, wheel, and keyboard.
  bool handleOverlayMouseMove(int mouseX, int mouseY);
  bool handleOverlayMouseWheel(int delta);
  bool handleOverlayKeyDown(int keyCode);
  
  // Right-click dispatcher for context menus
  bool handleOverlayRightClick(int mouseX, int mouseY);

  // Legacy tree-walk stubs (no-op, kept so flux_core.hpp compiles unchanged).
  bool checkDropdownOverlays(Widget *widget, int mouseX, int mouseY);
  bool checkDialogOverlays(Widget *widget, int mouseX, int mouseY);

  // Wire FluxAppWidget pointer into every overlay-capable widget in the tree.
  void wireFluxAppToWidgets(FluxAppWidget *fluxApp, Widget *widget);

  // ----------------------------------------------------------------
  // WINDOW PROCEDURE
  // ----------------------------------------------------------------

  static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam,
                                     LPARAM lParam) {
    FluxUI *instance =
        reinterpret_cast<FluxUI *>(GetWindowLongPtr(hwnd, GWLP_USERDATA));

    switch (uMsg) {

    // ----------------------------------------------------------------
    case WM_CREATE: {
      CREATESTRUCT *pCreate = reinterpret_cast<CREATESTRUCT *>(lParam);
      instance = reinterpret_cast<FluxUI *>(pCreate->lpCreateParams);
      SetWindowLongPtr(hwnd, GWLP_USERDATA,
                       reinterpret_cast<LONG_PTR>(instance));
      return 0;
    }

    // ----------------------------------------------------------------
    case WM_PAINT: {
      if (!instance || !instance->root) {
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

      Renderer::renderWidget(instance->hdcMem, instance->root.get(),
                             instance->fontCache);

      BitBlt(hdc, 0, 0, width, height, instance->hdcMem, 0, 0, SRCCOPY);

      EndPaint(hwnd, &ps);
      return 0;
    }

    // ----------------------------------------------------------------
    case WM_SIZE: {
      if (instance && instance->root) {
        RECT rect;
        GetClientRect(hwnd, &rect);
        int width = rect.right - rect.left;
        int height = rect.bottom - rect.top;

        HDC hdc = GetDC(hwnd);
        LayoutEngine::computeLayout(hdc, instance->root.get(), width, height,
                                    instance->fontCache);
        LayoutEngine::positionWidget(instance->root.get(), 0, 0);
        ReleaseDC(hwnd, hdc);

        InvalidateRect(hwnd, NULL, FALSE);
      }
      return 0;
    }

    // ----------------------------------------------------------------
    case WM_MOUSEWHEEL: {
      if (!instance || !instance->root)
        return 0;

      int delta = GET_WHEEL_DELTA_WPARAM(wParam);
      int x = GET_X_LPARAM(lParam);
      int y = GET_Y_LPARAM(lParam);

      POINT pt = {x, y};
      ScreenToClient(hwnd, &pt);

      // 1. Overlays get wheel events first (e.g. scrollable dropdown list)
      if (instance->handleOverlayMouseWheel(delta)) {
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
      }

      // 2. Normal widget tree
      if (findAndHandleMouseEvent(
              instance->root.get(), pt.x, pt.y,
              [delta](Widget *w) { return w->handleMouseWheel(delta); })) {
        InvalidateRect(hwnd, NULL, FALSE);
      }
      return 0;
    }

    // ----------------------------------------------------------------
    case WM_LBUTTONDOWN: {
      if (!instance || !instance->root)
        return 0;

      int mouseX = LOWORD(lParam);
      int mouseY = HIWORD(lParam);

      // 1. Unified overlay hit-test (highest zIndex wins)
      if (instance->handleDropdownOverlays(mouseX, mouseY)) {
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
      }

      // 2. Normal widget tree — handleMouseDown
      if (findAndHandleMouseEvent(instance->root.get(), mouseX, mouseY,
                                  [mouseX, mouseY, instance](Widget *w) {
                                    bool handled =
                                        w->handleMouseDown(mouseX, mouseY);
                                    if (handled && w->isFocusable)
                                      instance->setFocus(w);
                                    return handled;
                                  })) {
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
      }

      // 3. Click landed on no interactive widget — clear focus
      instance->setFocus(nullptr);

      // 4. Fallback onClick
      Widget *clicked = findWidgetAt(instance->root.get(), mouseX, mouseY);
      if (clicked && clicked->onClick) {
        clicked->onClick();
        InvalidateRect(hwnd, NULL, FALSE);
      }
      return 0;
    }

    // ----------------------------------------------------------------
    case WM_RBUTTONDOWN: {
      if (!instance || !instance->root)
        return 0;

      int mouseX = LOWORD(lParam);
      int mouseY = HIWORD(lParam);

      // 1. Unified overlay hit-test (highest zIndex wins)
      if (instance->handleOverlayRightClick(mouseX, mouseY)) {
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
      }

      // 2. Normal widget tree — handleRightClick
      if (findAndHandleMouseEvent(instance->root.get(), mouseX, mouseY,
                                  [mouseX, mouseY](Widget *w) {
                                    return w->handleRightClick(mouseX, mouseY);
                                  })) {
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
      }

      return 0;
    }

    // ----------------------------------------------------------------
    case WM_LBUTTONUP: {
      if (!instance || !instance->root)
        return 0;

      int mouseX = LOWORD(lParam);
      int mouseY = HIWORD(lParam);

      bool hasCapturedMouse = (GetCapture() == hwnd);

      if (hasCapturedMouse) {
        if (broadcastMouseEvent(instance->root.get(), mouseX, mouseY,
                                [](Widget *w, int mx, int my) {
                                  return w->handleMouseUp(mx, my);
                                })) {
          InvalidateRect(hwnd, NULL, FALSE);
          return 0;
        }
      }

      if (findAndHandleMouseEvent(instance->root.get(), mouseX, mouseY,
                                  [mouseX, mouseY](Widget *w) {
                                    return w->handleMouseUp(mouseX, mouseY);
                                  })) {
        InvalidateRect(hwnd, NULL, FALSE);
      }
      return 0;
    }

    // ----------------------------------------------------------------
    case WM_MOUSEMOVE: {
      if (!instance || !instance->root)
        return 0;

      int mouseX = LOWORD(lParam);
      int mouseY = HIWORD(lParam);

      TRACKMOUSEEVENT tme = {0};
      tme.cbSize = sizeof(TRACKMOUSEEVENT);
      tme.dwFlags = TME_LEAVE;
      tme.hwndTrack = hwnd;
      TrackMouseEvent(&tme);

      bool hasCapturedMouse = (GetCapture() == hwnd);

      // 1. Captured-mouse broadcast (e.g. drag operations)
      if (hasCapturedMouse) {
        if (broadcastMouseEvent(instance->root.get(), mouseX, mouseY,
                                [](Widget *w, int mx, int my) {
                                  return w->handleMouseMove(mx, my);
                                })) {
          InvalidateRect(hwnd, NULL, FALSE);
          return 0;
        }
      }

      // 2. Overlays get move events (tooltip show/hide, context menu highlight)
      bool overlayHandled = instance->handleOverlayMouseMove(mouseX, mouseY);

      // 3. Normal hover state update + widget tree move
      bool hoverChanged =
          updateHoverStates(instance->root.get(), mouseX, mouseY);

      bool customHandled = findAndHandleMouseEvent(
          instance->root.get(), mouseX, mouseY, [mouseX, mouseY](Widget *w) {
            return w->handleMouseMove(mouseX, mouseY);
          });

      if (overlayHandled || hoverChanged || customHandled) {
        InvalidateRect(hwnd, NULL, FALSE);
      }
      return 0;
    }

    // ----------------------------------------------------------------
    case WM_MOUSELEAVE: {
      if (!instance || !instance->root)
        return 0;

      instance->root->clearHoverState();
      InvalidateRect(hwnd, NULL, FALSE);
      return 0;
    }

    // ----------------------------------------------------------------
    case WM_CHAR: {
      if (!instance || !instance->focusedWidget)
        return 0;

      wchar_t ch = (wchar_t)wParam;

      if (instance->focusedWidget->handleChar(ch)) {
        InvalidateRect(hwnd, NULL, FALSE);
      }
      return 0;
    }

    // ----------------------------------------------------------------
    case WM_KEYDOWN: {
      if (!instance)
        return 0;

      int keyCode = (int)wParam;

      // 1. Topmost overlay gets keyboard first
      if (instance->handleOverlayKeyDown(keyCode)) {
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
      }

      // 2. Focused widget
      if (instance->focusedWidget &&
          instance->focusedWidget->handleKeyDown(keyCode)) {
        InvalidateRect(hwnd, NULL, FALSE);
      }
      return 0;
    }

    // ----------------------------------------------------------------
    case WM_TIMER: {
      if (!instance || !instance->focusedWidget)
        return 0;

      if (instance->focusedWidget->handleTimer((UINT)wParam)) {
        instance->invalidateWidget(instance->focusedWidget);
      }
      return 0;
    }

    // ----------------------------------------------------------------
    case WM_DESTROY:
      if (instance) {
        instance->destroyBackBuffer();
      }
      PostQuitMessage(0);
      return 0;
    }

    return DefWindowProc(hwnd, uMsg, wParam, lParam);
  }

  // ----------------------------------------------------------------
  // findByIdRecursive
  // ----------------------------------------------------------------
  WidgetPtr findByIdRecursive(WidgetPtr widget, const std::string &id) {
    if (!widget)
      return nullptr;

    if (widget->getId() == id)
      return widget;

    for (auto &child : widget->children) {
      auto found = findByIdRecursive(child, id);
      if (found)
        return found;
    }

    return nullptr;
  }

public:
  FluxUI(HINSTANCE hInst) : hInstance(hInst) { currentInstance = this; }

  ~FluxUI() {
    destroyBackBuffer();
    fontCache.clear();
    if (currentInstance == this)
      currentInstance = nullptr;
  }

  template <typename T> State<T> useState(T initialValue) {
    return State<T>(initialValue, this);
  }

  static FluxUI *getCurrentInstance() { return currentInstance; }

  void setFocus(Widget *widget) {
    if (focusedWidget == widget)
      return;

    if (focusedWidget) {
      focusedWidget->handleFocus(false);
      invalidateWidget(focusedWidget);
    }

    focusedWidget = widget;

    if (focusedWidget) {
      focusedWidget->handleFocus(true);
      invalidateWidget(focusedWidget);
    }
  }

  Widget *getFocusedWidget() const { return focusedWidget; }

  void build(std::function<WidgetPtr()> buildFunc) {
    builder = buildFunc;
    rebuild();
  }

  void rebuild(); // Defined in flux_core_impl.hpp

  void updateWidget(Widget *widget) {
    if (!widget || !hwnd)
      return;

    int oldWidth = widget->width;
    int oldHeight = widget->height;

    HDC hdc = GetDC(hwnd);
    widget->measureText(hdc, fontCache);
    widget->width += widget->paddingLeft + widget->paddingRight;
    widget->height += widget->paddingTop + widget->paddingBottom;
    ReleaseDC(hwnd, hdc);

    bool sizeChanged =
        (oldWidth != widget->width || oldHeight != widget->height);

    if (sizeChanged)
      partialRebuild(widget);
    else
      invalidateWidget(widget);
  }

  void invalidateWidget(Widget *widget) {
    if (!widget || !hwnd)
      return;

    RECT rect = {widget->x, widget->y, widget->x + widget->width,
                 widget->y + widget->height};
    InvalidateRect(hwnd, &rect, FALSE);
  }

  void partialRebuild(Widget *widget) {
    if (!widget || !hwnd)
      return;

    RECT rect;
    GetClientRect(hwnd, &rect);
    int width = rect.right - rect.left;
    int height = rect.bottom - rect.top;

    Widget *current = widget;
    while (current) {
      current->markNeedsLayout();
      current = current->parent;
    }

    HDC hdc = GetDC(hwnd);
    LayoutEngine::computeLayout(hdc, root.get(), width, height, fontCache);
    LayoutEngine::positionWidget(root.get(), 0, 0);
    ReleaseDC(hwnd, hdc);

    InvalidateRect(hwnd, NULL, FALSE);
  }

  HWND createWindow(const std::string &title, int width, int height) {
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
        0, "FluxUI", title.c_str(), WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, windowRect.right - windowRect.left,
        windowRect.bottom - windowRect.top, NULL, NULL, hInstance, this);

    if (hwnd && root) {
      RECT rect;
      GetClientRect(hwnd, &rect);
      int clientWidth = rect.right - rect.left;
      int clientHeight = rect.bottom - rect.top;

      HDC hdc = GetDC(hwnd);
      LayoutEngine::computeLayout(hdc, root.get(), clientWidth, clientHeight,
                                  fontCache);
      LayoutEngine::positionWidget(root.get(), 0, 0);
      ReleaseDC(hwnd, hdc);
    }

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

  HWND getWindow() const { return hwnd; }
  WidgetPtr getRoot() const { return root; }

  WidgetPtr findById(const std::string &id) {
    return findByIdRecursive(root, id);
  }

  FontCache &getFontCache() { return fontCache; }
};

inline FluxUI *FluxUI::currentInstance = nullptr;

#endif // FLUX_CORE_HPP