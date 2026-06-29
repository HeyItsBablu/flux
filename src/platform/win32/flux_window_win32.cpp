// flux_window_win32.cpp
#ifdef _WIN32

#include "flux/flux_window.hpp"
#include "flux/flux_d3d_device.hpp"
#include "flux/flux_render_loop.hpp"
#include "flux/flux_http_platform.hpp"
#include "flux/flux_core.hpp" // FluxUI::getCurrentInstance(), getFontCache()
#include "flux/flux_debug_log.hpp"
#include <windowsx.h>
#include <cassert>
#include <cstdio>

// ============================================================================
// Internal helpers
// ============================================================================

static void dbg(const char *msg)
{
    OutputDebugStringA(msg);
}

// ============================================================================
// GraphicsContext / MeasureContext construction helpers
// ============================================================================

// Build a GraphicsContext from the D3DDevice.
// Called on the render thread before each paint.
static GraphicsContext makeContext(D3DDevice *dev)
{
    return GraphicsContext(
        dev->d2dContext.Get(),
        dev->dwriteFactory.Get(),
        dev->d2dFactory.Get(),
        &dev->brushCache);
}

// ============================================================================
// PlatformWindow::getMeasureContext  (Win32)
// Borrows the live D2D context — no resource acquisition.
// DWrite is thread-safe so this is safe to call from any thread.
// ============================================================================

GraphicsContext PlatformWindow::getMeasureContext() const
{
    if (!d3dDevice_ || !d3dDevice_->valid)
        return GraphicsContext{};
    return makeContext(d3dDevice_);
}

GraphicsContext PlatformWindow::getD2DContext() const
{
    return getMeasureContext();
}

// ============================================================================
// PlatformWindow::valid
// ============================================================================

bool PlatformWindow::valid() const
{
    return hwnd_ != nullptr && d3dDevice_ && d3dDevice_->valid;
}

// ============================================================================
// PlatformWindow::create
// ============================================================================

// flux_window_win32.cpp

bool PlatformWindow::create(const std::string &title, int width, int height,
                            AppInstance hInst, void *userData)
{
    fluxLog("[PlatformWindow::create] Step 1: entered");
    hInstance_ = hInst;

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance_;
    wc.lpszClassName = L"FluxUI";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.style = 0;
    RegisterClassExW(&wc);
    fluxLog("[PlatformWindow::create] Step 2: class registered");

    RECT r = {0, 0, width, height};
    AdjustWindowRectEx(&r, WS_OVERLAPPEDWINDOW, FALSE, 0);

    std::wstring titleW = toWideString(title);
    hwnd_ = CreateWindowExW(
        0, L"FluxUI", titleW.c_str(),
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT,
        r.right - r.left, r.bottom - r.top,
        nullptr, nullptr, hInstance_, userData);

    fluxLog("[PlatformWindow::create] Step 3: HWND=" +
            std::string(hwnd_ ? "valid" : "NULL"));

    if (!hwnd_)
    {
        fluxLog("[PlatformWindow::create] FATAL: CreateWindowExW failed");
        return false;
    }

    updateClientSize();
    fluxLog("[PlatformWindow::create] Step 4: client size=" +
            std::to_string(cachedWidth) + "x" + std::to_string(cachedHeight));

    fluxLog("[PlatformWindow::create] Step 5: creating D3DDevice");
    d3dDevice_ = new D3DDevice();
    if (!d3dDevice_->create(hwnd_, cachedWidth, cachedHeight))
    {
        fluxLog("[PlatformWindow::create] FATAL: D3DDevice::create failed");
        delete d3dDevice_;
        d3dDevice_ = nullptr;
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
        return false;
    }
    fluxLog("[PlatformWindow::create] Step 6: D3DDevice created");

    fluxLog("[PlatformWindow::create] Step 7: creating RenderLoop");
    renderLoop_ = new RenderLoop();

    renderLoop_->onResize = [this](int newW, int newH)
    {
        fluxLog("[onResize] " + std::to_string(newW) + "x" +
                std::to_string(newH));
        if (!d3dDevice_->resize(newW, newH))
        {
            if (!d3dDevice_->recover(hwnd_))
                fluxLog("[onResize] FATAL: recovery failed");
        }
        cachedWidth = newW;

        cachedHeight = newH;
        if (callbacks.onResize)
        {
            auto ctx = makeContext(d3dDevice_);
            callbacks.onResize(ctx, newW, newH);
        }
        fluxLog("[onResize] done");
    };

    renderLoop_->onLayout = [this]()
    {
        fluxLog("[onLayout] called");
        if (callbacks.onResize)
        {
            auto ctx = makeContext(d3dDevice_);
            callbacks.onResize(ctx, cachedWidth, cachedHeight);
        }
        fluxLog("[onLayout] done");
    };

    renderLoop_->onFrame = [this](float dt)
    {
        renderLoop_->drainPending();
        if (auto *ui = FluxUI::getCurrentInstance())
            ui->drainPendingRebuilds();
        fluxLog("[onFrame] START");
        if (!d3dDevice_ || !d3dDevice_->valid)
            return;

        d3dDevice_->beginDraw();
        fluxLog("[onFrame] beginDraw done, calling onPaint");

        if (callbacks.onPaint)
        {
            auto ctx = makeContext(d3dDevice_);
            callbacks.onPaint(ctx, cachedWidth, cachedHeight);
            fluxLog("[onFrame] onPaint returned"); // markDirty before or after this?
        }

        HRESULT hr = d3dDevice_->endDrawAndPresent(1);
        fluxLog("[onFrame] END present hr=0x" + [hr]()
                {
        char buf[16]; sprintf_s(buf, "%08X", (unsigned)hr); return std::string(buf); }());

        if (d3dDevice_->isDeviceLost(hr))
            d3dDevice_->recover(hwnd_);
    };

    // ── DO NOT start the render loop here anymore ─────────────────────────
    // FluxUI::createWindow will call startRenderLoop() after the initial
    // layout is complete, preventing the race condition where onLayout fires
    // while computeLayout is already running on the main thread.

    fluxSetUIWindow(hwnd_);
    fluxLog("[PlatformWindow::create] Step 8: done (render loop not started yet)");
    return true;
}

