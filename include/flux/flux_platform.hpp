// flux/flux_platform.hpp
#pragma once

#include <cstdint>
#include <string>
#include <cmath>

// ============================================================================
// CROSS-PLATFORM COLOR
// ============================================================================

struct Color
{
    uint8_t r, g, b, a;

    constexpr Color() : r(0), g(0), b(0), a(255) {}
    constexpr Color(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255)
        : r(r), g(g), b(b), a(a) {}

    static constexpr Color fromRGB(uint8_t r, uint8_t g, uint8_t b)
    {
        return Color(r, g, b, 255);
    }
    static constexpr Color fromRGBA(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
    {
        return Color(r, g, b, a);
    }
    // 0xRRGGBBAA
    static constexpr Color fromHex(uint32_t hex)
    {
        return Color(
            (hex >> 24) & 0xFF,
            (hex >> 16) & 0xFF,
            (hex >> 8) & 0xFF,
            hex & 0xFF);
    }
    // 0xRRGGBB, alpha = 255
    static constexpr Color fromHex24(uint32_t hex)
    {
        return Color(
            (hex >> 16) & 0xFF,
            (hex >> 8) & 0xFF,
            hex & 0xFF,
            255);
    }

    static Color fromHSV(float h, float s, float v, float a = 1.f)
    {
        h = std::fmod(h, 360.f);
        if (h < 0.f)
            h += 360.f;
        float c = v * s;
        float x = c * (1.f - std::abs(std::fmod(h / 60.f, 2.f) - 1.f));
        float m = v - c;
        float r = 0, g = 0, b = 0;
        if (h < 60)
        {
            r = c;
            g = x;
            b = 0;
        }
        else if (h < 120)
        {
            r = x;
            g = c;
            b = 0;
        }
        else if (h < 180)
        {
            r = 0;
            g = c;
            b = x;
        }
        else if (h < 240)
        {
            r = 0;
            g = x;
            b = c;
        }
        else if (h < 300)
        {
            r = x;
            g = 0;
            b = c;
        }
        else
        {
            r = c;
            g = 0;
            b = x;
        }
        return Color(
            uint8_t((r + m) * 255.f),
            uint8_t((g + m) * 255.f),
            uint8_t((b + m) * 255.f),
            uint8_t(a * 255.f));
    }

    Color withAlpha(uint8_t newAlpha) const { return Color(r, g, b, newAlpha); }

    Color darken(int amount) const
    {
        auto clamp = [](int v) -> uint8_t
        {
            return v < 0 ? 0 : v > 255 ? 255
                                       : static_cast<uint8_t>(v);
        };
        return Color(clamp(r - amount), clamp(g - amount), clamp(b - amount), a);
    }

    Color lighten(int amount) const
    {
        auto clamp = [](int v) -> uint8_t
        {
            return v < 0 ? 0 : v > 255 ? 255
                                       : static_cast<uint8_t>(v);
        };
        return Color(clamp(r + amount), clamp(g + amount), clamp(b + amount), a);
    }

    Color interpolate(const Color &other, double t) const
    {
        if (t <= 0.0)
            return *this;
        if (t >= 1.0)
            return other;
        return Color(
            static_cast<uint8_t>(r + (other.r - r) * t),
            static_cast<uint8_t>(g + (other.g - g) * t),
            static_cast<uint8_t>(b + (other.b - b) * t),
            static_cast<uint8_t>(a + (other.a - a) * t));
    }

    bool operator==(const Color &o) const
    {
        return r == o.r && g == o.g && b == o.b && a == o.a;
    }
    bool operator!=(const Color &o) const { return !(*this == o); }
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
#include <objidl.h>
#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")

// ── Type aliases ──────────────────────────────────────────────────────────────

using NativeWindow = HWND;
using NativeContext = HDC;
using NativeFont = HFONT;
using AppInstance = HINSTANCE;
using TimerID = UINT;

// ── WGL extension typedefs ────────────────────────────────────────────────────

using PFNWGLCREATECONTEXTATTRIBSARBPROC =
    HGLRC(WINAPI *)(HDC, HGLRC, const int *);
using PFNWGLCHOOSEPIXELFORMATARBPROC =
    BOOL(WINAPI *)(HDC, const int *, const FLOAT *, UINT, int *, UINT *);

// ── Color conversion helpers — only used inside Painter / platform code ───────

inline COLORREF toColorRef(Color c) { return RGB(c.r, c.g, c.b); }
inline BYTE toAlphaByte(Color c) { return c.a; }
inline Gdiplus::Color toGdipColor(Color c)
{
    return Gdiplus::Color(c.a, c.r, c.g, c.b);
}

// ── BackBuffer ────────────────────────────────────────────────────────────────

struct BackBuffer
{
    HDC hdc = nullptr;
    HBITMAP hbm = nullptr;
    HBITMAP hbmOld = nullptr;
    int width = 0;
    int height = 0;

    bool valid() const { return hdc != nullptr; }

    void create(HWND hwnd, int w, int h)
    {
        if (hdc && (w != width || h != height))
            destroy();
        if (!hdc)
        {
            HDC screen = GetDC(hwnd);
            hdc = CreateCompatibleDC(screen);
            hbm = CreateCompatibleBitmap(screen, w, h);
            hbmOld = (HBITMAP)SelectObject(hdc, hbm);
            ReleaseDC(hwnd, screen);
            width = w;
            height = h;
        }
    }

    void destroy()
    {
        if (hdc)
        {
            SelectObject(hdc, hbmOld);
            DeleteObject(hbm);
            DeleteDC(hdc);
            hdc = nullptr;
            hbm = nullptr;
            hbmOld = nullptr;
        }
    }

    // Defined after GraphicsContext is complete — see bottom of this file.
    inline struct GraphicsContext context();
};

// ── Tick / input helpers ──────────────────────────────────────────────────────

inline uint32_t platformTickCount()
{
    return static_cast<uint32_t>(GetTickCount64());
}

inline bool platformKeyDown(int keyCode)
{
    return (GetKeyState(keyCode) & 0x8000) != 0;
}

inline bool platformCtrlDown() { return platformKeyDown(VK_CONTROL); }
inline bool platformShiftDown() { return platformKeyDown(VK_SHIFT); }
inline bool platformAltDown() { return platformKeyDown(VK_MENU); }

// ── String helpers ────────────────────────────────────────────────────────────

inline std::wstring toWideString(const std::string &utf8)
{
    if (utf8.empty())
        return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    std::wstring result(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, result.data(), len);
    return result;
}

inline std::wstring toWideString(const char *data, int byteCount)
{
    if (!data || byteCount <= 0)
        return {};
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
#if defined(__linux__) && !defined(__ANDROID__)

#include <ctime>
#include <SDL2/SDL.h>
#include <cairo/cairo.h>
#include <pango/pangocairo.h>

// ── Type aliases ──────────────────────────────────────────────────────────────

using NativeWindow = SDL_Window *;
using NativeFont = void *;  // PangoFontDescription* — cast in font/painter
using AppInstance = void *; // unused on Linux; kept for API symmetry
using TimerID = uint32_t;
using UINT = unsigned int; // mirrors Win32; used by DT_* flags and TimerID

// ── Tick helper ───────────────────────────────────────────────────────────────

inline uint32_t platformTickCount()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint32_t>(ts.tv_sec * 1000u + ts.tv_nsec / 1'000'000u);
}

// ── Key state ─────────────────────────────────────────────────────────────────

inline bool platformKeyDown(int sdlScancode)
{
    const uint8_t *state = SDL_GetKeyboardState(nullptr);
    return state != nullptr && state[sdlScancode];
}

inline bool platformCtrlDown()
{
    return (SDL_GetModState() & (KMOD_LCTRL | KMOD_RCTRL)) != 0;
}
inline bool platformShiftDown()
{
    return (SDL_GetModState() & (KMOD_LSHIFT | KMOD_RSHIFT)) != 0;
}
inline bool platformAltDown()
{
    return (SDL_GetModState() & (KMOD_LALT | KMOD_RALT)) != 0;
}

// ── Key code aliases — VK_* names → SDL_SCANCODE_* values ────────────────────

static constexpr int VK_BACK = SDL_SCANCODE_BACKSPACE;
static constexpr int VK_DELETE = SDL_SCANCODE_DELETE;
static constexpr int VK_LEFT = SDL_SCANCODE_LEFT;
static constexpr int VK_RIGHT = SDL_SCANCODE_RIGHT;
static constexpr int VK_UP = SDL_SCANCODE_UP;
static constexpr int VK_DOWN = SDL_SCANCODE_DOWN;
static constexpr int VK_HOME = SDL_SCANCODE_HOME;
static constexpr int VK_END = SDL_SCANCODE_END;
static constexpr int VK_RETURN = SDL_SCANCODE_RETURN;
static constexpr int VK_ESCAPE = SDL_SCANCODE_ESCAPE;
static constexpr int VK_TAB = SDL_SCANCODE_TAB;
static constexpr int VK_PRIOR = SDL_SCANCODE_PAGEUP;
static constexpr int VK_NEXT = SDL_SCANCODE_PAGEDOWN;
static constexpr int VK_CONTROL = SDL_SCANCODE_LCTRL;
static constexpr int VK_SHIFT = SDL_SCANCODE_LSHIFT;
static constexpr int VK_MENU = SDL_SCANCODE_LALT;

// ── Text format flags — mirrors Win32 DT_* (identical numeric values) ────────

static constexpr UINT DT_LEFT = 0x0000u;
static constexpr UINT DT_CENTER = 0x0001u;
static constexpr UINT DT_RIGHT = 0x0002u;
static constexpr UINT DT_VCENTER = 0x0004u;
static constexpr UINT DT_TOP = 0x0000u;
static constexpr UINT DT_WORDBREAK = 0x0010u;
static constexpr UINT DT_SINGLELINE = 0x0020u;
static constexpr UINT DT_NOCLIP = 0x0100u;
static constexpr UINT DT_END_ELLIPSIS = 0x8000u;

// ── String helpers ────────────────────────────────────────────────────────────

inline std::wstring toWideString(const std::string &utf8)
{
    std::wstring out;
    out.reserve(utf8.size());
    for (unsigned char c : utf8)
        out += static_cast<wchar_t>(c);
    return out;
}

inline std::wstring toWideString(const char *data, int byteCount)
{
    if (!data || byteCount <= 0)
        return {};
    std::wstring out;
    out.reserve(byteCount);
    for (int i = 0; i < byteCount; ++i)
        out += static_cast<wchar_t>(static_cast<unsigned char>(data[i]));
    return out;
}

#endif // __linux__

// ============================================================================
// ANDROID PLATFORM
// ============================================================================

#ifdef __ANDROID__

#include <android_native_app_glue.h>
#include <android/log.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <ctime>

using NativeWindow = ANativeWindow *;
using NativeFont = void *;
using AppInstance = android_app *;
using TimerID = uint32_t;
using UINT = unsigned int;

static constexpr UINT DT_LEFT = 0x0000u;
static constexpr UINT DT_CENTER = 0x0001u;
static constexpr UINT DT_RIGHT = 0x0002u;
static constexpr UINT DT_VCENTER = 0x0004u;
static constexpr UINT DT_TOP = 0x0000u;
static constexpr UINT DT_WORDBREAK = 0x0010u;
static constexpr UINT DT_SINGLELINE = 0x0020u;
static constexpr UINT DT_NOCLIP = 0x0100u;
static constexpr UINT DT_END_ELLIPSIS = 0x8000u;

static constexpr int VK_BACK = 67;
static constexpr int VK_DELETE = 112;
static constexpr int VK_LEFT = 21;
static constexpr int VK_RIGHT = 22;
static constexpr int VK_UP = 19;
static constexpr int VK_DOWN = 20;
static constexpr int VK_RETURN = 66;
static constexpr int VK_ESCAPE = 111;
static constexpr int VK_TAB = 61;

inline uint32_t platformTickCount()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint32_t>(ts.tv_sec * 1000u + ts.tv_nsec / 1'000'000u);
}

inline bool platformCtrlDown() { return false; }
inline bool platformShiftDown() { return false; }
inline bool platformAltDown() { return false; }
inline bool platformKeyDown(int) { return false; }

inline std::wstring toWideString(const std::string &utf8)
{
    std::wstring out;
    out.reserve(utf8.size());
    for (unsigned char c : utf8)
        out += static_cast<wchar_t>(c);
    return out;
}
inline std::wstring toWideString(const char *data, int byteCount)
{
    if (!data || byteCount <= 0)
        return {};
    std::wstring out;
    out.reserve(byteCount);
    for (int i = 0; i < byteCount; ++i)
        out += static_cast<wchar_t>(static_cast<unsigned char>(data[i]));
    return out;
}

#endif // __ANDROID__

// ============================================================================
// APPLE PLATFORM (macOS)
// ============================================================================

#ifdef __APPLE__
#include <TargetConditionals.h>

#if TARGET_OS_OSX

#ifdef __OBJC__
#import <CoreGraphics/CoreGraphics.h>
#import <CoreText/CoreText.h>
#else
#include <CoreGraphics/CoreGraphics.h>
#include <CoreText/CoreText.h>
#endif
#include <ctime>

using NativeWindow = void *;
using NativeFont = void *;
using AppInstance = void *;
using TimerID = uint32_t;
using UINT = unsigned int;

static constexpr UINT DT_LEFT = 0x0000u;
static constexpr UINT DT_CENTER = 0x0001u;
static constexpr UINT DT_RIGHT = 0x0002u;
static constexpr UINT DT_VCENTER = 0x0004u;
static constexpr UINT DT_TOP = 0x0000u;
static constexpr UINT DT_WORDBREAK = 0x0010u;
static constexpr UINT DT_SINGLELINE = 0x0020u;
static constexpr UINT DT_NOCLIP = 0x0100u;
static constexpr UINT DT_END_ELLIPSIS = 0x8000u;

static constexpr int VK_BACK = 51;
static constexpr int VK_DELETE = 117;
static constexpr int VK_LEFT = 123;
static constexpr int VK_RIGHT = 124;
static constexpr int VK_UP = 126;
static constexpr int VK_DOWN = 125;
static constexpr int VK_HOME = 115;
static constexpr int VK_END = 119;
static constexpr int VK_RETURN = 36;
static constexpr int VK_ESCAPE = 53;
static constexpr int VK_TAB = 48;
static constexpr int VK_PRIOR = 116;
static constexpr int VK_NEXT = 121;
static constexpr int VK_CONTROL = 59;
static constexpr int VK_SHIFT = 56;
static constexpr int VK_MENU = 58;

inline uint32_t platformTickCount()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint32_t>(ts.tv_sec * 1000u + ts.tv_nsec / 1'000'000u);
}

