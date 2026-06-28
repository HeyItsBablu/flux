// flux_canvas2d_cairo.cpp
// Cairo backend for Canvas2D — replaces the OpenGL backend on Linux.
//
// Link: cairo  pangocairo  pango  glib
// stb_image is NOT included here — flux_image_linux.cpp owns STB_IMAGE_IMPLEMENTATION.
// Images are decoded via the stb target (stb_impl.cpp) already in the build.

#if defined(__linux__) && !defined(__ANDROID__)

#include "flux/flux_canvas2d.hpp"

#include <cairo/cairo.h>
#include <pango/pangocairo.h>

// stb_image — declaration only (implementation is in stb_impl.cpp / stb target)
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_ONLY_BMP
#define STBI_ONLY_TGA
#define STBI_ONLY_GIF
#include "stb_image.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ============================================================================
// Canvas2DBackend  — one per application, shared across all CanvasWidgets
// ============================================================================

struct Canvas2DBackend
{
    PangoFontMap *fontMap = nullptr;

    // Logical name → TTF path hint (fontconfig handles actual loading).
    std::unordered_map<std::string, std::string> fontRegistry;

    Canvas2DBackend()
    {
        fontMap = pango_cairo_font_map_get_default();
        // process singleton — do NOT unref
    }
    ~Canvas2DBackend() = default;
};

// ============================================================================
// Canvas2DImage — Cairo implementation
// ============================================================================

struct Canvas2DImageCairo : public Canvas2DImage
{
    cairo_surface_t     *surface = nullptr;
    std::vector<uint8_t> pixels; // backing store (premultiplied ARGB32)

    ~Canvas2DImageCairo() override
    {
        if (surface) { cairo_surface_destroy(surface); surface = nullptr; }
    }
};

// ============================================================================
// cairo_t* carrier
//
// Canvas2D::canvasGL_ is declared as Canvas2DGL* in the header but is never
// dereferenced on Linux — it exists as a void*-equivalent slot.  We reuse it
// to carry the active cairo_t* for the duration of each render pass without
// touching the shared header.
//
// Both getCr/setCr are defined here (same TU as all Canvas2D methods) so they
// can access the private member via the friend declaration added in the header
// patch, OR — simpler — we just define them as inline helpers that are only
// used inside this file, accessing the member through the Canvas2D_setCairo
// entry point which IS a friend.
// ============================================================================

// Forward declaration of the entry point used by CanvasWidget.
// The actual friend declaration lives in flux_canvas2d.hpp (Linux section).
void Canvas2D_setCairo(Canvas2D &ctx, cairo_t *cr);

// Internal accessor — defined inline so the compiler can inline through it.
static inline cairo_t *getCr(const Canvas2D *c)
{
    // canvasGL_ stores the cairo_t* while the frame is in progress.
    return reinterpret_cast<cairo_t *>(c->canvasGL_);
}

// ============================================================================
// Internal helpers
// ============================================================================

static inline void cairoSetColor(cairo_t *cr, Color c, float alpha = 1.f)
{
    cairo_set_source_rgba(cr,
                          c.r / 255.0,
                          c.g / 255.0,
                          c.b / 255.0,
                          (c.a / 255.0) * (double)alpha);
}

static void cairoRoundedRect(cairo_t *cr,
                              double x, double y, double w, double h, double r)
{
    r = std::min(r, std::min(w, h) * 0.5);
    cairo_new_path(cr);
    cairo_arc(cr, x + r,     y + r,     r, M_PI,       1.5 * M_PI);
    cairo_arc(cr, x + w - r, y + r,     r, 1.5 * M_PI, 2.0 * M_PI);
    cairo_arc(cr, x + w - r, y + h - r, r, 0.0,        0.5 * M_PI);
    cairo_arc(cr, x + r,     y + h - r, r, 0.5 * M_PI, M_PI);
    cairo_close_path(cr);
}

static cairo_line_cap_t toCairoCap(LineCap cap)
{
    switch (cap)
    {
    case LineCap::Round:  return CAIRO_LINE_CAP_ROUND;
    case LineCap::Square: return CAIRO_LINE_CAP_SQUARE;
    default:              return CAIRO_LINE_CAP_BUTT;
    }
}

static cairo_line_join_t toCairoJoin(LineJoin join)
{
    switch (join)
    {
    case LineJoin::Round: return CAIRO_LINE_JOIN_ROUND;
    case LineJoin::Bevel: return CAIRO_LINE_JOIN_BEVEL;
    default:              return CAIRO_LINE_JOIN_MITER;
    }
}

