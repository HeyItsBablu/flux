// flux_file_picker_android.cpp
#ifdef __ANDROID__

#include "flux/widgets/flux_file_picker.hpp"

#include <android/log.h>
#include <android/native_activity.h>
#include <jni.h>

#include <algorithm>
#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#define FLUX_FP_TAG "FluxFilePicker"
#define FLUX_FP_LOG(...) __android_log_print(ANDROID_LOG_DEBUG, FLUX_FP_TAG, __VA_ARGS__)
#define FLUX_FP_ERR(...) __android_log_print(ANDROID_LOG_ERROR, FLUX_FP_TAG, __VA_ARGS__)

// ── Global JavaVM — set by JNI_OnLoad ────────────────────────────────────────
static JavaVM *s_javaVM = nullptr;

// ============================================================================
// §1  FluxFilePickerAndroid — Intent backend
// ============================================================================

namespace FluxFilePickerAndroid
{

    // ── Global state ──────────────────────────────────────────────────────────
    JNIEnv *g_env = nullptr;
    ANativeActivity *g_activity = nullptr;
    jclass s_bridgeClass = nullptr;

    // ── Pending callback queue (game-thread drain) ────────────────────────────
    static std::mutex s_pendingMutex;
    static std::vector<std::function<void()>> s_pendingCallbacks;

    void postToMainLoop(std::function<void()> fn)
    {
        std::lock_guard<std::mutex> lock(s_pendingMutex);
        s_pendingCallbacks.push_back(std::move(fn));
    }

    void drainPendingCallbacks()
    {
        std::vector<std::function<void()>> cbs;
        {
            std::lock_guard<std::mutex> lock(s_pendingMutex);
            cbs.swap(s_pendingCallbacks);
        }
        for (auto &fn : cbs)
            fn();
    }

    // ── Request-code registry ─────────────────────────────────────────────────
    using ResultCallback = std::function<void(std::vector<std::string>)>;

    static std::unordered_map<int, ResultCallback> s_callbacks;
    static std::atomic<int> s_nextCode{5000};

    static int registerCallback(ResultCallback cb)
    {
        int code = s_nextCode.fetch_add(1);
        s_callbacks[code] = std::move(cb);
        return code;
    }

    // ── init — called from APP_CMD_INIT_WINDOW on the game thread ────────────
    void init(JNIEnv *env, ANativeActivity *activity)
    {
        g_env = env;
        g_activity = activity;

        jobject activityObj = activity->clazz;
        jclass activityCls = env->GetObjectClass(activityObj);
        jmethodID getClassLoader = env->GetMethodID(activityCls, "getClassLoader",
                                                    "()Ljava/lang/ClassLoader;");
        jobject classLoader = env->CallObjectMethod(activityObj, getClassLoader);

        jclass clsLoaderCls = env->GetObjectClass(classLoader);
        jmethodID loadClass = env->GetMethodID(clsLoaderCls, "loadClass",
                                               "(Ljava/lang/String;)Ljava/lang/Class;");

        jstring className = env->NewStringUTF("com.flux.FluxBridge");
        jclass bridgeLocal = (jclass)env->CallObjectMethod(classLoader, loadClass, className);

        env->DeleteLocalRef(className);
        env->DeleteLocalRef(classLoader);
        env->DeleteLocalRef(clsLoaderCls);
        env->DeleteLocalRef(activityCls);

        if (!bridgeLocal || env->ExceptionCheck())
        {
            FLUX_FP_ERR("init: FluxBridge not found via ClassLoader");
            env->ExceptionClear();
            return;
        }

        s_bridgeClass = (jclass)env->NewGlobalRef(bridgeLocal);
        env->DeleteLocalRef(bridgeLocal);
        FLUX_FP_LOG("init: FluxBridge cached OK");
    }

    // ── dispatchResult — called from JNI callback (main thread) ──────────────
    void dispatchResult(int requestCode, std::vector<std::string> paths)
    {
        auto it = s_callbacks.find(requestCode);
        if (it == s_callbacks.end())
        {
            FLUX_FP_ERR("dispatchResult: unknown request code %d", requestCode);
            return;
        }
        auto cb = std::move(it->second);
        s_callbacks.erase(it);
        postToMainLoop([cb = std::move(cb), paths = std::move(paths)]() mutable
                       { cb(std::move(paths)); });
    }

