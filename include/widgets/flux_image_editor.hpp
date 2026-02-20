#ifndef FLUX_IMAGE_EDITOR_HPP
#define FLUX_IMAGE_EDITOR_HPP

#include "flux_state.hpp"
#include "flux_widget.hpp"

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <gl/GL.h>
#include <gl/GLU.h>
#include <windows.h>
#include <windowsx.h>
#ifndef GL_VERSION_2_0
typedef char GLchar;
#endif

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

// GL 1.3+ (multitexture)
#ifndef GL_TEXTURE0
#define GL_TEXTURE0 0x84C0
#endif

// GL 2.0
#ifndef GL_FRAGMENT_SHADER
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_VERTEX_SHADER 0x8B31
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_INFO_LOG_LENGTH 0x8B84
typedef char GLchar;
#endif

// GL 3.0 / ARB_framebuffer_object
#ifndef GL_FRAMEBUFFER
#define GL_FRAMEBUFFER 0x8D40
#define GL_COLOR_ATTACHMENT0 0x8CE0
#endif

#include <algorithm>
#include <cmath>
#include <functional>
#include <string>
#include <vector>

#pragma comment(lib, "opengl32.lib")
#pragma comment(lib, "glu32.lib")

// stb_image — drop stb_image.h next to this file
// In exactly ONE .cpp file define STB_IMAGE_IMPLEMENTATION before including
#include "stb_image.h"
#include "stb_image_write.h"

// ============================================================================
// PRECISION SWITCH
// Change these two lines to upgrade to 16-bit float precision:
//   kInternalFormat  GL_RGBA8        → GL_RGBA16F
//   kDataType        GL_UNSIGNED_BYTE → GL_FLOAT
// Everything else is format-agnostic.
// ============================================================================
static constexpr GLenum kInternalFormat = GL_RGBA8;
static constexpr GLenum kDataType = GL_UNSIGNED_BYTE;

// ============================================================================
// ADJUSTMENT PARAMS
// Plain data struct — serialisable, copyable, diffable.
// Each field maps 1-to-1 to a GLSL uniform.
// ============================================================================
struct ImageAdjustments {
  // ── Basic ────────────────────────────────────────────────────
  float exposure = 0.0f;   // EV:  -5 … +5
  float contrast = 0.0f;   // pct: -100 … +100
  float highlights = 0.0f; // pct: -100 … +100
  float shadows = 0.0f;    // pct: -100 … +100
  float whites = 0.0f;     // pct: -100 … +100
  float blacks = 0.0f;     // pct: -100 … +100

  // ── Presence ─────────────────────────────────────────────────
  float clarity = 0.0f;    // pct: -100 … +100  (local contrast)
  float saturation = 0.0f; // pct: -100 … +100
  float vibrance = 0.0f;   // pct: -100 … +100  (protects skin tones)

  // ── Color ────────────────────────────────────────────────────
  float temperature = 0.0f; // pct: -100 … +100  (cool → warm)
  float tint = 0.0f;        // pct: -100 … +100  (green → magenta)

  // ── Tone ─────────────────────────────────────────────────────
  float gamma = 1.0f; //       0.1 … 5.0

  // Reset everything to neutral
  void reset() { *this = ImageAdjustments{}; }
};

