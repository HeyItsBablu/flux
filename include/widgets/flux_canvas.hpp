#ifndef FLUX_CANVAS_HPP
#define FLUX_CANVAS_HPP

// ============================================================================
// flux_canvas.hpp  —  CanvasWidget + pluggable RenderSurface architecture
// ============================================================================
//
// Window hierarchy
// ────────────────
//   parentHwnd  (FluxUI root)
//     └─ frameHwnd_   "FluxGLFrame"    WS_CHILD | WS_HSCROLL | WS_VSCROLL
//                                       owns scrollbars, sized to widget
//           └─ glHwnd_   "FluxGLCanvas4"   WS_CHILD | CS_OWNDC
//                                           pure GL surface, no scrollbars
//                                           sized to frame interior
//
//   Scrollbar messages (WM_HSCROLL / WM_VSCROLL) go to frameHwnd_.
//   Mouse / keyboard messages go to glHwnd_ and are forwarded up.
//   The GL context lives on glHwnd_'s DC.
//
// Changes in this revision
// ────────────────────────
//  [V1-V5]  See previous revision history (viewport, input routing, etc.)
//  [V6]  Scrollbars moved from glHwnd_ to frameHwnd_ so they sit outside
//        the OpenGL drawable area.  glHwnd_ is sized to the frame interior
//        (frame minus scrollbar tracks) and has no WS_HSCROLL/WS_VSCROLL.
//
// ============================================================================

#include "flux_viewport.hpp"
#include "flux_widget.hpp"

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>

#include "flux_gl_types.hpp"
#include <gl/GL.h>
#pragma comment(lib, "opengl32.lib")

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <deque>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace Gdiplus;

// ============================================================================
// §1  GL FUNCTION POINTER TYPEDEFS
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
using PFNGLBLITFRAMEBUFFERPROC         = void(APIENTRY *)(GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum);
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
using PFNGLGETPROGRAMIVPROC            = void(APIENTRY *)(GLuint, GLenum, GLint *);
using PFNGLGETPROGRAMINFOLOGPROC       = void(APIENTRY *)(GLuint, GLsizei, GLsizei *, GLchar *);
using PFNGLUSEPROGRAMPROC              = void(APIENTRY *)(GLuint);
using PFNGLGETUNIFORMLOCATIONPROC      = GLint(APIENTRY *)(GLuint, const GLchar *);
using PFNGLUNIFORM1IPROC               = void(APIENTRY *)(GLint, GLint);
using PFNGLUNIFORM1FPROC               = void(APIENTRY *)(GLint, GLfloat);
using PFNGLUNIFORM4FPROC               = void(APIENTRY *)(GLint, GLfloat, GLfloat, GLfloat, GLfloat);
using PFNGLUNIFORMMATRIX4FVPROC        = void(APIENTRY *)(GLint, GLsizei, GLboolean, const GLfloat *);
using PFNGLENABLEVERTEXATTRIBARRAYPROC = void(APIENTRY *)(GLuint);
using PFNGLDISABLEVERTEXATTRIBARRAYPROC= void(APIENTRY *)(GLuint);
using PFNGLVERTEXATTRIBPOINTERPROC     = void(APIENTRY *)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void *);

#define GL_ARRAY_BUFFER                  0x8892
#define GL_STATIC_DRAW                   0x88B4
#define GL_DYNAMIC_DRAW                  0x88E8
#define GL_VERTEX_SHADER                 0x8B31
#define GL_FRAGMENT_SHADER               0x8B30
#define GL_COMPILE_STATUS                0x8B81
#define GL_LINK_STATUS                   0x8B82
#define GL_CLAMP_TO_EDGE                 0x812F
#define GL_FRAMEBUFFER                   0x8D40
#define GL_READ_FRAMEBUFFER              0x8CA8
#define GL_DRAW_FRAMEBUFFER              0x8CA9
#define GL_FRAMEBUFFER_COMPLETE          0x8CD5
#define GL_COLOR_ATTACHMENT0             0x8CE0
#define WGL_CONTEXT_MAJOR_VERSION_ARB    0x2091
#define WGL_CONTEXT_MINOR_VERSION_ARB    0x2092
#define WGL_CONTEXT_PROFILE_MASK_ARB     0x9126
#define WGL_CONTEXT_CORE_PROFILE_BIT_ARB 0x00000001

// ============================================================================
// §1b  DEBUG GL ERROR CHECK MACRO
// ============================================================================

#ifndef NDEBUG
namespace glcheck_detail {
inline void check(const char *file, int line) {
    GLenum err;
    while ((err = glGetError()) != GL_NO_ERROR) {
        char buf[128];
        (void)_snprintf_s(buf, sizeof(buf), _TRUNCATE,
            "glGetError() = 0x%04X at %s:%d\n", (unsigned)err, file, line);
        OutputDebugStringA(buf);
        assert(false && "OpenGL error — see Output window");
    }
}
} // namespace glcheck_detail
#define GL_CHECK() glcheck_detail::check(__FILE__, __LINE__)
#define GL_CHECK_CALL(expr) do { (expr); GL_CHECK(); } while(0)
#else
#define GL_CHECK()          do {} while(0)
#define GL_CHECK_CALL(expr) do { (expr); } while(0)
#endif

// ============================================================================
// §2  GL PROC TABLE
// ============================================================================

static void *safeGetProc(HMODULE gl32, const char *name) {
    void *p = reinterpret_cast<void *>(wglGetProcAddress(name));
    if (!p || p == reinterpret_cast<void *>(1) ||
              p == reinterpret_cast<void *>(2) ||
              p == reinterpret_cast<void *>(3) ||
              p == reinterpret_cast<void *>(-1))
        p = reinterpret_cast<void *>(GetProcAddress(gl32, name));
    return p;
}

struct GLProcs {
    PFNGLGENBUFFERSPROC              genBuffers{};
    PFNGLDELETEBUFFERSPROC           deleteBuffers{};
    PFNGLBINDBUFFERPROC              bindBuffer{};
    PFNGLBUFFERDATAPROC              bufferData{};
    PFNGLBUFFERSUBDATAPROC           bufferSubData{};
    PFNGLGENVERTEXARRAYSPROC         genVertexArrays{};
    PFNGLDELETEVERTEXARRAYSPROC      deleteVertexArrays{};
    PFNGLBINDVERTEXARRAYPROC         bindVertexArray{};
    PFNGLGENFRAMEBUFFERSPROC         genFramebuffers{};
    PFNGLDELETEFRAMEBUFFERSPROC      deleteFramebuffers{};
    PFNGLBINDFRAMEBUFFERPROC         bindFramebuffer{};
    PFNGLFRAMEBUFFERTEXTURE2DPROC    framebufferTexture2D{};
    PFNGLCHECKFRAMEBUFFERSTATUSPROC  checkFramebufferStatus{};
    PFNGLBLITFRAMEBUFFERPROC         blitFramebuffer{};
    PFNGLCREATESHADERPROC            createShader{};
    PFNGLDELETESHADERPROC            deleteShader{};
    PFNGLSHADERSOURCEPROC            shaderSource{};
    PFNGLCOMPILESHADERPROC           compileShader{};
    PFNGLGETSHADERIVPROC             getShaderiv{};
    PFNGLGETSHADERINFOLOGPROC        getShaderInfoLog{};
    PFNGLCREATEPROGRAMPROC           createProgram{};
    PFNGLDELETEPROGRAMPROC           deleteProgram{};
    PFNGLATTACHSHADERPROC            attachShader{};
    PFNGLLINKPROGRAMPROC             linkProgram{};
    PFNGLGETPROGRAMIVPROC            getProgramiv{};
    PFNGLGETPROGRAMINFOLOGPROC       getProgramInfoLog{};
    PFNGLUSEPROGRAMPROC              useProgram{};
    PFNGLGETUNIFORMLOCATIONPROC      getUniformLocation{};
    PFNGLUNIFORM1IPROC               uniform1i{};
    PFNGLUNIFORM1FPROC               uniform1f{};
    PFNGLUNIFORM4FPROC               uniform4f{};
    PFNGLUNIFORMMATRIX4FVPROC        uniformMatrix4fv{};
    PFNGLENABLEVERTEXATTRIBARRAYPROC  enableVertexAttribArray{};
    PFNGLDISABLEVERTEXATTRIBARRAYPROC disableVertexAttribArray{};
    PFNGLVERTEXATTRIBPOINTERPROC     vertexAttribPointer{};

