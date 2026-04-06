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
    Loading,    // URL fetch in progress
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
    int            imageWidth       = 0;
    int            imageHeight      = 0;
    Color          placeholderColor = Color::fromRGB(240, 240, 240);
    Color          errorColor       = Color::fromRGB(255, 200, 200);
    ImageLoadState loadState        = ImageLoadState::Idle;

    // Keep these for back-compat but derive from loadState
    bool imageLoaded = false;
    bool hasError    = false;

    // ── Platform pixel storage ────────────────────────────────────────────────
#ifdef _WIN32
    std::unique_ptr<Gdiplus::Bitmap> bitmap;
#endif
#if defined(__linux__) && !defined(__ANDROID__)
    std::vector<uint8_t> pixels;
#endif
#ifdef __ANDROID__
    int                  nvgImage         = -1;
    // Raw compressed bytes received from network, consumed on the render (GL) thread
    std::vector<uint8_t> pendingImageBytes;
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
#ifdef __ANDROID__
        _freeNvgImage();
#endif
    }

    // =========================================================================
    // loadFromUrl — fetch bytes over HTTP then decode in-memory
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
            // On Android, _decodeFromMemory stores bytes for GL-thread upload.
            // State stays Loading — _renderNanoVG will promote to Loaded once
            // nvgCreateImageMem succeeds on the render thread.
            // On other platforms ok==true means fully decoded, so set Loaded.
#ifndef __ANDROID__
            if (ok) self->_setLoadState(ImageLoadState::Loaded);
#endif
            if (auto* ui = FluxUI::getCurrentInstance())
                ui->partialRebuild(self.get());
            else
                self->markNeedsLayout();

        }, postToUI);
    }

    // =========================================================================
    // loadImage — existing file-based loader (unchanged behaviour)
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
            nvgImage = nvgCreateImageMem(vg, 0,
                                         (unsigned char*)AAsset_getBuffer(asset), (int)AAsset_getLength(asset));
            AAsset_close(asset);
        }
        if (nvgImage == -1) { _setLoadState(ImageLoadState::Error); return false; }
        nvgImageSize(vg, nvgImage, &imageWidth, &imageHeight);
        if (imageWidth == 0 || imageHeight == 0) {
            _freeNvgImage(); _setLoadState(ImageLoadState::Error); return false;
        }
#endif

        _setLoadState(ImageLoadState::Loaded);
        markNeedsLayout();
        return true;
    }

    void reloadIfDeferred() {
#ifdef __ANDROID__
        if (!imagePath.empty() && loadState == ImageLoadState::Idle)
            loadImage(imagePath);
#endif
    }

    // ── Layout ────────────────────────────────────────────────────────────────
    void computeLayout(GraphicsContext& /*ctx*/,
                       const BoxConstraints& constraints,
                       FontCache& /*fontCache*/) override
    {
        if (loadState == ImageLoadState::Loaded && imageWidth > 0 && imageHeight > 0) {
            if (autoWidth && autoHeight) {
                width  = imageWidth;
                height = imageHeight;
            } else if (autoWidth) {
                float aspect = (float)imageWidth / (float)imageHeight;
                width = (int)((float)height * aspect);
            } else if (autoHeight) {
                float aspect = (float)imageHeight / (float)imageWidth;
                height = (int)((float)width * aspect);
            }
        } else {
            if (autoWidth)  width  = 100;
            if (autoHeight) height = 100;
        }

        width  = constraints.clampWidth(width   + paddingLeft + paddingRight);
        height = constraints.clampHeight(height + paddingTop  + paddingBottom);
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
                                           borderRadius * 2, getCurrentBorderColor(), borderWidth);

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
                // May have pending bytes ready to upload on the GL thread.
                // _renderNanoVG will promote state to Loaded if upload succeeds,
                // otherwise falls through and shows the loading placeholder.
                if (!pendingImageBytes.empty()) {
                    _renderNanoVG(cx, cy, cw, ch);
                    if (loadState == ImageLoadState::Loaded) break;
                }
