// flux_painter_android.cpp
#ifdef __ANDROID__

#include "flux/flux_painter.hpp"
#include "nanovg.h"
#include "nanovg_gl.h"

// Set once per frame before any Painter calls
static NVGcontext* s_vg = nullptr;
void FluxAndroid_setVG(NVGcontext* vg) { s_vg = vg; }
NVGcontext* FluxAndroid_getVG()        { return s_vg; }

static NVGcolor toNVG(Color c) {
    return nvgRGBA(c.r, c.g, c.b, c.a);
}

void Painter::fillRect(int x, int y, int w, int h, Color color) {
    if (!s_vg) return;
    nvgBeginPath(s_vg);
    nvgRect(s_vg, x, y, w, h);
    nvgFillColor(s_vg, toNVG(color));
    nvgFill(s_vg);
}

void Painter::fillRoundedRect(int x, int y, int w, int h,
                               int radius, Color color) {
    if (!s_vg) return;
    nvgBeginPath(s_vg);
    nvgRoundedRect(s_vg, x, y, w, h, radius);
    nvgFillColor(s_vg, toNVG(color));
    nvgFill(s_vg);
}

void Painter::drawBorder(int x, int y, int w, int h, int radius,
                          Color color, int borderWidth) {
    if (!s_vg) return;
    nvgBeginPath(s_vg);
    nvgRoundedRect(s_vg, x + 0.5f, y + 0.5f, w - 1, h - 1, radius);
    nvgStrokeColor(s_vg, toNVG(color));
    nvgStrokeWidth(s_vg, static_cast<float>(borderWidth));
    nvgStroke(s_vg);
}

void Painter::fillRectAlpha(int x, int y, int w, int h, Color color) {
    fillRect(x, y, w, h, color); // NanoVG handles alpha natively
}

void Painter::drawLine(int x1, int y1, int x2, int y2,
                        Color color, int width) {
    if (!s_vg) return;
    nvgBeginPath(s_vg);
    nvgMoveTo(s_vg, x1, y1);
    nvgLineTo(s_vg, x2, y2);
    nvgStrokeColor(s_vg, toNVG(color));
    nvgStrokeWidth(s_vg, static_cast<float>(width));
    nvgStroke(s_vg);
}

void Painter::drawHLine(int x, int y, int len, Color color, int sw) {
    drawLine(x, y, x + len, y, color, sw);
}
void Painter::drawVLine(int x, int y, int len, Color color, int sw) {
    drawLine(x, y, x, y + len, color, sw);
}

void Painter::drawEllipse(int x, int y, int w, int h,
                           Color fill, Color stroke, int strokeWidth) {
    if (!s_vg) return;
    float cx = x + w * 0.5f, cy = y + h * 0.5f;
    float rx = w * 0.5f,     ry = h * 0.5f;
    nvgBeginPath(s_vg);
    nvgEllipse(s_vg, cx, cy, rx, ry);
    nvgFillColor(s_vg, toNVG(fill));
    nvgFill(s_vg);
    if (strokeWidth > 0) {
        nvgStrokeColor(s_vg, toNVG(stroke));
        nvgStrokeWidth(s_vg, static_cast<float>(strokeWidth));
        nvgStroke(s_vg);
    }
}

void Painter::drawText(const std::wstring& text, int x, int y, int w, int h,
                        NativeFont fontHandle, Color color, UINT format) {
    if (!s_vg || text.empty()) return;

    // ── Apply font state ──────────────────────────────────────────────────────
    auto* f = reinterpret_cast<FluxAndroidFont*>(fontHandle);
    if (f && f->nvgHandle != -1) {
        nvgFontFaceId(s_vg, f->nvgHandle);
        nvgFontSize(s_vg, f->size);
    } else {
        // Fallback — use whatever NanoVG has active
        nvgFontSize(s_vg, 16.f);
    }

    // ── wstring → UTF-8 ───────────────────────────────────────────────────────
    std::string utf8;
    utf8.reserve(text.size() * 2);
    for (wchar_t wc : text) {
        if (wc < 0x80) {
            utf8 += static_cast<char>(wc);
        } else if (wc < 0x800) {
            utf8 += static_cast<char>(0xC0 | (wc >> 6));
            utf8 += static_cast<char>(0x80 | (wc & 0x3F));
        } else {
            utf8 += static_cast<char>(0xE0 | (wc >> 12));
            utf8 += static_cast<char>(0x80 | ((wc >> 6) & 0x3F));
            utf8 += static_cast<char>(0x80 | (wc & 0x3F));
        }
    }

    // ── Alignment ─────────────────────────────────────────────────────────────
    int hAlign = (format & DT_CENTER) ? NVG_ALIGN_CENTER
               : (format & DT_RIGHT)  ? NVG_ALIGN_RIGHT
                                      : NVG_ALIGN_LEFT;
    int vAlign = (format & DT_VCENTER) ? NVG_ALIGN_MIDDLE : NVG_ALIGN_TOP;
    nvgTextAlign(s_vg, hAlign | vAlign);

    float tx = (format & DT_CENTER) ? x + w * 0.5f
             : (format & DT_RIGHT)  ? static_cast<float>(x + w)
                                    : static_cast<float>(x);
    float ty = (format & DT_VCENTER) ? y + h * 0.5f
                                     : static_cast<float>(y);

    nvgFillColor(s_vg, toNVG(color));
    nvgText(s_vg, tx, ty, utf8.c_str(), nullptr);
}

