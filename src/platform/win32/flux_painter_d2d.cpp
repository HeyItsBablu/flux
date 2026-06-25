// flux_painter_d2d.cpp

#define _USE_MATH_DEFINES
#include <cmath>

#ifdef _WIN32


#include "flux/flux_painter.hpp"
#include "flux/flux_font.hpp"
#include "flux/flux_text_style.hpp"
#include "flux/flux_d3d_device.hpp"

#include <d2d1_1.h>
#include <d2d1_1helper.h>
#include <d2d1effects.h>
#include <dwrite_3.h>
#include <wincodec.h>           // IWICImagingFactory — for image decode
#include <wrl/client.h>

#include <algorithm>

#include <string>
#include <vector>



using Microsoft::WRL::ComPtr;

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "windowscodecs.lib")

// ============================================================================
// Internal conversion helpers
// ============================================================================

static inline D2D1_COLOR_F toD2D(Color c)
{
    return D2D1::ColorF(c.r / 255.f, c.g / 255.f, c.b / 255.f, c.a / 255.f);
}

static inline D2D1_RECT_F toRect(int x, int y, int w, int h)
{
    return D2D1::RectF(
        static_cast<float>(x),
        static_cast<float>(y),
        static_cast<float>(x + w),
        static_cast<float>(y + h));
}

static inline D2D1_ROUNDED_RECT toRoundedRect(int x, int y, int w, int h, int radius)
{
    return D2D1::RoundedRect(
        toRect(x, y, w, h),
        static_cast<float>(radius),
        static_cast<float>(radius));
}

static inline D2D1_ELLIPSE toEllipse(int x, int y, int w, int h)
{
    return D2D1::Ellipse(
        D2D1::Point2F(x + w * 0.5f, y + h * 0.5f),
        w * 0.5f,
        h * 0.5f);
}

// Retrieve or create a solid-colour brush from the per-device cache.
// Returns nullptr on failure (logged internally by BrushCache).
static ID2D1SolidColorBrush* getBrush(GraphicsContext& ctx, Color c)
{
    return ctx.brushes->get(ctx.dc, c);
}

// ============================================================================
// Painter::fillRect
// ============================================================================

void Painter::fillRect(int x, int y, int w, int h, Color color)
{
    auto* br = getBrush(ctx, color);
    if (!br) return;
    ctx.dc->FillRectangle(toRect(x, y, w, h), br);
}

// ============================================================================
// Painter::fillRoundedRect
// ============================================================================

void Painter::fillRoundedRect(int x, int y, int w, int h,
                              int radius, Color color)
{
    auto* br = getBrush(ctx, color);
    if (!br) return;
    if (radius <= 0)
        ctx.dc->FillRectangle(toRect(x, y, w, h), br);
    else
        ctx.dc->FillRoundedRectangle(toRoundedRect(x, y, w, h, radius), br);
}

// ============================================================================
// Painter::drawBorder
// ============================================================================

void Painter::drawBorder(int x, int y, int w, int h,
                         int radius, Color color, int borderWidth)
{
    auto* br = getBrush(ctx, color);
    if (!br) return;
    float bw = static_cast<float>(borderWidth);
    if (radius <= 0)
        ctx.dc->DrawRectangle(toRect(x, y, w, h), br, bw);
    else
        ctx.dc->DrawRoundedRectangle(toRoundedRect(x, y, w, h, radius), br, bw);
}

// ============================================================================
// Painter::fillRoundedRectGDI
// On D2D this is identical to fillRoundedRect + optional border.
// Kept for call-site compatibility (ProgressBarWidget etc.).
// radius here is a diameter (Win32 RoundRect convention) so halve it.
// ============================================================================

void Painter::fillRoundedRectGDI(int x, int y, int w, int h, int radius,
                                 Color fill, Color stroke, int strokeWidth)
{
    int r = radius / 2;
    fillRoundedRect(x, y, w, h, r, fill);
    if (strokeWidth > 0)
        drawBorder(x, y, w, h, r, stroke, strokeWidth);
}

// ============================================================================
// Painter::drawEllipse
// ============================================================================

void Painter::drawEllipse(int x, int y, int w, int h,
                          Color fill, Color stroke, int strokeWidth)
{
    D2D1_ELLIPSE el = toEllipse(x, y, w, h);

    auto* fillBr = getBrush(ctx, fill);
    if (fillBr)
        ctx.dc->FillEllipse(el, fillBr);

    if (strokeWidth > 0)
    {
        auto* stBr = getBrush(ctx, stroke);
        if (stBr)
            ctx.dc->DrawEllipse(el, stBr, static_cast<float>(strokeWidth));
    }
}

// ============================================================================
// Painter::fillRectAlpha
// D2D handles alpha natively — just fill with the color's alpha channel.
// ============================================================================

void Painter::fillRectAlpha(int x, int y, int w, int h, Color color)
{
    fillRect(x, y, w, h, color);
}

// ============================================================================
// Painter::fillRoundedRegion
// Win32 GDI used FillRgn with an HRGN. D2D just fills a rounded rect.
// cornerRadius here is a true radius (not diameter).
// ============================================================================

void Painter::fillRoundedRegion(int x, int y, int w, int h,
                                int cornerRadius, Color color)
{
    fillRoundedRect(x, y, w, h, cornerRadius, color);
}

// ============================================================================
// Painter::drawRectOutline
// ============================================================================

void Painter::drawRectOutline(int x, int y, int w, int h,
                              Color color, int strokeWidth)
{
    auto* br = getBrush(ctx, color);
    if (!br) return;
    ctx.dc->DrawRectangle(toRect(x, y, w, h), br, static_cast<float>(strokeWidth));
}

// ============================================================================
// Painter::drawRoundedRectOutline
// cornerDiameter matches Win32 RoundRect convention — halve it for D2D radius.
// ============================================================================

