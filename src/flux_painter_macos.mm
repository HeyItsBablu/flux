#ifdef __APPLE__

#include <TargetConditionals.h>

#if !TARGET_OS_OSX
    #error "flux_painter_macos.mm is for macOS only"
#endif

#include "flux/flux_painter.hpp"
#import <CoreGraphics/CoreGraphics.h>
#import <CoreText/CoreText.h>

// ── Color helper ──────────────────────────────────────────────────────────────

static void setFillColor(CGContextRef ctx, Color c) {  
    CGContextSetRGBFillColor(ctx,
        c.r / 255.0, c.g / 255.0, c.b / 255.0, c.a / 255.0);
}

static void setStrokeColor(CGContextRef ctx, Color c) {
    CGContextSetRGBStrokeColor(ctx,
        c.r / 255.0, c.g / 255.0, c.b / 255.0, c.a / 255.0);
}

// ── Rounded rect path ─────────────────────────────────────────────────────────

static void addRoundedRectPath(CGContextRef ctx,
                                int x, int y, int w, int h, int radius)
{
    CGFloat r = std::min(radius, std::min(w, h) / 2);
    CGContextBeginPath(ctx);
    CGContextMoveToPoint(ctx, x + r, y);
    CGContextAddArcToPoint(ctx, x + w, y,     x + w, y + h, r);
    CGContextAddArcToPoint(ctx, x + w, y + h, x,     y + h, r);
    CGContextAddArcToPoint(ctx, x,     y + h, x,     y,     r);
    CGContextAddArcToPoint(ctx, x,     y,     x + w, y,     r);
    CGContextClosePath(ctx);
}

// Add a rounded-rect clip path using CGPathCreateWithRoundedRect (10.9+).
static void cgClipRoundedRect(CGContextRef ctx,
                               CGFloat rx, CGFloat ry,
                               CGFloat rw, CGFloat rh,
                               CGFloat radius)
{
    CGFloat r = std::min(radius, std::min(rw * 0.5f, rh * 0.5f));
    CGPathRef path = CGPathCreateWithRoundedRect(
        CGRectMake(rx, ry, rw, rh), r, r, nullptr);
    CGContextAddPath(ctx, path);
    CGContextClip(ctx);
    CGPathRelease(path);
}

static CGInterpolationQuality cgInterpolationFromQuality(FilterQuality q)
{
    switch (q)
    {
    case FilterQuality::None:   return kCGInterpolationNone;
    case FilterQuality::Low:    return kCGInterpolationLow;
    case FilterQuality::Medium: return kCGInterpolationMedium;
    case FilterQuality::High:   return kCGInterpolationHigh;
    }
    return kCGInterpolationLow;
}

// ============================================================================
// Basic shapes
// ============================================================================

void Painter::fillRoundedRect(int x, int y, int w, int h,
                               int radius, Color color)
{
    CGContextRef cg = ctx.cgContext;
    CGContextSaveGState(cg);
    setFillColor(cg, color);
    if (radius > 0)
        addRoundedRectPath(cg, x, y, w, h, radius);
    else
        CGContextAddRect(cg, CGRectMake(x, y, w, h));
    CGContextFillPath(cg);
    CGContextRestoreGState(cg);
}

void Painter::drawBorder(int x, int y, int w, int h,
                          int radius, Color color, int borderWidth)
{
    CGContextRef cg = ctx.cgContext;
    CGContextSaveGState(cg);
    CGContextSetLineWidth(cg, borderWidth);
    setStrokeColor(cg, color);
    if (radius > 0)
        addRoundedRectPath(cg, x, y, w, h, radius);
    else
        CGContextAddRect(cg, CGRectMake(x, y, w, h));
    CGContextStrokePath(cg);
    CGContextRestoreGState(cg);
}

void Painter::fillRect(int x, int y, int w, int h, Color color)
{
    CGContextRef cg = ctx.cgContext;
    CGContextSaveGState(cg);
    setFillColor(cg, color);
    CGContextFillRect(cg, CGRectMake(x, y, w, h));
    CGContextRestoreGState(cg);
}

