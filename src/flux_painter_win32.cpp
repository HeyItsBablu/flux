// // flux_painter_win32.cpp
// #include "flux/flux_painter.hpp"

// #ifdef _WIN32

// // -----------------------------------------------------------------------
// // Internal helper — builds a rounded rect path (GDI+)
// // -----------------------------------------------------------------------

// static void makeRoundedPath(Gdiplus::GraphicsPath &path, int x, int y, int w,
//                             int h, int r)
// {
//   int d = r * 2;
//   path.AddArc(x, y, d, d, 180, 90);
//   path.AddArc(x + w - d, y, d, d, 270, 90);
//   path.AddArc(x + w - d, y + h - d, d, d, 0, 90);
//   path.AddArc(x, y + h - d, d, d, 90, 90);
//   path.CloseFigure();
// }

// // -----------------------------------------------------------------------
// // Painter::fillRoundedRect  (GDI+)
// // -----------------------------------------------------------------------

// void Painter::fillRoundedRect(int x, int y, int w, int h, int radius,
//                               Color color)
// {
//   Gdiplus::Graphics g(ctx.hdc);
//   g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

//   Gdiplus::SolidBrush brush(toGdipColor(color));

//   if (radius > 0)
//   {
//     Gdiplus::GraphicsPath path;
//     makeRoundedPath(path, x, y, w, h, radius);
//     g.FillPath(&brush, &path);
//   }
//   else
//   {
//     Gdiplus::RectF rect((Gdiplus::REAL)x, (Gdiplus::REAL)y, (Gdiplus::REAL)w,
//                         (Gdiplus::REAL)h);
//     g.FillRectangle(&brush, rect);
//   }
// }

// // -----------------------------------------------------------------------
// // Painter::drawBorder  (GDI+)
// // -----------------------------------------------------------------------

// void Painter::drawBorder(int x, int y, int w, int h, int radius, Color color,
//                          int borderWidth)
// {
//   Gdiplus::Graphics g(ctx.hdc);
//   g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

//   Gdiplus::Pen pen(toGdipColor(color), (Gdiplus::REAL)borderWidth);

//   if (radius > 0)
//   {
//     Gdiplus::GraphicsPath path;
//     makeRoundedPath(path, x, y, w, h, radius);
//     g.DrawPath(&pen, &path);
//   }
//   else
//   {
//     Gdiplus::RectF rect((Gdiplus::REAL)x, (Gdiplus::REAL)y, (Gdiplus::REAL)w,
//                         (Gdiplus::REAL)h);
//     g.DrawRectangle(&pen, rect);
//   }
// }

// // -----------------------------------------------------------------------
// // Painter::fillRect  (GDI)
// // -----------------------------------------------------------------------

// void Painter::fillRect(int x, int y, int w, int h, Color color)
// {
//   RECT rect = {x, y, x + w, y + h};
//   HBRUSH brush = CreateSolidBrush(toColorRef(color));
//   ::FillRect(ctx.hdc, &rect, brush);
//   DeleteObject(brush);
// }

// // -----------------------------------------------------------------------
// // Painter::fillRoundedRectGDI
// // -----------------------------------------------------------------------

// void Painter::fillRoundedRectGDI(int x, int y, int w, int h, int radius,
//                                  Color fill, Color stroke, int strokeWidth)
// {
//   HBRUSH hBrush = CreateSolidBrush(toColorRef(fill));
//   HPEN hPen = (strokeWidth > 0)
//                   ? CreatePen(PS_SOLID, strokeWidth, toColorRef(stroke))
//                   : (HPEN)GetStockObject(NULL_PEN);

//   HBRUSH oldBrush = (HBRUSH)SelectObject(ctx.hdc, hBrush);
//   HPEN oldPen = (HPEN)SelectObject(ctx.hdc, hPen);

//   RoundRect(ctx.hdc, x, y, x + w, y + h, radius, radius);

//   SelectObject(ctx.hdc, oldBrush);
//   SelectObject(ctx.hdc, oldPen);
//   DeleteObject(hBrush);
//   if (strokeWidth > 0)
//     DeleteObject(hPen);
// }

// // -----------------------------------------------------------------------
// // Painter::drawEllipse
// // -----------------------------------------------------------------------

// void Painter::drawEllipse(int x, int y, int w, int h, Color fill, Color stroke,
//                           int strokeWidth)
// {
//   HBRUSH hBrush = CreateSolidBrush(toColorRef(fill));
//   HPEN hPen = (strokeWidth > 0)
//                   ? CreatePen(PS_SOLID, strokeWidth, toColorRef(stroke))
//                   : (HPEN)GetStockObject(NULL_PEN);

//   HBRUSH oldBrush = (HBRUSH)SelectObject(ctx.hdc, hBrush);
//   HPEN oldPen = (HPEN)SelectObject(ctx.hdc, hPen);

//   Ellipse(ctx.hdc, x, y, x + w, y + h);

//   SelectObject(ctx.hdc, oldBrush);
//   SelectObject(ctx.hdc, oldPen);
//   DeleteObject(hBrush);
//   if (strokeWidth > 0)
//     DeleteObject(hPen);
// }

// // -----------------------------------------------------------------------
// // Painter::drawLine
// // -----------------------------------------------------------------------

// void Painter::drawLine(int x1, int y1, int x2, int y2, Color color, int width)
// {
//   HPEN hPen = CreatePen(PS_SOLID, width, toColorRef(color));
//   HPEN oldPen = (HPEN)SelectObject(ctx.hdc, hPen);

//   MoveToEx(ctx.hdc, x1, y1, nullptr);
//   LineTo(ctx.hdc, x2, y2);

//   SelectObject(ctx.hdc, oldPen);
//   DeleteObject(hPen);
// }

// // -----------------------------------------------------------------------
// // Painter::drawText  (low-level — no style object)
// // -----------------------------------------------------------------------

// void Painter::drawText(const std::wstring &text, int x, int y, int w, int h,
//                        NativeFont font, Color color, UINT format)
// {
//   if (text.empty())
//     return;

//   SetTextColor(ctx.hdc, toColorRef(color));
//   SetBkMode(ctx.hdc, TRANSPARENT);

//   HFONT hOldFont = (HFONT)SelectObject(ctx.hdc, font);

//   RECT rect = {x, y, x + w, y + h};
//   DrawTextW(ctx.hdc, text.c_str(), -1, &rect, format);

