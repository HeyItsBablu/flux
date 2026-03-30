#ifndef FLUX_TAB_VIEW_HPP
#define FLUX_TAB_VIEW_HPP

#include "flux_core.hpp"
#include "flux_layout.hpp"
#include "flux_state.hpp"

// ============================================================================
// TAB VIEW WIDGET
// ============================================================================
//
// A tab bar + content area. Clicking a tab shows its pane; inactive panes
// are skipped during layout and render entirely (zero cost).
//
//   ┌──────────┬──────────┬──────────┬──────────┐
//   │ General  │ Display  │ Network  │ Advanced │  ← tab bar
//   └──────────┴──────────┴──────────┴──────────┘▓ ← indicator
//   ┌─────────────────────────────────────────────┐
//   │                                             │
//   │          active pane content                │  ← content area
//   │                                             │
//   └─────────────────────────────────────────────┘
//
// Usage:
//   auto tv = TabView({
//       Tab("General",  generalWidget),
//       Tab("Display",  displayWidget),
//       Tab("Network",  networkWidget),
//   });
//
// Reactive active index:
//   State<int> activeTab(0, app);
//   auto tv = TabView({...})->setActiveIndex(activeTab);
//   activeTab.set(2); // switches to third tab programmatically
//
// Callback:
//   tv->setOnTabChanged([](int index) { ... });
// ============================================================================

struct TabItem {
    std::string label;
    WidgetPtr   content;

    TabItem(const std::string &lbl, WidgetPtr w)
        : label(lbl), content(std::move(w)) {}
};

// ============================================================================
// TAB VIEW WIDGET
// ============================================================================

class TabViewWidget : public Widget {
public:
    // ── Appearance ───────────────────────────────────────────────────────────
    int      tabBarHeight       = 40;
    int      tabMinWidth        = 90;
    int      tabPadH            = 16;
    int      indicatorHeight    = 3;
    int      tabFontSize        = 13;

    COLORREF barBgColor         = RGB(245, 245, 245);
    COLORREF barBorderColor     = RGB(210, 210, 210);
    COLORREF activeTabBg        = RGB(255, 255, 255);
    COLORREF activeTabText      = RGB(33,  150, 243);
    COLORREF inactiveTabText    = RGB(80,  80,  80);
    COLORREF hoverTabBg         = RGB(235, 235, 235);
    COLORREF indicatorColor     = RGB(33,  150, 243);
    COLORREF contentBgColor     = RGB(255, 255, 255);
    COLORREF contentBorderColor = RGB(210, 210, 210);

    bool     hasContentBorder   = true;
    bool     hasContentBg       = true;
    int      contentPadding     = 0;

    // ── State ─────────────────────────────────────────────────────────────────
    int  activeIndex  = 0;
    int  hoveredIndex = -1;

    std::function<void(int)> onTabChanged;

    // ── Constructor ───────────────────────────────────────────────────────────
    explicit TabViewWidget(std::vector<TabItem> tabs)
        : tabs_(std::move(tabs)) {
        // Register tab content widgets as children so the tree
        // walk (hit testing, detach, etc.) can reach them.
        for (auto &tab : tabs_)
            if (tab.content)
                tab.content->parent = this;
    }

    void onDetach() override {
        for (auto &tab : tabs_)
            if (tab.content)
                tab.content->onDetach();
    }

