// flux_android_jni.hpp
#pragma once
#ifdef __ANDROID__

#include <android_native_app_glue.h>
#include <jni.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <GLES2/gl2.h>
#include <string>
#include <functional>
#include <unordered_map>
#include <mutex>
#include <vector>
#include <android/log.h>

#define FLUX_LOGI(...) __android_log_print(ANDROID_LOG_INFO, "FluxUI", __VA_ARGS__)
#define FLUX_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "FluxUI", __VA_ARGS__)


// ── Permission request codes — add yours here ────────────────────────────────
namespace PermissionCode
{
    constexpr int Camera = 1001;
    constexpr int Microphone = 1002;
    constexpr int Storage = 1003;
    constexpr int CameraAudio = 1004; // both at once
    constexpr int NoCallbackSentinel = 9999;
}

// Global ANativeActivity* — camera_widget_android.cpp reads this as a bare
// extern global (its old native-lib.cpp pattern). Defined in
// flux_android_jni.cpp and kept in sync by FluxJNI::init().
extern ANativeActivity *s_activity;


// ============================================================================
// FluxJNI — thin RAII wrapper around JNIEnv attachment + helper methods
// ============================================================================

class FluxJNI
{
public:
    // ── Call once from APP_CMD_INIT_WINDOW ───────────────────────────────
    static void init(android_app *app)
    {
        s_app = app;
        s_vm = app->activity->vm;
        s_activity = app->activity;
    }

    // ── Attach current thread and get env ───────────────────────────────
    // Returns nullptr if attach fails.
    static JNIEnv *attach()
    {
        if (!s_vm)
            return nullptr;
        JNIEnv *env = nullptr;
        jint res = s_vm->AttachCurrentThread(&env, nullptr);
        return (res == JNI_OK) ? env : nullptr;
    }

    static void detach()
    {
        if (s_vm)
            s_vm->DetachCurrentThread();
    }

    // ── RAII guard — attach on construction, detach on destruction ───────
    struct Env
    {
        JNIEnv *env = nullptr;
        bool attached = false;

        Env()
        {
            if (!s_vm)
                return;
            jint res = s_vm->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_6);
            if (res == JNI_EDETACHED)
            {
                res = s_vm->AttachCurrentThread(&env, nullptr);
                attached = (res == JNI_OK);
            }
        }
        ~Env()
        {
            if (attached && s_vm)
                s_vm->DetachCurrentThread();
        }

