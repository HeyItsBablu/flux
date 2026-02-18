#ifndef FLUX_PROGRESS_BAR_HPP
#define FLUX_PROGRESS_BAR_HPP

#include "flux_core.hpp"

class ProgressBarWidget : public Widget {
public:
  double value = 0.0; // 0.0 - 1.0
  int trackBorderRadius = 4;

  COLORREF trackColor = RGB(220, 220, 220);
  COLORREF trackBorderColor = RGB(0, 0, 0);
  bool hasTrackBorder = false;
  int trackBorderWidth = 1;

  std::vector<COLORREF> progressColors = {RGB(33, 150, 243)};

  ProgressBarWidget() {
    height = 12;
    autoHeight = false;
  }

  // ----------------------------------------------------------------
  // Builder API
  // ----------------------------------------------------------------

  std::shared_ptr<ProgressBarWidget> setValue(State<double> &state) {
    value = max(0.0, min(1.0, state.get()));

    state.bindProperty(
        shared_from_this(),
        [](Widget *w, const double &val) {
          auto *bar = static_cast<ProgressBarWidget *>(w);
          bar->value = max(0.0, min(1.0, val));
          bar->markNeedsPaint();
        },
        false);

    return std::static_pointer_cast<ProgressBarWidget>(shared_from_this());
  }

  std::shared_ptr<ProgressBarWidget> setBackgroundColor(COLORREF color) {
    trackColor = color;
    markNeedsPaint();
    return std::static_pointer_cast<ProgressBarWidget>(shared_from_this());
  }

  std::shared_ptr<ProgressBarWidget>
  setProgressColors(std::vector<COLORREF> colors) {
    if (!colors.empty())
      progressColors = colors;
    markNeedsPaint();
    return std::static_pointer_cast<ProgressBarWidget>(shared_from_this());
  }

  std::shared_ptr<ProgressBarWidget> setBorderColor(COLORREF color) {
    trackBorderColor = color;
    hasTrackBorder = true;
    markNeedsPaint();
    return std::static_pointer_cast<ProgressBarWidget>(shared_from_this());
  }

  std::shared_ptr<ProgressBarWidget> setBorderWidth(int w) {
    trackBorderWidth = w;
    markNeedsPaint();
    return std::static_pointer_cast<ProgressBarWidget>(shared_from_this());
  }

  std::shared_ptr<ProgressBarWidget> setBorderRadius(int r) {
    trackBorderRadius = r;
    markNeedsPaint();
    return std::static_pointer_cast<ProgressBarWidget>(shared_from_this());
  }

  std::shared_ptr<ProgressBarWidget> setValue(double v) {
    value = max(0.0, min(1.0, v));
    markNeedsPaint();
    return std::static_pointer_cast<ProgressBarWidget>(shared_from_this());
  }

  std::shared_ptr<ProgressBarWidget> setHeight(int h) {
    height = h;
    autoHeight = false;
    markNeedsPaint();
    return std::static_pointer_cast<ProgressBarWidget>(shared_from_this());
  }

  // ----------------------------------------------------------------
  // Layout
  // ----------------------------------------------------------------

  void computeLayout(HDC hdc, int availableWidth, int availableHeight,
                     FontCache &fontCache) override {
    if (autoWidth)
      width = availableWidth;
    applyConstraints();
    needsLayout = false;
  }

  // ----------------------------------------------------------------
  // Render
  // ----------------------------------------------------------------

  void render(HDC hdc, FontCache &fontCache) override {
    int rx = trackBorderRadius * 2;

    // -- Track background --
    HPEN trackPen =
        hasTrackBorder ? CreatePen(PS_SOLID, trackBorderWidth, trackBorderColor)
                       : CreatePen(PS_NULL, 0, 0);
    HBRUSH trackBrush = CreateSolidBrush(trackColor);

    HPEN oldPen = (HPEN)SelectObject(hdc, trackPen);
    HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, trackBrush);

    RoundRect(hdc, x, y, x + width, y + height, rx, rx);

    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(trackBrush);
    DeleteObject(trackPen);

    // -- Progress fill --
    int fillWidth = (int)(value * width);
    if (fillWidth > 0) {
      // Clip to track bounds so rounded fill doesn't bleed out
      HRGN clipRgn = CreateRoundRectRgn(x, y, x + width, y + height, rx, rx);
      SelectClipRgn(hdc, clipRgn);

      if (progressColors.size() == 1) {
        // Solid fill
        HBRUSH fillBrush = CreateSolidBrush(progressColors[0]);
        RECT fillRect = {x, y, x + fillWidth, y + height};
        FillRect(hdc, &fillRect, fillBrush);
        DeleteObject(fillBrush);
      } else {
        // Horizontal gradient across the fill area using GDI bands
        int bands = fillWidth;
        for (int i = 0; i < bands; i++) {
          double t = (double)i / max(1, bands - 1);

          // Map t to the color array
          double scaled = t * (progressColors.size() - 1);
          int idx = (int)scaled;
          double frac = scaled - idx;

          if (idx >= (int)progressColors.size() - 1)
            idx = (int)progressColors.size() - 2;

          COLORREF c0 = progressColors[idx];
          COLORREF c1 = progressColors[idx + 1];

          int r = (int)(GetRValue(c0) + frac * (GetRValue(c1) - GetRValue(c0)));
          int g = (int)(GetGValue(c0) + frac * (GetGValue(c1) - GetGValue(c0)));
          int b = (int)(GetBValue(c0) + frac * (GetBValue(c1) - GetBValue(c0)));

          HBRUSH band = CreateSolidBrush(RGB(r, g, b));
          RECT bandRect = {x + i, y, x + i + 1, y + height};
          FillRect(hdc, &bandRect, band);
          DeleteObject(band);
        }
      }

      // Remove clip
      SelectClipRgn(hdc, nullptr);
      DeleteObject(clipRgn);

      // -- Progress border (same radius as track) --
      if (hasTrackBorder) {
        HRGN fillRgn =
            CreateRoundRectRgn(x, y, x + fillWidth, y + height, rx, rx);
        HPEN progPen = CreatePen(PS_SOLID, trackBorderWidth, trackBorderColor);
        HPEN op = (HPEN)SelectObject(hdc, progPen);
        HBRUSH ob = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));

        RoundRect(hdc, x, y, x + fillWidth, y + height, rx, rx);

        SelectObject(hdc, ob);
        SelectObject(hdc, op);
        DeleteObject(progPen);
        DeleteObject(fillRgn);
      }
    }

    needsPaint = false;
  }
};

using ProgressBarWidgetPtr = std::shared_ptr<ProgressBarWidget>;

inline ProgressBarWidgetPtr ProgressBar(double value = 0.0) {
  auto w = std::make_shared<ProgressBarWidget>();
  w->setValue(value);
  return w;
}

#endif // FLUX_PROGRESS_BAR_HPP