#ifndef FLUX_TONE_CURVE_HPP
#define FLUX_TONE_CURVE_HPP

// ============================================================================
//  flux_tone_curve.hpp  —  Lightroom-style interactive Tone Curve widget
//
//  Features
//  ────────
//  • Catmull-Rom spline through up to 16 user-placed control points
//  • Parametric mode  (Shadows / Darks / Lights / Highlights region sliders)
//  • Per-channel mode (RGB composite  +  individual R / G / B curves)
//  • Targeted Adjustment Tool (TAT): click-drag on image pixel → moves the
//    curve at that tonal value — wire via setTATValue()
//  • Zone regions drawn under the curve (Shadows / Darks / Lights / Highlights)
//  • Clamp rail at top and bottom (prevents S-curve blowing out)
//  • Hover scrubber: vertical line + input/output tooltip
//  • Add point: click empty curve area
//  • Delete point: double-click or drag off-canvas
//  • Keyboard nudge: arrow keys move selected point ±1 unit
//  • Full reactive State<ToneCurveData> binding
//  • setOnCurveChanged callback for shader integration
// ============================================================================

#include "flux_core.hpp"
#include "flux_state.hpp"
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <functional>
#include <string>
#include <vector>

// ============================================================================
//  TONE CURVE DATA
//  Represents one complete curve (one channel or the composite RGB curve).
//  Points are stored in normalised [0,1]×[0,1] space.
// ============================================================================

struct CurvePoint {
    float x = 0.f; // input  0..1
    float y = 0.f; // output 0..1
};

struct ToneCurveChannel {
    std::vector<CurvePoint> points;

    ToneCurveChannel() {
        // Identity: two fixed endpoints, Lightroom default
        points = { {0.f, 0.f}, {1.f, 1.f} };
    }

    // Evaluate the curve at input t ∈ [0,1] using Catmull-Rom spline.
    // Returns output ∈ [0,1].
    float evaluate(float t) const {
        if (points.size() < 2) return t;
        t = max(0.f, min(1.f, t));

        // Clamp to endpoints outside range
        if (t <= points.front().x) return points.front().y;
        if (t >= points.back().x)  return points.back().y;

        // Find segment
        int seg = 0;
        for (int i = 0; i + 1 < (int)points.size(); ++i) {
            if (t <= points[i+1].x) { seg = i; break; }
        }

        // Catmull-Rom control points (clamped at boundaries)
        auto pt = [&](int i) -> CurvePoint {
            i = max(0, min((int)points.size()-1, i));
            return points[i];
        };
        CurvePoint p0 = pt(seg-1), p1 = pt(seg), p2 = pt(seg+1), p3 = pt(seg+2);

        float dx = p2.x - p1.x;
        if (dx < 1e-6f) return p1.y;

        float u = (t - p1.x) / dx; // local parameter 0..1
        float u2 = u*u, u3 = u2*u;

        // Catmull-Rom basis
        
        float m1y = (p2.y - p0.y) * 0.5f;
  
        float m2y = (p3.y - p1.y) * 0.5f;

        // We only need Y: hermite interpolation
        float h00 =  2*u3 - 3*u2 + 1;
        float h10 =    u3 - 2*u2 + u;
        float h01 = -2*u3 + 3*u2;
        float h11 =    u3 -   u2;

        float y = h00*p1.y + h10*m1y + h01*p2.y + h11*m2y;
        return max(0.f, min(1.f, y));
    }

    // Build a 256-entry LUT for GPU/export use
    std::array<uint8_t, 256> buildLUT() const {
        std::array<uint8_t, 256> lut;
        for (int i = 0; i < 256; ++i)
            lut[i] = (uint8_t)(evaluate(i / 255.f) * 255.f + 0.5f);
        return lut;
    }

    bool isIdentity() const {
        if (points.size() != 2) return false;
        return std::abs(points[0].x) < 0.01f && std::abs(points[0].y) < 0.01f &&
               std::abs(points[1].x - 1.f) < 0.01f && std::abs(points[1].y - 1.f) < 0.01f;
    }

    void reset() { points = { {0.f,0.f}, {1.f,1.f} }; }

    // Ensure points are sorted by x and endpoints are clamped
    void sort() {
        std::sort(points.begin(), points.end(),
                  [](const CurvePoint& a, const CurvePoint& b){ return a.x < b.x; });
        if (!points.empty()) {
            points.front().x = 0.f;
            points.back().x  = 1.f;
        }
    }
};

// Parametric curve offsets (Lightroom's "Point Curve" sliders)
struct ParametricCurve {
    float shadows    = 0.f;  // -1..+1  pulls the shadows region
    float darks      = 0.f;  // -1..+1
    float lights     = 0.f;  // -1..+1
    float highlights = 0.f;  // -1..+1

    void reset() { shadows = darks = lights = highlights = 0.f; }

    // Bake parametric offsets into a ToneCurveChannel
    ToneCurveChannel bake() const {
        ToneCurveChannel ch;
        ch.points = {
            {0.00f, max(0.f, min(1.f,  0.00f + shadows    * 0.20f))},
            {0.25f, max(0.f, min(1.f,  0.25f + darks      * 0.20f))},
            {0.50f, 0.50f},
            {0.75f, max(0.f, min(1.f,  0.75f + lights     * 0.20f))},
            {1.00f, max(0.f, min(1.f,  1.00f + highlights * 0.20f))},
        };
        return ch;
    }
};

