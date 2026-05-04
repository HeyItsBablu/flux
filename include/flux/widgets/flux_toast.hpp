#ifndef FLUX_TOAST_HPP
#define FLUX_TOAST_HPP

#include "../flux_app.hpp"
#include "../flux_core.hpp"
#include "../flux_overlay_host.hpp"

#include <chrono>
#include <deque>
#include <functional>
#include <string>

// ============================================================================
// TOAST TYPE
// ============================================================================

enum class ToastType { Info, Success, Warning, Error };

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
// TOAST ENTRY
// ============================================================================

struct ToastEntry {
    std::string           message;
    std::string           title;       // optional bold heading
    ToastType             type        = ToastType::Info;
    int                   durationMs  = 3000;  // 0 = sticky
    std::string           actionLabel;
    std::function<void()> onAction;
    std::function<void()> onDismiss;
};

// ============================================================================
// TOAST WIDGET
// ============================================================================

class ToastWidget : public Widget, public OverlayHost {
public:
    // ── Appearance ────────────────────────────────────────────────────────
    ToastPosition position_     = ToastPosition::BottomRight;
    int toastWidth_             = 320;
    int toastHeight_            = 64;
    int spacing_                = 8;
    int marginEdge_             = 20;
    int maxVisible_             = 3;
    int fontSize_               = 13;
    int borderRadius_           = 6;
    int shadowOffset_           = 3;

    Color colorInfo_    = Color::fromRGB(33,  150, 243);
    Color colorSuccess_ = Color::fromRGB(76,  175,  80);
    Color colorWarning_ = Color::fromRGB(255, 152,   0);
    Color colorError_   = Color::fromRGB(244,  67,  54);

    Color bgColor_      = Color::fromRGB(255, 255, 255);
    Color textColor_    = Color::fromRGB( 30,  30,  30);
    Color subtextColor_ = Color::fromRGB( 90,  90,  90);
    Color borderColor_  = Color::fromRGB(220, 220, 220);
    Color actionColor_  = Color::fromRGB( 33, 150, 243);
    Color closeColor_   = Color::fromRGB(150, 150, 150);

    // ── Constructor ───────────────────────────────────────────────────────
    ToastWidget() {
        width      = 0;
        height     = 0;
        autoWidth  = false;
        autoHeight = false;
    }

    // ── OverlayHost interface ─────────────────────────────────────────────
    // scaffold_ kept for compatibility but not relied on — we use
    // getRootScaffold() lazily at show time, same pattern as DialogWidget.
    void setScaffold(ScaffoldWidget *s) override { scaffold_ = s; }

    void onDetach() override {
        _dismissAll();
        Widget::onDetach();
    }

    // ── Public API ────────────────────────────────────────────────────────
    void show(const std::string &message,
              ToastType type      = ToastType::Info,
              int       durationMs = 3000) {
        ToastEntry e;
        e.message    = message;
        e.type       = type;
        e.durationMs = durationMs;
        showEntry(e);
    }

    void showEntry(ToastEntry entry) {
        queue_.push_back(std::move(entry));
        _pump();
    }

    void dismissTop() {
        if (!active_.empty()) _dismiss(0);
    }

    void dismissAll() { _dismissAll(); }

    // ── Fluent setters ────────────────────────────────────────────────────
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
    std::shared_ptr<ToastWidget> setColors(Color info, Color success,
                                           Color warning, Color error) {
        colorInfo_    = info;
        colorSuccess_ = success;
        colorWarning_ = warning;
        colorError_   = error;
        return self_();
    }
    std::shared_ptr<ToastWidget> setBgColor(Color c) {
        bgColor_ = c; return self_();
    }

    // ── Layout / render (zero-size anchor) ───────────────────────────────
    void computeLayout(GraphicsContext &, const BoxConstraints &,
                       FontCache &) override {
        needsLayout = false;
    }
    void positionChildren(int, int, int, int) override {}
    void render(GraphicsContext &, FontCache &) override {
        needsPaint = false;
    }

    // ── renderPopupContent ────────────────────────────────────────────────
    void renderPopupContent(GraphicsContext &ctx,
                            FontCache       &fontCache) override {
        if (active_.empty()) return;

        int winW  = _winW();
        int winH  = _winH();
        int count = std::min((int)active_.size(), maxVisible_);

        for (int i = 0; i < count; i++) {
            ActiveToast &at = active_[i];
            int tH = _toastH(at.entry);
            int tW = toastWidth_;

            _computeToastPos(i, tW, tH, winW, winH,
                             at.lastDrawX, at.lastDrawY);
            at.lastDrawW = tW;
            at.lastDrawH = tH;

            _renderSingleToast(ctx, fontCache, at,
                               at.lastDrawX, at.lastDrawY, tW, tH);
        }
    }

