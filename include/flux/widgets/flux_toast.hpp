#ifndef FLUX_TOAST_HPP
#define FLUX_TOAST_HPP

#include "flux_app.hpp"
#include "flux_core.hpp"
#include "flux_overlay_host.hpp"

#include <chrono>
#include <deque>
#include <functional>
#include <string>

// ============================================================================
// TOAST TYPE
// ============================================================================

enum class ToastType { Info, Success, Warning, Error };

// ============================================================================
// TOAST ENTRY  (one queued notification)
// ============================================================================

struct ToastEntry {
    std::string            message;
    std::string            title;         // optional — shown bold above message
    ToastType              type          = ToastType::Info;
    int                    durationMs    = 3000;   // 0 = sticky (no auto-dismiss)
    std::string            actionLabel;            // optional action button label
    std::function<void()>  onAction;               // fired when action clicked
    std::function<void()>  onDismiss;              // fired on any dismiss
};

// ============================================================================
// TOAST POSITION
// ============================================================================

enum class ToastPosition {
    BottomRight,
    BottomCenter,
    BottomLeft,
    TopRight,
    TopCenter,
    TopLeft,
};

// ============================================================================
// NOTIFICATION TOAST WIDGET
// ============================================================================
//
// Zero-size anchor widget — place it anywhere in the tree (typically inside
// Scaffold) and call show() / showEntry() to queue toasts.
//
// Usage:
//   auto toast = Toast()
//       ->setPosition(ToastPosition::BottomRight)
//       ->setMaxVisible(3);
//
//   toast->show("File saved successfully", ToastType::Success);
//
//   toast->showEntry({
//       .message     = "Upload failed",
//       .title       = "Error",
//       .type        = ToastType::Error,
//       .durationMs  = 0,           // sticky
//       .actionLabel = "Retry",
//       .onAction    = [&]{ retry(); },
//   });
// ============================================================================

class ToastWidget : public Widget, public OverlayHost {
public:
    // ── Appearance ────────────────────────────────────────────────────────────
    ToastPosition position_    = ToastPosition::BottomRight;
    int           toastWidth_  = 320;
    int           toastHeight_ = 64;   // per-toast height (grows with title)
    int           spacing_     = 8;    // gap between stacked toasts
    int           marginEdge_  = 20;   // distance from window edge
    int           maxVisible_  = 3;
    int           fontSize_    = 13;
    int           borderRadius_= 6;
    int           shadowOffset_= 3;

    // Per-type accent colours (left bar + icon tint)
    COLORREF colorInfo_    = RGB( 33, 150, 243);
    COLORREF colorSuccess_ = RGB( 76, 175,  80);
    COLORREF colorWarning_ = RGB(255, 152,   0);
    COLORREF colorError_   = RGB(244,  67,  54);

    COLORREF bgColor_      = RGB(255, 255, 255);
    COLORREF textColor_    = RGB( 30,  30,  30);
    COLORREF subtextColor_ = RGB( 90,  90,  90);
    COLORREF borderColor_  = RGB(220, 220, 220);
    COLORREF actionColor_  = RGB( 33, 150, 243);
    COLORREF closeColor_   = RGB(150, 150, 150);

    // ── Constructor ───────────────────────────────────────────────────────────
    ToastWidget() {
        // Zero-size anchor — takes no space in the layout
        width      = 0;
        height     = 0;
        autoWidth  = false;
        autoHeight = false;
    }

    // ── OverlayHost ───────────────────────────────────────────────────────────
    void setScaffold(ScaffoldWidget *s) override { scaffold_ = s; }

    void onDetach() override {
        _dismissAll();
        Widget::onDetach();
    }

    // ── Public API ────────────────────────────────────────────────────────────

    // Convenience overload — just a message
    void show(const std::string &message,
              ToastType          type       = ToastType::Info,
              int                durationMs = 3000) {
        ToastEntry e;
        e.message    = message;
        e.type       = type;
        e.durationMs = durationMs;
        showEntry(e);
    }

    // Full entry — all options
    void showEntry(ToastEntry entry) {
        queue_.push_back(std::move(entry));
        _pump();
    }

    // Dismiss the topmost visible toast
    void dismissTop() {
        if (active_.empty()) return;
        _dismiss(0);
    }

    // Dismiss all
    void dismissAll() { _dismissAll(); }

    // ── Fluent setters ────────────────────────────────────────────────────────

