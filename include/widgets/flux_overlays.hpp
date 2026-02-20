#ifndef FLUX_OVERLAYS_HPP
#define FLUX_OVERLAYS_HPP

#include "flux_app.hpp"
#include "flux_core.hpp"
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

  // Factory: Action item
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

  // Factory: Separator
  static ContextMenuItem Separator() {
    ContextMenuItem item;
    item.type = Type::Separator;
    item.label = "";
    item.action = nullptr;
    item.enabled = false;
    return item;
  }

  // Convenience constructor for Action items (most common case)
  ContextMenuItem(const std::string &lbl, std::function<void()> act,
                  bool en = true)
      : type(Type::Action), label(lbl), action(act), enabled(en) {}

  // Default constructor (needed for vectors)
  ContextMenuItem()
      : type(Type::Action), label(""), action(nullptr), enabled(true) {}
};

// ============================================================================
// CONTEXT MENU WIDGET
//
// Design:
//   - Wraps an anchor child widget (like Tooltip)
//   - Chains handleRightClick on the anchor to open the menu
//   - Opens overlay at cursor position with zIndex 150
//   - Renders menu with hover highlight, disabled state, separators
//   - Handles keyboard (arrows, Enter, Escape, Home/End)
//   - Self-cleans via onDetach() for rebuild safety
//
// Usage:
//   auto btn = Button("Right click me");
//   auto menu = ContextMenu(btn, {
//       {"Cut",   []{ /* ... */ }},
//       {"Copy",  []{ /* ... */ }},
//       ContextMenuItem::Separator(),
//       {"Paste", []{ /* ... */ }, false}  // disabled
//   });
// ============================================================================

class ContextMenuWidget : public Widget {
private:
  ScaffoldWidget *scaffold = nullptr;  

  // Menu geometry (computed on open)
  int menuX = 0, menuY = 0;
  int menuW = 0, menuH = 0;

  // Window bounds (needed for edge clamping)
  int windowW = 0, windowH = 0;

  // Items
  std::vector<ContextMenuItem> items;
  int hoveredIndex = -1;
  int selectedIndex = -1; // For keyboard navigation

  // Appearance
  int itemHeight = 28;
  int separatorHeight = 9;
  int minWidth = 160;
  int paddingH = 12;
  int paddingV = 4;

  COLORREF menuBgColor = RGB(255, 255, 255);
  COLORREF menuBorderColor = RGB(180, 180, 180);
  COLORREF itemHoverColor = RGB(240, 245, 250);
  COLORREF itemTextColor = RGB(30, 30, 30);
  COLORREF itemDisabledColor = RGB(160, 160, 160);
  COLORREF separatorColor = RGB(220, 220, 220);

  int menuFontSize = 13;
  int menuBorderRadius = 6;
  int shadowOffset = 3;

public:
  bool isOpen = false;

  // ----------------------------------------------------------------
  // Construction
  // ----------------------------------------------------------------
  explicit ContextMenuWidget(WidgetPtr anchor,
                             const std::vector<ContextMenuItem> &menuItems)
      : items(menuItems) {
    if (anchor) {
      addChild(anchor);
      chainAnchorRightClick(anchor.get());
    }
  }

  void setScaffold(ScaffoldWidget *s) { scaffold = s; }

  // ----------------------------------------------------------------
  // onDetach — called by FluxUI::rebuild() before old tree is dropped
  // ----------------------------------------------------------------
  void onDetach() override {
    if (isOpen && scaffold) {
      scaffold->removeOverlay(this);
      isOpen = false;
    }
    Widget::onDetach();
  }

  // ----------------------------------------------------------------
  // Builder API
  // ----------------------------------------------------------------
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

  std::shared_ptr<ContextMenuWidget> setMenuBackground(COLORREF color) {
    menuBgColor = color;
    return std::static_pointer_cast<ContextMenuWidget>(shared_from_this());
  }

  std::shared_ptr<ContextMenuWidget> setMenuBorder(COLORREF color) {
    menuBorderColor = color;
    return std::static_pointer_cast<ContextMenuWidget>(shared_from_this());
  }

  std::shared_ptr<ContextMenuWidget> setItemHoverColor(COLORREF color) {
    itemHoverColor = color;
    return std::static_pointer_cast<ContextMenuWidget>(shared_from_this());
  }

  // ----------------------------------------------------------------
  // Layout — pass-through to anchor child
  // ----------------------------------------------------------------
  void computeLayout(HDC hdc, int availableWidth, int availableHeight,
                     FontCache &fontCache) override {
    // Remember window size for edge clamping
    windowW = availableWidth;
    windowH = availableHeight;

    if (autoWidth)
      width = availableWidth;
    if (autoHeight)
      height = availableHeight;

    if (!children.empty()) {
      auto &anchor = children[0];
      anchor->computeLayout(hdc, availableWidth, availableHeight, fontCache);
      if (autoWidth)
        width = anchor->width;
      if (autoHeight)
        height = anchor->height;
    }

    applyConstraints();
    needsLayout = false;
  }

