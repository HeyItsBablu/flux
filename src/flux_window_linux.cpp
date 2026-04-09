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

  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {

    return false;
  }


  // Must be after SDL_Init so SDL_RegisterEvents works.
  fluxInitUIThread();


  nativeHandle = SDL_CreateWindow(
      title.c_str(),
      SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
      width, height,
      SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);

  if (!nativeHandle) {

    SDL_Quit(); return false;
  }

  cachedWidth  = width;
  cachedHeight = height;
  running      = true;

  cairoState = new CairoState();
  if (!cairoState->rebuild(nativeHandle)) {
  
    delete cairoState; cairoState = nullptr;
    SDL_DestroyWindow(nativeHandle); nativeHandle = nullptr;
    SDL_Quit(); return false;
  }

 
  return true;
}

// ============================================================================
// destroy
// ============================================================================

void PlatformWindow::destroy() {

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

  SDL_Event e;

  while (running) {

    // ── BLOCK until an event arrives ────────────────────────────────────────

    if (SDL_WaitEvent(&e) == 0) {

      continue;
    }



    handleSDLEvent(e);

    // ── Drain any additional queued events ──────────────────────────────────
    int extra = 0;
    while (SDL_PollEvent(&e)) {
      ++extra;
      handleSDLEvent(e);
    }
    

    // ── Process HTTP callbacks ──────────────────────────────────────────────

    fluxProcessHttpEvents();


    // ── Paint ───────────────────────────────────────────────────────────────
    if (dirty) {

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
    
    } 
  }


  return 0;
}

// ============================================================================
// handleSDLEvent
// ============================================================================

void PlatformWindow::handleSDLEvent(const SDL_Event &e) {

  // HTTP events are processed separately in run() via fluxProcessHttpEvents().
  if (e.type == gFluxHttpEventType) {
   
    return;
  }

  switch (e.type) {

  case SDL_QUIT:

    running = false;
    break;

  case SDL_MOUSEBUTTONDOWN:

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
  
    if (callbacks.onTimer) callbacks.onTimer(static_cast<TimerID>(e.user.code));
    dirty = true;
    break;

  default:
    break;
  }
}

// ============================================================================
// invalidate / invalidateRect
// ============================================================================

void PlatformWindow::invalidate() {
  dirty = true;
  
  SDL_Event e;
  SDL_zero(e);
  e.type = SDL_WINDOWEVENT;
  e.window.event = SDL_WINDOWEVENT_EXPOSED;
  SDL_PushEvent(&e);
}

void PlatformWindow::invalidateRect(int x, int y, int w, int h) {
  if (!dirty) {          
    dirty = true;
    SDL_Event e;
    SDL_zero(e);
    e.type = SDL_WINDOWEVENT;
    e.window.event = SDL_WINDOWEVENT_EXPOSED;
    SDL_PushEvent(&e);
  } else {
    dirty = true;        
  }
}

// ============================================================================
// setTimer / killTimer
// ============================================================================

void PlatformWindow::setTimer(TimerID id, int ms) {
  killTimer(id);
  SDL_TimerID sdlId = SDL_AddTimer(static_cast<Uint32>(ms), sdlTimerCallback,
      reinterpret_cast<void *>(static_cast<uintptr_t>(id)));
  if (sdlId != 0) sdlTimerMap[id] = sdlId;
  
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