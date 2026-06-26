#pragma once
#ifdef _WIN32
// ============================================================================
// flux_canvas2d_d2d.hpp
//
// Win32 Direct2D backend for Canvas2D.
// Replaces Canvas2DGL entirely on _WIN32; no GL headers are included.
//
// One Canvas2DD2D is owned by CanvasWidget and lives for the widget lifetime.
// It wraps a shared D3DDevice (owned by PlatformWindow) and maintains its
// own off-screen ID2D1Bitmap1 render target sized to the canvas document.
//
// Thread-safety: all methods must be called on the render thread.
// ============================================================================

#include "flux/flux_d3d_device.hpp" // D3DDevice, BrushCache
#include "flux/flux_platform.hpp"
#include "flux/flux_canvas_types.hpp" // RGBA, ScrollbarInfo, Viewport

#include <d2d1_3.h>
#include <d2d1_1helper.h>
#include <dwrite_3.h>
#include <wrl/client.h>

#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>

// FreeType forward-decls (implementation includes ft2build.h)
typedef struct FT_LibraryRec_ *FT_Library;
typedef struct FT_FaceRec_ *FT_Face;

using Microsoft::WRL::ComPtr;

// ─────────────────────────────────────────────────────────────────────────────
// Canvas2DImage  (D2D edition)
// Returned by Canvas2D::loadImage / wrapTexture.
// ─────────────────────────────────────────────────────────────────────────────
struct Canvas2DImage
{
    ComPtr<ID2D1Bitmap1> bitmap; // nullptr == invalid
    int width = 0;
    int height = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// Enums (identical public surface to the GL version)
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
// Canvas2DD2D  — shared D2D resources (one per CanvasWidget)
//
// Analogous to Canvas2DGL but backed by Direct2D + FreeType.
// The off-screen bitmap is resized whenever the canvas document size changes.
// ─────────────────────────────────────────────────────────────────────────────
struct Canvas2DD2D
{
    // ── Borrowed from PlatformWindow — not owned ──────────────────────────
    D3DDevice *device = nullptr; // the shared D3D/D2D device

    // ── Off-screen render target (document-sized) ─────────────────────────
    // CanvasWidget renders into this, then composites it into the main DC.
    ComPtr<ID2D1DeviceContext> offscreenDC; // targets offscreenBitmap
    ComPtr<ID2D1Bitmap1> offscreenBitmap;
    int bitmapW = 0;
    int bitmapH = 0;

    // ── FreeType ─────────────────────────────────────────────────────────
    FT_Library ftLibrary = nullptr;

    struct FontFace
    {
        std::string name;
        std::vector<uint8_t> ttfData; // kept alive — FT_Face refs it
        FT_Face ftFace = nullptr;
    };
    std::vector<FontFace> fonts;

    // ── Glyph atlas (BGRA D2D bitmap, grey channel in R) ─────────────────
    // We use a CPU-side buffer and upload regions to a D2D bitmap.
    static constexpr int kAtlasW = 1024;
    static constexpr int kAtlasH = 1024;

    std::vector<uint8_t> atlasPixels; // BGRA, kAtlasW*kAtlasH*4
    ComPtr<ID2D1Bitmap> atlasBitmap;  // D2D_ALPHA_MODE_PREMULTIPLIED

    int shelfX = 0, shelfY = 0, shelfH = 0;

    struct GlyphKey
    {
        int fontIdx, codepoint, pixelSize;
        bool operator==(const GlyphKey &o) const
        {
            return fontIdx == o.fontIdx && codepoint == o.codepoint && pixelSize == o.pixelSize;
        }
    };
    struct GlyphEntry
    {
        GlyphKey key;
        int atlasX = 0, atlasY = 0;
        int glyphW = 0, glyphH = 0;
        int xoff = 0, yoff = 0;
        int advance = 0;
    };
    std::vector<GlyphEntry> glyphs;

    // ── Lifecycle ─────────────────────────────────────────────────────────
    bool init(D3DDevice *dev);
    void destroy();

    // Resize the off-screen bitmap. Call when the canvas document changes size.
    bool resizeBitmap(int w, int h);

    // ── Font management ───────────────────────────────────────────────────
    static bool registerFont(Canvas2DD2D *self,
                             const std::string &name,
                             const std::string &path);
    int findFont(const std::string &name) const;
    int addFont(const std::string &name, const std::string &path);

