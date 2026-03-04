#ifndef FLUX_HSL_PANEL_HPP
#define FLUX_HSL_PANEL_HPP

// ============================================================================
//  flux_hsl_panel.hpp  —  Lightroom-style HSL / Color panel widget
//
//  Features
//  ────────
//  • Eight hue bands: Red, Orange, Yellow, Green, Aqua, Blue, Purple, Magenta
//  • Three sub-panels selectable via tab strip: Hue / Saturation / Luminance
//  • "All" view: shows all three sliders stacked per color band
//  • Per-band sliders with colour-tinted tracks matching the hue band
//  • Compact numeric readout (–100 … +100) next to each slider
//  • Full reactive State<HSLData> binding
//  • setOnHSLChanged callback for shader / EditParams integration
//  • Lightroom dark-theme colour palette baked in, fully overridable
//  • TAT (Targeted Adjustment Tool) support: call setTATHue() with the
//    dominant hue (0–360°) of the pixel under the cursor; the matching
//    band is highlighted with a subtle glow
//  • Reset-band on double-click of band label
//  • buildLUTs() → produces three 360-entry float arrays (hShift, sScale,
//    lShift) ready to be evaluated per-pixel in a shader or CPU path
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
//  HSL DATA
// ============================================================================

// Eight named hue bands, matching Lightroom order
enum class HSLBand {
    Red = 0, Orange, Yellow, Green, Aqua, Blue, Purple, Magenta,
    COUNT = 8
};
static constexpr int kHSLBandCount = (int)HSLBand::COUNT;

struct HSLBandValues {
    float hue        = 0.f;  // –1 … +1  (maps to ±30° hue shift)
    float saturation = 0.f;  // –1 … +1
    float luminance  = 0.f;  // –1 … +1
};

struct HSLData {
    std::array<HSLBandValues, kHSLBandCount> bands{};

    void reset() {
        for (auto& b : bands) b = HSLBandValues{};
    }

    bool isIdentity() const {
        for (auto& b : bands)
            if (b.hue != 0.f || b.saturation != 0.f || b.luminance != 0.f)
                return false;
        return true;
    }

    // ── LUT builders ─────────────────────────────────────────────────────────
    // Each LUT has 360 entries, one per integer hue degree.
    // hShift[h]  = total hue offset in degrees  for a pixel at hue h
    // sScale[h]  = saturation multiplier         for a pixel at hue h
    // lShift[h]  = luminance additive offset     for a pixel at hue h
    //
    // In GLSL the caller does:
    //   hsl.x = fract(hsl.x + texture(uLutHue, vec2(hsl.x, 0.5)).r - 0.5)
    //   hsl.y = clamp(hsl.y * texture(uLutSat, vec2(hsl.x, 0.5)).r,  0, 1)
    //   hsl.z = clamp(hsl.z + texture(uLutLum, vec2(hsl.x, 0.5)).r - 0.5, 0, 1)
    // (Each LUT is encoded as an 8-bit texture with 0.5 = 0, so shifts are
    //  stored as value+0.5; saturation scale is stored directly 0..2.)

    struct LUTs {
        std::array<float, 360> hShift{};   // degrees  –30..+30
        std::array<float, 360> sScale{};   // multiplier 0..2
        std::array<float, 360> lShift{};   // –1..+1
    };

    LUTs buildLUTs() const {
        LUTs out;
        // Band centre hues and half-widths (degrees), matching Lightroom
        struct BandDef { float center, halfWidth; };
        static constexpr BandDef kBands[kHSLBandCount] = {
            { 15.f, 30.f},  // Red     (wraps 345–45)
            { 45.f, 22.f},  // Orange  (23–67)
            { 75.f, 22.f},  // Yellow  (53–97)
            {135.f, 37.f},  // Green   (98–172)
            {195.f, 37.f},  // Aqua    (173–232)
            {245.f, 30.f},  // Blue    (215–275)
            {290.f, 30.f},  // Purple  (260–320)
            {330.f, 22.f},  // Magenta (308–352)
        };

        // Initialise: hShift=0, sScale=1, lShift=0
        out.sScale.fill(1.f);

        for (int h = 0; h < 360; ++h) {
            float totalH = 0.f, totalS = 0.f, totalL = 0.f, totalW = 0.f;
            for (int i = 0; i < kHSLBandCount; ++i) {
                float c   = kBands[i].center;
                float hw  = kBands[i].halfWidth;
                float diff = hueDiff(float(h), c);     // 0..180
                if (diff >= hw) continue;
                // Smooth tent falloff: weight = cos²(π/2 * diff/hw)
                float t = diff / hw;
                float w = (1.f - t * t);               // simpler quadratic
                const auto& bv = bands[i];
                totalH += bv.hue        * 30.f * w;    // ±30°
                totalS += bv.saturation * w;
                totalL += bv.luminance  * w;
                totalW += w;
            }
            if (totalW > 1e-6f) {
                out.hShift[h] = totalH / totalW;
                out.sScale[h] = 1.f + totalS / totalW;
                out.lShift[h] = totalL / totalW;
            } else {
                out.hShift[h] = 0.f;
                out.sScale[h] = 1.f;
                out.lShift[h] = 0.f;
            }
        }
        return out;
    }