inline bool platformKeyDown(int) { return false; }
inline bool platformCtrlDown() { return false; }
inline bool platformShiftDown() { return false; }
inline bool platformAltDown() { return false; }

inline std::wstring toWideString(const std::string &utf8)
{
    std::wstring out;
    out.reserve(utf8.size());
    for (unsigned char c : utf8)
        out += static_cast<wchar_t>(c);
    return out;
}

inline std::wstring toWideString(const char *data, int byteCount)
{
    if (!data || byteCount <= 0)
        return {};
    std::wstring out;
    out.reserve(byteCount);
    for (int i = 0; i < byteCount; ++i)
        out += static_cast<wchar_t>(static_cast<unsigned char>(data[i]));
    return out;
}

#endif // TARGET_OS_OSX
#endif // __APPLE__

// ============================================================================
// WEB PLATFORM (Emscripten)
// ============================================================================

#ifdef __EMSCRIPTEN__

#include <emscripten.h>
#include <emscripten/html5.h>
#include <ctime>

// ── Type aliases ──────────────────────────────────────────────────────────────
//
// NativeWindow  — canvas CSS selector string stored as void* (cast at use site
//                 in flux_window_web.cpp).  The two canvases are:
//                   "#flux-ui"  — Canvas 2D layer (painter / all widgets)
//                   "#flux-gl"  — WebGL2 layer    (CanvasWidget only)
//
// NativeFont    — heap-allocated CSS font string, e.g. "bold 14px Inter".
//                 Managed by FontCache in flux_font_web.cpp.
//
// AppInstance   — unused on web; kept for API symmetry with other platforms.
// TimerID       — Emscripten interval handle returned by emscripten_set_interval.

