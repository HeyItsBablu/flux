
#ifndef FLUX_GRAPH_HPP
#define FLUX_GRAPH_HPP

#include "flux_core.hpp"
#include "flux_state.hpp"
#include "flux_widget.hpp"

#include <iostream>
#include <algorithm>
#include <cmath>
#include <functional>
#include <string>
#include <vector>


// glad must come before any other GL headers
#include <glad/glad.h>


// ============================================================================
// GRAPH SERIES
// ============================================================================

struct GraphSeries {
  std::string label;
  std::vector<float> values;
  float r = 0.2f, g = 0.6f, b = 1.0f;
  float lineWidth = 2.0f;
};

// ============================================================================
// GRAPH TYPES
// ============================================================================

enum class GraphType { Line, Bar, Area };

// ============================================================================
// OPENGL GRAPH WIDGET  —  GL 4.6 core profile
// ============================================================================

class GraphWidget : public Widget {
public:
  std::vector<GraphSeries> series;
  GraphType graphType = GraphType::Line;

  std::vector<std::string> xLabels;
  std::string xAxisTitle;
  std::string yAxisTitle;
  std::string title;

  bool autoRange = true;
  float yMin = 0.0f, yMax = 1.0f;

  bool showGrid = true;
  bool showLegend = true;
  COLORREF bgColor = RGB(20, 20, 30);

  GraphWidget() {
    autoWidth = false;
    autoHeight = false;
    width = 400;
    height = 300;
  }
  ~GraphWidget() { destroyGL(); }

  // ── Fluent API ─────────────────────────────────────────────────────────────

  std::shared_ptr<GraphWidget> addSeries(const std::string &label,
                                         const std::vector<float> &vals,
                                         float r = 0.2f, float g = 0.6f,
                                         float b = 1.0f) {
    GraphSeries s;
    s.label = label;
    s.values = vals;
    s.r = r;
    s.g = g;
    s.b = b;
    series.push_back(s);
    markNeedsPaint();
    return std::static_pointer_cast<GraphWidget>(shared_from_this());
  }

  std::shared_ptr<GraphWidget> addSeries(const std::string &label,
                                         State<std::vector<float>> &state,
                                         float r = 0.2f, float g = 0.6f,
                                         float b = 1.0f) {
    GraphSeries s;
    s.label = label;
    s.values = state.get();
    s.r = r;
    s.g = g;
    s.b = b;
    int idx = (int)series.size();
    series.push_back(s);
    state.bindProperty(
        shared_from_this(),
        [idx](Widget *w, const std::vector<float> &vals) {
          auto *self = static_cast<GraphWidget *>(w);
          if (idx < (int)self->series.size())
            self->series[idx].values = vals;
        },
        false);
    markNeedsPaint();
    return std::static_pointer_cast<GraphWidget>(shared_from_this());
  }

  std::shared_ptr<GraphWidget> bindSeries(int idx,
                                          State<std::vector<float>> &state) {
    while ((int)series.size() <= idx)
      series.push_back({});
    series[idx].values = state.get();
    state.bindProperty(
        shared_from_this(),
        [idx](Widget *w, const std::vector<float> &vals) {
          auto *self = static_cast<GraphWidget *>(w);
          if (idx < (int)self->series.size())
            self->series[idx].values = vals;
        },
        false);
    markNeedsPaint();
    return std::static_pointer_cast<GraphWidget>(shared_from_this());
  }

