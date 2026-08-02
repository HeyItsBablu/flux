#include "create.hpp"
#include "package_manager.hpp"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#if defined(_WIN32)
int windows_build(bool release);
int windows_run(bool release);
int windows_doctor();
int windows_release();
#elif defined(__APPLE__)
int macos_build(bool release);
int macos_run(bool release);
int macos_doctor();
#elif defined(__linux__)
int linux_build(bool release);
int linux_run(bool release);
int linux_doctor();
#endif

// Buildable/runnable from any host — only needs gradlew + adb in PATH.
int android_build(bool release);
int android_run(bool release);
int android_doctor();

namespace {

void print_usage() {
  std::printf("flux - FluxUI project CLI\n\n");
  std::printf("Usage:\n");
  std::printf("  flux create <name>                  Scaffold a new project\n");
  std::printf("  flux run <platform> [--release]     Build and launch\n");
  std::printf("  flux build <platform> [--release]   Build only\n");
  std::printf("  flux release <platform>             Build a distributable installer\n");
  std::printf("  flux doctor [platform]              Check host toolchain\n");
  std::printf("  flux add <package> [--ref <ref>]    Add a dependency\n");
  std::printf("  flux remove <package>               Remove a dependency\n\n");
  std::printf("  flux install                        Install dependencies from "
              "flux.deps.json\n");
  std::printf("Platforms:\n");
  std::printf("  windows  linux  macos  web  android\n");
}

std::string host_platform_name() {
#if defined(_WIN32)
  return "windows";
#elif defined(__APPLE__)
  return "macos";
#elif defined(__linux__)
  return "linux";
#else
  return "";
#endif
}

int dispatch_doctor(const std::string &platform) {
  if (platform == "windows") {
#if defined(_WIN32)
    return windows_doctor();
#else
    std::fprintf(stderr, "flux: 'windows' doctor checks require running flux "
                         "from a Windows host.\n");
    return 1;
#endif
  }

  if (platform == "macos") {
#if defined(__APPLE__)
    return macos_doctor();
#else
    std::fprintf(stderr, "flux: 'macos' doctor checks require running flux "
                         "from a macOS host.\n");
    return 1;
#endif
  }

  if (platform == "linux") {
#if defined(__linux__)
    return linux_doctor();
#else
    std::fprintf(stderr, "flux: 'linux' doctor checks require running flux "
                         "from a Linux host.\n");
    return 1;
#endif
  }

  if (platform == "android") {
    return android_doctor();
  }

  // if (platform == "web")
  // {
  //     return stub_not_implemented(platform);
  // }

  std::fprintf(stderr, "flux: unknown platform '%s'\n\n", platform.c_str());
  print_usage();
  return 1;
}

bool has_flag(const std::vector<std::string> &args, const std::string &flag) {
  for (const auto &a : args) {
    if (a == flag)
      return true;
  }
  return false;
}

// Platforms that require running the flux CLI on that same OS.
[[maybe_unused]] bool is_desktop_platform(const std::string &p) {
  return p == "windows" || p == "linux" || p == "macos";
}

bool is_valid_platform(const std::string &p) {
  return p == "windows" || p == "linux" || p == "macos" || p == "web" ||
         p == "android";
}

int stub_not_implemented(const std::string &platform) {
  std::fprintf(stderr, "flux: '%s' support is not implemented yet.\n",
               platform.c_str());
  return 1;
}


int dispatch_release(const std::string &platform) {
  if (platform == "windows") {
#if defined(_WIN32)
    return windows_release();
#else
    std::fprintf(stderr,
                 "flux: 'windows' release requires running flux from a Windows host.\n");
    return 1;
#endif
  }

  std::fprintf(stderr,
               "flux: 'release' is not implemented for '%s' yet.\n",
               platform.c_str());
  return 1;
}


int dispatch(const std::string &command, const std::string &platform,
             bool release) {
  if (platform == "windows") {
#if defined(_WIN32)
    return (command == "run") ? windows_run(false) : windows_build(false);
#else
    std::fprintf(
        stderr,
        "flux: 'windows' target requires running flux from a Windows host.\n");
    return 1;
#endif
  }

  if (platform == "macos") {
#if defined(__APPLE__)
    if (release) {
      std::printf("note: --release is not implemented yet, building Debug "
                  "instead.\n\n");
    }
    return (command == "run") ? macos_run(false) : macos_build(false);
#else
    std::fprintf(
        stderr,
        "flux: 'macos' target requires running flux from a macOS host.\n");
    return 1;
#endif
  }

  if (platform == "linux") {
#if defined(__linux__)
    if (release) {
      std::printf("note: --release is not implemented yet, building Debug "
                  "instead.\n\n");
    }
    return (command == "run") ? linux_run(false) : linux_build(false);
#else
    std::fprintf(
        stderr,
        "flux: 'linux' target requires running flux from a Linux host.\n");
    return 1;
#endif
  }

  if (platform == "android") {
    // Reachable from any host, but not yet implemented.
    if (release) {
      std::printf("note: --release is not implemented yet, building Debug "
                  "instead.\n\n");
    }
    return (command == "run") ? android_run(false) : android_build(false);
  }

  if (platform == "web") {
    // Reachable from any host, but not yet implemented.
    return stub_not_implemented(platform);
  }

  std::fprintf(stderr, "flux: unknown platform '%s'\n\n", platform.c_str());
  print_usage();
  return 1;
}

} // namespace

int main(int argc, char **argv) {
  if (argc < 2) {
    print_usage();
    return 1;
  }

  const std::string command = argv[1];

  if (command == "help") {
    print_usage();
    return 0;
  }

  if (command == "doctor") {
    std::string platform = (argc >= 3) ? argv[2] : host_platform_name();
    if (platform.empty()) {
      std::fprintf(
          stderr,
          "flux: could not determine host platform; pass one explicitly.\n\n");
      print_usage();
      return 1;
    }
    if (!is_valid_platform(platform)) {
      std::fprintf(stderr, "flux: unknown platform '%s'\n\n", platform.c_str());
      print_usage();
      return 1;
    }
    return dispatch_doctor(platform);
  }

  if (command == "add" || command == "remove") {
    if (argc < 3) {
      std::fprintf(stderr, "flux: missing package name.\n\n");
      print_usage();
      return 1;
    }
    const std::string package = argv[2];

    if (command == "remove") {
      return cmd_remove(package);
    }

    std::string ref_override;
    for (int i = 3; i < argc; ++i) {
      std::string a = argv[i];
      if (a == "--ref" && i + 1 < argc) {
        ref_override = argv[++i];
      }
    }
    return cmd_add(package, ref_override);
  }

  if (command == "install") {
    return cmd_install();
  }

  if (command == "create") {
    if (argc < 3) {
      std::fprintf(stderr, "flux: missing project name.\n\n");
      print_usage();
      return 1;
    }
    const std::string project_name = argv[2];

    return cmd_create(project_name);
  }

  if (command == "release") {
    if (argc < 3) {
      std::fprintf(stderr, "flux: missing platform argument.\n\n");
      print_usage();
      return 1;
    }
    std::string platform = argv[2];
    if (!is_valid_platform(platform)) {
      std::fprintf(stderr, "flux: unknown platform '%s'\n\n", platform.c_str());
      print_usage();
      return 1;
    }
    return dispatch_release(platform);
  }


  if (command != "run" && command != "build") {
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
  for (int i = 3; i < argc; ++i)
    flags.emplace_back(argv[i]);
  const bool release = has_flag(flags, "--release");

  return dispatch(command, platform, release);
}