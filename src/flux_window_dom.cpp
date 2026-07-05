// src/flux_window_dom.cpp
//
// PlatformWindow implementation for the DOM renderer (FLUX_WEB_RENDERER=dom).
// Compiled INSTEAD OF flux_window_web.cpp — see the CMake FLUX_WEB_RENDERER
// switch (Phase 1's build-wiring step, added alongside this file).
//
// What's different from flux_window_web.cpp
// ──────────────────────────────────────────
// Painting: instead of clearing/redrawing a <canvas> every dirty tick,
// tick() just re-runs the same widget tree paint pass — flux_painter_dom.cpp
// updates existing DOM nodes' style/text in place (get-or-create by owner,
// see ensureNode() there), so there's no "clear" step needed at all; a node
// simply gets its properties overwritten to the current frame's values.
//
// Input: UNCHANGED IN SPIRIT. Real user interaction still happens on ONE
// full-surface capture element on top (#flux-input-capture, a plain <div>,
// not a canvas), with pointer-events:auto, receiving every mouse/touch/key
// event and forwarding coordinates into the exact same WindowCallbacks
// chain flux_core.cpp already wires up (onMouseDown/Up/Move/Wheel/etc).
// The DOM nodes flux_painter_dom.cpp paints sit BENEATH this capture layer
// (pointer-events:none), purely visual — hit-testing is still done in C++
// via findWidgetAt()/the overlay dispatch chain, exactly as before. This
// is deliberate: it means NOTHING in flux_core.cpp, flux_widget.cpp, or any
// widget's handleMouseDown/Up/Move override needs to change for this
// backend at all.
//
// Known duplication (flagged, not hidden): timer/clipboard/cursor/mouse-
// capture plumbing below is largely copied from flux_window_web.cpp rather
// than shared via a common base — reasonable for this first pass since the
// two files are compiled mutually exclusively (never both at once), but a
// good candidate to factor into a shared flux_window_web_common.hpp later
// if the two ever drift or the duplication becomes a maintenance cost.

#ifdef __EMSCRIPTEN__

#include "flux/flux_window.hpp"
#include "flux/flux_platform.hpp"
#include "flux/flux_core.hpp"
#include "flux/flux_dom_adapter.hpp"

#include <emscripten.h>
#include <emscripten/html5.h>

#include <string>
#include <unordered_map>
#include <cstring>

// ============================================================================
// Web-only private state
// ============================================================================

struct PlatformWindow::WebState
{
    bool initialized = false;
    bool dirty = false;
    bool mouseCapture = false;
    std::unordered_map<TimerID, long> timerHandles;
};

// ============================================================================
// Key-code mapping — identical to flux_window_web.cpp (browser keyCode
// values already match the VK_* constants defined in flux_platform.hpp's
// __EMSCRIPTEN__ block; no backend-specific translation needed).
// ============================================================================

namespace
{
    inline int emKeyToVK(const EmscriptenKeyboardEvent *e) { return (int)e->keyCode; }
}

// ============================================================================
// Event callbacks — registered on #flux-input-capture (the capture div),
// which sits ON TOP of the DOM paint layer with pointer-events:auto.
// Logic is identical to flux_window_web.cpp's onMouseDown/Up/Move/etc —
// only the element selector differs (capture div vs canvas).
// ============================================================================

static EM_BOOL onMouseDown(int, const EmscriptenMouseEvent *e, void *userData)
{
    auto *self = static_cast<PlatformWindow *>(userData);
    if (!self || !self->webState) return EM_FALSE;

    int x = (int)e->targetX, y = (int)e->targetY;
    bool handled = (e->button == 0 && self->callbacks.onMouseDown)
                       ? self->callbacks.onMouseDown(x, y)
                       : false;
    if (handled) self->invalidate();
    return handled ? EM_TRUE : EM_FALSE;
}

