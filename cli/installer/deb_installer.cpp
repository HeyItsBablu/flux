#include "deb_installer.hpp"
#include "linux_common.hpp"
#include "platform/linux_build.hpp"
#include "installer/default_desktop_template.hpp"
#include "installer/default_control_template.hpp"
#include "installer/parse_app_config.hpp"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

// Debian package names must be lowercase, alphanumerics plus -+. only.
// FLUX_APP_NAME ("My Application") is meant for display (window titles,
// .desktop Name=), not this — package/control metadata needs its own
// sanitized form.
std::string sanitize_package_name(const std::string &name) {
  std::string out;
  for (char c : name) {
    if (std::isalnum(static_cast<unsigned char>(c))) {
      out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    } else if (c == '-' || c == '+' || c == '.') {
      out += c;
    } else if (c == ' ' || c == '_') {
      out += '-';
    }
  }
  if (out.empty())
    out = "flux-app";
  return out;
}

} // namespace

int linux_release_deb() {
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

  std::string pkgName = sanitize_package_name(cfg->name);

  // ── Assemble Debian package root ─────────────────────────────────────
  // Unlike AppImage's AppDir (post-processed by linuxdeploy into its final
  // layout), dpkg-deb packages this tree exactly as laid out here, mapped
  // directly onto the target filesystem root — paths must already match
  // real install locations (/usr/bin, /usr/share/...).
  fs::path pkgRoot = root / "build" / "linux-release" / "deb" / pkgName;
  std::error_code ec;
  fs::remove_all(pkgRoot, ec); // clean slate each release

  fs::path binDir = pkgRoot / "usr" / "bin";
  fs::path appsDir = pkgRoot / "usr" / "share" / "applications";
  fs::path iconsDir = pkgRoot / "usr" / "share" / "icons" / "hicolor" / "256x256" / "apps";
  fs::create_directories(binDir);
  fs::create_directories(appsDir);
  fs::create_directories(iconsDir);

  fs::copy_file(exe, binDir / "flux_app", fs::copy_options::overwrite_existing);
  fs::permissions(binDir / "flux_app",
                  fs::perms::owner_all | fs::perms::group_read | fs::perms::group_exec |
                      fs::perms::others_read | fs::perms::others_exec,
                  fs::perm_options::add);

  // Icon — same resize step as the AppImage path, same source of truth
  // (FLUX_APP_ICON_PATH), different destination layout.
  fs::path iconSrc = root / cfg->iconPath;
  std::string iconName = pkgName;
  bool haveIcon = fs::exists(iconSrc);
  if (haveIcon) {
    if (!tool_on_path("convert")) {
      std::fprintf(stderr,
          "\nERROR: ImageMagick's `convert` is required to resize the app "
          "icon for .deb packaging. Install it with:\n"
          "  sudo apt install imagemagick\n");
      return 1;
    }
    if (!resize_icon(iconSrc, iconsDir / (iconName + ".png"), 256)) {
      std::fprintf(stderr, "\nERROR: failed to resize icon %s\n", iconSrc.string().c_str());
      return 1;
    }
  } else {
    std::fprintf(stderr,
        "WARNING: icon at %s not found — package will have no icon.\n",
        iconSrc.string().c_str());
  }

  // .desktop — same project-override pattern (installer/linux/app.desktop)
  // the AppImage path already uses, so one template covers both formats.
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
  } else {
    tpl = kDefaultDesktopTemplate;
  }
  {
    std::ofstream out(appsDir / (pkgName + ".desktop"));
    out << render_template(tpl, vars);
  }

  // DEBIAN/control — project override at installer/linux/control, else default
  fs::path controlDir = pkgRoot / "DEBIAN";
  fs::create_directories(controlDir);
  std::vector<std::pair<std::string, std::string>> controlVars = {
      {"APP_PACKAGE_NAME", pkgName},
      {"APP_VERSION", cfg->version},
      {"APP_PUBLISHER", cfg->publisher},
      {"APP_NAME", cfg->name},
  };
  fs::path controlTemplatePath = root / "installer" / "linux" / "control";
  std::string controlTpl;
  if (fs::exists(controlTemplatePath)) {
    std::ifstream in(controlTemplatePath);
    std::stringstream ss;
    ss << in.rdbuf();
    controlTpl = ss.str();
    std::printf("Using project installer template: %s\n", controlTemplatePath.string().c_str());
  } else {
    controlTpl = kDefaultControlTemplate;
  }
  {
    std::ofstream out(controlDir / "control");
    out << render_template(controlTpl, controlVars);
  }

  // ── Run dpkg-deb ──────────────────────────────────────────────────
  if (!tool_on_path("dpkg-deb")) {
    std::fprintf(stderr,
        "\nERROR: dpkg-deb not found. It ships with dpkg on Debian/Ubuntu — "
        "install with:\n  sudo apt install dpkg\n");
    return 1;
  }

  fs::path outputDir = root / "dist" / "linux";
  fs::create_directories(outputDir);
  fs::path outputFile = outputDir / (pkgName + "_" + cfg->version + "_amd64.deb");

  std::string buildCmd = "dpkg-deb --build --root-owner-group \"" +
                         pkgRoot.string() + "\" \"" + outputFile.string() + "\"";

  std::printf("\nRunning dpkg-deb...\n\n");
  int buildRc = std::system(buildCmd.c_str());
  if (buildRc != 0) {
    std::fprintf(stderr, "\n.deb packaging failed (exit code %d).\n", buildRc);
    return buildRc;
  }

  std::printf("\n.deb package created: %s\n", outputFile.string().c_str());
  std::printf("Install with: sudo apt install %s\n", outputFile.string().c_str());
  return 0;
}