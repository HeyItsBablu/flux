#ifndef FLUX_WINDOW_HPP
#define FLUX_WINDOW_HPP

#include "flux_font.hpp"
#include "flux_platform.hpp"

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#if defined(__linux__) && !defined(__ANDROID__)

#include <SDL2/SDL.h>
#endif

#ifdef __APPLE__
#include <TargetConditionals.h>

#endif

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
    void invalidate();
    void invalidateRect(int x, int y, int w, int h);

    // ── Timers ────────────────────────────────────────────────────────────────
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
    GraphicsContext getMeasureContext() const;

#ifdef _WIN32
    GraphicsContext getD2DContext() const;
    RenderLoop *getRenderLoop() const { return renderLoop_; }
    D3DDevice *getD3DDevice() const { return d3dDevice_; }
#endif

    // ── DPI helpers (Linux HiDPI) ─────────────────────────────────────────────
#if defined(__linux__) && !defined(__ANDROID__)
    float dpiScaleX() const { return 1.f; }
    float dpiScaleY() const { return 1.f; }
    int logicalWidth() const { return logicalWidth_; }
    int logicalHeight() const { return logicalHeight_; }
    void _blitCairoToScreen();
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
    D3DDevice *d3dDevice_ = nullptr;
    RenderLoop *renderLoop_ = nullptr;

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

    int logicalWidth_ = 0;
    int logicalHeight_ = 0;
    std::vector<uint8_t> cairoPixelBuf_;
    bool running = false;
    bool mouseCapture = false;
    bool dirty = false;
    CairoState *cairoState = nullptr;

    std::unordered_map<TimerID, SDL_TimerID> sdlTimerMap;

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
    struct MacState;              // opaque — full definition lives only in flux_window_macos.mm
    MacState *macState = nullptr;

    // Narrow accessors so other .mm files never need MacState's full
    // definition. Caller does the (__bridge id<...>) cast.
    void *getMacMetalDevicePtr() const;
    void *getMacNSViewPtr() const;
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