// flux_painter_linux.cpp
#if defined(__linux__) && !defined(__ANDROID__)

#include "flux/flux_painter.hpp"

#include <pango/pangocairo.h>
#include <algorithm>
#include <cmath>
#include <cstring> // memset
#include <vector>

// ============================================================================
// Internal helpers
// ============================================================================

// ── Color → Cairo components ─────────────────────────────────────────────────

static inline void setSourceColor(cairo_t *cr, Color c)
{
    cairo_set_source_rgba(cr,
                          c.r / 255.0,
                          c.g / 255.0,
                          c.b / 255.0,
                          c.a / 255.0);
}

// ── Rounded rect path ────────────────────────────────────────────────────────
// radius is the corner radius in pixels (not diameter).
// Matches the visual result of GDI+ makeRoundedPath / Win32 RoundRect.

static void makeRoundedPath(cairo_t *cr,
                            double x, double y,
                            double w, double h,
                            double r)
{
    // Clamp radius so it never exceeds half of the shortest side.
    r = std::min(r, std::min(w, h) * 0.5);

    cairo_new_path(cr);
    cairo_arc(cr, x + r, y + r, r, M_PI, 1.5 * M_PI);           // top-left
    cairo_arc(cr, x + w - r, y + r, r, 1.5 * M_PI, 2.0 * M_PI); // top-right
    cairo_arc(cr, x + w - r, y + h - r, r, 0.0, 0.5 * M_PI);    // bottom-right
    cairo_arc(cr, x + r, y + h - r, r, 0.5 * M_PI, M_PI);       // bottom-left
    cairo_close_path(cr);
}

// ── RAII save/restore ────────────────────────────────────────────────────────

struct CairoSave
{
    cairo_t *cr;
    explicit CairoSave(cairo_t *c) : cr(c) { cairo_save(cr); }
    ~CairoSave() { cairo_restore(cr); }
};

// ── Text alignment flags (mirrors Win32 DT_* flags) ─────────────────────────
// Defined in flux_platform.hpp for Linux as plain ints:
//   DT_LEFT   = 0x0000
//   DT_CENTER = 0x0001
//   DT_RIGHT  = 0x0002
//   DT_VCENTER= 0x0004
//   DT_SINGLELINE = 0x0020

// ============================================================================
// Painter::fillRoundedRect
// ============================================================================

void Painter::fillRoundedRect(int x, int y, int w, int h,
                              int radius, Color color)
{
    cairo_t *cr = ctx.cr;
    CairoSave save(cr);

    cairo_set_antialias(cr, CAIRO_ANTIALIAS_BEST);
    setSourceColor(cr, color);

    if (radius > 0)
        makeRoundedPath(cr, x, y, w, h, radius);
    else
        cairo_rectangle(cr, x, y, w, h);

    cairo_fill(cr);
}

// ============================================================================
// Painter::drawBorder
// ============================================================================

void Painter::drawBorder(int x, int y, int w, int h,
                         int radius, Color color, int borderWidth)
{
    cairo_t *cr = ctx.cr;
    CairoSave save(cr);

    cairo_set_antialias(cr, CAIRO_ANTIALIAS_BEST);
    cairo_set_line_width(cr, borderWidth);
    setSourceColor(cr, color);

    if (radius > 0)
        makeRoundedPath(cr, x, y, w, h, radius);
    else
        cairo_rectangle(cr, x, y, w, h);

    cairo_stroke(cr);
}

// ============================================================================
// Painter::fillRect
// ============================================================================

void Painter::fillRect(int x, int y, int w, int h, Color color)
{
    cairo_t *cr = ctx.cr;
    CairoSave save(cr);

    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE); // pixel-perfect, no AA
    setSourceColor(cr, color);
    cairo_rectangle(cr, x, y, w, h);
    cairo_fill(cr);
}

// ============================================================================
// Painter::fillRoundedRectGDI
// ============================================================================

void Painter::fillRoundedRectGDI(int x, int y, int w, int h, int radius,
                                 Color fill, Color stroke, int strokeWidth)
{
    cairo_t *cr = ctx.cr;
    CairoSave save(cr);

    cairo_set_antialias(cr, CAIRO_ANTIALIAS_BEST);
    double r = radius * 0.5;

    // Fill
    makeRoundedPath(cr, x, y, w, h, r);
    setSourceColor(cr, fill);
    cairo_fill_preserve(cr);

    // Stroke (optional)
    if (strokeWidth > 0)
    {
        cairo_set_line_width(cr, strokeWidth);
        setSourceColor(cr, stroke);
        cairo_stroke(cr);
    }
    else
    {
        cairo_new_path(cr);
    }
}

