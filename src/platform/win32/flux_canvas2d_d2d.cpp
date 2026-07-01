// ============================================================================
// flux_canvas2d_d2d.cpp
// Direct2D implementation of Canvas2D for Win32.
//
//
// Lifecycle / offscreen-target access exposed to flux_canvas_win32.cpp as
// free functions (declared `extern` locally in that file):
//
//   Canvas2DBackend    *Canvas2DBackend_create(D3DDevice *dev);
//   void                Canvas2DBackend_destroy(Canvas2DBackend *b);
//   bool                Canvas2DBackend_resizeBitmap(Canvas2DBackend *b, int w, int h);
//   ID2D1DeviceContext  *Canvas2DBackend_offscreenDC(Canvas2DBackend *b);
//   ID2D1Bitmap1        *Canvas2DBackend_offscreenBitmap(Canvas2DBackend *b);
//
// Font rasterization: stb_truetype (matches all other backends).
// ============================================================================

#ifdef _WIN32

#define _USE_MATH_DEFINES
#include "flux/flux_canvas2d.hpp"
#include "flux/flux_d3d_device.hpp"

#include <cmath>
#include <cassert>
#include <cstring>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <unordered_map>

#include <d2d1_3.h>
#include <d2d1_1helper.h>
#include <wrl/client.h>

#include <stb_truetype.h>
#include <stb_image.h>

#include <wincodec.h>
#pragma comment(lib, "windowscodecs.lib")

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

using Microsoft::WRL::ComPtr;

// ── Concrete image type — private to this file ──────────────────────────────
struct Canvas2DImageD2D : Canvas2DImage
{
    ComPtr<ID2D1Bitmap1> bitmap;
    ~Canvas2DImageD2D() override = default;
};

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

static inline D2D1_COLOR_F toD2DColor(Color c, float extraAlpha = 1.f)
{
    return D2D1::ColorF(c.r / 255.f, c.g / 255.f, c.b / 255.f, c.a / 255.f * extraAlpha);
}
static inline D2D1_RECT_F makeRect(float x, float y, float w, float h)
{
    return D2D1::RectF(x, y, x + w, y + h);
}
static inline D2D1_ROUNDED_RECT makeRounded(float x, float y, float w, float h, float r)
{
    return D2D1::RoundedRect(makeRect(x, y, w, h), r, r);
}

// =============================================================================
// Canvas2DBackend — FULL definition, private to this translation unit.
//
// Owns: the D3D device (borrowed), offscreen render target bitmap, glyph
// atlas bitmap + font faces, and the brush cache used by Canvas2D. One
// instance per CanvasWidget, created once and reused every frame.
// =============================================================================

struct Canvas2DBackend
{
    D3DDevice *device = nullptr; // borrowed — not owned

    ComPtr<ID2D1DeviceContext> offscreenDC;
    ComPtr<ID2D1Bitmap1> offscreenBitmap;
    int bitmapW = 0, bitmapH = 0;

    struct FontFace
    {
        std::string name;
        std::vector<uint8_t> ttfData;
        stbtt_fontinfo info = {};
        bool ready = false;
    };
    std::vector<FontFace> fonts;

    static constexpr int kAtlasW = 1024;
    static constexpr int kAtlasH = 1024;
    std::vector<uint8_t> atlasPixels; // kAtlasW * kAtlasH * 4  (BGRA premul)
    ComPtr<ID2D1Bitmap> atlasBitmap;
    int shelfX = 0, shelfY = 0, shelfH = 0;

    struct GlyphKey
    {
        int fontIdx, codepoint, pixelSize;
        bool operator==(const GlyphKey &o) const
        {
            return fontIdx == o.fontIdx &&
                   codepoint == o.codepoint &&
                   pixelSize == o.pixelSize;
        }
    };
    struct GlyphEntry
    {
        GlyphKey key;
        int atlasX, atlasY, glyphW, glyphH;
        int xoff, yoff, advance;
    };
    std::vector<GlyphEntry> glyphs;

