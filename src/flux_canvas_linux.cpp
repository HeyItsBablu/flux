// flux_canvas_linux.cpp
// Compiled only on Linux desktop (not Android).
#if defined(__linux__) && !defined(__ANDROID__)

#include "flux/flux_canvas.hpp"

#include <glad/glad.h>
#include <SDL2/SDL.h>
#include <cassert>

// ─────────────────────────────────────────────────────────────────────────────
// Scrollbar shaders (GL 3.3 core)
// ─────────────────────────────────────────────────────────────────────────────

static const char* kLinuxSBVert = R"GLSL(
#version 330 core
layout(location=0) in vec2 aPos;
uniform mat4 uMVP;
void main(){ gl_Position = uMVP * vec4(aPos, 0.0, 1.0); }
)GLSL";

static const char* kLinuxSBFrag = R"GLSL(
#version 330 core
out vec4 fragColor;
uniform vec4 uColor;
void main(){ fragColor = uColor; }
)GLSL";

// ─────────────────────────────────────────────────────────────────────────────
// Linux-only state
// ─────────────────────────────────────────────────────────────────────────────

struct LinuxCanvasState {
    bool   initialized    = false;
    bool   repaintPending = false;
    int    lastW          = 0;
    int    lastH          = 0;
};

#include <unordered_map>
static std::unordered_map<CanvasWidget*, LinuxCanvasState> s_linuxState;

static LinuxCanvasState& linuxState(CanvasWidget* w) {
    return s_linuxState[w];
}

// ─────────────────────────────────────────────────────────────────────────────
// SDL repaint event type (registered once)
// ─────────────────────────────────────────────────────────────────────────────

static Uint32 s_repaintEventType_ = 0;

void CanvasWidget::initEventType() {
    if (s_repaintEventType_ == 0) {
        Uint32 t = SDL_RegisterEvents(1);
        s_repaintEventType_ = (t != (Uint32)-1) ? t : SDL_USEREVENT;
    }
}
Uint32 CanvasWidget::repaintEventType() { return s_repaintEventType_; }



// ─────────────────────────────────────────────────────────────────────────────
// Window dimension helpers
// ─────────────────────────────────────────────────────────────────────────────

static int getWindowLogicalWidth() {
    auto* inst = FluxUI::getCurrentInstance();
    if (!inst) return 1;
    auto* pw = inst->getPlatformWindowPtr();
    return pw ? pw->clientWidth() : 1;
}
static int getWindowLogicalHeight() {
    auto* inst = FluxUI::getCurrentInstance();
    if (!inst) return 1;
    auto* pw = inst->getPlatformWindowPtr();
    return pw ? pw->clientHeight() : 1;
}

// ─────────────────────────────────────────────────────────────────────────────
// Platform registration helpers
// ─────────────────────────────────────────────────────────────────────────────

static void registerWithPlatform(CanvasWidget* w) {
    auto* inst = FluxUI::getCurrentInstance();
    if (!inst) return;
    auto* pw = inst->getPlatformWindowPtr();
    if (pw) pw->registerCanvas_public(w);
}

static void unregisterWithPlatform(CanvasWidget* w) {
    auto* inst = FluxUI::getCurrentInstance();
    if (!inst) return;
    auto* pw = inst->getPlatformWindowPtr();
    if (pw) pw->unregisterCanvas_public(w);
}

// ─────────────────────────────────────────────────────────────────────────────
// scheduleRepaint
// ─────────────────────────────────────────────────────────────────────────────

