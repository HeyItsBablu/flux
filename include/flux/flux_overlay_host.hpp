#ifndef FLUX_OVERLAY_HOST_HPP
#define FLUX_OVERLAY_HOST_HPP

// ============================================================================
// OVERLAY HOST MIXIN  —  v2  (popup-window edition)
//
// Problem solved
// ──────────────
// CanvasWidget creates a WS_CHILD HWND that is always painted *after* the
// GDI back-buffer BitBlt, so any overlay drawn into hdcMem is invisible
// whenever a CanvasWidget is present.
//
// Solution
// ────────
// Every overlay widget owns a floating WS_POPUP | WS_EX_LAYERED |
// WS_EX_TOPMOST window ("the popup").  It renders its content into a DIB
// section and blits it through UpdateLayeredWindow, which composites
// correctly over child HWNDs including OpenGL surfaces.
//
// The existing ScaffoldWidget overlay stack is kept for backwards compat
// (it still drives hit-testing and keyboard routing in FluxUI), but the
// *visual* output is now the popup window, not a paint into hdcMem.
//
// Usage contract for widget authors
// ──────────────────────────────────
//   1. Inherit from both Widget and OverlayHost.
//   2. Call showPopup(parentHwnd, x, y, w, h) to create/show the window.
//   3. Override renderPopupContent(HDC, FontCache&) to paint into it.
//      The HDC is already sized to (popupW × popupH), pre-cleared to
//      transparent (all-zero ARGB), so you draw normally.
//   4. Call refreshPopup() whenever the visual content changes.
//   5. Call hidePopup() to remove the window.
//   6. Implement setScaffold() as before; the base provides a default body
//      you can delegate to.
//
// OverlayHost::renderPopupContent default is a no-op — override it.
// ============================================================================

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include "flux_platform.hpp"

#include <cassert>
#include <functional>

#include "flux_font.hpp" // FontCache forward-declared here already

// Forward declaration — defined in flux_structure.hpp
class ScaffoldWidget;

// ============================================================================
// OverlayHost
// ============================================================================

class OverlayHost {
public:
  virtual ~OverlayHost() { destroyPopup(); }

  // ----------------------------------------------------------------
  // Interface every overlay widget must implement
  // ----------------------------------------------------------------

  // Called by FluxUI::wireScaffoldToWidgets after every rebuild().
  // 's' may be nullptr (standalone / detached tree).
  virtual void setScaffold(ScaffoldWidget *s) = 0;

  // Override to paint overlay content.
  // 'hdc' is a memory DC pre-sized to (popupW × popupH).
  // It is already filled with transparent black (ARGB 0,0,0,0).
  // Paint with premultiplied-alpha GDI calls — or paint opaque shapes
  // as usual; the base will pre-multiply for you before blitting.
  virtual void renderPopupContent(GraphicsContext & /*ctx*/,
                                  FontCache & /*fc*/) {}

  // ----------------------------------------------------------------
  // Popup lifecycle helpers (call from derived open/close methods)
  // ----------------------------------------------------------------

