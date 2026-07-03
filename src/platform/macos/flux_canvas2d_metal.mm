// flux_canvas2d_metal.mm
//
// Metal implementation of the Canvas2D API (flux_canvas2d.hpp).
//
// Canvas2DBackend is defined ENTIRELY in this file — no shared backend
// header. It is opaque everywhere else (flux_canvas2d.hpp forward-declares
// it; flux_canvas_macos.mm only ever holds/passes a Canvas2DBackend*).
//
// Lifecycle / frame wiring exposed to flux_canvas_macos.mm as four free
// functions (declared `extern` locally in that file — no shared header):
//
//   Canvas2DBackend *Canvas2DBackend_create(id<MTLDevice> device);
//   void             Canvas2DBackend_destroy(Canvas2DBackend *b);
//   void             Canvas2DBackend_metalBeginFrame(Canvas2DBackend *b,
//                         id<MTLDevice> device, id<MTLRenderCommandEncoder> encoder,
//                         id<MTLTexture> targetTexture, const float mvp[16]);
//   void             Canvas2DBackend_metalEndFrame(Canvas2DBackend *b);
//
#ifdef __APPLE__
#include <TargetConditionals.h>
#if TARGET_OS_OSX

#include "flux/flux_canvas2d.hpp"
#import <Metal/Metal.h>
#import <CoreGraphics/CoreGraphics.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include <stb_truetype.h>


// stb_image — declaration only; implementation is provided by
// src/stb_impl.cpp (STB_IMAGE_IMPLEMENTATION). Format support (JPEG/PNG/
// BMP/TGA) is controlled entirely by stb_impl.cpp's STBI_ONLY_* defines —
// any STBI_ONLY_* macros here would be no-ops, since this TU never sees
// STB_IMAGE_IMPLEMENTATION.
#include "stb_image.h"


// ============================================================================
// Shaders
// ============================================================================

static const char *kMSL_Canvas2DMetal = R"MSL(
#include <metal_stdlib>
using namespace metal;

struct VertexIn  { float2 pos [[attribute(0)]]; float2 uv [[attribute(1)]]; };
struct VertexOut { float4 pos [[position]]; float2 uv; float2 local; };
struct Uniforms  {
    float4x4 mvp;
    float4   color;
    int      mode;        // 0=solid,1=R8 glyph,2=RGBA image,3=linear grad,4=radial grad
    float4   gradP0P1;    // linear: p0.xy/p1.xy. radial: center.xy in .xy, unused .zw
    float4   gradParams;  // radial: innerR in .x, outerR in .y. unused for linear
};

vertex VertexOut c2d_vert(VertexIn in [[stage_in]],
                          constant Uniforms& u [[buffer(1)]]) {
    VertexOut out;
    out.pos = u.mvp * float4(in.pos, 0.0, 1.0);
    out.uv  = in.uv;
    out.local = in.pos; // canvas-space position, pre-transform — used by gradients
    return out;
}

fragment float4 c2d_frag(VertexOut in [[stage_in]],
                         constant Uniforms& u [[buffer(1)]],
                         texture2d<float> tex [[texture(0)]],
                         sampler smp [[sampler(0)]]) {
    if (u.mode == 0) {
        return u.color;
    } else if (u.mode == 1) {
        float a = tex.sample(smp, in.uv).r;
        return float4(u.color.rgb, u.color.a * a);
    } else if (u.mode == 2) {
        float4 t = tex.sample(smp, in.uv);
        return t * u.color.a;
    } else if (u.mode == 3) {
        // Linear gradient: project local position onto the p0->p1 axis.
        float2 p0 = u.gradP0P1.xy;
        float2 p1 = u.gradP0P1.zw;
        float2 d  = p1 - p0;
        float  lenSq = max(dot(d, d), 1e-6);
        float  t  = clamp(dot(in.local - p0, d) / lenSq, 0.0, 1.0);
        float4 g  = tex.sample(smp, float2(t, 0.5));
        return float4(g.rgb, g.a * u.color.a);
    } else {
        // Radial gradient: distance from center, remapped between innerR/outerR.
        float2 c  = u.gradP0P1.xy;
        float  r0 = u.gradParams.x;
        float  r1 = u.gradParams.y;
        float  dist = length(in.local - c);
        float  t  = (r1 > r0) ? clamp((dist - r0) / (r1 - r0), 0.0, 1.0) : 0.0;
        float4 g  = tex.sample(smp, float2(t, 0.5));
        return float4(g.rgb, g.a * u.color.a);
    }
}
)MSL";

// ============================================================================
// Canvas2DImageMetal — concrete image type, private to this file.
// (Canvas2DImage base struct is the only thing flux_canvas2d.hpp exposes.)
// ============================================================================

struct Canvas2DMetalTexContext
{
    id<MTLTexture> texture = nil;
};

struct Canvas2DImageMetal : Canvas2DImage
{
    Canvas2DMetalTexContext *ctx = nullptr;
    ~Canvas2DImageMetal() override { delete ctx; }
};

// ============================================================================
// Shared pipeline/sampler cache, lazily built per-device.
// ============================================================================

namespace {

struct C2DMetalRes
{
    id<MTLDevice> device = nil;
    id<MTLRenderPipelineState> pipeline = nil;
    id<MTLSamplerState> sampler = nil;
};

C2DMetalRes &c2dRes(id<MTLDevice> device)
{
    static C2DMetalRes res;
    if (res.device == device) return res;
    res.device = device;

    NSError *err = nil;
    id<MTLLibrary> lib = [device newLibraryWithSource:
        [NSString stringWithUTF8String:kMSL_Canvas2DMetal] options:nil error:&err];
    if (!lib) {
        fprintf(stderr, "flux_canvas2d_metal: shader compile failed: %s\n",
                [[err localizedDescription] UTF8String]);
        return res;
    }

    MTLVertexDescriptor *vd = [[MTLVertexDescriptor alloc] init];
    vd.attributes[0].format = MTLVertexFormatFloat2;
    vd.attributes[0].offset = 0;
    vd.attributes[0].bufferIndex = 0;
    vd.attributes[1].format = MTLVertexFormatFloat2;
    vd.attributes[1].offset = 8;
    vd.attributes[1].bufferIndex = 0;
    vd.layouts[0].stride = 16;

    MTLRenderPipelineDescriptor *desc = [[MTLRenderPipelineDescriptor alloc] init];
    desc.vertexFunction = [lib newFunctionWithName:@"c2d_vert"];
    desc.fragmentFunction = [lib newFunctionWithName:@"c2d_frag"];
    desc.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
    desc.colorAttachments[0].blendingEnabled = YES;
    desc.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
    desc.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    desc.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOne;
    desc.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    desc.vertexDescriptor = vd;

    NSError *perr = nil;
    res.pipeline = [device newRenderPipelineStateWithDescriptor:desc error:&perr];
    if (!res.pipeline)
        fprintf(stderr, "flux_canvas2d_metal: pipeline failed: %s\n",
                [[perr localizedDescription] UTF8String]);

    MTLSamplerDescriptor *sd = [[MTLSamplerDescriptor alloc] init];
    sd.minFilter = MTLSamplerMinMagFilterLinear;
    sd.magFilter = MTLSamplerMinMagFilterLinear;
    res.sampler = [device newSamplerStateWithDescriptor:sd];

    return res;
}

struct CVertex { float x, y, u, v; };


// Mirrors the MSL `Uniforms` struct byte-for-byte, including the implicit
// alignment padding MSL inserts after `int mode` to bring `gradP0P1` back
// to a 16-byte boundary. Every draw call — solid, glyph, image, and both
// gradient modes — uses this exact struct and always fills the whole
// thing, even when the gradient fields are unused, since the fragment
// shader's `constant Uniforms&` expects the full 128 bytes regardless of
// which mode branch actually reads them.
struct GradUniforms
{
    float mvp[16];
    float color[4];
    int   mode;
    float pad0[3];
    float gradP0P1[4];
    float gradParams[4];
};


} // anonymous namespace

