#ifndef FLUX_CANVAS_HPP
#define FLUX_CANVAS_HPP

// ============================================================================
// flux_canvas.hpp  —  HTML-Canvas-style 2-D drawing widget for Flux
//
// Drop next to flux_widget.hpp.  No extra dependencies beyond what
// flux_display.hpp already pulls in (OpenGL, GDI+, Win32).
//
// Quickstart
// ----------
//   auto c = Canvas(600, 400)
//       ->onDraw([](CanvasContext& ctx) {
//           ctx.fillStyle("#1a1a2e");
//           ctx.fillRect(0, 0, 600, 400);
//
//           ctx.strokeStyle("#e94560").lineWidth(3);
//           ctx.beginPath();
//           ctx.moveTo(50, 200);
//           ctx.lineTo(550, 200);
//           ctx.stroke();
//
//           ctx.fillStyle("#ffffff").font("24px", "Segoe UI");
//           ctx.fillText("Hello, Canvas!", 300, 40);
//       });
//
// API surface (mirrors HTML Canvas 2D context)
// --------------------------------------------
//   State management : save() / restore()
//   Transforms       : translate(x,y) / scale(x,y) / rotate(rad) /
//   resetTransform() Style            : fillStyle(css) / strokeStyle(css) /
//   lineWidth(px)
//                      lineCap("butt"|"round"|"square")
//                      lineJoin("miter"|"round"|"bevel")
//                      globalAlpha(0..1)
//   Paths            : beginPath() / closePath()
//                      moveTo / lineTo / quadraticCurveTo / bezierCurveTo
//                      arc / arcTo / rect / roundRect / ellipse
//   Drawing          : fill() / stroke()
//                      fillRect / strokeRect / clearRect
//                      fillText / strokeText
//   Helpers          : measureText(str) → float width
//   Pixel ops        : (extend as needed via raw GL)
// ============================================================================

#include "flux_state.hpp"
#include "flux_widget.hpp"

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <gl/GL.h>
#include <gl/GLU.h>
#include <windows.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <stack>
#include <string>
#include <vector>

#pragma comment(lib, "opengl32.lib")
#pragma comment(lib, "glu32.lib")

using namespace Gdiplus;

// ============================================================================
// COLOR PARSING
// ============================================================================

struct RGBA {
  float r, g, b, a;
};