// stb RGBA → Cairo ARGB32 premultiplied
static void rgbaToPremulArgb(const unsigned char *src, int w, int h,
                              std::vector<uint8_t> &out)
{
    const size_t n = (size_t)w * (size_t)h;
    out.resize(n * 4);
    for (size_t i = 0; i < n; ++i)
    {
        uint8_t r = src[i*4+0], g = src[i*4+1],
                b = src[i*4+2], a = src[i*4+3];
        out[i*4+0] = (uint8_t)((uint32_t)b * a / 255u);
        out[i*4+1] = (uint8_t)((uint32_t)g * a / 255u);
        out[i*4+2] = (uint8_t)((uint32_t)r * a / 255u);
        out[i*4+3] = a;
    }
}

static cairo_surface_t *makeSurface(std::vector<uint8_t> &pixels, int w, int h)
{
    return cairo_image_surface_create_for_data(
        pixels.data(), CAIRO_FORMAT_ARGB32, w, h, w * 4);
}

// Apply fill source (solid colour or gradient)
static void applyFillSource(cairo_t *cr,
                             bool fillIsGrad,
                             Canvas2D::GradType gradType,
                             float gx0, float gy0, float gx1, float gy1,
                             float gcx, float gcy, float gInR, float gOutR,
                             const std::vector<std::pair<float,Color>> &stops,
                             Color fillColor, float globalAlpha)
{
    if (fillIsGrad && !stops.empty())
    {
        cairo_pattern_t *pat =
            (gradType == Canvas2D::GradType::Linear)
            ? cairo_pattern_create_linear(gx0, gy0, gx1, gy1)
            : cairo_pattern_create_radial(gcx, gcy, gInR, gcx, gcy, gOutR);

        for (auto &[t, c] : stops)
            cairo_pattern_add_color_stop_rgba(pat, t,
                c.r/255.0, c.g/255.0, c.b/255.0, (c.a/255.0)*globalAlpha);

        cairo_set_source(cr, pat);
        cairo_pattern_destroy(pat);
    }
    else
    {
        cairoSetColor(cr, fillColor, globalAlpha);
    }
}

// Pango layout helper
static PangoLayout *makePangoLayout(cairo_t *cr, const std::string &face,
                                     float sizePx, bool bold, bool italic)
{
    PangoLayout *layout = pango_cairo_create_layout(cr);
    PangoFontDescription *desc = pango_font_description_new();
    pango_font_description_set_family(desc, face.c_str());
    pango_font_description_set_size(desc, (int)(sizePx * PANGO_SCALE));
    pango_font_description_set_weight(desc,
        bold ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL);
    pango_font_description_set_style(desc,
        italic ? PANGO_STYLE_ITALIC : PANGO_STYLE_NORMAL);
    pango_layout_set_font_description(layout, desc);
    pango_font_description_free(desc);
    return layout;
}

// ============================================================================
// Canvas2D — constructor
// ============================================================================

Canvas2D::Canvas2D(Canvas2DBackend *backend, int canvasW, int canvasH)
    : backend_(backend), w_(canvasW), h_(canvasH)
{
    // canvasGL_ = nullptr by member initialiser — used as cairo_t* carrier
}


Canvas2DBackend* Canvas2DBackend_create() {
    return new Canvas2DBackend();
}
void Canvas2DBackend_destroy(Canvas2DBackend* b) {
    delete b;
}
// ============================================================================
// registerFont
// ============================================================================

bool Canvas2D::registerFont(Canvas2DBackend *backend,
                             const std::string &name,
                             const std::string &ttfPath)
{
    if (!backend) return false;
    backend->fontRegistry[name] = ttfPath;
    return true;
}

// ============================================================================
// Font helpers — no numeric indices on Cairo/Pango
// ============================================================================

int   Canvas2D::currentFontIdx() const { return 0; }
float Canvas2D::getKernAdvance(int, int, int, int) const { return 0.f; }

void Canvas2D::parseFontDesc(const std::string &desc)
{
    fontBold_ = false; fontItalic_ = false;
    fontSize_ = 14.f;  fontFace_   = "sans";

    std::string s = desc;
    auto consume = [&](const std::string &tok) -> bool {
        if (s.size() >= tok.size() && s.substr(0, tok.size()) == tok &&
            (s.size() == tok.size() || s[tok.size()] == ' '))
        {
            s = (s.size() > tok.size()) ? s.substr(tok.size()+1) : "";
            return true;
        }
        return false;
    };

    if (consume("bold"))   fontBold_   = true;
    if (consume("italic")) fontItalic_ = true;
    if (consume("bold"))   fontBold_   = true; // "italic bold"

    auto pxPos = s.find("px");
    if (pxPos != std::string::npos)
    {
        fontSize_ = std::stof(s.substr(0, pxPos));
        s = (pxPos + 3 <= s.size()) ? s.substr(pxPos + 3) : "";
    }
    if (!s.empty()) fontFace_ = s;
}

