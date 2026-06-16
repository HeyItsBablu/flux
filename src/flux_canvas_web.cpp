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
// Unlike Win32 (which creates child HWNDs) or Linux (SDL subwindow),
// on web there is only one GL context and one 2-D context for the whole
// page.  CanvasWidget uses WebGL scissor + viewport to restrict its GL
// rendering to its own bounding rect within #flux-gl.
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
//                         ├─ Canvas2D ctx(canvasGL_, ..., mvp)
//                         ├─ surface->render(ctx)
//                         └─ renderScrollbarsGL() (draws into #flux-gl)
//
// Scrollbar overlay is drawn into the GL layer (matching Win32/Linux).
// The Canvas 2D layer is NOT used for scrollbars here — keeping the
// two layers cleanly separated avoids compositing order issues.
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
// There is no wglMakeCurrent / eglMakeCurrent equivalent needed.

#ifdef __EMSCRIPTEN__

#include "flux/flux_canvas.hpp"

#include <emscripten.h>
#include <emscripten/html5.h>
#include <GLES3/gl3.h>

#include <cassert>
#include <cstdio>
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
            ctx.save();
            ctx.setTransform(1, 0, 0, 1, 0, 0);
            ctx.clearRect($0, $1, $2, $3);
            ctx.restore(); }, r.x, r.y, r.w, r.h);
    }
    s_holeRects.clear();
}

// ============================================================================
// Scrollbar shaders — WebGL2 / GLSL ES 3.00
// ============================================================================

static const char *kSBVert = R"GLSL(#version 300 es
layout(location=0) in vec2 aPos;
uniform mat4 uMVP;
void main(){ gl_Position = uMVP * vec4(aPos, 0.0, 1.0); }
)GLSL";

static const char *kSBFrag = R"GLSL(#version 300 es
precision mediump float;
out vec4 fragColor;
uniform vec4 uColor;
void main(){ fragColor = uColor; }
)GLSL";

// ============================================================================
// Web-only per-widget state
// ============================================================================

struct WebCanvasState
{
    bool initialized = false;
    bool repaintPending = false;
    bool mouseCapture = false;
    EMSCRIPTEN_WEBGL_CONTEXT_HANDLE glCtx = 0;
    // Last known widget rect in physical pixels (set in render/computeLayout).
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
    // The actual repaint happens on the next tick() call from s_tick()
    // in main_web.cpp — no PostMessage needed.
    // Mark the PlatformWindow dirty so tick() repaints the whole frame.
    if (auto *ui = FluxUI::getCurrentInstance())
        ui->getPlatformWindow().invalidate();
}

// ── Canvas2DGL singleton ──────────────────────────────────────────────────────
//
// All CanvasWidgets on the page share a single GL context and therefore a
// single Canvas2DGL (font atlas, shader programs, VAO/VBO).  It is created
// on first CanvasWidget init and destroyed when the last one is detached.

static Canvas2DGL *s_sharedGL = nullptr;
static int s_sharedGLUsers = 0;

// ── WebGL2 context (created once, shared by all CanvasWidgets) ────────────────

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

        // Register fonts from the preloaded /fonts/ directory.
        // The examples/CMakeLists.txt --preload-file maps
        // web/fonts → /fonts at runtime.
        auto reg = [](const char *name, const char *path)
        {
            if (!Canvas2D::registerFont(s_sharedGL, name, path))
                emscripten_log(EM_LOG_WARN,
                               "FluxCanvas: font '%s' not found at '%s'", name, path);
        };
        reg("sans", "/fonts/Inter_18pt-Regular.ttf");
        reg("sans-bold", "/fonts/Inter_18pt-Bold.ttf");
        reg("sans-italic", "/fonts/Inter_18pt-Italic.ttf");
        reg("sans-bold-italic", "/fonts/Inter_18pt-BoldItalic.ttf");
    }
    ++s_sharedGLUsers;
    return s_sharedGL;
}

static void releaseSharedGL()
{
    if (--s_sharedGLUsers <= 0 && s_sharedGL)
    {
        s_sharedGL->destroy();
        delete s_sharedGL;
        s_sharedGL = nullptr;
        s_sharedGLUsers = 0;
    }
}

// ============================================================================
// ensureInit — lazy Canvas2DGL + scrollbar program creation
// ============================================================================

