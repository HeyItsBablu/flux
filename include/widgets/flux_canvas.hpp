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
// Step 4  (previous)
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
// Step 5  (previous)
// ───────────────────────
//   TEXT RENDERING PRIMITIVES added to RasterSurface:
//
//   • TextStyle struct (§5) — font face, size, color (0–1 floats), bold,
//     italic, underline.
//
//   • rasterizeTextGDI()  [private]
//     Single GDI offscreen DIB rasterizer shared by both public helpers.
//     Converts GL canvas-space Y-up coords to GDI top-down, renders with
//     ClearType into a full-canvas DIB, extracts a tight RGBA bounding box,
//     and returns the pixel data plus the bounding rect.  White pixels
//     (all channels >= 250) are made transparent so text composites cleanly
//     over painted strokes.
//
//   • renderTextToScratch()  [protected]
//     Calls rasterizeTextGDI and uploads the result to the scratch texture
//     via glTexSubImage2D.  Flips rows for GL Y-up.  Caller manages
//     scratchClear() timing.  showCursor=true appends a '|' caret for live
//     preview — this is the only cursor concern inside flux_canvas; blink
//     timing remains the subclass's responsibility.
//
//   • commitTextToCanvas()  [protected]
//     Pushes an undo snapshot, then calls rasterizeTextGDI (no cursor),
//     reads the committed buffer, CPU-composites text pixels over it with
//     SRC_OVER blending, and uploads the result back.  No GL FBO ops are
//     needed for the composite — the read-modify-write is done in RAM to
//     avoid any blend-state entanglement.
//
//   • gdiplusToken_  [private member]
//     GDI+ lifetime now owned by RasterSurface.  initialize() starts it,
//     destroy() shuts it down.  Subclasses must NOT call GdiplusStartup /
//     GdiplusShutdown themselves.
//
// Step 6  (previous)
// ───────────────────────
//   • setScrollbarsEnabled(bool) added to CanvasWidget — hides/shows
//     scrollbars at any time and resizes the GL child to fill freed space.
//
//   • onGLResize callback added to CanvasWidget — fires whenever the GL
//     child window is resized (distinct from canvas/image resize).
//     Used by ImageEditSurface to track the GL window size independently
//     of the canvas/image dimensions that come through resize().
//
// Step 7  (this revision)
// ───────────────────────
//   CUSTOM GL SCROLLBARS replacing Win32 OS scrollbars entirely.
//
//   • CustomScrollbar (§6b) — self-contained scrollbar renderer/tracker.
//     Drawn as OpenGL quads in screen-space NDC inside the GL child window.
//     Features:
//       - Thin minimal style (12 px track, 6 px thumb width when idle,
//         8 px when hovered/dragging).
//       - Smooth alpha fade: opaque on hover/drag → fades to kIdleAlpha
//         (0.15) after kIdleDelay (1.5 s) of inactivity.
//       - Arrow buttons at both ends with triangle glyphs.
//       - Rounded-rect thumb via GL_TRIANGLE_FAN circle caps.
//       - Hit-test: arrow, track-before, thumb, track-after zones.
//       - Drag: captures relative delta from thumb-center on mousedown.
//
//   • CanvasWidget changes:
//       - Frame window no longer has WS_HSCROLL | WS_VSCROLL.
//       - kSBThick (12 px) reserved on right/bottom edges of GL child.
//         Viewport view-size = (glW − kSBThick, glH − kSBThick) when
//         both bars are visible, shrinking accordingly when only one or
//         neither is shown.
//       - syncScrollbars() feeds CustomScrollbar::setThumb() instead of
//         Win32 SetScrollInfo().
//       - GLProc routes WM_LBUTTONDOWN/MOVE/UP to scrollbar hit-test
//         before forwarding to the surface.
//       - tickAndRender() calls CustomScrollbar::tick(dt) and
//         CustomScrollbar::render() after the surface render.
//       - needsContinuousRedraw() returns true while any bar is fading.
//
// Step 8  (this revision)
// ───────────────────────
//   RasterSurface has been moved to its own header:
//     flux_raster.hpp
//   Include it after flux_canvas.hpp when RasterSurface is needed.
//   The factory helpers (RasterCanvas) remain here and still pull in
//   flux_raster.hpp automatically via the include below.
//
// ============================================================================

#include "flux_widget.hpp"

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>

// GDI+ for PNG save + text rasterization
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
// §5  STROKE + TEXT DATA TYPES
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

// ── TextStyle ────────────────────────────────────────────────────────────────
struct TextStyle {
  std::wstring fontFace = L"Arial";
  int fontSize = 20;         // canvas pixels
  float r = 0, g = 0, b = 0; // text color, 0–1
  bool bold = false;
  bool italic = false;
  bool underline = false;
};

// ============================================================================
// §6  SCROLLBAR INFO  (used by Viewport; fed into CustomScrollbar)
// ============================================================================

struct ScrollbarInfo {
  float thumbMin = 0.f;
  float thumbMax = 1.f;
  bool visible = false;
};

// ============================================================================
// §6b  CUSTOM GL SCROLLBAR
// ============================================================================
//
// Draws a thin macOS/VS-Code-style scrollbar directly in the GL child window
// using screen-space quads.  No Win32 SCROLLBAR control is involved.
//
// Coordinate system: screen pixels, origin top-left (matches WM_MOUSEMOVE).
// The caller (CanvasWidget) must:
//   1. Call setGeometry() whenever the GL window is resized.
//   2. Call setThumb() every frame from Viewport::scrollbarH/V().
//   3. Call tick(dt) every frame (drives fade animation).
//   4. Call render(prog, vao, vbo, uMVP, uColor, glW, glH) after the
//      surface render, with blend already enabled.
//   5. Forward WM_LBUTTONDOWN/MOVE/UP to onMouseDown/Move/Up.
//   6. Forward WM_MOUSEMOVE (even without button) to onMouseMove for hover.
//   7. Forward WM_MOUSELEAVE to onMouseLeave.
//
// Returns needsRedraw() == true while alpha is still animating so the caller
// can schedule a continuous repaint.

