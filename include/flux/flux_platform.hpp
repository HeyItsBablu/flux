// flux/flux_platform.hpp
#pragma once

#include <cstdint>
#include <string>

// ============================================================================
// CROSS-PLATFORM COLOR
// ============================================================================

struct Color {
    uint8_t r, g, b, a;

    constexpr Color() : r(0), g(0), b(0), a(255) {}
    constexpr Color(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255)
        : r(r), g(g), b(b), a(a) {}

    static constexpr Color fromRGB(uint8_t r, uint8_t g, uint8_t b) {
        return Color(r, g, b, 255);
    }
    static constexpr Color fromRGBA(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
        return Color(r, g, b, a);
    }
    // 0xRRGGBBAA
    static constexpr Color fromHex(uint32_t hex) {
        return Color(
            (hex >> 24) & 0xFF,
            (hex >> 16) & 0xFF,
            (hex >>  8) & 0xFF,
             hex        & 0xFF
        );
    }

static constexpr Color fromHex24(uint32_t hex) {  // 0xRRGGBB, alpha=255
    return Color(
        (hex >> 16) & 0xFF,
        (hex >>  8) & 0xFF,
         hex        & 0xFF,
        255
    );
}

    Color withAlpha(uint8_t newAlpha) const { return Color(r, g, b, newAlpha); }

    Color darken(int amount) const {
        auto clamp = [](int v) -> uint8_t {
            return v < 0 ? 0 : v > 255 ? 255 : (uint8_t)v;
        };
        return Color(clamp(r - amount), clamp(g - amount), clamp(b - amount), a);
    }

    Color lighten(int amount) const {
        auto clamp = [](int v) -> uint8_t {
            return v < 0 ? 0 : v > 255 ? 255 : (uint8_t)v;
        };
        return Color(clamp(r + amount), clamp(g + amount), clamp(b + amount), a);
    }

    Color interpolate(const Color &other, double t) const {
        if (t <= 0.0) return *this;
        if (t >= 1.0) return other;
        return Color(
            (uint8_t)(r + (other.r - r) * t),
            (uint8_t)(g + (other.g - g) * t),
            (uint8_t)(b + (other.b - b) * t),
            (uint8_t)(a + (other.a - a) * t)
        );
    }

    bool operator==(const Color &o) const {
        return r == o.r && g == o.g && b == o.b && a == o.a;
    }
    bool operator!=(const Color &o) const { return !(*this == o); }
};

// ── NativeColor — cross-platform alias ──────────────────────────────────────
// Widgets and API code use Color exclusively.
// NativeColor is only used inside Painter implementations.

using NativeColor = Color;

// ============================================================================
// WIN32 PLATFORM
// ============================================================================

#ifdef _WIN32

#include <windows.h>
#include <windowsx.h>
#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")

using NativeWindow   = HWND;
using NativeContext  = HDC;
using NativeFont     = HFONT;
using AppInstance    = HINSTANCE;
using TimerID        = UINT;

// ── WGL extension typedefs ───────────────────────────────────────────────────

using PFNWGLCREATECONTEXTATTRIBSARBPROC = HGLRC(WINAPI *)(HDC, HGLRC, const int *);
using PFNWGLCHOOSEPIXELFORMATARBPROC    = BOOL(WINAPI *)(HDC, const int *,
                                                         const FLOAT *, UINT,
                                                         int *, UINT *);

// ── Win32 color conversion — only called inside Painter / platform code ──────

inline COLORREF toColorRef(Color c)  { return RGB(c.r, c.g, c.b); }
inline BYTE     toAlphaByte(Color c) { return c.a; }

inline Gdiplus::Color toGdipColor(Color c) {
    return Gdiplus::Color(c.a, c.r, c.g, c.b);
}

// ── GraphicsContext ──────────────────────────────────────────────────────────

struct GraphicsContext {
    NativeContext hdc;
    explicit GraphicsContext(NativeContext h) : hdc(h) {}
};

// ── BackBuffer ───────────────────────────────────────────────────────────────

struct BackBuffer {
    HDC     hdc    = nullptr;
    HBITMAP hbm    = nullptr;
    HBITMAP hbmOld = nullptr;
    int     width  = 0;
    int     height = 0;

    bool valid() const { return hdc != nullptr; }

    void create(HWND hwnd, int w, int h) {
        if (hdc && (w != width || h != height))
            destroy();
        if (!hdc) {
            HDC screen = GetDC(hwnd);
            hdc    = CreateCompatibleDC(screen);
            hbm    = CreateCompatibleBitmap(screen, w, h);
            hbmOld = (HBITMAP)SelectObject(hdc, hbm);
            ReleaseDC(hwnd, screen);
            width  = w;
            height = h;
        }
    }

    void destroy() {
        if (hdc) {
            SelectObject(hdc, hbmOld);
            DeleteObject(hbm);
            DeleteDC(hdc);
            hdc    = nullptr;
            hbm    = nullptr;
            hbmOld = nullptr;
        }
    }

    GraphicsContext context() { return GraphicsContext(hdc); }
};

// ── MeasureContext ───────────────────────────────────────────────────────────

struct MeasureContext {
    GraphicsContext ctx;
    HWND            hwnd;

    explicit MeasureContext(HWND h) : hwnd(h), ctx(GetDC(h)) {}
    ~MeasureContext() { ReleaseDC(hwnd, ctx.hdc); }

    MeasureContext(const MeasureContext &)            = delete;
    MeasureContext &operator=(const MeasureContext &) = delete;
};

// ── Platform tick / input ────────────────────────────────────────────────────

inline uint32_t platformTickCount() {
    return static_cast<uint32_t>(GetTickCount64());
}

inline bool platformKeyDown(int keyCode) {
    return (GetKeyState(keyCode) & 0x8000) != 0;
}

// ── String helpers ───────────────────────────────────────────────────────────

inline std::wstring toWideString(const std::string &utf8) {
    if (utf8.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    std::wstring result(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, result.data(), len);
    return result;
}

inline std::wstring toWideString(const char *data, int byteCount) {
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

inline uint32_t platformTickCount() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint32_t>(ts.tv_sec * 1000u + ts.tv_nsec / 1'000'000u);
}

inline std::wstring toWideString(const std::string &utf8) {
    // stub — replace with real iconv / platform conversion
    return std::wstring(utf8.begin(), utf8.end());
}

#endif // !_WIN32

// ============================================================================
// CROSS-PLATFORM CONSTANTS
// ============================================================================

#ifndef WHEEL_DELTA
static constexpr int WHEEL_DELTA = 120;
#endif