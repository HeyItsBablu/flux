#ifndef FLUX_OVERLAYS_HPP
#define FLUX_OVERLAYS_HPP

#include "flux_structure.hpp"

#include "../flux_app.hpp"
#include "../flux_core.hpp"
#include <algorithm>

// ============================================================================
// CONTEXT MENU ITEM
// ============================================================================

struct ContextMenuItem
{
  enum class Type
  {
    Action,
    Separator,
    Widget
  };

  Type type;
  std::string label;
  std::function<void()> action;
  bool enabled;
  WidgetPtr widget; // only used when type == Widget

  static ContextMenuItem Action(const std::string &label,
                                std::function<void()> action,
                                bool enabled = true)
  {
    ContextMenuItem item;
    item.type = Type::Action;
    item.label = label;
    item.action = action;
    item.enabled = enabled;
    return item;
  }

  static ContextMenuItem Separator()
  {
    ContextMenuItem item;
    item.type = Type::Separator;
    item.label = "";
    item.action = nullptr;
    item.enabled = false;
    return item;
  }

  // Embed an arbitrary widget as a menu row.
  // The widget lays itself out, paints itself, and receives forwarded mouse
  // events.  The menu does NOT auto-close on click — the widget decides.
  static ContextMenuItem Widget(WidgetPtr w)
  {
    ContextMenuItem item;
    item.type = Type::Widget;
    item.widget = std::move(w);
    item.enabled = true;
    return item;
  }

  ContextMenuItem(const std::string &lbl, std::function<void()> act,
                  bool en = true)
      : type(Type::Action), label(lbl), action(act), enabled(en) {}

  ContextMenuItem()
      : type(Type::Action), label(""), action(nullptr), enabled(true) {}
};

// ============================================================================
// CONTEXT MENU WIDGET
// ============================================================================

class ContextMenuWidget : public Widget, public OverlayContent
{
private:
  int menuW = 0, menuH = 0;

  std::vector<ContextMenuItem> items;
  int hoveredIndex = -1;
  int selectedIndex = -1;

  int itemHeight = 28;
  int separatorHeight = 9;
  int minWidth = 160;
  int paddingH = 12;
  int paddingV = 4;

  Color menuBgColor = Color::fromRGBA(255, 255, 255, 255);
  Color menuBorderColor = Color::fromRGBA(180, 180, 180, 255);
  Color itemHoverColor = Color::fromRGB(240, 245, 250);
  Color itemTextColor = Color::fromRGB(30, 30, 30);
  Color itemDisabledColor = Color::fromRGB(160, 160, 160);
  Color separatorColor = Color::fromRGB(220, 220, 220);

  int menuFontSize = 13;
  int menuBorderRadius = 6;
  int shadowOffset = 3;

public:
  bool isOpen = false;

  explicit ContextMenuWidget(WidgetPtr anchor,
                             const std::vector<ContextMenuItem> &menuItems)
      : items(menuItems)
  {
    if (anchor)
    {
      addChild(anchor);
      chainAnchorRightClick(anchor.get());
    }
  }

  void onDetach() override
  {
    if (isOpen)
      closeMenu();
    Widget::onDetach();
  }

  // ── OverlayContent ────────────────────────────────────────────────────
  OverlayPolicy overlayPolicy() const override
  {
    // modal = true: every click while open is consumed, including clicks
    // outside the menu (which close it) — matches the original behavior
    // exactly. blocksHoverBelow stays false: a context menu never paused
    // hover/tooltips elsewhere in the app.
    return {/*modal=*/true, /*blocksHoverBelow=*/false, /*capturesKeyboard=*/true};
  }

  // ── Builder API ───────────────────────────────────────────────────────
  std::shared_ptr<ContextMenuWidget>
  setMenuItems(const std::vector<ContextMenuItem> &menuItems)
  {
    items = menuItems;
    return std::static_pointer_cast<ContextMenuWidget>(shared_from_this());
  }
  std::shared_ptr<ContextMenuWidget> setItemHeight(int h)
  {
    itemHeight = h;
    return std::static_pointer_cast<ContextMenuWidget>(shared_from_this());
  }
  std::shared_ptr<ContextMenuWidget> setMinWidth(int w)
  {
    minWidth = w;
    return std::static_pointer_cast<ContextMenuWidget>(shared_from_this());
  }
  std::shared_ptr<ContextMenuWidget> setMenuBackground(Color color)
  {
    menuBgColor = color;
    return std::static_pointer_cast<ContextMenuWidget>(shared_from_this());
  }
  std::shared_ptr<ContextMenuWidget> setMenuBorder(Color color)
  {
    menuBorderColor = color;
    return std::static_pointer_cast<ContextMenuWidget>(shared_from_this());
  }
  std::shared_ptr<ContextMenuWidget> setItemHoverColor(Color color)
  {
    itemHoverColor = color;
    return std::static_pointer_cast<ContextMenuWidget>(shared_from_this());
  }

  // ── Layout (anchor only — menu itself has no in-tree size) ────────────
  void computeLayout(GraphicsContext &ctx, const BoxConstraints &constraints,
                     FontCache &fontCache) override
  {
    if (autoWidth)
      width = constraints.maxWidth;
    if (autoHeight)
      height = constraints.maxHeight;

    if (!children.empty())
    {
      auto &anchor = children[0];
      anchor->computeLayout(ctx, constraints, fontCache);
      if (autoWidth)
        width = anchor->width;
      if (autoHeight)
        height = anchor->height;
    }

    applyConstraints();
    needsLayout = false;
  }

  void positionChildren(int, int, int, int) override
  {
    if (!children.empty())
    {
      auto &anchor = children[0];
      anchor->x = x;
      anchor->y = y;
      anchor->positionChildren(
          anchor->x + anchor->paddingLeft,
          anchor->y + anchor->paddingTop,
          anchor->width - anchor->paddingLeft - anchor->paddingRight,
          anchor->height - anchor->paddingTop - anchor->paddingBottom);
    }
  }

  void render(GraphicsContext &ctx, FontCache &fontCache) override
  {
    if (!children.empty())
      children[0]->render(ctx, fontCache);
    needsPaint = false;
  }

