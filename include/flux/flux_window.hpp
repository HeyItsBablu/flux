#ifndef FLUX_WINDOW_HPP
#define FLUX_WINDOW_HPP

#include "flux_font.hpp"
#include "flux_platform.hpp"

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#if defined(__linux__) && !defined(__ANDROID__)
class CanvasWidget;
class CairoCompositor;
class Canvas2DGL;
#include <glad/glad.h>
#include <SDL2/SDL.h>
#endif

#ifdef __APPLE__
#include <TargetConditionals.h>
#if TARGET_OS_OSX
class CanvasWidget;
struct Canvas2DGL;
#endif
#endif

// Win32: forward-declare our new GPU objects so the header stays lean.
// Full types are only needed in flux_window_win32.cpp.
#ifdef _WIN32
struct D3DDevice;
class RenderLoop;
#endif

// ============================================================================
// WindowCallbacks
// ============================================================================

struct WindowCallbacks
{
    // Paint: called on the render thread each frame with dt in seconds.
    // ctx is the D2D device context on Win32, cairo_t* on Linux, etc.
    std::function<void(GraphicsContext &ctx, int width, int height)> onPaint;

    // Resize: called on the render thread after the swap chain is resized.
    std::function<void(GraphicsContext &ctx, int width, int height)> onResize;

    // Input: all called on the message thread, return true to request redraw.
    std::function<bool(int x, int y)> onMouseDown;
    std::function<bool(int x, int y)> onMouseUp;
    std::function<bool(int x, int y)> onMouseMove;
    std::function<bool(int x, int y)> onRightClick;
    std::function<bool(int delta)> onMouseWheel;
    std::function<void()> onMouseLeave;

    std::function<bool(int keyCode)> onKeyDown;
    std::function<bool(wchar_t ch)> onChar;

    // Timer: on Win32 these are now fired on the render thread by RenderLoop.
    // On other platforms they still use the platform timer mechanism.
    std::function<void(TimerID id)> onTimer;

    std::function<void()> onNonClientMouseDown;
    std::function<void()> onFocusLost;
};

// ============================================================================
// PlatformWindow
// ============================================================================

class PlatformWindow
{
public:
    PlatformWindow() = default;
    ~PlatformWindow() { destroy(); }

    PlatformWindow(const PlatformWindow &) = delete;
    PlatformWindow &operator=(const PlatformWindow &) = delete;

    bool create(const std::string &title, int width, int height,
                AppInstance hInstance, void *userData);

    void startRenderLoop();
    void destroy();
    int run();
    void tick();

    WindowCallbacks callbacks;

    // ── Dirty signaling ───────────────────────────────────────────────────────
    // On Win32 these now signal the RenderLoop rather than calling
    // InvalidateRect. On other platforms the behaviour is unchanged.
    void invalidate();
    void invalidateRect(int x, int y, int w, int h);

    // ── Timers ────────────────────────────────────────────────────────────────
    // On Win32 these are routed through RenderLoop (sub-frame accuracy,
    // render-thread execution). On other platforms: SDL_AddTimer / SetTimer.
    void setTimer(TimerID id, int ms);
    void killTimer(TimerID id);

    int clientWidth() const { return cachedWidth; }
    int clientHeight() const { return cachedHeight; }

    bool valid() const;

    void setClipboardText(const std::string &text);
    std::string getClipboardText();

    void captureMouseInput();
    void releaseMouseInput();

    NativeWindow handle() const;
    bool isMouseCaptured() const;

    struct ScreenPoint
    {
        int x = 0, y = 0;
    };
    struct ClientSize
    {
        int width = 0, height = 0;
    };

    ScreenPoint clientToScreen(int cx, int cy) const;
    ScreenPoint screenToClient(int sx, int sy) const;
    ClientSize getClientSize() const;

    void setResizeCursorH();
    void setResizeCursorV();
    void setDefaultCursor();

    // ── Measure context ───────────────────────────────────────────────────────
    // Returns a borrowed view of the D2D device context (Win32) or the
    // platform equivalent. Used by LayoutEngine for text measurement.
    GraphicsContext getMeasureContext() const;

    // ── D2D context accessor (Win32 only) ─────────────────────────────────────
    // Exposes the live D2D device context so FluxUI::getMeasureContext()
    // can borrow it without going through PlatformWindow internals.
#ifdef _WIN32
    GraphicsContext getD2DContext() const;
    RenderLoop *getRenderLoop() const { return renderLoop_; }
    D3DDevice *getD3DDevice() const { return d3dDevice_; }
#endif

