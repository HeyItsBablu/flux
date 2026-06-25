// flux_videoplayer.hpp
// Self-contained video player widget for FluxUI.
// Single class shared across Android, Windows, Linux, macOS, and Web
// (Emscripten).
// Platform differences are isolated to render(), private frame-storage
// members, and a few small helpers — everything else is written once.

// Usage (path):
//   VideoPlayer("video/sample.mp4")->setWidth(480)->setHeight(270)
//
// Usage (URL):
//   VideoPlayerFromUrl("https://example.com/video.mp4")->setWidth(480)->setHeight(270)
//
// Usage (memory):
//   VideoPlayerFromMemory(bytes)->setWidth(480)->setHeight(270)
//   VideoPlayerFromMemory(ptr, len)->setWidth(480)->setHeight(270)
//
#pragma once

#include "../flux_http.hpp"

// ── Platform headers ──────────────────────────────────────────────────────────

#ifdef __ANDROID__
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include "flux/flux.hpp"
#include "flux/flux_video.hpp"
#include "flux_icons.hpp"
#include "nanovg.h"
extern NVGcontext *FluxAndroid_getVG();
extern float FluxAndroid_getDpiScale();
#elif defined(__APPLE__)
#include <TargetConditionals.h>
#if TARGET_OS_OSX
#include "flux/flux.hpp"
#include "flux/flux_video.hpp"
#include "flux_icons.hpp"
#include <CoreGraphics/CoreGraphics.h>
#endif
#elif defined(_WIN32)
#include "flux/flux.hpp"
#include "flux/flux_video.hpp"
#include "flux_icons.hpp"
#include <wrl/client.h>
#elif defined(__linux__)
#include "flux/flux.hpp"
#include "flux/flux_video.hpp"
#include "flux_icons.hpp"
#include <cairo/cairo.h>
#elif defined(__EMSCRIPTEN__)
#include "flux/flux.hpp"
#include "flux/flux_video.hpp"
#include "flux_icons.hpp"
#endif

// ============================================================================
// Shared utilities
// ============================================================================

enum class VideoSourceType
{
    None,
    Path,
    Url,
    Memory
};

// Magic-byte container detection — returns ".mp4", ".mov", ".avi", ".mkv", ".flv"
inline std::string VP_detectVideoExtension(const std::vector<uint8_t> &bytes)
{
    if (bytes.size() >= 12)
    {
        if (bytes[4] == 'f' && bytes[5] == 't' && bytes[6] == 'y' && bytes[7] == 'p')
            return ".mp4";
        if (bytes[4] == 'm' && bytes[5] == 'o' && bytes[6] == 'o' && bytes[7] == 'v')
            return ".mov";
    }
    if (bytes.size() >= 4)
    {
        if (bytes[0] == 'R' && bytes[1] == 'I' && bytes[2] == 'F' && bytes[3] == 'F')
            return ".avi";
        if (bytes[0] == 0x1A && bytes[1] == 0x45 && bytes[2] == 0xDF && bytes[3] == 0xA3)
            return ".mkv";
    }
    if (bytes.size() >= 3)
    {
        if (bytes[0] == 'F' && bytes[1] == 'L' && bytes[2] == 'V')
            return ".flv";
    }
    return ".mp4";
}

// Write bytes to a platform temp file with the given extension.
// Returns the final path on success, empty string on failure.
inline std::string VP_writeTempFile(const std::vector<uint8_t> &bytes, const std::string &ext)
{
#ifdef _WIN32
    char tmpDir[MAX_PATH];
    if (GetTempPathA(MAX_PATH, tmpDir) == 0)
        return {};
    char tmpFile[MAX_PATH];
    if (GetTempFileNameA(tmpDir, "flxv", 0, tmpFile) == 0)
        return {};
    std::string outPath = std::string(tmpFile) + ext;
    FILE *f = nullptr;
    fopen_s(&f, outPath.c_str(), "wb");
#else
    std::string tmpl = std::string(P_tmpdir) + "/flxvideoXXXXXX";
    std::vector<char> tmplBuf(tmpl.begin(), tmpl.end());
    tmplBuf.push_back('\0');
    int fd = mkstemp(tmplBuf.data());
    if (fd < 0)
        return {};
    std::string outPath = std::string(tmplBuf.data()) + ext;
    ::rename(tmplBuf.data(), outPath.c_str());
    FILE *f = fopen(outPath.c_str(), "wb");
#endif
    if (!f)
        return {};
    fwrite(bytes.data(), 1, bytes.size(), f);
    fclose(f);
    return outPath;
}

// ============================================================================
// VideoPlayerWidget
// ============================================================================

class VideoPlayerWidget : public Widget
{
public:
    // ── Public config ─────────────────────────────────────────────────────────
    std::string videoPath;
    int barHeight = 40;
    bool autoPlay = false;

    // ── Colors ────────────────────────────────────────────────────────────────
    Color colBar = Color::fromRGBA(20, 20, 20, 220);
    Color colTrackBg = Color::fromRGB(100, 100, 100);
    Color colTrackFill = Color::fromRGB(220, 220, 220);
    Color colThumb = Color::fromRGB(255, 255, 255);
    Color colText = Color::fromRGB(230, 230, 230);
    Color colIcon = Color::fromRGB(220, 220, 220);
    Color colIconHov = Color::fromRGB(255, 255, 255);
    Color colBg = Color::fromRGB(0, 0, 0);
    Color colOverlay = Color::fromRGBA(0, 0, 0, 60);
    Color colLoadingText = Color::fromRGB(180, 180, 180);
    Color colErrorText = Color::fromRGB(220, 80, 80);

