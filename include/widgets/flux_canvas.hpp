#ifndef FLUX_CANVAS_HPP
#define FLUX_CANVAS_HPP

// ============================================================================
// flux_canvas.hpp  –  Modern OpenGL (3.3 Core) backend
// ============================================================================

// Mouse Events
// ------------
//   canvas->onMouseDown([](int x, int y) { ... });
//   canvas->onMouseMove([](int x, int y) { ... });
//   canvas->onMouseUp  ([](int x, int y) { ... });
//   canvas->onMouseEnter([](int x, int y) { ... });
//   canvas->onMouseLeave([](int x, int y) { ... });
//   canvas->onMouseWheel([](int x, int y, int delta) { ... });
//
// ============================================================================

#include "flux_state.hpp"
#include "flux_widget.hpp"

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>

// ── OpenGL headers ──────────────────────────────────────────────────────────
// We load all GL 3.3+ entry points ourselves via wglGetProcAddress so that
// we only need the system opengl32.lib (no external GLEW/GLAD dependency).
#include <gl/GL.h>           // GL 1.1 base types (GLuint, GLfloat, …) + constants
#include "flux_gl_types.hpp" // GLchar, GLsizeiptr, GLintptr, GLint64, GLhalf
#pragma comment(lib, "opengl32.lib")

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <functional>
#include <stack>
#include <string>
#include <vector>

using namespace Gdiplus;

// ============================================================================
// OPENGL 3.3 FUNCTION POINTER TYPEDEFS (subset we need)
// ============================================================================

using PFNWGLCREATECONTEXTATTRIBSARBPROC =
    HGLRC(WINAPI *)(HDC, HGLRC, const int *);

// Buffers / VAO
using PFNGLGENBUFFERSPROC         = void(APIENTRY *)(GLsizei, GLuint *);
using PFNGLDELETEBUFFERSPROC      = void(APIENTRY *)(GLsizei, const GLuint *);
using PFNGLBINDBUFFERPROC         = void(APIENTRY *)(GLenum, GLuint);
using PFNGLBUFFERDATAPROC         = void(APIENTRY *)(GLenum, GLsizeiptr, const void *, GLenum);
using PFNGLBUFFERSUBDATAPROC      = void(APIENTRY *)(GLenum, GLintptr, GLsizeiptr, const void *);
using PFNGLGENVERTEXARRAYSPROC    = void(APIENTRY *)(GLsizei, GLuint *);
using PFNGLDELETEVERTEXARRAYSPROC = void(APIENTRY *)(GLsizei, const GLuint *);
using PFNGLBINDVERTEXARRAYPROC    = void(APIENTRY *)(GLuint);
// Shaders / programs
using PFNGLCREATESHADERPROC       = GLuint(APIENTRY *)(GLenum);
using PFNGLDELETESHADERPROC       = void(APIENTRY *)(GLuint);
using PFNGLSHADERSOURCEPROC       = void(APIENTRY *)(GLuint, GLsizei, const GLchar *const *, const GLint *);
using PFNGLCOMPILESHADERPROC      = void(APIENTRY *)(GLuint);
using PFNGLGETSHADERIVPROC        = void(APIENTRY *)(GLuint, GLenum, GLint *);
using PFNGLGETSHADERINFOLOGPROC   = void(APIENTRY *)(GLuint, GLsizei, GLsizei *, GLchar *);
using PFNGLCREATEPROGRAMPROC      = GLuint(APIENTRY *)();
using PFNGLDELETEPROGRAMPROC      = void(APIENTRY *)(GLuint);
using PFNGLATTACHSHADERPROC       = void(APIENTRY *)(GLuint, GLuint);
using PFNGLLINKPROGRAMPROC        = void(APIENTRY *)(GLuint);
using PFNGLUSEPROGRAMPROC         = void(APIENTRY *)(GLuint);
using PFNGLGETUNIFORMLOCATIONPROC = GLint(APIENTRY *)(GLuint, const GLchar *);
using PFNGLUNIFORM1IPROC          = void(APIENTRY *)(GLint, GLint);
using PFNGLUNIFORM1FPROC          = void(APIENTRY *)(GLint, GLfloat);
using PFNGLUNIFORM4FPROC          = void(APIENTRY *)(GLint, GLfloat, GLfloat, GLfloat, GLfloat);
using PFNGLUNIFORMMATRIX4FVPROC   = void(APIENTRY *)(GLint, GLsizei, GLboolean, const GLfloat *);
// Vertex attribs
using PFNGLENABLEVERTEXATTRIBARRAYPROC  = void(APIENTRY *)(GLuint);
using PFNGLDISABLEVERTEXATTRIBARRAYPROC = void(APIENTRY *)(GLuint);
using PFNGLVERTEXATTRIBPOINTERPROC      = void(APIENTRY *)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void *);

// GL constants not in gl.h (GL 1.1)
#define GL_ARRAY_BUFFER         0x8892
#define GL_STATIC_DRAW          0x88B4
#define GL_DYNAMIC_DRAW         0x88E8
#define GL_VERTEX_SHADER        0x8B31
#define GL_FRAGMENT_SHADER      0x8B30
#define GL_COMPILE_STATUS       0x8B81
#define GL_LINK_STATUS          0x8B82
#define GL_CLAMP_TO_EDGE        0x812F
#define WGL_CONTEXT_MAJOR_VERSION_ARB 0x2091
#define WGL_CONTEXT_MINOR_VERSION_ARB 0x2092
#define WGL_CONTEXT_PROFILE_MASK_ARB  0x9126
#define WGL_CONTEXT_CORE_PROFILE_BIT_ARB 0x00000001

// ============================================================================
// GL PROC TABLE  –  populated once per process in GLProcs::init()
// ============================================================================

struct GLProcs {
    PFNGLGENBUFFERSPROC         genBuffers{};
    PFNGLDELETEBUFFERSPROC      deleteBuffers{};
    PFNGLBINDBUFFERPROC         bindBuffer{};
    PFNGLBUFFERDATAPROC         bufferData{};
    PFNGLBUFFERSUBDATAPROC      bufferSubData{};
    PFNGLGENVERTEXARRAYSPROC    genVertexArrays{};
    PFNGLDELETEVERTEXARRAYSPROC deleteVertexArrays{};
    PFNGLBINDVERTEXARRAYPROC    bindVertexArray{};
    PFNGLCREATESHADERPROC       createShader{};
    PFNGLDELETESHADERPROC       deleteShader{};
    PFNGLSHADERSOURCEPROC       shaderSource{};
    PFNGLCOMPILESHADERPROC      compileShader{};
    PFNGLGETSHADERIVPROC        getShaderiv{};
    PFNGLGETSHADERINFOLOGPROC   getShaderInfoLog{};
    PFNGLCREATEPROGRAMPROC      createProgram{};
    PFNGLDELETEPROGRAMPROC      deleteProgram{};
    PFNGLATTACHSHADERPROC       attachShader{};
    PFNGLLINKPROGRAMPROC        linkProgram{};
    PFNGLUSEPROGRAMPROC         useProgram{};
    PFNGLGETUNIFORMLOCATIONPROC getUniformLocation{};
    PFNGLUNIFORM1IPROC          uniform1i{};
    PFNGLUNIFORM1FPROC          uniform1f{};
    PFNGLUNIFORM4FPROC          uniform4f{};
    PFNGLUNIFORMMATRIX4FVPROC   uniformMatrix4fv{};
    PFNGLENABLEVERTEXATTRIBARRAYPROC  enableVertexAttribArray{};
    PFNGLDISABLEVERTEXATTRIBARRAYPROC disableVertexAttribArray{};
    PFNGLVERTEXATTRIBPOINTERPROC      vertexAttribPointer{};

