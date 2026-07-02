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
#include "flux/flux_glyph_atlas_macos.hpp" 
#include "flux/flux_core.hpp"
#include "flux/flux_keys.hpp"

#include <unordered_map>
#include <vector>

// ============================================================================
// FluxNSView — declared here (full @interface, not just a forward @class) so
// that PlatformWindow::MacState below can use NSView properties like .layer
// and .bounds on it. A bare `@class FluxNSView;` forward declaration only
// tells the compiler a type by this name exists, not that it inherits from
// NSView, so those properties would be invisible to MacState::ensureUILayer().
// The @implementation block stays further down, after MacState is defined,
// since FluxNSView's methods (renderFrame, event handlers) need the full
// MacState definition to reach _win->macState.
// ============================================================================

@interface FluxNSView : NSView <NSWindowDelegate> {
    PlatformWindow* _win;
}
- (instancetype)initWithFrame:(NSRect)frame window:(PlatformWindow*)win;
- (void)renderFrame;
@end

// ============================================================================
// PlatformWindow::MacState
// Window-lifetime resources: Cocoa window/view, shared Metal device/queue,
// UI glyph atlas, timers. Defined ONLY in this file — same pattern as
// PlatformWindow::CairoState in flux_window_linux.cpp. Other .mm files
// (painter, canvas) reach the pieces they need through GraphicsContext
// fields or the narrow getMacMetalDevicePtr()/getMacNSViewPtr() accessors
// below, never through this struct directly.
// ============================================================================
struct PlatformWindow::MacState
{
    NSWindow   *nsWindow = nil;
    FluxNSView *nsView   = nil;

    id<MTLDevice>       metalDevice  = nil;
    id<MTLCommandQueue> commandQueue = nil;
    CAMetalLayer        *uiMetalLayer = nil;

    GlyphAtlas glyphAtlas;
    bool glyphAtlasReady = false;

    std::unordered_map<TimerID, NSTimer *> timerMap;

    bool running      = false;
    bool dirty        = false;
    bool mouseCapture = false;

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
            uiMetalLayer.opaque          = NO;
            [nsView.layer addSublayer:uiMetalLayer];
            uiMetalLayer.zPosition = 1.0;
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

@implementation FluxNSView

- (instancetype)initWithFrame:(NSRect)frame window:(PlatformWindow*)win {
    self = [super initWithFrame:frame];
    if (self) { _win = win; self.wantsLayer = YES; }
    return self;
}

- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)isOpaque              { return YES; }
- (void)drawRect:(NSRect)dirtyRect { /* intentionally empty — Metal renders */ }

// ── Single paint entry point ─────────────────────────────────────────────────
// Renders the UI Metal layer (Painter's vector + text content).
// Each CanvasWidget renders to its own separate CAMetalLayer (zPosition 0)
// from within its render() override — no loop over widgets here.
- (void)renderFrame {
    if (!_win || !_win->macState) return;
    auto& s = *_win->macState;
    if (!s.ensureUILayer(_win->cachedWidth, _win->cachedHeight)) return;

    id<CAMetalDrawable> drawable = [s.uiMetalLayer nextDrawable];
    if (!drawable) return;

    MTLRenderPassDescriptor* rpd = [MTLRenderPassDescriptor renderPassDescriptor];
    rpd.colorAttachments[0].texture     = drawable.texture;
    rpd.colorAttachments[0].loadAction  = MTLLoadActionClear;
    rpd.colorAttachments[0].clearColor  = MTLClearColorMake(0, 0, 0, 0);
    rpd.colorAttachments[0].storeAction = MTLStoreActionStore;

    id<MTLCommandBuffer> cmd = [s.commandQueue commandBuffer];
    id<MTLRenderCommandEncoder> enc = [cmd renderCommandEncoderWithDescriptor:rpd];

    GraphicsContext ctx;
    ctx.mtlDevice           = (__bridge void*)s.metalDevice;
    ctx.mtlEncoder          = (__bridge void*)enc;
    ctx.width               = _win->cachedWidth;
    ctx.height              = _win->cachedHeight;
    ctx.glyphAtlas = s.glyphAtlasReady ? &s.glyphAtlas : nullptr;

    float l = 0, r = (float)ctx.width, t = 0, b = (float)ctx.height;
    memset(ctx.mvp, 0, sizeof(ctx.mvp));
    ctx.mvp[0]  =  2.f / (r - l);
    ctx.mvp[5]  =  2.f / (t - b);
    ctx.mvp[10] = -1.f;
    ctx.mvp[12] = -(r + l) / (r - l);
    ctx.mvp[13] = -(b + t) / (b - t);
    ctx.mvp[15] =  1.f;

    // onPaint walks the full widget tree. CanvasWidget::render() draws to its
    // own CAMetalLayer inline; no separate pre/glRenderPass loop needed.
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
    s.ensureUILayer(_win->cachedWidth, _win->cachedHeight);

    int lw = (int)newSize.width;
    int lh = (int)newSize.height;

    // No per-widget canvas resize loop — each CanvasWidget detects size
    // changes itself via syncPhysicalGeometry() in its next render() call,
    // the same way every other widget detects layout changes.

    if (_win->callbacks.onResize) {
        GraphicsContext ctx;
        ctx.width  = lw;
        ctx.height = lh;
        ctx.glyphAtlas = s.glyphAtlasReady ? &s.glyphAtlas : nullptr;
        _win->callbacks.onResize(ctx, lw, lh);
    }

    [self renderFrame];
}