    static GLProcs &get() { static GLProcs p; return p; }

    void init() {
        HMODULE gl32 = GetModuleHandleA("opengl32.dll");
#define LOAD(fn, name) fn = reinterpret_cast<decltype(fn)>(safeGetProc(gl32, name))
        LOAD(genBuffers,              "glGenBuffers");
        LOAD(deleteBuffers,           "glDeleteBuffers");
        LOAD(bindBuffer,              "glBindBuffer");
        LOAD(bufferData,              "glBufferData");
        LOAD(bufferSubData,           "glBufferSubData");
        LOAD(genVertexArrays,         "glGenVertexArrays");
        LOAD(deleteVertexArrays,      "glDeleteVertexArrays");
        LOAD(bindVertexArray,         "glBindVertexArray");
        LOAD(genFramebuffers,         "glGenFramebuffers");
        LOAD(deleteFramebuffers,      "glDeleteFramebuffers");
        LOAD(bindFramebuffer,         "glBindFramebuffer");
        LOAD(framebufferTexture2D,    "glFramebufferTexture2D");
        LOAD(checkFramebufferStatus,  "glCheckFramebufferStatus");
        LOAD(blitFramebuffer,         "glBlitFramebuffer");
        LOAD(createShader,            "glCreateShader");
        LOAD(deleteShader,            "glDeleteShader");
        LOAD(shaderSource,            "glShaderSource");
        LOAD(compileShader,           "glCompileShader");
        LOAD(getShaderiv,             "glGetShaderiv");
        LOAD(getShaderInfoLog,        "glGetShaderInfoLog");
        LOAD(createProgram,           "glCreateProgram");
        LOAD(deleteProgram,           "glDeleteProgram");
        LOAD(attachShader,            "glAttachShader");
        LOAD(linkProgram,             "glLinkProgram");
        LOAD(getProgramiv,            "glGetProgramiv");
        LOAD(getProgramInfoLog,       "glGetProgramInfoLog");
        LOAD(useProgram,              "glUseProgram");
        LOAD(getUniformLocation,      "glGetUniformLocation");
        LOAD(uniform1i,               "glUniform1i");
        LOAD(uniform1f,               "glUniform1f");
        LOAD(uniform4f,               "glUniform4f");
        LOAD(uniformMatrix4fv,        "glUniformMatrix4fv");
        LOAD(enableVertexAttribArray, "glEnableVertexAttribArray");
        LOAD(disableVertexAttribArray,"glDisableVertexAttribArray");
        LOAD(vertexAttribPointer,     "glVertexAttribPointer");
#undef LOAD

        assert(genBuffers             && "GL: glGenBuffers not resolved");
        assert(deleteBuffers          && "GL: glDeleteBuffers not resolved");
        assert(bindBuffer             && "GL: glBindBuffer not resolved");
        assert(bufferData             && "GL: glBufferData not resolved");
        assert(genVertexArrays        && "GL: glGenVertexArrays not resolved");
        assert(deleteVertexArrays     && "GL: glDeleteVertexArrays not resolved");
        assert(bindVertexArray        && "GL: glBindVertexArray not resolved");
        assert(genFramebuffers        && "GL: glGenFramebuffers not resolved");
        assert(deleteFramebuffers     && "GL: glDeleteFramebuffers not resolved");
        assert(bindFramebuffer        && "GL: glBindFramebuffer not resolved");
        assert(framebufferTexture2D   && "GL: glFramebufferTexture2D not resolved");
        assert(checkFramebufferStatus && "GL: glCheckFramebufferStatus not resolved");
        assert(blitFramebuffer        && "GL: glBlitFramebuffer not resolved");
        assert(createShader           && "GL: glCreateShader not resolved");
        assert(deleteShader           && "GL: glDeleteShader not resolved");
        assert(shaderSource           && "GL: glShaderSource not resolved");
        assert(compileShader          && "GL: glCompileShader not resolved");
        assert(getShaderiv            && "GL: glGetShaderiv not resolved");
        assert(getShaderInfoLog       && "GL: glGetShaderInfoLog not resolved");
        assert(createProgram          && "GL: glCreateProgram not resolved");
        assert(deleteProgram          && "GL: glDeleteProgram not resolved");
        assert(attachShader           && "GL: glAttachShader not resolved");
        assert(linkProgram            && "GL: glLinkProgram not resolved");
        assert(getProgramiv           && "GL: glGetProgramiv not resolved");
        assert(getProgramInfoLog      && "GL: glGetProgramInfoLog not resolved");
        assert(useProgram             && "GL: glUseProgram not resolved");
        assert(getUniformLocation     && "GL: glGetUniformLocation not resolved");
        assert(uniform1i              && "GL: glUniform1i not resolved");
        assert(uniform1f              && "GL: glUniform1f not resolved");
        assert(uniform4f              && "GL: glUniform4f not resolved");
        assert(uniformMatrix4fv       && "GL: glUniformMatrix4fv not resolved");
        assert(enableVertexAttribArray&& "GL: glEnableVertexAttribArray not resolved");
        assert(vertexAttribPointer    && "GL: glVertexAttribPointer not resolved");
    }

private:
    GLProcs() = default;
};

static inline GLProcs &GL = GLProcs::get();

// ============================================================================
// §3  GL UTILITIES
// ============================================================================

namespace glutil {

inline GLuint compileShader(GLenum type, const char *src) {
    GLuint s = GL.createShader(type);
    GL.shaderSource(s, 1, &src, nullptr);
    GL.compileShader(s);
    GLint ok = 0;
    GL.getShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char buf[1024]; GLsizei len = 0;
        GL.getShaderInfoLog(s, 1024, &len, buf);
        OutputDebugStringA("Shader compile error: ");
        OutputDebugStringA(buf);
        GL.deleteShader(s);
        return 0;
    }
    return s;
}

inline GLuint linkProgram(const char *vert, const char *frag) {
    GLuint vs = compileShader(GL_VERTEX_SHADER,   vert);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, frag);
    if (!vs || !fs) {
        if (vs) GL.deleteShader(vs);
        if (fs) GL.deleteShader(fs);
        return 0;
    }
    GLuint p = GL.createProgram();
    GL.attachShader(p, vs);
    GL.attachShader(p, fs);
    GL.linkProgram(p);
    GL.deleteShader(vs);
    GL.deleteShader(fs);
    GLint ok = 0;
    GL.getProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char buf[1024]; GLsizei len = 0;
        GL.getProgramInfoLog(p, 1024, &len, buf);
        OutputDebugStringA("Program link error: ");
        OutputDebugStringA(buf);
        GL.deleteProgram(p);
        return 0;
    }
    return p;
}

