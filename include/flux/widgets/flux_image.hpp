#ifndef FLUX_IMAGE_HPP
#define FLUX_IMAGE_HPP

#include "../flux_core.hpp"

// ============================================================================
// PLATFORM INCLUDES
// ============================================================================

#ifdef _WIN32
#   include <gdiplus.h>
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
extern NVGcontext* FluxAndroid_getVG();
extern AAssetManager* FluxAndroid_getAssetManager();  // set once in native-lib.cpp
#endif

#include <memory>
#include <string>
#include <iostream>
#include <vector>

// ============================================================================
// IMAGE FIT MODES
// ============================================================================

enum class ImageFit {
    Fill,       // Stretch to fill entire widget (may distort)
    Contain,    // Scale to fit inside widget (maintains aspect ratio, may letterbox)
    Cover,      // Scale to cover entire widget (maintains aspect ratio, may crop)
    None,       // Display at original size, centred
    ScaleDown   // Like None, but scales down if image is larger than widget
};

// ============================================================================
// IMAGE WIDGET
// ============================================================================

class ImageWidget : public Widget {
public:
    // ── Public state ─────────────────────────────────────────────────────────
    std::string imagePath;
    ImageFit    fit              = ImageFit::Contain;
    int         imageWidth       = 0;
    int         imageHeight      = 0;
    bool        imageLoaded      = false;
    bool        hasError         = false;
    Color       placeholderColor = Color::fromRGB(240, 240, 240);
    Color       errorColor       = Color::fromRGB(255, 200, 200);

    // ── Platform pixel storage ────────────────────────────────────────────────
#ifdef _WIN32
    std::unique_ptr<Gdiplus::Bitmap> bitmap;
#endif

#if defined(__linux__) && !defined(__ANDROID__)
    std::vector<uint8_t> pixels; // BGRA premultiplied, Cairo native
#endif

#ifdef __ANDROID__
    // NanoVG image handle — -1 means not loaded.
    // The handle is tied to the NVGcontext, so it must be recreated
    // if the context is destroyed and recreated (APP_CMD_TERM_WINDOW).
    int nvgImage = -1;
#endif

    // ── Constructors ──────────────────────────────────────────────────────────
    ImageWidget() {
        autoWidth  = true;
        autoHeight = true;
    }

    explicit ImageWidget(const std::string &path) : ImageWidget() {
        loadImage(path);
    }

    // ── Destructor ────────────────────────────────────────────────────────────
    ~ImageWidget() override {
#ifdef __ANDROID__
        _freeNvgImage();
#endif
    }