// ============================================================================
// Painter::drawEllipse
// strokeWidth == 0 → fill only (no stroke), matching PS_NULL behaviour.
// ============================================================================

void Painter::drawEllipse(int x, int y, int w, int h,
                          Color fill, Color stroke, int strokeWidth)
{
    cairo_t *cr = ctx.cr;
    CairoSave save(cr);

    cairo_set_antialias(cr, CAIRO_ANTIALIAS_BEST);

    // Cairo draws unit circles; scale to get an ellipse.
    cairo_save(cr);
    cairo_translate(cr, x + w * 0.5, y + h * 0.5);
    cairo_scale(cr, w * 0.5, h * 0.5);
    cairo_arc(cr, 0.0, 0.0, 1.0, 0.0, 2.0 * M_PI);
    cairo_restore(cr);

    setSourceColor(cr, fill);
    cairo_fill_preserve(cr);

    if (strokeWidth > 0)
    {
        cairo_set_line_width(cr, strokeWidth);
        setSourceColor(cr, stroke);
        cairo_stroke(cr);
    }
    else
    {
        cairo_new_path(cr);
    }
}

// ============================================================================
// Painter::drawLine
// ============================================================================

void Painter::drawLine(int x1, int y1, int x2, int y2,
                       Color color, int width)
{
    cairo_t *cr = ctx.cr;
    CairoSave save(cr);

    cairo_set_antialias(cr, CAIRO_ANTIALIAS_BEST);
    cairo_set_line_width(cr, width);
    setSourceColor(cr, color);

    cairo_move_to(cr, x1, y1);
    cairo_line_to(cr, x2, y2);
    cairo_stroke(cr);
}

// ============================================================================
// Painter::drawText  (wide string)
// ============================================================================

void Painter::drawText(const std::wstring &text,
                       int x, int y, int w, int h,
                       NativeFont font, Color color, UINT format)
{
    if (text.empty())
        return;

    // wstring → UTF-8
    std::string utf8;
    for (wchar_t wc : text)
    {
        if (wc < 0x80)
        {
            utf8 += static_cast<char>(wc);
        }
        else if (wc < 0x800)
        {
            utf8 += static_cast<char>(0xC0 | (wc >> 6));
            utf8 += static_cast<char>(0x80 | (wc & 0x3F));
        }
        else
        {
            utf8 += static_cast<char>(0xE0 | (wc >> 12));
            utf8 += static_cast<char>(0x80 | ((wc >> 6) & 0x3F));
            utf8 += static_cast<char>(0x80 | (wc & 0x3F));
        }
    }

    drawTextA(utf8, x, y, w, h, font, color, format);
}

// ============================================================================
// Painter::drawTextA  (UTF-8 string — primary implementation on Linux)
// ============================================================================

void Painter::drawTextA(const std::string &text,
                        int x, int y, int w, int h,
                        NativeFont font, Color color, UINT format)
{
    if (text.empty())
        return;

    cairo_t *cr = ctx.cr;
    CairoSave save(cr);

    PangoFontDescription *desc =
        reinterpret_cast<PangoFontDescription *>(font);

    // ── Create layout ────────────────────────────────────────────────────
    PangoLayout *layout = pango_cairo_create_layout(cr);
    pango_layout_set_font_description(layout, desc);
    pango_layout_set_text(layout, text.c_str(), -1);
    pango_layout_set_width(layout, w * PANGO_SCALE);

    // ── Horizontal alignment from DT_* flags ─────────────────────────────
    PangoAlignment align = PANGO_ALIGN_LEFT;
    if (format & 0x0002)
        align = PANGO_ALIGN_RIGHT; // DT_RIGHT
    else if (format & 0x0001)
        align = PANGO_ALIGN_CENTER; // DT_CENTER
    pango_layout_set_alignment(layout, align);

    // ── Vertical centering (DT_VCENTER) ──────────────────────────────────
    int textW = 0, textH = 0;
    pango_layout_get_pixel_size(layout, &textW, &textH);

    double drawY = y;
    if ((format & 0x0004) && h > 0) // DT_VCENTER
        drawY = y + (h - textH) * 0.5;

    // ── Clip to bounding box ─────────────────────────────────────────────
    cairo_rectangle(cr, x, y, w, h);
    cairo_clip(cr);

    // ── Paint ────────────────────────────────────────────────────────────
    setSourceColor(cr, color);
    cairo_move_to(cr, x, drawY);
    pango_cairo_show_layout(cr, layout);

    g_object_unref(layout);
}

