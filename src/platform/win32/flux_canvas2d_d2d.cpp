// flux_canvas2d_d2d.cpp
// Direct2D implementation of Canvas2D for Win32.
// Compiled only on _WIN32. No GL headers anywhere in this file.
#ifdef _WIN32

#define _USE_MATH_DEFINES
#include <cmath>
#include <cassert>
#include <cstring>
#include <algorithm>
#include <fstream>
#include <sstream>

#include "flux/flux_canvas2d_d2d.hpp"
#include "flux/flux_platform.hpp"

// FreeType
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H

// stb_image for loadImage
#include <stb_image.h>

#include <wincodec.h>
#pragma comment(lib, "windowscodecs.lib")

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

static inline D2D1_COLOR_F toD2DColor(Color c, float extraAlpha = 1.f)
{
    return D2D1::ColorF(c.r / 255.f, c.g / 255.f, c.b / 255.f,
                        c.a / 255.f * extraAlpha);
}

static inline D2D1_RECT_F makeRect(float x, float y, float w, float h)
{
    return D2D1::RectF(x, y, x + w, y + h);
}

static inline D2D1_ROUNDED_RECT makeRounded(float x, float y,
                                            float w, float h, float r)
{
    return D2D1::RoundedRect(makeRect(x, y, w, h), r, r);
}

// ─────────────────────────────────────────────────────────────────────────────
// Canvas2DD2D — init / destroy
// ─────────────────────────────────────────────────────────────────────────────

bool Canvas2DD2D::init(D3DDevice *dev)
{
    assert(dev && dev->valid);
    device = dev;

    // FreeType
    if (FT_Init_FreeType(&ftLibrary))
        return false;

    // Atlas CPU buffer (BGRA, pre-multiplied alpha: grey stored in all 4 channels)
    atlasPixels.assign(size_t(kAtlasW) * kAtlasH * 4, 0);

    // Atlas D2D bitmap — BGRA, premultiplied
    D2D1_BITMAP_PROPERTIES bmpProps = D2D1::BitmapProperties(
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                          D2D1_ALPHA_MODE_PREMULTIPLIED));

    // Create using device's d2dContext (the main render target context)
    HRESULT hr = dev->d2dContext->CreateBitmap(
        D2D1::SizeU(kAtlasW, kAtlasH),
        atlasPixels.data(),
        kAtlasW * 4,
        bmpProps,
        atlasBitmap.GetAddressOf());
    if (FAILED(hr))
        return false;

    return true;
}

bool Canvas2DD2D::resizeBitmap(int w, int h)
{
    if (w <= 0 || h <= 0)
        return false;
    if (w == bitmapW && h == bitmapH && offscreenBitmap)
        return true;

    offscreenDC.Reset();
    offscreenBitmap.Reset();

    // Off-screen bitmap (document-sized, BGRA, ignore alpha — fully opaque target)
    D2D1_BITMAP_PROPERTIES1 bmpProps = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));

    HRESULT hr = device->d2dContext->CreateBitmap(
        D2D1::SizeU(w, h),
        nullptr, 0,
        bmpProps,
        offscreenBitmap.GetAddressOf());
    if (FAILED(hr))
        return false;

    // Create a separate device context that targets this bitmap
    ComPtr<ID2D1DeviceContext> dc;
    hr = device->d2dDevice->CreateDeviceContext(
        D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &dc);
    if (FAILED(hr))
        return false;

    dc->SetTarget(offscreenBitmap.Get());
    offscreenDC = dc;

    bitmapW = w;
    bitmapH = h;
    return true;
}

void Canvas2DD2D::destroy()
{
    for (auto &f : fonts)
        if (f.ftFace)
        {
            FT_Done_Face(f.ftFace);
            f.ftFace = nullptr;
        }
    fonts.clear();
    glyphs.clear();

    if (ftLibrary)
    {
        FT_Done_FreeType(ftLibrary);
        ftLibrary = nullptr;
    }

    atlasBitmap.Reset();
    offscreenBitmap.Reset();
    offscreenDC.Reset();
    device = nullptr;
}

// ─────────────────────────────────────────────────────────────────────────────
// Font management
// ─────────────────────────────────────────────────────────────────────────────

bool Canvas2DD2D::registerFont(Canvas2DD2D *self,
                               const std::string &name,
                               const std::string &path)
{
    return self && self->addFont(name, path) >= 0;
}

int Canvas2DD2D::findFont(const std::string &name) const
{
    for (int i = 0; i < (int)fonts.size(); ++i)
        if (fonts[i].name == name)
            return i;
    return -1;
}

