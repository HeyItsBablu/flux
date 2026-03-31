// flux_painter_linux.cpp
#ifdef __linux__

#include "flux/flux_painter.hpp"

#include <pango/pangocairo.h>
#include <algorithm>
#include <cmath>
#include <cstring>   // memset
#include <vector>

// ============================================================================
// Internal helpers
// ============================================================================

// ── Color → Cairo components ─────────────────────────────────────────────────

static inline void setSourceColor(cairo_t* cr, Color c) {
    cairo_set_source_rgba(cr,
        c.r / 255.0,
        c.g / 255.0,
        c.b / 255.0,
        c.a / 255.0);
}

// ── Rounded rect path ────────────────────────────────────────────────────────
// radius is the corner radius in pixels (not diameter).
// Matches the visual result of GDI+ makeRoundedPath / Win32 RoundRect.

static void makeRoundedPath(cairo_t* cr,
                             double x, double y,
                             double w, double h,
                             double r) {
    // Clamp radius so it never exceeds half of the shortest side.
    r = std::min(r, std::min(w, h) * 0.5);

    cairo_new_path(cr);
    cairo_arc(cr, x + r,     y + r,     r,  M_PI,       1.5 * M_PI);  // top-left
    cairo_arc(cr, x + w - r, y + r,     r,  1.5 * M_PI, 2.0 * M_PI);  // top-right
    cairo_arc(cr, x + w - r, y + h - r, r,  0.0,        0.5 * M_PI);  // bottom-right
    cairo_arc(cr, x + r,     y + h - r, r,  0.5 * M_PI, M_PI);        // bottom-left
    cairo_close_path(cr);
}

// ── RAII save/restore ────────────────────────────────────────────────────────

struct CairoSave {
    cairo_t* cr;
    explicit CairoSave(cairo_t* c) : cr(c) { cairo_save(cr); }
    ~CairoSave() { cairo_restore(cr); }
};

// ── Text alignment flags (mirrors Win32 DT_* flags) ─────────────────────────
// Defined in flux_platform.hpp for Linux as plain ints:
//   DT_LEFT   = 0x0000
//   DT_CENTER = 0x0001
//   DT_RIGHT  = 0x0002
//   DT_VCENTER= 0x0004
//   DT_SINGLELINE = 0x0020
// (Only the subset used by FluxUI widgets.)

// ============================================================================
// Painter::fillRoundedRect
// ============================================================================

void Painter::fillRoundedRect(int x, int y, int w, int h,
                               int radius, Color color) {
    cairo_t* cr = ctx.cr;
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
                          int radius, Color color, int borderWidth) {
    cairo_t* cr = ctx.cr;
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

void Painter::fillRect(int x, int y, int w, int h, Color color) {
    cairo_t* cr = ctx.cr;
    CairoSave save(cr);

    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);   // pixel-perfect, no AA
    setSourceColor(cr, color);
    cairo_rectangle(cr, x, y, w, h);
    cairo_fill(cr);
}

// ============================================================================
// Painter::fillRoundedRectGDI
// On Linux there is no separate "GDI" path — same Cairo rounded rect,
// with an optional stroke painted on top.
// radius here is the corner *diameter* (matching Win32 RoundRect nWidth),
// so we halve it before passing to makeRoundedPath.
// ============================================================================

void Painter::fillRoundedRectGDI(int x, int y, int w, int h, int radius,
                                  Color fill, Color stroke, int strokeWidth) {
    cairo_t* cr = ctx.cr;
    CairoSave save(cr);

    cairo_set_antialias(cr, CAIRO_ANTIALIAS_BEST);
    double r = radius * 0.5;

    // Fill
    makeRoundedPath(cr, x, y, w, h, r);
    setSourceColor(cr, fill);
    cairo_fill_preserve(cr);

    // Stroke (optional)
    if (strokeWidth > 0) {
        cairo_set_line_width(cr, strokeWidth);
        setSourceColor(cr, stroke);
        cairo_stroke(cr);
    } else {
        cairo_new_path(cr);
    }
}

// ============================================================================
// Painter::drawEllipse
// strokeWidth == 0 → fill only (no stroke), matching PS_NULL behaviour.
// ============================================================================

