// // platform/win32/flux_canvas2d_win32.cpp
// #include "flux/flux_canvas2d.hpp"
// #ifdef _WIN32

// #include <windows.h>
// #include <gdiplus.h>
// #pragma comment(lib, "gdiplus.lib")

// #include <cassert>
// #include <cmath>
// #include <sstream>
// #include <algorithm>

// // ── Convenience casts ─────────────────────────────────────────────────────────
// static inline Gdiplus::Graphics* G(void* p)  { return static_cast<Gdiplus::Graphics*>(p); }
// static inline Gdiplus::GraphicsPath* P(void* p) { return static_cast<Gdiplus::GraphicsPath*>(p); }

// static Gdiplus::Color toGdip(Color c) {
//     return Gdiplus::Color(c.a, c.r, c.g, c.b);
// }

// // ── Constructor / destructor ──────────────────────────────────────────────────
// Canvas2D::Canvas2D(void* platformCtx, int canvasW, int canvasH)
//     : platformCtx_(platformCtx), canvasW_(canvasW), canvasH_(canvasH) {
//     currentPath_ = new Gdiplus::GraphicsPath();
//     G(platformCtx_)->SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
//     G(platformCtx_)->SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);
// }

// Canvas2D::~Canvas2D() {
//     delete P(currentPath_);
// }

// // ── State stack ───────────────────────────────────────────────────────────────
// void Canvas2D::save()    { G(platformCtx_)->Save(); }
// void Canvas2D::restore() { G(platformCtx_)->Restore(Gdiplus::GraphicsState{}); }

// // ── Transform ─────────────────────────────────────────────────────────────────
// void Canvas2D::translate(float dx, float dy) {
//     G(platformCtx_)->TranslateTransform(dx, dy);
// }
// void Canvas2D::scale(float sx, float sy) {
//     G(platformCtx_)->ScaleTransform(sx, sy);
// }
// void Canvas2D::rotate(float radians) {
//     G(platformCtx_)->RotateTransform(radians * 180.f / 3.14159265f);
// }
// void Canvas2D::resetTransform() {
//     G(platformCtx_)->ResetTransform();
// }

// // ── Style setters ─────────────────────────────────────────────────────────────
// void Canvas2D::setFillColor(Color c)   { fillColor_   = c; }
// void Canvas2D::setStrokeColor(Color c) { strokeColor_ = c; }
// void Canvas2D::setLineWidth(float w)   { lineWidth_   = w; }
// void Canvas2D::setGlobalAlpha(float a) { globalAlpha_ = std::clamp(a, 0.f, 1.f); }

// void Canvas2D::setLineCap(LineCap cap) {
//     // stored; applied when stroke() is called
//     (void)cap; // TODO: map to Gdiplus::LineCap
// }
// void Canvas2D::setLineJoin(LineJoin join) {
//     (void)join; // TODO: map to Gdiplus::LineJoin
// }
// void Canvas2D::setCompositeOp(CompositeOp op) {
//     switch (op) {
//     case CompositeOp::Copy:
//         G(platformCtx_)->SetCompositingMode(Gdiplus::CompositingModeSourceCopy);
//         break;
//     default:
//         G(platformCtx_)->SetCompositingMode(Gdiplus::CompositingModeSourceOver);
//         break;
//     }
// }

// // ── Gradient ─────────────────────────────────────────────────────────────────
// void Canvas2D::beginLinearGradient(float x0, float y0, float x1, float y1) {
//     x0_ = x0; y0_ = y0; x1_ = x1; y1_ = y1;
//     gradientStops_.clear();
//     hasGradient_ = true;
// }
// void Canvas2D::addColorStop(float t, Color c) {
//     gradientStops_.push_back({t, c});
// }
// void Canvas2D::setFillGradient() { /* hasGradient_ already true */ }

