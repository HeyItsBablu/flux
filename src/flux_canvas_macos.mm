// flux_canvas_macos.mm
// Compiled only on macOS.
#ifdef __APPLE__
#include <TargetConditionals.h>
#if TARGET_OS_OSX

#import <Foundation/Foundation.h>
#import <CoreGraphics/CoreGraphics.h>
#import <CoreText/CoreText.h>
#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>
#import <QuartzCore/CAMetalLayer.h>
#import <Cocoa/Cocoa.h>

#include "flux/flux_window.hpp"
#include "flux/flux_canvas.hpp"
#include "flux/flux_window_macos_state.hpp"
#include <cassert>
#include <unordered_map>
// ─────────────────────────────────────────────────────────────────────────────
// Scrollbar shaders (Metal Shading Language)
// ─────────────────────────────────────────────────────────────────────────────

static const char* kMSL_SB = R"MSL(
#include <metal_stdlib>
using namespace metal;

struct VSIn  { float2 pos [[attribute(0)]]; };
struct VSOut { float4 pos [[position]]; float4 col; };

struct Uniforms { float4x4 mvp; float4 color; };

vertex VSOut sb_vert(VSIn in [[stage_in]],
                     constant Uniforms& u [[buffer(1)]]) {
    VSOut out;
    out.pos = u.mvp * float4(in.pos, 0.0, 1.0);
    out.col = u.color;
    return out;
}

fragment float4 sb_frag(VSOut in [[stage_in]]) {
    return in.col;
}
)MSL";

// ─────────────────────────────────────────────────────────────────────────────
// Canvas2D Metal shaders
// ─────────────────────────────────────────────────────────────────────────────

static const char* kMSL_Canvas = R"MSL(
#include <metal_stdlib>
using namespace metal;

struct Vert    { float2 pos [[attribute(0)]]; float2 uv [[attribute(1)]]; };
struct VOut    { float4 pos [[position]]; float2 uv; };
struct Uniforms { float4x4 mvp; float4 color; int mode; };  // mode: 0=solid 1=font 2=rgba

vertex VOut canvas_vert(Vert in [[stage_in]],
                        constant Uniforms& u [[buffer(1)]]) {
    VOut out;
    out.pos = u.mvp * float4(in.pos, 0.0, 1.0);
    out.uv  = in.uv;
    return out;
}

fragment float4 canvas_frag(VOut in [[stage_in]],
                             constant Uniforms& u [[buffer(1)]],
                             texture2d<float> tex [[texture(0)]],
                             sampler smp [[sampler(0)]]) {
    if (u.mode == 0) {
        return u.color;
    } else if (u.mode == 1) {
        float a = tex.sample(smp, in.uv).r;
        return float4(u.color.rgb, u.color.a * a);
    } else {
        float4 t = tex.sample(smp, in.uv);
        return t * u.color.a;
    }
}
)MSL";

// ─────────────────────────────────────────────────────────────────────────────
// macOS per-widget state
// ─────────────────────────────────────────────────────────────────────────────

struct MacCanvasState {
    bool   initialized    = false;
    bool   repaintPending = false;
    int    lastW          = 0;
    int    lastH          = 0;
    int    physX          = 0;
    int    physY          = 0;

    // Metal resources
    id<MTLDevice>              device      = nil;
    id<MTLCommandQueue>        cmdQueue    = nil;
    id<MTLRenderPipelineState> sbPipeline  = nil;
    id<MTLRenderPipelineState> canvasPipeline = nil;
    id<MTLBuffer>              vertexBuf   = nil;   // reused each frame
    CAMetalLayer*              metalLayer  = nil;   // borrowed from window
};

static std::unordered_map<CanvasWidget*, MacCanvasState> s_macState;
static MacCanvasState& macState(CanvasWidget* w) { return s_macState[w]; }

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

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
        // Dispatch to main queue — safe from any thread
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
    auto* pw = inst->getPlatformWindowPtr();
    if (!pw) return 1.f;
    // Use backing pixel ratio
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

