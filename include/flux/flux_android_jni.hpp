// flux_android_jni.hpp
#pragma once
#ifdef __ANDROID__

#include <android_native_app_glue.h>
#include <jni.h>
#include <string>
#include <functional>
#include <unordered_map>
#include <android/log.h>

#define FLUX_LOGI(...) __android_log_print(ANDROID_LOG_INFO,  "FluxUI", __VA_ARGS__)
#define FLUX_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "FluxUI", __VA_ARGS__)

// ============================================================================
// FluxJNI — thin RAII wrapper around JNIEnv attachment + helper methods
// ============================================================================

class FluxJNI {
public:
    // ── Call once from APP_CMD_INIT_WINDOW ───────────────────────────────
    static void init(android_app* app) {
        s_app = app;
        s_vm  = app->activity->vm;
    }

    // ── Attach current thread and get env ───────────────────────────────
    // Returns nullptr if attach fails.
    static JNIEnv* attach() {
        if (!s_vm) return nullptr;
        JNIEnv* env = nullptr;
        jint res = s_vm->AttachCurrentThread(&env, nullptr);
        return (res == JNI_OK) ? env : nullptr;
    }

    static void detach() {
        if (s_vm) s_vm->DetachCurrentThread();
    }

    // ── RAII guard — attach on construction, detach on destruction ───────
    struct Env {
        JNIEnv* env = nullptr;
        bool    attached = false;

        Env() {
            if (!s_vm) return;
            jint res = s_vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
            if (res == JNI_EDETACHED) {
                res = s_vm->AttachCurrentThread(&env, nullptr);
                attached = (res == JNI_OK);
            }
        }
        ~Env() { if (attached && s_vm) s_vm->DetachCurrentThread(); }

        JNIEnv* operator->() { return env; }
        explicit operator bool() const { return env != nullptr; }
    };

    // ── Get the activity instance ────────────────────────────────────────
    static jobject getActivity() {
        return s_app ? s_app->activity->clazz : nullptr;
    }

    static JavaVM* getVM() { return s_vm; }
    static android_app* getApp() { return s_app; }

    // ── Permission helpers ───────────────────────────────────────────────

    // Check if a permission is already granted.
    // permName e.g. "android.permission.CAMERA"
    static bool hasPermission(const std::string& permName) {
        Env jni;
        if (!jni) return false;

        jobject activity = getActivity();
        jclass  actClass = jni->GetObjectClass(activity);

        jmethodID checkSelf = jni->GetMethodID(actClass,
            "checkSelfPermission", "(Ljava/lang/String;)I");
        if (!checkSelf) {
            jni->ExceptionClear();
            return false;
        }

        jstring jperm = jni->NewStringUTF(permName.c_str());
        jint    result = jni->CallIntMethod(activity, checkSelf, jperm);
        jni->DeleteLocalRef(jperm);
        jni->DeleteLocalRef(actClass);

        // PERMISSION_GRANTED = 0
        return result == 0;
    }

    // Request a permission — result comes back via onPermissionResult callback.
    // requestCode can be any int you use to identify the request.
    static void requestPermission(const std::string& permName,
                                  int requestCode,
                                  std::function<void(bool granted)> callback) {
        // Store callback keyed by requestCode
        s_callbacks[requestCode] = callback;

        Env jni;
        if (!jni) { callback(false); return; }

        jobject activity = getActivity();
        jclass  actClass = jni->GetObjectClass(activity);

        jmethodID reqPerm = jni->GetMethodID(actClass,
            "requestPermissions", "([Ljava/lang/String;I)V");

        if (!reqPerm) {
            // API < 23 — permissions are granted at install time
            jni->ExceptionClear();
            jni->DeleteLocalRef(actClass);
            callback(true);
            return;
        }

        jstring jperm = jni->NewStringUTF(permName.c_str());
        jclass  strClass = jni->FindClass("java/lang/String");
        jobjectArray arr = jni->NewObjectArray(1, strClass, jperm);

        jni->CallVoidMethod(activity, reqPerm, arr,
                            static_cast<jint>(requestCode));

        jni->DeleteLocalRef(jperm);
        jni->DeleteLocalRef(arr);
        jni->DeleteLocalRef(strClass);
        jni->DeleteLocalRef(actClass);
    }