int Canvas2DD2D::addFont(const std::string &name, const std::string &path)
{
    int existing = findFont(name);
    if (existing >= 0)
        return existing;

    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f)
        return -1;
    auto sz = f.tellg();
    if (sz <= 0)
        return -1;
    f.seekg(0);

    fonts.push_back({});
    FontFace &face = fonts.back();
    face.name = name;
    face.ttfData.resize(size_t(sz));
    f.read(reinterpret_cast<char *>(face.ttfData.data()), sz);
    if (!f)
    {
        fonts.pop_back();
        return -1;
    }

    FT_Error err = FT_New_Memory_Face(ftLibrary,
                                      face.ttfData.data(),
                                      FT_Long(face.ttfData.size()),
                                      0, &face.ftFace);
    if (err)
    {
        fonts.pop_back();
        return -1;
    }
    return (int)fonts.size() - 1;
}

// ─────────────────────────────────────────────────────────────────────────────
// Glyph atlas
// ─────────────────────────────────────────────────────────────────────────────

void Canvas2DD2D::uploadAtlas()
{
    // Upload the full atlas to the D2D bitmap
    D2D1_RECT_U dst = D2D1::RectU(0, 0, kAtlasW, kAtlasH);
    atlasBitmap->CopyFromMemory(&dst, atlasPixels.data(), kAtlasW * 4);
}

bool Canvas2DD2D::bakeGlyph(int fontIdx, int codepoint, int pixelSize,
                            GlyphEntry &out)
{
    if (fontIdx < 0 || fontIdx >= (int)fonts.size())
        return false;
    FT_Face face = fonts[fontIdx].ftFace;

    FT_Set_Pixel_Sizes(face, 0, FT_UInt(pixelSize));
    FT_UInt gi = FT_Get_Char_Index(face, FT_ULong(codepoint));
    if (FT_Load_Glyph(face, gi, FT_LOAD_DEFAULT))
        return false;
    if (FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL))
        return false;

    FT_GlyphSlot slot = face->glyph;
    FT_Bitmap &bmp = slot->bitmap;
    int gw = (int)bmp.width, gh = (int)bmp.rows;
    int advance = int(slot->advance.x >> 6);
    int xoff = slot->bitmap_left;
    int yoff = -slot->bitmap_top;

    if (gw <= 0 || gh <= 0)
    {
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
        out = {GlyphKey{fontIdx, codepoint, pixelSize}, 0, 0, gw, gh, xoff, yoff, advance};
        return true;
    }

    // Copy greyscale into BGRA atlas (premultiplied: B=G=R=A=grey)
    const int srcPitch = std::abs(bmp.pitch);
    for (int row = 0; row < gh; ++row)
    {
        const uint8_t *src = bmp.buffer + row * srcPitch;
        uint8_t *dst = atlasPixels.data() + ((shelfY + row) * kAtlasW + shelfX) * 4;
        for (int col = 0; col < gw; ++col)
        {
            uint8_t g = src[col];
            dst[col * 4 + 0] = g; // B
            dst[col * 4 + 1] = g; // G
            dst[col * 4 + 2] = g; // R
            dst[col * 4 + 3] = g; // A  (premultiplied grey)
        }
    }

    out = {GlyphKey{fontIdx, codepoint, pixelSize},
           shelfX, shelfY, gw, gh, xoff, yoff, advance};
    shelfX += gw + 1;
    if (gh > shelfH)
        shelfH = gh;

    uploadAtlas();
    return true;
}

const Canvas2DD2D::GlyphEntry *Canvas2DD2D::getGlyph(int fontIdx,
                                                     int codepoint,
                                                     int pixelSize)
{
    for (auto &g : glyphs)
        if (g.key.fontIdx == fontIdx &&
            g.key.codepoint == codepoint &&
            g.key.pixelSize == pixelSize)
            return &g;
    GlyphEntry e;
    if (!bakeGlyph(fontIdx, codepoint, pixelSize, e))
        return nullptr;
    glyphs.push_back(e);
    return &glyphs.back();
}

float Canvas2DD2D::getKernAdvance(int fontIdx, int cp1, int cp2, int pixelSize) const
{
    if (fontIdx < 0 || fontIdx >= (int)fonts.size())
        return 0.f;
    FT_Face face = fonts[fontIdx].ftFace;
    if (!FT_HAS_KERNING(face))
        return 0.f;
    FT_Set_Pixel_Sizes(face, 0, FT_UInt(pixelSize));
    FT_UInt g1 = FT_Get_Char_Index(face, FT_ULong(cp1));
    FT_UInt g2 = FT_Get_Char_Index(face, FT_ULong(cp2));
    FT_Vector delta;
    if (FT_Get_Kerning(face, g1, g2, FT_KERNING_DEFAULT, &delta))
        return 0.f;
    return float(delta.x >> 6);
}

// =============================================================================
// Canvas2D
// =============================================================================