  // ── renderOverlay ─────────────────────────────────────────────────────
  // Drawing was already entirely in local coordinates (0,0 = menu
  // top-left) — no changes needed inside this body at all.
  void renderOverlay(GraphicsContext &ctx, FontCache &fontCache) override
  {
    if (!isOpen || items.empty())
      return;

    Painter painter(ctx);

    painter.fillRoundedRect(shadowOffset, shadowOffset, menuW, menuH,
                            menuBorderRadius, Color::fromRGBA(0, 0, 0, 60));

    painter.fillRoundedRect(0, 0, menuW, menuH, menuBorderRadius, menuBgColor);
    painter.drawBorder(0, 0, menuW, menuH, menuBorderRadius, menuBorderColor, 1);

    NativeFont font = fontCache.getFont(menuFontSize, FontWeight::Normal);
    int currentY = paddingV;

    for (int i = 0; i < (int)items.size(); i++)
    {
      const auto &item = items[i];

      if (item.type == ContextMenuItem::Type::Separator)
      {
        int sepY = currentY + separatorHeight / 2;
        painter.drawLine(paddingH, sepY, menuW - paddingH, sepY,
                         separatorColor, 1);
        currentY += separatorHeight;
      }
      else if (item.type == ContextMenuItem::Type::Widget && item.widget)
      {
        int rowH = _widgetItemHeight(item);

        if (i == hoveredIndex)
          painter.fillRect(2, currentY, menuW - 4, rowH, itemHoverColor);

        auto *ui = FluxUI::getCurrentInstance();
        if (ui)
        {
          if (item.widget->needsLayout)
          {
            item.widget->computeLayout(
                ctx,
                BoxConstraints::tight(menuW - paddingH * 2, rowH),
                fontCache);
          }
          item.widget->x = paddingH;
          item.widget->y = currentY;
          item.widget->positionChildren(
              item.widget->x + item.widget->paddingLeft,
              item.widget->y + item.widget->paddingTop,
              item.widget->width - item.widget->paddingLeft - item.widget->paddingRight,
              item.widget->height - item.widget->paddingTop - item.widget->paddingBottom);
          item.widget->render(ctx, fontCache);
        }

        currentY += rowH;
      }
      else
      {
        if (i == hoveredIndex && item.enabled)
          painter.fillRect(2, currentY, menuW - 4, itemHeight, itemHoverColor);

        std::wstring wlabel = toWideString(item.label);
        Color textCol = item.enabled ? itemTextColor : itemDisabledColor;
        painter.drawText(
            wlabel, paddingH, currentY, menuW - paddingH * 2, itemHeight, font,
            textCol, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        currentY += itemHeight;
      }
    }
  }

  // ── OverlayContent input handlers ───────────────────────────────────────
  // Coordinates are already local to the menu's own rect — directly
  // equivalent to the old (mx - menuClientX, my - menuClientY) math.

  bool onOverlayMouseDown(int localX, int localY) override
  {
    int relativeY = localY - paddingV;
    int itemIdx = getItemIndexAtY(relativeY);

    if (itemIdx >= 0 && itemIdx < (int)items.size())
    {
      const auto &item = items[itemIdx];

      if (item.type == ContextMenuItem::Type::Widget && item.widget)
      {
        // NOTE: preserved as-is from the original — this still closes the
        // menu before the embedded widget sees the click, which contradicts
        // the "the widget decides whether to stay open" contract documented
        // on ContextMenuItem::Widget(). Flagging again since it survived
        // the migration unchanged; worth fixing separately if you want it.
        closeMenu();
        item.widget->handleMouseDown(localX, localY);
        return true;
      }
      if (item.type == ContextMenuItem::Type::Action && item.enabled)
      {
        if (item.action)
          item.action();
        closeMenu();
        return true;
      }
    }
    closeMenu();
    return true;
  }

  bool onOverlayMouseMove(int localX, int localY) override
  {
    if (localX >= 0 && localX < menuW && localY >= 0 && localY < menuH)
    {
      int relativeY = localY - paddingV;
      int itemIdx = getItemIndexAtY(relativeY);

      if (itemIdx >= 0 && itemIdx < (int)items.size())
      {
        const auto &item = items[itemIdx];
        if (item.type == ContextMenuItem::Type::Widget && item.widget)
          item.widget->handleMouseMove(localX, localY);
      }

      if (itemIdx != hoveredIndex)
      {
        hoveredIndex = itemIdx;
        selectedIndex = itemIdx;
        refreshPopupIfOpen_();
        return true;
      }
    }
    else if (hoveredIndex != -1)
    {
      hoveredIndex = -1;
      refreshPopupIfOpen_();
      return true;
    }
    return false;
  }

  bool onOverlayRightClick(int, int) override
  {
    if (!isOpen)
      return false;
    closeMenu();
    return true;
  }

  void onOverlayOutsideClick() override { closeMenu(); }

  bool onOverlayKeyDown(int keyCode) override
  {
    if (!isOpen || items.empty())
      return false;
    switch (keyCode)
    {
    case Key::Escape:
      closeMenu();
      return true;
    case Key::Up:
      moveToPrevious();
      return true;
    case Key::Down:
      moveToNext();
      return true;
    case Key::Home:
      selectedIndex = hoveredIndex = findFirstActionIndex();
      refreshPopupIfOpen_();
      return true;
    case Key::End:
      selectedIndex = hoveredIndex = findLastActionIndex();
      refreshPopupIfOpen_();
      return true;
    case Key::Return:
    case Key::Space:
      if (selectedIndex >= 0 && selectedIndex < (int)items.size())
      {
        const auto &item = items[selectedIndex];
        if (item.type == ContextMenuItem::Type::Widget && item.widget)
        {
          int cx = item.widget->x + item.widget->width / 2;
          int cy = item.widget->y + item.widget->height / 2;
          closeMenu();
          item.widget->handleMouseDown(cx, cy);
          return true;
        }
        if (item.type == ContextMenuItem::Type::Action && item.enabled)
        {
          if (item.action)
            item.action();
          closeMenu();
          return true;
        }
      }
      return true;
    }
    return false;
  }

private:
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

  void chainAnchorRightClick(Widget *anchor)
  {
    std::function<bool(int, int)> previous = anchor->onRightClick;
    anchor->onRightClick = [this, anchor, previous](int mx, int my)
    {
      if (mx >= anchor->x && mx < anchor->x + anchor->width &&
          my >= anchor->y && my < anchor->y + anchor->height)
      {
        openMenuAt(mx, my);
        return true;
      }
      if (previous)
        return previous(mx, my);
      return false;
    };
  }

  void openMenuAt(int clientX, int clientY)
  {
    if (isOpen || items.empty())
      return;

    auto *ui = FluxUI::getCurrentInstance();
    if (!ui)
      return;

    _layoutWidgetItems(ui);
    computeMenuGeometry(); // size only — positioning/clamping is the manager's job now

    isOpen = true;
    hoveredIndex = -1;
    selectedIndex = findFirstActionIndex();

    ui->overlays().show(this, clientX, clientY,
                        menuW + shadowOffset, menuH + shadowOffset,
                        150, ui->getFontCache());
  }

  void closeMenu()
  {
    if (!isOpen)
      return;
    isOpen = false;
    hoveredIndex = -1;
    selectedIndex = -1;
    if (auto *ui = FluxUI::getCurrentInstance())
      ui->overlays().hide(this);
  }

  void _layoutWidgetItems(FluxUI *ui)
  {
    auto mc = ui->getMeasureContext();
    FontCache &fc = ui->getFontCache();

    for (auto &item : items)
    {
      if (item.type == ContextMenuItem::Type::Widget && item.widget)
      {
        if (item.widget->needsLayout || item.widget->height == 0)
        {
          item.widget->computeLayout(
              mc.ctx,
              BoxConstraints::loose(minWidth - paddingH * 2, kUnbounded),
              fc);
        }
      }
    }
  }

  // Pure size calculation now — no screen coordinates, no monitor
  // clamping. OverlayManager::show() handles all of that internally.
  void computeMenuGeometry()
  {
    static constexpr int kGlyphWidthPx = 7;

    int maxLabelWidth = 0;
    for (const auto &item : items)
    {
      if (item.type == ContextMenuItem::Type::Action)
      {
        std::wstring wlabel = toWideString(item.label);
        int lw = static_cast<int>(wlabel.size()) * kGlyphWidthPx;
        maxLabelWidth = std::max(maxLabelWidth, lw);
      }
      else if (item.type == ContextMenuItem::Type::Widget && item.widget)
      {
        int lw = item.widget->width > 0 ? item.widget->width : item.widget->minWidth;
        maxLabelWidth = std::max(maxLabelWidth, lw);
      }
    }
    menuW = std::max(minWidth, maxLabelWidth + paddingH * 2);

    int totalH = paddingV * 2;
    for (const auto &item : items)
      totalH += _itemHeight(item);
    menuH = totalH;
  }

  void refreshPopupIfOpen_()
  {
    if (!isOpen)
      return;
    if (auto *ui = FluxUI::getCurrentInstance())
      ui->overlays().refresh(this, ui->getFontCache());
  }

  void moveToPrevious()
  {
    if (selectedIndex < 0)
      selectedIndex = findFirstActionIndex();
    else
    {
      int prev = selectedIndex - 1;
      while (prev >= 0)
      {
        if (items[prev].type != ContextMenuItem::Type::Separator)
        {
          selectedIndex = prev;
          break;
        }
        prev--;
      }
      if (prev < 0)
        selectedIndex = findLastActionIndex();
    }
    hoveredIndex = selectedIndex;
    refreshPopupIfOpen_();
  }

  void moveToNext()
  {
    if (selectedIndex < 0)
      selectedIndex = findFirstActionIndex();
    else
    {
      int next = selectedIndex + 1;
      while (next < (int)items.size())
      {
        if (items[next].type != ContextMenuItem::Type::Separator)
        {
          selectedIndex = next;
          break;
        }
        next++;
      }
      if (next >= (int)items.size())
        selectedIndex = findFirstActionIndex();
    }
    hoveredIndex = selectedIndex;
    refreshPopupIfOpen_();
  }

  int findFirstActionIndex() const
  {
    for (int i = 0; i < (int)items.size(); i++)
      if (items[i].type != ContextMenuItem::Type::Separator)
        return i;
    return 0;
  }
  int findLastActionIndex() const
  {
    for (int i = (int)items.size() - 1; i >= 0; i--)
      if (items[i].type != ContextMenuItem::Type::Separator)
        return i;
    return 0;
  }

  int getItemIndexAtY(int relativeY) const
  {
    int currentY = 0;
    for (int i = 0; i < (int)items.size(); i++)
    {
      int h = _itemHeight(items[i]);
      if (relativeY >= currentY && relativeY < currentY + h)
      {
        if (items[i].type == ContextMenuItem::Type::Separator)
          return -1;
        return i;
      }
      currentY += h;
    }
    return -1;
  }
};

// ============================================================================
// DIALOG WIDGET
// ============================================================================

class DialogWidget : public Widget, public OverlayContent
{
private:
  bool popupShown_ = false;
  bool contentDirty_ = true;

