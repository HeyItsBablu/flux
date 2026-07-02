// flux_painter_android.cpp

#ifdef __ANDROID__

#include "flux/flux_painter.hpp"
#include "flux/flux_text_style.hpp"
#include <GLES2/gl2.h>
#include <android/log.h>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>

#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, "FluxGL", __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "FluxGL", __VA_ARGS__)

// ============================================================================
// STB TRUETYPE  (include once in this TU)
// ============================================================================

#include "stb_truetype.h"

// ============================================================================
// FORWARD DECLARATIONS from other Android TUs
// ============================================================================
extern float FluxAndroid_getDpiScale();

// ============================================================================
// OVERLAY OFFSET HELPERS
// ============================================================================
// Every Painter method that emits geometry adds these to its x/y arguments.
// When a Painter is used for normal (non-overlay) rendering both fields are
// zero, so there is no cost and no behavioural change outside of overlays.

static inline int offX(const GraphicsContext &ctx) { return ctx.overlayOffsetX; }
static inline int offY(const GraphicsContext &ctx) { return ctx.overlayOffsetY; }

// ============================================================================
// UTF-8 HELPERS
// ============================================================================
static std::string wstringToUtf8(const std::wstring &text)
{
    std::string utf8;
    utf8.reserve(text.size() * 4);
    for (wchar_t wc : text)
    {
        uint32_t cp = static_cast<uint32_t>(wc);
        if (cp < 0x80)
        {
            utf8 += static_cast<char>(cp);
        }
        else if (cp < 0x800)
        {
            utf8 += static_cast<char>(0xC0 | (cp >> 6));
            utf8 += static_cast<char>(0x80 | (cp & 0x3F));
        }
        else if (cp < 0x10000)
        {
            utf8 += static_cast<char>(0xE0 | (cp >> 12));
            utf8 += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            utf8 += static_cast<char>(0x80 | (cp & 0x3F));
        }
        else
        {
            utf8 += static_cast<char>(0xF0 | (cp >> 18));
            utf8 += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            utf8 += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            utf8 += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }
    return utf8;
}

// Decode one UTF-8 codepoint; advances *p past it. Returns 0xFFFD on error.
static uint32_t utf8Next(const char *&p, const char *end)
{
    if (p >= end)
        return 0;
    unsigned char c = static_cast<unsigned char>(*p++);
    if (c < 0x80)
        return c;
    uint32_t cp;
    int extra;
    if ((c & 0xE0) == 0xC0)
    {
        cp = c & 0x1F;
        extra = 1;
    }
    else if ((c & 0xF0) == 0xE0)
    {
        cp = c & 0x0F;
        extra = 2;
    }
    else if ((c & 0xF8) == 0xF0)
    {
        cp = c & 0x07;
        extra = 3;
    }
    else
        return 0xFFFD;
    while (extra-- && p < end)
    {
        unsigned char b = static_cast<unsigned char>(*p++);
        if ((b & 0xC0) != 0x80)
            return 0xFFFD;
        cp = (cp << 6) | (b & 0x3F);
    }
    return cp;
}

// UTF-8 byte length of string, codepoint count
static int utf8Strlen(const std::string &s)
{
    const char *p = s.c_str();
    const char *e = p + s.size();
    int n = 0;
    while (p < e)
    {
        utf8Next(p, e);
        ++n;
    }
    return n;
}

// ============================================================================
// SHADER SOURCE
// ============================================================================

// ── Solid color (colored quads, lines, shapes) ────────────────────────────────
static const char *kColorVS = R"(
attribute vec2 aPos;
attribute vec4 aColor;
uniform vec2 uViewport;   // (width, height) in logical px
uniform vec2 uScale;      // DPI or custom scale
varying vec4 vColor;
void main() {
    // map logical px -> NDC
    vec2 ndc = (aPos / uViewport) * 2.0 - 1.0;
    ndc.y = -ndc.y;
    gl_Position = vec4(ndc, 0.0, 1.0);
    vColor = aColor;
}
)";

static const char *kColorFS = R"(
precision mediump float;
varying vec4 vColor;
void main() {
    gl_FragColor = vColor;
}
)";

// ── Textured quad (images, video, camera) ────────────────────────────────────
static const char *kTexVS = R"(
attribute vec2 aPos;
attribute vec2 aUV;
uniform vec2 uViewport;
varying vec2 vUV;
void main() {
    vec2 ndc = (aPos / uViewport) * 2.0 - 1.0;
    ndc.y = -ndc.y;
    gl_Position = vec4(ndc, 0.0, 1.0);
    vUV = aUV;
}
)";

static const char *kTexFS = R"(
precision mediump float;
uniform sampler2D uTex;
uniform float uAlpha;
varying vec2 vUV;
void main() {
    gl_FragColor = texture2D(uTex, vUV) * uAlpha;
}
)";

// ── Glyph atlas (alpha-only texture, colored via uniform) ─────────────────────
static const char *kGlyphVS = R"(
attribute vec2 aPos;
attribute vec2 aUV;
uniform vec2 uViewport;
varying vec2 vUV;
void main() {
    vec2 ndc = (aPos / uViewport) * 2.0 - 1.0;
    ndc.y = -ndc.y;
    gl_Position = vec4(ndc, 0.0, 1.0);
    vUV = aUV;
}
)";

static const char *kGlyphFS = R"(
precision mediump float;
uniform sampler2D uAtlas;
uniform vec4 uColor;
varying vec2 vUV;
void main() {
    float a = texture2D(uAtlas, vUV).r;
    gl_FragColor = vec4(uColor.rgb, uColor.a * a);
}
)";

// ── Rounded rect SDF (for filled rounded rects & borders) ────────────────────
static const char *kRoundVS = R"(
attribute vec2 aPos;     // screen-space position
attribute vec2 aLocal;   // position relative to rect center
uniform vec2 uViewport;
varying vec2 vLocal;
void main() {
    vec2 ndc = (aPos / uViewport) * 2.0 - 1.0;
    ndc.y = -ndc.y;
    gl_Position = vec4(ndc, 0.0, 1.0);
    vLocal = aLocal;
}
)";

static const char *kRoundFS = R"(
precision mediump float;
varying vec2 vLocal;
uniform vec2  uHalfSize;   // half-width, half-height of the rect
uniform float uRadius;     // corner radius
uniform vec4  uColor;
uniform float uBorder;     // 0 = fill, >0 = border width
void main() {
    vec2  q   = abs(vLocal) - uHalfSize + vec2(uRadius);
    float d   = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - uRadius;
    float aa  = fwidth(d);
    float alpha = 1.0 - smoothstep(-aa, aa, d);
    if (uBorder > 0.0) {
        float inner = d + uBorder;
        float alphaInner = 1.0 - smoothstep(-aa, aa, inner);
        alpha = alpha - alphaInner;
    }
    gl_FragColor = vec4(uColor.rgb, uColor.a * clamp(alpha, 0.0, 1.0));
}
)";

static const char *kRoundFSExt = R"(#extension GL_OES_standard_derivatives : enable
precision mediump float;
uniform vec2  uHalfSize;
uniform float uRadius;
uniform vec4  uColor;
uniform float uBorder;
void main() {
    vec2  q   = abs(vLocal) - uHalfSize + vec2(uRadius);
    float d   = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - uRadius;
    float aa  = fwidth(d);
    float alpha = 1.0 - smoothstep(-aa, aa, d);
    if (uBorder > 0.0) {
        float inner = d + uBorder;
        float alphaInner = 1.0 - smoothstep(-aa, aa, inner);
        alpha = alpha - alphaInner;
    }
    gl_FragColor = vec4(uColor.rgb, uColor.a * clamp(alpha, 0.0, 1.0));
}
)";

// ============================================================================
// SHADER HELPERS
// ============================================================================
static GLuint compileShader(GLenum type, const char *src)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
        char buf[512];
        glGetShaderInfoLog(s, 512, nullptr, buf);
        LOGW("Shader compile error: %s", buf);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

static GLuint linkProgram(const char *vs, const char *fs)
{
    GLuint v = compileShader(GL_VERTEX_SHADER, vs);
    GLuint f = compileShader(GL_FRAGMENT_SHADER, fs);
    if (!v || !f)
    {
        glDeleteShader(v);
        glDeleteShader(f);
        return 0;
    }
    GLuint p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);
    glDetachShader(p, v);
    glDetachShader(p, f);
    glDeleteShader(v);
    glDeleteShader(f);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok)
    {
        char buf[512];
        glGetProgramInfoLog(p, 512, nullptr, buf);
        LOGW("Program link error: %s", buf);
        glDeleteProgram(p);
        return 0;
    }
    return p;
}

