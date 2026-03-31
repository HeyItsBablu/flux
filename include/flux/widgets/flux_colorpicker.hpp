#ifndef FLUX_COLORPICKER_HPP
#define FLUX_COLORPICKER_HPP

#include "flux_core.hpp"
#include "flux_input.hpp"
#include "flux_state.hpp"
#include <cmath>
#include <iomanip>
#include <sstream>

// ============================================================================
// COLOR UTILITIES
// ============================================================================

struct HSV {
  double h; // 0..360
  double s; // 0..1
  double v; // 0..1
  double a; // 0..1
};

inline Color HSVtoRGB(const HSV &hsv) {
  double r = 0, g = 0, b = 0;
  double h = hsv.h, s = hsv.s, v = hsv.v;

  if (s == 0.0) {
    r = g = b = v;
  } else {
    int i = (int)(h / 60.0) % 6;
    double f = h / 60.0 - floor(h / 60.0);
    double p = v * (1.0 - s);
    double q = v * (1.0 - f * s);
    double t = v * (1.0 - (1.0 - f) * s);

    switch (i) {
    case 0:
      r = v;
      g = t;
      b = p;
      break;
    case 1:
      r = q;
      g = v;
      b = p;
      break;
    case 2:
      r = p;
      g = v;
      b = t;
      break;
    case 3:
      r = p;
      g = q;
      b = v;
      break;
    case 4:
      r = t;
      g = p;
      b = v;
      break;
    case 5:
      r = v;
      g = p;
      b = q;
      break;
    }
  }

  return Color::fromRGB((uint8_t)(r * 255), (uint8_t)(g * 255),
                        (uint8_t)(b * 255));
}

inline HSV RGBtoHSV(Color color) {
  double r = color.r / 255.0;
  double g = color.g / 255.0;
  double b = color.b / 255.0;

  double cmax = max(r, max(g, b));
  double cmin = min(r, min(g, b));
  double diff = cmax - cmin;

  HSV hsv = {0, 0, cmax, 1.0};

  if (diff > 0.0) {
    if (cmax == r)
      hsv.h = fmod(60.0 * ((g - b) / diff) + 360.0, 360.0);
    else if (cmax == g)
      hsv.h = 60.0 * ((b - r) / diff) + 120.0;
    else
      hsv.h = 60.0 * ((r - g) / diff) + 240.0;

    hsv.s = (cmax > 0.0) ? diff / cmax : 0.0;
  }

  return hsv;
}

inline std::string ColorToHex(Color color) {
  std::ostringstream ss;
  ss << "#" << std::uppercase << std::hex << std::setfill('0') << std::setw(2)
     << (int)color.r << std::setw(2) << (int)color.g << std::setw(2)
     << (int)color.b;
  return ss.str();
}

inline bool HexToColor(const std::string &hex, Color &outColor) {
  std::string h = hex;
  if (!h.empty() && h[0] == '#')
    h = h.substr(1);
  if (h.size() != 6)
    return false;

  try {
    int r = std::stoi(h.substr(0, 2), nullptr, 16);
    int g = std::stoi(h.substr(2, 2), nullptr, 16);
    int b = std::stoi(h.substr(4, 2), nullptr, 16);
    outColor = Color::fromRGB((uint8_t)r, (uint8_t)g, (uint8_t)b);
    return true;
  } catch (...) {
    return false;
  }
}

// ============================================================================
// COLOR PICKER WIDGET
// ============================================================================

class ColorPickerWidget : public Widget {
public:
  // Current color state
  HSV hsv = {0.0, 1.0, 1.0, 1.0};

  // Layout constants
  int pickerSize = 200; // SV square
  int hueBarHeight = 16;
  int alphaBarHeight = 16;
  int previewSize = 28;
  int barSpacing = 8;
  int hexInputHeight = 28;

  // Interaction tracking
  bool draggingSV = false;
  bool draggingHue = false;
  bool draggingAlpha = false;
  bool showAlpha = true;