// ============================================================================
// GLSL SHADERS
// ============================================================================
namespace ImageShaders {

// ---- Vertex (shared by all passes) ----------------------------------------
static const char *kVert = R"GLSL(
#version 120
attribute vec2 aPos;
attribute vec2 aUV;
varying   vec2 vUV;
void main() {
    vUV         = aUV;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)GLSL";

// ---- Basic adjustments fragment -------------------------------------------
// Applies: exposure, contrast, highlights/shadows/whites/blacks,
//          saturation, vibrance, temperature, tint, gamma
static const char *kBasicFrag = R"GLSL(
#version 120
uniform sampler2D uTex;
uniform float uExposure;
uniform float uContrast;
uniform float uHighlights;
uniform float uShadows;
uniform float uWhites;
uniform float uBlacks;
uniform float uSaturation;
uniform float uVibrance;
uniform float uTemperature;
uniform float uTint;
uniform float uGamma;
varying vec2 vUV;

// ── helpers ─────────────────────────────────────────────────────────────────

vec3 linearToSRGB(vec3 c) {
    return pow(clamp(c, 0.0, 1.0), vec3(1.0 / 2.2));
}
vec3 sRGBToLinear(vec3 c) {
    return pow(clamp(c, 0.0, 1.0), vec3(2.2));
}

// luminance (perceived)
float luma(vec3 c) {
    return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

// RGB → HSL
vec3 rgb2hsl(vec3 c) {
    float maxC = max(c.r, max(c.g, c.b));
    float minC = min(c.r, min(c.g, c.b));
    float d    = maxC - minC;
    float l    = (maxC + minC) * 0.5;
    float s    = (l < 0.5) ? d / (maxC + minC) : d / (2.0 - maxC - minC);
    if (d == 0.0) return vec3(0.0, 0.0, l);
    float h;
    if      (maxC == c.r) h = (c.g - c.b) / d + (c.g < c.b ? 6.0 : 0.0);
    else if (maxC == c.g) h = (c.b - c.r) / d + 2.0;
    else                  h = (c.r - c.g) / d + 4.0;
    return vec3(h / 6.0, s, l);
}

float hue2rgb(float p, float q, float t) {
    if (t < 0.0) t += 1.0;
    if (t > 1.0) t -= 1.0;
    if (t < 1.0/6.0) return p + (q - p) * 6.0 * t;
    if (t < 0.5)      return q;
    if (t < 2.0/3.0) return p + (q - p) * (2.0/3.0 - t) * 6.0;
    return p;
}
vec3 hsl2rgb(vec3 hsl) {
    float h = hsl.x, s = hsl.y, l = hsl.z;
    if (s == 0.0) return vec3(l);
    float q = (l < 0.5) ? l * (1.0 + s) : l + s - l * s;
    float p = 2.0 * l - q;
    return vec3(hue2rgb(p,q,h+1.0/3.0),
                hue2rgb(p,q,h),
                hue2rgb(p,q,h-1.0/3.0));
}

// ── main ────────────────────────────────────────────────────────────────────
void main() {
    vec3 col = texture2D(uTex, vUV).rgb;

    // 1. sRGB → linear
    col = sRGBToLinear(col);

    // 2. Exposure  (EV shift = multiply by 2^EV)
    col *= pow(2.0, uExposure);

    // 3. Whites / Blacks (soft-clip top and bottom)
    float wb_w = uWhites    * 0.01;
    float wb_b = uBlacks    * 0.01;
    col = col + wb_w * (1.0 - col);     // whites pull toward 1
    col = col + wb_b * col;             // blacks push toward 0

    // 4. Highlights / Shadows (luminance-masked)
    float lum = luma(col);
    float highlightMask = smoothstep(0.5, 1.0, lum);
    float shadowMask    = smoothstep(0.5, 0.0, lum);
    col += uHighlights * 0.01 * highlightMask * (1.0 - col);
    col += uShadows    * 0.01 * shadowMask    * col;

    // 5. Contrast  (S-curve around 0.5 in linear)
    float cf = 1.0 + uContrast * 0.01;
    col = (col - 0.5) * cf + 0.5;

    // 6. Gamma
    col = pow(max(col, 0.0), vec3(1.0 / uGamma));

    // 7. linear → sRGB before colour ops
    col = linearToSRGB(col);

    // 8. Temperature / Tint (additive shift in RGB)
    float t = uTemperature * 0.005;
    float tn = uTint        * 0.005;
    col.r += t;
    col.b -= t;
    col.g += tn;

    // 9. Saturation
    vec3 hsl = rgb2hsl(clamp(col, 0.0, 1.0));
    hsl.y = clamp(hsl.y * (1.0 + uSaturation * 0.01), 0.0, 1.0);
    col = hsl2rgb(hsl);

    // 10. Vibrance (saturation that protects already-saturated / skin-tone pixels)
    hsl = rgb2hsl(clamp(col, 0.0, 1.0));
    float vibranceMask = (1.0 - hsl.y) * (1.0 - abs(hsl.x * 6.0 - 3.0) / 3.0);
    hsl.y = clamp(hsl.y + uVibrance * 0.01 * vibranceMask, 0.0, 1.0);
    col = hsl2rgb(hsl);

    gl_FragColor = vec4(clamp(col, 0.0, 1.0), 1.0);
}
)GLSL";

// ---- Clarity pass (unsharp-mask style local contrast) ---------------------
// Runs as a second pass over the basic-adjusted FBO texture.
static const char *kClarityFrag = R"GLSL(
#version 120
uniform sampler2D uTex;
uniform float     uClarity;
uniform vec2      uTexelSize;   // 1.0/vec2(width, height)
varying vec2 vUV;

// 3x3 box blur (cheap low-frequency approximation)
vec3 blur(sampler2D tex, vec2 uv, vec2 ts) {
    vec3 s = vec3(0.0);
    for (int x = -1; x <= 1; x++)
        for (int y = -1; y <= 1; y++)
            s += texture2D(tex, uv + vec2(float(x),float(y)) * ts).rgb;
    return s / 9.0;
}

void main() {
    vec3 sharp  = texture2D(uTex, vUV).rgb;
    vec3 blurry = blur(uTex, vUV, uTexelSize * 4.0);
    // high-frequency detail
    vec3 detail = sharp - blurry;
    vec3 result = sharp + detail * (uClarity * 0.02);
    gl_FragColor = vec4(clamp(result, 0.0, 1.0), 1.0);
}
)GLSL";

