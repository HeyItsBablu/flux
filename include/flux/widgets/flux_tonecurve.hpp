#ifndef FLUX_TONE_CURVE_HPP
#define FLUX_TONE_CURVE_HPP

// ============================================================================
//  flux_tone_curve.hpp  —  Lightroom-style interactive Tone Curve widget
//
//  Features
//  ────────
//  • Catmull-Rom spline through up to 16 user-placed control points
//  • Parametric mode  (Shadows / Darks / Lights / Highlights region sliders)
//  • Per-channel mode (RGB composite  +  individual R / G / B curves)
//  • Targeted Adjustment Tool (TAT): click-drag on image pixel → moves the
//    curve at that tonal value — wire via setTATValue()
//  • Zone regions drawn under the curve (Shadows / Darks / Lights / Highlights)
//  • Clamp rail at top and bottom (prevents S-curve blowing out)
//  • Hover scrubber: vertical line + input/output tooltip
//  • Add point: click empty curve area
//  • Delete point: double-click or drag off-canvas
//  • Keyboard nudge: arrow keys move selected point ±1 unit
//  • Full reactive State<ToneCurveData> binding
//  • setOnCurveChanged callback for shader integration
// ============================================================================

#include "flux_core.hpp"
#include "flux_state.hpp"
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <functional>
#include <string>
#include <vector>

// ============================================================================
//  TONE CURVE DATA
//  Represents one complete curve (one channel or the composite RGB curve).
//  Points are stored in normalised [0,1]×[0,1] space.
// ============================================================================

struct CurvePoint {
  float x = 0.f; // input  0..1
  float y = 0.f; // output 0..1
};

struct ToneCurveChannel {
  std::vector<CurvePoint> points;

  ToneCurveChannel() {
    // Identity: two fixed endpoints, Lightroom default
    points = {{0.f, 0.f}, {1.f, 1.f}};
  }

  // Evaluate the curve at input t ∈ [0,1] using Catmull-Rom spline.
  // Returns output ∈ [0,1].
  float evaluate(float t) const {
    if (points.size() < 2)
      return t;
    t = std::max(0.f, std::min(1.f, t));

    // Clamp to endpoints outside range
    if (t <= points.front().x)
      return points.front().y;
    if (t >= points.back().x)
      return points.back().y;

    // Find segment
    int seg = 0;
    for (int i = 0; i + 1 < (int)points.size(); ++i) {
      if (t <= points[i + 1].x) {
        seg = i;
        break;
      }
    }

    // Catmull-Rom control points (clamped at boundaries)
    auto pt = [&](int i) -> CurvePoint {
      i = std::max(0, std::min((int)points.size() - 1, i));
      return points[i];
    };
    CurvePoint p0 = pt(seg - 1), p1 = pt(seg), p2 = pt(seg + 1),
               p3 = pt(seg + 2);

    float dx = p2.x - p1.x;
    if (dx < 1e-6f)
      return p1.y;

    float u = (t - p1.x) / dx; // local parameter 0..1
    float u2 = u * u, u3 = u2 * u;

    // Catmull-Rom basis

    float m1y = (p2.y - p0.y) * 0.5f;

    float m2y = (p3.y - p1.y) * 0.5f;

    // We only need Y: hermite interpolation
    float h00 = 2 * u3 - 3 * u2 + 1;
    float h10 = u3 - 2 * u2 + u;
    float h01 = -2 * u3 + 3 * u2;
    float h11 = u3 - u2;

    float y = h00 * p1.y + h10 * m1y + h01 * p2.y + h11 * m2y;
    return std::max(0.f, std::min(1.f, y));
  }

  // Build a 256-entry LUT for GPU/export use
  std::array<uint8_t, 256> buildLUT() const {
    std::array<uint8_t, 256> lut;
    for (int i = 0; i < 256; ++i)
      lut[i] = (uint8_t)(evaluate(i / 255.f) * 255.f + 0.5f);
    return lut;
  }

  bool isIdentity() const {
    if (points.size() != 2)
      return false;
    return std::abs(points[0].x) < 0.01f && std::abs(points[0].y) < 0.01f &&
           std::abs(points[1].x - 1.f) < 0.01f &&
           std::abs(points[1].y - 1.f) < 0.01f;
  }

  void reset() { points = {{0.f, 0.f}, {1.f, 1.f}}; }

  // Ensure points are sorted by x and endpoints are clamped
  void sort() {
    std::sort(
        points.begin(), points.end(),
        [](const CurvePoint &a, const CurvePoint &b) { return a.x < b.x; });
    if (!points.empty()) {
      points.front().x = 0.f;
      points.back().x = 1.f;
    }
  }
};

