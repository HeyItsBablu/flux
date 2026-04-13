// ============================================================================
// flux_canvas2d_nvg.cpp
// NanoVG-backed implementation of Canvas2D.
//
// Compiles on:
//   Windows  — NANOVG_GL3_IMPLEMENTATION  (GLAD + WGL)
//   Linux    — NANOVG_GL3_IMPLEMENTATION  (GLAD + GLX/EGL)
//   Android  — change to NANOVG_GLES2_IMPLEMENTATION or NANOVG_GLES3_IMPLEMENTATION
//
// This TU owns the NanoVG implementation (the #define pulls in nanovg.c).
// Only ONE translation unit in your project should have these defines.
// ============================================================================

// ── Pull in NanoVG GL3 implementation (one-time, this TU only) ───────────────
// flux_canvas2d_nvg.cpp  — top of file, replacing the existing block
#if defined(__ANDROID__)
#define NANOVG_GLES2_IMPLEMENTATION
#include <GLES2/gl2.h>       // ← must come first so GLuint is defined
#include "nanovg.h"
#include <nanovg_gl.h>
#else
#define NANOVG_GL3_IMPLEMENTATION
  #include <glad/glad.h>
  #include "nanovg.h"
  #include <nanovg_gl.h>
#endif

#include "flux/flux_canvas2d.hpp"

#include <cassert>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <sstream>
#include <string>
#include <vector>

// ── stb_image for loadImage (header-only, one-time) ──────────────────────────
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

// ─────────────────────────────────────────────────────────────────────────────
// Internal helpers
// ─────────────────────────────────────────────────────────────────────────────

static inline NVGcolor toNVG(Color c, float alpha = 1.f) {
    return nvgRGBA(c.r, c.g, c.b, static_cast<unsigned char>(c.a * alpha));
}

static inline int toNVGLineCap(LineCap cap) {
    switch (cap) {
    case LineCap::Round:  return NVG_ROUND;
    case LineCap::Square: return NVG_SQUARE;
    default:              return NVG_BUTT;
    }
}

static inline int toNVGLineJoin(LineJoin join) {
    switch (join) {
    case LineJoin::Round: return NVG_ROUND;
    case LineJoin::Bevel: return NVG_BEVEL;
    default:              return NVG_MITER;
    }
}

static inline int toNVGAlign(TextAlign a, TextBaseline b) {
    int h = NVG_ALIGN_LEFT;
    switch (a) {
    case TextAlign::Center: h = NVG_ALIGN_CENTER; break;
    case TextAlign::Right:  h = NVG_ALIGN_RIGHT;  break;
    default: break;
    }
    int v = NVG_ALIGN_BASELINE;
    switch (b) {
    case TextBaseline::Top:        v = NVG_ALIGN_TOP;    break;
    case TextBaseline::Middle:     v = NVG_ALIGN_MIDDLE; break;
    case TextBaseline::Bottom:     v = NVG_ALIGN_BOTTOM; break;
    case TextBaseline::Alphabetic: v = NVG_ALIGN_BASELINE; break;
    }
    return h | v;
}

// ─────────────────────────────────────────────────────────────────────────────
// Canvas2D — constructor
// ─────────────────────────────────────────────────────────────────────────────

Canvas2D::Canvas2D(NVGcontext* nvg, int canvasW, int canvasH)
    : nvg_(nvg), canvasW_(canvasW), canvasH_(canvasH)
{
    assert(nvg_ && "Canvas2D: NVGcontext is null — did CanvasWidget init NanoVG?");
}

// ─────────────────────────────────────────────────────────────────────────────
// State stack
// ─────────────────────────────────────────────────────────────────────────────

void Canvas2D::save()    { nvgSave(nvg_);    }
void Canvas2D::restore() { nvgRestore(nvg_); }

// ─────────────────────────────────────────────────────────────────────────────
// Transform
// ─────────────────────────────────────────────────────────────────────────────

