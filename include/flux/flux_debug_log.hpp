// flux_debug_log.hpp
#pragma once
#include <cstdio>
#include <string>

inline FILE*& getDebugLogFile()
{
    static FILE* f = nullptr;
    return f;
}

inline void fluxLog(const std::string& msg)
{
    FILE* f = getDebugLogFile();
    if (f) {
        fprintf(f, "%s\n", msg.c_str());
        fflush(f);
    }
}