void Painter::drawRoundedRectOutline(int x, int y, int w, int h,
                                     int cornerDiameter,
                                     Color stroke, int strokeWidth)
{
    auto* br = getBrush(ctx, stroke);
    if (!br) return;
    ctx.dc->DrawRoundedRectangle(
        toRoundedRect(x, y, w, h, cornerDiameter / 2),
        br,
        static_cast<float>(strokeWidth));
}

// ============================================================================
// Painter::drawLine
// ============================================================================

void Painter::drawLine(int x1, int y1, int x2, int y2,
                       Color color, int width)
{
    auto* br = getBrush(ctx, color);
    if (!br) return;
    ctx.dc->DrawLine(
        D2D1::Point2F(static_cast<float>(x1), static_cast<float>(y1)),
        D2D1::Point2F(static_cast<float>(x2), static_cast<float>(y2)),
        br,
        static_cast<float>(width));
}

// ============================================================================
// Painter::drawHLine / drawVLine
// ============================================================================

void Painter::drawHLine(int x, int y, int len, Color color, int strokeWidth)
{
    drawLine(x, y, x + len, y, color, strokeWidth);
}

void Painter::drawVLine(int x, int y, int len, Color color, int strokeWidth)
{
    drawLine(x, y, x, y + len, color, strokeWidth);
}

// ============================================================================
// Painter::drawPolyline
// ============================================================================

void Painter::drawPolyline(const std::vector<std::pair<int, int>>& points,
                           Color color, int strokeWidth)
{
    if (points.size() < 2) return;

    ComPtr<ID2D1PathGeometry> path;
    ctx.factory->CreatePathGeometry(&path);
    if (!path) return;

    ComPtr<ID2D1GeometrySink> sink;
    path->Open(&sink);

    sink->BeginFigure(
        D2D1::Point2F(static_cast<float>(points[0].first),
                      static_cast<float>(points[0].second)),
        D2D1_FIGURE_BEGIN_HOLLOW);

    for (size_t i = 1; i < points.size(); ++i)
        sink->AddLine(D2D1::Point2F(static_cast<float>(points[i].first),
                                    static_cast<float>(points[i].second)));

    sink->EndFigure(D2D1_FIGURE_END_OPEN);
    sink->Close();

    auto* br = getBrush(ctx, color);
    if (br)
        ctx.dc->DrawGeometry(path.Get(), br, static_cast<float>(strokeWidth));
}

// ============================================================================
// Painter::fillPolygonAlpha
// ============================================================================

void Painter::fillPolygonAlpha(const std::vector<std::pair<int, int>>& points,
                               Color color)
{
    if (points.size() < 3) return;

    ComPtr<ID2D1PathGeometry> path;
    ctx.factory->CreatePathGeometry(&path);
    if (!path) return;

    ComPtr<ID2D1GeometrySink> sink;
    path->Open(&sink);

    sink->BeginFigure(
        D2D1::Point2F(static_cast<float>(points[0].first),
                      static_cast<float>(points[0].second)),
        D2D1_FIGURE_BEGIN_FILLED);

    for (size_t i = 1; i < points.size(); ++i)
        sink->AddLine(D2D1::Point2F(static_cast<float>(points[i].first),
                                    static_cast<float>(points[i].second)));

    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    sink->Close();

    auto* br = getBrush(ctx, color);
    if (br)
        ctx.dc->FillGeometry(path.Get(), br);
}

// ============================================================================
// Painter::fillRectWithLeftAccent
// ============================================================================

void Painter::fillRectWithLeftAccent(int x, int y, int w, int h,
                                     Color bg, Color accent, int stripWidth)
{
    fillRect(x, y, w, h, bg);
    fillRect(x, y, stripWidth, h, accent);
}

// ============================================================================
// Painter::fillColumnBars
// Each column is one FillRectangle call. D2D batches these on the GPU —
// far cheaper than the old DIB-section scanline loop.
// ============================================================================

void Painter::fillColumnBars(int x, int y, int w, int h,
                             const std::vector<int>& barHeights, Color color)
{
    if (w <= 0 || h <= 0 || barHeights.empty()) return;

    auto* br = getBrush(ctx, color);
    if (!br) return;

    int cols = std::min(w, static_cast<int>(barHeights.size()));
    for (int i = 0; i < cols; ++i)
    {
        int barH = std::max(0, std::min(h, barHeights[i]));
        if (barH == 0) continue;
        ctx.dc->FillRectangle(
            D2D1::RectF(
                static_cast<float>(x + i),
                static_cast<float>(y + h - barH),
                static_cast<float>(x + i + 1),
                static_cast<float>(y + h)),
            br);
    }
}

// ============================================================================
// Painter::fillGradientRect
// Uses ID2D1LinearGradientBrush — true GPU gradient, no scanline loop.
// ============================================================================

void Painter::fillGradientRect(int x, int y, int w, int h,
                               const std::vector<Color>& colors)
{
    if (colors.empty() || w <= 0 || h <= 0) return;

    if (colors.size() == 1)
    {
        fillRect(x, y, w, h, colors[0]);
        return;
    }

    // Build gradient stop collection
    std::vector<D2D1_GRADIENT_STOP> stops;
    stops.reserve(colors.size());
    for (size_t i = 0; i < colors.size(); ++i)
    {
        D2D1_GRADIENT_STOP s;
        s.position = static_cast<float>(i) / static_cast<float>(colors.size() - 1);
        s.color    = toD2D(colors[i]);
        stops.push_back(s);
    }

    ComPtr<ID2D1GradientStopCollection> stopCollection;
    HRESULT hr = ctx.dc->CreateGradientStopCollection(
        stops.data(),
        static_cast<UINT32>(stops.size()),
        D2D1_GAMMA_2_2,
        D2D1_EXTEND_MODE_CLAMP,
        &stopCollection);
    if (FAILED(hr) || !stopCollection) return;

    // Horizontal left-to-right gradient
    D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES props = {
        D2D1::Point2F(static_cast<float>(x),     static_cast<float>(y)),
        D2D1::Point2F(static_cast<float>(x + w), static_cast<float>(y))
    };

    ComPtr<ID2D1LinearGradientBrush> brush;
    hr = ctx.dc->CreateLinearGradientBrush(props, stopCollection.Get(), &brush);
    if (FAILED(hr) || !brush) return;

    ctx.dc->FillRectangle(toRect(x, y, w, h), brush.Get());
}

