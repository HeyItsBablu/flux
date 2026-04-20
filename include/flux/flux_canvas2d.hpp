#pragma once
// ============================================================================
// flux_canvas2d.hpp  —  HTML5-style 2-D drawing API  (pure GL, no NanoVG)
//
// Drop-in replacement for the NanoVG-backed version.
// Public API is identical; internals use raw GL 3.3 core + stb_truetype.
//
// One Canvas2D is stack-allocated by CanvasWidget each frame and passed to
// RenderSurface::render().  Do NOT construct one yourself.
//
// Thread-safety: must be called on the GL thread only.
// ============================================================================

#include "flux_platform.hpp"
#include <string>
#include <vector>
#include <memory>

// ── GL (glad included by CanvasWidget before us) ─────────────────────────────
#if defined(_WIN32) || defined(__linux__) && !defined(__ANDROID__)
#  include <glad/glad.h>
#elif defined(__ANDROID__)
#  include <GLES3/gl3.h>
#elif defined(__APPLE__)
#  include <OpenGL/gl3.h>
#endif

// ─────────────────────────────────────────────────────────────────────────────
// Canvas2DImage — returned by Canvas2D::loadImage()
// ─────────────────────────────────────────────────────────────────────────────
struct Canvas2DImage {
    GLuint texId  = 0;   // GL texture id  (0 = invalid)
    int    width  = 0;
    int    height = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// Enums  (identical to the NanoVG version)
// ─────────────────────────────────────────────────────────────────────────────
enum class TextAlign    { Left, Right, Center };
enum class TextBaseline { Top, Middle, Bottom, Alphabetic };
enum class LineCap      { Butt, Round, Square };
enum class LineJoin     { Miter, Round, Bevel };
enum class CompositeOp  { SourceOver, Copy, Xor, Multiply, Screen };
enum class FillRule     { NonZero, EvenOdd };

// ─────────────────────────────────────────────────────────────────────────────
// Canvas2DGL — internal shared GL resources (one per GL context)
// Created by CanvasWidget::ensureWindows, destroyed by CanvasWidget::destroyGL.
// ─────────────────────────────────────────────────────────────────────────────
struct Canvas2DGL {
    // ── Flat-colour shader ────────────────────────────────────────────────
    GLuint flatProg  = 0;
    GLint  flatMVP   = -1;
    GLint  flatColor = -1;
    GLint  flatMode  = -1;   // 0=solid, 1=texture

    // ── Textured-quad shader ──────────────────────────────────────────────
    GLuint texProg   = 0;
    GLint  texMVP    = -1;
    GLint  texSampler= -1;
    GLint  texAlpha  = -1;

    // ── Dynamic geometry ──────────────────────────────────────────────────
    GLuint vao = 0, vbo = 0;

    // ── stb_truetype font atlas ───────────────────────────────────────────
    GLuint fontTex = 0;
    int    atlasW  = 1024;
    int    atlasH  = 1024;

    struct FontFace {
        std::string name;
        std::vector<unsigned char> ttfData;    // kept alive for stbtt
        void*  stbFont = nullptr;              // stbtt_fontinfo* (opaque)
        // baked bitmap glyph cache lives in the shared atlas
    };
    std::vector<FontFace> fonts;
    std::vector<unsigned char> atlasPixels;    // single-channel, atlasW*atlasH

    // atlas packing cursor (very simple shelf packer)
    int shelfX = 0, shelfY = 0, shelfH = 0;

    struct GlyphKey {
        int fontIdx;
        int codepoint;
        int pixelSize;
        bool operator==(const GlyphKey& o) const {
            return fontIdx==o.fontIdx && codepoint==o.codepoint && pixelSize==o.pixelSize;
        }
    };
    struct GlyphEntry {
        GlyphKey key;
        int atlasX, atlasY, glyphW, glyphH;
        int xoff, yoff;   // stbtt bearing
        int advance;
    };
    std::vector<GlyphEntry> glyphs;

    bool init();
    void destroy();
    int  findFont(const std::string& name) const;
    int  addFont(const std::string& name, const std::string& path);
    const GlyphEntry* getGlyph(int fontIdx, int codepoint, int pixelSize);

private:
    void uploadAtlas();
    bool bakeGlyph(int fontIdx, int codepoint, int pixelSize, GlyphEntry& out);
};

// ─────────────────────────────────────────────────────────────────────────────
// Canvas2D — main drawing API
// ─────────────────────────────────────────────────────────────────────────────
struct Canvas2D {

    // Constructed by CanvasWidget only.
    explicit Canvas2D(Canvas2DGL* gl, int canvasW, int canvasH,
                      const float mvp[16]);
    ~Canvas2D() = default;

    Canvas2D(const Canvas2D&)            = delete;
    Canvas2D& operator=(const Canvas2D&) = delete;

    // ── Dimensions ────────────────────────────────────────────────────────
    int width()  const { return canvasW_; }
    int height() const { return canvasH_; }

    // ── State stack ───────────────────────────────────────────────────────
    void save();
    void restore();

    // ── Transform ─────────────────────────────────────────────────────────
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

    // ── Linear gradient ───────────────────────────────────────────────────
    void beginLinearGradient(float x0, float y0, float x1, float y1);
    void addColorStop(float t, Color c);
    void setFillGradient();

