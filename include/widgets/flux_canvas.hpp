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
//              │ owns shared_ptr<RenderSurface>
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
// Bugs fixed vs. the previous submitted version
// ──────────────────────────────────────────────
//  [A] ensureChildWindow — CRITICAL CRASH: the if/else for coreRC was inverted.
//      When wglCreateContextAttribsARB succeeded it assigned glRC_ = tmpRC
//      (the legacy context) and hit exit(1) via the MessageBox path.
//      Every modern GPU returned a valid coreRC, so the app always exited.
//      Fixed: success → delete tmpRC, assign coreRC.  Failure → keep tmpRC.
//
//  [B] beginStroke — GPU texture leak: redoStack_.clear() dropped GLuint
//      handles without calling glDeleteTextures.  Replaced with
//      flushTextureDeque() which deletes each texture before clearing.
//
//  [C] destroy() — same leak: undoStack_.clear() / redoStack_.clear() must
//      go through flushTextureDeque().
//
//  [D] resize() — snapshotCommitted() returned a GLuint that was stored in
//      a local variable and never used or deleted (immediate leak).
//      Replaced with a direct GPU→GPU FBO blit: allocate new FBOs, blit
//      the old committed pixels in, then destroy the old FBOs.
//
// ============================================================================

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
#include <string>
#include <vector>

using namespace Gdiplus;

// ============================================================================
// § 1  OPENGL 3.3 FUNCTION POINTER TYPEDEFS
// ============================================================================

using PFNWGLCREATECONTEXTATTRIBSARBPROC = HGLRC(WINAPI *)(HDC, HGLRC,
                                                          const int *);

using PFNGLGENBUFFERSPROC = void(APIENTRY *)(GLsizei, GLuint *);
using PFNGLDELETEBUFFERSPROC = void(APIENTRY *)(GLsizei, const GLuint *);
using PFNGLBINDBUFFERPROC = void(APIENTRY *)(GLenum, GLuint);
using PFNGLBUFFERDATAPROC = void(APIENTRY *)(GLenum, GLsizeiptr, const void *,
                                             GLenum);
using PFNGLBUFFERSUBDATAPROC = void(APIENTRY *)(GLenum, GLintptr, GLsizeiptr,
                                                const void *);
using PFNGLGENVERTEXARRAYSPROC = void(APIENTRY *)(GLsizei, GLuint *);
using PFNGLDELETEVERTEXARRAYSPROC = void(APIENTRY *)(GLsizei, const GLuint *);
using PFNGLBINDVERTEXARRAYPROC = void(APIENTRY *)(GLuint);
using PFNGLGENFRAMEBUFFERSPROC = void(APIENTRY *)(GLsizei, GLuint *);
using PFNGLDELETEFRAMEBUFFERSPROC = void(APIENTRY *)(GLsizei, const GLuint *);
using PFNGLBINDFRAMEBUFFERPROC = void(APIENTRY *)(GLenum, GLuint);
using PFNGLFRAMEBUFFERTEXTURE2DPROC = void(APIENTRY *)(GLenum, GLenum, GLenum,
                                                       GLuint, GLint);
using PFNGLCHECKFRAMEBUFFERSTATUSPROC = GLenum(APIENTRY *)(GLenum);
using PFNGLBLITFRAMEBUFFERPROC = void(APIENTRY *)(GLint, GLint, GLint, GLint,
                                                  GLint, GLint, GLint, GLint,
                                                  GLbitfield, GLenum);
using PFNGLCREATESHADERPROC = GLuint(APIENTRY *)(GLenum);
using PFNGLDELETESHADERPROC = void(APIENTRY *)(GLuint);
using PFNGLSHADERSOURCEPROC = void(APIENTRY *)(GLuint, GLsizei,
                                               const GLchar *const *,
                                               const GLint *);
using PFNGLCOMPILESHADERPROC = void(APIENTRY *)(GLuint);
using PFNGLGETSHADERIVPROC = void(APIENTRY *)(GLuint, GLenum, GLint *);
using PFNGLGETSHADERINFOLOGPROC = void(APIENTRY *)(GLuint, GLsizei, GLsizei *,
                                                   GLchar *);
using PFNGLCREATEPROGRAMPROC = GLuint(APIENTRY *)();
using PFNGLDELETEPROGRAMPROC = void(APIENTRY *)(GLuint);
using PFNGLATTACHSHADERPROC = void(APIENTRY *)(GLuint, GLuint);
using PFNGLLINKPROGRAMPROC = void(APIENTRY *)(GLuint);
using PFNGLUSEPROGRAMPROC = void(APIENTRY *)(GLuint);
using PFNGLGETUNIFORMLOCATIONPROC = GLint(APIENTRY *)(GLuint, const GLchar *);
using PFNGLUNIFORM1IPROC = void(APIENTRY *)(GLint, GLint);
using PFNGLUNIFORM1FPROC = void(APIENTRY *)(GLint, GLfloat);
using PFNGLUNIFORM4FPROC = void(APIENTRY *)(GLint, GLfloat, GLfloat, GLfloat,
                                            GLfloat);