// ============================================================================
// GLYPH ATLAS
// ============================================================================
static constexpr int kAtlasW = 1024;
static constexpr int kAtlasH = 1024;
static constexpr int kMaxGlyphs = 4096;

struct GlyphKey
{
    uint32_t codepoint;
    int size;      // px
    int fontIndex; // index into s_fonts[]
    bool operator==(const GlyphKey &o) const
    {
        return codepoint == o.codepoint && size == o.size && fontIndex == o.fontIndex;
    }
};

struct GlyphEntry
{
    GlyphKey key;
    // atlas UV (in pixels)
    int ax, ay, aw, ah;
    // bearing
    int bearingX, bearingY;
    int advance;
    bool valid = false;
};

// Simple open-addressing hash map for glyph cache
static constexpr int kGlyphMapSize = 8192; // must be power of 2
struct GlyphMap
{
    GlyphEntry entries[kGlyphMapSize];
    bool used[kGlyphMapSize]{};

    int find(const GlyphKey &k) const
    {
        uint32_t h = (k.codepoint * 2654435761u ^ (uint32_t)k.size ^ (uint32_t)k.fontIndex);
        int idx = (int)(h & (kGlyphMapSize - 1));
        for (int i = 0; i < kGlyphMapSize; ++i)
        {
            int slot = (idx + i) & (kGlyphMapSize - 1);
            if (!used[slot])
                return -1;
            if (entries[slot].key == k)
                return slot;
        }
        return -1;
    }

    GlyphEntry *insert(const GlyphKey &k)
    {
        uint32_t h = (k.codepoint * 2654435761u ^ (uint32_t)k.size ^ (uint32_t)k.fontIndex);
        int idx = (int)(h & (kGlyphMapSize - 1));
        for (int i = 0; i < kGlyphMapSize; ++i)
        {
            int slot = (idx + i) & (kGlyphMapSize - 1);
            if (!used[slot])
            {
                used[slot] = true;
                entries[slot].key = k;
                return &entries[slot];
            }
        }
        return nullptr; // full — caller falls back
    }
};

// ============================================================================
// FONT STORE  (up to 16 loaded TTF fonts)
// ============================================================================
static constexpr int kMaxFonts = 16;

struct FontEntry
{
    bool loaded = false;
    std::string name;
    std::vector<uint8_t> data;
    stbtt_fontinfo info{};
};

// ============================================================================
// CLIP STACK
// ============================================================================
struct ClipRect
{
    int x, y, w, h;
};

// ============================================================================
// FluxGL — singleton renderer (one per app)
// ============================================================================
struct FluxGL
{
    // ── Shaders ───────────────────────────────────────────────────────────────
    GLuint progColor = 0;
    GLuint progTex = 0;
    GLuint progGlyph = 0;
    GLuint progRound = 0;

    // Uniform locations
    struct
    {
        GLint viewport, scale;
    } uColor;
    struct
    {
        GLint viewport, tex, alpha;
    } uTex;
    struct
    {
        GLint viewport, atlas, color;
    } uGlyph;
    struct
    {
        GLint viewport, halfSize, radius, color, border;
    } uRound;

    // ── Vertex buffer (shared, re-uploaded each draw) ─────────────────────────
    GLuint vbo = 0;

    // ── Glyph atlas texture ───────────────────────────────────────────────────
    GLuint atlasTex = 0;
    int atlasX = 0; // current pack cursor
    int atlasY = 0;
    int atlasRowH = 0;
    std::vector<uint8_t> atlasPixels; // CPU copy for upload

    // ── Glyph cache ───────────────────────────────────────────────────────────
    GlyphMap glyphMap;

    // ── Font store ────────────────────────────────────────────────────────────
    FontEntry fonts[kMaxFonts];
    int fontCount = 0;

    // ── Frame state ───────────────────────────────────────────────────────────
    float vpW = 1.f, vpH = 1.f;
    float dpi = 1.f;

    // ── Clip stack ────────────────────────────────────────────────────────────
    std::vector<ClipRect> clipStack;

    // ── Dirty flag for atlas ──────────────────────────────────────────────────
    bool atlasDirty = false;

    // ── Init ──────────────────────────────────────────────────────────────────
    bool init()
    {
        // Color program
        progColor = linkProgram(kColorVS, kColorFS);
        if (progColor)
        {
            uColor.viewport = glGetUniformLocation(progColor, "uViewport");
        }

        // Tex program
        progTex = linkProgram(kTexVS, kTexFS);
        if (progTex)
        {
            uTex.viewport = glGetUniformLocation(progTex, "uViewport");
            uTex.tex = glGetUniformLocation(progTex, "uTex");
            uTex.alpha = glGetUniformLocation(progTex, "uAlpha");
        }

        // Glyph program
        progGlyph = linkProgram(kGlyphVS, kGlyphFS);
        if (progGlyph)
        {
            uGlyph.viewport = glGetUniformLocation(progGlyph, "uViewport");
            uGlyph.atlas = glGetUniformLocation(progGlyph, "uAtlas");
            uGlyph.color = glGetUniformLocation(progGlyph, "uColor");
        }

        // Rounded rect program (try with extension first)
        progRound = linkProgram(kRoundVS, kRoundFSExt);
        if (!progRound)
            progRound = linkProgram(kRoundVS, kRoundFS);
        if (progRound)
        {
            uRound.viewport = glGetUniformLocation(progRound, "uViewport");
            uRound.halfSize = glGetUniformLocation(progRound, "uHalfSize");
            uRound.radius = glGetUniformLocation(progRound, "uRadius");
            uRound.color = glGetUniformLocation(progRound, "uColor");
            uRound.border = glGetUniformLocation(progRound, "uBorder");
        }

        // Shared VBO
        glGenBuffers(1, &vbo);

        // Atlas texture (R8)
        atlasPixels.assign(kAtlasW * kAtlasH, 0);
        glGenTextures(1, &atlasTex);
        glBindTexture(GL_TEXTURE_2D, atlasTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE,
                     kAtlasW, kAtlasH, 0,
                     GL_LUMINANCE, GL_UNSIGNED_BYTE, atlasPixels.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);

        LOGI("FluxGL initialized");
        return progColor && progTex && progGlyph && progRound;
    }

    void destroy()
    {
        if (progColor)
        {
            glDeleteProgram(progColor);
            progColor = 0;
        }
        if (progTex)
        {
            glDeleteProgram(progTex);
            progTex = 0;
        }
        if (progGlyph)
        {
            glDeleteProgram(progGlyph);
            progGlyph = 0;
        }
        if (progRound)
        {
            glDeleteProgram(progRound);
            progRound = 0;
        }
        if (vbo)
        {
            glDeleteBuffers(1, &vbo);
            vbo = 0;
        }
        if (atlasTex)
        {
            glDeleteTextures(1, &atlasTex);
            atlasTex = 0;
        }
        // Invalidate glyph handles — font data stays (fonts are re-registered after reinit)
        memset(&glyphMap, 0, sizeof(glyphMap));
        atlasX = atlasY = atlasRowH = 0;
        atlasPixels.assign(kAtlasW * kAtlasH, 0);
    }

    // ── Font registration ──────────────────────────────────────────────────────
    int registerFont(const std::string &name, const std::string &path)
    {
        // Already registered?
        for (int i = 0; i < fontCount; ++i)
            if (fonts[i].name == name)
                return i;

        if (fontCount >= kMaxFonts)
        {
            LOGW("Font table full");
            return -1;
        }

        FILE *f = fopen(path.c_str(), "rb");
        if (!f)
        {
            LOGW("Cannot open font: %s", path.c_str());
            return -1;
        }
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        FontEntry &fe = fonts[fontCount];
        fe.data.resize(sz);
        fread(fe.data.data(), 1, sz, f);
        fclose(f);

        if (!stbtt_InitFont(&fe.info, fe.data.data(),
                            stbtt_GetFontOffsetForIndex(fe.data.data(), 0)))
        {
            LOGW("stbtt_InitFont failed: %s", path.c_str());
            fe.data.clear();
            return -1;
        }
        fe.name = name;
        fe.loaded = true;
        LOGI("Registered font '%s' index=%d", name.c_str(), fontCount);
        return fontCount++;
    }

    int findFont(const std::string &name) const
    {
        for (int i = 0; i < fontCount; ++i)
            if (fonts[i].name == name)
                return i;
        return -1;
    }

