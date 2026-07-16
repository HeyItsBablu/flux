// flux_android_main.cpp
#ifdef __ANDROID__
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <android_native_app_glue.h>
#include <jni.h>
#include <time.h>
#include "flux/flux_android_jni.hpp"

#include "flux/flux.hpp"
#include "flux/flux_mic.hpp"
#include "flux/widgets/flux_file_picker.hpp"
#include "flux/flux_gl.hpp"
#include "flux/flux_oes_blit.hpp"
#include "AppConfig.generated.h"
// Forward declaration — defined in lib/main.cpp
WidgetPtr createApp(FluxUI *app);

// ── Statics ───────────────────────────────────────────────────────────────────
static FluxUI *s_app = nullptr;
static AAssetManager *s_assetManager = nullptr;

static EGLDisplay s_eglDisplay = EGL_NO_DISPLAY;
static EGLContext s_eglContext = EGL_NO_CONTEXT;
static EGLSurface s_eglSurface = EGL_NO_SURFACE;

// ── Misc ─────────────────────────────────────────────────────────────────────
static std::string s_cacertPath;

// ── MDI font path ─────────────────────────────────────────────────────────────
static std::string s_mdiFontPath;
const std::string &FluxAndroid_getMDIFontPath() { return s_mdiFontPath; }

// ── DPI scale ─────────────────────────────────────────────────────────────────
static float s_dpiScale = 1.f;
void FluxAndroid_setDpiScale(float scale) { s_dpiScale = scale; }
float FluxAndroid_getDpiScale() { return s_dpiScale; }

// ── Asset / misc accessors ────────────────────────────────────────────────────
void FluxAndroid_setAssetManager(AAssetManager *am) { s_assetManager = am; }
AAssetManager *FluxAndroid_getAssetManager() { return s_assetManager; }

EGLDisplay FluxAndroid_getEGLDisplay() { return s_eglDisplay; }
EGLContext FluxAndroid_getEGLContext() { return s_eglContext; }
EGLSurface FluxAndroid_getEGLSurface() { return s_eglSurface; }

// ── GL font registration ──────────────────────────────────────────────────────
// Called after FluxGL_init() / FluxGL_reinit() so the glyph atlas is ready.
static void gl_registerFonts()
{
    // Default / UI fonts
    FluxGL_registerFont("default", "/system/fonts/Roboto-Regular.ttf");
    FluxGL_registerFont("Roboto_regular", "/system/fonts/Roboto-Regular.ttf");
    FluxGL_registerFont("Roboto_bold", "/system/fonts/Roboto-Bold.ttf");
    FluxGL_registerFont("Roboto_light", "/system/fonts/Roboto-Light.ttf");
    FluxGL_registerFont("Segoe UI_regular", "/system/fonts/Roboto-Regular.ttf");
    FluxGL_registerFont("Segoe UI_bold", "/system/fonts/Roboto-Bold.ttf");
    FluxGL_registerFont("Segoe UI_light", "/system/fonts/Roboto-Light.ttf");

    // MDI icon font
    if (!s_mdiFontPath.empty())
    {
        int h = FluxGL_registerFont("Material Design Icons_regular", s_mdiFontPath);
        if (h != -1)
            __android_log_print(ANDROID_LOG_INFO, "FluxUI",
                                "FluxGL: MDI font registered, index=%d", h);
        else
            __android_log_print(ANDROID_LOG_ERROR, "FluxUI",
                                "FluxGL: Failed to register MDI font from %s",
                                s_mdiFontPath.c_str());
    }
    else
    {
        __android_log_print(ANDROID_LOG_WARN, "FluxUI",
                            "FluxGL: MDI font path empty — icons will not render");
    }
}

// ── CA cert extraction ────────────────────────────────────────────────────────
static void extractCACert(android_app *app)
{
    AAssetManager *am = app->activity->assetManager;
    if (!am)
        return;
    AAsset *asset = AAssetManager_open(am, "cacert.pem", AASSET_MODE_BUFFER);
    if (!asset)
        return;
    s_cacertPath = std::string(app->activity->internalDataPath) + "/cacert.pem";
    FILE *f = fopen(s_cacertPath.c_str(), "wb");
    if (f)
    {
        fwrite(AAsset_getBuffer(asset), 1, AAsset_getLength(asset), f);
        fclose(f);
    }
    else
    {
        s_cacertPath.clear();
    }
    AAsset_close(asset);
}

