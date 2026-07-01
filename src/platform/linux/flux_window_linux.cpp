// flux_window_linux.cpp
#if defined(__linux__) && !defined(__ANDROID__)

#include "flux/flux_http_platform.hpp"
#include "flux/flux_window.hpp"
#include "flux/widgets/flux_file_picker.hpp"
#include "flux/flux_core.hpp"

#include <SDL2/SDL.h>
#include <cairo/cairo.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>

// ============================================================================
// DEBUG MACRO
// ============================================================================
#define FLUX_DBG(fmt, ...)                          \
    do                                              \
    {                                               \
        fprintf(stderr, "[FLUX %s:%d] " fmt "\n",   \
                __func__, __LINE__, ##__VA_ARGS__); \
        fflush(stderr);                             \
    } while (0)

// ============================================================================
// FluxWin_markNeedsPaint — callable from any thread
// ============================================================================
// Generic repaint-request event type — owned by PlatformWindow, not by any
// particular widget. Any code (CanvasWidget included) that wants to request
// a repaint posts this event; PlatformWindow doesn't need to know who.
static Uint32 gFluxRepaintEventType = 0;

void FluxWin_markNeedsPaint()
{
    SDL_Event e;
    SDL_zero(e);
    Uint32 t = (gFluxRepaintEventType != 0) ? gFluxRepaintEventType : SDL_USEREVENT;
    e.type = t;
    e.user.code = -1;
    SDL_PushEvent(&e);
}

// ============================================================================
// CairoState — CPU-side ARGB32 surface for UI widget rendering
// ============================================================================
static Uint32 gFluxTimerEventType = 0;

struct PlatformWindow::CairoState
{
    std::vector<uint8_t> pixels;
    int width = 0;
    int height = 0;
    cairo_surface_t *cairoSurf = nullptr;
    cairo_t *cr = nullptr;

    bool rebuild(int w, int h)
    {
        teardown();
        if (w <= 0 || h <= 0)
            return false;
        width = w;
        height = h;
        pixels.assign(static_cast<size_t>(w) * h * 4, 0);
        cairoSurf = cairo_image_surface_create_for_data(
            pixels.data(), CAIRO_FORMAT_ARGB32, w, h, w * 4);
        if (cairo_surface_status(cairoSurf) != CAIRO_STATUS_SUCCESS)
        {
            teardown();
            return false;
        }
        cr = cairo_create(cairoSurf);
        if (cairo_status(cr) != CAIRO_STATUS_SUCCESS)
        {
            teardown();
            return false;
        }
        return true;
    }

    void teardown()
    {
        if (cr)
        {
            cairo_destroy(cr);
            cr = nullptr;
        }
        if (cairoSurf)
        {
            cairo_surface_destroy(cairoSurf);
            cairoSurf = nullptr;
        }
        pixels.clear();
        width = height = 0;
    }

    ~CairoState() { teardown(); }
};

// ============================================================================
// SDL timer callback
// ============================================================================
static Uint32 sdlTimerCallback(Uint32 interval, void *param)
{
    SDL_Event e;
    SDL_zero(e);
    e.type = gFluxTimerEventType;
    e.user.code = static_cast<Sint32>(reinterpret_cast<uintptr_t>(param));
    SDL_PushEvent(&e);
    return interval;
}

void PlatformWindow::startRenderLoop() {}

// ============================================================================
// PlatformWindow::create
// ============================================================================
bool PlatformWindow::create(const std::string &title, int width, int height,
                            AppInstance /*instance*/, void * /*userData*/)
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0)
    {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }

    gFluxTimerEventType = SDL_RegisterEvents(1);
    gFluxRepaintEventType = SDL_RegisterEvents(1);
    if (gFluxRepaintEventType == static_cast<Uint32>(-1))
        gFluxRepaintEventType = SDL_USEREVENT;

    fluxInitUIThread();

    nativeHandle = SDL_CreateWindow(title.c_str(),
                                    SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                    width, height,
                                    SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);

    if (!nativeHandle)
    {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return false;
    }

    SDL_GL_GetDrawableSize(nativeHandle, &cachedWidth, &cachedHeight);
    SDL_GetWindowSize(nativeHandle, &logicalWidth_, &logicalHeight_);

    fprintf(stderr, "PlatformWindow: logical=%dx%d  physical=%dx%d  scale=%.2fx%.2f\n",
            logicalWidth_, logicalHeight_,
            cachedWidth, cachedHeight,
            dpiScaleX(), dpiScaleY());

    // No Canvas2DBackend creation here — each CanvasWidget lazily creates
    // and owns its own backend (and registers its own fonts) the first
    // time it renders, mirroring flux_canvas_win32.cpp's ensureD2D(). This
    // window has no canvas-rendering resource of any kind.

    cairoState = new CairoState();
    if (!cairoState->rebuild(logicalWidth_, logicalHeight_))
        fprintf(stderr, "PlatformWindow: CairoState::rebuild failed\n");

    running = true;
    return true;
}