    // ── Glyph atlas packing ───────────────────────────────────────────────────
    const GlyphEntry *getGlyph(int fontIndex, uint32_t cp, int sizePx)
    {
        if (fontIndex < 0 || fontIndex >= fontCount)
            return nullptr;
        GlyphKey key{cp, sizePx, fontIndex};
        int slot = glyphMap.find(key);
        if (slot >= 0)
            return &glyphMap.entries[slot];

        FontEntry &fe = fonts[fontIndex];
        float scale = stbtt_ScaleForPixelHeight(&fe.info, (float)sizePx);

        int x0, y0, x1, y1;
        stbtt_GetCodepointBitmapBox(&fe.info, (int)cp, scale, scale,
                                    &x0, &y0, &x1, &y1);
        int gw = x1 - x0, gh = y1 - y0;

        int advance = 0, lsb = 0;
        stbtt_GetCodepointHMetrics(&fe.info, (int)cp, &advance, &lsb);

        if (gw > 0 && gh > 0)
        {
            if (atlasX + gw + 1 > kAtlasW)
            {
                atlasX = 0;
                atlasY += atlasRowH + 1;
                atlasRowH = 0;
            }
            if (atlasY + gh + 1 > kAtlasH)
            {
                LOGW("Glyph atlas full — consider increasing atlas size");
                return nullptr;
            }
            stbtt_MakeCodepointBitmap(&fe.info,
                                      atlasPixels.data() + atlasY * kAtlasW + atlasX,
                                      gw, gh, kAtlasW, scale, scale, (int)cp);

            if (gh > atlasRowH)
                atlasRowH = gh;

            GlyphEntry *e = glyphMap.insert(key);
            if (!e)
                return nullptr;
            e->ax = atlasX;
            e->ay = atlasY;
            e->aw = gw;
            e->ah = gh;
            e->bearingX = x0;
            e->bearingY = y0;
            e->advance = (int)(advance * scale);
            e->valid = true;
            atlasX += gw + 1;
            atlasDirty = true;
            return e;
        }

        // Space / zero-size glyph
        GlyphEntry *e = glyphMap.insert(key);
        if (!e)
            return nullptr;
        e->ax = e->ay = e->aw = e->ah = 0;
        e->bearingX = x0;
        e->bearingY = y0;
        e->advance = (int)(advance * scale);
        e->valid = true;
        return e;
    }

    void flushAtlas()
    {
        if (!atlasDirty)
            return;
        glBindTexture(GL_TEXTURE_2D, atlasTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE,
                     kAtlasW, kAtlasH, 0,
                     GL_LUMINANCE, GL_UNSIGNED_BYTE, atlasPixels.data());
        glBindTexture(GL_TEXTURE_2D, 0);
        atlasDirty = false;
    }

    // ── Frame begin ───────────────────────────────────────────────────────────
    void beginFrame(float w, float h, float dpiScale)
    {
        vpW = w;
        vpH = h;
        dpi = dpiScale;
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        clipStack.clear();
    }

    // ── Clip ──────────────────────────────────────────────────────────────────
    void pushScissor(int x, int y, int w, int h)
    {
        int px = (int)(x * dpi), py = (int)(y * dpi);
        int pw = (int)(w * dpi), ph = (int)(h * dpi);
        int physH = (int)(vpH * dpi);
        glEnable(GL_SCISSOR_TEST);
        glScissor(px, physH - py - ph, pw, ph);
        clipStack.push_back({x, y, w, h});
    }

    void popScissor()
    {
        if (!clipStack.empty())
            clipStack.pop_back();
        if (clipStack.empty())
        {
            glDisable(GL_SCISSOR_TEST);
        }
        else
        {
            auto &r = clipStack.back();
            int physH = (int)(vpH * dpi);
            glScissor((int)(r.x * dpi), physH - (int)(r.y * dpi) - (int)(r.h * dpi),
                      (int)(r.w * dpi), (int)(r.h * dpi));
        }
    }

    // ── Color helper ──────────────────────────────────────────────────────────
    static void setUniformColor(GLint loc, Color c)
    {
        glUniform4f(loc, c.r / 255.f, c.g / 255.f, c.b / 255.f, c.a / 255.f);
    }