    // ── computeLayout ─────────────────────────────────────────────────────────
    void computeLayout(GraphicsContext &ctx, const BoxConstraints &constraints,
                       FontCache &fontCache) override {
        if (autoWidth)  width  = constraints.maxWidth;
        if (autoHeight) height = constraints.maxHeight;

        // Clamp to sane minimum so inner layout never gets negative sizes
        if (width  < 1) width  = 1;
        if (height < 1) height = 1;

        // Nothing to do if no tabs
        if (tabs_.empty()) {
            applyConstraints();
            needsLayout = false;
            return;
        }

        // Measure tab widths
        tabRects_.resize(tabs_.size());
        int curX = 0;
        for (int i = 0; i < (int)tabs_.size(); i++) {
            int textW = _measureLabel(ctx, fontCache, tabs_[i].label);
            int tabW  = max(tabMinWidth, textW + tabPadH * 2);
            tabRects_[i] = {curX, 0, curX + tabW, tabBarHeight};
            curX += tabW;
        }

        // Layout only the active pane — skip if no usable content area yet
        int contentY = tabBarHeight + indicatorHeight;
        int contentH = height - contentY;
        int innerW   = max(1, width  - contentPadding * 2);
        int innerH   = max(1, contentH - contentPadding * 2);

        if (contentH > 0 && activeIndex >= 0 &&
            activeIndex < (int)tabs_.size()) {
            auto &pane = tabs_[activeIndex].content;
            if (pane) {
                pane->computeLayout(
                    ctx, BoxConstraints::tight(innerW, innerH), fontCache);
            }
        }

        applyConstraints();
        needsLayout = false;
    }

    // ── positionChildren ──────────────────────────────────────────────────────
    void positionChildren(int contentX, int contentY_,
                          int /*cw*/, int /*ch*/) override {
        int paneTop = contentY_ + tabBarHeight + indicatorHeight + contentPadding;
        int paneH   = height - tabBarHeight - indicatorHeight - contentPadding * 2;
        if (paneH <= 0) return; // nothing to position yet

        if (_activeValid()) {
            auto &pane = tabs_[activeIndex].content;
            if (pane) {
                pane->x = contentX + contentPadding;
                pane->y = paneTop;
                pane->positionChildren(
                    pane->x + pane->paddingLeft,
                    pane->y + pane->paddingTop,
                    pane->width  - pane->paddingLeft - pane->paddingRight,
                    pane->height - pane->paddingTop  - pane->paddingBottom);
            }
        }
    }

    // ── render ────────────────────────────────────────────────────────────────
    void render(GraphicsContext &ctx, FontCache &fontCache) override {
        if (!visible) return;

        _renderTabBar(ctx, fontCache);
        _renderContentArea(ctx, fontCache);

        needsPaint = false;
    }

    // ── Mouse events ──────────────────────────────────────────────────────────

    bool handleMouseDown(int mx, int my) override {
        int idx = _hitTestTab(mx, my);
        if (idx >= 0 && idx != activeIndex) {
            _switchTo(idx);
            return true;
        }
        // Forward to active pane
        if (_inContentArea(mx, my) && _activeValid()) {
            auto &pane = tabs_[activeIndex].content;
            if (pane) return pane->handleMouseDown(mx, my);
        }
        return false;
    }

    bool handleMouseMove(int mx, int my) override {
        int idx = _hitTestTab(mx, my);
        if (idx != hoveredIndex) {
            hoveredIndex = idx;
            if (auto *ui = FluxUI::getCurrentInstance())
                ui->updateWidget(this);
        }
        // Forward to active pane
        if (_inContentArea(mx, my) && _activeValid()) {
            auto &pane = tabs_[activeIndex].content;
            if (pane) return pane->handleMouseMove(mx, my);
        }
        return false;
    }

    bool handleMouseUp(int mx, int my) override {
        if (_inContentArea(mx, my) && _activeValid()) {
            auto &pane = tabs_[activeIndex].content;
            if (pane) return pane->handleMouseUp(mx, my);
        }
        return false;
    }

    bool handleMouseLeave() override {
        if (hoveredIndex != -1) {
            hoveredIndex = -1;
            if (auto *ui = FluxUI::getCurrentInstance())
                ui->updateWidget(this);
        }
        return false;
    }

    bool handleMouseWheel(int delta) override {
        if (!_activeValid()) return false;
        auto &pane = tabs_[activeIndex].content;
        if (pane) return pane->handleMouseWheel(delta);
        return false;
    }