Canvas2D::Canvas2D(Canvas2DD2D *d2d, int canvasW, int canvasH)
    : d2d_(d2d), canvasW_(canvasW), canvasH_(canvasH)
{
    assert(d2d_ && d2d_->offscreenDC);
    ctm_ = D2D1::Matrix3x2F::Identity();
    dc()->SetTransform(ctm_);
}

// ─────────────────────────────────────────────────────────────────────────────
// Internal: brush
// ─────────────────────────────────────────────────────────────────────────────

D2D1_COLOR_F Canvas2D::toD2D(Color c) const
{
    return toD2DColor(c, globalAlpha_);
}

ID2D1SolidColorBrush *Canvas2D::getBrush(Color c)
{
    uint32_t key = uint32_t(c.r) | (uint32_t(c.g) << 8) |
                   (uint32_t(c.b) << 16) | (uint32_t(c.a) << 24);
    auto it = brushCache_.find(key);
    if (it != brushCache_.end())
        return it->second.Get();

    ComPtr<ID2D1SolidColorBrush> br;
    dc()->CreateSolidColorBrush(toD2D(c), br.GetAddressOf());
    brushCache_[key] = br;
    return br.Get();
}

// ─────────────────────────────────────────────────────────────────────────────
// Internal: stroke style
// ─────────────────────────────────────────────────────────────────────────────

ComPtr<ID2D1StrokeStyle> Canvas2D::makeStrokeStyle() const
{
    D2D1_CAP_STYLE cap = D2D1_CAP_STYLE_FLAT;
    if (lineCap_ == LineCap::Round)
        cap = D2D1_CAP_STYLE_ROUND;
    if (lineCap_ == LineCap::Square)
        cap = D2D1_CAP_STYLE_SQUARE;

    D2D1_LINE_JOIN join = D2D1_LINE_JOIN_MITER;
    if (lineJoin_ == LineJoin::Round)
        join = D2D1_LINE_JOIN_ROUND;
    if (lineJoin_ == LineJoin::Bevel)
        join = D2D1_LINE_JOIN_BEVEL;

    D2D1_STROKE_STYLE_PROPERTIES sp = D2D1::StrokeStyleProperties(
        cap, cap, cap, join, miterLimit_);

    ComPtr<ID2D1StrokeStyle> ss;
    d2d_->device->d2dFactory->CreateStrokeStyle(sp, nullptr, 0, ss.GetAddressOf());
    return ss;
}

// ─────────────────────────────────────────────────────────────────────────────
// Internal: fill brush (solid or gradient)
// ─────────────────────────────────────────────────────────────────────────────

