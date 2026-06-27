// flux_image_android.cpp
#ifdef __ANDROID__

#include "flux/widgets/flux_image.hpp"
#include "nanovg.h"

extern NVGcontext *FluxAndroid_getVG();

ImageWidget::~ImageWidget() { _platformDestroy(); }

// ============================================================================
// _platformDecode  — not used on Android (stb path handles decode)
// ============================================================================

bool ImageWidget::_platformDecode(const uint8_t * /*data*/, int /*len*/)
{
    return false;
}

// ============================================================================
// _platformStorePixels  — Android keeps raw RGBA (NanoVG takes RGBA directly)
// ============================================================================

bool ImageWidget::_platformStorePixels(unsigned char *rgba, int w, int h)
{
    pending.rgba.assign(rgba, rgba + (size_t)w * h * 4);
    pending.w = w;
    pending.h = h;
    return true;
}

// ============================================================================
// _platformPromote  — must run on GL thread; called from render()
// NanoVG texture upload happens here.
// ============================================================================

void ImageWidget::_platformPromote()
{
    if (!pending.ready())
        return;

    NVGcontext *vg = FluxAndroid_getVG();
    if (!vg)
        return;

    // Free old texture
    if (nvgImage != -1)
    {
        nvgDeleteImage(vg, nvgImage);
        nvgImage = -1;
    }

    uploadBuffer = std::move(pending.rgba);
    uploadW = pending.w;
    uploadH = pending.h;
    pending.rgba.clear();
    pending.w = pending.h = 0;

    nvgImage = nvgCreateImageRGBA(vg, uploadW, uploadH,
                                  NVG_IMAGE_PREMULTIPLIED,
                                  uploadBuffer.data());
    if (nvgImage == -1)
    {
        LOGW_IMG("nvgCreateImageRGBA FAILED");
        _setLoadState(ImageLoadState::Error);
        return;
    }

    nvgImageSize(vg, nvgImage, &imageWidth, &imageHeight);
    if (imageWidth == 0 || imageHeight == 0)
    {
        nvgDeleteImage(vg, nvgImage);
        nvgImage = -1;
        _setLoadState(ImageLoadState::Error);
        return;
    }

    _setLoadState(ImageLoadState::Loaded);
    markNeedsLayout();
}

// ============================================================================
// _platformRender
// ============================================================================

void ImageWidget::_platformRender(GraphicsContext &ctx,
                                  int cx, int cy, int cw, int ch)
{
    NVGcontext *vg = FluxAndroid_getVG();
    if (!vg || nvgImage == -1)
        return;

    DestRect d = _calculateDestRect(cx, cy, cw, ch);

    Painter painter(ctx);
    Painter::ImageDrawParams params;
    params.image = nvgImage;
    params.srcWidth = imageWidth;
    params.srcHeight = imageHeight;
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
// _platformInvalidateCache  — no scaled cache on Android
// ============================================================================

void ImageWidget::_platformInvalidateCache()
{
    // NanoVG has no separate scaled cache; nothing to do.
}

// ============================================================================
// _platformDestroy
// ============================================================================

void ImageWidget::_platformDestroy()
{
    NVGcontext *vg = FluxAndroid_getVG();
    if (vg && nvgImage != -1)
    {
        nvgDeleteImage(vg, nvgImage);
        nvgImage = -1;
    }
    pending.rgba.clear();
    pending.w = pending.h = 0;
    uploadBuffer.clear();
    uploadW = uploadH = 0;
}

#endif // __ANDROID__