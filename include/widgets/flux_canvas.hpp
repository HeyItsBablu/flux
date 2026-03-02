#ifndef FLUX_CANVAS_HPP
#define FLUX_CANVAS_HPP

// ============================================================================
// flux_canvas.hpp  —  CanvasWidget + pluggable RenderSurface architecture
// ============================================================================
//
// Step 1  (previous)
// ──────────────────
//   Single child HWND, zoom/pan wired but no visual context for them:
//   no deadzone, no scrollbars.  Zoom/pan were intentionally disabled in the
//   test until the full viewport infrastructure was in place.
//
// Step 2  (previous)
// ───────────────────────
//   Two-window hierarchy restored.  Zoom, pan, deadzone, and scrollbars are
//   now all active together.
//
// Step 3  (previous)
// ───────────────────────
//   • Tool enum (Brush / Eraser) added to RasterSurface.
//   • Stroke opacity exposed.
//   • savePNG(path) via GDI+.
//   • Color history ring.
//
// Step 4  (this revision)
// ───────────────────────
//   BUG FIX: drawVertsToFBO used glBufferData (realloc) which could shrink
//   the shared quadVBO_ to fewer bytes than blitTexture's quad requires.
//   blitTexture uses glBufferSubData and assumes the buffer is large enough,
//   so a shrunken VBO caused a GL error / undefined render (white canvas
//   disappearing, wrong colors, stale geometry).
//
//   Fix: always use glBufferData in BOTH drawVertsToFBO AND blitTexture so
//   each call owns its allocation and there is no size assumption between
//   callers.  The VAO only stores the attrib format; actual data is uploaded
//   fresh every draw call, so this is safe and has negligible overhead.
//
// ============================================================================

#include "flux_widget.hpp"

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>

// GDI+ for PNG save
#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")

#include "flux_gl_types.hpp"
#include <gl/GL.h>
#pragma comment(lib, "opengl32.lib")

#include <algorithm>
#include <array>
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
// §1  OPENGL FUNCTION POINTER TYPEDEFS
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
using PFNGLGETPROGRAMIVPROC = void(APIENTRY *)(GLuint, GLenum, GLint *);
using PFNGLGETPROGRAMINFOLOGPROC = void(APIENTRY *)(GLuint, GLsizei, GLsizei *,
                                                    GLchar *);
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
using PFNGLBLENDFUNCSEPARATEPROC = void(APIENTRY *)(GLenum, GLenum, GLenum,
                                                    GLenum);

#define GL_ARRAY_BUFFER 0x8892
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

// Blend factors needed for eraser (destination-alpha punch-through)
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#define GL_DST_ALPHA 0x0304
#define GL_ONE_MINUS_DST_ALPHA 0x0305
#define GL_ZERO 0x0000
#define GL_ONE 0x0001

// ============================================================================
// §1b  DEBUG GL ERROR CHECK
// ============================================================================

#ifndef NDEBUG
namespace glcheck_detail {
inline void check(const char *file, int line) {
  GLenum err;
  while ((err = glGetError()) != GL_NO_ERROR) {
    char buf[128];
    _snprintf_s(buf, sizeof(buf), _TRUNCATE, "glGetError()=0x%04X at %s:%d\n",
                (unsigned)err, file, line);
    OutputDebugStringA(buf);
    assert(false && "OpenGL error");
  }
}
} // namespace glcheck_detail
#define GL_CHECK() glcheck_detail::check(__FILE__, __LINE__)
#else
#define GL_CHECK()                                                             \
  do {                                                                         \
  } while (0)
#endif

// ============================================================================
// §2  GL PROC TABLE
// ============================================================================

static void *safeGetProc(HMODULE gl32, const char *name) {
  void *p = reinterpret_cast<void *>(wglGetProcAddress(name));
  if (!p || p == (void *)1 || p == (void *)2 || p == (void *)3 ||
      p == (void *)-1)
    p = reinterpret_cast<void *>(GetProcAddress(gl32, name));
  return p;
}

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
  PFNGLGETPROGRAMIVPROC getProgramiv{};
  PFNGLGETPROGRAMINFOLOGPROC getProgramInfoLog{};
  PFNGLUSEPROGRAMPROC useProgram{};
  PFNGLGETUNIFORMLOCATIONPROC getUniformLocation{};
  PFNGLUNIFORM1IPROC uniform1i{};
  PFNGLUNIFORM1FPROC uniform1f{};
  PFNGLUNIFORM4FPROC uniform4f{};
  PFNGLUNIFORMMATRIX4FVPROC uniformMatrix4fv{};
  PFNGLENABLEVERTEXATTRIBARRAYPROC enableVertexAttribArray{};
  PFNGLDISABLEVERTEXATTRIBARRAYPROC disableVertexAttribArray{};
  PFNGLVERTEXATTRIBPOINTERPROC vertexAttribPointer{};
  PFNGLBLENDFUNCSEPARATEPROC blendFuncSeparate{};

  static GLProcs &get() {
    static GLProcs p;
    return p;
  }

  void init() {
    HMODULE gl32 = GetModuleHandleA("opengl32.dll");
#define LOAD(fn, name)                                                         \
  fn = reinterpret_cast<decltype(fn)>(safeGetProc(gl32, name))
    LOAD(genBuffers, "glGenBuffers");
    LOAD(deleteBuffers, "glDeleteBuffers");
    LOAD(bindBuffer, "glBindBuffer");
    LOAD(bufferData, "glBufferData");
    LOAD(bufferSubData, "glBufferSubData");
    LOAD(genVertexArrays, "glGenVertexArrays");
    LOAD(deleteVertexArrays, "glDeleteVertexArrays");
    LOAD(bindVertexArray, "glBindVertexArray");
    LOAD(genFramebuffers, "glGenFramebuffers");
    LOAD(deleteFramebuffers, "glDeleteFramebuffers");
    LOAD(bindFramebuffer, "glBindFramebuffer");
    LOAD(framebufferTexture2D, "glFramebufferTexture2D");
    LOAD(checkFramebufferStatus, "glCheckFramebufferStatus");
    LOAD(blitFramebuffer, "glBlitFramebuffer");
    LOAD(createShader, "glCreateShader");
    LOAD(deleteShader, "glDeleteShader");
    LOAD(shaderSource, "glShaderSource");
    LOAD(compileShader, "glCompileShader");
    LOAD(getShaderiv, "glGetShaderiv");
    LOAD(getShaderInfoLog, "glGetShaderInfoLog");
    LOAD(createProgram, "glCreateProgram");
    LOAD(deleteProgram, "glDeleteProgram");
    LOAD(attachShader, "glAttachShader");
    LOAD(linkProgram, "glLinkProgram");
    LOAD(getProgramiv, "glGetProgramiv");
    LOAD(getProgramInfoLog, "glGetProgramInfoLog");
    LOAD(useProgram, "glUseProgram");
    LOAD(getUniformLocation, "glGetUniformLocation");
    LOAD(uniform1i, "glUniform1i");
    LOAD(uniform1f, "glUniform1f");
    LOAD(uniform4f, "glUniform4f");
    LOAD(uniformMatrix4fv, "glUniformMatrix4fv");
    LOAD(enableVertexAttribArray, "glEnableVertexAttribArray");
    LOAD(disableVertexAttribArray, "glDisableVertexAttribArray");
    LOAD(vertexAttribPointer, "glVertexAttribPointer");
    LOAD(blendFuncSeparate, "glBlendFuncSeparate");
#undef LOAD
    assert(genBuffers && bindBuffer && bufferData && genVertexArrays &&
           bindVertexArray && genFramebuffers && bindFramebuffer &&
           framebufferTexture2D && checkFramebufferStatus && blitFramebuffer &&
           createShader && shaderSource && compileShader && createProgram &&
           attachShader && linkProgram && useProgram && getUniformLocation &&
           uniformMatrix4fv && enableVertexAttribArray && vertexAttribPointer);
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
    char buf[1024];
    GLsizei len = 0;
    GL.getShaderInfoLog(s, 1024, &len, buf);
    OutputDebugStringA("Shader error: ");
    OutputDebugStringA(buf);
    GL.deleteShader(s);
    return 0;
  }
  return s;
}