ComPtr<ID2D1Brush> Canvas2D::makeFillBrush(const D2D1_RECT_F &bounds)
{
    if (!fillIsGrad_ || gStops_.empty())
    {
        // Return the cached solid brush as an ID2D1Brush
        ComPtr<ID2D1Brush> br;
        getBrush(fillColor_)->QueryInterface(IID_PPV_ARGS(&br));
        return br;
    }

    // Build gradient stop collection
    std::vector<D2D1_GRADIENT_STOP> stops;
    stops.reserve(gStops_.size());
    for (auto &[t, c] : gStops_)
        stops.push_back({t, toD2DColor(c, globalAlpha_)});

    ComPtr<ID2D1GradientStopCollection> coll;
    dc()->CreateGradientStopCollection(stops.data(), (UINT32)stops.size(),
                                       D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP,
                                       coll.GetAddressOf());
    if (!coll)
        return nullptr;

    if (gradType_ == GradType::Linear)
    {
        D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES lp = {
            D2D1::Point2F(gx0_, gy0_), D2D1::Point2F(gx1_, gy1_)};
        ComPtr<ID2D1LinearGradientBrush> br;
        dc()->CreateLinearGradientBrush(lp, coll.Get(), br.GetAddressOf());
        ComPtr<ID2D1Brush> base;
        if (br)
            br->QueryInterface(IID_PPV_ARGS(&base));
        return base;
    }
    else
    {
        D2D1_RADIAL_GRADIENT_BRUSH_PROPERTIES rp = {
            D2D1::Point2F(gcx_, gcy_), D2D1::Point2F(0, 0),
            gOutR_, gOutR_};
        ComPtr<ID2D1RadialGradientBrush> br;
        dc()->CreateRadialGradientBrush(rp, coll.Get(), br.GetAddressOf());
        ComPtr<ID2D1Brush> base;
        if (br)
            br->QueryInterface(IID_PPV_ARGS(&base));
        return base;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Internal: path geometry
// ─────────────────────────────────────────────────────────────────────────────

ComPtr<ID2D1PathGeometry> Canvas2D::buildPathGeometry() const
{
    ComPtr<ID2D1PathGeometry> geom;
    d2d_->device->d2dFactory->CreatePathGeometry(geom.GetAddressOf());
    if (!geom)
        return nullptr;

    ComPtr<ID2D1GeometrySink> sink;
    geom->Open(sink.GetAddressOf());
    if (!sink)
        return nullptr;

    sink->SetFillMode(fillRule_ == FillRule::EvenOdd
                          ? D2D1_FILL_MODE_ALTERNATE
                          : D2D1_FILL_MODE_WINDING);

    bool figureOpen = false;
    for (const auto &pt : path_)
    {
        if (pt.close)
        {
            if (figureOpen)
            {
                sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                figureOpen = false;
            }
            continue;
        }
        if (pt.move)
        {
            if (figureOpen)
                sink->EndFigure(D2D1_FIGURE_END_OPEN);
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
            {
                sink->AddLine(D2D1::Point2F(pt.x, pt.y));
            }
        }
    }
    if (figureOpen)
        sink->EndFigure(D2D1_FIGURE_END_OPEN);
    sink->Close();
    return geom;
}

void Canvas2D::drawPathFilled()
{
    auto geom = buildPathGeometry();
    if (!geom)
        return;
    D2D1_RECT_F dummy = makeRect(0, 0, (float)canvasW_, (float)canvasH_);
    auto br = makeFillBrush(dummy);
    if (br)
        dc()->FillGeometry(geom.Get(), br.Get());
}

void Canvas2D::drawPathStroked()
{
    auto geom = buildPathGeometry();
    if (!geom)
        return;
    auto ss = makeStrokeStyle();
    auto *br = getBrush(strokeColor_);
    if (br)
        dc()->DrawGeometry(geom.Get(), br, lineWidth_, ss.Get());
}

// ─────────────────────────────────────────────────────────────────────────────
// Transform
// ─────────────────────────────────────────────────────────────────────────────

void Canvas2D::applyTransform()
{
    dc()->SetTransform(ctm_);
}

void Canvas2D::translate(float dx, float dy)
{
    ctm_ = D2D1::Matrix3x2F::Translation(dx, dy) * ctm_;
    applyTransform();
}
void Canvas2D::scale(float sx, float sy)
{
    ctm_ = D2D1::Matrix3x2F::Scale(sx, sy) * ctm_;
    applyTransform();
}
void Canvas2D::rotate(float r)
{
    // D2D rotation is in degrees
    ctm_ = D2D1::Matrix3x2F::Rotation(r * (180.f / (float)M_PI)) * ctm_;
    applyTransform();
}
void Canvas2D::resetTransform()
{
    ctm_ = D2D1::Matrix3x2F::Identity();
    applyTransform();
}

// ─────────────────────────────────────────────────────────────────────────────
// State stack
// ─────────────────────────────────────────────────────────────────────────────

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
    s.gx0 = gx0_;
    s.gy0 = gy0_;
    s.gx1 = gx1_;
    s.gy1 = gy1_;
    s.gcx = gcx_;
    s.gcy = gcy_;
    s.gInR = gInR_;
    s.gOutR = gOutR_;
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
    if (stateStack_.empty())
        return;
    const SaveState &s = stateStack_.back();

    // Pop clip layers added since this save
    while (clipDepth_ > s.clipDepth)
    {
        dc()->PopAxisAlignedClip();
        --clipDepth_;
    }

    ctm_ = s.ctm;
    applyTransform();
    fillColor_ = s.fillColor;
    strokeColor_ = s.strokeColor;
    lineWidth_ = s.lineWidth;
    globalAlpha_ = s.globalAlpha;
    fillIsGrad_ = s.fillIsGrad;
    clipDepth_ = s.clipDepth;
    gradType_ = s.gradType;
    gx0_ = s.gx0;
    gy0_ = s.gy0;
    gx1_ = s.gx1;
    gy1_ = s.gy1;
    gcx_ = s.gcx;
    gcy_ = s.gcy;
    gInR_ = s.gInR;
    gOutR_ = s.gOutR;
    gStops_ = s.stops;
    fontFace_ = s.fontFace;
    fontSize_ = s.fontSize;
    fontBold_ = s.fontBold;
    fontItalic_ = s.fontItalic;
    textAlign_ = s.textAlign;
    textBaseline_ = s.textBaseline;
    lineCap_ = s.lineCap;
    lineJoin_ = s.lineJoin;

    // Flush brush cache on restore (globalAlpha may have changed)
    brushCache_.clear();

    stateStack_.pop_back();
}

// ─────────────────────────────────────────────────────────────────────────────
// Style
// ─────────────────────────────────────────────────────────────────────────────

void Canvas2D::setFillColor(Color c)
{
    fillColor_ = c;
    fillIsGrad_ = false;
    brushCache_.clear();
}
void Canvas2D::setStrokeColor(Color c)
{
    strokeColor_ = c;
    brushCache_.clear();
}
void Canvas2D::setLineWidth(float w) { lineWidth_ = w; }
void Canvas2D::setLineCap(LineCap c) { lineCap_ = c; }
void Canvas2D::setLineJoin(LineJoin j) { lineJoin_ = j; }
void Canvas2D::setMiterLimit(float l) { miterLimit_ = l; }
void Canvas2D::setFillRule(FillRule r) { fillRule_ = r; }
void Canvas2D::setGlobalAlpha(float a)
{
    globalAlpha_ = std::max(0.f, std::min(1.f, a));
    brushCache_.clear(); // alpha affects all cached brushes
}
void Canvas2D::setCompositeOp(CompositeOp op) { compositeOp_ = op; } // TODO: map to D2D blend mode if needed

// ─────────────────────────────────────────────────────────────────────────────
// Gradient
// ─────────────────────────────────────────────────────────────────────────────

void Canvas2D::beginLinearGradient(float x0, float y0, float x1, float y1)
{
    gradType_ = GradType::Linear;
    gx0_ = x0;
    gy0_ = y0;
    gx1_ = x1;
    gy1_ = y1;
    gStops_.clear();
}
void Canvas2D::beginRadialGradient(float cx, float cy, float ir, float outr)
{
    gradType_ = GradType::Radial;
    gcx_ = cx;
    gcy_ = cy;
    gInR_ = ir;
    gOutR_ = outr;
    gStops_.clear();
}
void Canvas2D::addColorStop(float t, Color c) { gStops_.push_back({t, c}); }
void Canvas2D::setFillGradient() { fillIsGrad_ = true; }

// ─────────────────────────────────────────────────────────────────────────────
// Primitives
// ─────────────────────────────────────────────────────────────────────────────

void Canvas2D::clearRect(float x, float y, float w, float h)
{
    // Save composite op, set to Copy, draw transparent, restore
    auto prev = compositeOp_;
    // D2D: clear via a transparent filled rect with blend = Copy
    ComPtr<ID2D1SolidColorBrush> br;
    dc()->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 0), br.GetAddressOf());
    if (br)
        dc()->FillRectangle(makeRect(x, y, w, h), br.Get());
    compositeOp_ = prev;
}