    // ── Mouse events ──────────────────────────────────────────────────────
    bool handleMouseDown(int mx, int my) override {
        if (active_.empty()) return false;

        int count = std::min((int)active_.size(), maxVisible_);
        for (int i = 0; i < count; i++) {
            ActiveToast &at = active_[i];
            int tX = at.lastDrawX, tY = at.lastDrawY;
            int tW = at.lastDrawW, tH = at.lastDrawH;

            if (mx < tX || mx >= tX + tW || my < tY || my >= tY + tH)
                continue;

            int relX = mx - tX;
            int relY = my - tY;

            // Close button — top-right 20×20 zone
            if (relX >= tW - 28 && relX < tW - 8 &&
                relY >=       8 && relY <      28) {
                _dismiss(i);
                return true;
            }

            // Action button
            if (!at.entry.actionLabel.empty()) {
                int btnW = (int)at.entry.actionLabel.size() *
                           (fontSize_ / 2) + 16;
                int btnX = tW - 8 - btnW;
                int btnY = tH - 28;
                if (relX >= btnX && relX < btnX + btnW &&
                    relY >= btnY && relY < btnY + 20) {
                    if (at.entry.onAction) at.entry.onAction();
                    _dismiss(i);
                    return true;
                }
            }

            // Click anywhere else dismisses
            _dismiss(i);
            return true;
        }
        return false;
    }

    bool handleMouseMove(int mx, int my) override {
        if (active_.empty()) return false;
        int count = std::min((int)active_.size(), maxVisible_);
        for (int i = 0; i < count; i++) {
            ActiveToast &at = active_[i];
            at.hovered = (mx >= at.lastDrawX &&
                          mx <  at.lastDrawX + at.lastDrawW &&
                          my >= at.lastDrawY &&
                          my <  at.lastDrawY + at.lastDrawH);
        }
        return false; // don't consume mouse moves
    }

private:
    // ── Active toast state ────────────────────────────────────────────────
    struct ActiveToast {
        ToastEntry entry;
        int  remainingMs = 0;   // -1 = sticky
        bool hovered     = false;
        int  lastDrawX   = 0;
        int  lastDrawY   = 0;
        int  lastDrawW   = 0;
        int  lastDrawH   = 0;
    };

    ScaffoldWidget         *scaffold_    = nullptr;
    std::deque<ToastEntry>  queue_;
    std::vector<ActiveToast> active_;
    bool    popupShown_  = false;
    TimerID tickTimerId_ = 0;

    // ── Helpers ───────────────────────────────────────────────────────────
    std::shared_ptr<ToastWidget> self_() {
        return std::static_pointer_cast<ToastWidget>(shared_from_this());
    }

    // Lazy scaffold lookup — never relies on setScaffold being called
    ScaffoldWidget *_scaffold() const {
        if (scaffold_) return scaffold_;
        auto *ui = FluxUI::getCurrentInstance();
        return ui ? ui->getRootScaffold() : nullptr;
    }

    int _winW() const {
        auto *ui = FluxUI::getCurrentInstance();
        return ui ? ui->getClientSize().width : 800;
    }
    int _winH() const {
        auto *ui = FluxUI::getCurrentInstance();
        return ui ? ui->getClientSize().height : 600;
    }

    int _toastH(const ToastEntry &e) const {
        int h = toastHeight_;
        if (!e.title.empty())       h += fontSize_ + 4;
        if (!e.actionLabel.empty()) h += 28;
        return h;
    }

    Color _accentColor(ToastType t) const {
        switch (t) {
        case ToastType::Success: return colorSuccess_;
        case ToastType::Warning: return colorWarning_;
        case ToastType::Error:   return colorError_;
        default:                 return colorInfo_;
        }
    }

    const wchar_t *_icon(ToastType t) const {
        switch (t) {
        case ToastType::Success: return L"\uE73E";
        case ToastType::Warning: return L"\uE7BA";
        case ToastType::Error:   return L"\uEA39";
        default:                 return L"\uE946";
        }
    }

    // ── Position calculation ──────────────────────────────────────────────
    void _computeToastPos(int stackIdx, int tW, int tH,
                          int winW, int winH,
                          int &outX, int &outY) const {
        switch (position_) {
        case ToastPosition::BottomRight:
        case ToastPosition::TopRight:
            outX = winW - tW - marginEdge_;
            break;
        case ToastPosition::BottomLeft:
        case ToastPosition::TopLeft:
            outX = marginEdge_;
            break;
        default:
            outX = (winW - tW) / 2;
            break;
        }

        bool isBottom = (position_ == ToastPosition::BottomRight  ||
                         position_ == ToastPosition::BottomCenter ||
                         position_ == ToastPosition::BottomLeft);
        if (isBottom)
            outY = winH - marginEdge_ - tH - stackIdx * (tH + spacing_);
        else
            outY = marginEdge_ + stackIdx * (tH + spacing_);
    }

