// flux_secure_storage_android.cpp
// Android backend: Android Keystore System + AES-256-GCM via JNI
//
// Key material never leaves the Keystore hardware (TEE/StrongBox on API 28+).
// Encrypted blobs are stored in the app's internal files directory as
//   <filesDir>/flux_secure/<serviceName>/<base64(key)>.enc
// This matches how flutter_secure_storage works under the hood.

#ifdef __ANDROID__

#include "flux/flux_secure_storage.hpp"

#include <android/log.h>
#include <jni.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <functional>
#include <mutex>
#include <optional>
#include <sstream>
#include <thread>
#include <vector>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  "FluxSecStorage", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "FluxSecStorage", __VA_ARGS__)

// External — provided by flux_window_android.cpp / flux_app_android.cpp
extern JNIEnv*        getJNIEnv();
extern ANativeActivity* s_activity;

// ============================================================================
// Base64 (no OpenSSL on Android client side — use simple impl)
// ============================================================================

namespace {

static const char kB64Chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string b64Encode(const uint8_t* data, size_t len) {
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    for (size_t i = 0; i < len; i += 3) {
        uint32_t val = (uint32_t)data[i] << 16;
        if (i+1 < len) val |= (uint32_t)data[i+1] << 8;
        if (i+2 < len) val |= (uint32_t)data[i+2];
        out += kB64Chars[(val >> 18) & 63];
        out += kB64Chars[(val >> 12) & 63];
        out += (i+1 < len) ? kB64Chars[(val >> 6) & 63] : '=';
        out += (i+2 < len) ? kB64Chars[val & 63]        : '=';
    }
    return out;
}

std::vector<uint8_t> b64Decode(const std::string& enc) {
    auto lup = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62; if (c == '/') return 63; return -1;
    };
    std::vector<uint8_t> out;
    for (size_t i = 0; i + 3 < enc.size(); i += 4) {
        int v0 = lup(enc[i]), v1 = lup(enc[i+1]);
        int v2 = lup(enc[i+2]), v3 = lup(enc[i+3]);
        if (v0 < 0 || v1 < 0) break;
        out.push_back((uint8_t)((v0 << 2) | (v1 >> 4)));
        if (v2 >= 0) out.push_back((uint8_t)((v1 << 4) | (v2 >> 2)));
        if (v3 >= 0) out.push_back((uint8_t)((v2 << 6) | v3));
    }
    return out;
}

// URL-safe base64 for file names (replace +/ with -_)
std::string b64UrlEncode(const std::string& s) {
    std::string out = b64Encode(
        reinterpret_cast<const uint8_t*>(s.data()), s.size());
    for (char& c : out) {
        if (c == '+') c = '-';
        if (c == '/') c = '_';
        if (c == '=') c = '.'; // padding
    }
    return out;
}

std::string b64UrlDecode(const std::string& enc) {
    std::string s = enc;
    for (char& c : s) {
        if (c == '-') c = '+';
        if (c == '_') c = '/';
        if (c == '.') c = '=';
    }
    auto v = b64Decode(s);
    return std::string(v.begin(), v.end());
}

} // anon

// ============================================================================
// Android Keystore JNI wrapper
// Manages one AES-256-GCM key per serviceName stored in the Android Keystore.
// ============================================================================

class AndroidKeystore {
public:
    explicit AndroidKeystore(const std::string& serviceName,
                             bool requireAuth)
        : alias_("flux_" + serviceName), requireAuth_(requireAuth) {}

    // ── Encrypt plaintext, returns base64(IV||ciphertext||tag) ───────────────
    std::string encrypt(const std::string& plaintext,
                        flux::SecureStorageError* err) {
        JNIEnv* env = getJNIEnv();
        if (!env) { setErr(err, -1, "No JNI env"); return {}; }

        ensureKey_(env);

        // Build Java call: FluxKeystore.encrypt(alias, plaintext)
        jclass  cls = getKeystoreClass_(env);
        if (!cls) { setErr(err, -1, "FluxKeystore class not found"); return {}; }

        jmethodID mid = env->GetStaticMethodID(cls, "encrypt",
            "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;");
        if (!mid) { setErr(err, -1, "encrypt method not found"); return {}; }

        jstring jAlias = env->NewStringUTF(alias_.c_str());
        jstring jPlain = env->NewStringUTF(plaintext.c_str());
        auto    jResult= (jstring)env->CallStaticObjectMethod(cls, mid, jAlias, jPlain);
        env->DeleteLocalRef(jAlias); env->DeleteLocalRef(jPlain);

        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            setErr(err, -1, "encrypt threw exception");
            return {};
        }
        if (!jResult) { setErr(err, -1, "encrypt returned null"); return {}; }

