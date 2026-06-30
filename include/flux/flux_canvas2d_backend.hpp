#pragma once
// ============================================================================
// flux_canvas2d_backend.hpp  —  Internal backend definitions
//
// Included ONLY by:
//   flux_canvas2d_gl.cpp        (Android, Web  — GLES3/WebGL2)
//   flux_canvas2d_cairo.cpp     (Linux          — Cairo)
//   flux_canvas2d_metal.cpp     (macOS          — Metal stub)
//   flux_canvas2d_d2d.cpp       (Win32          — Direct2D)
//
// Never include this from flux_canvas2d.hpp or any RenderSurface header.
// RenderSurface authors see only flux_canvas2d.hpp.
//
// Platform matrix:
//   __ANDROID__  || __EMSCRIPTEN__  → GL block   (Canvas2DGL, GLES3)
//   __linux__    && !__ANDROID__    → Cairo block (Canvas2DCairo)
//   __APPLE__    && TARGET_OS_OSX   → Metal stub  (Canvas2DMetal, no GL)
//   _WIN32                          → D2D block   (Canvas2DD2D)
// ============================================================================

#include "flux_canvas2d.hpp"

#include <stb_truetype.h>

#include <string>
#include <vector>
#include <cstdint>

// =============================================================================
// GL backend  —  Android (GLES3) + Web/Emscripten (WebGL2) ONLY
// =============================================================================
#if defined(__ANDROID__) || defined(__EMSCRIPTEN__)

#include <GLES3/gl3.h>

// ── Concrete image type for GL ────────────────────────────────────────────────
struct Canvas2DImageGL : Canvas2DImage
{
    GLuint texId = 0;
    ~Canvas2DImageGL() override; // defined in flux_canvas2d_gl.cpp
};

// ── Shared GL resources (font atlas, shaders, VAO/VBO) ───────────────────────
// One per GL context. Created by CanvasWidget, passed into Canvas2DBackend.
struct Canvas2DGL
{
    // ── Flat-colour / textured shader ─────────────────────────────────────
    GLuint flatProg = 0;
    GLint flatMVP = -1;
    GLint flatColor = -1;
    GLint flatMode = -1; // 0=solid, 1=font atlas R8, 2=RGBA tex

    // ── Dynamic geometry ──────────────────────────────────────────────────
    GLuint vao = 0, vbo = 0;

    // ── Font atlas (R8, single channel, 1024×1024) ────────────────────────
    GLuint fontTex = 0;
    int atlasW = 1024;
    int atlasH = 1024;

    struct FontFace
    {
        std::string name;
        std::vector<uint8_t> ttfData; // kept alive — stbtt_fontinfo refs it
        stbtt_fontinfo info = {};
        bool ready = false;
    };
    std::vector<FontFace> fonts;
    std::vector<uint8_t> atlasPixels; // atlasW * atlasH, single channel

    // atlas shelf packer cursor
    int shelfX = 0, shelfY = 0, shelfH = 0;

    struct GlyphKey
    {
        int fontIdx, codepoint, pixelSize;
        bool operator==(const GlyphKey &o) const
        {
            return fontIdx == o.fontIdx &&
                   codepoint == o.codepoint &&
                   pixelSize == o.pixelSize;
        }
    };
    struct GlyphEntry
    {
        GlyphKey key;
        int atlasX, atlasY, glyphW, glyphH;
        int xoff, yoff; // left / top bearing (pixels)
        int advance;    // horizontal advance (pixels)
    };
    std::vector<GlyphEntry> glyphs;

    bool init();
    void destroy();
    int findFont(const std::string &name) const;
    int addFont(const std::string &name, const std::string &path);
    const GlyphEntry *getGlyph(int fontIdx, int codepoint, int pixelSize);
    float getKernAdvance(int fontIdx, int cp1, int cp2, int pixelSize) const;

private:
    void uploadAtlas();
    bool bakeGlyph(int fontIdx, int codepoint, int pixelSize, GlyphEntry &out);
};

// ── Canvas2DBackend — GL edition ──────────────────────────────────────────────
struct Canvas2DBackend
{
    Canvas2DGL *gl = nullptr; // owned externally by CanvasWidget

