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
//                         id<MTLRenderCommandEncoder> encoder, const float mvp[16]);
//   void             Canvas2DBackend_metalEndFrame(Canvas2DBackend *b);
//
#ifdef __APPLE__
#include <TargetConditionals.h>
#if TARGET_OS_OSX

#include "flux/flux_canvas2d.hpp"
#import <Metal/Metal.h>
#import <CoreGraphics/CoreGraphics.h>
#include <cmath>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include <stb_truetype.h>

// ============================================================================
// Shaders
// ============================================================================

static const char *kMSL_Canvas2DMetal = R"MSL(
#include <metal_stdlib>
using namespace metal;

struct VertexIn  { float2 pos [[attribute(0)]]; float2 uv [[attribute(1)]]; };
struct VertexOut { float4 pos [[position]]; float2 uv; };
struct Uniforms  { float4x4 mvp; float4 color; int mode; }; // mode: 0=solid,1=R8 glyph,2=RGBA image

vertex VertexOut c2d_vert(VertexIn in [[stage_in]],
                          constant Uniforms& u [[buffer(1)]]) {
    VertexOut out;
    out.pos = u.mvp * float4(in.pos, 0.0, 1.0);
    out.uv  = in.uv;
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
    } else {
        float4 t = tex.sample(smp, in.uv);
        return t * u.color.a;
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
    float frameMVP[16] = {};
};

// ============================================================================
// Public lifecycle / frame API — the ONLY surface flux_canvas_macos.mm sees.
// ============================================================================

Canvas2DBackend *Canvas2DBackend_create(id<MTLDevice> /*device*/)
{
    return new Canvas2DBackend();
}

void Canvas2DBackend_destroy(Canvas2DBackend *b)
{
    delete b;
}

void Canvas2DBackend_metalBeginFrame(Canvas2DBackend *b,
                                     id<MTLDevice> device,
                                     id<MTLRenderCommandEncoder> encoder,
                                     const float mvp[16])
{
    if (!b) return;
    b->frameEncoder = encoder;
    b->frameDevice = device;
    memcpy(b->frameMVP, mvp, sizeof(b->frameMVP));
}

void Canvas2DBackend_metalEndFrame(Canvas2DBackend *b)
{
    if (!b) return;
    b->frameEncoder = nil;
    b->frameDevice = nil;
}

// ============================================================================
// Canvas2D — Metal implementation
// ============================================================================

Canvas2D::Canvas2D(Canvas2DBackend *backend, int canvasW, int canvasH)
    : backend_(backend), w_(canvasW), h_(canvasH)
{
}

// ── State stack ──────────────────────────────────────────────────────────

