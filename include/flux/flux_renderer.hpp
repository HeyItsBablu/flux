#ifndef FLUX_RENDERER_HPP
#define FLUX_RENDERER_HPP
#include "flux_platform.hpp"
#include "flux_widget.hpp"


class Renderer {
public:
    static void renderWidget(GraphicsContext &ctx, Widget *w, FontCache &fontCache) {
        if (!w)
            return;
        w->render(ctx, fontCache);
    }
};

#endif // FLUX_RENDERER_HPP