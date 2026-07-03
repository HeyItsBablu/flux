#include <cstdio>
#include <string>

#if defined(_WIN32)
int cmd_build_windows();
#endif

static void print_usage() {
    std::printf("flux - FluxUI project CLI\n\n");
    std::printf("Usage:\n");
    std::printf("  flux build      Build the project (Debug)\n");
}

int main(int argc, char** argv) {
    if (argc < 2) {
        print_usage();
        return 1;
    }

    const std::string command = argv[1];

    if (command == "build") {
#if defined(_WIN32)
        return cmd_build_windows();
#else
        std::fprintf(stderr, "flux build: only Windows is supported right now.\n");
        return 1;
#endif
    }

    std::fprintf(stderr, "flux: unknown command '%s'\n\n", command.c_str());
    print_usage();
    return 1;
}