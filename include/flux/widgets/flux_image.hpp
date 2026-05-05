#ifndef FLUX_IMAGE_HPP
#define FLUX_IMAGE_HPP

#include "../flux_core.hpp"
#include "../flux_http.hpp"

// ============================================================================
// PLATFORM INCLUDES
// ============================================================================

#ifdef _WIN32
#   include <gdiplus.h>
#   include <objidl.h>   // IStream
#endif

#if defined(__linux__) && !defined(__ANDROID__)
#   include <cairo/cairo.h>
#   define STBI_ONLY_JPEG
#   define STBI_ONLY_PNG
#   define STBI_ONLY_BMP
#   define STBI_ONLY_TGA
#   include "stb_image.h"
#endif

#ifdef __ANDROID__
#   include "nanovg.h"
#   include <android/asset_manager.h>
#   include <android/log.h>
#   define LOGI_IMG(...) __android_log_print(ANDROID_LOG_INFO,  "FluxImage", __VA_ARGS__)
#   define LOGW_IMG(...) __android_log_print(ANDROID_LOG_WARN,  "FluxImage", __VA_ARGS__)
extern NVGcontext*    FluxAndroid_getVG();
extern AAssetManager* FluxAndroid_getAssetManager();
#   define STBI_ONLY_JPEG
#   define STBI_ONLY_PNG
#   define STBI_ONLY_BMP
#   define STBI_ONLY_TGA
#   define STBI_ONLY_GIF
#   include "stb_image.h"
#endif

#include <memory>
#include <string>
#include <iostream>
#include <vector>

// ============================================================================
// IMAGE FIT MODES
// ============================================================================

enum class ImageFit {
    Fill,
    Contain,
    Cover,
    None,
    ScaleDown
};

// ============================================================================
// IMAGE LOAD STATE
// ============================================================================

enum class ImageLoadState {
    Idle,       // nothing requested yet
    Loading,    // URL fetch / decode in progress
    Loaded,     // image decoded and ready
    Error       // load or decode failed
};

// ============================================================================
// IMAGE WIDGET
// ============================================================================

class ImageWidget : public Widget {
public:
    std::string    imagePath;
    ImageFit       fit              = ImageFit::Contain;
    int            imageWidth       = 0;   // intrinsic pixel width of the decoded image
    int            imageHeight      = 0;   // intrinsic pixel height of the decoded image
    Color          placeholderColor = Color::fromRGB(240, 240, 240);
    Color          errorColor       = Color::fromRGB(255, 200, 200);
    ImageLoadState loadState        = ImageLoadState::Idle;

    bool imageLoaded = false;
    bool hasError    = false;

    // ── Platform pixel storage ────────────────────────────────────────────────
#ifdef _WIN32
    std::unique_ptr<Gdiplus::Bitmap> bitmap;

    // Pre-scaled bitmap cache — rebuilt only when display size or fit changes.
    mutable std::unique_ptr<Gdiplus::Bitmap> _scaledBitmap;
    mutable int  _scaledW   = 0;
    mutable int  _scaledH   = 0;
    mutable int  _scaledFit = -1;
#endif

#if defined(__linux__) && !defined(__ANDROID__)
    std::vector<uint8_t> pixels;

    // Pre-scaled Cairo surface cache — rebuilt only when display size or fit changes.
    mutable cairo_surface_t* _scaledSurface = nullptr;
    mutable int  _scaledW   = 0;
    mutable int  _scaledH   = 0;
    mutable int  _scaledFit = -1;
#endif

#ifdef __ANDROID__
    int nvgImage = -1;

    struct PendingPixels {
        std::vector<uint8_t> rgba;
        int w = 0;
        int h = 0;
        bool ready() const { return !rgba.empty() && w > 0 && h > 0; }
    } pending;

    std::vector<uint8_t> uploadBuffer;
    int uploadW = 0;
    int uploadH = 0;
#endif

    // ── Constructors ──────────────────────────────────────────────────────────
    ImageWidget() {
        autoWidth  = true;
        autoHeight = true;
    }
    explicit ImageWidget(const std::string& path) : ImageWidget() {
        loadImage(path);
    }

