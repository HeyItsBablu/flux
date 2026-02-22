#ifndef FLUX_FONT_HPP
#define FLUX_FONT_HPP

#include <map>
#include <string>
#include <tuple>
#include <windows.h>

enum class FontWeight { Light = FW_LIGHT, Normal = FW_NORMAL, Bold = FW_BOLD };

class FontCache {
private:
    struct FontKey {
        std::string family;
        int size;
        FontWeight weight;
        bool operator<(const FontKey& o) const {
            if (family != o.family) return family < o.family;
            if (size   != o.size)   return size   < o.size;
            return weight < o.weight;
        }
    };

    std::map<FontKey, HFONT> cache;

public:
    ~FontCache() { clear(); }

    HFONT getFont(const std::string& family, int size, FontWeight weight) {
        FontKey key{ family, size, weight };
        auto it = cache.find(key);
        if (it != cache.end()) return it->second;

        LOGFONTW lf = {};
        lf.lfHeight         = -size;   // negative = character height
        lf.lfWeight         = static_cast<int>(weight);
        lf.lfCharSet        = DEFAULT_CHARSET;
        lf.lfOutPrecision   = OUT_DEFAULT_PRECIS;
        lf.lfClipPrecision  = CLIP_DEFAULT_PRECIS;
        lf.lfQuality        = CLEARTYPE_QUALITY;
        lf.lfPitchAndFamily = DEFAULT_PITCH | FF_DONTCARE;

        // Convert family name to wide — critical for icon fonts
        MultiByteToWideChar(CP_UTF8, 0,
            family.c_str(), -1,
            lf.lfFaceName, LF_FACESIZE);

        HFONT hFont = CreateFontIndirectW(&lf);
        cache[key] = hFont;
        return hFont;
    }

    // Backward-compat overload — uses Segoe UI as default
    HFONT getFont(int size, FontWeight weight) {
        return getFont("Segoe UI", size, weight);
    }

    void clear() {
        for (auto& pair : cache) DeleteObject(pair.second);
        cache.clear();
    }
};

#endif // FLUX_FONT_HPP