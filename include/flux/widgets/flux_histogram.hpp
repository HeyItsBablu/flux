#ifndef FLUX_HISTOGRAM_HPP
#define FLUX_HISTOGRAM_HPP

#include "flux_core.hpp"
#include "flux_state.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <string>
#include <vector>

// ============================================================================
// HISTOGRAM DATA
// ============================================================================

// Holds per-channel bin counts (256 bins each, matching 8-bit pixel values).
// You can fill this from a bitmap, a State<>, or set it manually.
struct HistogramData {
    std::array<uint32_t, 256> r{};
    std::array<uint32_t, 256> g{};
    std::array<uint32_t, 256> b{};
    std::array<uint32_t, 256> lum{}; // luminosity (composite)

    // Convenience: build from a flat RGBX / RGBA byte buffer
    static HistogramData fromPixels(const uint8_t* data, int pixelCount,
                                    int channels = 4 /*RGBA or RGBX*/) {
        HistogramData h;
        for (int i = 0; i < pixelCount; ++i) {
            const uint8_t* p = data + i * channels;
            uint8_t rv = p[0], gv = p[1], bv = p[2];
            h.r[rv]++;
            h.g[gv]++;
            h.b[bv]++;
            // Rec.709 luminosity
            uint8_t lv = (uint8_t)(0.2126f * rv + 0.7152f * gv + 0.0722f * bv);
            h.lum[lv]++;
        }
        return h;
    }

    // Peak across all enabled channels (for auto-scaling)
    uint32_t peak(bool showR, bool showG, bool showB, bool showLum) const {
        uint32_t m = 1;
        auto scan = [&](const std::array<uint32_t, 256>& ch) {
            for (auto v : ch) m = max(m, v);
        };
        if (showR)   scan(r);
        if (showG)   scan(g);
        if (showB)   scan(b);
        if (showLum) scan(lum);
        return m;
    }
};

// ============================================================================
// HISTOGRAM DISPLAY MODE
// ============================================================================

enum class HistogramMode {
    RGB,        // R, G, B channels overlaid (Lightroom default)
    Luminosity, // Luminosity only (greyscale)
    Red,
    Green,
    Blue,
    RGBL        // All four channels: R, G, B + Luminosity
};

// ============================================================================
// HISTOGRAM WIDGET
// ============================================================================

class HistogramWidget : public Widget {
public:
    // ── Data ──────────────────────────────────────────────────────────────────
    HistogramData histData;

    // ── Visual options ────────────────────────────────────────────────────────
    HistogramMode mode        = HistogramMode::RGB;
    bool          showClip    = true;   // Flash/tint clipping indicators
    bool          logScale    = false;  // Logarithmic Y axis (like ACR)
    bool          showGrid    = true;   // Subtle background grid lines
    bool          showChannelToggles = true; // Small R/G/B toggle buttons

    // Colors (match Lightroom's dark theme by default)
    COLORREF bgColor          = RGB(28,  28,  28);
    COLORREF gridColor        = RGB(50,  50,  50);
    COLORREF borderColor      = RGB(55,  55,  55);
    COLORREF rColor           = RGB(220, 60,  60);
    COLORREF gColor           = RGB(60,  210, 60);
    COLORREF bColor           = RGB(60,  100, 240);
    COLORREF lumColor         = RGB(190, 190, 190);
    COLORREF clipHighColor    = RGB(255, 60,  60);   // right-clip tint
    COLORREF clipShadowColor  = RGB(60,  120, 255);  // left-clip tint

    // Per-channel visibility toggles
    bool showR   = true;
    bool showG   = true;
    bool showB   = true;
    bool showLum = true;

    // Callback fired when the user clicks a zone (0.0–1.0 normalised position)
    std::function<void(float)> onZoneClicked;

    // ── Constructor ───────────────────────────────────────────────────────────
    HistogramWidget() {
        width      = 256;
        height     = 120;
        autoWidth  = false;
        autoHeight = false;
        hasBorder  = false;
        isFocusable = true;

        paddingLeft = paddingRight = paddingTop = paddingBottom = 0;
    }

