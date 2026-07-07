// src/flux_font_ssr.cpp
//
// SSR font backend + measurement, built on stb_truetype instead of Pango/
// Cairo. This was the ONLY thing coupling the SSR build to Linux — Pango's
// fontconfig integration is what made ssr/CMakeLists.txt need pkg-config/
// vcpkg discovery in the first place. stb_truetype needs no system
// library and no per-platform discovery logic at all: it's vendored
// header-only C, already linked into this project as the `stb` target.
//
// IMPORTANT divergence from the Pango version this replaces:
// stb_truetype does NOT do fontconfig-style "resolve this family name to
// an installed font file" lookup — it only parses raw bytes you hand it.
// So instead of resolving FontKey::family against the host's installed
// fonts, every request here is mapped onto one of a small, FIXED set of
// BUNDLED .ttf files, chosen by weight only (regular vs bold). `family`
// is accepted so call sites compile unchanged, but is otherwise ignored
// on this backend. This is a real behavior change, not just a library
// swap — SSR-rendered text metrics now come from whichever files are
// bundled below, not from whatever family the TextStyle actually
// requested. That's still within the kind of divergence Phase 3's
// hydration mismatch detector is meant to catch (see the comment this
// file's predecessor had about font-rendering divergence being expected).
//
// Italic is NOT introduced here: flux_font_linux.cpp's Pango backend
// hard-coded PANGO_STYLE_NORMAL regardless of request, and FontKey
// (non-Win32) has no italic field at all — getFontItalic below preserves
// that exact pre-existing limitation rather than adding new capability.

#ifdef FLUX_SSR

#include "flux/flux_text_style.hpp"
#include "flux/flux_font.hpp"

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#include <algorithm>
#include <cstdio>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    // ── Bundled font files ──────────────────────────────────────────────
    // Override at configure time with -DFLUX_SSR_FONT_DIR=... (see
    // ssr/CMakeLists.txt). Defaults to "fonts" relative to the process's
    // working directory. You must place at minimum Regular.ttf and
    // Regular-Bold.ttf here — there is no fallback search, an SSR host
    // with no bundled fonts will throw at first getFont() call.
