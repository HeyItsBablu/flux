#include <cstdio>
#include <cstdlib>
#include <string>
#include <filesystem>
#include <array>
#include <optional>

namespace fs = std::filesystem;

namespace {

std::optional<fs::path> find_project_root() {
    fs::path dir = fs::current_path();
    for (int i = 0; i < 16; ++i) {
        if (fs::exists(dir / "CMakeLists.txt") && fs::exists(dir / "config" / "AppConfig.cmake")) {
            return dir;
        }
        fs::path parent = dir.parent_path();
        if (parent.empty() || parent == dir) break;
        dir = parent;
    }
    return std::nullopt;
}

std::optional<std::string> run_capture(const std::string& command) {
    std::array<char, 4096> buffer{};
    std::string result;

    FILE* pipe = _popen(command.c_str(), "r");
    if (!pipe) return std::nullopt;

    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe)) {
        result += buffer.data();
    }
    int rc = _pclose(pipe);

    while (!result.empty() && (result.back() == '\n' || result.back() == '\r' || result.back() == ' ')) {
        result.pop_back();
    }

    if (rc != 0 || result.empty()) return std::nullopt;
    return result;
}

std::optional<fs::path> find_vs_install() {
    const char* pf_x86 = std::getenv("ProgramFiles(x86)");
    if (!pf_x86) {
        std::fprintf(stderr, "ERROR: could not read %%ProgramFiles(x86)%% environment variable.\n");
        return std::nullopt;
    }

    fs::path vswhere = fs::path(pf_x86) / "Microsoft Visual Studio" / "Installer" / "vswhere.exe";
    if (!fs::exists(vswhere)) {
        std::fprintf(stderr, "ERROR: vswhere.exe not found. Is Visual Studio installed?\n");
        return std::nullopt;
    }

    std::string cmd = "\"" + vswhere.string() +
        "\" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath";

    auto out = run_capture(cmd);
    if (!out) {
        std::fprintf(stderr, "ERROR: No Visual Studio installation with C++ tools found.\n");
        return std::nullopt;
    }

    return fs::path(*out);
}

} // namespace

int cmd_build_windows() {
    auto root = find_project_root();
    if (!root) {
        std::fprintf(stderr,
            "ERROR: could not locate project root "
            "(no CMakeLists.txt / config/AppConfig.cmake found above current directory).\n");
        return 1;
    }

    auto vs_path = find_vs_install();
    if (!vs_path) return 1;

    fs::path vcvars = *vs_path / "VC" / "Auxiliary" / "Build" / "vcvars64.bat";
    if (!fs::exists(vcvars)) {
        std::fprintf(stderr, "ERROR: vcvars64.bat not found at expected path: %s\n", vcvars.string().c_str());
        return 1;
    }

    const std::string build_dir = "build\\msvc";

    std::string command =
        "cd /d \"" + root->string() + "\" && "
        "call \"" + vcvars.string() + "\" && "
        "cmake -S . -B " + build_dir + " -G Ninja "
        "-DCMAKE_BUILD_TYPE=Debug "
        "-DCMAKE_C_COMPILER=cl.exe "
        "-DCMAKE_CXX_COMPILER=cl.exe && "
        "cmake --build " + build_dir;

    std::printf("Building flux (Debug, Windows)...\n\n");
    int rc = std::system(command.c_str());
    if (rc != 0) {
        std::fprintf(stderr, "\nBuild failed (exit code %d).\n", rc);
        return rc;
    }

    fs::path exe = *root / build_dir / "windows" / "flux_app.exe";
    if (!fs::exists(exe)) {
        std::fprintf(stderr, "\nBuild succeeded but could not find %s\n", exe.string().c_str());
        return 1;
    }

    std::printf("\nLaunching %s...\n\n", exe.string().c_str());
    return std::system(("\"" + exe.string() + "\"").c_str());
}