//   SelectObject(ctx.hdc, hOldFont);
// }

// // -----------------------------------------------------------------------
// // Painter::measureText  (low-level)
// // -----------------------------------------------------------------------

// void Painter::measureText(const std::wstring &text, NativeFont font,
//                           int &outWidth, int &outHeight)
// {
//   if (text.empty())
//   {
//     outWidth = outHeight = 0;
//     return;
//   }

//   HFONT oldFont = (HFONT)SelectObject(ctx.hdc, font);

//   SIZE sz = {};
//   GetTextExtentPoint32W(ctx.hdc, text.c_str(), static_cast<int>(text.size()),
//                         &sz);
//   outWidth = sz.cx;
//   outHeight = sz.cy;

//   SelectObject(ctx.hdc, oldFont);
// }

// // -----------------------------------------------------------------------
// // Painter::pushClipRect / popClipRect
// // -----------------------------------------------------------------------

// void Painter::pushClipRect(int x, int y, int w, int h)
// {
//     // Save whatever clip region is currently active
//     HRGN saved = CreateRectRgn(0, 0, 0, 0);
//     if (GetClipRgn(ctx.hdc, saved) != 1) {
//         DeleteObject(saved);
//         saved = nullptr; // nullptr = no clip was active
//     }
//     ctx.clipStack.push_back(saved);

//     // Intersect — never replace
//     HRGN newRgn = CreateRectRgn(x, y, x + w, y + h);
//     ExtSelectClipRgn(ctx.hdc, newRgn, RGN_AND);
//     DeleteObject(newRgn);
// }

// void Painter::popClipRect()
// {
//     if (ctx.clipStack.empty()) return;

//     HRGN saved = ctx.clipStack.back();
//     ctx.clipStack.pop_back();

//     SelectClipRgn(ctx.hdc, saved); // nullptr correctly means "remove all clipping"
//     if (saved) DeleteObject(saved);
// }

// void Painter::pushClipRoundedRect(int x, int y, int w, int h, int cornerDiameter)
// {
//     HRGN saved = CreateRectRgn(0, 0, 0, 0);
//     if (GetClipRgn(ctx.hdc, saved) != 1) {
//         DeleteObject(saved);
//         saved = nullptr;
//     }
//     ctx.clipStack.push_back(saved);

//     HRGN newRgn = CreateRoundRectRgn(x, y, x + w, y + h, cornerDiameter, cornerDiameter);
//     ExtSelectClipRgn(ctx.hdc, newRgn, RGN_AND);
//     DeleteObject(newRgn);
// }

// // -----------------------------------------------------------------------
// // Painter::fillGradientRect
// // -----------------------------------------------------------------------

// void Painter::fillGradientRect(int x, int y, int w, int h,
//                                const std::vector<Color> &colors)
// {
//   if (colors.empty() || w <= 0 || h <= 0)
//     return;

//   if (colors.size() == 1)
//   {
//     fillRect(x, y, w, h, colors[0]);
//     return;
//   }

//   const int stops = static_cast<int>(colors.size());

//   for (int i = 0; i < w; ++i)
//   {
//     double t = static_cast<double>(i) / (w - 1);
//     double scaled = t * (stops - 1);
//     int idx = static_cast<int>(scaled);
//     double frac = scaled - idx;

//     if (idx >= stops - 1)
//     {
//       idx = stops - 2;
//       frac = 1.0;
//     }

//     Color c = colors[idx].interpolate(colors[idx + 1], frac);

//     HBRUSH band = CreateSolidBrush(toColorRef(c));
//     RECT col = {x + i, y, x + i + 1, y + h};
//     ::FillRect(ctx.hdc, &col, band);
//     DeleteObject(band);
//   }
// }

// // -----------------------------------------------------------------------
// // Painter::drawRectOutline
// // -----------------------------------------------------------------------

// void Painter::drawRectOutline(int x, int y, int w, int h, Color color,
//                               int strokeWidth)
// {
//   HPEN hPen = CreatePen(PS_SOLID, strokeWidth, toColorRef(color));
//   HPEN oldPen = (HPEN)SelectObject(ctx.hdc, hPen);
//   HBRUSH oldBrush = (HBRUSH)SelectObject(ctx.hdc, GetStockObject(NULL_BRUSH));

//   Rectangle(ctx.hdc, x, y, x + w, y + h);

//   SelectObject(ctx.hdc, oldBrush);
//   SelectObject(ctx.hdc, oldPen);
//   DeleteObject(hPen);
// }


// // -----------------------------------------------------------------------
// // Painter::drawRoundedRectOutline
// // -----------------------------------------------------------------------

// void Painter::drawRoundedRectOutline(int x, int y, int w, int h,
//                                      int cornerDiameter, Color stroke,
//                                      int strokeWidth)
// {
//   HPEN hPen = CreatePen(PS_SOLID, strokeWidth, toColorRef(stroke));
//   HPEN oldPen = (HPEN)SelectObject(ctx.hdc, hPen);
//   HBRUSH oldBrush = (HBRUSH)SelectObject(ctx.hdc, GetStockObject(NULL_BRUSH));

//   RoundRect(ctx.hdc, x, y, x + w, y + h, cornerDiameter, cornerDiameter);

//   SelectObject(ctx.hdc, oldBrush);
//   SelectObject(ctx.hdc, oldPen);
//   DeleteObject(hPen);
// }

// // -----------------------------------------------------------------------
// // Painter::fillRectAlpha
// // -----------------------------------------------------------------------

// void Painter::fillRectAlpha(int x, int y, int w, int h, Color color)
// {
//   HDC tmpDC = CreateCompatibleDC(ctx.hdc);
//   HBITMAP tmpBmp = CreateCompatibleBitmap(ctx.hdc, w, h);
//   HBITMAP tmpOld = (HBITMAP)SelectObject(tmpDC, tmpBmp);

//   HBRUSH br = CreateSolidBrush(toColorRef(color));
//   RECT rc = {0, 0, w, h};
//   FillRect(tmpDC, &rc, br);
//   DeleteObject(br);

//   BLENDFUNCTION bf = {AC_SRC_OVER, 0, color.a, 0};
//   AlphaBlend(ctx.hdc, x, y, w, h, tmpDC, 0, 0, w, h, bf);

//   SelectObject(tmpDC, tmpOld);
//   DeleteObject(tmpBmp);
//   DeleteDC(tmpDC);
// }