#ifndef FLUX_SSR_FONT_DIR
#define FLUX_SSR_FONT_DIR "fonts"
#endif

    const char *fontFileFor(FontWeight weight)
    {
        bool bold = static_cast<int>(weight) >= static_cast<int>(FontWeight::SemiBold);
        return bold ? FLUX_SSR_FONT_DIR "/Regular-Bold.ttf"
                    : FLUX_SSR_FONT_DIR "/Regular.ttf";
    }

    // Raw file bytes must outlive every stbtt_fontinfo built from them —
    // stb_truetype stores pointers INTO this buffer, it does not copy it.
    // Kept alive for the process's whole lifetime; at most two files
    // (regular/bold), a few hundred KB — not worth reference-counting.
    std::vector<unsigned char> &loadFileBytes(const std::string &path)
    {
        static std::mutex mu;
        static std::map<std::string, std::vector<unsigned char>> loaded;

        std::lock_guard<std::mutex> lock(mu);
        auto it = loaded.find(path);
        if (it != loaded.end())
            return it->second;

        FILE *f = std::fopen(path.c_str(), "rb");
        if (!f)
            throw std::runtime_error("flux_ssr: could not open bundled font file: " + path);

        std::fseek(f, 0, SEEK_END);
        long size = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);

        std::vector<unsigned char> bytes(static_cast<size_t>(size));
        if (size > 0 && std::fread(bytes.data(), 1, static_cast<size_t>(size), f) != static_cast<size_t>(size))
        {
            std::fclose(f);
            throw std::runtime_error("flux_ssr: short read on font file: " + path);
        }
        std::fclose(f);

        auto &slot = loaded[path];
        slot = std::move(bytes);
        return slot;
    }

    struct SsrNativeFont
    {
        stbtt_fontinfo info{};
        int pixelSize = 14;
        bool underline = false;
        bool strikeOut = false;
    };

    void measureWithFont(SsrNativeFont *font, const std::wstring &wtext,
                         int maxWidth, bool softWrap, int maxLines,
                         int &outWidth, int &outHeight)
    {
        outWidth = outHeight = 0;
        if (wtext.empty() || !font)
            return;

        float scale = stbtt_ScaleForPixelHeight(&font->info,
                                                static_cast<float>(font->pixelSize));

        int ascent = 0, descent = 0, lineGap = 0;
        stbtt_GetFontVMetrics(&font->info, &ascent, &descent, &lineGap);
        int lineHeight = static_cast<int>((ascent - descent + lineGap) * scale + 0.5f);

        int lineWidth = 0, maxLineWidth = 0, lines = 1;

        // NOTE: each wchar_t is treated as one Unicode codepoint directly
        // (no surrogate-pair handling for astral characters) — this
        // mirrors the predecessor's wToUtf8() helper, which made the same
        // BMP-only simplification. Pre-existing limitation, not
        // introduced here.
        for (size_t i = 0; i < wtext.size(); ++i)
        {
            int cp = static_cast<int>(wtext[i]);

            if (cp == '\n')
            {
                maxLineWidth = std::max(maxLineWidth, lineWidth);
                lineWidth = 0;
                ++lines;
                continue;
            }

            int advance = 0, lsb = 0;
            stbtt_GetCodepointHMetrics(&font->info, cp, &advance, &lsb);
            int advPx = static_cast<int>(advance * scale + 0.5f);

            if (i + 1 < wtext.size())
            {
                int kern = stbtt_GetCodepointKernAdvance(
                    &font->info, cp, static_cast<int>(wtext[i + 1]));
                advPx += static_cast<int>(kern * scale + 0.5f);
            }

            // Greedy character-wrap — cruder than Pango's PANGO_WRAP_WORD_CHAR
            // (which breaks at word boundaries). Good enough for width/
            // height measurement; will wrap mid-word where Pango wouldn't.
            if (softWrap && maxWidth > 0 && lineWidth + advPx > maxWidth && lineWidth > 0)
            {
                maxLineWidth = std::max(maxLineWidth, lineWidth);
                lineWidth = 0;
                ++lines;
            }

            lineWidth += advPx;

            if (maxLines > 0 && lines > maxLines)
                break;
        }

        maxLineWidth = std::max(maxLineWidth, lineWidth);
        if (maxLines > 0)
            lines = std::min(lines, maxLines);

        // Safety margin: stb_truetype's raw advance-sum is a slightly different
        // (and typically slightly narrower) measurement than what the browser's
        // own rasterizer produces for the same string/font, even with the exact
        // same bytes loaded via @font-face. Without slack here, a text box sized
        // to the SSR-measured width can force an unwanted wrap the instant the
        // browser measures it a pixel or two wider — which then also throws off
        // every sibling positioned below it (the whole tree assumed the
        // unwrapped, shorter height). A few px is invisible in normal layout but
        // eliminates this class of divergence.
        constexpr int kWidthSafetyMarginPx = 4;
        outWidth = maxLineWidth + kWidthSafetyMarginPx;

        // Same story, vertically: pinning CSS line-height (see
        // fluxDomLineHeightPx / flux_painter_dom.cpp) keeps the TEXT LAYOUT
        // inside the box consistent, but does nothing to guarantee the
        // browser's actual glyph rendering — hinting, antialiasing, the
        // exact ascent/descent the rasterizer decides to use — fits inside
        // a box sized to stb_truetype's number. applyRect() writes this
        // outHeight as the DOM node's EXPLICIT, FIXED CSS height, and
        // that same number is what the layout engine uses to position the
        // NEXT sibling — so any shortfall doesn't get clipped (there's no
        // overflow:hidden on text nodes, deliberately, so real text is
        // never cut off) — it just visually bleeds downward into whatever
        // comes after. A small per-line margin makes that structurally
        // impossible rather than chasing exact cross-engine agreement,
        // which is unwinnable in general.
        constexpr int kHeightSafetyMarginPxPerLine = 3;
        outHeight = lines * (lineHeight + kHeightSafetyMarginPxPerLine);
    }
}

// ── FontCache backend (takes over flux_font_linux.cpp's role for SSR) ──────

