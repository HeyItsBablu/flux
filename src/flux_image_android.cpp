// flux_image_android.cpp
#ifdef __ANDROID__

#include "flux/widgets/flux_image.hpp"
#include "nanovg.h"

extern NVGcontext *FluxAndroid_getVG();


ImageWidget::~ImageWidget() { _platformDestroy(); }

// ============================================================================
// _platformDecode  — not used on Android (stb path handles decode)
// ============================================================================

bool ImageWidget::_platformDecode(const uint8_t * /*data*/, int /*len*/) {
    return false;
}

// ============================================================================
// _platformStorePixels  — Android keeps raw RGBA (NanoVG takes RGBA directly)
// ============================================================================

bool ImageWidget::_platformStorePixels(unsigned char *rgba, int w, int h) {
    pending.rgba.assign(rgba, rgba + (size_t)w * h * 4);
    pending.w = w;
    pending.h = h;
    return true;
}

// ============================================================================
// _platformPromote  — must run on GL thread; called from render()
// NanoVG texture upload happens here.
// ============================================================================

void ImageWidget::_platformPromote() {
    if (!pending.ready()) return;

    NVGcontext *vg = FluxAndroid_getVG();
    if (!vg) return;

    // Free old texture
    if (nvgImage != -1) {
        nvgDeleteImage(vg, nvgImage);
        nvgImage = -1;
    }

    uploadBuffer = std::move(pending.rgba);
    uploadW      = pending.w;
    uploadH      = pending.h;
    pending.rgba.clear();
    pending.w = pending.h = 0;

    nvgImage = nvgCreateImageRGBA(vg, uploadW, uploadH,
                                  NVG_IMAGE_PREMULTIPLIED,
                                  uploadBuffer.data());
    if (nvgImage == -1) {
        LOGW_IMG("nvgCreateImageRGBA FAILED");
        _setLoadState(ImageLoadState::Error);
        return;
    }

    nvgImageSize(vg, nvgImage, &imageWidth, &imageHeight);
    if (imageWidth == 0 || imageHeight == 0) {
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

void ImageWidget::_platformRender(GraphicsContext & /*ctx*/,
                                  int cx, int cy, int cw, int ch) {
    NVGcontext *vg = FluxAndroid_getVG();
    if (!vg || nvgImage == -1) return;

    DestRect d = _calculateDestRect(cx, cy, cw, ch);

    nvgSave(vg);
    nvgScissor(vg, (float)cx, (float)cy, (float)cw, (float)ch);

    if (repeat != ImageRepeat::NoRepeat) {
        float tileW  = d.w, tileH  = d.h;
        float startX = (repeat == ImageRepeat::RepeatY) ? d.x         : (float)cx;
        float startY = (repeat == ImageRepeat::RepeatX) ? d.y         : (float)cy;
        float endX   = (repeat == ImageRepeat::RepeatY) ? d.x + tileW : (float)(cx + cw);
        float endY   = (repeat == ImageRepeat::RepeatX) ? d.y + tileH : (float)(cy + ch);

        for (float ty = startY; ty < endY; ty += tileH) {
            for (float tx = startX; tx < endX; tx += tileW) {
                NVGpaint p = nvgImagePattern(vg, tx, ty, tileW, tileH,
                                             0.0f, nvgImage, 1.0f);
                nvgBeginPath(vg);
                nvgRect(vg, tx, ty, tileW, tileH);
                nvgFillPaint(vg, p);
                nvgFill(vg);
            }
        }
    } else {
        NVGpaint paint = nvgImagePattern(vg, d.x, d.y, d.w, d.h,
                                         0.0f, nvgImage, 1.0f);
        nvgBeginPath(vg);
        if (borderRadius > 0)
            nvgRoundedRect(vg, (float)cx, (float)cy, (float)cw, (float)ch,
                           (float)borderRadius);
        else
            nvgRect(vg, (float)cx, (float)cy, (float)cw, (float)ch);
        nvgFillPaint(vg, paint);
        nvgFill(vg);
    }

    nvgRestore(vg);
}

// ============================================================================
// _platformInvalidateCache  — no scaled cache on Android
// ============================================================================

void ImageWidget::_platformInvalidateCache() {
    // NanoVG has no separate scaled cache; nothing to do.
}

// ============================================================================
// _platformDestroy
// ============================================================================

void ImageWidget::_platformDestroy() {
    NVGcontext *vg = FluxAndroid_getVG();
    if (vg && nvgImage != -1) {
        nvgDeleteImage(vg, nvgImage);
        nvgImage = -1;
    }
    pending.rgba.clear();
    pending.w = pending.h = 0;
    uploadBuffer.clear();
    uploadW = uploadH = 0;
}

#endif // __ANDROID__