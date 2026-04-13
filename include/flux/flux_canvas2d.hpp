#pragma once
// ============================================================================
// flux_canvas2d.hpp
// HTML5-style 2D drawing API for RenderSurface::render().
//
// Backend  : NanoVG
// Platforms: Windows  (GL3, GLAD, WGL)
//            Linux    (GL3, GLAD, GLX/EGL)
//            Android  (GLES2/3, EGL)   — swap NANOVG_GL3 → NANOVG_GLES2/3
//            macOS    (GL3 or Metal)   — future
//
// Callers only need this header + flux_core.hpp.
// The NanoVG implementation lives entirely in flux_canvas2d_nvg.cpp.
// ============================================================================

#include "flux_core.hpp"   // Color  { uint8_t r,g,b,a; }
#include <string>
#include <vector>

// Forward-declare NVGcontext so callers never need to include nanovg.h.
struct NVGcontext;

// ─────────────────────────────────────────────────────────────────────────────
// Canvas2DImage
// Returned by Canvas2D::loadImage().
// Lifetime is tied to the NVGcontext that created it.
// Free before destroying the GL context — CanvasWidget::destroyGL() does this.
// ─────────────────────────────────────────────────────────────────────────────
struct Canvas2DImage {
    int nvgHandle = -1;   // NanoVG image id  (-1 = invalid)
    int width     = 0;
    int height    = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// Enums
// ─────────────────────────────────────────────────────────────────────────────
enum class TextAlign    { Left, Right, Center };
enum class TextBaseline { Top, Middle, Bottom, Alphabetic };
enum class LineCap      { Butt, Round, Square };
enum class LineJoin     { Miter, Round, Bevel };
enum class CompositeOp  { SourceOver, Copy, Xor, Multiply, Screen };
enum class FillRule     { NonZero, EvenOdd };

// ─────────────────────────────────────────────────────────────────────────────
// Canvas2D
//
// One instance is stack-allocated by CanvasWidget each frame, between
// nvgBeginFrame / nvgEndFrame, then passed to RenderSurface::render().
// Do NOT construct one yourself.
// ─────────────────────────────────────────────────────────────────────────────
struct Canvas2D {

    // Constructed by CanvasWidget only.
    explicit Canvas2D(NVGcontext* nvg, int canvasW, int canvasH);
    ~Canvas2D() = default;

    Canvas2D(const Canvas2D&)            = delete;
    Canvas2D& operator=(const Canvas2D&) = delete;

    // ── Dimensions ────────────────────────────────────────────────────────────
    int width()  const { return canvasW_; }
    int height() const { return canvasH_; }

    // Escape hatch — raw NanoVG access if you need something not exposed here.
    NVGcontext* nvg() const { return nvg_; }

    // ── State stack (save / restore) ──────────────────────────────────────────
    void save();
    void restore();

    // ── Transform ─────────────────────────────────────────────────────────────
    void translate(float dx, float dy);
    void scale(float sx, float sy);
    void rotate(float radians);       // clockwise, matches HTML canvas
    void resetTransform();

    // ── Style ─────────────────────────────────────────────────────────────────
    void setFillColor(Color c);
    void setStrokeColor(Color c);
    void setLineWidth(float w);
    void setLineCap(LineCap cap);
    void setLineJoin(LineJoin join);
    void setMiterLimit(float limit);
    void setGlobalAlpha(float a);     // 0..1, multiplied into all subsequent draws
    void setCompositeOp(CompositeOp op);
    void setFillRule(FillRule rule);

    // ── Linear gradient ───────────────────────────────────────────────────────
    // 1.  beginLinearGradient(x0,y0, x1,y1);
    // 2.  addColorStop(t, color);     // t in [0,1], add as many as needed
    // 3.  setFillGradient();          // activates gradient as fill paint
    // 4.  fillRect() / fill() / etc.
    void beginLinearGradient(float x0, float y0, float x1, float y1);
    void addColorStop(float t, Color c);
    void setFillGradient();

    // ── Radial gradient ───────────────────────────────────────────────────────
    // Same workflow — call beginRadialGradient instead of beginLinearGradient.
    void beginRadialGradient(float cx, float cy, float innerR, float outerR);

    // ── Primitive drawing ─────────────────────────────────────────────────────
    void clearRect(float x, float y, float w, float h);
    void fillRect(float x, float y, float w, float h);
    void strokeRect(float x, float y, float w, float h);
    void fillRoundedRect(float x, float y, float w, float h, float r);
    void strokeRoundedRect(float x, float y, float w, float h, float r);
    void fillCircle(float cx, float cy, float r);
    void strokeCircle(float cx, float cy, float r);