void Canvas2D::fillRect(float x, float y, float w, float h)
{
    auto bounds = makeRect(x, y, w, h);
    auto br = makeFillBrush(bounds);
    if (br)
        dc()->FillRectangle(bounds, br.Get());
}

void Canvas2D::strokeRect(float x, float y, float w, float h)
{
    auto *br = getBrush(strokeColor_);
    if (!br)
        return;
    auto ss = makeStrokeStyle();
    dc()->DrawRectangle(makeRect(x, y, w, h), br, lineWidth_, ss.Get());
}

void Canvas2D::fillRoundedRect(float x, float y, float w, float h, float r)
{
    auto bounds = makeRect(x, y, w, h);
    auto br = makeFillBrush(bounds);
    if (br)
        dc()->FillRoundedRectangle(makeRounded(x, y, w, h, r), br.Get());
}

void Canvas2D::strokeRoundedRect(float x, float y, float w, float h, float r)
{
    auto *br = getBrush(strokeColor_);
    if (!br)
        return;
    auto ss = makeStrokeStyle();
    dc()->DrawRoundedRectangle(makeRounded(x, y, w, h, r), br, lineWidth_, ss.Get());
}

void Canvas2D::fillCircle(float cx, float cy, float r)
{
    D2D1_ELLIPSE el = D2D1::Ellipse(D2D1::Point2F(cx, cy), r, r);
    auto bounds = makeRect(cx - r, cy - r, r * 2, r * 2);
    auto br = makeFillBrush(bounds);
    if (br)
        dc()->FillEllipse(el, br.Get());
}

void Canvas2D::strokeCircle(float cx, float cy, float r)
{
    D2D1_ELLIPSE el = D2D1::Ellipse(D2D1::Point2F(cx, cy), r, r);
    auto *br = getBrush(strokeColor_);
    if (!br)
        return;
    auto ss = makeStrokeStyle();
    dc()->DrawEllipse(el, br, lineWidth_, ss.Get());
}

// ─────────────────────────────────────────────────────────────────────────────
// Path API
// ─────────────────────────────────────────────────────────────────────────────

void Canvas2D::beginPath() { path_.clear(); }
void Canvas2D::closePath()
{
    path_.push_back({pathStartX_, pathStartY_, false, true});
}
void Canvas2D::moveTo(float x, float y)
{
    pathStartX_ = curX_ = x;
    pathStartY_ = curY_ = y;
    path_.push_back({x, y, true, false});
}
void Canvas2D::lineTo(float x, float y)
{
    curX_ = x;
    curY_ = y;
    path_.push_back({x, y, false, false});
}

