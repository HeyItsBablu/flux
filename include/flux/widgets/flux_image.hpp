#ifndef FLUX_IMAGE_HPP
#define FLUX_IMAGE_HPP

#include "../flux_core.hpp"

// ============================================================================
// PLATFORM INCLUDES
// ============================================================================

#ifdef _WIN32
#   include <gdiplus.h>
#endif

#ifdef __linux__
#   include <cairo/cairo.h>
#   define STBI_ONLY_JPEG
#   define STBI_ONLY_PNG
#   define STBI_ONLY_BMP
#   define STBI_ONLY_TGA
#   include "stb_image.h"   // ← declarations only, no STB_IMAGE_IMPLEMENTATION
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
    ImageFit    fit             = ImageFit::Contain;
    int         imageWidth      = 0;
    int         imageHeight     = 0;
    bool        imageLoaded     = false;
    bool        hasError        = false;
    Color       placeholderColor = Color::fromRGB(240, 240, 240);
    Color       errorColor       = Color::fromRGB(255, 200, 200);

    // ── Platform pixel storage ────────────────────────────────────────────────
#ifdef _WIN32
    std::unique_ptr<Gdiplus::Bitmap> bitmap;
#endif

#ifdef __linux__
    // RGBA pixels decoded by stb_image, kept alive for painting.
    std::vector<uint8_t> pixels; // imageWidth * imageHeight * 4 bytes, RGBA
