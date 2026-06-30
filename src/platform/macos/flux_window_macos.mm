// flux_window_macos.mm
#ifdef __APPLE__
#include <TargetConditionals.h>
#if TARGET_OS_OSX

#import <Cocoa/Cocoa.h>
#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>
#import <QuartzCore/CAMetalLayer.h>
#import <AudioToolbox/AudioToolbox.h>

#include "flux/flux_window.hpp"
#include "flux/flux_canvas.hpp"
#include "flux/flux_window_macos_state.hpp"
#include "flux/flux_core.hpp"

#include <unordered_map>
#include <vector>

// ============================================================================
// Forward declarations
// ============================================================================

@class FluxNSView;
@class FluxAppDelegate;

// ============================================================================
// FluxNSView — bridges Cocoa events to PlatformWindow; renderFrame is the
// single paint entry point (replaces the old CG-based drawRect: pipeline).
// ============================================================================

@interface FluxNSView : NSView <NSWindowDelegate> {
    PlatformWindow* _win;
}
- (instancetype)initWithFrame:(NSRect)frame window:(PlatformWindow*)win;
- (void)renderFrame;
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

// Content now lives entirely in uiMetalLayer (and per-CanvasWidget Metal
// layers beneath it), which present independently of AppKit's CG drawing
// cycle. Nothing to draw here.
- (void)drawRect:(NSRect)dirtyRect { /* intentionally empty */ }

// ── Single paint entry point ────────────────────────────────────────────────
- (void)renderFrame {
    if (!_win || !_win->macState) return;
    auto& s = *_win->macState;
    if (!s.ensureUILayer(_win->cachedWidth, _win->cachedHeight)) return;

    id<CAMetalDrawable> drawable = [s.uiMetalLayer nextDrawable];
    if (!drawable) return;

    MTLRenderPassDescriptor* rpd = [MTLRenderPassDescriptor renderPassDescriptor];
    rpd.colorAttachments[0].texture     = drawable.texture;
    rpd.colorAttachments[0].loadAction  = MTLLoadActionClear;
    rpd.colorAttachments[0].clearColor  = MTLClearColorMake(0, 0, 0, 0); // transparent
    rpd.colorAttachments[0].storeAction = MTLStoreActionStore;

    id<MTLCommandBuffer> cmd = [s.commandQueue commandBuffer];
    id<MTLRenderCommandEncoder> enc = [cmd renderCommandEncoderWithDescriptor:rpd];

    GraphicsContext ctx;
    ctx.mtlDevice          = (__bridge void*)s.metalDevice;
    ctx.mtlEncoder         = (__bridge void*)enc;
    ctx.width              = _win->cachedWidth;
    ctx.height             = _win->cachedHeight;
    ctx.textScratchProvider = &s;

    // Orthographic projection: pixel space (top-left origin) -> clip space.
    float l = 0, r = (float)ctx.width, t = 0, b = (float)ctx.height;
    memset(ctx.mvp, 0, sizeof(ctx.mvp));
    ctx.mvp[0]  =  2.f / (r - l);
    ctx.mvp[5]  =  2.f / (t - b);
    ctx.mvp[10] = -1.f;
    ctx.mvp[12] = -(r + l) / (r - l);
    ctx.mvp[13] = -(b + t) / (b - t);
    ctx.mvp[15] =  1.f;

    if (_win->callbacks.onPaint)
        _win->callbacks.onPaint(ctx, ctx.width, ctx.height);

    [enc endEncoding];
    [cmd presentDrawable:drawable];
    [cmd commit];
}

// ── Resize ───────────────────────────────────────────────────────────────────
- (void)setFrameSize:(NSSize)newSize {
    [super setFrameSize:newSize];
    if (!_win || !_win->macState) return;

    NSRect backing = [self convertRectToBacking:self.bounds];
    _win->cachedWidth  = (int)backing.size.width;
    _win->cachedHeight = (int)backing.size.height;

    auto& s = *_win->macState;
    s.ensureUILayer(_win->cachedWidth, _win->cachedHeight); // resize uiMetalLayer + (re)gate atlas init

    int lw = (int)newSize.width;
    int lh = (int)newSize.height;

    for (CanvasWidget* cw : s.canvasWidgets)
        if (cw) cw->onWindowResize(_win->cachedWidth, _win->cachedHeight);

    if (_win->callbacks.onResize) {
        GraphicsContext ctx; // layout-only context: no mtlEncoder, Painter calls no-op safely
        ctx.width = lw;
        ctx.height = lh;
        ctx.textScratchProvider = &s;
        _win->callbacks.onResize(ctx, lw, lh);
    }

    [self renderFrame];
}