inline GLuint linkProgram(const char *vert, const char *frag) {
  GLuint vs = compileShader(GL_VERTEX_SHADER, vert);
  GLuint fs = compileShader(GL_FRAGMENT_SHADER, frag);
  if (!vs || !fs) {
    if (vs)
      GL.deleteShader(vs);
    if (fs)
      GL.deleteShader(fs);
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
    char buf[1024];
    GLsizei len = 0;
    GL.getProgramInfoLog(p, 1024, &len, buf);
    OutputDebugStringA("Link error: ");
    OutputDebugStringA(buf);
    GL.deleteProgram(p);
    return 0;
  }
  return p;
}

// Column-major orthographic: [l,r] x [b,t] → NDC
inline void ortho(float l, float r, float b, float t, float out[16]) {
  float rml = r - l, tmb = t - b;
  out[0] = 2.f / rml;
  out[1] = 0;
  out[2] = 0;
  out[3] = 0;
  out[4] = 0;
  out[5] = 2.f / tmb;
  out[6] = 0;
  out[7] = 0;
  out[8] = 0;
  out[9] = 0;
  out[10] = -1;
  out[11] = 0;
  out[12] = -(r + l) / rml;
  out[13] = -(t + b) / tmb;
  out[14] = 0;
  out[15] = 1;
}

} // namespace glutil

// ============================================================================
// §4  COLOR HELPERS
// ============================================================================

struct RGBA {
  float r, g, b, a;
};

inline RGBA parseHexColor(const std::string &css) {
  auto h2f = [](unsigned v) { return v / 255.f; };
  try {
    if (!css.empty() && css[0] == '#') {
      std::string h = css.substr(1);
      if (h.size() == 6) {
        unsigned n = (unsigned)std::stoul(h, nullptr, 16);
        return {h2f((n >> 16) & 0xFF), h2f((n >> 8) & 0xFF), h2f(n & 0xFF),
                1.f};
      }
      if (h.size() == 8) {
        unsigned n = (unsigned)std::stoul(h, nullptr, 16);
        return {h2f((n >> 24) & 0xFF), h2f((n >> 16) & 0xFF),
                h2f((n >> 8) & 0xFF), h2f(n & 0xFF)};
      }
    }
  } catch (...) {
  }
  static const struct {
    const char *n;
    RGBA c;
  } kNamed[] = {{"black", {0, 0, 0, 1}}, {"white", {1, 1, 1, 1}},
                {"red", {1, 0, 0, 1}},   {"green", {0, .5f, 0, 1}},
                {"blue", {0, 0, 1, 1}},  {"transparent", {0, 0, 0, 0}}};
  std::string lo = css;
  for (auto &c : lo)
    c = (char)tolower((unsigned char)c);
  for (auto &e : kNamed)
    if (lo == e.n)
      return e.c;
  return {1, 1, 1, 1};
}

// ============================================================================
// §5  STROKE DATA TYPES
// ============================================================================

using ToolId = int;
static constexpr ToolId kToolBrush = 0;
static constexpr ToolId kToolEraser = 1;

struct StrokePoint {
  float x, y, pressure;
};

struct StrokeStyle {
  float r = 0, g = 0, b = 0, a = 1;
  float radius = 4, hardness = 0.8f, opacity = 1;
  ToolId tool = kToolBrush;
};

struct Stroke {
  StrokeStyle style;
  std::vector<StrokePoint> points;
};

// ============================================================================
// §6  SCROLLBAR INFO
// ============================================================================

struct ScrollbarInfo {
  float thumbMin = 0.f;
  float thumbMax = 1.f;
  bool visible = false;
};

// ============================================================================
// §7  VIEWPORT
// ============================================================================

static constexpr float kMinZoom = 1.f / 16.f;
static constexpr float kMaxZoom = 32.f;
static constexpr float kSnapTolerance = 0.04f;
static constexpr std::array<float, 12> kSnapZooms = {
    0.0625f, 0.125f, 0.25f, 0.5f, 0.75f, 1.0f,
    1.5f,    2.0f,   4.0f,  8.0f, 16.0f, 32.0f};

class Viewport {
public:
  Viewport() = default;

  void init(int viewW, int viewH, int canvasW, int canvasH) {
    vw_ = float(viewW);
    vh_ = float(viewH);
    cw_ = float(canvasW);
    ch_ = float(canvasH);
    zoom_ = 1.f;
    offsetX_ = 0.f;
    offsetY_ = 0.f;
  }

  void setViewSize(int w, int h) {
    vw_ = float(w);
    vh_ = float(h);
    clampOffset();
  }

  void setCanvasSize(int w, int h) {
    cw_ = float(w);
    ch_ = float(h);
    clampOffset();
  }

  void zoomToward(float sx, float sy, float factor) {
    auto [cpx, cpy] = screenToCanvas(sx, sy);
    float z = std::clamp(zoom_ * factor, kMinZoom, kMaxZoom);
    z = snapZoom(z);
    zoom_ = z;
    offsetX_ = cpx - sx / z;
    offsetY_ = cpy - (vh_ - sy) / z;
    clampOffset();
  }

  void zoomIn() { zoomToward(vw_ * .5f, vh_ * .5f, 1.25f); }
  void zoomOut() { zoomToward(vw_ * .5f, vh_ * .5f, 0.8f); }
  void resetZoom() {
    zoom_ = 1.f;
    centerCanvas();
  }
  void fitToView() {
    float f = min(vw_ / cw_, vh_ / ch_);
    zoom_ = snapZoom(std::clamp(f, kMinZoom, kMaxZoom));
    centerCanvas();
  }

  void panByScreen(float dsx, float dsy) {
    offsetX_ -= dsx / zoom_;
    offsetY_ += dsy / zoom_;
    clampOffset();
  }

  void setOffsetX(float cx) {
    offsetX_ = cx;
    clampOffset();
  }
  void setOffsetY(float cy) {
    offsetY_ = cy;
    clampOffset();
  }
  void setOffset(float cx, float cy) {
    offsetX_ = cx;
    offsetY_ = cy;
    clampOffset();
  }

  std::pair<float, float> screenToCanvas(float sx, float sy) const {
    return {offsetX_ + sx / zoom_, offsetY_ + (vh_ - sy) / zoom_};
  }

  ScrollbarInfo scrollbarH() const {
    float view = vw_ / zoom_;
    if (view >= cw_)
      return {0, 1, false};
    return {std::clamp(offsetX_ / cw_, 0.f, 1.f),
            std::clamp((offsetX_ + view) / cw_, 0.f, 1.f), true};
  }

  ScrollbarInfo scrollbarV() const {
    float view = vh_ / zoom_;
    if (view >= ch_)
      return {0, 1, false};
    float end = offsetY_ + view;
    return {std::clamp(1.f - end / ch_, 0.f, 1.f),
            std::clamp(1.f - offsetY_ / ch_, 0.f, 1.f), true};
  }

  void buildMVP(float out[16]) const {
    float l = offsetX_, r = offsetX_ + vw_ / zoom_;
    float b = offsetY_, t = offsetY_ + vh_ / zoom_;
    glutil::ortho(l, r, b, t, out);
  }

