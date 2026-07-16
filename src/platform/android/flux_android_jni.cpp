// flux_android_jni.cpp
#ifdef __ANDROID__
#include "flux/flux_android_jni.hpp"
#include <cstdio>

android_app *FluxJNI::s_app = nullptr;
JavaVM *FluxJNI::s_vm = nullptr;
std::unordered_map<int, std::function<void(bool)>> FluxJNI::s_callbacks;
std::unordered_map<int, std::function<void(const std::vector<bool> &)>> FluxJNI::s_multiCallbacks;
std::mutex FluxJNI::s_callbackMutex;
jobject FluxJNI::s_videoSurface = nullptr;
ANativeActivity *s_activity = nullptr;

// ============================================================================
// Filesystem / MediaStore — moved from flux_android_main.cpp
// ============================================================================

std::string FluxJNI::getFilesDir()
{
    Env jni;
    if (!jni)
        return "/sdcard";

    jobject activityObj = getActivity();
    jclass activityCls = jni->GetObjectClass(activityObj);
    jmethodID getFilesDir = jni->GetMethodID(activityCls, "getFilesDir", "()Ljava/io/File;");
    jobject filesDir = jni->CallObjectMethod(activityObj, getFilesDir);
    jclass fileCls = jni->GetObjectClass(filesDir);
    jmethodID getAbs = jni->GetMethodID(fileCls, "getAbsolutePath", "()Ljava/lang/String;");
    jstring pathStr = (jstring)jni->CallObjectMethod(filesDir, getAbs);
    const char *chars = jni->GetStringUTFChars(pathStr, nullptr);
    std::string result(chars);
    jni->ReleaseStringUTFChars(pathStr, chars);
    jni->DeleteLocalRef(filesDir);
    jni->DeleteLocalRef(fileCls);
    jni->DeleteLocalRef(activityCls);
    jni->DeleteLocalRef(pathStr);
    return result;
}

