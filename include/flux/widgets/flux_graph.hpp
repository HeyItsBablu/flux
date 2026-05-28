#pragma once

// ============================================================================
// flux_graph.hpp  —  Cross-platform graph widget for FluxUI
// ============================================================================

#include "../flux_core.hpp"
#include "../flux_state.hpp"
#include "../flux_widget.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

// ============================================================================
// GraphSeries
// ============================================================================

struct GraphSeries
{
  std::string label;
  std::vector<float> values;
  Color color = Color::fromRGB(51, 153, 255);
  float lineWidth = 2.0f; // used for Line / Area stroke
};

// ============================================================================
// GraphType
// ============================================================================

enum class GraphType
{
  Line,
  Bar,
  Area,
  Scatter
};

// ============================================================================
// GraphWidget
// ============================================================================

class GraphWidget : public Widget
{
public:
  // ── Public data ────────────────────────────────────────────────────────────
  std::vector<GraphSeries> series;
  GraphType graphType = GraphType::Line;

  std::vector<std::string> xLabels;
  std::string xAxisTitle;
  std::string yAxisTitle;
  std::string title;

  bool autoRange = true;
  float yMin = 0.f;
  float yMax = 1.f;

  bool showGrid = true;
  bool showLegend = true;

  // Callback: called with (seriesIndex, pointIndex, value) on click
  std::function<void(int, int, float)> onPointClick;

  // ── Colors (all theme-able) ────────────────────────────────────────────────
  Color bgColor = Color::fromRGB(22, 22, 35);
  Color gridColor = Color::fromRGBA(255, 255, 255, 20);
  Color axisColor = Color::fromRGBA(180, 180, 180, 200);
  Color labelColor = Color::fromRGB(160, 160, 175);
  Color titleColor = Color::fromRGB(220, 220, 235);
  Color tooltipBgColor = Color::fromRGBA(30, 30, 50, 230);
  Color tooltipTextColor = Color::fromRGB(230, 230, 240);

  // ── Construction ──────────────────────────────────────────────────────────
  GraphWidget()
  {
    autoWidth = false;
    autoHeight = false;
    width = 400;
    height = 300;
    isFocusable = true;
  }

  // ── Fluent API ─────────────────────────────────────────────────────────────

  std::shared_ptr<GraphWidget>
  addSeries(const std::string &label, const std::vector<float> &vals,
            Color color = Color::fromRGB(51, 153, 255))
  {
    GraphSeries s;
    s.label = label;
    s.values = vals;
    s.color = color;
    series.push_back(std::move(s));
    computeRange();
    markNeedsPaint();
    return std::static_pointer_cast<GraphWidget>(shared_from_this());
  }

  // Reactive binding: series updates whenever the State changes.
  std::shared_ptr<GraphWidget>
  addSeries(const std::string &label, State<std::vector<float>> &state,
            Color color = Color::fromRGB(51, 153, 255))
  {
    GraphSeries s;
    s.label = label;
    s.values = state.get();
    s.color = color;
    int idx = static_cast<int>(series.size());
    series.push_back(std::move(s));
    state.bindProperty(
        shared_from_this(),
        [idx](Widget *w, const std::vector<float> &vals)
        {
          auto *self = static_cast<GraphWidget *>(w);
          if (idx < static_cast<int>(self->series.size()))
          {
            self->series[idx].values = vals;
            self->computeRange();
          }
        },
        false);
    computeRange();
    markNeedsPaint();
    return std::static_pointer_cast<GraphWidget>(shared_from_this());
  }

  std::shared_ptr<GraphWidget> setType(GraphType t)
  {
    graphType = t;
    markNeedsPaint();
    return std::static_pointer_cast<GraphWidget>(shared_from_this());
  }

  std::shared_ptr<GraphWidget> setTitle(const std::string &t)
  {
    title = t;
    markNeedsPaint();
    return std::static_pointer_cast<GraphWidget>(shared_from_this());
  }

  std::shared_ptr<GraphWidget> setXLabels(const std::vector<std::string> &lbs)
  {
    xLabels = lbs;
    markNeedsPaint();
    return std::static_pointer_cast<GraphWidget>(shared_from_this());
  }