// ── Coordinate helper ─────────────────────────────────────────────────────────
- (NSPoint)fluxPoint:(NSEvent*)event {
    NSPoint p = [self convertPoint:event.locationInWindow fromView:nil];
    return NSMakePoint(p.x, self.bounds.size.height - p.y);
}

// ── Mouse events — all go straight to generic callbacks ──────────────────────
// The widget tree dispatch in flux_core.cpp (findAndHandleMouseEvent /
// broadcastMouseEvent) routes to CanvasWidget::handleMouseDown/Move/Up
// exactly as it does for every other Widget type on every platform.

- (void)mouseDown:(NSEvent*)event {
    if (!_win) return;
    [self becomeFirstResponder];
    NSPoint p = [self fluxPoint:event];
    if (_win->callbacks.onMouseDown &&
        _win->callbacks.onMouseDown((int)p.x, (int)p.y))
        [self renderFrame];
}

- (void)mouseUp:(NSEvent*)event {
    if (!_win) return;
    NSPoint p = [self fluxPoint:event];
    if (_win->callbacks.onMouseUp &&
        _win->callbacks.onMouseUp((int)p.x, (int)p.y))
        [self renderFrame];
}

- (void)mouseDragged:(NSEvent*)event { [self mouseMoved:event]; }

- (void)mouseMoved:(NSEvent*)event {
    if (!_win) return;
    NSPoint p = [self fluxPoint:event];
    if (_win->callbacks.onMouseMove &&
        _win->callbacks.onMouseMove((int)p.x, (int)p.y))
        [self renderFrame];
}

- (void)rightMouseDown:(NSEvent*)event {
    if (!_win) return;
    NSPoint p = [self fluxPoint:event];
    if (_win->callbacks.onRightClick &&
        _win->callbacks.onRightClick((int)p.x, (int)p.y))
        [self renderFrame];
}

- (void)otherMouseDown:(NSEvent*)event {
    // Middle-button pan is handled via space+drag through the normal
    // handleMouseDown override in CanvasWidget (same as Linux/Win32).
    // We still forward as a generic mouseDown so the widget can respond.
    if (!_win || event.buttonNumber != 2) return;
    NSPoint p = [self fluxPoint:event];
    if (_win->callbacks.onMouseDown &&
        _win->callbacks.onMouseDown((int)p.x, (int)p.y))
        [self renderFrame];
}

- (void)otherMouseUp:(NSEvent*)event {
    if (!_win || event.buttonNumber != 2) return;
    NSPoint p = [self fluxPoint:event];
    if (_win->callbacks.onMouseUp &&
        _win->callbacks.onMouseUp((int)p.x, (int)p.y))
        [self renderFrame];
}

- (void)otherMouseDragged:(NSEvent*)event { [self mouseMoved:event]; }

- (void)scrollWheel:(NSEvent*)event {
    if (!_win) return;
    // Convert to the same WHEEL_DELTA integer convention every other platform
    // uses; CanvasWidget::handleMouseWheel receives it via the generic
    // focusedWidget path in FluxUI::wireCallbacks.
    float deltaY = (float)event.scrollingDeltaY;
    int delta = (int)(deltaY * WHEEL_DELTA);
    if (_win->callbacks.onMouseWheel && _win->callbacks.onMouseWheel(delta))
        [self renderFrame];
}

- (void)mouseExited:(NSEvent*)event {
    if (!_win) return;
    // Generic mouse-leave: FluxUI's onMouseLeave clears hover states on the
    // whole widget tree. CanvasWidget handles its own scrollbar/pan cleanup
    // via handleMouseLeave() override.
    if (_win->callbacks.onMouseLeave)
        _win->callbacks.onMouseLeave();
    [self renderFrame];
}

// ── Keyboard ──────────────────────────────────────────────────────────────────
// Routes through the normal focusedWidget path — CanvasWidget sets
// isFocusable=true and FluxUI::setFocus() picks it up on mouseDown.