// // -----------------------------------------------------------------------
// // Painter::drawTextA  (UTF-8 → wide → drawText)
// // -----------------------------------------------------------------------

// void Painter::drawTextA(const std::string &text, int x, int y, int w, int h,
//                         NativeFont font, Color color, UINT format)
// {
//   if (text.empty())
//     return;
//   int wlen = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
//   std::wstring wtext(wlen, L'\0');
//   MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, wtext.data(), wlen);
//   drawText(wtext, x, y, w, h, font, color, format);
// }

// // -----------------------------------------------------------------------
// // Painter::fillRoundedRegion
// // -----------------------------------------------------------------------

// void Painter::fillRoundedRegion(int x, int y, int w, int h, int cornerRadius,
//                                 Color color)
// {
//   HBRUSH hb = CreateSolidBrush(toColorRef(color));
//   HRGN rgn = CreateRoundRectRgn(x, y, x + w, y + h, cornerRadius, cornerRadius);
//   FillRgn(ctx.hdc, rgn, hb);
//   DeleteObject(rgn);
//   DeleteObject(hb);
// }

// // -----------------------------------------------------------------------
// // Painter::drawHLine / drawVLine
// // -----------------------------------------------------------------------

// void Painter::drawHLine(int x, int y, int len, Color color, int strokeWidth)
// {
//   drawLine(x, y, x + len, y, color, strokeWidth);
// }

// void Painter::drawVLine(int x, int y, int len, Color color, int strokeWidth)
// {
//   drawLine(x, y, x, y + len, color, strokeWidth);
// }

// // -----------------------------------------------------------------------
// // Painter::fillRectWithLeftAccent
// // -----------------------------------------------------------------------

// void Painter::fillRectWithLeftAccent(int x, int y, int w, int h, Color bg,
//                                      Color accent, int stripWidth)
// {
//   fillRect(x, y, w, h, bg);
//   fillRect(x, y, stripWidth, h, accent);
// }

// // -----------------------------------------------------------------------
// // Painter::fillColumnBars
// // -----------------------------------------------------------------------

// void Painter::fillColumnBars(int x, int y, int w, int h,
//                              const std::vector<int> &barHeights, Color color)
// {
//   if (w <= 0 || h <= 0 || barHeights.empty())
//     return;

//   BITMAPINFO bmi = {};
//   bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
//   bmi.bmiHeader.biWidth = w;
//   bmi.bmiHeader.biHeight = -h;
//   bmi.bmiHeader.biPlanes = 1;
//   bmi.bmiHeader.biBitCount = 32;
//   bmi.bmiHeader.biCompression = BI_RGB;

//   void *bits = nullptr;
//   HBITMAP hBmp =
//       CreateDIBSection(ctx.hdc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
//   if (!hBmp)
//     return;

//   HDC memDC = CreateCompatibleDC(ctx.hdc);
//   SelectObject(memDC, hBmp);
//   memset(bits, 0, w * h * 4);

//   int cols = std::min(w, (int)barHeights.size());
//   for (int px = 0; px < cols; ++px)
//   {
//     int barH = std::max(0, std::min(h, barHeights[px]));
//     for (int py = h - barH; py < h; ++py)
//     {
//       uint8_t *pixel = (uint8_t *)bits + (py * w + px) * 4;
//       pixel[0] = color.b;
//       pixel[1] = color.g;
//       pixel[2] = color.r;
//       pixel[3] = 255;
//     }
//   }

//   BLENDFUNCTION bf = {};
//   bf.BlendOp = AC_SRC_OVER;
//   bf.SourceConstantAlpha = color.a;
//   bf.AlphaFormat = 0;
//   AlphaBlend(ctx.hdc, x, y, w, h, memDC, 0, 0, w, h, bf);

//   DeleteDC(memDC);
//   DeleteObject(hBmp);
// }

// // -----------------------------------------------------------------------
// // Painter::drawPolyline
// // -----------------------------------------------------------------------

// void Painter::drawPolyline(const std::vector<std::pair<int, int>> &points,
//                            Color color, int strokeWidth)
// {
//   if (points.size() < 2)
//     return;

//   std::vector<POINT> pts(points.size());
//   for (size_t i = 0; i < points.size(); ++i)
//     pts[i] = {points[i].first, points[i].second};

//   HPEN hPen = CreatePen(PS_SOLID, strokeWidth, toColorRef(color));
//   HPEN old = (HPEN)SelectObject(ctx.hdc, hPen);
//   Polyline(ctx.hdc, pts.data(), (int)pts.size());
//   SelectObject(ctx.hdc, old);
//   DeleteObject(hPen);
// }

// // -----------------------------------------------------------------------
// // Painter::fillPolygonAlpha
// // -----------------------------------------------------------------------

// void Painter::fillPolygonAlpha(const std::vector<std::pair<int, int>> &points,
//                                Color color)
// {
//   if (points.size() < 3)
//     return;

//   int x0 = points[0].first, y0 = points[0].second;
//   int x1 = x0, y1 = y0;
//   for (auto &p : points)
//   {
//     x0 = std::min(x0, p.first);
//     y0 = std::min(y0, p.second);
//     x1 = std::max(x1, p.first);
//     y1 = std::max(y1, p.second);
//   }
//   int w = x1 - x0, h = y1 - y0;
//   if (w <= 0 || h <= 0)
//     return;

//   BITMAPINFO bmi = {};
//   bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
//   bmi.bmiHeader.biWidth = w;
//   bmi.bmiHeader.biHeight = -h;
//   bmi.bmiHeader.biPlanes = 1;
//   bmi.bmiHeader.biBitCount = 32;
//   bmi.bmiHeader.biCompression = BI_RGB;

//   void *bits = nullptr;
//   HBITMAP hBmp =
//       CreateDIBSection(ctx.hdc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
//   if (!hBmp)
//     return;

//   HDC mdc = CreateCompatibleDC(ctx.hdc);
//   SelectObject(mdc, hBmp);
//   memset(bits, 0, w * h * 4);

//   std::vector<POINT> poly(points.size());
//   for (size_t i = 0; i < points.size(); ++i)
//     poly[i] = {points[i].first - x0, points[i].second - y0};

