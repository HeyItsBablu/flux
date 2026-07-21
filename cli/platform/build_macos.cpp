// cli/platform/build_macos.cpp
#include <cstdio>
#include <cstdlib>
#include <string>
#include <filesystem>
#include <optional>

namespace fs = std::filesystem;

namespace
{

    bool command_exists(const std::string &cmd)
    {
        return std::system(("command -v " + cmd + " > /dev/null 2>&1").c_str()) == 0;
    }

    std::optional<fs::path> find_project_root()
    {
        fs::path dir = fs::current_path();
        for (int i = 0; i < 16; ++i)
        {
            if (fs::exists(dir / "CMakeLists.txt") && fs::exists(dir / "config" / "AppConfig.cmake"))
            {
                return dir;
            }
            fs::path parent = dir.parent_path();
            if (parent.empty() || parent == dir)
                break;
            dir = parent;
        }
        return std::nullopt;
    }

    // Returns {exit code, project root}. project root is empty on failure.
    std::pair<int, fs::path> configure_and_build(bool /*release*/)
    {
        auto root = find_project_root();
        if (!root)
        {
            std::fprintf(stderr,
                         "ERROR: could not locate project root "
                         "(no CMakeLists.txt / config/AppConfig.cmake found above current directory).\n");
            return {1, {}};
        }

        const std::string build_dir = "build/macos";

        std::string command =
            "cd \"" + root->string() + "\" && "
                                       "cmake -S . -B " +
            build_dir + " -G Ninja "
                        "-DCMAKE_BUILD_TYPE=Debug "
                        "-DCMAKE_C_COMPILER=clang "
                        "-DCMAKE_CXX_COMPILER=clang++ "
                        "-DFLUX_BUILD_CLI=OFF && "
                        "cmake --build " +
            build_dir;

        std::printf("Building flux (Debug, macOS)...\n\n");
        int rc = std::system(command.c_str());
        if (rc != 0)
        {
            std::fprintf(stderr, "\nBuild failed (exit code %d).\n", rc);
            return {rc, {}};
        }

        return {0, *root};
    }

} // namespace

int macos_doctor()
{
    std::printf("flux doctor (macOS)\n\n");
    bool all_ok = true;

    auto check = [&](const std::string &label, bool ok, const std::string &detail = "")
    {
        std::printf("  [%s] %s", ok ? " OK " : "FAIL", label.c_str());
        if (!detail.empty())
            std::printf(" (%s)", detail.c_str());
        std::printf("\n");
        if (!ok)
            all_ok = false;
    };

    auto root = find_project_root();
    check("project root found", root.has_value(),
          root ? root->string() : "no CMakeLists.txt / config/AppConfig.cmake found above cwd");

    check("clang in PATH", command_exists("clang"));
    check("clang++ in PATH", command_exists("clang++"));
    check("cmake in PATH", command_exists("cmake"));
    check("ninja in PATH", command_exists("ninja"));

    bool xcode_ok = std::system("xcode-select -p > /dev/null 2>&1") == 0;
    check("Xcode command line tools installed", xcode_ok);

    std::printf("\n%s\n", all_ok ? "All checks passed." : "Some checks failed — see above.");
    return all_ok ? 0 : 1;
}

int macos_build(bool release)
{
    auto [rc, root] = configure_and_build(release);
    if (rc == 0)
    {
        std::printf("\nBuild succeeded.\n");
    }
    return rc;
}

int macos_run(bool release)
{
    auto [rc, root] = configure_and_build(release);
    if (rc != 0)
        return rc;

    fs::path exe = root / "build" / "macos" / "macos" / "flux_app";
    if (!fs::exists(exe))
    {
        std::fprintf(stderr, "\nBuild succeeded but could not find %s\n", exe.string().c_str());
        return 1;
    }

    std::printf("\nLaunching %s...\n\n", exe.string().c_str());
    return std::system(("\"" + exe.string() + "\"").c_str());
}