void Painter::fillRoundedRectGDI(int x, int y, int w, int h, int radius,
                                  Color fill, Color stroke, int strokeWidth)
{
    fillRoundedRect(x, y, w, h, radius * 0.5, fill);
    if (strokeWidth > 0)
        drawBorder(x, y, w, h, radius * 0.5, stroke, strokeWidth);
}

void Painter::drawEllipse(int x, int y, int w, int h,
                           Color fill, Color stroke, int strokeWidth)
{
    CGContextRef cg = ctx.cgContext;
    CGContextSaveGState(cg);
    CGRect r = CGRectMake(x, y, w, h);
    setFillColor(cg, fill);
    CGContextFillEllipseInRect(cg, r);
    if (strokeWidth > 0) {
        CGContextSetLineWidth(cg, strokeWidth);
        setStrokeColor(cg, stroke);
        CGContextStrokeEllipseInRect(cg, r);
    }
    CGContextRestoreGState(cg);
}

void Painter::drawLine(int x1, int y1, int x2, int y2,
                        Color color, int width)
{
    CGContextRef cg = ctx.cgContext;
    CGContextSaveGState(cg);
    CGContextSetLineWidth(cg, width);
    setStrokeColor(cg, color);
    CGContextMoveToPoint(cg, x1, y1);
    CGContextAddLineToPoint(cg, x2, y2);
    CGContextStrokePath(cg);
    CGContextRestoreGState(cg);
}

void Painter::drawHLine(int x, int y, int len, Color color, int sw) {
    drawLine(x, y, x + len, y, color, sw);
}
void Painter::drawVLine(int x, int y, int len, Color color, int sw) {
    drawLine(x, y, x, y + len, color, sw);
}

// ============================================================================
// Clip
// ============================================================================

void Painter::pushClipRect(int x, int y, int w, int h)
{
    CGContextSaveGState(ctx.cgContext);
    CGContextClipToRect(ctx.cgContext, CGRectMake(x, y, w, h));
}

void Painter::popClipRect()
{
    CGContextRestoreGState(ctx.cgContext);
}

void Painter::pushClipRoundedRect(int x, int y, int w, int h, int cornerDiameter)
{
    CGContextSaveGState(ctx.cgContext);
    addRoundedRectPath(ctx.cgContext, x, y, w, h, cornerDiameter * 0.5);
    CGContextClip(ctx.cgContext);
}

// ============================================================================
// Gradient
// ============================================================================

void Painter::fillGradientRect(int x, int y, int w, int h,
                                const std::vector<Color>& colors)
{
    if (colors.empty() || w <= 0 || h <= 0) return;
    if (colors.size() == 1) { fillRect(x, y, w, h, colors[0]); return; }

    CGContextRef cg = ctx.cgContext;
    CGContextSaveGState(cg);

    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    std::vector<CGFloat> comps;
    std::vector<CGFloat> locs;
    comps.reserve(colors.size() * 4);
    locs.reserve(colors.size());

    for (int i = 0; i < (int)colors.size(); ++i) {
        const Color& c = colors[i];
        comps.push_back(c.r / 255.0);
        comps.push_back(c.g / 255.0);
        comps.push_back(c.b / 255.0);
        comps.push_back(c.a / 255.0);
        locs.push_back((double)i / (colors.size() - 1));
    }

    CGGradientRef grad = CGGradientCreateWithColorComponents(
        cs, comps.data(), locs.data(), colors.size());

    CGContextClipToRect(cg, CGRectMake(x, y, w, h));
    CGContextDrawLinearGradient(cg, grad,
        CGPointMake(x, y), CGPointMake(x + w, y), 0);

    CGGradientRelease(grad);
    CGColorSpaceRelease(cs);
    CGContextRestoreGState(cg);
}

// ============================================================================
// Text — CoreText
// ============================================================================

