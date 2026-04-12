// flux/platform/linux/flux_canvas_linux.hpp
#pragma once
#if !defined(__linux__) || defined(__ANDROID__)
  #error "flux_canvas_linux.hpp must only be compiled on Linux"
#endif

// ============================================================================
// CanvasWidget — Linux (SDL2 + OpenGL)
//
// Architecture differences from the Win32 version:
//
//   Win32   — two child HWNDs (frame + GL window), WGL context,
//             WM_USER+1 for async repaint, SetCapture/ReleaseCapture.
//
//   Linux   — no child windows; SDL has one window for the whole app.
//             We render into an offscreen FBO at the widget's pixel rect,
//             then blit that FBO into the main SDL/Cairo surface via
//             glReadPixels each frame (or keep it as GL if the whole app
//             moves to GL — see note below).
//
//             SDL2 does not support child windows, so the canvas is a
//             "logical region" within the single SDL window.  Input is
//             filtered by pixel bounds in the event handlers that the
//             parent FluxUI already routes to widgets.
//
//             GL context:  SDL_GL_CreateContext on the main SDL_Window.
//             The context is shared with the rest of the app if another
//             already exists; otherwise we create it once here and store
//             it in a process-wide singleton so subsequent CanvasWidgets
//             reuse it.
//
// Repaint strategy:
//   markNeedsPaint() pushes SDL_WINDOWEVENT_EXPOSED so the main loop
//   repaints on the next iteration — identical to FluxWin_markNeedsPaint()
//   in flux_window_linux.cpp.
//
// Scrollbar + viewport logic is identical to Win32; only the GL setup and
// input wiring differ.
// ============================================================================

#include "flux_platform.hpp"
#include "flux_widget.hpp"
#include "flux_keys.hpp"
#include "flux_core.hpp"

#include <glad/glad.h>
#include "../../flux_glutil.hpp"
#include "../../flux_scrollbar.hpp"
#include "../../flux_render_surface.hpp"
#include "../../flux_canvas_types.hpp"

#include <SDL2/SDL.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cmath>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// ============================================================================
// §1  Process-wide GL context singleton
// ============================================================================
//
// SDL2 gives us one window → one GL context for the whole process.
// Every CanvasWidget shares it; they distinguish their work via FBOs.

namespace FluxGLLinux {

struct GLContext {
    SDL_GLContext ctx    = nullptr;
    SDL_Window*   window = nullptr;
    bool          gladOk = false;

    static GLContext& instance() {
        static GLContext s;
        return s;
    }

    // Called the first time any CanvasWidget needs a GL context.
    bool ensure(SDL_Window* sdlWin) {
        if (ctx) return gladOk;
        window = sdlWin;

        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                            SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE,   24);

        ctx = SDL_GL_CreateContext(sdlWin);
        if (!ctx) {
            fprintf(stderr, "[FluxGL] SDL_GL_CreateContext failed: %s\n",
                    SDL_GetError());
            return false;
        }
        SDL_GL_MakeCurrent(sdlWin, ctx);
        SDL_GL_SetSwapInterval(0); // vsync off — we drive repaints manually

        gladOk = gladLoadGLLoader(
            reinterpret_cast<GLADloadproc>(SDL_GL_GetProcAddress));
        if (!gladOk) {
            fprintf(stderr, "[FluxGL] gladLoadGLLoader failed\n");
            return false;
        }

        fprintf(stderr, "[FluxGL] OpenGL %s | GLSL %s | %s\n",
                glGetString(GL_VERSION),
                glGetString(GL_SHADING_LANGUAGE_VERSION),
                glGetString(GL_RENDERER));

#ifndef NDEBUG
        if (GLAD_GL_ARB_debug_output || GLAD_GL_KHR_debug) {
            glEnable(GL_DEBUG_OUTPUT);
            glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
            glDebugMessageCallback(
                [](GLenum /*src*/, GLenum type, GLuint id, GLenum severity,
                   GLsizei /*len*/, const GLchar* msg, const void*) {
                    if (severity == GL_DEBUG_SEVERITY_NOTIFICATION) return;
                    fprintf(stderr, "[GL] type=0x%x id=%u: %s\n", type, id, msg);
                }, nullptr);
            glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE,
                                  GL_DEBUG_SEVERITY_NOTIFICATION,
                                  0, nullptr, GL_FALSE);
        }
#endif
        return true;
    }

    void makeCurrent() {
        if (ctx && window)
            SDL_GL_MakeCurrent(window, ctx);
    }