    // ── uploadVerts ───────────────────────────────────────────────────────────
    void uploadVerts(const void *data, GLsizeiptr bytes)
    {
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, bytes, data, GL_DYNAMIC_DRAW);
    }

    // ============================================================================
    // DRAW: Filled rect
    // ============================================================================
    void drawFilledRect(float x, float y, float w, float h, Color c)
    {
        if (w <= 0 || h <= 0)
            return;
        float r = c.r / 255.f, g = c.g / 255.f, b = c.b / 255.f, a = c.a / 255.f;
        float verts[] = {
            x,
            y,
            r,
            g,
            b,
            a,
            x + w,
            y,
            r,
            g,
            b,
            a,
            x,
            y + h,
            r,
            g,
            b,
            a,
            x + w,
            y,
            r,
            g,
            b,
            a,
            x + w,
            y + h,
            r,
            g,
            b,
            a,
            x,
            y + h,
            r,
            g,
            b,
            a,
        };
        glUseProgram(progColor);
        glUniform2f(uColor.viewport, vpW, vpH);
        uploadVerts(verts, sizeof(verts));
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void *)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void *)(2 * sizeof(float)));
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glDisableVertexAttribArray(0);
        glDisableVertexAttribArray(1);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }

    // ============================================================================
    // DRAW: Gradient rect
    // ============================================================================
    void drawGradientRect(float x, float y, float w, float h, Color c0, Color c1)
    {
        auto toF = [](Color c, float *out)
        {
            out[0] = c.r / 255.f;
            out[1] = c.g / 255.f;
            out[2] = c.b / 255.f;
            out[3] = c.a / 255.f;
        };
        float a[4], b2[4];
        toF(c0, a);
        toF(c1, b2);
        float verts[6 * 6];
        float *p = verts;
        auto v = [&](float px, float py, float *col)
        {
            *p++ = px;
            *p++ = py;
            *p++ = col[0];
            *p++ = col[1];
            *p++ = col[2];
            *p++ = col[3];
        };
        v(x, y, a);
        v(x + w, y, b2);
        v(x, y + h, a);
        v(x + w, y, b2);
        v(x + w, y + h, b2);
        v(x, y + h, a);

        glUseProgram(progColor);
        glUniform2f(uColor.viewport, vpW, vpH);
        uploadVerts(verts, sizeof(verts));
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void *)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void *)(2 * sizeof(float)));
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glDisableVertexAttribArray(0);
        glDisableVertexAttribArray(1);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }

    // ============================================================================
    // DRAW: Rounded rect (SDF shader)
    // ============================================================================
    void drawRoundedRect(float x, float y, float w, float h,
                         float radius, Color color, float border = 0.f)
    {
        if (w <= 0 || h <= 0 || !progRound)
        {
            if (!progRound)
                drawFilledRect(x, y, w, h, color);
            return;
        }
        radius = std::min(radius, std::min(w, h) * 0.5f);
        float cx = x + w * 0.5f, cy = y + h * 0.5f;
        float hw = w * 0.5f, hh = h * 0.5f;

        float verts[] = {
            x,
            y,
            -hw,
            -hh,
            x + w,
            y,
            hw,
            -hh,
            x,
            y + h,
            -hw,
            hh,
            x + w,
            y,
            hw,
            -hh,
            x + w,
            y + h,
            hw,
            hh,
            x,
            y + h,
            -hw,
            hh,
        };
        glUseProgram(progRound);
        glUniform2f(uRound.viewport, vpW, vpH);
        glUniform2f(uRound.halfSize, hw, hh);
        glUniform1f(uRound.radius, radius);
        glUniform1f(uRound.border, border);
        setUniformColor(uRound.color, color);

        uploadVerts(verts, sizeof(verts));
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)(2 * sizeof(float)));
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glDisableVertexAttribArray(0);
        glDisableVertexAttribArray(1);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }

    // ============================================================================
    // DRAW: Line (as thin quad)
    // ============================================================================
    void drawLine(float x1, float y1, float x2, float y2, Color color, float width)
    {
        float dx = x2 - x1, dy = y2 - y1;
        float len = sqrtf(dx * dx + dy * dy);
        if (len < 0.001f)
            return;
        float nx = -dy / len * (width * 0.5f);
        float ny = dx / len * (width * 0.5f);
        float r = color.r / 255.f, g = color.g / 255.f,
              b = color.b / 255.f, a = color.a / 255.f;
        float verts[] = {
            x1 + nx,
            y1 + ny,
            r,
            g,
            b,
            a,
            x1 - nx,
            y1 - ny,
            r,
            g,
            b,
            a,
            x2 + nx,
            y2 + ny,
            r,
            g,
            b,
            a,
            x1 - nx,
            y1 - ny,
            r,
            g,
            b,
            a,
            x2 - nx,
            y2 - ny,
            r,
            g,
            b,
            a,
            x2 + nx,
            y2 + ny,
            r,
            g,
            b,
            a,
        };
        glUseProgram(progColor);
        glUniform2f(uColor.viewport, vpW, vpH);
        uploadVerts(verts, sizeof(verts));
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void *)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void *)(2 * sizeof(float)));
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glDisableVertexAttribArray(0);
        glDisableVertexAttribArray(1);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }

    // ============================================================================
    // DRAW: Ellipse (tessellated triangle fan)
    // ============================================================================
    void drawEllipse(float cx, float cy, float rx, float ry,
                     Color fill, Color stroke, float strokeW)
    {
        const int segs = 48;
        std::vector<float> verts;
        verts.reserve((segs + 2) * 6);
        float fr = fill.r / 255.f, fg = fill.g / 255.f,
              fb = fill.b / 255.f, fa = fill.a / 255.f;
        verts.push_back(cx);
        verts.push_back(cy);
        verts.push_back(fr);
        verts.push_back(fg);
        verts.push_back(fb);
        verts.push_back(fa);
        for (int i = 0; i <= segs; ++i)
        {
            float angle = (float)i / segs * 2.f * (float)M_PI;
            verts.push_back(cx + cosf(angle) * rx);
            verts.push_back(cy + sinf(angle) * ry);
            verts.push_back(fr);
            verts.push_back(fg);
            verts.push_back(fb);
            verts.push_back(fa);
        }
        glUseProgram(progColor);
        glUniform2f(uColor.viewport, vpW, vpH);
        uploadVerts(verts.data(), verts.size() * sizeof(float));
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void *)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void *)(2 * sizeof(float)));
        glDrawArrays(GL_TRIANGLE_FAN, 0, segs + 2);

        if (strokeW > 0.f && stroke.a > 0)
        {
            float sr = stroke.r / 255.f, sg = stroke.g / 255.f,
                  sb = stroke.b / 255.f, sa = stroke.a / 255.f;
            std::vector<float> sv;
            sv.reserve(segs * 12);
            for (int i = 0; i < segs; ++i)
            {
                float a0 = (float)i / segs * 2.f * (float)M_PI;
                float a1 = (float)(i + 1) / segs * 2.f * (float)M_PI;
                float x0o = cx + cosf(a0) * rx, y0o = cy + sinf(a0) * ry;
                float x1o = cx + cosf(a1) * rx, y1o = cy + sinf(a1) * ry;
                float x0i = cx + cosf(a0) * (rx - strokeW), y0i = cy + sinf(a0) * (ry - strokeW);
                float x1i = cx + cosf(a1) * (rx - strokeW), y1i = cy + sinf(a1) * (ry - strokeW);
                auto push = [&](float px, float py)
                {
                    sv.push_back(px);
                    sv.push_back(py);
                    sv.push_back(sr);
                    sv.push_back(sg);
                    sv.push_back(sb);
                    sv.push_back(sa);
                };
                push(x0o, y0o);
                push(x1o, y1o);
                push(x0i, y0i);
                push(x1o, y1o);
                push(x1i, y1i);
                push(x0i, y0i);
            }
            uploadVerts(sv.data(), sv.size() * sizeof(float));
            glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void *)0);
            glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void *)(2 * sizeof(float)));
            glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(sv.size() / 6));
        }

        glDisableVertexAttribArray(0);
        glDisableVertexAttribArray(1);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }

    // ============================================================================
    // DRAW: Arc (stroke only, triangle strip)
    // ============================================================================
    void drawArc(float cx, float cy, float radius, float strokeW,
                 float startAngle, float sweepAngle, Color color, bool /*rounded*/)
    {
        const int segs = std::max(12, (int)(fabsf(sweepAngle) * radius / 2.f));
        float r = color.r / 255.f, g = color.g / 255.f,
              b = color.b / 255.f, a = color.a / 255.f;
        float innerR = radius - strokeW;
        std::vector<float> verts;
        verts.reserve((segs + 1) * 12);
        for (int i = 0; i <= segs; ++i)
        {
            float angle = startAngle + sweepAngle * (float)i / segs;
            float c2 = cosf(angle), s2 = sinf(angle);
            verts.push_back(cx + c2 * radius);
            verts.push_back(cy + s2 * radius);
            verts.push_back(r);
            verts.push_back(g);
            verts.push_back(b);
            verts.push_back(a);
            verts.push_back(cx + c2 * innerR);
            verts.push_back(cy + s2 * innerR);
            verts.push_back(r);
            verts.push_back(g);
            verts.push_back(b);
            verts.push_back(a);
        }
        glUseProgram(progColor);
        glUniform2f(uColor.viewport, vpW, vpH);
        uploadVerts(verts.data(), verts.size() * sizeof(float));
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void *)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void *)(2 * sizeof(float)));
        glDrawArrays(GL_TRIANGLE_STRIP, 0, (GLsizei)(verts.size() / 6));
        glDisableVertexAttribArray(0);
        glDisableVertexAttribArray(1);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }

    // ============================================================================
    // DRAW: Polyline
    // ============================================================================
    void drawPolyline(const std::vector<std::pair<int, int>> &pts,
                      Color color, float strokeW)
    {
        if (pts.size() < 2)
            return;
        for (size_t i = 0; i + 1 < pts.size(); ++i)
            drawLine(pts[i].first, pts[i].second,
                     pts[i + 1].first, pts[i + 1].second,
                     color, strokeW);
    }

    // ============================================================================
    // DRAW: Polygon (triangle fan)
    // ============================================================================
    void drawPolygon(const std::vector<std::pair<int, int>> &pts, Color color)
    {
        if (pts.size() < 3)
            return;
        float r = color.r / 255.f, g = color.g / 255.f,
              b = color.b / 255.f, a = color.a / 255.f;
        std::vector<float> verts;
        verts.reserve(pts.size() * 6);
        for (auto &[px, py] : pts)
        {
            verts.push_back((float)px);
            verts.push_back((float)py);
            verts.push_back(r);
            verts.push_back(g);
            verts.push_back(b);
            verts.push_back(a);
        }
        glUseProgram(progColor);
        glUniform2f(uColor.viewport, vpW, vpH);
        uploadVerts(verts.data(), verts.size() * sizeof(float));
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void *)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void *)(2 * sizeof(float)));
        glDrawArrays(GL_TRIANGLE_FAN, 0, (GLsizei)pts.size());
        glDisableVertexAttribArray(0);
        glDisableVertexAttribArray(1);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }

    // ============================================================================
    // DRAW: Textured quad
    // ============================================================================
    void drawTexture(GLuint tex, float dstX, float dstY, float dstW, float dstH,
                     float u0 = 0.f, float v0 = 0.f, float u1 = 1.f, float v1 = 1.f,
                     float alpha = 1.f)
    {
        float verts[] = {
            dstX,
            dstY,
            u0,
            v0,
            dstX + dstW,
            dstY,
            u1,
            v0,
            dstX,
            dstY + dstH,
            u0,
            v1,
            dstX + dstW,
            dstY,
            u1,
            v0,
            dstX + dstW,
            dstY + dstH,
            u1,
            v1,
            dstX,
            dstY + dstH,
            u0,
            v1,
        };
        glUseProgram(progTex);
        glUniform2f(uTex.viewport, vpW, vpH);
        glUniform1i(uTex.tex, 0);
        glUniform1f(uTex.alpha, alpha);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, tex);
        uploadVerts(verts, sizeof(verts));
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)(2 * sizeof(float)));
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glDisableVertexAttribArray(0);
        glDisableVertexAttribArray(1);
        glBindTexture(GL_TEXTURE_2D, 0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }

    // ============================================================================
    // DRAW: Text line (UTF-8, returns total advance width)
    // ============================================================================
    int drawTextLine(const char *utf8, int len,
                     float x, float y,
                     int fontIdx, int sizePx,
                     Color color, float letterSpacing = 0.f)
    {
        if (fontIdx < 0 || fontIdx >= fontCount)
            return 0;
        flushAtlas();

        struct GlyphVert
        {
            float x, y, u, v;
        };
        std::vector<GlyphVert> verts;
        verts.reserve(len * 6);

        float cx = x;
        const char *p = utf8;
        const char *end = utf8 + len;

        while (p < end)
        {
            uint32_t cp = utf8Next(p, end);
            const GlyphEntry *g = getGlyph(fontIdx, cp, sizePx);
            if (!g)
                continue;

            if (g->aw > 0 && g->ah > 0)
            {
                float gx = cx + g->bearingX;
                float gy = y + g->bearingY;
                float u0 = g->ax / (float)kAtlasW;
                float v0 = g->ay / (float)kAtlasH;
                float u1 = (g->ax + g->aw) / (float)kAtlasW;
                float v1 = (g->ay + g->ah) / (float)kAtlasH;
                verts.push_back({gx, gy, u0, v0});
                verts.push_back({gx + g->aw, gy, u1, v0});
                verts.push_back({gx, gy + g->ah, u0, v1});
                verts.push_back({gx + g->aw, gy, u1, v0});
                verts.push_back({gx + g->aw, gy + g->ah, u1, v1});
                verts.push_back({gx, gy + g->ah, u0, v1});
            }
            cx += g->advance + letterSpacing;
        }

        if (verts.empty())
            return (int)(cx - x);

        flushAtlas();

        glUseProgram(progGlyph);
        glUniform2f(uGlyph.viewport, vpW, vpH);
        glUniform1i(uGlyph.atlas, 0);
        glUniform4f(uGlyph.color,
                    color.r / 255.f, color.g / 255.f,
                    color.b / 255.f, color.a / 255.f);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, atlasTex);
        uploadVerts(verts.data(), verts.size() * sizeof(GlyphVert));
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(GlyphVert), (void *)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(GlyphVert), (void *)(2 * sizeof(float)));
        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)verts.size());
        glDisableVertexAttribArray(0);
        glDisableVertexAttribArray(1);
        glBindTexture(GL_TEXTURE_2D, 0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);

        return (int)(cx - x);
    }

    // ============================================================================
    // MEASURE: Text line width (px), no drawing
    // ============================================================================
    int measureLine(const char *utf8, int len,
                    int fontIdx, int sizePx, float letterSpacing = 0.f)
    {
        if (fontIdx < 0 || fontIdx >= fontCount)
            return 0;
        FontEntry &fe = fonts[fontIdx];
        float scale = stbtt_ScaleForPixelHeight(&fe.info, (float)sizePx);
        const char *p = utf8;
        const char *end = utf8 + len;
        float cx = 0;
        while (p < end)
        {
            uint32_t cp = utf8Next(p, end);
            int advance = 0, lsb = 0;
            stbtt_GetCodepointHMetrics(&fe.info, (int)cp, &advance, &lsb);
            cx += advance * scale + letterSpacing;
        }
        return (int)cx;
    }

    // ============================================================================
    // METRICS: ascent/descent/lineGap for a font+size
    // ============================================================================
    void getMetrics(int fontIdx, int sizePx,
                    int &ascent, int &descent, int &lineGap)
    {
        ascent = descent = lineGap = 0;
        if (fontIdx < 0 || fontIdx >= fontCount)
            return;
        FontEntry &fe = fonts[fontIdx];
        float scale = stbtt_ScaleForPixelHeight(&fe.info, (float)sizePx);
        int a, d, lg;
        stbtt_GetFontVMetrics(&fe.info, &a, &d, &lg);
        ascent = (int)(a * scale);
        descent = (int)(d * scale);
        lineGap = (int)(lg * scale);
    }
};