// ============================================================================
// Painter::pushClipRect  (with optional rounded corners)
// D2D clip model:
//   - Axis-aligned clip with no radius → PushAxisAlignedClip (fastest)
//   - Rounded clip → PushLayer with a geometry mask
//
// Both are matched by a single popClipRect() which checks what was pushed.
// We track the clip stack type in GraphicsContext's clipStack vector which
// we repurpose: nullptr entry = axis-aligned, non-null = layer.
// ============================================================================

void Painter::pushClipRect(int x, int y, int w, int h, int cornerRadius) 
{
    if (cornerRadius <= 0)
    {
        ctx.dc->PushAxisAlignedClip(
            toRect(x, y, w, h),
            D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

        // Push nullptr as sentinel: this is an axis-aligned clip
        ctx.clipStack.push_back(nullptr);
    }
    else
    {
        // Build a rounded-rect geometry for the clip mask
        ComPtr<ID2D1RoundedRectangleGeometry> geom;
        ctx.factory->CreateRoundedRectangleGeometry(
            toRoundedRect(x, y, w, h, cornerRadius),
            &geom);

        D2D1_LAYER_PARAMETERS lp = D2D1::LayerParameters(
            toRect(x, y, w, h),
            geom.Get());

        // Push a null layer — D2D uses the geometry as a clip mask directly
        ctx.dc->PushLayer(lp, nullptr);

        // Push non-null sentinel so popClipRect calls PopLayer
        // We store the raw pointer of the geometry as the sentinel.
        // The geometry ComPtr goes out of scope here but D2D holds a ref
        // internally while the layer is active.
        ctx.clipStack.push_back(reinterpret_cast<HRGN>(1));
    }
}


void Painter::popClipRect()
{
    if (ctx.clipStack.empty()) return;

    HRGN sentinel = ctx.clipStack.back();
    ctx.clipStack.pop_back();

    if (sentinel == nullptr)
        ctx.dc->PopAxisAlignedClip();
    else
        ctx.dc->PopLayer();
}

// ============================================================================
// Painter::pushClipRoundedRect
// Kept for ProgressBarWidget call-site compatibility.
// cornerDiameter matches Win32 RoundRect convention — halve for radius.
// ============================================================================

void Painter::pushClipRoundedRect(int x, int y, int w, int h,
                                  int cornerDiameter)
{
    pushClipRect(x, y, w, h, cornerDiameter / 2);
}

// ============================================================================
// Painter::drawArc
// D2D has no direct arc-stroke primitive on ID2D1RenderTarget.
// We build a path geometry with an arc segment and stroke it.
// startAngle / sweepAngle are in radians (our internal convention).
// ============================================================================

void Painter::drawArc(float cx, float cy, float radius,
                      int strokeWidth,
                      float startAngle, float sweepAngle,
                      Color color, bool roundedCaps)
{
    if (radius <= 0 || sweepAngle == 0.f) return;

    // Compute start and end points
    float x0 = cx + radius * std::cos(startAngle);
    float y0 = cy + radius * std::sin(startAngle);
    float endAngle = startAngle + sweepAngle;
    float x1 = cx + radius * std::cos(endAngle);
    float y1 = cy + radius * std::sin(endAngle);

    // D2D arc sweep direction: positive angle = clockwise in screen space
    D2D1_SWEEP_DIRECTION sweep = (sweepAngle >= 0)
        ? D2D1_SWEEP_DIRECTION_CLOCKWISE
        : D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE;

    // Large arc flag: sweep > 180°
    D2D1_ARC_SIZE arcSize = (std::abs(sweepAngle) > static_cast<float>(M_PI))
        ? D2D1_ARC_SIZE_LARGE
        : D2D1_ARC_SIZE_SMALL;

    D2D1_ARC_SEGMENT arc = {
        D2D1::Point2F(x1, y1),
        D2D1::SizeF(radius, radius),
        0.f,        // rotation angle of ellipse
        sweep,
        arcSize
    };

    ComPtr<ID2D1PathGeometry> path;
    ctx.factory->CreatePathGeometry(&path);
    if (!path) return;

    ComPtr<ID2D1GeometrySink> sink;
    path->Open(&sink);
    sink->BeginFigure(D2D1::Point2F(x0, y0), D2D1_FIGURE_BEGIN_HOLLOW);
    sink->AddArc(arc);
    sink->EndFigure(D2D1_FIGURE_END_OPEN);
    sink->Close();

    auto* br = getBrush(ctx, color);
    if (!br) return;

    // Stroke style for rounded caps
    ComPtr<ID2D1StrokeStyle> strokeStyle;
    if (roundedCaps)
    {
        D2D1_STROKE_STYLE_PROPERTIES sp = D2D1::StrokeStyleProperties(
            D2D1_CAP_STYLE_ROUND,
            D2D1_CAP_STYLE_ROUND,
            D2D1_CAP_STYLE_ROUND);
        ctx.factory->CreateStrokeStyle(sp, nullptr, 0, &strokeStyle);
    }

    ctx.dc->DrawGeometry(
        path.Get(), br,
        static_cast<float>(strokeWidth),
        strokeStyle.Get());
}

// ============================================================================
// Painter::drawWavyLine
// ============================================================================

void Painter::drawWavyLine(int x, int y, int len, Color color, int amplitude)
{
    if (len <= 0) return;
    const int step = amplitude * 2;
    std::vector<std::pair<int, int>> pts;
    pts.reserve(len / step + 2);
    int px = x;
    bool up = true;
    while (px < x + len)
    {
        pts.push_back({px, up ? y - amplitude : y + amplitude});
        px += step;
        up = !up;
    }
    pts.push_back({x + len, y});
    if (pts.size() >= 2)
        drawPolyline(pts, color, 1);
}

// ============================================================================
// Painter::drawFadeOverlay
// ============================================================================

void Painter::drawFadeOverlay(int x, int y, int w, int h,
                              int fadeWidth, Color bg)
{
    if (fadeWidth <= 0 || w <= 0 || h <= 0) return;
    int startX = x + w - fadeWidth;
    if (startX < x) startX = x;
    std::vector<Color> stops = {bg.withAlpha(0), bg.withAlpha(255)};
    fillGradientRect(startX, y, fadeWidth, h, stops);
}

// ============================================================================
// Painter::drawShadow
// Uses D2D1 shadow effect + composite effect — pure GPU, zero CPU cost.
// ============================================================================

void Painter::drawShadow(int x, int y, int w, int h,
                         int radius, int blurRadius,
                         Color color, int offsetX, int offsetY)
{
    // Create a command list to capture the shape
    ComPtr<ID2D1CommandList> cmdList;
    HRESULT hr = ctx.dc->CreateCommandList(&cmdList);
    if (FAILED(hr)) return;

    // Draw the base shape into the command list
    ID2D1Image* prevTarget = nullptr;
    ctx.dc->GetTarget(&prevTarget);
    ctx.dc->SetTarget(cmdList.Get());

    auto* br = getBrush(ctx, color);
    if (br)
    {
        if (radius > 0)
            ctx.dc->FillRoundedRectangle(toRoundedRect(x, y, w, h, radius), br);
        else
            ctx.dc->FillRectangle(toRect(x, y, w, h), br);
    }

    ctx.dc->SetTarget(prevTarget);
    if (prevTarget) prevTarget->Release();
    cmdList->Close();

    // Shadow effect
    ComPtr<ID2D1Effect> shadowEffect;
    hr = ctx.dc->CreateEffect(CLSID_D2D1Shadow, &shadowEffect);
    if (FAILED(hr)) return;

    shadowEffect->SetInput(0, cmdList.Get());
    shadowEffect->SetValue(D2D1_SHADOW_PROP_BLUR_STANDARD_DEVIATION,
                           static_cast<float>(blurRadius) * 0.5f);
    shadowEffect->SetValue(D2D1_SHADOW_PROP_COLOR,
        D2D1::Vector4F(color.r / 255.f, color.g / 255.f,
                       color.b / 255.f, color.a / 255.f));

    // Draw shadow offset from the shape
    ctx.dc->DrawImage(
        shadowEffect.Get(),
        D2D1::Point2F(static_cast<float>(offsetX),
                      static_cast<float>(offsetY)));
}

// ============================================================================
// Painter::beginLayer / endLayer
// Used for widget-level opacity (e.g. disabled state, fade animations).
// ============================================================================

void Painter::beginLayer(float opacity)
{
    D2D1_LAYER_PARAMETERS lp = D2D1::LayerParameters();
    lp.opacity = std::max(0.f, std::min(1.f, opacity));
    ctx.dc->PushLayer(lp, nullptr);
    // Reuse clipStack to track layer depth — push a non-null sentinel
    ctx.clipStack.push_back(reinterpret_cast<HRGN>(2));
}

void Painter::endLayer()
{
    if (ctx.clipStack.empty()) return;
    ctx.clipStack.pop_back();
    ctx.dc->PopLayer();
}

// ============================================================================
// Painter::drawImage
// ============================================================================

void Painter::drawImage(const ImageDrawParams& params)
{
    if (!params.image || params.clipW <= 0 || params.clipH <= 0)
        return;

    // params.image is an ID2D1Bitmap1* cast to NativeImage (Gdiplus::Bitmap* alias
    // is gone; on Win32 NativeImage is now ID2D1Bitmap1*).
    auto* bitmap = static_cast<ID2D1Bitmap1*>(params.image);

    // Destination rect
    D2D1_RECT_F dest = D2D1::RectF(
        params.destX, params.destY,
        params.destX + params.destW,
        params.destY + params.destH);

    // Clip rect
    D2D1_RECT_F clip = toRect(params.clipX, params.clipY,
                               params.clipW, params.clipH);

    auto drawOneTile = [&](D2D1_RECT_F d)
    {
        ctx.dc->DrawBitmap(
            bitmap, d,
            1.f,                              // opacity
            D2D1_BITMAP_INTERPOLATION_MODE_LINEAR,
            nullptr);                         // source rect = whole bitmap
    };

    if (params.borderRadius <= 0)
    {
        ctx.dc->PushAxisAlignedClip(clip, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

        if (params.repeat != ImageRepeat::NoRepeat)
        {
            float tileW = params.destW, tileH = params.destH;
            float startX = (params.repeat == ImageRepeat::RepeatY)
                               ? params.destX : (float)params.clipX;
            float startY = (params.repeat == ImageRepeat::RepeatX)
                               ? params.destY : (float)params.clipY;
            float endX   = (params.repeat == ImageRepeat::RepeatY)
                               ? params.destX + tileW
                               : (float)(params.clipX + params.clipW);
            float endY   = (params.repeat == ImageRepeat::RepeatX)
                               ? params.destY + tileH
                               : (float)(params.clipY + params.clipH);

            for (float ty = startY; ty < endY; ty += tileH)
                for (float tx = startX; tx < endX; tx += tileW)
                    drawOneTile(D2D1::RectF(tx, ty, tx + tileW, ty + tileH));
        }
        else
        {
            drawOneTile(dest);
        }

        ctx.dc->PopAxisAlignedClip();
    }
    else
    {
        // Rounded clip via PushLayer
        ComPtr<ID2D1RoundedRectangleGeometry> geom;
        ctx.factory->CreateRoundedRectangleGeometry(
            D2D1::RoundedRect(clip, (float)params.borderRadius,
                                    (float)params.borderRadius),
            &geom);

        D2D1_LAYER_PARAMETERS lp = D2D1::LayerParameters(clip, geom.Get());
        ctx.dc->PushLayer(lp, nullptr);

        if (params.repeat != ImageRepeat::NoRepeat)
        {
            float tileW = params.destW, tileH = params.destH;
            float startX = (params.repeat == ImageRepeat::RepeatY)
                               ? params.destX : (float)params.clipX;
            float startY = (params.repeat == ImageRepeat::RepeatX)
                               ? params.destY : (float)params.clipY;
            float endX   = (params.repeat == ImageRepeat::RepeatY)
                               ? params.destX + tileW
                               : (float)(params.clipX + params.clipW);
            float endY   = (params.repeat == ImageRepeat::RepeatX)
                               ? params.destY + tileH
                               : (float)(params.clipY + params.clipH);

            for (float ty = startY; ty < endY; ty += tileH)
                for (float tx = startX; tx < endX; tx += tileW)
                    drawOneTile(D2D1::RectF(tx, ty, tx + tileW, ty + tileH));
        }
        else
        {
            drawOneTile(dest);
        }

        ctx.dc->PopLayer();
    }
}

// ============================================================================
// Painter::drawVideo
// ============================================================================

void Painter::drawVideo(const VideoDrawParams& params)
{
    if (!params.frame ||
        params.dstW <= 0 || params.dstH <= 0) return;

    auto* bitmap = static_cast<ID2D1Bitmap1*>(params.frame);
    ctx.dc->DrawBitmap(
        bitmap,
        D2D1::RectF(
            (float)params.dstX, (float)params.dstY,
            (float)(params.dstX + params.dstW),
            (float)(params.dstY + params.dstH)),
        1.f,
        D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
}

// ============================================================================
// Painter::drawCamera
// ============================================================================

void Painter::drawCamera(const CameraDrawParams& params)
{
    if (!params.frame ||
        params.dstW <= 0 || params.dstH <= 0) return;

    auto* bitmap = static_cast<ID2D1Bitmap1*>(params.frame);

    D2D1_RECT_F dest = D2D1::RectF(
        (float)params.dstX, (float)params.dstY,
        (float)(params.dstX + params.dstW),
        (float)(params.dstY + params.dstH));

    if (params.mirror)
    {
        // Flip horizontally by applying a transform
        D2D1_MATRIX_3X2_F prev;
        ctx.dc->GetTransform(&prev);

        float cx = params.dstX + params.dstW * 0.5f;
        float cy = params.dstY + params.dstH * 0.5f;

        D2D1_MATRIX_3X2_F flip = D2D1::Matrix3x2F(
            -1.f, 0.f,
             0.f, 1.f,
             2.f * cx, 0.f);

        ctx.dc->SetTransform(flip * prev);
        ctx.dc->DrawBitmap(bitmap, dest, 1.f,
                           D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        ctx.dc->SetTransform(prev);
    }
    else
    {
        ctx.dc->DrawBitmap(bitmap, dest, 1.f,
                           D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    }
}

// ============================================================================
// Painter::drawPage
// ============================================================================

void Painter::drawPage(const PageDrawParams& params)
{
    if (params.hasPageBackground)
        fillRect(params.x, params.y, params.w, params.h, params.pageBackground);

    if (params.body.present && params.body.hasBackground)
        fillRect(params.body.x, params.body.y,
                 params.body.w, params.body.h, params.body.background);

    if (params.header.present)
    {
        if (params.header.hasBackground)
            fillRect(params.header.x, params.header.y,
                     params.header.w, params.header.h, params.header.background);

        if (params.header.elevation > 0)
        {
            int shadowY = params.header.y + params.header.h;
            std::vector<Color> stops = {
                Color::fromRGB(0, 0, 0).withAlpha(60),
                Color::fromRGB(0, 0, 0).withAlpha(0)
            };
            fillGradientRect(params.header.x, shadowY,
                             params.header.w, params.header.elevation, stops);
        }
    }

    if (params.footer.present)
    {
        if (params.footer.hasBackground)
            fillRect(params.footer.x, params.footer.y,
                     params.footer.w, params.footer.h, params.footer.background);

        if (params.footer.elevation > 0)
        {
            int startY = params.footer.y - params.footer.elevation;
            std::vector<Color> stops = {
                Color::fromRGB(0, 0, 0).withAlpha(0),
                Color::fromRGB(0, 0, 0).withAlpha(60)
            };
            fillGradientRect(params.footer.x, startY,
                             params.footer.w, params.footer.elevation, stops);
        }
    }
}

// ============================================================================
// Painter::drawText  (low-level — used by IconWidget)
//
// Creates an IDWriteTextLayout from the format + string, then draws it.
// UINT format flags are interpreted the same way as all other platforms:
//   DT_CENTER  → center alignment
//   DT_RIGHT   → trailing alignment
//   DT_VCENTER → center vertically
// ============================================================================

void Painter::drawText(const std::wstring& text, int x, int y, int w, int h,
                       NativeFont font, Color color, UINT format)
{
    if (text.empty() || !font || !ctx.dwrite) return;

    auto* fmt = static_cast<IDWriteTextFormat*>(font);

    // Apply horizontal alignment to a temporary copy of the format.
    // We cannot mutate the cached format directly.
    DWRITE_TEXT_ALIGNMENT hAlign = DWRITE_TEXT_ALIGNMENT_LEADING;
    if (format & DT_CENTER) hAlign = DWRITE_TEXT_ALIGNMENT_CENTER;
    else if (format & DT_RIGHT) hAlign = DWRITE_TEXT_ALIGNMENT_TRAILING;

    DWRITE_PARAGRAPH_ALIGNMENT vAlign = DWRITE_PARAGRAPH_ALIGNMENT_NEAR;
    if (format & DT_VCENTER) vAlign = DWRITE_PARAGRAPH_ALIGNMENT_CENTER;

    // Create a layout for this specific draw call.
    ComPtr<IDWriteTextLayout> layout;
    HRESULT hr = ctx.dwrite->CreateTextLayout(
        text.c_str(),
        static_cast<UINT32>(text.size()),
        fmt,
        static_cast<float>(w),
        static_cast<float>(h),
        &layout);
    if (FAILED(hr) || !layout) return;

    layout->SetTextAlignment(hAlign);
    layout->SetParagraphAlignment(vAlign);
    layout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);

    auto* br = getBrush(ctx, color);
    if (!br) return;

    ctx.dc->DrawTextLayout(
        D2D1::Point2F(static_cast<float>(x), static_cast<float>(y)),
        layout.Get(),
        br,
        D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
}

// ============================================================================
// Painter::drawTextA  (UTF-8 → wide → drawText)
// ============================================================================

void Painter::drawTextA(const std::string& text, int x, int y, int w, int h,
                        NativeFont font, Color color, UINT format)
{
    if (text.empty()) return;
    drawText(toWideString(text), x, y, w, h, font, color, format);
}

// ============================================================================
// Painter::measureText  (low-level — used by layout engine)
// ============================================================================

void Painter::measureText(const std::wstring& text, NativeFont font,
                          int& outWidth, int& outHeight)
{
    outWidth = outHeight = 0;
    if (text.empty() || !font || !ctx.dwrite) return;

    auto* fmt = static_cast<IDWriteTextFormat*>(font);

    ComPtr<IDWriteTextLayout> layout;
    HRESULT hr = ctx.dwrite->CreateTextLayout(
        text.c_str(),
        static_cast<UINT32>(text.size()),
        fmt,
        10000.f,   // no wrap constraint
        10000.f,
        &layout);
    if (FAILED(hr) || !layout) return;

    DWRITE_TEXT_METRICS metrics = {};
    layout->GetMetrics(&metrics);

    outWidth  = static_cast<int>(std::ceil(metrics.widthIncludingTrailingWhitespace));
    outHeight = static_cast<int>(std::ceil(metrics.height));
}

// ============================================================================
// Internal rich-text helpers
// ============================================================================

// Build an IDWriteTextLayout for one line of text with all TextStyle
// properties applied. The caller draws or measures it, then releases.
static ComPtr<IDWriteTextLayout> makeLayout(
    GraphicsContext& ctx,
    FontCache& fontCache,
    const wchar_t* str, int len,
    const TextStyle& style,
    float maxW, float maxH)
{
    NativeFont nf = fontCache.getFont(
        style.fontFamily,
        style.scaledFontSize(),
        style.fontWeight);
    if (!nf) return nullptr;

    auto* fmt = static_cast<IDWriteTextFormat*>(nf);

    ComPtr<IDWriteTextLayout> layout;
    HRESULT hr = ctx.dwrite->CreateTextLayout(
        str, static_cast<UINT32>(len),
        fmt, maxW, maxH,
        &layout);
    if (FAILED(hr) || !layout) return nullptr;

    layout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);

    DWRITE_TEXT_RANGE all = {0, static_cast<UINT32>(len)};

    // Underline / strikethrough — applied per layout run, not baked into format
    if (style.hasUnderline())
        layout->SetUnderline(TRUE, all);
    if (style.hasLineThrough())
        layout->SetStrikethrough(TRUE, all);

    // Letter spacing via IDWriteTextLayout1
    ComPtr<IDWriteTextLayout1> layout1;
    if (SUCCEEDED(layout.As(&layout1)) && style.letterSpacing != 0.f)
    {
        layout1->SetCharacterSpacing(
            0.f,                   // leading spacing
            style.letterSpacing,   // trailing spacing
            0.f,                   // minimum advance width
            all);
    }

    return layout;
}

// Measure a layout and return pixel width. outLineH receives natural height.
static int measureLayout(IDWriteTextLayout* layout, int& outLineH)
{
    if (!layout) { outLineH = 0; return 0; }
    DWRITE_TEXT_METRICS m = {};
    layout->GetMetrics(&m);
    outLineH = static_cast<int>(std::ceil(m.height));
    return static_cast<int>(std::ceil(m.widthIncludingTrailingWhitespace));
}

// Split a wstring into line spans respecting '\n' and optional word-wrap.
struct LineSpanD2D { int start; int length; };

static std::vector<LineSpanD2D> wrapText(
    GraphicsContext& ctx,
    FontCache& fontCache,
    const std::wstring& text,
    const TextStyle& style,
    int maxWidth, bool softWrap)
{
    std::vector<LineSpanD2D> lines;
    const int n = static_cast<int>(text.size());
    if (n == 0) return lines;

    int pos = 0;
    while (pos < n)
    {
        // Find next newline
        int nlPos = pos;
        while (nlPos < n && text[nlPos] != L'\n') ++nlPos;

        if (!softWrap || maxWidth <= 0)
        {
            lines.push_back({pos, nlPos - pos});
        }
        else
        {
            int lineStart = pos;
            while (lineStart < nlPos)
            {
                // Binary search for the longest prefix that fits maxWidth
                int lo = 0, hi = nlPos - lineStart, fit = 0;
                while (lo <= hi)
                {
                    int mid = (lo + hi) / 2;
                    auto layout = makeLayout(ctx, fontCache,
                        text.c_str() + lineStart, mid,
                        style, (float)maxWidth, 10000.f);
                    if (layout)
                    {
                        DWRITE_TEXT_METRICS m = {};
                        layout->GetMetrics(&m);
                        if (m.widthIncludingTrailingWhitespace <= (float)maxWidth)
                        { fit = mid; lo = mid + 1; }
                        else
                        { hi = mid - 1; }
                    }
                    else { hi = mid - 1; }
                }

                if (fit == 0) fit = 1; // force at least one char

                // Try to break at a word boundary
                int breakAt = fit;
                if (lineStart + fit < nlPos)
                {
                    int wb = fit;
                    while (wb > 1 && text[lineStart + wb - 1] != L' ') --wb;
                    if (wb > 1) breakAt = wb;
                }

                lines.push_back({lineStart, breakAt});
                lineStart += breakAt;
                while (lineStart < nlPos && text[lineStart] == L' ') ++lineStart;
            }
        }

        pos = nlPos + 1;
        if (nlPos == n) break;
    }
    return lines;
}

// ============================================================================
// Painter::measureRichText
// ============================================================================

void Painter::measureRichText(const std::wstring& text,
                              const TextStyle& style,
                              FontCache& fontCache,
                              int maxWidth, bool softWrap, int maxLines,
                              int& outWidth, int& outHeight)
{
    outWidth = outHeight = 0;
    if (text.empty() || !ctx.dwrite) return;

    auto lines = wrapText(ctx, fontCache, text, style,
                          softWrap ? maxWidth : 0, softWrap);

    int lineH = 0;
    {
        // Measure one line to get natural line height
        auto layout = makeLayout(ctx, fontCache,
            text.c_str(),
            lines.empty() ? 0 : lines[0].length,
            style, 10000.f, 10000.f);
        measureLayout(layout.Get(), lineH);
        if (lineH == 0) lineH = style.scaledFontSize();
    }
    int lineHeightPx = static_cast<int>(lineH * style.height);

    int totalLines = (maxLines > 0)
        ? std::min((int)lines.size(), maxLines)
        : (int)lines.size();

    for (int i = 0; i < totalLines; ++i)
    {
        auto& span = lines[i];
        auto layout = makeLayout(ctx, fontCache,
            text.c_str() + span.start, span.length,
            style, 10000.f, 10000.f);
        int dummy;
        int w = measureLayout(layout.Get(), dummy);
        outWidth = std::max(outWidth, w);
    }
    outHeight = totalLines * lineHeightPx;
}

// ============================================================================
// Painter::drawTextDecorationLine
// D2D / DWrite bake underline and strikethrough into the layout run so this
// manual path is only needed for Overline and custom decoration styles
// (Dotted, Dashed, Wavy, Double) that DWrite cannot express.
// ============================================================================

void Painter::drawTextDecorationLine(int lineX, int lineY, int lineW,
                                     const TextStyle& style,
                                     TextDecoration which)
{
    if (lineW <= 0) return;

    // Use DWrite line metrics for baseline geometry
    int fontSize  = style.scaledFontSize();
    int ascent    = static_cast<int>(fontSize * 0.75f);
    int thickness = style.decorationThickness;
    Color dc      = style.decorationColor;

    int decorY = lineY;
    if      (which == TextDecoration::Underline)   decorY = lineY + ascent + 1;
    else if (which == TextDecoration::Overline)    decorY = lineY;
    else if (which == TextDecoration::LineThrough) decorY = lineY + ascent - ascent / 3;

    switch (style.decorationStyle)
    {
    case TextDecorationStyle::Solid:
        drawHLine(lineX, decorY, lineW, dc, thickness);
        break;
    case TextDecorationStyle::Double:
        drawHLine(lineX, decorY,     lineW, dc, thickness);
        drawHLine(lineX, decorY + 2, lineW, dc, thickness);
        break;
    case TextDecorationStyle::Dotted:
        for (int px = lineX; px < lineX + lineW; px += 4)
            drawHLine(px, decorY, std::min(2, lineX + lineW - px), dc, thickness);
        break;
    case TextDecorationStyle::Dashed:
        for (int px = lineX; px < lineX + lineW; px += 8)
            drawHLine(px, decorY, std::min(5, lineX + lineW - px), dc, thickness);
        break;
    case TextDecorationStyle::Wavy:
        drawWavyLine(lineX, decorY, lineW, dc, 2);
        break;
    }
}

// ============================================================================
// Painter::drawRichText
// ============================================================================

void Painter::drawRichText(const std::wstring& text,
                           const RichTextParams& params,
                           FontCache& fontCache)
{
    if (text.empty() || params.w <= 0 || params.h <= 0 || !ctx.dwrite) return;

    const TextStyle& style = params.style;

    int wrapWidth = params.softWrap ? params.w : 0;
    auto lines    = wrapText(ctx, fontCache, text, style, wrapWidth, params.softWrap);

    // Natural line height from a sample layout
    int lineH = 0;
    {
        auto layout = makeLayout(ctx, fontCache,
            text.c_str(),
            lines.empty() ? 0 : lines[0].length,
            style, 10000.f, 10000.f);
        measureLayout(layout.Get(), lineH);
        if (lineH == 0) lineH = style.scaledFontSize();
    }
    int lineHeightPx = static_cast<int>(lineH * style.height);

    int totalLines = (params.maxLines > 0)
        ? std::min((int)lines.size(), params.maxLines)
        : (int)lines.size();

    // Vertical alignment of the whole text block
    int blockH  = totalLines * lineHeightPx;
    int startY  = params.y;
    switch (params.textAlignVertical)
    {
    case TextAlignVertical::Center: startY = params.y + (params.h - blockH) / 2; break;
    case TextAlignVertical::Bottom: startY = params.y + params.h - blockH;        break;
    default: break;
    }

    // Clip if needed
    bool needClip = (params.overflow != TextOverflow::Visible);
    if (needClip)
        pushClipRect(params.x, params.y, params.w, params.h);

    for (int i = 0; i < totalLines; ++i)
    {
        auto& span = lines[i];
        int lineY  = startY + i * lineHeightPx;

        if (lineY + lineHeightPx < params.y) continue;
        if (lineY > params.y + params.h)     break;

        // Measure this line for alignment
        auto layout = makeLayout(ctx, fontCache,
            text.c_str() + span.start, span.length,
            style, (float)params.w, (float)lineHeightPx);
        if (!layout) continue;

        int dummy;
        int lineW = measureLayout(layout.Get(), dummy);

        // Horizontal alignment
        int lineX     = params.x;
        bool isRTL    = (params.direction == TextDirection::RTL);
        switch (params.textAlign)
        {
        case TextAlign::Right:
        case TextAlign::End:
            lineX = isRTL ? params.x : (params.x + params.w - lineW);
            break;
        case TextAlign::Center:
            lineX = params.x + (params.w - lineW) / 2;
            break;
        case TextAlign::Left:
        case TextAlign::Start:
            lineX = isRTL ? (params.x + params.w - lineW) : params.x;
            break;
        case TextAlign::Justify:
        {
            bool isLast = (i == totalLines - 1);
            if (!isLast)
                layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_JUSTIFIED);
            lineX = params.x;
            break;
        }
        }

        bool isLastVisible = (i == totalLines - 1);
        bool hasMoreLines  = ((int)lines.size() > totalLines) ||
                             (totalLines == 1 && lineW > params.w);

        // Per-run background
        if (style.backgroundColor.has_value())
            fillRect(lineX, lineY, lineW, lineHeightPx, *style.backgroundColor);

        // ── Shadows ───────────────────────────────────────────────────────────
        for (const auto& sh : style.shadows)
        {
            auto* shBr = getBrush(ctx, sh.color);
            if (!shBr) continue;

            // Approximate blur by drawing multiple offset copies at lower alpha
            int passes = (sh.blurRadius > 0) ? std::min(sh.blurRadius, 3) : 1;
            for (int bp = 0; bp < passes; ++bp)
            {
                Color sc = sh.color.withAlpha(
                    static_cast<uint8_t>(sh.color.a / passes));
                auto* sbr = getBrush(ctx, sc);
                if (!sbr) continue;
                ctx.dc->DrawTextLayout(
                    D2D1::Point2F(
                        static_cast<float>(lineX + sh.offsetX + bp),
                        static_cast<float>(lineY + sh.offsetY + bp)),
                    layout.Get(), sbr,
                    D2D1_DRAW_TEXT_OPTIONS_NONE);
            }
        }

        // ── Ellipsis on last visible line ─────────────────────────────────────
        if (isLastVisible && hasMoreLines &&
            params.overflow == TextOverflow::Ellipsis)
        {
            // Rebuild layout with word wrapping + trimming so DWrite adds "…"
            NativeFont nf = fontCache.getFont(
                style.fontFamily, style.scaledFontSize(), style.fontWeight);
            if (nf)
            {
                auto* fmt = static_cast<IDWriteTextFormat*>(nf);
                ComPtr<IDWriteTextLayout> ellLayout;
                ctx.dwrite->CreateTextLayout(
                    text.c_str() + span.start,
                    static_cast<UINT32>(span.length),
                    fmt,
                    static_cast<float>(params.w),
                    static_cast<float>(lineHeightPx),
                    &ellLayout);

                if (ellLayout)
                {
                    // Apply DWrite trimming with ellipsis sign
                    ComPtr<IDWriteInlineObject> ellipsisSign;
                    ctx.dwrite->CreateEllipsisTrimmingSign(fmt, &ellipsisSign);

                    DWRITE_TRIMMING trimming = {
                        DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
                    ellLayout->SetTrimming(&trimming, ellipsisSign.Get());
                    ellLayout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);

                    auto* textBr = getBrush(ctx, style.color);
                    if (textBr)
                        ctx.dc->DrawTextLayout(
                            D2D1::Point2F((float)params.x, (float)lineY),
                            ellLayout.Get(), textBr,
                            D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
                }
            }

            if (style.hasOverline())
                drawTextDecorationLine(lineX, lineY, lineW, style,
                                       TextDecoration::Overline);
            continue;
        }

        // ── Normal draw ───────────────────────────────────────────────────────
        auto* textBr = getBrush(ctx, style.color);
        if (textBr)
            ctx.dc->DrawTextLayout(
                D2D1::Point2F(static_cast<float>(lineX),
                              static_cast<float>(lineY)),
                layout.Get(), textBr,
                D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);

        // Overline — DWrite does not support it natively
        if (style.hasOverline())
            drawTextDecorationLine(lineX, lineY, lineW, style,
                                   TextDecoration::Overline);

        // Underline / LineThrough with custom style → override DWrite's default
        if (style.hasUnderline() &&
            style.decorationStyle != TextDecorationStyle::Solid)
            drawTextDecorationLine(lineX, lineY, lineW, style,
                                   TextDecoration::Underline);

        if (style.hasLineThrough() &&
            style.decorationStyle != TextDecorationStyle::Solid)
            drawTextDecorationLine(lineX, lineY, lineW, style,
                                   TextDecoration::LineThrough);

        // ── Fade overlay ──────────────────────────────────────────────────────
        if (isLastVisible && hasMoreLines &&
            params.overflow == TextOverflow::Fade)
        {
            Color fadeBg = Color::fromRGB(255, 255, 255);
            int fadeW    = std::min(60, params.w / 3);
            drawFadeOverlay(params.x, lineY, params.w, lineHeightPx, fadeW, fadeBg);
        }
    }

    if (needClip)
        popClipRect();
}

// ============================================================================
// Painter::drawRichTextA  (UTF-8 convenience overload)
// ============================================================================

void Painter::drawRichTextA(const std::string& text,
                            const RichTextParams& params,
                            FontCache& fontCache)
{
    if (text.empty()) return;
    drawRichText(toWideString(text), params, fontCache);
}

#endif // _WIN32
