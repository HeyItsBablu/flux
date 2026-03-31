#ifndef FLUX_PAINTER_HPP
#define FLUX_PAINTER_HPP

#include "flux_font.hpp"
#include "flux_platform.hpp"
#include <string>
#include <vector>

struct Painter {
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
    // radius is the corner diameter, matching RoundRect's nWidth/nHeight.
    void fillRoundedRectGDI(int x, int y, int w, int h, int radius,
                            Color fill, Color stroke, int strokeWidth);

    // ── Ellipse (filled + optional stroke) ────────────────────────────────
    // Pass strokeWidth = 0 to suppress the border (uses PS_NULL pen).
    void drawEllipse(int x, int y, int w, int h,
                     Color fill, Color stroke, int strokeWidth);

    // ── Line segment ──────────────────────────────────────────────────────
    void drawLine(int x1, int y1, int x2, int y2, Color color, int width);

    // ── Text (wide) ───────────────────────────────────────────────────────
    void drawText(const std::wstring &text, int x, int y, int w, int h,
                  NativeFont font, Color color, UINT format);

    // ── Text (narrow / UTF-8) ─────────────────────────────────────────────
    // Convenience overload — converts std::string → std::wstring internally.
    void drawTextA(const std::string &text, int x, int y, int w, int h,
                   NativeFont font, Color color, UINT format);

    // ── Text measurement (no drawing) ─────────────────────────────────────
    // Returns pixel width and height of the string rendered with the given font.
    void measureText(const std::wstring &text, NativeFont font,
                     int &outWidth, int &outHeight);

    // ── Clip region management ────────────────────────────────────────────
    // pushClipRect intersects a new rect clip with whatever is already active.
    // popClipRect restores to no clipping (call once per push).
    void pushClipRect(int x, int y, int w, int h);
    void popClipRect();
    void pushClipRoundedRect(int x, int y, int w, int h, int cornerDiameter);

    // ── Gradient filled rect ──────────────────────────────────────────────
    // Fills [x, y, w, h] with a horizontal gradient interpolated across
    // `colors`. Single-color case is just fillRect — handled internally.
    void fillGradientRect(int x, int y, int w, int h,
                          const std::vector<Color> &colors);

    // ── Rect / rounded rect outlines ──────────────────────────────────────
    void drawRectOutline(int x, int y, int w, int h,
                         Color color, int strokeWidth);
    void drawRoundedRectOutline(int x, int y, int w, int h, int cornerDiameter,
                                Color stroke, int strokeWidth);

    // ── Alpha-blended solid rect ──────────────────────────────────────────
    // Alpha is taken from color.a.
    void fillRectAlpha(int x, int y, int w, int h, Color color);

    // ── Filled region via rounded-rect HRGN ──────────────────────────────
    // Used for scrollbar thumbs.
    void fillRoundedRegion(int x, int y, int w, int h,
                           int cornerRadius, Color color);

    // ── Horizontal / vertical line helpers ───────────────────────────────
    void drawHLine(int x, int y, int len, Color color, int strokeWidth = 1);
    void drawVLine(int x, int y, int len, Color color, int strokeWidth = 1);

    // ── Filled rect with a solid left accent strip ────────────────────────
    // Renders bg across [x,y,w,h] then a stripWidth-wide accent on the left.
    void fillRectWithLeftAccent(int x, int y, int w, int h,
                                Color bg, Color accent, int stripWidth);

    // ── Column bar chart fill ─────────────────────────────────────────────
    // Alpha is taken from color.a.
    void fillColumnBars(int x, int y, int w, int h,
                        const std::vector<int> &barHeights, Color color);

    // ── Polyline ──────────────────────────────────────────────────────────
    // Draws a connected sequence of line segments in one call.
    void drawPolyline(const std::vector<std::pair<int, int>> &points,
                      Color color, int strokeWidth);

    // ── Alpha-blended polygon fill ────────────────────────────────────────
    // Fills a polygon and alpha-blends it onto the destination.
    // Alpha is taken from color.a.
    void fillPolygonAlpha(const std::vector<std::pair<int, int>> &points,
                          Color color);
};

#endif // FLUX_PAINTER_HPP