    // ── Fluent setters ────────────────────────────────────────────────────────

    std::shared_ptr<VideoPlayerWidget> setPath(const std::string &p)
    {
        videoPath = p;
        _sourceType = VideoSourceType::Path;
        _sourceUrl.clear();
        _sourceMemory.clear();
        return self();
    }

    std::shared_ptr<VideoPlayerWidget> setUrl(const std::string &url)
    {
        _sourceUrl = url;
        _sourceType = VideoSourceType::Url;
        videoPath.clear();
        _sourceMemory.clear();
        return self();
    }

    std::shared_ptr<VideoPlayerWidget> setMemory(const std::vector<uint8_t> &bytes)
    {
        _sourceMemory = bytes;
        _sourceType = VideoSourceType::Memory;
        videoPath.clear();
        _sourceUrl.clear();
        return self();
    }

    std::shared_ptr<VideoPlayerWidget> setMemory(const uint8_t *data, size_t len)
    {
        _sourceMemory.assign(data, data + len);
        _sourceType = VideoSourceType::Memory;
        videoPath.clear();
        _sourceUrl.clear();
        return self();
    }

    std::shared_ptr<VideoPlayerWidget> setWidth(int w)
    {
        Widget::width = w;
        autoWidth = false;
        return self();
    }

    std::shared_ptr<VideoPlayerWidget> setHeight(int h)
    {
        Widget::height = h;
        autoHeight = false;
        return self();
    }

    std::shared_ptr<VideoPlayerWidget> setAutoPlay(bool b)
    {
        autoPlay = b;
        return self();
    }

    // ── Constructor ───────────────────────────────────────────────────────────

    VideoPlayerWidget()
    {
        autoWidth = autoHeight = false;
        width = 320;
        height = 180;

#ifdef __ANDROID__
        // Android: callbacks fire on decode thread → safe to call markNeedsPaint directly.
        FluxVideo::get().setOnFinished([this]()
                                       {
            _playing  = false;
            _finished = true;
            _progress = 1.f;
            markNeedsPaint(); });

        FluxVideo::get().setOnReady([this](int w, int h)
                                    {
            _vidW          = w;
            _vidH          = h;
            _nvgImageDirty = true;
            markNeedsPaint(); });
#else
        // Win32 / Linux / macOS / Web: finish callback may arrive off the
        // UI thread; use atomic flag and consume it in render() on the UI
        // thread.
        FluxVideo::get().setOnFinished([this]()
                                       {
            _finishedPending = true;
            if (auto* ui = FluxUI::getCurrentInstance())
                ui->invalidateWidget(x, y, width, height); });

        FluxVideo::get().setOnReady([this](int, int)
                                    { markNeedsPaint(); });
#endif
    }

    // ── Destructor ────────────────────────────────────────────────────────────

    ~VideoPlayerWidget()
    {
#if !defined(__ANDROID__)
        _destroyed = true;
#endif
        _stopTimers();
        FluxVideo::get().close(); 

#ifdef __ANDROID__
        NVGcontext *vg = FluxAndroid_getVG();
        if (_nvgImage >= 0 && vg)
        {
            nvgDeleteImage(vg, _nvgImage);
            _nvgImage = -1;
        }
        if (_fbo)
        {
            glDeleteFramebuffers(1, &_fbo);
            _fbo = 0;
        }
        if (_fboTex)
        {
            glDeleteTextures(1, &_fboTex);
            _fboTex = 0;
        }
        if (_oesVBO)
        {
            glDeleteBuffers(1, &_oesVBO);
            _oesVBO = 0;
        }
        if (_oesProgram)
        {
            glDeleteProgram(_oesProgram);
            _oesProgram = 0;
        }
#elif defined(__APPLE__) && TARGET_OS_OSX
        _freeCGResources();
#elif defined(__linux__)
        _freeCairoSurface();
#endif
    }

    // =========================================================================
    // Layout — identical on all platforms
    // =========================================================================

