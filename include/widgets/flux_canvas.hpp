#ifndef FLUX_CANVAS_HPP
#define FLUX_CANVAS_HPP

// ============================================================================
// flux_canvas.hpp  –  CanvasWidget + pluggable RenderSurface architecture
// ============================================================================
//
// Architecture
// ────────────
//
//   ┌─────────────────────┐
//   │     Application     │  setSurface() / run()
//   └──────────┬──────────┘
//              │
//   ┌──────────▼──────────┐
//   │     CanvasWidget    │  Win32 HWND + GL context owner
//   │                     │  Input routing → activeSurface_
//   │                     │  Frame loop
//   └──────────┬──────────┘
//              │ owns unique_ptr<RenderSurface>
//   ┌──────────▼──────────┐
//   │    RenderSurface    │  Pure abstract interface
//   │                     │    initialize(w,h)
//   │                     │    resize(w,h)
//   │                     │    update(dt)
//   │                     │    render()
//   │                     │    onMouse*(x,y)
//   │                     │    onKey*(key)
//   └──────────┬──────────┘
//              │
//   ┌──────────▼──────────┐
//   │    RasterSurface    │  FBO-backed bitmap engine
//   │                     │    Stroke drawing (beginStroke /
//   │                     │    drawSegment / endStroke)
//   │                     │    clear()
//   │                     │    undo() / redo()
//   └─────────────────────┘
//
// Mouse Events (forwarded by CanvasWidget to the active surface)
// ────────────
//   Surface::onMouseDown(int x, int y)
//   Surface::onMouseMove(int x, int y)
//   Surface::onMouseUp  (int x, int y)
//   Surface::onKeyDown  (int key)
//   Surface::onKeyUp    (int key)
//
//   Coordinates are canvas-local pixels (top-left = 0,0).
//
// ============================================================================

#include "flux_widget.hpp"

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>

// ── OpenGL headers ──────────────────────────────────────────────────────────
#include "flux_gl_types.hpp"
#include <gl/GL.h>
#pragma comment(lib, "opengl32.lib")

#include <algorithm>
#include <cassert>
#include <cmath>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <deque>
#include <chrono>

using namespace Gdiplus;

// ============================================================================
// § 1  OPENGL 3.3 FUNCTION POINTER TYPEDEFS
// ============================================================================

using PFNWGLCREATECONTEXTATTRIBSARBPROC = HGLRC(WINAPI *)(HDC, HGLRC, const int *);

using PFNGLGENBUFFERSPROC              = void(APIENTRY *)(GLsizei, GLuint *);
using PFNGLDELETEBUFFERSPROC           = void(APIENTRY *)(GLsizei, const GLuint *);
using PFNGLBINDBUFFERPROC              = void(APIENTRY *)(GLenum, GLuint);
using PFNGLBUFFERDATAPROC              = void(APIENTRY *)(GLenum, GLsizeiptr, const void *, GLenum);
using PFNGLBUFFERSUBDATAPROC           = void(APIENTRY *)(GLenum, GLintptr, GLsizeiptr, const void *);
using PFNGLGENVERTEXARRAYSPROC         = void(APIENTRY *)(GLsizei, GLuint *);
using PFNGLDELETEVERTEXARRAYSPROC      = void(APIENTRY *)(GLsizei, const GLuint *);
using PFNGLBINDVERTEXARRAYPROC         = void(APIENTRY *)(GLuint);
using PFNGLGENFRAMEBUFFERSPROC         = void(APIENTRY *)(GLsizei, GLuint *);
using PFNGLDELETEFRAMEBUFFERSPROC      = void(APIENTRY *)(GLsizei, const GLuint *);
using PFNGLBINDFRAMEBUFFERPROC         = void(APIENTRY *)(GLenum, GLuint);
using PFNGLFRAMEBUFFERTEXTURE2DPROC    = void(APIENTRY *)(GLenum, GLenum, GLenum, GLuint, GLint);
using PFNGLCHECKFRAMEBUFFERSTATUSPROC  = GLenum(APIENTRY *)(GLenum);
using PFNGLCREATESHADERPROC            = GLuint(APIENTRY *)(GLenum);
using PFNGLDELETESHADERPROC            = void(APIENTRY *)(GLuint);
using PFNGLSHADERSOURCEPROC            = void(APIENTRY *)(GLuint, GLsizei, const GLchar *const *, const GLint *);
using PFNGLCOMPILESHADERPROC           = void(APIENTRY *)(GLuint);
using PFNGLGETSHADERIVPROC             = void(APIENTRY *)(GLuint, GLenum, GLint *);
using PFNGLGETSHADERINFOLOGPROC        = void(APIENTRY *)(GLuint, GLsizei, GLsizei *, GLchar *);
using PFNGLCREATEPROGRAMPROC           = GLuint(APIENTRY *)();
using PFNGLDELETEPROGRAMPROC           = void(APIENTRY *)(GLuint);
using PFNGLATTACHSHADERPROC            = void(APIENTRY *)(GLuint, GLuint);
using PFNGLLINKPROGRAMPROC             = void(APIENTRY *)(GLuint);
using PFNGLUSEPROGRAMPROC              = void(APIENTRY *)(GLuint);
using PFNGLGETUNIFORMLOCATIONPROC      = GLint(APIENTRY *)(GLuint, const GLchar *);
using PFNGLUNIFORM1IPROC               = void(APIENTRY *)(GLint, GLint);
using PFNGLUNIFORM1FPROC               = void(APIENTRY *)(GLint, GLfloat);
using PFNGLUNIFORM4FPROC               = void(APIENTRY *)(GLint, GLfloat, GLfloat, GLfloat, GLfloat);
using PFNGLUNIFORMMATRIX4FVPROC        = void(APIENTRY *)(GLint, GLsizei, GLboolean, const GLfloat *);
using PFNGLENABLEVERTEXATTRIBARRAYPROC = void(APIENTRY *)(GLuint);
using PFNGLDISABLEVERTEXATTRIBARRAYPROC= void(APIENTRY *)(GLuint);
using PFNGLVERTEXATTRIBPOINTERPROC     = void(APIENTRY *)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void *);

