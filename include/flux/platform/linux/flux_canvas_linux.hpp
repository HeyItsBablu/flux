// flux/platform/linux/flux_canvas_linux.hpp
#pragma once
#if !defined(__linux__) || defined(__ANDROID__)
#  error "flux_canvas_linux.hpp must only be compiled on Linux desktop"
#endif

// ============================================================================
// CanvasWidget — Linux / SDL2 / pure GL 
// ============================================================================

#include "../../flux_platform.hpp"
#include "../../flux_widget.hpp"
#include "../../flux_keys.hpp"
#include "../../flux_core.hpp"
#include "../../flux_canvas2d.hpp"
#include "../../flux_scrollbar.hpp"
#include "../../flux_render_surface.hpp"
#include "../../flux_canvas_types.hpp"
#include "../../flux_glutil.hpp"

#include <glad/glad.h>
#include <SDL2/SDL.h>

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class CanvasWidget;
class PlatformWindow;

// ── Scrollbar flat-colour GL shader (one program shared per widget) ───────────
// Owned by CanvasWidget, created lazily on first glRenderPass().

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

class CanvasWidget : public Widget {
public:
    static constexpr float kSBThick = CustomScrollbar::kTrackThick;

    // ── One-time global SDL event type registration ───────────────────────────

    static void initEventType() {
        if (s_repaintEventType_ == 0) {
            Uint32 t = SDL_RegisterEvents(1);
            s_repaintEventType_ = (t != (Uint32)-1) ? t : SDL_USEREVENT;
        }
    }
    static Uint32 repaintEventType() { return s_repaintEventType_; }

    // ── Constructor / destructor ──────────────────────────────────────────────

    explicit CanvasWidget()
        : hBar_(CustomScrollbar::Axis::Horizontal),
          vBar_(CustomScrollbar::Axis::Vertical)
    {
        autoWidth = autoHeight = false;
        width  = 400;
        height = 300;
        registerWithPlatform();
    }

    ~CanvasWidget() {
        destroyGLResources();
        unregisterWithPlatform();
    }

    // ── Configuration ─────────────────────────────────────────────────────────

    std::shared_ptr<CanvasWidget> setViewportEnabled(bool e) {
        viewportEnabled_ = e;
        return ptr();
    }
    std::shared_ptr<CanvasWidget> setScrollbarsEnabled(bool e) {
        scrollbarsEnabled_ = e;
        if (initialized_) { updateViewportSize(lastW_, lastH_); scheduleRepaint(); }
        return ptr();
    }
    bool scrollbarsEnabled() const { return scrollbarsEnabled_; }

    template<typename T, typename... A>
    std::shared_ptr<T> setSurface(A&&... a) {
        auto s = std::make_shared<T>(std::forward<A>(a)...);
        pendingSurface_ = s;
        return s;
    }

    RenderSurface*  getSurface() const { return activeSurface_.get(); }
    const Viewport& viewport()   const { return vp_; }
    Viewport&       viewport()         { return vp_; }

    std::shared_ptr<CanvasWidget> setSize(int w, int h) {
        width = w; height = h;
        autoWidth = autoHeight = false;
        markNeedsLayout();
        return ptr();
    }
    std::shared_ptr<CanvasWidget> setCanvasSize(int w, int h) {
        canvasW_ = w; canvasH_ = h;
        if (initialized_) {
            vp_.setCanvasSize(w, h);
            if (activeSurface_) activeSurface_->resize(w, h);
        }
        return ptr();
    }
    std::shared_ptr<CanvasWidget> redraw() { markNeedsPaint(); return ptr(); }

    // ── Callbacks ─────────────────────────────────────────────────────────────
    std::function<void(float zoom)>   onViewportChanged;
    std::function<void(int w, int h)> onGLResize;

    // ── Widget overrides ──────────────────────────────────────────────────────

