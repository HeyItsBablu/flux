#ifdef __APPLE__
#include <TargetConditionals.h>
#if !TARGET_OS_OSX
    #error "flux_painter_macos.mm is for macOS only"
#endif

#include "flux/flux_painter.hpp"
#include "flux/flux_window_macos_state.hpp" // fluxTextScratch / fluxGlyphAtlas bridges
#import <Metal/Metal.h>
#import <CoreText/CoreText.h>
#import <CoreGraphics/CoreGraphics.h>
#include <vector>
#include <cmath>
#include <algorithm>

// ============================================================================
// Shared Metal shaders
//
// painter_solid_frag   — flat color fills/strokes (rects, lines, arcs)
// painter_tex_frag     — RGBA textured quads (images)
// painter_glyph_frag   — R8 (alpha-only) textured quads, tinted by color.rgb
//                        — used for all text, sampling the glyph atlas.
// ============================================================================

static const char *kMSL_Painter = R"MSL(
#include <metal_stdlib>
using namespace metal;

struct VertexIn  { float2 pos [[attribute(0)]]; float2 uv [[attribute(1)]]; };
struct VertexOut { float4 pos [[position]]; float2 uv; };
struct Uniforms  { float4x4 mvp; float4 color; };

vertex VertexOut painter_vert(VertexIn in [[stage_in]],
                              constant Uniforms& u [[buffer(1)]]) {
    VertexOut out;
    out.pos = u.mvp * float4(in.pos, 0.0, 1.0);
    out.uv  = in.uv;
    return out;
}

fragment float4 painter_solid_frag(VertexOut in [[stage_in]],
                                   constant Uniforms& u [[buffer(1)]]) {
    return u.color;
}

fragment float4 painter_tex_frag(VertexOut in [[stage_in]],
                                 constant Uniforms& u [[buffer(1)]],
                                 texture2d<float> tex [[texture(0)]],
                                 sampler smp [[sampler(0)]]) {
    float4 t = tex.sample(smp, in.uv);
    return t * u.color.a;
}

fragment float4 painter_glyph_frag(VertexOut in [[stage_in]],
                                   constant Uniforms& u [[buffer(1)]],
                                   texture2d<float> tex [[texture(0)]],
                                   sampler smp [[sampler(0)]]) {
    float coverage = tex.sample(smp, in.uv).r;
    return float4(u.color.rgb, u.color.a * coverage);
}
)MSL";

// ============================================================================
// Pipeline cache — keyed by device. Stage 1 (shared MacState GPU resources)
// may eventually fold this into MacState directly; kept self-contained here
// so Painter doesn't need MacState beyond the two bridge calls.
// ============================================================================

struct PainterMetalRes
{
    id<MTLDevice> device = nil;
    id<MTLRenderPipelineState> solid = nil;
    id<MTLRenderPipelineState> textured = nil;
    id<MTLRenderPipelineState> glyph = nil;
    id<MTLSamplerState> sampler = nil;
};