  std::shared_ptr<GraphWidget> setXAxisTitle(const std::string &t)
  {
    xAxisTitle = t;
    markNeedsPaint();
    return std::static_pointer_cast<GraphWidget>(shared_from_this());
  }

  std::shared_ptr<GraphWidget> setYAxisTitle(const std::string &t)
  {
    yAxisTitle = t;
    markNeedsPaint();
    return std::static_pointer_cast<GraphWidget>(shared_from_this());
  }

  std::shared_ptr<GraphWidget> setYRange(float mn, float mx)
  {
    yMin = mn;
    yMax = mx;
    autoRange = false;
    markNeedsPaint();
    return std::static_pointer_cast<GraphWidget>(shared_from_this());
  }

  std::shared_ptr<GraphWidget> setShowGrid(bool v)
  {
    showGrid = v;
    markNeedsPaint();
    return std::static_pointer_cast<GraphWidget>(shared_from_this());
  }

  std::shared_ptr<GraphWidget> setShowLegend(bool v)
  {
    showLegend = v;
    markNeedsPaint();
    return std::static_pointer_cast<GraphWidget>(shared_from_this());
  }

  std::shared_ptr<GraphWidget> setColors(Color bg, Color grid, Color axis,
                                         Color label)
  {
    bgColor = bg;
    gridColor = grid;
    axisColor = axis;
    labelColor = label;
    markNeedsPaint();
    return std::static_pointer_cast<GraphWidget>(shared_from_this());
  }

  std::shared_ptr<GraphWidget>
  setOnPointClick(std::function<void(int, int, float)> cb)
  {
    onPointClick = std::move(cb);
    return std::static_pointer_cast<GraphWidget>(shared_from_this());
  }

  std::shared_ptr<GraphWidget> clearSeries()
  {
    series.clear();
    markNeedsPaint();
    return std::static_pointer_cast<GraphWidget>(shared_from_this());
  }

  std::shared_ptr<GraphWidget> setSize(int w, int h)
  {
    width = w;
    height = h;
    autoWidth = autoHeight = false;
    markNeedsLayout();
    return std::static_pointer_cast<GraphWidget>(shared_from_this());
  }

  // ── Widget overrides ───────────────────────────────────────────────────────

  void computeLayout(GraphicsContext & /*ctx*/,
                     const BoxConstraints &constraints,
                     FontCache & /*fontCache*/) override
  {

    if (!autoWidth)
      width = (constraints.maxWidth < width && constraints.maxWidth > 0)
                  ? constraints.maxWidth
                  : width;
    else
      width = constraints.clampWidth(constraints.maxWidth);

    if (!autoHeight)
      height = (constraints.maxHeight < height && constraints.maxHeight > 0)
                   ? constraints.maxHeight
                   : height;
    else
      height = constraints.clampHeight(constraints.maxHeight);

    applyConstraints();
    needsLayout = false;
  }

  void render(GraphicsContext &ctx, FontCache &fontCache) override
  {
    if (!visible || width <= 0 || height <= 0)
      return;

    computeRange();

    Painter p(ctx);

    // ── Background ─────────────────────────────────────────────────────────
    p.fillRoundedRect(x, y, width, height, borderRadius > 0 ? borderRadius : 6,
                      bgColor);

    // ── Clip to widget bounds ──────────────────────────────────────────────
    p.pushClipRect(x, y, width, height);

    // ── Compute plot area ──────────────────────────────────────────────────

    int marginL = computeMarginL();
    int marginR = computeMarginR(fontCache);
    int marginT = (!title.empty() ? kTitleFontSize + 10 : 0) + 10;
    int marginB =
        kAxisFontSize + (xAxisTitle.empty() ? 0 : kAxisFontSize + 4) + 24;

    PlotArea pa = makePlotArea(marginL, marginR, marginT, marginB);

    if (pa.w <= 0 || pa.h <= 0)
    {
      p.popClipRect();
      needsPaint = false;
      return;
    }

    // Cache the plot area so tooltip / hit-testing use identical geometry.
    cachedPlotArea_ = pa;
    cachedPlotAreaValid_ = true;

    // ── Draw components ────────────────────────────────────────────────────
    if (showGrid)
      drawGrid(p, pa, fontCache);
    drawAxes(p, pa);
    drawAxisLabels(p, ctx, pa, fontCache);
    drawTitle(p, ctx, fontCache);

    for (int si = 0; si < static_cast<int>(series.size()); ++si)
    {
      if (series[si].values.empty())
        continue;
      switch (graphType)
      {
      case GraphType::Line:
        drawLine(p, pa, si);
        break;
      case GraphType::Bar:
        drawBars(p, pa, si);
        break;
      case GraphType::Area:
        drawArea(p, pa, si);
        break;
      case GraphType::Scatter:
        drawScatter(p, pa, si);
        break;
      }
    }

    if (showLegend && series.size() > 1)
      drawLegend(p, ctx, fontCache);

    if (tooltipVisible_)
      drawTooltip(p, ctx, fontCache);

    p.popClipRect();

    // ── Border ─────────────────────────────────────────────────────────────
    if (hasBorder)
      p.drawBorder(x, y, width, height, borderRadius > 0 ? borderRadius : 6,
                   getCurrentBorderColor(), borderWidth);

    needsPaint = false;
  }

