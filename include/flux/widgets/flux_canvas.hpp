#ifndef FLUX_CANVAS_HPP
#define FLUX_CANVAS_HPP

// ============================================================================
// flux_canvas.hpp  —  CanvasWidget
// ============================================================================

#include "flux_widget.hpp"

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include "flux_platform.hpp"



// GLAD must come before any other GL headers.
// Provides all OpenGL 4.6 core functions and tokens — no manual loading needed.
#include <glad/glad.h>

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
// §1  WGL EXTENSION TYPEDEFS  (only WGL ones — GL functions come from GLAD)
// ============================================================================

using PFNWGLCREATECONTEXTATTRIBSARBPROC = HGLRC(WINAPI *)(HDC, HGLRC, const int *);
using PFNWGLCHOOSEPIXELFORMATARBPROC    = BOOL(WINAPI *)(HDC, const int *, const FLOAT *, UINT, int *, UINT *);

// WGL context-creation tokens (not provided by glad.h)
#define WGL_CONTEXT_MAJOR_VERSION_ARB    0x2091
#define WGL_CONTEXT_MINOR_VERSION_ARB    0x2092
#define WGL_CONTEXT_PROFILE_MASK_ARB     0x9126
#define WGL_CONTEXT_CORE_PROFILE_BIT_ARB 0x00000001

// WGL pixel-format tokens (wglext.h values)
#define WGL_DRAW_TO_WINDOW_ARB  0x2001
#define WGL_SUPPORT_OPENGL_ARB  0x2010
#define WGL_DOUBLE_BUFFER_ARB   0x2011
#define WGL_PIXEL_TYPE_ARB      0x2013
#define WGL_TYPE_RGBA_ARB       0x202B
#define WGL_COLOR_BITS_ARB      0x2014
#define WGL_DEPTH_BITS_ARB      0x2022
#define WGL_SAMPLE_BUFFERS_ARB  0x2041
#define WGL_SAMPLES_ARB         0x2042

// ============================================================================
// §2  GL UTILITIES
// ============================================================================

namespace glutil {

inline GLuint compileShader(GLenum type, const char *src) {
  GLuint s = glCreateShader(type);
  glShaderSource(s, 1, &src, nullptr);
  glCompileShader(s);
  GLint ok = 0;
  glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char buf[1024];
    GLsizei len = 0;
    glGetShaderInfoLog(s, 1024, &len, buf);
    OutputDebugStringA("Shader error: ");
    OutputDebugStringA(buf);
    glDeleteShader(s);
    return 0;
  }
  return s;
}

inline GLuint linkProgram(const char *vert, const char *frag) {
  GLuint vs = compileShader(GL_VERTEX_SHADER, vert);
  GLuint fs = compileShader(GL_FRAGMENT_SHADER, frag);
  if (!vs || !fs) {
    if (vs) glDeleteShader(vs);
    if (fs) glDeleteShader(fs);
    return 0;
  }
  GLuint p = glCreateProgram();
  glAttachShader(p, vs);
  glAttachShader(p, fs);
  glLinkProgram(p);
  glDeleteShader(vs);
  glDeleteShader(fs);
  GLint ok = 0;
  glGetProgramiv(p, GL_LINK_STATUS, &ok);
  if (!ok) {
    char buf[1024];
    GLsizei len = 0;
    glGetProgramInfoLog(p, 1024, &len, buf);
    OutputDebugStringA("Link error: ");
    OutputDebugStringA(buf);
    glDeleteProgram(p);
    return 0;
  }
  return p;
}

// Column-major orthographic: [l,r] x [b,t] -> NDC
inline void ortho(float l, float r, float b, float t, float out[16]) {
  float rml = r - l, tmb = t - b;
  out[0]  = 2.f / rml; out[1]  = 0;          out[2]  = 0;  out[3]  = 0;
  out[4]  = 0;          out[5]  = 2.f / tmb; out[6]  = 0;  out[7]  = 0;
  out[8]  = 0;          out[9]  = 0;          out[10] = -1; out[11] = 0;
  out[12] = -(r + l) / rml;
  out[13] = -(t + b) / tmb;
  out[14] = 0;
  out[15] = 1;
}

} // namespace glutil

// ============================================================================
// §3  COLOR HELPERS
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
        return {h2f((n >> 16) & 0xFF), h2f((n >> 8) & 0xFF), h2f(n & 0xFF), 1.f};
      }
      if (h.size() == 8) {
        unsigned n = (unsigned)std::stoul(h, nullptr, 16);
        return {h2f((n >> 24) & 0xFF), h2f((n >> 16) & 0xFF),
                h2f((n >> 8) & 0xFF),  h2f(n & 0xFF)};
      }
    }
  } catch (...) {}
  static const struct { const char *n; RGBA c; } kNamed[] = {
    {"black",       {0,   0,    0,   1}},
    {"white",       {1,   1,    1,   1}},
    {"red",         {1,   0,    0,   1}},
    {"green",       {0,   .5f,  0,   1}},
    {"blue",        {0,   0,    1,   1}},
    {"transparent", {0,   0,    0,   0}},
  };
  std::string lo = css;
  for (auto &c : lo) c = (char)tolower((unsigned char)c);
  for (auto &e : kNamed) if (lo == e.n) return e.c;
  return {1, 1, 1, 1};
}

// ============================================================================
// §4  STROKE + TEXT DATA TYPES
// ============================================================================

using ToolId = int;
static constexpr ToolId kToolBrush  = 0;
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

struct TextStyle {
  std::wstring fontFace = L"Arial";
  int   fontSize = 20;
  float r = 0, g = 0, b = 0;
  bool  bold      = false;
  bool  italic    = false;
  bool  underline = false;
};