void PlatformWindow::startRenderLoop()
{
    if (!renderLoop_)
        return;
    fluxLog("[PlatformWindow::startRenderLoop] starting");
    renderLoop_->start();
    renderLoop_->markDirty(); // only paint, no layout — layout already done
    fluxLog("[PlatformWindow::startRenderLoop] done");
}

// ============================================================================
// PlatformWindow::destroy
// ============================================================================

void PlatformWindow::destroy()
{
    // Stop render thread first — it may be using d3dDevice_
    if (renderLoop_)
    {
        renderLoop_->stop();
        delete renderLoop_;
        renderLoop_ = nullptr;
    }

    if (d3dDevice_)
    {
        d3dDevice_->destroy();
        delete d3dDevice_;
        d3dDevice_ = nullptr;
    }

    if (hwnd_)
    {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }

    timerMap_.clear();
}

// ============================================================================
// PlatformWindow::run  — hybrid message pump
//
// The render thread handles painting independently. The message thread only
// needs to process input and system messages. WaitMessage() sleeps the thread
// until a message arrives, so idle CPU usage is zero.
// ============================================================================

int PlatformWindow::run()
{
    MSG msg = {};
    while (true)
    {
        // Drain all pending messages without blocking
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
                return static_cast<int>(msg.wParam);
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        // Sleep until the next message arrives — no busy spin
        WaitMessage();
    }
}

// ============================================================================
// PlatformWindow::invalidate / invalidateRect
// Signal the render loop — no longer calls InvalidateRect.
// ============================================================================

void PlatformWindow::invalidate()
{
    fluxLog("[PlatformWindow::invalidate] called");
    if (renderLoop_)
        renderLoop_->markDirty();
}

void PlatformWindow::invalidateRect(int x, int y, int w, int h)
{
    fluxLog("[PlatformWindow::invalidateRect] x=" + std::to_string(x) +
            " y=" + std::to_string(y) +
            " w=" + std::to_string(w) +
            " h=" + std::to_string(h));
    if (renderLoop_)
        renderLoop_->markDirty();
}

// ============================================================================
// PlatformWindow::setTimer / killTimer
// Route through the render loop so timers fire on the render thread with
// sub-frame accuracy (no Win32 15 ms floor).
// ============================================================================

void PlatformWindow::setTimer(TimerID id, int ms)
{
    if (!renderLoop_)
        return;

    // If this id already has a render-loop timer, remove it first.
    auto it = timerMap_.find(id);
    if (it != timerMap_.end())
    {
        renderLoop_->removeTimer(it->second);
        timerMap_.erase(it);
    }

    uint32_t rlId = renderLoop_->addTimer(ms, [this, id]()
                                          {
        if (callbacks.onTimer)
            callbacks.onTimer(id); });

    timerMap_[id] = rlId;
}