// ============================================================================
// Canvas2DBackend — FULL definition, private to this translation unit.
//
// Owns: font faces + glyph metric cache (stb_truetype), and this frame's
// live encoder/device/mvp (set by Canvas2DBackend_metalBeginFrame, cleared
// by Canvas2DBackend_metalEndFrame). One instance per CanvasWidget, created
// once and reused every frame — NOT re-created per frame.
// ============================================================================

struct Canvas2DBackend
{
    // Persistent device reference — separate from frameDevice below, which
    // is only valid between Canvas2DBackend_metalBeginFrame/EndFrame. Image
    // loading needs a device at any time (e.g. eager asset loading before
    // the first frame renders), so it's captured once here at creation.
    id<MTLDevice> device = nil;


    // Small dedicated queue for getImageData's blit-readback. Kept
    // separate from the frame's own command buffer/encoder so a readback
    // never has to interleave with in-flight render commands on the same
    // queue. Created lazily on first use.
    id<MTLCommandQueue> ioQueue = nil;

    // ── Fonts / glyphs ───────────────────────────────────────────────────
    struct FontFace
    {
        std::string name;
        std::vector<uint8_t> ttfData;
        stbtt_fontinfo info = {};
        bool ready = false;
    };
    std::vector<FontFace> fonts;

    struct GlyphKey
    {
        int fontIdx, codepoint, pixelSize;
        bool operator==(const GlyphKey &o) const
        {
            return fontIdx == o.fontIdx && codepoint == o.codepoint && pixelSize == o.pixelSize;
        }
    };
    struct GlyphEntry
    {
        GlyphKey key;
        int xoff, yoff, advance;
    };
    std::vector<GlyphEntry> glyphs;

    int findFont(const std::string &name) const
    {
        for (size_t i = 0; i < fonts.size(); ++i)
            if (fonts[i].name == name) return (int)i;
        return -1;
    }

    int addFont(const std::string &name, const std::string &path)
    {
        int existing = findFont(name);
        if (existing >= 0) return existing;

        std::ifstream f(path, std::ios::binary | std::ios::ate);
        if (!f) return -1;
        auto size = f.tellg();
        f.seekg(0);
        std::vector<uint8_t> data((size_t)size);
        f.read(reinterpret_cast<char *>(data.data()), size);

        FontFace face;
        face.name = name;
        face.ttfData = std::move(data);
        face.ready = stbtt_InitFont(&face.info, face.ttfData.data(),
                                    stbtt_GetFontOffsetForIndex(face.ttfData.data(), 0)) != 0;
        if (!face.ready) return -1;

        fonts.push_back(std::move(face));
        return (int)fonts.size() - 1;
    }

    const GlyphEntry *getGlyph(int fontIdx, int codepoint, int pixelSize)
    {
        if (fontIdx < 0 || fontIdx >= (int)fonts.size()) return nullptr;
        GlyphKey key{fontIdx, codepoint, pixelSize};
        for (auto &g : glyphs)
            if (g.key == key) return &g;

        FontFace &f = fonts[fontIdx];
        if (!f.ready) return nullptr;

        float scale = stbtt_ScaleForPixelHeight(&f.info, (float)pixelSize);
        int advanceRaw, lsbRaw;
        stbtt_GetCodepointHMetrics(&f.info, codepoint, &advanceRaw, &lsbRaw);
        int x0, y0, x1, y1;
        stbtt_GetCodepointBitmapBox(&f.info, codepoint, scale, scale, &x0, &y0, &x1, &y1);

        GlyphEntry entry;
        entry.key = key;
        entry.xoff = x0;
        entry.yoff = y0;
        entry.advance = (int)std::round(advanceRaw * scale);
        glyphs.push_back(entry);
        return &glyphs.back();
    }

    float getKernAdvance(int fontIdx, int cp1, int cp2, int pixelSize) const
    {
        if (fontIdx < 0 || fontIdx >= (int)fonts.size()) return 0.f;
        const FontFace &f = fonts[fontIdx];
        if (!f.ready) return 0.f;
        float scale = stbtt_ScaleForPixelHeight(&f.info, (float)pixelSize);
        return stbtt_GetCodepointKernAdvance(&f.info, cp1, cp2) * scale;
    }

    // ── This frame's live encoder state ─────────────────────────────────
    // Valid only between Canvas2DBackend_metalBeginFrame/EndFrame calls.
    id<MTLRenderCommandEncoder> frameEncoder = nil;
    id<MTLDevice> frameDevice = nil;
    id<MTLTexture> frameTexture = nil; // the drawable's texture this frame
    float frameMVP[16] = {};
};

// ============================================================================
// Public lifecycle / frame API — the ONLY surface flux_canvas_macos.mm sees.
// ============================================================================

Canvas2DBackend *Canvas2DBackend_create(id<MTLDevice> device)
{
    auto *b = new Canvas2DBackend();
    b->device = device;
    return b;
}

void Canvas2DBackend_destroy(Canvas2DBackend *b)
{
    delete b;
}

void Canvas2DBackend_metalBeginFrame(Canvas2DBackend *b,
                                     id<MTLDevice> device,
                                     id<MTLRenderCommandEncoder> encoder,
                                     id<MTLTexture> targetTexture,
                                     const float mvp[16])
{
    if (!b) return;
    b->frameEncoder = encoder;
    b->frameDevice = device;
    b->frameTexture = targetTexture;
    memcpy(b->frameMVP, mvp, sizeof(b->frameMVP));
}

void Canvas2DBackend_metalEndFrame(Canvas2DBackend *b)
{
    if (!b) return;
    b->frameEncoder = nil;
    b->frameDevice = nil;
    b->frameTexture = nil;

}

// ============================================================================
// Canvas2D::Mat3 — CTM representation shared by GL and Metal backends.
// GL's implementation lives in flux_canvas2d_gl.cpp; that TU is never
// compiled for macOS, so Metal keeps its own copy here (same convention as
// the duplicated ortho helpers across each platform's canvas backend).
// ============================================================================