void Painter::drawTextA(const std::string& text, int x, int y, int w, int h,
                         NativeFont font, Color color, UINT format) {
    drawText(toWideString(text), x, y, w, h, font, color, format);
}

void Painter::measureText(const std::wstring& text, NativeFont fontHandle,
                           int& outW, int& outH) {
    if (!s_vg || text.empty()) { outW = outH = 0; return; }

    // ── Apply font state ──────────────────────────────────────────────────────
    auto* f = reinterpret_cast<FluxAndroidFont*>(fontHandle);
    if (f && f->nvgHandle != -1) {
        nvgFontFaceId(s_vg, f->nvgHandle);
        nvgFontSize(s_vg, f->size);
    } else {
        nvgFontSize(s_vg, 16.f);
    }

    // ── wstring → UTF-8 ───────────────────────────────────────────────────────
    std::string utf8;
    for (wchar_t wc : text) utf8 += static_cast<char>(wc);

    // ── Measure ───────────────────────────────────────────────────────────────
    float bounds[4] = {};
    nvgTextBounds(s_vg, 0, 0, utf8.c_str(), nullptr, bounds);
    outW = static_cast<int>(bounds[2] - bounds[0]);
    outH = static_cast<int>(bounds[3] - bounds[1]);
}

void Painter::pushClipRect(int x, int y, int w, int h) {
    if (!s_vg) return;
    nvgSave(s_vg);
    nvgScissor(s_vg, x, y, w, h);
}
void Painter::popClipRect() {
    if (s_vg) nvgRestore(s_vg);
}
void Painter::pushClipRoundedRect(int x, int y, int w, int h, int r) {
    pushClipRect(x, y, w, h); // NanoVG scissor is rect-only; close enough
}

void Painter::fillGradientRect(int x, int y, int w, int h,
                                const std::vector<Color>& colors) {
    if (colors.empty() || !s_vg) return;
    if (colors.size() == 1) { fillRect(x, y, w, h, colors[0]); return; }
    // NanoVG supports 2-stop gradients natively
    NVGpaint paint = nvgLinearGradient(s_vg,
        static_cast<float>(x),     static_cast<float>(y),
        static_cast<float>(x + w), static_cast<float>(y),
        toNVG(colors.front()), toNVG(colors.back()));
    nvgBeginPath(s_vg);
    nvgRect(s_vg, x, y, w, h);
    nvgFillPaint(s_vg, paint);
    nvgFill(s_vg);
}

void Painter::drawPolyline(const std::vector<std::pair<int,int>>& pts,
                            Color color, int strokeWidth) {
    if (!s_vg || pts.size() < 2) return;
    nvgBeginPath(s_vg);
    nvgMoveTo(s_vg, pts[0].first, pts[0].second);
    for (size_t i = 1; i < pts.size(); ++i)
        nvgLineTo(s_vg, pts[i].first, pts[i].second);
    nvgStrokeColor(s_vg, toNVG(color));
    nvgStrokeWidth(s_vg, static_cast<float>(strokeWidth));
    nvgStroke(s_vg);
}

void Painter::fillPolygonAlpha(const std::vector<std::pair<int,int>>& pts,
                                Color color) {
    if (!s_vg || pts.size() < 3) return;
    nvgBeginPath(s_vg);
    nvgMoveTo(s_vg, pts[0].first, pts[0].second);
    for (size_t i = 1; i < pts.size(); ++i)
        nvgLineTo(s_vg, pts[i].first, pts[i].second);
    nvgClosePath(s_vg);
    nvgFillColor(s_vg, toNVG(color));
    nvgFill(s_vg);
}

void Painter::fillRoundedRegion(int x, int y, int w, int h,
                                 int r, Color color) {
    fillRoundedRect(x, y, w, h, r, color);
}

void Painter::fillRectWithLeftAccent(int x, int y, int w, int h,
                                      Color bg, Color accent, int strip) {
    fillRect(x, y, w, h, bg);
    fillRect(x, y, strip, h, accent);
}

void Painter::drawRectOutline(int x, int y, int w, int h,
                               Color color, int sw) {
    drawBorder(x, y, w, h, 0, color, sw);
}
void Painter::drawRoundedRectOutline(int x, int y, int w, int h,
                                      int r, Color stroke, int sw) {
    drawBorder(x, y, w, h, r, stroke, sw);
}

void Painter::fillRoundedRectGDI(int x, int y, int w, int h, int r,
                                  Color fill, Color stroke, int sw) {
    fillRoundedRect(x, y, w, h, r, fill);
    if (sw > 0) drawBorder(x, y, w, h, r, stroke, sw);
}

void Painter::fillColumnBars(int x, int y, int w, int h,
                              const std::vector<int>& bars, Color color) {
    if (!s_vg || bars.empty()) return;
    int cols = std::min(w, (int)bars.size());
    for (int i = 0; i < cols; ++i) {
        int bh = std::max(0, std::min(h, bars[i]));
        fillRect(x + i, y + h - bh, 1, bh, color);
    }
}

#endif // __ANDROID__