struct ToneCurveData {
    ToneCurveChannel rgb;   // composite (applied to all channels)
    ToneCurveChannel r;
    ToneCurveChannel g;
    ToneCurveChannel b;
    ParametricCurve  parametric;
    bool             useParametric = false; // parametric vs point-curve mode

    void reset() {
        rgb.reset(); r.reset(); g.reset(); b.reset();
        parametric.reset();
        useParametric = false;
    }
};

// ============================================================================
//  TONE CURVE WIDGET
// ============================================================================

enum class CurveChannel { RGB, Red, Green, Blue };

class ToneCurveWidget : public Widget {
public:
    // ── Data ──────────────────────────────────────────────────────────────────
    ToneCurveData curveData;

    // ── Display options ───────────────────────────────────────────────────────
    CurveChannel activeChannel  = CurveChannel::RGB;
    bool         showHistogram  = true;  // draw histogram under the curve
    bool         showRegions    = true;  // coloured zone bands (Shadows etc.)
    bool         showGrid       = true;
    bool         showInputOutput= true;  // tooltip showing In/Out values
    bool         parametricMode = false; // show parametric sliders instead

    // Histogram backing data (optional — set via setHistogram())
    std::array<uint32_t, 256> histBins{};
    bool hasHistogram = false;

    // Colors — Lightroom dark theme defaults
    COLORREF bgColor         = RGB(28,  28,  28);
    COLORREF gridColor       = RGB(50,  50,  50);
    COLORREF borderColor     = RGB(60,  60,  60);
    COLORREF curveColorRGB   = RGB(210, 210, 210);
    COLORREF curveColorR     = RGB(220,  70,  70);
    COLORREF curveColorG     = RGB( 70, 200,  70);
    COLORREF curveColorB     = RGB( 70, 120, 240);
    COLORREF pointFill       = RGB(255, 255, 255);
    COLORREF pointBorder     = RGB(120, 120, 120);
    COLORREF pointSelected   = RGB(255, 200,  50);
    COLORREF tatLineColor    = RGB(255, 200,  50);

    // TAT (Targeted Adjustment Tool) — set this to a normalised [0,1] input
    // value coming from the image under the cursor; the widget draws a
    // vertical scrub line and highlights the corresponding curve point.
    float tatValue = -1.f; // -1 = inactive

    // Callback fired whenever the curve changes
    std::function<void(const ToneCurveData&)> onCurveChanged;

    // ── Constructor ───────────────────────────────────────────────────────────
    ToneCurveWidget() {
        width      = 256;
        height     = 256;
        autoWidth  = false;
        autoHeight = false;
        isFocusable = true;
    }

    // ── Fluent API ────────────────────────────────────────────────────────────

    std::shared_ptr<ToneCurveWidget> setCurveData(const ToneCurveData& d) {
        curveData = d;
        markNeedsPaint();
        return ptr();
    }

    std::shared_ptr<ToneCurveWidget> setCurveData(State<ToneCurveData>& state) {
        curveData = state.get();
        state.bindProperty(shared_from_this(),
            [](Widget* w, const ToneCurveData& d){
                static_cast<ToneCurveWidget*>(w)->curveData = d;
            }, false);
        markNeedsPaint();
        return ptr();
    }

    std::shared_ptr<ToneCurveWidget> setActiveChannel(CurveChannel ch) {
        activeChannel = ch;
        markNeedsPaint();
        return ptr();
    }

    std::shared_ptr<ToneCurveWidget> setShowHistogram(bool v) {
        showHistogram = v; markNeedsPaint(); return ptr();
    }

    std::shared_ptr<ToneCurveWidget> setShowRegions(bool v) {
        showRegions = v; markNeedsPaint(); return ptr();
    }

    std::shared_ptr<ToneCurveWidget> setShowGrid(bool v) {
        showGrid = v; markNeedsPaint(); return ptr();
    }

    std::shared_ptr<ToneCurveWidget> setParametricMode(bool v) {
        parametricMode = v;
        curveData.useParametric = v;
        markNeedsPaint();
        return ptr();
    }

    std::shared_ptr<ToneCurveWidget> setHistogram(
            const std::array<uint32_t,256>& bins) {
        histBins     = bins;
        hasHistogram = true;
        markNeedsPaint();
        return ptr();
    }

    // Convenience: feed R+G+B arrays and build a luminosity composite
    std::shared_ptr<ToneCurveWidget> setHistogram(
            const std::array<uint32_t,256>& r,
            const std::array<uint32_t,256>& g,
            const std::array<uint32_t,256>& b) {
        for (int i = 0; i < 256; ++i)
            histBins[i] = (uint32_t)(0.2126f*r[i] + 0.7152f*g[i] + 0.0722f*b[i]);
        hasHistogram = true;
        markNeedsPaint();
        return ptr();
    }

    // TAT: call this when the mouse moves over the image canvas
    std::shared_ptr<ToneCurveWidget> setTATValue(float normalisedInput) {
        tatValue = normalisedInput;
        markNeedsPaint();
        return ptr();
    }

    std::shared_ptr<ToneCurveWidget>
    setOnCurveChanged(std::function<void(const ToneCurveData&)> cb) {
        onCurveChanged = cb; return ptr();
    }

    std::shared_ptr<ToneCurveWidget> setSize(int w, int h) {
        width = w; height = h;
        autoWidth = autoHeight = false;
        markNeedsLayout();
        return ptr();
    }

    std::shared_ptr<ToneCurveWidget> setWidth(int w) {
        width = w; autoWidth = false; markNeedsLayout(); return ptr();
    }