  float zoom() const { return zoom_; }
  float offsetX() const { return offsetX_; }
  float offsetY() const { return offsetY_; }
  float viewW() const { return vw_; }
  float viewH() const { return vh_; }
  float canvasW() const { return cw_; }
  float canvasH() const { return ch_; }

private:
  float vw_ = 1, vh_ = 1, cw_ = 1, ch_ = 1;
  float zoom_ = 1.f, offsetX_ = 0.f, offsetY_ = 0.f;

  static float snapZoom(float z) {
    for (float s : kSnapZooms)
      if (std::abs(z - s) / s < kSnapTolerance)
        return s;
    return z;
  }

  void centerCanvas() {
    offsetX_ = (cw_ - vw_ / zoom_) * .5f;
    offsetY_ = (ch_ - vh_ / zoom_) * .5f;
    clampOffset();
  }

  void clampOffset() {
    float viewW = vw_ / zoom_;
    float viewH = vh_ / zoom_;
    if (viewW >= cw_)
      offsetX_ = (cw_ - viewW) * 0.5f;
    else
      offsetX_ = std::clamp(offsetX_, 0.f, cw_ - viewW);
    if (viewH >= ch_)
      offsetY_ = (ch_ - viewH) * 0.5f;
    else
      offsetY_ = std::clamp(offsetY_, 0.f, ch_ - viewH);
  }
};

// ============================================================================
// §8  RENDER SURFACE  —  pure abstract interface
// ============================================================================

class RenderSurface {
public:
  virtual ~RenderSurface() = default;
  virtual void initialize(int w, int h) = 0;
  virtual void resize(int w, int h) = 0;
  virtual void update(double dt) = 0;
  virtual void render(const float mvp[16]) = 0;
  virtual void onMouseDown(float x, float y) {}
  virtual void onMouseMove(float x, float y) {}
  virtual void onMouseUp(float x, float y) {}
  virtual void onKeyDown(int key) {}
  virtual void onKeyUp(int key) {}
  virtual void onRightMouseDown(float x, float y) {}
  virtual void destroy() = 0;
};

// ============================================================================
// §9  RASTER SURFACE
// ============================================================================

class RasterSurface : public RenderSurface {
public:
  static constexpr size_t kDefaultUndoBudgetBytes = 256ULL * 1024 * 1024;
  static constexpr int kColorHistoryMax = 16;

  explicit RasterSurface(size_t budget = kDefaultUndoBudgetBytes)
      : undoBudget_(budget) {}
  ~RasterSurface() { destroy(); }

  // ── Tool / style setters ─────────────────────────────────────────────────

  void setTool(ToolId t) { tool_ = t; }
  ToolId getTool() const { return tool_; }

  void setStrokeStyle(const StrokeStyle &s) {
    style_ = s;
    style_.tool = tool_;
  }
  const StrokeStyle &getStrokeStyle() const { return style_; }

  void setOpacity(float op) { style_.opacity = std::clamp(op, 0.f, 1.f); }
  float getOpacity() const { return style_.opacity; }

  // ── Color history ────────────────────────────────────────────────────────

  const std::vector<RGBA> &colorHistory() const { return colorHistory_; }

  // ── Undo / Redo ──────────────────────────────────────────────────────────

  void undo() {
    if (undoStack_.empty())
      return;
    size_t sz = snapshotBytes();
    redoStack_.push_back({snapshotCommitted(), sz});
    redoBytes_ += sz;
    Snapshot s = undoStack_.back();
    undoStack_.pop_back();
    undoBytes_ -= s.bytes;
    restoreCommitted(s.tex);
    glDeleteTextures(1, &s.tex);
    dirty_ = true;
  }

  void redo() {
    if (redoStack_.empty())
      return;
    size_t sz = snapshotBytes();
    undoStack_.push_back({snapshotCommitted(), sz});
    undoBytes_ += sz;
    Snapshot s = redoStack_.back();
    redoStack_.pop_back();
    redoBytes_ -= s.bytes;
    restoreCommitted(s.tex);
    glDeleteTextures(1, &s.tex);
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

  // ── PNG export ───────────────────────────────────────────────────────────

  bool savePNG(const std::wstring &path) {
    if (!committedFBO_ || w_ <= 0 || h_ <= 0)
      return false;

    std::vector<uint8_t> buf(size_t(w_) * h_ * 4);
    GL.bindFramebuffer(GL_READ_FRAMEBUFFER, committedFBO_);
    glReadPixels(0, 0, w_, h_, GL_RGBA, GL_UNSIGNED_BYTE, buf.data());
    GL.bindFramebuffer(GL_READ_FRAMEBUFFER, 0);

    std::vector<uint8_t> flipped(buf.size());
    const int stride = w_ * 4;
    for (int row = 0; row < h_; ++row) {
      memcpy(flipped.data() + row * stride,
             buf.data() + (h_ - 1 - row) * stride, stride);
    }

    for (int i = 0; i < w_ * h_; ++i) {
      uint8_t r = flipped[i * 4 + 0];
      uint8_t g = flipped[i * 4 + 1];
      uint8_t b = flipped[i * 4 + 2];
      uint8_t a = flipped[i * 4 + 3];
      flipped[i * 4 + 0] = b;
      flipped[i * 4 + 1] = g;
      flipped[i * 4 + 2] = r;
      flipped[i * 4 + 3] = a;
    }

    Gdiplus::Bitmap bmp(w_, h_, stride, PixelFormat32bppARGB, flipped.data());
    if (bmp.GetLastStatus() != Gdiplus::Ok)
      return false;

    CLSID pngClsid;
    {
      UINT num = 0, sz = 0;
      Gdiplus::GetImageEncodersSize(&num, &sz);
      if (sz == 0)
        return false;
      std::vector<uint8_t> ebuf(sz);
      auto *encoders = reinterpret_cast<Gdiplus::ImageCodecInfo *>(ebuf.data());
      Gdiplus::GetImageEncoders(num, sz, encoders);
      bool found = false;
      for (UINT i = 0; i < num; ++i) {
        if (wcscmp(encoders[i].MimeType, L"image/png") == 0) {
          pngClsid = encoders[i].Clsid;
          found = true;
          break;
        }
      }
      if (!found)
        return false;
    }

    Gdiplus::Status st = bmp.Save(path.c_str(), &pngClsid, nullptr);
    return st == Gdiplus::Ok;
  }

  // ── RenderSurface interface ───────────────────────────────────────────────

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
  }

  void resize(int w, int h) override {
    if (w == w_ && h == h_)
      return;
    GLuint nCF = 0, nCT = 0, nSF = 0, nST = 0;
    allocFBOPair(nCF, nCT, w, h);
    allocFBOPair(nSF, nST, w, h);
    clearFBO(nCF, w, h, 255, 255, 255, 255);
    clearFBO(nSF, w, h, 0, 0, 0, 0);
    int cw = min(w_, w), ch = min(h_, h);
    GL.bindFramebuffer(GL_READ_FRAMEBUFFER, committedFBO_);
    GL.bindFramebuffer(GL_DRAW_FRAMEBUFFER, nCF);
    GL.blitFramebuffer(0, 0, cw, ch, 0, 0, cw, ch, GL_COLOR_BUFFER_BIT,
                       GL_NEAREST);
    GL.bindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    GL.bindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    destroyFBOPair(committedFBO_, committedTex_);
    destroyFBOPair(scratchFBO_, scratchTex_);
    committedFBO_ = nCF;
    committedTex_ = nCT;
    scratchFBO_ = nSF;
    scratchTex_ = nST;
    flushDeque(undoStack_, undoBytes_);
    flushDeque(redoStack_, redoBytes_);
    w_ = w;
    h_ = h;
    dirty_ = true;
  }

  void update(double) override {}