// ============================================================================
// PlatformWindow::destroy
// ============================================================================
void PlatformWindow::destroy()
{
    for (auto &[id, sdlId] : sdlTimerMap)
        SDL_RemoveTimer(sdlId);
    sdlTimerMap.clear();

    fluxShutdownHttpQueue();

    delete cairoState;
    cairoState = nullptr;
    if (nativeHandle)
    {
        SDL_DestroyWindow(nativeHandle);
        nativeHandle = nullptr;
    }
    SDL_Quit();
}

NativeWindow PlatformWindow::handle() const
{
    return nativeHandle;
}
bool PlatformWindow::isMouseCaptured() const
{
    return mouseCapture;
}

// ============================================================================
// PlatformWindow::run
//
// Single-pass paint: callbacks.onPaint walks the entire widget tree once.
// Widgets that need per-frame ticking (CanvasWidget included) do so inline
// inside their own render() override — there is no separate widget-type-
// specific pass driven from here.
// ============================================================================
int PlatformWindow::run()
{
    SDL_Event e;

    while (running)
    {
        bool hasEvent = (SDL_WaitEventTimeout(&e, 16) != 0);
        if (hasEvent)
            handleSDLEvent(e);
        while (SDL_PollEvent(&e))
            handleSDLEvent(e);

        if (auto *ui = FluxUI::getCurrentInstance())
            ui->drainPendingRebuilds();

        fluxProcessHttpEvents();

        if (!dirty)
            continue;
        dirty = false;

        if (cairoState && cairoState->cr)
        {
            cairo_save(cairoState->cr);
            cairo_set_operator(cairoState->cr, CAIRO_OPERATOR_CLEAR);
            cairo_paint(cairoState->cr);
            cairo_restore(cairoState->cr);

            GraphicsContext ctx(cairoState->cr, logicalWidth_, logicalHeight_);

            if (callbacks.onPaint)
                callbacks.onPaint(ctx, logicalWidth_, logicalHeight_);

            cairo_surface_flush(cairoState->cairoSurf);
        }

        _blitCairoToScreen();
    }

    return 0;
}

void PlatformWindow::_blitCairoToScreen()
{
    if (!cairoState || !nativeHandle)
        return;

    SDL_Surface *sdlSurf = SDL_CreateRGBSurfaceFrom(
        cairoState->pixels.data(),
        cairoState->width,
        cairoState->height,
        32,
        cairoState->width * 4,
        0x00FF0000u,
        0x0000FF00u,
        0x000000FFu,
        0xFF000000u);

    if (!sdlSurf)
        return;

    SDL_Surface *screen = SDL_GetWindowSurface(nativeHandle);
    if (screen)
    {
        SDL_BlitScaled(sdlSurf, nullptr, screen, nullptr);
        SDL_UpdateWindowSurface(nativeHandle);
    }
    SDL_FreeSurface(sdlSurf);
}

