// main.cpp
#include "flux/flux.hpp"

// ============================================================================
// LINUX ENTRY POINT
// ============================================================================

#ifdef __linux__

#include <cstdio>

int main(int /*argc*/, char ** /*argv*/) {

#ifdef FLUX_DEBUG
  // stdout/stderr already open on Linux — nothing to allocate.
  // Disable buffering so debug output appears immediately.
  setvbuf(stdout, nullptr, _IONBF, 0);
  setvbuf(stderr, nullptr, _IONBF, 0);
#endif

  FluxUI app(nullptr); // AppInstance is void* / unused on Linux
  app.build([&]() { return createApp(&app); });

  auto *cfg = static_cast<FluxAppWidget *>(FluxAppWidget::getInstance());

  int w = cfg->width;
  int h = cfg->height;

  if (cfg->fullscreen || cfg->maximize) {
    SDL_DisplayMode dm;
    if (SDL_GetCurrentDisplayMode(0, &dm) == 0) {
      w = dm.w;
      h = dm.h;
    }
  }

  app.createWindow(cfg->title, w, h);

  // SDL fullscreen / maximize
  if (cfg->fullscreen) {
    SDL_SetWindowFullscreen(static_cast<SDL_Window *>(app.getWindow()),
                            SDL_WINDOW_FULLSCREEN_DESKTOP);
  } else if (cfg->maximize) {
    SDL_MaximizeWindow(static_cast<SDL_Window *>(app.getWindow()));
  }

  return app.run();
}

#endif // __linux__