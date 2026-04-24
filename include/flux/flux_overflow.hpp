#ifndef FLUX_OVERFLOW_HPP
#define FLUX_OVERFLOW_HPP

#include "flux_platform.hpp"
#include "flux_painter.hpp"
#include <string>
#include <algorithm>

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
        axis = OverflowAxis::None; overflowX = 0; overflowY = 0;
    }

    void evaluate(int childRight, int myRight, int childBottom, int myBottom) {
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

namespace FluxOverflow {

// ---------------------------------------------------------------------------
// Stripe bar — yellow base + diagonal black stripes, clamped to [bx,by,bw,bh].
// ---------------------------------------------------------------------------
inline void drawStripeBar(Painter &p, int bx, int by, int bw, int bh) {
    if (bw <= 0 || bh <= 0) return;

    p.fillRect(bx, by, bw, bh, Color(255, 200, 0, 220));

    const int stripeW = 5;
    const int period  = 12;

    for (int cx = 0; cx < bw; ++cx) {
        int runStart = -1;
        for (int cy = 0; cy <= bh; ++cy) {
            bool black = ((cx + cy) % period < stripeW);
            if (black && runStart < 0) {
                runStart = cy;
            } else if (!black && runStart >= 0) {
                p.fillRect(bx + cx, by + runStart, 1, cy - runStart,
                           Color(0, 0, 0, 150));
                runStart = -1;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Solid badge — Flutter style: filled black rect, small white text, no border.
// ---------------------------------------------------------------------------
inline void drawBadge(Painter &p, NativeFont font,
                      int bx, int by, int bw, int bh,
                      const std::string &label) {
    const int padX = 5;
    const int padY = 3;
    int badgeH = bh - padY * 2;
    if (badgeH < 6) return;

    int tw     = static_cast<int>(label.size()) * 6;
    int badgeW = std::min(tw + padX * 2, bw - 4);
    if (badgeW <= 0) return;
    int badgeX = bx + (bw - badgeW) / 2;  // centered in bar

    // Solid black — no alpha, no outline, no border
    p.fillRect(badgeX, by + padY, badgeW, badgeH, Color(0, 0, 0, 255));

    std::wstring wlabel(label.begin(), label.end());
    p.drawText(wlabel,
               badgeX + padX, by + padY,
               badgeW - padX * 2, badgeH,
               font,
               Color(255, 255, 255, 255),
               DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

// ---------------------------------------------------------------------------
// Public — call at the END of render(), after all children.
// ---------------------------------------------------------------------------
inline void render(Painter &p, FontCache &fontCache,
                   const OverflowInfo &info,
                   int wx, int wy, int ww, int wh) {
    if (!info.hasOverflow() || ww <= 0 || wh <= 0) return;

    NativeFont font = fontCache.getFont("Segoe UI", 10, FontWeight::Normal);

    const int thick = 40;  // wide enough for badge text

    if (info.overflowY > 0) {
        int barH = std::min(thick, wh);
        int barY = wy + wh - barH;
        drawStripeBar(p, wx, barY, ww, barH);
        drawBadge(p, font, wx, barY, ww, barH,
                  std::to_string(info.overflowY) + " px");
    }

    if (info.overflowX > 0) {
        int barW = std::min(thick, ww);
        int barX = wx + ww - barW;
        int barH = wh - (info.overflowY > 0 ? std::min(thick, wh) : 0);
        if (barH <= 0) return;
        drawStripeBar(p, barX, wy, barW, barH);
        drawBadge(p, font, barX, wy, barW, barH,
                  std::to_string(info.overflowX) + " px");
    }
}

// ---------------------------------------------------------------------------
// Log — one compact line per overflow axis
// ---------------------------------------------------------------------------
inline void logWarning(const std::string &widgetType,
                       const OverflowInfo &info,
                       const std::string &widgetId = "") {
    if (!info.hasOverflow()) return;
    const std::string id = widgetId.empty() ? "(unnamed)" : widgetId;
    std::string msg = "[FluxUI] Overflow in " + widgetType + " [" + id + "]";
    if (info.overflowX > 0)
        msg += "  X: " + std::to_string(info.overflowX) + "px";
    if (info.overflowY > 0)
        msg += "  Y: " + std::to_string(info.overflowY) + "px";
    msg += "\n";
#ifdef _WIN32
    OutputDebugStringA(msg.c_str());
#else
    fputs(msg.c_str(), stderr);
#endif
}

} // namespace FluxOverflow

// ============================================================================
// MEASUREMENT HELPERS
// ============================================================================

static constexpr int kOverflowThreshold = 0;

inline OverflowInfo detectRowOverflow(int totalChildWidth, int availableWidth) {
    OverflowInfo info;
    int ox = totalChildWidth - availableWidth;
    if (ox > kOverflowThreshold) {
        info.axis = OverflowAxis::Horizontal; info.overflowX = ox;
    }
    return info;
}

inline OverflowInfo detectColumnOverflow(int totalChildHeight, int availableHeight) {
    OverflowInfo info;
    int oy = totalChildHeight - availableHeight;
    if (oy > kOverflowThreshold) {
        info.axis = OverflowAxis::Vertical; info.overflowY = oy;
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