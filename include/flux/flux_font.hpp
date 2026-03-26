#ifndef FLUX_FONT_HPP
#define FLUX_FONT_HPP

#include "flux_platform.hpp"
#include <map>
#include <string>

enum class FontWeight { Light = FW_LIGHT, Normal = FW_NORMAL, Bold = FW_BOLD };

class FontCache {
private:
    struct FontKey {
        std::string family;
        int         size;
        FontWeight  weight;
        bool operator<(const FontKey &o) const {
            if (family != o.family) return family < o.family;
            if (size   != o.size)   return size   < o.size;
            return weight < o.weight;
        }
    };

    std::map<FontKey, NativeFont> cache;

    // Win32 implementation detail — defined in flux_font_win32.cpp
    static NativeFont createFont(const std::string &family, int size, FontWeight weight);

public:
    ~FontCache() { clear(); }

    NativeFont getFont(const std::string &family, int size, FontWeight weight);
    NativeFont getFont(int size, FontWeight weight); // backward-compat, uses Segoe UI
    void       clear();
};

#endif // FLUX_FONT_HPP