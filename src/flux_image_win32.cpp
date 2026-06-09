// flux_image_win32.cpp
#ifdef _WIN32

#include "flux/widgets/flux_image.hpp"

#include <gdiplus.h>
#include <mutex>
#include <objidl.h>

// ============================================================================
// Win32State definition
// ============================================================================

struct ImageWidget::Win32State {
    std::unique_ptr<Gdiplus::Bitmap> bitmap;
    mutable std::mutex               decodeMutex;

    mutable std::unique_ptr<Gdiplus::Bitmap> scaledBitmap;
    mutable int  scaledW       = 0;
    mutable int  scaledH       = 0;
    mutable int  scaledFit     = -1;
    mutable int  scaledQuality = -1;

    // Set to true by decode thread to signal cache flush needed
    mutable bool pendingFlush  = false;

    void invalidateCache() {
        scaledBitmap.reset();
        scaledW = scaledH = 0;
        scaledFit = scaledQuality = -1;
    }
};


void ImageWidget::Win32StateDeleter::operator()(Win32State* p) const { delete p; }

ImageWidget::~ImageWidget() { _platformDestroy(); }

// ============================================================================
// Helpers (file-scope)
// ============================================================================

static Gdiplus::InterpolationMode gdiInterpolation(FilterQuality q) {
    switch (q) {
    case FilterQuality::None:   return Gdiplus::InterpolationModeNearestNeighbor;
    case FilterQuality::Low:    return Gdiplus::InterpolationModeBilinear;
    case FilterQuality::Medium: return Gdiplus::InterpolationModeHighQualityBilinear;
    case FilterQuality::High:   return Gdiplus::InterpolationModeHighQualityBicubic;
    }
    return Gdiplus::InterpolationModeBilinear;
}

static void renderRepeat(Gdiplus::Graphics &g, Gdiplus::Bitmap *src,
                         ImageRepeat repeat,
                         const ImageWidget::DestRect &d,
                         int cx, int cy, int cw, int ch) {
    float tileW  = d.w, tileH  = d.h;
    float startX = (repeat == ImageRepeat::RepeatY) ? d.x        : (float)cx;
    float startY = (repeat == ImageRepeat::RepeatX) ? d.y        : (float)cy;
    float endX   = (repeat == ImageRepeat::RepeatY) ? d.x + tileW : (float)(cx + cw);
    float endY   = (repeat == ImageRepeat::RepeatX) ? d.y + tileH : (float)(cy + ch);

    for (float ty = startY; ty < endY; ty += tileH)
        for (float tx = startX; tx < endX; tx += tileW)
            g.DrawImage(src, Gdiplus::RectF(tx, ty, tileW, tileH));
}

// ============================================================================
// _platformDecode   (Win32 — GDI+ stream, called from background thread)
// ============================================================================

bool ImageWidget::_platformDecode(const uint8_t *data, int len) {
    if (!_win32) _win32.reset(new Win32State());

    IStream *stream = nullptr;
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, (SIZE_T)len);
    if (!hMem) return false;

    void *ptr = GlobalLock(hMem);
    if (!ptr) { GlobalFree(hMem); return false; }
    memcpy(ptr, data, len);
    GlobalUnlock(hMem);

    if (CreateStreamOnHGlobal(hMem, TRUE, &stream) != S_OK) {
        GlobalFree(hMem); return false;
    }

    std::unique_ptr<Gdiplus::Bitmap> decoded;
    try {
        decoded = std::make_unique<Gdiplus::Bitmap>(stream);
        stream->Release();
        if (!decoded || decoded->GetLastStatus() != Gdiplus::Ok) return false;
    } catch (...) {
        stream->Release(); return false;
    }

    int w = (int)decoded->GetWidth();
    int h = (int)decoded->GetHeight();
    if (w == 0 || h == 0) return false;

    {
        std::lock_guard<std::mutex> lock(_win32->decodeMutex);
        _win32->bitmap      = std::move(decoded);
        imageWidth          = w;
        imageHeight         = h;
        _win32->pendingFlush = true;
    }
    _setLoadState(ImageLoadState::Loaded);
    return true;
}

// ============================================================================
// _platformStorePixels  — not used on Win32
// ============================================================================

bool ImageWidget::_platformStorePixels(unsigned char * /*rgba*/,
                                       int /*w*/, int /*h*/) {
    return false; // GDI+ path never calls stb
}

// ============================================================================
// _platformPromote  (flush scaled cache if decode thread flagged it)
// ============================================================================

void ImageWidget::_platformPromote() {
    if (!_win32) return;
    std::lock_guard<std::mutex> lock(_win32->decodeMutex);
    if (_win32->pendingFlush) {
        _win32->invalidateCache();
        _win32->pendingFlush = false;
    }
}

// ============================================================================
// _platformRender
// ============================================================================

