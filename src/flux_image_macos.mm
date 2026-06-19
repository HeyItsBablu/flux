// flux_image_macos.mm
#if defined(__APPLE__) && !defined(__ANDROID__)
#include <TargetConditionals.h>
#if TARGET_OS_OSX

#include "flux/widgets/flux_image.hpp"

#import <CoreGraphics/CoreGraphics.h>
#import <CoreFoundation/CoreFoundation.h>

#include <algorithm>
#include <cmath>

// ============================================================================
// MacScaleCache definition
// ============================================================================
 
struct ImageWidget::MacScaleCache {
    CGImageRef image   = nullptr;
    int        w       = 0;
    int        h       = 0;
    int        fit     = -1;
    int        quality = -1;

    void invalidate() {
        if (image) { CGImageRelease(image); image = nullptr; }
        w = h = 0;
        fit = quality = -1;
    }

    ~MacScaleCache() { invalidate(); }
};

void ImageWidget::MacScaleCacheDeleter::operator()(MacScaleCache* p) const { delete p; }

ImageWidget::~ImageWidget() { _platformDestroy(); }

// ============================================================================
// Helpers (file-scope)
// ============================================================================

// stb RGBA → CoreGraphics premultiplied ARGB (BGRA on little-endian)
// CG wants kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Host
// which on ARM/x86 is BGRA in memory.
static void convertRGBAToCGPremul(unsigned char *src, int w, int h,
                                   std::vector<uint8_t> &out) {
    const size_t nPx = (size_t)w * (size_t)h;
    out.resize(nPx * 4);
    for (size_t i = 0; i < nPx; ++i) {
        uint8_t r = src[i*4+0], g = src[i*4+1],
                b = src[i*4+2], a = src[i*4+3];
        out[i*4+0] = (uint8_t)((uint32_t)b * a / 255u); // B premul
        out[i*4+1] = (uint8_t)((uint32_t)g * a / 255u); // G premul
        out[i*4+2] = (uint8_t)((uint32_t)r * a / 255u); // R premul
        out[i*4+3] = a;
    }
}

// Build a CGImageRef from a BGRA-premul pixel buffer.
// The returned CGImageRef retains a copy of the data via a CFData provider.
static CGImageRef makeCGImage(const uint8_t *pixels, int w, int h) {
    size_t bytesPerRow = (size_t)w * 4;
    size_t dataLen     = bytesPerRow * (size_t)h;

    // Copy pixels into a CFData so the CGImage owns its backing store.
    CFDataRef data = CFDataCreate(kCFAllocatorDefault, pixels, (CFIndex)dataLen);
    if (!data) return nullptr;

    CGDataProviderRef provider = CGDataProviderCreateWithCFData(data);
    CFRelease(data);
    if (!provider) return nullptr;

    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    CGImageRef img = CGImageCreate(
        (size_t)w, (size_t)h,
        8,                    // bits per component
        32,                   // bits per pixel
        bytesPerRow,
        cs,
        kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Host,
        provider,
        nullptr,              // decode array
        false,                // shouldInterpolate (we control this at draw time)
        kCGRenderingIntentDefault);

    CGColorSpaceRelease(cs);
    CGDataProviderRelease(provider);
    return img;
}

// Build a pre-scaled CGImageRef via an offscreen CGBitmapContext.
static CGImageRef makeScaledCGImage(CGImageRef src,
                                     int srcW, int srcH,
                                     int dstW, int dstH,
                                     CGInterpolationQuality iq) {
    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    CGContextRef ctx = CGBitmapContextCreate(
        nullptr,          // let CG allocate
        (size_t)dstW, (size_t)dstH,
        8,
        (size_t)dstW * 4,
        cs,
        kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Host);
    CGColorSpaceRelease(cs);
    if (!ctx) return nullptr;

    CGContextSetInterpolationQuality(ctx, iq);
    CGContextDrawImage(ctx, CGRectMake(0, 0, dstW, dstH), src);
    CGImageRef scaled = CGBitmapContextCreateImage(ctx);
    CGContextRelease(ctx);
    return scaled;  // caller owns
}

static CGInterpolationQuality cgInterpolation(FilterQuality q) {
    switch (q) {
    case FilterQuality::None:   return kCGInterpolationNone;
    case FilterQuality::Low:    return kCGInterpolationLow;
    case FilterQuality::Medium: return kCGInterpolationMedium;
    case FilterQuality::High:   return kCGInterpolationHigh;
    }
    return kCGInterpolationLow;
}