// // ── clearRect ─────────────────────────────────────────────────────────────────
// void Canvas2D::clearRect(float x, float y, float w, float h) {
//     G(platformCtx_)->SetCompositingMode(Gdiplus::CompositingModeSourceCopy);
//     Gdiplus::SolidBrush b(Gdiplus::Color(0,0,0,0));
//     G(platformCtx_)->FillRectangle(&b, x, y, w, h);
//     G(platformCtx_)->SetCompositingMode(Gdiplus::CompositingModeSourceOver);
// }

// // ── fillRect / strokeRect ─────────────────────────────────────────────────────
// void Canvas2D::fillRect(float x, float y, float w, float h) {
//     if (hasGradient_ && !gradientStops_.empty()) {
//         // Build a LinearGradientBrush from the stored stops
//         Gdiplus::PointF p0(x0_, y0_), p1(x1_, y1_);
//         // Simple 2-stop case (most common)
//         Color c0 = gradientStops_.front().color;
//         Color c1 = gradientStops_.back().color;
//         Gdiplus::LinearGradientBrush lgb(
//             p0, p1, toGdip(c0), toGdip(c1));
//         // Multi-stop
//         if (gradientStops_.size() > 2) {
//             std::vector<Gdiplus::Color> gc;
//             std::vector<float>          gt;
//             for (auto& s : gradientStops_) { gc.push_back(toGdip(s.color)); gt.push_back(s.t); }
//             lgb.SetInterpolationColors(gc.data(), gt.data(), (int)gc.size());
//         }
//         G(platformCtx_)->FillRectangle(&lgb, x, y, w, h);
//     } else {
//         Color fc = fillColor_;
//         fc.a = (uint8_t)(fc.a * globalAlpha_);
//         Gdiplus::SolidBrush b(toGdip(fc));
//         G(platformCtx_)->FillRectangle(&b, x, y, w, h);
//     }
// }

// void Canvas2D::strokeRect(float x, float y, float w, float h) {
//     Color sc = strokeColor_;
//     sc.a = (uint8_t)(sc.a * globalAlpha_);
//     Gdiplus::Pen pen(toGdip(sc), lineWidth_);
//     G(platformCtx_)->DrawRectangle(&pen, x, y, w, h);
// }

// // ── Rounded rects ─────────────────────────────────────────────────────────────
// static void addRoundedRectPath(Gdiplus::GraphicsPath& path,
//                                 float x, float y, float w, float h, float r) {
//     float d = r * 2.f;
//     path.AddArc(x,       y,       d, d, 180, 90);
//     path.AddArc(x+w-d,   y,       d, d, 270, 90);
//     path.AddArc(x+w-d,   y+h-d,   d, d,   0, 90);
//     path.AddArc(x,       y+h-d,   d, d,  90, 90);
//     path.CloseFigure();
// }

// void Canvas2D::fillRoundedRect(float x, float y, float w, float h, float r) {
//     Gdiplus::GraphicsPath path;
//     addRoundedRectPath(path, x, y, w, h, r > 0 ? r : 0);
//     Color fc = fillColor_; fc.a = (uint8_t)(fc.a * globalAlpha_);
//     Gdiplus::SolidBrush b(toGdip(fc));
//     G(platformCtx_)->FillPath(&b, &path);
// }

// void Canvas2D::strokeRoundedRect(float x, float y, float w, float h, float r) {
//     Gdiplus::GraphicsPath path;
//     addRoundedRectPath(path, x, y, w, h, r > 0 ? r : 0);
//     Color sc = strokeColor_; sc.a = (uint8_t)(sc.a * globalAlpha_);
//     Gdiplus::Pen pen(toGdip(sc), lineWidth_);
//     G(platformCtx_)->DrawPath(&pen, &path);
// }