static EM_BOOL onMouseUp(int, const EmscriptenMouseEvent *e, void *userData)
{
    auto *self = static_cast<PlatformWindow *>(userData);
    if (!self || !self->webState) return EM_FALSE;

    flux_web_detail::g_ctrlDown = e->ctrlKey;
    flux_web_detail::g_shiftDown = e->shiftKey;
    flux_web_detail::g_altDown = e->altKey;

    if (e->button != 0 || !self->callbacks.onMouseUp) return EM_FALSE;

    bool handled = self->callbacks.onMouseUp((int)e->targetX, (int)e->targetY);
    if (handled) self->invalidate();
    if (self->webState->mouseCapture) self->webState->mouseCapture = false;
    return handled ? EM_TRUE : EM_FALSE;
}

static EM_BOOL onMouseMove(int, const EmscriptenMouseEvent *e, void *userData)
{
    auto *self = static_cast<PlatformWindow *>(userData);
    if (!self || !self->webState) return EM_FALSE;

    flux_web_detail::g_ctrlDown = e->ctrlKey;
    flux_web_detail::g_shiftDown = e->shiftKey;
    flux_web_detail::g_altDown = e->altKey;

    if (!self->callbacks.onMouseMove) return EM_FALSE;
    bool handled = self->callbacks.onMouseMove((int)e->targetX, (int)e->targetY);
    if (handled) self->invalidate();
    return handled ? EM_TRUE : EM_FALSE;
}

static EM_BOOL onMouseLeave(int, const EmscriptenMouseEvent *, void *userData)
{
    auto *self = static_cast<PlatformWindow *>(userData);
    if (!self || !self->webState) return EM_FALSE;
    if (self->callbacks.onMouseLeave) self->callbacks.onMouseLeave();
    self->invalidate();
    return EM_FALSE;
}

static EM_BOOL onWheel(int, const EmscriptenWheelEvent *e, void *userData)
{
    auto *self = static_cast<PlatformWindow *>(userData);
    if (!self || !self->webState || !self->callbacks.onMouseWheel) return EM_FALSE;

    int delta = (e->deltaY != 0.0)
                    ? (int)(-(e->deltaY / std::abs(e->deltaY)) * WHEEL_DELTA)
                    : 0;
    if (delta == 0) return EM_FALSE;

    bool handled = self->callbacks.onMouseWheel(delta);
    if (handled) self->invalidate();
    return handled ? EM_TRUE : EM_FALSE;
}

static EM_BOOL onKeyDown(int, const EmscriptenKeyboardEvent *e, void *userData)
{
    auto *self = static_cast<PlatformWindow *>(userData);
    if (!self || !self->webState) return EM_FALSE;

    flux_web_detail::g_ctrlDown = e->ctrlKey;
    flux_web_detail::g_shiftDown = e->shiftKey;
    flux_web_detail::g_altDown = e->altKey;

    bool handled = false;
    if (self->callbacks.onKeyDown)
        handled = self->callbacks.onKeyDown(emKeyToVK(e));

    if (!handled && self->callbacks.onChar)
    {
        const char *key = e->key;
        if (key[0] != '\0' && key[1] == '\0')
        {
            wchar_t ch = (wchar_t)(unsigned char)key[0];
            if (ch >= 32) handled = self->callbacks.onChar(ch);
        }
    }

    if (handled) self->invalidate();
    return handled ? EM_TRUE : EM_FALSE;
}

static EM_BOOL onTouchStart(int, const EmscriptenTouchEvent *e, void *userData)
{
    auto *self = static_cast<PlatformWindow *>(userData);
    if (!self || !self->webState || e->numTouches == 0 || !self->callbacks.onMouseDown)
        return EM_FALSE;
    double dpr = EM_ASM_DOUBLE({ return Module._fluxDPR || 1.0; });
    int x = (int)(e->touches[0].targetX * dpr);
    int y = (int)(e->touches[0].targetY * dpr);
    bool handled = self->callbacks.onMouseDown(x, y);
    if (handled) self->invalidate();
    return EM_TRUE;
}

static EM_BOOL onTouchEnd(int, const EmscriptenTouchEvent *e, void *userData)
{
    auto *self = static_cast<PlatformWindow *>(userData);
    if (!self || !self->webState || e->numTouches == 0) return EM_FALSE;
    double dpr = EM_ASM_DOUBLE({ return Module._fluxDPR || 1.0; });
    int x = (int)(e->touches[0].targetX * dpr);
    int y = (int)(e->touches[0].targetY * dpr);
    if (self->callbacks.onMouseUp) self->callbacks.onMouseUp(x, y);
    self->invalidate();
    return EM_TRUE;
}

