// camera_widget_macos.mm
// macOS platform implementation for CameraWidget.
//
// Preview:   lockFrame() BGRA32 → CGDataProvider → CGImageRef → CGContextDrawImage
// Thumbnail: JPEG file  → CGDataProvider → CGImageCreateWithJPEGDataProvider
// Flash:     Painter::fillRectAlpha (CGContext handles alpha natively)
//
// Compile flags: -x objective-c++ -fobjc-arc  (set in CMakeLists)
// Frameworks:    CoreGraphics  CoreFoundation  ImageIO  AppKit

#if defined(__APPLE__) && !defined(__IPHONE_OS_VERSION_MIN_REQUIRED)

#include "flux/widgets/camera_widget.hpp"

#import <CoreGraphics/CoreGraphics.h>
#import <ImageIO/ImageIO.h>
#import <Foundation/Foundation.h>

// ── Convenience casts ─────────────────────────────────────────────────────────
// MacOSState stores CGImageRef as void* to keep CoreGraphics out of the header.

static inline CGImageRef asCGImage(void* p) {
    return static_cast<CGImageRef>(p);
}

// ── MacOSState deleter — CGImageRelease + delete ──────────────────────────────

void CameraWidget::MacOSStateDeleter::operator()(MacOSState* p) const {
    if (p) {
        if (p->previewImage) CGImageRelease(asCGImage(p->previewImage));
        if (p->thumbImage)   CGImageRelease(asCGImage(p->thumbImage));
    }
    delete p;
}

// ── Lazy initialiser ──────────────────────────────────────────────────────────

static CameraWidget::MacOSState& getMac(
        std::unique_ptr<CameraWidget::MacOSState,
                        CameraWidget::MacOSStateDeleter>& ptr) {
    if (!ptr) ptr.reset(new CameraWidget::MacOSState());
    return *ptr;
}

// ── Internal: build a CGImageRef from a BGRA32 CPU buffer ────────────────────
// The image does NOT copy the buffer — the caller must keep the FrameLock alive
// for the duration of the draw call, or pass a copy.  Here we copy once into a
// CFData so the CGImage is self-contained and the lock can be released early.

static CGImageRef makeCGImageFromBGRA(const uint8_t* bgra, int w, int h) {
    if (!bgra || w <= 0 || h <= 0) return nullptr;

    size_t stride   = (size_t)(w * 4);
    size_t byteSize = stride * (size_t)h;

    // Copy into a CFData so the CGImage lifetime is independent of the lock.
    CFDataRef data = CFDataCreate(kCFAllocatorDefault, bgra, (CFIndex)byteSize);
    if (!data) return nullptr;

    CGDataProviderRef provider =
        CGDataProviderCreateWithCFData(data);
    CFRelease(data);   // provider holds its own ref

    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();

    // kCGBitmapByteOrder32Little | kCGImageAlphaPremultipliedFirst = BGRA
    CGImageRef img = CGImageCreate(
            (size_t)w, (size_t)h,
            8,                          // bits per component
            32,                         // bits per pixel
            stride,
            cs,
            (CGBitmapInfo)(kCGBitmapByteOrder32Little |
                           kCGImageAlphaPremultipliedFirst),
            provider,
            nullptr,                    // no decode array
            false,                      // shouldInterpolate
            kCGRenderingIntentDefault);

    CGColorSpaceRelease(cs);
    CGDataProviderRelease(provider);
    return img;
}

// ── Internal: letterbox rect helper ──────────────────────────────────────────

static CGRect letterboxRect(int srcW, int srcH,
                             int dstX, int dstY,
                             int dstW, int dstH) {
    float camAR = (float)srcW / (float)srcH;
    float widAR = (float)dstW / (float)dstH;
    float fw, fh, ox, oy;
    if (camAR > widAR) {
        fw = (float)dstW;
        fh = fw / camAR;
        ox = 0.f;
        oy = ((float)dstH - fh) * 0.5f;
    } else {
        fh = (float)dstH;
        fw = fh * camAR;
        oy = 0.f;
        ox = ((float)dstW - fw) * 0.5f;
    }
    return CGRectMake(dstX + ox, dstY + oy, fw, fh);
}

// =============================================================================
// _platformScheduleOpen
// =============================================================================
// AVFoundation requests camera permission lazily on first open(); no polling
// timer is needed.  Set the flag immediately — same as Win32/Linux.

void CameraWidget::_platformScheduleOpen() {
    _shouldOpen = true;
}

// =============================================================================
// _platformOnFlip
// =============================================================================
// Drop the cached preview CGImage so a fresh one is built for the new device.

void CameraWidget::_platformOnFlip() {
    if (_macos && _macos->previewImage) {
        CGImageRelease(asCGImage(_macos->previewImage));
        _macos->previewImage = nullptr;
        _macos->cachedSrcW   = 0;
        _macos->cachedSrcH   = 0;
    }
}

// =============================================================================
// _platformRenderPreview
// =============================================================================