    bool init(D3DDevice *dev)
    {
        assert(dev && dev->valid);
        device = dev;

        atlasPixels.assign(size_t(kAtlasW) * kAtlasH * 4, 0);

        D2D1_BITMAP_PROPERTIES bmpProps = D2D1::BitmapProperties(
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
        HRESULT hr = dev->d2dContext->CreateBitmap(
            D2D1::SizeU(kAtlasW, kAtlasH),
            atlasPixels.data(), kAtlasW * 4, bmpProps,
            atlasBitmap.GetAddressOf());
        return SUCCEEDED(hr);
    }

    bool resizeBitmap(int w, int h)
    {
        if (w <= 0 || h <= 0)
            return false;
        if (w == bitmapW && h == bitmapH && offscreenBitmap)
            return true;

        offscreenDC.Reset();
        offscreenBitmap.Reset();

        D2D1_BITMAP_PROPERTIES1 bmpProps = D2D1::BitmapProperties1(
            D2D1_BITMAP_OPTIONS_TARGET,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
        HRESULT hr = device->d2dContext->CreateBitmap(
            D2D1::SizeU(w, h), nullptr, 0, bmpProps,
            offscreenBitmap.GetAddressOf());
        if (FAILED(hr))
            return false;

        ComPtr<ID2D1DeviceContext> dc;
        hr = device->d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &dc);
        if (FAILED(hr))
            return false;

        dc->SetTarget(offscreenBitmap.Get());
        offscreenDC = dc;
        bitmapW = w;
        bitmapH = h;
        return true;
    }

    void destroy()
    {
        fonts.clear(); // stbtt_fontinfo has no explicit cleanup needed
        glyphs.clear();
        atlasBitmap.Reset();
        offscreenBitmap.Reset();
        offscreenDC.Reset();
        device = nullptr;
    }

    // ── Font management ─────────────────────────────────────────────────

    int findFont(const std::string &name) const
    {
        for (int i = 0; i < (int)fonts.size(); ++i)
            if (fonts[i].name == name)
                return i;
        return -1;
    }

    int addFont(const std::string &name, const std::string &path)
    {
        int ex = findFont(name);
        if (ex >= 0)
            return ex;

        std::ifstream f(path, std::ios::binary | std::ios::ate);
        if (!f)
            return -1;
        auto sz = f.tellg();
        if (sz <= 0)
            return -1;
        f.seekg(0);

        fonts.push_back({});
        auto &face = fonts.back();
        face.name = name;
        face.ttfData.resize(size_t(sz));
        f.read(reinterpret_cast<char *>(face.ttfData.data()), sz);
        if (!f)
        {
            fonts.pop_back();
            return -1;
        }

        if (!stbtt_InitFont(&face.info, face.ttfData.data(),
                            stbtt_GetFontOffsetForIndex(face.ttfData.data(), 0)))
        {
            fonts.pop_back();
            return -1;
        }

        face.ready = true;
        return int(fonts.size()) - 1;
    }

    // ── Glyph atlas ──────────────────────────────────────────────────────

    void uploadAtlas()
    {
        D2D1_RECT_U dst = D2D1::RectU(0, 0, kAtlasW, kAtlasH);
        atlasBitmap->CopyFromMemory(&dst, atlasPixels.data(), kAtlasW * 4);
    }

    bool bakeGlyph(int fontIdx, int codepoint, int pixelSize, GlyphEntry &out)
    {
        if (fontIdx < 0 || fontIdx >= (int)fonts.size() || !fonts[fontIdx].ready)
            return false;

        const stbtt_fontinfo *info = &fonts[fontIdx].info;
        float scale = stbtt_ScaleForPixelHeight(info, float(pixelSize));

        int xoff, yoff, gw, gh;
        unsigned char *bmp = stbtt_GetCodepointBitmap(
            info, scale, scale, codepoint, &gw, &gh, &xoff, &yoff);

        int advanceWidth, leftBearing;
        stbtt_GetCodepointHMetrics(info, codepoint, &advanceWidth, &leftBearing);
        int advance = int(float(advanceWidth) * scale + 0.5f);

        if (!bmp || gw <= 0 || gh <= 0)
        {
            if (bmp)
                stbtt_FreeBitmap(bmp, nullptr);
            out = {GlyphKey{fontIdx, codepoint, pixelSize}, 0, 0, 0, 0, xoff, yoff, advance};
            return true;
        }

        if (shelfX + gw + 1 > kAtlasW)
        {
            shelfX = 0;
            shelfY += shelfH + 1;
            shelfH = 0;
        }
        if (shelfY + gh + 1 > kAtlasH)
        {
            stbtt_FreeBitmap(bmp, nullptr);
            out = {GlyphKey{fontIdx, codepoint, pixelSize}, 0, 0, gw, gh, xoff, yoff, advance};
            return true;
        }

        for (int row = 0; row < gh; ++row)
        {
            const uint8_t *src = bmp + row * gw;
            uint8_t *dst = atlasPixels.data() + ((shelfY + row) * kAtlasW + shelfX) * 4;
            for (int col = 0; col < gw; ++col)
            {
                uint8_t g = src[col];
                dst[col * 4 + 0] = g; // B
                dst[col * 4 + 1] = g; // G
                dst[col * 4 + 2] = g; // R
                dst[col * 4 + 3] = g; // A  (premultiplied white * alpha)
            }
        }

        out = {GlyphKey{fontIdx, codepoint, pixelSize}, shelfX, shelfY, gw, gh, xoff, yoff, advance};
        shelfX += gw + 1;
        if (gh > shelfH)
            shelfH = gh;

        stbtt_FreeBitmap(bmp, nullptr);
        uploadAtlas();
        return true;
    }

    const GlyphEntry *getGlyph(int fontIdx, int codepoint, int pixelSize)
    {
        for (auto &g : glyphs)
            if (g.key.fontIdx == fontIdx && g.key.codepoint == codepoint && g.key.pixelSize == pixelSize)
                return &g;
        GlyphEntry e;
        if (!bakeGlyph(fontIdx, codepoint, pixelSize, e))
            return nullptr;
        glyphs.push_back(e);
        return &glyphs.back();
    }

    float getKernAdvance(int fontIdx, int cp1, int cp2, int pixelSize) const
    {
        if (fontIdx < 0 || fontIdx >= (int)fonts.size() || !fonts[fontIdx].ready)
            return 0.f;
        const stbtt_fontinfo *info = &fonts[fontIdx].info;
        float scale = stbtt_ScaleForPixelHeight(info, float(pixelSize));
        int kern = stbtt_GetCodepointKernAdvance(info, cp1, cp2);
        return float(kern) * scale;
    }
};

static inline ID2D1DeviceContext *getDC(Canvas2DBackend *b)
{
    return b->offscreenDC.Get();
}

// ============================================================================
// Public lifecycle / offscreen-target API — the ONLY surface
// flux_canvas_win32.cpp sees.
// ============================================================================

Canvas2DBackend *Canvas2DBackend_create(D3DDevice *dev)
{
    auto *b = new Canvas2DBackend();
    if (!b->init(dev))
    {
        delete b;
        return nullptr;
    }
    return b;
}

void Canvas2DBackend_destroy(Canvas2DBackend *b)
{
    if (!b) return;
    b->destroy();
    delete b;
}

bool Canvas2DBackend_resizeBitmap(Canvas2DBackend *b, int w, int h)
{
    return b && b->resizeBitmap(w, h);
}

ID2D1DeviceContext *Canvas2DBackend_offscreenDC(Canvas2DBackend *b)
{
    return b ? b->offscreenDC.Get() : nullptr;
}

ID2D1Bitmap1 *Canvas2DBackend_offscreenBitmap(Canvas2DBackend *b)
{
    return b ? b->offscreenBitmap.Get() : nullptr;
}

// =============================================================================
// Canvas2D — constructor / destructor
// =============================================================================

Canvas2D::Canvas2D(Canvas2DBackend *backend, int w, int h)
    : backend_(backend), w_(w), h_(h)
{
    assert(backend_ && backend_->offscreenDC);

    using BrushMap = std::unordered_map<uint32_t, ComPtr<ID2D1SolidColorBrush>>;
    brushCache_ = new BrushMap();

    d2dM11_ = 1; d2dM12_ = 0;
    d2dM21_ = 0; d2dM22_ = 1;
    d2dDx_  = 0; d2dDy_  = 0;
    applyD2DTransform();
}

// =============================================================================
// D2D internal helpers
// =============================================================================

void Canvas2D::applyD2DTransform()
{
    D2D1_MATRIX_3X2_F m = D2D1::Matrix3x2F(d2dM11_, d2dM12_,
                                            d2dM21_, d2dM22_,
                                            d2dDx_,  d2dDy_);
    getDC(backend_)->SetTransform(m);
}

void *Canvas2D::getBrush(Color c)
{
    using BrushMap = std::unordered_map<uint32_t, ComPtr<ID2D1SolidColorBrush>>;
    auto *map = static_cast<BrushMap *>(brushCache_);
    uint32_t key = uint32_t(c.r) | (uint32_t(c.g) << 8) | (uint32_t(c.b) << 16) | (uint32_t(c.a) << 24);
    auto it = map->find(key);
    if (it != map->end())
        return it->second.Get();
    ComPtr<ID2D1SolidColorBrush> br;
    getDC(backend_)->CreateSolidColorBrush(toD2DColor(c, globalAlpha_), br.GetAddressOf());
    (*map)[key] = br;
    return br.Get();
}

void *Canvas2D::makeStrokeStyle() const
{
    D2D1_CAP_STYLE cap = D2D1_CAP_STYLE_FLAT;
    if (lineCap_ == LineCap::Round)  cap = D2D1_CAP_STYLE_ROUND;
    if (lineCap_ == LineCap::Square) cap = D2D1_CAP_STYLE_SQUARE;
    D2D1_LINE_JOIN join = D2D1_LINE_JOIN_MITER;
    if (lineJoin_ == LineJoin::Round) join = D2D1_LINE_JOIN_ROUND;
    if (lineJoin_ == LineJoin::Bevel) join = D2D1_LINE_JOIN_BEVEL;
    D2D1_STROKE_STYLE_PROPERTIES sp = D2D1::StrokeStyleProperties(cap, cap, cap, join, miterLimit_);
    auto *ss = new ComPtr<ID2D1StrokeStyle>();
    backend_->device->d2dFactory->CreateStrokeStyle(sp, nullptr, 0, ss->GetAddressOf());
    return ss;
}

void *Canvas2D::makeFillBrush(float x, float y, float w, float h)
{
    auto *dc = getDC(backend_);
    if (!fillIsGrad_ || gStops_.empty())
    {
        auto *br = new ComPtr<ID2D1Brush>();
        static_cast<ID2D1SolidColorBrush *>(getBrush(fillColor_))->QueryInterface(IID_PPV_ARGS(br->GetAddressOf()));
        return br;
    }
    std::vector<D2D1_GRADIENT_STOP> stops;
    stops.reserve(gStops_.size());
    for (auto &[t, c] : gStops_)
        stops.push_back({t, toD2DColor(c, globalAlpha_)});
    ComPtr<ID2D1GradientStopCollection> coll;
    dc->CreateGradientStopCollection(stops.data(), (UINT32)stops.size(),
                                     D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP, coll.GetAddressOf());
    if (!coll)
        return nullptr;
    auto *br = new ComPtr<ID2D1Brush>();
    if (gradType_ == GradType::Linear)
    {
        D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES lp = {{gx0_, gy0_}, {gx1_, gy1_}};
        ComPtr<ID2D1LinearGradientBrush> lb;
        dc->CreateLinearGradientBrush(lp, coll.Get(), lb.GetAddressOf());
        if (lb) lb->QueryInterface(IID_PPV_ARGS(br->GetAddressOf()));
    }
    else
    {
        D2D1_RADIAL_GRADIENT_BRUSH_PROPERTIES rp = {{gcx_, gcy_}, {0, 0}, gOutR_, gOutR_};
        ComPtr<ID2D1RadialGradientBrush> rb;
        dc->CreateRadialGradientBrush(rp, coll.Get(), rb.GetAddressOf());
        if (rb) rb->QueryInterface(IID_PPV_ARGS(br->GetAddressOf()));
    }
    return br;
}

void *Canvas2D::buildPathGeometry() const
{
    ComPtr<ID2D1PathGeometry> geom;
    backend_->device->d2dFactory->CreatePathGeometry(geom.GetAddressOf());
    if (!geom) return nullptr;
    ComPtr<ID2D1GeometrySink> sink;
    geom->Open(sink.GetAddressOf());
    if (!sink) return nullptr;
    sink->SetFillMode(fillRule_ == FillRule::EvenOdd ? D2D1_FILL_MODE_ALTERNATE : D2D1_FILL_MODE_WINDING);

    bool figureOpen = false;
    for (const auto &pt : path_)
    {
        if (pt.close)
        {
            if (figureOpen) { sink->EndFigure(D2D1_FIGURE_END_CLOSED); figureOpen = false; }
            continue;
        }
        if (pt.move)
        {
            if (figureOpen) sink->EndFigure(D2D1_FIGURE_END_OPEN);
            sink->BeginFigure(D2D1::Point2F(pt.x, pt.y), D2D1_FIGURE_BEGIN_FILLED);
            figureOpen = true;
        }
        else
        {
            if (!figureOpen)
            {
                sink->BeginFigure(D2D1::Point2F(pt.x, pt.y), D2D1_FIGURE_BEGIN_FILLED);
                figureOpen = true;
            }
            else
                sink->AddLine(D2D1::Point2F(pt.x, pt.y));
        }
    }
    if (figureOpen) sink->EndFigure(D2D1_FIGURE_END_OPEN);
    sink->Close();
    auto *ret = new ComPtr<ID2D1PathGeometry>(geom);
    return ret;
}

void Canvas2D::drawPathFilled()
{
    auto *geomPtr = static_cast<ComPtr<ID2D1PathGeometry> *>(buildPathGeometry());
    if (!geomPtr) return;
    auto *brPtr = static_cast<ComPtr<ID2D1Brush> *>(makeFillBrush(0, 0, float(w_), float(h_)));
    if (brPtr && *brPtr)
        getDC(backend_)->FillGeometry(geomPtr->Get(), brPtr->Get());
    delete brPtr;
    delete geomPtr;
}

void Canvas2D::drawPathStroked()
{
    auto *geomPtr = static_cast<ComPtr<ID2D1PathGeometry> *>(buildPathGeometry());
    if (!geomPtr) return;
    auto *ssPtr = static_cast<ComPtr<ID2D1StrokeStyle> *>(makeStrokeStyle());
    auto *br = static_cast<ID2D1SolidColorBrush *>(getBrush(strokeColor_));
    if (br)
        getDC(backend_)->DrawGeometry(geomPtr->Get(), br, lineWidth_,
                                      ssPtr ? ssPtr->Get() : nullptr);
    delete ssPtr;
    delete geomPtr;
}

// =============================================================================
// State stack
// =============================================================================

void Canvas2D::save()
{
    SaveState s;
    s.d2dCtmM11 = d2dM11_; s.d2dCtmM12 = d2dM12_;
    s.d2dCtmM21 = d2dM21_; s.d2dCtmM22 = d2dM22_;
    s.d2dCtmDx  = d2dDx_;  s.d2dCtmDy  = d2dDy_;
    s.fillColor    = fillColor_;
    s.strokeColor  = strokeColor_;
    s.lineWidth    = lineWidth_;
    s.globalAlpha  = globalAlpha_;
    s.fillIsGrad   = fillIsGrad_;
    s.clipDepth    = clipDepth_;
    s.gradType     = gradType_;
    s.gx0 = gx0_; s.gy0 = gy0_; s.gx1 = gx1_; s.gy1 = gy1_;
    s.gcx = gcx_; s.gcy = gcy_; s.gInR = gInR_; s.gOutR = gOutR_;
    s.stops        = gStops_;
    s.fontFace     = fontFace_;
    s.fontSize     = fontSize_;
    s.fontBold     = fontBold_;
    s.fontItalic   = fontItalic_;
    s.textAlign    = textAlign_;
    s.textBaseline = textBaseline_;
    s.lineCap      = lineCap_;
    s.lineJoin     = lineJoin_;
    stateStack_.push_back(std::move(s));
}

void Canvas2D::restore()
{
    if (stateStack_.empty()) return;
    const SaveState &s = stateStack_.back();

    while (clipDepth_ > s.clipDepth)
    {
        getDC(backend_)->PopAxisAlignedClip();
        --clipDepth_;
    }

    d2dM11_ = s.d2dCtmM11; d2dM12_ = s.d2dCtmM12;
    d2dM21_ = s.d2dCtmM21; d2dM22_ = s.d2dCtmM22;
    d2dDx_  = s.d2dCtmDx;  d2dDy_  = s.d2dCtmDy;
    applyD2DTransform();

    fillColor_    = s.fillColor;
    strokeColor_  = s.strokeColor;
    lineWidth_    = s.lineWidth;
    globalAlpha_  = s.globalAlpha;
    fillIsGrad_   = s.fillIsGrad;
    clipDepth_    = s.clipDepth;
    gradType_     = s.gradType;
    gx0_ = s.gx0; gy0_ = s.gy0; gx1_ = s.gx1; gy1_ = s.gy1;
    gcx_ = s.gcx; gcy_ = s.gcy; gInR_ = s.gInR; gOutR_ = s.gOutR;
    gStops_       = s.stops;
    fontFace_     = s.fontFace;
    fontSize_     = s.fontSize;
    fontBold_     = s.fontBold;
    fontItalic_   = s.fontItalic;
    textAlign_    = s.textAlign;
    textBaseline_ = s.textBaseline;
    lineCap_      = s.lineCap;
    lineJoin_     = s.lineJoin;

    using BrushMap = std::unordered_map<uint32_t, ComPtr<ID2D1SolidColorBrush>>;
    static_cast<BrushMap *>(brushCache_)->clear();

    stateStack_.pop_back();
}

// =============================================================================
// Transform
// =============================================================================

void Canvas2D::translate(float dx, float dy)
{
    d2dDx_ += d2dM11_ * dx + d2dM21_ * dy;
    d2dDy_ += d2dM12_ * dx + d2dM22_ * dy;
    applyD2DTransform();
}
void Canvas2D::scale(float sx, float sy)
{
    d2dM11_ *= sx; d2dM12_ *= sx;
    d2dM21_ *= sy; d2dM22_ *= sy;
    applyD2DTransform();
}
void Canvas2D::rotate(float r)
{
    float c = cosf(r), s = sinf(r);
    float m11 = d2dM11_ * c + d2dM21_ * s,  m12 = d2dM12_ * c + d2dM22_ * s;
    float m21 = d2dM11_ * -s + d2dM21_ * c, m22 = d2dM12_ * -s + d2dM22_ * c;
    d2dM11_ = m11; d2dM12_ = m12;
    d2dM21_ = m21; d2dM22_ = m22;
    applyD2DTransform();
}
void Canvas2D::resetTransform()
{
    d2dM11_ = 1; d2dM12_ = 0;
    d2dM21_ = 0; d2dM22_ = 1;
    d2dDx_  = 0; d2dDy_  = 0;
    applyD2DTransform();
}

// =============================================================================
// Style
// =============================================================================

void Canvas2D::setFillColor(Color c)
{
    fillColor_ = c;
    fillIsGrad_ = false;
    using BrushMap = std::unordered_map<uint32_t, ComPtr<ID2D1SolidColorBrush>>;
    static_cast<BrushMap *>(brushCache_)->clear();
}
void Canvas2D::setStrokeColor(Color c)
{
    strokeColor_ = c;
    using BrushMap = std::unordered_map<uint32_t, ComPtr<ID2D1SolidColorBrush>>;
    static_cast<BrushMap *>(brushCache_)->clear();
}
void Canvas2D::setLineWidth(float w)    { lineWidth_ = w; }
void Canvas2D::setLineCap(LineCap c)   { lineCap_ = c; }
void Canvas2D::setLineJoin(LineJoin j) { lineJoin_ = j; }
void Canvas2D::setMiterLimit(float l)  { miterLimit_ = l; }
void Canvas2D::setFillRule(FillRule r) { fillRule_ = r; }
void Canvas2D::setGlobalAlpha(float a)
{
    globalAlpha_ = std::max(0.f, std::min(1.f, a));
    using BrushMap = std::unordered_map<uint32_t, ComPtr<ID2D1SolidColorBrush>>;
    static_cast<BrushMap *>(brushCache_)->clear();
}
void Canvas2D::setCompositeOp(CompositeOp op) { compositeOp_ = op; /* TODO: map to D2D blend */ }

// =============================================================================
// Gradient
// =============================================================================

void Canvas2D::beginLinearGradient(float x0, float y0, float x1, float y1)
{
    gradType_ = GradType::Linear;
    gx0_ = x0; gy0_ = y0; gx1_ = x1; gy1_ = y1;
    gStops_.clear();
}
void Canvas2D::beginRadialGradient(float cx, float cy, float ir, float outr)
{
    gradType_ = GradType::Radial;
    gcx_ = cx; gcy_ = cy; gInR_ = ir; gOutR_ = outr;
    gStops_.clear();
}
void Canvas2D::addColorStop(float t, Color c) { gStops_.push_back({t, c}); }
void Canvas2D::setFillGradient()              { fillIsGrad_ = true; }

// =============================================================================
// Primitives
// =============================================================================

void Canvas2D::clearRect(float x, float y, float w, float h)
{
    ComPtr<ID2D1SolidColorBrush> br;
    getDC(backend_)->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 0), br.GetAddressOf());
    if (br) getDC(backend_)->FillRectangle(makeRect(x, y, w, h), br.Get());
}
void Canvas2D::fillRect(float x, float y, float w, float h)
{
    auto *brPtr = static_cast<ComPtr<ID2D1Brush> *>(makeFillBrush(x, y, w, h));
    if (brPtr && *brPtr) getDC(backend_)->FillRectangle(makeRect(x, y, w, h), brPtr->Get());
    delete brPtr;
}
void Canvas2D::strokeRect(float x, float y, float w, float h)
{
    auto *ssPtr = static_cast<ComPtr<ID2D1StrokeStyle> *>(makeStrokeStyle());
    auto *br = static_cast<ID2D1SolidColorBrush *>(getBrush(strokeColor_));
    if (br) getDC(backend_)->DrawRectangle(makeRect(x, y, w, h), br, lineWidth_,
                                            ssPtr ? ssPtr->Get() : nullptr);
    delete ssPtr;
}
void Canvas2D::fillRoundedRect(float x, float y, float w, float h, float r)
{
    auto *brPtr = static_cast<ComPtr<ID2D1Brush> *>(makeFillBrush(x, y, w, h));
    if (brPtr && *brPtr) getDC(backend_)->FillRoundedRectangle(makeRounded(x, y, w, h, r), brPtr->Get());
    delete brPtr;
}
void Canvas2D::strokeRoundedRect(float x, float y, float w, float h, float r)
{
    auto *ssPtr = static_cast<ComPtr<ID2D1StrokeStyle> *>(makeStrokeStyle());
    auto *br = static_cast<ID2D1SolidColorBrush *>(getBrush(strokeColor_));
    if (br) getDC(backend_)->DrawRoundedRectangle(makeRounded(x, y, w, h, r), br, lineWidth_,
                                                   ssPtr ? ssPtr->Get() : nullptr);
    delete ssPtr;
}
void Canvas2D::fillCircle(float cx, float cy, float r)
{
    D2D1_ELLIPSE el = D2D1::Ellipse(D2D1::Point2F(cx, cy), r, r);
    auto *brPtr = static_cast<ComPtr<ID2D1Brush> *>(makeFillBrush(cx - r, cy - r, r * 2, r * 2));
    if (brPtr && *brPtr) getDC(backend_)->FillEllipse(el, brPtr->Get());
    delete brPtr;
}
void Canvas2D::strokeCircle(float cx, float cy, float r)
{
    D2D1_ELLIPSE el = D2D1::Ellipse(D2D1::Point2F(cx, cy), r, r);
    auto *ssPtr = static_cast<ComPtr<ID2D1StrokeStyle> *>(makeStrokeStyle());
    auto *br = static_cast<ID2D1SolidColorBrush *>(getBrush(strokeColor_));
    if (br) getDC(backend_)->DrawEllipse(el, br, lineWidth_, ssPtr ? ssPtr->Get() : nullptr);
    delete ssPtr;
}