using PFNGLUNIFORMMATRIX4FVPROC = void(APIENTRY *)(GLint, GLsizei, GLboolean,
                                                   const GLfloat *);
using PFNGLENABLEVERTEXATTRIBARRAYPROC = void(APIENTRY *)(GLuint);
using PFNGLDISABLEVERTEXATTRIBARRAYPROC = void(APIENTRY *)(GLuint);
using PFNGLVERTEXATTRIBPOINTERPROC = void(APIENTRY *)(GLuint, GLint, GLenum,
                                                      GLboolean, GLsizei,
                                                      const void *);

// GL constants absent from the frozen Windows gl.h (GL 1.1)
#define GL_ARRAY_BUFFER 0x8892
#define GL_STATIC_DRAW 0x88B4
#define GL_DYNAMIC_DRAW 0x88E8
#define GL_VERTEX_SHADER 0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_FRAMEBUFFER 0x8D40
#define GL_READ_FRAMEBUFFER 0x8CA8
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define WGL_CONTEXT_MAJOR_VERSION_ARB 0x2091
#define WGL_CONTEXT_MINOR_VERSION_ARB 0x2092
#define WGL_CONTEXT_PROFILE_MASK_ARB 0x9126
#define WGL_CONTEXT_CORE_PROFILE_BIT_ARB 0x00000001

// ============================================================================
// § 2  GL PROC TABLE
// ============================================================================

struct GLProcs {
  PFNGLGENBUFFERSPROC genBuffers{};
  PFNGLDELETEBUFFERSPROC deleteBuffers{};
  PFNGLBINDBUFFERPROC bindBuffer{};
  PFNGLBUFFERDATAPROC bufferData{};
  PFNGLBUFFERSUBDATAPROC bufferSubData{};
  PFNGLGENVERTEXARRAYSPROC genVertexArrays{};
  PFNGLDELETEVERTEXARRAYSPROC deleteVertexArrays{};
  PFNGLBINDVERTEXARRAYPROC bindVertexArray{};
  PFNGLGENFRAMEBUFFERSPROC genFramebuffers{};
  PFNGLDELETEFRAMEBUFFERSPROC deleteFramebuffers{};
  PFNGLBINDFRAMEBUFFERPROC bindFramebuffer{};
  PFNGLFRAMEBUFFERTEXTURE2DPROC framebufferTexture2D{};
  PFNGLCHECKFRAMEBUFFERSTATUSPROC checkFramebufferStatus{};
  PFNGLBLITFRAMEBUFFERPROC blitFramebuffer{};
  PFNGLCREATESHADERPROC createShader{};
  PFNGLDELETESHADERPROC deleteShader{};
  PFNGLSHADERSOURCEPROC shaderSource{};
  PFNGLCOMPILESHADERPROC compileShader{};
  PFNGLGETSHADERIVPROC getShaderiv{};
  PFNGLGETSHADERINFOLOGPROC getShaderInfoLog{};
  PFNGLCREATEPROGRAMPROC createProgram{};
  PFNGLDELETEPROGRAMPROC deleteProgram{};
  PFNGLATTACHSHADERPROC attachShader{};
  PFNGLLINKPROGRAMPROC linkProgram{};
  PFNGLUSEPROGRAMPROC useProgram{};
  PFNGLGETUNIFORMLOCATIONPROC getUniformLocation{};
  PFNGLUNIFORM1IPROC uniform1i{};
  PFNGLUNIFORM1FPROC uniform1f{};
  PFNGLUNIFORM4FPROC uniform4f{};
  PFNGLUNIFORMMATRIX4FVPROC uniformMatrix4fv{};
  PFNGLENABLEVERTEXATTRIBARRAYPROC enableVertexAttribArray{};
  PFNGLDISABLEVERTEXATTRIBARRAYPROC disableVertexAttribArray{};
  PFNGLVERTEXATTRIBPOINTERPROC vertexAttribPointer{};

  static GLProcs &get() {
    static GLProcs p;
    return p;
  }