std::string FluxJNI::saveToMediaStore(const std::string &filename,
                                      const std::string &mimeType,
                                      const std::string &relativePath,
                                      const uint8_t *data, size_t dataSize)
{
    Env jni;
    if (!jni)
        return "";

    jobject activityObj = getActivity();
    jclass activityCls = jni->GetObjectClass(activityObj);

    jmethodID getResolver = jni->GetMethodID(activityCls, "getContentResolver",
                                             "()Landroid/content/ContentResolver;");
    jobject resolver = jni->CallObjectMethod(activityObj, getResolver);
    jclass resolverCls = jni->GetObjectClass(resolver);

    jclass cvCls = jni->FindClass("android/content/ContentValues");
    jmethodID cvCtor = jni->GetMethodID(cvCls, "<init>", "()V");
    jobject cv = jni->NewObject(cvCls, cvCtor);

    jmethodID cvPutStr = jni->GetMethodID(cvCls, "put",
                                          "(Ljava/lang/String;Ljava/lang/String;)V");
    jmethodID cvPutInt = jni->GetMethodID(cvCls, "put",
                                          "(Ljava/lang/String;Ljava/lang/Integer;)V");

    auto putStr = [&](const char *k, const char *v)
    {
        jstring jk = jni->NewStringUTF(k), jv = jni->NewStringUTF(v);
        jni->CallVoidMethod(cv, cvPutStr, jk, jv);
        jni->DeleteLocalRef(jk);
        jni->DeleteLocalRef(jv);
    };
    putStr("_display_name", filename.c_str());
    putStr("mime_type", mimeType.c_str());
    putStr("relative_path", relativePath.c_str());

    jclass intCls = jni->FindClass("java/lang/Integer");
    jmethodID intOf = jni->GetStaticMethodID(intCls, "valueOf", "(I)Ljava/lang/Integer;");
    jstring kPending = jni->NewStringUTF("is_pending");
    jni->CallVoidMethod(cv, cvPutInt, kPending, jni->CallStaticObjectMethod(intCls, intOf, 1));

    const char *collClass = "android/provider/MediaStore$Images$Media";
    if (mimeType.find("audio") != std::string::npos)
        collClass = "android/provider/MediaStore$Audio$Media";
    else if (mimeType.find("video") != std::string::npos)
        collClass = "android/provider/MediaStore$Video$Media";

    jclass mediaCls = jni->FindClass(collClass);
    jfieldID extUriField = jni->GetStaticFieldID(mediaCls, "EXTERNAL_CONTENT_URI", "Landroid/net/Uri;");
    jobject extUri = jni->GetStaticObjectField(mediaCls, extUriField);

    jmethodID insert = jni->GetMethodID(resolverCls, "insert",
                                        "(Landroid/net/Uri;Landroid/content/ContentValues;)Landroid/net/Uri;");
    jobject fileUri = jni->CallObjectMethod(resolver, insert, extUri, cv);
    if (!fileUri)
    {
        jni->DeleteLocalRef(cv);
        jni->DeleteLocalRef(kPending);
        return "";
    }

    jmethodID openStream = jni->GetMethodID(resolverCls, "openOutputStream",
                                            "(Landroid/net/Uri;)Ljava/io/OutputStream;");
    jobject outStream = jni->CallObjectMethod(resolver, openStream, fileUri);
    if (!outStream)
    {
        jni->DeleteLocalRef(cv);
        jni->DeleteLocalRef(fileUri);
        jni->DeleteLocalRef(kPending);
        return "";
    }

    jclass osCls = jni->GetObjectClass(outStream);
    jmethodID osWrite = jni->GetMethodID(osCls, "write", "([B)V");
    jmethodID osClose = jni->GetMethodID(osCls, "close", "()V");

    static constexpr size_t kChunk = 65536;
    for (size_t offset = 0; offset < dataSize;)
    {
        size_t toWrite = std::min(kChunk, dataSize - offset);
        jbyteArray arr = jni->NewByteArray((jsize)toWrite);
        jni->SetByteArrayRegion(arr, 0, (jsize)toWrite,
                                reinterpret_cast<const jbyte *>(data + offset));
        jni->CallVoidMethod(outStream, osWrite, arr);
        jni->DeleteLocalRef(arr);
        offset += toWrite;
    }
    jni->CallVoidMethod(outStream, osClose);

    jstring kPending2 = jni->NewStringUTF("is_pending");
    jni->CallVoidMethod(cv, cvPutInt, kPending2, jni->CallStaticObjectMethod(intCls, intOf, 0));

    jmethodID update = jni->GetMethodID(resolverCls, "update",
                                        "(Landroid/net/Uri;Landroid/content/ContentValues;Ljava/lang/String;[Ljava/lang/String;)I");
    jni->CallIntMethod(resolver, update, fileUri, cv, nullptr, nullptr);

    jclass uriCls = jni->GetObjectClass(fileUri);
    jmethodID uriToString = jni->GetMethodID(uriCls, "toString", "()Ljava/lang/String;");
    jstring uriStr = (jstring)jni->CallObjectMethod(fileUri, uriToString);
    const char *uriChars = jni->GetStringUTFChars(uriStr, nullptr);
    std::string result(uriChars);
    jni->ReleaseStringUTFChars(uriStr, uriChars);

    jni->DeleteLocalRef(cv);
    jni->DeleteLocalRef(resolver);
    jni->DeleteLocalRef(resolverCls);
    jni->DeleteLocalRef(fileUri);
    jni->DeleteLocalRef(uriCls);
    jni->DeleteLocalRef(outStream);
    jni->DeleteLocalRef(osCls);
    jni->DeleteLocalRef(uriStr);
    jni->DeleteLocalRef(kPending);
    jni->DeleteLocalRef(kPending2);
    jni->DeleteLocalRef(activityCls);
    jni->DeleteLocalRef(cvCls);
    jni->DeleteLocalRef(intCls);
    jni->DeleteLocalRef(mediaCls);
    jni->DeleteLocalRef(extUri);
    return result;
}

// ============================================================================
// SurfaceTexture (video) — moved from FluxVideo_* free functions
// ============================================================================