private:
    GLContext()  = default;
    ~GLContext() {
        if (ctx) { SDL_GL_DeleteContext(ctx); ctx = nullptr; }
    }
};

} // namespace FluxGLLinux

// ============================================================================
// §2  CanvasWidget
// ============================================================================

class CanvasWidget : public Widget {
public:
    static constexpr float kSBThick = CustomScrollbar::kTrackThick;

    explicit CanvasWidget()
        : hBar_(CustomScrollbar::Axis::Horizontal),
          vBar_(CustomScrollbar::Axis::Vertical) {
        autoWidth = autoHeight = false;
        width  = 400;
        height = 300;
    }

    ~CanvasWidget() { destroyGL(); }

    // ── Configuration ────────────────────────────────────────────────────────

    std::shared_ptr<CanvasWidget> setViewportEnabled(bool v) {
        viewportEnabled_ = v; return ptr();
    }
    std::shared_ptr<CanvasWidget> setScrollbarsEnabled(bool v) {
        scrollbarsEnabled_ = v;
        if (glReady_) { updateViewportSize(width, height); scheduleRepaint(); }
        return ptr();
    }
    bool scrollbarsEnabled() const { return scrollbarsEnabled_; }

    template <typename T, typename... A>
    std::shared_ptr<T> setSurface(A&&... a) {
        auto s = std::make_shared<T>(std::forward<A>(a)...);
        pendingSurface_ = s;
        return s;
    }

    RenderSurface*        getSurface() const { return activeSurface_.get(); }
    const Viewport& viewport()  const { return vp_; }
    Viewport&       viewport()        { return vp_; }

    std::shared_ptr<CanvasWidget> setSize(int w, int h) {
        width = w; height = h;
        autoWidth = autoHeight = false;
        markNeedsLayout();
        return ptr();
    }

    std::shared_ptr<CanvasWidget> setCanvasSize(int w, int h) {
        canvasW_ = w; canvasH_ = h;
        if (glReady_) {
            vp_.setCanvasSize(w, h);
            if (activeSurface_) activeSurface_->resize(w, h);
        }
        return ptr();
    }

    std::shared_ptr<CanvasWidget> redraw() { markNeedsPaint(); return ptr(); }

    std::function<void(float zoom)> onViewportChanged;
    std::function<void(int w, int h)> onGLResize;

    // ── Layout ───────────────────────────────────────────────────────────────

    void computeLayout(GraphicsContext& /*ctx*/, const BoxConstraints& c,
                       FontCache&) override {
        width  = c.clampWidth (autoWidth  ? c.maxWidth  : width);
        height = c.clampHeight(autoHeight ? c.maxHeight : height);
        needsLayout = false;
    }

    // ── Render — called by the FluxUI paint pass ──────────────────────────────
    //
    // The main paint pass runs in Cairo / software mode.  We intercept here,
    // switch to GL, render our FBO, blit it back into the Cairo surface via
    // glReadPixels, then return.  This keeps the rest of the widget tree
    // unaware of GL.

    void render(GraphicsContext& ctx, FontCache&) override {
        ensureGL(ctx);
        if (!glReady_) { needsPaint = false; return; }

        tickAndRender();

        // ── Blit FBO pixels into the Cairo surface ────────────────────────
        blitFBOtoCairo(ctx);

        needsPaint = false;
    }

    void onDetach() override {
        destroyGL();
        Widget::onDetach();
    }

    void markNeedsPaint() override {
        Widget::markNeedsPaint();
        // Push an expose event so the SDL main loop repaints
        SDL_Event e;
        SDL_zero(e);
        e.type         = SDL_WINDOWEVENT;
        e.window.event = SDL_WINDOWEVENT_EXPOSED;
        SDL_PushEvent(&e);
    }

    // ── Input — called by FluxUI event routing (after hit-test) ──────────────

