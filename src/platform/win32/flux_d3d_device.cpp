// flux_d3d_device.cpp
#ifdef _WIN32

#include "flux/flux_d3d_device.hpp"
#include "flux/flux_platform.hpp"   // Color

#include <d3d11_1.h>
#include <dxgi1_2.h>
#include <d2d1_1.h>
#include <dwrite_3.h>

#include <cstdio>

// ============================================================================
// Internal logging helper
// ============================================================================

static void logHR(const char* where, HRESULT hr)
{
    if (FAILED(hr))
    {
        char buf[256];
        _snprintf_s(buf, sizeof(buf), _TRUNCATE,
                    "[D3DDevice] FAILED %s  hr=0x%08X\n", where, (unsigned)hr);
        OutputDebugStringA(buf);
    }
}

#define CHECK(expr)  do { HRESULT _hr = (expr); if (FAILED(_hr)) { logHR(#expr, _hr); return false; } } while(0)

// ============================================================================
// BrushCache::get
// ============================================================================

ID2D1SolidColorBrush* BrushCache::get(ID2D1DeviceContext1* dc, Color c)
{
    uint32_t key = uint32_t(c.r)
                 | uint32_t(c.g) << 8
                 | uint32_t(c.b) << 16
                 | uint32_t(c.a) << 24;

    auto it = brushes.find(key);
    if (it != brushes.end())
        return it->second.Get();

    ComPtr<ID2D1SolidColorBrush> brush;
    D2D1_COLOR_F cf = D2D1::ColorF(
        c.r / 255.f, c.g / 255.f, c.b / 255.f, c.a / 255.f);
    HRESULT hr = dc->CreateSolidColorBrush(cf, &brush);
    if (FAILED(hr))
    {
        logHR("BrushCache::CreateSolidColorBrush", hr);
        return nullptr;
    }

    brushes[key] = brush;
    return brush.Get();
}

// ============================================================================
// D3DDevice::create
// ============================================================================

bool D3DDevice::create(HWND hwnd, int w, int h)
{
    hwnd_  = hwnd;
    width  = w;
    height = h;
    valid  = false;

    if (!createDeviceResources(hwnd, w, h))
        return false;

    valid = true;
    return true;
}

// ============================================================================
// D3DDevice::createDeviceResources
// All resources that survive resize.
// ============================================================================

bool D3DDevice::createDeviceResources(HWND hwnd, int w, int h)
{
    // ── D3D11 device (feature level 11.0 with fallback to 10.x / 9.x) ────────

    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT; // required for D2D interop
// #ifndef NDEBUG
//     flags |= D3D11_CREATE_DEVICE_DEBUG;
// #endif

    D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
    };
    D3D_FEATURE_LEVEL achievedLevel = {};

    ComPtr<ID3D11Device>        rawDevice;
    ComPtr<ID3D11DeviceContext> rawContext;

    CHECK(D3D11CreateDevice(
        nullptr,                    // default adapter
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        flags,
        featureLevels, ARRAYSIZE(featureLevels),
        D3D11_SDK_VERSION,
        &rawDevice, &achievedLevel, &rawContext));

    CHECK(rawDevice.As(&d3dDevice));
    CHECK(rawContext.As(&d3dContext));

    {
        char buf[128];
        _snprintf_s(buf, sizeof(buf), _TRUNCATE,
                    "[D3DDevice] D3D feature level: 0x%04X\n", achievedLevel);
        OutputDebugStringA(buf);
    }

    // ── DXGI factory (from the device's adapter) ──────────────────────────────

    ComPtr<IDXGIDevice1> dxgiDevice;
    CHECK(d3dDevice.As(&dxgiDevice));

    // Keep at most 1 frame queued — reduces input latency.
    dxgiDevice->SetMaximumFrameLatency(1);

    ComPtr<IDXGIAdapter> adapter;
    CHECK(dxgiDevice->GetAdapter(&adapter));

    ComPtr<IDXGIFactory2> factory;
    CHECK(adapter->GetParent(IID_PPV_ARGS(&factory)));

    // ── Swap chain ────────────────────────────────────────────────────────────

    DXGI_SWAP_CHAIN_DESC1 scd = {};
    scd.Width              = (UINT)w;
    scd.Height             = (UINT)h;
    scd.Format             = DXGI_FORMAT_B8G8R8A8_UNORM;  // D2D prefers BGRA
    scd.SampleDesc.Count   = 1;
    scd.SampleDesc.Quality = 0;
    scd.BufferUsage        = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.BufferCount        = 2;                            // double-buffered
    scd.SwapEffect         = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    scd.Flags              = 0;

    CHECK(factory->CreateSwapChainForHwnd(
        d3dDevice.Get(), hwnd, &scd,
        nullptr, nullptr,           // no fullscreen desc, no output restriction
        &swapChain));

    // Disable DXGI's alt-enter fullscreen shortcut (we manage that ourselves).
    factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);

    // ── D2D factory ───────────────────────────────────────────────────────────

    D2D1_FACTORY_OPTIONS opts = {};