    // ── Fluent API ────────────────────────────────────────────────────────────

    std::shared_ptr<HistogramWidget> setData(const HistogramData& data) {
        histData = data;
        markNeedsPaint();
        return std::static_pointer_cast<HistogramWidget>(shared_from_this());
    }

    std::shared_ptr<HistogramWidget> setData(State<HistogramData>& state) {
        histData = state.get();
        state.bindProperty(
            shared_from_this(),
            [](Widget* w, const HistogramData& d) {
                static_cast<HistogramWidget*>(w)->histData = d;
            },
            false);
        markNeedsPaint();
        return std::static_pointer_cast<HistogramWidget>(shared_from_this());
    }

    std::shared_ptr<HistogramWidget> setMode(HistogramMode m) {
        mode = m;
        markNeedsPaint();
        return std::static_pointer_cast<HistogramWidget>(shared_from_this());
    }

    std::shared_ptr<HistogramWidget> setLogScale(bool v) {
        logScale = v;
        markNeedsPaint();
        return std::static_pointer_cast<HistogramWidget>(shared_from_this());
    }

    std::shared_ptr<HistogramWidget> setShowClip(bool v) {
        showClip = v;
        markNeedsPaint();
        return std::static_pointer_cast<HistogramWidget>(shared_from_this());
    }

    std::shared_ptr<HistogramWidget> setShowGrid(bool v) {
        showGrid = v;
        markNeedsPaint();
        return std::static_pointer_cast<HistogramWidget>(shared_from_this());
    }

    std::shared_ptr<HistogramWidget> setShowChannelToggles(bool v) {
        showChannelToggles = v;
        markNeedsPaint();
        return std::static_pointer_cast<HistogramWidget>(shared_from_this());
    }

    std::shared_ptr<HistogramWidget> setBgColor(COLORREF c) {
        bgColor = c;
        markNeedsPaint();
        return std::static_pointer_cast<HistogramWidget>(shared_from_this());
    }

    std::shared_ptr<HistogramWidget> setSize(int w, int h) {
        width = w; height = h;
        autoWidth = autoHeight = false;
        markNeedsLayout();
        return std::static_pointer_cast<HistogramWidget>(shared_from_this());
    }

    std::shared_ptr<HistogramWidget>
    setOnZoneClicked(std::function<void(float)> cb) {
        onZoneClicked = cb;
        return std::static_pointer_cast<HistogramWidget>(shared_from_this());
    }

    // Hover position (normalised 0-1, -1 = none) — read-only for external use
    float hoverPos = -1.0f;

    // ── Widget overrides ──────────────────────────────────────────────────────

    void computeLayout(HDC /*hdc*/, const BoxConstraints& constraints,
                       FontCache& /*fontCache*/) override {
        width  = constraints.clampWidth (autoWidth  ? constraints.maxWidth  : width);
        height = constraints.clampHeight(autoHeight ? constraints.maxHeight : height);
        applyConstraints();
        needsLayout = false;
    }

