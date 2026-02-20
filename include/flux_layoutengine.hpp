#ifndef FLUX_LAYOUTENGINE_HPP
#define FLUX_LAYOUTENGINE_HPP

#include "flux_widget.hpp"

class LayoutEngine {
public:
  static void computeLayout(HDC hdc, Widget *w, int availableWidth,
                            int availableHeight, FontCache &fontCache) {
    if (!w)
      return;
    w->computeLayout(hdc, availableWidth, availableHeight, fontCache);
  }

  static void positionWidget(Widget *w, int x, int y) {
    if (!w)
      return;

    w->x = x + w->marginLeft;
    w->y = y + w->marginTop;

    int contentX = w->x + w->paddingLeft;
    int contentY = w->y + w->paddingTop;
    int contentWidth = w->width - w->paddingLeft - w->paddingRight;
    int contentHeight = w->height - w->paddingTop - w->paddingBottom;

    w->positionChildren(contentX, contentY, contentWidth, contentHeight);
  }
};

#endif // FLUX_LAYOUT_HPP