  int dialogX_ = 0, dialogY_ = 0; // box position within the overlay rect
  int winW_ = 0, winH_ = 0;       // client size captured when opened

public:
  bool isOpen = false;
  WidgetPtr content;

  int dialogWidth = 400;
  int dialogHeight = 300;
  Color overlayColor = Color::fromRGBA(0, 0, 0, 128);
  Color dialogBgColor = Color::fromRGBA(255, 255, 255, 255);
  Color dialogBorderColor = Color::fromRGBA(200, 200, 200, 255);
  int dialogBorderRadius = 8;
  int dialogPadding = 24;

  std::function<void()> onClose;
  bool closeOnClickOutside = true;
  bool closeOnEscape = true;

  DialogWidget() { hasBackground = false; }

  void onDetach() override
  {
    if (isOpen)
      close();
    Widget::onDetach();
  }

  // ── OverlayContent ────────────────────────────────────────────────────
  OverlayPolicy overlayPolicy() const override
  {
    // A dialog owns the whole screen while open — nothing below it should
    // see clicks, hover, or keys.
    return {/*modal=*/true, /*blocksHoverBelow=*/true, /*capturesKeyboard=*/true};
  }

  void renderOverlay(GraphicsContext &ctx, FontCache &fontCache) override
  {
    if (!isOpen)
      return;

    Painter painter(ctx);

    // Dim scrim across the whole overlay rect (the full client area —
    // see open()).
    painter.fillRectAlpha(0, 0, winW_, winH_, overlayColor);

    dialogX_ = (winW_ - dialogWidth) / 2;
    dialogY_ = (winH_ - dialogHeight) / 2;

    painter.fillRoundedRect(dialogX_, dialogY_, dialogWidth, dialogHeight,
                            dialogBorderRadius, dialogBgColor);
    painter.drawBorder(dialogX_, dialogY_, dialogWidth, dialogHeight,
                       dialogBorderRadius, dialogBorderColor, 1);

    if (content)
    {
      layoutContentIfNeeded(ctx, fontCache);
      content->render(ctx, fontCache);
    }
  }