using NativeWindow = void *; // const char* canvas selector — cast at use site
using NativeFont = void *;   // CSS font string — cast in font/painter
using AppInstance = void *;  // unused on web
using TimerID = long;        // emscripten_set_interval handle
using UINT = unsigned int;   // mirrors Win32; used by DT_* flags

// ── Text format flags — same values as all other platforms ───────────────────

static constexpr UINT DT_LEFT = 0x0000u;
static constexpr UINT DT_CENTER = 0x0001u;
static constexpr UINT DT_RIGHT = 0x0002u;
static constexpr UINT DT_VCENTER = 0x0004u;
static constexpr UINT DT_TOP = 0x0000u;
static constexpr UINT DT_WORDBREAK = 0x0010u;
static constexpr UINT DT_SINGLELINE = 0x0020u;
static constexpr UINT DT_NOCLIP = 0x0100u;
static constexpr UINT DT_END_ELLIPSIS = 0x8000u;

// ── VK_* key code aliases — HTML KeyboardEvent.keyCode values ─────────────────
//
// These match the browser's legacy keyCode values, which Emscripten exposes
// through emscripten_html5.h as DOM_KEY_LOCATION_* + key event structs.
// The subset here covers everything FluxUI widgets test for.

static constexpr int VK_BACK = 8;     // Backspace
static constexpr int VK_TAB = 9;      // Tab
static constexpr int VK_RETURN = 13;  // Enter
static constexpr int VK_ESCAPE = 27;  // Escape
static constexpr int VK_PRIOR = 33;   // Page Up
static constexpr int VK_NEXT = 34;    // Page Down
static constexpr int VK_END = 35;     // End
static constexpr int VK_HOME = 36;    // Home
static constexpr int VK_LEFT = 37;    // Arrow Left
static constexpr int VK_UP = 38;      // Arrow Up
static constexpr int VK_RIGHT = 39;   // Arrow Right
static constexpr int VK_DOWN = 40;    // Arrow Down
static constexpr int VK_DELETE = 46;  // Delete
static constexpr int VK_SHIFT = 16;   // Shift
static constexpr int VK_CONTROL = 17; // Control
static constexpr int VK_MENU = 18;    // Alt
static constexpr int VK_SPACE = 32;   // Space