int Canvas2D::resolveFont() const { return 0; }

// ============================================================================
// Canvas2D_setCairo — injection point called by CanvasWidget each frame
// ============================================================================

void Canvas2D_setCairo(Canvas2D &ctx, cairo_t *cr)
{
    // Store cairo_t* in the canvasGL_ slot (void*-equivalent on Linux).
    ctx.canvasGL_ = reinterpret_cast<Canvas2DGL *>(cr); 
}

// ============================================================================
// State stack
// ============================================================================

void Canvas2D::save()
{
    cairo_t *cr = getCr(this);
    if (!cr) return;

    SaveState ss;
    ss.fillColor    = fillColor_;
    ss.strokeColor  = strokeColor_;
    ss.lineWidth    = lineWidth_;
    ss.globalAlpha  = globalAlpha_;
    ss.fillIsGrad   = fillIsGrad_;
    ss.clipDepth    = clipDepth_;
    ss.gradType     = gradType_;
    ss.gx0 = gx0_; ss.gy0 = gy0_; ss.gx1 = gx1_; ss.gy1 = gy1_;
    ss.gcx = gcx_; ss.gcy = gcy_; ss.gInR = gInR_; ss.gOutR = gOutR_;
    ss.stops        = gStops_;
    ss.fontFace     = fontFace_;
    ss.fontSize     = fontSize_;
    ss.fontBold     = fontBold_;
    ss.fontItalic   = fontItalic_;
    ss.textAlign    = textAlign_;
    ss.textBaseline = textBaseline_;
    ss.lineCap      = lineCap_;
    ss.lineJoin     = lineJoin_;
    stateStack_.push_back(std::move(ss));

    cairo_save(cr);
}

void Canvas2D::restore()
{
    cairo_t *cr = getCr(this);
    if (!cr || stateStack_.empty()) return;

    const SaveState &ss = stateStack_.back();
    fillColor_    = ss.fillColor;
    strokeColor_  = ss.strokeColor;
    lineWidth_    = ss.lineWidth;
    globalAlpha_  = ss.globalAlpha;
    fillIsGrad_   = ss.fillIsGrad;
    clipDepth_    = ss.clipDepth;
    gradType_     = ss.gradType;
    gx0_ = ss.gx0; gy0_ = ss.gy0; gx1_ = ss.gx1; gy1_ = ss.gy1;
    gcx_ = ss.gcx; gcy_ = ss.gcy; gInR_ = ss.gInR; gOutR_ = ss.gOutR;
    gStops_       = ss.stops;
    fontFace_     = ss.fontFace;
    fontSize_     = ss.fontSize;
    fontBold_     = ss.fontBold;
    fontItalic_   = ss.fontItalic;
    textAlign_    = ss.textAlign;
    textBaseline_ = ss.textBaseline;
    lineCap_      = ss.lineCap;
    lineJoin_     = ss.lineJoin;
    stateStack_.pop_back();

    cairo_restore(cr);
}

// ============================================================================
// Transform
// ============================================================================

void Canvas2D::translate(float dx, float dy)
{
    cairo_t *cr = getCr(this);
    if (cr) cairo_translate(cr, dx, dy);
}
void Canvas2D::scale(float sx, float sy)
{
    cairo_t *cr = getCr(this);
    if (cr) cairo_scale(cr, sx, sy);
}
void Canvas2D::rotate(float rad)
{
    cairo_t *cr = getCr(this);
    if (cr) cairo_rotate(cr, rad);
}
void Canvas2D::resetTransform()
{
    cairo_t *cr = getCr(this);
    if (!cr) return;
    cairo_matrix_t m; cairo_matrix_init_identity(&m);
    cairo_set_matrix(cr, &m);
}

// ============================================================================
// Style
// ============================================================================