// ---- Passthrough (blit) ---------------------------------------------------
static const char *kBlitFrag = R"GLSL(
#version 120
uniform sampler2D uTex;
varying vec2 vUV;
void main() { gl_FragColor = texture2D(uTex, vUV); }
)GLSL";

} // namespace ImageShaders

// ============================================================================
// GL HELPERS  (self-contained, no external dependency)
// ============================================================================

// OpenGL 2.0 function pointers we need (not in gl.h on Windows)
typedef GLuint(APIENTRY *PFNGLCREATESHADERPROC)(GLenum);
typedef void(APIENTRY *PFNGLSHADERSOURCEPROC)(GLuint, GLsizei, const GLchar **,
                                              const GLint *);
typedef void(APIENTRY *PFNGLCOMPILESHADERPROC)(GLuint);
typedef GLuint(APIENTRY *PFNGLCREATEPROGRAMPROC)();
typedef void(APIENTRY *PFNGLATTACHSHADERPROC)(GLuint, GLuint);
typedef void(APIENTRY *PFNGLLINKPROGRAMPROC)(GLuint);
typedef void(APIENTRY *PFNGLUSEPROGRAMPROC)(GLuint);
typedef GLint(APIENTRY *PFNGLGETUNIFORMLOCATIONPROC)(GLuint, const GLchar *);
typedef void(APIENTRY *PFNGLUNIFORM1IPROC)(GLint, GLint);
typedef void(APIENTRY *PFNGLUNIFORM1FPROC)(GLint, GLfloat);
typedef void(APIENTRY *PFNGLUNIFORM2FPROC)(GLint, GLfloat, GLfloat);
typedef GLint(APIENTRY *PFNGLGETATTRIBLOCATIONPROC)(GLuint, const GLchar *);
typedef void(APIENTRY *PFNGLVERTEXATTRIBPOINTERPROC)(GLuint, GLint, GLenum,
                                                     GLboolean, GLsizei,
                                                     const void *);
typedef void(APIENTRY *PFNGLENABLEVERTEXATTRIBARRAYPROC)(GLuint);
typedef void(APIENTRY *PFNGLGENFRAMEBUFFERSPROC)(GLsizei, GLuint *);
typedef void(APIENTRY *PFNGLBINDFRAMEBUFFERPROC)(GLenum, GLuint);
typedef void(APIENTRY *PFNGLFRAMEBUFFERTEXTURE2DPROC)(GLenum, GLenum, GLenum,
                                                      GLuint, GLint);
typedef void(APIENTRY *PFNGLDELETEFRAMEBUFFERSPROC)(GLsizei, const GLuint *);
typedef void(APIENTRY *PFNGLDELETEPROGRAMPROC)(GLuint);
typedef void(APIENTRY *PFNGLDELETESHADERPROC)(GLuint);
typedef void(APIENTRY *PFNGLGETSHADERIVPROC)(GLuint, GLenum, GLint *);
typedef void(APIENTRY *PFNGLGETSHADERINFOLOGPROC)(GLuint, GLsizei, GLsizei *,
                                                  GLchar *);
typedef void(APIENTRY *PFNGLACTIVETEXTUREPROC)(GLenum);

struct GL2 {
  PFNGLCREATESHADERPROC CreateShader;
  PFNGLSHADERSOURCEPROC ShaderSource;
  PFNGLCOMPILESHADERPROC CompileShader;
  PFNGLCREATEPROGRAMPROC CreateProgram;
  PFNGLATTACHSHADERPROC AttachShader;
  PFNGLLINKPROGRAMPROC LinkProgram;
  PFNGLUSEPROGRAMPROC UseProgram;
  PFNGLGETUNIFORMLOCATIONPROC GetUniformLocation;
  PFNGLUNIFORM1IPROC Uniform1i;
  PFNGLUNIFORM1FPROC Uniform1f;
  PFNGLUNIFORM2FPROC Uniform2f;
  PFNGLGETATTRIBLOCATIONPROC GetAttribLocation;
  PFNGLVERTEXATTRIBPOINTERPROC VertexAttribPointer;
  PFNGLENABLEVERTEXATTRIBARRAYPROC EnableVertexAttribArray;
  PFNGLGENFRAMEBUFFERSPROC GenFramebuffers;
  PFNGLBINDFRAMEBUFFERPROC BindFramebuffer;
  PFNGLFRAMEBUFFERTEXTURE2DPROC FramebufferTexture2D;
  PFNGLDELETEFRAMEBUFFERSPROC DeleteFramebuffers;
  PFNGLDELETEPROGRAMPROC DeleteProgram;
  PFNGLDELETESHADERPROC DeleteShader;
  PFNGLGETSHADERIVPROC GetShaderiv;
  PFNGLGETSHADERINFOLOGPROC GetShaderInfoLog;
  PFNGLACTIVETEXTUREPROC ActiveTexture;

