// flux/flux_platform.hpp
#pragma once

#include <cstdint>

// ============================================================================
// WIN32 PLATFORM
// ============================================================================

#ifdef _WIN32


#include <windows.h>
#include <windowsx.h>
#include <cstdint>
#include <string> 
#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")

using NativeWindow = HWND;
using NativeContext = HDC;
using NativeFont = HFONT;
using NativeColor = COLORREF;
using AppInstance = HINSTANCE;
using TimerID = UINT;

// ── WGL extension typedefs ───────────────────────────────────────────────────

using PFNWGLCREATECONTEXTATTRIBSARBPROC = HGLRC(WINAPI *)(HDC, HGLRC,
                                                          const int *);
using PFNWGLCHOOSEPIXELFORMATARBPROC = BOOL(WINAPI *)(HDC, const int *,
                                                      const FLOAT *, UINT,
                                                      int *, UINT *);

// ── GraphicsContext ──────────────────────────────────────────────────────────

struct GraphicsContext {
  NativeContext hdc;
  explicit GraphicsContext(NativeContext h) : hdc(h) {}
};

// ── BackBuffer ───────────────────────────────────────────────────────────────

struct BackBuffer {
  HDC hdc = nullptr;
  HBITMAP hbm = nullptr;
  HBITMAP hbmOld = nullptr;
  int width = 0;
  int height = 0;

  bool valid() const { return hdc != nullptr; }

  void create(HWND hwnd, int w, int h) {
    if (hdc && (w != width || h != height))
      destroy();
    if (!hdc) {
      HDC screen = GetDC(hwnd);
      hdc = CreateCompatibleDC(screen);
      hbm = CreateCompatibleBitmap(screen, w, h);
      hbmOld = (HBITMAP)SelectObject(hdc, hbm);
      ReleaseDC(hwnd, screen);
      width = w;
      height = h;
    }
  }

  void destroy() {
    if (hdc) {
      SelectObject(hdc, hbmOld);
      DeleteObject(hbm);
      DeleteDC(hdc);
      hdc = nullptr;
      hbm = nullptr;
      hbmOld = nullptr;
    }
  }

  GraphicsContext context() { return GraphicsContext(hdc); }
};

// ── MeasureContext ───────────────────────────────────────────────────────────

struct MeasureContext {
  GraphicsContext ctx;
  HWND hwnd;

  explicit MeasureContext(HWND h) : hwnd(h), ctx(GetDC(h)) {}
  ~MeasureContext() { ReleaseDC(hwnd, ctx.hdc); }

  MeasureContext(const MeasureContext &) = delete;
  MeasureContext &operator=(const MeasureContext &) = delete;
};

// ── Color helpers ────────────────────────────────────────────────────────────

inline NativeColor colorFromRGB(int r, int g, int b) { return RGB(r, g, b); }
inline int colorRed(NativeColor c) { return GetRValue(c); }
inline int colorGreen(NativeColor c) { return GetGValue(c); }
inline int colorBlue(NativeColor c) { return GetBValue(c); }

inline NativeColor darkenColor(NativeColor c, int amount) {
  return RGB(max(0, GetRValue(c) - amount), max(0, GetGValue(c) - amount),
             max(0, GetBValue(c) - amount));
}

inline NativeColor interpolateColor(NativeColor c1, NativeColor c2, double t) {
  t = max(0.0, min(1.0, t));
  int r = colorRed(c1) + (int)((colorRed(c2) - colorRed(c1)) * t);
  int g = colorGreen(c1) + (int)((colorGreen(c2) - colorGreen(c1)) * t);
  int b = colorBlue(c1) + (int)((colorBlue(c2) - colorBlue(c1)) * t);
  return colorFromRGB(r, g, b);
}

// ── platformTickCount — Win32 ────────────────────────────────────────────────

inline uint32_t platformTickCount() {
  return static_cast<uint32_t>(GetTickCount64());
}

inline bool platformKeyDown(int keyCode) {

    return (GetKeyState(keyCode) & 0x8000) != 0;

}




inline std::wstring toWideString(const std::string &utf8) {
    if (utf8.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    std::wstring result(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, result.data(), len);
    return result;
}

inline std::wstring toWideString(const char* data, int byteCount) {
    if (!data || byteCount <= 0) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, data, byteCount, nullptr, 0);
    std::wstring result(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, data, byteCount, result.data(), len);
    return result;
}


#endif // _WIN32

// ============================================================================
// POSIX / OTHER PLATFORMS
// ============================================================================

#ifndef _WIN32

#include <ctime>

// Forward-declare the cross-platform tick function for non-Win32 builds.
// Concrete type aliases (NativeWindow, NativeColor, etc.) will be added
// here when other platform backends are implemented.

inline uint32_t platformTickCount() {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return static_cast<uint32_t>(ts.tv_sec * 1000u + ts.tv_nsec / 1'000'000u);
}

inline std::wstring toWideString(const std::string &utf8) {
    // stub — replace with real UTF-8 → wchar_t conversion per platform
    return std::wstring(utf8.begin(), utf8.end());
}

#endif // !_WIN32

// ============================================================================
// CROSS-PLATFORM CONSTANTS
// ============================================================================

#ifndef WHEEL_DELTA
static constexpr int WHEEL_DELTA = 120;
#endif