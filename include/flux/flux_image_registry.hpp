// include/flux/flux_image_registry.hpp
#pragma once

// ============================================================================
// SSR-only content-addressed image byte registry.
//
// NOT thread_local, unlike the per-request DOM node cache / hydration
// counters — this is process-lifetime, shared across every request and
// every thread deliberately. Two different pages (or two different
// requests for the same page) that reference the same logo asset should
// register it ONCE and share the same /img/<hash> URL, so the browser's
// HTTP cache actually pays off across navigations instead of re-fetching
// on every page the way the pre-Phase-N font handling used to.
//
// Stores the ORIGINAL COMPRESSED bytes verbatim — never decodes/
// re-encodes. This mirrors tryServeStaticFont's "read once, serve as-is"
// approach in ssr/main.cpp; the browser is a perfectly good PNG/JPEG
// decoder and re-implementing that server-side just to re-encode
// identical pixels would be pure waste.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include <mutex>
#include <string>
#include <unordered_map>

namespace flux_image_registry_detail
{
    struct Entry
    {
        std::string bytes;
        std::string contentType;
    };

    inline std::mutex g_mutex;
    inline std::unordered_map<std::string, Entry> g_entries; // key = hash

    // FNV-1a — fast, dependency-free, plenty for content-addressing;
    // collision resistance requirements here are "don't collide two
    // different real images in one running process," not cryptographic.
    inline std::string hashBytes(const uint8_t *data, size_t len)
    {
        uint64_t h = 1469598103934665603ull;
        for (size_t i = 0; i < len; ++i)
        {
            h ^= data[i];
            h *= 1099511628211ull;
        }
        char buf[17];
        snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)h);
        return std::string(buf);
    }

    // Magic-byte sniffing — same four formats flux_image.hpp's stb_image
    // build already restricts itself to (STBI_ONLY_PNG/JPEG/BMP/GIF/TGA).
    // TGA has no reliable magic number, so it isn't detected here; a TGA
    // asset falls back to contentType "application/octet-stream", which
    // browsers won't render inline — a known gap, flagged rather than
    // silently mis-served as something else.
    inline std::string sniffContentType(const std::string &bytes)
    {
        if (bytes.size() >= 8 &&
            (unsigned char)bytes[0] == 0x89 && bytes[1] == 'P' && bytes[2] == 'N' && bytes[3] == 'G')
            return "image/png";
        if (bytes.size() >= 3 &&
            (unsigned char)bytes[0] == 0xFF && (unsigned char)bytes[1] == 0xD8)
            return "image/jpeg";
        if (bytes.size() >= 6 && bytes.compare(0, 3, "GIF") == 0)
            return "image/gif";
        if (bytes.size() >= 2 && bytes[0] == 'B' && bytes[1] == 'M')
            return "image/bmp";
        return "application/octet-stream";
    }
}

// Registers `data`/`len` if not already present (dedup by content hash),
// and returns the URL path (e.g. "/img/1a2b3c4d5e6f7890") the SSR host
// should serve it under. Empty return means len==0 — caller should treat
// as a decode failure, same as any other "no image" case.
inline std::string fluxImageRegistryRegister(const uint8_t *data, size_t len)
{
    if (len == 0)
        return {};

    std::string key = flux_image_registry_detail::hashBytes(data, len);

    std::lock_guard<std::mutex> lock(flux_image_registry_detail::g_mutex);
    auto it = flux_image_registry_detail::g_entries.find(key);
    if (it == flux_image_registry_detail::g_entries.end())
    {
        std::string bytes(reinterpret_cast<const char *>(data), len);
        std::string contentType = flux_image_registry_detail::sniffContentType(bytes);
        flux_image_registry_detail::g_entries.emplace(
            key, flux_image_registry_detail::Entry{std::move(bytes), std::move(contentType)});
    }
    return "/img/" + key;
}

// Looked up by ssr/main.cpp's request handler. Returns false if `path`
// isn't a registered image (falls through to the 404/rendered-page path).
inline bool fluxImageRegistryServe(const std::string &path, std::string &outBody,
                                   std::string &outContentType)
{
    if (path.rfind("/img/", 0) != 0)
        return false;
    std::string key = path.substr(5);

    std::lock_guard<std::mutex> lock(flux_image_registry_detail::g_mutex);
    auto it = flux_image_registry_detail::g_entries.find(key);
    if (it == flux_image_registry_detail::g_entries.end())
        return false;
    outBody = it->second.bytes;
    outContentType = it->second.contentType;
    return true;
}