- (void)keyDown:(NSEvent*)event {
    if (!_win) return;

    if (event.keyCode == Key::Space)
        flux_mac_detail::g_spaceDown = true;

    bool needsRedraw = false;
    if (_win->callbacks.onKeyDown &&
        _win->callbacks.onKeyDown((int)event.keyCode))
        needsRedraw = true;

    NSString* chars = event.characters;
    if (chars.length > 0 && _win->callbacks.onChar) {
        wchar_t ch = (wchar_t)[chars characterAtIndex:0];
        if (ch >= 32 && _win->callbacks.onChar(ch))
            needsRedraw = true;
    }
    if (needsRedraw) [self renderFrame];
}



// No onKeyUp in WindowCallbacks — this exists solely to clear the
// space-drag-pan flag set above. Widget-level key-up handling isn't
// wired anywhere else on any platform, so nothing else needs to change.
- (void)keyUp:(NSEvent*)event {
    if (event.keyCode == Key::Space)
        flux_mac_detail::g_spaceDown = false;
}

// Ctrl/Shift/Option arrive only here on macOS, never via keyDown/keyUp.
- (void)flagsChanged:(NSEvent*)event {
    NSEventModifierFlags flags = event.modifierFlags;
    flux_mac_detail::g_ctrlDown  = (flags & NSEventModifierFlagControl) != 0;
    flux_mac_detail::g_shiftDown = (flags & NSEventModifierFlagShift)   != 0;
    flux_mac_detail::g_altDown   = (flags & NSEventModifierFlagOption)  != 0;
}

// ── NSWindowDelegate ──────────────────────────────────────────────────────────

- (void)windowWillClose:(NSNotification*)notification {
    if (_win) _win->macState->running = false;
    [NSApp stop:nil];
}

- (void)windowDidResignKey:(NSNotification*)notification {
    if (!_win) return;
    // No canvas-specific state to clear — capturedCanvas/focusedCanvas are
    // gone. FluxUI's generic onFocusLost clears focusedWidget.
    if (_win->callbacks.onFocusLost)
        _win->callbacks.onFocusLost();
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

    NSRect backing = [s.nsView convertRectToBacking:s.nsView.bounds];
    cachedWidth  = (int)backing.size.width;
    cachedHeight = (int)backing.size.height;

    // ensureUILayer sets up the UI Metal layer and glyph atlas.
    // No canvas backend registered here — each CanvasWidget creates and
    // registers its own fonts lazily on first render(), same as Win32.
    if (!s.ensureUILayer(cachedWidth, cachedHeight)) {
        fprintf(stderr, "PlatformWindow: ensureUILayer failed at create()\n");
        return false;
    }

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

    delete macState;
    macState = nullptr;
}

void PlatformWindow::startRenderLoop() {}

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
// Size helpers
// ============================================================================

void PlatformWindow::updateClientSize() {
    if (!macState || !macState->nsView) return;
    NSRect backing = [macState->nsView convertRectToBacking:macState->nsView.bounds];
    cachedWidth  = (int)backing.size.width;
    cachedHeight = (int)backing.size.height;
    if (macState) macState->ensureUILayer(cachedWidth, cachedHeight);
}

PlatformWindow::ClientSize PlatformWindow::getClientSize() const {
    if (!macState || !macState->nsView) return {0, 0};
    NSRect b = macState->nsView.bounds;
    return {(int)b.size.width, (int)b.size.height};
}

PlatformWindow::ScreenPoint PlatformWindow::clientToScreen(int cx, int cy) const {
    if (!macState || !macState->nsWindow) return {cx, cy};
    NSRect r = [macState->nsWindow convertRectToScreen:NSMakeRect(cx, cy, 1, 1)];
    return {(int)r.origin.x, (int)r.origin.y};
}

PlatformWindow::ScreenPoint PlatformWindow::screenToClient(int sx, int sy) const {
    if (!macState || !macState->nsWindow) return {sx, sy};
    NSRect r = [macState->nsWindow convertRectFromScreen:NSMakeRect(sx, sy, 1, 1)];
    return {(int)r.origin.x, (int)r.origin.y};
}

GraphicsContext PlatformWindow::getMeasureContext() const {
    GraphicsContext ctx;
    if (macState) {
        ctx.width  = cachedWidth;
        ctx.height = cachedHeight;
        ctx.glyphAtlas = macState->glyphAtlasReady ? &macState->glyphAtlas : nullptr;
    }
    return ctx; 
}

// ============================================================================
// Narrow accessors — let flux_canvas_macos.mm reach the shared Metal device
// and the Cocoa view without needing MacState's full definition.
// ============================================================================

void *PlatformWindow::getMacMetalDevicePtr() const {
    return macState ? (__bridge void *)macState->metalDevice : nullptr;
}

void *PlatformWindow::getMacNSViewPtr() const {
    return macState ? (__bridge void *)macState->nsView : nullptr;
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