Canvas2D::Mat3 Canvas2D::Mat3::identity() { return Mat3{}; }

Canvas2D::Mat3 Canvas2D::Mat3::multiply(const Mat3 &b) const
{
    Mat3 r;
    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 3; ++col)
        {
            float s = 0;
            for (int k = 0; k < 3; ++k)
                s += m[row * 3 + k] * b.m[k * 3 + col];
            r.m[row * 3 + col] = s;
        }
    return r;
}

Canvas2D::Mat3 Canvas2D::Mat3::translated(float dx, float dy) const
{
    Mat3 t;
    t.m[2] = dx;
    t.m[5] = dy;
    return this->multiply(t);
}
Canvas2D::Mat3 Canvas2D::Mat3::scaled(float sx, float sy) const
{
    Mat3 s;
    s.m[0] = sx;
    s.m[4] = sy;
    return this->multiply(s);
}
Canvas2D::Mat3 Canvas2D::Mat3::rotated(float a) const
{
    float c = cosf(a), s = sinf(a);
    Mat3 r;
    r.m[0] = c; r.m[1] = -s;
    r.m[3] = s; r.m[4] =  c;
    return this->multiply(r);
}
void Canvas2D::Mat3::apply(float &x, float &y) const
{
    float nx = m[0] * x + m[1] * y + m[2];
    float ny = m[3] * x + m[4] * y + m[5];
    x = nx;
    y = ny;
}

// ============================================================================
// Canvas2D — Metal implementation
// ============================================================================

Canvas2D::Canvas2D(Canvas2DBackend *backend, int canvasW, int canvasH)
    : backend_(backend), w_(canvasW), h_(canvasH)
{
    ctm_ = Mat3::identity();
}

// ── buildMVP — combines backend_->frameMVP (set per-frame by
// Canvas2DBackend_metalBeginFrame, called from flux_canvas_macos.mm's
// render()) with ctm_ (accumulated via translate/scale/rotate). Mirrors
// the GL backend's buildMVP exactly. ─────────────────────────────────────
void Canvas2D::buildMVP(float out[16]) const
{
    float c[16] = {
        ctm_.m[0], ctm_.m[3], 0, 0,
        ctm_.m[1], ctm_.m[4], 0, 0,
        0,         0,         1, 0,
        ctm_.m[2], ctm_.m[5], 0, 1};
    const float *base = backend_->frameMVP;
    memset(out, 0, 64);
    for (int col = 0; col < 4; ++col)
        for (int row = 0; row < 4; ++row)
            for (int k = 0; k < 4; ++k)
                out[col * 4 + row] += base[k * 4 + row] * c[col * 4 + k];
}

// ── State stack ──────────────────────────────────────────────────────────

void Canvas2D::save()
{
    SaveState s;
    s.ctm = ctm_;
    s.fillColor = fillColor_;
    s.strokeColor = strokeColor_;
    s.lineWidth = lineWidth_;
    s.globalAlpha = globalAlpha_;
    s.fillIsGrad = fillIsGrad_;
    s.clipDepth = clipDepth_;
    s.gradType = gradType_;
    s.gx0 = gx0_; s.gy0 = gy0_; s.gx1 = gx1_; s.gy1 = gy1_;
    s.gcx = gcx_; s.gcy = gcy_; s.gInR = gInR_; s.gOutR = gOutR_;
    s.stops = gStops_;
    s.fontFace = fontFace_;
    s.fontSize = fontSize_;
    s.fontBold = fontBold_;
    s.fontItalic = fontItalic_;
    s.textAlign = textAlign_;
    s.textBaseline = textBaseline_;
    s.lineCap = lineCap_;
    s.lineJoin = lineJoin_;
    stateStack_.push_back(std::move(s));
}

void Canvas2D::restore()
{
    if (stateStack_.empty()) return;
    const SaveState &s = stateStack_.back();
    ctm_ = s.ctm;
    fillColor_ = s.fillColor;
    strokeColor_ = s.strokeColor;
    lineWidth_ = s.lineWidth;
    globalAlpha_ = s.globalAlpha;
    fillIsGrad_ = s.fillIsGrad;
    clipDepth_ = s.clipDepth;
    gradType_ = s.gradType;
    gx0_ = s.gx0; gy0_ = s.gy0; gx1_ = s.gx1; gy1_ = s.gy1;
    gcx_ = s.gcx; gcy_ = s.gcy; gInR_ = s.gInR; gOutR_ = s.gOutR;
    gStops_ = s.stops;
    fontFace_ = s.fontFace;
    fontSize_ = s.fontSize;
    fontBold_ = s.fontBold;
    fontItalic_ = s.fontItalic;
    textAlign_ = s.textAlign;
    textBaseline_ = s.textBaseline;
    lineCap_ = s.lineCap;
    lineJoin_ = s.lineJoin;
    stateStack_.pop_back();
}

// ── Transform ────────────────────────────────────────────────────────────
void Canvas2D::translate(float dx, float dy) { ctm_ = ctm_.translated(dx, dy); }
void Canvas2D::scale(float sx, float sy)     { ctm_ = ctm_.scaled(sx, sy); }
void Canvas2D::rotate(float r)               { ctm_ = ctm_.rotated(r); }
void Canvas2D::resetTransform()              { ctm_ = Mat3::identity(); }

// ── Style ────────────────────────────────────────────────────────────────

void Canvas2D::setFillColor(Color c) { fillColor_ = c; fillIsGrad_ = false; }
void Canvas2D::setStrokeColor(Color c) { strokeColor_ = c; }
void Canvas2D::setLineWidth(float w) { lineWidth_ = w; }
void Canvas2D::setLineCap(LineCap cap) { lineCap_ = cap; }
void Canvas2D::setLineJoin(LineJoin join) { lineJoin_ = join; }
void Canvas2D::setMiterLimit(float limit) { miterLimit_ = limit; }
void Canvas2D::setGlobalAlpha(float a) { globalAlpha_ = a; }
void Canvas2D::setCompositeOp(CompositeOp op) { compositeOp_ = op; }
void Canvas2D::setFillRule(FillRule rule) { fillRule_ = rule; }

// ── Gradient — TODO: no LUT texture, multi-stop unsupported.
void Canvas2D::beginLinearGradient(float x0, float y0, float x1, float y1)
{
    gradType_ = GradType::Linear;
    gx0_ = x0; gy0_ = y0; gx1_ = x1; gy1_ = y1;
    gStops_.clear();
}
void Canvas2D::beginRadialGradient(float cx, float cy, float innerR, float outerR)
{
    gradType_ = GradType::Radial;
    gcx_ = cx; gcy_ = cy; gInR_ = innerR; gOutR_ = outerR;
    gStops_.clear();
}
void Canvas2D::addColorStop(float t, Color c) { gStops_.push_back({t, c}); }
void Canvas2D::setFillGradient() { fillIsGrad_ = true; }

// ── Draw helpers ────────────────────────────────────────────────────────

