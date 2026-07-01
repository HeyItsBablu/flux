// flux_canvas_macos.mm
#ifdef __APPLE__
#include <TargetConditionals.h>
#if TARGET_OS_OSX

#import <Cocoa/Cocoa.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include "flux/flux_canvas2d.hpp"
#include "flux/flux_window.hpp"
#include "flux/flux_canvas.hpp"
#include "flux/flux_painter.hpp"
#include <cassert>
#include <unordered_map>

// ── Canvas2DBackend lifecycle — defined in flux_canvas2d_metal.mm.
// No shared header: this is the entire contract between the two files.
extern Canvas2DBackend *Canvas2DBackend_create(id<MTLDevice> device);
extern void Canvas2DBackend_destroy(Canvas2DBackend *b);
extern void Canvas2DBackend_metalBeginFrame(Canvas2DBackend *b, id<MTLDevice> device,
                                            id<MTLRenderCommandEncoder> encoder,
                                            const float mvp[16]);
extern void Canvas2DBackend_metalEndFrame(Canvas2DBackend *b);

// ============================================================================
// macOS per-widget state
// ============================================================================

struct MacCanvasState
{
    bool initialized    = false;
    bool repaintPending = false;
    int  lastW          = 0;
    int  lastH          = 0;
    int  physX          = 0;
    int  physY          = 0;

    // ── Metal resources (per-widget) ─────────────────────────────────────────
    id<MTLDevice>       device    = nil;
    id<MTLCommandQueue> cmdQueue  = nil;
    CAMetalLayer        *metalLayer = nil;

    // ── Canvas2D backend (per-widget, owned) ─────────────────────────────────
    // Created in initCanvas, destroyed in destroyCanvas. Not shared —
    // each widget registers its own fonts, same as Win32's ensureD2D().
    Canvas2DBackend *backend = nullptr;
};

static std::unordered_map<CanvasWidget *, MacCanvasState> s_macState;
static MacCanvasState &macState(CanvasWidget *w) { return s_macState[w]; }

// ============================================================================
// Helpers
// ============================================================================

static void scheduleRepaint(CanvasWidget *w)
{
    if (w->onViewportChanged)
        w->onViewportChanged(w->vp_.zoom());
    auto &s = macState(w);
    if (!s.repaintPending)
    {
        s.repaintPending = true;
        dispatch_async(dispatch_get_main_queue(), ^{
            auto *inst = FluxUI::getCurrentInstance();
            if (!inst) return;
            auto *pw = inst->getPlatformWindowPtr();
            if (pw) pw->invalidate();
        });
    }
}

static float getDpiScale()
{
    NSScreen *scr = [NSScreen mainScreen];
    return scr ? (float)scr.backingScaleFactor : 1.f;
}

static int getWindowPhysW()
{
    auto *inst = FluxUI::getCurrentInstance();
    if (!inst) return 1;
    auto *pw = inst->getPlatformWindowPtr();
    return pw ? pw->clientWidth() : 1;
}
static int getWindowPhysH()
{
    auto *inst = FluxUI::getCurrentInstance();
    if (!inst) return 1;
    auto *pw = inst->getPlatformWindowPtr();
    return pw ? pw->clientHeight() : 1;
}

static void metalOrtho(float l, float r, float b, float t, float out[16])
{
    memset(out, 0, 64);
    out[0]  =  2.f / (r - l);
    out[5]  =  2.f / (t - b);
    out[10] = -1.f;
    out[12] = -(r + l) / (r - l);
    out[13] = -(t + b) / (t - b);
    out[15] =  1.f;
}

// ============================================================================
// initCanvas — creates per-widget Metal resources and Canvas2D backend,
// registers fonts. Mirroring ensureD2D() in flux_canvas_win32.cpp.
// ============================================================================

