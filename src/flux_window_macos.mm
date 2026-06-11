// flux_window_macos.mm
#ifdef __APPLE__
#include <TargetConditionals.h>
#if TARGET_OS_OSX

// Metal imports FIRST — before any flux headers
#import <Cocoa/Cocoa.h>
#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>
#import <QuartzCore/CAMetalLayer.h>
#import <CoreGraphics/CoreGraphics.h>
#import <AudioToolbox/AudioToolbox.h>

// flux headers AFTER
#include "flux/flux_window.hpp"
#include "flux/flux_canvas.hpp"
#include "flux/flux_window_macos_state.hpp"

#include <unordered_map>
#include <vector>

// ============================================================================
// Forward declarations
// ============================================================================

@class FluxNSView;
@class FluxAppDelegate;




// ============================================================================
// FluxNSView — NSView subclass that bridges Cocoa events to PlatformWindow
// ============================================================================

@interface FluxNSView : NSView <NSWindowDelegate> {
    PlatformWindow* _win;
}
- (instancetype)initWithFrame:(NSRect)frame window:(PlatformWindow*)win;
@end

@implementation FluxNSView

- (instancetype)initWithFrame:(NSRect)frame window:(PlatformWindow*)win {
    self = [super initWithFrame:frame];
    if (self) {
        _win = win;
        self.wantsLayer = YES;
    }
    return self;
}

- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)isOpaque              { return YES; }

// ── Drawing ──────────────────────────────────────────────────────────────────
- (void)drawRect:(NSRect)dirtyRect {
    if (!_win || !_win->macState) return;
    auto& s = *_win->macState;
    if (!s.cgContext) return;

    // Clear CG surface
    CGContextClearRect(s.cgContext, CGRectMake(0, 0, s.cgWidth, s.cgHeight));

    // Paint UI widgets
    GraphicsContext ctx(s.cgContext, s.cgWidth, s.cgHeight);
    if (_win->callbacks.onPaint)
        _win->callbacks.onPaint(ctx, s.cgWidth, s.cgHeight);

    // Composite CG pixels into the view via CGImage
    CGContextRef viewCtx = [[NSGraphicsContext currentContext] CGContext];
    if (viewCtx && s.cgContext) {
        CGImageRef img = CGBitmapContextCreateImage(s.cgContext);
        if (img) {
            NSRect bounds = self.bounds;
            CGContextDrawImage(viewCtx,
                CGRectMake(0, 0, bounds.size.width, bounds.size.height), img);
            CGImageRelease(img);
        }
    }
}

// ── Resize ───────────────────────────────────────────────────────────────────
- (void)setFrameSize:(NSSize)newSize {
    [super setFrameSize:newSize];
    if (!_win || !_win->macState) return;

    // Use backing (physical) pixels for cachedWidth/Height
    NSRect backing = [self convertRectToBacking:self.bounds];
    _win->cachedWidth  = (int)backing.size.width;
    _win->cachedHeight = (int)backing.size.height;

    int lw = (int)newSize.width;
    int lh = (int)newSize.height;

    auto& s = *_win->macState;
    s.rebuildCGContext(lw, lh);

    // Update Metal layer size
    if (s.metalLayer) {
        s.metalLayer.drawableSize = backing.size;
    }

    // Notify canvas widgets
    for (CanvasWidget* cw : s.canvasWidgets)
        if (cw) cw->onWindowResize(_win->cachedWidth, _win->cachedHeight);

    if (_win->callbacks.onResize && s.cgContext) {
        GraphicsContext ctx(s.cgContext, lw, lh);
        _win->callbacks.onResize(ctx, lw, lh);
    }

    [self setNeedsDisplay:YES];
}

// ── Mouse events ─────────────────────────────────────────────────────────────

- (NSPoint)fluxPoint:(NSEvent*)event {
    NSPoint p = [self convertPoint:event.locationInWindow fromView:nil];
    // Flip Y — AppKit origin is bottom-left, FluxUI is top-left
    return NSMakePoint(p.x, self.bounds.size.height - p.y);
}

