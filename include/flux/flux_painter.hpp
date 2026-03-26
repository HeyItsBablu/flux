#ifndef FLUX_PAINTER_HPP
#define FLUX_PAINTER_HPP

#include "flux_platform.hpp"
#include "flux_font.hpp"
#include <string>

struct Painter {
    GraphicsContext &ctx;

    explicit Painter(GraphicsContext &c) : ctx(c) {}

    // Filled rounded rect with optional border
    void fillRoundedRect(int x, int y, int w, int h, int radius,
                         NativeColor color, BYTE alpha);

    void drawBorder(int x, int y, int w, int h, int radius,
                    NativeColor color, int borderWidth, BYTE alpha);

    // Text
    void drawText(const std::wstring &text, int x, int y, int w, int h,
                  NativeFont font, NativeColor color, UINT format);

    // Solid filled rect (no rounding, no border) — used for background clear
    void fillRect(int x, int y, int w, int h, NativeColor color);
};

#endif // FLUX_PAINTER_HPP