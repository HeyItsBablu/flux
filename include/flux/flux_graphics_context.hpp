// flux/flux_graphics_context.hpp
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
//
// Rule: GraphicsContext never owns or frees its underlying handle.
//       Lifetime is managed by BackBuffer (Win32) or CairoState (Linux).
// ============================================================================

struct GraphicsContext {

#ifdef _WIN32

    NativeContext hdc = nullptr;

    GraphicsContext() = default;
    explicit GraphicsContext(NativeContext h) : hdc(h) {}

    bool valid() const { return hdc != nullptr; }

#endif // _WIN32

#ifdef __linux__

    cairo_t* cr     = nullptr;
    int      width  = 0;
    int      height = 0;

    GraphicsContext() = default;
    explicit GraphicsContext(cairo_t* c, int w = 0, int h = 0)
        : cr(c), width(w), height(h) {}

    bool valid() const { return cr != nullptr; }

#endif // __linux__

};

// ============================================================================
// MeasureContext
//
// RAII wrapper that provides a GraphicsContext suitable for text measurement
// before any painting occurs (e.g. during layout).
//
// Win32 : calls GetDC / ReleaseDC around the measurement window.
// Linux : borrows the permanently-live cairo_t* from CairoState — no
//         acquire/release needed; destructor is a no-op.
//
// Usage (identical on both platforms):
//
//   MeasureContext mc = platformWindow.getMeasureContext();
//   painter.measureText(text, font, w, h);   // uses mc.ctx
// ============================================================================

struct MeasureContext {
    GraphicsContext ctx;

    // Non-copyable — the Win32 variant holds a DC that must not be duplicated.
    MeasureContext(const MeasureContext&)            = delete;
    MeasureContext& operator=(const MeasureContext&) = delete;

    // Movable so PlatformWindow::getMeasureContext() can return by value.
    MeasureContext(MeasureContext&&)            = default;
    MeasureContext& operator=(MeasureContext&&) = default;

#ifdef _WIN32

    HWND hwnd = nullptr;

    explicit MeasureContext(HWND h)
        : hwnd(h), ctx(GetDC(h)) {}

    ~MeasureContext() {
        if (hwnd && ctx.hdc)
            ReleaseDC(hwnd, ctx.hdc);
    }

#endif // _WIN32

#ifdef __linux__

    // No resources to acquire — Cairo context is owned by CairoState.
    explicit MeasureContext(cairo_t* cr, int w = 0, int h = 0)
        : ctx(cr, w, h) {}

    ~MeasureContext() = default;   // nothing to release

#endif // __linux__

};