    bool handleMouseDown(int mx, int my) override {
        if (!inBounds(mx, my)) return false;
        int lx = mx - x, ly = my - y;

        bool hC = hBar_.onMouseDown(lx, ly, [this](float t){ applyHScroll(t); });
        bool vC = !hC && vBar_.onMouseDown(lx, ly, [this](float t){ applyVScroll(t); });

        if (!hC && !vC) {
            if (viewportEnabled_ && Key::isShiftDown()) {
                beginPan(lx, ly);
            } else if (activeSurface_) {
                auto [cx, cy] = vp_.screenToCanvas(float(lx), float(ly));
                activeSurface_->onMouseDown(cx, cy);
            }
        }
        scheduleRepaint();
        return true;
    }

    bool handleMouseUp(int mx, int my) override {
        int lx = mx - x, ly = my - y;
        bool hR = hBar_.onMouseUp(lx, ly);
        bool vR = vBar_.onMouseUp(lx, ly);
        if (!hR && !vR) {
            if (panning_) {
                panning_ = false;
            } else if (activeSurface_) {
                auto [cx, cy] = vp_.screenToCanvas(float(lx), float(ly));
                activeSurface_->onMouseUp(cx, cy);
            }
        }
        scheduleRepaint();
        return true;
    }

    bool handleMouseMove(int mx, int my) override {
        int lx = mx - x, ly = my - y;
        bool hDrag = hBar_.onMouseMove(lx, ly);
        bool vDrag = vBar_.onMouseMove(lx, ly);
        scheduleRepaint();
        if (hDrag || vDrag) return true;
        if (panning_) {
            continuePan(lx, ly);
        } else if (inBounds(mx, my) && activeSurface_) {
            auto [cx, cy] = vp_.screenToCanvas(float(lx), float(ly));
            activeSurface_->onMouseMove(cx, cy);
        }
        return false;
    }

    bool handleMouseLeave() override {
        hBar_.onMouseLeave();
        vBar_.onMouseLeave();
        panning_ = false;
        scheduleRepaint();
        return false;
    }

    bool handleMouseWheel(int delta) override {
        if (!viewportEnabled_) return false;
        bool ctrl  = Key::isControlDown();
        bool shift = Key::isShiftDown();
        // Wheel is fired with screen coords already; we pan/zoom around centre
        float cx = float(x + width  / 2);
        float cy = float(y + height / 2);
        if (ctrl) {
            float f = (delta > 0) ? 1.1f : 1.f / 1.1f;
            vp_.zoomToward(cx - float(x), cy - float(y), f);
        } else if (shift) {
            vp_.panByScreen(float(delta) * 0.5f, 0.f);
        } else {
            vp_.panByScreen(0.f, -(float(delta) / WHEEL_DELTA) * 40.f);
        }
        pokeScrollbars();
        scheduleRepaint();
        return true;
    }

    bool handleKeyDown(int key) override {
        bool ctrl = Key::isControlDown();
        if (ctrl && viewportEnabled_) {
            if (key == Key::OemPlus  || key == Key::NumpadPlus)  { vp_.zoomIn();    pokeScrollbars(); scheduleRepaint(); return true; }
            if (key == Key::OemMinus || key == Key::NumpadMinus) { vp_.zoomOut();   pokeScrollbars(); scheduleRepaint(); return true; }
            if (key == '0')                                        { vp_.resetZoom(); pokeScrollbars(); scheduleRepaint(); return true; }
        }
        if (activeSurface_) activeSurface_->onKeyDown(key);
        return false;
    }

private:
    // ── State ────────────────────────────────────────────────────────────────

    bool viewportEnabled_   = true;
    bool scrollbarsEnabled_ = true;
    bool glReady_           = false;

    std::shared_ptr<RenderSurface> activeSurface_, pendingSurface_;
    Viewport vp_;
    int canvasW_ = 512, canvasH_ = 512;

    CustomScrollbar hBar_, vBar_;
    GLuint sbVAO_ = 0, sbVBO_ = 0, sbProg_ = 0;
    GLint  sbUMVP_ = -1, sbUColor_ = -1;

    // Offscreen FBO — we render here then readback into Cairo
    GLuint fbo_        = 0;
    GLuint fboTex_     = 0;
    GLuint fboDepth_   = 0;
    int    fboW_       = 0, fboH_ = 0;