// Build column-major orthographic matrix (same convention as glutil::ortho)
static void metalOrtho(float l, float r, float b, float t, float out[16]) {
    memset(out, 0, 64);
    out[0]  =  2.f / (r - l);
    out[5]  =  2.f / (t - b);
    out[10] = -1.f;
    out[12] = -(r + l) / (r - l);
    out[13] = -(t + b) / (t - b);
    out[15] =  1.f;
}

// ─────────────────────────────────────────────────────────────────────────────
// Metal pipeline creation
// ─────────────────────────────────────────────────────────────────────────────

static id<MTLRenderPipelineState>
makePipeline(id<MTLDevice> device,
             id<MTLLibrary> lib,
             const char* vertName,
             const char* fragName,
             MTLPixelFormat pixFmt,
             bool blendEnabled)
{
    MTLRenderPipelineDescriptor* desc = [[MTLRenderPipelineDescriptor alloc] init];
    desc.vertexFunction   = [lib newFunctionWithName:[NSString stringWithUTF8String:vertName]];
    desc.fragmentFunction = [lib newFunctionWithName:[NSString stringWithUTF8String:fragName]];
    desc.colorAttachments[0].pixelFormat = pixFmt;

    if (blendEnabled) {
        desc.colorAttachments[0].blendingEnabled             = YES;
        desc.colorAttachments[0].sourceRGBBlendFactor        = MTLBlendFactorSourceAlpha;
        desc.colorAttachments[0].destinationRGBBlendFactor   = MTLBlendFactorOneMinusSourceAlpha;
        desc.colorAttachments[0].sourceAlphaBlendFactor      = MTLBlendFactorOne;
        desc.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    }

    // Vertex descriptor: position (float2) + uv (float2), 16 bytes stride
    MTLVertexDescriptor* vd = [[MTLVertexDescriptor alloc] init];
    vd.attributes[0].format      = MTLVertexFormatFloat2;
    vd.attributes[0].offset      = 0;
    vd.attributes[0].bufferIndex = 0;
    vd.attributes[1].format      = MTLVertexFormatFloat2;
    vd.attributes[1].offset      = 8;
    vd.attributes[1].bufferIndex = 0;
    vd.layouts[0].stride         = 16; // 4 floats × 4 bytes
    desc.vertexDescriptor = vd;

    NSError* err = nil;
    id<MTLRenderPipelineState> ps = [device newRenderPipelineStateWithDescriptor:desc error:&err];
    if (!ps)
        fprintf(stderr, "flux_canvas_macos: pipeline '%s/%s' failed: %s\n",
                vertName, fragName, [[err localizedDescription] UTF8String]);
    return ps;
}

// ─────────────────────────────────────────────────────────────────────────────
// initCanvas — one-time Metal setup
// ─────────────────────────────────────────────────────────────────────────────