static void initCanvas(CanvasWidget *w)
{
    auto &s = macState(w);
    if (s.initialized) return;

    auto *inst = FluxUI::getCurrentInstance(); 
    auto *pw   = inst ? inst->getPlatformWindowPtr() : nullptr;
    if (!pw) return;

    id<MTLDevice> device = (__bridge id<MTLDevice>)pw->getMacMetalDevicePtr();
    NSView       *nsView = (__bridge NSView *)pw->getMacNSViewPtr();
    if (!device || !nsView) return;

    // ── Metal resources ───────────────────────────────────────────────────────
    s.device   = device; // borrowed reference — device is window-lifetime
    s.cmdQueue = [s.device newCommandQueue];

    s.metalLayer                  = [CAMetalLayer layer];
    s.metalLayer.device           = s.device;
    s.metalLayer.pixelFormat      = MTLPixelFormatBGRA8Unorm;
    s.metalLayer.framebufferOnly  = YES;
    s.metalLayer.zPosition        = 0.0; // beneath uiMetalLayer (zPosition 1.0)
    [nsView.layer insertSublayer:s.metalLayer atIndex:0];

    // ── Canvas2D backend (per-widget, owned) ─────────────────────────────────
    s.backend = Canvas2DBackend_create(s.device);
    if (!s.backend) return;

    // Font registration — one set per widget, same as Win32's ensureD2D().
    auto reg = [&](const char *name, const char *path)
    {
        Canvas2D::registerFont(s.backend, name, path);
    };
    reg("sans",      "/System/Library/Fonts/Helvetica.ttc");
    reg("sans-bold", "/System/Library/Fonts/Helvetica.ttc");
    reg("mono",      "/System/Library/Fonts/Menlo.ttc");

    w->backend_ = s.backend;

    // ── Viewport / surface init ───────────────────────────────────────────────
    float dpi = getDpiScale();
    s.lastW = (w->width  > 0) ? (int)(w->width  * dpi) : 1;
    s.lastH = (w->height > 0) ? (int)(w->height * dpi) : 1;
    s.physX = (int)(w->x * dpi);
    s.physY = (int)(w->y * dpi);
    s.metalLayer.drawableSize = CGSizeMake(s.lastW, s.lastH);

    w->canvasW_ = s.lastW;
    w->canvasH_ = s.lastH;

    w->vp_.init(s.lastW, s.lastH, w->canvasW_, w->canvasH_);
    w->updateViewportSize(s.lastW, s.lastH);
    w->updateSBGeometry(s.lastW, s.lastH);
    w->vp_.fitToView();
    w->activatePendingSurface();
    w->lastTick_ = CanvasWidget::Clock::now();
    w->frameDt_  = 0.0;

    s.initialized = true;

    if (w->onViewportChanged) w->onViewportChanged(w->vp_.zoom());
    if (w->onGLResize)        w->onGLResize(s.lastW, s.lastH);
}

// ============================================================================
// destroyCanvas — tears down per-widget Metal resources and backend.
// Mirrors destroyD2D() in flux_canvas_win32.cpp.
// ============================================================================

static void destroyCanvas(CanvasWidget *w)
{
    auto it = s_macState.find(w);
    if (it == s_macState.end()) return;
    auto &s = it->second;

    if (w->activeSurface_) { w->activeSurface_->destroy(); w->activeSurface_.reset(); }
    w->pendingSurface_.reset();
    w->backend_ = nullptr;

    if (s.backend) { Canvas2DBackend_destroy(s.backend); s.backend = nullptr; }

    s.cmdQueue   = nil;
    s.metalLayer = nil;
    s.device     = nil;

    s_macState.erase(it);
}

// ============================================================================
// syncPhysicalGeometry — detect size/position changes from the widget's
// own layout fields. Called every render(), same pattern as Linux.
// ============================================================================

