// flux_overlay_manager_macos.mm
//
// Apple/Metal-specific half of OverlayManager::renderAll(). Kept out of
// flux_overlay_manager.cpp because that file compiles as plain C++ (no
// -x objective-c++), so it can never see ObjC/Metal syntax directly —
// same boundary flux_platform.hpp's void* mtlEncoder/mtlDevice fields
// exist to preserve. This file is the one place allowed to reach through
// those void*s with __bridge casts.
#ifdef __APPLE__
#include <TargetConditionals.h>
#if TARGET_OS_OSX

#import <Metal/Metal.h>
#include "flux/flux_overlay_manager.hpp"
#include "flux/flux_core.hpp"

void flux_overlayManager_renderMac(GraphicsContext &ctx, OverlayContent *content,
                                    FontCache &fc, int clientX, int clientY,
                                    int w, int h)
{
    id<MTLRenderCommandEncoder> encoder =
        (__bridge id<MTLRenderCommandEncoder>)ctx.mtlEncoder;
    if (!encoder)
        return;

    // Scissor-clip to the overlay's rect, in absolute window pixels — same
    // mechanism Painter::pushClipRect/popClipRect already use.
    MTLScissorRect rect;
    rect.x      = (NSUInteger)std::max(0, clientX);
    rect.y      = (NSUInteger)std::max(0, clientY);
    rect.width  = (NSUInteger)std::max(0, w);
    rect.height = (NSUInteger)std::max(0, h);
    [encoder setScissorRect:rect];

    // Local GraphicsContext: same encoder/device/atlas, but its mvp folds
    // in the (clientX, clientY) offset so renderOverlay() can keep drawing
    // in local (0,0-relative) coordinates, same as every other platform
    // branch in renderAll() (and same as what CGContextTranslateCTM used
    // to do before the Metal migration).
    GraphicsContext localCtx = ctx;
    localCtx.width  = w;
    localCtx.height = h;

    // ctx.mvp is a plain orthographic projection (no rotation/skew):
    // out.x = mvp[0]*x + mvp[12], out.y = mvp[5]*y + mvp[13].
    // Drawing at local x_local = x_global - clientX should produce the
    // same out.x as drawing x_global under the un-shifted mvp, so:
    //   mvp_local[12] = mvp[12] + mvp[0]*clientX   (and same for y)
    localCtx.mvp[12] += ctx.mvp[0] * (float)clientX;
    localCtx.mvp[13] += ctx.mvp[5] * (float)clientY;

    content->renderOverlay(localCtx, fc);

    // Restore full-frame scissor so whatever renders next (the next
    // overlay, or nothing) isn't left clipped to this one's rect. No clip
    // *stack* exists yet on this path (same TODO already called out in
    // flux_painter_macos.mm), so nested overlays-within-overlays would
    // still clobber each other's scissor rect — inherited limitation, not
    // introduced here.
    MTLScissorRect full;
    full.x = 0;
    full.y = 0;
    full.width  = (NSUInteger)ctx.width;
    full.height = (NSUInteger)ctx.height;
    [encoder setScissorRect:full];
}

#endif // TARGET_OS_OSX
#endif // __APPLE__