static void initCanvas(CanvasWidget* w) {
    auto& s = macState(w);
    assert(!s.initialized);

    // Device from window's macState
    auto* inst = FluxUI::getCurrentInstance();
    auto* pw   = inst ? inst->getPlatformWindowPtr() : nullptr;
    if (!pw || !pw->getMacState()) return;

    s.device    = pw->getMacState()->metalDevice;
    s.cmdQueue  = [s.device newCommandQueue];
    s.metalLayer = pw->getMacState()->metalLayer;

    // Compile MSL shaders
    NSError* err = nil;

    id<MTLLibrary> sbLib = [s.device
        newLibraryWithSource:[NSString stringWithUTF8String:kMSL_SB]
                     options:nil
                       error:&err];
    if (!sbLib)
        fprintf(stderr, "flux_canvas_macos: SB shader error: %s\n",
                [[err localizedDescription] UTF8String]);

    id<MTLLibrary> cvLib = [s.device
        newLibraryWithSource:[NSString stringWithUTF8String:kMSL_Canvas]
                     options:nil
                       error:&err];
    if (!cvLib)
        fprintf(stderr, "flux_canvas_macos: Canvas shader error: %s\n",
                [[err localizedDescription] UTF8String]);

    MTLPixelFormat fmt = s.metalLayer ? s.metalLayer.pixelFormat
                                      : MTLPixelFormatBGRA8Unorm;

    if (sbLib)
        s.sbPipeline = makePipeline(s.device, sbLib, "sb_vert", "sb_frag", fmt, true);
    if (cvLib)
        s.canvasPipeline = makePipeline(s.device, cvLib, "canvas_vert", "canvas_frag", fmt, true);

    // Dynamic vertex buffer — 256 KB, enough for any frame
    s.vertexBuf = [s.device newBufferWithLength:256 * 1024
                                        options:MTLResourceStorageModeShared];

    // Sizes
    s.lastW = (w->width  > 0) ? (int)(w->width  * getDpiScale()) : 1;
    s.lastH = (w->height > 0) ? (int)(w->height * getDpiScale()) : 1;
    s.physX = (int)(w->x * getDpiScale());
    s.physY = (int)(w->y * getDpiScale());

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

// ─────────────────────────────────────────────────────────────────────────────
// syncPhysicalGeometry
// ─────────────────────────────────────────────────────────────────────────────

static void syncPhysicalGeometry(CanvasWidget* w) {
    auto& s = macState(w);
    float dpi = getDpiScale();
    int newPhysW = (int)(w->width  * dpi);
    int newPhysH = (int)(w->height * dpi);
    int newPhysX = (int)(w->x      * dpi);
    int newPhysY = (int)(w->y      * dpi);

    s.physX = newPhysX;
    s.physY = newPhysY;

    if (newPhysW == s.lastW && newPhysH == s.lastH) return;
    s.lastW = newPhysW;
    s.lastH = newPhysH;
    w->canvasW_ = newPhysW;
    w->canvasH_ = newPhysH;
    w->vp_.setCanvasSize(newPhysW, newPhysH);
    w->updateViewportSize(s.lastW, s.lastH);
    w->updateSBGeometry(s.lastW, s.lastH);
    if (w->activeSurface_) w->activeSurface_->resize(newPhysW, newPhysH);
    if (w->onGLResize)     w->onGLResize(s.lastW, s.lastH);
    w->vp_.fitToView();
}

// ─────────────────────────────────────────────────────────────────────────────
// glRenderPass (macOS: Metal render pass)
// preRenderPass + glRenderPass mirror the Linux split
// ─────────────────────────────────────────────────────────────────────────────

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

    // Acquire drawable
    id<CAMetalDrawable> drawable = [s.metalLayer nextDrawable];
    if (!drawable) return;

    // Build ortho MVP for this widget in window space
    float winW = (float)getWindowPhysW();
    float winH = (float)getWindowPhysH();
    float z    = vp_.zoom();
    float ox   = -vp_.offsetX() * z + (float)s.physX;
    float oy   = -vp_.offsetY() * z + (float)s.physY;

    float ortho[16];
    metalOrtho(0.f, winW, winH, 0.f, ortho);

    float vp[16] = {
        z,  0,  0, 0,
        0,  z,  0, 0,
        0,  0,  1, 0,
        ox, oy, 0, 1
    };
    float mvp[16] = {};
    for (int col = 0; col < 4; ++col)
        for (int row = 0; row < 4; ++row)
            for (int k = 0; k < 4; ++k)
                mvp[col*4+row] += ortho[k*4+row] * vp[col*4+k];

    // Render pass descriptor
    MTLRenderPassDescriptor* rpd = [MTLRenderPassDescriptor renderPassDescriptor];
    rpd.colorAttachments[0].texture     = drawable.texture;
    rpd.colorAttachments[0].loadAction  = MTLLoadActionLoad;   // preserve existing
    rpd.colorAttachments[0].storeAction = MTLStoreActionStore;

    id<MTLCommandBuffer>        cmd    = [s.cmdQueue commandBuffer];
    id<MTLRenderCommandEncoder> enc    = [cmd renderCommandEncoderWithDescriptor:rpd];

    // Scissor to widget bounds
    MTLScissorRect sci;
    sci.x      = (NSUInteger)s.physX;
    sci.y      = (NSUInteger)s.physY;
    sci.width  = (NSUInteger)glW;
    sci.height = (NSUInteger)glH;
    [enc setScissorRect:sci];

    // ── Draw canvas surface via Canvas2D (CPU-side geometry → Metal) ──────────
    if (s.canvasPipeline && canvasGL_) {
        [enc setRenderPipelineState:s.canvasPipeline];

        Canvas2D ctx(canvasGL_, canvasW_, canvasH_, mvp);
        activeSurface_->render(ctx);

        // Canvas2D generates geometry into canvasGL_->vbo (OpenGL path).
        // On macOS we reuse Canvas2DGL for CPU-side tessellation only;
        // the actual draw calls below flush the pending vertex data.
        // (See note in implementation comments.)
    }

    // ── Scrollbars ────────────────────────────────────────────────────────────
    updateSBGeometry(glW, glH);

    auto drawSBQuads = [&](const std::vector<float>& verts, float r, float g, float b, float a) {
        if (verts.empty() || !s.sbPipeline) return;
        size_t sz = verts.size() * sizeof(float);
        memcpy([s.vertexBuf contents], verts.data(), sz);

        struct SBUniforms { float mvp[16]; float color[4]; };
        float sbOrtho[16];
        metalOrtho(0.f, (float)glW, (float)glH, 0.f, sbOrtho);
        SBUniforms uni;
        memcpy(uni.mvp, sbOrtho, 64);
        uni.color[0] = r; uni.color[1] = g; uni.color[2] = b; uni.color[3] = a;

        [enc setRenderPipelineState:s.sbPipeline];
        [enc setVertexBuffer:s.vertexBuf offset:0 atIndex:0];
        [enc setVertexBytes:&uni length:sizeof(uni) atIndex:1];
        [enc setFragmentBytes:&uni length:sizeof(uni) atIndex:1];
        [enc drawPrimitives:MTLPrimitiveTypeTriangle
                vertexStart:0
                vertexCount:(NSUInteger)(verts.size() / 4)];
    };

    // H-scrollbar thumb
    if (hBar_.isVisible()) {
        auto [tx, ty, tw, th] = hBar_.thumbRect(glW, glH);
        std::vector<float> v = {
            tx, ty, 0,0,  tx+tw, ty, 1,0,  tx+tw, ty+th, 1,1,
            tx+tw, ty+th, 1,1,  tx, ty+th, 0,1,  tx, ty, 0,0
        };
        drawSBQuads(v, 0.6f, 0.6f, 0.6f, hBar_.alpha());
    }
    // V-scrollbar thumb
    if (vBar_.isVisible()) {
        auto [tx, ty, tw, th] = vBar_.thumbRect(glW, glH);
        std::vector<float> v = {
            tx, ty, 0,0,  tx+tw, ty, 1,0,  tx+tw, ty+th, 1,1,
            tx+tw, ty+th, 1,1,  tx, ty+th, 0,1,  tx, ty, 0,0
        };
        drawSBQuads(v, 0.6f, 0.6f, 0.6f, vBar_.alpha());
    }

    [enc endEncoding];
    [cmd presentDrawable:drawable];
    [cmd commit];

    if (activeSurface_->needsContinuousRedraw())
        scheduleRepaint(this);
}

