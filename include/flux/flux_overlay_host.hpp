#ifndef FLUX_OVERLAY_HOST_HPP
#define FLUX_OVERLAY_HOST_HPP

#include "flux_font.hpp"
#include "flux_platform.hpp"
#include <cassert>
#include <functional>

class ScaffoldWidget;

// ============================================================================
// OverlayHost
//
// Win32 : renders into a layered HWND popup (per-pixel alpha via DIB).
// Linux : renders directly into the main Cairo surface via the scaffold's
//         overlay stack — no separate window needed.
//
// Derived classes implement renderPopupContent() identically on both
// platforms. The open/close/refresh plumbing differs per platform but
// the interface seen by ContextMenuWidget, DropdownWidget, etc. is the same.
// ============================================================================

class OverlayHost {
public:
  virtual ~OverlayHost() { destroyPopup(); }

  // ── Interface every overlay widget must implement ─────────────────────
  virtual void setScaffold(ScaffoldWidget *s) = 0;

  // Paint overlay content into ctx.
  // Win32: ctx wraps a DIB DC sized to (popupW x popupH), origin = (0,0).
  // Linux: ctx wraps the main cairo_t*, origin = screen coords of popup.
  virtual void renderPopupContent(GraphicsContext &ctx, FontCache &fc) {}

  // ── Popup lifecycle (called by derived open/close methods) ────────────

#ifdef _WIN32

  void showPopup(HWND parentHwnd, int screenX, int screenY, int w, int h,
                 FontCache &fontCache) {
    assert(parentHwnd && w > 0 && h > 0);
    if (!popupClass_)
      registerPopupClass_();

    popupW_ = w;
    popupH_ = h;

    if (!popupHwnd_) {
      popupHwnd_ = CreateWindowExW(
          WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
          kPopupClass_, nullptr, WS_POPUP, screenX, screenY, w, h, parentHwnd,
          nullptr, GetModuleHandleW(nullptr), static_cast<LPVOID>(this));
      assert(popupHwnd_);
    } else {
      SetWindowPos(popupHwnd_, HWND_TOPMOST, screenX, screenY, w, h,
                   SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }
    ShowWindow(popupHwnd_, SW_SHOWNOACTIVATE);
    paintLayered_(fontCache);
  }

  void refreshPopup(FontCache &fontCache) {
    if (popupHwnd_)
      paintLayered_(fontCache);
  }

  void hidePopup() {
    if (popupHwnd_) {
      DestroyWindow(popupHwnd_);
      popupHwnd_ = nullptr;
    }
    freeDIB_();
  }

  bool popupVisible() const { return popupHwnd_ != nullptr; }

  static POINT clientToScreen(HWND topLevel, int cx, int cy) {
    POINT pt = {cx, cy};
    ::ClientToScreen(topLevel, &pt);
    return pt;
  }

  static void forwardMouseEvent(HWND popupHwnd, UINT msg, WPARAM wp,
                                LPARAM lp) {
    HWND owner = GetWindow(popupHwnd, GW_OWNER);
    if (!owner)
      return;
    POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
    MapWindowPoints(popupHwnd, owner, &pt, 1);
    PostMessageW(owner, msg, wp, MAKELPARAM(pt.x, pt.y));
  }

#else // Linux ────────────────────────────────────────────────────────────────

  // On Linux there is no separate popup window.
  // showPopup() just records where the overlay should be drawn;
  // the scaffold calls renderPopupContent() during the normal paint pass,
  // passing a GraphicsContext that is already translated to (originX_,
  // originY_).

  void showPopup(NativeWindow /*parent*/, int screenX, int screenY, int w,
                 int h, FontCache & /*fontCache*/) {
    originX_ = screenX;
    originY_ = screenY;
    popupW_ = w;
    popupH_ = h;
    visible_ = true;
    // The scaffold invalidates the window after addOverlayHitTarget(),
    // which is called by the derived widget right after showPopup().
  }

  // On Linux, "refresh" just marks the window dirty — the next paint
  // pass will call renderPopupContent() automatically via the scaffold.
  void refreshPopup(FontCache & /*fontCache*/) {
    // Trigger a repaint through FluxUI if an instance is available.
    // The scaffold overlay stack already holds a pointer to this widget,
    // so it will be repainted on the next frame automatically.
    markDirty_();
  }

  void hidePopup() {
    visible_ = false;
    popupW_ = popupH_ = 0;
    markDirty_();
  }

  bool popupVisible() const { return visible_; }

  // The position where this overlay should be drawn in main-window
  // client coordinates. The scaffold calls renderAt() during paint.
  int overlayX() const { return originX_; }
  int overlayY() const { return originY_; }
  int overlayW() const { return popupW_; }
  int overlayH() const { return popupH_; }

  // Called by the scaffold's render pass for each overlay in the stack.
  // ctx is the main cairo_t* — we save/translate/clip/restore around the
  // derived widget's paint so it sees a local (0,0) origin, matching Win32.
  void renderOverlay(GraphicsContext &ctx, FontCache &fc) {
    if (!visible_ || popupW_ <= 0 || popupH_ <= 0)
      return;

    cairo_t *cr = ctx.cr;
    cairo_save(cr);

    // Clip to popup bounds so nothing bleeds outside.
    cairo_rectangle(cr, originX_, originY_, popupW_, popupH_);
    cairo_clip(cr);

    // Translate so renderPopupContent sees (0,0) at the popup's origin,
    // matching the Win32 DIB coordinate system.
    cairo_translate(cr, originX_, originY_);

    // Build a local GraphicsContext with the translated cr.
    GraphicsContext localCtx(cr, popupW_, popupH_);
    renderPopupContent(localCtx, fc);

    cairo_restore(cr);
  }

#endif // _WIN32 / Linux

protected:
  int popupW_ = 0;
  int popupH_ = 0;

private:
  void destroyPopup() { hidePopup(); }

#ifdef _WIN32
  // ── Win32 internals ───────────────────────────────────────────────────
  HWND popupHwnd_ = nullptr;
  HDC dibDC_ = nullptr;
  HBITMAP dibBmp_ = nullptr;
  HBITMAP dibOld_ = nullptr;
  void *dibBits_ = nullptr;

  void ensureDIB_(int w, int h) {
    if (dibDC_ && dibBmp_ && popupW_ == w && popupH_ == h)
      return;
    freeDIB_();

    BITMAPINFOHEADER bih{};
    bih.biSize = sizeof(bih);
    bih.biWidth = w;
    bih.biHeight = -h;
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
      dibDC_ =  nullptr;
      dibOld_ = nullptr;
    }
    if (dibBmp_) {
      DeleteObject(dibBmp_);
      dibBmp_ = nullptr;
    }
    dibBits_ = nullptr;
  }