//   HPEN nullPen = CreatePen(PS_NULL, 0, 0);
//   HBRUSH fillBr = CreateSolidBrush(toColorRef(color));
//   HPEN op = (HPEN)SelectObject(mdc, nullPen);
//   HBRUSH ob = (HBRUSH)SelectObject(mdc, fillBr);
//   Polygon(mdc, poly.data(), (int)poly.size());
//   SelectObject(mdc, op);
//   SelectObject(mdc, ob);
//   DeleteObject(nullPen);
//   DeleteObject(fillBr);

//   BLENDFUNCTION bf = {AC_SRC_OVER, 0, color.a, 0};
//   AlphaBlend(ctx.hdc, x0, y0, w, h, mdc, 0, 0, w, h, bf);
//   DeleteDC(mdc);
//   DeleteObject(hBmp);
// }

// // =============================================================================
// // RICH TEXT IMPLEMENTATION
// // =============================================================================

// // -----------------------------------------------------------------------
// // Internal: translate TextAlign → Win32 DT_* flags (horizontal part only)
// // -----------------------------------------------------------------------

// // static UINT textAlignFlags(TextAlign align, TextDirection dir) {
// //   // RTL: flip Left/Right/Start/End
// //   bool rtl = (dir == TextDirection::RTL);

// //   switch (align) {
// //   case TextAlign::Left:
// //   case TextAlign::Start:
// //     return rtl ? DT_RIGHT : DT_LEFT;

// //   case TextAlign::Right:
// //   case TextAlign::End:
// //     return rtl ? DT_LEFT : DT_RIGHT;

// //   case TextAlign::Center:
// //     return DT_CENTER;

// //   case TextAlign::Justify:

// //     return rtl ? DT_RIGHT : DT_LEFT;
// //   }
// //   return DT_LEFT;
// // }

// // -----------------------------------------------------------------------
// // Internal: get the native HFONT from a TextStyle
// // -----------------------------------------------------------------------

// static NativeFont fontFromStyle(const TextStyle &style, FontCache &fontCache)
// {
//   bool underline = hasDecoration(style.decoration, TextDecoration::Underline);
//   bool strikeOut = hasDecoration(style.decoration, TextDecoration::LineThrough);

//   return fontCache.getFont(style.fontFamily, style.scaledFontSize(),
//                            style.fontWeight, underline, strikeOut);
// }

// static int measureLine(HDC hdc, const wchar_t *str, int len,
//                        float letterSpacing, int &outSingleLineH)
// {
//   if (len <= 0)
//   {
//     SIZE sz = {};
//     GetTextExtentPoint32W(hdc, L" ", 1, &sz);
//     outSingleLineH = sz.cy;
//     return 0;
//   }

//   SIZE sz = {};
//   GetTextExtentPoint32W(hdc, str, len, &sz);
//   outSingleLineH = sz.cy;

//   if (letterSpacing == 0.f)
//     return sz.cx;

//   // Letter spacing: add extra between every pair of characters
//   int extra = static_cast<int>(letterSpacing);
//   return sz.cx + extra * std::max(0, len - 1);
// }

// struct LineSpan
// {
//   int start;
//   int length;
// };

// static std::vector<LineSpan> wrapText(HDC hdc, const std::wstring &text,
//                                       NativeFont font, int maxWidth,
//                                       bool softWrap, float letterSpacing)
// {
//   std::vector<LineSpan> lines;
//   const int n = static_cast<int>(text.size());
//   if (n == 0)
//     return lines;

//   HFONT oldFont = (HFONT)SelectObject(hdc, font);

//   int pos = 0;
//   while (pos < n)
//   {
//     // Find the next newline
//     int nlPos = pos;
//     while (nlPos < n && text[nlPos] != L'\n')
//       ++nlPos;

//     if (!softWrap || maxWidth <= 0)
//     {
//       // No wrapping — one line per newline
//       lines.push_back({pos, nlPos - pos});
//     }
//     else
//     {
//       // Word-wrap within [pos, nlPos)
//       int lineStart = pos;
//       while (lineStart < nlPos)
//       {
//         // Binary-search for the longest prefix that fits
//         int lo = 0, hi = nlPos - lineStart, fit = 0;
//         while (lo <= hi)
//         {
//           int mid = (lo + hi) / 2;
//           int dummy;
//           int w = measureLine(hdc, text.c_str() + lineStart, mid, letterSpacing,
//                               dummy);
//           if (w <= maxWidth)
//           {
//             fit = mid;
//             lo = mid + 1;
//           }
//           else
//           {
//             hi = mid - 1;
//           }
//         }

//         if (fit == 0)
//         {
//           // Even a single character doesn't fit — force one char
//           fit = 1;
//         }

//         // Try to break at a word boundary
//         int breakAt = fit;
//         if (lineStart + fit < nlPos)
//         {
//           // Walk back to a space
//           int wb = fit;
//           while (wb > 1 && text[lineStart + wb - 1] != L' ')
//             --wb;
//           if (wb > 1)
//             breakAt = wb;
//         }

//         lines.push_back({lineStart, breakAt});
//         lineStart += breakAt;

//         // Skip leading spaces on the next line
//         while (lineStart < nlPos && text[lineStart] == L' ')
//           ++lineStart;
//       }
//     }

//     pos = nlPos + 1; // skip '\n'
//     if (nlPos == n)
//       break; // no trailing newline
//   }

//   SelectObject(hdc, oldFont);
//   return lines;
// }

// // -----------------------------------------------------------------------
// // Painter::drawFadeOverlay
// // -----------------------------------------------------------------------

// void Painter::drawFadeOverlay(int x, int y, int w, int h, int fadeWidth,
//                               Color bg)
// {
//   if (fadeWidth <= 0 || w <= 0 || h <= 0)
//     return;
//   int startX = x + w - fadeWidth;
//   if (startX < x)
//     startX = x;

//   // Build a two-stop gradient: transparent → bg
//   std::vector<Color> stops = {bg.withAlpha(0), bg.withAlpha(255)};
//   fillGradientRect(startX, y, fadeWidth, h, stops);
// }

// // -----------------------------------------------------------------------
// // Painter::drawWavyLine
// // -----------------------------------------------------------------------

// void Painter::drawWavyLine(int x, int y, int len, Color color, int amplitude)
// {
//   if (len <= 0)
//     return;
//   const int step = amplitude * 2;
//   std::vector<std::pair<int, int>> pts;
//   pts.reserve(len / step + 2);

//   int px = x;
//   bool up = true;
//   while (px < x + len)
//   {
//     pts.push_back({px, up ? y - amplitude : y + amplitude});
//     px += step;
//     up = !up;
//   }
//   pts.push_back({x + len, y}); // end at baseline

