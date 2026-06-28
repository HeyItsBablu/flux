// src/flux_canvas_web.cpp
//
// CanvasWidget platform implementation for Emscripten / WebAssembly.
//
// Architecture
// ────────────
// Two <canvas> elements are set up by shell.html:
//
//   #flux-ui  (Canvas 2D, top layer)
//     • All regular widget painting goes here via flux_painter_web.cpp.
//     • Receives all pointer and keyboard events (pointer-events: auto).
//     • CanvasWidget paints its 2-D overlay (scrollbars via Canvas2D) here too.
//
//   #flux-gl  (WebGL2, bottom layer)
//     • Emscripten initialises its GL context on this canvas
//       (Module.canvas = glCanvas in shell.html).
//     • CanvasWidget renders its RenderSurface here each frame.
//     • pointer-events: none — input goes through #flux-ui.
//
// Render flow per tick()
// ──────────────────────
//   PlatformWindow::tick()  [flux_window_web.cpp]
//     └─ callbacks.onPaint(ctx, w, h)  [flux_core.cpp]
//          └─ Renderer::renderWidget(root)
//               └─ CanvasWidget::render()
//                    └─ tickAndRender()
//                         ├─ activatePendingSurface()
//                         ├─ surface->update(dt)
//                         ├─ GL: viewport + scissor to widget rect
//                         ├─ surface->preRender()
//                         ├─ Canvas2D ctx(s_sharedBackend, ..., mvp set on backend)
//                         ├─ surface->render(ctx)
//                         └─ Painter::drawScrollbar() into #flux-ui Canvas2D
//
// Event routing
// ─────────────
// All events land on #flux-ui via flux_window_web.cpp callbacks.
// FluxUI::wireCallbacks() dispatches them to the widget tree as usual.
// CanvasWidget::handleMouseDown/Move/Up do hit-testing against [x,y,w,h]
// and translate screen coords to canvas coords via Viewport::screenToCanvas().
//
// GL context
// ──────────
// Emscripten owns the WebGL2 context.  We do NOT call
// emscripten_webgl_make_context_current() — Emscripten keeps it current.
// Canvas2DGL::init() is called once and uses the already-current context.
//
// Canvas2D backend
// ────────────────
// All CanvasWidgets on the page share a single Canvas2DGL (font atlas,
// shaders, VAO/VBO) AND a single Canvas2DBackend wrapping it. Canvas2D is
// only ever constructed as Canvas2D(Canvas2DBackend*, w, h) — never from a
// raw Canvas2DGL* — so each widget writes its own per-frame MVP into the
// shared backend's `mvp` field immediately before constructing its Canvas2D
// and rendering. This is safe because all widgets render sequentially on
// the same thread within a single frame.

#ifdef __EMSCRIPTEN__

#include "flux/flux_canvas.hpp"
#include "flux/flux_canvas2d_backend.hpp"
#include "flux/flux_glutil.hpp"
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

// ── Canvas2DGL singleton ──────────────────────────────────────────────────────

static Canvas2DGL *s_sharedGL = nullptr;
static Canvas2DBackend *s_sharedBackend = nullptr;
static int s_sharedGLUsers = 0;

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

static Canvas2DGL *acquireSharedGL()
{
    if (!s_sharedGL)
    {
        s_sharedGL = new Canvas2DGL();
        if (!s_sharedGL->init())
        {
            emscripten_log(EM_LOG_ERROR, "FluxCanvas: Canvas2DGL init failed");
            delete s_sharedGL;
            s_sharedGL = nullptr;
            return nullptr;
        }

        s_sharedBackend = Canvas2DBackend::create(s_sharedGL);

        auto reg = [](const char *name, const char *path)
        {
            if (!Canvas2D::registerFont(s_sharedBackend, name, path))
                emscripten_log(EM_LOG_WARN,
                               "FluxCanvas: font '%s' not found at '%s'", name, path);
        };
        reg("sans",            "/fonts/Inter_18pt-Regular.ttf");
        reg("sans-bold",       "/fonts/Inter_18pt-Bold.ttf");
        reg("sans-italic",     "/fonts/Inter_18pt-Italic.ttf");
        reg("sans-bold-italic","/fonts/Inter_18pt-BoldItalic.ttf");
    }
    ++s_sharedGLUsers;
    return s_sharedGL;
}

