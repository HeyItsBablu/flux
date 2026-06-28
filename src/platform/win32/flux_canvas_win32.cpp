// flux_canvas_win32.cpp
// Win32 CanvasWidget implementation using Direct2D.
// Replaces flux_canvas_win32.cpp entirely — no GL, no child HWND, no WGL.
//
// Architecture:
//   CanvasWidget::render() is called by the widget tree on the render thread.
//   It drives Canvas2D (off-screen D2D bitmap) → surface renders into it →
//   the bitmap is composited into the main D2D device context with the correct
//   viewport transform applied.
//
// Scrollbars are drawn with the same D2D primitives as Painter, using the
//   existing CustomScrollbar geometry helpers (thumbRect / trackRect).
#ifdef _WIN32

#include "flux/flux_canvas.hpp"
#include "flux/flux_canvas2d.hpp"
#include "flux/flux_canvas2d_backend.hpp"
#include "flux/flux_d3d_device.hpp"
#include "flux/flux_core.hpp" // FluxUI::getCurrentInstance()

#include <d2d1_3.h>
#include <d2d1_1helper.h>
#include <wrl/client.h>
#include <cassert>
#include <cmath>
#include <algorithm>

using Microsoft::WRL::ComPtr;

// ─────────────────────────────────────────────────────────────────────────────
// Per-widget D2D state (stored in a parallel map, same pattern as before)
// ─────────────────────────────────────────────────────────────────────────────

#include <unordered_map>

struct Win32D2DCanvasState
{
    Canvas2DD2D *d2d = nullptr;           // owned by the widget — D2D resources
    Canvas2DBackend *backend = nullptr;   // owned by the widget — wraps d2d for Canvas2D
    bool inited = false;
    int lastW = 0;
    int lastH = 0;
};

static std::unordered_map<CanvasWidget *, Win32D2DCanvasState> s_d2dState;

static Win32D2DCanvasState &d2dState(CanvasWidget *w)
{
    return s_d2dState[w];
}

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

static inline D2D1_COLOR_F rgba(float r, float g, float b, float a)
{
    return D2D1::ColorF(r, g, b, a);
}

static D3DDevice *getDevice()
{
    auto *inst = FluxUI::getCurrentInstance();
    if (!inst)
        return nullptr;
    auto *win = inst->getPlatformWindowPtr();
    if (!win)
        return nullptr;
    return win->getD3DDevice();
}

static ID2D1DeviceContext *mainDC()
{
    auto *inst = FluxUI::getCurrentInstance();
    if (!inst)
        return nullptr;
    auto ctx = inst->getPlatformWindowPtr()->getD2DContext();
    return ctx.dc;
}

// ─────────────────────────────────────────────────────────────────────────────
// Ensure Canvas2D is initialised and bitmap is the right size
// ─────────────────────────────────────────────────────────────────────────────

