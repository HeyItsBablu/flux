//flux_font_win32.cpp
#include "flux/flux_font.hpp"

#ifdef _WIN32

// -----------------------------------------------------------------------
// FontCache::createFont  — Win32 font creation
// -----------------------------------------------------------------------

static BYTE getCharset(const std::string &family) {
    HDC dc = GetDC(nullptr);

    LOGFONTW lf = {};
    MultiByteToWideChar(CP_UTF8, 0, family.c_str(), -1, lf.lfFaceName, LF_FACESIZE);
    lf.lfCharSet = DEFAULT_CHARSET;

    BYTE result = DEFAULT_CHARSET;
    EnumFontFamiliesExW(
        dc, &lf,
        [](const LOGFONTW *, const TEXTMETRICW *tm, DWORD, LPARAM lParam) -> int {
            *reinterpret_cast<BYTE *>(lParam) = tm->tmCharSet;
            return 0;
        },
        (LPARAM)&result, 0);

    ReleaseDC(nullptr, dc);
    return result;
}

NativeFont FontCache::createFont(const std::string &family, int size, FontWeight weight) {
    LOGFONTW lf        = {};
    lf.lfHeight        = -size;
    lf.lfWeight        = static_cast<int>(weight);
    lf.lfCharSet       = getCharset(family);
    lf.lfOutPrecision  = OUT_DEFAULT_PRECIS;
    lf.lfClipPrecision = CLIP_DEFAULT_PRECIS;
    lf.lfQuality       = CLEARTYPE_QUALITY;
    lf.lfPitchAndFamily = DEFAULT_PITCH | FF_DONTCARE;

    MultiByteToWideChar(CP_UTF8, 0, family.c_str(), -1, lf.lfFaceName, LF_FACESIZE);

    return CreateFontIndirectW(&lf);
}

// -----------------------------------------------------------------------
// FontCache public methods
// -----------------------------------------------------------------------

NativeFont FontCache::getFont(const std::string &family, int size, FontWeight weight) {
    FontKey key{ family, size, weight };
    auto it = cache.find(key);
    if (it != cache.end())
        return it->second;

    NativeFont font = createFont(family, size, weight);
    cache[key] = font;
    return font;
}

NativeFont FontCache::getFont(int size, FontWeight weight) {
    return getFont("Segoe UI", size, weight);
}

void FontCache::clear() {
    for (auto &pair : cache)
        DeleteObject(pair.second);   // Win32 — lives here now
    cache.clear();
}

#endif // _WIN32