// flux_android_main.cpp
#ifdef __ANDROID__
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <android_native_app_glue.h>
#include <jni.h>
#include <time.h>

#include "flux/flux.hpp"
#include "flux/flux_canvas2d.hpp"
#include "flux/widgets/flux_file_picker.hpp"
#include "flux/flux_gl.hpp"
#include "flux/flux_oes_blit.hpp"
#include "flux/flux_canvas2d_backend.hpp"
// Forward declaration — defined in lib/main.cpp
WidgetPtr createApp(FluxUI *app);

// ── Statics ───────────────────────────────────────────────────────────────────
static FluxUI *s_app = nullptr;
static Canvas2DGL *s_canvasGL = nullptr;
static AAssetManager *s_assetManager = nullptr;
ANativeActivity *s_activity = nullptr;

static jobject s_videoSurface = nullptr;

// ── JVM + EGL handles ────────────────────────────────────────────────────────
static JavaVM *s_jvm = nullptr;
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

// ── Canvas2DGL accessor ───────────────────────────────────────────────────────
Canvas2DGL *FluxAndroid_getCanvasGL() { return s_canvasGL; }

// ── Asset / misc accessors ────────────────────────────────────────────────────
void FluxAndroid_setAssetManager(AAssetManager *am) { s_assetManager = am; }
AAssetManager *FluxAndroid_getAssetManager() { return s_assetManager; }

EGLDisplay FluxAndroid_getEGLDisplay() { return s_eglDisplay; }
EGLContext FluxAndroid_getEGLContext() { return s_eglContext; }
EGLSurface FluxAndroid_getEGLSurface() { return s_eglSurface; }

// ── JNI env ───────────────────────────────────────────────────────────────────
JNIEnv *getJNIEnv()
{
    if (!s_jvm)
        return nullptr;
    JNIEnv *env = nullptr;
    jint ret = s_jvm->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_6);
    if (ret == JNI_EDETACHED)
        s_jvm->AttachCurrentThread(&env, nullptr);
    return env;
}

// ── Canvas2DGL helpers ────────────────────────────────────────────────────────
static void canvasGL_registerFonts()
{
    if (!s_canvasGL)
        return;
    struct
    {
        const char *name;
        const char *path;
    } fonts[] = {
        {"sans", "/system/fonts/Roboto-Regular.ttf"},
        {"sans-bold", "/system/fonts/Roboto-Bold.ttf"},
        {"sans-italic", "/system/fonts/Roboto-Italic.ttf"},
        {"mono", "/system/fonts/DroidSansMono.ttf"},
    };
    for (auto &f : fonts)
    {
        Canvas2DBackend *b = Canvas2DBackend::create(s_canvasGL);
        Canvas2D::registerFont(b, f.name, f.path);
        Canvas2DBackend::destroy(b);
    }
}

static void canvasGL_create()
{
    if (!s_canvasGL)
        s_canvasGL = new Canvas2DGL();
    if (!s_canvasGL->init())
    {
        __android_log_print(ANDROID_LOG_ERROR, "FluxUI", "Canvas2DGL init failed");
    }
    else
    {
        canvasGL_registerFonts();
    }
}

static void canvasGL_reinit()
{
    if (!s_canvasGL)
    {
        canvasGL_create();
        return;
    }
    s_canvasGL->destroy();
    s_canvasGL->init();
    canvasGL_registerFonts();
}

static void canvasGL_destroyGLObjects()
{
    if (s_canvasGL)
        s_canvasGL->destroy();
}

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

// ── JNI / platform helpers ────────────────────────────────────────────────────
std::string FluxAndroid_getFilesDir()
{
    JNIEnv *env = getJNIEnv();
    if (!env)
        return "/sdcard";
    jobject activityObj = s_activity->clazz;
    jclass activityCls = env->GetObjectClass(activityObj);
    jmethodID getFilesDir = env->GetMethodID(activityCls, "getFilesDir", "()Ljava/io/File;");
    jobject filesDir = env->CallObjectMethod(activityObj, getFilesDir);
    jclass fileCls = env->GetObjectClass(filesDir);
    jmethodID getAbs = env->GetMethodID(fileCls, "getAbsolutePath", "()Ljava/lang/String;");
    jstring pathStr = (jstring)env->CallObjectMethod(filesDir, getAbs);
    const char *chars = env->GetStringUTFChars(pathStr, nullptr);
    std::string result(chars);
    env->ReleaseStringUTFChars(pathStr, chars);
    env->DeleteLocalRef(filesDir);
    env->DeleteLocalRef(pathStr);
    return result;
}

