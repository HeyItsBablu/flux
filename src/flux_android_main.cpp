#ifdef __ANDROID__
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <android_native_app_glue.h>
#include <jni.h>
#include <time.h>

#include "flux/flux.hpp"
#include "flux/widgets/flux_file_picker.hpp"

#include "nanovg.h"
#include "nanovg_gl.h"

// ── Statics
// ───────────────────────────────────────────────────────────────────
static FluxUI *s_app = nullptr;
static NVGcontext *s_vg = nullptr;
static AAssetManager *s_assetManager = nullptr;
ANativeActivity *s_activity = nullptr;

// ── JVM + EGL handles ────────────────────────────────────────────────────────
static JavaVM *s_jvm = nullptr;
static EGLDisplay s_eglDisplay = EGL_NO_DISPLAY;
static EGLContext s_eglContext = EGL_NO_CONTEXT;
static EGLSurface s_eglSurface = EGL_NO_SURFACE;

// ── CA cert path (set once at startup) ───────────────────────────────────────
static std::string s_cacertPath;
static std::string s_pendingPickedPath;
static std::function<void()> s_pendingPickerResult;

// ── Global accessors
// ──────────────────────────────────────────────────────────
extern void FluxAndroid_setVG(NVGcontext *vg);

void FluxAndroid_setAssetManager(AAssetManager *am) { s_assetManager = am; }
AAssetManager *FluxAndroid_getAssetManager() { return s_assetManager; }

EGLDisplay FluxAndroid_getEGLDisplay() { return s_eglDisplay; }
EGLContext FluxAndroid_getEGLContext() { return s_eglContext; }
EGLSurface FluxAndroid_getEGLSurface() { return s_eglSurface; }

JNIEnv *getJNIEnv() {
  if (!s_jvm)
    return nullptr;
  JNIEnv *env = nullptr;
  jint ret = s_jvm->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_6);
  if (ret == JNI_EDETACHED) {
    s_jvm->AttachCurrentThread(&env, nullptr);
  }
  return env;
}

extern void FluxAndroid_setDpiScale(float scale);
extern float FluxAndroid_getDpiScale();

// ============================================================================
// CA cert extraction — copies cacert.pem from assets to internal storage
// so curl can use it for HTTPS certificate verification.
// Place cacert.pem (from https://curl.se/ca/cacert.pem) in app/src/main/assets/
// ============================================================================

static void extractCACert(android_app *app) {
  AAssetManager *am = app->activity->assetManager;
  if (!am) {
    __android_log_print(ANDROID_LOG_ERROR, "FluxHttp",
                        "extractCACert: no asset manager");
    return;
  }

  AAsset *asset = AAssetManager_open(am, "cacert.pem", AASSET_MODE_BUFFER);
  if (!asset) {
    __android_log_print(ANDROID_LOG_ERROR, "FluxHttp",
                        "extractCACert: cacert.pem not found in assets");
    return;
  }

  s_cacertPath = std::string(app->activity->internalDataPath) + "/cacert.pem";

  FILE *f = fopen(s_cacertPath.c_str(), "wb");
  if (!f) {
    __android_log_print(ANDROID_LOG_ERROR, "FluxHttp",
                        "extractCACert: failed to open %s for writing",
                        s_cacertPath.c_str());
    AAsset_close(asset);
    s_cacertPath.clear();
    return;
  }

  const void *buf = AAsset_getBuffer(asset);
  size_t len = AAsset_getLength(asset);
  fwrite(buf, 1, len, f);
  fclose(f);
  AAsset_close(asset);

  __android_log_print(ANDROID_LOG_INFO, "FluxHttp",
                      "extractCACert: wrote %zu bytes to %s", len,
                      s_cacertPath.c_str());
}

// ============================================================================
// FluxVideo JNI shims
// ============================================================================

