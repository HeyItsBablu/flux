#ifndef FLUX_RENDERER_HPP
#define FLUX_RENDERER_HPP

#include "flux_widget.hpp"

class Renderer
{
public:
    static void renderWidget(HDC hdc, Widget *w, FontCache &fontCache)
    {
        if (!w)
            return;
        w->render(hdc, fontCache);
    }
};

#endif // FLUX_RENDERER_HPP