    bool handleKeyDown(int keyCode) override {
        // Ctrl+Tab / Ctrl+Shift+Tab to cycle tabs
        if (GetKeyState(Key::Control) & 0x8000) {
            if (keyCode == Key::Tab) {
                bool shift = (GetKeyState(Key::Shift) & 0x8000) != 0;
                int  next  = activeIndex + (shift ? -1 : 1);
                if (next < 0)                  next = (int)tabs_.size() - 1;
                if (next >= (int)tabs_.size()) next = 0;
                _switchTo(next);
                return true;
            }
        }
        // Forward to active pane
        if (!_activeValid()) return false;
        auto &pane = tabs_[activeIndex].content;
        if (pane) return pane->handleKeyDown(keyCode);
        return false;
    }

    // ── Fluent setters ────────────────────────────────────────────────────────

    std::shared_ptr<TabViewWidget> setActiveIndex(int idx) {
        if (idx >= 0 && idx < (int)tabs_.size() && idx != activeIndex) {
            activeIndex = idx;
            markNeedsLayout();
        }
        return self_();
    }

    // Reactive — binds to a State<int>
    std::shared_ptr<TabViewWidget> setActiveIndex(State<int> &state) {
        activeIndex = state.get();
        markNeedsLayout();
        state.bindProperty(shared_from_this(),
            [](Widget *w, const int &val) {
                auto *tv = static_cast<TabViewWidget *>(w);
                if (val >= 0 && val < (int)tv->tabs_.size() &&
                    val != tv->activeIndex) {
                    tv->activeIndex = val;
                    tv->markNeedsLayout();
                    if (auto *ui = FluxUI::getCurrentInstance())
                        ui->updateWidget(tv);
                }
            }, true);
        boundState_ = &state;
        return self_();
    }

    std::shared_ptr<TabViewWidget> setOnTabChanged(std::function<void(int)> cb) {
        onTabChanged = std::move(cb);
        return self_();
    }

    std::shared_ptr<TabViewWidget> setTabBarHeight(int h) {
        tabBarHeight = h; markNeedsLayout(); return self_();
    }
    std::shared_ptr<TabViewWidget> setTabMinWidth(int w) {
        tabMinWidth = w; markNeedsLayout(); return self_();
    }
    std::shared_ptr<TabViewWidget> setTabFontSize(int s) {
        tabFontSize = s; markNeedsLayout(); return self_();
    }
    std::shared_ptr<TabViewWidget> setIndicatorColor(COLORREF c) {
        indicatorColor = c; markNeedsPaint(); return self_();
    }
    std::shared_ptr<TabViewWidget> setActiveTabText(COLORREF c) {
        activeTabText = c; markNeedsPaint(); return self_();
    }
    std::shared_ptr<TabViewWidget> setBarBackground(COLORREF c) {
        barBgColor = c; markNeedsPaint(); return self_();
    }
    std::shared_ptr<TabViewWidget> setContentPadding(int p) {
        contentPadding = p; markNeedsLayout(); return self_();
    }
    std::shared_ptr<TabViewWidget> setHasContentBorder(bool b) {
        hasContentBorder = b; markNeedsPaint(); return self_();
    }
    std::shared_ptr<TabViewWidget> setWidth(int w) {
        width = w; autoWidth = false; markNeedsLayout(); return self_();
    }
    std::shared_ptr<TabViewWidget> setHeight(int h) {
        height = h; autoHeight = false; markNeedsLayout(); return self_();
    }
    std::shared_ptr<TabViewWidget> setFlex(int f) {
        flex = f; markNeedsLayout(); return self_();
    }

    // Accent color shortcut — sets indicator, active text, hover all at once
    std::shared_ptr<TabViewWidget> setAccentColor(COLORREF c) {
        indicatorColor = c;
        activeTabText  = c;
        markNeedsPaint();
        return self_();
    }

