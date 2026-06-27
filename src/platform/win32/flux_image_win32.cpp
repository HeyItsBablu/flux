// flux_image_win32.cpp
#ifdef _WIN32

#include "flux/widgets/flux_image.hpp"

#include <wincodec.h>   // WIC — replaces GDI+ for image decoding
#include <mutex>
#include <d2d1_1.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

#pragma comment(lib, "windowscodecs.lib")

// ============================================================================
// Win32State definition
//
// Strategy: WIC decodes compressed image bytes (PNG, JPEG, BMP, GIF, TIFF…)
// into raw BGRA32 pixels on the background thread.  The raw pixels sit in
// pendingPixels until _platformRender() is called on the render thread inside
// BeginDraw/EndDraw — the only safe place to call CreateBitmap / CopyFromMemory
// on the D2D device context.
//
// No GDI+ / GdiplusStartup needed.  WIC is always available on Vista+.
// ============================================================================

struct ImageWidget::Win32State
{
    mutable std::mutex decodeMutex;

    // ── Decode-thread side ────────────────────────────────────────────────────
    std::vector<uint8_t> pendingPixels;
    int pendingW = 0;
    int pendingH = 0;

    // ── Render-thread side (written & read only inside BeginDraw/EndDraw) ─────
    ComPtr<ID2D1Bitmap1> d2dBitmap;
    int bitmapW = 0;
    int bitmapH = 0;
};

void ImageWidget::Win32StateDeleter::operator()(Win32State *p) const { delete p; }

ImageWidget::~ImageWidget() { _platformDestroy(); }

// ============================================================================
// _platformDecode  (background thread)
//
// WIC pipeline:
//   IWICImagingFactory
//     → IWICBitmapDecoder      (from IStream wrapping the raw bytes)
//       → IWICBitmapFrameDecode
//         → IWICFormatConverter  (convert any format → GUID_WICPixelFormat32bppPBGRA)
//           → CopyPixels()       (write into our CPU buffer)
//
// GUID_WICPixelFormat32bppPBGRA == pre-multiplied BGRA32
//   == DXGI_FORMAT_B8G8R8A8_UNORM + D2D1_ALPHA_MODE_PREMULTIPLIED
// so the pixels can be uploaded to a D2D bitmap directly.
// ============================================================================

bool ImageWidget::_platformDecode(const uint8_t *data, int len)
{
    if (!_win32)
        _win32.reset(new Win32State());

    fluxLog("[_platformDecode] len=" + std::to_string(len));

    // CoInitialize for this background thread
    HRESULT hrCo = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    fluxLog("[_platformDecode] CoInitializeEx hr=0x" + [hrCo](){
        char b[16]; sprintf_s(b, "%08X", (unsigned)hrCo); return std::string(b); }());

    // 1. Wrap bytes in IStream
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, (SIZE_T)len);
    if (!hMem) { fluxLog("[_platformDecode] GlobalAlloc failed"); CoUninitialize(); return false; }

    void *ptr = GlobalLock(hMem);
    if (!ptr) { GlobalFree(hMem); fluxLog("[_platformDecode] GlobalLock failed"); CoUninitialize(); return false; }
    memcpy(ptr, data, len);
    GlobalUnlock(hMem);

    ComPtr<IStream> stream;
    HRESULT hr = CreateStreamOnHGlobal(hMem, TRUE, stream.GetAddressOf());
    fluxLog("[_platformDecode] CreateStreamOnHGlobal hr=0x" + [hr](){
        char b[16]; sprintf_s(b, "%08X", (unsigned)hr); return std::string(b); }());
    if (FAILED(hr)) { GlobalFree(hMem); CoUninitialize(); return false; }

    // 2. WIC factory
    ComPtr<IWICImagingFactory> wicFactory;
    hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                          CLSCTX_INPROC_SERVER, IID_PPV_ARGS(wicFactory.GetAddressOf()));
    fluxLog("[_platformDecode] CoCreateInstance WICFactory hr=0x" + [hr](){
        char b[16]; sprintf_s(b, "%08X", (unsigned)hr); return std::string(b); }());
    if (FAILED(hr) || !wicFactory) { CoUninitialize(); return false; }

    // 3. Decoder
    ComPtr<IWICBitmapDecoder> decoder;
    hr = wicFactory->CreateDecoderFromStream(stream.Get(), nullptr,
                                             WICDecodeMetadataCacheOnLoad,
                                             decoder.GetAddressOf());
    fluxLog("[_platformDecode] CreateDecoderFromStream hr=0x" + [hr](){
        char b[16]; sprintf_s(b, "%08X", (unsigned)hr); return std::string(b); }());
    if (FAILED(hr) || !decoder) { CoUninitialize(); return false; }

    // 4. Frame
    ComPtr<IWICBitmapFrameDecode> frame;
    hr = decoder->GetFrame(0, frame.GetAddressOf());
    fluxLog("[_platformDecode] GetFrame hr=0x" + [hr](){
        char b[16]; sprintf_s(b, "%08X", (unsigned)hr); return std::string(b); }());
    if (FAILED(hr) || !frame) { CoUninitialize(); return false; }

    // 5. Format converter
    ComPtr<IWICFormatConverter> converter;
    hr = wicFactory->CreateFormatConverter(converter.GetAddressOf());
    if (FAILED(hr)) { CoUninitialize(); return false; }

    hr = converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA,
                               WICBitmapDitherTypeNone, nullptr, 0.0,
                               WICBitmapPaletteTypeCustom);
    fluxLog("[_platformDecode] converter->Initialize hr=0x" + [hr](){
        char b[16]; sprintf_s(b, "%08X", (unsigned)hr); return std::string(b); }());
    if (FAILED(hr)) { CoUninitialize(); return false; }

    // 6. Copy pixels
    UINT w = 0, h = 0;
    converter->GetSize(&w, &h);
    fluxLog("[_platformDecode] size=" + std::to_string(w) + "x" + std::to_string(h));

    const UINT stride = w * 4;
    const UINT bufSize = stride * h;
    std::vector<uint8_t> pixels(bufSize);
    hr = converter->CopyPixels(nullptr, stride, bufSize, pixels.data());
    fluxLog("[_platformDecode] CopyPixels hr=0x" + [hr](){
        char b[16]; sprintf_s(b, "%08X", (unsigned)hr); return std::string(b); }());
    if (FAILED(hr)) { CoUninitialize(); return false; }

    {
        std::lock_guard<std::mutex> lock(_win32->decodeMutex);
        _win32->pendingPixels = std::move(pixels);
        _win32->pendingW      = (int)w;
        _win32->pendingH      = (int)h;
        imageWidth            = (int)w;
        imageHeight           = (int)h;
    }

    _setLoadState(ImageLoadState::Loaded);
    fluxLog("[_platformDecode] SUCCESS w=" + std::to_string(w) + " h=" + std::to_string(h));
    CoUninitialize();
    return true;
}

