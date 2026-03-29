// flux/flux_platform.hpp
#pragma once

#ifdef _WIN32


#include <windows.h>
#include <windowsx.h>
#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")

using NativeWindow = HWND;
using NativeContext = HDC;
using NativeFont = HFONT;
using NativeColor = COLORREF;
using AppInstance = HINSTANCE;
using TimerID = UINT;

// ============================================================================
// §1  WGL EXTENSION TYPEDEFS  (only WGL ones — GL functions come from GLAD)
// ============================================================================

using PFNWGLCREATECONTEXTATTRIBSARBPROC = HGLRC(WINAPI *)(HDC, HGLRC,
                                                          const int *);
using PFNWGLCHOOSEPIXELFORMATARBPROC = BOOL(WINAPI *)(HDC, const int *,
                                                      const FLOAT *, UINT,
                                                      int *, UINT *);

struct GraphicsContext {
  NativeContext hdc;
  explicit GraphicsContext(NativeContext h) : hdc(h) {}
};

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

struct MeasureContext {
  GraphicsContext ctx;
  HWND hwnd;

  explicit MeasureContext(HWND h) : hwnd(h), ctx(GetDC(h)) {}
  ~MeasureContext() { ReleaseDC(hwnd, ctx.hdc); }

  MeasureContext(const MeasureContext &) = delete;
  MeasureContext &operator=(const MeasureContext &) = delete;
};

inline NativeColor darkenColor(NativeColor c, int amount) {
  return RGB(max(0, GetRValue(c) - amount), max(0, GetGValue(c) - amount),
             max(0, GetBValue(c) - amount));
}

inline NativeColor colorFromRGB(int r, int g, int b) { return RGB(r, g, b); }

inline int colorRed(NativeColor c) { return GetRValue(c); }
inline int colorGreen(NativeColor c) { return GetGValue(c); }
inline int colorBlue(NativeColor c) { return GetBValue(c); }

static COLORREF interpolateColor(COLORREF c1, COLORREF c2, double t) {
    t = max(0.0, min(1.0, t));
    int r = colorRed  (c1) + (int)((colorRed  (c2) - colorRed  (c1)) * t);
    int g = colorGreen(c1) + (int)((colorGreen(c2) - colorGreen(c1)) * t);
    int b = colorBlue (c1) + (int)((colorBlue (c2) - colorBlue (c1)) * t);
    return colorFromRGB(r, g, b);
}

#endif // _WIN32

// // ── Cross-platform constants ──────────────────────────────────────────────
#ifndef WHEEL_DELTA
  static constexpr int WHEEL_DELTA = 120;
#endif