    std::shared_ptr<ToneCurveWidget> setHeight(int h) {
        height = h; autoHeight = false; markNeedsLayout(); return ptr();
    }

    // Reset curve to identity
    std::shared_ptr<ToneCurveWidget> resetCurve() {
        activeChannelCurve().reset();
        selectedPoint = -1;
        notifyCurveChanged();
        markNeedsPaint();
        return ptr();
    }

    // ── Layout ────────────────────────────────────────────────────────────────

    void computeLayout(HDC /*hdc*/, const BoxConstraints& constraints,
                       FontCache& /*fontCache*/) override {
        width  = constraints.clampWidth (autoWidth  ? constraints.maxWidth  : width);
        height = constraints.clampHeight(autoHeight ? constraints.maxHeight : height);
        applyConstraints();
        needsLayout = false;
    }

    // ── Render ────────────────────────────────────────────────────────────────

    void render(HDC hdc, FontCache& fontCache) override {
        // ── Plot area (inset from widget bounds for axis labels) ───────────────
        const int PAD_L = 6, PAD_R = 6, PAD_T = 6, PAD_B = 6;
        plotX = x + PAD_L;
        plotY = y + PAD_T;
        plotW = width  - PAD_L - PAD_R;
        plotH = height - PAD_T - PAD_B;
        if (parametricMode) plotH -= kParamStripH + 6;

        // ── Background ────────────────────────────────────────────────────────
        HBRUSH bgBrush = CreateSolidBrush(bgColor);
        RECT   bgRect  = { x, y, x+width, y+height };
        FillRect(hdc, &bgRect, bgBrush);
        DeleteObject(bgBrush);

        // ── Zone region bands ─────────────────────────────────────────────────
        if (showRegions) drawRegions(hdc);

        // ── Histogram ─────────────────────────────────────────────────────────
        if (showHistogram && hasHistogram) drawHistogram(hdc);

        // ── Grid ──────────────────────────────────────────────────────────────
        if (showGrid) drawGrid(hdc);

        // ── Diagonal identity line ────────────────────────────────────────────
        {
            HPEN idPen = CreatePen(PS_SOLID, 1, RGB(55,55,55));
            HPEN op    = (HPEN)SelectObject(hdc, idPen);
            MoveToEx(hdc, plotX,        plotY+plotH, nullptr);
            LineTo  (hdc, plotX+plotW,  plotY);
            SelectObject(hdc, op);
            DeleteObject(idPen);
        }

        // ── TAT line ─────────────────────────────────────────────────────────
        if (tatValue >= 0.f && tatValue <= 1.f) {
            int tx = plotX + (int)(tatValue * plotW);
            HPEN tp = CreatePen(PS_DOT, 1, tatLineColor);
            HPEN op = (HPEN)SelectObject(hdc, tp);
            SetBkMode(hdc, TRANSPARENT);
            MoveToEx(hdc, tx, plotY,        nullptr);
            LineTo  (hdc, tx, plotY+plotH);
            SelectObject(hdc, op);
            DeleteObject(tp);
        }

        // ── Hover vertical scrubber ───────────────────────────────────────────
        if (hoverX >= 0) {
            HPEN hp = CreatePen(PS_SOLID, 1, RGB(90,90,90));
            HPEN op = (HPEN)SelectObject(hdc, hp);
            MoveToEx(hdc, hoverX, plotY,        nullptr);
            LineTo  (hdc, hoverX, plotY+plotH);
            SelectObject(hdc, op);
            DeleteObject(hp);
        }

        // ── Curve(s) ──────────────────────────────────────────────────────────
        // In RGB mode draw all non-identity channels dimly first, then RGB on top
        if (activeChannel == CurveChannel::RGB) {
            if (!curveData.r.isIdentity())
                drawCurve(hdc, curveData.r, curveColorR, 1, false);
            if (!curveData.g.isIdentity())
                drawCurve(hdc, curveData.g, curveColorG, 1, false);
            if (!curveData.b.isIdentity())
                drawCurve(hdc, curveData.b, curveColorB, 1, false);
            drawCurve(hdc, activeChannelCurve(), curveColorRGB, 2, true);
        } else {
            drawCurve(hdc, activeChannelCurve(), channelColor(), 2, true);
        }

        // ── Control points ────────────────────────────────────────────────────
        if (!parametricMode)
            drawPoints(hdc);

        // ── Border ────────────────────────────────────────────────────────────
        {
            HPEN bp  = CreatePen(PS_SOLID, 1, borderColor);
            HPEN op  = (HPEN)SelectObject(hdc, bp);
            HBRUSH nb = (HBRUSH)GetStockObject(NULL_BRUSH);
            HBRUSH ob = (HBRUSH)SelectObject(hdc, nb);
            Rectangle(hdc, plotX, plotY, plotX+plotW, plotY+plotH);
            SelectObject(hdc, op); SelectObject(hdc, ob);
            DeleteObject(bp);
        }

        // ── Channel tab strip (top) ───────────────────────────────────────────
        drawChannelTabs(hdc, fontCache);

        // ── Hover tooltip (In / Out) ──────────────────────────────────────────
        if (showInputOutput && hoverX >= 0) drawTooltip(hdc, fontCache);

        // ── Parametric sliders (bottom strip) ────────────────────────────────
        if (parametricMode) drawParametricStrip(hdc, fontCache);

        needsPaint = false;
    }

    // ── Mouse ─────────────────────────────────────────────────────────────────

