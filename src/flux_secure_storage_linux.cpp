// flux_secure_storage_linux.cpp
// Linux backend:
//   Primary  → libsecret (GNOME Keyring / KWallet via Secret Service API)
//   Fallback → AES-256-GCM encrypted JSON file (machine-key derived via PBKDF2)
//
// libsecret is loaded at runtime with dlopen so the binary still works on
// systems without libsecret installed (it will automatically fall back).

#if defined(__linux__) && !defined(__ANDROID__)

#include "flux/flux_secure_storage.hpp"
#include "flux/flux_secure_crypto.hpp"

#include <dlfcn.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <thread>

// ============================================================================
// Minimal libsecret vtable loaded at runtime
// ============================================================================

// We only need a tiny subset of the libsecret C API.
// Types are opaque pointers to avoid needing the headers at compile time.

typedef void  GError;
typedef void  SecretSchema;
typedef void  SecretCollection;

typedef int           gboolean;
typedef unsigned int  guint;
typedef void*         gpointer;

// Attribute type enum value we need
static constexpr int kSecretSchemaAttributeString = 0;
static constexpr int kSecretSchemaFlagsNone       = 0;

struct LibSecretVtable {
    void* handle = nullptr;

    // secret_password_store_sync / lookup_sync / clear_sync
    gboolean (*password_store_sync)(const SecretSchema*, const char*,
                                    const char*, const char*, void*,
                                    GError**, ...) = nullptr;

    char*    (*password_lookup_sync)(const SecretSchema*, void*,
                                     GError**, ...) = nullptr;

    gboolean (*password_clear_sync)(const SecretSchema*, void*,
                                    GError**, ...) = nullptr;

    void     (*password_free)(char*) = nullptr;

    void     (*error_free)(GError*) = nullptr;

    // secret_schema_new / unref
    SecretSchema* (*schema_new)(const char*, int, ...) = nullptr;
    void          (*schema_unref)(SecretSchema*)       = nullptr;

    bool load() {
        // Try versioned sonames first (Debian/Ubuntu)
        for (const char* name : {"libsecret-1.so.0",
                                  "libsecret-1.so",
                                  "libsecret.so.0",
                                  "libsecret.so"}) {
            handle = dlopen(name, RTLD_LAZY | RTLD_LOCAL);
            if (handle) break;
        }
        if (!handle) return false;

        auto sym = [&](const char* s) { return dlsym(handle, s); };

        password_store_sync  = (decltype(password_store_sync)) sym("secret_password_store_sync");
        password_lookup_sync = (decltype(password_lookup_sync))sym("secret_password_lookup_sync");
        password_clear_sync  = (decltype(password_clear_sync)) sym("secret_password_clear_sync");
        password_free        = (decltype(password_free))        sym("secret_password_free");
        error_free           = (decltype(error_free))           sym("g_error_free");
        schema_new           = (decltype(schema_new))           sym("secret_schema_new");
        schema_unref         = (decltype(schema_unref))         sym("secret_schema_unref");

        return password_store_sync  != nullptr &&
               password_lookup_sync != nullptr &&
               password_clear_sync  != nullptr;
    }

    ~LibSecretVtable() { if (handle) dlclose(handle); }
};

// ============================================================================
// JSON mini-serialiser (no dependency)
// Only stores flat string maps — sufficient for key/value storage.
// ============================================================================

namespace minijson {

// Escape a UTF-8 string for JSON embedding
static std::string escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 4);
    for (unsigned char c : s) {
        if      (c == '"')  out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else if (c < 0x20) {
            char buf[8];
            snprintf(buf, sizeof(buf), "\\u%04x", c);
            out += buf;
        } else {
            out += (char)c;
        }
    }
    return out;
}

static std::string serialise(const std::map<std::string,std::string>& m) {
    std::string j = "{";
    bool first = true;
    for (auto& [k, v] : m) {
        if (!first) j += ',';
        first = false;
        j += '"'; j += escape(k); j += "\":\""; j += escape(v); j += '"';
    }
    j += '}';
    return j;
}