std::string FluxAndroid_saveToMediaStore(const std::string &filename,
                                         const std::string &mimeType,
                                         const std::string &relativePath,
                                         const uint8_t *data, size_t dataSize)
{
    JNIEnv *env = getJNIEnv();
    if (!env || !s_activity)
        return "";

    jobject activityObj = s_activity->clazz;
    jclass activityCls = env->GetObjectClass(activityObj);

    jmethodID getResolver = env->GetMethodID(activityCls, "getContentResolver",
                                             "()Landroid/content/ContentResolver;");
    jobject resolver = env->CallObjectMethod(activityObj, getResolver);
    jclass resolverCls = env->GetObjectClass(resolver);

    jclass cvCls = env->FindClass("android/content/ContentValues");
    jmethodID cvCtor = env->GetMethodID(cvCls, "<init>", "()V");
    jobject cv = env->NewObject(cvCls, cvCtor);

    jmethodID cvPutStr = env->GetMethodID(cvCls, "put",
                                          "(Ljava/lang/String;Ljava/lang/String;)V");
    jmethodID cvPutInt = env->GetMethodID(cvCls, "put",
                                          "(Ljava/lang/String;Ljava/lang/Integer;)V");

    auto putStr = [&](const char *k, const char *v)
    {
        jstring jk = env->NewStringUTF(k), jv = env->NewStringUTF(v);
        env->CallVoidMethod(cv, cvPutStr, jk, jv);
        env->DeleteLocalRef(jk);
        env->DeleteLocalRef(jv);
    };
    putStr("_display_name", filename.c_str());
    putStr("mime_type", mimeType.c_str());
    putStr("relative_path", relativePath.c_str());

    jclass intCls = env->FindClass("java/lang/Integer");
    jmethodID intOf = env->GetStaticMethodID(intCls, "valueOf",
                                             "(I)Ljava/lang/Integer;");
    jstring kPending = env->NewStringUTF("is_pending");
    env->CallVoidMethod(cv, cvPutInt, kPending,
                        env->CallStaticObjectMethod(intCls, intOf, 1));

    const char *collClass = "android/provider/MediaStore$Images$Media";
    if (mimeType.find("audio") != std::string::npos)
        collClass = "android/provider/MediaStore$Audio$Media";
    else if (mimeType.find("video") != std::string::npos)
        collClass = "android/provider/MediaStore$Video$Media";

    jclass mediaCls = env->FindClass(collClass);
    jfieldID extUriField = env->GetStaticFieldID(mediaCls,
                                                 "EXTERNAL_CONTENT_URI", "Landroid/net/Uri;");
    jobject extUri = env->GetStaticObjectField(mediaCls, extUriField);

    jmethodID insert = env->GetMethodID(resolverCls, "insert",
                                        "(Landroid/net/Uri;Landroid/content/ContentValues;)"
                                        "Landroid/net/Uri;");
    jobject fileUri = env->CallObjectMethod(resolver, insert, extUri, cv);
    if (!fileUri)
        return "";

    jmethodID openStream = env->GetMethodID(resolverCls, "openOutputStream",
                                            "(Landroid/net/Uri;)Ljava/io/OutputStream;");
    jobject outStream = env->CallObjectMethod(resolver, openStream, fileUri);
    if (!outStream)
        return "";

    jclass osCls = env->GetObjectClass(outStream);
    jmethodID osWrite = env->GetMethodID(osCls, "write", "([B)V");
    jmethodID osClose = env->GetMethodID(osCls, "close", "()V");

    static constexpr size_t kChunk = 65536;
    for (size_t offset = 0; offset < dataSize;)
    {
        size_t toWrite = std::min(kChunk, dataSize - offset);
        jbyteArray arr = env->NewByteArray((jsize)toWrite);
        env->SetByteArrayRegion(arr, 0, (jsize)toWrite,
                                reinterpret_cast<const jbyte *>(data + offset));
        env->CallVoidMethod(outStream, osWrite, arr);
        env->DeleteLocalRef(arr);
        offset += toWrite;
    }
    env->CallVoidMethod(outStream, osClose);

    jstring kPending2 = env->NewStringUTF("is_pending");
    env->CallVoidMethod(cv, cvPutInt, kPending2,
                        env->CallStaticObjectMethod(intCls, intOf, 0));

    jmethodID update = env->GetMethodID(resolverCls, "update",
                                        "(Landroid/net/Uri;Landroid/content/ContentValues;"
                                        "Ljava/lang/String;[Ljava/lang/String;)I");
    env->CallIntMethod(resolver, update, fileUri, cv, nullptr, nullptr);

    jclass uriCls = env->GetObjectClass(fileUri);
    jmethodID uriToString = env->GetMethodID(uriCls, "toString",
                                             "()Ljava/lang/String;");
    jstring uriStr = (jstring)env->CallObjectMethod(fileUri, uriToString);
    const char *uriChars = env->GetStringUTFChars(uriStr, nullptr);
    std::string result(uriChars);
    env->ReleaseStringUTFChars(uriStr, uriChars);

    env->DeleteLocalRef(cv);
    env->DeleteLocalRef(resolver);
    env->DeleteLocalRef(fileUri);
    env->DeleteLocalRef(outStream);
    env->DeleteLocalRef(uriStr);
    env->DeleteLocalRef(kPending);
    env->DeleteLocalRef(kPending2);
    return result;
}

