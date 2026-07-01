// flux/flux_platform.hpp
#pragma once

#include <cstdint>
#include <string>
#include <cmath>
#include <vector>

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

    bool operator==(const Color &o) const { return r == o.r && g == o.g && b == o.b && a == o.a; }
    bool operator!=(const Color &o) const { return !(*this == o); }
};

using NativeColor = Color;

// ============================================================================
// WIN32 PLATFORM
// ============================================================================

#ifdef _WIN32

#include <windows.h>
#include <windowsx.h>
#include <sdkddkver.h>
#include <d2d1_3.h>
#include <d2d1_1helper.h>
#include <dwrite_3.h>

using AppInstance = HINSTANCE;
using TimerID = uint32_t;

struct BrushCache;

using NativeWindow = HWND;
using NativeContext = ID2D1DeviceContext1 *;
using NativeFont = IDWriteTextFormat *;
using NativeImage = ID2D1Bitmap1 *;

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
inline bool platformSpaceDown() { return platformKeyDown(VK_SPACE); }  // VK_SPACE = 0x20, already defined by windows.h


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

#ifndef DT_LEFT
static constexpr UINT DT_LEFT = 0x0000u;
static constexpr UINT DT_CENTER = 0x0001u;
static constexpr UINT DT_RIGHT = 0x0002u;
static constexpr UINT DT_VCENTER = 0x0004u;
static constexpr UINT DT_TOP = 0x0000u;
static constexpr UINT DT_WORDBREAK = 0x0010u;
static constexpr UINT DT_SINGLELINE = 0x0020u;
static constexpr UINT DT_NOCLIP = 0x0100u;
static constexpr UINT DT_END_ELLIPSIS = 0x8000u;
static constexpr UINT DT_MODIFYSTRING = 0x10000u;
#endif

using PFNWGLCREATECONTEXTATTRIBSARBPROC =
    HGLRC(WINAPI *)(HDC, HGLRC, const int *);
using PFNWGLCHOOSEPIXELFORMATARBPROC =
    BOOL(WINAPI *)(HDC, const int *, const FLOAT *, UINT, int *, UINT *);

#endif // _WIN32

// ============================================================================
// LINUX PLATFORM
// ============================================================================
#if defined(__linux__) && !defined(__ANDROID__)

#include <ctime>
#include <SDL2/SDL.h>
#include <cairo/cairo.h>
#include <pango/pangocairo.h>

using NativeWindow = SDL_Window *;
using NativeFont = void *;
using AppInstance = void *;
using TimerID = uint32_t;
using UINT = unsigned int;
using NativeImage = void *;

inline uint32_t platformTickCount()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint32_t>(ts.tv_sec * 1000u + ts.tv_nsec / 1'000'000u);
}
inline bool platformKeyDown(int sdlScancode)
{
    const uint8_t *state = SDL_GetKeyboardState(nullptr);
    return state != nullptr && state[sdlScancode];
}
inline bool platformCtrlDown() { return (SDL_GetModState() & (KMOD_LCTRL | KMOD_RCTRL)) != 0; }
inline bool platformShiftDown() { return (SDL_GetModState() & (KMOD_LSHIFT | KMOD_RSHIFT)) != 0; }
inline bool platformAltDown() { return (SDL_GetModState() & (KMOD_LALT | KMOD_RALT)) != 0; }
inline bool platformSpaceDown() { return platformKeyDown(SDL_SCANCODE_SPACE); }

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

static constexpr UINT DT_LEFT = 0x0000u;
static constexpr UINT DT_CENTER = 0x0001u;
static constexpr UINT DT_RIGHT = 0x0002u;
static constexpr UINT DT_VCENTER = 0x0004u;
static constexpr UINT DT_TOP = 0x0000u;
static constexpr UINT DT_WORDBREAK = 0x0010u;
static constexpr UINT DT_SINGLELINE = 0x0020u;
static constexpr UINT DT_NOCLIP = 0x0100u;
static constexpr UINT DT_END_ELLIPSIS = 0x8000u;

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
// ── NativeImage is a GL texture handle — must include gl2.h for GLuint ───────
#include <GLES2/gl2.h>
#include <ctime>