class CustomScrollbar {
public:
  // ── Tuning constants ──────────────────────────────────────────────────────
  static constexpr float kTrackThick    = 12.f; // px — full track strip width
  static constexpr float kThumbThin     =  5.f; // px — thumb width at rest
  static constexpr float kThumbFat      =  8.f; // px — thumb width on hover/drag
  static constexpr float kArrowSize     = 12.f; // px — square arrow button
  static constexpr float kThumbMinLen   = 24.f; // px — minimum thumb length
  static constexpr float kCornerR       =  3.f; // px — thumb cap radius
  static constexpr int   kCapSegs       = 8;    // semicircle segments per cap
  static constexpr float kIdleAlpha     = 0.15f;// alpha when content overflows
  static constexpr float kActiveAlpha   = 0.90f;// alpha when hovered/dragging
  static constexpr float kIdleDelay     = 1.5f; // seconds before fade starts
  static constexpr float kFadeSpeed     = 3.5f; // alpha units per second
  static constexpr float kExpandSpeed   = 10.f; // thickness lerp speed (1/s)

  enum class Axis { Horizontal, Vertical };

  // Hit zones returned by hitTest()
  enum class Zone { None, ArrowStart, TrackBefore, Thumb, TrackAfter, ArrowEnd };

  explicit CustomScrollbar(Axis axis) : axis_(axis) {}

  // Called when GL window resizes.  x0,y0 = top-left of track strip in
  // screen pixels; trackLen = usable length excluding the two arrow buttons.
  void setGeometry(float stripX0, float stripY0, float stripLen) {
    stripX0_  = stripX0;
    stripY0_  = stripY0;
    stripLen_ = stripLen;
  }

  // thumbMin/Max in [0,1].  visible = whether content overflows.
  void setThumb(float thumbMin, float thumbMax, bool visible) {
    visible_   = visible;
    thumbMin_  = std::clamp(thumbMin, 0.f, 1.f);
    thumbMax_  = std::clamp(thumbMax, 0.f, 1.f);
    if (thumbMin_ > thumbMax_) std::swap(thumbMin_, thumbMax_);
  }

  // dt in seconds.  Returns true while animation is still running.
  bool tick(double dt) {
    float f = float(dt);

    // ── Thumb width lerp (idle ↔ hovered/dragging) ────────────────────────
    float targetW = (hovered_ || dragging_) ? kThumbFat : kThumbThin;
    currentW_ += (targetW - currentW_) * min(1.f, kExpandSpeed * f);

    // ── Alpha animation ───────────────────────────────────────────────────
    if (!visible_) {
      // No overflow — hide completely
      alpha_ += (0.f - alpha_) * min(1.f, kFadeSpeed * f);
      idleTimer_ = 0.f;
      return std::abs(alpha_) > 0.005f;
    }

    float targetAlpha;
    if (hovered_ || dragging_) {
      targetAlpha = kActiveAlpha;
      idleTimer_  = 0.f;
    } else {
      idleTimer_ += f;
      if (idleTimer_ < kIdleDelay)
        targetAlpha = kActiveAlpha; // stay bright immediately after activity
      else
        targetAlpha = kIdleAlpha;
    }
    float prev = alpha_;
    alpha_ += (targetAlpha - alpha_) * min(1.f, kFadeSpeed * f);
    return std::abs(alpha_ - prev) > 0.002f;
  }

  bool needsRedraw() const { return visible_ && alpha_ > 0.005f; }
  bool isVisible()   const { return visible_; }

  // ── Render ────────────────────────────────────────────────────────────────
  // prog    : the flat-color shader (uMode=1 path of the blit shader)
  // vao/vbo : shared geometry buffers (already large enough)
  // uMVP, uColor : uniform locations in prog
  // glW, glH : current GL window dimensions (screen pixels)
  void render(GLuint prog, GLuint vao, GLuint vbo,
              GLint uMVP, GLint uColor,
              int glW, int glH) const {
    if (alpha_ < 0.005f) return;

    // Build screen-space ortho: x in [0,glW], y in [0,glH] top-down.
    // We flip Y so that pixel y=0 is the top of the window.
    float mvp[16];
    glutil::ortho(0.f, float(glW), float(glH), 0.f, mvp);

    GL.useProgram(prog);
    GL.uniformMatrix4fv(uMVP, 1, GL_FALSE, mvp);
    GL.uniform1i(GL.getUniformLocation(prog, "uMode"), 1);

    // ── track background ─────────────────────────────────────────────────
    {
      float ta = alpha_ * 0.25f;
      GL.uniform4f(uColor, 0.12f, 0.12f, 0.12f, ta);
      pushRect(vao, vbo, stripX0_, stripY0_, trackStripW(), stripLen_);
    }

    // ── arrow buttons ────────────────────────────────────────────────────
    {
      float aa = alpha_;
      GL.uniform4f(uColor, 0.75f, 0.75f, 0.75f, aa);
      drawArrow(vao, vbo, true,  glW, glH, prog, uMVP, uColor);
      drawArrow(vao, vbo, false, glW, glH, prog, uMVP, uColor);
    }

    // ── thumb ─────────────────────────────────────────────────────────────
    if (thumbMax_ > thumbMin_) {
      float thumbAlpha = dragging_ ? 1.f : alpha_;
      float tc = dragging_ ? 0.92f : 0.72f;
      GL.uniform4f(uColor, tc, tc, tc, thumbAlpha);
      drawThumb(vao, vbo);
    }
  }

  // ── Mouse input ───────────────────────────────────────────────────────────

