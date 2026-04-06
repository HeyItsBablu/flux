// flux_window_linux.cpp
#if defined(__linux__) && !defined(__ANDROID__)

#include "flux/flux_window.hpp"
#include "flux/flux_http_platform.hpp"
#include <SDL2/SDL.h>
#include <cairo/cairo.h>

#include <cstdio>
#include <cstring>
#include <stdexcept>

// ============================================================================
// DEBUG MACRO — prints function name, line, and message to stderr instantly.
// Remove after diagnosis.
// ============================================================================
#define FLUX_DBG(fmt, ...) \
    do { \
        fprintf(stderr, "[FLUX %s:%d] " fmt "\n", __func__, __LINE__, ##__VA_ARGS__); \
        fflush(stderr); \
    } while(0)

// ============================================================================
// CairoState
// ============================================================================

struct PlatformWindow::CairoState {
  SDL_Surface     *sdlSurface  = nullptr;
  cairo_surface_t *cairoSurf   = nullptr;
  cairo_t         *cr          = nullptr;

  bool rebuild(SDL_Window *win) {
    teardown();
    sdlSurface = SDL_GetWindowSurface(win);
    if (!sdlSurface) {
      FLUX_DBG("SDL_GetWindowSurface FAILED: %s", SDL_GetError());
      return false;
    }
    cairoSurf = cairo_image_surface_create_for_data(
        static_cast<unsigned char *>(sdlSurface->pixels),
        CAIRO_FORMAT_ARGB32,
        sdlSurface->w, sdlSurface->h, sdlSurface->pitch);
    if (cairo_surface_status(cairoSurf) != CAIRO_STATUS_SUCCESS) {
      teardown(); return false;
    }
    cr = cairo_create(cairoSurf);
    return cairo_status(cr) == CAIRO_STATUS_SUCCESS;
  }

  void teardown() {
    if (cr)        { cairo_destroy(cr);               cr        = nullptr; }
    if (cairoSurf) { cairo_surface_destroy(cairoSurf); cairoSurf = nullptr; }
    sdlSurface = nullptr;
  }
  ~CairoState() { teardown(); }
};

// ============================================================================
// SDL timer callback
// ============================================================================

static Uint32 sdlTimerCallback(Uint32 interval, void *param) {
  SDL_Event e;
  SDL_zero(e);
  e.type      = SDL_USEREVENT;
  e.user.code = static_cast<Sint32>(reinterpret_cast<uintptr_t>(param));
  SDL_PushEvent(&e);
  return interval;
}

// ============================================================================
// create
// ============================================================================

bool PlatformWindow::create(const std::string &title, int width, int height,
                            AppInstance /*instance*/, void * /*userData*/) {
  FLUX_DBG("SDL_Init start");
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
    FLUX_DBG("SDL_Init FAILED: %s", SDL_GetError());
    return false;
  }
  FLUX_DBG("SDL_Init OK");

  // Must be after SDL_Init so SDL_RegisterEvents works.
  fluxInitUIThread();
  FLUX_DBG("fluxInitUIThread done, gFluxHttpEventType=%u", (unsigned)gFluxHttpEventType);

  nativeHandle = SDL_CreateWindow(
      title.c_str(),
      SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
      width, height,
      SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);

  if (!nativeHandle) {
    FLUX_DBG("SDL_CreateWindow FAILED: %s", SDL_GetError());
    SDL_Quit(); return false;
  }

  cachedWidth  = width;
  cachedHeight = height;
  running      = true;

  cairoState = new CairoState();
  if (!cairoState->rebuild(nativeHandle)) {
    FLUX_DBG("CairoState::rebuild FAILED");
    delete cairoState; cairoState = nullptr;
    SDL_DestroyWindow(nativeHandle); nativeHandle = nullptr;
    SDL_Quit(); return false;
  }

  FLUX_DBG("create() SUCCESS");
  return true;
}

// ============================================================================
// destroy
// ============================================================================

void PlatformWindow::destroy() {
  FLUX_DBG("destroy()");
  for (auto &[id, sdlId] : sdlTimerMap) SDL_RemoveTimer(sdlId);
  sdlTimerMap.clear();
  fluxShutdownHttpQueue();
  delete cairoState; cairoState = nullptr;
  if (nativeHandle) { SDL_DestroyWindow(nativeHandle); nativeHandle = nullptr; }
  SDL_Quit();
}

// ============================================================================
// run — THE KEY LOOP. Every line here is traced.
// ============================================================================