    static GLProcs &get() { static GLProcs p; return p; }

    void init() {
        auto load = [](const char *name) {
            return wglGetProcAddress(name);
        };
#define LOAD(fn, type, name) fn = reinterpret_cast<type>(load(name))
        LOAD(genBuffers,            PFNGLGENBUFFERSPROC,         "glGenBuffers");
        LOAD(deleteBuffers,         PFNGLDELETEBUFFERSPROC,      "glDeleteBuffers");
        LOAD(bindBuffer,            PFNGLBINDBUFFERPROC,         "glBindBuffer");
        LOAD(bufferData,            PFNGLBUFFERDATAPROC,         "glBufferData");
        LOAD(bufferSubData,         PFNGLBUFFERSUBDATAPROC,      "glBufferSubData");
        LOAD(genVertexArrays,       PFNGLGENVERTEXARRAYSPROC,    "glGenVertexArrays");
        LOAD(deleteVertexArrays,    PFNGLDELETEVERTEXARRAYSPROC, "glDeleteVertexArrays");
        LOAD(bindVertexArray,       PFNGLBINDVERTEXARRAYPROC,    "glBindVertexArray");
        LOAD(createShader,          PFNGLCREATESHADERPROC,       "glCreateShader");
        LOAD(deleteShader,          PFNGLDELETESHADERPROC,       "glDeleteShader");
        LOAD(shaderSource,          PFNGLSHADERSOURCEPROC,       "glShaderSource");
        LOAD(compileShader,         PFNGLCOMPILESHADERPROC,      "glCompileShader");
        LOAD(getShaderiv,           PFNGLGETSHADERIVPROC,        "glGetShaderiv");
        LOAD(getShaderInfoLog,      PFNGLGETSHADERINFOLOGPROC,   "glGetShaderInfoLog");
        LOAD(createProgram,         PFNGLCREATEPROGRAMPROC,      "glCreateProgram");
        LOAD(deleteProgram,         PFNGLDELETEPROGRAMPROC,      "glDeleteProgram");
        LOAD(attachShader,          PFNGLATTACHSHADERPROC,       "glAttachShader");
        LOAD(linkProgram,           PFNGLLINKPROGRAMPROC,        "glLinkProgram");
        LOAD(useProgram,            PFNGLUSEPROGRAMPROC,         "glUseProgram");
        LOAD(getUniformLocation,    PFNGLGETUNIFORMLOCATIONPROC, "glGetUniformLocation");
        LOAD(uniform1i,             PFNGLUNIFORM1IPROC,          "glUniform1i");
        LOAD(uniform1f,             PFNGLUNIFORM1FPROC,          "glUniform1f");
        LOAD(uniform4f,             PFNGLUNIFORM4FPROC,          "glUniform4f");
        LOAD(uniformMatrix4fv,      PFNGLUNIFORMMATRIX4FVPROC,   "glUniformMatrix4fv");
        LOAD(enableVertexAttribArray,  PFNGLENABLEVERTEXATTRIBARRAYPROC,  "glEnableVertexAttribArray");
        LOAD(disableVertexAttribArray, PFNGLDISABLEVERTEXATTRIBARRAYPROC, "glDisableVertexAttribArray");
        LOAD(vertexAttribPointer,      PFNGLVERTEXATTRIBPOINTERPROC,      "glVertexAttribPointer");
#undef LOAD
    }

private:
    GLProcs() = default;
};

// Convenience aliases
static inline GLProcs &GL = GLProcs::get();

// ============================================================================
// COLOR PARSING  (unchanged)
// ============================================================================

struct RGBA { float r, g, b, a; };

namespace detail {

inline RGBA parseHex(const std::string &s) {
    std::string h = s;
    if (!h.empty() && h[0] == '#') h = h.substr(1);
    auto hex2f = [](unsigned v){ return v / 255.0f; };
    unsigned n = (unsigned)std::stoul(h, nullptr, 16);
    if (h.size() == 3) {
        unsigned r=(n>>8)&0xF, g=(n>>4)&0xF, b=n&0xF;
        return {hex2f(r|(r<<4)), hex2f(g|(g<<4)), hex2f(b|(b<<4)), 1.f};
    }
    if (h.size() == 4) {
        unsigned r=(n>>12)&0xF, g=(n>>8)&0xF, b=(n>>4)&0xF, a=n&0xF;
        return {hex2f(r|(r<<4)), hex2f(g|(g<<4)), hex2f(b|(b<<4)), hex2f(a|(a<<4))};
    }
    if (h.size() == 6)
        return {hex2f((n>>16)&0xFF), hex2f((n>>8)&0xFF), hex2f(n&0xFF), 1.f};
    return {hex2f((n>>24)&0xFF), hex2f((n>>16)&0xFF), hex2f((n>>8)&0xFF), hex2f(n&0xFF)};
}

inline RGBA parseColor(const std::string &css) {
    if (css.empty()) return {1,1,1,1};
    if (css[0] == '#') return parseHex(css);
    if (css.substr(0,3) == "rgb") {
        auto a = css.find('('), b = css.find(')');
        if (a == std::string::npos) return {1,1,1,1};
        std::string inner = css.substr(a+1, b-a-1);
        float vals[4] = {0,0,0,1};
        int i = 0; size_t pos = 0;
        while (i < 4) {
            size_t comma = inner.find(',', pos);
            std::string tok = inner.substr(pos, comma == std::string::npos ? std::string::npos : comma-pos);
            vals[i++] = std::stof(tok);
            if (comma == std::string::npos) break;
            pos = comma+1;
        }
        return {vals[0]/255.f, vals[1]/255.f, vals[2]/255.f, vals[3]};
    }
    static const struct { const char *name; RGBA col; } kNamed[] = {
        {"black",{0,0,0,1}},       {"white",{1,1,1,1}},
        {"red",{1,0,0,1}},         {"green",{0,.5f,0,1}},
        {"blue",{0,0,1,1}},        {"yellow",{1,1,0,1}},
        {"cyan",{0,1,1,1}},        {"magenta",{1,0,1,1}},
        {"orange",{1,.647f,0,1}},  {"purple",{.5f,0,.5f,1}},
        {"gray",{.5f,.5f,.5f,1}},  {"grey",{.5f,.5f,.5f,1}},
        {"transparent",{0,0,0,0}},
    };
    std::string lo = css;
    for (auto &c : lo) c = (char)std::tolower((unsigned char)c);
    for (auto &e : kNamed) if (lo == e.name) return e.col;
    return {1,1,1,1};
}

} // namespace detail

