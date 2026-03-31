#ifndef FLUX_FONT_HPP
#define FLUX_FONT_HPP

#include "flux_platform.hpp"
#include <map>
#include <string>

// ============================================================================
// FontWeight
// Win32:  FW_LIGHT=300, FW_NORMAL=400, FW_BOLD=700  (LOGFONT lfWeight)
// Linux:  PANGO_WEIGHT_LIGHT=300, _NORMAL=400, _BOLD=700
// The numeric values are identical so we use plain integers internally;
// each platform .cpp maps them to its own enum.
// ============================================================================

#ifdef _WIN32
    // On Win32 we can use FW_* directly — they are just ints.
    enum class FontWeight {
        Light  = FW_LIGHT,   // 300
        Normal = FW_NORMAL,  // 400
        Bold   = FW_BOLD     // 700
    };
#else
    // Portable definition — same numeric values, no Win32 headers needed.
    enum class FontWeight {
        Light  = 300,
        Normal = 400,
        Bold   = 700
    };
#endif

// ============================================================================
// NativeFont
// Win32 : HFONT   (defined in flux_platform.hpp under _WIN32)
// Linux : void*   — stores a PangoFontDescription* cast to void*
//                   cast back in flux_font_linux.cpp and flux_painter_linux.cpp
// ============================================================================

#ifndef _WIN32
    using NativeFont = void*;
#endif
// (Win32 NativeFont = HFONT is already typedef'd in flux_platform.hpp)

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

    // Platform-specific font creation — implemented in:
    //   flux_font_win32.cpp   (_WIN32)
    //   flux_font_linux.cpp   (__linux__)
    static NativeFont createFont(const std::string& family,
                                  int                size,
                                  FontWeight         weight);

public:
    ~FontCache() { clear(); }

    // Full overload — explicit family
    NativeFont getFont(const std::string& family, int size, FontWeight weight);

    // Backward-compat — uses platform default:
    //   Win32 → "Segoe UI"
    //   Linux → "Sans"
    NativeFont getFont(int size, FontWeight weight);

    void clear();
};

#endif // FLUX_FONT_HPP