void Canvas2D::save()
{
    SaveState s;
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

// ── Transform — TODO: no CTM tracking on this path yet.
void Canvas2D::translate(float, float) { /* TODO */ }
void Canvas2D::scale(float, float) { /* TODO */ }
void Canvas2D::rotate(float) { /* TODO */ }
void Canvas2D::resetTransform() { /* TODO */ }

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

static void drawSolid(Canvas2DBackend *backend, const std::vector<CVertex> &verts,
                      Color color, float globalAlpha)
{
    if (verts.empty() || !backend || !backend->frameEncoder) return;
    auto &res = c2dRes(backend->frameDevice);
    if (!res.pipeline) return;

    [backend->frameEncoder setRenderPipelineState:res.pipeline];
    [backend->frameEncoder setVertexBytes:verts.data() length:verts.size()*sizeof(CVertex) atIndex:0];

    struct { float mvp[16]; float color[4]; int mode; } u;
    memcpy(u.mvp, backend->frameMVP, sizeof(u.mvp));
    u.color[0] = color.r/255.f; u.color[1] = color.g/255.f;
    u.color[2] = color.b/255.f; u.color[3] = (color.a/255.f) * globalAlpha;
    u.mode = 0;
    [backend->frameEncoder setVertexBytes:&u length:sizeof(u) atIndex:1];
    [backend->frameEncoder setFragmentBytes:&u length:sizeof(u) atIndex:1];
    [backend->frameEncoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:verts.size()];
}

static void drawTexturedR8(Canvas2DBackend *backend, const std::vector<CVertex> &verts,
                           Color tint, float globalAlpha, id<MTLTexture> tex)
{
    if (verts.empty() || !tex || !backend || !backend->frameEncoder) return;
    auto &res = c2dRes(backend->frameDevice);
    if (!res.pipeline) return;

    [backend->frameEncoder setRenderPipelineState:res.pipeline];
    [backend->frameEncoder setVertexBytes:verts.data() length:verts.size()*sizeof(CVertex) atIndex:0];

    struct { float mvp[16]; float color[4]; int mode; } u;
    memcpy(u.mvp, backend->frameMVP, sizeof(u.mvp));
    u.color[0] = tint.r/255.f; u.color[1] = tint.g/255.f;
    u.color[2] = tint.b/255.f; u.color[3] = (tint.a/255.f) * globalAlpha;
    u.mode = 1;
    [backend->frameEncoder setVertexBytes:&u length:sizeof(u) atIndex:1];
    [backend->frameEncoder setFragmentBytes:&u length:sizeof(u) atIndex:1];
    [backend->frameEncoder setFragmentTexture:tex atIndex:0];
    [backend->frameEncoder setFragmentSamplerState:res.sampler atIndex:0];
    [backend->frameEncoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:verts.size()];
}

static void drawTexturedRGBA(Canvas2DBackend *backend, const std::vector<CVertex> &verts,
                             float globalAlpha, id<MTLTexture> tex)
{
    if (verts.empty() || !tex || !backend || !backend->frameEncoder) return;
    auto &res = c2dRes(backend->frameDevice);
    if (!res.pipeline) return;

    [backend->frameEncoder setRenderPipelineState:res.pipeline];
    [backend->frameEncoder setVertexBytes:verts.data() length:verts.size()*sizeof(CVertex) atIndex:0];

    struct { float mvp[16]; float color[4]; int mode; } u;
    memcpy(u.mvp, backend->frameMVP, sizeof(u.mvp));
    u.color[0]=u.color[1]=u.color[2]=1.f; u.color[3]=globalAlpha;
    u.mode = 2;
    [backend->frameEncoder setVertexBytes:&u length:sizeof(u) atIndex:1];
    [backend->frameEncoder setFragmentBytes:&u length:sizeof(u) atIndex:1];
    [backend->frameEncoder setFragmentTexture:tex atIndex:0];
    [backend->frameEncoder setFragmentSamplerState:res.sampler atIndex:0];
    [backend->frameEncoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:verts.size()];
}

// ── Primitives ───────────────────────────────────────────────────────────

void Canvas2D::clearRect(float x, float y, float w, float h)
{
    std::vector<CVertex> v;
    pushQuad(v, x, y, w, h);
    drawSolid(backend_, v, Color::fromRGBA(0,0,0,0), 1.f);
}

void Canvas2D::fillRect(float x, float y, float w, float h)
{
    std::vector<CVertex> v;
    pushQuad(v, x, y, w, h);
    drawSolid(backend_, v, fillColor_, globalAlpha_);
}

void Canvas2D::strokeRect(float x, float y, float w, float h)
{
    float lw = lineWidth_;
    std::vector<CVertex> v;
    pushQuad(v, x, y, w, lw);
    pushQuad(v, x, y+h-lw, w, lw);
    pushQuad(v, x, y, lw, h);
    pushQuad(v, x+w-lw, y, lw, h);
    drawSolid(backend_, v, strokeColor_, globalAlpha_);
}

void Canvas2D::fillRoundedRect(float x, float y, float w, float h, float r)
{
    r = std::min(r, std::min(w, h) * 0.5f);
    std::vector<CVertex> v;
    if (r <= 0.f) { pushQuad(v, x, y, w, h); drawSolid(backend_, v, fillColor_, globalAlpha_); return; }

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

    drawSolid(backend_, v, fillColor_, globalAlpha_);
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
    drawSolid(backend_, v, fillColor_, globalAlpha_);
}

void Canvas2D::strokeCircle(float cx, float cy, float r)
{
    std::vector<CVertex> v;
    const int segs = 32;
    float lw = lineWidth_;
    for (int i = 0; i < segs; ++i) {
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
    drawSolid(backend_, v, strokeColor_, globalAlpha_);
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
    drawSolid(backend_, v, fillColor_, globalAlpha_);
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
    drawSolid(backend_, v, strokeColor_, globalAlpha_);
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

Canvas2DImage *Canvas2D::loadImage(const std::string &path)
{
    // TODO: PNG/JPEG decode not wired here yet.
    (void)path;
    return nullptr;
}

Canvas2DImage *Canvas2D::loadImageFromMemory(const unsigned char *data, int byteLen)
{
    (void)data; (void)byteLen;
    return nullptr;
}

void Canvas2D::updateImage(Canvas2DImage *img, const unsigned char *rgba, int w, int h)
{
    auto *m = static_cast<Canvas2DImageMetal *>(img);
    if (!m || !backend_ || !backend_->frameDevice) return;

    if (!m->ctx) m->ctx = new Canvas2DMetalTexContext();
    if (!m->ctx->texture || m->width != w || m->height != h) {
        MTLTextureDescriptor *td = [MTLTextureDescriptor
            texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                          width:w height:h mipmapped:NO];
        m->ctx->texture = [backend_->frameDevice newTextureWithDescriptor:td];
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
    drawTexturedRGBA(backend_, v, globalAlpha_, m->ctx->texture);
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
    drawTexturedRGBA(backend_, v, globalAlpha_, m->ctx->texture);
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
                             CanvasTextAlign align, TextBaseline baseline)
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
            drawTexturedR8(backend, v, color, globalAlpha, tex);
        }

        int advanceRaw, lsbRaw;
        stbtt_GetCodepointHMetrics(&face.info, cp, &advanceRaw, &lsbRaw);
        penX += advanceRaw * scale;
        prevCp = cp;
    }
}

void Canvas2D::fillText(const std::string &text, float x, float y, float /*maxWidth*/)
{
    drawTextInternal(this, backend_, text, x, y, fillColor_, globalAlpha_,
                     fontFace_, fontSize_, textAlign_, textBaseline_);
}

void Canvas2D::strokeText(const std::string &text, float x, float y, float /*maxWidth*/)
{
    drawTextInternal(this, backend_, text, x, y, strokeColor_, globalAlpha_,
                     fontFace_, fontSize_, textAlign_, textBaseline_);
}

// ── Clip rect ────────────────────────────────────────────────────────────

void Canvas2D::pushClipRect(float x, float y, float w, float h)
{
    if (!backend_ || !backend_->frameEncoder) return;
    MTLScissorRect r;
    r.x = (NSUInteger)std::max(0.f, x);
    r.y = (NSUInteger)std::max(0.f, y);
    r.width = (NSUInteger)std::max(0.f, w);
    r.height = (NSUInteger)std::max(0.f, h);
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
void Canvas2D::getImageData(float, float, float, float, std::vector<uint8_t> &out)
{
    out.clear();
}
void Canvas2D::putImageData(const std::vector<uint8_t> &, int, int, float, float)
{
}

#endif // TARGET_OS_OSX
#endif // __APPLE__