  bool load() {
    auto g = [](const char *n) { return wglGetProcAddress(n); };
    CreateShader = (PFNGLCREATESHADERPROC)g("glCreateShader");
    ShaderSource = (PFNGLSHADERSOURCEPROC)g("glShaderSource");
    CompileShader = (PFNGLCOMPILESHADERPROC)g("glCompileShader");
    CreateProgram = (PFNGLCREATEPROGRAMPROC)g("glCreateProgram");
    AttachShader = (PFNGLATTACHSHADERPROC)g("glAttachShader");
    LinkProgram = (PFNGLLINKPROGRAMPROC)g("glLinkProgram");
    UseProgram = (PFNGLUSEPROGRAMPROC)g("glUseProgram");
    GetUniformLocation = (PFNGLGETUNIFORMLOCATIONPROC)g("glGetUniformLocation");
    Uniform1i = (PFNGLUNIFORM1IPROC)g("glUniform1i");
    Uniform1f = (PFNGLUNIFORM1FPROC)g("glUniform1f");
    Uniform2f = (PFNGLUNIFORM2FPROC)g("glUniform2f");
    GetAttribLocation = (PFNGLGETATTRIBLOCATIONPROC)g("glGetAttribLocation");
    VertexAttribPointer =
        (PFNGLVERTEXATTRIBPOINTERPROC)g("glVertexAttribPointer");
    EnableVertexAttribArray =
        (PFNGLENABLEVERTEXATTRIBARRAYPROC)g("glEnableVertexAttribArray");
    GenFramebuffers = (PFNGLGENFRAMEBUFFERSPROC)g("glGenFramebuffers");
    BindFramebuffer = (PFNGLBINDFRAMEBUFFERPROC)g("glBindFramebuffer");
    FramebufferTexture2D =
        (PFNGLFRAMEBUFFERTEXTURE2DPROC)g("glFramebufferTexture2D");
    DeleteFramebuffers = (PFNGLDELETEFRAMEBUFFERSPROC)g("glDeleteFramebuffers");
    DeleteProgram = (PFNGLDELETEPROGRAMPROC)g("glDeleteProgram");
    DeleteShader = (PFNGLDELETESHADERPROC)g("glDeleteShader");
    GetShaderiv = (PFNGLGETSHADERIVPROC)g("glGetShaderiv");
    GetShaderInfoLog = (PFNGLGETSHADERINFOLOGPROC)g("glGetShaderInfoLog");
    ActiveTexture = (PFNGLACTIVETEXTUREPROC)g("glActiveTexture");
    return CreateShader != nullptr; // spot-check
  }
} gl2;

// ============================================================================
// SHADER PROGRAM HELPER
// ============================================================================
struct ShaderProgram {
  GLuint id = 0;

  bool build(const char *vert, const char *frag) {
    // compile vertex
    GLuint vs = gl2.CreateShader(GL_VERTEX_SHADER);
    gl2.ShaderSource(vs, 1, &vert, nullptr);
    gl2.CompileShader(vs);

    // compile fragment
    GLuint fs = gl2.CreateShader(GL_FRAGMENT_SHADER);
    gl2.ShaderSource(fs, 1, &frag, nullptr);
    gl2.CompileShader(fs);

#ifdef _DEBUG
    // Print compile errors in debug builds
    GLint ok = 0;
    gl2.GetShaderiv(fs, GL_COMPILE_STATUS, &ok);
    if (!ok) {
      char log[1024];
      gl2.GetShaderInfoLog(fs, 1024, nullptr, log);
      OutputDebugStringA(log);
    }
#endif

    id = gl2.CreateProgram();
    gl2.AttachShader(id, vs);
    gl2.AttachShader(id, fs);
    gl2.LinkProgram(id);
    gl2.DeleteShader(vs);
    gl2.DeleteShader(fs);
    return id != 0;
  }

  void use() const { gl2.UseProgram(id); }
  void destroy() {
    if (id) {
      gl2.DeleteProgram(id);
      id = 0;
    }
  }

  GLint uniform(const char *name) const {
    return gl2.GetUniformLocation(id, name);
  }
  void set(const char *name, float v) const { gl2.Uniform1f(uniform(name), v); }
  void set(const char *name, int v) const { gl2.Uniform1i(uniform(name), v); }
  void set(const char *name, float a, float b) const {
    gl2.Uniform2f(uniform(name), a, b);
  }
};