#endif
                _renderLoading(painter, fontCache, cx, cy, cw, ch);
                break;

            case ImageLoadState::Error:
                painter.fillRect(cx, cy, cw, ch, errorColor);
                painter.drawTextA("✕", cx, cy, cw, ch,
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
        loadImage(path);
        return self();
    }
    std::shared_ptr<ImageWidget> setUrl(const std::string& url,
                                        bool postToUI = true) {
        loadFromUrl(url, postToUI);
        return self();
    }
    std::shared_ptr<ImageWidget> setFit(ImageFit fitMode) {
        fit = fitMode; markNeedsPaint(); return self();
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

    // =========================================================================
    // _decodeFromMemory — shared by loadFromUrl across all platforms
    // =========================================================================
    bool _decodeFromMemory(const uint8_t* data, int len) {
#ifdef _WIN32
        // GDI+: wrap bytes in an IStream, then create Bitmap from it
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
        return (imageWidth > 0 && imageHeight > 0);
#endif

#if defined(__linux__) && !defined(__ANDROID__)
        int w = 0, h = 0, channels = 0;
        stbi_set_flip_vertically_on_load(0);
        unsigned char* decoded = stbi_load_from_memory(data, len, &w, &h, &channels, 4);
        if (!decoded) { pixels.clear(); return false; }
        _convertStbiToCairo(decoded, w, h);
        stbi_image_free(decoded);
        return (imageWidth > 0 && imageHeight > 0);
#endif

#ifdef __ANDROID__
        // ── IMPORTANT ──────────────────────────────────────────────────────────
        // We are on the HTTP callback (background) thread here.
        // nvgCreateImageMem() requires the GL/render thread, so we must NOT
        // call it now. Instead, store the raw compressed bytes and let
        // _renderNanoVG() upload the texture on the next render pass.
        _freeNvgImage();
        pendingImageBytes.assign(data, data + len);
        // imageWidth/imageHeight are unknown until NVG decodes on render thread;
        // they will be filled in by _renderNanoVG() when it flushes the pending bytes.
        imageWidth  = 0;
        imageHeight = 0;
        return true;   // optimistically succeed
#endif

        return false;
    }

    // ── Loading spinner / shimmer placeholder ─────────────────────────────────
    void _renderLoading(Painter& painter, FontCache& fontCache,
                        int cx, int cy, int cw, int ch) const
    {
        // Shimmer-style grey placeholder
        painter.fillRect(cx, cy, cw, ch, placeholderColor);

        // Centred "…" label
        painter.drawTextA("\xe2\x80\xa6",  // UTF-8 ellipsis
                          cx, cy, cw, ch,
                          fontCache.getFont(fontSize, fontWeight),
                          Color::fromRGB(160, 160, 160),
                          DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    // ── Destination rect (shared by all backends) ─────────────────────────────
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
                    dw = (float)cw;
                    dh = dw / imgAspect;
                } else {
                    dh = (float)ch;
                    dw = dh * imgAspect;
                }
                return { cx + (cw - dw) * 0.5f, cy + (ch - dh) * 0.5f, dw, dh };
            }

            case ImageFit::Cover: {
                float dw, dh;
                if (imgAspect > containerAspect) {
                    dh = (float)ch;
                    dw = dh * imgAspect;
                } else {
                    dw = (float)cw;
                    dh = dw / imgAspect;
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

    // =========================================================================
    // Win32 — GDI+
    // =========================================================================
#ifdef _WIN32
    void _renderGDIPlus(GraphicsContext& ctx,
                        int cx, int cy, int cw, int ch) const {
        if (!bitmap) return;
        Gdiplus::Graphics g(ctx.hdc);
        g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        g.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);

        DestRect d = _calculateDestRect(cx, cy, cw, ch);
        Gdiplus::RectF destRect(d.x, d.y, d.w, d.h);

        if (borderRadius > 0) {
            Gdiplus::GraphicsPath path;
            int diam = borderRadius * 2;
            path.AddArc(Gdiplus::Rect(x,             y,              diam, diam), 180, 90);
            path.AddArc(Gdiplus::Rect(x + width - diam, y,           diam, diam), 270, 90);
            path.AddArc(Gdiplus::Rect(x + width - diam, y+height-diam, diam, diam), 0, 90);
            path.AddArc(Gdiplus::Rect(x,             y + height-diam, diam, diam), 90, 90);
            path.CloseFigure();
            g.SetClip(&path);
        } else {
            g.SetClip(Gdiplus::Rect(cx, cy, cw, ch));
        }
        g.DrawImage(bitmap.get(), destRect);
    }
#endif

    // =========================================================================
    // Linux — Cairo
    // =========================================================================
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
        cairo_t* cr = ctx.cr;
        cairo_save(cr);

        if (borderRadius > 0) {
            _cairoRoundedRectPath(cr, x, y, width, height, borderRadius);
            cairo_clip(cr);
        } else {
            cairo_rectangle(cr, cx, cy, cw, ch);
            cairo_clip(cr);
        }

        cairo_surface_t* surf = cairo_image_surface_create_for_data(
            const_cast<uint8_t*>(pixels.data()),
            CAIRO_FORMAT_ARGB32, imageWidth, imageHeight, imageWidth * 4);

        if (cairo_surface_status(surf) == CAIRO_STATUS_SUCCESS) {
            DestRect d = _calculateDestRect(cx, cy, cw, ch);
            cairo_translate(cr, d.x, d.y);
            cairo_scale(cr, d.w / (double)imageWidth,
                            d.h / (double)imageHeight);
            cairo_set_source_surface(cr, surf, 0.0, 0.0);
            cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BILINEAR);
            cairo_paint(cr);
        }
        cairo_surface_destroy(surf);
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
        cairo_arc(cr, rx+rw-r, ry    +r, r, -M_PI*0.5,  0.0      );
        cairo_arc(cr, rx+rw-r, ry+rh-r, r,  0.0,         M_PI*0.5 );
        cairo_arc(cr, rx    +r, ry+rh-r, r,  M_PI*0.5,   M_PI     );
        cairo_arc(cr, rx    +r, ry    +r, r,  M_PI,      -M_PI*0.5 );
        cairo_close_path(cr);
    }
