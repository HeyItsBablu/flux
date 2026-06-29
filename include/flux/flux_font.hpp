#ifndef FLUX_FONT_HPP
#define FLUX_FONT_HPP

#include "flux_platform.hpp"
#include <map>
#include <string>

// ============================================================================
// FontWeight
// Win32:   DWrite DWRITE_FONT_WEIGHT values (Light=300, Normal=400, Bold=700)
// Linux:   PANGO_WEIGHT_LIGHT=300, _NORMAL=400, _BOLD=700
// Android: passed to  — same numeric values
// macOS:   passed to CTFont — same numeric values
//
// The numeric values are identical across all platforms so the enum is
// defined once without platform guards.
// ============================================================================

enum class FontWeight
{
    Thin       = 100,
    ExtraLight = 200,
    Light      = 300,
    Normal     = 400,
    Medium     = 500,
    SemiBold   = 600,
    Bold       = 700,
    ExtraBold  = 800,
    Black      = 900,
};

// ============================================================================
// TextDecoration — mirrors Flutter's TextDecoration flags (bitmask)
// ============================================================================

enum class TextDecoration : uint8_t
{
    None        = 0,
    Underline   = 1 << 0,
    Overline    = 1 << 1,
    LineThrough = 1 << 2,
};

inline TextDecoration operator|(TextDecoration a, TextDecoration b)
{
    return static_cast<TextDecoration>(
        static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}
inline TextDecoration operator&(TextDecoration a, TextDecoration b)
{
    return static_cast<TextDecoration>(
        static_cast<uint8_t>(a) & static_cast<uint8_t>(b));
}
inline bool hasDecoration(TextDecoration flags, TextDecoration flag)
{
    return (static_cast<uint8_t>(flags) & static_cast<uint8_t>(flag)) != 0;
}

// ============================================================================
// TextDecorationStyle — how the decoration line is drawn
// ============================================================================

enum class TextDecorationStyle
{
    Solid,
    Double,
    Dotted,
    Dashed,
    Wavy,
};

// ============================================================================
// Android font descriptor
// ============================================================================

#ifdef __ANDROID__
struct FluxAndroidFont
{
    int   nvgHandle = -1;
    float size      = 16.f;
    bool  bold      = false;
    bool  light     = false;
};
#endif

// ============================================================================
// FontCache
//
// Platform differences in FontKey:
//
//   Win32  (DWrite):
//     - size   is float  (DWrite is subpixel-accurate)
//     - italic is a first-class field (IDWriteTextFormat takes it at creation)
//     - underline / strikeOut removed — DWrite draws these via
//       IDWriteTextLayout attributes at draw time, not baked into the format
//
//   Linux / Android / macOS / Web:
//     - size   stays int  (Pango / NVG / CoreText APIs use integer sizes)
//     - underline / strikeOut stay in the key (Pango bakes them into
//       PangoFontDescription; NVG does not support them at the font level
//       but the key fields are kept so getFont() call sites compile unchanged)
//
// To keep all call sites compiling on every platform, getFont() overloads
// with (underline, strikeOut) params are provided on all platforms.
// On Win32 those params are accepted but ignored in the key — decorations
// are applied at draw time by Painter::drawRichText via IDWriteTextLayout.
// ============================================================================

class FontCache
{
public:

#ifdef _WIN32
    // ── Win32 / DWrite font key ───────────────────────────────────────────────
    struct FontKey
    {
        std::string family;
        float       size   = 14.f;   // points/px — DWrite takes float
        FontWeight  weight = FontWeight::Normal;
        bool        italic = false;

        bool operator<(const FontKey& o) const
        {
            if (family != o.family) return family < o.family;
            if (size   != o.size)   return size   < o.size;
            if (weight != o.weight) return weight < o.weight;
            return italic < o.italic;
        }
    };

#else
    // ── Non-Win32 font key (unchanged from original) ──────────────────────────
    struct FontKey
    {
        std::string family;
        int         size       = 14;
        FontWeight  weight     = FontWeight::Normal;
        bool        underline  = false;
        bool        strikeOut  = false;

        bool operator<(const FontKey& o) const
        {
            if (family    != o.family)    return family    < o.family;
            if (size      != o.size)      return size      < o.size;
            if (weight    != o.weight)    return weight    < o.weight;
            if (underline != o.underline) return underline < o.underline;
            return strikeOut < o.strikeOut;
        }
    };
#endif

private:
    std::map<FontKey, NativeFont> cache;

    // Platform-specific font object creation.
    // Win32:   returns IDWriteTextFormat*
    // Others:  returns HFONT / PangoFontDescription* / etc.
    NativeFont createFont(const FontKey& key);

    // Win32 only: the DWrite factory is needed to create IDWriteTextFormat.
    // It is set once by FontCache::setDWriteFactory() after D3DDevice::create().
#ifdef _WIN32
    IDWriteFactory3* dwriteFactory_ = nullptr;
#endif

public:
    ~FontCache() { clear(); }

#ifdef _WIN32
    // Must be called before any getFont() on Win32.
    void setDWriteFactory(IDWriteFactory3* factory)
    {
        dwriteFactory_ = factory;
    }
#endif

    // ── Primary overload ──────────────────────────────────────────────────────
    NativeFont getFont(const std::string& family, int size, FontWeight weight);

    // ── Convenience — uses default family ("Segoe UI" / system font) ─────────
    NativeFont getFont(int size, FontWeight weight);

    // ── Extended overload with decoration flags ───────────────────────────────
    // On Win32: underline/strikeOut params are accepted for call-site compat
    //           but are NOT baked into the IDWriteTextFormat (applied at draw).
    // On others: behaves as before.
    NativeFont getFont(const std::string& family, int size, FontWeight weight,
                       bool underline, bool strikeOut);

    // ── Italic variant (Win32 D2D path) ───────────────────────────────────────
    // On non-Win32 platforms this delegates to getFont(family, size, weight).
    NativeFont getFontItalic(const std::string& family, int size,
                             FontWeight weight);

    void clear();
};

#endif // FLUX_FONT_HPP
