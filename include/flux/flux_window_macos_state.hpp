// flux_window_macos_state.hpp - macOS-specific state for Flux windows
#pragma once
#ifdef __APPLE__
#include <TargetConditionals.h>
#if TARGET_OS_OSX

#import <Cocoa/Cocoa.h>
#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>
#import <QuartzCore/CAMetalLayer.h>

#include "flux_window.hpp"
#include "flux_canvas.hpp" // for CanvasWidget*
#include "flux_canvas2d.hpp"

#include <vector>
#include <unordered_map>

@class FluxNSView;

struct PlatformWindow::MacState
{
    NSWindow *nsWindow = nil;
    FluxNSView *nsView = nil;
    id<MTLDevice> metalDevice = nil;
    CAMetalLayer *metalLayer = nil;

    std::vector<uint8_t> cgPixels;
    CGContextRef cgContext = nullptr;
    int cgWidth = 0;
    int cgHeight = 0;

    std::vector<CanvasWidget *> canvasWidgets;
    CanvasWidget *capturedCanvas = nullptr;
    CanvasWidget *focusedCanvas = nullptr;

    Canvas2DGL *canvasGL = nullptr;
    std::unordered_map<TimerID, NSTimer *> timerMap;

    bool running = false;
    bool dirty = false;
    bool mouseCapture = false;

    bool rebuildCGContext(int w, int h)
    {
        teardownCGContext();
        if (w <= 0 || h <= 0)
            return false;
        cgWidth = w;
        cgHeight = h;
        cgPixels.assign(static_cast<size_t>(w) * h * 4, 0);

        CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
        cgContext = CGBitmapContextCreate(
            cgPixels.data(), w, h,
            8,     // bits per component
            w * 4, // bytes per row
            cs,
            kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Host);
        CGColorSpaceRelease(cs);

        if (!cgContext)
        {
            cgPixels.clear();
            cgWidth = cgHeight = 0;
            return false;
        }

        // Flip coordinate system so (0,0) is top-left, matching Win32/Linux
        CGContextTranslateCTM(cgContext, 0, h);
        CGContextScaleCTM(cgContext, 1.0, -1.0);

        return true;
    }

    void teardownCGContext()
    {
        if (cgContext)
        {
            CGContextRelease(cgContext);
            cgContext = nullptr;
        }
        cgPixels.clear();
        cgWidth = cgHeight = 0;
    }

    ~MacState()
    {
        teardownCGContext();
        for (auto &[id, timer] : timerMap)
            [timer invalidate];
        timerMap.clear();
    }
};

#endif
#endif