    // ── Path API ──────────────────────────────────────────────────────────────
    void beginPath();
    void closePath();
    void moveTo(float x, float y);
    void lineTo(float x, float y);
    void arc(float cx, float cy, float radius,
             float startAngle, float endAngle, bool anticlockwise = false);
    void arcTo(float x1, float y1, float x2, float y2, float radius);
    void quadraticCurveTo(float cpx, float cpy, float x, float y);
    void bezierCurveTo(float cp1x, float cp1y,
                       float cp2x, float cp2y,
                       float x,    float y);
    void rect(float x, float y, float w, float h);
    void ellipse(float cx, float cy, float rx, float ry,
                 float rotation     = 0.f,
                 float startAngle   = 0.f,
                 float endAngle     = 6.28318530f,
                 bool  anticlockwise = false);

    void fill();
    void stroke();
    void clip();   // clip subsequent draws to current path (intersects existing clip)

    // ── Image drawing ─────────────────────────────────────────────────────────
    // loadImage / freeImage: call from initialize() or update(), NOT from render().
    Canvas2DImage* loadImage(const std::string& path);
    Canvas2DImage* loadImageFromMemory(const unsigned char* data, int byteLen);
    void           freeImage(Canvas2DImage* img);

    // drawImage variants (mirrors HTML canvas API)
    void drawImage(const Canvas2DImage* img, float dx, float dy);
    void drawImage(const Canvas2DImage* img,
                   float dx, float dy, float dw, float dh);
    void drawImage(const Canvas2DImage* img,
                   float sx, float sy, float sw, float sh,    // source (pixels)
                   float dx, float dy, float dw, float dh);   // dest (canvas units)

    // ── Text ──────────────────────────────────────────────────────────────────
    // fontDesc examples:
    //   "14px sans"        "bold 18px Segoe UI"      "italic 12px Consolas"
    //
    // Font faces must be registered once via Canvas2D::registerFont() before use.
    // CanvasWidget registers "sans", "sans-bold", "mono" from system paths on init.
    static bool registerFont(NVGcontext* nvg,
                             const std::string& name,
                             const std::string& ttfPath);

    void  setFont(const std::string& fontDesc);
    void  setTextAlign(TextAlign align);
    void  setTextBaseline(TextBaseline baseline);
    void  fillText(const std::string& text, float x, float y,
                   float maxWidth = -1.f);
    void  strokeText(const std::string& text, float x, float y,
                     float maxWidth = -1.f);
    float measureText(const std::string& text);

    // ── Clip rect helpers ─────────────────────────────────────────────────────
    // Simple scissor-style clip. Pairs must match.
    void pushClipRect(float x, float y, float w, float h);
    void popClipRect();

    // ── Pixel access ──────────────────────────────────────────────────────────
    // Both are GPU-stalling. Use sparingly.
    void getImageData(float x, float y, float w, float h,
                      std::vector<uint8_t>& out);   // RGBA, row-major
    void putImageData(const std::vector<uint8_t>& data,
                      int srcW, int srcH,
                      float dx, float dy);

    // ── Internal (CanvasWidget use only) ──────────────────────────────────────
    NVGcontext* nvg_     = nullptr;
    int         canvasW_ = 0;
    int         canvasH_ = 0;

private:
    // ── Gradient build state ──────────────────────────────────────────────────
    enum class GradType { None, Linear, Radial };
    struct GStop { float t; Color c; };

    GradType           gradType_ = GradType::None;
    float              gx0_=0,gy0_=0, gx1_=0,gy1_=0;    // linear
    float              gcx_=0,gcy_=0, gInR_=0,gOutR_=0;  // radial
    std::vector<GStop> gStops_;
    bool               fillIsGrad_ = false;

    // ── Text state ────────────────────────────────────────────────────────────
    std::string  fontFace_     = "sans";
    float        fontSize_     = 14.f;
    bool         fontBold_     = false; 
    bool         fontItalic_   = false;
    TextAlign    textAlign_    = TextAlign::Left;
    TextBaseline textBaseline_ = TextBaseline::Alphabetic;

    // ── Draw state ────────────────────────────────────────────────────────────
    float    lineWidth_   = 1.f;
    float    globalAlpha_ = 1.f;
    Color    fillColor_   = {0,0,0,255};
    Color    strokeColor_ = {0,0,0,255};
    FillRule fillRule_    = FillRule::NonZero;
    int      clipDepth_   = 0;

    // ── Helpers ───────────────────────────────────────────────────────────────
    void        applyFill();
    void        applyStroke();
    void        parseFontDesc(const std::string& desc);
    std::string resolveFontFace() const;
};