// ============================================================================
// DRAW STATE
// ============================================================================

struct CanvasState {
    RGBA  fillColor   = {0,0,0,1};
    RGBA  strokeColor = {0,0,0,1};
    float lineW       = 1.f;
    float alpha       = 1.f;
    int   lineCap     = 0;   // 0=butt 1=round 2=square
    int   lineJoin    = 0;   // 0=miter 1=round 2=bevel
    // Column-major 3×3 transform (we expand to 4×4 for GL)
    float m[9] = {1,0,0, 0,1,0, 0,0,1};
    float fontSize = 16.f;
    std::string fontFamily = "Segoe UI";
};

// ============================================================================
// PATH COMMAND  (unchanged)
// ============================================================================

struct PathCmd {
    enum Type { MoveTo, LineTo, QuadTo, BezierTo, ArcTo, Arc, Close } type;
    float x0,y0,x1,y1,x2,y2,r,r2,start,end;
    bool  ccw;
};

// ============================================================================
// SHADER SOURCE
// ============================================================================

namespace shaders {

// ── Solid-color program ─────────────────────────────────────────────────────
// Attributes:  layout(location=0) vec2 aPos
// Uniforms:    mat4 uMVP,  vec4 uColor

inline const char *colorVert = R"GLSL(
#version 330 core
layout(location = 0) in vec2 aPos;
uniform mat4 uMVP;
void main() {
    gl_Position = uMVP * vec4(aPos, 0.0, 1.0);
}
)GLSL";

inline const char *colorFrag = R"GLSL(
#version 330 core
uniform vec4 uColor;
out vec4 fragColor;
void main() {
    fragColor = uColor;
}
)GLSL";

// ── Textured-quad program ───────────────────────────────────────────────────
// Attributes:  layout(location=0) vec2 aPos,  layout(location=1) vec2 aUV
// Uniforms:    mat4 uMVP,  sampler2D uTex

inline const char *texVert = R"GLSL(
#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
uniform mat4 uMVP;
out vec2 vUV;
void main() {
    vUV         = aUV;
    gl_Position = uMVP * vec4(aPos, 0.0, 1.0);
}
)GLSL";

inline const char *texFrag = R"GLSL(
#version 330 core
in  vec2      vUV;
uniform sampler2D uTex;
out vec4      fragColor;
void main() {
    fragColor = texture(uTex, vUV);
}
)GLSL";

} // namespace shaders

// ============================================================================
// SHADER HELPER
// ============================================================================

namespace glutil {

inline GLuint compileShader(GLenum type, const char *src) {
    GLuint s = GL.createShader(type);
    GL.shaderSource(s, 1, &src, nullptr);
    GL.compileShader(s);
    GLint ok = 0;
    GL.getShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char buf[1024]; GLsizei len;
        GL.getShaderInfoLog(s, 1024, &len, buf);
        OutputDebugStringA(buf);
    }
    return s;
}

inline GLuint linkProgram(const char *vert, const char *frag) {
    GLuint vs = compileShader(GL_VERTEX_SHADER,   vert);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, frag);
    GLuint p  = GL.createProgram();
    GL.attachShader(p, vs);
    GL.attachShader(p, fs);
    GL.linkProgram(p);
    GL.deleteShader(vs);
    GL.deleteShader(fs);
    return p;
}

// Build a column-major 4×4 orthographic projection matrix
// (maps [0,w] x [0,h] → NDC, y-down so y=0 is at the top)
inline void ortho(float w, float h, float out[16]) {
    // Equivalent of glOrtho(0,w, h,0, -1,1) — y-axis flipped so canvas y=0 is top
    float r = 1.f/w, t = 1.f/h;
    // Column-major
    out[ 0]=2*r; out[ 1]=0;    out[ 2]=0;  out[ 3]=0;
    out[ 4]=0;   out[ 5]=-2*t; out[ 6]=0;  out[ 7]=0;
    out[ 8]=0;   out[ 9]=0;    out[10]=-1; out[11]=0;
    out[12]=-1;  out[13]=1;    out[14]=0;  out[15]=1;
}

// Multiply two column-major 4×4 matrices (c = a * b)
inline void mat4mul(const float a[16], const float b[16], float c[16]) {
    for (int col=0; col<4; col++)
        for (int row=0; row<4; row++) {
            float s=0;
            for (int k=0; k<4; k++) s += a[k*4+row] * b[col*4+k];
            c[col*4+row] = s;
        }
}

} // namespace glutil

// ============================================================================
// GEOMETRY BATCHER
// ============================================================================
//
// One persistent VAO + VBO pair for geometry, one for textured quads.
// Data is rebuilt each draw call and uploaded with GL_DYNAMIC_DRAW.
//

struct GeomBatch {
    GLuint vao = 0, vbo = 0;

    void init() {
        GL.genVertexArrays(1, &vao);
        GL.genBuffers(1, &vbo);
        GL.bindVertexArray(vao);
        GL.bindBuffer(GL_ARRAY_BUFFER, vbo);
        // Attribute 0: vec2 position
        GL.enableVertexAttribArray(0);
        GL.vertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(float)*2, nullptr);
        GL.bindVertexArray(0);
    }

    void upload(const std::vector<float> &xy) {
        GL.bindBuffer(GL_ARRAY_BUFFER, vbo);
        GL.bufferData(GL_ARRAY_BUFFER,
                      (GLsizeiptr)(xy.size() * sizeof(float)),
                      xy.data(), GL_DYNAMIC_DRAW);
    }

    void draw(GLenum mode, GLsizei count) {
        GL.bindVertexArray(vao);
        glDrawArrays(mode, 0, count);
        GL.bindVertexArray(0);
    }

    void destroy() {
        if (vao) { GL.deleteVertexArrays(1, &vao); vao=0; }
        if (vbo) { GL.deleteBuffers(1, &vbo);      vbo=0; }
    }
};

struct TexBatch {
    GLuint vao = 0, vbo = 0;