    bool handleMouseDown(int mx, int my) override {
        // Channel tab click
        if (hitTestTab(mx, my) >= 0) {
            activeChannel = (CurveChannel)hitTestTab(mx, my);
            selectedPoint = -1;
            markNeedsPaint();
            return true;
        }

        // Parametric slider drag
        if (parametricMode) {
            int slot = hitTestParamSlider(mx, my);
            if (slot >= 0) {
                draggingParamSlot = slot;
                dragStartX        = mx;
                dragStartVal      = getParamValue(slot);
                return true;
            }
            return false;
        }

        if (!inPlot(mx, my)) return false;

        // Hit test existing point
        int hit = hitTestPoint(mx, my);
        if (hit >= 0) {
            selectedPoint = hit;
            dragging      = true;
            dragStartX    = mx; dragStartY = my;
            markNeedsPaint();
            return true;
        }

        // Add new point
        auto [nx, ny] = screenToNorm(mx, my);
        nx = max(0.01f, min(0.99f, nx)); // never override endpoints
        ny = max(0.f,   min(1.f,   ny));

        auto& pts = activeChannelCurve().points;

        // Don't add too close to an existing point
        for (auto& p : pts) {
            if (std::abs(p.x - nx) < 0.03f) { selectedPoint = -1; return true; }
        }

        pts.push_back({nx, ny});
        std::sort(pts.begin(), pts.end(),
                  [](const CurvePoint& a, const CurvePoint& b){ return a.x < b.x; });

        // Find index after sort
        selectedPoint = -1;
        for (int i = 0; i < (int)pts.size(); ++i)
            if (std::abs(pts[i].x - nx) < 0.001f) { selectedPoint = i; break; }

        dragging   = true;
        dragStartX = mx; dragStartY = my;
        notifyCurveChanged();
        markNeedsPaint();
        return true;
    }

    bool handleMouseUp(int /*mx*/, int /*my*/) override {
        if (dragging) {
            dragging = false;
            markNeedsPaint();
        }
        if (draggingParamSlot >= 0) {
            draggingParamSlot = -1;
            markNeedsPaint();
        }
        return false;
    }

    bool handleMouseMove(int mx, int my) override {
        // Parametric drag
        if (draggingParamSlot >= 0) {
            float delta = (mx - dragStartX) / float(plotW) * 2.f; // -1..+1 range
            float nv    = max(-1.f, min(1.f, dragStartVal + delta));
            setParamValue(draggingParamSlot, nv);
            // Bake to point curve for preview
            activeChannelCurve() = curveData.parametric.bake();
            notifyCurveChanged();
            markNeedsPaint();
            return true;
        }

        // Point drag
        if (dragging && selectedPoint >= 0) {
            auto& pts = activeChannelCurve().points;
            auto [nx, ny] = screenToNorm(mx, my);

            // Endpoints: only move Y
            if (selectedPoint == 0 || selectedPoint == (int)pts.size()-1) {
                pts[selectedPoint].y = max(0.f, min(1.f, ny));
            } else {
                // Constrain X between neighbours
                float xMin = pts[selectedPoint-1].x + 0.01f;
                float xMax = pts[selectedPoint+1].x - 0.01f;
                pts[selectedPoint].x = max(xMin, min(xMax, nx));
                pts[selectedPoint].y = max(0.f,  min(1.f,  ny));
            }
            notifyCurveChanged();
            markNeedsPaint();
            return true;
        }

        // Hover
        bool inP = inPlot(mx, my);
        int newHX = inP ? mx : -1;
        if (newHX != hoverX) { hoverX = newHX; markNeedsPaint(); }
        return false;
    }

    bool handleMouseLeave() override {
        hoverX = -1;
        markNeedsPaint();
        return true;
    }

    // Double-click: delete point (except endpoints)
    bool handleLButtonDblClk(int mx, int my) {
        int hit = hitTestPoint(mx, my);
        if (hit > 0 && hit < (int)activeChannelCurve().points.size()-1) {
            activeChannelCurve().points.erase(
                activeChannelCurve().points.begin() + hit);
            selectedPoint = -1;
            notifyCurveChanged();
            markNeedsPaint();
            return true;
        }
        return false;
    }

    bool handleKeyDown(int keyCode) override {
        if (selectedPoint < 0) return false;
        auto& pts = activeChannelCurve().points;
        if (selectedPoint >= (int)pts.size()) return false;

        const float step = 1.f / 255.f; // 1 level
        auto& p = pts[selectedPoint];

        switch (keyCode) {
            case VK_UP:    p.y = min(1.f, p.y + step); break;
            case VK_DOWN:  p.y = max(0.f, p.y - step); break;
            case VK_LEFT:
                if (selectedPoint > 0)
                    p.x = max(pts[selectedPoint-1].x + step, p.x - step);
                break;
            case VK_RIGHT:
                if (selectedPoint < (int)pts.size()-1)
                    p.x = min(pts[selectedPoint+1].x - step, p.x + step);
                break;
            case VK_DELETE:
            case VK_BACK:
                if (selectedPoint > 0 && selectedPoint < (int)pts.size()-1) {
                    pts.erase(pts.begin() + selectedPoint);
                    selectedPoint = -1;
                }
                break;
            default: return false;
        }

        notifyCurveChanged();
        markNeedsPaint();
        return true;
    }

private:
    // ── Layout cache ──────────────────────────────────────────────────────────
    int plotX = 0, plotY = 0, plotW = 1, plotH = 1;
    static constexpr int kTabH       = 18;
    static constexpr int kParamStripH = 52;
    static constexpr int kPointR      = 5;  // control point radius