NativeFont FontCache::createFont(const FontKey &key)
{
    const char *path = fontFileFor(key.weight);
    auto &bytes = loadFileBytes(path);

    auto *font = new SsrNativeFont();
    int offset = stbtt_GetFontOffsetForIndex(bytes.data(), 0);
    if (offset < 0 || !stbtt_InitFont(&font->info, bytes.data(), offset))
    {
        delete font;
        throw std::runtime_error(std::string("flux_ssr: stbtt_InitFont failed for ") + path);
    }
    font->pixelSize = key.size;
    font->underline = key.underline;
    font->strikeOut = key.strikeOut;
    return reinterpret_cast<NativeFont>(font);
}

NativeFont FontCache::getFont(const std::string &family, int size, FontWeight weight)
{
    return getFont(family, size, weight, false, false);
}

NativeFont FontCache::getFont(int size, FontWeight weight)
{
    return getFont("Sans", size, weight, false, false);
}

NativeFont FontCache::getFont(const std::string &family, int size, FontWeight weight,
                              bool underline, bool strikeOut)
{
    FontKey key{family, size, weight, underline, strikeOut};
    auto it = cache.find(key);
    if (it != cache.end())
        return it->second;
    NativeFont font = createFont(key);
    cache[key] = font;
    return font;
}

NativeFont FontCache::getFontItalic(const std::string &family, int size, FontWeight weight)
{
    // Non-Win32 behavior, preserved: flux_font_linux.cpp never actually
    // distinguished italic (PangoFontDescription style was hard-coded to
    // PANGO_STYLE_NORMAL), and FontKey has no italic field on this
    // branch. This delegates straight to the regular getFont(), matching
    // that existing limitation rather than introducing new behavior.
    return getFont(family, size, weight);
}

void FontCache::clear()
{
    for (auto &pair : cache)
        delete reinterpret_cast<SsrNativeFont *>(pair.second);
    cache.clear();
}

// ── Measurement (what flux_font_ssr.cpp originally added on top) ───────────

void measureDomText(const char * /*cssFont*/, const std::wstring &wtext,
                    int &outWidth, int &outHeight)
{
    // cssFont ignored, as in the Pango version — kept only so the symbol
    // exists for linking. Real call sites should use measureDomRichText.
    static FontCache defaultCache;
    NativeFont fnt = defaultCache.getFont("Sans", 14, FontWeight::Normal);
    measureWithFont(reinterpret_cast<SsrNativeFont *>(fnt), wtext,
                    0, false, 0, outWidth, outHeight);
}

void measureDomRichText(const std::wstring &wtext, const TextStyle &style,
                        FontCache &fontCache, int maxWidth, bool softWrap,
                        int maxLines, int &outWidth, int &outHeight)
{
    NativeFont fnt = fontCache.getFont(style.fontFamily, style.scaledFontSize(),
                                       style.fontWeight);
    measureWithFont(reinterpret_cast<SsrNativeFont *>(fnt), wtext,
                    maxWidth, softWrap, maxLines, outWidth, outHeight);
}

// ── Exposes the same line-height formula measureWithFont() uses internally,
// so flux_painter_dom.cpp can pin the browser's CSS line-height to the
// EXACT number layout already assumed — instead of letting the browser
// fall back to its own default leading, which uses a different formula
// than stb_truetype's ascent/descent/lineGap and is never guaranteed to
// match. A few px of drift here is invisible on its own but throws off
// every sibling positioned below a multi-line-height-sensitive box.
int fluxDomLineHeightPx(const std::string &fontFamily, int fontSize, FontWeight weight)
{
    static FontCache cache;
    NativeFont fnt = cache.getFont(fontFamily, fontSize, weight);
    auto *font = reinterpret_cast<SsrNativeFont *>(fnt);
    if (!font) return static_cast<int>(fontSize * 1.2f);

    float scale = stbtt_ScaleForPixelHeight(&font->info, static_cast<float>(font->pixelSize));
    int ascent = 0, descent = 0, lineGap = 0;
    stbtt_GetFontVMetrics(&font->info, &ascent, &descent, &lineGap);
    return static_cast<int>((ascent - descent + lineGap) * scale + 0.5f);
}

#endif // FLUX_SSR