#endif

    // ── Constructors ──────────────────────────────────────────────────────────
    ImageWidget() {
        autoWidth  = true;
        autoHeight = true;
    }

    explicit ImageWidget(const std::string &path) : ImageWidget() {
        loadImage(path);
    }

    // ── loadImage ─────────────────────────────────────────────────────────────
    bool loadImage(const std::string &path) {
        if (path.empty()) {
            std::cout << "[ImageWidget] loadImage: empty path\n";
            hasError    = false;
            imageLoaded = false;
            return false;
        }

        imagePath = path;
        hasError  = false;

#ifdef _WIN32
        // ── Windows: GDI+ path (unchanged) ───────────────────────────────────
        wchar_t absPath[MAX_PATH] = {};
        std::wstring wpath = toWideString(path);
        if (!GetFullPathNameW(wpath.c_str(), MAX_PATH, absPath, nullptr)) {
            std::cout << "[ImageWidget] GetFullPathNameW failed for: " << path << "\n";
            hasError    = true;
            imageLoaded = false;
            return false;
        }

        std::wstring resolvedW(absPath);
        std::string  resolved(resolvedW.begin(), resolvedW.end());
        std::cout << "[ImageWidget] loading: " << resolved << "\n";

        try {
            bitmap = std::make_unique<Gdiplus::Bitmap>(absPath);

            if (!bitmap) {
                std::cout << "[ImageWidget] Bitmap allocation failed: " << resolved << "\n";
                hasError    = true;
                imageLoaded = false;
                return false;
            }

            Gdiplus::Status status = bitmap->GetLastStatus();
            if (status != Gdiplus::Ok) {
                std::cout << "[ImageWidget] GDI+ status " << (int)status
                          << " for: " << resolved << "\n";
                hasError    = true;
                imageLoaded = false;
                bitmap.reset();
                return false;
            }
        } catch (const std::exception &e) {
            std::cout << "[ImageWidget] exception: " << e.what()
                      << " loading: " << resolved << "\n";
            hasError    = true;
            imageLoaded = false;
            bitmap.reset();
            return false;
        } catch (...) {
            std::cout << "[ImageWidget] unknown exception loading: " << resolved << "\n";
            hasError    = true;
            imageLoaded = false;
            bitmap.reset();
            return false;
        }

        imageWidth  = (int)bitmap->GetWidth();
        imageHeight = (int)bitmap->GetHeight();

        std::cout << "[ImageWidget] loaded OK: " << resolved
                  << "  " << imageWidth << "x" << imageHeight << "\n";

        if (imageWidth == 0 || imageHeight == 0) {
            std::cout << "[ImageWidget] zero dimensions, rejecting: " << resolved << "\n";
            hasError    = true;
            imageLoaded = false;
            bitmap.reset();
            return false;
        }
#endif // _WIN32

#ifdef __linux__
        // ── Linux: stb_image decode ───────────────────────────────────────────
        std::cout << "[ImageWidget] loading: " << path << "\n";

        int w = 0, h = 0, channels = 0;
        // Force 4-channel RGBA output regardless of source format.
        stbi_set_flip_vertically_on_load(0); // Cairo origin is top-left, same as stb default
        unsigned char *data = stbi_load(path.c_str(), &w, &h, &channels, 4);

        if (!data) {
            std::cout << "[ImageWidget] stb_image failed: "
                      << stbi_failure_reason() << " — " << path << "\n";
            hasError    = true;
            imageLoaded = false;
            pixels.clear();
            return false;
        }

        imageWidth  = w;
        imageHeight = h;

        // Cairo expects BGRA (premultiplied) for CAIRO_FORMAT_ARGB32.
        // stb gives us RGBA (straight alpha).  Swap R<->B and premultiply.
        const size_t nPx = size_t(w) * size_t(h);
        pixels.resize(nPx * 4);
        for (size_t i = 0; i < nPx; ++i) {
            uint8_t r = data[i * 4 + 0];
            uint8_t g = data[i * 4 + 1];
            uint8_t b = data[i * 4 + 2];
            uint8_t a = data[i * 4 + 3];

            // Premultiply alpha
            uint8_t pr = (uint8_t)((uint32_t)r * a / 255u);
            uint8_t pg = (uint8_t)((uint32_t)g * a / 255u);
            uint8_t pb = (uint8_t)((uint32_t)b * a / 255u);

            // Store as BGRA (Cairo native byte order on little-endian)
            pixels[i * 4 + 0] = pb;
            pixels[i * 4 + 1] = pg;
            pixels[i * 4 + 2] = pr;
            pixels[i * 4 + 3] = a;
        }

        stbi_image_free(data);

        std::cout << "[ImageWidget] loaded OK: " << path
                  << "  " << imageWidth << "x" << imageHeight << "\n";

        if (imageWidth == 0 || imageHeight == 0) {
            std::cout << "[ImageWidget] zero dimensions, rejecting: " << path << "\n";
            hasError    = true;
            imageLoaded = false;
            pixels.clear();
            return false;
        }
#endif // __linux__

        imageLoaded = true;
        markNeedsLayout();
        return true;
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

        const int contentX      = x + paddingLeft;
        const int contentY      = y + paddingTop;
        const int contentWidth  = width  - paddingLeft - paddingRight;
        const int contentHeight = height - paddingTop  - paddingBottom;

        if (imageLoaded) {
#ifdef _WIN32
            _renderGDIPlus(ctx, contentX, contentY, contentWidth, contentHeight);
#endif
#ifdef __linux__
            _renderCairo(ctx, contentX, contentY, contentWidth, contentHeight);
#endif
        } else if (hasError) {
            painter.fillRect(contentX, contentY, contentWidth, contentHeight, errorColor);
            painter.drawTextA("x",
                              contentX, contentY, contentWidth, contentHeight,
                              fontCache.getFont(fontSize, fontWeight),
                              Color::fromRGB(150, 0, 0),
                              DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        } else {
            painter.fillRect(contentX, contentY, contentWidth, contentHeight, placeholderColor);
            painter.drawTextA("[ ]",
                              contentX, contentY, contentWidth, contentHeight,
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
        if (width != w) {
            width     = w;
            autoWidth = false;
            markNeedsLayout();
        }
        return std::static_pointer_cast<ImageWidget>(shared_from_this());
    }

    std::shared_ptr<ImageWidget> setHeight(int h) {
        if (height != h) {
            height     = h;
            autoHeight = false;
            markNeedsLayout();
        }
        return std::static_pointer_cast<ImageWidget>(shared_from_this());
    }

    std::shared_ptr<ImageWidget> setBorderRadius(int r) {
        if (borderRadius != r) {
            borderRadius = r;
            markNeedsPaint();
        }
        return std::static_pointer_cast<ImageWidget>(shared_from_this());
    }

    std::shared_ptr<ImageWidget> setPadding(int p) {
        padding = p;
        paddingLeft = paddingRight = paddingTop = paddingBottom = p;
        markNeedsLayout();
        return std::static_pointer_cast<ImageWidget>(shared_from_this());
    }

private:
    // ── Destination rect calculation (shared by both backends) ────────────────
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
                scale = (float)cw / (float)imageWidth;
                dw    = (float)cw;
                dh    = (float)imageHeight * scale;
            } else {
                scale = (float)ch / (float)imageHeight;
                dw    = (float)imageWidth * scale;
                dh    = (float)ch;
            }
            return { cx + (cw - dw) * 0.5f, cy + (ch - dh) * 0.5f, dw, dh };
        }

        case ImageFit::Cover: {
            float scale, dw, dh;
            if (imgAspect > containerAspect) {
                scale = (float)ch / (float)imageHeight;
                dw    = (float)imageWidth * scale;
                dh    = (float)ch;
            } else {
                scale = (float)cw / (float)imageWidth;
                dw    = (float)cw;
                dh    = (float)imageHeight * scale;
            }
            return { cx + (cw - dw) * 0.5f, cy + (ch - dh) * 0.5f, dw, dh };
        }

        case ImageFit::None:
            return { cx + (cw - imageWidth)  * 0.5f,
                     cy + (ch - imageHeight) * 0.5f,
                     (float)imageWidth, (float)imageHeight };

        case ImageFit::ScaleDown: {
            if (imageWidth <= cw && imageHeight <= ch) {
                return { cx + (cw - imageWidth)  * 0.5f,
                         cy + (ch - imageHeight) * 0.5f,
                         (float)imageWidth, (float)imageHeight };
            }
            // Fall through to Contain behaviour
            float scale, dw, dh;
            if (imgAspect > containerAspect) {
                scale = (float)cw / (float)imageWidth;
                dw    = (float)cw;
                dh    = (float)imageHeight * scale;
            } else {
                scale = (float)ch / (float)imageHeight;
                dw    = (float)imageWidth * scale;
                dh    = (float)ch;
            }
            return { cx + (cw - dw) * 0.5f, cy + (ch - dh) * 0.5f, dw, dh };
        }

        default:
            return { (float)cx, (float)cy, (float)cw, (float)ch };
        }
    }

    // =========================================================================
    // WINDOWS — GDI+ renderer (unchanged logic, just moved here)
    // =========================================================================