  std::shared_ptr<GraphWidget> setType(GraphType t) {
    graphType = t;
    markNeedsPaint();
    return std::static_pointer_cast<GraphWidget>(shared_from_this());
  }
  std::shared_ptr<GraphWidget> setTitle(const std::string &t) {
    title = t;
    markNeedsPaint();
    return std::static_pointer_cast<GraphWidget>(shared_from_this());
  }
  std::shared_ptr<GraphWidget> setXLabels(const std::vector<std::string> &lbs) {
    xLabels = lbs;
    markNeedsPaint();
    return std::static_pointer_cast<GraphWidget>(shared_from_this());
  }
  std::shared_ptr<GraphWidget> setYRange(float mn, float mx) {
    yMin = mn;
    yMax = mx;
    autoRange = false;
    markNeedsPaint();
    return std::static_pointer_cast<GraphWidget>(shared_from_this());
  }
  std::shared_ptr<GraphWidget> setShowGrid(bool v) {
    showGrid = v;
    markNeedsPaint();
    return std::static_pointer_cast<GraphWidget>(shared_from_this());
  }
  std::shared_ptr<GraphWidget> clearSeries() {
    series.clear();
    markNeedsPaint();
    return std::static_pointer_cast<GraphWidget>(shared_from_this());
  }

  std::shared_ptr<GraphWidget> setSize(int w, int h) {
    width = w;
    height = h;
    autoWidth = false;
    autoHeight = false;
    markNeedsLayout();
    return std::static_pointer_cast<GraphWidget>(shared_from_this());
  }

  // ── Widget overrides ───────────────────────────────────────────────────────

  void computeLayout(GraphicsContext &/*ctx*/, const BoxConstraints &constraints,
                     FontCache & /*fontCache*/) override {
    width = constraints.clampWidth(autoWidth ? constraints.maxWidth : width);
    height =
        constraints.clampHeight(autoHeight ? constraints.maxHeight : height);
    applyConstraints();
    needsLayout = false;
  }

  void render(GraphicsContext &ctx, FontCache & /*fontCache*/) override {
    ensureChildWindow(ctx);
    moveChildWindow();
    renderGL();
    needsPaint = false;
  }

  void onDetach() override {
    destroyGL();
    Widget::onDetach();
  }

private:
  // ── GL handles ────────────────────────────────────────────────────────────
  HWND childHwnd = nullptr;
  HDC glDC = nullptr;
  HGLRC glRC = nullptr;
  HWND parentHwnd = nullptr;

  // Geometry program (colored triangles / lines)
  GLuint geomProg_ = 0;
  GLuint geomVAO_ = 0;
  GLuint geomVBO_ = 0;
  GLint uGeomMVP_ = -1;
  GLint uGeomCol_ = -1;

  // Texture program (for GDI+ text labels)
  GLuint texProg_ = 0;
  GLuint texVAO_ = 0;
  GLuint texVBO_ = 0;
  GLint uTexMVP_ = -1;

  bool glResourcesBuilt_ = false;

  // ── Text texture (single-frame, freed after draw) ─────────────────────────
  struct TextTexture {
    GLuint id = 0;
    int width = 0, height = 0;
  };

  // ── Shaders ───────────────────────────────────────────────────────────────
  static GLuint compileShader(GLenum type, const char *src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
      char buf[512];
      glGetShaderInfoLog(s, 512, nullptr, buf);
      OutputDebugStringA(buf);
      glDeleteShader(s);
      return 0;
    }
    return s;
  }

  static GLuint linkProgram(const char *vs, const char *fs) {
    GLuint v = compileShader(GL_VERTEX_SHADER, vs);
    GLuint f = compileShader(GL_FRAGMENT_SHADER, fs);
    if (!v || !f) {
      if (v)
        glDeleteShader(v);
      if (f)
        glDeleteShader(f);
      return 0;
    }
    GLuint p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);
    glDeleteShader(v);
    glDeleteShader(f);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
      char buf[512];
      glGetProgramInfoLog(p, 512, nullptr, buf);
      OutputDebugStringA(buf);
      glDeleteProgram(p);
      return 0;
    }
    return p;
  }

  void buildGLResources() {
    if (glResourcesBuilt_)
      return;

    // ── Geometry shader (draws lines + filled triangles) ──────────────────
    const char *geomVS = R"GLSL(
#version 460 core
layout(location=0) in vec2 aPos;
uniform mat4 uMVP;
void main(){ gl_Position = uMVP * vec4(aPos, 0.0, 1.0); }
)GLSL";
    const char *geomFS = R"GLSL(
