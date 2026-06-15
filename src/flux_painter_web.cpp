// src/flux_painter_web.cpp
//
// Painter implementation for Emscripten / WebAssembly.
//
// Every draw call reaches the browser's CanvasRenderingContext2D through
// EM_ASM blocks.  The JS context object lives on Module._fluxCtx2D (set by
// shell.html before WASM starts); C++ never holds a pointer to it.
//
// Rounded-rect strategy
// ─────────────────────
// Inline EM_ASM blocks cannot contain JS `var a = $0, b = $1` declarations
// because the EM_ASM macro parser treats the commas as argument separators.
// Similarly, `ctx.method()` inside EM_ASM is parsed by the C++ preprocessor
// as a member access on the C++ `ctx` variable, not as JavaScript.
//
// Solution: a one-time JS helper `Module._fluxRRect(ctx,x,y,w,h,r)` is
// registered at startup via fluxPainterWebInit() and called by name from all
// subsequent EM_ASM blocks.  Each EM_ASM block receives the five numeric
// parameters as $0..$4 and passes them to the helper — no JS variable
// declarations or dot-notation member access needed inside the macro body.
//
// Clip stack
// ──────────
// pushClipRect / pushClipRoundedRect call ctx.save() before clip().
// popClipRect calls ctx.restore() and immediately re-applies the dpr scale
// transform (restore() would otherwise pop it back to identity).
//
// Coordinate system
// ─────────────────
// All coordinates are physical pixels (CSS px × devicePixelRatio).
// flux_window_web.cpp applies ctx.scale(dpr,dpr) once in create(), so all
// draw calls here work in physical-pixel space — identical to GDI+ on Win32.

#ifdef __EMSCRIPTEN__

#include "flux/flux_painter.hpp"
#include "flux/flux_font.hpp"
#include "flux/flux_platform.hpp"

#include <emscripten.h>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <vector>
#include <string>

// ── Forward declarations from flux_font_web.cpp ───────────────────────────────
void measureWebText(const char *cssFont, const char *utf8Text,
                    int fontSize, int &outWidth, int &outHeight);
void measureWebTextW(const char *cssFont, const std::wstring &wtext,
                     int fontSize, int &outWidth, int &outHeight);

// ============================================================================
// One-time JS helper registration
//
// Call this once (from main_web.cpp after Module is ready, or lazily on first
// paint).  Registers Module._fluxRRect so EM_ASM blocks can call it by name
// without triggering the EM_ASM comma/dot parsing bugs.
// ============================================================================

extern "C" void fluxPainterWebInit()
{
    EM_ASM({
        // Module._fluxRRect(ctx, x, y, w, h, r)
        // Draws a rounded-rect path. Caller must call fill() or stroke() after.
        // Uses ctx.roundRect if available (Chrome 99+, Safari 15.4+, FF 112+),
        // otherwise falls back to arcTo corner segments.
        Module._fluxRRect = function(ctx, x, y, w, h, r)
        {
            ctx.beginPath();
            if (ctx.roundRect)
            {
                ctx.roundRect(x, y, w, h, r);
            }
            else
            {
                ctx.moveTo(x + r, y);
                ctx.lineTo(x + w - r, y);
                ctx.arcTo(x + w, y, x + w, y + r, r);
                ctx.lineTo(x + w, y + h - r);
                ctx.arcTo(x + w, y + h, x + w - r, y + h, r);
                ctx.lineTo(x + r, y + h);
                ctx.arcTo(x, y + h, x, y + h - r, r);
                ctx.lineTo(x, y + r);
                ctx.arcTo(x, y, x + r, y, r);
                ctx.closePath();
            }
        };
    });
}

// ============================================================================
// Internal helpers
// ============================================================================

namespace
{

    // ── CSS colour string ─────────────────────────────────────────────────────────
    // Writes "rgba(r,g,b,a)" into buf (stack-allocated, no heap).
    // Max output: "rgba(255,255,255,1.000)" = 22 chars; 32-byte buf is safe.

