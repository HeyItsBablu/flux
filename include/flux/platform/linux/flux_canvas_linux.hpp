// flux/platform/linux/flux_canvas_linux.hpp
#pragma once
#if !defined(__linux__) || defined(__ANDROID__)
#  error "flux_canvas_linux.hpp must only be compiled on Linux desktop"
#endif

// ============================================================================
// CanvasWidget — Linux / SDL2 / WGL / NanoVG
//
// Owns:
//   • A separate SDL_Window with SDL_WINDOW_OPENGL
//   • An SDL_GLContext (OpenGL 3.3+ core)
//   • A NanoVG context (GL3 backend)
//   • CustomScrollbar widgets rendered via NanoVG
//   • Viewport pan / zoom
//
// The parent window uses Cairo/SDL software rendering. CanvasWidget creates
// its own SDL GL window positioned to overlap the parent at the correct
// screen coordinates — same visual result as Win32 child HWNDs.
//
// Repaints are triggered by pushing SDL_WINDOWEVENT_EXPOSED to the parent
// event queue, which causes the main loop to call FluxUI callbacks, which
// call Widget::render(), which calls tickAndRender() here.
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
#include <glad/glad.h>
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

    ~CanvasWidget() { destroyGL(); }

    // ── Configuration ─────────────────────────────────────────────────────

    std::shared_ptr<CanvasWidget> setViewportEnabled(bool e) {
        viewportEnabled_ = e;
        return ptr();
    }
    std::shared_ptr<CanvasWidget> setScrollbarsEnabled(bool e) {
        scrollbarsEnabled_ = e;
        if (glWindow_) { updateViewportSize(lastGLW_, lastGLH_); scheduleRepaint(); }
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
        if (glWindow_) {
            vp_.setCanvasSize(w, h);
            if (activeSurface_) {
                makeCurrent();
                activeSurface_->resize(w, h);
            }
        }
        return ptr();
    }
    std::shared_ptr<CanvasWidget> redraw() { markNeedsPaint(); return ptr(); }

    // ── Callbacks ─────────────────────────────────────────────────────────
    std::function<void(float zoom)>  onViewportChanged;
    std::function<void(int w, int h)> onGLResize;

    // ── Widget overrides ──────────────────────────────────────────────────

    void computeLayout(GraphicsContext& /*ctx*/,
                       const BoxConstraints& c, FontCache&) override {
        width  = c.clampWidth (autoWidth  ? c.maxWidth  : width);
        height = c.clampHeight(autoHeight ? c.maxHeight : height);
        needsLayout = false;
    }

    void render(GraphicsContext& ctx, FontCache&) override {
        ensureWindow(ctx);
        moveWindow();
        tickAndRender();
        needsPaint = false;
    }

    void onDetach() override {
        destroyGL();
        Widget::onDetach();
    }

    void markNeedsPaint() override {
        Widget::markNeedsPaint();
        scheduleRepaint();
    }

    // ── SDL event forwarding ──────────────────────────────────────────────
    // Called by the platform layer when an SDL event arrives for our window ID.

    void handleSDLEvent(const SDL_Event& e) {
        switch (e.type) {
        case SDL_MOUSEBUTTONDOWN: onMouseButtonDown(e.button); break;
        case SDL_MOUSEBUTTONUP:   onMouseButtonUp  (e.button); break;
        case SDL_MOUSEMOTION:     onMouseMotion    (e.motion); break;
        case SDL_MOUSEWHEEL:      onMouseWheel     (e.wheel);  break;
        case SDL_KEYDOWN:         onKeyDown        (e.key);    break;
        case SDL_KEYUP:           onKeyUp          (e.key);    break;
        case SDL_WINDOWEVENT:
            if (e.window.event == SDL_WINDOWEVENT_LEAVE)
                onMouseLeave();
            break;
        default: break;
        }
    }

    Uint32 sdlWindowID() const {
        return glWindow_ ? SDL_GetWindowID(glWindow_) : 0;
    }

