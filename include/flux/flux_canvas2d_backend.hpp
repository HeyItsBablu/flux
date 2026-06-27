#pragma once
// ============================================================================
// flux_canvas2d_backend.hpp  —  Internal backend definitions
//
// Included ONLY by:
//   flux_canvas2d_gl.cpp
//   flux_canvas2d_d2d.cpp
//
// Never include this from flux_canvas.hpp or any RenderSurface header.
// RenderSurface authors see only flux_canvas2d.hpp.
// ============================================================================

#include "flux_canvas2d.hpp"

// ─────────────────────────────────────────────────────────────────────────────
// GL backend
// ─────────────────────────────────────────────────────────────────────────────
#if !defined(_WIN32)

// GL / GLES headers — included here so the main header stays clean.
#if defined(__linux__) && !defined(__ANDROID__)
#include <glad/glad.h>
#elif defined(__ANDROID__) || defined(__EMSCRIPTEN__)
#include <GLES3/gl3.h>
#elif defined(__APPLE__)
#include <TargetConditionals.h>
#if TARGET_OS_OSX
// macOS uses Metal via Canvas2DGL stub — GL types still needed for
// the shared Canvas2DGL struct even though draw calls go through Metal.
#include <stdint.h>
using GLuint = uint32_t;
using GLenum = uint32_t;
using GLint = int32_t;
using GLsizei = int32_t;
using GLfloat = float;
using GLubyte = uint8_t;
#endif
#endif

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H

#include <string>
#include <vector>
#include <cstdint>

// ── Concrete image type for GL ────────────────────────────────────────────────
// Canvas2DImage* returned to callers is actually a Canvas2DImageGL*.
// freeImage() deletes via the virtual destructor, which cleans up texId.
struct Canvas2DImageGL : Canvas2DImage
{
    GLuint texId = 0;
    ~Canvas2DImageGL() override; // defined in flux_canvas2d_gl.cpp
};

// ── Shared GL resources (font atlas, shaders, VAO/VBO) ───────────────────────
// One per GL context.  Created by CanvasWidget, passed into Canvas2DBackend.
struct Canvas2DGL
{
    // ── Flat-colour / textured shader ─────────────────────────────────────
    GLuint flatProg = 0;
    GLint flatMVP = -1;
    GLint flatColor = -1;
    GLint flatMode = -1; // 0=solid, 1=font atlas R8, 2=RGBA tex

    // ── Dynamic geometry ──────────────────────────────────────────────────
    GLuint vao = 0, vbo = 0;

    // ── FreeType ──────────────────────────────────────────────────────────
    FT_Library ftLibrary = nullptr;

    // ── Font atlas (R8, single channel, 1024×1024) ────────────────────────
    GLuint fontTex = 0;
    int atlasW = 1024;
    int atlasH = 1024;

    struct FontFace
    {
        std::string name;
        std::vector<uint8_t> ttfData; // kept alive — FT_Face refs it
        FT_Face ftFace = nullptr;
    };
    std::vector<FontFace> fonts;
    std::vector<unsigned char> atlasPixels; // atlasW * atlasH, single channel

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
//
// CanvasWidget holds one of these per widget (or a shared one on Web/Android).
// Before constructing Canvas2D each frame, set mvp to the combined
// ortho × viewport matrix.  Canvas2D::buildMVP() multiplies ctm_ into it.
//
struct Canvas2DBackend
{
    Canvas2DGL *gl = nullptr; // owned externally by CanvasWidget

    // Set by CanvasWidget before each Canvas2D construction:
    //   memcpy(backend->mvp, computedMVP, 64);
    //   Canvas2D ctx(backend, w, h);
    float mvp[16] = {
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1};

    // Factory helpers called by CanvasWidget
    static Canvas2DBackend *create(Canvas2DGL *gl);
    static void destroy(Canvas2DBackend *b); // does NOT free gl
};

// ─────────────────────────────────────────────────────────────────────────────
// D2D backend
// ─────────────────────────────────────────────────────────────────────────────
#else // _WIN32

#include "flux/flux_d3d_device.hpp" // D3DDevice

#include <d2d1_3.h>
#include <d2d1_1helper.h>
#include <wrl/client.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H

#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>

using Microsoft::WRL::ComPtr;

// ── Concrete image type for D2D ───────────────────────────────────────────────
struct Canvas2DImageD2D : Canvas2DImage
{
    ComPtr<ID2D1Bitmap1> bitmap;
    ~Canvas2DImageD2D() override = default;
};

// ── Shared D2D resources ──────────────────────────────────────────────────────
// One per CanvasWidget.  Owns the offscreen render target + glyph atlas.
struct Canvas2DD2D
{
    D3DDevice *device = nullptr; // borrowed — not owned

    // Off-screen render target (document-sized)
    ComPtr<ID2D1DeviceContext> offscreenDC;
    ComPtr<ID2D1Bitmap1> offscreenBitmap;
    int bitmapW = 0, bitmapH = 0;

    // FreeType
    FT_Library ftLibrary = nullptr;

    struct FontFace
    {
        std::string name;
        std::vector<uint8_t> ttfData;
        FT_Face ftFace = nullptr;
    };
    std::vector<FontFace> fonts;

    // Glyph atlas (BGRA, premultiplied grey in all 4 channels)
    static constexpr int kAtlasW = 1024;
    static constexpr int kAtlasH = 1024;
    std::vector<uint8_t> atlasPixels; // kAtlasW * kAtlasH * 4
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
//
// On Win32 there is no MVP: D2D applies the viewport/zoom transform on the
// main device context in CanvasWidget::render() before blitting the offscreen
// bitmap.  Canvas2D's translate/scale/rotate methods manipulate the D2D
// transform stack directly via offscreenDC->SetTransform().
//
struct Canvas2DBackend
{
    Canvas2DD2D *d2d = nullptr; // owned externally by CanvasWidget

    // mvp is unused on D2D — kept in the struct so CanvasWidget code that
    // sets it compiles without #ifdefs.
    float mvp[16] = {
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1};

    static Canvas2DBackend *create(Canvas2DD2D *d2d);
    static void destroy(Canvas2DBackend *b);
};

#endif // _WIN32