// ============================================================================
// Painter::measureText
// ============================================================================

void Painter::measureText(const std::wstring &text, NativeFont font,
                          int &outWidth, int &outHeight)
{
    if (text.empty())
    {
        outWidth = outHeight = 0;
        return;
    }

    // Convert wstring → UTF-8
    std::string utf8;
    for (wchar_t wc : text)
    {
        if (wc < 0x80)
        {
            utf8 += static_cast<char>(wc);
        }
        else if (wc < 0x800)
        {
            utf8 += static_cast<char>(0xC0 | (wc >> 6));
            utf8 += static_cast<char>(0x80 | (wc & 0x3F));
        }
        else
        {
            utf8 += static_cast<char>(0xE0 | (wc >> 12));
            utf8 += static_cast<char>(0x80 | ((wc >> 6) & 0x3F));
            utf8 += static_cast<char>(0x80 | (wc & 0x3F));
        }
    }

    cairo_t *cr = ctx.cr;

    PangoFontDescription *desc =
        reinterpret_cast<PangoFontDescription *>(font);

    PangoLayout *layout = pango_cairo_create_layout(cr);
    pango_layout_set_font_description(layout, desc);
    pango_layout_set_text(layout, utf8.c_str(), -1);

    pango_layout_get_pixel_size(layout, &outWidth, &outHeight);

    g_object_unref(layout);
}

// ============================================================================
// Painter::pushClipRect / popClipRect
// ============================================================================

void Painter::pushClipRect(int x, int y, int w, int h)
{
    cairo_t *cr = ctx.cr;
    cairo_save(cr); // matched by popClipRect
    cairo_rectangle(cr, x, y, w, h);
    cairo_clip(cr);
}

void Painter::popClipRect()
{
    cairo_restore(ctx.cr); // restores clip + graphics state
}

// ============================================================================
// Painter::pushClipRoundedRect
// cornerDiameter matches Win32 convention (diameter, not radius).
// ============================================================================

void Painter::pushClipRoundedRect(int x, int y, int w, int h,
                                  int cornerDiameter)
{
    cairo_t *cr = ctx.cr;
    cairo_save(cr); // matched by popClipRect
    makeRoundedPath(cr, x, y, w, h, cornerDiameter * 0.5);
    cairo_clip(cr);
}

// ============================================================================
// Painter::fillGradientRect
// Uses Color::interpolate — identical logic to Win32 version.
// Cairo has native gradient support; we use it here instead of per-pixel
// columns for much better performance.
// ============================================================================

void Painter::fillGradientRect(int x, int y, int w, int h,
                               const std::vector<Color> &colors)
{
    if (colors.empty() || w <= 0 || h <= 0)
        return;

    if (colors.size() == 1)
    {
        fillRect(x, y, w, h, colors[0]);
        return;
    }

    cairo_t *cr = ctx.cr;
    CairoSave save(cr);

    cairo_pattern_t *pat =
        cairo_pattern_create_linear(x, y, x + w, y);

    const int stops = static_cast<int>(colors.size());
    for (int i = 0; i < stops; ++i)
    {
        double offset = static_cast<double>(i) / (stops - 1);
        const Color &c = colors[i];
        cairo_pattern_add_color_stop_rgba(pat, offset,
                                          c.r / 255.0, c.g / 255.0, c.b / 255.0, c.a / 255.0);
    }

    cairo_set_source(cr, pat);
    cairo_rectangle(cr, x, y, w, h);
    cairo_fill(cr);

    cairo_pattern_destroy(pat);
}

// ============================================================================
// Painter::drawRectOutline
// ============================================================================

void Painter::drawRectOutline(int x, int y, int w, int h,
                              Color color, int strokeWidth)
{
    cairo_t *cr = ctx.cr;
    CairoSave save(cr);

    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    cairo_set_line_width(cr, strokeWidth);
    setSourceColor(cr, color);
    cairo_rectangle(cr, x, y, w, h);
    cairo_stroke(cr);
}