- (void)mouseDown:(NSEvent*)event {
    if (!_win) return;
    NSPoint p = [self fluxPoint:event];
    int mx = (int)p.x, my = (int)p.y;
    [self becomeFirstResponder];

    auto& s = *_win->macState;
    CanvasWidget* cw = _win->hitTestCanvas(mx, my);
    if (cw) {
        s.capturedCanvas = cw;
        s.focusedCanvas  = cw;
        cw->onMouseButtonDown(mx - cw->x, my - cw->y, 0);
        _win->macState->dirty = true; return;
    }
    s.focusedCanvas = nullptr;
    if (_win->callbacks.onMouseDown && _win->callbacks.onMouseDown(mx, my))
        [self setNeedsDisplay:YES];
}

- (void)mouseUp:(NSEvent*)event {
    if (!_win) return;
    NSPoint p = [self fluxPoint:event];
    int mx = (int)p.x, my = (int)p.y;

    auto& s = *_win->macState;
    CanvasWidget* cw = s.capturedCanvas ? s.capturedCanvas : _win->hitTestCanvas(mx, my);
    s.capturedCanvas = nullptr;
    if (cw) { cw->onMouseButtonUp(mx - cw->x, my - cw->y, 0); [self setNeedsDisplay:YES]; return; }
    if (_win->callbacks.onMouseUp && _win->callbacks.onMouseUp(mx, my))
        [self setNeedsDisplay:YES];
}

- (void)mouseDragged:(NSEvent*)event { [self mouseMoved:event]; }

- (void)mouseMoved:(NSEvent*)event {
    if (!_win) return;
    NSPoint p = [self fluxPoint:event];
    int mx = (int)p.x, my = (int)p.y;

    auto& s = *_win->macState;
    CanvasWidget* cw = s.capturedCanvas ? s.capturedCanvas : _win->hitTestCanvas(mx, my);
    if (cw) { cw->onMouseMove(mx - cw->x, my - cw->y); [self setNeedsDisplay:YES]; return; }
    if (_win->callbacks.onMouseMove && _win->callbacks.onMouseMove(mx, my))
        [self setNeedsDisplay:YES];
}

- (void)rightMouseDown:(NSEvent*)event {
    if (!_win) return;
    NSPoint p = [self fluxPoint:event];
    if (_win->callbacks.onRightClick &&
        _win->callbacks.onRightClick((int)p.x, (int)p.y))
        [self setNeedsDisplay:YES];
}

- (void)scrollWheel:(NSEvent*)event {
    if (!_win) return;
    // Normalize to Win32 WHEEL_DELTA units
    int delta = (int)(event.scrollingDeltaY * WHEEL_DELTA);
    if (_win->callbacks.onMouseWheel && _win->callbacks.onMouseWheel(delta))
        [self setNeedsDisplay:YES];
}

- (void)mouseExited:(NSEvent*)event {
    if (!_win) return;
    auto& s = *_win->macState;
    s.capturedCanvas = nullptr;
    for (CanvasWidget* cw : s.canvasWidgets)
        if (cw) cw->onMouseLeave();
    if (_win->callbacks.onMouseLeave) _win->callbacks.onMouseLeave();
}

// ── Keyboard events ───────────────────────────────────────────────────────────

- (void)keyDown:(NSEvent*)event {
    if (!_win) return;
    auto& s = *_win->macState;

    // Pass to focused canvas first
    if (s.focusedCanvas) {
        s.focusedCanvas->onKeyDown((int)event.keyCode);
        [self setNeedsDisplay:YES];
        return;
    }

    if (_win->callbacks.onKeyDown &&
        _win->callbacks.onKeyDown((int)event.keyCode))
        [self setNeedsDisplay:YES];

    // Character input
    NSString* chars = event.characters;
    if (chars.length > 0 && _win->callbacks.onChar) {
        wchar_t ch = (wchar_t)[chars characterAtIndex:0];
        if (ch >= 32 && _win->callbacks.onChar(ch))  // skip control chars
            [self setNeedsDisplay:YES];
    }
}