    // ── DPI helpers (Linux HiDPI) ─────────────────────────────────────────────
#if defined(__linux__) && !defined(__ANDROID__)
    float dpiScaleX() const
    {
        return (logicalWidth_ > 0)
                   ? float(cachedWidth) / float(logicalWidth_)
                   : 1.f;
    }
    float dpiScaleY() const
    {
        return (logicalHeight_ > 0)
                   ? float(cachedHeight) / float(logicalHeight_)
                   : 1.f;
    }
    int logicalWidth() const { return logicalWidth_; }
    int logicalHeight() const { return logicalHeight_; }
#endif

#ifdef __ANDROID__
    EGLDisplay getEGLDisplay() const;
    EGLSurface getEGLSurface() const;
    EGLContext getEGLContext() const;
    void handleAndroidEvent(const AInputEvent *event);
    void pollTimers();
    void reinitSurface(ANativeWindow *window);
    void destroySurface();
#endif

    void updateClientSize();

    // Shared (public so platform .mm files can access directly)
    int cachedWidth = 0;
    int cachedHeight = 0;

private:
    AppInstance hInstance_ = nullptr;

    // =========================================================================
    // Win32
    // =========================================================================
#ifdef _WIN32
    HWND hwnd_ = nullptr;
    D3DDevice *d3dDevice_ = nullptr;   // owns GPU pipeline
    RenderLoop *renderLoop_ = nullptr; // owns render thread + timers

    // Message-thread timer id → render-loop timer id mapping.
    // setTimer(id, ms) registers with renderLoop_ and stores the mapping
    // so killTimer(id) can find and remove the right render-loop entry.
    std::unordered_map<TimerID, uint32_t> timerMap_;

    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg,
                                       WPARAM wParam, LPARAM lParam);
#endif // _WIN32

    // =========================================================================
    // Linux
    // =========================================================================
#if defined(__linux__) && !defined(__ANDROID__)

    struct CairoState;

    SDL_Window *nativeHandle = nullptr;
    SDL_GLContext glContext_ = nullptr;
    Canvas2DGL *canvasGL_ = nullptr;
    CairoCompositor *cairoCompositor_ = nullptr;

    int logicalWidth_ = 0;
    int logicalHeight_ = 0;
    std::vector<uint8_t> cairoPixelBuf_;
    CanvasWidget *capturedCanvas_ = nullptr;
    CanvasWidget *focusedCanvas_ = nullptr;
    bool running = false;
    bool mouseCapture = false;
    bool dirty = false;
    CairoState *cairoState = nullptr;

    std::unordered_map<TimerID, SDL_TimerID> sdlTimerMap;

    std::vector<CanvasWidget *> canvasWidgets_;
    void registerCanvas(CanvasWidget *c);
    void unregisterCanvas(CanvasWidget *c);
    CanvasWidget *hitTestCanvas(int sx, int sy);

public:
    void registerCanvas_public(CanvasWidget *c) { registerCanvas(c); }
    void unregisterCanvas_public(CanvasWidget *c) { unregisterCanvas(c); }

private:
    void handleSDLEvent(const SDL_Event &e);

#endif // __linux__

    // =========================================================================
    // Android
    // =========================================================================
#ifdef __ANDROID__
    struct EGLState;
    struct TimerEntry
    {
        int intervalMs;
        uint32_t nextFireMs;
    };

    android_app *androidApp = nullptr;
    ANativeWindow *nativeHandle = nullptr;
    bool running = false;
    bool mouseCapture = false;
    bool dirty = false;
    EGLState *eglState = nullptr;

    std::unordered_map<TimerID, TimerEntry> androidTimers;
#endif // __ANDROID__

    // =========================================================================
    // Apple / macOS
    // =========================================================================
#ifdef __APPLE__
#if TARGET_OS_OSX
public:
    struct MacState;
    MacState *macState = nullptr;
    CanvasWidget *hitTestCanvas(int x, int y);
    MacState *getMacState() const { return macState; }
    void registerCanvas_public(CanvasWidget *c);
    void unregisterCanvas_public(CanvasWidget *c);
#endif
#endif

    // =========================================================================
    // Web (Emscripten)
    // =========================================================================
#ifdef __EMSCRIPTEN__
public:
    struct WebState;
    WebState *webState = nullptr;
#endif
};

#endif // FLUX_WINDOW_HPP