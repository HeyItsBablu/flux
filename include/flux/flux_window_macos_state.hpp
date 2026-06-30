// flux_window_macos_state.hpp - macOS-specific state for Flux windows
#pragma once
#ifdef __APPLE__
#include <TargetConditionals.h>
#if TARGET_OS_OSX

#import <Cocoa/Cocoa.h>
#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>
#import <QuartzCore/CAMetalLayer.h>
#import <CoreGraphics/CoreGraphics.h>

#include "flux_window.hpp"
#include "flux_canvas.hpp"            // for CanvasWidget*
#include "flux_canvas2d_backend.hpp"  // Canvas2DMetal / Canvas2DBackend (Apple block)
#include "flux_glyph_atlas_macos.hpp"

#include <algorithm>
#include <vector>
#include <unordered_map>

@class FluxNSView;

// ============================================================================
// PlatformWindow::MacState
//
// Owns all macOS-specific window resources:
//   - Cocoa window/view
//   - Shared Metal device/queue/layer for UI content (Painter's vector +
//     text draw calls all render into uiMetalLayer via this device/queue)
//   - Glyph atlas (Stage 2)
//   - Canvas2DMetal + Canvas2DBackend (shared, per-window) — backs every
//     CanvasWidget's drawing surface. Per-CanvasWidget state (its own
//     CAMetalLayer, command queue) still lives in flux_canvas_macos.mm's
//     MacCanvasState; only the font/glyph/draw-call backend is shared here,
//     since registering fonts once per window (not per widget) avoids
//     redundant font loads.
//   - Timers, mouse capture/focus state.
// ============================================================================

struct PlatformWindow::MacState
{
    NSWindow   *nsWindow = nil;
    FluxNSView *nsView   = nil;

    // ── Shared Metal — primary surface for all UI (non-canvas) content ───────
    id<MTLDevice>       metalDevice  = nil;
    id<MTLCommandQueue> commandQueue = nil;
    CAMetalLayer        *uiMetalLayer = nil;

    // ── Glyph atlas (Stage 2) ──────────────────────────────────────────────
    GlyphAtlas glyphAtlas;
    bool glyphAtlasReady = false;

    // ── Canvas2D Metal backend (shared across all CanvasWidgets in this
    // window) ────────────────────────────────────────────────────────────
    // Canvas2DMetal owns font registration / glyph metrics for the
    // CanvasWidget-facing Canvas2D API (distinct from Painter's own glyph
    // atlas above — Canvas2D is a separate user-facing drawing surface,
    // e.g. for custom RenderSurface content, not UI widget text).
    Canvas2DMetal   *canvasMetal  = nullptr;
    Canvas2DBackend *canvasBackend = nullptr;

    // ── Canvas registry ─────────────────────────────────────────────────────
    std::vector<CanvasWidget *> canvasWidgets;
    CanvasWidget *capturedCanvas = nullptr;
    CanvasWidget *focusedCanvas  = nullptr;

    std::unordered_map<TimerID, NSTimer *> timerMap;

    bool running      = false;
    bool dirty        = false;
    bool mouseCapture = false;

    // ── Metal UI layer setup ───────────────────────────────────────────────
    bool ensureUILayer(int physW, int physH)
    {
        if (!metalDevice) return false;

        if (!commandQueue)
            commandQueue = [metalDevice newCommandQueue];

        if (!uiMetalLayer)
        {
            uiMetalLayer = [CAMetalLayer layer];
            uiMetalLayer.device          = metalDevice;
            uiMetalLayer.pixelFormat     = MTLPixelFormatBGRA8Unorm;
            uiMetalLayer.framebufferOnly = YES;
            uiMetalLayer.opaque          = NO; // let canvas layers beneath show through
            [nsView.layer addSublayer:uiMetalLayer];
            uiMetalLayer.zPosition = 1.0; // always above per-widget canvas layers
        }
        uiMetalLayer.frame        = nsView.bounds;
        uiMetalLayer.drawableSize = CGSizeMake(physW, physH);

        if (!glyphAtlasReady)
            glyphAtlasReady = glyphAtlas.init(metalDevice);

        ensureCanvasBackend();

        return true;
    }

    // ── Canvas2D backend setup ───────────────────────────────────────────────
    // Lazily creates the shared Canvas2DMetal + Canvas2DBackend once the
    // device is live. registerFont() calls (sans/sans-bold/mono) happen in
    // PlatformWindow::create() against canvasBackend, same as the old
    // Canvas2DGL registerFont calls did.
    void ensureCanvasBackend()
    {
        if (canvasBackend || !metalDevice) return;
        canvasMetal = new Canvas2DMetal();
        if (!canvasMetal->init())
        {
            delete canvasMetal;
            canvasMetal = nullptr;
            return;
        }
        canvasBackend = Canvas2DBackend::create(canvasMetal);
    }

    void teardownCanvasBackend()
    {
        if (canvasBackend) { Canvas2DBackend::destroy(canvasBackend); canvasBackend = nullptr; }
        if (canvasMetal)   { canvasMetal->destroy(); delete canvasMetal; canvasMetal = nullptr; }
    }

    ~MacState()
    {
        teardownCanvasBackend();
        for (auto &[id, timer] : timerMap)
            [timer invalidate];
        timerMap.clear();
    }
};

// ============================================================================
// Bridge functions for flux_painter_macos.mm
// ============================================================================

inline GlyphAtlas *fluxGlyphAtlas(void *provider)
{
    auto *state = static_cast<PlatformWindow::MacState *>(provider);
    if (!state || !state->glyphAtlasReady) return nullptr;
    return &state->glyphAtlas;
}

#endif
#endif