    void cssColor(Color c, char *buf, int bufLen)
    {
        float a = c.a / 255.f;
        snprintf(buf, bufLen, "rgba(%d,%d,%d,%.3f)", c.r, c.g, c.b, a);
    }

    // ── wstring → UTF-8 (BMP only) ───────────────────────────────────────────────

    std::string wToUtf8(const std::wstring &ws)
    {
        std::string out;
        out.reserve(ws.size() * 3);
        for (wchar_t wc : ws)
        {
            unsigned cp = static_cast<unsigned>(wc);
            if (cp < 0x80)
            {
                out += static_cast<char>(cp);
            }
            else if (cp < 0x800)
            {
                out += static_cast<char>(0xC0 | (cp >> 6));
                out += static_cast<char>(0x80 | (cp & 0x3F));
            }
            else
            {
                out += static_cast<char>(0xE0 | (cp >> 12));
                out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                out += static_cast<char>(0x80 | (cp & 0x3F));
            }
        }
        return out;
    }

    // ── DT_* flag helpers ─────────────────────────────────────────────────────────

    inline bool dtCenter(UINT f) { return (f & DT_CENTER) == DT_CENTER; }
    inline bool dtRight(UINT f) { return (f & DT_RIGHT) == DT_RIGHT; }
    inline bool dtVCenter(UINT f) { return (f & DT_VCENTER) == DT_VCENTER; }

    // ── Parse fontSize from CSS font string "NNN NNpx Family" ────────────────────

    int parseFontSize(const char *cssFont)
    {
        if (!cssFont)
            return 14;
        const char *px = strstr(cssFont, "px");
        if (!px)
            return 14;
        const char *p = px - 1;
        while (p > cssFont && *p >= '0' && *p <= '9')
            --p;
        return atoi(p + 1);
    }

} // namespace

// ============================================================================
// Painter::fillRect
// ============================================================================

void Painter::fillRect(int x, int y, int w, int h, Color color)
{
    char col[32];
    cssColor(color, col, sizeof(col));
    EM_ASM({
        var c = Module._fluxCtx2D;
        if (!c) return;
        c.fillStyle = UTF8ToString($4);
        c.fillRect($0, $1, $2, $3); }, x, y, w, h, col);
}

// ============================================================================
// Painter::fillRoundedRect
// ============================================================================

void Painter::fillRoundedRect(int x, int y, int w, int h, int radius,
                              Color color)
{
    if (radius <= 0)
    {
        fillRect(x, y, w, h, color);
        return;
    }
    char col[32];
    cssColor(color, col, sizeof(col));
    EM_ASM({
        var c = Module._fluxCtx2D;
        if (!c) return;
        c.fillStyle = UTF8ToString($5);
        Module._fluxRRect(c, $0, $1, $2, $3, $4);
        c.fill(); }, x, y, w, h, radius, col);
}

// ============================================================================
// Painter::drawBorder
// ============================================================================

void Painter::drawBorder(int x, int y, int w, int h, int radius, Color color,
                         int borderWidth)
{
    char col[32];
    cssColor(color, col, sizeof(col));
    EM_ASM({
        var c = Module._fluxCtx2D;
        if (!c) return;
        c.strokeStyle = UTF8ToString($5);
        c.lineWidth   = $6;
        if ($4 > 0) {
            Module._fluxRRect(c, $0, $1, $2, $3, $4);
        } else {
            c.beginPath();
            c.rect($0, $1, $2, $3);
        }
        c.stroke(); }, x, y, w, h, radius, col, borderWidth);
}

// ============================================================================
// Painter::fillRoundedRectGDI
// ============================================================================

void Painter::fillRoundedRectGDI(int x, int y, int w, int h, int radius,
                                 Color fill, Color stroke, int strokeWidth)
{
    fillRoundedRect(x, y, w, h, radius, fill);
    if (strokeWidth > 0)
        drawBorder(x, y, w, h, radius, stroke, strokeWidth);
}

