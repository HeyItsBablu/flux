#if defined(__linux__) && !defined(__ANDROID__)
#include <cstdio>
#include <cstdlib>
#include <string>
#include <SDL2/SDL.h>
#include "flux/flux.hpp"

int main(int /*argc*/, char** /*argv*/) {

#ifdef FLUX_DEBUG
    setvbuf(stdout, nullptr, _IONBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);
#endif

    // ── Undo VS Code snap's GTK/GDK environment pollution ────────────────────
    // VS Code snap overwrites GTK/GDK vars and saves the originals in
    // *_VSCODE_SNAP_ORIG counterparts. Restore them so zenity (a GTK app)
    // links against the system glibc instead of the snap runtime.
    auto restoreOrUnset = [](const char* var) {
        std::string origKey = std::string(var) + "_VSCODE_SNAP_ORIG";
        const char* orig = ::getenv(origKey.c_str());
        if (orig && orig[0] != '\0')
            ::setenv(var, orig, 1);   // restore the pre-snap value
        else
            ::unsetenv(var);          // wasn't set before snap, remove it
    };

    restoreOrUnset("GDK_PIXBUF_MODULEDIR");
    restoreOrUnset("GDK_PIXBUF_MODULE_FILE");
    restoreOrUnset("GTK_PATH");
    restoreOrUnset("GTK_EXE_PREFIX");
    restoreOrUnset("GTK_IM_MODULE_FILE");
    restoreOrUnset("GSETTINGS_SCHEMA_DIR");
    restoreOrUnset("GIO_MODULE_DIR");
    restoreOrUnset("LOCPATH");

    // XDG_DATA_DIRS: restore original — snap prepends its own paths to the front
    const char* xdgOrig = ::getenv("XDG_DATA_DIRS_VSCODE_SNAP_ORIG");
    if (xdgOrig && xdgOrig[0] != '\0')
        ::setenv("XDG_DATA_DIRS", xdgOrig, 1);

    // XDG_DATA_HOME was empty before snap — remove it so apps use the default
    const char* dataHomeOrig = ::getenv("XDG_DATA_HOME_VSCODE_SNAP_ORIG");
    if (dataHomeOrig && dataHomeOrig[0] == '\0')
        ::unsetenv("XDG_DATA_HOME");

    ::unsetenv("LD_PRELOAD");
    ::unsetenv("LD_LIBRARY_PATH");
    // ─────────────────────────────────────────────────────────────────────────

    FluxUI app(nullptr);
    app.build([&]() { return createApp(&app); });

    auto* cfg = static_cast<FluxAppWidget*>(FluxAppWidget::getInstance());
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
        // On Linux, use maximize instead of true fullscreen
        // to keep window decorations (title bar, close/min/max buttons)
        SDL_MaximizeWindow(static_cast<SDL_Window*>(app.getWindow()));
    } else if (cfg->maximize) {
        SDL_MaximizeWindow(static_cast<SDL_Window*>(app.getWindow()));
    }

    return app.run();
}

#endif // __linux__