    // ── loadImage ─────────────────────────────────────────────────────────────
    bool loadImage(const std::string &path) {
        if (path.empty()) {
            hasError    = false;
            imageLoaded = false;
            return false;
        }

        imagePath = path;
        hasError  = false;

        // =====================================================================
        // Win32 — GDI+
        // =====================================================================
#ifdef _WIN32
        wchar_t absPath[MAX_PATH] = {};
        std::wstring wpath = toWideString(path);
        if (!GetFullPathNameW(wpath.c_str(), MAX_PATH, absPath, nullptr)) {
            hasError    = true;
            imageLoaded = false;
            return false;
        }

        try {
            bitmap = std::make_unique<Gdiplus::Bitmap>(absPath);
            if (!bitmap || bitmap->GetLastStatus() != Gdiplus::Ok) {
                hasError    = true;
                imageLoaded = false;
                bitmap.reset();
                return false;
            }
        } catch (...) {
            hasError    = true;
            imageLoaded = false;
            bitmap.reset();
            return false;
        }

        imageWidth  = (int)bitmap->GetWidth();
        imageHeight = (int)bitmap->GetHeight();

        if (imageWidth == 0 || imageHeight == 0) {
            hasError    = true;
            imageLoaded = false;
            bitmap.reset();
            return false;
        }
#endif // _WIN32

        // =====================================================================
        // Linux — stb_image + Cairo
        // =====================================================================
#if defined(__linux__) && !defined(__ANDROID__)
        int w = 0, h = 0, channels = 0;
        stbi_set_flip_vertically_on_load(0);
        unsigned char *data = stbi_load(path.c_str(), &w, &h, &channels, 4);

        if (!data) {
            hasError    = true;
            imageLoaded = false;
            pixels.clear();
            return false;
        }

        imageWidth  = w;
        imageHeight = h;

        const size_t nPx = size_t(w) * size_t(h);
        pixels.resize(nPx * 4);
        for (size_t i = 0; i < nPx; ++i) {
            uint8_t r = data[i * 4 + 0];
            uint8_t g = data[i * 4 + 1];
            uint8_t b = data[i * 4 + 2];
            uint8_t a = data[i * 4 + 3];
            uint8_t pr = (uint8_t)((uint32_t)r * a / 255u);
            uint8_t pg = (uint8_t)((uint32_t)g * a / 255u);
            uint8_t pb = (uint8_t)((uint32_t)b * a / 255u);
            pixels[i * 4 + 0] = pb;
            pixels[i * 4 + 1] = pg;
            pixels[i * 4 + 2] = pr;
            pixels[i * 4 + 3] = a;
        }
        stbi_image_free(data);

        if (imageWidth == 0 || imageHeight == 0) {
            hasError    = true;
            imageLoaded = false;
            pixels.clear();
            return false;
        }
#endif // __linux__

        // =====================================================================
        // Android — NanoVG image
        //
        // Two loading strategies:
        //   1. Path starts with '/' → absolute filesystem path
        //      (works for /system/... and app-specific dirs like
        //       getFilesDir(), getExternalFilesDir(), etc.)
        //   2. Otherwise → treat as Android asset path and load via
        //      AAssetManager (files packaged in assets/ folder of the APK)
        // =====================================================================
#ifdef __ANDROID__
        _freeNvgImage(); // release any previous handle

        NVGcontext *vg = FluxAndroid_getVG();
        if (!vg) {
            // NVG not ready — store the path and defer.
            // render() will retry when s_vg becomes available.
            LOGW_IMG("loadImage('%s') before NVG ready — deferred", path.c_str());
            imageLoaded = false;
            return false;
        }

        if (!path.empty() && path[0] == '/') {
            // ── Absolute path ─────────────────────────────────────────────────
            nvgImage = nvgCreateImage(vg, path.c_str(), 0);
            if (nvgImage == -1) {
                LOGW_IMG("nvgCreateImage failed for absolute path: %s", path.c_str());
                hasError    = true;
                imageLoaded = false;
                return false;
            }
        } else {
            // ── Asset path ────────────────────────────────────────────────────
            AAssetManager *am = FluxAndroid_getAssetManager();
            if (!am) {
                LOGW_IMG("AAssetManager not set — cannot load asset: %s", path.c_str());
                hasError    = true;
                imageLoaded = false;
                return false;
            }

            AAsset *asset = AAssetManager_open(am, path.c_str(),
                                               AASSET_MODE_BUFFER);
            if (!asset) {
                LOGW_IMG("AAssetManager_open failed: %s", path.c_str());
                hasError    = true;
                imageLoaded = false;
                return false;
            }

            const void *buf  = AAsset_getBuffer(asset);
            size_t       len  = (size_t)AAsset_getLength(asset);

            nvgImage = nvgCreateImageMem(vg, 0,
                                         (unsigned char*)AAsset_getBuffer(asset),
                                         (int)len);
            AAsset_close(asset);

            if (nvgImage == -1) {
                LOGW_IMG("nvgCreateImageMem failed for asset: %s", path.c_str());
                hasError    = true;
                imageLoaded = false;
                return false;
            }
        }

        // Query actual dimensions from NanoVG
        nvgImageSize(vg, nvgImage, &imageWidth, &imageHeight);
        LOGI_IMG("Loaded '%s' handle=%d  %dx%d",
                 path.c_str(), nvgImage, imageWidth, imageHeight);

        if (imageWidth == 0 || imageHeight == 0) {
            LOGW_IMG("Zero dimensions for: %s", path.c_str());
            hasError    = true;
            imageLoaded = false;
            _freeNvgImage();
            return false;
        }
#endif // __ANDROID__

        imageLoaded = true;
        markNeedsLayout();
        return true;
    }