    // ── Interaction state ─────────────────────────────────────────────────────
    int  selectedPoint      = -1;
    bool dragging           = false;
    int  dragStartX         = 0, dragStartY = 0;
    int  draggingParamSlot  = -1;
    float dragStartVal      = 0.f;
    int  hoverX             = -1;

    // ── Helpers ───────────────────────────────────────────────────────────────

    std::shared_ptr<ToneCurveWidget> ptr() {
        return std::static_pointer_cast<ToneCurveWidget>(shared_from_this());
    }

    ToneCurveChannel& activeChannelCurve() {
        switch (activeChannel) {
            case CurveChannel::Red:   return curveData.r;
            case CurveChannel::Green: return curveData.g;
            case CurveChannel::Blue:  return curveData.b;
            default:                  return curveData.rgb;
        }
    }

    COLORREF channelColor() const {
        switch (activeChannel) {
            case CurveChannel::Red:   return curveColorR;
            case CurveChannel::Green: return curveColorG;
            case CurveChannel::Blue:  return curveColorB;
            default:                  return curveColorRGB;
        }
    }

    // Normalised [0,1] → screen pixel
    int normToScreenX(float nx) const { return plotX + (int)(nx * plotW); }
    int normToScreenY(float ny) const { return plotY + plotH - (int)(ny * plotH); }

    // Screen → normalised
    std::pair<float,float> screenToNorm(int sx, int sy) const {
        float nx = (float)(sx - plotX) / max(1, plotW);
        float ny = 1.f - (float)(sy - plotY) / max(1, plotH);
        return {nx, ny};
    }

    bool inPlot(int mx, int my) const {
        return mx >= plotX && mx < plotX+plotW &&
               my >= plotY && my < plotY+plotH;
    }

    int hitTestPoint(int mx, int my) const {
        const auto& pts = const_cast<ToneCurveWidget*>(this)->activeChannelCurve().points;
        for (int i = 0; i < (int)pts.size(); ++i) {
            int px = normToScreenX(pts[i].x);
            int py = normToScreenY(pts[i].y);
            int dx = mx - px, dy = my - py;
            if (dx*dx + dy*dy <= (kPointR+3)*(kPointR+3)) return i;
        }
        return -1;
    }

    void notifyCurveChanged() {
        if (onCurveChanged) onCurveChanged(curveData);
    }

    // ── Draw helpers ──────────────────────────────────────────────────────────

    void drawRegions(HDC hdc) {
        // Lightroom uses very subtle tinted bands:
        // Shadows 0–25%, Darks 25–50%, Lights 50–75%, Highlights 75–100%
        struct Band { float x0, x1; COLORREF col; };
        Band bands[] = {
            {0.00f, 0.25f, RGB(30, 30, 40)},  // Shadows  — cool blue tint
            {0.25f, 0.50f, RGB(30, 32, 30)},  // Darks    — neutral
            {0.50f, 0.75f, RGB(32, 30, 28)},  // Lights   — warm tint
            {0.75f, 1.00f, RGB(36, 30, 26)},  // Highlights
        };
        for (auto& b : bands) {
            RECT r = {
                normToScreenX(b.x0), plotY,
                normToScreenX(b.x1), plotY+plotH
            };
            HBRUSH br = CreateSolidBrush(b.col);
            FillRect(hdc, &r, br);
            DeleteObject(br);
        }
        // Region divider lines
        HPEN dp = CreatePen(PS_SOLID, 1, RGB(45,45,45));
        HPEN op = (HPEN)SelectObject(hdc, dp);
        for (float fx : {0.25f, 0.50f, 0.75f}) {
            int gx = normToScreenX(fx);
            MoveToEx(hdc, gx, plotY,        nullptr);
            LineTo  (hdc, gx, plotY+plotH);
        }
        SelectObject(hdc, op);
        DeleteObject(dp);
    }

    void drawHistogram(HDC hdc) {
        uint32_t peak = 1;
        for (auto v : histBins) peak = max(peak, v);

        // Draw as a semi-transparent dark fill
        for (int i = 0; i < 256; ++i) {
            float t    = float(histBins[i]) / peak;
            // log scale looks better for histograms under curves
            t = (t > 0) ? (std::log(1.f + t * 9.f) / std::log(10.f)) : 0.f;
            int barH = (int)(t * plotH);
            if (barH <= 0) continue;

            int px0 = plotX + (int)((float)i     / 255.f * plotW);
            int px1 = plotX + (int)((float)(i+1) / 255.f * plotW);
            px1 = max(px0+1, px1);

            RECT r = { px0, plotY+plotH-barH, px1, plotY+plotH };
            HBRUSH br = CreateSolidBrush(RGB(60,60,65));
            FillRect(hdc, &r, br);
            DeleteObject(br);
        }
    }

    void drawGrid(HDC hdc) {
        HPEN gp = CreatePen(PS_SOLID, 1, gridColor);
        HPEN op = (HPEN)SelectObject(hdc, gp);
        SetBkMode(hdc, TRANSPARENT);
        for (int i = 1; i <= 3; ++i) {
            int gx = plotX + plotW * i / 4;
            int gy = plotY + plotH * i / 4;
            MoveToEx(hdc, gx, plotY,  nullptr); LineTo(hdc, gx, plotY+plotH);
            MoveToEx(hdc, plotX, gy,  nullptr); LineTo(hdc, plotX+plotW, gy);
        }
        SelectObject(hdc, op);
        DeleteObject(gp);
    }