    // Access tab count
    int tabCount() const { return (int)tabs_.size(); }

    // Replace a tab's content at runtime
    void setTabContent(int idx, WidgetPtr content) {
        if (idx < 0 || idx >= (int)tabs_.size()) return;
        tabs_[idx].content = content;
        if (content) content->parent = this;
        if (idx == activeIndex) markNeedsLayout();
    }

    // Replace a tab's label at runtime
    void setTabLabel(int idx, const std::string &label) {
        if (idx < 0 || idx >= (int)tabs_.size()) return;
        tabs_[idx].label = label;
        markNeedsLayout();
    }

private:
    std::vector<TabItem> tabs_;
    std::vector<RECT>    tabRects_; // bar-local x coords
    State<int>          *boundState_ = nullptr;

    std::shared_ptr<TabViewWidget> self_() {
        return std::static_pointer_cast<TabViewWidget>(shared_from_this());
    }

    // ── Switch tab ────────────────────────────────────────────────────────────
    void _switchTo(int idx) {
        activeIndex = idx;
        if (boundState_) boundState_->set(idx);
        if (onTabChanged) onTabChanged(idx);
        markNeedsLayout();
        if (auto *ui = FluxUI::getCurrentInstance())
            ui->updateWidget(this);
    }

    // ── Hit testing ───────────────────────────────────────────────────────────

    // Returns tab index if (mx,my) is in the tab bar, else -1
    int _hitTestTab(int mx, int my) const {
        if (tabRects_.empty()) return -1;
        if (my < y || my >= y + tabBarHeight) return -1;
        for (int i = 0; i < (int)tabRects_.size(); i++) {
            const RECT &r = tabRects_[i];
            if (mx >= x + r.left && mx < x + r.right) return i;
        }
        return -1;
    }

    // Returns true if activeIndex is in range for both tabs_ and tabRects_
    bool _activeValid() const {
        return activeIndex >= 0 &&
               activeIndex < (int)tabs_.size() &&
               !tabRects_.empty();
    }

    bool _inContentArea(int mx, int my) const {
        if (tabRects_.empty()) return false;
        int paneTop = y + tabBarHeight + indicatorHeight;
        return (mx >= x && mx < x + width &&
                my >= paneTop && my < y + height &&
                activeIndex >= 0 && activeIndex < (int)tabs_.size());
    }

    // ── Rendering ─────────────────────────────────────────────────────────────