// ============================================================================
// SINGLETON
// ============================================================================
static FluxGL *s_gl = nullptr;

void FluxGL_init()
{
    if (!s_gl)
    {
        s_gl = new FluxGL();
        if (!s_gl->init())
            LOGW("FluxGL::init() had errors");
    }
}

void FluxGL_destroy()
{
    if (s_gl)
    {
        s_gl->destroy();
        delete s_gl;
        s_gl = nullptr;
    }
}

void FluxGL_reinit()
{
    if (!s_gl)
    {
        FluxGL_init();
        return;
    }
    s_gl->destroy();
    s_gl->init();
}

void FluxGL_beginFrame(float w, float h, float dpi)
{
    if (s_gl)
        s_gl->beginFrame(w, h, dpi);
}

int FluxGL_registerFont(const std::string &name, const std::string &path)
{
    if (!s_gl)
        return -1;
    return s_gl->registerFont(name, path);
}

int FluxGL_findFont(const std::string &name)
{
    if (!s_gl)
        return -1;
    return s_gl->findFont(name);
}

FluxGL *FluxGL_get() { return s_gl; }

// ============================================================================
// FONT HANDLE HELPERS
// ============================================================================

static int resolveFont(NativeFont fontHandle)
{
    if (!fontHandle)
        return (s_gl && s_gl->fontCount > 0) ? 0 : -1;
    auto *f = reinterpret_cast<FluxAndroidFont *>(fontHandle);
    return f->nvgHandle; // repurposed: glFontIndex
}

static int resolveFontSize(NativeFont fontHandle, int fallback = 16)
{
    if (!fontHandle)
        return fallback;
    auto *f = reinterpret_cast<FluxAndroidFont *>(fontHandle);
    return (int)f->size;
}

// ============================================================================
// PAINTER IMPLEMENTATION
// ============================================================================

// ── Filled shapes ─────────────────────────────────────────────────────────────

void Painter::fillRect(int x, int y, int w, int h, Color color)
{
    if (!s_gl)
        return;
    s_gl->drawFilledRect(x + offX(ctx), y + offY(ctx), w, h, color);
}

void Painter::fillRectAlpha(int x, int y, int w, int h, Color color)
{
    fillRect(x, y, w, h, color); // delegates — offset applied there
}

void Painter::fillRoundedRect(int x, int y, int w, int h, int radius, Color color)
{
    if (!s_gl)
        return;
    s_gl->drawRoundedRect(x + offX(ctx), y + offY(ctx), w, h, (float)radius, color, 0.f);
}

void Painter::fillRoundedRegion(int x, int y, int w, int h, int r, Color color)
{
    fillRoundedRect(x, y, w, h, r, color); // delegates
}

void Painter::fillRoundedRectGDI(int x, int y, int w, int h, int r,
                                 Color fill, Color stroke, int sw)
{
    fillRoundedRect(x, y, w, h, r / 2, fill); // delegates
    if (sw > 0 && stroke.a > 0)
        drawBorder(x, y, w, h, r / 2, stroke, sw); // delegates
}

void Painter::fillRectWithLeftAccent(int x, int y, int w, int h,
                                     Color bg, Color accent, int strip)
{
    fillRect(x, y, w, h, bg);         // delegates
    fillRect(x, y, strip, h, accent); // delegates
}

void Painter::fillGradientRect(int x, int y, int w, int h,
                               const std::vector<Color> &colors)
{
    if (!s_gl || colors.empty())
        return;
    if (colors.size() == 1)
    {
        fillRect(x, y, w, h, colors[0]);
        return;
    }
    s_gl->drawGradientRect(x + offX(ctx), y + offY(ctx), w, h,
                           colors.front(), colors.back());
}

void Painter::fillColumnBars(int x, int y, int w, int h,
                             const std::vector<int> &bars, Color color)
{
    if (!s_gl)
        return;
    int cols = std::min(w, (int)bars.size());
    for (int i = 0; i < cols; ++i)
    {
        int bh = std::max(0, std::min(h, bars[i]));
        if (bh > 0)
            s_gl->drawFilledRect(x + offX(ctx) + i, y + offY(ctx) + h - bh, 1, bh, color);
    }
}

void Painter::fillPolygonAlpha(const std::vector<std::pair<int, int>> &pts, Color color)
{
    if (!s_gl)
        return;
    std::vector<std::pair<int, int>> shifted;
    shifted.reserve(pts.size());
    for (auto &[px, py] : pts)
        shifted.push_back({px + offX(ctx), py + offY(ctx)});
    s_gl->drawPolygon(shifted, color);
}

// ── Stroked shapes ────────────────────────────────────────────────────────────

void Painter::drawBorder(int x, int y, int w, int h, int radius,
                         Color color, int borderWidth)
{
    if (!s_gl)
        return;
    if (radius <= 0)
    {
        int ox = offX(ctx), oy = offY(ctx);
        s_gl->drawFilledRect(x + ox, y + oy, w, borderWidth, color);
        s_gl->drawFilledRect(x + ox, y + oy + h - borderWidth, w, borderWidth, color);
        s_gl->drawFilledRect(x + ox, y + oy, borderWidth, h, color);
        s_gl->drawFilledRect(x + ox + w - borderWidth, y + oy, borderWidth, h, color);
    }
    else
    {
        s_gl->drawRoundedRect(x + offX(ctx), y + offY(ctx), w, h,
                              (float)radius, color, (float)borderWidth);
    }
}

