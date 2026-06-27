#pragma once

// ============================================================================
// flux_canvas.hpp  —  CanvasWidget shared declaration
//
// Platform implementations live in:
//   flux_canvas_win32.cpp   (_WIN32)
//   flux_canvas_linux.cpp   (__linux__ && !__ANDROID__)
//   flux_canvas_android.cpp (__ANDROID__)
// ============================================================================

#include "flux_platform.hpp"
#include "flux_widget.hpp"
#include "flux_keys.hpp"
#include "flux_core.hpp"
#include "flux_canvas2d.hpp"
#include "flux_scrollbar.hpp"
#include "flux_render_surface.hpp"
#include "flux_canvas_types.hpp"

#if !defined(__APPLE__)
#include "flux_glutil.hpp"
#endif

#include <chrono>
#include <functional>
#include <memory>

// ── Platform-specific GL / windowing headers included by each .cpp ────────────
// (not included here so this header stays platform-neutral)

// Canvas2DGL is the per-context font/shader/atlas state used by the GL
// backend (flux_canvas2d_gl.cpp).  It is fully defined only in
// flux_canvas2d_backend.hpp (included by the platform .cpp files, never
// from here).  CanvasWidget only ever stores/forwards the pointer — it
// never dereferences it directly — so a forward declaration is sufficient.
struct Canvas2DGL;

// ─────────────────────────────────────────────────────────────────────────────
// CanvasWidget
//
// A Widget that hosts a RenderSurface rendered via OpenGL.
// Supports pan/zoom viewport, fade-in/out scrollbars, and optional
// continuous-redraw mode.
//
// Usage (same API on all platforms):
//
//   auto canvas = Canvas(800, 600);
//   canvas->setSurface<MyRenderSurface>(...);
//   canvas->setViewportEnabled(true);
//   canvas->setScrollbarsEnabled(true);
// ─────────────────────────────────────────────────────────────────────────────

class CanvasWidget : public Widget
{
public:
    static constexpr float kSBThick = CustomScrollbar::kTrackThick;

    // ── Construction / destruction ────────────────────────────────────────────
    explicit CanvasWidget();
    ~CanvasWidget();

    // ── Configuration ─────────────────────────────────────────────────────────

    std::shared_ptr<CanvasWidget> setViewportEnabled(bool e);
    std::shared_ptr<CanvasWidget> setScrollbarsEnabled(bool e);
    bool scrollbarsEnabled() const;

    template <typename T, typename... A>
    std::shared_ptr<T> setSurface(A &&...a)
    {
        auto s = std::make_shared<T>(std::forward<A>(a)...);
        pendingSurface_ = s;
        return s;
    }

    RenderSurface *getSurface() const;
    const Viewport &viewport() const;
    Viewport &viewport();

    std::shared_ptr<CanvasWidget> setSize(int w, int h);
    std::shared_ptr<CanvasWidget> setCanvasSize(int w, int h);
    std::shared_ptr<CanvasWidget> redraw();

    std::shared_ptr<CanvasWidget> setFlexGrow(int g)
    {
        flexGrow = g;
        markNeedsLayout();
        return ptr();
    }

    int docW() const { return docW_ ? docW_ : canvasW_; }
    int docH() const { return docH_ ? docH_ : canvasH_; }

    // ── Callbacks ─────────────────────────────────────────────────────────────
    std::function<void(float zoom)> onViewportChanged;
    std::function<void(int w, int h)> onGLResize;

    // ── Widget overrides ──────────────────────────────────────────────────────
    void computeLayout(GraphicsContext &ctx,
                       const BoxConstraints &c, FontCache &) override;
    void render(GraphicsContext &ctx, FontCache &) override;
    void onDetach() override;
    void markNeedsPaint() override;

    // ── Shared state (all platforms) ──────────────────────────────────────────

    bool viewportEnabled_ = true;
    bool scrollbarsEnabled_ = true;

    std::shared_ptr<RenderSurface> activeSurface_;
    std::shared_ptr<RenderSurface> pendingSurface_;

    Viewport vp_;
    int canvasW_ = 512;
    int canvasH_ = 512;

    int docW_ = 0;
    int docH_ = 0;

    CustomScrollbar hBar_;
    CustomScrollbar vBar_;

    using Clock = std::chrono::steady_clock;
    Clock::time_point lastTick_ = Clock::now();
    double frameDt_ = 0.0;

    // ── GL backend pointer (GL platforms only: Linux, Web, Android, macOS) ────
    // Opaque here — only flux_canvas2d_gl.cpp and the platform .cpp files
    // (which include flux_canvas2d_backend.hpp) dereference it. Win32 never
    // sets or reads this (it uses Canvas2DD2D / Canvas2DBackend locally
    // instead, stored in its own per-widget state map).
    Canvas2DGL *canvasGL_ = nullptr;