    // Build GPU-ready uint8 textures (360×1) from the LUTs
    // hue  : stored as (shift/60 + 0.5)*255  → 0=–30°, 128=0°, 255=+30°
    // sat  : stored as (scale/2)*255          → 0=0x, 128=1x, 255=2x
    // lum  : stored as (shift + 1)/2 * 255   → 0=–1, 128=0, 255=+1
    struct GPULUTs {
        std::array<uint8_t, 360> hue{};
        std::array<uint8_t, 360> sat{};
        std::array<uint8_t, 360> lum{};
    };
    GPULUTs buildGPULUTs() const {
        auto raw = buildLUTs();
        GPULUTs g;
        for (int i = 0; i < 360; ++i) {
            g.hue[i] = (uint8_t)std::clamp((raw.hShift[i] / 60.f + 0.5f) * 255.f, 0.f, 255.f);
            g.sat[i] = (uint8_t)std::clamp((raw.sScale[i] / 2.f)         * 255.f, 0.f, 255.f);
            g.lum[i] = (uint8_t)std::clamp((raw.lShift[i] + 1.f) / 2.f   * 255.f, 0.f, 255.f);
        }
        return g;
    }

private:
    static float hueDiff(float a, float b) {
        float d = std::fabs(a - b);
        return d > 180.f ? 360.f - d : d;
    }
};

// ============================================================================
//  HSL PANEL WIDGET
// ============================================================================

enum class HSLTab { Hue = 0, Saturation, Luminance, All, COUNT };

class HSLPanelWidget : public Widget {
public:
    // ── Data ──────────────────────────────────────────────────────────────────
    HSLData data;
    HSLTab  activeTab = HSLTab::All;

    // TAT: dominant hue (0–360°) of the image pixel under the cursor.
    // Set to –1 to disable the highlight.
    float tatHue = -1.f;

    // ── Colors ────────────────────────────────────────────────────────────────
    COLORREF bgColor     = RGB(22, 22, 32);
    COLORREF borderColor = RGB(49, 50, 68);
    COLORREF textDim     = RGB(100,105,130);
    COLORREF textBright  = RGB(205,214,244);
    COLORREF tabActive   = RGB(174,129,255);

    // Callback
    std::function<void(const HSLData&)> onHSLChanged;

    // ── Constructor ───────────────────────────────────────────────────────────
    HSLPanelWidget() {
        width      = 258;
        height     = 300;  // auto-sized in computeLayout
        autoWidth  = false;
        autoHeight = true;
        isFocusable = true;
    }

    // ── Fluent API ────────────────────────────────────────────────────────────

    std::shared_ptr<HSLPanelWidget> setData(const HSLData& d) {
        data = d; markNeedsPaint(); return ptr();
    }
    std::shared_ptr<HSLPanelWidget> setData(State<HSLData>& s) {
        data = s.get();
        s.bindProperty(shared_from_this(),
            [](Widget* w, const HSLData& d){
                static_cast<HSLPanelWidget*>(w)->data = d;
            }, false);
        markNeedsPaint();
        return ptr();
    }
    std::shared_ptr<HSLPanelWidget> setActiveTab(HSLTab t) {
        activeTab = t; markNeedsPaint(); return ptr();
    }
    std::shared_ptr<HSLPanelWidget> setTATHue(float hue360) {
        tatHue = hue360; markNeedsPaint(); return ptr();
    }
    std::shared_ptr<HSLPanelWidget> setOnHSLChanged(
            std::function<void(const HSLData&)> cb) {
        onHSLChanged = cb; return ptr();
    }
    std::shared_ptr<HSLPanelWidget> setWidth(int w) {
        width = w; autoWidth = false; markNeedsLayout(); return ptr();
    }