// GL constants not in the frozen Windows gl.h (GL 1.1)
#define GL_ARRAY_BUFFER                  0x8892
#define GL_STATIC_DRAW                   0x88B4
#define GL_DYNAMIC_DRAW                  0x88E8
#define GL_VERTEX_SHADER                 0x8B31
#define GL_FRAGMENT_SHADER               0x8B30
#define GL_COMPILE_STATUS                0x8B81
#define GL_LINK_STATUS                   0x8B82
#define GL_CLAMP_TO_EDGE                 0x812F
#define GL_FRAMEBUFFER                   0x8D40
#define GL_FRAMEBUFFER_COMPLETE          0x8CD5
#define GL_COLOR_ATTACHMENT0             0x8CE0
#define WGL_CONTEXT_MAJOR_VERSION_ARB    0x2091
#define WGL_CONTEXT_MINOR_VERSION_ARB    0x2092
#define WGL_CONTEXT_PROFILE_MASK_ARB     0x9126
#define WGL_CONTEXT_CORE_PROFILE_BIT_ARB 0x00000001

// ============================================================================
// § 2  GL PROC TABLE
// ============================================================================

struct GLProcs {
    PFNGLGENBUFFERSPROC               genBuffers{};
    PFNGLDELETEBUFFERSPROC            deleteBuffers{};
    PFNGLBINDBUFFERPROC               bindBuffer{};
    PFNGLBUFFERDATAPROC               bufferData{};
    PFNGLBUFFERSUBDATAPROC            bufferSubData{};
    PFNGLGENVERTEXARRAYSPROC          genVertexArrays{};
    PFNGLDELETEVERTEXARRAYSPROC       deleteVertexArrays{};
    PFNGLBINDVERTEXARRAYPROC          bindVertexArray{};
    PFNGLGENFRAMEBUFFERSPROC          genFramebuffers{};
    PFNGLDELETEFRAMEBUFFERSPROC       deleteFramebuffers{};
    PFNGLBINDFRAMEBUFFERPROC          bindFramebuffer{};
    PFNGLFRAMEBUFFERTEXTURE2DPROC     framebufferTexture2D{};
    PFNGLCHECKFRAMEBUFFERSTATUSPROC   checkFramebufferStatus{};
    PFNGLCREATESHADERPROC             createShader{};
    PFNGLDELETESHADERPROC             deleteShader{};
    PFNGLSHADERSOURCEPROC             shaderSource{};
    PFNGLCOMPILESHADERPROC            compileShader{};
    PFNGLGETSHADERIVPROC              getShaderiv{};
    PFNGLGETSHADERINFOLOGPROC         getShaderInfoLog{};
    PFNGLCREATEPROGRAMPROC            createProgram{};
    PFNGLDELETEPROGRAMPROC            deleteProgram{};
    PFNGLATTACHSHADERPROC             attachShader{};
    PFNGLLINKPROGRAMPROC              linkProgram{};
    PFNGLUSEPROGRAMPROC               useProgram{};
    PFNGLGETUNIFORMLOCATIONPROC       getUniformLocation{};
    PFNGLUNIFORM1IPROC                uniform1i{};
    PFNGLUNIFORM1FPROC                uniform1f{};
    PFNGLUNIFORM4FPROC                uniform4f{};
    PFNGLUNIFORMMATRIX4FVPROC         uniformMatrix4fv{};
    PFNGLENABLEVERTEXATTRIBARRAYPROC  enableVertexAttribArray{};
    PFNGLDISABLEVERTEXATTRIBARRAYPROC disableVertexAttribArray{};
    PFNGLVERTEXATTRIBPOINTERPROC      vertexAttribPointer{};

    static GLProcs &get() {
        static GLProcs p;
        return p;
    }

    void init() {
#define LOAD(fn, type, name) fn = reinterpret_cast<type>(wglGetProcAddress(name))
        LOAD(genBuffers,               PFNGLGENBUFFERSPROC,               "glGenBuffers");
        LOAD(deleteBuffers,            PFNGLDELETEBUFFERSPROC,            "glDeleteBuffers");
        LOAD(bindBuffer,               PFNGLBINDBUFFERPROC,               "glBindBuffer");
        LOAD(bufferData,               PFNGLBUFFERDATAPROC,               "glBufferData");
        LOAD(bufferSubData,            PFNGLBUFFERSUBDATAPROC,            "glBufferSubData");
        LOAD(genVertexArrays,          PFNGLGENVERTEXARRAYSPROC,          "glGenVertexArrays");
        LOAD(deleteVertexArrays,       PFNGLDELETEVERTEXARRAYSPROC,       "glDeleteVertexArrays");
        LOAD(bindVertexArray,          PFNGLBINDVERTEXARRAYPROC,          "glBindVertexArray");
        LOAD(genFramebuffers,          PFNGLGENFRAMEBUFFERSPROC,          "glGenFramebuffers");
        LOAD(deleteFramebuffers,       PFNGLDELETEFRAMEBUFFERSPROC,       "glDeleteFramebuffers");
        LOAD(bindFramebuffer,          PFNGLBINDFRAMEBUFFERPROC,          "glBindFramebuffer");
        LOAD(framebufferTexture2D,     PFNGLFRAMEBUFFERTEXTURE2DPROC,     "glFramebufferTexture2D");
        LOAD(checkFramebufferStatus,   PFNGLCHECKFRAMEBUFFERSTATUSPROC,   "glCheckFramebufferStatus");
        LOAD(createShader,             PFNGLCREATESHADERPROC,             "glCreateShader");
        LOAD(deleteShader,             PFNGLDELETESHADERPROC,             "glDeleteShader");
        LOAD(shaderSource,             PFNGLSHADERSOURCEPROC,             "glShaderSource");
        LOAD(compileShader,            PFNGLCOMPILESHADERPROC,            "glCompileShader");
        LOAD(getShaderiv,              PFNGLGETSHADERIVPROC,              "glGetShaderiv");
        LOAD(getShaderInfoLog,         PFNGLGETSHADERINFOLOGPROC,         "glGetShaderInfoLog");
        LOAD(createProgram,            PFNGLCREATEPROGRAMPROC,            "glCreateProgram");
        LOAD(deleteProgram,            PFNGLDELETEPROGRAMPROC,            "glDeleteProgram");
        LOAD(attachShader,             PFNGLATTACHSHADERPROC,             "glAttachShader");
        LOAD(linkProgram,              PFNGLLINKPROGRAMPROC,              "glLinkProgram");
        LOAD(useProgram,               PFNGLUSEPROGRAMPROC,               "glUseProgram");
        LOAD(getUniformLocation,       PFNGLGETUNIFORMLOCATIONPROC,       "glGetUniformLocation");
        LOAD(uniform1i,                PFNGLUNIFORM1IPROC,                "glUniform1i");
        LOAD(uniform1f,                PFNGLUNIFORM1FPROC,                "glUniform1f");
        LOAD(uniform4f,                PFNGLUNIFORM4FPROC,                "glUniform4f");
        LOAD(uniformMatrix4fv,         PFNGLUNIFORMMATRIX4FVPROC,         "glUniformMatrix4fv");
        LOAD(enableVertexAttribArray,  PFNGLENABLEVERTEXATTRIBARRAYPROC,  "glEnableVertexAttribArray");
        LOAD(disableVertexAttribArray, PFNGLDISABLEVERTEXATTRIBARRAYPROC, "glDisableVertexAttribArray");
        LOAD(vertexAttribPointer,      PFNGLVERTEXATTRIBPOINTERPROC,      "glVertexAttribPointer");
#undef LOAD
    }

private:
    GLProcs() = default;
};

