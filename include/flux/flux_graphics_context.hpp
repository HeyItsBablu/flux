#pragma once

#include "flux_platform.hpp"

// ============================================================================
// GraphicsContext
//
// Thin wrapper around the platform drawing surface passed to every Painter
// call and every layout/measure operation.
//
// Win32 : wraps HDC  — identical lifetime to the BackBuffer HDC it comes from.
// Linux : wraps cairo_t* — borrowed from CairoState; never owned here.
// Android: wraps width/height — NanoVG is stateful; no context pointer needed.
//
// Rule: GraphicsContext never owns or frees its underlying handle.
//       Lifetime is managed by BackBuffer (Win32), CairoState (Linux),
//       or the NanoVG frame (Android).
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

    // Non-copyable — the Win32 variant holds a DC that must not be duplicated.
    MeasureContext(const MeasureContext &) = delete;
    MeasureContext &operator=(const MeasureContext &) = delete;

    // Movable so PlatformWindow::getMeasureContext() can return by value.
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

    // No resources to acquire — Cairo context is owned by CairoState.
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

    // CoreGraphics context is owned by the window/view — never by MeasureContext.
    explicit MeasureContext(CGContextRef cg, int w = 0, int h = 0)
        : ctx(cg, w, h) {}

    ~MeasureContext() = default;

#endif // TARGET_OS_OSX
#endif // __APPLE__
};