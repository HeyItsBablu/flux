#pragma once

// ============================================================================
// flux_secure_crypto.hpp — minimal AES-256-GCM helpers (OpenSSL / mbedTLS)
//
// Used by the Linux fallback backend when libsecret is unavailable.
// On Windows this is unused (DPAPI handles everything).
// On Android this is unused (Android Keystore handles everything).
// ============================================================================

#include <cstdint>
#include <string>
#include <vector>

namespace flux::crypto {

// 256-bit key, 96-bit IV, 128-bit tag
static constexpr int kKeyLen = 32;
static constexpr int kIVLen  = 12;
static constexpr int kTagLen = 16;

// ── Key derivation ────────────────────────────────────────────────────────────
// Derives a 256-bit key from a machine-unique secret + salt using PBKDF2-SHA256.
// `machineSecret` should be 32+ bytes of entropy (e.g. from /etc/machine-id).
std::vector<uint8_t> deriveKey(const std::string& machineSecret,
                               const std::string& salt,
                               int iterations = 100'000);

// ── Encrypt ───────────────────────────────────────────────────────────────────
// Returns: [IV(12)] [ciphertext] [tag(16)]
// Returns empty vector on failure.
std::vector<uint8_t> aesGcmEncrypt(const std::vector<uint8_t>& key,
                                   const std::string&           plaintext);

// ── Decrypt ───────────────────────────────────────────────────────────────────
// Input must be at least kIVLen + kTagLen bytes.
// Returns empty string on failure (authentication error, bad key, etc.).
std::string aesGcmDecrypt(const std::vector<uint8_t>& key,
                          const std::vector<uint8_t>& cipherBlob);

// ── Machine secret ────────────────────────────────────────────────────────────
// Returns a stable per-machine secret derived from OS identifiers.
// Linux: /etc/machine-id  (or /var/lib/dbus/machine-id)
std::string getMachineSecret();

// ── Base64 helpers (no dependency on OpenSSL EVP_Encode) ─────────────────────
std::string base64Encode(const uint8_t* data, size_t len);
std::vector<uint8_t> base64Decode(const std::string& encoded);

} // namespace flux::crypto