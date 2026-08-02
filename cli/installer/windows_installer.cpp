#include "windows_build.hpp"
#include "installer/default_iss_template.hpp"
#include "installer/guid.hpp"
#include "installer/parse_app_config.hpp"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

std::optional<fs::path> registry_iscc_path() {
  // Inno Setup's installer writes InstallLocation under one of these keys
  // depending on per-machine (HKLM, WOW6432Node on 64-bit) vs per-user
  // (HKCU) install — winget's default for GUI installers is per-user,
  // unelevated, which is why the fixed Program Files guesses miss it.
  const char *commands[] = {
      "reg query \"HKCU\\SOFTWARE\\JRSoftware\\Inno Setup 6\" /v InstallLocation",
      "reg query \"HKLM\\SOFTWARE\\WOW6432Node\\JRSoftware\\Inno Setup 6\" /v InstallLocation",
      "reg query \"HKLM\\SOFTWARE\\JRSoftware\\Inno Setup 6\" /v InstallLocation",
  };

  for (const char *cmd : commands) {
    std::array<char, 4096> buffer{};
    std::string output;
    FILE *pipe = _popen((std::string(cmd) + " 2>NUL").c_str(), "r");
    if (!pipe)
      continue;
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe))
      output += buffer.data();
    _pclose(pipe);

    // Line looks like:
    //   InstallLocation    REG_SZ    C:/Users/you/AppData/Local/Programs/Inno Setup 6
    size_t pos = output.find("REG_SZ");
    if (pos == std::string::npos)
      continue;
    pos += std::strlen("REG_SZ");
    while (pos < output.size() && (output[pos] == ' ' || output[pos] == '\t'))
      ++pos;
    size_t end = output.find_first_of("\r\n", pos);
    std::string dir = output.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
    while (!dir.empty() && (dir.back() == '\\' || dir.back() == ' '))
      dir.pop_back();

    if (dir.empty())
      continue;
    fs::path candidate = fs::path(dir) / "ISCC.exe";
    if (fs::exists(candidate))
      return candidate;
  }
  return std::nullopt;
}

std::optional<fs::path> find_iscc() {
  if (std::system("where ISCC.exe > NUL 2>&1") == 0)
    return fs::path("ISCC.exe"); // resolved via PATH when spawned

  const char *candidates[] = {
      "C:\\Program Files (x86)\\Inno Setup 6\\ISCC.exe",
      "C:\\Program Files\\Inno Setup 6\\ISCC.exe",
  };
  for (const char *c : candidates)
    if (fs::exists(c))
      return fs::path(c);

  const char *localAppData = std::getenv("LOCALAPPDATA");
  if (localAppData) {
    fs::path candidate = fs::path(localAppData) / "Programs" / "Inno Setup 6" / "ISCC.exe";
    if (fs::exists(candidate))
      return candidate;
  }

  if (auto reg = registry_iscc_path())
    return reg;

  return std::nullopt;
}

std::string
render_template(std::string tpl,
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

} // namespace

int windows_release() {
  auto [rc, root] = windows_configure_and_build(/*release=*/true, "build\\msvc-release");
  if (rc != 0)
    return rc;

  fs::path exe = root / "build" / "msvc-release" / "windows" / "flux_app.exe";
  if (!fs::exists(exe)) {
    std::fprintf(stderr, "\nERROR: build succeeded but %s was not found.\n",
                 exe.string().c_str());
    return 1;
  }

  fs::path generatedHeader =
      root / "build" / "msvc-release" / "generated" / "AppConfig.generated.h";
  auto cfg = read_app_config(generatedHeader);
  if (!cfg) {
    std::fprintf(stderr, "\nERROR: could not read app config from %s\n",
                 generatedHeader.string().c_str());
    return 1;
  }

  auto iscc = find_iscc();
  if (!iscc) {
    std::fprintf(
        stderr,
        "\nERROR: Inno Setup compiler (ISCC.exe) was not found.\n"
        "Install Inno Setup 6 from https://jrsoftware.org/isinfo.php — make "
        "sure ISCC.exe ends up on PATH, or in its default install location.\n");
    return 1;
  }

  fs::path assetsDir = root / "assets";
  std::string assetsLine;
  if (fs::exists(assetsDir)) {
    assetsLine = "Source: \"" + assetsDir.string() +
                "\\*\"; DestDir: \"{app}\\assets\"; Flags: ignoreversion "
                "recursesubdirs createallsubdirs";
  }

  fs::path outputDir = root / "dist" / "windows";
  fs::create_directories(outputDir);

  std::string guid = deterministic_guid(cfg->bundleId);

  // Reuse the same .ico windows/app.rc already embeds into flux_app.exe
  // (IDI_APP_ICON in windows/resource.h) — no separate PNG-to-ICO
  // conversion exists yet, this file is manually maintained alongside
  // app.rc. SetupIconFile requires a true .ico, so only emit the line if
  // the file is actually there; otherwise Inno just uses its own default
  // wizard icon.
  fs::path windowsIcon = root / "windows" / "app.ico";
  std::string iconLine;
  if (fs::exists(windowsIcon)) {
    iconLine = "SetupIconFile=" + windowsIcon.string();
  }

  std::vector<std::pair<std::string, std::string>> vars = {
      {"APP_NAME", cfg->name},         {"APP_VERSION", cfg->version},
      {"APP_PUBLISHER", cfg->publisher}, {"APP_EXE_NAME", "flux_app.exe"},
      {"APP_EXE_PATH", exe.string()},  {"APP_GUID", guid},
      {"OUTPUT_DIR", outputDir.string()}, {"ICON_FILE_LINE", iconLine},
      {"ASSETS_FILES_LINE", assetsLine},
  };

  fs::path templatePath = root / "installer" / "windows" / "app.iss";
  std::string tpl;
  if (fs::exists(templatePath)) {
    std::ifstream in(templatePath);
    std::stringstream ss;
    ss << in.rdbuf();
    tpl = ss.str();
    std::printf("Using project installer template: %s\n",
               templatePath.string().c_str());
  } else {
    tpl = kDefaultInnoTemplate;
  }

  std::string rendered = render_template(tpl, vars);

  fs::path generatedIss = root / "build" / "installer" / "windows" / "app.iss";
  fs::create_directories(generatedIss.parent_path());
  {
    std::ofstream out(generatedIss);
    out << rendered;
  }

  // Windows cmd.exe quoting quirk: when a command starts with a quote and
  // contains further quoted args later (ISCC path AND the .iss path both
  // have spaces), cmd.exe mis-parses it and truncates at the first space
  // — see the "Inno' is not recognized" failure this produces without the
  // extra wrapping. Wrapping the whole command in one more pair of quotes
  // makes cmd.exe strip that outer pair before parsing, leaving the two
  // inner quoted paths intact.
  std::string command =
      "\"\"" + iscc->string() + "\" \"" + generatedIss.string() + "\"\"";
  std::printf("\nRunning Inno Setup compiler...\n\n");
  int isccRc = std::system(command.c_str());
  if (isccRc != 0) {
    std::fprintf(stderr, "\nInstaller build failed (exit code %d).\n", isccRc);
    return isccRc;
  }

  std::printf("\nInstaller created in %s\n", outputDir.string().c_str());
  return 0;
}