// ============================================================================
// Painter::drawEllipse
// ============================================================================

void Painter::drawEllipse(int x, int y, int w, int h,
                          Color fill, Color stroke, int strokeWidth)
{
    char fcol[32];
    cssColor(fill, fcol, sizeof(fcol));
    char scol[32];
    cssColor(stroke, scol, sizeof(scol));
    double cx = x + w * 0.5;
    double cy = y + h * 0.5;
    double rx = w * 0.5;
    double ry = h * 0.5;
    EM_ASM({
        var c = Module._fluxCtx2D;
        if (!c) return;
        c.beginPath();
        c.ellipse($0, $1, $2, $3, 0, 0, 6.2831853);
        c.fillStyle = UTF8ToString($4);
        c.fill();
        if ($6 > 0) {
            c.strokeStyle = UTF8ToString($5);
            c.lineWidth   = $6;
            c.stroke();
        } }, cx, cy, rx, ry, fcol, scol, strokeWidth);
}

// ============================================================================
// Painter::drawLine
// ============================================================================

void Painter::drawLine(int x1, int y1, int x2, int y2, Color color, int width)
{
    char col[32];
    cssColor(color, col, sizeof(col));
    EM_ASM({
        var c = Module._fluxCtx2D;
        if (!c) return;
        c.strokeStyle = UTF8ToString($4);
        c.lineWidth   = $5;
        c.beginPath();
        c.moveTo($0, $1);
        c.lineTo($2, $3);
        c.stroke(); }, x1, y1, x2, y2, col, width);
}

// ============================================================================
// Painter::drawHLine / drawVLine
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
// Painter::drawRectOutline
// ============================================================================

void Painter::drawRectOutline(int x, int y, int w, int h,
                              Color color, int strokeWidth)
{
    char col[32];
    cssColor(color, col, sizeof(col));
    EM_ASM({
        var c = Module._fluxCtx2D;
        if (!c) return;
        c.strokeStyle = UTF8ToString($4);
        c.lineWidth   = $5;
        c.strokeRect($0, $1, $2, $3); }, x, y, w, h, col, strokeWidth);
}

// ============================================================================
// Painter::drawRoundedRectOutline
// ============================================================================

void Painter::drawRoundedRectOutline(int x, int y, int w, int h,
                                     int cornerDiameter, Color stroke,
                                     int strokeWidth)
{
    drawBorder(x, y, w, h, cornerDiameter / 2, stroke, strokeWidth);
}

// ============================================================================
// Painter::fillRectAlpha
// Canvas 2D handles premultiplied alpha natively — rgba() covers it.
// ============================================================================

void Painter::fillRectAlpha(int x, int y, int w, int h, Color color)
{
    fillRect(x, y, w, h, color);
}

// ============================================================================
// Painter::fillRoundedRegion
// ============================================================================

void Painter::fillRoundedRegion(int x, int y, int w, int h,
                                int cornerRadius, Color color)
{
    fillRoundedRect(x, y, w, h, cornerRadius, color);
}

// ============================================================================
// Painter::pushClipRect / popClipRect
// ============================================================================

void Painter::pushClipRect(int x, int y, int w, int h)
{
    EM_ASM({
        var c = Module._fluxCtx2D;
        if (!c) return;
        c.save();
        c.beginPath();
        c.rect($0, $1, $2, $3);
        c.clip(); }, x, y, w, h);
}

void Painter::popClipRect()
{
    EM_ASM({
        var c = Module._fluxCtx2D;
        if (!c)
            return;
        c.restore();
        // Re-apply dpr scale — restore() pops it back to identity.
        var dpr = Module._fluxDPR || 1.0;
        c.scale(dpr, dpr);
    });
}

// ============================================================================
// Painter::pushClipRoundedRect
// ============================================================================

