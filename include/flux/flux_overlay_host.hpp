#ifndef FLUX_OVERLAY_HOST_HPP
#define FLUX_OVERLAY_HOST_HPP

#include "flux_font.hpp"
#include "flux_platform.hpp"
#include <cassert>
#include <functional>

#ifdef __ANDROID__
#include "nanovg.h"
extern NVGcontext* FluxAndroid_getVG();
extern float       FluxAndroid_getDpiScale();
#endif

class ScaffoldWidget;

// ============================================================================
// OverlayHost
//
// Win32   : renders into a layered HWND popup (per-pixel alpha via DIB).
// Linux   : renders directly into the main Cairo surface via the scaffold's
//           overlay stack — no separate window needed.
// Android : renders directly into the current NanoVG frame via the scaffold's
//           overlay stack — no separate window needed.
//
// Derived classes implement renderPopupContent() identically on all
// platforms. The open/close/refresh plumbing differs per platform but
// the interface seen by ContextMenuWidget, DropdownWidget, ToastWidget,
// etc. is the same.
// ============================================================================

class OverlayHost {
public:
    virtual ~OverlayHost() { destroyPopup(); }

    // ── Interface every overlay widget must implement ─────────────────────
    virtual void setScaffold(ScaffoldWidget *s) = 0;

    // Paint overlay content into ctx.
    // Win32   : ctx wraps a DIB DC sized to (popupW x popupH), origin = (0,0).
    // Linux   : ctx wraps the main cairo_t*, origin = client coords of popup.
    // Android : ctx carries (popupW x popupH); NVG context is global to frame.
    virtual void renderPopupContent(GraphicsContext &/*ctx*/, FontCache &/*fc*/) {}

    // ── Popup lifecycle (called by derived open/close methods) ────────────

    // =========================================================================
    // Win32
    // =========================================================================
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
                kPopupClass_, nullptr, WS_POPUP,
                screenX, screenY, w, h,
                parentHwnd, nullptr,
                GetModuleHandleW(nullptr),
                static_cast<LPVOID>(this));
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

    static void forwardMouseEvent(HWND popupHwnd, UINT msg, WPARAM wp, LPARAM lp) {
        HWND owner = GetWindow(popupHwnd, GW_OWNER);
        if (!owner) return;
        POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        MapWindowPoints(popupHwnd, owner, &pt, 1);
        PostMessageW(owner, msg, wp, MAKELPARAM(pt.x, pt.y));
    }

    // =========================================================================
    // Linux (Cairo)
    // =========================================================================
#elif defined(__linux__) && !defined(__ANDROID__)

    // On Linux there is no separate popup window.
    // showPopup() records where the overlay should be drawn; the scaffold
    // calls renderOverlay() during the normal paint pass, passing a
    // GraphicsContext already translated to (originX_, originY_).

    void showPopup(NativeWindow /*parent*/,
                   int screenX, int screenY,
                   int w, int h,
                   FontCache & /*fontCache*/) {
        // Convert screen → client for Cairo rendering
        auto *ui = FluxUI::getCurrentInstance();
        if (ui) {
            auto client = ui->screenToClient(screenX, screenY);
            originX_ = client.x;
            originY_ = client.y;
        } else {
            originX_ = screenX;
            originY_ = screenY;
        }
        popupW_  = w;
        popupH_  = h;
        visible_ = true;
    }

    void refreshPopup(FontCache & /*fontCache*/) {
        markDirty_();
    }

    void hidePopup() {
        visible_ = false;
        popupW_ = popupH_ = 0;
        markDirty_();
    }

    bool popupVisible() const { return visible_; }

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

        GraphicsContext localCtx(cr, popupW_, popupH_);
        renderPopupContent(localCtx, fc);

        cairo_restore(cr);
    }

    // =========================================================================
    // Android (NanoVG)
    // =========================================================================