// ── Modifier state ────────────────────────────────────────────────────────────
//
// Emscripten does not expose a synchronous "is key held right now" query like
// Win32's GetKeyState() or SDL's SDL_GetKeyboardState().  Instead, modifier
// state is captured from the most-recent keyboard/mouse event and stored here
// by flux_window_web.cpp.  All four functions read from these globals so the
// rest of FluxUI can call them identically to every other platform.

namespace flux_web_detail
{
    inline bool g_ctrlDown = false;
    inline bool g_shiftDown = false;
    inline bool g_altDown = false;
}

inline bool platformCtrlDown() { return flux_web_detail::g_ctrlDown; }
inline bool platformShiftDown() { return flux_web_detail::g_shiftDown; }
inline bool platformAltDown() { return flux_web_detail::g_altDown; }

// platformKeyDown() — not meaningful on web (no synchronous scan-code query).
// Returns false so widgets that call it at non-event time get a safe answer.
// Event-driven key checks go through the keydown/keyup callbacks in
// flux_window_web.cpp instead.
inline bool platformKeyDown(int) { return false; }

// ── Tick helper ───────────────────────────────────────────────────────────────

inline uint32_t platformTickCount()
{
    // emscripten_get_now() returns milliseconds as a double.
    return static_cast<uint32_t>(emscripten_get_now());
}