// ── MDI font extraction ───────────────────────────────────────────────────────
static void extractMDIFont(android_app *app)
{
    AAssetManager *am = app->activity->assetManager;
    if (!am)
        return;

    s_mdiFontPath = std::string(app->activity->internalDataPath) + "/materialdesignicons-webfont.ttf";

    FILE *check = fopen(s_mdiFontPath.c_str(), "rb");
    if (check)
    {
        fclose(check);
        return;
    }

    AAsset *asset = AAssetManager_open(am,
                                       "materialdesignicons-webfont.ttf", AASSET_MODE_BUFFER);
    if (!asset)
    {
        __android_log_print(ANDROID_LOG_ERROR, "FluxUI",
                            "MDI font asset not found!");
        s_mdiFontPath.clear();
        return;
    }

    FILE *f = fopen(s_mdiFontPath.c_str(), "wb");
    if (f)
    {
        fwrite(AAsset_getBuffer(asset), 1, AAsset_getLength(asset), f);
        fclose(f);
    }
    else
    {
        s_mdiFontPath.clear();
    }
    AAsset_close(asset);
}

// ── Input ─────────────────────────────────────────────────────────────────────
static int32_t handle_input(android_app * /*app*/, AInputEvent *event)
{
    if (s_app)
        s_app->getPlatformWindow().handleAndroidEvent(event);
    return 1;
}

// ── Asset debug ───────────────────────────────────────────────────────────────
static void debugListAssets(AAssetManager *am, const char *path = "")
{
    AAssetDir *dir = AAssetManager_openDir(am, path);
    if (!dir)
        return;
    const char *name;
    while ((name = AAssetDir_getNextFileName(dir)) != nullptr)
    {
        std::string full = std::string(path).empty() ? name : std::string(path) + "/" + name;
        __android_log_print(ANDROID_LOG_DEBUG, "FluxAssets", "  [FILE] %s", full.c_str());
    }
    AAssetDir_close(dir);
}