static void releaseSharedGL()
{
    if (--s_sharedGLUsers <= 0 && s_sharedGL)
    {
        if (s_sharedBackend)
        {
            Canvas2DBackend::destroy(s_sharedBackend);
            s_sharedBackend = nullptr;
        }
        s_sharedGL->destroy();
        delete s_sharedGL;
        s_sharedGL = nullptr;
        s_sharedGLUsers = 0;
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

    w->canvasGL_ = acquireSharedGL();
    if (!w->canvasGL_)
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

    w->canvasGL_ = nullptr;
    releaseSharedGL();

    s.initialized = false;
    s_webState.erase(w);

    if (s_sharedGLUsers <= 0 && s_glCtx > 0)
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
    if (!s.initialized || !w->canvasGL_ || !s_sharedBackend)
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

    int totalW = EM_ASM_INT({ return Module._fluxPhysicalWidth  | 0; });
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
    float z  = w->vp_.zoom() * (float)dpr;
    float ox = -w->vp_.offsetX() * z;
    float oy = -w->vp_.offsetY() * z;

    float ortho[16];
    glutil::ortho(0.f, float(physW), float(physH), 0.f, ortho);

    float vp[16] = {z, 0, 0, 0, 0, z, 0, 0, 0, 0, 1, 0, ox, oy, 0, 1};
    float mvp[16] = {};
    for (int col = 0; col < 4; ++col)
        for (int row = 0; row < 4; ++row)
            for (int k = 0; k < 4; ++k)
                mvp[col * 4 + row] += ortho[k * 4 + row] * vp[col * 4 + k];

    // ── Canvas2D render pass ──────────────────────────────────────────────
    {
        std::memcpy(s_sharedBackend->mvp, mvp, sizeof(mvp));
        Canvas2D ctx(s_sharedBackend, w->docW(), w->docH());
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
    autoWidth  = true;
    autoHeight = true;
    width  = 400;
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

RenderSurface  *CanvasWidget::getSurface()  const { return activeSurface_.get(); }
const Viewport &CanvasWidget::viewport()    const { return vp_; }
Viewport       &CanvasWidget::viewport()          { return vp_; }

std::shared_ptr<CanvasWidget> CanvasWidget::setSize(int w, int h)
{
    width = w; height = h;
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
        width  = (c.maxWidth  < kUnbounded) ? c.maxWidth  : width;
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
                                [this](float t){ applyHScrollFraction(t); });
    bool vC = !hC && vBar_.onMouseDown(mx - x, my - y,
                                       [this](float t){ applyVScrollFraction(t); });
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
    if (hDrag || vDrag) return true;
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
        if (panning_) panning_ = false;
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
    if (!viewportEnabled_) return false;
    bool ctrl  = platformCtrlDown();
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
    bool ctrl  = platformCtrlDown();
    bool shift = platformShiftDown();
    bool alt   = platformAltDown();
    if (ctrl && viewportEnabled_)
    {
        if (keyCode == VK_UP || keyCode == '=')
            { vp_.zoomIn();    pokeScrollbars(); scheduleRepaint(this); return true; }
        if (keyCode == VK_DOWN || keyCode == '-')
            { vp_.zoomOut();   pokeScrollbars(); scheduleRepaint(this); return true; }
        if (keyCode == '0')
            { vp_.resetZoom(); pokeScrollbars(); scheduleRepaint(this); return true; }
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
    if (!viewportEnabled_ || !scrollbarsEnabled_) { vpW = glW; vpH = glH; return; }
    ScrollbarInfo h = vp_.scrollbarH(), v = vp_.scrollbarV();
    vpW = glW - (v.visible ? (int)kSBThick : 0);
    vpH = glH - (h.visible ? (int)kSBThick : 0);
    if (vpW < 1) vpW = 1;
    if (vpH < 1) vpH = 1;
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
    panStartSX_ = sx; panStartSY_ = sy;
    panStartOX_ = vp_.offsetX(); panStartOY_ = vp_.offsetY();
}

void CanvasWidget::continuePan(int sx, int sy)
{
    float dx = float(sx - panStartSX_), dy = float(sy - panStartSY_);
    vp_.setOffset(panStartOX_ - dx / vp_.zoom(),
                  panStartOY_ + dy / vp_.zoom());
    pokeScrollbars();
    scheduleRepaint(this);
}

void CanvasWidget::pokeScrollbars() { hBar_.poke(); vBar_.poke(); }

void CanvasWidget::applyHScrollFraction(float thumbMin)
{
    float span  = vp_.scrollbarH().thumbMax - vp_.scrollbarH().thumbMin;
    float range = vp_.canvasW() - vp_.viewW() / vp_.zoom();
    if (range <= 0.f) return;
    vp_.setOffsetX(thumbMin / (1.f - span) * range);
    pokeScrollbars();
    scheduleRepaint(this);
}

void CanvasWidget::applyVScrollFraction(float thumbMin)
{
    float span  = vp_.scrollbarV().thumbMax - vp_.scrollbarV().thumbMin;
    float range = vp_.canvasH() - vp_.viewH() / vp_.zoom();
    if (range <= 0.f) return;
    float t = 1.f - thumbMin - span;
    vp_.setOffsetY(t / (1.f - span) * range);
    pokeScrollbars();
    scheduleRepaint(this);
}

void CanvasWidget::activatePendingSurface()
{
    if (!pendingSurface_) return;
    if (activeSurface_) { activeSurface_->destroy(); activeSurface_.reset(); }
    activeSurface_ = pendingSurface_;
    pendingSurface_.reset();
    activeSurface_->initialize(docW(), docH());
    vp_.setCanvasSize(docW(), docH());
}

#endif // __EMSCRIPTEN__