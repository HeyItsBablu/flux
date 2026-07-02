// src/flux_canvas_web.cpp
//
// CanvasWidget platform implementation for Emscripten / WebAssembly.
// (Header comment block unchanged from before — see architecture notes.)

#ifdef __EMSCRIPTEN__

#include "flux/flux_canvas.hpp"
#include "flux/flux_canvas2d.hpp"
#include "flux/flux_painter.hpp"

#include <emscripten.h>
#include <emscripten/html5.h>
#include <GLES3/gl3.h>

#include <cassert>
#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <algorithm>
#include <chrono>
#include <cmath>

// ── Canvas2DBackend lifecycle — defined in flux_canvas2d_gl.cpp.
// No shared header: this is the entire contract between the two files.
extern Canvas2DBackend *Canvas2DBackend_create();
extern void Canvas2DBackend_destroy(Canvas2DBackend *b);
extern void Canvas2DBackend_setMVP(Canvas2DBackend *b, const float mvp[16]);

// Column-major, maps [l,r] x [b,t] → NDC.
static void webOrtho(float l, float r, float b, float t, float out[16])
{
    float rml = r - l, tmb = t - b;
    out[0] = 2.f / rml;
    out[1] = 0;
    out[2] = 0;
    out[3] = 0;
    out[4] = 0;
    out[5] = 2.f / tmb;
    out[6] = 0;
    out[7] = 0;
    out[8] = 0;
    out[9] = 0;
    out[10] = -1;
    out[11] = 0;
    out[12] = -(r + l) / rml;
    out[13] = -(t + b) / tmb;
    out[14] = 0;
    out[15] = 1;
}

struct CanvasHoleRect
{
    int x, y, w, h;
};
static std::vector<CanvasHoleRect> s_holeRects;

void fluxClearCanvasWidgetRects()
{
    if (s_holeRects.empty())
        return;
    for (auto &r : s_holeRects)
    {
        EM_ASM({
            var ctx = Module._fluxCtx2D;
            if (!ctx) return;
            ctx.clearRect($0, $1, $2, $3); }, r.x, r.y, r.w, r.h);
    }
    s_holeRects.clear();
}

// ============================================================================
// Web-only per-widget state
// ============================================================================

struct WebCanvasState
{
    bool initialized = false;
    bool repaintPending = false;
    bool mouseCapture = false;
    EMSCRIPTEN_WEBGL_CONTEXT_HANDLE glCtx = 0;
    int lastX = 0, lastY = 0;
    int lastW = 0, lastH = 0;
};

static std::unordered_map<CanvasWidget *, WebCanvasState> s_webState;

static WebCanvasState &webState(CanvasWidget *w) { return s_webState[w]; }

// ============================================================================
// Helpers
// ============================================================================

static void scheduleRepaint(CanvasWidget *w)
{
    if (w->onViewportChanged)
        w->onViewportChanged(w->vp_.zoom());
    webState(w).repaintPending = true;
    if (auto *ui = FluxUI::getCurrentInstance())
        ui->getPlatformWindow().invalidate();
}

// ── Shared Canvas2DBackend (font atlas, shaders, VAO/VBO) ────────────────────
// All CanvasWidgets on the page share ONE backend — refcounted, created on
// first use, destroyed when the last widget releases it. Each widget just
// overwrites the shared backend's MVP immediately before rendering, which is
// safe because widgets render sequentially on the same thread each frame.

static Canvas2DBackend *s_sharedBackend = nullptr;
static int s_sharedBackendUsers = 0;

static EMSCRIPTEN_WEBGL_CONTEXT_HANDLE s_glCtx = 0;

extern "C" EMSCRIPTEN_WEBGL_CONTEXT_HANDLE fluxGetGLContext()
{
    return s_glCtx;
}