    ~ImageWidget() override {
#if defined(__linux__) && !defined(__ANDROID__)
        _invalidateScaledCache();
#endif
#ifdef _WIN32
        _invalidateScaledCache();
#endif
#ifdef __ANDROID__
        _freeNvgImage();
#endif
    }

    // =========================================================================
    // loadFromUrl
    // =========================================================================
    void loadFromUrl(const std::string& url, bool postToUI = true) {
        if (url.empty()) return;

        imagePath = url;
        _setLoadState(ImageLoadState::Loading);
        markNeedsPaint();

        auto weak = std::weak_ptr<ImageWidget>(
                std::static_pointer_cast<ImageWidget>(shared_from_this()));

        FluxHttp::get(url, [weak](HttpResult result) {
            auto self = weak.lock();
            if (!self) return;

            if (!result.success || result.body.empty()) {
                self->_setLoadState(ImageLoadState::Error);
                self->markNeedsLayout();
                return;
            }

            const auto& bytes = result.body;
            bool ok = self->_decodeFromMemory(
                    reinterpret_cast<const uint8_t*>(bytes.data()),
                    (int)bytes.size());

            if (!ok) {
                self->_setLoadState(ImageLoadState::Error);
            }
#ifndef __ANDROID__
            if (ok) {
                self->_invalidateScaledCache();
                self->_setLoadState(ImageLoadState::Loaded);
            }
#endif
            if (auto* ui = FluxUI::getCurrentInstance())
                ui->partialRebuild(self.get());
            else
                self->markNeedsLayout();

        }, postToUI);
    }

    // =========================================================================
    // loadImage — file-based loader
    // =========================================================================
    bool loadImage(const std::string& path) {
        if (path.empty()) {
            _setLoadState(ImageLoadState::Idle);
            return false;
        }

        imagePath = path;

#ifdef _WIN32
        wchar_t absPath[MAX_PATH] = {};
        std::wstring wpath = toWideString(path);
        if (!GetFullPathNameW(wpath.c_str(), MAX_PATH, absPath, nullptr)) {
            _setLoadState(ImageLoadState::Error); return false;
        }
        try {
            bitmap = std::make_unique<Gdiplus::Bitmap>(absPath);
            if (!bitmap || bitmap->GetLastStatus() != Gdiplus::Ok) {
                bitmap.reset(); _setLoadState(ImageLoadState::Error); return false;
            }
        } catch (...) {
            bitmap.reset(); _setLoadState(ImageLoadState::Error); return false;
        }
        imageWidth  = (int)bitmap->GetWidth();
        imageHeight = (int)bitmap->GetHeight();
        if (imageWidth == 0 || imageHeight == 0) {
            bitmap.reset(); _setLoadState(ImageLoadState::Error); return false;
        }
#endif

#if defined(__linux__) && !defined(__ANDROID__)
        int w = 0, h = 0, channels = 0;
        stbi_set_flip_vertically_on_load(0);
        unsigned char* data = stbi_load(path.c_str(), &w, &h, &channels, 4);
        if (!data) { pixels.clear(); _setLoadState(ImageLoadState::Error); return false; }
        _convertStbiToCairo(data, w, h);
        stbi_image_free(data);
        if (imageWidth == 0 || imageHeight == 0) {
            pixels.clear(); _setLoadState(ImageLoadState::Error); return false;
        }
#endif

#ifdef __ANDROID__
        _freeNvgImage();
        NVGcontext* vg = FluxAndroid_getVG();
        if (!vg) { _setLoadState(ImageLoadState::Idle); return false; }

        if (!path.empty() && path[0] == '/') {
            nvgImage = nvgCreateImage(vg, path.c_str(), 0);
        } else {
            AAssetManager* am = FluxAndroid_getAssetManager();
            if (!am) { _setLoadState(ImageLoadState::Error); return false; }
            AAsset* asset = AAssetManager_open(am, path.c_str(), AASSET_MODE_BUFFER);
            if (!asset) { _setLoadState(ImageLoadState::Error); return false; }

            const uint8_t* assetData = (const uint8_t*)AAsset_getBuffer(asset);
            int assetLen = (int)AAsset_getLength(asset);

            int w = 0, h = 0, ch = 0;
            stbi_set_flip_vertically_on_load(0);
            unsigned char* rgba = stbi_load_from_memory(assetData, assetLen,
                                                        &w, &h, &ch, 4);
            AAsset_close(asset);

            if (!rgba) {
                LOGW_IMG("stbi_load_from_memory failed for asset '%s': %s",
                         path.c_str(), stbi_failure_reason());
                _setLoadState(ImageLoadState::Error); return false;
            }

            nvgImage = nvgCreateImageRGBA(vg, w, h, NVG_IMAGE_PREMULTIPLIED, rgba);
            stbi_image_free(rgba);
        }

        if (nvgImage == -1) { _setLoadState(ImageLoadState::Error); return false; }
        nvgImageSize(vg, nvgImage, &imageWidth, &imageHeight);
        if (imageWidth == 0 || imageHeight == 0) {
            _freeNvgImage(); _setLoadState(ImageLoadState::Error); return false;
        }
#endif

        // Invalidate any stale pre-scaled cache from a previous image.
        _invalidateScaledCache();

        _setLoadState(ImageLoadState::Loaded);

        // If the widget already has an explicit size, a repaint is enough —
        // no need to re-measure the whole tree.
        if (!autoWidth && !autoHeight)
            markNeedsPaint();
        else
            markNeedsLayout();

        return true;
    }