// #ifndef NDEBUG
//     opts.debugLevel = D2D1_DEBUG_LEVEL_INFORMATION;
// #endif

    CHECK(D2D1CreateFactory(
        D2D1_FACTORY_TYPE_SINGLE_THREADED,   // render thread only
        __uuidof(ID2D1Factory1),
        &opts,
        reinterpret_cast<void**>(d2dFactory.GetAddressOf())));

    // ── D2D device (wraps the D3D device) ────────────────────────────────────

    CHECK(d2dFactory->CreateDevice(dxgiDevice.Get(), &d2dDevice));

    // ── D2D device context ────────────────────────────────────────────────────

    {
        ComPtr<ID2D1DeviceContext> dc0;
        CHECK(d2dDevice->CreateDeviceContext(
            D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &dc0));
        CHECK(dc0.As(&d2dContext));
    }

    // Enable sub-pixel AA for text on opaque surfaces.
    d2dContext->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);

    // ── DWrite factory ────────────────────────────────────────────────────────
    // IDWriteFactory3 is thread-safe — it can be shared with any thread.

    CHECK(DWriteCreateFactory(
        DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory3),
        reinterpret_cast<IUnknown**>(dwriteFactory.GetAddressOf())));

    // ── Size-dependent resources (back-buffer bitmap) ─────────────────────────

    return createSizeDependentResources(w, h);
}

// ============================================================================
// D3DDevice::createSizeDependentResources
// Back-buffer bitmap — must be recreated on every resize.
// ============================================================================

bool D3DDevice::createSizeDependentResources(int w, int h)
{
    // Release old target before resizing.
    d2dContext->SetTarget(nullptr);
    d2dTarget.Reset();
    dxgiSurface.Reset();

    // If the swap chain already exists, resize its buffers.
    if (swapChain && (w != width || h != height))
    {
        HRESULT hr = swapChain->ResizeBuffers(
            0,                         // keep buffer count
            (UINT)w, (UINT)h,
            DXGI_FORMAT_UNKNOWN,       // keep format
            0);

        if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
        {
            logHR("ResizeBuffers", hr);
            return false;  // caller must handle device loss
        }
        CHECK(hr);
    }

    width  = w;
    height = h;

    // Get the back buffer as a DXGI surface.
    CHECK(swapChain->GetBuffer(0, IID_PPV_ARGS(&dxgiSurface)));

    // Wrap it as a D2D bitmap that we can use as a render target.
    float dpi = 96.f; // physical pixel units — widgets already work in px
    D2D1_BITMAP_PROPERTIES1 bmpProps = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                          D2D1_ALPHA_MODE_IGNORE),
        dpi, dpi);

    CHECK(d2dContext->CreateBitmapFromDxgiSurface(
        dxgiSurface.Get(), &bmpProps, &d2dTarget));

    d2dContext->SetTarget(d2dTarget.Get());

    // Flush brush cache — brushes are device-context bound.
    brushCache.flush();

    return true;
}

// ============================================================================
// D3DDevice::releaseSizeDependentResources
// ============================================================================

void D3DDevice::releaseSizeDependentResources()
{
    d2dContext->SetTarget(nullptr);
    d2dTarget.Reset();
    dxgiSurface.Reset();
    brushCache.flush();
}

// ============================================================================
// D3DDevice::resize
// ============================================================================

bool D3DDevice::resize(int newW, int newH)
{
    if (!valid || (newW == width && newH == height))
        return true;

    // D3D context must be flushed before ResizeBuffers.
    d3dContext->ClearState();
    d3dContext->Flush();

    return createSizeDependentResources(newW, newH);
}

// ============================================================================
// D3DDevice::destroy
// ============================================================================

void D3DDevice::destroy()
{
    valid = false;

    releaseSizeDependentResources();

    dwriteFactory.Reset();
    d2dContext.Reset();
    d2dDevice.Reset();
    d2dFactory.Reset();
    swapChain.Reset();

    if (d3dContext)
    {
        d3dContext->ClearState();
        d3dContext->Flush();
    }
    d3dContext.Reset();
    d3dDevice.Reset();
}

// ============================================================================
// D3DDevice::beginDraw
// ============================================================================

void D3DDevice::beginDraw()
{
    d2dContext->BeginDraw();

    // Clear to a neutral background each frame.
    // Widgets paint their own backgrounds, so this only shows through on
    // areas the layout engine leaves uncovered (shouldn't happen, but safe).
    d2dContext->Clear(D2D1::ColorF(D2D1::ColorF::White));
}

// ============================================================================
// D3DDevice::endDrawAndPresent
// ============================================================================

HRESULT D3DDevice::endDrawAndPresent(UINT syncInterval)
{
    HRESULT hr = d2dContext->EndDraw();

    if (hr == D2DERR_RECREATE_TARGET)
    {
        // Device lost via D2D path — mark invalid, caller handles recovery.
        valid = false;
        return hr;
    }

    logHR("EndDraw", hr);
    if (FAILED(hr))
        return hr;

    // Present the frame.
    // syncInterval 1 = wait for VSync, 0 = immediate (tearing possible).
    DXGI_PRESENT_PARAMETERS pp = {};
    hr = swapChain->Present1(syncInterval, 0, &pp);

    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
    {
        valid = false;
        logHR("Present1", hr);
    }

    return hr;
}

// ============================================================================
// D3DDevice::isDeviceLost
// ============================================================================

bool D3DDevice::isDeviceLost(HRESULT hr) const
{
    return hr == DXGI_ERROR_DEVICE_REMOVED
        || hr == DXGI_ERROR_DEVICE_RESET
        || hr == D2DERR_RECREATE_TARGET;
}

// ============================================================================
// D3DDevice::recover
// ============================================================================

bool D3DDevice::recover(HWND hwnd)
{
    OutputDebugStringA("[D3DDevice] Recovering from device loss...\n");
    destroy();
    return create(hwnd, width, height);
}

#undef CHECK

#endif // _WIN32