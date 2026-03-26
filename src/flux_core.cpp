#include "flux/flux_core.hpp"
#include "flux/flux_app.hpp"
#include "flux/flux_overlay_host.hpp"

// ============================================================================
// STATIC MEMBER DEFINITION
// ============================================================================

FluxUI *FluxUI::currentInstance = nullptr;

// ============================================================================
// CONSTRUCTION / DESTRUCTION
// ============================================================================

FluxUI::FluxUI(HINSTANCE hInst) : hInstance(hInst) {
  currentInstance = this;
  Gdiplus::GdiplusStartupInput input;
  Gdiplus::GdiplusStartup(&gdiplusToken, &input, NULL);
}

FluxUI::~FluxUI() {

  fontCache.clear();
  Gdiplus::GdiplusShutdown(gdiplusToken);
  if (currentInstance == this)
    currentInstance = nullptr;
}

// ============================================================================
// STATIC ACCESSOR
// ============================================================================

FluxUI *FluxUI::getCurrentInstance() { return currentInstance; }



// ============================================================================
// PRIVATE HELPERS
// ============================================================================

Widget *FluxUI::findLayoutBoundary(Widget *widget) {
  Widget *boundary = widget;
  Widget *current = widget->parent;

  while (current) {
    boundary = current;
    if (!current->autoWidth && !current->autoHeight)
      break;
    current = current->parent;
  }
  return boundary;
}