static void pushQuad(std::vector<CVertex> &out, float x, float y, float w, float h,
                     float u0=0, float v0=0, float u1=1, float v1=1)
{
    out.push_back({x,     y,     u0, v0});
    out.push_back({x + w, y,     u1, v0});
    out.push_back({x,     y + h, u0, v1});
    out.push_back({x + w, y,     u1, v0});
    out.push_back({x + w, y + h, u1, v1});
    out.push_back({x,     y + h, u0, v1});
}

static void drawSolid(Canvas2DBackend *backend, const float mvp[16],
                      const std::vector<CVertex> &verts,
                      Color color, float globalAlpha)
{
    if (verts.empty() || !backend || !backend->frameEncoder) return;
    auto &res = c2dRes(backend->frameDevice);
    if (!res.pipeline) return;

    [backend->frameEncoder setRenderPipelineState:res.pipeline];
    [backend->frameEncoder setVertexBytes:verts.data() length:verts.size()*sizeof(CVertex) atIndex:0];

    GradUniforms u{};
    memcpy(u.mvp, mvp, sizeof(u.mvp));
    u.color[0] = color.r/255.f; u.color[1] = color.g/255.f;
    u.color[2] = color.b/255.f; u.color[3] = (color.a/255.f) * globalAlpha;
    u.mode = 0;
    [backend->frameEncoder setVertexBytes:&u length:sizeof(u) atIndex:1];
    [backend->frameEncoder setFragmentBytes:&u length:sizeof(u) atIndex:1];
    [backend->frameEncoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:verts.size()];
}

static void drawTexturedR8(Canvas2DBackend *backend, const float mvp[16],
                           const std::vector<CVertex> &verts,
                           Color tint, float globalAlpha, id<MTLTexture> tex)
{
    if (verts.empty() || !tex || !backend || !backend->frameEncoder) return;
    auto &res = c2dRes(backend->frameDevice);
    if (!res.pipeline) return;

    [backend->frameEncoder setRenderPipelineState:res.pipeline];
    [backend->frameEncoder setVertexBytes:verts.data() length:verts.size()*sizeof(CVertex) atIndex:0];

    GradUniforms u{};
    memcpy(u.mvp, mvp, sizeof(u.mvp));
    u.color[0] = tint.r/255.f; u.color[1] = tint.g/255.f;
    u.color[2] = tint.b/255.f; u.color[3] = (tint.a/255.f) * globalAlpha;
    u.mode = 1;
    [backend->frameEncoder setVertexBytes:&u length:sizeof(u) atIndex:1];
    [backend->frameEncoder setFragmentBytes:&u length:sizeof(u) atIndex:1];
    [backend->frameEncoder setFragmentTexture:tex atIndex:0];
    [backend->frameEncoder setFragmentSamplerState:res.sampler atIndex:0];
    [backend->frameEncoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:verts.size()];
}

static void drawTexturedRGBA(Canvas2DBackend *backend, const float mvp[16],
                             const std::vector<CVertex> &verts,
                             float globalAlpha, id<MTLTexture> tex)
{
    if (verts.empty() || !tex || !backend || !backend->frameEncoder) return;
    auto &res = c2dRes(backend->frameDevice);
    if (!res.pipeline) return;

    [backend->frameEncoder setRenderPipelineState:res.pipeline];
    [backend->frameEncoder setVertexBytes:verts.data() length:verts.size()*sizeof(CVertex) atIndex:0];

    GradUniforms u{};
    memcpy(u.mvp, mvp, sizeof(u.mvp));
    u.color[0]=u.color[1]=u.color[2]=1.f; u.color[3]=globalAlpha;
    u.mode = 2;
    [backend->frameEncoder setVertexBytes:&u length:sizeof(u) atIndex:1];
    [backend->frameEncoder setFragmentBytes:&u length:sizeof(u) atIndex:1];
    [backend->frameEncoder setFragmentTexture:tex atIndex:0];
    [backend->frameEncoder setFragmentSamplerState:res.sampler atIndex:0];
    [backend->frameEncoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:verts.size()];
}


// Builds a 256x1 RGBA8 LUT from sorted color stops, sampled by the
// fragment shader's computed `t`. Rebuilt on every gradient draw call —
// deliberately uncached for this pass; if profiling shows this matters,
// the natural fix is a small cache keyed by a hash of (gradType, stops).
static id<MTLTexture> buildGradientLUT(id<MTLDevice> device,
                                       const std::vector<std::pair<float, Color>> &stopsIn)
{
    if (!device || stopsIn.empty()) return nil;

    std::vector<std::pair<float, Color>> stops = stopsIn;
    std::sort(stops.begin(), stops.end(),
             [](auto &a, auto &b) { return a.first < b.first; });

    static constexpr int kLUTSize = 256;
    std::vector<uint8_t> pixels(kLUTSize * 4);

    for (int i = 0; i < kLUTSize; ++i)
    {
        float t = float(i) / float(kLUTSize - 1);
        Color c;
        if (t <= stops.front().first) c = stops.front().second;
        else if (t >= stops.back().first) c = stops.back().second;
        else
        {
            size_t j = 0;
            while (j + 1 < stops.size() && stops[j + 1].first < t) ++j;
            const auto &a = stops[j];
            const auto &b = stops[std::min(j + 1, stops.size() - 1)];
            float span = b.first - a.first;
            float local = (span > 1e-6f) ? (t - a.first) / span : 0.f;
            c = a.second.interpolate(b.second, local);
        }
        pixels[i*4+0] = c.r; pixels[i*4+1] = c.g;
        pixels[i*4+2] = c.b; pixels[i*4+3] = c.a;
    }

    MTLTextureDescriptor *td = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                      width:kLUTSize height:1 mipmapped:NO];
    id<MTLTexture> tex = [device newTextureWithDescriptor:td];
    [tex replaceRegion:MTLRegionMake2D(0, 0, kLUTSize, 1) mipmapLevel:0
             withBytes:pixels.data() bytesPerRow:kLUTSize * 4];
    return tex;
}

static void drawGradient(Canvas2DBackend *backend, const float mvp[16],
                         const std::vector<CVertex> &verts, float globalAlpha,
                         Canvas2D::GradType gradType,
                         float gx0, float gy0, float gx1, float gy1,
                         float gcx, float gcy, float gInR, float gOutR,
                         const std::vector<std::pair<float, Color>> &stops)
{
    if (verts.empty() || !backend || !backend->frameEncoder || stops.empty()) return;
    auto &res = c2dRes(backend->frameDevice);
    if (!res.pipeline) return;

    id<MTLTexture> lut = buildGradientLUT(backend->frameDevice, stops);
    if (!lut) return;

    [backend->frameEncoder setRenderPipelineState:res.pipeline];
    [backend->frameEncoder setVertexBytes:verts.data() length:verts.size()*sizeof(CVertex) atIndex:0];

    GradUniforms u{};
    memcpy(u.mvp, mvp, sizeof(u.mvp));
    u.color[0] = u.color[1] = u.color[2] = 1.f;
    u.color[3] = globalAlpha;

    if (gradType == Canvas2D::GradType::Linear)
    {
        u.mode = 3;
        u.gradP0P1[0] = gx0; u.gradP0P1[1] = gy0;
        u.gradP0P1[2] = gx1; u.gradP0P1[3] = gy1;
    }
    else
    {
        u.mode = 4;
        u.gradP0P1[0] = gcx; u.gradP0P1[1] = gcy;
        u.gradParams[0] = gInR; u.gradParams[1] = gOutR;
    }

    [backend->frameEncoder setVertexBytes:&u length:sizeof(u) atIndex:1];
    [backend->frameEncoder setFragmentBytes:&u length:sizeof(u) atIndex:1];
    [backend->frameEncoder setFragmentTexture:lut atIndex:0];
    [backend->frameEncoder setFragmentSamplerState:res.sampler atIndex:0];
    [backend->frameEncoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:verts.size()];
}