    void computeLayout(GraphicsContext & /*ctx*/, const BoxConstraints &constraints,
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
            _openVideoSource();
        }
    }

    // =========================================================================
    // Render — platform-specific frame blit, shared bar/overlay rendering
    // =========================================================================

    void render(GraphicsContext &ctx, FontCache &fontCache) override
    {
        Painter p(ctx);

        // ── Loading / error overlays (shared) ─────────────────────────────────
        if (_netState == NetState::Loading)
        {
            p.fillRect(x, y, width, height, colBg);
            _renderStatusOverlay(p, fontCache, "... Loading ...", colLoadingText);
            needsPaint = false;
            return;
        }
        if (_netState == NetState::Error)
        {
            p.fillRect(x, y, width, height, colBg);
            _renderStatusOverlay(p, fontCache, "Error loading video", colErrorText);
            needsPaint = false;
            return;
        }

        p.fillRect(x, y, width, height, colBg);

        // ── Platform frame blit ───────────────────────────────────────────────

#ifdef __ANDROID__
        {
            NVGcontext *vg = FluxAndroid_getVG();
            auto &vid = FluxVideo::get();

            _initGLResources();

            if (_nvgImageDirty && _vidW > 0 && _vidH > 0 && vg)
            {
                _nvgImageDirty = false;
                _rebuildFBO(vg, _vidW, _vidH);
            }

            if (vid.updateFrame() && _fbo && _oesProgram)
            {
                _blitOESToFBO(vid.getTextureId());
                _progress = vid.getProgress();
            }

            if (_nvgImage >= 0 && _fboW > 0 && _fboH > 0 && vg)
            {
                int videoAreaH = height - barHeight;
                float vidAR = (float)_fboW / (float)_fboH;
                float widAR = (float)width / (float)videoAreaH;
                float dstW, dstH, dstX, dstY;
                if (vidAR > widAR)
                {
                    dstW = (float)width;
                    dstH = dstW / vidAR;
                    dstX = (float)x;
                    dstY = (float)y + ((float)videoAreaH - dstH) * 0.5f;
                }
                else
                {
                    dstH = (float)videoAreaH;
                    dstW = dstH * vidAR;
                    dstX = (float)x + ((float)width - dstW) * 0.5f;
                    dstY = (float)y;
                }

                Painter::VideoDrawParams vp;
                vp.frame = _nvgImage;
                vp.srcW = _fboW;
                vp.srcH = _fboH;
                vp.dstX = (int)dstX;
                vp.dstY = (int)dstY;
                vp.dstW = (int)dstW;
                vp.dstH = (int)dstH;
                p.drawVideo(vp);

                if (_barVisible)
                    p.fillRect(x, y, width, videoAreaH, colOverlay);
            }
        }

#elif defined(__APPLE__) && TARGET_OS_OSX
        {
            if (_finishedPending.exchange(false))
            {
                _playing = false;
                _finished = true;
                _progress = 1.f;
            }

            if (FluxVideo::get().hasNewFrame())
            {
                auto frame = FluxVideo::get().lockFrame();
                if (frame.data && frame.width > 0 && frame.height > 0)
                {
                    int expectedStride = frame.width * 3;
                    _frameCache.resize((size_t)(frame.width * frame.height * 3));
                    if (frame.stride == expectedStride)
                    {
                        memcpy(_frameCache.data(), frame.data, _frameCache.size());
                    }
                    else
                    {
                        const uint8_t *src = frame.data;
                        uint8_t *dst = _frameCache.data();
                        for (int row = 0; row < frame.height; ++row)
                        {
                            memcpy(dst, src, (size_t)expectedStride);
                            src += frame.stride;
                            dst += expectedStride;
                        }
                    }
                    _cachedSrcW = frame.width;
                    _cachedSrcH = frame.height;
                    _cgImageDirty = true;
                }
                _progress = FluxVideo::get().getProgress();
            }

            if (!_frameCache.empty() && _cachedSrcW > 0 && ctx.cgContext)
            {
                if (_cgImageDirty)
                {
                    _cgImageDirty = false;
                    _rebuildCGImage();
                }

                if (_cgImage)
                {
                    Painter::VideoDrawParams vp;
                    vp.frame = (NativeImage)_cgImage;
                    _letterbox(_cachedSrcW, _cachedSrcH, vp.dstX, vp.dstY, vp.dstW, vp.dstH);
                    p.drawVideo(vp);
                }
            }
        }

#elif defined(_WIN32)
        {
            if (_finishedPending.exchange(false))
            {
                _playing = false;
                _finished = true;
                _progress = 1.f;
            }

            if (FluxVideo::get().hasNewFrame())
            {
                auto frame = FluxVideo::get().lockFrame();
                if (frame.data && frame.width > 0 && frame.height > 0)
                {
                    _cachedSrcW = frame.width;
                    _cachedSrcH = frame.height;

                    // Convert RGB24 → BGRA32 (D2D requires 32bpp)
                    int nPixels = frame.width * frame.height;
                    _frameCache.resize(nPixels * 4);
                    const uint8_t *src = frame.data;
                    uint8_t *dst = _frameCache.data();
                    for (int i = 0; i < nPixels; ++i)
                    {
                        dst[0] = src[2]; // B
                        dst[1] = src[1]; // G
                        dst[2] = src[0]; // R
                        dst[3] = 0xFF;   // A
                        src += 3;
                        dst += 4;
                    }
                    _d2dBitmapDirty = true;
                }
                _progress = FluxVideo::get().getProgress();
            }

            // Upload to D2D bitmap if pixel data changed
            if (_d2dBitmapDirty && !_frameCache.empty() && _cachedSrcW > 0 && ctx.dc)
            {
                _d2dBitmapDirty = false;

                // Recreate bitmap if size changed
                if (!_d2dBitmap ||
                    _d2dBitmapW != _cachedSrcW ||
                    _d2dBitmapH != _cachedSrcH)
                {
                    _d2dBitmap = nullptr;
                    _d2dBitmapW = _cachedSrcW;
                    _d2dBitmapH = _cachedSrcH;

                    D2D1_BITMAP_PROPERTIES bp = D2D1::BitmapProperties(
                        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                                          D2D1_ALPHA_MODE_IGNORE));
                    ctx.dc->CreateBitmap(
                        D2D1::SizeU((UINT32)_cachedSrcW, (UINT32)_cachedSrcH),
                        bp, &_d2dBitmap);
                }

                if (_d2dBitmap)
                {
                    D2D1_RECT_U destRect = D2D1::RectU(
                        0, 0, (UINT32)_cachedSrcW, (UINT32)_cachedSrcH);
                    _d2dBitmap->CopyFromMemory(
                        &destRect,
                        _frameCache.data(),
                        (UINT32)(_cachedSrcW * 4));
                }
            }

            if (_d2dBitmap && _cachedSrcW > 0)
            {
                Painter::VideoDrawParams vp;
                vp.frame = static_cast<NativeImage>(_d2dBitmap.Get());
                _letterbox(_cachedSrcW, _cachedSrcH, vp.dstX, vp.dstY, vp.dstW, vp.dstH);
                p.drawVideo(vp);
            }
        }