        const char* chars = env->GetStringUTFChars(jResult, nullptr);
        std::string result(chars);
        env->ReleaseStringUTFChars(jResult, chars);
        env->DeleteLocalRef(jResult);
        return result;
    }

    // ── Decrypt base64 blob, returns plaintext ────────────────────────────────
    std::optional<std::string> decrypt(const std::string& blob,
                                       flux::SecureStorageError* err) {
        JNIEnv* env = getJNIEnv();
        if (!env) { setErr(err, -1, "No JNI env"); return std::nullopt; }

        jclass  cls = getKeystoreClass_(env);
        if (!cls) { setErr(err, -1, "FluxKeystore class not found"); return std::nullopt; }

        jmethodID mid = env->GetStaticMethodID(cls, "decrypt",
            "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;");
        if (!mid) { setErr(err, -1, "decrypt method not found"); return std::nullopt; }

        jstring jAlias = env->NewStringUTF(alias_.c_str());
        jstring jBlob  = env->NewStringUTF(blob.c_str());
        auto    jResult= (jstring)env->CallStaticObjectMethod(cls, mid, jAlias, jBlob);
        env->DeleteLocalRef(jAlias); env->DeleteLocalRef(jBlob);

        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            setErr(err, -1, "decrypt threw exception");
            return std::nullopt;
        }
        if (!jResult) return std::nullopt;

        const char* chars = env->GetStringUTFChars(jResult, nullptr);
        std::string result(chars);
        env->ReleaseStringUTFChars(jResult, chars);
        env->DeleteLocalRef(jResult);
        return result;
    }

    void deleteKey(JNIEnv* env) {
        jclass cls = getKeystoreClass_(env);
        if (!cls) return;
        jmethodID mid = env->GetStaticMethodID(cls, "deleteKey",
            "(Ljava/lang/String;)V");
        if (!mid) return;
        jstring jAlias = env->NewStringUTF(alias_.c_str());
        env->CallStaticVoidMethod(cls, mid, jAlias);
        env->DeleteLocalRef(jAlias);
        env->ExceptionClear();
    }

private:
    std::string alias_;
    bool        requireAuth_;
    bool        keyEnsured_ = false;

    void ensureKey_(JNIEnv* env) {
        if (keyEnsured_) return;
        jclass cls = getKeystoreClass_(env);
        if (!cls) return;
        jmethodID mid = env->GetStaticMethodID(cls, "ensureKey",
            "(Ljava/lang/String;Z)V");
        if (!mid) return;
        jstring jAlias = env->NewStringUTF(alias_.c_str());
        env->CallStaticVoidMethod(cls, mid, jAlias, (jboolean)requireAuth_);
        env->DeleteLocalRef(jAlias);
        if (env->ExceptionCheck()) env->ExceptionClear();
        else keyEnsured_ = true;
    }

    static jclass getKeystoreClass_(JNIEnv* env) {
        return env->FindClass("com/flux/security/FluxKeystore");
    }

    static void setErr(flux::SecureStorageError* e, int c, const char* m) {
        if (e) { e->code = c; e->message = m; }
    }
};

// ============================================================================
// File store — one file per key under filesDir/flux_secure/<service>/
// ============================================================================

class AndroidFileStore {
public:
    explicit AndroidFileStore(const std::string& serviceName)
        : serviceName_(serviceName) {
        dir_ = getFilesDir_() + "/flux_secure/" + serviceName + "/";
        mkdir((getFilesDir_() + "/flux_secure").c_str(), 0700);
        mkdir(dir_.c_str(), 0700);
    }

    bool write(const std::string& key, const std::string& encBlob,
               flux::SecureStorageError* err) {
        std::string path = pathFor_(key);
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        if (!f.is_open()) {
            if (err) { err->code = -1; err->message = "Cannot write " + path; }
            return false;
        }
        f.write(encBlob.data(), (std::streamsize)encBlob.size());
        chmod(path.c_str(), 0600);
        return true;
    }

    std::optional<std::string> read(const std::string& key) {
        std::ifstream f(pathFor_(key), std::ios::binary);
        if (!f.is_open()) return std::nullopt;
        return std::string(std::istreambuf_iterator<char>(f), {});
    }

    bool del(const std::string& key, flux::SecureStorageError*) {
        return remove(pathFor_(key).c_str()) == 0;
    }

    void delAll() {
        for (auto& k : allKeys()) del(k, nullptr);
    }

    bool contains(const std::string& key) {
        return access(pathFor_(key).c_str(), F_OK) == 0;
    }

    std::vector<std::string> allKeys() {
        std::vector<std::string> keys;
        DIR* d = opendir(dir_.c_str());
        if (!d) return keys;
        struct dirent* ent;
        while ((ent = readdir(d)) != nullptr) {
            std::string name = ent->d_name;
            if (name == "." || name == "..") continue;
            if (name.size() > 4 && name.substr(name.size() - 4) == ".enc")
                keys.push_back(b64UrlDecode(name.substr(0, name.size() - 4)));
        }
        closedir(d);
        return keys;
    }

private:
    std::string serviceName_;
    std::string dir_;

    std::string pathFor_(const std::string& key) const {
        return dir_ + b64UrlEncode(key) + ".enc";
    }

    static std::string getFilesDir_() {
        JNIEnv* env = getJNIEnv();
        if (!env || !s_activity) return "/data/local/tmp";
        return s_activity->internalDataPath
               ? std::string(s_activity->internalDataPath)
               : "/data/local/tmp";
    }
};

