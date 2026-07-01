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
// No flux_canvas.hpp or flux_canvas2d_backend.hpp here anymore —
// MacState has zero knowledge of CanvasWidget types or canvas rendering
// resources. Each CanvasWidget creates and owns its own Canvas2DMetal/
// Canvas2DBackend (and CAMetalLayer/command queue) lazily, mirroring
// flux_canvas_win32.cpp's ensureD2D/destroyD2D pattern.
#include "flux_glyph_atlas_macos.hpp"

#include <unordered_map>

@class FluxNSView;

// ============================================================================
// PlatformWindow::MacState
//
// Owns only window-lifetime resources:
//   - Cocoa window/view
//   - Shared Metal device + command queue for UI content (Painter's vector
//     and text draw calls render into uiMetalLayer via this device/queue)
//   - Glyph atlas for UI text (distinct from CanvasWidget's own font
//     resources, which live in each widget's own Canvas2DMetal)
//   - Timers, mouse capture state
//
// NOT owned here (moved per-widget to MacCanvasState in flux_canvas_macos.mm):
//   - Canvas widget registry (canvasWidgets / capturedCanvas / focusedCanvas)
//   - Canvas2DMetal / Canvas2DBackend (now one per CanvasWidget, not one
//     per window)
//   - Per-widget CAMetalLayer and command queue
// ============================================================================

struct PlatformWindow::MacState
{
    NSWindow   *nsWindow = nil;
    FluxNSView *nsView   = nil;

    // ── Shared Metal — primary surface for all UI (non-canvas) content ───────
    id<MTLDevice>       metalDevice  = nil;
    id<MTLCommandQueue> commandQueue = nil;
    CAMetalLayer        *uiMetalLayer = nil;

    // ── Glyph atlas for Painter / UI text ────────────────────────────────────
    GlyphAtlas glyphAtlas;
    bool glyphAtlasReady = false;

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

        return true;
    }

    ~MacState()
    {
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