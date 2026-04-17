// flux/platform/linux/flux_canvas_linux.hpp
#pragma once
#if !defined(__linux__) || defined(__ANDROID__)
#  error "flux_canvas_linux.hpp must only be compiled on Linux desktop"
#endif

// ============================================================================
// CanvasWidget — Linux / Single-window / NanoVG
//
// Phase 2 redesign: CanvasWidget no longer owns any OS window, GL context,
// or NVG context. PlatformWindow owns all three and shares the NVGcontext*
// via setNVGContext(). The widget registers itself with PlatformWindow on
// construction and unregisters on detach.
//
// Rendering happens inside PlatformWindow::run()'s compositor — the platform
// calls tickAndRender() for each registered canvas between nvgBeginFrame and
// nvgEndFrame. The widget scissors to its own rect and renders into the
// shared frame.
//
// Everything that was in ensureWindow / moveWindow / destroyGL is gone.
// Viewport, scrollbars, pan/zoom, RenderSurface, Canvas2D — all unchanged.
// ============================================================================

#include "../../flux_platform.hpp"
#include "../../flux_widget.hpp"
#include "../../flux_keys.hpp"
#include "../../flux_core.hpp"
#include "../../flux_canvas2d.hpp"
#include "../../flux_scrollbar.hpp"
#include "../../flux_render_surface.hpp"
#include "../../flux_canvas_types.hpp"

