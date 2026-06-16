// ============================================================================
// flux_canvas2d_gl.cpp
// Pure-GL implementation of Canvas2D.
// Font rasterisation: FreeType 2  (replaces stb_truetype)
// ============================================================================

#include <utility>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <sstream>
#include <fstream>
#include <vector>

// ── GL headers ──────────────────────────────────────────────────────────────
#if defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__))
#include <glad/glad.h>
#elif defined(__ANDROID__) || defined(__EMSCRIPTEN__)
#include <GLES3/gl3.h>
#elif defined(__APPLE__)

#endif

// ── FreeType ─────────────────────────────────────────────────────────────────
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H

// ── stb_image (images only — no stb_truetype) ────────────────────────────────
#include <stb_image.h>

#include "flux/flux_canvas2d.hpp"
#include "flux/flux_canvas_types.hpp"
#ifndef __APPLE__
#include "flux/flux_glutil.hpp"
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ─────────────────────────────────────────────────────────────────────────────
// GLSL shaders  (identical to old version)
// ─────────────────────────────────────────────────────────────────────────────

#if defined(__ANDROID__) || defined(__EMSCRIPTEN__)
static const char *kFlatVert =
    "#version 300 es\n"
    "precision mediump float;\n"
    "layout(location=0) in vec2 aPos;\n"
    "layout(location=1) in vec2 aUV;\n"
    "uniform mat4 uMVP;\n"
    "out vec2 vUV;\n"
    "void main(){ vUV=aUV; gl_Position=uMVP*vec4(aPos,0.0,1.0); }\n";

static const char *kFlatFrag =
    "#version 300 es\n"
    "precision mediump float;\n"
    "in  vec2 vUV;\n"
    "out vec4 fragColor;\n"
    "uniform vec4      uColor;\n"
    "uniform int       uMode;\n"
    "uniform sampler2D uTex;\n"
    "void main(){\n"
    "    if(uMode==0){\n"
    "        fragColor = uColor;\n"
    "    } else if(uMode==1){\n"
    "        float a = texture(uTex, vUV).r;\n"
    "        fragColor = vec4(uColor.rgb, uColor.a * a);\n"
    "    } else {\n"
    "        vec4 t = texture(uTex, vUV);\n"
    "        fragColor = t * uColor.a;\n"
    "    }\n"
    "}\n";
#else
static const char *kFlatVert = R"GLSL(
#version 330 core
layout(location=0) in vec2 aPos;
layout(location=1) in vec2 aUV;
uniform mat4 uMVP;
out vec2 vUV;
void main(){ vUV=aUV; gl_Position=uMVP*vec4(aPos,0.0,1.0); }
)GLSL";

// mode 0 = solid colour, mode 1 = single-channel font atlas, mode 2 = RGBA tex
static const char *kFlatFrag = R"GLSL(
#version 330 core
in  vec2 vUV;
out vec4 fragColor;
uniform vec4      uColor;
uniform int       uMode;
uniform sampler2D uTex;
void main(){
    if(uMode==0){
        fragColor = uColor;
    } else if(uMode==1){
        float a = texture(uTex, vUV).r;
        fragColor = vec4(uColor.rgb, uColor.a * a);
    } else {
        vec4 t = texture(uTex, vUV);
        fragColor = t * uColor.a;
    }
}
)GLSL";
#endif

// ─────────────────────────────────────────────────────────────────────────────
// Canvas2DGL — init / destroy
// ─────────────────────────────────────────────────────────────────────────────

bool Canvas2DGL::init()
{
    // ── FreeType library ──────────────────────────────────────────────────
    if (FT_Init_FreeType(&ftLibrary))
    {
#ifdef __ANDROID__
        __android_log_print(ANDROID_LOG_ERROR, "Canvas2DGL",
                            "FT_Init_FreeType failed");
#endif
        return false;
    }

    // ── Shader diagnostic (Android only) ─────────────────────────────────
#ifdef __ANDROID__
    {
        auto checkShader = [](const char *src, GLenum type, const char *tag)
        {
            GLuint s = glCreateShader(type);
            glShaderSource(s, 1, &src, nullptr);
            glCompileShader(s);
            GLint ok = 0;
            glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
            if (!ok)
            {
                char buf[2048];
                GLsizei len = 0;
                glGetShaderInfoLog(s, sizeof(buf), &len, buf);
                __android_log_print(ANDROID_LOG_ERROR, "Canvas2DGL",
                                    "%s compile FAILED:\n%s", tag, buf);
            }
            else
            {
                __android_log_print(ANDROID_LOG_INFO, "Canvas2DGL",
                                    "%s compile OK", tag);
            }
            glDeleteShader(s);
        };
        checkShader(kFlatVert, GL_VERTEX_SHADER, "kFlatVert");
        checkShader(kFlatFrag, GL_FRAGMENT_SHADER, "kFlatFrag");
    }
#endif

    flatProg = glutil::linkProgram(kFlatVert, kFlatFrag);
    if (!flatProg)
        return false;

    flatMVP = glGetUniformLocation(flatProg, "uMVP");
    flatColor = glGetUniformLocation(flatProg, "uColor");
    flatMode = glGetUniformLocation(flatProg, "uMode");

    // ── Geometry buffers ──────────────────────────────────────────────────
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, 1024 * 4 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)(2 * sizeof(float)));
    glBindVertexArray(0);

    // ── Font atlas texture (R8) ───────────────────────────────────────────
    atlasPixels.assign(size_t(atlasW) * atlasH, 0);
    glGenTextures(1, &fontTex);
    glBindTexture(GL_TEXTURE_2D, fontTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, atlasW, atlasH, 0,
                 GL_RED, GL_UNSIGNED_BYTE, atlasPixels.data());
    glBindTexture(GL_TEXTURE_2D, 0);
    return true;
}

