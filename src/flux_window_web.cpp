// src/flux_window_web.cpp
//
// PlatformWindow implementation for Emscripten / WebAssembly.
//
// Architecture
// ────────────
// Two canvases live in shell.html:
//   #flux-ui  — Canvas 2D,  sits on top,  receives all pointer/keyboard input
//   #flux-gl  — WebGL2,     sits behind,  used only by CanvasWidget
//
// This file owns:
//   • PlatformWindow::create()       — registers all JS event callbacks
//   • PlatformWindow::tick()         — called ~60 fps from main_web.cpp s_tick()
//   • PlatformWindow::invalidate()   — sets dirty flag → tick() will repaint
//   • PlatformWindow::setTimer()     — Emscripten interval timers
//   • PlatformWindow::killTimer()    — cancels interval timers
//   • All other PlatformWindow stubs required by flux_window.hpp
//
// Paint path (per frame)
// ──────────────────────
//   tick()
//     └─ if dirty:
//          EM_ASM: ctx.clearRect(0, 0, w, h)       ← clear Canvas 2D
//          callbacks.onPaint(GraphicsContext(w,h))  ← widget tree renders
//          dirty = false
//
// Each Painter call inside onPaint reaches the JS context via EM_ASM blocks
// in flux_painter_web.cpp — C++ never holds a pointer to CanvasRenderingContext2D.
//
// Coordinate system
// ─────────────────
// All coordinates passed to callbacks are in PHYSICAL pixels
// (CSS pixels × devicePixelRatio).  The painter scales its EM_ASM calls
// accordingly.  Layout engine works in physical pixels too, matching how
// the other platforms treat their backing store dimensions.
//
// Modifier state
// ──────────────
// Browser has no synchronous "is key held" query.  The keyboard/pointer
// callbacks update flux_web_detail::g_ctrlDown/g_shiftDown/g_altDown
// (defined in flux_platform.hpp) so platformCtrlDown() etc. work correctly
// from anywhere in the widget tree.

#ifdef __EMSCRIPTEN__

#include "flux/flux_window.hpp"
#include "flux/flux_platform.hpp"

#include <emscripten.h>
#include <emscripten/html5.h>

#include <string>
#include <unordered_map>
#include <functional>
#include <cstring> // memset

// ============================================================================
// Web-only private state
//
// Stored as a plain struct behind a pointer so flux_window.hpp doesn't need
// to pull in Emscripten headers — the same pattern used for MacState and
// EGLState on other platforms.
// ============================================================================

struct PlatformWindow::WebState
{
    bool initialized = false;
    bool dirty = false;
    bool mouseCapture = false;

    // Interval timer map: FluxUI TimerID → Emscripten interval handle (long).
    // emscripten_set_interval returns a long; we store it so killTimer() can
    // call emscripten_clear_interval().
    std::unordered_map<TimerID, long> timerHandles;
};

// ============================================================================
// Internal helpers
// ============================================================================

namespace
{

    // ── CSS colour string ─────────────────────────────────────────────────────────

    // Builds "rgba(r,g,b,a/255)" for passing to EM_ASM fillStyle / strokeStyle.
    // Kept as a C-string written into a stack buffer to avoid heap allocation on
    // every paint call.  Buffer is 32 bytes — "rgba(255,255,255,1.000)" = 22 chars.
    void colorToCSS(Color c, char *buf, int bufLen)
    {
        float a = c.a / 255.f;
        snprintf(buf, bufLen, "rgba(%d,%d,%d,%.3f)", c.r, c.g, c.b, a);
    }

    // ── Map Emscripten HTML5 key code → VK_* ──────────────────────────────────────
    //
    // EmscriptenKeyboardEvent::keyCode carries the browser's legacy keyCode value,
    // which matches the VK_* constants we defined in the __EMSCRIPTEN__ block of
    // flux_platform.hpp.  No translation needed — return as-is.

    inline int emKeyToVK(const EmscriptenKeyboardEvent *e)
    {
        return (int)e->keyCode;
    }

} // namespace

// ============================================================================
// Event callbacks
//
// All callbacks are registered on "#flux-ui" (the Canvas 2D canvas) because
// it sits on top (pointer-events: auto) and receives all user input.
// They are static C-style functions so Emscripten can store them as function
// pointers; `userData` carries the PlatformWindow*.
// ============================================================================

