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
        SendMessageW(owner, msg, wp, MAKELPARAM(pt.x, pt.y));
    }

    // =========================================================================
    // Linux (Cairo) and Android (NanoVG) — shared non-Win32 interface
    // =========================================================================
#else // !_WIN32



    void showPopup(NativeWindow /*parent*/,
                   int screenX, int screenY,
                   int w, int h,
                   FontCache & /*fontCache*/) {
#if defined(__linux__) && !defined(__ANDROID__)
        // Convert screen → client for Cairo rendering.
        auto *ui = FluxUI::getCurrentInstance();
        if (ui) {
            auto client = ui->screenToClient(screenX, screenY);
            originX_ = client.x;
            originY_ = client.y;
        } else {
            originX_ = screenX;
            originY_ = screenY;
        }
#else

        auto *ui = FluxUI::getCurrentInstance();
        if (ui) {
            auto sz = ui->getClientSize();
            (void)sz; // used only in the asserts below
            assert(screenX <= sz.width  &&
                   "showPopup: screenX looks like physical px — pass logical px");
            assert(screenY <= sz.height &&
                   "showPopup: screenY looks like physical px — pass logical px");
        }
        originX_ = screenX;
        originY_ = screenY;
#endif
        popupW_  = w;
        popupH_  = h;
        visible_ = true;
    }

    void refreshPopup(FontCache & /*fontCache*/) {
        markDirty_();
    }


    void hidePopup() {
        int w = popupW_;
        int h = popupH_;
        visible_ = false;
        popupW_  = 0;
        popupH_  = 0;
        markDirty_(w, h);
    }

    bool popupVisible() const { return visible_; }

    int overlayX() const { return originX_; }
    int overlayY() const { return originY_; }
    int overlayW() const { return popupW_; }
    int overlayH() const { return popupH_; }

    // Called by the scaffold's render pass for each overlay in the stack.
    void renderOverlay(GraphicsContext &ctx, FontCache &fc) {
        if (!visible_ || popupW_ <= 0 || popupH_ <= 0)
            return;

#if defined(__linux__) && !defined(__ANDROID__)
        // Cairo: save/translate/clip/restore so renderPopupContent sees a
        // local (0,0) origin matching the Win32 DIB coordinate system.
        cairo_t *cr = ctx.cr;
        cairo_save(cr);

        cairo_rectangle(cr, originX_, originY_, popupW_, popupH_);
        cairo_clip(cr);
        cairo_translate(cr, originX_, originY_);

        GraphicsContext localCtx(cr, popupW_, popupH_);
        renderPopupContent(localCtx, fc);

        cairo_restore(cr);

#else // Android / NanoVG
        NVGcontext* vg = FluxAndroid_getVG();
        if (!vg) return;


        nvgSave(vg);

        float dpi = FluxAndroid_getDpiScale();
        nvgTranslate(vg,
                     static_cast<float>(originX_) * dpi,
                     static_cast<float>(originY_) * dpi);

        GraphicsContext localCtx(popupW_, popupH_);
        renderPopupContent(localCtx, fc);

        nvgRestore(vg);
#endif
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
        const int n = popupW_ * popupH_;
        for (int i = 0; i < n; ++i, px += 4) {
            // px layout: [0]=B [1]=G [2]=R [3]=A
            if (px[3] == 0) {
                if (px[0] == 1 && px[1] == 2 && px[2] == 3) {
                    // Sentinel clear-color → fully transparent (click-through).
                    px[0] = px[1] = px[2] = px[3] = 0;
                } else {
                    // GDI-drawn pixel — alpha was never written. Force opaque
                    // so it is both visible and receives mouse hit-testing.
                    px[3] = 255;
                }
            }
            // alpha > 0: renderer wrote a real alpha value — leave it alone.
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
#else // !_WIN32

    int  originX_ = 0;
    int  originY_ = 0;
    bool visible_ = false;


    void markDirty_(int w = -1, int h = -1) {
        auto *ui = FluxUI::getCurrentInstance();
        if (ui)
            ui->invalidateWidget(originX_, originY_,
                                 w >= 0 ? w : popupW_,
                                 h >= 0 ? h : popupH_);
    }

#endif // private internals
};

#ifdef _WIN32
inline bool OverlayHost::popupClass_ = false;
#endif

#endif // FLUX_OVERLAY_HOST_HPP