#elif defined(__linux__)
        {
            if (_finishedPending.exchange(false))
            {
                _playing = false;
                _finished = true;
                _progress = 1.f;
            }

            if (FluxVideo::get().hasNewFrame())
            {
                auto frame = FluxVideo::get().lockFrame();
                if (frame.data && frame.width > 0 && frame.height > 0)
                {
                    _frameCache.assign(frame.data, frame.data + frame.stride * frame.height);
                    _cachedSrcW = frame.width;
                    _cachedSrcH = frame.height;
                    if (_cachedSrcW != _cairoSurfW || _cachedSrcH != _cairoSurfH)
                        _rebuildCairoSurface();
                }
                _progress = FluxVideo::get().getProgress();
            }

            if (_cairoSurf && !_frameCache.empty() && _cachedSrcW > 0 && ctx.cr)
            {
                _updateCairoPixels();
                cairo_surface_mark_dirty(_cairoSurf);

                Painter::VideoDrawParams vp;
                vp.frame = (NativeImage)_cairoSurf;
                _letterbox(_cachedSrcW, _cachedSrcH, vp.dstX, vp.dstY, vp.dstW, vp.dstH);
                p.drawVideo(vp);
            }
        }

#elif defined(__EMSCRIPTEN__)
        {
            if (_finishedPending.exchange(false))
            {
                _playing = false;
                _finished = true;
                _progress = 1.f;
            }

            auto &vid = FluxVideo::get();
            if (vid.hasNewFrame())
            {
                int srcW = vid.getVideoWidth();
                int srcH = vid.getVideoHeight();
                if (srcW > 0 && srcH > 0)
                {
                    Painter::VideoDrawParams vp;
                    _letterbox(srcW, srcH, vp.dstX, vp.dstY, vp.dstW, vp.dstH);
                    p.drawVideo(vp);
                }
                _progress = vid.getProgress();
            }
        }
#endif

        // ── Shared bar + center-play overlay ──────────────────────────────────────
        if (_barVisible)
            _renderBar(ctx, fontCache, p);
        if (!_playing && _barVisible)
            _renderCenterPlay(p, fontCache);

        needsPaint = false;
    }

    // =========================================================================
    // Mouse events — identical on all platforms
    // =========================================================================

    bool handleMouseDown(int mx, int my) override
    {
        if (!_inWidget(mx, my))
            return false;

        if (!_inRect(mx, my, _barRect))
        {
            _barVisible = true;
            _resetBarTimer();
            _togglePlayPause();
            markNeedsPaint();
            return true;
        }
        if (_inRect(mx, my, _playBtnRect))
        {
            _togglePlayPause();
            markNeedsPaint();
            return true;
        }
        if (_inRect(mx, my, _trackRect))
        {
            _dragging = true;
            FluxUI::getCurrentInstance()->captureMouseInput();
            _seekFromMouse(mx);
            markNeedsPaint();
            return true;
        }
        _barVisible = true;
        _resetBarTimer();
        markNeedsPaint();
        return true;
    }

    bool handleMouseUp(int, int) override
    {
        if (_dragging)
        {
            _dragging = false;
            FluxUI::getCurrentInstance()->releaseMouseInput();
            markNeedsPaint();
        }
        return false;
    }

    bool handleMouseMove(int mx, int my) override
    {
        if (_dragging)
        {
            _seekFromMouse(mx);
            return true;
        }
        bool inW = _inWidget(mx, my);
        if (inW != _barVisible)
        {
            _barVisible = inW;
            markNeedsPaint();
        }
        if (inW)
            _resetBarTimer();

        bool hp = _inRect(mx, my, _playBtnRect);
        if (hp != _hovPlay)
        {
            _hovPlay = hp;
            markNeedsPaint();
        }
        return false;
    }

    bool handleMouseLeave() override
    {
        _barVisible = _hovPlay = false;
        markNeedsPaint();
        return true;
    }

private:
    // ── Source ────────────────────────────────────────────────────────────────
    VideoSourceType _sourceType = VideoSourceType::None;
    std::string _sourceUrl;
    std::vector<uint8_t> _sourceMemory;

    enum class NetState
    {
        Idle,
        Loading,
        Error
    };
    NetState _netState = NetState::Idle;

    // ── Shared playback state ─────────────────────────────────────────────────
    bool _opened = false;
    bool _playing = false;
    bool _finished = false;
    float _progress = 0.f;
    bool _dragging = false;
    bool _barVisible = true;
    bool _hovPlay = false;

    struct Rect
    {
        int x, y, w, h;
    };
    Rect _barRect{}, _playBtnRect{}, _trackRect{};

    TimerID _progressTimer = 0;
    TimerID _barHideTimer = 0;

    // ── Platform frame storage ────────────────────────────────────────────────
