// src/flux_font_dom.cpp
//
// Text measurement for the DOM Painter backend (flux_painter_dom.cpp).
//
// FontCache itself (flux_font.hpp / flux_font_web.cpp) is REUSED UNCHANGED —
// NativeFont is already a CSS font string on web, usable identically by a
// DOM element's style.font as by Canvas2D's ctx.font. This file only
// supplies the two forward-declared measurement functions
// flux_painter_dom.cpp calls: measureDomText / measureDomRichText.
//
// Measurement strategy
// ─────────────────────
// A single persistent, hidden, off-screen <div> ("the sandbox") is created
// once and reused for every measurement call — never one node per call,
// which would be exactly the layout-thrashing problem this whole design
// is meant to avoid, just moved from painting to measuring.
//
// Unlike the canvas backend (which has to hand-roll line-wrapping via
// binary search over ctx.measureText(), see flux_painter_web.cpp's
// wrapTextWeb()), this file sets real CSS wrap properties on the sandbox
// and lets the browser do the wrapping natively, then reads the resulting
// box size back via getBoundingClientRect(). Less code, and correct by
// construction for RTL/justify/inter-glyph spacing in a way the manual
// approach wasn't.
//
// This file is NOT reused by the SSR string-builder adapter (Phase 4) —
// unlike flux_painter_dom.cpp, which is shared verbatim between the live
// browser and the server. The SSR host is a native, non-Emscripten build
// with no live DOM to measure against at all; it measures text via its
// own native means (the same category of concern FontCache already
// handles per-platform via DWrite/Pango/CoreText). Some divergence
// between server-measured and client-measured text metrics is expected
// and is exactly what Phase 3's hydration-mismatch detector exists to
// surface — not something this file can or should try to eliminate.

#ifdef __EMSCRIPTEN__

#include "flux/flux_text_style.hpp"
#include "flux/flux_font.hpp"

#include <emscripten.h>
#include <string>
#include <cstdio>

// ============================================================================
// One-time sandbox setup
//
// Call once at startup, alongside fluxDomAdapterLiveInit() / fluxDomAdapterLiveActivate()
// in main.cpp. Creates the hidden measurement element directly (not through
// IDomAdapter — this element is a measurement scratch space, not part of
// the rendered widget tree, so it doesn't need node-cache/ownership
// semantics at all).
// ============================================================================

extern "C" void fluxFontDomInit()
{
    EM_ASM({
        var el = document.createElement('div');
        el.style.position = 'absolute';
        el.style.visibility = 'hidden';
        el.style.left = '-99999px';
        el.style.top = '0';
        el.style.margin = '0';
        el.style.padding = '0';
        el.style.border = 'none';
        document.body.appendChild(el);
        Module._fluxFontSandbox = el;
    });
}

// ============================================================================
// wstring -> UTF-8 (matches the same BMP-only scheme used throughout the
// web painter/font files)
// ============================================================================

namespace
{
    std::string wToUtf8(const std::wstring &ws)
    {
        std::string out;
        out.reserve(ws.size() * 4);
        for (wchar_t wc : ws)
        {
            uint32_t cp = static_cast<uint32_t>(wc);
            if (cp < 0x80) out += static_cast<char>(cp);
            else if (cp < 0x800)
            {
                out += static_cast<char>(0xC0 | (cp >> 6));
                out += static_cast<char>(0x80 | (cp & 0x3F));
            }
            else if (cp < 0x10000)
            {
                out += static_cast<char>(0xE0 | (cp >> 12));
                out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                out += static_cast<char>(0x80 | (cp & 0x3F));
            }
            else
            {
                out += static_cast<char>(0xF0 | (cp >> 18));
                out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
                out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                out += static_cast<char>(0x80 | (cp & 0x3F));
            }
        }
        return out;
    }
}

// ============================================================================
// fluxDomLineHeightPx — CSS-pixel line-height for the given font, so
// Painter::drawRichText() can pin a DOM node's `line-height` to the exact
// value measureDomRichText() assumed during layout (see flux_painter_dom.cpp
// call site). Unlike the SSR backend (which derives this from
// stb_truetype's ascent/descent/lineGap metrics), the live browser can
// just ask the sandbox element directly — create a single line of text at
// this font and read back its rendered line box height. Simpler and, by
// construction, exactly what the browser will use when actually painting.
// ============================================================================