// ── Mouse events ─────────────────────────────────────────────────────────────

- (NSPoint)fluxPoint:(NSEvent*)event {
    NSPoint p = [self convertPoint:event.locationInWindow fromView:nil];
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
        [self renderFrame]; return;
    }
    s.focusedCanvas = nullptr;
    if (_win->callbacks.onMouseDown && _win->callbacks.onMouseDown(mx, my))
        [self renderFrame];
}

- (void)mouseUp:(NSEvent*)event {
    if (!_win) return;
    NSPoint p = [self fluxPoint:event];
    int mx = (int)p.x, my = (int)p.y;

    auto& s = *_win->macState;
    CanvasWidget* cw = s.capturedCanvas ? s.capturedCanvas : _win->hitTestCanvas(mx, my);
    s.capturedCanvas = nullptr;
    if (cw) { cw->onMouseButtonUp(mx - cw->x, my - cw->y, 0); [self renderFrame]; return; }
    if (_win->callbacks.onMouseUp && _win->callbacks.onMouseUp(mx, my))
        [self renderFrame];
}

- (void)mouseDragged:(NSEvent*)event { [self mouseMoved:event]; }

- (void)mouseMoved:(NSEvent*)event {
    if (!_win) return;
    NSPoint p = [self fluxPoint:event];
    int mx = (int)p.x, my = (int)p.y;

    auto& s = *_win->macState;
    CanvasWidget* cw = s.capturedCanvas ? s.capturedCanvas : _win->hitTestCanvas(mx, my);
    if (cw) { cw->onMouseMove(mx - cw->x, my - cw->y); [self renderFrame]; return; }
    if (_win->callbacks.onMouseMove && _win->callbacks.onMouseMove(mx, my))
        [self renderFrame];
}

- (void)rightMouseDown:(NSEvent*)event {
    if (!_win) return;
    NSPoint p = [self fluxPoint:event];
    if (_win->callbacks.onRightClick &&
        _win->callbacks.onRightClick((int)p.x, (int)p.y))
        [self renderFrame];
}

- (void)scrollWheel:(NSEvent*)event {
    if (!_win) return;
    int delta = (int)(event.scrollingDeltaY * WHEEL_DELTA);
    if (_win->callbacks.onMouseWheel && _win->callbacks.onMouseWheel(delta))
        [self renderFrame];
}

- (void)mouseExited:(NSEvent*)event {
    if (!_win) return;
    auto& s = *_win->macState;
    s.capturedCanvas = nullptr;
    for (CanvasWidget* cw : s.canvasWidgets)
        if (cw) cw->onMouseLeave();
    if (_win->callbacks.onMouseLeave) _win->callbacks.onMouseLeave();
    [self renderFrame];
}

// ── Keyboard events ───────────────────────────────────────────────────────────

- (void)keyDown:(NSEvent*)event {
    if (!_win) return;
    auto& s = *_win->macState;

    if (s.focusedCanvas) {
        s.focusedCanvas->onKeyDown((int)event.keyCode);
        [self renderFrame];
        return;
    }

    bool needsRedraw = false;
    if (_win->callbacks.onKeyDown && _win->callbacks.onKeyDown((int)event.keyCode))
        needsRedraw = true;

    NSString* chars = event.characters;
    if (chars.length > 0 && _win->callbacks.onChar) {
        wchar_t ch = (wchar_t)[chars characterAtIndex:0];
        if (ch >= 32 && _win->callbacks.onChar(ch))
            needsRedraw = true;
    }
    if (needsRedraw) [self renderFrame];
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
    [self renderFrame];
}

@end