// ============================================================================
// Painter::drawRoundedRectOutline
// cornerDiameter → radius = diameter / 2
// ============================================================================

void Painter::drawRoundedRectOutline(int x, int y, int w, int h,
                                     int cornerDiameter,
                                     Color stroke, int strokeWidth)
{
    cairo_t *cr = ctx.cr;
    CairoSave save(cr);

    cairo_set_antialias(cr, CAIRO_ANTIALIAS_BEST);
    cairo_set_line_width(cr, strokeWidth);
    setSourceColor(cr, stroke);
    makeRoundedPath(cr, x, y, w, h, cornerDiameter * 0.5);
    cairo_stroke(cr);
}

// ============================================================================
// Painter::fillRectAlpha
// Alpha taken from color.a — mirrors Win32 AlphaBlend behaviour.
// Cairo handles pre-multiplied alpha natively; we use CAIRO_OPERATOR_OVER.
// ============================================================================

void Painter::fillRectAlpha(int x, int y, int w, int h, Color color)
{
    cairo_t *cr = ctx.cr;
    CairoSave save(cr);

    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    setSourceColor(cr, color); // color.a already folded into RGBA
    cairo_rectangle(cr, x, y, w, h);
    cairo_fill(cr);
}

// ============================================================================
// Painter::fillRoundedRegion
// Win32 used FillRgn with a rounded-rect HRGN.
// Cairo equivalent: rounded path + fill.
// cornerRadius is a true radius (not diameter) — matches the Win32 call site.
// ============================================================================

void Painter::fillRoundedRegion(int x, int y, int w, int h,
                                int cornerRadius, Color color)
{
    cairo_t *cr = ctx.cr;
    CairoSave save(cr);

    cairo_set_antialias(cr, CAIRO_ANTIALIAS_BEST);
    setSourceColor(cr, color);
    makeRoundedPath(cr, x, y, w, h, cornerRadius);
    cairo_fill(cr);
}

// ============================================================================
// Painter::drawHLine / drawVLine  — delegate to drawLine
// ============================================================================

void Painter::drawHLine(int x, int y, int len, Color color, int strokeWidth)
{
    drawLine(x, y, x + len, y, color, strokeWidth);
}

void Painter::drawVLine(int x, int y, int len, Color color, int strokeWidth)
{
    drawLine(x, y, x, y + len, color, strokeWidth);
}

// ============================================================================
// Painter::fillRectWithLeftAccent
// ============================================================================

void Painter::fillRectWithLeftAccent(int x, int y, int w, int h,
                                     Color bg, Color accent, int stripWidth)
{
    fillRect(x, y, w, h, bg);
    fillRect(x, y, stripWidth, h, accent);
}

// ============================================================================
// Painter::fillColumnBars
// Win32 wrote into a DIB section pixel-by-pixel then AlphaBlended.
// On Linux we draw each bar as a Cairo rectangle and set alpha via the
// source colour — same visual result, far simpler.
// ============================================================================

void Painter::fillColumnBars(int x, int y, int w, int h,
                             const std::vector<int> &barHeights,
                             Color color)
{
    if (w <= 0 || h <= 0 || barHeights.empty())
        return;

    cairo_t *cr = ctx.cr;
    CairoSave save(cr);

    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    setSourceColor(cr, color); // alpha already in color.a

    int cols = std::min(w, static_cast<int>(barHeights.size()));
    for (int px = 0; px < cols; ++px)
    {
        int barH = std::max(0, std::min(h, barHeights[px]));
        if (barH == 0)
            continue;
        cairo_rectangle(cr,
                        x + px,       // x
                        y + h - barH, // y (bottom-aligned)
                        1,            // width: one column pixel
                        barH);
    }
    cairo_fill(cr);
}

// ============================================================================
// Painter::drawPolyline
// ============================================================================

void Painter::drawPolyline(const std::vector<std::pair<int, int>> &points,
                           Color color, int strokeWidth)
{
    if (points.size() < 2)
        return;

    cairo_t *cr = ctx.cr;
    CairoSave save(cr);

    cairo_set_antialias(cr, CAIRO_ANTIALIAS_BEST);
    cairo_set_line_width(cr, strokeWidth);
    setSourceColor(cr, color);

    cairo_move_to(cr, points[0].first, points[0].second);
    for (size_t i = 1; i < points.size(); ++i)
        cairo_line_to(cr, points[i].first, points[i].second);

    cairo_stroke(cr);
}

