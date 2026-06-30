// flux_canvas_macos.mm

#ifdef __APPLE__
#include <TargetConditionals.h>
#if TARGET_OS_OSX

#include "flux/flux_canvas2d.hpp"
#include "flux/flux_canvas2d_backend.hpp"
#include "flux/flux_window.hpp"
#include "flux/flux_canvas.hpp"
#include "flux/flux_window_macos_state.hpp"
#include "flux/flux_painter.hpp"
#include <cassert>
#include <unordered_map>

// ============================================================================
// macOS per-widget state
//
// Each CanvasWidget owns its own CAMetalLayer/command queue (kept separate
// from MacState::uiMetalLayer until Stage 4 unifies frame orchestration —
// see flux_window_macos.mm's renderFrame). Canvas content rendering itself
// (Canvas2D fill/stroke/text calls) is handled internally by Canvas2DMetal
// via the shared MacState::canvasBackend; this struct only tracks the
// widget's own surface/sizing/lifecycle plumbing.
// ============================================================================

struct MacCanvasState {
    bool   initialized    = false;
    bool   repaintPending = false;
    int    lastW          = 0;
    int    lastH          = 0;
    int    physX          = 0;
    int    physY          = 0;

    id<MTLDevice>       device   = nil;
    id<MTLCommandQueue> cmdQueue = nil;
    CAMetalLayer        *metalLayer = nil;

    Canvas2DBackend *backend = nullptr; // borrowed from MacState — not owned
};

static std::unordered_map<CanvasWidget*, MacCanvasState> s_macState;
static MacCanvasState& macState(CanvasWidget* w) { return s_macState[w]; }

// ============================================================================
// Helpers
// ============================================================================

static void registerWithPlatform(CanvasWidget* w) {
    auto* inst = FluxUI::getCurrentInstance();
    if (!inst) return;
    auto* pw = inst->getPlatformWindowPtr();
    if (pw) pw->registerCanvas_public(w);
}
static void unregisterWithPlatform(CanvasWidget* w) {
    auto* inst = FluxUI::getCurrentInstance();
    if (!inst) return;
    auto* pw = inst->getPlatformWindowPtr();
    if (pw) pw->unregisterCanvas_public(w);
}

static void scheduleRepaint(CanvasWidget* w) {
    if (w->onViewportChanged)
        w->onViewportChanged(w->vp_.zoom());
    auto& s = macState(w);
    if (!s.repaintPending) {
        s.repaintPending = true;
        dispatch_async(dispatch_get_main_queue(), ^{
            auto* inst = FluxUI::getCurrentInstance();
            if (!inst) return;
            auto* pw = inst->getPlatformWindowPtr();
            if (pw) pw->invalidate();
        });
    }
}

static float getDpiScale() {
    auto* inst = FluxUI::getCurrentInstance();
    if (!inst) return 1.f;
    NSScreen* scr = [NSScreen mainScreen];
    return scr ? (float)scr.backingScaleFactor : 1.f;
}

static int getWindowPhysW() {
    auto* inst = FluxUI::getCurrentInstance();
    if (!inst) return 1;
    auto* pw = inst->getPlatformWindowPtr();
    return pw ? pw->clientWidth() : 1;
}
static int getWindowPhysH() {
    auto* inst = FluxUI::getCurrentInstance();
    if (!inst) return 1;
    auto* pw = inst->getPlatformWindowPtr();
    return pw ? pw->clientHeight() : 1;
}

static void metalOrtho(float l, float r, float b, float t, float out[16]) {
    memset(out, 0, 64);
    out[0]  =  2.f / (r - l);
    out[5]  =  2.f / (t - b);
    out[10] = -1.f;
    out[12] = -(r + l) / (r - l);
    out[13] = -(t + b) / (t - b);
    out[15] =  1.f;
}

// ============================================================================
// initCanvas
// ============================================================================

