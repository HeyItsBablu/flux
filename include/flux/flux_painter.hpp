#ifndef FLUX_PAINTER_HPP
#define FLUX_PAINTER_HPP

#include "flux_font.hpp"
#include "flux_platform.hpp"
#include "flux_text_style.hpp"
#include "flux/flux_scrollbar.hpp"
#include <string>
#include <vector>

// ── Forward declaration ───────────────────────────────────────────────────
// Painter needs to tag each instance with the widget that owns it (see
// below), but including flux_widget.hpp here would create a circular
// include (flux_widget.hpp already includes flux_painter.hpp). A forward
// declaration is all Painter needs, since it only ever stores the pointer.
class Widget;

// ── ImageRepeat / FilterQuality ───────────────────────────────────────────────

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

// ============================================================================
// Painter
// Stack-allocated wrapper around GraphicsContext.
// One instance per draw call site; never stored.

//
// "owner" — the Widget this Painter instance is drawing on behalf of, if
// any. Every existing backend (D2D/Cairo/Metal/Canvas2D) ignores this
// field entirely — it exists purely so future backends (e.g. a DOM
// backend) can correlate multiple draw calls (background fill, border,
// text) that all belong to the same logical widget, without needing a
// separate draw method per widget type. Defaults to nullptr so every
// existing call site (`Painter(ctx)`) keeps compiling unchanged.
// ============================================================================

struct Painter
{
    GraphicsContext &ctx;
    Widget *owner = nullptr;
    explicit Painter(GraphicsContext &c, Widget *owner = nullptr)
        : ctx(c), owner(owner) {}

    // ── Filled shapes ─────────────────────────────────────────────────────────
    void fillRect(int x, int y, int w, int h, Color color, const char *slot = "");
    void fillRoundedRect(int x, int y, int w, int h, int radius, Color color,
                         const char *slot = "");
    void fillRectAlpha(int x, int y, int w, int h, Color color);
    void fillRoundedRegion(int x, int y, int w, int h, int cornerRadius, Color color);
    void fillRectWithLeftAccent(int x, int y, int w, int h,
                                Color bg, Color accent, int stripWidth);
    void fillColumnBars(int x, int y, int w, int h,
                        const std::vector<int> &barHeights, Color color);
    void fillGradientRect(int x, int y, int w, int h,
                          const std::vector<Color> &colors);

    // GDI-compat overload: radius is a diameter (Win32 RoundRect convention).
    // On D2D this halves the radius and delegates to fillRoundedRect.
    void fillRoundedRectGDI(int x, int y, int w, int h, int radius,
                            Color fill, Color stroke, int strokeWidth,
                            const char *slot = "");

    void fillPolygonAlpha(const std::vector<std::pair<int, int>> &points,
                          Color color);

    // ── Stroked shapes ────────────────────────────────────────────────────────
    void drawBorder(int x, int y, int w, int h, int radius,
                    Color color, int borderWidth);
    void drawEllipse(int x, int y, int w, int h,
                     Color fill, Color stroke, int strokeWidth,
                     const char *slot = "");
    void drawLine(int x1, int y1, int x2, int y2, Color color, int width);
    void drawHLine(int x, int y, int len, Color color, int strokeWidth = 1);
    void drawVLine(int x, int y, int len, Color color, int strokeWidth = 1);
    void drawPolyline(const std::vector<std::pair<int, int>> &points,
                      Color color, int strokeWidth);
    void drawRectOutline(int x, int y, int w, int h,
                         Color color, int strokeWidth);
    void drawRoundedRectOutline(int x, int y, int w, int h,
                                int cornerDiameter,
                                Color stroke, int strokeWidth);
    void drawArc(float cx, float cy, float radius,
                 int strokeWidth,
                 float startAngle, float sweepAngle,
                 Color color, bool roundedCaps);
    void drawWavyLine(int x, int y, int len, Color color, int amplitude = 2);

    // ── Clip ──────────────────────────────────────────────────────────────────
    // Primary overload — cornerRadius=0 means axis-aligned clip (fastest).
    // cornerRadius>0 uses a rounded-rect layer clip.
    void pushClipRect(int x, int y, int w, int h, int cornerRadius = 0);
    void popClipRect();

    // Legacy overload kept for source compatibility (all existing call sites
    // that don't pass a radius still compile without changes).
    void pushClipRoundedRect(int x, int y, int w, int h, int cornerDiameter);

    // ── Opacity / shadow layers (D2D; no-op on other platforms) ──────────────
    // Draw a GPU shadow under a rect. blurRadius controls softness.
    void drawShadow(int x, int y, int w, int h,
                    int radius, int blurRadius,
                    Color color, int offsetX, int offsetY);