// ── NSWindowDelegate ──────────────────────────────────────────────────────────

- (void)windowWillClose:(NSNotification*)notification {
    if (_win) _win->macState->running = false;
    [NSApp stop:nil];
}

- (void)windowDidResignKey:(NSNotification*)notification {
    if (!_win) return;
    auto& s = *_win->macState;
    s.capturedCanvas = nullptr;
    s.focusedCanvas  = nullptr;
    if (_win->callbacks.onFocusLost) _win->callbacks.onFocusLost();
    [self setNeedsDisplay:YES];
}

@end

// ============================================================================
// Timer shim — bridges NSTimer to PlatformWindow callbacks
// ============================================================================

@interface FluxTimerShim : NSObject {
    PlatformWindow* _win;
    TimerID         _id;
}
- (instancetype)initWithWindow:(PlatformWindow*)win timerID:(TimerID)tid;
- (void)fire:(NSTimer*)timer;
@end

@implementation FluxTimerShim
- (instancetype)initWithWindow:(PlatformWindow*)win timerID:(TimerID)tid {
    self = [super init];
    if (self) { _win = win; _id = tid; }
    return self;
}
- (void)fire:(NSTimer*)timer {
    if (_win && _win->callbacks.onTimer) {
        _win->callbacks.onTimer(_id);
        _win->invalidate();
    }
}
@end



// ============================================================================
// PlatformWindow — macOS implementation
// ============================================================================

bool PlatformWindow::create(const std::string& title,
                             int width, int height,
                             AppInstance /*instance*/, void* /*userData*/) {
    macState = new MacState();
    auto& s  = *macState;

    // ── NSApplication setup ───────────────────────────────────────────────────
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];

    // ── Metal device ──────────────────────────────────────────────────────────
    s.metalDevice = MTLCreateSystemDefaultDevice();
    if (!s.metalDevice) {
        fprintf(stderr, "PlatformWindow: no Metal device\n");
        return false;
    }

    // ── NSWindow ──────────────────────────────────────────────────────────────
    NSRect frame = NSMakeRect(0, 0, width, height);
    NSWindowStyleMask style =
        NSWindowStyleMaskTitled        |
        NSWindowStyleMaskClosable      |
        NSWindowStyleMaskMiniaturizable|
        NSWindowStyleMaskResizable;

    s.nsWindow = [[NSWindow alloc]
        initWithContentRect:frame
                  styleMask:style
                    backing:NSBackingStoreBuffered
                      defer:NO];
    [s.nsWindow setTitle:[NSString stringWithUTF8String:title.c_str()]];
    [s.nsWindow center];

    // ── FluxNSView ────────────────────────────────────────────────────────────
    s.nsView = [[FluxNSView alloc] initWithFrame:frame window:this];
    [s.nsWindow setContentView:s.nsView];
    [s.nsWindow setDelegate:s.nsView];
    [s.nsWindow makeFirstResponder:s.nsView];

    // Track mouse-moved and mouse-exit events
    [s.nsView addTrackingArea:[[NSTrackingArea alloc]
        initWithRect:frame
             options:NSTrackingMouseMoved |
                     NSTrackingMouseEnteredAndExited |
                     NSTrackingActiveInKeyWindow |
                     NSTrackingInVisibleRect
               owner:s.nsView
            userInfo:nil]];

    // ── Metal layer on top of the view (canvas only) ──────────────────────────
    s.metalLayer              = [CAMetalLayer layer];
    s.metalLayer.device       = s.metalDevice;
    s.metalLayer.pixelFormat  = MTLPixelFormatBGRA8Unorm;
    s.metalLayer.framebufferOnly = YES;

    // Metal layer sits behind the CG overlay — insert at index 0
    [s.nsView.layer insertSublayer:s.metalLayer atIndex:0];

    // ── Physical / logical sizes ──────────────────────────────────────────────
    NSRect backing = [s.nsView convertRectToBacking:s.nsView.bounds];
    cachedWidth  = (int)backing.size.width;
    cachedHeight = (int)backing.size.height;

    int lw = (int)frame.size.width;
    int lh = (int)frame.size.height;

    s.metalLayer.drawableSize = backing.size;

    // ── CoreGraphics CPU surface for UI painter ───────────────────────────────
    if (!s.rebuildCGContext(lw, lh)) {
        fprintf(stderr, "PlatformWindow: CGContext creation failed\n");
        return false;
    }

    // ── Canvas2DGL (Metal-backed NanoVG equivalent) ───────────────────────────
    s.canvasGL = new Canvas2DGL();
    if (!s.canvasGL->init()) {
        fprintf(stderr, "PlatformWindow: Canvas2DGL init failed\n");
        delete s.canvasGL; s.canvasGL = nullptr;
    }

    if (s.canvasGL) {
        // Register fonts — adjust paths for macOS system fonts
        auto regFont = [&](const char* name, const char* path) {
            if (!Canvas2D::registerFont(s.canvasGL, name, path))
                fprintf(stderr, "PlatformWindow: font '%s' not found at '%s'\n", name, path);
        };
        regFont("sans",      "/System/Library/Fonts/Helvetica.ttc");
        regFont("sans-bold", "/System/Library/Fonts/Helvetica.ttc");
        regFont("mono",      "/System/Library/Fonts/Menlo.ttc");
    }

    // Notify pre-registered canvas widgets
    for (CanvasWidget* cw : s.canvasWidgets)
        if (cw) cw->setCanvasGL(s.canvasGL);

    // ── Show window ───────────────────────────────────────────────────────────
    [s.nsWindow makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];

    s.running = true;
    return true;
}

