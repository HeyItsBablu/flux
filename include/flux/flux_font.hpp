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
// flux_font_android.cpp. NanoVG identifies fonts by integer handle,
// but we also need to carry size/weight for nvgFontSize() calls since
// NanoVG font state is set per-frame, not baked into the handle.
// ============================================================================

#ifdef __ANDROID__
struct FluxAndroidFont {
    int   nvgHandle = -1;   // returned by nvgCreateFont / nvgFindFont
    float size      = 16.f;
    bool  bold      = false;
    bool  light     = false;
};
#endif

// ============================================================================
// FontCache
// ============================================================================

class FontCache {
private:
    struct FontKey {
        std::string family;
        int         size;
        FontWeight  weight;

        bool operator<(const FontKey& o) const {
            if (family != o.family) return family < o.family;
            if (size   != o.size)   return size   < o.size;
            return weight < o.weight;
        }
    };

    std::map<FontKey, NativeFont> cache;

    static NativeFont createFont(const std::string& family,
                                  int                size,
                                  FontWeight         weight);

public:
    ~FontCache() { clear(); }

    NativeFont getFont(const std::string& family, int size, FontWeight weight);
    NativeFont getFont(int size, FontWeight weight);

    void clear();
};

#endif // FLUX_FONT_HPP