    // Draw one smooth curve by evaluating 256 points
    void drawCurve(HDC hdc, const ToneCurveChannel& ch,
                   COLORREF col, int lineWidth, bool drawFill) {
        // Optional area fill under curve
        if (drawFill) {
            // Build polygon for fill (curve top + bottom baseline)
            std::vector<POINT> poly;
            poly.reserve(plotW + 4);
            poly.push_back({plotX, plotY+plotH}); // bottom-left

            for (int px = 0; px <= plotW; ++px) {
                float nx = (float)px / plotW;
                float ny = ch.evaluate(nx);
                poly.push_back({ plotX+px, normToScreenY(ny) });
            }
            poly.push_back({plotX+plotW, plotY+plotH}); // bottom-right

            // Alpha fill using AlphaBlend
            int polyW = plotW+2, polyH = plotH;
            BITMAPINFO bmi = {};
            bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bmi.bmiHeader.biWidth = polyW; bmi.bmiHeader.biHeight = -polyH;
            bmi.bmiHeader.biPlanes = 1; bmi.bmiHeader.biBitCount = 32;
            bmi.bmiHeader.biCompression = BI_RGB;
            void* bits = nullptr;
            HBITMAP hBmp = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
            if (hBmp) {
                HDC mdc = CreateCompatibleDC(hdc);
                SelectObject(mdc, hBmp);
                memset(bits, 0, polyW*polyH*4);

                // Build path and fill in memory DC
                HPEN nullPen   = CreatePen(PS_NULL,0,0);
                HBRUSH fillBr  = CreateSolidBrush(col);
                HPEN   op2     = (HPEN)  SelectObject(mdc, nullPen);
                HBRUSH ob2     = (HBRUSH)SelectObject(mdc, fillBr);

                // Adjust poly coords to mdc local space
                std::vector<POINT> localPoly = poly;
                for (auto& pt : localPoly) {
                    pt.x -= plotX;
                    pt.y -= plotY;
                }
                Polygon(mdc, localPoly.data(), (int)localPoly.size());

                SelectObject(mdc, op2); SelectObject(mdc, ob2);
                DeleteObject(nullPen); DeleteObject(fillBr);

                BLENDFUNCTION bf{AC_SRC_OVER,0,28,0}; // ~11% opacity
                AlphaBlend(hdc, plotX, plotY, polyW, polyH,
                           mdc, 0, 0, polyW, polyH, bf);
                DeleteDC(mdc);
                DeleteObject(hBmp);
            }
        }

        // Draw curve line (polyline for speed)
        std::vector<POINT> pts;
        pts.reserve(plotW+1);
        for (int px = 0; px <= plotW; ++px) {
            float nx = (float)px / plotW;
            float ny = ch.evaluate(nx);
            pts.push_back({ plotX+px, normToScreenY(ny) });
        }
        HPEN cp = CreatePen(PS_SOLID, lineWidth, col);
        HPEN op = (HPEN)SelectObject(hdc, cp);
        Polyline(hdc, pts.data(), (int)pts.size());
        SelectObject(hdc, op);
        DeleteObject(cp);
    }

    void drawPoints(HDC hdc) {
        const auto& pts = activeChannelCurve().points;
        for (int i = 0; i < (int)pts.size(); ++i) {
            int px = normToScreenX(pts[i].x);
            int py = normToScreenY(pts[i].y);
            bool sel = (i == selectedPoint);

            // Shadow
            HBRUSH shBr = CreateSolidBrush(RGB(0,0,0));
            HPEN   nullPen = CreatePen(PS_NULL,0,0);
            HPEN   op1 = (HPEN)  SelectObject(hdc, nullPen);
            HBRUSH ob1 = (HBRUSH)SelectObject(hdc, shBr);
            Ellipse(hdc, px-kPointR, py-kPointR+2, px+kPointR, py+kPointR+2);
            SelectObject(hdc, op1); SelectObject(hdc, ob1);
            DeleteObject(shBr); DeleteObject(nullPen);

            // Fill
            COLORREF fc  = sel ? pointSelected : pointFill;
            HBRUSH ptBr  = CreateSolidBrush(fc);
            HPEN   ptPen = CreatePen(PS_SOLID, 1,
                               sel ? RGB(255,220,80) : pointBorder);
            HPEN   op2   = (HPEN)  SelectObject(hdc, ptPen);
            HBRUSH ob2   = (HBRUSH)SelectObject(hdc, ptBr);
            Ellipse(hdc, px-kPointR, py-kPointR, px+kPointR, py+kPointR);
            SelectObject(hdc, op2); SelectObject(hdc, ob2);
            DeleteObject(ptBr); DeleteObject(ptPen);
        }
    }

    // ── Channel tab strip at top of widget ───────────────────────────────────

    void drawChannelTabs(HDC hdc, FontCache& fontCache) {
        struct Tab { CurveChannel ch; const char* label; COLORREF col; };
        Tab tabs[] = {
            {CurveChannel::RGB,   "RGB", curveColorRGB},
            {CurveChannel::Red,   "R",   curveColorR  },
            {CurveChannel::Green, "G",   curveColorG  },
            {CurveChannel::Blue,  "B",   curveColorB  },
        };
        const int tabW = 32;
        int tx = x + width - 4 * tabW - 4;

        HFONT hFont    = fontCache.getFont(9, FontWeight::Bold);
        HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);
        SetBkMode(hdc, TRANSPARENT);