  // Returns true if the event was consumed by the scrollbar.
  bool onMouseDown(int sx, int sy, std::function<void(float)> scrollTo) {
    Zone z = hitTest(sx, sy);
    if (z == Zone::None) return false;

    hovered_    = true;
    idleTimer_  = 0.f;
    scrollToFn_ = scrollTo;

    if (z == Zone::Thumb) {
      dragging_     = true;
      dragStartPx_  = axis_ == Axis::Horizontal ? float(sx) : float(sy);
      dragStartMin_ = thumbMin_;
      return true;
    }
    if (z == Zone::ArrowStart) {
      scrollBy(-kArrowStep, scrollTo);
      return true;
    }
    if (z == Zone::ArrowEnd) {
      scrollBy(+kArrowStep, scrollTo);
      return true;
    }
    if (z == Zone::TrackBefore) {
      scrollBy(-(thumbMax_ - thumbMin_), scrollTo);
      return true;
    }
    if (z == Zone::TrackAfter) {
      scrollBy(+(thumbMax_ - thumbMin_), scrollTo);
      return true;
    }
    return false;
  }

  // Returns true if dragging (caller should suppress surface mouse move).
  bool onMouseMove(int sx, int sy) {
    Zone z = hitTest(sx, sy);
    bool wasHovered = hovered_;
    hovered_ = (z != Zone::None);
    if (hovered_ != wasHovered) idleTimer_ = 0.f;

    if (dragging_ && scrollToFn_) {
      float pos  = axis_ == Axis::Horizontal ? float(sx) : float(sy);
      float delta = pos - dragStartPx_;
      float usable = usableLen();
      float thumbPx = (thumbMax_ - thumbMin_) * usable;
      float range   = usable - thumbPx;
      if (range > 0.f) {
        float newMin = std::clamp(dragStartMin_ + delta / range, 0.f, 1.f - (thumbMax_ - thumbMin_));
        scrollToFn_(newMin);
      }
      return true;
    }
    return false;
  }

  bool onMouseUp(int /*sx*/, int /*sy*/) {
    bool was = dragging_;
    dragging_ = false;
    idleTimer_ = 0.f;
    return was;
  }

  void onMouseLeave() {
    hovered_  = false;
    dragging_ = false;
  }

  // Wakes the bar (resets idle timer) — call on any viewport change.
  void poke() { idleTimer_ = 0.f; }

private:
  Axis  axis_;
  float stripX0_ = 0, stripY0_ = 0, stripLen_ = 100.f;
  float thumbMin_ = 0.f, thumbMax_ = 1.f;
  bool  visible_  = false;
  bool  hovered_  = false;
  bool  dragging_ = false;
  float alpha_    = kIdleAlpha;
  float idleTimer_= 0.f;
  float currentW_ = kThumbThin;
  float dragStartPx_  = 0.f;
  float dragStartMin_ = 0.f;

  std::function<void(float)> scrollToFn_;

  static constexpr float kArrowStep = 0.05f; // fraction per arrow click

  // Total width of the track strip (cross-axis dimension)
  float trackStripW() const { return kTrackThick; }

  // Usable track length (minus two arrow buttons)
  float usableLen() const { return max(1.f, stripLen_ - kArrowSize * 2.f); }

  // Thumb position along the track axis (pixels from track start, after arrow)
  void thumbPixels(float &pxStart, float &pxLen) const {
    float ul = usableLen();
    pxLen  = max(kThumbMinLen, (thumbMax_ - thumbMin_) * ul);
    pxStart = kArrowSize + thumbMin_ * (ul - pxLen + (thumbMax_ - thumbMin_) * ul - pxLen);
    // Simpler: map thumbMin to [0, ul - pxLen]
    float range = ul - pxLen;
    pxStart = kArrowSize + thumbMin_ / (1.f - (thumbMax_ - thumbMin_)) * range;
    if (!std::isfinite(pxStart)) pxStart = kArrowSize;
    pxStart = std::clamp(pxStart, kArrowSize, kArrowSize + range);
  }

  Zone hitTest(int sx, int sy) const {
    if (!visible_) return Zone::None;
    float px = axis_ == Axis::Horizontal ? float(sx) : float(sy);
    float py = axis_ == Axis::Horizontal ? float(sy) : float(sx);
    float trackCross = axis_ == Axis::Horizontal ? stripY0_ : stripX0_;

    // Must be within the cross-axis strip
    if (py < trackCross || py >= trackCross + kTrackThick) return Zone::None;
    float along = axis_ == Axis::Horizontal
                  ? px - stripX0_
                  : px - stripY0_;
    if (along < 0 || along > stripLen_) return Zone::None;

    if (along < kArrowSize)                   return Zone::ArrowStart;
    if (along > stripLen_ - kArrowSize)       return Zone::ArrowEnd;

    float ts, tl;
    thumbPixels(ts, tl);
    if (along < ts)         return Zone::TrackBefore;
    if (along < ts + tl)    return Zone::Thumb;
    return Zone::TrackAfter;
  }

  void scrollBy(float delta, std::function<void(float)> fn) {
    if (!fn) return;
    float span   = thumbMax_ - thumbMin_;
    float newMin = std::clamp(thumbMin_ + delta, 0.f, 1.f - span);
    fn(newMin);
  }

  // ── geometry helpers ──────────────────────────────────────────────────────

