// flux_font_win32.cpp
#include "flux/flux_font.hpp"

#ifdef _WIN32

// -----------------------------------------------------------------------
// Internal helper — queries the charset for a given family name.
// -----------------------------------------------------------------------

static BYTE getCharset(const std::string &family) {
  HDC dc = GetDC(nullptr);

  LOGFONTW lf = {};
  MultiByteToWideChar(CP_UTF8, 0, family.c_str(), -1, lf.lfFaceName,
                      LF_FACESIZE);
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

// -----------------------------------------------------------------------
// FontCache::createFont  — Win32 HFONT creation (decoration-aware)
// -----------------------------------------------------------------------

NativeFont FontCache::createFont(const FontKey &key) {
  LOGFONTW lf = {};
  lf.lfHeight = -key.size;
  lf.lfWeight = static_cast<int>(key.weight);
  lf.lfCharSet = getCharset(key.family);
  lf.lfOutPrecision = OUT_DEFAULT_PRECIS;
  lf.lfClipPrecision = CLIP_DEFAULT_PRECIS;
  lf.lfQuality = CLEARTYPE_QUALITY;
  lf.lfPitchAndFamily = DEFAULT_PITCH | FF_DONTCARE;
  lf.lfUnderline = key.underline ? TRUE : FALSE;
  lf.lfStrikeOut = key.strikeOut ? TRUE : FALSE;

  MultiByteToWideChar(CP_UTF8, 0, key.family.c_str(), -1, lf.lfFaceName,
                      LF_FACESIZE);

  return CreateFontIndirectW(&lf);
}

// -----------------------------------------------------------------------
// FontCache public methods
// -----------------------------------------------------------------------

NativeFont FontCache::getFont(const std::string &family, int size,
                              FontWeight weight) {
  return getFont(family, size, weight, false, false);
}

NativeFont FontCache::getFont(int size, FontWeight weight) {
  return getFont("Segoe UI", size, weight, false, false);
}

NativeFont FontCache::getFont(const std::string &family, int size,
                              FontWeight weight, bool underline,
                              bool strikeOut) {
  FontKey key{family, size, weight, underline, strikeOut};
  auto it = cache.find(key);
  if (it != cache.end())
    return it->second;

  NativeFont font = createFont(key);
  cache[key] = font;
  return font;
}

void FontCache::clear() {
  for (auto &pair : cache)
    DeleteObject(pair.second);
  cache.clear();
}

#endif // _WIN32