bool CameraWidget::_platformRenderPreview(GraphicsContext& ctx, Painter& p,
                                          FontCache& /*fontCache*/, int viewH) {
    auto& cam = FluxCamera::get();
    auto& s   = getMac(_macos);

    // Pull latest frame
    if (cam.updateFrame() && cam.getPreviewWidth() > 0) {
        auto frame = cam.lockFrame();   // holds _frameMutex until destroyed
        if (frame.data && frame.width > 0 && frame.height > 0) {
            // Rebuild CGImage only when dimensions change (common case: same
            // size every frame, so we create a new one cheaply each frame via
            // the CFData path — no persistent surface like Cairo needs).
            if (s.previewImage) {
                CGImageRelease(asCGImage(s.previewImage));
                s.previewImage = nullptr;
            }
            s.previewImage = makeCGImageFromBGRA(
                    frame.data, frame.width, frame.height);
            s.cachedSrcW = frame.width;
            s.cachedSrcH = frame.height;
        }
    }   // FrameLock released here — safe to draw

    if (!s.previewImage || s.cachedSrcW <= 0 || !ctx.cgContext)
        return false;

    CGContextRef cg = ctx.cgContext;

    // Letterbox rect
    CGRect dst = letterboxRect(s.cachedSrcW, s.cachedSrcH,
                               x, y, width, viewH);

    // Fill any exposed letterbox bars with the placeholder colour
    if ((int)dst.origin.x > x)
        p.fillRect(x, y, (int)dst.origin.x - x, viewH, colPlaceholder);
    if ((int)(dst.origin.x + dst.size.width) < x + width)
        p.fillRect((int)(dst.origin.x + dst.size.width), y,
                   (x + width) - (int)(dst.origin.x + dst.size.width),
                   viewH, colPlaceholder);
    if ((int)dst.origin.y > y)
        p.fillRect(x, y, width, (int)dst.origin.y - y, colPlaceholder);
    if ((int)(dst.origin.y + dst.size.height) < y + viewH)
        p.fillRect(x, (int)(dst.origin.y + dst.size.height),
                   width,
                   (y + viewH) - (int)(dst.origin.y + dst.size.height),
                   colPlaceholder);

    Painter::CameraDrawParams cp;
    cp.frame  = (NativeImage)s.previewImage;
    cp.dstX   = (int)dst.origin.x;
    cp.dstY   = (int)dst.origin.y;
    cp.dstW   = (int)dst.size.width;
    cp.dstH   = (int)dst.size.height;
    cp.mirror = cam.isFrontCamera();
    p.drawCamera(cp);
    return true;
}

// =============================================================================
// _platformRenderFlash
// =============================================================================

void CameraWidget::_platformRenderFlash(GraphicsContext& /*ctx*/, Painter& p,
                                        int viewH) {
    // CGContext handles alpha via color.a — fillRectAlpha is sufficient.
    Color fc = Color::fromRGBA(255, 255, 255, (uint8_t)(_flashAlpha * 255));
    p.fillRectAlpha(x, y, width, viewH, fc);
}

// =============================================================================
// _platformRenderThumb
// =============================================================================

bool CameraWidget::_platformRenderThumb(GraphicsContext& ctx,
                                        int thumbX, int thumbY,
                                        int thumbW, int thumbH) {
    if (!_macos || !_macos->thumbImage || !ctx.cgContext) return false;

    CGContextRef cg = ctx.cgContext;

    CGContextSaveGState(cg);
    // Flip Y for top-left origin
    CGContextTranslateCTM(cg, 0, thumbY * 2 + thumbH);
    CGContextScaleCTM(cg, 1.0, -1.0);

    CGRect dst = CGRectMake(thumbX, thumbY, thumbW, thumbH);
    CGContextSetInterpolationQuality(cg, kCGInterpolationLow);
    CGContextDrawImage(cg, dst, asCGImage(_macos->thumbImage));
    CGContextRestoreGState(cg);
    return true;
}

// =============================================================================
// _platformLoadThumb
// =============================================================================
// Decode a JPEG (or any ImageIO-supported format) to a CGImageRef and cache it.

void CameraWidget::_platformLoadThumb(const std::string& path) {
    auto& s = getMac(_macos);

    if (s.thumbImage) {
        CGImageRelease(asCGImage(s.thumbImage));
        s.thumbImage = nullptr;
        s.thumbSrcW  = s.thumbSrcH = 0;
    }

    NSString* nsPath = [NSString stringWithUTF8String:path.c_str()];
    NSURL*    url    = [NSURL fileURLWithPath:nsPath];
    if (!url) return;

    CGImageSourceRef source =
        CGImageSourceCreateWithURL((__bridge CFURLRef)url, nullptr);
    if (!source) return;

    CGImageRef img = CGImageSourceCreateImageAtIndex(source, 0, nullptr);
    CFRelease(source);
    if (!img) return;

    s.thumbImage = img;
    s.thumbSrcW  = (int)CGImageGetWidth(img);
    s.thumbSrcH  = (int)CGImageGetHeight(img);
}

// =============================================================================
// _platformDestroy
// =============================================================================

void CameraWidget::_platformDestroy() {
    _macos.reset();   // MacOSStateDeleter releases both CGImageRefs
}

#endif // __APPLE__ && !__IPHONE_OS_VERSION_MIN_REQUIRED