#ifdef __ANDROID__
    int _vidW = 0, _vidH = 0;
    GLuint _fboTex = 0;
    GLuint _fbo = 0;
    int _fboW = 0, _fboH = 0;
    int _nvgImage = -1;
    GLuint _oesProgram = 0;
    GLuint _oesVAO = 0;
    GLuint _oesVBO = 0;
    bool _glResourcesReady = false;
    bool _nvgImageDirty = false;
    std::vector<uint8_t> _fboPixels;

#elif defined(__APPLE__) && TARGET_OS_OSX
    // macOS: raw RGB24 cache + a CGImage built from it.
    // _cgColorSpace is retained for the lifetime of the widget to avoid
    // re-creating it on every frame; _cgImage is released and rebuilt
    // whenever a new frame arrives.
    std::vector<uint8_t> _frameCache;
    int _cachedSrcW = 0, _cachedSrcH = 0;
    bool _cgImageDirty = false;
    CGImageRef _cgImage = nullptr;
    CGColorSpaceRef _cgColorSpace = nullptr;
    std::atomic<bool> _destroyed{false};
    std::atomic<bool> _finishedPending{false};

#elif defined(_WIN32)
    std::vector<uint8_t> _frameCache;
    int _cachedSrcW = 0, _cachedSrcH = 0;
    bool _d2dBitmapDirty = false;
    int _d2dBitmapW = 0, _d2dBitmapH = 0;
    Microsoft::WRL::ComPtr<ID2D1Bitmap> _d2dBitmap;
    std::atomic<bool> _destroyed{false};
    std::atomic<bool> _finishedPending{false};

#elif defined(__linux__)
    std::vector<uint8_t> _frameCache;
    int _cachedSrcW = 0, _cachedSrcH = 0;
    std::vector<uint8_t> _cairoPixels;
    cairo_surface_t *_cairoSurf = nullptr;
    int _cairoSurfW = 0, _cairoSurfH = 0;
    std::atomic<bool> _destroyed{false};
    std::atomic<bool> _finishedPending{false};

#elif defined(__EMSCRIPTEN__)
    // No pixel buffer to cache — FluxVideo::renderFrame() blits the
    // <video> element straight into the canvas. Same destroyed/pending
    // bookkeeping as the other non-Android platforms, nothing else.
    std::atomic<bool> _destroyed{false};
    std::atomic<bool> _finishedPending{false};
#endif

    // =========================================================================
    // Source dispatch — identical on all platforms
    // =========================================================================

    void _openVideoSource()
    {
        if (_sourceType == VideoSourceType::Url && !_sourceUrl.empty())
        {
            _loadFromUrl();
            return;
        }
        if (_sourceType == VideoSourceType::Memory && !_sourceMemory.empty())
        {
            _playFromMemory();
            return;
        }
        if (!videoPath.empty())
            _openVideo();
    }

    void _loadFromUrl()
    {
        _netState = NetState::Loading;
        markNeedsPaint();
        std::weak_ptr<VideoPlayerWidget> weak = self();
        std::string url = _sourceUrl;
        FluxHttp::get(url, [weak](HttpResult result)
                      {
            auto s = weak.lock();
            if (!s) return;
            if (!result.success || result.body.empty()) {
                s->_netState = NetState::Error;
                s->markNeedsPaint();
                return;
            }
            const auto* d = reinterpret_cast<const uint8_t*>(result.body.data());
            s->_sourceMemory.assign(d, d + result.body.size());
            s->_netState = NetState::Idle;
            s->_playFromMemory(); });
    }

    void _playFromMemory()
    {
        if (_sourceMemory.empty())
            return;
        std::string ext = VP_detectVideoExtension(_sourceMemory);
        std::string path = VP_writeTempFile(_sourceMemory, ext);
        if (path.empty())
        {
            _netState = NetState::Error;
            markNeedsPaint();
            return;
        }
        videoPath = path;
        _openVideo();
    }

    void _openVideo()
    {
        FluxVideo::get().open(videoPath);
        if (autoPlay)
        {
            FluxVideo::get().play();
            _playing = true;
            _startProgressTimer();
        }
    }

    // =========================================================================
    // Playback helpers — identical on all platforms
    // =========================================================================

    void _togglePlayPause()
    {
        auto &vid = FluxVideo::get();
        if (_finished)
        {
            _finished = false;
            _progress = 0.f;
            vid.seekToProgress(0.f);
            vid.play();
            _playing = true;
            _startProgressTimer();
            return;
        }
        if (_playing)
        {
            vid.pause();
            _playing = false;
        }
        else
        {
            vid.play();
            _playing = true;
            _startProgressTimer();
        }
    }

    void _seekFromMouse(int mx)
    {
        if (_trackRect.w <= 0)
            return;
        float t = std::max(0.f, std::min(1.f,
                                         (float)(mx - _trackRect.x) / (float)_trackRect.w));
        _progress = t;
        FluxVideo::get().seekToProgress(t);
        if (_finished && t < 0.999f)
        {
            _finished = false;
            FluxVideo::get().play();
            _playing = true;
            _startProgressTimer();
        }
        markNeedsPaint();
    }

    void _startProgressTimer()
    {
        if (_progressTimer)
            return;
        _progressTimer = FluxUI::getCurrentInstance()->setInterval(33, [this]()
                                                                   {
#if !defined(__ANDROID__)
            if (_destroyed) return;
#endif
            if (_playing) {
                _progress = FluxVideo::get().getProgress();
                markNeedsPaint();
            } });
    }

    void _resetBarTimer()
    {
        auto *ui = FluxUI::getCurrentInstance();
        if (!ui)
            return;
        if (_barHideTimer)
        {
            ui->clearInterval(_barHideTimer);
            _barHideTimer = 0;
        }
        _barHideTimer = ui->setInterval(3000, [this]()
                                        {
#if !defined(__ANDROID__)
                                            if (_destroyed)
                                                return;
#endif
                                            if (_playing)
                                            {
                                                _barVisible = false;
                                                markNeedsPaint();
                                            }
                                            // do NOT clear/touch _barHideTimer here
                                        });
    }

    void _stopTimers()
    {
        auto *ui = FluxUI::getCurrentInstance();
        if (!ui)
            return;
        if (_progressTimer)
        {
            ui->clearInterval(_progressTimer);
            _progressTimer = 0;
        }
        if (_barHideTimer)
        {
            ui->clearInterval(_barHideTimer);
            _barHideTimer = 0;
        }
    }

    // =========================================================================
    // Platform-specific helpers
    // =========================================================================

