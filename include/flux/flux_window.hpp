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
class Canvas2DGL;
#endif
#endif

// ============================================================================
// WindowCallbacks
// ============================================================================

struct WindowCallbacks
{
    std::function<void(GraphicsContext &ctx, int width, int height)> onPaint;
    std::function<void(GraphicsContext &ctx, int width, int height)> onResize;

    std::function<bool(int x, int y)> onMouseDown;
    std::function<bool(int x, int y)> onMouseUp;
    std::function<bool(int x, int y)> onMouseMove;
    std::function<bool(int x, int y)> onRightClick;
    std::function<bool(int delta)> onMouseWheel;
    std::function<void()> onMouseLeave;

    std::function<bool(int keyCode)> onKeyDown;
    std::function<bool(wchar_t ch)> onChar;

    std::function<void(TimerID timerId)> onTimer;
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
    void destroy();
    int run();

    WindowCallbacks callbacks;

    void invalidate();
    void invalidateRect(int x, int y, int w, int h);
    void setTimer(TimerID id, int ms);
    void killTimer(TimerID id);

    int clientWidth() const { return cachedWidth; }
    int clientHeight() const { return cachedHeight; }

    bool valid() const
    {
#ifdef _WIN32
        return hwnd != nullptr;
#elif defined(__linux__) && !defined(__ANDROID__)
        return nativeHandle != nullptr;
#elif defined(__ANDROID__)
        return eglState != nullptr;
#else
        return false;
#endif
    }

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

    void startupGdiplus();
    GraphicsContext getMeasureContext() const;

    // ── DPI helpers (Linux HiDPI: drawable pixels / logical pixels) ───────────
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

private:
    // ── Shared ───────────────────────────────────────────────────────────────
    AppInstance hInstance = nullptr;
    int cachedWidth = 0;
    int cachedHeight = 0;

    // =========================================================================
    // Win32
    // =========================================================================
#ifdef _WIN32
    NativeWindow hwnd = nullptr;
    BackBuffer backBuffer;
    ULONG_PTR gdiplusToken = 0;

    void shutdownGdiplus();
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

    // ── Canvas widget registry ────────────────────────────────────────────────
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

#ifdef __APPLE__
#if TARGET_OS_OSX
    struct MacState;
    MacState *macState = nullptr;
    friend class CanvasWidget;
    CanvasWidget *hitTestCanvas(int x, int y);

public:
    void registerCanvas_public(CanvasWidget *c);
    void unregisterCanvas_public(CanvasWidget *c);

private:
#endif
#endif
};

#endif // FLUX_WINDOW_HPP