    // ── MIME helpers ──────────────────────────────────────────────────────────

    static std::string extToMime(const std::string &ext)
    {
        std::string e = ext;
        if (e.size() >= 2 && e[0] == '*' && e[1] == '.')
            e = e.substr(2);

        if (e == "*" || e == "*.*")
            return "*/*";
        if (e == "png")
            return "image/png";
        if (e == "jpg" || e == "jpeg")
            return "image/jpeg";
        if (e == "gif")
            return "image/gif";
        if (e == "bmp")
            return "image/bmp";
        if (e == "webp")
            return "image/webp";
        if (e == "tga" || e == "tiff" || e == "tif")
            return "image/tiff";
        if (e == "svg")
            return "image/svg+xml";
        if (e == "heic" || e == "heif")
            return "image/heic";
        if (e == "ico")
            return "image/x-icon";
        if (e == "mp4")
            return "video/mp4";
        if (e == "mkv")
            return "video/x-matroska";
        if (e == "avi")
            return "video/x-msvideo";
        if (e == "mov")
            return "video/quicktime";
        if (e == "webm")
            return "video/webm";
        if (e == "mp3")
            return "audio/mpeg";
        if (e == "wav")
            return "audio/wav";
        if (e == "ogg")
            return "audio/ogg";
        if (e == "flac")
            return "audio/flac";
        if (e == "aac")
            return "audio/aac";
        if (e == "m4a")
            return "audio/mp4";
        if (e == "pdf")
            return "application/pdf";
        if (e == "txt")
            return "text/plain";
        if (e == "md")
            return "text/markdown";
        if (e == "html" || e == "htm")
            return "text/html";
        if (e == "csv")
            return "text/csv";
        if (e == "xml")
            return "text/xml";
        if (e == "json")
            return "application/json";
        if (e == "zip")
            return "application/zip";
        if (e == "tar")
            return "application/x-tar";
        if (e == "gz")
            return "application/gzip";
        if (e == "doc")
            return "application/msword";
        if (e == "docx")
            return "application/vnd.openxmlformats-officedocument.wordprocessingml.document";
        if (e == "xls")
            return "application/vnd.ms-excel";
        if (e == "xlsx")
            return "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet";
        if (e == "ppt")
            return "application/vnd.ms-powerpoint";
        if (e == "pptx")
            return "application/vnd.openxmlformats-officedocument.presentationml.presentation";
        return "application/octet-stream";
    }

    static std::vector<std::string> filtersToMimes(const std::vector<FileFilter> &filters)
    {
        std::unordered_map<std::string, bool> seen;
        std::vector<std::string> result;
        for (const auto &f : filters)
            for (const auto &ext : f.extensions)
            {
                std::string mime = extToMime(ext);
                if (!seen.count(mime))
                {
                    seen[mime] = true;
                    result.push_back(mime);
                }
            }
        if (result.empty())
            return {"*/*"};
        for (const auto &m : result)
            if (m == "*/*")
                return {"*/*"};
        return result;
    }

    // ── URI → path resolver ───────────────────────────────────────────────────

