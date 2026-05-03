#include "flux/flux.hpp"
#ifdef _WIN32

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
#ifdef FLUX_DEBUG
    AllocConsole();
    FILE *f;
    freopen_s(&f, "CONOUT$", "w", stdout);
#endif

    FluxUI app(hInstance);

    // Build widget first — it carries all config now
    app.build([&]() { return createApp(&app); });

    // getInstance() now returns shared_ptr<FluxAppWidget>, not a raw pointer.
    // Keep the shared_ptr alive for the duration of the config reads so the
    // widget cannot be destroyed between getInstance() and the first use.
    auto cfg = FluxAppWidget::getInstance();

    int w = cfg->windowWidth;
    int h = cfg->windowHeight;
    if (cfg->fullscreen || cfg->maximize) {
        w = GetSystemMetrics(SM_CXSCREEN);
        h = GetSystemMetrics(SM_CYSCREEN);
    }

    app.createWindow(cfg->title, w, h);

    if (cfg->maximize || cfg->fullscreen)
        ShowWindow(app.getWindow(), SW_MAXIMIZE);

    return app.run();
}

#endif // _WIN32