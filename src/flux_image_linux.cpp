// flux_image_linux.cpp
#if defined(__linux__) && !defined(__ANDROID__)

#include "flux/widgets/flux_image.hpp"

#include <cairo/cairo.h>
#include <algorithm>
#include <cmath>

// ============================================================================
// LinuxScaleCache definition
// ============================================================================

struct ImageWidget::LinuxScaleCache
{
    cairo_surface_t *surface = nullptr;
    int w = 0;
    int h = 0;
    int fit = -1;
    int quality = -1;

    void invalidate()
    {
        if (surface)
        {
            cairo_surface_destroy(surface);
            surface = nullptr;
        }
        w = h = 0;
        fit = quality = -1;
    }

    ~LinuxScaleCache() { invalidate(); }
};

void ImageWidget::LinuxScaleCacheDeleter::operator()(LinuxScaleCache *p) const { delete p; }

ImageWidget::~ImageWidget() { _platformDestroy(); }

// ============================================================================
// Helpers (file-scope)
// ============================================================================

static cairo_filter_t cairoFilter(FilterQuality q)
{
    switch (q)
    {
    case FilterQuality::None:
        return CAIRO_FILTER_NEAREST;
    case FilterQuality::Low:
        return CAIRO_FILTER_BILINEAR;
    case FilterQuality::Medium:
        return CAIRO_FILTER_BILINEAR;
    case FilterQuality::High:
        return CAIRO_FILTER_BEST;
    }
    return CAIRO_FILTER_BILINEAR;
}

static void cairoRoundedRectPath(cairo_t *cr,
                                 double rx, double ry,
                                 double rw, double rh,
                                 double radius)
{
    double r = radius;
    if (r > rw * 0.5)
        r = rw * 0.5;
    if (r > rh * 0.5)
        r = rh * 0.5;
    cairo_new_path(cr);
    cairo_arc(cr, rx + rw - r, ry + r, r, -M_PI * 0.5, 0.0);
    cairo_arc(cr, rx + rw - r, ry + rh - r, r, 0.0, M_PI * 0.5);
    cairo_arc(cr, rx + r, ry + rh - r, r, M_PI * 0.5, M_PI);
    cairo_arc(cr, rx + r, ry + r, r, M_PI, -M_PI * 0.5);
    cairo_close_path(cr);
}

// stb RGBA → Cairo ARGB32 premultiplied
static void convertRGBAToCairoPremul(unsigned char *src, int w, int h,
                                     std::vector<uint8_t> &out)
{
    const size_t nPx = (size_t)w * (size_t)h;
    out.resize(nPx * 4);
    for (size_t i = 0; i < nPx; ++i)
    {
        uint8_t r = src[i * 4 + 0], g = src[i * 4 + 1],
                b = src[i * 4 + 2], a = src[i * 4 + 3];
        out[i * 4 + 0] = (uint8_t)((uint32_t)b * a / 255u); // B premul
        out[i * 4 + 1] = (uint8_t)((uint32_t)g * a / 255u); // G premul
        out[i * 4 + 2] = (uint8_t)((uint32_t)r * a / 255u); // R premul
        out[i * 4 + 3] = a;
    }
}

// ============================================================================
// _platformDecode  — not used on Linux (stb path handles decode)
// ============================================================================

bool ImageWidget::_platformDecode(const uint8_t * /*data*/, int /*len*/)
{
    return false;
}

// ============================================================================
// _platformStorePixels  — convert stb RGBA → premul ARGB, write to _pending
// ============================================================================

bool ImageWidget::_platformStorePixels(unsigned char *rgba, int w, int h)
{
    DecodedImage img;
    img.width = w;
    img.height = h;
    convertRGBAToCairoPremul(rgba, w, h, img.pixels);

    std::lock_guard<std::mutex> lock(_decodeMutex);
    _pending = std::move(img);
    return true;
}

// ============================================================================
// _platformPromote  — UI thread: move _pending → pixels, rebuild cache
// ============================================================================

void ImageWidget::_platformPromote()
{
    DecodedImage staged;
    {
        std::lock_guard<std::mutex> lock(_decodeMutex);
        if (!_pending.ready())
            return;
        staged = std::move(_pending);
        _pending.clear();
    }

    pixels = std::move(staged.pixels);
    imageWidth = staged.width;
    imageHeight = staged.height;
    _platformInvalidateCache();
    _setLoadState(ImageLoadState::Loaded);
    markNeedsLayout();
}

// ============================================================================
// _platformRender
// ============================================================================