    // Reset a single band
    std::shared_ptr<HSLPanelWidget> resetBand(int bandIdx) {
        if (bandIdx >= 0 && bandIdx < kHSLBandCount) {
            data.bands[bandIdx] = HSLBandValues{};
            notifyChanged();
            markNeedsPaint();
        }
        return ptr();
    }
    // Reset everything
    std::shared_ptr<HSLPanelWidget> resetAll() {
        data.reset();
        notifyChanged();
        markNeedsPaint();
        return ptr();
    }

    // ── Layout ────────────────────────────────────────────────────────────────

    void computeLayout(HDC, const BoxConstraints& c, FontCache&) override {
        width  = c.clampWidth(autoWidth ? c.maxWidth : width);
        height = computeContentHeight();
        height = c.clampHeight(height);
        needsLayout = false;
    }

    // ── Render ────────────────────────────────────────────────────────────────

    void render(HDC hdc, FontCache& fc) override {
        cacheLayout();

        // Background
        {
            HBRUSH br = CreateSolidBrush(bgColor);
            RECT rc = {x, y, x+width, y+height};
            FillRect(hdc, &rc, br);
            DeleteObject(br);
        }

        // Tab strip
        drawTabStrip(hdc, fc);

        // Band rows
        int rowY = y + kTabH + kTabPad;
        for (int i = 0; i < kHSLBandCount; ++i) {
            bool tatHighlight = isTATBand(i);
            drawBandRow(hdc, fc, i, rowY, tatHighlight);
            rowY += rowHeight(i);
        }

        // Border
        {
            HPEN pen = CreatePen(PS_SOLID, 1, borderColor);
            HPEN op  = (HPEN)SelectObject(hdc, pen);
            HBRUSH nb = (HBRUSH)GetStockObject(NULL_BRUSH);
            HBRUSH ob = (HBRUSH)SelectObject(hdc, nb);
            Rectangle(hdc, x, y, x+width, y+height);
            SelectObject(hdc, op); SelectObject(hdc, ob);
            DeleteObject(pen);
        }

        needsPaint = false;
    }

    // ── Mouse ─────────────────────────────────────────────────────────────────

    bool handleMouseDown(int mx, int my) override {
        // Tab click
        int tab = hitTestTab(mx, my);
        if (tab >= 0) {
            activeTab = (HSLTab)tab;
            markNeedsLayout();
            markNeedsPaint();
            return true;
        }
        // Slider hit
        SliderHit hit = hitTestSlider(mx, my);
        if (hit.valid) {
            drag_ = hit;
            dragging_ = true;
            captureMouse();
            applyDrag(mx);
            return true;
        }
        return false;
    }

    bool handleMouseMove(int mx, int /*my*/) override {
        if (!dragging_) return false;
        applyDrag(mx);
        return true;
    }

    bool handleMouseUp(int /*mx*/, int /*my*/) override {
        if (dragging_) { dragging_ = false; releaseMouse(); }
        return false;
    }

    // Double-click on band label → reset that band
    bool handleLButtonDblClk(int mx, int my) {
        int band = hitTestBandLabel(mx, my);
        if (band >= 0) { resetBand(band); return true; }
        return false;
    }

private:
    // ── Layout constants ──────────────────────────────────────────────────────
    static constexpr int kTabH      = 22;
    static constexpr int kTabPad    = 4;
    static constexpr int kLabelW    = 62;
    static constexpr int kSliderH   = 14;
    static constexpr int kRowPad    = 5;   // vertical padding per slider row
    static constexpr int kNumW      = 30;
    static constexpr int kBandPadV  = 6;   // extra space between bands

    // Cached per-slider rect info (filled in cacheLayout)
    struct SliderRect { int sx, sy, sw; }; // left-x, top-y, width of track