#version 460 core
uniform vec4 uColor;
out vec4 fragColor;
void main(){ fragColor = uColor; }
)GLSL";
    geomProg_ = linkProgram(geomVS, geomFS);
    uGeomMVP_ = glGetUniformLocation(geomProg_, "uMVP");
    uGeomCol_ = glGetUniformLocation(geomProg_, "uColor");

    glGenVertexArrays(1, &geomVAO_);
    glGenBuffers(1, &geomVBO_);
    glBindVertexArray(geomVAO_);
    glBindBuffer(GL_ARRAY_BUFFER, geomVBO_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 2 * 4096, nullptr,
                 GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 2, nullptr);
    glBindVertexArray(0);

    // ── Texture shader (for GDI+ text labels) ────────────────────────────
    const char *texVS = R"GLSL(
#version 460 core
layout(location=0) in vec2 aPos;
layout(location=1) in vec2 aUV;
uniform mat4 uMVP;
out vec2 vUV;
void main(){ vUV = aUV; gl_Position = uMVP * vec4(aPos, 0.0, 1.0); }
)GLSL";
    const char *texFS = R"GLSL(
#version 460 core
in vec2 vUV;
uniform sampler2D uTex;
out vec4 fragColor;
void main(){ fragColor = texture(uTex, vUV); }
)GLSL";
    texProg_ = linkProgram(texVS, texFS);
    uTexMVP_ = glGetUniformLocation(texProg_, "uMVP");
    glUseProgram(texProg_);
    glUniform1i(glGetUniformLocation(texProg_, "uTex"), 0);

    glGenVertexArrays(1, &texVAO_);
    glGenBuffers(1, &texVBO_);
    glBindVertexArray(texVAO_);
    glBindBuffer(GL_ARRAY_BUFFER, texVBO_);
    // 6 verts × (2 pos + 2 uv) floats
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 4 * 6, nullptr,
                 GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 4,
                          (void *)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 4,
                          (void *)(sizeof(float) * 2));
    glBindVertexArray(0);

    glResourcesBuilt_ = true;
  }

  void destroyGLResources() {
    if (!glResourcesBuilt_)
      return;
    if (geomProg_) {
      glDeleteProgram(geomProg_);
      geomProg_ = 0;
    }
    if (geomVAO_) {
      glDeleteVertexArrays(1, &geomVAO_);
      geomVAO_ = 0;
    }
    if (geomVBO_) {
      glDeleteBuffers(1, &geomVBO_);
      geomVBO_ = 0;
    }
    if (texProg_) {
      glDeleteProgram(texProg_);
      texProg_ = 0;
    }
    if (texVAO_) {
      glDeleteVertexArrays(1, &texVAO_);
      texVAO_ = 0;
    }
    if (texVBO_) {
      glDeleteBuffers(1, &texVBO_);
      texVBO_ = 0;
    }
    glResourcesBuilt_ = false;
  }

  // ── Orthographic matrix (NDC: x∈[-1,1], y∈[-1,1]) ────────────────────────
  // We just use identity since we already work in NDC. Helper kept for future.
  static void identityMVP(float out[16]) {
    for (int i = 0; i < 16; i++)
      out[i] = 0.f;
    out[0] = out[5] = out[10] = out[15] = 1.f;
  }

  // ── Draw helpers (modern GL) ──────────────────────────────────────────────

  void drawVerts(GLenum mode, const std::vector<float> &xy, float r, float g,
                 float b, float a, const float mvp[16]) {
    if (xy.empty())
      return;
    glUseProgram(geomProg_);
    glUniformMatrix4fv(uGeomMVP_, 1, GL_FALSE, mvp);
    glUniform4f(uGeomCol_, r, g, b, a);
    glBindVertexArray(geomVAO_);
    glBindBuffer(GL_ARRAY_BUFFER, geomVBO_);
    glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(xy.size() * sizeof(float)),
                 xy.data(), GL_DYNAMIC_DRAW);
    glDrawArrays(mode, 0, (GLsizei)(xy.size() / 2));
    glBindVertexArray(0);
  }

  // Emit two verts per line segment (GL_LINES)
  static void pushLine(std::vector<float> &v, float x0, float y0, float x1,
                       float y1) {
    v.insert(v.end(), {x0, y0, x1, y1});
  }
  // Emit a filled quad as two triangles
  static void pushQuad(std::vector<float> &v, float x0, float y0, float x1,
                       float y1) {
    v.insert(v.end(), {x0, y0, x1, y0, x1, y1, x1, y1, x0, y1, x0, y0});
  }

  // ── GDI+ text texture ─────────────────────────────────────────────────────