void Painter::pushClipRoundedRect(int x, int y, int w, int h,
                                  int cornerDiameter)
{
    int r = cornerDiameter / 2;
    EM_ASM({
        var c = Module._fluxCtx2D;
        if (!c) return;
        c.save();
        Module._fluxRRect(c, $0, $1, $2, $3, $4);
        c.clip(); }, x, y, w, h, r);
}

// ============================================================================
// Painter::fillGradientRect
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

    // Build JSON array of CSS colour strings: ["rgba(...)","rgba(...)"]
    std::string json = "[";
    const int n = (int)colors.size();
    for (int i = 0; i < n; ++i)
    {
        char buf[48];
        snprintf(buf, sizeof(buf), "\"rgba(%d,%d,%d,%.3f)\"",
                 colors[i].r, colors[i].g, colors[i].b, colors[i].a / 255.f);
        json += buf;
        if (i < n - 1)
            json += ',';
    }
    json += ']';

    EM_ASM({
        var c = Module._fluxCtx2D;
        if (!c) return;
        var stops = JSON.parse(UTF8ToString($4));
        var grad  = c.createLinearGradient($0, 0, $0 + $2, 0);
        var n     = stops.length;
        for (var i = 0; i < n; i++)
            grad.addColorStop(i / (n - 1), stops[i]);
        c.fillStyle = grad;
        c.fillRect($0, $1, $2, $3); }, x, y, w, h, json.c_str());
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
// Win32 uses a DIB pixel loop; on web one fillRect per column is sufficient.
// ============================================================================

void Painter::fillColumnBars(int x, int y, int w, int h,
                             const std::vector<int> &barHeights, Color color)
{
    if (w <= 0 || h <= 0 || barHeights.empty())
        return;
    char col[32];
    cssColor(color, col, sizeof(col));
    int cols = std::min(w, (int)barHeights.size());
    for (int i = 0; i < cols; ++i)
    {
        int barH = std::max(0, std::min(h, barHeights[i]));
        if (barH == 0)
            continue;
        int bx = x + i;
        int by = y + h - barH;
        EM_ASM({
            var c = Module._fluxCtx2D;
            if (!c) return;
            c.fillStyle = UTF8ToString($3);
            c.fillRect($0, $1, 1, $2); }, bx, by, barH, col);
    }
}

// ============================================================================
// Painter::drawPolyline
// ============================================================================

void Painter::drawPolyline(const std::vector<std::pair<int, int>> &points,
                           Color color, int strokeWidth)
{
    if (points.size() < 2)
        return;
    char col[32];
    cssColor(color, col, sizeof(col));

    // Pass as flat Int32 array: [x0,y0,x1,y1,...]
    std::vector<int> flat;
    flat.reserve(points.size() * 2);
    for (auto &p : points)
    {
        flat.push_back(p.first);
        flat.push_back(p.second);
    }

    EM_ASM({
        var c = Module._fluxCtx2D;
        if (!c) return;
        var pts = new Int32Array(Module.HEAP32.buffer, $0, $1);
        c.strokeStyle = UTF8ToString($2);
        c.lineWidth   = $3;
        c.beginPath();
        c.moveTo(pts[0], pts[1]);
        for (var i = 2; i < $1; i += 2)
            c.lineTo(pts[i], pts[i + 1]);
        c.stroke(); }, flat.data(), (int)flat.size(), col, strokeWidth);
}

// ============================================================================
// Painter::fillPolygonAlpha
// ============================================================================

void Painter::fillPolygonAlpha(const std::vector<std::pair<int, int>> &points,
                               Color color)
{
    if (points.size() < 3)
        return;
    char col[32];
    cssColor(color, col, sizeof(col));

    std::vector<int> flat;
    flat.reserve(points.size() * 2);
    for (auto &p : points)
    {
        flat.push_back(p.first);
        flat.push_back(p.second);
    }

    EM_ASM({
        var c = Module._fluxCtx2D;
        if (!c) return;
        var pts = new Int32Array(Module.HEAP32.buffer, $0, $1);
        c.fillStyle = UTF8ToString($2);
        c.beginPath();
        c.moveTo(pts[0], pts[1]);
        for (var i = 2; i < $1; i += 2)
            c.lineTo(pts[i], pts[i + 1]);
        c.closePath();
        c.fill(); }, flat.data(), (int)flat.size(), col);
}