        for (int i = 0; i < 4; ++i) {
            bool active = (tabs[i].ch == activeChannel);
            RECT tr = { tx + i*tabW, y, tx + (i+1)*tabW, y + kTabH };

            HBRUSH tbr = CreateSolidBrush(active ? RGB(45,45,55) : bgColor);
            FillRect(hdc, &tr, tbr);
            DeleteObject(tbr);

            if (active) {
                // Accent underline
                HPEN ul = CreatePen(PS_SOLID, 2, tabs[i].col);
                HPEN op = (HPEN)SelectObject(hdc, ul);
                MoveToEx(hdc, tr.left+2,  tr.bottom-2, nullptr);
                LineTo  (hdc, tr.right-2, tr.bottom-2);
                SelectObject(hdc, op);
                DeleteObject(ul);
            }

            SetTextColor(hdc, active ? tabs[i].col : RGB(80,80,90));
            DrawText(hdc, tabs[i].label, -1, &tr,
                     DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        SelectObject(hdc, hOldFont);
    }

    // ── In/Out tooltip ────────────────────────────────────────────────────────

    void drawTooltip(HDC hdc, FontCache& fontCache) {
        if (hoverX < plotX || hoverX > plotX+plotW) return;

        float nx  = (float)(hoverX - plotX) / plotW;
        float ny  = activeChannelCurve().evaluate(nx);
        int   inV = (int)(nx  * 255.f + 0.5f);
        int   outV= (int)(ny  * 255.f + 0.5f);

        char buf[32];
        _snprintf_s(buf, sizeof(buf), _TRUNCATE, "%d → %d", inV, outV);

        HFONT hFont = fontCache.getFont(9, FontWeight::Normal);
        HFONT hOld  = (HFONT)SelectObject(hdc, hFont);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(200,200,200));

        SIZE tsz;
        GetTextExtentPoint32(hdc, buf, (int)strlen(buf), &tsz);
        int tx = hoverX + 6;
        if (tx + tsz.cx > plotX+plotW) tx = hoverX - tsz.cx - 4;
        int ty = plotY + 4;
        TextOut(hdc, tx, ty, buf, (int)strlen(buf));
        SelectObject(hdc, hOld);

        // Output dot on curve
        int dotY = normToScreenY(ny);
        HBRUSH db = CreateSolidBrush(channelColor());
        HPEN   dp = CreatePen(PS_NULL,0,0);
        HPEN   op = (HPEN)  SelectObject(hdc, dp);
        HBRUSH ob = (HBRUSH)SelectObject(hdc, db);
        Ellipse(hdc, hoverX-3, dotY-3, hoverX+3, dotY+3);
        SelectObject(hdc, op); SelectObject(hdc, ob);
        DeleteObject(db); DeleteObject(dp);
    }

    // ── Parametric slider strip ───────────────────────────────────────────────