// ============================================================================
// FRAMEBUFFER HELPER
// ============================================================================
struct Framebuffer {
  GLuint fbo = 0;
  GLuint tex = 0;
  int w = 0, h = 0;

  void create(int width, int height) {
    w = width;
    h = height;

    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, kInternalFormat, w, h, 0, GL_RGBA, kDataType,
                 nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    gl2.GenFramebuffers(1, &fbo);
    gl2.BindFramebuffer(GL_FRAMEBUFFER, fbo);
    gl2.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                             GL_TEXTURE_2D, tex, 0);
    gl2.BindFramebuffer(GL_FRAMEBUFFER, 0);
  }

  void bind() const { gl2.BindFramebuffer(GL_FRAMEBUFFER, fbo); }
  void unbind() const { gl2.BindFramebuffer(GL_FRAMEBUFFER, 0); }

  void destroy() {
    if (fbo) {
      gl2.DeleteFramebuffers(1, &fbo);
      fbo = 0;
    }
    if (tex) {
      glDeleteTextures(1, &tex);
      tex = 0;
    }
  }
};

// ============================================================================
// EDITABLE IMAGE WIDGET
// ============================================================================
class EditableImageWidget : public Widget {
public:
  // ----------------------------------------------------------------
  // Public API
  // ----------------------------------------------------------------

  HDC getGLDC() const { return glDC; }
  HGLRC getGLRC() const { return glRC; }

  void exportToFile(const std::string &path) {
    wglMakeCurrent(glDC, glRC);
    exportAdjustedImage(fboClarity.tex, imgW, imgH, path);
  }

  // Load from file path
  std::shared_ptr<EditableImageWidget> loadImage(const std::string &path) {
    imagePath = path;
    imageLoaded = false;
    markNeedsPaint();
    return std::static_pointer_cast<EditableImageWidget>(shared_from_this());
  }

  // Bind an adjustments state — any field change triggers a re-render
  std::shared_ptr<EditableImageWidget>
  setAdjustments(State<ImageAdjustments> &state) {
    // Snapshot current value
    adj = state.get();

    state.bindProperty(
        shared_from_this(),
        [](Widget *w, const ImageAdjustments &a) {
          static_cast<EditableImageWidget *>(w)->adj = a;
          // markNeedsPaint called automatically by bindProperty
        },
        false); // paint only — no layout change

    return std::static_pointer_cast<EditableImageWidget>(shared_from_this());
  }

  // Direct set (without State, for quick testing)
  std::shared_ptr<EditableImageWidget>
  setAdjustments(const ImageAdjustments &a) {
    adj = a;
    markNeedsPaint();
    return std::static_pointer_cast<EditableImageWidget>(shared_from_this());
  }

  std::shared_ptr<EditableImageWidget> setSize(int w, int h) {
    width = w;
    height = h;
    autoWidth = autoHeight = false;
    markNeedsLayout();
    return std::static_pointer_cast<EditableImageWidget>(shared_from_this());
  }

  // Show original / adjusted toggle (e.g. hold backslash like Lightroom)
  std::shared_ptr<EditableImageWidget> setShowOriginal(bool v) {
    showOriginal = v;
    markNeedsPaint();
    return std::static_pointer_cast<EditableImageWidget>(shared_from_this());
  }

  // Fit modes
  enum class FitMode { Contain, Cover, Fill };
  std::shared_ptr<EditableImageWidget> setFitMode(FitMode m) {
    fitMode = m;
    markNeedsPaint();
    return std::static_pointer_cast<EditableImageWidget>(shared_from_this());
  }

  // Expose the post-adjustment texture so a HistogramWidget can read it
  GLuint getOutputTexture() const {
    return showOriginal ? srcTex : fboClarity.tex;
  }
  int imageWidth() const { return imgW; }
  int imageHeight() const { return imgH; }

  EditableImageWidget() {
    autoWidth = false;
    autoHeight = false;
    width = 600;
    height = 400;
  }

  ~EditableImageWidget() { destroyGL(); }

  // ----------------------------------------------------------------
  // Widget overrides
  // ----------------------------------------------------------------

  void computeLayout(HDC hdc, int availableWidth, int availableHeight,
                     FontCache &fontCache) override {
    // Respect parent's offered space as a hard ceiling
    if (autoWidth)
      width = availableWidth;
    else
      width = min(width, availableWidth); // never exceed what parent offers

    if (autoHeight)
      height = availableHeight;
    else
      height = min(height, availableHeight); // never exceed what parent offers

    applyConstraints();
    needsLayout = false;
  }

  void render(HDC hdc, FontCache &) override {
    ensureChildWindow(hdc);
    moveChildWindow();
    renderGL();
    needsPaint = false;
  }

