// flux_font_android.cpp
// FontCache implementation for Android — uses FluxGL (stb_truetype).
#ifdef __ANDROID__

#include "flux/flux_font.hpp"
#include "flux/flux_gl.hpp"
#include <android/log.h>
#include <string>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  "FluxUI", __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN,  "FluxUI", __VA_ARGS__)

// Provided by flux_android_main.cpp
extern std::string FluxAndroid_getMDIFontPath();

// ── Font path resolution ──────────────────────────────────────────────────────
static std::string resolveFontPath(const std::string& family, FontWeight weight)
{
    if (family == "Roboto"      || family == "Segoe UI" ||
        family == "Sans"        || family == "sans-serif" ||
        family == "Arial"       || family == "Helvetica")
    {
        switch (weight) {
            case FontWeight::Bold:  return "/system/fonts/Roboto-Bold.ttf";
            case FontWeight::Light: return "/system/fonts/Roboto-Light.ttf";
            default:                return "/system/fonts/Roboto-Regular.ttf";
        }
    }
    if (family == "Consolas" || family == "Courier New" ||
        family == "monospace" || family == "Courier")
        return "/system/fonts/DroidSansMono.ttf";

    if (family == "Times New Roman" || family == "serif" || family == "Georgia")
        return "/system/fonts/NotoSerif-Regular.ttf";

    if (family == "Material Design Icons")
    {
        const std::string& p = FluxAndroid_getMDIFontPath();
        if (!p.empty()) return p;
    }

    LOGW("Unknown font family '%s' — falling back to Roboto-Regular", family.c_str());
    return "/system/fonts/Roboto-Regular.ttf";
}

// Internal font name key (same scheme as before, kept for FontCache identity)
static std::string nvgFontName(const std::string& family, FontWeight weight)
{
    std::string name = family;
    switch (weight) {
        case FontWeight::Bold:  name += "_bold";    break;
        case FontWeight::Light: name += "_light";   break;
        default:                name += "_regular"; break;
    }
    return name;
}

// ============================================================================
// FontCache::createFont
// ============================================================================
NativeFont FontCache::createFont(const FontKey& key)
{
    auto* f  = new FluxAndroidFont();
    f->size  = static_cast<float>(key.size);
    f->bold  = (key.weight == FontWeight::Bold);
    f->light = (key.weight == FontWeight::Light);
    f->nvgHandle = -1; // repurposed: glFontIndex

    std::string name = nvgFontName(key.family, key.weight);

    // Try to find an already-registered font first (avoids re-loading from disk)
    int handle = FluxGL_findFont(name);

    if (handle == -1) {
        std::string path = resolveFontPath(key.family, key.weight);
        handle = FluxGL_registerFont(name, path);

        if (handle == -1) {
            LOGW("FluxGL: Failed '%s' from '%s', trying Roboto-Regular",
                 name.c_str(), path.c_str());
            handle = FluxGL_registerFont(name, "/system/fonts/Roboto-Regular.ttf");
        }

        if (handle != -1)
            LOGI("FluxGL: Loaded '%s' glIndex=%d size=%.1f",
                 name.c_str(), handle, f->size);
        else
            LOGW("FluxGL: All fallbacks failed for '%s'", name.c_str());
    }

    f->nvgHandle = handle; // glFontIndex stored here
    return reinterpret_cast<NativeFont>(f);
}

// ============================================================================
// FontCache::getFont overloads
// ============================================================================
NativeFont FontCache::getFont(const std::string& family, int size,
                               FontWeight weight)
{
    return getFont(family, size, weight, false, false);
}

NativeFont FontCache::getFont(int size, FontWeight weight)
{
    return getFont("Segoe UI", size, weight, false, false);
}

NativeFont FontCache::getFont(const std::string& family, int size,
                               FontWeight weight,
                               bool underline, bool strikeOut)
{
    FontKey key{ family, size, weight, underline, strikeOut };
    auto it = cache.find(key);
    if (it != cache.end()) return it->second;
    NativeFont font = createFont(key);
    cache[key] = font;
    return font;
}

NativeFont FontCache::getFontItalic(const std::string& family, int size,
                                     FontWeight weight)
{
    // stb_truetype doesn't synthesize italic; return regular weight.
    // If a dedicated italic TTF is needed, add it as a separate family entry.
    return getFont(family, size, weight, false, false);
}

// ============================================================================
// FontCache::clear
// ============================================================================
void FontCache::clear()
{
    for (auto& pair : cache) {
        auto* f = reinterpret_cast<FluxAndroidFont*>(pair.second);
        delete f;
    }
    cache.clear();
    // Note: FluxGL font data survives clear() — fonts are re-looked-up
    // by name on next getFont() call and FluxGL_findFont() returns them
    // without re-loading from disk.
}

#endif // __ANDROID__