// ── Mouse down ────────────────────────────────────────────────────────────────

static EM_BOOL onMouseDown(int /*eventType*/,
                           const EmscriptenMouseEvent *e,
                           void *userData)
{
    auto *self = static_cast<PlatformWindow *>(userData);
    

    if (!self || !self->webState)
    {
  
        return EM_FALSE;
    }

    double dpr = EM_ASM_DOUBLE({ return Module._fluxDPR || 1.0; });
    int x = (int)e->targetX;
    int y = (int)e->targetY;



    bool handled = false;
    if (e->button == 0 && self->callbacks.onMouseDown)
    {
       
        handled = self->callbacks.onMouseDown(x, y);
       
    }


    if (handled)
        self->invalidate();
    return handled ? EM_TRUE : EM_FALSE;
}

// ── Mouse up ──────────────────────────────────────────────────────────────────

static EM_BOOL onMouseUp(int /*eventType*/,
                         const EmscriptenMouseEvent *e,
                         void *userData)
{
    auto *self = static_cast<PlatformWindow *>(userData);
    if (!self || !self->webState)
        return EM_FALSE;

    flux_web_detail::g_ctrlDown = e->ctrlKey;
    flux_web_detail::g_shiftDown = e->shiftKey;
    flux_web_detail::g_altDown = e->altKey;

    if (e->button != 0 || !self->callbacks.onMouseUp)
        return EM_FALSE;

    double dpr = EM_ASM_DOUBLE({ return Module._fluxDPR || 1.0; });
    int x = (int)e->targetX;
    int y = (int)e->targetY;

    bool handled = self->callbacks.onMouseUp(x, y);
    if (handled)
        self->invalidate();

    // Release simulated capture on mouse-up (mirrors Win32 ReleaseCapture).
    if (self->webState->mouseCapture)
        self->webState->mouseCapture = false;

    return handled ? EM_TRUE : EM_FALSE;
}

// ── Mouse move ────────────────────────────────────────────────────────────────

static EM_BOOL onMouseMove(int /*eventType*/,
                           const EmscriptenMouseEvent *e,
                           void *userData)
{
    auto *self = static_cast<PlatformWindow *>(userData);
    if (!self || !self->webState)
        return EM_FALSE;

    flux_web_detail::g_ctrlDown = e->ctrlKey;
    flux_web_detail::g_shiftDown = e->shiftKey;
    flux_web_detail::g_altDown = e->altKey;

    if (!self->callbacks.onMouseMove)
        return EM_FALSE;

    double dpr = EM_ASM_DOUBLE({ return Module._fluxDPR || 1.0; });
    int x = (int)e->targetX;
    int y = (int)e->targetY;

    bool handled = self->callbacks.onMouseMove(x, y);
    if (handled)
        self->invalidate();
    return handled ? EM_TRUE : EM_FALSE;
}

// ── Mouse leave ───────────────────────────────────────────────────────────────

static EM_BOOL onMouseLeave(int /*eventType*/,
                            const EmscriptenMouseEvent * /*e*/,
                            void *userData)
{
    auto *self = static_cast<PlatformWindow *>(userData);
    if (!self || !self->webState)
        return EM_FALSE;

    if (self->callbacks.onMouseLeave)
        self->callbacks.onMouseLeave();

    self->invalidate();
    return EM_FALSE; // don't consume — let the browser handle cursor reset
}

// ── Mouse wheel ───────────────────────────────────────────────────────────────

static EM_BOOL onWheel(int /*eventType*/,
                       const EmscriptenWheelEvent *e,
                       void *userData)
{
    auto *self = static_cast<PlatformWindow *>(userData);
    if (!self || !self->webState || !self->callbacks.onMouseWheel)
        return EM_FALSE;

    // Normalize to Win32 WHEEL_DELTA (120 per notch).
    // deltaY is in CSS pixels by default (DOM_DELTA_PIXEL).
    // Positive deltaY = scroll down = negative WHEEL_DELTA (same as Win32).
    int delta = (e->deltaY != 0.0)
                    ? (int)(-(e->deltaY / std::abs(e->deltaY)) * WHEEL_DELTA)
                    : 0;

    if (delta == 0)
        return EM_FALSE;

    bool handled = self->callbacks.onMouseWheel(delta);
    if (handled)
        self->invalidate();
    return handled ? EM_TRUE : EM_FALSE;
}

