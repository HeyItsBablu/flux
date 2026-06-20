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
//   // Widget items are supported too:
//   MenuBarItem("File", {
//       ContextMenuItem::Widget(
//           FilePicker("Open file…")
//               ->setMode(FilePickerMode::Open)
//               ->addFilter("All files", {"*.*"})
//               ->setOnChanged([](const std::string &p){ ... })
//       ),
//       ContextMenuItem::Separator(),
//       ContextMenuItem::Action("Exit", []{...}),
//   })
//
//   // Just add it like any other widget — no Scaffold wiring needed, the
//   // OverlayManager (owned by FluxUI) handles popup mechanics:
//   Scaffold(menuBar, body)
// ============================================================================

// ── One top-level menu entry (label + its drop-down items) ──────────────────
struct MenuBarItem
{
  std::string label;
  std::vector<ContextMenuItem> items;

  MenuBarItem(const std::string &lbl, std::vector<ContextMenuItem> its)
      : label(lbl), items(std::move(its)) {}
};

// ============================================================================
// MENU BAR WIDGET
// ============================================================================
// Shares the exact rendering approach as ContextMenuWidget — same shadow,
// same rounded rect, same item/separator geometry. Only the open trigger
// differs (left-click on the bar button vs right-click on an anchor), and
// there's a second open trigger besides: hot-tracking between bar buttons
// while a menu is already open.
//
// renderOverlay()/onOverlay*() all operate in coordinates LOCAL to the
// popup's own rect — (0,0) is the popup's top-left corner. OverlayManager
// handles screen-space conversion, monitor clamping, and native popup
// creation; this widget never touches any of that.

class MenuBarWidget : public Widget, public OverlayContent
{
public:
  // ── Appearance ───────────────────────────────────────────────────────────
  int barHeight = 28;
  int buttonPadH = 12; // horizontal padding inside each button
  Color barBgColor = Color::fromRGBA(0, 0, 0, 0);
  Color barBorderColor = Color::fromRGBA(0, 0, 0, 0);
  Color btnHoverColor = Color::fromRGB(225, 235, 245);
  Color btnOpenColor = Color::fromRGB(210, 228, 248);
  Color btnTextColor = Color::fromRGB(0, 0, 0);

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
  int hoveredItem = -1;   // which drop-down item is hovered (popup-local)

  explicit MenuBarWidget(std::vector<MenuBarItem> entries)
      : entries_(std::move(entries)) {}

  void onDetach() override
  {
    if (openMenuIndex >= 0)
      closeMenu_();
    Widget::onDetach();
  }

  // ── OverlayContent ────────────────────────────────────────────────────────
  OverlayPolicy overlayPolicy() const override
  {
    // Same reasoning as ContextMenuWidget: modal = true so every click
    // while open is consumed (including outside clicks, which close it).
    // blocksHoverBelow stays false — an open pulldown never pauses
    // hover/tooltips elsewhere in the app, and critically it must NOT
    // block hover reaching this same widget's bar buttons, or
    // hot-tracking between File/Edit/View would stop working while a
    // menu is open.
    return {/*modal=*/true, /*blocksHoverBelow=*/false, /*capturesKeyboard=*/true};
  }

  // ── Layout ────────────────────────────────────────────────────────────────
  void computeLayout(GraphicsContext &ctx, const BoxConstraints & /*constraints*/,
                     FontCache &fontCache) override
  {
    // Measure button widths first
    buttonRects_.resize(entries_.size());
    int curX = 0;
    for (int i = 0; i < (int)entries_.size(); i++)
    {
      int textW = _measureLabel(ctx, fontCache, entries_[i].label);
      int btnW = textW + buttonPadH * 2;
      buttonRects_[i] = {curX, 0, curX + btnW, barHeight};
      curX += btnW;
    }

    // Fit to content width instead of expanding to max
    if (autoWidth)
      width = curX;
    height = barHeight;
    autoHeight = false;

    applyConstraints();
    needsLayout = false;
  }

  void positionChildren(int, int, int, int) override {}

