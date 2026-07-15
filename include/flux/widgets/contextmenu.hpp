#ifndef FLUX_CONTEXT_MENU_HPP
#define FLUX_CONTEXT_MENU_HPP



#include "flux/flux_app.hpp"
#include "flux/flux_core.hpp"
#include <algorithm>

#if (defined(__EMSCRIPTEN__) && defined(FLUX_WEB_RENDERER_DOM)) || defined(FLUX_SSR)
#include "flux/flux_dom_adapter.hpp"
extern void fluxDomEvictWidget(Widget *owner);
#endif

// ============================================================================
// CONTEXT MENU ITEM
// ============================================================================

// ============================================================================
// MENU TRIGGER
// ============================================================================
// RightClick — classic context menu (unchanged default behavior).
// LeftClick  — desktop-style pulldown/menu-bar: a normal left click on the
//              anchor opens the menu, flush against the anchor's bottom
//              edge (see openMenuAt call site in handleMouseDown below),
//              rather than at the raw click coordinates a right-click
//              context menu uses.
enum class MenuTrigger
{
  RightClick,
  LeftClick
};

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
#if (defined(__EMSCRIPTEN__) && defined(FLUX_WEB_RENDERER_DOM)) || defined(FLUX_SSR)
      if (IDomAdapter *adapter = getActiveDomAdapter())
      {
        _renderDom(adapter, ctx, fontCache);
        needsPaint = false;
        return;
      }
#endif
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

  private:
#if (defined(__EMSCRIPTEN__) && defined(FLUX_WEB_RENDERER_DOM)) || defined(FLUX_SSR)
    // One "bg" node for the menu chrome (background+border+shadow, same
    // collapsed-node trick TooltipWidget uses), plus one slot PER ITEM,
    // keyed by that item's index — "sep{i}" for a separator line,
    // "item{i}" for an action's label+hover, "row{i}" for a widget-item's
    // hover-highlight backdrop (the widget itself still renders through
    // its own widget->render() call, unmodified — it's parentless and
    // already gets root-absolute DOM placement correctly, same as
    // before this file had any DOM awareness at all).
    void _renderDom(IDomAdapter *adapter, GraphicsContext &ctx, FontCache &fontCache)
    {
      if (!owner || !owner->isOpen || owner->items.empty())
        return;
      char buf[24];
      auto px = [&](int v)
      { snprintf(buf, sizeof(buf), "%dpx", v); return std::string(buf); };
      char colbuf[48];
      auto rgba = [&](Color c)
      {
        snprintf(colbuf, sizeof(colbuf), "rgba(%d,%d,%d,%.3f)", c.r, c.g, c.b, c.a / 255.f);
        return std::string(colbuf);
      };

      DomNodeHandle bg = fluxDomEnsureNode(this, "div", "bg");
      fluxDomApplyRect(this, x, y, owner->menuW, owner->menuH, "bg");
      adapter->setStyle(bg, "background-color", rgba(owner->menuBgColor));
      adapter->setStyle(bg, "border", "1px solid " + rgba(owner->menuBorderColor));
      adapter->setStyle(bg, "border-radius", px(owner->menuBorderRadius));
      adapter->setStyle(bg, "box-shadow",
                        px(owner->shadowOffset) + " " + px(owner->shadowOffset) +
                            " 0 rgba(0,0,0,0.235)");
      adapter->setStyle(bg, "box-sizing", "border-box");
      adapter->setStyle(bg, "pointer-events", "none");
      adapter->setStyle(bg, "z-index", "10");

      int currentY = y + owner->paddingV;

      for (int i = 0; i < (int)owner->items.size(); i++)
      {
        const auto &item = owner->items[i];

        if (item.type == ContextMenuItem::Type::Separator)
        {
          std::string slot = "sep" + std::to_string(i);
          DomNodeHandle sep = fluxDomEnsureNode(this, "div", slot.c_str());
          int sepY = currentY + owner->separatorHeight / 2;
          fluxDomApplyRect(this, x + owner->paddingH, sepY,
                           owner->menuW - owner->paddingH * 2, 1, slot.c_str());
          adapter->setStyle(sep, "background-color", rgba(owner->separatorColor));
          adapter->setStyle(sep, "pointer-events", "none");
          adapter->setStyle(sep, "z-index", "11");
          currentY += owner->separatorHeight;
        }
        else if (item.type == ContextMenuItem::Type::Widget && item.widget)
        {
          int rowH = owner->_widgetItemHeight(item);
          std::string slot = "row" + std::to_string(i);
          DomNodeHandle row = fluxDomEnsureNode(this, "div", slot.c_str());
          fluxDomApplyRect(this, x + 2, currentY, owner->menuW - 4, rowH, slot.c_str());
          adapter->setStyle(row, "background-color",
                            (i == owner->hoveredIndex) ? rgba(owner->itemHoverColor) : "transparent");
          adapter->setStyle(row, "pointer-events", "none");
          adapter->setStyle(row, "z-index", "11");

          if (item.widget->needsLayout)
            item.widget->computeLayout(
                ctx, BoxConstraints::tight(owner->menuW - owner->paddingH * 2, rowH), fontCache);
          item.widget->x = x + owner->paddingH;
          item.widget->y = currentY;
          item.widget->positionChildren(
              item.widget->x + item.widget->paddingLeft,
              item.widget->y + item.widget->paddingTop,
              item.widget->width - item.widget->paddingLeft - item.widget->paddingRight,
              item.widget->height - item.widget->paddingTop - item.widget->paddingBottom);
          item.widget->render(ctx, fontCache);

          currentY += rowH;
        }
        else
        {
          std::string slot = "item" + std::to_string(i);
          DomNodeHandle node = fluxDomEnsureNode(this, "div", slot.c_str());
          fluxDomApplyRect(this, x + 2, currentY, owner->menuW - 4, owner->itemHeight, slot.c_str());
          bool highlighted = (i == owner->hoveredIndex && item.enabled);
          adapter->setStyle(node, "background-color",
                            highlighted ? rgba(owner->itemHoverColor) : "transparent");
          adapter->setStyle(node, "display", "flex");
          adapter->setStyle(node, "align-items", "center");
          adapter->setStyle(node, "padding-left", px(owner->paddingH - 2));
          adapter->setStyle(node, "box-sizing", "border-box");
          adapter->setStyle(node, "white-space", "nowrap");
          adapter->setStyle(node, "overflow", "hidden");
          adapter->setStyle(node, "text-overflow", "ellipsis");
          adapter->setStyle(node, "font-size", px(owner->menuFontSize));
          adapter->setStyle(node, "color",
                            rgba(item.enabled ? owner->itemTextColor : owner->itemDisabledColor));
          adapter->setStyle(node, "pointer-events", "none");
          adapter->setStyle(node, "z-index", "11");
          adapter->setText(node, item.label);
          currentY += owner->itemHeight;
        }
      }
    }