inline void ortho(float l, float r, float b, float t, float out[16]) {
    float rml = r - l, tmb = t - b;
    out[0]  =  2/rml; out[1]  = 0;      out[2]  = 0;  out[3]  = 0;
    out[4]  =  0;     out[5]  = 2/tmb;  out[6]  = 0;  out[7]  = 0;
    out[8]  =  0;     out[9]  = 0;      out[10] = -1; out[11] = 0;
    out[12] = -(r+l)/rml;
    out[13] = -(t+b)/tmb;
    out[14] = 0; out[15] = 1;
}

inline void ortho(float w, float h, float out[16]) {
    ortho(0, w, h, 0, out);
}

} // namespace glutil

// ============================================================================
// §4  COLOR HELPERS
// ============================================================================

struct RGBA { float r, g, b, a; };

inline RGBA parseHexColor(const std::string &css) {
    auto h2f = [](unsigned v) { return v / 255.f; };
    try {
        if (!css.empty() && css[0] == '#') {
            std::string h = css.substr(1);
            if (h.size() == 3) {
                auto d = [&](int i) -> unsigned {
                    char c = h[i];
                    return c>='0'&&c<='9' ? c-'0' : c>='a'&&c<='f' ? c-'a'+10 : c-'A'+10;
                };
                unsigned r=d(0),g=d(1),b=d(2);
                return {h2f(r|(r<<4)), h2f(g|(g<<4)), h2f(b|(b<<4)), 1.f};
            }
            if (h.size()==6) {
                unsigned n=(unsigned)std::stoul(h,nullptr,16);
                return {h2f((n>>16)&0xFF), h2f((n>>8)&0xFF), h2f(n&0xFF), 1.f};
            }
            if (h.size()==8) {
                unsigned n=(unsigned)std::stoul(h,nullptr,16);
                return {h2f((n>>24)&0xFF),h2f((n>>16)&0xFF),h2f((n>>8)&0xFF),h2f(n&0xFF)};
            }
        }
        if (css.size()>=3 && css.substr(0,3)=="rgb") {
            auto a=css.find('('), b=css.find(')');
            if (a!=std::string::npos && b!=std::string::npos && b>a) {
                std::string inner=css.substr(a+1,b-a-1);
                float vals[4]={0,0,0,1}; int i=0; size_t pos=0;
                while(i<4){
                    size_t comma=inner.find(',',pos);
                    std::string tok=inner.substr(pos, comma==std::string::npos?std::string::npos:comma-pos);
                    vals[i++]=std::stof(tok);
                    if(comma==std::string::npos) break;
                    pos=comma+1;
                }
                return {vals[0]/255.f, vals[1]/255.f, vals[2]/255.f, vals[3]};
            }
        }
    } catch(const std::exception&) {}

    static const struct { const char *name; RGBA col; } kNamed[] = {
        {"black",{0,0,0,1}},{"white",{1,1,1,1}},{"red",{1,0,0,1}},
        {"green",{0,.5f,0,1}},{"blue",{0,0,1,1}},{"yellow",{1,1,0,1}},
        {"cyan",{0,1,1,1}},{"magenta",{1,0,1,1}},{"orange",{1,.647f,0,1}},
        {"purple",{.5f,0,.5f,1}},{"gray",{.5f,.5f,.5f,1}},{"grey",{.5f,.5f,.5f,1}},
        {"transparent",{0,0,0,0}},
    };
    std::string lo=css;
    for(auto &c:lo) c=(char)std::tolower((unsigned char)c);
    for(auto &e:kNamed) if(lo==e.name) return e.col;
    return {1,1,1,1};
}

// ============================================================================
// §5  STROKE DATA TYPES
// ============================================================================

struct StrokePoint { float x, y, pressure; };
struct StrokeStyle {
    float r=0,g=0,b=0,a=1;
    float radius=4, hardness=0.8f, opacity=1;
};
struct Stroke { StrokeStyle style; std::vector<StrokePoint> points; };

// ============================================================================
// §6  RENDER SURFACE  —  pure abstract interface
// ============================================================================

class RenderSurface {
public:
    virtual ~RenderSurface() = default;

    virtual void initialize(int w, int h)       = 0;
    virtual void resize(int w, int h)            = 0;
    virtual void update(double dt)               = 0;
    virtual void render(const float mvp[16])     = 0;

    virtual void onMouseDown(float x, float y)   {}
    virtual void onMouseMove(float x, float y)   {}
    virtual void onMouseUp  (float x, float y)   {}
    virtual void onKeyDown  (int key)             {}
    virtual void onKeyUp    (int key)             {}
    virtual void destroy()                        = 0;
};

// ============================================================================
// §7  RASTER SURFACE
// ============================================================================

class RasterSurface : public RenderSurface {
public:
    static constexpr size_t kDefaultUndoBudgetBytes = 256ULL * 1024 * 1024;

    explicit RasterSurface(size_t undoBudgetBytes = kDefaultUndoBudgetBytes)
        : undoBudgetBytes_(undoBudgetBytes) {}
    ~RasterSurface() { destroy(); }

    void setStrokeStyle(const StrokeStyle &s) { style_ = s; }
    const StrokeStyle &getStrokeStyle() const  { return style_; }

    void undo() {
        if (undoStack_.empty()) return;
        size_t sz = snapshotBytes();
        redoStack_.push_back({snapshotCommitted(), sz});
        redoBytes_ += sz;
        Snapshot snap = undoStack_.back();
        undoStack_.pop_back();
        undoBytes_ -= snap.bytes;
        restoreCommitted(snap.tex);
        glDeleteTextures(1, &snap.tex);
        dirty_ = true;
    }

    void redo() {
        if (redoStack_.empty()) return;
        size_t sz = snapshotBytes();
        undoStack_.push_back({snapshotCommitted(), sz});
        undoBytes_ += sz;
        Snapshot snap = redoStack_.back();
        redoStack_.pop_back();
        redoBytes_ -= snap.bytes;
        restoreCommitted(snap.tex);
        glDeleteTextures(1, &snap.tex);
        dirty_ = true;
    }

    bool canUndo() const { return !undoStack_.empty(); }
    bool canRedo() const { return !redoStack_.empty(); }

    void clear() {
        pushUndoSnapshot();
        clearFBO(committedFBO_, w_, h_, 255, 255, 255, 255);
        clearFBO(scratchFBO_,   w_, h_,   0,   0,   0,   0);
        dirty_ = true;
    }

    void initialize(int w, int h) override {
        w_ = w; h_ = h;
        buildShaders();
        buildQuadVAO();
        buildDabVerts();
        allocFBOPair(committedFBO_, committedTex_, w_, h_);
        allocFBOPair(scratchFBO_,   scratchTex_,   w_, h_);
        clearFBO(committedFBO_, w_, h_, 255, 255, 255, 255);
        clearFBO(scratchFBO_,   w_, h_,   0,   0,   0,   0);
    }

