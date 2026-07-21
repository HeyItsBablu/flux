// cli/platform/build_windows.cpp
#include <cstdio>
#include <cstdlib>
#include <string>
#include <filesystem>
#include <array>
#include <optional>

namespace fs = std::filesystem;

namespace
{

    bool command_exists(const std::string &cmd)
    {
        return std::system(("where " + cmd + " > NUL 2>&1").c_str()) == 0;
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

    std::optional<std::string> run_capture(const std::string &command)
    {
        std::array<char, 4096> buffer{};
        std::string result;

        FILE *pipe = _popen(command.c_str(), "r");
        if (!pipe)
            return std::nullopt;

        while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe))
        {
            result += buffer.data();
        }
        int rc = _pclose(pipe);

        while (!result.empty() && (result.back() == '\n' || result.back() == '\r' || result.back() == ' '))
        {
            result.pop_back();
        }

        if (rc != 0 || result.empty())
            return std::nullopt;
        return result;
    }

    std::optional<fs::path> find_vs_install()
    {
        const char *pf_x86 = std::getenv("ProgramFiles(x86)");
        if (!pf_x86)
        {
            std::fprintf(stderr, "ERROR: could not read %%ProgramFiles(x86)%% environment variable.\n");
            return std::nullopt;
        }

        fs::path vswhere = fs::path(pf_x86) / "Microsoft Visual Studio" / "Installer" / "vswhere.exe";
        if (!fs::exists(vswhere))
        {
            std::fprintf(stderr, "ERROR: vswhere.exe not found. Is Visual Studio installed?\n");
            return std::nullopt;
        }

        std::string cmd = "\"" + vswhere.string() +
                          "\" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath";

        auto out = run_capture(cmd);
        if (!out)
        {
            std::fprintf(stderr, "ERROR: No Visual Studio installation with C++ tools found.\n");
            return std::nullopt;
        }

        return fs::path(*out);
    }

    // Runs vswhere/vcvars/cmake configure+build. Returns {exit code, project root}.
    // project root is empty on failure.
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

        auto vs_path = find_vs_install();
        if (!vs_path)
            return {1, {}};

        fs::path vcvars = *vs_path / "VC" / "Auxiliary" / "Build" / "vcvars64.bat";
        if (!fs::exists(vcvars))
        {
            std::fprintf(stderr, "ERROR: vcvars64.bat not found at expected path: %s\n", vcvars.string().c_str());
            return {1, {}};
        }

        const std::string build_dir = "build\\msvc";

        std::string command =
            "cd /d \"" + root->string() + "\" && "
                                          "call \"" +
            vcvars.string() + "\" && "
                              "cmake -S . -B " +
            build_dir + " -G Ninja "
                        "-DCMAKE_BUILD_TYPE=Debug "
                        "-DCMAKE_C_COMPILER=cl.exe "
                        "-DCMAKE_CXX_COMPILER=cl.exe "
                        "-DFLUX_BUILD_CLI=OFF && "
                        "cmake --build " +
            build_dir;

        std::printf("Building flux (Debug, Windows)...\n\n");
        int rc = std::system(command.c_str());
        if (rc != 0)
        {
            std::fprintf(stderr, "\nBuild failed (exit code %d).\n", rc);
            return {rc, {}};
        }

        return {0, *root};
    }

} // namespace

int windows_doctor()
{
    std::printf("flux doctor (Windows)\n\n");
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

    check("cmake in PATH", command_exists("cmake"));
    check("ninja in PATH", command_exists("ninja"));

    auto vs_path = find_vs_install();
    check("Visual Studio with C++ tools", vs_path.has_value(),
          vs_path ? vs_path->string() : "");

    if (vs_path)
    {
        fs::path vcvars = *vs_path / "VC" / "Auxiliary" / "Build" / "vcvars64.bat";
        check("vcvars64.bat present", fs::exists(vcvars), vcvars.string());
    }

    std::printf("\n%s\n", all_ok ? "All checks passed." : "Some checks failed — see above.");
    return all_ok ? 0 : 1;
}

int windows_build(bool release)
{
    auto [rc, root] = configure_and_build(release);
    if (rc == 0)
    {
        std::printf("\nBuild succeeded.\n");
    }
    return rc;
}

int windows_run(bool release)
{
    auto [rc, root] = configure_and_build(release);
    if (rc != 0)
        return rc;

    fs::path exe = root / "build" / "msvc" / "windows" / "flux_app.exe";
    if (!fs::exists(exe))
    {
        std::fprintf(stderr, "\nBuild succeeded but could not find %s\n", exe.string().c_str());
        return 1;
    }

    std::printf("\nLaunching %s...\n\n", exe.string().c_str());
    return std::system(("\"" + exe.string() + "\"").c_str());
}