// ── Letterbox (Win32 + Linux + macOS + Web — not needed on Android) ───────
#if defined(_WIN32) || defined(__linux__) || (defined(__APPLE__) && TARGET_OS_OSX) || defined(__EMSCRIPTEN__)
    void _letterbox(int srcW, int srcH, int &dstX, int &dstY, int &dstW, int &dstH) const
    {
        float vidAR = (float)srcW / (float)srcH;
        float widAR = (float)width / (float)(height - barHeight);
        if (vidAR > widAR)
        {
            dstW = width;
            dstH = (int)((float)dstW / vidAR);
            dstX = x;
            dstY = y + (height - barHeight - dstH) / 2;
        }
        else
        {
            dstH = height - barHeight;
            dstW = (int)((float)dstH * vidAR);
            dstX = x + (width - dstW) / 2;
            dstY = y;
        }
    }
#endif

    // ── macOS CoreGraphics helpers ─────────────────────────────────────────────
#if defined(__APPLE__) && TARGET_OS_OSX

    // Build (or rebuild) the CGImage from the current _frameCache contents.
    // Called only when _cgImageDirty is set, so typically once per decoded frame.
    void _rebuildCGImage()
    {
        if (_cgImage)
        {
            CGImageRelease(_cgImage);
            _cgImage = nullptr;
        }
        if (_frameCache.empty() || _cachedSrcW <= 0 || _cachedSrcH <= 0)
            return;

        // Lazily create the device-RGB colour space (retained).
        if (!_cgColorSpace)
            _cgColorSpace = CGColorSpaceCreateDeviceRGB();

        // Wrap _frameCache in a CGDataProvider without copying.
        // The retain/release pair ensures the cache outlives the provider.
        // We use a simple no-op releaser because _frameCache is owned by this
        // object and will always outlive the CGImage it backs.
        CGDataProviderRef provider = CGDataProviderCreateWithData(
            nullptr,
            _frameCache.data(),
            _frameCache.size(),
            nullptr // no-op release callback — _frameCache is member-owned
        );

        if (!provider)
            return;

        // kCVPixelFormatType_24RGB → 8 bits per component, 3 components, no alpha.
        _cgImage = CGImageCreate(
            (size_t)_cachedSrcW,       // width
            (size_t)_cachedSrcH,       // height
            8,                         // bitsPerComponent
            24,                        // bitsPerPixel
            (size_t)(_cachedSrcW * 3), // bytesPerRow (no padding — we stripped it above)
            _cgColorSpace,
            (CGBitmapInfo)((uint32_t)kCGBitmapByteOrderDefault | (uint32_t)kCGImageAlphaNone), // RGB24, no alpha
            provider,
            nullptr, // no decode array
            false,   // shouldInterpolate (we set it on ctx instead)
            kCGRenderingIntentDefault);

        CGDataProviderRelease(provider);
    }

    void _freeCGResources()
    {
        if (_cgImage)
        {
            CGImageRelease(_cgImage);
            _cgImage = nullptr;
        }
        if (_cgColorSpace)
        {
            CGColorSpaceRelease(_cgColorSpace);
            _cgColorSpace = nullptr;
        }
    }

#endif // __APPLE__ && TARGET_OS_OSX

    // ── Android GL helpers ────────────────────────────────────────────────────