  // ── Mouse events ──────────────────────────────────────────────────────────

  bool handleMouseMove(int mx, int my) override
  {
    if (!contains(mx, my))
    {
      if (tooltipVisible_)
      {
        tooltipVisible_ = false;
        markNeedsPaint();
      }
      return false;
    }
    updateTooltip(mx, my);
    return true;
  }

  bool handleMouseDown(int mx, int my) override
  {
    if (!contains(mx, my) || !onPointClick)
      return false;
    auto [si, pi] = nearestPoint(mx, my);
    if (si >= 0 && pi >= 0)
      onPointClick(si, pi, series[si].values[pi]);
    return true;
  }

  bool handleMouseLeave() override
  {
    if (tooltipVisible_)
    {
      tooltipVisible_ = false;
      markNeedsPaint();
    }
    return false;
  }

private:
  // ── Constants ──────────────────────────────────────────────────────────────
  static constexpr int kTitleFontSize = 13;
  static constexpr int kAxisFontSize = 10;
  static constexpr int kLabelFontSize = 10;
  static constexpr int kLegendFontSize = 10;
  static constexpr int kTooltipFontSize = 11;
  static constexpr int kDotRadius = 3;
  static constexpr int kYTicks = 6;
  static constexpr int kXTickMax = 8;

  // ── Plot-area descriptor ──────────────────────────────────────────────────
  struct PlotArea
  {
    int x, y, w, h;
  };

  // ── Cached plot area (set each render, read by hit-test / tooltip) ─────────

  mutable PlotArea cachedPlotArea_ = {0, 0, 0, 0};
  mutable bool cachedPlotAreaValid_ = false;

  // ── Hover / tooltip state ──────────────────────────────────────────────────
  bool tooltipVisible_ = false;
  int tooltipX_ = 0;
  int tooltipY_ = 0;
  int tooltipSeriesIdx_ = -1;
  int tooltipPointIdx_ = -1;
  std::string tooltipText_;

  // ── Range computation ──────────────────────────────────────────────────────
  void computeRange()
  {
    if (!autoRange)
      return;
    bool first = true;
    for (auto &s : series)
      for (float v : s.values)
      {
        if (first)
        {
          yMin = yMax = v;
          first = false;
        }
        else
        {
          yMin = std::min(yMin, v);
          yMax = std::max(yMax, v);
        }
      }
    if (first)
    {
      yMin = 0.f;
      yMax = 1.f;
      return;
    }
    if (yMin == yMax)
    {
      yMin -= 1.f;
      yMax += 1.f;
      return;
    }
    float pad = (yMax - yMin) * 0.08f;
    yMin -= pad;
    yMax += pad;
  }

  // ── Margin helpers (single source of truth) ────────────────────────────────

  int computeMarginL() const
  {
    float absMax = std::max(std::abs(yMin), std::abs(yMax));
    int digits = absMax >= 10000  ? 6
                 : absMax >= 1000 ? 5
                 : absMax >= 100  ? 4
                                  : 3;
    return (!yAxisTitle.empty() ? kAxisFontSize + 4 : 0) + digits * 7 + 18;
  }