using NativeWindow = ANativeWindow *;
using NativeFont = void *;
using AppInstance = android_app *;
using TimerID = uint32_t;
using UINT = unsigned int;

using NativeImage = GLuint;

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
inline bool platformSpaceDown() { return false; }

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
using NativeImage = void *;

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
inline bool platformSpaceDown() { return false; } 

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

using NativeWindow = void *;
using NativeFont = void *;
using AppInstance = void *;
using TimerID = long;
using UINT = unsigned int;
using NativeImage = void *;

static constexpr UINT DT_LEFT = 0x0000u;
static constexpr UINT DT_CENTER = 0x0001u;
static constexpr UINT DT_RIGHT = 0x0002u;
static constexpr UINT DT_VCENTER = 0x0004u;
static constexpr UINT DT_TOP = 0x0000u;
static constexpr UINT DT_WORDBREAK = 0x0010u;
static constexpr UINT DT_SINGLELINE = 0x0020u;
static constexpr UINT DT_NOCLIP = 0x0100u;
static constexpr UINT DT_END_ELLIPSIS = 0x8000u;

static constexpr int VK_BACK = 8;
static constexpr int VK_TAB = 9;
static constexpr int VK_RETURN = 13;
static constexpr int VK_ESCAPE = 27;
static constexpr int VK_PRIOR = 33;
static constexpr int VK_NEXT = 34;
static constexpr int VK_END = 35;
static constexpr int VK_HOME = 36;
static constexpr int VK_LEFT = 37;
static constexpr int VK_UP = 38;
static constexpr int VK_RIGHT = 39;
static constexpr int VK_DOWN = 40;
static constexpr int VK_DELETE = 46;
static constexpr int VK_SHIFT = 16;
static constexpr int VK_CONTROL = 17;
static constexpr int VK_MENU = 18;
static constexpr int VK_SPACE = 32;

namespace flux_web_detail
{
    inline bool g_ctrlDown = false;
    inline bool g_shiftDown = false;
    inline bool g_altDown = false;
}
inline bool platformCtrlDown() { return flux_web_detail::g_ctrlDown; }
inline bool platformShiftDown() { return flux_web_detail::g_shiftDown; }
inline bool platformAltDown() { return flux_web_detail::g_altDown; }
inline bool platformKeyDown(int) { return false; }
inline bool platformSpaceDown() { return platformKeyDown(VK_SPACE); }

inline uint32_t platformTickCount()
{
    return static_cast<uint32_t>(emscripten_get_now());
}

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
// ============================================================================

#ifdef _WIN32

struct GraphicsContext
{
    ID2D1DeviceContext1 *dc = nullptr;
    IDWriteFactory3 *dwrite = nullptr;
    ID2D1Factory1 *factory = nullptr;
    BrushCache *brushes = nullptr;

    std::vector<HRGN> clipStack;

    GraphicsContext() = default;
    GraphicsContext(ID2D1DeviceContext1 *dc,
                    IDWriteFactory3 *dwrite,
                    ID2D1Factory1 *factory,
                    BrushCache *brushes)
        : dc(dc), dwrite(dwrite), factory(factory), brushes(brushes) {}