static EM_BOOL onTouchMove(int, const EmscriptenTouchEvent *e, void *userData)
{
    auto *self = static_cast<PlatformWindow *>(userData);
    if (!self || !self->webState || e->numTouches == 0 || !self->callbacks.onMouseMove)
        return EM_FALSE;
    double dpr = EM_ASM_DOUBLE({ return Module._fluxDPR || 1.0; });
    int x = (int)(e->touches[0].targetX * dpr);
    int y = (int)(e->touches[0].targetY * dpr);
    bool handled = self->callbacks.onMouseMove(x, y);
    if (handled) self->invalidate();
    return EM_TRUE;
}

// ============================================================================
// Timer shim — unchanged from flux_window_web.cpp
// ============================================================================

struct TimerShimArg { PlatformWindow *window; TimerID timerId; };

static void timerShim(void *arg)
{
    auto *shim = static_cast<TimerShimArg *>(arg);
    if (!shim || !shim->window) return;
    if (shim->window->callbacks.onTimer) shim->window->callbacks.onTimer(shim->timerId);
    shim->window->invalidate();
}

// ============================================================================
// PlatformWindow — DOM-renderer implementation
// ============================================================================

bool PlatformWindow::create(const std::string &, int width, int height,
                            AppInstance, void *)
{
    webState = new WebState();

    int physW = EM_ASM_INT({ return Module._fluxPhysicalWidth | 0; });
    int physH = EM_ASM_INT({ return Module._fluxPhysicalHeight | 0; });
    cachedWidth = (physW > 0) ? physW : width;
    cachedHeight = (physH > 0) ? physH : height;

    // ── Register event listeners on the capture div, NOT a canvas ──────────
    emscripten_set_mousedown_callback("#flux-input-capture", this, 1, onMouseDown);
    emscripten_set_mouseup_callback("#flux-input-capture", this, 1, onMouseUp);
    emscripten_set_mousemove_callback("#flux-input-capture", this, 1, onMouseMove);
    emscripten_set_mouseleave_callback("#flux-input-capture", this, 0, onMouseLeave);
    emscripten_set_wheel_callback("#flux-input-capture", this, 1, onWheel);
    emscripten_set_keydown_callback(EMSCRIPTEN_EVENT_TARGET_DOCUMENT, this, 1, onKeyDown);
    emscripten_set_touchstart_callback("#flux-input-capture", this, 1, onTouchStart);
    emscripten_set_touchend_callback("#flux-input-capture", this, 1, onTouchEnd);
    emscripten_set_touchmove_callback("#flux-input-capture", this, 1, onTouchMove);

    webState->initialized = true;
    webState->dirty = true; // first tick() must paint
    return true;
}

void PlatformWindow::destroy()
{
    if (!webState) return;
    for (auto &[id, handle] : webState->timerHandles)
        emscripten_clear_interval(handle);
    webState->timerHandles.clear();

    emscripten_set_mousedown_callback("#flux-input-capture", nullptr, 0, nullptr);
    emscripten_set_mouseup_callback("#flux-input-capture", nullptr, 0, nullptr);
    emscripten_set_mousemove_callback("#flux-input-capture", nullptr, 0, nullptr);
    emscripten_set_mouseleave_callback("#flux-input-capture", nullptr, 0, nullptr);
    emscripten_set_wheel_callback("#flux-input-capture", nullptr, 0, nullptr);
    emscripten_set_keydown_callback(EMSCRIPTEN_EVENT_TARGET_DOCUMENT, nullptr, 0, nullptr);
    emscripten_set_touchstart_callback("#flux-input-capture", nullptr, 0, nullptr);
    emscripten_set_touchend_callback("#flux-input-capture", nullptr, 0, nullptr);
    emscripten_set_touchmove_callback("#flux-input-capture", nullptr, 0, nullptr);

    delete webState;
    webState = nullptr;
}