#ifdef __ANDROID__
    void _initGLResources()
    {
        if (_glResourcesReady)
            return;
        _glResourcesReady = true;

        static const char *vsrc = R"(
            attribute vec2 aPos;
            attribute vec2 aUV;
            varying   vec2 vUV;
            void main() { vUV = aUV; gl_Position = vec4(aPos, 0.0, 1.0); }
        )";
        static const char *fsrc = R"(
            #extension GL_OES_EGL_image_external : require
            precision mediump float;
            uniform samplerExternalOES uTex;
            varying vec2 vUV;
            void main() { gl_FragColor = texture2D(uTex, vUV); }
        )";

        auto compile = [](GLenum type, const char *src) -> GLuint
        {
            GLuint s = glCreateShader(type);
            glShaderSource(s, 1, &src, nullptr);
            glCompileShader(s);
            GLint ok = 0;
            glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
            if (!ok)
            {
                char log[512];
                glGetShaderInfoLog(s, 512, nullptr, log);
                __android_log_print(ANDROID_LOG_ERROR, "FluxVideo",
                                    "Shader compile error: %s", log);
            }
            return s;
        };

        GLuint vs = compile(GL_VERTEX_SHADER, vsrc);
        GLuint fs = compile(GL_FRAGMENT_SHADER, fsrc);
        _oesProgram = glCreateProgram();
        glAttachShader(_oesProgram, vs);
        glAttachShader(_oesProgram, fs);
        glBindAttribLocation(_oesProgram, 0, "aPos");
        glBindAttribLocation(_oesProgram, 1, "aUV");
        glLinkProgram(_oesProgram);
        glDeleteShader(vs);
        glDeleteShader(fs);

        static const float quad[] = {
            -1.f,
            -1.f,
            0.f,
            1.f,
            1.f,
            -1.f,
            1.f,
            1.f,
            -1.f,
            1.f,
            0.f,
            0.f,
            1.f,
            1.f,
            1.f,
            0.f,
        };
        glGenBuffers(1, &_oesVBO);
        glBindBuffer(GL_ARRAY_BUFFER, _oesVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }

    void _rebuildFBO(NVGcontext *vg, int w, int h)
    {
        if (_fbo)
        {
            glDeleteFramebuffers(1, &_fbo);
            _fbo = 0;
        }
        if (_fboTex)
        {
            glDeleteTextures(1, &_fboTex);
            _fboTex = 0;
        }
        if (_nvgImage >= 0 && vg)
        {
            nvgDeleteImage(vg, _nvgImage);
            _nvgImage = -1;
        }

        _fboW = w;
        _fboH = h;
        if (w <= 0 || h <= 0)
            return;

        glGenTextures(1, &_fboTex);
        glBindTexture(GL_TEXTURE_2D, _fboTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);

        glGenFramebuffers(1, &_fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, _fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, _fboTex, 0);
        GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        if (status != GL_FRAMEBUFFER_COMPLETE)
        {
            __android_log_print(ANDROID_LOG_ERROR, "FluxVideo",
                                "FBO incomplete: 0x%x", status);
            return;
        }

        _fboPixels.resize((size_t)(w * h * 4));
        _nvgImage = nvgCreateImageRGBA(vg, w, h, 0, _fboPixels.data());
        __android_log_print(ANDROID_LOG_DEBUG, "FluxVideo",
                            "FBO %dx%d created, nvgImage=%d", w, h, _nvgImage);
    }

    void _blitOESToFBO(GLuint oesTexId)
    {
        if (!_fbo || !_oesProgram)
            return;

        GLint prevFBO = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);
        GLint prevVP[4];
        glGetIntegerv(GL_VIEWPORT, prevVP);
        GLint prevProg = 0;
        glGetIntegerv(GL_CURRENT_PROGRAM, &prevProg);

        glBindFramebuffer(GL_FRAMEBUFFER, _fbo);
        glViewport(0, 0, _fboW, _fboH);
        glDisable(GL_BLEND);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_STENCIL_TEST);

        glUseProgram(_oesProgram);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_EXTERNAL_OES, oesTexId);
        glUniform1i(glGetUniformLocation(_oesProgram, "uTex"), 0);

        glBindBuffer(GL_ARRAY_BUFFER, _oesVBO);
        glEnableVertexAttribArray(0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)0);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)(2 * sizeof(float)));
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glDisableVertexAttribArray(0);
        glDisableVertexAttribArray(1);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindTexture(GL_TEXTURE_EXTERNAL_OES, 0);

        if (_nvgImage >= 0 && !_fboPixels.empty())
        {
            glReadPixels(0, 0, _fboW, _fboH, GL_RGBA, GL_UNSIGNED_BYTE, _fboPixels.data());
            NVGcontext *vg = FluxAndroid_getVG();
            if (vg)
                nvgUpdateImage(vg, _nvgImage, _fboPixels.data());
        }

        glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prevFBO);
        glViewport(prevVP[0], prevVP[1], prevVP[2], prevVP[3]);
        glUseProgram((GLuint)prevProg);
        glEnable(GL_BLEND);
    }
#endif // __ANDROID__

    // ── Linux Cairo helpers ───────────────────────────────────────────────────
#if defined(__linux__) && !defined(__ANDROID__)
    void _rebuildCairoSurface()
    {
        _freeCairoSurface();
        if (_cachedSrcW <= 0 || _cachedSrcH <= 0)
            return;
        _cairoPixels.resize((size_t)(_cachedSrcW * _cachedSrcH * 4));
        _cairoSurf = cairo_image_surface_create_for_data(
            _cairoPixels.data(), CAIRO_FORMAT_RGB24,
            _cachedSrcW, _cachedSrcH, _cachedSrcW * 4);
        _cairoSurfW = _cachedSrcW;
        _cairoSurfH = _cachedSrcH;
    }

    void _freeCairoSurface()
    {
        if (_cairoSurf)
        {
            cairo_surface_destroy(_cairoSurf);
            _cairoSurf = nullptr;
        }
        _cairoSurfW = _cairoSurfH = 0;
    }

    void _updateCairoPixels()
    {
        // FluxVideo delivers RGB24; Cairo ARGB32 stores as BGRX in memory
        int n = _cachedSrcW * _cachedSrcH;
        const uint8_t *src = _frameCache.data();
        uint8_t *dst = _cairoPixels.data();
        for (int i = 0; i < n; ++i)
        {
            dst[0] = src[2]; // B
            dst[1] = src[1]; // G
            dst[2] = src[0]; // R
            dst[3] = 0xFF;
            src += 3;
            dst += 4;
        }
    }