// ── Primitives ───────────────────────────────────────────────────────────

void Canvas2D::clearRect(float x, float y, float w, float h)
{
    std::vector<CVertex> v;
    pushQuad(v, x, y, w, h);
    float mvp[16]; buildMVP(mvp);
    drawSolid(backend_, mvp, v, Color::fromRGBA(0,0,0,0), 1.f);
}

void Canvas2D::fillRect(float x, float y, float w, float h)
{
    std::vector<CVertex> v;
    pushQuad(v, x, y, w, h);
    float mvp[16]; buildMVP(mvp);
    if (fillIsGrad_ && !gStops_.empty())
        drawGradient(backend_, mvp, v, globalAlpha_, gradType_,
                    gx0_, gy0_, gx1_, gy1_, gcx_, gcy_, gInR_, gOutR_, gStops_);
    else
        drawSolid(backend_, mvp, v, fillColor_, globalAlpha_);
}

void Canvas2D::strokeRect(float x, float y, float w, float h)
{
    float lw = lineWidth_;
    std::vector<CVertex> v;
    pushQuad(v, x, y, w, lw);
    pushQuad(v, x, y+h-lw, w, lw);
    pushQuad(v, x, y, lw, h);
    pushQuad(v, x+w-lw, y, lw, h);
    float mvp[16]; buildMVP(mvp);
    drawSolid(backend_, mvp, v, strokeColor_, globalAlpha_);
}

void Canvas2D::fillRoundedRect(float x, float y, float w, float h, float r)
{
    r = std::min(r, std::min(w, h) * 0.5f);
    std::vector<CVertex> v;
    float mvp[16]; buildMVP(mvp);
    auto doFill = [&](const std::vector<CVertex> &vv) {
        if (fillIsGrad_ && !gStops_.empty())
            drawGradient(backend_, mvp, vv, globalAlpha_, gradType_,
                        gx0_, gy0_, gx1_, gy1_, gcx_, gcy_, gInR_, gOutR_, gStops_);
        else
            drawSolid(backend_, mvp, vv, fillColor_, globalAlpha_);
    };
    if (r <= 0.f) { pushQuad(v, x, y, w, h); doFill(v); return; }

    pushQuad(v, x+r, y,     w-2*r, r);
    pushQuad(v, x+r, y+h-r, w-2*r, r);
    pushQuad(v, x,   y+r,   w,     h-2*r);

    auto corner = [&](float cx, float cy, float a0, float a1) {
        const int segs = 12;
        for (int i = 0; i < segs; ++i) {
            float t0 = a0 + (a1-a0)*(i/(float)segs);
            float t1 = a0 + (a1-a0)*((i+1)/(float)segs);
            v.push_back({cx, cy, 0, 0});
            v.push_back({cx + r*cosf(t0), cy + r*sinf(t0), 0, 0});
            v.push_back({cx + r*cosf(t1), cy + r*sinf(t1), 0, 0});
        }
    };
    const float PI = 3.14159265f;
    corner(x+r,   y+r,   PI,      1.5f*PI);
    corner(x+w-r, y+r,   1.5f*PI, 2.f*PI);
    corner(x+w-r, y+h-r, 0,       0.5f*PI);
    corner(x+r,   y+h-r, 0.5f*PI, PI);

    doFill(v);
}

void Canvas2D::strokeRoundedRect(float x, float y, float w, float h, float r)
{
    strokeRect(x, y, w, h);
    (void)r;
}

void Canvas2D::fillCircle(float cx, float cy, float r)
{
    std::vector<CVertex> v;
    const int segs = 32;
    for (int i = 0; i < segs; ++i) {
        float t0 = (i/(float)segs)*2.f*3.14159265f;
        float t1 = ((i+1)/(float)segs)*2.f*3.14159265f;
        v.push_back({cx, cy, 0, 0});
        v.push_back({cx + r*cosf(t0), cy + r*sinf(t0), 0, 0});
        v.push_back({cx + r*cosf(t1), cy + r*sinf(t1), 0, 0});
    }
    float mvp[16]; buildMVP(mvp);
    if (fillIsGrad_ && !gStops_.empty())
        drawGradient(backend_, mvp, v, globalAlpha_, gradType_,
                    gx0_, gy0_, gx1_, gy1_, gcx_, gcy_, gInR_, gOutR_, gStops_);
    else
        drawSolid(backend_, mvp, v, fillColor_, globalAlpha_);
}

void Canvas2D::strokeCircle(float cx, float cy, float r)
{
    std::vector<CVertex> v;
    const int segs = 32;
    float lw = lineWidth_;
    for (int i = 0; i <= segs; ++i) {
        float t0 = (i/(float)segs)*2.f*3.14159265f;
        float t1 = ((i+1)/(float)segs)*2.f*3.14159265f;
        float x0 = cx+r*cosf(t0), y0 = cy+r*sinf(t0);
        float x1 = cx+r*cosf(t1), y1 = cy+r*sinf(t1);
        float dx=x1-x0, dy=y1-y0, len=sqrtf(dx*dx+dy*dy);
        if (len < 0.0001f) continue;
        float nx=-dy/len*(lw*0.5f), ny=dx/len*(lw*0.5f);
        v.push_back({x0+nx,y0+ny,0,0}); v.push_back({x0-nx,y0-ny,0,0}); v.push_back({x1+nx,y1+ny,0,0});
        v.push_back({x1+nx,y1+ny,0,0}); v.push_back({x0-nx,y0-ny,0,0}); v.push_back({x1-nx,y1-ny,0,0});
    }
    float mvp[16]; buildMVP(mvp);
    drawSolid(backend_, mvp, v, strokeColor_, globalAlpha_);
}

// ── Path API — TODO: minimal (convex fan fill / polyline stroke).
void Canvas2D::beginPath() { path_.clear(); }
void Canvas2D::closePath() { if (!path_.empty()) path_.push_back({pathStartX_, pathStartY_, false}); }
void Canvas2D::moveTo(float x, float y) { path_.push_back({x, y, true}); pathStartX_=x; pathStartY_=y; curX_=x; curY_=y; }
void Canvas2D::lineTo(float x, float y) { path_.push_back({x, y, false}); curX_=x; curY_=y; }