void Canvas2D::setFillColor(Color c)   { fillColor_   = c; fillIsGrad_ = false; }
void Canvas2D::setStrokeColor(Color c) { strokeColor_ = c; }
void Canvas2D::setLineWidth(float w)   { lineWidth_   = w; }
void Canvas2D::setLineCap(LineCap c)   { lineCap_     = c; }
void Canvas2D::setLineJoin(LineJoin j) { lineJoin_    = j; }
void Canvas2D::setMiterLimit(float l)  { miterLimit_  = l; }
void Canvas2D::setGlobalAlpha(float a) { globalAlpha_ = std::max(0.f, std::min(1.f, a)); }
void Canvas2D::setFillRule(FillRule r) { fillRule_    = r; }

void Canvas2D::setCompositeOp(CompositeOp op)
{
    compositeOp_ = op;
    cairo_t *cr = getCr(this);
    if (!cr) return;
    switch (op)
    {
    case CompositeOp::Copy:     cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);   break;
    case CompositeOp::Xor:     cairo_set_operator(cr, CAIRO_OPERATOR_XOR);      break;
    case CompositeOp::Multiply: cairo_set_operator(cr, CAIRO_OPERATOR_MULTIPLY); break;
    case CompositeOp::Screen:   cairo_set_operator(cr, CAIRO_OPERATOR_SCREEN);   break;
    default:                    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);     break;
    }
}

// ============================================================================
// Gradient
// ============================================================================

void Canvas2D::beginLinearGradient(float x0, float y0, float x1, float y1)
{
    gradType_ = GradType::Linear;
    gx0_ = x0; gy0_ = y0; gx1_ = x1; gy1_ = y1;
    gStops_.clear();
}
void Canvas2D::beginRadialGradient(float cx, float cy, float innerR, float outerR)
{
    gradType_ = GradType::Radial;
    gcx_ = cx; gcy_ = cy; gInR_ = innerR; gOutR_ = outerR;
    gStops_.clear();
}
void Canvas2D::addColorStop(float t, Color c) { gStops_.push_back({t, c}); }
void Canvas2D::setFillGradient()              { fillIsGrad_ = true; }

// ============================================================================
// Primitives
// ============================================================================