    // ── Radial gradient ───────────────────────────────────────────────────
    void beginRadialGradient(float cx, float cy, float innerR, float outerR);

    // ── Primitive drawing ─────────────────────────────────────────────────
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
    void clip();

    // ── Image drawing ─────────────────────────────────────────────────────
    // Call loadImage / freeImage from initialize() or update(), NOT render().
    Canvas2DImage* loadImage(const std::string& path);
    Canvas2DImage* loadImageFromMemory(const unsigned char* data, int byteLen);
    Canvas2DImage* wrapTexture(GLuint texId, int w, int h);  // NEW: wrap existing GL tex
    void           updateTexture(Canvas2DImage* img,          // NEW: pixel upload
                                 const unsigned char* rgba, int w, int h);
    void           freeImage(Canvas2DImage* img);

    void drawImage(const Canvas2DImage* img, float dx, float dy);
    void drawImage(const Canvas2DImage* img,
                   float dx, float dy, float dw, float dh);
    void drawImage(const Canvas2DImage* img,
                   float sx, float sy, float sw, float sh,
                   float dx, float dy, float dw, float dh);

    // ── Text ──────────────────────────────────────────────────────────────
    static bool registerFont(Canvas2DGL* gl,
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

    // ── Clip rect ─────────────────────────────────────────────────────────
    void pushClipRect(float x, float y, float w, float h);
    void popClipRect();

    // ── Pixel access ──────────────────────────────────────────────────────
    void getImageData(float x, float y, float w, float h,
                      std::vector<uint8_t>& out);
    void putImageData(const std::vector<uint8_t>& data,
                      int srcW, int srcH,
                      float dx, float dy);

    // ── Internal state ────────────────────────────────────────────────────
    Canvas2DGL* gl_     = nullptr;
    int         canvasW_= 0;
    int         canvasH_= 0;

private:
    // ── MVP / transform stack ─────────────────────────────────────────────
    struct Mat3 {
        float m[9] = {1,0,0, 0,1,0, 0,0,1};
        static Mat3 identity();
        Mat3 multiply(const Mat3& b) const;
        Mat3 translated(float dx, float dy) const;
        Mat3 scaled(float sx, float sy) const;
        Mat3 rotated(float r) const;
        void apply(float& x, float& y) const;
    };

    float       baseMVP_[16];   // ortho from CanvasWidget
    Mat3        ctm_;           // current transform (canvas-space)
    struct SaveState {
        Mat3   ctm;
        Color  fillColor, strokeColor;
        float  lineWidth, globalAlpha;
        bool   fillIsGrad;
        int    clipDepth;
        float  gx0,gy0,gx1,gy1,gcx,gcy,gInR,gOutR;
        enum class GradType { None, Linear, Radial } gradType;
        std::vector<std::pair<float,Color>> stops;
        std::string fontFace;
        float fontSize;
        bool fontBold, fontItalic;
        TextAlign textAlign;
        TextBaseline textBaseline;
    };
    std::vector<SaveState> stateStack_;

    // ── Path buffer ───────────────────────────────────────────────────────
    struct PathPt { float x, y; bool move; };
    std::vector<PathPt> path_;
    float pathStartX_ = 0, pathStartY_ = 0;
    float curX_       = 0, curY_       = 0;

    // ── Gradient state ────────────────────────────────────────────────────
    enum class GradType { None, Linear, Radial };
    GradType gradType_ = GradType::None;
    float gx0_=0,gy0_=0,gx1_=0,gy1_=0;
    float gcx_=0,gcy_=0,gInR_=0,gOutR_=0;
    std::vector<std::pair<float,Color>> gStops_;
    bool fillIsGrad_ = false;

    // ── Draw state ────────────────────────────────────────────────────────
    Color  fillColor_   = {0,0,0,255};
    Color  strokeColor_ = {0,0,0,255};
    float  lineWidth_   = 1.f;
    float  globalAlpha_ = 1.f;
    FillRule fillRule_  = FillRule::NonZero;
    int    clipDepth_   = 0;
    CompositeOp compositeOp_ = CompositeOp::SourceOver;

    // ── Text state ────────────────────────────────────────────────────────
    std::string  fontFace_     = "sans";
    float        fontSize_     = 14.f;
    bool         fontBold_     = false;
    bool         fontItalic_   = false;
    TextAlign    textAlign_    = TextAlign::Left;
    TextBaseline textBaseline_ = TextBaseline::Alphabetic;

    // ── Helpers ───────────────────────────────────────────────────────────
    void buildMVP(float out[16]) const;
    void uploadAndDraw(const float* verts, int count, GLenum mode,
                       Color col, float alpha = 1.f);
    void drawTexturedQuad(GLuint tex,
                          float sx, float sy, float sw, float sh,
                          float dx, float dy, float dw, float dh,
                          int texW, int texH, float alpha = 1.f);
    void tessellateArc(float cx, float cy, float r,
                       float a0, float a1, bool ccw, bool move);
    void tessellateEllipse(float cx, float cy, float rx, float ry,
                           float rot, float a0, float a1, bool ccw);
    void strokePath();
    void fillPath();
    Color blendAlpha(Color c) const;
    void  parseFontDesc(const std::string& desc);
    int   resolveFont() const;
};