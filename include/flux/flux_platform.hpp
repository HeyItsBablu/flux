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
    // 0xRRGGBB, alpha = 255
    static constexpr Color fromHex24(uint32_t hex) {
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
            return v < 0 ? 0 : v > 255 ? 255 : static_cast<uint8_t>(v);
        };
        return Color(clamp(r - amount), clamp(g - amount), clamp(b - amount), a);
    }

    Color lighten(int amount) const {
        auto clamp = [](int v) -> uint8_t {
            return v < 0 ? 0 : v > 255 ? 255 : static_cast<uint8_t>(v);
        };
        return Color(clamp(r + amount), clamp(g + amount), clamp(b + amount), a);
    }

    Color interpolate(const Color& other, double t) const {
        if (t <= 0.0) return *this;
        if (t >= 1.0) return other;
        return Color(
            static_cast<uint8_t>(r + (other.r - r) * t),
            static_cast<uint8_t>(g + (other.g - g) * t),
            static_cast<uint8_t>(b + (other.b - b) * t),
            static_cast<uint8_t>(a + (other.a - a) * t)
        );
    }

    bool operator==(const Color& o) const {
        return r == o.r && g == o.g && b == o.b && a == o.a;
    }
    bool operator!=(const Color& o) const { return !(*this == o); }
};

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

// ── Type aliases ──────────────────────────────────────────────────────────────

using NativeWindow  = HWND;
using NativeContext = HDC;
using NativeFont    = HFONT;
using AppInstance   = HINSTANCE;
using TimerID       = UINT;

// ── WGL extension typedefs ────────────────────────────────────────────────────

using PFNWGLCREATECONTEXTATTRIBSARBPROC =
    HGLRC(WINAPI*)(HDC, HGLRC, const int*);
using PFNWGLCHOOSEPIXELFORMATARBPROC =
    BOOL(WINAPI*)(HDC, const int*, const FLOAT*, UINT, int*, UINT*);

// ── Color conversion helpers — only used inside Painter / platform code ───────

inline COLORREF       toColorRef (Color c) { return RGB(c.r, c.g, c.b); }
inline BYTE           toAlphaByte(Color c) { return c.a; }
inline Gdiplus::Color toGdipColor(Color c) {
    return Gdiplus::Color(c.a, c.r, c.g, c.b);
}

// ── BackBuffer ────────────────────────────────────────────────────────────────
// GraphicsContext is defined in flux_graphics_context.hpp (included at the
// bottom of this file). BackBuffer::context() is defined there too, after
// GraphicsContext is complete.

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

    // Defined after GraphicsContext is complete — see bottom of this file.
    inline struct GraphicsContext context();
};

// ── Tick / input helpers ──────────────────────────────────────────────────────

inline uint32_t platformTickCount() {
    return static_cast<uint32_t>(GetTickCount64());
}

inline bool platformKeyDown(int keyCode) {
    return (GetKeyState(keyCode) & 0x8000) != 0;
}

inline bool platformCtrlDown()  { return platformKeyDown(VK_CONTROL); }
inline bool platformShiftDown() { return platformKeyDown(VK_SHIFT);   }
inline bool platformAltDown()   { return platformKeyDown(VK_MENU);    }

// ── String helpers ────────────────────────────────────────────────────────────

inline std::wstring toWideString(const std::string& utf8) {
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

// DT_* text format flags are already defined by <windows.h> — no redefinition.

#endif // _WIN32

// ============================================================================
// LINUX PLATFORM
// ============================================================================

#ifdef __linux__

#include <ctime>
#include <SDL2/SDL.h>
#include <cairo/cairo.h>
#include <pango/pangocairo.h>

// ── Type aliases ──────────────────────────────────────────────────────────────

using NativeWindow  = SDL_Window*;
using NativeFont    = void*;        // PangoFontDescription* — cast in font/painter
using AppInstance   = void*;        // unused on Linux; kept for API symmetry
using TimerID       = uint32_t;
using UINT          = unsigned int; // mirrors Win32; used by DT_* flags and TimerID

// ── Tick helper ───────────────────────────────────────────────────────────────

inline uint32_t platformTickCount() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint32_t>(ts.tv_sec * 1000u + ts.tv_nsec / 1'000'000u);
}

// ── Key state ─────────────────────────────────────────────────────────────────