    // ── Band metadata ─────────────────────────────────────────────────────────
    struct BandMeta {
        const char* name;
        COLORREF    trackColor;   // saturated band color for track fill
        float       centerHue;   // degrees (for TAT)
        float       halfWidth;
    };
    static constexpr BandMeta kMeta[kHSLBandCount] = {
        {"Red",     RGB(220, 60,  60),   15.f, 30.f},
        {"Orange",  RGB(220,130,  40),   45.f, 22.f},
        {"Yellow",  RGB(200,200,  30),   75.f, 22.f},
        {"Green",   RGB( 50,180,  70),  135.f, 37.f},
        {"Aqua",    RGB( 50,190, 190),  195.f, 37.f},
        {"Blue",    RGB( 60,100, 220),  245.f, 30.f},
        {"Purple",  RGB(160, 60, 200),  290.f, 30.f},
        {"Magenta", RGB(210, 60, 150),  330.f, 22.f},
    };

    // How many slider rows for the active tab?
    int slidersPerBand() const {
        return (activeTab == HSLTab::All) ? 3 : 1;
    }
    int rowHeight(int /*band*/) const {
        return slidersPerBand() * (kSliderH + kRowPad) + kBandPadV + 12;
        // 12 = label row
    }
    int computeContentHeight() const {
        int h = kTabH + kTabPad;
        for (int i = 0; i < kHSLBandCount; ++i) h += rowHeight(i);
        h += 4;
        return h;
    }

    // ── Interaction state ─────────────────────────────────────────────────────
    struct SliderHit {
        bool valid   = false;
        int  band    = 0;
        int  channel = 0; // 0=hue, 1=sat, 2=lum
        int  trackX  = 0;
        int  trackW  = 0;
    };
    SliderHit drag_;
    bool      dragging_ = false;

    std::shared_ptr<HSLPanelWidget> ptr() {
        return std::static_pointer_cast<HSLPanelWidget>(shared_from_this());
    }

    void notifyChanged() {
        if (onHSLChanged) onHSLChanged(data);
    }

    // ── TAT matching ─────────────────────────────────────────────────────────
    bool isTATBand(int i) const {
        if (tatHue < 0.f) return false;
        float diff = std::fabs(tatHue - kMeta[i].centerHue);
        if (diff > 180.f) diff = 360.f - diff;
        return diff < kMeta[i].halfWidth * 1.4f;
    }

    // ── cacheLayout  (no-op here; rects computed on-the-fly in render) ────────
    void cacheLayout() {}