    void computeLayout(GraphicsContext& /*ctx*/,
                       const BoxConstraints& c, FontCache&) override {
        width  = c.clampWidth (autoWidth  ? c.maxWidth  : width);
        height = c.clampHeight(autoHeight ? c.maxHeight : height);
        needsLayout = false;
    }

void render(GraphicsContext& ctx, FontCache&) override {
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

    void onDetach() override {
        destroyGLResources();
        unregisterWithPlatform();
        Widget::onDetach();
    }

    void markNeedsPaint() override {
        Widget::markNeedsPaint();
        scheduleRepaint();
    }

    // ── Called by PlatformWindow after GLAD is loaded ─────────────────────────


    void setCanvasGL(Canvas2DGL* gl) {
        canvasGL_ = gl;
        if (canvasGL_ && width > 0 && height > 0 && !initialized_)
            initCanvas();
    }

    // ── Called by PlatformWindow on window resize ─────────────────────────────

    void onWindowResize(int /*newW*/, int /*newH*/) {
        if (!initialized_) return;
        lastW_ = width;
        lastH_ = height;
        updateViewportSize(lastW_, lastH_);
        updateSBGeometry (lastW_, lastH_);
        if (onGLResize) onGLResize(lastW_, lastH_);
        scheduleRepaint();
    }

    void preRenderPass() {
        if (!canvasGL_ || !initialized_) return;
        if (pendingSurface_) activatePendingSurface();
        if (!activeSurface_) return;

        auto   now = Clock::now();
        frameDt_   = std::chrono::duration<double>(now - lastTick_).count();
        lastTick_  = now;

        activeSurface_->update(frameDt_);
        activeSurface_->preRender();


        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }


    void glRenderPass() {
        if (!canvasGL_ || !initialized_) return;
        if (!activeSurface_)             return;

        repaintPending_ = false;

        int glW = lastW_ < 1 ? 1 : lastW_;
        int glH = lastH_ < 1 ? 1 : lastH_;


        glEnable(GL_SCISSOR_TEST);

        int windowH = getWindowLogicalHeight();
        glScissor(x, windowH - y - glH, glW, glH);

        // Clear widget area to background colour.
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);


        float z  = vp_.zoom();
        float ox = -vp_.offsetX() * z + float(x);
        float oy =  vp_.offsetY() * z + float(y);

        // Ortho: maps [x, x+glW] x [y, y+glH] → NDC (top-left origin, Y down).
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

        // ── Canvas2D render ───────────────────────────────────────────────────
        {
            Canvas2D ctx(canvasGL_, canvasW_, canvasH_, mvp);
            activeSurface_->render(ctx);
        }

        // ── Scrollbars ────────────────────────────────────────────────────────
        ensureSBProgram();
        updateSBGeometry(glW, glH);
        renderScrollbarsGL(glW, glH, frameDt_);

        glDisable(GL_SCISSOR_TEST);

        if (activeSurface_->needsContinuousRedraw())
            scheduleRepaint();
    }

    // ── State queries ─────────────────────────────────────────────────────────
    bool isInitialized() const { return initialized_; }
    bool needsRepaint()  const { return repaintPending_; }

    // ── SDL event forwarding ──────────────────────────────────────────────────

