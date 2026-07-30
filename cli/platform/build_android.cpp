// cli/platform/build_android.cpp
#include <cstdio>
#include <cstdlib>
#include <string>
#include <filesystem>
#include <optional>
#include <fstream>

namespace fs = std::filesystem;

namespace
{
    bool command_exists(const std::string &cmd)
    {
#if defined(_WIN32)
        return std::system(("where " + cmd + " > NUL 2>&1").c_str()) == 0;
#else
        return std::system(("command -v " + cmd + " > /dev/null 2>&1").c_str()) == 0;
#endif
    }

    // Reads sdk.dir=... out of android/local.properties (Java properties
    // escaping: \\ and \: ). Returns nullopt if the file or key is missing.
    std::optional<fs::path> read_sdk_dir_from_local_properties(const fs::path &android_dir)
    {
        fs::path props = android_dir / "local.properties";
        std::ifstream in(props);
        if (!in)
            return std::nullopt;

        std::string line;
        while (std::getline(in, line))
        {
            const std::string prefix = "sdk.dir=";
            if (line.rfind(prefix, 0) != 0)
                continue;

            std::string raw = line.substr(prefix.size());
            std::string unescaped;
            for (size_t i = 0; i < raw.size(); ++i)
            {
                if (raw[i] == '\\' && i + 1 < raw.size())
                {
                    unescaped += raw[i + 1];
                    ++i;
                }
                else
                {
                    unescaped += raw[i];
                }
            }
            if (!unescaped.empty())
                return fs::path(unescaped);
        }
        return std::nullopt;
    }

    // Resolution order matches Gradle's own: ANDROID_HOME env var, then
    // ANDROID_SDK_ROOT (legacy but still honored), then local.properties.
    std::optional<fs::path> find_android_sdk(const fs::path &android_dir)
    {
        if (const char *home = std::getenv("ANDROID_HOME"); home && fs::exists(home))
            return fs::path(home);

        if (const char *root = std::getenv("ANDROID_SDK_ROOT"); root && fs::exists(root))
            return fs::path(root);

        return read_sdk_dir_from_local_properties(android_dir);
    }

    // Returns a path to adb: prefers the SDK's own platform-tools/adb(.exe)
    // (matches what Gradle just used), falls back to PATH lookup.
    std::optional<fs::path> find_adb(const fs::path &android_dir)
    {
        if (auto sdk = find_android_sdk(android_dir))
        {
#if defined(_WIN32)
            fs::path adb = *sdk / "platform-tools" / "adb.exe";
#else
            fs::path adb = *sdk / "platform-tools" / "adb";
#endif
            if (fs::exists(adb))
                return adb;
        }

        if (command_exists("adb"))
            return fs::path("adb"); // resolved via PATH at exec time

        return std::nullopt;
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

    // Matches android/app/src/main/AndroidManifest.xml in the scaffolded
    // project. TODO: read this from the manifest / build.gradle instead of
    // hardcoding it, once flux create templates it per-project.
    const char *kAndroidPackage  = "com.example.myapplication";
    const char *kAndroidActivity = ".MainActivity";

    std::optional<fs::path> gradlew_path(const fs::path &android_dir)
    {
#if defined(_WIN32)
        fs::path gradlew = android_dir / "gradlew.bat";
#else
        fs::path gradlew = android_dir / "gradlew";
#endif
        if (!fs::exists(gradlew))
            return std::nullopt;
        return gradlew;
    }

    // Runs `gradlew installDebug` from the project's android/ dir.
    int run_gradle_install(const fs::path &root)
    {
        fs::path android_dir = root / "android";
        if (!fs::exists(android_dir))
        {
            std::fprintf(stderr,
                         "ERROR: no 'android/' directory found at %s\n",
                         android_dir.string().c_str());
            return 1;
        }

        auto gradlew = gradlew_path(android_dir);
        if (!gradlew)
        {
            std::fprintf(stderr, "ERROR: gradle wrapper not found in %s\n",
                         android_dir.string().c_str());
            return 1;
        }

        std::string command =
#if defined(_WIN32)
            "cd /d \"" + android_dir.string() + "\" && gradlew.bat installDebug";
#else
            "cd \"" + android_dir.string() + "\" && ./gradlew installDebug";
#endif

        std::printf("Building flux (Debug, Android)...\n\n");
        int rc = std::system(command.c_str());
        if (rc != 0)
        {
            std::fprintf(stderr, "\nBuild failed (exit code %d).\n", rc);
        }
        return rc;
    }
}

int android_doctor()
{
    std::printf("flux doctor (Android)\n\n");
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

    if (root)
    {
        fs::path android_dir = *root / "android";
        check("android/ directory present", fs::exists(android_dir), android_dir.string());
        check("gradle wrapper present", gradlew_path(android_dir).has_value());



    auto sdk = find_android_sdk(android_dir);
    check("Android SDK location resolvable", sdk.has_value(),
          sdk ? sdk->string() : "set ANDROID_HOME, or add sdk.dir to android/local.properties");

    auto adb = find_adb(android_dir);
    check("adb found", adb.has_value(),
          adb ? adb->string() : "not in SDK platform-tools/ and not in PATH");
    }



    std::printf("\n%s\n", all_ok ? "All checks passed." : "Some checks failed — see above.");
    return all_ok ? 0 : 1;
}

int android_build(bool /*release*/)
{
    auto root = find_project_root();
    if (!root)
    {
        std::fprintf(stderr,
                     "ERROR: could not locate project root "
                     "(no CMakeLists.txt / config/AppConfig.cmake found above current directory).\n");
        return 1;
    }

    int rc = run_gradle_install(*root);
    if (rc == 0)
    {
        std::printf("\nBuild succeeded.\n");
    }
    return rc;
}

int android_run(bool /*release*/)
{
    auto root = find_project_root();
    if (!root)
    {
        std::fprintf(stderr,
                     "ERROR: could not locate project root "
                     "(no CMakeLists.txt / config/AppConfig.cmake found above current directory).\n");
        return 1;
    }

    int rc = run_gradle_install(*root);
    if (rc != 0)
        return rc;

    fs::path android_dir = *root / "android";
    auto adb = find_adb(android_dir);
    if (!adb)
    {
        std::fprintf(stderr,
                     "\nBuild succeeded but could not locate 'adb' "
                     "(checked SDK platform-tools/ and PATH).\n");
        return 1;
    }

    std::string launch_cmd =
        "\"" + adb->string() + "\" shell am start -n " +
        std::string(kAndroidPackage) + "/" + kAndroidActivity;

    std::printf("\nLaunching %s...\n\n", kAndroidPackage);
    int rc2 = std::system(launch_cmd.c_str());
    if (rc2 != 0)
    {
        std::fprintf(stderr, "\nInstall succeeded but launch failed (exit code %d). "
                             "Is a device/emulator connected (`adb devices`)?\n", rc2);
    }
    return rc2;
}