  // Create (or reposition) the layered popup and paint its first frame.
  // parentHwnd  – the application's top-level HWND (for ownership/z-order).
  // screenX/Y   – screen coordinates of the popup's top-left corner.
  // w, h        – pixel dimensions of the popup client area.
  void showPopup(HWND parentHwnd, int screenX, int screenY, int w, int h,
                 FontCache &fontCache) {
    assert(parentHwnd);
    assert(w > 0 && h > 0);

    if (!popupClass_)
      registerPopupClass_();

    popupW_ = w;
    popupH_ = h;

    if (!popupHwnd_) {
      // WS_POPUP        – no caption, no border, no taskbar entry
      // WS_EX_LAYERED   – enables UpdateLayeredWindow
      // WS_EX_TOPMOST   – always above child HWNDs (inc. OpenGL surface)
      // WS_EX_TOOLWINDOW– keeps it off the Alt-Tab list
      // WS_EX_NOACTIVATE– clicking doesn't steal keyboard focus
      popupHwnd_ = CreateWindowExW(
          WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
          kPopupClass_, nullptr, WS_POPUP, screenX, screenY, w, h, parentHwnd,
          nullptr, GetModuleHandleW(nullptr), static_cast<LPVOID>(this));
      assert(popupHwnd_ && "CreateWindowExW for overlay popup failed");
    } else {
      SetWindowPos(popupHwnd_, HWND_TOPMOST, screenX, screenY, w, h,
                   SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }

    ShowWindow(popupHwnd_, SW_SHOWNOACTIVATE);
    paintLayered_(fontCache);
  }

  // Repaint the popup contents without moving/resizing it.
  void refreshPopup(FontCache &fontCache) {
    if (popupHwnd_)
      paintLayered_(fontCache);
  }

  // Hide and destroy the popup window.
  void hidePopup() {
    if (popupHwnd_) {
      DestroyWindow(popupHwnd_);
      popupHwnd_ = nullptr;
    }
    freeDIB_();
  }

  // True while the popup window exists and is visible.
  bool popupVisible() const { return popupHwnd_ != nullptr; }

  // Convenience: convert a widget-local point (relative to the FluxUI
  // top-level window's client area) to screen coordinates.
  static POINT clientToScreen(HWND topLevel, int cx, int cy) {
    POINT pt = {cx, cy};
    ClientToScreen(topLevel, &pt);
    return pt;
  }

  // ----------------------------------------------------------------
  // Forward WM_LBUTTONDOWN / WM_MOUSEMOVE etc. from the popup HWND
  // to the application's top-level window proc so that FluxUI's
  // overlay dispatchers still see the events expressed in client
  // coordinates of the main window.
  //
  // Call this from the popup's WM_LBUTTONDOWN handler:
  //   OverlayHost::forwardMouseEvent(hwnd, WM_LBUTTONDOWN, wp, lp);
  // ----------------------------------------------------------------
  static void forwardMouseEvent(HWND popupHwnd, UINT msg, WPARAM wp,
                                LPARAM lp) {
    // lp is in popup client coords; convert to main-window client coords.
    HWND owner = GetWindow(popupHwnd, GW_OWNER);
    if (!owner)
      return;
    POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
    MapWindowPoints(popupHwnd, owner, &pt, 1);
    PostMessageW(owner, msg, wp, MAKELPARAM(pt.x, pt.y));
  }

private:
  // ----------------------------------------------------------------
  // Internal state
  // ----------------------------------------------------------------
  HWND popupHwnd_ = nullptr;
  int popupW_ = 0;
  int popupH_ = 0;

  // DIB section used as the layered-window surface
  HDC dibDC_ = nullptr;
  HBITMAP dibBmp_ = nullptr;
  HBITMAP dibOld_ = nullptr;
  void *dibBits_ = nullptr; // raw pixel pointer (BGRA, premul)

  // ----------------------------------------------------------------
  // DIB management
  // ----------------------------------------------------------------
  void ensureDIB_(int w, int h) {
    if (dibDC_ && dibBmp_ && popupW_ == w && popupH_ == h)
      return;
    freeDIB_();

    BITMAPINFOHEADER bih{};
    bih.biSize = sizeof(bih);
    bih.biWidth = w;
    bih.biHeight = -h; // top-down
    bih.biPlanes = 1;
    bih.biBitCount = 32;
    bih.biCompression = BI_RGB;

    BITMAPINFO bi{};
    bi.bmiHeader = bih;

    HDC screenDC = GetDC(nullptr);
    dibBmp_ =
        CreateDIBSection(screenDC, &bi, DIB_RGB_COLORS, &dibBits_, nullptr, 0);
    ReleaseDC(nullptr, screenDC);
    assert(dibBmp_);

    dibDC_ = CreateCompatibleDC(nullptr);
    dibOld_ = static_cast<HBITMAP>(SelectObject(dibDC_, dibBmp_));
  }

  void freeDIB_() {
    if (dibDC_) {
      SelectObject(dibDC_, dibOld_);
      DeleteDC(dibDC_);
      dibDC_ = nullptr;
      dibOld_ = nullptr;
    }
    if (dibBmp_) {
      DeleteObject(dibBmp_);
      dibBmp_ = nullptr;
    }
    dibBits_ = nullptr;
  }

  // ----------------------------------------------------------------
  // Paint + UpdateLayeredWindow
  // ----------------------------------------------------------------
  // Magic colour used to mark "untouched" pixels so we can make them
  // transparent after GDI paint.  Chosen to be an unlikely real colour.
  // BGRA layout in the DIB: B=1, G=2, R=3, A=0  → stored as 0x00030201
  static constexpr COLORREF kClearKey_ = RGB(3, 2, 1); // R=3,G=2,B=1

  void paintLayered_(FontCache &fontCache) {
    if (!popupHwnd_ || popupW_ <= 0 || popupH_ <= 0)
      return;

    ensureDIB_(popupW_, popupH_);

    // ── Step 1: flood-fill the DIB with the magic key colour ──────────
    // GDI FillRect writes opaque pixels; our post-pass turns key-coloured
    // pixels back to transparent and forces A=255 on everything else.
    {
      HBRUSH clearBrush = CreateSolidBrush(kClearKey_);
      RECT all = {0, 0, popupW_, popupH_};
      FillRect(dibDC_, &all, clearBrush);
      DeleteObject(clearBrush);
    }
    GraphicsContext ctx(dibDC_);
    // ── Step 2: let the derived widget paint normally ──────────────────
    renderPopupContent(ctx, fontCache);

    // ── Step 3: fix up alpha channel ──────────────────────────────────
    // GDI leaves A=0 in every pixel it touches (it never writes alpha).
    // We convert:
    //   key-colour pixels  → fully transparent  (A=0, RGB=0)
    //   all other pixels   → fully opaque       (A=255, RGB unchanged)
    fixupAlpha_();

    // ── Step 4: push to the layered window ────────────────────────────
    SIZE dstSz{popupW_, popupH_};
    POINT srcPt{0, 0};
    BLENDFUNCTION bf{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    UpdateLayeredWindow(popupHwnd_, nullptr, nullptr, &dstSz, dibDC_, &srcPt, 0,
                        &bf, ULW_ALPHA);
  }

  void fixupAlpha_() {
    if (!dibBits_)
      return;
    // DIB is top-down 32-bit BGRA.  GDI writes B/G/R but always leaves
    // A=0.  We rely on the key colour to distinguish background from paint.
    // Key in DIB byte order: B=1, G=2, R=3  (kClearKey_ = RGB(3,2,1))
    auto *px = static_cast<BYTE *>(dibBits_);
    int n = popupW_ * popupH_;
    for (int i = 0; i < n; ++i, px += 4) {
      // px[0]=B, px[1]=G, px[2]=R, px[3]=A
      if (px[0] == 1 && px[1] == 2 && px[2] == 3) {
        // Background key → fully transparent
        px[0] = px[1] = px[2] = px[3] = 0;
      } else {
        // Real content → fully opaque
        px[3] = 255;
      }
    }
  }

  // ----------------------------------------------------------------
  // Static popup window class + proc
  // ----------------------------------------------------------------
  static constexpr const wchar_t *kPopupClass_ = L"FluxOverlayPopup_v2";
  static bool popupClass_;

  static void registerPopupClass_() {
    if (popupClass_)
      return;
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = popupProc_;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kPopupClass_;
    wc.style = 0; // no CS_HREDRAW/CS_VREDRAW
    RegisterClassExW(&wc);
    popupClass_ = true;
  }

  static LRESULT CALLBACK popupProc_(HWND hwnd, UINT msg, WPARAM wp,
                                     LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
      auto *cs = reinterpret_cast<CREATESTRUCTW *>(lp);
      SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                        reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
      return 0;
    }
    case WM_ERASEBKGND:
      return 1;
    case WM_PAINT: {
      PAINTSTRUCT ps;
      BeginPaint(hwnd, &ps);
      EndPaint(hwnd, &ps);
      return 0;
    }
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_MOUSEMOVE:
    case WM_MOUSEWHEEL:
      forwardMouseEvent(hwnd, msg, wp, lp);
      return 0;
    case WM_NCHITTEST:
      return HTCLIENT;
    default:
      return DefWindowProcW(hwnd, msg, wp, lp);
    }
  }

  // ----------------------------------------------------------------
  // destroy helper (called from destructor)
  // ----------------------------------------------------------------
  void destroyPopup() {
    hidePopup(); // destroys HWND + frees DIB
  }
};

// Out-of-line static definition
inline bool OverlayHost::popupClass_ = false;

#endif // FLUX_OVERLAY_HOST_HPP