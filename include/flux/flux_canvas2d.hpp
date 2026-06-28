#pragma once
// ============================================================================
// flux_canvas2d.hpp  —  Unified HTML5-style 2-D drawing API
//
// Single public header for all platforms.  Backend internals (GL, D2D, Metal)
// are hidden behind the opaque Canvas2DBackend pointer.
//
// Platform implementations:
//   flux_canvas2d_gl.cpp    — OpenGL / GLES3  (Linux, Web, Android, macOS)
//   flux_canvas2d_d2d.cpp   — Direct2D        (Win32)
//
// Usage (identical on every platform):
//
//   // In CanvasWidget per-frame render:
//   backend_->mvp = <computed ortho * viewport>;   // GL only; D2D ignores
//   Canvas2D ctx(backend_, canvasW, canvasH);
//   surface->render(ctx);
//
// Thread-safety: all methods must be called on the render thread only.
// ============================================================================

#include "flux_platform.hpp"


#include <string>
#include <vector>
#include <cstdint>

// ─────────────────────────────────────────────────────────────────────────────
// Forward declarations
// ─────────────────────────────────────────────────────────────────────────────

// Opaque backend — defined privately in each platform's .cpp.
// RenderSurface authors never touch this.
struct Canvas2DBackend;
struct Canvas2DGL;

// ─────────────────────────────────────────────────────────────────────────────
// Canvas2DImage  — opaque image handle
//
// Returned by Canvas2D::loadImage / loadImageFromMemory.
// The concrete subtype (Canvas2DImageGL / Canvas2DImageD2D) is defined in
// each backend's .cpp and carries the platform resource (GL texture id /
// D2D bitmap ComPtr).  Always free via Canvas2D::freeImage().
// ─────────────────────────────────────────────────────────────────────────────
struct Canvas2DImage
{
    virtual ~Canvas2DImage() = default;
    int width = 0;
    int height = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// Enums  (identical across all backends)
// ─────────────────────────────────────────────────────────────────────────────
enum class CanvasTextAlign
{
    Left,
    Right,
    Center
};
enum class TextBaseline
{
    Top,
    Middle,
    Bottom,
    Alphabetic
};
enum class LineCap
{
    Butt,
    Round,
    Square
};
enum class LineJoin
{
    Miter,
    Round,
    Bevel
};
enum class CompositeOp
{
    SourceOver,
    Copy,
    Xor,
    Multiply,
    Screen
};
enum class FillRule
{
    NonZero,
    EvenOdd
};

// ─────────────────────────────────────────────────────────────────────────────
// Canvas2D  — main drawing API
//
// One instance is stack-allocated per frame by CanvasWidget and passed to
// RenderSurface::render().  Do NOT construct one yourself.
// ─────────────────────────────────────────────────────────────────────────────
struct Canvas2D
{
    // -------------------------------------------------------------------------
    // Construction — called by CanvasWidget only.
    //
    // On GL platforms, set backend->mvp before calling this constructor.
    // On D2D, the MVP is not used (D2D manages its own transform stack).
    // -------------------------------------------------------------------------
    explicit Canvas2D(Canvas2DBackend *backend, int canvasW, int canvasH);
    ~Canvas2D() = default;

    Canvas2D(const Canvas2D &) = delete;
    Canvas2D &operator=(const Canvas2D &) = delete;

    // ── Dimensions ────────────────────────────────────────────────────────
    int width() const { return w_; }
    int height() const { return h_; }

    // ── State stack ───────────────────────────────────────────────────────
    void save();
    void restore();

    // ── Transform  (applied on top of the base MVP / D2D transform) ──────
    void translate(float dx, float dy);
    void scale(float sx, float sy);
    void rotate(float radians);
    void resetTransform();

    // ── Style ─────────────────────────────────────────────────────────────
    void setFillColor(Color c);
    void setStrokeColor(Color c);
    void setLineWidth(float w);
    void setLineCap(LineCap cap);
    void setLineJoin(LineJoin join);
    void setMiterLimit(float limit);
    void setGlobalAlpha(float a);
    void setCompositeOp(CompositeOp op);
    void setFillRule(FillRule rule);

    // ── Gradient ──────────────────────────────────────────────────────────
    void beginLinearGradient(float x0, float y0, float x1, float y1);
    void beginRadialGradient(float cx, float cy, float innerR, float outerR);
    void addColorStop(float t, Color c);
    void setFillGradient();

    // ── Primitives ────────────────────────────────────────────────────────
    void clearRect(float x, float y, float w, float h);
    void fillRect(float x, float y, float w, float h);
    void strokeRect(float x, float y, float w, float h);
    void fillRoundedRect(float x, float y, float w, float h, float r);
    void strokeRoundedRect(float x, float y, float w, float h, float r);
    void fillCircle(float cx, float cy, float r);
    void strokeCircle(float cx, float cy, float r);

