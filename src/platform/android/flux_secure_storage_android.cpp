// flux_secure_storage_android.cpp
// Android backend — pure C++, no Java required.
//
// Uses AES-256-GCM (same as Linux fallback) with a key derived from the
// app's internal data path + package-unique salt via PBKDF2-SHA256.
//
// Storage: <internalDataPath>/flux_secure/<serviceName>/<b64url(key)>.enc
// Each key is a separate file — owner-only (0600), sandboxed by Android.
// No permissions required. Works from API 21+.
//
// Depends on: mbedTLS (already in your CMake via FetchContent for Android)

#ifdef __ANDROID__

#include "flux/flux_secure_storage.hpp"

#include <android/log.h>
#include <android_native_app_glue.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>

#include <cstring>
#include <fstream>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

// mbedTLS headers (available via FetchContent in your CMakeLists.txt)
#include <mbedtls/gcm.h>
#include <mbedtls/pkcs5.h>
#include <mbedtls/md.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  "FluxSecStorage", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "FluxSecStorage", __VA_ARGS__)

// External — already defined in flux_app_android.cpp
extern ANativeActivity* s_activity;

// ============================================================================
// AES-256-GCM via mbedTLS
// ============================================================================

static constexpr int kKeyLen = 32; // 256-bit
static constexpr int kIVLen  = 12; // 96-bit GCM IV
static constexpr int kTagLen = 16; // 128-bit GCM tag

// Derive a 256-bit key from a secret + salt using PBKDF2-SHA256
static std::vector<uint8_t> deriveKey(const std::string& secret,
                                      const std::string& salt) {
    std::vector<uint8_t> key(kKeyLen);
    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 1);
    mbedtls_pkcs5_pbkdf2_hmac(
        &ctx,
        reinterpret_cast<const unsigned char*>(secret.data()), secret.size(),
        reinterpret_cast<const unsigned char*>(salt.data()),   salt.size(),
        100000,   // iterations
        kKeyLen, key.data());
    mbedtls_md_free(&ctx);
    return key;
}

// Encrypt — returns [IV(12)][ciphertext][tag(16)] or empty on failure
static std::vector<uint8_t> aesGcmEncrypt(const std::vector<uint8_t>& key,
                                          const std::string& plaintext) {
    // Generate random IV
    std::vector<uint8_t> iv(kIVLen);
    mbedtls_entropy_context  entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&ctr_drbg);
    const char* pers = "flux_secure_storage";
    int ret = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy,
                                     reinterpret_cast<const unsigned char*>(pers),
                                     strlen(pers));
    if (ret != 0) {
        mbedtls_entropy_free(&entropy);
        mbedtls_ctr_drbg_free(&ctr_drbg);
        return {};
    }
    mbedtls_ctr_drbg_random(&ctr_drbg, iv.data(), kIVLen);
    mbedtls_entropy_free(&entropy);
    mbedtls_ctr_drbg_free(&ctr_drbg);

    std::vector<uint8_t> ciphertext(plaintext.size());
    std::vector<uint8_t> tag(kTagLen);

    mbedtls_gcm_context gcm;
    mbedtls_gcm_init(&gcm);
    ret = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key.data(), 256);
    if (ret != 0) { mbedtls_gcm_free(&gcm); return {}; }

    ret = mbedtls_gcm_crypt_and_tag(
        &gcm, MBEDTLS_GCM_ENCRYPT,
        plaintext.size(),
        iv.data(), kIVLen,
        nullptr, 0,
        reinterpret_cast<const unsigned char*>(plaintext.data()),
        ciphertext.data(),
        kTagLen, tag.data());
    mbedtls_gcm_free(&gcm);
    if (ret != 0) return {};

    // Pack: IV || ciphertext || tag
    std::vector<uint8_t> out;
    out.reserve(kIVLen + ciphertext.size() + kTagLen);
    out.insert(out.end(), iv.begin(),         iv.end());
    out.insert(out.end(), ciphertext.begin(), ciphertext.end());
    out.insert(out.end(), tag.begin(),        tag.end());
    return out;
}