// ============================================================================
// Painter::drawArc
// startAngle / sweepAngle in radians; 0 = 3 o'clock, clockwise.
// ============================================================================

void Painter::drawArc(float cx, float cy, float radius,
                      int strokeWidth,
                      float startAngle, float sweepAngle,
                      Color color, bool roundedCaps)
{
    char col[32];
    cssColor(color, col, sizeof(col));
    int caps = roundedCaps ? 1 : 0;
    EM_ASM({
        var c = Module._fluxCtx2D;
        if (!c) return;
        c.strokeStyle = UTF8ToString($5);
        c.lineWidth   = $3;
        c.lineCap     = $6 ? "round" : "butt";
        c.beginPath();
        c.arc($0, $1, $2, $4, $4 + $7, false);
        c.stroke(); }, (double)cx, (double)cy, (double)radius, strokeWidth, (double)startAngle, col, caps, (double)sweepAngle);
}

// ============================================================================
// Painter::drawWavyLine
// ============================================================================

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

// ============================================================================
// Painter::drawFadeOverlay
// ============================================================================

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

// ============================================================================
// Painter::drawTextDecorationLine
// ============================================================================

void Painter::drawTextDecorationLine(int lineX, int lineY, int lineW,
                                     const TextStyle &style,
                                     TextDecoration which)
{
    if (lineW <= 0)
        return;
    int thickness = style.decorationThickness;
    Color dc = style.decorationColor;
    int fontSize = style.scaledFontSize();

    int decorY = lineY;
    if (which == TextDecoration::Underline)
        decorY = lineY + (int)(fontSize * 0.88f);
    else if (which == TextDecoration::Overline)
        decorY = lineY;
    else if (which == TextDecoration::LineThrough)
        decorY = lineY + (int)(fontSize * 0.45f);

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
        for (int p = lineX; p < lineX + lineW; p += 4)
            drawHLine(p, decorY, std::min(2, lineX + lineW - p), dc, thickness);
        break;
    case TextDecorationStyle::Dashed:
        for (int p = lineX; p < lineX + lineW; p += 8)
            drawHLine(p, decorY, std::min(5, lineX + lineW - p), dc, thickness);
        break;
    case TextDecorationStyle::Wavy:
        drawWavyLine(lineX, decorY, lineW, dc, 2);
        break;
    }
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
    const char *cssFont = static_cast<const char *>(font);
    if (!cssFont)
    {
        outWidth = outHeight = 0;
        return;
    }
    measureWebTextW(cssFont, text, parseFontSize(cssFont), outWidth, outHeight);
}

// ============================================================================
// Painter::drawText   (wide string)
// ============================================================================