    void reloadIfDeferred() {
#ifdef __ANDROID__
        if (!imagePath.empty() && loadState == ImageLoadState::Idle)
            loadImage(imagePath);
#endif
    }



    void computeLayout(GraphicsContext& /*ctx*/,
                       const BoxConstraints& constraints,
                       FontCache& /*fontCache*/) override
    {
        const int padW = paddingLeft + paddingRight;
        const int padH = paddingTop  + paddingBottom;

        if (loadState == ImageLoadState::Loaded && imageWidth > 0 && imageHeight > 0) {

            if (autoWidth && autoHeight) {
                // Both axes auto → wrap intrinsic size + padding
                width  = constraints.clampWidth (imageWidth  + padW);
                height = constraints.clampHeight(imageHeight + padH);

            } else if (autoWidth) {
                // Height is explicit (already the full widget height including padding)
                int contentH = std::max(1, height - padH);
                float aspect = (float)imageWidth / (float)imageHeight;
                int inferredContentW = (int)((float)contentH * aspect);
                width = constraints.clampWidth(inferredContentW + padW);

            } else if (autoHeight) {
                // Width is explicit (already the full widget width including padding)
                int contentW = std::max(1, width - padW);
                float aspect = (float)imageHeight / (float)imageWidth;
                int inferredContentH = (int)((float)contentW * aspect);
                height = constraints.clampHeight(inferredContentH + padH);

            } else {
                // Both explicit — just clamp to constraints; don't touch the values.
                width  = constraints.clampWidth (width);
                height = constraints.clampHeight(height);
            }

        } else {
            // Image not loaded yet (Loading, Error, Idle) — use explicit sizes or
            // a sensible fallback so the placeholder occupies real space.

            if (autoWidth)  width  = constraints.clampWidth (100 + padW);
            else            width  = constraints.clampWidth (width);

            if (autoHeight) height = constraints.clampHeight(100 + padH);
            else            height = constraints.clampHeight(height);
        }

        applyConstraints();
        needsLayout = false;
    }