    static std::string resolveUri(JNIEnv *env, jobject contentResolver, jstring uriJStr)
    {
        if (!uriJStr)
            return {};

        const char *raw = env->GetStringUTFChars(uriJStr, nullptr);
        std::string uriStr(raw ? raw : "");
        env->ReleaseStringUTFChars(uriJStr, raw);

        if (uriStr.rfind("file://", 0) == 0)
            return uriStr.substr(7);
        if (!uriStr.empty() && uriStr[0] == '/')
            return uriStr;

        jclass uriCls = env->FindClass("android/net/Uri");
        jmethodID parseUri = env->GetStaticMethodID(uriCls, "parse",
                                                    "(Ljava/lang/String;)Landroid/net/Uri;");
        jobject uri = env->CallStaticObjectMethod(uriCls, parseUri, uriJStr);

        jclass strCls = env->FindClass("java/lang/String");
        jobjectArray projection = env->NewObjectArray(1, strCls, nullptr);
        jstring colData = env->NewStringUTF("_data");
        env->SetObjectArrayElement(projection, 0, colData);

        jmethodID query = env->GetMethodID(env->GetObjectClass(contentResolver), "query",
                                           "(Landroid/net/Uri;[Ljava/lang/String;"
                                           "Ljava/lang/String;[Ljava/lang/String;"
                                           "Ljava/lang/String;)Landroid/database/Cursor;");
        jobject cursor = env->CallObjectMethod(contentResolver, query,
                                               uri, projection, nullptr, nullptr, nullptr);

        std::string resolved;
        if (cursor)
        {
            jclass cc = env->GetObjectClass(cursor);
            jmethodID moveToFirst = env->GetMethodID(cc, "moveToFirst", "()Z");
            jmethodID getColIdx = env->GetMethodID(cc, "getColumnIndex", "(Ljava/lang/String;)I");
            jmethodID getString = env->GetMethodID(cc, "getString", "(I)Ljava/lang/String;");
            jmethodID close = env->GetMethodID(cc, "close", "()V");

            if (env->CallBooleanMethod(cursor, moveToFirst))
            {
                jint idx = env->CallIntMethod(cursor, getColIdx, colData);
                if (idx >= 0)
                {
                    jstring val = (jstring)env->CallObjectMethod(cursor, getString, idx);
                    if (val)
                    {
                        const char *p = env->GetStringUTFChars(val, nullptr);
                        if (p && p[0] != '\0')
                            resolved = p;
                        env->ReleaseStringUTFChars(val, p);
                        env->DeleteLocalRef(val);
                    }
                }
            }
            env->CallVoidMethod(cursor, close);
            env->DeleteLocalRef(cursor);
        }

        env->DeleteLocalRef(colData);
        env->DeleteLocalRef(projection);
        env->DeleteLocalRef(uri);

        if (!resolved.empty())
            return resolved;

        // Fallback: copy stream to cache dir
        uri = env->CallStaticObjectMethod(uriCls, parseUri, uriJStr);

        jmethodID openStream = env->GetMethodID(env->GetObjectClass(contentResolver),
                                                "openInputStream",
                                                "(Landroid/net/Uri;)Ljava/io/InputStream;");
        jobject stream = env->CallObjectMethod(contentResolver, openStream, uri);
        env->DeleteLocalRef(uri);

        if (!stream)
        {
            FLUX_FP_ERR("resolveUri: openInputStream failed");
            return uriStr;
        }

        std::string filename = "flux_pick_tmp";
        size_t slash = uriStr.rfind('/');
        if (slash != std::string::npos && slash + 1 < uriStr.size())
            filename = uriStr.substr(slash + 1);
        size_t q = filename.find('?');
        if (q != std::string::npos)
            filename = filename.substr(0, q);

        std::string cachePath;
        {
            jobject actObj = g_activity->clazz;
            jclass actCls = env->GetObjectClass(actObj);
            jmethodID getDir = env->GetMethodID(actCls, "getCacheDir", "()Ljava/io/File;");
            jobject cacheDir = env->CallObjectMethod(actObj, getDir);
            jmethodID getPath = env->GetMethodID(env->GetObjectClass(cacheDir),
                                                 "getAbsolutePath", "()Ljava/lang/String;");
            jstring pathStr = (jstring)env->CallObjectMethod(cacheDir, getPath);
            const char *p = env->GetStringUTFChars(pathStr, nullptr);
            cachePath = std::string(p) + "/" + filename;
            env->ReleaseStringUTFChars(pathStr, p);
            env->DeleteLocalRef(pathStr);
            env->DeleteLocalRef(cacheDir);
        }

        jclass isCls = env->GetObjectClass(stream);
        jmethodID isRead = env->GetMethodID(isCls, "read", "([B)I");
        jmethodID isClose = env->GetMethodID(isCls, "close", "()V");
        jbyteArray buf = env->NewByteArray(65536);

        FILE *f = fopen(cachePath.c_str(), "wb");
        if (f)
        {
            jint n;
            while ((n = env->CallIntMethod(stream, isRead, buf)) > 0)
            {
                jbyte *data = env->GetByteArrayElements(buf, nullptr);
                fwrite(data, 1, (size_t)n, f);
                env->ReleaseByteArrayElements(buf, data, JNI_ABORT);
            }
            fclose(f);
            resolved = cachePath;
        }
        else
        {
            resolved = uriStr;
        }

        env->CallVoidMethod(stream, isClose);
        env->DeleteLocalRef(buf);
        env->DeleteLocalRef(stream);
        return resolved;
    }