  void onDetach() override {
    destroyGL();
    Widget::onDetach();
  }

private:
  // ── state ───────────────────────────────────────────────────────
  std::string imagePath;
  ImageAdjustments adj;
  bool showOriginal = false;
  FitMode fitMode = FitMode::Contain;

  // ── image data ──────────────────────────────────────────────────
  bool imageLoaded = false;
  GLuint srcTex = 0;
  int imgW = 0;
  int imgH = 0;

  // ── GL pipeline ─────────────────────────────────────────────────
  ShaderProgram basicProg;   // basic adjustments pass
  ShaderProgram clarityProg; // clarity / local contrast pass
  ShaderProgram blitProg;    // final blit to screen

  Framebuffer fboBasic;   // output of basic pass
  Framebuffer fboClarity; // output of clarity pass (= final output)

  bool glReady = false;

  // ── win32 / context ─────────────────────────────────────────────
  HWND childHwnd = nullptr;
  HDC glDC = nullptr;
  HGLRC glRC = nullptr;
  HWND parentHwnd = nullptr;

  // ── quad geometry (NDC fullscreen triangle pair) ─────────────────
  // pos(xy) + uv(xy)  — two triangles covering [-1,1]²
  static constexpr float kQuad[] = {
      // x      y     u     v
      -1.0f, -1.0f, 0.0f, 0.0f, 1.0f,  -1.0f, 1.0f, 0.0f,
      1.0f,  1.0f,  1.0f, 1.0f, -1.0f, -1.0f, 0.0f, 0.0f,
      1.0f,  1.0f,  1.0f, 1.0f, -1.0f, 1.0f,  0.0f, 1.0f,
  };