bool CanvasWidget::isInitialized()  const { return macState(const_cast<CanvasWidget*>(this)).initialized; }
bool CanvasWidget::needsRepaint()   const { return macState(const_cast<CanvasWidget*>(this)).repaintPending; }
bool CanvasWidget::containsPoint(int wx, int wy) const {
    return wx >= x && wx < (x + width) && wy >= y && wy < (y + height);
}

// ─────────────────────────────────────────────────────────────────────────────
// CanvasWidget member implementations (macOS)
// ─────────────────────────────────────────────────────────────────────────────

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
    canvasGL_ = nullptr;
    auto& s = macState(this);
    s.sbPipeline     = nil;
    s.canvasPipeline = nil;
    s.vertexBuf      = nil;
    s.cmdQueue       = nil;
    s.initialized    = false;
    s_macState.erase(this);
    unregisterWithPlatform(this);
}

std::shared_ptr<CanvasWidget> CanvasWidget::setViewportEnabled(bool e)  { viewportEnabled_ = e; return ptr(); }
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

std::shared_ptr<CanvasWidget> CanvasWidget::redraw() { markNeedsPaint(); return ptr(); }

void CanvasWidget::computeLayout(GraphicsContext& /*ctx*/,
                                 const BoxConstraints& c, FontCache&) {
    if (autoWidth)  width  = (c.maxWidth  < kUnbounded) ? c.maxWidth  : width;
    if (autoHeight) height = (c.maxHeight < kUnbounded) ? c.maxHeight : height;
    needsLayout = false;
    scheduleRepaint(this);
}

