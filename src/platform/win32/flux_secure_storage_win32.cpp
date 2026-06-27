// flux_secure_storage_win32.cpp


#ifdef _WIN32

#include "flux/flux_secure_storage.hpp"

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincrypt.h>
#pragma comment(lib, "Crypt32.lib")

#include <stdexcept>
#include <thread>
#include <mutex>

// ── Base64 (Windows CryptBinaryToString / CryptStringToBinary) ───────────────

static std::string win32Base64Encode(const BYTE* data, DWORD len) {
    DWORD outLen = 0;
    CryptBinaryToStringA(data, len, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF,
                         nullptr, &outLen);
    std::string out(outLen, '\0');
    CryptBinaryToStringA(data, len, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF,
                         out.data(), &outLen);
    // strip null terminator that CryptBinaryToString adds
    while (!out.empty() && out.back() == '\0') out.pop_back();
    return out;
}

static std::vector<BYTE> win32Base64Decode(const std::string& encoded) {
    DWORD outLen = 0;
    CryptStringToBinaryA(encoded.c_str(), (DWORD)encoded.size(),
                         CRYPT_STRING_BASE64, nullptr, &outLen, nullptr, nullptr);
    std::vector<BYTE> out(outLen);
    CryptStringToBinaryA(encoded.c_str(), (DWORD)encoded.size(),
                         CRYPT_STRING_BASE64, out.data(), &outLen, nullptr, nullptr);
    out.resize(outLen);
    return out;
}

// ── Registry helpers ──────────────────────────────────────────────────────────

static std::wstring toWide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
    if (!w.empty() && w.back() == L'\0') w.pop_back();
    return w;
}

static std::string toUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1,
                                nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1,
                        s.data(), n, nullptr, nullptr);
    if (!s.empty() && s.back() == '\0') s.pop_back();
    return s;
}

static std::wstring registryPath(const std::string& serviceName) {
    return L"Software\\" + toWide(serviceName) + L"\\SecureStorage";
}

static HKEY openOrCreateKey(const std::string& serviceName, REGSAM access) {
    std::wstring path = registryPath(serviceName);
    HKEY hKey = nullptr;
    DWORD disp = 0;
    LONG res = RegCreateKeyExW(HKEY_CURRENT_USER, path.c_str(), 0,
                                nullptr, REG_OPTION_NON_VOLATILE,
                                access, nullptr, &hKey, &disp);
    if (res != ERROR_SUCCESS) return nullptr;
    return hKey;
}

// ── DPAPI encrypt / decrypt ───────────────────────────────────────────────────

static std::string dpapiEncrypt(const std::string& plaintext,
                                const std::string& description,
                                flux::SecureStorageError* err) {
    DATA_BLOB in{};
    in.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(plaintext.data()));
    in.cbData = static_cast<DWORD>(plaintext.size());

    std::wstring descW = toWide(description);
    DATA_BLOB out{};

    if (!CryptProtectData(&in, descW.c_str(), nullptr, nullptr, nullptr,
                          0, &out)) {
        if (err) {
            err->code    = static_cast<int>(GetLastError());
            err->message = "CryptProtectData failed";
        }
        return {};
    }

    std::string encoded = win32Base64Encode(out.pbData, out.cbData);
    LocalFree(out.pbData);
    return encoded;
}

static std::string dpapiDecrypt(const std::string& base64Blob,
                                flux::SecureStorageError* err) {
    auto blob = win32Base64Decode(base64Blob);
    if (blob.empty()) {
        if (err) { err->code = -1; err->message = "Empty or invalid base64"; }
        return {};
    }

    DATA_BLOB in{};
    in.pbData = blob.data();
    in.cbData = static_cast<DWORD>(blob.size());
    DATA_BLOB out{};

    if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr,
                            0, &out)) {
        DWORD lastErr = GetLastError();
        fprintf(stderr, "[FluxSecureStorage] CryptUnprotectData failed: error %lu\n", lastErr);
        if (err) {
            err->code    = static_cast<int>(lastErr);
            err->message = "CryptUnprotectData failed (error " + std::to_string(lastErr) + ")";
        }
        return {};
    }

    std::string plaintext(reinterpret_cast<char*>(out.pbData), out.cbData);
    LocalFree(out.pbData);
    return plaintext;
}

// ── Registry read / write / delete ───────────────────────────────────────────