  void render(const float mvp[16]) override {
    GLboolean blendWas = glIsEnabled(GL_BLEND);

    glDisable(GL_SCISSOR_TEST);
    glClearColor(0.15f, 0.15f, 0.17f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    blitTexture(committedTex_, 1.f, mvp);
    blitTexture(scratchTex_, 1.f, mvp);

    glDisable(GL_BLEND);

    dirty_ = false;
    GL.useProgram(0);
    if (blendWas)
      glEnable(GL_BLEND);
  }

  void onMouseDown(float x, float y) override { beginStroke(x, y); }
  void onMouseMove(float x, float y) override {
    if (drawing_)
      drawSegment(x, y);
  }
  void onMouseUp(float x, float y) override {
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
  void onKeyUp(int) override {}

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
    if (shapeVBO_) {
      GL.deleteBuffers(1, &shapeVBO_);
      shapeVBO_ = 0;
    }
    flushDeque(undoStack_, undoBytes_);
    flushDeque(redoStack_, redoBytes_);
  }

protected:
  int canvasWidth() const { return w_; }
  int canvasHeight() const { return h_; }
  GLuint committedFBOHandle() const { return committedFBO_; }
  GLuint committedTexHandle() const { return committedTex_; }
  GLuint scratchFBOHandle() const { return scratchFBO_; }
  GLuint scratchTexHandle() const { return scratchTex_; }
  GLuint blitProgHandle() const { return blitProg_; }
  GLuint quadVAOHandle() const { return quadVAO_; }
  GLuint quadVBOHandle() const { return quadVBO_; }
  bool isDrawing() const { return drawing_; }

  void pushUndoSnapshotPublic() { pushUndoSnapshot(); }

  void scratchClear() { clearFBO(scratchFBO_, w_, h_, 0, 0, 0, 0); }

  void scratchCommit() {
    pushUndoSnapshot();
    mergeScratchIntoCommitted();
    clearFBO(scratchFBO_, w_, h_, 0, 0, 0, 0);
  }

  GLint uMVP() const { return u_.mvp; }
  GLint uMode() const { return u_.mode; }
  GLint uColor() const { return u_.color; }
  GLint uTex() const { return u_.tex; }
  GLint uAlpha() const { return u_.alpha; }

  void getCanvasOrtho(float out[16]) const { canvasOrtho(out); }

  // ── FIX: drawVertsToFBO now uses a dedicated shapeVBO_ so it never
  //         touches the quadVBO_ that blitTexture depends on.
  //         This eliminates the VBO size corruption that wiped the canvas.
  void drawVertsToFBO(GLuint fbo, const float *verts, int count, GLenum mode,
                      float r, float g, float b, float a) {
    float mvp[16];
    canvasOrtho(mvp);
    GL.bindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, w_, h_);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    GL.useProgram(blitProg_);
    GL.uniformMatrix4fv(u_.mvp, 1, GL_FALSE, mvp);
    GL.uniform1i(u_.mode, 1);
    GL.uniform4f(u_.color, r, g, b, a);

    // Use the dedicated shape VBO — never touches quadVBO_
    GLsizeiptr needed = GLsizeiptr(sizeof(float)) * 2 * count;
    GL.bindBuffer(GL_ARRAY_BUFFER, shapeVBO_);
    GL.bufferData(GL_ARRAY_BUFFER, needed, verts, GL_DYNAMIC_DRAW);

    // Bind a minimal VAO state inline (no persistent VAO format needed
    // since we always set attrib pointers before drawing)
    GL.bindVertexArray(quadVAO_);
    GL.enableVertexAttribArray(0);
    GL.vertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 2,
                           nullptr);
    // Disable attrib 1 so the blit shader's UV input reads (0,0) if it ever
    // runs in this path (it won't — mode==1 skips the texture sample).
    GL.disableVertexAttribArray(1);

    glDrawArrays(mode, 0, count);

