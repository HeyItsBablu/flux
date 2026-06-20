#include "flux/flux_overlay_manager.hpp"
#include "flux/flux_core.hpp"
#include <algorithm>

#ifdef __ANDROID__
#include "nanovg.h"
extern NVGcontext *FluxAndroid_getVG();
extern float FluxAndroid_getDpiScale();
#endif

// ============================================================================
// Entry — one open overlay's bookkeeping + platform popup surface.
// Reuses the exact same Win32 layered-window/DIB technique and the same
// non-Win32 clip+translate render technique that used to live inside
// OverlayHost — just owned by the manager instead of mixed into the widget.
// ============================================================================

struct OverlayManager::Entry
{
    OverlayContent *content = nullptr;
    int zIndex = 0;
    bool pendingRemoval = false;

    // Client-space rect — used for hit-testing on every platform.
    int clientX = 0, clientY = 0, w = 0, h = 0;

#ifdef _WIN32
    HWND popupHwnd_ = nullptr;
    HDC dibDC_ = nullptr;
    HBITMAP dibBmp_ = nullptr;
    HBITMAP dibOld_ = nullptr;
    void *dibBits_ = nullptr;

    static constexpr COLORREF kClearKey_ = RGB(3, 2, 1);
    static constexpr const wchar_t *kPopupClass_ = L"FluxOverlayPopup_v3";
    static bool popupClassRegistered_;

    bool visible() const { return popupHwnd_ != nullptr; }

    static void registerClass_()
    {
        if (popupClassRegistered_)
            return;
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = popupProc_;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = kPopupClass_;
        RegisterClassExW(&wc);
        popupClassRegistered_ = true;
    }

    static void forwardMouseEvent_(HWND popupHwnd, UINT msg, WPARAM wp, LPARAM lp)
    {
        HWND owner = GetWindow(popupHwnd, GW_OWNER);
        if (!owner)
            return;
        POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        MapWindowPoints(popupHwnd, owner, &pt, 1);
        SendMessageW(owner, msg, wp, MAKELPARAM(pt.x, pt.y));
    }

