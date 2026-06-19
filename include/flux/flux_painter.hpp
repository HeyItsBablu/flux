#ifndef FLUX_PAINTER_HPP
#define FLUX_PAINTER_HPP

#include "flux_font.hpp"
#include "flux_platform.hpp"
#include "flux_text_style.hpp"
#include <string>
#include <vector>

// ── Native image handle (platform-specific) ────────────────────────────────
#ifdef _WIN32
namespace Gdiplus
{
    class Bitmap;
}
using NativeImage = Gdiplus::Bitmap *;
#elif defined(__APPLE__) && !defined(__ANDROID__)
using NativeImage = struct CGImage *;
#elif defined(__ANDROID__)
using NativeImage = int; // NanoVG image handle (nvgImage)
#elif defined(__linux__)
typedef struct _cairo_surface cairo_surface_t;
using NativeImage = cairo_surface_t *;
#elif defined(__EMSCRIPTEN__)
using NativeImage = int; // OffscreenCanvas store key
#else
using NativeImage = void *;
#endif

// ── Image repeat / filter quality (moved here from flux_image.hpp — these
//    describe HOW Painter draws an image, not widget state) ────────────────
enum class ImageRepeat
{
    NoRepeat,
    Repeat,
    RepeatX,
    RepeatY
};

enum class FilterQuality
{
    None,
    Low,
    Medium,
    High
};

struct Painter
{
    GraphicsContext &ctx;

    explicit Painter(GraphicsContext &c) : ctx(c) {}

    // ── Filled rounded rect (GDI+ anti-aliased) ───────────────────────────
    void fillRoundedRect(int x, int y, int w, int h, int radius, Color color);

    // ── Border only (GDI+ anti-aliased) ──────────────────────────────────
    void drawBorder(int x, int y, int w, int h, int radius, Color color,
                    int borderWidth);

    // ── Solid filled rect (no rounding) ──────────────────────────────────
    void fillRect(int x, int y, int w, int h, Color color);

    // ── Filled rounded rect via GDI RoundRect (for non-GDI+ paths) ───────
    void fillRoundedRectGDI(int x, int y, int w, int h, int radius,
                            Color fill, Color stroke, int strokeWidth);

    // ── Ellipse (filled + optional stroke) ────────────────────────────────
    void drawEllipse(int x, int y, int w, int h,
                     Color fill, Color stroke, int strokeWidth);

    // ── Line segment ──────────────────────────────────────────────────────
    void drawLine(int x1, int y1, int x2, int y2, Color color, int width);

    // ── Text (wide) — low-level, no style object ──────────────────────────
    void drawText(const std::wstring &text, int x, int y, int w, int h,
                  NativeFont font, Color color, UINT format);

    // ── Text (narrow / UTF-8) — low-level ────────────────────────────────
    void drawTextA(const std::string &text, int x, int y, int w, int h,
                   NativeFont font, Color color, UINT format);

    // ── Text measurement (no drawing) ─────────────────────────────────────
    void measureText(const std::wstring &text, NativeFont font,
                     int &outWidth, int &outHeight);

    // ── Clip region management ────────────────────────────────────────────
    void pushClipRect(int x, int y, int w, int h);
    void popClipRect();
    void pushClipRoundedRect(int x, int y, int w, int h, int cornerDiameter);

    // ── Gradient filled rect ──────────────────────────────────────────────
    void fillGradientRect(int x, int y, int w, int h,
                          const std::vector<Color> &colors);

    // ── Rect / rounded rect outlines ──────────────────────────────────────
    void drawRectOutline(int x, int y, int w, int h,
                         Color color, int strokeWidth);
    void drawRoundedRectOutline(int x, int y, int w, int h, int cornerDiameter,
                                Color stroke, int strokeWidth);

    // ── Alpha-blended solid rect ──────────────────────────────────────────
    void fillRectAlpha(int x, int y, int w, int h, Color color);

    // ── Filled region via rounded-rect HRGN ──────────────────────────────
    void fillRoundedRegion(int x, int y, int w, int h,
                           int cornerRadius, Color color);

    // ── Horizontal / vertical line helpers ───────────────────────────────
    void drawHLine(int x, int y, int len, Color color, int strokeWidth = 1);
    void drawVLine(int x, int y, int len, Color color, int strokeWidth = 1);

    // ── Filled rect with a solid left accent strip ────────────────────────
    void fillRectWithLeftAccent(int x, int y, int w, int h,
                                Color bg, Color accent, int stripWidth);