    void resize(int w, int h) override {
        if (w == w_ && h == h_) return;

        GLuint newCFBO=0, newCTex=0, newSFBO=0, newSTex=0;
        allocFBOPair(newCFBO, newCTex, w, h);
        allocFBOPair(newSFBO, newSTex, w, h);
        clearFBO(newCFBO, w, h, 255, 255, 255, 255);
        clearFBO(newSFBO, w, h,   0,   0,   0,   0);

        int copyW = min(w_, w), copyH = min(h_, h);
        GL.bindFramebuffer(GL_READ_FRAMEBUFFER, committedFBO_);
        GL.bindFramebuffer(GL_DRAW_FRAMEBUFFER, newCFBO);
        GL.blitFramebuffer(0,0,copyW,copyH, 0,0,copyW,copyH, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        GL.bindFramebuffer(GL_READ_FRAMEBUFFER, 0);
        GL.bindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);

        destroyFBOPair(committedFBO_, committedTex_);
        destroyFBOPair(scratchFBO_,   scratchTex_);
        committedFBO_ = newCFBO; committedTex_ = newCTex;
        scratchFBO_   = newSFBO; scratchTex_   = newSTex;

        flushSnapshotDeque(undoStack_, undoBytes_);
        flushSnapshotDeque(redoStack_, redoBytes_);

        w_ = w; h_ = h;
        dirty_ = true;
    }

    void update(double /*dt*/) override {}

    void render(const float mvp[16]) override {
        GLboolean blendWas = glIsEnabled(GL_BLEND);

        glDisable(GL_SCISSOR_TEST);
        glClearColor(0.18f, 0.18f, 0.20f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        blitTexture(committedTex_, 1.f, mvp);
        blitTexture(scratchTex_,   1.f, mvp);
        glDisable(GL_BLEND);

        dirty_ = false;

        GL.useProgram(0);
        if (blendWas) glEnable(GL_BLEND);
    }

    void onMouseDown(float x, float y) override { beginStroke(x, y); }
    void onMouseMove(float x, float y) override { if (drawing_) drawSegment(x, y); }
    void onMouseUp  (float x, float y) override { if (drawing_) endStroke(x, y); }

    void onKeyDown(int key) override {
        bool ctrl  = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        bool shift = (GetKeyState(VK_SHIFT)   & 0x8000) != 0;
        if (ctrl && key == 'Z') { if (shift) redo(); else undo(); }
        if (ctrl && key == 'Y') redo();
    }
    void onKeyUp(int) override {}

    void destroy() override {
        destroyFBOPair(committedFBO_, committedTex_);
        destroyFBOPair(scratchFBO_,   scratchTex_);
        if (blitProg_)  { GL.deleteProgram(blitProg_);        blitProg_ = 0; }
        if (quadVAO_)   { GL.deleteVertexArrays(1, &quadVAO_); quadVAO_ = 0; }
        if (quadVBO_)   { GL.deleteBuffers(1, &quadVBO_);      quadVBO_ = 0; }
        flushSnapshotDeque(undoStack_, undoBytes_);
        flushSnapshotDeque(redoStack_, redoBytes_);
    }

protected:
    int    canvasWidth()  const { return w_; }
    int    canvasHeight() const { return h_; }
    GLuint committedFBOHandle() const { return committedFBO_; }
    GLuint committedTexHandle() const { return committedTex_; }
    void   pushUndoSnapshotPublic()   { pushUndoSnapshot(); }

private:
    int  w_ = 0, h_ = 0;
    bool drawing_ = false, dirty_ = false;
    StrokePoint lastPt_{};
    StrokeStyle style_{};

    GLuint committedFBO_=0, committedTex_=0;
    GLuint scratchFBO_=0,   scratchTex_=0;
    GLuint blitProg_=0, quadVAO_=0, quadVBO_=0;

    struct ULocs { GLint mvp=-1, mode=-1, tex=-1, alpha=-1, color=-1; } uniforms_;

    struct Snapshot { GLuint tex; size_t bytes; };
    size_t undoBudgetBytes_ = kDefaultUndoBudgetBytes;
    size_t undoBytes_ = 0, redoBytes_ = 0;
    std::deque<Snapshot> undoStack_, redoStack_;

    size_t snapshotBytes() const { return (size_t)w_ * h_ * 4; }

    static constexpr int kDabVerts = 32;
    float dabVerts[(kDabVerts + 2) * 2]{};

    static void flushSnapshotDeque(std::deque<Snapshot> &dq, size_t &counter) {
        for (const Snapshot &s : dq) glDeleteTextures(1, &s.tex);
        dq.clear(); counter = 0;
    }

    void beginStroke(float x, float y) {
        if (drawing_) return;
        drawing_ = true;
        lastPt_  = {x, y, 1.f};
        flushSnapshotDeque(redoStack_, redoBytes_);
        clearFBO(scratchFBO_, w_, h_, 0, 0, 0, 0);
        paintDab(scratchFBO_, x, y, style_);
        dirty_ = true;
    }

    void drawSegment(float x, float y) {
        float dx = x - lastPt_.x, dy = y - lastPt_.y;
        float dist = std::sqrt(dx*dx + dy*dy);
        if (dist < 0.0001f) return;
        float step  = max(1.f, style_.radius * 0.25f);
        int   steps = max(1, (int)(dist / step));
        for (int i = 1; i <= steps; ++i) {
            float t = (float)i / steps;
            paintDab(scratchFBO_, lastPt_.x + dx*t, lastPt_.y + dy*t, style_);
        }
        lastPt_ = {x, y, 1.f};
        dirty_ = true;
    }

    void endStroke(float x, float y) {
        drawSegment(x, y);
        pushUndoSnapshot();
        mergeScratchIntoCommitted();
        clearFBO(scratchFBO_, w_, h_, 0, 0, 0, 0);
        drawing_ = false;
        dirty_   = true;
    }

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
        GLenum status = GL.checkFramebufferStatus(GL_FRAMEBUFFER);
        assert(status == GL_FRAMEBUFFER_COMPLETE && "FBO incomplete");
        (void)status;
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

    GLuint snapshotCommitted() {
        GLuint snapTex;
        glGenTextures(1, &snapTex);
        glBindTexture(GL_TEXTURE_2D, snapTex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w_, h_, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glBindTexture(GL_TEXTURE_2D, 0);

        GLuint snapFBO;
        GL.genFramebuffers(1, &snapFBO);
        GL.bindFramebuffer(GL_DRAW_FRAMEBUFFER, snapFBO);
        GL.framebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, snapTex, 0);
        GL.bindFramebuffer(GL_READ_FRAMEBUFFER, committedFBO_);
        GL.blitFramebuffer(0,0,w_,h_, 0,0,w_,h_, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        GL.bindFramebuffer(GL_READ_FRAMEBUFFER, 0);
        GL.bindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        GL.deleteFramebuffers(1, &snapFBO);
        return snapTex;
    }

    void restoreCommitted(GLuint snapTex) {
        GLuint readFBO;
        GL.genFramebuffers(1, &readFBO);
        GL.bindFramebuffer(GL_READ_FRAMEBUFFER, readFBO);
        GL.framebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, snapTex, 0);
        GL.bindFramebuffer(GL_DRAW_FRAMEBUFFER, committedFBO_);
        GL.blitFramebuffer(0,0,w_,h_, 0,0,w_,h_, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        GL.bindFramebuffer(GL_READ_FRAMEBUFFER, 0);
        GL.bindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        GL.deleteFramebuffers(1, &readFBO);
    }

    void pushUndoSnapshot() {
        size_t sz = snapshotBytes();
        while (undoBudgetBytes_ > 0 && !undoStack_.empty() &&
               undoBytes_ + sz > undoBudgetBytes_) {
            Snapshot oldest = undoStack_.front();
            undoStack_.pop_front();
            glDeleteTextures(1, &oldest.tex);
            undoBytes_ -= oldest.bytes;
        }
        undoStack_.push_back({snapshotCommitted(), sz});
        undoBytes_ += sz;
    }

    float canvasOrtho_[16]{};

    void updateCanvasOrtho() {
        glutil::ortho(0, (float)w_, 0, (float)h_, canvasOrtho_);
    }

    void mergeScratchIntoCommitted() {
        updateCanvasOrtho();
        GL.bindFramebuffer(GL_FRAMEBUFFER, committedFBO_);
        glViewport(0, 0, w_, h_);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        blitTexture(scratchTex_, 1.f, canvasOrtho_);
        glDisable(GL_BLEND);
        GL.bindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    void paintDab(GLuint fbo, float cx, float cy, const StrokeStyle &s) {
        updateCanvasOrtho();
        GL.bindFramebuffer(GL_FRAMEBUFFER, fbo);
        glViewport(0, 0, w_, h_);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        GL.useProgram(blitProg_);
        GL.uniformMatrix4fv(uniforms_.mvp,   1, GL_FALSE, canvasOrtho_);
        GL.uniform1i(uniforms_.mode,  1);
        GL.uniform4f(uniforms_.color, s.r, s.g, s.b, s.a * s.opacity);

        float verts[(kDabVerts + 2) * 2];
        for (int i = 0; i < kDabVerts + 2; ++i) {
            verts[i*2+0] = cx + dabVerts[i*2+0] * s.radius;
            verts[i*2+1] = cy + dabVerts[i*2+1] * s.radius;
        }

        GL.bindVertexArray(quadVAO_);
        GL.bindBuffer(GL_ARRAY_BUFFER, quadVBO_);
        GL.bufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
        GL.enableVertexAttribArray(0);
        GL.vertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(float)*2, nullptr);
        glDrawArrays(GL_TRIANGLE_FAN, 0, kDabVerts + 2);
        GL.bindVertexArray(0);

        glDisable(GL_BLEND);
        GL.bindFramebuffer(GL_FRAMEBUFFER, 0);
        GL.useProgram(0);
    }

    void blitTexture(GLuint tex, float alpha, const float *mvp) {
        GL.useProgram(blitProg_);
        GL.uniformMatrix4fv(uniforms_.mvp, 1, GL_FALSE, mvp);
        GL.uniform1i(uniforms_.mode, 0);
        GL.uniform1i(uniforms_.tex,  0);
        GL.uniform1f(uniforms_.alpha, alpha);
        glBindTexture(GL_TEXTURE_2D, tex);

        float q[] = {
            0.f,      0.f,      0.f, 0.f,
            (float)w_,0.f,      1.f, 0.f,
            (float)w_,(float)h_,1.f, 1.f,
            (float)w_,(float)h_,1.f, 1.f,
            0.f,      (float)h_,0.f, 1.f,
            0.f,      0.f,      0.f, 0.f,
        };
        GL.bindVertexArray(quadVAO_);
        GL.bindBuffer(GL_ARRAY_BUFFER, quadVBO_);
        GL.bufferSubData(GL_ARRAY_BUFFER, 0, sizeof(q), q);
        GL.enableVertexAttribArray(0);
        GL.vertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(float)*4, (void*)0);
        GL.enableVertexAttribArray(1);
        GL.vertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(float)*4, (void*)(sizeof(float)*2));
        glDrawArrays(GL_TRIANGLES, 0, 6);
        GL.bindVertexArray(0);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    void buildShaders() {
        const char *vert = R"GLSL(
#version 330 core
layout(location=0) in vec2 aPos;
layout(location=1) in vec2 aUV;
uniform mat4 uMVP;
out vec2 vUV;
void main(){ vUV=aUV; gl_Position=uMVP*vec4(aPos,0.0,1.0); }
)GLSL";
        const char *frag = R"GLSL(
#version 330 core
in  vec2 vUV;
uniform sampler2D uTex;
uniform vec4      uColor;
uniform float     uAlpha;
uniform int       uMode;
out vec4 fragColor;
void main(){
    if(uMode==0){ vec4 c=texture(uTex,vUV); fragColor=vec4(c.rgb,c.a*uAlpha); }
    else         { fragColor=uColor; }
}
)GLSL";
        blitProg_ = glutil::linkProgram(vert, frag);
        assert(blitProg_ && "RasterSurface: shader failed");

