// flux_window_linux.cpp
#if defined(__linux__) && !defined(__ANDROID__)

#include "flux/flux_window.hpp"
#include <SDL2/SDL.h>
#include <cairo/cairo.h>

#include <cstring> // memset
#include <stdexcept>

// ============================================================================
// GraphicsContext helpers
// On Linux, GraphicsContext wraps a cairo_t*.
// The Win32 side wraps an HDC — we mirror that pattern exactly.
// ============================================================================

// Defined here so the rest of the codebase can access cr through ctx.cr
// Make sure flux_platform.hpp has for Linux:
//
//   struct GraphicsContext {
//       cairo_t* cr  = nullptr;
//       int      width  = 0;
//       int      height = 0;
//       explicit GraphicsContext(cairo_t* c, int w = 0, int h = 0)
//           : cr(c), width(w), height(h) {}
//       GraphicsContext() = default;
//   };
//
// The Win32 side keeps   GraphicsContext(HDC)  — both live inside
// their respective #ifdef blocks in flux_platform.hpp.

// ============================================================================
// Internal Cairo surface state — kept entirely out of the header
// ============================================================================

struct PlatformWindow::CairoState {
  SDL_Surface *sdlSurface = nullptr;
  cairo_surface_t *cairoSurf = nullptr;
  cairo_t *cr = nullptr;

  bool rebuild(SDL_Window *win) {
    teardown();

    sdlSurface = SDL_GetWindowSurface(win);
    if (!sdlSurface)
      return false;

    cairoSurf = cairo_image_surface_create_for_data(
        static_cast<unsigned char *>(sdlSurface->pixels), CAIRO_FORMAT_ARGB32,
        sdlSurface->w, sdlSurface->h, sdlSurface->pitch);

    if (cairo_surface_status(cairoSurf) != CAIRO_STATUS_SUCCESS) {
      teardown();
      return false;
    }

    cr = cairo_create(cairoSurf);
    return cairo_status(cr) == CAIRO_STATUS_SUCCESS;
  }

  void teardown() {
    if (cr) {
      cairo_destroy(cr);
      cr = nullptr;
    }
    if (cairoSurf) {
      cairo_surface_destroy(cairoSurf);
      cairoSurf = nullptr;
    }
    sdlSurface = nullptr;
  }

  ~CairoState() { teardown(); }
};

// ============================================================================
// SDL timer callback — fires a SDL_USEREVENT carrying the TimerID
// ============================================================================

static Uint32 sdlTimerCallback(Uint32 interval, void *param) {
  SDL_Event e;
  SDL_zero(e);
  e.type = SDL_USEREVENT;
  e.user.code = static_cast<Sint32>(reinterpret_cast<uintptr_t>(param));
  SDL_PushEvent(&e);
  return interval; // ← return interval to repeat, not 0
}

// ============================================================================
// PlatformWindow private Linux members
// Declared in the #ifndef _WIN32 block of flux_window.hpp:
//
//   SDL_Window*  nativeHandle = nullptr;
//   bool         running      = false;
//   bool         mouseCapture = false;
//   CairoState*  cairoState   = nullptr;   // opaque pointer
//
//   void handleSDLEvent(const SDL_Event&);
// ============================================================================

// ============================================================================
// create
// ============================================================================

bool PlatformWindow::create(const std::string &title, int width, int height,
                            AppInstance /*instance*/, void * /*userData*/) {
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0)
    return false;

  nativeHandle = SDL_CreateWindow(
      title.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width,
      height, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
      // ✗ SDL_WINDOW_OPENGL must NOT be set — incompatible with
      //   SDL_GetWindowSurface / software Cairo rendering.
  );
  if (!nativeHandle) {
    SDL_Quit();
    return false;
  }

  cachedWidth = width;
  cachedHeight = height;
  running = true;

  cairoState = new CairoState();
  if (!cairoState->rebuild(nativeHandle)) {
    delete cairoState;
    cairoState = nullptr;
    SDL_DestroyWindow(nativeHandle);
    nativeHandle = nullptr;
    SDL_Quit();
    return false;
  }

  return true;
}

// ============================================================================
// destroy
// ============================================================================

void PlatformWindow::destroy() {
  delete cairoState;
  cairoState = nullptr;

  if (nativeHandle) {
    SDL_DestroyWindow(nativeHandle);
    nativeHandle = nullptr;
  }
  SDL_Quit();
}

// ============================================================================
// run  —  event-driven; paints only when invalidated (dirty flag)
// ============================================================================