// ============================================================================
// destroy
// ============================================================================

void PlatformWindow::destroy() {
    if (!macState) return;
    auto& s = *macState;

    for (auto& [id, timer] : s.timerMap)
        [timer invalidate];
    s.timerMap.clear();

    if (s.canvasGL) { s.canvasGL->destroy(); delete s.canvasGL; s.canvasGL = nullptr; }
    if (s.nsWindow) { [s.nsWindow close]; s.nsWindow = nil; }

    delete macState;
    macState = nullptr;
}

// ============================================================================
// run
// ============================================================================

int PlatformWindow::run() {
    [NSApp run];
    return 0;
}

 
NativeWindow PlatformWindow::handle() const {
    return macState ? reinterpret_cast<NativeWindow>(macState->nsWindow) : nullptr;
}
bool PlatformWindow::isMouseCaptured() const {
    return macState && macState->mouseCapture;
}

// ============================================================================
// invalidate / invalidateRect
// ============================================================================

void PlatformWindow::invalidate() {
    if (!macState) return;
    macState->dirty = true;
    dispatch_async(dispatch_get_main_queue(), ^{
        if (macState && macState->nsView)
            [macState->nsView setNeedsDisplay:YES];
    });
}

void PlatformWindow::invalidateRect(int x, int y, int w, int h) {
    if (!macState || !macState->nsView) return;
    macState->dirty = true;
    NSRect r = NSMakeRect(x, y, w, h);
    dispatch_async(dispatch_get_main_queue(), ^{
        if (macState && macState->nsView)
            [macState->nsView setNeedsDisplayInRect:r];
    });
}

// ============================================================================
// Timer
// ============================================================================

void PlatformWindow::setTimer(TimerID id, int ms) {
    if (!macState) return;
    killTimer(id);

    FluxTimerShim* shim = [[FluxTimerShim alloc] initWithWindow:this timerID:id];
    NSTimer* timer = [NSTimer scheduledTimerWithTimeInterval:ms / 1000.0
                                                      target:shim
                                                    selector:@selector(fire:)
                                                    userInfo:nil
                                                     repeats:YES];
    macState->timerMap[id] = timer;
}

void PlatformWindow::killTimer(TimerID id) {
    if (!macState) return;
    auto it = macState->timerMap.find(id);
    if (it != macState->timerMap.end()) {
        [it->second invalidate];
        macState->timerMap.erase(it);
    }
}

// ============================================================================
// Canvas widget registry
// ============================================================================