    // ── Tab strip ─────────────────────────────────────────────────────────────
    void drawTabStrip(HDC hdc, FontCache& fc) {
        static const char* kLabels[] = {"Hue","Sat","Lum","All"};
        int tabCount = 4;
        int tw = width / tabCount;
        HFONT hf = fc.getFont(9, FontWeight::Bold);
        HFONT of = (HFONT)SelectObject(hdc, hf);
        SetBkMode(hdc, TRANSPARENT);
        for (int i = 0; i < tabCount; ++i) {
            bool active = ((int)activeTab == i);
            RECT tr = { x + i*tw, y, x + (i+1)*tw, y + kTabH };
            HBRUSH tbr = CreateSolidBrush(active ? RGB(35,35,50) : bgColor);
            FillRect(hdc, &tr, tbr);
            DeleteObject(tbr);
            if (active) {
                // Accent underline
                HPEN ul = CreatePen(PS_SOLID, 2, tabActive);
                HPEN op = (HPEN)SelectObject(hdc, ul);
                MoveToEx(hdc, tr.left+3,  tr.bottom-1, nullptr);
                LineTo  (hdc, tr.right-3, tr.bottom-1);
                SelectObject(hdc, op); DeleteObject(ul);
            }
            SetTextColor(hdc, active ? tabActive : textDim);
            DrawTextA(hdc, kLabels[i], -1, &tr, DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        }
        SelectObject(hdc, of);
        // separator line
        HPEN sep = CreatePen(PS_SOLID, 1, borderColor);
        HPEN op  = (HPEN)SelectObject(hdc, sep);
        MoveToEx(hdc, x,       y+kTabH, nullptr);
        LineTo  (hdc, x+width, y+kTabH);
        SelectObject(hdc, op); DeleteObject(sep);
    }

    // ── Band row ──────────────────────────────────────────────────────────────
    // Each row contains:
    //   • colored band label (left)
    //   • one slider per active channel
    //   • numeric value (right)
    void drawBandRow(HDC hdc, FontCache& fc, int bi, int rowY, bool tatHL) {
        const BandMeta& m = kMeta[bi];
        int rh = rowHeight(bi);

        // TAT glow: subtle left bar
        if (tatHL) {
            HBRUSH glw = CreateSolidBrush(RGB(
                (GetRValue(m.trackColor)*2 + 28*3)/5,
                (GetGValue(m.trackColor)*2 + 28*3)/5,
                (GetBValue(m.trackColor)*2 + 28*3)/5));
            RECT gr = {x, rowY, x+3, rowY+rh};
            FillRect(hdc, &gr, glw);
            DeleteObject(glw);
        }

        // Band label
        HFONT hf = fc.getFont(9, FontWeight::Bold);
        HFONT of = (HFONT)SelectObject(hdc, hf);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, m.trackColor);
        RECT lr = {x+5, rowY+2, x+kLabelW, rowY+14};
        DrawTextA(hdc, m.name, -1, &lr, DT_LEFT|DT_VCENTER|DT_SINGLELINE);
        SelectObject(hdc, of);

        // Determine which channels to show
        int channels[3]; int nCh = 0;
        if (activeTab == HSLTab::All) {
            channels[0]=0; channels[1]=1; channels[2]=2; nCh=3;
        } else {
            channels[0] = (int)activeTab; nCh = 1;
        }

        // Slider track area
        int trackX = x + kLabelW;
        int trackW = width - kLabelW - kNumW - 6;
        int slotY  = rowY + 14;

        for (int ci = 0; ci < nCh; ++ci) {
            int ch = channels[ci];
            float val = getChannelVal(bi, ch);
            drawSlider(hdc, fc, bi, ch, val, trackX, slotY, trackW);
            slotY += kSliderH + kRowPad;
        }

        // Numeric readout: show primary channel value
        int showCh = (activeTab == HSLTab::All) ? 1 : (int)activeTab; // sat by default in All
        float showVal = getChannelVal(bi, showCh);
        int dispVal = (int)std::round(showVal * 100.f);

        hf = fc.getFont(8, FontWeight::Normal);
        of = (HFONT)SelectObject(hdc, hf);
        SetTextColor(hdc, std::abs(showVal) > 0.01f ? textBright : textDim);
        char buf[8]; _snprintf_s(buf, sizeof(buf), _TRUNCATE, "%+d", dispVal);
        RECT nr = {x+width-kNumW-2, rowY+2, x+width-2, rowY+14};
        DrawTextA(hdc, buf, -1, &nr, DT_RIGHT|DT_VCENTER|DT_SINGLELINE);
        SelectObject(hdc, of);

        // Divider
        HPEN dp = CreatePen(PS_SOLID, 1, RGB(38,38,52));
        HPEN op = (HPEN)SelectObject(hdc, dp);
        MoveToEx(hdc, x+4,       rowY+rh-1, nullptr);
        LineTo  (hdc, x+width-4, rowY+rh-1);
        SelectObject(hdc, op); DeleteObject(dp);
    }

    // ── Single slider ─────────────────────────────────────────────────────────
    void drawSlider(HDC hdc, FontCache& fc, int bi, int ch, float val,
                    int tx, int ty, int tw) {
        // Channel label (tiny, left of track)
        static const char* kChLabel[] = {"H","S","L"};
        COLORREF chCol = channelColor(ch);

        HFONT hf = fc.getFont(7, FontWeight::Normal);
        HFONT of = (HFONT)SelectObject(hdc, hf);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, chCol);
        RECT cl = {tx-12, ty, tx, ty+kSliderH};
        DrawTextA(hdc, kChLabel[ch], -1, &cl, DT_RIGHT|DT_VCENTER|DT_SINGLELINE);
        SelectObject(hdc, of);

        // Track background (gradient from dark-band-color to lighter)
        int trackH = 5;
        int trackY = ty + (kSliderH - trackH) / 2;

        // Background track
        {
            HBRUSH br = CreateSolidBrush(RGB(40,40,55));
            RECT tr = {tx, trackY, tx+tw, trackY+trackH};
            FillRect(hdc, &tr, br);
            DeleteObject(br);
        }

        // Filled portion: center = 0, left = negative, right = positive
        int mid = tx + tw/2;
        int thumb = tx + (int)((val * 0.5f + 0.5f) * tw);
        thumb = std::clamp(thumb, tx, tx+tw);
        if (thumb != mid) {
            int fx0 = min(mid, thumb);
            int fx1 = max(mid, thumb);
            COLORREF fillCol = blendColor(kMeta[bi].trackColor, chCol);
            HBRUSH fb = CreateSolidBrush(fillCol);
            RECT fr = {fx0, trackY, fx1, trackY+trackH};
            FillRect(hdc, &fr, fb);
            DeleteObject(fb);
        }