    GL.bindVertexArray(0);
    GL.bindBuffer(GL_ARRAY_BUFFER, 0);
    GL.useProgram(0);
    glDisable(GL_BLEND);
    GL.bindFramebuffer(GL_FRAMEBUFFER, 0);
  }

  void uploadToCommitted(const uint8_t *pixels) {
    glBindTexture(GL_TEXTURE_2D, committedTex_);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w_, h_, GL_RGBA, GL_UNSIGNED_BYTE,
                    pixels);
    glBindTexture(GL_TEXTURE_2D, 0);
  }

  void readCommitted(uint8_t *pixels) {
    GL.bindFramebuffer(GL_READ_FRAMEBUFFER, committedFBO_);
    glReadPixels(0, 0, w_, h_, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    GL.bindFramebuffer(GL_READ_FRAMEBUFFER, 0);
  }

private:
  int w_ = 0, h_ = 0;
  bool drawing_ = false, dirty_ = false;
  StrokePoint lastPt_{};
  StrokeStyle style_{};
  ToolId tool_ = kToolBrush;

  GLuint committedFBO_ = 0, committedTex_ = 0;
  GLuint scratchFBO_ = 0, scratchTex_ = 0;
  GLuint blitProg_ = 0, quadVAO_ = 0, quadVBO_ = 0;
  GLuint shapeVBO_ = 0; // ── FIX: dedicated VBO for drawVertsToFBO

  struct ULocs {
    GLint mvp = -1, mode = -1, tex = -1, alpha = -1, color = -1;
  } u_;

  struct Snapshot {
    GLuint tex;
    size_t bytes;
  };
  size_t undoBudget_, undoBytes_ = 0, redoBytes_ = 0;
  std::deque<Snapshot> undoStack_, redoStack_;

  std::vector<RGBA> colorHistory_;

  void pushColorHistory(const RGBA &c) {
    if (!colorHistory_.empty()) {
      const RGBA &last = colorHistory_.front();
      if (std::abs(last.r - c.r) < 0.01f && std::abs(last.g - c.g) < 0.01f &&
          std::abs(last.b - c.b) < 0.01f)
        return;
    }
    colorHistory_.insert(colorHistory_.begin(), c);
    if ((int)colorHistory_.size() > kColorHistoryMax)
      colorHistory_.resize(kColorHistoryMax);
  }

  size_t snapshotBytes() const { return size_t(w_) * h_ * 4; }

  static constexpr int kDabVerts = 32;
  float dabVerts[(kDabVerts + 2) * 2]{};

  static void flushDeque(std::deque<Snapshot> &dq, size_t &cnt) {
    for (auto &s : dq)
      glDeleteTextures(1, &s.tex);
    dq.clear();
    cnt = 0;
  }

  // ── Stroke lifecycle ──────────────────────────────────────────────────────

  void beginStroke(float x, float y) {
    if (drawing_)
      return;
    drawing_ = true;
    lastPt_ = {x, y, 1.f};
    flushDeque(redoStack_, redoBytes_);

    if (tool_ == kToolBrush) {
      pushColorHistory({style_.r, style_.g, style_.b, 1.f});
      clearFBO(scratchFBO_, w_, h_, 0, 0, 0, 0);
      paintDabBrush(scratchFBO_, x, y, style_);
    } else {
      pushUndoSnapshot();
      paintDabEraser(committedFBO_, x, y, style_);
    }
    dirty_ = true;
  }

  void drawSegment(float x, float y) {
    float dx = x - lastPt_.x, dy = y - lastPt_.y;
    float dist = std::sqrt(dx * dx + dy * dy);
    if (dist < 0.0001f)
      return;
    float step = max(1.f, style_.radius * .25f);
    int steps = max(1, (int)(dist / step));
    for (int i = 1; i <= steps; ++i) {
      float t = float(i) / steps;
      if (tool_ == kToolBrush)
        paintDabBrush(scratchFBO_, lastPt_.x + dx * t, lastPt_.y + dy * t,
                      style_);
      else
        paintDabEraser(committedFBO_, lastPt_.x + dx * t, lastPt_.y + dy * t,
                       style_);
    }
    lastPt_ = {x, y, 1.f};
    dirty_ = true;
  }

  void endStroke(float x, float y) {
    drawSegment(x, y);
    if (tool_ == kToolBrush) {
      pushUndoSnapshot();
      mergeScratchIntoCommitted();
      clearFBO(scratchFBO_, w_, h_, 0, 0, 0, 0);
    }
    drawing_ = false;
    dirty_ = true;
  }

  // ── FBO helpers ───────────────────────────────────────────────────────────

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
    assert(GL.checkFramebufferStatus(GL_FRAMEBUFFER) ==
           GL_FRAMEBUFFER_COMPLETE);
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

  GLuint snapshotCommitted() {
    GLuint st, sf;
    glGenTextures(1, &st);
    glBindTexture(GL_TEXTURE_2D, st);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w_, h_, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, nullptr);
    glBindTexture(GL_TEXTURE_2D, 0);
    GL.genFramebuffers(1, &sf);
    GL.bindFramebuffer(GL_DRAW_FRAMEBUFFER, sf);
    GL.framebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                            GL_TEXTURE_2D, st, 0);
    assert(GL.checkFramebufferStatus(GL_DRAW_FRAMEBUFFER) ==
           GL_FRAMEBUFFER_COMPLETE);
    GL.bindFramebuffer(GL_READ_FRAMEBUFFER, committedFBO_);
    GL.blitFramebuffer(0, 0, w_, h_, 0, 0, w_, h_, GL_COLOR_BUFFER_BIT,
                       GL_NEAREST);
    GL.bindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    GL.bindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    GL.deleteFramebuffers(1, &sf);
    return st;
  }

  void restoreCommitted(GLuint snapTex) {
    GLuint rf;
    GL.genFramebuffers(1, &rf);
    GL.bindFramebuffer(GL_READ_FRAMEBUFFER, rf);
    GL.framebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                            GL_TEXTURE_2D, snapTex, 0);
    assert(GL.checkFramebufferStatus(GL_READ_FRAMEBUFFER) ==
           GL_FRAMEBUFFER_COMPLETE);
    GL.bindFramebuffer(GL_DRAW_FRAMEBUFFER, committedFBO_);
    GL.blitFramebuffer(0, 0, w_, h_, 0, 0, w_, h_, GL_COLOR_BUFFER_BIT,
                       GL_NEAREST);
    GL.bindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    GL.bindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    GL.deleteFramebuffers(1, &rf);
  }

  void pushUndoSnapshot() {
    size_t sz = snapshotBytes();
    while (undoBudget_ > 0 && !undoStack_.empty() &&
           undoBytes_ + sz > undoBudget_) {
      auto &o = undoStack_.front();
      glDeleteTextures(1, &o.tex);
      undoBytes_ -= o.bytes;
      undoStack_.pop_front();
    }
    undoStack_.push_back({snapshotCommitted(), sz});
    undoBytes_ += sz;
  }

  void canvasOrtho(float out[16]) const {
    glutil::ortho(0.f, float(w_), 0.f, float(h_), out);
  }

  void mergeScratchIntoCommitted() {
    float mvp[16];
    canvasOrtho(mvp);
    GL.bindFramebuffer(GL_FRAMEBUFFER, committedFBO_);
    glViewport(0, 0, w_, h_);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    blitTexture(scratchTex_, 1.f, mvp);
    glDisable(GL_BLEND);
    GL.bindFramebuffer(GL_FRAMEBUFFER, 0);
  }

  // ── Dab painting ─────────────────────────────────────────────────────────

  void paintDabBrush(GLuint fbo, float cx, float cy, const StrokeStyle &s) {
    float mvp[16];
    canvasOrtho(mvp);
    GL.bindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, w_, h_);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    paintDabImpl(cx, cy, s.r, s.g, s.b, s.a * s.opacity, s.radius, mvp);
    glDisable(GL_BLEND);
    GL.bindFramebuffer(GL_FRAMEBUFFER, 0);
  }

  void paintDabEraser(GLuint fbo, float cx, float cy, const StrokeStyle &s) {
    float mvp[16];
    canvasOrtho(mvp);
    GL.bindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, w_, h_);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    paintDabImpl(cx, cy, 1.f, 1.f, 1.f, s.opacity, s.radius, mvp);
    glDisable(GL_BLEND);
    GL.bindFramebuffer(GL_FRAMEBUFFER, 0);
  }

  // paintDabImpl uses quadVBO_ (dab geometry), which is always sized to
  // hold (kDabVerts+2)*2 floats — large enough for blitTexture too.
  // It never calls bufferData, only bufferSubData, so the size is stable.
  void paintDabImpl(float cx, float cy, float r, float g, float b, float alpha,
                    float radius, const float mvp[16]) {
    GL.useProgram(blitProg_);
    GL.uniformMatrix4fv(u_.mvp, 1, GL_FALSE, mvp);
    GL.uniform1i(u_.mode, 1);
    GL.uniform4f(u_.color, r, g, b, alpha);

    float verts[(kDabVerts + 2) * 2];
    for (int i = 0; i < kDabVerts + 2; ++i) {
      verts[i * 2 + 0] = cx + dabVerts[i * 2 + 0] * radius;
      verts[i * 2 + 1] = cy + dabVerts[i * 2 + 1] * radius;
    }
    GL.bindVertexArray(quadVAO_);
    GL.bindBuffer(GL_ARRAY_BUFFER, quadVBO_);
    GL.bufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
    GL.enableVertexAttribArray(0);
    GL.vertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 2,
                           nullptr);
    GL.disableVertexAttribArray(1);
    glDrawArrays(GL_TRIANGLE_FAN, 0, kDabVerts + 2);
    GL.bindVertexArray(0);
    GL.useProgram(0);
  }

  // blitTexture exclusively uses quadVBO_ with bufferSubData.
  // quadVBO_ is sized in buildQuadVAO to the maximum of the blit quad and
  // the dab verts, so bufferSubData is always safe.
  void blitTexture(GLuint tex, float alpha, const float *mvp) {
    GL.useProgram(blitProg_);
    GL.uniformMatrix4fv(u_.mvp, 1, GL_FALSE, mvp);
    GL.uniform1i(u_.mode, 0);
    GL.uniform1i(u_.tex, 0);
    GL.uniform1f(u_.alpha, alpha);
    glBindTexture(GL_TEXTURE_2D, tex);
    float q[] = {
        0.f,       0.f,       0.f, 0.f, float(w_), 0.f,       1.f, 0.f,
        float(w_), float(h_), 1.f, 1.f, float(w_), float(h_), 1.f, 1.f,
        0.f,       float(h_), 0.f, 1.f, 0.f,       0.f,       0.f, 0.f,
    };
    GL.bindVertexArray(quadVAO_);
    GL.bindBuffer(GL_ARRAY_BUFFER, quadVBO_);
    GL.bufferSubData(GL_ARRAY_BUFFER, 0, sizeof(q), q);
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
void main(){ vUV=aUV; gl_Position=uMVP*vec4(aPos,0,1); }
)GLSL";
    const char *frag = R"GLSL(