// ============================================================================
// handleSDLEvent
//
// Every input event flows through the generic WindowCallbacks. There is no
// canvas-specific hit-testing or dispatch here — that responsibility lives
// entirely in the widget tree (FluxUI::wireCallbacks / findAndHandleMouseEvent
// / broadcastMouseEvent), exactly as it does for every other widget type.
// ============================================================================
void PlatformWindow::handleSDLEvent(const SDL_Event &e)
{
    if (e.type == gFluxHttpEventType)
        return;

    if (e.type == gFluxRepaintEventType)
    {
        dirty = true;
        return;
    }

    switch (e.type)
    {

    case SDL_QUIT:
        running = false;
        break;

    case SDL_MOUSEBUTTONDOWN:
    {
        if (e.button.button == SDL_BUTTON_LEFT && callbacks.onMouseDown)
            if (callbacks.onMouseDown(e.button.x, e.button.y))
                dirty = true;
        if (e.button.button == SDL_BUTTON_RIGHT && callbacks.onRightClick)
            if (callbacks.onRightClick(e.button.x, e.button.y))
                dirty = true;
        break;
    }

    case SDL_MOUSEBUTTONUP:
    {
        if (e.button.button == SDL_BUTTON_LEFT && callbacks.onMouseUp)
            if (callbacks.onMouseUp(e.button.x, e.button.y))
                dirty = true;
        break;
    }

    case SDL_MOUSEMOTION:
    {
        if (callbacks.onMouseMove)
            if (callbacks.onMouseMove(e.motion.x, e.motion.y))
                dirty = true;
        break;
    }

    case SDL_MOUSEWHEEL:
    {
        // No positional hit-test here. Wheel routes to whatever currently
        // holds focus (FluxUI::focusedWidget), same model as Win32's
        // WM_MOUSEWHEEL handler.
        int delta = e.wheel.y * WHEEL_DELTA;
        if (callbacks.onMouseWheel && callbacks.onMouseWheel(delta))
            dirty = true;
        break;
    }

    case SDL_KEYDOWN:
        if (callbacks.onKeyDown && callbacks.onKeyDown(e.key.keysym.sym))
            dirty = true;
        break;

    case SDL_KEYUP:
        // Key-up is not currently surfaced through WindowCallbacks; widgets
        // needing it (e.g. CanvasWidget releasing a modifier) can track
        // modifier state from subsequent KEYDOWN/mouse events instead.
        break;

    case SDL_TEXTINPUT:
        if (callbacks.onChar && e.text.text[0] != '\0')
        {
            unsigned char b0 = static_cast<unsigned char>(e.text.text[0]);
            wchar_t ch = 0;
            if (b0 < 0x80)
                ch = static_cast<wchar_t>(b0);
            else if ((b0 & 0xE0) == 0xC0 && e.text.text[1])
                ch = static_cast<wchar_t>(
                    ((b0 & 0x1F) << 6) |
                    (static_cast<unsigned char>(e.text.text[1]) & 0x3F));
            else if ((b0 & 0xF0) == 0xE0 && e.text.text[1] && e.text.text[2])
                ch = static_cast<wchar_t>(
                    ((b0 & 0x0F) << 12) |
                    ((static_cast<unsigned char>(e.text.text[1]) & 0x3F) << 6) |
                    (static_cast<unsigned char>(e.text.text[2]) & 0x3F));
            if (ch && callbacks.onChar(ch))
                dirty = true;
        }
        break;

    case SDL_WINDOWEVENT:
        switch (e.window.event)
        {

        case SDL_WINDOWEVENT_RESIZED:
        case SDL_WINDOWEVENT_SIZE_CHANGED:
        {
            SDL_GetWindowSize(nativeHandle, &logicalWidth_, &logicalHeight_);

            fprintf(stderr, "PlatformWindow resize: logical=%dx%d  physical=%dx%d\n",
                    logicalWidth_, logicalHeight_, cachedWidth, cachedHeight);

            cachedWidth = logicalWidth_;
            cachedHeight = logicalHeight_;

            if (cairoState)
                cairoState->rebuild(logicalWidth_, logicalHeight_);

            if (callbacks.onResize && cairoState && cairoState->cr)
            {
                GraphicsContext ctx(cairoState->cr, logicalWidth_, logicalHeight_);
                callbacks.onResize(ctx, logicalWidth_, logicalHeight_);
            }

            // No canvas-widget resize notification loop here — any widget
            // that cares about size changes detects it itself inside
            // computeLayout()/render() by comparing against its own
            // last-known width/height, the same way every other widget
            // already reacts to layout changes.

            dirty = true;
            break;
        }

        case SDL_WINDOWEVENT_EXPOSED:
            dirty = true;
            break;

        case SDL_WINDOWEVENT_LEAVE:
            if (callbacks.onMouseLeave)
                callbacks.onMouseLeave();
            dirty = true;
            break;

        case SDL_WINDOWEVENT_FOCUS_LOST:
            if (callbacks.onFocusLost)
                callbacks.onFocusLost();
            dirty = true;
            break;

        case SDL_WINDOWEVENT_FOCUS_GAINED:
            dirty = true;
            break;
        }
        break;

    default:
        if (e.type == gFluxTimerEventType)
        {
            if (e.user.code == kFluxFilePickerEvent)
            {
                fluxFilePickerDispatchSDLEvent(e);
                dirty = true;
            }
            else
            {
                if (callbacks.onTimer)
                    callbacks.onTimer(static_cast<TimerID>(e.user.code));
                dirty = true;
            }
        }
        break;
    }
}