  void init() {
#define LOAD(fn, type, name)                                                   \
  fn = reinterpret_cast<type>(wglGetProcAddress(name))
    LOAD(genBuffers, PFNGLGENBUFFERSPROC, "glGenBuffers");
    LOAD(deleteBuffers, PFNGLDELETEBUFFERSPROC, "glDeleteBuffers");
    LOAD(bindBuffer, PFNGLBINDBUFFERPROC, "glBindBuffer");
    LOAD(bufferData, PFNGLBUFFERDATAPROC, "glBufferData");
    LOAD(bufferSubData, PFNGLBUFFERSUBDATAPROC, "glBufferSubData");
    LOAD(genVertexArrays, PFNGLGENVERTEXARRAYSPROC, "glGenVertexArrays");
    LOAD(deleteVertexArrays, PFNGLDELETEVERTEXARRAYSPROC,
         "glDeleteVertexArrays");
    LOAD(bindVertexArray, PFNGLBINDVERTEXARRAYPROC, "glBindVertexArray");
    LOAD(genFramebuffers, PFNGLGENFRAMEBUFFERSPROC, "glGenFramebuffers");
    LOAD(deleteFramebuffers, PFNGLDELETEFRAMEBUFFERSPROC,
         "glDeleteFramebuffers");
    LOAD(bindFramebuffer, PFNGLBINDFRAMEBUFFERPROC, "glBindFramebuffer");
    LOAD(framebufferTexture2D, PFNGLFRAMEBUFFERTEXTURE2DPROC,
         "glFramebufferTexture2D");
    LOAD(checkFramebufferStatus, PFNGLCHECKFRAMEBUFFERSTATUSPROC,
         "glCheckFramebufferStatus");
    LOAD(blitFramebuffer, PFNGLBLITFRAMEBUFFERPROC, "glBlitFramebuffer");
    LOAD(createShader, PFNGLCREATESHADERPROC, "glCreateShader");
    LOAD(deleteShader, PFNGLDELETESHADERPROC, "glDeleteShader");
    LOAD(shaderSource, PFNGLSHADERSOURCEPROC, "glShaderSource");
    LOAD(compileShader, PFNGLCOMPILESHADERPROC, "glCompileShader");
    LOAD(getShaderiv, PFNGLGETSHADERIVPROC, "glGetShaderiv");
    LOAD(getShaderInfoLog, PFNGLGETSHADERINFOLOGPROC, "glGetShaderInfoLog");
    LOAD(createProgram, PFNGLCREATEPROGRAMPROC, "glCreateProgram");
    LOAD(deleteProgram, PFNGLDELETEPROGRAMPROC, "glDeleteProgram");
    LOAD(attachShader, PFNGLATTACHSHADERPROC, "glAttachShader");
    LOAD(linkProgram, PFNGLLINKPROGRAMPROC, "glLinkProgram");
    LOAD(useProgram, PFNGLUSEPROGRAMPROC, "glUseProgram");
    LOAD(getUniformLocation, PFNGLGETUNIFORMLOCATIONPROC,
         "glGetUniformLocation");
    LOAD(uniform1i, PFNGLUNIFORM1IPROC, "glUniform1i");
    LOAD(uniform1f, PFNGLUNIFORM1FPROC, "glUniform1f");
    LOAD(uniform4f, PFNGLUNIFORM4FPROC, "glUniform4f");
    LOAD(uniformMatrix4fv, PFNGLUNIFORMMATRIX4FVPROC, "glUniformMatrix4fv");
    LOAD(enableVertexAttribArray, PFNGLENABLEVERTEXATTRIBARRAYPROC,
         "glEnableVertexAttribArray");
    LOAD(disableVertexAttribArray, PFNGLDISABLEVERTEXATTRIBARRAYPROC,
         "glDisableVertexAttribArray");
    LOAD(vertexAttribPointer, PFNGLVERTEXATTRIBPOINTERPROC,
         "glVertexAttribPointer");
#undef LOAD
  }

private:
  GLProcs() = default;
};

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
    char buf[1024];
    GLsizei len;
    GL.getShaderInfoLog(s, 1024, &len, buf);
    OutputDebugStringA(buf);
  }
  return s;
}

inline GLuint linkProgram(const char *vert, const char *frag) {
  GLuint vs = compileShader(GL_VERTEX_SHADER, vert);
  GLuint fs = compileShader(GL_FRAGMENT_SHADER, frag);
  GLuint p = GL.createProgram();
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
  out[0] = 2 * r;
  out[1] = 0;
  out[2] = 0;
  out[3] = 0;
  out[4] = 0;
  out[5] = -2 * t;
  out[6] = 0;
  out[7] = 0;
  out[8] = 0;
  out[9] = 0;
  out[10] = -1;
  out[11] = 0;
  out[12] = -1;
  out[13] = 1;
  out[14] = 0;
  out[15] = 1;
}

} // namespace glutil

// ============================================================================
// § 4  COLOR HELPERS
// ============================================================================

struct RGBA {
  float r, g, b, a;
};

