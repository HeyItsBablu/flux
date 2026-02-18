#ifndef FLUX_GRAPH_HPP
#define FLUX_GRAPH_HPP

#include "flux_widget.hpp"
#define NOMINMAX // prevent windows.h from defining min/max macros
#include <algorithm>
#include <cmath>
#include <functional>
#include <gl/GL.h>
#include <gl/GLU.h>
#include <string>
#include <vector>
#include <windows.h>
#include <windowsx.h> // for GET_X_LPARAM / GET_Y_LPARAM

#pragma comment(lib, "opengl32.lib")
#pragma comment(lib, "glu32.lib")

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
// OPENGL GRAPH WIDGET
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

  // ----------------------------------------------------------------
  // Public fluent API
  // ----------------------------------------------------------------

  std::shared_ptr<GraphWidget> addSeries(const std::string &label,
                                         const std::vector<float> &vals,
                                         float r = 0.2f, float g = 0.6f,
                                         float b = 1.0f) {
    GraphSeries s;
    s.label = label;
    s.values = vals;
    s.r = r; s.g = g; s.b = b;
    series.push_back(s);
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

  std::shared_ptr<GraphWidget> setXLabels(const std::vector<std::string> &labels) {
    xLabels = labels;
    markNeedsPaint();
    return std::static_pointer_cast<GraphWidget>(shared_from_this());
  }

  std::shared_ptr<GraphWidget> setYRange(float mn, float mx) {
    yMin = mn; yMax = mx;
    autoRange = false;
    markNeedsPaint();
    return std::static_pointer_cast<GraphWidget>(shared_from_this());
  }

  std::shared_ptr<GraphWidget> setSize(int w, int h) {
    width = w; height = h;
    autoWidth = false; autoHeight = false;
    markNeedsLayout();
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

  // ----------------------------------------------------------------
  // Widget overrides
  // ----------------------------------------------------------------

  void computeLayout(HDC hdc, int availableWidth, int availableHeight,
                     FontCache &fontCache) override {
    applyConstraints();
    needsLayout = false;
  }

  void render(HDC hdc, FontCache &fontCache) override {
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
  HWND  childHwnd  = nullptr;
  HDC   glDC       = nullptr;
  HGLRC glRC       = nullptr;
  HWND  parentHwnd = nullptr;

  // ----------------------------------------------------------------
  // Child HWND / GL context management
  // ----------------------------------------------------------------

  static LRESULT CALLBACK ChildProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    // Suppress background erase to prevent flicker
    if (msg == WM_ERASEBKGND)
      return 1;

    // Suppress default paint — GL handles all drawing via SwapBuffers
    if (msg == WM_PAINT) {
      PAINTSTRUCT ps;
      BeginPaint(hwnd, &ps);
      EndPaint(hwnd, &ps);
      return 0;
    }

    // Forward mouse events to parent with corrected coordinates
    if (msg == WM_LBUTTONDOWN || msg == WM_RBUTTONDOWN ||
        msg == WM_MOUSEMOVE   || msg == WM_MOUSEWHEEL) {
      HWND parent = GetParent(hwnd);
      if (parent) {
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
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
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = ChildProc;
    wc.hInstance     = GetModuleHandle(nullptr);
    wc.lpszClassName = L"FluxGLGraph";
    wc.style         = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
    RegisterClassExW(&wc);

    registered = true;
  }

  HWND findRootHwnd() {
    if (parentHwnd)
      return parentHwnd;
    return GetActiveWindow();
  }

  void ensureChildWindow(HDC parentDC) {
    if (childHwnd)
      return;

    HWND owner = WindowFromDC(parentDC);
    if (!owner)
      owner = findRootHwnd();
    parentHwnd = owner;

    registerChildClass();

    childHwnd = CreateWindowExW(
        WS_EX_NOPARENTNOTIFY,           // don't trigger parent repaints
        L"FluxGLGraph", nullptr,
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
        x, y, width, height,
        owner, nullptr, GetModuleHandle(nullptr), nullptr);

    if (!childHwnd)
      return;

    glDC = GetDC(childHwnd);
    setupPixelFormat(glDC);
    glRC = wglCreateContext(glDC);
    wglMakeCurrent(glDC, glRC);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_LINE_SMOOTH);
    glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
  }

  void setupPixelFormat(HDC dc) {
    PIXELFORMATDESCRIPTOR pfd = {};
    pfd.nSize      = sizeof(pfd);
    pfd.nVersion   = 1;
    pfd.dwFlags    = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
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

  // ----------------------------------------------------------------
  // OpenGL rendering
  // ----------------------------------------------------------------

  void renderGL() {
    if (!glRC || !glDC)
      return;

    // Only switch context if not already current
    if (wglGetCurrentContext() != glRC)
      wglMakeCurrent(glDC, glRC);

    float bg_r = GetRValue(bgColor) / 255.0f;
    float bg_g = GetGValue(bgColor) / 255.0f;
    float bg_b = GetBValue(bgColor) / 255.0f;

    glViewport(0, 0, width, height);
    glClearColor(bg_r, bg_g, bg_b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    const float marginL = 0.12f, marginR = 0.04f;
    const float marginT = 0.08f, marginB = 0.12f;

    float plotX0 = -1.0f + marginL * 2.0f;
    float plotX1 =  1.0f - marginR * 2.0f;
    float plotY0 = -1.0f + marginB * 2.0f;
    float plotY1 =  1.0f - marginT * 2.0f;

    computeRange();

    if (showGrid)
      drawGrid(plotX0, plotX1, plotY0, plotY1);
    drawAxes(plotX0, plotX1, plotY0, plotY1);

    for (auto &s : series) {
      if (s.values.empty())
        continue;
      switch (graphType) {
      case GraphType::Line: drawLine(s, plotX0, plotX1, plotY0, plotY1); break;
      case GraphType::Bar:  drawBars(s, plotX0, plotX1, plotY0, plotY1); break;
      case GraphType::Area: drawArea(s, plotX0, plotX1, plotY0, plotY1); break;
      }
    }

    SwapBuffers(glDC);
  }

  void computeRange() {
    if (!autoRange)
      return;
    yMin = 0.0f;
    yMax = 1.0f;
    bool first = true;
    for (auto &s : series) {
      for (float v : s.values) {
        if (first) { yMin = yMax = v; first = false; }
        else { yMin = min(yMin, v); yMax = max(yMax, v); }
      }
    }
    if (yMin == yMax) { yMin -= 1.0f; yMax += 1.0f; }
    float pad = (yMax - yMin) * 0.1f;
    yMin -= pad;
    yMax += pad;
  }

  float dataToNDC_Y(float v, float py0, float py1) {
    float t = (v - yMin) / (yMax - yMin);
    return py0 + t * (py1 - py0);
  }

  float indexToNDC_X(int i, int n, float px0, float px1) {
    if (n <= 1) return (px0 + px1) * 0.5f;
    float t = (float)i / (n - 1);
    return px0 + t * (px1 - px0);
  }

  void drawGrid(float px0, float px1, float py0, float py1) {
    glLineWidth(1.0f);
    glColor4f(1.0f, 1.0f, 1.0f, 0.08f);
    glBegin(GL_LINES);
    for (int i = 0; i <= 5; ++i) {
      float t = (float)i / 5;
      float y = py0 + t * (py1 - py0);
      glVertex2f(px0, y); glVertex2f(px1, y);
    }
    for (int i = 0; i <= 6; ++i) {
      float t = (float)i / 6;
      float x = px0 + t * (px1 - px0);
      glVertex2f(x, py0); glVertex2f(x, py1);
    }
    glEnd();
  }

  void drawAxes(float px0, float px1, float py0, float py1) {
    glLineWidth(1.5f);
    glColor4f(0.6f, 0.6f, 0.6f, 1.0f);
    glBegin(GL_LINES);
    glVertex2f(px0, py0); glVertex2f(px1, py0); // X axis
    glVertex2f(px0, py0); glVertex2f(px0, py1); // Y axis
    glEnd();
  }

  void drawLine(const GraphSeries &s, float px0, float px1, float py0, float py1) {
    int n = (int)s.values.size();
    glLineWidth(s.lineWidth);
    glColor4f(s.r, s.g, s.b, 1.0f);
    glBegin(GL_LINE_STRIP);
    for (int i = 0; i < n; ++i)
      glVertex2f(indexToNDC_X(i, n, px0, px1), dataToNDC_Y(s.values[i], py0, py1));
    glEnd();

    glPointSize(4.0f);
    glBegin(GL_POINTS);
    for (int i = 0; i < n; ++i)
      glVertex2f(indexToNDC_X(i, n, px0, px1), dataToNDC_Y(s.values[i], py0, py1));
    glEnd();
  }

  void drawArea(const GraphSeries &s, float px0, float px1, float py0, float py1) {
    int n = (int)s.values.size();
    float baseline = dataToNDC_Y(max(0.0f, yMin), py0, py1);

    glColor4f(s.r, s.g, s.b, 0.25f);
    glBegin(GL_TRIANGLE_STRIP);
    for (int i = 0; i < n; ++i) {
      float nx = indexToNDC_X(i, n, px0, px1);
      float ny = dataToNDC_Y(s.values[i], py0, py1);
      glVertex2f(nx, baseline);
      glVertex2f(nx, ny);
    }
    glEnd();

    drawLine(s, px0, px1, py0, py1);
  }

  void drawBars(const GraphSeries &s, float px0, float px1, float py0, float py1) {
    int n = (int)s.values.size();
    if (n == 0) return;

    float baseline = dataToNDC_Y(max(yMin, 0.0f), py0, py1);
    float barW = (px1 - px0) / n * 0.6f;

    for (int i = 0; i < n; ++i) {
      float cx  = indexToNDC_X(i, n, px0, px1);
      float top = dataToNDC_Y(s.values[i], py0, py1);
      float l   = cx - barW * 0.5f;
      float r   = cx + barW * 0.5f;

      // Fill
      glColor4f(s.r, s.g, s.b, 0.85f);
      glBegin(GL_QUADS);
      glVertex2f(l, baseline); glVertex2f(r, baseline);
      glVertex2f(r, top);      glVertex2f(l, top);
      glEnd();

      // Outline — clamp to [0,1] to avoid undefined GL behavior
      glColor4f(min(s.r * 1.3f, 1.0f),
                min(s.g * 1.3f, 1.0f),
                min(s.b * 1.3f, 1.0f), 1.0f);
      glLineWidth(1.0f);
      glBegin(GL_LINE_LOOP);
      glVertex2f(l, baseline); glVertex2f(r, baseline);
      glVertex2f(r, top);      glVertex2f(l, top);
      glEnd();
    }
  }
};

// ============================================================================
// FACTORY
// ============================================================================

using GraphWidgetPtr = std::shared_ptr<GraphWidget>;

inline GraphWidgetPtr Graph() {
  return std::make_shared<GraphWidget>();
}

inline GraphWidgetPtr Graph(int w, int h) {
  auto g = std::make_shared<GraphWidget>();
  g->setSize(w, h);
  return g;
}

#endif // FLUX_GRAPH_HPP

/*
// ============================================================================
// USAGE EXAMPLES
// ============================================================================

// 1. Simple line chart
Graph(500, 300)
    ->addSeries("Temperature", {22,24,27,23,19,21,26}, 1.0f,0.4f,0.2f)
    ->setTitle("Daily Temps")
    ->setXLabels({"Mon","Tue","Wed","Thu","Fri","Sat","Sun"});

// 2. Bar chart with multiple series
Graph(600, 350)
    ->setType(GraphType::Bar)
    ->addSeries("Sales",  {120,95,180,75,200}, 0.2f,0.7f,1.0f)
    ->addSeries("Target", {100,100,150,100,180},1.0f,0.6f,0.2f)
    ->setXLabels({"Q1","Q2","Q3","Q4","Q5"});

// 3. Area chart
Graph(500, 280)
    ->setType(GraphType::Area)
    ->addSeries("Revenue", {10,40,30,80,60,95,110}, 0.3f,1.0f,0.5f)
    ->setYRange(0, 130);

// 4. Update data at runtime (e.g. live sensor feed)
auto liveChart = Graph(600, 300);
liveChart->addSeries("CPU", cpuData, 0.0f,1.0f,0.4f);
// later:
liveChart->series[0].values.push_back(newValue);
liveChart->markNeedsPaint();

*/