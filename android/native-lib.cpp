// native-lib.cpp
#include <android_native_app_glue.h>
#include <GLES2/gl2.h>          // must come before nanovg_gl.h
#include "flux/flux.hpp"
#include "app.hpp"
#include "nanovg.h"
#include "nanovg_gl.h"

static FluxUI*     s_app = nullptr;
static NVGcontext* s_vg  = nullptr;

extern void FluxAndroid_setVG(NVGcontext* vg);
extern void FluxAndroid_setVG(NVGcontext* vg);
extern void FluxAndroid_setDpiScale(float scale);
// ── Input → PlatformWindow ────────────────────────────────────────────────────
static int32_t handle_input(android_app* app, AInputEvent* event) {
    // PlatformWindow::handleAndroidEvent routes to WindowCallbacks
    // We access it via a small accessor you add to FluxUI:
    //   PlatformWindow& FluxUI::getPlatformWindow() { return window; }
    if (s_app)
        s_app->getPlatformWindow().handleAndroidEvent(event);
    return 1;
}

// ── Lifecycle ─────────────────────────────────────────────────────────────────
static void handle_cmd(android_app* app, int32_t cmd) {
    switch (cmd) {
        case APP_CMD_INIT_WINDOW: {
            if (!app->window) break;

            FluxAndroid_setAssetManager(app->activity->assetManager);

            AConfiguration* config = AConfiguration_new();
            AConfiguration_fromAssetManager(config, app->activity->assetManager);
            float dpiScale = AConfiguration_getDensity(config) / 160.0f;
            AConfiguration_delete(config);
            FluxAndroid_setDpiScale(dpiScale);

            s_app = new FluxUI(app);
            s_app->build([&]() { return createApp(s_app); });

            auto* cfg = static_cast<FluxAppWidget*>(FluxAppWidget::getInstance());
            s_app->createWindow(cfg->title, cfg->windowWidth, cfg->windowHeight);

            s_vg = nvgCreateGLES2(NVG_ANTIALIAS | NVG_STENCIL_STROKES);
            nvgCreateFont(s_vg, "default", "/system/fonts/Roboto-Regular.ttf");
            FluxAndroid_setVG(s_vg);


            static AAssetManager* s_assetManager = nullptr;
            void FluxAndroid_setAssetManager(AAssetManager* am) { s_assetManager = am; }
            AAssetManager* FluxAndroid_getAssetManager() { return s_assetManager; }

            // ← Clear font cache now that NVG is ready, then rebuild layout
            //   so all fonts that were created with nvgHandle=-1 get reloaded.
            s_app->getFontCache().clear();
            s_app->rebuild();

            break;
        }

        case APP_CMD_TERM_WINDOW:
            if (s_vg)  { nvgDeleteGLES2(s_vg); s_vg = nullptr; }
            if (s_app) { delete s_app; s_app = nullptr; }
            break;

        case APP_CMD_WINDOW_RESIZED:
        case APP_CMD_CONFIG_CHANGED:
            if (s_app) {
                auto& win = s_app->getPlatformWindow();
                win.updateClientSize();   // re-query EGL surface size
                auto gc = win.getMeasureContext();
                // trigger onResize callback → layout engine re-runs
                if (win.callbacks.onResize)
                    win.callbacks.onResize(gc,
                                           win.clientWidth(), win.clientHeight());
            }
            break;

        case APP_CMD_GAINED_FOCUS:
            break;

        case APP_CMD_LOST_FOCUS:
            if (s_app && s_app->getPlatformWindow().callbacks.onMouseLeave)
                s_app->getPlatformWindow().callbacks.onMouseLeave();
            break;
    }
}

// ── Main loop ─────────────────────────────────────────────────────────────────
void android_main(android_app* app) {
    app->onAppCmd     = handle_cmd;
    app->onInputEvent = handle_input;

    while (true) {
        int events;
        android_poll_source* source;
        while (ALooper_pollOnce(0, nullptr, &events,
                                reinterpret_cast<void**>(&source)) >= 0) {
            if (source) source->process(app, source);
            if (app->destroyRequested) return;
        }

        if (!s_app || !s_vg) continue;

        auto& win = s_app->getPlatformWindow();

        // Poll software timers (replaces WM_TIMER on Win32)
        win.pollTimers();

        // Render every frame (or only when dirty — your choice)
        auto sz = win.getClientSize();
        glClearColor(0.98f, 0.98f, 0.98f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

        nvgBeginFrame(s_vg, sz.width, sz.height, 1.0f);
        FluxAndroid_setVG(s_vg);

        // This is your unmodified widget render path
        GraphicsContext gc(sz.width, sz.height);
        Renderer::renderWidget(gc, s_app->getRoot().get(),
                               s_app->getFontCache());

        nvgEndFrame(s_vg);
        eglSwapBuffers(win.getEGLDisplay(), win.getEGLSurface());
    }
}