void Canvas2D::clearRect(float x, float y, float w, float h)
{
    cairo_t *cr = getCr(this); if (!cr) return;
    cairo_save(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_rectangle(cr, x, y, w, h);
    cairo_fill(cr);
    cairo_restore(cr);
}

void Canvas2D::fillRect(float x, float y, float w, float h)
{
    cairo_t *cr = getCr(this); if (!cr) return;
    cairo_save(cr);
    applyFillSource(cr, fillIsGrad_, gradType_,
                    gx0_, gy0_, gx1_, gy1_, gcx_, gcy_, gInR_, gOutR_,
                    gStops_, fillColor_, globalAlpha_);
    cairo_rectangle(cr, x, y, w, h);
    cairo_fill(cr);
    cairo_restore(cr);
}

void Canvas2D::strokeRect(float x, float y, float w, float h)
{
    cairo_t *cr = getCr(this); if (!cr) return;
    cairo_save(cr);
    cairoSetColor(cr, strokeColor_, globalAlpha_);
    cairo_set_line_width(cr, lineWidth_);
    cairo_set_line_cap(cr, toCairoCap(lineCap_));
    cairo_set_line_join(cr, toCairoJoin(lineJoin_));
    cairo_set_miter_limit(cr, miterLimit_);
    cairo_rectangle(cr, x, y, w, h);
    cairo_stroke(cr);
    cairo_restore(cr);
}

void Canvas2D::fillRoundedRect(float x, float y, float w, float h, float r)
{
    cairo_t *cr = getCr(this); if (!cr) return;
    cairo_save(cr);
    applyFillSource(cr, fillIsGrad_, gradType_,
                    gx0_, gy0_, gx1_, gy1_, gcx_, gcy_, gInR_, gOutR_,
                    gStops_, fillColor_, globalAlpha_);
    cairoRoundedRect(cr, x, y, w, h, r);
    cairo_fill(cr);
    cairo_restore(cr);
}

void Canvas2D::strokeRoundedRect(float x, float y, float w, float h, float r)
{
    cairo_t *cr = getCr(this); if (!cr) return;
    cairo_save(cr);
    cairoSetColor(cr, strokeColor_, globalAlpha_);
    cairo_set_line_width(cr, lineWidth_);
    cairo_set_line_cap(cr, toCairoCap(lineCap_));
    cairo_set_line_join(cr, toCairoJoin(lineJoin_));
    cairoRoundedRect(cr, x, y, w, h, r);
    cairo_stroke(cr);
    cairo_restore(cr);
}

void Canvas2D::fillCircle(float cx, float cy, float r)
{
    cairo_t *cr = getCr(this); if (!cr) return;
    cairo_save(cr);
    applyFillSource(cr, fillIsGrad_, gradType_,
                    gx0_, gy0_, gx1_, gy1_, gcx_, gcy_, gInR_, gOutR_,
                    gStops_, fillColor_, globalAlpha_);
    cairo_arc(cr, cx, cy, r, 0.0, 2.0 * M_PI);
    cairo_fill(cr);
    cairo_restore(cr);
}

void Canvas2D::strokeCircle(float cx, float cy, float r)
{
    cairo_t *cr = getCr(this); if (!cr) return;
    cairo_save(cr);
    cairoSetColor(cr, strokeColor_, globalAlpha_);
    cairo_set_line_width(cr, lineWidth_);
    cairo_arc(cr, cx, cy, r, 0.0, 2.0 * M_PI);
    cairo_stroke(cr);
    cairo_restore(cr);
}

// ============================================================================
// Path API
// ============================================================================

void Canvas2D::beginPath()
{
    cairo_t *cr = getCr(this);
    if (cr) cairo_new_path(cr);
    path_.clear();
}
void Canvas2D::closePath()
{
    cairo_t *cr = getCr(this);
    if (cr) cairo_close_path(cr);
}
void Canvas2D::moveTo(float x, float y)
{
    cairo_t *cr = getCr(this); if (!cr) return;
    cairo_move_to(cr, x, y);
    curX_ = x; curY_ = y; pathStartX_ = x; pathStartY_ = y;
}
void Canvas2D::lineTo(float x, float y)
{
    cairo_t *cr = getCr(this); if (!cr) return;
    cairo_line_to(cr, x, y);
    curX_ = x; curY_ = y;
}

void Canvas2D::arc(float cx, float cy, float radius,
                   float startAngle, float endAngle, bool anticlockwise)
{
    cairo_t *cr = getCr(this); if (!cr) return;
    if (anticlockwise)
        cairo_arc_negative(cr, cx, cy, radius, startAngle, endAngle);
    else
        cairo_arc(cr, cx, cy, radius, startAngle, endAngle);
    curX_ = cx + radius * std::cos(endAngle);
    curY_ = cy + radius * std::sin(endAngle);
}

void Canvas2D::arcTo(float x1, float y1, float x2, float y2, float radius)
{
    cairo_t *cr = getCr(this); if (!cr) return;
    double x0 = curX_, y0 = curY_;
    double dx0 = x0-x1, dy0 = y0-y1, dx1 = x2-x1, dy1 = y2-y1;
    double len0 = std::sqrt(dx0*dx0+dy0*dy0);
    double len1 = std::sqrt(dx1*dx1+dy1*dy1);
    if (len0 < 1e-6 || len1 < 1e-6) { cairo_line_to(cr, x1, y1); return; }
    double cos_a = std::max(-1.0, std::min(1.0,
        (dx0*dx1+dy0*dy1)/(len0*len1)));
    double angle = std::acos(cos_a);
    if (std::abs(angle) < 1e-6) { cairo_line_to(cr, x1, y1); return; }
    double tan_half = std::tan(angle*0.5);
    if (std::abs(tan_half) < 1e-6) { cairo_line_to(cr, x1, y1); return; }
    double d = radius/tan_half;
    double t0x = x1+dx0/len0*d, t0y = y1+dy0/len0*d;
    cairo_line_to(cr, t0x, t0y);
    double nx = dx0/len0+dx1/len1, ny = dy0/len0+dy1/len1;
    double nlen = std::sqrt(nx*nx+ny*ny);
    if (nlen < 1e-6) return;
    double arcCx = x1+(nx/nlen)*radius/std::sin(angle*0.5);
    double arcCy = y1+(ny/nlen)*radius/std::sin(angle*0.5);
    double a0 = std::atan2(t0y-arcCy, t0x-arcCx);
    double t1x = x1+dx1/len1*d, t1y = y1+dy1/len1*d;
    double a1 = std::atan2(t1y-arcCy, t1x-arcCx);
    double cross = dx0*dy1-dy0*dx1;
    if (cross > 0) cairo_arc(cr, arcCx, arcCy, radius, a0, a1);
    else           cairo_arc_negative(cr, arcCx, arcCy, radius, a0, a1);
    curX_ = (float)t1x; curY_ = (float)t1y;
}

void Canvas2D::quadraticCurveTo(float cpx, float cpy, float x, float y)
{
    cairo_t *cr = getCr(this); if (!cr) return;
    double p0x = curX_, p0y = curY_;
    cairo_curve_to(cr,
        p0x + 2.0/3.0*(cpx-p0x), p0y + 2.0/3.0*(cpy-p0y),
        x   + 2.0/3.0*(cpx-x),   y   + 2.0/3.0*(cpy-y),
        x, y);
    curX_ = x; curY_ = y;
}

void Canvas2D::bezierCurveTo(float cp1x, float cp1y,
                              float cp2x, float cp2y, float x, float y)
{
    cairo_t *cr = getCr(this); if (!cr) return;
    cairo_curve_to(cr, cp1x, cp1y, cp2x, cp2y, x, y);
    curX_ = x; curY_ = y;
}

void Canvas2D::rect(float x, float y, float w, float h)
{
    cairo_t *cr = getCr(this); if (!cr) return;
    cairo_rectangle(cr, x, y, w, h);
    curX_ = x; curY_ = y;
}

void Canvas2D::ellipse(float cx, float cy, float rx, float ry,
                        float rotation, float startAngle, float endAngle,
                        bool anticlockwise)
{
    cairo_t *cr = getCr(this); if (!cr) return;
    cairo_save(cr);
    cairo_translate(cr, cx, cy);
    cairo_rotate(cr, rotation);
    cairo_scale(cr, rx, ry);
    if (anticlockwise) cairo_arc_negative(cr, 0, 0, 1.0, startAngle, endAngle);
    else               cairo_arc(cr, 0, 0, 1.0, startAngle, endAngle);
    cairo_restore(cr);
}

void Canvas2D::fill()
{
    cairo_t *cr = getCr(this); if (!cr) return;
    cairo_save(cr);
    applyFillSource(cr, fillIsGrad_, gradType_,
                    gx0_, gy0_, gx1_, gy1_, gcx_, gcy_, gInR_, gOutR_,
                    gStops_, fillColor_, globalAlpha_);
    cairo_set_fill_rule(cr, fillRule_ == FillRule::EvenOdd
                            ? CAIRO_FILL_RULE_EVEN_ODD
                            : CAIRO_FILL_RULE_WINDING);
    cairo_fill_preserve(cr);
    cairo_restore(cr);
}

void Canvas2D::stroke()
{
    cairo_t *cr = getCr(this); if (!cr) return;
    cairo_save(cr);
    cairoSetColor(cr, strokeColor_, globalAlpha_);
    cairo_set_line_width(cr, lineWidth_);
    cairo_set_line_cap(cr, toCairoCap(lineCap_));
    cairo_set_line_join(cr, toCairoJoin(lineJoin_));
    cairo_set_miter_limit(cr, miterLimit_);
    cairo_stroke_preserve(cr);
    cairo_restore(cr);
}

void Canvas2D::clip()
{
    cairo_t *cr = getCr(this); if (!cr) return;
    cairo_set_fill_rule(cr, fillRule_ == FillRule::EvenOdd
                            ? CAIRO_FILL_RULE_EVEN_ODD
                            : CAIRO_FILL_RULE_WINDING);
    cairo_clip(cr);
    ++clipDepth_;
}

// ============================================================================
// Image
// ============================================================================

Canvas2DImage *Canvas2D::loadImage(const std::string &path)
{
    int w = 0, h = 0, ch = 0;
    unsigned char *raw = stbi_load(path.c_str(), &w, &h, &ch, 4);
    if (!raw) return nullptr;
    auto *img = new Canvas2DImageCairo();
    img->width = w; img->height = h;
    rgbaToPremulArgb(raw, w, h, img->pixels);
    stbi_image_free(raw);
    img->surface = makeSurface(img->pixels, w, h);
    if (cairo_surface_status(img->surface) != CAIRO_STATUS_SUCCESS)
        { delete img; return nullptr; }
    return img;
}

Canvas2DImage *Canvas2D::loadImageFromMemory(const unsigned char *data, int byteLen)
{
    int w = 0, h = 0, ch = 0;
    unsigned char *raw = stbi_load_from_memory(data, byteLen, &w, &h, &ch, 4);
    if (!raw) return nullptr;
    auto *img = new Canvas2DImageCairo();
    img->width = w; img->height = h;
    rgbaToPremulArgb(raw, w, h, img->pixels);
    stbi_image_free(raw);
    img->surface = makeSurface(img->pixels, w, h);
    if (cairo_surface_status(img->surface) != CAIRO_STATUS_SUCCESS)
        { delete img; return nullptr; }
    return img;
}

void Canvas2D::updateImage(Canvas2DImage *img,
                            const unsigned char *rgba, int w, int h)
{
    if (!img) return;
    auto *ci = static_cast<Canvas2DImageCairo *>(img);
    if (ci->surface) { cairo_surface_destroy(ci->surface); ci->surface = nullptr; }
    ci->width = w; ci->height = h;
    rgbaToPremulArgb(rgba, w, h, ci->pixels);
    ci->surface = makeSurface(ci->pixels, w, h);
}

void Canvas2D::freeImage(Canvas2DImage *img) { delete img; }

void Canvas2D::drawImage(const Canvas2DImage *img, float dx, float dy)
{
    if (!img) return;
    drawImage(img, dx, dy, (float)img->width, (float)img->height);
}

void Canvas2D::drawImage(const Canvas2DImage *img,
                          float dx, float dy, float dw, float dh)
{
    if (!img) return;
    drawImage(img, 0, 0, (float)img->width, (float)img->height, dx, dy, dw, dh);
}

void Canvas2D::drawImage(const Canvas2DImage *img,
                          float sx, float sy, float sw, float sh,
                          float dx, float dy, float dw, float dh)
{
    if (!img || img->width <= 0 || img->height <= 0) return;
    const auto *ci = static_cast<const Canvas2DImageCairo *>(img);
    if (!ci->surface) return;
    cairo_t *cr = getCr(this); if (!cr) return;
    cairo_save(cr);
    cairo_rectangle(cr, dx, dy, dw, dh);
    cairo_clip(cr);
    double scaleX = dw/sw, scaleY = dh/sh;
    cairo_translate(cr, dx - sx*scaleX, dy - sy*scaleY);
    cairo_scale(cr, scaleX, scaleY);
    cairo_set_source_surface(cr, ci->surface, 0.0, 0.0);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BILINEAR);
    cairo_paint_with_alpha(cr, globalAlpha_);
    cairo_restore(cr);
}

