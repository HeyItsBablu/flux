#ifndef FLUX_MENU_BAR_HPP
#define FLUX_MENU_BAR_HPP

#include "../flux_core.hpp"
#include "flux_overlays.hpp"

// ============================================================================
// MENU BAR WIDGET
// ============================================================================
//
// A horizontal strip of labeled menu buttons. Clicking a button opens a
// pulldown list below it — identical to ContextMenuWidget but left-click
// activated, like the File / Edit / View bar in a Windows app.
//
// Usage:
//
//   auto menuBar = MenuBar({
//       MenuBarItem("File", {
//           ContextMenuItem::Action("New",  []{...}),
//           ContextMenuItem::Action("Open", []{...}),
//           ContextMenuItem::Separator(),
//           ContextMenuItem::Action("Exit", []{...}),
//       }),
//       MenuBarItem("Edit", {
//           ContextMenuItem::Action("Cut",   []{...}),
//           ContextMenuItem::Action("Copy",  []{...}),
//           ContextMenuItem::Action("Paste", []{...}),
//       }),
//   });
//
//   // Plug into Scaffold's appBar slot alongside (or instead of) AppBar:
//   Scaffold(menuBar, body)
// ============================================================================

// ── One top-level menu entry (label + its drop-down items) ──────────────────
struct MenuBarItem {
  std::string label;
  std::vector<ContextMenuItem> items;

  MenuBarItem(const std::string &lbl, std::vector<ContextMenuItem> its)
      : label(lbl), items(std::move(its)) {}
};

// ============================================================================
// PULLDOWN POPUP  (one instance, re-used for every open menu)
// ============================================================================
// Shares the exact rendering code from ContextMenuWidget — same shadow,
// same rounded rect, same item/separator geometry.  Only the open trigger
// differs (left-click on the bar button vs right-click on an anchor).

class MenuBarWidget : public Widget, public OverlayHost {
public:
  // ── Appearance ───────────────────────────────────────────────────────────
  int barHeight = 28;
  int buttonPadH = 12; // horizontal padding inside each button
  Color barBgColor = Color::fromRGB(245, 245, 245);
  Color barBorderColor = Color::fromRGB(210, 210, 210);
  Color btnHoverColor = Color::fromRGB(225, 235, 245);
  Color btnOpenColor = Color::fromRGB(210, 228, 248);
  Color btnTextColor = Color::fromRGB(30, 30, 30);

  // Drop-down list appearance (mirrors ContextMenuWidget)
  int itemHeight = 28;
  int separatorHeight = 9;
  int minMenuWidth = 160;
  int menuPadH = 12;
  int menuPadV = 4;
  int menuBorderRadius = 6;
  int menuFontSize = 13;
  int shadowOffset = 3;

  Color menuBgColor = Color::fromRGB(255, 255, 255);
  Color menuBorderColor = Color::fromRGB(180, 180, 180);
  Color itemHoverColor = Color::fromRGB(240, 245, 250);
  Color itemTextColor = Color::fromRGB(30, 30, 30);
  Color itemDisabledColor = Color::fromRGB(160, 160, 160);
  Color separatorColor = Color::fromRGB(220, 220, 220);

  // ── State ─────────────────────────────────────────────────────────────────
  int openMenuIndex = -1; // which top-level entry is open (-1 = none)
  int hoveredBtn = -1;    // which button the mouse is over
  int hoveredItem = -1;   // which drop-down item is hovered

  explicit MenuBarWidget(std::vector<MenuBarItem> entries)
      : entries_(std::move(entries)) {}

  // OverlayHost
  void setScaffold(ScaffoldWidget *s) override { scaffold_ = s; }

  void onDetach() override {
    if (openMenuIndex >= 0)
      closeMenu_();
    Widget::onDetach();
  }

  // ── Layout ────────────────────────────────────────────────────────────────
  void computeLayout(GraphicsContext &ctx, const BoxConstraints &constraints,
                     FontCache &fontCache) override {
    if (autoWidth)
      width = constraints.maxWidth;
    height = barHeight;
    autoHeight = false;

    // Measure button widths
    buttonRects_.resize(entries_.size());
    int curX = 0;
    for (int i = 0; i < (int)entries_.size(); i++) {
      int textW = _measureLabel(ctx, fontCache, entries_[i].label);
      int btnW = textW + buttonPadH * 2;
      buttonRects_[i] = {curX, 0, curX + btnW, barHeight};
      curX += btnW;
    }

    applyConstraints();
    needsLayout = false;
  }

