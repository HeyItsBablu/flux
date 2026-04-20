// flux/platform/android/flux_canvas_android.hpp
#pragma once
#ifndef __ANDROID__
#  error "flux_canvas_android.hpp must only be compiled on Android"
#endif

#include "../../flux_platform.hpp"
#include "../../flux_widget.hpp"
#include "../../flux_keys.hpp"
#include "../../flux_core.hpp"
#include "../../flux_canvas2d.hpp"
#include "../../flux_scrollbar.hpp"
#include "../../flux_render_surface.hpp"
#include "../../flux_canvas_types.hpp"
#include "../../flux_glutil.hpp"

#include <GLES3/gl3.h>
#include <EGL/egl.h>
#include <android/log.h>
#include "nanovg.h"
#include "nanovg_gl.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#define CANVAS_LOGI(...) __android_log_print(ANDROID_LOG_INFO,  "CanvasWidget", __VA_ARGS__)
#define CANVAS_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "CanvasWidget", __VA_ARGS__)

extern float        FluxAndroid_getDpiScale();
extern Canvas2DGL*  FluxAndroid_getCanvasGL();
extern NVGcontext*  FluxAndroid_getVG();

// ─────────────────────────────────────────────────────────────────────────────
// CanvasWidget
//
// Architecture:
//   Pass 1 (tickAllGL)  – RenderSurface renders into a per-widget RGBA FBO.
//   Pass 2 (NanoVG)     – render() blits the FBO texture via nvgImagePattern,
//                         so NanoVG composites it naturally with the rest of UI.
//
// No stencil tricks, no shared framebuffer fighting.
// ─────────────────────────────────────────────────────────────────────────────

class CanvasWidget : public Widget {
public:
    // ── Construction / destruction ────────────────────────────────────────
    explicit CanvasWidget()
            : hBar_(CustomScrollbar::Axis::Horizontal)
            , vBar_(CustomScrollbar::Axis::Vertical)
    {
        autoWidth = autoHeight = false;
        width  = 400;
        height = 300;
    }

    ~CanvasWidget() { destroyGL(); }

    // ── Configuration ─────────────────────────────────────────────────────

    std::shared_ptr<CanvasWidget> setViewportEnabled(bool e) {
        viewportEnabled_ = e; return ptr();
    }
    std::shared_ptr<CanvasWidget> setScrollbarsEnabled(bool e) {
        scrollbarsEnabled_ = e; return ptr();
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
        vp_.setCanvasSize(w, h);
        if (activeSurface_) activeSurface_->resize(w, h);
        return ptr();
    }
    std::shared_ptr<CanvasWidget> redraw() { markNeedsPaint(); return ptr(); }

    std::function<void(float zoom)>   onViewportChanged;
    std::function<void(int w, int h)> onGLResize;

    // ── Widget overrides ──────────────────────────────────────────────────

    void computeLayout(GraphicsContext& /*ctx*/,
                       const BoxConstraints& c, FontCache&) override {
        width  = c.clampWidth (autoWidth  ? c.maxWidth  : width);
        height = c.clampHeight(autoHeight ? c.maxHeight : height);
        needsLayout = false;
    }

    // Called during the NanoVG pass — blits the FBO texture into the UI.
    void render(GraphicsContext& /*ctx*/, FontCache&) override {
        NVGcontext* vg = FluxAndroid_getVG();
        if (!vg || nvgImage_ == -1) { needsPaint = false; return; }

        // nvgImagePattern works in logical pixels (NanoVG is scaled by dpi).
        NVGpaint paint = nvgImagePattern(vg,
                                         float(x), float(y),
                                         float(width), float(height),
                                         0.f, nvgImage_, 1.f);

        nvgBeginPath(vg);
        nvgRect(vg, float(x), float(y), float(width), float(height));
        nvgFillPaint(vg, paint);
        nvgFill(vg);

        needsPaint = false;
    }

    void onDetach() override {
        destroyGL();
        Widget::onDetach();
    }

    void markNeedsPaint() override {
        Widget::markNeedsPaint();
        needsPaint = true;
    }

    // ── Pass 1: called from android_main before NanoVG ────────────────────