void Painter::drawEllipse(int x, int y, int w, int h,
                           Color fill, Color stroke, int strokeWidth) {
    cairo_t* cr = ctx.cr;
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

    if (strokeWidth > 0) {
        cairo_set_line_width(cr, strokeWidth);
        setSourceColor(cr, stroke);
        cairo_stroke(cr);
    } else {
        cairo_new_path(cr);
    }
}

// ============================================================================
// Painter::drawLine
// ============================================================================

void Painter::drawLine(int x1, int y1, int x2, int y2,
                        Color color, int width) {
    cairo_t* cr = ctx.cr;
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
//
// Win32 uses std::wstring + DT_* flags.
// On Linux we convert to UTF-8 and use Pango layouts with equivalent
// alignment derived from the DT_* flags.
//
// Supported flags (same values as in flux_platform.hpp Linux block):
//   DT_LEFT=0x00, DT_CENTER=0x01, DT_RIGHT=0x02, DT_VCENTER=0x04
// ============================================================================

void Painter::drawText(const std::wstring& text,
                        int x, int y, int w, int h,
                        NativeFont font, Color color, UINT format) {
    if (text.empty()) return;

    // wstring → UTF-8
    std::string utf8;
    for (wchar_t wc : text) {
        if (wc < 0x80) {
            utf8 += static_cast<char>(wc);
        } else if (wc < 0x800) {
            utf8 += static_cast<char>(0xC0 | (wc >> 6));
            utf8 += static_cast<char>(0x80 | (wc & 0x3F));
        } else {
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

void Painter::drawTextA(const std::string& text,
                         int x, int y, int w, int h,
                         NativeFont font, Color color, UINT format) {
    if (text.empty()) return;

    cairo_t* cr = ctx.cr;
    CairoSave save(cr);

    PangoFontDescription* desc =
        reinterpret_cast<PangoFontDescription*>(font);

    // ── Create layout ────────────────────────────────────────────────────
    PangoLayout* layout = pango_cairo_create_layout(cr);
    pango_layout_set_font_description(layout, desc);
    pango_layout_set_text(layout, text.c_str(), -1);
    pango_layout_set_width(layout, w * PANGO_SCALE);

    // ── Horizontal alignment from DT_* flags ─────────────────────────────
    PangoAlignment align = PANGO_ALIGN_LEFT;
    if      (format & 0x0002) align = PANGO_ALIGN_RIGHT;   // DT_RIGHT
    else if (format & 0x0001) align = PANGO_ALIGN_CENTER;  // DT_CENTER
    pango_layout_set_alignment(layout, align);

    // ── Vertical centering (DT_VCENTER) ──────────────────────────────────
    int textW = 0, textH = 0;
    pango_layout_get_pixel_size(layout, &textW, &textH);

    double drawY = y;
    if ((format & 0x0004) && h > 0)     // DT_VCENTER
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

void Painter::measureText(const std::wstring& text, NativeFont font,
                           int& outWidth, int& outHeight) {
    if (text.empty()) {
        outWidth = outHeight = 0;
        return;
    }

    // Convert wstring → UTF-8
    std::string utf8;
    for (wchar_t wc : text) {
        if (wc < 0x80) {
            utf8 += static_cast<char>(wc);
        } else if (wc < 0x800) {
            utf8 += static_cast<char>(0xC0 | (wc >> 6));
            utf8 += static_cast<char>(0x80 | (wc & 0x3F));
        } else {
            utf8 += static_cast<char>(0xE0 | (wc >> 12));
            utf8 += static_cast<char>(0x80 | ((wc >> 6) & 0x3F));
            utf8 += static_cast<char>(0x80 | (wc & 0x3F));
        }
    }

    cairo_t* cr = ctx.cr;

    PangoFontDescription* desc =
        reinterpret_cast<PangoFontDescription*>(font);

    PangoLayout* layout = pango_cairo_create_layout(cr);
    pango_layout_set_font_description(layout, desc);
    pango_layout_set_text(layout, utf8.c_str(), -1);

    pango_layout_get_pixel_size(layout, &outWidth, &outHeight);

    g_object_unref(layout);
}

// ============================================================================
// Painter::pushClipRect / popClipRect
// ============================================================================

void Painter::pushClipRect(int x, int y, int w, int h) {
    cairo_t* cr = ctx.cr;
    cairo_save(cr);                        // matched by popClipRect
    cairo_rectangle(cr, x, y, w, h);
    cairo_clip(cr);
}

void Painter::popClipRect() {
    cairo_restore(ctx.cr);                 // restores clip + graphics state
}

// ============================================================================
// Painter::pushClipRoundedRect
// cornerDiameter matches Win32 convention (diameter, not radius).
// ============================================================================

void Painter::pushClipRoundedRect(int x, int y, int w, int h,
                                   int cornerDiameter) {
    cairo_t* cr = ctx.cr;
    cairo_save(cr);                        // matched by popClipRect
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
                                const std::vector<Color>& colors) {
    if (colors.empty() || w <= 0 || h <= 0) return;

    if (colors.size() == 1) {
        fillRect(x, y, w, h, colors[0]);
        return;
    }

    cairo_t* cr = ctx.cr;
    CairoSave save(cr);

    cairo_pattern_t* pat =
        cairo_pattern_create_linear(x, y, x + w, y);

    const int stops = static_cast<int>(colors.size());
    for (int i = 0; i < stops; ++i) {
        double offset = static_cast<double>(i) / (stops - 1);
        const Color& c = colors[i];
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
                               Color color, int strokeWidth) {
    cairo_t* cr = ctx.cr;
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
                                      Color stroke, int strokeWidth) {
    cairo_t* cr = ctx.cr;
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

void Painter::fillRectAlpha(int x, int y, int w, int h, Color color) {
    cairo_t* cr = ctx.cr;
    CairoSave save(cr);

    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    setSourceColor(cr, color);   // color.a already folded into RGBA
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
                                 int cornerRadius, Color color) {
    cairo_t* cr = ctx.cr;
    CairoSave save(cr);

    cairo_set_antialias(cr, CAIRO_ANTIALIAS_BEST);
    setSourceColor(cr, color);
    makeRoundedPath(cr, x, y, w, h, cornerRadius);
    cairo_fill(cr);
}

// ============================================================================
// Painter::drawHLine / drawVLine  — delegate to drawLine
// ============================================================================

void Painter::drawHLine(int x, int y, int len, Color color, int strokeWidth) {
    drawLine(x, y, x + len, y, color, strokeWidth);
}

void Painter::drawVLine(int x, int y, int len, Color color, int strokeWidth) {
    drawLine(x, y, x, y + len, color, strokeWidth);
}

// ============================================================================
// Painter::fillRectWithLeftAccent
// ============================================================================

void Painter::fillRectWithLeftAccent(int x, int y, int w, int h,
                                      Color bg, Color accent, int stripWidth) {
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
                              const std::vector<int>& barHeights,
                              Color color) {
    if (w <= 0 || h <= 0 || barHeights.empty()) return;

    cairo_t* cr = ctx.cr;
    CairoSave save(cr);

    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    setSourceColor(cr, color);   // alpha already in color.a

    int cols = std::min(w, static_cast<int>(barHeights.size()));
    for (int px = 0; px < cols; ++px) {
        int barH = std::max(0, std::min(h, barHeights[px]));
        if (barH == 0) continue;
        cairo_rectangle(cr,
                        x + px,      // x
                        y + h - barH, // y (bottom-aligned)
                        1,            // width: one column pixel
                        barH);
    }
    cairo_fill(cr);
}

// ============================================================================
// Painter::drawPolyline
// ============================================================================

void Painter::drawPolyline(const std::vector<std::pair<int,int>>& points,
                            Color color, int strokeWidth) {
    if (points.size() < 2) return;

    cairo_t* cr = ctx.cr;
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

void Painter::fillPolygonAlpha(const std::vector<std::pair<int,int>>& points,
                                Color color) {
    if (points.size() < 3) return;

    cairo_t* cr = ctx.cr;
    CairoSave save(cr);

    cairo_set_antialias(cr, CAIRO_ANTIALIAS_BEST);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    setSourceColor(cr, color);   // color.a carries alpha

    cairo_move_to(cr, points[0].first, points[0].second);
    for (size_t i = 1; i < points.size(); ++i)
        cairo_line_to(cr, points[i].first, points[i].second);
    cairo_close_path(cr);

    cairo_fill(cr);
}

#endif // __linux__