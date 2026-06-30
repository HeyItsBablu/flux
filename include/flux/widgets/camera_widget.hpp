// camera_widget.hpp
// Cross-platform camera viewfinder widget for FluxUI.
//
// Single class — platform separation mirrors flux_image.hpp:
//   platform state lives in nested structs (Android/Win32/Linux/macOS)
//   platform methods are declared here, implemented in:
//       camera_widget_android.cpp  — GLES2
//       camera_widget_win32.cpp    — GDI StretchDIBits / WIC
//       camera_widget_linux.cpp    — Cairo / libjpeg
//       camera_widget_macos.mm     — CoreGraphics / AVFoundation
//
// Controls:
//   - Shutter button (center bottom) — tap/click to capture photo
//   - Flash toggle  (left bottom)    — on/off (no-op on Linux/Win32 webcams)
//   - Flip button   (right bottom)   — front/back (Android/macOS) or next /dev/videoN
//   - Flash overlay — brief white flash on capture
//   - Thumbnail     — last photo shown in bottom-left corner
//
// Usage:
//   CameraView()
//       ->setWidth(380)->setHeight(270)
//       ->setOnPhoto([](const std::string& path) { ... })
//
#pragma once

#include "flux/flux.hpp"
#include "flux/flux_camera.hpp"

// ── Platform-specific system headers (kept isolated) ─────────────────────────

#ifdef __ANDROID__
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#endif // __ANDROID__

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
#endif // _WIN32

#if defined(__linux__) && !defined(__ANDROID__)
#include <cairo/cairo.h>
#include <jpeglib.h>
#include <setjmp.h>
#endif // Linux

// ============================================================================
// CameraWidget
// ============================================================================

class CameraWidget : public Widget
{
public:
    // ── Config ────────────────────────────────────────────────────────────────
    int barHeight = 56;
    bool startFront = false;

    // ── Colors ────────────────────────────────────────────────────────────────
    Color colBar = Color::fromRGBA(15, 15, 15, 210);
    Color colShutter = Color::fromRGB(255, 255, 255);
    Color colShutterRing = Color::fromRGBA(255, 255, 255, 180);
    Color colShutterHov = Color::fromRGBA(220, 220, 220, 255);
    Color colIcon = Color::fromRGB(220, 220, 220);
    Color colIconHov = Color::fromRGB(255, 255, 255);
    Color colIconActive = Color::fromRGB(255, 210, 60);
    Color colFlash = Color::fromRGBA(255, 255, 255, 200);
    Color colPlaceholder = Color::fromRGB(20, 20, 20);
    Color colThumbBorder = Color::fromRGB(255, 255, 255);

    // ── Fluent setters ────────────────────────────────────────────────────────
    std::shared_ptr<CameraWidget> setWidth(int w)
    {
        Widget::width = w;
        autoWidth = false;
        return self();
    }
    std::shared_ptr<CameraWidget> setHeight(int h)
    {
        Widget::height = h;
        autoHeight = false;
        return self();
    }
    std::shared_ptr<CameraWidget> setOnPhoto(
        std::function<void(const std::string &)> cb)
    {
        _onPhoto = std::move(cb);
        return self();
    }
    std::shared_ptr<CameraWidget> setStartFront(bool f)
    {
        startFront = f;
        return self();
    }

    // ── Constructor / destructor ───────────────────────────────────────────────
    CameraWidget()
    {
        autoWidth = false;
        autoHeight = false;
        width = 380;
        height = 270;

        FluxCamera::get().setOnPhoto([this](const std::string &path)
                                     {
            _lastPhotoPath = path;
            _thumbDirty    = true;
            markNeedsPaint();
            if (_onPhoto) _onPhoto(path); });
    }

    ~CameraWidget() override
    {
        _stopTimer();
        FluxCamera::get().close();
        _platformDestroy();
    }

    // =========================================================================
    // Layout
    // =========================================================================

    void computeLayout(GraphicsContext & /*ctx*/,
                       const BoxConstraints &constraints,
                       FontCache & /*fontCache*/) override
    {
        if (autoWidth)
            width = constraints.maxWidth;
        if (autoHeight)
            height = constraints.maxHeight;
        applyConstraints();
        needsLayout = false;

        if (!_opened)
        {
            _opened = true;
            _platformScheduleOpen(); // platform decides when/how to open
        }
    }

    // =========================================================================
    // Render
    // =========================================================================

