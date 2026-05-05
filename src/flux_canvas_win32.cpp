// flux_canvas_win32.cpp
// Compiled only on Win32.
#ifdef _WIN32

#include "flux/flux_canvas.hpp"
#include "flux/flux_wgl.hpp"

#include <cassert>
#include <cstdio>
#include <glad/glad.h>
#include <unordered_map>

// ─────────────────────────────────────────────────────────────────────────────
// Scrollbar shaders (GL 3.3 core)
// ─────────────────────────────────────────────────────────────────────────────

static const char *kSBVert = R"GLSL(
#version 330 core
layout(location=0) in vec2 aPos;
uniform mat4 uMVP;
void main(){ gl_Position = uMVP * vec4(aPos, 0.0, 1.0); }
)GLSL";

static const char *kSBFrag = R"GLSL(
#version 330 core
out vec4 fragColor;
uniform vec4 uColor;
void main(){ fragColor = uColor; }
)GLSL";

// ─────────────────────────────────────────────────────────────────────────────
// Win32-only state
// ─────────────────────────────────────────────────────────────────────────────

struct Win32CanvasState {
  HWND frameHwnd = nullptr;
  HWND glHwnd = nullptr;
  HDC glDC = nullptr;
  HGLRC glRC = nullptr;
  HWND parentHwnd = nullptr;

  bool repaintPending = false;
  bool trackingLeave = false;
  int lastGLW = 0;
  int lastGLH = 0;
};

static std::unordered_map<CanvasWidget *, Win32CanvasState> s_win32State;

static Win32CanvasState &win32State(CanvasWidget *w) { return s_win32State[w]; }

// ─────────────────────────────────────────────────────────────────────────────
// Forward declarations for window procedures
// ─────────────────────────────────────────────────────────────────────────────

static LRESULT CALLBACK FrameProc(HWND, UINT, WPARAM, LPARAM);
static LRESULT CALLBACK GLProc(HWND, UINT, WPARAM, LPARAM);

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

static void makeCurrent(CanvasWidget *w) {
  auto &s = win32State(w);
  if (wglGetCurrentContext() != s.glRC)
    wglMakeCurrent(s.glDC, s.glRC);
}

static void scheduleRepaint(CanvasWidget *w) {
  if (w->onViewportChanged)
    w->onViewportChanged(w->vp_.zoom());
  auto &s = win32State(w);
  if (!s.repaintPending) {
    s.repaintPending = true;
    PostMessage(s.glHwnd, WM_USER + 1, 0, 0);
  }
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

static void setupPixelFormat(HDC dc) {
  PIXELFORMATDESCRIPTOR pfd{};
  pfd.nSize = sizeof(pfd);
  pfd.nVersion = 1;
  pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
  pfd.iPixelType = PFD_TYPE_RGBA;
  pfd.cColorBits = 32;
  pfd.cDepthBits = 24;
  pfd.cStencilBits = 8;
  pfd.iLayerType = PFD_MAIN_PLANE;
  SetPixelFormat(dc, ChoosePixelFormat(dc, &pfd), &pfd);
}

static bool tryUpgradeToMSAA(CanvasWidget *w, int winW, int winH,
                             HGLRC bootstrapCtx) {
  auto &s = win32State(w);
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
                           WGL_STENCIL_BITS_ARB,
                           8,
                           WGL_SAMPLE_BUFFERS_ARB,
                           1,
                           WGL_SAMPLES_ARB,
                           samples,
                           0};
    int fmt = 0;
    UINT numFormats = 0;
    if (!wglCPF(s.glDC, attribs, nullptr, 1, &fmt, &numFormats) || !numFormats)
      continue;
    wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext(bootstrapCtx);
    ReleaseDC(s.glHwnd, s.glDC);
    DestroyWindow(s.glHwnd);
    s.glHwnd = CreateWindowExW(
        WS_EX_NOPARENTNOTIFY, L"FluxGLCanvas9", nullptr,
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN, 0, 0, winW,
        winH, s.frameHwnd, nullptr, GetModuleHandle(nullptr), nullptr);
    assert(s.glHwnd);
    SetWindowLongPtrW(s.glHwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(w));
    s.glDC = GetDC(s.glHwnd);
    PIXELFORMATDESCRIPTOR pfd2{};
    pfd2.nSize = sizeof(pfd2);
    pfd2.nVersion = 1;
    SetPixelFormat(s.glDC, fmt, &pfd2);
    HGLRC newBootstrap = wglCreateContext(s.glDC);
    wglMakeCurrent(s.glDC, newBootstrap);
    char dbg[64];
    _snprintf_s(dbg, sizeof(dbg), _TRUNCATE, "FluxCanvas: MSAA %dx enabled\n",
                samples);
    OutputDebugStringA(dbg);
    return true;
  }
  return false;
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