#version 330 core
in vec2 vUV;
uniform sampler2D uTex;
uniform vec4 uColor;
uniform float uAlpha;
uniform int uMode;
out vec4 fragColor;
void main(){
    if(uMode==0){ vec4 c=texture(uTex,vUV); fragColor=vec4(c.rgb,c.a*uAlpha); }
    else         { fragColor=uColor; }
}
)GLSL";
    blitProg_ = glutil::linkProgram(vert, frag);
    assert(blitProg_);
    u_.mvp = GL.getUniformLocation(blitProg_, "uMVP");
    u_.mode = GL.getUniformLocation(blitProg_, "uMode");
    u_.tex = GL.getUniformLocation(blitProg_, "uTex");
    u_.alpha = GL.getUniformLocation(blitProg_, "uAlpha");
    u_.color = GL.getUniformLocation(blitProg_, "uColor");
  }

  void buildQuadVAO() {
    // ── FIX: size quadVBO_ to the MAXIMUM of all users:
    //   blitTexture:  6 verts × 4 floats  = 96 bytes
    //   paintDabImpl: (kDabVerts+2) × 2 floats = (32+2)*2*4 = 272 bytes
    //   Both use bufferSubData, so the buffer must be pre-sized to the max.
    //   shapeVBO_ is separate and uses bufferData, so no size constraint.
    constexpr GLsizeiptr kBlitBytes = sizeof(float) * 6 * 4;
    constexpr GLsizeiptr kDabBytes = sizeof(float) * (kDabVerts + 2) * 2;
    constexpr GLsizeiptr kQuadBufSz =
        kBlitBytes > kDabBytes ? kBlitBytes : kDabBytes;

    GL.genVertexArrays(1, &quadVAO_);
    GL.genBuffers(1, &quadVBO_);
    GL.bindVertexArray(quadVAO_);
    GL.bindBuffer(GL_ARRAY_BUFFER, quadVBO_);
    GL.bufferData(GL_ARRAY_BUFFER, kQuadBufSz, nullptr, GL_DYNAMIC_DRAW);
    GL.bindVertexArray(0);

    // Dedicated shape VBO — starts with 0 bytes; drawVertsToFBO always
    // calls bufferData so the size is set fresh each call.
    GL.genBuffers(1, &shapeVBO_);
    GL.bindBuffer(GL_ARRAY_BUFFER, shapeVBO_);
    GL.bufferData(GL_ARRAY_BUFFER, sizeof(float) * 2 * 12, nullptr,
                  GL_DYNAMIC_DRAW);
    GL.bindBuffer(GL_ARRAY_BUFFER, 0);
  }

  void buildDabVerts() {
    dabVerts[0] = 0.f;
    dabVerts[1] = 0.f;
    for (int i = 0; i <= kDabVerts; ++i) {
      float a = float(i) / kDabVerts * 6.2831853f;
      dabVerts[2 + i * 2 + 0] = cosf(a);
      dabVerts[2 + i * 2 + 1] = sinf(a);
    }
  }
};

// ============================================================================
// §10  CANVAS WIDGET  (two-window hierarchy)
// ============================================================================

class CanvasWidget : public Widget {
public:
  explicit CanvasWidget() {
    autoWidth = autoHeight = false;
    width = 400;
    height = 300;
  }
  ~CanvasWidget() { destroyGL(); }

  std::shared_ptr<CanvasWidget> setViewportEnabled(bool enabled) {
    viewportEnabled_ = enabled;
    return ptr();
  }

  template <typename T, typename... A> std::shared_ptr<T> setSurface(A &&...a) {
    auto s = std::make_shared<T>(std::forward<A>(a)...);
    pendingSurface_ = s;
    return s;
  }

  RenderSurface *getSurface() const { return activeSurface_.get(); }
  const Viewport &viewport() const { return vp_; }
  Viewport &viewport() { return vp_; }

  std::shared_ptr<CanvasWidget> setSize(int w, int h) {
    width = w;
    height = h;
    autoWidth = autoHeight = false;
    markNeedsLayout();
    return ptr();
  }

  std::shared_ptr<CanvasWidget> setCanvasSize(int w, int h) {
    canvasW_ = w;
    canvasH_ = h;
    if (frameHwnd_) {
      vp_.setCanvasSize(w, h);
      if (activeSurface_) {
        if (wglGetCurrentContext() != glRC_)
          wglMakeCurrent(glDC_, glRC_);
        activeSurface_->resize(w, h);
      }
    }
    return ptr();
  }

  std::shared_ptr<CanvasWidget> redraw() {
    markNeedsPaint();
    return ptr();
  }

  std::function<void(float zoom)> onViewportChanged;

  // ── Widget virtuals ───────────────────────────────────────────────────────