//   if (pts.size() >= 2)
//     drawPolyline(pts, color, 1);
// }

// // -----------------------------------------------------------------------
// // Painter::drawTextDecorationLine
// // Draws a single decoration (Underline, Overline, or LineThrough)
// // at the correct position relative to the text.
// // lineX, lineY  — top-left of the text line bounding box
// // lineW         — pixel width of the text on that line
// // -----------------------------------------------------------------------

// void Painter::drawTextDecorationLine(int lineX, int lineY, int lineW,
//                                      const TextStyle &style,
//                                      TextDecoration which)
// {
//   if (lineW <= 0)
//     return;

//   // Query TEXTMETRIC for baseline / ascent / descent geometry
//   TEXTMETRICW tm = {};
//   GetTextMetricsW(ctx.hdc, &tm);

//   int thickness = style.decorationThickness;
//   Color dc = style.decorationColor;

//   int decorY = lineY;
//   if (which == TextDecoration::Underline)
//   {
//     // Typically 1-2 px below the baseline
//     decorY = lineY + tm.tmAscent + 1;
//   }
//   else if (which == TextDecoration::Overline)
//   {
//     decorY = lineY;
//   }
//   else if (which == TextDecoration::LineThrough)
//   {
//     // Midway through the cap height
//     decorY = lineY + tm.tmAscent - tm.tmAscent / 3;
//   }

//   switch (style.decorationStyle)
//   {
//   case TextDecorationStyle::Solid:
//     drawHLine(lineX, decorY, lineW, dc, thickness);
//     break;

//   case TextDecorationStyle::Double:
//     drawHLine(lineX, decorY, lineW, dc, thickness);
//     drawHLine(lineX, decorY + 2, lineW, dc, thickness);
//     break;

//   case TextDecorationStyle::Dotted:
//   {
//     // Simulate dots by drawing short segments with gaps
//     for (int px = lineX; px < lineX + lineW; px += 4)
//       drawHLine(px, decorY, std::min(2, lineX + lineW - px), dc, thickness);
//     break;
//   }

//   case TextDecorationStyle::Dashed:
//   {
//     for (int px = lineX; px < lineX + lineW; px += 8)
//       drawHLine(px, decorY, std::min(5, lineX + lineW - px), dc, thickness);
//     break;
//   }

//   case TextDecorationStyle::Wavy:
//     drawWavyLine(lineX, decorY, lineW, dc, 2);
//     break;
//   }
// }

// // -----------------------------------------------------------------------
// // Painter::measureRichText
// // -----------------------------------------------------------------------

// void Painter::measureRichText(const std::wstring &text, const TextStyle &style,
//                               FontCache &fontCache, int maxWidth, bool softWrap,
//                               int maxLines, int &outWidth, int &outHeight)
// {
//   outWidth = outHeight = 0;
//   if (text.empty())
//     return;

//   NativeFont font = fontFromStyle(style, fontCache);
//   HFONT oldFont = (HFONT)SelectObject(ctx.hdc, font);

//   // Apply letter spacing
//   int prevExtra =
//       SetTextCharacterExtra(ctx.hdc, static_cast<int>(style.letterSpacing));

//   std::vector<LineSpan> lines =
//       wrapText(ctx.hdc, text, font, maxWidth, softWrap, style.letterSpacing);

//   int lineH = 0;
//   {
//     SIZE sz = {};
//     GetTextExtentPoint32W(ctx.hdc, L"Wg", 2, &sz);
//     lineH = sz.cy;
//   }
//   int lineHeightPx = static_cast<int>(lineH * style.height);

//   int totalLines = (maxLines > 0) ? std::min((int)lines.size(), maxLines)
//                                   : (int)lines.size();

//   for (int i = 0; i < totalLines; ++i)
//   {
//     const auto &span = lines[i];
//     int dummy;
//     int w = measureLine(ctx.hdc, text.c_str() + span.start, span.length,
//                         style.letterSpacing, dummy);
//     outWidth = std::max(outWidth, w);
//   }

//   outHeight = totalLines * lineHeightPx;

//   SetTextCharacterExtra(ctx.hdc, prevExtra);
//   SelectObject(ctx.hdc, oldFont);
// }

// // -----------------------------------------------------------------------
// // Painter::drawRichText  (the main rich-text entry point)
// // -----------------------------------------------------------------------

// void Painter::drawRichText(const std::wstring &text,
//                            const RichTextParams &params, FontCache &fontCache)
// {
//   if (text.empty() || params.w <= 0 || params.h <= 0)
//     return;

//   const TextStyle &style = params.style;
//   NativeFont font = fontFromStyle(style, fontCache);

//   HFONT oldFont = (HFONT)SelectObject(ctx.hdc, font);

//   // ── Letter spacing ────────────────────────────────────────────────────
//   int prevExtra =
//       SetTextCharacterExtra(ctx.hdc, static_cast<int>(style.letterSpacing));

//   // ── Wrap text into lines ──────────────────────────────────────────────
//   int wrapWidth = params.softWrap ? params.w : 0;
//   std::vector<LineSpan> lines = wrapText(ctx.hdc, text, font, wrapWidth,
//                                          params.softWrap, style.letterSpacing);

//   // ── Compute natural line height ───────────────────────────────────────
//   TEXTMETRICW tm = {};
//   GetTextMetricsW(ctx.hdc, &tm);
//   int naturalLineH = tm.tmHeight;
//   int lineHeightPx = static_cast<int>(naturalLineH * style.height);

//   // ── Cap at maxLines ───────────────────────────────────────────────────
//   int totalLines = (params.maxLines > 0)
//                        ? std::min((int)lines.size(), params.maxLines)
//                        : (int)lines.size();

//   // ── Compute total text block height for vertical alignment ────────────
//   int blockH = totalLines * lineHeightPx;

//   int startY = params.y;
//   switch (params.textAlignVertical)
//   {
//   case TextAlignVertical::Top:
//     startY = params.y;
//     break;
//   case TextAlignVertical::Center:
//     startY = params.y + (params.h - blockH) / 2;
//     break;
//   case TextAlignVertical::Bottom:
//     startY = params.y + params.h - blockH;
//     break;
//   }