  // Push a screen-space quad [x,y,w,h] using the shared vbo.
  // The quad is axis-aligned in the top-left-origin screen coord system.
  static void pushRect(GLuint vao, GLuint vbo,
                       float x, float y, float w, float h) {
    float v[] = {
      x,     y,
      x + w, y,
      x + w, y + h,
      x + w, y + h,
      x,     y + h,
      x,     y,
    };
    GL.bindVertexArray(vao);
    GL.bindBuffer(GL_ARRAY_BUFFER, vbo);
    GL.bufferData(GL_ARRAY_BUFFER, sizeof(v), v, GL_DYNAMIC_DRAW);
    GL.enableVertexAttribArray(0);
    GL.vertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(float)*2, nullptr);
    GL.disableVertexAttribArray(1);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    GL.bindVertexArray(0);
  }

  // Rounded-rectangle thumb drawn as a central rect + two circular caps.
  void drawThumb(GLuint vao, GLuint vbo) const {
    float ts, tl;
    thumbPixels(ts, tl);

    // Thumb rect in track-local coords, then map to screen
    float halfW = currentW_ * 0.5f;
    float crossCenter = (axis_ == Axis::Horizontal)
                        ? stripY0_ + kTrackThick * 0.5f
                        : stripX0_ + kTrackThick * 0.5f;

    // Build rounded rect verts
    // We'll approximate with a central rect + semicircle fans at each end.
    static const int kSegs = kCapSegs;
    const float cr = min(kCornerR, halfW); // cap radius = half-thickness

    // Central rect (excluding cap zones)
    float rLen = tl - cr * 2.f;
    if (rLen > 0.f) {
      float rx, ry, rw, rh;
      if (axis_ == Axis::Horizontal) {
        rx = stripX0_ + ts + cr;
        ry = crossCenter - halfW;
        rw = rLen; rh = currentW_;
      } else {
        rx = crossCenter - halfW;
        ry = stripY0_ + ts + cr;
        rw = currentW_; rh = rLen;
      }
      pushRect(vao, vbo, rx, ry, rw, rh);
    }

    // Two semicircle caps
    for (int cap = 0; cap < 2; ++cap) {
      float capAlong = (cap == 0) ? (stripX0_ + ts + cr)
                                  : (stripX0_ + ts + tl - cr);
      float capCross = crossCenter;
      if (axis_ == Axis::Vertical) {
        capAlong = (cap == 0) ? (stripY0_ + ts + cr)
                              : (stripY0_ + ts + tl - cr);
        capCross = crossCenter;
      }

      float startAngle = (axis_ == Axis::Horizontal)
                         ? (cap == 0 ? 3.14159265f * 0.5f  : -3.14159265f * 0.5f)
                         : (cap == 0 ? 3.14159265f          : 0.f);
      float sweep = 3.14159265f;

      // Fan: center + kSegs+1 rim verts
      std::vector<float> fan;
      fan.reserve((kSegs + 2) * 2);
      if (axis_ == Axis::Horizontal) {
        fan.push_back(capAlong); fan.push_back(capCross);
      } else {
        fan.push_back(capCross); fan.push_back(capAlong);
      }
      for (int i = 0; i <= kSegs; ++i) {
        float a = startAngle + sweep * float(i) / kSegs;
        float dx = cosf(a) * cr;
        float dy = sinf(a) * cr;
        if (axis_ == Axis::Horizontal) {
          fan.push_back(capAlong + dx);
          fan.push_back(capCross + dy);
        } else {
          fan.push_back(capCross + dx);
          fan.push_back(capAlong + dy);
        }
      }
      GL.bindVertexArray(vao);
      GL.bindBuffer(GL_ARRAY_BUFFER, vbo);
      GL.bufferData(GL_ARRAY_BUFFER,
                    GLsizeiptr(fan.size() * sizeof(float)),
                    fan.data(), GL_DYNAMIC_DRAW);
      GL.enableVertexAttribArray(0);
      GL.vertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(float)*2, nullptr);
      GL.disableVertexAttribArray(1);
      glDrawArrays(GL_TRIANGLE_FAN, 0, int(fan.size() / 2));
      GL.bindVertexArray(0);
    }
  }

  // Arrow button: a small triangle glyph centered in a kArrowSize square.
  void drawArrow(GLuint vao, GLuint vbo,
                 bool isStart, int /*glW*/, int /*glH*/,
                 GLuint /*prog*/, GLint /*uMVP*/, GLint /*uColor*/) const {
    // Arrow button background (subtle)
    float ax, ay;
    if (axis_ == Axis::Horizontal) {
      ax = isStart ? stripX0_ : stripX0_ + stripLen_ - kArrowSize;
      ay = stripY0_;
    } else {
      ax = stripX0_;
      ay = isStart ? stripY0_ : stripY0_ + stripLen_ - kArrowSize;
    }
    pushRect(vao, vbo, ax, ay, kArrowSize,
             axis_ == Axis::Horizontal ? kTrackThick : kArrowSize);

    // Triangle glyph (3 verts)
    float cx = ax + kArrowSize * 0.5f;
    float cy = ay + kTrackThick * 0.5f;
    if (axis_ == Axis::Vertical)
      cy = ay + kArrowSize * 0.5f;
    float hs = 3.f; // half-size of the triangle

    float v[6];
    if (axis_ == Axis::Horizontal) {
      if (isStart) { // ◀
        v[0]=cx+hs; v[1]=cy-hs;
        v[2]=cx-hs; v[3]=cy;
        v[4]=cx+hs; v[5]=cy+hs;
      } else {       // ▶
        v[0]=cx-hs; v[1]=cy-hs;
        v[2]=cx+hs; v[3]=cy;
        v[4]=cx-hs; v[5]=cy+hs;
      }
    } else {
      if (isStart) { // ▲
        v[0]=cx-hs; v[1]=cy+hs;
        v[2]=cx;    v[3]=cy-hs;
        v[4]=cx+hs; v[5]=cy+hs;
      } else {       // ▼
        v[0]=cx-hs; v[1]=cy-hs;
        v[2]=cx;    v[3]=cy+hs;
        v[4]=cx+hs; v[5]=cy-hs;
      }
    }
    GL.bindVertexArray(vao);
    GL.bindBuffer(GL_ARRAY_BUFFER, vbo);
    GL.bufferData(GL_ARRAY_BUFFER, sizeof(v), v, GL_DYNAMIC_DRAW);
    GL.enableVertexAttribArray(0);
    GL.vertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(float)*2, nullptr);
    GL.disableVertexAttribArray(1);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    GL.bindVertexArray(0);
  }
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

    constexpr float kPanSlack = 0.5f;
    float slackX = viewW * kPanSlack;
    float slackY = viewH * kPanSlack;

    if (viewW >= cw_) {
      offsetX_ = std::clamp(offsetX_, -slackX, cw_ - viewW + slackX);
    } else {
      offsetX_ = std::clamp(offsetX_, 0.f, cw_ - viewW);
    }

    if (viewH >= ch_) {
      offsetY_ = std::clamp(offsetY_, -slackY, ch_ - viewH + slackY);
    } else {
      offsetY_ = std::clamp(offsetY_, 0.f, ch_ - viewH);
    }
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
  virtual bool needsContinuousRedraw() const { return false; }
  virtual void destroy() = 0;
};