inline RGBA parseHexColor(const std::string &css) {
  auto h2f = [](unsigned v) { return v / 255.f; };
  if (!css.empty() && css[0] == '#') {
    std::string h = css.substr(1);
    if (h.size() == 3) {
      auto d = [&](int i) -> unsigned {
        char c = h[i];
        return c >= '0' && c <= '9'   ? c - '0'
               : c >= 'a' && c <= 'f' ? c - 'a' + 10
                                      : c - 'A' + 10;
      };
      unsigned r = d(0), g = d(1), b = d(2);
      return {h2f(r | (r << 4)), h2f(g | (g << 4)), h2f(b | (b << 4)), 1.f};
    }
    if (h.size() == 6) {
      unsigned n = (unsigned)std::stoul(h, nullptr, 16);
      return {h2f((n >> 16) & 0xFF), h2f((n >> 8) & 0xFF), h2f(n & 0xFF), 1.f};
    }
    if (h.size() == 8) {
      unsigned n = (unsigned)std::stoul(h, nullptr, 16);
      return {h2f((n >> 24) & 0xFF), h2f((n >> 16) & 0xFF),
              h2f((n >> 8) & 0xFF), h2f(n & 0xFF)};
    }
  }
  if (css.size() >= 3 && css.substr(0, 3) == "rgb") {
    auto a = css.find('('), b = css.find(')');
    if (a != std::string::npos) {
      std::string inner = css.substr(a + 1, b - a - 1);
      float vals[4] = {0, 0, 0, 1};
      int i = 0;
      size_t pos = 0;
      while (i < 4) {
        size_t comma = inner.find(',', pos);
        std::string tok = inner.substr(
            pos, comma == std::string::npos ? std::string::npos : comma - pos);
        vals[i++] = std::stof(tok);
        if (comma == std::string::npos)
          break;
        pos = comma + 1;
      }
      return {vals[0] / 255.f, vals[1] / 255.f, vals[2] / 255.f, vals[3]};
    }
  }
  static const struct {
    const char *name;
    RGBA col;
  } kNamed[] = {
      {"black", {0, 0, 0, 1}},       {"white", {1, 1, 1, 1}},
      {"red", {1, 0, 0, 1}},         {"green", {0, .5f, 0, 1}},
      {"blue", {0, 0, 1, 1}},        {"yellow", {1, 1, 0, 1}},
      {"cyan", {0, 1, 1, 1}},        {"magenta", {1, 0, 1, 1}},
      {"orange", {1, .647f, 0, 1}},  {"purple", {.5f, 0, .5f, 1}},
      {"gray", {.5f, .5f, .5f, 1}},  {"grey", {.5f, .5f, .5f, 1}},
      {"transparent", {0, 0, 0, 0}},
  };
  std::string lo = css;
  for (auto &c : lo)
    c = (char)std::tolower((unsigned char)c);
  for (auto &e : kNamed)
    if (lo == e.name)
      return e.col;
  return {1, 1, 1, 1};
}

// ============================================================================
// § 5  STROKE DATA TYPES
// ============================================================================

struct StrokePoint {
  float x, y, pressure;
};

struct StrokeStyle {
  float r = 0.f, g = 0.f, b = 0.f, a = 1.f;
  float radius = 4.f, hardness = 0.8f, opacity = 1.f;
};

struct Stroke {
  StrokeStyle style;
  std::vector<StrokePoint> points;
};

// ============================================================================
// § 6  RENDER SURFACE  —  pure abstract interface
// ============================================================================

class RenderSurface {
public:
  virtual ~RenderSurface() = default;
  virtual void initialize(int w, int h) = 0;
  virtual void resize(int w, int h) = 0;
  virtual void update(double dt) = 0;
  virtual void render() = 0;
  virtual void onMouseDown(int x, int y) {}
  virtual void onMouseMove(int x, int y) {}
  virtual void onMouseUp(int x, int y) {}
  virtual void onKeyDown(int key) {}
  virtual void onKeyUp(int key) {}
  virtual void destroy() = 0;
};

// ============================================================================
// § 7  RASTER SURFACE
// ============================================================================

class RasterSurface : public RenderSurface {
public:
  RasterSurface() = default;
  ~RasterSurface() { destroy(); }

  void setStrokeStyle(const StrokeStyle &s) { style_ = s; }
  const StrokeStyle &getStrokeStyle() const { return style_; }

  // ── Undo / redo ──────────────────────────────────────────────────────────

  void undo() {
    if (undoStack_.empty())
      return;
    redoStack_.push_back(snapshotCommitted());
    GLuint snap = undoStack_.back();
    undoStack_.pop_back();
    restoreCommitted(snap);
    glDeleteTextures(1, &snap);
    dirty_ = true;
  }

  void redo() {
    if (redoStack_.empty())
      return;
    undoStack_.push_back(snapshotCommitted());
    GLuint snap = redoStack_.back();
    redoStack_.pop_back();
    restoreCommitted(snap);
    glDeleteTextures(1, &snap);
    dirty_ = true;
  }

  bool canUndo() const { return !undoStack_.empty(); }
  bool canRedo() const { return !redoStack_.empty(); }

  void clear() {
    pushUndoSnapshot();
    clearFBO(committedFBO_, w_, h_, 255, 255, 255, 255);
    clearFBO(scratchFBO_, w_, h_, 0, 0, 0, 0);
    dirty_ = true;
  }

  // ── RenderSurface interface ──────────────────────────────────────────────

  void initialize(int w, int h) override {
    w_ = w;
    h_ = h;
    buildShaders();
    buildQuadVAO();
    buildDabVerts();
    allocFBOPair(committedFBO_, committedTex_, w_, h_);
    allocFBOPair(scratchFBO_, scratchTex_, w_, h_);
    clearFBO(committedFBO_, w_, h_, 255, 255, 255, 255);
    clearFBO(scratchFBO_, w_, h_, 0, 0, 0, 0);
    glutil::ortho((float)w_, (float)h_, proj_);
  }

