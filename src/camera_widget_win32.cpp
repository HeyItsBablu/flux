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

    // Pull latest frame
    if (cam.updateFrame() && cam.getPreviewWidth() > 0)
    {
        auto frame = cam.lockFrame();
        if (frame.data && frame.width > 0)
        {
            size_t bytes = (size_t)(frame.stride * frame.height);
            s.frameCache.assign(frame.data, frame.data + bytes);
            s.cachedSrcW = frame.width;
            s.cachedSrcH = frame.height;

            s.bmi = {};
            s.bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            s.bmi.bmiHeader.biWidth = frame.width;
            s.bmi.bmiHeader.biHeight = -frame.height; // top-down
            s.bmi.bmiHeader.biPlanes = 1;
            s.bmi.bmiHeader.biBitCount = 32;
            s.bmi.bmiHeader.biCompression = BI_RGB;
        }
    }

    if (s.frameCache.empty() || s.cachedSrcW <= 0)
        return false;

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

    ::SetStretchBltMode(ctx.hdc, HALFTONE);
    ::SetBrushOrgEx(ctx.hdc, 0, 0, nullptr);

    if (cam.isFrontCamera())
    {
        // Negative dstW → GDI mirrors horizontally
        ::StretchDIBits(ctx.hdc,
                        dstX + dstW, dstY, -dstW, dstH,
                        0, 0, s.cachedSrcW, s.cachedSrcH,
                        s.frameCache.data(), &s.bmi, DIB_RGB_COLORS, SRCCOPY);
    }
    else
    {
        ::StretchDIBits(ctx.hdc,
                        dstX, dstY, dstW, dstH,
                        0, 0, s.cachedSrcW, s.cachedSrcH,
                        s.frameCache.data(), &s.bmi, DIB_RGB_COLORS, SRCCOPY);
    }
    return true;
}

// ── _platformRenderFlash ──────────────────────────────────────────────────────

void CameraWidget::_platformRenderFlash(GraphicsContext &ctx, Painter & /*p*/,
                                        int viewH)
{
    HDC memDC = ::CreateCompatibleDC(ctx.hdc);
    HBITMAP hbm = ::CreateCompatibleBitmap(ctx.hdc, 1, 1);
    HGDIOBJ old = ::SelectObject(memDC, hbm);
    ::SetPixel(memDC, 0, 0, RGB(255, 255, 255));

    BLENDFUNCTION bf = {};
    bf.BlendOp = AC_SRC_OVER;
    bf.SourceConstantAlpha = (BYTE)(_flashAlpha * 255.f);
    bf.AlphaFormat = 0;
    ::AlphaBlend(ctx.hdc, x, y, width, viewH, memDC, 0, 0, 1, 1, bf);

    ::SelectObject(memDC, old);
    ::DeleteObject(hbm);
    ::DeleteDC(memDC);
}

// ── _platformRenderThumb ──────────────────────────────────────────────────────

bool CameraWidget::_platformRenderThumb(GraphicsContext &ctx,
                                        int thumbX, int thumbY,
                                        int thumbW, int thumbH)
{
    if (!_win32 || _win32->thumbCache.empty() || _win32->thumbSrcW <= 0)
        return false;
    auto &s = *_win32;
    ::SetStretchBltMode(ctx.hdc, HALFTONE);
    ::SetBrushOrgEx(ctx.hdc, 0, 0, nullptr);
    ::StretchDIBits(ctx.hdc,
                    thumbX, thumbY, thumbW, thumbH,
                    0, 0, s.thumbSrcW, s.thumbSrcH,
                    s.thumbCache.data(), &s.thumbBmi, DIB_RGB_COLORS, SRCCOPY);
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