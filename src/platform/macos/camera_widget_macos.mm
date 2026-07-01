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
#import <Metal/Metal.h>
#import <CoreGraphics/CoreGraphics.h>
#import <ImageIO/ImageIO.h>
#import <Foundation/Foundation.h>




// ── Convenience casts ─────────────────────────────────────────────────────────
static inline id<MTLTexture> asMTLTexture(void* p) {
    return (__bridge id<MTLTexture>)p;
}

// ── MacOSState deleter — release retained textures ────────────────────────────
void CameraWidget::MacOSStateDeleter::operator()(MacOSState* p) const {
    if (p) {
        if (p->previewTexture) { id<MTLTexture> t = (__bridge_transfer id<MTLTexture>)p->previewTexture; (void)t; }
        if (p->thumbTexture)   { id<MTLTexture> t = (__bridge_transfer id<MTLTexture>)p->thumbTexture;   (void)t; }
    }
    delete p;
}

// ── Build an id<MTLTexture> from a BGRA32 CPU buffer ──────────────────────────
static id<MTLTexture> makeMTLTextureFromBGRA(id<MTLDevice> device,
                                              const uint8_t* bgra, int w, int h) {
    if (!device || !bgra || w <= 0 || h <= 0) return nil;
    MTLTextureDescriptor* td = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                      width:w height:h mipmapped:NO];
    td.usage = MTLTextureUsageShaderRead;
    id<MTLTexture> tex = [device newTextureWithDescriptor:td];
    [tex replaceRegion:MTLRegionMake2D(0, 0, w, h) mipmapLevel:0
            withBytes:bgra bytesPerRow:(NSUInteger)w * 4];
    return tex;
}

static CameraWidget::MacOSState& getMac(
        std::unique_ptr<CameraWidget::MacOSState,
                        CameraWidget::MacOSStateDeleter>& ptr) {
    if (!ptr) ptr.reset(new CameraWidget::MacOSState());
    return *ptr;
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
    if (_macos && _macos->previewTexture) {
        id<MTLTexture> t = (__bridge_transfer id<MTLTexture>)_macos->previewTexture;
        (void)t; // released
        _macos->previewTexture = nullptr;
        _macos->cachedSrcW = 0;
        _macos->cachedSrcH = 0;
    }
}

// =============================================================================
// _platformRenderPreview
// =============================================================================

bool CameraWidget::_platformRenderPreview(GraphicsContext& ctx, Painter& p,
                                          FontCache& /*fontCache*/, int viewH) {
    auto& cam = FluxCamera::get();
    auto& s   = getMac(_macos);

    if (!ctx.mtlDevice) return false;
    id<MTLDevice> device = (__bridge id<MTLDevice>)ctx.mtlDevice;

    if (cam.updateFrame() && cam.getPreviewWidth() > 0) {
        auto frame = cam.lockFrame();
        if (frame.data && frame.width > 0 && frame.height > 0) {
            id<MTLTexture> newTex = makeMTLTextureFromBGRA(device, frame.data,
                                                            frame.width, frame.height);
            if (s.previewTexture) {
                id<MTLTexture> old = (__bridge_transfer id<MTLTexture>)s.previewTexture;
                (void)old;
            }
            s.previewTexture = newTex ? (__bridge_retained void*)newTex : nullptr;
            s.cachedSrcW = frame.width;
            s.cachedSrcH = frame.height;
        }
    }

    if (!s.previewTexture || s.cachedSrcW <= 0) return false;

    CGRect dst = letterboxRect(s.cachedSrcW, s.cachedSrcH, x, y, width, viewH);

    if ((int)dst.origin.x > x)
        p.fillRect(x, y, (int)dst.origin.x - x, viewH, colPlaceholder);
    if ((int)(dst.origin.x + dst.size.width) < x + width)
        p.fillRect((int)(dst.origin.x + dst.size.width), y,
                   (x + width) - (int)(dst.origin.x + dst.size.width), viewH, colPlaceholder);
    if ((int)dst.origin.y > y)
        p.fillRect(x, y, width, (int)dst.origin.y - y, colPlaceholder);
    if ((int)(dst.origin.y + dst.size.height) < y + viewH)
        p.fillRect(x, (int)(dst.origin.y + dst.size.height), width,
                   (y + viewH) - (int)(dst.origin.y + dst.size.height), colPlaceholder);

    Painter::CameraDrawParams cp;
    cp.frame  = (__bridge NativeImage)asMTLTexture(s.previewTexture);
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
    if (!_macos || !_macos->thumbTexture) return false;

    Painter p(ctx);
    Painter::ImageDrawParams ip;
    ip.image  = (__bridge NativeImage)asMTLTexture(_macos->thumbTexture);
    ip.destX  = (float)thumbX;
    ip.destY  = (float)thumbY;
    ip.destW  = (float)thumbW;
    ip.destH  = (float)thumbH;
    ip.clipX = thumbX; ip.clipY = thumbY; ip.clipW = thumbW; ip.clipH = thumbH;
    p.drawImage(ip);
    return true;
}

// =============================================================================
// _platformLoadThumb
// =============================================================================
// Decode a JPEG (or any ImageIO-supported format) to a CGImageRef and cache it.

void CameraWidget::_platformLoadThumb(const std::string& path) {
    auto& s = getMac(_macos);

    if (s.thumbTexture) {
        id<MTLTexture> old = (__bridge_transfer id<MTLTexture>)s.thumbTexture;
        (void)old;
        s.thumbTexture = nullptr;
        s.thumbSrcW = s.thumbSrcH = 0;
    }

    NSString* nsPath = [NSString stringWithUTF8String:path.c_str()];
    NSURL*    url    = [NSURL fileURLWithPath:nsPath];
    if (!url) return;

    CGImageSourceRef source = CGImageSourceCreateWithURL((__bridge CFURLRef)url, nullptr);
    if (!source) return;
    CGImageRef img = CGImageSourceCreateImageAtIndex(source, 0, nullptr);
    CFRelease(source);
    if (!img) return;

    int w = (int)CGImageGetWidth(img);
    int h = (int)CGImageGetHeight(img);

    std::vector<uint8_t> bgra((size_t)w * h * 4);
    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    CGContextRef cg = CGBitmapContextCreate(
        bgra.data(), w, h, 8, w * 4, cs,
        kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little);
    CGColorSpaceRelease(cs);
    if (cg) {
        CGContextDrawImage(cg, CGRectMake(0, 0, w, h), img);
        CGContextRelease(cg);
    }
    CGImageRelease(img);
    if (!cg) return;

    auto* inst = FluxUI::getCurrentInstance();
    auto* pw = inst ? inst->getPlatformWindowPtr() : nullptr;
    id<MTLDevice> device = pw ? (__bridge id<MTLDevice>)pw->getMacMetalDevicePtr() : nil;
    if (!device) return;
    
    id<MTLTexture> tex = makeMTLTextureFromBGRA(device, bgra.data(), w, h);
    if (tex) {
        s.thumbTexture = (__bridge_retained void*)tex;
        s.thumbSrcW = w;
        s.thumbSrcH = h;
    }
}

// =============================================================================
// _platformDestroy
// =============================================================================

void CameraWidget::_platformDestroy() {
    _macos.reset();   // MacOSStateDeleter releases both CGImageRefs
}

#endif // __APPLE__ && !__IPHONE_OS_VERSION_MIN_REQUIRED