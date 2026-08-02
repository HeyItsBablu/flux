#pragma once
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>

struct AppReleaseConfig {
  std::string name;
  std::string bundleId;
  std::string version;
  std::string iconPath;
  std::string publisher;
};

namespace detail {
inline std::optional<std::string> extract_define(const std::string &contents,
                                                   const std::string &key) {
  std::string needle = "#define " + key + " ";
  size_t pos = contents.find(needle);
  if (pos == std::string::npos)
    return std::nullopt;
  pos += needle.size();

  size_t lineEnd = contents.find('\n', pos);
  size_t quoteStart = contents.find('"', pos);
  // FLUX_APP_BUILD is an unquoted int define — skip it if the quote we
  // found belongs to a later line.
  if (quoteStart == std::string::npos ||
      (lineEnd != std::string::npos && quoteStart > lineEnd))
    return std::nullopt;

  size_t quoteEnd = contents.find('"', quoteStart + 1);
  if (quoteEnd == std::string::npos)
    return std::nullopt;

  return contents.substr(quoteStart + 1, quoteEnd - quoteStart - 1);
}
} // namespace detail

inline std::optional<AppReleaseConfig>
read_app_config(const std::filesystem::path &generatedHeader) {
  std::ifstream in(generatedHeader);
  if (!in)
    return std::nullopt;

  std::stringstream ss;
  ss << in.rdbuf();
  std::string contents = ss.str();

  auto name = detail::extract_define(contents, "FLUX_APP_NAME");
  auto bundle = detail::extract_define(contents, "FLUX_APP_BUNDLE_ID");
  auto version = detail::extract_define(contents, "FLUX_APP_VERSION");
  auto icon = detail::extract_define(contents, "FLUX_APP_ICON_PATH");
  auto publisher = detail::extract_define(contents, "FLUX_APP_PUBLISHER");

  if (!name || !bundle || !version || !icon)
    return std::nullopt;

  AppReleaseConfig cfg;
  cfg.name = *name;
  cfg.bundleId = *bundle;
  cfg.version = *version;
  cfg.iconPath = *icon;
  cfg.publisher = publisher.value_or(cfg.name); // fall back if unset
  return cfg;
}