  // ----------------------------------------------------------------
  // Win32 child window (same pattern as GraphWidget)
  // ----------------------------------------------------------------
  static LRESULT CALLBACK ChildProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_ERASEBKGND)
      return 1;
    if (msg == WM_PAINT) {
      PAINTSTRUCT ps;
      BeginPaint(hwnd, &ps);
      EndPaint(hwnd, &ps);
      return 0;
    }
    if (msg == WM_LBUTTONDOWN || msg == WM_RBUTTONDOWN || msg == WM_MOUSEMOVE ||
        msg == WM_MOUSEWHEEL) {
      HWND parent = GetParent(hwnd);
      if (parent) {
        POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        MapWindowPoints(hwnd, parent, &pt, 1);
        PostMessage(parent, msg, wp, MAKELPARAM(pt.x, pt.y));
      }
    }
    return DefWindowProc(hwnd, msg, wp, lp);
  }

  void registerChildClass() {
    static bool done = false;
    if (done)
      return;
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = ChildProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = L"FluxGLImage";
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
        WS_EX_NOPARENTNOTIFY, L"FluxGLImage", nullptr,
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN, x, y, width,
        height, owner, nullptr, GetModuleHandle(nullptr), nullptr);

    if (!childHwnd)
      return;

    glDC = GetDC(childHwnd);
    setupPixelFormat(glDC);
    glRC = wglCreateContext(glDC);
    wglMakeCurrent(glDC, glRC);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Load GL2 function pointers & compile shaders once
    if (gl2.load())
      initPipeline();
  }

  void setupPixelFormat(HDC dc) {
    PIXELFORMATDESCRIPTOR pfd = {};
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    pfd.iLayerType = PFD_MAIN_PLANE;
    int fmt = ChoosePixelFormat(dc, &pfd);
    SetPixelFormat(dc, fmt, &pfd);
  }

  void moveChildWindow() {
    if (!childHwnd)
      return;
    SetWindowPos(childHwnd, nullptr, x, y, width, height,
                 SWP_NOZORDER | SWP_NOACTIVATE);
  }

  // ----------------------------------------------------------------
  // Pipeline init
  // ----------------------------------------------------------------
  void initPipeline() {
    basicProg.build(ImageShaders::kVert, ImageShaders::kBasicFrag);
    clarityProg.build(ImageShaders::kVert, ImageShaders::kClarityFrag);
    blitProg.build(ImageShaders::kVert, ImageShaders::kBlitFrag);
    glReady = true;
  }

  void exportAdjustedImage(GLuint fboTex, int imgW, int imgH,
                           const std::string &outPath) {
    // Bind the FBO texture to read from it
    GLuint fbo;
    gl2.GenFramebuffers(1, &fbo);
    gl2.BindFramebuffer(GL_FRAMEBUFFER, fbo);
    gl2.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                             GL_TEXTURE_2D, fboTex, 0);

    std::vector<unsigned char> pixels(imgW * imgH * 4);
    glReadPixels(0, 0, imgW, imgH, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

    gl2.BindFramebuffer(GL_FRAMEBUFFER, 0);
    gl2.DeleteFramebuffers(1, &fbo);

    // Flip vertically (GL origin is bottom-left)
    for (int y = 0; y < imgH / 2; y++) {
      int opposite = imgH - 1 - y;
      for (int x = 0; x < imgW * 4; x++)
        std::swap(pixels[y * imgW * 4 + x], pixels[opposite * imgW * 4 + x]);
    }

    // Save using stb_image_write
    stbi_write_png(outPath.c_str(), imgW, imgH, 4, pixels.data(), imgW * 4);
  }
  // ----------------------------------------------------------------
  // Image upload
  // ----------------------------------------------------------------
  void uploadImage() {
    if (imagePath.empty())
      return;

    // Free previous texture
    if (srcTex) {
      glDeleteTextures(1, &srcTex);
      srcTex = 0;
    }
    fboBasic.destroy();
    fboClarity.destroy();

    // Decode via stb_image
    int channels = 0;
    stbi_set_flip_vertically_on_load(true); // GL has origin at bottom-left
    unsigned char *pixels =
        stbi_load(imagePath.c_str(), &imgW, &imgH, &channels, 4);
    if (!pixels)
      return;

    // Upload source texture (always RGBA8 — this is the original, never
    // modified)
    glGenTextures(1, &srcTex);
    glBindTexture(GL_TEXTURE_2D, srcTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, imgW, imgH, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    stbi_image_free(pixels);

    // Create FBOs at image resolution (not widget size)
    // This means processing happens at full image quality regardless of display
    // size
    fboBasic.create(imgW, imgH);
    fboClarity.create(imgW, imgH);

    imageLoaded = true;
  }

  // ----------------------------------------------------------------
  // Draw a textured quad (used by every pass)
  // ----------------------------------------------------------------
  void drawQuad(const ShaderProgram &prog, GLuint tex, float x0, float y0,
                float x1, float y1) {
    // Build a quad for the given NDC rect
    float verts[] = {
        x0, y0, 0.0f, 0.0f, x1, y0, 1.0f, 0.0f, x1, y1, 1.0f, 1.0f,
        x0, y0, 0.0f, 0.0f, x1, y1, 1.0f, 1.0f, x0, y1, 0.0f, 1.0f,
    };

    prog.use();

    GLint aPos = gl2.GetAttribLocation(prog.id, "aPos");
    GLint aUV = gl2.GetAttribLocation(prog.id, "aUV");

    gl2.VertexAttribPointer(aPos, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                            verts);
    gl2.VertexAttribPointer(aUV, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                            verts + 2);
    gl2.EnableVertexAttribArray(aPos);
    gl2.EnableVertexAttribArray(aUV);

    gl2.ActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    prog.set("uTex", 0);

    glDrawArrays(GL_TRIANGLES, 0, 6);
  }

  // Full-screen quad version (NDC -1…1)
  void drawFullscreenQuad(const ShaderProgram &prog, GLuint tex) {
    drawQuad(prog, tex, -1.0f, -1.0f, 1.0f, 1.0f);
  }

  // ----------------------------------------------------------------
  // Compute the "contain" fit rect in NDC space
  // Returns {x0, y0, x1, y1} in [-1,1]²
  // ----------------------------------------------------------------
  void fitRect(float &x0, float &y0, float &x1, float &y1) const {
    float imgAspect = (float)imgW / (float)imgH;
    float widgetAspect = (float)width / (float)height;
    float scaleX = 1.0f, scaleY = 1.0f;

    if (fitMode == FitMode::Contain) {
      if (imgAspect > widgetAspect)
        scaleY = widgetAspect / imgAspect;
      else
        scaleX = imgAspect / widgetAspect;
    }
    // Cover and Fill just use 1.0

    x0 = -scaleX;
    y0 = -scaleY;
    x1 = scaleX;
    y1 = scaleY;
  }

  // ----------------------------------------------------------------
  // Render
  // ----------------------------------------------------------------
  void renderGL() {
    if (!glRC || !glDC)
      return;
    if (wglGetCurrentContext() != glRC)
      wglMakeCurrent(glDC, glRC);

    // Upload image on first render (or when path changes)
    if (!imageLoaded && !imagePath.empty())
      uploadImage();

    glViewport(0, 0, width, height);
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    if (!imageLoaded || !glReady) {
      SwapBuffers(glDC);
      return;
    }

    if (showOriginal) {
      // ── Bypass all adjustments, blit original directly ──────
      float x0, y0, x1, y1;
      fitRect(x0, y0, x1, y1);
      glViewport(0, 0, width, height);
      drawQuad(blitProg, srcTex, x0, y0, x1, y1);
    } else {
      // ── Pass 1: Basic adjustments → fboBasic ────────────────
      fboBasic.bind();
      glViewport(0, 0, imgW, imgH);
      glClear(GL_COLOR_BUFFER_BIT);

      basicProg.use();
      basicProg.set("uExposure", adj.exposure);
      basicProg.set("uContrast", adj.contrast);
      basicProg.set("uHighlights", adj.highlights);
      basicProg.set("uShadows", adj.shadows);
      basicProg.set("uWhites", adj.whites);
      basicProg.set("uBlacks", adj.blacks);
      basicProg.set("uSaturation", adj.saturation);
      basicProg.set("uVibrance", adj.vibrance);
      basicProg.set("uTemperature", adj.temperature);
      basicProg.set("uTint", adj.tint);
      basicProg.set("uGamma", adj.gamma);
      drawFullscreenQuad(basicProg, srcTex);

      fboBasic.unbind();

      // ── Pass 2: Clarity → fboClarity ────────────────────────
      fboClarity.bind();
      glViewport(0, 0, imgW, imgH);
      glClear(GL_COLOR_BUFFER_BIT);

      clarityProg.use();
      clarityProg.set("uClarity", adj.clarity);
      clarityProg.set("uTexelSize", 1.0f / (float)imgW, 1.0f / (float)imgH);
      drawFullscreenQuad(clarityProg, fboBasic.tex);

      fboClarity.unbind();

      // ── Pass 3: Blit final FBO to screen (with fit) ──────────
      glViewport(0, 0, width, height);
      glClear(GL_COLOR_BUFFER_BIT);
      float x0, y0, x1, y1;
      fitRect(x0, y0, x1, y1);
      drawQuad(blitProg, fboClarity.tex, x0, y0, x1, y1);
    }

    SwapBuffers(glDC);
  }

  // ----------------------------------------------------------------
  // Cleanup
  // ----------------------------------------------------------------
  void destroyGL() {
    if (glRC) {
      wglMakeCurrent(glDC, glRC);
      basicProg.destroy();
      clarityProg.destroy();
      blitProg.destroy();
      fboBasic.destroy();
      fboClarity.destroy();
      if (srcTex) {
        glDeleteTextures(1, &srcTex);
        srcTex = 0;
      }
      wglMakeCurrent(nullptr, nullptr);
      wglDeleteContext(glRC);
      glRC = nullptr;
    }
    if (childHwnd && glDC) {
      ReleaseDC(childHwnd, glDC);
      glDC = nullptr;
    }
    if (childHwnd) {
      DestroyWindow(childHwnd);
      childHwnd = nullptr;
    }
  }
};