        JNIEnv *operator->() { return env; }
        explicit operator bool() const { return env != nullptr; }
    };

    // ── Get the activity instance ────────────────────────────────────────
    static jobject getActivity()
    {
        return s_app ? s_app->activity->clazz : nullptr;
    }

    static JavaVM *getVM() { return s_vm; }
    static android_app *getApp() { return s_app; }

    // ── Filesystem / MediaStore ──────────────────────────────────────────
    // Moved from flux_android_main.cpp — was hand-rolling its own
    // GetEnv/AttachCurrentThread dance via a free-standing getJNIEnv().
    // Now goes through the same Env RAII guard as everything else here.
    static std::string getFilesDir();

    static std::string saveToMediaStore(const std::string &filename,
                                        const std::string &mimeType,
                                        const std::string &relativePath,
                                        const uint8_t *data, size_t dataSize);

    // ── SurfaceTexture (video) ───────────────────────────────────────────
    // Moved from FluxVideo_createSurfaceTexture/updateTexImage/
    // getNativeWindow/destroySurfaceTexture in flux_android_main.cpp.
    // s_videoSurface tracked here instead of as a bare file-static in the
    // old TU, so it lives next to the JNI state it's paired with.
    static void *createSurfaceTexture(GLuint texId);
    static void updateTexImage(void *surfaceTexture);
    static ANativeWindow *getNativeWindowFromSurfaceTexture(void *surfaceTexture);
    static void destroySurfaceTexture(void *surfaceTexture);

    // ── Permission helpers ───────────────────────────────────────────────

    // Check if a permission is already granted.
    // permName e.g. "android.permission.CAMERA"
    static bool hasPermission(const std::string &permName)
    {
        Env jni;
        if (!jni)
            return false;

        jobject activity = getActivity();
        jclass actClass = jni->GetObjectClass(activity);

        jmethodID checkSelf = jni->GetMethodID(actClass,
                                               "checkSelfPermission", "(Ljava/lang/String;)I");
        if (!checkSelf)
        {
            jni->ExceptionClear();
            return false;
        }

        jstring jperm = jni->NewStringUTF(permName.c_str());
        jint result = jni->CallIntMethod(activity, checkSelf, jperm);
        jni->DeleteLocalRef(jperm);
        jni->DeleteLocalRef(actClass);

        // PERMISSION_GRANTED = 0
        return result == 0;
    }

    // Request a permission — result comes back via onPermissionResult callback.
    // requestCode can be any int you use to identify the request.
    static void requestPermission(const std::string &permName,
                                  int requestCode,
                                  std::function<void(bool granted)> callback)
    {

        Env jni;
        if (!jni)
        {
            callback(false);
            return;
        }
        {
            std::lock_guard<std::mutex> lock(s_callbackMutex);
            s_callbacks[requestCode] = callback;
        }

        jobject activity = getActivity();
        jclass actClass = jni->GetObjectClass(activity);

        jmethodID reqPerm = jni->GetMethodID(actClass,
                                             "requestPermissions", "([Ljava/lang/String;I)V");

        if (!reqPerm)
        {
            // API < 23 — permissions are granted at install time
            jni->ExceptionClear();
            jni->DeleteLocalRef(actClass);
            callback(true);
            return;
        }

        jstring jperm = jni->NewStringUTF(permName.c_str());
        jclass strClass = jni->FindClass("java/lang/String");
        jobjectArray arr = jni->NewObjectArray(1, strClass, jperm);

        jni->CallVoidMethod(activity, reqPerm, arr,
                            static_cast<jint>(requestCode));

        jni->DeleteLocalRef(jperm);
        jni->DeleteLocalRef(arr);
        jni->DeleteLocalRef(strClass);
        jni->DeleteLocalRef(actClass);
    }

    // Fire-and-forget single permission request — no result callback.
    // Replaces the old free function FluxAndroid_requestPermission() from
    // flux_android_main.cpp. Kept only for call sites that genuinely don't
    // care about the result; prefer requestPermission(name, code, cb).
    static void requestPermissionNoCallback(const std::string &permName)
    {
        if (hasPermission(permName))
            return;

        Env jni;
        if (!jni)
            return;

        jobject activity = getActivity();
        jclass actClass = jni->GetObjectClass(activity);
        jmethodID reqPerm = jni->GetMethodID(actClass,
                                             "requestPermissions", "([Ljava/lang/String;I)V");
        if (!reqPerm)
        {
            jni->ExceptionClear();
            jni->DeleteLocalRef(actClass);
            return;
        }

        jstring jperm = jni->NewStringUTF(permName.c_str());
        jclass strClass = jni->FindClass("java/lang/String");
        jobjectArray arr = jni->NewObjectArray(1, strClass, jperm);
        jni->CallVoidMethod(activity, reqPerm, arr, PermissionCode::NoCallbackSentinel);

        jni->DeleteLocalRef(jperm);
        jni->DeleteLocalRef(arr);
        jni->DeleteLocalRef(strClass);
        jni->DeleteLocalRef(actClass);
    }

    // Request multiple permissions at once. Unlike the single-permission
    // path, Android reports one grant result PER permission, so the
    // callback here gets the full vector rather than one collapsed bool —
    // the old version silently discarded per-permission results.
    static void requestPermissions(const std::vector<std::string> &perms,
                                   int requestCode,
                                   std::function<void(const std::vector<bool> &results)> callback)
    {
        {
            std::lock_guard<std::mutex> lock(s_callbackMutex);
            s_multiCallbacks[requestCode] = callback;
        }

        Env jni;
        if (!jni)
        {
            callback(std::vector<bool>(perms.size(), false));
            return;
        }

        jobject activity = getActivity();
        jclass actClass = jni->GetObjectClass(activity);
        jmethodID reqPerm = jni->GetMethodID(actClass,
                                             "requestPermissions", "([Ljava/lang/String;I)V");

        if (!reqPerm)
        {
            jni->ExceptionClear();
            jni->DeleteLocalRef(actClass);
            callback(std::vector<bool>(perms.size(), true));
            return;
        }

        jclass strClass = jni->FindClass("java/lang/String");
        jobjectArray arr = jni->NewObjectArray(
            static_cast<jsize>(perms.size()), strClass, nullptr);

        for (int i = 0; i < (int)perms.size(); i++)
        {
            jstring js = jni->NewStringUTF(perms[i].c_str());
            jni->SetObjectArrayElement(arr, i, js);
            jni->DeleteLocalRef(js);
        }

        jni->CallVoidMethod(activity, reqPerm, arr,
                            static_cast<jint>(requestCode));

        jni->DeleteLocalRef(arr);
        jni->DeleteLocalRef(strClass);
        jni->DeleteLocalRef(actClass);
    }

    // ── Called from onRequestPermissionsResult (see native-lib.cpp) ─────
    static void dispatchPermissionResult(int requestCode, bool granted)
    {
        std::function<void(bool)> cb;
        {
            std::lock_guard<std::mutex> lock(s_callbackMutex);
            auto it = s_callbacks.find(requestCode);
            if (it == s_callbacks.end())
                return;
            cb = it->second;
            s_callbacks.erase(it);
        }
        cb(granted); // invoke outside the lock — callback may re-enter FluxJNI
    }

    // Multi-permission counterpart — call with the full grantResults[]
    // array converted to bools (PERMISSION_GRANTED == 0).
    static void dispatchPermissionResults(int requestCode, const std::vector<bool> &results)
    {
        std::function<void(const std::vector<bool> &)> cb;
        {
            std::lock_guard<std::mutex> lock(s_callbackMutex);
            auto it = s_multiCallbacks.find(requestCode);
            if (it == s_multiCallbacks.end())
                return;
            cb = it->second;
            s_multiCallbacks.erase(it);
        }
        cb(results);
    }

    // ── Soft keyboard ────────────────────────────────────────────────────
    static void showKeyboard()
    {
        Env jni;
        if (!jni)
            return;
        jobject activity = getActivity();
        jclass actClass = jni->GetObjectClass(activity);

        // Get InputMethodManager via getSystemService
        jmethodID getSvc = jni->GetMethodID(actClass,
                                            "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;");
        jstring svcName = jni->NewStringUTF("input_method");
        jobject imm = jni->CallObjectMethod(activity, getSvc, svcName);
        jni->DeleteLocalRef(svcName);

        // Get window's DecorView
        jmethodID getWin = jni->GetMethodID(actClass,
                                            "getWindow", "()Landroid/view/Window;");
        jobject win = jni->CallObjectMethod(activity, getWin);
        jclass winCls = jni->GetObjectClass(win);
        jmethodID getDV = jni->GetMethodID(winCls,
                                           "getDecorView", "()Landroid/view/View;");
        jobject dv = jni->CallObjectMethod(win, getDV);

        jclass immCls = jni->GetObjectClass(imm);
        jmethodID showSoft = jni->GetMethodID(immCls,
                                              "showSoftInput", "(Landroid/view/View;I)Z");
        jni->CallBooleanMethod(imm, showSoft, dv, 0);

        jni->DeleteLocalRef(actClass);
        jni->DeleteLocalRef(imm);
        jni->DeleteLocalRef(immCls);
        jni->DeleteLocalRef(win);
        jni->DeleteLocalRef(winCls);
        jni->DeleteLocalRef(dv);
    }

    static void hideKeyboard()
    {
        Env jni;
        if (!jni)
            return;
        jobject activity = getActivity();
        jclass actClass = jni->GetObjectClass(activity);

        jmethodID getSvc = jni->GetMethodID(actClass,
                                            "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;");
        jstring svcName = jni->NewStringUTF("input_method");
        jobject imm = jni->CallObjectMethod(activity, getSvc, svcName);
        jni->DeleteLocalRef(svcName);

        jmethodID getWin = jni->GetMethodID(actClass,
                                            "getWindow", "()Landroid/view/Window;");
        jobject win = jni->CallObjectMethod(activity, getWin);
        jclass winCls = jni->GetObjectClass(win);
        jmethodID getDV = jni->GetMethodID(winCls,
                                           "getDecorView", "()Landroid/view/View;");
        jobject dv = jni->CallObjectMethod(win, getDV);

        jclass immCls = jni->GetObjectClass(imm);
        jmethodID hideSoft = jni->GetMethodID(immCls,
                                              "hideSoftInputFromWindow",
                                              "(Landroid/os/IBinder;I)Z");

        jclass dvCls = jni->GetObjectClass(dv);
        jmethodID getToken = jni->GetMethodID(dvCls, "getWindowToken",
                                              "()Landroid/os/IBinder;");
        jobject token = jni->CallObjectMethod(dv, getToken);

        jni->CallBooleanMethod(imm, hideSoft, token, 0);

        jni->DeleteLocalRef(actClass);
        jni->DeleteLocalRef(imm);
        jni->DeleteLocalRef(immCls);
        jni->DeleteLocalRef(win);
        jni->DeleteLocalRef(winCls);
        jni->DeleteLocalRef(dvCls);
        jni->DeleteLocalRef(dv);
        jni->DeleteLocalRef(token);
    }