static void ensureInit(CanvasWidget *w)
{
    auto &s = webState(w);
    if (s.initialized)
        return;

    // Must create and make current BEFORE any GL calls
    if (!ensureWebGLContext())
        return;

    w->canvasGL_ = acquireSharedGL();
    if (!w->canvasGL_)
        return;

    // Scrollbar GL program (shared structure on CanvasWidget).
    w->ensureSBProgram(kSBVert, kSBFrag);

    // Viewport init.
    w->vp_.init(w->width, w->height, w->docW(), w->docH());
    w->updateViewportSize(w->width, w->height);
    w->updateSBGeometry(w->width, w->height);
    w->vp_.fitToView();

    w->lastTick_ = CanvasWidget::Clock::now();

    if (w->onViewportChanged)
        w->onViewportChanged(w->vp_.zoom());
    if (w->onGLResize)
        w->onGLResize(w->width, w->height);

    s.lastW = w->width;
    s.lastH = w->height;
    s.initialized = true;
    printf("[flux] ensureInit complete w=%d h=%d\n", w->width, w->height);
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

    // Scrollbar GL resources (owned per-widget even though program is shared
    // via ensureSBProgram — glDeleteProgram is safe to call multiple times
    // with the same handle only if we track ownership; here we just null it).
    if (w->sbProg_)
    {
        glDeleteProgram(w->sbProg_);
        w->sbProg_ = 0;
    }
    if (w->sbVAO_)
    {
        glDeleteVertexArrays(1, &w->sbVAO_);
        w->sbVAO_ = 0;
    }
    if (w->sbVBO_)
    {
        glDeleteBuffers(1, &w->sbVBO_);
        w->sbVBO_ = 0;
    }

    // Release our reference to the shared Canvas2DGL.
    // canvasGL_ is the shared pointer — don't delete it directly.
    w->canvasGL_ = nullptr;
    releaseSharedGL();

    s.initialized = false;
    s_webState.erase(w);

    // Destroy shared GL context when last widget is gone
    if (s_sharedGLUsers <= 0 && s_glCtx > 0)
    {
        emscripten_webgl_destroy_context(s_glCtx);
        s_glCtx = 0;
    }
}

// ============================================================================
// tickAndRender — one GL frame for this widget
// ============================================================================