static bool regWrite(const std::string& serviceName,
                     const std::string& key,
                     const std::string& value,
                     flux::SecureStorageError* err) {
    HKEY hKey = openOrCreateKey(serviceName, KEY_SET_VALUE);
    if (!hKey) {
        if (err) { err->code = -1; err->message = "Cannot open registry key"; }
        return false;
    }
    std::wstring wkey = toWide(key);
    LONG res = RegSetValueExW(hKey, wkey.c_str(), 0, REG_BINARY,
                              reinterpret_cast<const BYTE*>(value.c_str()),
                              static_cast<DWORD>(value.size()));
    RegCloseKey(hKey);
    if (res != ERROR_SUCCESS) {
        if (err) { err->code = res; err->message = "RegSetValueEx failed"; }
        return false;
    }
    return true;
}

static std::optional<std::string> regRead(const std::string& serviceName,
                                          const std::string& key) {
    HKEY hKey = openOrCreateKey(serviceName, KEY_QUERY_VALUE);
    if (!hKey) return std::nullopt;

    std::wstring wkey = toWide(key);
    DWORD type  = 0;
    DWORD cbData= 0;
    if (RegQueryValueExW(hKey, wkey.c_str(), nullptr, &type,
                         nullptr, &cbData) != ERROR_SUCCESS) {
        RegCloseKey(hKey);
        return std::nullopt;
    }
    std::string val(cbData, '\0');
    RegQueryValueExW(hKey, wkey.c_str(), nullptr, &type,
                     reinterpret_cast<BYTE*>(val.data()), &cbData);
    RegCloseKey(hKey);
    val.resize(cbData); // exact byte count — no null trimming needed
    return val;
}

static std::map<std::string,std::string> regReadAll(const std::string& serviceName) {
    std::map<std::string,std::string> result;
    HKEY hKey = openOrCreateKey(serviceName, KEY_QUERY_VALUE);
    if (!hKey) return result;

    DWORD index = 0;
    WCHAR nameBuf[16384];
    DWORD nameLen = 0;
    while (true) {
        nameLen = static_cast<DWORD>(std::size(nameBuf));
        DWORD type = 0, cbData = 0;
        LONG res = RegEnumValueW(hKey, index++, nameBuf, &nameLen,
                                 nullptr, &type, nullptr, &cbData);
        if (res != ERROR_SUCCESS) break;
        std::wstring wname(nameBuf, nameLen);
        std::string val(cbData, '\0');
        DWORD nl2 = static_cast<DWORD>(std::size(nameBuf));
        RegEnumValueW(hKey, index - 1, nameBuf, &nl2, nullptr, &type,
                      reinterpret_cast<BYTE*>(val.data()), &cbData);
        val.resize(cbData); // REG_BINARY: exact byte count
        result[toUtf8(wname)] = val;
    }
    RegCloseKey(hKey);
    return result;
}

static bool regDelete(const std::string& serviceName,
                      const std::string& key,
                      flux::SecureStorageError* err) {
    HKEY hKey = openOrCreateKey(serviceName, KEY_SET_VALUE);
    if (!hKey) {
        if (err) { err->code = -1; err->message = "Cannot open registry key"; }
        return false;
    }
    std::wstring wkey = toWide(key);
    LONG res = RegDeleteValueW(hKey, wkey.c_str());
    RegCloseKey(hKey);
    if (res != ERROR_SUCCESS && res != ERROR_FILE_NOT_FOUND) {
        if (err) { err->code = res; err->message = "RegDeleteValue failed"; }
        return false;
    }
    return true;
}

static bool regDeleteAll(const std::string& serviceName,
                         flux::SecureStorageError* err) {
    std::wstring path = registryPath(serviceName);
    LONG res = RegDeleteKeyExW(HKEY_CURRENT_USER, path.c_str(), KEY_WOW64_64KEY, 0);
    if (res != ERROR_SUCCESS && res != ERROR_FILE_NOT_FOUND) {
        if (err) { err->code = res; err->message = "RegDeleteKey failed"; }
        return false;
    }
    return true;
}

// ============================================================================
// Impl
// ============================================================================

namespace flux {

struct FluxSecureStorage::Impl {
    SecureStorageOptions opts;
    mutable std::mutex   mtx; // guards synchronous operations

    explicit Impl(SecureStorageOptions o) : opts(std::move(o)) {}

    // ── Sync ops ──────────────────────────────────────────────────────────────

    bool writeSync(const std::string& key,
                   const std::string& value,
                   SecureStorageError* err) {
        std::lock_guard<std::mutex> lk(mtx);
        std::string blob = dpapiEncrypt(value, opts.serviceName + ":" + key, err);
        if (blob.empty()) return false;
        return regWrite(opts.serviceName, key, blob, err);
    }

