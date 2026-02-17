#ifndef FLUX_DROPDOWN_HPP
#define FLUX_DROPDOWN_HPP

#include "flux_core.hpp"
#include "flux_state.hpp"
#include <iostream>

template <typename T> class State;

class Widget;

using WidgetPtr = std::shared_ptr<Widget>;
using ClickHandler = std::function<void()>;
using HoverHandler = std::function<void(bool)>;

// ============================================================================
// DROPDOWN WIDGET
// ============================================================================

class DropdownWidget : public Widget {
private:
  FluxAppWidget *fluxApp = nullptr; // Reference to app for overlay management

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
  void setFluxApp(FluxAppWidget *app) { fluxApp = app; }

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
  // Mouse Events (Now register/unregister overlay with FluxApp)
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
    if (isOpen || !fluxApp)
      return;

    isOpen = true;
    hoveredItemIndex = -1;
    scrollOffset = 0;

    // 🎯 Register overlay with FluxApp
    fluxApp->addOverlay(
        this,
        [this](HDC hdc, FontCache &fontCache) {
          this->renderDropdownList(hdc, fontCache);
        },
        100 // zIndex: render on top of everything
    );

    markNeedsPaint();
  }

  void closeDropdown() {
    if (!isOpen || !fluxApp)
      return;

    isOpen = false;
    hoveredItemIndex = -1;

    // 🎯 Unregister overlay from FluxApp
    fluxApp->removeOverlay(this);

    markNeedsPaint();
  }

  // ----------------------------------------------------------------
  // Render Dropdown List (Called by FluxApp overlay system)
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

#endif