int PlatformWindow::run() {
  SDL_Event e;

  while (running) {
    // Block until there is at least one event — no busy-spin.
    if (SDL_WaitEvent(&e) == 0)
      continue;

    handleSDLEvent(e);

    // Drain any additional queued events before repainting.
    while (SDL_PollEvent(&e))
      handleSDLEvent(e);

    // Paint once per batch of events if dirty.
    if (dirty && callbacks.onPaint && cairoState && cairoState->cr) {
      if (SDL_MUSTLOCK(cairoState->sdlSurface))
        SDL_LockSurface(cairoState->sdlSurface);

      GraphicsContext ctx(cairoState->cr, cachedWidth, cachedHeight);
      callbacks.onPaint(ctx, cachedWidth, cachedHeight);

      cairo_surface_flush(cairoState->cairoSurf);

      if (SDL_MUSTLOCK(cairoState->sdlSurface))
        SDL_UnlockSurface(cairoState->sdlSurface);

      SDL_UpdateWindowSurface(nativeHandle);
      dirty = false;
    }
  }

  return 0;
}

// ============================================================================
// handleSDLEvent
// ============================================================================

void PlatformWindow::handleSDLEvent(const SDL_Event &e) {
  switch (e.type) {

  // ── Quit ────────────────────────────────────────────────────────────────
  case SDL_QUIT:
    running = false;
    break;

  // ── Mouse buttons ────────────────────────────────────────────────────────
  case SDL_MOUSEBUTTONDOWN:
    if (e.button.button == SDL_BUTTON_LEFT && callbacks.onMouseDown)
      if (callbacks.onMouseDown(e.button.x, e.button.y))
        dirty = true;
    if (e.button.button == SDL_BUTTON_RIGHT && callbacks.onRightClick)
      if (callbacks.onRightClick(e.button.x, e.button.y))
        dirty = true;
    break;

  case SDL_MOUSEBUTTONUP:
    if (e.button.button == SDL_BUTTON_LEFT && callbacks.onMouseUp)
      if (callbacks.onMouseUp(e.button.x, e.button.y))
        dirty = true;
    break;

  // ── Mouse motion ─────────────────────────────────────────────────────────
  case SDL_MOUSEMOTION:
    if (callbacks.onMouseMove)
      if (callbacks.onMouseMove(e.motion.x, e.motion.y))
        dirty = true;
    break;

  // ── Mouse wheel ──────────────────────────────────────────────────────────
  case SDL_MOUSEWHEEL: {
    // SDL y > 0 = scroll up; Win32 WHEEL_DELTA > 0 = scroll up — same sign.
    int delta = e.wheel.y * WHEEL_DELTA;
    if (callbacks.onMouseWheel)
      if (callbacks.onMouseWheel(delta))
        dirty = true;
    break;
  }

  // ── Keyboard ─────────────────────────────────────────────────────────────
  case SDL_KEYDOWN:
    if (callbacks.onKeyDown)
      if (callbacks.onKeyDown(e.key.keysym.sym))
        dirty = true;
    break;

  case SDL_TEXTINPUT:
    // SDL_TEXTINPUT gives UTF-8; we convert the first codepoint to wchar_t.
    if (callbacks.onChar && e.text.text[0] != '\0') {
      // Fast path: ASCII
      wchar_t ch =
          static_cast<wchar_t>(static_cast<unsigned char>(e.text.text[0]));
      // For codepoints > 127 a proper UTF-8 decode is needed;
      // this handles the common Latin/ASCII case used by most widgets.
      if (callbacks.onChar(ch))
        dirty = true;
    }
    break;

  // ── Window events ────────────────────────────────────────────────────────
  case SDL_WINDOWEVENT:
    switch (e.window.event) {

    case SDL_WINDOWEVENT_RESIZED:
    case SDL_WINDOWEVENT_SIZE_CHANGED:
      cachedWidth = e.window.data1;
      cachedHeight = e.window.data2;

      if (cairoState)
        cairoState->rebuild(nativeHandle);
      if (callbacks.onResize && cairoState && cairoState->cr) {
        GraphicsContext ctx(cairoState->cr, cachedWidth, cachedHeight);
        callbacks.onResize(ctx, cachedWidth, cachedHeight);
      }
      dirty = true;
      break;

    case SDL_WINDOWEVENT_EXPOSED:
      dirty = true;
      break;

    case SDL_WINDOWEVENT_LEAVE:
      if (callbacks.onMouseLeave)
        callbacks.onMouseLeave();
      break;

    case SDL_WINDOWEVENT_FOCUS_LOST:
      // Window lost focus (title bar drag, alt-tab, click outside, etc.)
      if (callbacks.onFocusLost)
        callbacks.onFocusLost();
      dirty = true;
      break;

    case SDL_WINDOWEVENT_FOCUS_GAINED:
      dirty = true;
      break;
    }
    break;

  // ── Timers (fired as SDL_USEREVENT by sdlTimerCallback) ─────────────────
  case SDL_USEREVENT:
    if (callbacks.onTimer)
      callbacks.onTimer(static_cast<TimerID>(e.user.code));
    // Timer callbacks typically mutate state → repaint.
    dirty = true;
    break;
  }
}