void Canvas2DGL::destroy()
{
    // ── Free FreeType faces then library ──────────────────────────────────
    for (auto &f : fonts)
    {
        if (f.ftFace)
        {
            FT_Done_Face(f.ftFace);
            f.ftFace = nullptr;
        }
    }
    fonts.clear();
    glyphs.clear();

    if (ftLibrary)
    {
        FT_Done_FreeType(ftLibrary);
        ftLibrary = nullptr;
    }

    if (fontTex)
    {
        glDeleteTextures(1, &fontTex);
        fontTex = 0;
    }
    if (vao)
    {
        glDeleteVertexArrays(1, &vao);
        vao = 0;
    }
    if (vbo)
    {
        glDeleteBuffers(1, &vbo);
        vbo = 0;
    }
    if (flatProg)
    {
        glDeleteProgram(flatProg);
        flatProg = 0;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Font management
// ─────────────────────────────────────────────────────────────────────────────

int Canvas2DGL::findFont(const std::string &name) const
{
    for (int i = 0; i < (int)fonts.size(); ++i)
        if (fonts[i].name == name)
            return i;
    return -1;
}

int Canvas2DGL::addFont(const std::string &name, const std::string &path)
{
    int existing = findFont(name);
    if (existing >= 0)
        return existing;

    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f)
        return -1;
    auto sz = f.tellg();
    if (sz <= 0)
        return -1;
    f.seekg(0);

    fonts.push_back(FontFace());
    FontFace &face = fonts.back();
    face.name = name;
    face.ttfData.resize(size_t(sz));
    f.read(reinterpret_cast<char *>(face.ttfData.data()), sz);
    if (!f)
    {
        fonts.pop_back();
        return -1;
    }

    // FT_New_Memory_Face keeps a pointer into face.ttfData — the vector must
    // not be reallocated afterward.  We never resize it after this point.
    FT_Error err = FT_New_Memory_Face(ftLibrary,
                                      face.ttfData.data(),
                                      FT_Long(face.ttfData.size()),
                                      0,
                                      &face.ftFace);
    if (err)
    {
        fonts.pop_back();
        return -1;
    }

    return int(fonts.size()) - 1;
}

// ─────────────────────────────────────────────────────────────────────────────
// Glyph atlas
// ─────────────────────────────────────────────────────────────────────────────

void Canvas2DGL::uploadAtlas()
{
    glBindTexture(GL_TEXTURE_2D, fontTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, atlasW, atlasH, 0,
                 GL_RED, GL_UNSIGNED_BYTE, atlasPixels.data());
    glBindTexture(GL_TEXTURE_2D, 0);
}

bool Canvas2DGL::bakeGlyph(int fontIdx, int codepoint, int pixelSize,
                           GlyphEntry &out)
{
    if (fontIdx < 0 || fontIdx >= (int)fonts.size())
        return false;

    FT_Face face = fonts[fontIdx].ftFace;

    // Set pixel size (cached internally by FreeType when the same size is set
    // repeatedly, so this is cheap on repeated calls).
    FT_Set_Pixel_Sizes(face, 0, FT_UInt(pixelSize));

    FT_UInt glyphIndex = FT_Get_Char_Index(face, FT_ULong(codepoint));

    // Load and render to a 8-bit greyscale bitmap.
    FT_Error err = FT_Load_Glyph(face, glyphIndex, FT_LOAD_DEFAULT);
    if (err)
        return false;

    err = FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL);
    if (err)
        return false;

    FT_GlyphSlot slot = face->glyph;
    FT_Bitmap &bitmap = slot->bitmap;

    int gw = (int)bitmap.width;
    int gh = (int)bitmap.rows;

    // Metrics in 26.6 fixed-point → pixels
    int advance = int(slot->advance.x >> 6);
    int xoff = slot->bitmap_left; //  left bearing
    int yoff = -slot->bitmap_top; //  negate: FT top-down, canvas top-down

    if (gw <= 0 || gh <= 0)
    {
        // Whitespace / invisible glyph — store metrics only, no atlas entry.
        out = {GlyphKey{fontIdx, codepoint, pixelSize},
               0, 0, 0, 0, xoff, yoff, advance};
        return true;
    }

    // Shelf-pack into atlas.
    if (shelfX + gw + 1 > atlasW)
    {
        shelfX = 0;
        shelfY += shelfH + 1;
        shelfH = 0;
    }
    if (shelfY + gh + 1 > atlasH)
    {
        // Atlas full — return metrics but no usable pixels.
        out = {GlyphKey{fontIdx, codepoint, pixelSize},
               0, 0, gw, gh, xoff, yoff, advance};
        return true;
    }

    // Copy the FreeType bitmap row-by-row into the atlas.
    // FreeType's pitch may be negative (bottom-up) or have padding; handle
    // both by always using abs(pitch) as the source row stride.
    const int srcPitch = std::abs(bitmap.pitch);
    for (int row = 0; row < gh; ++row)
    {
        const unsigned char *src = bitmap.buffer + row * srcPitch;
        unsigned char *dst = atlasPixels.data() + (shelfY + row) * atlasW + shelfX;
        std::memcpy(dst, src, size_t(gw));
    }

    out = {GlyphKey{fontIdx, codepoint, pixelSize},
           shelfX, shelfY, gw, gh, xoff, yoff, advance};

    shelfX += gw + 1;
    if (gh > shelfH)
        shelfH = gh;

    uploadAtlas();
    return true;
}

const Canvas2DGL::GlyphEntry *Canvas2DGL::getGlyph(int fontIdx, int codepoint,
                                                   int pixelSize)
{
    for (auto &g : glyphs)
        if (g.key.fontIdx == fontIdx &&
            g.key.codepoint == codepoint &&
            g.key.pixelSize == pixelSize)
            return &g;

    GlyphEntry e;
    if (!bakeGlyph(fontIdx, codepoint, pixelSize, e))
        return nullptr;
    glyphs.push_back(e);
    return &glyphs.back();
}

