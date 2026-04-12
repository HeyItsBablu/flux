// flux/platform/win32/flux_canvas_win32.hpp
#pragma once
#ifndef _WIN32
  #error "flux_canvas_win32.hpp must only be compiled on Win32"
#endif

#include "../../flux_platform.hpp"
#include "../../flux_widget.hpp"
#include "../../flux_keys.hpp"
#include "../../flux_core.hpp"

#include <glad/glad.h>         

#include "../../flux_glutil.hpp"    
#include "../../flux_scrollbar.hpp" 
#include "../../flux_render_surface.hpp"
#include "../../flux_canvas_types.hpp"
#include "flux_wgl.hpp"




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

class CanvasWidget : public Widget {
public:
  static constexpr float kSBThick = CustomScrollbar::kTrackThick;

  explicit CanvasWidget()
      : hBar_(CustomScrollbar::Axis::Horizontal),
        vBar_(CustomScrollbar::Axis::Vertical) {
    autoWidth = autoHeight = false;
    width = 400;
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
      updateViewportSize(lastGLW_, lastGLH_);
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

  std::function<void(float zoom)> onViewportChanged;
  std::function<void(int w, int h)> onGLResize;

  void computeLayout(GraphicsContext & /*ctx*/, const BoxConstraints &c,
                     FontCache &) override {
    width = c.clampWidth(autoWidth ? c.maxWidth : width);
    height = c.clampHeight(autoHeight ? c.maxHeight : height);
    needsLayout = false;
  }

  void render(GraphicsContext &ctx, FontCache &) override {
    ensureWindows(ctx);
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
  bool scrollbarsEnabled_ = true;

  std::shared_ptr<CanvasWidget> ptr() {
    return std::static_pointer_cast<CanvasWidget>(shared_from_this());
  }

  std::shared_ptr<RenderSurface> activeSurface_, pendingSurface_;
  Viewport vp_;
  int canvasW_ = 512, canvasH_ = 512;

  CustomScrollbar hBar_, vBar_;
  GLuint sbVAO_ = 0, sbVBO_ = 0, sbProg_ = 0;
  GLint sbUMVP_ = -1, sbUColor_ = -1, sbUMode_ = -1;

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

  // ── Viewport helpers ──────────────────────────────────────────────────────

  void viewportDims(int glW, int glH, int &vpW, int &vpH) const {
    if (!viewportEnabled_ || !scrollbarsEnabled_) {
      vpW = glW;
      vpH = glH;
      return;
    }
    ScrollbarInfo h = vp_.scrollbarH(), v = vp_.scrollbarV();
    vpW = glW - (v.visible ? (int)kSBThick : 0);
    vpH = glH - (h.visible ? (int)kSBThick : 0);
    if (vpW < 1)
      vpW = 1;
    if (vpH < 1)
      vpH = 1;
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
    panning_ = true;
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

  // ── Scrollbar callbacks ───────────────────────────────────────────────────

  void applyHScrollFraction(float thumbMin) {
    float span = vp_.scrollbarH().thumbMax - vp_.scrollbarH().thumbMin;
    float range = vp_.canvasW() - vp_.viewW() / vp_.zoom();
    if (range <= 0.f)
      return;
    vp_.setOffsetX(thumbMin / (1.f - span) * range);
    pokeScrollbars();
    scheduleRepaint();
  }

  void applyVScrollFraction(float thumbMin) {
    float span = vp_.scrollbarV().thumbMax - vp_.scrollbarV().thumbMin;
    float range = vp_.canvasH() - vp_.viewH() / vp_.zoom();
    if (range <= 0.f)
      return;
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
    sbUMVP_ = glGetUniformLocation(sbProg_, "uMVP");
    sbUColor_ = glGetUniformLocation(sbProg_, "uColor");
    sbUMode_ = glGetUniformLocation(sbProg_, "uMode");

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
    if (sbProg_) {
      glDeleteProgram(sbProg_);
      sbProg_ = 0;
    }
    if (sbVAO_) {
      glDeleteVertexArrays(1, &sbVAO_);
      sbVAO_ = 0;
    }
    if (sbVBO_) {
      glDeleteBuffers(1, &sbVBO_);
      sbVBO_ = 0;
    }
  }

  // ── Window procs ──────────────────────────────────────────────────────────

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
      SetFocus(hwnd);
      if (!self)
        return 0;
      int sx = GET_X_LPARAM(lp), sy = GET_Y_LPARAM(lp);
      bool hC = self->hBar_.onMouseDown(
          sx, sy, [self](float t) { self->applyHScrollFraction(t); });
      bool vC = !hC && self->vBar_.onMouseDown(sx, sy, [self](float t) {
        self->applyVScrollFraction(t);
      });
      if (!hC && !vC) {
        if (self->viewportEnabled_ && (GetKeyState(Key::Space) & 0x8000)) {
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
      if (!self)
        return 0;
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
      bool hDrag = self->hBar_.onMouseMove(sx, sy);
      bool vDrag = self->vBar_.onMouseMove(sx, sy);
      self->scheduleRepaint();
      if (hDrag || vDrag)
        return 0;
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
        self->vp_.panByScreen(0.f, -(float(delta) / WHEEL_DELTA) * 40.f);
      }
      self->pokeScrollbars();
      self->scheduleRepaint();
      forwardToParent(hwnd, msg, wp, lp);
      return 0;
    }

    case WM_KEYDOWN: {
      if (!self)
        return 0;
      bool ctrl = (GetKeyState(Key::Control) & 0x8000) != 0;
      bool consumed = false;
      if (ctrl && self->viewportEnabled_) {
        if (wp == Key::OemPlus || wp == Key::NumpadPlus) {
          self->vp_.zoomIn();
          self->pokeScrollbars();
          self->scheduleRepaint();
          consumed = true;
        } else if (wp == Key::OemMinus || wp == Key::NumpadMinus) {
          self->vp_.zoomOut();
          self->pokeScrollbars();
          self->scheduleRepaint();
          consumed = true;
        } else if (wp == '0') {
          self->vp_.resetZoom();
          self->pokeScrollbars();
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
    wf.lpszClassName = L"FluxGLFrame9";
    wf.style = CS_HREDRAW | CS_VREDRAW;
    RegisterClassExW(&wf);
    WNDCLASSEXW wg{};
    wg.cbSize = sizeof(wg);
    wg.lpfnWndProc = GLProc;
    wg.hInstance = hi;
    wg.lpszClassName = L"FluxGLCanvas9";
    wg.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
    RegisterClassExW(&wg);
    done = true;
  }

  // ── MSAA pixel-format upgrade ─────────────────────────────────────────────

  bool tryUpgradeToMSAA(int winW, int winH, HGLRC bootstrapCtx) {
    auto wglCPF = reinterpret_cast<PFNWGLCHOOSEPIXELFORMATARBPROC>(
        wglGetProcAddress("wglChoosePixelFormatARB"));
    if (!wglCPF)
      return false;

    for (int samples : {8, 4, 2}) {
      const int attribs[] = {WGL_DRAW_TO_WINDOW_ARB,
                             1,
                             WGL_SUPPORT_OPENGL_ARB,
                             1,
                             WGL_DOUBLE_BUFFER_ARB,
                             1,
                             WGL_PIXEL_TYPE_ARB,
                             WGL_TYPE_RGBA_ARB,
                             WGL_COLOR_BITS_ARB,
                             32,
                             WGL_DEPTH_BITS_ARB,
                             24,
                             WGL_SAMPLE_BUFFERS_ARB,
                             1,
                             WGL_SAMPLES_ARB,
                             samples,
                             0};
      int fmt = 0;
      UINT numFormats = 0;
      if (!wglCPF(glDC_, attribs, nullptr, 1, &fmt, &numFormats) || !numFormats)
        continue;

      wglMakeCurrent(nullptr, nullptr);
      wglDeleteContext(bootstrapCtx);
      ReleaseDC(glHwnd_, glDC_);
      DestroyWindow(glHwnd_);

      glHwnd_ = CreateWindowExW(
          WS_EX_NOPARENTNOTIFY, L"FluxGLCanvas9", nullptr,
          WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN, 0, 0, winW,
          winH, frameHwnd_, nullptr, GetModuleHandle(nullptr), nullptr);
      assert(glHwnd_);
      SetWindowLongPtrW(glHwnd_, GWLP_USERDATA,
                        reinterpret_cast<LONG_PTR>(this));

      glDC_ = GetDC(glHwnd_);
      PIXELFORMATDESCRIPTOR pfd2{};
      pfd2.nSize = sizeof(pfd2);
      pfd2.nVersion = 1;
      SetPixelFormat(glDC_, fmt, &pfd2);

      HGLRC newBootstrap = wglCreateContext(glDC_);
      wglMakeCurrent(glDC_, newBootstrap);

      char dbg[64];
      _snprintf_s(dbg, sizeof(dbg), _TRUNCATE, "FluxCanvas: MSAA %dx enabled\n",
                  samples);
      OutputDebugStringA(dbg);
      return true;
    }
    return false;
  }

  // ── GL context creation + GLAD init ──────────────────────────────────────

  void ensureWindows(GraphicsContext &ctx) {
    (void)ctx; // ctx no longer used inside
    if (frameHwnd_)
      return;
    parentHwnd_ = FluxUI::getCurrentInstance()->getWindow();
    if (!parentHwnd_)
      return;
    registerClasses();

    frameHwnd_ = CreateWindowExW(
        WS_EX_NOPARENTNOTIFY, L"FluxGLFrame9", nullptr,
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN, x, y, width,
        height, parentHwnd_, nullptr, GetModuleHandle(nullptr), nullptr);
    assert(frameHwnd_);
    SetWindowLongPtrW(frameHwnd_, GWLP_USERDATA,
                      reinterpret_cast<LONG_PTR>(this));

    glHwnd_ = CreateWindowExW(
        WS_EX_NOPARENTNOTIFY, L"FluxGLCanvas9", nullptr,
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN, 0, 0, width,
        height, frameHwnd_, nullptr, GetModuleHandle(nullptr), nullptr);
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
      if (!wglCA)
        return nullptr;
      const int att[] = {WGL_CONTEXT_MAJOR_VERSION_ARB,
                         major,
                         WGL_CONTEXT_MINOR_VERSION_ARB,
                         minor,
                         WGL_CONTEXT_PROFILE_MASK_ARB,
                         WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
                         0};
      return wglCA(glDC_, nullptr, att);
    };

    HGLRC core = tryContext(4, 6);
    if (!core)
      core = tryContext(4, 5);
    if (!core)
      core = tryContext(3, 3);

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
      OutputDebugStringA(
          "GLAD: gladLoadGL() failed — no GL function pointers loaded!\n");
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
           GLsizei /*length*/, const GLchar *message,
           const void * /*userParam*/) {
          if (severity == GL_DEBUG_SEVERITY_NOTIFICATION)
            return;
          const char *sevStr = severity == GL_DEBUG_SEVERITY_HIGH     ? "HIGH"
                               : severity == GL_DEBUG_SEVERITY_MEDIUM ? "MEDIUM"
                               : severity == GL_DEBUG_SEVERITY_LOW ? "LOW"
                                                                   : "NOTIFY";
          char buf[512];
          _snprintf_s(buf, sizeof(buf), _TRUNCATE,
                      "[GL %s] src=0x%04X type=0x%04X id=%u: %s\n", sevStr,
                      source, type, msgId, message);
          OutputDebugStringA(buf);
          if (type == GL_DEBUG_TYPE_ERROR)
            DebugBreak();
        },
        nullptr);
    // Silence driver spam notifications
    glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE,
                          GL_DEBUG_SEVERITY_NOTIFICATION, 0, nullptr, GL_FALSE);
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
    if (onViewportChanged)
      onViewportChanged(vp_.zoom());
    if (onGLResize)
      onGLResize(width, height);
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

    int glW = lastGLW_ < 1 ? 1 : lastGLW_;
    int glH = lastGLH_ < 1 ? 1 : lastGLH_;
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
        float cv[] = {cx,
                      cy,
                      cx + kSBThick,
                      cy,
                      cx + kSBThick,
                      cy + kSBThick,
                      cx + kSBThick,
                      cy + kSBThick,
                      cx,
                      cy + kSBThick,
                      cx,
                      cy};
        glBindVertexArray(sbVAO_);
        glBindBuffer(GL_ARRAY_BUFFER, sbVBO_);
        glBufferData(GL_ARRAY_BUFFER, sizeof(cv), cv, GL_DYNAMIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 2,
                              nullptr);
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
    if (!frameHwnd_)
      return;
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
      if (onGLResize)
        onGLResize(width, height);
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
// §10  FACTORY HELPERS
// ============================================================================

inline std::shared_ptr<CanvasWidget> Canvas() {
  return std::make_shared<CanvasWidget>();
}
inline std::shared_ptr<CanvasWidget> Canvas(int w, int h) {
  return std::make_shared<CanvasWidget>()->setSize(w, h);
}