// =============================================================================
// Path API
// =============================================================================

void Canvas2D::beginPath() { path_.clear(); }
void Canvas2D::closePath() { path_.push_back({pathStartX_, pathStartY_, false, true}); }
void Canvas2D::moveTo(float x, float y)
{
    pathStartX_ = curX_ = x;
    pathStartY_ = curY_ = y;
    path_.push_back({x, y, true, false});
}
void Canvas2D::lineTo(float x, float y)
{
    curX_ = x; curY_ = y;
    path_.push_back({x, y, false, false});
}
void Canvas2D::arc(float cx, float cy, float radius, float a0, float a1, bool ccw)
{
    int segs = std::min(std::max(8, int(radius * 0.5f)), 64);
    float step = (a1 - a0) / float(segs);
    if (ccw) step = -step;
    for (int i = 0; i <= segs; ++i)
    {
        float a = a0 + step * float(i);
        path_.push_back({cx + cosf(a) * radius, cy + sinf(a) * radius, i == 0, false});
    }
}
void Canvas2D::arcTo(float x1, float y1, float x2, float y2, float)
{
    path_.push_back({x1, y1, false, false});
    path_.push_back({x2, y2, false, false});
}
void Canvas2D::quadraticCurveTo(float cpx, float cpy, float x, float y)
{
    int n = 8;
    for (int i = 1; i <= n; ++i)
    {
        float t = float(i) / n, mt = 1 - t;
        path_.push_back({mt*mt*curX_ + 2*mt*t*cpx + t*t*x,
                         mt*mt*curY_ + 2*mt*t*cpy + t*t*y, false, false});
    }
    curX_ = x; curY_ = y;
}
void Canvas2D::bezierCurveTo(float c1x, float c1y, float c2x, float c2y, float x, float y)
{
    int n = 12;
    for (int i = 1; i <= n; ++i)
    {
        float t = float(i) / n, mt = 1 - t;
        path_.push_back({mt*mt*mt*curX_ + 3*mt*mt*t*c1x + 3*mt*t*t*c2x + t*t*t*x,
                         mt*mt*mt*curY_ + 3*mt*mt*t*c1y + 3*mt*t*t*c2y + t*t*t*y, false, false});
    }
    curX_ = x; curY_ = y;
}
void Canvas2D::rect(float x, float y, float w, float h)
{
    moveTo(x, y); lineTo(x+w, y); lineTo(x+w, y+h); lineTo(x, y+h); closePath();
}
void Canvas2D::ellipse(float cx, float cy, float rx, float ry,
                       float rot, float a0, float a1, bool ccw)
{
    int segs = 32;
    float step = (a1 - a0) / float(segs);
    if (ccw) step = -step;
    for (int i = 0; i <= segs; ++i)
    {
        float a = a0 + step * float(i);
        float px = cosf(a) * rx, py = sinf(a) * ry;
        path_.push_back({cosf(rot)*px - sinf(rot)*py + cx,
                         sinf(rot)*px + cosf(rot)*py + cy, i == 0, false});
    }
}
void Canvas2D::fill()  { drawPathFilled(); }
void Canvas2D::stroke(){ drawPathStroked(); }
void Canvas2D::clip()  { /* use pushClipRect */ }