static void syncPhysicalGeometry(CanvasWidget *w)
{
    auto &s = macState(w);
    float dpi    = getDpiScale();
    int newPhysW = (int)(w->width  * dpi);
    int newPhysH = (int)(w->height * dpi);
    int newPhysX = (int)(w->x      * dpi);
    int newPhysY = (int)(w->y      * dpi);

    s.physX = newPhysX;
    s.physY = newPhysY;

    if (newPhysW == s.lastW && newPhysH == s.lastH) return;
    s.lastW = newPhysW;
    s.lastH = newPhysH;

    s.metalLayer.drawableSize = CGSizeMake(
        s.lastW < 1 ? 1 : s.lastW,
        s.lastH < 1 ? 1 : s.lastH);

    w->canvasW_ = newPhysW;
    w->canvasH_ = newPhysH;
    w->vp_.setCanvasSize(newPhysW, newPhysH);
    w->updateViewportSize(s.lastW, s.lastH);
    w->updateSBGeometry(s.lastW, s.lastH);
    if (w->activeSurface_) w->activeSurface_->resize(newPhysW, newPhysH);
    if (w->onGLResize)     w->onGLResize(s.lastW, s.lastH);
    w->vp_.fitToView();
}

// ============================================================================
// CanvasWidget member implementations (macOS)
// ============================================================================

CanvasWidget::CanvasWidget()
    : hBar_(CustomScrollbar::Axis::Horizontal)
    , vBar_(CustomScrollbar::Axis::Vertical)
{
    autoWidth  = true;
    autoHeight = true;
    width  = 400;
    height = 300;
    isFocusable = true; // focus flows through FluxUI's normal Widget system
}

CanvasWidget::~CanvasWidget()
{
    destroyCanvas(this);
}

std::shared_ptr<CanvasWidget> CanvasWidget::setViewportEnabled(bool e)
    { viewportEnabled_ = e; return ptr(); }

std::shared_ptr<CanvasWidget> CanvasWidget::setScrollbarsEnabled(bool e)
{
    scrollbarsEnabled_ = e;
    auto &s = macState(this);
    if (s.initialized) { updateViewportSize(s.lastW, s.lastH); scheduleRepaint(this); }
    return ptr();
}
bool CanvasWidget::scrollbarsEnabled() const { return scrollbarsEnabled_; }

RenderSurface  *CanvasWidget::getSurface() const { return activeSurface_.get(); }
const Viewport &CanvasWidget::viewport()   const { return vp_; }
Viewport       &CanvasWidget::viewport()         { return vp_; }

std::shared_ptr<CanvasWidget> CanvasWidget::setSize(int w, int h)
{
    width = w; height = h;
    autoWidth = autoHeight = false;
    markNeedsLayout();
    return ptr();
}

std::shared_ptr<CanvasWidget> CanvasWidget::setCanvasSize(int w, int h)
{
    float dpi = getDpiScale();
    canvasW_ = (int)(w * dpi);
    canvasH_ = (int)(h * dpi);
    auto &s = macState(this);
    if (s.initialized)
    {
        vp_.setCanvasSize(canvasW_, canvasH_);
        if (activeSurface_) activeSurface_->resize(canvasW_, canvasH_);
    }
    return ptr();
}

std::shared_ptr<CanvasWidget> CanvasWidget::redraw()
    { markNeedsPaint(); return ptr(); }

void CanvasWidget::computeLayout(GraphicsContext & /*ctx*/,
                                 const BoxConstraints &c, FontCache &)
{
    if (autoWidth)  width  = (c.maxWidth  < kUnbounded) ? c.maxWidth  : width;
    if (autoHeight) height = (c.maxHeight < kUnbounded) ? c.maxHeight : height;
    needsLayout = false;
    scheduleRepaint(this);
}