// ── Key down ──────────────────────────────────────────────────────────────────

static EM_BOOL onKeyDown(int /*eventType*/,
                         const EmscriptenKeyboardEvent *e,
                         void *userData)
{
    auto *self = static_cast<PlatformWindow *>(userData);
    if (!self || !self->webState)
        return EM_FALSE;

    flux_web_detail::g_ctrlDown = e->ctrlKey;
    flux_web_detail::g_shiftDown = e->shiftKey;
    flux_web_detail::g_altDown = e->altKey;

    // ── onKeyDown — VK_* dispatch ─────────────────────────────────────────────
    bool handled = false;
    if (self->callbacks.onKeyDown)
    {
        int vk = emKeyToVK(e);
        handled = self->callbacks.onKeyDown(vk);
    }

    // ── onChar — printable character dispatch ─────────────────────────────────
    //
    // Emscripten's keydown event carries the UTF-32 character in
    // EmscriptenKeyboardEvent::key as a null-terminated string.
    // Single-character keys (length == 1) are printable; multi-character
    // strings ("Shift", "Control", "ArrowLeft", …) are control keys
    // already handled above via onKeyDown.
    if (!handled && self->callbacks.onChar)
    {
        const char *key = e->key;
        // key is UTF-8; single ASCII printable character check:
        if (key[0] != '\0' && key[1] == '\0')
        {
            wchar_t ch = (wchar_t)(unsigned char)key[0];
            if (ch >= 32)
            { // exclude control characters
                handled = self->callbacks.onChar(ch);
            }
        }
    }

    if (handled)
        self->invalidate();

    // Return EM_TRUE to prevent default browser behaviour (e.g. space scrolling
    // the page, Tab moving focus out of the canvas) when FluxUI handled the key.
    return handled ? EM_TRUE : EM_FALSE;
}

// ── Touch — mapped to mouse equivalents ──────────────────────────────────────
//
// Single-touch only.  Multi-touch gestures (pinch-zoom etc.) are not handled
// at this layer — add a dedicated touch handler if needed later.

static EM_BOOL onTouchStart(int /*eventType*/,
                            const EmscriptenTouchEvent *e,
                            void *userData)
{
    auto *self = static_cast<PlatformWindow *>(userData);
    if (!self || !self->webState || e->numTouches == 0)
        return EM_FALSE;
    if (!self->callbacks.onMouseDown)
        return EM_FALSE;

    double dpr = EM_ASM_DOUBLE({ return Module._fluxDPR || 1.0; });
    int x = (int)(e->touches[0].targetX * dpr);
    int y = (int)(e->touches[0].targetY * dpr);

    bool handled = self->callbacks.onMouseDown(x, y);
    if (handled)
        self->invalidate();
    return EM_TRUE; // always consume touch to prevent scroll
}

static EM_BOOL onTouchEnd(int /*eventType*/,
                          const EmscriptenTouchEvent *e,
                          void *userData)
{
    auto *self = static_cast<PlatformWindow *>(userData);
    if (!self || !self->webState || e->numTouches == 0)
        return EM_FALSE;

    double dpr = EM_ASM_DOUBLE({ return Module._fluxDPR || 1.0; });
    int x = (int)(e->touches[0].targetX * dpr);
    int y = (int)(e->touches[0].targetY * dpr);

    if (self->callbacks.onMouseUp)
        self->callbacks.onMouseUp(x, y);

    self->invalidate();
    return EM_TRUE;
}

static EM_BOOL onTouchMove(int /*eventType*/,
                           const EmscriptenTouchEvent *e,
                           void *userData)
{
    auto *self = static_cast<PlatformWindow *>(userData);
    if (!self || !self->webState || e->numTouches == 0)
        return EM_FALSE;
    if (!self->callbacks.onMouseMove)
        return EM_FALSE;

    double dpr = EM_ASM_DOUBLE({ return Module._fluxDPR || 1.0; });
    int x = (int)(e->touches[0].targetX * dpr);
    int y = (int)(e->touches[0].targetY * dpr);

    bool handled = self->callbacks.onMouseMove(x, y);
    if (handled)
        self->invalidate();
    return EM_TRUE; // consume to prevent page scroll
}