  // Coordinates are local to the overlay rect, which is the full client
  // area starting at (0,0) — numerically identical to absolute client
  // coordinates, same as ToastWidget.
  bool onOverlayMouseDown(int mx, int my) override
  {
    if (!isOpen)
      return false;

    if (mx < dialogX_ || mx >= dialogX_ + dialogWidth ||
        my < dialogY_ || my >= dialogY_ + dialogHeight)
    {
      if (closeOnClickOutside)
        close();
      return true; // modal — swallow regardless of whether we closed
    }

    if (content)
      dispatchContentMouseDown(mx, my);

    return true;
  }

  bool onOverlayMouseUp(int mx, int my) override
  {
    if (!isOpen || !content)
      return false;
    return broadcastMouseEvent(content.get(), mx, my,
                               [](Widget *w, int x, int y)
                               { return w->handleMouseUp(x, y); });
  }

  bool onOverlayMouseMove(int mx, int my) override
  {
    if (!isOpen || !content)
      return false;
    // blocksHoverBelow keeps FluxUI from running updateHoverStates on
    // root while the dialog is open, so the dialog has to drive hover
    // for its own content subtree itself (otherwise buttons inside the
    // dialog would never show hover feedback).
    return updateHoverStates(content.get(), mx, my);
  }

  bool onOverlayKeyDown(int keyCode) override
  {
    if (!isOpen)
      return false;
    if (closeOnEscape && keyCode == Key::Escape)
    {
      close();
      return true;
    }
    return true; // modal — swallow all keys while open, handled or not
  }

  void onOverlayOutsideClick() override
  {
    // The dialog's overlay rect covers the full client area, so
    // onOverlayMouseDown already handles "inside the scrim, outside the
    // box". This only fires if a click somehow lands outside the
    // overlay's own rect entirely — shouldn't happen for a full-window
    // overlay, but close defensively if it ever does.
    if (closeOnClickOutside)
      close();
  }

  // ── Builder API ───────────────────────────────────────────────────────
  std::shared_ptr<DialogWidget> setContent(WidgetPtr child)
  {
    content = child;
    if (content)
      content->parent = this;
    contentDirty_ = true;
    return self_();
  }
  std::shared_ptr<DialogWidget> setSize(int w, int h)
  {
    dialogWidth = w;
    dialogHeight = h;
    contentDirty_ = true;
    return self_();
  }
  std::shared_ptr<DialogWidget> setCloseOnClickOutside(bool value)
  {
    closeOnClickOutside = value;
    return self_();
  }
  std::shared_ptr<DialogWidget> setCloseOnEscape(bool value)
  {
    closeOnEscape = value;
    return self_();
  }
  std::shared_ptr<DialogWidget> setOnClose(std::function<void()> cb)
  {
    onClose = cb;
    return self_();
  }
  std::shared_ptr<DialogWidget> setOverlayColor(Color c)
  {
    overlayColor = c;
    return self_();
  }

  // ── Normal Widget — zero-size anchor, purely so onDetach() fires ──────
  void computeLayout(GraphicsContext &, const BoxConstraints &, FontCache &) override
  {
    width = 0;
    height = 0;
    needsLayout = false;
  }
  void positionChildren(int, int, int, int) override {}
  void render(GraphicsContext &, FontCache &) override { needsPaint = false; }

  // ── Open / Close ──────────────────────────────────────────────────────
  void open()
  {
    if (isOpen)
      return;

    auto *ui = FluxUI::getCurrentInstance();
    if (!ui)
      return;

    isOpen = true;
    contentDirty_ = true;

    auto sz = ui->getClientSize();
    winW_ = sz.width;
    winH_ = sz.height;
    dialogX_ = (winW_ - dialogWidth) / 2;
    dialogY_ = (winH_ - dialogHeight) / 2;

    ui->overlays().show(this, 0, 0, winW_, winH_, 200, ui->getFontCache());
    popupShown_ = true;
  }

  void close()
  {
    if (!isOpen)
      return;
    isOpen = false;

    auto *ui = FluxUI::getCurrentInstance();
    if (ui)
    {
      Widget *focused = ui->getFocusedWidget();
      if (focused && content && isDescendantOf(focused, content.get()))
        ui->setFocus(nullptr);
      if (popupShown_)
        ui->overlays().hide(this);
    }
    popupShown_ = false;

    if (onClose)
      onClose();
  }

private:
  std::shared_ptr<DialogWidget> self_()
  {
    return std::static_pointer_cast<DialogWidget>(shared_from_this());
  }

  void layoutContentIfNeeded(GraphicsContext &ctx, FontCache &fontCache)
  {
    int contentX = dialogX_ + dialogPadding;
    int contentY = dialogY_ + dialogPadding;
    int contentW = dialogWidth - dialogPadding * 2;
    int contentH = dialogHeight - dialogPadding * 2;

    if (contentDirty_ || content->needsLayout)
    {
      content->computeLayout(ctx, BoxConstraints::tight(contentW, contentH), fontCache);
      contentDirty_ = false;
    }

    content->x = contentX;
    content->y = contentY;
    content->positionChildren(
        contentX + content->paddingLeft,
        contentY + content->paddingTop,
        content->width - content->paddingLeft - content->paddingRight,
        content->height - content->paddingTop - content->paddingBottom);
  }