    void _renderTabBar(GraphicsContext &ctx, FontCache &fontCache) const {
        // Bar background
        {
            HBRUSH hb = CreateSolidBrush(barBgColor);
            RECT   br = {x, y, x + width, y + tabBarHeight};
            FillRect(ctx.hdc, &br, hb);
            DeleteObject(hb);
        }

        // Bottom border of bar
        {
            HPEN pen = CreatePen(PS_SOLID, 1, barBorderColor);
            HPEN old = (HPEN)SelectObject(ctx.hdc, pen);
            MoveToEx(ctx.hdc, x,         y + tabBarHeight, nullptr);
            LineTo  (ctx.hdc, x + width, y + tabBarHeight);
            SelectObject(ctx.hdc, old);
            DeleteObject(pen);
        }

        // Tab buttons
        HFONT hFont    = fontCache.getFont(tabFontSize, FontWeight::Normal);
        HFONT hOldFont = (HFONT)SelectObject(ctx.hdc, hFont);
        SetBkMode(ctx.hdc, TRANSPARENT);

        for (int i = 0; i < (int)tabs_.size(); i++) {
            const RECT &r   = tabRects_[i];
            bool isActive   = (i == activeIndex);
            bool isHovered  = (i == hoveredIndex && !isActive);

            RECT absR = {x + r.left, y + r.top,
                         x + r.right, y + r.bottom};

            // Tab background
            COLORREF bgCol = isActive  ? activeTabBg :
                             isHovered ? hoverTabBg  : barBgColor;
            HBRUSH hb = CreateSolidBrush(bgCol);
            FillRect(ctx.hdc, &absR, hb);
            DeleteObject(hb);

            // Right divider between inactive tabs
            if (!isActive && i + 1 < (int)tabs_.size() && (i + 1) != activeIndex) {
                HPEN dp  = CreatePen(PS_SOLID, 1, barBorderColor);
                HPEN odp = (HPEN)SelectObject(ctx.hdc, dp);
                MoveToEx(ctx.hdc, x + r.right - 1, y + 8,                    nullptr);
                LineTo  (ctx.hdc, x + r.right - 1, y + tabBarHeight - 8);
                SelectObject(ctx.hdc, odp);
                DeleteObject(dp);
            }

            // Label
            SetTextColor(ctx.hdc, isActive ? activeTabText : inactiveTabText);
            DrawTextA(ctx.hdc, tabs_[i].label.c_str(), -1, &absR,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }

        SelectObject(ctx.hdc, hOldFont);

        // Active indicator bar
        if (activeIndex >= 0 && activeIndex < (int)tabRects_.size()) {
            const RECT &ar = tabRects_[activeIndex];
            HBRUSH ib = CreateSolidBrush(indicatorColor);
            RECT   ir = {x + ar.left,
                         y + tabBarHeight,
                         x + ar.right,
                         y + tabBarHeight + indicatorHeight};
            FillRect(ctx.hdc, &ir, ib);
            DeleteObject(ib);
        }
    }

    void _renderContentArea(GraphicsContext &ctx, FontCache &fontCache) const {
        int paneTop = y + tabBarHeight + indicatorHeight;
        int paneH   = height - tabBarHeight - indicatorHeight;

        // Content background
        if (hasContentBg) {
            HBRUSH cb = CreateSolidBrush(contentBgColor);
            RECT   cr = {x, paneTop, x + width, y + height};
            FillRect(ctx.hdc, &cr, cb);
            DeleteObject(cb);
        }

        // Content border
        if (hasContentBorder) {
            HPEN   cp  = CreatePen(PS_SOLID, 1, contentBorderColor);
            HPEN   old = (HPEN)SelectObject(ctx.hdc, cp);
            HBRUSH nb  = (HBRUSH)GetStockObject(NULL_BRUSH);
            HBRUSH ob  = (HBRUSH)SelectObject(ctx.hdc, nb);
            Rectangle(ctx.hdc, x, paneTop, x + width, y + height);
            SelectObject(ctx.hdc, ob);
            SelectObject(ctx.hdc, old);
            DeleteObject(cp);
        }

        // Active pane content
        if (activeIndex >= 0 && activeIndex < (int)tabs_.size()) {
            auto &pane = tabs_[activeIndex].content;
            if (pane) pane->render(ctx, fontCache);
        }
    }

    // ── Text measurement ──────────────────────────────────────────────────────
    int _measureLabel(GraphicsContext &ctx, FontCache &fc, const std::string &label) const {
        HFONT hFont    = fc.getFont(tabFontSize, FontWeight::Normal);
        HFONT hOldFont = (HFONT)SelectObject(ctx.hdc, hFont);
        SIZE  sz       = {};
        GetTextExtentPoint32A(ctx.hdc, label.c_str(), (int)label.size(), &sz);
        SelectObject(ctx.hdc, hOldFont);
        return sz.cx;
    }
};

// ============================================================================
// FACTORY
// ============================================================================

using TabViewWidgetPtr = std::shared_ptr<TabViewWidget>;

inline TabViewWidgetPtr TabView(std::vector<TabItem> tabs) {
    return std::make_shared<TabViewWidget>(std::move(tabs));
}

// Convenience alias so Tab(...) reads naturally at the call site
inline TabItem Tab(const std::string &label, WidgetPtr content) {
    return TabItem(label, std::move(content));
}

#endif // FLUX_TAB_VIEW_HPP