TextTexture makeTextTexture(const std::string &str, float size,
                            COLORREF color, bool bold = false) {
    if (str.empty())
        return {};
    int wlen = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
    std::wstring wtext(wlen, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, wtext.data(), wlen);

    Gdiplus::Bitmap measure(1, 1, PixelFormat32bppARGB);
    Gdiplus::Graphics gMeasure(&measure);
    int style = bold ? Gdiplus::FontStyleBold : Gdiplus::FontStyleRegular;
    Gdiplus::Font font(L"Segoe UI", size, style, Gdiplus::UnitPixel);
    Gdiplus::RectF layout(0, 0, 2000, 2000), bounds;
    gMeasure.MeasureString(wtext.c_str(), -1, &font, layout, &bounds);

    int tw = max(1, (int)std::ceil(bounds.Width) + 2);
    int th = max(1, (int)std::ceil(bounds.Height) + 2);

    Gdiplus::Bitmap bmp(tw, th, PixelFormat32bppARGB);
    Gdiplus::Graphics g(&bmp);
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAlias);
    g.Clear(Gdiplus::Color(0, 0, 0, 0));
    Gdiplus::SolidBrush brush(Gdiplus::Color(
        255, GetRValue(color), GetGValue(color), GetBValue(color)));
    g.DrawString(wtext.c_str(), -1, &font, Gdiplus::PointF(1.f, 1.f), &brush);

    Gdiplus::BitmapData bdata;
    Gdiplus::Rect rect(0, 0, tw, th);
    bmp.LockBits(&rect, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB,
                 &bdata);
    std::vector<BYTE> rgba(tw * th * 4);
    for (int row = 0; row < th; ++row) {
      const BYTE *src = (const BYTE *)bdata.Scan0 + row * bdata.Stride;
      BYTE *dst = rgba.data() + row * tw * 4;
      for (int col = 0; col < tw; ++col) {
        dst[0] = src[2];
        dst[1] = src[1];
        dst[2] = src[0];
        dst[3] = src[3];
        src += 4;
        dst += 4;
      }
    }
    bmp.UnlockBits(&bdata);

    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tw, th, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, rgba.data());
    glBindTexture(GL_TEXTURE_2D, 0);
    return {tex, tw, th};
  }

  // Draw a text texture at NDC position (cx,cy), anchor (0..1, 0..1)
  // Frees the texture immediately after drawing (single-frame use).
  void drawTextNDC(const TextTexture &tt, float cx, float cy, float anchorX,
                   float anchorY, const float mvp[16]) {
    if (!tt.id || !width || !height)
      return;
    float ndcW = tt.width * 2.f / width;
    float ndcH = tt.height * 2.f / height;
    float x0 = cx - anchorX * ndcW;
    float x1 = x0 + ndcW;
    float y0 = cy - (1.f - anchorY) * ndcH;
    float y1 = y0 + ndcH;

    // 6 verts: (pos.xy, uv.xy) — two triangles
    float verts[] = {
        x0, y1, 0.f, 0.f, x1, y1, 1.f, 0.f, x1, y0, 1.f, 1.f,
        x1, y0, 1.f, 1.f, x0, y0, 0.f, 1.f, x0, y1, 0.f, 0.f,
    };

    glUseProgram(texProg_);
    glUniformMatrix4fv(uTexMVP_, 1, GL_FALSE, mvp);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tt.id);
    glBindVertexArray(texVAO_);
    glBindBuffer(GL_ARRAY_BUFFER, texVBO_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_DYNAMIC_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glDeleteTextures(1, &tt.id);
  }

  // Draw text rotated 90° CCW (Y-axis label)
  void drawTextNDC_rotated(const TextTexture &tt, float cx, float cy,
                           const float mvp[16]) {
    if (!tt.id || !width || !height)
      return;
    float ndcW = tt.width * 2.f / width;
    float ndcH = tt.height * 2.f / height;
    // Swap w/h because we rotate
    float x0 = cx - ndcH * 0.5f, x1 = cx + ndcH * 0.5f;
    float y0 = cy - ndcW * 0.5f, y1 = cy + ndcW * 0.5f;

    // UVs rotated 90° CCW
    float verts[] = {
        x0, y1, 1.f, 0.f, x1, y1, 1.f, 1.f, x1, y0, 0.f, 1.f,
        x1, y0, 0.f, 1.f, x0, y0, 0.f, 0.f, x0, y1, 1.f, 0.f,
    };

    glUseProgram(texProg_);
    glUniformMatrix4fv(uTexMVP_, 1, GL_FALSE, mvp);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tt.id);
    glBindVertexArray(texVAO_);
    glBindBuffer(GL_ARRAY_BUFFER, texVBO_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_DYNAMIC_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glDeleteTextures(1, &tt.id);
  }

  // ── Child HWND / GL context ────────────────────────────────────────────────

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
    static bool registered = false;
    if (registered)
      return;
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = ChildProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = L"FluxGLGraph";
    wc.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
    RegisterClassExW(&wc);
    registered = true;
  }