// =============================================================================
// Clip rect
// =============================================================================

void Canvas2D::pushClipRect(float x, float y, float w, float h)
{
    getDC(backend_)->PushAxisAlignedClip(makeRect(x, y, w, h), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    ++clipDepth_;
}
void Canvas2D::popClipRect()
{
    if (clipDepth_ <= 0) return;
    getDC(backend_)->PopAxisAlignedClip();
    --clipDepth_;
}

// =============================================================================
// Image
// =============================================================================

static ComPtr<ID2D1Bitmap1> createBitmapFromRGBA(ID2D1DeviceContext *dc,
                                                 const uint8_t *rgba, int w, int h)
{
    std::vector<uint8_t> bgra(size_t(w) * h * 4);
    for (int i = 0; i < w * h; ++i)
    {
        bgra[i*4+0] = rgba[i*4+2];
        bgra[i*4+1] = rgba[i*4+1];
        bgra[i*4+2] = rgba[i*4+0];
        bgra[i*4+3] = rgba[i*4+3];
    }
    D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_NONE,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    ComPtr<ID2D1Bitmap1> bmp;
    dc->CreateBitmap(D2D1::SizeU(w, h), bgra.data(), w * 4, props, bmp.GetAddressOf());
    return bmp;
}

bool Canvas2D::registerFont(Canvas2DBackend *backend,
                            const std::string &name, const std::string &path)
{
    return backend && backend->addFont(name, path) >= 0;
}

Canvas2DImage *Canvas2D::loadImage(const std::string &path)
{
    int w, h, ch;
    stbi_set_flip_vertically_on_load(0);
    uint8_t *data = stbi_load(path.c_str(), &w, &h, &ch, 4);
    if (!data) return nullptr;
    auto bmp = createBitmapFromRGBA(getDC(backend_), data, w, h);
    stbi_image_free(data);
    if (!bmp) return nullptr;
    auto *img = new Canvas2DImageD2D();
    img->bitmap = bmp; img->width = w; img->height = h;
    return img;
}
Canvas2DImage *Canvas2D::loadImageFromMemory(const unsigned char *data, int byteLen)
{
    int w, h, ch;
    stbi_set_flip_vertically_on_load(0);
    uint8_t *px = stbi_load_from_memory(data, byteLen, &w, &h, &ch, 4);
    if (!px) return nullptr;
    auto bmp = createBitmapFromRGBA(getDC(backend_), px, w, h);
    stbi_image_free(px);
    if (!bmp) return nullptr;
    auto *img = new Canvas2DImageD2D();
    img->bitmap = bmp; img->width = w; img->height = h;
    return img;
}
void Canvas2D::updateImage(Canvas2DImage *img, const unsigned char *rgba, int w, int h)
{
    if (!img) return;
    auto *di = static_cast<Canvas2DImageD2D *>(img);
    di->bitmap = createBitmapFromRGBA(getDC(backend_), rgba, w, h);
    img->width = w; img->height = h;
}
void Canvas2D::freeImage(Canvas2DImage *img) { delete img; }

static void drawBitmapRegion(ID2D1DeviceContext *dc, ID2D1Bitmap *bmp,
                             float sx, float sy, float sw, float sh,
                             float dx, float dy, float dw, float dh, float alpha)
{
    D2D1_RECT_F src = makeRect(sx, sy, sw, sh), dst = makeRect(dx, dy, dw, dh);
    dc->DrawBitmap(bmp, &dst, alpha, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, &src);
}

void Canvas2D::drawImage(const Canvas2DImage *img, float dx, float dy)
{
    if (!img) return;
    const auto *di = static_cast<const Canvas2DImageD2D *>(img);
    if (!di->bitmap) return;
    drawBitmapRegion(getDC(backend_), di->bitmap.Get(),
                     0, 0, float(img->width), float(img->height),
                     dx, dy, float(img->width), float(img->height), globalAlpha_);
}
void Canvas2D::drawImage(const Canvas2DImage *img, float dx, float dy, float dw, float dh)
{
    if (!img) return;
    const auto *di = static_cast<const Canvas2DImageD2D *>(img);
    if (!di->bitmap) return;
    drawBitmapRegion(getDC(backend_), di->bitmap.Get(),
                     0, 0, float(img->width), float(img->height),
                     dx, dy, dw, dh, globalAlpha_);
}
void Canvas2D::drawImage(const Canvas2DImage *img,
                         float sx, float sy, float sw, float sh,
                         float dx, float dy, float dw, float dh)
{
    if (!img) return;
    const auto *di = static_cast<const Canvas2DImageD2D *>(img);
    if (!di->bitmap) return;
    drawBitmapRegion(getDC(backend_), di->bitmap.Get(),
                     sx, sy, sw, sh, dx, dy, dw, dh, globalAlpha_);
}

// =============================================================================
// Text
// =============================================================================

void Canvas2D::parseFontDesc(const std::string &desc)
{
    fontBold_ = false; fontItalic_ = false;
    fontSize_ = 14.f;  fontFace_   = "sans";
    std::istringstream ss(desc);
    std::string tok, family;
    while (ss >> tok)
    {
        if (tok == "bold")   { fontBold_   = true; continue; }
        if (tok == "italic") { fontItalic_ = true; continue; }
        if (tok.size() > 2 && tok.compare(tok.size() - 2, 2, "px") == 0)
        {
            fontSize_ = std::stof(tok.substr(0, tok.size() - 2));
            std::getline(ss, family);
            auto start = family.find_first_not_of(" \t");
            if (start != std::string::npos) family = family.substr(start);
            break;
        }
    }
    if (!family.empty()) fontFace_ = family;
}

void Canvas2D::setFont(const std::string &desc) { parseFontDesc(desc); }
void Canvas2D::setTextAlign(CanvasTextAlign a)  { textAlign_ = a; }
void Canvas2D::setTextBaseline(TextBaseline b)  { textBaseline_ = b; }

int Canvas2D::resolveFont() const
{
    if (fontBold_ && fontItalic_) { int i = backend_->findFont(fontFace_ + "-bold-italic"); if (i >= 0) return i; }
    if (fontBold_)                { int i = backend_->findFont(fontFace_ + "-bold");         if (i >= 0) return i; }
    if (fontItalic_)              { int i = backend_->findFont(fontFace_ + "-italic");       if (i >= 0) return i; }
    int i = backend_->findFont(fontFace_);
    if (i >= 0) return i;
    return backend_->fonts.empty() ? -1 : 0;
}

int Canvas2D::currentFontIdx() const { return resolveFont(); }

float Canvas2D::getKernAdvance(int fontIdx, int cp1, int cp2, int pixelSize) const
{
    return backend_->getKernAdvance(fontIdx, cp1, cp2, pixelSize);
}

void Canvas2D::fillText(const std::string &text, float x, float y, float /*maxW*/)
{
    if (text.empty()) return;
    int fontIdx = resolveFont();
    if (fontIdx < 0) return;

    const stbtt_fontinfo *info = &backend_->fonts[fontIdx].info;
    int ps = std::max(4, int(fontSize_ + 0.5f));
    float scale = stbtt_ScaleForPixelHeight(info, float(ps));

    int ascent, descent, lineGap;
    stbtt_GetFontVMetrics(info, &ascent, &descent, &lineGap);
    float ascender = float(ascent) * scale;

    float totalW = 0;
    for (unsigned char ch : text)
    {
        auto *g = backend_->getGlyph(fontIdx, ch, ps);
        if (g) totalW += float(g->advance);
    }

    float px = x;
    if (textAlign_ == CanvasTextAlign::Center) px = x - totalW * 0.5f;
    if (textAlign_ == CanvasTextAlign::Right)  px = x - totalW;

    float py = y;
    if (textBaseline_ == TextBaseline::Top)    py = y + ascender;
    if (textBaseline_ == TextBaseline::Middle) py = y + ascender * 0.5f;
    if (textBaseline_ == TextBaseline::Bottom) py = y + float(descent) * scale;

    ComPtr<ID2D1SolidColorBrush> textBrush;
    getDC(backend_)->CreateSolidColorBrush(toD2DColor(fillColor_, globalAlpha_), textBrush.GetAddressOf());
    if (!textBrush) return;

    getDC(backend_)->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
    for (unsigned char ch : text)
    {
        auto *g = backend_->getGlyph(fontIdx, ch, ps);
        if (!g) continue;
        if (g->glyphW > 0 && g->glyphH > 0)
        {
            float gx = px + float(g->xoff), gy = py + float(g->yoff);
            D2D1_RECT_F src = D2D1::RectF(float(g->atlasX), float(g->atlasY),
                                           float(g->atlasX + g->glyphW), float(g->atlasY + g->glyphH));
            D2D1_RECT_F dst = D2D1::RectF(gx, gy, gx + float(g->glyphW), gy + float(g->glyphH));
            getDC(backend_)->FillOpacityMask(backend_->atlasBitmap.Get(), textBrush.Get(),
                                             D2D1_OPACITY_MASK_CONTENT_TEXT_NATURAL, &dst, &src);
        }
        px += float(g->advance);
    }
    getDC(backend_)->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
}

void Canvas2D::strokeText(const std::string &text, float x, float y, float maxW)
{
    Color saved = fillColor_;
    setFillColor(strokeColor_);
    fillText(text, x - 1, y, maxW);
    fillText(text, x + 1, y, maxW);
    fillText(text, x, y - 1, maxW);
    fillText(text, x, y + 1, maxW);
    setFillColor(saved);
    fillText(text, x, y, maxW);
}

float Canvas2D::measureText(const std::string &text)
{
    if (text.empty()) return 0.f;
    int fontIdx = resolveFont();
    if (fontIdx < 0) return float(text.size()) * fontSize_ * 0.6f;
    int ps = std::max(4, int(fontSize_ + 0.5f));
    float total = 0;
    for (unsigned char ch : text)
    {
        auto *g = backend_->getGlyph(fontIdx, ch, ps);
        if (g) total += float(g->advance);
    }
    return total;
}

// =============================================================================
// Pixel access
// =============================================================================

void Canvas2D::getImageData(float x, float y, float w, float h,
                            std::vector<uint8_t> &out)
{
    int iw = int(w), ih = int(h);
    out.assign(size_t(iw) * ih * 4, 0);
    // Full GPU→CPU readback requires a D3D11 staging texture.
    // Returning zeros for now — add staging path if needed.
    (void)x; (void)y;
}

void Canvas2D::putImageData(const std::vector<uint8_t> &data,
                            int srcW, int srcH, float dx, float dy)
{
    auto bmp = createBitmapFromRGBA(getDC(backend_), data.data(), srcW, srcH);
    if (!bmp) return;
    D2D1_RECT_F dst = makeRect(dx, dy, float(srcW), float(srcH));
    D2D1_RECT_F src = makeRect(0, 0, float(srcW), float(srcH));
    getDC(backend_)->DrawBitmap(bmp.Get(), &dst, globalAlpha_,
                                D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR, &src);
}

#endif // _WIN32