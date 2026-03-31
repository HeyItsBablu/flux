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
  WidgetPtr content;

  TabItem(const std::string &lbl, WidgetPtr w)
      : label(lbl), content(std::move(w)) {}
};

// ============================================================================
// TAB VIEW WIDGET
// ============================================================================

class TabViewWidget : public Widget {
public:
  // ── Appearance ───────────────────────────────────────────────────────────
  int tabBarHeight = 40;
  int tabMinWidth = 90;
  int tabPadH = 16;
  int indicatorHeight = 3;
  int tabFontSize = 13;

  Color barBgColor = Color::fromRGB(245, 245, 245);
  Color barBorderColor = Color::fromRGB(210, 210, 210);
  Color activeTabBg = Color::fromRGB(255, 255, 255);
  Color activeTabText = Color::fromRGB(33, 150, 243);
  Color inactiveTabText = Color::fromRGB(80, 80, 80);
  Color hoverTabBg = Color::fromRGB(235, 235, 235);
  Color indicatorColor = Color::fromRGB(33, 150, 243);
  Color contentBgColor = Color::fromRGB(255, 255, 255);
  Color contentBorderColor = Color::fromRGB(210, 210, 210);

  bool hasContentBorder = true;
  bool hasContentBg = true;
  int contentPadding = 0;

  // ── State ─────────────────────────────────────────────────────────────────
  int activeIndex = 0;
  int hoveredIndex = -1;

  std::function<void(int)> onTabChanged;

  // ── Constructor ───────────────────────────────────────────────────────────
  explicit TabViewWidget(std::vector<TabItem> tabs) : tabs_(std::move(tabs)) {
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
    if (autoWidth)
      width = constraints.maxWidth;
    if (autoHeight)
      height = constraints.maxHeight;
    if (width < 1)
      width = 1;
    if (height < 1)
      height = 1;

    if (tabs_.empty()) {
      applyConstraints();
      needsLayout = false;
      return;
    }

    tabRects_.resize(tabs_.size());
    int curX = 0;
    for (int i = 0; i < (int)tabs_.size(); i++) {
      int textW = _measureLabel(ctx, fontCache, tabs_[i].label);
      int tabW = max(tabMinWidth, textW + tabPadH * 2);
      tabRects_[i] = {curX, 0, curX + tabW, tabBarHeight};
      curX += tabW;
    }

    int contentY = tabBarHeight + indicatorHeight;
    int contentH = height - contentY;
    int innerW = max(1, width - contentPadding * 2);
    int innerH = max(1, contentH - contentPadding * 2);

    if (contentH > 0 && activeIndex >= 0 && activeIndex < (int)tabs_.size()) {
      auto &pane = tabs_[activeIndex].content;
      if (pane)
        pane->computeLayout(ctx, BoxConstraints::tight(innerW, innerH),
                            fontCache);
    }

    applyConstraints();
    needsLayout = false;
  }

  // ── positionChildren ──────────────────────────────────────────────────────
  void positionChildren(int contentX, int contentY_, int /*cw*/,
                        int /*ch*/) override {
    int paneTop = contentY_ + tabBarHeight + indicatorHeight + contentPadding;
    int paneH = height - tabBarHeight - indicatorHeight - contentPadding * 2;
    if (paneH <= 0)
      return; // nothing to position yet

    if (_activeValid()) {
      auto &pane = tabs_[activeIndex].content;
      if (pane) {
        pane->x = contentX + contentPadding;
        pane->y = paneTop;
        pane->positionChildren(
            pane->x + pane->paddingLeft, pane->y + pane->paddingTop,
            pane->width - pane->paddingLeft - pane->paddingRight,
            pane->height - pane->paddingTop - pane->paddingBottom);
      }
    }
  }