// // ── Path API ──────────────────────────────────────────────────────────────────
// void Canvas2D::beginPath() {
//     delete P(currentPath_);
//     currentPath_ = new Gdiplus::GraphicsPath();
// }
// void Canvas2D::closePath() { P(currentPath_)->CloseFigure(); }
// void Canvas2D::moveTo(float x, float y) {
//     P(currentPath_)->StartFigure();
//     // store last point for lineTo
//     (void)x; (void)y;
//     // GDI+ paths don't have a moveTo — we track manually
//     lastX_ = x; lastY_ = y;  // add float lastX_, lastY_ to private
// }
// void Canvas2D::lineTo(float x, float y) {
//     P(currentPath_)->AddLine(lastX_, lastY_, x, y);
//     lastX_ = x; lastY_ = y;
// }
// void Canvas2D::rect(float x, float y, float w, float h) {
//     P(currentPath_)->AddRectangle(Gdiplus::RectF(x, y, w, h));
// }
// void Canvas2D::arc(float cx, float cy, float radius,
//                    float startAngle, float endAngle, bool anticlockwise) {
//     float start = startAngle * 180.f / 3.14159265f;
//     float end   = endAngle   * 180.f / 3.14159265f;
//     float sweep = anticlockwise ? -(end - start) : (end - start);
//     P(currentPath_)->AddArc(cx - radius, cy - radius,
//                              radius * 2.f, radius * 2.f,
//                              start, sweep);
//     lastX_ = cx + radius * std::cos(endAngle);
//     lastY_ = cy + radius * std::sin(endAngle);
// }
// void Canvas2D::arcTo(float x1, float y1, float x2, float y2, float radius) {
//     // Approximate via two tangent lines + arc
//     (void)x1; (void)y1; (void)x2; (void)y2; (void)radius; // TODO: full impl
// }
// void Canvas2D::quadraticCurveTo(float cpx, float cpy, float x, float y) {
//     // Convert quadratic → cubic Bezier for GDI+
//     float cp1x = lastX_ + 2.f/3.f * (cpx - lastX_);
//     float cp1y = lastY_ + 2.f/3.f * (cpy - lastY_);
//     float cp2x = x      + 2.f/3.f * (cpx - x);
//     float cp2y = y      + 2.f/3.f * (cpy - y);
//     bezierCurveTo(cp1x, cp1y, cp2x, cp2y, x, y);
// }
// void Canvas2D::bezierCurveTo(float cp1x, float cp1y,
//                               float cp2x, float cp2y,
//                               float x,    float y) {
//     P(currentPath_)->AddBezier(lastX_, lastY_, cp1x, cp1y, cp2x, cp2y, x, y);
//     lastX_ = x; lastY_ = y;
// }

// void Canvas2D::fill() {
//     Color fc = fillColor_; fc.a = (uint8_t)(fc.a * globalAlpha_);
//     Gdiplus::SolidBrush b(toGdip(fc));
//     G(platformCtx_)->FillPath(&b, P(currentPath_));
// }
// void Canvas2D::stroke() {
//     Color sc = strokeColor_; sc.a = (uint8_t)(sc.a * globalAlpha_);
//     Gdiplus::Pen pen(toGdip(sc), lineWidth_);
//     G(platformCtx_)->DrawPath(&pen, P(currentPath_));
// }
// void Canvas2D::clip() {
//     G(platformCtx_)->SetClip(P(currentPath_), Gdiplus::CombineModeIntersect);
// }

// // ── Images ────────────────────────────────────────────────────────────────────
// Canvas2DImage* Canvas2D::loadImage(const std::string& path) {
//     std::wstring wpath(path.begin(), path.end());
//     auto* bmp = new Gdiplus::Bitmap(wpath.c_str());
//     if (bmp->GetLastStatus() != Gdiplus::Ok) { delete bmp; return nullptr; }
//     auto* img = new Canvas2DImage();
//     img->nativeHandle = bmp;
//     img->width  = (int)bmp->GetWidth();
//     img->height = (int)bmp->GetHeight();
//     return img;
// }
// void Canvas2D::freeImage(Canvas2DImage* img) {
//     if (!img) return;
//     delete static_cast<Gdiplus::Bitmap*>(img->nativeHandle);
//     delete img;
// }