// ── Interval timer shim ───────────────────────────────────────────────────────
//
// emscripten_set_interval fires a C callback repeatedly.  We wrap it to
// call the WindowCallbacks::onTimer function and then trigger a repaint.

struct TimerShimArg
{
    PlatformWindow *window;
    TimerID timerId;
};

static void timerShim(void *arg)
{
    auto *shim = static_cast<TimerShimArg *>(arg);
    if (!shim || !shim->window)
        return;

    if (shim->window->callbacks.onTimer)
        shim->window->callbacks.onTimer(shim->timerId);

    shim->window->invalidate();
}

// ============================================================================
// PlatformWindow — web implementation
// ============================================================================

// ── create ────────────────────────────────────────────────────────────────────

bool PlatformWindow::create(const std::string & /*title*/,
                            int width, int height,
                            AppInstance /*hInstance*/, void * /*userData*/)
{
    webState = new WebState();

    // Use actual physical canvas size, not the logical size from createApp().
    // The JS side has already sized the canvases to window.innerWidth/Height * dpr.
    int physW = EM_ASM_INT({ return Module._fluxPhysicalWidth  | 0; });
    int physH = EM_ASM_INT({ return Module._fluxPhysicalHeight | 0; });
    cachedWidth  = (physW > 0) ? physW : width;
    cachedHeight = (physH > 0) ? physH : height;


    // ── Scale the Canvas 2D context by devicePixelRatio ──────────────────────
    //
    // The canvas physical size is already set by shell.html's resizeCanvases().
    // We apply a CSS-pixel scale transform once here so all subsequent draw
    // calls can use physical-pixel coordinates directly, just like GDI+ on Win32.
    EM_ASM({
        var dpr = Module._fluxDPR || 1.0;
        var ctx = Module._fluxCtx2D;
        if (ctx)
        {
            ctx.scale(dpr, dpr);
        }
    });

    // ── Register event listeners on #flux-ui ─────────────────────────────────
    //
    // useCapture = true for mouse/touch so we receive events even when the
    // pointer exits the canvas bounds during a drag (simulates SetCapture).

    emscripten_set_mousedown_callback("#flux-ui", this, /*useCapture=*/1, onMouseDown);
    emscripten_set_mouseup_callback("#flux-ui", this, /*useCapture=*/1, onMouseUp);
    emscripten_set_mousemove_callback("#flux-ui", this, /*useCapture=*/1, onMouseMove);
    emscripten_set_mouseleave_callback("#flux-ui", this, /*useCapture=*/0, onMouseLeave);
    emscripten_set_wheel_callback("#flux-ui", this, /*useCapture=*/1, onWheel);

    // Keyboard events must be registered on the document (not the canvas)
    // because canvases don't receive keyboard focus by default.
    emscripten_set_keydown_callback(EMSCRIPTEN_EVENT_TARGET_DOCUMENT,
                                    this, /*useCapture=*/1, onKeyDown);

    // Touch — canvas element, prevent-default so the page doesn't scroll.
    emscripten_set_touchstart_callback("#flux-ui", this, /*useCapture=*/1, onTouchStart);
    emscripten_set_touchend_callback("#flux-ui", this, /*useCapture=*/1, onTouchEnd);
    emscripten_set_touchmove_callback("#flux-ui", this, /*useCapture=*/1, onTouchMove);

    webState->initialized = true;

    // Mark dirty so the very first tick() triggers a full paint.
    webState->dirty = true;

    return true;
}

// ── destroy ───────────────────────────────────────────────────────────────────