    // ── Intent helpers ────────────────────────────────────────────────────────

    static jobject makeIntent(JNIEnv *env, const char *action)
    {
        jclass cls = env->FindClass("android/content/Intent");
        jmethodID ctor = env->GetMethodID(cls, "<init>", "(Ljava/lang/String;)V");
        jstring actionStr = env->NewStringUTF(action);
        jobject intent = env->NewObject(cls, ctor, actionStr);
        env->DeleteLocalRef(actionStr);
        env->DeleteLocalRef(cls);
        return intent;
    }

    static void intentSetMimes(JNIEnv *env, jobject intent,
                               const std::vector<std::string> &mimes)
    {
        jclass cls = env->FindClass("android/content/Intent");
        jmethodID setType = env->GetMethodID(cls, "setType",
                                             "(Ljava/lang/String;)Landroid/content/Intent;");
        jstring primary = env->NewStringUTF(mimes[0].c_str());
        env->CallObjectMethod(intent, setType, primary);
        env->DeleteLocalRef(primary);

        if (mimes.size() > 1)
        {
            jclass strCls = env->FindClass("java/lang/String");
            jobjectArray arr = env->NewObjectArray((jsize)mimes.size(), strCls, nullptr);
            for (size_t i = 0; i < mimes.size(); ++i)
            {
                jstring ms = env->NewStringUTF(mimes[i].c_str());
                env->SetObjectArrayElement(arr, (jsize)i, ms);
                env->DeleteLocalRef(ms);
            }
            jmethodID putExtra = env->GetMethodID(cls, "putExtra",
                                                  "(Ljava/lang/String;[Ljava/lang/String;)Landroid/content/Intent;");
            jstring key = env->NewStringUTF("android.intent.extra.MIME_TYPES");
            env->CallObjectMethod(intent, putExtra, key, arr);
            env->DeleteLocalRef(key);
            env->DeleteLocalRef(arr);
        }
        env->DeleteLocalRef(cls);
    }

    static void intentAddCategory(JNIEnv *env, jobject intent, const char *cat)
    {
        jclass cls = env->FindClass("android/content/Intent");
        jmethodID addCat = env->GetMethodID(cls, "addCategory",
                                            "(Ljava/lang/String;)Landroid/content/Intent;");
        jstring catStr = env->NewStringUTF(cat);
        env->CallObjectMethod(intent, addCat, catStr);
        env->DeleteLocalRef(catStr);
        env->DeleteLocalRef(cls);
    }

    static void intentPutStringExtra(JNIEnv *env, jobject intent,
                                     const char *key, const char *val)
    {
        jclass cls = env->FindClass("android/content/Intent");
        jmethodID putExtra = env->GetMethodID(cls, "putExtra",
                                              "(Ljava/lang/String;Ljava/lang/String;)Landroid/content/Intent;");
        jstring k = env->NewStringUTF(key);
        jstring v = env->NewStringUTF(val);
        env->CallObjectMethod(intent, putExtra, k, v);
        env->DeleteLocalRef(k);
        env->DeleteLocalRef(v);
        env->DeleteLocalRef(cls);
    }

    static void intentPutBoolExtra(JNIEnv *env, jobject intent,
                                   const char *key, bool val)
    {
        jclass cls = env->FindClass("android/content/Intent");
        jmethodID putExtra = env->GetMethodID(cls, "putExtra",
                                              "(Ljava/lang/String;Z)Landroid/content/Intent;");
        jstring k = env->NewStringUTF(key);
        env->CallObjectMethod(intent, putExtra, k, val ? JNI_TRUE : JNI_FALSE);
        env->DeleteLocalRef(k);
        env->DeleteLocalRef(cls);
    }

    // ── launchIntent ─────────────────────────────────────────────────────────