void Painter::drawText(const std::wstring& text, int x, int y, int w, int h,
                        NativeFont font, Color color, UINT format)
{
    if (text.empty()) return;
    // wstring → UTF-8 → drawTextA
    std::string utf8;
    for (wchar_t wc : text) {
        uint32_t cp = wc;
        if (cp < 0x80)       utf8 += (char)cp;
        else if (cp < 0x800) { utf8 += (char)(0xC0|(cp>>6)); utf8 += (char)(0x80|(cp&0x3F)); }
        else                 { utf8 += (char)(0xE0|(cp>>12)); utf8 += (char)(0x80|((cp>>6)&0x3F)); utf8 += (char)(0x80|(cp&0x3F)); }
    }
    drawTextA(utf8, x, y, w, h, font, color, format);
}

void Painter::drawTextA(const std::string& text, int x, int y, int w, int h,
                         NativeFont font, Color color, UINT format)
{
    if (text.empty()) return;

    CGContextRef cg = ctx.cgContext;
    CGContextSaveGState(cg);

    // CoreText draws with bottom-left origin; flip for top-left coords
    CGContextTranslateCTM(cg, 0, y + h);
    CGContextScaleCTM(cg, 1.0, -1.0);

    CTFontRef ctFont = reinterpret_cast<CTFontRef>(font);

    CGFloat r = color.r / 255.0, g = color.g / 255.0,
            b = color.b / 255.0, a = color.a / 255.0;
    CGColorRef cgColor = CGColorCreateGenericRGB(r, g, b, a);

    CFStringRef str = CFStringCreateWithCString(
        nullptr, text.c_str(), kCFStringEncodingUTF8);

    CFStringRef keys[] = { kCTFontAttributeName, kCTForegroundColorAttributeName };
    CFTypeRef   vals[] = { ctFont, cgColor };
    CFDictionaryRef attrs = CFDictionaryCreate(nullptr,
        (const void**)keys, (const void**)vals, 2,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);

    CFAttributedStringRef attrStr =
        CFAttributedStringCreate(nullptr, str, attrs);
    CTLineRef line = CTLineCreateWithAttributedString(attrStr);

    // Horizontal alignment
    CGFloat drawX = x;
    if (format & DT_CENTER) {
        CGFloat lineW = CTLineGetTypographicBounds(line, nullptr, nullptr, nullptr);
        drawX = x + (w - lineW) * 0.5;
    } else if (format & DT_RIGHT) {
        CGFloat lineW = CTLineGetTypographicBounds(line, nullptr, nullptr, nullptr);
        drawX = x + w - lineW;
    }

    // Vertical: baseline is approximate (ascent ~75% of font size)
    CGFloat ascent = 0;
    CTLineGetTypographicBounds(line, &ascent, nullptr, nullptr);
    CGFloat drawY = (format & DT_VCENTER)
        ? (h - ascent) * 0.5
        : h - ascent;

    CGContextSetTextPosition(cg, drawX, drawY);
    CTLineDraw(line, cg);

    CFRelease(line); CFRelease(attrStr);
    CFRelease(attrs); CFRelease(str); CGColorRelease(cgColor);
    CGContextRestoreGState(cg);
}

void Painter::measureText(const std::wstring& text, NativeFont font,
                           int& outWidth, int& outHeight)
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
    CFTypeRef   vals[] = { ctFont };
    CFDictionaryRef attrs = CFDictionaryCreate(nullptr,
        (const void**)keys, (const void**)vals, 1,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFAttributedStringRef attrStr = CFAttributedStringCreate(nullptr, str, attrs);
    CTLineRef line = CTLineCreateWithAttributedString(attrStr);

    CGFloat ascent, descent, leading;
    outWidth  = (int)CTLineGetTypographicBounds(line, &ascent, &descent, &leading);
    outHeight = (int)(ascent + descent + leading);

    CFRelease(line); CFRelease(attrStr); CFRelease(attrs); CFRelease(str);
}

// ============================================================================
// Remaining methods — delegate to existing helpers
// ============================================================================

void Painter::fillRectAlpha(int x, int y, int w, int h, Color color) {
    fillRect(x, y, w, h, color); // CGContext handles alpha via color.a
}

void Painter::fillRoundedRegion(int x, int y, int w, int h,
                                 int cornerRadius, Color color) {
    fillRoundedRect(x, y, w, h, cornerRadius, color);
}