float Canvas2DGL::getKernAdvance(int fontIdx, int cp1, int cp2,
                                 int pixelSize) const
{
    if (fontIdx < 0 || fontIdx >= (int)fonts.size())
        return 0.f;
    FT_Face face = fonts[fontIdx].ftFace;
    if (!FT_HAS_KERNING(face))
        return 0.f;

    FT_Set_Pixel_Sizes(face, 0, FT_UInt(pixelSize));

    FT_UInt g1 = FT_Get_Char_Index(face, FT_ULong(cp1));
    FT_UInt g2 = FT_Get_Char_Index(face, FT_ULong(cp2));

    FT_Vector delta;
    if (FT_Get_Kerning(face, g1, g2, FT_KERNING_DEFAULT, &delta))
        return 0.f;

    return float(delta.x >> 6); // 26.6 → pixels
}

// ─────────────────────────────────────────────────────────────────────────────
// Mat3
// ─────────────────────────────────────────────────────────────────────────────
Canvas2D::Mat3 Canvas2D::Mat3::identity() { return Mat3{}; }

Canvas2D::Mat3 Canvas2D::Mat3::multiply(const Mat3 &b) const
{
    Mat3 r;
    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 3; ++col)
        {
            float s = 0;
            for (int k = 0; k < 3; ++k)
                s += m[row * 3 + k] * b.m[k * 3 + col];
            r.m[row * 3 + col] = s;
        }
    return r;
}
Canvas2D::Mat3 Canvas2D::Mat3::translated(float dx, float dy) const
{
    Mat3 t;
    t.m[2] = dx;
    t.m[5] = dy;
    return this->multiply(t);
}
Canvas2D::Mat3 Canvas2D::Mat3::scaled(float sx, float sy) const
{
    Mat3 s;
    s.m[0] = sx;
    s.m[4] = sy;
    return this->multiply(s);
}
Canvas2D::Mat3 Canvas2D::Mat3::rotated(float a) const
{
    float c = cosf(a), s = sinf(a);
    Mat3 r;
    r.m[0] = c;
    r.m[1] = -s;
    r.m[3] = s;
    r.m[4] = c;
    return this->multiply(r);
}
void Canvas2D::Mat3::apply(float &x, float &y) const
{
    float nx = m[0] * x + m[1] * y + m[2];
    float ny = m[3] * x + m[4] * y + m[5];
    x = nx;
    y = ny;
}

// ─────────────────────────────────────────────────────────────────────────────
// Canvas2D constructor
// ─────────────────────────────────────────────────────────────────────────────
Canvas2D::Canvas2D(Canvas2DGL *gl, int canvasW, int canvasH,
                   const float mvp[16])
    : gl_(gl), canvasW_(canvasW), canvasH_(canvasH)
{
    assert(gl_ && "Canvas2D: Canvas2DGL is null");
    memcpy(baseMVP_, mvp, 64);
    ctm_ = Mat3::identity();
}

void Canvas2D::buildMVP(float out[16]) const
{
    float c[16] = {
        ctm_.m[0], ctm_.m[3], 0, 0,
        ctm_.m[1], ctm_.m[4], 0, 0,
        0, 0, 1, 0,
        ctm_.m[2], ctm_.m[5], 0, 1};

    for (int col = 0; col < 4; ++col)
        for (int row = 0; row < 4; ++row)
        {
            float s = 0;
            for (int k = 0; k < 4; ++k)
                s += baseMVP_[k * 4 + row] * c[col * 4 + k];
            out[col * 4 + row] = s;
        }
}

// ─────────────────────────────────────────────────────────────────────────────
// State stack
// ─────────────────────────────────────────────────────────────────────────────
void Canvas2D::save()
{
    SaveState s;
    s.ctm = ctm_;
    s.fillColor = fillColor_;
    s.strokeColor = strokeColor_;
    s.lineWidth = lineWidth_;
    s.globalAlpha = globalAlpha_;
    s.fillIsGrad = fillIsGrad_;
    s.clipDepth = clipDepth_;
    s.gx0 = gx0_;
    s.gy0 = gy0_;
    s.gx1 = gx1_;
    s.gy1 = gy1_;
    s.gcx = gcx_;
    s.gcy = gcy_;
    s.gInR = gInR_;
    s.gOutR = gOutR_;
    s.gradType = static_cast<SaveState::GradType>(gradType_);
    s.stops = gStops_;
    s.fontFace = fontFace_;
    s.fontSize = fontSize_;
    s.fontBold = fontBold_;
    s.fontItalic = fontItalic_;
    s.textAlign = textAlign_;
    s.textBaseline = textBaseline_;
    stateStack_.push_back(s);
}

void Canvas2D::restore()
{
    if (stateStack_.empty())
        return;
    auto &s = stateStack_.back();
    while (clipDepth_ > s.clipDepth)
    {
        glDisable(GL_SCISSOR_TEST);
        --clipDepth_;
    }
    ctm_ = s.ctm;
    fillColor_ = s.fillColor;
    strokeColor_ = s.strokeColor;
    lineWidth_ = s.lineWidth;
    globalAlpha_ = s.globalAlpha;
    fillIsGrad_ = s.fillIsGrad;
    clipDepth_ = s.clipDepth;
    gx0_ = s.gx0;
    gy0_ = s.gy0;
    gx1_ = s.gx1;
    gy1_ = s.gy1;
    gcx_ = s.gcx;
    gcy_ = s.gcy;
    gInR_ = s.gInR;
    gOutR_ = s.gOutR;
    gradType_ = static_cast<GradType>(s.gradType);
    gStops_ = s.stops;
    fontFace_ = s.fontFace;
    fontSize_ = s.fontSize;
    fontBold_ = s.fontBold;
    fontItalic_ = s.fontItalic;
    textAlign_ = s.textAlign;
    textBaseline_ = s.textBaseline;
    stateStack_.pop_back();
}

// ─────────────────────────────────────────────────────────────────────────────
// Transform
// ─────────────────────────────────────────────────────────────────────────────
void Canvas2D::translate(float dx, float dy) { ctm_ = ctm_.translated(dx, dy); }
void Canvas2D::scale(float sx, float sy) { ctm_ = ctm_.scaled(sx, sy); }
void Canvas2D::rotate(float r) { ctm_ = ctm_.rotated(r); }
void Canvas2D::resetTransform() { ctm_ = Mat3::identity(); }