void ensureChildWindow(GraphicsContext & /*ctx*/) {
    if (childHwnd) return;
    parentHwnd = FluxUI::getCurrentInstance()->getWindow();
    if (!parentHwnd) return;
    registerChildClass();

    childHwnd = CreateWindowExW(
        WS_EX_NOPARENTNOTIFY, L"FluxGLGraph", nullptr,
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN, x, y, width,
        height, parentHwnd, nullptr, GetModuleHandle(nullptr), nullptr);
    if (!childHwnd)
      return;

    glDC = GetDC(childHwnd);
    setupPixelFormat(glDC);

    // Bootstrap context needed to call wglCreateContextAttribsARB
    HGLRC tmp = wglCreateContext(glDC);
    wglMakeCurrent(glDC, tmp);


    auto wglCA = reinterpret_cast<PFNWGLCREATECONTEXTATTRIBSARBPROC>(
        wglGetProcAddress("wglCreateContextAttribsARB"));

    if (wglCA) {
      const int att[] = {
          0x2091, 4,          // WGL_CONTEXT_MAJOR_VERSION_ARB
          0x2092, 6,          // WGL_CONTEXT_MINOR_VERSION_ARB
          0x9126, 0x00000001, // WGL_CONTEXT_PROFILE_MASK_ARB, CORE
          0};
      HGLRC core = wglCA(glDC, nullptr, att);
      wglMakeCurrent(nullptr, nullptr);
      wglDeleteContext(tmp);
      glRC = core ? core : wglCreateContext(glDC);
    } else {
      glRC = tmp;
    }

    wglMakeCurrent(glDC, glRC);

    // Let GLAD load all function pointers for this context.
    // If the CanvasWidget has already called gladLoadGL() in its own context,
    // this second call is safe — GLAD is per-context and re-entrant.
    if (!gladLoadGL()) {
      OutputDebugStringA("GraphWidget: gladLoadGL() failed\n");
    }

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    buildGLResources();
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
    SetPixelFormat(dc, ChoosePixelFormat(dc, &pfd), &pfd);
  }

  void moveChildWindow() {
    if (!childHwnd)
      return;
    SetWindowPos(childHwnd, nullptr, x, y, width, height,
                 SWP_NOZORDER | SWP_NOACTIVATE);
  }

  void destroyGL() {
    if (glRC) {
      wglMakeCurrent(glDC, glRC);
      destroyGLResources();
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

  // ── Main render ───────────────────────────────────────────────────────────

  void renderGL() {
    if (!glRC || !glDC)
      return;
    if (wglGetCurrentContext() != glRC)
      wglMakeCurrent(glDC, glRC);

    float mvp[16];
    identityMVP(mvp); // NDC passthrough

    glViewport(0, 0, width, height);
    float bgR = GetRValue(bgColor) / 255.f, bgG = GetGValue(bgColor) / 255.f,
          bgB = GetBValue(bgColor) / 255.f;
    glClearColor(bgR, bgG, bgB, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);

    const float marginL = 0.15f, marginR = 0.04f;
    const float marginT = 0.10f, marginB = 0.15f;
    float px0 = -1.f + marginL * 2.f, px1 = 1.f - marginR * 2.f;
    float py0 = -1.f + marginB * 2.f, py1 = 1.f - marginT * 2.f;

    computeRange();

    if (showGrid)
      drawGrid(px0, px1, py0, py1, mvp);
    drawAxes(px0, px1, py0, py1, mvp);

    for (auto &s : series) {
      if (s.values.empty())
        continue;
      switch (graphType) {
      case GraphType::Line:
        drawLine(s, px0, px1, py0, py1, mvp);
        break;
      case GraphType::Bar:
        drawBars(s, px0 + 0.1f, px1 + 0.1f, py0, py1, mvp);
        break;
      case GraphType::Area:
        drawArea(s, px0, px1, py0, py1, mvp);
        break;
      }
    }

    // ── Text labels ─────────────────────────────────────────────────────────

    if (!title.empty()) {
      auto tt = makeTextTexture(title, 13.f, RGB(220, 220, 220), true);
      drawTextNDC(tt, (px0 + px1) * 0.5f, 1.f - marginT * 0.5f, 0.5f, 0.5f,
                  mvp);
    }
    if (!yAxisTitle.empty()) {
      auto tt = makeTextTexture(yAxisTitle, 11.f, RGB(180, 180, 180));
      drawTextNDC_rotated(tt, -1.f + 0.03f, (py0 + py1) * 0.5f, mvp);
    }
    if (!xAxisTitle.empty()) {
      auto tt = makeTextTexture(xAxisTitle, 11.f, RGB(180, 180, 180));
      drawTextNDC(tt, (px0 + px1) * 0.5f, -1.f + marginB * 0.3f, 0.5f, 0.5f,
                  mvp);
    }

    // Y tick labels (6 ticks)
    for (int i = 0; i <= 5; ++i) {
      float t = float(i) / 5.f;
      float val = yMin + t * (yMax - yMin);
      float ndcY = py0 + t * (py1 - py0);
      char buf[32];
      if (std::abs(val) >= 1000)
        std::snprintf(buf, sizeof(buf), "%.0f", val);
      else if (std::abs(val) >= 10)
        std::snprintf(buf, sizeof(buf), "%.1f", val);
      else
        std::snprintf(buf, sizeof(buf), "%.2f", val);
      auto tt = makeTextTexture(buf, 10.f, RGB(160, 160, 160));
      drawTextNDC(tt, px0 - 0.01f, ndcY, 1.f, 0.5f, mvp);
    }

    // X tick labels
    if (!xLabels.empty()) {
      int n = (int)xLabels.size();
      for (int i = 0; i < n; ++i) {
        float ndcX = indexToNDC_X(i, n, px0, px1);
        auto tt = makeTextTexture(xLabels[i], 10.f, RGB(160, 160, 160));
        drawTextNDC(tt, ndcX, py0 - 0.17f, 0.5f, 1.f, mvp);
      }
    } else if (!series.empty() && !series[0].values.empty()) {
      int n = (int)series[0].values.size();
      int step = max(1, n / 6);
      for (int i = 0; i < n; i += step) {
        float ndcX = indexToNDC_X(i, n, px0, px1);
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%d", i);
        auto tt = makeTextTexture(buf, 10.f, RGB(160, 160, 160));
        drawTextNDC(tt, ndcX, py0 - 0.17f, 0.5f, 1.f, mvp);
      }
    }

    // Legend (top-right)
    if (showLegend && series.size() > 1) {
      float lx = px1 - 0.01f;
      float ly = py1;
      float lineH = 18.f * 2.f / height;
      float swatchW = 0.06f;

      for (auto &s : series) {
        // Color swatch — a thin filled rect
        std::vector<float> sw;
        float sy = ly - lineH * 0.5f;
        pushQuad(sw, lx - swatchW, sy - 0.005f, lx, sy + 0.005f);
        drawVerts(GL_TRIANGLES, sw, s.r, s.g, s.b, 1.f, mvp);

        auto tt = makeTextTexture(s.label, 10.f, RGB(200, 200, 200));
        drawTextNDC(tt, lx - swatchW - 0.01f, sy, 1.f, 0.5f, mvp);
        ly -= lineH;
      }
    }

    SwapBuffers(glDC);
  }

  // ── Range / coordinate helpers ────────────────────────────────────────────

  void computeRange() {
    if (!autoRange)
      return;
    yMin = 0.f;
    yMax = 1.f;
    bool first = true;
    for (auto &s : series)
      for (float v : s.values) {
        if (first) {
          yMin = yMax = v;
          first = false;
        } else {
          yMin = min(yMin, v);
          yMax = max(yMax, v);
        }
      }
    if (yMin == yMax) {
      yMin -= 1.f;
      yMax += 1.f;
    }
    float pad = (yMax - yMin) * 0.1f;
    yMin -= pad;
    yMax += pad;
  }

  float dataToNDC_Y(float v, float py0, float py1) const {
    float t = (v - yMin) / (yMax - yMin);
    return py0 + t * (py1 - py0);
  }
  float indexToNDC_X(int i, int n, float px0, float px1) const {
    if (n <= 1)
      return (px0 + px1) * 0.5f;
    return px0 + float(i) / (n - 1) * (px1 - px0);
  }

  // ── Geometry draw calls ───────────────────────────────────────────────────

void drawGrid(float px0, float px1, float py0, float py1,
              const float mvp[16]) {
    std::vector<float> lines;
    for (int i = 0; i <= 5; ++i) {
        float t = float(i) / 5.f;
        float lineY = py0 + t * (py1 - py0);
        pushLine(lines, px0, lineY, px1, lineY);
    }
    for (int i = 0; i <= 6; ++i) {
        float t = float(i) / 6.f;
        float lineX = px0 + t * (px1 - px0);
        pushLine(lines, lineX, py0, lineX, py1);
    }
    drawVerts(GL_LINES, lines, 1.f, 1.f, 1.f, 0.08f, mvp);
}

  void drawAxes(float px0, float px1, float py0, float py1,
                const float mvp[16]) {
    std::vector<float> lines;
    pushLine(lines, px0, py0, px1, py0); // X axis
    pushLine(lines, px0, py0, px0, py1); // Y axis
    drawVerts(GL_LINES, lines, 0.6f, 0.6f, 0.6f, 1.f, mvp);
  }

  void drawLine(const GraphSeries &s, float px0, float px1, float py0,
                float py1, const float mvp[16]) {
    int n = (int)s.values.size();
    // Line strip — build as GL_LINE_STRIP via individual segments
    std::vector<float> strip;
    for (int i = 0; i < n - 1; ++i) {
      pushLine(strip, indexToNDC_X(i, n, px0, px1),
               dataToNDC_Y(s.values[i], py0, py1),
               indexToNDC_X(i + 1, n, px0, px1),
               dataToNDC_Y(s.values[i + 1], py0, py1));
    }
    drawVerts(GL_LINES, strip, s.r, s.g, s.b, 1.f, mvp);

    // Point dots — small quads (GL doesn't guarantee point size in core)
    float dotR = 3.f * 2.f / min(width, height);
    std::vector<float> dots;
    for (int i = 0; i < n; ++i) {
      float cx = indexToNDC_X(i, n, px0, px1);
      float cy = dataToNDC_Y(s.values[i], py0, py1);
      pushQuad(dots, cx - dotR, cy - dotR, cx + dotR, cy + dotR);
    }
    drawVerts(GL_TRIANGLES, dots, s.r, s.g, s.b, 1.f, mvp);
  }

  void drawArea(const GraphSeries &s, float px0, float px1, float py0,
                float py1, const float mvp[16]) {
    int n = (int)s.values.size();
    float baseline = dataToNDC_Y(max(0.f, yMin), py0, py1);

    // Filled area as triangle strip (emulated with triangles)
    std::vector<float> fill;
    for (int i = 0; i < n - 1; ++i) {
      float xa = indexToNDC_X(i, n, px0, px1),
            ya = dataToNDC_Y(s.values[i], py0, py1);
      float xb = indexToNDC_X(i + 1, n, px0, px1),
            yb = dataToNDC_Y(s.values[i + 1], py0, py1);
      // Two triangles per column slice
      fill.insert(fill.end(), {xa, baseline, xb, baseline, xb, yb, xb, yb, xa,
                               ya, xa, baseline});
    }
    drawVerts(GL_TRIANGLES, fill, s.r, s.g, s.b, 0.25f, mvp);
    drawLine(s, px0, px1, py0, py1, mvp);
  }

  void drawBars(const GraphSeries &s, float px0, float px1, float py0,
                float py1, const float mvp[16]) {
    int n = (int)s.values.size();
    if (n == 0)
      return;
    float baseline = dataToNDC_Y(max(yMin, 0.f), py0, py1);
    float barW = (px1 - px0) / n * 0.6f;

    std::vector<float> fill, outline;
    for (int i = 0; i < n; ++i) {
      float cx = indexToNDC_X(i, n, px0, px1);
      float top = dataToNDC_Y(s.values[i], py0, py1);
      float l = cx - barW * 0.5f, r = cx + barW * 0.5f;
      pushQuad(fill, l, baseline, r, top);
      // Outline as 4 lines
      pushLine(outline, l, baseline, r, baseline);
      pushLine(outline, r, baseline, r, top);
      pushLine(outline, r, top, l, top);
      pushLine(outline, l, top, l, baseline);
    }
    drawVerts(GL_TRIANGLES, fill, s.r, s.g, s.b, 0.85f, mvp);
    drawVerts(GL_LINES, outline, min(s.r * 1.3f, 1.f), min(s.g * 1.3f, 1.f),
              min(s.b * 1.3f, 1.f), 1.f, mvp);
  }
};

// ============================================================================
// FACTORY
// ============================================================================

using GraphWidgetPtr = std::shared_ptr<GraphWidget>;

inline GraphWidgetPtr Graph() { return std::make_shared<GraphWidget>(); }
inline GraphWidgetPtr Graph(int w, int h) {
  auto g = std::make_shared<GraphWidget>();
  g->setSize(w, h);
  return g;
}


#endif // FLUX_GRAPH_HPP