    std::shared_ptr<ToastWidget> setPosition(ToastPosition p) {
        position_ = p; return self_();
    }
    std::shared_ptr<ToastWidget> setToastWidth(int w) {
        toastWidth_ = w; return self_();
    }
    std::shared_ptr<ToastWidget> setToastHeight(int h) {
        toastHeight_ = h; return self_();
    }
    std::shared_ptr<ToastWidget> setMaxVisible(int n) {
        maxVisible_ = n; return self_();
    }
    std::shared_ptr<ToastWidget> setMarginEdge(int m) {
        marginEdge_ = m; return self_();
    }
    std::shared_ptr<ToastWidget> setSpacing(int s) {
        spacing_ = s; return self_();
    }
    std::shared_ptr<ToastWidget> setFontSize(int s) {
        fontSize_ = s; return self_();
    }
    std::shared_ptr<ToastWidget> setColors(COLORREF info, COLORREF success,
                                           COLORREF warning, COLORREF error) {
        colorInfo_ = info; colorSuccess_ = success;
        colorWarning_ = warning; colorError_ = error;
        return self_();
    }
    std::shared_ptr<ToastWidget> setBgColor(COLORREF c) {
        bgColor_ = c; return self_();
    }

    // ── Layout / render (anchor is zero-size) ─────────────────────────────────
    void computeLayout(GraphicsContext &/*ctx*/, const BoxConstraints &, FontCache &) override {
        needsLayout = false;
    }
    void positionChildren(int, int, int, int) override {}
    void render(GraphicsContext &/*ctx*/, FontCache &) override { needsPaint = false; }

    // ── renderPopupContent ────────────────────────────────────────────────────
    // The popup covers the whole window so we can composite multiple toasts.
    // hdc is (winW × winH), pre-cleared to transparent.
    void renderPopupContent(GraphicsContext &ctx, FontCache &fontCache) override {
        if (active_.empty()) return;

        HWND hw = _hwnd();
        int  winW = _winW(), winH = _winH();

        int count = min((int)active_.size(), maxVisible_);

        for (int i = 0; i < count; i++) {
            ActiveToast &at = active_[i];

            // Compute per-toast height (taller if there's a title)
            int tH = _toastH(at.entry);
            int tW = toastWidth_;

            // Stack position
            int tX, tY;
            _computeToastPos(i, tW, tH, winW, winH, tX, tY);

            at.lastDrawX = tX;
            at.lastDrawY = tY;
            at.lastDrawW = tW;
            at.lastDrawH = tH;

            _renderSingleToast(ctx.hdc, fontCache, at, tX, tY, tW, tH);
        }
    }

    // ── Mouse events ──────────────────────────────────────────────────────────

    bool handleMouseDown(int mx, int my) override {
        if (active_.empty()) return false;

        int count = min((int)active_.size(), maxVisible_);
        for (int i = 0; i < count; i++) {
            ActiveToast &at = active_[i];
            int tX = at.lastDrawX, tY = at.lastDrawY;
            int tW = at.lastDrawW, tH = at.lastDrawH;

            // Convert screen popup coords to client (popup covers whole window)
            if (mx < tX || mx >= tX + tW || my < tY || my >= tY + tH)
                continue;

            int relX = mx - tX;
            int relY = my - tY;

            // Close button (top-right 20×20 zone)
            if (relX >= tW - 28 && relX < tW - 8 && relY >= 8 && relY < 28) {
                _dismiss(i);
                return true;
            }

            // Action button (bottom-right zone, only if actionLabel set)
            if (!at.entry.actionLabel.empty()) {
                int btnW = (int)at.entry.actionLabel.size() * (fontSize_ / 2) + 16;
                int btnX = tW - 8 - btnW;
                int btnY = tH - 28;
                if (relX >= btnX && relX < btnX + btnW &&
                    relY >= btnY && relY < btnY + 20) {
                    if (at.entry.onAction) at.entry.onAction();
                    _dismiss(i);
                    return true;
                }
            }

            // Click anywhere else on the toast dismisses it
            _dismiss(i);
            return true;
        }
        return false;
    }

    bool handleMouseMove(int mx, int my) override {
        if (active_.empty()) return false;
        // Pause auto-dismiss timer while hovered
        bool overAny = false;
        int count = min((int)active_.size(), maxVisible_);
        for (int i = 0; i < count; i++) {
            ActiveToast &at = active_[i];
            if (mx >= at.lastDrawX && mx < at.lastDrawX + at.lastDrawW &&
                my >= at.lastDrawY && my < at.lastDrawY + at.lastDrawH) {
                overAny = true;
                at.hovered = true;
            } else {
                at.hovered = false;
            }
        }
        return false; // don't consume — let other widgets see moves
    }