        uniforms_.mvp   = GL.getUniformLocation(blitProg_, "uMVP");
        uniforms_.mode  = GL.getUniformLocation(blitProg_, "uMode");
        uniforms_.tex   = GL.getUniformLocation(blitProg_, "uTex");
        uniforms_.alpha = GL.getUniformLocation(blitProg_, "uAlpha");
        uniforms_.color = GL.getUniformLocation(blitProg_, "uColor");
    }

    void buildQuadVAO() {
        constexpr GLsizeiptr kBlitBytes = sizeof(float) * 6 * 4;
        constexpr GLsizeiptr kDabBytes  = sizeof(float) * (kDabVerts+2) * 2;
        constexpr GLsizeiptr kVBOBytes  = kBlitBytes > kDabBytes ? kBlitBytes : kDabBytes;
        GL.genVertexArrays(1, &quadVAO_);
        GL.genBuffers(1, &quadVBO_);
        GL.bindVertexArray(quadVAO_);
        GL.bindBuffer(GL_ARRAY_BUFFER, quadVBO_);
        GL.bufferData(GL_ARRAY_BUFFER, kVBOBytes, nullptr, GL_DYNAMIC_DRAW);
        GL.bindVertexArray(0);
    }

    void buildDabVerts() {
        dabVerts[0] = 0.f; dabVerts[1] = 0.f;
        for (int i = 0; i <= kDabVerts; ++i) {
            float a = (float)i / kDabVerts * 6.2831853f;
            dabVerts[2 + i*2 + 0] = cosf(a);
            dabVerts[2 + i*2 + 1] = sinf(a);
        }
    }
};

// ============================================================================
// §8  CANVAS WIDGET
// ============================================================================
//
// Window hierarchy  [V6]
// ──────────────────────
//   frameHwnd_  "FluxGLFrame"    WS_CHILD | WS_HSCROLL | WS_VSCROLL
//     └─ glHwnd_  "FluxGLCanvas4"  WS_CHILD | CS_OWNDC   (no scrollbars)
//
//   frameHwnd_ is positioned to the widget rect.
//   glHwnd_    fills frameHwnd_'s client area minus scrollbar tracks
//              (GetSystemMetrics SM_CXVSCROLL / SM_CYHSCROLL).
//
//   WM_HSCROLL / WM_VSCROLL go to frameHwnd_.
//   All mouse / keyboard events go to glHwnd_ and are forwarded up.

class CanvasWidget : public Widget {
public:
    explicit CanvasWidget() {
        autoWidth = autoHeight = false;
        width = 400; height = 300;
    }
    ~CanvasWidget() { destroyGL(); }

    template<typename SurfaceT, typename... Args>
    std::shared_ptr<SurfaceT> setSurface(Args &&...args) {
        auto s = std::make_shared<SurfaceT>(std::forward<Args>(args)...);
        pendingSurface_ = s;
        return s;
    }

    RenderSurface   *getSurface() const { return activeSurface_.get(); }
    const Viewport  &viewport()   const { return viewport_; }
    Viewport        &viewport()         { return viewport_; }

