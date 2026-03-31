#ifndef FLUX_HISTOGRAM_HPP
#define FLUX_HISTOGRAM_HPP

#include "flux_core.hpp"
#include "flux_state.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <string>
#include <vector>

// ============================================================================
// HISTOGRAM DATA
// ============================================================================

// Holds per-channel bin counts (256 bins each, matching 8-bit pixel values).
// You can fill this from a bitmap, a State<>, or set it manually.
struct HistogramData {
  std::array<uint32_t, 256> r{};
  std::array<uint32_t, 256> g{};
  std::array<uint32_t, 256> b{};
  std::array<uint32_t, 256> lum{}; // luminosity (composite)

  // Convenience: build from a flat RGBX / RGBA byte buffer
  static HistogramData fromPixels(const uint8_t *data, int pixelCount,
                                  int channels = 4 /*RGBA or RGBX*/) {
    HistogramData h;
    for (int i = 0; i < pixelCount; ++i) {
      const uint8_t *p = data + i * channels;
      uint8_t rv = p[0], gv = p[1], bv = p[2];
      h.r[rv]++;
      h.g[gv]++;
      h.b[bv]++;
      // Rec.709 luminosity
      uint8_t lv = (uint8_t)(0.2126f * rv + 0.7152f * gv + 0.0722f * bv);
      h.lum[lv]++;
    }
    return h;
  }

  // Peak across all enabled channels (for auto-scaling)
  std::uint32_t peak(bool showR, bool showG, bool showB, bool showLum) const {
    std::uint32_t m = 1;
    auto scan = [&](const std::array<std::uint32_t, 256> &ch) {
      for (auto v : ch)
        m = std::max(m, v);
    };
    if (showR)
      scan(r);
    if (showG)
      scan(g);
    if (showB)
      scan(b);
    if (showLum)
      scan(lum);
    return m;
  }
};

// ============================================================================
// HISTOGRAM DISPLAY MODE
// ============================================================================

enum class HistogramMode {
  RGB,        // R, G, B channels overlaid (Lightroom default)
  Luminosity, // Luminosity only (greyscale)
  Red,
  Green,
  Blue,
  RGBL // All four channels: R, G, B + Luminosity
};

// ============================================================================
// HISTOGRAM WIDGET
// ============================================================================

class HistogramWidget : public Widget {
public:
  // ── Data ──────────────────────────────────────────────────────────────────
  HistogramData histData;

  // ── Visual options ────────────────────────────────────────────────────────
  HistogramMode mode = HistogramMode::RGB;
  bool showClip = true;           // Flash/tint clipping indicators
  bool logScale = false;          // Logarithmic Y axis (like ACR)
  bool showGrid = true;           // Subtle background grid lines
  bool showChannelToggles = true; // Small R/G/B toggle buttons

  // Colors (match Lightroom's dark theme by default)
  Color bgColor = Color::fromRGB(28, 28, 28);
  Color gridColor = Color::fromRGB(50, 50, 50);
  Color borderColor = Color::fromRGB(55, 55, 55);
  Color rColor = Color::fromRGBA(220, 60, 60, 160);
  Color gColor = Color::fromRGBA(60, 210, 60, 160);
  Color bColor = Color::fromRGBA(60, 100, 240, 160);
  Color lumColor = Color::fromRGBA(190, 190, 190, 130);
  Color clipHighColor = Color::fromRGB(255, 60, 60);    // right-clip tint
  Color clipShadowColor = Color::fromRGB(60, 120, 255); // left-clip tint

  // Per-channel visibility toggles
  bool showR = true;
  bool showG = true;
  bool showB = true;
  bool showLum = true;

  // Callback fired when the user clicks a zone (0.0–1.0 normalised position)
  std::function<void(float)> onZoneClicked;

  // ── Constructor ───────────────────────────────────────────────────────────
  HistogramWidget() {
    width = 256;
    height = 120;
    autoWidth = false;
    autoHeight = false;
    hasBorder = false;
    isFocusable = true;

    paddingLeft = paddingRight = paddingTop = paddingBottom = 0;
  }

  // ── Fluent API ────────────────────────────────────────────────────────────

  std::shared_ptr<HistogramWidget> setData(const HistogramData &data) {
    histData = data;
    markNeedsPaint();
    return std::static_pointer_cast<HistogramWidget>(shared_from_this());
  }

  std::shared_ptr<HistogramWidget> setData(State<HistogramData> &state) {
    histData = state.get();
    state.bindProperty(
        shared_from_this(),
        [](Widget *w, const HistogramData &d) {
          static_cast<HistogramWidget *>(w)->histData = d;
        },
        false);
    markNeedsPaint();
    return std::static_pointer_cast<HistogramWidget>(shared_from_this());
  }