    void render(GraphicsContext &ctx, FontCache &fontCache) override
    {
        // Deferred open flag set by platform (e.g. after permission granted)
        if (_shouldOpen)
        {
            _shouldOpen = false;
            FluxCamera::get().open(startFront);
            _startTimer();
        }

        auto &cam = FluxCamera::get();
        Painter p(ctx);
        int viewH = height - barHeight;

        // Load thumbnail when a new photo arrives
        if (_thumbDirty && !_lastPhotoPath.empty())
        {
            _thumbDirty = false;
            _platformLoadThumb(_lastPhotoPath);
        }

        // Preview — delegates all pixel work to the platform implementation
        if (!_platformRenderPreview(ctx, p, fontCache, viewH))
        {
            // Placeholder while camera opens
            p.fillRect(x, y, width, viewH, colPlaceholder);
            NativeFont tf = fontCache.getFont(
#ifdef _WIN32
                "Segoe UI",
#elif defined(__ANDROID__)
                "Roboto",
#else
                "Sans",
#endif
                13, FontWeight::Normal);
            p.drawText(toWideString("Opening camera..."),
                       x, y, width, viewH, tf,
                       Color::fromRGB(120, 120, 120),
                       DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }

        // Flash overlay
        if (_flashAlpha > 0.f)
        {
            _platformRenderFlash(ctx, p, viewH);
            _flashAlpha -= 0.15f;
            if (_flashAlpha < 0.f)
                _flashAlpha = 0.f;
            markNeedsPaint();
        }

        // Thumbnail (bottom-left corner)
        if (!_lastPhotoPath.empty())
        {
            constexpr int thumbW = 44, thumbH = 44;
            int thumbX = x + 8;
            int thumbY = y + viewH - thumbH - 8;

            p.fillRect(thumbX - 2, thumbY - 2,
                       thumbW + 4, thumbH + 4, colThumbBorder);

            if (!_platformRenderThumb(ctx, thumbX, thumbY, thumbW, thumbH))
            {
                p.fillRect(thumbX, thumbY, thumbW, thumbH,
                           Color::fromRGB(60, 60, 60));
            }
        }

        // Control bar
        int barY = y + viewH;
        p.fillRect(x, barY, width, barHeight, colBar);
        int midY = barY + barHeight / 2;

        // Flash button (left)
        constexpr int iconR = 16;
        int flashCx = x + 36;
        _flashBtnRect = {flashCx - iconR, barY + (barHeight - iconR * 2) / 2,
                         iconR * 2, iconR * 2};
        Color flashCol = _hovFlash
                             ? colIconHov
                             : (cam.isFlashOn() ? colIconActive : colIcon);
        _drawFlashIcon(p, flashCx, midY, 12, flashCol);

        // Shutter button (center)
        int shutterR = 22;
        int shutterCx = x + width / 2;
        _shutterRect = {shutterCx - shutterR, barY + (barHeight - shutterR * 2) / 2,
                        shutterR * 2, shutterR * 2};
        p.drawEllipse(shutterCx - shutterR - 3,
                      midY - shutterR - 3,
                      (shutterR + 3) * 2, (shutterR + 3) * 2,
                      Color::fromRGBA(0, 0, 0, 0), colShutterRing, 2);
        Color sc = cam.isCapturing()
                       ? Color::fromRGB(200, 200, 200)
                       : (_hovShutter ? colShutterHov : colShutter);
        p.drawEllipse(shutterCx - shutterR, midY - shutterR,
                      shutterR * 2, shutterR * 2, sc, sc, 0);

        // Flip button (right)
        int flipCx = x + width - 36;
        _flipBtnRect = {flipCx - iconR, barY + (barHeight - iconR * 2) / 2,
                        iconR * 2, iconR * 2};
        Color flipCol = _hovFlip ? colIconHov : colIcon;
        _drawFlipIcon(p, flipCx, midY, 12, flipCol);

        needsPaint = false;
    }

    // =========================================================================
    // Mouse / touch events
    // =========================================================================

    bool handleMouseDown(int mx, int my) override
    {
        if (!_inWidget(mx, my))
            return false;

        if (_inRect(mx, my, _shutterRect))
        {
            _triggerCapture();
            return true;
        }
        if (_inRect(mx, my, _flashBtnRect))
        {
            FluxCamera::get().toggleFlash();
            markNeedsPaint();
            return true;
        }
        if (_inRect(mx, my, _flipBtnRect))
        {
            _platformOnFlip();
            FluxCamera::get().flipCamera();
            _startTimer();
            markNeedsPaint();
            return true;
        }
        return false;
    }

    bool handleMouseMove(int mx, int my) override
    {
        bool hs = _inRect(mx, my, _shutterRect);
        bool hf = _inRect(mx, my, _flashBtnRect);
        bool hfl = _inRect(mx, my, _flipBtnRect);
        if (hs != _hovShutter || hf != _hovFlash || hfl != _hovFlip)
        {
            _hovShutter = hs;
            _hovFlash = hf;
            _hovFlip = hfl;
            markNeedsPaint();
        }
        return false;
    }

    bool handleMouseLeave() override
    {
        _hovShutter = _hovFlash = _hovFlip = false;
        markNeedsPaint();
        return true;
    }

    // =========================================================================
    // Platform state — public so platform .cpp files can reference the types.
    // Treat as an implementation detail; do not use outside camera_widget_*.cpp.
    // =========================================================================

#ifdef __ANDROID__
    // ── Android: raw GLES2 ──────────────────────────────────────
    struct AndroidState
    {
        // OES preview → rotated GL_TEXTURE_2D, blitted once per frame
        GLuint fbo = 0;
        GLuint fboTex = 0;
        int fboW = 0, fboH = 0; // already portrait (sensorH x sensorW)

        GLuint blitProgram = 0;
        GLuint blitVBO = 0;
        bool glResourcesReady = false;

        // Thumbnail: decoded JPEG → GL_TEXTURE_2D
        GLuint thumbTex = 0;
        int thumbW = 0, thumbH = 0;
    };
    AndroidState _android;

    void _initGLBlitPipeline();
    void _rebuildFBO(int sensorW, int sensorH);
    void _blitOESToFBO(GLuint oesTexId);
#endif // __ANDROID__

#ifdef _WIN32
    struct Win32State
    {
        std::vector<uint8_t> frameCache; // BGRA32, top-down
        int cachedSrcW = 0;
        int cachedSrcH = 0;

        Microsoft::WRL::ComPtr<ID2D1Bitmap> d2dBitmap;
        int d2dBitmapW = 0;
        int d2dBitmapH = 0;

        std::vector<uint8_t> thumbCache;
        BITMAPINFO thumbBmi{};
        int thumbSrcW = 0;
        int thumbSrcH = 0;

        Microsoft::WRL::ComPtr<ID2D1Bitmap> thumbD2dBitmap;
        int thumbD2dW = 0;
        int thumbD2dH = 0;
    };
    struct Win32StateDeleter
    {
        void operator()(Win32State *) const;
    };
    std::unique_ptr<Win32State, Win32StateDeleter> _win32;
#endif // _WIN32

#if defined(__linux__) && !defined(__ANDROID__)
    // ── Linux: Cairo surfaces + BGRX frame cache ──────────────────────────
    struct LinuxState
    {
        std::vector<uint8_t> bgrxCache;
        int cachedSrcW = 0;
        int cachedSrcH = 0;

        cairo_surface_t *previewSurf = nullptr;
        int cairoSurfW = 0;
        int cairoSurfH = 0;

        cairo_surface_t *thumbSurf = nullptr;
        std::vector<uint8_t> thumbBgrx; // backing store for thumbSurf
        int thumbSrcW = 0;
        int thumbSrcH = 0;
    };
    struct LinuxStateDeleter
    {
        void operator()(LinuxState *) const;
    };
    std::unique_ptr<LinuxState, LinuxStateDeleter> _linux;
#endif // Linux

#if defined(__APPLE__) && !defined(__ANDROID__)
    struct MacOSState
    {
        void *previewTexture = nullptr; // id<MTLTexture>, retained via __bridge_retained in the .mm
        int cachedSrcW = 0;
        int cachedSrcH = 0;

        void *thumbTexture = nullptr;   // id<MTLTexture>, retained via __bridge_retained
        int thumbSrcW = 0;
        int thumbSrcH = 0;
    };
    struct MacOSStateDeleter
    {
        void operator()(MacOSState *) const;
    };
    std::unique_ptr<MacOSState, MacOSStateDeleter> _macos;
#endif // macOS

private:
    // ── Shared state ─────────────────────────────────────────────────────────
    bool _opened = false;
    bool _shouldOpen = false;
    bool _thumbDirty = false;
    std::string _lastPhotoPath;
    float _flashAlpha = 0.f;
    bool _hovShutter = false;
    bool _hovFlash = false;
    bool _hovFlip = false;

    std::function<void(const std::string &)> _onPhoto;

    // ── Hit rects ─────────────────────────────────────────────────────────────
    struct Rect
    {
        int x, y, w, h;
    };
    Rect _shutterRect{};
    Rect _flashBtnRect{};
    Rect _flipBtnRect{};

    // ── Timers ────────────────────────────────────────────────────────────────
    TimerID _frameTimer = 0;
    TimerID _permCheckTimer = 0; // Android only; unused on other platforms

    // =========================================================================
    // Platform interface — implemented in camera_widget_{android,win32,linux,macos}.cpp/.mm
    // =========================================================================

    // Called from computeLayout on first layout pass.
    // Android: starts permission-check timer and sets _shouldOpen via callback.
    // Win32/Linux: sets _shouldOpen = true immediately.
    void _platformScheduleOpen();

    // Called when the flip button is hit, before FluxCamera::flipCamera().
    // Each platform clears its stale frame/texture state here.
    void _platformOnFlip();

    // Draw the camera preview into the viewfinder area (x, y, width, viewH).
    // Returns true if a frame was rendered; false triggers the placeholder text.
    bool _platformRenderPreview(GraphicsContext &ctx, Painter &p,
                                FontCache &fontCache, int viewH);

    // Draw the white flash overlay with the current _flashAlpha.
    void _platformRenderFlash(GraphicsContext &ctx, Painter &p, int viewH);

    // Draw the thumbnail at (thumbX, thumbY, thumbW, thumbH).
    // Returns true if drawn; false causes the caller to render a grey rect.
    bool _platformRenderThumb(GraphicsContext &ctx,
                              int thumbX, int thumbY,
                              int thumbW, int thumbH);

    // Decode the photo at path into the platform thumbnail cache.
    void _platformLoadThumb(const std::string &path);

    // Release all platform GPU / GDI / Cairo resources.
    // Called from ~CameraWidget before FluxCamera::close().
    void _platformDestroy();

    // =========================================================================
    // Shared helpers
    // =========================================================================

    void _startTimer()
    {
        if (_frameTimer)
            return;
        _frameTimer = FluxUI::getCurrentInstance()->setInterval(33, [this]()
                                                                {
            if (FluxCamera::get().isPreviewing() ||
                FluxCamera::get().isCapturing())
                markNeedsPaint(); });
    }

    void _stopTimer()
    {
        auto *ui = FluxUI::getCurrentInstance();
        if (!ui)
            return;
        if (_frameTimer)
        {
            ui->clearInterval(_frameTimer);
            _frameTimer = 0;
        }
        if (_permCheckTimer)
        {
            ui->clearInterval(_permCheckTimer);
            _permCheckTimer = 0;
        }
    }

    void _triggerCapture()
    {
        if (!FluxCamera::get().isPreviewing())
            return;
        _flashAlpha = 1.f;
        FluxCamera::get().capturePhoto();
        markNeedsPaint();
    }

    static void _drawFlashIcon(Painter &p, int cx, int cy, int sz, Color col)
    {
        p.fillRect(cx - 2, cy - sz, 4, sz * 2, col);
        p.fillRect(cx - sz / 2, cy - 2, sz, 4, col);
    }

    static void _drawFlipIcon(Painter &p, int cx, int cy, int sz, Color col)
    {
        p.fillRect(cx - sz, cy - 2, sz * 2, 4, col);
        p.fillRect(cx - sz, cy - sz / 2, 4, sz, col);
        p.fillRect(cx + sz - 4, cy - sz / 2, 4, sz, col);
    }

    std::shared_ptr<CameraWidget> self()
    {
        return std::static_pointer_cast<CameraWidget>(shared_from_this());
    }
    bool _inWidget(int mx, int my) const
    {
        return mx >= x && mx < x + width && my >= y && my < y + height;
    }
    static bool _inRect(int mx, int my, const Rect &r)
    {
        return mx >= r.x && mx < r.x + r.w && my >= r.y && my < r.y + r.h;
    }
};

using CameraWidgetPtr = std::shared_ptr<CameraWidget>;

inline CameraWidgetPtr CameraView()
{
    return std::make_shared<CameraWidget>();
}