namespace detail {

// Parse hex: #rgb, #rgba, #rrggbb, #rrggbbaa
inline RGBA parseHex(const std::string &s) {
  std::string h = s;
  if (!h.empty() && h[0] == '#')
    h = h.substr(1);
  auto hex2f = [](unsigned v) { return v / 255.0f; };
  unsigned n = (unsigned)std::stoul(h, nullptr, 16);
  if (h.size() == 3) {
    unsigned r = (n >> 8) & 0xF, g = (n >> 4) & 0xF, b = n & 0xF;
    return {hex2f(r | (r << 4)), hex2f(g | (g << 4)), hex2f(b | (b << 4)), 1.f};
  }
  if (h.size() == 4) {
    unsigned r = (n >> 12) & 0xF, g = (n >> 8) & 0xF, b = (n >> 4) & 0xF,
             a = n & 0xF;
    return {hex2f(r | (r << 4)), hex2f(g | (g << 4)), hex2f(b | (b << 4)),
            hex2f(a | (a << 4))};
  }
  if (h.size() == 6) {
    return {hex2f((n >> 16) & 0xFF), hex2f((n >> 8) & 0xFF), hex2f(n & 0xFF),
            1.f};
  }
  // 8 chars
  return {hex2f((n >> 24) & 0xFF), hex2f((n >> 16) & 0xFF),
          hex2f((n >> 8) & 0xFF), hex2f(n & 0xFF)};
}

inline RGBA parseColor(const std::string &css) {
  if (css.empty())
    return {1, 1, 1, 1};
  if (css[0] == '#')
    return parseHex(css);

  // rgb(r,g,b) / rgba(r,g,b,a)
  if (css.substr(0, 3) == "rgb") {
    auto a = css.find('('), b = css.find(')');
    if (a == std::string::npos)
      return {1, 1, 1, 1};
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

  // named colours (small common set)
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

} // namespace detail

// ============================================================================
// DRAW STATE  (mirrors the HTML canvas state stack entry)
// ============================================================================

struct CanvasState {
  RGBA fillColor = {0, 0, 0, 1};
  RGBA strokeColor = {0, 0, 0, 1};
  float lineW = 1.f;
  float alpha = 1.f;
  int lineCap = 0;  // 0=butt 1=round 2=square
  int lineJoin = 0; // 0=miter 1=round 2=bevel
  // transform: column-major 3x3 affine  [a,b,0, c,d,0, tx,ty,1]
  float m[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
  // font
  float fontSize = 16.f;
  std::string fontFamily = "Segoe UI";
};

// ============================================================================
// PATH COMMAND  (tiny bytecode)
// ============================================================================

struct PathCmd {
  enum Type { MoveTo, LineTo, QuadTo, BezierTo, ArcTo, Arc, Close } type;
  float x0, y0, x1, y1, x2, y2, r, r2, start, end;
  bool ccw;
};

// ============================================================================
// CANVAS CONTEXT  (the drawing API, created fresh each frame)
// ============================================================================

class CanvasContext {
public:
  int W, H;  // canvas pixel dimensions
  HDC gdiDC; // GDI+ compatible DC for text measurement / rasterisation

  CanvasContext(int w, int h, HDC dc) : W(w), H(h), gdiDC(dc) {
    st_.push({});
    setupGL();
  }

  // ── State stack ─────────────────────────────────────────────────────────
  CanvasContext &save() {
    st_.push(st_.top());
    return *this;
  }
  CanvasContext &restore() {
    if (st_.size() > 1)
      st_.pop();
    applyTransform();
    return *this;
  }

  // ── Styles ──────────────────────────────────────────────────────────────
  CanvasContext &fillStyle(const std::string &css) {
    st_.top().fillColor = detail::parseColor(css);
    return *this;
  }
  CanvasContext &strokeStyle(const std::string &css) {
    st_.top().strokeColor = detail::parseColor(css);
    return *this;
  }
  CanvasContext &lineWidth(float w) {
    st_.top().lineW = w;
    return *this;
  }
  CanvasContext &globalAlpha(float a) {
    st_.top().alpha = a;
    return *this;
  }
  CanvasContext &lineCap(const std::string &v) {
    auto &s = st_.top();
    s.lineCap = (v == "round") ? 1 : (v == "square") ? 2 : 0;
    return *this;
  }
  CanvasContext &lineJoin(const std::string &v) {
    auto &s = st_.top();
    s.lineJoin = (v == "round") ? 1 : (v == "bevel") ? 2 : 0;
    return *this;
  }
  CanvasContext &font(const std::string &sizeStr,
                      const std::string &family = "") {
    auto &s = st_.top();
    // parse "24px" or "24"
    try {
      s.fontSize = std::stof(sizeStr);
    } catch (...) {
    }
    if (!family.empty())
      s.fontFamily = family;
    return *this;
  }

  // ── Transforms ──────────────────────────────────────────────────────────
  CanvasContext &translate(float tx, float ty) {
    float *m = st_.top().m;
    // column-major: new tx = m[6]+tx, ty = m[7]+ty
    m[6] += tx * m[0] + ty * m[3];
    m[7] += tx * m[1] + ty * m[4];
    applyTransform();
    return *this;
  }
  CanvasContext &scale(float sx, float sy) {
    float *m = st_.top().m;
    m[0] *= sx;
    m[1] *= sx;
    m[3] *= sy;
    m[4] *= sy;
    applyTransform();
    return *this;
  }
  CanvasContext &rotate(float rad) {
    float c = std::cos(rad), s = std::sin(rad);
    float *m = st_.top().m;
    float a = m[0], b = m[1], d = m[3], e = m[4];
    m[0] = a * c + d * s;
    m[1] = b * c + e * s;
    m[3] = -a * s + d * c;
    m[4] = -b * s + e * c;
    applyTransform();
    return *this;
  }
  CanvasContext &resetTransform() {
    float *m = st_.top().m;
    m[0] = 1;
    m[1] = 0;
    m[2] = 0;
    m[3] = 0;
    m[4] = 1;
    m[5] = 0;
    m[6] = 0;
    m[7] = 0;
    m[8] = 1;
    applyTransform();
    return *this;
  }

  // ── Path API ────────────────────────────────────────────────────────────
  CanvasContext &beginPath() {
    path_.clear();
    return *this;
  }
  CanvasContext &closePath() {
    path_.push_back({PathCmd::Close});
    return *this;
  }
  CanvasContext &moveTo(float x, float y) {
    path_.push_back({PathCmd::MoveTo, x, y});
    return *this;
  }
  CanvasContext &lineTo(float x, float y) {
    path_.push_back({PathCmd::LineTo, x, y});
    return *this;
  }

  CanvasContext &quadraticCurveTo(float cpx, float cpy, float x, float y) {
    PathCmd c{};
    c.type = PathCmd::QuadTo;
    c.x0 = cpx;
    c.y0 = cpy;
    c.x1 = x;
    c.y1 = y;
    path_.push_back(c);
    return *this;
  }
  CanvasContext &bezierCurveTo(float cp1x, float cp1y, float cp2x, float cp2y,
                               float x, float y) {
    PathCmd c{};
    c.type = PathCmd::BezierTo;
    c.x0 = cp1x;
    c.y0 = cp1y;
    c.x1 = cp2x;
    c.y1 = cp2y;
    c.x2 = x;
    c.y2 = y;
    path_.push_back(c);
    return *this;
  }
  CanvasContext &arc(float x, float y, float r, float startAngle,
                     float endAngle, bool anticlockwise = false) {
    PathCmd c{};
    c.type = PathCmd::Arc;
    c.x0 = x;
    c.y0 = y;
    c.r = r;
    c.start = startAngle;
    c.end = endAngle;
    c.ccw = anticlockwise;
    path_.push_back(c);
    return *this;
  }
  CanvasContext &rect(float x, float y, float w, float h) {
    moveTo(x, y);
    lineTo(x + w, y);
    lineTo(x + w, y + h);
    lineTo(x, y + h);
    closePath();
    return *this;
  }
  CanvasContext &roundRect(float x, float y, float w, float h, float r) {
    r = min(r, min(w, h) * 0.5f);
    moveTo(x + r, y);
    lineTo(x + w - r, y);
    arc(x + w - r, y + r, r, -M_PI_2_F, 0);
    lineTo(x + w, y + h - r);
    arc(x + w - r, y + h - r, r, 0, M_PI_2_F);
    lineTo(x + r, y + h);
    arc(x + r, y + h - r, r, M_PI_2_F, (float)M_PI);
    lineTo(x, y + r);
    arc(x + r, y + r, r, (float)M_PI, -M_PI_2_F);
    closePath();
    return *this;
  }
  CanvasContext &ellipse(float cx, float cy, float rx, float ry, float rotation,
                         float startAngle, float endAngle) {
    // Approximate ellipse as a series of lines (tessellate)
    int segs = 64;
    float da = (endAngle - startAngle) / segs;
    float cosR = std::cos(rotation), sinR = std::sin(rotation);
    bool first = true;
    for (int i = 0; i <= segs; i++) {
      float a = startAngle + i * da;
      float ex = rx * std::cos(a), ey = ry * std::sin(a);
      float px = cx + ex * cosR - ey * sinR;
      float py = cy + ex * sinR + ey * cosR;
      if (first) {
        moveTo(px, py);
        first = false;
      } else
        lineTo(px, py);
    }
    return *this;
  }

  // ── Fill / Stroke ────────────────────────────────────────────────────────
  CanvasContext &fill() {
    setColor(st_.top().fillColor);
    rasterisePath(true);
    return *this;
  }
  CanvasContext &stroke() {
    glLineWidth(st_.top().lineW);
    setColor(st_.top().strokeColor);
    rasterisePath(false);
    return *this;
  }

  // ── Rect shortcuts ───────────────────────────────────────────────────────
  CanvasContext &fillRect(float x, float y, float w, float h) {
    setColor(st_.top().fillColor);
    glBegin(GL_QUADS);
    emitV(x, y);
    emitV(x + w, y);
    emitV(x + w, y + h);
    emitV(x, y + h);
    glEnd();
    return *this;
  }
  CanvasContext &strokeRect(float x, float y, float w, float h) {
    glLineWidth(st_.top().lineW);
    setColor(st_.top().strokeColor);
    glBegin(GL_LINE_LOOP);
    emitV(x, y);
    emitV(x + w, y);
    emitV(x + w, y + h);
    emitV(x, y + h);
    glEnd();
    return *this;
  }
  CanvasContext &clearRect(float x, float y, float w, float h) {
    glScissor((GLint)x, H - (GLint)(y + h), (GLsizei)w, (GLsizei)h);
    glEnable(GL_SCISSOR_TEST);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_SCISSOR_TEST);
    return *this;
  }

  // ── Text ─────────────────────────────────────────────────────────────────
  //   Renders via GDI+ into a GL texture, then blits at (x,y)
  CanvasContext &fillText(const std::string &text, float x, float y,
                          float maxWidth = -1.f) {
    drawText(text, x, y, true);
    return *this;
  }
  CanvasContext &strokeText(const std::string &text, float x, float y,
                            float maxWidth = -1.f) {
    drawText(text, x, y, false);
    return *this;
  }
  float measureText(const std::string &text) {
    auto tt = makeTextTex(text, false);
    if (tt.id)
      glDeleteTextures(1, &tt.id);
    return (float)tt.w;
  }

  // ── Line helpers ─────────────────────────────────────────────────────────
  CanvasContext &drawLine(float x1, float y1, float x2, float y2) {
    glLineWidth(st_.top().lineW);
    setColor(st_.top().strokeColor);
    glBegin(GL_LINES);
    emitV(x1, y1);
    emitV(x2, y2);
    glEnd();
    return *this;
  }

  // ── Circle shortcuts ─────────────────────────────────────────────────────
  CanvasContext &fillCircle(float cx, float cy, float r) {
    beginPath();
    arc(cx, cy, r, 0, (float)(2 * M_PI));
    fill();
    return *this;
  }
  CanvasContext &strokeCircle(float cx, float cy, float r) {
    beginPath();
    arc(cx, cy, r, 0, (float)(2 * M_PI));
    stroke();
    return *this;
  }

private:
  std::stack<CanvasState> st_;
  std::vector<PathCmd> path_;
  static constexpr double M_PI = 3.14159265358979323846;
  static constexpr float M_PI_2_F = 1.5707963f;

  // ── GL setup ─────────────────────────────────────────────────────────────
  void setupGL() {
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, W, H, 0, -1, 1); // pixel-space, Y-down like HTML canvas
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_LINE_SMOOTH);
    glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
    glEnable(GL_POINT_SMOOTH);
  }

