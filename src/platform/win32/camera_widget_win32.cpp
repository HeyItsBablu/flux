// camera_widget_win32.cpp
// Windows platform implementation for CameraWidget.
//
// Preview: GDI StretchDIBits from a BGRA32 frame cache (lockFrame()).
// Thumbnail: WIC decoder → BGRA32 → StretchDIBits.
// Flash: AlphaBlend over a 1×1 white bitmap.
//
// Link: gdi32  msimg32  windowscodecs  mf  mfplat  mfreadwrite  mfuuid
//       ole32  oleaut32

#ifdef _WIN32

#include "flux/widgets/camera_widget.hpp"

// ── Win32State deleter ────────────────────────────────────────────────────────

void CameraWidget::Win32StateDeleter::operator()(Win32State *p) const
{
    delete p;
}

// ── Lazy initialiser ──────────────────────────────────────────────────────────

static CameraWidget::Win32State &getWin32(
    std::unique_ptr<CameraWidget::Win32State,
                    CameraWidget::Win32StateDeleter> &ptr)
{
    if (!ptr)
        ptr.reset(new CameraWidget::Win32State());
    return *ptr;
}

// ── _platformScheduleOpen ─────────────────────────────────────────────────────
// No permission check needed on Windows — open immediately via the render flag.

void CameraWidget::_platformScheduleOpen()
{
    _shouldOpen = true;
}

// ── _platformOnFlip ───────────────────────────────────────────────────────────

void CameraWidget::_platformOnFlip()
{
    if (_win32)
        _win32->frameCache.clear();
}

// ── _platformRenderPreview ────────────────────────────────────────────────────
bool CameraWidget::_platformRenderPreview(GraphicsContext &ctx, Painter &p,
                                          FontCache & /*fontCache*/, int viewH)
{
    auto &cam = FluxCamera::get();
    auto &s = getWin32(_win32);

    // Pull latest frame — camera delivers BGRA32
    if (cam.updateFrame() && cam.getPreviewWidth() > 0)
    {
        auto frame = cam.lockFrame();
        if (frame.data && frame.width > 0)
        {
            size_t bytes = (size_t)(frame.stride * frame.height);
            s.frameCache.assign(frame.data, frame.data + bytes);
            s.cachedSrcW = frame.width;
            s.cachedSrcH = frame.height;
        }
    }

    if (s.frameCache.empty() || s.cachedSrcW <= 0 || !ctx.dc)
        return false;

    // Upload to D2D bitmap if size changed or first time
    if (!s.d2dBitmap ||
        s.d2dBitmapW != s.cachedSrcW ||
        s.d2dBitmapH != s.cachedSrcH)
    {
        s.d2dBitmap = nullptr;
        s.d2dBitmapW = s.cachedSrcW;
        s.d2dBitmapH = s.cachedSrcH;

        D2D1_BITMAP_PROPERTIES bp = D2D1::BitmapProperties(
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                              D2D1_ALPHA_MODE_IGNORE));
        ctx.dc->CreateBitmap(
            D2D1::SizeU((UINT32)s.cachedSrcW, (UINT32)s.cachedSrcH),
            nullptr, 0,
            bp, &s.d2dBitmap);
    }

    if (s.d2dBitmap)
    {
        D2D1_RECT_U r = D2D1::RectU(0, 0,
                                    (UINT32)s.cachedSrcW,
                                    (UINT32)s.cachedSrcH);
        s.d2dBitmap->CopyFromMemory(&r,
                                    s.frameCache.data(),
                                    (UINT32)(s.cachedSrcW * 4));
    }

    // Letterbox / pillarbox
    float camAR = (float)s.cachedSrcW / (float)s.cachedSrcH;
    float widgetAR = (float)width / (float)viewH;
    int dstW, dstH, dstX, dstY;
    if (camAR > widgetAR)
    {
        dstW = width;
        dstH = (int)((float)width / camAR);
        dstX = x;
        dstY = y + (viewH - dstH) / 2;
    }
    else
    {
        dstH = viewH;
        dstW = (int)((float)viewH * camAR);
        dstX = x + (width - dstW) / 2;
        dstY = y;
    }

    // Fill letterbox bars
    if (dstX > x)
        p.fillRect(x, y, dstX - x, viewH, colPlaceholder);
    if (dstX + dstW < x + width)
        p.fillRect(dstX + dstW, y, (x + width) - (dstX + dstW), viewH, colPlaceholder);
    if (dstY > y)
        p.fillRect(x, y, width, dstY - y, colPlaceholder);
    if (dstY + dstH < y + viewH)
        p.fillRect(x, dstY + dstH, width, (y + viewH) - (dstY + dstH), colPlaceholder);

    Painter::CameraDrawParams cp;
    cp.frame = static_cast<NativeImage>(s.d2dBitmap.Get());
    cp.dstX = dstX;
    cp.dstY = dstY;
    cp.dstW = dstW;
    cp.dstH = dstH;
    cp.mirror = cam.isFrontCamera();
    p.drawCamera(cp);
    return true;
}