void Canvas2D::arc(float cx, float cy, float radius,
                   float a0, float a1, bool ccw)
{
    // Tessellate into line segments (consistent with the GL backend)
    int segs = std::max(8, int(radius * 0.5f));
    segs = std::min(segs, 64);
    float step = (a1 - a0) / float(segs);
    if (ccw)
        step = -step;
    for (int i = 0; i <= segs; ++i)
    {
        float a = a0 + step * float(i);
        float px = cx + cosf(a) * radius;
        float py = cy + sinf(a) * radius;
        path_.push_back({px, py, i == 0, false});
    }
}

void Canvas2D::arcTo(float x1, float y1, float x2, float y2, float /*r*/)
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
        path_.push_back({mt * mt * curX_ + 2 * mt * t * cpx + t * t * x,
                         mt * mt * curY_ + 2 * mt * t * cpy + t * t * y, false, false});
    }
    curX_ = x;
    curY_ = y;
}

void Canvas2D::bezierCurveTo(float c1x, float c1y,
                             float c2x, float c2y,
                             float x, float y)
{
    int n = 12;
    for (int i = 1; i <= n; ++i)
    {
        float t = float(i) / n, mt = 1 - t;
        path_.push_back({mt * mt * mt * curX_ + 3 * mt * mt * t * c1x + 3 * mt * t * t * c2x + t * t * t * x,
                         mt * mt * mt * curY_ + 3 * mt * mt * t * c1y + 3 * mt * t * t * c2y + t * t * t * y,
                         false, false});
    }
    curX_ = x;
    curY_ = y;
}

void Canvas2D::rect(float x, float y, float w, float h)
{
    moveTo(x, y);
    lineTo(x + w, y);
    lineTo(x + w, y + h);
    lineTo(x, y + h);
    closePath();
}

void Canvas2D::ellipse(float cx, float cy, float rx, float ry,
                       float rot, float a0, float a1, bool ccw)
{
    int segs = 32;
    float step = (a1 - a0) / float(segs);
    if (ccw)
        step = -step;
    for (int i = 0; i <= segs; ++i)
    {
        float a = a0 + step * float(i);
        float px = cosf(a) * rx, py = sinf(a) * ry;
        path_.push_back({cosf(rot) * px - sinf(rot) * py + cx,
                         sinf(rot) * px + cosf(rot) * py + cy,
                         i == 0, false});
    }
}

void Canvas2D::fill() { drawPathFilled(); }
void Canvas2D::stroke() { drawPathStroked(); }
void Canvas2D::clip() { /* use pushClipRect */ }

// ─────────────────────────────────────────────────────────────────────────────
// Clip rect
// ─────────────────────────────────────────────────────────────────────────────