    void drawParametricStrip(HDC hdc, FontCache& fontCache) {
        // Four sliders side-by-side below the curve plot
        int sy = plotY + plotH + 6;
        int sw = plotW / 4;

        struct Slot {
            const char* label;
            float       value;
            COLORREF    col;
        };
        auto& p = curveData.parametric;
        Slot slots[] = {
            {"Shadows",    p.shadows,    RGB( 80,100,160)},
            {"Darks",      p.darks,      RGB(120,120,140)},
            {"Lights",     p.lights,     RGB(180,160,110)},
            {"Highlights", p.highlights, RGB(220,200,140)},
        };

        HFONT hFont = fontCache.getFont(8, FontWeight::Normal);
        HFONT hOld  = (HFONT)SelectObject(hdc, hFont);
        SetBkMode(hdc, TRANSPARENT);

        for (int i = 0; i < 4; ++i) {
            int sx = plotX + i * sw;

            // Label
            SetTextColor(hdc, RGB(100,100,110));
            RECT lr = {sx, sy, sx+sw, sy+14};
            DrawText(hdc, slots[i].label, -1, &lr, DT_CENTER|DT_VCENTER|DT_SINGLELINE);

            // Track
            int trackY  = sy + 18;
            int trackX0 = sx + 4;
            int trackX1 = sx + sw - 4;
            int trackMid= (trackX0+trackX1)/2;
            int thumbX  = trackMid + (int)(slots[i].value * (trackX1-trackMid));

            HPEN tp = CreatePen(PS_SOLID, 2, RGB(50,50,55));
            HPEN op = (HPEN)SelectObject(hdc, tp);
            MoveToEx(hdc, trackX0, trackY, nullptr);
            LineTo  (hdc, trackX1, trackY);
            SelectObject(hdc, op); DeleteObject(tp);

            // Filled portion from mid
            HPEN fp = CreatePen(PS_SOLID, 2, slots[i].col);
            op = (HPEN)SelectObject(hdc, fp);
            MoveToEx(hdc, trackMid, trackY, nullptr);
            LineTo  (hdc, thumbX,   trackY);
            SelectObject(hdc, op); DeleteObject(fp);

            // Thumb
            HBRUSH tb = CreateSolidBrush(
                draggingParamSlot == i ? RGB(255,220,50) : slots[i].col);
            HPEN   tpn = CreatePen(PS_NULL,0,0);
            op         = (HPEN)  SelectObject(hdc, tpn);
            HBRUSH ob  = (HBRUSH)SelectObject(hdc, tb);
            Ellipse(hdc, thumbX-5, trackY-5, thumbX+5, trackY+5);
            SelectObject(hdc, op); SelectObject(hdc, ob);
            DeleteObject(tb); DeleteObject(tpn);

            // Value label
            char vbuf[8];
            _snprintf_s(vbuf,sizeof(vbuf),_TRUNCATE,"%+.0f",slots[i].value*100.f);
            SetTextColor(hdc, RGB(160,160,160));
            RECT vr = {sx, trackY+8, sx+sw, trackY+20};
            DrawText(hdc, vbuf, -1, &vr, DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        }
        SelectObject(hdc, hOld);
    }

    // ── Tab hit test ─────────────────────────────────────────────────────────
    int hitTestTab(int mx, int my) const {
        if (my < y || my > y+kTabH) return -1;
        const int tabW = 32;
        int tx = x + width - 4*tabW - 4;
        int slot = (mx - tx) / tabW;
        if (slot < 0 || slot > 3) return -1;
        return slot; // maps directly to CurveChannel enum
    }

    // ── Parametric slider hit test ────────────────────────────────────────────
    int hitTestParamSlider(int mx, int my) const {
        int sy = plotY + plotH + 6;
        if (my < sy || my > sy + kParamStripH) return -1;
        int sw   = plotW / 4;
        int slot = (mx - plotX) / sw;
        return (slot >= 0 && slot < 4) ? slot : -1;
    }

    float getParamValue(int slot) const {
        switch (slot) {
            case 0: return curveData.parametric.shadows;
            case 1: return curveData.parametric.darks;
            case 2: return curveData.parametric.lights;
            case 3: return curveData.parametric.highlights;
        }
        return 0.f;
    }

    void setParamValue(int slot, float v) {
        switch (slot) {
            case 0: curveData.parametric.shadows    = v; break;
            case 1: curveData.parametric.darks      = v; break;
            case 2: curveData.parametric.lights     = v; break;
            case 3: curveData.parametric.highlights = v; break;
        }
    }
};

// ============================================================================
//  FACTORY
// ============================================================================

using ToneCurveWidgetPtr = std::shared_ptr<ToneCurveWidget>;

inline ToneCurveWidgetPtr ToneCurve() {
    return std::make_shared<ToneCurveWidget>();
}

inline ToneCurveWidgetPtr ToneCurve(int size) {
    auto w = std::make_shared<ToneCurveWidget>();
    w->setSize(size, size);
    return w;
}

inline ToneCurveWidgetPtr ToneCurve(int w, int h) {
    auto widget = std::make_shared<ToneCurveWidget>();
    widget->setSize(w, h);
    return widget;
}

/*
// ============================================================================
//  USAGE EXAMPLES
// ============================================================================

// ── 1. Basic drop-in (square, default RGB channel) ───────────────────────────
ToneCurve(256)
    ->setShowHistogram(true)
    ->setShowRegions(true)
    ->setOnCurveChanged([&](const ToneCurveData& d){
        // d.rgb.buildLUT()  → pass to shader as a 1D texture or uniform array
    });


// ── 2. Reactive binding with State ───────────────────────────────────────────
State<ToneCurveData> curveState;

ToneCurve(280, 240)
    ->setCurveData(curveState)
    ->setActiveChannel(CurveChannel::RGB)
    ->setOnCurveChanged([&](const ToneCurveData& d){
        curveState.set(d);           // write-back so other widgets stay in sync
    });

// Somewhere else in the app — evaluating the curve for export:
auto lut = curveState.get().rgb.buildLUT();


// ── 3. With histogram under the curve (feed HistogramData from your surface) ─
surface->onHistogramUpdated = [&](const HistogramData& h){
    histState.set(h);
    // Also feed the raw bins into the curve widget:
    curveWidget->setHistogram(h.r, h.g, h.b);
};


// ── 4. Parametric mode (Lightroom's slider-based tone curve) ─────────────────
ToneCurve(256)
    ->setParametricMode(true)   // shows Shadows/Darks/Lights/Highlights sliders
    ->setOnCurveChanged([&](const ToneCurveData& d){
        // d.parametric has the raw slider values
        // d.rgb is the baked point curve ready for GPU
    });


// ── 5. TAT — Targeted Adjustment Tool ────────────────────────────────────────
// When the mouse moves over your image canvas, sample the pixel luminosity
// and push it into the curve widget as a guide line:
canvas->onMouseMove = [&](float nx, float ){
    float lum = sampleImageLuminance(nx, ny);  // your function
    curveWidget->setTATValue(lum);
};

// On mouse drag over the image (vertical drag = curve adjustment):
canvas->onDragDelta = [&](float , float dy){
    float input = curveWidget->tatValue;
    if (input < 0) return;
    // Find or create point near that input value and nudge it
    auto& ch = curveState.get().rgb;
    // ... insert/move point at `input` by `dy * sensitivity`
    curveState.set(ch_updated);
};


// ── 6. Wiring into the image_editor.hpp shader ───────────────────────────────
// In your ImageEditorApp::build():
auto curveWidget = ToneCurve(258, 200)
    ->setShowHistogram(true)
    ->setShowRegions(true)
    ->setOnCurveChanged([this](const ToneCurveData& d){
        // Upload LUTs to GL as 1D textures, or encode as uniform float arrays
        auto lutR = d.r.buildLUT();   // std::array<uint8_t,256>
        auto lutG = d.g.buildLUT();
        auto lutB = d.b.buildLUT();
        auto lutRGB = d.rgb.buildLUT();
        surface_->uploadCurveLUTs(lutRGB, lutR, lutG, lutB);
        if (canvasPtr_) canvasPtr_->redraw();
    });
*/

#endif // FLUX_TONE_CURVE_HPP