    float mvp[16] = {
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1};

    static Canvas2DBackend *create(Canvas2DGL *gl);
    static void destroy(Canvas2DBackend *b);
};

// =============================================================================
// Cairo backend  —  Linux desktop ONLY (not Android)
// =============================================================================
#elif defined(__linux__) && !defined(__ANDROID__)

#include <cairo/cairo.h>

// ── Concrete image type for Cairo ─────────────────────────────────────────────
struct Canvas2DImageCairo : Canvas2DImage
{
    cairo_surface_t *surface = nullptr;
    ~Canvas2DImageCairo() override; // defined in flux_canvas2d_cairo.cpp
};

// ── Shared Cairo resources ────────────────────────────────────────────────────
// stbtt is used for advance/kern queries to match GL behaviour.
// Cairo/Pango handles actual glyph rendering.
struct Canvas2DCairo
{
    cairo_t *cr = nullptr; // borrowed each frame from CanvasWidget — not owned

    struct FontFace
    {
        std::string name;
        std::vector<uint8_t> ttfData;
        stbtt_fontinfo info = {};
        bool ready = false;
        cairo_font_face_t *cairoFace = nullptr;
    };
    std::vector<FontFace> fonts;

    struct GlyphKey
    {
        int fontIdx, codepoint, pixelSize;
        bool operator==(const GlyphKey &o) const
        {
            return fontIdx == o.fontIdx &&
                   codepoint == o.codepoint &&
                   pixelSize == o.pixelSize;
        }
    };
    struct GlyphEntry
    {
        GlyphKey key;
        int xoff, yoff, advance;
    };
    std::vector<GlyphEntry> glyphs;

    bool init();
    void destroy();
    int findFont(const std::string &name) const;
    int addFont(const std::string &name, const std::string &path);
    const GlyphEntry *getGlyph(int fontIdx, int codepoint, int pixelSize);
    float getKernAdvance(int fontIdx, int cp1, int cp2, int pixelSize) const;
};

// ── Canvas2DBackend — Cairo edition ──────────────────────────────────────────
struct Canvas2DBackend
{
    Canvas2DCairo *cairo = nullptr; // owned externally by CanvasWidget

    // mvp unused on Cairo — kept so shared CanvasWidget code compiles cleanly.
    float mvp[16] = {
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1};

    static Canvas2DBackend *create(Canvas2DCairo *cairo);
    static void destroy(Canvas2DBackend *b);
};

// =============================================================================
// Metal stub  —  macOS ONLY
// =============================================================================
#elif defined(__APPLE__)
#include <TargetConditionals.h>
#if TARGET_OS_OSX

#import <Metal/Metal.h> // needed for id<MTLDevice>/id<MTLRenderCommandEncoder> below

// Opaque forward — full definition in flux_canvas2d_metal.mm
struct Canvas2DMetalContext;

// ── Concrete image type for Metal ────────────────────────────────────────────
struct Canvas2DImageMetal : Canvas2DImage
{
    Canvas2DMetalContext *ctx = nullptr; // texture handle, owned
    ~Canvas2DImageMetal() override;      // defined in flux_canvas2d_metal.mm
};

// ── Shared Metal resources ────────────────────────────────────────────────────
struct Canvas2DMetal
{
    Canvas2DMetalContext *ctx = nullptr; // owned

    struct FontFace
    {
        std::string name;
        std::vector<uint8_t> ttfData;
        stbtt_fontinfo info = {};
        bool ready = false;
    };
    std::vector<FontFace> fonts;

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
        int xoff, yoff, advance;
    };
    std::vector<GlyphEntry> glyphs;

    bool init();
    void destroy();
    int findFont(const std::string &name) const;
    int addFont(const std::string &name, const std::string &path);
    const GlyphEntry *getGlyph(int fontIdx, int codepoint, int pixelSize);
    float getKernAdvance(int fontIdx, int cp1, int cp2, int pixelSize) const;
};

// ── Canvas2DBackend — Metal edition ──────────────────────────────────────────
struct Canvas2DBackend
{
    Canvas2DMetal *metal = nullptr;

