// flux_painter_win32.cpp
#include "flux/flux_painter.hpp"

#ifdef _WIN32

// -----------------------------------------------------------------------
// Internal helper — builds a rounded rect path (GDI+)
// -----------------------------------------------------------------------

static void makeRoundedPath(Gdiplus::GraphicsPath &path, int x, int y, int w,
                             int h, int r) {
    int d = r * 2;
    path.AddArc(x,         y,         d, d, 180, 90);
    path.AddArc(x + w - d, y,         d, d, 270, 90);
    path.AddArc(x + w - d, y + h - d, d, d,   0, 90);
    path.AddArc(x,         y + h - d, d, d,  90, 90);
    path.CloseFigure();
}

// -----------------------------------------------------------------------
// Painter::fillRoundedRect  (GDI+)
// -----------------------------------------------------------------------

void Painter::fillRoundedRect(int x, int y, int w, int h, int radius,
                               Color color) {
    Gdiplus::Graphics g(ctx.hdc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

    Gdiplus::SolidBrush brush(toGdipColor(color));

    if (radius > 0) {
        Gdiplus::GraphicsPath path;
        makeRoundedPath(path, x, y, w, h, radius);
        g.FillPath(&brush, &path);
    } else {
        Gdiplus::RectF rect((Gdiplus::REAL)x, (Gdiplus::REAL)y,
                            (Gdiplus::REAL)w, (Gdiplus::REAL)h);
        g.FillRectangle(&brush, rect);
    }
}

// -----------------------------------------------------------------------
// Painter::drawBorder  (GDI+)
// -----------------------------------------------------------------------

void Painter::drawBorder(int x, int y, int w, int h, int radius,
                          Color color, int borderWidth) {
    Gdiplus::Graphics g(ctx.hdc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

    Gdiplus::Pen pen(toGdipColor(color), (Gdiplus::REAL)borderWidth);

    if (radius > 0) {
        Gdiplus::GraphicsPath path;
        makeRoundedPath(path, x, y, w, h, radius);
        g.DrawPath(&pen, &path);
    } else {
        Gdiplus::RectF rect((Gdiplus::REAL)x, (Gdiplus::REAL)y,
                            (Gdiplus::REAL)w, (Gdiplus::REAL)h);
        g.DrawRectangle(&pen, rect);
    }
}

// -----------------------------------------------------------------------
// Painter::fillRect  (GDI)
// -----------------------------------------------------------------------

void Painter::fillRect(int x, int y, int w, int h, Color color) {
    RECT rect = {x, y, x + w, y + h};
    HBRUSH brush = CreateSolidBrush(toColorRef(color));
    ::FillRect(ctx.hdc, &rect, brush);
    DeleteObject(brush);
}

// -----------------------------------------------------------------------
// Painter::fillRoundedRectGDI
// radius is the corner ellipse diameter (nWidth/nHeight in MSDN terms).
// -----------------------------------------------------------------------

void Painter::fillRoundedRectGDI(int x, int y, int w, int h, int radius,
                                  Color fill, Color stroke, int strokeWidth) {
    HBRUSH hBrush = CreateSolidBrush(toColorRef(fill));
    HPEN   hPen   = (strokeWidth > 0)
                        ? CreatePen(PS_SOLID, strokeWidth, toColorRef(stroke))
                        : (HPEN)GetStockObject(NULL_PEN);

    HBRUSH oldBrush = (HBRUSH)SelectObject(ctx.hdc, hBrush);
    HPEN   oldPen   = (HPEN)  SelectObject(ctx.hdc, hPen);

    RoundRect(ctx.hdc, x, y, x + w, y + h, radius, radius);

    SelectObject(ctx.hdc, oldBrush);
    SelectObject(ctx.hdc, oldPen);
    DeleteObject(hBrush);
    if (strokeWidth > 0)
        DeleteObject(hPen);
}

// -----------------------------------------------------------------------
// Painter::drawEllipse
// strokeWidth == 0 → PS_NULL pen (fill only).
// -----------------------------------------------------------------------

void Painter::drawEllipse(int x, int y, int w, int h,
                           Color fill, Color stroke, int strokeWidth) {
    HBRUSH hBrush = CreateSolidBrush(toColorRef(fill));
    HPEN   hPen   = (strokeWidth > 0)
                        ? CreatePen(PS_SOLID, strokeWidth, toColorRef(stroke))
                        : (HPEN)GetStockObject(NULL_PEN);

    HBRUSH oldBrush = (HBRUSH)SelectObject(ctx.hdc, hBrush);
    HPEN   oldPen   = (HPEN)  SelectObject(ctx.hdc, hPen);

    Ellipse(ctx.hdc, x, y, x + w, y + h);

    SelectObject(ctx.hdc, oldBrush);
    SelectObject(ctx.hdc, oldPen);
    DeleteObject(hBrush);
    if (strokeWidth > 0)
        DeleteObject(hPen);
}

// -----------------------------------------------------------------------
// Painter::drawLine
// -----------------------------------------------------------------------

void Painter::drawLine(int x1, int y1, int x2, int y2, Color color, int width) {
    HPEN hPen   = CreatePen(PS_SOLID, width, toColorRef(color));
    HPEN oldPen = (HPEN)SelectObject(ctx.hdc, hPen);

    MoveToEx(ctx.hdc, x1, y1, nullptr);
    LineTo(ctx.hdc, x2, y2);

    SelectObject(ctx.hdc, oldPen);
    DeleteObject(hPen);
}

// -----------------------------------------------------------------------
// Painter::drawText
// -----------------------------------------------------------------------

void Painter::drawText(const std::wstring &text, int x, int y, int w, int h,
                        NativeFont font, Color color, UINT format) {
    if (text.empty())
        return;

    SetTextColor(ctx.hdc, toColorRef(color));
    SetBkMode(ctx.hdc, TRANSPARENT);

    HFONT hOldFont = (HFONT)SelectObject(ctx.hdc, font);

    RECT rect = {x, y, x + w, y + h};
    DrawTextW(ctx.hdc, text.c_str(), -1, &rect, format);

    SelectObject(ctx.hdc, hOldFont);
}

// -----------------------------------------------------------------------
// Painter::measureText
// -----------------------------------------------------------------------

void Painter::measureText(const std::wstring &text, NativeFont font,
                           int &outWidth, int &outHeight) {
    if (text.empty()) {
        outWidth = outHeight = 0;
        return;
    }

    HFONT oldFont = (HFONT)SelectObject(ctx.hdc, font);

    SIZE sz = {};
    GetTextExtentPoint32W(ctx.hdc, text.c_str(),
                          static_cast<int>(text.size()), &sz);
    outWidth  = sz.cx;
    outHeight = sz.cy;

    SelectObject(ctx.hdc, oldFont);
}

// -----------------------------------------------------------------------
// Painter::pushClipRect / popClipRect
// -----------------------------------------------------------------------

void Painter::pushClipRect(int x, int y, int w, int h) {
    HRGN rgn = CreateRectRgn(x, y, x + w, y + h);
    SelectClipRgn(ctx.hdc, rgn);
    DeleteObject(rgn);
}

void Painter::popClipRect() {
    SelectClipRgn(ctx.hdc, nullptr);
}

// -----------------------------------------------------------------------
// Painter::fillGradientRect
// Uses Color::interpolate — no platform color math needed here.
// -----------------------------------------------------------------------

void Painter::fillGradientRect(int x, int y, int w, int h,
                                const std::vector<Color> &colors) {
    if (colors.empty() || w <= 0 || h <= 0)
        return;

    if (colors.size() == 1) {
        fillRect(x, y, w, h, colors[0]);
        return;
    }

    const int stops = static_cast<int>(colors.size());

    for (int i = 0; i < w; ++i) {
        double t      = static_cast<double>(i) / (w - 1);
        double scaled = t * (stops - 1);
        int    idx    = static_cast<int>(scaled);
        double frac   = scaled - idx;

        if (idx >= stops - 1) { idx = stops - 2; frac = 1.0; }

        Color c = colors[idx].interpolate(colors[idx + 1], frac);

        HBRUSH band = CreateSolidBrush(toColorRef(c));
        RECT   col  = {x + i, y, x + i + 1, y + h};
        ::FillRect(ctx.hdc, &col, band);
        DeleteObject(band);
    }
}

// -----------------------------------------------------------------------
// Painter::drawRectOutline
// -----------------------------------------------------------------------

void Painter::drawRectOutline(int x, int y, int w, int h,
                               Color color, int strokeWidth) {
    HPEN   hPen     = CreatePen(PS_SOLID, strokeWidth, toColorRef(color));
    HPEN   oldPen   = (HPEN)  SelectObject(ctx.hdc, hPen);
    HBRUSH oldBrush = (HBRUSH)SelectObject(ctx.hdc, GetStockObject(NULL_BRUSH));

    Rectangle(ctx.hdc, x, y, x + w, y + h);

    SelectObject(ctx.hdc, oldBrush);
    SelectObject(ctx.hdc, oldPen);
    DeleteObject(hPen);
}

// -----------------------------------------------------------------------
// Painter::pushClipRoundedRect
// -----------------------------------------------------------------------

void Painter::pushClipRoundedRect(int x, int y, int w, int h,
                                   int cornerDiameter) {
    HRGN rgn = CreateRoundRectRgn(x, y, x + w, y + h,
                                   cornerDiameter, cornerDiameter);
    SelectClipRgn(ctx.hdc, rgn);
    DeleteObject(rgn);
}

// -----------------------------------------------------------------------
// Painter::drawRoundedRectOutline
// -----------------------------------------------------------------------

void Painter::drawRoundedRectOutline(int x, int y, int w, int h,
                                      int cornerDiameter,
                                      Color stroke, int strokeWidth) {
    HPEN   hPen     = CreatePen(PS_SOLID, strokeWidth, toColorRef(stroke));
    HPEN   oldPen   = (HPEN)  SelectObject(ctx.hdc, hPen);
    HBRUSH oldBrush = (HBRUSH)SelectObject(ctx.hdc, GetStockObject(NULL_BRUSH));

    RoundRect(ctx.hdc, x, y, x + w, y + h, cornerDiameter, cornerDiameter);

    SelectObject(ctx.hdc, oldBrush);
    SelectObject(ctx.hdc, oldPen);
    DeleteObject(hPen);
}

// -----------------------------------------------------------------------
// Painter::fillRectAlpha
// Alpha is taken from color.a — no separate alpha parameter.
// -----------------------------------------------------------------------

void Painter::fillRectAlpha(int x, int y, int w, int h, Color color) {
    HDC     tmpDC  = CreateCompatibleDC(ctx.hdc);
    HBITMAP tmpBmp = CreateCompatibleBitmap(ctx.hdc, w, h);
    HBITMAP tmpOld = (HBITMAP)SelectObject(tmpDC, tmpBmp);

    HBRUSH br = CreateSolidBrush(toColorRef(color));
    RECT   rc = {0, 0, w, h};
    FillRect(tmpDC, &rc, br);
    DeleteObject(br);

    BLENDFUNCTION bf = {AC_SRC_OVER, 0, color.a, 0};
    AlphaBlend(ctx.hdc, x, y, w, h, tmpDC, 0, 0, w, h, bf);

    SelectObject(tmpDC, tmpOld);
    DeleteObject(tmpBmp);
    DeleteDC(tmpDC);
}

// -----------------------------------------------------------------------
// Painter::drawTextA  (UTF-8 narrow → wide, then drawText)
// -----------------------------------------------------------------------

void Painter::drawTextA(const std::string &text, int x, int y, int w, int h,
                         NativeFont font, Color color, UINT format) {
    if (text.empty())
        return;
    int          wlen = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    std::wstring wtext(wlen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, wtext.data(), wlen);
    drawText(wtext, x, y, w, h, font, color, format);
}

// -----------------------------------------------------------------------
// Painter::fillRoundedRegion
// -----------------------------------------------------------------------

void Painter::fillRoundedRegion(int x, int y, int w, int h,
                                 int cornerRadius, Color color) {
    HBRUSH hb  = CreateSolidBrush(toColorRef(color));
    HRGN   rgn = CreateRoundRectRgn(x, y, x + w, y + h,
                                     cornerRadius, cornerRadius);
    FillRgn(ctx.hdc, rgn, hb);
    DeleteObject(rgn);
    DeleteObject(hb);
}

// -----------------------------------------------------------------------
// Painter::drawHLine / drawVLine
// -----------------------------------------------------------------------

void Painter::drawHLine(int x, int y, int len, Color color, int strokeWidth) {
    drawLine(x, y, x + len, y, color, strokeWidth);
}

void Painter::drawVLine(int x, int y, int len, Color color, int strokeWidth) {
    drawLine(x, y, x, y + len, color, strokeWidth);
}

// -----------------------------------------------------------------------
// Painter::fillRectWithLeftAccent
// -----------------------------------------------------------------------

void Painter::fillRectWithLeftAccent(int x, int y, int w, int h,
                                      Color bg, Color accent, int stripWidth) {
    fillRect(x, y, w, h, bg);
    fillRect(x, y, stripWidth, h, accent);
}

// -----------------------------------------------------------------------
// Painter::fillColumnBars
// Alpha from color.a — no separate parameter.
// -----------------------------------------------------------------------

void Painter::fillColumnBars(int x, int y, int w, int h,
                              const std::vector<int> &barHeights, Color color) {
    if (w <= 0 || h <= 0 || barHeights.empty())
        return;

    BITMAPINFO bmi                  = {};
    bmi.bmiHeader.biSize            = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth           = w;
    bmi.bmiHeader.biHeight          = -h;
    bmi.bmiHeader.biPlanes          = 1;
    bmi.bmiHeader.biBitCount        = 32;
    bmi.bmiHeader.biCompression     = BI_RGB;

    void   *bits = nullptr;
    HBITMAP hBmp = CreateDIBSection(ctx.hdc, &bmi, DIB_RGB_COLORS,
                                    &bits, nullptr, 0);
    if (!hBmp) return;

    HDC memDC = CreateCompatibleDC(ctx.hdc);
    SelectObject(memDC, hBmp);
    memset(bits, 0, w * h * 4);

    int cols = min(w, (int)barHeights.size());
    for (int px = 0; px < cols; ++px) {
        int barH = max(0, min(h, barHeights[px]));
        for (int py = h - barH; py < h; ++py) {
            uint8_t *pixel = (uint8_t *)bits + (py * w + px) * 4;
            pixel[0] = color.b;
            pixel[1] = color.g;
            pixel[2] = color.r;
            pixel[3] = 255;
        }
    }

    BLENDFUNCTION bf           = {};
    bf.BlendOp                 = AC_SRC_OVER;
    bf.SourceConstantAlpha     = color.a;
    bf.AlphaFormat             = 0;
    AlphaBlend(ctx.hdc, x, y, w, h, memDC, 0, 0, w, h, bf);

    DeleteDC(memDC);
    DeleteObject(hBmp);
}

// -----------------------------------------------------------------------
// Painter::drawPolyline
// -----------------------------------------------------------------------

void Painter::drawPolyline(const std::vector<std::pair<int, int>> &points,
                            Color color, int strokeWidth) {
    if (points.size() < 2)
        return;

    std::vector<POINT> pts(points.size());
    for (size_t i = 0; i < points.size(); ++i)
        pts[i] = {points[i].first, points[i].second};

    HPEN hPen = CreatePen(PS_SOLID, strokeWidth, toColorRef(color));
    HPEN old  = (HPEN)SelectObject(ctx.hdc, hPen);
    Polyline(ctx.hdc, pts.data(), (int)pts.size());
    SelectObject(ctx.hdc, old);
    DeleteObject(hPen);
}

// -----------------------------------------------------------------------
// Painter::fillPolygonAlpha
// Alpha from color.a — no separate parameter.
// -----------------------------------------------------------------------

void Painter::fillPolygonAlpha(const std::vector<std::pair<int, int>> &points,
                                Color color) {
    if (points.size() < 3)
        return;

    int x0 = points[0].first,  y0 = points[0].second;
    int x1 = x0,               y1 = y0;
    for (auto &p : points) {
        x0 = min(x0, p.first);  y0 = min(y0, p.second);
        x1 = max(x1, p.first);  y1 = max(y1, p.second);
    }
    int w = x1 - x0, h = y1 - y0;
    if (w <= 0 || h <= 0)
        return;

    BITMAPINFO bmi              = {};
    bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth       = w;
    bmi.bmiHeader.biHeight      = -h;
    bmi.bmiHeader.biPlanes      = 1;
    bmi.bmiHeader.biBitCount    = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void   *bits = nullptr;
    HBITMAP hBmp = CreateDIBSection(ctx.hdc, &bmi, DIB_RGB_COLORS,
                                    &bits, nullptr, 0);
    if (!hBmp) return;

    HDC mdc = CreateCompatibleDC(ctx.hdc);
    SelectObject(mdc, hBmp);
    memset(bits, 0, w * h * 4);

    std::vector<POINT> poly(points.size());
    for (size_t i = 0; i < points.size(); ++i)
        poly[i] = {points[i].first - x0, points[i].second - y0};

    HPEN   nullPen = CreatePen(PS_NULL, 0, 0);
    HBRUSH fillBr  = CreateSolidBrush(toColorRef(color));
    HPEN   op      = (HPEN)  SelectObject(mdc, nullPen);
    HBRUSH ob      = (HBRUSH)SelectObject(mdc, fillBr);
    Polygon(mdc, poly.data(), (int)poly.size());
    SelectObject(mdc, op);
    SelectObject(mdc, ob);
    DeleteObject(nullPen);
    DeleteObject(fillBr);

    BLENDFUNCTION bf = {AC_SRC_OVER, 0, color.a, 0};
    AlphaBlend(ctx.hdc, x0, y0, w, h, mdc, 0, 0, w, h, bf);
    DeleteDC(mdc);
    DeleteObject(hBmp);
}

#endif // _WIN32