    std::shared_ptr<CanvasWidget> setSize(int w, int h) {
        width = w; height = h;
        autoWidth = autoHeight = false;
        markNeedsLayout();
        return ptr();
    }

    std::shared_ptr<CanvasWidget> setCanvasSize(int w, int h) {
        canvasW_ = w; canvasH_ = h;
        if (frameHwnd_) {
            viewport_.setCanvasSize(w, h);
            if (activeSurface_) {
                if (wglGetCurrentContext() != glRC_)
                    wglMakeCurrent(glDC_, glRC_);
                activeSurface_->resize(w, h);
            }
        }
        return ptr();
    }

    std::shared_ptr<CanvasWidget> redraw() { markNeedsPaint(); return ptr(); }

    void computeLayout(HDC, const BoxConstraints &c, FontCache &) override {
        width  = c.clampWidth (autoWidth  ? c.maxWidth  : width);
        height = c.clampHeight(autoHeight ? c.maxHeight : height);
        needsLayout = false;
    }

    void render(HDC hdc, FontCache &) override {
        ensureWindows(hdc);
        moveWindows();
        tickAndRender();
        needsPaint = false;
    }

    void onDetach() override {
        destroyGL();
        Widget::onDetach();
    }

    void markNeedsPaint() override {
        Widget::markNeedsPaint();
        if (glHwnd_ && !repaintPending_) {
            repaintPending_ = true;
            PostMessage(glHwnd_, WM_USER + 1, 0, 0);
        }
    }

private:
    std::shared_ptr<CanvasWidget> ptr() {
        return std::static_pointer_cast<CanvasWidget>(shared_from_this());
    }

    // ── State ─────────────────────────────────────────────────────────────────

    std::shared_ptr<RenderSurface> activeSurface_, pendingSurface_;
    Viewport viewport_;
    int canvasW_ = 512, canvasH_ = 512;

    using Clock = std::chrono::steady_clock;
    Clock::time_point lastTick_ = Clock::now();

    // [V6] Two windows: frame owns scrollbars, GL child is pure render surface
    HWND  frameHwnd_   = nullptr;   // outer frame: WS_HSCROLL | WS_VSCROLL
    HWND  glHwnd_      = nullptr;   // inner GL surface: CS_OWNDC, no scrollbars
    HDC   glDC_        = nullptr;
    HGLRC glRC_        = nullptr;
    HWND  parentHwnd_  = nullptr;

    bool repaintPending_ = false;
    bool trackingLeave_  = false;

    int lastGLW_ = 0, lastGLH_ = 0;  // last GL child size

    bool  panning_    = false;
    int   panStartSX_ = 0, panStartSY_  = 0;
    float panStartOX_ = 0, panStartOY_  = 0;

    // ── Scrollbar geometry helpers ────────────────────────────────────────────

    // Width of the vertical scrollbar track (pixels).
    static int sbW() { return GetSystemMetrics(SM_CXVSCROLL); }
    // Height of the horizontal scrollbar track (pixels).
    static int sbH() { return GetSystemMetrics(SM_CYHSCROLL); }

    // GL child size = frame size minus scrollbar tracks (when visible).
    void glChildSize(int frameW, int frameH, int &outW, int &outH) const {
        ScrollbarInfo h = viewport_.scrollbarH();
        ScrollbarInfo v = viewport_.scrollbarV();
        outW = frameW - (v.visible ? sbW() : 0);
        outH = frameH - (h.visible ? sbH() : 0);
        if (outW < 1) outW = 1;
        if (outH < 1) outH = 1;
    }

    // ── Scroll helpers ────────────────────────────────────────────────────────

    void applyHScrollPos(int pos, int maxPos) {
        if (maxPos <= 0) return;
        float canvasRange = viewport_.canvasW() - viewport_.viewW() / viewport_.zoom();
        viewport_.setOffset(
            (float)pos / maxPos * max(0.f, canvasRange),
            viewport_.offsetY());
        scheduleRepaint();
    }

    void applyVScrollPos(int pos, int maxPos) {
        if (maxPos <= 0) return;
        float canvasRange = viewport_.canvasH() - viewport_.viewH() / viewport_.zoom();
        float canvasY = (1.f - (float)pos / maxPos) * max(0.f, canvasRange);
        viewport_.setOffset(viewport_.offsetX(), canvasY);
        scheduleRepaint();
    }

    void syncScrollbars() {
        if (!frameHwnd_) return;

        ScrollbarInfo h = viewport_.scrollbarH();
        ScrollbarInfo v = viewport_.scrollbarV();

        ShowScrollBar(frameHwnd_, SB_HORZ, h.visible ? TRUE : FALSE);
        ShowScrollBar(frameHwnd_, SB_VERT, v.visible ? TRUE : FALSE);

        constexpr int kRange = 10000;

        if (h.visible) {
            SCROLLINFO si{}; si.cbSize = sizeof(si);
            si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
            si.nMin  = 0; si.nMax = kRange;
            si.nPage = (UINT)((h.thumbMax - h.thumbMin) * kRange);
            si.nPos  = (int)(h.thumbMin * kRange);
            SetScrollInfo(frameHwnd_, SB_HORZ, &si, TRUE);
        }
        if (v.visible) {
            SCROLLINFO si{}; si.cbSize = sizeof(si);
            si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
            si.nMin  = 0; si.nMax = kRange;
            si.nPage = (UINT)((v.thumbMax - v.thumbMin) * kRange);
            si.nPos  = (int)(v.thumbMin * kRange);
            SetScrollInfo(frameHwnd_, SB_VERT, &si, TRUE);
        }

        // Resize the GL child whenever scrollbar visibility may have changed
        RECT rc; GetClientRect(frameHwnd_, &rc);
        int gw, gh;
        glChildSize(rc.right, rc.bottom, gw, gh);
        SetWindowPos(glHwnd_, nullptr, 0, 0, gw, gh,
                     SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOMOVE);
    }

    void scheduleRepaint() {
        if (!repaintPending_) {
            repaintPending_ = true;
            PostMessage(glHwnd_, WM_USER + 1, 0, 0);
        }
    }

    // ── Frame window proc (owns scrollbars) ───────────────────────────────────

    static LRESULT CALLBACK FrameProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        CanvasWidget *self = reinterpret_cast<CanvasWidget *>(
            GetWindowLongPtrW(hwnd, GWLP_USERDATA));