#include <SDL2/SDL.h>
#include <nanovg.h>
#include <nanovg_gl.h>

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// Forward declaration — PlatformWindow is defined in flux_window.hpp which
// is included after this header at the application level.
class PlatformWindow;

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

        // Self-register with PlatformWindow so the compositor and event
        // router can find us. setNVGContext() will be called back from
        // registerCanvas() if GL is already initialised.
        registerWithPlatform();
    }

    ~CanvasWidget() {
        // onDetach() handles unregistration during normal widget tree
        // teardown. The destructor handles the case where the widget is
        // destroyed without ever being attached (e.g. early error paths).
        if (nvg_) {
            unregisterWithPlatform();
            nvg_ = nullptr;
        }
    }

    // ── Configuration ─────────────────────────────────────────────────────

    std::shared_ptr<CanvasWidget> setViewportEnabled(bool e) {
        viewportEnabled_ = e;
        return ptr();
    }
    std::shared_ptr<CanvasWidget> setScrollbarsEnabled(bool e) {
        scrollbarsEnabled_ = e;
        if (initialized_) {
            updateViewportSize(lastGLW_, lastGLH_);
            scheduleRepaint();
        }
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
            if (activeSurface_)
                activeSurface_->resize(w, h);
        }
        return ptr();
    }

    std::shared_ptr<CanvasWidget> redraw() {
        markNeedsPaint();
        return ptr();
    }

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

    // render() is called by the Cairo widget pass. In the single-window model
    // the actual GL rendering is driven by the compositor in PlatformWindow::run(),
    // not from here. We just record our position and mark layout done.
    void render(GraphicsContext& /*ctx*/, FontCache&) override {
        // Our x/y/width/height are now valid — the compositor uses them
        // to scissor and to do hit-testing. Just ensure the canvas has the
        // NVG context in case it was set before layout was known.
        if (!initialized_ && nvg_)
            initCanvas();
        needsPaint = false;
    }

    void onDetach() override {
        unregisterWithPlatform();

        if (activeSurface_) {
            activeSurface_->destroy();
            activeSurface_.reset();
        }
        pendingSurface_.reset();
        nvg_         = nullptr;
        initialized_ = false;

        Widget::onDetach();
    }

    void markNeedsPaint() override {
        Widget::markNeedsPaint();
        scheduleRepaint();
    }

    // ── Called by PlatformWindow after GL + NVG are ready ─────────────────
    // Also called when a canvas is registered after GL is already up.
    void setNVGContext(NVGcontext* nvg) {
        nvg_ = nvg;
        // Defer full init until layout has run (width/height must be valid).
        // If layout is already done, init immediately.
        if (nvg_ && width > 0 && height > 0 && !initialized_)
            initCanvas();
    }

    // ── Called by PlatformWindow on window resize ──────────────────────────
    void onWindowResize(int newW, int newH) {
        // Canvas occupies a sub-rect of the window, not the full window.
        // The widget layout system will update x/y/width/height via
        // computeLayout(). We only need to refresh viewport here if the
        // canvas fills the window (autoWidth/autoHeight case).
        if (autoWidth)  width  = newW;
        if (autoHeight) height = newH;
        if (initialized_) {
            lastGLW_ = width;
            lastGLH_ = height;
            updateViewportSize(width, height);
            updateSBGeometry (width, height);
            if (onGLResize) onGLResize(width, height);
            scheduleRepaint();
        }
    }

    // ── Called by the compositor in PlatformWindow::run() ─────────────────
    // Executes inside an active nvgBeginFrame / nvgEndFrame block.
    // Do NOT call nvgBeginFrame or nvgEndFrame here.
    void tickAndRender() {
        if (!nvg_ || !initialized_) return;
        if (pendingSurface_) activatePendingSurface();
        if (!activeSurface_) return;

        repaintPending_ = false;

        auto   now = Clock::now();
        double dt  = std::chrono::duration<double>(now - lastTick_).count();
        lastTick_  = now;

        activeSurface_->update(dt);

        int glW = lastGLW_ < 1 ? 1 : lastGLW_;
        int glH = lastGLH_ < 1 ? 1 : lastGLH_;

        // Save NVG state so our scissor / transform don't leak
        nvgSave(nvg_);

        // Scissor to this widget's screen rect
        nvgScissor(nvg_, float(x), float(y), float(glW), float(glH));

        // Fill background (avoids Cairo bleeding through)
        nvgBeginPath(nvg_);
        nvgRect(nvg_, float(x), float(y), float(glW), float(glH));
        nvgFillColor(nvg_, nvgRGBA(0, 0, 0, 255));
        nvgFill(nvg_);

        // Translate so surface draws in its own local coordinate space
        nvgTranslate(nvg_, float(x), float(y));

        {
            Canvas2D ctx(nvg_, canvasW_, canvasH_);
            activeSurface_->render(ctx);
        }

        updateSBGeometry(glW, glH);
        renderScrollbarsNVG(glW, glH, dt);

        nvgRestore(nvg_);

        if (activeSurface_->needsContinuousRedraw())
            scheduleRepaint();
    }

    // ── State queries ──────────────────────────────────────────────────────
    bool isInitialized() const { return initialized_; }

    // ── SDL event forwarding ───────────────────────────────────────────────
    // Called by PlatformWindow::handleSDLEvent with coords already
    // translated to canvas-local space (except wheel which has no position).
    void handleSDLEvent(const SDL_Event& e) {
        switch (e.type) {
        case SDL_MOUSEBUTTONDOWN: onMouseButtonDown(e.button); break;
        case SDL_MOUSEBUTTONUP:   onMouseButtonUp  (e.button); break;
        case SDL_MOUSEMOTION:     onMouseMotion    (e.motion); break;
        case SDL_MOUSEWHEEL:      onMouseWheel     (e.wheel);  break;
        case SDL_KEYDOWN:         onKeyDown        (e.key);    break;
        case SDL_KEYUP:           onKeyUp          (e.key);    break;
        default: break;
        }
    }

    // onMouseLeave is called by PlatformWindow on SDL_WINDOWEVENT_LEAVE
    void onMouseLeave() {
        hBar_.onMouseLeave();
        vBar_.onMouseLeave();
        scheduleRepaint();
    }

