#ifndef FLUX_OVERFLOW_HPP
#define FLUX_OVERFLOW_HPP

// ============================================================================
// OVERFLOW DETECTION & RENDERING — Flux UI
//
// Mirrors Flutter's overflow system:
//   - Row  → horizontal overflow
//   - Column → vertical overflow
//   - Container (fixed size) → both axes
//
// Visual output (debug builds):
//   - Yellow/black diagonal stripe bar along the overflowing edge
//   - Red badge with pixel amount ("OVERFLOW 42px →")
//
// Usage:
//   #define FLUX_DEBUG_OVERFLOW   (already on in debug; disable for release)
//   Each widget stores an OverflowInfo; renderer checks it after normal paint.
// ============================================================================


#include <string>
#include "flux_platform.hpp"

// ── Debug gate ───────────────────────────────────────────────────────────────
// Both the visual stripe renderer AND logWarning are compiled in only when
// FLUX_DEBUG is defined (i.e. your Debug configuration).
// In Release builds the entire namespace collapses to empty no-op stubs,
// with zero runtime cost and no dependency on any output stream.
//
// To force-disable even in Debug:  #define FLUX_DEBUG_OVERFLOW 0
// To force-enable  even in Release: #define FLUX_DEBUG_OVERFLOW 1
// ─────────────────────────────────────────────────────────────────────────────
#ifndef FLUX_DEBUG_OVERFLOW
  #ifdef FLUX_DEBUG
    #define FLUX_DEBUG_OVERFLOW 1
  #else
    #define FLUX_DEBUG_OVERFLOW 0
  #endif
#endif

// ============================================================================
// DATA TYPES
// ============================================================================

enum class OverflowAxis { None, Horizontal, Vertical, Both };

struct OverflowInfo {
    OverflowAxis axis      = OverflowAxis::None;
    int          overflowX = 0;  // pixels beyond right edge  (> 0 = overflow)
    int          overflowY = 0;  // pixels beyond bottom edge (> 0 = overflow)

    bool hasOverflow() const { return axis != OverflowAxis::None; }

    void reset() {
        axis      = OverflowAxis::None;
        overflowX = 0;
        overflowY = 0;
    }

    // Called after measuring children vs. our own size.
    void evaluate(int childRight,  int myRight,
                  int childBottom, int myBottom) {
        int ox = childRight  - myRight;
        int oy = childBottom - myBottom;
        overflowX = ox > 0 ? ox : 0;
        overflowY = oy > 0 ? oy : 0;

        if      (overflowX > 0 && overflowY > 0) axis = OverflowAxis::Both;
        else if (overflowX > 0)                   axis = OverflowAxis::Horizontal;
        else if (overflowY > 0)                   axis = OverflowAxis::Vertical;
        else                                       axis = OverflowAxis::None;
    }
};

// ============================================================================
// RENDERER
// ============================================================================

#if FLUX_DEBUG_OVERFLOW