void *FluxVideo_createSurfaceTexture(GLuint texId)
{
    JNIEnv *env = getJNIEnv();
    if (!env)
        return nullptr;
    jclass cls = env->FindClass("android/graphics/SurfaceTexture");
    jmethodID ctor = env->GetMethodID(cls, "<init>", "(I)V");
    jobject st = env->NewObject(cls, ctor, (jint)texId);
    return st ? reinterpret_cast<void *>(env->NewGlobalRef(st)) : nullptr;
}

void FluxVideo_updateTexImage(void *surfaceTexture)
{
    JNIEnv *env = getJNIEnv();
    if (!env || !surfaceTexture)
        return;
    jobject st = reinterpret_cast<jobject>(surfaceTexture);
    jclass cls = env->GetObjectClass(st);
    jmethodID mid = env->GetMethodID(cls, "updateTexImage", "()V");
    env->CallVoidMethod(st, mid);
}

ANativeWindow *FluxVideo_getNativeWindow(void *surfaceTexture)
{
    JNIEnv *env = getJNIEnv();
    if (!env || !surfaceTexture)
        return nullptr;

    if (s_videoSurface)
    {
        env->DeleteGlobalRef(s_videoSurface);
        s_videoSurface = nullptr;
    }

    jobject st = reinterpret_cast<jobject>(surfaceTexture);
    jclass surfCls = env->FindClass("android/view/Surface");
    jmethodID ctor = env->GetMethodID(surfCls, "<init>",
                                      "(Landroid/graphics/SurfaceTexture;)V");
    jobject local = env->NewObject(surfCls, ctor, st);
    if (!local)
        return nullptr;

    s_videoSurface = env->NewGlobalRef(local);
    env->DeleteLocalRef(local);

    return ANativeWindow_fromSurface(env, s_videoSurface);
}

void FluxVideo_destroySurfaceTexture(void *surfaceTexture)
{
    JNIEnv *env = getJNIEnv();
    if (!env || !surfaceTexture)
        return;

    if (s_videoSurface)
    {
        env->DeleteGlobalRef(s_videoSurface);
        s_videoSurface = nullptr;
    }

    env->DeleteGlobalRef(reinterpret_cast<jobject>(surfaceTexture));
}

// ── Permissions ───────────────────────────────────────────────────────────────
void FluxAndroid_requestPermission(const char* permission)
{
    JNIEnv *env = getJNIEnv();
    if (!env || !s_activity)
        return;

    jobject activityObj = s_activity->clazz;
    jclass activityCls = env->GetObjectClass(activityObj);
    jmethodID checkPerm = env->GetMethodID(activityCls, "checkSelfPermission",
                                           "(Ljava/lang/String;)I");
    jstring jp = env->NewStringUTF(permission);
    jint result = env->CallIntMethod(activityObj, checkPerm, jp);
    env->DeleteLocalRef(jp);
    if (result == 0) return; // already granted

    jmethodID reqPerms = env->GetMethodID(activityCls, "requestPermissions",
                                          "([Ljava/lang/String;I)V");
    jclass strCls = env->FindClass("java/lang/String");
    jobjectArray arr = env->NewObjectArray(1, strCls, nullptr);
    jstring p = env->NewStringUTF(permission);
    env->SetObjectArrayElement(arr, 0, p);
    env->DeleteLocalRef(p);
    env->CallVoidMethod(activityObj, reqPerms, arr, 1001);
    env->DeleteLocalRef(arr);
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

        s_jvm = app->activity->vm;
        s_activity = app->activity;

        FluxFilePickerAndroid::init(getJNIEnv(), app->activity);
        FluxJNI::init(app);
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
            auto fcfg = FluxAppWidget::getInstance();
            s_app->createWindow(fcfg->title, fcfg->windowWidth, fcfg->windowHeight);

            // ── FluxGL (replaces nvgCreateGLES2) ─────────────────────────
            FluxGL_init();
            gl_registerFonts();

            canvasGL_create();

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

            canvasGL_reinit();

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
        FluxMic::get().cancel();
        FluxVideo::get().close();

        // ── Destroy FluxGL (replaces nvgDeleteGLES2) ──────────────────────
        FluxGL_destroy();
        FluxOESBlit_destroy();
        canvasGL_destroyGLObjects();

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
        if (s_canvasGL)
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
        Renderer::renderWidget(gc, s_app->getRoot().get(), s_app->getFontCache());

        eglSwapBuffers(win.getEGLDisplay(), win.getEGLSurface());
    }
}

#endif // __ANDROID__