  void resize(int w, int h) override {
    if (w == w_ && h == h_)
      return;

    // Fix [D]: GPU→GPU blit — allocate new FBOs, blit old into them,
    // then destroy the old ones.  No CPU readback, no snapshot leak.
    GLuint newCFBO = 0, newCTex = 0, newSFBO = 0, newSTex = 0;
    allocFBOPair(newCFBO, newCTex, w, h);
    allocFBOPair(newSFBO, newSTex, w, h);
    clearFBO(newCFBO, w, h, 255, 255, 255, 255);
    clearFBO(newSFBO, w, h, 0, 0, 0, 0);

    int copyW = min(w_, w), copyH = min(h_, h);
    GL.bindFramebuffer(GL_READ_FRAMEBUFFER, committedFBO_);
    GL.bindFramebuffer(GL_DRAW_FRAMEBUFFER, newCFBO);
    GL.blitFramebuffer(0, 0, copyW, copyH, 0, 0, copyW, copyH,
                       GL_COLOR_BUFFER_BIT, GL_NEAREST);
    GL.bindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    GL.bindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);

    destroyFBOPair(committedFBO_, committedTex_);
    destroyFBOPair(scratchFBO_, scratchTex_);
    committedFBO_ = newCFBO;
    committedTex_ = newCTex;
    scratchFBO_ = newSFBO;
    scratchTex_ = newSTex;
    w_ = w;
    h_ = h;