// ============================================================================
// §5  SCROLLBAR INFO
// ============================================================================

struct ScrollbarInfo {
  float thumbMin = 0.f;
  float thumbMax = 1.f;
  bool  visible  = false;
};

// ============================================================================
// §6  CUSTOM GL SCROLLBAR
// ============================================================================

class CustomScrollbar {
public:
  static constexpr float kTrackThick  = 12.f;
  static constexpr float kThumbThin   = 5.f;
  static constexpr float kThumbFat    = 8.f;
  static constexpr float kArrowSize   = 12.f;
  static constexpr float kThumbMinLen = 24.f;
  static constexpr float kCornerR     = 3.f;
  static constexpr int   kCapSegs     = 8;
  static constexpr float kIdleAlpha   = 0.15f;
  static constexpr float kActiveAlpha = 0.90f;
  static constexpr float kIdleDelay   = 1.5f;
  static constexpr float kFadeSpeed   = 3.5f;
  static constexpr float kExpandSpeed = 10.f;

  enum class Axis { Horizontal, Vertical };
  enum class Zone { None, ArrowStart, TrackBefore, Thumb, TrackAfter, ArrowEnd };

  explicit CustomScrollbar(Axis axis) : axis_(axis) {}

  void setGeometry(float stripX0, float stripY0, float stripLen) {
    stripX0_  = stripX0;
    stripY0_  = stripY0;
    stripLen_ = stripLen;
  }

  void setThumb(float thumbMin, float thumbMax, bool visible) {
    visible_  = visible;
    thumbMin_ = std::clamp(thumbMin, 0.f, 1.f);
    thumbMax_ = std::clamp(thumbMax, 0.f, 1.f);
    if (thumbMin_ > thumbMax_) std::swap(thumbMin_, thumbMax_);
  }

  bool tick(double dt) {
    float f = float(dt);
    float targetW = (hovered_ || dragging_) ? kThumbFat : kThumbThin;
    currentW_ += (targetW - currentW_) * min(1.f, kExpandSpeed * f);

    if (!visible_) {
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
      targetAlpha = (idleTimer_ < kIdleDelay) ? kActiveAlpha : kIdleAlpha;
    }
    float prev = alpha_;
    alpha_ += (targetAlpha - alpha_) * min(1.f, kFadeSpeed * f);
    return std::abs(alpha_ - prev) > 0.002f;
  }

  bool needsRedraw() const { return visible_ && alpha_ > 0.005f; }
  bool isVisible()   const { return visible_; }

  void render(GLuint prog, GLuint vao, GLuint vbo,
              GLint uMVP, GLint uColor, int glW, int glH) const {
    if (alpha_ < 0.005f) return;

    float mvp[16];
    glutil::ortho(0.f, float(glW), float(glH), 0.f, mvp);

    glUseProgram(prog);
    glUniformMatrix4fv(uMVP, 1, GL_FALSE, mvp);
    glUniform1i(glGetUniformLocation(prog, "uMode"), 1);

    {
      float ta = alpha_ * 0.25f;
      glUniform4f(uColor, 0.12f, 0.12f, 0.12f, ta);
      pushRect(vao, vbo, stripX0_, stripY0_, trackStripW(), stripLen_);
    }
    {
      glUniform4f(uColor, 0.75f, 0.75f, 0.75f, alpha_);
      drawArrow(vao, vbo, true,  glW, glH, prog, uMVP, uColor);
      drawArrow(vao, vbo, false, glW, glH, prog, uMVP, uColor);
    }
    if (thumbMax_ > thumbMin_) {
      float thumbAlpha = dragging_ ? 1.f : alpha_;
      float tc         = dragging_ ? 0.92f : 0.72f;
      glUniform4f(uColor, tc, tc, tc, thumbAlpha);
      drawThumb(vao, vbo);
    }
  }

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
    if (z == Zone::ArrowStart)  { scrollBy(-kArrowStep,              scrollTo); return true; }
    if (z == Zone::ArrowEnd)    { scrollBy(+kArrowStep,              scrollTo); return true; }
    if (z == Zone::TrackBefore) { scrollBy(-(thumbMax_ - thumbMin_), scrollTo); return true; }
    if (z == Zone::TrackAfter)  { scrollBy(+(thumbMax_ - thumbMin_), scrollTo); return true; }
    return false;
  }

  bool onMouseMove(int sx, int sy) {
    Zone z        = hitTest(sx, sy);
    bool wasHover = hovered_;
    hovered_      = (z != Zone::None);
    if (hovered_ != wasHover) idleTimer_ = 0.f;
    if (dragging_ && scrollToFn_) {
      float pos    = axis_ == Axis::Horizontal ? float(sx) : float(sy);
      float delta  = pos - dragStartPx_;
      float usable = usableLen();
      float thumbPx = (thumbMax_ - thumbMin_) * usable;
      float range   = usable - thumbPx;
      if (range > 0.f) {
        float newMin = std::clamp(dragStartMin_ + delta / range,
                                  0.f, 1.f - (thumbMax_ - thumbMin_));
        scrollToFn_(newMin);
      }
      return true;
    }
    return false;
  }

  bool onMouseUp(int, int) {
    bool was   = dragging_;
    dragging_  = false;
    idleTimer_ = 0.f;
    return was;
  }

  void onMouseLeave() { hovered_ = false; dragging_ = false; }
  void poke()         { idleTimer_ = 0.f; }

