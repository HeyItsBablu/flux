#pragma once

#ifndef FLUX_SECURE_STORAGE_HPP
#define FLUX_SECURE_STORAGE_HPP

// ============================================================================
// FluxSecureStorage — cross-platform secure key/value storage
//
// 
//    write(key, value)
//    read(key)          -> optional<string>
//    readAll()          -> map<string, string>
//    delete(key)
//    deleteAll()
//    containsKey(key)   -> bool
//
//  Platform backends:
//    Windows  → DPAPI  (CryptProtectData / CryptUnprotectData)
//    Linux    → libsecret (GNOME Keyring) with AES-256-GCM file fallback
//    Android  → Android Keystore + AES-256-GCM via JNI
// ============================================================================

#include <functional>
#include <map>
#include <optional>
#include <string>

namespace flux {

// ----------------------------------------------------------------------------
// Options — passed at construction (mirrors Flutter IOSOptions / AndroidOptions)
// ----------------------------------------------------------------------------

struct SecureStorageOptions {
    /// Logical "collection" / service name used by the keychain / keyring.
    /// Defaults to "FluxApp".
    std::string serviceName = "FluxApp";

    /// If true, on Android the key material survives a device reboot
    /// (requires API 23+, uses BIOMETRIC_STRONG or device-credential unlock).
    bool androidRequireAuthentication = false;

    /// Linux-only: if libsecret is unavailable, fall back to an
    /// AES-256-GCM encrypted file stored at this path.
    /// Leave empty to use $XDG_DATA_HOME/<serviceName>/secure.db
    std::string linuxFallbackPath;
};

// ----------------------------------------------------------------------------
// Error info returned by async callbacks
// ----------------------------------------------------------------------------

struct SecureStorageError {
    int         code    = 0;      ///< platform-specific error code
    std::string message;          ///< human-readable description
};

// ----------------------------------------------------------------------------
// Callback types (async API — called on the UI thread via invalidate())
// ----------------------------------------------------------------------------

using WriteCallback  = std::function<void(bool success, SecureStorageError)>;
using ReadCallback   = std::function<void(std::optional<std::string> value,
                                          SecureStorageError)>;
using ReadAllCallback= std::function<void(std::map<std::string,std::string>,
                                          SecureStorageError)>;
using DeleteCallback = std::function<void(bool success, SecureStorageError)>;
using ContainsCallback=std::function<void(bool contains, SecureStorageError)>;

// ----------------------------------------------------------------------------
// FluxSecureStorage
// ----------------------------------------------------------------------------

class FluxSecureStorage {
public:
    explicit FluxSecureStorage(SecureStorageOptions opts = {});
    ~FluxSecureStorage();

    // Non-copyable, non-movable (holds platform resources)
    FluxSecureStorage(const FluxSecureStorage&)             = delete;
    FluxSecureStorage& operator=(const FluxSecureStorage&)  = delete;
    FluxSecureStorage(FluxSecureStorage&&)                  = delete;
    FluxSecureStorage& operator=(FluxSecureStorage&&)       = delete;

    // ── Async API (recommended — identical call style to Flutter) ────────────

    void write(const std::string& key,
               const std::string& value,
               WriteCallback      callback = {});

    void read(const std::string& key,
              ReadCallback       callback);

    void readAll(ReadAllCallback callback);

    void deleteKey(const std::string& key,
                   DeleteCallback     callback = {});

    void deleteAll(DeleteCallback callback = {});

    void containsKey(const std::string& key,
                     ContainsCallback   callback);

    // ── Synchronous API (blocks caller thread — use off UI thread) ───────────

    bool                           writeSync(const std::string& key,
                                             const std::string& value,
                                             SecureStorageError* err = nullptr);

    std::optional<std::string>     readSync(const std::string& key,
                                            SecureStorageError* err = nullptr);

    std::map<std::string,std::string>
                                   readAllSync(SecureStorageError* err = nullptr);

    bool                           deleteKeySync(const std::string& key,
                                                 SecureStorageError* err = nullptr);

    bool                           deleteAllSync(SecureStorageError* err = nullptr);

    bool                           containsKeySync(const std::string& key,
                                                   SecureStorageError* err = nullptr);

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

} // namespace flux

#endif // FLUX_SECURE_STORAGE_HPP