// Minimal pull parser — handles flat {string:string} maps only.
static std::map<std::string,std::string> parse(const std::string& json) {
    std::map<std::string,std::string> m;
    size_t i = 0;
    auto skip = [&]() { while (i < json.size() && json[i] <= ' ') ++i; };
    auto readStr = [&]() -> std::string {
        std::string s;
        if (i >= json.size() || json[i] != '"') return s;
        ++i;
        while (i < json.size() && json[i] != '"') {
            if (json[i] == '\\') {
                ++i;
                if (i >= json.size()) break;
                switch (json[i]) {
                    case '"':  s += '"';  break;
                    case '\\': s += '\\'; break;
                    case 'n':  s += '\n'; break;
                    case 'r':  s += '\r'; break;
                    case 't':  s += '\t'; break;
                    default:   s += json[i]; break;
                }
            } else {
                s += json[i];
            }
            ++i;
        }
        if (i < json.size()) ++i; // closing "
        return s;
    };
    skip(); if (i >= json.size() || json[i] != '{') return m; ++i;
    while (true) {
        skip(); if (i >= json.size() || json[i] == '}') break;
        std::string key = readStr();
        skip(); if (i < json.size() && json[i] == ':') ++i;
        skip(); std::string val = readStr();
        m[key] = val;
        skip(); if (i < json.size() && json[i] == ',') ++i;
    }
    return m;
}
} // namespace minijson

// ============================================================================
// File-based fallback (AES-256-GCM encrypted JSON)
// ============================================================================

class FileFallbackBackend {
public:
    explicit FileFallbackBackend(const std::string& path,
                                 const std::string& serviceName)
        : path_(path), key_(deriveKey_(serviceName)) {}

    bool write(const std::string& k, const std::string& v,
               flux::SecureStorageError* err) {
        std::lock_guard<std::mutex> lk(mtx_);
        auto m = loadAll_();
        m[k] = v;
        return saveAll_(m, err);
    }

    std::optional<std::string> read(const std::string& k,
                                    flux::SecureStorageError*) {
        std::lock_guard<std::mutex> lk(mtx_);
        auto m = loadAll_();
        auto it = m.find(k);
        if (it == m.end()) return std::nullopt;
        return it->second;
    }

    std::map<std::string,std::string> readAll(flux::SecureStorageError*) {
        std::lock_guard<std::mutex> lk(mtx_);
        return loadAll_();
    }

    bool del(const std::string& k, flux::SecureStorageError* err) {
        std::lock_guard<std::mutex> lk(mtx_);
        auto m = loadAll_();
        m.erase(k);
        return saveAll_(m, err);
    }

    bool delAll(flux::SecureStorageError* err) {
        std::lock_guard<std::mutex> lk(mtx_);
        return saveAll_({}, err);
    }

    bool contains(const std::string& k, flux::SecureStorageError*) {
        std::lock_guard<std::mutex> lk(mtx_);
        auto m = loadAll_();
        return m.count(k) > 0;
    }

private:
    std::string              path_;
    std::vector<uint8_t>     key_;
    std::mutex               mtx_;

    static std::vector<uint8_t> deriveKey_(const std::string& service) {
        std::string secret = flux::crypto::getMachineSecret();
        std::string salt   = "flux_secure_storage_v1_" + service;
        return flux::crypto::deriveKey(secret, salt);
    }

    std::map<std::string,std::string> loadAll_() {
        std::ifstream f(path_, std::ios::binary);
        if (!f.is_open()) return {};
        std::vector<uint8_t> blob(
            (std::istreambuf_iterator<char>(f)),
            std::istreambuf_iterator<char>());
        if (blob.empty()) return {};
        std::string json = flux::crypto::aesGcmDecrypt(key_, blob);
        if (json.empty()) return {};
        return minijson::parse(json);
    }