void PlatformWindow::killTimer(TimerID id)
{
    if (!renderLoop_)
        return;

    auto it = timerMap_.find(id);
    if (it != timerMap_.end())
    {
        renderLoop_->removeTimer(it->second);
        timerMap_.erase(it);
    }
}

// ============================================================================
// PlatformWindow::updateClientSize
// ============================================================================

void PlatformWindow::updateClientSize()
{
    if (!hwnd_)
        return;
    RECT rect;
    GetClientRect(hwnd_, &rect);
    cachedWidth = rect.right - rect.left;
    cachedHeight = rect.bottom - rect.top;
}

// ============================================================================
// Clipboard
// ============================================================================

void PlatformWindow::setClipboardText(const std::string &text)
{
    if (!hwnd_ || !OpenClipboard(hwnd_))
        return;
    EmptyClipboard();
    // Allocate global memory for the text
    HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, text.size() + 1);
    if (hg)
    {
        memcpy(GlobalLock(hg), text.c_str(), text.size() + 1);
        GlobalUnlock(hg);
        SetClipboardData(CF_TEXT, hg);
    }
    CloseClipboard();
}

std::string PlatformWindow::getClipboardText()
{
    if (!hwnd_ || !OpenClipboard(hwnd_))
        return "";
    HANDLE hd = GetClipboardData(CF_TEXT);
    if (!hd)
    {
        CloseClipboard();
        return "";
    }
    std::string text = static_cast<char *>(GlobalLock(hd));
    GlobalUnlock(hd);
    CloseClipboard();
    return text;
}

// ============================================================================
// Mouse capture / cursor
// ============================================================================

void PlatformWindow::captureMouseInput()
{
    if (hwnd_)
        SetCapture(hwnd_);
}
void PlatformWindow::releaseMouseInput() { ReleaseCapture(); }
bool PlatformWindow::isMouseCaptured() const { return GetCapture() == hwnd_; }

void PlatformWindow::setResizeCursorH() { SetCursor(LoadCursor(nullptr, IDC_SIZEWE)); }
void PlatformWindow::setResizeCursorV() { SetCursor(LoadCursor(nullptr, IDC_SIZENS)); }
void PlatformWindow::setDefaultCursor() { SetCursor(LoadCursor(nullptr, IDC_ARROW)); }

// ============================================================================
// Coordinate helpers
// ============================================================================

PlatformWindow::ScreenPoint PlatformWindow::clientToScreen(int cx, int cy) const
{
    POINT pt = {cx, cy};
    if (hwnd_)
        ClientToScreen(hwnd_, &pt);
    return {pt.x, pt.y};
}

PlatformWindow::ScreenPoint PlatformWindow::screenToClient(int sx, int sy) const
{
    POINT pt = {sx, sy};
    if (hwnd_)
        ScreenToClient(hwnd_, &pt);
    return {pt.x, pt.y};
}

PlatformWindow::ClientSize PlatformWindow::getClientSize() const
{
    return {cachedWidth, cachedHeight};
}

NativeWindow PlatformWindow::handle() const { return hwnd_; }

// ============================================================================
// WINDOW PROCEDURE
// ============================================================================