//   // ── Clipping (for Clip / Ellipsis / Fade modes) ───────────────────────
//   bool needClip = (params.overflow != TextOverflow::Visible);
//   if (needClip)
//     pushClipRect(params.x, params.y, params.w, params.h);

//   // ── Setup GDI drawing state ───────────────────────────────────────────
//   SetBkMode(ctx.hdc, TRANSPARENT);
//   Color textColor = style.color;
//   SetTextColor(ctx.hdc, toColorRef(textColor));

//   // ── Draw each line ────────────────────────────────────────────────────
//   for (int i = 0; i < totalLines; ++i)
//   {
//     const auto &span = lines[i];
//     const wchar_t *lineStr = text.c_str() + span.start;
//     int lineLen = span.length;

//     int lineY = startY + i * lineHeightPx;

//     // Skip lines entirely outside the clipping rect
//     if (lineY + lineHeightPx < params.y)
//       continue;
//     if (lineY > params.y + params.h)
//       break;

//     // ── Measure this line for alignment ──────────────────────────────
//     int dummy;
//     int lineW =
//         measureLine(ctx.hdc, lineStr, lineLen, style.letterSpacing, dummy);

//     // ── Compute X origin ─────────────────────────────────────────────
//     int lineX = params.x;
//     bool isRTL = (params.direction == TextDirection::RTL);

//     switch (params.textAlign)
//     {
//     case TextAlign::Left:
//     case TextAlign::Start:
//       lineX = isRTL ? (params.x + params.w - lineW) : params.x;
//       break;

//     case TextAlign::Right:
//     case TextAlign::End:
//       lineX = isRTL ? params.x : (params.x + params.w - lineW);
//       break;

//     case TextAlign::Center:
//       lineX = params.x + (params.w - lineW) / 2;
//       break;

//     case TextAlign::Justify:
//     {
//       // For the last line (or a line shorter than the box), left-align.
//       // For other lines, expand spaces to fill the full width.
//       bool isLast = (i == totalLines - 1);
//       if (!isLast && lineLen > 1 && lineW < params.w)
//       {
//         // Count spaces in this line
//         int spaceCount = 0;
//         for (int k = 0; k < lineLen; ++k)
//           if (lineStr[k] == L' ')
//             ++spaceCount;

//         if (spaceCount > 0)
//         {
//           int extraPerSpace = (params.w - lineW) / spaceCount;
//           // Draw word by word with expanded spacing
//           int curX = params.x;
//           int wordStart = 0;
//           for (int k = 0; k <= lineLen; ++k)
//           {
//             bool isEnd = (k == lineLen || lineStr[k] == L' ');
//             if (isEnd && k > wordStart)
//             {
//               // Draw word
//               RECT wr = {curX, lineY, curX + params.w, lineY + lineHeightPx};
//               // Draw shadows first
//               for (const auto &sh : style.shadows)
//               {
//                 SetTextColor(ctx.hdc, toColorRef(sh.color));
//                 RECT sr = wr;
//                 OffsetRect(&sr, sh.offsetX, sh.offsetY);
//                 DrawTextW(ctx.hdc, lineStr + wordStart, k - wordStart, &sr,
//                           DT_LEFT | DT_SINGLELINE | DT_NOCLIP);
//               }
//               SetTextColor(ctx.hdc, toColorRef(textColor));
//               DrawTextW(ctx.hdc, lineStr + wordStart, k - wordStart, &wr,
//                         DT_LEFT | DT_SINGLELINE | DT_NOCLIP);

//               SIZE wSz = {};
//               GetTextExtentPoint32W(ctx.hdc, lineStr + wordStart, k - wordStart,
//                                     &wSz);
//               curX += wSz.cx;
//               if (k < lineLen && lineStr[k] == L' ')
//                 curX += extraPerSpace;

//               wordStart = k + 1;
//             }
//           }

//           // Draw decorations for this line manually
//           if (style.decoration != TextDecoration::None)
//           {
//             if (style.hasOverline())
//               drawTextDecorationLine(params.x, lineY, lineW, style,
//                                      TextDecoration::Overline);
//             if (style.hasLineThrough())
//               drawTextDecorationLine(params.x, lineY, lineW, style,
//                                      TextDecoration::LineThrough);
//           }
//           continue; // Skip the normal draw below
//         }
//       }
//       lineX = isRTL ? (params.x + params.w - lineW) : params.x;
//       break;
//     }
//     }

//     // ── Per-run background color ──────────────────────────────────────
//     if (style.backgroundColor.has_value())
//     {
//       RECT bgRect = {lineX, lineY, lineX + lineW, lineY + lineHeightPx};
//       HBRUSH bgBr = CreateSolidBrush(toColorRef(*style.backgroundColor));
//       FillRect(ctx.hdc, &bgRect, bgBr);
//       DeleteObject(bgBr);
//     }

//     // ── Ellipsis for last visible line ────────────────────────────────────
//     bool isLastVisible = (i == totalLines - 1);
//     bool hasMoreLines = ((int)lines.size() > totalLines) ||
//                         (totalLines == 1 && lineW > params.w);

//     UINT drawFlags = DT_LEFT | DT_SINGLELINE;

//     if (isLastVisible && hasMoreLines &&
//         params.overflow == TextOverflow::Ellipsis)
//     {
//       // DT_MODIFYSTRING requires a writable, null-terminated buffer
//       std::wstring ellipsisLine(lineStr, lineLen);
//       ellipsisLine.resize(lineLen + 4); // room for "..." + null
//       RECT textRect2 = {lineX, lineY, params.x + params.w,
//                         lineY + lineHeightPx};
//       DrawTextW(ctx.hdc, ellipsisLine.data(), -1, &textRect2,
//                 DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_MODIFYSTRING);
//       continue; // skip the normal draw below
//     }

//     // ── Draw shadows ──────────────────────────────────────────────────
//     for (const auto &sh : style.shadows)
//     {
//       // Approximate blur by drawing multiple offset copies at lower alpha
//       int blurPasses = (sh.blurRadius > 0) ? std::min(sh.blurRadius, 3) : 1;
//       for (int bp = 0; bp < blurPasses; ++bp)
//       {
//         Color shadowCol =
//             sh.color.withAlpha(static_cast<uint8_t>(sh.color.a / blurPasses));
//         SetTextColor(ctx.hdc, toColorRef(shadowCol));