    // ── Render ────────────────────────────────────────────────────────────────
    void render(GraphicsContext& ctx, FontCache& fontCache) override {
        Painter painter(ctx);

        if (hasBackground)
            drawRoundedRectangle(ctx);
        if (hasBorder)
            painter.drawRoundedRectOutline(x, y, width, height,
                                           borderRadius * 2,
                                           getCurrentBorderColor(), borderWidth);

        const int cx = x + paddingLeft;
        const int cy = y + paddingTop;
        const int cw = width  - paddingLeft - paddingRight;
        const int ch = height - paddingTop  - paddingBottom;

#ifdef __ANDROID__
        if (loadState == ImageLoadState::Idle && !imagePath.empty())
            loadImage(imagePath);
#endif

        switch (loadState) {
            case ImageLoadState::Loaded:
#ifdef _WIN32
                _renderGDIPlus(ctx, cx, cy, cw, ch);
#elif defined(__ANDROID__)
                _renderNanoVG(cx, cy, cw, ch);
#else
                _renderCairo(ctx, cx, cy, cw, ch);
#endif
                break;

            case ImageLoadState::Loading:
#ifdef __ANDROID__
                if (pending.ready()) {
                    _renderNanoVG(cx, cy, cw, ch);
                    if (loadState == ImageLoadState::Loaded) break;
                }
#endif
                _renderLoading(painter, fontCache, cx, cy, cw, ch);
                break;

            case ImageLoadState::Error:
                painter.fillRect(cx, cy, cw, ch, errorColor);
                painter.drawTextA("\xe2\x9c\x95",
                                  cx, cy, cw, ch,
                                  fontCache.getFont(fontSize, fontWeight),
                                  Color::fromRGB(150, 0, 0),
                                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                break;

            default:
                painter.fillRect(cx, cy, cw, ch, placeholderColor);
                break;
        }

        needsPaint = false;
    }

    // ── Fluent setters ────────────────────────────────────────────────────────
    std::shared_ptr<ImageWidget> setImagePath(const std::string& path) {
        loadImage(path); return self();
    }
    std::shared_ptr<ImageWidget> setUrl(const std::string& url,
                                        bool postToUI = true) {
        loadFromUrl(url, postToUI); return self();
    }
    std::shared_ptr<ImageWidget> setFit(ImageFit fitMode) {
        if (fit != fitMode) {
            fit = fitMode;
            _invalidateScaledCache();
            markNeedsPaint();
        }
        return self();
    }
    std::shared_ptr<ImageWidget> setPlaceholderColor(Color color) {
        placeholderColor = color; markNeedsPaint(); return self();
    }
    std::shared_ptr<ImageWidget> setErrorColor(Color color) {
        errorColor = color; markNeedsPaint(); return self();
    }
    std::shared_ptr<ImageWidget> setWidth(int w) {
        width = w; autoWidth = false; markNeedsLayout(); return self();
    }
    std::shared_ptr<ImageWidget> setHeight(int h) {
        height = h; autoHeight = false; markNeedsLayout(); return self();
    }
    std::shared_ptr<ImageWidget> setBorderRadius(int r) {
        borderRadius = r; markNeedsPaint(); return self();
    }
    std::shared_ptr<ImageWidget> setPadding(int p) {
        padding = paddingLeft = paddingRight = paddingTop = paddingBottom = p;
        markNeedsLayout(); return self();
    }

private:
    std::shared_ptr<ImageWidget> self() {
        return std::static_pointer_cast<ImageWidget>(shared_from_this());
    }

    void _setLoadState(ImageLoadState s) {
        loadState   = s;
        imageLoaded = (s == ImageLoadState::Loaded);
        hasError    = (s == ImageLoadState::Error);
    }



    void _invalidateScaledCache() {
#if defined(__linux__) && !defined(__ANDROID__)
        if (_scaledSurface) {
            cairo_surface_destroy(_scaledSurface);
            _scaledSurface = nullptr;
        }
        _scaledW = _scaledH = 0;
        _scaledFit = -1;
#endif
#ifdef _WIN32
        _scaledBitmap.reset();
        _scaledW = _scaledH = 0;
        _scaledFit = -1;
#endif
        // Android uses NanoVG textures that are already GPU-resident;
        // no separate pre-scale cache is needed there.
    }

    // =========================================================================
    // _decodeFromMemory — called from the HTTP callback (background thread)
    // =========================================================================
    bool _decodeFromMemory(const uint8_t* data, int len) {
#ifdef _WIN32
        IStream* stream = nullptr;
        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, (SIZE_T)len);
        if (!hMem) return false;
        void* ptr = GlobalLock(hMem);
        if (!ptr) { GlobalFree(hMem); return false; }
        memcpy(ptr, data, len);
        GlobalUnlock(hMem);
        if (CreateStreamOnHGlobal(hMem, TRUE, &stream) != S_OK) {
            GlobalFree(hMem); return false;
        }
        try {
            bitmap = std::make_unique<Gdiplus::Bitmap>(stream);
            stream->Release();
            if (!bitmap || bitmap->GetLastStatus() != Gdiplus::Ok) {
                bitmap.reset(); return false;
            }
        } catch (...) {
            stream->Release(); bitmap.reset(); return false;
        }
        imageWidth  = (int)bitmap->GetWidth();
        imageHeight = (int)bitmap->GetHeight();
        // Invalidate stale cache — new source pixels arrived.
        _invalidateScaledCache();
        return (imageWidth > 0 && imageHeight > 0);
#endif

#if defined(__linux__) && !defined(__ANDROID__)
        int w = 0, h = 0, channels = 0;
        stbi_set_flip_vertically_on_load(0);
        unsigned char* decoded = stbi_load_from_memory(data, len, &w, &h, &channels, 4);
        if (!decoded) { pixels.clear(); return false; }
        _convertStbiToCairo(decoded, w, h);
        stbi_image_free(decoded);
        // Invalidate stale cache — new source pixels arrived.
        _invalidateScaledCache();
        return (imageWidth > 0 && imageHeight > 0);
#endif

#ifdef __ANDROID__
        int w = 0, h = 0, ch = 0;
        stbi_set_flip_vertically_on_load(0);
        unsigned char* rgba = stbi_load_from_memory(data, len, &w, &h, &ch, 4);
        if (!rgba) {
            LOGW_IMG("stbi_load_from_memory failed: %s", stbi_failure_reason());
            return false;
        }

        LOGI_IMG("Decoded network image %dx%d (%d ch) — queuing for GL upload", w, h, ch);

        pending.rgba.assign(rgba, rgba + (size_t)w * h * 4);
        pending.w = w;
        pending.h = h;
        stbi_image_free(rgba);
        return true;
#endif

        return false;
    }