// ============================================================================
// Text
// ============================================================================

void Canvas2D::setFont(const std::string &fontDesc) { parseFontDesc(fontDesc); }
void Canvas2D::setTextAlign(CanvasTextAlign a)       { textAlign_    = a; }
void Canvas2D::setTextBaseline(TextBaseline b)       { textBaseline_ = b; }

void Canvas2D::fillText(const std::string &text, float x, float y, float maxWidth)
{
    cairo_t *cr = getCr(this); if (!cr || text.empty()) return;
    cairo_save(cr);
    applyFillSource(cr, fillIsGrad_, gradType_,
                    gx0_, gy0_, gx1_, gy1_, gcx_, gcy_, gInR_, gOutR_,
                    gStops_, fillColor_, globalAlpha_);
    PangoLayout *layout = makePangoLayout(cr, fontFace_, fontSize_,
                                           fontBold_, fontItalic_);
    pango_layout_set_text(layout, text.c_str(), -1);
    if (maxWidth > 0)
        pango_layout_set_width(layout, (int)(maxWidth * PANGO_SCALE));

    int tw = 0, th = 0;
    pango_layout_get_pixel_size(layout, &tw, &th);
    double drawX = x, drawY = y;

    switch (textAlign_)
    {
    case CanvasTextAlign::Center: drawX -= tw * 0.5; break;
    case CanvasTextAlign::Right:  drawX -= tw;       break;
    default: break;
    }

    switch (textBaseline_)
    {
    case TextBaseline::Top:    drawY = y;            break;
    case TextBaseline::Middle: drawY = y - th * 0.5; break;
    case TextBaseline::Bottom: drawY = y - th;       break;
    case TextBaseline::Alphabetic:
    default:
        {
            PangoContext    *pctx    = pango_layout_get_context(layout);
            PangoFontMetrics *metrics = pango_context_get_metrics(pctx,
                pango_layout_get_font_description(layout), nullptr);
            int ascent = pango_font_metrics_get_ascent(metrics) / PANGO_SCALE;
            pango_font_metrics_unref(metrics);
            drawY = y - ascent;
        }
        break;
    }

    cairo_move_to(cr, drawX, drawY);
    pango_cairo_show_layout(cr, layout);
    g_object_unref(layout);
    cairo_restore(cr);
}