void Canvas2D::pushClipRect(float x, float y, float w, float h)
{
    dc()->PushAxisAlignedClip(makeRect(x, y, w, h),
                              D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    ++clipDepth_;
}

void Canvas2D::popClipRect()
{
    if (clipDepth_ <= 0)
        return;
    dc()->PopAxisAlignedClip();
    --clipDepth_;
}

// ─────────────────────────────────────────────────────────────────────────────
// Image
// ─────────────────────────────────────────────────────────────────────────────

static ComPtr<ID2D1Bitmap1> createBitmapFromRGBA(ID2D1DeviceContext *dc,
                                                 const uint8_t *rgba,
                                                 int w, int h)
{
    // D2D wants BGRA — swap R and B
    std::vector<uint8_t> bgra(size_t(w) * h * 4);
    for (int i = 0; i < w * h; ++i)
    {
        bgra[i * 4 + 0] = rgba[i * 4 + 2];
        bgra[i * 4 + 1] = rgba[i * 4 + 1];
        bgra[i * 4 + 2] = rgba[i * 4 + 0];
        bgra[i * 4 + 3] = rgba[i * 4 + 3];
    }
    D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_NONE,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    ComPtr<ID2D1Bitmap1> bmp;
    dc->CreateBitmap(D2D1::SizeU(w, h), bgra.data(), w * 4, props, bmp.GetAddressOf());
    return bmp;
}

Canvas2DImage *Canvas2D::loadImage(const std::string &path)
{
    int w, h, ch;
    stbi_set_flip_vertically_on_load(0);
    uint8_t *data = stbi_load(path.c_str(), &w, &h, &ch, 4);
    if (!data)
        return nullptr;
    auto bmp = createBitmapFromRGBA(dc(), data, w, h);
    stbi_image_free(data);
    if (!bmp)
        return nullptr;
    auto *img = new Canvas2DImage();
    img->bitmap = bmp;
    img->width = w;
    img->height = h;
    return img;
}

Canvas2DImage *Canvas2D::loadImageFromMemory(const unsigned char *data, int byteLen)
{
    int w, h, ch;
    stbi_set_flip_vertically_on_load(0);
    uint8_t *px = stbi_load_from_memory(data, byteLen, &w, &h, &ch, 4);
    if (!px)
        return nullptr;
    auto bmp = createBitmapFromRGBA(dc(), px, w, h);
    stbi_image_free(px);
    if (!bmp)
        return nullptr;
    auto *img = new Canvas2DImage();
    img->bitmap = bmp;
    img->width = w;
    img->height = h;
    return img;
}

Canvas2DImage *Canvas2D::wrapBitmap(ComPtr<ID2D1Bitmap1> bmp, int w, int h)
{
    auto *img = new Canvas2DImage();
    img->bitmap = bmp;
    img->width = w;
    img->height = h;
    return img;
}

void Canvas2D::updateImage(Canvas2DImage *img, const unsigned char *rgba, int w, int h)
{
    if (!img)
        return;
    auto bmp = createBitmapFromRGBA(dc(), rgba, w, h);
    img->bitmap = bmp;
    img->width = w;
    img->height = h;
}

void Canvas2D::freeImage(Canvas2DImage *img) { delete img; }

static void drawBitmapRegion(ID2D1DeviceContext *dc,
                             ID2D1Bitmap *bmp,
                             float sx, float sy, float sw, float sh,
                             float dx, float dy, float dw, float dh,
                             float alpha)
{
    D2D1_RECT_F src = makeRect(sx, sy, sw, sh);
    D2D1_RECT_F dst = makeRect(dx, dy, dw, dh);
    dc->DrawBitmap(bmp, &dst, alpha,
                   D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, &src);
}

void Canvas2D::drawImage(const Canvas2DImage *img, float dx, float dy)
{
    if (!img || !img->bitmap)
        return;
    drawBitmapRegion(dc(), img->bitmap.Get(),
                     0, 0, (float)img->width, (float)img->height,
                     dx, dy, (float)img->width, (float)img->height,
                     globalAlpha_);
}
void Canvas2D::drawImage(const Canvas2DImage *img,
                         float dx, float dy, float dw, float dh)
{
    if (!img || !img->bitmap)
        return;
    drawBitmapRegion(dc(), img->bitmap.Get(),
                     0, 0, (float)img->width, (float)img->height,
                     dx, dy, dw, dh, globalAlpha_);
}
void Canvas2D::drawImage(const Canvas2DImage *img,
                         float sx, float sy, float sw, float sh,
                         float dx, float dy, float dw, float dh)
{
    if (!img || !img->bitmap)
        return;
    drawBitmapRegion(dc(), img->bitmap.Get(),
                     sx, sy, sw, sh, dx, dy, dw, dh, globalAlpha_);
}

// ─────────────────────────────────────────────────────────────────────────────
// Text
// ─────────────────────────────────────────────────────────────────────────────

bool Canvas2D::registerFont(Canvas2DD2D *d2d,
                            const std::string &name,
                            const std::string &path)
{
    return Canvas2DD2D::registerFont(d2d, name, path);
}

void Canvas2D::parseFontDesc(const std::string &desc)
{
    fontBold_ = fontItalic_ = false;
    fontSize_ = 14.f;
    fontFace_ = "sans";

    std::istringstream ss(desc);
    std::string tok, family;
    while (ss >> tok)
    {
        if (tok == "bold")
        {
            fontBold_ = true;
            continue;
        }
        if (tok == "italic")
        {
            fontItalic_ = true;
            continue;
        }
        if (tok.size() > 2 && tok.compare(tok.size() - 2, 2, "px") == 0)
        {
            fontSize_ = std::stof(tok.substr(0, tok.size() - 2));
            std::getline(ss, family);
            auto start = family.find_first_not_of(" \t");
            if (start != std::string::npos)
                family = family.substr(start);
            break;
        }
    }
    if (!family.empty())
        fontFace_ = family;
}

void Canvas2D::setFont(const std::string &desc) { parseFontDesc(desc); }
void Canvas2D::setTextAlign(CanvasTextAlign a) { textAlign_ = a; }
void Canvas2D::setTextBaseline(TextBaseline b) { textBaseline_ = b; }

int Canvas2D::resolveFont() const
{
    if (fontBold_ && fontItalic_)
    {
        int i = d2d_->findFont(fontFace_ + "-bold-italic");
        if (i >= 0)
            return i;
    }
    if (fontBold_)
    {
        int i = d2d_->findFont(fontFace_ + "-bold");
        if (i >= 0)
            return i;
    }
    if (fontItalic_)
    {
        int i = d2d_->findFont(fontFace_ + "-italic");
        if (i >= 0)
            return i;
    }
    int i = d2d_->findFont(fontFace_);
    if (i >= 0)
        return i;
    return d2d_->fonts.empty() ? -1 : 0;
}

void Canvas2D::fillText(const std::string &text, float x, float y, float /*maxW*/)
{
    if (text.empty())
        return;
    int fontIdx = resolveFont();
    if (fontIdx < 0)
        return;
    int ps = std::max(4, int(fontSize_ + 0.5f));

    FT_Face face = d2d_->fonts[fontIdx].ftFace;
    FT_Set_Pixel_Sizes(face, 0, FT_UInt(ps));

    float scale = float(ps) / float(face->units_per_EM);
    float ascender = float(face->ascender) * scale;

    // Measure for alignment
    float totalW = 0;
    for (unsigned char ch : text)
    {
        const auto *g = d2d_->getGlyph(fontIdx, ch, ps);
        if (g)
            totalW += float(g->advance);
    }

    float px = x;
    if (textAlign_ == CanvasTextAlign::Center)
        px = x - totalW * 0.5f;
    if (textAlign_ == CanvasTextAlign::Right)
        px = x - totalW;

    float py = y;
    if (textBaseline_ == TextBaseline::Top)
        py = y + ascender;
    if (textBaseline_ == TextBaseline::Middle)
        py = y + ascender * 0.5f;
    if (textBaseline_ == TextBaseline::Bottom)
    {
        float desc = float(face->descender) * scale;
        py = y + desc;
    }

    // Create one solid brush with the fill color for all glyphs
    ComPtr<ID2D1SolidColorBrush> textBrush;
    D2D1_COLOR_F tc = toD2D(fillColor_); // already bakes in globalAlpha_
    dc()->CreateSolidColorBrush(tc, textBrush.GetAddressOf());
    if (!textBrush)
        return;

    // FillOpacityMask requires aliased antialias mode
    dc()->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);

    for (unsigned char ch : text)
    {
        const auto *g = d2d_->getGlyph(fontIdx, ch, ps);
        if (!g)
            continue;
        if (g->glyphW > 0 && g->glyphH > 0)
        {
            float gx = px + float(g->xoff);
            float gy = py + float(g->yoff);

            D2D1_RECT_F src = D2D1::RectF(float(g->atlasX), float(g->atlasY),
                                          float(g->atlasX + g->glyphW),
                                          float(g->atlasY + g->glyphH));
            D2D1_RECT_F dst = D2D1::RectF(gx, gy,
                                          gx + float(g->glyphW),
                                          gy + float(g->glyphH));

            dc()->FillOpacityMask(
                d2d_->atlasBitmap.Get(),
                textBrush.Get(),
                D2D1_OPACITY_MASK_CONTENT_TEXT_NATURAL,
                &dst,
                &src);
        }
        px += float(g->advance);
    }

    dc()->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
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
    if (text.empty())
        return 0.f;
    int fontIdx = resolveFont();
    if (fontIdx < 0)
        return float(text.size()) * fontSize_ * 0.6f;
    int ps = std::max(4, int(fontSize_ + 0.5f));
    float total = 0;
    for (unsigned char ch : text)
    {
        const auto *g = d2d_->getGlyph(fontIdx, ch, ps);
        if (g)
            total += float(g->advance);
    }
    return total;
}