//         RECT shadowRect = {lineX + sh.offsetX + bp, lineY + sh.offsetY + bp,
//                            lineX + sh.offsetX + bp + params.w,
//                            lineY + sh.offsetY + bp + lineHeightPx};
//         DrawTextW(ctx.hdc, lineStr, lineLen, &shadowRect, drawFlags);
//       }
//     }

//     // ── Draw text ─────────────────────────────────────────────────────
//     SetTextColor(ctx.hdc, toColorRef(textColor));
//     RECT textRect = {lineX, lineY, lineX + params.w, lineY + lineHeightPx};
//     DrawTextW(ctx.hdc, lineStr, lineLen, &textRect, drawFlags);

//     // ── Draw overline (must be done manually — not in LOGFONT) ────────
//     if (style.hasOverline())
//       drawTextDecorationLine(lineX, lineY, lineW, style,
//                              TextDecoration::Overline);

//     // ── LineThrough  ─────────────

//     if (style.hasLineThrough() &&
//         (style.decorationStyle != TextDecorationStyle::Solid ||
//          style.decorationColor != textColor))
//     {
//       // Reset baked strikeout to plain font for manual draw
//       NativeFont plainFont =
//           fontCache.getFont(style.fontFamily, style.scaledFontSize(),
//                             style.fontWeight, style.hasUnderline(), false);
//       SelectObject(ctx.hdc, plainFont);
//       SetTextColor(ctx.hdc, toColorRef(textColor));
//       DrawTextW(ctx.hdc, lineStr, lineLen, &textRect, drawFlags);
//       SelectObject(ctx.hdc, font); // restore decorated font
//       drawTextDecorationLine(lineX, lineY, lineW, style,
//                              TextDecoration::LineThrough);
//     }

//     // ── Fade overlay (last line when overflow == Fade) ────────────────
//     if (isLastVisible && hasMoreLines &&
//         params.overflow == TextOverflow::Fade)
//     {
//       // Determine what color to fade to (widget background or white)
//       Color fadeBg = Color::fromRGB(255, 255, 255);
//       int fadeW = std::min(60, params.w / 3);
//       drawFadeOverlay(params.x, lineY, params.w, lineHeightPx, fadeW, fadeBg);
//     }
//   }

//   // ── Restore ───────────────────────────────────────────────────────────
//   if (needClip)
//     popClipRect();

//   SetTextCharacterExtra(ctx.hdc, prevExtra);
//   SelectObject(ctx.hdc, oldFont);
// }

// // -----------------------------------------------------------------------
// // Painter::drawRichTextA  (UTF-8 convenience overload)
// // -----------------------------------------------------------------------

// void Painter::drawRichTextA(const std::string &text,
//                             const RichTextParams &params,
//                             FontCache &fontCache)
// {
//   if (text.empty())
//     return;
//   int wlen = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
//   std::wstring wtext(wlen, L'\0');
//   MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, wtext.data(), wlen);
//   drawRichText(wtext, params, fontCache);
// }

// void Painter::drawArc(float cx, float cy, float radius,
//                       int strokeWidth,
//                       float startAngle, float sweepAngle,
//                       Color color, bool roundedCaps)
// {
//   Gdiplus::Graphics g(ctx.hdc);
//   g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

//   Gdiplus::Pen pen(
//       Gdiplus::Color(color.a, color.r, color.g, color.b),
//       static_cast<Gdiplus::REAL>(strokeWidth));
//   pen.SetLineCap(
//       roundedCaps ? Gdiplus::LineCapRound : Gdiplus::LineCapFlat,
//       roundedCaps ? Gdiplus::LineCapRound : Gdiplus::LineCapFlat,
//       Gdiplus::DashCapRound);

//   constexpr float kRad2Deg = 180.0f / 3.14159265f;
//   float d = radius * 2.0f;
//   g.DrawArc(&pen,
//             cx - radius, cy - radius, d, d,
//             startAngle * kRad2Deg,
//             sweepAngle * kRad2Deg);
// }

// // -----------------------------------------------------------------------
// // Painter::drawImage  (Win32 / GDI+)
// // -----------------------------------------------------------------------

// static Gdiplus::InterpolationMode gdiInterpolationFromQuality(FilterQuality q)
// {
//   switch (q)
//   {
//   case FilterQuality::None:
//     return Gdiplus::InterpolationModeNearestNeighbor;
//   case FilterQuality::Low:
//     return Gdiplus::InterpolationModeBilinear;
//   case FilterQuality::Medium:
//     return Gdiplus::InterpolationModeHighQualityBilinear;
//   case FilterQuality::High:
//     return Gdiplus::InterpolationModeHighQualityBicubic;
//   }
//   return Gdiplus::InterpolationModeBilinear;
// }

// void Painter::drawImage(const ImageDrawParams &params)
// {
//   if (!params.image || params.clipW <= 0 || params.clipH <= 0)
//     return;

//   bool needsScale = (params.srcWidth != (int)params.destW ||
//                      params.srcHeight != (int)params.destH);
//   Gdiplus::InterpolationMode interp = needsScale
//                                           ? gdiInterpolationFromQuality(params.filterQuality)
//                                           : Gdiplus::InterpolationModeNearestNeighbor;

//   // ── Fast path: no border radius ──────────────────────────────────────────
//   if (params.borderRadius <= 0)
//   {
//     Gdiplus::Graphics g(ctx.hdc);
//     g.SetInterpolationMode(interp);
//     g.SetClip(Gdiplus::Rect(params.clipX, params.clipY, params.clipW, params.clipH));

//     if (params.repeat != ImageRepeat::NoRepeat)
//     {
//       float tileW = params.destW, tileH = params.destH;
//       float startX = (params.repeat == ImageRepeat::RepeatY) ? params.destX : (float)params.clipX;
//       float startY = (params.repeat == ImageRepeat::RepeatX) ? params.destY : (float)params.clipY;
//       float endX = (params.repeat == ImageRepeat::RepeatY) ? params.destX + tileW : (float)(params.clipX + params.clipW);
//       float endY = (params.repeat == ImageRepeat::RepeatX) ? params.destY + tileH : (float)(params.clipY + params.clipH);

//       for (float ty = startY; ty < endY; ty += tileH)
//         for (float tx = startX; tx < endX; tx += tileW)
//           g.DrawImage(params.image, Gdiplus::RectF(tx, ty, tileW, tileH));
//     }
//     else
//     {
//       g.DrawImage(params.image,
//                   Gdiplus::RectF(params.destX, params.destY, params.destW, params.destH));
//     }
//     return;
//   }

