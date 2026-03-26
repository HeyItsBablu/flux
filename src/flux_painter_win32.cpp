#include "flux/flux_painter.hpp"

#ifdef _WIN32

// -----------------------------------------------------------------------
// Internal helper — builds a rounded rect path
// -----------------------------------------------------------------------

static void makeRoundedPath(Gdiplus::GraphicsPath &path,
                             int x, int y, int w, int h, int r) {
    int d = r * 2;
    path.AddArc(x,         y,         d, d, 180, 90);
    path.AddArc(x + w - d, y,         d, d, 270, 90);
    path.AddArc(x + w - d, y + h - d, d, d,   0, 90);
    path.AddArc(x,         y + h - d, d, d,  90, 90);
    path.CloseFigure();
}

// -----------------------------------------------------------------------
// Painter::fillRoundedRect
// -----------------------------------------------------------------------

void Painter::fillRoundedRect(int x, int y, int w, int h, int radius,
                               NativeColor color, BYTE alpha) {
    Gdiplus::Graphics g(ctx.hdc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

    Gdiplus::Color fillColor(alpha,
                             GetRValue(color),
                             GetGValue(color),
                             GetBValue(color));
    Gdiplus::SolidBrush brush(fillColor);

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
// Painter::drawBorder
// -----------------------------------------------------------------------

void Painter::drawBorder(int x, int y, int w, int h, int radius,
                          NativeColor color, int borderWidth, BYTE alpha) {
    Gdiplus::Graphics g(ctx.hdc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

    Gdiplus::Color strokeColor(alpha,
                               GetRValue(color),
                               GetGValue(color),
                               GetBValue(color));
    Gdiplus::Pen pen(strokeColor, (Gdiplus::REAL)borderWidth);

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
// Painter::drawText
// -----------------------------------------------------------------------

void Painter::drawText(const std::wstring &text, int x, int y, int w, int h,
                        NativeFont font, NativeColor color, UINT format) {
    if (text.empty())
        return;

    SetTextColor(ctx.hdc, color);
    SetBkMode(ctx.hdc, TRANSPARENT);

    HFONT hOldFont = (HFONT)SelectObject(ctx.hdc, font);

    RECT rect = { x, y, x + w, y + h };
    DrawTextW(ctx.hdc, text.c_str(), -1, &rect, format);

    SelectObject(ctx.hdc, hOldFont);
}

// -----------------------------------------------------------------------
// Painter::fillRect
// -----------------------------------------------------------------------

void Painter::fillRect(int x, int y, int w, int h, NativeColor color) {
    RECT rect = { x, y, x + w, y + h };
    HBRUSH brush = CreateSolidBrush(color);
    ::FillRect(ctx.hdc, &rect, brush);
    DeleteObject(brush);
}

#endif // _WIN32