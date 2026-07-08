#ifndef FLUX_OVERLAYS_HPP
#define FLUX_OVERLAYS_HPP

#include "flux_structure.hpp"

#include "flux/flux_app.hpp"
#include "flux/flux_core.hpp"
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

class ContextMenuWidget : public Widget
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
  // ── Popup-body surface ─────────────────────────────────────────────────
  // See DropdownWidget::ListSurface for the general pattern. As a nested
  // class it has full access to ContextMenuWidget's private members
  // through `owner`. All draw/hit-test math below now works in absolute
  // client coordinates (x/y is the surface's own real position), whereas
  // the old renderOverlay()/onOverlay* body worked in local (0,0-relative)
  // coordinates baked in by OverlayManager::renderAll() per-platform.
  class MenuSurface : public Widget
  {
  public:
    ContextMenuWidget *owner = nullptr;

    void render(GraphicsContext &ctx, FontCache &fontCache) override
    {
      if (!owner || !owner->isOpen || owner->items.empty())
        return;

      Painter painter(ctx, this);

      painter.fillRoundedRect(x + owner->shadowOffset, y + owner->shadowOffset,
                              owner->menuW, owner->menuH,
                              owner->menuBorderRadius, Color::fromRGBA(0, 0, 0, 60));

      painter.fillRoundedRect(x, y, owner->menuW, owner->menuH,
                              owner->menuBorderRadius, owner->menuBgColor);
      painter.drawBorder(x, y, owner->menuW, owner->menuH,
                         owner->menuBorderRadius, owner->menuBorderColor, 1);

      NativeFont font = fontCache.getFont(owner->menuFontSize, FontWeight::Normal);
      int currentY = y + owner->paddingV;

      for (int i = 0; i < (int)owner->items.size(); i++)
      {
        const auto &item = owner->items[i];

        if (item.type == ContextMenuItem::Type::Separator)
        {
          int sepY = currentY + owner->separatorHeight / 2;
          painter.drawLine(x + owner->paddingH, sepY,
                           x + owner->menuW - owner->paddingH, sepY,
                           owner->separatorColor, 1);
          currentY += owner->separatorHeight;
        }
        else if (item.type == ContextMenuItem::Type::Widget && item.widget)
        {
          int rowH = owner->_widgetItemHeight(item);

          if (i == owner->hoveredIndex)
            painter.fillRect(x + 2, currentY, owner->menuW - 4, rowH,
                             owner->itemHoverColor);

          auto *ui = FluxUI::getCurrentInstance();
          if (ui)
          {
            if (item.widget->needsLayout)
            {
              item.widget->computeLayout(
                  ctx,
                  BoxConstraints::tight(owner->menuW - owner->paddingH * 2, rowH),
                  fontCache);
            }
            item.widget->x = x + owner->paddingH;
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
          if (i == owner->hoveredIndex && item.enabled)
            painter.fillRect(x + 2, currentY, owner->menuW - 4, owner->itemHeight,
                             owner->itemHoverColor);

          std::wstring wlabel = toWideString(item.label);
          Color textCol = item.enabled ? owner->itemTextColor : owner->itemDisabledColor;
          painter.drawText(
              wlabel, x + owner->paddingH, currentY,
              owner->menuW - owner->paddingH * 2, owner->itemHeight, font,
              textCol, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
          currentY += owner->itemHeight;
        }
      }
      needsPaint = false;
    }

    bool handleMouseDown(int mx, int my) override
    {
      if (!owner)
        return false;
      int relativeY = (my - y) - owner->paddingV;
      int itemIdx = owner->getItemIndexAtY(relativeY);

      if (itemIdx >= 0 && itemIdx < (int)owner->items.size())
      {
        const auto &item = owner->items[itemIdx];

        if (item.type == ContextMenuItem::Type::Widget && item.widget)
        {
          // NOTE: preserved as-is from the pre-migration code — this still
          // closes the menu before the embedded widget sees the click,
          // which contradicts the "the widget decides whether to stay
          // open" contract documented on ContextMenuItem::Widget(). Not
          // fixed here to keep this a faithful behavioral port; worth
          // addressing separately if you want it.
          owner->closeMenu();
          item.widget->handleMouseDown(mx, my);
          return true;
        }
        if (item.type == ContextMenuItem::Type::Action && item.enabled)
        {
          if (item.action)
            item.action();
          owner->closeMenu();
          return true;
        }
      }
      owner->closeMenu();
      return true;
    }

    bool handleMouseMove(int mx, int my) override
    {
      if (!owner)
        return false;
      int localX = mx - x, localY = my - y;
      if (localX >= 0 && localX < owner->menuW && localY >= 0 && localY < owner->menuH)
      {
        int relativeY = localY - owner->paddingV;
        int itemIdx = owner->getItemIndexAtY(relativeY);

        if (itemIdx >= 0 && itemIdx < (int)owner->items.size())
        {
          const auto &item = owner->items[itemIdx];
          if (item.type == ContextMenuItem::Type::Widget && item.widget)
            item.widget->handleMouseMove(mx, my);
        }

        if (itemIdx != owner->hoveredIndex)
        {
          owner->hoveredIndex = itemIdx;
          owner->selectedIndex = itemIdx;
          owner->refreshPopupIfOpen_();
          return true;
        }
      }
      else if (owner->hoveredIndex != -1)
      {
        owner->hoveredIndex = -1;
        owner->refreshPopupIfOpen_();
        return true;
      }
      return false;
    }

    bool handleRightClick(int, int) override
    {
      if (!owner || !owner->isOpen)
        return false;
      owner->closeMenu();
      return true;
    }

    void onOverlayOutsideClick() override
    {
      if (owner)
        owner->closeMenu();
    }

    bool handleKeyDown(int keyCode) override
    {
      if (!owner || !owner->isOpen || owner->items.empty())
        return false;
      switch (keyCode)
      {
      case Key::Escape:
        owner->closeMenu();
        return true;
      case Key::Up:
        owner->moveToPrevious();
        return true;
      case Key::Down:
        owner->moveToNext();
        return true;
      case Key::Home:
        owner->selectedIndex = owner->hoveredIndex = owner->findFirstActionIndex();
        owner->refreshPopupIfOpen_();
        return true;
      case Key::End:
        owner->selectedIndex = owner->hoveredIndex = owner->findLastActionIndex();
        owner->refreshPopupIfOpen_();
        return true;
      case Key::Return:
      case Key::Space:
        if (owner->selectedIndex >= 0 && owner->selectedIndex < (int)owner->items.size())
        {
          const auto &item = owner->items[owner->selectedIndex];
          if (item.type == ContextMenuItem::Type::Widget && item.widget)
          {
            int cx = item.widget->x + item.widget->width / 2;
            int cy = item.widget->y + item.widget->height / 2;
            owner->closeMenu();
            item.widget->handleMouseDown(cx, cy);
            return true;
          }
          if (item.type == ContextMenuItem::Type::Action && item.enabled)
          {
            if (item.action)
              item.action();
            owner->closeMenu();
            return true;
          }
        }
        return true;
      }
      return false;
    }
  };

  std::shared_ptr<MenuSurface> menuSurface_;

public:
  bool isOpen = false;

  explicit ContextMenuWidget(WidgetPtr anchor,
                             const std::vector<ContextMenuItem> &menuItems)
      : items(menuItems)
  {

    menuSurface_ = std::make_shared<MenuSurface>();
    menuSurface_->owner = this;
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

    menuSurface_->x = clientX;
    menuSurface_->y = clientY;
    menuSurface_->width = menuW + shadowOffset;
    menuSurface_->height = menuH + shadowOffset;
    ui->showOverlay(menuSurface_.get(), /*zIndex=*/150,
                    /*modal=*/true, /*blocksHoverBelow=*/false,
                    /*capturesKeyboard=*/true);
  }

  void closeMenu()
  {
    if (!isOpen)
      return;
    isOpen = false;
    hoveredIndex = -1;
    selectedIndex = -1;
    if (auto *ui = FluxUI::getCurrentInstance())
      ui->hideOverlay(menuSurface_.get());
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
      ui->refreshOverlay(menuSurface_.get());
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

class DialogWidget : public Widget
{
private:
  bool popupShown_ = false;
  bool contentDirty_ = true;
  int dialogX_ = 0, dialogY_ = 0; // box position within the overlay rect
  int winW_ = 0, winH_ = 0;       // client size captured when opened

  // ── Popup-body surface ─────────────────────────────────────────────────
  // Covers the full client area (x=0, y=0, width=winW_, height=winH_),
  // same footprint the old overlay rect used. Because the surface's own
  // origin is (0,0), every coordinate handleMouseDown/Move/Up receives is
  // already numerically identical to the old "local to the overlay rect"
  // coordinates — dialogX_/dialogY_ math below is untouched.
  class DialogSurface : public Widget
  {
  public:
    DialogWidget *owner = nullptr;

    void render(GraphicsContext &ctx, FontCache &fontCache) override
    {
      if (!owner || !owner->isOpen)
        return;

      Painter painter(ctx, this);

      // Dim scrim across the whole overlay rect (the full client area —
      // see DialogWidget::open()).
      painter.fillRectAlpha(x, y, width, height, owner->overlayColor);

      owner->dialogX_ = x + (width - owner->dialogWidth) / 2;
      owner->dialogY_ = y + (height - owner->dialogHeight) / 2;

      painter.fillRoundedRect(owner->dialogX_, owner->dialogY_,
                              owner->dialogWidth, owner->dialogHeight,
                              owner->dialogBorderRadius, owner->dialogBgColor);
      painter.drawBorder(owner->dialogX_, owner->dialogY_,
                         owner->dialogWidth, owner->dialogHeight,
                         owner->dialogBorderRadius, owner->dialogBorderColor, 1);

      if (owner->content)
      {
        owner->layoutContentIfNeeded(ctx, fontCache);
        owner->content->render(ctx, fontCache);
      }
      needsPaint = false;
    }

    bool handleMouseDown(int mx, int my) override
    {
      if (!owner || !owner->isOpen)
        return false;

      if (mx < owner->dialogX_ || mx >= owner->dialogX_ + owner->dialogWidth ||
          my < owner->dialogY_ || my >= owner->dialogY_ + owner->dialogHeight)
      {
        if (owner->closeOnClickOutside)
          owner->close();
        return true; // modal — swallow regardless of whether we closed
      }

      if (owner->content)
        owner->dispatchContentMouseDown(mx, my);

      return true;
    }

    bool handleMouseUp(int mx, int my) override
    {
      if (!owner || !owner->isOpen || !owner->content)
        return false;
      return broadcastMouseEvent(owner->content.get(), mx, my,
                                 [](Widget *w, int x2, int y2)
                                 { return w->handleMouseUp(x2, y2); });
    }

    bool handleMouseMove(int mx, int my) override
    {
      if (!owner || !owner->isOpen || !owner->content)
        return false;
      // owner's overlay entry sets blocksHoverBelow=true, so FluxUI skips
      // updateHoverStates on root while the dialog is open — the dialog
      // has to drive hover for its own content subtree itself (otherwise
      // buttons inside the dialog would never show hover feedback).
      return updateHoverStates(owner->content.get(), mx, my);
    }

    bool handleKeyDown(int keyCode) override
    {
      if (!owner || !owner->isOpen)
        return false;
      if (owner->closeOnEscape && keyCode == Key::Escape)
      {
        owner->close();
        return true;
      }
      return true; // modal — swallow all keys while open, handled or not
    }

    void onOverlayOutsideClick() override
    {
      // The dialog's overlay rect covers the full client area, so
      // handleMouseDown already handles "inside the scrim, outside the
      // box". This only fires if a click somehow lands outside the
      // surface's own rect entirely — shouldn't happen for a full-window
      // overlay, but close defensively if it ever does.
      if (owner && owner->closeOnClickOutside)
        owner->close();
    }
  };

  std::shared_ptr<DialogSurface> dialogSurface_;

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

  DialogWidget()
  {
    hasBackground = false;
    dialogSurface_ = std::make_shared<DialogSurface>();
    dialogSurface_->owner = this;
  }

  void onDetach() override
  {
    if (isOpen)
      close();
    Widget::onDetach();
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

    dialogSurface_->x = 0;
    dialogSurface_->y = 0;
    dialogSurface_->width = winW_;
    dialogSurface_->height = winH_;
    // A dialog owns the whole screen while open — nothing below it should
    // see clicks, hover, or keys.
    ui->showOverlay(dialogSurface_.get(), /*zIndex=*/200,
                    /*modal=*/true, /*blocksHoverBelow=*/true,
                    /*capturesKeyboard=*/true);
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
        ui->hideOverlay(dialogSurface_.get());
    }
    popupShown_ = false;

    if (onClose)
      onClose();
  }

private:
  friend class DialogSurface;
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
// FACTORY FUNCTIONS
// ============================================================================

using DialogWidgetPtr = std::shared_ptr<DialogWidget>;
using ContextMenuWidgetPtr = std::shared_ptr<ContextMenuWidget>;


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