std::string FluxAndroid_getFilesDir() {
  JNIEnv *env = getJNIEnv();
  if (!env)
    return "/sdcard";

  jobject activityObj = s_activity->clazz;
  jclass activityCls = env->GetObjectClass(activityObj);
  jmethodID getFilesDir =
      env->GetMethodID(activityCls, "getFilesDir", "()Ljava/io/File;");
  jobject filesDir = env->CallObjectMethod(activityObj, getFilesDir);

  jclass fileCls = env->GetObjectClass(filesDir);
  jmethodID getAbsPath =
      env->GetMethodID(fileCls, "getAbsolutePath", "()Ljava/lang/String;");
  jstring pathStr = (jstring)env->CallObjectMethod(filesDir, getAbsPath);

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
                                         const uint8_t *data, size_t dataSize) {
  JNIEnv *env = getJNIEnv();
  if (!env || !s_activity)
    return "";

  jobject activityObj = s_activity->clazz;
  jclass activityCls = env->GetObjectClass(activityObj);

  jmethodID getResolver = env->GetMethodID(
      activityCls, "getContentResolver", "()Landroid/content/ContentResolver;");
  jobject resolver = env->CallObjectMethod(activityObj, getResolver);
  jclass resolverCls = env->GetObjectClass(resolver);

  jclass cvCls = env->FindClass("android/content/ContentValues");
  jmethodID cvCtor = env->GetMethodID(cvCls, "<init>", "()V");
  jobject cv = env->NewObject(cvCls, cvCtor);

  jmethodID cvPutStr =
      env->GetMethodID(cvCls, "put", "(Ljava/lang/String;Ljava/lang/String;)V");
  jmethodID cvPutInt = env->GetMethodID(
      cvCls, "put", "(Ljava/lang/String;Ljava/lang/Integer;)V");

  jstring kDisplayName = env->NewStringUTF("_display_name");
  jstring vDisplayName = env->NewStringUTF(filename.c_str());
  env->CallVoidMethod(cv, cvPutStr, kDisplayName, vDisplayName);

  jstring kMime = env->NewStringUTF("mime_type");
  jstring vMime = env->NewStringUTF(mimeType.c_str());
  env->CallVoidMethod(cv, cvPutStr, kMime, vMime);

  jstring kRelPath = env->NewStringUTF("relative_path");
  jstring vRelPath = env->NewStringUTF(relativePath.c_str());
  env->CallVoidMethod(cv, cvPutStr, kRelPath, vRelPath);

  jclass intCls = env->FindClass("java/lang/Integer");
  jmethodID intOf =
      env->GetStaticMethodID(intCls, "valueOf", "(I)Ljava/lang/Integer;");
  jstring kPending = env->NewStringUTF("is_pending");
  jobject vOne = env->CallStaticObjectMethod(intCls, intOf, 1);
  env->CallVoidMethod(cv, cvPutInt, kPending, vOne);

  const char *collectionClass = "android/provider/MediaStore$Images$Media";
  if (mimeType.find("audio") != std::string::npos)
    collectionClass = "android/provider/MediaStore$Audio$Media";
  else if (mimeType.find("video") != std::string::npos)
    collectionClass = "android/provider/MediaStore$Video$Media";

  jclass mediaCls = env->FindClass(collectionClass);
  jfieldID extUriField = env->GetStaticFieldID(mediaCls, "EXTERNAL_CONTENT_URI",
                                               "Landroid/net/Uri;");
  jobject extUri = env->GetStaticObjectField(mediaCls, extUriField);

  jmethodID insert = env->GetMethodID(
      resolverCls, "insert",
      "(Landroid/net/Uri;Landroid/content/ContentValues;)Landroid/net/Uri;");
  jobject fileUri = env->CallObjectMethod(resolver, insert, extUri, cv);

  if (!fileUri) {
    __android_log_print(ANDROID_LOG_ERROR, "FluxCamera",
                        "MediaStore insert failed for %s", filename.c_str());
    return "";
  }

  jmethodID openStream =
      env->GetMethodID(resolverCls, "openOutputStream",
                       "(Landroid/net/Uri;)Ljava/io/OutputStream;");
  jobject outStream = env->CallObjectMethod(resolver, openStream, fileUri);

  if (!outStream) {
    __android_log_print(ANDROID_LOG_ERROR, "FluxCamera",
                        "openOutputStream failed for %s", filename.c_str());
    return "";
  }

  jclass osCls = env->GetObjectClass(outStream);
  jmethodID osWrite = env->GetMethodID(osCls, "write", "([B)V");
  jmethodID osClose = env->GetMethodID(osCls, "close", "()V");

  static constexpr size_t kChunk = 65536;
  size_t offset = 0;
  while (offset < dataSize) {
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
  jobject vZero = env->CallStaticObjectMethod(intCls, intOf, 0);
  env->CallVoidMethod(cv, cvPutInt, kPending2, vZero);

  jmethodID update =
      env->GetMethodID(resolverCls, "update",
                       "(Landroid/net/Uri;Landroid/content/ContentValues;"
                       "Ljava/lang/String;[Ljava/lang/String;)I");
  env->CallIntMethod(resolver, update, fileUri, cv, nullptr, nullptr);

  jclass uriCls = env->GetObjectClass(fileUri);
  jmethodID uriToString =
      env->GetMethodID(uriCls, "toString", "()Ljava/lang/String;");
  jstring uriStr = (jstring)env->CallObjectMethod(fileUri, uriToString);
  const char *uriChars = env->GetStringUTFChars(uriStr, nullptr);
  std::string result(uriChars);
  env->ReleaseStringUTFChars(uriStr, uriChars);

  __android_log_print(ANDROID_LOG_INFO, "FluxCamera",
                      "Saved to MediaStore: %s (%zu bytes)", filename.c_str(),
                      dataSize);

  env->DeleteLocalRef(cv);
  env->DeleteLocalRef(resolver);
  env->DeleteLocalRef(fileUri);
  env->DeleteLocalRef(outStream);
  env->DeleteLocalRef(uriStr);
  env->DeleteLocalRef(kDisplayName);
  env->DeleteLocalRef(vDisplayName);
  env->DeleteLocalRef(kMime);
  env->DeleteLocalRef(vMime);
  env->DeleteLocalRef(kRelPath);
  env->DeleteLocalRef(vRelPath);
  env->DeleteLocalRef(kPending);
  env->DeleteLocalRef(kPending2);
  env->DeleteLocalRef(vOne);
  env->DeleteLocalRef(vZero);

  return result;
}

void *FluxVideo_createSurfaceTexture(GLuint texId) {
  JNIEnv *env = getJNIEnv();
  if (!env)
    return nullptr;

  jclass cls = env->FindClass("android/graphics/SurfaceTexture");
  jmethodID ctor = env->GetMethodID(cls, "<init>", "(I)V");
  jobject st = env->NewObject(cls, ctor, (jint)texId);
  if (!st)
    return nullptr;

  return reinterpret_cast<void *>(env->NewGlobalRef(st));
}

void FluxVideo_updateTexImage(void *surfaceTexture) {
  JNIEnv *env = getJNIEnv();
  if (!env || !surfaceTexture)
    return;

  jobject st = reinterpret_cast<jobject>(surfaceTexture);
  jclass cls = env->GetObjectClass(st);
  jmethodID mid = env->GetMethodID(cls, "updateTexImage", "()V");
  env->CallVoidMethod(st, mid);
}

ANativeWindow *FluxVideo_getNativeWindow(void *surfaceTexture) {
  JNIEnv *env = getJNIEnv();
  if (!env || !surfaceTexture)
    return nullptr;

  jobject st = reinterpret_cast<jobject>(surfaceTexture);
  jclass surfCls = env->FindClass("android/view/Surface");
  jmethodID surfCtor = env->GetMethodID(surfCls, "<init>",
                                        "(Landroid/graphics/SurfaceTexture;)V");
  jobject surface = env->NewObject(surfCls, surfCtor, st);
  if (!surface)
    return nullptr;

  ANativeWindow *window = ANativeWindow_fromSurface(env, surface);
  env->DeleteLocalRef(surface);
  return window;
}

void FluxVideo_destroySurfaceTexture(void *surfaceTexture) {
  JNIEnv *env = getJNIEnv();
  if (!env || !surfaceTexture)
    return;
  env->DeleteGlobalRef(reinterpret_cast<jobject>(surfaceTexture));
}

// ── NVG_initOESBlit forward
// ───────────────────────────────────────────────────
extern void NVG_initOESBlit(int maxW, int maxH);

// ── Input
// ─────────────────────────────────────────────────────────────────────
static int32_t handle_input(android_app *app, AInputEvent *event) {
  if (s_app)
    s_app->getPlatformWindow().handleAndroidEvent(event);
  return 1;
}

static void requestPermissionsFromNative() {
  JNIEnv *env = getJNIEnv();
  if (!env || !s_activity)
    return;

  jobject activityObj = s_activity->clazz;
  jclass activityCls = env->GetObjectClass(activityObj);

  jmethodID checkPerm = env->GetMethodID(activityCls, "checkSelfPermission",
                                         "(Ljava/lang/String;)I");
  jmethodID reqPerms = env->GetMethodID(activityCls, "requestPermissions",
                                        "([Ljava/lang/String;I)V");
  jclass strCls = env->FindClass("java/lang/String");

  std::vector<std::string> needed;

  jstring camPerm = env->NewStringUTF("android.permission.CAMERA");
  jstring micPerm = env->NewStringUTF("android.permission.RECORD_AUDIO");

  if (env->CallIntMethod(activityObj, checkPerm, camPerm) != 0)
    needed.push_back("android.permission.CAMERA");
  if (env->CallIntMethod(activityObj, checkPerm, micPerm) != 0)
    needed.push_back("android.permission.RECORD_AUDIO");

  env->DeleteLocalRef(camPerm);
  env->DeleteLocalRef(micPerm);

  if (needed.empty()) {
    __android_log_print(ANDROID_LOG_INFO, "FluxCamera",
                        "All permissions already granted");
    return;
  }

  jobjectArray permsArr =
      env->NewObjectArray((jsize)needed.size(), strCls, nullptr);
  for (int i = 0; i < (int)needed.size(); i++) {
    jstring p = env->NewStringUTF(needed[i].c_str());
    env->SetObjectArrayElement(permsArr, i, p);
    env->DeleteLocalRef(p);
  }

  env->CallVoidMethod(activityObj, reqPerms, permsArr, 1001);
  env->DeleteLocalRef(permsArr);

  __android_log_print(ANDROID_LOG_INFO, "FluxCamera",
                      "Requested %zu permissions", needed.size());
}

// ── Lifecycle
// ─────────────────────────────────────────────────────────────────
static void handle_cmd(android_app *app, int32_t cmd) {
  switch (cmd) {
case APP_CMD_INIT_WINDOW: {
    if (!app->window) break;

    s_jvm      = app->activity->vm;
    s_activity = app->activity;

    FluxFilePickerAndroid::init(getJNIEnv(), app->activity);
    requestPermissionsFromNative();
    FluxJNI::init(app);
    FluxAndroid_setAssetManager(app->activity->assetManager);

    extractCACert(app);
    if (!s_cacertPath.empty())
        FluxHttp::setCABundle(s_cacertPath);

    AConfiguration* config = AConfiguration_new();
    AConfiguration_fromAssetManager(config, app->activity->assetManager);
    float dpiScale = AConfiguration_getDensity(config) / 160.0f;
    AConfiguration_delete(config);
    FluxAndroid_setDpiScale(dpiScale);

    if (!s_app) {
        // ── First launch only ─────────────────────────────────────────
        s_app = new FluxUI(app);
        s_app->build([&]() { return createApp(s_app); });
        auto* cfg = static_cast<FluxAppWidget*>(FluxAppWidget::getInstance());
        s_app->createWindow(cfg->title, cfg->windowWidth, cfg->windowHeight);

        if (s_vg) { nvgDeleteGLES2(s_vg); s_vg = nullptr; }
        s_vg = nvgCreateGLES2(NVG_ANTIALIAS | NVG_STENCIL_STROKES);
        nvgCreateFont(s_vg, "default", "/system/fonts/Roboto-Regular.ttf");
        FluxAndroid_setVG(s_vg);

        auto& win    = s_app->getPlatformWindow();
        s_eglDisplay = win.getEGLDisplay();
        s_eglContext = win.getEGLContext();
        s_eglSurface = win.getEGLSurface();

        NVG_initOESBlit(1920, 1080);
        s_app->getFontCache().clear();
        s_app->rebuild();  // ← only here

    } else {
        // ── Returning from file picker — surface reconnect only ────────
        s_app->getPlatformWindow().reinitSurface(app->window);

        if (s_vg) { nvgDeleteGLES2(s_vg); s_vg = nullptr; }
        s_vg = nvgCreateGLES2(NVG_ANTIALIAS | NVG_STENCIL_STROKES);
        nvgCreateFont(s_vg, "default", "/system/fonts/Roboto-Regular.ttf");
        FluxAndroid_setVG(s_vg);

        auto& win    = s_app->getPlatformWindow();
        s_eglDisplay = win.getEGLDisplay();
        s_eglContext = win.getEGLContext();
        s_eglSurface = win.getEGLSurface();

        NVG_initOESBlit(1920, 1080);
        s_app->getFontCache().clear();
        // NO rebuild() here — widget tree stays alive, State<> stays valid
        s_app->getRoot()->markNeedsLayout();
    }
    break;
}

  case APP_CMD_TERM_WINDOW:
    FluxCamera::get().close();
    FluxMic::get().cancel();
    FluxVideo::get().close();

    if (s_vg) {
      nvgDeleteGLES2(s_vg);
      s_vg = nullptr;
    }
    if (s_app)
      s_app->getPlatformWindow().destroySurface();

    s_eglDisplay = EGL_NO_DISPLAY;
    s_eglContext = EGL_NO_CONTEXT;
    s_eglSurface = EGL_NO_SURFACE;
    break;

  case APP_CMD_WINDOW_RESIZED:
  case APP_CMD_CONFIG_CHANGED:
    if (s_app) {
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

// ── Main loop
// ─────────────────────────────────────────────────────────────────
void android_main(android_app *app) {
  app->onAppCmd = handle_cmd;
  app->onInputEvent = handle_input;

  while (true) {
    int events;
    android_poll_source *source;
    while (ALooper_pollOnce(0, nullptr, &events,
                            reinterpret_cast<void **>(&source)) >= 0) {
      if (source)
        source->process(app, source);
      if (app->destroyRequested)
        return;
    }

    if (!s_app || !s_vg)
      continue;

    auto &win = s_app->getPlatformWindow();
    win.pollTimers();

    FluxFilePickerAndroid::drainPendingCallbacks();


    auto sz = win.getClientSize();
    glClearColor(0.98f, 0.98f, 0.98f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

    float dpi = FluxAndroid_getDpiScale();
    nvgBeginFrame(s_vg, sz.width * dpi, sz.height * dpi, dpi);
    FluxAndroid_setVG(s_vg);
    nvgScale(s_vg, dpi, dpi);

    GraphicsContext gc(sz.width, sz.height);
    Renderer::renderWidget(gc, s_app->getRoot().get(), s_app->getFontCache());

    nvgEndFrame(s_vg);
    eglSwapBuffers(win.getEGLDisplay(), win.getEGLSurface());
  }
}

#endif