  int legendWidthEstimate() const
  {
    int maxLen = 0;
    for (auto &s : series)
      maxLen = std::max(maxLen, static_cast<int>(s.label.size()));
    return maxLen * 7 + 24;
  }

  int computeMarginR() const
  {
    return showLegend && series.size() > 1 ? legendWidthEstimate() + 16 : 12;
  }

  // Overload used from render() which already holds a FontCache ref — kept for
  // symmetry with the rest of the draw helpers but internally identical.
  int computeMarginR(FontCache & /*fc*/) const { return computeMarginR(); }

  PlotArea makePlotArea(int mL, int mR, int mT, int mB) const
  {
    return {x + mL, y + mT, width - mL - mR, height - mT - mB};
  }

  // ── Coordinate helpers ─────────────────────────────────────────────────────
  int dataToPixY(float v, const PlotArea &pa) const
  {
    float t = (v - yMin) / (yMax - yMin);
    return pa.y + pa.h - static_cast<int>(t * pa.h);
  }

  int indexToPixX(int i, int n, const PlotArea &pa) const
  {
    if (n <= 1)
      return pa.x + pa.w / 2;
    return pa.x + static_cast<int>(static_cast<float>(i) / (n - 1) * pa.w);
  }

  struct BarGeom
  {
    int cellW;  // total width reserved per data-point group
    int barW;   // width of one individual bar
    int groupW; // total visual width of all bars in one group
  };

  BarGeom barGeom(int n, const PlotArea &pa) const
  {
    BarGeom g;
    int nseries = std::max(1, static_cast<int>(series.size()));
    g.cellW = (n > 0) ? pa.w / n : 4;
    // Divide the 65% fill across all series with 2px gaps between them.
    int totalFill = static_cast<int>(g.cellW * 0.65f);
    int gaps = (nseries - 1) * 2;
    g.barW = std::max(1, (totalFill - gaps) / nseries);
    g.groupW = g.barW * nseries + gaps;
    return g;
  }

  int barLeft(int i, int si, const BarGeom &bg, int n,
              const PlotArea &pa) const
  {
    if (n == 0)
      return pa.x;
    int cellCenter = pa.x + i * bg.cellW + bg.cellW / 2;
    int groupStart = cellCenter - bg.groupW / 2;
    return groupStart + si * (bg.barW + 2);
  }

  bool contains(int mx, int my) const
  {
    return mx >= x && mx < x + width && my >= y && my < y + height;
  }

  // ── Draw: grid ────────────────────────────────────────────────────────────
  void drawGrid(Painter &p, const PlotArea &pa, FontCache &) const
  {
    for (int i = 0; i <= kYTicks; ++i)
    {
      int ly = pa.y + static_cast<int>(static_cast<float>(i) / kYTicks * pa.h);
      p.drawHLine(pa.x, ly, pa.w, gridColor, 1);
    }

    int nCols = xGridCount();
    for (int i = 0; i <= nCols; ++i)
    {
      int lx = pa.x + static_cast<int>(static_cast<float>(i) / nCols * pa.w);
      p.drawVLine(lx, pa.y, pa.h, gridColor, 1);
    }
  }

  int xGridCount() const
  {
    int n = xLabelCount();
    if (n <= 0)
      return kXTickMax;
    // Bar charts: one column per data point → N grid lines needs N divisor.
    // Line/Scatter/Area: lines sit between grid posts → N-1 divisor.
    if (graphType == GraphType::Bar)
      return std::min(kXTickMax, n);
    return std::min(kXTickMax, n - 1);
  }

  // ── Draw: axes ─────────────────────────────────────────────────────────────
  void drawAxes(Painter &p, const PlotArea &pa) const
  {
    p.drawHLine(pa.x, pa.y + pa.h, pa.w, axisColor, 1);
    p.drawVLine(pa.x, pa.y, pa.h, axisColor, 1);
  }

