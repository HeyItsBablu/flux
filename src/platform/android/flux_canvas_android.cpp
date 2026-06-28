// flux_canvas_android.cpp
// Compiled only on Android.
#ifdef __ANDROID__

#include "flux/flux_canvas.hpp"

#include <GLES3/gl3.h>
#include <EGL/egl.h>
#include <android/log.h>
#include "nanovg.h"
#include "nanovg_gl.h"

#define CANVAS_LOGI(...) __android_log_print(ANDROID_LOG_INFO, "CanvasWidget", __VA_ARGS__)
#define CANVAS_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "CanvasWidget", __VA_ARGS__)

extern float FluxAndroid_getDpiScale();
extern Canvas2DGL *FluxAndroid_getCanvasGL();
extern NVGcontext *FluxAndroid_getVG();

struct AndroidCanvasState
{
    bool glInitialized = false;

    // FBO
    GLuint fboId = 0;
    GLuint fboTex = 0;
    GLuint fboDepth = 0;
    int fboW = 0;
    int fboH = 0;
    int nvgImage = -1; // NanoVG handle wrapping fboTex
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
    NVGcontext *vg = FluxAndroid_getVG();
    if (s.nvgImage != -1 && vg)
    {
        nvgDeleteImage(vg, s.nvgImage);
        s.nvgImage = -1;
    }
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