    // Begin/end a compositing layer with fractional opacity.
    // Every beginLayer must be matched by an endLayer.
    void beginLayer(float opacity);
    void endLayer();

    // ── Text (low-level — used by IconWidget) ─────────────────────────────────
    // UINT format: DT_CENTER / DT_RIGHT / DT_VCENTER flags (cross-platform).
    void drawText(const std::wstring &text, int x, int y, int w, int h,
                  NativeFont font, Color color, UINT format, const char *slot = "");
    void drawTextA(const std::string &text, int x, int y, int w, int h,
                   NativeFont font, Color color, UINT format, const char *slot = "");
    void measureText(const std::wstring &text, NativeFont font,
                     int &outWidth, int &outHeight);

    // ── Rich text ─────────────────────────────────────────────────────────────
    struct RichTextParams
    {
        int x = 0, y = 0, w = 0, h = 0;
        TextAlign textAlign = TextAlign::Left;
        TextAlignVertical textAlignVertical = TextAlignVertical::Top;
        TextOverflow overflow = TextOverflow::Clip;
        TextDirection direction = TextDirection::LTR;
        bool softWrap = true;
        int maxLines = 0;
        TextStyle style;
    };

    void drawRichText(const std::wstring &text,
                      const RichTextParams &params,
                      FontCache &fontCache);
    void drawRichTextA(const std::string &text,
                       const RichTextParams &params,
                       FontCache &fontCache);
    void measureRichText(const std::wstring &text,
                         const TextStyle &style,
                         FontCache &fontCache,
                         int maxWidth, bool softWrap, int maxLines,
                         int &outWidth, int &outHeight);

    // ── Decoration helpers ────────────────────────────────────────────────────
    void drawFadeOverlay(int x, int y, int w, int h, int fadeWidth, Color bg);
    void drawTextDecorationLine(int lineX, int lineY, int lineW,
                                const TextStyle &style,
                                TextDecoration which);

    // ── Image / Video / Camera / Page ─────────────────────────────────────────

    struct ImageDrawParams
    {
        NativeImage image{};
        int srcWidth = 0;
        int srcHeight = 0;
        // Optional source sub-rect, in the same pixel space as
        // srcWidth/srcHeight. Sentinel: srcW/srcH < 0 means "use the
        // full texture" (srcX/srcY ignored), preserving old behavior on
        // every platform that doesn't set these. Used by Android's
        // CanvasWidget to composite a pan/zoomed view of a doc-sized FBO.
        float srcX = 0.f, srcY = 0.f;
        float srcW = -1.f, srcH = -1.f;
        int clipX = 0, clipY = 0, clipW = 0, clipH = 0;
        float destX = 0, destY = 0, destW = 0, destH = 0;
        int borderRadius = 0;
        ImageRepeat repeat = ImageRepeat::NoRepeat;
        FilterQuality filterQuality = FilterQuality::Low;
        bool flipY = false; // true for FBO-backed textures (GL render-target convention)
    };
    void drawImage(const ImageDrawParams &params);

    struct VideoDrawParams
    {
        int dstX = 0, dstY = 0, dstW = 0, dstH = 0;
        NativeImage frame{};

#ifdef _WIN32
        // Win32 D2D path: frame is ID2D1Bitmap1* — no separate pixel/bmi needed
#endif
#ifdef __ANDROID__
        int srcW = 0, srcH = 0;
#endif
    };
    void drawVideo(const VideoDrawParams &params);

    struct CameraDrawParams
    {
        int dstX = 0, dstY = 0, dstW = 0, dstH = 0;
        bool mirror = false;
        float rotationDeg = 0.f;
        float rotCenterX = 0.f;
        float rotCenterY = 0.f;
        NativeImage frame{};

#ifdef __ANDROID__
        int srcW = 0, srcH = 0;
#endif
    };
    void drawCamera(const CameraDrawParams &params);

    struct PageDrawParams
    {
        int x = 0, y = 0, w = 0, h = 0;

        struct Region
        {
            bool present = false;
            int x = 0, y = 0, w = 0, h = 0;
            bool hasBackground = false;
            Color background = Color::fromRGB(255, 255, 255);
            int elevation = 0;
        };

        Region header, body, footer;
        bool hasPageBackground = false;
        Color pageBackground = Color::fromRGB(255, 255, 255);
        std::string widgetId;
    };
    void drawPage(const PageDrawParams &params);

    // ── Scrollbar ─────────────────────────────────────────────────────────────
    // glW / glH are the physical pixel dimensions of the rendering area
    // (widget width × height on all platforms).
    void drawScrollbar(const CustomScrollbar &bar, int glW, int glH);
};

#endif // FLUX_PAINTER_HPP