    // ── Glyph cache ───────────────────────────────────────────────────────
    const GlyphEntry *getGlyph(int fontIdx, int codepoint, int pixelSize);
    float getKernAdvance(int fontIdx, int cp1, int cp2, int pixelSize) const;

private:
    void uploadAtlas();
    bool bakeGlyph(int fontIdx, int codepoint, int pixelSize, GlyphEntry &out);
};

// ─────────────────────────────────────────────────────────────────────────────
// Canvas2D  — main drawing API (same public surface as the GL version)
//
// One instance is stack-allocated per frame and passed to RenderSurface::render().
// ─────────────────────────────────────────────────────────────────────────────
struct Canvas2D
{
    explicit Canvas2D(Canvas2DD2D *d2d, int canvasW, int canvasH);
    ~Canvas2D() = default;

    Canvas2D(const Canvas2D &) = delete;
    Canvas2D &operator=(const Canvas2D &) = delete;

    // ── Dimensions ────────────────────────────────────────────────────────
    int width() const { return canvasW_; }
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
                       float cp2x, float cp2y,
                       float x, float y);
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
    static bool registerFont(Canvas2DD2D *d2d,
                             const std::string &name,
                             const std::string &path);

    Canvas2DImage *loadImage(const std::string &path);
    Canvas2DImage *loadImageFromMemory(const unsigned char *data, int byteLen);
    Canvas2DImage *wrapBitmap(ComPtr<ID2D1Bitmap1> bmp, int w, int h);
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
    void setFont(const std::string &fontDesc);
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

    float currentFontSize() const { return fontSize_; }
    int currentFontIdx() const { return resolveFont(); }

    // ── Kerning ───────────────────────────────────────────────────────────────
    float getKernAdvance(int fontIdx, int cp1, int cp2, int pixelSize) const
    {
        if (!d2d_)
            return 0.f;
        return d2d_->getKernAdvance(fontIdx, cp1, cp2, pixelSize);
    }

    // ── Accessors used by Canvas2DD2D internals ────────────────────────────
    Canvas2DD2D *d2d_ = nullptr;
    int canvasW_ = 0;
    int canvasH_ = 0;

private:
    // The D2D device context we draw into (= d2d_->offscreenDC)
    ID2D1DeviceContext *dc() const { return d2d_->offscreenDC.Get(); }

    // ── Transform ─────────────────────────────────────────────────────────
    D2D1_MATRIX_3X2_F ctm_ = D2D1::Matrix3x2F::Identity();

    // ── Draw state ────────────────────────────────────────────────────────
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

    // ── Gradient state ────────────────────────────────────────────────────
    enum class GradType
    {
        None,
        Linear,
        Radial
    };
    GradType gradType_ = GradType::None;
    float gx0_ = 0, gy0_ = 0, gx1_ = 0, gy1_ = 0;
    float gcx_ = 0, gcy_ = 0, gInR_ = 0, gOutR_ = 0;
    std::vector<std::pair<float, Color>> gStops_;

    // ── Clip tracking ─────────────────────────────────────────────────────
    int clipDepth_ = 0; // counts PushAxisAlignedClip calls

    // ── Text state ────────────────────────────────────────────────────────
    std::string fontFace_ = "sans";
    float fontSize_ = 14.f;
    bool fontBold_ = false;
    bool fontItalic_ = false;
    CanvasTextAlign textAlign_ = CanvasTextAlign::Left;
    TextBaseline textBaseline_ = TextBaseline::Alphabetic;

    // ── Path ──────────────────────────────────────────────────────────────
    // We accumulate path commands and flush to a D2D PathGeometry on fill/stroke
    struct PathPt
    {
        float x, y;
        bool move;
        bool close;
    };
    std::vector<PathPt> path_;
    float curX_ = 0, curY_ = 0;
    float pathStartX_ = 0, pathStartY_ = 0;

    // ── Save state ────────────────────────────────────────────────────────
    struct SaveState
    {
        D2D1_MATRIX_3X2_F ctm;
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

    // ── Helpers ───────────────────────────────────────────────────────────
    ID2D1SolidColorBrush *getBrush(Color c);
    ComPtr<ID2D1StrokeStyle> makeStrokeStyle() const;
    ComPtr<ID2D1Brush> makeFillBrush(const D2D1_RECT_F &bounds);
    ComPtr<ID2D1PathGeometry> buildPathGeometry() const;
    void drawPathFilled();
    void drawPathStroked();
    D2D1_COLOR_F toD2D(Color c) const;
    void parseFontDesc(const std::string &desc);
    int resolveFont() const;
    void applyTransform();
    void pushTransform(D2D1_MATRIX_3X2_F m);

    // brush cache per frame (keyed by RGBA packed uint32)
    std::unordered_map<uint32_t, ComPtr<ID2D1SolidColorBrush>> brushCache_;
};

#endif // _WIN32