
#ifndef FLUX_OVERLAYS_HPP
#define FLUX_OVERLAYS_HPP

#include "flux_structure.hpp"    
#include "../flux_overlay_host.hpp"  
#include "../flux_app.hpp"       
#include "../flux_core.hpp"      
#include <algorithm>

// ============================================================================
// CONTEXT MENU ITEM
// ============================================================================

struct ContextMenuItem {
  enum class Type { Action, Separator };

  Type type;
  std::string label;
  std::function<void()> action;
  bool enabled;

  static ContextMenuItem Action(const std::string &label,
                                std::function<void()> action,
                                bool enabled = true) {
    ContextMenuItem item;
    item.type = Type::Action;
    item.label = label;
    item.action = action;
    item.enabled = enabled;
    return item;
  }

  static ContextMenuItem Separator() {
    ContextMenuItem item;
    item.type = Type::Separator;
    item.label = "";
    item.action = nullptr;
    item.enabled = false;
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

class ContextMenuWidget : public Widget, public OverlayHost {
private:
  ScaffoldWidget *scaffold = nullptr;

  // Screen coords — for showPopup only
  int menuX = 0, menuY = 0;
  // Client coords — for hit-testing, stored once at open time
  int menuClientX = 0, menuClientY = 0;
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
      : items(menuItems) {
    if (anchor) {
      addChild(anchor);
      chainAnchorRightClick(anchor.get());
    }
  }

  void setScaffold(ScaffoldWidget *s) override { scaffold = s; }

  void onDetach() override {
    if (isOpen)
      closeMenu();
    Widget::onDetach();
  }

  // ── Builder API ───────────────────────────────────────────────────────
  std::shared_ptr<ContextMenuWidget>
  setMenuItems(const std::vector<ContextMenuItem> &menuItems) {
    items = menuItems;
    return std::static_pointer_cast<ContextMenuWidget>(shared_from_this());
  }
  std::shared_ptr<ContextMenuWidget> setItemHeight(int h) {
    itemHeight = h;
    return std::static_pointer_cast<ContextMenuWidget>(shared_from_this());
  }
  std::shared_ptr<ContextMenuWidget> setMinWidth(int w) {
    minWidth = w;
    return std::static_pointer_cast<ContextMenuWidget>(shared_from_this());
  }
  std::shared_ptr<ContextMenuWidget> setMenuBackground(Color color) {
    menuBgColor = color;
    return std::static_pointer_cast<ContextMenuWidget>(shared_from_this());
  }
  std::shared_ptr<ContextMenuWidget> setMenuBorder(Color color) {
    menuBorderColor = color;
    return std::static_pointer_cast<ContextMenuWidget>(shared_from_this());
  }
  std::shared_ptr<ContextMenuWidget> setItemHoverColor(Color color) {
    itemHoverColor = color;
    return std::static_pointer_cast<ContextMenuWidget>(shared_from_this());
  }

  // ── Layout ────────────────────────────────────────────────────────────
  void computeLayout(GraphicsContext &ctx, const BoxConstraints &constraints,
                     FontCache &fontCache) override {
    if (autoWidth)
      width = constraints.maxWidth;
    if (autoHeight)
      height = constraints.maxHeight;

    if (!children.empty()) {
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

  void positionChildren(int, int, int, int) override {
    if (!children.empty()) {
      auto &anchor = children[0];
      anchor->x = x;
      anchor->y = y;
      anchor->positionChildren(
          anchor->x + anchor->paddingLeft, anchor->y + anchor->paddingTop,
          anchor->width - anchor->paddingLeft - anchor->paddingRight,
          anchor->height - anchor->paddingTop - anchor->paddingBottom);
    }
  }

  void render(GraphicsContext &ctx, FontCache &fontCache) override {
    if (!children.empty())
      children[0]->render(ctx, fontCache);
    needsPaint = false;
  }

  // ── renderPopupContent ────────────────────────────────────────────────
  void renderPopupContent(GraphicsContext &ctx, FontCache &fontCache) override {
    if (!isOpen || items.empty())
      return;

    Painter painter(ctx);

    // Shadow
    painter.fillRoundedRect(shadowOffset, shadowOffset, menuW, menuH,
                            menuBorderRadius, Color::fromRGBA(0, 0, 0, 60));

    // Background + border
    painter.fillRoundedRect(0, 0, menuW, menuH, menuBorderRadius, menuBgColor);
    painter.drawBorder(0, 0, menuW, menuH, menuBorderRadius, menuBorderColor,
                       1);

    // Items
    NativeFont font = fontCache.getFont(menuFontSize, FontWeight::Normal);
    int currentY = paddingV;

    for (int i = 0; i < (int)items.size(); i++) {
      const auto &item = items[i];

      if (item.type == ContextMenuItem::Type::Separator) {
        int sepY = currentY + separatorHeight / 2;
        painter.drawLine(paddingH, sepY, menuW - paddingH, sepY, separatorColor,
                         1);
        currentY += separatorHeight;
      } else {
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

  // ── Mouse Events ──────────────────────────────────────────────────────
  bool handleMouseDown(int mx, int my) override {
    if (!isOpen)
      return false;

    if (mx >= menuClientX && mx < menuClientX + menuW && my >= menuClientY &&
        my < menuClientY + menuH) {
      int relativeY = my - menuClientY - paddingV;
      int itemIdx = getItemIndexAtY(relativeY);
      if (itemIdx >= 0 && itemIdx < (int)items.size()) {
        const auto &item = items[itemIdx];
        if (item.type == ContextMenuItem::Type::Action && item.enabled) {
          if (item.action)
            item.action();
          closeMenu();
          return true;
        }
      }
      return true;
    }
    closeMenu();
    return true;
  }

  bool handleMouseMove(int mx, int my) override {
    if (!isOpen)
      return false;

    if (mx >= menuClientX && mx < menuClientX + menuW && my >= menuClientY &&
        my < menuClientY + menuH) {
      int relativeY = my - menuClientY - paddingV;
      int itemIdx = getItemIndexAtY(relativeY);
      if (itemIdx != hoveredIndex) {
        hoveredIndex = itemIdx;
        selectedIndex = itemIdx;
        refreshPopupIfOpen_();
        return true;
      }
    } else {
      if (hoveredIndex != -1) {
        hoveredIndex = -1;
        refreshPopupIfOpen_();
        return true;
      }
    }
    return false;
  }

  bool handleRightClick(int /*mx*/, int /*my*/) override {
    if (!isOpen)
      return false;
    closeMenu();
    return true;
  }

  bool handleKeyDown(int keyCode) override {
    if (!isOpen || items.empty())
      return false;
    switch (keyCode) {
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
      if (selectedIndex >= 0 && selectedIndex < (int)items.size()) {
        const auto &item = items[selectedIndex];
        if (item.type == ContextMenuItem::Type::Action && item.enabled) {
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
  void chainAnchorRightClick(Widget *anchor) {
    std::function<bool(int, int)> previous = anchor->onRightClick;
    anchor->onRightClick = [this, anchor, previous](int mx, int my) {
      if (mx >= anchor->x && mx < anchor->x + anchor->width &&
          my >= anchor->y && my < anchor->y + anchor->height) {
        openMenuAt(mx, my);
        return true;
      }
      if (previous)
        return previous(mx, my);
      return false;
    };
  }

  void openMenuAt(int clientX, int clientY) {
    if (isOpen || items.empty())
      return;
    computeMenuGeometry(clientX, clientY);
    isOpen = true;
    hoveredIndex = -1;
    selectedIndex = findFirstActionIndex();

    NativeWindow hw = FluxUI::getCurrentInstance()->getWindow();
    if (hw) {
      FontCache &fc = FluxUI::getCurrentInstance()->getFontCache();
      showPopup(hw, menuX, menuY, menuW + shadowOffset, menuH + shadowOffset,
                fc);
    }

    if (scaffold)
      scaffold->addOverlayHitTarget(this, 150);
  }

  void closeMenu() {
    if (!isOpen)
      return;
    isOpen = false;
    hoveredIndex = -1;
    selectedIndex = -1;
    hidePopup();
    if (scaffold)
      scaffold->removeOverlay(this);
  }

  void computeMenuGeometry(int clientX, int clientY) {
    int maxLabelWidth = 0;
    for (const auto &item : items) {
      if (item.type == ContextMenuItem::Type::Action) {
        int lw = (int)item.label.size() * (menuFontSize / 2);
        maxLabelWidth = std::max(maxLabelWidth, lw);
      }
    }
    menuW = std::max(minWidth, maxLabelWidth + paddingH * 2);

    int totalH = paddingV * 2;
    for (const auto &item : items)
      totalH += (item.type == ContextMenuItem::Type::Separator)
                    ? separatorHeight
                    : itemHeight;
    menuH = totalH;

    // Store client coords for hit-testing — no conversion needed later
    menuClientX = clientX;
    menuClientY = clientY;

    // Convert to screen for showPopup
    auto sc = FluxUI::getCurrentInstance()->clientToScreen(clientX, clientY);
    menuX = sc.x;
    menuY = sc.y;
#ifdef _WIN32
    // Clamp to monitor
    POINT pt = {menuX, menuY};
    HMONITOR mon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(mon, &mi)) {
      if (menuX + menuW > mi.rcWork.right) {
        menuX = mi.rcWork.right - menuW;
        menuClientX =
            FluxUI::getCurrentInstance()->screenToClient(menuX, menuY).x;
      }
      if (menuX < mi.rcWork.left) {
        menuX = mi.rcWork.left;
        menuClientX =
            FluxUI::getCurrentInstance()->screenToClient(menuX, menuY).x;
      }
      if (menuY + menuH > mi.rcWork.bottom) {
        menuY = mi.rcWork.bottom - menuH;
        menuClientY =
            FluxUI::getCurrentInstance()->screenToClient(menuX, menuY).y;
      }
      if (menuY < mi.rcWork.top) {
        menuY = mi.rcWork.top;
        menuClientY =
            FluxUI::getCurrentInstance()->screenToClient(menuX, menuY).y;
      }
    }
#endif

  }

  void refreshPopupIfOpen_() {
    if (!isOpen || !popupVisible())
      return;
    auto *ui = FluxUI::getCurrentInstance();
    if (ui)
      refreshPopup(ui->getFontCache());
  }

  void moveToPrevious() {
    if (selectedIndex < 0)
      selectedIndex = findFirstActionIndex();
    else {
      int prev = selectedIndex - 1;
      while (prev >= 0) {
        if (items[prev].type == ContextMenuItem::Type::Action &&
            items[prev].enabled) {
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

  void moveToNext() {
    if (selectedIndex < 0)
      selectedIndex = findFirstActionIndex();
    else {
      int next = selectedIndex + 1;
      while (next < (int)items.size()) {
        if (items[next].type == ContextMenuItem::Type::Action &&
            items[next].enabled) {
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

  int findFirstActionIndex() const {
    for (int i = 0; i < (int)items.size(); i++)
      if (items[i].type == ContextMenuItem::Type::Action && items[i].enabled)
        return i;
    return 0;
  }
  int findLastActionIndex() const {
    for (int i = (int)items.size() - 1; i >= 0; i--)
      if (items[i].type == ContextMenuItem::Type::Action && items[i].enabled)
        return i;
    return 0;
  }

  int getItemIndexAtY(int relativeY) const {
    int currentY = 0;
    for (int i = 0; i < (int)items.size(); i++) {
      int h = (items[i].type == ContextMenuItem::Type::Separator)
                  ? separatorHeight
                  : itemHeight;
      if (relativeY >= currentY && relativeY < currentY + h) {
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

class DialogWidget : public Widget, public OverlayHost {
private:
  ScaffoldWidget *scaffold = nullptr;
  bool contentLayoutDirty = true;

public:
  bool isOpen = false;
  WidgetPtr content;

  int dialogWidth = 400;
  int dialogHeight = 300;
  Color overlayColor = Color::fromRGBA(0, 0, 0, 128);
  int overlayAlpha = 128;
  Color dialogBgColor = Color::fromRGBA(255, 255, 255, 255);
  Color dialogBorderColor = Color::fromRGBA(200, 200, 200, 255);
  int dialogBorderRadius = 8;
  int dialogPadding = 24;

  std::function<void()> onClose;
  bool closeOnClickOutside = true;

  DialogWidget() { hasBackground = false; }

  void setScaffold(ScaffoldWidget *s) override { scaffold = s; }

  // ── Layout ────────────────────────────────────────────────────────────
  void computeLayout(GraphicsContext &, const BoxConstraints &,
                     FontCache &) override {
    width = 0;
    height = 0;
    needsLayout = false;
  }
  void render(GraphicsContext &, FontCache &) override { needsPaint = false; }

  // ── renderPopupContent ────────────────────────────────────────────────
  void renderPopupContent(GraphicsContext &ctx, FontCache &fontCache) override {
    if (!isOpen)
      return;

    Painter painter(ctx);

    auto sz = FluxUI::getCurrentInstance()->getClientSize();
    int winW = sz.width;
    int winH = sz.height;

    // Semi-transparent dim overlay
    painter.fillRectAlpha(0, 0, winW, winH, overlayColor);

    int dialogX = (winW - dialogWidth) / 2;
    int dialogY = (winH - dialogHeight) / 2;

    // Dialog box
    painter.fillRoundedRect(dialogX, dialogY, dialogWidth, dialogHeight,
                            dialogBorderRadius, dialogBgColor);
    painter.drawBorder(dialogX, dialogY, dialogWidth, dialogHeight,
                       dialogBorderRadius, dialogBorderColor, 1);

    // Content
    if (content) {
      int contentX = dialogX + dialogPadding;
      int contentY = dialogY + dialogPadding;
      int contentW = dialogWidth - dialogPadding * 2;
      int contentH = dialogHeight - dialogPadding * 2;

      if (contentLayoutDirty) {
        content->computeLayout(ctx, BoxConstraints::tight(contentW, contentH),
                               fontCache);
        content->x = contentX;
        content->y = contentY;
        content->positionChildren(
            contentX + content->paddingLeft, contentY + content->paddingTop,
            content->width - content->paddingLeft - content->paddingRight,
            content->height - content->paddingTop - content->paddingBottom);
        contentLayoutDirty = false;
      }
      content->render(ctx, fontCache);
    }
  }

  // ── Mouse Events ──────────────────────────────────────────────────────
  bool handleMouseDown(int mx, int my) override {
    if (!isOpen)
      return false;

    auto sz = FluxUI::getCurrentInstance()->getClientSize();
    int winW = sz.width;
    int winH = sz.height;

    int dialogX = (winW - dialogWidth) / 2;
    int dialogY = (winH - dialogHeight) / 2;

    if (mx < dialogX || mx >= dialogX + dialogWidth || my < dialogY ||
        my >= dialogY + dialogHeight) {
      if (closeOnClickOutside)
        close();
      return true;
    }

    if (content) {
      Widget *toFocus = nullptr;
      if (findAndHandleMouseEvent(content.get(), mx, my,
                                  [mx, my, &toFocus](Widget *w) {
                                    bool handled = w->handleMouseDown(mx, my);
                                    if (!handled && w->onClick && mx >= w->x &&
                                        mx < w->x + w->width && my >= w->y &&
                                        my < w->y + w->height) {
                                      w->onClick();
                                      handled = true;
                                    }
                                    if (handled && w->isFocusable)
                                      toFocus = w;
                                    return handled;
                                  })) {
        if (toFocus && FluxUI::getCurrentInstance())
          FluxUI::getCurrentInstance()->setFocus(toFocus);
        return true;
      }
      Widget *clicked = findWidgetAt(content.get(), mx, my);
      if (clicked) {
        if (clicked->onClick) {
          clicked->onClick();
          return true;
        }
        if (clicked->isFocusable) {
          clicked->handleMouseDown(mx, my);
          if (FluxUI::getCurrentInstance())
            FluxUI::getCurrentInstance()->setFocus(clicked);
          return true;
        }
      }
    }
    return true;
  }

  // ── Open / Close ──────────────────────────────────────────────────────
  void open() {
    if (isOpen)
      return;
    isOpen = true;
    contentLayoutDirty = true;

    auto sz = FluxUI::getCurrentInstance()->getClientSize();
    int winW = sz.width;
    int winH = sz.height;

    // Layout content before first paint — use MeasureContext
    if (content) {
      auto mc = FluxUI::getCurrentInstance()->getMeasureContext();
      FontCache &fc = FluxUI::getCurrentInstance()->getFontCache();
      int contentW = dialogWidth - dialogPadding * 2;
      int contentH = dialogHeight - dialogPadding * 2;
      content->computeLayout(mc.ctx, BoxConstraints::tight(contentW, contentH),
                             fc);

      int dialogX = (winW - dialogWidth) / 2;
      int dialogY = (winH - dialogHeight) / 2;
      int contentX = dialogX + dialogPadding;
      int contentY = dialogY + dialogPadding;
      content->x = contentX;
      content->y = contentY;
      content->positionChildren(
          contentX + content->paddingLeft, contentY + content->paddingTop,
          content->width - content->paddingLeft - content->paddingRight,
          content->height - content->paddingTop - content->paddingBottom);
      contentLayoutDirty = false;
    } // MeasureContext destructor releases DC here

    NativeWindow hw = FluxUI::getCurrentInstance()->getWindow();
    if (hw) {
      auto origin = FluxUI::getCurrentInstance()->clientToScreen(0, 0);
      FontCache &fc = FluxUI::getCurrentInstance()->getFontCache();
      showPopup(hw, origin.x, origin.y, winW, winH, fc);
    }

    if (scaffold)
      scaffold->addOverlayHitTarget(this, 200);
    markNeedsPaint();
  }

  void close() {
    if (!isOpen)
      return;
    contentLayoutDirty = true;

    if (FluxUI::getCurrentInstance()) {
      Widget *focused = FluxUI::getCurrentInstance()->getFocusedWidget();
      if (focused && isDescendantOf(focused, content.get()))
        FluxUI::getCurrentInstance()->setFocus(nullptr);
    }

    isOpen = false;
    hidePopup();
    if (scaffold)
      scaffold->removeOverlay(this);
    if (onClose)
      onClose();
    markNeedsPaint();
  }

  // ── Builder Methods ───────────────────────────────────────────────────
  std::shared_ptr<DialogWidget> setContent(WidgetPtr child) {
    content = child;
    if (content)
      content->parent = this;
    contentLayoutDirty = true;
    markNeedsPaint();
    return std::static_pointer_cast<DialogWidget>(shared_from_this());
  }
  std::shared_ptr<DialogWidget> setSize(int w, int h) {
    dialogWidth = w;
    dialogHeight = h;
    contentLayoutDirty = true;
    markNeedsPaint();
    return std::static_pointer_cast<DialogWidget>(shared_from_this());
  }
  std::shared_ptr<DialogWidget> setCloseOnClickOutside(bool value) {
    closeOnClickOutside = value;
    return std::static_pointer_cast<DialogWidget>(shared_from_this());
  }
  std::shared_ptr<DialogWidget> setOnClose(std::function<void()> cb) {
    onClose = cb;
    return std::static_pointer_cast<DialogWidget>(shared_from_this());
  }
  std::shared_ptr<DialogWidget> setOverlayAlpha(int alpha) {
    overlayAlpha = alpha;
    return std::static_pointer_cast<DialogWidget>(shared_from_this());
  }

private:
  bool isDescendantOf(Widget *candidate, Widget *subtreeRoot) {
    if (!candidate || !subtreeRoot)
      return false;
    Widget *current = candidate->parent;
    while (current) {
      if (current == subtreeRoot)
        return true;
      current = current->parent;
    }
    return candidate == subtreeRoot;
  }
};

// ============================================================================
// TOOLTIP WIDGET
// ============================================================================

enum class TooltipPosition { Above, Below, Auto };

class TooltipWidget : public Widget, public OverlayHost {
private:
  ScaffoldWidget *scaffold = nullptr;

  int tipScreenX = 0, tipScreenY = 0;
  int tipW = 0, tipH = 0;

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

public:
  bool isVisible = false;

  explicit TooltipWidget(WidgetPtr anchor, const std::string &tooltip)
      : tipText(tooltip) {
    if (anchor) {
      addChild(anchor);
      chainAnchorHover(anchor.get());
    }
  }

  void setScaffold(ScaffoldWidget *s) override { scaffold = s; }

  void onDetach() override {
    if (isVisible)
      closeTooltip();
    Widget::onDetach();
  }

  // ── Builder API ───────────────────────────────────────────────────────
  std::shared_ptr<TooltipWidget> setTooltipText(const std::string &t) {
    tipText = t;
    return std::static_pointer_cast<TooltipWidget>(shared_from_this());
  }
  std::shared_ptr<TooltipWidget> setPosition(TooltipPosition pos) {
    preferredPosition = pos;
    return std::static_pointer_cast<TooltipWidget>(shared_from_this());
  }
  std::shared_ptr<TooltipWidget> setTooltipBackground(Color color) {
    tipBgColor = color;
    return std::static_pointer_cast<TooltipWidget>(shared_from_this());
  }
  std::shared_ptr<TooltipWidget> setTooltipTextColor(Color color) {
    tipTextColor = color;
    return std::static_pointer_cast<TooltipWidget>(shared_from_this());
  }
  std::shared_ptr<TooltipWidget> setTooltipFontSize(int size) {
    tipFontSize = size;
    return std::static_pointer_cast<TooltipWidget>(shared_from_this());
  }
  std::shared_ptr<TooltipWidget> setTooltipMaxWidth(int w) {
    tipMaxWidth = w;
    return std::static_pointer_cast<TooltipWidget>(shared_from_this());
  }

  // ── Layout ────────────────────────────────────────────────────────────
  void computeLayout(GraphicsContext &ctx, const BoxConstraints &constraints,
                     FontCache &fontCache) override {
    if (autoWidth)
      width = constraints.maxWidth;
    if (autoHeight)
      height = constraints.maxHeight;
    if (!children.empty()) {
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

  void positionChildren(int, int, int, int) override {
    if (!children.empty()) {
      auto &anchor = children[0];
      anchor->x = x;
      anchor->y = y;
      anchor->positionChildren(
          anchor->x + anchor->paddingLeft, anchor->y + anchor->paddingTop,
          anchor->width - anchor->paddingLeft - anchor->paddingRight,
          anchor->height - anchor->paddingTop - anchor->paddingBottom);
    }
  }

  void render(GraphicsContext &ctx, FontCache &fontCache) override {
    if (!children.empty())
      children[0]->render(ctx, fontCache);
    needsPaint = false;
  }

  // ── renderPopupContent ────────────────────────────────────────────────
  void renderPopupContent(GraphicsContext &ctx, FontCache &fontCache) override {
    if (!isVisible || tipText.empty())
      return;

    Painter painter(ctx);

    // Shadow
    painter.fillRoundedRect(2, 2, tipW, tipH, tipBorderRadius,
                            Color::fromRGBA(0, 0, 0, 60));

    // Bubble
    painter.fillRoundedRect(0, 0, tipW, tipH, tipBorderRadius, tipBgColor);
    painter.drawBorder(0, 0, tipW, tipH, tipBorderRadius, tipBorderColor, 1);

    // Text

    std::wstring wtip = toWideString(tipText);
    NativeFont font = fontCache.getFont(tipFontSize, FontWeight::Normal);
    painter.drawText(wtip, tipPadH, tipPadV, tipW - tipPadH * 2,
                     tipH - tipPadV * 2, font, tipTextColor,
                     DT_CENTER | DT_VCENTER | DT_WORDBREAK | DT_END_ELLIPSIS);
  }

private:
  void chainAnchorHover(Widget *anchor) {
    HoverHandler previous = anchor->onHover;
    anchor->onHover = [this, previous](bool hovered) {
      if (hovered)
        openTooltip();
      else
        closeTooltip();
      if (previous)
        previous(hovered);
    };
  }

  void openTooltip() {
    if (isVisible || tipText.empty())
      return;
    computeBubbleGeometry();
    isVisible = true;

    NativeWindow hw = FluxUI::getCurrentInstance()->getWindow();
    if (hw) {
      FontCache &fc = FluxUI::getCurrentInstance()->getFontCache();
      showPopup(hw, tipScreenX, tipScreenY, tipW + 2, tipH + 2, fc);
    }

    if (scaffold)
      scaffold->addOverlayHitTarget(this, 50);
  }

  void closeTooltip() {
    if (!isVisible)
      return;
    isVisible = false;
    hidePopup();
    if (scaffold)
      scaffold->removeOverlay(this);
  }

  void computeBubbleGeometry() {
    int charW = (int)(tipFontSize * 0.62);
    int lineH = tipFontSize + 4;
    int textW = (int)tipText.size() * charW;
    int maxTW = tipMaxWidth - tipPadH * 2;
    int lines = std::max(1, (textW + maxTW - 1) / maxTW);

    tipW = std::min(textW + tipPadH * 2, tipMaxWidth);
    tipH = lines * lineH + tipPadV * 2;

    int anchorCX = x + width / 2;
    int anchorCY = y;

    auto sc = FluxUI::getCurrentInstance()->clientToScreen(anchorCX - tipW / 2,
                                                           anchorCY);
    tipScreenX = sc.x;
    tipScreenY = sc.y - tipH - 6;

#ifdef _WIN32
    POINT pt = {sc.x, sc.y};
    HMONITOR mon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(mon, &mi)) {
      bool wantAbove = (preferredPosition != TooltipPosition::Below);
      auto below = FluxUI::getCurrentInstance()->clientToScreen(
          anchorCX - tipW / 2, y + height + 6);
      if (wantAbove && tipScreenY >= mi.rcWork.top) {
        // above fits — already set
      } else {
        tipScreenY = below.y;
      }
      if (tipScreenX + tipW > mi.rcWork.right)
        tipScreenX = mi.rcWork.right - tipW;
      if (tipScreenX < mi.rcWork.left)
        tipScreenX = mi.rcWork.left;
    }
#endif
  }
};

// ============================================================================
// DROPDOWN WIDGET
// ============================================================================

class DropdownWidget : public Widget, public OverlayHost {
private:
  ScaffoldWidget *scaffold = nullptr;

  // Screen coords — for showPopup only
  int listScreenX = 0, listScreenY = 0;
  // Client coords — for hit-testing, stored once at open time
  int listClientX = 0, listClientY = 0;
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

  DropdownWidget() {
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

  void setScaffold(ScaffoldWidget *s) override { scaffold = s; }

  // ── Layout ────────────────────────────────────────────────────────────
  void computeLayout(GraphicsContext &, const BoxConstraints &constraints,
                     FontCache &) override {
    if (autoWidth)
      width = constraints.maxWidth;
    applyConstraints();
    needsLayout = false;
  }

  // ── Render main box ───────────────────────────────────────────────────
  void render(GraphicsContext &ctx, FontCache &fontCache) override {
    borderColor = isFocused ? dropdownFocusedBorderColor : dropdownBorderColor;
    drawRoundedRectangle(ctx);

    Painter painter(ctx);
    NativeFont font = fontCache.getFont(fontSize, fontWeight);

    // Selected text or placeholder
    Color textCol = (selectedIndex >= 0 && selectedIndex < (int)options.size())
                        ? getCurrentTextColor()
                        : placeholderColor;
    const std::string &label =
        (selectedIndex >= 0 && selectedIndex < (int)options.size())
            ? options[selectedIndex]
            : placeholder;

    std::wstring wlabel = toWideString(label);
    painter.drawText(wlabel, x + paddingLeft, y + paddingTop,
                     width - paddingLeft - paddingRight,
                     height - paddingTop - paddingBottom, font, textCol,
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    // Chevron arrow — two line segments
    int arrowX = x + width - paddingRight + 10;
    int arrowY = y + height / 2;
    int hs = arrowSize / 2;
    int vs = arrowSize / 4;

    if (isOpen) {
      painter.drawLine(arrowX - hs, arrowY + vs, arrowX, arrowY - vs,
                       arrowColor, 2);
      painter.drawLine(arrowX, arrowY - vs, arrowX + hs, arrowY + vs,
                       arrowColor, 2);
    } else {
      painter.drawLine(arrowX - hs, arrowY - vs, arrowX, arrowY + vs,
                       arrowColor, 2);
      painter.drawLine(arrowX, arrowY + vs, arrowX + hs, arrowY - vs,
                       arrowColor, 2);
    }

    needsPaint = false;
  }

  // ── renderPopupContent ────────────────────────────────────────────────
  void renderPopupContent(GraphicsContext &ctx, FontCache &fontCache) override {
    if (!isOpen || options.empty())
      return;

    Painter painter(ctx);

    int visibleCount = std::min((int)options.size(), maxVisibleItems);
    int listH = visibleCount * itemHeight + 2;

    // Box outline + fill
    painter.fillRect(0, 0, listWidth_, listH, listBgColor);
    painter.drawRectOutline(0, 0, listWidth_, listH, listBorderColor, 1);

    // Clip to inner area
    painter.pushClipRect(1, 1, listWidth_ - 2, listH - 2);

    NativeFont font = fontCache.getFont(fontSize, fontWeight);

    int endIndex = std::min((int)options.size(), scrollOffset + visibleCount);
    for (int i = scrollOffset; i < endIndex; i++) {
      int itemY = 1 + (i - scrollOffset) * itemHeight;

      if (i == hoveredItemIndex)
        painter.fillRect(1, itemY, listWidth_ - 2, itemHeight, itemHoverColor);
      else if (i == selectedIndex)
        painter.fillRect(1, itemY, listWidth_ - 2, itemHeight,
                         itemSelectedColor);

      std::wstring wopt = toWideString(options[i]);
      painter.drawText(wopt, 12, itemY, listWidth_ - 24, itemHeight, font,
                       Color::fromRGB(30, 30, 30),
                       DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }

    painter.popClipRect();
  }

  // ── Mouse Events ──────────────────────────────────────────────────────
  bool handleMouseDown(int mx, int my) override {
    if (isOpen) {
      int visibleCount = std::min((int)options.size(), maxVisibleItems);
      int listH = visibleCount * itemHeight + 2;

      if (mx >= listClientX && mx < listClientX + listWidth_ &&
          my >= listClientY && my < listClientY + listH) {
        int itemIndex = scrollOffset + ((my - listClientY - 1) / itemHeight);
        if (itemIndex >= 0 && itemIndex < (int)options.size())
          selectItem(itemIndex);
        closeDropdown();
        return true;
      }
      closeDropdown();
      return true;
    } else {
      if (mx >= x && mx < x + width && my >= y && my < y + height) {
        openDropdown();
        return true;
      }
    }
    return false;
  }

  bool handleMouseMove(int mx, int my) override {
    if (!isOpen)
      return false;

    int visibleCount = std::min((int)options.size(), maxVisibleItems);
    int listH = visibleCount * itemHeight + 2;

    if (mx >= listClientX && mx < listClientX + listWidth_ &&
        my >= listClientY && my < listClientY + listH) {
      int itemIndex = scrollOffset + ((my - listClientY - 1) / itemHeight);
      if (itemIndex >= 0 && itemIndex < (int)options.size() &&
          itemIndex != hoveredItemIndex) {
        hoveredItemIndex = itemIndex;
        refreshDropdownPopup_();
        return true;
      }
    } else if (hoveredItemIndex != -1) {
      hoveredItemIndex = -1;
      refreshDropdownPopup_();
      return true;
    }
    return false;
  }

  bool handleMouseWheel(int delta) override {
    if (!isOpen)
      return false;
    int maxScroll = std::max(0, (int)options.size() - maxVisibleItems);
    scrollOffset = (delta > 0) ? std::max(0, scrollOffset - 1)
                               : std::min(maxScroll, scrollOffset + 1);
    refreshDropdownPopup_();
    return true;
  }

  bool handleKeyDown(int keyCode) override {
    if (options.empty())
      return false;
    switch (keyCode) {
    case Key::Return:
    case Key::Space:
      if (isOpen) {
        int idx = (hoveredItemIndex >= 0) ? hoveredItemIndex : selectedIndex;
        if (idx >= 0 && idx < (int)options.size())
          selectItem(idx);
        closeDropdown();
      } else {
        openDropdown();
        hoveredItemIndex = selectedIndex;
        if (hoveredItemIndex >= 0)
          ensureItemVisible(hoveredItemIndex);
      }
      markNeedsPaint();
      return true;
    case Key::Escape:
      if (isOpen) {
        closeDropdown();
        markNeedsPaint();
        return true;
      }
      break;
    case Key::Up:
      if (isOpen) {
        if (hoveredItemIndex < 0)
          hoveredItemIndex = std::max(0, selectedIndex);
        else if (hoveredItemIndex > 0)
          hoveredItemIndex--;
        ensureItemVisible(hoveredItemIndex);
        refreshDropdownPopup_();
      } else if (selectedIndex > 0) {
        selectItem(selectedIndex - 1);
      }
      return true;
    case Key::Down:
      if (isOpen) {
        if (hoveredItemIndex < 0)
          hoveredItemIndex = std::max(0, selectedIndex);
        else if (hoveredItemIndex < (int)options.size() - 1)
          hoveredItemIndex++;
        ensureItemVisible(hoveredItemIndex);
        refreshDropdownPopup_();
      } else if (selectedIndex < (int)options.size() - 1) {
        selectItem(selectedIndex + 1);
      }
      return true;
    case Key::Home:
      if (isOpen) {
        hoveredItemIndex = 0;
        scrollOffset = 0;
        refreshDropdownPopup_();
      } else {
        selectItem(0);
      }
      return true;
    case Key::End:
      if (isOpen) {
        hoveredItemIndex = (int)options.size() - 1;
        scrollOffset = std::max(0, (int)options.size() - maxVisibleItems);
        refreshDropdownPopup_();
      } else {
        selectItem((int)options.size() - 1);
      }
      return true;
    }
    return false;
  }

  bool handleFocus(bool focused) override {
    isFocused = focused;
    if (!focused && isOpen)
      closeDropdown();
    markNeedsPaint();
    return true;
  }

  // ── Builder Methods ───────────────────────────────────────────────────
  std::shared_ptr<DropdownWidget>
  setOptions(const std::vector<std::string> &opts) {
    options = opts;
    if (selectedIndex >= (int)options.size())
      selectedIndex = -1;
    markNeedsPaint();
    return std::static_pointer_cast<DropdownWidget>(shared_from_this());
  }
  std::shared_ptr<DropdownWidget> setPlaceholder(const std::string &ph) {
    placeholder = ph;
    markNeedsPaint();
    return std::static_pointer_cast<DropdownWidget>(shared_from_this());
  }
  std::shared_ptr<DropdownWidget> setItemHeight(int h) {
    itemHeight = h;
    markNeedsPaint();
    return std::static_pointer_cast<DropdownWidget>(shared_from_this());
  }
  std::shared_ptr<DropdownWidget> setMaxVisibleItems(int count) {
    maxVisibleItems = count;
    markNeedsPaint();
    return std::static_pointer_cast<DropdownWidget>(shared_from_this());
  }
  std::shared_ptr<DropdownWidget> setOnSelectionChanged(
      std::function<void(int, const std::string &)> callback) {
    onSelectionChanged = callback;
    return std::static_pointer_cast<DropdownWidget>(shared_from_this());
  }
  std::shared_ptr<DropdownWidget> setSelectedIndex(State<int> &state) {
    selectedIndex = state.get();
    state.bindProperty(
        shared_from_this(),
        [](Widget *w, const int &val) {
          static_cast<DropdownWidget *>(w)->selectedIndex = val;
        },
        false);
    boundIntState = &state;
    return std::static_pointer_cast<DropdownWidget>(shared_from_this());
  }
  std::shared_ptr<DropdownWidget> setSelectedValue(State<std::string> &state) {
    selectedIndex = findOptionIndex(state.get());
    state.bindProperty(
        shared_from_this(),
        [](Widget *w, const std::string &val) {
          static_cast<DropdownWidget *>(w)->selectedIndex =
              static_cast<DropdownWidget *>(w)->findOptionIndex(val);
        },
        false);
    boundStringState = &state;
    return std::static_pointer_cast<DropdownWidget>(shared_from_this());
  }
  std::shared_ptr<DropdownWidget> setWidth(int w) {
    width = w;
    autoWidth = false;
    return std::static_pointer_cast<DropdownWidget>(shared_from_this());
  }

  bool hasOverlay() const { return isOpen && !options.empty(); }

private:
  State<int> *boundIntState = nullptr;
  State<std::string> *boundStringState = nullptr;

  void openDropdown() {
    if (isOpen)
      return;
    isOpen = true;
    hoveredItemIndex = -1;
    scrollOffset = 0;
    listWidth_ = width;

    // Store client coords at open time — used for hit-testing
    listClientX = x;
    listClientY = y + height + 2;

    int visibleCount = std::min((int)options.size(), maxVisibleItems);
    int listH = visibleCount * itemHeight + 2;

    // Convert to screen for showPopup
    auto sc =
        FluxUI::getCurrentInstance()->clientToScreen(listClientX, listClientY);
    listScreenX = sc.x;
    listScreenY = sc.y;

    #ifdef _WIN32
    // Clamp to monitor
    POINT pt = {listScreenX, listScreenY};
    HMONITOR mon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(mon, &mi)) {
      if (listScreenX + listWidth_ > mi.rcWork.right)
        listScreenX = mi.rcWork.right - listWidth_;
      if (listScreenY + listH > mi.rcWork.bottom) {
        // flip above the dropdown box
        auto above =
            FluxUI::getCurrentInstance()->clientToScreen(x, y - listH - 2);
        listScreenY = above.y;
        listClientY = y - listH - 2;
      }
    }

    #endif

    NativeWindow hw = FluxUI::getCurrentInstance()->getWindow();
    if (hw) {
      FontCache &fc = FluxUI::getCurrentInstance()->getFontCache();
      showPopup(hw, listScreenX, listScreenY, listWidth_, listH, fc);
    }

    if (scaffold)
      scaffold->addOverlayHitTarget(this, 100);
    markNeedsPaint();
  }

  void closeDropdown() {
    if (!isOpen)
      return;
    isOpen = false;
    hoveredItemIndex = -1;
    hidePopup();
    if (scaffold)
      scaffold->removeOverlay(this);
    markNeedsPaint();
  }

  void refreshDropdownPopup_() {
    if (!isOpen || !popupVisible())
      return;
    auto *ui = FluxUI::getCurrentInstance();
    if (ui)
      refreshPopup(ui->getFontCache());
  }

  void selectItem(int index) {
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

  void ensureItemVisible(int index) {
    if (index < scrollOffset)
      scrollOffset = index;
    else if (index >= scrollOffset + maxVisibleItems)
      scrollOffset = index - maxVisibleItems + 1;
  }

  int findOptionIndex(const std::string &value) const {
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
Dropdown(const std::vector<std::string> &options = {}) {
  auto w = std::make_shared<DropdownWidget>();
  if (!options.empty())
    w->setOptions(options);
  return w;
}

inline TooltipWidgetPtr Tooltip(WidgetPtr anchor, const std::string &tooltip) {
  return std::make_shared<TooltipWidget>(anchor, tooltip);
}

inline DialogWidgetPtr Dialog(WidgetPtr content = nullptr) {
  auto w = std::make_shared<DialogWidget>();
  if (content)
    w->setContent(content);
  return w;
}

inline ContextMenuWidgetPtr
ContextMenu(WidgetPtr anchor, const std::vector<ContextMenuItem> &items) {
  return std::make_shared<ContextMenuWidget>(anchor, items);
}

#endif // FLUX_OVERLAYS_HPP