    bool saveAll_(const std::map<std::string,std::string>& m,
                  flux::SecureStorageError* err) {
        // Ensure parent directory exists
        auto dir = path_.substr(0, path_.rfind('/'));
        if (!dir.empty()) {
            mkdir(dir.c_str(), 0700);
        }

        std::string json = minijson::serialise(m);
        auto blob = flux::crypto::aesGcmEncrypt(key_, json);
        if (blob.empty()) {
            if (err) { err->code = -1; err->message = "AES-GCM encrypt failed"; }
            return false;
        }
        // Atomic write via tmp file
        std::string tmp = path_ + ".tmp";
        {
            std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
            if (!f.is_open()) {
                if (err) { err->code = -1; err->message = "Cannot write to " + tmp; }
                return false;
            }
            f.write(reinterpret_cast<const char*>(blob.data()), (std::streamsize)blob.size());
        }
        if (rename(tmp.c_str(), path_.c_str()) != 0) {
            if (err) { err->code = errno; err->message = "Rename failed"; }
            return false;
        }
        chmod(path_.c_str(), 0600); // owner-only
        return true;
    }
};

// ============================================================================
// libsecret backend
// ============================================================================

class LibSecretBackend {
public:
    explicit LibSecretBackend(const std::string& serviceName)
        : serviceName_(serviceName) {}

    bool available() const { return vtbl_.handle != nullptr; }

    bool init() {
        if (!vtbl_.load()) return false;
        // Build schema: two attributes — "service" + "key"
        schema_ = vtbl_.schema_new(
            serviceName_.c_str(),
            kSecretSchemaFlagsNone,
            "service", kSecretSchemaAttributeString,
            "key",     kSecretSchemaAttributeString,
            nullptr);
        return schema_ != nullptr;
    }

    ~LibSecretBackend() {
        if (vtbl_.handle && schema_)
            vtbl_.schema_unref(schema_);
    }

    bool write(const std::string& k, const std::string& v,
               flux::SecureStorageError* err) {
        GError* gerr = nullptr;
        std::string label = serviceName_ + "/" + k;
        gboolean ok = vtbl_.password_store_sync(
            schema_, "default", label.c_str(), v.c_str(),
            nullptr, &gerr,
            "service", serviceName_.c_str(),
            "key",     k.c_str(),
            nullptr);
        return handleResult(ok, gerr, err);
    }

    std::optional<std::string> read(const std::string& k,
                                    flux::SecureStorageError* err) {
        GError* gerr = nullptr;
        char* secret = vtbl_.password_lookup_sync(
            schema_, nullptr, &gerr,
            "service", serviceName_.c_str(),
            "key",     k.c_str(),
            nullptr);
        if (gerr) {
            if (err) err->message = gerr ? "libsecret lookup failed" : "";
            if (vtbl_.error_free) vtbl_.error_free(gerr);
            return std::nullopt;
        }
        if (!secret) return std::nullopt;
        std::string result(secret);
        vtbl_.password_free(secret);
        return result;
    }

    // libsecret doesn't have a "list all" API for a schema directly via the
    // simple password API; we store an index key that tracks all written keys.
    std::map<std::string,std::string> readAll(flux::SecureStorageError* err) {
        std::map<std::string,std::string> result;
        // Read index
        auto indexOpt = read("__flux_key_index__", nullptr);
        if (!indexOpt || indexOpt->empty()) return result;
        // Index is comma-separated list of keys
        std::istringstream ss(*indexOpt);
        std::string key;
        while (std::getline(ss, key, ',')) {
            if (key.empty()) continue;
            auto val = read(key, err);
            if (val) result[key] = *val;
        }
        return result;
    }

    bool del(const std::string& k, flux::SecureStorageError* err) {
        GError* gerr = nullptr;
        gboolean ok = vtbl_.password_clear_sync(
            schema_, nullptr, &gerr,
            "service", serviceName_.c_str(),
            "key",     k.c_str(),
            nullptr);
        bool res = handleResult(ok, gerr, err);
        if (res) removeFromIndex_(k);
        return res;
    }

    bool delAll(flux::SecureStorageError* err) {
        auto all = readAll(nullptr);
        bool ok = true;
        for (auto& [k, _] : all) {
            flux::SecureStorageError e;
            if (!del(k, &e)) { if (err) *err = e; ok = false; }
        }
        // Clear the index key itself
        del("__flux_key_index__", nullptr);
        return ok;
    }