void Painter::drawText(const std::wstring &text, int x, int y, int w, int h,
                       NativeFont font, Color color, UINT format)
{
    if (text.empty())
        return;
    const char *cssFont = static_cast<const char *>(font);
    if (!cssFont)
        return;

    std::string utf8 = wToUtf8(text);
    char col[32];
    cssColor(color, col, sizeof(col));

    // Horizontal alignment
    const char *align = "left";
    int drawX = x;
    if (dtCenter(format))
    {
        align = "center";
        drawX = x + w / 2;
    }
    else if (dtRight(format))
    {
        align = "right";
        drawX = x + w;
    }

    // Vertical alignment — textBaseline="top" so y = top of em-square
    int fontSize = parseFontSize(cssFont);
    int drawY = y;
    if (dtVCenter(format))
    {
        int lineH = (int)(fontSize * 1.2f);
        drawY = y + (h - lineH) / 2;
        if (drawY < y)
            drawY = y;
    }

    int ellipsis = (format & DT_END_ELLIPSIS) ? 1 : 0;

    EM_ASM({
        var c     = Module._fluxCtx2D;
        if (!c) return;
        var font  = UTF8ToString($5);
        var text  = UTF8ToString($6);
        var col   = UTF8ToString($7);
        var align = UTF8ToString($8);
        var maxW  = $3;
        var useE  = $9;

        c.save();
        c.font         = font;
        c.fillStyle    = col;
        c.textAlign    = align;
        c.textBaseline = "top";

        if (useE && maxW > 0 && c.measureText(text).width > maxW) {
            var el = "\u2026";
            var ew = c.measureText(el).width;
            while (text.length > 0 && c.measureText(text).width + ew > maxW)
                text = text.slice(0, -1);
            text = text + el;
        }

        c.fillText(text, $0, $1, maxW > 0 ? maxW : undefined);
        c.restore(); }, drawX, drawY, x, w, h, cssFont, utf8.c_str(), col, align, ellipsis);
}

// ============================================================================
// Painter::drawTextA
// ============================================================================

void Painter::drawTextA(const std::string &text, int x, int y, int w, int h,
                        NativeFont font, Color color, UINT format)
{
    if (text.empty())
        return;
    std::wstring ws(text.begin(), text.end());
    drawText(ws, x, y, w, h, font, color, format);
}

// ============================================================================
// Rich text helpers
// ============================================================================

namespace
{

    struct WebLineSpan
    {
        int start;
        int length;
    };

    double measureLineWeb(const char *cssFont, const std::string &utf8,
                          int start, int len)
    {
        if (len <= 0)
            return 0.0;
        std::string sub = utf8.substr(start, len);
        return EM_ASM_DOUBLE({
        var c = Module._fluxCtx2D;
        if (!c) return 0.0;
        c.font = UTF8ToString($0);
        return c.measureText(UTF8ToString($1)).width; }, cssFont, sub.c_str());
    }

    std::vector<WebLineSpan> wrapTextWeb(const char *cssFont,
                                         const std::string &utf8,
                                         int maxWidth, bool softWrap)
    {
        std::vector<WebLineSpan> lines;
        const int n = (int)utf8.size();
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
                int ls = pos;
                while (ls < nlPos)
                {
                    int lo = 0, hi = nlPos - ls, fit = 0;
                    while (lo <= hi)
                    {
                        int mid = (lo + hi) / 2;
                        if (measureLineWeb(cssFont, utf8, ls, mid) <= maxWidth)
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
                    int brk = fit;
                    if (ls + fit < nlPos)
                    {
                        int wb = fit;
                        while (wb > 1 && utf8[ls + wb - 1] != ' ')
                            --wb;
                        if (wb > 1)
                            brk = wb;
                    }
                    lines.push_back({ls, brk});
                    ls += brk;
                    while (ls < nlPos && utf8[ls] == ' ')
                        ++ls;
                }
            }
            pos = nlPos + 1;
            if (nlPos == n)
                break;
        }
        return lines;
    }

    int lineHeightPx(const char *cssFont, float heightMult)
    {
        return (int)(parseFontSize(cssFont) * 1.2f * heightMult);
    }

} // namespace

// ============================================================================
// Painter::measureRichText
// ============================================================================

void Painter::measureRichText(const std::wstring &wtext,
                              const TextStyle &style,
                              FontCache &fontCache,
                              int maxWidth, bool softWrap, int maxLines,
                              int &outWidth, int &outHeight)
{
    outWidth = outHeight = 0;
    if (wtext.empty())
        return;

    NativeFont fnt = fontCache.getFont(style.fontFamily,
                                       style.scaledFontSize(),
                                       style.fontWeight);
    const char *cssFont = static_cast<const char *>(fnt);
    if (!cssFont)
        return;

    std::string utf8 = wToUtf8(wtext);
    auto lines = wrapTextWeb(cssFont, utf8, maxWidth, softWrap);

    int lineH = lineHeightPx(cssFont, style.height);
    int total = (maxLines > 0)
                    ? std::min((int)lines.size(), maxLines)
                    : (int)lines.size();

    for (int i = 0; i < total; ++i)
    {
        double lw = measureLineWeb(cssFont, utf8, lines[i].start, lines[i].length);
        outWidth = std::max(outWidth, (int)std::ceil(lw));
    }
    outHeight = total * lineH;
}