// ============================================================================
// invalidate / invalidateRect
// ============================================================================
void PlatformWindow::invalidate()
{
    dirty = true;
    SDL_Event e;
    SDL_zero(e);
    e.type = (gFluxRepaintEventType != 0) ? gFluxRepaintEventType : static_cast<Uint32>(SDL_USEREVENT);
    e.user.code = -1;
    SDL_PushEvent(&e);
}

void PlatformWindow::invalidateRect(int, int, int, int)
{
    if (!dirty)
    {
        dirty = true;
        invalidate();
    }
}

// ============================================================================
// setTimer / killTimer
// ============================================================================
void PlatformWindow::setTimer(TimerID id, int ms)
{
    killTimer(id);
    SDL_TimerID sdlId = SDL_AddTimer(
        static_cast<Uint32>(ms), sdlTimerCallback,
        reinterpret_cast<void *>(static_cast<uintptr_t>(id)));
    if (sdlId != 0)
        sdlTimerMap[id] = sdlId;
}
void PlatformWindow::killTimer(TimerID id)
{
    auto it = sdlTimerMap.find(id);
    if (it != sdlTimerMap.end())
    {
        SDL_RemoveTimer(it->second);
        sdlTimerMap.erase(it);
    }
}

bool PlatformWindow::valid() const
{
    return nativeHandle != nullptr;
}

// ============================================================================
// Clipboard / input / coords / cursors
// ============================================================================
void PlatformWindow::setClipboardText(const std::string &text)
{
    SDL_SetClipboardText(text.c_str());
}
std::string PlatformWindow::getClipboardText()
{
    char *raw = SDL_GetClipboardText();
    std::string s = raw ? raw : "";
    SDL_free(raw);
    return s;
}
void PlatformWindow::captureMouseInput()
{
    SDL_CaptureMouse(SDL_TRUE);
    mouseCapture = true;
}
void PlatformWindow::releaseMouseInput()
{
    SDL_CaptureMouse(SDL_FALSE);
    mouseCapture = false;
}
PlatformWindow::ScreenPoint PlatformWindow::clientToScreen(int cx, int cy) const
{
    int wx = 0, wy = 0;
    if (nativeHandle)
        SDL_GetWindowPosition(nativeHandle, &wx, &wy);
    return {wx + cx, wy + cy};
}
PlatformWindow::ScreenPoint PlatformWindow::screenToClient(int sx, int sy) const
{
    int wx = 0, wy = 0;
    if (nativeHandle)
        SDL_GetWindowPosition(nativeHandle, &wx, &wy);
    return {sx - wx, sy - wy};
}
PlatformWindow::ClientSize PlatformWindow::getClientSize() const
{
    return {logicalWidth_, logicalHeight_};
}
void PlatformWindow::setResizeCursorH()
{
    static SDL_Cursor *c = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZEWE);
    if (c)
        SDL_SetCursor(c);
}
void PlatformWindow::setResizeCursorV()
{
    static SDL_Cursor *c = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZENS);
    if (c)
        SDL_SetCursor(c);
}
void PlatformWindow::setDefaultCursor()
{
    static SDL_Cursor *c = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_ARROW);
    if (c)
        SDL_SetCursor(c);
}

GraphicsContext PlatformWindow::getMeasureContext() const
{
    if (cairoState && cairoState->cr)
        return GraphicsContext(cairoState->cr, logicalWidth_, logicalHeight_);
    return GraphicsContext();
}
void PlatformWindow::updateClientSize()
{
    if (nativeHandle)
    {
        SDL_GetWindowSize(nativeHandle, &logicalWidth_, &logicalHeight_);
        cachedWidth = logicalWidth_;
        cachedHeight = logicalHeight_;
    }
}

#endif // __linux__