void Painter::fillRectWithLeftAccent(int x, int y, int w, int h,
                                      Color bg, Color accent, int stripWidth) {
    fillRect(x, y, w, h, bg);
    fillRect(x, y, stripWidth, h, accent);
}

void Painter::drawRectOutline(int x, int y, int w, int h,
                               Color color, int strokeWidth) {
    drawBorder(x, y, w, h, 0, color, strokeWidth);
}

void Painter::drawRoundedRectOutline(int x, int y, int w, int h,
                                      int cornerDiameter,
                                      Color stroke, int strokeWidth) {
    drawBorder(x, y, w, h, cornerDiameter * 0.5, stroke, strokeWidth);
}

void Painter::fillColumnBars(int x, int y, int w, int h,
                              const std::vector<int>& barHeights, Color color)
{
    int cols = std::min(w, (int)barHeights.size());
    for (int i = 0; i < cols; ++i) {
        int bh = std::max(0, std::min(h, barHeights[i]));
        fillRect(x + i, y + h - bh, 1, bh, color);
    }
}

void Painter::drawPolyline(const std::vector<std::pair<int,int>>& points,
                            Color color, int strokeWidth)
{
    if (points.size() < 2) return;
    CGContextRef cg = ctx.cgContext;
    CGContextSaveGState(cg);
    CGContextSetLineWidth(cg, strokeWidth);
    setStrokeColor(cg, color);
    CGContextMoveToPoint(cg, points[0].first, points[0].second);
    for (size_t i = 1; i < points.size(); ++i)
        CGContextAddLineToPoint(cg, points[i].first, points[i].second);
    CGContextStrokePath(cg);
    CGContextRestoreGState(cg);
}

void Painter::fillPolygonAlpha(const std::vector<std::pair<int,int>>& points,
                                Color color)
{
    if (points.size() < 3) return;
    CGContextRef cg = ctx.cgContext;
    CGContextSaveGState(cg);
    setFillColor(cg, color);
    CGContextMoveToPoint(cg, points[0].first, points[0].second);
    for (size_t i = 1; i < points.size(); ++i)
        CGContextAddLineToPoint(cg, points[i].first, points[i].second);
    CGContextClosePath(cg);
    CGContextFillPath(cg);
    CGContextRestoreGState(cg);
}

// Rich text — wavyline, fade, decorations, measureRichText,
// drawRichText, drawRichTextA follow the same structure as
// flux_painter_linux.cpp but use CoreText for measurement.
// Implement them the same way as Linux replacing Pango calls
// with CTLine/CTFont equivalents.
void Painter::drawRichText(const std::wstring&,
                           const RichTextParams&,
                           FontCache&)
{
}

void Painter::drawRichTextA(const std::string&,
                            const RichTextParams&,
                            FontCache&)
{
}

void Painter::measureRichText(const std::wstring&,
                              const TextStyle&,
                              FontCache&,
                              int,
                              bool,
                              int,
                              int& outWidth,
                              int& outHeight)
{
    outWidth = 0;
    outHeight = 0;
}
void Painter::drawWavyLine(int x, int y, int len, Color color, int amplitude) {
    if (len <= 0) return;
    const int step = amplitude * 2;
    std::vector<std::pair<int,int>> pts;
    int px = x; bool up = true;
    while (px < x + len) {
        pts.push_back({px, up ? y - amplitude : y + amplitude});
        px += step; up = !up;
    }
    pts.push_back({x + len, y});
    if (pts.size() >= 2) drawPolyline(pts, color, 1);
}

void Painter::drawFadeOverlay(int x, int y, int w, int h,
                               int fadeWidth, Color bg) {
    if (fadeWidth <= 0 || w <= 0 || h <= 0) return;
    int startX = x + w - fadeWidth;
    if (startX < x) startX = x;
    std::vector<Color> stops = { bg.withAlpha(0), bg.withAlpha(255) };
    fillGradientRect(startX, y, fadeWidth, h, stops);
}