  void applyTransform() {
    float *m = st_.top().m;
    GLfloat mat[16] = {m[0], m[1], 0, 0, m[3], m[4], 0, 0,
                       0,    0,    1, 0, m[6], m[7], 0, 1};
    glMatrixMode(GL_MODELVIEW);
    glLoadMatrixf(mat);
  }

  void setColor(const RGBA &c) {
    float a = c.a * st_.top().alpha;
    glColor4f(c.r, c.g, c.b, a);
  }

  // Emit a vertex in pixel coords (already in correct ortho space)
  void emitV(float px, float py) { glVertex2f(px, py); }

  // ── Path tessellation ─────────────────────────────────────────────────────
  struct FlatVert {
    float x, y;
  };
  std::vector<FlatVert> tessPath() {
    std::vector<FlatVert> verts;
    float cx = 0, cy = 0, sx = 0, sy = 0;
    bool hasSub = false;

    for (auto &cmd : path_) {
      switch (cmd.type) {
      case PathCmd::MoveTo:
        cx = cmd.x0;
        cy = cmd.y0;
        sx = cx;
        sy = cy;
        hasSub = true;
        verts.push_back({-1e9f, -1e9f}); // sub-path separator
        verts.push_back({cx, cy});
        break;
      case PathCmd::LineTo:
        cx = cmd.x0;
        cy = cmd.y0;
        verts.push_back({cx, cy});
        break;
      case PathCmd::QuadTo: {
        // Subdivide quadratic Bézier
        float x0 = cx, y0 = cy, x1 = cmd.x0, y1 = cmd.y0, x2 = cmd.x1,
              y2 = cmd.y1;
        const int N = 16;
        for (int i = 1; i <= N; i++) {
          float t = (float)i / N, it = 1 - t;
          float bx = it * it * x0 + 2 * it * t * x1 + t * t * x2;
          float by = it * it * y0 + 2 * it * t * y1 + t * t * y2;
          verts.push_back({bx, by});
        }
        cx = x2;
        cy = y2;
        break;
      }
      case PathCmd::BezierTo: {
        float x0 = cx, y0 = cy, x1 = cmd.x0, y1 = cmd.y0, x2 = cmd.x1,
              y2 = cmd.y1, x3 = cmd.x2, y3 = cmd.y2;
        const int N = 32;
        for (int i = 1; i <= N; i++) {
          float t = (float)i / N, it = 1 - t;
          float bx = it * it * it * x0 + 3 * it * it * t * x1 +
                     3 * it * t * t * x2 + t * t * t * x3;
          float by = it * it * it * y0 + 3 * it * it * t * y1 +
                     3 * it * t * t * y2 + t * t * t * y3;
          verts.push_back({bx, by});
        }
        cx = x3;
        cy = y3;
        break;
      }
      case PathCmd::Arc: {
        float angStart = cmd.start, angEnd = cmd.end;
        if (cmd.ccw) {
          while (angEnd > angStart)
            angEnd -= (float)(2 * M_PI);
        } else {
          while (angEnd < angStart)
            angEnd += (float)(2 * M_PI);
        }
        float sweep = angEnd - angStart;
        int N = max(8, (int)(std::abs(sweep) * cmd.r / 2));
        N = min(N, 256);
        for (int i = 0; i <= N; i++) {
          float a = angStart + sweep * ((float)i / N);
          float px = cmd.x0 + cmd.r * std::cos(a);
          float py = cmd.y0 + cmd.r * std::sin(a);
          if (i == 0 && verts.empty())
            verts.push_back({px, py});
          else
            verts.push_back({px, py});
        }
        cx = cmd.x0 + cmd.r * std::cos(angEnd);
        cy = cmd.y0 + cmd.r * std::sin(angEnd);
        break;
      }
      case PathCmd::Close:
        verts.push_back({sx, sy});
        cx = sx;
        cy = sy;
        break;
      default:
        break;
      }
    }
    return verts;
  }