// void Canvas2D::drawImage(const Canvas2DImage* img, float dx, float dy) {
//     if (!img) return;
//     G(platformCtx_)->DrawImage(
//         static_cast<Gdiplus::Bitmap*>(img->nativeHandle), dx, dy);
// }
// void Canvas2D::drawImage(const Canvas2DImage* img,
//                           float dx, float dy, float dw, float dh) {
//     if (!img) return;
//     G(platformCtx_)->DrawImage(
//         static_cast<Gdiplus::Bitmap*>(img->nativeHandle),
//         Gdiplus::RectF(dx, dy, dw, dh));
// }
// void Canvas2D::drawImage(const Canvas2DImage* img,
//                           float sx, float sy, float sw, float sh,
//                           float dx, float dy, float dw, float dh) {
//     if (!img) return;
//     Gdiplus::RectF dest(dx, dy, dw, dh);
//     G(platformCtx_)->DrawImage(
//         static_cast<Gdiplus::Bitmap*>(img->nativeHandle),
//         dest, sx, sy, sw, sh, Gdiplus::UnitPixel);
// }

// // ── Text ──────────────────────────────────────────────────────────────────────
// // Parse "bold 14px Segoe UI" → Gdiplus::Font
// static Gdiplus::Font* parseFont(const std::string& desc) {
//     bool bold = desc.find("bold") != std::string::npos;
//     float size = 14.f;
//     std::string family = "Segoe UI";

//     std::istringstream ss(desc);
//     std::string token;
//     while (ss >> token) {
//         if (token == "bold") continue;
//         if (token.size() > 2 && token.substr(token.size()-2) == "px") {
//             size = std::stof(token.substr(0, token.size()-2));
//         } else if (token != "bold") {
//             family = token;
//             // rest of stream is the font name
//             std::string rest;
//             std::getline(ss, rest);
//             if (!rest.empty()) family += rest;
//             break;
//         }
//     }

//     std::wstring wfamily(family.begin(), family.end());
//     Gdiplus::FontFamily ff(wfamily.c_str());
//     Gdiplus::FontStyle style = bold ? Gdiplus::FontStyleBold
//                                      : Gdiplus::FontStyleRegular;
//     return new Gdiplus::Font(&ff, size, style, Gdiplus::UnitPixel);
// }

// void Canvas2D::setFont(const std::string& fontDesc) {
//     fontDesc_ = fontDesc;
// }
// void Canvas2D::setTextAlign(TextAlign align)       { textAlign_    = align;    }
// void Canvas2D::setTextBaseline(TextBaseline base)  { textBaseline_ = base;     }

// void Canvas2D::fillText(const std::string& text, float x, float y, float maxWidth) {
//     if (text.empty()) return;
//     std::unique_ptr<Gdiplus::Font> font(parseFont(fontDesc_));

//     std::wstring wtext(text.begin(), text.end());
//     Color fc = fillColor_; fc.a = (uint8_t)(fc.a * globalAlpha_);
//     Gdiplus::SolidBrush b(toGdip(fc));

//     Gdiplus::StringFormat fmt;
//     switch (textAlign_) {
//     case TextAlign::Center: fmt.SetAlignment(Gdiplus::StringAlignmentCenter); break;
//     case TextAlign::Right:  fmt.SetAlignment(Gdiplus::StringAlignmentFar);   break;
//     default:                fmt.SetAlignment(Gdiplus::StringAlignmentNear);  break;
//     }

//     // Measure for baseline offset
//     Gdiplus::RectF bounds;
//     G(platformCtx_)->MeasureString(wtext.c_str(), -1, font.get(),
//                                     Gdiplus::PointF(0,0), &bounds);
//     float offsetY = 0.f;
//     switch (textBaseline_) {
//     case TextBaseline::Middle:     offsetY = -bounds.Height * 0.5f; break;
//     case TextBaseline::Bottom:     offsetY = -bounds.Height;        break;
//     case TextBaseline::Top:        offsetY = 0.f;                   break;
//     case TextBaseline::Alphabetic: offsetY = -bounds.Height * 0.8f; break;
//     }