void Canvas2D::arc(float cx, float cy, float radius, float startAngle, float endAngle, bool anticlockwise)
{
    const int segs = 24;
    float sweep = endAngle - startAngle;
    if (anticlockwise) sweep = -sweep;
    for (int i = 0; i <= segs; ++i) {
        float t = startAngle + sweep * (i/(float)segs) * (anticlockwise ? -1.f : 1.f);
        float x = cx + radius*cosf(t), y = cy + radius*sinf(t);
        lineTo(x, y);
    }
}
void Canvas2D::arcTo(float, float, float x2, float y2, float) { lineTo(x2, y2); }
void Canvas2D::quadraticCurveTo(float cpx, float cpy, float x, float y)
{
    const int segs = 16;
    float x0 = curX_, y0 = curY_;
    for (int i = 1; i <= segs; ++i) {
        float t = i/(float)segs, mt = 1-t;
        float px = mt*mt*x0 + 2*mt*t*cpx + t*t*x;
        float py = mt*mt*y0 + 2*mt*t*cpy + t*t*y;
        lineTo(px, py);
    }
}
void Canvas2D::bezierCurveTo(float cp1x, float cp1y, float cp2x, float cp2y, float x, float y)
{
    const int segs = 24;
    float x0 = curX_, y0 = curY_;
    for (int i = 1; i <= segs; ++i) {
        float t = i/(float)segs, mt = 1-t;
        float px = mt*mt*mt*x0 + 3*mt*mt*t*cp1x + 3*mt*t*t*cp2x + t*t*t*x;
        float py = mt*mt*mt*y0 + 3*mt*mt*t*cp1y + 3*mt*t*t*cp2y + t*t*t*y;
        lineTo(px, py);
    }
}
void Canvas2D::rect(float x, float y, float w, float h)
{
    moveTo(x, y); lineTo(x+w, y); lineTo(x+w, y+h); lineTo(x, y+h); closePath();
}
void Canvas2D::ellipse(float cx, float cy, float rx, float ry, float rotation,
                       float startAngle, float endAngle, bool anticlockwise)
{
    const int segs = 32;
    float sweep = endAngle - startAngle;
    if (anticlockwise) sweep = -sweep;
    for (int i = 0; i <= segs; ++i) {
        float t = startAngle + sweep * (i/(float)segs);
        float ex = rx*cosf(t), ey = ry*sinf(t);
        float x = cx + ex*cosf(rotation) - ey*sinf(rotation);
        float y = cy + ex*sinf(rotation) + ey*cosf(rotation);
        lineTo(x, y);
    }
}

void Canvas2D::fill()
{
    if (path_.size() < 3) return;
    std::vector<CVertex> v;
    for (size_t i = 1; i+1 < path_.size(); ++i) {
        v.push_back({path_[0].x, path_[0].y, 0, 0});
        v.push_back({path_[i].x, path_[i].y, 0, 0});
        v.push_back({path_[i+1].x, path_[i+1].y, 0, 0});
    }
    float mvp[16]; buildMVP(mvp);
    if (fillIsGrad_ && !gStops_.empty())
        drawGradient(backend_, mvp, v, globalAlpha_, gradType_,
                    gx0_, gy0_, gx1_, gy1_, gcx_, gcy_, gInR_, gOutR_, gStops_);
    else
        drawSolid(backend_, mvp, v, fillColor_, globalAlpha_);
}

void Canvas2D::stroke()
{
    if (path_.size() < 2) return;
    std::vector<CVertex> v;
    float lw = lineWidth_;
    for (size_t i = 1; i < path_.size(); ++i) {
        if (path_[i].move) continue;
        float x0=path_[i-1].x, y0=path_[i-1].y, x1=path_[i].x, y1=path_[i].y;
        float dx=x1-x0, dy=y1-y0, len=sqrtf(dx*dx+dy*dy);
        if (len < 0.0001f) continue;
        float nx=-dy/len*(lw*0.5f), ny=dx/len*(lw*0.5f);
        v.push_back({x0+nx,y0+ny,0,0}); v.push_back({x0-nx,y0-ny,0,0}); v.push_back({x1+nx,y1+ny,0,0});
        v.push_back({x1+nx,y1+ny,0,0}); v.push_back({x0-nx,y0-ny,0,0}); v.push_back({x1-nx,y1-ny,0,0});
    }
    float mvp[16]; buildMVP(mvp);
    drawSolid(backend_, mvp, v, strokeColor_, globalAlpha_);
}

void Canvas2D::clip()
{
    ++clipDepth_;
}

// ── Font registration ───────────────────────────────────────────────────

bool Canvas2D::registerFont(Canvas2DBackend *backend, const std::string &name,
                            const std::string &ttfPath)
{
    if (!backend) return false;
    return backend->addFont(name, ttfPath) >= 0;
}

// ── Image ────────────────────────────────────────────────────────────────

static Canvas2DImage *makeImageFromRGBA(Canvas2DBackend *backend,
                                        const unsigned char *rgba, int w, int h)
{
    if (!backend || !backend->device || !rgba || w <= 0 || h <= 0)
        return nullptr;

    auto *img = new Canvas2DImageMetal();
    img->ctx = new Canvas2DMetalTexContext();

    MTLTextureDescriptor *td = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                      width:w height:h mipmapped:NO];
    img->ctx->texture = [backend->device newTextureWithDescriptor:td];
    if (!img->ctx->texture)
    {
        delete img;
        return nullptr;
    }
    [img->ctx->texture replaceRegion:MTLRegionMake2D(0, 0, w, h) mipmapLevel:0
                           withBytes:rgba bytesPerRow:w * 4];

    img->width = w;
    img->height = h;
    return img;
}

Canvas2DImage *Canvas2D::loadImage(const std::string &path)
{
    int w, h, ch;
    stbi_set_flip_vertically_on_load(0);
    unsigned char *data = stbi_load(path.c_str(), &w, &h, &ch, 4);
    if (!data)
        return nullptr;
    Canvas2DImage *img = makeImageFromRGBA(backend_, data, w, h);
    stbi_image_free(data);
    return img;
}

Canvas2DImage *Canvas2D::loadImageFromMemory(const unsigned char *data, int byteLen)
{
    int w, h, ch;
    stbi_set_flip_vertically_on_load(0);
    unsigned char *px = stbi_load_from_memory(data, byteLen, &w, &h, &ch, 4);
    if (!px)
        return nullptr;
    Canvas2DImage *img = makeImageFromRGBA(backend_, px, w, h);
    stbi_image_free(px);
    return img;
}