    static void launchIntent(jobject intentGlobalRef, ResultCallback cb)
    {
        int code = registerCallback(std::move(cb));
        JNIEnv *env = g_env;

        if (!s_bridgeClass)
        {
            FLUX_FP_ERR("launchIntent: s_bridgeClass is null — was init() called?");
            env->DeleteGlobalRef(intentGlobalRef);
            dispatchResult(code, {});
            return;
        }

        jmethodID launch = env->GetStaticMethodID(s_bridgeClass, "launchFilePicker",
                                                  "(Landroid/app/Activity;Landroid/content/Intent;I)V");
        if (!launch || env->ExceptionCheck())
        {
            FLUX_FP_ERR("launchIntent: launchFilePicker method not found");
            env->ExceptionClear();
            env->DeleteGlobalRef(intentGlobalRef);
            dispatchResult(code, {});
            return;
        }

        env->CallStaticVoidMethod(s_bridgeClass, launch,
                                  g_activity->clazz, intentGlobalRef, (jint)code);
        env->DeleteGlobalRef(intentGlobalRef);
        FLUX_FP_LOG("launchIntent: requestCode=%d", code);
    }

    // ── Public async API ──────────────────────────────────────────────────────

    void pickFileAsync(const std::string &title,
                       const std::vector<FileFilter> &filters,
                       std::function<void(std::string)> callback)
    {
        if (!g_env || !g_activity || !s_bridgeClass)
        {
            callback({});
            return;
        }
        auto mimes = filtersToMimes(filters);
        jobject intent = makeIntent(g_env, "android.intent.action.OPEN_DOCUMENT");
        intentAddCategory(g_env, intent, "android.intent.category.OPENABLE");
        intentSetMimes(g_env, intent, mimes);
        if (!title.empty())
            intentPutStringExtra(g_env, intent, "android.intent.extra.TITLE", title.c_str());
        jobject intentGlobal = g_env->NewGlobalRef(intent);
        g_env->DeleteLocalRef(intent);
        launchIntent(intentGlobal, [callback](std::vector<std::string> ps)
                     { callback(ps.empty() ? "" : ps[0]); });
    }

    void pickFilesAsync(const std::string &title,
                        const std::vector<FileFilter> &filters,
                        std::function<void(std::vector<std::string>)> callback)
    {
        if (!g_env || !g_activity || !s_bridgeClass)
        {
            callback({});
            return;
        }
        auto mimes = filtersToMimes(filters);
        jobject intent = makeIntent(g_env, "android.intent.action.OPEN_DOCUMENT");
        intentAddCategory(g_env, intent, "android.intent.category.OPENABLE");
        intentSetMimes(g_env, intent, mimes);
        intentPutBoolExtra(g_env, intent, "android.intent.extra.ALLOW_MULTIPLE", true);
        if (!title.empty())
            intentPutStringExtra(g_env, intent, "android.intent.extra.TITLE", title.c_str());
        jobject intentGlobal = g_env->NewGlobalRef(intent);
        g_env->DeleteLocalRef(intent);
        launchIntent(intentGlobal, callback);
    }

    void saveFileAsync(const std::string &title,
                       const std::string &defaultFilename,
                       const std::vector<FileFilter> &filters,
                       std::function<void(std::string)> callback)
    {
        if (!g_env || !g_activity || !s_bridgeClass)
        {
            callback({});
            return;
        }
        auto mimes = filtersToMimes(filters);
        jobject intent = makeIntent(g_env, "android.intent.action.CREATE_DOCUMENT");
        intentAddCategory(g_env, intent, "android.intent.category.OPENABLE");
        intentSetMimes(g_env, intent, mimes);
        if (!defaultFilename.empty())
            intentPutStringExtra(g_env, intent, "android.intent.extra.TITLE",
                                 defaultFilename.c_str());
        else if (!title.empty())
            intentPutStringExtra(g_env, intent, "android.intent.extra.TITLE", title.c_str());
        jobject intentGlobal = g_env->NewGlobalRef(intent);
        g_env->DeleteLocalRef(intent);
        launchIntent(intentGlobal, [callback](std::vector<std::string> ps)
                     { callback(ps.empty() ? "" : ps[0]); });
    }

    void pickFolderAsync(const std::string &title,
                         std::function<void(std::string)> callback)
    {
        if (!g_env || !g_activity || !s_bridgeClass)
        {
            callback({});
            return;
        }
        jobject intent = makeIntent(g_env, "android.intent.action.OPEN_DOCUMENT_TREE");
        if (!title.empty())
            intentPutStringExtra(g_env, intent, "android.intent.extra.TITLE", title.c_str());
        jobject intentGlobal = g_env->NewGlobalRef(intent);
        g_env->DeleteLocalRef(intent);
        launchIntent(intentGlobal, [callback](std::vector<std::string> ps)
                     { callback(ps.empty() ? "" : ps[0]); });
    }

} // namespace FluxFilePickerAndroid