// ============================================================================
// Painter::fillPolygonAlpha
// Win32 rendered into an off-screen DIB then AlphaBlended.
// Cairo supports alpha natively — just fill the path with the source RGBA.
// Alpha is taken from color.a, matching the Win32 contract.
// ============================================================================

void Painter::fillPolygonAlpha(const std::vector<std::pair<int, int>> &points,
                               Color color)
{
    if (points.size() < 3)
        return;

    cairo_t *cr = ctx.cr;
    CairoSave save(cr);

    cairo_set_antialias(cr, CAIRO_ANTIALIAS_BEST);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    setSourceColor(cr, color); // color.a carries alpha

    cairo_move_to(cr, points[0].first, points[0].second);
    for (size_t i = 1; i < points.size(); ++i)
        cairo_line_to(cr, points[i].first, points[i].second);
    cairo_close_path(cr);

    cairo_fill(cr);
}

// =============================================================================
// RICH TEXT IMPLEMENTATION  (Linux / Cairo+Pango)
// =============================================================================

// -----------------------------------------------------------------------
// Internal helpers
// -----------------------------------------------------------------------

static std::string wstringToUtf8Linux(const std::wstring &text)
{
    std::string utf8;
    utf8.reserve(text.size() * 3);
    for (wchar_t wc : text)
    {
        uint32_t cp = static_cast<uint32_t>(wc);
        if (cp < 0x80)
        {
            utf8 += static_cast<char>(cp);
        }
        else if (cp < 0x800)
        {
            utf8 += static_cast<char>(0xC0 | (cp >> 6));
            utf8 += static_cast<char>(0x80 | (cp & 0x3F));
        }
        else
        {
            utf8 += static_cast<char>(0xE0 | (cp >> 12));
            utf8 += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            utf8 += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }
    return utf8;
}

// Measure a single UTF-8 line via Pango, returning pixel width.
// outLineH receives the natural line height.
static int measureLinePango(cairo_t *cr,
                            PangoFontDescription *desc,
                            const std::string &utf8,
                            int start, int len,
                            float letterSpacing,
                            int &outLineH)
{
    if (len <= 0)
    {
        outLineH = 16; // fallback
        return 0;
    }

    PangoLayout *layout = pango_cairo_create_layout(cr);
    pango_layout_set_font_description(layout, desc);
    pango_layout_set_text(layout, utf8.c_str() + start, len);

    if (letterSpacing != 0.f)
    {
        PangoAttrList *attrs = pango_attr_list_new();
        // Pango letter-spacing is in Pango units (1/1024 pt); we have pixels.
        // Approximate: 1 px ≈ 1024 Pango units at 96 dpi.
        PangoAttribute *attr = pango_attr_letter_spacing_new(
            static_cast<int>(letterSpacing * PANGO_SCALE));
        pango_attr_list_insert(attrs, attr);
        pango_layout_set_attributes(layout, attrs);
        pango_attr_list_unref(attrs);
    }

    int w = 0, h = 0;
    pango_layout_get_pixel_size(layout, &w, &h);
    outLineH = h;
    g_object_unref(layout);
    return w;
}

// Split a UTF-8 string (converted from wstring) into wrapped line spans.
// Each span is {byte_start, byte_len} into the utf8 string.
struct LinePango
{
    int start;
    int length;
};

static std::vector<LinePango> wrapTextPango(cairo_t *cr,
                                            PangoFontDescription *desc,
                                            const std::string &utf8,
                                            int maxWidth,
                                            bool softWrap,
                                            float letterSpacing)
{
    std::vector<LinePango> lines;
    const int n = static_cast<int>(utf8.size());
    if (n == 0)
        return lines;

    int pos = 0;
    while (pos < n)
    {
        int nlPos = pos;
        while (nlPos < n && utf8[nlPos] != '\n')
            ++nlPos;

        if (!softWrap || maxWidth <= 0)
        {
            lines.push_back({pos, nlPos - pos});
        }
        else
        {
            int lineStart = pos;
            while (lineStart < nlPos)
            {
                int lo = 0, hi = nlPos - lineStart, fit = 0;
                while (lo <= hi)
                {
                    int mid = (lo + hi) / 2;
                    int dummy;
                    int w = measureLinePango(cr, desc, utf8,
                                             lineStart, mid,
                                             letterSpacing, dummy);
                    if (w <= maxWidth)
                    {
                        fit = mid;
                        lo = mid + 1;
                    }
                    else
                    {
                        hi = mid - 1;
                    }
                }
                if (fit == 0)
                    fit = 1;

                int breakAt = fit;
                if (lineStart + fit < nlPos)
                {
                    int wb = fit;
                    while (wb > 1 && utf8[lineStart + wb - 1] != ' ')
                        --wb;
                    if (wb > 1)
                        breakAt = wb;
                }

                lines.push_back({lineStart, breakAt});
                lineStart += breakAt;
                while (lineStart < nlPos && utf8[lineStart] == ' ')
                    ++lineStart;
            }
        }

        pos = nlPos + 1;
        if (nlPos == n)
            break;
    }
    return lines;
}

// -----------------------------------------------------------------------
// Painter::drawWavyLine  (already declared in header)
// -----------------------------------------------------------------------

void Painter::drawWavyLine(int x, int y, int len, Color color, int amplitude)
{
    if (len <= 0)
        return;
    const int step = amplitude * 2;
    std::vector<std::pair<int, int>> pts;
    pts.reserve(len / step + 2);
    int px = x;
    bool up = true;
    while (px < x + len)
    {
        pts.push_back({px, up ? y - amplitude : y + amplitude});
        px += step;
        up = !up;
    }
    pts.push_back({x + len, y});
    if (pts.size() >= 2)
        drawPolyline(pts, color, 1);
}

// -----------------------------------------------------------------------
// Painter::drawFadeOverlay
// -----------------------------------------------------------------------

void Painter::drawFadeOverlay(int x, int y, int w, int h,
                              int fadeWidth, Color bg)
{
    if (fadeWidth <= 0 || w <= 0 || h <= 0)
        return;
    int startX = x + w - fadeWidth;
    if (startX < x)
        startX = x;
    std::vector<Color> stops = {bg.withAlpha(0), bg.withAlpha(255)};
    fillGradientRect(startX, y, fadeWidth, h, stops);
}

// -----------------------------------------------------------------------
// Painter::drawTextDecorationLine
// -----------------------------------------------------------------------

void Painter::drawTextDecorationLine(int lineX, int lineY, int lineW,
                                     const TextStyle &style,
                                     TextDecoration which)
{
    if (lineW <= 0)
        return;

    int fontSize = style.scaledFontSize();
    int ascent = static_cast<int>(fontSize * 0.75);
    int thickness = style.decorationThickness;
    Color dc = style.decorationColor;

    int decorY = lineY;
    if (which == TextDecoration::Underline)
        decorY = lineY + ascent + 1;
    else if (which == TextDecoration::Overline)
        decorY = lineY;
    else if (which == TextDecoration::LineThrough)
        decorY = lineY + ascent - ascent / 3;

    switch (style.decorationStyle)
    {
    case TextDecorationStyle::Solid:
        drawHLine(lineX, decorY, lineW, dc, thickness);
        break;
    case TextDecorationStyle::Double:
        drawHLine(lineX, decorY, lineW, dc, thickness);
        drawHLine(lineX, decorY + 2, lineW, dc, thickness);
        break;
    case TextDecorationStyle::Dotted:
        for (int px = lineX; px < lineX + lineW; px += 4)
            drawHLine(px, decorY, std::min(2, lineX + lineW - px), dc, thickness);
        break;
    case TextDecorationStyle::Dashed:
        for (int px = lineX; px < lineX + lineW; px += 8)
            drawHLine(px, decorY, std::min(5, lineX + lineW - px), dc, thickness);
        break;
    case TextDecorationStyle::Wavy:
        drawWavyLine(lineX, decorY, lineW, dc, 2);
        break;
    }
}

// -----------------------------------------------------------------------
// Painter::measureRichText
// -----------------------------------------------------------------------

void Painter::measureRichText(const std::wstring &text,
                              const TextStyle &style,
                              FontCache &fontCache,
                              int maxWidth, bool softWrap, int maxLines,
                              int &outWidth, int &outHeight)
{
    outWidth = outHeight = 0;
    if (text.empty())
        return;

    cairo_t *cr = ctx.cr;
    NativeFont font = fontCache.getFont(style.fontFamily,
                                        style.scaledFontSize(),
                                        style.fontWeight);
    PangoFontDescription *desc =
        reinterpret_cast<PangoFontDescription *>(font);

    std::string utf8 = wstringToUtf8Linux(text);
    auto lines = wrapTextPango(cr, desc, utf8, maxWidth, softWrap,
                               style.letterSpacing);

    int dummy;
    int lineH = 0;
    measureLinePango(cr, desc, utf8, 0,
                     lines.empty() ? 0 : lines[0].length,
                     style.letterSpacing, lineH);
    if (lineH == 0)
        lineH = style.scaledFontSize();
    int lineHeightPx = static_cast<int>(lineH * style.height);

    int totalLines = (maxLines > 0)
                         ? std::min((int)lines.size(), maxLines)
                         : (int)lines.size();

    for (int i = 0; i < totalLines; ++i)
    {
        int w = measureLinePango(cr, desc, utf8,
                                 lines[i].start, lines[i].length,
                                 style.letterSpacing, dummy);
        outWidth = std::max(outWidth, w);
    }
    outHeight = totalLines * lineHeightPx;
}

// -----------------------------------------------------------------------
// Painter::drawRichText
// -----------------------------------------------------------------------

void Painter::drawRichText(const std::wstring &text,
                           const RichTextParams &params,
                           FontCache &fontCache)
{
    if (text.empty() || params.w <= 0 || params.h <= 0)
        return;

    cairo_t *cr = ctx.cr;
    const TextStyle &style = params.style;

    NativeFont font = fontCache.getFont(style.fontFamily,
                                        style.scaledFontSize(),
                                        style.fontWeight);
    PangoFontDescription *desc =
        reinterpret_cast<PangoFontDescription *>(font);

    std::string utf8 = wstringToUtf8Linux(text);

    int wrapWidth = params.softWrap ? params.w : 0;
    auto lines = wrapTextPango(cr, desc, utf8, wrapWidth,
                               params.softWrap, style.letterSpacing);

    // Natural line height from a sample measure

    int lineH = 0;
    {
        int lineHDummy = 0;
        measureLinePango(cr, desc, utf8, 0,
                         lines.empty() ? 0 : lines[0].length,
                         style.letterSpacing, lineHDummy);
        lineH = (lineHDummy > 0) ? lineHDummy : style.scaledFontSize();
    }
    int lineHeightPx = static_cast<int>(lineH * style.height);

    int totalLines = (params.maxLines > 0)
                         ? std::min((int)lines.size(), params.maxLines)
                         : (int)lines.size();

    int blockH = totalLines * lineHeightPx;
    int startY = params.y;
    switch (params.textAlignVertical)
    {
    case TextAlignVertical::Center:
        startY = params.y + (params.h - blockH) / 2;
        break;
    case TextAlignVertical::Bottom:
        startY = params.y + params.h - blockH;
        break;
    default:
        break;
    }

    // Clipping
    bool needClip = (params.overflow != TextOverflow::Visible);
    if (needClip)
        pushClipRect(params.x, params.y, params.w, params.h);

    for (int i = 0; i < totalLines; ++i)
    {
        const auto &span = lines[i];
        int lineY = startY + i * lineHeightPx;

        if (lineY + lineHeightPx < params.y)
            continue;
        if (lineY > params.y + params.h)
            break;

        int dummy;
        int lineW = measureLinePango(cr, desc, utf8,
                                     span.start, span.length,
                                     style.letterSpacing, dummy);

        // X alignment
        int lineX = params.x;
        bool isRTL = (params.direction == TextDirection::RTL);
        switch (params.textAlign)
        {
        case TextAlign::Right:
        case TextAlign::End:
            lineX = isRTL ? params.x : (params.x + params.w - lineW);
            break;
        case TextAlign::Center:
            lineX = params.x + (params.w - lineW) / 2;
            break;
        case TextAlign::Left:
        case TextAlign::Start:
            lineX = isRTL ? (params.x + params.w - lineW) : params.x;
            break;
        default:
            lineX = params.x;
            break;
        }

        bool isLastVisible = (i == totalLines - 1);
        bool hasMoreLines = ((int)lines.size() > totalLines) ||
                            (totalLines == 1 && lineW > params.w);

        // Per-run background
        if (style.backgroundColor.has_value())
        {
            CairoSave save(cr);
            setSourceColor(cr, *style.backgroundColor);
            cairo_rectangle(cr, lineX, lineY, lineW, lineHeightPx);
            cairo_fill(cr);
        }

        // Shadows
        for (const auto &sh : style.shadows)
        {
            CairoSave save(cr);
            PangoLayout *layout = pango_cairo_create_layout(cr);
            pango_layout_set_font_description(layout, desc);
            pango_layout_set_text(layout, utf8.c_str() + span.start, span.length);
            setSourceColor(cr, sh.color);
            cairo_move_to(cr, lineX + sh.offsetX, lineY + sh.offsetY);
            pango_cairo_show_layout(cr, layout);
            g_object_unref(layout);
        }

        // Ellipsis on last visible line
        if (isLastVisible && hasMoreLines &&
            params.overflow == TextOverflow::Ellipsis)
        {
            CairoSave save(cr);
            PangoLayout *layout = pango_cairo_create_layout(cr);
            pango_layout_set_font_description(layout, desc);
            pango_layout_set_text(layout, utf8.c_str() + span.start, span.length);
            pango_layout_set_width(layout, params.w * PANGO_SCALE);
            pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
            setSourceColor(cr, style.color);
            cairo_move_to(cr, lineX, lineY);
            pango_cairo_show_layout(cr, layout);
            g_object_unref(layout);

            if (style.hasOverline())
                drawTextDecorationLine(lineX, lineY, lineW, style,
                                       TextDecoration::Overline);
            if (style.hasUnderline())
                drawTextDecorationLine(lineX, lineY, lineW, style,
                                       TextDecoration::Underline);
            if (style.hasLineThrough())
                drawTextDecorationLine(lineX, lineY, lineW, style,
                                       TextDecoration::LineThrough);

            if (isLastVisible && hasMoreLines &&
                params.overflow == TextOverflow::Fade)
            {
                Color fadeBg = Color::fromRGB(255, 255, 255);
                int fadeW = std::min(60, params.w / 3);
                drawFadeOverlay(params.x, lineY, params.w,
                                lineHeightPx, fadeW, fadeBg);
            }
            continue;
        }

        // Normal draw
        {
            CairoSave save(cr);
            PangoLayout *layout = pango_cairo_create_layout(cr);
            pango_layout_set_font_description(layout, desc);
            pango_layout_set_text(layout, utf8.c_str() + span.start, span.length);

            if (style.letterSpacing != 0.f)
            {
                PangoAttrList *attrs = pango_attr_list_new();
                PangoAttribute *attr = pango_attr_letter_spacing_new(
                    static_cast<int>(style.letterSpacing * PANGO_SCALE));
                pango_attr_list_insert(attrs, attr);
                pango_layout_set_attributes(layout, attrs);
                pango_attr_list_unref(attrs);
            }

            setSourceColor(cr, style.color);
            cairo_move_to(cr, lineX, lineY);
            pango_cairo_show_layout(cr, layout);
            g_object_unref(layout);
        }

        // Decorations
        if (style.hasOverline())
            drawTextDecorationLine(lineX, lineY, lineW, style,
                                   TextDecoration::Overline);
        if (style.hasUnderline())
            drawTextDecorationLine(lineX, lineY, lineW, style,
                                   TextDecoration::Underline);
        if (style.hasLineThrough())
            drawTextDecorationLine(lineX, lineY, lineW, style,
                                   TextDecoration::LineThrough);

        // Fade overlay
        if (isLastVisible && hasMoreLines &&
            params.overflow == TextOverflow::Fade)
        {
            Color fadeBg = Color::fromRGB(255, 255, 255);
            int fadeW = std::min(60, params.w / 3);
            drawFadeOverlay(params.x, lineY, params.w,
                            lineHeightPx, fadeW, fadeBg);
        }
    }

    if (needClip)
        popClipRect();
}

// -----------------------------------------------------------------------
// Painter::drawRichTextA
// -----------------------------------------------------------------------

void Painter::drawRichTextA(const std::string &text,
                            const RichTextParams &params,
                            FontCache &fontCache)
{
    if (text.empty())
        return;
    // Convert UTF-8 → wstring → back through drawRichText
    // (keeps a single code path; overhead is negligible for UI text)
    std::wstring wide;
    wide.reserve(text.size());
    for (unsigned char c : text)
        wide += static_cast<wchar_t>(c); // ASCII-safe; full UTF-8 handled by Pango
    drawRichText(wide, params, fontCache);
}

#endif // __linux__