void Canvas2D::strokeText(const std::string &text, float x, float y, float maxWidth)
{
    cairo_t *cr = getCr(this); if (!cr || text.empty()) return;
    cairo_save(cr);
    cairoSetColor(cr, strokeColor_, globalAlpha_);
    cairo_set_line_width(cr, lineWidth_);
    PangoLayout *layout = makePangoLayout(cr, fontFace_, fontSize_,
                                           fontBold_, fontItalic_);
    pango_layout_set_text(layout, text.c_str(), -1);
    if (maxWidth > 0)
        pango_layout_set_width(layout, (int)(maxWidth * PANGO_SCALE));
    int tw = 0, th = 0;
    pango_layout_get_pixel_size(layout, &tw, &th);
    double drawX = x, drawY = y;
    switch (textAlign_)
    {
    case CanvasTextAlign::Center: drawX -= tw * 0.5; break;
    case CanvasTextAlign::Right:  drawX -= tw;       break;
    default: break;
    }
    switch (textBaseline_)
    {
    case TextBaseline::Top:    drawY = y;            break;
    case TextBaseline::Middle: drawY = y - th * 0.5; break;
    case TextBaseline::Bottom: drawY = y - th;       break;
    default:                   drawY = y - th * 0.8; break;
    }
    cairo_move_to(cr, drawX, drawY);
    pango_cairo_layout_path(cr, layout);
    cairo_stroke(cr);
    g_object_unref(layout);
    cairo_restore(cr);
}