// Parametric curve offsets (Lightroom's "Point Curve" sliders)
struct ParametricCurve {
  float shadows = 0.f;    // -1..+1  pulls the shadows region
  float darks = 0.f;      // -1..+1
  float lights = 0.f;     // -1..+1
  float highlights = 0.f; // -1..+1

  void reset() { shadows = darks = lights = highlights = 0.f; }

  // Bake parametric offsets into a ToneCurveChannel
  ToneCurveChannel bake() const {
    ToneCurveChannel ch;
    ch.points = {
        {0.00f, std::max(0.f, std::min(1.f, 0.00f + shadows * 0.20f))},
        {0.25f, std::max(0.f, std::min(1.f, 0.25f + darks * 0.20f))},
        {0.50f, 0.50f},
        {0.75f, std::max(0.f, std::min(1.f, 0.75f + lights * 0.20f))},
        {1.00f, std::max(0.f, std::min(1.f, 1.00f + highlights * 0.20f))},
    };
    return ch;
  }
};

struct ToneCurveData {
  ToneCurveChannel rgb; // composite (applied to all channels)
  ToneCurveChannel r;
  ToneCurveChannel g;
  ToneCurveChannel b;
  ParametricCurve parametric;
  bool useParametric = false; // parametric vs point-curve mode

  void reset() {
    rgb.reset();
    r.reset();
    g.reset();
    b.reset();
    parametric.reset();
    useParametric = false;
  }
};

// ============================================================================
//  TONE CURVE WIDGET
// ============================================================================

enum class CurveChannel { RGB, Red, Green, Blue };

class ToneCurveWidget : public Widget {
public:
  // ── Data ──────────────────────────────────────────────────────────────────
  ToneCurveData curveData;

  // ── Display options ───────────────────────────────────────────────────────
  CurveChannel activeChannel = CurveChannel::RGB;
  bool showHistogram = true; // draw histogram under the curve
  bool showRegions = true;   // coloured zone bands (Shadows etc.)
  bool showGrid = true;
  bool showInputOutput = true; // tooltip showing In/Out values
  bool parametricMode = false; // show parametric sliders instead

  // Histogram backing data (optional — set via setHistogram())
  std::array<std::uint32_t, 256> histBins{};
  bool hasHistogram = false;

  // Colors — Lightroom dark theme defaults
  Color bgColor = Color::fromRGB(28, 28, 28);
  Color gridColor = Color::fromRGB(50, 50, 50);
  Color borderColor = Color::fromRGB(60, 60, 60);
  Color curveColorRGB = Color::fromRGB(210, 210, 210);
  Color curveColorR = Color::fromRGB(220, 70, 70);
  Color curveColorG = Color::fromRGB(70, 200, 70);
  Color curveColorB = Color::fromRGB(70, 120, 240);
  Color pointFill = Color::fromRGB(255, 255, 255);
  Color pointBorder = Color::fromRGB(120, 120, 120);
  Color pointSelected = Color::fromRGB(255, 200, 50);
  Color tatLineColor = Color::fromRGB(255, 200, 50);

  // TAT (Targeted Adjustment Tool) — set this to a normalised [0,1] input
  // value coming from the image under the cursor; the widget draws a
  // vertical scrub line and highlights the corresponding curve point.
  float tatValue = -1.f; // -1 = inactive

  // Callback fired whenever the curve changes
  std::function<void(const ToneCurveData &)> onCurveChanged;

  // ── Constructor ───────────────────────────────────────────────────────────
  ToneCurveWidget() {
    width = 256;
    height = 256;
    autoWidth = false;
    autoHeight = false;
    isFocusable = true;
  }

  // ── Fluent API ────────────────────────────────────────────────────────────

  std::shared_ptr<ToneCurveWidget> setCurveData(const ToneCurveData &d) {
    curveData = d;
    markNeedsPaint();
    return ptr();
  }

  std::shared_ptr<ToneCurveWidget> setCurveData(State<ToneCurveData> &state) {
    curveData = state.get();
    state.bindProperty(
        shared_from_this(),
        [](Widget *w, const ToneCurveData &d) {
          static_cast<ToneCurveWidget *>(w)->curveData = d;
        },
        false);
    markNeedsPaint();
    return ptr();
  }

  std::shared_ptr<ToneCurveWidget> setActiveChannel(CurveChannel ch) {
    activeChannel = ch;
    markNeedsPaint();
    return ptr();
  }

  std::shared_ptr<ToneCurveWidget> setShowHistogram(bool v) {
    showHistogram = v;
    markNeedsPaint();
    return ptr();
  }