// ============================================================================
// FACTORY
// ============================================================================
using EditableImagePtr = std::shared_ptr<EditableImageWidget>;

inline EditableImagePtr EditableImage() {
  return std::make_shared<EditableImageWidget>();
}
inline EditableImagePtr EditableImage(int w, int h) {
  return std::make_shared<EditableImageWidget>()->setSize(w, h);
}
inline EditableImagePtr EditableImage(const std::string &path, int w, int h) {
  return std::make_shared<EditableImageWidget>()->setSize(w, h)->loadImage(
      path);
}

#endif // FLUX_IMAGE_EDITOR_HPP

/*
// ============================================================================
// USAGE EXAMPLE
// ============================================================================

class PhotoEditorComponent : public Component {
    State<ImageAdjustments> adj{ {}, context };
    State<bool> showOriginal{ false, context };

    WidgetPtr build() override {

        auto image = EditableImage("photo.jpg", 800, 600)
            ->setAdjustments(adj)
            ->setFitMode(EditableImageWidget::FitMode::Contain);

        // Bind showOriginal to the backslash key externally:
        //   showOriginal.set(true/false) → blit toggles automatically

        auto panel = Column(
            // Exposure
            Row(Text("Exposure")->setWidth(100),
                Slider(-5.0, 5.0, 0.01)
                    ->setOnValueChanged([&](double v){
                        auto a = adj.get(); a.exposure = v; adj.set(a);
                    })),
            // Contrast
            Row(Text("Contrast")->setWidth(100),
                Slider(-100.0, 100.0, 1.0)
                    ->setOnValueChanged([&](double v){
                        auto a = adj.get(); a.contrast = v; adj.set(a);
                    })),
            // Highlights
            Row(Text("Highlights")->setWidth(100),
                Slider(-100.0, 100.0, 1.0)
                    ->setOnValueChanged([&](double v){
                        auto a = adj.get(); a.highlights = v; adj.set(a);
                    })),
            // ... same pattern for all other fields ...

            Button(Text("Reset"), [&]{ adj.set({}); })
        )->setSpacing(12);

        return Scaffold(
            AppBar("Photo Editor"),
            Row(image, panel->setWidth(280))
        );
    }
};
*/