#ifndef FLUX_TEXT_STYLE_HPP
#define FLUX_TEXT_STYLE_HPP

#include "flux_font.hpp"
#include "flux_platform.hpp"
#include <optional>
#include <string>
#include <vector>

// ============================================================================
// TextAlign — mirrors Flutter's TextAlign
// ============================================================================

enum class TextAlign {
    Left,
    Center,
    Right,
    Justify,    // Win32: DT_EXPANDTABS | word-wrap; approximated
    Start,      // Alias for Left (LTR assumed for Win32)
    End,        // Alias for Right
};

// ============================================================================
// TextAlignVertical — vertical alignment inside the widget bounds
// ============================================================================

enum class TextAlignVertical {
    Top,
    Center,
    Bottom,
};

// ============================================================================
// TextOverflow — mirrors Flutter's TextOverflow
// ============================================================================

enum class TextOverflow {
    Clip,       // Hard clip at the widget boundary
    Ellipsis,   // Show "…" at the end of the last visible line
    Fade,       // Fade the last visible characters to transparent
    Visible,    // Allow text to paint outside its bounds (no clipping)
};

// ============================================================================
// TextDirection
// ============================================================================

enum class TextDirection {
    LTR,
    RTL,
};

// ============================================================================
// TextShadow — single shadow entry (Flutter allows a list)
// ============================================================================

struct TextShadow {
    Color color  = Color::fromRGBA(0, 0, 0, 128);
    int   offsetX = 2;
    int   offsetY = 2;
    int   blurRadius = 0;   // Win32: approximated by multiple draws

    TextShadow() = default;
    TextShadow(Color c, int dx, int dy, int blur = 0)
        : color(c), offsetX(dx), offsetY(dy), blurRadius(blur) {}
};

// ============================================================================
// TextStyle — mirrors Flutter's TextStyle
// Carries all per-run styling properties.  TextWidget holds one of these.
// ============================================================================

struct TextStyle {
    // ── Font ──────────────────────────────────────────────────────────────
    std::string fontFamily  = "Segoe UI";
    int         fontSize    = 14;
    FontWeight  fontWeight  = FontWeight::Normal;

    // ── Color ─────────────────────────────────────────────────────────────
    Color color             = Color::fromRGB(0, 0, 0);

    // ── Spacing ───────────────────────────────────────────────────────────
    float letterSpacing     = 0.f;   // extra pixels between characters
    float wordSpacing       = 0.f;   // extra pixels between words
    float height            = 1.0f;  // line height multiplier (1.0 = natural)

    // ── Decoration ────────────────────────────────────────────────────────
    TextDecoration      decoration      = TextDecoration::None;
    TextDecorationStyle decorationStyle = TextDecorationStyle::Solid;
    Color               decorationColor = Color::fromRGB(0, 0, 0);
    int                 decorationThickness = 1;

    // ── Shadows ───────────────────────────────────────────────────────────
    std::vector<TextShadow> shadows;

    // ── Scale ─────────────────────────────────────────────────────────────
    float textScaleFactor   = 1.0f;

    // ── Background ────────────────────────────────────────────────────────
    // Per-run background (distinct from widget background)
    std::optional<Color> backgroundColor;

    // ── Helpers ───────────────────────────────────────────────────────────
    int scaledFontSize() const {
        return static_cast<int>(fontSize * textScaleFactor);
    }

    bool hasUnderline()   const { return hasDecoration(decoration, TextDecoration::Underline);   }
    bool hasLineThrough() const { return hasDecoration(decoration, TextDecoration::LineThrough);  }
    bool hasOverline()    const { return hasDecoration(decoration, TextDecoration::Overline);     }

    // Fluent builder helpers so TextStyle can be constructed inline:
    TextStyle withFontFamily(const std::string &f) const {
        auto s = *this; s.fontFamily = f; return s;
    }
    TextStyle withFontSize(int sz) const {
        auto s = *this; s.fontSize = sz; return s;
    }
    TextStyle withFontWeight(FontWeight w) const {
        auto s = *this; s.fontWeight = w; return s;
    }
    TextStyle withColor(Color c) const {
        auto s = *this; s.color = c; return s;
    }
    TextStyle withLetterSpacing(float ls) const {
        auto s = *this; s.letterSpacing = ls; return s;
    }
    TextStyle withWordSpacing(float ws) const {
        auto s = *this; s.wordSpacing = ws; return s;
    }
    TextStyle withHeight(float h) const {
        auto s = *this; s.height = h; return s;
    }
    TextStyle withDecoration(TextDecoration d) const {
        auto s = *this; s.decoration = d; return s;
    }
    TextStyle withDecorationColor(Color c) const {
        auto s = *this; s.decorationColor = c; return s;
    }
    TextStyle withDecorationStyle(TextDecorationStyle ds) const {
        auto s = *this; s.decorationStyle = ds; return s;
    }
    TextStyle withDecorationThickness(int t) const {
        auto s = *this; s.decorationThickness = t; return s;
    }
    TextStyle withShadow(const TextShadow &sh) const {
        auto s = *this; s.shadows.push_back(sh); return s;
    }
    TextStyle withShadows(const std::vector<TextShadow> &sh) const {
        auto s = *this; s.shadows = sh; return s;
    }
    TextStyle withTextScaleFactor(float f) const {
        auto s = *this; s.textScaleFactor = f; return s;
    }
    TextStyle withBackgroundColor(Color c) const {
        auto s = *this; s.backgroundColor = c; return s;
    }
};

// ============================================================================
// StrutStyle — controls minimum line height / baseline grid
// (Advanced layout feature; used when height consistency matters)
// ============================================================================

struct StrutStyle {
    bool        enabled         = false;
    std::string fontFamily;
    float       fontSize        = 0.f;  // 0 = inherit from TextStyle
    FontWeight  fontWeight      = FontWeight::Normal;
    float       height          = 1.0f;
    float       leading         = 0.f;  // extra leading above lines
    bool        forceStrutHeight= false;// force all lines to strut height
};

#endif // FLUX_TEXT_STYLE_HPP