    void render(HDC hdc, FontCache& fontCache) override {
        // ── Determine active channels ────────────────────────────────────────
        bool drawR   = false, drawG = false, drawB = false, drawL = false;
        switch (mode) {
            case HistogramMode::RGB:        drawR=showR; drawG=showG; drawB=showB;         break;
            case HistogramMode::Luminosity: drawL=showLum;                                 break;
            case HistogramMode::Red:        drawR=true;                                    break;
            case HistogramMode::Green:      drawG=true;                                    break;
            case HistogramMode::Blue:       drawB=true;                                    break;
            case HistogramMode::RGBL:       drawR=showR; drawG=showG; drawB=showB; drawL=showLum; break;
        }

        // Layout rects
        const int toggleH = showChannelToggles ? 18 : 0;
        RECT plotRect = { x, y, x + width, y + height - toggleH };
        int pw = plotRect.right  - plotRect.left;
        int ph = plotRect.bottom - plotRect.top;

        // ── Background ───────────────────────────────────────────────────────
        HBRUSH bgBrush = CreateSolidBrush(bgColor);
        FillRect(hdc, &plotRect, bgBrush);
        DeleteObject(bgBrush);

        // ── Grid ─────────────────────────────────────────────────────────────
        if (showGrid) {
            HPEN gridPen = CreatePen(PS_SOLID, 1, gridColor);
            HPEN oldPen  = (HPEN)SelectObject(hdc, gridPen);
            // Vertical: shadows / 1/4 / mid / 3/4 / highlights
            for (int i = 1; i <= 3; ++i) {
                int gx = plotRect.left + pw * i / 4;
                MoveToEx(hdc, gx, plotRect.top,    nullptr);
                LineTo  (hdc, gx, plotRect.bottom);
            }
            // Horizontal: 25%, 50%, 75%
            for (int i = 1; i <= 3; ++i) {
                int gy = plotRect.top + ph * i / 4;
                MoveToEx(hdc, plotRect.left,  gy, nullptr);
                LineTo  (hdc, plotRect.right, gy);
            }
            SelectObject(hdc, oldPen);
            DeleteObject(gridPen);
        }

        // ── Peak for normalisation ────────────────────────────────────────────
        uint32_t peak = histData.peak(drawR, drawG, drawB, drawL);

        // ── Clip regions (Lightroom-style: tiny tint strips at extremes) ──────
        if (showClip) {
            const int clipW = 3;
            // Shadow clip tint (left edge)
            HBRUSH shadowBrush = CreateSolidBrush(clipShadowColor);
            RECT   shadowRect  = { plotRect.left, plotRect.top,
                                   plotRect.left + clipW, plotRect.bottom };
            FillRect(hdc, &shadowRect, shadowBrush);
            DeleteObject(shadowBrush);

            // Highlight clip tint (right edge)
            HBRUSH highlightBrush = CreateSolidBrush(clipHighColor);
            RECT   hlRect = { plotRect.right - clipW, plotRect.top,
                              plotRect.right,          plotRect.bottom };
            FillRect(hdc, &hlRect, highlightBrush);
            DeleteObject(highlightBrush);
        }

        // ── Draw channel bars ─────────────────────────────────────────────────
        // We draw each channel with per-pixel alpha blending via GDI's
        // AlphaBlend so overlapping R/G/B mix naturally (like Lightroom).
        // Strategy: draw into an off-screen DIB, then AlphaBlend onto hdc.

        auto drawChannel = [&](const std::array<uint32_t,256>& bins,
                                COLORREF col, BYTE alpha) {
            if (pw <= 0 || ph <= 0) return;

            // Create 32-bit DIB
            BITMAPINFO bmi = {};
            bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
            bmi.bmiHeader.biWidth       = pw;
            bmi.bmiHeader.biHeight      = -ph; // top-down
            bmi.bmiHeader.biPlanes      = 1;
            bmi.bmiHeader.biBitCount    = 32;
            bmi.bmiHeader.biCompression = BI_RGB;

            void* bits = nullptr;
            HBITMAP hBmp = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
            if (!hBmp) return;

            HDC memDC = CreateCompatibleDC(hdc);
            SelectObject(memDC, hBmp);

            // Clear to transparent
            memset(bits, 0, pw * ph * 4);

            BYTE cr = GetRValue(col), cg = GetGValue(col), cb = GetBValue(col);

            // For each pixel column, map bin index → height
            for (int px = 0; px < pw; ++px) {
                // Map pixel X → bin index (0-255)
                float binF = (float)px / (pw - 1) * 255.0f;
                int   bin0 = (int)binF;
                int   bin1 = min(255, bin0 + 1);
                float t    = binF - bin0;

                float raw = (float)bins[bin0] * (1.0f - t) + (float)bins[bin1] * t;

                float normalized;
                if (logScale && peak > 0) {
                    normalized = (raw > 0)
                        ? (float)(std::log(1.0 + raw) / std::log(1.0 + peak))
                        : 0.0f;
                } else {
                    normalized = (peak > 0) ? (raw / (float)peak) : 0.0f;
                }

                int barH = (int)(normalized * ph);
                if (barH <= 0) continue;

                // Fill column bottom-up
                for (int py = ph - barH; py < ph; ++py) {
                    uint8_t* pixel = (uint8_t*)bits + (py * pw + px) * 4;
                    pixel[0] = cb;   // B
                    pixel[1] = cg;   // G
                    pixel[2] = cr;   // R
                    pixel[3] = 255;  // A (premultiplied handled by AlphaBlend)
                }
            }

            BLENDFUNCTION bf = {};
            bf.BlendOp             = AC_SRC_OVER;
            bf.BlendFlags          = 0;
            bf.SourceConstantAlpha = alpha;
            bf.AlphaFormat         = 0; // no per-pixel alpha

            AlphaBlend(hdc, plotRect.left, plotRect.top, pw, ph,
                       memDC,          0,           0, pw, ph, bf);

            DeleteDC(memDC);
            DeleteObject(hBmp);
        };

        // Draw order: luminosity first (bottom), then RGB on top
        if (drawL) drawChannel(histData.lum, lumColor, 130);
        if (drawB) drawChannel(histData.b,   bColor,   160);
        if (drawG) drawChannel(histData.g,   gColor,   160);
        if (drawR) drawChannel(histData.r,   rColor,   160);

        // ── Hover crosshair ───────────────────────────────────────────────────
        if (hoverPos >= 0.0f && hoverPos <= 1.0f) {
            int hx = plotRect.left + (int)(hoverPos * (pw - 1));
            HPEN hoverPen = CreatePen(PS_SOLID, 1, RGB(255,255,255));
            HPEN oldPen   = (HPEN)SelectObject(hdc, hoverPen);
            // Dashed vertical line
            SetBkMode(hdc, TRANSPARENT);
            MoveToEx(hdc, hx, plotRect.top,    nullptr);
            LineTo  (hdc, hx, plotRect.bottom);
            SelectObject(hdc, oldPen);
            DeleteObject(hoverPen);

            // Bin value tooltip
            int binIdx = (int)(hoverPos * 255.0f + 0.5f);
            binIdx = max(0, min(255, binIdx));

            char buf[64];
            if (mode == HistogramMode::Luminosity) {
                std::snprintf(buf, sizeof(buf), "L: %u", histData.lum[binIdx]);
            } else {
                std::snprintf(buf, sizeof(buf), "R:%u G:%u B:%u",
                    histData.r[binIdx], histData.g[binIdx], histData.b[binIdx]);
            }

            // Tiny label above crosshair
            HFONT hFont    = fontCache.getFont(9, FontWeight::Normal);
            HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);
            SetTextColor(hdc, RGB(220,220,220));
            SetBkMode(hdc, TRANSPARENT);

            SIZE  tsz;
            GetTextExtentPoint32(hdc, buf, (int)strlen(buf), &tsz);
            int tx = hx + 4;
            if (tx + tsz.cx > plotRect.right) tx = hx - tsz.cx - 4;
            int ty = plotRect.top + 3;
            TextOut(hdc, tx, ty, buf, (int)strlen(buf));
            SelectObject(hdc, hOldFont);
        }