// ============================================================================
// Android Impl
// ============================================================================

namespace flux {

struct FluxSecureStorage::Impl {
    SecureStorageOptions opts;
    mutable std::mutex   mtx;
    AndroidKeystore      keystore;
    AndroidFileStore     fileStore;

    explicit Impl(SecureStorageOptions o)
        : opts(o),
          keystore(o.serviceName, o.androidRequireAuthentication),
          fileStore(o.serviceName) {}

    bool writeSync(const std::string& k, const std::string& v,
                   SecureStorageError* err) {
        std::lock_guard<std::mutex> lk(mtx);
        std::string blob = keystore.encrypt(v, err);
        if (blob.empty()) return false;
        return fileStore.write(k, blob, err);
    }

    std::optional<std::string> readSync(const std::string& k,
                                        SecureStorageError* err) {
        std::lock_guard<std::mutex> lk(mtx);
        auto blob = fileStore.read(k);
        if (!blob) return std::nullopt;
        return keystore.decrypt(*blob, err);
    }

    std::map<std::string,std::string> readAllSync(SecureStorageError* err) {
        std::lock_guard<std::mutex> lk(mtx);
        std::map<std::string,std::string> result;
        for (auto& k : fileStore.allKeys()) {
            auto v = readSync(k, err);
            if (v) result[k] = *v;
        }
        return result;
    }

    bool deleteKeySync(const std::string& k, SecureStorageError* err) {
        std::lock_guard<std::mutex> lk(mtx);
        return fileStore.del(k, err);
    }

    bool deleteAllSync(SecureStorageError* err) {
        std::lock_guard<std::mutex> lk(mtx);
        fileStore.delAll();
        JNIEnv* env = getJNIEnv();
        if (env) keystore.deleteKey(env);
        (void)err;
        return true;
    }

    bool containsKeySync(const std::string& k, SecureStorageError*) {
        std::lock_guard<std::mutex> lk(mtx);
        return fileStore.contains(k);
    }
};

// ── Public API ────────────────────────────────────────────────────────────────

FluxSecureStorage::FluxSecureStorage(SecureStorageOptions opts)
    : impl_(new Impl(std::move(opts))) {}

FluxSecureStorage::~FluxSecureStorage() { delete impl_; }

bool FluxSecureStorage::writeSync(const std::string& k, const std::string& v,
                                  SecureStorageError* err) {
    return impl_->writeSync(k, v, err);
}
std::optional<std::string> FluxSecureStorage::readSync(const std::string& k,
                                                       SecureStorageError* err) {
    return impl_->readSync(k, err);
}
std::map<std::string,std::string>
FluxSecureStorage::readAllSync(SecureStorageError* err) {
    return impl_->readAllSync(err);
}
bool FluxSecureStorage::deleteKeySync(const std::string& k,
                                      SecureStorageError* err) {
    return impl_->deleteKeySync(k, err);
}
bool FluxSecureStorage::deleteAllSync(SecureStorageError* err) {
    return impl_->deleteAllSync(err);
}
bool FluxSecureStorage::containsKeySync(const std::string& k,
                                        SecureStorageError* err) {
    return impl_->containsKeySync(k, err);
}

namespace {
void runAsync(std::function<void()> work) {
    std::thread([w = std::move(work)]() mutable { w(); }).detach();
}
} // anon

void FluxSecureStorage::write(const std::string& k, const std::string& v,
                              WriteCallback cb) {
    auto* impl = impl_;
    runAsync([impl, k, v, cb]() {
        SecureStorageError err;
        bool ok = impl->writeSync(k, v, &err);
        if (cb) cb(ok, err);
    });
}
void FluxSecureStorage::read(const std::string& k, ReadCallback cb) {
    auto* impl = impl_;
    runAsync([impl, k, cb]() {
        SecureStorageError err;
        auto val = impl->readSync(k, &err);
        if (cb) cb(val, err);
    });
}
void FluxSecureStorage::readAll(ReadAllCallback cb) {
    auto* impl = impl_;
    runAsync([impl, cb]() {
        SecureStorageError err;
        auto all = impl->readAllSync(&err);
        if (cb) cb(all, err);
    });
}
void FluxSecureStorage::deleteKey(const std::string& k, DeleteCallback cb) {
    auto* impl = impl_;
    runAsync([impl, k, cb]() {
        SecureStorageError err;
        bool ok = impl->deleteKeySync(k, &err);
        if (cb) cb(ok, err);
    });
}
void FluxSecureStorage::deleteAll(DeleteCallback cb) {
    auto* impl = impl_;
    runAsync([impl, cb]() {
        SecureStorageError err;
        bool ok = impl->deleteAllSync(&err);
        if (cb) cb(ok, err);
    });
}
void FluxSecureStorage::containsKey(const std::string& k, ContainsCallback cb) {
    auto* impl = impl_;
    runAsync([impl, k, cb]() {
        SecureStorageError err;
        bool has = impl->containsKeySync(k, &err);
        if (cb) cb(has, err);
    });
}

} // namespace flux

#endif // __ANDROID__