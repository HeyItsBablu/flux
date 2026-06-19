// flux_image_win32.cpp
#ifdef _WIN32

#include "flux/widgets/flux_image.hpp"

#include <gdiplus.h>
#include <mutex>
#include <objidl.h>

// ============================================================================
// Win32State definition
// ============================================================================

struct ImageWidget::Win32State
{
    std::unique_ptr<Gdiplus::Bitmap> bitmap;
    mutable std::mutex decodeMutex;

    mutable std::unique_ptr<Gdiplus::Bitmap> scaledBitmap;
    mutable int scaledW = 0;
    mutable int scaledH = 0;
    mutable int scaledFit = -1;
    mutable int scaledQuality = -1;

    // Set to true by decode thread to signal cache flush needed
    mutable bool pendingFlush = false;

    void invalidateCache()
    {
        scaledBitmap.reset();
        scaledW = scaledH = 0;
        scaledFit = scaledQuality = -1;
    }
};

void ImageWidget::Win32StateDeleter::operator()(Win32State *p) const { delete p; }

ImageWidget::~ImageWidget() { _platformDestroy(); }

// ============================================================================
// Helpers (file-scope)
// ============================================================================

static Gdiplus::InterpolationMode gdiInterpolation(FilterQuality q)
{
    switch (q)
    {
    case FilterQuality::None:
        return Gdiplus::InterpolationModeNearestNeighbor;
    case FilterQuality::Low:
        return Gdiplus::InterpolationModeBilinear;
    case FilterQuality::Medium:
        return Gdiplus::InterpolationModeHighQualityBilinear;
    case FilterQuality::High:
        return Gdiplus::InterpolationModeHighQualityBicubic;
    }
    return Gdiplus::InterpolationModeBilinear;
}

static void renderRepeat(Gdiplus::Graphics &g, Gdiplus::Bitmap *src,
                         ImageRepeat repeat,
                         const ImageWidget::DestRect &d,
                         int cx, int cy, int cw, int ch)
{
    float tileW = d.w, tileH = d.h;
    float startX = (repeat == ImageRepeat::RepeatY) ? d.x : (float)cx;
    float startY = (repeat == ImageRepeat::RepeatX) ? d.y : (float)cy;
    float endX = (repeat == ImageRepeat::RepeatY) ? d.x + tileW : (float)(cx + cw);
    float endY = (repeat == ImageRepeat::RepeatX) ? d.y + tileH : (float)(cy + ch);

    for (float ty = startY; ty < endY; ty += tileH)
        for (float tx = startX; tx < endX; tx += tileW)
            g.DrawImage(src, Gdiplus::RectF(tx, ty, tileW, tileH));
}

// ============================================================================
// _platformDecode   (Win32 — GDI+ stream, called from background thread)
// ============================================================================

bool ImageWidget::_platformDecode(const uint8_t *data, int len)
{
    if (!_win32)
        _win32.reset(new Win32State());

    IStream *stream = nullptr;
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, (SIZE_T)len);
    if (!hMem)
        return false;

    void *ptr = GlobalLock(hMem);
    if (!ptr)
    {
        GlobalFree(hMem);
        return false;
    }
    memcpy(ptr, data, len);
    GlobalUnlock(hMem);

    if (CreateStreamOnHGlobal(hMem, TRUE, &stream) != S_OK)
    {
        GlobalFree(hMem);
        return false;
    }

    std::unique_ptr<Gdiplus::Bitmap> decoded;
    try
    {
        decoded = std::make_unique<Gdiplus::Bitmap>(stream);
        stream->Release();
        if (!decoded || decoded->GetLastStatus() != Gdiplus::Ok)
            return false;
    }
    catch (...)
    {
        stream->Release();
        return false;
    }

    int w = (int)decoded->GetWidth();
    int h = (int)decoded->GetHeight();
    if (w == 0 || h == 0)
        return false;

    {
        std::lock_guard<std::mutex> lock(_win32->decodeMutex);
        _win32->bitmap = std::move(decoded);
        imageWidth = w;
        imageHeight = h;
        _win32->pendingFlush = true;
    }
    _setLoadState(ImageLoadState::Loaded);
    return true;
}

// ============================================================================
// _platformStorePixels  — not used on Win32
// ============================================================================

bool ImageWidget::_platformStorePixels(unsigned char * /*rgba*/,
                                       int /*w*/, int /*h*/)
{
    return false; // GDI+ path never calls stb
}

// ============================================================================
// _platformPromote  (flush scaled cache if decode thread flagged it)
// ============================================================================

void ImageWidget::_platformPromote()
{
    if (!_win32)
        return;
    std::lock_guard<std::mutex> lock(_win32->decodeMutex);
    if (_win32->pendingFlush)
    {
        _win32->invalidateCache();
        _win32->pendingFlush = false;
    }
}

// ============================================================================
// _platformRender
// ============================================================================

void ImageWidget::_platformRender(GraphicsContext &ctx, int cx, int cy,
                                  int cw, int ch)
{
    if (!_win32)
        return;
    std::lock_guard<std::mutex> lock(_win32->decodeMutex);
    if (!_win32->bitmap)
        return;

    DestRect d = _calculateDestRect(cx, cy, cw, ch);
    int dw = std::max(1, (int)d.w);
    int dh = std::max(1, (int)d.h);

    // ── Rebuild scaled bitmap if stale ───────────────────────────────────────
    bool stale = !_win32->scaledBitmap || _win32->scaledW != dw || _win32->scaledH != dh || _win32->scaledFit != (int)fit || _win32->scaledQuality != (int)filterQuality;

    if (stale)
    {
        _win32->scaledBitmap =
            std::make_unique<Gdiplus::Bitmap>(dw, dh, PixelFormat32bppPARGB);
        if (_win32->scaledBitmap &&
            _win32->scaledBitmap->GetLastStatus() == Gdiplus::Ok)
        {
            Gdiplus::Graphics sg(_win32->scaledBitmap.get());
            sg.SetInterpolationMode(gdiInterpolation(filterQuality));
            sg.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
            sg.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
            sg.DrawImage(_win32->bitmap.get(),
                         Gdiplus::RectF(0.f, 0.f, (float)dw, (float)dh));
            _win32->scaledW = dw;
            _win32->scaledH = dh;
            _win32->scaledFit = (int)fit;
            _win32->scaledQuality = (int)filterQuality;
        }
        else
        {
            _win32->scaledBitmap.reset();
            _win32->scaledW = _win32->scaledH = 0;
            _win32->scaledFit = _win32->scaledQuality = -1;
        }
    }

    Painter painter(ctx);
    Painter::ImageDrawParams params;
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

    if (_win32->scaledBitmap)
    {
        // Pre-scaled — painter does a plain (unscaled, NEAREST) blit.
        params.image = _win32->scaledBitmap.get();
        params.srcWidth = dw;
        params.srcHeight = dh;
    }
    else
    {
        // No usable cache — let painter scale the raw bitmap at draw time.
        params.image = _win32->bitmap.get();
        params.srcWidth = imageWidth;
        params.srcHeight = imageHeight;
    }

    painter.drawImage(params);
}

// ============================================================================
// _platformInvalidateCache
// ============================================================================

void ImageWidget::_platformInvalidateCache()
{
    if (_win32)
    {
        std::lock_guard<std::mutex> lock(_win32->decodeMutex);
        _win32->invalidateCache();
    }
}

// ============================================================================
// _platformDestroy
// ============================================================================

void ImageWidget::_platformDestroy()
{
    _win32.reset();
}

#endif // _WIN32