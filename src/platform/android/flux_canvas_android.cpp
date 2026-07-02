// flux_canvas_android.cpp

#ifdef __ANDROID__

#include "flux/flux_canvas.hpp"
#include "flux/flux_painter.hpp"
#include "flux/flux_canvas2d.hpp"

#include <GLES3/gl3.h>
#include <EGL/egl.h>
#include <android/log.h>

// ── Canvas2DBackend lifecycle — defined in flux_canvas2d_gl.cpp.
// No shared header: this is the entire contract between the two files.
extern Canvas2DBackend *Canvas2DBackend_create();
extern void Canvas2DBackend_destroy(Canvas2DBackend *b);
extern void Canvas2DBackend_destroyContextLost(Canvas2DBackend *b);
extern void Canvas2DBackend_setMVP(Canvas2DBackend *b, const float mvp[16]);

static void localOrtho(float l, float r, float b, float t, float out[16])
{
    memset(out, 0, 64);
    out[0] = 2.f / (r - l);
    out[5] = 2.f / (t - b);
    out[10] = -1.f;
    out[12] = -(r + l) / (r - l);
    out[13] = -(t + b) / (t - b);
    out[15] = 1.f;
}

#define CANVAS_LOGI(...) __android_log_print(ANDROID_LOG_INFO, "CanvasWidget", __VA_ARGS__)
#define CANVAS_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "CanvasWidget", __VA_ARGS__)

extern float FluxAndroid_getDpiScale();

// ─────────────────────────────────────────────────────────────────────────────
// Per-widget Android canvas state
// ─────────────────────────────────────────────────────────────────────────────

struct AndroidCanvasState
{
    // ── GL object lifetime (shader/atlas/fonts/backend) ────────────────────
    bool glObjectsReady = false;

    // ── Viewport lifetime (vp_ sizing) ──────────────────────────────────────
    bool viewportReady = false;

    // Owned entirely by this widget.
    Canvas2DBackend *backend = nullptr;

    // FBO — sized to the DOCUMENT (w->docW()/docH()), not the viewport.
    GLuint fboId = 0;
    GLuint fboTex = 0;
    GLuint fboDepth = 0;
    int fboW = 0;
    int fboH = 0;
};

// One state blob per CanvasWidget instance; keyed by pointer.
#include <unordered_map>
static std::unordered_map<CanvasWidget *, AndroidCanvasState> s_androidState;

static AndroidCanvasState &androidState(CanvasWidget *w)
{
    return s_androidState[w];
}

// ─────────────────────────────────────────────────────────────────────────────
// FBO helpers (Android-local)
// ─────────────────────────────────────────────────────────────────────────────

static void deleteFBO(CanvasWidget *w)
{
    auto &s = androidState(w);

    if (s.fboId)
    {
        glDeleteFramebuffers(1, &s.fboId);
        s.fboId = 0;
    }
    if (s.fboTex)
    {
        glDeleteTextures(1, &s.fboTex);
        s.fboTex = 0;
    }
    if (s.fboDepth)
    {
        glDeleteRenderbuffers(1, &s.fboDepth);
        s.fboDepth = 0;
    }
    s.fboW = s.fboH = 0;
}

// Tears down this widget's own Canvas2DBackend while the GL context is
// still ALIVE (widget destroyed/detached during normal operation, e.g.
// removed from the tree mid-session). Issues real glDelete* calls. Do NOT
// call this from onGLContextLost() — the context is already gone there;
// see that method for the CPU-side-only teardown.
static void deleteGL(CanvasWidget *w)
{
    auto &s = androidState(w);
    if (s.backend)
    {
        Canvas2DBackend_destroy(s.backend);
        s.backend = nullptr;
    }
    w->backend_ = nullptr;
    s.glObjectsReady = false;
}

static void ensureFBO(CanvasWidget *w, int physW, int physH)
{
    auto &s = androidState(w);
    if (s.fboId && s.fboW == physW && s.fboH == physH)
        return;
    deleteFBO(w);

    s.fboW = physW;
    s.fboH = physH;

    glGenTextures(1, &s.fboTex);
    glBindTexture(GL_TEXTURE_2D, s.fboTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, physW, physH, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    glGenRenderbuffers(1, &s.fboDepth);
    glBindRenderbuffer(GL_RENDERBUFFER, s.fboDepth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, physW, physH);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);

    glGenFramebuffers(1, &s.fboId);
    glBindFramebuffer(GL_FRAMEBUFFER, s.fboId);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, s.fboTex, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                              GL_RENDERBUFFER, s.fboDepth);

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE)
        CANVAS_LOGE("FBO incomplete: 0x%x", status);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// ─────────────────────────────────────────────────────────────────────────────