  // ── Render the bar ────────────────────────────────────────────────────────
  void render(GraphicsContext &ctx, FontCache &fontCache) override
  {
    if (!visible)
      return;
    Painter painter(ctx);

    if (pendingSwitch_ >= 0)
    {
      int target = pendingSwitch_;
      pendingSwitch_ = -1;
      closeMenu_();
      openMenu_(target);
    }

    // Bar background + bottom border
    if (barBgColor.a > 0)
      painter.fillRect(x, y, width, barHeight, barBgColor);
    painter.drawHLine(x, y + barHeight - 1, width, barBorderColor, 1);

    // Buttons
    NativeFont hFont = fontCache.getFont(menuFontSize, FontWeight::Normal);

    for (int i = 0; i < (int)entries_.size(); i++)
    {
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

  // ── renderOverlay ─────────────────────────────────────────────────────────
  // Draws the open drop-down list. Fully local coordinates — (0,0) is the
  // popup's own top-left corner, identical in spirit to
  // ContextMenuWidget::renderOverlay().
  void renderOverlay(GraphicsContext &ctx, FontCache &fontCache) override
  {
    if (openMenuIndex < 0)
      return;
    const auto &items = entries_[openMenuIndex].items;
    if (items.empty())
      return;

    int mW = popupW_, mH = popupH_;
    Painter painter(ctx);

    // Shadow
    painter.fillRoundedRect(shadowOffset, shadowOffset, mW, mH,
                            menuBorderRadius, Color::fromRGBA(0, 0, 0, 60));

    // Background + border
    painter.fillRoundedRect(0, 0, mW, mH, menuBorderRadius, menuBgColor);
    painter.drawBorder(0, 0, mW, mH, menuBorderRadius, menuBorderColor, 1);

    // Items
    NativeFont hFont = fontCache.getFont(menuFontSize, FontWeight::Normal);
    int curY = menuPadV;

    for (int i = 0; i < (int)items.size(); i++)
    {
      const auto &item = items[i];

      if (item.type == ContextMenuItem::Type::Separator)
      {
        int sy = curY + separatorHeight / 2;
        painter.drawLine(menuPadH, sy, mW - menuPadH, sy, separatorColor, 1);
        curY += separatorHeight;
      }
      else if (item.type == ContextMenuItem::Type::Widget && item.widget)
      {
        int rowH = _widgetItemHeight(item);

        if (i == hoveredItem)
          painter.fillRect(2, curY, mW - 4, rowH, itemHoverColor);

        // Layout the embedded widget into the available row width, then paint.
        auto *ui = FluxUI::getCurrentInstance();
        if (ui)
        {
          if (item.widget->needsLayout)
          {
            item.widget->computeLayout(
                ctx,
                BoxConstraints::tight(mW - menuPadH * 2, rowH),
                fontCache);
          }
          item.widget->x = menuPadH;
          item.widget->y = curY;
          item.widget->positionChildren(
              item.widget->x + item.widget->paddingLeft,
              item.widget->y + item.widget->paddingTop,
              item.widget->width - item.widget->paddingLeft - item.widget->paddingRight,
              item.widget->height - item.widget->paddingTop - item.widget->paddingBottom);
          item.widget->render(ctx, fontCache);
        }

        curY += rowH;
      }
      else
      {
        // Action row
        if (i == hoveredItem && item.enabled)
          painter.fillRect(2, curY, mW - 4, itemHeight, itemHoverColor);

        std::wstring wlabel = toWideString(item.label);
        painter.drawText(
            wlabel, menuPadH, curY, mW - menuPadH * 2, itemHeight, hFont,
            item.enabled ? itemTextColor : itemDisabledColor,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        curY += itemHeight;
      }
    }
  }

  // ── OverlayContent input handlers (popup-local coordinates) ─────────────

  bool onOverlayMouseDown(int localX, int localY) override
  {
    if (openMenuIndex < 0)
      return false;

    int itemIdx = hitTestPopupLocal_(localX, localY);
    if (itemIdx >= 0)
    {
      const auto &item = entries_[openMenuIndex].items[itemIdx];

      if (item.type == ContextMenuItem::Type::Widget && item.widget)
      {
        // Forward click into the embedded widget (already popup-local —
        // no coordinate translation needed, unlike the old screen-coord
        // version). Do NOT auto-close before this if you want the widget
        // to own its own lifecycle; preserved here exactly as the
        // ContextMenuWidget migration did, with the same caveat noted
        // there: this still closes first. Flagging again — fix together
        // if/when you address it for ContextMenuWidget.
        closeMenu_();
        item.widget->handleMouseDown(localX, localY);
        return true;
      }
      if (item.type == ContextMenuItem::Type::Action && item.enabled)
      {
        if (item.action)
          item.action();
        closeMenu_();
        return true;
      }
      return true; // absorb click on disabled action
    }

    closeMenu_(); // click outside the popup -> close
    return true;
  }

  bool onOverlayMouseMove(int localX, int localY) override
  {
    if (openMenuIndex < 0)
      return false;

    int itemIdx = hitTestPopupLocal_(localX, localY);

    // Forward move into widget items so their internal hover states update.
    if (itemIdx >= 0 && itemIdx < (int)entries_[openMenuIndex].items.size())
    {
      const auto &item = entries_[openMenuIndex].items[itemIdx];
      if (item.type == ContextMenuItem::Type::Widget && item.widget)
        item.widget->handleMouseMove(localX, localY);
    }

    if (itemIdx != hoveredItem)
    {
      hoveredItem = itemIdx;
      refresh_();
      return true;
    }
    return false;
  }

  void onOverlayOutsideClick() override { closeMenu_(); }

  bool onOverlayKeyDown(int keyCode) override
  {
    if (openMenuIndex < 0)
      return false;
    const auto &items = entries_[openMenuIndex].items;

    switch (keyCode)
    {
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

    case Key::Up:
    {
      int prev = (hoveredItem <= 0) ? (int)items.size() - 1 : hoveredItem - 1;
      while (prev >= 0 && items[prev].type == ContextMenuItem::Type::Separator)
        prev--;
      hoveredItem = (prev < 0) ? (int)items.size() - 1 : prev;
      refresh_();
      return true;
    }
    case Key::Down:
    {
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
      if (hoveredItem >= 0 && hoveredItem < (int)items.size())
      {
        const auto &item = items[hoveredItem];

        if (item.type == ContextMenuItem::Type::Widget && item.widget)
        {
          // Synthesise a click at the widget's centre (already local).
          int cx = item.widget->x + item.widget->width / 2;
          int cy = item.widget->y + item.widget->height / 2;
          item.widget->handleMouseDown(cx, cy);
          return true;
        }
        if (item.type == ContextMenuItem::Type::Action && item.enabled)
        {
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

  // ── Bar mouse events (normal widget-tree dispatch) ───────────────────────
  // These only ever see bar-button hits now. Hit-testing the open popup is
  // handled by OverlayManager via onOverlayMouseDown/onOverlayMouseMove
  // above — the manager dispatches directly to the popup's local handlers
  // while it's open and never reaches here for popup-area coordinates.

  bool handleMouseDown(int mx, int my) override
  {
    int btnIdx = hitTestBar_(mx, my);
    if (btnIdx >= 0)
    {
      if (openMenuIndex == btnIdx)
      {
        closeMenu_(); // toggle closed
      }
      else
      {
        if (openMenuIndex >= 0)
          closeMenu_();
        openMenu_(btnIdx);
      }
      return true;
    }
    return false;
  }

  bool handleMouseMove(int mx, int my) override
  {
    // Hover over bar buttons. This keeps running even while a menu is
    // open because blocksHoverBelow is false (see overlayPolicy()) — the
    // normal tree walk in FluxUI::wireCallbacks' onMouseMove still
    // reaches this widget.
    int btnIdx = hitTestBar_(mx, my);
    if (btnIdx != hoveredBtn)
    {
      hoveredBtn = btnIdx;
      if (auto *ui = FluxUI::getCurrentInstance())
        ui->updateWidget(this); // force immediate redraw so highlight moves
    }

    // Hot-tracking: if a menu is already open and cursor moved to a
    // different button, schedule the switch AFTER this event returns so
    // we never close/reopen overlays (and mutate OverlayManager's entry
    // list) while something is still iterating it.
    if (openMenuIndex >= 0 && btnIdx >= 0 && btnIdx != openMenuIndex)
    {
      pendingSwitch_ = btnIdx;
      if (auto *ui = FluxUI::getCurrentInstance())
        ui->updateWidget(this);
    }

    return false;
  }

  bool handleMouseLeave() override
  {
    hoveredBtn = -1;
    markNeedsPaint();
    return false;
  }

  // Keyboard while open is handled by onOverlayKeyDown (above) via
  // OverlayManager::dispatchKeyDown, since overlayPolicy().capturesKeyboard
  // is true. No handleKeyDown override needed here.

  // ── Fluent setters ────────────────────────────────────────────────────────

  std::shared_ptr<MenuBarWidget> setBarHeight(int h)
  {
    barHeight = h;
    markNeedsLayout();
    return self_();
  }
  std::shared_ptr<MenuBarWidget> setBarBackground(Color c)
  {
    barBgColor = c;
    markNeedsPaint();
    return self_();
  }
  std::shared_ptr<MenuBarWidget> setItemHeight(int h)
  {
    itemHeight = h;
    return self_();
  }
  std::shared_ptr<MenuBarWidget> setMinMenuWidth(int w)
  {
    minMenuWidth = w;
    return self_();
  }
  std::shared_ptr<MenuBarWidget> setBtnTextColor(Color c)
  {
    btnTextColor = c;
    markNeedsPaint();
    return self_();
  }

private:
  std::vector<MenuBarItem> entries_;
  struct BtnRect
  {
    int left, top, right, bottom;
  };
  std::vector<BtnRect> buttonRects_;
  int pendingSwitch_ = -1; // deferred hot-track switch

  // Popup geometry — CLIENT coordinates now (not screen). OverlayManager
  // does its own screen-space conversion and monitor clamping internally.
  int popupClientX_ = 0, popupClientY_ = 0;
  int popupW_ = 0, popupH_ = 0;

  std::shared_ptr<MenuBarWidget> self_()
  {
    return std::static_pointer_cast<MenuBarWidget>(shared_from_this());
  }

  // ── Height helpers ────────────────────────────────────────────────────────

  int _itemHeight(const ContextMenuItem &item) const
  {
    if (item.type == ContextMenuItem::Type::Separator)
      return separatorHeight;
    if (item.type == ContextMenuItem::Type::Widget && item.widget)
      return _widgetItemHeight(item);
    return itemHeight;
  }

  int _widgetItemHeight(const ContextMenuItem &item) const
  {
    if (!item.widget)
      return itemHeight;
    int h = item.widget->height > 0 ? item.widget->height : item.widget->minHeight;
    return h > 0 ? h : itemHeight;
  }

  // ── Open / close ──────────────────────────────────────────────────────────

  void openMenu_(int idx)
  {
    if (idx < 0 || idx >= (int)entries_.size())
      return;

    auto *ui = FluxUI::getCurrentInstance();
    if (!ui)
      return;

    openMenuIndex = idx;
    hoveredItem = -1;

    // Pre-layout all widget items so their heights are known before geometry.
    _layoutWidgetItems(idx, ui);

    _computePopupGeometry(idx); // size + client-space position only

    ui->overlays().show(this, popupClientX_, popupClientY_,
                        popupW_ + shadowOffset, popupH_ + shadowOffset,
                        150, ui->getFontCache());

    markNeedsPaint();
  }

  void closeMenu_()
  {
    if (openMenuIndex < 0)
      return;
    openMenuIndex = -1;
    hoveredItem = -1;
    if (auto *ui = FluxUI::getCurrentInstance())
      ui->overlays().hide(this);
    markNeedsPaint();
  }

  void refresh_()
  {
    if (openMenuIndex < 0)
      return;
    if (auto *ui = FluxUI::getCurrentInstance())
      ui->overlays().refresh(this, ui->getFontCache());
  }

  // ── Widget pre-layout ─────────────────────────────────────────────────────

  void _layoutWidgetItems(int idx, FluxUI *ui)
  {
    auto mc = ui->getMeasureContext();
    FontCache &fc = ui->getFontCache();

    for (auto &item : entries_[idx].items)
    {
      if (item.type == ContextMenuItem::Type::Widget && item.widget)
      {
        if (item.widget->needsLayout || item.widget->height == 0)
        {
          item.widget->computeLayout(
              mc.ctx,
              BoxConstraints::loose(minMenuWidth - menuPadH * 2, kUnbounded),
              fc);
        }
      }
    }
  }

  // ── Geometry ──────────────────────────────────────────────────────────────
  // Pure size + client-space position now. No screen coordinates, no
  // monitor clamping — OverlayManager::show() handles all of that.
  void _computePopupGeometry(int idx)
  {
    const auto &items = entries_[idx].items;

    // Determine required width: longest action label OR widest widget.
    int maxLabelW = 0;
    for (const auto &item : items)
    {
      if (item.type == ContextMenuItem::Type::Action)
      {
        int lw = (int)item.label.size() * (menuFontSize / 2 + 1);
        maxLabelW = std::max(maxLabelW, lw);
      }
      else if (item.type == ContextMenuItem::Type::Widget && item.widget)
      {
        int lw = item.widget->width > 0 ? item.widget->width : item.widget->minWidth;
        maxLabelW = std::max(maxLabelW, lw);
      }
    }
    popupW_ = std::max(minMenuWidth, maxLabelW + menuPadH * 2);

    // Determine required height: sum of all item heights.
    int totalH = menuPadV * 2;
    for (const auto &item : items)
      totalH += _itemHeight(item);
    popupH_ = totalH;

    // Position below the button, in this widget's own (client-space)
    // coordinate system — x/y here are already client coordinates since
    // every widget lays itself out in client space.
    auto &br = buttonRects_[idx];
    popupClientX_ = x + br.left;
    popupClientY_ = y + barHeight;
  }

  // ── Hit testing ───────────────────────────────────────────────────────────

  // Returns button index if (mx,my) is inside the bar, else -1.
  // mx/my here are normal client coordinates (bar dispatch only).
  int hitTestBar_(int mx, int my) const
  {
    if (my < y || my >= y + barHeight)
      return -1;
    for (int i = 0; i < (int)buttonRects_.size(); i++)
    {
      const BtnRect &r = buttonRects_[i];
      if (mx >= x + r.left && mx < x + r.right)
        return i;
    }
    return -1;
  }

  // Returns item index inside the open popup, else -1. localX/localY are
  // already popup-local (0,0 = popup top-left) — no client/screen
  // conversion needed, unlike the old version.
  int hitTestPopupLocal_(int localX, int localY) const
  {
    if (openMenuIndex < 0)
      return -1;
    if (localX < 0 || localX >= popupW_ || localY < 0 || localY >= popupH_)
      return -1;

    const auto &items = entries_[openMenuIndex].items;
    int relY = localY - menuPadV;
    int curY = 0;

    for (int i = 0; i < (int)items.size(); i++)
    {
      int h = _itemHeight(items[i]);
      if (relY >= curY && relY < curY + h)
      {
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
                    const std::string &label) const
  {
    NativeFont hFont = fc.getFont(menuFontSize, FontWeight::Normal);
    std::wstring wlabel = toWideString(label);
    int w = 0, h = 0;
    Painter(ctx).measureText(wlabel, hFont, w, h);
    return w;
  }
};

// ============================================================================
// FACTORY
// ============================================================================

using MenuBarWidgetPtr = std::shared_ptr<MenuBarWidget>;

inline MenuBarWidgetPtr MenuBar(std::vector<MenuBarItem> items)
{
  return std::make_shared<MenuBarWidget>(std::move(items));
}

#endif // FLUX_MENU_BAR_HPP