#ifndef FLUX_LAYOUTENGINE_HPP
#define FLUX_LAYOUTENGINE_HPP

#include "flux_platform.hpp"
#include "flux_widget.hpp"

class LayoutEngine {
public:
    static void computeLayout(GraphicsContext &ctx, Widget *w, int availableWidth,
                              int availableHeight, FontCache &fontCache) {
        if (!w)
            return;
        // Stamp the viewport width onto ctx before laying out, so any
        // widget in the tree (BoxWidget::resolveProps(), for its
        // responsive() breakpoints) can read it directly from the ctx
        // it's already been handed, instead of reaching for a thread_local
        // FluxUI singleton that may not be valid on the calling thread.
        ctx.fluxViewportWidth = availableWidth;
        w->computeLayout(
            ctx, BoxConstraints::loose(availableWidth, availableHeight), fontCache);
    }

    static void positionWidget(Widget *w, int x, int y) {
        if (!w)
            return;

        w->x = x + w->marginLeft;
        w->y = y + w->marginTop;

        int contentX      = w->x + w->paddingLeft;
        int contentY      = w->y + w->paddingTop;
        int contentWidth  = w->width  - w->paddingLeft - w->paddingRight;
        int contentHeight = w->height - w->paddingTop  - w->paddingBottom;

        w->positionChildren(contentX, contentY, contentWidth, contentHeight);
    }
};

#endif // FLUX_LAYOUTENGINE_HPP