static bool ensureWebGLContext()
{
    if (s_glCtx > 0)
    {
        emscripten_webgl_make_context_current(s_glCtx);
        return true;
    }

    EmscriptenWebGLContextAttributes attrs;
    emscripten_webgl_init_context_attributes(&attrs);
    attrs.majorVersion = 2;
    attrs.minorVersion = 0;
    attrs.alpha = 0;
    attrs.depth = 1;
    attrs.stencil = 1;
    attrs.antialias = 0;
    attrs.preserveDrawingBuffer = 0;
    attrs.powerPreference = EM_WEBGL_POWER_PREFERENCE_HIGH_PERFORMANCE;
    attrs.failIfMajorPerformanceCaveat = 0;
    attrs.enableExtensionsByDefault = 1;

    s_glCtx = emscripten_webgl_create_context("#flux-gl", &attrs);
    if (s_glCtx <= 0)
    {
        emscripten_log(EM_LOG_ERROR,
                       "FluxCanvas: failed to create WebGL2 context (error %d)", (int)s_glCtx);
        return false;
    }

    EMSCRIPTEN_RESULT r = emscripten_webgl_make_context_current(s_glCtx);
    if (r != EMSCRIPTEN_RESULT_SUCCESS)
    {
        emscripten_log(EM_LOG_ERROR,
                       "FluxCanvas: make_context_current failed (%d)", r);
        return false;
    }

    emscripten_log(EM_LOG_INFO, "FluxCanvas: WebGL2 context created OK");
    return true;
}

static Canvas2DBackend *acquireSharedBackend()
{
    if (!s_sharedBackend)
    {
        s_sharedBackend = Canvas2DBackend_create();
        if (!s_sharedBackend)
        {
            emscripten_log(EM_LOG_ERROR, "FluxCanvas: Canvas2DBackend_create failed");
            return nullptr;
        }

        auto reg = [](const char *name, const char *path)
        {
            if (!Canvas2D::registerFont(s_sharedBackend, name, path))
                emscripten_log(EM_LOG_WARN,
                               "FluxCanvas: font '%s' not found at '%s'", name, path);
        };
        reg("sans", "/fonts/Inter_18pt-Regular.ttf");
        reg("sans-bold", "/fonts/Inter_18pt-Bold.ttf");
        reg("sans-italic", "/fonts/Inter_18pt-Italic.ttf");
        reg("sans-bold-italic", "/fonts/Inter_18pt-BoldItalic.ttf");
    }
    ++s_sharedBackendUsers;
    return s_sharedBackend;
}

static void releaseSharedBackend()
{
    if (--s_sharedBackendUsers <= 0 && s_sharedBackend)
    {
        Canvas2DBackend_destroy(s_sharedBackend);
        s_sharedBackend = nullptr;
        s_sharedBackendUsers = 0;
    }
}

// ============================================================================
// ensureInit
// ============================================================================

static void ensureInit(CanvasWidget *w)
{
    auto &s = webState(w);
    if (s.initialized)
        return;

    if (!ensureWebGLContext())
        return;

    w->backend_ = acquireSharedBackend();
    if (!w->backend_)
        return;

    double dpr = EM_ASM_DOUBLE({ return Module._fluxDPR || 1.0; });
    int physW = (int)std::lround(w->width * dpr);
    int physH = (int)std::lround(w->height * dpr);

    w->vp_.init(w->width, w->height, w->docW(), w->docH());
    w->updateViewportSize(w->width, w->height);
    w->updateSBGeometry(physW, physH);
    w->vp_.fitToView();

    w->lastTick_ = CanvasWidget::Clock::now();

    if (w->onViewportChanged)
        w->onViewportChanged(w->vp_.zoom());
    if (w->onGLResize)
        w->onGLResize(w->width, w->height);

    s.lastW = w->width;
    s.lastH = w->height;
    s.initialized = true;
    printf("[flux] ensureInit complete w=%d h=%d (physical %dx%d, dpr=%.3f)\n",
           w->width, w->height, physW, physH, dpr);
    scheduleRepaint(w);
}

// ============================================================================
// destroyGL
// ============================================================================

static void destroyGL(CanvasWidget *w)
{
    auto &s = webState(w);
    if (!s.initialized)
    {
        s_webState.erase(w);
        return;
    }

    if (w->activeSurface_)
    {
        w->activeSurface_->destroy();
        w->activeSurface_.reset();
    }
    w->pendingSurface_.reset();

    w->backend_ = nullptr;
    releaseSharedBackend();

    s.initialized = false;
    s_webState.erase(w);

    if (s_sharedBackendUsers <= 0 && s_glCtx > 0)
    {
        emscripten_webgl_destroy_context(s_glCtx);
        s_glCtx = 0;
    }
}

// ============================================================================
// tickAndRender
// ============================================================================