  std::shared_ptr<HistogramWidget> setMode(HistogramMode m) {
    mode = m;
    markNeedsPaint();
    return std::static_pointer_cast<HistogramWidget>(shared_from_this());
  }

  std::shared_ptr<HistogramWidget> setLogScale(bool v) {
    logScale = v;
    markNeedsPaint();
    return std::static_pointer_cast<HistogramWidget>(shared_from_this());
  }

  std::shared_ptr<HistogramWidget> setShowClip(bool v) {
    showClip = v;
    markNeedsPaint();
    return std::static_pointer_cast<HistogramWidget>(shared_from_this());
  }

  std::shared_ptr<HistogramWidget> setShowGrid(bool v) {
    showGrid = v;
    markNeedsPaint();
    return std::static_pointer_cast<HistogramWidget>(shared_from_this());
  }

  std::shared_ptr<HistogramWidget> setShowChannelToggles(bool v) {
    showChannelToggles = v;
    markNeedsPaint();
    return std::static_pointer_cast<HistogramWidget>(shared_from_this());
  }

  std::shared_ptr<HistogramWidget> setBgColor(Color c) {
    bgColor = c;
    markNeedsPaint();
    return std::static_pointer_cast<HistogramWidget>(shared_from_this());
  }

  std::shared_ptr<HistogramWidget> setSize(int w, int h) {
    width = w;
    height = h;
    autoWidth = autoHeight = false;
    markNeedsLayout();
    return std::static_pointer_cast<HistogramWidget>(shared_from_this());
  }

  std::shared_ptr<HistogramWidget>
  setOnZoneClicked(std::function<void(float)> cb) {
    onZoneClicked = cb;
    return std::static_pointer_cast<HistogramWidget>(shared_from_this());
  }

  // Hover position (normalised 0-1, -1 = none) — read-only for external use
  float hoverPos = -1.0f;

  // ── Widget overrides ──────────────────────────────────────────────────────

  void computeLayout(GraphicsContext & /*ctx*/,
                     const BoxConstraints &constraints,
                     FontCache & /*fontCache*/) override {
    width = constraints.clampWidth(autoWidth ? constraints.maxWidth : width);
    height =
        constraints.clampHeight(autoHeight ? constraints.maxHeight : height);
    applyConstraints();
    needsLayout = false;
  }