  void rasterisePath(bool doFill) {
    auto verts = tessPath();
    if (verts.empty())
      return;

    // Split on separator markers into sub-paths
    std::vector<std::vector<FlatVert>> subpaths;
    std::vector<FlatVert> cur;
    for (auto &v : verts) {
      if (v.x < -1e8f) {
        if (!cur.empty())
          subpaths.push_back(std::move(cur));
        cur.clear();
      } else {
        cur.push_back(v);
      }
    }
    if (!cur.empty())
      subpaths.push_back(std::move(cur));

    if (doFill) {
      // Simple fan triangulation for convex or near-convex paths
      for (auto &sp : subpaths) {
        if (sp.size() < 3)
          continue;
        glBegin(GL_TRIANGLE_FAN);
        for (auto &v : sp)
          glVertex2f(v.x, v.y);
        glEnd();
      }
    } else {
      for (auto &sp : subpaths) {
        glBegin(GL_LINE_STRIP);
        for (auto &v : sp)
          glVertex2f(v.x, v.y);
        glEnd();
      }
    }
  }

  // ── Text rendering via GDI+ ───────────────────────────────────────────────
  struct TextTex {
    GLuint id = 0;
    int w = 0, h = 0;
  };

  TextTex makeTextTex(const std::string &text, bool filled) {
    if (text.empty())
      return {};
    auto &s = st_.top();

    int wlen = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    std::wstring wt(wlen, 0);
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, wt.data(), wlen);