        // ── Border around plot ────────────────────────────────────────────────
        HPEN borderPen = CreatePen(PS_SOLID, 1, borderColor);
        HPEN oldBPen   = (HPEN)SelectObject(hdc, borderPen);
        HBRUSH nullBrush = (HBRUSH)GetStockObject(NULL_BRUSH);
        HBRUSH oldBrush  = (HBRUSH)SelectObject(hdc, nullBrush);
        Rectangle(hdc, plotRect.left, plotRect.top,
                       plotRect.right, plotRect.bottom);
        SelectObject(hdc, oldBPen);
        SelectObject(hdc, oldBrush);
        DeleteObject(borderPen);

        // ── Channel toggles (bottom strip) ───────────────────────────────────
        if (showChannelToggles) {
            renderToggles(hdc, fontCache,
                          x, y + height - toggleH, width, toggleH,
                          drawR, drawG, drawB, drawL);
        }

        needsPaint = false;
    }

    // ── Mouse interaction ─────────────────────────────────────────────────────

    bool handleMouseMove(int mx, int my) override {
        const int toggleH = showChannelToggles ? 18 : 0;
        int ph = height - toggleH;

        if (mx >= x && mx < x + width && my >= y && my < y + ph) {
            float newPos = (float)(mx - x) / max(1, width - 1);
            newPos = max(0.0f, min(1.0f, newPos));
            if (std::abs(newPos - hoverPos) > 0.001f) {
                hoverPos = newPos;
                markNeedsPaint();
                return true;
            }
        } else if (hoverPos >= 0.0f) {
            hoverPos = -1.0f;
            markNeedsPaint();
            return true;
        }
        return false;
    }

    bool handleMouseLeave() override {
        if (hoverPos >= 0.0f) {
            hoverPos = -1.0f;
            markNeedsPaint();
            return true;
        }
        return false;
    }

    bool handleMouseDown(int mx, int my) override {
        const int toggleH = showChannelToggles ? 18 : 0;
        int ph = height - toggleH;

        // Click in plot → zone callback
        if (mx >= x && mx < x + width && my >= y && my < y + ph) {
            if (onZoneClicked) {
                float pos = (float)(mx - x) / max(1, width - 1);
                onZoneClicked(max(0.0f, min(1.0f, pos)));
            }
            return true;
        }

        // Click in toggle strip
        if (showChannelToggles && my >= y + ph && my < y + height) {
            handleToggleClick(mx - x, width);
            return true;
        }

        return false;
    }

    // ── Size helpers ──────────────────────────────────────────────────────────

    std::shared_ptr<HistogramWidget> setWidth(int w) {
        width = w; autoWidth = false;
        markNeedsLayout();
        return std::static_pointer_cast<HistogramWidget>(shared_from_this());
    }

    std::shared_ptr<HistogramWidget> setHeight(int h) {
        height = h; autoHeight = false;
        markNeedsLayout();
        return std::static_pointer_cast<HistogramWidget>(shared_from_this());
    }