    void init() {
        GL.genVertexArrays(1, &vao);
        GL.genBuffers(1, &vbo);
        GL.bindVertexArray(vao);
        GL.bindBuffer(GL_ARRAY_BUFFER, vbo);
        // Attribute 0: vec2 pos, attribute 1: vec2 uv  (stride = 4 floats)
        GL.enableVertexAttribArray(0);
        GL.vertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(float)*4, (void*)0);
        GL.enableVertexAttribArray(1);
        GL.vertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(float)*4, (void*)(sizeof(float)*2));
        GL.bindVertexArray(0);
    }

    // Upload 6 vertices (2 triangles) for a textured quad
    void uploadQuad(float x0, float y0, float x1, float y1) {
        // pos(2) + uv(2) = 4 floats per vertex, 6 vertices
        float d[] = {
            x0,y0, 0,0,   x1,y0, 1,0,   x1,y1, 1,1,
            x1,y1, 1,1,   x0,y1, 0,1,   x0,y0, 0,0,
        };
        GL.bindBuffer(GL_ARRAY_BUFFER, vbo);
        GL.bufferData(GL_ARRAY_BUFFER, sizeof(d), d, GL_DYNAMIC_DRAW);
    }

    void draw() {
        GL.bindVertexArray(vao);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        GL.bindVertexArray(0);
    }

    void destroy() {
        if (vao) { GL.deleteVertexArrays(1, &vao); vao=0; }
        if (vbo) { GL.deleteBuffers(1, &vbo);      vbo=0; }
    }
};

// ============================================================================
// CANVAS CONTEXT  –  modern GL 3.3 implementation
// ============================================================================

class CanvasContext {
public:
    int W, H;
    HDC gdiDC;

    // ── Construction / destruction ──────────────────────────────────────────
    CanvasContext(int w, int h, HDC dc,
                  GLuint colorProg, GLuint texProg,
                  GeomBatch &geomBatch, TexBatch &texBatch)
        : W(w), H(h), gdiDC(dc),
          colorProg_(colorProg), texProg_(texProg),
          geom_(geomBatch), tex_(texBatch)
    {
        st_.push({});
        glutil::ortho((float)W, (float)H, proj_);
        buildMVP();
        setupGL();
    }

    // ── State ───────────────────────────────────────────────────────────────
    CanvasContext &save()    { st_.push(st_.top()); return *this; }
    CanvasContext &restore() { if (st_.size()>1) { st_.pop(); buildMVP(); } return *this; }

    // ── Styles ──────────────────────────────────────────────────────────────
    CanvasContext &fillStyle  (const std::string &css) { st_.top().fillColor   = detail::parseColor(css); return *this; }
    CanvasContext &strokeStyle(const std::string &css) { st_.top().strokeColor = detail::parseColor(css); return *this; }
    CanvasContext &lineWidth  (float w)                { st_.top().lineW = w;   return *this; }
    CanvasContext &globalAlpha(float a)                { st_.top().alpha = a;   return *this; }
    CanvasContext &lineCap    (const std::string &v)   {
        auto &s = st_.top();
        s.lineCap = (v=="round")?1:(v=="square")?2:0;
        return *this;
    }
    CanvasContext &lineJoin   (const std::string &v)   {
        auto &s = st_.top();
        s.lineJoin = (v=="round")?1:(v=="bevel")?2:0;
        return *this;
    }
    CanvasContext &font(const std::string &sizeStr, const std::string &family = "") {
        auto &s = st_.top();
        try { s.fontSize = std::stof(sizeStr); } catch (...) {}
        if (!family.empty()) s.fontFamily = family;
        return *this;
    }

    // ── Transforms ──────────────────────────────────────────────────────────
    CanvasContext &translate(float tx, float ty) {
        float *m = st_.top().m;
        m[6] += tx*m[0] + ty*m[3];
        m[7] += tx*m[1] + ty*m[4];
        buildMVP();
        return *this;
    }
    CanvasContext &scale(float sx, float sy) {
        float *m = st_.top().m;
        m[0]*=sx; m[1]*=sx; m[3]*=sy; m[4]*=sy;
        buildMVP();
        return *this;
    }
    CanvasContext &rotate(float rad) {
        float c=std::cos(rad), s=std::sin(rad);
        float *m=st_.top().m, a=m[0],b=m[1],d=m[3],e=m[4];
        m[0]=a*c+d*s; m[1]=b*c+e*s;
        m[3]=-a*s+d*c; m[4]=-b*s+e*c;
        buildMVP();
        return *this;
    }
    CanvasContext &resetTransform() {
        float *m = st_.top().m;
        m[0]=1;m[1]=0;m[2]=0; m[3]=0;m[4]=1;m[5]=0; m[6]=0;m[7]=0;m[8]=1;
        buildMVP();
        return *this;
    }

    // ── Path ────────────────────────────────────────────────────────────────
    CanvasContext &beginPath()          { path_.clear(); return *this; }
    CanvasContext &closePath()          { path_.push_back({PathCmd::Close}); return *this; }
    CanvasContext &moveTo(float x,float y){ path_.push_back({PathCmd::MoveTo,x,y}); return *this; }
    CanvasContext &lineTo(float x,float y){ path_.push_back({PathCmd::LineTo,x,y}); return *this; }

    CanvasContext &quadraticCurveTo(float cpx,float cpy,float x,float y){
        PathCmd c{}; c.type=PathCmd::QuadTo; c.x0=cpx;c.y0=cpy;c.x1=x;c.y1=y;
        path_.push_back(c); return *this;
    }
    CanvasContext &bezierCurveTo(float cp1x,float cp1y,float cp2x,float cp2y,float x,float y){
        PathCmd c{}; c.type=PathCmd::BezierTo;
        c.x0=cp1x;c.y0=cp1y;c.x1=cp2x;c.y1=cp2y;c.x2=x;c.y2=y;
        path_.push_back(c); return *this;
    }
    CanvasContext &arc(float x,float y,float r,float sa,float ea,bool ccw=false){
        PathCmd c{}; c.type=PathCmd::Arc; c.x0=x;c.y0=y;c.r=r;c.start=sa;c.end=ea;c.ccw=ccw;
        path_.push_back(c); return *this;
    }
    CanvasContext &rect(float x,float y,float w,float h){
        moveTo(x,y); lineTo(x+w,y); lineTo(x+w,y+h); lineTo(x,y+h); closePath();
        return *this;
    }
    CanvasContext &roundRect(float x,float y,float w,float h,float r){
        r = min(r, min(w,h)*0.5f);
        moveTo(x+r,y);
        lineTo(x+w-r,y);    arc(x+w-r,y+r,    r,-kHPI,0);
        lineTo(x+w,y+h-r);  arc(x+w-r,y+h-r,  r,0,kHPI);
        lineTo(x+r,y+h);    arc(x+r,  y+h-r,  r,kHPI,(float)kPI);
        lineTo(x,y+r);      arc(x+r,  y+r,    r,(float)kPI,-kHPI);
        closePath();
        return *this;
    }
    CanvasContext &ellipse(float cx,float cy,float rx,float ry,float rotation,float sa,float ea){
        int segs=64; float da=(ea-sa)/segs;
        float cosR=std::cos(rotation), sinR=std::sin(rotation);
        bool first=true;
        for (int i=0;i<=segs;i++){
            float a=sa+i*da, ex=rx*std::cos(a), ey=ry*std::sin(a);
            float px=cx+ex*cosR-ey*sinR, py=cy+ex*sinR+ey*cosR;
            if (first){moveTo(px,py);first=false;}else lineTo(px,py);
        }
        return *this;
    }