  void render(GraphicsContext &ctx, FontCache &fontCache) override {
    bool drawR = false, drawG = false, drawB = false, drawL = false;
    switch (mode) {
    case HistogramMode::RGB:
      drawR = showR;
      drawG = showG;
      drawB = showB;
      break;
    case HistogramMode::Luminosity:
      drawL = showLum;
      break;
    case HistogramMode::Red:
      drawR = true;
      break;
    case HistogramMode::Green:
      drawG = true;
      break;
    case HistogramMode::Blue:
      drawB = true;
      break;
    case HistogramMode::RGBL:
      drawR = showR;
      drawG = showG;
      drawB = showB;
      drawL = showLum;
      break;
    }

    const int toggleH = showChannelToggles ? 18 : 0;
    int px = x, py = y;
    int pw = width, ph = height - toggleH;

    Painter painter(ctx);

    // Background
    painter.fillRect(px, py, pw, ph, bgColor);

    // Grid
    if (showGrid) {
      for (int i = 1; i <= 3; ++i) {
        int gx = px + pw * i / 4;
        painter.drawVLine(gx, py, ph, gridColor, 1);
      }
      for (int i = 1; i <= 3; ++i) {
        int gy = py + ph * i / 4;
        painter.drawHLine(px, gy, pw, gridColor, 1);
      }
    }

    // Clipping indicator strips
    if (showClip) {
      const int clipW = 3;
      painter.fillRect(px, py, clipW, ph, clipShadowColor);
      painter.fillRect(px + pw - clipW, py, clipW, ph, clipHighColor);
    }

    // Channel bars
    std::uint32_t peak = histData.peak(drawR, drawG, drawB, drawL);

    auto buildBarHeights = [&](const std::array<uint32_t, 256> &bins) {
      std::vector<int> heights(pw);
      for (int col = 0; col < pw; ++col) {
        float binF = (float)col / (pw - 1) * 255.0f;
        int bin0 = (int)binF;
        int bin1 = std::min(255, bin0 + 1);
        float t = binF - bin0;
        float raw = (float)bins[bin0] * (1.f - t) + (float)bins[bin1] * t;

        float norm;
        if (logScale && peak > 0)
          norm = (raw > 0) ? (float)(std::log(1.0 + raw) / std::log(1.0 + peak))
                           : 0.f;
        else
          norm = (peak > 0) ? raw / (float)peak : 0.f;

        heights[col] = (int)(norm * ph);
      }
      return heights;
    };

    if (drawL)
      painter.fillColumnBars(px, py, pw, ph, buildBarHeights(histData.lum),
                             lumColor);
    if (drawB)
      painter.fillColumnBars(px, py, pw, ph, buildBarHeights(histData.b),
                             bColor);
    if (drawG)
      painter.fillColumnBars(px, py, pw, ph, buildBarHeights(histData.g),
                             gColor);
    if (drawR)
      painter.fillColumnBars(px, py, pw, ph, buildBarHeights(histData.r),
                             rColor);

    // Hover crosshair
    if (hoverPos >= 0.0f && hoverPos <= 1.0f) {
      int hx = px + (int)(hoverPos * (pw - 1));
      painter.drawVLine(hx, py, ph, Color::fromRGB(255, 255, 255), 1);

      // Tooltip label
      int binIdx = std::max(0, std::min(255, (int)(hoverPos * 255.f + 0.5f)));
      char buf[64];
      if (mode == HistogramMode::Luminosity)
        std::snprintf(buf, sizeof(buf), "L: %u", histData.lum[binIdx]);
      else
        std::snprintf(buf, sizeof(buf), "R:%u G:%u B:%u", histData.r[binIdx],
                      histData.g[binIdx], histData.b[binIdx]);

      NativeFont hFont = fontCache.getFont(9, FontWeight::Normal);

      // Measure to decide left or right placement
      int tw = 0, th = 0;
      painter.measureText(std::wstring(buf, buf + strlen(buf)), hFont, tw, th);

      int tx = hx + 4;
      if (tx + tw > px + pw)
        tx = hx - tw - 4;

      painter.drawTextA(std::string(buf), tx, py + 3, tw + 2, th + 2, hFont,
                        Color::fromRGB(220, 220, 220), DT_LEFT | DT_TOP | DT_SINGLELINE);
    }

    // Border
    painter.drawRectOutline(px, py, pw, ph, borderColor, 1);

    // Channel toggles
    if (showChannelToggles)
      renderToggles(ctx, fontCache, x, y + ph, width, toggleH, drawR, drawG,
                    drawB, drawL);

    needsPaint = false;
  }

  // ── Mouse interaction ─────────────────────────────────────────────────────

  bool handleMouseMove(int mx, int my) override {
    const int toggleH = showChannelToggles ? 18 : 0;
    int ph = height - toggleH;

    if (mx >= x && mx < x + width && my >= y && my < y + ph) {
      float newPos = (float)(mx - x) / std::max(1, width - 1);
      newPos = std::max(0.0f, std::min(1.0f, newPos));
      if (std::abs(newPos - hoverPos) > 0.001f) {
        hoverPos = newPos;
        markNeedsPaint();
        return true;
      }
    } else if (hoverPos >= 0.0f) {
      hoverPos = -1.0f;
      markNeedsPaint();
      return true;
    }
    return false;
  }

  bool handleMouseLeave() override {
    if (hoverPos >= 0.0f) {
      hoverPos = -1.0f;
      markNeedsPaint();
      return true;
    }
    return false;
  }

  bool handleMouseDown(int mx, int my) override {
    const int toggleH = showChannelToggles ? 18 : 0;
    int ph = height - toggleH;

    // Click in plot → zone callback
    if (mx >= x && mx < x + width && my >= y && my < y + ph) {
      if (onZoneClicked) {
        float pos = (float)(mx - x) / std::max(1, width - 1);
        onZoneClicked(std::max(0.0f, std::min(1.0f, pos)));
      }
      return true;
    }

    // Click in toggle strip
    if (showChannelToggles && my >= y + ph && my < y + height) {
      handleToggleClick(mx - x, width);
      return true;
    }

    return false;
  }

  // ── Size helpers ──────────────────────────────────────────────────────────

  std::shared_ptr<HistogramWidget> setWidth(int w) {
    width = w;
    autoWidth = false;
    markNeedsLayout();
    return std::static_pointer_cast<HistogramWidget>(shared_from_this());
  }

