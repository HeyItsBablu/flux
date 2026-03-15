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

inline COLORREF HSVtoRGB(const HSV &hsv) {
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

  return RGB((int)(r * 255), (int)(g * 255), (int)(b * 255));
}

inline HSV RGBtoHSV(COLORREF color) {
  double r = GetRValue(color) / 255.0;
  double g = GetGValue(color) / 255.0;
  double b = GetBValue(color) / 255.0;

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

inline std::string ColorToHex(COLORREF color) {
  std::ostringstream ss;
  ss << "#" << std::uppercase << std::hex << std::setfill('0') << std::setw(2)
     << (int)GetRValue(color) << std::setw(2) << (int)GetGValue(color)
     << std::setw(2) << (int)GetBValue(color);
  return ss.str();
}

inline bool HexToColor(const std::string &hex, COLORREF &outColor) {
  std::string h = hex;
  if (!h.empty() && h[0] == '#')
    h = h.substr(1);
  if (h.size() != 6)
    return false;

  try {
    int r = std::stoi(h.substr(0, 2), nullptr, 16);
    int g = std::stoi(h.substr(2, 2), nullptr, 16);
    int b = std::stoi(h.substr(4, 2), nullptr, 16);
    outColor = RGB(r, g, b);
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

  std::function<void(COLORREF)> onColorChanged;

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
  void computeLayout(HDC hdc, const BoxConstraints &constraints,
                     FontCache &fontCache) override {
                       if (!visible) { width = 0; height = 0; needsLayout = false; return; }
    width = constraints.clampWidth(width);
    height = constraints.clampHeight(computeTotalHeight());
    applyConstraints();
    needsLayout = false;
  }

  // -------------------------------------------------------------------------
  // Render
  // -------------------------------------------------------------------------
  void render(HDC hdc, FontCache &fontCache) override {
    if (!visible) return;
    int cx = x + paddingLeft;
    int cy = y + paddingTop;
    int psz = pickerSize;

    // -- 1. SV Square -------------------------------------------------------
    renderSVSquare(hdc, cx, cy, psz);

    // SV thumb
    int thumbX = cx + (int)(hsv.s * psz);
    int thumbY = cy + (int)((1.0 - hsv.v) * psz);
    drawThumb(hdc, thumbX, thumbY, 6);

    cy += psz + barSpacing;

    // -- 2. Hue Bar ---------------------------------------------------------
    renderHueBar(hdc, cx, cy, psz, hueBarHeight);

    int hueThumbX = cx + (int)(hsv.h / 360.0 * psz);
    drawBarThumb(hdc, hueThumbX, cy, hueBarHeight);

    cy += hueBarHeight + barSpacing;

    // -- 3. Alpha Bar (optional) -------------------------------------------
    if (showAlpha) {
      renderAlphaBar(hdc, cx, cy, psz, alphaBarHeight);

      int alphaThumbX = cx + (int)(hsv.a * psz);
      drawBarThumb(hdc, alphaThumbX, cy, alphaBarHeight);

      cy += alphaBarHeight + barSpacing;
    }

    // -- 4. Preview swatch + hex input -------------------------------------
    COLORREF currentColor = HSVtoRGB(hsv);

    // Preview circle/swatch
    HBRUSH previewBrush = CreateSolidBrush(currentColor);
    HPEN previewPen = CreatePen(PS_SOLID, 1, RGB(180, 180, 180));
    HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, previewBrush);
    HPEN oldPen = (HPEN)SelectObject(hdc, previewPen);
    Ellipse(hdc, cx, cy, cx + previewSize, cy + previewSize);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(previewBrush);
    DeleteObject(previewPen);

    // Hex text
    std::string hexStr = ColorToHex(currentColor);
    RECT hexRect = {cx + previewSize + 8, cy, cx + psz, cy + previewSize};
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(30, 30, 30));

    HFONT hFont = fontCache.getFont(13, FontWeight::Normal);
    HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);
    DrawText(hdc, hexStr.c_str(), -1, &hexRect,
             DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hdc, hOldFont);

    needsPaint = false;
  }

  // -------------------------------------------------------------------------
  // Mouse Handling
  // -------------------------------------------------------------------------
  bool handleMouseDown(int mx, int my) override {
    int cx = x + paddingLeft;
    int cy = y + paddingTop;

    // SV Square
    if (inRect(mx, my, cx, cy, pickerSize, pickerSize)) {
      draggingSV = true;
      HWND hwnd = FluxUI::getCurrentInstance()->getWindow();
      SetCapture(hwnd);
      updateSV(mx, my, cx, cy);
      return true;
    }
    cy += pickerSize + barSpacing;

    // Hue Bar
    if (inRect(mx, my, cx, cy, pickerSize, hueBarHeight)) {
      draggingHue = true;
      HWND hwnd = FluxUI::getCurrentInstance()->getWindow();
      SetCapture(hwnd);
      updateHue(mx, cx);
      return true;
    }
    cy += hueBarHeight + barSpacing;

    // Alpha Bar
    if (showAlpha && inRect(mx, my, cx, cy, pickerSize, alphaBarHeight)) {
      draggingAlpha = true;
      HWND hwnd = FluxUI::getCurrentInstance()->getWindow();
      SetCapture(hwnd);
      updateAlpha(mx, cx);
      return true;
    }

    return false;
  }

  bool handleMouseUp(int mx, int my) override {
    if (draggingSV || draggingHue || draggingAlpha) {
      draggingSV = draggingHue = draggingAlpha = false;
      ReleaseCapture();
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
  std::shared_ptr<ColorPickerWidget> setColor(COLORREF color) {
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
  setOnColorChanged(std::function<void(COLORREF)> cb) {
    onColorChanged = cb;
    return std::static_pointer_cast<ColorPickerWidget>(shared_from_this());
  }

  std::shared_ptr<ColorPickerWidget> bindValue(State<COLORREF> &state) {
    COLORREF init = state.get();
    hsv = RGBtoHSV(init);

    state.bindProperty(
        shared_from_this(),
        [](Widget *w, const COLORREF &val) {
          auto *cp = static_cast<ColorPickerWidget *>(w);
          cp->hsv = RGBtoHSV(val);
          cp->hsv.a = 1.0;
        },
        false);

    boundState = &state;
    return std::static_pointer_cast<ColorPickerWidget>(shared_from_this());
  }

  COLORREF getColor() const { return HSVtoRGB(hsv); }

private:
  State<COLORREF> *boundState = nullptr;

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
    COLORREF c = getColor();
    if (onColorChanged)
      onColorChanged(c);
    if (boundState)
      boundState->set(c);
  }

  // Renders the saturation/value gradient square using GDI blending
  void renderSVSquare(HDC hdc, int cx, int cy, int size) {
    // Draw column by column for a smooth gradient
    for (int px = 0; px < size; px++) {
      double s = (double)px / size;

      // For each column, draw a vertical gradient from white→pure hue (top)
      // to black (bottom) — we approximate with two lines per column
      COLORREF topColor = HSVtoRGB({hsv.h, s, 1.0, 1.0});
      COLORREF bottomColor = RGB(0, 0, 0);

      for (int py = 0; py < size; py++) {
        double t = (double)py / size; // 0=top(bright), 1=bottom(dark)

        int r = (int)(GetRValue(topColor) * (1.0 - t));
        int g = (int)(GetGValue(topColor) * (1.0 - t));
        int b = (int)(GetBValue(topColor) * (1.0 - t));

        SetPixel(hdc, cx + px, cy + py, RGB(r, g, b));
      }
    }

    // Border
    HPEN borderPen = CreatePen(PS_SOLID, 1, RGB(180, 180, 180));
    HPEN oldPen = (HPEN)SelectObject(hdc, borderPen);
    HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, cx, cy, cx + size, cy + size);
    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBrush);
    DeleteObject(borderPen);
  }

  void renderHueBar(HDC hdc, int cx, int cy, int width, int barH) {
    for (int px = 0; px < width; px++) {
      double hue = (double)px / width * 360.0;
      COLORREF c = HSVtoRGB({hue, 1.0, 1.0, 1.0});
      HPEN pen = CreatePen(PS_SOLID, 1, c);
      HPEN old = (HPEN)SelectObject(hdc, pen);
      MoveToEx(hdc, cx + px, cy, nullptr);
      LineTo(hdc, cx + px, cy + barH);
      SelectObject(hdc, old);
      DeleteObject(pen);
    }
    // Border
    HPEN borderPen = CreatePen(PS_SOLID, 1, RGB(180, 180, 180));
    HPEN oldPen = (HPEN)SelectObject(hdc, borderPen);
    HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, cx, cy, cx + width, cy + barH);
    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBrush);
    DeleteObject(borderPen);
  }

  void renderAlphaBar(HDC hdc, int cx, int cy, int barW, int barH) {
    // Checkerboard background
    int tileSize = 4;
    for (int px = 0; px < barW; px += tileSize) {
      for (int py = 0; py < barH; py += tileSize) {
        bool light = ((px / tileSize + py / tileSize) % 2 == 0);
        COLORREF bg = light ? RGB(200, 200, 200) : RGB(150, 150, 150);
        RECT tile = {cx + px, cy + py, cx + min(px + tileSize, barW),
                     cy + min(py + tileSize, barH)};
        HBRUSH br = CreateSolidBrush(bg);
        FillRect(hdc, &tile, br);
        DeleteObject(br);
      }
    }

    // Alpha gradient overlay
    COLORREF baseColor = HSVtoRGB(hsv);
    for (int px = 0; px < barW; px++) {
      double a = (double)px / barW;
      int r = (int)(GetRValue(baseColor) * a + 200 * (1.0 - a));
      int g = (int)(GetGValue(baseColor) * a + 200 * (1.0 - a));
      int b = (int)(GetBValue(baseColor) * a + 200 * (1.0 - a));
      HPEN pen = CreatePen(PS_SOLID, 1, RGB(r, g, b));
      HPEN old = (HPEN)SelectObject(hdc, pen);
      MoveToEx(hdc, cx + px, cy, nullptr);
      LineTo(hdc, cx + px, cy + barH);
      SelectObject(hdc, old);
      DeleteObject(pen);
    }

    // Border
    HPEN borderPen = CreatePen(PS_SOLID, 1, RGB(180, 180, 180));
    HPEN oldPen = (HPEN)SelectObject(hdc, borderPen);
    HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, cx, cy, cx + barW, cy + barH);
    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBrush);
    DeleteObject(borderPen);
  }

  // Circular thumb for SV square
  void drawThumb(HDC hdc, int tx, int ty, int radius) {
    HBRUSH nullBrush = (HBRUSH)GetStockObject(NULL_BRUSH);
    HPEN whitePen = CreatePen(PS_SOLID, 2, RGB(255, 255, 255));
    HPEN darkPen = CreatePen(PS_SOLID, 1, RGB(80, 80, 80));

    HPEN oldPen = (HPEN)SelectObject(hdc, whitePen);
    HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, nullBrush);

    Ellipse(hdc, tx - radius, ty - radius, tx + radius, ty + radius);

    SelectObject(hdc, darkPen);
    Ellipse(hdc, tx - radius - 1, ty - radius - 1, tx + radius + 1,
            ty + radius + 1);

    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBrush);
    DeleteObject(whitePen);
    DeleteObject(darkPen);
  }

  // Vertical bar thumb (triangle pointer)
  void drawBarThumb(HDC hdc, int tx, int barY, int barH) {
    int py = barY + barH / 2;

    HPEN whitePen = CreatePen(PS_SOLID, 2, RGB(255, 255, 255));
    HPEN darkPen = CreatePen(PS_SOLID, 1, RGB(80, 80, 80));
    HBRUSH nullBr = (HBRUSH)GetStockObject(NULL_BRUSH);

    // White outline
    SelectObject(hdc, whitePen);
    SelectObject(hdc, nullBr);
    Ellipse(hdc, tx - 5, py - 5, tx + 5, py + 5);

    // Dark inner ring
    SelectObject(hdc, darkPen);
    Ellipse(hdc, tx - 4, py - 4, tx + 4, py + 4);

    DeleteObject(whitePen);
    DeleteObject(darkPen);
  }
};

// ============================================================================
// FACTORY FUNCTION
// ============================================================================

using ColorPickerWidgetPtr = std::shared_ptr<ColorPickerWidget>;

inline ColorPickerWidgetPtr ColorPicker(COLORREF initialColor = RGB(255, 0,
                                                                    0)) {
  auto w = std::make_shared<ColorPickerWidget>();
  w->setColor(initialColor);
  return w;
}

#endif // FLUX_COLORPICKER_HPP