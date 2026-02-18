#ifndef FLUX_CONTEXT_MENU_HPP
#define FLUX_CONTEXT_MENU_HPP

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
  FluxAppWidget *fluxApp = nullptr;

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

  void setFluxApp(FluxAppWidget *app) { fluxApp = app; }

  // ----------------------------------------------------------------
  // onDetach — called by FluxUI::rebuild() before old tree is dropped
  // ----------------------------------------------------------------
  void onDetach() override {
    if (isOpen && fluxApp) {
      fluxApp->removeOverlay(this);
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
    if (isOpen || !fluxApp || items.empty())
      return;

    computeMenuGeometry(cursorX, cursorY);
    isOpen = true;
    hoveredIndex = -1;
    selectedIndex = findFirstActionIndex(); // Start keyboard nav at first item

    fluxApp->addOverlay(
        this, [this](HDC hdc, FontCache &fc) { renderMenu(hdc, fc); },
        150 // zIndex: above Dropdown(100) and Tooltip(50), below Dialog(200)
    );
  }

  // ----------------------------------------------------------------
  // closeMenu
  // ----------------------------------------------------------------
  void closeMenu() {
    if (!isOpen || !fluxApp)
      return;

    isOpen = false;
    hoveredIndex = -1;
    selectedIndex = -1;
    fluxApp->removeOverlay(this);
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
  // renderMenu — called by FluxApp overlay system
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

using ContextMenuWidgetPtr = std::shared_ptr<ContextMenuWidget>;

// ============================================================================
// FACTORY
// ============================================================================

inline ContextMenuWidgetPtr
ContextMenu(WidgetPtr anchor, const std::vector<ContextMenuItem> &items) {
  return std::make_shared<ContextMenuWidget>(anchor, items);
}

#endif // FLUX_CONTEXT_MENU_HPP