  std::shared_ptr<ToneCurveWidget> setShowRegions(bool v) {
    showRegions = v;
    markNeedsPaint();
    return ptr();
  }

  std::shared_ptr<ToneCurveWidget> setShowGrid(bool v) {
    showGrid = v;
    markNeedsPaint();
    return ptr();
  }

  std::shared_ptr<ToneCurveWidget> setParametricMode(bool v) {
    parametricMode = v;
    curveData.useParametric = v;
    markNeedsPaint();
    return ptr();
  }

  std::shared_ptr<ToneCurveWidget>
  setHistogram(const std::array<std::uint32_t, 256> &bins) {
    histBins = bins;
    hasHistogram = true;
    markNeedsPaint();
    return ptr();
  }

  // Convenience: feed R+G+B arrays and build a luminosity composite
  std::shared_ptr<ToneCurveWidget>
  setHistogram(const std::array<std::uint32_t, 256> &r,
               const std::array<std::uint32_t, 256> &g,
               const std::array<std::uint32_t, 256> &b) {
    for (int i = 0; i < 256; ++i)
      histBins[i] =
          (std::uint32_t)(0.2126f * r[i] + 0.7152f * g[i] + 0.0722f * b[i]);
    hasHistogram = true;
    markNeedsPaint();
    return ptr();
  }

  // TAT: call this when the mouse moves over the image canvas
  std::shared_ptr<ToneCurveWidget> setTATValue(float normalisedInput) {
    tatValue = normalisedInput;
    markNeedsPaint();
    return ptr();
  }

  std::shared_ptr<ToneCurveWidget>
  setOnCurveChanged(std::function<void(const ToneCurveData &)> cb) {
    onCurveChanged = cb;
    return ptr();
  }

  std::shared_ptr<ToneCurveWidget> setSize(int w, int h) {
    width = w;
    height = h;
    autoWidth = autoHeight = false;
    markNeedsLayout();
    return ptr();
  }

  std::shared_ptr<ToneCurveWidget> setWidth(int w) {
    width = w;
    autoWidth = false;
    markNeedsLayout();
    return ptr();
  }

  std::shared_ptr<ToneCurveWidget> setHeight(int h) {
    height = h;
    autoHeight = false;
    markNeedsLayout();
    return ptr();
  }

  // Reset curve to identity
  std::shared_ptr<ToneCurveWidget> resetCurve() {
    activeChannelCurve().reset();
    selectedPoint = -1;
    notifyCurveChanged();
    markNeedsPaint();
    return ptr();
  }

  // ── Layout ────────────────────────────────────────────────────────────────

  void computeLayout(GraphicsContext & /*ctx*/,
                     const BoxConstraints &constraints,
                     FontCache & /*fontCache*/) override {
    width = constraints.clampWidth(autoWidth ? constraints.maxWidth : width);
    height =
        constraints.clampHeight(autoHeight ? constraints.maxHeight : height);
    applyConstraints();
    needsLayout = false;
  }

  // ── Render ────────────────────────────────────────────────────────────────

  void render(GraphicsContext &ctx, FontCache &fontCache) override {
    const int PAD_L = 6, PAD_R = 6, PAD_T = 6, PAD_B = 6;
    plotX = x + PAD_L;
    plotY = y + PAD_T;
    plotW = width - PAD_L - PAD_R;
    plotH = height - PAD_T - PAD_B;
    if (parametricMode)
      plotH -= kParamStripH + 6;

    Painter painter(ctx);
    painter.fillRect(x, y, width, height, bgColor);

    if (showRegions)
      drawRegions(ctx);
    if (showHistogram && hasHistogram)
      drawHistogram(ctx);
    if (showGrid)
      drawGrid(ctx);

    // Identity diagonal
    painter.drawLine(plotX, plotY + plotH, plotX + plotW, plotY,
                     Color::fromRGB(55, 55, 55), 1);

    // TAT line
    if (tatValue >= 0.f && tatValue <= 1.f) {
      int tx = plotX + (int)(tatValue * plotW);
      painter.drawVLine(tx, plotY, plotH, tatLineColor, 1);
    }

    // Hover scrubber
    if (hoverX >= 0)
      painter.drawVLine(hoverX, plotY, plotH, Color::fromRGB(90, 90, 90), 1);

    // Curves
    if (activeChannel == CurveChannel::RGB) {
      if (!curveData.r.isIdentity())
        drawCurve(ctx, curveData.r, curveColorR, 1, false);
      if (!curveData.g.isIdentity())
        drawCurve(ctx, curveData.g, curveColorG, 1, false);
      if (!curveData.b.isIdentity())
        drawCurve(ctx, curveData.b, curveColorB, 1, false);
      drawCurve(ctx, activeChannelCurve(), curveColorRGB, 2, true);
    } else {
      drawCurve(ctx, activeChannelCurve(), channelColor(), 2, true);
    }

    if (!parametricMode)
      drawPoints(ctx);

    painter.drawRectOutline(plotX, plotY, plotW, plotH, borderColor, 1);

    drawChannelTabs(ctx, fontCache);
    if (showInputOutput && hoverX >= 0)
      drawTooltip(ctx, fontCache);
    if (parametricMode)
      drawParametricStrip(ctx, fontCache);

    needsPaint = false;
  }