static void tickAndRender(CanvasWidget *w)
{
    auto &s = webState(w);
    if (!s.initialized || !w->backend_)
        return;

    if (s_glCtx > 0)
        emscripten_webgl_make_context_current(s_glCtx);

    if (w->pendingSurface_)
        w->activatePendingSurface();
    if (!w->activeSurface_)
        return;

    // ── Timing ────────────────────────────────────────────────────────────
    auto now = CanvasWidget::Clock::now();
    double dt = std::chrono::duration<double>(now - w->lastTick_).count();
    w->lastTick_ = now;
    w->activeSurface_->update(dt);

    // ── Widget rect ───────────────────────────────────────────────────────
    int logicalW = w->width;
    int logicalH = w->height;

    double dpr = EM_ASM_DOUBLE({ return Module._fluxDPR || 1.0; });
    int physX = (int)std::lround(w->x * dpr);
    int physY = (int)std::lround(w->y * dpr);
    int physW = (int)std::lround(logicalW * dpr);
    int physH = (int)std::lround(logicalH * dpr);

    int totalW = EM_ASM_INT({ return Module._fluxPhysicalWidth | 0; });
    int totalH = EM_ASM_INT({ return Module._fluxPhysicalHeight | 0; });

    if (logicalW != s.lastW || logicalH != s.lastH)
    {
        s.lastW = logicalW;
        s.lastH = logicalH;
        w->updateViewportSize(logicalW, logicalH);
        if (w->onGLResize)
            w->onGLResize(logicalW, logicalH);
    }

    // ── GL setup ──────────────────────────────────────────────────────────
    int glY = totalH - physY - physH;

    glEnable(GL_SCISSOR_TEST);
    glScissor(physX, glY, physW, physH);
    glViewport(physX, glY, physW, physH);

    glClearColor(0.18f, 0.18f, 0.18f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    w->activeSurface_->preRender();

    // ── Build MVP for Canvas2D ────────────────────────────────────────────
    float z = w->vp_.zoom() * (float)dpr;
    float ox = -w->vp_.offsetX() * z;
    float oy = -w->vp_.offsetY() * z;

    float ortho[16];
    webOrtho(0.f, float(physW), float(physH), 0.f, ortho);

    float vp[16] = {z, 0, 0, 0, 0, z, 0, 0, 0, 0, 1, 0, ox, oy, 0, 1};
    float mvp[16] = {};
    for (int col = 0; col < 4; ++col)
        for (int row = 0; row < 4; ++row)
            for (int k = 0; k < 4; ++k)
                mvp[col * 4 + row] += ortho[k * 4 + row] * vp[col * 4 + k];

    // ── Canvas2D render pass ──────────────────────────────────────────────
    {
        Canvas2DBackend_setMVP(w->backend_, mvp);
        Canvas2D ctx(w->backend_, w->docW(), w->docH());
        w->activeSurface_->render(ctx);
    }

    glDisable(GL_SCISSOR_TEST);
    glViewport(0, 0, totalW, totalH);

    // ── Scrollbars via Painter into #flux-ui Canvas2D ─────────────────────
    w->updateSBGeometry(physW, physH);
    w->hBar_.tick(dt);
    w->vBar_.tick(dt);

    if (w->hBar_.needsRedraw() || w->vBar_.needsRedraw())
    {
        GraphicsContext gctx;
        Painter p(gctx);
        p.drawScrollbar(w->hBar_, physW, physH);
        p.drawScrollbar(w->vBar_, physW, physH);

        if (w->hBar_.needsRedraw() || w->vBar_.needsRedraw())
            scheduleRepaint(w);
    }

    if (w->activeSurface_->needsContinuousRedraw())
        scheduleRepaint(w);

    s.repaintPending = false;
}

// ============================================================================
// CanvasWidget member implementations (Web)
// ============================================================================

CanvasWidget::CanvasWidget()
    : hBar_(CustomScrollbar::Axis::Horizontal),
      vBar_(CustomScrollbar::Axis::Vertical)
{
    autoWidth = true;
    autoHeight = true;
    width = 400;
    height = 300;
}

CanvasWidget::~CanvasWidget() { destroyGL(this); }

std::shared_ptr<CanvasWidget> CanvasWidget::setViewportEnabled(bool e)
{
    viewportEnabled_ = e;
    return ptr();
}
std::shared_ptr<CanvasWidget> CanvasWidget::setScrollbarsEnabled(bool e)
{
    scrollbarsEnabled_ = e;
    auto &s = webState(this);
    if (s.initialized)
    {
        updateViewportSize(s.lastW, s.lastH);
        scheduleRepaint(this);
    }
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
    auto &s = webState(this);
    if (s.initialized)
    {
        vp_.setCanvasSize(w, h);
        if (activeSurface_)
            activeSurface_->resize(w, h);
        scheduleRepaint(this);
    }
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

void CanvasWidget::render(GraphicsContext & /*ctx*/, FontCache &)
{
    ensureInit(this);

    webState(this).lastX = x;
    webState(this).lastY = y;

    tickAndRender(this);

    s_holeRects.push_back({x, y, width, height});
    needsPaint = false;
}

void CanvasWidget::onDetach()
{
    destroyGL(this);
    Widget::onDetach();
}

void CanvasWidget::markNeedsPaint()
{
    Widget::markNeedsPaint();
    scheduleRepaint(this);
}

bool CanvasWidget::handleMouseDown(int mx, int my)
{
    bool hC = hBar_.onMouseDown(mx - x, my - y,
                                [this](float t)
                                { applyHScrollFraction(t); });
    bool vC = !hC && vBar_.onMouseDown(mx - x, my - y,
                                       [this](float t)
                                       { applyVScrollFraction(t); });
    if (!hC && !vC)
    {
        if (viewportEnabled_ && platformKeyDown(VK_SPACE))
            beginPan(mx - x, my - y);
        else if (activeSurface_)
        {
            auto [cx, cy] = vp_.screenToCanvas(float(mx - x), float(my - y));
            activeSurface_->onMouseDown(cx, cy);
        }
    }
    scheduleRepaint(this);
    return true;
}

bool CanvasWidget::handleMouseMove(int mx, int my)
{
    int lx = mx - x, ly = my - y;
    bool hDrag = hBar_.onMouseMove(lx, ly);
    bool vDrag = vBar_.onMouseMove(lx, ly);
    scheduleRepaint(this);
    if (hDrag || vDrag)
        return true;
    if (panning_)
        continuePan(lx, ly);
    else if (activeSurface_)
    {
        auto [cx, cy] = vp_.screenToCanvas(float(lx), float(ly));
        activeSurface_->onMouseMove(cx, cy);
    }
    return true;
}

bool CanvasWidget::handleMouseUp(int mx, int my)
{
    int lx = mx - x, ly = my - y;
    bool hR = hBar_.onMouseUp(lx, ly);
    bool vR = vBar_.onMouseUp(lx, ly);
    if (!hR && !vR)
    {
        if (panning_)
            panning_ = false;
        else if (activeSurface_)
        {
            auto [cx, cy] = vp_.screenToCanvas(float(lx), float(ly));
            activeSurface_->onMouseUp(cx, cy);
        }
    }
    scheduleRepaint(this);
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
        vp_.zoomToward(float(width) * 0.5f, float(height) * 0.5f, f);
    }
    else if (shift)
        vp_.panByScreen(float(delta) * 0.5f, 0.f);
    else
        vp_.panByScreen(0.f, -(float(delta) / WHEEL_DELTA) * 40.f);
    pokeScrollbars();
    scheduleRepaint(this);
    return true;
}

bool CanvasWidget::handleKeyDown(int keyCode)
{
    bool ctrl = platformCtrlDown();
    bool shift = platformShiftDown();
    bool alt = platformAltDown();
    if (ctrl && viewportEnabled_)
    {
        if (keyCode == VK_UP || keyCode == '=')
        {
            vp_.zoomIn();
            pokeScrollbars();
            scheduleRepaint(this);
            return true;
        }
        if (keyCode == VK_DOWN || keyCode == '-')
        {
            vp_.zoomOut();
            pokeScrollbars();
            scheduleRepaint(this);
            return true;
        }
        if (keyCode == '0')
        {
            vp_.resetZoom();
            pokeScrollbars();
            scheduleRepaint(this);
            return true;
        }
    }
    if (activeSurface_)
    {
        KeyEvent ke{0, keyCode, ctrl, shift, alt};
        activeSurface_->onKeyDown(ke);
        return true;
    }
    return false;
}

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
    scheduleRepaint(this);
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
    scheduleRepaint(this);
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
    scheduleRepaint(this);
}

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

#endif // __EMSCRIPTEN__