  bool dispatchContentMouseDown(int mx, int my)
  {
    auto *ui = FluxUI::getCurrentInstance();
    Widget *toFocus = nullptr;

    bool handled = findAndHandleMouseEvent(
        content.get(), mx, my,
        [mx, my, &toFocus](Widget *w)
        {
          bool h = w->handleMouseDown(mx, my);
          if (!h && w->onClick && mx >= w->x && mx < w->x + w->width &&
              my >= w->y && my < w->y + w->height)
          {
            w->onClick();
            h = true;
          }
          if (h && w->isFocusable)
            toFocus = w;
          return h;
        });

    if (handled)
    {
      if (toFocus && ui)
        ui->setFocus(toFocus);
      return true;
    }

    Widget *clicked = findWidgetAt(content.get(), mx, my);
    if (clicked)
    {
      if (clicked->onClick)
      {
        clicked->onClick();
        return true;
      }
      if (clicked->isFocusable)
      {
        clicked->handleMouseDown(mx, my);
        if (ui)
          ui->setFocus(clicked);
        return true;
      }
    }
    return false;
  }

  bool isDescendantOf(Widget *candidate, Widget *subtreeRoot)
  {
    Widget *current = candidate;
    while (current)
    {
      if (current == subtreeRoot)
        return true;
      current = current->parent;
    }
    return false;
  }
};

// ============================================================================
// TOOLTIP WIDGET
// ============================================================================

enum class TooltipPosition
{
  Above,
  Below,
  Auto
};

class TooltipWidget : public Widget, public OverlayContent
{
private:
  int tipW_ = 0, tipH_ = 0;

  std::string tipText;
  TooltipPosition preferredPosition = TooltipPosition::Auto;

  Color tipBgColor = Color::fromRGBA(50, 50, 50, 255);
  Color tipTextColor = Color::fromRGB(255, 255, 255);
  Color tipBorderColor = Color::fromRGBA(80, 80, 80, 255);
  int tipFontSize = 12;
  int tipPadH = 10;
  int tipPadV = 6;
  int tipBorderRadius = 4;
  int tipMaxWidth = 240;
  int tipGap = 6; // space between anchor and bubble
  int shadowOffset = 2;

public:
  bool isVisible = false;

  explicit TooltipWidget(WidgetPtr anchor, const std::string &tooltip)
      : tipText(tooltip)
  {
    if (anchor)
    {
      addChild(anchor);
      chainAnchorHover(anchor.get());
    }
  }

  void onDetach() override
  {
    if (isVisible)
      closeTooltip();
    Widget::onDetach();
  }

  // ── OverlayContent ────────────────────────────────────────────────────
  OverlayPolicy overlayPolicy() const override
  {
    // Tooltips are purely informational — never modal, never eat hover
    // from the tree below, never capture keyboard. Whatever is under the
    // cursor behaves exactly as if the tooltip weren't there.
    return {/*modal=*/false, /*blocksHoverBelow=*/false, /*capturesKeyboard=*/false};
  }

  void renderOverlay(GraphicsContext &ctx, FontCache &fontCache) override
  {
    if (!isVisible || tipText.empty())
      return;

    Painter painter(ctx);

    painter.fillRoundedRect(shadowOffset, shadowOffset, tipW_, tipH_,
                            tipBorderRadius, Color::fromRGBA(0, 0, 0, 60));
    painter.fillRoundedRect(0, 0, tipW_, tipH_, tipBorderRadius, tipBgColor);
    painter.drawBorder(0, 0, tipW_, tipH_, tipBorderRadius, tipBorderColor, 1);

    std::wstring wtip = toWideString(tipText);
    NativeFont font = fontCache.getFont(tipFontSize, FontWeight::Normal);
    painter.drawText(wtip, tipPadH, tipPadV, tipW_ - tipPadH * 2,
                     tipH_ - tipPadV * 2, font, tipTextColor,
                     DT_CENTER | DT_VCENTER | DT_WORDBREAK | DT_END_ELLIPSIS);
  }

  // A click anywhere means the user moved on from hovering — dismiss, but
  // never claim the click (return false) so it still reaches the anchor
  // or whatever's underneath.
  bool onOverlayMouseDown(int, int) override
  {
    closeTooltip();
    return false;
  }
  void onOverlayOutsideClick() override { closeTooltip(); }

  // ── Builder API ───────────────────────────────────────────────────────
  std::shared_ptr<TooltipWidget> setTooltipText(const std::string &t)
  {
    tipText = t;
    return self_();
  }
  std::shared_ptr<TooltipWidget> setPosition(TooltipPosition pos)
  {
    preferredPosition = pos;
    return self_();
  }
  std::shared_ptr<TooltipWidget> setTooltipBackground(Color color)
  {
    tipBgColor = color;
    return self_();
  }
  std::shared_ptr<TooltipWidget> setTooltipTextColor(Color color)
  {
    tipTextColor = color;
    return self_();
  }
  std::shared_ptr<TooltipWidget> setTooltipFontSize(int size)
  {
    tipFontSize = size;
    return self_();
  }
  std::shared_ptr<TooltipWidget> setTooltipMaxWidth(int w)
  {
    tipMaxWidth = w;
    return self_();
  }

  // ── Layout (anchor only — tooltip itself has no in-tree size) ─────────
  void computeLayout(GraphicsContext &ctx, const BoxConstraints &constraints,
                     FontCache &fontCache) override
  {
    if (autoWidth)
      width = constraints.maxWidth;
    if (autoHeight)
      height = constraints.maxHeight;

    if (!children.empty())
    {
      auto &anchor = children[0];
      anchor->computeLayout(ctx, constraints, fontCache);
      if (autoWidth)
        width = anchor->width;
      if (autoHeight)
        height = anchor->height;
    }
    applyConstraints();
    needsLayout = false;
  }

  void positionChildren(int, int, int, int) override
  {
    if (!children.empty())
    {
      auto &anchor = children[0];
      anchor->x = x;
      anchor->y = y;
      anchor->positionChildren(
          anchor->x + anchor->paddingLeft,
          anchor->y + anchor->paddingTop,
          anchor->width - anchor->paddingLeft - anchor->paddingRight,
          anchor->height - anchor->paddingTop - anchor->paddingBottom);
    }
  }

