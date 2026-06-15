// src/flux_font_web.cpp
//
// FontCache implementation for Emscripten / WebAssembly.
//
// NativeFont on web
// ─────────────────
// NativeFont is void* (defined in flux_platform.hpp for all non-Win32 platforms).
// On web each cache entry is a heap-allocated const char* holding a CSS font
// string compatible with CanvasRenderingContext2D.font, e.g.:
//
//   "300 14px Inter"     — Light
//   "400 14px Inter"     — Normal
//   "700 14px Inter"     — Bold
//   "700 14px Segoe UI"  — Bold, fallback family
//
// The string format is: "<weight> <size>px <family>"
// This is the CSS font shorthand without style (always "normal") or line-height.
//
// Underline / strikethrough
// ─────────────────────────
// These are NOT part of the CSS font shorthand — Canvas 2D applies them via
// ctx.textDecoration (non-standard, limited support) or by drawing lines
// manually after fillText().  The FontKey still includes underline/strikeOut so
// two otherwise identical fonts with different decoration are separate cache
// entries, preserving the same caching contract as Win32.  The actual CSS
// string stored for both will be identical; the painter reads the FontKey's
// decoration flags separately to decide whether to draw the lines.
//
// Text measurement
// ────────────────
// Painter::measureText() calls measureWebText() below, which runs
// ctx.measureText() in JS and returns the advance width + an estimated height
// (Canvas 2D does not expose a reliable ascent+descent sum until
// TextMetrics.fontBoundingBoxAscent/Descent, which is now broadly supported).
//
// Font loading
// ────────────
// Web fonts must be loaded before measureText / fillText produce correct
// results.  shell.html should include @font-face rules (or Google Fonts links)
// for every family the app uses.  The default fallback chain below covers the
// common case where no custom font is loaded.

#ifdef __EMSCRIPTEN__

#include "flux/flux_font.hpp"
#include "flux/flux_platform.hpp"

#include <emscripten.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>

// ============================================================================
// Internal helpers
// ============================================================================

namespace
{

    // ── CSS font string builder ───────────────────────────────────────────────────
    //
    // Format: "<weight> <size>px <family-with-fallbacks>"
    //
    // Family fallback chain appended automatically:
    //   • System UI fonts first so the app looks native on each OS.
    //   • Generic sans-serif last as the final safety net.
    //
    // The caller owns the returned char* and must free() it.

    char *buildCSSFont(const std::string &family, int size, FontWeight weight)
    {
        int w = static_cast<int>(weight); // 300 / 400 / 700

        // Fallback chain: requested family → system UI → generic sans-serif.
        // If the requested family IS a system UI font or generic, it will just
        // appear twice in the list — browsers deduplicate that silently.
        char buf[512];
        snprintf(buf, sizeof(buf),
                 "%d %dpx %s, -apple-system, BlinkMacSystemFont, "
                 "'Segoe UI', Roboto, sans-serif",
                 w, size, family.c_str());

        // Heap-allocate so the pointer stays valid for the FontCache lifetime.
        char *result = static_cast<char *>(malloc(strlen(buf) + 1));
        if (result)
            strcpy(result, buf);
        return result;
    }

} // namespace

// ============================================================================
// FontCache::createFont
// ============================================================================

NativeFont FontCache::createFont(const FontKey &key)
{
    // Decoration flags (underline, strikeOut) are NOT encoded in the CSS font
    // string — they are applied at draw time by the painter.  Two FontKeys that
    // differ only in decoration will produce the same CSS string but remain
    // separate cache entries so the painter can distinguish them.
    char *cssFont = buildCSSFont(key.family, key.size, key.weight);
    return static_cast<NativeFont>(cssFont);
}

// ============================================================================
// FontCache public methods
// ============================================================================

NativeFont FontCache::getFont(const std::string &family, int size,
                              FontWeight weight)
{
    return getFont(family, size, weight, false, false);
}

NativeFont FontCache::getFont(int size, FontWeight weight)
{
    return getFont("Segoe UI", size, weight, false, false);
}

NativeFont FontCache::getFont(const std::string &family, int size,
                              FontWeight weight, bool underline, bool strikeOut)
{
    FontKey key{family, size, weight, underline, strikeOut};

    auto it = cache.find(key);
    if (it != cache.end())
        return it->second;

    NativeFont font = createFont(key);
    cache[key] = font;
    return font;
}

