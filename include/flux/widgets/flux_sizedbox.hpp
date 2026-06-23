#ifndef FLUX_SIZEDBOX_HPP
#define FLUX_SIZEDBOX_HPP

#include "flux/flux_core.hpp"





class SizedBoxWidget : public Widget
{
public:
    void computeLayout(GraphicsContext &ctx, const BoxConstraints &constraints,
                       FontCache &fontCache) override
    {

        BoxConstraints self = selfConstraints(constraints);

        // Clamp our fixed dimensions to incoming constraints
        int finalW = autoWidth ? self.maxWidth : self.clampWidth(width);
        int finalH = autoHeight ? self.maxHeight : self.clampHeight(height);

        if (!children.empty())
        {
            // Child gets tight constraints on fixed axes, loose on auto axes
            int minW = autoWidth ? 0 : finalW - paddingLeft - paddingRight;
            int maxW = finalW - paddingLeft - paddingRight;
            int minH = autoHeight ? 0 : finalH - paddingTop - paddingBottom;
            int maxH = finalH - paddingTop - paddingBottom;

            BoxConstraints childC(std::max(0, minW), std::max(0, maxW),
                                  std::max(0, minH), std::max(0, maxH));
            children[0]->computeLayout(ctx, childC, fontCache);

            // Auto axes shrink to child
            if (autoWidth)
                finalW = children[0]->width + paddingLeft + paddingRight;
            if (autoHeight)
                finalH = children[0]->height + paddingTop + paddingBottom;
        }

        width = finalW;
        height = finalH;
        needsLayout = false;
    }

    void positionChildren(int contentX, int contentY, int /*cW*/,
                          int /*cH*/) override
    {
        if (!children.empty())
        {
            auto &child = children[0];
            child->x = contentX;
            child->y = contentY;
            child->positionChildren(
                child->x + child->paddingLeft, child->y + child->paddingTop,
                child->width - child->paddingLeft - child->paddingRight,
                child->height - child->paddingTop - child->paddingBottom);
        }
    }
};

// ============================================================================
// FACTORY FUNCTION
// ============================================================================

inline std::shared_ptr<SizedBoxWidget> SizedBox(int w, int h)
{
    auto box = std::make_shared<SizedBoxWidget>();
    if (w >= 0)
    {
        box->width = w;
        box->autoWidth = false;
    }
    if (h >= 0)
    {
        box->height = h;
        box->autoHeight = false;
    }
    return box;
}

#endif // FLUX_SIZEDBOX_HPP