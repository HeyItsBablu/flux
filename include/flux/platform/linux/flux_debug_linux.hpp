// flux/platform/linux/flux_debug_linux.hpp
#pragma once
#ifdef __linux__
#include <cstdio>
#include <ctime>

inline void FluxDebugLog(const char* msg) {
    // Timestamp prefix — matches what tools like journald / logcat show
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    fprintf(stderr, "[flux %4ld.%03ld] %s\n",
            ts.tv_sec,
            ts.tv_nsec / 1'000'000,
            msg);
    fflush(stderr);   // flush immediately so output survives crashes
}
#endif