private:
    // ── Toggle-strip hit areas (normalised 0-1 each) ──────────────────────────
    // Layout: [  R  |  G  |  B  |  L  ]  equal width
    void handleToggleClick(int localX, int totalW) {
        int numChannels = 4;
        int slot = localX * numChannels / max(1, totalW);
        switch (slot) {
            case 0: showR   = !showR;   break;
            case 1: showG   = !showG;   break;
            case 2: showB   = !showB;   break;
            case 3: showLum = !showLum; break;
        }
        markNeedsPaint();
    }

    void renderToggles(HDC hdc, FontCache& fontCache,
                       int tx, int ty, int tw, int th,
                       bool /*drawR*/, bool /*drawG*/,
                       bool /*drawB*/, bool /*drawL*/) {
        struct ChanInfo { const char* label; COLORREF col; bool active; };
        ChanInfo channels[4] = {
            { "R", rColor,   showR   },
            { "G", gColor,   showG   },
            { "B", bColor,   showB   },
            { "L", lumColor, showLum },
        };

        // Background
        HBRUSH stripBrush = CreateSolidBrush(RGB(20,20,20));
        RECT   stripRect  = { tx, ty, tx + tw, ty + th };
        FillRect(hdc, &stripRect, stripBrush);
        DeleteObject(stripBrush);

        HFONT hFont    = fontCache.getFont(9, FontWeight::Normal);
        HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);
        SetBkMode(hdc, TRANSPARENT);

        int slotW = tw / 4;
        for (int i = 0; i < 4; ++i) {
            int sx = tx + i * slotW;

            // Active: colored dot + label; Inactive: dimmed
            COLORREF dotCol  = channels[i].active ? channels[i].col : RGB(55,55,55);
            COLORREF textCol = channels[i].active ? channels[i].col : RGB(70,70,70);

            // Dot
            HBRUSH dotBrush = CreateSolidBrush(dotCol);
            HPEN   nullPen  = CreatePen(PS_NULL,0,0);
            HBRUSH ob = (HBRUSH)SelectObject(hdc, dotBrush);
            HPEN   op = (HPEN)  SelectObject(hdc, nullPen);
            int cy = ty + th / 2;
            Ellipse(hdc, sx + 3, cy - 3, sx + 9, cy + 3);
            SelectObject(hdc, ob); SelectObject(hdc, op);
            DeleteObject(dotBrush); DeleteObject(nullPen);

            // Label
            SetTextColor(hdc, textCol);
            RECT labelRect = { sx + 11, ty, sx + slotW - 2, ty + th };
            DrawText(hdc, channels[i].label, -1, &labelRect,
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }

        SelectObject(hdc, hOldFont);
    }
};