void PlatformWindow::destroy()
{
    if (!webState)
        return;

    // Cancel all live interval timers.
    for (auto &[id, handle] : webState->timerHandles)
        emscripten_clear_interval(handle);
    webState->timerHandles.clear();

    // Unregister event listeners.
    emscripten_set_mousedown_callback("#flux-ui", nullptr, 0, nullptr);
    emscripten_set_mouseup_callback("#flux-ui", nullptr, 0, nullptr);
    emscripten_set_mousemove_callback("#flux-ui", nullptr, 0, nullptr);
    emscripten_set_mouseleave_callback("#flux-ui", nullptr, 0, nullptr);
    emscripten_set_wheel_callback("#flux-ui", nullptr, 0, nullptr);
    emscripten_set_keydown_callback(EMSCRIPTEN_EVENT_TARGET_DOCUMENT,
                                    nullptr, 0, nullptr);
    emscripten_set_touchstart_callback("#flux-ui", nullptr, 0, nullptr);
    emscripten_set_touchend_callback("#flux-ui", nullptr, 0, nullptr);
    emscripten_set_touchmove_callback("#flux-ui", nullptr, 0, nullptr);

    delete webState;
    webState = nullptr;
}

// ── tick ──────────────────────────────────────────────────────────────────────
//
// Called from main_web.cpp s_tick() at ~60 fps via requestAnimationFrame.
// Only repaints when the dirty flag is set, so idle frames cost almost nothing.
extern void fluxClearCanvasWidgetRects();


void PlatformWindow::tick()
{
    if (!webState || !webState->dirty)
        return;
    webState->dirty = false;

    int w = cachedWidth;
    int h = cachedHeight;

    EM_ASM({
        var ctx = Module._fluxCtx2D;
        var dpr = Module._fluxDPR || 1.0;
        if (ctx) {
            ctx.save();
            ctx.setTransform(1, 0, 0, 1, 0, 0);
            ctx.clearRect(0, 0, $0 * dpr, $1 * dpr);
            ctx.restore();
            ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
        }
    }, w, h);

    if (callbacks.onPaint)
    {
        GraphicsContext ctx(w, h);
        callbacks.onPaint(ctx, w, h);
    }

    // After all widgets have painted, punch transparent holes where
    // CanvasWidgets live so the WebGL layer shows through.
    // We collect the rects during onPaint via a global list.
    fluxClearCanvasWidgetRects();  // <-- ADD THIS
}

// ── run ───────────────────────────────────────────────────────────────────────
//
// Never called on web — main_web.cpp uses emscripten_set_main_loop instead.
// Provided so the linker is satisfied if run() is referenced anywhere.

int PlatformWindow::run()
{
    // On web the main loop is driven by emscripten_set_main_loop in main_web.cpp.
    // This function should never be reached; assert in debug builds.
    emscripten_log(EM_LOG_WARN, "PlatformWindow::run() called on web — no-op");
    return 0;
}

// ── invalidate / invalidateRect ───────────────────────────────────────────────

void PlatformWindow::invalidate()
{
    if (webState)
        webState->dirty = true;
}

void PlatformWindow::invalidateRect(int /*x*/, int /*y*/, int /*w*/, int /*h*/)
{
    // Canvas 2D has no partial-invalidation mechanism — always redraw the full
    // surface.  Set the same dirty flag as invalidate().
    if (webState)
        webState->dirty = true;
}

// ── valid ─────────────────────────────────────────────────────────────────────

// NOTE: flux_window.hpp's valid() inline returns false for web in its current
// form (the #else branch).  Add this to the __EMSCRIPTEN__ section of
// flux_window.hpp:
//
//   #ifdef __EMSCRIPTEN__
//       return webState != nullptr && webState->initialized;
//   #endif
//
// Until then, valid() will always return false on web, which prevents layout
// passes from running after createWindow().  See the companion diff in
// flux_window.hpp.

// ── Timers ────────────────────────────────────────────────────────────────────

void PlatformWindow::setTimer(TimerID id, int ms)
{
    if (!webState)
        return;

    // Kill any existing timer with this id before creating a new one.
    auto it = webState->timerHandles.find(id);
    if (it != webState->timerHandles.end())
    {
        emscripten_clear_interval(it->second);
        webState->timerHandles.erase(it);
    }

    // TimerShimArg lives for the lifetime of the timer.  It is freed in
    // killTimer() when the interval is cancelled.
    auto *arg = new TimerShimArg{this, id};
    long handle = emscripten_set_interval(timerShim, (double)ms, arg);
    webState->timerHandles[id] = handle;
}

