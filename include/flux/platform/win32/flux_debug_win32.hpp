// flux/platform/win32/flux_debug_win32.hpp
#pragma once
#ifdef _WIN32
  #include <windows.h>
  inline void FluxDebugLog(const char *msg) { OutputDebugStringA(msg); }
#else
  #include <cstdio>
  inline void FluxDebugLog(const char *msg) { fprintf(stderr, "%s", msg); }
#endif