LRESULT CALLBACK PlatformWindow::WindowProc(HWND hwnd, UINT uMsg,
                                            WPARAM wParam, LPARAM lParam)
{
    PlatformWindow *self = reinterpret_cast<PlatformWindow *>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (uMsg)
    {
    // ── Setup ─────────────────────────────────────────────────────────────────
    case WM_CREATE:
    {
        auto *cs = reinterpret_cast<CREATESTRUCTW *>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        return 0;
    }

    // ── HTTP async results ────────────────────────────────────────────────────
    case WM_FLUX_HTTP_RESULT:
    {
        FluxHttp_Win32_HandleMessage(lParam);
        return 0;
    }

        // ── Paint ─────────────────────────────────────────────────────────────────
        // D2D renders on its own thread via RenderLoop. WM_PAINT just validates
        // the window so Windows doesn't keep re-posting it.
    case WM_PAINT:
    {
        fluxLog("[WM_PAINT] called");
        ValidateRect(hwnd, nullptr);
        return 0;
    }

        // ── Resize ────────────────────────────────────────────────────────────────
        // Forward new dimensions to the render loop — it will resize the swap
        // chain and re-run layout on the render thread.
    case WM_SIZE:
    {
        if (!self)
            return DefWindowProcW(hwnd, uMsg, wParam, lParam);
        if (wParam == SIZE_MINIMIZED)
            return 0;
        fluxLog("[WM_SIZE] called");
        self->updateClientSize();
        int w = self->cachedWidth;
        int h = self->cachedHeight;
        if (self->renderLoop_)
            self->renderLoop_->markResize(w, h);
        return 0;
    }

    // ── Mouse wheel ───────────────────────────────────────────────────────────
    case WM_MOUSEWHEEL:
    {
        if (!self)
            return 0;
        int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        if (self->callbacks.onMouseWheel && self->callbacks.onMouseWheel(delta))
            self->invalidate();
        return 0;
    }

    // ── Mouse buttons ─────────────────────────────────────────────────────────
    case WM_LBUTTONDOWN:
    {
        if (!self)
            return 0;
        int x = GET_X_LPARAM(lParam), y = GET_Y_LPARAM(lParam);
        if (self->callbacks.onMouseDown && self->callbacks.onMouseDown(x, y))
            self->invalidate();
        return 0;
    }

    case WM_RBUTTONDOWN:
    {
        if (!self)
            return 0;
        int x = GET_X_LPARAM(lParam), y = GET_Y_LPARAM(lParam);
        if (self->callbacks.onRightClick && self->callbacks.onRightClick(x, y))
            self->invalidate();
        return 0;
    }

    case WM_LBUTTONUP:
    {
        if (!self)
            return 0;
        int x = GET_X_LPARAM(lParam), y = GET_Y_LPARAM(lParam);
        if (self->callbacks.onMouseUp && self->callbacks.onMouseUp(x, y))
            self->invalidate();
        return 0;
    }

        // ── Mouse move ────────────────────────────────────────────────────────────
    case WM_MOUSEMOVE:
    {
        if (!self)
            return 0;
        int x = GET_X_LPARAM(lParam), y = GET_Y_LPARAM(lParam);
        fluxLog("[WM_MOUSEMOVE] x=" + std::to_string(x) + " y=" + std::to_string(y));

        TRACKMOUSEEVENT tme = {};
        tme.cbSize = sizeof(TRACKMOUSEEVENT);
        tme.dwFlags = TME_LEAVE;
        tme.hwndTrack = hwnd;
        TrackMouseEvent(&tme);

        if (self->callbacks.onMouseMove && self->callbacks.onMouseMove(x, y))
            self->invalidate();
        return 0;
    }

    case WM_MOUSELEAVE:
    {
        if (!self)
            return 0;
        if (self->callbacks.onMouseLeave)
            self->callbacks.onMouseLeave();
        self->invalidate();
        return 0;
    }

    // ── Keyboard ──────────────────────────────────────────────────────────────
    case WM_CHAR:
    {
        if (!self)
            return 0;
        wchar_t ch = static_cast<wchar_t>(wParam);
        if (self->callbacks.onChar && self->callbacks.onChar(ch))
            self->invalidate();
        return 0;
    }

    case WM_KEYDOWN:
    {
        if (!self)
            return 0;
        int keyCode = static_cast<int>(wParam);
        if (self->callbacks.onKeyDown && self->callbacks.onKeyDown(keyCode))
            self->invalidate();
        return 0;
    }

    // ── WM_TIMER ──────────────────────────────────────────────────────────────
    // Win32 timers are no longer used on the D2D path — timers go through
    // RenderLoop. This handler is kept as a safety net (e.g. CanvasWidget on
    // non-D2D paths may still post a WM_TIMER).
    case WM_TIMER:
    {
        if (!self)
            return 0;
        TimerID id = static_cast<TimerID>(wParam);
        if (self->callbacks.onTimer)
            self->callbacks.onTimer(id);
        return 0;
    }

    // ── Non-client mouse (focus loss) ─────────────────────────────────────────
    case WM_NCLBUTTONDOWN:
    {
        if (!self)
            return 0;
        if (self->callbacks.onNonClientMouseDown)
            self->callbacks.onNonClientMouseDown();
        return DefWindowProcW(hwnd, uMsg, wParam, lParam);
    }

    // ── Focus lost ────────────────────────────────────────────────────────────
    case WM_KILLFOCUS:
    {
        if (self && self->callbacks.onFocusLost)
            self->callbacks.onFocusLost();
        return 0;
    }

    // ── Destroy ───────────────────────────────────────────────────────────────
    case WM_DESTROY:
    {
        fluxDrainPendingMessages(hwnd);
        PostQuitMessage(0);
        return 0;
    }
    }

    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

#endif // _WIN32