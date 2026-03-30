// flux_painter_win32.cpp
#include "flux/flux_painter.hpp"

#ifdef _WIN32

// -----------------------------------------------------------------------
// Internal helper — builds a rounded rect path (GDI+)
// -----------------------------------------------------------------------

static void makeRoundedPath(Gdiplus::GraphicsPath &path, int x, int y, int w,
                            int h, int r) {
  int d = r * 2;
  path.AddArc(x, y, d, d, 180, 90);
  path.AddArc(x + w - d, y, d, d, 270, 90);
  path.AddArc(x + w - d, y + h - d, d, d, 0, 90);
  path.AddArc(x, y + h - d, d, d, 90, 90);
  path.CloseFigure();
}

// -----------------------------------------------------------------------
// Painter::fillRoundedRect  (GDI+ — unchanged from original)
// -----------------------------------------------------------------------

void Painter::fillRoundedRect(int x, int y, int w, int h, int radius,
                              NativeColor color, BYTE alpha) {
  Gdiplus::Graphics g(ctx.hdc);
  g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

  Gdiplus::Color fillColor(alpha, GetRValue(color), GetGValue(color),
                           GetBValue(color));
  Gdiplus::SolidBrush brush(fillColor);

  if (radius > 0) {
    Gdiplus::GraphicsPath path;
    makeRoundedPath(path, x, y, w, h, radius);
    g.FillPath(&brush, &path);
  } else {
    Gdiplus::RectF rect((Gdiplus::REAL)x, (Gdiplus::REAL)y, (Gdiplus::REAL)w,
                        (Gdiplus::REAL)h);
    g.FillRectangle(&brush, rect);
  }
}

// -----------------------------------------------------------------------
// Painter::drawBorder  (GDI+ — unchanged from original)
// -----------------------------------------------------------------------

void Painter::drawBorder(int x, int y, int w, int h, int radius,
                         NativeColor color, int borderWidth, BYTE alpha) {
  Gdiplus::Graphics g(ctx.hdc);
  g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

  Gdiplus::Color strokeColor(alpha, GetRValue(color), GetGValue(color),
                             GetBValue(color));
  Gdiplus::Pen pen(strokeColor, (Gdiplus::REAL)borderWidth);

  if (radius > 0) {
    Gdiplus::GraphicsPath path;
    makeRoundedPath(path, x, y, w, h, radius);
    g.DrawPath(&pen, &path);
  } else {
    Gdiplus::RectF rect((Gdiplus::REAL)x, (Gdiplus::REAL)y, (Gdiplus::REAL)w,
                        (Gdiplus::REAL)h);
    g.DrawRectangle(&pen, rect);
  }
}

// -----------------------------------------------------------------------
// Painter::fillRect  (GDI — unchanged from original)
// -----------------------------------------------------------------------

void Painter::fillRect(int x, int y, int w, int h, NativeColor color) {
  RECT rect = {x, y, x + w, y + h};
  HBRUSH brush = CreateSolidBrush(color);
  ::FillRect(ctx.hdc, &rect, brush);
  DeleteObject(brush);
}

// -----------------------------------------------------------------------
// Painter::fillRoundedRectGDI
// Wraps Win32 RoundRect — radius is the corner ellipse diameter (nWidth /
// nHeight in MSDN terms), so a caller that used RoundRect(..., r*2, r*2)
// should now call fillRoundedRectGDI(..., r*2, ...).
// -----------------------------------------------------------------------

void Painter::fillRoundedRectGDI(int x, int y, int w, int h, int radius,
                                 NativeColor fill, NativeColor stroke,
                                 int strokeWidth) {
  HBRUSH hBrush = CreateSolidBrush(fill);
  HPEN hPen = (strokeWidth > 0) ? CreatePen(PS_SOLID, strokeWidth, stroke)
                                : (HPEN)GetStockObject(NULL_PEN);

  HBRUSH oldBrush = (HBRUSH)SelectObject(ctx.hdc, hBrush);
  HPEN oldPen = (HPEN)SelectObject(ctx.hdc, hPen);

  RoundRect(ctx.hdc, x, y, x + w, y + h, radius, radius);

  SelectObject(ctx.hdc, oldBrush);
  SelectObject(ctx.hdc, oldPen);
  DeleteObject(hBrush);
  if (strokeWidth > 0)
    DeleteObject(hPen);
}

// -----------------------------------------------------------------------
// Painter::drawEllipse
// strokeWidth == 0  → PS_NULL pen (fill only, no border).
// -----------------------------------------------------------------------