  static constexpr COLORREF kClearKey_ = RGB(3, 2, 1);

  void paintLayered_(FontCache &fontCache) {
    if (!popupHwnd_ || popupW_ <= 0 || popupH_ <= 0)
      return;
    ensureDIB_(popupW_, popupH_);

    {
      HBRUSH clearBrush = CreateSolidBrush(kClearKey_);
      RECT all = {0, 0, popupW_, popupH_};
      FillRect(dibDC_, &all, clearBrush);
      DeleteObject(clearBrush);
    }

    GraphicsContext ctx(dibDC_);
    renderPopupContent(ctx, fontCache);
    fixupAlpha_();

    SIZE dstSz{popupW_, popupH_};
    POINT srcPt{0, 0};
    BLENDFUNCTION bf{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    UpdateLayeredWindow(popupHwnd_, nullptr, nullptr, &dstSz, dibDC_, &srcPt, 0,
                        &bf, ULW_ALPHA);
  }

  void fixupAlpha_() {
    if (!dibBits_)
      return;
    auto *px = static_cast<BYTE *>(dibBits_);
    int n = popupW_ * popupH_;
    for (int i = 0; i < n; ++i, px += 4) {
      if (px[0] == 1 && px[1] == 2 && px[2] == 3)
        px[0] = px[1] = px[2] = px[3] = 0;
      else
        px[3] = 255;
    }
  }

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

#else // Linux internals ───────────────────────────────────────────────────────

  int originX_ = 0;
  int originY_ = 0;
  bool visible_ = false;

  void markDirty_() {
    // FluxUI::getCurrentInstance() is always available when overlays
    // are active — the scaffold holds us in its stack.
    auto *ui = FluxUI::getCurrentInstance();
    if (ui)
      ui->invalidateWidget(originX_, originY_, popupW_, popupH_);
  }

#endif
};

#ifdef _WIN32
inline bool OverlayHost::popupClass_ = false;
#endif

#endif // FLUX_OVERLAY_HOST_HPP