void ImageWidget::_platformRender(GraphicsContext &ctx, int cx, int cy,
                                  int cw, int ch) {
    if (!_win32) return;
    std::lock_guard<std::mutex> lock(_win32->decodeMutex);
    if (!_win32->bitmap) return;

    DestRect d  = _calculateDestRect(cx, cy, cw, ch);
    int dw = std::max(1, (int)d.w);
    int dh = std::max(1, (int)d.h);

    // ── Rebuild scaled bitmap if stale ───────────────────────────────────────
    bool stale = !_win32->scaledBitmap
              || _win32->scaledW       != dw
              || _win32->scaledH       != dh
              || _win32->scaledFit     != (int)fit
              || _win32->scaledQuality != (int)filterQuality;

    if (stale) {
        _win32->scaledBitmap =
            std::make_unique<Gdiplus::Bitmap>(dw, dh, PixelFormat32bppPARGB);
        if (_win32->scaledBitmap &&
            _win32->scaledBitmap->GetLastStatus() == Gdiplus::Ok) {
            Gdiplus::Graphics sg(_win32->scaledBitmap.get());
            sg.SetInterpolationMode(gdiInterpolation(filterQuality));
            sg.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
            sg.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
            sg.DrawImage(_win32->bitmap.get(),
                         Gdiplus::RectF(0.f, 0.f, (float)dw, (float)dh));
            _win32->scaledW       = dw;
            _win32->scaledH       = dh;
            _win32->scaledFit     = (int)fit;
            _win32->scaledQuality = (int)filterQuality;
        } else {
            _win32->scaledBitmap.reset();
            _win32->scaledW = _win32->scaledH = 0;
            _win32->scaledFit = _win32->scaledQuality = -1;
        }
    }

    Gdiplus::Bitmap *src = _win32->scaledBitmap
                         ? _win32->scaledBitmap.get()
                         : _win32->bitmap.get();

    // ── Fast path: no border radius ──────────────────────────────────────────
    if (borderRadius <= 0) {
        Gdiplus::Graphics g(ctx.hdc);
        g.SetInterpolationMode(_win32->scaledBitmap
            ? Gdiplus::InterpolationModeNearestNeighbor
            : gdiInterpolation(filterQuality));
        g.SetClip(Gdiplus::Rect(cx, cy, cw, ch));

        if (repeat != ImageRepeat::NoRepeat && _win32->scaledBitmap)
            renderRepeat(g, src, repeat, d, cx, cy, cw, ch);
        else
            g.DrawImage(src, Gdiplus::RectF(d.x, d.y, (float)dw, (float)dh));
        return;
    }

    // ── Rounded corners: render to offscreen ARGB bitmap then alpha-blit ─────
    int ow = width, oh = height;
    Gdiplus::Bitmap offscreen(ow, oh, PixelFormat32bppPARGB);
    if (offscreen.GetLastStatus() != Gdiplus::Ok) return;

    {
        Gdiplus::Graphics og(&offscreen);
        og.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        og.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
        og.SetInterpolationMode(_win32->scaledBitmap
            ? Gdiplus::InterpolationModeNearestNeighbor
            : gdiInterpolation(filterQuality));
        og.Clear(Gdiplus::Color(0, 0, 0, 0));

        float r    = (float)borderRadius;
        float diam = r * 2.0f;
        float fw   = (float)ow;
        float fh   = (float)oh;

        Gdiplus::GraphicsPath path;
        path.AddArc(0.f,        0.f,        diam, diam, 180.f, 90.f);
        path.AddArc(fw - diam,  0.f,        diam, diam, 270.f, 90.f);
        path.AddArc(fw - diam,  fh - diam,  diam, diam,   0.f, 90.f);
        path.AddArc(0.f,        fh - diam,  diam, diam,  90.f, 90.f);
        path.CloseFigure();

        Gdiplus::Region region(&path);
        og.SetClip(&region);

        float lx = d.x - (float)x;
        float ly = d.y - (float)y;

        if (repeat != ImageRepeat::NoRepeat && _win32->scaledBitmap) {
            float tileW  = d.w, tileH  = d.h;
            float startX = (repeat == ImageRepeat::RepeatY) ? lx         : 0.f;
            float startY = (repeat == ImageRepeat::RepeatX) ? ly         : 0.f;
            float endX   = (repeat == ImageRepeat::RepeatY) ? lx + tileW : (float)ow;
            float endY   = (repeat == ImageRepeat::RepeatX) ? ly + tileH : (float)oh;
            for (float ty = startY; ty < endY; ty += tileH)
                for (float tx = startX; tx < endX; tx += tileW)
                    og.DrawImage(src, Gdiplus::RectF(tx, ty, tileW, tileH));
        } else {
            og.DrawImage(src, Gdiplus::RectF(lx, ly, (float)dw, (float)dh));
        }
    }

    Gdiplus::Graphics g(ctx.hdc);
    g.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
    g.DrawImage(&offscreen,
                Gdiplus::RectF((float)x, (float)y, (float)ow, (float)oh));
}

// ============================================================================
// _platformInvalidateCache
// ============================================================================

void ImageWidget::_platformInvalidateCache() {
    if (_win32) {
        std::lock_guard<std::mutex> lock(_win32->decodeMutex);
        _win32->invalidateCache();
    }
}

// ============================================================================
// _platformDestroy
// ============================================================================

void ImageWidget::_platformDestroy() {
    _win32.reset();
}

#endif // _WIN32