inline bool platformKeyDown(int sdlScancode) {
    const uint8_t* state = SDL_GetKeyboardState(nullptr);
    return state != nullptr && state[sdlScancode];
}

inline bool platformCtrlDown() {
    return (SDL_GetModState() & (KMOD_LCTRL  | KMOD_RCTRL))  != 0;
}
inline bool platformShiftDown() {
    return (SDL_GetModState() & (KMOD_LSHIFT | KMOD_RSHIFT)) != 0;
}
inline bool platformAltDown() {
    return (SDL_GetModState() & (KMOD_LALT   | KMOD_RALT))   != 0;
}

// ── Key code aliases — VK_* names → SDL_SCANCODE_* values ────────────────────
// Only the subset FluxUI widgets actually test for.

static constexpr int VK_BACK     = SDL_SCANCODE_BACKSPACE;
static constexpr int VK_DELETE   = SDL_SCANCODE_DELETE;
static constexpr int VK_LEFT     = SDL_SCANCODE_LEFT;
static constexpr int VK_RIGHT    = SDL_SCANCODE_RIGHT;
static constexpr int VK_UP       = SDL_SCANCODE_UP;
static constexpr int VK_DOWN     = SDL_SCANCODE_DOWN;
static constexpr int VK_HOME     = SDL_SCANCODE_HOME;
static constexpr int VK_END      = SDL_SCANCODE_END;
static constexpr int VK_RETURN   = SDL_SCANCODE_RETURN;
static constexpr int VK_ESCAPE   = SDL_SCANCODE_ESCAPE;
static constexpr int VK_TAB      = SDL_SCANCODE_TAB;
static constexpr int VK_PRIOR    = SDL_SCANCODE_PAGEUP;    // Page Up
static constexpr int VK_NEXT     = SDL_SCANCODE_PAGEDOWN;  // Page Down
static constexpr int VK_CONTROL  = SDL_SCANCODE_LCTRL;
static constexpr int VK_SHIFT    = SDL_SCANCODE_LSHIFT;
static constexpr int VK_MENU     = SDL_SCANCODE_LALT;      // Alt

// ── Text format flags — mirrors Win32 DT_* (identical numeric values) ────────

// ── Text format flags — mirrors Win32 DT_* (identical numeric values) ────────
static constexpr UINT DT_LEFT         = 0x0000u;
static constexpr UINT DT_CENTER       = 0x0001u;
static constexpr UINT DT_RIGHT        = 0x0002u;
static constexpr UINT DT_VCENTER      = 0x0004u;
static constexpr UINT DT_TOP          = 0x0000u;  
static constexpr UINT DT_WORDBREAK    = 0x0010u;
static constexpr UINT DT_SINGLELINE   = 0x0020u;
static constexpr UINT DT_NOCLIP       = 0x0100u;  
static constexpr UINT DT_END_ELLIPSIS = 0x8000u;  

// ── String helpers ────────────────────────────────────────────────────────────
// toWideString is provided for call-site compatibility with widgets that build
// wstrings. The BMP-only path is sufficient for UI label text.

inline std::wstring toWideString(const std::string& utf8) {
    std::wstring out;
    out.reserve(utf8.size());
    for (unsigned char c : utf8)
        out += static_cast<wchar_t>(c);
    return out;
}

// Two-argument overload: converts first `byteCount` bytes of `data`
inline std::wstring toWideString(const char* data, int byteCount) {
    if (!data || byteCount <= 0) return {};
    std::wstring out;
    out.reserve(byteCount);
    for (int i = 0; i < byteCount; ++i)
        out += static_cast<wchar_t>(static_cast<unsigned char>(data[i]));
    return out;
}

#endif // __linux__

// ============================================================================
// CROSS-PLATFORM CONSTANTS
// ============================================================================

#ifndef WHEEL_DELTA
static constexpr int WHEEL_DELTA = 120;
#endif

// ============================================================================
// GraphicsContext + MeasureContext
//
// Pulled into their own header so flux_window.hpp and other consumers can
// include just what they need. We include it here so every translation unit
// that pulls in flux_platform.hpp automatically gets both types — no extra
// include required at call sites.
// ============================================================================

#include "flux_graphics_context.hpp"

// ── BackBuffer::context() — defined here, after GraphicsContext is complete ───
#ifdef _WIN32
inline GraphicsContext BackBuffer::context() {
    return GraphicsContext(hdc);
}
#endif