int PlatformWindow::run() {
  FLUX_DBG("run() entered");
  SDL_Event e;

  while (running) {

    // ── BLOCK until an event arrives ────────────────────────────────────────
    FLUX_DBG(">>> blocking in SDL_WaitEvent ...");
    if (SDL_WaitEvent(&e) == 0) {
      FLUX_DBG("SDL_WaitEvent ERROR: %s", SDL_GetError());
      continue;
    }
    FLUX_DBG("<<< SDL_WaitEvent woke up: type=0x%08x  (HTTP=0x%08x  USEREVENT=0x%08x)",
             (unsigned)e.type, (unsigned)gFluxHttpEventType, (unsigned)SDL_USEREVENT);

    // ── Is this our HTTP wake-up event? ────────────────────────────────────
    if (e.type == gFluxHttpEventType) {
      FLUX_DBG("    --> it IS the HTTP event type");
    } else {
      FLUX_DBG("    --> NOT the HTTP event type");
    }

    handleSDLEvent(e);

    // ── Drain any additional queued events ──────────────────────────────────
    int extra = 0;
    while (SDL_PollEvent(&e)) {
      ++extra;
      handleSDLEvent(e);
    }
    if (extra) FLUX_DBG("    drained %d extra events", extra);

    // ── Process HTTP callbacks ──────────────────────────────────────────────
    FLUX_DBG("    calling fluxProcessHttpEvents()");
    fluxProcessHttpEvents();
    FLUX_DBG("    fluxProcessHttpEvents() done. dirty=%d", (int)dirty);

    // ── Paint ───────────────────────────────────────────────────────────────
    if (dirty) {
      FLUX_DBG("    >>> PAINTING (dirty=true)");
      if (callbacks.onPaint && cairoState && cairoState->cr) {
        if (SDL_MUSTLOCK(cairoState->sdlSurface))
          SDL_LockSurface(cairoState->sdlSurface);

        GraphicsContext ctx(cairoState->cr, cachedWidth, cachedHeight);
        callbacks.onPaint(ctx, cachedWidth, cachedHeight);
        cairo_surface_flush(cairoState->cairoSurf);

        if (SDL_MUSTLOCK(cairoState->sdlSurface))
          SDL_UnlockSurface(cairoState->sdlSurface);

        SDL_UpdateWindowSurface(nativeHandle);
      }
      dirty = false;
      FLUX_DBG("    <<< paint done, dirty reset to false");
    } else {
      FLUX_DBG("    dirty=false, no paint this iteration");
    }
  }

  FLUX_DBG("run() exiting");
  return 0;
}

// ============================================================================
// handleSDLEvent
// ============================================================================

void PlatformWindow::handleSDLEvent(const SDL_Event &e) {

  // HTTP events are processed separately in run() via fluxProcessHttpEvents().
  if (e.type == gFluxHttpEventType) {
    FLUX_DBG("handleSDLEvent: HTTP event — skipping (handled in run())");
    return;
  }

  switch (e.type) {

  case SDL_QUIT:
    FLUX_DBG("SDL_QUIT");
    running = false;
    break;

  case SDL_MOUSEBUTTONDOWN:
    FLUX_DBG("SDL_MOUSEBUTTONDOWN btn=%d (%d,%d)", e.button.button, e.button.x, e.button.y);
    if (e.button.button == SDL_BUTTON_LEFT && callbacks.onMouseDown)
      if (callbacks.onMouseDown(e.button.x, e.button.y)) dirty = true;
    if (e.button.button == SDL_BUTTON_RIGHT && callbacks.onRightClick)
      if (callbacks.onRightClick(e.button.x, e.button.y)) dirty = true;
    break;

  case SDL_MOUSEBUTTONUP:
    if (e.button.button == SDL_BUTTON_LEFT && callbacks.onMouseUp)
      if (callbacks.onMouseUp(e.button.x, e.button.y)) dirty = true;
    break;

  case SDL_MOUSEMOTION:
    if (callbacks.onMouseMove) {
      bool changed = callbacks.onMouseMove(e.motion.x, e.motion.y);
      if (changed) {
        FLUX_DBG("SDL_MOUSEMOTION caused dirty=true");
        dirty = true;
      }
    }
    break;

  case SDL_MOUSEWHEEL: {
    int delta = e.wheel.y * WHEEL_DELTA;
    if (callbacks.onMouseWheel && callbacks.onMouseWheel(delta)) dirty = true;
    break;
  }

  case SDL_KEYDOWN:
    FLUX_DBG("SDL_KEYDOWN sym=0x%x scan=%d", (unsigned)e.key.keysym.sym, (int)e.key.keysym.scancode);
    if (callbacks.onKeyDown && callbacks.onKeyDown(e.key.keysym.sym)) dirty = true;
    break;

  case SDL_TEXTINPUT:
    if (callbacks.onChar && e.text.text[0] != '\0') {
      unsigned char b0 = static_cast<unsigned char>(e.text.text[0]);
      wchar_t ch = 0;
      if (b0 < 0x80) {
        ch = static_cast<wchar_t>(b0);
      } else if ((b0 & 0xE0) == 0xC0 && e.text.text[1]) {
        ch = static_cast<wchar_t>(((b0 & 0x1F) << 6) |
             (static_cast<unsigned char>(e.text.text[1]) & 0x3F));
      } else if ((b0 & 0xF0) == 0xE0 && e.text.text[1] && e.text.text[2]) {
        ch = static_cast<wchar_t>(((b0 & 0x0F) << 12) |
             ((static_cast<unsigned char>(e.text.text[1]) & 0x3F) << 6) |
             (static_cast<unsigned char>(e.text.text[2]) & 0x3F));
      }
      if (ch && callbacks.onChar(ch)) dirty = true;
    }
    break;

  case SDL_WINDOWEVENT:
    FLUX_DBG("SDL_WINDOWEVENT sub=%d", (int)e.window.event);
    switch (e.window.event) {
    case SDL_WINDOWEVENT_RESIZED:
    case SDL_WINDOWEVENT_SIZE_CHANGED:
      cachedWidth  = e.window.data1;
      cachedHeight = e.window.data2;
      if (cairoState && !cairoState->rebuild(nativeHandle)) { dirty = true; break; }
      if (callbacks.onResize && cairoState && cairoState->cr) {
        GraphicsContext ctx(cairoState->cr, cachedWidth, cachedHeight);
        callbacks.onResize(ctx, cachedWidth, cachedHeight);
      }
      dirty = true;
      break;
    case SDL_WINDOWEVENT_EXPOSED:       dirty = true; break;
    case SDL_WINDOWEVENT_LEAVE:         if (callbacks.onMouseLeave) callbacks.onMouseLeave(); break;
    case SDL_WINDOWEVENT_FOCUS_LOST:    if (callbacks.onFocusLost)  callbacks.onFocusLost(); dirty = true; break;
    case SDL_WINDOWEVENT_FOCUS_GAINED:  dirty = true; break;
    }
    break;

  case SDL_USEREVENT:
    FLUX_DBG("SDL_USEREVENT (timer) code=%d", e.user.code);
    if (callbacks.onTimer) callbacks.onTimer(static_cast<TimerID>(e.user.code));
    dirty = true;
    break;

  default:
    FLUX_DBG("unhandled event type=0x%08x", (unsigned)e.type);
    break;
  }
}