// ─────────────────────────────────────────────────────────────────────────────
// tickAndRender (main render loop, called on WM_USER+1 / WM_TIMER)
// ─────────────────────────────────────────────────────────────────────────────

static void tickAndRender(CanvasWidget *w) {
  auto &s = win32State(w);
  if (!s.glRC || !s.glDC || !w->canvasGL_)
    return;
  if (w->pendingSurface_)
    w->activatePendingSurface();
  if (!w->activeSurface_)
    return;

  makeCurrent(w);

  auto now = CanvasWidget::Clock::now();
  double dt = std::chrono::duration<double>(now - w->lastTick_).count();
  w->lastTick_ = now;

  w->activeSurface_->update(dt);

  int glW = s.lastGLW < 1 ? 1 : s.lastGLW;
  int glH = s.lastGLH < 1 ? 1 : s.lastGLH;

  glViewport(0, 0, glW, glH);
  glClearColor(0.f, 0.f, 0.f, 1.f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
  glEnable(GL_MULTISAMPLE);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  w->activeSurface_->preRender();

  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glViewport(0, 0, glW, glH);

  float z = w->vp_.zoom();
  float ox = -w->vp_.offsetX() * z;
  float oy = w->vp_.offsetY() * z;

  float ortho[16];
  glutil::ortho(0.f, float(glW), float(glH), 0.f, ortho);

  float vp[16] = {z, 0, 0, 0, 0, z, 0, 0, 0, 0, 1, 0, ox, oy, 0, 1};
  float mvp[16] = {};
  for (int col = 0; col < 4; ++col)
    for (int row = 0; row < 4; ++row)
      for (int k = 0; k < 4; ++k)
        mvp[col * 4 + row] += ortho[k * 4 + row] * vp[col * 4 + k];

  {
    Canvas2D ctx(w->canvasGL_, w->canvasW_, w->canvasH_, mvp);
    w->activeSurface_->render(ctx);
  }

  w->updateSBGeometry(glW, glH);
  w->renderScrollbarsGL(glW, glH, dt);

  SwapBuffers(s.glDC);

  if (w->activeSurface_->needsContinuousRedraw() && !s.repaintPending) {
    s.repaintPending = true;
    SetTimer(s.glHwnd, 1, 16, nullptr);
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// ensureWindows — lazy GL context + Canvas2DGL creation
// ─────────────────────────────────────────────────────────────────────────────

static void ensureWindows(CanvasWidget *w, GraphicsContext &ctx) {
  (void)ctx;
  auto &s = win32State(w);
  if (s.frameHwnd)
    return;

  s.parentHwnd = FluxUI::getCurrentInstance()->getWindow();
  if (!s.parentHwnd)
    return;
  registerClasses();

  s.frameHwnd =
      CreateWindowExW(WS_EX_NOPARENTNOTIFY, L"FluxGLFrame9", nullptr,
                      WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
                      w->x, w->y, w->width, w->height, s.parentHwnd, nullptr,
                      GetModuleHandle(nullptr), nullptr);
  assert(s.frameHwnd);
  SetWindowLongPtrW(s.frameHwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(w));

  s.glHwnd = CreateWindowExW(
      WS_EX_NOPARENTNOTIFY, L"FluxGLCanvas9", nullptr,
      WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN, 0, 0, w->width,
      w->height, s.frameHwnd, nullptr, GetModuleHandle(nullptr), nullptr);
  assert(s.glHwnd);
  SetWindowLongPtrW(s.glHwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(w));

  s.glDC = GetDC(s.glHwnd);
  setupPixelFormat(s.glDC);
  HGLRC tmp = wglCreateContext(s.glDC);
  wglMakeCurrent(s.glDC, tmp);

  tryUpgradeToMSAA(w, w->width, w->height, tmp);
  HGLRC currentBootstrap = wglGetCurrentContext();

  auto wglCA = reinterpret_cast<PFNWGLCREATECONTEXTATTRIBSARBPROC>(
      wglGetProcAddress("wglCreateContextAttribsARB"));
  auto tryCtx = [&](int major, int minor) -> HGLRC {
    if (!wglCA)
      return nullptr;
    const int att[] = {WGL_CONTEXT_MAJOR_VERSION_ARB,
                       major,
                       WGL_CONTEXT_MINOR_VERSION_ARB,
                       minor,
                       WGL_CONTEXT_PROFILE_MASK_ARB,
                       WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
                       0};
    return wglCA(s.glDC, nullptr, att);
  };
  HGLRC core = tryCtx(4, 6);
  if (!core)
    core = tryCtx(4, 5);
  if (!core)
    core = tryCtx(3, 3);
  if (core) {
    wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext(currentBootstrap);
    s.glRC = core;
  } else {
    s.glRC = currentBootstrap;
  }
  wglMakeCurrent(s.glDC, s.glRC);

  if (!gladLoadGL()) {
    OutputDebugStringA("GLAD: gladLoadGL() failed!\n");
    assert(false && "gladLoadGL failed");
  }

  {
    char info[256];
    _snprintf_s(info, sizeof(info), _TRUNCATE, "OpenGL %s  |  GLSL %s  |  %s\n",
                (const char *)glGetString(GL_VERSION),
                (const char *)glGetString(GL_SHADING_LANGUAGE_VERSION),
                (const char *)glGetString(GL_RENDERER));
    OutputDebugStringA(info);
  }

#ifndef NDEBUG
  glEnable(GL_DEBUG_OUTPUT);
  glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
  glDebugMessageCallback(
      [](GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei,
         const GLchar *message, const void *) {
        if (severity == GL_DEBUG_SEVERITY_NOTIFICATION)
          return;
        const char *sev = severity == GL_DEBUG_SEVERITY_HIGH     ? "HIGH"
                          : severity == GL_DEBUG_SEVERITY_MEDIUM ? "MEDIUM"
                          : severity == GL_DEBUG_SEVERITY_LOW    ? "LOW"
                                                                 : "NOTIFY";
        char buf[512];
        _snprintf_s(buf, sizeof(buf), _TRUNCATE,
                    "[GL %s] src=0x%04X type=0x%04X id=%u: %s\n", sev, source,
                    type, id, message);
        OutputDebugStringA(buf);
        if (type == GL_DEBUG_TYPE_ERROR)
          DebugBreak();
      },
      nullptr);
  glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE,
                        GL_DEBUG_SEVERITY_NOTIFICATION, 0, nullptr, GL_FALSE);
#endif

  // Canvas2DGL
  w->canvasGL_ = new Canvas2DGL();
  if (!w->canvasGL_->init()) {
    OutputDebugStringA("FluxCanvas: Canvas2DGL init failed!\n");
    assert(false && "Canvas2DGL init failed");
  }

  auto regFont = [&](const char *name, const char *path) {
    if (!Canvas2D::registerFont(w->canvasGL_, name, path)) {
      char warn[256];
      _snprintf_s(warn, sizeof(warn), _TRUNCATE,
                  "FluxCanvas: font '%s' not found at '%s'\n", name, path);
      OutputDebugStringA(warn);
    }
  };
  regFont("sans", "C:\\Windows\\Fonts\\segoeui.ttf");
  regFont("sans-bold", "C:\\Windows\\Fonts\\segoeuib.ttf");
  regFont("sans-italic", "C:\\Windows\\Fonts\\segoeuii.ttf");
  regFont("sans-bold-italic", "C:\\Windows\\Fonts\\segoeuiz.ttf");
  regFont("mono", "C:\\Windows\\Fonts\\consola.ttf");
  regFont("mono-bold", "C:\\Windows\\Fonts\\consolab.ttf");

  // Scrollbar GL program
  w->ensureSBProgram(kSBVert, kSBFrag);

  // Viewport / surface init
  w->vp_.init(w->width, w->height, w->canvasW_, w->canvasH_);
  s.lastGLW = w->width;
  s.lastGLH = w->height;
  w->updateViewportSize(w->width, w->height);
  w->updateSBGeometry(w->width, w->height);
  w->vp_.fitToView();

  w->activatePendingSurface();
  w->lastTick_ = CanvasWidget::Clock::now();

  if (w->onViewportChanged)
    w->onViewportChanged(w->vp_.zoom());
  if (w->onGLResize)
    w->onGLResize(w->width, w->height);
}

static void moveWindows(CanvasWidget *w) {
  auto &s = win32State(w);
  if (!s.frameHwnd)
    return;
  SetWindowPos(s.frameHwnd, nullptr, w->x, w->y, w->width, w->height,
               SWP_NOZORDER | SWP_NOACTIVATE);
  SetWindowPos(s.glHwnd, nullptr, 0, 0, w->width, w->height,
               SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOMOVE);
  if (w->width != s.lastGLW || w->height != s.lastGLH) {
    makeCurrent(w);
    s.lastGLW = w->width;
    s.lastGLH = w->height;
    w->updateViewportSize(w->width, w->height);
    glViewport(0, 0, w->width, w->height);
    if (w->onGLResize)
      w->onGLResize(w->width, w->height);
  }
}

static void destroyGL(CanvasWidget *w) {
  auto &s = win32State(w);
  if (s.glRC) {
    wglMakeCurrent(s.glDC, s.glRC);

    if (w->activeSurface_) {
      w->activeSurface_->destroy();
      w->activeSurface_.reset();
    }
    w->pendingSurface_.reset();

    if (w->canvasGL_) {
      w->canvasGL_->destroy();
      delete w->canvasGL_;
      w->canvasGL_ = nullptr;
    }

    if (w->sbProg_) {
      glDeleteProgram(w->sbProg_);
      w->sbProg_ = 0;
    }
    if (w->sbVAO_) {
      glDeleteVertexArrays(1, &w->sbVAO_);
      w->sbVAO_ = 0;
    }
    if (w->sbVBO_) {
      glDeleteBuffers(1, &w->sbVBO_);
      w->sbVBO_ = 0;
    }

    wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext(s.glRC);
    s.glRC = nullptr;
  }
  if (s.glHwnd && s.glDC) {
    ReleaseDC(s.glHwnd, s.glDC);
    s.glDC = nullptr;
  }
  if (s.glHwnd) {
    DestroyWindow(s.glHwnd);
    s.glHwnd = nullptr;
  }
  if (s.frameHwnd) {
    DestroyWindow(s.frameHwnd);
    s.frameHwnd = nullptr;
  }
  s_win32State.erase(w);
}

// ─────────────────────────────────────────────────────────────────────────────
// Window procedures
// ─────────────────────────────────────────────────────────────────────────────

static LRESULT CALLBACK FrameProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  auto *w =
      reinterpret_cast<CanvasWidget *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
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
    if (w && w->viewportEnabled_ && win32State(w).glHwnd)
      PostMessage(win32State(w).glHwnd, msg, wp, lp);
    return 0;
  default:
    return DefWindowProc(hwnd, msg, wp, lp);
  }
}

