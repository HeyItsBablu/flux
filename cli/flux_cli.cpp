#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#if defined(_WIN32)
int windows_build(bool release);
int windows_run(bool release);
#elif defined(__APPLE__)
int macos_build(bool release);
int macos_run(bool release);
#elif defined(__linux__)
int linux_build(bool release);
int linux_run(bool release);
#endif

namespace {

void print_usage() {
    std::printf("flux - FluxUI project CLI\n\n");
    std::printf("Usage:\n");
    std::printf("  flux run <platform> [--release]     Build and launch\n");
    std::printf("  flux build <platform> [--release]   Build only\n\n");
    std::printf("Platforms:\n");
    std::printf("  windows  linux  macos  web  android\n");
}

bool has_flag(const std::vector<std::string>& args, const std::string& flag) {
    for (const auto& a : args) {
        if (a == flag) return true;
    }
    return false;
}

// Platforms that require running the flux CLI on that same OS.
bool is_desktop_platform(const std::string& p) {
    return p == "windows" || p == "linux" || p == "macos";
}

bool is_valid_platform(const std::string& p) {
    return p == "windows" || p == "linux" || p == "macos" || p == "web" || p == "android";
}

int stub_not_implemented(const std::string& platform) {
    std::fprintf(stderr, "flux: '%s' support is not implemented yet.\n", platform.c_str());
    return 1;
}

int dispatch(const std::string& command, const std::string& platform, bool release) {
    if (platform == "windows") {
#if defined(_WIN32)
        if (release) {
            std::printf("note: --release is not implemented yet, building Debug instead.\n\n");
        }
        return (command == "run") ? windows_run(false) : windows_build(false);
#else
        std::fprintf(stderr,
            "flux: 'windows' target requires running flux from a Windows host.\n");
        return 1;
#endif
    }

    if (platform == "macos") {
#if defined(__APPLE__)
        if (release) {
            std::printf("note: --release is not implemented yet, building Debug instead.\n\n");
        }
        return (command == "run") ? macos_run(false) : macos_build(false);
#else
        std::fprintf(stderr,
            "flux: 'macos' target requires running flux from a macOS host.\n");
        return 1;
#endif
    }

    if (platform == "linux") {
#if defined(__linux__)
        if (release) {
            std::printf("note: --release is not implemented yet, building Debug instead.\n\n");
        }
        return (command == "run") ? linux_run(false) : linux_build(false);
#else
        std::fprintf(stderr,
            "flux: 'linux' target requires running flux from a Linux host.\n");
        return 1;
#endif
    }

    if (platform == "web" || platform == "android") {
        // Reachable from any host, but not yet implemented.
        return stub_not_implemented(platform);
    }

    std::fprintf(stderr, "flux: unknown platform '%s'\n\n", platform.c_str());
    print_usage();
    return 1;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        print_usage();
        return 1;
    }

    const std::string command = argv[1];
    if (command != "run" && command != "build") {
        if (command == "help") {
            print_usage();
            return 0;
        }
        std::fprintf(stderr, "flux: unknown command '%s'\n\n", command.c_str());
        print_usage();
        return 1;
    }

    if (argc < 3) {
        std::fprintf(stderr, "flux: missing platform argument.\n\n");
        print_usage();
        return 1;
    }

    const std::string platform = argv[2];
    if (!is_valid_platform(platform)) {
        std::fprintf(stderr, "flux: unknown platform '%s'\n\n", platform.c_str());
        print_usage();
        return 1;
    }

    std::vector<std::string> flags;
    for (int i = 3; i < argc; ++i) flags.emplace_back(argv[i]);
    const bool release = has_flag(flags, "--release");

    (void)is_desktop_platform; // reserved for host-compatibility checks as platforms are added

    return dispatch(command, platform, release);
}