    // ── Path API ──────────────────────────────────────────────────────────
    void beginPath();
    void closePath();
    void moveTo(float x, float y);
    void lineTo(float x, float y);
    void arc(float cx, float cy, float radius,
             float startAngle, float endAngle, bool anticlockwise = false);
    void arcTo(float x1, float y1, float x2, float y2, float radius);
    void quadraticCurveTo(float cpx, float cpy, float x, float y);
    void bezierCurveTo(float cp1x, float cp1y,
                       float cp2x, float cp2y, float x, float y);
    void rect(float x, float y, float w, float h);
    void ellipse(float cx, float cy, float rx, float ry,
                 float rotation = 0.f,
                 float startAngle = 0.f,
                 float endAngle = 6.28318530f,
                 bool anticlockwise = false);
    void fill();
    void stroke();
    void clip();

    // ── Image ─────────────────────────────────────────────────────────────
    // Font registration — call once at startup (e.g. from CanvasWidget init).
    // backend must match the one used to construct Canvas2D.
    static bool registerFont(Canvas2DBackend *backend,
                             const std::string &name,
                             const std::string &ttfPath);

    Canvas2DImage *loadImage(const std::string &path);
    Canvas2DImage *loadImageFromMemory(const unsigned char *data, int byteLen);
    void updateImage(Canvas2DImage *img,
                     const unsigned char *rgba, int w, int h);
    void freeImage(Canvas2DImage *img);

    void drawImage(const Canvas2DImage *img, float dx, float dy);
    void drawImage(const Canvas2DImage *img,
                   float dx, float dy, float dw, float dh);
    void drawImage(const Canvas2DImage *img,
                   float sx, float sy, float sw, float sh,
                   float dx, float dy, float dw, float dh);

    // ── Text ──────────────────────────────────────────────────────────────
    void setFont(const std::string &fontDesc); // e.g. "bold 14px sans"
    void setTextAlign(CanvasTextAlign align);
    void setTextBaseline(TextBaseline baseline);
    void fillText(const std::string &text, float x, float y,
                  float maxWidth = -1.f);
    void strokeText(const std::string &text, float x, float y,
                    float maxWidth = -1.f);
    float measureText(const std::string &text);

    // ── Clip rect ─────────────────────────────────────────────────────────
    void pushClipRect(float x, float y, float w, float h);
    void popClipRect();

    // ── Pixel access ──────────────────────────────────────────────────────
    void getImageData(float x, float y, float w, float h,
                      std::vector<uint8_t> &out);
    void putImageData(const std::vector<uint8_t> &data,
                      int srcW, int srcH, float dx, float dy);

    // ── Font info (used internally by text-layout helpers) ────────────────
    float currentFontSize() const { return fontSize_; }
    int currentFontIdx() const;
    float getKernAdvance(int fontIdx, int cp1, int cp2, int pixelSize) const;

    // ── Backend access (CanvasWidget internals only) ───────────────────────
    Canvas2DBackend *backend() const { return backend_; }

    // ── Gradient type (public so free functions in cairo backend can use it)
    enum class GradType
    {
        None,
        Linear,
        Radial
    };

#if defined(__ANDROID__) || defined(__EMSCRIPTEN__)
    // GL backend pointer — Android/Web only.
    Canvas2DGL *canvasGL_ = nullptr;

    // 3×3 column-major transform matrix — GL only.
    // Cairo and D2D manage their own transform stacks natively.
    struct Mat3
    {
        float m[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
        static Mat3 identity();
        Mat3 multiply(const Mat3 &b) const;
        Mat3 translated(float dx, float dy) const;
        Mat3 scaled(float sx, float sy) const;
        Mat3 rotated(float angle) const;
        void apply(float &x, float &y) const;
    };
#elif defined(__linux__) && !defined(__ANDROID__)
    // Cairo context pointer — Linux only.
    cairo_t *cairoCtx_ = nullptr;
#endif

private:
    // ── Backend ───────────────────────────────────────────────────────────
    Canvas2DBackend *backend_ = nullptr;
    int w_ = 0, h_ = 0;

    // ── Draw state (shared across all backends) ───────────────────────────

    Color fillColor_ = {0, 0, 0, 255};
    Color strokeColor_ = {0, 0, 0, 255};
    float lineWidth_ = 1.f;
    float globalAlpha_ = 1.f;
    bool fillIsGrad_ = false;
    FillRule fillRule_ = FillRule::NonZero;
    CompositeOp compositeOp_ = CompositeOp::SourceOver;
    LineCap lineCap_ = LineCap::Butt;
    LineJoin lineJoin_ = LineJoin::Miter;
    float miterLimit_ = 10.f;
    int clipDepth_ = 0;

