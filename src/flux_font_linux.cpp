// flux_font_linux.cpp
#ifdef __linux__

#include "flux/flux_font.hpp"

// Pango is the standard text/font engine on Linux desktops (GTK, GNOME, etc.)
// It integrates directly with Cairo via pango-cairo, giving us:
//   - Font family + weight + size  →  PangoFontDescription*  (= NativeFont)
//   - Text measurement             →  pango_layout_get_pixel_extents()
//   - Rendering                    →  pango_cairo_show_layout()
//
// Build flags needed:
//   pkg-config --cflags --libs pangocairo
//   → typically: -I/usr/include/pango-1.0 -I/usr/include/glib-2.0 ...
//                -lpangocairo-1.0 -lpango-1.0 -lglib-2.0

#include <pango/pangocairo.h>
#include <stdexcept>

// ============================================================================
// FontWeight → Pango weight mapping
// Mirrors Win32: FW_LIGHT=300, FW_NORMAL=400, FW_BOLD=700
// ============================================================================

static PangoWeight toPangoWeight(FontWeight w) {
    switch (w) {
    case FontWeight::Light:  return PANGO_WEIGHT_LIGHT;   // 300
    case FontWeight::Bold:   return PANGO_WEIGHT_BOLD;    // 700
    case FontWeight::Normal:
    default:                 return PANGO_WEIGHT_NORMAL;  // 400
    }
}

// ============================================================================
// FontCache::createFont
//
// Returns a PangoFontDescription* cast to NativeFont (void*).
// The caller (getFont) owns the object and stores it in the cache.
// FontCache::clear() calls pango_font_description_free() on each entry.
//
// Size convention: matches Win32 lfHeight = -size (i.e. size in points,
// not pixels).  Pango sizes are in Pango units = points * PANGO_SCALE.
// ============================================================================

NativeFont FontCache::createFont(const std::string& family,
                                  int                size,
                                  FontWeight         weight) {
    PangoFontDescription* desc = pango_font_description_new();

    pango_font_description_set_family(desc, family.c_str());

    // Win32 lfHeight is negative pixels; Pango wants points × PANGO_SCALE.
    // On a 96-dpi screen: points = pixels × 72 / 96 = pixels × 0.75
    // We preserve the Win32 convention of passing raw pixel size and convert.
    int pangoSize = static_cast<int>(size * 0.75 * PANGO_SCALE);
    pango_font_description_set_size(desc, pangoSize);

    pango_font_description_set_weight(desc, toPangoWeight(weight));
    pango_font_description_set_style(desc, PANGO_STYLE_NORMAL);

    return reinterpret_cast<NativeFont>(desc);
}

// ============================================================================
// FontCache::getFont  (family overload)
// ============================================================================

NativeFont FontCache::getFont(const std::string& family,
                               int                size,
                               FontWeight         weight) {
    FontKey key{ family, size, weight };

    auto it = cache.find(key);
    if (it != cache.end())
        return it->second;

    NativeFont font = createFont(family, size, weight);
    cache[key]      = font;
    return font;
}

// ============================================================================
// FontCache::getFont  (backward-compat overload — uses "Sans" on Linux)
//
// Win32 defaults to "Segoe UI" which does not exist on Linux.
// "Sans" resolves to the system sans-serif (usually DejaVu Sans or
// Noto Sans) via fontconfig, giving the same intent.
// ============================================================================

NativeFont FontCache::getFont(int size, FontWeight weight) {
    return getFont("Sans", size, weight);
}

// ============================================================================
// FontCache::clear
// ============================================================================

void FontCache::clear() {
    for (auto& pair : cache) {
        PangoFontDescription* desc =
            reinterpret_cast<PangoFontDescription*>(pair.second);
        pango_font_description_free(desc);
    }
    cache.clear();
}

#endif // __linux__