        switch (msg) {
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps; BeginPaint(hwnd, &ps); EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_HSCROLL: {
            if (!self) return 0;
            SCROLLINFO si{}; si.cbSize = sizeof(si); si.fMask = SIF_ALL;
            GetScrollInfo(hwnd, SB_HORZ, &si);
            int newPos = si.nPos;
            switch (LOWORD(wp)) {
                case SB_LINELEFT:      newPos -= 200;          break;
                case SB_LINERIGHT:     newPos += 200;          break;
                case SB_PAGELEFT:      newPos -= si.nPage;     break;
                case SB_PAGERIGHT:     newPos += si.nPage;     break;
                case SB_THUMBTRACK:
                case SB_THUMBPOSITION: newPos  = si.nTrackPos; break;
                default: break;
            }
            newPos = std::clamp(newPos, si.nMin, si.nMax - (int)si.nPage);
            self->applyHScrollPos(newPos, si.nMax - (int)si.nPage);
            return 0;
        }

        case WM_VSCROLL: {
            if (!self) return 0;
            SCROLLINFO si{}; si.cbSize = sizeof(si); si.fMask = SIF_ALL;
            GetScrollInfo(hwnd, SB_VERT, &si);
            int newPos = si.nPos;
            switch (LOWORD(wp)) {
                case SB_LINEUP:        newPos -= 200;          break;
                case SB_LINEDOWN:      newPos += 200;          break;
                case SB_PAGEUP:        newPos -= si.nPage;     break;
                case SB_PAGEDOWN:      newPos += si.nPage;     break;
                case SB_THUMBTRACK:
                case SB_THUMBPOSITION: newPos  = si.nTrackPos; break;
                default: break;
            }
            newPos = std::clamp(newPos, si.nMin, si.nMax - (int)si.nPage);
            self->applyVScrollPos(newPos, si.nMax - (int)si.nPage);
            return 0;
        }

        // Forward mouse wheel from frame to GL child so it reaches the parent chain
        case WM_MOUSEWHEEL:
            if (self && self->glHwnd_)
                PostMessage(self->glHwnd_, msg, wp, lp);
            return 0;

        default:
            return DefWindowProc(hwnd, msg, wp, lp);
        }
    }

    // ── GL child window proc (pure render surface + input) ────────────────────

    static LRESULT CALLBACK GLProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        CanvasWidget *self = reinterpret_cast<CanvasWidget *>(
            GetWindowLongPtrW(hwnd, GWLP_USERDATA));

        switch (msg) {

        case WM_USER + 1:
            if (self) { self->repaintPending_ = false; self->tickAndRender(); }
            return 0;

        case WM_ERASEBKGND: return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps; BeginPaint(hwnd, &ps); EndPaint(hwnd, &ps);
            return 0;
        }

        // ── Mouse: pan (MMB / Space+LMB) ─────────────────────────────────────
        case WM_MBUTTONDOWN: {
            SetCapture(hwnd);
            if (self) {
                self->panning_    = true;
                self->panStartSX_ = GET_X_LPARAM(lp);
                self->panStartSY_ = GET_Y_LPARAM(lp);
                self->panStartOX_ = self->viewport_.offsetX();
                self->panStartOY_ = self->viewport_.offsetY();
            }
            return 0;
        }
        case WM_MBUTTONUP:
            ReleaseCapture();
            if (self) self->panning_ = false;
            return 0;

        // ── Mouse: LMB ───────────────────────────────────────────────────────
        case WM_LBUTTONDOWN: {
            SetCapture(hwnd);
            if (!self) return 0;
            bool space = (GetKeyState(VK_SPACE) & 0x8000) != 0;
            if (space) {
                self->panning_    = true;
                self->panStartSX_ = GET_X_LPARAM(lp);
                self->panStartSY_ = GET_Y_LPARAM(lp);
                self->panStartOX_ = self->viewport_.offsetX();
                self->panStartOY_ = self->viewport_.offsetY();
            } else if (self->activeSurface_) {
                auto [cx, cy] = self->viewport_.screenToCanvas(
                    (float)GET_X_LPARAM(lp), (float)GET_Y_LPARAM(lp));
                self->activeSurface_->onMouseDown(cx, cy);
            }
            forwardToParent(hwnd, msg, wp, lp);
            return 0;
        }
        case WM_LBUTTONUP: {
            ReleaseCapture();
            if (!self) return 0;
            if (self->panning_) {
                self->panning_ = false;
            } else if (self->activeSurface_) {
                auto [cx, cy] = self->viewport_.screenToCanvas(
                    (float)GET_X_LPARAM(lp), (float)GET_Y_LPARAM(lp));
                self->activeSurface_->onMouseUp(cx, cy);
            }
            forwardToParent(hwnd, msg, wp, lp);
            return 0;
        }
        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP: {
            SetCapture(hwnd);
            if (self && self->activeSurface_) {
                auto [cx, cy] = self->viewport_.screenToCanvas(
                    (float)GET_X_LPARAM(lp), (float)GET_Y_LPARAM(lp));
                if (msg == WM_RBUTTONDOWN)
                    self->activeSurface_->onMouseDown(cx, cy);
                else {
                    ReleaseCapture();
                    self->activeSurface_->onMouseUp(cx, cy);
                }
            }
            forwardToParent(hwnd, msg, wp, lp);
            return 0;
        }

        case WM_MOUSEMOVE: {
            if (!self) return 0;
            int sx = GET_X_LPARAM(lp), sy = GET_Y_LPARAM(lp);

            if (!self->trackingLeave_) {
                TRACKMOUSEEVENT tme{};
                tme.cbSize    = sizeof(tme);
                tme.dwFlags   = TME_LEAVE;
                tme.hwndTrack = hwnd;
                TrackMouseEvent(&tme);
                self->trackingLeave_ = true;
            }

            if (self->panning_) {
                float dx = (float)(sx - self->panStartSX_);
                float dy = (float)(sy - self->panStartSY_);
                self->viewport_.setOffset(
                    self->panStartOX_ - dx / self->viewport_.zoom(),
                    self->panStartOY_ + dy / self->viewport_.zoom());
                self->scheduleRepaint();
            } else if (self->activeSurface_) {
                auto [cx, cy] = self->viewport_.screenToCanvas((float)sx, (float)sy);
                self->activeSurface_->onMouseMove(cx, cy);
            }
            forwardToParent(hwnd, msg, wp, lp);
            return 0;
        }
        case WM_MOUSELEAVE:
            if (self) self->trackingLeave_ = false;
            return 0;

        case WM_MOUSEWHEEL:
            forwardToParent(hwnd, msg, wp, lp);
            return 0;

        // ── Keyboard ─────────────────────────────────────────────────────────
        case WM_KEYDOWN: {
            if (!self) return 0;
            bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            bool consumed = false;
            if (ctrl) {
                if (wp == VK_OEM_PLUS || wp == VK_ADD) {
                    self->viewport_.zoomIn();
                    self->scheduleRepaint();
                    consumed = true;
                } else if (wp == VK_OEM_MINUS || wp == VK_SUBTRACT) {
                    self->viewport_.zoomOut();
                    self->scheduleRepaint();
                    consumed = true;
                } else if (wp == '0') {
                    self->viewport_.resetZoom();
                    self->scheduleRepaint();
                    consumed = true;
                }
            }
            if (!consumed && self->activeSurface_)
                self->activeSurface_->onKeyDown((int)wp);
            return 0;
        }
        case WM_KEYUP:
            if (self && self->activeSurface_)
                self->activeSurface_->onKeyUp((int)wp);
            return 0;

        default:
            return DefWindowProc(hwnd, msg, wp, lp);
        }
    }

    static void forwardToParent(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        // Forward to the FluxUI root (grandparent of glHwnd_)
        HWND frame  = GetParent(hwnd);
        HWND parent = frame ? GetParent(frame) : nullptr;
        if (!parent) return;
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        MapWindowPoints(hwnd, parent, &pt, 1);
        PostMessage(parent, msg, wp, MAKELPARAM(pt.x, pt.y));
    }

    // ── Window class registration ─────────────────────────────────────────────

    static void registerClasses() {
        static bool done = false;
        if (done) return;
        HINSTANCE hi = GetModuleHandle(nullptr);

        // Frame class — plain, handles scrollbars
        WNDCLASSEXW wf{};
        wf.cbSize       = sizeof(wf);
        wf.lpfnWndProc  = FrameProc;
        wf.hInstance    = hi;
        wf.lpszClassName= L"FluxGLFrame";
        wf.style        = CS_HREDRAW | CS_VREDRAW;
        RegisterClassExW(&wf);

        // GL child class — CS_OWNDC, no scrollbars
        WNDCLASSEXW wg{};
        wg.cbSize       = sizeof(wg);
        wg.lpfnWndProc  = GLProc;
        wg.hInstance    = hi;
        wg.lpszClassName= L"FluxGLCanvas4";
        wg.style        = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
        RegisterClassExW(&wg);

        done = true;
    }

    // ── Window creation + GL init ─────────────────────────────────────────────

    void ensureWindows(HDC parentDC) {
        if (frameHwnd_) return;

        HWND owner = WindowFromDC(parentDC);
        if (!owner) owner = GetActiveWindow();
        parentHwnd_ = owner;
        registerClasses();

        // 1. Create the frame window (no GL, owns scrollbars)
        frameHwnd_ = CreateWindowExW(
            WS_EX_NOPARENTNOTIFY,
            L"FluxGLFrame", nullptr,
            WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN
            | WS_HSCROLL | WS_VSCROLL,
            x, y, width, height,
            owner, nullptr, GetModuleHandle(nullptr), nullptr);
        assert(frameHwnd_ && "CanvasWidget: failed to create frame window");
        SetWindowLongPtrW(frameHwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

        // 2. Create the GL child inside the frame, sized to interior
        int gw, gh;
        glChildSize(width, height, gw, gh);

        glHwnd_ = CreateWindowExW(
            WS_EX_NOPARENTNOTIFY,
            L"FluxGLCanvas4", nullptr,
            WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
            0, 0, gw, gh,
            frameHwnd_, nullptr, GetModuleHandle(nullptr), nullptr);
        assert(glHwnd_ && "CanvasWidget: failed to create GL window");
        SetWindowLongPtrW(glHwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

        // 3. Set up OpenGL on the GL child's DC
        glDC_ = GetDC(glHwnd_);
        setupPixelFormat(glDC_);

        HGLRC tmpRC = wglCreateContext(glDC_);
        wglMakeCurrent(glDC_, tmpRC);

        auto wglCreateCtxAttribs = reinterpret_cast<PFNWGLCREATECONTEXTATTRIBSARBPROC>(
            wglGetProcAddress("wglCreateContextAttribsARB"));
        if (wglCreateCtxAttribs) {
            const int attribs[] = {
                WGL_CONTEXT_MAJOR_VERSION_ARB, 3,
                WGL_CONTEXT_MINOR_VERSION_ARB, 3,
                WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
                0
            };
            HGLRC coreRC = wglCreateCtxAttribs(glDC_, nullptr, attribs);
            if (coreRC) {
                wglMakeCurrent(nullptr, nullptr);
                wglDeleteContext(tmpRC);
                glRC_ = coreRC;
            } else {
                glRC_ = tmpRC;
            }
        } else {
            glRC_ = tmpRC;
        }
        wglMakeCurrent(glDC_, glRC_);
        GL.init();

        viewport_.init(gw, gh, canvasW_, canvasH_);
        lastGLW_ = gw; lastGLH_ = gh;

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

    void activatePendingSurface() {
        if (!pendingSurface_) return;
        if (activeSurface_) { activeSurface_->destroy(); activeSurface_.reset(); }
        activeSurface_ = pendingSurface_;
        pendingSurface_.reset();
        activeSurface_->initialize(canvasW_, canvasH_);
        viewport_.setCanvasSize(canvasW_, canvasH_);
    }

    // ── Per-frame tick ────────────────────────────────────────────────────────

    void tickAndRender() {
        if (!glRC_ || !glDC_) return;
        if (pendingSurface_) activatePendingSurface();
        if (!activeSurface_) return;

        if (wglGetCurrentContext() != glRC_)
            wglMakeCurrent(glDC_, glRC_);

        auto now = Clock::now();
        double dt = std::chrono::duration<double>(now - lastTick_).count();
        lastTick_ = now;

        activeSurface_->update(dt);
        viewport_.update(dt);   // no-op without animation, kept for compat

        // Resize GL viewport to current GL child size
        RECT rc; GetClientRect(glHwnd_, &rc);
        int glW = rc.right, glH = rc.bottom;
        if (glW < 1) glW = 1;
        if (glH < 1) glH = 1;
        glViewport(0, 0, glW, glH);

        float mvp[16];
        viewport_.buildMatrix(mvp);
        activeSurface_->render(mvp);

        SwapBuffers(glDC_);
        syncScrollbars();
    }

    // ── Layout / resize ───────────────────────────────────────────────────────

    void moveWindows() {
        if (!frameHwnd_) return;

        // Reposition the frame to the widget rect
        SetWindowPos(frameHwnd_, nullptr, x, y, width, height,
                     SWP_NOZORDER | SWP_NOACTIVATE);

        // Recompute GL child size from current frame client area
        RECT rc; GetClientRect(frameHwnd_, &rc);
        int gw, gh;
        glChildSize(rc.right, rc.bottom, gw, gh);

        SetWindowPos(glHwnd_, nullptr, 0, 0, gw, gh,
                     SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOMOVE);

        if (gw != lastGLW_ || gh != lastGLH_) {
            if (wglGetCurrentContext() != glRC_)
                wglMakeCurrent(glDC_, glRC_);
            viewport_.setViewSize(gw, gh);
            glViewport(0, 0, gw, gh);
            lastGLW_ = gw; lastGLH_ = gh;
        }
    }

    // ── Cleanup ───────────────────────────────────────────────────────────────

    void destroyGL() {
        if (glRC_) {
            wglMakeCurrent(glDC_, glRC_);
            if (activeSurface_)  { activeSurface_->destroy();  activeSurface_.reset(); }
            if (pendingSurface_)   pendingSurface_.reset();
            wglMakeCurrent(nullptr, nullptr);
            wglDeleteContext(glRC_);
            glRC_ = nullptr;
        }
        if (glHwnd_ && glDC_) { ReleaseDC(glHwnd_, glDC_); glDC_ = nullptr; }
        if (glHwnd_)           { DestroyWindow(glHwnd_);    glHwnd_ = nullptr; }
        if (frameHwnd_)        { DestroyWindow(frameHwnd_); frameHwnd_ = nullptr; }
    }
};

// ============================================================================
// §9  FACTORY HELPERS
// ============================================================================

using CanvasPtr = std::shared_ptr<CanvasWidget>;

inline CanvasPtr Canvas() {
    return std::make_shared<CanvasWidget>();
}

inline CanvasPtr Canvas(int w, int h) {
    return std::make_shared<CanvasWidget>()->setSize(w, h);
}

inline CanvasPtr RasterCanvas(int w, int h) {
    auto c = std::make_shared<CanvasWidget>()->setSize(w, h);
    c->setCanvasSize(w, h);
    c->setSurface<RasterSurface>();
    return c;
}

inline CanvasPtr RasterCanvas(int w, int h, size_t undoBudgetBytes) {
    auto c = std::make_shared<CanvasWidget>()->setSize(w, h);
    c->setCanvasSize(w, h);
    c->setSurface<RasterSurface>(undoBudgetBytes);
    return c;
}

inline CanvasPtr RasterCanvas(int viewW, int viewH, int canvasW, int canvasH) {
    auto c = std::make_shared<CanvasWidget>()->setSize(viewW, viewH);
    c->setCanvasSize(canvasW, canvasH);
    c->setSurface<RasterSurface>();
    return c;
}

#endif // FLUX_CANVAS_HPP