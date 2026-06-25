#pragma once
#ifdef _WIN32

// ============================================================================
// flux_d3d_device.hpp
//
// Owns the entire GPU pipeline for the Win32 D2D renderer:
//   D3D11Device → IDXGISwapChain1 → ID2D1DeviceContext1 + IDWriteFactory3
//
// One instance per top-level window. Created by PlatformWindow::create(),
// destroyed by PlatformWindow::destroy().
//
// Thread safety:
//   - d3dDevice, dxgiSwapChain, d2dDevice: call on render thread only
//   - dwriteFactory:                        thread-safe (per MSDN)
//   - d2dFactory:                           thread-safe for geometry creation
// ============================================================================

#include <d3d11_1.h>
#include <dxgi1_2.h>
#include <d2d1_3.h>
#include <d2d1_1helper.h>
#include <dwrite_3.h>
#include <wrl/client.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

using Microsoft::WRL::ComPtr;

// ============================================================================
// BrushCache
//
// ID2D1SolidColorBrush creation is not free. We cache one brush per RGBA
// value and reuse across frames. The cache lives on D3DDevice so it shares
// the device lifetime and is flushed on resize/device-loss.
// ============================================================================

#include <unordered_map>
#include <cstdint>

// Color is defined in flux_platform.hpp; forward-declare to avoid including
// the whole platform header here. The .cpp includes both.
struct Color;

struct BrushCache
{
    // Key: pack RGBA into a uint32_t  (r | g<<8 | b<<16 | a<<24)
    std::unordered_map<uint32_t, ComPtr<ID2D1SolidColorBrush>> brushes;

    // Retrieve or create a brush for the given color.
    // dc must be the currently-active render target.
    ID2D1SolidColorBrush* get(ID2D1DeviceContext1* dc, Color c);

    // Call after device loss or swap-chain resize — all brushes are device-bound.
    void flush() { brushes.clear(); }
};

// ============================================================================
// D3DDevice
// ============================================================================

struct D3DDevice
{
    // ── D3D / DXGI ───────────────────────────────────────────────────────────
    ComPtr<ID3D11Device1>           d3dDevice;
    ComPtr<ID3D11DeviceContext1>    d3dContext;
    ComPtr<IDXGISwapChain1>         swapChain;
    ComPtr<IDXGISurface>            dxgiSurface;   // back-buffer surface

    // ── D2D ──────────────────────────────────────────────────────────────────
    ComPtr<ID2D1Factory1>           d2dFactory;
    ComPtr<ID2D1Device>             d2dDevice;
    ComPtr<ID2D1DeviceContext1>     d2dContext;    // the render target
    ComPtr<ID2D1Bitmap1>            d2dTarget;     // bitmap wrapping the back buffer

    // ── DWrite ───────────────────────────────────────────────────────────────
    ComPtr<IDWriteFactory3>         dwriteFactory;

    // ── Brush cache ───────────────────────────────────────────────────────────
    BrushCache                      brushCache;

    // ── State ─────────────────────────────────────────────────────────────────
    int   width  = 0;
    int   height = 0;
    bool  valid  = false;

    // ── Lifecycle ─────────────────────────────────────────────────────────────

    // Create all GPU resources for the given window handle.
    // Returns false and logs via OutputDebugStringA on failure.
    bool create(HWND hwnd, int w, int h);

    // Resize the swap chain and recreate the D2D render target.
    // Call from the render thread after WM_SIZE is processed.
    bool resize(int newW, int newH);

    // Release all COM objects. Safe to call multiple times.
    void destroy();

    // ── Per-frame helpers ─────────────────────────────────────────────────────

    // Begin a D2D frame. Call before any painting.
    void beginDraw();

    // End the D2D frame and present.
    // syncInterval: 0 = immediate (no vsync), 1 = vsync.
    HRESULT endDrawAndPresent(UINT syncInterval = 1);

    // ── Utilities ─────────────────────────────────────────────────────────────

    // True if the device has been lost (DXGI_ERROR_DEVICE_REMOVED / RESET).
    bool isDeviceLost(HRESULT hr) const;

    // Attempt to recreate all resources after device loss.
    bool recover(HWND hwnd);

private:
    HWND hwnd_ = nullptr;

    bool createDeviceResources(HWND hwnd, int w, int h);
    bool createSizeDependentResources(int w, int h);
    void releaseSizeDependentResources();
};

#endif // _WIN32