private:
  Axis  axis_;
  float stripX0_ = 0, stripY0_ = 0, stripLen_ = 100.f;
  float thumbMin_ = 0.f, thumbMax_ = 1.f;
  bool  visible_  = false;
  bool  hovered_  = false;
  bool  dragging_ = false;
  float alpha_    = kIdleAlpha;
  float idleTimer_    = 0.f;
  float currentW_     = kThumbThin;
  float dragStartPx_  = 0.f;
  float dragStartMin_ = 0.f;
  std::function<void(float)> scrollToFn_;
  static constexpr float kArrowStep = 0.05f;

  float trackStripW() const { return kTrackThick; }
  float usableLen()   const { return max(1.f, stripLen_ - kArrowSize * 2.f); }

  void thumbPixels(float &pxStart, float &pxLen) const {
    float ul   = usableLen();
    pxLen      = max(kThumbMinLen, (thumbMax_ - thumbMin_) * ul);
    float range = ul - pxLen;
    pxStart     = kArrowSize + thumbMin_ / (1.f - (thumbMax_ - thumbMin_)) * range;
    if (!std::isfinite(pxStart)) pxStart = kArrowSize;
    pxStart = std::clamp(pxStart, kArrowSize, kArrowSize + range);
  }

  Zone hitTest(int sx, int sy) const {
    if (!visible_) return Zone::None;
    float px         = axis_ == Axis::Horizontal ? float(sx) : float(sy);
    float py         = axis_ == Axis::Horizontal ? float(sy) : float(sx);
    float trackCross = axis_ == Axis::Horizontal ? stripY0_ : stripX0_;
    if (py < trackCross || py >= trackCross + kTrackThick) return Zone::None;
    float along = axis_ == Axis::Horizontal ? px - stripX0_ : px - stripY0_;
    if (along < 0 || along > stripLen_) return Zone::None;
    if (along < kArrowSize)                return Zone::ArrowStart;
    if (along > stripLen_ - kArrowSize)    return Zone::ArrowEnd;
    float ts, tl;
    thumbPixels(ts, tl);
    if (along < ts)       return Zone::TrackBefore;
    if (along < ts + tl)  return Zone::Thumb;
    return Zone::TrackAfter;
  }

  void scrollBy(float delta, std::function<void(float)> fn) {
    if (!fn) return;
    float span = thumbMax_ - thumbMin_;
    fn(std::clamp(thumbMin_ + delta, 0.f, 1.f - span));
  }

  static void pushRect(GLuint vao, GLuint vbo, float x, float y, float w, float h) {
    float v[] = {x, y,  x+w, y,  x+w, y+h,  x+w, y+h,  x, y+h,  x, y};
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(v), v, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 2, nullptr);
    glDisableVertexAttribArray(1);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
  }

  void drawThumb(GLuint vao, GLuint vbo) const {
    float ts, tl;
    thumbPixels(ts, tl);
    float halfW       = currentW_ * 0.5f;
    float crossCenter = (axis_ == Axis::Horizontal)
                        ? stripY0_ + kTrackThick * 0.5f
                        : stripX0_ + kTrackThick * 0.5f;
    const float cr   = min(kCornerR, halfW);
    float rLen       = tl - cr * 2.f;
    if (rLen > 0.f) {
      float rx, ry, rw, rh;
      if (axis_ == Axis::Horizontal) {
        rx = stripX0_ + ts + cr; ry = crossCenter - halfW;
        rw = rLen;               rh = currentW_;
      } else {
        rx = crossCenter - halfW; ry = stripY0_ + ts + cr;
        rw = currentW_;           rh = rLen;
      }
      pushRect(vao, vbo, rx, ry, rw, rh);
    }
    for (int cap = 0; cap < 2; ++cap) {
      float capAlong, capCross = crossCenter;
      if (axis_ == Axis::Horizontal)
        capAlong = (cap == 0) ? (stripX0_ + ts + cr) : (stripX0_ + ts + tl - cr);
      else
        capAlong = (cap == 0) ? (stripY0_ + ts + cr) : (stripY0_ + ts + tl - cr);
      float startAngle = (axis_ == Axis::Horizontal)
          ? (cap == 0 ?  3.14159265f * 0.5f : -3.14159265f * 0.5f)
          : (cap == 0 ?  3.14159265f         :  0.f);
      float sweep = 3.14159265f;
      std::vector<float> fan;
      fan.reserve((kCapSegs + 2) * 2);
      if (axis_ == Axis::Horizontal) { fan.push_back(capAlong); fan.push_back(capCross); }
      else                           { fan.push_back(capCross); fan.push_back(capAlong); }
      for (int i = 0; i <= kCapSegs; ++i) {
        float a  = startAngle + sweep * float(i) / kCapSegs;
        float dx = cosf(a) * cr, dy = sinf(a) * cr;
        if (axis_ == Axis::Horizontal) { fan.push_back(capAlong + dx); fan.push_back(capCross + dy); }
        else                           { fan.push_back(capCross + dx); fan.push_back(capAlong + dy); }
      }
      glBindVertexArray(vao);
      glBindBuffer(GL_ARRAY_BUFFER, vbo);
      glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(fan.size() * sizeof(float)),
                   fan.data(), GL_DYNAMIC_DRAW);
      glEnableVertexAttribArray(0);
      glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 2, nullptr);
      glDisableVertexAttribArray(1);
      glDrawArrays(GL_TRIANGLE_FAN, 0, int(fan.size() / 2));
      glBindVertexArray(0);
    }
  }

  void drawArrow(GLuint vao, GLuint vbo, bool isStart,
                 int, int, GLuint, GLint, GLint) const {
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
    float cx = ax + kArrowSize * 0.5f;
    float cy = (axis_ == Axis::Vertical) ? ay + kArrowSize * 0.5f
                                         : ay + kTrackThick * 0.5f;
    float hs = 3.f;
    float v[6];
    if (axis_ == Axis::Horizontal) {
      if (isStart) { v[0]=cx+hs; v[1]=cy-hs; v[2]=cx-hs; v[3]=cy; v[4]=cx+hs; v[5]=cy+hs; }
      else         { v[0]=cx-hs; v[1]=cy-hs; v[2]=cx+hs; v[3]=cy; v[4]=cx-hs; v[5]=cy+hs; }
    } else {
      if (isStart) { v[0]=cx-hs; v[1]=cy+hs; v[2]=cx;    v[3]=cy-hs; v[4]=cx+hs; v[5]=cy+hs; }
      else         { v[0]=cx-hs; v[1]=cy-hs; v[2]=cx;    v[3]=cy+hs; v[4]=cx+hs; v[5]=cy-hs; }
    }
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(v), v, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 2, nullptr);
    glDisableVertexAttribArray(1);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
  }
};