    // Pixel readback buffer (RGBA, flipped)
    std::vector<uint8_t> readBuf_;

    using Clock = std::chrono::steady_clock;
    Clock::time_point lastTick_ = Clock::now();

    bool panning_    = false;
    int  panStartLX_ = 0, panStartLY_ = 0;
    float panStartOX_ = 0, panStartOY_ = 0;

    // ── Helpers ───────────────────────────────────────────────────────────────

    std::shared_ptr<CanvasWidget> ptr() {
        return std::static_pointer_cast<CanvasWidget>(shared_from_this());
    }

    bool inBounds(int mx, int my) const {
        return mx >= x && mx < x + width && my >= y && my < y + height;
    }

    // ── Viewport / scrollbar helpers ─────────────────────────────────────────

    void viewportDims(int glW, int glH, int& vpW, int& vpH) const {
        if (!viewportEnabled_ || !scrollbarsEnabled_) { vpW = glW; vpH = glH; return; }
        ScrollbarInfo h = vp_.scrollbarH(), v = vp_.scrollbarV();
        vpW = glW - (v.visible ? int(kSBThick) : 0);
        vpH = glH - (h.visible ? int(kSBThick) : 0);
        if (vpW < 1) vpW = 1;
        if (vpH < 1) vpH = 1;
    }

    void updateViewportSize(int glW, int glH) {
        int vpW, vpH;
        viewportDims(glW, glH, vpW, vpH);
        vp_.setViewSize(vpW, vpH);
    }

    void updateSBGeometry(int glW, int glH) {
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

    void pokeScrollbars() { hBar_.poke(); vBar_.poke(); }

    void scheduleRepaint() {
        if (onViewportChanged) onViewportChanged(vp_.zoom());
        markNeedsPaint();
    }

    void applyHScroll(float thumbMin) {
        float span  = vp_.scrollbarH().thumbMax - vp_.scrollbarH().thumbMin;
        float range = vp_.canvasW() - vp_.viewW() / vp_.zoom();
        if (range <= 0.f) return;
        vp_.setOffsetX(thumbMin / (1.f - span) * range);
        pokeScrollbars(); scheduleRepaint();
    }

    void applyVScroll(float thumbMin) {
        float span  = vp_.scrollbarV().thumbMax - vp_.scrollbarV().thumbMin;
        float range = vp_.canvasH() - vp_.viewH() / vp_.zoom();
        if (range <= 0.f) return;
        float t = 1.f - thumbMin - span;
        vp_.setOffsetY(t / (1.f - span) * range);
        pokeScrollbars(); scheduleRepaint();
    }

    // ── Pan helpers ───────────────────────────────────────────────────────────

    void beginPan(int lx, int ly) {
        panning_    = true;
        panStartLX_ = lx; panStartLY_ = ly;
        panStartOX_ = vp_.offsetX();
        panStartOY_ = vp_.offsetY();
    }

    void continuePan(int lx, int ly) {
        float dx = float(lx - panStartLX_), dy = float(ly - panStartLY_);
        vp_.setOffset(panStartOX_ - dx / vp_.zoom(),
                      panStartOY_ + dy / vp_.zoom());
        pokeScrollbars(); scheduleRepaint();
    }

    // ── GL initialisation ─────────────────────────────────────────────────────

    void ensureGL(GraphicsContext& /*ctx*/) {
        if (glReady_) return;

        // Grab (or create) the process-wide SDL GL context
        SDL_Window* sdlWin = nullptr;
        if (auto* ui = FluxUI::getCurrentInstance())
            sdlWin = ui->getPlatformWindow().handle();
        if (!sdlWin) return;

        auto& glCtx = FluxGLLinux::GLContext::instance();
        if (!glCtx.ensure(sdlWin)) return;
        glCtx.makeCurrent();

        vp_.init(width, height, canvasW_, canvasH_);
        updateViewportSize(width, height);
        updateSBGeometry(width, height);
        vp_.fitToView();

        buildFBO(width, height);
        buildSBResources();
        activatePendingSurface();

        lastTick_ = Clock::now();
        glReady_ = true;

        if (onViewportChanged) onViewportChanged(vp_.zoom());
        if (onGLResize)        onGLResize(width, height);
    }

    // ── FBO management ────────────────────────────────────────────────────────

    void buildFBO(int w, int h) {
        if (w < 1) w = 1;
        if (h < 1) h = 1;
        destroyFBO();

        glGenTextures(1, &fboTex_);
        glBindTexture(GL_TEXTURE_2D, fboTex_);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glBindTexture(GL_TEXTURE_2D, 0);

        glGenRenderbuffers(1, &fboDepth_);
        glBindRenderbuffer(GL_RENDERBUFFER, fboDepth_);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, w, h);
        glBindRenderbuffer(GL_RENDERBUFFER, 0);

        glGenFramebuffers(1, &fbo_);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, fboTex_, 0);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                                  GL_RENDERBUFFER, fboDepth_);

        GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE)
            fprintf(stderr, "[FluxGL] FBO incomplete: 0x%x\n", status);

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        fboW_ = w; fboH_ = h;
        readBuf_.resize(size_t(w) * h * 4);
    }

    void destroyFBO() {
        if (fbo_)      { glDeleteFramebuffers(1,  &fbo_);      fbo_      = 0; }
        if (fboTex_)   { glDeleteTextures(1,      &fboTex_);   fboTex_   = 0; }
        if (fboDepth_) { glDeleteRenderbuffers(1, &fboDepth_); fboDepth_ = 0; }
        fboW_ = fboH_ = 0;
        readBuf_.clear();
    }

    // ── Scrollbar GL resources ────────────────────────────────────────────────

    void buildSBResources() {
        const char* vert = R"GLSL(
#version 330 core
layout(location=0) in vec2 aPos;
uniform mat4 uMVP;
void main(){ gl_Position = uMVP * vec4(aPos, 0.0, 1.0); }
)GLSL";
        const char* frag = R"GLSL(