static void scheduleRepaint(CanvasWidget* w) {
    if (w->onViewportChanged) w->onViewportChanged(w->vp_.zoom());
    auto& s = linuxState(w);
    if (!s.repaintPending) {
        s.repaintPending = true;
        SDL_Event e;
        SDL_zero(e);
        e.type       = (s_repaintEventType_ != 0)
                       ? s_repaintEventType_ : SDL_USEREVENT;
        e.user.code  = 0;
        e.user.data1 = w;
        SDL_PushEvent(&e);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// One-time initialisation
// ─────────────────────────────────────────────────────────────────────────────

static void initCanvas(CanvasWidget* w) {
    auto& s = linuxState(w);
    assert(w->canvasGL_ && !s.initialized);
    assert(w->width > 0 && w->height > 0);

    s.lastW = w->width;
    s.lastH = w->height;

    w->vp_.init(s.lastW, s.lastH, w->canvasW_, w->canvasH_);
    w->updateViewportSize(s.lastW, s.lastH);
    w->updateSBGeometry(s.lastW, s.lastH);
    w->vp_.fitToView();

    w->activatePendingSurface();
    w->lastTick_ = CanvasWidget::Clock::now();
    w->frameDt_  = 0.0;

    s.initialized = true;

    if (w->onViewportChanged) w->onViewportChanged(w->vp_.zoom());
    if (w->onGLResize)        w->onGLResize(s.lastW, s.lastH);
}

// ─────────────────────────────────────────────────────────────────────────────
// CanvasWidget member implementations (Linux)
// ─────────────────────────────────────────────────────────────────────────────

CanvasWidget::CanvasWidget()
    : hBar_(CustomScrollbar::Axis::Horizontal)
    , vBar_(CustomScrollbar::Axis::Vertical)
{
    autoWidth = autoHeight = false;
    width  = 400;
    height = 300;
    registerWithPlatform(this);


}

CanvasWidget::~CanvasWidget() {
    // destroyGLResources
    if (activeSurface_) { activeSurface_->destroy(); activeSurface_.reset(); }
    pendingSurface_.reset();
    if (sbProg_) { glDeleteProgram(sbProg_);         sbProg_ = 0; }
    if (sbVAO_)  { glDeleteVertexArrays(1, &sbVAO_); sbVAO_  = 0; }
    if (sbVBO_)  { glDeleteBuffers(1,      &sbVBO_); sbVBO_  = 0; }
    canvasGL_ = nullptr;
    linuxState(this).initialized = false;
    s_linuxState.erase(this);
    unregisterWithPlatform(this);
}

std::shared_ptr<CanvasWidget> CanvasWidget::setViewportEnabled(bool e) {
    viewportEnabled_ = e; return ptr();
}
std::shared_ptr<CanvasWidget> CanvasWidget::setScrollbarsEnabled(bool e) {
    scrollbarsEnabled_ = e;
    auto& s = linuxState(this);
    if (s.initialized) { updateViewportSize(s.lastW, s.lastH); scheduleRepaint(this); }
    return ptr();
}
bool CanvasWidget::scrollbarsEnabled() const { return scrollbarsEnabled_; }

RenderSurface*  CanvasWidget::getSurface() const { return activeSurface_.get(); }
const Viewport& CanvasWidget::viewport()   const { return vp_; }
Viewport&       CanvasWidget::viewport()         { return vp_; }

std::shared_ptr<CanvasWidget> CanvasWidget::setSize(int w, int h) {
    width = w; height = h;
    autoWidth = autoHeight = false;
    markNeedsLayout();
    return ptr();
}
std::shared_ptr<CanvasWidget> CanvasWidget::setCanvasSize(int w, int h) {
    canvasW_ = w; canvasH_ = h;
    auto& s = linuxState(this);
    if (s.initialized) {
        vp_.setCanvasSize(w, h);
        if (activeSurface_) activeSurface_->resize(w, h);
    }
    return ptr();
}
std::shared_ptr<CanvasWidget> CanvasWidget::redraw() { markNeedsPaint(); return ptr(); }

void CanvasWidget::computeLayout(GraphicsContext& /*ctx*/,
                                 const BoxConstraints& c, FontCache&) {
    width  = c.clampWidth (autoWidth  ? c.maxWidth  : width);
    height = c.clampHeight(autoHeight ? c.maxHeight : height);
    needsLayout = false;
}

void CanvasWidget::render(GraphicsContext& ctx, FontCache&) {
    if (ctx.cr) {
        cairo_save(ctx.cr);
        cairo_set_operator(ctx.cr, CAIRO_OPERATOR_CLEAR);
        cairo_rectangle(ctx.cr, double(x), double(y),
                        double(width), double(height));
        cairo_fill(ctx.cr);
        cairo_restore(ctx.cr);
    }
    needsPaint = false;
}

void CanvasWidget::onDetach() {
    if (activeSurface_) { activeSurface_->destroy(); activeSurface_.reset(); }
    pendingSurface_.reset();
    if (sbProg_) { glDeleteProgram(sbProg_);         sbProg_ = 0; }
    if (sbVAO_)  { glDeleteVertexArrays(1, &sbVAO_); sbVAO_  = 0; }
    if (sbVBO_)  { glDeleteBuffers(1,      &sbVBO_); sbVBO_  = 0; }
    canvasGL_ = nullptr;
    s_linuxState.erase(this);
    unregisterWithPlatform(this);
    Widget::onDetach();
}

void CanvasWidget::markNeedsPaint() {
    Widget::markNeedsPaint();
    scheduleRepaint(this);
}

// ── Called by PlatformWindow after GLAD is loaded ─────────────────────────────
void CanvasWidget::setCanvasGL(Canvas2DGL* gl) {
    canvasGL_ = gl;
    auto& s = linuxState(this);
    if (canvasGL_ && width > 0 && height > 0 && !s.initialized)
        initCanvas(this);
}

// ── Called by PlatformWindow on window resize ─────────────────────────────────
void CanvasWidget::onWindowResize(int /*newW*/, int /*newH*/) {
    auto& s = linuxState(this);
    if (!s.initialized) return;
    s.lastW = width;
    s.lastH = height;
    updateViewportSize(s.lastW, s.lastH);
    updateSBGeometry(s.lastW, s.lastH);
    if (onGLResize) onGLResize(s.lastW, s.lastH);
    scheduleRepaint(this);
}

void CanvasWidget::preRenderPass() {
    auto& s = linuxState(this);
    if (!canvasGL_ || !s.initialized) return;
    if (pendingSurface_) activatePendingSurface();
    if (!activeSurface_) return;

    auto  now = Clock::now();
    frameDt_  = std::chrono::duration<double>(now - lastTick_).count();
    lastTick_ = now;

    activeSurface_->update(frameDt_);
    activeSurface_->preRender();

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void CanvasWidget::glRenderPass() {
    auto& s = linuxState(this);
    if (!canvasGL_ || !s.initialized) return;
    if (!activeSurface_)              return;

    s.repaintPending = false;

    int glW = s.lastW < 1 ? 1 : s.lastW;
    int glH = s.lastH < 1 ? 1 : s.lastH;

    glEnable(GL_SCISSOR_TEST);
    int windowH = getWindowLogicalHeight();
    glScissor(x, windowH - y - glH, glW, glH);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    float z  = vp_.zoom();
    float ox = -vp_.offsetX() * z + float(x);
    float oy =  vp_.offsetY() * z + float(y);

    float ortho[16];
    glutil::ortho(0.f, float(getWindowLogicalWidth()),
                  float(getWindowLogicalHeight()), 0.f, ortho);

    float vp[16] = {
        z,  0,  0, 0,
        0,  z,  0, 0,
        0,  0,  1, 0,
        ox, oy, 0, 1
    };
    float mvp[16] = {};
    for (int col = 0; col < 4; ++col)
        for (int row = 0; row < 4; ++row)
            for (int k = 0; k < 4; ++k)
                mvp[col*4+row] += ortho[k*4+row] * vp[col*4+k];

    {
        Canvas2D ctx(canvasGL_, canvasW_, canvasH_, mvp);
        activeSurface_->render(ctx);
    }

    ensureSBProgram(kLinuxSBVert, kLinuxSBFrag);
    updateSBGeometry(glW, glH);
    renderScrollbarsGL(glW, glH, frameDt_);

    glDisable(GL_SCISSOR_TEST);

    if (activeSurface_->needsContinuousRedraw())
        scheduleRepaint(this);
}

bool CanvasWidget::isInitialized() const { return linuxState(const_cast<CanvasWidget*>(this)).initialized; }
bool CanvasWidget::needsRepaint()  const { return linuxState(const_cast<CanvasWidget*>(this)).repaintPending; }

bool CanvasWidget::containsPoint(int wx, int wy) const {
    return wx >= x && wx < (x + width) &&
           wy >= y && wy < (y + height);
}

void CanvasWidget::onMouseLeave() {
    hBar_.onMouseLeave();
    vBar_.onMouseLeave();
    scheduleRepaint(this);
}

// ── SDL event forwarding ──────────────────────────────────────────────────────
void CanvasWidget::handleSDLEvent(const SDL_Event& e) {
    switch (e.type) {
    case SDL_MOUSEBUTTONDOWN: onMouseButtonDown(e.button); break;
    case SDL_MOUSEBUTTONUP:   onMouseButtonUp  (e.button); break;
    case SDL_MOUSEMOTION:     onMouseMotion    (e.motion); break;
    case SDL_MOUSEWHEEL:      onMouseWheel     (e.wheel);  break;
    case SDL_KEYDOWN:         onKeyDownEvent   (e.key);    break;
    case SDL_KEYUP:           onKeyUpEvent     (e.key);    break;
    default: break;
    }
}

void CanvasWidget::onMouseButtonDown(const SDL_MouseButtonEvent& e) {
    SDL_CaptureMouse(SDL_TRUE);
    int sx = e.x, sy = e.y;

    if (e.button == SDL_BUTTON_MIDDLE && viewportEnabled_) {
        beginPan(sx, sy); return;
    }
    if (e.button == SDL_BUTTON_LEFT) {
        bool hC = hBar_.onMouseDown(sx, sy,
            [this](float t){ applyHScrollFraction(t); });
        bool vC = !hC && vBar_.onMouseDown(sx, sy,
            [this](float t){ applyVScrollFraction(t); });
        if (!hC && !vC) {
            if (viewportEnabled_ &&
                (SDL_GetKeyboardState(nullptr)[SDL_SCANCODE_SPACE] != 0)) {
                beginPan(sx, sy);
            } else if (activeSurface_) {
                auto [cx, cy] = vp_.screenToCanvas(float(sx), float(sy));
                activeSurface_->onMouseDown(cx, cy);
            }
        }
        scheduleRepaint(this);
    }
    if (e.button == SDL_BUTTON_RIGHT && activeSurface_) {
        auto [cx, cy] = vp_.screenToCanvas(float(sx), float(sy));
        activeSurface_->onRightMouseDown(cx, cy);
    }
}

void CanvasWidget::onMouseButtonUp(const SDL_MouseButtonEvent& e) {
    SDL_CaptureMouse(SDL_FALSE);
    int sx = e.x, sy = e.y;
    if (e.button == SDL_BUTTON_MIDDLE) { panning_ = false; return; }
    if (e.button == SDL_BUTTON_LEFT) {
        bool hR = hBar_.onMouseUp(sx, sy);
        bool vR = vBar_.onMouseUp(sx, sy);
        if (!hR && !vR) {
            if (panning_) { panning_ = false; }
            else if (activeSurface_) {
                auto [cx, cy] = vp_.screenToCanvas(float(sx), float(sy));
                activeSurface_->onMouseUp(cx, cy);
            }
        }
        scheduleRepaint(this);
    }
    if (e.button == SDL_BUTTON_RIGHT && activeSurface_) {
        auto [cx, cy] = vp_.screenToCanvas(float(e.x), float(e.y));
        activeSurface_->onMouseUp(cx, cy);
    }
}

void CanvasWidget::onMouseMotion(const SDL_MouseMotionEvent& e) {
    int sx = e.x, sy = e.y;
    bool hDrag = hBar_.onMouseMove(sx, sy);
    bool vDrag = vBar_.onMouseMove(sx, sy);
    scheduleRepaint(this);
    if (hDrag || vDrag) return;
    if (panning_) { continuePan(sx, sy); }
    else if (activeSurface_) {
        auto [cx, cy] = vp_.screenToCanvas(float(sx), float(sy));
        activeSurface_->onMouseMove(cx, cy);
    }
}

void CanvasWidget::onMouseWheel(const SDL_MouseWheelEvent& e) {
    if (!viewportEnabled_) return;
    static constexpr int kWheelDelta = 120;
    int  delta = e.y * kWheelDelta;
    bool ctrl  = platformCtrlDown();
    bool shift = platformShiftDown();
    int wmx, wmy;
    SDL_GetMouseState(&wmx, &wmy);
    int mx = wmx - x, my = wmy - y;
    if (ctrl) {
        float f = (delta > 0) ? 1.1f : 1.f / 1.1f;
        vp_.zoomToward(float(mx), float(my), f);
    } else if (shift) {
        vp_.panByScreen(float(delta) * 0.5f, 0.f);
    } else {
        vp_.panByScreen(0.f, -(float(delta) / float(kWheelDelta)) * 40.f);
    }
    pokeScrollbars();
    scheduleRepaint(this);
}

void CanvasWidget::onKeyDownEvent(const SDL_KeyboardEvent& e) {
    bool ctrl = platformCtrlDown(), consumed = false;
    if (ctrl && viewportEnabled_) {
        SDL_Keycode k = e.keysym.sym;
        if (k==SDLK_EQUALS||k==SDLK_PLUS||k==SDLK_KP_PLUS) {
            vp_.zoomIn();    pokeScrollbars(); scheduleRepaint(this); consumed=true;
        } else if (k==SDLK_MINUS||k==SDLK_KP_MINUS) {
            vp_.zoomOut();   pokeScrollbars(); scheduleRepaint(this); consumed=true;
        } else if (k==SDLK_0||k==SDLK_KP_0) {
            vp_.resetZoom(); pokeScrollbars(); scheduleRepaint(this); consumed=true;
        }
    }
    if (!consumed && activeSurface_)
        activeSurface_->onKeyDown(e.keysym.scancode);
}

void CanvasWidget::onKeyUpEvent(const SDL_KeyboardEvent& e) {
    if (activeSurface_) activeSurface_->onKeyUp(e.keysym.scancode);
}

// ─────────────────────────────────────────────────────────────────────────────
// Shared helper implementations
// ─────────────────────────────────────────────────────────────────────────────

void CanvasWidget::viewportDims(int glW, int glH, int& vpW, int& vpH) const {
    if (!viewportEnabled_ || !scrollbarsEnabled_) { vpW=glW; vpH=glH; return; }
    ScrollbarInfo h = vp_.scrollbarH(), v = vp_.scrollbarV();
    vpW = glW - (v.visible ? (int)kSBThick : 0);
    vpH = glH - (h.visible ? (int)kSBThick : 0);
    if (vpW < 1) vpW = 1;
    if (vpH < 1) vpH = 1;
}
void CanvasWidget::updateViewportSize(int glW, int glH) {
    int vpW, vpH; viewportDims(glW, glH, vpW, vpH);
    vp_.setViewSize(vpW, vpH);
}
void CanvasWidget::updateSBGeometry(int glW, int glH) {
    ScrollbarInfo h = vp_.scrollbarH(), v = vp_.scrollbarV();
    float hLen = float(glW) - (v.visible ? kSBThick : 0.f);
    hBar_.setGeometry(0.f, float(glH)-kSBThick, hLen);
    hBar_.setThumb(h.thumbMin, h.thumbMax,
                   h.visible && scrollbarsEnabled_ && viewportEnabled_);
    float vLen = float(glH) - (h.visible ? kSBThick : 0.f);
    vBar_.setGeometry(float(glW)-kSBThick, 0.f, vLen);
    vBar_.setThumb(v.thumbMin, v.thumbMax,
                   v.visible && scrollbarsEnabled_ && viewportEnabled_);
}
void CanvasWidget::beginPan(int sx, int sy) {
    panning_=true; panStartSX_=sx; panStartSY_=sy;
    panStartOX_=vp_.offsetX(); panStartOY_=vp_.offsetY();
}
void CanvasWidget::continuePan(int sx, int sy) {
    float dx=float(sx-panStartSX_), dy=float(sy-panStartSY_);
    vp_.setOffset(panStartOX_-dx/vp_.zoom(), panStartOY_+dy/vp_.zoom());
    pokeScrollbars(); scheduleRepaint(this);
}
void CanvasWidget::pokeScrollbars() { hBar_.poke(); vBar_.poke(); }
void CanvasWidget::applyHScrollFraction(float thumbMin) {
    float span  = vp_.scrollbarH().thumbMax - vp_.scrollbarH().thumbMin;
    float range = vp_.canvasW() - vp_.viewW()/vp_.zoom();
    if (range<=0.f) return;
    vp_.setOffsetX(thumbMin/(1.f-span)*range);
    pokeScrollbars(); scheduleRepaint(this);
}
void CanvasWidget::applyVScrollFraction(float thumbMin) {
    float span  = vp_.scrollbarV().thumbMax - vp_.scrollbarV().thumbMin;
    float range = vp_.canvasH() - vp_.viewH()/vp_.zoom();
    if (range<=0.f) return;
    float t = 1.f - thumbMin - span;
    vp_.setOffsetY(t/(1.f-span)*range);
    pokeScrollbars(); scheduleRepaint(this);
}
void CanvasWidget::activatePendingSurface() {
    if (!pendingSurface_) return;
    if (activeSurface_) { activeSurface_->destroy(); activeSurface_.reset(); }
    activeSurface_ = pendingSurface_;
    pendingSurface_.reset();
    activeSurface_->initialize(canvasW_, canvasH_);
    vp_.setCanvasSize(canvasW_, canvasH_);
}
void CanvasWidget::ensureSBProgram(const char* vert, const char* frag) {
    if (sbProg_) return;
    sbProg_  = glutil::linkProgram(vert, frag);
    assert(sbProg_ && "CanvasWidget: scrollbar shader link failed");
    sbMVP_   = glGetUniformLocation(sbProg_, "uMVP");
    sbColor_ = glGetUniformLocation(sbProg_, "uColor");
    if (!sbVAO_) glGenVertexArrays(1, &sbVAO_);
    if (!sbVBO_) glGenBuffers(1, &sbVBO_);
}
void CanvasWidget::renderSBCorner(int glW, int glH) {
    if (!hBar_.isVisible() || !vBar_.isVisible() || !scrollbarsEnabled_) return;
    float cx = float(glW) - kSBThick;
    float cy = float(glH) - kSBThick;
    float mvp[16];
    glutil::ortho(0.f, float(glW), float(glH), 0.f, mvp);
    glUseProgram(sbProg_);
    glUniformMatrix4fv(sbMVP_, 1, GL_FALSE, mvp);
    glUniform4f(sbColor_, 0.12f, 0.12f, 0.12f, 0.35f);
    float v[] = {
        cx,          cy,
        cx+kSBThick, cy,
        cx+kSBThick, cy+kSBThick,
        cx+kSBThick, cy+kSBThick,
        cx,          cy+kSBThick,
        cx,          cy
    };
    glBindVertexArray(sbVAO_);
    glBindBuffer(GL_ARRAY_BUFFER, sbVBO_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(v), v, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(float)*2, nullptr);
    glDisableVertexAttribArray(1);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
}
void CanvasWidget::renderScrollbarsGL(int glW, int glH, double dt) {
    bool hFade = hBar_.tick(dt);
    bool vFade = vBar_.tick(dt);

    if (!hBar_.needsRedraw() && !vBar_.needsRedraw()) {
        if ((hFade || vFade) && !linuxState(this).repaintPending) scheduleRepaint(this);
        return;
    }

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    hBar_.render(sbProg_, sbVAO_, sbVBO_, sbMVP_, sbColor_, glW, glH);
    vBar_.render(sbProg_, sbVAO_, sbVBO_, sbMVP_, sbColor_, glW, glH);
    renderSBCorner(glW, glH);

    if ((hFade || vFade) && !linuxState(this).repaintPending) scheduleRepaint(this);
}

#endif