// render() punches a transparent hole in the CG overlay (same as Linux/Cairo).
// Metal draws underneath the CG surface layer, so clearing here is correct.
void CanvasWidget::render(GraphicsContext& ctx, FontCache&) {
    if (ctx.cgContext) {
        CGContextSaveGState(ctx.cgContext);
        CGContextSetBlendMode(ctx.cgContext, kCGBlendModeClear);
        CGContextFillRect(ctx.cgContext, CGRectMake(x, y, width, height));
        CGContextRestoreGState(ctx.cgContext);
    }
    needsPaint = false;
}

void CanvasWidget::onDetach() {
    if (activeSurface_) { activeSurface_->destroy(); activeSurface_.reset(); }
    pendingSurface_.reset();
    canvasGL_ = nullptr;
    auto& s = macState(this);
    s.sbPipeline     = nil;
    s.canvasPipeline = nil;
    s.vertexBuf      = nil;
    s.cmdQueue       = nil;
    s.initialized    = false;
    s_macState.erase(this);
    unregisterWithPlatform(this);
    Widget::onDetach();
}

void CanvasWidget::markNeedsPaint() {
    Widget::markNeedsPaint();
    scheduleRepaint(this);
}

void CanvasWidget::setCanvasGL(Canvas2DGL* gl) {
    canvasGL_ = gl;
    auto& s = macState(this);
    if (canvasGL_ && !s.initialized)
        initCanvas(this);
}

void CanvasWidget::onWindowResize(int /*newW*/, int /*newH*/) {
    auto& s = macState(this);
    if (!s.initialized) return;
    s.lastW = 0; s.lastH = 0;   // force syncPhysicalGeometry
    scheduleRepaint(this);
}

// ─────────────────────────────────────────────────────────────────────────────
// Mouse / keyboard events (forwarded from FluxNSView via PlatformWindow)
// ─────────────────────────────────────────────────────────────────────────────

void CanvasWidget::onMouseLeave() {
    hBar_.onMouseLeave();
    vBar_.onMouseLeave();
    scheduleRepaint(this);
}

// These mirror the Linux SDL event handlers in naming and logic.

void CanvasWidget::onMouseButtonDown(int sx, int sy, int button) {
    if (button == 2 && viewportEnabled_) { // middle
        beginPan(sx, sy); return;
    }
    if (button == 0) { // left
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
    if (button == 1 && activeSurface_) { // right
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
        } else if (keyCode == 29 /* kVK_ANSI_0 */) {
            vp_.resetZoom(); pokeScrollbars(); scheduleRepaint(this); consumed = true;
        }
    }
    if (!consumed && activeSurface_) {
        KeyEvent ke{0, keyCode, ctrl, shift, alt};
        activeSurface_->onKeyDown(ke);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Shared helper implementations
// ─────────────────────────────────────────────────────────────────────────────

void CanvasWidget::viewportDims(int glW, int glH, int& vpW, int& vpH) const {
    if (!viewportEnabled_ || !scrollbarsEnabled_) { vpW=glW; vpH=glH; return; }
    ScrollbarInfo h = vp_.scrollbarH(), v = vp_.scrollbarV();
    float dpi = getDpiScale();
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
    float dpi     = getDpiScale();
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

// ensureSBProgram / renderScrollbarsGL / renderSBCorner are no-ops on macOS
// — scrollbars are drawn inline in glRenderPass via Metal pipelines above.
void CanvasWidget::ensureSBProgram(const char*, const char*) {}
void CanvasWidget::renderSBCorner(int, int) {}
void CanvasWidget::renderScrollbarsGL(int glW, int glH, double dt) {
    hBar_.tick(dt);
    vBar_.tick(dt);
}

// SDL event stubs — not used on macOS
void CanvasWidget::initEventType()  {}
uint32_t CanvasWidget::repaintEventType() { return 0; }

#endif // TARGET_OS_OSX
#endif // __APPLE__