namespace FluxOverflow {

// ---------------------------------------------------------------------------
// Internal: draw the classic Flutter-style diagonal stripe band.
// ---------------------------------------------------------------------------
inline void drawStripeBar(Gdiplus::Graphics &g,
                          Gdiplus::REAL bx, Gdiplus::REAL by,
                          Gdiplus::REAL bw, Gdiplus::REAL bh) {
    // Clip to the bar rect so stripes don't bleed outside.
    g.SetClip(Gdiplus::RectF(bx, by, bw, bh));

    const Gdiplus::Color yellow(220, 255, 220,   0);
    const Gdiplus::Color black (180,   0,   0,   0);

    Gdiplus::SolidBrush yellowBrush(yellow);
    Gdiplus::SolidBrush blackBrush(black);

    // Fill base yellow.
    g.FillRectangle(&yellowBrush, bx, by, bw, bh);

    // Draw diagonal black stripes at 45°.
    const float stripeW  = 6.0f;
    const float stripeGap = 6.0f;
    const float period   = stripeW + stripeGap;
    const float diag     = bw + bh;   // length needed to cover the rect

    Gdiplus::Matrix saved;
    g.GetTransform(&saved);

    // Translate origin to top-left of bar, rotate 45°.
    Gdiplus::Matrix m;
    m.Translate(bx, by);
    m.Rotate(45.0f);
    g.SetTransform(&m);

    for (float pos = -diag; pos < diag * 2; pos += period)
        g.FillRectangle(&blackBrush, pos, -diag, stripeW, diag * 3);

    g.SetTransform(&saved);
    g.ResetClip();
}

// ---------------------------------------------------------------------------
// Internal: draw the red label badge.
// ---------------------------------------------------------------------------
inline void drawBadge(HDC hdc, int bx, int by, const std::wstring &label) {
    // Measure text first.
    HFONT hFont = CreateFontW(-11, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                               DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                               CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                               DEFAULT_PITCH, L"Segoe UI");
    HFONT hOld  = (HFONT)SelectObject(hdc, hFont);

    SIZE sz;
    GetTextExtentPoint32W(hdc, label.c_str(), (int)label.size(), &sz);

    int padX = 4, padY = 2;
    RECT badgeRect = { bx, by,
                       bx + sz.cx + padX * 2,
                       by + sz.cy + padY * 2 };

    // Background: solid red.
    HBRUSH redBrush = CreateSolidBrush(RGB(200, 0, 0));
    FillRect(hdc, &badgeRect, redBrush);
    DeleteObject(redBrush);

    // White text.
    SetTextColor(hdc, RGB(255, 255, 255));
    SetBkMode(hdc, TRANSPARENT);

    RECT textRect = { badgeRect.left  + padX, badgeRect.top    + padY,
                      badgeRect.right - padX, badgeRect.bottom - padY };
    DrawTextW(hdc, label.c_str(), -1, &textRect,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);

    SelectObject(hdc, hOld);
    DeleteObject(hFont);
}

// ---------------------------------------------------------------------------
// Public: render overflow indicators for one widget.
// Call this at the END of a widget's render() after drawing itself/children.
// ---------------------------------------------------------------------------
inline void render(HDC hdc, const OverflowInfo &info,
                   int wx, int wy, int ww, int wh) {
    if (!info.hasOverflow()) return;

    Gdiplus::Graphics g(hdc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeNone);

    const int barThickness = 8;

    const int badgeH = 18;  // approximate badge height for stacking

    // ---- Horizontal overflow: stripe bar on the right edge ----
    if (info.overflowX > 0) {
        Gdiplus::REAL bx = (Gdiplus::REAL)(wx + ww - barThickness);
        Gdiplus::REAL by = (Gdiplus::REAL)wy;
        Gdiplus::REAL bw = (Gdiplus::REAL)barThickness;
        Gdiplus::REAL bh = (Gdiplus::REAL)wh;
        drawStripeBar(g, bx, by, bw, bh);

        // Badge: right edge, above the bottom corner.
        // If vertical also overflows, shift up an extra badge-height so they don't collide.
        int badgeY = wy + wh - barThickness - badgeH
                     - (info.overflowY > 0 ? badgeH + 2 : 0);
        std::wstring label = L"OVERFLOW  " + std::to_wstring(info.overflowX) + L"px \u2192";
        drawBadge(hdc, wx + ww - barThickness - 2, badgeY, label);
    }

    // ---- Vertical overflow: stripe bar on the bottom edge ----
    if (info.overflowY > 0) {
        Gdiplus::REAL bx = (Gdiplus::REAL)wx;
        Gdiplus::REAL by = (Gdiplus::REAL)(wy + wh - barThickness);
        Gdiplus::REAL bw = (Gdiplus::REAL)ww;
        Gdiplus::REAL bh = (Gdiplus::REAL)barThickness;
        drawStripeBar(g, bx, by, bw, bh);

        // Badge: bottom-left, just above the stripe bar.
        std::wstring label = L"OVERFLOW  " + std::to_wstring(info.overflowY) + L"px \u2193";
        drawBadge(hdc, wx + 4, wy + wh - barThickness - badgeH, label);
    }
}

// ---------------------------------------------------------------------------
// Debug-output warning (visible in the VS / debugger Output window).
// Uses OutputDebugStringA — no console, no <iostream>, no std::cerr.
// ---------------------------------------------------------------------------
inline void logWarning(const std::string &widgetType,
                       const OverflowInfo &info,
                       const std::string &widgetId = "") {
    if (!info.hasOverflow()) return;

    const std::string id = widgetId.empty() ? "(unnamed)" : widgetId;

    // Build the full message in one string so it appears as a single block
    // in the Output window rather than interleaved with other debug output.
    std::string msg;
    msg.reserve(512);
    msg += "\n";
    msg += "══════════════════════════════════════════════════════\n";
    msg += "  FLUX OVERFLOW WARNING\n";
    msg += "  Widget : " + widgetType + " [" + id + "]\n";

    if (info.overflowX > 0)
        msg += "  Axis   : HORIZONTAL  (" + std::to_string(info.overflowX) + " px)\n";
    if (info.overflowY > 0)
        msg += "  Axis   : VERTICAL    (" + std::to_string(info.overflowY) + " px)\n";

    msg += "\n";
    msg += "  Fix options:\n";
    msg += "    - Wrap children in Expanded() or Flexible()\n";
    msg += "    - Add scrolling via ScrollWidget\n";
    msg += "    - Increase the parent's fixed size\n";
    msg += "    - Set mainAxisAlignment / crossAlignment appropriately\n";
    msg += "══════════════════════════════════════════════════════\n\n";

    OutputDebugStringA(msg.c_str());
}

} // namespace FluxOverflow