// ============================================================================
// §7  VIEWPORT
// ============================================================================

static constexpr float kMinZoom       = 1.f / 16.f;
static constexpr float kMaxZoom       = 32.f;
static constexpr float kSnapTolerance = 0.04f;
static constexpr std::array<float, 12> kSnapZooms = {
    0.0625f, 0.125f, 0.25f, 0.5f, 0.75f, 1.0f,
    1.5f,    2.0f,   4.0f,  8.0f, 16.0f, 32.0f};

class Viewport {
public:
  Viewport() = default;

  void init(int viewW, int viewH, int canvasW, int canvasH) {
    vw_ = float(viewW); vh_ = float(viewH);
    cw_ = float(canvasW); ch_ = float(canvasH);
    zoom_ = 1.f; offsetX_ = 0.f; offsetY_ = 0.f;
  }

  void setViewSize(int w, int h)   { vw_ = float(w); vh_ = float(h); clampOffset(); }
  void setCanvasSize(int w, int h) { cw_ = float(w); ch_ = float(h); clampOffset(); }

  void zoomToward(float sx, float sy, float factor) {
    auto [cpx, cpy] = screenToCanvas(sx, sy);
    float z = std::clamp(zoom_ * factor, kMinZoom, kMaxZoom);
    z = snapZoom(z);
    zoom_    = z;
    offsetX_ = cpx - sx / z;
    offsetY_ = cpy - (vh_ - sy) / z;
    offsetX_ = std::roundf(offsetX_ * zoom_) / zoom_;
    offsetY_ = std::roundf(offsetY_ * zoom_) / zoom_;
    clampOffset();
  }

  void zoomIn()    { zoomToward(vw_ * .5f, vh_ * .5f, 1.25f); }
  void zoomOut()   { zoomToward(vw_ * .5f, vh_ * .5f, 0.8f);  }
  void resetZoom() { zoom_ = 1.f; centerCanvas(); }
  void fitToView() {
    float f = min(vw_ / cw_, vh_ / ch_);
    zoom_ = snapZoom(std::clamp(f, kMinZoom, kMaxZoom));
    centerCanvas();
  }

  void panByScreen(float dsx, float dsy) {
    float screenOX = offsetX_ * zoom_ - dsx;
    float screenOY = offsetY_ * zoom_ + dsy;
    offsetX_ = std::roundf(screenOX) / zoom_;
    offsetY_ = std::roundf(screenOY) / zoom_;
    clampOffset();
  }

  void setOffsetX(float cx) { offsetX_ = cx; clampOffset(); }
  void setOffsetY(float cy) { offsetY_ = cy; clampOffset(); }
  void setOffset(float cx, float cy) {
    offsetX_ = std::roundf(cx * zoom_) / zoom_;
    offsetY_ = std::roundf(cy * zoom_) / zoom_;
    clampOffset();
  }

  std::pair<float, float> screenToCanvas(float sx, float sy) const {
    return {offsetX_ + sx / zoom_, offsetY_ + (vh_ - sy) / zoom_};
  }

  ScrollbarInfo scrollbarH() const {
    float view = vw_ / zoom_;
    if (view >= cw_) return {0, 1, false};
    return {std::clamp(offsetX_ / cw_, 0.f, 1.f),
            std::clamp((offsetX_ + view) / cw_, 0.f, 1.f), true};
  }

  ScrollbarInfo scrollbarV() const {
    float view = vh_ / zoom_;
    if (view >= ch_) return {0, 1, false};
    float end = offsetY_ + view;
    return {std::clamp(1.f - end / ch_,     0.f, 1.f),
            std::clamp(1.f - offsetY_ / ch_, 0.f, 1.f), true};
  }

  void buildMVP(float out[16]) const {
    float l = offsetX_, r = offsetX_ + vw_ / zoom_;
    float b = offsetY_, t = offsetY_ + vh_ / zoom_;
    glutil::ortho(l, r, b, t, out);
  }

  float zoom()    const { return zoom_;    }
  float offsetX() const { return offsetX_; }
  float offsetY() const { return offsetY_; }
  float viewW()   const { return vw_;      }
  float viewH()   const { return vh_;      }
  float canvasW() const { return cw_;      }
  float canvasH() const { return ch_;      }

private:
  float vw_ = 1, vh_ = 1, cw_ = 1, ch_ = 1;
  float zoom_ = 1.f, offsetX_ = 0.f, offsetY_ = 0.f;

  static float snapZoom(float z) {
    for (float s : kSnapZooms)
      if (std::abs(z - s) / s < kSnapTolerance) return s;
    return z;
  }

  void centerCanvas() {
    offsetX_ = (cw_ - vw_ / zoom_) * .5f;
    offsetY_ = (ch_ - vh_ / zoom_) * .5f;
    clampOffset();
  }