    // ── Fill / Stroke ────────────────────────────────────────────────────────
    CanvasContext &fill()   { rasterisePath(true);  return *this; }
    CanvasContext &stroke() {
        glLineWidth(st_.top().lineW);
        rasterisePath(false);
        return *this;
    }

    // ── Rect shortcuts ───────────────────────────────────────────────────────
    CanvasContext &fillRect(float x,float y,float w,float h){
        setColorUniform(st_.top().fillColor);
        float xy[]={x,y, x+w,y, x+w,y+h,  x+w,y+h, x,y+h, x,y};
        geom_.upload(std::vector<float>(xy, xy+12));
        geom_.draw(GL_TRIANGLES, 6);
        return *this;
    }
    CanvasContext &strokeRect(float x,float y,float w,float h){
        glLineWidth(st_.top().lineW);
        setColorUniform(st_.top().strokeColor);
        float xy[]={x,y, x+w,y, x+w,y+h, x,y+h, x,y};
        geom_.upload(std::vector<float>(xy,xy+10));
        geom_.draw(GL_LINE_STRIP, 5);
        return *this;
    }
    CanvasContext &clearRect(float x,float y,float w,float h){
        glScissor((GLint)x, H-(GLint)(y+h), (GLsizei)w, (GLsizei)h);
        glEnable(GL_SCISSOR_TEST);
        glClear(GL_COLOR_BUFFER_BIT);
        glDisable(GL_SCISSOR_TEST);
        return *this;
    }

    // ── Text ─────────────────────────────────────────────────────────────────
    CanvasContext &fillText  (const std::string &t, float x, float y, float mw=-1.f){ drawText(t,x,y,true);  return *this; }
    CanvasContext &strokeText(const std::string &t, float x, float y, float mw=-1.f){ drawText(t,x,y,false); return *this; }
    float measureText(const std::string &text){
        auto tt = makeTextTex(text, false);
        if (tt.id) glDeleteTextures(1,&tt.id);
        return (float)tt.w;
    }

    // ── Convenience ──────────────────────────────────────────────────────────
    CanvasContext &drawLine(float x1,float y1,float x2,float y2){
        glLineWidth(st_.top().lineW);
        setColorUniform(st_.top().strokeColor);
        float xy[]={x1,y1,x2,y2};
        geom_.upload(std::vector<float>(xy,xy+4));
        geom_.draw(GL_LINES,2);
        return *this;
    }
    CanvasContext &fillCircle  (float cx,float cy,float r){ beginPath(); arc(cx,cy,r,0,(float)(2*kPI)); fill();   return *this; }
    CanvasContext &strokeCircle(float cx,float cy,float r){ beginPath(); arc(cx,cy,r,0,(float)(2*kPI)); stroke(); return *this; }

private:
    std::stack<CanvasState>  st_;
    std::vector<PathCmd>     path_;
    GLuint  colorProg_, texProg_;
    GeomBatch &geom_;
    TexBatch  &tex_;

    // Pre-multiplied MVP matrices
    float proj_[16];  // orthographic – updated once in ctor
    float mvp_[16];   // proj * model – rebuilt on every transform change

    static constexpr double kPI  = 3.14159265358979323846;
    static constexpr float  kHPI = 1.5707963f;

    // ── GL Setup ─────────────────────────────────────────────────────────────
    void setupGL() {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        // Line smoothing still available in compatibility path;
        // in strict core it's a no-op on most drivers but harmless.
        glEnable(GL_LINE_SMOOTH);
        glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
    }

    // ── MVP ──────────────────────────────────────────────────────────────────
    // Expand the canvas's 3×3 affine transform to a 4×4 column-major matrix
    // then multiply by the ortho projection.
    void buildMVP() {
        const float *m = st_.top().m;
        // Column-major 4×4 from 3×3:
        //   [m[0] m[3] 0 m[6]]
        //   [m[1] m[4] 0 m[7]]
        //   [0    0    1 0   ]
        //   [0    0    0 1   ]
        float model[16] = {
            m[0], m[1], 0, 0,
            m[3], m[4], 0, 0,
            0,    0,    1, 0,
            m[6], m[7], 0, 1
        };
        glutil::mat4mul(proj_, model, mvp_);
    }

    void setMVPUniform(GLuint prog) {
        GL.useProgram(prog);
        GLint loc = GL.getUniformLocation(prog, "uMVP");
        GL.uniformMatrix4fv(loc, 1, GL_FALSE, mvp_);
    }

    void setColorUniform(const RGBA &c) {
        GL.useProgram(colorProg_);
        GLint mvpLoc = GL.getUniformLocation(colorProg_, "uMVP");
        GL.uniformMatrix4fv(mvpLoc, 1, GL_FALSE, mvp_);
        GLint colLoc = GL.getUniformLocation(colorProg_, "uColor");
        GL.uniform4f(colLoc, c.r, c.g, c.b, c.a * st_.top().alpha);
    }

    // ── Path tessellation (unchanged logic, new upload path) ─────────────────
    struct FlatVert { float x, y; };

    std::vector<FlatVert> tessPath() {
        std::vector<FlatVert> verts;
        float cx=0, cy=0, sx=0, sy=0;
        for (auto &cmd : path_) {
            switch (cmd.type) {
            case PathCmd::MoveTo:
                cx=cmd.x0; cy=cmd.y0; sx=cx; sy=cy;
                verts.push_back({-1e9f,-1e9f});
                verts.push_back({cx,cy});
                break;
            case PathCmd::LineTo:
                cx=cmd.x0; cy=cmd.y0;
                verts.push_back({cx,cy});
                break;
            case PathCmd::QuadTo: {
                float x0=cx,y0=cy,x1=cmd.x0,y1=cmd.y0,x2=cmd.x1,y2=cmd.y1;
                const int N=16;
                for (int i=1;i<=N;i++){
                    float t=(float)i/N, it=1-t;
                    verts.push_back({it*it*x0+2*it*t*x1+t*t*x2,
                                     it*it*y0+2*it*t*y1+t*t*y2});
                }
                cx=x2; cy=y2; break;
            }
            case PathCmd::BezierTo: {
                float x0=cx,y0=cy,x1=cmd.x0,y1=cmd.y0,
                      x2=cmd.x1,y2=cmd.y1,x3=cmd.x2,y3=cmd.y2;
                const int N=32;
                for (int i=1;i<=N;i++){
                    float t=(float)i/N, it=1-t;
                    verts.push_back({it*it*it*x0+3*it*it*t*x1+3*it*t*t*x2+t*t*t*x3,
                                     it*it*it*y0+3*it*it*t*y1+3*it*t*t*y2+t*t*t*y3});
                }
                cx=x3; cy=y3; break;
            }
            case PathCmd::Arc: {
                float as=cmd.start, ae=cmd.end;
                if (cmd.ccw){ while(ae>as)  ae-=(float)(2*kPI); }
                else         { while(ae<as)  ae+=(float)(2*kPI); }
                float sw=ae-as;
                int N=max(8,(int)(std::abs(sw)*cmd.r/2));
                N=min(N,256);
                for (int i=0;i<=N;i++){
                    float a=as+sw*((float)i/N);
                    verts.push_back({cmd.x0+cmd.r*std::cos(a),
                                     cmd.y0+cmd.r*std::sin(a)});
                }
                cx=cmd.x0+cmd.r*std::cos(ae);
                cy=cmd.y0+cmd.r*std::sin(ae);
                break;
            }
            case PathCmd::Close:
                verts.push_back({sx,sy}); cx=sx; cy=sy; break;
            default: break;
            }
        }
        return verts;
    }