// ── Lifecycle ─────────────────────────────────────────────────────────────────
static void handle_cmd(android_app *app, int32_t cmd)
{
    switch (cmd)
    {
    case APP_CMD_INIT_WINDOW:
    {
        if (!app->window)
            break;

        FluxJNI::init(app);
        FluxFilePickerAndroid::init(FluxJNI::attach(), app->activity);
        FluxAndroid_setAssetManager(app->activity->assetManager);

        __android_log_print(ANDROID_LOG_DEBUG, "FluxAssets", "=== Asset listing ===");
        debugListAssets(app->activity->assetManager, "");
        __android_log_print(ANDROID_LOG_DEBUG, "FluxAssets", "=====================");

        extractCACert(app);
        if (!s_cacertPath.empty())
            FluxHttp::setCABundle(s_cacertPath);

        extractMDIFont(app);

        // DPI
        AConfiguration *cfg = AConfiguration_new();
        AConfiguration_fromAssetManager(cfg, app->activity->assetManager);
        FluxAndroid_setDpiScale(AConfiguration_getDensity(cfg) / 160.f);
        AConfiguration_delete(cfg);

        if (!s_app)
        {
            // ── First launch ──────────────────────────────────────────────
            s_app = new FluxUI(app);
            s_app->build([&]()
                         { return createApp(s_app); });
            // Title/size come straight from AppConfig.json — FluxAppWidget no
            // longer carries window state. Note: Android has no title bar
            // (Theme.NoTitleBar.Fullscreen in the manifest) and the surface
            // is always resized to the real screen via APP_CMD_WINDOW_RESIZED
            // below, so these are just seed values before the real size is known.
            s_app->createWindow(FLUX_APP_NAME, FLUX_APP_WINDOW_WIDTH, FLUX_APP_WINDOW_HEIGHT);

            // ── FluxGL (replaces nvgCreateGLES2) ─────────────────────────
            FluxGL_init();
            gl_registerFonts();

            auto &win = s_app->getPlatformWindow();
            s_eglDisplay = win.getEGLDisplay();
            s_eglContext = win.getEGLContext();
            s_eglSurface = win.getEGLSurface();

            FluxOESBlit_init(1920, 1080);
            s_app->getFontCache().clear();
        }
        else
        {
            // ── Surface reconnect ─────────────────────────────────────────
            s_app->getPlatformWindow().reinitSurface(app->window);

            // ── Reinit FluxGL (replaces nvgDeleteGLES2 / nvgCreateGLES2) ─
            FluxGL_reinit();
            gl_registerFonts();

            auto &win = s_app->getPlatformWindow();
            s_eglDisplay = win.getEGLDisplay();
            s_eglContext = win.getEGLContext();
            s_eglSurface = win.getEGLSurface();

            FluxOESBlit_reinit();
            s_app->getFontCache().clear();
            s_app->getRoot()->markNeedsLayout();
        }
        break;
    }

    case APP_CMD_TERM_WINDOW:
        FluxCamera::get().close();
        FluxMic::get().close();
        FluxVideo::get().close();

        // ── Destroy FluxGL (replaces nvgDeleteGLES2) ──────────────────────
        FluxGL_destroy();
        FluxOESBlit_destroy();

        if (s_app)
            s_app->getPlatformWindow().destroySurface();

        s_eglDisplay = EGL_NO_DISPLAY;
        s_eglContext = EGL_NO_CONTEXT;
        s_eglSurface = EGL_NO_SURFACE;
        break;

    case APP_CMD_WINDOW_RESIZED:
    case APP_CMD_CONFIG_CHANGED:
        if (s_app)
        {
            auto &win = s_app->getPlatformWindow();
            win.updateClientSize();
            auto gc = win.getMeasureContext();
            if (win.callbacks.onResize)
                win.callbacks.onResize(gc, win.clientWidth(), win.clientHeight());
        }
        break;

    case APP_CMD_LOST_FOCUS:
        if (s_app && s_app->getPlatformWindow().callbacks.onMouseLeave)
            s_app->getPlatformWindow().callbacks.onMouseLeave();
        break;

    default:
        break;
    }
}

// ── Main loop ─────────────────────────────────────────────────────────────────
void android_main(android_app *app)
{
    app->onAppCmd = handle_cmd;
    app->onInputEvent = handle_input;

    while (true)
    {
        int events;
        android_poll_source *source;
        while (ALooper_pollOnce(0, nullptr, &events,
                                reinterpret_cast<void **>(&source)) >= 0)
        {
            if (source)
                source->process(app, source);
            if (app->destroyRequested)
                return;
        }

        if (!s_app || !FluxGL_get())
            continue;

        auto &win = s_app->getPlatformWindow();
        win.pollTimers();
        FluxFilePickerAndroid::drainPendingCallbacks();

        s_app->drainPendingRebuilds();

        auto sz = win.getClientSize();
        float dpi = FluxAndroid_getDpiScale();
        int physW = (int)(sz.width * dpi);
        int physH = (int)(sz.height * dpi);

        // ── Pass 1: render each CanvasWidget into its own FBO ─────────────
        // Each widget owns and lazily creates its own GL resources
        // (shader/atlas/fonts/backend) on first render — no shared-global
        // gate needed here.
        CanvasWidget::tickAllGL(s_app->getRoot().get(), sz.width, sz.height, dpi);

        // ── Pass 2: FluxGL renders the full UI ────────────────────────────
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, physW, physH);
        glClearColor(0.f, 0.f, 0.f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        // Begin frame: sets viewport + blend state in FluxGL
        FluxGL_beginFrame((float)sz.width, (float)sz.height, dpi);

        GraphicsContext gc(sz.width, sz.height);

        if (win.callbacks.onPaint)
            win.callbacks.onPaint(gc, sz.width, sz.height);

        eglSwapBuffers(win.getEGLDisplay(), win.getEGLSurface());
    }
}

#endif // __ANDROID__