    int wflen =
        MultiByteToWideChar(CP_UTF8, 0, s.fontFamily.c_str(), -1, nullptr, 0);
    std::wstring wf(wflen, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.fontFamily.c_str(), -1, wf.data(), wflen);

    Gdiplus::Bitmap measure(1, 1, PixelFormat32bppARGB);
    Gdiplus::Graphics gm(&measure);
    Gdiplus::Font font(wf.c_str(), s.fontSize, Gdiplus::FontStyleRegular,
                       Gdiplus::UnitPixel);
    Gdiplus::RectF lay(0, 0, 4000, 4000), bounds;
    gm.MeasureString(wt.c_str(), -1, &font, lay, &bounds);

    int tw = max(1, (int)std::ceil(bounds.Width) + 2);
    int th = max(1, (int)std::ceil(bounds.Height) + 2);

    Gdiplus::Bitmap bmp(tw, th, PixelFormat32bppARGB);
    Gdiplus::Graphics g(&bmp);
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAlias);
    g.Clear(Gdiplus::Color(0, 0, 0, 0));

    const RGBA &col = filled ? s.fillColor : s.strokeColor;
    BYTE cr = (BYTE)(col.r * 255), cg = (BYTE)(col.g * 255),
         cb = (BYTE)(col.b * 255);
    BYTE ca = (BYTE)(col.a * s.alpha * 255);
    Gdiplus::SolidBrush brush(Gdiplus::Color(ca, cr, cg, cb));
    g.DrawString(wt.c_str(), -1, &font, Gdiplus::PointF(1, 1), &brush);

    Gdiplus::BitmapData bd;
    Gdiplus::Rect r(0, 0, tw, th);
    bmp.LockBits(&r, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &bd);
    std::vector<BYTE> rgba(tw * th * 4);
    for (int row = 0; row < th; row++) {
      const BYTE *src = (const BYTE *)bd.Scan0 + row * bd.Stride;
      BYTE *dst = rgba.data() + row * tw * 4;
      for (int col = 0; col < tw; col++, src += 4, dst += 4) {
        dst[0] = src[2];
        dst[1] = src[1];
        dst[2] = src[0];
        dst[3] = src[3];
      }
    }
    bmp.UnlockBits(&bd);

    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tw, th, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, rgba.data());
    glBindTexture(GL_TEXTURE_2D, 0);

    return {tex, tw, th};
  }

  void drawText(const std::string &text, float x, float y, bool filled) {
    auto tt = makeTextTex(text, filled);
    if (!tt.id)
      return;

    float x1 = x, y1 = y - tt.h; // Y-down: baseline at y, top at y-h
    float x2 = x + tt.w, y2 = y;

    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, tt.id);
    glColor4f(1, 1, 1, 1);
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0);
    glVertex2f(x1, y1);
    glTexCoord2f(1, 0);
    glVertex2f(x2, y1);
    glTexCoord2f(1, 1);
    glVertex2f(x2, y2);
    glTexCoord2f(0, 1);
    glVertex2f(x1, y2);
    glEnd();
    glDisable(GL_TEXTURE_2D);
    glDeleteTextures(1, &tt.id);
    glPopMatrix();
  }
};