    void rasterisePath(bool doFill) {
        auto verts = tessPath();
        if (verts.empty()) return;

        // Split at sentinel (-1e9, -1e9)
        std::vector<std::vector<FlatVert>> subs;
        std::vector<FlatVert> cur;
        for (auto &v : verts) {
            if (v.x < -1e8f) {
                if (!cur.empty()) subs.push_back(std::move(cur));
                cur.clear();
            } else {
                cur.push_back(v);
            }
        }
        if (!cur.empty()) subs.push_back(std::move(cur));

        if (doFill) {
            setColorUniform(st_.top().fillColor);
        } else {
            setColorUniform(st_.top().strokeColor);
        }

        for (auto &sp : subs) {
            if (doFill && sp.size() < 3) continue;
            if (!doFill && sp.size() < 2) continue;

            // Flatten to raw float array
            std::vector<float> xy;
            xy.reserve(sp.size()*2);
            for (auto &v : sp) { xy.push_back(v.x); xy.push_back(v.y); }

            geom_.upload(xy);
            geom_.draw(doFill ? GL_TRIANGLE_FAN : GL_LINE_STRIP,
                       (GLsizei)sp.size());
        }
    }

    // ── Text (GDI+ → texture → quad) ─────────────────────────────────────────
    struct TextTex { GLuint id=0; int w=0, h=0; };

    TextTex makeTextTex(const std::string &text, bool filled) {
        if (text.empty()) return {};
        auto &s = st_.top();

        auto toWide = [](const std::string &u) {
            int n = MultiByteToWideChar(CP_UTF8,0,u.c_str(),-1,nullptr,0);
            std::wstring w(n,0);
            MultiByteToWideChar(CP_UTF8,0,u.c_str(),-1,w.data(),n);
            return w;
        };
        std::wstring wt = toWide(text);
        std::wstring wf = toWide(s.fontFamily);

        Gdiplus::Bitmap measure(1,1,PixelFormat32bppARGB);
        Gdiplus::Graphics gm(&measure);
        Gdiplus::Font font(wf.c_str(), s.fontSize, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
        Gdiplus::RectF lay(0,0,4000,4000), bounds;
        gm.MeasureString(wt.c_str(),-1,&font,lay,&bounds);
        int tw=max(1,(int)std::ceil(bounds.Width)+2);
        int th=max(1,(int)std::ceil(bounds.Height)+2);

        Gdiplus::Bitmap bmp(tw,th,PixelFormat32bppARGB);
        Gdiplus::Graphics g(&bmp);
        g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAlias);
        g.Clear(Gdiplus::Color(0,0,0,0));

        const RGBA &col = filled ? s.fillColor : s.strokeColor;
        BYTE cr=(BYTE)(col.r*255), cg=(BYTE)(col.g*255),
             cb=(BYTE)(col.b*255), ca=(BYTE)(col.a*s.alpha*255);
        Gdiplus::SolidBrush brush(Gdiplus::Color(ca,cr,cg,cb));
        g.DrawString(wt.c_str(),-1,&font,Gdiplus::PointF(1,1),&brush);

        Gdiplus::BitmapData bd;
        Gdiplus::Rect r(0,0,tw,th);
        bmp.LockBits(&r,Gdiplus::ImageLockModeRead,PixelFormat32bppARGB,&bd);
        std::vector<BYTE> rgba(tw*th*4);
        for (int row=0;row<th;row++){
            const BYTE *src=(const BYTE*)bd.Scan0+row*bd.Stride;
            BYTE *dst=rgba.data()+row*tw*4;
            for (int col2=0;col2<tw;col2++,src+=4,dst+=4){
                dst[0]=src[2];dst[1]=src[1];dst[2]=src[0];dst[3]=src[3];
            }
        }
        bmp.UnlockBits(&bd);

        GLuint tex=0;
        glGenTextures(1,&tex);
        glBindTexture(GL_TEXTURE_2D,tex);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,tw,th,0,GL_RGBA,GL_UNSIGNED_BYTE,rgba.data());
        glBindTexture(GL_TEXTURE_2D,0);
        return {tex,tw,th};
    }

    void drawText(const std::string &text, float x, float y, bool filled) {
        auto tt = makeTextTex(text, filled);
        if (!tt.id) return;

        // Text is drawn without the canvas model transform applied
        // (same behaviour as legacy version — text sits in screen space).
        // We temporarily replace MVP with the pure ortho projection.
        GL.useProgram(texProg_);
        GLint mvpLoc = GL.getUniformLocation(texProg_, "uMVP");
        GL.uniformMatrix4fv(mvpLoc, 1, GL_FALSE, proj_);

        glBindTexture(GL_TEXTURE_2D, tt.id);
        tex_.uploadQuad(x, y - tt.h, x + tt.w, y);
        tex_.draw();
        glBindTexture(GL_TEXTURE_2D, 0);

        glDeleteTextures(1, &tt.id);
    }
};

// ============================================================================
// CANVAS WIDGET
// ============================================================================

class CanvasWidget : public Widget {
public:
    using DrawFn  = std::function<void(CanvasContext &)>;
    using MouseFn = std::function<void(int x, int y)>;
    using WheelFn = std::function<void(int x, int y, int delta)>;

    CanvasWidget() {
        autoWidth  = false;
        autoHeight = false;
        width  = 400;
        height = 300;
    }
    ~CanvasWidget() { destroyGL(); }

    // ── Fluent API ────────────────────────────────────────────────────────────
    std::shared_ptr<CanvasWidget> onDraw(DrawFn fn) {
        drawFn = std::move(fn);
        markNeedsPaint();
        return ptr();
    }
    std::shared_ptr<CanvasWidget> setSize(int w, int h) {
        width=w; height=h;
        autoWidth=autoHeight=false;
        markNeedsLayout();
        return ptr();
    }
    std::shared_ptr<CanvasWidget> redraw() { markNeedsPaint(); return ptr(); }