// ============================================================================
// FACTORY
// ============================================================================

using HistogramWidgetPtr = std::shared_ptr<HistogramWidget>;

inline HistogramWidgetPtr Histogram() {
    return std::make_shared<HistogramWidget>();
}

inline HistogramWidgetPtr Histogram(int w, int h) {
    auto widget = std::make_shared<HistogramWidget>();
    widget->setSize(w, h);
    return widget;
}

// Convenience: build directly from a raw pixel buffer
inline HistogramWidgetPtr Histogram(const uint8_t* pixels, int pixelCount,
                                     int channels = 4, int w = 256, int h = 120) {
    auto widget = std::make_shared<HistogramWidget>();
    widget->setSize(w, h);
    widget->setData(HistogramData::fromPixels(pixels, pixelCount, channels));
    return widget;
}

/*
// ============================================================================
// USAGE EXAMPLES
// ============================================================================

// ── 1. Static histogram from raw image data ───────────────────────────────────
std::vector<uint8_t> imgPixels = loadImageRGBA("photo.png"); // your loader
int pixelCount = imgPixels.size() / 4;

Histogram(imgPixels.data(), pixelCount, 4, 300, 140)
    ->setMode(HistogramMode::RGB)
    ->setShowGrid(true)
    ->setShowClip(true);


// ── 2. Reactive: live histogram that updates when your image state changes ────
State<HistogramData> histState;

// Build the widget once in your build() method:
Histogram(300, 140)
    ->setData(histState)
    ->setMode(HistogramMode::RGB)
    ->setLogScale(false)
    ->setOnZoneClicked([](float pos) {
        // pos is 0.0 (shadows) → 1.0 (highlights)
        // e.g. jump a tone-curve point to this brightness zone
    });

// Anywhere in your app logic (e.g. after applying an edit):
HistogramData fresh = HistogramData::fromPixels(newPixels.data(), count);
histState.set(fresh);   // histogram repaints itself automatically


// ── 3. Single-channel (Lightroom "Red" channel view) ─────────────────────────
Histogram(300, 140)
    ->setData(myHistState)
    ->setMode(HistogramMode::Red)
    ->setLogScale(true);


// ── 4. Dark-room style: all four channels (R, G, B, Luminosity) ──────────────
Histogram(300, 140)
    ->setData(myHistState)
    ->setMode(HistogramMode::RGBL)
    ->setShowChannelToggles(true)   // click R/G/B/L buttons to show/hide
    ->setShowClip(true);


// ── 5. Embedding in a sidebar panel ──────────────────────────────────────────
Column(
    Text("Histogram")->setFontSize(11)->setTextColor(RGB(180,180,180)),
    Histogram(panelWidth, 120)
        ->setData(currentHistState)
        ->setMode(HistogramMode::RGB)
        ->setShowChannelToggles(true),
    Divider(),
    ...
)
*/

#endif // FLUX_HISTOGRAM_HPP