// One-time (per-context) resource creation — shader, VAO/VBO, font atlas,
// glyph cache, backend. Owned entirely by this widget. Survives resizes;
// only cleared by onGLContextLost() (context died) or deleteGL() (widget
// torn down while the context is alive).
// ─────────────────────────────────────────────────────────────────────────────

static void ensureGLObjects(CanvasWidget *w)
{
    auto &s = androidState(w);
    if (s.glObjectsReady)
        return;

    s.backend = Canvas2DBackend_create();
    if (!s.backend)
    {
        CANVAS_LOGE("Canvas2DBackend_create failed for widget %p", (void *)w);
        return; // leave glObjectsReady == false — retry next frame
    }

    Canvas2D::registerFont(s.backend, "sans", "/system/fonts/Roboto-Regular.ttf");
    Canvas2D::registerFont(s.backend, "sans-bold", "/system/fonts/Roboto-Bold.ttf");
    Canvas2D::registerFont(s.backend, "sans-italic", "/system/fonts/Roboto-Italic.ttf");
    Canvas2D::registerFont(s.backend, "mono", "/system/fonts/DroidSansMono.ttf");

    w->backend_ = s.backend;
    s.glObjectsReady = true;
}

// Cheap viewport/surface (re)initialization — safe to repeat on every
// resize. Independent of GL object lifetime; does not touch s.backend.
static void ensureViewport(CanvasWidget *w, int windowW, int windowH, float /*dpi*/)
{
    auto &s = androidState(w);
    if (s.viewportReady)
        return;
    s.viewportReady = true;

    // canvasW_/canvasH_ are physical pixels set by computeLayout — the
    // widget's own on-screen (viewport) size.
    int physW = w->canvasW_ > 0 ? w->canvasW_ : windowW;
    int physH = w->canvasH_ > 0 ? w->canvasH_ : windowH;

    // docW()/docH() fall back to canvasW_/canvasH_ when unset (see
    // flux_canvas.hpp). Passing the document extent here — not physW/physH
    // for both view and canvas — is what lets the viewport pan/zoom beyond
    // the visible widget bounds, matching Win32/Linux.
    w->vp_.init(physW, physH, w->docW(), w->docH());
    w->updateViewportSize(physW, physH);
    w->vp_.fitToView();

    w->activatePendingSurface();
    w->lastTick_ = CanvasWidget::Clock::now();

    if (w->onViewportChanged)
        w->onViewportChanged(w->vp_.zoom());
    if (w->onGLResize)
        w->onGLResize(physW, physH);
}

// ─────────────────────────────────────────────────────────────────────────────
// Pass 1: render surface into FBO  (called from android_main before Painter)
// ─────────────────────────────────────────────────────────────────────────────