static void tickAndRender(CanvasWidget *w)
{
    auto &s = webState(w);
    if (!s.initialized || !w->canvasGL_)
        return;

    if (s_glCtx > 0)
        emscripten_webgl_make_context_current(s_glCtx);

    if (w->pendingSurface_)
    {
        printf("[flux] activating pending surface\n");
        w->activatePendingSurface();
        printf("[flux] activeSurface after activate: %p\n", (void *)w->activeSurface_.get());
    }
    if (!w->activeSurface_)
    {
        printf("[flux] no activeSurface, returning\n");
        return;
    }
 
    printf("[flux] rendering surface physX=%d physY=%d physW=%d physH=%d\n",
           w->x, w->y, w->width, w->height);

    // ── Timing ────────────────────────────────────────────────────────────
    auto now = CanvasWidget::Clock::now();
    double dt = std::chrono::duration<double>(now - w->lastTick_).count();
    w->lastTick_ = now;
    w->activeSurface_->update(dt);

    // ── Widget rect in physical pixels ────────────────────────────────────
    int physX = w->x;
    int physY = w->y;
    int physW = w->width;
    int physH = w->height;

    // Total physical canvas size (needed for GL viewport and ortho).
    int totalW = EM_ASM_INT({ return Module._fluxPhysicalWidth | 0; });
    int totalH = EM_ASM_INT({ return Module._fluxPhysicalHeight | 0; });

    // Handle resize.
    if (physW != s.lastW || physH != s.lastH)
    {
        s.lastW = physW;
        s.lastH = physH;
        w->updateViewportSize(physW, physH);
        if (w->onGLResize)
            w->onGLResize(physW, physH);
    }

    // ── GL setup ──────────────────────────────────────────────────────────
    // Scissor to this widget's rect so we don't overdraw other widgets.
    // WebGL (like OpenGL) uses bottom-left origin for scissor/viewport;
    // our coordinate system has y=0 at top, so flip:
    //   gl_y = totalH - physY - physH
    int glY = totalH - physY - physH;

    glEnable(GL_SCISSOR_TEST);
    glScissor(physX, glY, physW, physH);
    glViewport(physX, glY, physW, physH);

    glClearColor(0.18f, 0.18f, 0.18f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // ── Surface pre-render (raw GL pass) ──────────────────────────────────
    w->activeSurface_->preRender();

    // ── Build MVP for Canvas2D ────────────────────────────────────────────
    // Ortho projection maps [0, docW] × [0, docH] → clip space,
    // then pan/zoom applied via the viewport transform.
    float z = w->vp_.zoom();
    float ox = -w->vp_.offsetX() * z;
    float oy = -w->vp_.offsetY() * z;

    // Ortho over the widget's physical pixel rect (origin at widget top-left).
    float ortho[16];
    glutil::ortho(0.f, float(physW), float(physH), 0.f, ortho);

    float vp[16] = {z, 0, 0, 0, 0, z, 0, 0, 0, 0, 1, 0, ox, oy, 0, 1};
    float mvp[16] = {};
    for (int col = 0; col < 4; ++col)
        for (int row = 0; row < 4; ++row)
            for (int k = 0; k < 4; ++k)
                mvp[col * 4 + row] += ortho[k * 4 + row] * vp[col * 4 + k];

    // ── Drop shadow ───────────────────────────────────────────────────────
    {
        const float kShadowOffX = 6.f;
        const float kShadowOffY = 6.f;
        const float kShadowLayers = 4;
        const float kShadowAlpha = 0.18f;

        float cw = float(w->docW()) * z;
        float ch = float(w->docH()) * z;

        if (w->sbProg_)
        {
            glUseProgram(w->sbProg_);
            float screenMVP[16];
            glutil::ortho(0.f, float(physW), float(physH), 0.f, screenMVP);
            glUniformMatrix4fv(w->sbMVP_, 1, GL_FALSE, screenMVP);
            glBindVertexArray(w->sbVAO_);
            glBindBuffer(GL_ARRAY_BUFFER, w->sbVBO_);

            for (int i = (int)kShadowLayers; i >= 1; --i)
            {
                float spread = float(i) * 2.f;
                float alpha = kShadowAlpha *
                              (1.f - float(i - 1) / kShadowLayers);
                float sx = ox + kShadowOffX - spread;
                float sy = oy + kShadowOffY - spread;
                float sw = cw + spread * 2.f;
                float sh = ch + spread * 2.f;

                glUniform4f(w->sbColor_, 0.f, 0.f, 0.f, alpha);
                float v[] = {
                    sx, sy, sx + sw, sy,
                    sx + sw, sy + sh, sx + sw, sy + sh,
                    sx, sy + sh, sx, sy};
                glBufferData(GL_ARRAY_BUFFER, sizeof(v), v, GL_DYNAMIC_DRAW);
                glEnableVertexAttribArray(0);
                glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE,
                                      2 * sizeof(float), nullptr);
                glDisableVertexAttribArray(1);
                glDrawArrays(GL_TRIANGLES, 0, 6);
            }
            glBindVertexArray(0);
        }
    }

    // ── Canvas2D render pass ──────────────────────────────────────────────
    {
        Canvas2D ctx(w->canvasGL_, w->docW(), w->docH(), mvp);
        w->activeSurface_->render(ctx);
    }

    // ── Scrollbars ────────────────────────────────────────────────────────
    w->updateSBGeometry(physW, physH);
    w->renderScrollbarsGL(physW, physH, dt);

    // ── Restore full GL viewport for next widget / next frame ─────────────
    glDisable(GL_SCISSOR_TEST);
    glViewport(0, 0, totalW, totalH);

    // Continuous-redraw surfaces keep the dirty flag set.
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

// ── Configuration ─────────────────────────────────────────────────────────────

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
    autoWidth = false;
    autoHeight = false;
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

// ── Layout ────────────────────────────────────────────────────────────────────

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

// ── Render ────────────────────────────────────────────────────────────────────

void CanvasWidget::render(GraphicsContext & /*ctx*/, FontCache &)
{
    ensureInit(this);

    webState(this).lastX = x;
    webState(this).lastY = y;

    tickAndRender(this);

    // Register this widget's rect to be cleared after all 2D painting is done.
    s_holeRects.push_back({x, y, width, height});

    needsPaint = false;
}

// ── Lifecycle ─────────────────────────────────────────────────────────────────

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

// ── Input events ──────────────────────────────────────────────────────────────
//
// Events are delivered here by FluxUI::wireCallbacks() via the standard
// Widget::handleMouseDown/Move/Up virtuals.  We hit-test against our
// bounding rect (already done by findAndHandleMouseEvent) and translate
// to canvas coordinates.

bool CanvasWidget::handleMouseDown(int mx, int my)
{
    auto &s = webState(this);

    // Scrollbar hit-test first.
    bool hC = hBar_.onMouseDown(mx - x, my - y,
                                [this](float t)
                                { applyHScrollFraction(t); });
    bool vC = !hC && vBar_.onMouseDown(mx - x, my - y,
                                       [this](float t)
                                       { applyVScrollFraction(t); });

    if (!hC && !vC)
    {
        // Space + click = pan (mirrors Win32 behaviour).
        if (viewportEnabled_ && platformKeyDown(VK_SPACE))
        {
            beginPan(mx - x, my - y);
        }
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
    int lx = mx - x;
    int ly = my - y;

    bool hDrag = hBar_.onMouseMove(lx, ly);
    bool vDrag = vBar_.onMouseMove(lx, ly);
    scheduleRepaint(this);
    if (hDrag || vDrag)
        return true;

    if (panning_)
    {
        continuePan(lx, ly);
    }
    else if (activeSurface_)
    {
        auto [cx, cy] = vp_.screenToCanvas(float(lx), float(ly));
        activeSurface_->onMouseMove(cx, cy);
    }
    return true;
}

bool CanvasWidget::handleMouseUp(int mx, int my)
{
    int lx = mx - x;
    int ly = my - y;

    bool hR = hBar_.onMouseUp(lx, ly);
    bool vR = vBar_.onMouseUp(lx, ly);

    if (!hR && !vR)
    {
        if (panning_)
        {
            panning_ = false;
        }
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
        // Zoom toward centre of widget.
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

// ── Shared helper implementations ─────────────────────────────────────────────

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
    float dx = float(sx - panStartSX_);
    float dy = float(sy - panStartSY_);
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

void CanvasWidget::ensureSBProgram(const char *vert, const char *frag)
{
    if (sbProg_)
        return;
    sbProg_ = glutil::linkProgram(vert, frag);
    assert(sbProg_ && "FluxCanvas: scrollbar shader link failed");
    sbMVP_ = glGetUniformLocation(sbProg_, "uMVP");
    sbColor_ = glGetUniformLocation(sbProg_, "uColor");
    if (!sbVAO_)
        glGenVertexArrays(1, &sbVAO_);
    if (!sbVBO_)
        glGenBuffers(1, &sbVBO_);
}

void CanvasWidget::renderSBCorner(int glW, int glH)
{
    if (!hBar_.isVisible() || !vBar_.isVisible() || !scrollbarsEnabled_)
        return;
    float cx = float(glW) - kSBThick;
    float cy = float(glH) - kSBThick;
    float mvp[16];
    glutil::ortho(0.f, float(glW), float(glH), 0.f, mvp);
    glUseProgram(sbProg_);
    glUniformMatrix4fv(sbMVP_, 1, GL_FALSE, mvp);
    glUniform4f(sbColor_, 0.12f, 0.12f, 0.12f, 0.35f);
    float v[] = {
        cx, cy,
        cx + kSBThick, cy,
        cx + kSBThick, cy + kSBThick,
        cx + kSBThick, cy + kSBThick,
        cx, cy + kSBThick,
        cx, cy};
    glBindVertexArray(sbVAO_);
    glBindBuffer(GL_ARRAY_BUFFER, sbVBO_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(v), v, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 2, nullptr);
    glDisableVertexAttribArray(1);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
}

void CanvasWidget::renderScrollbarsGL(int glW, int glH, double dt)
{
    bool hFade = hBar_.tick(dt);
    bool vFade = vBar_.tick(dt);

    if (!hBar_.needsRedraw() && !vBar_.needsRedraw())
    {
        if (hFade || vFade)
            scheduleRepaint(this);
        return;
    }

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    hBar_.render(sbProg_, sbVAO_, sbVBO_, sbMVP_, sbColor_, glW, glH);
    vBar_.render(sbProg_, sbVAO_, sbVBO_, sbMVP_, sbColor_, glW, glH);
    renderSBCorner(glW, glH);

    if (hFade || vFade)
        scheduleRepaint(this);
}

#endif // __EMSCRIPTEN__