// ============================================================================
// §9  RASTER SURFACE  —  see flux_raster.hpp
// ============================================================================

#include "flux_raster.hpp"

// ============================================================================
// §10  CANVAS WIDGET  (two-window hierarchy + custom GL scrollbars)
// ============================================================================

class CanvasWidget : public Widget {
public:
  // Thickness of the custom scrollbar strip in pixels (screen-space).
  // The viewport sees (glW - kSBThick) x (glH - kSBThick) as its view area
  // when both bars are present, adjusted when only one or neither is visible.
  static constexpr float kSBThick = CustomScrollbar::kTrackThick;

  explicit CanvasWidget()
      : hBar_(CustomScrollbar::Axis::Horizontal)
      , vBar_(CustomScrollbar::Axis::Vertical)
  {
    autoWidth = autoHeight = false;
    width = 400;
    height = 300;
  }
  ~CanvasWidget() { destroyGL(); }

  std::shared_ptr<CanvasWidget> setViewportEnabled(bool enabled) {
    viewportEnabled_ = enabled;
    return ptr();
  }

  // Show or hide the custom scrollbars.  Can be toggled at any time.
  std::shared_ptr<CanvasWidget> setScrollbarsEnabled(bool enabled) {
    scrollbarsEnabled_ = enabled;
    if (glHwnd_) {
      RECT rc;
      GetClientRect(glHwnd_, &rc);
      updateViewportSize(rc.right, rc.bottom);
      scheduleRepaint();
    }
    return ptr();
  }

  bool scrollbarsEnabled() const { return scrollbarsEnabled_; }

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
    if (glHwnd_) {
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

  // ── Callbacks ─────────────────────────────────────────────────────────────
  std::function<void(float zoom)> onViewportChanged;
  std::function<void(int w, int h)> onGLResize;

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
  bool viewportEnabled_   = true;
  bool scrollbarsEnabled_ = true;

  std::shared_ptr<CanvasWidget> ptr() {
    return std::static_pointer_cast<CanvasWidget>(shared_from_this());
  }

  std::shared_ptr<RenderSurface> activeSurface_, pendingSurface_;
  Viewport vp_;
  int canvasW_ = 512, canvasH_ = 512;

  // ── Custom scrollbars ─────────────────────────────────────────────────────
  CustomScrollbar hBar_;  // horizontal
  CustomScrollbar vBar_;  // vertical

  // Dedicated VAO/VBO for scrollbar geometry (avoids touching the surface's
  // shared quadVBO_ which has a fixed pre-allocated size for dab/blit quads).
  GLuint sbVAO_ = 0, sbVBO_ = 0;
  GLuint sbProg_ = 0;
  GLint  sbUMVP_ = -1, sbUColor_ = -1, sbUMode_ = -1;

  using Clock = std::chrono::steady_clock;
  Clock::time_point lastTick_ = Clock::now();

  HWND frameHwnd_ = nullptr;
  HWND glHwnd_    = nullptr;
  HDC  glDC_      = nullptr;
  HGLRC glRC_     = nullptr;
  HWND parentHwnd_ = nullptr;

  bool repaintPending_  = false;
  bool trackingLeave_   = false;
  int  lastGLW_ = 0, lastGLH_ = 0;

  bool  panning_     = false;
  int   panStartSX_  = 0, panStartSY_ = 0;
  float panStartOX_  = 0, panStartOY_ = 0;

  // ── Viewport helpers ──────────────────────────────────────────────────────

  // Compute the effective viewport dimensions accounting for scrollbar strips.
  void viewportDims(int glW, int glH, int &vpW, int &vpH) const {
    if (!viewportEnabled_ || !scrollbarsEnabled_) {
      vpW = glW; vpH = glH;
      return;
    }
    ScrollbarInfo h = vp_.scrollbarH();
    ScrollbarInfo v = vp_.scrollbarV();
    vpW = glW - (v.visible ? (int)kSBThick : 0);
    vpH = glH - (h.visible ? (int)kSBThick : 0);
    if (vpW < 1) vpW = 1;
    if (vpH < 1) vpH = 1;
  }

  void updateViewportSize(int glW, int glH) {
    int vpW, vpH;
    viewportDims(glW, glH, vpW, vpH);
    vp_.setViewSize(vpW, vpH);
  }

  // Recompute and push geometry to both bars based on current glW/glH.
  void updateSBGeometry(int glW, int glH) {
    // Horizontal bar: bottom strip, full width minus vertical bar
    ScrollbarInfo h = vp_.scrollbarH();
    ScrollbarInfo v = vp_.scrollbarV();
    float hLen = float(glW) - (v.visible ? kSBThick : 0.f);
    hBar_.setGeometry(0.f, float(glH) - kSBThick, hLen);
    hBar_.setThumb(h.thumbMin, h.thumbMax, h.visible && scrollbarsEnabled_ && viewportEnabled_);

    // Vertical bar: right strip, full height minus horizontal bar
    float vLen = float(glH) - (h.visible ? kSBThick : 0.f);
    vBar_.setGeometry(float(glW) - kSBThick, 0.f, vLen);
    vBar_.setThumb(v.thumbMin, v.thumbMax, v.visible && scrollbarsEnabled_ && viewportEnabled_);
  }

  // ── Pan helpers ───────────────────────────────────────────────────────────

  void beginPan(int sx, int sy) {
    panning_    = true;
    panStartSX_ = sx;
    panStartSY_ = sy;
    panStartOX_ = vp_.offsetX();
    panStartOY_ = vp_.offsetY();
  }

  void continuePan(int sx, int sy) {
    float dx = float(sx - panStartSX_), dy = float(sy - panStartSY_);
    vp_.setOffset(panStartOX_ - dx / vp_.zoom(), panStartOY_ + dy / vp_.zoom());
    pokeScrollbars();
    scheduleRepaint();
  }

  void pokeScrollbars() {
    hBar_.poke();
    vBar_.poke();
  }

  void scheduleRepaint() {
    if (onViewportChanged)
      onViewportChanged(vp_.zoom());
    if (!repaintPending_) {
      repaintPending_ = true;
      PostMessage(glHwnd_, WM_USER + 1, 0, 0);
    }
  }

  // ── Scrollbar scroll callbacks ────────────────────────────────────────────

  void applyHScrollFraction(float thumbMin) {
    float span  = vp_.scrollbarH().thumbMax - vp_.scrollbarH().thumbMin;
    float range = vp_.canvasW() - vp_.viewW() / vp_.zoom();
    if (range <= 0.f) return;
    vp_.setOffsetX(thumbMin / (1.f - span) * range);
    pokeScrollbars();
    scheduleRepaint();
  }

  void applyVScrollFraction(float thumbMin) {
    float span  = vp_.scrollbarV().thumbMax - vp_.scrollbarV().thumbMin;
    float range = vp_.canvasH() - vp_.viewH() / vp_.zoom();
    if (range <= 0.f) return;
    // Vertical scrollbar: thumbMin=0 → top (max offsetY), thumbMin=1-span → bottom (offsetY=0)
    float t = 1.f - thumbMin - span;
    vp_.setOffsetY(t / (1.f - span) * range);
    pokeScrollbars();
    scheduleRepaint();
  }

  // ── Scrollbar GL resources ────────────────────────────────────────────────

  void buildSBResources() {
    // Reuse the same flat-color shader as RasterSurface but compiled fresh
    // for this widget so it owns its program handle independently.
    const char *vert = R"GLSL(
#version 330 core
layout(location=0) in vec2 aPos;
uniform mat4 uMVP;
void main(){ gl_Position = uMVP * vec4(aPos, 0.0, 1.0); }
)GLSL";
    const char *frag = R"GLSL(
#version 330 core
uniform vec4 uColor;
uniform int  uMode;
out vec4 fragColor;
void main(){ fragColor = uColor; }
)GLSL";
    sbProg_   = glutil::linkProgram(vert, frag);
    assert(sbProg_);
    sbUMVP_   = GL.getUniformLocation(sbProg_, "uMVP");
    sbUColor_ = GL.getUniformLocation(sbProg_, "uColor");
    sbUMode_  = GL.getUniformLocation(sbProg_, "uMode");

