#include "flux/flux_window.hpp"

#ifdef _WIN32
#include <windowsx.h>

GraphicsContext PlatformWindow::getMeasureContext() const {
  // On Win32, GetDC(nullptr) gives a screen DC — valid before any window exists
  return GraphicsContext(GetDC(hwnd ? hwnd : nullptr));
}

void PlatformWindow::startupGdiplus() {
  if (gdiplusToken != 0)
    return; // already initialized, no-op
  Gdiplus::GdiplusStartupInput input;
  Gdiplus::GdiplusStartup(&gdiplusToken, &input, nullptr);
}

void PlatformWindow::shutdownGdiplus() {
  Gdiplus::GdiplusShutdown(gdiplusToken);
  gdiplusToken = 0;
}

void PlatformWindow::setClipboardText(const std::string &text) {
  if (!hwnd || !OpenClipboard(hwnd))
    return;
  EmptyClipboard();
  HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, text.size() + 1);
  if (hg) {
    memcpy(GlobalLock(hg), text.c_str(), text.size() + 1);
    GlobalUnlock(hg);
    SetClipboardData(CF_TEXT, hg);
  }
  CloseClipboard();
}

std::string PlatformWindow::getClipboardText() {
  if (!hwnd || !OpenClipboard(hwnd))
    return "";
  HANDLE hd = GetClipboardData(CF_TEXT);
  if (!hd) {
    CloseClipboard();
    return "";
  }
  std::string text = static_cast<char *>(GlobalLock(hd));
  GlobalUnlock(hd);
  CloseClipboard();
  return text;
}

void PlatformWindow::captureMouseInput() {
  if (hwnd)
    SetCapture(hwnd);
}
void PlatformWindow::releaseMouseInput() { ReleaseCapture(); }

PlatformWindow::ScreenPoint PlatformWindow::clientToScreen(int cx,
                                                           int cy) const {
  POINT pt = {cx, cy};
  if (hwnd)
    ClientToScreen(hwnd, &pt);
  return {pt.x, pt.y};
}

PlatformWindow::ScreenPoint PlatformWindow::screenToClient(int sx,
                                                           int sy) const {
  POINT pt = {sx, sy};
  if (hwnd)
    ScreenToClient(hwnd, &pt);
  return {pt.x, pt.y};
}

PlatformWindow::ClientSize PlatformWindow::getClientSize() const {
  return {cachedWidth, cachedHeight};
}

void PlatformWindow::setResizeCursorH() {
  SetCursor(LoadCursor(nullptr, IDC_SIZEWE));
}
void PlatformWindow::setResizeCursorV() {
  SetCursor(LoadCursor(nullptr, IDC_SIZENS));
}
void PlatformWindow::setDefaultCursor() {
  SetCursor(LoadCursor(nullptr, IDC_ARROW));
}

// ============================================================================
// PlatformWindow::create
// ============================================================================

bool PlatformWindow::create(const std::string &title, int width, int height,
                            AppInstance hInst, void *userData) {
  startupGdiplus();
  hInstance = hInst;

  WNDCLASSEX wc = {};
  wc.cbSize = sizeof(WNDCLASSEX);
  wc.lpfnWndProc = WindowProc;
  wc.hInstance = hInstance;
  wc.lpszClassName = "FluxUI";
  wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
  wc.hCursor = LoadCursor(NULL, IDC_ARROW);
  wc.style = CS_HREDRAW | CS_VREDRAW;
  RegisterClassEx(&wc);

  RECT r = {0, 0, width, height};
  AdjustWindowRectEx(&r, WS_OVERLAPPEDWINDOW, FALSE, 0);

  hwnd = CreateWindowEx(
      0, "FluxUI", title.c_str(), WS_OVERLAPPEDWINDOW | WS_VISIBLE,
      CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top, NULL,
      NULL, hInstance, userData); // userData goes to WM_CREATE

  if (!hwnd)
    return false;

  updateClientSize();
  return true;
}

// ============================================================================
// PlatformWindow::destroy
// ============================================================================