// ============================================================================
// JNI entry point — called from FluxBridge.java → nativeOnFilePickerResult()
// ============================================================================

extern "C" JNIEXPORT void JNICALL
Java_com_flux_FluxBridge_nativeOnFilePickerResult(JNIEnv *env,
                                                  jclass /*clazz*/,
                                                  jint requestCode,
                                                  jint resultCode,
                                                  jobjectArray uris,
                                                  jobject contentResolver)
{
    using namespace FluxFilePickerAndroid;

    static constexpr jint RESULT_OK = -1;

    if (resultCode != RESULT_OK || !uris)
    {
        FLUX_FP_LOG("nativeOnFilePickerResult: cancelled (code=%d)", (int)requestCode);
        dispatchResult((int)requestCode, {});
        return;
    }

    jsize count = env->GetArrayLength(uris);
    std::vector<std::string> paths;
    paths.reserve(count);

    for (jsize i = 0; i < count; ++i)
    {
        jstring uriStr = (jstring)env->GetObjectArrayElement(uris, i);
        std::string resolved = resolveUri(env, contentResolver, uriStr);
        if (!resolved.empty())
            paths.push_back(resolved);
        env->DeleteLocalRef(uriStr);
    }

    FLUX_FP_LOG("nativeOnFilePickerResult: requestCode=%d paths=%zu",
                (int)requestCode, paths.size());
    dispatchResult((int)requestCode, std::move(paths));
}

// ============================================================================
// §2  FilePickerWidget — method definitions
// ============================================================================

std::shared_ptr<FilePickerWidget> FilePickerWidget::self_()
{
    return std::static_pointer_cast<FilePickerWidget>(shared_from_this());
}

bool FilePickerWidget::_inBounds(int mx, int my) const
{
    return mx >= x && mx < x + width && my >= y && my < y + height;
}

std::string FilePickerWidget::_label() const
{
    if (!customLabel_.empty())
        return customLabel_;
    switch (mode_)
    {
    case FilePickerMode::Open:
    case FilePickerMode::OpenMultiple:
        return "Open";
    case FilePickerMode::Save:
        return "Save As";
    case FilePickerMode::Folder:
        return "Choose Folder";
    }
    return "Open";
}

std::string FilePickerWidget::_defaultTitle() const
{
    switch (mode_)
    {
    case FilePickerMode::Open:
        return "Open File";
    case FilePickerMode::OpenMultiple:
        return "Open Files";
    case FilePickerMode::Save:
        return "Save File";
    case FilePickerMode::Folder:
        return "Select Folder";
    }
    return "Browse";
}

void FilePickerWidget::_measureLabel(GraphicsContext &ctx, FontCache &fc)
{
    NativeFont font = fc.getFont(fontFamily, labelFontSize, labelFontWeight);
    Painter p(ctx);
    int tw = 0, th = 0;
    p.measureText(toWideString(_label()), font, tw, th);
    if (autoWidth)
        width = tw;
    if (autoHeight)
        height = th;
}

void FilePickerWidget::_setSinglePath(const std::string &p)
{
    path_ = p;
    paths_ = {p};
    _commitPaths();
}

void FilePickerWidget::_setMultiPaths(const std::vector<std::string> &ps)
{
    paths_ = ps;
    path_ = ps.empty() ? "" : ps[0];
    _commitPaths();
}

void FilePickerWidget::_commitPaths()
{
    FLUX_FP_LOG("_commitPaths: path='%s'", path_.c_str());
    if (boundPath_)
        boundPath_->set(path_);
    if (boundPaths_)
        boundPaths_->set(paths_);
    if (onChanged_ && !path_.empty())
        onChanged_(path_);
    if (onMultiChanged_)
        onMultiChanged_(paths_);
    markNeedsPaint();
    _repaint();
}

void FilePickerWidget::open() { _openDialog(); }

void FilePickerWidget::clear()
{
    path_ = "";
    paths_.clear();
    if (boundPath_)
        boundPath_->set("");
    if (boundPaths_)
        boundPaths_->set({});
    if (onChanged_)
        onChanged_("");
    markNeedsPaint();
    _repaint();
}