void Painter::drawRectOutline(int x, int y, int w, int h, Color color, int sw)
{
    drawBorder(x, y, w, h, 0, color, sw); // delegates
}

void Painter::drawRoundedRectOutline(int x, int y, int w, int h,
                                     int r, Color stroke, int sw)
{
    drawBorder(x, y, w, h, r, stroke, sw); // delegates
}

void Painter::drawLine(int x1, int y1, int x2, int y2, Color color, int width)
{
    if (!s_gl)
        return;
    s_gl->drawLine(x1 + offX(ctx), y1 + offY(ctx),
                   x2 + offX(ctx), y2 + offY(ctx),
                   color, (float)width);
}

void Painter::drawHLine(int x, int y, int len, Color color, int sw)
{
    drawLine(x, y, x + len, y, color, sw); // delegates
}

void Painter::drawVLine(int x, int y, int len, Color color, int sw)
{
    drawLine(x, y, x, y + len, color, sw); // delegates
}

void Painter::drawPolyline(const std::vector<std::pair<int, int>> &pts,
                           Color color, int strokeWidth)
{
    if (!s_gl)
        return;
    std::vector<std::pair<int, int>> shifted;
    shifted.reserve(pts.size());
    for (auto &[px, py] : pts)
        shifted.push_back({px + offX(ctx), py + offY(ctx)});
    s_gl->drawPolyline(shifted, color, (float)strokeWidth);
}

void Painter::drawEllipse(int x, int y, int w, int h,
                          Color fill, Color stroke, int strokeWidth)
{
    if (!s_gl)
        return;
    float cx = (x + offX(ctx)) + w * 0.5f;
    float cy = (y + offY(ctx)) + h * 0.5f;
    s_gl->drawEllipse(cx, cy, w * 0.5f, h * 0.5f, fill, stroke, (float)strokeWidth);
}

void Painter::drawArc(float cx, float cy, float radius,
                      int strokeWidth,
                      float startAngle, float sweepAngle,
                      Color color, bool roundedCaps)
{
    if (!s_gl)
        return;
    s_gl->drawArc(cx + offX(ctx), cy + offY(ctx), radius, (float)strokeWidth,
                  startAngle, sweepAngle, color, roundedCaps);
}

// ── Clip ──────────────────────────────────────────────────────────────────────

void Painter::pushClipRect(int x, int y, int w, int h, int /*cornerRadius*/)
{
    if (s_gl)
        s_gl->pushScissor(x + offX(ctx), y + offY(ctx), w, h);
}

void Painter::popClipRect()
{
    if (s_gl)
        s_gl->popScissor();
}

void Painter::pushClipRoundedRect(int x, int y, int w, int h, int /*r*/)
{
    pushClipRect(x, y, w, h); // GL scissor is rect-only
}

// ── Stubs for D2D-only features ───────────────────────────────────────────────
void Painter::drawShadow(int, int, int, int, int, int, Color, int, int) {}
void Painter::beginLayer(float) {}
void Painter::endLayer() {}

// ── Wavy / fade / decoration helpers ─────────────────────────────────────────
// These all delegate to methods already offset above — no direct s_gl calls.

void Painter::drawWavyLine(int x, int y, int len, Color color, int amplitude)
{
    if (len <= 0)
        return;
    const int step = amplitude * 2;
    std::vector<std::pair<int, int>> pts;
    pts.reserve(len / step + 2);
    int px = x;
    bool up = true;
    while (px < x + len)
    {
        pts.push_back({px, up ? y - amplitude : y + amplitude});
        px += step;
        up = !up;
    }
    pts.push_back({x + len, y});
    if (pts.size() >= 2)
        drawPolyline(pts, color, 1);
}

void Painter::drawFadeOverlay(int x, int y, int w, int h,
                              int fadeWidth, Color bg)
{
    if (fadeWidth <= 0 || w <= 0 || h <= 0)
        return;
    int startX = x + w - fadeWidth;
    if (startX < x)
        startX = x;
    std::vector<Color> stops = {bg.withAlpha(0), bg.withAlpha(255)};
    fillGradientRect(startX, y, fadeWidth, h, stops);
}

void Painter::drawTextDecorationLine(int lineX, int lineY, int lineW,
                                     const TextStyle &style, TextDecoration which)
{
    if (lineW <= 0)
        return;
    int fontSize = style.scaledFontSize();
    int ascent = (int)(fontSize * 0.75f);
    int thickness = style.decorationThickness;
    Color dc = style.decorationColor;

    int decorY = lineY;
    if (which == TextDecoration::Underline)
        decorY = lineY + ascent + 1;
    else if (which == TextDecoration::Overline)
        decorY = lineY;
    else if (which == TextDecoration::LineThrough)
        decorY = lineY + ascent - ascent / 3;

    switch (style.decorationStyle)
    {
    case TextDecorationStyle::Solid:
        drawHLine(lineX, decorY, lineW, dc, thickness);
        break;
    case TextDecorationStyle::Double:
        drawHLine(lineX, decorY, lineW, dc, thickness);
        drawHLine(lineX, decorY + 2, lineW, dc, thickness);
        break;
    case TextDecorationStyle::Dotted:
        for (int p = lineX; p < lineX + lineW; p += 4)
            drawHLine(p, decorY, std::min(2, lineX + lineW - p), dc, thickness);
        break;
    case TextDecorationStyle::Dashed:
        for (int p = lineX; p < lineX + lineW; p += 8)
            drawHLine(p, decorY, std::min(5, lineX + lineW - p), dc, thickness);
        break;
    case TextDecorationStyle::Wavy:
        drawWavyLine(lineX, decorY, lineW, dc, 2);
        break;
    }
}

// ============================================================================
// TEXT — low-level drawText / measureText
// ============================================================================

void Painter::drawText(const std::wstring &text, int x, int y, int w, int h,
                       NativeFont fontHandle, Color color, UINT format)
{
    if (!s_gl || text.empty())
        return;
    std::string utf8 = wstringToUtf8(text);
    int fontIdx = resolveFont(fontHandle);
    int sizePx = resolveFontSize(fontHandle);

    int tw = s_gl->measureLine(utf8.c_str(), (int)utf8.size(), fontIdx, sizePx);

    int ascent, descent, lineGap;
    s_gl->getMetrics(fontIdx, sizePx, ascent, descent, lineGap);
    int th = ascent - descent;

    // Start from the (already-logical) x/y and add the overlay offset.
    int tx = x + offX(ctx);
    if (format & DT_CENTER)
        tx = x + offX(ctx) + (w - tw) / 2;
    else if (format & DT_RIGHT)
        tx = x + offX(ctx) + w - tw;

    int ty = (format & DT_VCENTER)
                 ? y + offY(ctx) + (h - th) / 2 + ascent
                 : y + offY(ctx) + ascent;

    s_gl->drawTextLine(utf8.c_str(), (int)utf8.size(),
                       (float)tx, (float)ty, fontIdx, sizePx, color);
}

void Painter::drawTextA(const std::string &text, int x, int y, int w, int h,
                        NativeFont font, Color color, UINT format)
{
    std::wstring wide;
    wide.reserve(text.size());
    for (unsigned char c : text)
        wide += static_cast<wchar_t>(c);
    drawText(wide, x, y, w, h, font, color, format);
}

void Painter::measureText(const std::wstring &text, NativeFont fontHandle,
                          int &outW, int &outH)
{
    outW = outH = 0;
    if (!s_gl || text.empty())
        return;
    std::string utf8 = wstringToUtf8(text);
    int fontIdx = resolveFont(fontHandle);
    int sizePx = resolveFontSize(fontHandle);
    outW = s_gl->measureLine(utf8.c_str(), (int)utf8.size(), fontIdx, sizePx);
    int a, d, lg;
    s_gl->getMetrics(fontIdx, sizePx, a, d, lg);
    outH = a - d;
}

// ============================================================================
// RICH TEXT HELPERS
// ============================================================================

struct LineSpanGL
{
    int start, length;
};