  // ── Mouse ─────────────────────────────────────────────────────────────────

  bool handleMouseDown(int mx, int my) override {
    // Channel tab click
    if (hitTestTab(mx, my) >= 0) {
      activeChannel = (CurveChannel)hitTestTab(mx, my);
      selectedPoint = -1;
      markNeedsPaint();
      return true;
    }

    // Parametric slider drag
    if (parametricMode) {
      int slot = hitTestParamSlider(mx, my);
      if (slot >= 0) {
        draggingParamSlot = slot;
        dragStartX = mx;
        dragStartVal = getParamValue(slot);
        return true;
      }
      return false;
    }

    if (!inPlot(mx, my))
      return false;

    // Hit test existing point
    int hit = hitTestPoint(mx, my);
    if (hit >= 0) {
      selectedPoint = hit;
      dragging = true;
      dragStartX = mx;
      dragStartY = my;
      markNeedsPaint();
      return true;
    }

    // Add new point
    auto [nx, ny] = screenToNorm(mx, my);
    nx = std::max(0.01f, std::min(0.99f, nx)); // never override endpoints
    ny = std::max(0.f, std::min(1.f, ny));

    auto &pts = activeChannelCurve().points;

    // Don't add too close to an existing point
    for (auto &p : pts) {
      if (std::abs(p.x - nx) < 0.03f) {
        selectedPoint = -1;
        return true;
      }
    }

    pts.push_back({nx, ny});
    std::sort(
        pts.begin(), pts.end(),
        [](const CurvePoint &a, const CurvePoint &b) { return a.x < b.x; });

    // Find index after sort
    selectedPoint = -1;
    for (int i = 0; i < (int)pts.size(); ++i)
      if (std::abs(pts[i].x - nx) < 0.001f) {
        selectedPoint = i;
        break;
      }

    dragging = true;
    dragStartX = mx;
    dragStartY = my;
    notifyCurveChanged();
    markNeedsPaint();
    return true;
  }

  bool handleMouseUp(int /*mx*/, int /*my*/) override {
    if (dragging) {
      dragging = false;
      markNeedsPaint();
    }
    if (draggingParamSlot >= 0) {
      draggingParamSlot = -1;
      markNeedsPaint();
    }
    return false;
  }

  bool handleMouseMove(int mx, int my) override {
    // Parametric drag
    if (draggingParamSlot >= 0) {
      float delta = (mx - dragStartX) / float(plotW) * 2.f; // -1..+1 range
      float nv = std::max(-1.f, std::min(1.f, dragStartVal + delta));
      setParamValue(draggingParamSlot, nv);
      // Bake to point curve for preview
      activeChannelCurve() = curveData.parametric.bake();
      notifyCurveChanged();
      markNeedsPaint();
      return true;
    }

    // Point drag
    if (dragging && selectedPoint >= 0) {
      auto &pts = activeChannelCurve().points;
      auto [nx, ny] = screenToNorm(mx, my);

      // Endpoints: only move Y
      if (selectedPoint == 0 || selectedPoint == (int)pts.size() - 1) {
        pts[selectedPoint].y = std::max(0.f, std::min(1.f, ny));
      } else {
        // Constrain X between neighbours
        float xMin = pts[selectedPoint - 1].x + 0.01f;
        float xMax = pts[selectedPoint + 1].x - 0.01f;
        pts[selectedPoint].x = std::max(xMin, std::min(xMax, nx));
        pts[selectedPoint].y = std::max(0.f, std::min(1.f, ny));
      }
      notifyCurveChanged();
      markNeedsPaint();
      return true;
    }

    // Hover
    bool inP = inPlot(mx, my);
    int newHX = inP ? mx : -1;
    if (newHX != hoverX) {
      hoverX = newHX;
      markNeedsPaint();
    }
    return false;
  }

  bool handleMouseLeave() override {
    hoverX = -1;
    markNeedsPaint();
    return true;
  }