static void ensureD2D(CanvasWidget *w, GraphicsContext &gctx)
{
    auto &s = d2dState(w);
    if (!s.d2d)
    {
        D3DDevice *dev = getDevice();
        if (!dev || !dev->valid)
            return;

        s.d2d = new Canvas2DD2D();
        if (!s.d2d->init(dev))
        {
            delete s.d2d;
            s.d2d = nullptr;
            return;
        }

        s.backend = Canvas2DBackend::create(s.d2d);
        if (!s.backend)
        {
            s.d2d->destroy();
            delete s.d2d;
            s.d2d = nullptr;
            return;
        }

        // Register default fonts (same set as the GL path)
        auto reg = [&](const char *name, const char *path)
        {
            Canvas2D::registerFont(s.backend, name, path);
        };
        reg("sans", "C:\\Windows\\Fonts\\segoeui.ttf");
        reg("sans-bold", "C:\\Windows\\Fonts\\segoeuib.ttf");
        reg("sans-italic", "C:\\Windows\\Fonts\\segoeuii.ttf");
        reg("sans-bold-italic", "C:\\Windows\\Fonts\\segoeuiz.ttf");
        reg("mono", "C:\\Windows\\Fonts\\consola.ttf");
        reg("mono-bold", "C:\\Windows\\Fonts\\consolab.ttf");

        s.inited = true;
    }

    // Resize off-screen bitmap if needed
    int docW = w->docW(), docH = w->docH();
    if (docW != s.lastW || docH != s.lastH || !s.d2d->offscreenBitmap)
    {
        s.d2d->resizeBitmap(docW, docH);
        s.lastW = docW;
        s.lastH = docH;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Destroy GL state (called from destructor / onDetach)
// ─────────────────────────────────────────────────────────────────────────────

static void destroyD2D(CanvasWidget *w)
{
    auto it = s_d2dState.find(w);
    if (it == s_d2dState.end())
        return;

    auto &s = it->second;
    if (w->activeSurface_)
    {
        w->activeSurface_->destroy();
        w->activeSurface_.reset();
    }
    w->pendingSurface_.reset();

    if (s.backend)
    {
        Canvas2DBackend::destroy(s.backend);
        s.backend = nullptr;
    }
    if (s.d2d)
    {
        s.d2d->destroy();
        delete s.d2d;
        s.d2d = nullptr;
    }
    s_d2dState.erase(it);
}

// ─────────────────────────────────────────────────────────────────────────────
// activatePendingSurface
// ─────────────────────────────────────────────────────────────────────────────

void CanvasWidget::activatePendingSurface()
{
    if (!pendingSurface_)
        return;
    if (activeSurface_)
    {
        activeSurface_->destroy();
        activeSurface_.reset();
    }
    activeSurface_ = pendingSurface_;
    pendingSurface_.reset();
    activeSurface_->initialize(docW(), docH());
    vp_.setCanvasSize(docW(), docH());
}

// ─────────────────────────────────────────────────────────────────────────────
// Draw scrollbars into the main DC using D2D primitives
// ─────────────────────────────────────────────────────────────────────────────

static void drawScrollbarD2D(ID2D1DeviceContext *dc,
                             ID2D1Factory1 *factory,
                             const CustomScrollbar &bar,
                             int glW, int glH,
                             float alpha)
{
    if (!bar.isVisible() || alpha < 0.005f)
        return;

    // ── Track ────────────────────────────────────────────────────────────────
    {
        auto [tx, ty, tw, th] = bar.trackRect(glW, glH);
        D2D1_RECT_F r = D2D1::RectF(tx, ty, tx + tw, ty + th);
        ComPtr<ID2D1SolidColorBrush> br;
        dc->CreateSolidColorBrush(rgba(0.08f, 0.08f, 0.08f, alpha * 0.3f),
                                  br.GetAddressOf());
        if (br)
            dc->FillRectangle(r, br.Get());
    }

    // ── Thumb ─────────────────────────────────────────────────────────────────
    {
        auto [tx, ty, tw, th] = bar.thumbRect(glW, glH);
        float r = std::min(tw, th) * 0.5f;
        D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(
            D2D1::RectF(tx, ty, tx + tw, ty + th), r, r);
        float tc = 0.76f;
        ComPtr<ID2D1SolidColorBrush> br;
        dc->CreateSolidColorBrush(rgba(tc, tc, tc, alpha), br.GetAddressOf());
        if (br)
            dc->FillRoundedRectangle(rr, br.Get());
    }
}

static void drawScrollbarCornerD2D(ID2D1DeviceContext *dc,
                                   const CustomScrollbar &hBar,
                                   const CustomScrollbar &vBar,
                                   int glW, int glH)
{
    if (!hBar.isVisible() || !vBar.isVisible())
        return;
    float thick = CustomScrollbar::kTrackThick;
    D2D1_RECT_F r = D2D1::RectF(float(glW) - thick, float(glH) - thick,
                                float(glW), float(glH));
    ComPtr<ID2D1SolidColorBrush> br;
    dc->CreateSolidColorBrush(rgba(0.12f, 0.12f, 0.12f, 0.35f), br.GetAddressOf());
    if (br)
        dc->FillRectangle(r, br.Get());
}

// ─────────────────────────────────────────────────────────────────────────────
// Drop shadow (drawn before compositing the canvas bitmap)
// ─────────────────────────────────────────────────────────────────────────────

static void drawDropShadow(ID2D1DeviceContext *dc,
                           ID2D1Factory1 *factory,
                           float ox, float oy, float cw, float ch)
{
    const float kOffX = 6.f, kOffY = 6.f;
    const int kLayers = 4;
    const float kBaseAlpha = 0.18f;

    for (int i = kLayers; i >= 1; --i)
    {
        float spread = float(i) * 2.f;
        float alpha = kBaseAlpha * (1.f - float(i - 1) / float(kLayers));
        D2D1_RECT_F r = D2D1::RectF(
            ox + kOffX - spread,
            oy + kOffY - spread,
            ox + kOffX + cw + spread,
            oy + kOffY + ch + spread);

        ComPtr<ID2D1SolidColorBrush> br;
        dc->CreateSolidColorBrush(rgba(0, 0, 0, alpha), br.GetAddressOf());
        if (br)
            dc->FillRectangle(r, br.Get());
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// tickAndRender — main per-frame render function
// ─────────────────────────────────────────────────────────────────────────────

static void tickAndRender(CanvasWidget *w, GraphicsContext &gctx)
{
    auto &s = d2dState(w);
    if (!s.d2d || !s.backend || !s.d2d->offscreenBitmap)
        return;

    if (w->pendingSurface_)
        w->activatePendingSurface();
    if (!w->activeSurface_)
        return;

    // Timing
    auto now = CanvasWidget::Clock::now();
    double dt = std::chrono::duration<double>(now - w->lastTick_).count();
    w->lastTick_ = now;
    w->activeSurface_->update(dt);

    // ── Render surface into off-screen bitmap ─────────────────────────────
    auto *offDC = s.d2d->offscreenDC.Get();
    offDC->BeginDraw();
    offDC->Clear(D2D1::ColorF(D2D1::ColorF::White));
    offDC->SetTransform(D2D1::Matrix3x2F::Identity());
    w->activeSurface_->preRender();
    {
        Canvas2D ctx(s.backend, w->docW(), w->docH());
        w->activeSurface_->render(ctx);
    }
    offDC->EndDraw();

    // ── Composite into main DC ────────────────────────────────────────────
    ID2D1DeviceContext *dc = gctx.dc;
    ID2D1Factory1 *factory = gctx.factory;
    if (!dc)
        return;

    int vpW = w->width, vpH = w->height;

    float zoom = w->vp_.zoom();
    float offX = float(w->x) - w->vp_.offsetX() * zoom;
    float offY = float(w->y) - w->vp_.offsetY() * zoom;

    D2D1_MATRIX_3X2_F prevTransform;
    dc->GetTransform(&prevTransform);

    // 1. Push clip in current (window) space BEFORE changing transform
    dc->PushAxisAlignedClip(
        D2D1::RectF(float(w->x), float(w->y),
                    float(w->x + vpW), float(w->y + vpH)),
        D2D1_ANTIALIAS_MODE_ALIASED);

    // 2. Now set the viewport transform
    // screenPt = canvasPt * zoom + (offX, offY)
    D2D1_MATRIX_3X2_F vpTransform =
        D2D1::Matrix3x2F::Scale(zoom, zoom) *
        D2D1::Matrix3x2F::Translation(offX, offY);
    dc->SetTransform(vpTransform * prevTransform);

    // 3. Draw the bitmap in canvas space (0,0 → docW,docH)
    D2D1_RECT_F docRect = D2D1::RectF(0, 0,
                                      float(w->docW()), float(w->docH()));

    // Fill widget background (the "mat" around the canvas document)
    dc->SetTransform(prevTransform);
    {
        ComPtr<ID2D1SolidColorBrush> matBrush;
        dc->CreateSolidColorBrush(D2D1::ColorF(0.78f, 0.78f, 0.78f, 1.f), matBrush.GetAddressOf());
        if (matBrush)
            dc->FillRectangle(
                D2D1::RectF(float(w->x), float(w->y),
                            float(w->x + vpW), float(w->y + vpH)),
                matBrush.Get());
    }
    // Then restore the viewport transform before drawing the bitmap
    dc->SetTransform(vpTransform * prevTransform);
    dc->DrawBitmap(s.d2d->offscreenBitmap.Get(), &docRect, 1.f,
                   D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);

    dc->PopAxisAlignedClip();
    dc->SetTransform(prevTransform);

    // ── Scrollbars ────────────────────────────────────────────────────────
    if (w->scrollbarsEnabled_ && w->viewportEnabled_)
    {
        w->updateSBGeometry(vpW, vpH);
        double dt2 = dt;
        w->hBar_.tick(dt2);
        w->vBar_.tick(dt2);

        D2D1_MATRIX_3X2_F sbTransform =
            D2D1::Matrix3x2F::Translation(float(w->x), float(w->y));
        dc->SetTransform(sbTransform * prevTransform);

        dc->PushAxisAlignedClip(
            D2D1::RectF(float(w->x), float(w->y),
                        float(w->x + vpW), float(w->y + vpH)),
            D2D1_ANTIALIAS_MODE_ALIASED);

        drawScrollbarD2D(dc, factory, w->hBar_, vpW, vpH, w->hBar_.alpha());
        drawScrollbarD2D(dc, factory, w->vBar_, vpW, vpH, w->vBar_.alpha());
        drawScrollbarCornerD2D(dc, w->hBar_, w->vBar_, vpW, vpH);

        dc->PopAxisAlignedClip();
        dc->SetTransform(prevTransform);
    }

    w->pokeScrollbars();
}

// ─────────────────────────────────────────────────────────────────────────────
// CanvasWidget member implementations (Win32 D2D)
// ─────────────────────────────────────────────────────────────────────────────

CanvasWidget::CanvasWidget()
    : hBar_(CustomScrollbar::Axis::Horizontal),
      vBar_(CustomScrollbar::Axis::Vertical)
{
    autoWidth = true;
    autoHeight = true;
    width = 400;
    height = 300;
    isFocusable = true;
}

CanvasWidget::~CanvasWidget()
{
    destroyD2D(this);
}

std::shared_ptr<CanvasWidget> CanvasWidget::setViewportEnabled(bool e)
{
    viewportEnabled_ = e;
    markNeedsPaint();
    return ptr();
}

std::shared_ptr<CanvasWidget> CanvasWidget::setScrollbarsEnabled(bool e)
{
    scrollbarsEnabled_ = e;
    markNeedsPaint();
    return ptr();
}

bool CanvasWidget::scrollbarsEnabled() const { return scrollbarsEnabled_; }

RenderSurface *CanvasWidget::getSurface() const { return activeSurface_.get(); }
const Viewport &CanvasWidget::viewport() const { return vp_; }
Viewport &CanvasWidget::viewport() { return vp_; }

std::shared_ptr<CanvasWidget> CanvasWidget::setSize(int w, int h)
{
    width = w;
    height = h;
    autoWidth = autoHeight = false;
    markNeedsLayout();
    return ptr();
}

std::shared_ptr<CanvasWidget> CanvasWidget::setCanvasSize(int w, int h)
{
    docW_ = w;
    docH_ = h;
    vp_.setCanvasSize(w, h);
    auto &s = d2dState(this);
    if (s.d2d)
    {
        s.d2d->resizeBitmap(w, h);
        s.lastW = w;
        s.lastH = h;
        if (activeSurface_)
            activeSurface_->resize(w, h);
    }
    markNeedsPaint();
    return ptr();
}

std::shared_ptr<CanvasWidget> CanvasWidget::redraw()
{
    markNeedsPaint();
    return ptr();
}

void CanvasWidget::computeLayout(GraphicsContext & /*ctx*/,
                                 const BoxConstraints &c, FontCache &)
{
    if (autoWidth)
        width = (c.maxWidth < kUnbounded) ? c.maxWidth : width;
    if (autoHeight)
        height = (c.maxHeight < kUnbounded) ? c.maxHeight : height;
    canvasW_ = width;
    canvasH_ = height;
    needsLayout = false;
}

void CanvasWidget::render(GraphicsContext &ctx, FontCache &)
{
    // Lazy init
    ensureD2D(this, ctx);

    auto &s = d2dState(this);
    if (!s.d2d || !s.backend)
        return;

    // First-time viewport init
    if (!s.inited || vp_.viewW() != float(width) || vp_.viewH() != float(height))
    {
        vp_.init(width, height, docW(), docH());
        vp_.fitToView();
        updateViewportSize(width, height);
        updateSBGeometry(width, height);
        if (onViewportChanged)
            onViewportChanged(vp_.zoom());
        if (onGLResize)
            onGLResize(width, height);
        s.inited = true;
    }

    tickAndRender(this, ctx);
    needsPaint = false;
}

void CanvasWidget::onDetach()
{
    destroyD2D(this);
    Widget::onDetach();
}

void CanvasWidget::markNeedsPaint()
{
    Widget::markNeedsPaint();
    auto *inst = FluxUI::getCurrentInstance();
    if (inst)
        inst->getPlatformWindowPtr()->invalidate();
}

// ─────────────────────────────────────────────────────────────────────────────
// Shared viewport / scrollbar helpers (identical logic to GL version)
// ─────────────────────────────────────────────────────────────────────────────

void CanvasWidget::viewportDims(int glW, int glH, int &vpW, int &vpH) const
{
    if (!viewportEnabled_ || !scrollbarsEnabled_)
    {
        vpW = glW;
        vpH = glH;
        return;
    }
    ScrollbarInfo h = vp_.scrollbarH(), v = vp_.scrollbarV();
    vpW = glW - (v.visible ? (int)kSBThick : 0);
    vpH = glH - (h.visible ? (int)kSBThick : 0);
    if (vpW < 1)
        vpW = 1;
    if (vpH < 1)
        vpH = 1;
}

void CanvasWidget::updateViewportSize(int glW, int glH)
{
    int vpW, vpH;
    viewportDims(glW, glH, vpW, vpH);
    vp_.setViewSize(vpW, vpH);
}

void CanvasWidget::updateSBGeometry(int glW, int glH)
{
    ScrollbarInfo h = vp_.scrollbarH(), v = vp_.scrollbarV();
    float hLen = float(glW) - (v.visible ? kSBThick : 0.f);
    hBar_.setGeometry(0.f, float(glH) - kSBThick, hLen);
    hBar_.setThumb(h.thumbMin, h.thumbMax,
                   h.visible && scrollbarsEnabled_ && viewportEnabled_);
    float vLen = float(glH) - (h.visible ? kSBThick : 0.f);
    vBar_.setGeometry(float(glW) - kSBThick, 0.f, vLen);
    vBar_.setThumb(v.thumbMin, v.thumbMax,
                   v.visible && scrollbarsEnabled_ && viewportEnabled_);
}

void CanvasWidget::beginPan(int sx, int sy)
{
    panning_ = true;
    panStartSX_ = sx;
    panStartSY_ = sy;
    panStartOX_ = vp_.offsetX();
    panStartOY_ = vp_.offsetY();
}

void CanvasWidget::continuePan(int sx, int sy)
{
    float dx = float(sx - panStartSX_), dy = float(sy - panStartSY_);
    vp_.setOffset(panStartOX_ - dx / vp_.zoom(),
                  panStartOY_ + dy / vp_.zoom());
    pokeScrollbars();
    markNeedsPaint();
}

void CanvasWidget::pokeScrollbars()
{
    hBar_.poke();
    vBar_.poke();
}

void CanvasWidget::applyHScrollFraction(float thumbMin)
{
    float span = vp_.scrollbarH().thumbMax - vp_.scrollbarH().thumbMin;
    float range = vp_.canvasW() - vp_.viewW() / vp_.zoom();
    if (range <= 0.f)
        return;
    vp_.setOffsetX(thumbMin / (1.f - span) * range);
    pokeScrollbars();
    markNeedsPaint();
}

void CanvasWidget::applyVScrollFraction(float thumbMin)
{
    float span = vp_.scrollbarV().thumbMax - vp_.scrollbarV().thumbMin;
    float range = vp_.canvasH() - vp_.viewH() / vp_.zoom();
    if (range <= 0.f)
        return;
    float t = 1.f - thumbMin - span;
    vp_.setOffsetY(t / (1.f - span) * range);
    pokeScrollbars();
    markNeedsPaint();
}

// ─────────────────────────────────────────────────────────────────────────────
// Input handling
// Input coordinates arrive in window space from PlatformWindow's WndProc.
// CanvasWidget receives them via the widget tree hit-test and translates
// them to canvas space using the viewport.
// ─────────────────────────────────────────────────────────────────────────────

// These are called by the widget dispatch layer (not WndProc directly).
// The (sx, sy) coords are already widget-local (0,0 = top-left of widget).

bool CanvasWidget::handleMouseDown(int sx, int sy)
{
    int localX = sx - x;
    int localY = sy - y;

    bool hC = hBar_.onMouseDown(localX, localY, [this](float t)
                                { applyHScrollFraction(t); });
    bool vC = !hC && vBar_.onMouseDown(localX, localY, [this](float t)
                                       { applyVScrollFraction(t); });

    if (!hC && !vC)
    {
        if (viewportEnabled_ && panning_)
        {
            beginPan(localX, localY);
        }
        else if (activeSurface_)
        {
            auto [cx, cy] = vp_.screenToCanvas(float(localX), float(localY));
            activeSurface_->onMouseDown(cx, cy);
        }
    }
    markNeedsPaint();
    return true;
}

bool CanvasWidget::handleMouseMove(int sx, int sy)
{
    int localX = sx - x;
    int localY = sy - y;

    bool hDrag = hBar_.onMouseMove(localX, localY);
    bool vDrag = vBar_.onMouseMove(localX, localY);
    markNeedsPaint();

    if (hDrag || vDrag)
        return true;

    if (panning_)
        continuePan(localX, localY);
    else if (activeSurface_)
    {
        auto [cx, cy] = vp_.screenToCanvas(float(localX), float(localY));
        activeSurface_->onMouseMove(cx, cy);
    }
    return true;
}

bool CanvasWidget::handleMouseUp(int sx, int sy)
{
    int localX = sx - x;
    int localY = sy - y;

    bool hR = hBar_.onMouseUp(localX, localY);
    bool vR = vBar_.onMouseUp(localX, localY);
    if (!hR && !vR)
    {
        if (panning_)
            panning_ = false;
        else if (activeSurface_)
        {
            auto [cx, cy] = vp_.screenToCanvas(float(localX), float(localY));
            activeSurface_->onMouseUp(cx, cy);
        }
    }
    markNeedsPaint();
    return true;
}

bool CanvasWidget::handleMouseWheel(int delta)
{
    if (!viewportEnabled_)
        return false;
    bool ctrl = platformCtrlDown();
    bool shift = platformShiftDown();
    if (ctrl)
    {
        float f = (delta > 0) ? 1.1f : 1.f / 1.1f;
        // Zoom toward widget centre
        vp_.zoomToward(float(width) * 0.5f, float(height) * 0.5f, f);
    }
    else if (shift)
    {
        vp_.panByScreen(float(delta) * 0.5f, 0.f);
    }
    else
    {
        vp_.panByScreen(0.f, -(float(delta) / WHEEL_DELTA) * 40.f);
    }
    pokeScrollbars();
    if (onViewportChanged)
        onViewportChanged(vp_.zoom());
    markNeedsPaint();
    return true;
}

bool CanvasWidget::handleChar(wchar_t ch)
{
    if (!activeSurface_)
        return false;
    if (ch < 32 || ch == 127)
        return false; // control chars handled by keydown

    KeyEvent ke{};
    ke.codepoint = int(ch);
    ke.virtualKey = 0;
    ke.ctrl = platformCtrlDown();
    ke.shift = platformShiftDown();
    ke.alt = platformAltDown();
    activeSurface_->onKeyDown(ke);
    markNeedsPaint();
    return true;
}

bool CanvasWidget::handleKeyDown(int keyCode)
{
    bool ctrl = platformCtrlDown();
    bool shift = platformShiftDown();
    bool alt = platformAltDown();

    if (ctrl && viewportEnabled_)
    {
        // +/- zoom, 0 = reset
        if (keyCode == VK_OEM_PLUS || keyCode == VK_ADD)
        {
            vp_.zoomIn();
            pokeScrollbars();
            markNeedsPaint();
            return true;
        }
        if (keyCode == VK_OEM_MINUS || keyCode == VK_SUBTRACT)
        {
            vp_.zoomOut();
            pokeScrollbars();
            markNeedsPaint();
            return true;
        }
        if (keyCode == '0')
        {
            vp_.resetZoom();
            pokeScrollbars();
            markNeedsPaint();
            return true;
        }
    }

    if (activeSurface_)
    {
        KeyEvent ke{};
        ke.codepoint = 0;
        ke.virtualKey = keyCode;
        ke.ctrl = ctrl;
        ke.shift = shift;
        ke.alt = alt;
        activeSurface_->onKeyDown(ke);
        markNeedsPaint();
    }
    return false;
}


#endif // _WIN32