    // ── Shared helpers ────────────────────────────────────────────────────────

    std::shared_ptr<CanvasWidget> ptr()
    {
        return std::static_pointer_cast<CanvasWidget>(shared_from_this());
    }

    void viewportDims(int glW, int glH, int &vpW, int &vpH) const;
    void updateViewportSize(int glW, int glH);
    void updateSBGeometry(int glW, int glH);

    void beginPan(int sx, int sy);
    void continuePan(int sx, int sy);
    void pokeScrollbars();

    void applyHScrollFraction(float thumbMin);
    void applyVScrollFraction(float thumbMin);

    void activatePendingSurface();

    // ── Pan state ─────────────────────────────────────────────────────────────
    bool panning_ = false;
    int panStartSX_ = 0, panStartSY_ = 0;
    float panStartOX_ = 0, panStartOY_ = 0;

    // ── Scrollbar GL program (shared structure, created per platform) ─────────
    GLuint sbProg_ = 0;
    GLint sbMVP_ = -1;
    GLint sbColor_ = -1;
    GLuint sbVAO_ = 0;
    GLuint sbVBO_ = 0;

    void ensureSBProgram(const char *vert, const char *frag);
    void renderScrollbarsGL(int glW, int glH, double dt);
    void renderSBCorner(int glW, int glH);

    // ── Web-only ──────────────────────────────────────────────────────────────
#ifdef __EMSCRIPTEN__
    bool handleMouseDown(int mx, int my) override;
    bool handleMouseMove(int mx, int my) override;
    bool handleMouseUp(int mx, int my) override;
    bool handleMouseWheel(int delta) override;
    bool handleKeyDown(int keyCode) override;
#endif

// ── Win32-only input ──────────────────────────────────────────────────────
#if defined(_WIN32)
    bool handleMouseDown(int mx, int my) override;
    bool handleMouseMove(int mx, int my) override;
    bool handleMouseUp(int mx, int my) override;
    bool handleMouseWheel(int delta) override;
    bool handleKeyDown(int keyCode) override;
    bool handleChar(wchar_t ch) override;
#endif
    // ── Android-only ──────────────────────────────────────────────────────────
#ifdef __ANDROID__
    static void tickAllGL(Widget *root, int windowW, int windowH, float dpi);
    bool handleMouseDown(int mx, int my) override;
    bool handleMouseMove(int mx, int my) override;
    bool handleMouseUp(int mx, int my) override;
    bool hitTest(int mx, int my) const;
#endif

    // ── Linux-only ────────────────────────────────────────────────────────────
#if defined(__linux__) && !defined(__ANDROID__)
#include <SDL2/SDL.h>
    static void initEventType();
    static Uint32 repaintEventType();

    void setCanvasGL(Canvas2DGL *gl);
    void onWindowResize(int newW, int newH);
    void preRenderPass();
    void glRenderPass();

    bool isInitialized() const;
    bool needsRepaint() const;
    bool containsPoint(int wx, int wy) const;

    void onMouseLeave();
    void handleSDLEvent(const SDL_Event &e);

    void onMouseButtonDown(const SDL_MouseButtonEvent &e);
    void onMouseButtonUp(const SDL_MouseButtonEvent &e);
    void onMouseMotion(const SDL_MouseMotionEvent &e);
    void onMouseWheel(const SDL_MouseWheelEvent &e);
    void onKeyDownEvent(const SDL_KeyboardEvent &e);
    void onKeyUpEvent(const SDL_KeyboardEvent &e);
#endif

    // ── macOS-only ────────────────────────────────────────────────────────────
#ifdef __APPLE__
#if TARGET_OS_OSX
    static void initEventType();
    static uint32_t repaintEventType();

    void setCanvasGL(Canvas2DGL *gl);
    void onWindowResize(int newW, int newH);
    void preRenderPass();
    void glRenderPass();

    bool isInitialized() const;
    bool needsRepaint() const;
    bool containsPoint(int wx, int wy) const;

    void onMouseLeave();

    void onMouseButtonDown(int sx, int sy, int button);
    void onMouseButtonUp(int sx, int sy, int button);
    void onMouseMove(int sx, int sy);
    void onScrollWheel(float deltaY);
    void onKeyDown(int keyCode);
#endif
#endif
};

// ── Factory helpers ───────────────────────────────────────────────────────────

inline std::shared_ptr<CanvasWidget> Canvas()
{
    return std::make_shared<CanvasWidget>();
}
inline std::shared_ptr<CanvasWidget> Canvas(int w, int h)
{
    return std::make_shared<CanvasWidget>()->setSize(w, h);
}