  // Double-click: delete point (except endpoints)
  bool handleLButtonDblClk(int mx, int my) {
    int hit = hitTestPoint(mx, my);
    if (hit > 0 && hit < (int)activeChannelCurve().points.size() - 1) {
      activeChannelCurve().points.erase(activeChannelCurve().points.begin() +
                                        hit);
      selectedPoint = -1;
      notifyCurveChanged();
      markNeedsPaint();
      return true;
    }
    return false;
  }

  bool handleKeyDown(int keyCode) override {
    if (selectedPoint < 0)
      return false;
    auto &pts = activeChannelCurve().points;
    if (selectedPoint >= (int)pts.size())
      return false;

    const float step = 1.f / 255.f; // 1 level
    auto &p = pts[selectedPoint];

    switch (keyCode) {
    case Key::Up:
      p.y = std::min(1.f, p.y + step);
      break;
    case Key::Down:
      p.y = std::max(0.f, p.y - step);
      break;
    case Key::Left:
      if (selectedPoint > 0)
        p.x = std::max(pts[selectedPoint - 1].x + step, p.x - step);
      break;
    case Key::Right:
      if (selectedPoint < (int)pts.size() - 1)
        p.x = std::min(pts[selectedPoint + 1].x - step, p.x + step);
      break;
    case Key::Delete:
    case Key::Backspace:
      if (selectedPoint > 0 && selectedPoint < (int)pts.size() - 1) {
        pts.erase(pts.begin() + selectedPoint);
        selectedPoint = -1;
      }
      break;
    default:
      return false;
    }

    notifyCurveChanged();
    markNeedsPaint();
    return true;
  }

private:
  // ── Layout cache ──────────────────────────────────────────────────────────
  int plotX = 0, plotY = 0, plotW = 1, plotH = 1;
  static constexpr int kTabH = 18;
  static constexpr int kParamStripH = 52;
  static constexpr int kPointR = 5; // control point radius

  // ── Interaction state ─────────────────────────────────────────────────────
  int selectedPoint = -1;
  bool dragging = false;
  int dragStartX = 0, dragStartY = 0;
  int draggingParamSlot = -1;
  float dragStartVal = 0.f;
  int hoverX = -1;

  // ── Helpers ───────────────────────────────────────────────────────────────

  std::shared_ptr<ToneCurveWidget> ptr() {
    return std::static_pointer_cast<ToneCurveWidget>(shared_from_this());
  }

  ToneCurveChannel &activeChannelCurve() {
    switch (activeChannel) {
    case CurveChannel::Red:
      return curveData.r;
    case CurveChannel::Green:
      return curveData.g;
    case CurveChannel::Blue:
      return curveData.b;
    default:
      return curveData.rgb;
    }
  }

  Color channelColor() const {
    switch (activeChannel) {
    case CurveChannel::Red:
      return curveColorR;
    case CurveChannel::Green:
      return curveColorG;
    case CurveChannel::Blue:
      return curveColorB;
    default:
      return curveColorRGB;
    }
  }

  // Normalised [0,1] → screen pixel
  int normToScreenX(float nx) const { return plotX + (int)(nx * plotW); }
  int normToScreenY(float ny) const {
    return plotY + plotH - (int)(ny * plotH);
  }

  // Screen → normalised
  std::pair<float, float> screenToNorm(int sx, int sy) const {
    float nx = (float)(sx - plotX) / std::max(1, plotW);
    float ny = 1.f - (float)(sy - plotY) / std::max(1, plotH);
    return {nx, ny};
  }

  bool inPlot(int mx, int my) const {
    return mx >= plotX && mx < plotX + plotW && my >= plotY &&
           my < plotY + plotH;
  }

  int hitTestPoint(int mx, int my) const {
    const auto &pts =
        const_cast<ToneCurveWidget *>(this)->activeChannelCurve().points;
    for (int i = 0; i < (int)pts.size(); ++i) {
      int px = normToScreenX(pts[i].x);
      int py = normToScreenY(pts[i].y);
      int dx = mx - px, dy = my - py;
      if (dx * dx + dy * dy <= (kPointR + 3) * (kPointR + 3))
        return i;
    }
    return -1;
  }

  void notifyCurveChanged() {
    if (onCurveChanged)
      onCurveChanged(curveData);
  }

  // ── Draw helpers ──────────────────────────────────────────────────────────