void Canvas2D::updateImage(Canvas2DImage *img, const unsigned char *rgba, int w, int h)
{
    auto *m = static_cast<Canvas2DImageMetal *>(img);
    if (!m || !backend_ || !backend_->device) return;

    if (!m->ctx) m->ctx = new Canvas2DMetalTexContext();
    if (!m->ctx->texture || m->width != w || m->height != h) {
        MTLTextureDescriptor *td = [MTLTextureDescriptor
            texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                          width:w height:h mipmapped:NO];
        m->ctx->texture = [backend_->device newTextureWithDescriptor:td];
        m->width = w; m->height = h;
    }
    [m->ctx->texture replaceRegion:MTLRegionMake2D(0,0,w,h) mipmapLevel:0
                         withBytes:rgba bytesPerRow:w*4];
}

void Canvas2D::freeImage(Canvas2DImage *img)
{
    delete img;
}

void Canvas2D::drawImage(const Canvas2DImage *img, float dx, float dy)
{
    if (!img) return;
    drawImage(img, dx, dy, (float)img->width, (float)img->height);
}

void Canvas2D::drawImage(const Canvas2DImage *img, float dx, float dy, float dw, float dh)
{
    auto *m = static_cast<const Canvas2DImageMetal *>(img);
    if (!m || !m->ctx || !m->ctx->texture) return;
    std::vector<CVertex> v;
    pushQuad(v, dx, dy, dw, dh);
    float mvp[16]; buildMVP(mvp);
    drawTexturedRGBA(backend_, mvp, v, globalAlpha_, m->ctx->texture);
}

void Canvas2D::drawImage(const Canvas2DImage *img, float sx, float sy, float sw, float sh,
                         float dx, float dy, float dw, float dh)
{
    auto *m = static_cast<const Canvas2DImageMetal *>(img);
    if (!m || !m->ctx || !m->ctx->texture || m->width <= 0 || m->height <= 0) return;
    float u0 = sx / m->width, v0 = sy / m->height;
    float u1 = (sx+sw) / m->width, v1 = (sy+sh) / m->height;
    std::vector<CVertex> v;
    pushQuad(v, dx, dy, dw, dh, u0, v0, u1, v1);
    float mvp[16]; buildMVP(mvp);
    drawTexturedRGBA(backend_, mvp, v, globalAlpha_, m->ctx->texture);
}

// ── Text ─────────────────────────────────────────────────────────────────

void Canvas2D::parseFontDesc(const std::string &desc)
{
    fontBold_ = desc.find("bold") != std::string::npos;
    fontItalic_ = desc.find("italic") != std::string::npos;
    size_t pxPos = desc.find("px");
    if (pxPos != std::string::npos) {
        size_t start = desc.find_last_of(" ", pxPos);
        start = (start == std::string::npos) ? 0 : start + 1;
        fontSize_ = (float)atof(desc.substr(start, pxPos - start).c_str());
    }
    size_t lastSpace = desc.find_last_of(' ');
    fontFace_ = (lastSpace != std::string::npos) ? desc.substr(lastSpace + 1) : desc;
}

void Canvas2D::setFont(const std::string &fontDesc) { parseFontDesc(fontDesc); }
void Canvas2D::setTextAlign(CanvasTextAlign align) { textAlign_ = align; }
void Canvas2D::setTextBaseline(TextBaseline baseline) { textBaseline_ = baseline; }

int Canvas2D::resolveFont() const
{
    if (!backend_) return -1;
    int idx = backend_->findFont(fontFace_);
    return idx >= 0 ? idx : (backend_->fonts.empty() ? -1 : 0);
}
int Canvas2D::currentFontIdx() const { return resolveFont(); }

float Canvas2D::getKernAdvance(int fontIdx, int cp1, int cp2, int pixelSize) const
{
    return backend_ ? backend_->getKernAdvance(fontIdx, cp1, cp2, pixelSize) : 0.f;
}

float Canvas2D::measureText(const std::string &text)
{
    int fontIdx = resolveFont();
    if (fontIdx < 0 || !backend_) return 0.f;
    float total = 0.f;
    int prevCp = 0;
    for (unsigned char c : text) {
        int cp = c;
        const auto *g = backend_->getGlyph(fontIdx, cp, (int)fontSize_);
        if (g) total += g->advance;
        if (prevCp) total += getKernAdvance(fontIdx, prevCp, cp, (int)fontSize_);
        prevCp = cp;
    }
    return total;
}

static void drawTextInternal(Canvas2D *self, Canvas2DBackend *backend,
                             const std::string &text, float x, float y,
                             Color color, float globalAlpha,
                             const std::string &fontFace, float fontSize,
                             CanvasTextAlign align, TextBaseline baseline,
                             const float mvp[16])
{
    if (!backend || text.empty()) return;
    int fontIdx = backend->findFont(fontFace);
    if (fontIdx < 0) fontIdx = backend->fonts.empty() ? -1 : 0;
    if (fontIdx < 0) return;

    auto &face = backend->fonts[fontIdx];
    if (!face.ready) return;

    float scale = stbtt_ScaleForPixelHeight(&face.info, fontSize);
    int ascent, descent, lineGap;
    stbtt_GetFontVMetrics(&face.info, &ascent, &descent, &lineGap);

    float totalW = self->measureText(text);
    float startX = x;
    if (align == CanvasTextAlign::Center) startX -= totalW * 0.5f;
    else if (align == CanvasTextAlign::Right) startX -= totalW;

    float baselineY = y;
    if (baseline == TextBaseline::Top) baselineY = y + ascent * scale;
    else if (baseline == TextBaseline::Middle) baselineY = y + (ascent + descent) * 0.5f * scale;
    else if (baseline == TextBaseline::Bottom) baselineY = y + descent * scale;

    if (!backend->frameDevice) return;

    float penX = startX;
    int prevCp = 0;
    for (unsigned char c : text) {
        int cp = c;
        int x0, y0, x1, y1;
        stbtt_GetCodepointBitmapBox(&face.info, cp, scale, scale, &x0, &y0, &x1, &y1);
        int w = x1 - x0, h = y1 - y0;

        if (prevCp) penX += stbtt_GetCodepointKernAdvance(&face.info, prevCp, cp) * scale;

        if (w > 0 && h > 0) {
            std::vector<uint8_t> bitmap((size_t)w * h, 0);
            stbtt_MakeCodepointBitmap(&face.info, bitmap.data(), w, h, w, scale, scale, cp);

            MTLTextureDescriptor *td = [MTLTextureDescriptor
                texture2DDescriptorWithPixelFormat:MTLPixelFormatR8Unorm
                                              width:w height:h mipmapped:NO];
            id<MTLTexture> tex = [backend->frameDevice newTextureWithDescriptor:td];
            [tex replaceRegion:MTLRegionMake2D(0,0,w,h) mipmapLevel:0
                    withBytes:bitmap.data() bytesPerRow:w];

            std::vector<CVertex> v;
            pushQuad(v, penX + x0, baselineY + y0, (float)w, (float)h);
            drawTexturedR8(backend, mvp, v, color, globalAlpha, tex);
        }

        int advanceRaw, lsbRaw;
        stbtt_GetCodepointHMetrics(&face.info, cp, &advanceRaw, &lsbRaw);
        penX += advanceRaw * scale;
        prevCp = cp;
    }
}