// ── String helpers ────────────────────────────────────────────────────────────
//
// The web painter works in UTF-8 throughout (Canvas 2D fillText accepts it
// directly via EM_ASM).  toWideString() is provided for call-site compatibility
// with widget code that still builds std::wstrings — the BMP-only path used on
// Android/macOS/Linux is sufficient for UI label text on web too.

inline std::wstring toWideString(const std::string &utf8)
{
    std::wstring out;
    out.reserve(utf8.size());
    for (unsigned char c : utf8)
        out += static_cast<wchar_t>(c);
    return out;
}

inline std::wstring toWideString(const char *data, int byteCount)
{
    if (!data || byteCount <= 0)
        return {};
    std::wstring out;
    out.reserve(byteCount);
    for (int i = 0; i < byteCount; ++i)
        out += static_cast<wchar_t>(static_cast<unsigned char>(data[i]));
    return out;
}

#endif // __EMSCRIPTEN__

// ============================================================================
// CROSS-PLATFORM CONSTANTS
// ============================================================================

#ifndef WHEEL_DELTA
static constexpr int WHEEL_DELTA = 120;
#endif

// ============================================================================
// GraphicsContext
//
// Thin wrapper around the platform drawing surface passed to every Painter
// call and every layout/measure operation.
//
// Win32     : wraps HDC           — lifetime tied to BackBuffer HDC.
// Linux     : wraps cairo_t*      — borrowed from CairoState; never owned here.
// Android   : wraps width/height  — NanoVG is stateful; no context pointer needed.
// macOS     : wraps CGContextRef  — borrowed from the MTKView draw callback.
// Web       : wraps width/height  — Canvas 2D context lives in JS; C++ side
//             only needs dimensions for layout/clip maths.  Painter methods
//             reach the actual context via EM_ASM / Module._fluxCtx2D.
//
// Rule: GraphicsContext never owns or frees its underlying handle.
// ============================================================================