    bool handleTimer(UINT timerId) override {
        if (timerId != tickTimerId_) return false;
        _tick();
        return true;
    }

private:
    // ── Active toast state ────────────────────────────────────────────────────
    struct ActiveToast {
        ToastEntry  entry;
        int         remainingMs  = 0;   // countdown; -1 = sticky
        bool        hovered      = false;
        // Last drawn bounds (client coords within popup = window coords)
        int         lastDrawX    = 0;
        int         lastDrawY    = 0;
        int         lastDrawW    = 0;
        int         lastDrawH    = 0;
    };

    ScaffoldWidget          *scaffold_    = nullptr;
    std::deque<ToastEntry>   queue_;
    std::vector<ActiveToast> active_;
    bool                     popupShown_  = false;
    UINT                     tickTimerId_ = 0;

    // ── Helpers ───────────────────────────────────────────────────────────────

    std::shared_ptr<ToastWidget> self_() {
        return std::static_pointer_cast<ToastWidget>(shared_from_this());
    }

    HWND _hwnd() const {
        auto *ui = FluxUI::getCurrentInstance();
        return ui ? ui->getWindow() : nullptr;
    }

    int _winW() const {
        HWND hw = _hwnd();
        if (!hw) return 800;
        RECT cr; GetClientRect(hw, &cr); return cr.right;
    }
    int _winH() const {
        HWND hw = _hwnd();
        if (!hw) return 600;
        RECT cr; GetClientRect(hw, &cr); return cr.bottom;
    }

    int _toastH(const ToastEntry &e) const {
        int h = toastHeight_;
        if (!e.title.empty())      h += fontSize_ + 4;  // title line
        if (!e.actionLabel.empty()) h += 28;             // action button row
        return h;
    }

    COLORREF _accentColor(ToastType t) const {
        switch (t) {
        case ToastType::Success: return colorSuccess_;
        case ToastType::Warning: return colorWarning_;
        case ToastType::Error:   return colorError_;
        default:                 return colorInfo_;
        }
    }

    // Icon character per type (Segoe MDL2 Assets)
    const wchar_t *_icon(ToastType t) const {
        switch (t) {
        case ToastType::Success: return L"\uE73E"; // CheckMark
        case ToastType::Warning: return L"\uE7BA"; // Warning
        case ToastType::Error:   return L"\uEA39"; // ErrorBadge
        default:                 return L"\uE946"; // Info
        }
    }

    // ── Position calculation ──────────────────────────────────────────────────

    void _computeToastPos(int stackIdx, int tW, int tH,
                          int winW, int winH,
                          int &outX, int &outY) const {
        // Horizontal
        switch (position_) {
        case ToastPosition::BottomRight:
        case ToastPosition::TopRight:
            outX = winW - tW - marginEdge_;
            break;
        case ToastPosition::BottomLeft:
        case ToastPosition::TopLeft:
            outX = marginEdge_;
            break;
        default: // Center variants
            outX = (winW - tW) / 2;
            break;
        }

        // Vertical — stack grows inward from the chosen edge
        bool isBottom = (position_ == ToastPosition::BottomRight  ||
                         position_ == ToastPosition::BottomCenter ||
                         position_ == ToastPosition::BottomLeft);

        if (isBottom) {
            // Stack upward: index 0 is closest to edge
            outY = winH - marginEdge_ - tH - stackIdx * (tH + spacing_);
        } else {
            outY = marginEdge_ + stackIdx * (tH + spacing_);
        }
    }

    // ── Rendering ─────────────────────────────────────────────────────────────

