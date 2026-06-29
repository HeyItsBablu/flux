// flux_image_android.cpp
// Android image platform implementation — direct OpenGL ES texture upload.

#ifdef __ANDROID__

#include "flux/widgets/flux_image.hpp"
#include <GLES2/gl2.h>
#include <android/log.h>

#define LOGE_IMG(...) __android_log_print(ANDROID_LOG_ERROR, "FluxImage", __VA_ARGS__)
#define LOGW_IMG2(...) __android_log_print(ANDROID_LOG_WARN,  "FluxImage", __VA_ARGS__)
#define LOGI_IMG2(...) __android_log_print(ANDROID_LOG_INFO,  "FluxImage", __VA_ARGS__)

ImageWidget::~ImageWidget() { _platformDestroy(); }

// ============================================================================
// _platformDecode  — not used on Android (stb_image path handles decode)
// ============================================================================

bool ImageWidget::_platformDecode(const uint8_t* /*data*/, int /*len*/)
{
    return false;
}

// ============================================================================
// _platformStorePixels
// Called on the decode thread with raw RGBA pixels from stb_image.
// Stores them in pending for later upload on the GL thread.
// ============================================================================

bool ImageWidget::_platformStorePixels(unsigned char* rgba, int w, int h)
{
    pending.rgba.assign(rgba, rgba + (size_t)w * h * 4);
    pending.w = w;
    pending.h = h;
    return true;
}

// ============================================================================
// _platformPromote
// Must run on the GL thread (called from computeLayout and render).
// Uploads pending RGBA pixels to a GL_TEXTURE_2D and sets glTexture.
// ============================================================================

void ImageWidget::_platformPromote()
{
    if (!pending.ready()) return;

    // ── Move pixel data to upload buffer ─────────────────────────────────────
    uploadBuffer = std::move(pending.rgba);
    uploadW      = pending.w;
    uploadH      = pending.h;
    pending.rgba.clear();
    pending.w = pending.h = 0;

    // ── Delete old texture if we're replacing it ──────────────────────────────
    if (glTexture != 0) {
        glDeleteTextures(1, &glTexture);
        glTexture = 0;
    }

    // ── Upload to GPU ─────────────────────────────────────────────────────────
    glGenTextures(1, &glTexture);
    if (glTexture == 0) {
        LOGE_IMG("glGenTextures failed (GL error 0x%x)", glGetError());
        _setLoadState(ImageLoadState::Error);
        return;
    }

    glBindTexture(GL_TEXTURE_2D, glTexture);

    // Default filter: linear (matches FilterQuality::Low; trilinear not
    // available in GLES2 without mip generation, but bilinear is fine).
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glTexImage2D(GL_TEXTURE_2D, 0,
                 GL_RGBA,
                 uploadW, uploadH, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE,
                 uploadBuffer.data());

    GLenum err = glGetError();
    glBindTexture(GL_TEXTURE_2D, 0);

    if (err != GL_NO_ERROR) {
        LOGE_IMG("glTexImage2D failed: GL error 0x%x (%dx%d)",
                 err, uploadW, uploadH);
        glDeleteTextures(1, &glTexture);
        glTexture = 0;
        _setLoadState(ImageLoadState::Error);
        return;
    }

    imageWidth  = uploadW;
    imageHeight = uploadH;

    LOGI_IMG2("Uploaded GL texture %u (%dx%d)", glTexture, imageWidth, imageHeight);
    _setLoadState(ImageLoadState::Loaded);
    markNeedsLayout();
}

// ============================================================================
// _platformRender
// Passes the GL texture handle as NativeImage to Painter::drawImage.
// NativeImage on Android is GLuint (see flux_platform.hpp).
// ============================================================================

void ImageWidget::_platformRender(GraphicsContext& ctx,
                                   int cx, int cy, int cw, int ch)
{
    if (glTexture == 0) return;

    DestRect d = _calculateDestRect(cx, cy, cw, ch);

    Painter painter(ctx);
    Painter::ImageDrawParams params;
    params.image       = glTexture;   // GLuint cast to NativeImage (same type)
    params.srcWidth    = imageWidth;
    params.srcHeight   = imageHeight;
    params.clipX       = cx;
    params.clipY       = cy;
    params.clipW       = cw;
    params.clipH       = ch;
    params.destX       = d.x;
    params.destY       = d.y;
    params.destW       = d.w;
    params.destH       = d.h;
    params.borderRadius  = borderRadius;
    params.repeat        = repeat;
    params.filterQuality = filterQuality;

    painter.drawImage(params);
}

// ============================================================================
// _platformInvalidateCache
// No scaled cache on Android — nothing to invalidate.
// ============================================================================

void ImageWidget::_platformInvalidateCache()
{
    // GL textures are uploaded at their source resolution.
    // Scaling is done by the GPU at draw time via GL_LINEAR filtering.
    // No per-ImageWidget CPU cache exists.
}

// ============================================================================
// _platformDestroy
// Releases the GL texture. Safe to call from the destructor (GL thread only).
// ============================================================================

void ImageWidget::_platformDestroy()
{
    if (glTexture != 0) {
        glDeleteTextures(1, &glTexture);
        glTexture = 0;
        LOGI_IMG2("Deleted GL texture");
    }
    pending.rgba.clear();
    pending.w = pending.h = 0;
    uploadBuffer.clear();
    uploadW = uploadH = 0;
}

#endif // __ANDROID__