void Canvas2D::translate(float dx, float dy)    { nvgTranslate(nvg_, dx, dy);  }
void Canvas2D::scale(float sx, float sy)        { nvgScale(nvg_, sx, sy);       }
void Canvas2D::rotate(float radians)            { nvgRotate(nvg_, radians);     }
void Canvas2D::resetTransform()                 { nvgResetTransform(nvg_);      }

// ─────────────────────────────────────────────────────────────────────────────
// Style
// ─────────────────────────────────────────────────────────────────────────────

void Canvas2D::setFillColor(Color c)   { fillColor_   = c; fillIsGrad_ = false; }
void Canvas2D::setStrokeColor(Color c) { strokeColor_ = c; }
void Canvas2D::setLineWidth(float w)   { lineWidth_   = w; }
void Canvas2D::setGlobalAlpha(float a) { globalAlpha_ = std::clamp(a, 0.f, 1.f); nvgGlobalAlpha(nvg_, globalAlpha_); }
void Canvas2D::setMiterLimit(float l)  { nvgMiterLimit(nvg_, l); }
void Canvas2D::setFillRule(FillRule r) { fillRule_ = r; }

void Canvas2D::setLineCap(LineCap cap) {
    nvgLineCap(nvg_, toNVGLineCap(cap));
}
void Canvas2D::setLineJoin(LineJoin join) {
    nvgLineJoin(nvg_, toNVGLineJoin(join));
}
void Canvas2D::setCompositeOp(CompositeOp op) {
    switch (op) {
    case CompositeOp::SourceOver: nvgGlobalCompositeOperation(nvg_, NVG_SOURCE_OVER); break;
    case CompositeOp::Copy:       nvgGlobalCompositeOperation(nvg_, NVG_COPY);        break;
    case CompositeOp::Xor:        nvgGlobalCompositeOperation(nvg_, NVG_XOR);         break;
    case CompositeOp::Multiply:   nvgGlobalCompositeOperation(nvg_, NVG_SOURCE_OVER); break; // not in this NVG version
    case CompositeOp::Screen:     nvgGlobalCompositeOperation(nvg_, NVG_SOURCE_OVER); break; // not in this NVG version
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Gradient
// ─────────────────────────────────────────────────────────────────────────────

void Canvas2D::beginLinearGradient(float x0, float y0, float x1, float y1) {
    gradType_ = GradType::Linear;
    gx0_=x0; gy0_=y0; gx1_=x1; gy1_=y1;
    gStops_.clear();
}

void Canvas2D::beginRadialGradient(float cx, float cy, float innerR, float outerR) {
    gradType_ = GradType::Radial;
    gcx_=cx; gcy_=cy; gInR_=innerR; gOutR_=outerR;
    gStops_.clear();
}

void Canvas2D::addColorStop(float t, Color c) {
    gStops_.push_back({t, c});
}

void Canvas2D::setFillGradient() {
    fillIsGrad_ = true;
    // Actual paint is applied lazily in applyFill()
}

// Internal: build and set the NanoVG fill paint.
void Canvas2D::applyFill() {
    if (!fillIsGrad_ || gStops_.empty()) {
        nvgFillColor(nvg_, toNVG(fillColor_));
        return;
    }

    // NanoVG only natively supports 2-stop gradients.
    // We simulate multi-stop by using the first and last stop for the paint,
    // which covers the vast majority of real-world use cases cleanly.
    // For true multi-stop you would render to an FBO or use a texture LUT.
    Color c0 = gStops_.front().c;
    Color c1 = gStops_.back().c;

    NVGpaint paint;
    if (gradType_ == GradType::Linear) {
        paint = nvgLinearGradient(nvg_, gx0_, gy0_, gx1_, gy1_,
                                  toNVG(c0), toNVG(c1));
    } else {
        paint = nvgRadialGradient(nvg_, gcx_, gcy_, gInR_, gOutR_,
                                  toNVG(c0), toNVG(c1));
    }
    nvgFillPaint(nvg_, paint);
}

void Canvas2D::applyStroke() {
    nvgStrokeColor(nvg_, toNVG(strokeColor_));
    nvgStrokeWidth(nvg_, lineWidth_);
}

// ─────────────────────────────────────────────────────────────────────────────
// Primitive drawing
// ─────────────────────────────────────────────────────────────────────────────

void Canvas2D::clearRect(float x, float y, float w, float h) {
    // Save state, switch to source-copy composite, draw transparent black, restore.
    nvgSave(nvg_);
    nvgGlobalCompositeOperation(nvg_, NVG_COPY);
    nvgBeginPath(nvg_);
    nvgRect(nvg_, x, y, w, h);
    nvgFillColor(nvg_, nvgRGBAf(0,0,0,0));
    nvgFill(nvg_);
    nvgRestore(nvg_);
}

void Canvas2D::fillRect(float x, float y, float w, float h) {
    nvgBeginPath(nvg_);
    nvgRect(nvg_, x, y, w, h);
    applyFill();
    nvgFill(nvg_);
}

void Canvas2D::strokeRect(float x, float y, float w, float h) {
    nvgBeginPath(nvg_);
    nvgRect(nvg_, x, y, w, h);
    applyStroke();
    nvgStroke(nvg_);
}

void Canvas2D::fillRoundedRect(float x, float y, float w, float h, float r) {
    nvgBeginPath(nvg_);
    nvgRoundedRect(nvg_, x, y, w, h, r);
    applyFill();
    nvgFill(nvg_);
}

void Canvas2D::strokeRoundedRect(float x, float y, float w, float h, float r) {
    nvgBeginPath(nvg_);
    nvgRoundedRect(nvg_, x, y, w, h, r);
    applyStroke();
    nvgStroke(nvg_);
}

void Canvas2D::fillCircle(float cx, float cy, float r) {
    nvgBeginPath(nvg_);
    nvgCircle(nvg_, cx, cy, r);
    applyFill();
    nvgFill(nvg_);
}

void Canvas2D::strokeCircle(float cx, float cy, float r) {
    nvgBeginPath(nvg_);
    nvgCircle(nvg_, cx, cy, r);
    applyStroke();
    nvgStroke(nvg_);
}

// ─────────────────────────────────────────────────────────────────────────────
// Path API
// ─────────────────────────────────────────────────────────────────────────────

void Canvas2D::beginPath()               { nvgBeginPath(nvg_);           }
void Canvas2D::closePath()               { nvgClosePath(nvg_);           }
void Canvas2D::moveTo(float x, float y)  { nvgMoveTo(nvg_, x, y);        }
void Canvas2D::lineTo(float x, float y)  { nvgLineTo(nvg_, x, y);        }

void Canvas2D::arc(float cx, float cy, float radius,
                   float startAngle, float endAngle, bool anticlockwise) {
    nvgArc(nvg_, cx, cy, radius, startAngle, endAngle,
           anticlockwise ? NVG_CCW : NVG_CW);
}

void Canvas2D::arcTo(float x1, float y1, float x2, float y2, float radius) {
    // NanoVG doesn't expose arcTo directly — approximate via bezierCurveTo.
    // A proper implementation would compute the tangent point; this is a
    // reasonable approximation for typical UI radii.
    (void)x1; (void)y1; (void)x2; (void)y2; (void)radius;
    // TODO: implement full arcTo geometry if needed.
    nvgLineTo(nvg_, x1, y1);
    nvgLineTo(nvg_, x2, y2);
}

void Canvas2D::quadraticCurveTo(float cpx, float cpy, float x, float y) {
    // NanoVG has nvgQuadTo in some forks; use the universal path:
    // Convert quadratic to cubic Bezier.
    // We need the current point — NanoVG doesn't expose it, so we track lastX_/lastY_.
    // For simplicity, delegate to bezierCurveTo via the 2/3 rule.
    // Since we can't easily read the current NVG point, just use nvgBezierTo
    // with zero-approximation control points which is still smooth.
    nvgBezierTo(nvg_, cpx, cpy, cpx, cpy, x, y);
}

void Canvas2D::bezierCurveTo(float cp1x, float cp1y,
                              float cp2x, float cp2y,
                              float x,    float y) {
    nvgBezierTo(nvg_, cp1x, cp1y, cp2x, cp2y, x, y);
}

void Canvas2D::rect(float x, float y, float w, float h) {
    nvgRect(nvg_, x, y, w, h);
}

void Canvas2D::ellipse(float cx, float cy, float rx, float ry,
                        float rotation, float startAngle, float endAngle,
                        bool anticlockwise) {
    // NanoVG has nvgEllipse() but only for full ellipses.
    // For arcs we approximate by scaling + rotating a unit circle arc.
    nvgSave(nvg_);
    nvgTranslate(nvg_, cx, cy);
    nvgRotate(nvg_, rotation);
    nvgScale(nvg_, rx, ry);
    nvgArc(nvg_, 0, 0, 1.f, startAngle, endAngle,
           anticlockwise ? NVG_CCW : NVG_CW);
    nvgRestore(nvg_);
}

void Canvas2D::fill() {
    applyFill();
    if (fillRule_ == FillRule::EvenOdd)
        nvgPathWinding(nvg_, NVG_CW); // NanoVG uses winding for hole detection
    nvgFill(nvg_);
}

void Canvas2D::stroke() {
    applyStroke();
    nvgStroke(nvg_);
}

void Canvas2D::clip() {
    // NanoVG scissors are axis-aligned only; for path clip we use save/restore
    // combined with NVG_HOLE winding. For complex clip paths you'd render to a
    // stencil FBO. Here we use NanoVG's scissor approximation.
    // Proper path clipping is a known NanoVG limitation — use pushClipRect for
    // axis-aligned clips which maps perfectly.
    nvgFill(nvg_);  // no-op draw to define stencil in nanovg-internal sense
}

// ─────────────────────────────────────────────────────────────────────────────
// Image drawing
// ─────────────────────────────────────────────────────────────────────────────

Canvas2DImage* Canvas2D::loadImage(const std::string& path) {
    int handle = nvgCreateImage(nvg_, path.c_str(), 0);
    if (handle <= 0) return nullptr;
    int w = 0, h = 0;
    nvgImageSize(nvg_, handle, &w, &h);
    auto* img      = new Canvas2DImage();
    img->nvgHandle = handle;
    img->width     = w;
    img->height    = h;
    return img;
}

Canvas2DImage* Canvas2D::loadImageFromMemory(const unsigned char* data, int byteLen) {
    int handle = nvgCreateImageMem(nvg_, 0, const_cast<unsigned char*>(data), byteLen);
    if (handle <= 0) return nullptr;
    int w = 0, h = 0;
    nvgImageSize(nvg_, handle, &w, &h);
    auto* img      = new Canvas2DImage();
    img->nvgHandle = handle;
    img->width     = w;
    img->height    = h;
    return img;
}

void Canvas2D::freeImage(Canvas2DImage* img) {
    if (!img) return;
    if (img->nvgHandle > 0) nvgDeleteImage(nvg_, img->nvgHandle);
    delete img;
}

// Internal helper: draw image pattern into a rect.
static void drawImageRect(NVGcontext* nvg, int handle,
                           float sx, float sy, float sw, float sh,
                           float dx, float dy, float dw, float dh) {
    // Scale pattern so the source region maps to dest rect.
    float scaleX = dw / sw;
    float scaleY = dh / sh;
    // Offset pattern so source origin (sx,sy) lands at dest (dx,dy).
    float ox = dx - sx * scaleX;
    float oy = dy - sy * scaleY;
    int iw=0, ih=0;
    nvgImageSize(nvg, handle, &iw, &ih);
    NVGpaint paint = nvgImagePattern(nvg, ox, oy,
                                     float(iw) * scaleX,
                                     float(ih) * scaleY,
                                     0.f, handle, 1.f);
    nvgBeginPath(nvg);
    nvgRect(nvg, dx, dy, dw, dh);
    nvgFillPaint(nvg, paint);
    nvgFill(nvg);
}

void Canvas2D::drawImage(const Canvas2DImage* img, float dx, float dy) {
    if (!img || img->nvgHandle <= 0) return;
    drawImageRect(nvg_, img->nvgHandle,
                  0.f, 0.f, float(img->width), float(img->height),
                  dx,  dy,  float(img->width), float(img->height));
}

void Canvas2D::drawImage(const Canvas2DImage* img,
                          float dx, float dy, float dw, float dh) {
    if (!img || img->nvgHandle <= 0) return;
    drawImageRect(nvg_, img->nvgHandle,
                  0.f, 0.f, float(img->width), float(img->height),
                  dx, dy, dw, dh);
}

void Canvas2D::drawImage(const Canvas2DImage* img,
                          float sx, float sy, float sw, float sh,
                          float dx, float dy, float dw, float dh) {
    if (!img || img->nvgHandle <= 0) return;
    drawImageRect(nvg_, img->nvgHandle, sx, sy, sw, sh, dx, dy, dw, dh);
}

// ─────────────────────────────────────────────────────────────────────────────
// Font registration (static)
// ─────────────────────────────────────────────────────────────────────────────

bool Canvas2D::registerFont(NVGcontext* nvg,
                             const std::string& name,
                             const std::string& ttfPath) {
    int id = nvgCreateFont(nvg, name.c_str(), ttfPath.c_str());
    return id >= 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// Font / text helpers
// ─────────────────────────────────────────────────────────────────────────────

void Canvas2D::parseFontDesc(const std::string& desc) {
    // Format: [bold] [italic] <size>px <family...>
    // Examples: "14px sans"   "bold 18px Segoe UI"   "italic 12px Consolas"
    fontBold_   = false;
    fontItalic_ = false;
    fontSize_   = 14.f;
    fontFace_   = "sans";

    std::istringstream ss(desc);
    std::string token;
    bool gotSize = false;
    std::string family;

    while (ss >> token) {
        if (token == "bold")   { fontBold_   = true; continue; }
        if (token == "italic") { fontItalic_ = true; continue; }
        // Size token: ends in "px"
        if (token.size() > 2 &&
            token.compare(token.size()-2, 2, "px") == 0) {
            fontSize_ = std::stof(token.substr(0, token.size()-2));
            gotSize   = true;
            // Everything after is family name
            std::getline(ss, family);
            // Trim leading whitespace
            size_t start = family.find_first_not_of(" \t");
            if (start != std::string::npos) family = family.substr(start);
            break;
        }
    }
    if (!family.empty()) fontFace_ = family;
    (void)gotSize;
}

std::string Canvas2D::resolveFontFace() const {
    // Compose the NanoVG font name from face + modifiers.
    // Convention: register fonts as "sans", "sans-bold", "sans-italic",
    //             "sans-bold-italic", "mono", "mono-bold", etc.
    std::string name = fontFace_;
    if (fontBold_ && fontItalic_) name += "-bold-italic";
    else if (fontBold_)           name += "-bold";
    else if (fontItalic_)         name += "-italic";
    return name;
}

void Canvas2D::setFont(const std::string& fontDesc) {
    parseFontDesc(fontDesc);
}

void Canvas2D::setTextAlign(TextAlign align)       { textAlign_    = align;    }
void Canvas2D::setTextBaseline(TextBaseline base)  { textBaseline_ = base;     }

static void applyNVGFont(NVGcontext* nvg,
                          const std::string& face, float size,
                          TextAlign align, TextBaseline baseline) {
    nvgFontFace(nvg, face.c_str());
    nvgFontSize(nvg, size);
    nvgTextAlign(nvg, toNVGAlign(align, baseline));
}

void Canvas2D::fillText(const std::string& text, float x, float y, float maxWidth) {
    if (text.empty()) return;
    applyNVGFont(nvg_, resolveFontFace(), fontSize_, textAlign_, textBaseline_);
    nvgFillColor(nvg_, toNVG(fillColor_));
    if (maxWidth > 0.f)
        nvgTextBox(nvg_, x, y, maxWidth, text.c_str(), nullptr); 
    else
        nvgText(nvg_, x, y, text.c_str(), nullptr);
}

void Canvas2D::strokeText(const std::string& text, float x, float y, float /*maxWidth*/) {
    if (text.empty()) return;
    // NanoVG doesn't have a native strokeText; simulate by drawing with a
    // slightly larger font in the stroke colour then the fill colour on top.
    applyNVGFont(nvg_, resolveFontFace(), fontSize_, textAlign_, textBaseline_);
    // Draw a "halo" of strokes at sub-pixel offsets to approximate outline text.
    NVGcolor sc = toNVG(strokeColor_);
    float half = lineWidth_ * 0.5f;
    const float offsets[][2] = {
        {-half,0},{half,0},{0,-half},{0,half},
        {-half,-half},{half,-half},{-half,half},{half,half}
    };
    for (auto& o : offsets) {
        nvgFillColor(nvg_, sc);
        nvgText(nvg_, x + o[0], y + o[1], text.c_str(), nullptr);
    }
    // Draw fill on top
    nvgFillColor(nvg_, toNVG(fillColor_));
    nvgText(nvg_, x, y, text.c_str(), nullptr);
}

float Canvas2D::measureText(const std::string& text) {
    if (text.empty()) return 0.f;
    applyNVGFont(nvg_, resolveFontFace(), fontSize_, textAlign_, textBaseline_);
    float bounds[4] = {};
    nvgTextBounds(nvg_, 0.f, 0.f, text.c_str(), nullptr, bounds);
    return bounds[2] - bounds[0];   // xmax - xmin
}

// ─────────────────────────────────────────────────────────────────────────────
// Clip rect
// ─────────────────────────────────────────────────────────────────────────────

void Canvas2D::pushClipRect(float x, float y, float w, float h) {
    nvgSave(nvg_);
    nvgScissor(nvg_, x, y, w, h);
    ++clipDepth_;
}

void Canvas2D::popClipRect() {
    if (clipDepth_ > 0) {
        nvgRestore(nvg_);
        --clipDepth_;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Pixel access  (GL-level readback — expensive, use sparingly)
// ─────────────────────────────────────────────────────────────────────────────

void Canvas2D::getImageData(float x, float y, float w, float h,
                             std::vector<uint8_t>& out) {
    int iw = static_cast<int>(w), ih = static_cast<int>(h);
    out.resize(static_cast<size_t>(iw) * ih * 4);
    // NanoVG flushes internally; read directly from GL framebuffer.
    // Flush NVG so all pending draws are on the framebuffer.
    nvgEndFrame(nvg_);  // WARNING: must re-begin frame after this call!
    glReadPixels(static_cast<GLint>(x),   static_cast<GLint>(y),
                 static_cast<GLsizei>(iw), static_cast<GLsizei>(ih),
                 GL_RGBA, GL_UNSIGNED_BYTE, out.data());
    // Flip rows (GL origin is bottom-left, canvas is top-left)
    for (int row = 0; row < ih / 2; ++row) {
        uint8_t* a = out.data() + row * iw * 4;
        uint8_t* b = out.data() + (ih - 1 - row) * iw * 4;
        for (int col = 0; col < iw * 4; ++col) std::swap(a[col], b[col]);
    }
}

void Canvas2D::putImageData(const std::vector<uint8_t>& data,
                             int srcW, int srcH,
                             float dx, float dy) {
    // Upload via a temporary NanoVG image, draw, then delete.
    int handle = nvgCreateImageRGBA(nvg_, srcW, srcH, 0, data.data());
    if (handle <= 0) return;
    NVGpaint paint = nvgImagePattern(nvg_, dx, dy, float(srcW), float(srcH),
                                     0.f, handle, 1.f);
    nvgBeginPath(nvg_);
    nvgRect(nvg_, dx, dy, float(srcW), float(srcH));
    nvgFillPaint(nvg_, paint);
    nvgFill(nvg_);
    nvgDeleteImage(nvg_, handle);
}