std::shared_ptr<FilePickerWidget> FilePickerWidget::setMode(FilePickerMode m)
{
    mode_ = m;
    return self_();
}
std::shared_ptr<FilePickerWidget> FilePickerWidget::setTitle(const std::string &t)
{
    title_ = t;
    return self_();
}
std::shared_ptr<FilePickerWidget> FilePickerWidget::setDefaultFilename(const std::string &f)
{
    defaultFilename_ = f;
    return self_();
}
std::shared_ptr<FilePickerWidget> FilePickerWidget::setDefaultExtension(const std::string &e)
{
    defaultExt_ = (!e.empty() && e[0] == '.') ? e.substr(1) : e;
    return self_();
}
std::shared_ptr<FilePickerWidget> FilePickerWidget::setInitialDir(const std::string &d)
{
    initialDir_ = d;
    return self_();
}
std::shared_ptr<FilePickerWidget> FilePickerWidget::addFilter(const std::string &label,
                                                              std::vector<std::string> exts)
{
    filters_.emplace_back(label, std::move(exts));
    return self_();
}
std::shared_ptr<FilePickerWidget> FilePickerWidget::addFilter(FileFilter f)
{
    filters_.push_back(std::move(f));
    return self_();
}
std::shared_ptr<FilePickerWidget> FilePickerWidget::setFilters(std::vector<FileFilter> fs)
{
    filters_ = std::move(fs);
    return self_();
}
std::shared_ptr<FilePickerWidget> FilePickerWidget::setHeight(int h)
{
    height = h;
    autoHeight = false;
    markNeedsLayout();
    return self_();
}
std::shared_ptr<FilePickerWidget> FilePickerWidget::setWidth(int w)
{
    width = w;
    autoWidth = false;
    markNeedsLayout();
    return self_();
}
std::shared_ptr<FilePickerWidget> FilePickerWidget::setFlex(int f)
{
    flex = f;
    return self_();
}

std::shared_ptr<FilePickerWidget> FilePickerWidget::setLabelTextColor(Color c)
{
    labelTextColor = c;
    markNeedsPaint();
    return self_();
}
std::shared_ptr<FilePickerWidget> FilePickerWidget::setLabelHoverColor(Color c)
{
    labelHoverColor = c;
    markNeedsPaint();
    return self_();
}
std::shared_ptr<FilePickerWidget> FilePickerWidget::setLabelFontSize(int s)
{
    labelFontSize = s;
    markNeedsLayout();
    return self_();
}
std::shared_ptr<FilePickerWidget> FilePickerWidget::setLabelFontWeight(FontWeight w)
{
    labelFontWeight = w;
    markNeedsLayout();
    return self_();
}

std::shared_ptr<FilePickerWidget> FilePickerWidget::bindPath(State<std::string> &state)
{
    path_ = state.get();
    state.bindProperty(shared_from_this(), [](Widget *w, const std::string &v)
                       {
                           auto *fp = static_cast<FilePickerWidget *>(w);
                           fp->path_ = v;
                           fp->markNeedsPaint(); }, false);
    boundPath_ = &state;
    return self_();
}

std::shared_ptr<FilePickerWidget>
FilePickerWidget::bindPaths(State<std::vector<std::string>> &state)
{
    paths_ = state.get();
    if (!paths_.empty())
        path_ = paths_[0];
    state.bindProperty(shared_from_this(), [](Widget *w, const std::vector<std::string> &v)
                       {
                           auto *fp = static_cast<FilePickerWidget *>(w);
                           fp->paths_ = v;
                           fp->path_  = v.empty() ? "" : v[0];
                           fp->markNeedsPaint(); }, false);
    boundPaths_ = &state;
    return self_();
}

std::shared_ptr<FilePickerWidget>
FilePickerWidget::setOnChanged(std::function<void(const std::string &)> fn)
{
    onChanged_ = std::move(fn);
    return self_();
}

std::shared_ptr<FilePickerWidget>
FilePickerWidget::setOnMultiChanged(std::function<void(const std::vector<std::string> &)> fn)
{
    onMultiChanged_ = std::move(fn);
    return self_();
}