    // ── Mouse event registration ──────────────────────────────────────────────
    std::shared_ptr<CanvasWidget> onMouseDown (MouseFn fn){ mouseDownFn  =std::move(fn); return ptr(); }
    std::shared_ptr<CanvasWidget> onMouseMove (MouseFn fn){ mouseMoveFn  =std::move(fn); return ptr(); }
    std::shared_ptr<CanvasWidget> onMouseUp   (MouseFn fn){ mouseUpFn    =std::move(fn); return ptr(); }
    std::shared_ptr<CanvasWidget> onMouseEnter(MouseFn fn){ mouseEnterFn =std::move(fn); return ptr(); }
    std::shared_ptr<CanvasWidget> onMouseLeave(MouseFn fn){ mouseLeaveFn =std::move(fn); return ptr(); }
    std::shared_ptr<CanvasWidget> onMouseWheel(WheelFn fn){ mouseWheelFn =std::move(fn); return ptr(); }

    // ── Reactive state binding ────────────────────────────────────────────────
    template <typename T>
    std::shared_ptr<CanvasWidget> bindState(State<T> &state) {
        state.bindProperty(
            shared_from_this(),
            [](Widget *, const T &){},
            false
        );
        return ptr();
    }

    // ── Widget overrides ──────────────────────────────────────────────────────
    void computeLayout(HDC hdc, const BoxConstraints &constraints, FontCache &fontCache) override {
        width  = constraints.clampWidth (autoWidth  ? constraints.maxWidth  : width);
        height = constraints.clampHeight(autoHeight ? constraints.maxHeight : height);
        needsLayout = false;
    }

    void render(HDC hdc, FontCache &) override {
        ensureChildWindow(hdc);
        moveChildWindow();
        renderGL();
        needsPaint = false;
    }

    void onDetach() override { destroyGL(); Widget::onDetach(); }

    void markNeedsPaint() override {
        Widget::markNeedsPaint();
        if (childHwnd && !repaintPending_) {
            repaintPending_ = true;
            PostMessage(childHwnd, WM_USER+1, 0, 0);
        }
    }

private:
    std::shared_ptr<CanvasWidget> ptr() {
        return std::static_pointer_cast<CanvasWidget>(shared_from_this());
    }

    DrawFn   drawFn;
    MouseFn  mouseDownFn, mouseMoveFn, mouseUpFn, mouseEnterFn, mouseLeaveFn;
    WheelFn  mouseWheelFn;

    bool mouseInside_    = false;
    bool trackingLeave_  = false;
    bool repaintPending_ = false;

    HWND  childHwnd  = nullptr;
    HDC   glDC       = nullptr;
    HGLRC glRC       = nullptr;
    HWND  parentHwnd = nullptr;

    // Modern GL resources — created once after context is ready
    GLuint    colorProg_ = 0;
    GLuint    texProg_   = 0;
    GeomBatch geomBatch_;
    TexBatch  texBatch_;

    // ── Win32 child window proc ───────────────────────────────────────────────
    static LRESULT CALLBACK ChildProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        CanvasWidget *self =
            reinterpret_cast<CanvasWidget*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
        switch (msg) {
        case WM_USER+1:
            if (self){ self->repaintPending_=false; self->renderGL(); }
            return 0;
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: { PAINTSTRUCT ps; BeginPaint(hwnd,&ps); EndPaint(hwnd,&ps); return 0; }

        case WM_LBUTTONDOWN: case WM_RBUTTONDOWN: case WM_MBUTTONDOWN:
            SetCapture(hwnd);
            if (self && self->mouseDownFn) self->mouseDownFn(GET_X_LPARAM(lp),GET_Y_LPARAM(lp));
            forwardToParent(hwnd,msg,wp,lp); return 0;

        case WM_LBUTTONUP: case WM_RBUTTONUP: case WM_MBUTTONUP:
            ReleaseCapture();
            if (self && self->mouseUpFn) self->mouseUpFn(GET_X_LPARAM(lp),GET_Y_LPARAM(lp));
            forwardToParent(hwnd,msg,wp,lp); return 0;

        case WM_MOUSEMOVE: {
            int mx=GET_X_LPARAM(lp),my=GET_Y_LPARAM(lp);
            if (self){
                if (!self->trackingLeave_){
                    TRACKMOUSEEVENT tme{}; tme.cbSize=sizeof(tme);
                    tme.dwFlags=TME_LEAVE; tme.hwndTrack=hwnd;
                    TrackMouseEvent(&tme); self->trackingLeave_=true;
                }
                if (!self->mouseInside_){
                    self->mouseInside_=true;
                    if (self->mouseEnterFn) self->mouseEnterFn(mx,my);
                }
                if (self->mouseMoveFn) self->mouseMoveFn(mx,my);
            }
            forwardToParent(hwnd,msg,wp,lp); return 0;
        }
        case WM_MOUSELEAVE:
            if (self){ self->trackingLeave_=false; self->mouseInside_=false;
                       if (self->mouseLeaveFn) self->mouseLeaveFn(0,0); }
            return 0;
        case WM_MOUSEWHEEL: {
            int delta=GET_WHEEL_DELTA_WPARAM(wp);
            POINT pt={GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
            ScreenToClient(hwnd,&pt);
            if (self && self->mouseWheelFn) self->mouseWheelFn(pt.x,pt.y,delta);
            forwardToParent(hwnd,msg,wp,lp); return 0;
        }
        default: return DefWindowProc(hwnd,msg,wp,lp);
        }
    }

