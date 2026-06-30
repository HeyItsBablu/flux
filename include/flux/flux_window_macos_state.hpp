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
#include "flux_canvas.hpp"       // for CanvasWidget*
#include "flux_canvas2d.hpp"
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
//   - Glyph atlas (Stage 2) — replaces the old whole-string CG scratch
//     upload from the previous pass. textScratch fields are kept only as
//     a fallback rasterization buffer used internally by GlyphAtlas's
//     per-glyph rasterization, not for whole-line text anymore.
//   - Per-CanvasWidget Metal layer registry — still independent of
//     uiMetalLayer; folding these together is Stage 4, not done here.
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
    // Per-window. Lazily initialized once metalDevice is available (see
    // ensureUILayer, which both Metal layer and atlas init are gated on
    // since both need a live device).
    GlyphAtlas glyphAtlas;
    bool glyphAtlasReady = false;

    // ── Canvas registry (unchanged from prior stages) ─────────────────────
    std::vector<CanvasWidget *> canvasWidgets;
    CanvasWidget *capturedCanvas = nullptr;
    CanvasWidget *focusedCanvas  = nullptr;

    Canvas2DGL *canvasGL = nullptr;
    std::unordered_map<TimerID, NSTimer *> timerMap;

    bool running      = false;
    bool dirty        = false;
    bool mouseCapture = false;

    // ── Metal UI layer setup ───────────────────────────────────────────────
    // Called from create(), setFrameSize:, and lazily from renderFrame if
    // somehow not yet sized. zPosition is explicit so layering relative to
    // per-CanvasWidget Metal layers (zPosition 0, see flux_canvas_macos.mm)
    // doesn't depend on insertion-order luck.
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

    // ── Text scratch surface ───────────────────────────────────────────────
    // NOTE: no longer used by Painter::drawTextA directly (that now goes
    // through GlyphAtlas::getOrInsert, one glyph at a time). Kept here as
    // shared scratch CG storage available to any future per-glyph or
    // per-run rasterization helper that needs a CG surface without
    // allocating one ad hoc. Currently unused by the Stage 2 draw path
    // itself (GlyphAtlas rasterizes its own small per-glyph buffers
    // internally) — left in place as reusable infrastructure rather than
    // removed, in case rich-text decorations or future CG-assisted effects
    // need it. Safe to delete later if nothing ends up using it.
    std::vector<uint8_t> textScratchPixels;
    CGContextRef textScratchCtx = nullptr;
    int textScratchCapW = 0;
    int textScratchCapH = 0;

    CGContextRef ensureTextScratch(int w, int h)
    {
        w = std::max(w, 1);
        h = std::max(h, 1);
        if (w > textScratchCapW || h > textScratchCapH || !textScratchCtx)
        {
            if (textScratchCtx) { CGContextRelease(textScratchCtx); textScratchCtx = nullptr; }
            textScratchCapW = std::max(w, textScratchCapW);
            textScratchCapH = std::max(h, textScratchCapH);
            textScratchPixels.assign(
                (size_t)textScratchCapW * textScratchCapH * 4, 0);

            CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
            // Host-order premultiplied-first == BGRA on Apple platforms.
            textScratchCtx = CGBitmapContextCreate(
                textScratchPixels.data(), textScratchCapW, textScratchCapH,
                8, textScratchCapW * 4, cs,
                kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Host);
            CGColorSpaceRelease(cs);
        }
        CGContextClearRect(textScratchCtx, CGRectMake(0, 0, w, h));
        return textScratchCtx;
    }

    ~MacState()
    {
        if (textScratchCtx) { CGContextRelease(textScratchCtx); textScratchCtx = nullptr; }
        for (auto &[id, timer] : timerMap)
            [timer invalidate];
        timerMap.clear();
    }
};

// ============================================================================
// Bridge functions for flux_painter_macos.mm
//
// Keep Painter free of Cocoa/Metal *window*-management headers (it only
// needs Metal types it already imports directly, plus these two calls into
// MacState). `provider` is GraphicsContext::textScratchProvider, always a
// PlatformWindow::MacState* on this platform.
// ============================================================================

inline CGContextRef fluxTextScratch(void *provider, int w, int h)
{
    auto *state = static_cast<PlatformWindow::MacState *>(provider);
    return state ? state->ensureTextScratch(w, h) : nullptr;
}

inline const uint8_t *fluxTextScratchPixels(void *provider, int *outStride)
{
    auto *state = static_cast<PlatformWindow::MacState *>(provider);
    if (!state || !state->textScratchCtx) { if (outStride) *outStride = 0; return nullptr; }
    if (outStride) *outStride = state->textScratchCapW * 4;
    return state->textScratchPixels.data();
}

// Glyph atlas accessor — Painter's draw loop calls this per glyph.
// Returns nullptr if the atlas isn't ready yet (shouldn't happen in
// practice since ensureUILayer() initializes it before any paint pass
// can run, but checked defensively).
inline GlyphAtlas *fluxGlyphAtlas(void *provider)
{
    auto *state = static_cast<PlatformWindow::MacState *>(provider);
    if (!state || !state->glyphAtlasReady) return nullptr;
    return &state->glyphAtlas;
}

#endif
#endif