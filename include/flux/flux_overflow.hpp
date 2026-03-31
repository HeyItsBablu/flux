#ifndef FLUX_OVERFLOW_HPP
#define FLUX_OVERFLOW_HPP

#include "flux_platform.hpp"
#include "flux_painter.hpp"
#include <string>

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
    int          overflowX = 0;
    int          overflowY = 0;

    bool hasOverflow() const { return axis != OverflowAxis::None; }

    void reset() {
        axis      = OverflowAxis::None;
        overflowX = 0;
        overflowY = 0;
    }

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
// Internal: draw the diagonal stripe bar using Painter primitives.
// Works on both Win32 and Linux — no raw HDC/GDI+ required.
// ---------------------------------------------------------------------------
inline void drawStripeBar(Painter &p,
                          int bx, int by, int bw, int bh) {
    // Base yellow fill
    p.fillRect(bx, by, bw, bh, Color(255, 220, 0, 220));

    // Diagonal black stripes — approximated as thin rects at 45°.
    // We clip by only drawing within [bx, by, bw, bh].
    p.pushClipRect(bx, by, bw, bh);

    const int stripeW  = 6;
    const int period   = 12;   // stripeW + gap
    const int diag     = bw + bh;

    for (int pos = -diag; pos < diag * 2; pos += period) {
        // Each stripe is a thin parallelogram drawn as a polyline poly fill.
        // Simple approach: draw a 1px-wide line at 45° across the bar.
        // For a bolder stripe, draw several adjacent lines.
        for (int t = 0; t < stripeW; ++t) {
            int sx = bx + pos + t;
            p.drawLine(sx, by, sx + bh, by + bh,
                       Color(0, 0, 0, 180), 1);
        }
    }

    p.popClipRect();
}

// ---------------------------------------------------------------------------
// Internal: draw the red badge label.
// Uses Painter::fillRect + drawTextA — fully cross-platform.
// ---------------------------------------------------------------------------
inline void drawBadge(Painter &p, int bx, int by,
                      const std::string &label, NativeFont font) {
    const int padX = 4, padY = 2;

    // Measure text so the badge sizes itself correctly.
    int tw = 0, th = 0;
    p.measureText(std::wstring(label.begin(), label.end()), font, tw, th);

    int bw = tw + padX * 2;
    int bh = th + padY * 2;

    // Red background
    p.fillRect(bx, by, bw, bh, Color(200, 0, 0, 255));

    // White label
    p.drawTextA(label,
                bx + padX, by + padY, tw, th,
                font,
                Color(255, 255, 255, 255),
                DT_LEFT | DT_SINGLELINE);
}

// ---------------------------------------------------------------------------
// Public: render overflow indicators for one widget.
// Call at the END of a widget's render(), after drawing itself/children.
// ---------------------------------------------------------------------------
inline void render(Painter &p, NativeFont font,
                   const OverflowInfo &info,
                   int wx, int wy, int ww, int wh) {
    if (!info.hasOverflow()) return;

    const int barThickness = 8;
    const int badgeH       = 18;

    // ── Horizontal overflow: stripe bar on the right edge ────────────────
    if (info.overflowX > 0) {
        int bx = wx + ww - barThickness;
        drawStripeBar(p, bx, wy, barThickness, wh);

        int badgeY = wy + wh - barThickness - badgeH
                     - (info.overflowY > 0 ? badgeH + 2 : 0);
        std::string label = "OVERFLOW  "
                          + std::to_string(info.overflowX)
                          + "px >";
        drawBadge(p, bx - 2, badgeY, label, font);
    }

    // ── Vertical overflow: stripe bar on the bottom edge ─────────────────
    if (info.overflowY > 0) {
        int by = wy + wh - barThickness;
        drawStripeBar(p, wx, by, ww, barThickness);

        std::string label = "OVERFLOW  "
                          + std::to_string(info.overflowY)
                          + "px v";
        drawBadge(p, wx + 4, by - badgeH, label, font);
    }
}

// ---------------------------------------------------------------------------
// Debug warning — Win32 uses OutputDebugStringA, Linux uses stderr.
// ---------------------------------------------------------------------------
inline void logWarning(const std::string &widgetType,
                       const OverflowInfo &info,
                       const std::string &widgetId = "") {
    if (!info.hasOverflow()) return;

    const std::string id = widgetId.empty() ? "(unnamed)" : widgetId;

    std::string msg;
    msg.reserve(512);
    msg += "\n";
    msg += "======================================================\n";
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
    msg += "======================================================\n\n";

#ifdef _WIN32
    OutputDebugStringA(msg.c_str());
#else
    fputs(msg.c_str(), stderr);
#endif
}

} // namespace FluxOverflow

#else  // FLUX_DEBUG_OVERFLOW == 0

namespace FluxOverflow {
    inline void render(Painter &, NativeFont,
                       const OverflowInfo &, int, int, int, int) {}
    inline void logWarning(const std::string &, const OverflowInfo &,
                           const std::string & = "") {}
}

#endif // FLUX_DEBUG_OVERFLOW

// ============================================================================
// OVERFLOW-AWARE MEASUREMENT HELPERS  (unchanged)
// ============================================================================

static constexpr int kOverflowThreshold = 20;

inline OverflowInfo detectRowOverflow(int totalChildWidth, int availableWidth) {
    OverflowInfo info;
    int ox = totalChildWidth - availableWidth;
    if (ox > kOverflowThreshold) {
        info.axis      = OverflowAxis::Horizontal;
        info.overflowX = ox;
    }
    return info;
}

inline OverflowInfo detectColumnOverflow(int totalChildHeight, int availableHeight) {
    OverflowInfo info;
    int oy = totalChildHeight - availableHeight;
    if (oy > kOverflowThreshold) {
        info.axis      = OverflowAxis::Vertical;
        info.overflowY = oy;
    }
    return info;
}

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