  void clampOffset() {
    float viewW = vw_ / zoom_, viewH = vh_ / zoom_;
    constexpr float kPanSlack = 0.5f;
    float slackX = viewW * kPanSlack, slackY = viewH * kPanSlack;
    if (viewW >= cw_)
      offsetX_ = std::clamp(offsetX_, -slackX, cw_ - viewW + slackX);
    else
      offsetX_ = std::clamp(offsetX_, 0.f, cw_ - viewW);
    if (viewH >= ch_)
      offsetY_ = std::clamp(offsetY_, -slackY, ch_ - viewH + slackY);
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
  virtual void resize(int w, int h)     = 0;
  virtual void update(double dt)        = 0;
  virtual void render(const float mvp[16]) = 0;
virtual void onMouseDown(float /*x*/, float /*y*/)      {}
virtual void onMouseMove(float /*x*/, float /*y*/)      {}
virtual void onMouseUp(float /*x*/, float /*y*/)        {}
virtual void onKeyDown(int /*key*/)                     {}
virtual void onKeyUp(int /*key*/)                       {}
virtual void onRightMouseDown(float /*x*/, float /*y*/) {}
  virtual bool needsContinuousRedraw() const      { return false; }
  virtual void destroy() = 0;
};

// ============================================================================
// §9  CANVAS WIDGET  (two-window hierarchy + custom GL scrollbars + MSAA)
// ============================================================================

class CanvasWidget : public Widget {
public:
  static constexpr float kSBThick = CustomScrollbar::kTrackThick;

  explicit CanvasWidget()
      : hBar_(CustomScrollbar::Axis::Horizontal),
        vBar_(CustomScrollbar::Axis::Vertical) {
    autoWidth = autoHeight = false;
    width  = 400;
    height = 300;
  }
  ~CanvasWidget() { destroyGL(); }

  std::shared_ptr<CanvasWidget> setViewportEnabled(bool enabled) {
    viewportEnabled_ = enabled;
    return ptr();
  }
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

  template <typename T, typename... A>
  std::shared_ptr<T> setSurface(A &&...a) {
    auto s = std::make_shared<T>(std::forward<A>(a)...);
    pendingSurface_ = s;
    return s;
  }

  RenderSurface      *getSurface()       const { return activeSurface_.get(); }
  const Viewport     &viewport()         const { return vp_; }
  Viewport           &viewport()               { return vp_; }

  std::shared_ptr<CanvasWidget> setSize(int w, int h) {
    width = w; height = h;
    autoWidth = autoHeight = false;
    markNeedsLayout();
    return ptr();
  }

  std::shared_ptr<CanvasWidget> setCanvasSize(int w, int h) {
    canvasW_ = w; canvasH_ = h;
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

  std::shared_ptr<CanvasWidget> redraw() { markNeedsPaint(); return ptr(); }

  std::function<void(float zoom)> onViewportChanged;
  std::function<void(int w, int h)> onGLResize;

  void computeLayout(GraphicsContext &/*ctx*/, const BoxConstraints &c, FontCache &) override {
    width  = c.clampWidth (autoWidth  ? c.maxWidth  : width);
    height = c.clampHeight(autoHeight ? c.maxHeight : height);
    needsLayout = false;
  }

  void render(GraphicsContext &ctx, FontCache &) override {
    ensureWindows(ctx.hdc);
    moveWindows();
    tickAndRender();
    needsPaint = false;
  }

  void onDetach() override { destroyGL(); Widget::onDetach(); }

  void markNeedsPaint() override {
    Widget::markNeedsPaint();
    if (glHwnd_ && !repaintPending_) {
      repaintPending_ = true;
      PostMessage(glHwnd_, WM_USER + 1, 0, 0);
    }
  }

private:
  bool viewportEnabled_  = true;
  bool scrollbarsEnabled_ = true;

  std::shared_ptr<CanvasWidget> ptr() {
    return std::static_pointer_cast<CanvasWidget>(shared_from_this());
  }

  std::shared_ptr<RenderSurface> activeSurface_, pendingSurface_;
  Viewport vp_;
  int canvasW_ = 512, canvasH_ = 512;

  CustomScrollbar hBar_, vBar_;
  GLuint sbVAO_   = 0, sbVBO_ = 0, sbProg_ = 0;
  GLint  sbUMVP_  = -1, sbUColor_ = -1, sbUMode_ = -1;

  using Clock = std::chrono::steady_clock;
  Clock::time_point lastTick_ = Clock::now();

  HWND  frameHwnd_  = nullptr;
  HWND  glHwnd_     = nullptr;
  HDC   glDC_       = nullptr;
  HGLRC glRC_       = nullptr;
  HWND  parentHwnd_ = nullptr;

  bool repaintPending_ = false;
  bool trackingLeave_  = false;
  int  lastGLW_ = 0, lastGLH_ = 0;

  bool  panning_    = false;
  int   panStartSX_ = 0, panStartSY_ = 0;
  float panStartOX_ = 0, panStartOY_ = 0;

  // ── Viewport helpers ──────────────────────────────────────────────────────

  void viewportDims(int glW, int glH, int &vpW, int &vpH) const {
    if (!viewportEnabled_ || !scrollbarsEnabled_) { vpW = glW; vpH = glH; return; }
    ScrollbarInfo h = vp_.scrollbarH(), v = vp_.scrollbarV();
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

  void updateSBGeometry(int glW, int glH) {
    ScrollbarInfo h = vp_.scrollbarH(), v = vp_.scrollbarV();
    float hLen = float(glW) - (v.visible ? kSBThick : 0.f);
    hBar_.setGeometry(0.f, float(glH) - kSBThick, hLen);
    hBar_.setThumb(h.thumbMin, h.thumbMax,
                   h.visible && scrollbarsEnabled_ && viewportEnabled_);
    float vLen = float(glH) - (h.visible ? kSBThick : 0.f);
    vBar_.setGeometry(float(glW) - kSBThick, 0.f, vLen);
    vBar_.setThumb(v.thumbMin, v.thumbMax,
                   v.visible && scrollbarsEnabled_ && viewportEnabled_);
  }

  // ── Pan helpers ───────────────────────────────────────────────────────────

  void beginPan(int sx, int sy) {
    panning_    = true;
    panStartSX_ = sx; panStartSY_ = sy;
    panStartOX_ = vp_.offsetX();
    panStartOY_ = vp_.offsetY();
  }

  void continuePan(int sx, int sy) {
    float dx = float(sx - panStartSX_), dy = float(sy - panStartSY_);
    vp_.setOffset(panStartOX_ - dx / vp_.zoom(),
                  panStartOY_ + dy / vp_.zoom());
    pokeScrollbars();
    scheduleRepaint();
  }

  void pokeScrollbars() { hBar_.poke(); vBar_.poke(); }

  void scheduleRepaint() {
    if (onViewportChanged) onViewportChanged(vp_.zoom());
    if (!repaintPending_) {
      repaintPending_ = true;
      PostMessage(glHwnd_, WM_USER + 1, 0, 0);
    }
  }

  // ── Scrollbar callbacks ───────────────────────────────────────────────────

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
    float t = 1.f - thumbMin - span;
    vp_.setOffsetY(t / (1.f - span) * range);
    pokeScrollbars();
    scheduleRepaint();
  }

  // ── Scrollbar GL resources ────────────────────────────────────────────────

  void buildSBResources() {
    const char *vert = R"GLSL(
#version 460 core
layout(location=0) in vec2 aPos;
uniform mat4 uMVP;
void main(){ gl_Position = uMVP * vec4(aPos, 0.0, 1.0); }
)GLSL";
    const char *frag = R"GLSL(
#version 460 core
uniform vec4 uColor;
uniform int  uMode;
out vec4 fragColor;
void main(){ fragColor = uColor; }
)GLSL";
    sbProg_ = glutil::linkProgram(vert, frag);
    assert(sbProg_);
    sbUMVP_   = glGetUniformLocation(sbProg_, "uMVP");
    sbUColor_ = glGetUniformLocation(sbProg_, "uColor");
    sbUMode_  = glGetUniformLocation(sbProg_, "uMode");