static LRESULT CALLBACK GLProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  auto *w =
      reinterpret_cast<CanvasWidget *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

  switch (msg) {

  case WM_USER + 1:
    if (w) {
      win32State(w).repaintPending = false;
      tickAndRender(w);
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
    if (!w || !w->viewportEnabled_)
      return 0;
    SetCapture(hwnd);
    w->beginPan(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
    return 0;
  case WM_MBUTTONUP:
    if (!w || !w->viewportEnabled_)
      return 0;
    ReleaseCapture();
    w->panning_ = false;
    return 0;

  case WM_LBUTTONDOWN: {
    SetCapture(hwnd);
    SetFocus(hwnd);
    if (!w)
      return 0;
    int sx = GET_X_LPARAM(lp), sy = GET_Y_LPARAM(lp);
    bool hC = w->hBar_.onMouseDown(
        sx, sy, [w](float t) { w->applyHScrollFraction(t); });
    bool vC = !hC && w->vBar_.onMouseDown(
                         sx, sy, [w](float t) { w->applyVScrollFraction(t); });
    if (!hC && !vC) {
      if (w->viewportEnabled_ && (GetKeyState(Key::Space) & 0x8000)) {
        w->beginPan(sx, sy);
      } else if (w->activeSurface_) {
        auto [cx, cy] = w->vp_.screenToCanvas(float(sx), float(sy));
        w->activeSurface_->onMouseDown(cx, cy);
      }
      forwardToParent(hwnd, msg, wp, lp);
    }
    scheduleRepaint(w);
    return 0;
  }
  case WM_LBUTTONUP: {
    ReleaseCapture();
    if (!w)
      return 0;
    int sx = GET_X_LPARAM(lp), sy = GET_Y_LPARAM(lp);
    bool hR = w->hBar_.onMouseUp(sx, sy);
    bool vR = w->vBar_.onMouseUp(sx, sy);
    if (!hR && !vR) {
      if (w->panning_) {
        w->panning_ = false;
      } else if (w->activeSurface_) {
        auto [cx, cy] = w->vp_.screenToCanvas(float(sx), float(sy));
        w->activeSurface_->onMouseUp(cx, cy);
      }
      forwardToParent(hwnd, msg, wp, lp);
    }
    scheduleRepaint(w);
    return 0;
  }

  case WM_RBUTTONDOWN: {
    SetCapture(hwnd);
    if (w && w->activeSurface_) {
      auto [cx, cy] = w->vp_.screenToCanvas(float(GET_X_LPARAM(lp)),
                                            float(GET_Y_LPARAM(lp)));
      w->activeSurface_->onRightMouseDown(cx, cy);
    }
    forwardToParent(hwnd, msg, wp, lp);
    return 0;
  }
  case WM_RBUTTONUP: {
    ReleaseCapture();
    if (w && w->activeSurface_) {
      auto [cx, cy] = w->vp_.screenToCanvas(float(GET_X_LPARAM(lp)),
                                            float(GET_Y_LPARAM(lp)));
      w->activeSurface_->onMouseUp(cx, cy);
    }
    forwardToParent(hwnd, msg, wp, lp);
    return 0;
  }

  case WM_MOUSEMOVE: {
    if (!w)
      return 0;
    int sx = GET_X_LPARAM(lp), sy = GET_Y_LPARAM(lp);
    auto &s = win32State(w);
    if (!s.trackingLeave) {
      TRACKMOUSEEVENT tme{};
      tme.cbSize = sizeof(tme);
      tme.dwFlags = TME_LEAVE;
      tme.hwndTrack = hwnd;
      TrackMouseEvent(&tme);
      s.trackingLeave = true;
    }
    bool hDrag = w->hBar_.onMouseMove(sx, sy);
    bool vDrag = w->vBar_.onMouseMove(sx, sy);
    scheduleRepaint(w);
    if (hDrag || vDrag)
      return 0;
    if (w->panning_) {
      w->continuePan(sx, sy);
    } else if (w->activeSurface_) {
      auto [cx, cy] = w->vp_.screenToCanvas(float(sx), float(sy));
      w->activeSurface_->onMouseMove(cx, cy);
    }
    forwardToParent(hwnd, msg, wp, lp);
    return 0;
  }
  case WM_MOUSELEAVE:
    if (w) {
      win32State(w).trackingLeave = false;
      w->hBar_.onMouseLeave();
      w->vBar_.onMouseLeave();
      scheduleRepaint(w);
    }
    return 0;

  case WM_MOUSEWHEEL: {
    if (!w || !w->viewportEnabled_)
      return 0;
    int delta = GET_WHEEL_DELTA_WPARAM(wp);
    bool ctrl = (GET_KEYSTATE_WPARAM(wp) & MK_CONTROL) != 0;
    bool shift = (GET_KEYSTATE_WPARAM(wp) & MK_SHIFT) != 0;
    POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
    ScreenToClient(hwnd, &pt);
    if (ctrl) {
      float f = (delta > 0) ? 1.1f : 1.f / 1.1f;
      w->vp_.zoomToward(float(pt.x), float(pt.y), f);
    } else if (shift) {
      w->vp_.panByScreen(float(delta) * 0.5f, 0.f);
    } else {
      w->vp_.panByScreen(0.f, -(float(delta) / WHEEL_DELTA) * 40.f);
    }
    w->pokeScrollbars();
    scheduleRepaint(w);
    forwardToParent(hwnd, msg, wp, lp);
    return 0;
  }

  case WM_KEYDOWN: {
    if (!w)
      return 0;
    bool ctrl = (GetKeyState(Key::Control) & 0x8000) != 0;
    bool consumed = false;
    if (ctrl && w->viewportEnabled_) {
      if (wp == Key::OemPlus || wp == Key::NumpadPlus) {
        w->vp_.zoomIn();
        w->pokeScrollbars();
        scheduleRepaint(w);
        consumed = true;
      } else if (wp == Key::OemMinus || wp == Key::NumpadMinus) {
        w->vp_.zoomOut();
        w->pokeScrollbars();
        scheduleRepaint(w);
        consumed = true;
      } else if (wp == '0') {
        w->vp_.resetZoom();
        w->pokeScrollbars();
        scheduleRepaint(w);
        consumed = true;
      }
    }
    if (!consumed && w->activeSurface_)
      w->activeSurface_->onKeyDown(int(wp));
    return 0;
  }
  case WM_KEYUP:
    if (w && w->activeSurface_)
      w->activeSurface_->onKeyUp(int(wp));
    return 0;

  case WM_TIMER:
    KillTimer(hwnd, 1);
    if (w) {
      win32State(w).repaintPending = false;
      tickAndRender(w);
    }
    return 0;

  default:
    return DefWindowProc(hwnd, msg, wp, lp);
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// CanvasWidget member implementations (Win32)
// ─────────────────────────────────────────────────────────────────────────────

CanvasWidget::CanvasWidget()
    : hBar_(CustomScrollbar::Axis::Horizontal),
      vBar_(CustomScrollbar::Axis::Vertical) {
  autoWidth = true; // fill available space by default
  autoHeight = true;
  width = 400; // fallback if parent is unbounded
  height = 300;
}

CanvasWidget::~CanvasWidget() { destroyGL(this); }

std::shared_ptr<CanvasWidget> CanvasWidget::setViewportEnabled(bool e) {
  viewportEnabled_ = e;
  return ptr();
}
std::shared_ptr<CanvasWidget> CanvasWidget::setScrollbarsEnabled(bool e) {
  scrollbarsEnabled_ = e;
  auto &s = win32State(this);
  if (s.glHwnd) {
    updateViewportSize(s.lastGLW, s.lastGLH);
    scheduleRepaint(this);
  }
  return ptr();
}
bool CanvasWidget::scrollbarsEnabled() const { return scrollbarsEnabled_; }

RenderSurface *CanvasWidget::getSurface() const { return activeSurface_.get(); }
const Viewport &CanvasWidget::viewport() const { return vp_; }
Viewport &CanvasWidget::viewport() { return vp_; }

std::shared_ptr<CanvasWidget> CanvasWidget::setSize(int w, int h) {
    width  = w;
    height = h;
    autoWidth  = false;  // fixed size — stop auto-expanding
    autoHeight = false;
    markNeedsLayout();
    return ptr();
}
std::shared_ptr<CanvasWidget> CanvasWidget::setCanvasSize(int w, int h) {
  canvasW_ = w;
  canvasH_ = h;
  auto &s = win32State(this);
  if (s.glHwnd) {
    vp_.setCanvasSize(w, h);
    if (activeSurface_) {
      makeCurrent(this);
      activeSurface_->resize(w, h);
    }
  }
  return ptr();
}
std::shared_ptr<CanvasWidget> CanvasWidget::redraw() {
  markNeedsPaint();
  return ptr();
}

void CanvasWidget::computeLayout(GraphicsContext & /*ctx*/,
                                 const BoxConstraints &c, FontCache &) {
  if (autoWidth) {
    width = (c.maxWidth < kUnbounded) ? c.maxWidth : width;
    canvasW_ = width;
  }
  if (autoHeight) {
    height = (c.maxHeight < kUnbounded) ? c.maxHeight : height;
    canvasH_ = height;
  }
  needsLayout = false;
}

void CanvasWidget::render(GraphicsContext &ctx, FontCache &) {
  ensureWindows(this, ctx);
  moveWindows(this);
  tickAndRender(this);
  needsPaint = false;
}

void CanvasWidget::onDetach() {
  destroyGL(this);
  Widget::onDetach();
}

void CanvasWidget::markNeedsPaint() {
  Widget::markNeedsPaint();
  auto &s = win32State(this);
  if (s.glHwnd && !s.repaintPending) {
    s.repaintPending = true;
    PostMessage(s.glHwnd, WM_USER + 1, 0, 0);
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Shared helper implementations
// ─────────────────────────────────────────────────────────────────────────────

void CanvasWidget::viewportDims(int glW, int glH, int &vpW, int &vpH) const {
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
void CanvasWidget::updateViewportSize(int glW, int glH) {
  int vpW, vpH;
  viewportDims(glW, glH, vpW, vpH);
  vp_.setViewSize(vpW, vpH);
}
void CanvasWidget::updateSBGeometry(int glW, int glH) {
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
void CanvasWidget::beginPan(int sx, int sy) {
  panning_ = true;
  panStartSX_ = sx;
  panStartSY_ = sy;
  panStartOX_ = vp_.offsetX();
  panStartOY_ = vp_.offsetY();
}
void CanvasWidget::continuePan(int sx, int sy) {
  float dx = float(sx - panStartSX_), dy = float(sy - panStartSY_);
  vp_.setOffset(panStartOX_ - dx / vp_.zoom(), panStartOY_ + dy / vp_.zoom());
  pokeScrollbars();
  scheduleRepaint(this);
}
void CanvasWidget::pokeScrollbars() {
  hBar_.poke();
  vBar_.poke();
}
void CanvasWidget::applyHScrollFraction(float thumbMin) {
  float span = vp_.scrollbarH().thumbMax - vp_.scrollbarH().thumbMin;
  float range = vp_.canvasW() - vp_.viewW() / vp_.zoom();
  if (range <= 0.f)
    return;
  vp_.setOffsetX(thumbMin / (1.f - span) * range);
  pokeScrollbars();
  scheduleRepaint(this);
}
void CanvasWidget::applyVScrollFraction(float thumbMin) {
  float span = vp_.scrollbarV().thumbMax - vp_.scrollbarV().thumbMin;
  float range = vp_.canvasH() - vp_.viewH() / vp_.zoom();
  if (range <= 0.f)
    return;
  float t = 1.f - thumbMin - span;
  vp_.setOffsetY(t / (1.f - span) * range);
  pokeScrollbars();
  scheduleRepaint(this);
}
void CanvasWidget::activatePendingSurface() {
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
void CanvasWidget::ensureSBProgram(const char *vert, const char *frag) {
  if (sbProg_)
    return;
  sbProg_ = glutil::linkProgram(vert, frag);
  assert(sbProg_);
  sbMVP_ = glGetUniformLocation(sbProg_, "uMVP");
  sbColor_ = glGetUniformLocation(sbProg_, "uColor");
  if (!sbVAO_)
    glGenVertexArrays(1, &sbVAO_);
  if (!sbVBO_)
    glGenBuffers(1, &sbVBO_);
}
void CanvasWidget::renderSBCorner(int glW, int glH) {
  if (!hBar_.isVisible() || !vBar_.isVisible() || !scrollbarsEnabled_)
    return;
  float cx_ = float(glW) - kSBThick;
  float cy_ = float(glH) - kSBThick;
  float mvp[16];
  glutil::ortho(0.f, float(glW), float(glH), 0.f, mvp);
  glUseProgram(sbProg_);
  glUniformMatrix4fv(sbMVP_, 1, GL_FALSE, mvp);
  glUniform4f(sbColor_, 0.12f, 0.12f, 0.12f, 0.35f);
  float v[] = {cx_,
               cy_,
               cx_ + kSBThick,
               cy_,
               cx_ + kSBThick,
               cy_ + kSBThick,
               cx_ + kSBThick,
               cy_ + kSBThick,
               cx_,
               cy_ + kSBThick,
               cx_,
               cy_};
  glBindVertexArray(sbVAO_);
  glBindBuffer(GL_ARRAY_BUFFER, sbVBO_);
  glBufferData(GL_ARRAY_BUFFER, sizeof(v), v, GL_DYNAMIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 2, nullptr);
  glDisableVertexAttribArray(1);
  glDrawArrays(GL_TRIANGLES, 0, 6);
  glBindVertexArray(0);
}
void CanvasWidget::renderScrollbarsGL(int glW, int glH, double dt) {
  auto &s = win32State(this);
  bool hFade = hBar_.tick(dt);
  bool vFade = vBar_.tick(dt);

  if (!hBar_.needsRedraw() && !vBar_.needsRedraw()) {
    if ((hFade || vFade) && !s.repaintPending) {
      s.repaintPending = true;
      SetTimer(s.glHwnd, 1, 16, nullptr);
    }
    return;
  }

  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  hBar_.render(sbProg_, sbVAO_, sbVBO_, sbMVP_, sbColor_, glW, glH);
  vBar_.render(sbProg_, sbVAO_, sbVBO_, sbMVP_, sbColor_, glW, glH);
  renderSBCorner(glW, glH);

  if ((hFade || vFade) && !s.repaintPending) {
    s.repaintPending = true;
    SetTimer(s.glHwnd, 1, 16, nullptr);
  }
}

#endif