    static void tickAllGL(Widget* root, int windowW, int windowH, float dpi) {
        if (!root) return;
        if (auto* cw = dynamic_cast<CanvasWidget*>(root))
            cw->glRenderPass(windowW, windowH, dpi);
        for (auto& child : root->children)
            tickAllGL(child.get(), windowW, windowH, dpi);
    }

    // ── Touch input ───────────────────────────────────────────────────────

    bool handleMouseDown(int mx, int my) override {
        if (!hitTest(mx, my)) return false;
        int lx = mx - x, ly = my - y;
        bool hC = hBar_.onMouseDown(lx, ly,
                                    [this](float t){ applyHScrollFraction(t); });
        bool vC = !hC && vBar_.onMouseDown(lx, ly,
                                           [this](float t){ applyVScrollFraction(t); });
        if (!hC && !vC) {
            if (viewportEnabled_) beginPan(lx, ly);
            else if (activeSurface_) {
                auto [cx, cy] = vp_.screenToCanvas(float(lx), float(ly));
                activeSurface_->onMouseDown(cx, cy);
            }
        }
        return true;
    }

    bool handleMouseMove(int mx, int my) override {
        if (!hitTest(mx, my) && !panning_) return false;
        int lx = mx - x, ly = my - y;
        bool hDrag = hBar_.onMouseMove(lx, ly);
        bool vDrag = vBar_.onMouseMove(lx, ly);
        if (hDrag || vDrag) return true;
        if (panning_) continuePan(lx, ly);
        else if (activeSurface_) {
            auto [cx, cy] = vp_.screenToCanvas(float(lx), float(ly));
            activeSurface_->onMouseMove(cx, cy);
        }
        return true;
    }

    bool handleMouseUp(int mx, int my) override {
        int lx = mx - x, ly = my - y;
        bool hR = hBar_.onMouseUp(lx, ly);
        bool vR = vBar_.onMouseUp(lx, ly);
        if (!hR && !vR) {
            if (panning_) panning_ = false;
            else if (activeSurface_) {
                auto [cx, cy] = vp_.screenToCanvas(float(lx), float(ly));
                activeSurface_->onMouseUp(cx, cy);
            }
        }
        return true;
    }

private:
    // ── State ─────────────────────────────────────────────────────────────

    bool viewportEnabled_   = true;
    bool scrollbarsEnabled_ = true;
    bool glInitialized_     = false;

    std::shared_ptr<RenderSurface> activeSurface_, pendingSurface_;
    Viewport vp_;
    int canvasW_ = 512, canvasH_ = 512;

    CustomScrollbar hBar_, vBar_;

    using Clock = std::chrono::steady_clock;
    Clock::time_point lastTick_ = Clock::now();
    double frameDt_ = 0.0;

    Canvas2DGL* canvasGL_ = nullptr;

    // ── FBO ───────────────────────────────────────────────────────────────
    GLuint fboId_    = 0;
    GLuint fboTex_   = 0;
    GLuint fboDepth_ = 0;
    int    fboW_     = 0;
    int    fboH_     = 0;
    int    nvgImage_ = -1;   // NanoVG handle wrapping fboTex_

    // ── Scrollbar GL program (drawn into FBO) ─────────────────────────────
    static const char* kSBVert() {
        return
                "#version 300 es\n"
                "precision mediump float;\n"
                "layout(location=0) in vec2 aPos;\n"
                "uniform mat4 uMVP;\n"
                "void main(){ gl_Position = uMVP * vec4(aPos, 0.0, 1.0); }\n";
    }
    static const char* kSBFrag() {
        return
                "#version 300 es\n"
                "precision mediump float;\n"
                "out vec4 fragColor;\n"
                "uniform vec4 uColor;\n"
                "void main(){ fragColor = uColor; }\n";
    }

    GLuint sbProg_  = 0;
    GLint  sbMVP_   = -1;
    GLint  sbColor_ = -1;
    GLuint sbVAO_   = 0;
    GLuint sbVBO_   = 0;

    static constexpr float kSBThick = CustomScrollbar::kTrackThick;