//   // ── Rounded corners: render to an offscreen ARGB bitmap, then blit ───────
//   int ow = params.clipW, oh = params.clipH;
//   Gdiplus::Bitmap offscreen(ow, oh, PixelFormat32bppPARGB);
//   if (offscreen.GetLastStatus() != Gdiplus::Ok)
//     return;

//   {
//     Gdiplus::Graphics og(&offscreen);
//     og.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
//     og.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
//     og.SetInterpolationMode(interp);
//     og.Clear(Gdiplus::Color(0, 0, 0, 0));

//     float r = (float)params.borderRadius;
//     float diam = r * 2.0f;
//     float fw = (float)ow;
//     float fh = (float)oh;

//     Gdiplus::GraphicsPath path;
//     path.AddArc(0.f, 0.f, diam, diam, 180.f, 90.f);
//     path.AddArc(fw - diam, 0.f, diam, diam, 270.f, 90.f);
//     path.AddArc(fw - diam, fh - diam, diam, diam, 0.f, 90.f);
//     path.AddArc(0.f, fh - diam, diam, diam, 90.f, 90.f);
//     path.CloseFigure();

//     Gdiplus::Region region(&path);
//     og.SetClip(&region);

//     float lx = params.destX - (float)params.clipX;
//     float ly = params.destY - (float)params.clipY;

//     if (params.repeat != ImageRepeat::NoRepeat)
//     {
//       float tileW = params.destW, tileH = params.destH;
//       float startX = (params.repeat == ImageRepeat::RepeatY) ? lx : 0.f;
//       float startY = (params.repeat == ImageRepeat::RepeatX) ? ly : 0.f;
//       float endX = (params.repeat == ImageRepeat::RepeatY) ? lx + tileW : (float)ow;
//       float endY = (params.repeat == ImageRepeat::RepeatX) ? ly + tileH : (float)oh;
//       for (float ty = startY; ty < endY; ty += tileH)
//         for (float tx = startX; tx < endX; tx += tileW)
//           og.DrawImage(params.image, Gdiplus::RectF(tx, ty, tileW, tileH));
//     }
//     else
//     {
//       og.DrawImage(params.image, Gdiplus::RectF(lx, ly, params.destW, params.destH));
//     }
//   }

//   Gdiplus::Graphics g(ctx.hdc);
//   g.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
//   g.DrawImage(&offscreen,
//               Gdiplus::RectF((float)params.clipX, (float)params.clipY, (float)ow, (float)oh));
// }

// void Painter::drawVideo(const VideoDrawParams &params)
// {
//   if (!params.pixels || !params.bmi ||
//       params.srcW <= 0 || params.srcH <= 0 ||
//       params.dstW <= 0 || params.dstH <= 0)
//     return;

//   ::SetStretchBltMode(ctx.hdc, HALFTONE);
//   ::SetBrushOrgEx(ctx.hdc, 0, 0, nullptr);
//   ::StretchDIBits(ctx.hdc,
//                   params.dstX, params.dstY, params.dstW, params.dstH,
//                   0, 0, params.srcW, params.srcH,
//                   params.pixels, params.bmi,
//                   DIB_RGB_COLORS, SRCCOPY);
// }

// void Painter::drawCamera(const CameraDrawParams &params)
// {
//   if (!params.pixels || !params.bmi ||
//       params.srcW <= 0 || params.srcH <= 0 ||
//       params.dstW <= 0 || params.dstH <= 0)
//     return;

//   ::SetStretchBltMode(ctx.hdc, HALFTONE);
//   ::SetBrushOrgEx(ctx.hdc, 0, 0, nullptr);

//   if (params.mirror)
//   {
//     // Negative dstW mirrors horizontally — same trick camera_widget_win32 uses
//     ::StretchDIBits(ctx.hdc,
//                     params.dstX + params.dstW, params.dstY,
//                     -params.dstW, params.dstH,
//                     0, 0, params.srcW, params.srcH,
//                     params.pixels, params.bmi,
//                     DIB_RGB_COLORS, SRCCOPY);
//   }
//   else
//   {
//     ::StretchDIBits(ctx.hdc,
//                     params.dstX, params.dstY, params.dstW, params.dstH,
//                     0, 0, params.srcW, params.srcH,
//                     params.pixels, params.bmi,
//                     DIB_RGB_COLORS, SRCCOPY);
//   }
// }


// // ============================================================================
// // Painter::drawPage  (Win32 / GDI+)
// // Append to flux_painter_win32.cpp, inside the #ifdef _WIN32 block.
// // ============================================================================

// void Painter::drawPage(const PageDrawParams &params)
// {
//     if (params.hasPageBackground)
//         fillRect(params.x, params.y, params.w, params.h, params.pageBackground);

//     if (params.body.present && params.body.hasBackground)
//         fillRect(params.body.x, params.body.y, params.body.w, params.body.h,
//                  params.body.background);

//     if (params.header.present)
//     {
//         if (params.header.hasBackground)
//             fillRect(params.header.x, params.header.y, params.header.w, params.header.h,
//                      params.header.background);
//         if (params.header.elevation > 0)
//         {
//             Color from = Color::fromRGB(0, 0, 0).withAlpha(60);
//             Color to = Color::fromRGB(0, 0, 0).withAlpha(0);
//             int shadowY = params.header.y + params.header.h;
//             for (int i = 0; i < params.header.elevation; ++i)
//             {
//                 double t = (double)i / params.header.elevation;
//                 fillRectAlpha(params.header.x, shadowY + i, params.header.w, 1,
//                               from.interpolate(to, t));
//             }
//         }
//     }

//     if (params.footer.present)
//     {
//         if (params.footer.hasBackground)
//             fillRect(params.footer.x, params.footer.y, params.footer.w, params.footer.h,
//                      params.footer.background);
//         if (params.footer.elevation > 0)
//         {
//             Color from = Color::fromRGB(0, 0, 0).withAlpha(0);
//             Color to = Color::fromRGB(0, 0, 0).withAlpha(60);
//             int startY = params.footer.y - params.footer.elevation;
//             for (int i = 0; i < params.footer.elevation; ++i)
//             {
//                 double t = (double)i / params.footer.elevation;
//                 fillRectAlpha(params.footer.x, startY + i, params.footer.w, 1,
//                               from.interpolate(to, t));
//             }
//         }
//     }
// }

// #endif // _WIN32