    bool valid() const { return dc != nullptr; }
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
    int overlayOffsetX = 0;
    int overlayOffsetY = 0;

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
    // ── Metal (primary path — vectors, images, fills, and now text via the
    // glyph atlas) ───────────────────────────────────────────────────────────
    // Real types: id<MTLDevice>, id<MTLRenderCommandEncoder>,
    // id<MTLRenderPipelineState>, id<MTLBuffer>. Stored as void* here so this
    // header stays usable from plain .cpp translation units; cast via
    // (__bridge id<...>) only inside flux_painter_macos.mm / flux_window_macos.mm.
    void *mtlDevice        = nullptr;
    void *mtlEncoder       = nullptr; // current frame's render command encoder; null for measure-only contexts
    void *solidPipeline    = nullptr; // unused currently — Painter builds its own pipeline cache; reserved for Stage 1 consolidation
    void *texturedPipeline = nullptr; // unused currently — see above
    void *scratchVertexBuf = nullptr; // unused currently — see above

    // Orthographic projection mapping window pixel space -> clip space.
    // Column-major, matches the layout used in flux_window_macos.mm's renderFrame.
    float mvp[16] = {
        1,0,0,0,
        0,1,0,0,
        0,0,1,0,
        0,0,0,1
    };

    int width  = 0; // physical pixels
    int height = 0;

    // ── Bridge to PlatformWindow::MacState (opaque here) ────────────────────
    // Used by Painter::drawTextA/drawRichTextA to reach the per-window
    // GlyphAtlas via fluxGlyphAtlas(), and by the (legacy/optional) scratch
    // CG buffer via fluxTextScratch(). Always a PlatformWindow::MacState* on
    // this platform; kept void* so this header doesn't need to know that
    // type. Set on every GraphicsContext handed to Painter or LayoutEngine —
    // including measure-only contexts, since CoreText measurement also
    // resolves through the same FontCache/CTFontRef path.
    void *textScratchProvider = nullptr;

    GraphicsContext() = default;

    // valid() distinguishes a paint-capable context (has a live Metal
    // encoder) from a measure/layout-only context (mtlEncoder is null).
    // Painter's draw methods early-return safely when this is false via
    // their own enc(ctx) null checks — valid() is for callers who want to
    // check before doing any work at all.
    bool valid() const { return mtlEncoder != nullptr; }
};

#endif // TARGET_OS_OSX
#endif // __APPLE__

#ifdef __EMSCRIPTEN__

struct GraphicsContext
{
    int width = 0;
    int height = 0;

    GraphicsContext() = default;
    GraphicsContext(int w, int h) : width(w), height(h) {}

    bool valid() const { return width > 0 && height > 0; }
};

#endif // __EMSCRIPTEN__

// ============================================================================
// MeasureContext
// ============================================================================

struct MeasureContext
{
    GraphicsContext ctx;

    MeasureContext(const MeasureContext &) = delete;
    MeasureContext &operator=(const MeasureContext &) = delete;
    MeasureContext(MeasureContext &&) = default;
    MeasureContext &operator=(MeasureContext &&) = default;

#ifdef _WIN32
    explicit MeasureContext(ID2D1DeviceContext1 *dc,
                            IDWriteFactory3 *dwrite,
                            ID2D1Factory1 *factory,
                            BrushCache *brushes)
        : ctx(dc, dwrite, factory, brushes) {}
    ~MeasureContext() = default;
#endif

#if defined(__linux__) && !defined(__ANDROID__)
    explicit MeasureContext(cairo_t *cr, int w = 0, int h = 0)
        : ctx(cr, w, h) {}
    ~MeasureContext() = default;
#endif

#ifdef __ANDROID__
    explicit MeasureContext(int w, int h) : ctx(w, h) {}
    ~MeasureContext() = default;
#endif

#ifdef __APPLE__
#if TARGET_OS_OSX
    // No parameterized constructor needed — GraphicsContext's fields
    // (width/height/textScratchProvider) are public and set directly by
    // the caller (see PlatformWindow::getMeasureContext() in
    // flux_window_macos.mm) before/after this MeasureContext wraps it.
    MeasureContext() = default;
    ~MeasureContext() = default;
#endif
#endif

#ifdef __EMSCRIPTEN__
    explicit MeasureContext(int w, int h) : ctx(w, h) {}
    ~MeasureContext() = default;
#endif
};