// Process-wide singleton reference
static inline GLProcs &GL = GLProcs::get();

// ============================================================================
// § 3  GL UTILITIES
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

// Column-major 4×4 orthographic: [0,w]×[0,h] → NDC, y=0 at top.
inline void ortho(float w, float h, float out[16]) {
    float r = 1.f / w, t = 1.f / h;
    out[0]=2*r; out[1]=0;    out[2]=0;  out[3]=0;
    out[4]=0;   out[5]=-2*t; out[6]=0;  out[7]=0;
    out[8]=0;   out[9]=0;    out[10]=-1;out[11]=0;
    out[12]=-1; out[13]=1;   out[14]=0; out[15]=1;
}

} // namespace glutil

// ============================================================================
// § 4  COLOR HELPERS  (available to app code)
// ============================================================================

struct RGBA { float r, g, b, a; };

// Parse a CSS color string ("#rrggbb", "#rgb", "rgb(r,g,b[,a])", named colors)
// into a normalized RGBA struct.
inline RGBA parseHexColor(const std::string &css) {
    auto h2f = [](unsigned v) { return v / 255.f; };
    if (!css.empty() && css[0] == '#') {
        std::string h = css.substr(1);
        if (h.size() == 3) {
            auto d = [&](int i) -> unsigned { char c = h[i]; return c>='0'&&c<='9' ? c-'0' : c>='a'&&c<='f' ? c-'a'+10 : c-'A'+10; };
            unsigned r=d(0), g=d(1), b=d(2);
            return { h2f(r|(r<<4)), h2f(g|(g<<4)), h2f(b|(b<<4)), 1.f };
        }
        if (h.size() == 6) {
            unsigned n = (unsigned)std::stoul(h, nullptr, 16);
            return { h2f((n>>16)&0xFF), h2f((n>>8)&0xFF), h2f(n&0xFF), 1.f };
        }
        if (h.size() == 8) {
            unsigned n = (unsigned)std::stoul(h, nullptr, 16);
            return { h2f((n>>24)&0xFF), h2f((n>>16)&0xFF), h2f((n>>8)&0xFF), h2f(n&0xFF) };
        }
    }
    if (css.size() >= 3 && css.substr(0,3) == "rgb") {
        auto a = css.find('('), b = css.find(')');
        if (a != std::string::npos) {
            std::string inner = css.substr(a+1, b-a-1);
            float vals[4] = {0,0,0,1}; int i = 0; size_t pos = 0;
            while (i < 4) {
                size_t comma = inner.find(',', pos);
                std::string tok = inner.substr(pos, comma == std::string::npos ? std::string::npos : comma-pos);
                vals[i++] = std::stof(tok);
                if (comma == std::string::npos) break;
                pos = comma + 1;
            }
            return { vals[0]/255.f, vals[1]/255.f, vals[2]/255.f, vals[3] };
        }
    }
    static const struct { const char *name; RGBA col; } kNamed[] = {
        {"black",{0,0,0,1}},   {"white",{1,1,1,1}},      {"red",{1,0,0,1}},
        {"green",{0,.5f,0,1}}, {"blue",{0,0,1,1}},        {"yellow",{1,1,0,1}},
        {"cyan",{0,1,1,1}},    {"magenta",{1,0,1,1}},     {"orange",{1,.647f,0,1}},
        {"purple",{.5f,0,.5f,1}}, {"gray",{.5f,.5f,.5f,1}}, {"grey",{.5f,.5f,.5f,1}},
        {"transparent",{0,0,0,0}},
    };
    std::string lo = css;
    for (auto &c : lo) c = (char)std::tolower((unsigned char)c);
    for (auto &e : kNamed) if (lo == e.name) return e.col;
    return {1,1,1,1};
}

// ============================================================================
// § 5  STROKE DATA TYPES
// ============================================================================

struct StrokePoint {
    float x, y;
    float pressure; // 0..1, unused for now but reserved for stylus support
};

struct StrokeStyle {
    float r = 0.f, g = 0.f, b = 0.f, a = 1.f; // RGBA 0..1
    float radius   = 4.f;   // brush radius in canvas pixels
    float hardness = 0.8f;  // 0 = fully soft, 1 = hard edge
    float opacity  = 1.f;   // per-stroke opacity multiplier
};

struct Stroke {
    StrokeStyle         style;
    std::vector<StrokePoint> points;
};

// ============================================================================
// § 5  RENDER SURFACE  —  pure abstract interface
// ============================================================================
//
// CanvasWidget owns exactly one RenderSurface and calls these methods:
//
//   initialize(w, h)        Once, after GL context is current.
//   resize(w, h)            Whenever the child HWND changes size.
//   update(dt)              Called every frame before render(); dt in seconds.
//   render()                Draw the current frame (SwapBuffers inside).
//   onMouseDown(x, y)       Canvas-local pixel coordinates.
//   onMouseMove(x, y)
//   onMouseUp  (x, y)
//   onKeyDown  (key)        Virtual-key codes (VK_*)
//   onKeyUp    (key)
//   destroy()               Free all GPU resources (GL context current).