//     float drawW = (maxWidth > 0) ? maxWidth : 8192.f;
//     Gdiplus::RectF layout(x, y + offsetY, drawW, bounds.Height * 2.f);
//     G(platformCtx_)->DrawString(wtext.c_str(), -1, font.get(), layout, &fmt, &b);
// }

// void Canvas2D::strokeText(const std::string& text, float x, float y, float maxWidth) {
//     if (text.empty()) return;
//     std::unique_ptr<Gdiplus::Font> font(parseFont(fontDesc_));
//     std::wstring wtext(text.begin(), text.end());

//     // Convert text to a path, then stroke it
//     Gdiplus::GraphicsPath textPath;
//     Gdiplus::FontFamily ff; font->GetFamily(&ff);
//     Gdiplus::StringFormat fmt;
//     textPath.AddString(wtext.c_str(), -1, &ff,
//                         font->GetStyle(), font->GetSize(),
//                         Gdiplus::PointF(x, y), &fmt);

//     Color sc = strokeColor_; sc.a = (uint8_t)(sc.a * globalAlpha_);
//     Gdiplus::Pen pen(toGdip(sc), lineWidth_);
//     G(platformCtx_)->DrawPath(&pen, &textPath);
// }

// float Canvas2D::measureText(const std::string& text) {
//     if (text.empty()) return 0.f;
//     std::unique_ptr<Gdiplus::Font> font(parseFont(fontDesc_));
//     std::wstring wtext(text.begin(), text.end());
//     Gdiplus::RectF bounds;
//     G(platformCtx_)->MeasureString(wtext.c_str(), -1, font.get(),
//                                     Gdiplus::PointF(0.f, 0.f), &bounds);
//     return bounds.Width;
// }

// // ── Clip ──────────────────────────────────────────────────────────────────────
// void Canvas2D::pushClipRect(float x, float y, float w, float h) {
//     G(platformCtx_)->SetClip(Gdiplus::RectF(x, y, w, h),
//                               Gdiplus::CombineModeIntersect);
// }
// void Canvas2D::popClipRect() {
//     G(platformCtx_)->ResetClip();
// }

// // ── Pixel access ──────────────────────────────────────────────────────────────
// void Canvas2D::getImageData(float x, float y, float w, float h,
//                              std::vector<uint8_t>& out) {
//     int iw = (int)w, ih = (int)h;
//     out.resize(size_t(iw) * ih * 4);
//     Gdiplus::Bitmap bmp(iw, ih, PixelFormat32bppARGB);
//     Gdiplus::Graphics tmpG(&bmp);
//     tmpG.DrawImage(/* source bitmap from platformCtx? */
//                    // This requires access to the underlying bitmap —
//                    // CanvasWidget passes it in if needed
//                    nullptr, 0.f, 0.f);
//     Gdiplus::BitmapData bd;
//     Gdiplus::Rect r((int)x, (int)y, iw, ih);
//     bmp.LockBits(&r, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &bd);
//     memcpy(out.data(), bd.Scan0, out.size());
//     bmp.UnlockBits(&bd);
// }

// void Canvas2D::putImageData(const std::vector<uint8_t>& data,
//                              int srcW, int srcH, float dx, float dy) {
//     Gdiplus::Bitmap bmp(srcW, srcH, PixelFormat32bppARGB);
//     Gdiplus::BitmapData bd;
//     Gdiplus::Rect r(0, 0, srcW, srcH);
//     bmp.LockBits(&r, Gdiplus::ImageLockModeWrite, PixelFormat32bppARGB, &bd);
//     memcpy(bd.Scan0, data.data(), data.size());
//     bmp.UnlockBits(&bd);
//     G(platformCtx_)->DrawImage(&bmp, dx, dy);
// }

// #endif // _WIN32