static PainterMetalRes &painterRes(id<MTLDevice> device)
{
    static PainterMetalRes res;
    if (res.device == device)
        return res;

    res.device = device;
    NSError *err = nil;
    id<MTLLibrary> lib = [device newLibraryWithSource:
        [NSString stringWithUTF8String:kMSL_Painter] options:nil error:&err];
    if (!lib)
    {
        fprintf(stderr, "flux_painter_macos: shader compile failed: %s\n",
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

    auto makePipeline = [&](NSString *fragName) -> id<MTLRenderPipelineState> {
        MTLRenderPipelineDescriptor *desc = [[MTLRenderPipelineDescriptor alloc] init];
        desc.vertexFunction = [lib newFunctionWithName:@"painter_vert"];
        desc.fragmentFunction = [lib newFunctionWithName:fragName];
        desc.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
        desc.colorAttachments[0].blendingEnabled = YES;
        desc.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
        desc.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        desc.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOne;
        desc.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        desc.vertexDescriptor = vd;
        NSError *e = nil;
        id<MTLRenderPipelineState> ps = [device newRenderPipelineStateWithDescriptor:desc error:&e];
        if (!ps)
            fprintf(stderr, "flux_painter_macos: pipeline '%s' failed: %s\n",
                    [fragName UTF8String], [[e localizedDescription] UTF8String]);
        return ps;
    };

    res.solid    = makePipeline(@"painter_solid_frag");
    res.textured = makePipeline(@"painter_tex_frag");
    res.glyph    = makePipeline(@"painter_glyph_frag");

    MTLSamplerDescriptor *sd = [[MTLSamplerDescriptor alloc] init];
    sd.minFilter = MTLSamplerMinMagFilterLinear;
    sd.magFilter = MTLSamplerMinMagFilterLinear;
    res.sampler = [device newSamplerStateWithDescriptor:sd];

    return res;
}

// ============================================================================
// Vertex / draw helpers
// ============================================================================

struct PVertex { float x, y, u, v; };
struct Uniforms { float mvp[16]; float color[4]; };

static id<MTLDevice> dev(GraphicsContext &ctx) { return (__bridge id<MTLDevice>)ctx.mtlDevice; }
static id<MTLRenderCommandEncoder> enc(GraphicsContext &ctx) { return (__bridge id<MTLRenderCommandEncoder>)ctx.mtlEncoder; }

static void drawTriList(GraphicsContext &ctx, const std::vector<PVertex> &verts,
                        Color color, id<MTLRenderPipelineState> pipeline)
{
    if (verts.empty()) return;
    id<MTLRenderCommandEncoder> e = enc(ctx);
    if (!e || !pipeline) return;

    [e setRenderPipelineState:pipeline];
    [e setVertexBytes:verts.data() length:verts.size() * sizeof(PVertex) atIndex:0];

    Uniforms u;
    memcpy(u.mvp, ctx.mvp, sizeof(u.mvp));
    u.color[0] = color.r / 255.f; u.color[1] = color.g / 255.f;
    u.color[2] = color.b / 255.f; u.color[3] = color.a / 255.f;
    [e setVertexBytes:&u length:sizeof(u) atIndex:1];
    [e setFragmentBytes:&u length:sizeof(u) atIndex:1];

    [e drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:verts.size()];
}

static void drawTexturedTriList(GraphicsContext &ctx, const std::vector<PVertex> &verts,
                                Color tint, id<MTLTexture> tex,
                                id<MTLRenderPipelineState> pipeline)
{
    if (verts.empty() || !tex) return;
    id<MTLRenderCommandEncoder> e = enc(ctx);
    if (!e || !pipeline) return;

    auto &res = painterRes(dev(ctx));
    [e setRenderPipelineState:pipeline];
    [e setVertexBytes:verts.data() length:verts.size() * sizeof(PVertex) atIndex:0];

    Uniforms u;
    memcpy(u.mvp, ctx.mvp, sizeof(u.mvp));
    u.color[0] = tint.r / 255.f; u.color[1] = tint.g / 255.f;
    u.color[2] = tint.b / 255.f; u.color[3] = tint.a / 255.f;
    [e setVertexBytes:&u length:sizeof(u) atIndex:1];
    [e setFragmentBytes:&u length:sizeof(u) atIndex:1];
    [e setFragmentTexture:tex atIndex:0];
    [e setFragmentSamplerState:res.sampler atIndex:0];

    [e drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:verts.size()];
}

static void quad(std::vector<PVertex> &out, float x, float y, float w, float h,
                 float u0 = 0, float v0 = 0, float u1 = 1, float v1 = 1)
{
    out.push_back({x,     y,     u0, v0});
    out.push_back({x + w, y,     u1, v0});
    out.push_back({x,     y + h, u0, v1});
    out.push_back({x + w, y,     u1, v0});
    out.push_back({x + w, y + h, u1, v1});
    out.push_back({x,     y + h, u0, v1});
}

static void strokeSegment(std::vector<PVertex> &out, float x1, float y1,
                          float x2, float y2, float width)
{
    float dx = x2 - x1, dy = y2 - y1;
    float len = sqrtf(dx*dx + dy*dy);
    if (len < 0.0001f) return;
    float nx = -dy / len * (width * 0.5f);
    float ny =  dx / len * (width * 0.5f);
    out.push_back({x1+nx, y1+ny, 0,0});
    out.push_back({x1-nx, y1-ny, 0,0});
    out.push_back({x2+nx, y2+ny, 0,0});
    out.push_back({x2+nx, y2+ny, 0,0});
    out.push_back({x1-nx, y1-ny, 0,0});
    out.push_back({x2-nx, y2-ny, 0,0});
}

// ============================================================================
// Basic fills
// ============================================================================

void Painter::fillRect(int x, int y, int w, int h, Color color)
{
    auto &res = painterRes(dev(ctx));
    std::vector<PVertex> v;
    quad(v, (float)x, (float)y, (float)w, (float)h);
    drawTriList(ctx, v, color, res.solid);
}

void Painter::fillRectAlpha(int x, int y, int w, int h, Color color)
{
    fillRect(x, y, w, h, color); // alpha already carried by Color.a
}

void Painter::fillRoundedRect(int x, int y, int w, int h, int radius, Color color)
{
    auto &res = painterRes(dev(ctx));
    std::vector<PVertex> v;
    float r = std::min((float)radius, std::min(w, h) * 0.5f);

    if (r <= 0.f) { quad(v, x, y, w, h); drawTriList(ctx, v, color, res.solid); return; }

    quad(v, x + r, y,         w - 2*r, r);
    quad(v, x + r, y + h - r, w - 2*r, r);
    quad(v, x,     y + r,     w,       h - 2*r);

    auto corner = [&](float cx, float cy, float a0, float a1) {
        const int segs = 12;
        for (int i = 0; i < segs; ++i) {
            float t0 = a0 + (a1 - a0) * (i / (float)segs);
            float t1 = a0 + (a1 - a0) * ((i + 1) / (float)segs);
            v.push_back({cx, cy, 0, 0});
            v.push_back({cx + r * cosf(t0), cy + r * sinf(t0), 0, 0});
            v.push_back({cx + r * cosf(t1), cy + r * sinf(t1), 0, 0});
        }
    };
    const float PI = 3.14159265f;
    corner(x + r,     y + r,     PI,      1.5f*PI);
    corner(x + w - r, y + r,     1.5f*PI, 2.f*PI);
    corner(x + w - r, y + h - r, 0,       0.5f*PI);
    corner(x + r,     y + h - r, 0.5f*PI, PI);

    drawTriList(ctx, v, color, res.solid);
}

void Painter::fillRoundedRegion(int x, int y, int w, int h, int r, Color c) {
    fillRoundedRect(x, y, w, h, r, c);
}

void Painter::fillRectWithLeftAccent(int x, int y, int w, int h,
                                     Color bg, Color accent, int stripWidth) {
    fillRect(x, y, w, h, bg);
    fillRect(x, y, stripWidth, h, accent);
}

void Painter::fillRoundedRectGDI(int x, int y, int w, int h, int radius,
                                 Color fill, Color stroke, int strokeWidth) {
    fillRoundedRect(x, y, w, h, radius / 2, fill);
    if (strokeWidth > 0)
        drawRoundedRectOutline(x, y, w, h, radius, stroke, strokeWidth);
}

void Painter::fillColumnBars(int x, int y, int w, int h,
                             const std::vector<int> &barHeights, Color color) {
    int cols = std::min(w, (int)barHeights.size());
    for (int i = 0; i < cols; ++i) {
        int bh = std::max(0, std::min(h, barHeights[i]));
        fillRect(x + i, y + h - bh, 1, bh, color);
    }
}

void Painter::fillPolygonAlpha(const std::vector<std::pair<int,int>> &points, Color color)
{
    if (points.size() < 3) return;
    auto &res = painterRes(dev(ctx));
    std::vector<PVertex> v;
    for (size_t i = 1; i + 1 < points.size(); ++i) {
        v.push_back({(float)points[0].first, (float)points[0].second, 0, 0});
        v.push_back({(float)points[i].first, (float)points[i].second, 0, 0});
        v.push_back({(float)points[i+1].first, (float)points[i+1].second, 0, 0});
    }
    drawTriList(ctx, v, color, res.solid);
}

// ============================================================================
// Strokes
// ============================================================================

void Painter::drawLine(int x1, int y1, int x2, int y2, Color color, int width)
{
    auto &res = painterRes(dev(ctx));
    std::vector<PVertex> v;
    strokeSegment(v, (float)x1, (float)y1, (float)x2, (float)y2, (float)width);
    drawTriList(ctx, v, color, res.solid);
}
void Painter::drawHLine(int x, int y, int len, Color c, int sw) { drawLine(x, y, x+len, y, c, sw); }
void Painter::drawVLine(int x, int y, int len, Color c, int sw) { drawLine(x, y, x, y+len, c, sw); }

void Painter::drawPolyline(const std::vector<std::pair<int,int>> &points,
                           Color color, int strokeWidth)
{
    if (points.size() < 2) return;
    auto &res = painterRes(dev(ctx));
    std::vector<PVertex> v;
    for (size_t i = 1; i < points.size(); ++i)
        strokeSegment(v, (float)points[i-1].first, (float)points[i-1].second,
                     (float)points[i].first, (float)points[i].second, (float)strokeWidth);
    drawTriList(ctx, v, color, res.solid);
}

void Painter::drawBorder(int x, int y, int w, int h, int radius,
                         Color color, int borderWidth)
{
    if (radius <= 0) {
        drawHLine(x, y, w, color, borderWidth);
        drawHLine(x, y + h - borderWidth, w, color, borderWidth);
        drawVLine(x, y, h, color, borderWidth);
        drawVLine(x + w - borderWidth, y, h, color, borderWidth);
        return;
    }
    drawRoundedRectOutline(x, y, w, h, radius * 2, color, borderWidth);
}

void Painter::drawRectOutline(int x, int y, int w, int h, Color c, int sw) {
    drawBorder(x, y, w, h, 0, c, sw);
}

void Painter::drawRoundedRectOutline(int x, int y, int w, int h,
                                     int cornerDiameter, Color stroke, int strokeWidth)
{
    // TODO (Stage 3 polish): proper mitered outline via stencil-punched
    // double fillRoundedRect. Interim: straight edges + arc strokes.
    int r = cornerDiameter / 2;
    drawHLine(x + r, y, w - 2*r, stroke, strokeWidth);
    drawHLine(x + r, y + h, w - 2*r, stroke, strokeWidth);
    drawVLine(x, y + r, h - 2*r, stroke, strokeWidth);
    drawVLine(x + w, y + r, h - 2*r, stroke, strokeWidth);
    drawArc(x + r,     y + r,     r, strokeWidth, 180, 90, stroke, false);
    drawArc(x + w - r, y + r,     r, strokeWidth, 270, 90, stroke, false);
    drawArc(x + w - r, y + h - r, r, strokeWidth, 0,   90, stroke, false);
    drawArc(x + r,     y + h - r, r, strokeWidth, 90,  90, stroke, false);
}

void Painter::drawArc(float cx, float cy, float radius, int strokeWidth,
                      float startAngle, float sweepAngle, Color color, bool /*roundedCaps*/)
{
    auto &res = painterRes(dev(ctx));
    std::vector<PVertex> v;
    const int segs = std::max(4, (int)(fabsf(sweepAngle) / 6.f));
    const float DEG2RAD = 3.14159265f / 180.f;
    float prevX = cx + radius * cosf(startAngle * DEG2RAD);
    float prevY = cy + radius * sinf(startAngle * DEG2RAD);
    for (int i = 1; i <= segs; ++i) {
        float a = (startAngle + sweepAngle * (i / (float)segs)) * DEG2RAD;
        float px = cx + radius * cosf(a), py = cy + radius * sinf(a);
        strokeSegment(v, prevX, prevY, px, py, (float)strokeWidth);
        prevX = px; prevY = py;
    }
    drawTriList(ctx, v, color, res.solid);
}

void Painter::drawEllipse(int x, int y, int w, int h, Color fill, Color stroke, int strokeWidth)
{
    float cx = x + w * 0.5f, cy = y + h * 0.5f;
    auto &res = painterRes(dev(ctx));
    std::vector<PVertex> v;
    const int segs = 32;
    for (int i = 0; i < segs; ++i) {
        float t0 = (i / (float)segs) * 2.f * 3.14159265f;
        float t1 = ((i+1) / (float)segs) * 2.f * 3.14159265f;
        v.push_back({cx, cy, 0, 0});
        v.push_back({cx + (w*0.5f)*cosf(t0), cy + (h*0.5f)*sinf(t0), 0, 0});
        v.push_back({cx + (w*0.5f)*cosf(t1), cy + (h*0.5f)*sinf(t1), 0, 0});
    }
    drawTriList(ctx, v, fill, res.solid);
    if (strokeWidth > 0) {
        std::vector<PVertex> sv;
        for (int i = 0; i < segs; ++i) {
            float t0 = (i / (float)segs) * 2.f * 3.14159265f;
            float t1 = ((i+1) / (float)segs) * 2.f * 3.14159265f;
            strokeSegment(sv, cx+(w*0.5f)*cosf(t0), cy+(h*0.5f)*sinf(t0),
                         cx+(w*0.5f)*cosf(t1), cy+(h*0.5f)*sinf(t1), (float)strokeWidth);
        }
        drawTriList(ctx, sv, stroke, res.solid);
    }
}

void Painter::drawWavyLine(int x, int y, int len, Color color, int amplitude)
{
    if (len <= 0) return;
    std::vector<std::pair<int,int>> pts;
    int step = amplitude * 2, px = x; bool up = true;
    while (px < x + len) {
        pts.push_back({px, up ? y - amplitude : y + amplitude});
        px += step; up = !up;
    }
    pts.push_back({x + len, y});
    drawPolyline(pts, color, 1);
}

// ============================================================================
// Clip — axis-aligned via Metal scissor rect. Rounded clip deferred (TODO).
// ============================================================================

void Painter::pushClipRect(int x, int y, int w, int h, int cornerRadius)
{
    if (cornerRadius > 0) {
        // TODO: stencil-based rounded clip. Falls back to bounding-box
        // clip for now — safe (no overdraw beyond bounds), slightly
        // imprecise at rounded corners.
    }
    id<MTLRenderCommandEncoder> e = enc(ctx);
    if (!e) return;
    MTLScissorRect r;
    r.x = (NSUInteger)std::max(0, x);
    r.y = (NSUInteger)std::max(0, y);
    r.width  = (NSUInteger)std::max(0, w);
    r.height = (NSUInteger)std::max(0, h);
    [e setScissorRect:r];
}

void Painter::popClipRect()
{
    // TODO: real clip stack (host it on MacState alongside the encoder so
    // nested push/pop restores the *previous* rect, not always full-frame).
    id<MTLRenderCommandEncoder> e = enc(ctx);
    if (!e) return;
    MTLScissorRect r{0, 0, (NSUInteger)ctx.width, (NSUInteger)ctx.height};
    [e setScissorRect:r];
}

void Painter::pushClipRoundedRect(int x, int y, int w, int h, int cornerDiameter)
{
    pushClipRect(x, y, w, h, cornerDiameter / 2);
}

// ============================================================================
// Gradient — TODO: multi-stop needs a 1D LUT texture or per-vertex color
// interpolation in a dedicated shader. Approximated with a 2-color blend.
// ============================================================================

void Painter::fillGradientRect(int x, int y, int w, int h, const std::vector<Color> &colors)
{
    if (colors.empty() || w <= 0 || h <= 0) return;
    if (colors.size() == 1) { fillRect(x, y, w, h, colors[0]); return; }
    fillRect(x, y, w, h, colors.front().interpolate(colors.back(), 0.5));
}

void Painter::drawFadeOverlay(int x, int y, int w, int h, int fadeWidth, Color bg)
{
    if (fadeWidth <= 0 || w <= 0 || h <= 0) return;
    int startX = std::max(x, x + w - fadeWidth);
    fillGradientRect(startX, y, fadeWidth, h, { bg.withAlpha(0), bg.withAlpha(255) });
}

// ============================================================================
// Layers / shadows — deferred (need offscreen render target + composite,
// and a blur kernel respectively). Not blocking vector/text migration.
// ============================================================================

void Painter::beginLayer(float /*opacity*/) { /* TODO */ }
void Painter::endLayer() { /* TODO */ }
void Painter::drawShadow(int, int, int, int, int, int, Color, int, int) { /* TODO */ }

// ============================================================================
// drawTextDecorationLine — underline/strikethrough as explicit geometry.
// Previously implicit via CTLine attributes; now drawn manually since we
// render glyphs ourselves rather than handing the whole line to CTLineDraw.
// ============================================================================

void Painter::drawTextDecorationLine(int lineX, int lineY, int lineW,
                                     const TextStyle & /*style*/,
                                     TextDecoration /*which*/)
{
    // Generic 1px solid line — callers (drawRichTextA) compute lineY per
    // decoration type (underline/strikethrough) from font metrics and call
    // this once per decoration. Style-specific (dotted/wavy/double) left
    // as a follow-up; solid covers the common case.
    drawHLine(lineX, lineY, lineW, Color::fromRGB(0, 0, 0), 1);
}

// ============================================================================
// Text — Stage 2: glyph-atlas based. CoreText is used ONLY for shaping
// (CTLineCreateWithAttributedString + CTRun glyph/position extraction) and
// measurement; no CG/CTLineDraw rendering happens here anymore.
// ============================================================================

namespace {

struct ShapedGlyphRun
{
    CTFontRef font; // borrowed from the run's attributes
    std::vector<CGGlyph> glyphs;
    std::vector<CGPoint> positions; // pen-space, baseline-relative, from CTLine
};

// Shape a CFAttributedStringRef into per-run glyph/position arrays.
std::vector<ShapedGlyphRun> shapeLine(CTLineRef line)
{
    std::vector<ShapedGlyphRun> runs;
    CFArrayRef ctRuns = CTLineGetGlyphRuns(line);
    CFIndex runCount = CFArrayGetCount(ctRuns);

    for (CFIndex i = 0; i < runCount; ++i)
    {
        CTRunRef run = (CTRunRef)CFArrayGetValueAtIndex(ctRuns, i);
        CFIndex count = CTRunGetGlyphCount(run);
        if (count == 0) continue;

        CFDictionaryRef attrs = CTRunGetAttributes(run);
        CTFontRef font = (CTFontRef)CFDictionaryGetValue(attrs, kCTFontAttributeName);
        if (!font) continue;

        ShapedGlyphRun sr;
        sr.font = font;
        sr.glyphs.resize(count);
        sr.positions.resize(count);
        CTRunGetGlyphs(run, CFRangeMake(0, count), sr.glyphs.data());
        CTRunGetPositions(run, CFRangeMake(0, count), sr.positions.data());
        runs.push_back(std::move(sr));
    }
    return runs;
}

// Emit textured quads for one shaped line into `verts`, anchored at
// (originX, originY) in top-left pixel space (CoreText positions are
// baseline-relative with y-up; we flip here).
void emitGlyphQuads(GlyphAtlas &atlas, const std::vector<ShapedGlyphRun> &runs,
                    float originX, float originY, std::vector<PVertex> &verts)
{
    for (const auto &run : runs)
    {
        for (size_t i = 0; i < run.glyphs.size(); ++i)
        {
            const GlyphEntry *entry = atlas.getOrInsert(run.font, run.glyphs[i]);
            if (!entry || entry->width <= 0.f || entry->height <= 0.f)
                continue; // atlas full, or whitespace glyph (correctly skipped)

            float penX = originX + (float)run.positions[i].x;
            float penY = originY - (float)run.positions[i].y; // flip: CT y-up -> our y-down

            float qx = penX + entry->offsetX;
            float qy = penY - entry->offsetY - entry->height; // bbox origin is glyph-bottom in CT space

            quad(verts, qx, qy, entry->width, entry->height,
                entry->uvMinX, entry->uvMinY, entry->uvMaxX, entry->uvMaxY);
        }
    }
}

} // anonymous namespace

void Painter::drawTextA(const std::string &text, int x, int y, int w, int h,
                        NativeFont font, Color color, UINT format)
{
    if (text.empty() || !ctx.textScratchProvider) return;
    GlyphAtlas *atlas = fluxGlyphAtlas(ctx.textScratchProvider);
    if (!atlas) return;

    CTFontRef ctFont = reinterpret_cast<CTFontRef>(font);
    CFStringRef str = CFStringCreateWithCString(nullptr, text.c_str(), kCFStringEncodingUTF8);
    CFStringRef keys[] = { kCTFontAttributeName };
    CFTypeRef vals[] = { ctFont };
    CFDictionaryRef attrs = CFDictionaryCreate(nullptr, (const void**)keys, (const void**)vals, 1,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFAttributedStringRef attrStr = CFAttributedStringCreate(nullptr, str, attrs);
    CTLineRef line = CTLineCreateWithAttributedString(attrStr);

    double lineWidth = CTLineGetTypographicBounds(line, nullptr, nullptr, nullptr);
    CGFloat ascent = 0;
    CTLineGetTypographicBounds(line, &ascent, nullptr, nullptr);

    float drawX = (float)x;
    if (format & DT_CENTER) drawX = x + (w - (float)lineWidth) * 0.5f;
    else if (format & DT_RIGHT) drawX = x + w - (float)lineWidth;

    float drawY = (format & DT_VCENTER) ? (y + (h - ascent) * 0.5f) : (y + h - ascent);

    auto runs = shapeLine(line);
    std::vector<PVertex> verts;
    // originY passed as the baseline's y in top-left space: drawY currently
    // represents the top of the ascent box, so the baseline sits at
    // drawY + ascent.
    emitGlyphQuads(*atlas, runs, drawX, drawY + (float)ascent, verts);

    if (!verts.empty())
    {
        auto &res = painterRes(dev(ctx));
        drawTexturedTriList(ctx, verts, color, atlas->texture(), res.glyph);
    }

    CFRelease(line); CFRelease(attrStr); CFRelease(attrs); CFRelease(str);
}

void Painter::drawText(const std::wstring &text, int x, int y, int w, int h,
                       NativeFont font, Color color, UINT format)
{
    if (text.empty()) return;
    std::string utf8;
    for (wchar_t wc : text) {
        uint32_t cp = wc;
        if (cp < 0x80) utf8 += (char)cp;
        else if (cp < 0x800) { utf8 += (char)(0xC0|(cp>>6)); utf8 += (char)(0x80|(cp&0x3F)); }
        else { utf8 += (char)(0xE0|(cp>>12)); utf8 += (char)(0x80|((cp>>6)&0x3F)); utf8 += (char)(0x80|(cp&0x3F)); }
    }
    drawTextA(utf8, x, y, w, h, font, color, format);
}

void Painter::measureText(const std::wstring &text, NativeFont font,
                          int &outWidth, int &outHeight)
{
    if (text.empty()) { outWidth = outHeight = 0; return; }
    std::string utf8;
    for (wchar_t wc : text) {
        uint32_t cp = wc;
        if (cp < 0x80) utf8 += (char)cp;
        else if (cp < 0x800) { utf8 += (char)(0xC0|(cp>>6)); utf8 += (char)(0x80|(cp&0x3F)); }
        else { utf8 += (char)(0xE0|(cp>>12)); utf8 += (char)(0x80|((cp>>6)&0x3F)); utf8 += (char)(0x80|(cp&0x3F)); }
    }

    CTFontRef ctFont = reinterpret_cast<CTFontRef>(font);
    CFStringRef str = CFStringCreateWithCString(nullptr, utf8.c_str(), kCFStringEncodingUTF8);
    CFStringRef keys[] = { kCTFontAttributeName };
    CFTypeRef vals[] = { ctFont };
    CFDictionaryRef attrs = CFDictionaryCreate(nullptr, (const void**)keys, (const void**)vals, 1,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFAttributedStringRef attrStr = CFAttributedStringCreate(nullptr, str, attrs);
    CTLineRef line = CTLineCreateWithAttributedString(attrStr);

    CGFloat ascent, descent, leading;
    outWidth  = (int)CTLineGetTypographicBounds(line, &ascent, &descent, &leading);
    outHeight = (int)(ascent + descent + leading);

    CFRelease(line); CFRelease(attrStr); CFRelease(attrs); CFRelease(str);
}

// ============================================================================
// Rich text — single TextStyle applied to the whole run (matches
// RichTextParams's existing shape: one `style` field, not per-span).
// Decorations (underline/strikethrough) drawn manually via
// drawTextDecorationLine since CTLineDraw no longer renders anything.
// ============================================================================

void Painter::drawRichTextA(const std::string &text,
                            const RichTextParams &params,
                            FontCache &fontCache)
{
    if (text.empty() || !ctx.textScratchProvider) return;
    GlyphAtlas *atlas = fluxGlyphAtlas(ctx.textScratchProvider);
    if (!atlas) return;

    NativeFont nf = fontCache.getFont(
        params.style.fontFamily.empty() ? "Helvetica" : params.style.fontFamily,
        params.style.fontSize, params.style.fontWeight);
    CTFontRef ctFont = reinterpret_cast<CTFontRef>(nf);

    CFStringRef str = CFStringCreateWithCString(nullptr, text.c_str(), kCFStringEncodingUTF8);
    CFStringRef keys[] = { kCTFontAttributeName };
    CFTypeRef vals[] = { ctFont };
    CFDictionaryRef attrs = CFDictionaryCreate(nullptr, (const void**)keys, (const void**)vals, 1,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFAttributedStringRef attrStr = CFAttributedStringCreate(nullptr, str, attrs);

    // NOTE: softWrap / maxLines multi-line layout is not implemented in this
    // pass — single CTLine only, matching what the old CG implementation's
    // stub also left unhandled (drawRichText/A were empty no-ops before
    // Stage 2; this is new functionality, not a regression).
    CTLineRef line = CTLineCreateWithAttributedString(attrStr);

    CGFloat ascent = 0, descent = 0;
    double lineWidth = CTLineGetTypographicBounds(line, &ascent, &descent, nullptr);

    float drawX = (float)params.x;
    if (params.textAlign == TextAlign::Center) drawX = params.x + (params.w - (float)lineWidth) * 0.5f;
    else if (params.textAlign == TextAlign::Right) drawX = params.x + params.w - (float)lineWidth;

    float drawY = (params.textAlignVertical == TextAlignVertical::Center)
        ? params.y + (params.h - (ascent + descent)) * 0.5f
        : params.y;

    auto runs = shapeLine(line);
    std::vector<PVertex> verts;
    emitGlyphQuads(*atlas, runs, drawX, drawY + (float)ascent, verts);

    if (!verts.empty())
    {
        auto &res = painterRes(dev(ctx));
        drawTexturedTriList(ctx, verts, params.style.color, atlas->texture(), res.glyph);
    }

    if (hasDecoration(params.style.decoration, TextDecoration::Underline))
    {
        CGFloat underlinePos = CTFontGetUnderlinePosition(ctFont);
        CGFloat underlineThk = std::max((CGFloat)1.0, CTFontGetUnderlineThickness(ctFont));
        int lineY = (int)(drawY + ascent - underlinePos);
        drawHLine((int)drawX, lineY, (int)lineWidth, params.style.color, (int)underlineThk);
    }
    if (hasDecoration(params.style.decoration, TextDecoration::LineThrough))
    {
        int lineY = (int)(drawY + ascent * 0.6f); // ~strikethrough height, no direct CTFont accessor
        drawHLine((int)drawX, lineY, (int)lineWidth, params.style.color, 1);
    }

    CFRelease(line); CFRelease(attrStr); CFRelease(attrs); CFRelease(str);
}

void Painter::drawRichText(const std::wstring &text,
                           const RichTextParams &params,
                           FontCache &fontCache)
{
    if (text.empty()) return;
    std::string utf8;
    for (wchar_t wc : text) {
        uint32_t cp = wc;
        if (cp < 0x80) utf8 += (char)cp;
        else if (cp < 0x800) { utf8 += (char)(0xC0|(cp>>6)); utf8 += (char)(0x80|(cp&0x3F)); }
        else { utf8 += (char)(0xE0|(cp>>12)); utf8 += (char)(0x80|((cp>>6)&0x3F)); utf8 += (char)(0x80|(cp&0x3F)); }
    }
    drawRichTextA(utf8, params, fontCache);
}

void Painter::measureRichText(const std::wstring &text,
                              const TextStyle &style,
                              FontCache &fontCache,
                              int maxWidth, bool /*softWrap*/, int /*maxLines*/,
                              int &outWidth, int &outHeight)
{
    if (text.empty()) { outWidth = outHeight = 0; return; }
    NativeFont nf = fontCache.getFont(
        style.fontFamily.empty() ? "Helvetica" : style.fontFamily,
        style.fontSize, style.fontWeight);
    measureText(text, nf, outWidth, outHeight);
    if (maxWidth > 0) outWidth = std::min(outWidth, maxWidth);
}

// ============================================================================
// Images
// ============================================================================

void Painter::drawImage(const ImageDrawParams &params)
{
    if (!params.image || params.clipW <= 0 || params.clipH <= 0) return;
    auto &res = painterRes(dev(ctx));

    // NativeImage is expected to carry an id<MTLTexture> on macOS once the
    // image-loading path is ported to Metal (currently a loose end flagged
    // in Stage 3 — NativeImage is still void* and nothing populates it on
    // this platform yet).
    id<MTLTexture> tex = (__bridge id<MTLTexture>)(void*)params.image;

    std::vector<PVertex> v;
    quad(v, params.destX, params.destY, params.destW, params.destH);
    drawTexturedTriList(ctx, v, Color::fromRGBA(255,255,255, 255), tex, res.textured);
}

void Painter::drawVideo(const VideoDrawParams &params)
{
    if (!params.frame || params.dstW <= 0 || params.dstH <= 0) return;
    auto &res = painterRes(dev(ctx));
    id<MTLTexture> tex = (__bridge id<MTLTexture>)(void*)params.frame;
    std::vector<PVertex> v;
    quad(v, (float)params.dstX, (float)params.dstY, (float)params.dstW, (float)params.dstH);
    drawTexturedTriList(ctx, v, Color::fromRGBA(255,255,255,255), tex, res.textured);
}

void Painter::drawCamera(const CameraDrawParams &params)
{
    if (!params.frame || params.dstW <= 0 || params.dstH <= 0) return;
    auto &res = painterRes(dev(ctx));
    id<MTLTexture> tex = (__bridge id<MTLTexture>)(void*)params.frame;
    std::vector<PVertex> v;
    float u0 = params.mirror ? 1.f : 0.f, u1 = params.mirror ? 0.f : 1.f;
    quad(v, (float)params.dstX, (float)params.dstY, (float)params.dstW, (float)params.dstH,
        u0, 0.f, u1, 1.f);
    drawTexturedTriList(ctx, v, Color::fromRGBA(255,255,255,255), tex, res.textured);
}

// ============================================================================
// Page (header/body/footer regions) — composition of fillRect + simple
// elevation gradients, same structure as prior CG version.
// ============================================================================

void Painter::drawPage(const PageDrawParams &params)
{
    if (params.hasPageBackground)
        fillRect(params.x, params.y, params.w, params.h, params.pageBackground);

    if (params.body.present && params.body.hasBackground)
        fillRect(params.body.x, params.body.y, params.body.w, params.body.h,
                 params.body.background);

    if (params.header.present)
    {
        if (params.header.hasBackground)
            fillRect(params.header.x, params.header.y, params.header.w, params.header.h,
                     params.header.background);
        if (params.header.elevation > 0)
        {
            Color from = Color::fromRGB(0, 0, 0).withAlpha(60);
            Color to = Color::fromRGB(0, 0, 0).withAlpha(0);
            int shadowY = params.header.y + params.header.h;
            for (int i = 0; i < params.header.elevation; ++i)
            {
                double t = (double)i / params.header.elevation;
                fillRect(params.header.x, shadowY + i, params.header.w, 1,
                         from.interpolate(to, t));
            }
        }
    }

    if (params.footer.present)
    {
        if (params.footer.hasBackground)
            fillRect(params.footer.x, params.footer.y, params.footer.w, params.footer.h,
                     params.footer.background);
        if (params.footer.elevation > 0)
        {
            Color from = Color::fromRGB(0, 0, 0).withAlpha(0);
            Color to = Color::fromRGB(0, 0, 0).withAlpha(60);
            int startY = params.footer.y - params.footer.elevation;
            for (int i = 0; i < params.footer.elevation; ++i)
            {
                double t = (double)i / params.footer.elevation;
                fillRect(params.footer.x, startY + i, params.footer.w, 1,
                         from.interpolate(to, t));
            }
        }
    }
}

// ============================================================================
// Scrollbar — now drawn through the same Metal solid pipeline as everything
// else, rather than the separate CG-via-Painter detour the old
// CanvasWidget::glRenderPass used at the end of its frame.
// ============================================================================

void Painter::drawScrollbar(const CustomScrollbar &bar, int glW, int glH)
{
    if (!bar.isVisible() || bar.alpha() < 0.005f)
        return;

    float alpha = bar.alpha();

    {
        auto [tx, ty, tw, th] = bar.trackRect(glW, glH);
        fillRect((int)tx, (int)ty, (int)tw, (int)th,
                 Color::fromRGBA(20, 20, 20, (uint8_t)(alpha * 0.30f * 255)));
    }
    {
        auto [tx, ty, tw, th] = bar.thumbRect(glW, glH);
        int r = (int)(std::min(tw, th) * 0.5f);
        fillRoundedRect((int)tx, (int)ty, (int)tw, (int)th, r,
                        Color::fromRGBA(194, 194, 194, (uint8_t)(alpha * 255)));
    }
}

#endif // TARGET_OS_OSX
#endif // __APPLE__