// ── tick ──────────────────────────────────────────────────────────────────
//
// No canvas clear step needed — flux_painter_dom.cpp's ensureNode() updates
// each widget's EXISTING node in place every time its render() runs, so
// re-running the same paint pass each dirty frame is sufficient; there's
// nothing analogous to Canvas2D's "leftover pixels from last frame" problem
// for DOM to clean up first.

void PlatformWindow::tick()
{
    if (!webState || !webState->dirty) return;
    webState->dirty = false;

    if (auto *ui = FluxUI::getCurrentInstance())
        ui->drainPendingRebuilds();

    if (callbacks.onPaint)
    {
        GraphicsContext ctx(cachedWidth, cachedHeight);
        callbacks.onPaint(ctx, cachedWidth, cachedHeight);
    }
}

int PlatformWindow::run()
{
    emscripten_log(EM_LOG_WARN, "PlatformWindow::run() called on web — no-op");
    return 0;
}

void PlatformWindow::invalidate() { if (webState) webState->dirty = true; }
void PlatformWindow::invalidateRect(int, int, int, int) { if (webState) webState->dirty = true; }

bool PlatformWindow::valid() const { return webState != nullptr && webState->initialized; }

void PlatformWindow::startRenderLoop()
{
    // No-op — main loop is emscripten_set_main_loop in main.cpp, same as
    // the canvas backend. create() already sets webState->dirty = true so
    // the first tick() paints immediately.
}

// ── Timers — identical logic to flux_window_web.cpp ─────────────────────

void PlatformWindow::setTimer(TimerID id, int ms)
{
    if (!webState) return;
    auto it = webState->timerHandles.find(id);
    if (it != webState->timerHandles.end())
    {
        emscripten_clear_interval(it->second);
        webState->timerHandles.erase(it);
    }
    auto *arg = new TimerShimArg{this, id};
    long handle = emscripten_set_interval(timerShim, (double)ms, arg);
    webState->timerHandles[id] = handle;
}

void PlatformWindow::killTimer(TimerID id)
{
    if (!webState) return;
    auto it = webState->timerHandles.find(id);
    if (it == webState->timerHandles.end()) return;
    emscripten_clear_interval(it->second);
    webState->timerHandles.erase(it);
}

// ── Mouse capture — simulated via bool flag, same as canvas backend ──────

void PlatformWindow::captureMouseInput() { if (webState) webState->mouseCapture = true; }
void PlatformWindow::releaseMouseInput() { if (webState) webState->mouseCapture = false; }
bool PlatformWindow::isMouseCaptured() const { return webState && webState->mouseCapture; }

// ── Clipboard — identical to flux_window_web.cpp ─────────────────────────

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

// ── Handle / size accessors ───────────────────────────────────────────────

NativeWindow PlatformWindow::handle() const
{
    return const_cast<void *>(static_cast<const void *>("#flux-input-capture"));
}

void PlatformWindow::updateClientSize() { /* pushed in from JS, see fluxOnResize */ }

PlatformWindow::ScreenPoint PlatformWindow::clientToScreen(int cx, int cy) const { return {cx, cy}; }
PlatformWindow::ScreenPoint PlatformWindow::screenToClient(int sx, int sy) const { return {sx, sy}; }
PlatformWindow::ClientSize PlatformWindow::getClientSize() const { return {cachedWidth, cachedHeight}; }

GraphicsContext PlatformWindow::getMeasureContext() const
{
    return GraphicsContext(cachedWidth, cachedHeight);
}

// ── Cursor — targets the capture div instead of #flux-ui ────────────────

void PlatformWindow::setResizeCursorH()
{
    EM_ASM({ document.getElementById('flux-input-capture').style.cursor = 'ew-resize'; });
}
void PlatformWindow::setResizeCursorV()
{
    EM_ASM({ document.getElementById('flux-input-capture').style.cursor = 'ns-resize'; });
}
void PlatformWindow::setDefaultCursor()
{
    EM_ASM({ document.getElementById('flux-input-capture').style.cursor = 'default'; });
}

#endif // __EMSCRIPTEN__