void Painter::drawArc(float cx, float cy, float radius,
                      int strokeWidth, float startAngle, float sweepAngle,
                      Color color, bool roundedCaps)
{
    CGContextRef cg = ctx.cgContext;
    CGContextSaveGState(cg);
    CGContextSetLineWidth(cg, strokeWidth);
    CGContextSetLineCap(cg, roundedCaps ? kCGLineCapRound : kCGLineCapButt);
    setStrokeColor(cg, color);
    // CG angles: 0 = right, counter-clockwise positive
    // Our convention: 0 = right, clockwise positive → negate for CG
    CGContextAddArc(cg, cx, cy, radius,
                    -startAngle, -(startAngle + sweepAngle), 1);
    CGContextStrokePath(cg);
    CGContextRestoreGState(cg);
}


// ============================================================================
// Painter::drawImage
// ============================================================================

void Painter::drawImage(const ImageDrawParams &params)
{
    if (!params.image || params.clipW <= 0 || params.clipH <= 0)
        return;

    CGContextRef cgCtx = ctx.cgContext;
    if (!cgCtx)
        return;

    CGContextSaveGState(cgCtx);

    // ── Clip ─────────────────────────────────────────────────────────────────
    if (params.borderRadius > 0)
        cgClipRoundedRect(cgCtx, params.clipX, params.clipY, params.clipW, params.clipH,
                          (CGFloat)params.borderRadius);
    else
        CGContextClipToRect(cgCtx, CGRectMake(params.clipX, params.clipY,
                                              params.clipW, params.clipH));

    // ── Draw ─────────────────────────────────────────────────────────────────
    // CoreGraphics origin is bottom-left; FluxUI is top-left. Flip the CTM
    // around the bottom of the clip rect so the rest of the math can work in
    // top-left-relative coordinates.
    CGContextTranslateCTM(cgCtx, 0, (CGFloat)(params.clipY + params.clipH));
    CGContextScaleCTM(cgCtx, 1.0, -1.0);

    bool needsScale = (params.srcWidth != (int)params.destW ||
                       params.srcHeight != (int)params.destH);
    CGContextSetInterpolationQuality(cgCtx,
        needsScale ? cgInterpolationFromQuality(params.filterQuality) : kCGInterpolationNone);

    float localX = params.destX - (float)params.clipX;
    float imgY = (float)params.clipH - ((params.destY - (float)params.clipY) + params.destH);

    if (params.repeat != ImageRepeat::NoRepeat)
    {
        float tileW = params.destW, tileH = params.destH;
        float startX = (params.repeat == ImageRepeat::RepeatY) ? localX : 0.f;
        float startY = (params.repeat == ImageRepeat::RepeatX)
                            ? (float)params.clipH - ((params.destY - (float)params.clipY) + tileH)
                            : 0.f;
        float endX = (params.repeat == ImageRepeat::RepeatY) ? localX + tileW : (float)params.clipW;
        float endY = (params.repeat == ImageRepeat::RepeatX)
                            ? (float)params.clipH - (params.destY - (float)params.clipY)
                            : (float)params.clipH;

        for (float ty = startY; ty < endY; ty += tileH)
            for (float tx = startX; tx < endX; tx += tileW)
                CGContextDrawImage(cgCtx, CGRectMake(tx, ty, tileW, tileH), params.image);
    }
    else
    {
        CGContextDrawImage(cgCtx, CGRectMake(localX, imgY, params.destW, params.destH), params.image);
    }

    CGContextRestoreGState(cgCtx);
}

void Painter::drawVideo(const VideoDrawParams& params)
{
    if (!params.frame || params.dstW <= 0 || params.dstH <= 0) return;
    CGContextRef cg = ctx.cgContext;
    CGContextSaveGState(cg);
    CGContextTranslateCTM(cg, params.dstX, params.dstY + params.dstH);
    CGContextScaleCTM(cg, 1.0, -1.0);
    CGContextSetInterpolationQuality(cg, kCGInterpolationHigh);
    CGContextDrawImage(cg,
        CGRectMake(0, 0, params.dstW, params.dstH),
        (CGImageRef)params.frame);
    CGContextRestoreGState(cg);
}
#endif // __APPLE__