    bool contains(const std::string& k, flux::SecureStorageError* err) {
        return read(k, err).has_value();
    }

    // Called after a successful write to update the key index.
    void addToIndex_(const std::string& k) {
        auto cur = read("__flux_key_index__", nullptr).value_or("");
        if (cur.find(k) != std::string::npos) return;
        cur += (cur.empty() ? "" : ",") + k;
        write("__flux_key_index__", cur, nullptr);
    }

private:
    std::string     serviceName_;
    LibSecretVtable vtbl_;
    SecretSchema*   schema_ = nullptr;

    bool handleResult(gboolean ok, GError* gerr,
                      flux::SecureStorageError* err) {
        if (gerr) {
            if (err) err->message = "libsecret error";
            if (vtbl_.error_free) vtbl_.error_free(gerr);
            return false;
        }
        return ok != 0;
    }

    void removeFromIndex_(const std::string& k) {
        auto cur = read("__flux_key_index__", nullptr).value_or("");
        if (cur.empty()) return;
        std::string out;
        std::istringstream ss(cur);
        std::string token;
        while (std::getline(ss, token, ',')) {
            if (token != k) {
                if (!out.empty()) out += ',';
                out += token;
            }
        }
        write("__flux_key_index__", out, nullptr);
    }
};

// ============================================================================
// Linux Impl
// ============================================================================

namespace flux {

struct FluxSecureStorage::Impl {
    SecureStorageOptions opts;
    mutable std::mutex   mtx;

    LibSecretBackend*    libsecret   = nullptr;
    FileFallbackBackend* fileFallback= nullptr;

    explicit Impl(SecureStorageOptions o) : opts(std::move(o)) {

        auto* ls = new LibSecretBackend(opts.serviceName);
        if (ls->init()) {
            libsecret = ls;
        } else {
            delete ls;
            // Build fallback path
            std::string path = opts.linuxFallbackPath;
            if (path.empty()) {
                const char* xdg = getenv("XDG_DATA_HOME");
                std::string base = xdg ? xdg :
                    (std::string(getenv("HOME") ? getenv("HOME") : "/tmp") + "/.local/share");
                path = base + "/" + opts.serviceName + "/secure.db";
            }
            fileFallback = new FileFallbackBackend(path, opts.serviceName);
        }
    }

    ~Impl() {
        delete libsecret;
        delete fileFallback;
    }

    bool writeSync(const std::string& k, const std::string& v,
                   SecureStorageError* err) {
        std::lock_guard<std::mutex> lk(mtx);
        if (libsecret) {
            bool ok = libsecret->write(k, v, err);
            if (ok) libsecret->addToIndex_(k);
            return ok;
        }
        return fileFallback->write(k, v, err);
    }

    std::optional<std::string> readSync(const std::string& k,
                                        SecureStorageError* err) {
        std::lock_guard<std::mutex> lk(mtx);
        if (libsecret) return libsecret->read(k, err);
        return fileFallback->read(k, err);
    }

    std::map<std::string,std::string> readAllSync(SecureStorageError* err) {
        std::lock_guard<std::mutex> lk(mtx);
        if (libsecret) return libsecret->readAll(err);
        return fileFallback->readAll(err);
    }

    bool deleteKeySync(const std::string& k, SecureStorageError* err) {
        std::lock_guard<std::mutex> lk(mtx);
        if (libsecret) return libsecret->del(k, err);
        return fileFallback->del(k, err);
    }

    bool deleteAllSync(SecureStorageError* err) {
        std::lock_guard<std::mutex> lk(mtx);
        if (libsecret) return libsecret->delAll(err);
        return fileFallback->delAll(err);
    }

    bool containsKeySync(const std::string& k, SecureStorageError* err) {
        std::lock_guard<std::mutex> lk(mtx);
        if (libsecret) return libsecret->contains(k, err);
        return fileFallback->contains(k, err);
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

// ── Async ─────────────────────────────────────────────────────────────────────

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

#endif // linux desktop