    // ── Loading shimmer placeholder ───────────────────────────────────────────
    void _renderLoading(Painter& painter, FontCache& fontCache,
                        int cx, int cy, int cw, int ch) const
    {
        painter.fillRect(cx, cy, cw, ch, placeholderColor);
        painter.drawTextA("\xe2\x80\xa6",
                          cx, cy, cw, ch,
                          fontCache.getFont(fontSize, fontWeight),
                          Color::fromRGB(160, 160, 160),
                          DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    // ── Destination rect (all backends) ──────────────────────────────────────
    struct DestRect { float x, y, w, h; };

    DestRect _calculateDestRect(int cx, int cy, int cw, int ch) const {
        const float imgAspect       = (float)imageWidth  / (float)imageHeight;
        const float containerAspect = (float)cw          / (float)ch;

        switch (fit) {
            case ImageFit::Fill:
                return { (float)cx, (float)cy, (float)cw, (float)ch };

            case ImageFit::Contain: {
                float dw, dh;
                if (imgAspect > containerAspect) {
                    dw = (float)cw; dh = dw / imgAspect;
                } else {
                    dh = (float)ch; dw = dh * imgAspect;
                }
                return { cx + (cw - dw) * 0.5f, cy + (ch - dh) * 0.5f, dw, dh };
            }

            case ImageFit::Cover: {
                float dw, dh;
                if (imgAspect > containerAspect) {
                    dh = (float)ch; dw = dh * imgAspect;
                } else {
                    dw = (float)cw; dh = dw / imgAspect;
                }
                return { cx + (cw - dw) * 0.5f, cy + (ch - dh) * 0.5f, dw, dh };
            }

            case ImageFit::None:
                return { cx + (cw - imageWidth)  * 0.5f,
                         cy + (ch - imageHeight) * 0.5f,
                         (float)imageWidth, (float)imageHeight };

            case ImageFit::ScaleDown: {
                if (imageWidth <= cw && imageHeight <= ch)
                    return { cx + (cw - imageWidth)  * 0.5f,
                             cy + (ch - imageHeight) * 0.5f,
                             (float)imageWidth, (float)imageHeight };
                float dw, dh;
                if (imgAspect > containerAspect) {
                    dw = (float)cw; dh = dw / imgAspect;
                } else {
                    dh = (float)ch; dw = dh * imgAspect;
                }
                return { cx + (cw - dw) * 0.5f, cy + (ch - dh) * 0.5f, dw, dh };
            }

            default:
                return { (float)cx, (float)cy, (float)cw, (float)ch };
        }
    }


#ifdef _WIN32
    void _renderGDIPlus(GraphicsContext& ctx,
                        int cx, int cy, int cw, int ch) const {
        if (!bitmap) return;

        DestRect d  = _calculateDestRect(cx, cy, cw, ch);
        int dw = std::max(1, (int)d.w);
        int dh = std::max(1, (int)d.h);

        // Rebuild the pre-scaled bitmap when the display size or fit mode changes.
        if (!_scaledBitmap || _scaledW != dw || _scaledH != dh
                            || _scaledFit != (int)fit) {
            _scaledBitmap = std::make_unique<Gdiplus::Bitmap>(dw, dh,
                PixelFormat32bppPARGB);

            if (_scaledBitmap && _scaledBitmap->GetLastStatus() == Gdiplus::Ok) {
                Gdiplus::Graphics sg(_scaledBitmap.get());
                sg.SetInterpolationMode(
                    Gdiplus::InterpolationModeHighQualityBicubic);
                sg.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
                sg.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
                sg.DrawImage(bitmap.get(),
                    Gdiplus::RectF(0.f, 0.f, (float)dw, (float)dh));

                _scaledW   = dw;
                _scaledH   = dh;
                _scaledFit = (int)fit;
            } else {
                // Fallback: clear cache so we try again next frame rather than
                // using a corrupt bitmap.
                _scaledBitmap.reset();
                _scaledW = _scaledH = 0;
                _scaledFit = -1;
            }
        }


        Gdiplus::Bitmap* src = _scaledBitmap ? _scaledBitmap.get() : bitmap.get();
        Gdiplus::RectF   destRect = _scaledBitmap
            ? Gdiplus::RectF(d.x, d.y, (float)dw, (float)dh)
            : Gdiplus::RectF(d.x, d.y, d.w, d.h);

        Gdiplus::Graphics g(ctx.hdc);
        // Nearest-neighbour: the source is already at display resolution.
        g.SetInterpolationMode(_scaledBitmap
            ? Gdiplus::InterpolationModeNearestNeighbor
            : Gdiplus::InterpolationModeHighQualityBicubic);
        g.SetSmoothingMode(_scaledBitmap
            ? Gdiplus::SmoothingModeNone
            : Gdiplus::SmoothingModeHighQuality);

        if (borderRadius > 0) {
            Gdiplus::GraphicsPath path;
            int diam = borderRadius * 2;
            path.AddArc(Gdiplus::Rect(x,                  y,                 diam, diam), 180, 90);
            path.AddArc(Gdiplus::Rect(x + width - diam,   y,                 diam, diam), 270, 90);
            path.AddArc(Gdiplus::Rect(x + width - diam,   y + height - diam, diam, diam), 0,   90);
            path.AddArc(Gdiplus::Rect(x,                  y + height - diam, diam, diam), 90,  90);
            path.CloseFigure();
            g.SetClip(&path);
        } else {
            g.SetClip(Gdiplus::Rect(cx, cy, cw, ch));
        }

        g.DrawImage(src, destRect);
    }
#endif

#if defined(__linux__) && !defined(__ANDROID__)
    void _convertStbiToCairo(unsigned char* data, int w, int h) {
        imageWidth  = w;
        imageHeight = h;
        const size_t nPx = (size_t)w * (size_t)h;
        pixels.resize(nPx * 4);
        for (size_t i = 0; i < nPx; ++i) {
            uint8_t r = data[i*4+0], g = data[i*4+1],
                    b = data[i*4+2], a = data[i*4+3];
            pixels[i*4+0] = (uint8_t)((uint32_t)b * a / 255u);
            pixels[i*4+1] = (uint8_t)((uint32_t)g * a / 255u);
            pixels[i*4+2] = (uint8_t)((uint32_t)r * a / 255u);
            pixels[i*4+3] = a;
        }
    }

    void _renderCairo(GraphicsContext& ctx,
                      int cx, int cy, int cw, int ch) const {
        if (pixels.empty() || !ctx.cr) return;

        DestRect d  = _calculateDestRect(cx, cy, cw, ch);
        int dw = std::max(1, (int)d.w);
        int dh = std::max(1, (int)d.h);

        // Rebuild the pre-scaled surface when display size or fit mode changes.
        if (!_scaledSurface || _scaledW != dw || _scaledH != dh
                             || _scaledFit != (int)fit) {

            // Free any existing cached surface.
            if (_scaledSurface) {
                cairo_surface_destroy(_scaledSurface);
                _scaledSurface = nullptr;
            }

            cairo_surface_t* scaledSurf =
                cairo_image_surface_create(CAIRO_FORMAT_ARGB32, dw, dh);

            if (cairo_surface_status(scaledSurf) == CAIRO_STATUS_SUCCESS) {
                cairo_t* scr = cairo_create(scaledSurf);

                // Temporary view of the source pixels — no copy.
                cairo_surface_t* srcSurf = cairo_image_surface_create_for_data(
                    const_cast<uint8_t*>(pixels.data()),
                    CAIRO_FORMAT_ARGB32,
                    imageWidth, imageHeight, imageWidth * 4);

                if (cairo_surface_status(srcSurf) == CAIRO_STATUS_SUCCESS) {
                    cairo_scale(scr,
                        (double)dw / imageWidth,
                        (double)dh / imageHeight);
                    cairo_set_source_surface(scr, srcSurf, 0.0, 0.0);
                    cairo_pattern_set_filter(cairo_get_source(scr),
                        CAIRO_FILTER_BILINEAR);
                    cairo_paint(scr);
                }

                cairo_surface_destroy(srcSurf);
                cairo_destroy(scr);

                _scaledSurface = scaledSurf;
                _scaledW       = dw;
                _scaledH       = dh;
                _scaledFit     = (int)fit;
            } else {
                // Surface creation failed — release and leave cache empty so
                // we fall back to the original slow path below.
                cairo_surface_destroy(scaledSurf);
            }
        }

        cairo_t* cr = ctx.cr;
        cairo_save(cr);

        if (borderRadius > 0) {
            _cairoRoundedRectPath(cr, x, y, width, height, borderRadius);
            cairo_clip(cr);
        } else {
            cairo_rectangle(cr, cx, cy, cw, ch);
            cairo_clip(cr);
        }

        if (_scaledSurface) {
            // Fast path: blit the pre-scaled surface with no scaling (nearest).
            cairo_set_source_surface(cr, _scaledSurface, d.x, d.y);
            cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_NEAREST);
            cairo_paint(cr);
        } else {
            // Slow fallback: scale from source on every frame.
            cairo_surface_t* srcSurf = cairo_image_surface_create_for_data(
                const_cast<uint8_t*>(pixels.data()),
                CAIRO_FORMAT_ARGB32,
                imageWidth, imageHeight, imageWidth * 4);

            if (cairo_surface_status(srcSurf) == CAIRO_STATUS_SUCCESS) {
                cairo_translate(cr, d.x, d.y);
                cairo_scale(cr,
                    (double)d.w / imageWidth,
                    (double)d.h / imageHeight);
                cairo_set_source_surface(cr, srcSurf, 0.0, 0.0);
                cairo_pattern_set_filter(cairo_get_source(cr),
                    CAIRO_FILTER_BILINEAR);
                cairo_paint(cr);
            }
            cairo_surface_destroy(srcSurf);
        }

        cairo_restore(cr);
    }

    static void _cairoRoundedRectPath(cairo_t* cr,
                                      double rx, double ry,
                                      double rw, double rh,
                                      double radius) {
        double r = radius;
        if (r > rw * 0.5) r = rw * 0.5;
        if (r > rh * 0.5) r = rh * 0.5;
        cairo_new_path(cr);
        cairo_arc(cr, rx+rw-r, ry    +r, r, -M_PI*0.5,  0.0     );
        cairo_arc(cr, rx+rw-r, ry+rh-r, r,  0.0,        M_PI*0.5);
        cairo_arc(cr, rx    +r, ry+rh-r, r,  M_PI*0.5,  M_PI    );
        cairo_arc(cr, rx    +r, ry    +r, r,  M_PI,     -M_PI*0.5);
        cairo_close_path(cr);
    }
#endif

    // =========================================================================
    // Android — NanoVG
    // =========================================================================
#ifdef __ANDROID__

    void _renderNanoVG(int cx, int cy, int cw, int ch) {
        NVGcontext* vg = FluxAndroid_getVG();
        if (!vg) return;

        if (pending.ready()) {
            _deleteNvgTexture();

            uploadBuffer = std::move(pending.rgba);
            uploadW      = pending.w;
            uploadH      = pending.h;
            pending.rgba.clear();
            pending.w = pending.h = 0;

            LOGI_IMG("Uploading RGBA texture %dx%d (%zu bytes)",
                     uploadW, uploadH, uploadBuffer.size());

            nvgImage = nvgCreateImageRGBA(vg, uploadW, uploadH,
                                          NVG_IMAGE_PREMULTIPLIED,
                                          uploadBuffer.data());
            if (nvgImage == -1) {
                LOGW_IMG("nvgCreateImageRGBA FAILED");
                _setLoadState(ImageLoadState::Error);
                return;
            }

            nvgImageSize(vg, nvgImage, &imageWidth, &imageHeight);
            if (imageWidth == 0 || imageHeight == 0) {
                LOGW_IMG("nvgImageSize returned zero — treating as error");
                _deleteNvgTexture();
                _setLoadState(ImageLoadState::Error);
                return;
            }

            LOGI_IMG("Texture ready: %dx%d handle=%d", imageWidth, imageHeight, nvgImage);
            _setLoadState(ImageLoadState::Loaded);
            markNeedsLayout();
        }

        if (nvgImage == -1) return;

        DestRect d = _calculateDestRect(cx, cy, cw, ch);

        NVGpaint paint = nvgImagePattern(vg, d.x, d.y, d.w, d.h,
                                         0.0f, nvgImage, 1.0f);
        nvgSave(vg);
        nvgBeginPath(vg);
        if (borderRadius > 0) {
            nvgRoundedRect(vg, d.x, d.y, d.w, d.h, (float)borderRadius);
        } else {
            nvgRect(vg, d.x, d.y, d.w, d.h);
        }
        nvgFillPaint(vg, paint);
        nvgFill(vg);
        nvgRestore(vg);
    }

    void _deleteNvgTexture() {
        if (nvgImage != -1) {
            NVGcontext* vg = FluxAndroid_getVG();
            if (vg) nvgDeleteImage(vg, nvgImage);
            nvgImage = -1;
        }
    }

    void _freeNvgImage() {
        _deleteNvgTexture();
        pending.rgba.clear();
        pending.w = pending.h = 0;
        uploadBuffer.clear();
        uploadW = uploadH = 0;
    }

#endif  // __ANDROID__
};

// ============================================================================
// FACTORY FUNCTIONS
// ============================================================================

using ImageWidgetPtr = std::shared_ptr<ImageWidget>;

inline ImageWidgetPtr Image(const std::string& path) {
    return std::make_shared<ImageWidget>(path);
}
inline ImageWidgetPtr Image() {
    return std::make_shared<ImageWidget>();
}

inline ImageWidgetPtr NetworkImage(const std::string& url,
                                   bool postToUI = true) {
    auto w = std::make_shared<ImageWidget>();
    w->loadFromUrl(url, postToUI);
    return w;
}

#endif // FLUX_IMAGE_HPP