    // ── reloadIfDeferred — call after NVG context becomes available ───────────
    // In native-lib.cpp, after nvgCreateGLES2(), call:
    //   yourImageWidget->reloadIfDeferred();
    // or simply call rebuild() on FluxUI which re-runs loadImage via
    // the widget's constructor / setImagePath chain.
    void reloadIfDeferred() {
#ifdef __ANDROID__
        if (!imagePath.empty() && !imageLoaded && !hasError)
            loadImage(imagePath);
#endif
    }

    // ── Layout ────────────────────────────────────────────────────────────────
    void computeLayout(GraphicsContext & /*ctx*/,
                       const BoxConstraints &constraints,
                       FontCache & /*fontCache*/) override {
        if (imageLoaded) {
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
    void render(GraphicsContext &ctx, FontCache &fontCache) override {
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
        // Retry deferred load now that NVG is certainly available during render
        if (!imageLoaded && !hasError && !imagePath.empty())
            loadImage(imagePath);
#endif

        if (imageLoaded) {
#ifdef _WIN32
            _renderGDIPlus(ctx, cx, cy, cw, ch);
#elif defined(__ANDROID__)
            _renderNanoVG(cx, cy, cw, ch);
#else
            _renderCairo(ctx, cx, cy, cw, ch);
#endif
        } else if (hasError) {
            painter.fillRect(cx, cy, cw, ch, errorColor);
            painter.drawTextA("x", cx, cy, cw, ch,
                              fontCache.getFont(fontSize, fontWeight),
                              Color::fromRGB(150, 0, 0),
                              DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        } else {
            painter.fillRect(cx, cy, cw, ch, placeholderColor);
            painter.drawTextA("[ ]", cx, cy, cw, ch,
                              fontCache.getFont(fontSize, fontWeight),
                              Color::fromRGB(180, 180, 180),
                              DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }

        needsPaint = false;
    }

    // ── Fluent setters ────────────────────────────────────────────────────────

    std::shared_ptr<ImageWidget> setImagePath(const std::string &path) {
        loadImage(path);
        return std::static_pointer_cast<ImageWidget>(shared_from_this());
    }
    std::shared_ptr<ImageWidget> setFit(ImageFit fitMode) {
        fit = fitMode;
        markNeedsPaint();
        return std::static_pointer_cast<ImageWidget>(shared_from_this());
    }
    std::shared_ptr<ImageWidget> setPlaceholderColor(Color color) {
        placeholderColor = color;
        markNeedsPaint();
        return std::static_pointer_cast<ImageWidget>(shared_from_this());
    }
    std::shared_ptr<ImageWidget> setErrorColor(Color color) {
        errorColor = color;
        markNeedsPaint();
        return std::static_pointer_cast<ImageWidget>(shared_from_this());
    }
    std::shared_ptr<ImageWidget> setWidth(int w) {
        width = w; autoWidth = false; markNeedsLayout();
        return std::static_pointer_cast<ImageWidget>(shared_from_this());
    }
    std::shared_ptr<ImageWidget> setHeight(int h) {
        height = h; autoHeight = false; markNeedsLayout();
        return std::static_pointer_cast<ImageWidget>(shared_from_this());
    }
    std::shared_ptr<ImageWidget> setBorderRadius(int r) {
        borderRadius = r; markNeedsPaint();
        return std::static_pointer_cast<ImageWidget>(shared_from_this());
    }
    std::shared_ptr<ImageWidget> setPadding(int p) {
        padding = paddingLeft = paddingRight = paddingTop = paddingBottom = p;
        markNeedsLayout();
        return std::static_pointer_cast<ImageWidget>(shared_from_this());
    }

private:
    // ── Destination rect (shared by all backends) ─────────────────────────────
    struct DestRect { float x, y, w, h; };

    DestRect _calculateDestRect(int cx, int cy, int cw, int ch) const {
        const float imgAspect       = (float)imageWidth  / (float)imageHeight;
        const float containerAspect = (float)cw          / (float)ch;

        switch (fit) {
            case ImageFit::Fill:
                return { (float)cx, (float)cy, (float)cw, (float)ch };

            case ImageFit::Contain: {
                float scale, dw, dh;
                if (imgAspect > containerAspect) {
                    scale = (float)cw / (float)imageWidth;  dw = (float)cw;
                    dh    = (float)imageHeight * scale;
                } else {
                    scale = (float)ch / (float)imageHeight; dh = (float)ch;
                    dw    = (float)imageWidth * scale;
                }
                return { cx + (cw - dw) * 0.5f, cy + (ch - dh) * 0.5f, dw, dh };
            }

            case ImageFit::Cover: {
                float scale, dw, dh;
                if (imgAspect > containerAspect) {
                    scale = (float)ch / (float)imageHeight; dw = (float)imageWidth * scale;
                    dh    = (float)ch;
                } else {
                    scale = (float)cw / (float)imageWidth;  dw = (float)cw;
                    dh    = (float)imageHeight * scale;
                }
                return { cx + (cw - dw) * 0.5f, cy + (ch - dh) * 0.5f, dw, dh };
            }

            case ImageFit::None:
                return { cx + (cw - imageWidth) * 0.5f,
                         cy + (ch - imageHeight) * 0.5f,
                         (float)imageWidth, (float)imageHeight };

            case ImageFit::ScaleDown: {
                if (imageWidth <= cw && imageHeight <= ch)
                    return { cx + (cw - imageWidth) * 0.5f,
                             cy + (ch - imageHeight) * 0.5f,
                             (float)imageWidth, (float)imageHeight };
                float scale, dw, dh;
                if (imgAspect > containerAspect) {
                    scale = (float)cw / (float)imageWidth;  dw = (float)cw;
                    dh    = (float)imageHeight * scale;
                } else {
                    scale = (float)ch / (float)imageHeight; dh = (float)ch;
                    dw    = (float)imageWidth * scale;
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
    void _renderGDIPlus(GraphicsContext &ctx,
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
            path.AddArc(Gdiplus::Rect(x, y, diam, diam), 180, 90);
            path.AddArc(Gdiplus::Rect(x + width - diam, y, diam, diam), 270, 90);
            path.AddArc(Gdiplus::Rect(x + width - diam, y + height - diam, diam, diam), 0, 90);
            path.AddArc(Gdiplus::Rect(x, y + height - diam, diam, diam), 90, 90);
            path.CloseFigure();
            g.SetClip(&path);
        } else {
            g.SetClip(Gdiplus::Rect(cx, cy, cw, ch));
        }
        g.DrawImage(bitmap.get(), destRect);
    }
#endif // _WIN32

    // =========================================================================
    // Linux — Cairo
    // =========================================================================
#if defined(__linux__) && !defined(__ANDROID__)
    void _renderCairo(GraphicsContext &ctx,
                      int cx, int cy, int cw, int ch) const {
        if (pixels.empty() || !ctx.cr) return;
        cairo_t *cr = ctx.cr;
        cairo_save(cr);

        if (borderRadius > 0) {
            _cairoRoundedRectPath(cr, x, y, width, height, borderRadius);
            cairo_clip(cr);
        } else {
            cairo_rectangle(cr, cx, cy, cw, ch);
            cairo_clip(cr);
        }

        cairo_surface_t *surf = cairo_image_surface_create_for_data(
            const_cast<uint8_t *>(pixels.data()),
            CAIRO_FORMAT_ARGB32, imageWidth, imageHeight, imageWidth * 4);

        if (cairo_surface_status(surf) == CAIRO_STATUS_SUCCESS) {
            DestRect d = _calculateDestRect(cx, cy, cw, ch);
            double sx = d.w / (double)imageWidth;
            double sy = d.h / (double)imageHeight;
            cairo_translate(cr, d.x, d.y);
            cairo_scale(cr, sx, sy);
            cairo_set_source_surface(cr, surf, 0.0, 0.0);
            cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_NEAREST);
            cairo_paint(cr);
        }
        cairo_surface_destroy(surf);
        cairo_restore(cr);
    }

    static void _cairoRoundedRectPath(cairo_t *cr,
                                      double rx, double ry,
                                      double rw, double rh,
                                      double radius) {
        double r = radius;
        if (r > rw * 0.5) r = rw * 0.5;
        if (r > rh * 0.5) r = rh * 0.5;
        cairo_new_path(cr);
        cairo_arc(cr, rx + rw - r, ry      + r, r, -M_PI * 0.5,  0.0);
        cairo_arc(cr, rx + rw - r, ry + rh - r, r,  0.0,          M_PI * 0.5);
        cairo_arc(cr, rx      + r, ry + rh - r, r,  M_PI * 0.5,   M_PI);
        cairo_arc(cr, rx      + r, ry      + r, r,  M_PI,         -M_PI * 0.5);
        cairo_close_path(cr);
    }
#endif // __linux__

    // =========================================================================
    // Android — NanoVG
    //
    // NanoVG renders images as paint patterns applied to a path.
    // nvgImagePattern() maps the image onto a rectangle, then nvgFill()
    // paints it. This is the standard NanoVG image rendering idiom.
    // =========================================================================
#ifdef __ANDROID__
    void _renderNanoVG(int cx, int cy, int cw, int ch) const {
        if (nvgImage == -1) return;
        NVGcontext *vg = FluxAndroid_getVG();
        if (!vg) return;

        DestRect d = _calculateDestRect(cx, cy, cw, ch);

        // nvgImagePattern maps the image so that (ox,oy) is the top-left
        // corner of the image, (iw,ih) is its rendered size, angle=0, alpha=1.
        NVGpaint paint = nvgImagePattern(vg,
                                         d.x, d.y,   // origin
                                         d.w, d.h,   // rendered size
                                         0.0f,        // rotation
                                         nvgImage,
                                         1.0f);       // alpha

        nvgSave(vg);

        // Optional rounded-rect clip
        if (borderRadius > 0) {
            nvgBeginPath(vg);
            nvgRoundedRect(vg, (float)x, (float)y,
                           (float)width, (float)height,
                           (float)borderRadius);
            nvgPathWinding(vg, NVG_SOLID);
            nvgScissor(vg, (float)x, (float)y,
                       (float)width, (float)height);
        }

        nvgBeginPath(vg);
        nvgRect(vg, d.x, d.y, d.w, d.h);
        nvgFillPaint(vg, paint);
        nvgFill(vg);

        nvgRestore(vg);
    }

    void _freeNvgImage() {
        if (nvgImage != -1) {
            NVGcontext *vg = FluxAndroid_getVG();
            if (vg) nvgDeleteImage(vg, nvgImage);
            nvgImage = -1;
        }
    }
#endif // __ANDROID__
};

// ============================================================================
// FACTORY FUNCTIONS
// ============================================================================

using ImageWidgetPtr = std::shared_ptr<ImageWidget>;

inline ImageWidgetPtr Image(const std::string &path) {
    return std::make_shared<ImageWidget>(path);
}
inline ImageWidgetPtr Image() {
    return std::make_shared<ImageWidget>();
}

#endif // FLUX_IMAGE_HPP