    glutil::ortho((float)w_, (float)h_, proj_);
    glViewport(0, 0, w_, h_);
    dirty_ = true;
  }

  void update(double /*dt*/) override {}

  void render() override {
    glViewport(0, 0, w_, h_);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(1, 1, 1, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    blitTexture(committedTex_, 1.f);
    blitTexture(scratchTex_, 1.f); // transparent when idle — no cost
    glDisable(GL_BLEND);
    dirty_ = false;
  }

  void onMouseDown(int x, int y) override { beginStroke(x, y); }
  void onMouseMove(int x, int y) override {
    if (drawing_)
      drawSegment(x, y);
  }
  void onMouseUp(int x, int y) override {
    if (drawing_)
      endStroke(x, y);
  }

  void onKeyDown(int key) override {
    bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    if (ctrl && key == 'Z') {
      if (shift)
        redo();
      else
        undo();
    }
    if (ctrl && key == 'Y')
      redo();
  }
  void onKeyUp(int /*key*/) override {}

  void destroy() override {
    destroyFBOPair(committedFBO_, committedTex_);
    destroyFBOPair(scratchFBO_, scratchTex_);
    if (blitProg_) {
      GL.deleteProgram(blitProg_);
      blitProg_ = 0;
    }
    if (quadVAO_) {
      GL.deleteVertexArrays(1, &quadVAO_);
      quadVAO_ = 0;
    }
    if (quadVBO_) {
      GL.deleteBuffers(1, &quadVBO_);
      quadVBO_ = 0;
    }
    // Fix [C]: delete GPU textures before clearing — bare .clear() leaks
    flushTextureDeque(undoStack_);
    flushTextureDeque(redoStack_);
  }

private:
  int w_ = 0, h_ = 0;
  bool drawing_ = false, dirty_ = false;
  StrokePoint lastPt_{};
  StrokeStyle style_{};

  GLuint committedFBO_ = 0, committedTex_ = 0;
  GLuint scratchFBO_ = 0, scratchTex_ = 0;
  GLuint blitProg_ = 0, quadVAO_ = 0, quadVBO_ = 0;
  float proj_[16]{};

  static constexpr size_t kMaxUndoDepth = 64;
  std::deque<GLuint> undoStack_; // each entry: one snapshot texture
  std::deque<GLuint> redoStack_;

  static constexpr int kDabVerts = 32;
  float dabVerts[(kDabVerts + 2) * 2]; // precomputed unit circle

  // ── Safe deque flush — always use this instead of bare .clear() ──────────
  static void flushTextureDeque(std::deque<GLuint> &dq) {
    for (GLuint t : dq)
      glDeleteTextures(1, &t);
    dq.clear();
  }

  // ── Stroke ───────────────────────────────────────────────────────────────

  void beginStroke(int x, int y) {
    if (drawing_)
      return;
    drawing_ = true;
    lastPt_ = {(float)x, (float)y, 1.f};
    // Fix [B]: must delete GPU textures — not bare .clear()
    flushTextureDeque(redoStack_);
    clearFBO(scratchFBO_, w_, h_, 0, 0, 0, 0);
    paintDab(scratchFBO_, (float)x, (float)y, style_); // dab at click point
    dirty_ = true;
  }

  void drawSegment(int x, int y) {
    float dx = (float)x - lastPt_.x, dy = (float)y - lastPt_.y;
    float dist = std::sqrt(dx * dx + dy * dy);
    if (dist < 0.0001f)
      return;

    float step = max(1.f, style_.radius * 0.25f);
    int steps = max(1, (int)(dist / step));
    for (int i = 1; i <= steps; ++i) {
      float t = (float)i / steps;
      paintDab(scratchFBO_, lastPt_.x + dx * t, lastPt_.y + dy * t, style_);
    }
    lastPt_ = {(float)x, (float)y, 1.f};
    dirty_ = true;
  }

  void endStroke(int x, int y) {
    drawSegment(x, y);
    pushUndoSnapshot();
    mergeScratchIntoCommitted();
    clearFBO(scratchFBO_, w_, h_, 0, 0, 0, 0);
    drawing_ = false;
    dirty_ = true;
  }

  // ── FBO helpers ──────────────────────────────────────────────────────────

  void allocFBOPair(GLuint &fbo, GLuint &tex, int w, int h) {
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 nullptr);
    glBindTexture(GL_TEXTURE_2D, 0);
    GL.genFramebuffers(1, &fbo);
    GL.bindFramebuffer(GL_FRAMEBUFFER, fbo);
    GL.framebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                            tex, 0);
    GL.bindFramebuffer(GL_FRAMEBUFFER, 0);
  }

  void destroyFBOPair(GLuint &fbo, GLuint &tex) {
    if (fbo) {
      GL.deleteFramebuffers(1, &fbo);
      fbo = 0;
    }
    if (tex) {
      glDeleteTextures(1, &tex);
      tex = 0;
    }
  }

  void clearFBO(GLuint fbo, int w, int h, uint8_t r, uint8_t g, uint8_t b,
                uint8_t a) {
    GL.bindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, w, h);
    glClearColor(r / 255.f, g / 255.f, b / 255.f, a / 255.f);
    glClear(GL_COLOR_BUFFER_BIT);
    GL.bindFramebuffer(GL_FRAMEBUFFER, 0);
  }

  // ── GPU snapshot / restore ───────────────────────────────────────────────
  // Stores only a texture; a temporary FBO is created per operation and
  // deleted immediately — no persistent per-snapshot FBO allocation.

  GLuint snapshotCommitted() {
    GLuint snapTex;
    glGenTextures(1, &snapTex);
    glBindTexture(GL_TEXTURE_2D, snapTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w_, h_, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, nullptr);
    glBindTexture(GL_TEXTURE_2D, 0);

    GLuint snapFBO;
    GL.genFramebuffers(1, &snapFBO);
    GL.bindFramebuffer(GL_DRAW_FRAMEBUFFER, snapFBO);
    GL.framebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                            GL_TEXTURE_2D, snapTex, 0);
    GL.bindFramebuffer(GL_READ_FRAMEBUFFER, committedFBO_);
    GL.blitFramebuffer(0, 0, w_, h_, 0, 0, w_, h_, GL_COLOR_BUFFER_BIT,
                       GL_NEAREST);
    GL.bindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    GL.bindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    GL.deleteFramebuffers(1, &snapFBO);
    return snapTex;
  }

  void restoreCommitted(GLuint snapTex) {
    GLuint readFBO;
    GL.genFramebuffers(1, &readFBO);
    GL.bindFramebuffer(GL_READ_FRAMEBUFFER, readFBO);
    GL.framebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                            GL_TEXTURE_2D, snapTex, 0);
    GL.bindFramebuffer(GL_DRAW_FRAMEBUFFER, committedFBO_);
    GL.blitFramebuffer(0, 0, w_, h_, 0, 0, w_, h_, GL_COLOR_BUFFER_BIT,
                       GL_NEAREST);
    GL.bindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    GL.bindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    GL.deleteFramebuffers(1, &readFBO);
  }

  void pushUndoSnapshot() {
    if (undoStack_.size() >= kMaxUndoDepth) {
      GLuint oldest = undoStack_.front();
      undoStack_.pop_front();
      glDeleteTextures(1, &oldest);
    }
    undoStack_.push_back(snapshotCommitted());
  }

  // ── Merge + dab + blit ───────────────────────────────────────────────────

  void mergeScratchIntoCommitted() {
    GL.bindFramebuffer(GL_FRAMEBUFFER, committedFBO_);
    glViewport(0, 0, w_, h_);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    blitTexture(scratchTex_, 1.f);
    glDisable(GL_BLEND);
    GL.bindFramebuffer(GL_FRAMEBUFFER, 0);
  }

  void paintDab(GLuint fbo, float cx, float cy, const StrokeStyle &s) {
    GL.bindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, w_, h_);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    GL.useProgram(blitProg_);
    GL.uniformMatrix4fv(GL.getUniformLocation(blitProg_, "uMVP"), 1, GL_FALSE,
                        proj_);
    GL.uniform1i(GL.getUniformLocation(blitProg_, "uMode"), 1);
    GL.uniform4f(GL.getUniformLocation(blitProg_, "uColor"), s.r, s.g, s.b,
                 s.a * s.opacity);

    // Transform precomputed circle → dab position & radius
    float verts[(kDabVerts + 2) * 2];
    for (int i = 0; i < kDabVerts + 2; ++i) {
      verts[i * 2 + 0] = cx + dabVerts[i * 2 + 0] * s.radius;
      verts[i * 2 + 1] = cy + dabVerts[i * 2 + 1] * s.radius;
    }

    GL.bindVertexArray(quadVAO_);
    GL.bindBuffer(GL_ARRAY_BUFFER, quadVBO_);
    GL.bufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_DYNAMIC_DRAW);
    GL.enableVertexAttribArray(0);
    GL.vertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 2,
                           nullptr);
    glDrawArrays(GL_TRIANGLE_FAN, 0, kDabVerts + 2);
    GL.bindVertexArray(0);

    glDisable(GL_BLEND);
    GL.bindFramebuffer(GL_FRAMEBUFFER, 0);
  }

  void blitTexture(GLuint tex, float alpha) {
    GL.useProgram(blitProg_);
    GL.uniformMatrix4fv(GL.getUniformLocation(blitProg_, "uMVP"), 1, GL_FALSE,
                        proj_);
    GL.uniform1i(GL.getUniformLocation(blitProg_, "uMode"), 0);
    GL.uniform1i(GL.getUniformLocation(blitProg_, "uTex"), 0);
    GL.uniform1f(GL.getUniformLocation(blitProg_, "uAlpha"), alpha);
    glBindTexture(GL_TEXTURE_2D, tex);

    float q[] = {
        0.f,       (float)0,  0.f, 1.f, (float)w_, 0.f,       1.f, 1.f,
        (float)w_, (float)h_, 1.f, 0.f, (float)w_, (float)h_, 1.f, 0.f,
        0.f,       (float)h_, 0.f, 0.f, 0.f,       0.f,       0.f, 1.f,
    };
    GL.bindVertexArray(quadVAO_);
    GL.bindBuffer(GL_ARRAY_BUFFER, quadVBO_);
    GL.bufferData(GL_ARRAY_BUFFER, sizeof(q), q, GL_DYNAMIC_DRAW);
    GL.enableVertexAttribArray(0);
    GL.vertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 4,
                           (void *)0);
    GL.enableVertexAttribArray(1);
    GL.vertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 4,
                           (void *)(sizeof(float) * 2));
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
  }

  void buildQuadVAO() {
    GL.genVertexArrays(1, &quadVAO_);
    GL.genBuffers(1, &quadVBO_);
    GL.bindVertexArray(quadVAO_);
    GL.bindBuffer(GL_ARRAY_BUFFER, quadVBO_);
    GL.bufferData(GL_ARRAY_BUFFER, sizeof(float) * 6 * 4, nullptr,
                  GL_DYNAMIC_DRAW);
    GL.bindVertexArray(0);
  }
  void buildDabVerts() {
    dabVerts[0] = 0.f; // center x
    dabVerts[1] = 0.f; // center y
    for (int i = 0; i <= kDabVerts; ++i) {
      float a = (float)i / kDabVerts * 6.2831853f;
      dabVerts[2 + i * 2 + 0] = cosf(a);
      dabVerts[2 + i * 2 + 1] = sinf(a);
    }
  }
};