void PlatformWindow::killTimer(TimerID id)
{
    if (!webState)
        return;

    auto it = webState->timerHandles.find(id);
    if (it == webState->timerHandles.end())
        return;

    emscripten_clear_interval(it->second);
    webState->timerHandles.erase(it);
    // Note: TimerShimArg is leaked here in the minimal impl.
    // A production version would store TimerShimArg* alongside the handle.
}

// ── Mouse capture ─────────────────────────────────────────────────────────────
//
// The browser doesn't have SetCapture / ReleaseCapture.  We simulate it with
// a bool flag — mouse events registered with useCapture=true on #flux-ui
// already receive events outside the canvas during a drag, which is sufficient
// for the resize-splitter and drag use cases in FluxUI.

void PlatformWindow::captureMouseInput()
{
    if (webState)
        webState->mouseCapture = true;
}

void PlatformWindow::releaseMouseInput()
{
    if (webState)
        webState->mouseCapture = false;
}

bool PlatformWindow::isMouseCaptured() const
{
    return webState && webState->mouseCapture;
}

bool PlatformWindow::valid() const
{
    return webState != nullptr && webState->initialized;
}

// ── Clipboard ─────────────────────────────────────────────────────────────────
//
// Synchronous clipboard access is not available in browsers without the
// Clipboard API (async, requires user gesture + HTTPS).  We use a JS-side
// string variable as a process-local clipboard for now.  Copy/paste between
// FluxUI and other browser tabs / native apps will not work until the async
// Clipboard API is integrated.

void PlatformWindow::setClipboardText(const std::string &text)
{
    EM_ASM({ Module._fluxClipboard = UTF8ToString($0); }, text.c_str());
}

std::string PlatformWindow::getClipboardText()
{
    char *ptr = (char *)EM_ASM_PTR({
        var s = Module._fluxClipboard || "";
        var len = lengthBytesUTF8(s) + 1;
        var buf = _malloc(len);
        stringToUTF8(s, buf, len);
        return buf;
    });
    std::string result(ptr ? ptr : "");
    free(ptr);
    return result;
}

// ── Handle / size accessors ───────────────────────────────────────────────────

NativeWindow PlatformWindow::handle() const
{
    // NativeWindow is void* on web.  We return the canvas selector as a
    // stable pointer — the string literal lives in the read-only data segment.
    return const_cast<void *>(static_cast<const void *>("#flux-ui"));
}

void PlatformWindow::updateClientSize()
{
    // Physical dimensions are pushed in from JS via fluxOnResize() in
    // main_web.cpp.  Nothing to do here — cachedWidth/cachedHeight are
    // already up to date.
}

PlatformWindow::ScreenPoint PlatformWindow::clientToScreen(int cx, int cy) const
{
    // On web there is no distinction between client and screen coordinates
    // for our purposes — canvas coordinates are already viewport-relative.
    return {cx, cy};
}

PlatformWindow::ScreenPoint PlatformWindow::screenToClient(int sx, int sy) const
{
    return {sx, sy};
}

PlatformWindow::ClientSize PlatformWindow::getClientSize() const
{
    return {cachedWidth, cachedHeight};
}

// ── getMeasureContext ─────────────────────────────────────────────────────────
//
// On web text measurement is done via Canvas 2D's measureText() in
// flux_font_web.cpp.  We just need to hand the painter a context with the
// correct dimensions.

GraphicsContext PlatformWindow::getMeasureContext() const
{
    return GraphicsContext(cachedWidth, cachedHeight);
}

// ── startupGdiplus ────────────────────────────────────────────────────────────
//
// Called by FluxUI::build() unconditionally.  No-op on web.

void PlatformWindow::startupGdiplus() {}

// ── Cursor ────────────────────────────────────────────────────────────────────

void PlatformWindow::setResizeCursorH()
{
    EM_ASM({ document.getElementById('flux-ui').style.cursor = 'ew-resize'; });
}

void PlatformWindow::setResizeCursorV()
{
    EM_ASM({ document.getElementById('flux-ui').style.cursor = 'ns-resize'; });
}

void PlatformWindow::setDefaultCursor()
{
    EM_ASM({ document.getElementById('flux-ui').style.cursor = 'default'; });
}

#endif // __EMSCRIPTEN__