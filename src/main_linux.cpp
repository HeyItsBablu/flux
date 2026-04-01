#ifdef __linux__

#include <cstdio>          // setvbuf, stdout, stderr
#include <SDL2/SDL.h>      // SDL_DisplayMode, SDL_Window, SDL_SetWindowFullscreen etc.
#include "flux/flux.hpp"   // FluxUI, FluxAppWidget, createApp

int main(int /*argc*/, char ** /*argv*/) {
#ifdef FLUX_DEBUG
    setvbuf(stdout, nullptr, _IONBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);
#endif

    FluxUI app(nullptr);
    app.build([&]() { return createApp(&app); });

    auto *cfg = static_cast<FluxAppWidget *>(FluxAppWidget::getInstance());

    int w = cfg->windowWidth;
    int h = cfg->windowHeight;

    if (cfg->fullscreen || cfg->maximize) {
        SDL_DisplayMode dm;
        if (SDL_GetCurrentDisplayMode(0, &dm) == 0) {
            w = dm.w;
            h = dm.h;
        }
    }

    app.createWindow(cfg->title, w, h);

    if (cfg->fullscreen) {
        SDL_SetWindowFullscreen(static_cast<SDL_Window *>(app.getWindow()),
                                SDL_WINDOW_FULLSCREEN_DESKTOP);
    } else if (cfg->maximize) {
        SDL_MaximizeWindow(static_cast<SDL_Window *>(app.getWindow()));
    }

    return app.run();
}

#endif // __linux__