// Decrypt — input is [IV(12)][ciphertext][tag(16)]
static std::string aesGcmDecrypt(const std::vector<uint8_t>& key,
                                 const std::vector<uint8_t>& blob) {
    if (blob.size() < (size_t)(kIVLen + kTagLen)) return {};

    const uint8_t* iv         = blob.data();
    const uint8_t* ciphertext = blob.data() + kIVLen;
    size_t         cipherLen  = blob.size() - kIVLen - kTagLen;
    const uint8_t* tag        = blob.data() + kIVLen + cipherLen;

    std::vector<uint8_t> plain(cipherLen);

    mbedtls_gcm_context gcm;
    mbedtls_gcm_init(&gcm);
    int ret = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key.data(), 256);
    if (ret != 0) { mbedtls_gcm_free(&gcm); return {}; }

    ret = mbedtls_gcm_auth_decrypt(
        &gcm, cipherLen,
        iv, kIVLen,
        nullptr, 0,
        tag, kTagLen,
        ciphertext,
        plain.data());
    mbedtls_gcm_free(&gcm);

    if (ret != 0) {
        LOGE("aesGcmDecrypt: auth failed ret=%d", ret);
        return {};
    }
    return std::string(reinterpret_cast<char*>(plain.data()), plain.size());
}

// ============================================================================
// Base64 URL-safe (for file names)
// ============================================================================