    void handleSDLEvent(const SDL_Event& e) {
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

    void onMouseLeave() {
        hBar_.onMouseLeave();
        vBar_.onMouseLeave();
        scheduleRepaint();
    }

    bool containsPoint(int wx, int wy) const {
        return wx >= x && wx < (x + width) &&
               wy >= y && wy < (y + height);
    }

private:
    // ── Static state ──────────────────────────────────────────────────────────
    inline static Uint32 s_repaintEventType_ = 0;

    // ── Instance state ────────────────────────────────────────────────────────
    bool viewportEnabled_   = true;
    bool scrollbarsEnabled_ = true;
    bool initialized_       = false;
    bool repaintPending_    = false;

    std::shared_ptr<CanvasWidget> ptr() {
        return std::static_pointer_cast<CanvasWidget>(shared_from_this());
    }

    std::shared_ptr<RenderSurface> activeSurface_, pendingSurface_;
    Viewport vp_;
    int canvasW_ = 512, canvasH_ = 512;

    CustomScrollbar hBar_, vBar_;

    using Clock = std::chrono::steady_clock;
    Clock::time_point lastTick_ = Clock::now();
    double frameDt_ = 0.0;


    Canvas2DGL* canvasGL_ = nullptr;

    // Per-widget scrollbar GL program.
    GLuint sbProg_  = 0;
    GLint  sbMVP_   = -1;
    GLint  sbColor_ = -1;
    GLuint sbVAO_   = 0;
    GLuint sbVBO_   = 0;

    int lastW_ = 0, lastH_ = 0;

    bool  panning_    = false;
    int   panStartSX_ = 0, panStartSY_ = 0;
    float panStartOX_ = 0, panStartOY_ = 0;

    // ── Window dimension helpers ───────────────────────────────────────────────

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

    // ── Platform registration ─────────────────────────────────────────────────

    void registerWithPlatform() {
        auto* inst = FluxUI::getCurrentInstance();
        if (!inst) return;
        auto* pw = inst->getPlatformWindowPtr();
        if (pw) pw->registerCanvas_public(this);
    }

    void unregisterWithPlatform() {
        auto* inst = FluxUI::getCurrentInstance();
        if (!inst) return;
        auto* pw = inst->getPlatformWindowPtr();
        if (pw) pw->unregisterCanvas_public(this);
    }

    // ── GL resource lifecycle ─────────────────────────────────────────────────

    void ensureSBProgram() {
        if (sbProg_) return;
        sbProg_  = glutil::linkProgram(kLinuxSBVert, kLinuxSBFrag);
        assert(sbProg_ && "CanvasWidget: scrollbar shader link failed");
        sbMVP_   = glGetUniformLocation(sbProg_, "uMVP");
        sbColor_ = glGetUniformLocation(sbProg_, "uColor");
        glGenVertexArrays(1, &sbVAO_);
        glGenBuffers(1, &sbVBO_);
    }

    void destroyGLResources() {
        if (activeSurface_) { activeSurface_->destroy(); activeSurface_.reset(); }
        pendingSurface_.reset();

        if (sbProg_) { glDeleteProgram(sbProg_);        sbProg_ = 0; }
        if (sbVAO_)  { glDeleteVertexArrays(1, &sbVAO_); sbVAO_ = 0; }
        if (sbVBO_)  { glDeleteBuffers(1, &sbVBO_);      sbVBO_ = 0; }

        canvasGL_    = nullptr;
        initialized_ = false;
    }

    // ── One-time initialisation ───────────────────────────────────────────────

    void initCanvas() {
        assert(canvasGL_ && !initialized_);
        assert(width > 0 && height > 0);

        lastW_ = width;
        lastH_ = height;

        vp_.init(lastW_, lastH_, canvasW_, canvasH_);
        updateViewportSize(lastW_, lastH_);
        updateSBGeometry (lastW_, lastH_);
        vp_.fitToView();

        activatePendingSurface();
        lastTick_ = Clock::now();
        frameDt_  = 0.0;

        initialized_ = true;

        if (onViewportChanged) onViewportChanged(vp_.zoom());
        if (onGLResize)        onGLResize(lastW_, lastH_);
    }

    // ── Viewport / scrollbar geometry ─────────────────────────────────────────

    void viewportDims(int glW, int glH, int& vpW, int& vpH) const {
        if (!viewportEnabled_ || !scrollbarsEnabled_) { vpW=glW; vpH=glH; return; }
        ScrollbarInfo h = vp_.scrollbarH(), v = vp_.scrollbarV();
        vpW = glW - (v.visible ? (int)kSBThick : 0);
        vpH = glH - (h.visible ? (int)kSBThick : 0);
        if (vpW < 1) vpW = 1;
        if (vpH < 1) vpH = 1;
    }
    void updateViewportSize(int glW, int glH) {
        int vpW, vpH; viewportDims(glW, glH, vpW, vpH);
        vp_.setViewSize(vpW, vpH);
    }
    void updateSBGeometry(int glW, int glH) {
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

    // ── Pan ───────────────────────────────────────────────────────────────────

    void beginPan(int sx, int sy) {
        panning_=true; panStartSX_=sx; panStartSY_=sy;
        panStartOX_=vp_.offsetX(); panStartOY_=vp_.offsetY();
    }
    void continuePan(int sx, int sy) {
        float dx=float(sx-panStartSX_), dy=float(sy-panStartSY_);
        vp_.setOffset(panStartOX_-dx/vp_.zoom(), panStartOY_+dy/vp_.zoom());
        pokeScrollbars(); scheduleRepaint();
    }
    void pokeScrollbars() { hBar_.poke(); vBar_.poke(); }

    void scheduleRepaint() {
        if (onViewportChanged) onViewportChanged(vp_.zoom());
        if (!repaintPending_) {
            repaintPending_ = true;
            SDL_Event e;
            SDL_zero(e);
            e.type       = (s_repaintEventType_ != 0)
                           ? s_repaintEventType_ : SDL_USEREVENT;
            e.user.code  = 0;
            e.user.data1 = this;
            SDL_PushEvent(&e);
        }
    }

    // ── Scrollbar callbacks ───────────────────────────────────────────────────

    void applyHScrollFraction(float thumbMin) {
        float span  = vp_.scrollbarH().thumbMax - vp_.scrollbarH().thumbMin;
        float range = vp_.canvasW() - vp_.viewW()/vp_.zoom();
        if (range<=0.f) return;
        vp_.setOffsetX(thumbMin/(1.f-span)*range);
        pokeScrollbars(); scheduleRepaint();
    }
    void applyVScrollFraction(float thumbMin) {
        float span  = vp_.scrollbarV().thumbMax - vp_.scrollbarV().thumbMin;
        float range = vp_.canvasH() - vp_.viewH()/vp_.zoom();
        if (range<=0.f) return;
        float t = 1.f - thumbMin - span;
        vp_.setOffsetY(t/(1.f-span)*range);
        pokeScrollbars(); scheduleRepaint();
    }

    // ── GL scrollbar rendering ────────────────────────────────────────────────

    void renderScrollbarsGL(int glW, int glH, double dt) {
        bool hFade = hBar_.tick(dt);
        bool vFade = vBar_.tick(dt);

        if (!hBar_.needsRedraw() && !vBar_.needsRedraw()) {
            if ((hFade || vFade) && !repaintPending_) scheduleRepaint();
            return;
        }


        float sbOrtho[16];
        glutil::ortho(0.f, float(glW), float(glH), 0.f, sbOrtho);

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        hBar_.render(sbProg_, sbVAO_, sbVBO_, sbMVP_, sbColor_, glW, glH);
        vBar_.render(sbProg_, sbVAO_, sbVBO_, sbMVP_, sbColor_, glW, glH);

        // Corner fill
        if (hBar_.isVisible() && vBar_.isVisible() && scrollbarsEnabled_) {
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

        if ((hFade || vFade) && !repaintPending_) scheduleRepaint();
    }

    // ── Surface management ────────────────────────────────────────────────────

    void activatePendingSurface() {
        if (!pendingSurface_) return;
        if (activeSurface_) { activeSurface_->destroy(); activeSurface_.reset(); }
        activeSurface_ = pendingSurface_;
        pendingSurface_.reset();
        activeSurface_->initialize(canvasW_, canvasH_);
        vp_.setCanvasSize(canvasW_, canvasH_);
    }

    // ── SDL modifier helpers ──────────────────────────────────────────────────

    static bool platformCtrlDown() {
        return (SDL_GetModState() & KMOD_CTRL) != 0;
    }
    static bool platformShiftDown() {
        return (SDL_GetModState() & KMOD_SHIFT) != 0;
    }

    // ── SDL event handlers ────────────────────────────────────────────────────

    void onMouseButtonDown(const SDL_MouseButtonEvent& e) {
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
            scheduleRepaint();
        }
        if (e.button == SDL_BUTTON_RIGHT && activeSurface_) {
            auto [cx, cy] = vp_.screenToCanvas(float(sx), float(sy));
            activeSurface_->onRightMouseDown(cx, cy);
        }
    }

    void onMouseButtonUp(const SDL_MouseButtonEvent& e) {
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
            scheduleRepaint();
        }
        if (e.button == SDL_BUTTON_RIGHT && activeSurface_) {
            auto [cx, cy] = vp_.screenToCanvas(float(e.x), float(e.y));
            activeSurface_->onMouseUp(cx, cy);
        }
    }

    void onMouseMotion(const SDL_MouseMotionEvent& e) {
        int sx = e.x, sy = e.y;
        bool hDrag = hBar_.onMouseMove(sx, sy);
        bool vDrag = vBar_.onMouseMove(sx, sy);
        scheduleRepaint();
        if (hDrag || vDrag) return;
        if (panning_) { continuePan(sx, sy); }
        else if (activeSurface_) {
            auto [cx, cy] = vp_.screenToCanvas(float(sx), float(sy));
            activeSurface_->onMouseMove(cx, cy);
        }
    }

    void onMouseWheel(const SDL_MouseWheelEvent& e) {
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
        scheduleRepaint();
    }

    void onKeyDownEvent(const SDL_KeyboardEvent& e) {
        bool ctrl = platformCtrlDown(), consumed = false;
        if (ctrl && viewportEnabled_) {
            SDL_Keycode k = e.keysym.sym;
            if (k==SDLK_EQUALS||k==SDLK_PLUS||k==SDLK_KP_PLUS) {
                vp_.zoomIn();    pokeScrollbars(); scheduleRepaint(); consumed=true;
            } else if (k==SDLK_MINUS||k==SDLK_KP_MINUS) {
                vp_.zoomOut();   pokeScrollbars(); scheduleRepaint(); consumed=true;
            } else if (k==SDLK_0||k==SDLK_KP_0) {
                vp_.resetZoom(); pokeScrollbars(); scheduleRepaint(); consumed=true;
            }
        }
        if (!consumed && activeSurface_)
            activeSurface_->onKeyDown(e.keysym.scancode);
    }

    void onKeyUpEvent(const SDL_KeyboardEvent& e) {
        if (activeSurface_) activeSurface_->onKeyUp(e.keysym.scancode);
    }
};

// ── Factory helpers ───────────────────────────────────────────────────────────

inline std::shared_ptr<CanvasWidget> Canvas() {
    return std::make_shared<CanvasWidget>();
}
inline std::shared_ptr<CanvasWidget> Canvas(int w, int h) {
    return std::make_shared<CanvasWidget>()->setSize(w, h);
}