#ifdef _WIN32

struct GraphicsContext
{
    NativeContext hdc = nullptr;

    GraphicsContext() = default;
    explicit GraphicsContext(NativeContext h) : hdc(h) {}

    bool valid() const { return hdc != nullptr; }
};

#endif // _WIN32

#if defined(__linux__) && !defined(__ANDROID__)

struct GraphicsContext
{
    cairo_t *cr = nullptr;
    int width = 0;
    int height = 0;

    GraphicsContext() = default;
    explicit GraphicsContext(cairo_t *c, int w = 0, int h = 0)
        : cr(c), width(w), height(h) {}

    bool valid() const { return cr != nullptr; }
};

#endif // __linux__

#ifdef __ANDROID__

struct GraphicsContext
{
    int width = 0;
    int height = 0;

    GraphicsContext() = default;
    GraphicsContext(int w, int h) : width(w), height(h) {}

    bool valid() const { return width > 0 && height > 0; }
};

#endif // __ANDROID__

#ifdef __APPLE__
#include <TargetConditionals.h>
#if TARGET_OS_OSX

#include <CoreGraphics/CoreGraphics.h>

struct GraphicsContext
{
    CGContextRef cgContext = nullptr;
    int width = 0;
    int height = 0;

    GraphicsContext() = default;
    explicit GraphicsContext(CGContextRef cg, int w = 0, int h = 0)
        : cgContext(cg), width(w), height(h) {}