// ============================================================================
// Painter::drawRichText
// ============================================================================

void Painter::drawRichText(const std::wstring &wtext,
                           const RichTextParams &params,
                           FontCache &fontCache)
{
    if (wtext.empty() || params.w <= 0 || params.h <= 0)
        return;

    const TextStyle &style = params.style;
    bool underline = hasDecoration(style.decoration, TextDecoration::Underline);
    bool strikeOut = hasDecoration(style.decoration, TextDecoration::LineThrough);

    NativeFont fnt = fontCache.getFont(style.fontFamily,
                                       style.scaledFontSize(),
                                       style.fontWeight,
                                       underline, strikeOut);
    const char *cssFont = static_cast<const char *>(fnt);
    if (!cssFont)
        return;

    std::string utf8 = wToUtf8(wtext);
    int wrapWidth = params.softWrap ? params.w : 0;
    auto lines = wrapTextWeb(cssFont, utf8, wrapWidth, params.softWrap);

    int lineH = lineHeightPx(cssFont, style.height);
    int total = (params.maxLines > 0)
                    ? std::min((int)lines.size(), params.maxLines)
                    : (int)lines.size();

    // ── Vertical alignment ─────────────────────────────────────────────────
    int blockH = total * lineH;
    int startY = params.y;
    if (params.textAlignVertical == TextAlignVertical::Center)
        startY = params.y + (params.h - blockH) / 2;
    else if (params.textAlignVertical == TextAlignVertical::Bottom)
        startY = params.y + params.h - blockH;

    // ── Clipping ───────────────────────────────────────────────────────────
    bool needClip = (params.overflow != TextOverflow::Visible);
    if (needClip)
        pushClipRect(params.x, params.y, params.w, params.h);

    char col[32];
    cssColor(style.color, col, sizeof(col));

    // Set font + baseline once; individual draw calls restore as needed.
    EM_ASM({
        var c = Module._fluxCtx2D;
        if (!c) return;
        c.font         = UTF8ToString($0);
        c.textBaseline = "top";
        c.fillStyle    = UTF8ToString($1); }, cssFont, col);

    // ── Draw lines ─────────────────────────────────────────────────────────
    for (int i = 0; i < total; ++i)
    {
        const auto &span = lines[i];
        std::string lineU = utf8.substr(span.start, span.length);

        int lineY = startY + i * lineH;
        if (lineY + lineH < params.y)
            continue;
        if (lineY > params.y + params.h)
            break;

        int lineWi = (int)std::ceil(measureLineWeb(cssFont, utf8,
                                                   span.start, span.length));

        // ── Horizontal alignment ────────────────────────────────────────
        int lineX = params.x;
        bool isRTL = (params.direction == TextDirection::RTL);
        bool isLast = (i == total - 1);

        switch (params.textAlign)
        {
        case TextAlign::Center:
            lineX = params.x + (params.w - lineWi) / 2;
            break;
        case TextAlign::Right:
        case TextAlign::End:
            lineX = isRTL ? params.x : (params.x + params.w - lineWi);
            break;
        case TextAlign::Left:
        case TextAlign::Start:
            lineX = isRTL ? (params.x + params.w - lineWi) : params.x;
            break;
        case TextAlign::Justify:
        {
            if (!isLast && lineWi < params.w)
            {
                // Count spaces
                int spaces = 0;
                for (char ch : lineU)
                    if (ch == ' ')
                        ++spaces;
                if (spaces > 0)
                {
                    double extra = (double)(params.w - lineWi) / spaces;
                    double curX = params.x;
                    int wstart = 0;
                    for (int k = 0; k <= (int)lineU.size(); ++k)
                    {
                        bool end = (k == (int)lineU.size() || lineU[k] == ' ');
                        if (end && k > wstart)
                        {
                            std::string word = lineU.substr(wstart, k - wstart);
                            int wx = (int)curX;
                            EM_ASM({
                                var c = Module._fluxCtx2D;
                                if (c) c.fillText(UTF8ToString($1), $0, $2); }, wx, word.c_str(), lineY);
                            double ww = measureLineWeb(cssFont, lineU, wstart, k - wstart);
                            curX += ww;
                            if (k < (int)lineU.size())
                                curX += extra;
                            wstart = k + 1;
                        }
                    }
                    goto draw_decorations;
                }
            }
            lineX = params.x;
            break;
        }
        }

        // ── Per-line background ─────────────────────────────────────────
        if (style.backgroundColor.has_value())
            fillRect(lineX, lineY, lineWi, lineH, *style.backgroundColor);

        // ── Shadows ────────────────────────────────────────────────────
        for (const auto &sh : style.shadows)
        {
            char shCol[32];
            cssColor(sh.color, shCol, sizeof(shCol));
            EM_ASM({
                var c = Module._fluxCtx2D;
                if (!c) return;
                c.fillStyle = UTF8ToString($2);
                c.fillText(UTF8ToString($3), $0, $1);
                c.fillStyle = UTF8ToString($4); }, lineX + sh.offsetX, lineY + sh.offsetY, shCol, lineU.c_str(), col);
        }

        // ── Ellipsis ───────────────────────────────────────────────────
        if (isLast && (int)lines.size() > total &&
            params.overflow == TextOverflow::Ellipsis)
        {
            EM_ASM({
                var c    = Module._fluxCtx2D;
                if (!c) return;
                var text = UTF8ToString($1);
                var maxW = $2;
                var el   = "\u2026";
                var ew   = c.measureText(el).width;
                c.fillStyle = UTF8ToString($3);
                while (text.length > 0 && c.measureText(text).width + ew > maxW)
                    text = text.slice(0, -1);
                c.fillText(text + el, $0, $4); }, lineX, lineU.c_str(), params.w, col, lineY);
            goto draw_decorations;
        }

        // ── Normal fillText ─────────────────────────────────────────────
        EM_ASM({
            var c = Module._fluxCtx2D;
            if (c) c.fillText(UTF8ToString($1), $0, $2); }, lineX, lineU.c_str(), lineY);

    draw_decorations:

        // ── Text decorations ────────────────────────────────────────────
        if (style.decoration != TextDecoration::None)
        {
            if (style.hasOverline())
                drawTextDecorationLine(lineX, lineY, lineWi, style,
                                       TextDecoration::Overline);
            if (style.hasUnderline())
                drawTextDecorationLine(lineX, lineY, lineWi, style,
                                       TextDecoration::Underline);
            if (style.hasLineThrough())
                drawTextDecorationLine(lineX, lineY, lineWi, style,
                                       TextDecoration::LineThrough);
        }

        // ── Fade overlay ────────────────────────────────────────────────
        if (isLast && (int)lines.size() > total &&
            params.overflow == TextOverflow::Fade)
        {
            int fadeW = std::min(60, params.w / 3);
            drawFadeOverlay(params.x, lineY, params.w, lineH, fadeW,
                            Color::fromRGB(255, 255, 255));
        }
    }

    if (needClip)
        popClipRect();
}

// ============================================================================
// Painter::drawRichTextA
// ============================================================================

void Painter::drawRichTextA(const std::string &text,
                            const RichTextParams &params,
                            FontCache &fontCache)
{
    if (text.empty())
        return;
    std::wstring ws(text.begin(), text.end());
    drawRichText(ws, params, fontCache);
}

#endif // __EMSCRIPTEN__