#ifdef _WIN32
    void _renderGDIPlus(GraphicsContext &ctx,
                        int cx, int cy, int cw, int ch) const {
        if (!bitmap) return;

        Gdiplus::Graphics graphics(ctx.hdc);
        graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);

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
            graphics.SetClip(&path);
        } else {
            graphics.SetClip(Gdiplus::Rect(cx, cy, cw, ch));
        }

        graphics.DrawImage(bitmap.get(), destRect);
    }
#endif // _WIN32

    // =========================================================================
    // LINUX — Cairo + stb_image renderer
    // =========================================================================
#ifdef __linux__
    void _renderCairo(GraphicsContext &ctx,
                      int cx, int cy, int cw, int ch) const {
        if (pixels.empty() || !ctx.cr) return;

        cairo_t *cr = ctx.cr;

        cairo_save(cr);

        // ── 1. Optional rounded-rect clip ────────────────────────────────────
        if (borderRadius > 0) {
            _cairoRoundedRectPath(cr, (double)x, (double)y,
                                  (double)width, (double)height,
                                  (double)borderRadius);
            cairo_clip(cr);
        } else {
            cairo_rectangle(cr, (double)cx, (double)cy, (double)cw, (double)ch);
            cairo_clip(cr);
        }

        // ── 2. Wrap pixel buffer in a Cairo image surface ─────────────────────
        //  cairo_image_surface_create_for_data does NOT copy — pixels must stay
        //  alive, which they do because `pixels` is a member vector.
        cairo_surface_t *imgSurface = cairo_image_surface_create_for_data(
            const_cast<uint8_t *>(pixels.data()),
            CAIRO_FORMAT_ARGB32,
            imageWidth,
            imageHeight,
            imageWidth * 4   // stride = width * 4 bytes (no padding)
        );

        if (cairo_surface_status(imgSurface) != CAIRO_STATUS_SUCCESS) {
            std::cout << "[ImageWidget] cairo_image_surface_create_for_data failed: "
                      << cairo_status_to_string(cairo_surface_status(imgSurface)) << "\n";
            cairo_surface_destroy(imgSurface);
            cairo_restore(cr);
            return;
        }

        // ── 3. Compute destination rect ───────────────────────────────────────
        DestRect d = _calculateDestRect(cx, cy, cw, ch);

        // ── 4. Scale: Cairo paints the surface at 1:1, so we apply a matrix
        //     that maps image pixels → destination rect pixels.
        //     scale factors = destSize / imageSize
        double sx = d.w / (double)imageWidth;
        double sy = d.h / (double)imageHeight;

        cairo_translate(cr, d.x, d.y);
        cairo_scale(cr, sx, sy);

        // Nearest-neighbour (fast, matches the request).
        cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_NEAREST);

        cairo_set_source_surface(cr, imgSurface, 0.0, 0.0);

        // Re-apply nearest-neighbour to the new source pattern.
        cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_NEAREST);

        cairo_paint(cr);

        // ── 5. Cleanup ────────────────────────────────────────────────────────
        cairo_surface_destroy(imgSurface); // surface references pixels; vector stays alive
        cairo_restore(cr);                 // pops clip, transform, source
    }

    // Utility: add a rounded-rect path to the current Cairo context.
    static void _cairoRoundedRectPath(cairo_t *cr,
                                      double rx, double ry,
                                      double rw, double rh,
                                      double radius) {
        double r = radius;
        // Clamp radius so it can never exceed half the smallest side.
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