#endif // __linux__

    // =========================================================================
    // Shared rendering helpers
    // =========================================================================

    static const char *_uiFont()
    {
#if defined(__APPLE__) && TARGET_OS_OSX
        return "SF Pro Text"; // San Francisco — present on all macOS 10.11+
#elif defined(__linux__)
        return "Sans";
#else
        return "Segoe UI";
#endif
    }

    void _renderStatusOverlay(Painter &p, FontCache &fontCache,
                              const std::string &msg, Color col)
    {
        NativeFont tf = fontCache.getFont(_uiFont(), 14, FontWeight::Normal);
        p.drawTextA(msg, x, y, width, height, tf, col,
                    DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    void _renderBar(GraphicsContext & /*ctx*/, FontCache &fontCache, Painter &p)
    {
        int barY = y + height - barHeight;
        _barRect = {x, barY, width, barHeight};
        p.fillRect(x, barY, width, barHeight, colBar);

        int midY = barY + barHeight / 2;
        int btnSz = 28;
        int cx = x + 8;

        _playBtnRect = {cx, barY + (barHeight - btnSz) / 2, btnSz, btnSz};
        {
            NativeFont iconFont = fontCache.getFont(kIconFont, 18, FontWeight::Normal);
            std::wstring g(1, FluxIcons::glyph(_playing ? FluxIcons::Pause : FluxIcons::Play));
            Color c = _hovPlay ? colIconHov : colIcon;
            p.drawText(g, _playBtnRect.x, _playBtnRect.y,
                       _playBtnRect.w, _playBtnRect.h,
                       iconFont, c, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        cx += btnSz + 6;

        // Timestamp
        float dur = FluxVideo::get().getDurationSeconds();
        std::string ts = _fmtTime(_progress * dur) + " / " + _fmtTime(dur);
        NativeFont tf = fontCache.getFont(_uiFont(), 12, FontWeight::Normal);
        int tw = 0, th = 0;
        p.measureText(toWideString(ts), tf, tw, th);

#ifdef __ANDROID__
        float dpi = FluxAndroid_getDpiScale();
        if (tw > width / 2)
            tw = (int)(tw / dpi);
#endif

        p.drawText(toWideString(ts), cx, barY, tw + 4, barHeight,
                   tf, colText, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        cx += tw + 10;

        // Seek track
        int trackW = std::max(20, x + width - 12 - cx);
        _trackRect = {cx, midY - 8, trackW, 16};

        p.fillRoundedRectGDI(cx, midY - 2, trackW, 4, 2, colTrackBg, colTrackBg, 0);
        int fillW = (int)(_progress * trackW);
        if (fillW > 0)
            p.fillRoundedRectGDI(cx, midY - 2, fillW, 4, 2, colTrackFill, colTrackFill, 0);
        p.drawEllipse(cx + fillW - 6, midY - 6, 12, 12, colThumb, colThumb, 0);
    }

    void _renderCenterPlay(Painter &p, FontCache &fontCache)
    {
        int cx = x + width / 2;
        int cy = y + (height - barHeight) / 2;
        int r = 28;
        p.drawEllipse(cx - r, cy - r, r * 2, r * 2,
                      Color::fromRGBA(0, 0, 0, 160),
                      Color::fromRGBA(0, 0, 0, 0), 0);
        NativeFont iconFont = fontCache.getFont(kIconFont, 32, FontWeight::Normal);
        std::wstring g(1, FluxIcons::glyph(FluxIcons::Play));
        p.drawText(g, cx - r, cy - r, r * 2, r * 2,
                   iconFont, Color::fromRGB(255, 255, 255),
                   DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    // =========================================================================
    // Misc helpers
    // =========================================================================

    std::shared_ptr<VideoPlayerWidget> self()
    {
        return std::static_pointer_cast<VideoPlayerWidget>(shared_from_this());
    }

    bool _inWidget(int mx, int my) const
    {
        return mx >= x && mx < x + width && my >= y && my < y + height;
    }

    static bool _inRect(int mx, int my, const Rect &r)
    {
        return mx >= r.x && mx < r.x + r.w && my >= r.y && my < r.y + r.h;
    }

    static std::string _fmtTime(float s)
    {
        int si = (int)s;
        char buf[16];
        snprintf(buf, sizeof(buf), "%d:%02d", si / 60, si % 60);
        return buf;
    }
};

// ============================================================================
// Factory functions
// ============================================================================

using VideoPlayerWidgetPtr = std::shared_ptr<VideoPlayerWidget>;

inline VideoPlayerWidgetPtr VideoPlayer(const std::string &path = "")
{
    auto w = std::make_shared<VideoPlayerWidget>();
    if (!path.empty())
        w->setPath(path);
    return w;
}

inline VideoPlayerWidgetPtr VideoPlayerFromUrl(const std::string &url)
{
    return std::make_shared<VideoPlayerWidget>()->setUrl(url);
}

inline VideoPlayerWidgetPtr VideoPlayerFromMemory(const std::vector<uint8_t> &bytes)
{
    return std::make_shared<VideoPlayerWidget>()->setMemory(bytes);
}

inline VideoPlayerWidgetPtr VideoPlayerFromMemory(const uint8_t *data, size_t len)
{
    return std::make_shared<VideoPlayerWidget>()->setMemory(data, len);
}