// Add a rounded-rect clip path using CGPathCreateWithRoundedRect (10.9+).
static void cgClipRoundedRect(CGContextRef ctx,
                               CGFloat rx, CGFloat ry,
                               CGFloat rw, CGFloat rh,
                               CGFloat radius) {
    CGFloat r = std::min(radius, std::min(rw * 0.5f, rh * 0.5f));
    CGPathRef path = CGPathCreateWithRoundedRect(
        CGRectMake(rx, ry, rw, rh), r, r, nullptr);
    CGContextAddPath(ctx, path);
    CGContextClip(ctx);
    CGPathRelease(path);
}

// ============================================================================
// _platformDecode  — not used on macOS (stb path handles decode)
// ============================================================================

bool ImageWidget::_platformDecode(const uint8_t * /*data*/, int /*len*/) {
    return false;
}

// ============================================================================
// _platformStorePixels  — convert stb RGBA → CG premul BGRA, write to _pending
// ============================================================================

bool ImageWidget::_platformStorePixels(unsigned char *rgba, int w, int h) {
    DecodedImage img;
    img.width  = w;
    img.height = h;
    convertRGBAToCGPremul(rgba, w, h, img.pixels);

    std::lock_guard<std::mutex> lock(_decodeMutex);
    _pending = std::move(img);
    return true;
}

// ============================================================================
// _platformPromote  — UI thread: move _pending → pixels + rebuild base CGImage
// ============================================================================

void ImageWidget::_platformPromote() {
    DecodedImage staged;
    {
        std::lock_guard<std::mutex> lock(_decodeMutex);
        if (!_pending.ready()) return;
        staged = std::move(_pending);
        _pending.clear();
    }

    pixels      = std::move(staged.pixels);
    imageWidth  = staged.width;
    imageHeight = staged.height;
    _platformInvalidateCache();
    _setLoadState(ImageLoadState::Loaded);
    markNeedsLayout();
}

// ============================================================================
// _platformRender
// ============================================================================

void ImageWidget::_platformRender(GraphicsContext &ctx, int cx, int cy,
                                  int cw, int ch) {
    if (pixels.empty() || !ctx.cgContext) return;
    if (!_macCache) _macCache.reset(new MacScaleCache());
    auto &cache = *_macCache;

    DestRect d  = _calculateDestRect(cx, cy, cw, ch);
    int dw = std::max(1, (int)d.w);
    int dh = std::max(1, (int)d.h);

    // ── Rebuild scaled CGImageRef if stale ───────────────────────────────────
    bool stale = !cache.image
              || cache.w       != dw
              || cache.h       != dh
              || cache.fit     != (int)fit
              || cache.quality != (int)filterQuality;

    if (stale) {
        cache.invalidate();

        CGImageRef base = makeCGImage(pixels.data(), imageWidth, imageHeight);
        if (!base) return;

        if (dw == imageWidth && dh == imageHeight) {
            cache.image = base;
        } else {
            cache.image = makeScaledCGImage(base, imageWidth, imageHeight,
                                            dw, dh,
                                            cgInterpolation(filterQuality));
            CGImageRelease(base);
            if (!cache.image) return;
        }

        cache.w       = dw;
        cache.h       = dh;
        cache.fit     = (int)fit;
        cache.quality = (int)filterQuality;
    }

    Painter painter(ctx);
    Painter::ImageDrawParams params;
    params.image    = cache.image;
    params.srcWidth  = dw;
    params.srcHeight = dh;
    params.clipX = cx;
    params.clipY = cy;
    params.clipW = cw;
    params.clipH = ch;
    params.destX = d.x;
    params.destY = d.y;
    params.destW = d.w;
    params.destH = d.h;
    params.borderRadius = borderRadius;
    params.repeat = repeat;
    params.filterQuality = filterQuality;

    painter.drawImage(params);
}

// ============================================================================
// _platformInvalidateCache
// ============================================================================

void ImageWidget::_platformInvalidateCache() {
    if (_macCache) _macCache->invalidate();
}

// ============================================================================
// _platformDestroy
// ============================================================================

void ImageWidget::_platformDestroy() {
    _macCache.reset();
}

#endif // TARGET_OS_OSX
#endif // __APPLE__