    // ── Pan ───────────────────────────────────────────────────────────────
    bool  panning_    = false;
    int   panStartSX_ = 0, panStartSY_ = 0;
    float panStartOX_ = 0, panStartOY_ = 0;

    // ── Helpers ───────────────────────────────────────────────────────────

    std::shared_ptr<CanvasWidget> ptr() {
        return std::static_pointer_cast<CanvasWidget>(shared_from_this());
    }
    bool hitTest(int mx, int my) const {
        return mx >= x && mx < x + width && my >= y && my < y + height;
    }

    // ── FBO management ────────────────────────────────────────────────────

    void ensureFBO(int physW, int physH) {
        if (fboId_ && fboW_ == physW && fboH_ == physH) return;
        deleteFBO();

        fboW_ = physW; fboH_ = physH;

        glGenTextures(1, &fboTex_);
        glBindTexture(GL_TEXTURE_2D, fboTex_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, physW, physH, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);

        glGenRenderbuffers(1, &fboDepth_);
        glBindRenderbuffer(GL_RENDERBUFFER, fboDepth_);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, physW, physH);
        glBindRenderbuffer(GL_RENDERBUFFER, 0);

        glGenFramebuffers(1, &fboId_);
        glBindFramebuffer(GL_FRAMEBUFFER, fboId_);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, fboTex_, 0);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                  GL_RENDERBUFFER, fboDepth_);

        GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE)
            CANVAS_LOGE("FBO incomplete: 0x%x", status);

        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        // Register with NanoVG so render() can blit it.
        // The function name is backend-specific: nvglCreateImageFromHandleGLES2
        // when built with NANOVG_GLES2, nvglCreateImageFromHandleGL3 otherwise.
        NVGcontext* vg = FluxAndroid_getVG();
        if (vg) {
            if (nvgImage_ != -1) nvgDeleteImage(vg, nvgImage_);
            // NVG_IMAGE_FLIPY because FBO Y=0 is bottom, NanoVG expects top-left
#if defined(NANOVG_GLES2)
            nvgImage_ = nvglCreateImageFromHandleGLES2(vg, fboTex_, physW, physH,
                                                       NVG_IMAGE_FLIPY);
#elif defined(NANOVG_GLES3)
            nvgImage_ = nvglCreateImageFromHandleGLES3(vg, fboTex_, physW, physH,
                                                       NVG_IMAGE_FLIPY);
#else
            nvgImage_ = nvglCreateImageFromHandleGL3(vg, fboTex_, physW, physH,
                                                     NVG_IMAGE_FLIPY);
#endif
        }
    }

    void deleteFBO() {
        NVGcontext* vg = FluxAndroid_getVG();
        if (nvgImage_ != -1 && vg) { nvgDeleteImage(vg, nvgImage_); nvgImage_ = -1; }
        if (fboId_)    { glDeleteFramebuffers(1,  &fboId_);    fboId_    = 0; }
        if (fboTex_)   { glDeleteTextures(1,       &fboTex_);  fboTex_   = 0; }
        if (fboDepth_) { glDeleteRenderbuffers(1,  &fboDepth_);fboDepth_ = 0; }
        fboW_ = fboH_ = 0;
    }

    // ── GL init (lazy, first tickAllGL) ───────────────────────────────────

    void ensureGL(int windowW, int windowH, float dpi) {
        if (glInitialized_) return;
        glInitialized_ = true;

        canvasGL_ = FluxAndroid_getCanvasGL();

        // Scrollbar shader
        sbProg_  = glutil::linkProgram(kSBVert(), kSBFrag());
        sbMVP_   = glGetUniformLocation(sbProg_, "uMVP");
        sbColor_ = glGetUniformLocation(sbProg_, "uColor");
        glGenVertexArrays(1, &sbVAO_);
        glGenBuffers(1, &sbVBO_);

        vp_.init(width, height, canvasW_, canvasH_);
        updateViewportSize(width, height);
        vp_.fitToView();

        activatePendingSurface();
        lastTick_ = Clock::now();

        if (onViewportChanged) onViewportChanged(vp_.zoom());
        if (onGLResize)        onGLResize(width, height);
    }

    // ── Pass 1: render surface into FBO ──────────────────────────────────

    void glRenderPass(int windowW, int windowH, float dpi) {
        ensureGL(windowW, windowH, dpi);
        if (!canvasGL_) return;
        if (pendingSurface_) activatePendingSurface();
        if (!activeSurface_) return;

        auto now = Clock::now();
        frameDt_ = std::chrono::duration<double>(now - lastTick_).count();
        lastTick_ = now;

        activeSurface_->update(frameDt_);
        activeSurface_->preRender();

        // Physical pixel size of this widget
        int physW = int(width  * dpi);
        int physH = int(height * dpi);
        ensureFBO(physW, physH);

        // ── Render surface into FBO ───────────────────────────────────────
        glBindFramebuffer(GL_FRAMEBUFFER, fboId_);
        glViewport(0, 0, physW, physH);
        glClearColor(0.f, 0.f, 0.f, 0.f);   // transparent
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        // Build MVP: ortho mapping canvas coords → clip space.
        // Viewport pan/zoom is baked in so the user's pan/zoom works.
        float z   = vp_.zoom();
        float ox  = -vp_.offsetX() * z;
        float oy  =  vp_.offsetY() * z;

        float orthoM[16];
        // Y-down ortho (top=0, bottom=physH in canvas-scaled units)
        glutil::ortho(0.f, float(physW), float(physH), 0.f, orthoM);

        float vpM[16] = {
                z,  0,  0, 0,
                0,  z,  0, 0,
                0,  0,  1, 0,
                ox, oy,  0, 1
        };
        float mvp[16] = {};
        for (int col = 0; col < 4; ++col)
            for (int row = 0; row < 4; ++row)
                for (int k = 0; k < 4; ++k)
                    mvp[col*4+row] += orthoM[k*4+row] * vpM[col*4+k];

        {
            Canvas2D ctx(canvasGL_, canvasW_, canvasH_, mvp);
            activeSurface_->render(ctx);
        }

        // ── Scrollbars (also into FBO, in widget-local coords) ────────────
        renderScrollbarsGL(physW, physH, frameDt_);

        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        // Mark NanoVG pass dirty so render() gets called this frame
        needsPaint = true;

        if (activeSurface_->needsContinuousRedraw())
            Widget::markNeedsPaint();
    }

    // ── Scrollbar rendering (into current FBO) ────────────────────────────

    void ensureSBProgram() {
        if (sbProg_) return;
        sbProg_  = glutil::linkProgram(kSBVert(), kSBFrag());
        sbMVP_   = glGetUniformLocation(sbProg_, "uMVP");
        sbColor_ = glGetUniformLocation(sbProg_, "uColor");
        if (!sbVAO_) glGenVertexArrays(1, &sbVAO_);
        if (!sbVBO_) glGenBuffers(1, &sbVBO_);
    }

    void renderScrollbarsGL(int glW, int glH, double dt) {
        ensureSBProgram();
        updateSBGeometry(glW, glH);

        bool hFade = hBar_.tick(dt);
        bool vFade = vBar_.tick(dt);
        if (!hBar_.needsRedraw() && !vBar_.needsRedraw()) return;

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        hBar_.render(sbProg_, sbVAO_, sbVBO_, sbMVP_, sbColor_, glW, glH);
        vBar_.render(sbProg_, sbVAO_, sbVBO_, sbMVP_, sbColor_, glW, glH);

        // Corner square where both scrollbars meet
        if (hBar_.isVisible() && vBar_.isVisible() && scrollbarsEnabled_) {
            float cx = float(glW) - kSBThick;
            float cy = float(glH) - kSBThick;
            float mvp[16];
            glutil::ortho(0.f, float(glW), float(glH), 0.f, mvp);
            glUseProgram(sbProg_);
            glUniformMatrix4fv(sbMVP_, 1, GL_FALSE, mvp);
            glUniform4f(sbColor_, 0.12f, 0.12f, 0.12f, 0.35f);
            float v[] = {
                    cx,          cy,           cx+kSBThick, cy,
                    cx+kSBThick, cy+kSBThick,  cx+kSBThick, cy+kSBThick,
                    cx,          cy+kSBThick,  cx,          cy
            };
            glBindVertexArray(sbVAO_);
            glBindBuffer(GL_ARRAY_BUFFER, sbVBO_);
            glBufferData(GL_ARRAY_BUFFER, sizeof(v), v, GL_DYNAMIC_DRAW);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE,
                                  2*sizeof(float), nullptr);
            glDrawArrays(GL_TRIANGLES, 0, 6);
            glBindVertexArray(0);
        }
    }

    // ── Viewport / scrollbar geometry ─────────────────────────────────────

    void viewportDims(int glW, int glH, int& vpW, int& vpH) const {
        if (!viewportEnabled_ || !scrollbarsEnabled_) { vpW=glW; vpH=glH; return; }
        ScrollbarInfo h = vp_.scrollbarH(), v = vp_.scrollbarV();
        vpW = glW - (v.visible ? (int)kSBThick : 0);
        vpH = glH - (h.visible ? (int)kSBThick : 0);
        if (vpW < 1) vpW = 1;
        if (vpH < 1) vpH = 1;
    }
    void updateViewportSize(int w, int h) {
        int vpW, vpH; viewportDims(w, h, vpW, vpH);
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

    // ── Pan ───────────────────────────────────────────────────────────────

    void beginPan(int sx, int sy) {
        panning_ = true; panStartSX_ = sx; panStartSY_ = sy;
        panStartOX_ = vp_.offsetX(); panStartOY_ = vp_.offsetY();
    }
    void continuePan(int sx, int sy) {
        float dx = float(sx - panStartSX_), dy = float(sy - panStartSY_);
        vp_.setOffset(panStartOX_ - dx/vp_.zoom(),
                      panStartOY_ + dy/vp_.zoom());
        pokeScrollbars();
    }
    void pokeScrollbars() {
        hBar_.poke(); vBar_.poke();
        if (onViewportChanged) onViewportChanged(vp_.zoom());
    }

    void applyHScrollFraction(float thumbMin) {
        float span  = vp_.scrollbarH().thumbMax - vp_.scrollbarH().thumbMin;
        float range = vp_.canvasW() - vp_.viewW() / vp_.zoom();
        if (range <= 0.f) return;
        vp_.setOffsetX(thumbMin / (1.f - span) * range);
        pokeScrollbars();
    }
    void applyVScrollFraction(float thumbMin) {
        float span  = vp_.scrollbarV().thumbMax - vp_.scrollbarV().thumbMin;
        float range = vp_.canvasH() - vp_.viewH() / vp_.zoom();
        if (range <= 0.f) return;
        float t = 1.f - thumbMin - span;
        vp_.setOffsetY(t / (1.f - span) * range);
        pokeScrollbars();
    }

    // ── Surface lifecycle ─────────────────────────────────────────────────

    void activatePendingSurface() {
        if (!pendingSurface_) return;
        if (activeSurface_) { activeSurface_->destroy(); activeSurface_.reset(); }
        activeSurface_ = pendingSurface_;
        pendingSurface_.reset();
        activeSurface_->initialize(canvasW_, canvasH_);
        vp_.setCanvasSize(canvasW_, canvasH_);
    }

    void destroyGL() {
        if (activeSurface_) { activeSurface_->destroy(); activeSurface_.reset(); }
        pendingSurface_.reset();
        deleteFBO();
        if (sbProg_) { glDeleteProgram(sbProg_);          sbProg_ = 0; }
        if (sbVAO_)  { glDeleteVertexArrays(1, &sbVAO_);  sbVAO_  = 0; }
        if (sbVBO_)  { glDeleteBuffers(1,      &sbVBO_);  sbVBO_  = 0; }
        canvasGL_      = nullptr;
        glInitialized_ = false;
    }
};

// ── Factory helpers ───────────────────────────────────────────────────────────

inline std::shared_ptr<CanvasWidget> Canvas() {
    return std::make_shared<CanvasWidget>();
}
inline std::shared_ptr<CanvasWidget> Canvas(int w, int h) {
    return std::make_shared<CanvasWidget>()->setSize(w, h);
}