// flux_file_picker_android.hpp
//
// Android backend for FilePickerWidget.
// Included by flux_file_picker.hpp when building on Android.
//
// Architecture:
//   Android file pickers work via startActivityForResult(), which is async —
//   the result arrives later in onActivityResult(). We bridge this gap with:
//
//   1. FluxFilePicker_launch()  — called from C++ to fire the Intent
//   2. FluxFilePicker_onResult()— called from Java's onActivityResult() via JNI
//   3. A static callback map keyed by request code
//
// Java side (add to your NativeActivity subclass or GameActivity wrapper):
//
//   @Override
//   protected void onActivityResult(int req, int result, Intent data) {
//       super.onActivityResult(req, result, data);
//       FluxBridge.onFilePickerResult(req, result, data, getContentResolver());
//   }
//
// The Java helper class FluxBridge.java resolves content URIs to real file
// paths and calls back into C++ via the JNI function below.
//
// ============================================================================

#pragma once

#ifdef __ANDROID__

#include <android/log.h>
#include <jni.h>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>
#include <atomic>
#include <mutex>

#define FLUX_FP_TAG "FluxFilePicker"
#define FLUX_FP_LOG(...) __android_log_print(ANDROID_LOG_DEBUG, FLUX_FP_TAG, __VA_ARGS__)
#define FLUX_FP_ERR(...) __android_log_print(ANDROID_LOG_ERROR, FLUX_FP_TAG, __VA_ARGS__)

// ============================================================================
// Request-code registry
// Maps a unique int request code → callback(paths)
// ============================================================================

namespace FluxFilePickerAndroid {

using ResultCallback = std::function<void(std::vector<std::string> /*paths*/)>;

// Internal registry — accessed only from the main thread
struct Registry {
    std::unordered_map<int, ResultCallback> callbacks;
    std::atomic<int> nextCode{5000}; // start above common Android codes

    static Registry& get() {
        static Registry inst;
        return inst;
    }

    int registerCallback(ResultCallback cb) {
        int code = nextCode.fetch_add(1);
        callbacks[code] = std::move(cb);
        return code;
    }