    // Register texture with NanoVG so render() can blit it.
    // NVG_IMAGE_FLIPY because FBO Y=0 is bottom, NanoVG expects top-left.
    NVGcontext *vg = FluxAndroid_getVG();
    if (vg)
    {
        if (s.nvgImage != -1)
            nvgDeleteImage(vg, s.nvgImage);
#if defined(NANOVG_GLES2)
        s.nvgImage = nvglCreateImageFromHandleGLES2(vg, s.fboTex, physW, physH,
                                                    NVG_IMAGE_FLIPY);
#elif defined(NANOVG_GLES3)
        s.nvgImage = nvglCreateImageFromHandleGLES3(vg, s.fboTex, physW, physH,
                                                    NVG_IMAGE_FLIPY);
#else
        s.nvgImage = nvglCreateImageFromHandleGL3(vg, s.fboTex, physW, physH,
                                                  NVG_IMAGE_FLIPY);
#endif
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// GL init (lazy, first tickAllGL)
// ─────────────────────────────────────────────────────────────────────────────

static void ensureGL(CanvasWidget *w, int windowW, int windowH, float dpi)
{
    auto &s = androidState(w);
    if (s.glInitialized)
        return;
    s.glInitialized = true;

    w->canvasGL_ = FluxAndroid_getCanvasGL();

    // canvasW_/canvasH_ are now physical pixels set by computeLayout
    int physW = w->canvasW_ > 0 ? w->canvasW_ : windowW;
    int physH = w->canvasH_ > 0 ? w->canvasH_ : windowH;

    w->vp_.init(physW, physH, physW, physH);
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
// Pass 1: render surface into FBO  (called from android_main before NanoVG)
// ─────────────────────────────────────────────────────────────────────────────

static void glRenderPass(CanvasWidget *w, int windowW, int windowH, float dpi)
{
    ensureGL(w, windowW, windowH, dpi);
    if (!w->canvasGL_)
        return;
    if (w->pendingSurface_)
        w->activatePendingSurface();
    if (!w->activeSurface_)
        return;

    auto &s = androidState(w);

    auto now = CanvasWidget::Clock::now();
    w->frameDt_ = std::chrono::duration<double>(now - w->lastTick_).count();
    w->lastTick_ = now;

    w->activeSurface_->update(w->frameDt_);
    w->activeSurface_->preRender();

    int physW = int(w->width * dpi);
    int physH = int(w->height * dpi);
    ensureFBO(w, physW, physH);

    // ── Render surface into FBO ───────────────────────────────────────────
    glBindFramebuffer(GL_FRAMEBUFFER, s.fboId);
    glViewport(0, 0, physW, physH);
    glClearColor(0.f, 0.f, 0.f, 0.f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    float z = w->vp_.zoom();
    float ox = -w->vp_.offsetX() * z;
    float oy = w->vp_.offsetY() * z;

    float orthoM[16];
    glutil::ortho(0.f, float(physW), float(physH), 0.f, orthoM);

    float vpM[16] = {
        z, 0, 0, 0,
        0, z, 0, 0,
        0, 0, 1, 0,
        ox, oy, 0, 1};
    float mvp[16] = {};
    for (int col = 0; col < 4; ++col)
        for (int row = 0; row < 4; ++row)
            for (int k = 0; k < 4; ++k)
                mvp[col * 4 + row] += orthoM[k * 4 + row] * vpM[col * 4 + k];

    {
        Canvas2D ctx(w->canvasGL_, w->canvasW_, w->canvasH_, mvp);
        w->activeSurface_->render(ctx);
    }

    w->updateSBGeometry(physW, physH);
    {
        // Scrollbars draw into the NanoVG layer via Painter.
        // GraphicsContext on Android wraps the NVGcontext implicitly
        // (Painter uses FluxAndroid_getVG() internally).
        GraphicsContext gctx;
        Painter p(gctx);
        p.drawScrollbar(w->hBar_, physW, physH);
        p.drawScrollbar(w->vBar_, physW, physH);
    }
    w->hBar_.tick(w->frameDt_);
    w->vBar_.tick(w->frameDt_);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // Mark NanoVG pass dirty so render() gets called this frame
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
    autoWidth = true;  // was false — must match Win32/Linux
    autoHeight = true; // was false
    width = 400;
    height = 300;
}

CanvasWidget::~CanvasWidget()
{
    // Destroy surface and GL resources
    if (activeSurface_)
    {
        activeSurface_->destroy();
        activeSurface_.reset();
    }
    pendingSurface_.reset();
    deleteFBO(this);
    canvasGL_ = nullptr;
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
    canvasW_ = w;
    canvasH_ = h;
    vp_.setCanvasSize(w, h);
    if (activeSurface_)
        activeSurface_->resize(w, h);
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
        // Force ensureGL to reinitialise with correct dims
        androidState(this).glInitialized = false;
    }

    needsLayout = false;
}

// Pass 2 (NanoVG): blit FBO texture into the UI
void CanvasWidget::render(GraphicsContext & /*ctx*/, FontCache &)
{
    NVGcontext *vg = FluxAndroid_getVG();
    auto &s = androidState(this);
    if (!vg || s.nvgImage == -1)
    {
        needsPaint = false;
        return;
    }

    NVGpaint paint = nvgImagePattern(vg,
                                     float(x), float(y),
                                     float(width), float(height),
                                     0.f, s.nvgImage, 1.f);
    nvgBeginPath(vg);
    nvgRect(vg, float(x), float(y), float(width), float(height));
    nvgFillPaint(vg, paint);
    nvgFill(vg);

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
    canvasGL_ = nullptr;
    androidState(this).glInitialized = false;
    s_androidState.erase(this);
    Widget::onDetach();
}

void CanvasWidget::markNeedsPaint()
{
    Widget::markNeedsPaint();
    needsPaint = true;
}

// Static pass-1 traversal called from android_main before NanoVG
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
    activeSurface_->initialize(canvasW_, canvasH_);
    vp_.setCanvasSize(canvasW_, canvasH_);
}

void CanvasWidget::ensureSBProgram(const char *vert, const char *frag)
{
    if (sbProg_)
        return;
    sbProg_ = glutil::linkProgram(vert, frag);
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
        cx, cy, cx + kSBThick, cy,
        cx + kSBThick, cy + kSBThick, cx + kSBThick, cy + kSBThick,
        cx, cy + kSBThick, cx, cy};
    glBindVertexArray(sbVAO_);
    glBindBuffer(GL_ARRAY_BUFFER, sbVBO_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(v), v, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
}

void CanvasWidget::renderScrollbarsGL(int glW, int glH, double dt)
{
    bool hFade = hBar_.tick(dt);
    bool vFade = vBar_.tick(dt);
    if (!hBar_.needsRedraw() && !vBar_.needsRedraw())
        return;

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    hBar_.render(sbProg_, sbVAO_, sbVBO_, sbMVP_, sbColor_, glW, glH);
    vBar_.render(sbProg_, sbVAO_, sbVBO_, sbMVP_, sbColor_, glW, glH);
    renderSBCorner(glW, glH);
}

#endif