void FontCache::clear()
{
    for (auto &pair : cache)
        free(pair.second); // matches malloc() in buildCSSFont
    cache.clear();
}

// ============================================================================
// Web text measurement
//
// Called by Painter::measureText() in flux_painter_web.cpp.
// Sets the font on the JS Canvas 2D context, calls measureText(), and returns
// width + height back to C++.
//
// Height strategy:
//   TextMetrics.fontBoundingBoxAscent + fontBoundingBoxDescent is the
//   most accurate available measure and is now supported in Chrome 87+,
//   Firefox 116+, Safari 16.4+.  We fall back to `size` (the point size
//   passed in) if the properties are missing (older browsers / jsdom).
//
// This is a free function rather than a method because the painter needs to
// call it directly with a font pointer + known size, without going through
// FontCache again.
// ============================================================================

void measureWebText(const char *cssFont, const char *utf8Text,
                    int /*fontSize*/, int &outWidth, int &outHeight)
{
    if (!cssFont || !utf8Text || utf8Text[0] == '\0')
    {
        outWidth = 0;
        outHeight = 0;
        return;
    }

    // Pass both strings to JS and get back {width, height} as a packed int64:
    //   high 32 bits = ceil(width)
    //   low  32 bits = ceil(height)
    //
    // Using a packed int64 avoids two round-trips and keeps the EM_ASM clean.
    double packed = EM_ASM_DOUBLE({
        var ctx    = Module._fluxCtx2D;
        if (!ctx) return 0.0;

        var font   = UTF8ToString($0);
        var text   = UTF8ToString($1);

        ctx.save();
        ctx.font = font;

        var m    = ctx.measureText(text);
        var w    = Math.ceil(m.width);

        // Prefer bounding-box metrics; fall back to actualBoundingBox sum,
        // then to a rough estimate based on the font size extracted from the
        // font string (e.g. "700 14px Inter" → 14).
        var h;
        if (m.fontBoundingBoxAscent !== undefined &&
            m.fontBoundingBoxDescent !== undefined) {
            h = Math.ceil(m.fontBoundingBoxAscent + m.fontBoundingBoxDescent);
        } else if (m.actualBoundingBoxAscent !== undefined &&
                   m.actualBoundingBoxDescent !== undefined) {
            h = Math.ceil(m.actualBoundingBoxAscent + m.actualBoundingBoxDescent);
        } else {
            // Last resort: parse "NNN px" out of the font string.
            var match = font.match(/(\d+)px/);
            h = match ? Math.ceil(parseInt(match[1]) * 1.3) : 16;
        }

        ctx.restore();

        // Pack: width in upper 20 bits, height in lower 20 bits.
        // Both are clamped to 0xFFFFF (≈1 million px) which is more than enough.
        return (w & 0xFFFFF) * 1048576.0 + (h & 0xFFFFF); }, cssFont, utf8Text);

    // Unpack.  Use double arithmetic to avoid sign-extension issues.
    int w = (int)(packed / 1048576.0);
    int h = (int)(packed - w * 1048576.0);

    outWidth = (w > 0) ? w : 0;
    outHeight = (h > 0) ? h : 0;
}

// ============================================================================
// measureWebTextW — wide-string overload
//
// Widgets call Painter::measureText(std::wstring, ...) so we need a path that
// converts the wstring to UTF-8 before calling into JS.
// Handles the BMP-only wstrings produced by toWideString() on web.
// ============================================================================

void measureWebTextW(const char *cssFont, const std::wstring &wtext,
                     int fontSize, int &outWidth, int &outHeight)
{
    if (wtext.empty())
    {
        outWidth = outHeight = 0;
        return;
    }

    // Convert wstring → UTF-8.  BMP only (all chars ≤ U+FFFF).
    std::string utf8;
    utf8.reserve(wtext.size() * 3); // worst case 3 bytes per BMP char
    for (wchar_t wc : wtext)
    {
        uint32_t cp = static_cast<uint32_t>(wc);
        if (cp < 0x80)
        {
            utf8 += static_cast<char>(cp);
        }
        else if (cp < 0x800)
        {
            utf8 += static_cast<char>(0xC0 | (cp >> 6));
            utf8 += static_cast<char>(0x80 | (cp & 0x3F));
        }
        else
        {
            utf8 += static_cast<char>(0xE0 | (cp >> 12));
            utf8 += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            utf8 += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }

    measureWebText(cssFont, utf8.c_str(), fontSize, outWidth, outHeight);
}

#endif // __EMSCRIPTEN__