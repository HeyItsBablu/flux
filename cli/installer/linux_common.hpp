#pragma once
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// Shared between linux_installer.cpp (AppImage) and deb_installer.cpp
// (.deb) — both stage a flux_app binary, a resized icon, and a rendered
// .desktop file before handing off to their own packaging tool.

inline std::string render_template(std::string tpl,
                                    const std::vector<std::pair<std::string, std::string>> &vars) {
  for (const auto &[key, value] : vars) {
    std::string token = "@@" + key + "@@";
    size_t pos = 0;
    while ((pos = tpl.find(token, pos)) != std::string::npos) {
      tpl.replace(pos, token.size(), value);
      pos += value.size();
    }
  }
  return tpl;
}

inline bool tool_on_path(const std::string &cmd) {
  return std::system(("command -v " + cmd + " > /dev/null 2>&1").c_str()) == 0;
}

// linuxdeploy (AppImage) and the freedesktop hicolor icon theme (.deb)
// both require square, standard-size icons — unlike Windows' .ico, which
// bundles multiple resolutions and needs no resize step. FLUX_APP_ICON_PATH
// is a single arbitrary-size PNG shared across all platforms, so Linux
// needs its own resize pass regardless of package format.
inline bool resize_icon(const fs::path &src, const fs::path &dst, int size) {
  std::string cmd = "convert \"" + src.string() + "\" -resize " +
                     std::to_string(size) + "x" + std::to_string(size) +
                     "! \"" + dst.string() + "\"";
  return std::system(cmd.c_str()) == 0;
}

inline fs::path flux_tool_cache_dir() {
  return fs::path(std::getenv("HOME") ? std::getenv("HOME") : ".") /
         ".cache" / "flux" / "tools";
}