static void glRenderPass(CanvasWidget *w, int windowW, int windowH, float dpi)
{
    ensureGLObjects(w);
    auto &s = androidState(w);
    if (!s.backend)
        return;

    ensureViewport(w, windowW, windowH, dpi);

    if (w->pendingSurface_)
        w->activatePendingSurface();
    if (!w->activeSurface_)
        return;

    auto now = CanvasWidget::Clock::now();
    w->frameDt_ = std::chrono::duration<double>(now - w->lastTick_).count();
    w->lastTick_ = now;

    w->activeSurface_->update(w->frameDt_);
    w->activeSurface_->preRender();

    // FBO is sized to the DOCUMENT, not the viewport — matches Win32's
    // offscreen bitmap (sized w->docW()/docH()) and Linux's Canvas2D
    // (sized canvasW_/canvasH_, i.e. also the doc extent post-split).
    int docW = w->docW();
    int docH = w->docH();
    ensureFBO(w, docW, docH);

    // ── Render surface into FBO ───────────────────────────────────────────
    glBindFramebuffer(GL_FRAMEBUFFER, s.fboId);
    glViewport(0, 0, docW, docH);
    glClearColor(0.f, 0.f, 0.f, 0.f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    float orthoM[16];
    // No pan/zoom baked in here — the surface always renders at 1:1
    // doc-space. Pan/zoom is applied later, at composite time in render().
    localOrtho(0.f, float(docW), float(docH), 0.f, orthoM);
    Canvas2DBackend_setMVP(s.backend, orthoM);
    {
        Canvas2D ctx(s.backend, docW, docH);
        w->activeSurface_->render(ctx);
    }

    // Scrollbars are viewport-space UI chrome — drawn at viewport
    // (physical widget) size, not doc size.
    int physW = w->canvasW_, physH = w->canvasH_;
    w->updateSBGeometry(physW, physH);
    {
        // Scrollbars draw via the same plain-GLES2 Painter used everywhere
        // else on Android (flux_painter_android.cpp)
        GraphicsContext gctx;
        Painter p(gctx);
        p.drawScrollbar(w->hBar_, physW, physH);
        p.drawScrollbar(w->vBar_, physW, physH);
    }
    w->hBar_.tick(w->frameDt_);
    w->vBar_.tick(w->frameDt_);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // Mark the UI pass dirty so render() blits the updated FBO this frame
    w->needsPaint = true;

    if (w->activeSurface_->needsContinuousRedraw())
        w->Widget::markNeedsPaint(); // calls base to propagate
}

// ─────────────────────────────────────────────────────────────────────────────
// CanvasWidget member implementations (Android)
// ─────────────────────────────────────────────────────────────────────────────

CanvasWidget::CanvasWidget()
    : hBar_(CustomScrollbar::Axis::Horizontal), vBar_(CustomScrollbar::Axis::Vertical)
{
    autoWidth = true;
    autoHeight = true;
    width = 400;
    height = 300;
}

CanvasWidget::~CanvasWidget()
{
    if (activeSurface_)
    {
        activeSurface_->destroy();
        activeSurface_.reset();
    }
    pendingSurface_.reset();
    deleteFBO(this);
    deleteGL(this);
    s_androidState.erase(this);
}

std::shared_ptr<CanvasWidget> CanvasWidget::setViewportEnabled(bool e)
{
    viewportEnabled_ = e;
    return ptr();
}
std::shared_ptr<CanvasWidget> CanvasWidget::setScrollbarsEnabled(bool e)
{
    scrollbarsEnabled_ = e;
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
    // w/h are logical pixels (matching the public API on every other
    // platform) — store the DOCUMENT size in docW_/docH_, in physical
    // pixels, distinct from canvasW_/canvasH_ (the viewport's own
    // physical size, driven by computeLayout()).
    float dpi = FluxAndroid_getDpiScale();
    docW_ = int(w * dpi);
    docH_ = int(h * dpi);
    vp_.setCanvasSize(docW_, docH_);
    if (activeSurface_)
        activeSurface_->resize(docW_, docH_);
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

    float dpi = FluxAndroid_getDpiScale();
    int newPhysW = int(width * dpi);
    int newPhysH = int(height * dpi);

    if (newPhysW != canvasW_ || newPhysH != canvasH_)
    {
        canvasW_ = newPhysW;
        canvasH_ = newPhysH;
        // Cheap re-init only — GL objects (shader/atlas/fonts) are
        // untouched by a mere resize.
        androidState(this).viewportReady = false;
    }

    needsLayout = false;
}

// Pass 2: composite the doc-sized FBO into the UI with pan/zoom applied
// here (not baked into the FBO render) — mirrors Win32's D2D
// transform-then-DrawBitmap and Linux's cairo_translate/scale-then-paint
// composite step.
void CanvasWidget::render(GraphicsContext &ctx, FontCache &)
{
    auto &s = androidState(this);
    if (!s.fboTex)
    {
        needsPaint = false;
        return;
    }

    // Sample only the visible sub-rect of the doc-sized FBO, in doc-space
    // pixels, scaled into the widget's on-screen rect.
    float z = vp_.zoom();
    float srcX = vp_.offsetX();
    float srcY = vp_.offsetY();
    float srcW = (z > 0.f) ? float(canvasW_) / z : float(canvasW_);
    float srcH = (z > 0.f) ? float(canvasH_) / z : float(canvasH_);

    Painter p(ctx);
    Painter::ImageDrawParams ip;
    ip.image = (NativeImage)s.fboTex;
    ip.srcWidth = s.fboW;
    ip.srcHeight = s.fboH;
    ip.srcX = srcX;
    ip.srcY = srcY;
    ip.srcW = srcW;
    ip.srcH = srcH;
    ip.clipX = x;
    ip.clipY = y;
    ip.clipW = width;
    ip.clipH = height;
    ip.destX = (float)x;
    ip.destY = (float)y;
    ip.destW = (float)width;
    ip.destH = (float)height;
    // FBO render targets are bottom-up in GL's sampling convention while the
    // canvas surface was rendered top-down  — flip on the way out.
    ip.flipY = true;
    p.drawImage(ip);

    needsPaint = false;
}

void CanvasWidget::onDetach()
{
    if (activeSurface_)
    {
        activeSurface_->destroy();
        activeSurface_.reset();
    }
    pendingSurface_.reset();
    deleteFBO(this);
    deleteGL(this);
    s_androidState.erase(this);
    Widget::onDetach();
}

// Context is already gone by the time this fires (invoked from
// PlatformWindow::destroySurface() -> callbacks.onGLContextLost ->
// Widget::onGLContextLost() tree-wide broadcast, wired in
// FluxUI::wireCallbacks()) — CPU-side cleanup only, no glDelete* calls,
// since they would be issued against a dead/absent context.
void CanvasWidget::onGLContextLost()
{
    auto &s = androidState(this);
    if (s.backend)
    {
        Canvas2DBackend_destroyContextLost(s.backend);
        s.backend = nullptr;
    }
    backend_ = nullptr;
    s.fboId = s.fboTex = s.fboDepth = 0;
    s.fboW = s.fboH = 0;
    s.glObjectsReady = false;
    // viewportReady is deliberately left alone — vp_ (pan/zoom/offset) is
    // pure CPU-side state and survives context loss fine. Leaving it true
    // preserves the user's current pan/zoom across a background/foreground
    // cycle; ensureFBO() still recreates the FBO next frame regardless,
    // since fboId was zeroed above, and ensureGLObjects() rebuilds the
    // shader/atlas/backend from scratch since glObjectsReady is now false.
    Widget::onGLContextLost();
}

void CanvasWidget::markNeedsPaint()
{
    Widget::markNeedsPaint();
    needsPaint = true;
}

// Static pass-1 traversal called from android_main before the UI paint pass
void CanvasWidget::tickAllGL(Widget *root, int windowW, int windowH, float dpi)
{
    if (!root)
        return;
    if (auto *cw = dynamic_cast<CanvasWidget *>(root))
        glRenderPass(cw, windowW, windowH, dpi);
    for (auto &child : root->children)
        tickAllGL(child.get(), windowW, windowH, dpi);
}

// ── Touch input ───────────────────────────────────────────────────────────────

bool CanvasWidget::handleMouseDown(int mx, int my)
{
    if (!hitTest(mx, my))
        return false;
    int lx = mx - x, ly = my - y;
    bool hC = hBar_.onMouseDown(lx, ly, [this](float t)
                                { applyHScrollFraction(t); });
    bool vC = !hC && vBar_.onMouseDown(lx, ly, [this](float t)
                                       { applyVScrollFraction(t); });
    if (!hC && !vC)
    {
        if (viewportEnabled_)
            beginPan(lx, ly);
        else if (activeSurface_)
        {
            auto [cx, cy] = vp_.screenToCanvas(float(lx), float(ly));
            activeSurface_->onMouseDown(cx, cy);
        }
    }
    return true;
}

bool CanvasWidget::handleMouseMove(int mx, int my)
{
    if (!hitTest(mx, my) && !panning_)
        return false;
    int lx = mx - x, ly = my - y;
    bool hDrag = hBar_.onMouseMove(lx, ly);
    bool vDrag = vBar_.onMouseMove(lx, ly);
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
    return true;
}

bool CanvasWidget::hitTest(int mx, int my) const
{
    return mx >= x && mx < x + width && my >= y && my < y + height;
}

// ─────────────────────────────────────────────────────────────────────────────
// Shared helper implementations (defined once; other platforms link the same)
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
}
void CanvasWidget::pokeScrollbars()
{
    hBar_.poke();
    vBar_.poke();
    if (onViewportChanged)
        onViewportChanged(vp_.zoom());
}

void CanvasWidget::applyHScrollFraction(float thumbMin)
{
    float span = vp_.scrollbarH().thumbMax - vp_.scrollbarH().thumbMin;
    float range = vp_.canvasW() - vp_.viewW() / vp_.zoom();
    if (range <= 0.f)
        return;
    vp_.setOffsetX(thumbMin / (1.f - span) * range);
    pokeScrollbars();
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

#endif