    std::optional<std::string> readSync(const std::string& key,
                                        SecureStorageError* err) {
        std::lock_guard<std::mutex> lk(mtx);
        auto blob = regRead(opts.serviceName, key);
        if (!blob) return std::nullopt;
        SecureStorageError localErr;
        std::string plain = dpapiDecrypt(*blob, &localErr);
        if (localErr.code != 0) {
            if (err) *err = localErr;
            return std::nullopt;
        }
        return plain;
    }

    std::map<std::string,std::string> readAllSync(SecureStorageError* err) {
        std::lock_guard<std::mutex> lk(mtx);
        auto raw = regReadAll(opts.serviceName);
        std::map<std::string,std::string> result;
        for (auto& [k, b] : raw) {
            SecureStorageError localErr;
            std::string plain = dpapiDecrypt(b, &localErr);
            if (localErr.code == 0)
                result[k] = plain;
          
        }
        return result;
    }

    bool deleteKeySync(const std::string& key, SecureStorageError* err) {
        std::lock_guard<std::mutex> lk(mtx);
        return regDelete(opts.serviceName, key, err);
    }

    bool deleteAllSync(SecureStorageError* err) {
        std::lock_guard<std::mutex> lk(mtx);
        return regDeleteAll(opts.serviceName, err);
    }

    bool containsKeySync(const std::string& key, SecureStorageError* err) {
        std::lock_guard<std::mutex> lk(mtx);
        (void)err;
        return regRead(opts.serviceName, key).has_value();
    }
};

// ── Public class implementation ──────────────────────────────────────────────

FluxSecureStorage::FluxSecureStorage(SecureStorageOptions opts)
    : impl_(new Impl(std::move(opts))) {}

FluxSecureStorage::~FluxSecureStorage() { delete impl_; }

// ── Sync ─────────────────────────────────────────────────────────────────────

bool FluxSecureStorage::writeSync(const std::string& key,
                                  const std::string& value,
                                  SecureStorageError* err) {
    return impl_->writeSync(key, value, err);
}

std::optional<std::string> FluxSecureStorage::readSync(const std::string& key,
                                                       SecureStorageError* err) {
    return impl_->readSync(key, err);
}

std::map<std::string,std::string>
FluxSecureStorage::readAllSync(SecureStorageError* err) {
    return impl_->readAllSync(err);
}

bool FluxSecureStorage::deleteKeySync(const std::string& key,
                                      SecureStorageError* err) {
    return impl_->deleteKeySync(key, err);
}

bool FluxSecureStorage::deleteAllSync(SecureStorageError* err) {
    return impl_->deleteAllSync(err);
}

bool FluxSecureStorage::containsKeySync(const std::string& key,
                                        SecureStorageError* err) {
    return impl_->containsKeySync(key, err);
}



namespace {
void runAsync(std::function<void()> work) {
    std::thread([work = std::move(work)]() mutable { work(); }).detach();
}
} // anon

void FluxSecureStorage::write(const std::string& key,
                              const std::string& value,
                              WriteCallback callback) {
    auto* impl = impl_;
    runAsync([impl, key, value, cb = std::move(callback)]() {
        SecureStorageError err;
        bool ok = impl->writeSync(key, value, &err);
        if (cb) cb(ok, err);
    });
}

void FluxSecureStorage::read(const std::string& key, ReadCallback callback) {
    auto* impl = impl_;
    runAsync([impl, key, cb = std::move(callback)]() {
        SecureStorageError err;
        auto val = impl->readSync(key, &err);
        if (cb) cb(val, err);
    });
}

void FluxSecureStorage::readAll(ReadAllCallback callback) {
    auto* impl = impl_;
    runAsync([impl, cb = std::move(callback)]() {
        SecureStorageError err;
        auto all = impl->readAllSync(&err);
        if (cb) cb(all, err);
    });
}

void FluxSecureStorage::deleteKey(const std::string& key,
                                  DeleteCallback callback) {
    auto* impl = impl_;
    runAsync([impl, key, cb = std::move(callback)]() {
        SecureStorageError err;
        bool ok = impl->deleteKeySync(key, &err);
        if (cb) cb(ok, err);
    });
}

void FluxSecureStorage::deleteAll(DeleteCallback callback) {
    auto* impl = impl_;
    runAsync([impl, cb = std::move(callback)]() {
        SecureStorageError err;
        bool ok = impl->deleteAllSync(&err);
        if (cb) cb(ok, err);
    });
}

void FluxSecureStorage::containsKey(const std::string& key,
                                    ContainsCallback callback) {
    auto* impl = impl_;
    runAsync([impl, key, cb = std::move(callback)]() {
        SecureStorageError err;
        bool has = impl->containsKeySync(key, &err);
        if (cb) cb(has, err);
    });
}

} // namespace flux

#endif // _WIN32