// render() does everything inline — lazy init, geometry sync, tick, and
// draw to the widget's own CAMetalLayer.
void CanvasWidget::render(GraphicsContext & /*ctx*/, FontCache &)
{
    auto &s = macState(this);

    if (!s.initialized)
        initCanvas(this);
    if (!s.initialized) { needsPaint = false; return; }

    syncPhysicalGeometry(this);

    if (pendingSurface_) activatePendingSurface();
    if (!activeSurface_) { needsPaint = false; return; }

    // ── tick ──
    auto now = Clock::now();
    frameDt_  = std::chrono::duration<double>(now - lastTick_).count();
    lastTick_ = now;
    activeSurface_->update(frameDt_);
    activeSurface_->preRender();

    // ── draw ──
    s.repaintPending = false;

    int glW = s.lastW < 1 ? 1 : s.lastW;
    int glH = s.lastH < 1 ? 1 : s.lastH;

    auto drawable = [s.metalLayer nextDrawable];
    if (!drawable) { needsPaint = false; return; }

    float winW = (float)getWindowPhysW();
    float winH = (float)getWindowPhysH();
    float z    = vp_.zoom();
    float ox   = -vp_.offsetX() * z + (float)s.physX;
    float oy   = -vp_.offsetY() * z + (float)s.physY;

    float ortho[16];
    metalOrtho(0.f, winW, winH, 0.f, ortho);

    float vpMat[16] = {
        z,  0,  0, 0,
        0,  z,  0, 0,
        0,  0,  1, 0,
        ox, oy, 0, 1
    };
    float mvp[16] = {};
    for (int col = 0; col < 4; ++col)
        for (int row = 0; row < 4; ++row)
            for (int k = 0; k < 4; ++k)
                mvp[col*4+row] += ortho[k*4+row] * vpMat[col*4+k];

    MTLRenderPassDescriptor *rpd = [MTLRenderPassDescriptor renderPassDescriptor];
    rpd.colorAttachments[0].texture     = drawable.texture;
    rpd.colorAttachments[0].loadAction  = MTLLoadActionClear;
    rpd.colorAttachments[0].clearColor  = MTLClearColorMake(0, 0, 0, 0);
    rpd.colorAttachments[0].storeAction = MTLStoreActionStore;

    auto cmd = [s.cmdQueue commandBuffer];
    auto enc = [cmd renderCommandEncoderWithDescriptor:rpd];

    MTLScissorRect sci;
    sci.x      = (NSUInteger)s.physX;
    sci.y      = (NSUInteger)s.physY;
    sci.width  = (NSUInteger)glW;
    sci.height = (NSUInteger)glH;
    [enc setScissorRect:sci];

    if (s.backend)
    {
        Canvas2DBackend_metalBeginFrame(s.backend, s.device, enc, mvp);
        Canvas2D ctx2d(s.backend, canvasW_, canvasH_);
        activeSurface_->render(ctx2d);
        Canvas2DBackend_metalEndFrame(s.backend);
    }

    updateSBGeometry(glW, glH);
    hBar_.tick(frameDt_);
    vBar_.tick(frameDt_);

    {
        GraphicsContext gctx;
        gctx.mtlDevice  = (__bridge void*)s.device;
        gctx.mtlEncoder = (__bridge void*)enc;
        gctx.width  = glW;
        gctx.height = glH;
        memcpy(gctx.mvp, ortho, sizeof(ortho));

        Painter p(gctx);
        p.drawScrollbar(hBar_, glW, glH);
        p.drawScrollbar(vBar_, glW, glH);
    }

    [enc endEncoding];
    [cmd presentDrawable:drawable];
    [cmd commit];

    if (activeSurface_->needsContinuousRedraw())
        scheduleRepaint(this);

    needsPaint = false;
}

void CanvasWidget::onDetach()
{
    destroyCanvas(this);
    Widget::onDetach();
}

void CanvasWidget::markNeedsPaint()
{
    Widget::markNeedsPaint();
    scheduleRepaint(this);
}

void CanvasWidget::onWindowResize(int /*newW*/, int /*newH*/)
{
    auto &s = macState(this);
    if (!s.initialized) return;
    s.lastW = 0;
    s.lastH = 0;
    scheduleRepaint(this);
}

bool CanvasWidget::isInitialized() const
    { return macState(const_cast<CanvasWidget *>(this)).initialized; }
bool CanvasWidget::needsRepaint() const
    { return macState(const_cast<CanvasWidget *>(this)).repaintPending; }
bool CanvasWidget::containsPoint(int wx, int wy) const
    { return wx >= x && wx < (x + width) && wy >= y && wy < (y + height); }