  std::shared_ptr<HistogramWidget> setHeight(int h) {
    height = h;
    autoHeight = false;
    markNeedsLayout();
    return std::static_pointer_cast<HistogramWidget>(shared_from_this());
  }

private:
  // ── Toggle-strip hit areas (normalised 0-1 each) ──────────────────────────
  // Layout: [  R  |  G  |  B  |  L  ]  equal width
  void handleToggleClick(int localX, int totalW) {
    int numChannels = 4;
    int slot = localX * numChannels / std::max(1, totalW);
    switch (slot) {
    case 0:
      showR = !showR;
      break;
    case 1:
      showG = !showG;
      break;
    case 2:
      showB = !showB;
      break;
    case 3:
      showLum = !showLum;
      break;
    }
    markNeedsPaint();
  }

void renderToggles(GraphicsContext &ctx, FontCache &fontCache,
                   int tx, int ty, int tw, int th,
                   bool, bool, bool, bool) {
    struct ChanInfo { const char *label; Color col; bool active; };
    ChanInfo channels[4] = {
        {"R", rColor,   showR},
        {"G", gColor,   showG},
        {"B", bColor,   showB},
        {"L", lumColor, showLum},
    };

    Painter painter(ctx);
    painter.fillRect(tx, ty, tw, th, Color::fromRGB(20, 20, 20));

    NativeFont hFont = fontCache.getFont(9, FontWeight::Normal);
    int slotW = tw / 4;

    for (int i = 0; i < 4; ++i) {
        int sx  = tx + i * slotW;
        int cy  = ty + th / 2;

        Color dotCol  = channels[i].active ? channels[i].col : Color::fromRGB(55, 55, 55);
        Color textCol = channels[i].active ? channels[i].col : Color::fromRGB(70, 70, 70);

        // Dot — small filled ellipse (no stroke)
        painter.drawEllipse(sx + 3, cy - 3, 6, 6, dotCol, dotCol, 0);

        // Label
        painter.drawTextA(channels[i].label,
                          sx + 11, ty, slotW - 13, th,
                          hFont, textCol,
                          DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }
}
};

// ============================================================================
// FACTORY
// ============================================================================

using HistogramWidgetPtr = std::shared_ptr<HistogramWidget>;

inline HistogramWidgetPtr Histogram() {
  return std::make_shared<HistogramWidget>();
}

inline HistogramWidgetPtr Histogram(int w, int h) {
  auto widget = std::make_shared<HistogramWidget>();
  widget->setSize(w, h);
  return widget;
}

// Convenience: build directly from a raw pixel buffer
inline HistogramWidgetPtr Histogram(const uint8_t *pixels, int pixelCount,
                                    int channels = 4, int w = 256,
                                    int h = 120) {
  auto widget = std::make_shared<HistogramWidget>();
  widget->setSize(w, h);
  widget->setData(HistogramData::fromPixels(pixels, pixelCount, channels));
  return widget;
}

/*
// ============================================================================
// USAGE EXAMPLES
// ============================================================================

// ── 1. Static histogram from raw image data
─────────────────────────────────── std::vector<uint8_t> imgPixels =
loadImageRGBA("photo.png"); // your loader int pixelCount = imgPixels.size() /
4;

Histogram(imgPixels.data(), pixelCount, 4, 300, 140)
    ->setMode(HistogramMode::RGB)
    ->setShowGrid(true)
    ->setShowClip(true);


// ── 2. Reactive: live histogram that updates when your image state changes
──── State<HistogramData> histState;

// Build the widget once in your build() method:
Histogram(300, 140)
    ->setData(histState)
    ->setMode(HistogramMode::RGB)
    ->setLogScale(false)
    ->setOnZoneClicked([](float pos) {
        // pos is 0.0 (shadows) → 1.0 (highlights)
        // e.g. jump a tone-curve point to this brightness zone
    });

// Anywhere in your app logic (e.g. after applying an edit):
HistogramData fresh = HistogramData::fromPixels(newPixels.data(), count);
histState.set(fresh);   // histogram repaints itself automatically


// ── 3. Single-channel (Lightroom "Red" channel view) ─────────────────────────
Histogram(300, 140)
    ->setData(myHistState)
    ->setMode(HistogramMode::Red)
    ->setLogScale(true);


// ── 4. Dark-room style: all four channels (R, G, B, Luminosity) ──────────────
Histogram(300, 140)
    ->setData(myHistState)
    ->setMode(HistogramMode::RGBL)
    ->setShowChannelToggles(true)   // click R/G/B/L buttons to show/hide
    ->setShowClip(true);


// ── 5. Embedding in a sidebar panel ──────────────────────────────────────────
Column(
    Text("Histogram")->setFontSize(11)->setTextColor(Color::fromRGB(180,180,180)),
    Histogram(panelWidth, 120)
        ->setData(currentHistState)
        ->setMode(HistogramMode::RGB)
        ->setShowChannelToggles(true),
    Divider(),
    ...
)
*/

#endif // FLUX_HISTOGRAM_HPP