// ============================================================================
// CANVAS WIDGET
// ============================================================================

class CanvasWidget : public Widget {
public:
  using DrawFn = std::function<void(CanvasContext &)>;

  CanvasWidget() {
    autoWidth = false;
    autoHeight = false;
    width = 400;
    height = 300;
  }
  ~CanvasWidget() { destroyGL(); }

  // ── Fluent API ────────────────────────────────────────────────────────────
  std::shared_ptr<CanvasWidget> onDraw(DrawFn fn) {
    drawFn = std::move(fn);
    markNeedsPaint();
    return std::static_pointer_cast<CanvasWidget>(shared_from_this());
  }
  std::shared_ptr<CanvasWidget> setSize(int w, int h) {
    width = w;
    height = h;
    autoWidth = autoHeight = false;
    markNeedsLayout();
    return std::static_pointer_cast<CanvasWidget>(shared_from_this());
  }
  // Redraw (call from outside to trigger repaint)
  std::shared_ptr<CanvasWidget> redraw() {
    markNeedsPaint();
    return std::static_pointer_cast<CanvasWidget>(shared_from_this());
  }

  // ── Widget overrides ──────────────────────────────────────────────────────
  void computeLayout(HDC hdc, const BoxConstraints &constraints,
                     FontCache &fontCache) override {
    width = constraints.clampWidth(autoWidth ? constraints.maxWidth : width);
    height =
        constraints.clampHeight(autoHeight ? constraints.maxHeight : height);
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
  DrawFn drawFn;

  HWND childHwnd = nullptr;
  HDC glDC = nullptr;
  HGLRC glRC = nullptr;
  HWND parentHwnd = nullptr;

  // ── Win32 child window ────────────────────────────────────────────────────
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
    wc.lpszClassName = L"FluxGLCanvas";
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
        WS_EX_NOPARENTNOTIFY, L"FluxGLCanvas", nullptr,
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

  void renderGL() {
    if (!glRC || !glDC)
      return;
    if (wglGetCurrentContext() != glRC)
      wglMakeCurrent(glDC, glRC);

    glViewport(0, 0, width, height);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);

    if (drawFn) {
      CanvasContext ctx(width, height, glDC);
      drawFn(ctx);
    }

    SwapBuffers(glDC);
  }