  std::function<void(Color)> onColorChanged;

  ColorPickerWidget() {
    autoWidth = false;
    autoHeight = false;
    paddingLeft = paddingRight = paddingTop = paddingBottom = 10;
    width = pickerSize + paddingLeft + paddingRight;
    height = computeTotalHeight();
  }

  // -------------------------------------------------------------------------
  // Layout
  // -------------------------------------------------------------------------
  void computeLayout(GraphicsContext & /*ctx*/,
                     const BoxConstraints &constraints,
                     FontCache & /*fontCache*/) override {
    if (!visible) {
      width = 0;
      height = 0;
      needsLayout = false;
      return;
    }
    width = constraints.clampWidth(width);
    height = constraints.clampHeight(computeTotalHeight());
    applyConstraints();
    needsLayout = false;
  }

  // -------------------------------------------------------------------------
  // Render
  // -------------------------------------------------------------------------
  void render(GraphicsContext &ctx, FontCache &fontCache) override {
    if (!visible)
      return;
    Painter painter(ctx);

    int cx = x + paddingLeft;
    int cy = y + paddingTop;
    int psz = pickerSize;

    // 1. SV Square
    renderSVSquare(ctx, cx, cy, psz);
    int thumbX = cx + (int)(hsv.s * psz);
    int thumbY = cy + (int)((1.0 - hsv.v) * psz);
    drawThumb(ctx, thumbX, thumbY, 6);
    cy += psz + barSpacing;

    // 2. Hue Bar
    renderHueBar(ctx, cx, cy, psz, hueBarHeight);
    int hueThumbX = cx + (int)(hsv.h / 360.0 * psz);
    drawBarThumb(ctx, hueThumbX, cy, hueBarHeight);
    cy += hueBarHeight + barSpacing;

    // 3. Alpha Bar
    if (showAlpha) {
      renderAlphaBar(ctx, cx, cy, psz, alphaBarHeight);
      int alphaThumbX = cx + (int)(hsv.a * psz);
      drawBarThumb(ctx, alphaThumbX, cy, alphaBarHeight);
      cy += alphaBarHeight + barSpacing;
    }

    // 4. Preview swatch + hex label
    Color currentColor = HSVtoRGB(hsv);
    painter.drawEllipse(cx, cy, previewSize, previewSize, currentColor,
                        Color::fromRGB(180, 180, 180), 1);

    std::string hexStr = ColorToHex(currentColor);
    NativeFont hFont = fontCache.getFont(13, FontWeight::Normal);
    painter.drawTextA(hexStr, cx + previewSize + 8, cy, psz - previewSize - 8,
                      previewSize, hFont, Color::fromRGB(30, 30, 30),
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    needsPaint = false;
  }

  // -------------------------------------------------------------------------
  // Mouse Handling
  // -------------------------------------------------------------------------
  bool handleMouseDown(int mx, int my) override {
    auto *ui = FluxUI::getCurrentInstance();
    int cx = x + paddingLeft;
    int cy = y + paddingTop;

    if (inRect(mx, my, cx, cy, pickerSize, pickerSize)) {
      draggingSV = true;
      if (ui)
        ui->captureMouseInput();
      updateSV(mx, my, cx, cy);
      return true;
    }
    cy += pickerSize + barSpacing;

    if (inRect(mx, my, cx, cy, pickerSize, hueBarHeight)) {
      draggingHue = true;
      if (ui)
        ui->captureMouseInput();
      updateHue(mx, cx);
      return true;
    }
    cy += hueBarHeight + barSpacing;

    if (showAlpha && inRect(mx, my, cx, cy, pickerSize, alphaBarHeight)) {
      draggingAlpha = true;
      if (ui)
        ui->captureMouseInput();
      updateAlpha(mx, cx);
      return true;
    }
    return false;
  }

  bool handleMouseUp(int /*mx*/, int /*my*/) override {
    if (draggingSV || draggingHue || draggingAlpha) {
      draggingSV = draggingHue = draggingAlpha = false;
      if (auto *ui = FluxUI::getCurrentInstance())
        ui->releaseMouseInput();
      return true;
    }
    return false;
  }

  bool handleMouseMove(int mx, int my) override {
    int cx = x + paddingLeft;
    int cy = y + paddingTop;

    if (draggingSV) {
      updateSV(mx, my, cx, cy);
      return true;
    }
    if (draggingHue) {
      updateHue(mx, cx);
      return true;
    }
    if (draggingAlpha) {
      updateAlpha(mx, cx);
      return true;
    }
    return false;
  }

  // -------------------------------------------------------------------------
  // Builder / Setters
  // -------------------------------------------------------------------------
  std::shared_ptr<ColorPickerWidget> setColor(Color color) {
    hsv = RGBtoHSV(color);
    hsv.a = 1.0;
    markNeedsPaint();
    return std::static_pointer_cast<ColorPickerWidget>(shared_from_this());
  }

  std::shared_ptr<ColorPickerWidget> setShowAlpha(bool show) {
    showAlpha = show;
    height = computeTotalHeight();
    markNeedsLayout();
    return std::static_pointer_cast<ColorPickerWidget>(shared_from_this());
  }

  std::shared_ptr<ColorPickerWidget>
  setOnColorChanged(std::function<void(Color)> cb) {
    onColorChanged = cb;
    return std::static_pointer_cast<ColorPickerWidget>(shared_from_this());
  }

  std::shared_ptr<ColorPickerWidget> bindValue(State<Color> &state) {
    Color init = state.get();
    hsv = RGBtoHSV(init);

    state.bindProperty(
        shared_from_this(),
        [](Widget *w, const Color &val) {
          auto *cp = static_cast<ColorPickerWidget *>(w);
          cp->hsv = RGBtoHSV(val);
          cp->hsv.a = 1.0;
        },
        false);

    boundState = &state;
    return std::static_pointer_cast<ColorPickerWidget>(shared_from_this());
  }

  Color getColor() const { return HSVtoRGB(hsv); }

private:
  State<Color> *boundState = nullptr;

  // -------------------------------------------------------------------------
  // Internal helpers
  // -------------------------------------------------------------------------
  int computeTotalHeight() const {
    int h = paddingTop + pickerSize + barSpacing + hueBarHeight + barSpacing;
    if (showAlpha)
      h += alphaBarHeight + barSpacing;
    h += previewSize + paddingBottom;
    return h;
  }

  bool inRect(int mx, int my, int rx, int ry, int rw, int rh) const {
    return mx >= rx && mx <= rx + rw && my >= ry && my <= ry + rh;
  }

  void updateSV(int mx, int my, int cx, int cy) {
    double s = (double)(mx - cx) / pickerSize;
    double v = 1.0 - (double)(my - cy) / pickerSize;
    hsv.s = max(0.0, min(1.0, s));
    hsv.v = max(0.0, min(1.0, v));
    notifyChanged();
    markNeedsPaint();
  }

  void updateHue(int mx, int cx) {
    double t = (double)(mx - cx) / pickerSize;
    hsv.h = max(0.0, min(360.0, t * 360.0));
    notifyChanged();
    markNeedsPaint();
  }

  void updateAlpha(int mx, int cx) {
    double t = (double)(mx - cx) / pickerSize;
    hsv.a = max(0.0, min(1.0, t));
    notifyChanged();
    markNeedsPaint();
  }

  void notifyChanged() {
    Color c = getColor();
    if (onColorChanged)
      onColorChanged(c);
    if (boundState)
      boundState->set(c);
  }

  // Renders the saturation/value gradient square using GDI blending
  void renderSVSquare(GraphicsContext &ctx, int cx, int cy, int size) {
#ifdef _WIN32
    for (int px = 0; px < size; px++) {
      double s = (double)px / size;
      Color topColor = HSVtoRGB({hsv.h, s, 1.0, 1.0});
      for (int py = 0; py < size; py++) {
        double t = (double)py / size;
        SetPixel(ctx.hdc, cx + px, cy + py,
                 toColorRef(Color::fromRGB((uint8_t)(topColor.r * (1.0 - t)),
                                           (uint8_t)(topColor.g * (1.0 - t)),
                                           (uint8_t)(topColor.b * (1.0 - t)))));
      }
    }
#endif
    Painter(ctx).drawRectOutline(cx, cy, size, size,
                                 Color::fromRGB(180, 180, 180), 1);
  }
  void renderHueBar(GraphicsContext &ctx, int cx, int cy, int barW, int barH) {
    Painter painter(ctx);
    for (int px = 0; px < barW; px++) {
      double hue = (double)px / barW * 360.0;
      Color c = HSVtoRGB({hue, 1.0, 1.0, 1.0});
      painter.drawVLine(cx + px, cy, barH, c, 1);
    }
    painter.drawRectOutline(cx, cy, barW, barH, Color::fromRGB(180, 180, 180), 1);
  }

  void renderAlphaBar(GraphicsContext &ctx, int cx, int cy, int barW,
                      int barH) {
    Painter painter(ctx);

    // Checkerboard background
    int tileSize = 4;
    for (int px = 0; px < barW; px += tileSize) {
      for (int py = 0; py < barH; py += tileSize) {
        bool light = ((px / tileSize + py / tileSize) % 2 == 0);
        Color bg = light ? Color::fromRGB(200, 200, 200)
                         : Color::fromRGB(150, 150, 150);
        int tw = min(px + tileSize, barW) - px;
        int th = min(py + tileSize, barH) - py;
        painter.fillRect(cx + px, cy + py, tw, th, bg);
      }
    }

    // Alpha gradient overlay
    Color baseColor = HSVtoRGB(hsv);
    for (int px = 0; px < barW; px++) {
      double a = (double)px / barW;
      painter.drawVLine(
          cx + px, cy, barH,
          Color::fromRGB((uint8_t)(baseColor.r * a + 200 * (1.0 - a)),
                         (uint8_t)(baseColor.g * a + 200 * (1.0 - a)),
                         (uint8_t)(baseColor.b * a + 200 * (1.0 - a))),
          1);
    }
    painter.drawRectOutline(cx, cy, barW, barH, Color::fromRGB(180, 180, 180),
                            1);
  }

  // Circular thumb for SV square
  void drawThumb(GraphicsContext &ctx, int tx, int ty, int radius) {
    Painter painter(ctx);
    // Dark outer ring
painter.drawEllipse(tx - radius - 1, ty - radius - 1, (radius+1)*2, (radius+1)*2,
                    Color::fromRGB(0,0,0), Color::fromRGB(80,80,80), 1);
    // White ring on top
painter.drawEllipse(tx - radius, ty - radius, radius*2, radius*2,
                    Color::fromRGB(0,0,0), Color::fromRGB(255,255,255), 2);
  }

  void drawBarThumb(GraphicsContext &ctx, int tx, int barY, int barH) {
    Painter painter(ctx);
    int py = barY + barH / 2;
    painter.drawEllipse(tx - 5, py - 5, 10, 10, Color::fromRGB(0, 0, 0),
                        Color::fromRGB(255, 255, 255), 2);
    painter.drawEllipse(tx - 4, py - 4, 8, 8, Color::fromRGB(0, 0, 0), Color::fromRGB(80, 80, 80), 1);
  }
};

// ============================================================================
// FACTORY FUNCTION
// ============================================================================

using ColorPickerWidgetPtr = std::shared_ptr<ColorPickerWidget>;

inline ColorPickerWidgetPtr ColorPicker(Color initialColor = Color::fromRGB(255, 0, 0)) {
  auto w = std::make_shared<ColorPickerWidget>();
  w->setColor(initialColor);
  return w;
}

#endif // FLUX_COLORPICKER_HPP