#else  // FLUX_DEBUG_OVERFLOW == 0

namespace FluxOverflow {
    inline void render(HDC, const OverflowInfo &, int, int, int, int) {}
    inline void logWarning(const std::string &, const OverflowInfo &,
                           const std::string & = "") {}
}

#endif // FLUX_DEBUG_OVERFLOW

// ============================================================================
// OVERFLOW-AWARE MEASUREMENT HELPERS
// ============================================================================

// Overflows smaller than this are ignored — filters out font-measurement
// rounding noise and sub-pixel text overruns inside decorative containers.
static constexpr int kOverflowThreshold = 20;

// Call after measuring all children in a Row.
inline OverflowInfo detectRowOverflow(int totalChildWidth, int availableWidth) {
    OverflowInfo info;
    int ox = totalChildWidth - availableWidth;
    if (ox > kOverflowThreshold) {
        info.axis      = OverflowAxis::Horizontal;
        info.overflowX = ox;
    }
    return info;
}

// Call after measuring all children in a Column.
inline OverflowInfo detectColumnOverflow(int totalChildHeight, int availableHeight) {
    OverflowInfo info;
    int oy = totalChildHeight - availableHeight;
    if (oy > kOverflowThreshold) {
        info.axis      = OverflowAxis::Vertical;
        info.overflowY = oy;
    }
    return info;
}

// Call after measuring a single child in a Container with a fixed size.
inline OverflowInfo detectContainerOverflow(int childW, int childH,
                                             int containerW, int containerH) {
    OverflowInfo info;
    int ox = childW - containerW;
    int oy = childH - containerH;
    info.overflowX = ox > kOverflowThreshold ? ox : 0;
    info.overflowY = oy > kOverflowThreshold ? oy : 0;

    if      (info.overflowX > 0 && info.overflowY > 0) info.axis = OverflowAxis::Both;
    else if (info.overflowX > 0)                        info.axis = OverflowAxis::Horizontal;
    else if (info.overflowY > 0)                        info.axis = OverflowAxis::Vertical;
    return info;
}

#endif // FLUX_OVERFLOW_HPP