// ============================================================================
// _platformStorePixels  — not used on Win32 (stb path is bypassed)
// ============================================================================

bool ImageWidget::_platformStorePixels(unsigned char * /*rgba*/,
                                       int /*w*/, int /*h*/)
{
    return false;
}

// ============================================================================
// _platformPromote  — no-op on Win32
//
// Upload happens inside _platformRender() where ctx.dc is always live.
// ============================================================================

void ImageWidget::_platformPromote()
{
    // Intentionally empty — upload happens in _platformRender().
}

// ============================================================================
// _platformRender  (render thread, inside BeginDraw/EndDraw)
//
// 1. If pendingPixels is non-empty, upload to D2D bitmap.
// 2. DrawBitmap into the clip rect — D2D handles GPU-side scaling.
// ============================================================================

void ImageWidget::_platformRender(GraphicsContext &ctx, int cx, int cy,
                                  int cw, int ch)
{
    if (!_win32 || !ctx.dc)
        return;

    // ── Upload pending pixels if the decode thread produced new ones ──────────
    {
        std::lock_guard<std::mutex> lock(_win32->decodeMutex);

        if (!_win32->pendingPixels.empty())
        {
            int w = _win32->pendingW;
            int h = _win32->pendingH;

            // Recreate bitmap if size changed
            if (!_win32->d2dBitmap ||
                _win32->bitmapW != w ||
                _win32->bitmapH != h)
            {
                _win32->d2dBitmap = nullptr;

                D2D1_BITMAP_PROPERTIES1 bp = D2D1::BitmapProperties1(
                    D2D1_BITMAP_OPTIONS_NONE,
                    D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                                      D2D1_ALPHA_MODE_PREMULTIPLIED));

                HRESULT hr = ctx.dc->CreateBitmap(
                    D2D1::SizeU((UINT32)w, (UINT32)h),
                    nullptr, 0,
                    bp,
                    &_win32->d2dBitmap);

                if (FAILED(hr))
                {
                    _win32->pendingPixels.clear();
                    _win32->pendingW = _win32->pendingH = 0;
                    return;
                }

                _win32->bitmapW = w;
                _win32->bitmapH = h;
            }

            // Upload pixels to GPU
            D2D1_RECT_U destRect = D2D1::RectU(0, 0, (UINT32)w, (UINT32)h);
            _win32->d2dBitmap->CopyFromMemory(
                &destRect,
                _win32->pendingPixels.data(),
                (UINT32)(w * 4));

            _win32->pendingPixels.clear();
            _win32->pendingW = _win32->pendingH = 0;
        }
    }

    if (!_win32->d2dBitmap)
        return;

    // ── Draw ──────────────────────────────────────────────────────────────────
    DestRect d = _calculateDestRect(cx, cy, cw, ch);

    Painter painter(ctx);
    Painter::ImageDrawParams params;
    params.image         = static_cast<NativeImage>(_win32->d2dBitmap.Get());
    params.srcWidth      = _win32->bitmapW;
    params.srcHeight     = _win32->bitmapH;
    params.clipX         = cx;
    params.clipY         = cy;
    params.clipW         = cw;
    params.clipH         = ch;
    params.destX         = d.x;
    params.destY         = d.y;
    params.destW         = d.w;
    params.destH         = d.h;
    params.borderRadius  = borderRadius;
    params.repeat        = repeat;
    params.filterQuality = filterQuality;

    painter.drawImage(params);
}

// ============================================================================
// _platformInvalidateCache  — no-op (D2D scales at draw time)
// ============================================================================

void ImageWidget::_platformInvalidateCache()
{
}

// ============================================================================
// _platformDestroy
// ============================================================================

void ImageWidget::_platformDestroy()
{
    _win32.reset();
}

#endif // _WIN32