  void computeLayout(HDC, const BoxConstraints &c, FontCache &) override {
    width = c.clampWidth(autoWidth ? c.maxWidth : width);
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
  bool viewportEnabled_ = true;

  std::shared_ptr<CanvasWidget> ptr() {
    return std::static_pointer_cast<CanvasWidget>(shared_from_this());
  }

  std::shared_ptr<RenderSurface> activeSurface_, pendingSurface_;
  Viewport vp_;
  int canvasW_ = 512, canvasH_ = 512;

  using Clock = std::chrono::steady_clock;
  Clock::time_point lastTick_ = Clock::now();

  HWND frameHwnd_ = nullptr;
  HWND glHwnd_ = nullptr;
  HDC glDC_ = nullptr;
  HGLRC glRC_ = nullptr;
  HWND parentHwnd_ = nullptr;

  bool repaintPending_ = false;
  bool trackingLeave_ = false;
  int lastGLW_ = 0, lastGLH_ = 0;

  bool panning_ = false;
  int panStartSX_ = 0, panStartSY_ = 0;
  float panStartOX_ = 0, panStartOY_ = 0;

  void beginPan(int sx, int sy) {
    panning_ = true;
    panStartSX_ = sx;
    panStartSY_ = sy;
    panStartOX_ = vp_.offsetX();
    panStartOY_ = vp_.offsetY();
  }

  void continuePan(int sx, int sy) {
    float dx = float(sx - panStartSX_), dy = float(sy - panStartSY_);
    vp_.setOffset(panStartOX_ - dx / vp_.zoom(), panStartOY_ + dy / vp_.zoom());
    scheduleRepaint();
  }

  void scheduleRepaint() {
    if (onViewportChanged)
      onViewportChanged(vp_.zoom());
    if (!repaintPending_) {
      repaintPending_ = true;
      PostMessage(glHwnd_, WM_USER + 1, 0, 0);
    }
  }

  static constexpr int kSBRange = 10000;
  static int sbW() { return GetSystemMetrics(SM_CXVSCROLL); }
  static int sbH() { return GetSystemMetrics(SM_CYHSCROLL); }

  void glChildSize(int fw, int fh, int &ow, int &oh) const {
    if (!viewportEnabled_) {
      ow = fw;
      oh = fh;
    } else {
      ScrollbarInfo h = vp_.scrollbarH(), v = vp_.scrollbarV();
      ow = fw - (v.visible ? sbW() : 0);
      oh = fh - (h.visible ? sbH() : 0);
    }
    if (ow < 1)
      ow = 1;
    if (oh < 1)
      oh = 1;
  }

  void syncScrollbars() {
    if (!frameHwnd_ || !viewportEnabled_)
      return;
    ScrollbarInfo h = vp_.scrollbarH(), v = vp_.scrollbarV();
    ShowScrollBar(frameHwnd_, SB_HORZ, h.visible ? TRUE : FALSE);
    ShowScrollBar(frameHwnd_, SB_VERT, v.visible ? TRUE : FALSE);
    if (h.visible) {
      SCROLLINFO si{};
      si.cbSize = sizeof(si);
      si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
      si.nMin = 0;
      si.nMax = kSBRange;
      si.nPage = UINT((h.thumbMax - h.thumbMin) * kSBRange);
      si.nPos = int(h.thumbMin * kSBRange);
      SetScrollInfo(frameHwnd_, SB_HORZ, &si, TRUE);
    }
    if (v.visible) {
      SCROLLINFO si{};
      si.cbSize = sizeof(si);
      si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
      si.nMin = 0;
      si.nMax = kSBRange;
      si.nPage = UINT((v.thumbMax - v.thumbMin) * kSBRange);
      si.nPos = int(v.thumbMin * kSBRange);
      SetScrollInfo(frameHwnd_, SB_VERT, &si, TRUE);
    }
    RECT rc;
    GetClientRect(frameHwnd_, &rc);
    int gw, gh;
    glChildSize(rc.right, rc.bottom, gw, gh);
    SetWindowPos(glHwnd_, nullptr, 0, 0, gw, gh,
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOMOVE);
  }

  void applyHScroll(int pos, int maxPos) {
    if (maxPos <= 0)
      return;
    float range = vp_.canvasW() - vp_.viewW() / vp_.zoom();
    vp_.setOffsetX(float(pos) / maxPos * max(0.f, range));
    scheduleRepaint();
  }

  void applyVScroll(int pos, int maxPos) {
    if (maxPos <= 0)
      return;
    float range = vp_.canvasH() - vp_.viewH() / vp_.zoom();
    float t = 1.f - float(pos) / maxPos;
    vp_.setOffsetY(t * max(0.f, range));
    scheduleRepaint();
  }

  // ── Frame window proc ────────────────────────────────────────────────────

  static LRESULT CALLBACK FrameProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto *self = reinterpret_cast<CanvasWidget *>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
    case WM_ERASEBKGND:
      return 1;
    case WM_PAINT: {
      PAINTSTRUCT ps;
      BeginPaint(hwnd, &ps);
      EndPaint(hwnd, &ps);
      return 0;
    }
    case WM_HSCROLL: {
      if (!self)
        return 0;
      SCROLLINFO si{};
      si.cbSize = sizeof(si);
      si.fMask = SIF_ALL;
      GetScrollInfo(hwnd, SB_HORZ, &si);
      int p = si.nPos;
      switch (LOWORD(wp)) {
      case SB_LINELEFT:
        p -= kSBRange / 50;
        break;
      case SB_LINERIGHT:
        p += kSBRange / 50;
        break;
      case SB_PAGELEFT:
        p -= si.nPage;
        break;
      case SB_PAGERIGHT:
        p += si.nPage;
        break;
      case SB_THUMBTRACK:
      case SB_THUMBPOSITION:
        p = si.nTrackPos;
        break;
      }
      p = std::clamp(p, si.nMin, si.nMax - (int)si.nPage);
      self->applyHScroll(p, si.nMax - (int)si.nPage);
      return 0;
    }
    case WM_VSCROLL: {
      if (!self)
        return 0;
      SCROLLINFO si{};
      si.cbSize = sizeof(si);
      si.fMask = SIF_ALL;
      GetScrollInfo(hwnd, SB_VERT, &si);
      int p = si.nPos;
      switch (LOWORD(wp)) {
      case SB_LINEUP:
        p -= kSBRange / 50;
        break;
      case SB_LINEDOWN:
        p += kSBRange / 50;
        break;
      case SB_PAGEUP:
        p -= si.nPage;
        break;
      case SB_PAGEDOWN:
        p += si.nPage;
        break;
      case SB_THUMBTRACK:
      case SB_THUMBPOSITION:
        p = si.nTrackPos;
        break;
      }
      p = std::clamp(p, si.nMin, si.nMax - (int)si.nPage);
      self->applyVScroll(p, si.nMax - (int)si.nPage);
      return 0;
    }
    case WM_MOUSEWHEEL:
      if (self && self->viewportEnabled_ && self->glHwnd_)
        PostMessage(self->glHwnd_, msg, wp, lp);
      return 0;
    default:
      return DefWindowProc(hwnd, msg, wp, lp);
    }
  }

  // ── GL child window proc ─────────────────────────────────────────────────

  static LRESULT CALLBACK GLProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto *self = reinterpret_cast<CanvasWidget *>(
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

    case WM_MBUTTONDOWN:
      if (!self || !self->viewportEnabled_)
        return 0;
      SetCapture(hwnd);
      self->beginPan(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
      return 0;
    case WM_MBUTTONUP:
      if (!self || !self->viewportEnabled_)
        return 0;
      ReleaseCapture();
      self->panning_ = false;
      return 0;

    case WM_LBUTTONDOWN: {
      SetCapture(hwnd);
      if (!self)
        return 0;
      if (self->viewportEnabled_ && (GetKeyState(VK_SPACE) & 0x8000)) {
        self->beginPan(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
      } else if (self->activeSurface_) {
        auto [cx, cy] = self->vp_.screenToCanvas(float(GET_X_LPARAM(lp)),
                                                 float(GET_Y_LPARAM(lp)));
        self->activeSurface_->onMouseDown(cx, cy);
      }
      forwardToParent(hwnd, msg, wp, lp);
      return 0;
    }
    case WM_LBUTTONUP: {
      ReleaseCapture();
      if (!self)
        return 0;
      if (self->panning_) {
        self->panning_ = false;
      } else if (self->activeSurface_) {
        auto [cx, cy] = self->vp_.screenToCanvas(float(GET_X_LPARAM(lp)),
                                                 float(GET_Y_LPARAM(lp)));
        self->activeSurface_->onMouseUp(cx, cy);
      }
      forwardToParent(hwnd, msg, wp, lp);
      return 0;
    }
    case WM_RBUTTONDOWN: {
      SetCapture(hwnd);
      if (self && self->activeSurface_) {
        auto [cx, cy] = self->vp_.screenToCanvas(float(GET_X_LPARAM(lp)),
                                                 float(GET_Y_LPARAM(lp)));
        self->activeSurface_->onRightMouseDown(cx, cy);
      }
      forwardToParent(hwnd, msg, wp, lp);
      return 0;
    }
    case WM_RBUTTONUP: {
      ReleaseCapture();
      if (self && self->activeSurface_) {
        auto [cx, cy] = self->vp_.screenToCanvas(float(GET_X_LPARAM(lp)),
                                                 float(GET_Y_LPARAM(lp)));
        self->activeSurface_->onMouseUp(cx, cy);
      }
      forwardToParent(hwnd, msg, wp, lp);
      return 0;
    }
    case WM_MOUSEMOVE: {
      if (!self)
        return 0;
      int sx = GET_X_LPARAM(lp), sy = GET_Y_LPARAM(lp);
      if (!self->trackingLeave_) {
        TRACKMOUSEEVENT tme{};
        tme.cbSize = sizeof(tme);
        tme.dwFlags = TME_LEAVE;
        tme.hwndTrack = hwnd;
        TrackMouseEvent(&tme);
        self->trackingLeave_ = true;
      }
      if (self->panning_) {
        self->continuePan(sx, sy);
      } else if (self->activeSurface_) {
        auto [cx, cy] = self->vp_.screenToCanvas(float(sx), float(sy));
        self->activeSurface_->onMouseMove(cx, cy);
      }
      forwardToParent(hwnd, msg, wp, lp);
      return 0;
    }
    case WM_MOUSELEAVE:
      if (self)
        self->trackingLeave_ = false;
      return 0;

    case WM_MOUSEWHEEL: {
      if (!self || !self->viewportEnabled_)
        return 0;
      int delta = GET_WHEEL_DELTA_WPARAM(wp);
      bool ctrl = (GET_KEYSTATE_WPARAM(wp) & MK_CONTROL) != 0;
      bool shift = (GET_KEYSTATE_WPARAM(wp) & MK_SHIFT) != 0;
      POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
      ScreenToClient(hwnd, &pt);
      if (ctrl) {
        float f = (delta > 0) ? 1.1f : 1.f / 1.1f;
        self->vp_.zoomToward(float(pt.x), float(pt.y), f);
      } else if (shift) {
        self->vp_.panByScreen(float(delta) * .5f, 0.f);
      } else {
        float steps = float(delta) / WHEEL_DELTA;
        self->vp_.panByScreen(0.f, -steps * 40.f);
      }
      self->scheduleRepaint();
      forwardToParent(hwnd, msg, wp, lp);
      return 0;
    }
    case WM_KEYDOWN: {
      if (!self)
        return 0;
      bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
      bool consumed = false;
      if (ctrl && self->viewportEnabled_) {
        if (wp == VK_OEM_PLUS || wp == VK_ADD) {
          self->vp_.zoomIn();
          self->scheduleRepaint();
          consumed = true;
        } else if (wp == VK_OEM_MINUS || wp == VK_SUBTRACT) {
          self->vp_.zoomOut();
          self->scheduleRepaint();
          consumed = true;
        } else if (wp == '0') {
          self->vp_.resetZoom();
          self->scheduleRepaint();
          consumed = true;
        }
      }
      if (!consumed && self->activeSurface_)
        self->activeSurface_->onKeyDown(int(wp));
      return 0;
    }
    case WM_KEYUP:
      if (self && self->activeSurface_)
        self->activeSurface_->onKeyUp(int(wp));
      return 0;
    default:
      return DefWindowProc(hwnd, msg, wp, lp);
    }
  }

  static void forwardToParent(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    HWND frame = GetParent(hwnd);
    HWND parent = frame ? GetParent(frame) : nullptr;
    if (!parent)
      return;
    POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
    MapWindowPoints(hwnd, parent, &pt, 1);
    PostMessage(parent, msg, wp, MAKELPARAM(pt.x, pt.y));
  }

  static void registerClasses() {
    static bool done = false;
    if (done)
      return;
    HINSTANCE hi = GetModuleHandle(nullptr);
    WNDCLASSEXW wf{};
    wf.cbSize = sizeof(wf);
    wf.lpfnWndProc = FrameProc;
    wf.hInstance = hi;
    wf.lpszClassName = L"FluxGLFrame6";
    wf.style = CS_HREDRAW | CS_VREDRAW;

    RegisterClassExW(&wf);
    WNDCLASSEXW wg{};
    wg.cbSize = sizeof(wg);
    wg.lpfnWndProc = GLProc;
    wg.hInstance = hi;
    wg.lpszClassName = L"FluxGLCanvas6";
    wg.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
    RegisterClassExW(&wg);
    done = true;
  }

  void ensureWindows(HDC parentDC) {
    if (frameHwnd_)
      return;
    HWND owner = WindowFromDC(parentDC);
    if (!owner)
      owner = GetActiveWindow();
    parentHwnd_ = owner;
    registerClasses();

    const DWORD sbStyles = viewportEnabled_ ? (WS_HSCROLL | WS_VSCROLL) : 0;
    frameHwnd_ = CreateWindowExW(
        WS_EX_NOPARENTNOTIFY, L"FluxGLFrame6", nullptr,
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | sbStyles, x,
        y, width, height, owner, nullptr, GetModuleHandle(nullptr), nullptr);
    assert(frameHwnd_);
    SetWindowLongPtrW(frameHwnd_, GWLP_USERDATA,
                      reinterpret_cast<LONG_PTR>(this));

    int gw, gh;
    glChildSize(width, height, gw, gh);
    glHwnd_ = CreateWindowExW(
        WS_EX_NOPARENTNOTIFY, L"FluxGLCanvas6", nullptr,
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN, 0, 0, gw, gh,
        frameHwnd_, nullptr, GetModuleHandle(nullptr), nullptr);
    assert(glHwnd_);
    SetWindowLongPtrW(glHwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

    glDC_ = GetDC(glHwnd_);
    setupPixelFormat(glDC_);
    HGLRC tmp = wglCreateContext(glDC_);
    wglMakeCurrent(glDC_, tmp);
    auto wglCA = reinterpret_cast<PFNWGLCREATECONTEXTATTRIBSARBPROC>(
        wglGetProcAddress("wglCreateContextAttribsARB"));
    if (wglCA) {
      const int att[] = {WGL_CONTEXT_MAJOR_VERSION_ARB,
                         3,
                         WGL_CONTEXT_MINOR_VERSION_ARB,
                         3,
                         WGL_CONTEXT_PROFILE_MASK_ARB,
                         WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
                         0};
      HGLRC core = wglCA(glDC_, nullptr, att);
      if (core) {
        wglMakeCurrent(nullptr, nullptr);
        wglDeleteContext(tmp);
        glRC_ = core;
      } else
        glRC_ = tmp;
    } else
      glRC_ = tmp;
    wglMakeCurrent(glDC_, glRC_);
    GL.init();

    vp_.init(gw, gh, canvasW_, canvasH_);
    lastGLW_ = gw;
    lastGLH_ = gh;
    activatePendingSurface();
    lastTick_ = Clock::now();
    if (onViewportChanged)
      onViewportChanged(vp_.zoom());
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
    activeSurface_->initialize(canvasW_, canvasH_);
    vp_.setCanvasSize(canvasW_, canvasH_);
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
    RECT rc;
    GetClientRect(glHwnd_, &rc);
    int glW = rc.right < 1 ? 1 : rc.right;
    int glH = rc.bottom < 1 ? 1 : rc.bottom;
    glViewport(0, 0, glW, glH);

    float mvp[16];
    vp_.buildMVP(mvp);
    activeSurface_->render(mvp);
    SwapBuffers(glDC_);
    syncScrollbars();
  }

  void moveWindows() {
    if (!frameHwnd_)
      return;
    SetWindowPos(frameHwnd_, nullptr, x, y, width, height,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    RECT rc;
    GetClientRect(frameHwnd_, &rc);
    int gw, gh;
    glChildSize(rc.right, rc.bottom, gw, gh);
    SetWindowPos(glHwnd_, nullptr, 0, 0, gw, gh,
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOMOVE);
    if (gw != lastGLW_ || gh != lastGLH_) {
      if (wglGetCurrentContext() != glRC_)
        wglMakeCurrent(glDC_, glRC_);
      vp_.setViewSize(gw, gh);
      glViewport(0, 0, gw, gh);
      lastGLW_ = gw;
      lastGLH_ = gh;
    }
  }

  void destroyGL() {
    if (glRC_) {
      wglMakeCurrent(glDC_, glRC_);
      if (activeSurface_) {
        activeSurface_->destroy();
        activeSurface_.reset();
      }
      pendingSurface_.reset();
      wglMakeCurrent(nullptr, nullptr);
      wglDeleteContext(glRC_);
      glRC_ = nullptr;
    }
    if (glHwnd_ && glDC_) {
      ReleaseDC(glHwnd_, glDC_);
      glDC_ = nullptr;
    }
    if (glHwnd_) {
      DestroyWindow(glHwnd_);
      glHwnd_ = nullptr;
    }
    if (frameHwnd_) {
      DestroyWindow(frameHwnd_);
      frameHwnd_ = nullptr;
    }
  }
};

// ============================================================================
// §11  FACTORY HELPERS
// ============================================================================

using CanvasPtr = std::shared_ptr<CanvasWidget>;

inline CanvasPtr Canvas() { return std::make_shared<CanvasWidget>(); }
inline CanvasPtr Canvas(int w, int h) {
  return std::make_shared<CanvasWidget>()->setSize(w, h);
}
inline CanvasPtr RasterCanvas(int w, int h) {
  auto c = std::make_shared<CanvasWidget>()->setSize(w, h);
  c->setCanvasSize(w, h);
  c->setViewportEnabled(false);
  c->setSurface<RasterSurface>();
  return c;
}
inline CanvasPtr RasterCanvas(int w, int h, size_t undoBudget) {
  auto c = std::make_shared<CanvasWidget>()->setSize(w, h);
  c->setCanvasSize(w, h);
  c->setSurface<RasterSurface>(undoBudget);
  return c;
}
inline CanvasPtr RasterCanvas(int viewW, int viewH, int canvasW, int canvasH) {
  auto c = std::make_shared<CanvasWidget>()->setSize(viewW, viewH);
  c->setCanvasSize(canvasW, canvasH);
  c->setSurface<RasterSurface>();
  return c;
}

#endif // FLUX_CANVAS_HPP