    // ── Gradient state ────────────────────────────────────────────────────
    GradType gradType_ = GradType::None;
    float gx0_ = 0, gy0_ = 0, gx1_ = 0, gy1_ = 0;
    float gcx_ = 0, gcy_ = 0, gInR_ = 0, gOutR_ = 0;
    std::vector<std::pair<float, Color>> gStops_;

    // ── Text state ────────────────────────────────────────────────────────
    std::string fontFace_ = "sans";
    float fontSize_ = 14.f;
    bool fontBold_ = false;
    bool fontItalic_ = false;
    CanvasTextAlign textAlign_ = CanvasTextAlign::Left;
    TextBaseline textBaseline_ = TextBaseline::Alphabetic;

    // ── Path buffer (GL) / path points (D2D) ─────────────────────────────
#if defined(_WIN32)
    struct PathPt
    {
        float x, y;
        bool move;
        bool close;
    };
#else
    struct PathPt
    {
        float x, y;
        bool move;
    };
#endif
    std::vector<PathPt> path_;
    float pathStartX_ = 0, pathStartY_ = 0;
    float curX_ = 0, curY_ = 0;

    // ── Save/restore state ────────────────────────────────────────────────
    struct SaveState
    {
        // GL platforms track the CTM here so save/restore can replay it.
        // Cairo manages its own transform stack via cairo_save/cairo_restore,
        // so no CTM storage is needed there.  D2D has its own transform too.
#if defined(__ANDROID__) || defined(__EMSCRIPTEN__)
        Mat3 ctm;
#endif

#if defined(_WIN32)
        // D2D stores its transform differently — see d2d .cpp
        float d2dCtmM11, d2dCtmM12, d2dCtmM21, d2dCtmM22, d2dCtmDx, d2dCtmDy;
#endif
        Color fillColor, strokeColor;
        float lineWidth, globalAlpha;
        bool fillIsGrad;
        int clipDepth;
        GradType gradType;
        float gx0, gy0, gx1, gy1, gcx, gcy, gInR, gOutR;
        std::vector<std::pair<float, Color>> stops;
        std::string fontFace;
        float fontSize;
        bool fontBold, fontItalic;
        CanvasTextAlign textAlign;
        TextBaseline textBaseline;
        LineCap lineCap;
        LineJoin lineJoin;
    };
    std::vector<SaveState> stateStack_;

    // ── GL-only transform state ───────────────────────────────────────────
#if defined(__ANDROID__) || defined(__EMSCRIPTEN__)
    Mat3 ctm_;
    void buildMVP(float out[16]) const;
    // Internal helpers used by GL draw calls
    Color blendAlpha(Color c) const;
    void uploadAndDraw(const float *verts, int count, unsigned int mode,
                       Color col, float alpha = 1.f);
    void drawTexturedQuad(unsigned int tex,
                          float sx, float sy, float sw, float sh,
                          float dx, float dy, float dw, float dh,
                          int texW, int texH, float alpha = 1.f);
    void tessellateArc(float cx, float cy, float r,
                       float a0, float a1, bool ccw, bool move);
    void fillPath();
    void strokePath();
#endif

    // ── Helpers ───────────────────────────────────────────────────────────
    void parseFontDesc(const std::string &desc);
    int resolveFont() const;

#if defined(_WIN32)
    // D2D-specific draw helpers — forward declared here, defined in d2d .cpp
    void *getBrush(Color c);                                 // returns ID2D1SolidColorBrush*
    void *makeStrokeStyle() const;                           // returns ComPtr<ID2D1StrokeStyle>
    void *makeFillBrush(float x, float y, float w, float h); // returns ComPtr<ID2D1Brush>
    void *buildPathGeometry() const;                         // returns ComPtr<ID2D1PathGeometry>
    void drawPathFilled();
    void drawPathStroked();
    void applyD2DTransform();

    // D2D transform matrix (mirrors ctm_ role on GL)
    // Stored as flat floats to avoid D2D headers leaking into this header.
    float d2dM11_ = 1, d2dM12_ = 0;
    float d2dM21_ = 0, d2dM22_ = 1;
    float d2dDx_ = 0, d2dDy_ = 0;

    // Per-frame brush cache keyed by packed RGBA uint32
    // Stored as void* to avoid <unordered_map> + COM headers here.
    // Actual type: std::unordered_map<uint32_t, ComPtr<ID2D1SolidColorBrush>>
    // Allocated in constructor, deleted in destructor.
    void *brushCache_ = nullptr;
#endif
};