#version 330 core
uniform vec4 uColor;
out vec4 fragColor;
void main(){ fragColor = uColor; }
)GLSL";
        sbProg_  = glutil::linkProgram(vert, frag);
        assert(sbProg_);
        sbUMVP_  = glGetUniformLocation(sbProg_, "uMVP");
        sbUColor_= glGetUniformLocation(sbProg_, "uColor");

        glGenVertexArrays(1, &sbVAO_);
        glGenBuffers(1, &sbVBO_);
        glBindVertexArray(sbVAO_);
        glBindBuffer(GL_ARRAY_BUFFER, sbVBO_);
        glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 2 * 64, nullptr, GL_DYNAMIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 2, nullptr);
        glBindVertexArray(0);
    }

    void destroySBResources() {
        if (sbProg_) { glDeleteProgram(sbProg_);           sbProg_ = 0; }
        if (sbVAO_)  { glDeleteVertexArrays(1, &sbVAO_);   sbVAO_  = 0; }
        if (sbVBO_)  { glDeleteBuffers(1,      &sbVBO_);   sbVBO_  = 0; }
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

    // ── Render into FBO ───────────────────────────────────────────────────────

    void tickAndRender() {
        if (!glReady_ || !activeSurface_) return;

        FluxGLLinux::GLContext::instance().makeCurrent();

        if (pendingSurface_) activatePendingSurface();

        // Resize FBO if widget size changed
        if (width != fboW_ || height != fboH_) {
            buildFBO(width, height);
            updateViewportSize(width, height);
            updateSBGeometry(width, height);
            if (onGLResize) onGLResize(width, height);
        }

        auto now = Clock::now();
        double dt = std::chrono::duration<double>(now - lastTick_).count();
        lastTick_ = now;

        activeSurface_->update(dt);

        // Render scene into FBO
        glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
        glViewport(0, 0, fboW_, fboH_);

        float mvp[16];
        vp_.buildMVP(mvp);
        activeSurface_->render(mvp);

        // Render scrollbars on top of scene (inside same FBO)
        updateSBGeometry(fboW_, fboH_);
        bool hFade = hBar_.tick(dt);
        bool vFade = vBar_.tick(dt);

        if (hBar_.needsRedraw() || vBar_.needsRedraw()) {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            hBar_.render(sbProg_, sbVAO_, sbVBO_, sbUMVP_, sbUColor_, fboW_, fboH_);
            vBar_.render(sbProg_, sbVAO_, sbVBO_, sbUMVP_, sbUColor_, fboW_, fboH_);

            // Corner fill when both bars visible
            if (hBar_.isVisible() && vBar_.isVisible() && scrollbarsEnabled_) {
                float mvpCorner[16];
                glutil::ortho(0.f, float(fboW_), float(fboH_), 0.f, mvpCorner);
                glUseProgram(sbProg_);
                glUniformMatrix4fv(sbUMVP_, 1, GL_FALSE, mvpCorner);
                glUniform4f(sbUColor_, 0.12f, 0.12f, 0.12f, 0.35f);
                float cx = float(fboW_) - kSBThick, cy = float(fboH_) - kSBThick;
                float cv[] = {cx, cy, cx+kSBThick, cy, cx+kSBThick, cy+kSBThick,
                              cx+kSBThick, cy+kSBThick, cx, cy+kSBThick, cx, cy};
                glBindVertexArray(sbVAO_);
                glBindBuffer(GL_ARRAY_BUFFER, sbVBO_);
                glBufferData(GL_ARRAY_BUFFER, sizeof(cv), cv, GL_DYNAMIC_DRAW);
                glDrawArrays(GL_TRIANGLES, 0, 6);
                glBindVertexArray(0);
            }
            glDisable(GL_BLEND);
        }

        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        // Schedule next frame if surface needs continuous redraw or SBs fading
        if (activeSurface_->needsContinuousRedraw() || hFade || vFade)
            scheduleRepaint();
    }

    // ── Blit FBO → Cairo surface ──────────────────────────────────────────────
    //
    // We read the FBO pixels back to CPU and paint them into the Cairo context
    // at the widget's position. This is the "safe" interop path that doesn't
    // require the entire app to move to GL.
    //
    // Performance note: glReadPixels is a GPU stall. For a single image editor
    // canvas this is fine (~1ms for a 1080p region). If you add many canvases
    // you may want to move the whole renderer to GL and skip Cairo entirely.

    void blitFBOtoCairo(GraphicsContext& ctx) {
        if (!ctx.cr || readBuf_.empty()) return;

        FluxGLLinux::GLContext::instance().makeCurrent();
        glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo_);

        // GL origin is bottom-left; Cairo origin is top-left — we flip on read.
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, fboW_, fboH_,
                     GL_BGRA, GL_UNSIGNED_BYTE, readBuf_.data());

        glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);

        // Flip rows vertically into a cairo_surface_t
        int stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, fboW_);
        std::vector<uint8_t> flipped(size_t(stride) * fboH_);
        int srcStride = fboW_ * 4;
        for (int row = 0; row < fboH_; ++row) {
            memcpy(flipped.data() + row * stride,
                   readBuf_.data() + (fboH_ - 1 - row) * srcStride,
                   srcStride);
        }

        cairo_surface_t* img = cairo_image_surface_create_for_data(
            flipped.data(), CAIRO_FORMAT_ARGB32, fboW_, fboH_, stride);

        cairo_save(ctx.cr);
        cairo_set_source_surface(ctx.cr, img, double(x), double(y));
        cairo_rectangle(ctx.cr, double(x), double(y),
                        double(fboW_), double(fboH_));
        cairo_fill(ctx.cr);
        cairo_restore(ctx.cr);

        cairo_surface_destroy(img);
    }

    // ── Teardown ──────────────────────────────────────────────────────────────

    void destroyGL() {
        if (!glReady_) return;
        FluxGLLinux::GLContext::instance().makeCurrent();
        destroySBResources();
        destroyFBO();
        if (activeSurface_) { activeSurface_->destroy(); activeSurface_.reset(); }
        pendingSurface_.reset();
        glReady_ = false;
    }
};

// ============================================================================
// §3  Factory helpers
// ============================================================================

inline std::shared_ptr<CanvasWidget> Canvas() {
    return std::make_shared<CanvasWidget>();
}
inline std::shared_ptr<CanvasWidget> Canvas(int w, int h) {
    return std::make_shared<CanvasWidget>()->setSize(w, h);
}