void Painter::drawEllipse(int x, int y, int w, int h, NativeColor fill,
                          NativeColor stroke, int strokeWidth) {
  HBRUSH hBrush = CreateSolidBrush(fill);
  HPEN hPen = (strokeWidth > 0) ? CreatePen(PS_SOLID, strokeWidth, stroke)
                                : (HPEN)GetStockObject(NULL_PEN);

  HBRUSH oldBrush = (HBRUSH)SelectObject(ctx.hdc, hBrush);
  HPEN oldPen = (HPEN)SelectObject(ctx.hdc, hPen);

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

void Painter::drawLine(int x1, int y1, int x2, int y2, NativeColor color,
                       int width) {
  HPEN hPen = CreatePen(PS_SOLID, width, color);
  HPEN oldPen = (HPEN)SelectObject(ctx.hdc, hPen);

  MoveToEx(ctx.hdc, x1, y1, nullptr);
  LineTo(ctx.hdc, x2, y2);

  SelectObject(ctx.hdc, oldPen);
  DeleteObject(hPen);
}

// -----------------------------------------------------------------------
// Painter::drawText  (unchanged from original)
// -----------------------------------------------------------------------

void Painter::drawText(const std::wstring &text, int x, int y, int w, int h,
                       NativeFont font, NativeColor color, UINT format) {
  if (text.empty())
    return;

  SetTextColor(ctx.hdc, color);
  SetBkMode(ctx.hdc, TRANSPARENT);

  HFONT hOldFont = (HFONT)SelectObject(ctx.hdc, font);

  RECT rect = {x, y, x + w, y + h};
  DrawTextW(ctx.hdc, text.c_str(), -1, &rect, format);

  SelectObject(ctx.hdc, hOldFont);
}

// -----------------------------------------------------------------------
// Painter::measureText
// Fills outWidth / outHeight with the bounding box of the string.
// Does not draw anything.
// -----------------------------------------------------------------------

void Painter::measureText(const std::wstring &text, NativeFont font,
                          int &outWidth, int &outHeight) {
  if (text.empty()) {
    outWidth = outHeight = 0;
    return;
  }

  HFONT oldFont = (HFONT)SelectObject(ctx.hdc, font);

  SIZE sz = {};
  GetTextExtentPoint32W(ctx.hdc, text.c_str(), static_cast<int>(text.size()),
                        &sz);
  outWidth = sz.cx;
  outHeight = sz.cy;

  SelectObject(ctx.hdc, oldFont);
}

// -----------------------------------------------------------------------
// Painter::pushClipRect
// Intersects a rectangular clip region with the current clip.
// Call popClipRect() once to remove it.
// -----------------------------------------------------------------------

void Painter::pushClipRect(int x, int y, int w, int h) {
  HRGN rgn = CreateRectRgn(x, y, x + w, y + h);
  SelectClipRgn(ctx.hdc, rgn);
  DeleteObject(rgn); // GDI copies it internally — safe to free immediately
}

// -----------------------------------------------------------------------
// Painter::popClipRect
// Removes all clip regions, restoring unrestricted painting.
// -----------------------------------------------------------------------

void Painter::popClipRect() { SelectClipRgn(ctx.hdc, nullptr); }

// -----------------------------------------------------------------------
// Painter::fillGradientRect
// Single color → plain fillRect.
// Multiple colors → one vertical band per pixel, interpolating across
// the color stops linearly.  Same logic as ProgressBarWidget had inline,
// now centralised here.
// -----------------------------------------------------------------------

void Painter::fillGradientRect(int x, int y, int w, int h,
                               const std::vector<NativeColor> &colors) {
  if (colors.empty() || w <= 0 || h <= 0)
    return;

  if (colors.size() == 1) {
    fillRect(x, y, w, h, colors[0]);
    return;
  }

  const int stops = static_cast<int>(colors.size());

  for (int i = 0; i < w; ++i) {
    double t = static_cast<double>(i) / (w - 1); // 0.0 … 1.0
    double scaled = t * (stops - 1);
    int idx = static_cast<int>(scaled);
    double frac = scaled - idx;

    if (idx >= stops - 1) {
      idx = stops - 2;
      frac = 1.0;
    }

    NativeColor c0 = colors[idx];
    NativeColor c1 = colors[idx + 1];

    int r = static_cast<int>(GetRValue(c0) +
                             frac * (GetRValue(c1) - GetRValue(c0)));
    int g = static_cast<int>(GetGValue(c0) +
                             frac * (GetGValue(c1) - GetGValue(c0)));
    int b = static_cast<int>(GetBValue(c0) +
                             frac * (GetBValue(c1) - GetBValue(c0)));

    HBRUSH band = CreateSolidBrush(RGB(r, g, b));
    RECT col = {x + i, y, x + i + 1, y + h};
    ::FillRect(ctx.hdc, &col, band);
    DeleteObject(band);
  }
}

// flux_painter_win32.cpp — implementation
void Painter::drawRectOutline(int x, int y, int w, int h, NativeColor color,
                              int strokeWidth) {
  HPEN hPen = CreatePen(PS_SOLID, strokeWidth, color);
  HPEN oldPen = (HPEN)SelectObject(ctx.hdc, hPen);
  HBRUSH oldBrush = (HBRUSH)SelectObject(ctx.hdc, GetStockObject(NULL_BRUSH));

  Rectangle(ctx.hdc, x, y, x + w, y + h);

  SelectObject(ctx.hdc, oldBrush);
  SelectObject(ctx.hdc, oldPen);
  DeleteObject(hPen);
  // NULL_BRUSH is a stock object — never DeleteObject it
}

void Painter::pushClipRoundedRect(int x, int y, int w, int h,
                                  int cornerDiameter) {
  HRGN rgn =
      CreateRoundRectRgn(x, y, x + w, y + h, cornerDiameter, cornerDiameter);
  SelectClipRgn(ctx.hdc, rgn);
  DeleteObject(rgn);
}
void Painter::drawRoundedRectOutline(int x, int y, int w, int h,
                                     int cornerDiameter, NativeColor stroke,
                                     int strokeWidth) {
  HPEN hPen = CreatePen(PS_SOLID, strokeWidth, stroke);
  HPEN oldPen = (HPEN)SelectObject(ctx.hdc, hPen);
  HBRUSH oldBrush = (HBRUSH)SelectObject(ctx.hdc, GetStockObject(NULL_BRUSH));
  RoundRect(ctx.hdc, x, y, x + w, y + h, cornerDiameter, cornerDiameter);
  SelectObject(ctx.hdc, oldBrush);
  SelectObject(ctx.hdc, oldPen);
  DeleteObject(hPen);
}

void Painter::fillRectAlpha(int x, int y, int w, int h, NativeColor color,
                            BYTE alpha) {
  HDC tmpDC = CreateCompatibleDC(ctx.hdc);
  HBITMAP tmpBmp = CreateCompatibleBitmap(ctx.hdc, w, h);
  HBITMAP tmpOld = (HBITMAP)SelectObject(tmpDC, tmpBmp);

  HBRUSH br = CreateSolidBrush(color);
  RECT rc = {0, 0, w, h};
  FillRect(tmpDC, &rc, br);
  DeleteObject(br);

  BLENDFUNCTION bf = {AC_SRC_OVER, 0, alpha, 0};
  AlphaBlend(ctx.hdc, x, y, w, h, tmpDC, 0, 0, w, h, bf);

  SelectObject(tmpDC, tmpOld);
  DeleteObject(tmpBmp);
  DeleteDC(tmpDC);
}

void Painter::drawTextA(const std::string &text, int x, int y, int w, int h,
                        NativeFont font, NativeColor color, UINT format) {
  if (text.empty())
    return;
  int wlen = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
  std::wstring wtext(wlen, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, wtext.data(), wlen);
  drawText(wtext, x, y, w, h, font, color, format);
}

void Painter::fillRoundedRegion(int x, int y, int w, int h, int cornerRadius,
                                NativeColor color) {
  HBRUSH hb = CreateSolidBrush(color);
  HRGN rgn = CreateRoundRectRgn(x, y, x + w, y + h, cornerRadius, cornerRadius);
  FillRgn(ctx.hdc, rgn, hb);
  DeleteObject(rgn);
  DeleteObject(hb);
}

void Painter::drawHLine(int x, int y, int len, NativeColor color,
                        int strokeWidth) {
  drawLine(x, y, x + len, y, color, strokeWidth);
}

void Painter::drawVLine(int x, int y, int len, NativeColor color,
                        int strokeWidth) {
  drawLine(x, y, x, y + len, color, strokeWidth);
}

void Painter::fillRectWithLeftAccent(int x, int y, int w, int h, NativeColor bg,
                                     NativeColor accent, int stripWidth) {
  fillRect(x, y, w, h, bg);
  fillRect(x, y, stripWidth, h, accent);
}


void Painter::fillColumnBars(int x, int y, int w, int h,
                              const std::vector<int> &barHeights,
                              NativeColor color, BYTE alpha) {
    if (w <= 0 || h <= 0 || barHeights.empty()) return;

    BITMAPINFO bmi        = {};
    bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth       = w;
    bmi.bmiHeader.biHeight      = -h;   // top-down
    bmi.bmiHeader.biPlanes      = 1;
    bmi.bmiHeader.biBitCount    = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void   *bits = nullptr;
    HBITMAP hBmp = CreateDIBSection(ctx.hdc, &bmi, DIB_RGB_COLORS,
                                    &bits, nullptr, 0);
    if (!hBmp) return;

    HDC memDC = CreateCompatibleDC(ctx.hdc);
    SelectObject(memDC, hBmp);
    memset(bits, 0, w * h * 4);

    BYTE cr = GetRValue(color), cg = GetGValue(color), cb = GetBValue(color);

    int cols = min(w, (int)barHeights.size());
    for (int px = 0; px < cols; ++px) {
        int barH = max(0, min(h, barHeights[px]));
        for (int py = h - barH; py < h; ++py) {
            uint8_t *pixel = (uint8_t *)bits + (py * w + px) * 4;
            pixel[0] = cb;
            pixel[1] = cg;
            pixel[2] = cr;
            pixel[3] = 255;
        }
    }

    BLENDFUNCTION bf    = {};
    bf.BlendOp          = AC_SRC_OVER;
    bf.SourceConstantAlpha = alpha;
    bf.AlphaFormat      = 0;
    AlphaBlend(ctx.hdc, x, y, w, h, memDC, 0, 0, w, h, bf);

    DeleteDC(memDC);
    DeleteObject(hBmp);
}




#endif // _WIN32