class RenderSurface {
public:
    virtual ~RenderSurface() = default;

    virtual void initialize(int w, int h) = 0;
    virtual void resize(int w, int h)     = 0;
    virtual void update(double dt)        = 0;
    virtual void render()                 = 0;

    virtual void onMouseDown(int x, int y) {}
    virtual void onMouseMove(int x, int y) {}
    virtual void onMouseUp  (int x, int y) {}
    virtual void onKeyDown  (int key)      {}
    virtual void onKeyUp    (int key)      {}

    virtual void destroy() = 0;
};

// ============================================================================
// § 6  RASTER SURFACE  —  FBO-backed bitmap drawing engine
// ============================================================================
//
// Drawing model
// ─────────────
//   Two persistent FBO layers are maintained:
//     committed_ — fully committed strokes (persisted on mouseUp / undo unit)
//     scratch_   — the stroke currently being drawn (merged into committed_
//                  on endStroke() and erased on undo of the live stroke)
//
//   Every render() blits committed_ then scratch_ onto the back-buffer via a
//   single full-screen textured quad per layer.
//
// Undo / Redo
// ───────────
//   Each completed stroke pushes a snapshot of committed_ onto undoStack_.
//   undo() pops the top snapshot and restores committed_; the popped stroke
//   goes onto redoStack_.  Any new stroke clears redoStack_.
//   Snapshots are raw RGBA pixel buffers — compact and allocation-free on the
//   GPU side.
//
// Public surface-level API (call from your application via the surface ptr):
//   setStrokeStyle(StrokeStyle)
//   clear()
//   undo()   / redo()
//   canUndo()/ canRedo()

class RasterSurface : public RenderSurface {
public:
    // ── Construction / destruction ──────────────────────────────────────────

    RasterSurface() = default;
    ~RasterSurface() { destroy(); }

    // ── Stroke style ────────────────────────────────────────────────────────

    void setStrokeStyle(const StrokeStyle &s) { style_ = s; }
    const StrokeStyle &getStrokeStyle() const  { return style_; }

    // ── Undo / redo ─────────────────────────────────────────────────────────

    void undo() {
        if (undoStack_.empty()) return;
        redoStack_.push_back(snapshotCommitted());
        restoreCommitted(undoStack_.back());
        undoStack_.pop_back();
        dirty_ = true;
    }

    void redo() {
        if (redoStack_.empty()) return;
        undoStack_.push_back(snapshotCommitted());
        restoreCommitted(redoStack_.back());
        redoStack_.pop_back();
        dirty_ = true;
    }

    bool canUndo() const { return !undoStack_.empty(); }
    bool canRedo() const { return !redoStack_.empty(); }

    // ── Clear ────────────────────────────────────────────────────────────────

    void clear() {
        pushUndoSnapshot();
        clearFBO(committedFBO_, w_, h_, 0, 0, 0, 0);
        clearFBO(scratchFBO_,   w_, h_, 0, 0, 0, 0);
        dirty_ = true;
    }

    // ── RenderSurface interface ──────────────────────────────────────────────

    void initialize(int w, int h) override {
        w_ = w; h_ = h;
        buildShaders();
        buildQuadVAO();
        allocFBOPair(committedFBO_, committedTex_, w_, h_);
        allocFBOPair(scratchFBO_,   scratchTex_,   w_, h_);
        clearFBO(committedFBO_, w_, h_, 255, 255, 255, 255); // white canvas
        clearFBO(scratchFBO_,   w_, h_, 0, 0, 0, 0);
        glutil::ortho((float)w_, (float)h_, proj_);
    }

    void resize(int w, int h) override {
        if (w == w_ && h == h_) return;

        // Snapshot old committed layer pixels so we can blit them into the new size
        auto oldPixels = snapshotCommitted();
        int  oldW = w_, oldH = h_;

        w_ = w; h_ = h;

        // Re-allocate FBOs at the new size
        destroyFBOPair(committedFBO_, committedTex_);
        destroyFBOPair(scratchFBO_,   scratchTex_);
        allocFBOPair(committedFBO_, committedTex_, w_, h_);
        allocFBOPair(scratchFBO_,   scratchTex_,   w_, h_);

        // Clear both, then blit the old committed pixels back
        clearFBO(committedFBO_, w_, h_, 255, 255, 255, 255);
        clearFBO(scratchFBO_,   w_, h_, 0, 0, 0, 0);
        restoreCommittedAt(oldPixels, oldW, oldH); // blit top-left corner

        glutil::ortho((float)w_, (float)h_, proj_);
        glViewport(0, 0, w_, h_);
        dirty_ = true;
    }

    void update(double /*dt*/) override {
        // Nothing to simulate right now; dt is available for future animations.
    }