// ============================================================================
// § 8  CANVAS WIDGET
// ============================================================================

class CanvasWidget : public Widget {
public:
  explicit CanvasWidget() {
    autoWidth = autoHeight = false;
    width = 400;
    height = 300;
  }
  ~CanvasWidget() { destroyGL(); }

  template <typename SurfaceT, typename... Args>
  std::shared_ptr<SurfaceT> setSurface(Args &&...args) {
    auto s = std::make_shared<SurfaceT>(std::forward<Args>(args)...);
    pendingSurface_ = s;
    return s;
  }

  RenderSurface *getSurface() const { return activeSurface_.get(); }

  std::shared_ptr<CanvasWidget> setSize(int w, int h) {
    width = w;
    height = h;
    autoWidth = autoHeight = false;
    markNeedsLayout();
    return ptr();
  }
  std::shared_ptr<CanvasWidget> redraw() {
    markNeedsPaint();
    return ptr();
  }

  void computeLayout(HDC, const BoxConstraints &c, FontCache &) override {
    width = c.clampWidth(autoWidth ? c.maxWidth : width);
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

  std::shared_ptr<RenderSurface> activeSurface_, pendingSurface_;

  using Clock = std::chrono::steady_clock;
  Clock::time_point lastTick_ = Clock::now();

  HWND childHwnd = nullptr;
  HDC glDC_ = nullptr;
  HGLRC glRC_ = nullptr;
  HWND parentHwnd = nullptr;
  bool repaintPending_ = false, mouseInside_ = false, trackingLeave_ = false;
  int lastW_ = 0, lastH_ = 0;

  static LRESULT CALLBACK ChildProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    CanvasWidget *self = reinterpret_cast<CanvasWidget *>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
    case WM_USER + 1:
      if (self) {
        self->repaintPending_ = false;
        self->tickAndRender();
      }
      return 0;
    case WM_ERASEBKGND:
      return 1;
    case WM_PAINT: {
      PAINTSTRUCT ps;
      BeginPaint(hwnd, &ps);
      EndPaint(hwnd, &ps);
      return 0;
    }
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
          tme.cbSize = sizeof(tme);
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
        self->mouseInside_ = false;
      }
      return 0;
    case WM_MOUSEWHEEL:
      forwardToParent(hwnd, msg, wp, lp);
      return 0;
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
    if (!parent)
      return;
    POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
    MapWindowPoints(hwnd, parent, &pt, 1);
    PostMessage(parent, msg, wp, MAKELPARAM(pt.x, pt.y));
  }

  static void registerChildClass() {
    static bool done = false;
    if (done)
      return;
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = ChildProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = L"FluxGLCanvas2";
    wc.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
    RegisterClassExW(&wc);
    done = true;
  }

  void ensureChildWindow(HDC parentDC) {
    if (childHwnd)
      return;
    HWND owner = WindowFromDC(parentDC);
    if (!owner)
      owner = GetActiveWindow();
    parentHwnd = owner;
    registerChildClass();

    childHwnd = CreateWindowExW(
        WS_EX_NOPARENTNOTIFY, L"FluxGLCanvas2", nullptr,
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN, x, y, width,
        height, owner, nullptr, GetModuleHandle(nullptr), nullptr);
    if (!childHwnd)
      return;
    SetWindowLongPtrW(childHwnd, GWLP_USERDATA,
                      reinterpret_cast<LONG_PTR>(this));

    glDC_ = GetDC(childHwnd);
    setupPixelFormat(glDC_);

    // Step 1 — legacy context (needed to resolve wglCreateContextAttribsARB)
    HGLRC tmpRC = wglCreateContext(glDC_);
    wglMakeCurrent(glDC_, tmpRC);

    // Step 2 — try to upgrade to Core 3.3
    auto wglCreateCtxAttribs =
        reinterpret_cast<PFNWGLCREATECONTEXTATTRIBSARBPROC>(
            wglGetProcAddress("wglCreateContextAttribsARB"));

    if (wglCreateCtxAttribs) {
      const int attribs[] = {WGL_CONTEXT_MAJOR_VERSION_ARB,
                             3,
                             WGL_CONTEXT_MINOR_VERSION_ARB,
                             3,
                             WGL_CONTEXT_PROFILE_MASK_ARB,
                             WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
                             0};
      HGLRC coreRC = wglCreateCtxAttribs(glDC_, nullptr, attribs);
      if (coreRC) {
        // Fix [A]: success → discard legacy, use core context
        wglMakeCurrent(nullptr, nullptr);
        wglDeleteContext(tmpRC);
        glRC_ = coreRC;
      } else {
        // Fallback: driver too old, keep legacy context
        glRC_ = tmpRC;
      }
    } else {
      glRC_ = tmpRC;
    }
    wglMakeCurrent(glDC_, glRC_);

    GL.init();
    activatePendingSurface();
    lastTick_ = Clock::now();
  }

  void setupPixelFormat(HDC dc) {
    PIXELFORMATDESCRIPTOR pfd{};
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    pfd.iLayerType = PFD_MAIN_PLANE;
    SetPixelFormat(dc, ChoosePixelFormat(dc, &pfd), &pfd);
  }

  void activatePendingSurface() {
    if (!pendingSurface_)
      return;
    if (activeSurface_) {
      activeSurface_->destroy();
      activeSurface_.reset();
    }
    activeSurface_ = pendingSurface_;
    pendingSurface_.reset();
    activeSurface_->initialize(width, height);
  }

  void tickAndRender() {
    if (!glRC_ || !glDC_)
      return;
    if (pendingSurface_)
      activatePendingSurface();
    if (!activeSurface_)
      return;
    if (wglGetCurrentContext() != glRC_)
      wglMakeCurrent(glDC_, glRC_);
    auto now = Clock::now();
    double dt = std::chrono::duration<double>(now - lastTick_).count();
    lastTick_ = now;
    activeSurface_->update(dt);
    activeSurface_->render();
    SwapBuffers(glDC_);
  }

  void moveChildWindow() {
    if (!childHwnd)
      return;
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

  void destroyGL() {
    if (glRC_) {
      wglMakeCurrent(glDC_, glRC_);
      if (activeSurface_) {
        activeSurface_->destroy();
        activeSurface_.reset();
      }
      if (pendingSurface_)
        pendingSurface_.reset();
      wglMakeCurrent(nullptr, nullptr);
      wglDeleteContext(glRC_);
      glRC_ = nullptr;
    }
    if (childHwnd && glDC_) {
      ReleaseDC(childHwnd, glDC_);
      glDC_ = nullptr;
    }
    if (childHwnd) {
      DestroyWindow(childHwnd);
      childHwnd = nullptr;
    }
  }
};

// ============================================================================
// § 9  FACTORY HELPERS
// ============================================================================

using CanvasPtr = std::shared_ptr<CanvasWidget>;

inline CanvasPtr Canvas() { return std::make_shared<CanvasWidget>(); }
inline CanvasPtr Canvas(int w, int h) {
  return std::make_shared<CanvasWidget>()->setSize(w, h);
}

inline CanvasPtr RasterCanvas(int w, int h) {
  auto c = std::make_shared<CanvasWidget>()->setSize(w, h);
  c->setSurface<RasterSurface>();
  return c;
}

#endif // FLUX_CANVAS_HPP