static std::vector<LineSpanGL> wrapText(const std::string &utf8,
                                        int maxWidth, bool softWrap,
                                        int fontIdx, int sizePx,
                                        float letterSpacing)
{
    std::vector<LineSpanGL> lines;
    const int n = (int)utf8.size();
    if (n == 0)
        return lines;

    int pos = 0;
    while (pos < n)
    {
        int nlPos = pos;
        while (nlPos < n && utf8[nlPos] != '\n')
            ++nlPos;

        if (!softWrap || maxWidth <= 0)
        {
            lines.push_back({pos, nlPos - pos});
        }
        else
        {
            int lineStart = pos;
            while (lineStart < nlPos)
            {
                int lo = 0, hi = nlPos - lineStart, fit = 0;
                while (lo <= hi)
                {
                    int mid = (lo + hi) / 2;
                    int w2 = s_gl->measureLine(utf8.c_str() + lineStart, mid,
                                               fontIdx, sizePx, letterSpacing);
                    if (w2 <= maxWidth)
                    {
                        fit = mid;
                        lo = mid + 1;
                    }
                    else
                        hi = mid - 1;
                }
                if (fit == 0)
                    fit = 1;

                int breakAt = fit;
                if (lineStart + fit < nlPos)
                {
                    int wb = fit;
                    while (wb > 1 && utf8[lineStart + wb - 1] != ' ')
                        --wb;
                    if (wb > 1)
                        breakAt = wb;
                }

                lines.push_back({lineStart, breakAt});
                lineStart += breakAt;
                while (lineStart < nlPos && utf8[lineStart] == ' ')
                    ++lineStart;
            }
        }
        pos = nlPos + 1;
        if (nlPos == n)
            break;
    }
    return lines;
}

// ============================================================================
// Painter::measureRichText
// ============================================================================
void Painter::measureRichText(const std::wstring &text,
                              const TextStyle &style,
                              FontCache &fontCache,
                              int maxWidth, bool softWrap, int maxLines,
                              int &outWidth, int &outHeight)
{
    outWidth = outHeight = 0;
    if (!s_gl || text.empty())
        return;

    NativeFont font = fontCache.getFont(style.fontFamily, style.scaledFontSize(), style.fontWeight);
    int fontIdx = resolveFont(font);
    int sizePx = style.scaledFontSize();
    std::string utf8 = wstringToUtf8(text);

    auto lines = wrapText(utf8, maxWidth, softWrap, fontIdx, sizePx, style.letterSpacing);

    int ascent, descent, lineGap;
    s_gl->getMetrics(fontIdx, sizePx, ascent, descent, lineGap);
    int lineH = ascent - descent + lineGap;
    int lineHeightPx = (int)(lineH * style.height);

    int total = (maxLines > 0) ? std::min((int)lines.size(), maxLines) : (int)lines.size();
    for (int i = 0; i < total; ++i)
    {
        int w2 = s_gl->measureLine(utf8.c_str() + lines[i].start, lines[i].length,
                                   fontIdx, sizePx, style.letterSpacing);
        outWidth = std::max(outWidth, w2);
    }
    outHeight = total * lineHeightPx;
}

// ============================================================================
// Painter::drawRichText
// ============================================================================
void Painter::drawRichText(const std::wstring &text,
                           const RichTextParams &params,
                           FontCache &fontCache)
{
    if (!s_gl || text.empty() || params.w <= 0 || params.h <= 0)
        return;

    // Apply overlay offset to a local copy of the params rect.
    // All geometry emitted below goes through drawTextLine / fillRect /
    // drawPolyline, but those delegate to s_gl directly rather than back
    // through Painter, so we must bake the offset here.
    RichTextParams p = params;
    p.x += offX(ctx);
    p.y += offY(ctx);

    const TextStyle &style = p.style;
    NativeFont font = fontCache.getFont(style.fontFamily, style.scaledFontSize(), style.fontWeight);
    int fontIdx = resolveFont(font);
    int sizePx = style.scaledFontSize();
    std::string utf8 = wstringToUtf8(text);

    int wrapWidth = p.softWrap ? p.w : 0;
    auto lines = wrapText(utf8, wrapWidth, p.softWrap,
                          fontIdx, sizePx, style.letterSpacing);

    int ascent, descent, lineGap;
    s_gl->getMetrics(fontIdx, sizePx, ascent, descent, lineGap);
    int lineH = ascent - descent + lineGap;
    int lineHeightPx = (int)(lineH * style.height);
    if (lineHeightPx == 0)
        lineHeightPx = sizePx;

    int totalLines = (p.maxLines > 0)
                         ? std::min((int)lines.size(), p.maxLines)
                         : (int)lines.size();

    int blockH = totalLines * lineHeightPx;
    int startY = p.y;
    switch (p.textAlignVertical)
    {
    case TextAlignVertical::Center:
        startY = p.y + (p.h - blockH) / 2;
        break;
    case TextAlignVertical::Bottom:
        startY = p.y + p.h - blockH;
        break;
    default:
        break;
    }

    bool needClip = (p.overflow != TextOverflow::Visible);
    if (needClip)
    {
        // pushClipRect would double-add the offset, so call s_gl directly
        // with already-shifted coords.
        s_gl->pushScissor(p.x, p.y, p.w, p.h);
    }

    for (int i = 0; i < totalLines; ++i)
    {
        const auto &span = lines[i];
        int lineY = startY + i * lineHeightPx;
        int drawY = lineY + ascent;

        if (lineY + lineHeightPx < p.y)
            continue;
        if (lineY > p.y + p.h)
            break;

        int lineW = s_gl->measureLine(utf8.c_str() + span.start, span.length,
                                      fontIdx, sizePx, style.letterSpacing);

        bool isRTL = (p.direction == TextDirection::RTL);
        int lineX = p.x;
        switch (p.textAlign)
        {
        case TextAlign::Right:
        case TextAlign::End:
            lineX = isRTL ? p.x : (p.x + p.w - lineW);
            break;
        case TextAlign::Center:
            lineX = p.x + (p.w - lineW) / 2;
            break;
        default:
            lineX = isRTL ? (p.x + p.w - lineW) : p.x;
            break;
        }

        bool isLastVisible = (i == totalLines - 1);
        bool hasMoreLines = ((int)lines.size() > totalLines);

        // Background
        if (style.backgroundColor.has_value())
            s_gl->drawFilledRect(lineX, lineY, lineW, lineHeightPx,
                                 *style.backgroundColor);

        // Shadows
        for (const auto &sh : style.shadows)
        {
            s_gl->drawTextLine(utf8.c_str() + span.start, span.length,
                               (float)(lineX + sh.offsetX),
                               (float)(drawY + sh.offsetY),
                               fontIdx, sizePx, sh.color, style.letterSpacing);
        }

        // Ellipsis
        if (isLastVisible && hasMoreLines &&
            p.overflow == TextOverflow::Ellipsis)
        {
            std::string lineText(utf8.c_str() + span.start, span.length);
            static const std::string ellipsis = "\xe2\x80\xa6";
            while (!lineText.empty())
            {
                int tw = s_gl->measureLine((lineText + ellipsis).c_str(),
                                           (int)(lineText.size() + ellipsis.size()),
                                           fontIdx, sizePx, style.letterSpacing);
                if (tw <= p.w || lineText.size() == 1)
                    break;
                lineText.pop_back();
                while (!lineText.empty() &&
                       (static_cast<unsigned char>(lineText.back()) & 0xC0) == 0x80)
                    lineText.pop_back();
            }
            lineText += ellipsis;
            s_gl->drawTextLine(lineText.c_str(), (int)lineText.size(),
                               (float)lineX, (float)drawY,
                               fontIdx, sizePx, style.color, style.letterSpacing);
        }
        else
        {
            s_gl->drawTextLine(utf8.c_str() + span.start, span.length,
                               (float)lineX, (float)drawY,
                               fontIdx, sizePx, style.color, style.letterSpacing);
        }

        // Decorations — these call back through Painter methods which would
        // double-apply the offset via offX/offY. We use lineX/lineY which
        // already carry the offset, so call s_gl directly.
        auto drawDecoLine = [&](int dx, int dy, int dw, TextDecoration which)
        {
            int fontSize = style.scaledFontSize();
            int asc = (int)(fontSize * 0.75f);
            int thickness = style.decorationThickness;
            Color dc = style.decorationColor;
            int decorY = dy;
            if (which == TextDecoration::Underline)
                decorY = dy + asc + 1;
            else if (which == TextDecoration::Overline)
                decorY = dy;
            else if (which == TextDecoration::LineThrough)
                decorY = dy + asc - asc / 3;

            switch (style.decorationStyle)
            {
            case TextDecorationStyle::Solid:
                s_gl->drawLine(dx, decorY, dx + dw, decorY, dc, thickness);
                break;
            case TextDecorationStyle::Double:
                s_gl->drawLine(dx, decorY, dx + dw, decorY, dc, thickness);
                s_gl->drawLine(dx, decorY + 2, dx + dw, decorY + 2, dc, thickness);
                break;
            case TextDecorationStyle::Dotted:
                for (int pp = dx; pp < dx + dw; pp += 4)
                    s_gl->drawLine(pp, decorY, pp + std::min(2, dx + dw - pp), decorY, dc, thickness);
                break;
            case TextDecorationStyle::Dashed:
                for (int pp = dx; pp < dx + dw; pp += 8)
                    s_gl->drawLine(pp, decorY, pp + std::min(5, dx + dw - pp), decorY, dc, thickness);
                break;
            case TextDecorationStyle::Wavy:
            {
                const int amp = 2;
                const int step = amp * 2;
                std::vector<std::pair<int, int>> wpts;
                int wx = dx;
                bool up = true;
                while (wx < dx + dw)
                {
                    wpts.push_back({wx, up ? decorY - amp : decorY + amp});
                    wx += step;
                    up = !up;
                }
                wpts.push_back({dx + dw, decorY});
                s_gl->drawPolyline(wpts, dc, 1.f);
                break;
            }
            }
        };

        if (style.hasOverline())
            drawDecoLine(lineX, lineY, lineW, TextDecoration::Overline);
        if (style.hasUnderline())
            drawDecoLine(lineX, lineY, lineW, TextDecoration::Underline);
        if (style.hasLineThrough())
            drawDecoLine(lineX, lineY, lineW, TextDecoration::LineThrough);

        // Fade overlay
        if (isLastVisible && hasMoreLines &&
            p.overflow == TextOverflow::Fade)
        {
            int fadeW = std::min(60, p.w / 3);
            int startX = p.x + p.w - fadeW;
            std::vector<Color> stops = {Color::fromRGBA(255, 255, 255, 0),
                                        Color::fromRGBA(255, 255, 255, 255)};
            // Call s_gl directly — coords already offset
            s_gl->drawGradientRect(startX, lineY, fadeW, lineHeightPx,
                                   stops.front(), stops.back());
        }
    }

    if (needClip)
        s_gl->popScissor();
}