        // Center tick
        {
            HPEN cp = CreatePen(PS_SOLID, 1, RGB(60,60,80));
            HPEN op = (HPEN)SelectObject(hdc, cp);
            MoveToEx(hdc, mid, trackY-1,       nullptr);
            LineTo  (hdc, mid, trackY+trackH+1);
            SelectObject(hdc, op); DeleteObject(cp);
        }

        // Thumb
        {
            bool active = dragging_ && drag_.band==bi && drag_.channel==ch;
            COLORREF tc = active ? RGB(255,220,80) : RGB(210,210,225);
            HBRUSH tb = CreateSolidBrush(tc);
            HPEN   tp = CreatePen(PS_SOLID, 1, RGB(80,80,100));
            HPEN   op = (HPEN)  SelectObject(hdc, tp);
            HBRUSH ob = (HBRUSH)SelectObject(hdc, tb);
            int r = 5;
            Ellipse(hdc, thumb-r, trackY+trackH/2-r,
                         thumb+r, trackY+trackH/2+r);
            SelectObject(hdc, op); SelectObject(hdc, ob);
            DeleteObject(tb); DeleteObject(tp);
        }

        // Track border
        {
            HPEN bp = CreatePen(PS_SOLID, 1, RGB(55,55,70));
            HPEN op = (HPEN)SelectObject(hdc, bp);
            HBRUSH nb = (HBRUSH)GetStockObject(NULL_BRUSH);
            HBRUSH ob = (HBRUSH)SelectObject(hdc, nb);
            Rectangle(hdc, tx, trackY, tx+tw, trackY+trackH);
            SelectObject(hdc, op); SelectObject(hdc, ob);
            DeleteObject(bp);
        }
    }

    // ── Hit testing ───────────────────────────────────────────────────────────

    int hitTestTab(int mx, int my) const {
        if (my < y || my > y+kTabH) return -1;
        int tw = width / 4;
        int slot = (mx - x) / tw;
        return (slot >= 0 && slot < 4) ? slot : -1;
    }

    int hitTestBandLabel(int mx, int my) const {
        int rowY = y + kTabH + kTabPad;
        for (int i = 0; i < kHSLBandCount; ++i) {
            int rh = rowHeight(i);
            if (my >= rowY && my < rowY+14 && mx >= x+5 && mx < x+kLabelW)
                return i;
            rowY += rh;
        }
        return -1;
    }

    SliderHit hitTestSlider(int mx, int my) const {
        int rowY = y + kTabH + kTabPad;
        int trackX = x + kLabelW;
        int trackW = width - kLabelW - kNumW - 6;

        for (int bi = 0; bi < kHSLBandCount; ++bi) {
            int rh = rowHeight(bi);
            if (my >= rowY && my < rowY+rh) {
                int channels[3]; int nCh = 0;
                if (activeTab == HSLTab::All) {
                    channels[0]=0; channels[1]=1; channels[2]=2; nCh=3;
                } else {
                    channels[0] = (int)activeTab; nCh=1;
                }
                int slotY = rowY + 14;
                for (int ci = 0; ci < nCh; ++ci) {
                    int trackY = slotY + (kSliderH-5)/2;
                    if (my >= slotY && my < slotY+kSliderH &&
                        mx >= trackX && mx < trackX+trackW) {
                        SliderHit h;
                        h.valid   = true;
                        h.band    = bi;
                        h.channel = channels[ci];
                        h.trackX  = trackX;
                        h.trackW  = trackW;
                        return h;
                    }
                    slotY += kSliderH + kRowPad;
                }
            }
            rowY += rh;
        }
        return {};
    }

    void applyDrag(int mx) {
        if (!drag_.valid) return;
        float nx = (float)(mx - drag_.trackX) / drag_.trackW;
        float val = std::clamp(nx * 2.f - 1.f, -1.f, 1.f);
        setChannelVal(drag_.band, drag_.channel, val);
        notifyChanged();
        markNeedsPaint();
    }

    // ── Value accessors ───────────────────────────────────────────────────────
    float getChannelVal(int bi, int ch) const {
        const auto& bv = data.bands[bi];
        if (ch == 0) return bv.hue;
        if (ch == 1) return bv.saturation;
        return bv.luminance;
    }
    void setChannelVal(int bi, int ch, float v) {
        auto& bv = data.bands[bi];
        if (ch == 0) bv.hue        = v;
        else if (ch==1) bv.saturation= v;
        else          bv.luminance  = v;
    }

    // ── Color helpers ─────────────────────────────────────────────────────────
    static COLORREF channelColor(int ch) {
        if (ch == 0) return RGB(200,170, 80);  // hue    — gold
        if (ch == 1) return RGB(100,200,120);  // sat    — green
        return              RGB(140,160,210);  // lum    — blue-grey
    }
    static COLORREF blendColor(COLORREF a, COLORREF b) {
        return RGB((GetRValue(a)+GetRValue(b))/2,
                   (GetGValue(a)+GetGValue(b))/2,
                   (GetBValue(a)+GetBValue(b))/2);
    }

    // Placeholder stubs — real implementation hooks into the Widget mouse capture
    void captureMouse()  {}
    void releaseMouse()  {}
};