WidgetPtr FluxUI::findByIdRecursive(WidgetPtr widget, const std::string &id) {
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

// ============================================================================
// SCAFFOLD WIRING
// ============================================================================

void FluxUI::wireScaffoldToWidgets(ScaffoldWidget *scaffold, Widget *widget) {
  if (!widget)
    return;

  if (auto *host = dynamic_cast<OverlayHost *>(widget))
    host->setScaffold(scaffold);

  for (auto &child : widget->children)
    wireScaffoldToWidgets(scaffold, child.get());
}

// ============================================================================
// OVERLAY DISPATCHERS
// ============================================================================

namespace {
// File-local helper: depth-first search for a ScaffoldWidget.
ScaffoldWidget *findScaffold(Widget *widget) {
  if (!widget)
    return nullptr;
  if (auto *s = dynamic_cast<ScaffoldWidget *>(widget))
    return s;
  for (auto &child : widget->children) {
    if (auto *s = findScaffold(child.get()))
      return s;
  }
  return nullptr;
}
} // namespace

bool FluxUI::handleDropdownOverlays(int mouseX, int mouseY) {
  auto *scaffold = findScaffold(root.get());
  if (!scaffold || !scaffold->hasOverlays())
    return false;
  const auto &stack = scaffold->getOverlayStack();
  for (auto it = stack.rbegin(); it != stack.rend(); ++it)
    if (it->widget && it->widget->handleMouseDown(mouseX, mouseY))
      return true;
  return false;
}

bool FluxUI::handleDialogOverlays(int mouseX, int mouseY) {
  (void)mouseX;
  (void)mouseY;
  return false;
}

bool FluxUI::handleOverlayMouseMove(int mouseX, int mouseY) {
  auto *scaffold = findScaffold(root.get());
  if (!scaffold || !scaffold->hasOverlays())
    return false;
  const auto &stack = scaffold->getOverlayStack();
  for (auto it = stack.rbegin(); it != stack.rend(); ++it)
    if (it->widget && it->widget->handleMouseMove(mouseX, mouseY))
      return true;
  return false;
}

bool FluxUI::handleOverlayMouseWheel(int delta) {
  auto *scaffold = findScaffold(root.get());
  if (!scaffold || !scaffold->hasOverlays())
    return false;
  const auto &stack = scaffold->getOverlayStack();
  for (auto it = stack.rbegin(); it != stack.rend(); ++it)
    if (it->widget && it->widget->handleMouseWheel(delta))
      return true;
  return false;
}

bool FluxUI::handleOverlayKeyDown(int keyCode) {
  auto *scaffold = findScaffold(root.get());
  if (!scaffold || !scaffold->hasOverlays())
    return false;
  Widget *top = scaffold->getTopmostOverlay();
  return top ? top->handleKeyDown(keyCode) : false;
}

bool FluxUI::handleOverlayRightClick(int mouseX, int mouseY) {
  auto *scaffold = findScaffold(root.get());
  if (!scaffold || !scaffold->hasOverlays())
    return false;
  const auto &stack = scaffold->getOverlayStack();
  for (auto it = stack.rbegin(); it != stack.rend(); ++it)
    if (it->widget && it->widget->handleRightClick(mouseX, mouseY))
      return true;
  return false;
}

bool FluxUI::checkDropdownOverlays(Widget *, int, int) { return false; }
bool FluxUI::checkDialogOverlays(Widget *, int, int) { return false; }

// ============================================================================
// WINDOW PROCEDURE
// ============================================================================

LRESULT CALLBACK FluxUI::WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam,
                                    LPARAM lParam) {
  FluxUI *instance =
      reinterpret_cast<FluxUI *>(GetWindowLongPtr(hwnd, GWLP_USERDATA));

  switch (uMsg) {

  // ----------------------------------------------------------------
  case WM_CREATE: {
    CREATESTRUCT *pCreate = reinterpret_cast<CREATESTRUCT *>(lParam);
    instance = reinterpret_cast<FluxUI *>(pCreate->lpCreateParams);
    SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(instance));
    return 0;
  }

  // WM_PAINT
  case WM_PAINT: {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps); // Win32 layer — stays raw

    if (!instance || !instance->root) {
      EndPaint(hwnd, &ps);
      return 0;
    }

    RECT clientRect;
    GetClientRect(hwnd, &clientRect);
    int width = clientRect.right - clientRect.left;
    int height = clientRect.bottom - clientRect.top;

    instance->backBuffer.create(hwnd, width, height);

    if (instance->backBuffer.valid()) {
      static HBRUSH bgBrush = CreateSolidBrush(RGB(250, 250, 250));
      FillRect(instance->backBuffer.hdc, &clientRect,
               bgBrush); // Win32 — raw ok

      GraphicsContext ctx = instance->backBuffer.context(); // wrap here
      Renderer::renderWidget(ctx, instance->root.get(), instance->fontCache);

      HWND child = GetWindow(hwnd, GW_CHILD);
      while (child) {
        RECT cr;
        GetWindowRect(child, &cr);
        MapWindowPoints(HWND_DESKTOP, hwnd, (POINT *)&cr, 2);
        ExcludeClipRect(hdc, cr.left, cr.top, cr.right, cr.bottom);
        child = GetNextWindow(child, GW_HWNDNEXT);
      }

      BitBlt(hdc, 0, 0, width, height, instance->backBuffer.hdc, 0, 0,
             SRCCOPY); // Win32 — raw ok
    }

    EndPaint(hwnd, &ps);
    return 0;
  }

  // WM_SIZE
  case WM_SIZE: {
    if (instance && instance->root) {
      RECT rect;
      GetClientRect(hwnd, &rect);
      int width = rect.right - rect.left;
      int height = rect.bottom - rect.top;

      HDC hdc = GetDC(hwnd);    // Win32 — stays raw
      GraphicsContext ctx(hdc); // wrap immediately
      LayoutEngine::computeLayout(ctx, instance->root.get(), width, height,
                                  instance->fontCache);
      LayoutEngine::positionWidget(instance->root.get(), 0, 0);
      ReleaseDC(hwnd, hdc); // Win32 — stays raw

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

    if (instance->handleOverlayMouseWheel(delta)) {
      InvalidateRect(hwnd, NULL, FALSE);
      return 0;
    }

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

    if (instance->handleDropdownOverlays(mouseX, mouseY)) {
      InvalidateRect(hwnd, NULL, FALSE);
      return 0;
    }

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

    instance->setFocus(nullptr);

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

    if (instance->handleOverlayRightClick(mouseX, mouseY)) {
      InvalidateRect(hwnd, NULL, FALSE);
      return 0;
    }

    if (findAndHandleMouseEvent(instance->root.get(), mouseX, mouseY,
                                [mouseX, mouseY](Widget *w) {
                                  return w->handleRightClick(mouseX, mouseY);
                                })) {
      InvalidateRect(hwnd, NULL, FALSE);
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

    if (hasCapturedMouse) {
      if (broadcastMouseEvent(instance->root.get(), mouseX, mouseY,
                              [](Widget *w, int mx, int my) {
                                return w->handleMouseMove(mx, my);
                              })) {
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
      }
    }

    bool overlayHandled = instance->handleOverlayMouseMove(mouseX, mouseY);
    bool hoverChanged = updateHoverStates(instance->root.get(), mouseX, mouseY);
    bool customHandled = findAndHandleMouseEvent(
        instance->root.get(), mouseX, mouseY, [mouseX, mouseY](Widget *w) {
          return w->handleMouseMove(mouseX, mouseY);
        });

    if (overlayHandled || hoverChanged || customHandled)
      InvalidateRect(hwnd, NULL, FALSE);

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
    if (instance->focusedWidget->handleChar(ch))
      InvalidateRect(hwnd, NULL, FALSE);
    return 0;
  }

  // ----------------------------------------------------------------
  case WM_KEYDOWN: {
    if (!instance)
      return 0;

    int keyCode = (int)wParam;

    if (instance->handleOverlayKeyDown(keyCode)) {
      InvalidateRect(hwnd, NULL, FALSE);
      return 0;
    }

    if (instance->focusedWidget &&
        instance->focusedWidget->handleKeyDown(keyCode)) {
      InvalidateRect(hwnd, NULL, FALSE);
    }
    return 0;
  }

  // ----------------------------------------------------------------
  case WM_TIMER: {
    if (!instance)
      return 0;

    auto it = instance->timerCallbacks.find((UINT)wParam);
    if (it != instance->timerCallbacks.end()) {
      it->second();
      return 0;
    }

    if (instance->focusedWidget &&
        instance->focusedWidget->handleTimer((UINT)wParam)) {
      instance->invalidateWidget(instance->focusedWidget);
    }
    return 0;
  }

  // ----------------------------------------------------------------
  case WM_DESTROY:

    PostQuitMessage(0);
    return 0;
  }

  return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

// ============================================================================
// STATE FACTORY
// ============================================================================

template <typename T> State<T> FluxUI::useState(T initialValue) {
  return State<T>(initialValue, this);
}

// ============================================================================
// FOCUS MANAGEMENT
// ============================================================================

void FluxUI::setFocus(Widget *widget) {
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

Widget *FluxUI::getFocusedWidget() const { return focusedWidget; }

// ============================================================================
// TIMER HELPERS
// ============================================================================

UINT FluxUI::setInterval(int ms, std::function<void()> callback) {
  static UINT nextId = 100;
  UINT id = nextId++;
  timerCallbacks[id] = callback;

  if (hwnd) {
    SetTimer(hwnd, id, ms, nullptr);
  } else {
    pendingTimers.push_back(
        {id, [this, id, ms]() { SetTimer(hwnd, id, ms, nullptr); }});
  }
  return id;
}

void FluxUI::clearInterval(UINT id) {
  if (hwnd)
    KillTimer(hwnd, id);
  timerCallbacks.erase(id);

  pendingTimers.erase(
      std::remove_if(pendingTimers.begin(), pendingTimers.end(),
                     [id](const std::pair<UINT, std::function<void()>> &p) {
                       return p.first == id;
                     }),
      pendingTimers.end());
}

// ============================================================================
// BUILD / REBUILD
// ============================================================================

void FluxUI::build(std::function<WidgetPtr()> buildFunc) {
  builder = buildFunc;
  rebuild();
}

void FluxUI::rebuild() {
  if (!builder)
    return;

  if (root) {
    root->onDetach();
    if (auto *scaffold = findScaffold(root.get()))
      scaffold->clearOverlays();
  }

  root = builder();

  if (auto *scaffold = findScaffold(root.get()))
    wireScaffoldToWidgets(scaffold, root.get());

  if (hwnd) {
    RECT rect;
    GetClientRect(hwnd, &rect);
    int w = rect.right - rect.left;
    int h = rect.bottom - rect.top;
    HDC hdc = GetDC(hwnd);
    GraphicsContext ctx(hdc);  
    LayoutEngine::computeLayout(ctx, root.get(), w, h, fontCache);
    LayoutEngine::positionWidget(root.get(), 0, 0);
    ReleaseDC(hwnd, hdc);
    InvalidateRect(hwnd, NULL, FALSE);
  }
}

// ============================================================================
// INVALIDATION / PARTIAL LAYOUT
// ============================================================================

void FluxUI::updateWidget(Widget *widget) {
  if (!widget || !hwnd)
    return;

  int oldWidth = widget->width;
  int oldHeight = widget->height;

  HDC hdc = GetDC(hwnd);
  GraphicsContext ctx(hdc);  
  widget->measureText(ctx, fontCache);
  widget->width += widget->paddingLeft + widget->paddingRight;
  widget->height += widget->paddingTop + widget->paddingBottom;
  ReleaseDC(hwnd, hdc);

  bool sizeChanged = (oldWidth != widget->width || oldHeight != widget->height);

  if (sizeChanged)
    partialRebuild(widget);
  else
    invalidateWidget(widget);
}

void FluxUI::invalidateWidget(Widget *widget) {
  if (!widget || !hwnd)
    return;

  RECT rect = {widget->x, widget->y, widget->x + widget->width,
               widget->y + widget->height};
  InvalidateRect(hwnd, &rect, FALSE);
}

void FluxUI::partialRebuild(Widget *widget) {
  if (!widget || !hwnd)
    return;

  Widget *boundary = findLayoutBoundary(widget);

  Widget *current = widget;
  while (current && current != boundary) {
    current->markNeedsLayout();
    current = current->parent;
  }
  boundary->markNeedsLayout();

  HDC hdc = GetDC(hwnd);
GraphicsContext ctx(hdc);  
  if (boundary == root.get()) {
    RECT rect;
    GetClientRect(hwnd, &rect);
    int w = rect.right - rect.left;
    int h = rect.bottom - rect.top;
    
    LayoutEngine::computeLayout(ctx, root.get(), w, h, fontCache);
    LayoutEngine::positionWidget(root.get(), 0, 0);
  } else {
    LayoutEngine::computeLayout(ctx, boundary, boundary->width,
                                boundary->height, fontCache);
    LayoutEngine::positionWidget(boundary, boundary->x, boundary->y);
  }

  ReleaseDC(hwnd, hdc);

  RECT dirtyRect = {boundary->x, boundary->y, boundary->x + boundary->width,
                    boundary->y + boundary->height};
  InvalidateRect(hwnd, &dirtyRect, FALSE);
}

// ============================================================================
// WINDOW MANAGEMENT
// ============================================================================

HWND FluxUI::createWindow(const std::string &title, int width, int height) {
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
    GraphicsContext ctx(hdc);  
    LayoutEngine::computeLayout(ctx, root.get(), clientWidth, clientHeight,
                                fontCache);
    LayoutEngine::positionWidget(root.get(), 0, 0);
    ReleaseDC(hwnd, hdc);
  }

  for (auto &[id, fn] : pendingTimers)
    fn();
  pendingTimers.clear();

  return hwnd;
}

int FluxUI::run() {
  MSG msg;
  while (GetMessage(&msg, NULL, 0, 0)) {
    TranslateMessage(&msg);
    DispatchMessage(&msg);
  }
  return (int)msg.wParam;
}

// ============================================================================
// ACCESSORS
// ============================================================================

HWND FluxUI::getWindow() const { return hwnd; }
WidgetPtr FluxUI::getRoot() const { return root; }

WidgetPtr FluxUI::findById(const std::string &id) {
  return findByIdRecursive(root, id);
}

FontCache &FluxUI::getFontCache() { return fontCache; }