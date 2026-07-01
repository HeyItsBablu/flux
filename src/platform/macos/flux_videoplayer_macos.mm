// flux_videoplayer_macos.mm
//
// Apple/Metal half of VideoPlayerWidget's frame texture management. Kept
// out of flux_videoplayer.hpp because that header is included from plain
// C++ translation units (flux_core.cpp, and transitively any .cpp that
// pulls in widgets.hpp) which cannot parse id<...>/__bridge syntax — same
// boundary as flux_overlay_manager_macos.mm.
#ifdef __APPLE__
#include <TargetConditionals.h>
#if TARGET_OS_OSX

#import <Metal/Metal.h>
#include <cstdint>
#include <vector>

// Converts RGB24 -> BGRA8 (Metal has no 3-byte format) and uploads to a new
// texture. Returns a CFBridgingRetain'd id<MTLTexture> as an opaque void* —
// same representation as NativeImage on this platform, so callers can pass
// the result straight into Painter::VideoDrawParams::frame with no further
// bridging. Caller owns the returned pointer and must eventually pass it to
// flux_videoPlayerMac_releaseTexture.
void *flux_videoPlayerMac_rebuildTexture(void *mtlDeviceOpaque,
                                          const uint8_t *rgbData,
                                          int width, int height)
{
    id<MTLDevice> device = (__bridge id<MTLDevice>)mtlDeviceOpaque;
    if (!device || !rgbData || width <= 0 || height <= 0)
        return nullptr;

    size_t n = (size_t)width * (size_t)height;
    std::vector<uint8_t> bgra(n * 4);
    const uint8_t *src = rgbData;
    uint8_t *dst = bgra.data();
    for (size_t i = 0; i < n; ++i)
    {
        dst[0] = src[2]; dst[1] = src[1]; dst[2] = src[0]; dst[3] = 0xFF;
        src += 3; dst += 4;
    }

    MTLTextureDescriptor *td = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                      width:width height:height mipmapped:NO];
    td.usage = MTLTextureUsageShaderRead;
    id<MTLTexture> tex = [device newTextureWithDescriptor:td];
    [tex replaceRegion:MTLRegionMake2D(0, 0, width, height) mipmapLevel:0
              withBytes:bgra.data() bytesPerRow:(NSUInteger)width * 4];

    return (__bridge_retained void *)tex;
}

// Releases a texture previously returned by flux_videoPlayerMac_rebuildTexture.
// Safe to call with nullptr.
void flux_videoPlayerMac_releaseTexture(void *texOpaque)
{
    if (!texOpaque)
        return;
    id<MTLTexture> tex = (__bridge_transfer id<MTLTexture>)texOpaque;
    (void)tex; // ARC releases when this local goes out of scope
}

#endif // TARGET_OS_OSX
#endif // __APPLE__