  void positionChildren(int, int, int, int) override {}

  // ── Render the bar ────────────────────────────────────────────────────────
  void render(GraphicsContext &ctx, FontCache &fontCache) override {
    if (!visible)
      return;
    Painter painter(ctx);

    if (pendingSwitch_ >= 0) {
      int target = pendingSwitch_;
      pendingSwitch_ = -1;
      closeMenu_();
      openMenu_(target);
    }

    // Bar background + bottom border
    painter.fillRect(x, y, width, barHeight, barBgColor);
    painter.drawHLine(x, y + barHeight - 1, width, barBorderColor, 1);

    // Buttons
    NativeFont hFont = fontCache.getFont(menuFontSize, FontWeight::Normal);

    for (int i = 0; i < (int)entries_.size(); i++) {
      auto &r = buttonRects_[i];
      int ax = x + r.left, ay = y + r.top;
      int aw = r.right - r.left, ah = r.bottom - r.top;
      bool isOpen = (i == openMenuIndex);
      bool isHover = (i == hoveredBtn);

      if (isOpen || isHover)
        painter.fillRect(ax, ay, aw, ah, isOpen ? btnOpenColor : btnHoverColor);

      painter.drawTextA(entries_[i].label, ax, ay, aw, ah, hFont, btnTextColor,
                        DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    needsPaint = false;
  }

  // ── renderPopupContent ────────────────────────────────────────────────────
  // Draws the open drop-down list into the layered popup DC.
  void renderPopupContent(GraphicsContext &ctx, FontCache &fontCache) override {
    if (openMenuIndex < 0)
      return;
    const auto &items = entries_[openMenuIndex].items;
    if (items.empty())
      return;

    int mW = popupW_, mH = popupH_;
    Painter painter(ctx);

    // Shadow
    painter.fillRoundedRegion(shadowOffset, shadowOffset, mW, mH,
                              menuBorderRadius * 2, Color::fromRGB(0, 0, 0));

    // Background + border
    painter.fillRoundedRectGDI(0, 0, mW, mH, menuBorderRadius * 2, menuBgColor,
                               menuBorderColor, 1);

    // Items
    NativeFont hFont = fontCache.getFont(menuFontSize, FontWeight::Normal);
    int curY = menuPadV;

    for (int i = 0; i < (int)items.size(); i++) {
      const auto &item = items[i];

      if (item.type == ContextMenuItem::Type::Separator) {
        int sy = curY + separatorHeight / 2;
        painter.drawHLine(menuPadH, sy, mW - menuPadH * 2, separatorColor, 1);
        curY += separatorHeight;
      } else {
        if (i == hoveredItem && item.enabled)
          painter.fillRect(2, curY, mW - 4, itemHeight, itemHoverColor);

        painter.drawTextA(
            item.label, menuPadH, curY, mW - menuPadH * 2, itemHeight, hFont,
            item.enabled ? itemTextColor : itemDisabledColor,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        curY += itemHeight;
      }
    }
  }

  // ── Mouse events ──────────────────────────────────────────────────────────

  bool handleMouseDown(int mx, int my) override {
    // Click on a bar button
    int btnIdx = hitTestBar_(mx, my);
    if (btnIdx >= 0) {
      if (openMenuIndex == btnIdx) {
        closeMenu_(); // toggle closed
      } else {
        if (openMenuIndex >= 0)
          closeMenu_();
        openMenu_(btnIdx);
      }
      return true;
    }

    // Click inside the open drop-down
    if (openMenuIndex >= 0) {
      int itemIdx = hitTestPopup_(mx, my);
      if (itemIdx >= 0) {
        const auto &item = entries_[openMenuIndex].items[itemIdx];
        if (item.type == ContextMenuItem::Type::Action && item.enabled) {
          if (item.action)
            item.action();
          closeMenu_();
          return true;
        }
        return true; // absorb click on separator / disabled
      }
      closeMenu_(); // click outside → close
      return true;
    }
    return false;
  }

  bool handleMouseMove(int mx, int my) override {
    // Hover over bar buttons
    int btnIdx = hitTestBar_(mx, my);
    if (btnIdx != hoveredBtn) {
      hoveredBtn = btnIdx;
      if (auto *ui = FluxUI::getCurrentInstance())
        ui->updateWidget(this); // force immediate redraw so highlight moves
    }

    // Hot-tracking: if a menu is already open and cursor moved to a
    // different button, schedule the switch AFTER this event returns so
    // we never invalidate iterators while the caller is still walking the
    // widget tree.
    if (openMenuIndex >= 0 && btnIdx >= 0 && btnIdx != openMenuIndex) {
      pendingSwitch_ = btnIdx;
      if (auto *ui = FluxUI::getCurrentInstance())
        ui->updateWidget(this);
    }

    // Hover over drop-down items
    if (openMenuIndex >= 0) {
      int itemIdx = hitTestPopup_(mx, my);
      if (itemIdx != hoveredItem) {
        hoveredItem = itemIdx;
        refresh_();
      }
    }
    return false;
  }

  bool handleMouseLeave() override {
    hoveredBtn = -1;
    markNeedsPaint();
    return false;
  }

  bool handleKeyDown(int keyCode) override {
    if (openMenuIndex < 0)
      return false;
    const auto &items = entries_[openMenuIndex].items;

    switch (keyCode) {
    case Key::Escape:
      closeMenu_();
      return true;

    case Key::Left:
      closeMenu_();
      openMenu_((openMenuIndex - 1 + (int)entries_.size()) %
                (int)entries_.size());
      return true;

    case Key::Right:
      closeMenu_();
      openMenu_((openMenuIndex + 1) % (int)entries_.size());
      return true;

    case Key::Up: {
      int prev = (hoveredItem <= 0) ? (int)items.size() - 1 : hoveredItem - 1;
      while (prev >= 0 && items[prev].type == ContextMenuItem::Type::Separator)
        prev--;
      hoveredItem = (prev < 0) ? (int)items.size() - 1 : prev;
      refresh_();
      return true;
    }
    case Key::Down: {
      int next = hoveredItem + 1;
      while (next < (int)items.size() &&
             items[next].type == ContextMenuItem::Type::Separator)
        next++;
      hoveredItem = (next >= (int)items.size()) ? 0 : next;
      refresh_();
      return true;
    }
    case Key::Return:
    case Key::Space:
      if (hoveredItem >= 0 && hoveredItem < (int)items.size()) {
        const auto &item = items[hoveredItem];
        if (item.type == ContextMenuItem::Type::Action && item.enabled) {
          if (item.action)
            item.action();
          closeMenu_();
          return true;
        }
      }
      return true;
    }
    return false;
  }

  // ── Fluent setters ────────────────────────────────────────────────────────

  std::shared_ptr<MenuBarWidget> setBarHeight(int h) {
    barHeight = h;
    markNeedsLayout();
    return self_();
  }
  std::shared_ptr<MenuBarWidget> setBarBackground(Color c) {
    barBgColor = c;
    markNeedsPaint();
    return self_();
  }
  std::shared_ptr<MenuBarWidget> setItemHeight(int h) {
    itemHeight = h;
    return self_();
  }
  std::shared_ptr<MenuBarWidget> setMinMenuWidth(int w) {
    minMenuWidth = w;
    return self_();
  }
  std::shared_ptr<MenuBarWidget> setScaffoldPtr(ScaffoldWidget *s) {
    scaffold_ = s;
    return self_();
  }

private:
  std::vector<MenuBarItem> entries_;
  struct BtnRect {
    int left, top, right, bottom;
  };
  std::vector<BtnRect> buttonRects_;
  ScaffoldWidget *scaffold_ = nullptr;
  int pendingSwitch_ = -1; // deferred hot-track switch

  // Popup geometry (screen coords)
  int popupScreenX_ = 0, popupScreenY_ = 0;
  int popupW_ = 0, popupH_ = 0;

  std::shared_ptr<MenuBarWidget> self_() {
    return std::static_pointer_cast<MenuBarWidget>(shared_from_this());
  }

  // ── Open / close ──────────────────────────────────────────────────────────

  void openMenu_(int idx) {
    if (idx < 0 || idx >= (int)entries_.size())
      return;
    openMenuIndex = idx;
    hoveredItem = -1;

    _computePopupGeometry(idx); // now uses FluxUI::clientToScreen internally

    auto *ui = FluxUI::getCurrentInstance();
    if (!ui)
      return;

    FontCache &fc = ui->getFontCache();
    NativeWindow hw = ui->getWindow();
    if (hw)
      showPopup(hw, popupScreenX_, popupScreenY_, popupW_ + shadowOffset,
                popupH_ + shadowOffset, fc);

    if (scaffold_)
      scaffold_->addOverlay(this, [](GraphicsContext, FontCache &) {}, 150);

    markNeedsPaint();
  }

  void closeMenu_() {
    if (openMenuIndex < 0)
      return;
    openMenuIndex = -1;
    hoveredItem = -1;
    hidePopup();
    if (scaffold_)
      scaffold_->removeOverlay(this);
    markNeedsPaint();
  }

  void refresh_() {
    if (!popupVisible())
      return;
    if (auto *ui = FluxUI::getCurrentInstance())
      refreshPopup(ui->getFontCache());
  }

  // ── Geometry ──────────────────────────────────────────────────────────────

  void _computePopupGeometry(int idx) {
    const auto &items = entries_[idx].items;

    int maxLabelW = 0;
    for (const auto &item : items) {
      if (item.type == ContextMenuItem::Type::Action) {
        int lw = (int)item.label.size() * (menuFontSize / 2 + 1);
        maxLabelW = std::max(maxLabelW, lw);
      }
    }
    popupW_ = std::max(minMenuWidth, maxLabelW + menuPadH * 2);

    int totalH = menuPadV * 2;
    for (const auto &item : items)
      totalH += (item.type == ContextMenuItem::Type::Separator)
                    ? separatorHeight
                    : itemHeight;
    popupH_ = totalH;

    // Position below the button
    auto &br = buttonRects_[idx];
    auto sc = FluxUI::getCurrentInstance()->clientToScreen(x + br.left,
                                                           y + barHeight);
    popupScreenX_ = sc.x;
    popupScreenY_ = sc.y;

#ifdef _WIN32
    // Clamp to monitor working area
    POINT pt = {popupScreenX_, popupScreenY_};
    HMONITOR mon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(mon, &mi)) {
      if (popupScreenX_ + popupW_ > mi.rcWork.right)
        popupScreenX_ = mi.rcWork.right - popupW_;
      if (popupScreenY_ + popupH_ > mi.rcWork.bottom)
        popupScreenY_ = sc.y - barHeight - popupH_;
    }
#endif
  }

  // ── Hit testing ───────────────────────────────────────────────────────────

  // Returns button index if (mx,my) is inside the bar, else -1
int hitTestBar_(int mx, int my) const {
    if (my < y || my >= y + barHeight)
        return -1;
    for (int i = 0; i < (int)buttonRects_.size(); i++) {
        const BtnRect &r = buttonRects_[i];
        if (mx >= x + r.left && mx < x + r.right)
            return i;
    }
    return -1;
}

  // Returns item index inside the open popup, else -1
  int hitTestPopup_(int mx, int my) const {
    if (openMenuIndex < 0)
      return -1;

    auto origin = FluxUI::getCurrentInstance()->screenToClient(popupScreenX_,
                                                               popupScreenY_);

    if (mx < origin.x || mx >= origin.x + popupW_ || my < origin.y ||
        my >= origin.y + popupH_)
      return -1;

    const auto &items = entries_[openMenuIndex].items;
    int relY = my - origin.y - menuPadV;
    int curY = 0;

    for (int i = 0; i < (int)items.size(); i++) {
      int h = (items[i].type == ContextMenuItem::Type::Separator)
                  ? separatorHeight
                  : itemHeight;
      if (relY >= curY && relY < curY + h) {
        if (items[i].type == ContextMenuItem::Type::Separator)
          return -1;
        return i;
      }
      curY += h;
    }
    return -1;
  }

  // ── Text measurement ──────────────────────────────────────────────────────

  int _measureLabel(GraphicsContext &ctx, FontCache &fc,
                    const std::string &label) const {
    NativeFont hFont = fc.getFont(menuFontSize, FontWeight::Normal);
    std::wstring wlabel(label.begin(), label.end());
    int w = 0, h = 0;
    Painter(ctx).measureText(wlabel, hFont, w, h);
    return w;
  }
};

// ============================================================================
// FACTORY
// ============================================================================

using MenuBarWidgetPtr = std::shared_ptr<MenuBarWidget>;

inline MenuBarWidgetPtr MenuBar(std::vector<MenuBarItem> items) {
  return std::make_shared<MenuBarWidget>(std::move(items));
}

#endif // FLUX_MENU_BAR_HPP