  void render(GraphicsContext &ctx, FontCache &fontCache) override
  {
    if (!children.empty())
      children[0]->render(ctx, fontCache);
    needsPaint = false;
  }

private:
  std::shared_ptr<TooltipWidget> self_()
  {
    return std::static_pointer_cast<TooltipWidget>(shared_from_this());
  }

  void chainAnchorHover(Widget *anchor)
  {
    HoverHandler previous = anchor->onHover;
    anchor->onHover = [this, previous](bool hovered)
    {
      if (hovered)
        openTooltip();
      else
        closeTooltip();
      if (previous)
        previous(hovered);
    };
  }

  // clientX/clientY are MAIN-WINDOW CLIENT coordinates, same space show()
  // expects. Win32's show() does its own screen-space monitor clamping on
  // top of this; the clamp here just keeps the bubble inside the window
  // itself, which matters on every platform including non-Win32 (where
  // show() does no clamping at all).
  void computeBubbleGeometry(FluxUI *ui, int &outClientX, int &outClientY)
  {
    std::wstring wtip = toWideString(tipText);
    int charW = static_cast<int>(tipFontSize * 0.62f);
    int lineH = tipFontSize + 4;
    int textW = static_cast<int>(wtip.size()) * charW;
    int maxTW = tipMaxWidth - tipPadH * 2;
    int lines = std::max(1, (textW + maxTW - 1) / maxTW);

    tipW_ = std::min(textW + tipPadH * 2, tipMaxWidth);
    tipH_ = lines * lineH + tipPadV * 2;

    int anchorCX = x + width / 2;
    int above = y - tipH_ - tipGap;
    int below = y + height + tipGap;
    bool fitsAbove = above >= 0;

    int clientX = anchorCX - tipW_ / 2;
    int clientY;
    if (preferredPosition == TooltipPosition::Above)
      clientY = fitsAbove ? above : 0;
    else if (preferredPosition == TooltipPosition::Below)
      clientY = below;
    else // Auto
      clientY = fitsAbove ? above : below;

    auto sz = ui->getClientSize();
    if (clientX + tipW_ > sz.width)
      clientX = sz.width - tipW_;
    if (clientX < 0)
      clientX = 0;
    if (clientY + tipH_ > sz.height)
      clientY = sz.height - tipH_;
    if (clientY < 0)
      clientY = 0;

    outClientX = clientX;
    outClientY = clientY;
  }

  void openTooltip()
  {
    if (isVisible || tipText.empty())
      return;

    auto *ui = FluxUI::getCurrentInstance();
    if (!ui)
      return;

    int clientX, clientY;
    computeBubbleGeometry(ui, clientX, clientY);

    isVisible = true;

    // zIndex 50: above the base widget tree, below dropdowns (100) and
    // context menus (150) — a tooltip should never sit on top of an open
    // menu/dropdown it happens to overlap.
    ui->overlays().show(this, clientX, clientY, tipW_, tipH_, 50,
                        ui->getFontCache());
  }

  void closeTooltip()
  {
    if (!isVisible)
      return;
    isVisible = false;
    if (auto *ui = FluxUI::getCurrentInstance())
      ui->overlays().hide(this);
  }
};

// ============================================================================
// DROPDOWN WIDGET
// ============================================================================

class DropdownWidget : public Widget, public OverlayContent
{
private:
  int listWidth_ = 0;

public:
  std::vector<std::string> options;
  int selectedIndex = -1;
  bool isOpen = false;
  int hoveredItemIndex = -1;

  int itemHeight = 32;
  int maxVisibleItems = 6;
  int arrowSize = 8;
  int scrollOffset = 0;

  Color dropdownBgColor = Color::fromRGB(255, 255, 255);
  Color dropdownBorderColor = Color::fromRGB(180, 180, 180);
  Color dropdownFocusedBorderColor = Color::fromRGB(33, 150, 243);
  Color placeholderColor = Color::fromRGB(150, 150, 150);
  Color itemHoverColor = Color::fromRGB(240, 240, 240);
  Color itemSelectedColor = Color::fromRGB(230, 245, 255);
  Color listBgColor = Color::fromRGB(255, 255, 255);
  Color listBorderColor = Color::fromRGB(200, 200, 200);
  Color arrowColor = Color::fromRGB(100, 100, 100);

  std::string placeholder = "Select an option...";
  std::function<void(int, const std::string &)> onSelectionChanged;

  DropdownWidget()
  {
    isFocusable = true;
    hasBorder = true;
    hasBackground = true;
    backgroundColor = dropdownBgColor;
    borderColor = dropdownBorderColor;
    borderWidth = 1;
    borderRadius = 4;
    paddingLeft = 12;
    paddingRight = 30;
    paddingTop = paddingBottom = 8;
    height = 36;
    autoHeight = false;
  }

  // ── OverlayContent ────────────────────────────────────────────────────
  // overlayPolicy() not overridden — default (non-modal) is correct here.

