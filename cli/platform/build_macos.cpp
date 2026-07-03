#include <cstdio>
#include <cstdlib>
#include <string>
#include <filesystem>
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

// Returns {exit code, project root}. project root is empty on failure.
std::pair<int, fs::path> configure_and_build(bool /*release*/) {
    auto root = find_project_root();
    if (!root) {
        std::fprintf(stderr,
            "ERROR: could not locate project root "
            "(no CMakeLists.txt / config/AppConfig.cmake found above current directory).\n");
        return {1, {}};
    }

    const std::string build_dir = "build/macos";

    std::string command =
        "cd \"" + root->string() + "\" && "
        "cmake -S . -B " + build_dir + " -G Ninja "
        "-DCMAKE_BUILD_TYPE=Debug "
        "-DCMAKE_C_COMPILER=clang "
        "-DCMAKE_CXX_COMPILER=clang++ && "
        "cmake --build " + build_dir;

    std::printf("Building flux (Debug, macOS)...\n\n");
    int rc = std::system(command.c_str());
    if (rc != 0) {
        std::fprintf(stderr, "\nBuild failed (exit code %d).\n", rc);
        return {rc, {}};
    }

    return {0, *root};
}

} // namespace

int macos_build(bool release) {
    auto [rc, root] = configure_and_build(release);
    if (rc == 0) {
        std::printf("\nBuild succeeded.\n");
    }
    return rc;
}

int macos_run(bool release) {
    auto [rc, root] = configure_and_build(release);
    if (rc != 0) return rc;

    fs::path exe = root / "build" / "macos" / "macos" / "flux_app";
    if (!fs::exists(exe)) {
        std::fprintf(stderr, "\nBuild succeeded but could not find %s\n", exe.string().c_str());
        return 1;
    }

    std::printf("\nLaunching %s...\n\n", exe.string().c_str());
    return std::system(("\"" + exe.string() + "\"").c_str());
}