// ─────────────────────────────────────────────────────────────────────────────
// Pixel access
// ─────────────────────────────────────────────────────────────────────────────

void Canvas2D::getImageData(float x, float y, float w, float h,
                            std::vector<uint8_t> &out)
{
    int iw = int(w), ih = int(h);
    out.assign(size_t(iw) * ih * 4, 0);
    // Copy sub-rect from the offscreen bitmap
    D2D1_POINT_2U dst = D2D1::Point2U(0, 0);
    D2D1_RECT_U src = D2D1::RectU((UINT32)x, (UINT32)y,
                                  (UINT32)(x + w), (UINT32)(y + h));

    // Create a temp CPU-readable bitmap
    D2D1_BITMAP_PROPERTIES bmpProps = D2D1::BitmapProperties(
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    ComPtr<ID2D1Bitmap> tmp;
    dc()->CreateBitmap(D2D1::SizeU(iw, ih), nullptr, 0, bmpProps, tmp.GetAddressOf());
    if (tmp)
    {
        tmp->CopyFromBitmap(&dst, d2d_->offscreenBitmap.Get(), &src);
        // Note: CopyFromBitmap into a CPU-readable surface requires a staging
        // texture in D3D11. For now we return zeros — full readback needs
        // a D3D11 staging texture path (add if needed).
    }
    // TODO: Full GPU→CPU readback via D3D11 staging texture
}

void Canvas2D::putImageData(const std::vector<uint8_t> &data,
                            int srcW, int srcH, float dx, float dy)
{
    auto bmp = createBitmapFromRGBA(dc(), data.data(), srcW, srcH);
    if (!bmp)
        return;
    D2D1_RECT_F dst = makeRect(dx, dy, (float)srcW, (float)srcH);
    D2D1_RECT_F src = makeRect(0, 0, (float)srcW, (float)srcH);
    dc()->DrawBitmap(bmp.Get(), &dst, globalAlpha_,
                     D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR, &src);
}

#endif // _WIN32