void PlatformWindow::registerCanvas_public(CanvasWidget* c) {
    if (!c || !macState) return;
    macState->canvasWidgets.push_back(c);
    if (macState->canvasGL) c->setCanvasGL(macState->canvasGL);
}

void PlatformWindow::unregisterCanvas_public(CanvasWidget* c) {
    if (!macState) return;
    if (macState->capturedCanvas == c) macState->capturedCanvas = nullptr;
    if (macState->focusedCanvas  == c) macState->focusedCanvas  = nullptr;
    auto& v = macState->canvasWidgets;
    v.erase(std::remove(v.begin(), v.end(), c), v.end());
}

CanvasWidget* PlatformWindow::hitTestCanvas(int x, int y) {
    if (!macState) return nullptr;
    for (auto it = macState->canvasWidgets.rbegin();
         it != macState->canvasWidgets.rend(); ++it) {
        CanvasWidget* cw = *it;
        if (cw && cw->isInitialized() && cw->containsPoint(x, y)) return cw;
    }
    return nullptr;
}

// ============================================================================
// Size helpers
// ============================================================================

void PlatformWindow::updateClientSize() {
    if (!macState || !macState->nsView) return;
    NSRect backing = [macState->nsView convertRectToBacking:macState->nsView.bounds];
    cachedWidth  = (int)backing.size.width;
    cachedHeight = (int)backing.size.height;
}

PlatformWindow::ClientSize PlatformWindow::getClientSize() const {
    if (!macState || !macState->nsView) return {0, 0};
    NSRect b = macState->nsView.bounds;
    return {(int)b.size.width, (int)b.size.height};
}

PlatformWindow::ScreenPoint
PlatformWindow::clientToScreen(int cx, int cy) const {
    if (!macState || !macState->nsWindow) return {cx, cy};
    NSRect r = [macState->nsWindow convertRectToScreen:NSMakeRect(cx, cy, 1, 1)];
    return {(int)r.origin.x, (int)r.origin.y};
}

PlatformWindow::ScreenPoint
PlatformWindow::screenToClient(int sx, int sy) const {
    if (!macState || !macState->nsWindow) return {sx, sy};
    NSRect r = [macState->nsWindow convertRectFromScreen:NSMakeRect(sx, sy, 1, 1)];
    return {(int)r.origin.x, (int)r.origin.y};
}

// ============================================================================
// GraphicsContext for measure
// ============================================================================

GraphicsContext PlatformWindow::getMeasureContext() const {
    if (!macState) return GraphicsContext();
    return GraphicsContext(macState->cgContext,
                           macState->cgWidth,
                           macState->cgHeight);
}

// ============================================================================
// Clipboard
// ============================================================================

void PlatformWindow::setClipboardText(const std::string& text) {
    NSPasteboard* pb = [NSPasteboard generalPasteboard];
    [pb clearContents];
    [pb setString:[NSString stringWithUTF8String:text.c_str()]
          forType:NSPasteboardTypeString];
}

std::string PlatformWindow::getClipboardText() {
    NSPasteboard* pb  = [NSPasteboard generalPasteboard];
    NSString*     str = [pb stringForType:NSPasteboardTypeString];
    return str ? std::string([str UTF8String]) : std::string();
}

// ============================================================================
// Mouse capture / cursors
// ============================================================================

void PlatformWindow::captureMouseInput()  { if (macState) macState->mouseCapture = true;  }
void PlatformWindow::releaseMouseInput()  { if (macState) macState->mouseCapture = false; }

void PlatformWindow::setResizeCursorH() {
    [[NSCursor resizeLeftRightCursor] set];
}
void PlatformWindow::setResizeCursorV() {
    [[NSCursor resizeUpDownCursor] set];
}
void PlatformWindow::setDefaultCursor() {
    [[NSCursor arrowCursor] set];
}

// ============================================================================
// Stubs
// ============================================================================

void PlatformWindow::startupGdiplus() {}   // no-op on macOS

#endif // TARGET_OS_OSX
#endif // __APPLE__