void CameraWidget::_platformRenderFlash(GraphicsContext &ctx, Painter &p,
                                        int viewH)
{
    // D2D path: just fill a semi-transparent white rect — no GDI needed
    Color flashColor = Color::fromRGBA(255, 255, 255,
                                       (uint8_t)(_flashAlpha * 255.f));
    p.fillRect(x, y, width, viewH, flashColor);
}

bool CameraWidget::_platformRenderThumb(GraphicsContext &ctx,
                                        int thumbX, int thumbY,
                                        int thumbW, int thumbH)
{
    if (!_win32 || _win32->thumbCache.empty() || _win32->thumbSrcW <= 0 || !ctx.dc)
        return false;

    auto &s = *_win32;

    // Upload thumbnail to D2D bitmap on first use or size change
    if (!s.thumbD2dBitmap ||
        s.thumbD2dW != s.thumbSrcW ||
        s.thumbD2dH != s.thumbSrcH)
    {
        s.thumbD2dBitmap = nullptr;
        s.thumbD2dW = s.thumbSrcW;
        s.thumbD2dH = s.thumbSrcH;

        D2D1_BITMAP_PROPERTIES bp = D2D1::BitmapProperties(
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                              D2D1_ALPHA_MODE_IGNORE));
        ctx.dc->CreateBitmap(
            D2D1::SizeU((UINT32)s.thumbSrcW, (UINT32)s.thumbSrcH),
            nullptr, 0,
            bp, &s.thumbD2dBitmap);

        if (s.thumbD2dBitmap)
        {
            D2D1_RECT_U r = D2D1::RectU(0, 0,
                                        (UINT32)s.thumbSrcW,
                                        (UINT32)s.thumbSrcH);
            s.thumbD2dBitmap->CopyFromMemory(&r,
                                             s.thumbCache.data(),
                                             (UINT32)(s.thumbSrcW * 4));
        }
    }

    if (!s.thumbD2dBitmap)
        return false;

    Painter::CameraDrawParams cp;
    cp.frame = static_cast<NativeImage>(s.thumbD2dBitmap.Get());
    cp.dstX = thumbX;
    cp.dstY = thumbY;
    cp.dstW = thumbW;
    cp.dstH = thumbH;
    Painter(ctx).drawCamera(cp);
    return true;
}

// ── _platformLoadThumb ────────────────────────────────────────────────────────
// Decode any WIC-supported format (JPEG, PNG, …) to BGRA32 for StretchDIBits.

void CameraWidget::_platformLoadThumb(const std::string &path)
{
    auto &s = getWin32(_win32);
    s.thumbCache.clear();
    s.thumbSrcW = s.thumbSrcH = 0;

    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    ComPtr<IWICImagingFactory> wic;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                                CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))))
        return;

    std::wstring wpath(path.begin(), path.end());

    ComPtr<IWICBitmapDecoder> decoder;
    if (FAILED(wic->CreateDecoderFromFilename(wpath.c_str(), nullptr,
                                              GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder)))
        return;

    ComPtr<IWICBitmapFrameDecode> srcFrame;
    if (FAILED(decoder->GetFrame(0, &srcFrame)))
        return;

    ComPtr<IWICFormatConverter> conv;
    if (FAILED(wic->CreateFormatConverter(&conv)))
        return;
    if (FAILED(conv->Initialize(srcFrame.Get(),
                                GUID_WICPixelFormat32bppBGRA,
                                WICBitmapDitherTypeNone, nullptr, 0.0,
                                WICBitmapPaletteTypeCustom)))
        return;

    UINT w = 0, h = 0;
    conv->GetSize(&w, &h);
    if (w == 0 || h == 0)
        return;

    UINT stride = w * 4;
    s.thumbCache.resize((size_t)(stride * h));
    if (FAILED(conv->CopyPixels(nullptr, stride,
                                (UINT)s.thumbCache.size(),
                                s.thumbCache.data())))
    {
        s.thumbCache.clear();
        return;
    }

    s.thumbSrcW = (int)w;
    s.thumbSrcH = (int)h;

    s.thumbBmi = {};
    s.thumbBmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    s.thumbBmi.bmiHeader.biWidth = (int)w;
    s.thumbBmi.bmiHeader.biHeight = -(int)h; // top-down
    s.thumbBmi.bmiHeader.biPlanes = 1;
    s.thumbBmi.bmiHeader.biBitCount = 32;
    s.thumbBmi.bmiHeader.biCompression = BI_RGB;
}

// ── _platformDestroy ──────────────────────────────────────────────────────────

void CameraWidget::_platformDestroy()
{
    _win32.reset();
}

#endif // _WIN32