    // mvp unused on Metal — kept for shared CanvasWidget compilation.
    // Live per-frame uniforms (including the real mvp) are passed via
    // Canvas2DMetalBeginFrame below, not read from this field.
    float mvp[16] = {
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1};

    static Canvas2DBackend *create(Canvas2DMetal *metal);
    static void destroy(Canvas2DBackend *b);
};

// ── Encoder wiring (Stage: Canvas2D Metal backend) ───────────────────────────
// Canvas2D's constructor (flux_canvas2d.hpp) takes no encoder parameter, so
// the live MTLRenderCommandEncoder for the current frame must be supplied
// separately. CanvasWidget::glRenderPass() (flux_canvas_macos.mm) calls
// these immediately around its Canvas2D usage:
//
//   Canvas2DMetalBeginFrame(s.backend->metal, s.device, enc, mvp);
//   Canvas2D ctx2d(s.backend, canvasW_, canvasH_);
//   activeSurface_->render(ctx2d);
//   Canvas2DMetalEndFrame(s.backend->metal);
//

void Canvas2DMetalEndFrame(Canvas2DMetal *metal);

#endif // TARGET_OS_OSX
#endif // __APPLE__

// =============================================================================
// D2D backend  —  Win32 ONLY
// =============================================================================
#ifdef _WIN32

#include "flux/flux_d3d_device.hpp"

#include <d2d1_3.h>
#include <d2d1_1helper.h>
#include <wrl/client.h>

#include <unordered_map>

using Microsoft::WRL::ComPtr;

// ── Concrete image type for D2D ───────────────────────────────────────────────
struct Canvas2DImageD2D : Canvas2DImage
{
    ComPtr<ID2D1Bitmap1> bitmap;
    ~Canvas2DImageD2D() override = default;
};

// ── Shared D2D resources ──────────────────────────────────────────────────────
struct Canvas2DD2D
{
    D3DDevice *device = nullptr; // borrowed — not owned

    ComPtr<ID2D1DeviceContext> offscreenDC;
    ComPtr<ID2D1Bitmap1> offscreenBitmap;
    int bitmapW = 0, bitmapH = 0;

    struct FontFace
    {
        std::string name;
        std::vector<uint8_t> ttfData;
        stbtt_fontinfo info = {};
        bool ready = false;
    };
    std::vector<FontFace> fonts;

    static constexpr int kAtlasW = 1024;
    static constexpr int kAtlasH = 1024;
    std::vector<uint8_t> atlasPixels; // kAtlasW * kAtlasH * 4  (BGRA premul)
    ComPtr<ID2D1Bitmap> atlasBitmap;
    int shelfX = 0, shelfY = 0, shelfH = 0;

    struct GlyphKey
    {
        int fontIdx, codepoint, pixelSize;
        bool operator==(const GlyphKey &o) const
        {
            return fontIdx == o.fontIdx &&
                   codepoint == o.codepoint &&
                   pixelSize == o.pixelSize;
        }
    };
    struct GlyphEntry
    {
        GlyphKey key;
        int atlasX, atlasY, glyphW, glyphH;
        int xoff, yoff, advance;
    };
    std::vector<GlyphEntry> glyphs;

    bool init(D3DDevice *dev);
    void destroy();
    bool resizeBitmap(int w, int h);

    static bool registerFont(Canvas2DD2D *self,
                             const std::string &name,
                             const std::string &path);
    int findFont(const std::string &name) const;
    int addFont(const std::string &name, const std::string &path);
    const GlyphEntry *getGlyph(int fontIdx, int codepoint, int pixelSize);
    float getKernAdvance(int fontIdx, int cp1, int cp2, int pixelSize) const;

private:
    void uploadAtlas();
    bool bakeGlyph(int fontIdx, int codepoint, int pixelSize, GlyphEntry &out);
};

// ── Canvas2DBackend — D2D edition ─────────────────────────────────────────────
struct Canvas2DBackend
{
    Canvas2DD2D *d2d = nullptr; // owned externally by CanvasWidget

    // mvp is unused on D2D — kept so CanvasWidget code compiles without #ifdefs.
    float mvp[16] = {
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1};

    static Canvas2DBackend *create(Canvas2DD2D *d2d);
    static void destroy(Canvas2DBackend *b);
};

#endif // _WIN32