int fluxDomLineHeightPx(const std::string &fontFamily, int fontSize, FontWeight weight)
{
    // Build the same CSS font shorthand fluxDomCssFontString would, without
    // needing a NativeFont handle on hand at every call site.
    char fontBuf[96];
    snprintf(fontBuf, sizeof(fontBuf), "%d %dpx '%s'",
             static_cast<int>(weight), fontSize > 0 ? fontSize : 14,
             fontFamily.empty() ? "sans-serif" : fontFamily.c_str());

    double h = EM_ASM_DOUBLE({
        var el = Module._fluxFontSandbox;
        if (!el) return 0.0;

        el.style.font        = UTF8ToString($0);
        el.style.whiteSpace   = 'pre';
        el.style.width        = 'auto';
        el.style.display      = 'block';
        el.style.overflow     = 'visible';
        el.style.webkitLineClamp = '';
        el.style.webkitBoxOrient = '';
        el.textContent        = 'M'; // single reference glyph, single line

        var rect = el.getBoundingClientRect();
        return Math.ceil(rect.height);
    }, fontBuf);

    return (h > 0) ? (int)h : (int)(fontSize > 0 ? fontSize * 1.2 : 17);
}


// Must match flux_font_ssr.cpp's kWidthSafetyMarginPx /
// kHeightSafetyMarginPxPerLine exactly. The server pads every
// measurement by these amounts before layout ever sees it (to protect
// against stb_truetype/browser metric drift — see that file's comment).
// If the client's own measurement doesn't apply the SAME padding,
// hydration always re-lays-out slightly smaller than what SSR shipped,
// which is exactly the "settles into a smaller size" shift after boot.
// TODO: move these two constants into a shared header (e.g.
// flux_text_style.hpp) so they can't drift apart again.
// Zero: the client's own sandbox measurement already IS what the
// browser will paint (same DOM/CSS/font, same code path drawRichText
// uses) — it needs no defensive padding against itself. SSR is the side
// with a real measurement gap to close (stb_truetype's unhinted advance
// widths vs the browser's hinted ones — see the per-character margin
// comment in flux_font_ssr.cpp's measureWithFont), so that's where the
// margin belongs. Padding both sides double-counts and reintroduces
// exactly the drift this is meant to eliminate.
constexpr int kWidthSafetyMarginPx = 0;
constexpr int kHeightSafetyMarginPxPerLine = 3;


// ============================================================================
// measureDomText — natural (unwrapped) single-run width/height.
//
// Mirrors measureWebText's contract from flux_font_web.cpp: given a CSS
// font string and text, return the size the text would occupy with no
// wrap constraint at all. Used by Painter::measureText() (plain drawText
// callers — buttons, icons, TextInput's cursor math, etc).
// ============================================================================

void measureDomText(const char *cssFont, const std::wstring &wtext,
                    int &outWidth, int &outHeight)
{
    if (!cssFont || wtext.empty())
    {
        outWidth = outHeight = 0;
        return;
    }

    std::string utf8 = wToUtf8(wtext);

    double packed = EM_ASM_DOUBLE({
        var el = Module._fluxFontSandbox;
        if (!el) return 0.0;

        el.style.font       = UTF8ToString($0);
        el.style.whiteSpace  = 'pre'; // no wrapping, preserve exact spacing
        el.style.width       = 'auto';
        el.textContent       = UTF8ToString($1);

        var rect = el.getBoundingClientRect();
        var w = Math.ceil(rect.width);
        var h = Math.ceil(rect.height);

        // Pack the same way flux_font_web.cpp's measureWebText does, so
        // both files can be reasoned about identically.
        return (w & 0xFFFFF) * 1048576.0 + (h & 0xFFFFF);
    }, cssFont, utf8.c_str());

    int w = (int)(packed / 1048576.0);
    int h = (int)(packed - w * 1048576.0);
    if (w <= 0) w = 0;
    if (h <= 0) h = 0;
    // white-space:pre with no wrap constraint (see the EM_ASM above) —
    // line count is just newlines+1, matching how server-side
    // measureWithFont() counts lines for this same unwrapped case.
    int lineCount = 1;
    for (wchar_t wc : wtext) if (wc == L'\n') ++lineCount;
    outWidth = w > 0 ? w + kWidthSafetyMarginPx : 0;
    outHeight = h > 0 ? h + lineCount * kHeightSafetyMarginPxPerLine : 0;
}