#endif

    // =========================================================================
    // Android — NanoVG
    // =========================================================================
#ifdef __ANDROID__

    // Persistent buffer to ensure memory stays valid for NanoVG
    std::vector<uint8_t> uploadBuffer;

    void _renderNanoVG(int cx, int cy, int cw, int ch) {
        NVGcontext* vg = FluxAndroid_getVG();
        if (!vg) return;

        // ── Upload pending network image on GL thread ───────────────────────
        if (!pendingImageBytes.empty()) {
            // Move bytes safely first
            uploadBuffer = std::move(pendingImageBytes);
            pendingImageBytes.clear();

            LOGI_IMG("Uploading image, size: %zu bytes", uploadBuffer.size());

            if (uploadBuffer.size() >= 4) {
                LOGI_IMG("Header: %02X %02X %02X %02X",
                         uploadBuffer[0], uploadBuffer[1],
                         uploadBuffer[2], uploadBuffer[3]);
            }

            _deleteNvgTexture();

            // IMPORTANT: use PREMULTIPLIED flag
            nvgImage = nvgCreateImageMem(
                    vg,
                    NVG_IMAGE_PREMULTIPLIED,
                    uploadBuffer.data(),
                    (int)uploadBuffer.size()
            );

            if (nvgImage == -1) {
                LOGW_IMG("nvgCreateImageMem FAILED");
                _setLoadState(ImageLoadState::Error);
                return;
            }

            nvgImageSize(vg, nvgImage, &imageWidth, &imageHeight);

            LOGI_IMG("Image uploaded: %d x %d", imageWidth, imageHeight);

            if (imageWidth == 0 || imageHeight == 0) {
                LOGW_IMG("Invalid image size");
                _deleteNvgTexture();
                _setLoadState(ImageLoadState::Error);
                return;
            }

            _setLoadState(ImageLoadState::Loaded);
            markNeedsLayout();
        }

        if (nvgImage == -1) return;

        DestRect d = _calculateDestRect(cx, cy, cw, ch);

        LOGI_IMG("Draw rect: x=%f y=%f w=%f h=%f", d.x, d.y, d.w, d.h);

        NVGpaint paint = nvgImagePattern(
                vg,
                d.x, d.y,
                d.w, d.h,
                0.0f,
                nvgImage,
                1.0f
        );

        nvgSave(vg);

        nvgBeginPath(vg);

        // ⚠️ Use DEST rect for clipping (important fix)
        if (borderRadius > 0) {
            nvgRoundedRect(vg, d.x, d.y, d.w, d.h, (float)borderRadius);
        } else {
            nvgRect(vg, d.x, d.y, d.w, d.h);
        }

        nvgFillPaint(vg, paint);
        nvgFill(vg);

        nvgRestore(vg);
    }

    // ── Separate GPU cleanup (DO NOT clear pending bytes here) ───────────────
    void _deleteNvgTexture() {
        if (nvgImage != -1) {
            NVGcontext* vg = FluxAndroid_getVG();
            if (vg) {
                nvgDeleteImage(vg, nvgImage);
            }
            nvgImage = -1;
        }
    }

    void _freeNvgImage() {
        _deleteNvgTexture();
        pendingImageBytes.clear();
        uploadBuffer.clear();
    }

#endif
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

// Url-loaded image — shows placeholder while fetching, then the image
inline ImageWidgetPtr NetworkImage(const std::string& url,
                                   bool postToUI = true) {
    auto w = std::make_shared<ImageWidget>();
    w->loadFromUrl(url, postToUI);
    return w;
}

#endif