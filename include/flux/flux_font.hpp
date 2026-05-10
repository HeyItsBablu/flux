#ifndef FLUX_FONT_HPP
#define FLUX_FONT_HPP

#include "flux_platform.hpp"
#include <map>
#include <string>

// ============================================================================
// FontWeight
// Win32:  FW_LIGHT=300, FW_NORMAL=400, FW_BOLD=700
// Linux:  PANGO_WEIGHT_LIGHT=300, _NORMAL=400, _BOLD=700
// Android: passed to nvgFontSize/nvgFontFace — same numeric values
// ============================================================================

#ifdef _WIN32
    enum class FontWeight {
        Light  = FW_LIGHT,   // 300
        Normal = FW_NORMAL,  // 400
        Bold   = FW_BOLD     // 700
    };
#else
    enum class FontWeight {
        Light  = 300,
        Normal = 400,
        Bold   = 700
    };
#endif

// ============================================================================
// TextDecoration — mirrors Flutter's TextDecoration flags (bitmask)
// ============================================================================

enum class TextDecoration : uint8_t {
    None          = 0,
    Underline     = 1 << 0,
    Overline      = 1 << 1,
    LineThrough   = 1 << 2,
};

inline TextDecoration operator|(TextDecoration a, TextDecoration b) {
    return static_cast<TextDecoration>(
        static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}
inline TextDecoration operator&(TextDecoration a, TextDecoration b) {
    return static_cast<TextDecoration>(
        static_cast<uint8_t>(a) & static_cast<uint8_t>(b));
}
inline bool hasDecoration(TextDecoration flags, TextDecoration flag) {
    return (static_cast<uint8_t>(flags) & static_cast<uint8_t>(flag)) != 0;
}

// ============================================================================
// TextDecorationStyle — how the decoration line is drawn
// ============================================================================

enum class TextDecorationStyle {
    Solid,
    Double,
    Dotted,
    Dashed,
    Wavy,   // Win32: approximated with a custom drawn wave
};

// ============================================================================
// NativeFont
// Win32   : HFONT
// Linux   : void* → PangoFontDescription*
// Android : void* → FluxAndroidFont*  (defined in flux_font_android.cpp)
// ============================================================================

#ifndef _WIN32
    using NativeFont = void*;
#endif

// ============================================================================
// Android font descriptor — forward declared here, defined in
// flux_font_android.cpp.
// ============================================================================

#ifdef __ANDROID__
struct FluxAndroidFont {
    int   nvgHandle = -1;
    float size      = 16.f;
    bool  bold      = false;
    bool  light     = false;
};
#endif

// ============================================================================
// FontCache
// Extended to support underline/strikethrough as part of the font key,
// since Win32 bakes those flags into LOGFONT.
// ============================================================================

class FontCache {
public:
    // Extended key that includes decoration flags baked into the HFONT.
    // Note: only Underline and LineThrough (StrikeOut) are baked into the
    // Win32 HFONT. Overline is drawn manually after the fact.
    struct FontKey {
        std::string family;
        int         size;
        FontWeight  weight;
        bool        underline   = false;
        bool        strikeOut   = false;

        bool operator<(const FontKey& o) const {
            if (family    != o.family)    return family    < o.family;
            if (size      != o.size)      return size      < o.size;
            if (weight    != o.weight)    return weight    < o.weight;
            if (underline != o.underline) return underline < o.underline;
            return strikeOut < o.strikeOut;
        }
    };

private:
    std::map<FontKey, NativeFont> cache;

    static NativeFont createFont(const FontKey& key);

public:
    ~FontCache() { clear(); }

    // ── Original overloads (no decoration) ───────────────────────────────
    NativeFont getFont(const std::string& family, int size, FontWeight weight);
    NativeFont getFont(int size, FontWeight weight);

    // ── Extended overload with decoration flags ───────────────────────────
    NativeFont getFont(const std::string& family, int size, FontWeight weight,
                       bool underline, bool strikeOut);

    void clear();
};

#endif // FLUX_FONT_HPP