  void positionChildren(int contentX, int contentY, int contentWidth,
                        int contentHeight) override {
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

  // ----------------------------------------------------------------
  // Render — only the anchor child; menu is drawn via overlay
  // ----------------------------------------------------------------
  void render(HDC hdc, FontCache &fontCache) override {
    if (!children.empty())
      children[0]->render(hdc, fontCache);
    needsPaint = false;
  }

  // ----------------------------------------------------------------
  // Mouse Events (overlay handlers)
  // ----------------------------------------------------------------
  bool handleMouseDown(int mx, int my) override {
    if (!isOpen)
      return false;

    // Check if clicked inside menu bounds
    if (mx >= menuX && mx < menuX + menuW && my >= menuY &&
        my < menuY + menuH) {
      int relativeY = my - menuY - paddingV;
      int itemIdx = getItemIndexAtY(relativeY);

      if (itemIdx >= 0 && itemIdx < (int)items.size()) {
        const auto &item = items[itemIdx];
        if (item.type == ContextMenuItem::Type::Action && item.enabled) {
          // Execute action
          if (item.action)
            item.action();
          closeMenu();
          return true;
        }
      }
      // Clicked on separator or disabled item — consume but don't close
      return true;
    }

    // Clicked outside menu — close
    closeMenu();
    return true;
  }

  bool handleMouseMove(int mx, int my) override {
    if (!isOpen)
      return false;

    if (mx >= menuX && mx < menuX + menuW && my >= menuY &&
        my < menuY + menuH) {
      int relativeY = my - menuY - paddingV;
      int itemIdx = getItemIndexAtY(relativeY);

      if (itemIdx != hoveredIndex) {
        hoveredIndex = itemIdx;
        selectedIndex = itemIdx; // Sync keyboard selection with hover
        markNeedsPaint();
        return true;
      }
    } else {
      if (hoveredIndex != -1) {
        hoveredIndex = -1;
        markNeedsPaint();
        return true;
      }
    }

    return false;
  }

  bool handleRightClick(int mx, int my) override {
    if (!isOpen)
      return false;

    // Right-click on open menu closes it
    closeMenu();
    return true;
  }

  // ----------------------------------------------------------------
  // Keyboard Events
  // ----------------------------------------------------------------
  bool handleKeyDown(int keyCode) override {
    if (!isOpen || items.empty())
      return false;

    switch (keyCode) {
    case VK_ESCAPE:
      closeMenu();
      return true;

    case VK_UP:
      moveToPrevious();
      return true;

    case VK_DOWN:
      moveToNext();
      return true;

    case VK_HOME:
      selectedIndex = findFirstActionIndex();
      hoveredIndex = selectedIndex;
      markNeedsPaint();
      return true;

    case VK_END:
      selectedIndex = findLastActionIndex();
      hoveredIndex = selectedIndex;
      markNeedsPaint();
      return true;

    case VK_RETURN:
    case VK_SPACE:
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
  // ----------------------------------------------------------------
  // chainAnchorRightClick
  // Captures right-click on the anchor widget to open the menu.
  // Similar pattern to Tooltip's onHover chaining.
  // ----------------------------------------------------------------
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

  // ----------------------------------------------------------------
  // openMenuAt
  // ----------------------------------------------------------------
  void openMenuAt(int cursorX, int cursorY) {
    if (isOpen || !scaffold || items.empty())
      return;

    computeMenuGeometry(cursorX, cursorY);
    isOpen = true;
    hoveredIndex = -1;
    selectedIndex = findFirstActionIndex(); // Start keyboard nav at first item

    scaffold->addOverlay(
        this, [this](HDC hdc, FontCache &fc) { renderMenu(hdc, fc); },
        150 // zIndex: above Dropdown(100) and Tooltip(50), below Dialog(200)
    );
  }

  // ----------------------------------------------------------------
  // closeMenu
  // ----------------------------------------------------------------
  void closeMenu() {
    if (!isOpen || !scaffold)
      return;

    isOpen = false;
    hoveredIndex = -1;
    selectedIndex = -1;
    scaffold->removeOverlay(this);
  }

  // ----------------------------------------------------------------
  // computeMenuGeometry
  // Measures menu based on items, positions at cursor, clamps to window.
  // ----------------------------------------------------------------
  void computeMenuGeometry(int cursorX, int cursorY) {
    // Width: longest label + padding (rough estimate)
    int maxLabelWidth = 0;
    for (const auto &item : items) {
      if (item.type == ContextMenuItem::Type::Action) {
        int labelW = (int)item.label.size() * (menuFontSize / 2);
        maxLabelWidth = max(maxLabelWidth, labelW);
      }
    }
    menuW = max(minWidth, maxLabelWidth + paddingH * 2);

    // Height: sum of item heights
    int totalH = paddingV * 2;
    for (const auto &item : items) {
      totalH += (item.type == ContextMenuItem::Type::Separator)
                    ? separatorHeight
                    : itemHeight;
    }
    menuH = totalH;

    // Position at cursor
    menuX = cursorX;
    menuY = cursorY;

    // Clamp to window bounds (right edge)
    if (menuX + menuW > windowW)
      menuX = windowW - menuW;
    if (menuX < 0)
      menuX = 0;

    // Clamp to window bounds (bottom edge)
    if (menuY + menuH > windowH)
      menuY = windowH - menuH;
    if (menuY < 0)
      menuY = 0;
  }

  // ----------------------------------------------------------------
  // renderMenu — called by scaffold overlay system
  // ----------------------------------------------------------------
  void renderMenu(HDC hdc, FontCache &fontCache) {
    if (!isOpen || items.empty())
      return;

    // Shadow
    HBRUSH shadowBrush = CreateSolidBrush(RGB(0, 0, 0));
    RECT shadowRect = {menuX + shadowOffset, menuY + shadowOffset,
                       menuX + menuW + shadowOffset,
                       menuY + menuH + shadowOffset};
    HRGN shadowRgn =
        CreateRoundRectRgn(shadowRect.left, shadowRect.top, shadowRect.right,
                           shadowRect.bottom, menuBorderRadius * 2,
                           menuBorderRadius * 2);
    FillRgn(hdc, shadowRgn, shadowBrush);
    DeleteObject(shadowRgn);
    DeleteObject(shadowBrush);

    // Menu background
    HPEN pen = CreatePen(PS_SOLID, 1, menuBorderColor);
    HBRUSH brush = CreateSolidBrush(menuBgColor);
    HPEN oldPen = (HPEN)SelectObject(hdc, pen);
    HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, brush);

    RoundRect(hdc, menuX, menuY, menuX + menuW, menuY + menuH,
              menuBorderRadius * 2, menuBorderRadius * 2);

    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(brush);
    DeleteObject(pen);

    // Items
    HFONT hFont = fontCache.getFont(menuFontSize, FontWeight::Normal);
    HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);
    SetBkMode(hdc, TRANSPARENT);

    int currentY = menuY + paddingV;

    for (int i = 0; i < (int)items.size(); i++) {
      const auto &item = items[i];

      if (item.type == ContextMenuItem::Type::Separator) {
        // Draw separator line
        int sepY = currentY + separatorHeight / 2;
        HPEN sepPen = CreatePen(PS_SOLID, 1, separatorColor);
        HPEN oldSepPen = (HPEN)SelectObject(hdc, sepPen);

        MoveToEx(hdc, menuX + paddingH, sepY, nullptr);
        LineTo(hdc, menuX + menuW - paddingH, sepY);

        SelectObject(hdc, oldSepPen);
        DeleteObject(sepPen);

        currentY += separatorHeight;
      } else {
        // Draw action item
        int itemY = currentY;

        // Hover highlight
        if (i == hoveredIndex && item.enabled) {
          HBRUSH hoverBrush = CreateSolidBrush(itemHoverColor);
          RECT hoverRect = {menuX + 2, itemY, menuX + menuW - 2,
                            itemY + itemHeight};
          FillRect(hdc, &hoverRect, hoverBrush);
          DeleteObject(hoverBrush);
        }

        // Text
        COLORREF textColor = item.enabled ? itemTextColor : itemDisabledColor;
        SetTextColor(hdc, textColor);

        RECT textRect = {menuX + paddingH, itemY, menuX + menuW - paddingH,
                         itemY + itemHeight};
        DrawText(hdc, item.label.c_str(), -1, &textRect,
                 DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

        currentY += itemHeight;
      }
    }

    SelectObject(hdc, hOldFont);
  }

  // ----------------------------------------------------------------
  // Keyboard navigation helpers
  // ----------------------------------------------------------------
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
      // Wrap to last if at top
      if (prev < 0)
        selectedIndex = findLastActionIndex();
    }
    hoveredIndex = selectedIndex;
    markNeedsPaint();
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
      // Wrap to first if at bottom
      if (next >= (int)items.size())
        selectedIndex = findFirstActionIndex();
    }
    hoveredIndex = selectedIndex;
    markNeedsPaint();
  }

  int findFirstActionIndex() const {
    for (int i = 0; i < (int)items.size(); i++) {
      if (items[i].type == ContextMenuItem::Type::Action && items[i].enabled)
        return i;
    }
    return 0;
  }

  int findLastActionIndex() const {
    for (int i = (int)items.size() - 1; i >= 0; i--) {
      if (items[i].type == ContextMenuItem::Type::Action && items[i].enabled)
        return i;
    }
    return 0;
  }

  // ----------------------------------------------------------------
  // getItemIndexAtY
  // Returns the item index at a given Y coordinate relative to menu top.
  // Returns -1 if not over any item.
  // ----------------------------------------------------------------
  int getItemIndexAtY(int relativeY) const {
    int currentY = 0;

    for (int i = 0; i < (int)items.size(); i++) {
      const auto &item = items[i];
      int h = (item.type == ContextMenuItem::Type::Separator) ? separatorHeight
                                                               : itemHeight;

      if (relativeY >= currentY && relativeY < currentY + h) {
        // Don't return separators as selectable
        if (item.type == ContextMenuItem::Type::Separator)
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

class DialogWidget : public Widget {
private:
  ScaffoldWidget *scaffold = nullptr;  
  bool contentLayoutDirty = true; // Track whether layout needs to run

public:
  bool isOpen = false;
  WidgetPtr content;

  // Dialog styling
  int dialogWidth = 400;
  int dialogHeight = 300;
  COLORREF overlayColor = RGB(0, 0, 0); // Black overlay
  int overlayAlpha = 128;               // 50% opacity (0-255)
  COLORREF dialogBgColor = RGB(255, 255, 255);
  COLORREF dialogBorderColor = RGB(200, 200, 200);
  int dialogBorderRadius = 8;
  int dialogPadding = 24;

  // Callbacks
  std::function<void()> onClose;
  bool closeOnClickOutside = true;

  DialogWidget() { hasBackground = false; }

  // ----------------------------------------------------------------
  // SET FLUX APP
  // ----------------------------------------------------------------
  void setScaffold(ScaffoldWidget *s) { scaffold = s; }

  // ----------------------------------------------------------------
  // Layout
  // ----------------------------------------------------------------
  void computeLayout(HDC hdc, int availableWidth, int availableHeight,
                     FontCache &fontCache) override {
    // Dialog doesn't take up space in normal layout
    width = 0;
    height = 0;
    needsLayout = false;
  }

  void render(HDC hdc, FontCache &fontCache) override {
    // Dialog renders via overlay system, not normal render
    needsPaint = false;
  }

  // ----------------------------------------------------------------
  // Mouse Events
  // ----------------------------------------------------------------
  bool handleMouseDown(int mx, int my) override {
    if (!isOpen)
      return false;

    RECT windowRect = {0, 0, 800, 600};
    if (scaffold) {
      windowRect.right = scaffold->width;
      windowRect.bottom = scaffold->height;
    }

    int dialogX = (windowRect.right - dialogWidth) / 2;
    int dialogY = (windowRect.bottom - dialogHeight) / 2;

    // Always consume clicks when dialog is open (nothing behind should get it)
    // Check if clicked OUTSIDE dialog first
    if (mx < dialogX || mx >= dialogX + dialogWidth || my < dialogY ||
        my >= dialogY + dialogHeight) {
      if (closeOnClickOutside) {
        close();
      }
      return true; // Always consume - block clicks behind dialog
    }

    // Clicked INSIDE dialog - dispatch to content
    if (content) {
      Widget *toFocus = nullptr;

      // Walk the content tree properly
      if (findAndHandleMouseEvent(content.get(), mx, my,
                                  [mx, my, &toFocus](Widget *w) {
                                    bool handled = w->handleMouseDown(mx, my);
                                    // Also trigger onClick for button-like
                                    // widgets
                                    if (!handled && w->onClick &&
                                        mx >= w->x && mx < w->x + w->width &&
                                        my >= w->y && my < w->y + w->height) {
                                      w->onClick();
                                      handled = true;
                                    }
                                    // Capture focusable widget at the moment it
                                    // claims the event
                                    if (handled && w->isFocusable) {
                                      toFocus = w;
                                    }
                                    return handled;
                                  })) {
        // Sync focus with FluxUI after dialog dispatches click
        if (toFocus && FluxUI::getCurrentInstance()) {
          FluxUI::getCurrentInstance()->setFocus(toFocus);
        }
        return true;
      }

      // Fallback: check onClick directly on widgets at click position
      Widget *clicked = findWidgetAt(content.get(), mx, my);
      if (clicked) {
        if (clicked->onClick) {
          clicked->onClick();
          return true;
        }
        if (clicked->isFocusable) {
          clicked->handleMouseDown(mx, my);
          if (FluxUI::getCurrentInstance()) {
            FluxUI::getCurrentInstance()->setFocus(clicked);
          }
          return true;
        }
      }
    }

    return true; // Consume click inside dialog even if nothing handled it
  }

  // ----------------------------------------------------------------
  // Open/Close Dialog
  // ----------------------------------------------------------------
  void open() {
    if (isOpen || !scaffold)
      return;

    isOpen = true;
    contentLayoutDirty = true; // Force layout on first render after open

    // Run layout eagerly so hit-testing is valid before the first click
    if (content) {
      HWND hwnd =
          FluxUI::getCurrentInstance() ? FluxUI::getCurrentInstance()->getWindow() : nullptr;
      if (hwnd) {
        HDC hdc = GetDC(hwnd);
        FontCache &fontCache = FluxUI::getCurrentInstance()->getFontCache();

        int contentW = dialogWidth - dialogPadding * 2;
        int contentH = dialogHeight - dialogPadding * 2;
        content->computeLayout(hdc, contentW, contentH, fontCache);

        RECT windowRect = {0, 0, 800, 600};
        windowRect.right = scaffold->width;
        windowRect.bottom = scaffold->height;
        int dialogX = (windowRect.right - dialogWidth) / 2;
        int dialogY = (windowRect.bottom - dialogHeight) / 2;
        int contentX = dialogX + dialogPadding;
        int contentY = dialogY + dialogPadding;

        content->x = contentX;
        content->y = contentY;
        content->positionChildren(
            contentX + content->paddingLeft, contentY + content->paddingTop,
            content->width - content->paddingLeft - content->paddingRight,
            content->height - content->paddingTop - content->paddingBottom);

        ReleaseDC(hwnd, hdc);
        contentLayoutDirty = false;
      }
    }

    // Register overlay with scaffold
    scaffold->addOverlay(
        this,
        [this](HDC hdc, FontCache &fontCache) {
          this->renderDialog(hdc, fontCache);
        },
        200 // zIndex: higher than dropdown (100)
    );

    markNeedsPaint();
  }

  void close() {
    if (!isOpen || !scaffold)
      return;

    isOpen = false;
    contentLayoutDirty = true; // Reset for next open

    // Clear FluxUI focus if a widget inside this dialog currently owns it
    if (FluxUI::getCurrentInstance()) {
      Widget *focused = FluxUI::getCurrentInstance()->getFocusedWidget();
      if (focused && isDescendantOf(focused, content.get())) {
        FluxUI::getCurrentInstance()->setFocus(nullptr);
      }
    }

    // Unregister overlay
    scaffold->removeOverlay(this);

    if (onClose) {
      onClose();
    }

    markNeedsPaint();
  }

  // ----------------------------------------------------------------
  // Render Dialog (Called by scaffold overlay system)
  // ----------------------------------------------------------------
  void renderDialog(HDC hdc, FontCache &fontCache) {
    if (!isOpen)
      return;

    RECT windowRect = {0, 0, 800, 600};
    if (scaffold) {
      windowRect.right = scaffold->width;
      windowRect.bottom = scaffold->height;
    }

    // Draw semi-transparent overlay
    HBRUSH overlayBrush = CreateSolidBrush(overlayColor);
    BLENDFUNCTION blend = {AC_SRC_OVER, 0, (BYTE)overlayAlpha, 0};

    HDC hdcOverlay = CreateCompatibleDC(hdc);
    HBITMAP hbmOverlay =
        CreateCompatibleBitmap(hdc, windowRect.right, windowRect.bottom);
    HBITMAP hbmOldOverlay = (HBITMAP)SelectObject(hdcOverlay, hbmOverlay);

    FillRect(hdcOverlay, &windowRect, overlayBrush);

    AlphaBlend(hdc, 0, 0, windowRect.right, windowRect.bottom, hdcOverlay, 0,
               0, windowRect.right, windowRect.bottom, blend);

    SelectObject(hdcOverlay, hbmOldOverlay);
    DeleteObject(hbmOverlay);
    DeleteDC(hdcOverlay);
    DeleteObject(overlayBrush);

    int dialogX = (windowRect.right - dialogWidth) / 2;
    int dialogY = (windowRect.bottom - dialogHeight) / 2;

    // Draw dialog background
    HBRUSH dialogBrush = CreateSolidBrush(dialogBgColor);
    HPEN dialogPen = CreatePen(PS_SOLID, 1, dialogBorderColor);

    HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, dialogBrush);
    HPEN oldPen = (HPEN)SelectObject(hdc, dialogPen);

    RoundRect(hdc, dialogX, dialogY, dialogX + dialogWidth,
              dialogY + dialogHeight, dialogBorderRadius * 2,
              dialogBorderRadius * 2);

    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(dialogBrush);
    DeleteObject(dialogPen);

    // Render content
    if (content) {
      int contentX = dialogX + dialogPadding;
      int contentY = dialogY + dialogPadding;
      int contentW = dialogWidth - dialogPadding * 2;
      int contentH = dialogHeight - dialogPadding * 2;

      // Only re-layout if dirty (e.g. content changed), not every frame
      if (contentLayoutDirty) {
        content->computeLayout(hdc, contentW, contentH, fontCache);

        content->x = contentX;
        content->y = contentY;
        content->positionChildren(
            contentX + content->paddingLeft, contentY + content->paddingTop,
            content->width - content->paddingLeft - content->paddingRight,
            content->height - content->paddingTop - content->paddingBottom);

        contentLayoutDirty = false;
      }

      content->render(hdc, fontCache);
    }
  }

  // ----------------------------------------------------------------
  // Builder Methods
  // ----------------------------------------------------------------
  std::shared_ptr<DialogWidget> setContent(WidgetPtr child) {
    content = child;
    if (content) {
      content->parent = this;
    }
    contentLayoutDirty = true; // Content changed, layout must re-run
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

  std::shared_ptr<DialogWidget> setOnClose(std::function<void()> callback) {
    onClose = callback;
    return std::static_pointer_cast<DialogWidget>(shared_from_this());
  }

  std::shared_ptr<DialogWidget> setOverlayAlpha(int alpha) {
    overlayAlpha = alpha; // 0-255
    return std::static_pointer_cast<DialogWidget>(shared_from_this());
  }

private:
  // ----------------------------------------------------------------
  // Helper: check whether a widget is a descendant of a subtree root
  // Used by close() to clear focus when the focused widget is inside
  // this dialog's content tree.
  // ----------------------------------------------------------------
  bool isDescendantOf(Widget *candidate, Widget *subtreeRoot) {
    if (!candidate || !subtreeRoot)
      return false;
    Widget *current = candidate->parent;
    while (current) {
      if (current == subtreeRoot)
        return true;
      current = current->parent;
    }
    // Also handle the case where candidate IS the subtree root
    return candidate == subtreeRoot;
  }
};

// ============================================================================
// TOOLTIP WIDGET
//
// Design:
//   - TooltipWidget wraps an anchor child widget.
//   - It hijacks (chains) the anchor's onHover callback to open/close itself.
//   - When open it registers a renderer with scaffoldWidget at zIndex 50
//     (below Dropdown=100, Dialog=200).
//   - No new input mechanism needed: the existing onHover bool already delivers
//     mouse-enter (true) and mouse-leave / WM_MOUSELEAVE (false).
//   - onDetach() removes the overlay so rebuild() never leaves a dangling ptr.
//
// Usage:
//   auto btn = Button("Hover me");
//   auto tip = Tooltip(btn, "This is a tooltip");
//   // tip replaces btn in the layout — btn is tip's child
// ============================================================================

enum class TooltipPosition {
  Above,  // Prefer above anchor; fall back to below if no room
  Below,  // Prefer below anchor; fall back to above if no room
  Auto    // Default: prefer Above
};

class TooltipWidget : public Widget {
private:
ScaffoldWidget *scaffold = nullptr;  

  // Tooltip bubble geometry (computed on open)
  int tipX = 0, tipY = 0;
  int tipW = 0, tipH = 0;

  // Configurable appearance
  std::string tipText;
  TooltipPosition preferredPosition = TooltipPosition::Auto;
  COLORREF tipBgColor     = RGB(50,  50,  50);
  COLORREF tipTextColor   = RGB(255, 255, 255);
  COLORREF tipBorderColor = RGB(80,  80,  80);
  int tipFontSize         = 12;
  int tipPadH             = 10; // horizontal padding inside bubble
  int tipPadV             = 6;  // vertical padding inside bubble
  int tipBorderRadius     = 4;
  int tipMaxWidth         = 240;
  int windowHeight        = 0;  // set from anchor position, used for bounds check

public:
  bool isVisible = false;

  // ----------------------------------------------------------------
  // Construction
  // ----------------------------------------------------------------
  explicit TooltipWidget(WidgetPtr anchor, const std::string &tooltip)
      : tipText(tooltip) {
    if (anchor) {
      addChild(anchor);
      chainAnchorHover(anchor.get());
    }
  }

void setScaffold(ScaffoldWidget *s) { scaffold = s; }

  // ----------------------------------------------------------------
  // onDetach — called by FluxUI::rebuild() before old tree is dropped.
  // Ensures the overlay stack never holds a dangling pointer.
  // ----------------------------------------------------------------
  void onDetach() override {
    if (isVisible && scaffold) {
      scaffold->removeOverlay(this);
      isVisible = false;
    }
    // Propagate to children
    Widget::onDetach();
  }

  // ----------------------------------------------------------------
  // Builder API
  // ----------------------------------------------------------------
  std::shared_ptr<TooltipWidget> setTooltipText(const std::string &t) {
    tipText = t;
    return std::static_pointer_cast<TooltipWidget>(shared_from_this());
  }

  std::shared_ptr<TooltipWidget> setPosition(TooltipPosition pos) {
    preferredPosition = pos;
    return std::static_pointer_cast<TooltipWidget>(shared_from_this());
  }

  std::shared_ptr<TooltipWidget> setTooltipBackground(COLORREF color) {
    tipBgColor = color;
    return std::static_pointer_cast<TooltipWidget>(shared_from_this());
  }

  std::shared_ptr<TooltipWidget> setTooltipTextColor(COLORREF color) {
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

  // ----------------------------------------------------------------
  // Layout — pass-through to the single anchor child
  // ----------------------------------------------------------------
  void computeLayout(HDC hdc, int availableWidth, int availableHeight,
                     FontCache &fontCache) override {
    // Remember window height for Above/Below bounds check
    windowHeight = availableHeight;

    if (autoWidth)  width  = availableWidth;
    if (autoHeight) height = availableHeight;

    if (!children.empty()) {
      auto &anchor = children[0];
      anchor->computeLayout(hdc, availableWidth, availableHeight, fontCache);
      // Shrink-wrap to anchor size so we don't steal extra hit area
      if (autoWidth)  width  = anchor->width;
      if (autoHeight) height = anchor->height;
    }

    applyConstraints();
    needsLayout = false;
  }

  void positionChildren(int contentX, int contentY, int contentWidth,
                        int contentHeight) override {
    if (!children.empty()) {
      auto &anchor = children[0];
      anchor->x = x;
      anchor->y = y;
      anchor->positionChildren(
          anchor->x + anchor->paddingLeft,
          anchor->y + anchor->paddingTop,
          anchor->width  - anchor->paddingLeft - anchor->paddingRight,
          anchor->height - anchor->paddingTop  - anchor->paddingBottom);
    }
  }

  // ----------------------------------------------------------------
  // Render — only the anchor child; bubble is drawn via overlay
  // ----------------------------------------------------------------
  void render(HDC hdc, FontCache &fontCache) override {
    if (!children.empty())
      children[0]->render(hdc, fontCache);
    needsPaint = false;
  }

private:
  // ----------------------------------------------------------------
  // chainAnchorHover
  // Saves any existing onHover on the anchor and prepends our logic.
  // This means the anchor's own hover effects (color change, etc.)
  // continue to work uninterrupted.
  // ----------------------------------------------------------------
  void chainAnchorHover(Widget *anchor) {
    HoverHandler previous = anchor->onHover; // may be nullptr

    // Capture 'this' by raw pointer — safe because TooltipWidget owns the
    // anchor child, so it always outlives it.
    anchor->onHover = [this, previous](bool hovered) {
      if (hovered)
        openTooltip();
      else
        closeTooltip();

      if (previous)
        previous(hovered);
    };
  }

  // ----------------------------------------------------------------
  // openTooltip
  // ----------------------------------------------------------------
  void openTooltip() {
    if (isVisible || !scaffold || tipText.empty())
      return;

    computeBubbleGeometry();
    isVisible = true;

    scaffold->addOverlay(
        this,
        [this](HDC hdc, FontCache &fc) { renderBubble(hdc, fc); },
        50 // zIndex: below Dropdown(100) and Dialog(200)
    );
  }

  // ----------------------------------------------------------------
  // closeTooltip
  // ----------------------------------------------------------------
  void closeTooltip() {
    if (!isVisible || !scaffold)
      return;

    isVisible = false;
    scaffold->removeOverlay(this);
  }

  // ----------------------------------------------------------------
  // computeBubbleGeometry
  // Measures the tip text, decides Above vs Below, clamps to window.
  // ----------------------------------------------------------------
  void computeBubbleGeometry() {
    // Estimate text size using a rough character-width heuristic.
    // A proper implementation would use a temporary HDC + GetTextExtentPoint32,
    // but we don't have an HDC here.  The average character width at
    // tipFontSize pt on a 96-dpi screen is roughly (tipFontSize * 0.6) px.
    int charW    = (int)(tipFontSize * 0.62);
    int lineH    = tipFontSize + 4;
    int textW    = (int)tipText.size() * charW;
    int maxTW    = tipMaxWidth - tipPadH * 2;
    int lines    = (textW + maxTW - 1) / maxTW; // word-wrap estimate
    if (lines < 1) lines = 1;

    tipW = min(textW + tipPadH * 2, tipMaxWidth);
    tipH = lines * lineH + tipPadV * 2;

    // Horizontal: centre on anchor, clamped to [0, anchorParentWidth]
    int anchorCX = x + width / 2;
    tipX = anchorCX - tipW / 2;
    if (tipX < 0) tipX = 0;

    // Vertical: prefer above, fall back to below
    bool wantAbove = (preferredPosition != TooltipPosition::Below);
    int aboveY = y - tipH - 6;
    int belowY = y + height + 6;

    if (wantAbove && aboveY >= 0) {
      tipY = aboveY;
    } else if (!wantAbove && belowY + tipH <= windowHeight) {
      tipY = belowY;
    } else if (aboveY >= 0) {
      tipY = aboveY; // Fall back to above
    } else {
      tipY = belowY; // Last resort: below
    }
  }

  // ----------------------------------------------------------------
  // renderBubble — called by the scaffold overlay renderer
  // ----------------------------------------------------------------
  void renderBubble(HDC hdc, FontCache &fontCache) {
    if (!isVisible || tipText.empty())
      return;

    // Shadow (simple 1px offset darker box)
    HBRUSH shadowBrush = CreateSolidBrush(RGB(0, 0, 0));
    RECT shadowRect    = {tipX + 2, tipY + 2, tipX + tipW + 2, tipY + tipH + 2};
    HRGN shadowRgn     = CreateRoundRectRgn(shadowRect.left, shadowRect.top,
                                            shadowRect.right, shadowRect.bottom,
                                            tipBorderRadius * 2, tipBorderRadius * 2);
    // Paint shadow at 25% opacity via a blended fill
    // (Win32 has no native alpha for arbitrary shapes without layered windows;
    //  we approximate with a darker solid and paint before the bubble.)
    FillRgn(hdc, shadowRgn, shadowBrush);
    DeleteObject(shadowRgn);
    DeleteObject(shadowBrush);

    // Bubble background
    HPEN   pen   = CreatePen(PS_SOLID, 1, tipBorderColor);
    HBRUSH brush = CreateSolidBrush(tipBgColor);
    HPEN   oldPen   = (HPEN)  SelectObject(hdc, pen);
    HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, brush);

    RoundRect(hdc, tipX, tipY, tipX + tipW, tipY + tipH,
              tipBorderRadius * 2, tipBorderRadius * 2);

    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(brush);
    DeleteObject(pen);

    // Text
    HFONT hFont    = fontCache.getFont(tipFontSize, FontWeight::Normal);
    HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);
    SetTextColor(hdc, tipTextColor);
    SetBkMode(hdc, TRANSPARENT);

    RECT textRect = {tipX + tipPadH, tipY + tipPadV,
                     tipX + tipW - tipPadH, tipY + tipH - tipPadV};

    DrawText(hdc, tipText.c_str(), -1, &textRect,
             DT_CENTER | DT_VCENTER | DT_WORDBREAK | DT_END_ELLIPSIS);

    SelectObject(hdc, hOldFont);
  }
};

// ============================================================================
// DROPDOWN WIDGET
// ============================================================================

class DropdownWidget : public Widget {
private:
 ScaffoldWidget *scaffold = nullptr;  // Reference to app for overlay management

public:
  std::vector<std::string> options;
  int selectedIndex = -1;
  bool isOpen = false;
  int hoveredItemIndex = -1;

  // Dimensions
  int itemHeight = 32;
  int maxVisibleItems = 6;
  int arrowSize = 8;
  int scrollOffset = 0;

  // Colors
  COLORREF dropdownBgColor = RGB(255, 255, 255);
  COLORREF dropdownBorderColor = RGB(180, 180, 180);
  COLORREF dropdownFocusedBorderColor = RGB(33, 150, 243);
  COLORREF placeholderColor = RGB(150, 150, 150);
  COLORREF itemHoverColor = RGB(240, 240, 240);
  COLORREF itemSelectedColor = RGB(230, 245, 255);
  COLORREF listBgColor = RGB(255, 255, 255);
  COLORREF listBorderColor = RGB(200, 200, 200);
  COLORREF arrowColor = RGB(100, 100, 100);

  std::string placeholder = "Select an option";

  // Callbacks
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
    paddingRight = 30; // Space for arrow
    paddingTop = paddingBottom = 8;
    height = 36;
    autoHeight = false;
  }

  // ----------------------------------------------------------------
  // SET FLUX APP (Called during widget tree setup)
  // ----------------------------------------------------------------
void setScaffold(ScaffoldWidget *s) { scaffold = s; }

  // ----------------------------------------------------------------
  // Layout
  // ----------------------------------------------------------------
  void computeLayout(HDC hdc, int availableWidth, int availableHeight,
                     FontCache &fontCache) override {
    if (autoWidth)
      width = availableWidth;

    applyConstraints();
    needsLayout = false;
  }

  // ----------------------------------------------------------------
  // Render (Only render the main dropdown box, not the list)
  // ----------------------------------------------------------------
  void render(HDC hdc, FontCache &fontCache) override {
    // Draw the main dropdown box
    borderColor = isFocused ? dropdownFocusedBorderColor : dropdownBorderColor;
    drawRoundedRectangle(hdc);

    // Draw selected text or placeholder
    HFONT hFont = fontCache.getFont(fontSize, fontWeight);
    HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);
    SetBkMode(hdc, TRANSPARENT);

    RECT textRect = {x + paddingLeft, y + paddingTop, x + width - paddingRight,
                     y + height - paddingBottom};

    if (selectedIndex >= 0 && selectedIndex < (int)options.size()) {
      SetTextColor(hdc, getCurrentTextColor());
      DrawText(hdc, options[selectedIndex].c_str(), -1, &textRect,
               DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    } else {
      SetTextColor(hdc, placeholderColor);
      DrawText(hdc, placeholder.c_str(), -1, &textRect,
               DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }

    // Draw dropdown arrow
    int arrowX = x + width - paddingRight + 10;
    int arrowY = y + height / 2;

    HPEN arrowPen = CreatePen(PS_SOLID, 2, arrowColor);
    HPEN oldPen = (HPEN)SelectObject(hdc, arrowPen);

    if (isOpen) {
      MoveToEx(hdc, arrowX - arrowSize / 2, arrowY + arrowSize / 4, nullptr);
      LineTo(hdc, arrowX, arrowY - arrowSize / 4);
      LineTo(hdc, arrowX + arrowSize / 2, arrowY + arrowSize / 4);
    } else {
      MoveToEx(hdc, arrowX - arrowSize / 2, arrowY - arrowSize / 4, nullptr);
      LineTo(hdc, arrowX, arrowY + arrowSize / 4);
      LineTo(hdc, arrowX + arrowSize / 2, arrowY - arrowSize / 4);
    }

    SelectObject(hdc, oldPen);
    DeleteObject(arrowPen);
    SelectObject(hdc, hOldFont);
    needsPaint = false;
  }

  // ----------------------------------------------------------------
  // Mouse Events (Now register/unregister overlay with scaffold)
  // ----------------------------------------------------------------
  bool handleMouseDown(int mx, int my) override {
    if (isOpen) {
      // Calculate dropdown list bounds
      int listX = x;
      int listY = y + height + 2;
      int listWidth = width;
      int visibleItemCount = min((int)options.size(), maxVisibleItems);
      int listHeight = visibleItemCount * itemHeight + 2;

      // Check if clicked on dropdown list
      if (mx >= listX && mx < listX + listWidth && my >= listY &&
          my < listY + listHeight) {
        int relativeY = my - listY - 1;
        int itemIndex = scrollOffset + (relativeY / itemHeight);

        if (itemIndex >= 0 && itemIndex < (int)options.size()) {
          selectItem(itemIndex);
        }

        closeDropdown();
        return true;
      }

      // Check if clicked on the main dropdown box
      if (mx >= x && mx < x + width && my >= y && my < y + height) {
        closeDropdown();
        return true;
      }

      // Clicked outside - close dropdown
      closeDropdown();
      return true;
    } else {
      // Dropdown is closed - check if clicked on main box to open
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

    int listX = x;
    int listY = y + height + 2;
    int listWidth = width;
    int visibleItemCount = min((int)options.size(), maxVisibleItems);
    int listHeight = visibleItemCount * itemHeight + 2;

    if (mx >= listX && mx < listX + listWidth && my >= listY &&
        my < listY + listHeight) {
      int relativeY = my - listY - 1;
      int itemIndex = scrollOffset + (relativeY / itemHeight);

      if (itemIndex >= 0 && itemIndex < (int)options.size()) {
        if (hoveredItemIndex != itemIndex) {
          hoveredItemIndex = itemIndex;
          markNeedsPaint();
          return true;
        }
      }
    } else {
      if (hoveredItemIndex != -1) {
        hoveredItemIndex = -1;
        markNeedsPaint();
        return true;
      }
    }

    return false;
  }

  bool handleMouseWheel(int delta) override {
    if (!isOpen)
      return false;

    int maxScroll = max(0, (int)options.size() - maxVisibleItems);

    if (delta > 0) {
      // Scroll up
      scrollOffset = max(0, scrollOffset - 1);
    } else {
      // Scroll down
      scrollOffset = min(maxScroll, scrollOffset + 1);
    }

    markNeedsPaint();
    return true;
  }

  // ----------------------------------------------------------------
  // Keyboard Events
  // ----------------------------------------------------------------
  bool handleKeyDown(int keyCode) override {
    if (options.empty())
      return false;

    switch (keyCode) {
    case VK_RETURN:
    case VK_SPACE:
      if (isOpen) {
        int indexToSelect =
            (hoveredItemIndex >= 0) ? hoveredItemIndex : selectedIndex;
        if (indexToSelect >= 0 && indexToSelect < (int)options.size()) {
          selectItem(indexToSelect);
        }
        closeDropdown();
      } else {
        openDropdown();
        hoveredItemIndex = selectedIndex;
        if (hoveredItemIndex >= 0) {
          ensureItemVisible(hoveredItemIndex);
        }
      }
      markNeedsPaint();
      return true;

    case VK_ESCAPE:
      if (isOpen) {
        closeDropdown();
        markNeedsPaint();
        return true;
      }
      break;

    case VK_UP:
      if (isOpen) {
        // Initialize hoveredItemIndex if not set
        if (hoveredItemIndex < 0) {
          hoveredItemIndex = selectedIndex >= 0 ? selectedIndex : 0;
        } else if (hoveredItemIndex > 0) {
          hoveredItemIndex--;
        }
        ensureItemVisible(hoveredItemIndex);
        markNeedsPaint();
      } else if (selectedIndex > 0) {
        selectItem(selectedIndex - 1);
      }
      return true;

    case VK_DOWN:
      if (isOpen) {
        // Initialize hoveredItemIndex if not set
        if (hoveredItemIndex < 0) {
          hoveredItemIndex = selectedIndex >= 0 ? selectedIndex : 0;
        } else if (hoveredItemIndex < (int)options.size() - 1) {
          hoveredItemIndex++;
        }
        ensureItemVisible(hoveredItemIndex);
        markNeedsPaint();
      } else if (selectedIndex < (int)options.size() - 1) {
        selectItem(selectedIndex + 1);
      }
      return true;

    case VK_HOME:
      if (isOpen) {
        hoveredItemIndex = 0;
        scrollOffset = 0;
        markNeedsPaint();
      } else {
        selectItem(0);
      }
      return true;

    case VK_END:
      if (isOpen) {
        hoveredItemIndex = (int)options.size() - 1;
        scrollOffset = max(0, (int)options.size() - maxVisibleItems);
        markNeedsPaint();
      } else {
        selectItem((int)options.size() - 1);
      }
      return true;
    }

    return false;
  }

  bool handleFocus(bool focused) override {
    isFocused = focused;
    if (!focused && isOpen) {
      closeDropdown();
    }
    markNeedsPaint();
    return true;
  }

  // ----------------------------------------------------------------
  // Builder Methods
  // ----------------------------------------------------------------
  std::shared_ptr<DropdownWidget>
  setOptions(const std::vector<std::string> &opts) {
    options = opts;

    // Reset selection if out of bounds
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

  // ----------------------------------------------------------------
  // State Binding (by index)
  // ----------------------------------------------------------------
  std::shared_ptr<DropdownWidget> setSelectedIndex(State<int> &state) {
    selectedIndex = state.get();

    state.bindProperty(
        shared_from_this(),
        [](Widget *w, const int &val) {
          auto *dropdown = static_cast<DropdownWidget *>(w);
          dropdown->selectedIndex = val;
        },
        false);

    boundIntState = &state;

    return std::static_pointer_cast<DropdownWidget>(shared_from_this());
  }

  // ----------------------------------------------------------------
  // State Binding (by value string)
  // ----------------------------------------------------------------
  std::shared_ptr<DropdownWidget> setSelectedValue(State<std::string> &state) {
    // Find index for initial value
    std::string initialValue = state.get();
    selectedIndex = findOptionIndex(initialValue);

    state.bindProperty(
        shared_from_this(),
        [](Widget *w, const std::string &val) {
          auto *dropdown = static_cast<DropdownWidget *>(w);
          dropdown->selectedIndex = dropdown->findOptionIndex(val);
        },
        false);

    boundStringState = &state;

    return std::static_pointer_cast<DropdownWidget>(shared_from_this());
  }

  // ----------------------------------------------------------------
  // Helper: Check if dropdown list should be rendered
  // ----------------------------------------------------------------
  bool hasOverlay() const { return isOpen && !options.empty(); }

private:
  State<int> *boundIntState = nullptr;
  State<std::string> *boundStringState = nullptr;

  // ----------------------------------------------------------------
  // Open/Close Dropdown (Register/Unregister Overlay)
  // ----------------------------------------------------------------
  void openDropdown() {
    if (isOpen || !scaffold)
      return;

    isOpen = true;
    hoveredItemIndex = -1;
    scrollOffset = 0;

    // 🎯 Register overlay with scaffold
    scaffold->addOverlay(
        this,
        [this](HDC hdc, FontCache &fontCache) {
          this->renderDropdownList(hdc, fontCache);
        },
        100 // zIndex: render on top of everything
    );

    markNeedsPaint();
  }

  void closeDropdown() {
    if (!isOpen || !scaffold)
      return;

    isOpen = false;
    hoveredItemIndex = -1;

    // 🎯 Unregister overlay from scaffold
    scaffold->removeOverlay(this);

    markNeedsPaint();
  }

  // ----------------------------------------------------------------
  // Render Dropdown List (Called by scaffold overlay system)
  // ----------------------------------------------------------------
  void renderDropdownList(HDC hdc, FontCache &fontCache) {
    if (!isOpen || options.empty())
      return;

    // Calculate dropdown list position and size
    int listX = x;
    int listY = y + height + 2;
    int listWidth = width;
    int visibleItemCount = min((int)options.size(), maxVisibleItems);
    int listHeight = visibleItemCount * itemHeight + 2;

    // Draw dropdown list background and border
    HBRUSH listBrush = CreateSolidBrush(listBgColor);
    HPEN listPen = CreatePen(PS_SOLID, 1, listBorderColor);

    HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, listBrush);
    HPEN oldPen = (HPEN)SelectObject(hdc, listPen);

    Rectangle(hdc, listX, listY, listX + listWidth, listY + listHeight);

    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(listBrush);
    DeleteObject(listPen);

    // Set up clipping
    RECT clipRect = {listX + 1, listY + 1, listX + listWidth - 1,
                     listY + listHeight - 1};
    HRGN clipRgn = CreateRectRgn(clipRect.left, clipRect.top, clipRect.right,
                                 clipRect.bottom);
    SelectClipRgn(hdc, clipRgn);

    // Draw items
    HFONT hFont = fontCache.getFont(fontSize, fontWeight);
    HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);
    SetBkMode(hdc, TRANSPARENT);

    int startIndex = scrollOffset;
    int endIndex = min((int)options.size(), scrollOffset + visibleItemCount);

    for (int i = startIndex; i < endIndex; i++) {
      int itemY = listY + 1 + (i - scrollOffset) * itemHeight;

      // Draw item background if hovered or selected
      if (i == hoveredItemIndex) {
        HBRUSH hoverBrush = CreateSolidBrush(itemHoverColor);
        RECT itemRect = {listX + 1, itemY, listX + listWidth - 1,
                         itemY + itemHeight};
        FillRect(hdc, &itemRect, hoverBrush);
        DeleteObject(hoverBrush);
      } else if (i == selectedIndex) {
        HBRUSH selectedBrush = CreateSolidBrush(itemSelectedColor);
        RECT itemRect = {listX + 1, itemY, listX + listWidth - 1,
                         itemY + itemHeight};
        FillRect(hdc, &itemRect, selectedBrush);
        DeleteObject(selectedBrush);
      }

      // Draw item text
      RECT textRect = {listX + 12, itemY, listX + listWidth - 12,
                       itemY + itemHeight};
      SetTextColor(hdc, RGB(30, 30, 30));
      DrawText(hdc, options[i].c_str(), -1, &textRect,
               DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }

    SelectObject(hdc, hOldFont);
    SelectClipRgn(hdc, nullptr);
    DeleteObject(clipRgn);
  }

  void selectItem(int index) {
    if (index < 0 || index >= (int)options.size())
      return;

    selectedIndex = index;

    // Notify callback
    if (onSelectionChanged)
      onSelectionChanged(selectedIndex, options[selectedIndex]);

    // Update bound states
    if (boundIntState)
      boundIntState->set(selectedIndex);

    if (boundStringState)
      boundStringState->set(options[selectedIndex]);

    markNeedsPaint();
  }

  void ensureItemVisible(int index) {
    if (index < scrollOffset) {
      scrollOffset = index;
    } else if (index >= scrollOffset + maxVisibleItems) {
      scrollOffset = index - maxVisibleItems + 1;
    }
  }

  int findOptionIndex(const std::string &value) const {
    for (int i = 0; i < (int)options.size(); i++) {
      if (options[i] == value)
        return i;
    }
    return -1;
  }
};

using DropdownWidgetPtr = std::shared_ptr<DropdownWidget>;

inline DropdownWidgetPtr
Dropdown(const std::vector<std::string> &options = {}) {
  auto w = std::make_shared<DropdownWidget>();
  if (!options.empty())
    w->setOptions(options);
  return w;
}

using TooltipWidgetPtr = std::shared_ptr<TooltipWidget>;

// ============================================================================
// FACTORY
// ============================================================================

inline TooltipWidgetPtr Tooltip(WidgetPtr anchor, const std::string &tooltip) {
  return std::make_shared<TooltipWidget>(anchor, tooltip);
}

using DialogWidgetPtr = std::shared_ptr<DialogWidget>;

inline DialogWidgetPtr Dialog(WidgetPtr content = nullptr) {
  auto w = std::make_shared<DialogWidget>();
  if (content)
    w->setContent(content);
  return w;
}


using ContextMenuWidgetPtr = std::shared_ptr<ContextMenuWidget>;

// ============================================================================
// FACTORY
// ============================================================================

inline ContextMenuWidgetPtr
ContextMenu(WidgetPtr anchor, const std::vector<ContextMenuItem> &items) {
  return std::make_shared<ContextMenuWidget>(anchor, items);
}

#endif // FLUX_CONTEXT_MENU_HPP