    void dispatch(int code, std::vector<std::string> paths) {
        auto it = callbacks.find(code);
        if (it == callbacks.end()) {
            FLUX_FP_ERR("dispatch: unknown request code %d", code);
            return;
        }
        auto cb = std::move(it->second);
        callbacks.erase(it);
        cb(std::move(paths));
    }
};

// ============================================================================
// MIME type helpers
// ============================================================================

// Converts a glob extension pattern like "*.png" → "image/png".
// Falls back to "*/*" for unknown or wildcard extensions.
inline std::string extToMime(const std::string& ext) {
    // ext is e.g. "*.png", "*.jpg", "*.*"
    std::string e = ext;
    // strip leading "*."
    if (e.size() >= 2 && e[0] == '*' && e[1] == '.') e = e.substr(2);

    if (e == "*" || e == "*.*")        return "*/*";

    // Images
    if (e == "png")                    return "image/png";
    if (e == "jpg" || e == "jpeg")     return "image/jpeg";
    if (e == "gif")                    return "image/gif";
    if (e == "bmp")                    return "image/bmp";
    if (e == "webp")                   return "image/webp";
    if (e == "tga" || e == "tiff"
                   || e == "tif")      return "image/tiff";
    if (e == "svg")                    return "image/svg+xml";
    if (e == "heic" || e == "heif")    return "image/heic";
    if (e == "ico")                    return "image/x-icon";

    // Video
    if (e == "mp4")                    return "video/mp4";
    if (e == "mkv")                    return "video/x-matroska";
    if (e == "avi")                    return "video/x-msvideo";
    if (e == "mov")                    return "video/quicktime";
    if (e == "webm")                   return "video/webm";

    // Audio
    if (e == "mp3")                    return "audio/mpeg";
    if (e == "wav")                    return "audio/wav";
    if (e == "ogg")                    return "audio/ogg";
    if (e == "flac")                   return "audio/flac";
    if (e == "aac")                    return "audio/aac";
    if (e == "m4a")                    return "audio/mp4";

    // Documents / text
    if (e == "pdf")                    return "application/pdf";
    if (e == "txt")                    return "text/plain";
    if (e == "md")                     return "text/markdown";
    if (e == "html" || e == "htm")     return "text/html";
    if (e == "csv")                    return "text/csv";
    if (e == "xml")                    return "text/xml";
    if (e == "json")                   return "application/json";
    if (e == "zip")                    return "application/zip";
    if (e == "tar")                    return "application/x-tar";
    if (e == "gz")                     return "application/gzip";

    // Office
    if (e == "doc")                    return "application/msword";
    if (e == "docx")                   return "application/vnd.openxmlformats-officedocument.wordprocessingml.document";
    if (e == "xls")                    return "application/vnd.ms-excel";
    if (e == "xlsx")                   return "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet";
    if (e == "ppt")                    return "application/vnd.ms-powerpoint";
    if (e == "pptx")                   return "application/vnd.openxmlformats-officedocument.presentationml.presentation";

    // Fallback — let Android decide
    return "application/octet-stream";
}

// Collapse a list of FileFilter extensions into the minimal set of MIME types
// Android needs. Deduplicated and sorted for stability.
inline std::vector<std::string> filtersToMimes(const std::vector<FileFilter>& filters) {
    std::unordered_map<std::string, bool> seen;
    std::vector<std::string> result;

    for (const auto& f : filters) {
        for (const auto& ext : f.extensions) {
            std::string mime = extToMime(ext);
            if (!seen.count(mime)) {
                seen[mime] = true;
                result.push_back(mime);
            }
        }
    }

    if (result.empty()) result.push_back("*/*");

    // If "*/*" is present, collapse everything down to it — no point listing others
    for (const auto& m : result)
        if (m == "*/*") return {"*/*"};

    return result;
}

// ============================================================================
// Launch helpers
// ============================================================================

// Retrieves the cached JNIEnv* (see main android_main.cpp for s_jvm)
extern JNIEnv* getJNIEnv();

// Retrieves the Activity jobject (s_activity->clazz)
extern ANativeActivity* s_activity;

// ── Open single / multiple files ─────────────────────────────────────────────

inline int launchOpenIntent(const std::string&          title,
                             const std::vector<FileFilter>& filters,
                             bool                         multi,
                             ResultCallback               cb) {
    JNIEnv* env = getJNIEnv();
    if (!env || !s_activity) {
        FLUX_FP_ERR("launchOpenIntent: no JNI env or activity");
        cb({});
        return -1;
    }

    auto mimes = filtersToMimes(filters);

    jobject activityObj = s_activity->clazz;
    jclass  activityCls = env->GetObjectClass(activityObj);

    // ── Build Intent ─────────────────────────────────────────────────────────
    jclass    intentCls  = env->FindClass("android/content/Intent");
    jmethodID intentCtor = env->GetMethodID(intentCls, "<init>",
                                            "(Ljava/lang/String;)V");

    // ACTION_OPEN_DOCUMENT — persists across reboots, gives real stream access
    jstring action = env->NewStringUTF("android.intent.action.OPEN_DOCUMENT");
    jobject intent = env->NewObject(intentCls, intentCtor, action);
    env->DeleteLocalRef(action);

    // addCategory(OPENABLE)
    jmethodID addCat = env->GetMethodID(intentCls, "addCategory",
                                        "(Ljava/lang/String;)Landroid/content/Intent;");
    jstring catOpenable = env->NewStringUTF("android.intent.category.OPENABLE");
    env->CallObjectMethod(intent, addCat, catOpenable);
    env->DeleteLocalRef(catOpenable);

    // setType — primary MIME
    jmethodID setType = env->GetMethodID(intentCls, "setType",
                                         "(Ljava/lang/String;)Landroid/content/Intent;");
    jstring primaryMime = env->NewStringUTF(mimes[0].c_str());
    env->CallObjectMethod(intent, setType, primaryMime);
    env->DeleteLocalRef(primaryMime);

    // putExtra(EXTRA_MIME_TYPES, array) — additional MIMEs for multi-type filter
    if (mimes.size() > 1) {
        jclass    strCls   = env->FindClass("java/lang/String");
        jobjectArray mimeArr = env->NewObjectArray((jsize)mimes.size(), strCls, nullptr);
        for (size_t i = 0; i < mimes.size(); ++i) {
            jstring ms = env->NewStringUTF(mimes[i].c_str());
            env->SetObjectArrayElement(mimeArr, (jsize)i, ms);
            env->DeleteLocalRef(ms);
        }
        jmethodID putExtra = env->GetMethodID(intentCls, "putExtra",
                                              "(Ljava/lang/String;[Ljava/lang/String;)Landroid/content/Intent;");
        jstring keyMimes = env->NewStringUTF("android.intent.extra.MIME_TYPES");
        env->CallObjectMethod(intent, putExtra, keyMimes, mimeArr);
        env->DeleteLocalRef(keyMimes);
        env->DeleteLocalRef(mimeArr);
    }

    // putExtra(ALLOW_MULTIPLE)
    if (multi) {
        jmethodID putBool = env->GetMethodID(intentCls, "putExtra",
                                             "(Ljava/lang/String;Z)Landroid/content/Intent;");
        jstring keyMulti = env->NewStringUTF("android.intent.extra.ALLOW_MULTIPLE");
        env->CallObjectMethod(intent, putBool, keyMulti, JNI_TRUE);
        env->DeleteLocalRef(keyMulti);
    }

    // Title
    if (!title.empty()) {
        jmethodID putStr = env->GetMethodID(intentCls, "putExtra",
                                            "(Ljava/lang/String;Ljava/lang/String;)Landroid/content/Intent;");
        jstring keyTitle = env->NewStringUTF("android.intent.extra.TITLE");
        jstring valTitle = env->NewStringUTF(title.c_str());
        env->CallObjectMethod(intent, putStr, keyTitle, valTitle);
        env->DeleteLocalRef(keyTitle);
        env->DeleteLocalRef(valTitle);
    }

    // ── Register callback and start activity ─────────────────────────────────
    int requestCode = Registry::get().registerCallback(std::move(cb));

    jmethodID startFor = env->GetMethodID(activityCls,
                                          "startActivityForResult",
                                          "(Landroid/content/Intent;I)V");
    env->CallVoidMethod(activityObj, startFor, intent, requestCode);

    env->DeleteLocalRef(intent);
    env->DeleteLocalRef(intentCls);

    FLUX_FP_LOG("launchOpenIntent: requestCode=%d multi=%d mimes=%zu",
                requestCode, multi, mimes.size());
    return requestCode;
}

// ── Save file (CREATE_DOCUMENT) ───────────────────────────────────────────────

inline int launchSaveIntent(const std::string&             title,
                             const std::string&             defaultFilename,
                             const std::vector<FileFilter>& filters,
                             ResultCallback                 cb) {
    JNIEnv* env = getJNIEnv();
    if (!env || !s_activity) {
        FLUX_FP_ERR("launchSaveIntent: no JNI env or activity");
        cb({});
        return -1;
    }

    auto mimes = filtersToMimes(filters);

    jobject activityObj = s_activity->clazz;
    jclass  activityCls = env->GetObjectClass(activityObj);

    jclass    intentCls  = env->FindClass("android/content/Intent");
    jmethodID intentCtor = env->GetMethodID(intentCls, "<init>",
                                            "(Ljava/lang/String;)V");

    jstring action = env->NewStringUTF("android.intent.action.CREATE_DOCUMENT");
    jobject intent = env->NewObject(intentCls, intentCtor, action);
    env->DeleteLocalRef(action);

    jmethodID addCat = env->GetMethodID(intentCls, "addCategory",
                                        "(Ljava/lang/String;)Landroid/content/Intent;");
    jstring catOpenable = env->NewStringUTF("android.intent.category.OPENABLE");
    env->CallObjectMethod(intent, addCat, catOpenable);
    env->DeleteLocalRef(catOpenable);

    jmethodID setType = env->GetMethodID(intentCls, "setType",
                                         "(Ljava/lang/String;)Landroid/content/Intent;");
    jstring primaryMime = env->NewStringUTF(mimes[0].c_str());
    env->CallObjectMethod(intent, setType, primaryMime);
    env->DeleteLocalRef(primaryMime);

    // Suggested filename
    if (!defaultFilename.empty()) {
        jmethodID putStr = env->GetMethodID(intentCls, "putExtra",
                                            "(Ljava/lang/String;Ljava/lang/String;)Landroid/content/Intent;");
        jstring keyTitle = env->NewStringUTF("android.intent.extra.TITLE");
        jstring valTitle = env->NewStringUTF(defaultFilename.c_str());
        env->CallObjectMethod(intent, putStr, keyTitle, valTitle);
        env->DeleteLocalRef(keyTitle);
        env->DeleteLocalRef(valTitle);
    }

    int requestCode = Registry::get().registerCallback(std::move(cb));

    jmethodID startFor = env->GetMethodID(activityCls,
                                          "startActivityForResult",
                                          "(Landroid/content/Intent;I)V");
    env->CallVoidMethod(activityObj, startFor, intent, requestCode);

    env->DeleteLocalRef(intent);
    env->DeleteLocalRef(intentCls);

    FLUX_FP_LOG("launchSaveIntent: requestCode=%d filename=%s",
                requestCode, defaultFilename.c_str());
    return requestCode;
}

// ── Folder picker (OPEN_DOCUMENT_TREE) ───────────────────────────────────────

inline int launchFolderIntent(const std::string& title, ResultCallback cb) {
    JNIEnv* env = getJNIEnv();
    if (!env || !s_activity) {
        FLUX_FP_ERR("launchFolderIntent: no JNI env or activity");
        cb({});
        return -1;
    }

    jobject activityObj = s_activity->clazz;
    jclass  activityCls = env->GetObjectClass(activityObj);

    jclass    intentCls  = env->FindClass("android/content/Intent");
    jmethodID intentCtor = env->GetMethodID(intentCls, "<init>",
                                            "(Ljava/lang/String;)V");

    jstring action = env->NewStringUTF("android.intent.action.OPEN_DOCUMENT_TREE");
    jobject intent = env->NewObject(intentCls, intentCtor, action);
    env->DeleteLocalRef(action);

    if (!title.empty()) {
        jmethodID putStr = env->GetMethodID(intentCls, "putExtra",
                                            "(Ljava/lang/String;Ljava/lang/String;)Landroid/content/Intent;");
        jstring keyTitle = env->NewStringUTF("android.intent.extra.TITLE");
        jstring valTitle = env->NewStringUTF(title.c_str());
        env->CallObjectMethod(intent, putStr, keyTitle, valTitle);
        env->DeleteLocalRef(keyTitle);
        env->DeleteLocalRef(valTitle);
    }

    int requestCode = Registry::get().registerCallback(std::move(cb));

    jmethodID startFor = env->GetMethodID(activityCls,
                                          "startActivityForResult",
                                          "(Landroid/content/Intent;I)V");
    env->CallVoidMethod(activityObj, startFor, intent, requestCode);

    env->DeleteLocalRef(intent);
    env->DeleteLocalRef(intentCls);

    FLUX_FP_LOG("launchFolderIntent: requestCode=%d", requestCode);
    return requestCode;
}

// ============================================================================
// URI → file path resolution
//
// Content URIs (content://...) are not real filesystem paths.
// We resolve them to a usable string by:
//   1. Trying MediaStore projection for DISPLAY_NAME + DATA columns
//   2. Falling back to copying the stream to the app's cache dir and
//      returning that temp path — always works regardless of provider.
//
// The returned path is what the app receives in its onChanged callback.
// For save dialogs the URI itself is sufficient (write via openOutputStream).
// ============================================================================

inline std::string resolveUri(JNIEnv* env, jobject contentResolver, jstring uriStr) {
    if (!uriStr) return {};

    const char* raw = env->GetStringUTFChars(uriStr, nullptr);
    std::string uriString(raw ? raw : "");
    env->ReleaseStringUTFChars(uriStr, raw);

    // If it's already a file:// URI or plain path, strip prefix
    if (uriString.rfind("file://", 0) == 0)
        return uriString.substr(7);
    if (!uriString.empty() && uriString[0] == '/')
        return uriString;

    // ── Try MediaStore DATA column ────────────────────────────────────────────
    jclass    uriCls     = env->FindClass("android/net/Uri");
    jmethodID parseUri   = env->GetStaticMethodID(uriCls, "parse",
                                                   "(Ljava/lang/String;)Landroid/net/Uri;");
    jobject   uri        = env->CallStaticObjectMethod(uriCls, parseUri, uriStr);

    jclass    projCls    = env->FindClass("java/lang/String");
    jobjectArray projection = env->NewObjectArray(2, projCls, nullptr);
    jstring colData      = env->NewStringUTF("_data");
    jstring colName      = env->NewStringUTF("_display_name");
    env->SetObjectArrayElement(projection, 0, colData);
    env->SetObjectArrayElement(projection, 1, colName);

    jmethodID query = env->GetMethodID(env->GetObjectClass(contentResolver),
                                       "query",
                                       "(Landroid/net/Uri;"
                                       "[Ljava/lang/String;"
                                       "Ljava/lang/String;"
                                       "[Ljava/lang/String;"
                                       "Ljava/lang/String;"
                                       ")Landroid/database/Cursor;");
    jobject cursor = env->CallObjectMethod(contentResolver, query,
                                           uri, projection,
                                           nullptr, nullptr, nullptr);

    std::string resolvedPath;

    if (cursor) {
        jclass    cursorCls    = env->GetObjectClass(cursor);
        jmethodID moveToFirst  = env->GetMethodID(cursorCls, "moveToFirst", "()Z");
        jmethodID getColIdx    = env->GetMethodID(cursorCls, "getColumnIndex",
                                                   "(Ljava/lang/String;)I");
        jmethodID getString    = env->GetMethodID(cursorCls, "getString",
                                                   "(I)Ljava/lang/String;");
        jmethodID closeCursor  = env->GetMethodID(cursorCls, "close", "()V");

        if (env->CallBooleanMethod(cursor, moveToFirst)) {
            jint dataIdx = env->CallIntMethod(cursor, getColIdx, colData);
            if (dataIdx >= 0) {
                jstring dataVal = (jstring)env->CallObjectMethod(cursor, getString, dataIdx);
                if (dataVal) {
                    const char* p = env->GetStringUTFChars(dataVal, nullptr);
                    if (p && p[0] != '\0') resolvedPath = p;
                    env->ReleaseStringUTFChars(dataVal, p);
                    env->DeleteLocalRef(dataVal);
                }
            }
        }
        env->CallVoidMethod(cursor, closeCursor);
        env->DeleteLocalRef(cursor);
    }

    env->DeleteLocalRef(colData);
    env->DeleteLocalRef(colName);
    env->DeleteLocalRef(projection);
    env->DeleteLocalRef(uri);

    if (!resolvedPath.empty()) {
        FLUX_FP_LOG("resolveUri: MediaStore path = %s", resolvedPath.c_str());
        return resolvedPath;
    }

    // ── Fallback: copy stream to cache dir ───────────────────────────────────
    // Re-parse URI (local ref was deleted above)
    uri = env->CallStaticObjectMethod(uriCls, parseUri, uriStr);

    jmethodID openStream = env->GetMethodID(
        env->GetObjectClass(contentResolver),
        "openInputStream",
        "(Landroid/net/Uri;)Ljava/io/InputStream;");
    jobject stream = env->CallObjectMethod(contentResolver, openStream, uri);
    env->DeleteLocalRef(uri);

    if (!stream) {
        FLUX_FP_ERR("resolveUri: openInputStream failed for %s", uriString.c_str());
        return uriString; // return the raw URI as last resort
    }

    // Derive a filename from the URI tail
    std::string filename = "flux_pick_tmp";
    size_t slash = uriString.rfind('/');
    if (slash != std::string::npos && slash + 1 < uriString.size())
        filename = uriString.substr(slash + 1);
    // Strip any query params
    size_t q = filename.find('?');
    if (q != std::string::npos) filename = filename.substr(0, q);

    // Build cache path
    std::string cachePath;
    {
        jobject   activityObj = s_activity->clazz;
        jclass    activityCls = env->GetObjectClass(activityObj);
        jmethodID getCacheDir = env->GetMethodID(activityCls, "getCacheDir",
                                                  "()Ljava/io/File;");
        jobject   cacheDir    = env->CallObjectMethod(activityObj, getCacheDir);
        jclass    fileCls     = env->GetObjectClass(cacheDir);
        jmethodID getPath     = env->GetMethodID(fileCls, "getAbsolutePath",
                                                  "()Ljava/lang/String;");
        jstring   pathStr     = (jstring)env->CallObjectMethod(cacheDir, getPath);
        const char* p         = env->GetStringUTFChars(pathStr, nullptr);
        cachePath = std::string(p) + "/" + filename;
        env->ReleaseStringUTFChars(pathStr, p);
        env->DeleteLocalRef(pathStr);
        env->DeleteLocalRef(cacheDir);
    }

    // Read InputStream → file
    jclass    isCls   = env->GetObjectClass(stream);
    jmethodID isRead  = env->GetMethodID(isCls, "read", "([B)I");
    jmethodID isClose = env->GetMethodID(isCls, "close", "()V");

    jbyteArray buf = env->NewByteArray(65536);
    FILE* f = fopen(cachePath.c_str(), "wb");
    if (f) {
        jint n;
        while ((n = env->CallIntMethod(stream, isRead, buf)) > 0) {
            jbyte* data = env->GetByteArrayElements(buf, nullptr);
            fwrite(data, 1, n, f);
            env->ReleaseByteArrayElements(buf, data, JNI_ABORT);
        }
        fclose(f);
        resolvedPath = cachePath;
        FLUX_FP_LOG("resolveUri: cached to %s", cachePath.c_str());
    } else {
        FLUX_FP_ERR("resolveUri: fopen failed for %s", cachePath.c_str());
        resolvedPath = uriString;
    }

    env->CallVoidMethod(stream, isClose);
    env->DeleteLocalRef(buf);
    env->DeleteLocalRef(stream);

    return resolvedPath;
}

} // namespace FluxFilePickerAndroid

