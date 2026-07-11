// src/flux_image_ssr.cpp


#ifdef FLUX_SSR

#include "flux/widgets/flux_image.hpp"
#include "flux/flux_image_registry.hpp"

// Self-contained, like flux_font_ssr.cpp is for stb_truetype — do NOT
// link against the shared `stb` target for this. stb_impl.cpp compiles
// BOTH STB_IMAGE_IMPLEMENTATION and STB_TRUETYPE_IMPLEMENTATION into one
// object file; pulling stb_impl.obj out of stb.lib to resolve
// stbi_info_from_memory would ALSO drag in a second, colliding
// definition of every stbtt_* symbol flux_font_ssr.cpp already
// self-contains — exactly the LNK2005 flood this caused. Matching the
// same STBI_ONLY_* restriction flux_image.hpp's non-Win32 branch uses
// keeps behavior identical to every other platform's stb_image config.
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_ONLY_BMP
#define STBI_ONLY_TGA
#define STBI_ONLY_GIF
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

struct ImageWidget::SsrNativeImage
{
    int width = 0;
    int height = 0;
    std::string url;
};

ImageWidget::~ImageWidget()
{
    _platformDestroy();
}

bool ImageWidget::_platformDecode(const uint8_t *data, int len)
{
    int w = 0, h = 0, comp = 0;
    if (!stbi_info_from_memory(data, len, &w, &h, &comp) || w <= 0 || h <= 0)
        return false;

    std::string url = fluxImageRegistryRegister(data, (size_t)len);
    if (url.empty())
        return false;

    auto *img = new SsrNativeImage();
    img->width = w;
    img->height = h;
    img->url = std::move(url);

    _ssrPending = img;
    imageWidth = w;
    imageHeight = h;
    return true;
}

bool ImageWidget::_platformDecodeNetwork(const uint8_t *data, int len,
                                         const std::string &sourceUrl)
{
    int w = 0, h = 0, comp = 0;
    if (!stbi_info_from_memory(data, len, &w, &h, &comp) || w <= 0 || h <= 0)
        return false;

    auto *img = new SsrNativeImage();
    img->width = w;
    img->height = h;
    img->url = sourceUrl;

    _ssrPending = img;
    imageWidth = w;
    imageHeight = h;
    return true;
}

bool ImageWidget::_platformStorePixels(unsigned char *, int, int)
{
    return false;
}

void ImageWidget::_platformPromote()
{
    if (!_ssrPending)
        return;
    _ssrImage = _ssrPending;
    _ssrPending = nullptr;
    _setLoadState(ImageLoadState::Loaded);
}

void ImageWidget::_platformRender(GraphicsContext &ctx, int cx, int cy, int cw, int ch)
{
    if (!_ssrImage)
        return;
    Painter painter(ctx, this);
    Painter::ImageDrawParams params;
    params.image = reinterpret_cast<NativeImage>(_ssrImage);
    params.clipX = cx;
    params.clipY = cy;
    params.clipW = cw;
    params.clipH = ch;
    params.borderRadius = borderRadius;
    params.repeat = repeat;
    params.filterQuality = filterQuality;
    painter.drawImage(params);
}

void ImageWidget::_platformInvalidateCache() {}

void ImageWidget::_platformDestroy()
{
    delete _ssrPending;
    _ssrPending = nullptr;
    delete _ssrImage;
    _ssrImage = nullptr;
}

#endif // FLUX_SSR