#elif defined(__ANDROID__)

    // On Android there is no separate popup window.
    // showPopup() records position; the scaffold calls renderOverlay() during
    // the NanoVG frame. Coordinates are already in client (window) space since
    // Android has no screen/client distinction for a full-screen activity.

    void showPopup(NativeWindow /*parent*/,
                   int screenX, int screenY,
                   int w, int h,
                   FontCache & /*fontCache*/) {
        originX_ = screenX;
        originY_ = screenY;
        popupW_  = w;
        popupH_  = h;
        visible_ = true;
    }

    void refreshPopup(FontCache & /*fontCache*/) {
        markDirty_();
    }

    void hidePopup() {
        visible_ = false;
        popupW_ = popupH_ = 0;
        markDirty_();
    }

    bool popupVisible() const { return visible_; }

    int overlayX() const { return originX_; }
    int overlayY() const { return originY_; }
    int overlayW() const { return popupW_; }
    int overlayH() const { return popupH_; }

    // Called by the scaffold's render pass.
    // The NanoVG context is global to the frame (accessed via
    // FluxAndroid_getVG() inside Painter). We pass a sized GraphicsContext
    // so renderPopupContent knows its canvas dimensions; the Painter
    // implementation handles the NVG state internally.
    // Unlike Cairo there is no save/translate/restore — derived widgets
    // must offset their draw calls by (originX_, originY_) themselves,
    // or Painter helpers accept absolute coordinates.
    void renderOverlay(GraphicsContext &ctx, FontCache &fc) {
        if (!visible_ || popupW_ <= 0 || popupH_ <= 0) return;

        NVGcontext* vg = FluxAndroid_getVG();
        if (!vg) return;

        nvgSave(vg);
        // originX_/originY_ are logical coords, but the current transform already
        // has nvgScale(dpi,dpi) applied — so we must NOT pass logical coords
        // directly to nvgTranslate or they get double-scaled.
        // Reset to identity first, then translate in physical pixels.
        float dpi = FluxAndroid_getDpiScale();
        nvgResetTransform(vg);
        nvgScale(vg, dpi, dpi);                          // re-apply base scale
        nvgTranslate(vg, static_cast<float>(originX_),   // now in logical space ✓
                     static_cast<float>(originY_));

        GraphicsContext localCtx(popupW_, popupH_);
        renderPopupContent(localCtx, fc);

        nvgRestore(vg);
    }


#endif // platform

protected:
    int popupW_ = 0;
    int popupH_ = 0;

private:
    void destroyPopup() { hidePopup(); }

    // =========================================================================
    // Win32 private internals
    // =========================================================================
#ifdef _WIN32

    HWND    popupHwnd_ = nullptr;
    HDC     dibDC_     = nullptr;
    HBITMAP dibBmp_    = nullptr;
    HBITMAP dibOld_    = nullptr;
    void   *dibBits_   = nullptr;

    void ensureDIB_(int w, int h) {
        if (dibDC_ && dibBmp_ && popupW_ == w && popupH_ == h)
            return;
        freeDIB_();

        BITMAPINFOHEADER bih{};
        bih.biSize        = sizeof(bih);
        bih.biWidth       = w;
        bih.biHeight      = -h;
        bih.biPlanes      = 1;
        bih.biBitCount    = 32;
        bih.biCompression = BI_RGB;

        BITMAPINFO bi{};
        bi.bmiHeader = bih;

        HDC screenDC = GetDC(nullptr);
        dibBmp_ = CreateDIBSection(screenDC, &bi, DIB_RGB_COLORS,
                                   &dibBits_, nullptr, 0);
        ReleaseDC(nullptr, screenDC);
        assert(dibBmp_);

        dibDC_  = CreateCompatibleDC(nullptr);
        dibOld_ = static_cast<HBITMAP>(SelectObject(dibDC_, dibBmp_));
    }

    void freeDIB_() {
        if (dibDC_) {
            SelectObject(dibDC_, dibOld_);
            DeleteDC(dibDC_);
            dibDC_  = nullptr;
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

        SIZE          dstSz{popupW_, popupH_};
        POINT         srcPt{0, 0};
        BLENDFUNCTION bf{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        UpdateLayeredWindow(popupHwnd_, nullptr, nullptr, &dstSz,
                            dibDC_, &srcPt, 0, &bf, ULW_ALPHA);
    }

    void fixupAlpha_() {
        if (!dibBits_) return;
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
        if (popupClass_) return;
        WNDCLASSEXW wc{};
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = popupProc_;
        wc.hInstance     = GetModuleHandleW(nullptr);
        wc.lpszClassName = kPopupClass_;
        RegisterClassExW(&wc);
        popupClass_ = true;
    }

    static LRESULT CALLBACK popupProc_(HWND hwnd, UINT msg,
                                        WPARAM wp, LPARAM lp) {
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

    // =========================================================================
    // Linux + Android shared private internals
    // =========================================================================
#else

    int  originX_ = 0;
    int  originY_ = 0;
    bool visible_ = false;

    void markDirty_() {
        auto *ui = FluxUI::getCurrentInstance();
        if (ui)
            ui->invalidateWidget(originX_, originY_, popupW_, popupH_);
    }

#endif // private internals
};

#ifdef _WIN32
inline bool OverlayHost::popupClass_ = false;
#endif

#endif // FLUX_OVERLAY_HOST_HPP