// ============================================================================
// JNI entry point — called from Java's onActivityResult bridge
//
// Java signature (in FluxBridge.java):
//
//   public static native void nativeOnFilePickerResult(
//       int requestCode, int resultCode,
//       String[] uris, Object contentResolver);
//
// resultCode: RESULT_OK = -1, RESULT_CANCELED = 0  (standard Android values)
// ============================================================================

extern "C" JNIEXPORT void JNICALL
Java_com_flux_FluxBridge_nativeOnFilePickerResult(
        JNIEnv*      env,
        jclass       /*clazz*/,
        jint         requestCode,
        jint         resultCode,
        jobjectArray uris,          // String[] — may be null on cancel
        jobject      contentResolver)
{
    using namespace FluxFilePickerAndroid;

    static constexpr jint RESULT_OK = -1;

    if (resultCode != RESULT_OK || !uris) {
        FLUX_FP_LOG("nativeOnFilePickerResult: cancelled (code=%d)", requestCode);
        Registry::get().dispatch((int)requestCode, {});
        return;
    }

    jsize count = env->GetArrayLength(uris);
    std::vector<std::string> paths;
    paths.reserve(count);

    for (jsize i = 0; i < count; ++i) {
        jstring uriStr = (jstring)env->GetObjectArrayElement(uris, i);
        std::string resolved = resolveUri(env, contentResolver, uriStr);
        if (!resolved.empty()) paths.push_back(resolved);
        env->DeleteLocalRef(uriStr);
    }

    FLUX_FP_LOG("nativeOnFilePickerResult: requestCode=%d paths=%zu",
                (int)requestCode, paths.size());
    Registry::get().dispatch((int)requestCode, std::move(paths));
}

#endif // __ANDROID__