float Canvas2D::measureText(const std::string &text)
{
    cairo_t *cr = getCr(this); if (!cr || text.empty()) return 0.f;
    PangoLayout *layout = makePangoLayout(cr, fontFace_, fontSize_,
                                           fontBold_, fontItalic_);
    pango_layout_set_text(layout, text.c_str(), -1);
    int tw = 0, th = 0;
    pango_layout_get_pixel_size(layout, &tw, &th);
    g_object_unref(layout);
    return (float)tw;
}

// ============================================================================
// Clip rect
// ============================================================================

void Canvas2D::pushClipRect(float x, float y, float w, float h)
{
    cairo_t *cr = getCr(this); if (!cr) return;
    cairo_save(cr);
    cairo_rectangle(cr, x, y, w, h);
    cairo_clip(cr);
    ++clipDepth_;
}

void Canvas2D::popClipRect()
{
    cairo_t *cr = getCr(this);
    if (!cr || clipDepth_ <= 0) return;
    cairo_restore(cr);
    --clipDepth_;
}

// ============================================================================
// Pixel access
// ============================================================================

void Canvas2D::getImageData(float x, float y, float w, float h,
                             std::vector<uint8_t> &out)
{
    cairo_t *cr = getCr(this); if (!cr) return;
    cairo_surface_t *target = cairo_get_target(cr);
    cairo_surface_flush(target);
    unsigned char *data = cairo_image_surface_get_data(target);
    if (!data) return;
    int stride = cairo_image_surface_get_stride(target);
    int iw = (int)w, ih = (int)h, ix = (int)x, iy = (int)y;
    out.resize((size_t)iw * ih * 4);
    for (int row = 0; row < ih; ++row)
    {
        const uint8_t *src = data + (iy+row)*stride + ix*4;
        uint8_t       *dst = out.data() + (size_t)row*iw*4;
        for (int col = 0; col < iw; ++col)
        {
            uint8_t a = src[3];
            dst[0] = a ? (uint8_t)((uint32_t)src[2]*255u/a) : 0; // R
            dst[1] = a ? (uint8_t)((uint32_t)src[1]*255u/a) : 0; // G
            dst[2] = a ? (uint8_t)((uint32_t)src[0]*255u/a) : 0; // B
            dst[3] = a;
            src += 4; dst += 4;
        }
    }
}

void Canvas2D::putImageData(const std::vector<uint8_t> &rgbaData,
                             int srcW, int srcH, float dx, float dy)
{
    if (rgbaData.size() < (size_t)srcW*srcH*4) return;
    std::vector<uint8_t> premul;
    rgbaToPremulArgb(rgbaData.data(), srcW, srcH, premul);
    cairo_surface_t *surf = cairo_image_surface_create_for_data(
        premul.data(), CAIRO_FORMAT_ARGB32, srcW, srcH, srcW*4);
    if (cairo_surface_status(surf) != CAIRO_STATUS_SUCCESS)
        { cairo_surface_destroy(surf); return; }
    cairo_t *cr = getCr(this);
    if (cr)
    {
        cairo_save(cr);
        cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
        cairo_set_source_surface(cr, surf, dx, dy);
        cairo_rectangle(cr, dx, dy, srcW, srcH);
        cairo_fill(cr);
        cairo_restore(cr); 
    }
    cairo_surface_destroy(surf);
}

#endif // __linux__ && !__ANDROID__