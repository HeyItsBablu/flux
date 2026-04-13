// flux/platform/android/flux_canvas_android.hpp
#pragma once
#ifndef __ANDROID__
#  error "flux_canvas_android.hpp must only be compiled on Android"
#endif

// ============================================================================
// CanvasWidget — Android / GLES2 / NanoVG
//
// Android has a single EGL surface owned by the app. The global NanoVG
// context (s_vg, GLES2 backend) is created in main_android.cpp and shared
// across all widgets. CanvasWidget draws into the active NanoVG frame that
// android_main opens each tick — no separate window or EGL context needed.
//
// Lifecycle:
//   android_main loop:
//     nvgBeginFrame(s_vg, ...)
//       Renderer::renderWidget(...)         ← walks widget tree
//         CanvasWidget::render(ctx, fc)     ← called here
//           tickAndRender()                 ← draws surface + scrollbars
//     nvgEndFrame(s_vg)
//     eglSwapBuffers(...)
// ============================================================================

#include "../../flux_platform.hpp"
#include "../../flux_widget.hpp"
#include "../../flux_keys.hpp"
#include "../../flux_core.hpp"
#include "../../flux_canvas2d.hpp"
#include "../../flux_scrollbar.hpp"
#include "../../flux_render_surface.hpp"
#include "../../flux_canvas_types.hpp"

#include <GLES2/gl2.h>
#include <EGL/egl.h>
#include <nanovg.h>
#include <nanovg_gl.h>
#include <android/log.h>

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

// ── Global NanoVG accessor (defined in main_android.cpp) ─────────────────────
extern NVGcontext* FluxAndroid_getVG();
extern float       FluxAndroid_getDpiScale();

class CanvasWidget : public Widget {
public:
    static constexpr float kSBThick = CustomScrollbar::kTrackThick;

    explicit CanvasWidget()
        : hBar_(CustomScrollbar::Axis::Horizontal),
          vBar_(CustomScrollbar::Axis::Vertical)
    {
        autoWidth = autoHeight = false;
        width  = 400;
        height = 300;
    }

    ~CanvasWidget() { destroySurface(); }

    // ── Configuration ─────────────────────────────────────────────────────

    std::shared_ptr<CanvasWidget> setViewportEnabled(bool e) {
        viewportEnabled_ = e;
        return ptr();
    }
    std::shared_ptr<CanvasWidget> setScrollbarsEnabled(bool e) {
        scrollbarsEnabled_ = e;
        scheduleRepaint();
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
        vp_.setCanvasSize(w, h);
        if (activeSurface_) activeSurface_->resize(w, h);
        return ptr();
    }
    std::shared_ptr<CanvasWidget> redraw() { markNeedsPaint(); return ptr(); }

    // ── Callbacks ─────────────────────────────────────────────────────────
    std::function<void(float zoom)>   onViewportChanged;
    std::function<void(int w, int h)> onGLResize;

    // ── Widget overrides ──────────────────────────────────────────────────

    void computeLayout(GraphicsContext& /*ctx*/,
                       const BoxConstraints& c, FontCache&) override {
        width  = c.clampWidth (autoWidth  ? c.maxWidth  : width);
        height = c.clampHeight(autoHeight ? c.maxHeight : height);
        needsLayout = false;
    }

    void render(GraphicsContext& /*ctx*/, FontCache&) override {
        // Ensure surface is initialized before first draw.
        ensureSurface();
        tickAndRender();
        needsPaint = false;
    }

    void onDetach() override {
        destroySurface();
        Widget::onDetach();
    }

    void markNeedsPaint() override {
        Widget::markNeedsPaint();
        scheduleRepaint();
    }

    // ── Touch / input ─────────────────────────────────────────────────────
    // Android input comes through FluxUI callbacks → widget handleMouse* chain.
    // We convert logical coords to canvas coords and forward to the surface.

