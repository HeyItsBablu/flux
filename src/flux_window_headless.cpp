// src/flux_window_headless.cpp
//
// PlatformWindow implementation for FLUX_SSR — a native, non-Emscripten
// build with no real window, no event loop, no GPU. Used only by flux_ssr
// (ssr/main.cpp).
//
// Model: "do layout + one render pass, then stop." There is no run()/tick()
// loop here at all — ssr/main.cpp drives everything explicitly:
//   1. create()                          — just remembers width/height
//   2. FluxUI::createWindow()/build()    — runs layout (via flux_core.cpp's
//                                          existing logic, unchanged)
//   3. ssr/main.cpp calls
//      callbacks.onPaint(ctx, w, h) ITSELF, once, directly — there is no
//      tick()/main-loop to do this automatically, unlike every other
//      platform. This is deliberate: a one-shot render has no reason to
//      own a loop at all.

#ifdef FLUX_SSR

#include "flux/flux_window.hpp"
#include "flux/flux_platform.hpp"

bool PlatformWindow::create(const std::string & /*title*/, int width, int height,
                            AppInstance /*hInstance*/, void * /*userData*/)
{
    cachedWidth = width;
    cachedHeight = height;
    return true;
}

void PlatformWindow::destroy() { /* nothing owned to release */ }

void PlatformWindow::startRenderLoop() { /* no loop — see file header */ }

int PlatformWindow::run()
{
    // Never called — ssr/main.cpp drives rendering directly. Present so
    // the linker is satisfied if something generic calls it.
    return 0;
}

void PlatformWindow::tick() { /* no-op — see file header */ }

void PlatformWindow::invalidate() { /* nothing to mark dirty — single-shot */ }
void PlatformWindow::invalidateRect(int, int, int, int) { }

void PlatformWindow::setTimer(TimerID, int) { /* no event loop to fire on */ }
void PlatformWindow::killTimer(TimerID) { }

bool PlatformWindow::valid() const { return cachedWidth > 0 && cachedHeight > 0; }

void PlatformWindow::setClipboardText(const std::string &) { }
std::string PlatformWindow::getClipboardText() { return {}; }

void PlatformWindow::captureMouseInput() { }
void PlatformWindow::releaseMouseInput() { }
bool PlatformWindow::isMouseCaptured() const { return false; }

NativeWindow PlatformWindow::handle() const { return nullptr; }
void PlatformWindow::updateClientSize() { }

PlatformWindow::ScreenPoint PlatformWindow::clientToScreen(int cx, int cy) const { return {cx, cy}; }
PlatformWindow::ScreenPoint PlatformWindow::screenToClient(int sx, int sy) const { return {sx, sy}; }
PlatformWindow::ClientSize PlatformWindow::getClientSize() const { return {cachedWidth, cachedHeight}; }

// GraphicsContext on this platform mirrors the web/Android shape (just
// width/height — see flux_platform.hpp's __EMSCRIPTEN__ / __ANDROID__
// branches) since flux_painter_dom.cpp never actually reads ctx fields
// beyond that; every real paint call goes through IDomAdapter, not ctx.
GraphicsContext PlatformWindow::getMeasureContext() const
{
    return GraphicsContext(cachedWidth, cachedHeight);
}

void PlatformWindow::setResizeCursorH() { }
void PlatformWindow::setResizeCursorV() { }
void PlatformWindow::setDefaultCursor() { }

#endif // FLUX_SSR