  // ── Draw: axis labels + titles ─────────────────────────────────────────────
  void drawAxisLabels(Painter &p, GraphicsContext & /*ctx*/, const PlotArea &pa,
                      FontCache &fc) const
  {
    NativeFont font = fc.getFont(kAxisFontSize, FontWeight::Normal);

    // Y tick labels
    for (int i = 0; i <= kYTicks; ++i)
    {
      float t = static_cast<float>(i) / kYTicks;
      float val = yMin + t * (yMax - yMin);
      int ly = pa.y + pa.h - static_cast<int>(t * pa.h);

      char buf[32];
      if (std::abs(val) >= 10000)
        std::snprintf(buf, sizeof(buf), "%.0f", val);
      else if (std::abs(val) >= 1000)
        std::snprintf(buf, sizeof(buf), "%.0f", val);
      else if (std::abs(val) >= 10)
        std::snprintf(buf, sizeof(buf), "%.1f", val);
      else
        std::snprintf(buf, sizeof(buf), "%.2f", val);

      int tw = 0, th = 0;

      p.measureText(toWideString(buf), font, tw, th);
      p.drawTextA(buf, pa.x - tw - 4, ly - th / 2, tw + 2, th, font, labelColor,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }

    // X tick labels
    int n = xLabelCount();
    if (n > 0)
    {
      int step = std::max(1, n / kXTickMax);
      for (int i = 0; i < n; i += step)
      {
        std::string lbl =
            (!xLabels.empty() && i < static_cast<int>(xLabels.size()))
                ? xLabels[i]
                : std::to_string(i);
        int px = indexToPixX(i, n, pa);
        int tw = 0, th = 0;

        p.measureText(toWideString(lbl), font, tw, th);
        p.drawTextA(lbl, px - tw / 2, pa.y + pa.h + 4, tw + 2, th + 2, font,
                    labelColor, DT_LEFT | DT_TOP | DT_SINGLELINE);
      }
    }

    // Y axis title — drawn character-by-character vertically.

    if (!yAxisTitle.empty())
    {
      NativeFont tf = fc.getFont(kAxisFontSize, FontWeight::Normal);
      int chW = 0, chH = 0;
      // Measure a representative character to get per-char height.
      p.measureText(L"M", tf, chW, chH);
      int totalH = static_cast<int>(yAxisTitle.size()) * (chH + 1);
      int lx = x + 2;
      int ly = pa.y + pa.h / 2 - totalH / 2;
      for (int c = 0; c < static_cast<int>(yAxisTitle.size()) && c < 20; ++c)
      {
        char ch[2] = {yAxisTitle[c], '\0'};
        p.drawTextA(ch, lx, ly + c * (chH + 1), chW + 4, chH + 2, tf,
                    labelColor, DT_CENTER | DT_TOP | DT_SINGLELINE);
      }
    }

    // X axis title
    if (!xAxisTitle.empty())
    {
      NativeFont tf = fc.getFont(kAxisFontSize, FontWeight::Normal);
      int tw = 0, th = 0;
      p.measureText(toWideString(xAxisTitle), tf, tw, th);
      p.drawTextA(xAxisTitle, pa.x + (pa.w - tw) / 2,
                  pa.y + pa.h + kAxisFontSize + 8, tw + 4, th + 2, tf,
                  labelColor, DT_LEFT | DT_TOP | DT_SINGLELINE);
    }
  }

  void drawTitle(Painter &p, GraphicsContext & /*ctx*/, FontCache &fc) const
  {
    if (title.empty())
      return;
    NativeFont tf = fc.getFont(kTitleFontSize, FontWeight::Bold);
    int tw = 0, th = 0;

    p.measureText(toWideString(title), tf, tw, th);
    p.drawTextA(title, x + (width - tw) / 2, y + 6, tw + 4, th + 2, tf,
                titleColor, DT_LEFT | DT_TOP | DT_SINGLELINE);
  }

  // ── Draw: line series ──────────────────────────────────────────────────────
  void drawLine(Painter &p, const PlotArea &pa, int si) const
  {
    const auto &s = series[si];
    int n = static_cast<int>(s.values.size());
    if (n < 2)
    {
      drawDots(p, pa, si);
      return;
    }

    std::vector<std::pair<int, int>> pts;
    pts.reserve(n);
    for (int i = 0; i < n; ++i)
      pts.emplace_back(indexToPixX(i, n, pa), dataToPixY(s.values[i], pa));

    p.drawPolyline(pts, s.color, std::max(1, static_cast<int>(s.lineWidth)));
    drawDots(p, pa, si);
  }

  void drawDots(Painter &p, const PlotArea &pa, int si) const
  {
    const auto &s = series[si];
    int n = static_cast<int>(s.values.size());
    for (int i = 0; i < n; ++i)
    {
      int cx = indexToPixX(i, n, pa);
      int cy = dataToPixY(s.values[i], pa);
      int r = kDotRadius;
      p.fillRoundedRect(cx - r, cy - r, r * 2, r * 2, r, s.color);
    }
  }

  // ── Draw: bar series ───────────────────────────────────────────────────────

  void drawBars(Painter &p, const PlotArea &pa, int si) const
  {
    const auto &s = series[si];
    int n = static_cast<int>(s.values.size());
    if (n == 0)
      return;

    BarGeom bg = barGeom(n, pa);
    int baseline = dataToPixY(std::max(0.f, yMin), pa);

    for (int i = 0; i < n; ++i)
    {
      int bx = barLeft(i, si, bg, n, pa);
      int top = dataToPixY(s.values[i], pa);
      int bh = std::abs(baseline - top);
      if (bh == 0)
        bh = 1; // always draw at least 1px

      if (s.values[i] >= 0)
        p.fillRect(bx, top, bg.barW, bh, s.color);
      else
        p.fillRect(bx, baseline, bg.barW, bh, s.color);

      Color border = s.color.darken(40);
      p.drawRectOutline(bx, s.values[i] >= 0 ? top : baseline, bg.barW, bh,
                        border, 1);
    }
  }

  // ── Draw: area series ──────────────────────────────────────────────────────
  void drawArea(Painter &p, const PlotArea &pa, int si) const
  {
    const auto &s = series[si];
    int n = static_cast<int>(s.values.size());
    if (n < 2)
    {
      drawDots(p, pa, si);
      return;
    }

    int baseline = dataToPixY(std::max(0.f, yMin), pa);

    std::vector<std::pair<int, int>> poly;
    poly.reserve(n * 2 + 2);

    for (int i = 0; i < n; ++i)
      poly.emplace_back(indexToPixX(i, n, pa), dataToPixY(s.values[i], pa));

    for (int i = n - 1; i >= 0; --i)
      poly.emplace_back(indexToPixX(i, n, pa), baseline);

    Color fillCol = s.color.withAlpha(60);
    p.fillPolygonAlpha(poly, fillCol);

    std::vector<std::pair<int, int>> pts(poly.begin(), poly.begin() + n);
    p.drawPolyline(pts, s.color, std::max(1, static_cast<int>(s.lineWidth)));
    drawDots(p, pa, si);
  }

  // ── Draw: scatter series ───────────────────────────────────────────────────
  void drawScatter(Painter &p, const PlotArea &pa, int si) const
  {
    const auto &s = series[si];
    int n = static_cast<int>(s.values.size());
    int r = kDotRadius + 1;

    for (int i = 0; i < n; ++i)
    {
      int cx = indexToPixX(i, n, pa);
      int cy = dataToPixY(s.values[i], pa);
      p.fillRoundedRect(cx - r, cy - r, r * 2, r * 2, r,
                        s.color.withAlpha(180));
      p.drawRoundedRectOutline(cx - r, cy - r, r * 2, r * 2, r * 2, s.color, 1);
    }
  }

  // ── Draw: legend ──────────────────────────────────────────────────────────
  void drawLegend(Painter &p, GraphicsContext & /*ctx*/, FontCache &fc) const
  {
    NativeFont font = fc.getFont(kLegendFontSize, FontWeight::Normal);
    int lineH = kLegendFontSize + 6;
    int swatchW = 12;

    int startX = x + width - legendWidthEstimate() - 8;
    int startY = y + 32;

    for (int si = 0; si < static_cast<int>(series.size()); ++si)
    {
      int ly = startY + si * lineH;
      p.fillRoundedRect(startX, ly + 2, swatchW, kLegendFontSize - 2, 2,
                        series[si].color);
      int tw = 0, th = 0;
      p.measureText(toWideString(series[si].label), font, tw, th);
      p.drawTextA(series[si].label, startX + swatchW + 4, ly, tw + 4, lineH,
                  font, labelColor, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }
  }

  // ── Tooltip helpers ────────────────────────────────────────────────────────

  const PlotArea &currentPlotArea() const { return cachedPlotArea_; }

  int xLabelCount() const
  {
    for (auto &s : series)
      if (!s.values.empty())
        return static_cast<int>(s.values.size());
    return 0;
  }

  void updateTooltip(int mx, int my)
  {
    auto [si, pi] = nearestPoint(mx, my);
    bool changed = (si != tooltipSeriesIdx_ || pi != tooltipPointIdx_);

    if (si >= 0 && pi >= 0)
    {
      tooltipVisible_ = true;
      tooltipSeriesIdx_ = si;
      tooltipPointIdx_ = pi;
      tooltipX_ = mx + 12;
      tooltipY_ = my - 8;

      char buf[64];
      std::snprintf(buf, sizeof(buf), "%.3g", series[si].values[pi]);
      tooltipText_ = series[si].label.empty() ? std::string(buf)
                                              : series[si].label + ": " + buf;
    }
    else
    {
      tooltipVisible_ = false;
    }

    if (changed)
      markNeedsPaint();
  }

  // Returns {seriesIdx, pointIdx} of the nearest data point within 20px.

  std::pair<int, int> nearestPoint(int mx, int my) const
  {
    if (!cachedPlotAreaValid_)
      return {-1, -1};
    const PlotArea &pa = cachedPlotArea_;
    int bestSi = -1, bestPi = -1;
    int bestDist = 21; // threshold px

    for (int si = 0; si < static_cast<int>(series.size()); ++si)
    {
      int n = static_cast<int>(series[si].values.size());
      for (int pi = 0; pi < n; ++pi)
      {
        int px = indexToPixX(pi, n, pa);
        int py = dataToPixY(series[si].values[pi], pa);
        int d = static_cast<int>(std::sqrt(static_cast<double>(
            (mx - px) * (mx - px) + (my - py) * (my - py))));
        if (d < bestDist)
        {
          bestDist = d;
          bestSi = si;
          bestPi = pi;
        }
      }
    }
    return {bestSi, bestPi};
  }

  void drawTooltip(Painter &p, GraphicsContext & /*ctx*/, FontCache &fc) const
  {
    if (tooltipText_.empty())
      return;
    NativeFont font = fc.getFont(kTooltipFontSize, FontWeight::Normal);
    int tw = 0, th = 0;

    p.measureText(toWideString(tooltipText_), font, tw, th);

    int pad = 6;
    int bx = tooltipX_;
    int by = tooltipY_ - th - pad * 2;

    // Keep inside widget horizontally.
    if (bx + tw + pad * 2 > x + width)
      bx = tooltipX_ - tw - pad * 2 - 14;
    if (bx < x)
      bx = x + 2;

    if (by < y)
      by = tooltipY_ + 12;
    if (by + th + pad * 2 > y + height)
      by = y + height - th - pad * 2 - 4;

    p.fillRoundedRect(bx, by, tw + pad * 2, th + pad * 2, 4, tooltipBgColor);
    p.drawBorder(bx, by, tw + pad * 2, th + pad * 2, 4,
                 Color::fromRGBA(255, 255, 255, 40), 1);
    p.drawTextA(tooltipText_, bx + pad, by + pad, tw + 2, th + 2, font,
                tooltipTextColor, DT_LEFT | DT_TOP | DT_SINGLELINE);
  }
};

// ============================================================================
// Factory helpers
// ============================================================================

using GraphWidgetPtr = std::shared_ptr<GraphWidget>;

inline GraphWidgetPtr Graph() { return std::make_shared<GraphWidget>(); }

inline GraphWidgetPtr Graph(int w, int h)
{
  return std::make_shared<GraphWidget>()->setSize(w, h);
}