    bool handleMouseDown(int mx, int my) override {
        if (!hitTest(mx, my)) return false;
        int lx = mx - x, ly = my - y;   // widget-local coords

        bool hC = hBar_.onMouseDown(lx, ly, [this](float t){ applyHScrollFraction(t); });
        bool vC = !hC && vBar_.onMouseDown(lx, ly, [this](float t){ applyVScrollFraction(t); });

        if (!hC && !vC) {
            if (viewportEnabled_) {
                beginPan(lx, ly);
            } else if (activeSurface_) {
                auto [cx,cy] = vp_.screenToCanvas(float(lx), float(ly));
                activeSurface_->onMouseDown(cx, cy);
            }
        }
        scheduleRepaint();
        return true;
    }

    bool handleMouseMove(int mx, int my) override {
        if (!hitTest(mx, my) && !panning_) return false;
        int lx = mx - x, ly = my - y;

        bool hDrag = hBar_.onMouseMove(lx, ly);
        bool vDrag = vBar_.onMouseMove(lx, ly);
        scheduleRepaint();

        if (hDrag || vDrag) return true;

        if (panning_) {
            continuePan(lx, ly);
        } else if (activeSurface_) {
            auto [cx,cy] = vp_.screenToCanvas(float(lx), float(ly));
            activeSurface_->onMouseMove(cx, cy);
        }
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
                auto [cx,cy] = vp_.screenToCanvas(float(lx), float(ly));
                activeSurface_->onMouseUp(cx, cy);
            }
        }
        scheduleRepaint();
        return true;
    }