  void drawRegions(GraphicsContext &ctx) {
    Painter painter(ctx);
    struct Band {
      float x0, x1;
      Color col;
    };
    Band bands[] = {
        {0.00f, 0.25f, Color::fromRGB(30, 30, 40)},
        {0.25f, 0.50f, Color::fromRGB(30, 32, 30)},
        {0.50f, 0.75f, Color::fromRGB(32, 30, 28)},
        {0.75f, 1.00f, Color::fromRGB(36, 30, 26)},
    };
    for (auto &b : bands)
      painter.fillRect(normToScreenX(b.x0), plotY,
                       normToScreenX(b.x1) - normToScreenX(b.x0), plotH, b.col);

    for (float fx : {0.25f, 0.50f, 0.75f})
      painter.drawVLine(normToScreenX(fx), plotY, plotH, Color::fromRGB(45, 45, 45), 1);
  }

  void drawHistogram(GraphicsContext &ctx) {
    Painter painter(ctx);
    std::uint32_t peak = 1;
    for (auto v : histBins)
      peak = std::max(peak, v);

    for (int i = 0; i < 256; ++i) {
      float t = float(histBins[i]) / peak;
      t = (t > 0) ? (std::log(1.f + t * 9.f) / std::log(10.f)) : 0.f;
      int barH = (int)(t * plotH);
      if (barH <= 0)
        continue;

      int px0 = plotX + (int)((float)i / 255.f * plotW);
      int px1 = plotX + (int)((float)(i + 1) / 255.f * plotW);
      px1 = std::max(px0 + 1, px1);

      painter.fillRect(px0, plotY + plotH - barH, px1 - px0, barH,
                       Color::fromRGB(60, 60, 65));
    }
  }

  void drawGrid(GraphicsContext &ctx) {
    Painter painter(ctx);
    for (int i = 1; i <= 3; ++i) {
      painter.drawVLine(plotX + plotW * i / 4, plotY, plotH, gridColor, 1);
      painter.drawHLine(plotX, plotY + plotH * i / 4, plotW, gridColor, 1);
    }
  }

  // Draw one smooth curve by evaluating 256 points
  void drawCurve(GraphicsContext &ctx, const ToneCurveChannel &ch, Color col,
                 int lineWidth, bool drawFill) {
    Painter painter(ctx);

    if (drawFill) {
      std::vector<std::pair<int, int>> poly;
      poly.reserve(plotW + 4);
      poly.push_back({plotX, plotY + plotH});
      for (int px = 0; px <= plotW; ++px) {
        float nx = (float)px / plotW;
        float ny = ch.evaluate(nx);
        poly.push_back({plotX + px, normToScreenY(ny)});
      }
      poly.push_back({plotX + plotW, plotY + plotH});
painter.fillPolygonAlpha(poly, col.withAlpha(28));
    }

    std::vector<std::pair<int, int>> pts;
    pts.reserve(plotW + 1);
    for (int px = 0; px <= plotW; ++px) {
      float nx = (float)px / plotW;
      float ny = ch.evaluate(nx);
      pts.push_back({plotX + px, normToScreenY(ny)});
    }
    painter.drawPolyline(pts, col, lineWidth);
  }

  void drawPoints(GraphicsContext &ctx) {
    Painter painter(ctx);
    const auto &pts = activeChannelCurve().points;

    for (int i = 0; i < (int)pts.size(); ++i) {
      int px = normToScreenX(pts[i].x);
      int py = normToScreenY(pts[i].y);
      bool sel = (i == selectedPoint);

      // Drop shadow (offset down 2px, black, no border)
      painter.drawEllipse(px - kPointR, py - kPointR + 2, kPointR * 2,
                          kPointR * 2, Color::fromRGB(0, 0, 0), Color::fromRGB(0, 0, 0), 0);

      // Point fill + border
      Color fill = sel ? pointSelected : pointFill;
      Color border = sel ? Color::fromRGB(255, 220, 80) : pointBorder;
      painter.drawEllipse(px - kPointR, py - kPointR, kPointR * 2, kPointR * 2,
                          fill, border, 1);
    }
  }

  // ── Channel tab strip at top of widget ───────────────────────────────────