void Painter::drawRichTextA(const std::string &text,
                            const RichTextParams &params,
                            FontCache &fontCache)
{
    if (text.empty())
        return;
    std::wstring wide;
    wide.reserve(text.size());
    for (unsigned char c : text)
        wide += static_cast<wchar_t>(c);
    drawRichText(wide, params, fontCache);
}

// ============================================================================
// IMAGE / VIDEO / CAMERA / PAGE
// ============================================================================

void Painter::drawImage(const ImageDrawParams &p)
{
    if (!s_gl || p.image == -1 || p.clipW <= 0 || p.clipH <= 0)
        return;

    // Shift both the clip rect and the dest rect by the overlay offset.
    ImageDrawParams sp = p;
    sp.clipX += offX(ctx);
    sp.clipY += offY(ctx);
    sp.destX += offX(ctx);
    sp.destY += offY(ctx);

    // Compute UVs from the optional source sub-rect. srcW/srcH < 0 (the
    // default) means "use the full texture" — identical to the old
    // hardcoded 0..1 behavior, so every existing call site is unaffected.
    float sw = (sp.srcW >= 0.f) ? sp.srcW : (float)sp.srcWidth;
    float sh = (sp.srcH >= 0.f) ? sp.srcH : (float)sp.srcHeight;
    float u0 = (sp.srcWidth  > 0) ? sp.srcX / sp.srcWidth              : 0.f;
    float u1 = (sp.srcWidth  > 0) ? (sp.srcX + sw) / sp.srcWidth        : 1.f;
    float v0, v1;
    if (sp.srcHeight > 0)
    {
        float vTop    = sp.srcY / sp.srcHeight;
        float vBottom = (sp.srcY + sh) / sp.srcHeight;
        v0 = sp.flipY ? vBottom : vTop;
        v1 = sp.flipY ? vTop    : vBottom;
    }
    else
    {
        v0 = sp.flipY ? 1.f : 0.f;
        v1 = sp.flipY ? 0.f : 1.f;
    }

    s_gl->pushScissor(sp.clipX, sp.clipY, sp.clipW, sp.clipH);

    if (sp.repeat != ImageRepeat::NoRepeat)
    {
        float tileW = sp.destW, tileH = sp.destH;
        float sX = (sp.repeat == ImageRepeat::RepeatY) ? sp.destX : (float)sp.clipX;
        float sY = (sp.repeat == ImageRepeat::RepeatX) ? sp.destY : (float)sp.clipY;
        float eX = (sp.repeat == ImageRepeat::RepeatY) ? sp.destX + tileW : (float)(sp.clipX + sp.clipW);
        float eY = (sp.repeat == ImageRepeat::RepeatX) ? sp.destY + tileH : (float)(sp.clipY + sp.clipH);
        for (float ty = sY; ty < eY; ty += tileH)
            for (float tx = sX; tx < eX; tx += tileW)
                s_gl->drawTexture((GLuint)sp.image, tx, ty, tileW, tileH, u0, v0, u1, v1);
    }
    else
    {
        if (sp.borderRadius > 0)
        {
            // Rounded clip — scissor only approximates, but consistent with
            // the rest of the Android GL path.
            s_gl->pushScissor(sp.clipX, sp.clipY, sp.clipW, sp.clipH);
            s_gl->drawTexture((GLuint)sp.image, sp.destX, sp.destY, sp.destW, sp.destH, u0, v0, u1, v1);
            s_gl->popScissor();
        }
        else
        {
            s_gl->drawTexture((GLuint)sp.image, sp.destX, sp.destY, sp.destW, sp.destH, u0, v0, u1, v1);
        }
    }

    s_gl->popScissor();
}

void Painter::drawVideo(const VideoDrawParams &p)
{
    if (!s_gl || p.frame == -1 || p.dstW <= 0)
        return;
    s_gl->drawTexture((GLuint)p.frame,
                      (float)(p.dstX + offX(ctx)), (float)(p.dstY + offY(ctx)),
                      (float)p.dstW, (float)p.dstH);
}

void Painter::drawCamera(const CameraDrawParams &p)
{
    if (!s_gl || p.frame == -1 || p.dstW <= 0 || p.dstH <= 0)
        return;
    float u0 = p.mirror ? 1.f : 0.f;
    float u1 = p.mirror ? 0.f : 1.f;
    s_gl->drawTexture((GLuint)p.frame,
                      (float)(p.dstX + offX(ctx)), (float)(p.dstY + offY(ctx)),
                      (float)p.dstW, (float)p.dstH,
                      u0, 0.f, u1, 1.f);
}

void Painter::drawPage(const PageDrawParams &p)
{
    if (!s_gl)
        return;

    // Shift every region by the overlay offset.
    PageDrawParams sp = p;
    sp.x += offX(ctx);
    sp.y += offY(ctx);
    if (sp.body.present)
    {
        sp.body.x += offX(ctx);
        sp.body.y += offY(ctx);
    }
    if (sp.header.present)
    {
        sp.header.x += offX(ctx);
        sp.header.y += offY(ctx);
    }
    if (sp.footer.present)
    {
        sp.footer.x += offX(ctx);
        sp.footer.y += offY(ctx);
    }

    if (sp.hasPageBackground)
        s_gl->drawFilledRect(sp.x, sp.y, sp.w, sp.h, sp.pageBackground);

    if (sp.body.present && sp.body.hasBackground)
        s_gl->drawFilledRect(sp.body.x, sp.body.y, sp.body.w, sp.body.h,
                             sp.body.background);

    if (sp.header.present)
    {
        if (sp.header.hasBackground)
            s_gl->drawFilledRect(sp.header.x, sp.header.y, sp.header.w, sp.header.h,
                                 sp.header.background);
        if (sp.header.elevation > 0)
        {
            s_gl->drawGradientRect(sp.header.x, sp.header.y + sp.header.h,
                                   sp.header.w, sp.header.elevation,
                                   Color::fromRGBA(0, 0, 0, 60), Color::fromRGBA(0, 0, 0, 0));
        }
    }

    if (sp.footer.present)
    {
        if (sp.footer.hasBackground)
            s_gl->drawFilledRect(sp.footer.x, sp.footer.y, sp.footer.w, sp.footer.h,
                                 sp.footer.background);
        if (sp.footer.elevation > 0)
        {
            s_gl->drawGradientRect(sp.footer.x, sp.footer.y - sp.footer.elevation,
                                   sp.footer.w, sp.footer.elevation,
                                   Color::fromRGBA(0, 0, 0, 0), Color::fromRGBA(0, 0, 0, 60));
        }
    }
}

// ============================================================================
// Scrollbar stub — implement if/when needed on Android
// ============================================================================
void Painter::drawScrollbar(const CustomScrollbar &, int, int) {}

#endif // __ANDROID__