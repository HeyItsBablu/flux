#ifndef FLUX_FONT_HPP
#define FLUX_FONT_HPP

#include <map>
#include <tuple>
#include <windows.h>

enum class FontWeight { Light = FW_LIGHT, Normal = FW_NORMAL, Bold = FW_BOLD };

class FontCache {
private:
  std::map<std::tuple<int, FontWeight>, HFONT> cache;

public:
  ~FontCache() {
    for (auto &pair : cache) {
      DeleteObject(pair.second);
    }
    cache.clear();
  }

  HFONT getFont(int size, FontWeight weight) {
    auto key = std::make_tuple(size, weight);
    auto it = cache.find(key);

    if (it != cache.end()) {
      return it->second;
    }

    HFONT hFont =
        CreateFont(size, 0, 0, 0, static_cast<int>(weight), FALSE, FALSE, FALSE,
                   DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                   CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");

    cache[key] = hFont;
    return hFont;
  }

  void clear() {
    for (auto &pair : cache) {
      DeleteObject(pair.second);
    }
    cache.clear();
  }
};

#endif // FLUX_FONT_HPP