    void render() override {
        glViewport(0, 0, w_, h_);
        glDisable(GL_SCISSOR_TEST);
        glClearColor(1, 1, 1, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        // 1. Draw committed layer
        blitTexture(committedTex_, 1.f);

        // 2. Draw scratch (in-progress stroke) on top
        if (drawing_)
            blitTexture(scratchTex_, 1.f);

        dirty_ = false;
    }

    void onMouseDown(int x, int y) override {
        beginStroke(x, y);
    }

    void onMouseMove(int x, int y) override {
        if (drawing_) drawSegment(x, y);
    }

    void onMouseUp(int x, int y) override {
        if (drawing_) endStroke(x, y);
    }

    void onKeyDown(int key) override {
        // Ctrl+Z → undo,  Ctrl+Y / Ctrl+Shift+Z → redo
        bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        bool shift= (GetKeyState(VK_SHIFT)   & 0x8000) != 0;
        if (ctrl && key == 'Z') { if (shift) redo(); else undo(); }
        if (ctrl && key == 'Y') redo();
    }

    void onKeyUp(int /*key*/) override {}

    void destroy() override {
        destroyFBOPair(committedFBO_, committedTex_);
        destroyFBOPair(scratchFBO_,   scratchTex_);
        if (blitProg_)  { GL.deleteProgram(blitProg_);  blitProg_  = 0; }
        if (quadVAO_)   { GL.deleteVertexArrays(1, &quadVAO_); quadVAO_ = 0; }
        if (quadVBO_)   { GL.deleteBuffers(1, &quadVBO_);      quadVBO_ = 0; }
        undoStack_.clear();
        redoStack_.clear();
    }

private:
    // ── State ────────────────────────────────────────────────────────────────
    int w_ = 0, h_ = 0;
    bool drawing_ = false;
    bool dirty_   = false;
    StrokePoint   lastPt_{};
    StrokeStyle   style_{};

    // FBO handles
    GLuint committedFBO_ = 0, committedTex_ = 0;
    GLuint scratchFBO_   = 0, scratchTex_   = 0;

    // Shader / geometry
    GLuint blitProg_ = 0;
    GLuint quadVAO_  = 0, quadVBO_ = 0;
    float  proj_[16]{};

    // Undo/redo stacks — each entry is a raw RGBA pixel buffer (w_×h_×4 bytes)
    static constexpr size_t kMaxUndoDepth = 64;
    std::deque<std::vector<uint8_t>> undoStack_;
    std::deque<std::vector<uint8_t>> redoStack_;

    // ── Stroke implementation ────────────────────────────────────────────────

    void beginStroke(int x, int y) {
        if (drawing_) return;
        drawing_ = true;
        lastPt_ = {(float)x, (float)y, 1.f};
        redoStack_.clear(); // new drawing invalidates redo history
        // Clear scratch layer
        clearFBO(scratchFBO_, w_, h_, 0, 0, 0, 0);
    }

    void drawSegment(int x, int y) {
        StrokePoint cur{(float)x, (float)y, 1.f};
        paintDab(scratchFBO_, cur.x, cur.y, style_);
        // Interpolate along the segment for smooth lines
        float dx = cur.x - lastPt_.x, dy = cur.y - lastPt_.y;
        float dist = std::sqrt(dx*dx + dy*dy);
        float step = max(1.f, style_.radius * 0.25f);
        if (dist > step) {
            int steps = (int)(dist / step);
            for (int i = 1; i < steps; ++i) {
                float t = (float)i / steps;
                paintDab(scratchFBO_,
                         lastPt_.x + dx * t,
                         lastPt_.y + dy * t,
                         style_);
            }
        }
        lastPt_ = cur;
        dirty_  = true;
    }

    void endStroke(int x, int y) {
        drawSegment(x, y);
        // Commit: push undo snapshot, merge scratch → committed, clear scratch
        pushUndoSnapshot();
        mergeScratchIntoCommitted();
        clearFBO(scratchFBO_, w_, h_, 0, 0, 0, 0);
        drawing_ = false;
        dirty_   = true;
    }

    // ── FBO / texture helpers ────────────────────────────────────────────────

    void allocFBOPair(GLuint &fbo, GLuint &tex, int w, int h) {
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glBindTexture(GL_TEXTURE_2D, 0);

        GL.genFramebuffers(1, &fbo);
        GL.bindFramebuffer(GL_FRAMEBUFFER, fbo);
        GL.framebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
        GL.bindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    void destroyFBOPair(GLuint &fbo, GLuint &tex) {
        if (fbo) { GL.deleteFramebuffers(1, &fbo); fbo = 0; }
        if (tex) { glDeleteTextures(1, &tex);       tex = 0; }
    }

    void clearFBO(GLuint fbo, int w, int h, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
        GL.bindFramebuffer(GL_FRAMEBUFFER, fbo);
        glViewport(0, 0, w, h);
        glClearColor(r/255.f, g/255.f, b/255.f, a/255.f);
        glClear(GL_COLOR_BUFFER_BIT);
        GL.bindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    // ── Snapshot / restore ───────────────────────────────────────────────────

    std::vector<uint8_t> snapshotCommitted() {
        std::vector<uint8_t> pixels(w_ * h_ * 4);
        GL.bindFramebuffer(GL_FRAMEBUFFER, committedFBO_);
        glReadPixels(0, 0, w_, h_, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        GL.bindFramebuffer(GL_FRAMEBUFFER, 0);
        return pixels;
    }

    void restoreCommitted(const std::vector<uint8_t> &pixels) {
        glBindTexture(GL_TEXTURE_2D, committedTex_);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w_, h_, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    // Blit old pixels into the top-left of the newly-sized committed texture
    void restoreCommittedAt(const std::vector<uint8_t> &pixels, int oldW, int oldH) {
        int copyW = min(oldW, w_);
        int copyH = min(oldH, h_);
        // Build a new buffer of size w_ × h_ (white background)
        std::vector<uint8_t> buf(w_ * h_ * 4, 255);
        for (int row = 0; row < copyH; ++row) {
            const uint8_t *src = pixels.data() + row * oldW * 4;
            uint8_t       *dst = buf.data()    + row * w_   * 4;
            memcpy(dst, src, copyW * 4);
        }
        restoreCommitted(buf);
    }

    void pushUndoSnapshot() {
        if (undoStack_.size() >= kMaxUndoDepth) undoStack_.pop_front();
        undoStack_.push_back(snapshotCommitted());
    }

    // ── Merge scratch → committed ────────────────────────────────────────────
    // We draw the scratch texture on top of the committed FBO using the same
    // blit shader (alpha blend).

    void mergeScratchIntoCommitted() {
        GL.bindFramebuffer(GL_FRAMEBUFFER, committedFBO_);
        glViewport(0, 0, w_, h_);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        blitTexture(scratchTex_, 1.f);
        GL.bindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    // ── Brush dab ────────────────────────────────────────────────────────────
    // Renders a soft circular dab at (cx, cy) into the given FBO using the
    // colour shader (a triangle-fan approximation of a disk).

    void paintDab(GLuint fbo, float cx, float cy, const StrokeStyle &s) {
        GL.bindFramebuffer(GL_FRAMEBUFFER, fbo);
        glViewport(0, 0, w_, h_);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        GL.useProgram(blitProg_);
        GL.uniformMatrix4fv(GL.getUniformLocation(blitProg_, "uMVP"), 1, GL_FALSE, proj_);
        GL.uniform1i(GL.getUniformLocation(blitProg_, "uMode"), 1); // solid-colour mode
        GL.uniform4f(GL.getUniformLocation(blitProg_, "uColor"),
                     s.r, s.g, s.b, s.a * s.opacity);

        // Build a disk fan (centre + N perimeter verts)
        constexpr int N = 32;
        std::vector<float> verts;
        verts.reserve((N + 2) * 2);
        verts.push_back(cx); verts.push_back(cy);
        for (int i = 0; i <= N; ++i) {
            float a = (float)i / N * 6.28318530f;
            verts.push_back(cx + s.radius * std::cos(a));
            verts.push_back(cy + s.radius * std::sin(a));
        }

        GL.bindVertexArray(quadVAO_);
        GL.bindBuffer(GL_ARRAY_BUFFER, quadVBO_);
        GL.bufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(verts.size() * sizeof(float)),
                      verts.data(), GL_DYNAMIC_DRAW);
        GL.enableVertexAttribArray(0);
        GL.vertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(float)*2, nullptr);
        glDrawArrays(GL_TRIANGLE_FAN, 0, (GLsizei)(verts.size() / 2));
        GL.bindVertexArray(0);

        GL.bindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    // ── Blit a texture to the currently-bound framebuffer ───────────────────

    void blitTexture(GLuint tex, float alpha) {
        GL.useProgram(blitProg_);
        GL.uniformMatrix4fv(GL.getUniformLocation(blitProg_, "uMVP"), 1, GL_FALSE, proj_);
        GL.uniform1i(GL.getUniformLocation(blitProg_, "uMode"),  0); // texture mode
        GL.uniform1i(GL.getUniformLocation(blitProg_, "uTex"),   0);
        GL.uniform1f(GL.getUniformLocation(blitProg_, "uAlpha"), alpha);
        glBindTexture(GL_TEXTURE_2D, tex);

        // Full-screen quad: two triangles covering [0,w_]×[0,h_]
        float q[] = {
            0.f,   0.f,   0.f, 1.f,
            (float)w_, 0.f,   1.f, 1.f,
            (float)w_, (float)h_, 1.f, 0.f,
            (float)w_, (float)h_, 1.f, 0.f,
            0.f,   (float)h_, 0.f, 0.f,
            0.f,   0.f,   0.f, 1.f,
        };
        GL.bindVertexArray(quadVAO_);
        GL.bindBuffer(GL_ARRAY_BUFFER, quadVBO_);
        GL.bufferData(GL_ARRAY_BUFFER, sizeof(q), q, GL_DYNAMIC_DRAW);
        // attrib 0: vec2 pos  (stride 4 floats, offset 0)
        GL.enableVertexAttribArray(0);
        GL.vertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(float)*4, (void*)0);
        // attrib 1: vec2 uv   (stride 4 floats, offset 8 bytes)
        GL.enableVertexAttribArray(1);
        GL.vertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(float)*4, (void*)(sizeof(float)*2));
        glDrawArrays(GL_TRIANGLES, 0, 6);
        GL.bindVertexArray(0);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    // ── Shader + geometry setup ──────────────────────────────────────────────

    void buildShaders() {
        // Single program handles two modes:
        //   uMode == 0 → texture blit  (uses uTex + uAlpha)
        //   uMode == 1 → solid colour  (uses uColor)
        const char *vert = R"GLSL(
#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
uniform mat4 uMVP;
out vec2 vUV;
void main() { vUV = aUV; gl_Position = uMVP * vec4(aPos, 0.0, 1.0); }
)GLSL";
        const char *frag = R"GLSL(
#version 330 core
in  vec2 vUV;
uniform sampler2D uTex;
uniform vec4      uColor;
uniform float     uAlpha;
uniform int       uMode;
out vec4 fragColor;
void main() {
    if (uMode == 0) {
        vec4 c = texture(uTex, vUV);
        fragColor = vec4(c.rgb, c.a * uAlpha);
    } else {
        fragColor = uColor;
    }
}
)GLSL";
        blitProg_ = glutil::linkProgram(vert, frag);
    }

    void buildQuadVAO() {
        GL.genVertexArrays(1, &quadVAO_);
        GL.genBuffers(1, &quadVBO_);
        GL.bindVertexArray(quadVAO_);
        GL.bindBuffer(GL_ARRAY_BUFFER, quadVBO_);
        // We'll fill this dynamically per draw call
        GL.bufferData(GL_ARRAY_BUFFER, sizeof(float) * 6 * 4, nullptr, GL_DYNAMIC_DRAW);
        GL.bindVertexArray(0);
    }
};

// ============================================================================
// § 7  CANVAS WIDGET
// ============================================================================
//
// Responsibilities:
//   • Win32 child HWND with CS_OWNDC
//   • GL context bootstrap (legacy → Core 3.3 two-step)
//   • GL proc loading (once per process)
//   • Active surface ownership and lifetime
//   • Mouse / keyboard event routing to the surface
//   • Steady-state frame timer (WM_TIMER) → update() + render()

class CanvasWidget : public Widget {
public:
    explicit CanvasWidget() {
        autoWidth = autoHeight = false;
        width  = 400;
        height = 300;
    }
    ~CanvasWidget() { destroyGL(); }

    // ── Surface management ────────────────────────────────────────────────────

    /// Replace the active surface.  If the GL context already exists the new
    /// surface is initialized immediately; otherwise initialization is deferred
    /// to the first render() call.
    template<typename SurfaceT, typename... Args>
    std::shared_ptr<SurfaceT> setSurface(Args&&... args) {
        auto s = std::make_shared<SurfaceT>(std::forward<Args>(args)...);
        pendingSurface_ = s;
        return s;
    }

    /// Direct pointer access to the active surface (cast to your type).
    RenderSurface *getSurface() const { return activeSurface_.get(); }

    // ── Fluent API ────────────────────────────────────────────────────────────

    std::shared_ptr<CanvasWidget> setSize(int w, int h) {
        width = w; height = h;
        autoWidth = autoHeight = false;
        markNeedsLayout();
        return ptr();
    }

    std::shared_ptr<CanvasWidget> redraw() {
        markNeedsPaint();
        return ptr();
    }

    // ── Widget overrides ──────────────────────────────────────────────────────

    void computeLayout(HDC, const BoxConstraints &c, FontCache &) override {
        width  = c.clampWidth (autoWidth  ? c.maxWidth  : width);
        height = c.clampHeight(autoHeight ? c.maxHeight : height);
        needsLayout = false;
    }

    void render(HDC hdc, FontCache &) override {
        ensureChildWindow(hdc);
        moveChildWindow();
        tickAndRender();
        needsPaint = false;
    }

    void onDetach() override {
        destroyGL();
        Widget::onDetach();
    }

    void markNeedsPaint() override {
        Widget::markNeedsPaint();
        if (childHwnd && !repaintPending_) {
            repaintPending_ = true;
            PostMessage(childHwnd, WM_USER + 1, 0, 0);
        }
    }

private:
    std::shared_ptr<CanvasWidget> ptr() {
        return std::static_pointer_cast<CanvasWidget>(shared_from_this());
    }

    // ── Surface slots ─────────────────────────────────────────────────────────

    std::shared_ptr<RenderSurface> activeSurface_;  // currently live
    std::shared_ptr<RenderSurface> pendingSurface_;  // waiting for GL context

    // ── Timing ───────────────────────────────────────────────────────────────

    using Clock = std::chrono::steady_clock;
    Clock::time_point lastTick_ = Clock::now();

    // ── Win32 handles ─────────────────────────────────────────────────────────

    HWND  childHwnd  = nullptr;
    HDC   glDC_      = nullptr;
    HGLRC glRC_      = nullptr;
    HWND  parentHwnd = nullptr;
    bool  repaintPending_ = false;
    bool  mouseInside_    = false;
    bool  trackingLeave_  = false;

    // ── Child window proc ─────────────────────────────────────────────────────

    static LRESULT CALLBACK ChildProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        CanvasWidget *self = reinterpret_cast<CanvasWidget *>(
            GetWindowLongPtrW(hwnd, GWLP_USERDATA));

        switch (msg) {
        // ── Repaint dedup ─────────────────────────────────────────────────────
        case WM_USER + 1:
            if (self) {
                self->repaintPending_ = false;
                self->tickAndRender();
            }
            return 0;

        // Suppress default background erase (GL owns the surface)
        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(hwnd, &ps);
            EndPaint(hwnd, &ps);
            return 0;
        }

        // ── Mouse ─────────────────────────────────────────────────────────────
        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_MBUTTONDOWN:
            SetCapture(hwnd);
            if (self && self->activeSurface_)
                self->activeSurface_->onMouseDown(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
            forwardToParent(hwnd, msg, wp, lp);
            return 0;

        case WM_LBUTTONUP:
        case WM_RBUTTONUP:
        case WM_MBUTTONUP:
            ReleaseCapture();
            if (self && self->activeSurface_)
                self->activeSurface_->onMouseUp(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
            forwardToParent(hwnd, msg, wp, lp);
            return 0;

        case WM_MOUSEMOVE: {
            int mx = GET_X_LPARAM(lp), my = GET_Y_LPARAM(lp);
            if (self) {
                if (!self->trackingLeave_) {
                    TRACKMOUSEEVENT tme{};
                    tme.cbSize  = sizeof(tme);
                    tme.dwFlags = TME_LEAVE;
                    tme.hwndTrack = hwnd;
                    TrackMouseEvent(&tme);
                    self->trackingLeave_ = true;
                }
                self->mouseInside_ = true;
                if (self->activeSurface_)
                    self->activeSurface_->onMouseMove(mx, my);
            }
            forwardToParent(hwnd, msg, wp, lp);
            return 0;
        }

        case WM_MOUSELEAVE:
            if (self) {
                self->trackingLeave_ = false;
                self->mouseInside_   = false;
            }
            return 0;

        case WM_MOUSEWHEEL:
            forwardToParent(hwnd, msg, wp, lp);
            return 0;

        // ── Keyboard ──────────────────────────────────────────────────────────
        case WM_KEYDOWN:
            if (self && self->activeSurface_)
                self->activeSurface_->onKeyDown((int)wp);
            return 0;

        case WM_KEYUP:
            if (self && self->activeSurface_)
                self->activeSurface_->onKeyUp((int)wp);
            return 0;

        default:
            return DefWindowProc(hwnd, msg, wp, lp);
        }
    }

    static void forwardToParent(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        HWND parent = GetParent(hwnd);
        if (!parent) return;
        POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        MapWindowPoints(hwnd, parent, &pt, 1);
        PostMessage(parent, msg, wp, MAKELPARAM(pt.x, pt.y));
    }

    // ── Child HWND class registration ─────────────────────────────────────────

    static void registerChildClass() {
        static bool done = false;
        if (done) return;
        WNDCLASSEXW wc{};
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = ChildProc;
        wc.hInstance     = GetModuleHandle(nullptr);
        wc.lpszClassName = L"FluxGLCanvas2";
        wc.style         = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
        RegisterClassExW(&wc);
        done = true;
    }

    // ── GL context bootstrap ──────────────────────────────────────────────────
    // Two-step: legacy context → load wglCreateContextAttribsARB → Core 3.3.

    void ensureChildWindow(HDC parentDC) {
        if (childHwnd) return;

        HWND owner = WindowFromDC(parentDC);
        if (!owner) owner = GetActiveWindow();
        parentHwnd = owner;

        registerChildClass();

        childHwnd = CreateWindowExW(
            WS_EX_NOPARENTNOTIFY, L"FluxGLCanvas2", nullptr,
            WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
            x, y, width, height, owner, nullptr, GetModuleHandle(nullptr), nullptr);
        if (!childHwnd) return;

        SetWindowLongPtrW(childHwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

        glDC_ = GetDC(childHwnd);
        setupPixelFormat(glDC_);

        // Step 1: temporary legacy context (needed to call wglGetProcAddress)
        HGLRC tmpRC = wglCreateContext(glDC_);
        wglMakeCurrent(glDC_, tmpRC);

        // Step 2: upgrade to Core 3.3 if the driver supports it
        auto wglCreateCtxAttribs =
            reinterpret_cast<PFNWGLCREATECONTEXTATTRIBSARBPROC>(
                wglGetProcAddress("wglCreateContextAttribsARB"));
        if (wglCreateCtxAttribs) {
            const int attribs[] = {
                WGL_CONTEXT_MAJOR_VERSION_ARB, 3,
                WGL_CONTEXT_MINOR_VERSION_ARB, 3,
                WGL_CONTEXT_PROFILE_MASK_ARB,  WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
                0
            };
            HGLRC coreRC = wglCreateCtxAttribs(glDC_, nullptr, attribs);
            if (coreRC) {
                wglMakeCurrent(nullptr, nullptr);
                wglDeleteContext(tmpRC);
                glRC_ = coreRC;
            } else {
                glRC_ = tmpRC; // driver too old — stay on legacy
            }
        } else {
            glRC_ = tmpRC;
        }
        wglMakeCurrent(glDC_, glRC_);

        // Step 3: populate GL proc table (once per process)
        GL.init();

        // Step 4: activate any pending surface
        activatePendingSurface();

        lastTick_ = Clock::now();
    }

    void setupPixelFormat(HDC dc) {
        PIXELFORMATDESCRIPTOR pfd{};
        pfd.nSize      = sizeof(pfd);
        pfd.nVersion   = 1;
        pfd.dwFlags    = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
        pfd.iPixelType = PFD_TYPE_RGBA;
        pfd.cColorBits = 32;
        pfd.cDepthBits = 24;
        pfd.iLayerType = PFD_MAIN_PLANE;
        SetPixelFormat(dc, ChoosePixelFormat(dc, &pfd), &pfd);
    }

    // ── Surface activation ────────────────────────────────────────────────────

    void activatePendingSurface() {
        if (!pendingSurface_) return;
        if (activeSurface_) {
            activeSurface_->destroy();
            activeSurface_.reset();
        }
        activeSurface_ = pendingSurface_;
        pendingSurface_.reset();
        activeSurface_->initialize(width, height);
    }

    // ── Frame loop ────────────────────────────────────────────────────────────

    void tickAndRender() {
        if (!glRC_ || !glDC_) return;

        // Activate any surface that arrived after the GL context was created
        if (pendingSurface_)
            activatePendingSurface();

        if (!activeSurface_) return;

        if (wglGetCurrentContext() != glRC_)
            wglMakeCurrent(glDC_, glRC_);

        // Compute delta time
        auto now = Clock::now();
        double dt = std::chrono::duration<double>(now - lastTick_).count();
        lastTick_ = now;

        activeSurface_->update(dt);
        activeSurface_->render();
        SwapBuffers(glDC_);
    }

    void moveChildWindow() {
        if (!childHwnd) return;
        SetWindowPos(childHwnd, nullptr, x, y, width, height,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        if (activeSurface_ && (width != lastW_ || height != lastH_)) {
            if (wglGetCurrentContext() != glRC_)
                wglMakeCurrent(glDC_, glRC_);
            activeSurface_->resize(width, height);
            lastW_ = width;
            lastH_ = height;
        }
    }

    int lastW_ = 0, lastH_ = 0;

    void destroyGL() {
        if (glRC_) {
            wglMakeCurrent(glDC_, glRC_);
            if (activeSurface_) { activeSurface_->destroy(); activeSurface_.reset(); }
            if (pendingSurface_) pendingSurface_.reset();
            wglMakeCurrent(nullptr, nullptr);
            wglDeleteContext(glRC_);
            glRC_ = nullptr;
        }
        if (childHwnd && glDC_) { ReleaseDC(childHwnd, glDC_); glDC_ = nullptr; }
        if (childHwnd)           { DestroyWindow(childHwnd);    childHwnd = nullptr; }
    }
};

// ============================================================================
// § 8  FACTORY HELPERS
// ============================================================================

using CanvasPtr = std::shared_ptr<CanvasWidget>;

/// Bare canvas widget — attach a surface with setSurface<T>().
inline CanvasPtr Canvas() {
    return std::make_shared<CanvasWidget>();
}

/// Canvas with explicit size.
inline CanvasPtr Canvas(int w, int h) {
    return std::make_shared<CanvasWidget>()->setSize(w, h);
}

/// Canvas pre-wired with a RasterSurface (the most common use case).
inline CanvasPtr RasterCanvas(int w, int h) {
    auto c = std::make_shared<CanvasWidget>()->setSize(w, h);
    c->setSurface<RasterSurface>();
    return c;
}

#endif // FLUX_CANVAS_HPP

// ============================================================================
// USAGE EXAMPLE — drawing application with undo / redo
// ============================================================================
/*

// Create a drawing canvas
auto canvas = RasterCanvas(800, 600);

// Get the surface to configure stroke style and handle undo/redo
auto surface = std::static_pointer_cast<RasterSurface>(
    std::shared_ptr<RenderSurface>(canvas->getSurface(), [](auto*){}) // non-owning view
);

// Or use setSurface<> and keep the returned shared_ptr:
auto raster = canvas->setSurface<RasterSurface>();

// Set initial brush
StrokeStyle brush;
brush.r = 0.1f; brush.g = 0.1f; brush.b = 0.8f; brush.a = 1.f;
brush.radius  = 6.f;
brush.opacity = 0.9f;
raster->setStrokeStyle(brush);

// Mouse events are automatically forwarded from CanvasWidget → RasterSurface.
// No extra wiring needed: onMouseDown/Move/Up drive beginStroke/drawSegment/endStroke.

// Keyboard:  Ctrl+Z → undo,  Ctrl+Y / Ctrl+Shift+Z → redo
// (handled inside RasterSurface::onKeyDown — nothing to set up)

// You can also call undo/redo programmatically (e.g. from toolbar buttons):
//   if (raster->canUndo()) raster->undo();
//   if (raster->canRedo()) raster->redo();

// Clear the canvas:
//   raster->clear();

// ── Custom surface example ────────────────────────────────────────────────

// Subclass RenderSurface to build your own renderer:
class MyParticleSurface : public RenderSurface {
public:
    void initialize(int w, int h) override { ... }
    void resize    (int w, int h) override { ... }
    void update    (double dt)    override { simulateParticles(dt); }
    void render    ()             override { drawParticles(); SwapBuffers(glDC_); }
    void onMouseDown(int x, int y) override { spawnBurst(x, y); }
    void onMouseMove(int x, int y) override { attractTo(x, y); }
    void onMouseUp  (int x, int y) override {}
    void onKeyDown  (int key)      override { if (key == 'R') reset(); }
    void onKeyUp    (int key)      override {}
    void destroy()                 override { freeGPUResources(); }
};

auto canvas2 = Canvas(1024, 768);
canvas2->setSurface<MyParticleSurface>();

*/