    // Large enough for any single draw call the scrollbar emits
    // (fan caps: (kCapSegs+2)*2 floats is the largest single upload)
    constexpr GLsizeiptr kSBBufSz = sizeof(float) * 2 * 64;
    GL.genVertexArrays(1, &sbVAO_);
    GL.genBuffers(1, &sbVBO_);
    GL.bindVertexArray(sbVAO_);
    GL.bindBuffer(GL_ARRAY_BUFFER, sbVBO_);
    GL.bufferData(GL_ARRAY_BUFFER, kSBBufSz, nullptr, GL_DYNAMIC_DRAW);
    GL.enableVertexAttribArray(0);
    GL.vertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(float)*2, nullptr);
    GL.bindVertexArray(0);
  }

  void destroySBResources() {
    if (sbProg_) { GL.deleteProgram(sbProg_); sbProg_ = 0; }
    if (sbVAO_)  { GL.deleteVertexArrays(1, &sbVAO_); sbVAO_ = 0; }
    if (sbVBO_)  { GL.deleteBuffers(1, &sbVBO_); sbVBO_ = 0; }
  }

  // ── Frame window proc ────────────────────────────────────────────────────
  // Frame window is now a plain clip container — no WS_HSCROLL/WS_VSCROLL.

  static LRESULT CALLBACK FrameProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto *self = reinterpret_cast<CanvasWidget *>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
      PAINTSTRUCT ps;
      BeginPaint(hwnd, &ps);
      EndPaint(hwnd, &ps);
      return 0;
    }
    case WM_MOUSEWHEEL:
      // Forward wheel events to GL child
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

    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
      PAINTSTRUCT ps;
      BeginPaint(hwnd, &ps);
      EndPaint(hwnd, &ps);
      return 0;
    }

    // ── Middle-button pan ────────────────────────────────────────────────
    case WM_MBUTTONDOWN:
      if (!self || !self->viewportEnabled_) return 0;
      SetCapture(hwnd);
      self->beginPan(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
      return 0;
    case WM_MBUTTONUP:
      if (!self || !self->viewportEnabled_) return 0;
      ReleaseCapture();
      self->panning_ = false;
      return 0;

    // ── Left button ──────────────────────────────────────────────────────
    case WM_LBUTTONDOWN: {
      SetCapture(hwnd);
      SetFocus(hwnd);
      if (!self) return 0;
      int sx = GET_X_LPARAM(lp), sy = GET_Y_LPARAM(lp);

      // Offer to scrollbars first
      bool hConsumed = self->hBar_.onMouseDown(sx, sy,
          [self](float t){ self->applyHScrollFraction(t); });
      bool vConsumed = !hConsumed && self->vBar_.onMouseDown(sx, sy,
          [self](float t){ self->applyVScrollFraction(t); });

      if (!hConsumed && !vConsumed) {
        if (self->viewportEnabled_ && (GetKeyState(VK_SPACE) & 0x8000)) {
          self->beginPan(sx, sy);
        } else if (self->activeSurface_) {
          auto [cx, cy] = self->vp_.screenToCanvas(float(sx), float(sy));
          self->activeSurface_->onMouseDown(cx, cy);
        }
        forwardToParent(hwnd, msg, wp, lp);
      }
      self->scheduleRepaint();
      return 0;
    }
    case WM_LBUTTONUP: {
      ReleaseCapture();
      if (!self) return 0;
      int sx = GET_X_LPARAM(lp), sy = GET_Y_LPARAM(lp);
      bool hRel = self->hBar_.onMouseUp(sx, sy);
      bool vRel = self->vBar_.onMouseUp(sx, sy);
      if (!hRel && !vRel) {
        if (self->panning_) {
          self->panning_ = false;
        } else if (self->activeSurface_) {
          auto [cx, cy] = self->vp_.screenToCanvas(float(sx), float(sy));
          self->activeSurface_->onMouseUp(cx, cy);
        }
        forwardToParent(hwnd, msg, wp, lp);
      }
      self->scheduleRepaint();
      return 0;
    }

    // ── Right button ─────────────────────────────────────────────────────
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

    // ── Mouse move ───────────────────────────────────────────────────────
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

      // Always update hover state on scrollbars (no button required)
      bool hDrag = self->hBar_.onMouseMove(sx, sy);
      bool vDrag = self->vBar_.onMouseMove(sx, sy);
      self->scheduleRepaint(); // re-draw hover highlight

      if (hDrag || vDrag) return 0; // scrollbar drag suppresses canvas

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
      if (self) {
        self->trackingLeave_ = false;
        self->hBar_.onMouseLeave();
        self->vBar_.onMouseLeave();
        self->scheduleRepaint();
      }
      return 0;

    // ── Scroll wheel ─────────────────────────────────────────────────────
    case WM_MOUSEWHEEL: {
      if (!self || !self->viewportEnabled_) return 0;
      int   delta = GET_WHEEL_DELTA_WPARAM(wp);
      bool  ctrl  = (GET_KEYSTATE_WPARAM(wp) & MK_CONTROL) != 0;
      bool  shift = (GET_KEYSTATE_WPARAM(wp) & MK_SHIFT) != 0;
      POINT pt    = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
      ScreenToClient(hwnd, &pt);
      if (ctrl) {
        float f = (delta > 0) ? 1.1f : 1.f / 1.1f;
        self->vp_.zoomToward(float(pt.x), float(pt.y), f);
      } else if (shift) {
        self->vp_.panByScreen(float(delta) * .5f, 0.f);
      } else {
        self->vp_.panByScreen(0.f, -(float(delta) / WHEEL_DELTA) * 40.f);
      }
      self->pokeScrollbars();
      self->scheduleRepaint();
      forwardToParent(hwnd, msg, wp, lp);
      return 0;
    }

    // ── Keyboard ─────────────────────────────────────────────────────────
    case WM_KEYDOWN: {
      if (!self) return 0;
      bool ctrl     = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
      bool consumed = false;
      if (ctrl && self->viewportEnabled_) {
        if (wp == VK_OEM_PLUS || wp == VK_ADD) {
          self->vp_.zoomIn(); self->pokeScrollbars(); self->scheduleRepaint();
          consumed = true;
        } else if (wp == VK_OEM_MINUS || wp == VK_SUBTRACT) {
          self->vp_.zoomOut(); self->pokeScrollbars(); self->scheduleRepaint();
          consumed = true;
        } else if (wp == '0') {
          self->vp_.resetZoom(); self->pokeScrollbars(); self->scheduleRepaint();
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

    case WM_TIMER:
      KillTimer(hwnd, 1);
      if (self) {
        self->repaintPending_ = false;
        self->tickAndRender();
      }
      return 0;

    default:
      return DefWindowProc(hwnd, msg, wp, lp);
    }
  }

  static void forwardToParent(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    HWND frame  = GetParent(hwnd);
    HWND parent = frame ? GetParent(frame) : nullptr;
    if (!parent) return;
    POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
    MapWindowPoints(hwnd, parent, &pt, 1);
    PostMessage(parent, msg, wp, MAKELPARAM(pt.x, pt.y));
  }

  static void registerClasses() {
    static bool done = false;
    if (done) return;
    HINSTANCE hi = GetModuleHandle(nullptr);

    WNDCLASSEXW wf{};
    wf.cbSize        = sizeof(wf);
    wf.lpfnWndProc   = FrameProc;
    wf.hInstance     = hi;
    wf.lpszClassName = L"FluxGLFrame7";   // bumped class name for Step 7
    wf.style         = CS_HREDRAW | CS_VREDRAW;
    RegisterClassExW(&wf);

    WNDCLASSEXW wg{};
    wg.cbSize        = sizeof(wg);
    wg.lpfnWndProc   = GLProc;
    wg.hInstance     = hi;
    wg.lpszClassName = L"FluxGLCanvas7";
    wg.style         = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
    RegisterClassExW(&wg);

    done = true;
  }

  void ensureWindows(HDC parentDC) {
    if (frameHwnd_) return;
    HWND owner = WindowFromDC(parentDC);
    if (!owner) owner = GetActiveWindow();
    parentHwnd_ = owner;
    registerClasses();

    // Frame window: plain child, no scrollbar styles
    frameHwnd_ = CreateWindowExW(
        WS_EX_NOPARENTNOTIFY, L"FluxGLFrame7", nullptr,
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
        x, y, width, height, owner, nullptr, GetModuleHandle(nullptr), nullptr);
    assert(frameHwnd_);
    SetWindowLongPtrW(frameHwnd_, GWLP_USERDATA,
                      reinterpret_cast<LONG_PTR>(this));

    // GL child fills the entire frame (scrollbars are drawn inside it)
    glHwnd_ = CreateWindowExW(
        WS_EX_NOPARENTNOTIFY, L"FluxGLCanvas7", nullptr,
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
        0, 0, width, height, frameHwnd_, nullptr, GetModuleHandle(nullptr), nullptr);
    assert(glHwnd_);
    SetWindowLongPtrW(glHwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

    glDC_ = GetDC(glHwnd_);
    setupPixelFormat(glDC_);
    HGLRC tmp = wglCreateContext(glDC_);
    wglMakeCurrent(glDC_, tmp);
    auto wglCA = reinterpret_cast<PFNWGLCREATECONTEXTATTRIBSARBPROC>(
        wglGetProcAddress("wglCreateContextAttribsARB"));
    if (wglCA) {
      const int att[] = {WGL_CONTEXT_MAJOR_VERSION_ARB, 3,
                         WGL_CONTEXT_MINOR_VERSION_ARB, 3,
                         WGL_CONTEXT_PROFILE_MASK_ARB,
                         WGL_CONTEXT_CORE_PROFILE_BIT_ARB, 0};
      HGLRC core = wglCA(glDC_, nullptr, att);
      if (core) {
        wglMakeCurrent(nullptr, nullptr);
        wglDeleteContext(tmp);
        glRC_ = core;
      } else {
        glRC_ = tmp;
      }
    } else {
      glRC_ = tmp;
    }
    wglMakeCurrent(glDC_, glRC_);
    GL.init();

    // Initialise viewport — scrollbars not yet known so use full size
    vp_.init(width, height, canvasW_, canvasH_);
    lastGLW_ = width;
    lastGLH_ = height;
    updateViewportSize(width, height);
    updateSBGeometry(width, height);

    buildSBResources();
    activatePendingSurface();
    lastTick_ = Clock::now();
    if (onViewportChanged) onViewportChanged(vp_.zoom());
    if (onGLResize) onGLResize(width, height);
  }

  void setupPixelFormat(HDC dc) {
    PIXELFORMATDESCRIPTOR pfd{};
    pfd.nSize        = sizeof(pfd);
    pfd.nVersion     = 1;
    pfd.dwFlags      = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType   = PFD_TYPE_RGBA;
    pfd.cColorBits   = 32;
    pfd.cDepthBits   = 24;
    pfd.iLayerType   = PFD_MAIN_PLANE;
    SetPixelFormat(dc, ChoosePixelFormat(dc, &pfd), &pfd);
  }

  void activatePendingSurface() {
    if (!pendingSurface_) return;
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
    if (!glRC_ || !glDC_) return;
    if (pendingSurface_) activatePendingSurface();
    if (!activeSurface_) return;
    if (wglGetCurrentContext() != glRC_)
      wglMakeCurrent(glDC_, glRC_);

    auto   now = Clock::now();
    double dt  = std::chrono::duration<double>(now - lastTick_).count();
    lastTick_  = now;

    activeSurface_->update(dt);

    RECT rc;
    GetClientRect(glHwnd_, &rc);
    int glW = rc.right  < 1 ? 1 : rc.right;
    int glH = rc.bottom < 1 ? 1 : rc.bottom;
    glViewport(0, 0, glW, glH);

    float mvp[16];
    vp_.buildMVP(mvp);
    activeSurface_->render(mvp);

    // ── Tick and render custom scrollbars ─────────────────────────────────
    updateSBGeometry(glW, glH);
    bool hFade = hBar_.tick(dt);
    bool vFade = vBar_.tick(dt);

    if (hBar_.needsRedraw() || vBar_.needsRedraw()) {
      glEnable(GL_BLEND);
      glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
      glDisable(GL_SCISSOR_TEST);

      // Render directly to the default framebuffer (screen) in screen-space.
      GL.bindFramebuffer(GL_FRAMEBUFFER, 0);
      glViewport(0, 0, glW, glH);

      hBar_.render(sbProg_, sbVAO_, sbVBO_, sbUMVP_, sbUColor_, glW, glH);
      vBar_.render(sbProg_, sbVAO_, sbVBO_, sbUMVP_, sbUColor_, glW, glH);

      // Corner square where both bars meet
      if (hBar_.isVisible() && vBar_.isVisible() && scrollbarsEnabled_) {
        float mvpCorner[16];
        glutil::ortho(0.f, float(glW), float(glH), 0.f, mvpCorner);
        GL.useProgram(sbProg_);
        GL.uniformMatrix4fv(sbUMVP_, 1, GL_FALSE, mvpCorner);
        GL.uniform4f(sbUColor_, 0.12f, 0.12f, 0.12f, 0.35f);
        float cx = float(glW) - kSBThick, cy = float(glH) - kSBThick;
        float cv[] = {
          cx,              cy,
          cx + kSBThick,   cy,
          cx + kSBThick,   cy + kSBThick,
          cx + kSBThick,   cy + kSBThick,
          cx,              cy + kSBThick,
          cx,              cy,
        };
        GL.bindVertexArray(sbVAO_);
        GL.bindBuffer(GL_ARRAY_BUFFER, sbVBO_);
        GL.bufferData(GL_ARRAY_BUFFER, sizeof(cv), cv, GL_DYNAMIC_DRAW);
        GL.enableVertexAttribArray(0);
        GL.vertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(float)*2, nullptr);
        GL.disableVertexAttribArray(1);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        GL.bindVertexArray(0);
      }

      glDisable(GL_BLEND);
    }

    SwapBuffers(glDC_);

    // Keep animating while bars are fading
    bool needsCont = activeSurface_->needsContinuousRedraw()
                     || hFade || vFade;
    if (needsCont && !repaintPending_) {
      repaintPending_ = true;
      SetTimer(glHwnd_, 1, 16, nullptr);
    }
  }

  void moveWindows() {
    if (!frameHwnd_) return;
    SetWindowPos(frameHwnd_, nullptr, x, y, width, height,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    // GL child fills the entire frame — scrollbars live inside it
    SetWindowPos(glHwnd_, nullptr, 0, 0, width, height,
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOMOVE);

    if (width != lastGLW_ || height != lastGLH_) {
      if (wglGetCurrentContext() != glRC_)
        wglMakeCurrent(glDC_, glRC_);
      lastGLW_ = width;
      lastGLH_ = height;
      updateViewportSize(width, height);
      glViewport(0, 0, width, height);
      if (onGLResize) onGLResize(width, height);
    }
  }

  void destroyGL() {
    if (glRC_) {
      wglMakeCurrent(glDC_, glRC_);
      destroySBResources();
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