// ============================================================================
// measureDomRichText — wrap-aware measurement matching drawRichText's own
// CSS property choices (see flux_painter_dom.cpp), so what gets measured
// here and what later gets painted there agree with each other. This is
// the wrap-by-letting-the-browser-do-it approach described at the top of
// this file.
// ============================================================================

void measureDomRichText(const std::wstring &wtext, const TextStyle &style,
                        FontCache &fontCache, int maxWidth, bool softWrap,
                        int maxLines, int &outWidth, int &outHeight)
{
    outWidth = outHeight = 0;
    if (wtext.empty())
        return;

    NativeFont fnt = fontCache.getFont(style.fontFamily, style.scaledFontSize(),
                                       style.fontWeight);
    const char *cssFont = static_cast<const char *>(fnt);
    if (!cssFont)
        return;

    std::string utf8 = wToUtf8(wtext);



    // maxWidth <= 0 means "no wrap constraint" (mirrors the canvas
    // backend's wrapTextWeb() convention) — treat identically to
    // softWrap=false for sandbox purposes.
    bool constrained = softWrap && maxWidth > 0;

    double packed = EM_ASM_DOUBLE({
        var el = Module._fluxFontSandbox;
        if (!el) return 0.0;

        el.style.font        = UTF8ToString($0);
        el.style.whiteSpace   = $2 ? 'normal' : 'pre';
        el.style.width        = $2 ? ($1 + 'px') : 'auto';
        el.style.lineHeight   = $3;
        el.style.letterSpacing= $4 + 'px';
        el.style.wordSpacing  = $5 + 'px';

        var maxLines = $6;
        if (maxLines > 0) {
            el.style.display        = '-webkit-box';
            el.style.webkitLineClamp = String(maxLines);
            el.style.webkitBoxOrient = 'vertical';
            el.style.overflow        = 'hidden';
        } else {
            el.style.display  = 'block';
            el.style.overflow = 'visible';
        }

        el.textContent = UTF8ToString($7);

        var rect = el.getBoundingClientRect();
        var w = Math.ceil(rect.width);
        var h = Math.ceil(rect.height);
        return (w & 0xFFFFF) * 1048576.0 + (h & 0xFFFFF);
    }, cssFont, maxWidth, constrained ? 1 : 0,
       (double)style.height, (double)style.letterSpacing,
       (double)style.wordSpacing, maxLines, utf8.c_str());

    int w = (int)(packed / 1048576.0);
    int h = (int)(packed - w * 1048576.0);
    if (w <= 0) { outWidth = 0; outHeight = 0; return; }
    // Wrapped text: line count isn't known directly from
    // getBoundingClientRect(), so derive it from the measured height
    // divided by the same per-line height the CSS line-height gets
    // pinned to (fluxDomLineHeightPx) — matches how many lines
    // flux_painter_dom.cpp's drawRichText will actually render.
    int lineHeightPx = fluxDomLineHeightPx(style.fontFamily, style.scaledFontSize(),
                                           style.fontWeight);
    int lineCount = (lineHeightPx > 0) ? std::max(1, (int)std::round((double)h / lineHeightPx)) : 1;
    outWidth = w + kWidthSafetyMarginPx;
    outHeight = h + lineCount * kHeightSafetyMarginPxPerLine;

}

// ============================================================================
// fluxDomCssFontString — on this backend, NativeFont already IS a CSS font
// string (see this file's header comment), so this is a pure passthrough.
// fontFamily/fontSize/weight are accepted for interface parity with the
// SSR implementation but unused here.
// ============================================================================

std::string fluxDomCssFontString(NativeFont font, const std::string & /*fontFamily*/,
                                 int /*fontSize*/, FontWeight /*weight*/)
{
    const char *cssFont = static_cast<const char *>(font);
    return cssFont ? std::string(cssFont) : std::string();
}


#endif // __EMSCRIPTEN__