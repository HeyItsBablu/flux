#pragma once
#include <cstdint>
#include <cstdio>
#include <string>

// Deterministic "GUID" derived from a name (here, bundleId). Not a
// cryptographic hash — Inno only needs AppId to be a syntactically valid,
// STABLE identifier across releases so Setup recognizes "same app, do an
// upgrade" instead of a side-by-side install. As long as bundleId doesn't
// change, this doesn't either, with nothing to persist or migrate.
inline std::string deterministic_guid(const std::string &seed) {
  auto fnv1a = [&](uint64_t offset) -> uint64_t {
    uint64_t hash = offset;
    for (unsigned char c : seed) {
      hash ^= c;
      hash *= 0x100000001B3ULL;
    }
    return hash;
  };

  uint64_t hi = fnv1a(0xCBF29CE484222325ULL);
  uint64_t lo = fnv1a(0x84222325CBF29CE4ULL); // different offset basis

  // Force UUID v4 version/variant bits so it's syntactically valid.
  hi = (hi & 0xFFFFFFFFFFFF0FFFULL) | 0x0000000000004000ULL;
  lo = (lo & 0x3FFFFFFFFFFFFFFFULL) | 0x8000000000000000ULL;

  char buf[40];
  std::snprintf(buf, sizeof(buf), "%08X-%04X-%04X-%04X-%012llX",
               static_cast<unsigned>(hi >> 32),
               static_cast<unsigned>((hi >> 16) & 0xFFFF),
               static_cast<unsigned>(hi & 0xFFFF),
               static_cast<unsigned>(lo >> 48),
               static_cast<unsigned long long>(lo & 0xFFFFFFFFFFFFULL));
  return buf;
}