// ============================================================================
// Timer shim
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

    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];

    s.metalDevice = MTLCreateSystemDefaultDevice();
    if (!s.metalDevice) {
        fprintf(stderr, "PlatformWindow: no Metal device\n");
        return false;
    }

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

    s.nsView = [[FluxNSView alloc] initWithFrame:frame window:this];
    [s.nsWindow setContentView:s.nsView];
    [s.nsWindow setDelegate:s.nsView];
    [s.nsWindow makeFirstResponder:s.nsView];

    [s.nsView addTrackingArea:[[NSTrackingArea alloc]
        initWithRect:frame
             options:NSTrackingMouseMoved |
                     NSTrackingMouseEnteredAndExited |
                     NSTrackingActiveInKeyWindow |
                     NSTrackingInVisibleRect
               owner:s.nsView
            userInfo:nil]];

    // ── Physical / logical sizes ──────────────────────────────────────────────
    NSRect backing = [s.nsView convertRectToBacking:s.nsView.bounds];
    cachedWidth  = (int)backing.size.width;
    cachedHeight = (int)backing.size.height;

    // ── Call site #1: initial UI Metal layer + glyph atlas + canvas backend ──
    // ensureUILayer() also lazily creates s.canvasMetal / s.canvasBackend
    // via ensureCanvasBackend() internally (see flux_window_macos_state.hpp).
    if (!s.ensureUILayer(cachedWidth, cachedHeight)) {
        fprintf(stderr, "PlatformWindow: ensureUILayer failed at create()\n");
        return false;
    }

    // ── Canvas2D Metal backend (shared, per-window) — fonts registered once ──
    if (s.canvasBackend) {
        auto regFont = [&](const char* name, const char* path) {
            if (!Canvas2D::registerFont(s.canvasBackend, name, path))
                fprintf(stderr, "PlatformWindow: font '%s' not found at '%s'\n", name, path);
        };
        regFont("sans",      "/System/Library/Fonts/Helvetica.ttc");
        regFont("sans-bold", "/System/Library/Fonts/Helvetica.ttc");
        regFont("mono",      "/System/Library/Fonts/Menlo.ttc");
    } else {
        fprintf(stderr, "PlatformWindow: canvasBackend unavailable, fonts not registered\n");
    }

    for (CanvasWidget* cw : s.canvasWidgets)
        if (cw && s.canvasBackend) cw->setCanvasBackend(s.canvasBackend);

    [s.nsWindow makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];

    s.running = true;
    return true;
}

void PlatformWindow::destroy() {
    if (!macState) return;
    auto& s = *macState;

    for (auto& [id, timer] : s.timerMap)
        [timer invalidate];
    s.timerMap.clear();

    if (s.nsWindow) { [s.nsWindow close]; s.nsWindow = nil; }

    // canvasBackend/canvasMetal teardown happens in ~MacState() via
    // teardownCanvasBackend() — no explicit call needed here.
    delete macState;
    macState = nullptr;
}

int PlatformWindow::run() {
    [NSApp run];
    return 0;
}

NativeWindow PlatformWindow::handle() const {
    return macState ? (__bridge NativeWindow)(macState->nsWindow) : nullptr;
}
bool PlatformWindow::isMouseCaptured() const {
    return macState && macState->mouseCapture;
}

// ============================================================================
// invalidate / invalidateRect
//
// Metal redraws the full frame each pass (no partial-surface presentation),
// so invalidateRect collapses to invalidate. x/y/w/h kept for signature
// compatibility with callers (e.g. FluxUI::invalidateWidget) but unused.
// ============================================================================

void PlatformWindow::invalidate() {
    if (!macState) return;
    macState->dirty = true;
    dispatch_async(dispatch_get_main_queue(), ^{
        if (auto *ui = FluxUI::getCurrentInstance())
            ui->drainPendingRebuilds();
        if (macState && macState->nsView)
            [macState->nsView renderFrame];
    });
}

void PlatformWindow::invalidateRect(int /*x*/, int /*y*/, int /*w*/, int /*h*/) {
    invalidate();
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
    // ── Call site #3: keep uiMetalLayer's drawable size in sync if this is
    // ever invoked outside of setFrameSize: (e.g. from a DPI-change path).
    if (macState) macState->ensureUILayer(cachedWidth, cachedHeight);
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
// GraphicsContext for measure — layout-only, no Metal encoder needed.
// CoreText measurement (FontCache / Painter::measureText) doesn't touch
// any field here beyond what's already populated.
// ============================================================================

GraphicsContext PlatformWindow::getMeasureContext() const {
    GraphicsContext ctx;
    if (macState) {
        ctx.width  = cachedWidth;
        ctx.height = cachedHeight;
        ctx.textScratchProvider = macState;
    }
    return ctx;
}

bool PlatformWindow::valid() const {
    return macState != nullptr && macState->nsWindow != nil;
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

void PlatformWindow::setResizeCursorH() { [[NSCursor resizeLeftRightCursor] set]; }
void PlatformWindow::setResizeCursorV() { [[NSCursor resizeUpDownCursor] set]; }
void PlatformWindow::setDefaultCursor() { [[NSCursor arrowCursor] set]; }



#endif // TARGET_OS_OSX
#endif // __APPLE__