void ImageWidget::_platformRender(GraphicsContext &ctx, int cx, int cy,
                                  int cw, int ch)
{
    if (pixels.empty() || !ctx.cr)
        return;
    if (!_linuxCache)
        _linuxCache.reset(new LinuxScaleCache());
    auto &cache = *_linuxCache;

    DestRect d = _calculateDestRect(cx, cy, cw, ch);
    int dw = std::max(1, (int)d.w);
    int dh = std::max(1, (int)d.h);

    // ── Rebuild scaled surface if stale ──────────────────────────────────────
    bool stale = !cache.surface || cache.w != dw || cache.h != dh || cache.fit != (int)fit || cache.quality != (int)filterQuality;

    if (stale)
    {
        cache.invalidate();
        cairo_surface_t *surf =
            cairo_image_surface_create(CAIRO_FORMAT_ARGB32, dw, dh);

        if (cairo_surface_status(surf) == CAIRO_STATUS_SUCCESS)
        {
            cairo_t *scr = cairo_create(surf);
            cairo_surface_t *src = cairo_image_surface_create_for_data(
                const_cast<uint8_t *>(pixels.data()),
                CAIRO_FORMAT_ARGB32,
                imageWidth, imageHeight, imageWidth * 4);

            if (cairo_surface_status(src) == CAIRO_STATUS_SUCCESS)
            {
                cairo_scale(scr,
                            (double)dw / imageWidth,
                            (double)dh / imageHeight);
                cairo_set_source_surface(scr, src, 0.0, 0.0);
                cairo_pattern_set_filter(cairo_get_source(scr),
                                         cairoFilter(filterQuality));
                cairo_paint(scr);
            }
            cairo_surface_destroy(src);
            cairo_destroy(scr);

            cache.surface = surf;
            cache.w = dw;
            cache.h = dh;
            cache.fit = (int)fit;
            cache.quality = (int)filterQuality;
        }
        else
        {
            cairo_surface_destroy(surf);
        }
    }

    cairo_t *cr = ctx.cr;
    cairo_save(cr);

    if (borderRadius > 0)
        cairoRoundedRectPath(cr, x, y, width, height, borderRadius);
    else
        cairo_rectangle(cr, cx, cy, cw, ch);
    cairo_clip(cr);

    if (cache.surface)
    {
        if (repeat != ImageRepeat::NoRepeat)
        {
            float tileW = d.w, tileH = d.h;
            float startX = (repeat == ImageRepeat::RepeatY) ? d.x : (float)cx;
            float startY = (repeat == ImageRepeat::RepeatX) ? d.y : (float)cy;
            float endX = (repeat == ImageRepeat::RepeatY) ? d.x + tileW : (float)(cx + cw);
            float endY = (repeat == ImageRepeat::RepeatX) ? d.y + tileH : (float)(cy + ch);

            for (float ty = startY; ty < endY; ty += tileH)
            {
                for (float tx = startX; tx < endX; tx += tileW)
                {
                    cairo_set_source_surface(cr, cache.surface, tx, ty);
                    cairo_pattern_set_filter(cairo_get_source(cr),
                                             CAIRO_FILTER_NEAREST);
                    cairo_rectangle(cr, tx, ty, tileW, tileH);
                    cairo_fill(cr);
                }
            }
        }
        else
        {
            cairo_set_source_surface(cr, cache.surface, d.x, d.y);
            cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_NEAREST);
            cairo_paint(cr);
        }
    }
    else
    {
        // Slow fallback — no pre-scaled surface
        cairo_surface_t *src = cairo_image_surface_create_for_data(
            const_cast<uint8_t *>(pixels.data()),
            CAIRO_FORMAT_ARGB32,
            imageWidth, imageHeight, imageWidth * 4);
        if (cairo_surface_status(src) == CAIRO_STATUS_SUCCESS)
        {
            cairo_translate(cr, d.x, d.y);
            cairo_scale(cr, d.w / imageWidth, d.h / imageHeight);
            cairo_set_source_surface(cr, src, 0.0, 0.0);
            cairo_pattern_set_filter(cairo_get_source(cr),
                                     cairoFilter(filterQuality));
            cairo_paint(cr);
        }
        cairo_surface_destroy(src);
    }

    cairo_restore(cr);
}

// ============================================================================
// _platformInvalidateCache
// ============================================================================

void ImageWidget::_platformInvalidateCache()
{
    if (_linuxCache)
        _linuxCache->invalidate();
}

// ============================================================================
// _platformDestroy
// ============================================================================

void ImageWidget::_platformDestroy()
{
    _linuxCache.reset();
}

#endif // __linux__