std::shared_ptr<FilePickerWidget>
FilePickerWidget::setOnCancelled(std::function<void()> fn)
{
    onCancelled_ = std::move(fn);
    return self_();
}

// ── Layout ────────────────────────────────────────────────────────────────

void FilePickerWidget::computeLayout(GraphicsContext &ctx,
                                     const BoxConstraints &constraints,
                                     FontCache &fc)
{
    if (labelWidget_)
    {
        labelWidget_->computeLayout(ctx, constraints, fc);
        if (autoWidth)
            width = labelWidget_->width;
        if (autoHeight)
            height = labelWidget_->height;
    }
    else
    {
        _measureLabel(ctx, fc);
    }

    width = constraints.clampWidth(width);
    height = constraints.clampHeight(height);
    applyConstraints();
    needsLayout = false;

    if (labelWidget_)
    {
        labelWidget_->x = x;
        labelWidget_->y = y;
        labelWidget_->positionChildren(
            x + labelWidget_->paddingLeft,
            y + labelWidget_->paddingTop,
            labelWidget_->width - labelWidget_->paddingLeft - labelWidget_->paddingRight,
            labelWidget_->height - labelWidget_->paddingTop - labelWidget_->paddingBottom);
    }
}

// ── Render ────────────────────────────────────────────────────────────────

void FilePickerWidget::render(GraphicsContext &ctx, FontCache &fontCache)
{
    if (!visible)
        return;

    if (labelWidget_)
    {
        labelWidget_->isHovered = isHovered;
        labelWidget_->render(ctx, fontCache);
    }
    else
    {
        Color col = (isHovered && isFocusable) ? labelHoverColor : labelTextColor;
        NativeFont font = fontCache.getFont(fontFamily, labelFontSize, labelFontWeight);
        Painter(ctx).drawTextA(_label(), x, y, width, height, font, col,
                               DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }

    needsPaint = false;
}

// ── Input events ──────────────────────────────────────────────────────────

bool FilePickerWidget::handleMouseDown(int mx, int my)
{
    if (!_inBounds(mx, my))
        return false;
    _openDialog();
    return true;
}

bool FilePickerWidget::handleMouseMove(int mx, int my)
{
    lastMx_ = mx;
    lastMy_ = my;
    bool nowHovered = _inBounds(mx, my);
    if (nowHovered != isHovered)
    {
        isHovered = nowHovered;
        markNeedsPaint();
    }
    return false;
}

bool FilePickerWidget::handleMouseLeave()
{
    lastMx_ = lastMy_ = -9999;
    isHovered = false;
    markNeedsPaint();
    return false;
}

bool FilePickerWidget::handleKeyDown(int key)
{
    if (key == Key::Return || key == Key::Space)
    {
        _openDialog();
        return true;
    }
    return false;
}

void FilePickerWidget::_repaint()
{
    needsPaint = true;
}

void FilePickerWidget::_openDialog()
{
    FLUX_FP_LOG("_openDialog: mode=%d", (int)mode_);
    using namespace FluxFilePickerAndroid;

    const std::string title = title_.empty() ? _defaultTitle() : title_;
    auto self = self_();

    switch (mode_)
    {
    case FilePickerMode::Open:
        pickFileAsync(title, filters_, [self](std::string p)
                      {
            if (p.empty()) { if (self->onCancelled_) self->onCancelled_(); return; }
            self->_setSinglePath(p); });
        break;
    case FilePickerMode::OpenMultiple:
        pickFilesAsync(title, filters_, [self](std::vector<std::string> ps)
                       {
            if (ps.empty()) { if (self->onCancelled_) self->onCancelled_(); return; }
            self->_setMultiPaths(ps); });
        break;
    case FilePickerMode::Save:
        saveFileAsync(title, defaultFilename_, filters_, [self](std::string p)
                      {
            if (p.empty()) { if (self->onCancelled_) self->onCancelled_(); return; }
            if (!self->defaultExt_.empty() && p.find('.') == std::string::npos)
                p += '.' + self->defaultExt_;
            self->_setSinglePath(p); });
        break;
    case FilePickerMode::Folder:
        pickFolderAsync(title, [self](std::string p)
                        {
            if (p.empty()) { if (self->onCancelled_) self->onCancelled_(); return; }
            self->_setSinglePath(p); });
        break;
    }
}

#endif // __ANDROID__