// ============================================================================
// Input — unchanged from before
// ============================================================================

bool CanvasWidget::handleMouseDown(int mx, int my)
{
    int lx = mx - x, ly = my - y;

    if (auto *inst = FluxUI::getCurrentInstance())
        inst->captureMouseInput();

    bool hC = hBar_.onMouseDown(lx, ly, [this](float t) { applyHScrollFraction(t); });
    bool vC = !hC && vBar_.onMouseDown(lx, ly, [this](float t) { applyVScrollFraction(t); });

    if (!hC && !vC)
    {
        if (viewportEnabled_ && platformSpaceDown())
            beginPan(lx, ly);
        else if (activeSurface_)
        {
            auto [cx, cy] = vp_.screenToCanvas(float(lx), float(ly));
            activeSurface_->onMouseDown(cx, cy);
        }
    }
    scheduleRepaint(this);
    return true;
}

bool CanvasWidget::handleMouseMove(int mx, int my)
{
    int lx = mx - x, ly = my - y;

    bool hDrag = hBar_.onMouseMove(lx, ly);
    bool vDrag = vBar_.onMouseMove(lx, ly);
    scheduleRepaint(this);
    if (hDrag || vDrag) return true;

    if (panning_)
        continuePan(lx, ly);
    else if (activeSurface_)
    {
        auto [cx, cy] = vp_.screenToCanvas(float(lx), float(ly));
        activeSurface_->onMouseMove(cx, cy);
    }
    return true;
}

bool CanvasWidget::handleMouseUp(int mx, int my)
{
    int lx = mx - x, ly = my - y;

    if (auto *inst = FluxUI::getCurrentInstance())
        inst->releaseMouseInput();

    bool hR = hBar_.onMouseUp(lx, ly);
    bool vR = vBar_.onMouseUp(lx, ly);
    if (!hR && !vR)
    {
        if (panning_) panning_ = false;
        else if (activeSurface_)
        {
            auto [cx, cy] = vp_.screenToCanvas(float(lx), float(ly));
            activeSurface_->onMouseUp(cx, cy);
        }
    }
    scheduleRepaint(this);
    return true;
}

bool CanvasWidget::handleMouseWheel(int delta)
{
    if (!viewportEnabled_) return false;
    bool ctrl  = platformCtrlDown();
    bool shift = platformShiftDown();

    if (ctrl)
    {
        float f = (delta > 0) ? 1.1f : 1.f / 1.1f;
        vp_.zoomToward(float(width) * 0.5f, float(height) * 0.5f, f);
    }
    else if (shift)
        vp_.panByScreen(float(delta) * 0.5f, 0.f);
    else
        vp_.panByScreen(0.f, -(float(delta) / float(WHEEL_DELTA)) * 40.f);

    pokeScrollbars();
    scheduleRepaint(this);
    return true;
}

bool CanvasWidget::handleKeyDown(int keyCode)
{
    bool ctrl  = platformCtrlDown();
    bool shift = platformShiftDown();
    bool alt   = platformAltDown(); 
    bool consumed = false;

    if (ctrl && viewportEnabled_)
    {
        if (keyCode == Key::OemPlus || keyCode == 43)
        { vp_.zoomIn(); pokeScrollbars(); scheduleRepaint(this); consumed = true; }
        else if (keyCode == Key::OemMinus || keyCode == 45)
        { vp_.zoomOut(); pokeScrollbars(); scheduleRepaint(this); consumed = true; }
        else if (keyCode == 29)
        { vp_.resetZoom(); pokeScrollbars(); scheduleRepaint(this); consumed = true; }
    }

    if (!consumed && activeSurface_)
    {
        KeyEvent ke{0, keyCode, ctrl, shift, alt};
        activeSurface_->onKeyDown(ke);
    }
    return consumed;
}

bool CanvasWidget::handleMouseLeave()
{
    hBar_.onMouseLeave();
    vBar_.onMouseLeave();
    if (panning_) panning_ = false;
    scheduleRepaint(this);
    return true;
}

