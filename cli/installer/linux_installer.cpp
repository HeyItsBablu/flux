// cli/installer/linux_installer.cpp
#include "platform/linux_build.hpp"
#include "installer/default_desktop_template.hpp"
#include "installer/default_apprun_template.hpp"
#include "installer/parse_app_config.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

std::string render_template(std::string tpl,
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

bool tool_on_path(const std::string &cmd) {
  return std::system(("command -v " + cmd + " > /dev/null 2>&1").c_str()) == 0;
}

// linuxdeploy/appimagetool ship as standalone AppImages themselves (the
// usual distribution form on their GitHub Releases pages) — so unlike
// ISCC.exe there's no installer to detect, just "is it somewhere runnable".
// We check PATH first, then a project-local cache dir, so `flux release`
// can work without requiring a system-wide install.
std::optional<fs::path> find_tool(const std::string &name, const fs::path &cacheDir) {
  if (tool_on_path(name))
    return fs::path(name); // resolved via PATH when spawned

  fs::path cached = cacheDir / name;
  if (fs::exists(cached))
    return cached;

  return std::nullopt;
}

} // namespace

int linux_release() {
  auto [rc, root] = linux_configure_and_build(/*release=*/true, "build/linux-release");
  if (rc != 0)
    return rc;

  fs::path exe = root / "build" / "linux-release" / "linux" / "flux_app";
  if (!fs::exists(exe)) {
    std::fprintf(stderr, "\nERROR: build succeeded but %s was not found.\n",
                 exe.string().c_str());
    return 1;
  }

  fs::path generatedHeader =
      root / "build" / "linux-release" / "generated" / "AppConfig.generated.h";
  auto cfg = read_app_config(generatedHeader);
  if (!cfg) {
    std::fprintf(stderr, "\nERROR: could not read app config from %s\n",
                 generatedHeader.string().c_str());
    return 1;
  }

  fs::path toolCacheDir = fs::path(std::getenv("HOME") ? std::getenv("HOME") : ".") /
                          ".cache" / "flux" / "tools";
  auto linuxdeploy = find_tool("linuxdeploy-x86_64.AppImage", toolCacheDir);
  auto appimagetool = find_tool("appimagetool-x86_64.AppImage", toolCacheDir);

  if (!linuxdeploy || !appimagetool) {
    std::fprintf(stderr,
        "\nERROR: AppImage packaging tools not found.\n"
        "Download these and place them on PATH or in %s:\n"
        "  linuxdeploy  : https://github.com/linuxdeploy/linuxdeploy/releases\n"
        "  appimagetool : https://github.com/AppImage/AppImageKit/releases\n"
        "(grab the x86_64.AppImage builds, chmod +x them)\n",
        toolCacheDir.string().c_str());
    return 1;
  }

  // ── Assemble AppDir ──────────────────────────────────────────────────
  fs::path appDir = root / "build" / "linux-release" / "AppDir";
  std::error_code ec;
  fs::remove_all(appDir, ec); // clean slate each release
  fs::create_directories(appDir / "usr" / "bin");

  fs::copy_file(exe, appDir / "usr" / "bin" / "flux_app",
                fs::copy_options::overwrite_existing);

  // Icon: reuse the same FLUX_APP_ICON_PATH already threaded through
  // AppConfig for every platform, rather than a Linux-only manual file
  // the way windows/app.ico is manually maintained.
  fs::path iconSrc = root / cfg->iconPath;
  std::string iconName = "flux_app_icon";
  bool haveIcon = fs::exists(iconSrc);
  if (haveIcon) {
    std::string ext = iconSrc.extension().string();
    fs::copy_file(iconSrc, appDir / (iconName + ext),
                  fs::copy_options::overwrite_existing);
    iconName += ext == ".png" ? "" : ""; // appimagetool wants just the base name in .desktop
  } else {
    std::fprintf(stderr,
        "WARNING: icon at %s not found — AppImage will use a default icon.\n",
        iconSrc.string().c_str());
  }

  // AppRun
  {
    std::ofstream out(appDir / "AppRun");
    out << kAppRunTemplate;
  }
  fs::permissions(appDir / "AppRun",
                  fs::perms::owner_all | fs::perms::group_read | fs::perms::group_exec |
                      fs::perms::others_read | fs::perms::others_exec,
                  fs::perm_options::add);

  // .desktop — project override at installer/linux/app.desktop, else default
  std::vector<std::pair<std::string, std::string>> vars = {
      {"APP_NAME", cfg->name},
      {"APP_ICON_NAME", haveIcon ? iconName : "flux_app_icon"},
  };
  fs::path templatePath = root / "installer" / "linux" / "app.desktop";
  std::string tpl;
  if (fs::exists(templatePath)) {
    std::ifstream in(templatePath);
    std::stringstream ss;
    ss << in.rdbuf();
    tpl = ss.str();
    std::printf("Using project installer template: %s\n", templatePath.string().c_str());
  } else {
    tpl = kDefaultDesktopTemplate;
  }
  std::string renderedDesktop = render_template(tpl, vars);
  std::string desktopFileName = cfg->name + ".desktop";
  {
    std::ofstream out(appDir / desktopFileName);
    out << renderedDesktop;
  }

  // ── Run linuxdeploy (bundles shared libs pulled in via ldd) ─────────
  fs::path outputDir = root / "dist" / "linux";
  fs::create_directories(outputDir);

  std::string deployCmd =
      "\"" + linuxdeploy->string() + "\" "
      "--appdir \"" + appDir.string() + "\" "
      "--executable \"" + (appDir / "usr" / "bin" / "flux_app").string() + "\" "
      "--desktop-file \"" + (appDir / desktopFileName).string() + "\"";
  if (haveIcon)
    deployCmd += " --icon-file \"" + (appDir / (iconName + iconSrc.extension().string())).string() + "\"";

  std::printf("\nRunning linuxdeploy...\n\n");
  int deployRc = std::system(deployCmd.c_str());
  if (deployRc != 0) {
    std::fprintf(stderr, "\nlinuxdeploy failed (exit code %d).\n", deployRc);
    return deployRc;
  }

  // ── Run appimagetool ──────────────────────────────────────────────
  fs::path outputFile = outputDir / (cfg->name + "-" + cfg->version + "-x86_64.AppImage");
  std::string packCmd =
      "\"" + appimagetool->string() + "\" \"" + appDir.string() + "\" \"" + outputFile.string() + "\"";

  std::printf("\nRunning appimagetool...\n\n");
  int packRc = std::system(packCmd.c_str());
  if (packRc != 0) {
    std::fprintf(stderr, "\nAppImage packaging failed (exit code %d).\n", packRc);
    return packRc;
  }

  std::printf("\nAppImage created: %s\n", outputFile.string().c_str());
  return 0;
}