  void destroyGL() {
    if (glRC) {
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

using CanvasPtr = std::shared_ptr<CanvasWidget>;

inline CanvasPtr Canvas() { return std::make_shared<CanvasWidget>(); }
inline CanvasPtr Canvas(int w, int h) {
  return std::make_shared<CanvasWidget>()->setSize(w, h);
}
inline CanvasPtr Canvas(int w, int h, CanvasWidget::DrawFn fn) {
  return std::make_shared<CanvasWidget>()->setSize(w, h)->onDraw(std::move(fn));
}

#endif // FLUX_CANVAS_HPP

// ============================================================================
// USAGE EXAMPLES
// ============================================================================
/*

// ── 1. Static drawing ───────────────────────────────────────────────────────
auto c = Canvas(600, 400, [](CanvasContext& ctx) {
    // Background
    ctx.fillStyle("#0f0e17").fillRect(0, 0, 600, 400);

    // Rounded card
    ctx.fillStyle("#1a1a2e");
    ctx.beginPath().roundRect(40, 40, 520, 160, 16).fill();

    // Title text
    ctx.fillStyle("#fffffe").font("28px", "Segoe UI");
    ctx.fillText("Hello, Canvas!", 60, 100);

    ctx.fillStyle("#a7a9be").font("14px", "Segoe UI");
    ctx.fillText("A native HTML-canvas-style widget for Flux", 60, 130);

    // Colourful circles
    float cx = 300, cy = 280;
    for (int i = 0; i < 7; i++) {
        float hue = i / 7.f;
        float r = 0.5f + hue * 0.5f, g = 1.f - hue, b = hue;
        ctx.strokeStyle("rgb(" + std::to_string((int)(r*255)) + ","
                                + std::to_string((int)(g*255)) + ","
                                + std::to_string((int)(b*255)) + ")")
           .lineWidth(2).strokeCircle(cx - 180 + i*60, cy, 22);
    }

    // Bezier curve
    ctx.strokeStyle("#ff8906").lineWidth(3)
       .beginPath()
       .moveTo(60, 360).bezierCurveTo(150, 280, 450, 340, 540, 260)
       .stroke();
});


// ── 2. Reactive canvas (animates or updates on state change) ────────────────
State<float> angle{0.f, context};

auto c = Canvas(400, 400)->onDraw([&](CanvasContext& ctx) {
    float a = angle.get();
    ctx.fillStyle("#000").fillRect(0, 0, 400, 400);

    ctx.save()
       .translate(200, 200).rotate(a)
       .fillStyle("#e94560")
       .fillRect(-50, -50, 100, 100)
       .restore();
});

// In a timer / update loop:
//   angle.set(angle.get() + 0.01f);  // Canvas repaints automatically


// ── 3. Mini chart ────────────────────────────────────────────────────────────
std::vector<float> data = {0.2f, 0.5f, 0.3f, 0.8f, 0.6f, 0.9f, 0.4f};

auto c = Canvas(500, 200, [data](CanvasContext& ctx) {
    ctx.fillStyle("#111827").fillRect(0,0,500,200);

    int n = (int)data.size();
    float bw = 500.f / n;
    for (int i = 0; i < n; i++) {
        float bh = data[i] * 160;
        ctx.fillStyle("rgba(99,179,237,0.85)")
           .fillRect(i*bw + 4, 200 - bh, bw - 8, bh);
    }
});


// ── 4. Path shapes ───────────────────────────────────────────────────────────
auto c = Canvas(400, 400, [](CanvasContext& ctx) {
    ctx.fillStyle("#1e1e1e").fillRect(0,0,400,400);

    // Star via lines
    ctx.strokeStyle("#ffd700").lineWidth(2).beginPath();
    float cx=200, cy=200, R=100, r=40;
    for (int i=0; i<10; i++) {
        float rad = (float)M_PI/2 + i*(float)M_PI/5;
        float len = (i%2==0) ? R : r;
        float px = cx + std::cos(rad)*len;
        float py = cy - std::sin(rad)*len;
        if (i==0) ctx.moveTo(px,py); else ctx.lineTo(px,py);
    }
    ctx.closePath().stroke();
});

*/