void PlatformWindow::destroy() {
  backBuffer.destroy();
  if (hwnd) {
    DestroyWindow(hwnd);
    hwnd = nullptr;
  }
  shutdownGdiplus();
}

// ============================================================================
// PlatformWindow::run
// ============================================================================

int PlatformWindow::run() {
  MSG msg;
  while (GetMessage(&msg, NULL, 0, 0)) {
    TranslateMessage(&msg);
    DispatchMessage(&msg);
  }
  return (int)msg.wParam;
}

// ============================================================================
// PlatformWindow::invalidate
// ============================================================================

void PlatformWindow::invalidate() {
  if (hwnd)
    InvalidateRect(hwnd, NULL, FALSE);
}

void PlatformWindow::invalidateRect(int x, int y, int w, int h) {
  if (!hwnd)
    return;
  RECT rect = {x, y, x + w, y + h};
  ::InvalidateRect(hwnd, &rect, FALSE);
}

// ============================================================================
// PlatformWindow::setTimer / killTimer
// ============================================================================

void PlatformWindow::setTimer(UINT id, int ms) {
  if (hwnd)
    ::SetTimer(hwnd, id, ms, nullptr);
}

void PlatformWindow::killTimer(UINT id) {
  if (hwnd)
    ::KillTimer(hwnd, id);
}

// ============================================================================
// PlatformWindow::updateClientSize
// ============================================================================

void PlatformWindow::updateClientSize() {
  if (!hwnd)
    return;
  RECT rect;
  GetClientRect(hwnd, &rect);
  cachedWidth = rect.right - rect.left;
  cachedHeight = rect.bottom - rect.top;
}

// ============================================================================
// WINDOW PROCEDURE
// ============================================================================