private:
    // ── State ─────────────────────────────────────────────────────────────
    bool viewportEnabled_   = true;
    bool scrollbarsEnabled_ = true;

    std::shared_ptr<CanvasWidget> ptr() {
        return std::static_pointer_cast<CanvasWidget>(shared_from_this());
    }

    std::shared_ptr<RenderSurface> activeSurface_, pendingSurface_;
    Viewport vp_;
    int canvasW_ = 512, canvasH_ = 512;

    CustomScrollbar hBar_, vBar_;

    using Clock = std::chrono::steady_clock;
    Clock::time_point lastTick_ = Clock::now();

    SDL_Window*   glWindow_  = nullptr;
    SDL_GLContext glContext_  = nullptr;
    NVGcontext*   nvg_        = nullptr;

    bool repaintPending_ = false;
    int  lastGLW_ = 0, lastGLH_ = 0;

    bool  panning_    = false;
    int   panStartSX_ = 0, panStartSY_ = 0;
    float panStartOX_ = 0, panStartOY_ = 0;

    bool trackingLeave_ = false;

    // ── GL helpers ────────────────────────────────────────────────────────

    void makeCurrent() {
        if (glWindow_ && glContext_)
            SDL_GL_MakeCurrent(glWindow_, glContext_);
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
        float dx = float(sx - panStartSX_), dy = float(sy - panStartSY_);
        vp_.setOffset(panStartOX_ - dx / vp_.zoom(),
                      panStartOY_ + dy / vp_.zoom());
        pokeScrollbars();
        scheduleRepaint();
    }
    void pokeScrollbars() { hBar_.poke(); vBar_.poke(); }

    void scheduleRepaint() {
        if (onViewportChanged) onViewportChanged(vp_.zoom());
        // Push an EXPOSED event to the parent SDL window to wake the main loop.
        // FluxWin_markNeedsPaint() does exactly this — reuse the same mechanism.
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

        // Corner fill when both scrollbars visible
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

    // ── SDL event handlers ────────────────────────────────────────────────

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
                bool spaceDown = (SDL_GetKeyboardState(nullptr)[SDL_SCANCODE_SPACE] != 0);
                if (viewportEnabled_ && spaceDown) {
                    beginPan(sx, sy);
                } else if (activeSurface_) {
                    auto [cx,cy] = vp_.screenToCanvas(float(sx), float(sy));
                    activeSurface_->onMouseDown(cx, cy);
                }
            }
            scheduleRepaint();
        }
        if (e.button == SDL_BUTTON_RIGHT && activeSurface_) {
            auto [cx,cy] = vp_.screenToCanvas(float(sx), float(sy));
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
                    auto [cx,cy] = vp_.screenToCanvas(float(sx), float(sy));
                    activeSurface_->onMouseUp(cx, cy);
                }
            }
            scheduleRepaint();
        }
        if (e.button == SDL_BUTTON_RIGHT && activeSurface_) {
            auto [cx,cy] = vp_.screenToCanvas(float(e.x), float(e.y));
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
            auto [cx,cy] = vp_.screenToCanvas(float(sx), float(sy));
            activeSurface_->onMouseMove(cx, cy);
        }
    }

    void onMouseWheel(const SDL_MouseWheelEvent& e) {
        if (!viewportEnabled_) return;
        int   delta = e.y * WHEEL_DELTA;
        bool  ctrl  = platformCtrlDown();
        bool  shift = platformShiftDown();
        int   mx, my;
        SDL_GetMouseState(&mx, &my);
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
        bool ctrl = platformCtrlDown();
        bool consumed = false;
        if (ctrl && viewportEnabled_) {
            SDL_Keycode k = e.keysym.sym;
            if (k == SDLK_EQUALS || k == SDLK_PLUS || k == SDLK_KP_PLUS) {
                vp_.zoomIn(); pokeScrollbars(); scheduleRepaint(); consumed = true;
            } else if (k == SDLK_MINUS || k == SDLK_KP_MINUS) {
                vp_.zoomOut(); pokeScrollbars(); scheduleRepaint(); consumed = true;
            } else if (k == SDLK_0 || k == SDLK_KP_0) {
                vp_.resetZoom(); pokeScrollbars(); scheduleRepaint(); consumed = true;
            }
        }
        if (!consumed)
            activeSurface_->onKeyDown(e.keysym.scancode);
    }

    void onKeyUp(const SDL_KeyboardEvent& e) {
        if (activeSurface_)
            activeSurface_->onKeyUp(e.keysym.scancode);
    }

    void onMouseLeave() {
        hBar_.onMouseLeave();
        vBar_.onMouseLeave();
        scheduleRepaint();
    }

    // ── Window creation ───────────────────────────────────────────────────

    void ensureWindow(GraphicsContext& /*ctx*/) {
        if (glWindow_) return;

        // Compute absolute screen position of this widget inside the parent.
        SDL_Window* parentWin =
            static_cast<SDL_Window*>(
                FluxUI::getCurrentInstance()->getPlatformWindow().handle());

        int parentX = 0, parentY = 0;
        if (parentWin)
            SDL_GetWindowPosition(parentWin, &parentX, &parentY);

        // Request GL attributes before creating the window.
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                            SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER,  1);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE,   24);
        SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE,  8);  // NanoVG requires stencil
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 4);

        glWindow_ = SDL_CreateWindow(
            "",                                          // no title bar
            parentX + x, parentY + y,                   // absolute screen pos
            width, height,
            SDL_WINDOW_OPENGL | SDL_WINDOW_BORDERLESS |
            SDL_WINDOW_SHOWN  | SDL_WINDOW_SKIP_TASKBAR);

        if (!glWindow_) {
            // MSAA might not be supported — retry without it.
            SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 0);
            SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 0);
            glWindow_ = SDL_CreateWindow(
                "",
                parentX + x, parentY + y,
                width, height,
                SDL_WINDOW_OPENGL | SDL_WINDOW_BORDERLESS |
                SDL_WINDOW_SHOWN  | SDL_WINDOW_SKIP_TASKBAR);
        }

        assert(glWindow_ && "CanvasWidget: SDL_CreateWindow failed");

        glContext_ = SDL_GL_CreateContext(glWindow_);
        assert(glContext_ && "CanvasWidget: SDL_GL_CreateContext failed");
        SDL_GL_MakeCurrent(glWindow_, glContext_);
        SDL_GL_SetSwapInterval(0);  // no vsync — we drive repaints manually

        // Load GL via GLAD.
        if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
            fprintf(stderr, "CanvasWidget: gladLoadGLLoader failed\n");
            assert(false);
        }

        fprintf(stderr, "CanvasWidget GL: %s | GLSL: %s | %s\n",
                (const char*)glGetString(GL_VERSION),
                (const char*)glGetString(GL_SHADING_LANGUAGE_VERSION),
                (const char*)glGetString(GL_RENDERER));

        // NanoVG context.
        nvg_ = nvgCreateGL3(NVG_ANTIALIAS | NVG_STENCIL_STROKES);
        assert(nvg_ && "CanvasWidget: nvgCreateGL3 failed");

        // Register fonts from system paths.
        auto regFont = [this](const char* name, const char* path) {
            if (nvgCreateFont(nvg_, name, path) < 0)
                fprintf(stderr, "CanvasWidget: font '%s' not found at '%s'\n",
                        name, path);
        };
        regFont("sans",             "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf");
        regFont("sans-bold",        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf");
        regFont("sans-italic",      "/usr/share/fonts/truetype/dejavu/DejaVuSans-Oblique.ttf");
        regFont("sans-bold-italic", "/usr/share/fonts/truetype/dejavu/DejaVuSans-BoldOblique.ttf");
        regFont("mono",             "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf");
        regFont("mono-bold",        "/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf");

        // Viewport init.
        vp_.init(width, height, canvasW_, canvasH_);
        lastGLW_ = width;
        lastGLH_ = height;
        updateViewportSize(width, height);
        updateSBGeometry(width, height);
        vp_.fitToView();

        activatePendingSurface();
        lastTick_ = Clock::now();

        if (onViewportChanged) onViewportChanged(vp_.zoom());
        if (onGLResize)        onGLResize(width, height);
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

    // ── Main render ───────────────────────────────────────────────────────

    void tickAndRender() {
        if (!glContext_ || !nvg_) return;
        if (pendingSurface_) activatePendingSurface();
        if (!activeSurface_) return;

        makeCurrent();
        repaintPending_ = false;

        auto   now = Clock::now();
        double dt  = std::chrono::duration<double>(now - lastTick_).count();
        lastTick_  = now;

        activeSurface_->update(dt);

        int glW = lastGLW_ < 1 ? 1 : lastGLW_;
        int glH = lastGLH_ < 1 ? 1 : lastGLH_;

        glViewport(0, 0, glW, glH);
        glClearColor(0.f, 0.f, 0.f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        glEnable(GL_MULTISAMPLE);

        nvgBeginFrame(nvg_, float(glW), float(glH), 1.f);

        {
            Canvas2D ctx(nvg_, canvasW_, canvasH_);
            activeSurface_->render(ctx);
        }

        updateSBGeometry(glW, glH);
        renderScrollbarsNVG(glW, glH, dt);

        nvgEndFrame(nvg_);
        SDL_GL_SwapWindow(glWindow_);

        if (activeSurface_->needsContinuousRedraw())
            scheduleRepaint();
    }

    // ── Window repositioning ──────────────────────────────────────────────
    // Called every render() to keep the GL window aligned with the widget's
    // position inside the parent SDL window.

    void moveWindow() {
        if (!glWindow_) return;

        SDL_Window* parentWin =
            static_cast<SDL_Window*>(
                FluxUI::getCurrentInstance()->getPlatformWindow().handle());

        int parentX = 0, parentY = 0;
        if (parentWin)
            SDL_GetWindowPosition(parentWin, &parentX, &parentY);

        SDL_SetWindowPosition(glWindow_, parentX + x, parentY + y);
        SDL_SetWindowSize(glWindow_, width, height);

        if (width != lastGLW_ || height != lastGLH_) {
            makeCurrent();
            lastGLW_ = width;
            lastGLH_ = height;
            updateViewportSize(width, height);
            glViewport(0, 0, width, height);
            if (onGLResize) onGLResize(width, height);
        }
    }

    // ── Teardown ──────────────────────────────────────────────────────────

    void destroyGL() {
        if (glContext_) {
            makeCurrent();

            if (nvg_) {
                nvgDeleteGL3(nvg_);
                nvg_ = nullptr;
            }
            if (activeSurface_) {
                activeSurface_->destroy();
                activeSurface_.reset();
            }
            pendingSurface_.reset();

            SDL_GL_DeleteContext(glContext_);
            glContext_ = nullptr;
        }
        if (glWindow_) {
            SDL_DestroyWindow(glWindow_);
            glWindow_ = nullptr;
        }
    }
};

// ── Factory helpers ───────────────────────────────────────────────────────────

inline std::shared_ptr<CanvasWidget> Canvas() {
    return std::make_shared<CanvasWidget>();
}
inline std::shared_ptr<CanvasWidget> Canvas(int w, int h) {
    return std::make_shared<CanvasWidget>()->setSize(w, h);
}



// // Route events belonging to a CanvasWidget's GL window
// if (FluxUI::getCurrentInstance()) {
//     auto* root = FluxUI::getCurrentInstance()->getRoot().get();
//     // walk widgets, find CanvasWidget whose sdlWindowID() == e.window.windowID
//     // call canvasWidget->handleSDLEvent(e)
// }