  void renderOverlay(GraphicsContext &ctx, FontCache &fontCache) override
  {
    if (options.empty())
      return;
    Painter painter(ctx);

    int visibleCount = std::min((int)options.size(), maxVisibleItems);
    int listH = visibleCount * itemHeight + 2;

    painter.fillRect(0, 0, listWidth_, listH, listBgColor);
    painter.drawRectOutline(0, 0, listWidth_, listH, listBorderColor, 1);
    painter.pushClipRect(1, 1, listWidth_ - 2, listH - 2);

    NativeFont font = fontCache.getFont(fontSize, fontWeight);
    int endIndex = std::min((int)options.size(), scrollOffset + visibleCount);
    for (int i = scrollOffset; i < endIndex; i++)
    {
      int itemY = 1 + (i - scrollOffset) * itemHeight;
      if (i == hoveredItemIndex)
        painter.fillRect(1, itemY, listWidth_ - 2, itemHeight, itemHoverColor);
      else if (i == selectedIndex)
        painter.fillRect(1, itemY, listWidth_ - 2, itemHeight, itemSelectedColor);

      std::wstring wopt = toWideString(options[i]);
      painter.drawText(wopt, 12, itemY, listWidth_ - 24, itemHeight, font,
                       Color::fromRGB(30, 30, 30),
                       DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    painter.popClipRect();
  }

  bool onOverlayMouseDown(int localX, int localY) override
  {
    int visibleCount = std::min((int)options.size(), maxVisibleItems);
    int listH = visibleCount * itemHeight + 2;
    if (localX < 0 || localX >= listWidth_ || localY < 0 || localY >= listH)
    {
      closeDropdown();
      return true;
    }
    int itemIndex = scrollOffset + ((localY - 1) / itemHeight);
    if (itemIndex >= 0 && itemIndex < (int)options.size())
      selectItem(itemIndex);
    closeDropdown();
    return true;
  }

  bool onOverlayMouseMove(int localX, int localY) override
  {
    int visibleCount = std::min((int)options.size(), maxVisibleItems);
    int listH = visibleCount * itemHeight + 2;
    if (localX >= 0 && localX < listWidth_ && localY >= 0 && localY < listH)
    {
      int itemIndex = scrollOffset + ((localY - 1) / itemHeight);
      if (itemIndex != hoveredItemIndex)
      {
        hoveredItemIndex = itemIndex;
        refreshDropdownPopup_();
        return true;
      }
    }
    else if (hoveredItemIndex != -1)
    {
      hoveredItemIndex = -1;
      refreshDropdownPopup_();
      return true;
    }
    return false;
  }

  bool onOverlayMouseWheel(int delta) override
  {
    int maxScroll = std::max(0, (int)options.size() - maxVisibleItems);
    scrollOffset = (delta > 0) ? std::max(0, scrollOffset - 1)
                               : std::min(maxScroll, scrollOffset + 1);
    refreshDropdownPopup_();
    return true;
  }

  bool onOverlayKeyDown(int keyCode) override
  {
    if (options.empty())
      return false;
    switch (keyCode)
    {
    case Key::Return:
    case Key::Space:
    {
      int idx = (hoveredItemIndex >= 0) ? hoveredItemIndex : selectedIndex;
      if (idx >= 0 && idx < (int)options.size())
        selectItem(idx);
      closeDropdown();
      return true;
    }
    case Key::Escape:
      closeDropdown();
      return true;
    case Key::Up:
      if (hoveredItemIndex < 0)
        hoveredItemIndex = std::max(0, selectedIndex);
      else if (hoveredItemIndex > 0)
        hoveredItemIndex--;
      ensureItemVisible(hoveredItemIndex);
      refreshDropdownPopup_();
      return true;
    case Key::Down:
      if (hoveredItemIndex < 0)
        hoveredItemIndex = std::max(0, selectedIndex);
      else if (hoveredItemIndex < (int)options.size() - 1)
        hoveredItemIndex++;
      ensureItemVisible(hoveredItemIndex);
      refreshDropdownPopup_();
      return true;
    case Key::Home:
      hoveredItemIndex = 0;
      scrollOffset = 0;
      refreshDropdownPopup_();
      return true;
    case Key::End:
      hoveredItemIndex = (int)options.size() - 1;
      scrollOffset = std::max(0, (int)options.size() - maxVisibleItems);
      refreshDropdownPopup_();
      return true;
    }
    return false;
  }

  void onOverlayOutsideClick() override { closeDropdown(); }

  // ── Normal Widget — closed-state box ─────────────────────────────────
  void computeLayout(GraphicsContext &, const BoxConstraints &constraints, FontCache &) override
  {
    if (autoWidth)
      width = constraints.maxWidth;
    applyConstraints();
    needsLayout = false;
  }

  void render(GraphicsContext &ctx, FontCache &fontCache) override
  {
    borderColor = isFocused ? dropdownFocusedBorderColor : dropdownBorderColor;
    drawRoundedRectangle(ctx);

    Painter painter(ctx);
    NativeFont font = fontCache.getFont(fontSize, fontWeight);
    Color textCol = (selectedIndex >= 0 && selectedIndex < (int)options.size())
                        ? getCurrentTextColor()
                        : placeholderColor;
    const std::string &label = (selectedIndex >= 0 && selectedIndex < (int)options.size())
                                   ? options[selectedIndex]
                                   : placeholder;

    std::wstring wlabel = toWideString(label);
    painter.drawText(wlabel, x + paddingLeft, y + paddingTop,
                     width - paddingLeft - paddingRight, height - paddingTop - paddingBottom,
                     font, textCol, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    int arrowX = x + width - paddingRight + 10;
    int arrowY = y + height / 2;
    int hs = arrowSize / 2, vs = arrowSize / 4;
    if (isOpen)
    {
      painter.drawLine(arrowX - hs, arrowY + vs, arrowX, arrowY - vs, arrowColor, 2);
      painter.drawLine(arrowX, arrowY - vs, arrowX + hs, arrowY + vs, arrowColor, 2);
    }
    else
    {
      painter.drawLine(arrowX - hs, arrowY - vs, arrowX, arrowY + vs, arrowColor, 2);
      painter.drawLine(arrowX, arrowY + vs, arrowX + hs, arrowY - vs, arrowColor, 2);
    }
    needsPaint = false;
  }

  bool handleMouseDown(int mx, int my) override
  {
    if (isOpen)
      return false; // manager routes to onOverlayMouseDown while open
    if (mx >= x && mx < x + width && my >= y && my < y + height)
    {
      openDropdown();
      return true;
    }
    return false;
  }

  bool handleKeyDown(int keyCode) override
  {
    if (isOpen)
      return false; // manager routes to onOverlayKeyDown while open
    if (options.empty())
      return false;
    switch (keyCode)
    {
    case Key::Return:
    case Key::Space:
      openDropdown();
      hoveredItemIndex = selectedIndex;
      if (hoveredItemIndex >= 0)
        ensureItemVisible(hoveredItemIndex);
      markNeedsPaint();
      return true;
    case Key::Up:
      if (selectedIndex > 0)
        selectItem(selectedIndex - 1);
      return true;
    case Key::Down:
      if (selectedIndex < (int)options.size() - 1)
        selectItem(selectedIndex + 1);
      return true;
    case Key::Home:
      selectItem(0);
      return true;
    case Key::End:
      selectItem((int)options.size() - 1);
      return true;
    }
    return false;
  }

  bool handleFocus(bool focused) override
  {
    isFocused = focused;
    if (!focused && isOpen)
      closeDropdown();
    markNeedsPaint();
    return true;
  }

  OverlayPolicy overlayPolicy() const override
  {
    return {/*modal=*/true, /*blocksHoverBelow=*/false, /*capturesKeyboard=*/true};
  }

  // ── Builder methods — unchanged from before ──────────────────────────
  std::shared_ptr<DropdownWidget> setOptions(const std::vector<std::string> &opts)
  {
    options = opts;
    if (selectedIndex >= (int)options.size())
      selectedIndex = -1;
    scrollOffset = 0;
    hoveredItemIndex = -1;
    if (isOpen)
      closeDropdown();
    markNeedsPaint();
    return std::static_pointer_cast<DropdownWidget>(shared_from_this());
  }
  std::shared_ptr<DropdownWidget> setPlaceholder(const std::string &ph)
  {
    placeholder = ph;
    markNeedsPaint();
    return std::static_pointer_cast<DropdownWidget>(shared_from_this());
  }
  std::shared_ptr<DropdownWidget> setItemHeight(int h)
  {
    itemHeight = h;
    markNeedsPaint();
    return std::static_pointer_cast<DropdownWidget>(shared_from_this());
  }
  std::shared_ptr<DropdownWidget> setMaxVisibleItems(int count)
  {
    maxVisibleItems = count;
    markNeedsPaint();
    return std::static_pointer_cast<DropdownWidget>(shared_from_this());
  }
  std::shared_ptr<DropdownWidget> setOnSelectionChanged(std::function<void(int, const std::string &)> cb)
  {
    onSelectionChanged = cb;
    return std::static_pointer_cast<DropdownWidget>(shared_from_this());
  }
  std::shared_ptr<DropdownWidget> setSelectedIndex(State<int> &state)
  {
    selectedIndex = state.get();
    state.bindProperty(shared_from_this(), [](Widget *w, const int &val)
                       { static_cast<DropdownWidget *>(w)->selectedIndex = val; }, false);
    boundIntState = &state;
    return std::static_pointer_cast<DropdownWidget>(shared_from_this());
  }
  std::shared_ptr<DropdownWidget> setSelectedValue(State<std::string> &state)
  {
    selectedIndex = findOptionIndex(state.get());
    state.bindProperty(shared_from_this(), [](Widget *w, const std::string &val)
                       { static_cast<DropdownWidget *>(w)->selectedIndex = static_cast<DropdownWidget *>(w)->findOptionIndex(val); }, false);
    boundStringState = &state;
    return std::static_pointer_cast<DropdownWidget>(shared_from_this());
  }
  std::shared_ptr<DropdownWidget> setWidth(int w)
  {
    width = w;
    autoWidth = false;
    return std::static_pointer_cast<DropdownWidget>(shared_from_this());
  }

private:
  State<int> *boundIntState = nullptr;
  State<std::string> *boundStringState = nullptr;

  void openDropdown()
  {
    auto *ui = FluxUI::getCurrentInstance();
    if (!ui || isOpen)
      return;
    isOpen = true;
    hoveredItemIndex = -1;
    scrollOffset = 0;
    listWidth_ = width;

    int visibleCount = std::min((int)options.size(), maxVisibleItems);
    int listH = visibleCount * itemHeight + 2;

    ui->overlays().show(this, x, y + height + 2, listWidth_, listH, 100, ui->getFontCache());
    markNeedsPaint();
  }

  void closeDropdown()
  {
    if (!isOpen)
      return;
    isOpen = false;
    hoveredItemIndex = -1;
    if (auto *ui = FluxUI::getCurrentInstance())
      ui->overlays().hide(this);
    markNeedsPaint();
  }

  void refreshDropdownPopup_()
  {
    if (!isOpen)
      return;
    if (auto *ui = FluxUI::getCurrentInstance())
      ui->overlays().refresh(this, ui->getFontCache());
  }

  void selectItem(int index)
  {
    if (index < 0 || index >= (int)options.size())
      return;
    selectedIndex = index;
    if (onSelectionChanged)
      onSelectionChanged(selectedIndex, options[selectedIndex]);
    if (boundIntState)
      boundIntState->set(selectedIndex);
    if (boundStringState)
      boundStringState->set(options[selectedIndex]);
    markNeedsPaint();
  }

  void ensureItemVisible(int index)
  {
    if (index < scrollOffset)
      scrollOffset = index;
    else if (index >= scrollOffset + maxVisibleItems)
      scrollOffset = index - maxVisibleItems + 1;
  }

  int findOptionIndex(const std::string &value) const
  {
    for (int i = 0; i < (int)options.size(); i++)
      if (options[i] == value)
        return i;
    return -1;
  }
};

// ============================================================================
// FACTORY FUNCTIONS
// ============================================================================

using DropdownWidgetPtr = std::shared_ptr<DropdownWidget>;
using TooltipWidgetPtr = std::shared_ptr<TooltipWidget>;
using DialogWidgetPtr = std::shared_ptr<DialogWidget>;
using ContextMenuWidgetPtr = std::shared_ptr<ContextMenuWidget>;

inline DropdownWidgetPtr
Dropdown(const std::vector<std::string> &options = {})
{
  auto w = std::make_shared<DropdownWidget>();
  if (!options.empty())
    w->setOptions(options);
  return w;
}

inline TooltipWidgetPtr Tooltip(WidgetPtr anchor, const std::string &tooltip)
{
  return std::make_shared<TooltipWidget>(anchor, tooltip);
}

inline DialogWidgetPtr Dialog(WidgetPtr content = nullptr)
{
  auto w = std::make_shared<DialogWidget>();
  if (content)
    w->setContent(content);
  return w;
}

inline ContextMenuWidgetPtr
ContextMenu(WidgetPtr anchor, const std::vector<ContextMenuItem> &items)
{
  return std::make_shared<ContextMenuWidget>(anchor, items);
}

#endif // FLUX_OVERLAYS_HPP