static void initCanvas(CanvasWidget* w) {
    auto& s = macState(w);
    assert(!s.initialized);

    auto* inst = FluxUI::getCurrentInstance();
    auto* pw   = inst ? inst->getPlatformWindowPtr() : nullptr;
    if (!pw || !pw->getMacState()) return;

    auto* mac = pw->getMacState();
    s.device     = mac->metalDevice;
    s.cmdQueue   = [s.device newCommandQueue];
    s.metalLayer = [CAMetalLayer layer];
    s.metalLayer.device          = s.device;
    s.metalLayer.pixelFormat     = MTLPixelFormatBGRA8Unorm;
    s.metalLayer.framebufferOnly = YES;
    s.metalLayer.zPosition       = 0.0; // beneath uiMetalLayer (zPosition 1.0)
    [pw->getMacState()->nsView.layer insertSublayer:s.metalLayer atIndex:0];

    s.backend = mac->canvasBackend; // shared, borrowed

    s.lastW = (w->width  > 0) ? (int)(w->width  * getDpiScale()) : 1;
    s.lastH = (w->height > 0) ? (int)(w->height * getDpiScale()) : 1;
    s.physX = (int)(w->x * getDpiScale());
    s.physY = (int)(w->y * getDpiScale());
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
// syncPhysicalGeometry
// ============================================================================

static void syncPhysicalGeometry(CanvasWidget* w) {
    auto& s = macState(w);
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
    s.metalLayer.drawableSize = CGSizeMake(s.lastW < 1 ? 1 : s.lastW, s.lastH < 1 ? 1 : s.lastH);
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
// preRenderPass + glRenderPass
// ============================================================================

void CanvasWidget::preRenderPass() {
    auto& s = macState(this);
    if (!s.initialized) return;

    syncPhysicalGeometry(this);
    if (pendingSurface_) activatePendingSurface();
    if (!activeSurface_) return;

    auto now = Clock::now();
    frameDt_  = std::chrono::duration<double>(now - lastTick_).count();
    lastTick_ = now;

    activeSurface_->update(frameDt_);
    activeSurface_->preRender();
}

void CanvasWidget::glRenderPass() {
    auto& s = macState(this);
    if (!s.initialized || !s.cmdQueue || !s.metalLayer) return;
    if (!activeSurface_) return;

    s.repaintPending = false;

    int glW = s.lastW < 1 ? 1 : s.lastW;
    int glH = s.lastH < 1 ? 1 : s.lastH;

    auto drawable = [s.metalLayer nextDrawable];
    if (!drawable) return;

    float winW = (float)getWindowPhysW();
    float winH = (float)getWindowPhysH();
    float z    = vp_.zoom();
    float ox   = -vp_.offsetX() * z + (float)s.physX;
    float oy   = -vp_.offsetY() * z + (float)s.physY;

    float ortho[16];
    metalOrtho(0.f, winW, winH, 0.f, ortho);

    // Pan/zoom transform composed with the window-space ortho projection —
    // used for the canvas content itself (RenderSurface::render via Canvas2D).
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

    MTLRenderPassDescriptor* rpd = [MTLRenderPassDescriptor renderPassDescriptor];
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

    // ── Canvas content (RenderSurface) — drawn via the shared Canvas2DMetal
    // backend. Wire the live encoder/device/mvp into the backend for the
    // duration of this frame before constructing Canvas2D, and unwire it
    // immediately after — without this, Canvas2D's draw calls silently
    // no-op (currentFrame() returns nullptr inside flux_canvas2d_metal.mm).
    if (s.backend && s.backend->metal) {
        Canvas2DMetalBeginFrame(s.backend->metal, s.device, enc, mvp);
        Canvas2D ctx2d(s.backend, canvasW_, canvasH_);
        activeSurface_->render(ctx2d);
        Canvas2DMetalEndFrame(s.backend->metal);
    }

    updateSBGeometry(glW, glH);
    hBar_.tick(frameDt_);
    vBar_.tick(frameDt_);

    // ── Scrollbars — drawn through Painter's Metal solid pipeline, sharing
    // the same painterRes(device) pipeline cache flux_painter_macos.mm uses
    // for UI content. Uses the window-space ortho only (no pan/zoom — the
    // scrollbar track is fixed to the widget's screen rect).
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
}

bool CanvasWidget::isInitialized() const {
    return macState(const_cast<CanvasWidget*>(this)).initialized;
}
bool CanvasWidget::needsRepaint() const {
    return macState(const_cast<CanvasWidget*>(this)).repaintPending;
}
bool CanvasWidget::containsPoint(int wx, int wy) const {
    return wx >= x && wx < (x + width) && wy >= y && wy < (y + height);
}

// ============================================================================
// CanvasWidget member implementations
// ============================================================================

CanvasWidget::CanvasWidget()
    : hBar_(CustomScrollbar::Axis::Horizontal)
    , vBar_(CustomScrollbar::Axis::Vertical)
{
    autoWidth  = true;
    autoHeight = true;
    width  = 400;
    height = 300;
    registerWithPlatform(this);
}

CanvasWidget::~CanvasWidget() {
    if (activeSurface_) { activeSurface_->destroy(); activeSurface_.reset(); }
    pendingSurface_.reset();
    backend_ = nullptr;
    auto& s = macState(this);
    s.cmdQueue    = nil;
    s.metalLayer  = nil;
    s.backend     = nullptr;
    s.initialized = false;
    s_macState.erase(this);
    unregisterWithPlatform(this);
}

std::shared_ptr<CanvasWidget> CanvasWidget::setViewportEnabled(bool e)
    { viewportEnabled_ = e; return ptr(); }

std::shared_ptr<CanvasWidget> CanvasWidget::setScrollbarsEnabled(bool e) {
    scrollbarsEnabled_ = e;
    auto& s = macState(this);
    if (s.initialized) { updateViewportSize(s.lastW, s.lastH); scheduleRepaint(this); }
    return ptr();
}
bool CanvasWidget::scrollbarsEnabled() const { return scrollbarsEnabled_; }

RenderSurface*  CanvasWidget::getSurface() const { return activeSurface_.get(); }
const Viewport& CanvasWidget::viewport()   const { return vp_; }
Viewport&       CanvasWidget::viewport()         { return vp_; }

std::shared_ptr<CanvasWidget> CanvasWidget::setSize(int w, int h) {
    width = w; height = h;
    autoWidth = autoHeight = false;
    markNeedsLayout();
    return ptr();
}

std::shared_ptr<CanvasWidget> CanvasWidget::setCanvasSize(int w, int h) {
    float dpi = getDpiScale();
    canvasW_ = (int)(w * dpi);
    canvasH_ = (int)(h * dpi);
    auto& s = macState(this);
    if (s.initialized) {
        vp_.setCanvasSize(canvasW_, canvasH_);
        if (activeSurface_) activeSurface_->resize(canvasW_, canvasH_);
    }
    return ptr();
}

std::shared_ptr<CanvasWidget> CanvasWidget::redraw()
    { markNeedsPaint(); return ptr(); }

void CanvasWidget::computeLayout(GraphicsContext& /*ctx*/,
                                 const BoxConstraints& c, FontCache&) {
    if (autoWidth)  width  = (c.maxWidth  < kUnbounded) ? c.maxWidth  : width;
    if (autoHeight) height = (c.maxHeight < kUnbounded) ? c.maxHeight : height;
    needsLayout = false;
    scheduleRepaint(this);
}

void CanvasWidget::render(GraphicsContext& /*ctx*/, FontCache&) {
    // No-op: uiMetalLayer (the UI content layer) is transparent by default
    // (see MacState::ensureUILayer's opaque=NO + renderFrame's transparent
    // clear color), and this widget's own CAMetalLayer renders beneath it
    // at zPosition 0. No "punch a hole" step is needed — UI content simply
    // never draws over canvas regions unless a widget explicitly paints
    // there. (Old CG path used CGContextSetBlendMode(kCGBlendModeClear),
    // which no longer applies since GraphicsContext has no cgContext field.)
    needsPaint = false;
}

void CanvasWidget::onDetach() {
    if (activeSurface_) { activeSurface_->destroy(); activeSurface_.reset(); }
    pendingSurface_.reset();
    backend_ = nullptr;
    auto& s = macState(this);
    s.cmdQueue    = nil;
    s.metalLayer  = nil;
    s.backend     = nullptr;
    s.initialized = false;
    s_macState.erase(this);
    unregisterWithPlatform(this);
    Widget::onDetach();
}

void CanvasWidget::markNeedsPaint() {
    Widget::markNeedsPaint();
    scheduleRepaint(this);
}

void CanvasWidget::setCanvasBackend(Canvas2DBackend* backend) {
    backend_ = backend;
    auto& s = macState(this);
    s.backend = backend;
    if (backend_ && !s.initialized)
        initCanvas(this);
}

void CanvasWidget::onWindowResize(int /*newW*/, int /*newH*/) {
    auto& s = macState(this);
    if (!s.initialized) return;
    s.lastW = 0; s.lastH = 0;
    scheduleRepaint(this);
}

// ============================================================================
// Mouse / keyboard
// ============================================================================

void CanvasWidget::onMouseLeave() {
    hBar_.onMouseLeave();
    vBar_.onMouseLeave();
    scheduleRepaint(this);
}

void CanvasWidget::onMouseButtonDown(int sx, int sy, int button) {
    if (button == 2 && viewportEnabled_) { beginPan(sx, sy); return; }
    if (button == 0) {
        bool hC = hBar_.onMouseDown(sx, sy, [this](float t){ applyHScrollFraction(t); });
        bool vC = !hC && vBar_.onMouseDown(sx, sy, [this](float t){ applyVScrollFraction(t); });
        if (!hC && !vC) {
            if (viewportEnabled_ && platformSpaceDown())
                beginPan(sx, sy);
            else if (activeSurface_) {
                auto [cx, cy] = vp_.screenToCanvas(float(sx), float(sy));
                activeSurface_->onMouseDown(cx, cy);
            }
        }
        scheduleRepaint(this);
    }
    if (button == 1 && activeSurface_) {
        auto [cx, cy] = vp_.screenToCanvas(float(sx), float(sy));
        activeSurface_->onRightMouseDown(cx, cy);
    }
}

void CanvasWidget::onMouseButtonUp(int sx, int sy, int button) {
    if (button == 2) { panning_ = false; return; }
    if (button == 0) {
        bool hR = hBar_.onMouseUp(sx, sy);
        bool vR = vBar_.onMouseUp(sx, sy);
        if (!hR && !vR) {
            if (panning_) panning_ = false;
            else if (activeSurface_) {
                auto [cx, cy] = vp_.screenToCanvas(float(sx), float(sy));
                activeSurface_->onMouseUp(cx, cy);
            }
        }
        scheduleRepaint(this);
    }
}

void CanvasWidget::onMouseMove(int sx, int sy) {
    bool hDrag = hBar_.onMouseMove(sx, sy);
    bool vDrag = vBar_.onMouseMove(sx, sy);
    scheduleRepaint(this);
    if (hDrag || vDrag) return;
    if (panning_) continuePan(sx, sy);
    else if (activeSurface_) {
        auto [cx, cy] = vp_.screenToCanvas(float(sx), float(sy));
        activeSurface_->onMouseMove(cx, cy);
    }
}

void CanvasWidget::onScrollWheel(float deltaY) {
    if (!viewportEnabled_) return;
    int delta = (int)(deltaY * WHEEL_DELTA);
    bool ctrl  = platformCtrlDown();
    bool shift = platformShiftDown();
    if (ctrl) {
        float f = (delta > 0) ? 1.1f : 1.f / 1.1f;
        vp_.zoomToward(float(width) * 0.5f, float(height) * 0.5f, f);
    } else if (shift) {
        vp_.panByScreen(float(delta) * 0.5f, 0.f);
    } else {
        vp_.panByScreen(0.f, -(float(delta) / float(WHEEL_DELTA)) * 40.f);
    }
    pokeScrollbars();
    scheduleRepaint(this);
}

void CanvasWidget::onKeyDown(int keyCode) {
    bool ctrl  = platformCtrlDown();
    bool shift = platformShiftDown();
    bool alt   = platformAltDown();
    bool consumed = false;
    if (ctrl && viewportEnabled_) {
        if (keyCode == VK_OemPlus || keyCode == 43) {
            vp_.zoomIn(); pokeScrollbars(); scheduleRepaint(this); consumed = true;
        } else if (keyCode == VK_OemMinus || keyCode == 45) {
            vp_.zoomOut(); pokeScrollbars(); scheduleRepaint(this); consumed = true;
        } else if (keyCode == 29) {
            vp_.resetZoom(); pokeScrollbars(); scheduleRepaint(this); consumed = true;
        }
    }
    if (!consumed && activeSurface_) {
        KeyEvent ke{0, keyCode, ctrl, shift, alt};
        activeSurface_->onKeyDown(ke);
    }
}

// ============================================================================
// Shared helpers
// ============================================================================

void CanvasWidget::viewportDims(int glW, int glH, int& vpW, int& vpH) const {
    if (!viewportEnabled_ || !scrollbarsEnabled_) { vpW=glW; vpH=glH; return; }
    ScrollbarInfo h = vp_.scrollbarH(), v = vp_.scrollbarV();
    float dpi    = getDpiScale();
    int sbThickX = (int)(kSBThick * dpi);
    int sbThickY = (int)(kSBThick * dpi);
    vpW = glW - (v.visible ? sbThickX : 0);
    vpH = glH - (h.visible ? sbThickY : 0);
    if (vpW < 1) vpW = 1;
    if (vpH < 1) vpH = 1;
}

void CanvasWidget::updateViewportSize(int glW, int glH) {
    int vpW, vpH; viewportDims(glW, glH, vpW, vpH);
    vp_.setViewSize(vpW, vpH);
}

void CanvasWidget::updateSBGeometry(int glW, int glH) {
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

void CanvasWidget::beginPan(int sx, int sy) {
    panning_ = true; panStartSX_ = sx; panStartSY_ = sy;
    panStartOX_ = vp_.offsetX(); panStartOY_ = vp_.offsetY();
}
void CanvasWidget::continuePan(int sx, int sy) {
    float dx = float(sx - panStartSX_), dy = float(sy - panStartSY_);
    vp_.setOffset(panStartOX_ - dx/vp_.zoom(), panStartOY_ + dy/vp_.zoom());
    pokeScrollbars();
    scheduleRepaint(this);
}
void CanvasWidget::pokeScrollbars() { hBar_.poke(); vBar_.poke(); }

void CanvasWidget::applyHScrollFraction(float thumbMin) {
    float span  = vp_.scrollbarH().thumbMax - vp_.scrollbarH().thumbMin;
    float range = vp_.canvasW() - vp_.viewW() / vp_.zoom();
    if (range <= 0.f) return;
    vp_.setOffsetX(thumbMin / (1.f - span) * range);
    pokeScrollbars(); scheduleRepaint(this);
}
void CanvasWidget::applyVScrollFraction(float thumbMin) {
    float span  = vp_.scrollbarV().thumbMax - vp_.scrollbarV().thumbMin;
    float range = vp_.canvasH() - vp_.viewH() / vp_.zoom();
    if (range <= 0.f) return;
    float t = 1.f - thumbMin - span;
    vp_.setOffsetY(t / (1.f - span) * range);
    pokeScrollbars(); scheduleRepaint(this);
}

void CanvasWidget::activatePendingSurface() {
    if (!pendingSurface_) return;
    if (activeSurface_) { activeSurface_->destroy(); activeSurface_.reset(); }
    activeSurface_ = pendingSurface_;
    pendingSurface_.reset();
    activeSurface_->initialize(canvasW_, canvasH_);
    vp_.setCanvasSize(canvasW_, canvasH_);
}

void CanvasWidget::initEventType() {}
uint32_t CanvasWidget::repaintEventType() { return 0; }

#endif // TARGET_OS_OSX
#endif // __APPLE__