  // ── render ────────────────────────────────────────────────────────────────
  void render(GraphicsContext &ctx, FontCache &fontCache) override {
    if (!visible)
      return;

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
      if (pane)
        return pane->handleMouseDown(mx, my);
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
      if (pane)
        return pane->handleMouseMove(mx, my);
    }
    return false;
  }

  bool handleMouseUp(int mx, int my) override {
    if (_inContentArea(mx, my) && _activeValid()) {
      auto &pane = tabs_[activeIndex].content;
      if (pane)
        return pane->handleMouseUp(mx, my);
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
    if (!_activeValid())
      return false;
    auto &pane = tabs_[activeIndex].content;
    if (pane)
      return pane->handleMouseWheel(delta);
    return false;
  }

  bool handleKeyDown(int keyCode) override {
    if (platformKeyDown(Key::Control)) {
      if (keyCode == Key::Tab) {
        bool shift = platformKeyDown(Key::Shift);
        int next = activeIndex + (shift ? -1 : 1);
        if (next < 0)
          next = (int)tabs_.size() - 1;
        if (next >= (int)tabs_.size())
          next = 0;
        _switchTo(next);
        return true;
      }
    }
    if (!_activeValid())
      return false;
    auto &pane = tabs_[activeIndex].content;
    if (pane)
      return pane->handleKeyDown(keyCode);
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
    state.bindProperty(
        shared_from_this(),
        [](Widget *w, const int &val) {
          auto *tv = static_cast<TabViewWidget *>(w);
          if (val >= 0 && val < (int)tv->tabs_.size() &&
              val != tv->activeIndex) {
            tv->activeIndex = val;
            tv->markNeedsLayout();
            if (auto *ui = FluxUI::getCurrentInstance())
              ui->updateWidget(tv);
          }
        },
        true);
    boundState_ = &state;
    return self_();
  }

  std::shared_ptr<TabViewWidget> setOnTabChanged(std::function<void(int)> cb) {
    onTabChanged = std::move(cb);
    return self_();
  }

  std::shared_ptr<TabViewWidget> setTabBarHeight(int h) {
    tabBarHeight = h;
    markNeedsLayout();
    return self_();
  }
  std::shared_ptr<TabViewWidget> setTabMinWidth(int w) {
    tabMinWidth = w;
    markNeedsLayout();
    return self_();
  }
  std::shared_ptr<TabViewWidget> setTabFontSize(int s) {
    tabFontSize = s;
    markNeedsLayout();
    return self_();
  }
  std::shared_ptr<TabViewWidget> setIndicatorColor(Color c) {
    indicatorColor = c;
    markNeedsPaint();
    return self_();
  }
  std::shared_ptr<TabViewWidget> setActiveTabText(Color c) {
    activeTabText = c;
    markNeedsPaint();
    return self_();
  }
  std::shared_ptr<TabViewWidget> setBarBackground(Color c) {
    barBgColor = c;
    markNeedsPaint();
    return self_();
  }
  std::shared_ptr<TabViewWidget> setContentPadding(int p) {
    contentPadding = p;
    markNeedsLayout();
    return self_();
  }
  std::shared_ptr<TabViewWidget> setHasContentBorder(bool b) {
    hasContentBorder = b;
    markNeedsPaint();
    return self_();
  }
  std::shared_ptr<TabViewWidget> setWidth(int w) {
    width = w;
    autoWidth = false;
    markNeedsLayout();
    return self_();
  }
  std::shared_ptr<TabViewWidget> setHeight(int h) {
    height = h;
    autoHeight = false;
    markNeedsLayout();
    return self_();
  }
  std::shared_ptr<TabViewWidget> setFlex(int f) {
    flex = f;
    markNeedsLayout();
    return self_();
  }

  // Accent color shortcut — sets indicator, active text, hover all at once
  std::shared_ptr<TabViewWidget> setAccentColor(Color c) {
    indicatorColor = c;
    activeTabText = c;
    markNeedsPaint();
    return self_();
  }

  // Access tab count
  int tabCount() const { return (int)tabs_.size(); }

  // Replace a tab's content at runtime
  void setTabContent(int idx, WidgetPtr content) {
    if (idx < 0 || idx >= (int)tabs_.size())
      return;
    tabs_[idx].content = content;
    if (content)
      content->parent = this;
    if (idx == activeIndex)
      markNeedsLayout();
  }

  // Replace a tab's label at runtime
  void setTabLabel(int idx, const std::string &label) {
    if (idx < 0 || idx >= (int)tabs_.size())
      return;
    tabs_[idx].label = label;
    markNeedsLayout();
  }

private:
  std::vector<TabItem> tabs_;
  struct TabRect {
    int left, top, right, bottom;
  };
  std::vector<TabRect> tabRects_;
  State<int> *boundState_ = nullptr;

  std::shared_ptr<TabViewWidget> self_() {
    return std::static_pointer_cast<TabViewWidget>(shared_from_this());
  }

  // ── Switch tab ────────────────────────────────────────────────────────────
  void _switchTo(int idx) {
    activeIndex = idx;
    if (boundState_)
      boundState_->set(idx);
    if (onTabChanged)
      onTabChanged(idx);
    markNeedsLayout();
    if (auto *ui = FluxUI::getCurrentInstance())
      ui->updateWidget(this);
  }

  // ── Hit testing ───────────────────────────────────────────────────────────

  // Returns tab index if (mx,my) is in the tab bar, else -1
  int _hitTestTab(int mx, int my) const {
    if (tabRects_.empty())
      return -1;
    if (my < y || my >= y + tabBarHeight)
      return -1;
    for (int i = 0; i < (int)tabRects_.size(); i++) {
      const TabRect &r = tabRects_[i];
      if (mx >= x + r.left && mx < x + r.right)
        return i;
    }
    return -1;
  }

  // Returns true if activeIndex is in range for both tabs_ and tabRects_
  bool _activeValid() const {
    return activeIndex >= 0 && activeIndex < (int)tabs_.size() &&
           !tabRects_.empty();
  }

  bool _inContentArea(int mx, int my) const {
    if (tabRects_.empty())
      return false;
    int paneTop = y + tabBarHeight + indicatorHeight;
    return (mx >= x && mx < x + width && my >= paneTop && my < y + height &&
            activeIndex >= 0 && activeIndex < (int)tabs_.size());
  }

  // ── Rendering ─────────────────────────────────────────────────────────────

  void _renderTabBar(GraphicsContext &ctx, FontCache &fontCache) const {
    Painter painter(ctx);

    // Bar background
    painter.fillRect(x, y, width, tabBarHeight, barBgColor);

    // Bottom border
    painter.drawHLine(x, y + tabBarHeight, width, barBorderColor, 1);

    NativeFont hFont = fontCache.getFont(tabFontSize, FontWeight::Normal);

    for (int i = 0; i < (int)tabs_.size(); i++) {
      const TabRect &r = tabRects_[i];
      bool isActive = (i == activeIndex);
      // bool isHovered = (i == hoveredIndex && !isActive);

      int ax = x + r.left, ay = y + r.top;
      int aw = r.right - r.left, ah = r.bottom - r.top;

      // Tab background
      Color bgCol = isActive    ? activeTabBg
                       : isHovered ? hoverTabBg
                                   : barBgColor;
      painter.fillRect(ax, ay, aw, ah, bgCol);

      // Divider between inactive tabs
      if (!isActive && i + 1 < (int)tabs_.size() && (i + 1) != activeIndex) {
        painter.drawVLine(ax + aw - 1, ay + 8, ah - 16, barBorderColor, 1);
      }

      // Label
      painter.drawTextA(tabs_[i].label, ax, ay, aw, ah, hFont,
                        isActive ? activeTabText : inactiveTabText,
                        DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    // Active indicator bar
    if (activeIndex >= 0 && activeIndex < (int)tabRects_.size()) {
      const TabRect &ar = tabRects_[activeIndex];
      painter.fillRect(x + ar.left, y + tabBarHeight, ar.right - ar.left,
                       indicatorHeight, indicatorColor);
    }
  }

  void _renderContentArea(GraphicsContext &ctx, FontCache &fontCache) const {
    Painter painter(ctx);
    int paneTop = y + tabBarHeight + indicatorHeight;

    if (hasContentBg)
      painter.fillRect(x, paneTop, width,
                       height - tabBarHeight - indicatorHeight, contentBgColor);

    if (hasContentBorder)
      painter.drawRectOutline(x, paneTop, width,
                              height - tabBarHeight - indicatorHeight,
                              contentBorderColor, 1);

    if (activeIndex >= 0 && activeIndex < (int)tabs_.size()) {
      auto &pane = tabs_[activeIndex].content;
      if (pane)
        pane->render(ctx, fontCache);
    }
  }

  // ── Text measurement ──────────────────────────────────────────────────────
  int _measureLabel(GraphicsContext &ctx, FontCache &fc,
                    const std::string &label) const {
    NativeFont hFont = fc.getFont(tabFontSize, FontWeight::Normal);
    std::wstring wlabel(label.begin(), label.end());
    int w = 0, h = 0;
    Painter(ctx).measureText(wlabel, hFont, w, h);
    return w;
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