    void _renderSingleToast(HDC hdc, FontCache &fontCache,
                             const ActiveToast &at,
                             int tX, int tY, int tW, int tH) const {
        COLORREF accent = _accentColor(at.entry.type);

        // ── Shadow ────────────────────────────────────────────────────────────
        {
            HRGN rg = CreateRoundRectRgn(
                tX + shadowOffset_, tY + shadowOffset_,
                tX + tW + shadowOffset_, tY + tH + shadowOffset_,
                borderRadius_ * 2, borderRadius_ * 2);
            HBRUSH sb = CreateSolidBrush(RGB(0, 0, 0));
            // Soften shadow via AlphaBlend on a temp DC
            HDC     tmpDC  = CreateCompatibleDC(hdc);
            HBITMAP tmpBmp = CreateCompatibleBitmap(hdc, tW, tH);
            HBITMAP tmpOld = (HBITMAP)SelectObject(tmpDC, tmpBmp);
            HBRUSH  tb     = CreateSolidBrush(RGB(0, 0, 0));
            RECT    tr     = {0, 0, tW, tH};
            FillRect(tmpDC, &tr, tb);
            DeleteObject(tb);
            BLENDFUNCTION bf = {AC_SRC_OVER, 0, 40, 0};
            AlphaBlend(hdc,
                       tX + shadowOffset_, tY + shadowOffset_, tW, tH,
                       tmpDC, 0, 0, tW, tH, bf);
            SelectObject(tmpDC, tmpOld);
            DeleteObject(tmpBmp);
            DeleteDC(tmpDC);
            DeleteObject(rg);
            DeleteObject(sb);
        }

        // ── Background + border ───────────────────────────────────────────────
        {
            HPEN   pen    = CreatePen(PS_SOLID, 1, borderColor_);
            HBRUSH brush  = CreateSolidBrush(bgColor_);
            HPEN   oldPen = (HPEN)  SelectObject(hdc, pen);
            HBRUSH oldBrush=(HBRUSH)SelectObject(hdc, brush);
            RoundRect(hdc, tX, tY, tX + tW, tY + tH,
                      borderRadius_ * 2, borderRadius_ * 2);
            SelectObject(hdc, oldBrush); SelectObject(hdc, oldPen);
            DeleteObject(brush); DeleteObject(pen);
        }

        // ── Accent left bar ───────────────────────────────────────────────────
        {
            HBRUSH ab  = CreateSolidBrush(accent);
            HRGN   rg  = CreateRoundRectRgn(tX, tY, tX + 5, tY + tH,
                                             borderRadius_ * 2, borderRadius_ * 2);
            FillRgn(hdc, rg, ab);
            // Square off the right half of the rounded bar so it butts the body
            RECT sq = {tX + 3, tY, tX + 5, tY + tH};
            HBRUSH sqb = CreateSolidBrush(accent);
            FillRect(hdc, &sq, sqb);
            DeleteObject(sqb);
            DeleteObject(rg); DeleteObject(ab);
        }

        // ── Icon (Segoe MDL2) ─────────────────────────────────────────────────
        {
            HFONT  iconFont = fontCache.getFont("Segoe MDL2 Assets",
                                                fontSize_ + 2, FontWeight::Normal);
            HFONT  oldFont  = (HFONT)SelectObject(hdc, iconFont);
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, accent);
            RECT ir = {tX + 14, tY + (tH / 2) - (fontSize_ + 2) / 2 - 2,
                       tX + 14 + fontSize_ + 8,
                       tY + (tH / 2) + (fontSize_ + 2) / 2 + 2};
            DrawTextW(hdc, _icon(at.entry.type), 1, &ir,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            SelectObject(hdc, oldFont);
        }

        // ── Close button (×) ──────────────────────────────────────────────────
        {
            HFONT  cf    = fontCache.getFont(11, FontWeight::Normal);
            HFONT  oldCF = (HFONT)SelectObject(hdc, cf);
            SetTextColor(hdc, closeColor_);
            RECT cr2 = {tX + tW - 28, tY + 8, tX + tW - 8, tY + 28};
            DrawTextA(hdc, "\xC3\x97", -1, &cr2,  // UTF-8 × (fallback)
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            // Prefer the × glyph as a simple text
            DrawTextA(hdc, "x", -1, &cr2,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            SelectObject(hdc, oldCF);
        }

        SetBkMode(hdc, TRANSPARENT);

        int textLeft = tX + 14 + fontSize_ + 12; // after accent bar + icon
        int textRight = tX + tW - 36;             // before close button
        int curY = tY + 10;

        // ── Title (bold, optional) ────────────────────────────────────────────
        if (!at.entry.title.empty()) {
            HFONT  bf2   = fontCache.getFont(fontSize_, FontWeight::Bold);
            HFONT  oldBF = (HFONT)SelectObject(hdc, bf2);
            SetTextColor(hdc, textColor_);
            RECT tr2 = {textLeft, curY, textRight, curY + fontSize_ + 4};
            DrawTextA(hdc, at.entry.title.c_str(), -1, &tr2,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            SelectObject(hdc, oldBF);
            curY += fontSize_ + 6;
        }

        // ── Message ───────────────────────────────────────────────────────────
        {
            HFONT  nf    = fontCache.getFont(fontSize_, FontWeight::Normal);
            HFONT  oldNF = (HFONT)SelectObject(hdc, nf);
            SetTextColor(hdc, at.entry.title.empty() ? textColor_ : subtextColor_);

            int msgH = at.entry.actionLabel.empty()
                ? tY + tH - curY - 10
                : tY + tH - curY - 36;   // leave room for action button

            RECT mr = {textLeft, curY, textRight, curY + msgH};
            DrawTextA(hdc, at.entry.message.c_str(), -1, &mr,
                      DT_LEFT | DT_TOP | DT_WORDBREAK | DT_END_ELLIPSIS);
            SelectObject(hdc, oldNF);
        }

        // ── Action button (optional) ──────────────────────────────────────────
        if (!at.entry.actionLabel.empty()) {
            HFONT  af    = fontCache.getFont(fontSize_ - 1, FontWeight::Bold);
            HFONT  oldAF = (HFONT)SelectObject(hdc, af);
            SetTextColor(hdc, accent);

            int btnW = (int)at.entry.actionLabel.size() *
                       (fontSize_ / 2) + 16;
            RECT br = {tX + tW - 8 - btnW, tY + tH - 28,
                       tX + tW - 8,         tY + tH - 8};
            DrawTextA(hdc, at.entry.actionLabel.c_str(), -1, &br,
                      DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
            SelectObject(hdc, oldAF);
        }

        // ── Progress bar (auto-dismiss countdown) ─────────────────────────────
        if (at.remainingMs > 0 && at.entry.durationMs > 0) {
            float pct = (float)at.remainingMs / (float)at.entry.durationMs;
            int   barW = (int)((tW - 2) * pct);
            HBRUSH pb = CreateSolidBrush(accent);
            RECT   pr = {tX + 1, tY + tH - 3, tX + 1 + barW, tY + tH - 1};
            FillRect(hdc, &pr, pb);
            DeleteObject(pb);
        }
    }

    // ── Timer / lifecycle ─────────────────────────────────────────────────────

    void _pump() {
        // Move queued entries into active_ up to maxVisible_
        while (!queue_.empty() && (int)active_.size() < maxVisible_) {
            ActiveToast at;
            at.entry       = queue_.front();
            at.remainingMs = at.entry.durationMs > 0 ? at.entry.durationMs : -1;
            queue_.pop_front();
            active_.push_back(std::move(at));
        }

        if (active_.empty()) return;

        // Ensure popup is shown
        if (!popupShown_) {
            HWND hw = _hwnd();
            if (hw) {
                POINT sc = fluxClientToScreen(0, 0);
                auto *ui = FluxUI::getCurrentInstance();
                showPopup(hw, sc.x, sc.y, _winW(), _winH(),
                          ui->getFontCache());
                popupShown_ = true;

                if (scaffold_) {
                    scaffold_->addOverlay(this,
                        [](HDC, FontCache &) {}, 75); // between Tooltip(50) and Dropdown(100)
                }
            }
        }

        // Start tick timer if not running (fires every 100ms)
        if (tickTimerId_ == 0) {
            auto *ui = FluxUI::getCurrentInstance();
            if (ui) {
                tickTimerId_ = ui->setInterval(100, [this] { _tick(); });
            }
        }

        _refresh();
    }

    void _tick() {
        if (active_.empty()) return;

        bool anyDismissed = false;
        for (int i = (int)active_.size() - 1; i >= 0; i--) {
            ActiveToast &at = active_[i];
            if (at.remainingMs < 0) continue;   // sticky
            if (at.hovered)        continue;     // paused while hovered
            at.remainingMs -= 100;
            if (at.remainingMs <= 0) {
                _dismiss(i);
                anyDismissed = true;
            }
        }

        if (!anyDismissed) _refresh();

        // Try to show more from queue
        _pump();
    }

    void _dismiss(int index) {
        if (index < 0 || index >= (int)active_.size()) return;
        if (active_[index].entry.onDismiss)
            active_[index].entry.onDismiss();
        active_.erase(active_.begin() + index);

        if (active_.empty() && queue_.empty()) {
            _teardown();
        } else {
            _pump();   // may pull next from queue
            _refresh();
        }
    }

    void _dismissAll() {
        for (auto &at : active_)
            if (at.entry.onDismiss) at.entry.onDismiss();
        active_.clear();
        queue_.clear();
        _teardown();
    }

    void _teardown() {
        if (tickTimerId_ != 0) {
            auto *ui = FluxUI::getCurrentInstance();
            if (ui) ui->clearInterval(tickTimerId_);
            tickTimerId_ = 0;
        }
        if (popupShown_) {
            hidePopup();
            popupShown_ = false;
            if (scaffold_) scaffold_->removeOverlay(this);
        }
    }

    void _refresh() {
        if (!popupShown_ || !popupVisible()) return;
        auto *ui = FluxUI::getCurrentInstance();
        if (ui) refreshPopup(ui->getFontCache());
    }
};

// ============================================================================
// FACTORY
// ============================================================================

using ToastWidgetPtr = std::shared_ptr<ToastWidget>;

inline ToastWidgetPtr Toast() {
    return std::make_shared<ToastWidget>();
}

#endif // FLUX_TOAST_HPP