    constexpr GLsizeiptr kSBBufSz = sizeof(float) * 2 * 64;
    glGenVertexArrays(1, &sbVAO_);
    glGenBuffers(1, &sbVBO_);
    glBindVertexArray(sbVAO_);
    glBindBuffer(GL_ARRAY_BUFFER, sbVBO_);
    glBufferData(GL_ARRAY_BUFFER, kSBBufSz, nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 2, nullptr);
    glBindVertexArray(0);
  }

  void destroySBResources() {
    if (sbProg_) { glDeleteProgram(sbProg_);          sbProg_ = 0; }
    if (sbVAO_)  { glDeleteVertexArrays(1, &sbVAO_);  sbVAO_  = 0; }
    if (sbVBO_)  { glDeleteBuffers(1, &sbVBO_);        sbVBO_  = 0; }
  }

  // ── Window procs ──────────────────────────────────────────────────────────

  static LRESULT CALLBACK FrameProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto *self = reinterpret_cast<CanvasWidget *>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
      PAINTSTRUCT ps; BeginPaint(hwnd, &ps); EndPaint(hwnd, &ps);
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

  static LRESULT CALLBACK GLProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto *self = reinterpret_cast<CanvasWidget *>(
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

    case WM_LBUTTONDOWN: {
      SetCapture(hwnd); SetFocus(hwnd);
      if (!self) return 0;
      int sx = GET_X_LPARAM(lp), sy = GET_Y_LPARAM(lp);
      bool hC = self->hBar_.onMouseDown(sx, sy,
          [self](float t){ self->applyHScrollFraction(t); });
      bool vC = !hC && self->vBar_.onMouseDown(sx, sy,
          [self](float t){ self->applyVScrollFraction(t); });
      if (!hC && !vC) {
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
      bool hR = self->hBar_.onMouseUp(sx, sy);
      bool vR = self->vBar_.onMouseUp(sx, sy);
      if (!hR && !vR) {
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

    case WM_RBUTTONDOWN: {
      SetCapture(hwnd);
      if (self && self->activeSurface_) {
        auto [cx, cy] = self->vp_.screenToCanvas(
            float(GET_X_LPARAM(lp)), float(GET_Y_LPARAM(lp)));
        self->activeSurface_->onRightMouseDown(cx, cy);
      }
      forwardToParent(hwnd, msg, wp, lp);
      return 0;
    }
    case WM_RBUTTONUP: {
      ReleaseCapture();
      if (self && self->activeSurface_) {
        auto [cx, cy] = self->vp_.screenToCanvas(
            float(GET_X_LPARAM(lp)), float(GET_Y_LPARAM(lp)));
        self->activeSurface_->onMouseUp(cx, cy);
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
      bool hDrag = self->hBar_.onMouseMove(sx, sy);
      bool vDrag = self->vBar_.onMouseMove(sx, sy);
      self->scheduleRepaint();
      if (hDrag || vDrag) return 0;
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

    case WM_MOUSEWHEEL: {
      if (!self || !self->viewportEnabled_) return 0;
      int   delta = GET_WHEEL_DELTA_WPARAM(wp);
      bool  ctrl  = (GET_KEYSTATE_WPARAM(wp) & MK_CONTROL) != 0;
      bool  shift = (GET_KEYSTATE_WPARAM(wp) & MK_SHIFT)   != 0;
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
      if (self) { self->repaintPending_ = false; self->tickAndRender(); }
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
    wf.cbSize       = sizeof(wf);
    wf.lpfnWndProc  = FrameProc;
    wf.hInstance    = hi;
    wf.lpszClassName = L"FluxGLFrame9";
    wf.style        = CS_HREDRAW | CS_VREDRAW;
    RegisterClassExW(&wf);
    WNDCLASSEXW wg{};
    wg.cbSize       = sizeof(wg);
    wg.lpfnWndProc  = GLProc;
    wg.hInstance    = hi;
    wg.lpszClassName = L"FluxGLCanvas9";
    wg.style        = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
    RegisterClassExW(&wg);
    done = true;
  }

  // ── MSAA pixel-format upgrade ─────────────────────────────────────────────

  bool tryUpgradeToMSAA(int winW, int winH, HGLRC bootstrapCtx) {
    auto wglCPF = reinterpret_cast<PFNWGLCHOOSEPIXELFORMATARBPROC>(
        wglGetProcAddress("wglChoosePixelFormatARB"));
    if (!wglCPF) return false;

    for (int samples : {8, 4, 2}) {
      const int attribs[] = {
        WGL_DRAW_TO_WINDOW_ARB, 1,
        WGL_SUPPORT_OPENGL_ARB, 1,
        WGL_DOUBLE_BUFFER_ARB,  1,
        WGL_PIXEL_TYPE_ARB,     WGL_TYPE_RGBA_ARB,
        WGL_COLOR_BITS_ARB,     32,
        WGL_DEPTH_BITS_ARB,     24,
        WGL_SAMPLE_BUFFERS_ARB, 1,
        WGL_SAMPLES_ARB,        samples,
        0
      };
      int fmt = 0; UINT numFormats = 0;
      if (!wglCPF(glDC_, attribs, nullptr, 1, &fmt, &numFormats) || !numFormats)
        continue;

      wglMakeCurrent(nullptr, nullptr);
      wglDeleteContext(bootstrapCtx);
      ReleaseDC(glHwnd_, glDC_);
      DestroyWindow(glHwnd_);

      glHwnd_ = CreateWindowExW(
          WS_EX_NOPARENTNOTIFY, L"FluxGLCanvas9", nullptr,
          WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
          0, 0, winW, winH, frameHwnd_, nullptr,
          GetModuleHandle(nullptr), nullptr);
      assert(glHwnd_);
      SetWindowLongPtrW(glHwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

      glDC_ = GetDC(glHwnd_);
      PIXELFORMATDESCRIPTOR pfd2{};
      pfd2.nSize = sizeof(pfd2); pfd2.nVersion = 1;
      SetPixelFormat(glDC_, fmt, &pfd2);

      HGLRC newBootstrap = wglCreateContext(glDC_);
      wglMakeCurrent(glDC_, newBootstrap);

      char dbg[64];
      _snprintf_s(dbg, sizeof(dbg), _TRUNCATE,
                  "FluxCanvas: MSAA %dx enabled\n", samples);
      OutputDebugStringA(dbg);
      return true;
    }
    return false;
  }

  // ── GL context creation + GLAD init ──────────────────────────────────────

  void ensureWindows(HDC parentDC) {
    if (frameHwnd_) return;
    HWND owner = WindowFromDC(parentDC);
    if (!owner) owner = GetActiveWindow();
    parentHwnd_ = owner;
    registerClasses();

    frameHwnd_ = CreateWindowExW(
        WS_EX_NOPARENTNOTIFY, L"FluxGLFrame9", nullptr,
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
        x, y, width, height, owner, nullptr,
        GetModuleHandle(nullptr), nullptr);
    assert(frameHwnd_);
    SetWindowLongPtrW(frameHwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

    glHwnd_ = CreateWindowExW(
        WS_EX_NOPARENTNOTIFY, L"FluxGLCanvas9", nullptr,
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
        0, 0, width, height, frameHwnd_, nullptr,
        GetModuleHandle(nullptr), nullptr);
    assert(glHwnd_);
    SetWindowLongPtrW(glHwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

    // ── Bootstrap context (needed to query WGL extensions) ────────────────
    glDC_ = GetDC(glHwnd_);
    setupPixelFormat(glDC_);
    HGLRC tmp = wglCreateContext(glDC_);
    wglMakeCurrent(glDC_, tmp);

    // ── Optional MSAA upgrade (recreates window + bootstrap ctx) ──────────
    tryUpgradeToMSAA(width, height, tmp);
    HGLRC currentBootstrap = wglGetCurrentContext();

    // ── Create GL 4.6 core profile context, fall back to 4.5 then 3.3 ─────
    auto wglCA = reinterpret_cast<PFNWGLCREATECONTEXTATTRIBSARBPROC>(
        wglGetProcAddress("wglCreateContextAttribsARB"));

    auto tryContext = [&](int major, int minor) -> HGLRC {
      if (!wglCA) return nullptr;
      const int att[] = {
        WGL_CONTEXT_MAJOR_VERSION_ARB, major,
        WGL_CONTEXT_MINOR_VERSION_ARB, minor,
        WGL_CONTEXT_PROFILE_MASK_ARB,  WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
        0
      };
      return wglCA(glDC_, nullptr, att);
    };

    HGLRC core = tryContext(4, 6);
    if (!core)  core = tryContext(4, 5);
    if (!core)  core = tryContext(3, 3);

    if (core) {
      wglMakeCurrent(nullptr, nullptr);
      wglDeleteContext(currentBootstrap);
      glRC_ = core;
    } else {
      glRC_ = currentBootstrap;
    }
    wglMakeCurrent(glDC_, glRC_);

    // ── GLAD: loads every GL function pointer for the active context ───────
    if (!gladLoadGL()) {
      OutputDebugStringA("GLAD: gladLoadGL() failed — no GL function pointers loaded!\n");
      assert(false && "gladLoadGL failed");
    }

    {
      char info[256];
      _snprintf_s(info, sizeof(info), _TRUNCATE,
          "OpenGL %s  |  GLSL %s  |  %s\n",
          (const char *)glGetString(GL_VERSION),
          (const char *)glGetString(GL_SHADING_LANGUAGE_VERSION),
          (const char *)glGetString(GL_RENDERER));
      OutputDebugStringA(info);
    }

    // ── GL 4.3 debug output (Debug builds only) ───────────────────────────
#ifndef NDEBUG
    glEnable(GL_DEBUG_OUTPUT);
    glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
glDebugMessageCallback(
    [](GLenum source, GLenum type, GLuint msgId, GLenum severity,
       GLsizei /*length*/, const GLchar *message, const void * /*userParam*/)
    {
      if (severity == GL_DEBUG_SEVERITY_NOTIFICATION) return;
      const char *sevStr =
          severity == GL_DEBUG_SEVERITY_HIGH   ? "HIGH"   :
          severity == GL_DEBUG_SEVERITY_MEDIUM ? "MEDIUM" :
          severity == GL_DEBUG_SEVERITY_LOW    ? "LOW"    : "NOTIFY";
      char buf[512];
      _snprintf_s(buf, sizeof(buf), _TRUNCATE,
          "[GL %s] src=0x%04X type=0x%04X id=%u: %s\n",
          sevStr, source, type, msgId, message);
      OutputDebugStringA(buf);
      if (type == GL_DEBUG_TYPE_ERROR) DebugBreak();
    },
    nullptr);
    // Silence driver spam notifications
    glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE,
                          GL_DEBUG_SEVERITY_NOTIFICATION,
                          0, nullptr, GL_FALSE);
#endif

    vp_.init(width, height, canvasW_, canvasH_);
    lastGLW_ = width;
    lastGLH_ = height;
    updateViewportSize(width, height);
    updateSBGeometry(width, height);
    vp_.fitToView();

    buildSBResources();
    activatePendingSurface();
    lastTick_ = Clock::now();
    if (onViewportChanged) onViewportChanged(vp_.zoom());
    if (onGLResize)        onGLResize(width, height);
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
    vp_.setCanvasSize(canvasW_, canvasH_);
  }

  void tickAndRender() {
    if (!glRC_ || !glDC_) return;
    if (pendingSurface_)  activatePendingSurface();
    if (!activeSurface_)  return;
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

    glEnable(GL_MULTISAMPLE);

    float mvp[16];
    vp_.buildMVP(mvp);
    activeSurface_->render(mvp);

    updateSBGeometry(glW, glH);
    bool hFade = hBar_.tick(dt);
    bool vFade = vBar_.tick(dt);

    if (hBar_.needsRedraw() || vBar_.needsRedraw()) {
      glEnable(GL_BLEND);
      glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
      glDisable(GL_SCISSOR_TEST);
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
      glViewport(0, 0, glW, glH);

      hBar_.render(sbProg_, sbVAO_, sbVBO_, sbUMVP_, sbUColor_, glW, glH);
      vBar_.render(sbProg_, sbVAO_, sbVBO_, sbUMVP_, sbUColor_, glW, glH);

      if (hBar_.isVisible() && vBar_.isVisible() && scrollbarsEnabled_) {
        float mvpCorner[16];
        glutil::ortho(0.f, float(glW), float(glH), 0.f, mvpCorner);
        glUseProgram(sbProg_);
        glUniformMatrix4fv(sbUMVP_, 1, GL_FALSE, mvpCorner);
        glUniform4f(sbUColor_, 0.12f, 0.12f, 0.12f, 0.35f);
        float cx = float(glW) - kSBThick, cy = float(glH) - kSBThick;
        float cv[] = {
          cx,           cy,
          cx + kSBThick, cy,
          cx + kSBThick, cy + kSBThick,
          cx + kSBThick, cy + kSBThick,
          cx,            cy + kSBThick,
          cx,            cy
        };
        glBindVertexArray(sbVAO_);
        glBindBuffer(GL_ARRAY_BUFFER, sbVBO_);
        glBufferData(GL_ARRAY_BUFFER, sizeof(cv), cv, GL_DYNAMIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 2, nullptr);
        glDisableVertexAttribArray(1);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindVertexArray(0);
      }
      glDisable(GL_BLEND);
    }

    SwapBuffers(glDC_);

    bool needsCont = activeSurface_->needsContinuousRedraw() || hFade || vFade;
    if (needsCont && !repaintPending_) {
      repaintPending_ = true;
      SetTimer(glHwnd_, 1, 16, nullptr);
    }
  }

  void moveWindows() {
    if (!frameHwnd_) return;
    SetWindowPos(frameHwnd_, nullptr, x, y, width, height,
                 SWP_NOZORDER | SWP_NOACTIVATE);
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
      if (activeSurface_) { activeSurface_->destroy(); activeSurface_.reset(); }
      pendingSurface_.reset();
      wglMakeCurrent(nullptr, nullptr);
      wglDeleteContext(glRC_);
      glRC_ = nullptr;
    }
    if (glHwnd_ && glDC_) { ReleaseDC(glHwnd_, glDC_); glDC_ = nullptr; }
    if (glHwnd_)  { DestroyWindow(glHwnd_);   glHwnd_  = nullptr; }
    if (frameHwnd_){ DestroyWindow(frameHwnd_); frameHwnd_ = nullptr; }
  }
};

// ============================================================================
// §10  FACTORY HELPERS
// ============================================================================

inline std::shared_ptr<CanvasWidget> Canvas() {
  return std::make_shared<CanvasWidget>();
}
inline std::shared_ptr<CanvasWidget> Canvas(int w, int h) {
  return std::make_shared<CanvasWidget>()->setSize(w, h);
}

#endif // FLUX_CANVAS_HPP