private:
    // ── State ─────────────────────────────────────────────────────────────
    bool viewportEnabled_   = true;
    bool scrollbarsEnabled_ = true;
    bool initialized_       = false;

    std::shared_ptr<CanvasWidget> ptr() {
        return std::static_pointer_cast<CanvasWidget>(shared_from_this());
    }

    bool hitTest(int mx, int my) const {
        return mx >= x && mx < x + width && my >= y && my < y + height;
    }

    std::shared_ptr<RenderSurface> activeSurface_, pendingSurface_;
    Viewport vp_;
    int canvasW_ = 512, canvasH_ = 512;

    CustomScrollbar hBar_, vBar_;

    using Clock = std::chrono::steady_clock;
    Clock::time_point lastTick_ = Clock::now();

    bool  panning_    = false;
    int   panStartSX_ = 0, panStartSY_ = 0;
    float panStartOX_ = 0, panStartOY_ = 0;

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
        int vpW, vpH;
        viewportDims(w, h, vpW, vpH);
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
        panning_    = true;
        panStartSX_ = sx; panStartSY_ = sy;
        panStartOX_ = vp_.offsetX(); panStartOY_ = vp_.offsetY();
    }
    void continuePan(int sx, int sy) {
        float dx = float(sx - panStartSX_), dy = float(sy - panStartSY_);
        vp_.setOffset(panStartOX_ - dx / vp_.zoom(),
                      panStartOY_ + dy / vp_.zoom());
        pokeScrollbars();
        scheduleRepaint();
    }
    void pokeScrollbars() { hBar_.poke(); vBar_.poke(); }

    void scheduleRepaint() {
        if (onViewportChanged) onViewportChanged(vp_.zoom());
        // On Android the render loop runs every frame — just mark dirty.
        markNeedsPaint();
    }

    // ── Scrollbar callbacks ───────────────────────────────────────────────

    void applyHScrollFraction(float thumbMin) {
        float span  = vp_.scrollbarH().thumbMax - vp_.scrollbarH().thumbMin;
        float range = vp_.canvasW() - vp_.viewW() / vp_.zoom();
        if (range <= 0.f) return;
        vp_.setOffsetX(thumbMin / (1.f - span) * range);
        pokeScrollbars(); scheduleRepaint();
    }
    void applyVScrollFraction(float thumbMin) {
        float span  = vp_.scrollbarV().thumbMax - vp_.scrollbarV().thumbMin;
        float range = vp_.canvasH() - vp_.viewH() / vp_.zoom();
        if (range <= 0.f) return;
        float t = 1.f - thumbMin - span;
        vp_.setOffsetY(t / (1.f - span) * range);
        pokeScrollbars(); scheduleRepaint();
    }

    // ── NanoVG scrollbar rendering ────────────────────────────────────────

    void renderScrollbarsNVG(NVGcontext* nvg, int glW, int glH, double dt) {
        bool hFade = hBar_.tick(dt);
        bool vFade = vBar_.tick(dt);

        if (!hBar_.needsRedraw() && !vBar_.needsRedraw()) {
            if (hFade || vFade) scheduleRepaint();
            return;
        }

        hBar_.renderNVG(nvg, glW, glH);
        vBar_.renderNVG(nvg, glW, glH);

        if (hBar_.isVisible() && vBar_.isVisible() && scrollbarsEnabled_) {
            float cx = float(glW) - kSBThick;
            float cy = float(glH) - kSBThick;
            nvgBeginPath(nvg);
            nvgRect(nvg, cx, cy, kSBThick, kSBThick);
            nvgFillColor(nvg, nvgRGBA(30, 30, 30, 90));
            nvgFill(nvg);
        }

        if (hFade || vFade) scheduleRepaint();
    }

    // ── Surface init ──────────────────────────────────────────────────────

    void ensureSurface() {
        if (initialized_) return;
        initialized_ = true;

        vp_.init(width, height, canvasW_, canvasH_);
        updateViewportSize(width, height);
        updateSBGeometry(width, height);
        vp_.fitToView();

        activatePendingSurface();
        lastTick_ = Clock::now();

        if (onViewportChanged) onViewportChanged(vp_.zoom());
        if (onGLResize)        onGLResize(width, height);
    }

    void activatePendingSurface() {
        if (!pendingSurface_) return;
        if (activeSurface_) {
            activeSurface_->destroy();
            activeSurface_.reset();
        }
        activeSurface_ = pendingSurface_;
        pendingSurface_.reset();
        activeSurface_->initialize(canvasW_, canvasH_);
        vp_.setCanvasSize(canvasW_, canvasH_);
    }

    // ── Main render ───────────────────────────────────────────────────────
    // Called from render() which is called from Renderer::renderWidget()
    // which is called from android_main INSIDE an active nvgBeginFrame.
    // We must NOT call nvgBeginFrame/nvgEndFrame ourselves.

    void tickAndRender() {
        NVGcontext* nvg = FluxAndroid_getVG();
        if (!nvg) return;
        if (pendingSurface_) activatePendingSurface();
        if (!activeSurface_) return;

        auto   now = Clock::now();
        double dt  = std::chrono::duration<double>(now - lastTick_).count();
        lastTick_  = now;

        activeSurface_->update(dt);

        float dpi = FluxAndroid_getDpiScale();
        int   glW = width;
        int   glH = height;

        // ── Save NVG state and set up a local transform for this widget ───
        // The parent frame renders at logical coords scaled by DPI.
        // We translate to widget position so Canvas2D coords start at (0,0).
        nvgSave(nvg);
        nvgTranslate(nvg, float(x), float(y));

        // ── Clip to widget bounds ─────────────────────────────────────────
        nvgScissor(nvg, 0.f, 0.f, float(glW), float(glH));

        // ── Surface render ────────────────────────────────────────────────
        {
            Canvas2D ctx(nvg, canvasW_, canvasH_);
            activeSurface_->render(ctx);
        }

        // ── Scrollbars ────────────────────────────────────────────────────
        updateSBGeometry(glW, glH);
        renderScrollbarsNVG(nvg, glW, glH, dt);

        // ── Restore NVG state ─────────────────────────────────────────────
        nvgResetScissor(nvg);
        nvgRestore(nvg);

        // Schedule continuous redraw if surface needs it.
        if (activeSurface_->needsContinuousRedraw())
            scheduleRepaint();
    }

    // ── Teardown ──────────────────────────────────────────────────────────

    void destroySurface() {
        if (activeSurface_) {
            activeSurface_->destroy();
            activeSurface_.reset();
        }
        pendingSurface_.reset();
        initialized_ = false;
    }
};

// ── Factory helpers ───────────────────────────────────────────────────────────

inline std::shared_ptr<CanvasWidget> Canvas() {
    return std::make_shared<CanvasWidget>();
}
inline std::shared_ptr<CanvasWidget> Canvas(int w, int h) {
    return std::make_shared<CanvasWidget>()->setSize(w, h);
}