    static void forwardToParent(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
        HWND parent=GetParent(hwnd); if(!parent)return;
        POINT pt={GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
        MapWindowPoints(hwnd,parent,&pt,1);
        PostMessage(parent,msg,wp,MAKELPARAM(pt.x,pt.y));
    }

    void registerChildClass(){
        static bool done=false; if(done)return;
        WNDCLASSEXW wc={};
        wc.cbSize=sizeof(wc); wc.lpfnWndProc=ChildProc;
        wc.hInstance=GetModuleHandle(nullptr); wc.lpszClassName=L"FluxGLCanvas";
        wc.style=CS_OWNDC|CS_HREDRAW|CS_VREDRAW;
        RegisterClassExW(&wc); done=true;
    }

    void ensureChildWindow(HDC parentDC){
        if(childHwnd)return;
        HWND owner=WindowFromDC(parentDC);
        if(!owner)owner=GetActiveWindow();
        parentHwnd=owner;
        registerChildClass();
        childHwnd=CreateWindowExW(WS_EX_NOPARENTNOTIFY,L"FluxGLCanvas",nullptr,
            WS_CHILD|WS_VISIBLE|WS_CLIPSIBLINGS|WS_CLIPCHILDREN,
            x,y,width,height,owner,nullptr,GetModuleHandle(nullptr),nullptr);
        if(!childHwnd)return;
        SetWindowLongPtrW(childHwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(this));

        glDC=GetDC(childHwnd);
        setupPixelFormat(glDC);

        // ── Step 1: create a temporary legacy context to call wglGetProcAddress
        HGLRC tmpRC=wglCreateContext(glDC);
        wglMakeCurrent(glDC,tmpRC);

        // ── Step 2: load wglCreateContextAttribsARB
        auto wglCreateCtxAttribs=reinterpret_cast<PFNWGLCREATECONTEXTATTRIBSARBPROC>(
            wglGetProcAddress("wglCreateContextAttribsARB"));

        // ── Step 3: request Core 3.3 context
        if(wglCreateCtxAttribs){
            const int attribs[]={
                WGL_CONTEXT_MAJOR_VERSION_ARB, 3,
                WGL_CONTEXT_MINOR_VERSION_ARB, 3,
                WGL_CONTEXT_PROFILE_MASK_ARB,  WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
                0
            };
            HGLRC coreRC=wglCreateCtxAttribs(glDC,nullptr,attribs);
            if(coreRC){
                wglMakeCurrent(nullptr,nullptr);
                wglDeleteContext(tmpRC);
                glRC=coreRC;
            } else {
                glRC=tmpRC; // fallback — driver too old
            }
        } else {
            glRC=tmpRC;
        }
        wglMakeCurrent(glDC,glRC);

        // ── Step 4: load all GL 3.3 proc pointers
        GL.init();

        // ── Step 5: compile shaders and allocate GPU resources
        colorProg_ = glutil::linkProgram(shaders::colorVert, shaders::colorFrag);
        texProg_   = glutil::linkProgram(shaders::texVert,   shaders::texFrag);
        geomBatch_.init();
        texBatch_.init();

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }

    void setupPixelFormat(HDC dc){
        PIXELFORMATDESCRIPTOR pfd={};
        pfd.nSize=sizeof(pfd); pfd.nVersion=1;
        pfd.dwFlags=PFD_DRAW_TO_WINDOW|PFD_SUPPORT_OPENGL|PFD_DOUBLEBUFFER;
        pfd.iPixelType=PFD_TYPE_RGBA; pfd.cColorBits=32; pfd.cDepthBits=24;
        pfd.iLayerType=PFD_MAIN_PLANE;
        SetPixelFormat(dc,ChoosePixelFormat(dc,&pfd),&pfd);
    }

    void moveChildWindow(){
        if(!childHwnd)return;
        SetWindowPos(childHwnd,nullptr,x,y,width,height,SWP_NOZORDER|SWP_NOACTIVATE);
    }

    void renderGL(){
        if(!glRC||!glDC)return;
        if(wglGetCurrentContext()!=glRC) wglMakeCurrent(glDC,glRC);
        glViewport(0,0,width,height);
        glClearColor(0,0,0,1);
        glClear(GL_COLOR_BUFFER_BIT);
        if(drawFn){
            CanvasContext ctx(width,height,glDC,
                              colorProg_,texProg_,geomBatch_,texBatch_);
            drawFn(ctx);
        }
        SwapBuffers(glDC);
    }

    void destroyGL(){
        if(glRC){
            wglMakeCurrent(glDC,glRC);
            // Free shader programs and GPU buffers before context teardown
            if(colorProg_){ GL.deleteProgram(colorProg_); colorProg_=0; }
            if(texProg_)  { GL.deleteProgram(texProg_);   texProg_=0;   }
            geomBatch_.destroy();
            texBatch_.destroy();
            wglMakeCurrent(nullptr,nullptr);
            wglDeleteContext(glRC); glRC=nullptr;
        }
        if(childHwnd&&glDC){ ReleaseDC(childHwnd,glDC); glDC=nullptr; }
        if(childHwnd)      { DestroyWindow(childHwnd);  childHwnd=nullptr; }
    }
};

// ============================================================================
// FACTORY  (unchanged)
// ============================================================================

using CanvasPtr = std::shared_ptr<CanvasWidget>;

inline CanvasPtr Canvas()                                        { return std::make_shared<CanvasWidget>(); }
inline CanvasPtr Canvas(int w,int h)                             { return std::make_shared<CanvasWidget>()->setSize(w,h); }
inline CanvasPtr Canvas(int w,int h,CanvasWidget::DrawFn fn)     { return std::make_shared<CanvasWidget>()->setSize(w,h)->onDraw(std::move(fn)); }

#endif // FLUX_CANVAS_HPP

// ============================================================================
// USAGE EXAMPLES  (API unchanged)
// ============================================================================
/*

// ── Drag a dot ───────────────────────────────────────────────────────────────
State<int>  dotX(300,context), dotY(200,context);
State<bool> dragging(false,context);

auto canvas = Canvas(700,480)
    ->bindState(dotX)->bindState(dotY)->bindState(dragging)
    ->onDraw([&](CanvasContext &ctx){
        int dx=dotX.get(), dy=dotY.get();
        ctx.fillStyle("#0d1117").fillRect(0,0,700,480);
        ctx.strokeStyle("#30363d").lineWidth(1);
        ctx.beginPath().moveTo(dx,0).lineTo(dx,480).stroke();
        ctx.beginPath().moveTo(0,dy).lineTo(700,dy).stroke();
        if(dragging.get()){
            ctx.strokeStyle("rgba(243,139,168,0.5)").lineWidth(12);
            ctx.strokeCircle(dx,dy,26);
        }
        ctx.fillStyle(dragging.get()?"#f38ba8":"#cba6f7").fillCircle(dx,dy,12);
        ctx.strokeStyle("#ffffff").lineWidth(1.5f).strokeCircle(dx,dy,12);
    })
    ->onMouseDown([&](int mx,int my){
        dragging.set(true); dotX.set(mx); dotY.set(my);
    })
    ->onMouseMove([&](int mx,int my){
        if(!dragging.get())return;
        dotX.set(std::clamp(mx,0,700)); dotY.set(std::clamp(my,0,480));
    })
    ->onMouseUp([&](int,int){ dragging.set(false); });


// ── Zoom with scroll wheel ────────────────────────────────────────────────────
State<float> zoom(1.f,context);

auto canvas = Canvas(600,400)
    ->bindState(zoom)
    ->onDraw([&](CanvasContext &ctx){
        float z=zoom.get();
        ctx.fillStyle("#111").fillRect(0,0,600,400);
        ctx.save().translate(300,200).scale(z,z).translate(-300,-200)
           .fillStyle("#3fb950").fillRect(250,150,100,100).restore();
    })
    ->onMouseWheel([&](int,int,int delta){
        float z=zoom.get()*(delta>0?1.1f:0.9f);
        zoom.set(std::clamp(z,0.1f,10.f));
    });


// ── Cursor change on hover ────────────────────────────────────────────────────
auto canvas = Canvas(400,300)
    ->onMouseEnter([](int,int){ SetCursor(LoadCursor(nullptr,IDC_HAND));  })
    ->onMouseLeave([](int,int){ SetCursor(LoadCursor(nullptr,IDC_ARROW)); });

*/