// ─────────────────────────────────────────────────────────────────────────────
// Style
// ─────────────────────────────────────────────────────────────────────────────
void Canvas2D::setFillColor(Color c)
{
    fillColor_ = c;
    fillIsGrad_ = false;
}
void Canvas2D::setStrokeColor(Color c) { strokeColor_ = c; }
void Canvas2D::setLineWidth(float w) { lineWidth_ = w; }
void Canvas2D::setGlobalAlpha(float a) { globalAlpha_ = std::max(0.f, std::min(1.f, a)); }
void Canvas2D::setMiterLimit(float) { /* no-op */ }
void Canvas2D::setFillRule(FillRule r) { fillRule_ = r; }
void Canvas2D::setLineCap(LineCap) { /* approximated */ }
void Canvas2D::setLineJoin(LineJoin) { /* approximated */ }

void Canvas2D::setCompositeOp(CompositeOp op)
{
    compositeOp_ = op;
    switch (op)
    {
    case CompositeOp::SourceOver:
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        break;
    case CompositeOp::Copy:
        glBlendFunc(GL_ONE, GL_ZERO);
        break;
    default:
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        break;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Gradient
// ─────────────────────────────────────────────────────────────────────────────
void Canvas2D::beginLinearGradient(float x0, float y0, float x1, float y1)
{
    gradType_ = GradType::Linear;
    gx0_ = x0;
    gy0_ = y0;
    gx1_ = x1;
    gy1_ = y1;
    gStops_.clear();
}
void Canvas2D::beginRadialGradient(float cx, float cy, float ir, float outr)
{
    gradType_ = GradType::Radial;
    gcx_ = cx;
    gcy_ = cy;
    gInR_ = ir;
    gOutR_ = outr;
    gStops_.clear();
}
void Canvas2D::addColorStop(float t, Color c) { gStops_.push_back({t, c}); }
void Canvas2D::setFillGradient() { fillIsGrad_ = true; }

// ─────────────────────────────────────────────────────────────────────────────
// Internal: upload verts + draw
// ─────────────────────────────────────────────────────────────────────────────
Color Canvas2D::blendAlpha(Color c) const
{
    c.a = uint8_t(c.a * globalAlpha_);
    return c;
}

void Canvas2D::uploadAndDraw(const float *verts, int count, GLenum mode,
                             Color col, float alpha)
{
    if (!gl_->flatProg || !gl_->vbo)
        return;
    float mvp[16];
    buildMVP(mvp);

    glBindVertexArray(gl_->vao);
    glBindBuffer(GL_ARRAY_BUFFER, gl_->vbo);
    glBufferData(GL_ARRAY_BUFFER, count * 4 * sizeof(float), verts, GL_DYNAMIC_DRAW);

    glUseProgram(gl_->flatProg);
    glUniformMatrix4fv(gl_->flatMVP, 1, GL_FALSE, mvp);
    float a = col.a / 255.f * globalAlpha_ * alpha;
    glUniform4f(gl_->flatColor, col.r / 255.f, col.g / 255.f, col.b / 255.f, a);
    glUniform1i(gl_->flatMode, 0);

    glDrawArrays(mode, 0, count);
    glBindVertexArray(0);
}

// ─────────────────────────────────────────────────────────────────────────────
// Primitives  (identical to old version — no font logic here)
// ─────────────────────────────────────────────────────────────────────────────

void Canvas2D::clearRect(float x, float y, float w, float h)
{
    glDisable(GL_BLEND);
    float v[] = {
        x, y, 0, 0, x + w, y, 1, 0,
        x + w, y + h, 1, 1, x + w, y + h, 1, 1,
        x, y + h, 0, 1, x, y, 0, 0};
    uploadAndDraw(v, 6, GL_TRIANGLES, {0, 0, 0, 0});
    glEnable(GL_BLEND);
}

void Canvas2D::fillRect(float x, float y, float w, float h)
{
    if (fillIsGrad_ && !gStops_.empty())
    {
        Color c0 = gStops_.front().second;
        Color c1 = gStops_.back().second;
        Color cm{uint8_t((c0.r + c1.r) / 2), uint8_t((c0.g + c1.g) / 2),
                 uint8_t((c0.b + c1.b) / 2), uint8_t((c0.a + c1.a) / 2)};
        float v[] = {
            x, y, 0, 0, x + w, y, 1, 0,
            x + w, y + h, 1, 1, x + w, y + h, 1, 1,
            x, y + h, 0, 1, x, y, 0, 0};
        uploadAndDraw(v, 6, GL_TRIANGLES, cm);
        return;
    }
    float v[] = {
        x, y, 0, 0, x + w, y, 1, 0,
        x + w, y + h, 1, 1, x + w, y + h, 1, 1,
        x, y + h, 0, 1, x, y, 0, 0};
    uploadAndDraw(v, 6, GL_TRIANGLES, fillColor_);
}

void Canvas2D::strokeRect(float x, float y, float w, float h)
{
    float lw = lineWidth_;
    auto drawEdge = [&](float ex, float ey, float ew, float eh)
    {
        float v[] = {
            ex, ey, 0, 0, ex + ew, ey, 1, 0,
            ex + ew, ey + eh, 1, 1, ex + ew, ey + eh, 1, 1,
            ex, ey + eh, 0, 1, ex, ey, 0, 0};
        uploadAndDraw(v, 6, GL_TRIANGLES, strokeColor_);
    };
    drawEdge(x, y, w, lw);
    drawEdge(x, y + h - lw, w, lw);
    drawEdge(x, y + lw, lw, h - lw * 2);
    drawEdge(x + w - lw, y + lw, lw, h - lw * 2);
}

static void buildRoundedRectVerts(float x, float y, float w, float h, float r,
                                  int segs, std::vector<float> &out)
{
    r = std::min(r, std::min(w, h) * 0.5f);
    float cx = x + w * 0.5f, cy = y + h * 0.5f;
    out.push_back(cx);
    out.push_back(cy);
    out.push_back(0);
    out.push_back(0);

    auto corner = [&](float ox, float oy, float startA)
    {
        for (int i = 0; i <= segs; ++i)
        {
            float a = startA + float(i) / float(segs) * float(M_PI) * 0.5f;
            out.push_back(ox + cosf(a) * r);
            out.push_back(oy + sinf(a) * r);
            out.push_back(0);
            out.push_back(0);
        }
    };
    corner(x + w - r, y + r, -float(M_PI) * 0.5f);
    corner(x + r, y + r, float(M_PI));
    corner(x + r, y + h - r, float(M_PI) * 0.5f);
    corner(x + w - r, y + h - r, 0.f);
    float a = -float(M_PI) * 0.5f;
    out.push_back(x + w - r + cosf(a) * r);
    out.push_back(y + r + sinf(a) * r);
    out.push_back(0);
    out.push_back(0);
}

void Canvas2D::fillRoundedRect(float x, float y, float w, float h, float r)
{
    std::vector<float> verts;
    buildRoundedRectVerts(x, y, w, h, r, 8, verts);
    uploadAndDraw(verts.data(), int(verts.size() / 4), GL_TRIANGLE_FAN, fillColor_);
}

void Canvas2D::strokeRoundedRect(float x, float y, float w, float h, float r)
{
    r = std::max(0.f, std::min(r, std::min(w, h) * 0.5f));
    int segs = 8;
    std::vector<float> loop;
    auto corner = [&](float ox, float oy, float startA)
    {
        for (int i = 0; i <= segs; ++i)
        {
            float a = startA + float(i) / float(segs) * float(M_PI) * 0.5f;
            loop.push_back(ox + cosf(a) * r);
            loop.push_back(oy + sinf(a) * r);
        }
    };
    corner(x + w - r, y + r, -float(M_PI) * 0.5f);
    corner(x + r, y + r, float(M_PI));
    corner(x + r, y + h - r, float(M_PI) * 0.5f);
    corner(x + w - r, y + h - r, 0.f);

    float lw = lineWidth_ * 0.5f;
    std::vector<float> strip;
    int n = int(loop.size() / 2);
    for (int i = 0; i < n; ++i)
    {
        int j = (i + 1) % n;
        float ax = loop[i * 2], ay = loop[i * 2 + 1];
        float bx = loop[j * 2], by = loop[j * 2 + 1];
        float dx = bx - ax, dy = by - ay;
        float len = sqrtf(dx * dx + dy * dy);
        if (len < 1e-5f)
            continue;
        float nx = -dy / len * lw, ny = dx / len * lw;
        auto push4 = [&](float x_, float y_)
        {
            strip.push_back(x_); strip.push_back(y_);
            strip.push_back(0); strip.push_back(0); };
        push4(ax - nx, ay - ny);
        push4(ax + nx, ay + ny);
        push4(bx + nx, by + ny);
        push4(bx + nx, by + ny);
        push4(bx - nx, by - ny);
        push4(ax - nx, ay - ny);
    }
    uploadAndDraw(strip.data(), int(strip.size() / 4), GL_TRIANGLES, strokeColor_);
}

void Canvas2D::tessellateArc(float cx, float cy, float r,
                             float a0, float a1, bool ccw, bool move)
{
    int segs = std::min(64, std::max(8, int(r * 0.5f)));
    float step = (a1 - a0) / float(segs);
    if (ccw)
        step = -step;
    for (int i = 0; i <= segs; ++i)
    {
        float a = a0 + step * float(i);
        float px = cx + cosf(a) * r, py = cy + sinf(a) * r;
        path_.push_back({px, py, i == 0 && move});
    }
}

void Canvas2D::fillCircle(float cx, float cy, float r)
{
    int segs = std::min(64, std::max(16, int(r)));
    std::vector<float> verts;
    verts.push_back(cx);
    verts.push_back(cy);
    verts.push_back(0);
    verts.push_back(0);
    for (int i = 0; i <= segs; ++i)
    {
        float a = float(i) / float(segs) * 2.f * float(M_PI);
        verts.push_back(cx + cosf(a) * r);
        verts.push_back(cy + sinf(a) * r);
        verts.push_back(0);
        verts.push_back(0);
    }
    uploadAndDraw(verts.data(), int(verts.size() / 4), GL_TRIANGLE_FAN, fillColor_);
}

void Canvas2D::strokeCircle(float cx, float cy, float r)
{
    int segs = std::min(64, std::max(16, int(r)));
    float lw = lineWidth_ * 0.5f;
    std::vector<float> strip;
    for (int i = 0; i <= segs; ++i)
    {
        float a = float(i) / float(segs) * 2.f * float(M_PI);
        float ca = cosf(a), sa = sinf(a);
        strip.push_back((r - lw) * ca + cx);
        strip.push_back((r - lw) * sa + cy);
        strip.push_back(0);
        strip.push_back(0);
        strip.push_back((r + lw) * ca + cx);
        strip.push_back((r + lw) * sa + cy);
        strip.push_back(0);
        strip.push_back(0);
    }
    uploadAndDraw(strip.data(), int(strip.size() / 4), GL_TRIANGLE_STRIP, strokeColor_);
}

// ─────────────────────────────────────────────────────────────────────────────
// Path API
// ─────────────────────────────────────────────────────────────────────────────
void Canvas2D::beginPath() { path_.clear(); }
void Canvas2D::closePath()
{
    if (!path_.empty())
        path_.push_back({pathStartX_, pathStartY_, false});
}
void Canvas2D::moveTo(float x, float y)
{
    pathStartX_ = x;
    pathStartY_ = y;
    curX_ = x;
    curY_ = y;
    path_.push_back({x, y, true});
}
void Canvas2D::lineTo(float x, float y)
{
    curX_ = x;
    curY_ = y;
    path_.push_back({x, y, false});
}
void Canvas2D::arc(float cx, float cy, float r, float a0, float a1, bool ccw)
{
    tessellateArc(cx, cy, r, a0, a1, ccw, false);
}
void Canvas2D::arcTo(float x1, float y1, float x2, float y2, float)
{
    path_.push_back({x1, y1, false});
    path_.push_back({x2, y2, false});
}
void Canvas2D::quadraticCurveTo(float cpx, float cpy, float x, float y)
{
    int n = 8;
    for (int i = 1; i <= n; ++i)
    {
        float t = float(i) / float(n), mt = 1 - t;
        path_.push_back({mt * mt * curX_ + 2 * mt * t * cpx + t * t * x,
                         mt * mt * curY_ + 2 * mt * t * cpy + t * t * y, false});
    }
    curX_ = x;
    curY_ = y;
}
void Canvas2D::bezierCurveTo(float cp1x, float cp1y, float cp2x, float cp2y,
                             float x, float y)
{
    int n = 12;
    for (int i = 1; i <= n; ++i)
    {
        float t = float(i) / float(n), mt = 1 - t;
        path_.push_back({mt * mt * mt * curX_ + 3 * mt * mt * t * cp1x + 3 * mt * t * t * cp2x + t * t * t * x,
                         mt * mt * mt * curY_ + 3 * mt * mt * t * cp1y + 3 * mt * t * t * cp2y + t * t * t * y,
                         false});
    }
    curX_ = x;
    curY_ = y;
}
void Canvas2D::rect(float x, float y, float w, float h)
{
    moveTo(x, y);
    lineTo(x + w, y);
    lineTo(x + w, y + h);
    lineTo(x, y + h);
    closePath();
}
void Canvas2D::ellipse(float cx, float cy, float rx, float ry,
                       float rot, float a0, float a1, bool ccw)
{
    int segs = 32;
    bool first = true;
    float step = (a1 - a0) / float(segs);
    if (ccw)
        step = -step;
    for (int i = 0; i <= segs; ++i)
    {
        float a = a0 + step * float(i);
        float px = cosf(a) * rx, py = sinf(a) * ry;
        path_.push_back({cosf(rot) * px - sinf(rot) * py + cx,
                         sinf(rot) * px + cosf(rot) * py + cy, first});
        first = false;
    }
}

void Canvas2D::fillPath()
{
    if (path_.empty())
        return;
    float sumX = 0, sumY = 0;
    int cnt = 0;
    for (auto &p : path_)
    {
        sumX += p.x;
        sumY += p.y;
        ++cnt;
    }
    if (cnt < 2)
        return;
    float cx2 = sumX / cnt, cy2 = sumY / cnt;

    std::vector<float> verts;
    verts.push_back(cx2);
    verts.push_back(cy2);
    verts.push_back(0);
    verts.push_back(0);
    for (auto &p : path_)
    {
        verts.push_back(p.x);
        verts.push_back(p.y);
        verts.push_back(0);
        verts.push_back(0);
    }
    verts.push_back(path_[0].x);
    verts.push_back(path_[0].y);
    verts.push_back(0);
    verts.push_back(0);

    Color col = (fillIsGrad_ && !gStops_.empty()) ? gStops_.front().second : fillColor_;
    uploadAndDraw(verts.data(), int(verts.size() / 4), GL_TRIANGLE_FAN, col);
}

void Canvas2D::strokePath()
{
    if (path_.size() < 2)
        return;
    float lw = lineWidth_ * 0.5f;
    std::vector<float> strip;
    for (int i = 0; i < (int)path_.size() - 1; ++i)
    {
        float ax = path_[i].x, ay = path_[i].y;
        float bx = path_[i + 1].x, by = path_[i + 1].y;
        float dx = bx - ax, dy = by - ay;
        float len = sqrtf(dx * dx + dy * dy);
        if (len < 1e-5f)
            continue;
        float nx = -dy / len * lw, ny = dx / len * lw;
        auto push4 = [&](float x_, float y_)
        {
            strip.push_back(x_); strip.push_back(y_);
            strip.push_back(0); strip.push_back(0); };
        push4(ax - nx, ay - ny);
        push4(ax + nx, ay + ny);
        push4(bx + nx, by + ny);
        push4(bx + nx, by + ny);
        push4(bx - nx, by - ny);
        push4(ax - nx, ay - ny);
    }
    if (!strip.empty())
        uploadAndDraw(strip.data(), int(strip.size() / 4), GL_TRIANGLES, strokeColor_);
}

void Canvas2D::fill() { fillPath(); }
void Canvas2D::stroke() { strokePath(); }
void Canvas2D::clip() { /* no-op: use pushClipRect */ }

// ─────────────────────────────────────────────────────────────────────────────
// Image drawing
// ─────────────────────────────────────────────────────────────────────────────
Canvas2DImage *Canvas2D::loadImage(const std::string &path)
{
    int w, h, ch;
    stbi_set_flip_vertically_on_load(0);
    unsigned char *data = stbi_load(path.c_str(), &w, &h, &ch, 4);
    if (!data)
        return nullptr;
    auto *img = new Canvas2DImage();
    glGenTextures(1, &img->texId);
    glBindTexture(GL_TEXTURE_2D, img->texId);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    glBindTexture(GL_TEXTURE_2D, 0);
    stbi_image_free(data);
    img->width = w;
    img->height = h;
    return img;
}

Canvas2DImage *Canvas2D::loadImageFromMemory(const unsigned char *data, int byteLen)
{
    int w, h, ch;
    stbi_set_flip_vertically_on_load(0);
    unsigned char *px = stbi_load_from_memory(data, byteLen, &w, &h, &ch, 4);
    if (!px)
        return nullptr;
    auto *img = new Canvas2DImage();
    glGenTextures(1, &img->texId);
    glBindTexture(GL_TEXTURE_2D, img->texId);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    glBindTexture(GL_TEXTURE_2D, 0);
    stbi_image_free(px);
    img->width = w;
    img->height = h;
    return img;
}

Canvas2DImage *Canvas2D::wrapTexture(GLuint texId, int w, int h)
{
    auto *img = new Canvas2DImage();
    img->texId = texId;
    img->width = w;
    img->height = h;
    return img;
}

void Canvas2D::updateTexture(Canvas2DImage *img,
                             const unsigned char *rgba, int w, int h)
{
    if (!img || !img->texId)
        return;
    glBindTexture(GL_TEXTURE_2D, img->texId);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glBindTexture(GL_TEXTURE_2D, 0);
    img->width = w;
    img->height = h;
}

void Canvas2D::freeImage(Canvas2DImage *img)
{
    if (!img)
        return;
    if (img->texId)
        glDeleteTextures(1, &img->texId);
    delete img;
}

void Canvas2D::drawTexturedQuad(GLuint tex,
                                float sx, float sy, float sw, float sh,
                                float dx, float dy, float dw, float dh,
                                int texW, int texH, float alpha)
{
    float u0 = sx / texW, v0 = sy / texH, u1 = (sx + sw) / texW, v1 = (sy + sh) / texH;
    float verts[] = {
        dx, dy, u0, v0,
        dx + dw, dy, u1, v0,
        dx + dw, dy + dh, u1, v1,
        dx + dw, dy + dh, u1, v1,
        dx, dy + dh, u0, v1,
        dx, dy, u0, v0};

    float mvp[16];
    buildMVP(mvp);
    glBindVertexArray(gl_->vao);
    glBindBuffer(GL_ARRAY_BUFFER, gl_->vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_DYNAMIC_DRAW);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    glUseProgram(gl_->flatProg);
    glUniformMatrix4fv(gl_->flatMVP, 1, GL_FALSE, mvp);
    glUniform1i(gl_->flatMode, 2);
    glUniform4f(gl_->flatColor, 1, 1, 1, alpha * globalAlpha_);
    glUniform1i(glGetUniformLocation(gl_->flatProg, "uTex"), 0);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindVertexArray(0);
}

void Canvas2D::drawImage(const Canvas2DImage *img, float dx, float dy)
{
    if (!img || !img->texId)
        return;
    drawTexturedQuad(img->texId, 0, 0, float(img->width), float(img->height),
                     dx, dy, float(img->width), float(img->height),
                     img->width, img->height);
}
void Canvas2D::drawImage(const Canvas2DImage *img,
                         float dx, float dy, float dw, float dh)
{
    if (!img || !img->texId)
        return;
    drawTexturedQuad(img->texId, 0, 0, float(img->width), float(img->height),
                     dx, dy, dw, dh, img->width, img->height);
}
void Canvas2D::drawImage(const Canvas2DImage *img,
                         float sx, float sy, float sw, float sh,
                         float dx, float dy, float dw, float dh)
{
    if (!img || !img->texId)
        return;
    drawTexturedQuad(img->texId, sx, sy, sw, sh, dx, dy, dw, dh, img->width, img->height);
}

// ─────────────────────────────────────────────────────────────────────────────
// Font / text
// ─────────────────────────────────────────────────────────────────────────────
bool Canvas2D::registerFont(Canvas2DGL *gl,
                            const std::string &name,
                            const std::string &path)
{
    return gl->addFont(name, path) >= 0;
}

void Canvas2D::parseFontDesc(const std::string &desc)
{
    fontBold_ = false;
    fontItalic_ = false;
    fontSize_ = 14.f;
    fontFace_ = "sans";
    std::istringstream ss(desc);
    std::string tok, family;
    while (ss >> tok)
    {
        if (tok == "bold")
        {
            fontBold_ = true;
            continue;
        }
        if (tok == "italic")
        {
            fontItalic_ = true;
            continue;
        }
        if (tok.size() > 2 && tok.compare(tok.size() - 2, 2, "px") == 0)
        {
            fontSize_ = std::stof(tok.substr(0, tok.size() - 2));
            std::getline(ss, family);
            auto start = family.find_first_not_of(" \t");
            if (start != std::string::npos)
                family = family.substr(start);
            break;
        }
    }
    if (!family.empty())
        fontFace_ = family;
}

void Canvas2D::setFont(const std::string &desc) { parseFontDesc(desc); }
void Canvas2D::setTextAlign(CanvasTextAlign a) { textAlign_ = a; }
void Canvas2D::setTextBaseline(TextBaseline b) { textBaseline_ = b; }

int Canvas2D::resolveFont() const
{
    if (fontBold_ && fontItalic_)
    {
        int i = gl_->findFont(fontFace_ + "-bold-italic");
        if (i >= 0)
            return i;
    }
    if (fontBold_)
    {
        int i = gl_->findFont(fontFace_ + "-bold");
        if (i >= 0)
            return i;
    }
    if (fontItalic_)
    {
        int i = gl_->findFont(fontFace_ + "-italic");
        if (i >= 0)
            return i;
    }
    int i = gl_->findFont(fontFace_);
    if (i >= 0)
        return i;
    if (!gl_->fonts.empty())
        return 0;
    return -1;
}

void Canvas2D::fillText(const std::string &text, float x, float y, float /*maxW*/)
{
    if (text.empty())
        return;
    int fontIdx = resolveFont();
    if (fontIdx < 0)
        return;
    int ps = std::max(4, int(fontSize_ + 0.5f));

    FT_Face face = gl_->fonts[fontIdx].ftFace;
    FT_Set_Pixel_Sizes(face, 0, FT_UInt(ps));

    // Ascender for baseline adjustment (in pixels)
    float scale = float(ps) / float(face->units_per_EM);
    float ascender = float(face->ascender) * scale;

    // Measure total width for alignment
    float totalW = 0;
    for (char ch : text)
    {
        auto *g = gl_->getGlyph(fontIdx, (unsigned char)ch, ps);
        if (g)
            totalW += float(g->advance);
    }

    float px = x;
    if (textAlign_ == CanvasTextAlign::Center)
        px = x - totalW * 0.5f;
    else if (textAlign_ == CanvasTextAlign::Right)
        px = x - totalW;

    float py = y;
    if (textBaseline_ == TextBaseline::Top)
        py = y + ascender;
    else if (textBaseline_ == TextBaseline::Middle)
        py = y + ascender * 0.5f;
    else if (textBaseline_ == TextBaseline::Bottom)
    {
        float descender = float(face->descender) * scale;
        py = y + descender;
    }

    float mvp[16];
    buildMVP(mvp);
    glUseProgram(gl_->flatProg);
    glUniformMatrix4fv(gl_->flatMVP, 1, GL_FALSE, mvp);
    glUniform1i(gl_->flatMode, 1);
    float a = fillColor_.a / 255.f * globalAlpha_;
    glUniform4f(gl_->flatColor,
                fillColor_.r / 255.f, fillColor_.g / 255.f, fillColor_.b / 255.f, a);
    glUniform1i(glGetUniformLocation(gl_->flatProg, "uTex"), 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, gl_->fontTex);
    glBindVertexArray(gl_->vao);

    float cx_ = px;
    for (char ch : text)
    {
        auto *g = gl_->getGlyph(fontIdx, (unsigned char)ch, ps);
        if (!g)
            continue;
        if (g->glyphW > 0 && g->glyphH > 0)
        {
            float gx = cx_ + float(g->xoff);
            float gy = py + float(g->yoff);
            float u0 = float(g->atlasX) / float(gl_->atlasW);
            float v0 = float(g->atlasY) / float(gl_->atlasH);
            float u1 = float(g->atlasX + g->glyphW) / float(gl_->atlasW);
            float v1 = float(g->atlasY + g->glyphH) / float(gl_->atlasH);
            float verts[] = {
                gx, gy, u0, v0,
                gx + g->glyphW, gy, u1, v0,
                gx + g->glyphW, gy + g->glyphH, u1, v1,
                gx + g->glyphW, gy + g->glyphH, u1, v1,
                gx, gy + g->glyphH, u0, v1,
                gx, gy, u0, v0};
            glBindBuffer(GL_ARRAY_BUFFER, gl_->vbo);
            glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_DYNAMIC_DRAW);
            glDrawArrays(GL_TRIANGLES, 0, 6);
        }
        cx_ += float(g->advance);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindVertexArray(0);
}

void Canvas2D::strokeText(const std::string &text, float x, float y, float maxW)
{
    Color saved = fillColor_;
    setFillColor(strokeColor_);
    fillText(text, x - 1, y, maxW);
    fillText(text, x + 1, y, maxW);
    fillText(text, x, y - 1, maxW);
    fillText(text, x, y + 1, maxW);
    setFillColor(saved);
    fillText(text, x, y, maxW);
}

float Canvas2D::measureText(const std::string &text)
{
    if (text.empty())
        return 0.f;
    int fontIdx = resolveFont();
    if (fontIdx < 0)
        return float(text.size()) * fontSize_ * 0.6f;
    int ps = std::max(4, int(fontSize_ + 0.5f));
    float total = 0;
    for (char ch : text)
    {
        auto *g = gl_->getGlyph(fontIdx, (unsigned char)ch, ps);
        if (g)
            total += float(g->advance);
    }
    return total;
}

// ─────────────────────────────────────────────────────────────────────────────
// Clip rect (GL scissor)
// ─────────────────────────────────────────────────────────────────────────────
void Canvas2D::pushClipRect(float x, float y, float w, float h)
{
    float x0 = x, y0 = y;
    ctm_.apply(x0, y0);
    float x1 = x + w, y1 = y + h;
    ctm_.apply(x1, y1);

    float scaleX = baseMVP_[0], scaleY = baseMVP_[5];
    float transX = baseMVP_[12], transY = baseMVP_[13];
    float viewW = 2.f / fabsf(scaleX), viewH = 2.f / fabsf(scaleY);

    auto toPixX = [&](float cx)
    { return (scaleX * cx + transX + 1.f) * 0.5f * viewW; };
    auto toPixY = [&](float cy)
    { return (scaleY * cy + transY + 1.f) * 0.5f * viewH; };
    float px0 = toPixX(x0), py0 = toPixY(y0);
    float px1 = toPixX(x1), py1 = toPixY(y1);
    if (px0 > px1)
        std::swap(px0, px1);
    if (py0 > py1)
        std::swap(py0, py1);
    glEnable(GL_SCISSOR_TEST);
    glScissor(GLint(px0), GLint(py0), GLsizei(px1 - px0), GLsizei(py1 - py0));
    ++clipDepth_;
    save();
}

void Canvas2D::popClipRect()
{
    restore();
    if (clipDepth_ <= 0)
        glDisable(GL_SCISSOR_TEST);
}

// ─────────────────────────────────────────────────────────────────────────────
// Pixel access
// ─────────────────────────────────────────────────────────────────────────────
void Canvas2D::getImageData(float x, float y, float w, float h,
                            std::vector<uint8_t> &out)
{
    int iw = int(w), ih = int(h);
    out.resize(size_t(iw) * ih * 4);
    glReadPixels(GLint(x), GLint(y), iw, ih, GL_RGBA, GL_UNSIGNED_BYTE, out.data());
    for (int row = 0; row < ih / 2; ++row)
    {
        uint8_t *a = out.data() + row * iw * 4;
        uint8_t *b = out.data() + (ih - 1 - row) * iw * 4;
        for (int i = 0; i < iw * 4; ++i)
            std::swap(a[i], b[i]);
    }
}

void Canvas2D::putImageData(const std::vector<uint8_t> &data,
                            int srcW, int srcH, float dx, float dy)
{
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, srcW, srcH, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, data.data());
    glBindTexture(GL_TEXTURE_2D, 0);
    drawTexturedQuad(tex, 0, 0, float(srcW), float(srcH),
                     dx, dy, float(srcW), float(srcH), srcW, srcH);
    glDeleteTextures(1, &tex);
}