// ============================================================================
// invalidate / invalidateRect
// ============================================================================

void PlatformWindow::invalidate() {
  FLUX_DBG("invalidate() -> dirty=true");
  dirty = true;
}

void PlatformWindow::invalidateRect(int x, int y, int w, int h) {
  FLUX_DBG("invalidateRect(%d,%d,%d,%d) -> dirty=true", x, y, w, h);
  dirty = true;
}

// ============================================================================
// setTimer / killTimer
// ============================================================================

void PlatformWindow::setTimer(TimerID id, int ms) {
  killTimer(id);
  SDL_TimerID sdlId = SDL_AddTimer(static_cast<Uint32>(ms), sdlTimerCallback,
      reinterpret_cast<void *>(static_cast<uintptr_t>(id)));
  if (sdlId != 0) sdlTimerMap[id] = sdlId;
  FLUX_DBG("setTimer id=%u ms=%d sdlId=%d", (unsigned)id, ms, (int)sdlId);
}

void PlatformWindow::killTimer(TimerID id) {
  auto it = sdlTimerMap.find(id);
  if (it != sdlTimerMap.end()) { SDL_RemoveTimer(it->second); sdlTimerMap.erase(it); }
}

// ============================================================================
// Clipboard / mouse capture / coords / cursors / GDI stub
// ============================================================================

void PlatformWindow::setClipboardText(const std::string &text) { SDL_SetClipboardText(text.c_str()); }
std::string PlatformWindow::getClipboardText() {
  char *raw = SDL_GetClipboardText();
  std::string s = raw ? raw : "";
  SDL_free(raw);
  return s;
}

void PlatformWindow::captureMouseInput() { SDL_CaptureMouse(SDL_TRUE);  mouseCapture = true;  }
void PlatformWindow::releaseMouseInput() { SDL_CaptureMouse(SDL_FALSE); mouseCapture = false; }

PlatformWindow::ScreenPoint PlatformWindow::clientToScreen(int cx, int cy) const {
  int wx = 0, wy = 0;
  if (nativeHandle) SDL_GetWindowPosition(nativeHandle, &wx, &wy);
  return {wx + cx, wy + cy};
}
PlatformWindow::ScreenPoint PlatformWindow::screenToClient(int sx, int sy) const {
  int wx = 0, wy = 0;
  if (nativeHandle) SDL_GetWindowPosition(nativeHandle, &wx, &wy);
  return {sx - wx, sy - wy};
}
PlatformWindow::ClientSize PlatformWindow::getClientSize() const { return {cachedWidth, cachedHeight}; }

void PlatformWindow::setResizeCursorH() { static SDL_Cursor *c = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZEWE); if (c) SDL_SetCursor(c); }
void PlatformWindow::setResizeCursorV() { static SDL_Cursor *c = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZENS); if (c) SDL_SetCursor(c); }
void PlatformWindow::setDefaultCursor() { static SDL_Cursor *c = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_ARROW);  if (c) SDL_SetCursor(c); }

void PlatformWindow::startupGdiplus() {}

GraphicsContext PlatformWindow::getMeasureContext() const {
  if (cairoState && cairoState->cr)
    return GraphicsContext(cairoState->cr, cachedWidth, cachedHeight);
  return GraphicsContext();
}

#endif // __linux__