// ============================================================================
// Shared helpers — unchanged
// ============================================================================

void CanvasWidget::viewportDims(int glW, int glH, int &vpW, int &vpH) const
{
    if (!viewportEnabled_ || !scrollbarsEnabled_) { vpW = glW; vpH = glH; return; }
    ScrollbarInfo h = vp_.scrollbarH(), v = vp_.scrollbarV();
    float dpi    = getDpiScale();
    int sbThickX = (int)(kSBThick * dpi);
    int sbThickY = (int)(kSBThick * dpi);
    vpW = glW - (v.visible ? sbThickX : 0);
    vpH = glH - (h.visible ? sbThickY : 0);
    if (vpW < 1) vpW = 1;
    if (vpH < 1) vpH = 1;
}

void CanvasWidget::updateViewportSize(int glW, int glH)
{
    int vpW, vpH;
    viewportDims(glW, glH, vpW, vpH);
    vp_.setViewSize(vpW, vpH);
}

void CanvasWidget::updateSBGeometry(int glW, int glH)
{
    float dpi      = getDpiScale();
    float sbThickX = kSBThick * dpi;
    float sbThickY = kSBThick * dpi;
    ScrollbarInfo h = vp_.scrollbarH(), v = vp_.scrollbarV();
    float hLen = float(glW) - (v.visible ? sbThickX : 0.f);
    hBar_.setGeometry(0.f, float(glH) - sbThickY, hLen);
    hBar_.setThumb(h.thumbMin, h.thumbMax,
                   h.visible && scrollbarsEnabled_ && viewportEnabled_);
    float vLen = float(glH) - (h.visible ? sbThickY : 0.f);
    vBar_.setGeometry(float(glW) - sbThickX, 0.f, vLen);
    vBar_.setThumb(v.thumbMin, v.thumbMax,
                   v.visible && scrollbarsEnabled_ && viewportEnabled_);
}

void CanvasWidget::beginPan(int sx, int sy)
{
    panning_ = true;
    panStartSX_ = sx; panStartSY_ = sy;
    panStartOX_ = vp_.offsetX(); panStartOY_ = vp_.offsetY();
}

void CanvasWidget::continuePan(int sx, int sy)
{
    float dx = float(sx - panStartSX_), dy = float(sy - panStartSY_);
    vp_.setOffset(panStartOX_ - dx / vp_.zoom(), panStartOY_ + dy / vp_.zoom());
    pokeScrollbars();
    scheduleRepaint(this);
}

void CanvasWidget::pokeScrollbars() { hBar_.poke(); vBar_.poke(); }

void CanvasWidget::applyHScrollFraction(float thumbMin)
{
    float span  = vp_.scrollbarH().thumbMax - vp_.scrollbarH().thumbMin;
    float range = vp_.canvasW() - vp_.viewW() / vp_.zoom();
    if (range <= 0.f) return;
    vp_.setOffsetX(thumbMin / (1.f - span) * range);
    pokeScrollbars(); scheduleRepaint(this);
}

void CanvasWidget::applyVScrollFraction(float thumbMin)
{
    float span  = vp_.scrollbarV().thumbMax - vp_.scrollbarV().thumbMin;
    float range = vp_.canvasH() - vp_.viewH() / vp_.zoom();
    if (range <= 0.f) return;
    float t = 1.f - thumbMin - span;
    vp_.setOffsetY(t / (1.f - span) * range);
    pokeScrollbars(); scheduleRepaint(this);
}

void CanvasWidget::activatePendingSurface()
{
    if (!pendingSurface_) return;
    if (activeSurface_) { activeSurface_->destroy(); activeSurface_.reset(); }
    activeSurface_ = pendingSurface_;
    pendingSurface_.reset();
    activeSurface_->initialize(canvasW_, canvasH_);
    vp_.setCanvasSize(canvasW_, canvasH_);
}



#endif // TARGET_OS_OSX
#endif // __APPLE__