    // ── Single toast render ───────────────────────────────────────────────
    void _renderSingleToast(GraphicsContext &ctx, FontCache &fontCache,
                            const ActiveToast &at,
                            int tX, int tY, int tW, int tH) const {
        Color   accent = _accentColor(at.entry.type);
        Painter painter(ctx);

        // Shadow
        painter.fillRectAlpha(tX + shadowOffset_, tY + shadowOffset_,
                              tW, tH,
                              Color::fromRGBA(0, 0, 0, 40));

        // Background + border (GDI RoundRect — sharp but lightweight)
        painter.fillRoundedRectGDI(tX, tY, tW, tH,
                                   borderRadius_ * 2,
                                   bgColor_, borderColor_, 1);

        // Accent left bar
        painter.fillRoundedRegion(tX, tY, 5, tH,
                                  borderRadius_ * 2, accent);
        painter.fillRect(tX + 3, tY, 2, tH, accent); // square off right edge

        // Icon (Segoe MDL2 Assets)
        {
            NativeFont iconFont = fontCache.getFont(
#ifdef _WIN32
                "Segoe MDL2 Assets",
#else
                "Sans",
#endif
                fontSize_ + 2, FontWeight::Normal);
            int iconY = tY + (tH / 2) - (fontSize_ + 2) / 2 - 2;
            painter.drawText(std::wstring(_icon(at.entry.type)),
                             tX + 14, iconY,
                             fontSize_ + 8, fontSize_ + 4,
                             iconFont, accent,
                             DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }

        // Close button
        {
            NativeFont cf = fontCache.getFont(11, FontWeight::Normal);
            painter.drawTextA("x",
                              tX + tW - 28, tY + 8, 20, 20,
                              cf, closeColor_,
                              DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }

        int textLeft = tX + 14 + fontSize_ + 12;
        int textRight = tX + tW - 36;
        int curY      = tY + 10;

        // Title (optional)
        if (!at.entry.title.empty()) {
            NativeFont bf = fontCache.getFont(fontSize_, FontWeight::Bold);
            painter.drawTextA(at.entry.title,
                              textLeft, curY,
                              textRight - textLeft, fontSize_ + 4,
                              bf, textColor_,
                              DT_LEFT | DT_VCENTER | DT_SINGLELINE |
                              DT_END_ELLIPSIS);
            curY += fontSize_ + 6;
        }

        // Message
        {
            NativeFont nf = fontCache.getFont(fontSize_, FontWeight::Normal);
            Color mc = at.entry.title.empty() ? textColor_ : subtextColor_;
            int msgH = at.entry.actionLabel.empty()
                     ? tY + tH - curY - 10
                     : tY + tH - curY - 36;
            painter.drawTextA(at.entry.message,
                              textLeft, curY,
                              textRight - textLeft, msgH,
                              nf, mc,
                              DT_LEFT | DT_TOP | DT_WORDBREAK |
                              DT_END_ELLIPSIS);
        }

        // Action button (optional)
        if (!at.entry.actionLabel.empty()) {
            NativeFont af = fontCache.getFont(fontSize_ - 1, FontWeight::Bold);
            int btnW = (int)at.entry.actionLabel.size() *
                       (fontSize_ / 2) + 16;
            painter.drawTextA(at.entry.actionLabel,
                              tX + tW - 8 - btnW, tY + tH - 28,
                              btnW, 20,
                              af, accent,
                              DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        }

        // Progress bar
        if (at.remainingMs > 0 && at.entry.durationMs > 0) {
            float pct  = (float)at.remainingMs / (float)at.entry.durationMs;
            int   barW = (int)((tW - 2) * pct);
            painter.fillRect(tX + 1, tY + tH - 3, barW, 2, accent);
        }
    }

    // ── Timer / lifecycle ─────────────────────────────────────────────────
    void _pump() {
        while (!queue_.empty() &&
               (int)active_.size() < maxVisible_) {
            ActiveToast at;
            at.entry       = queue_.front();
            at.remainingMs = at.entry.durationMs > 0
                           ? at.entry.durationMs : -1;
            queue_.pop_front();
            active_.push_back(std::move(at));
        }

        if (active_.empty()) return;

        if (!popupShown_) {
            auto *ui = FluxUI::getCurrentInstance();
            if (ui) {
                NativeWindow hw = ui->getWindow();
                auto sc = ui->clientToScreen(0, 0);
                showPopup(hw, sc.x, sc.y, _winW(), _winH(),
                          ui->getFontCache());
                popupShown_ = true;


                if (auto *s = _scaffold())
                    s->addOverlayHitTarget(this, 75);
            }
        }

        if (tickTimerId_ == 0) {
            auto *ui = FluxUI::getCurrentInstance();
            if (ui)
                tickTimerId_ = ui->setInterval(100, [this] { _tick(); });
        }

        _refresh();
    }

    void _tick() {
        if (active_.empty()) return;

        bool anyDismissed = false;
        for (int i = (int)active_.size() - 1; i >= 0; i--) {
            ActiveToast &at = active_[i];
            if (at.remainingMs < 0) continue; // sticky
            if (at.hovered)         continue; // paused on hover
            at.remainingMs -= 100;
            if (at.remainingMs <= 0) {
                _dismiss(i);
                anyDismissed = true;
            }
        }

        if (!anyDismissed) _refresh();
        _pump();
    }

    void _dismiss(int index) {
        if (index < 0 || index >= (int)active_.size()) return;
        if (active_[index].entry.onDismiss)
            active_[index].entry.onDismiss();
        active_.erase(active_.begin() + index);

        if (active_.empty() && queue_.empty())
            _teardown();
        else {
            _pump();
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
            if (auto *s = _scaffold())
                s->removeOverlay(this);
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