    // Request multiple permissions at once
    static void requestPermissions(const std::vector<std::string>& perms,
                                   int requestCode,
                                   std::function<void(bool allGranted)> callback) {
        s_callbacks[requestCode] = [callback, count = perms.size()](bool granted) {
            // Simplified: callback fires once with overall result
            callback(granted);
        };

        Env jni;
        if (!jni) { callback(false); return; }

        jobject activity = getActivity();
        jclass  actClass = jni->GetObjectClass(activity);
        jmethodID reqPerm = jni->GetMethodID(actClass,
            "requestPermissions", "([Ljava/lang/String;I)V");

        if (!reqPerm) {
            jni->ExceptionClear();
            jni->DeleteLocalRef(actClass);
            callback(true);
            return;
        }

        jclass strClass = jni->FindClass("java/lang/String");
        jobjectArray arr = jni->NewObjectArray(
            static_cast<jsize>(perms.size()), strClass, nullptr);

        for (int i = 0; i < (int)perms.size(); i++) {
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
    static void dispatchPermissionResult(int requestCode, bool granted) {
        auto it = s_callbacks.find(requestCode);
        if (it != s_callbacks.end()) {
            it->second(granted);
            s_callbacks.erase(it);
        }
    }

    // ── Soft keyboard ────────────────────────────────────────────────────
    static void showKeyboard() {
        Env jni;
        if (!jni) return;
        jobject activity = getActivity();
        jclass  actClass = jni->GetObjectClass(activity);

        // Get InputMethodManager via getSystemService
        jmethodID getSvc = jni->GetMethodID(actClass,
            "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;");
        jstring svcName = jni->NewStringUTF("input_method");
        jobject imm = jni->CallObjectMethod(activity, getSvc, svcName);
        jni->DeleteLocalRef(svcName);

        // Get window's DecorView
        jmethodID getWin = jni->GetMethodID(actClass,
            "getWindow", "()Landroid/view/Window;");
        jobject   win    = jni->CallObjectMethod(activity, getWin);
        jclass    winCls = jni->GetObjectClass(win);
        jmethodID getDV  = jni->GetMethodID(winCls,
            "getDecorView", "()Landroid/view/View;");
        jobject   dv     = jni->CallObjectMethod(win, getDV);

        jclass    immCls   = jni->GetObjectClass(imm);
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

    static void hideKeyboard() {
        Env jni;
        if (!jni) return;
        jobject activity = getActivity();
        jclass  actClass = jni->GetObjectClass(activity);

        jmethodID getSvc = jni->GetMethodID(actClass,
            "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;");
        jstring svcName = jni->NewStringUTF("input_method");
        jobject imm     = jni->CallObjectMethod(activity, getSvc, svcName);
        jni->DeleteLocalRef(svcName);

        jmethodID getWin = jni->GetMethodID(actClass,
            "getWindow", "()Landroid/view/Window;");
        jobject   win    = jni->CallObjectMethod(activity, getWin);
        jclass    winCls = jni->GetObjectClass(win);
        jmethodID getDV  = jni->GetMethodID(winCls,
            "getDecorView", "()Landroid/view/View;");
        jobject   dv     = jni->CallObjectMethod(win, getDV);

        jclass    immCls   = jni->GetObjectClass(imm);
        jmethodID hideSoft = jni->GetMethodID(immCls,
            "hideSoftInputFromWindow",
            "(Landroid/os/IBinder;I)Z");

        jmethodID getToken = jni->GetMethodID(
            jni->GetObjectClass(dv), "getWindowToken",
            "()Landroid/os/IBinder;");
        jobject token = jni->CallObjectMethod(dv, getToken);

        jni->CallBooleanMethod(imm, hideSoft, token, 0);

        jni->DeleteLocalRef(actClass);
        jni->DeleteLocalRef(imm);
        jni->DeleteLocalRef(immCls);
        jni->DeleteLocalRef(win);
        jni->DeleteLocalRef(winCls);
        jni->DeleteLocalRef(dv);
        jni->DeleteLocalRef(token);
    }

private:
    static android_app* s_app;
    static JavaVM*      s_vm;
    static std::unordered_map<int, std::function<void(bool)>> s_callbacks;
};

// ── Static member definitions (in a .cpp that includes this header) ──────────
// Put these in flux_android_jni.cpp:
//   android_app* FluxJNI::s_app = nullptr;
//   JavaVM*      FluxJNI::s_vm  = nullptr;
//   std::unordered_map<int, std::function<void(bool)>> FluxJNI::s_callbacks;

// ── Permission request codes — add yours here ────────────────────────────────
namespace PermissionCode {
    constexpr int Camera      = 1001;
    constexpr int Microphone  = 1002;
    constexpr int Storage     = 1003;
    constexpr int CameraAudio = 1004;  // both at once
}

namespace Permission {
    constexpr const char* Camera      = "android.permission.CAMERA";
    constexpr const char* Microphone  = "android.permission.RECORD_AUDIO";
    constexpr const char* ReadImages  = "android.permission.READ_MEDIA_IMAGES";
    constexpr const char* ReadAudio   = "android.permission.READ_MEDIA_AUDIO";
    constexpr const char* ReadVideo   = "android.permission.READ_MEDIA_VIDEO";
}

#endif // __ANDROID__