void *FluxJNI::createSurfaceTexture(GLuint texId)
{
    Env jni;
    if (!jni)
        return nullptr;
    jclass cls = jni->FindClass("android/graphics/SurfaceTexture");
    jmethodID ctor = jni->GetMethodID(cls, "<init>", "(I)V");
    jobject st = jni->NewObject(cls, ctor, (jint)texId);
    jni->DeleteLocalRef(cls);
    return st ? reinterpret_cast<void *>(jni->NewGlobalRef(st)) : nullptr;
}

void FluxJNI::updateTexImage(void *surfaceTexture)
{
    Env jni;
    if (!jni || !surfaceTexture)
        return;
    jobject st = reinterpret_cast<jobject>(surfaceTexture);
    jclass cls = jni->GetObjectClass(st);
    jmethodID mid = jni->GetMethodID(cls, "updateTexImage", "()V");
    jni->CallVoidMethod(st, mid);
    jni->DeleteLocalRef(cls);
}

ANativeWindow *FluxJNI::getNativeWindowFromSurfaceTexture(void *surfaceTexture)
{
    Env jni;
    if (!jni || !surfaceTexture)
        return nullptr;

    if (s_videoSurface)
    {
        jni->DeleteGlobalRef(s_videoSurface);
        s_videoSurface = nullptr;
    }

    jobject st = reinterpret_cast<jobject>(surfaceTexture);
    jclass surfCls = jni->FindClass("android/view/Surface");
    jmethodID ctor = jni->GetMethodID(surfCls, "<init>", "(Landroid/graphics/SurfaceTexture;)V");
    jobject local = jni->NewObject(surfCls, ctor, st);
    jni->DeleteLocalRef(surfCls);
    if (!local)
        return nullptr;

    s_videoSurface = jni->NewGlobalRef(local);
    jni->DeleteLocalRef(local);

    return ANativeWindow_fromSurface(jni.env, s_videoSurface);
}

void FluxJNI::destroySurfaceTexture(void *surfaceTexture)
{
    Env jni;
    if (!jni || !surfaceTexture)
        return;

    if (s_videoSurface)
    {
        jni->DeleteGlobalRef(s_videoSurface);
        s_videoSurface = nullptr;
    }
    jni->DeleteGlobalRef(reinterpret_cast<jobject>(surfaceTexture));
}

// ============================================================================
// Legacy free-function shims
// ============================================================================
// Several files (camera_widget_android.cpp, flux_camera_android.cpp,
// flux_video_android.cpp, flux_audioplayer.hpp) still declare/call these as
// free functions "from native-lib.cpp". The implementation now lives on
// FluxJNI — these forward to it. Defined here (once, in this .cpp) rather
// than in the header, so exactly one copy exists in the final binary.

void *FluxVideo_createSurfaceTexture(GLuint texId)
{
    return FluxJNI::createSurfaceTexture(texId);
}

void FluxVideo_updateTexImage(void *surfaceTexture)
{
    FluxJNI::updateTexImage(surfaceTexture);
}

ANativeWindow *FluxVideo_getNativeWindow(void *surfaceTexture)
{
    return FluxJNI::getNativeWindowFromSurfaceTexture(surfaceTexture);
}

void FluxVideo_destroySurfaceTexture(void *surfaceTexture)
{
    FluxJNI::destroySurfaceTexture(surfaceTexture);
}

std::string FluxAndroid_saveToMediaStore(const std::string &filename,
                                         const std::string &mimeType,
                                         const std::string &relativePath,
                                         const uint8_t *data, size_t dataSize)
{
    return FluxJNI::saveToMediaStore(filename, mimeType, relativePath, data, dataSize);
}

JNIEnv *getJNIEnv()
{
    return FluxJNI::attach();
}

void FluxAndroid_requestPermission(const char *permission)
{
    FluxJNI::requestPermissionNoCallback(permission);
}

std::string FluxAndroid_getFilesDir()
{
    return FluxJNI::getFilesDir();
}


#endif