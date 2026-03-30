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
  void fillRoundedRect(int x, int y, int w, int h, int radius,
                       NativeColor color, BYTE alpha);

  // ── Border only (GDI+ anti-aliased) ──────────────────────────────────
  void drawBorder(int x, int y, int w, int h, int radius, NativeColor color,
                  int borderWidth, BYTE alpha);

  // ── Solid filled rect (no rounding) ──────────────────────────────────
  void fillRect(int x, int y, int w, int h, NativeColor color);

  // ── Filled rounded rect via GDI RoundRect (for non-GDI+ paths) ───────
  // radius here is the corner diameter, matching RoundRect's nWidth/nHeight
  void fillRoundedRectGDI(int x, int y, int w, int h, int radius,
                          NativeColor fill, NativeColor stroke,
                          int strokeWidth);

  // ── Ellipse (filled + optional stroke) ────────────────────────────────
  // Pass strokeWidth = 0 to suppress the border (uses PS_NULL pen).
  void drawEllipse(int x, int y, int w, int h, NativeColor fill,
                   NativeColor stroke, int strokeWidth);

  // ── Line segment ─────────────────────────────────────────────────────
  void drawLine(int x1, int y1, int x2, int y2, NativeColor color, int width);

  // ── Text ─────────────────────────────────────────────────────────────
  void drawText(const std::wstring &text, int x, int y, int w, int h,
                NativeFont font, NativeColor color, UINT format);

  // ── Text measurement (no drawing) ────────────────────────────────────
  // Returns pixel width and height of the string rendered with the given font.
  void measureText(const std::wstring &text, NativeFont font, int &outWidth,
                   int &outHeight);

  // ── Clip region management ────────────────────────────────────────────
  // pushClipRect intersects a new rect clip with whatever is already active.
  // popClipRect restores to no clipping (call once per push).
  // For nested clipping, callers save/restore the HRGN themselves — these
  // helpers cover the common single-level case used in all input widgets.
  void pushClipRect(int x, int y, int w, int h);
  void popClipRect();

  // ── Gradient filled rect ──────────────────────────────────────────────
  // Fills [x, y, w, h] with a horizontal gradient interpolated across
  // `colors`.  Used by ProgressBarWidget's multi-color progress fill.
  // Single-color case is just fillRect — handled internally for efficiency.
  void fillGradientRect(int x, int y, int w, int h,
                        const std::vector<NativeColor> &colors);

  void drawRectOutline(int x, int y, int w, int h, NativeColor color,
                       int strokeWidth);

  void pushClipRoundedRect(int x, int y, int w, int h, int cornerDiameter);
  void drawRoundedRectOutline(int x, int y, int w, int h, int cornerDiameter,
                              NativeColor stroke, int strokeWidth);

  void fillRectAlpha(int x, int y, int w, int h, NativeColor color, BYTE alpha);

  // ── Text (narrow / ANSI) ─────────────────────────────────────────────
  // Convenience overload — converts std::string → std::wstring internally.
  void drawTextA(const std::string &text, int x, int y, int w, int h,
                 NativeFont font, NativeColor color, UINT format);

  // ── Filled region via rounded-rect HRGN ──────────────────────────────
  // Used for scrollbar thumbs.
  void fillRoundedRegion(int x, int y, int w, int h, int cornerRadius,
                         NativeColor color);

  // ── Horizontal / vertical line (1px) ─────────────────────────────────
  // Thin convenience wrappers used for separators and borders.
  void drawHLine(int x, int y, int len, NativeColor color, int strokeWidth = 1);
  void drawVLine(int x, int y, int len, NativeColor color, int strokeWidth = 1);

  // ── Filled rect with a solid left accent strip ────────────────────────
  // Renders bg across [x,y,w,h] then a stripWidth-wide accent on the left.
  void fillRectWithLeftAccent(int x, int y, int w, int h, NativeColor bg,
                              NativeColor accent, int stripWidth);

  void fillColumnBars(int x, int y, int w, int h,
                      const std::vector<int> &barHeights, NativeColor color,
                      BYTE alpha);

// ── Polyline ──────────────────────────────────────────────────────────────
// Draws a connected sequence of line segments in one call.
void drawPolyline(const std::vector<std::pair<int,int>> &points,
                  NativeColor color, int strokeWidth);

// ── Alpha-blended polygon fill ────────────────────────────────────────────
// Fills a polygon and alpha-blends it onto the destination.
// Used for the tone-curve area fill.
void fillPolygonAlpha(const std::vector<std::pair<int,int>> &points,
                      NativeColor color, BYTE alpha);
};

#endif // FLUX_PAINTER_HPP