void Canvas2D::fillText(const std::string &text, float x, float y, float /*maxWidth*/)
{
    float mvp[16]; buildMVP(mvp);
    drawTextInternal(this, backend_, text, x, y, fillColor_, globalAlpha_,
                     fontFace_, fontSize_, textAlign_, textBaseline_, mvp);
}

void Canvas2D::strokeText(const std::string &text, float x, float y, float /*maxWidth*/)
{
    float mvp[16]; buildMVP(mvp);
    drawTextInternal(this, backend_, text, x, y, strokeColor_, globalAlpha_,
                     fontFace_, fontSize_, textAlign_, textBaseline_, mvp);
}

// ── Clip rect ────────────────────────────────────────────────────────────


void Canvas2D::pushClipRect(float x, float y, float w, float h)
{
    if (!backend_ || !backend_->frameEncoder) return;

    // Project the rect's corners through ctm_ (canvas-space transform),
    // then convert NDC -> pixel space using the base frameMVP's own
    // implied viewport extent — same technique as flux_canvas2d_gl.cpp's
    // pushClipRect. Deriving the viewport size from the matrix itself
    // (rather than assuming it equals w_/h_) also happens to route around
    // a mismatch worth flagging separately: flux_canvas_macos.mm builds
    // frameMVP from the window's full physical size plus a baked-in
    // widget offset (physX/physY), while this widget's actual render
    // target (the per-widget CAMetalLayer) is sized only to the widget
    // itself. That offset bakes into every draw call today, not just
    // clipping — outside this fix's scope, but likely worth a follow-up
    // look if canvas content appears mispositioned on multi-widget layouts.
    float x0 = x, y0 = y;
    ctm_.apply(x0, y0);
    float x1 = x + w, y1 = y + h;
    ctm_.apply(x1, y1);

    const float *base = backend_->frameMVP;
    float scaleX = base[0], scaleY = base[5];
    float transX = base[12], transY = base[13];
    float viewW = (scaleX != 0.f) ? 2.f / fabsf(scaleX) : float(w_);
    float viewH = (scaleY != 0.f) ? 2.f / fabsf(scaleY) : float(h_);
    auto toPixX = [&](float cx) { return (scaleX * cx + transX + 1.f) * 0.5f * viewW; };
    auto toPixY = [&](float cy) { return (scaleY * cy + transY + 1.f) * 0.5f * viewH; };

    float px0 = toPixX(x0), py0 = toPixY(y0);
    float px1 = toPixX(x1), py1 = toPixY(y1);
    if (px0 > px1) std::swap(px0, px1);
    if (py0 > py1) std::swap(py0, py1);

    // Clamp into this widget's own render-target bounds — Metal validates
    // that a scissor rect must fit within the framebuffer and will assert
    // otherwise, which the pre-fix version never risked since it only
    // ever used the untransformed x/y/w/h directly.
    px0 = std::max(0.f, std::min(px0, float(w_)));
    py0 = std::max(0.f, std::min(py0, float(h_)));
    px1 = std::max(px0, std::min(px1, float(w_)));
    py1 = std::max(py0, std::min(py1, float(h_)));

    MTLScissorRect r;
    r.x      = (NSUInteger)px0;
    r.y      = (NSUInteger)py0;
    r.width  = (NSUInteger)(px1 - px0);
    r.height = (NSUInteger)(py1 - py0);
    [backend_->frameEncoder setScissorRect:r];
    ++clipDepth_;
}

void Canvas2D::popClipRect()
{
    if (!backend_ || !backend_->frameEncoder) return;
    MTLScissorRect r{0, 0, (NSUInteger)w_, (NSUInteger)h_};
    [backend_->frameEncoder setScissorRect:r];
    if (clipDepth_ > 0) --clipDepth_;
}

// ── Pixel access — TODO: not implemented on this path.
// Reads back pixels from the current frame's render target via a blit
// encoder on backend_->ioQueue, synced with waitUntilCompleted.
//
// LIMITATION: this reads whatever has already been committed to the GPU.
// Metal command buffers execute out-of-order relative to a still-open
// encoder, so draws issued earlier THIS frame, on the encoder that is
// still open when this is called, are not guaranteed visible here yet —
// only content from prior committed frames is reliably read. A full fix
// requires the caller (flux_canvas_macos.mm) to end/commit/reopen its
// encoder mid-frame before this call, which is a larger structural change
// deliberately left as a follow-up rather than folded in silently here.
void Canvas2D::getImageData(float x, float y, float w, float h,
                            std::vector<uint8_t> &out)
{
    out.clear();
    if (!backend_ || !backend_->frameTexture || !backend_->device) return;
    if (w <= 0 || h <= 0) return;

    id<MTLTexture> tex = backend_->frameTexture;
    int texW = (int)tex.width, texH = (int)tex.height;

    int ix = std::max(0, (int)x);
    int iy = std::max(0, (int)y);
    int iw = std::min((int)w, texW - ix);
    int ih = std::min((int)h, texH - iy);
    if (iw <= 0 || ih <= 0) return;

    if (!backend_->ioQueue)
        backend_->ioQueue = [backend_->device newCommandQueue];

    size_t bytesPerRow = size_t(iw) * 4;
    id<MTLBuffer> readbackBuf = [backend_->device
        newBufferWithLength:bytesPerRow * ih
                    options:MTLResourceStorageModeShared];
    if (!readbackBuf) return;

    id<MTLCommandBuffer> cmd = [backend_->ioQueue commandBuffer];
    id<MTLBlitCommandEncoder> blit = [cmd blitCommandEncoder];
    [blit copyFromTexture:tex
               sourceSlice:0
               sourceLevel:0
              sourceOrigin:MTLOriginMake(ix, iy, 0)
                sourceSize:MTLSizeMake(iw, ih, 1)
                  toBuffer:readbackBuf
         destinationOffset:0
    destinationBytesPerRow:bytesPerRow
  destinationBytesPerImage:bytesPerRow * ih];
    [blit endEncoding];
    [cmd commit];
    [cmd waitUntilCompleted];

    const uint8_t *bgra = (const uint8_t *)readbackBuf.contents;
    out.resize(size_t(iw) * ih * 4);
    for (size_t i = 0; i < size_t(iw) * ih; ++i)
    {
        // BGRA8Unorm -> RGBA
        out[i*4+0] = bgra[i*4+2];
        out[i*4+1] = bgra[i*4+1];
        out[i*4+2] = bgra[i*4+0];
        out[i*4+3] = bgra[i*4+3];
    }
}
void Canvas2D::putImageData(const std::vector<uint8_t> &data, int srcW, int srcH,
                            float dx, float dy)
{
    if (data.size() < size_t(srcW) * srcH * 4 || srcW <= 0 || srcH <= 0) return;

    Canvas2DImage *tmp = makeImageFromRGBA(backend_, data.data(), srcW, srcH);
    if (!tmp) return;

    drawImage(tmp, dx, dy);
    freeImage(tmp);
}

#endif // TARGET_OS_OSX
#endif // __APPLE__