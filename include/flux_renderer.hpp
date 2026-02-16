#ifndef FLUX_RENDERER_HPP
#define FLUX_RENDERER_HPP

#include "flux_widget.hpp"

class Renderer {
public:
  static void renderWidget(HDC hdc, Widget *w, FontCache &fontCache) {
    if (!w)
      return;

    // First pass: Render all widgets normally
    renderWidgetRecursive(hdc, w, fontCache);

    // Second pass: Render overlays (for widgets that need to draw on top)
    renderOverlays(hdc, w, fontCache);
  }

private:
  // First pass: Normal widget rendering
  static void renderWidgetRecursive(HDC hdc, Widget *w, FontCache &fontCache) {
    if (!w)
      return;

    w->render(hdc, fontCache);
  }

  // Second pass: Render overlays
  static void renderOverlays(HDC hdc, Widget *w, FontCache &fontCache) {
    if (!w)
      return;

    // Call the widget's renderOverlay method (no-op by default)
    w->renderOverlay(hdc, fontCache);

    // Recursively render overlays for children
    for (auto &child : w->children) {
      renderOverlays(hdc, child.get(), fontCache);
    }
  }
};

#endif // FLUX_RENDERER_HPP