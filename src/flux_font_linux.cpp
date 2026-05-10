// flux_font_linux.cpp
#if defined(__linux__) && !defined(__ANDROID__)

#include "flux/flux_font.hpp"
#include <pango/pangocairo.h>
#include <stdexcept>

static PangoWeight toPangoWeight(FontWeight w) {
    switch (w) {
    case FontWeight::Light:  return PANGO_WEIGHT_LIGHT;
    case FontWeight::Bold:   return PANGO_WEIGHT_BOLD;
    case FontWeight::Normal:
    default:                 return PANGO_WEIGHT_NORMAL;
    }
}

NativeFont FontCache::createFont(const FontKey& key) {
    PangoFontDescription* desc = pango_font_description_new();
    pango_font_description_set_family(desc, key.family.c_str());
    int pangoSize = static_cast<int>(key.size * 0.75 * PANGO_SCALE);
    pango_font_description_set_size(desc, pangoSize);
    pango_font_description_set_weight(desc, toPangoWeight(key.weight));
    pango_font_description_set_style(desc, PANGO_STYLE_NORMAL);
    return reinterpret_cast<NativeFont>(desc);
}

NativeFont FontCache::getFont(const std::string& family, int size,
                               FontWeight weight) {
    return getFont(family, size, weight, false, false);
}

NativeFont FontCache::getFont(int size, FontWeight weight) {
    return getFont("Sans", size, weight, false, false);
}


NativeFont FontCache::getFont(const std::string& family, int size,
                               FontWeight weight,
                               bool underline, bool strikeOut) {
    FontKey key{ family, size, weight, underline, strikeOut };
    auto it = cache.find(key);
    if (it != cache.end())
        return it->second;
    NativeFont font = createFont(key);
    cache[key] = font;
    return font;
}

void FontCache::clear() {
    for (auto& pair : cache) {
        PangoFontDescription* desc =
            reinterpret_cast<PangoFontDescription*>(pair.second);
        pango_font_description_free(desc);
    }
    cache.clear();
}

#endif // __linux__