private:
    // ── State ─────────────────────────────────────────────────────────────
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

    // Non-owning pointer — lifetime managed by PlatformWindow
    NVGcontext* nvg_     = nullptr;
    int lastGLW_         = 0;
    int lastGLH_         = 0;

    bool  panning_    = false;
    int   panStartSX_ = 0, panStartSY_ = 0;
    float panStartOX_ = 0, panStartOY_ = 0;

    // ── Platform registration ─────────────────────────────────────────────

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

    // ── One-time canvas initialisation (runs after layout + NVG ready) ────

    void initCanvas() {
        assert(nvg_ && "initCanvas called without NVG context");
        assert(!initialized_);

        lastGLW_ = width;
        lastGLH_ = height;

        vp_.init(width, height, canvasW_, canvasH_);
        updateViewportSize(width, height);
        updateSBGeometry (width, height);
        vp_.fitToView();

        activatePendingSurface();
        lastTick_ = Clock::now();

        initialized_ = true;

        if (onViewportChanged) onViewportChanged(vp_.zoom());
        if (onGLResize)        onGLResize(width, height);
    }

    // ── Viewport / scrollbar geometry ─────────────────────────────────────

    void viewportDims(int glW, int glH, int& vpW, int& vpH) const {
        if (!viewportEnabled_ || !scrollbarsEnabled_) {
            vpW = glW; vpH = glH; return;
        }
        ScrollbarInfo h = vp_.scrollbarH(), v = vp_.scrollbarV();
        vpW = glW - (v.visible ? (int)kSBThick : 0);
        vpH = glH - (h.visible ? (int)kSBThick : 0);
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

    // ── Pan ───────────────────────────────────────────────────────────────

    void beginPan(int sx, int sy) {
        panning_    = true;
        panStartSX_ = sx; panStartSY_ = sy;
        panStartOX_ = vp_.offsetX(); panStartOY_ = vp_.offsetY();
    }
    void continuePan(int sx, int sy) {
        float dx = float(sx - panStartSX_);
        float dy = float(sy - panStartSY_);
        vp_.setOffset(panStartOX_ - dx / vp_.zoom(),
                      panStartOY_ + dy / vp_.zoom());
        pokeScrollbars();
        scheduleRepaint();
    }
    void pokeScrollbars() { hBar_.poke(); vBar_.poke(); }

    void scheduleRepaint() {
        if (onViewportChanged) onViewportChanged(vp_.zoom());
        if (!repaintPending_) {
            repaintPending_ = true;
            SDL_Event e;
            SDL_zero(e);
            e.type         = SDL_WINDOWEVENT;
            e.window.event = SDL_WINDOWEVENT_EXPOSED;
            SDL_PushEvent(&e);
        }
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

    void renderScrollbarsNVG(int glW, int glH, double dt) {
        bool hFade = hBar_.tick(dt);
        bool vFade = vBar_.tick(dt);

        if (!hBar_.needsRedraw() && !vBar_.needsRedraw()) {
            if ((hFade || vFade) && !repaintPending_)
                scheduleRepaint();
            return;
        }

        hBar_.renderNVG(nvg_, glW, glH);
        vBar_.renderNVG(nvg_, glW, glH);

        // Corner fill when both scrollbars are visible
        if (hBar_.isVisible() && vBar_.isVisible() && scrollbarsEnabled_) {
            float cx = float(glW) - kSBThick;
            float cy = float(glH) - kSBThick;
            nvgBeginPath(nvg_);
            nvgRect(nvg_, cx, cy, kSBThick, kSBThick);
            nvgFillColor(nvg_, nvgRGBA(30, 30, 30, 90));
            nvgFill(nvg_);
        }

        if ((hFade || vFade) && !repaintPending_)
            scheduleRepaint();
    }

    // ── Surface management ────────────────────────────────────────────────

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

    // ── SDL event handlers (coords already in canvas-local space) ─────────

    void onMouseButtonDown(const SDL_MouseButtonEvent& e) {
        SDL_CaptureMouse(SDL_TRUE);
        int sx = e.x, sy = e.y;

        if (e.button == SDL_BUTTON_MIDDLE && viewportEnabled_) {
            beginPan(sx, sy);
            return;
        }

        if (e.button == SDL_BUTTON_LEFT) {
            bool hC = hBar_.onMouseDown(sx, sy,
                [this](float t){ applyHScrollFraction(t); });
            bool vC = !hC && vBar_.onMouseDown(sx, sy,
                [this](float t){ applyVScrollFraction(t); });

            if (!hC && !vC) {
                bool spaceDown =
                    (SDL_GetKeyboardState(nullptr)[SDL_SCANCODE_SPACE] != 0);
                if (viewportEnabled_ && spaceDown) {
                    beginPan(sx, sy);
                } else if (activeSurface_) {
                    auto [cx, cy] =
                        vp_.screenToCanvas(float(sx), float(sy));
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

        if (e.button == SDL_BUTTON_MIDDLE) {
            panning_ = false;
            return;
        }

        if (e.button == SDL_BUTTON_LEFT) {
            bool hR = hBar_.onMouseUp(sx, sy);
            bool vR = vBar_.onMouseUp(sx, sy);
            if (!hR && !vR) {
                if (panning_) {
                    panning_ = false;
                } else if (activeSurface_) {
                    auto [cx, cy] =
                        vp_.screenToCanvas(float(sx), float(sy));
                    activeSurface_->onMouseUp(cx, cy);
                }
            }
            scheduleRepaint();
        }

        if (e.button == SDL_BUTTON_RIGHT && activeSurface_) {
            auto [cx, cy] =
                vp_.screenToCanvas(float(e.x), float(e.y));
            activeSurface_->onMouseUp(cx, cy);
        }
    }

    void onMouseMotion(const SDL_MouseMotionEvent& e) {
        int sx = e.x, sy = e.y;
        bool hDrag = hBar_.onMouseMove(sx, sy);
        bool vDrag = vBar_.onMouseMove(sx, sy);
        scheduleRepaint();
        if (hDrag || vDrag) return;
        if (panning_) {
            continuePan(sx, sy);
        } else if (activeSurface_) {
            auto [cx, cy] = vp_.screenToCanvas(float(sx), float(sy));
            activeSurface_->onMouseMove(cx, cy);
        }
    }

    void onMouseWheel(const SDL_MouseWheelEvent& e) {
        if (!viewportEnabled_) return;
        int  delta = e.y * WHEEL_DELTA;
        bool ctrl  = platformCtrlDown();
        bool shift = platformShiftDown();
        int  mx, my;
        SDL_GetMouseState(&mx, &my);
        // Translate to canvas-local
        mx -= x;
        my -= y;
        if (ctrl) {
            float f = (delta > 0) ? 1.1f : 1.f / 1.1f;
            vp_.zoomToward(float(mx), float(my), f);
        } else if (shift) {
            vp_.panByScreen(float(delta) * .5f, 0.f);
        } else {
            vp_.panByScreen(0.f, -(float(delta) / WHEEL_DELTA) * 40.f);
        }
        pokeScrollbars();
        scheduleRepaint();
    }

    void onKeyDown(const SDL_KeyboardEvent& e) {
        if (!activeSurface_) return;
        bool ctrl     = platformCtrlDown();
        bool consumed = false;
        if (ctrl && viewportEnabled_) {
            SDL_Keycode k = e.keysym.sym;
            if (k == SDLK_EQUALS || k == SDLK_PLUS || k == SDLK_KP_PLUS) {
                vp_.zoomIn();  pokeScrollbars(); scheduleRepaint();
                consumed = true;
            } else if (k == SDLK_MINUS || k == SDLK_KP_MINUS) {
                vp_.zoomOut(); pokeScrollbars(); scheduleRepaint();
                consumed = true;
            } else if (k == SDLK_0 || k == SDLK_KP_0) {
                vp_.resetZoom(); pokeScrollbars(); scheduleRepaint();
                consumed = true;
            }
        }
        if (!consumed)
            activeSurface_->onKeyDown(e.keysym.scancode);
    }

    void onKeyUp(const SDL_KeyboardEvent& e) {
        if (activeSurface_)
            activeSurface_->onKeyUp(e.keysym.scancode);
    }
};

// ── Factory helpers ───────────────────────────────────────────────────────────

inline std::shared_ptr<CanvasWidget> Canvas() {
    return std::make_shared<CanvasWidget>();
}
inline std::shared_ptr<CanvasWidget> Canvas(int w, int h) {
    return std::make_shared<CanvasWidget>()->setSize(w, h);
}