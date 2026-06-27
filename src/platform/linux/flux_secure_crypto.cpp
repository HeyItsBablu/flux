// flux_secure_crypto.cpp
// AES-256-GCM + PBKDF2-SHA256 helpers using OpenSSL (EVP API).
// Used by the Linux fallback backend when libsecret is unavailable.

#if defined(__linux__) && !defined(__ANDROID__)

#include "flux/flux_secure_crypto.hpp"

#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <openssl/hmac.h>

#include <cstring>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <unistd.h>

namespace flux::crypto {

// ── PBKDF2-SHA256 key derivation ──────────────────────────────────────────────

std::vector<uint8_t> deriveKey(const std::string& machineSecret,
                               const std::string& salt,
                               int iterations) {
    std::vector<uint8_t> key(kKeyLen);
    int ok = PKCS5_PBKDF2_HMAC(
        machineSecret.c_str(), (int)machineSecret.size(),
        reinterpret_cast<const unsigned char*>(salt.c_str()), (int)salt.size(),
        iterations, EVP_sha256(),
        kKeyLen, key.data());
    if (!ok) key.clear();
    return key;
}

// ── AES-256-GCM encrypt ───────────────────────────────────────────────────────
// Output layout: [IV(12)] [ciphertext(N)] [tag(16)]

std::vector<uint8_t> aesGcmEncrypt(const std::vector<uint8_t>& key,
                                   const std::string&           plaintext) {
    if (key.size() < kKeyLen) return {};

    // Random IV
    std::vector<uint8_t> iv(kIVLen);
    if (RAND_bytes(iv.data(), kIVLen) != 1) return {};

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return {};

    std::vector<uint8_t> out;
    out.reserve(kIVLen + plaintext.size() + kTagLen);

    auto fail = [&]() -> std::vector<uint8_t> {
        EVP_CIPHER_CTX_free(ctx);
        return {};
    };

    if (EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr,
                           key.data(), iv.data()) != 1) return fail();

    // Prepend IV
    out.insert(out.end(), iv.begin(), iv.end());

    // Encrypt
    std::vector<uint8_t> cipherBuf(plaintext.size() + 16);
    int outLen = 0;
    if (EVP_EncryptUpdate(ctx, cipherBuf.data(), &outLen,
                          reinterpret_cast<const uint8_t*>(plaintext.data()),
                          (int)plaintext.size()) != 1) return fail();
    out.insert(out.end(), cipherBuf.begin(), cipherBuf.begin() + outLen);

    int finalLen = 0;
    if (EVP_EncryptFinal_ex(ctx, cipherBuf.data(), &finalLen) != 1) return fail();
    out.insert(out.end(), cipherBuf.begin(), cipherBuf.begin() + finalLen);

    // Append GCM tag
    std::vector<uint8_t> tag(kTagLen);
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, kTagLen, tag.data()) != 1)
        return fail();
    out.insert(out.end(), tag.begin(), tag.end());

    EVP_CIPHER_CTX_free(ctx);
    return out;
}

// ── AES-256-GCM decrypt ───────────────────────────────────────────────────────
// Input: [IV(12)] [ciphertext(N)] [tag(16)]

std::string aesGcmDecrypt(const std::vector<uint8_t>& key,
                          const std::vector<uint8_t>& blob) {
    if (key.size() < kKeyLen) return {};
    if (blob.size() < (size_t)(kIVLen + kTagLen)) return {};

    const uint8_t* iv     = blob.data();
    const uint8_t* cipher = blob.data() + kIVLen;
    size_t cipherLen      = blob.size() - kIVLen - kTagLen;
    const uint8_t* tag    = blob.data() + kIVLen + cipherLen;

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return {};

    auto fail = [&]() -> std::string {
        EVP_CIPHER_CTX_free(ctx);
        return {};
    };

    if (EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr,
                           key.data(), iv) != 1) return fail();

    // Set expected tag (must be set BEFORE final)
    std::vector<uint8_t> tagCopy(tag, tag + kTagLen);
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, kTagLen, tagCopy.data()) != 1)
        return fail();

    std::vector<uint8_t> plain(cipherLen + 16);
    int outLen = 0;
    if (EVP_DecryptUpdate(ctx, plain.data(), &outLen, cipher, (int)cipherLen) != 1)
        return fail();

    int finalLen = 0;
    int tagOk = EVP_DecryptFinal_ex(ctx, plain.data() + outLen, &finalLen);
    EVP_CIPHER_CTX_free(ctx);

    if (tagOk <= 0) return {}; // authentication failed — tag mismatch

    return std::string(reinterpret_cast<char*>(plain.data()), outLen + finalLen);
}

// ── Machine secret ────────────────────────────────────────────────────────────

std::string getMachineSecret() {
    // Try /etc/machine-id first (systemd), then dbus fallback
    for (const char* path : {"/etc/machine-id", "/var/lib/dbus/machine-id"}) {
        std::ifstream f(path);
        if (f.is_open()) {
            std::string id;
            std::getline(f, id);
            // Trim whitespace
            while (!id.empty() && (id.back() == '\n' || id.back() == '\r'
                                   || id.back() == ' '))
                id.pop_back();
            if (!id.empty()) return id;
        }
    }
    // Last resort: hostname
    char host[256] = {};
    if (gethostname(host, sizeof(host)) == 0)
        return std::string(host) + "_flux_fallback";
    return "flux_static_fallback_secret_do_not_use";
}

// ── Base64 ────────────────────────────────────────────────────────────────────

static const char kB64Chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64Encode(const uint8_t* data, size_t len) {
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    for (size_t i = 0; i < len; i += 3) {
        uint32_t val = (uint32_t)data[i] << 16;
        if (i + 1 < len) val |= (uint32_t)data[i+1] << 8;
        if (i + 2 < len) val |= (uint32_t)data[i+2];
        out += kB64Chars[(val >> 18) & 63];
        out += kB64Chars[(val >> 12) & 63];
        out += (i + 1 < len) ? kB64Chars[(val >> 6) & 63] : '=';
        out += (i + 2 < len) ? kB64Chars[val & 63]        : '=';
    }
    return out;
}

std::vector<uint8_t> base64Decode(const std::string& encoded) {
    auto lookup = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    };
    std::vector<uint8_t> out;
    out.reserve(encoded.size() * 3 / 4);
    for (size_t i = 0; i + 3 < encoded.size(); i += 4) {
        int v0 = lookup(encoded[i]);
        int v1 = lookup(encoded[i+1]);
        int v2 = lookup(encoded[i+2]);
        int v3 = lookup(encoded[i+3]);
        if (v0 < 0 || v1 < 0) break;
        out.push_back((uint8_t)((v0 << 2) | (v1 >> 4)));
        if (v2 >= 0) out.push_back((uint8_t)((v1 << 4) | (v2 >> 2)));
        if (v3 >= 0) out.push_back((uint8_t)((v2 << 6) | v3));
    }
    return out;
}

} // namespace flux::crypto

#endif // linux desktop