// ============================================================================
// invalidate / invalidateRect
// On Linux we use a dirty flag; SDL redraws the whole surface at once.
// ============================================================================

void PlatformWindow::invalidate() { dirty = true; }

void PlatformWindow::invalidateRect(int /*x*/, int /*y*/, int /*w*/,
                                    int /*h*/) {
  // Cairo/SDL software rendering redraws the whole surface anyway.
  dirty = true;
}

// ============================================================================
// setTimer / killTimer
// ============================================================================

void PlatformWindow::setTimer(TimerID id, int ms) {
  // Cancel any existing SDL timer for this id before registering a new one.
  killTimer(id);
  SDL_TimerID sdlId =
      SDL_AddTimer(static_cast<Uint32>(ms), sdlTimerCallback,
                   reinterpret_cast<void *>(static_cast<uintptr_t>(id)));
  if (sdlId != 0)
    sdlTimerMap[id] = sdlId;
}

void PlatformWindow::killTimer(TimerID id) {
  auto it = sdlTimerMap.find(id);
  if (it != sdlTimerMap.end()) {
    SDL_RemoveTimer(it->second);
    sdlTimerMap.erase(it);
  }
}

// ============================================================================
// Clipboard
// ============================================================================

void PlatformWindow::setClipboardText(const std::string &text) {
  SDL_SetClipboardText(text.c_str());
}

std::string PlatformWindow::getClipboardText() {
  char *raw = SDL_GetClipboardText();
  std::string s = raw ? raw : "";
  SDL_free(raw);
  return s;
}

// ============================================================================
// Mouse capture
// ============================================================================

void PlatformWindow::captureMouseInput() {
  SDL_CaptureMouse(SDL_TRUE);
  mouseCapture = true;
}

void PlatformWindow::releaseMouseInput() {
  SDL_CaptureMouse(SDL_FALSE);
  mouseCapture = false;
}

// ============================================================================
// Coordinate conversion
// ============================================================================

PlatformWindow::ScreenPoint PlatformWindow::clientToScreen(int cx,
                                                           int cy) const {
  int wx = 0, wy = 0;
  if (nativeHandle)
    SDL_GetWindowPosition(nativeHandle, &wx, &wy);
  return {wx + cx, wy + cy};
}

PlatformWindow::ScreenPoint PlatformWindow::screenToClient(int sx,
                                                           int sy) const {
  int wx = 0, wy = 0;
  if (nativeHandle)
    SDL_GetWindowPosition(nativeHandle, &wx, &wy);
  return {sx - wx, sy - wy};
}

PlatformWindow::ClientSize PlatformWindow::getClientSize() const {
  return {cachedWidth, cachedHeight};
}

// ============================================================================
// Cursors
// SDL_CreateSystemCursor allocates — cache to avoid leaking one per call.
// ============================================================================

void PlatformWindow::setResizeCursorH() {
  static SDL_Cursor *c = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZEWE);
  if (c)
    SDL_SetCursor(c);
}

void PlatformWindow::setResizeCursorV() {
  static SDL_Cursor *c = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZENS);
  if (c)
    SDL_SetCursor(c);
}

void PlatformWindow::setDefaultCursor() {
  static SDL_Cursor *c = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_ARROW);
  if (c)
    SDL_SetCursor(c);
}

// ============================================================================
// GDI+ stub / getMeasureContext
// ============================================================================

void PlatformWindow::startupGdiplus() { /* no-op on Linux */ }

GraphicsContext PlatformWindow::getMeasureContext() const {
  // Return the live Cairo context so LayoutEngine can measure text.
  // If called before create() (unlikely but guard it), return empty.
  if (cairoState && cairoState->cr)
    return GraphicsContext(cairoState->cr, cachedWidth, cachedHeight);
  return GraphicsContext();
}

// ============================================================================
// handle() — NativeWindow is SDL_Window* on Linux
// ============================================================================

#endif // __linux__