private:
    static android_app *s_app;
    static JavaVM *s_vm;
    static std::unordered_map<int, std::function<void(bool)>> s_callbacks;
    static std::unordered_map<int, std::function<void(const std::vector<bool> &)>> s_multiCallbacks;
    static std::mutex s_callbackMutex;
    static jobject s_videoSurface; // global ref, owned by getNativeWindowFromSurfaceTexture
};

// ── Static member definitions (in a .cpp that includes this header) ──────────
// Put these in flux_android_jni.cpp:
//   android_app* FluxJNI::s_app = nullptr;
//   JavaVM*      FluxJNI::s_vm  = nullptr;
//   std::unordered_map<int, std::function<void(bool)>> FluxJNI::s_callbacks;
//   std::unordered_map<int, std::function<void(const std::vector<bool>&)>> FluxJNI::s_multiCallbacks;
//   std::mutex FluxJNI::s_callbackMutex;
//   jobject FluxJNI::s_videoSurface = nullptr;



namespace Permission
{
    constexpr const char *Camera = "android.permission.CAMERA";
    constexpr const char *Microphone = "android.permission.RECORD_AUDIO";
    constexpr const char *ReadImages = "android.permission.READ_MEDIA_IMAGES";
    constexpr const char *ReadAudio = "android.permission.READ_MEDIA_AUDIO";
    constexpr const char *ReadVideo = "android.permission.READ_MEDIA_VIDEO";
}


#endif // __ANDROID__