LRESULT CALLBACK PlatformWindow::WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam,
                                            LPARAM lParam) {
  // FluxUI* is stored as user data — same pattern as before
  PlatformWindow *self =
      reinterpret_cast<PlatformWindow *>(GetWindowLongPtr(hwnd, GWLP_USERDATA));

  switch (uMsg) {

  // ----------------------------------------------------------------
  case WM_CREATE: {
    // lpCreateParams is the userData passed to CreateWindowEx
    CREATESTRUCT *cs = reinterpret_cast<CREATESTRUCT *>(lParam);
    SetWindowLongPtr(hwnd, GWLP_USERDATA,
                     reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
    return 0;
  }

  // ----------------------------------------------------------------
  case WM_PAINT: {
    if (!self)
      return DefWindowProc(hwnd, uMsg, wParam, lParam);

    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);

    self->updateClientSize();
    int w = self->cachedWidth;
    int h = self->cachedHeight;

    self->backBuffer.create(hwnd, w, h);

    if (self->backBuffer.valid()) {
      RECT clientRect = {0, 0, w, h};
      static HBRUSH bgBrush = CreateSolidBrush(RGB(250, 250, 250));
      FillRect(self->backBuffer.hdc, &clientRect, bgBrush);

      if (self->callbacks.onPaint) {
        GraphicsContext ctx = self->backBuffer.context();
        self->callbacks.onPaint(ctx, w, h);
      }

      // Exclude child HWNDs before blit
      HWND child = GetWindow(hwnd, GW_CHILD);
      while (child) {
        RECT cr;
        GetWindowRect(child, &cr);
        MapWindowPoints(HWND_DESKTOP, hwnd, (POINT *)&cr, 2);
        ExcludeClipRect(hdc, cr.left, cr.top, cr.right, cr.bottom);
        child = GetNextWindow(child, GW_HWNDNEXT);
      }

      BitBlt(hdc, 0, 0, w, h, self->backBuffer.hdc, 0, 0, SRCCOPY);
    }

    EndPaint(hwnd, &ps);
    return 0;
  }

  // ----------------------------------------------------------------
  case WM_SIZE: {
    if (!self)
      return DefWindowProc(hwnd, uMsg, wParam, lParam);

    self->updateClientSize();
    int w = self->cachedWidth;
    int h = self->cachedHeight;

    if (self->callbacks.onResize) {
      HDC hdc = GetDC(hwnd);
      GraphicsContext ctx(hdc);
      self->callbacks.onResize(ctx, w, h);
      ReleaseDC(hwnd, hdc);
    }

    InvalidateRect(hwnd, NULL, FALSE);
    return 0;
  }

  // ----------------------------------------------------------------
  case WM_MOUSEWHEEL: {
    if (!self)
      return 0;
    int delta = GET_WHEEL_DELTA_WPARAM(wParam);
    int x = GET_X_LPARAM(lParam);
    int y = GET_Y_LPARAM(lParam);
    POINT pt = {x, y};
    ScreenToClient(hwnd, &pt);
    if (self->callbacks.onMouseWheel && self->callbacks.onMouseWheel(delta))
      self->invalidate();
    return 0;
  }

  // ----------------------------------------------------------------
  case WM_LBUTTONDOWN: {
    if (!self)
      return 0;
    int x = LOWORD(lParam), y = HIWORD(lParam);
    if (self->callbacks.onMouseDown && self->callbacks.onMouseDown(x, y))
      self->invalidate();
    return 0;
  }

  // ----------------------------------------------------------------
  case WM_RBUTTONDOWN: {
    if (!self)
      return 0;
    int x = LOWORD(lParam), y = HIWORD(lParam);
    if (self->callbacks.onRightClick && self->callbacks.onRightClick(x, y))
      self->invalidate();
    return 0;
  }

  // ----------------------------------------------------------------
  case WM_LBUTTONUP: {
    if (!self)
      return 0;
    int x = LOWORD(lParam), y = HIWORD(lParam);
    if (self->callbacks.onMouseUp && self->callbacks.onMouseUp(x, y))
      self->invalidate();
    return 0;
  }

  // ----------------------------------------------------------------
  case WM_MOUSEMOVE: {
    if (!self)
      return 0;

    TRACKMOUSEEVENT tme = {};
    tme.cbSize = sizeof(TRACKMOUSEEVENT);
    tme.dwFlags = TME_LEAVE;
    tme.hwndTrack = hwnd;
    TrackMouseEvent(&tme);

    int x = LOWORD(lParam), y = HIWORD(lParam);
    if (self->callbacks.onMouseMove && self->callbacks.onMouseMove(x, y))
      self->invalidate();
    return 0;
  }

  // ----------------------------------------------------------------
  case WM_MOUSELEAVE: {
    if (!self)
      return 0;
    if (self->callbacks.onMouseLeave)
      self->callbacks.onMouseLeave();
    self->invalidate();
    return 0;
  }

  // ----------------------------------------------------------------
  case WM_CHAR: {
    if (!self)
      return 0;
    wchar_t ch = (wchar_t)wParam;
    if (self->callbacks.onChar && self->callbacks.onChar(ch))
      self->invalidate();
    return 0;
  }

  // ----------------------------------------------------------------
  case WM_KEYDOWN: {
    if (!self)
      return 0;
    int keyCode = (int)wParam;
    if (self->callbacks.onKeyDown && self->callbacks.onKeyDown(keyCode))
      self->invalidate();
    return 0;
  }

  // ----------------------------------------------------------------
  case WM_TIMER: {
    if (!self)
      return 0;
    if (self->callbacks.onTimer)
      self->callbacks.onTimer((UINT)wParam);
    return 0;
  }

  case WM_NCLBUTTONDOWN: {
    if (!self)
      return 0;
    if (self->callbacks.onNonClientMouseDown)
      self->callbacks.onNonClientMouseDown();
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
  }

//   case WM_ACTIVATE:
//       if (!self)
//       return 0;
//     if (LOWORD(wParam) == WA_INACTIVE) {
//       if (self->callbacks.onFocusLost)
//         self->callbacks.onFocusLost(); 
//     }
//     break;

  // ----------------------------------------------------------------
  case WM_DESTROY:
    if (self)
      self->backBuffer.destroy();
    PostQuitMessage(0);
    return 0;
  }

  return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

#endif // _WIN32