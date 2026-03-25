// flux/flux_platform.hpp
#pragma once

#ifdef _WIN32
  #include <windows.h>
  #include <windowsx.h>
  #include <gdiplus.h>
  #pragma comment(lib, "gdiplus.lib")

  using NativeWindow  = HWND;
  using NativeContext = HDC;
  using NativeFont    = HFONT;
  using NativeColor   = COLORREF;
  using AppInstance   = HINSTANCE;
#endif