    static LRESULT CALLBACK popupProc_(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
    {
        switch (msg)
        {
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
        {
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
            forwardMouseEvent_(hwnd, msg, wp, lp);
            return 0;
        case WM_NCHITTEST:
            return HTCLIENT;
        default:
            return DefWindowProcW(hwnd, msg, wp, lp);
        }
    }

    void ensureDIB_(int dw, int dh)
    {
        if (dibDC_ && dibBmp_ && w == dw && h == dh)
            return;
        freeDIB_();
        BITMAPINFOHEADER bih{};
        bih.biSize = sizeof(bih);
        bih.biWidth = dw;
        bih.biHeight = -dh;
        bih.biPlanes = 1;
        bih.biBitCount = 32;
        bih.biCompression = BI_RGB;
        BITMAPINFO bi{};
        bi.bmiHeader = bih;
        HDC screenDC = GetDC(nullptr);
        dibBmp_ = CreateDIBSection(screenDC, &bi, DIB_RGB_COLORS, &dibBits_, nullptr, 0);
        ReleaseDC(nullptr, screenDC);
        dibDC_ = CreateCompatibleDC(nullptr);
        dibOld_ = static_cast<HBITMAP>(SelectObject(dibDC_, dibBmp_));
    }

    void freeDIB_()
    {
        if (dibDC_)
        {
            SelectObject(dibDC_, dibOld_);
            DeleteDC(dibDC_);
            dibDC_ = nullptr;
            dibOld_ = nullptr;
        }
        if (dibBmp_)
        {
            DeleteObject(dibBmp_);
            dibBmp_ = nullptr;
        }
        dibBits_ = nullptr;
    }

    void fixupAlpha_()
    {
        if (!dibBits_)
            return;
        auto *px = static_cast<BYTE *>(dibBits_);
        const int n = w * h;
        for (int i = 0; i < n; ++i, px += 4)
        {
            if (px[3] == 0)
            {
                if (px[0] == 1 && px[1] == 2 && px[2] == 3)
                    px[0] = px[1] = px[2] = px[3] = 0;
                else
                    px[3] = 255;
            }
        }
    }

    void paint(FontCache &fc)
    {
        if (!popupHwnd_ || w <= 0 || h <= 0)
            return;
        ensureDIB_(w, h);
        HBRUSH clearBrush = CreateSolidBrush(kClearKey_);
        RECT all = {0, 0, w, h};
        FillRect(dibDC_, &all, clearBrush);
        DeleteObject(clearBrush);
        GraphicsContext ctx(dibDC_);
        content->renderOverlay(ctx, fc);
        fixupAlpha_();
        SIZE dstSz{w, h};
        POINT srcPt{0, 0};
        BLENDFUNCTION bf{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        UpdateLayeredWindow(popupHwnd_, nullptr, nullptr, &dstSz, dibDC_, &srcPt, 0, &bf, ULW_ALPHA);
    }

    void destroyPopup()
    {
        if (popupHwnd_)
        {
            DestroyWindow(popupHwnd_);
            popupHwnd_ = nullptr;
        }
        freeDIB_();
    }

    ~Entry() { destroyPopup(); }
#else
    bool visible_ = false;
    bool visible() const { return visible_; }
    void destroyPopup() { visible_ = false; }
    ~Entry() = default;
#endif
};

#ifdef _WIN32
bool OverlayManager::Entry::popupClassRegistered_ = false;
#endif

OverlayManager::OverlayManager() = default;
OverlayManager::~OverlayManager() = default;

OverlayManager::Entry *OverlayManager::find(OverlayContent *content)
{
    for (auto &e : entries_)
        if (e->content == content && !e->pendingRemoval)
            return e.get();
    return nullptr;
}
const OverlayManager::Entry *OverlayManager::find(OverlayContent *content) const
{
    for (auto &e : entries_)
        if (e->content == content && !e->pendingRemoval)
            return e.get();
    return nullptr;
}

void OverlayManager::sortByZ()
{
    std::stable_sort(entries_.begin(), entries_.end(),
                     [](const std::unique_ptr<Entry> &a, const std::unique_ptr<Entry> &b)
                     {
                         return a->zIndex < b->zIndex;
                     });
}

struct OverlayManager::DispatchScope
{
    OverlayManager *mgr;
    explicit DispatchScope(OverlayManager *m) : mgr(m) { ++mgr->dispatchDepth_; }
    ~DispatchScope()
    {
        if (--mgr->dispatchDepth_ == 0)
            mgr->pruneRemoved_();
    }
};

void OverlayManager::pruneRemoved_()
{
    entries_.erase(std::remove_if(entries_.begin(), entries_.end(),
                                  [](const std::unique_ptr<Entry> &en)
                                  { return en->pendingRemoval; }),
                   entries_.end());
}

void OverlayManager::show(OverlayContent *content, int clientX, int clientY,
                          int w, int h, int zIndex, FontCache &fontCache)
{
    auto *ui = FluxUI::getCurrentInstance();
    if (!ui)
        return;

    Entry *e = find(content);
    if (!e)
    {
        entries_.push_back(std::make_unique<Entry>());
        e = entries_.back().get();
        e->content = content;
    }
    e->clientX = clientX;
    e->clientY = clientY;
    e->w = w;
    e->h = h;
    e->zIndex = zIndex;
    sortByZ();

#ifdef _WIN32
    auto sc = ui->clientToScreen(clientX, clientY);
    int screenX = sc.x, screenY = sc.y;

    POINT pt = {screenX, screenY};
    HMONITOR mon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(mon, &mi))
    {
        if (screenX + w > mi.rcWork.right)
            screenX = mi.rcWork.right - w;
        if (screenX < mi.rcWork.left)
            screenX = mi.rcWork.left;
        if (screenY + h > mi.rcWork.bottom)
            screenY = mi.rcWork.bottom - h;
        if (screenY < mi.rcWork.top)
            screenY = mi.rcWork.top;
    }

    if (!Entry::popupClassRegistered_)
        Entry::registerClass_();

    NativeWindow owner = ui->getWindow();
    if (!owner)
        return;

    if (!e->popupHwnd_)
    {
        e->popupHwnd_ = CreateWindowExW(
            WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
            Entry::kPopupClass_, nullptr, WS_POPUP,
            screenX, screenY, w, h, owner, nullptr,
            GetModuleHandleW(nullptr), nullptr);
    }
    else
    {
        SetWindowPos(e->popupHwnd_, HWND_TOPMOST, screenX, screenY, w, h,
                     SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }
    ShowWindow(e->popupHwnd_, SW_SHOWNOACTIVATE);
    e->paint(fontCache);
#else
    e->visible_ = true;
    ui->invalidateWidget(clientX, clientY, w, h);
#endif
}

void OverlayManager::hide(OverlayContent *content)
{
    Entry *e = find(content);
    if (!e)
        return;

    int x = e->clientX, y = e->clientY, w = e->w, h = e->h;
    e->destroyPopup();
    e->pendingRemoval = true;

    if (dispatchDepth_ == 0)
        pruneRemoved_();

    if (auto *ui = FluxUI::getCurrentInstance())
        ui->invalidateWidget(x, y, w, h);
}

void OverlayManager::refresh(OverlayContent *content, FontCache &fontCache)
{
    Entry *e = find(content);
    if (!e || !e->visible())
        return;
#ifdef _WIN32
    e->paint(fontCache);
#else
    if (auto *ui = FluxUI::getCurrentInstance())
        ui->invalidateWidget(e->clientX, e->clientY, e->w, e->h);
#endif
}

bool OverlayManager::isOpen(OverlayContent *content) const
{
    const Entry *e = find(content);
    return e && e->visible();
}

void OverlayManager::closeAll()
{
    for (auto &e : entries_)
    {
        e->destroyPopup();
        e->pendingRemoval = true;
    }
    if (dispatchDepth_ == 0)
        pruneRemoved_();
}

bool OverlayManager::hasBlockingOverlay() const
{
    for (auto &e : entries_)
        if (e->visible() && e->content->overlayPolicy().blocksHoverBelow)
            return true;
    return false;
}

bool OverlayManager::dispatchMouseDown(int clientX, int clientY)
{
    DispatchScope scope(this);
    for (auto it = entries_.rbegin(); it != entries_.rend(); ++it)
    {
        Entry *e = it->get();
        if (e->pendingRemoval || !e->visible())
            continue;
        bool inside = clientX >= e->clientX && clientX < e->clientX + e->w &&
                      clientY >= e->clientY && clientY < e->clientY + e->h;
        if (inside)
        {
            if (e->content->onOverlayMouseDown(clientX - e->clientX, clientY - e->clientY))
                return true;
        }
        else
        {
            e->content->onOverlayOutsideClick();
        }
        if (e->content->overlayPolicy().modal)
            return true;
    }
    return false;
}

bool OverlayManager::dispatchMouseUp(int clientX, int clientY)
{
    DispatchScope scope(this);
    for (auto it = entries_.rbegin(); it != entries_.rend(); ++it)
    {
        Entry *e = it->get();
        if (e->pendingRemoval || !e->visible())
            continue;
        if (e->content->onOverlayMouseUp(clientX - e->clientX, clientY - e->clientY))
            return true;
    }
    return false;
}

bool OverlayManager::dispatchMouseMove(int clientX, int clientY)
{
    DispatchScope scope(this);
    for (auto it = entries_.rbegin(); it != entries_.rend(); ++it)
    {
        Entry *e = it->get();
        if (e->pendingRemoval || !e->visible())
            continue;
        if (e->content->onOverlayMouseMove(clientX - e->clientX, clientY - e->clientY))
            return true;
    }
    return false;
}

bool OverlayManager::dispatchMouseWheel(int delta)
{
    DispatchScope scope(this);
    for (auto it = entries_.rbegin(); it != entries_.rend(); ++it)
    {
        Entry *e = it->get();
        if (e->pendingRemoval || !e->visible())
            continue;
        if (e->content->onOverlayMouseWheel(delta))
            return true;
    }
    return false;
}

bool OverlayManager::dispatchKeyDown(int keyCode)
{
    DispatchScope scope(this);
    for (auto it = entries_.rbegin(); it != entries_.rend(); ++it)
    {
        Entry *e = it->get();
        if (e->pendingRemoval || !e->visible())
            continue;
        if (e->content->overlayPolicy().capturesKeyboard)
            return e->content->onOverlayKeyDown(keyCode);
    }
    return false;
}

bool OverlayManager::dispatchRightClick(int clientX, int clientY)
{
    DispatchScope scope(this);
    for (auto it = entries_.rbegin(); it != entries_.rend(); ++it)
    {
        Entry *e = it->get();
        if (e->pendingRemoval || !e->visible())
            continue;
        if (e->content->onOverlayRightClick(clientX - e->clientX, clientY - e->clientY))
            return true;
    }
    return false;
}

void OverlayManager::renderAll(GraphicsContext &ctx, FontCache &fc)
{
#if !defined(_WIN32)
    for (auto &e : entries_)
    {
        if (!e->visible_ || e->w <= 0 || e->h <= 0)
            continue;

#if defined(__linux__) && !defined(__ANDROID__)
        cairo_t *cr = ctx.cr;
        cairo_save(cr);
        cairo_rectangle(cr, e->clientX, e->clientY, e->w, e->h);
        cairo_clip(cr);
        cairo_translate(cr, e->clientX, e->clientY);
        GraphicsContext localCtx(cr, e->w, e->h);
        e->content->renderOverlay(localCtx, fc);
        cairo_restore(cr);

#elif defined(__APPLE__)
        CGContextRef cg = ctx.cgContext;
        if (!cg)
            continue;
        CGContextSaveGState(cg);
        CGContextClipToRect(cg, CGRectMake(e->clientX, e->clientY, e->w, e->h));
        CGContextTranslateCTM(cg, e->clientX, e->clientY);
        GraphicsContext localCtx(cg, e->w, e->h);
        e->content->renderOverlay(localCtx, fc);
        CGContextRestoreGState(cg);

#elif defined(__EMSCRIPTEN__)
        EM_ASM({
            var c = Module._fluxCtx2D;
            if (!c) return;
            c.save();
            c.beginPath();
            c.rect($0, $1, $2, $3);
            c.clip();
            c.translate($0, $1); }, e->clientX, e->clientY, e->w, e->h);
        {
            GraphicsContext localCtx(e->w, e->h);
            e->content->renderOverlay(localCtx, fc);
        }
        EM_ASM({ var c = Module._fluxCtx2D; if (c) c.restore(); });

#else // Android / NanoVG
        NVGcontext *vg = FluxAndroid_getVG();
        if (!vg)
            continue;
        nvgSave(vg);
        float dpi = FluxAndroid_getDpiScale();
        nvgTranslate(vg, e->clientX * dpi, e->clientY * dpi);
        GraphicsContext localCtx(e->w, e->h);
        e->content->renderOverlay(localCtx, fc);
        nvgRestore(vg);
#endif
    }
#endif // !_WIN32
}