    bool valid() const { return cgContext != nullptr; }
};

#endif // TARGET_OS_OSX
#endif // __APPLE__

#ifdef __EMSCRIPTEN__

// ── Web GraphicsContext ───────────────────────────────────────────────────────
//
// The Canvas 2D rendering context (CanvasRenderingContext2D) lives entirely in
// JavaScript as Module._fluxCtx2D.  C++ never holds a pointer to it.
//
// GraphicsContext therefore carries only the physical pixel dimensions of the
// #flux-ui canvas, which the painter needs for:
//   • clip-rect calculations
//   • coordinate transforms (logical → physical when DPR != 1)
//   • bounds checks in layout code
//
// Both fields are in physical pixels (already multiplied by devicePixelRatio).
// Logical-pixel dimensions = width / dpr  (dpr stored in Module._fluxDPR).
//
// The painter accesses the JS context exclusively through EM_ASM blocks:
//   EM_ASM({ const ctx = Module._fluxCtx2D; ctx.fillRect(...); });

struct GraphicsContext
{
    int width = 0;  // physical pixels (CSS px * devicePixelRatio)
    int height = 0; // physical pixels

    GraphicsContext() = default;
    GraphicsContext(int w, int h) : width(w), height(h) {}

    bool valid() const { return width > 0 && height > 0; }
};

#endif // __EMSCRIPTEN__

// ============================================================================
// MeasureContext
//
// Wraps a GraphicsContext for text-measurement calls that happen outside of
// a paint pass.  Non-copyable; movable so PlatformWindow::getMeasureContext()
// can return by value.
// ============================================================================

struct MeasureContext
{
    GraphicsContext ctx;

    MeasureContext(const MeasureContext &) = delete;
    MeasureContext &operator=(const MeasureContext &) = delete;
    MeasureContext(MeasureContext &&) = default;
    MeasureContext &operator=(MeasureContext &&) = default;

#ifdef _WIN32

    HWND hwnd = nullptr;

    explicit MeasureContext(HWND h) : hwnd(h), ctx(GetDC(h)) {}

    ~MeasureContext()
    {
        if (hwnd && ctx.hdc)
            ReleaseDC(hwnd, ctx.hdc);
    }

#endif // _WIN32

#if defined(__linux__) && !defined(__ANDROID__)

    explicit MeasureContext(cairo_t *cr, int w = 0, int h = 0)
        : ctx(cr, w, h) {}

    ~MeasureContext() = default;

#endif // __linux__

#ifdef __ANDROID__

    explicit MeasureContext(int w, int h) : ctx(w, h) {}

    ~MeasureContext() = default;

#endif // __ANDROID__

#ifdef __APPLE__
#if TARGET_OS_OSX

    explicit MeasureContext(CGContextRef cg, int w = 0, int h = 0)
        : ctx(cg, w, h) {}

    ~MeasureContext() = default;

#endif // TARGET_OS_OSX
#endif // __APPLE__

#ifdef __EMSCRIPTEN__

    // ── Web MeasureContext ────────────────────────────────────────────────────
    //
    // Text measurement on web uses Canvas 2D's ctx.measureText(), called via
    // EM_ASM in flux_font_web.cpp.  No C++ resource needs to be acquired or
    // released — the JS context is always live while the page is running.
    //
    // Dimensions come from the current #flux-ui canvas size, passed in by
    // flux_window_web.cpp when it calls getMeasureContext().

    explicit MeasureContext(int w, int h) : ctx(w, h) {}

    ~MeasureContext() = default;

#endif // __EMSCRIPTEN__
};

// ── BackBuffer::context() — defined here, after GraphicsContext is complete ───
#ifdef _WIN32
inline GraphicsContext BackBuffer::context()
{
    return GraphicsContext(hdc);
}
#endif