#endif
  };

  std::shared_ptr<MenuSurface> menuSurface_;

public:
  bool isOpen = false;

  std::shared_ptr<ContextMenuWidget> setTrigger(MenuTrigger t)
  {
    trigger_ = t;
    return std::static_pointer_cast<ContextMenuWidget>(shared_from_this());
  }
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

  // Only meaningful when trigger_ == LeftClick. findAndHandleMouseEvent
  // (flux_widget.hpp) checks CHILDREN first, then falls back to the
  // PARENT's own handleMouseDown if none of them claimed the click —
  // ContextMenuWidget is anchor's direct parent (see addChild(anchor)
  // above), so this fires exactly when a click lands inside the
  // anchor's bounds but the anchor itself didn't handle it (true for a
  // plain Flex/Container anchor; a real Button anchor would already
  // consume the click via its own handleMouseDown, so LeftClick trigger
  // is intended for non-interactive anchors — a Flex "menu bar item",
  // not a Button).
  bool handleMouseDown(int mx, int my) override
  {
    if (trigger_ != MenuTrigger::LeftClick)
      return false;
    if (mx < x || mx >= x + width || my < y || my >= y + height)
      return false;
    // Pulldown convention: open flush below the anchor's own box,
    // left-aligned to it — NOT at the raw click position a right-click
    // context menu uses. x/y/width/height here are already synced to
    // the anchor's box by computeLayout()/positionChildren() above.
    openMenuAt(x, y + height + 2);
    return true;
  }

private:
  MenuTrigger trigger_ = MenuTrigger::RightClick;

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

      if (trigger_ != MenuTrigger::RightClick)
        return previous ? previous(mx, my) : false;
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
#if (defined(__EMSCRIPTEN__) && defined(FLUX_WEB_RENDERER_DOM)) || defined(FLUX_SSR)
    // Same rationale as DropdownWidget/TooltipWidget — hideOverlay() only
    // stops future render() calls, it doesn't remove the DOM nodes the
    // last successful render already attached. MenuSurface's own slots
    // (bg/sep{i}/item{i}/row{i}) are evicted here; any Widget-type item's
    // OWN cached nodes are untouched by this call, since that widget owns
    // its lifecycle independently (it's typically a reusable widget the
    // caller constructed once, not something this menu should tear down).
    fluxDomEvictWidget(menuSurface_.get());
#endif
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
// FACTORY FUNCTIONS
// ============================================================================

using ContextMenuWidgetPtr = std::shared_ptr<ContextMenuWidget>;

inline ContextMenuWidgetPtr
ContextMenu(WidgetPtr anchor, const std::vector<ContextMenuItem> &items)
{
  return std::make_shared<ContextMenuWidget>(anchor, items);
}

// Convenience wrapper — identical widget, pre-configured for the
// desktop "menu bar pulldown" pattern: left-click opens, positioned
// flush below the anchor. Equivalent to
// ContextMenu(anchor, items)->setTrigger(MenuTrigger::LeftClick).
inline ContextMenuWidgetPtr
PulldownMenu(WidgetPtr anchor, const std::vector<ContextMenuItem> &items)
{
  auto w = std::make_shared<ContextMenuWidget>(anchor, items);
  w->setTrigger(MenuTrigger::LeftClick);
  return w;
}

#endif // FLUX_CONTEXT_MENU_HPP