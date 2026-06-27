//linux/main.cpp
#if defined(__linux__) && !defined(__ANDROID__)
#include <cstdio>
#include <cstdlib>
#include <string>
#include <SDL2/SDL.h>
#include "flux/flux.hpp"

// Forward declaration — defined in lib/main.cpp
WidgetPtr createApp(FluxUI* app);

int main(int /*argc*/, char** /*argv*/) {

#ifdef FLUX_DEBUG
    setvbuf(stdout, nullptr, _IONBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);
#endif

    // ── Undo VS Code snap's GTK/GDK environment pollution ────────────────────
    auto restoreOrUnset = [](const char* var) {
        std::string origKey = std::string(var) + "_VSCODE_SNAP_ORIG";
        const char* orig = ::getenv(origKey.c_str());
        if (orig && orig[0] != '\0')
            ::setenv(var, orig, 1);
        else
            ::unsetenv(var);
    };

    restoreOrUnset("GDK_PIXBUF_MODULEDIR");
    restoreOrUnset("GDK_PIXBUF_MODULE_FILE");
    restoreOrUnset("GTK_PATH");
    restoreOrUnset("GTK_EXE_PREFIX");
    restoreOrUnset("GTK_IM_MODULE_FILE");
    restoreOrUnset("GSETTINGS_SCHEMA_DIR");
    restoreOrUnset("GIO_MODULE_DIR");
    restoreOrUnset("LOCPATH");

    const char* xdgOrig = ::getenv("XDG_DATA_DIRS_VSCODE_SNAP_ORIG");
    if (xdgOrig && xdgOrig[0] != '\0')
        ::setenv("XDG_DATA_DIRS", xdgOrig, 1);

    const char* dataHomeOrig = ::getenv("XDG_DATA_HOME_VSCODE_SNAP_ORIG");
    if (dataHomeOrig && dataHomeOrig[0] == '\0')
        ::unsetenv("XDG_DATA_HOME");

    ::unsetenv("LD_PRELOAD");
    ::unsetenv("LD_LIBRARY_PATH");
    // ─────────────────────────────────────────────────────────────────────────

    FluxUI app(nullptr);
    app.build([&]() { return createApp(&app); });

    // getInstance() now returns shared_ptr<FluxAppWidget>, not a raw pointer.
    auto cfg = FluxAppWidget::getInstance();

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

    if (cfg->fullscreen || cfg->maximize)
        SDL_MaximizeWindow(static_cast<SDL_Window*>(app.getWindow()));

    return app.run();
}

#endif // __linux__