// ============================================================================
//  FACTORY
// ============================================================================

using HSLPanelWidgetPtr = std::shared_ptr<HSLPanelWidget>;

inline HSLPanelWidgetPtr HSLPanel() {
    return std::make_shared<HSLPanelWidget>();
}
inline HSLPanelWidgetPtr HSLPanel(int w) {
    auto p = std::make_shared<HSLPanelWidget>();
    p->setWidth(w);
    return p;
}

// ============================================================================
//  USAGE EXAMPLES
// ============================================================================
/*

// ── 1. Basic drop-in in an editor panel ──────────────────────────────────────
State<HSLData> hslState(HSLData{}, context);

auto hslWidget = HSLPanel(258)
    ->setData(hslState)
    ->setActiveTab(HSLTab::All)
    ->setOnHSLChanged([&](const HSLData& d) {
        hslState.set(d);
        // Build GPU LUTs and upload to shader
        auto luts = d.buildGPULUTs();
        surface->uploadHSLLUTs(luts.hue.data(), luts.sat.data(), luts.lum.data());
        canvasPtr->redraw();
    });

// ── 2. TAT – wire to image canvas mouse-move ─────────────────────────────────
canvas->onMouseMove = [&](float nx, float ny) {
    float hue = samplePixelHue(nx, ny);   // your function: returns 0..360
    hslWidget->setTATHue(hue);
};

// ── 3. In the reset button handler ───────────────────────────────────────────
resetBtn->setOnTap([&]() {
    hslWidget->resetAll();
    hslState.set(HSLData{});
    // re-upload identity LUTs …
});

// ── 4. Querying LUTs for CPU export ──────────────────────────────────────────
auto raw = hslState.get().buildLUTs();
// raw.hShift[h]  degrees of hue shift for input hue h (0..359)
// raw.sScale[h]  saturation multiplier
// raw.lShift[h]  luminance additive offset

// ── 5. GLSL shader integration ───────────────────────────────────────────────
// Add to kEditFrag (image_editor.hpp):
//
//   uniform sampler2D uHSLHue;   // 360×1 R8
//   uniform sampler2D uHSLSat;   // 360×1 R8
//   uniform sampler2D uHSLLum;   // 360×1 R8
//
//   float applyHSLLUTs(sampler2D lh, sampler2D ls, sampler2D ll, vec3 rgb) {
//       // (inside main(), after saturation/vibrance, before vignette)
//       vec3 hsl = rgb2hsl(c);
//       float hueNorm = hsl.x;                          // 0..1
//       float hs = texture(lh, vec2(hueNorm, 0.5)).r;  // 0..1 encodes –30°..+30°
//       float ss = texture(ls, vec2(hueNorm, 0.5)).r;  // 0..1 encodes 0x..2x
//       float ls2= texture(ll, vec2(hueNorm, 0.5)).r;  // 0..1 encodes –1..+1
//       hsl.x = fract(hsl.x + (hs - 0.5) / 6.0);      // /6 because 60°/360°
//       hsl.y = clamp(hsl.y * ss * 2.0, 0.0, 1.0);
//       hsl.z = clamp(hsl.z + (ls2 - 0.5) * 2.0, 0.0, 1.0);
//       c = hsl2rgb(hsl);
//   }
*/

#endif // FLUX_HSL_PANEL_HPP