  void drawChannelTabs(GraphicsContext &ctx, FontCache &fontCache) {
    struct Tab {
      CurveChannel ch;
      const char *label;
      Color col;
    };
    Tab tabs[] = {
        {CurveChannel::RGB, "RGB", curveColorRGB},
        {CurveChannel::Red, "R", curveColorR},
        {CurveChannel::Green, "G", curveColorG},
        {CurveChannel::Blue, "B", curveColorB},
    };
    const int tabW = 32;
    int tx = x + width - 4 * tabW - 4;
    Painter painter(ctx);
    NativeFont hFont = fontCache.getFont(9, FontWeight::Bold);

    for (int i = 0; i < 4; ++i) {
      bool active = (tabs[i].ch == activeChannel);
      int sx = tx + i * tabW;

      painter.fillRect(sx, y, tabW, kTabH, active ? Color::fromRGB(45, 45, 55) : bgColor);

      if (active)
        painter.drawHLine(sx + 2, y + kTabH - 2, tabW - 4, tabs[i].col, 2);

      painter.drawTextA(tabs[i].label, sx, y, tabW, kTabH, hFont,
                        active ? tabs[i].col : Color::fromRGB(80, 80, 90),
                        DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
  }
  // ── In/Out tooltip ────────────────────────────────────────────────────────

  void drawTooltip(GraphicsContext &ctx, FontCache &fontCache) {
    if (hoverX < plotX || hoverX > plotX + plotW)
      return;
    Painter painter(ctx);

    float nx = (float)(hoverX - plotX) / plotW;
    float ny = activeChannelCurve().evaluate(nx);
    int inV = (int)(nx * 255.f + 0.5f);
    int outV = (int)(ny * 255.f + 0.5f);

    char buf[32];
    std::snprintf(buf, sizeof(buf), "%d \xe2\x86\x92 %d", inV, outV); // → arrow

    NativeFont hFont = fontCache.getFont(9, FontWeight::Normal);
    int tw = 0, th = 0;
    painter.measureText(std::wstring(buf, buf + strlen(buf)), hFont, tw, th);

    int ttx = hoverX + 6;
    if (ttx + tw > plotX + plotW)
      ttx = hoverX - tw - 4;

    painter.drawTextA(std::string(buf), ttx, plotY + 4, tw + 2, th + 2, hFont,
                      Color::fromRGB(200, 200, 200), DT_LEFT | DT_TOP | DT_SINGLELINE);

    // Output dot on curve
    int dotY = normToScreenY(ny);
    painter.drawEllipse(hoverX - 3, dotY - 3, 6, 6, channelColor(),
                        channelColor(), 0);
  }

  // ── Parametric slider strip ───────────────────────────────────────────────

  void drawParametricStrip(GraphicsContext &ctx, FontCache &fontCache) {
    int sy = plotY + plotH + 6;
    int sw = plotW / 4;
    Painter painter(ctx);

    struct Slot {
      const char *label;
      float value;
      Color col;
    };
    auto &p = curveData.parametric;
    Slot slots[] = {
        {"Shadows", p.shadows, Color::fromRGB(80, 100, 160)},
        {"Darks", p.darks, Color::fromRGB(120, 120, 140)},
        {"Lights", p.lights, Color::fromRGB(180, 160, 110)},
        {"Highlights", p.highlights, Color::fromRGB(220, 200, 140)},
    };

    NativeFont hFont = fontCache.getFont(8, FontWeight::Normal);

    for (int i = 0; i < 4; ++i) {
      int sx = plotX + i * sw;
      int trackY = sy + 18;
      int trackX0 = sx + 4;
      int trackX1 = sx + sw - 4;
      int trackMid = (trackX0 + trackX1) / 2;
      int thumbX = trackMid + (int)(slots[i].value * (trackX1 - trackMid));

      // Label
      painter.drawTextA(slots[i].label, sx, sy, sw, 14, hFont,
                        Color::fromRGB(100, 100, 110),
                        DT_CENTER | DT_VCENTER | DT_SINGLELINE);

      // Track background
      painter.drawHLine(trackX0, trackY, trackX1 - trackX0, Color::fromRGB(50, 50, 55), 2);

      // Filled portion from midpoint
      if (thumbX != trackMid)
        painter.drawHLine(std::min(trackMid, thumbX), trackY, abs(thumbX - trackMid),
                          slots[i].col, 2);

      // Thumb
      Color thumbC =
          (draggingParamSlot == i) ? Color::fromRGB(255, 220, 50) : slots[i].col;
      painter.drawEllipse(thumbX - 5, trackY - 5, 10, 10, thumbC, thumbC, 0);

      // Value label
      char vbuf[8];
      std::snprintf(vbuf, sizeof(vbuf), "%+.0f", slots[i].value * 100.f);
      painter.drawTextA(vbuf, sx, trackY + 8, sw, 12, hFont, Color::fromRGB(160, 160, 160),
                        DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
  }

  // ── Tab hit test ─────────────────────────────────────────────────────────
  int hitTestTab(int mx, int my) const {
    if (my < y || my > y + kTabH)
      return -1;
    const int tabW = 32;
    int tx = x + width - 4 * tabW - 4;
    int slot = (mx - tx) / tabW;
    if (slot < 0 || slot > 3)
      return -1;
    return slot; // maps directly to CurveChannel enum
  }

  // ── Parametric slider hit test ────────────────────────────────────────────
  int hitTestParamSlider(int mx, int my) const {
    int sy = plotY + plotH + 6;
    if (my < sy || my > sy + kParamStripH)
      return -1;
    int sw = plotW / 4;
    int slot = (mx - plotX) / sw;
    return (slot >= 0 && slot < 4) ? slot : -1;
  }

  float getParamValue(int slot) const {
    switch (slot) {
    case 0:
      return curveData.parametric.shadows;
    case 1:
      return curveData.parametric.darks;
    case 2:
      return curveData.parametric.lights;
    case 3:
      return curveData.parametric.highlights;
    }
    return 0.f;
  }

  void setParamValue(int slot, float v) {
    switch (slot) {
    case 0:
      curveData.parametric.shadows = v;
      break;
    case 1:
      curveData.parametric.darks = v;
      break;
    case 2:
      curveData.parametric.lights = v;
      break;
    case 3:
      curveData.parametric.highlights = v;
      break;
    }
  }
};

// ============================================================================
//  FACTORY
// ============================================================================

using ToneCurveWidgetPtr = std::shared_ptr<ToneCurveWidget>;

inline ToneCurveWidgetPtr ToneCurve() {
  return std::make_shared<ToneCurveWidget>();
}

inline ToneCurveWidgetPtr ToneCurve(int size) {
  auto w = std::make_shared<ToneCurveWidget>();
  w->setSize(size, size);
  return w;
}

inline ToneCurveWidgetPtr ToneCurve(int w, int h) {
  auto widget = std::make_shared<ToneCurveWidget>();
  widget->setSize(w, h);
  return widget;
}

/*
// ============================================================================
//  USAGE EXAMPLES
// ============================================================================

// ── 1. Basic drop-in (square, default RGB channel) ───────────────────────────
ToneCurve(256)
    ->setShowHistogram(true)
    ->setShowRegions(true)
    ->setOnCurveChanged([&](const ToneCurveData& d){
        // d.rgb.buildLUT()  → pass to shader as a 1D texture or uniform array
    });


// ── 2. Reactive binding with State ───────────────────────────────────────────
State<ToneCurveData> curveState;

ToneCurve(280, 240)
    ->setCurveData(curveState)
    ->setActiveChannel(CurveChannel::RGB)
    ->setOnCurveChanged([&](const ToneCurveData& d){
        curveState.set(d);           // write-back so other widgets stay in sync
    });

// Somewhere else in the app — evaluating the curve for export:
auto lut = curveState.get().rgb.buildLUT();


// ── 3. With histogram under the curve (feed HistogramData from your surface) ─
surface->onHistogramUpdated = [&](const HistogramData& h){
    histState.set(h);
    // Also feed the raw bins into the curve widget:
    curveWidget->setHistogram(h.r, h.g, h.b);
};


// ── 4. Parametric mode (Lightroom's slider-based tone curve) ─────────────────
ToneCurve(256)
    ->setParametricMode(true)   // shows Shadows/Darks/Lights/Highlights sliders
    ->setOnCurveChanged([&](const ToneCurveData& d){
        // d.parametric has the raw slider values
        // d.rgb is the baked point curve ready for GPU
    });


// ── 5. TAT — Targeted Adjustment Tool ────────────────────────────────────────
// When the mouse moves over your image canvas, sample the pixel luminosity
// and push it into the curve widget as a guide line:
canvas->onMouseMove = [&](float nx, float ){
    float lum = sampleImageLuminance(nx, ny);  // your function
    curveWidget->setTATValue(lum);
};

// On mouse drag over the image (vertical drag = curve adjustment):
canvas->onDragDelta = [&](float , float dy){
    float input = curveWidget->tatValue;
    if (input < 0) return;
    // Find or create point near that input value and nudge it
    auto& ch = curveState.get().rgb;
    // ... insert/move point at `input` by `dy * sensitivity`
    curveState.set(ch_updated);
};


// ── 6. Wiring into the image_editor.hpp shader ───────────────────────────────
// In your ImageEditorApp::build():
auto curveWidget = ToneCurve(258, 200)
    ->setShowHistogram(true)
    ->setShowRegions(true)
    ->setOnCurveChanged([this](const ToneCurveData& d){
        // Upload LUTs to GL as 1D textures, or encode as uniform float arrays
        auto lutR = d.r.buildLUT();   // std::array<uint8_t,256>
        auto lutG = d.g.buildLUT();
        auto lutB = d.b.buildLUT();
        auto lutRGB = d.rgb.buildLUT();
        surface_->uploadCurveLUTs(lutRGB, lutR, lutG, lutB);
        if (canvasPtr_) canvasPtr_->redraw();
    });
*/

#endif // FLUX_TONE_CURVE_HPP