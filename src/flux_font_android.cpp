// flux_font_android.cpp
#ifdef __ANDROID__

#include "flux/flux_font.hpp"
#include "nanovg.h"
#include <android/log.h>
#include <string>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  "FluxUI", __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN,  "FluxUI", __VA_ARGS__)

extern NVGcontext* FluxAndroid_getVG();

// ============================================================================
// DPI scale — set once from native-lib.cpp after AConfiguration is read.
// Default 1.0 so fonts are usable even if setDpiScale is never called.
// ============================================================================

static float s_dpiScale = 1.0f;

extern void  FluxAndroid_setDpiScale(float scale);
extern float FluxAndroid_getDpiScale();

// ============================================================================
// Font file resolution
// ============================================================================

static std::string resolveFontPath(const std::string& family, FontWeight weight) {
    if (family == "Roboto" || family == "Segoe UI" ||
        family == "Sans"   || family == "sans-serif" ||
        family == "Arial"  || family == "Helvetica") {
        switch (weight) {
            case FontWeight::Bold:  return "/system/fonts/Roboto-Bold.ttf";
            case FontWeight::Light: return "/system/fonts/Roboto-Light.ttf";
            default:                return "/system/fonts/Roboto-Regular.ttf";
        }
    }
    if (family == "Consolas" || family == "Courier New" ||
        family == "monospace" || family == "Courier") {
        return "/system/fonts/DroidSansMono.ttf";
    }
    if (family == "Times New Roman" || family == "serif" ||
        family == "Georgia") {
        return "/system/fonts/NotoSerif-Regular.ttf";
    }
    LOGW("Unknown font family '%s' — falling back to Roboto-Regular", family.c_str());
    return "/system/fonts/Roboto-Regular.ttf";
}

// ============================================================================
// NVG font name
// ============================================================================

static std::string nvgFontName(const std::string& family, FontWeight weight) {
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

NativeFont FontCache::createFont(const std::string& family,
                                 int                size,
                                 FontWeight         weight) {
    NVGcontext* vg = FluxAndroid_getVG();

    auto* f  = new FluxAndroidFont();
    f->size = static_cast<float>(size);
    f->bold  = (weight == FontWeight::Bold);
    f->light = (weight == FontWeight::Light);

    if (!vg) {
        LOGW("createFont('%s', %d) before NVG ready", family.c_str(), size);
        return reinterpret_cast<NativeFont>(f);
    }

    std::string name   = nvgFontName(family, weight);
    int         handle = nvgFindFont(vg, name.c_str());

    if (handle == -1) {
        std::string path = resolveFontPath(family, weight);
        handle = nvgCreateFont(vg, name.c_str(), path.c_str());
        if (handle == -1) {
            LOGW("Failed '%s' from '%s', trying Roboto-Regular",
                 name.c_str(), path.c_str());
            handle = nvgCreateFont(vg, name.c_str(),
                                   "/system/fonts/Roboto-Regular.ttf");
        }
        if (handle != -1)
            LOGI("Loaded '%s' handle=%d size=%.1f", name.c_str(), handle, f->size);
        else
            LOGW("All fallbacks failed for '%s'", name.c_str());
    }

    f->nvgHandle = handle;
    return reinterpret_cast<NativeFont>(f);
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
// FontCache::getFont  (default family)
// ============================================================================

NativeFont FontCache::getFont(int size, FontWeight weight) {
    return getFont("Segoe UI", size, weight);
}

// ============================================================================
// FontCache::clear
// ============================================================================

void FontCache::clear() {
    for (auto& pair : cache) {
        auto* f = reinterpret_cast<FluxAndroidFont*>(pair.second);
        delete f;
    }
    cache.clear();
}

#endif // __ANDROID__