    // ── Column bar chart fill ─────────────────────────────────────────────
    void fillColumnBars(int x, int y, int w, int h,
                        const std::vector<int> &barHeights, Color color);

    // ── Polyline ──────────────────────────────────────────────────────────
    void drawPolyline(const std::vector<std::pair<int, int>> &points,
                      Color color, int strokeWidth);

    // ── Alpha-blended polygon fill ────────────────────────────────────────
    void fillPolygonAlpha(const std::vector<std::pair<int, int>> &points,
                          Color color);

    // =========================================================================
    // RICH TEXT DRAWING — Flutter-like, uses TextStyle
    // =========================================================================

    // ── Parameters shared by all rich-text drawing calls ─────────────────────
    struct RichTextParams
    {
        int x = 0;
        int y = 0;
        int w = 0;
        int h = 0;

        TextAlign textAlign = TextAlign::Left;
        TextAlignVertical textAlignVertical = TextAlignVertical::Top;
        TextOverflow overflow = TextOverflow::Clip;
        TextDirection direction = TextDirection::LTR;

        bool softWrap = true; // wrap at word boundaries
        int maxLines = 0;     // 0 = unlimited

        // The style carries font, color, spacing, decoration, shadows, etc.
        TextStyle style;
    };

    // ── Main rich-text draw call (wide string) ────────────────────────────────
    // This is the single function that TextWidget ultimately calls.
    void drawRichText(const std::wstring &text,
                      const RichTextParams &params,
                      FontCache &fontCache);

    // ── Convenience overload for narrow UTF-8 strings ─────────────────────────
    void drawRichTextA(const std::string &text,
                       const RichTextParams &params,
                       FontCache &fontCache);

    // ── Rich text measurement (no drawing) ───────────────────────────────────
    // Returns the bounding box the text would occupy with the given params.
    // maxWidth limits line wrapping; use kUnbounded for single-line measure.
    void measureRichText(const std::wstring &text,
                         const TextStyle &style,
                         FontCache &fontCache,
                         int maxWidth,
                         bool softWrap,
                         int maxLines,
                         int &outWidth, int &outHeight);

    // ── Fade overlay helper (used internally for TextOverflow::Fade) ──────────
    // Draws a horizontal gradient from transparent to `bg` over the last
    // `fadeWidth` pixels of [x, y, w, h].
    void drawFadeOverlay(int x, int y, int w, int h, int fadeWidth, Color bg);

    // ── Decoration line drawing helpers ──────────────────────────────────────
    // Called internally after text is drawn to render underline / overline /
    // line-through at the correct baseline position.
    void drawTextDecorationLine(int lineX, int lineY, int lineW,
                                const TextStyle &style,
                                TextDecoration which);

    // ── Wavy line helper (TextDecorationStyle::Wavy approximation) ───────────
    void drawWavyLine(int x, int y, int len, Color color, int amplitude = 2);

    // ── Arc stroke (used by CircularProgressIndicatorWidget) ──────────────────
    // startAngle and sweepAngle are in radians. 0 = 3 o'clock, clockwise.
    void drawArc(float cx, float cy, float radius,
                 int strokeWidth,
                 float startAngle, float sweepAngle,
                 Color color, bool roundedCaps);

    // =========================================================================
    // IMAGE DRAWING — the only place that issues system image-blit calls.
    // ImageWidget/_platformRender prepares `image` (already decoded, and
    // ideally pre-scaled to destW/destH) and hands it here. If srcWidth/
    // srcHeight differ from destW/destH, drawImage scales at draw time using
    // filterQuality — this covers both the "cache hit" (srcW==destW) and
    // "fallback, not yet cached" (srcW==original image size) cases uniformly.
    // =========================================================================

    struct ImageDrawParams
    {
        NativeImage image = nullptr; // platform-native, already-decoded image
        int srcWidth = 0;
        int srcHeight = 0;

        // Clip rect == widget's padded content rect; also bounds repeat tiling.
        int clipX = 0, clipY = 0, clipW = 0, clipH = 0;

        // Single-tile placement rect, already computed by the widget's
        // fit/alignment math (e.g. ImageWidget::_calculateDestRect).
        float destX = 0, destY = 0, destW = 0, destH = 0;

        int borderRadius = 0;
        ImageRepeat repeat = ImageRepeat::NoRepeat;
        FilterQuality filterQuality = FilterQuality::Low;
    };

    void drawImage(const ImageDrawParams &params);
};

#endif // FLUX_PAINTER_HPP