static const char kB64[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static std::string b64Encode(const uint8_t* data, size_t len) {
    std::string out;
    for (size_t i = 0; i < len; i += 3) {
        uint32_t v = (uint32_t)data[i] << 16;
        if (i+1 < len) v |= (uint32_t)data[i+1] << 8;
        if (i+2 < len) v |= (uint32_t)data[i+2];
        out += kB64[(v>>18)&63];
        out += kB64[(v>>12)&63];
        out += (i+1<len) ? kB64[(v>>6)&63]  : '=';
        out += (i+2<len) ? kB64[v&63]        : '=';
    }
    // make URL-safe for filenames
    for (char& c : out) {
        if (c == '+') c = '-';
        if (c == '/') c = '_';
        if (c == '=') c = '.';
    }
    return out;
}

static std::vector<uint8_t> b64Decode(std::string s) {
    for (char& c : s) {
        if (c == '-') c = '+';
        if (c == '_') c = '/';
        if (c == '.') c = '=';
    }
    auto lup = [](char c) -> int {
        if (c>='A'&&c<='Z') return c-'A';
        if (c>='a'&&c<='z') return c-'a'+26;
        if (c>='0'&&c<='9') return c-'0'+52;
        if (c=='+') return 62; if (c=='/') return 63; return -1;
    };
    std::vector<uint8_t> out;
    for (size_t i = 0; i+3 < s.size(); i += 4) {
        int v0=lup(s[i]),v1=lup(s[i+1]),v2=lup(s[i+2]),v3=lup(s[i+3]);
        if (v0<0||v1<0) break;
        out.push_back((uint8_t)((v0<<2)|(v1>>4)));
        if (v2>=0) out.push_back((uint8_t)((v1<<4)|(v2>>2)));
        if (v3>=0) out.push_back((uint8_t)((v2<<6)|v3));
    }
    return out;
}

// ============================================================================
// File store — one .enc file per key
// ============================================================================

class AndroidFileStore {
public:
    explicit AndroidFileStore(const std::string& serviceName) {
        // Use internalDataPath if available, fallback to /data/local/tmp
        std::string base = s_activity && s_activity->internalDataPath
                           ? s_activity->internalDataPath
                           : "/data/local/tmp";

        dir_ = base + "/flux_secure/" + serviceName + "/";

        mkdir((base + "/flux_secure").c_str(), 0700);
        mkdir(dir_.c_str(), 0700);

        LOGI("FileStore dir: %s", dir_.c_str());
    }

    bool write(const std::string& key, const std::vector<uint8_t>& blob,
               flux::SecureStorageError* err) {
        std::string path = pathFor(key);
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        if (!f.is_open()) {
            LOGE("write: cannot open %s", path.c_str());
            if (err) { err->code = -1; err->message = "Cannot write file: " + path; }
            return false;
        }
        f.write(reinterpret_cast<const char*>(blob.data()), (std::streamsize)blob.size());
        chmod(path.c_str(), 0600);
        LOGI("write: OK key=%s bytes=%zu", key.c_str(), blob.size());
        return true;
    }

    std::optional<std::vector<uint8_t>> read(const std::string& key) {
        std::string path = pathFor(key);
        std::ifstream f(path, std::ios::binary);
        if (!f.is_open()) {
            LOGI("read: key not found: %s", key.c_str());
            return std::nullopt;
        }
        std::vector<uint8_t> data(
            (std::istreambuf_iterator<char>(f)),
            std::istreambuf_iterator<char>());
        LOGI("read: key=%s bytes=%zu", key.c_str(), data.size());
        return data;
    }

    bool del(const std::string& key) {
        return remove(pathFor(key).c_str()) == 0;
    }

    void delAll() {
        for (auto& k : allKeys()) del(k);
    }

    bool contains(const std::string& key) {
        return access(pathFor(key).c_str(), F_OK) == 0;
    }

    std::vector<std::string> allKeys() {
        std::vector<std::string> keys;
        DIR* d = opendir(dir_.c_str());
        if (!d) return keys;
        struct dirent* ent;
        while ((ent = readdir(d)) != nullptr) {
            std::string name = ent->d_name;
            if (name == "." || name == "..") continue;
            if (name.size() > 4 && name.substr(name.size()-4) == ".enc") {
                auto raw = b64Decode(name.substr(0, name.size()-4));
                keys.push_back(std::string(raw.begin(), raw.end()));
            }
        }
        closedir(d);
        return keys;
    }

    std::string pathFor(const std::string& key) const {
        return dir_ + b64Encode(reinterpret_cast<const uint8_t*>(key.data()),
                                key.size()) + ".enc";
    }

private:
    std::string dir_;
};

// ============================================================================
// Impl
// ============================================================================

namespace flux {

struct FluxSecureStorage::Impl {
    SecureStorageOptions     opts;
    mutable std::mutex       mtx;
    std::vector<uint8_t>     key_;
    AndroidFileStore         store_;

    explicit Impl(SecureStorageOptions o)
        : opts(o), store_(o.serviceName) {
        // Derive key from internalDataPath — stable per app install
        std::string secret = s_activity && s_activity->internalDataPath
                             ? std::string(s_activity->internalDataPath)
                             : "flux_android_fallback";
        std::string salt = "flux_secure_storage_v1_" + o.serviceName;
        key_ = deriveKey(secret, salt);
        LOGI("Impl: key derived for service=%s", o.serviceName.c_str());
    }

    bool writeSync(const std::string& key, const std::string& value,
                   SecureStorageError* err) {
        std::lock_guard<std::mutex> lk(mtx);
        auto blob = aesGcmEncrypt(key_, value);
        if (blob.empty()) {
            LOGE("writeSync: encrypt failed for key=%s", key.c_str());
            if (err) { err->code = -1; err->message = "Encrypt failed"; }
            return false;
        }
        return store_.write(key, blob, err);
    }

    std::optional<std::string> readSync(const std::string& key,
                                        SecureStorageError* err) {
        std::lock_guard<std::mutex> lk(mtx);
        auto blob = store_.read(key);
        if (!blob) return std::nullopt;
        std::string plain = aesGcmDecrypt(key_, *blob);
        if (plain.empty() && !blob->empty()) {
            LOGE("readSync: decrypt failed for key=%s", key.c_str());
            if (err) { err->code = -1; err->message = "Decrypt failed"; }
            return std::nullopt;
        }
        return plain;
    }

    std::map<std::string,std::string> readAllSync(SecureStorageError* err) {
        std::lock_guard<std::mutex> lk(mtx);
        std::map<std::string,std::string> result;
        for (auto& k : store_.allKeys()) {
            auto blob = store_.read(k);
            if (!blob) continue;
            std::string plain = aesGcmDecrypt(key_, *blob);
            if (!plain.empty() || blob->size() == (size_t)(kIVLen + kTagLen))
                result[k] = plain;
        }
        return result;
    }

    bool deleteKeySync(const std::string& key, SecureStorageError* err) {
        std::lock_guard<std::mutex> lk(mtx);
        bool ok = store_.del(key);
        if (!ok && err) { err->code = -1; err->message = "Delete failed"; }
        return ok;
    }

    bool deleteAllSync(SecureStorageError*) {
        std::lock_guard<std::mutex> lk(mtx);
        store_.delAll();
        return true;
    }

    bool containsKeySync(const std::string& key, SecureStorageError*) {
        std::lock_guard<std::mutex> lk(mtx);
        return store_.contains(key);
    }
};

// ── Public API ────────────────────────────────────────────────────────────────

FluxSecureStorage::FluxSecureStorage(SecureStorageOptions opts)
    : impl_(new Impl(std::move(opts))) {}

FluxSecureStorage::~FluxSecureStorage() { delete impl_; }

bool FluxSecureStorage::writeSync(const std::string& k, const std::string& v,
                                  SecureStorageError* e) { return impl_->writeSync(k,v,e); }
std::optional<std::string> FluxSecureStorage::readSync(const std::string& k,
                                                       SecureStorageError* e) { return impl_->readSync(k,e); }
std::map<std::string,std::string> FluxSecureStorage::readAllSync(SecureStorageError* e) { return impl_->readAllSync(e); }
bool FluxSecureStorage::deleteKeySync(const std::string& k, SecureStorageError* e) { return impl_->deleteKeySync(k,e); }
bool FluxSecureStorage::deleteAllSync(SecureStorageError* e) { return impl_->deleteAllSync(e); }
bool FluxSecureStorage::containsKeySync(const std::string& k, SecureStorageError* e) { return impl_->containsKeySync(k,e); }

namespace { void runAsync(std::function<void()> w) { std::thread(std::move(w)).detach(); } }

void FluxSecureStorage::write(const std::string& k, const std::string& v, WriteCallback cb) {
    auto* i=impl_; runAsync([i,k,v,cb](){ SecureStorageError e; bool ok=i->writeSync(k,v,&e); if(cb)cb(ok,e); });
}
void FluxSecureStorage::read(const std::string& k, ReadCallback cb) {
    auto* i=impl_; runAsync([i,k,cb](){ SecureStorageError e; auto v=i->readSync(k,&e); if(cb)cb(v,e); });
}
void FluxSecureStorage::readAll(ReadAllCallback cb) {
    auto* i=impl_; runAsync([i,cb](){ SecureStorageError e; auto a=i->readAllSync(&e); if(cb)cb(a,e); });
}
void FluxSecureStorage::deleteKey(const std::string& k, DeleteCallback cb) {
    auto* i=impl_; runAsync([i,k,cb](){ SecureStorageError e; bool ok=i->deleteKeySync(k,&e); if(cb)cb(ok,e); });
}
void FluxSecureStorage::deleteAll(DeleteCallback cb) {
    auto* i=impl_; runAsync([i,cb](){ SecureStorageError e; bool ok=i->deleteAllSync(&e); if(cb)cb(ok,e); });
}
void FluxSecureStorage::containsKey(const std::string& k, ContainsCallback cb) {
    auto* i=impl_; runAsync([i,k,cb](){ SecureStorageError e; bool h=i->containsKeySync(k,&e); if(cb)cb(h,e); });
}

} // namespace flux

#endif // __ANDROID__