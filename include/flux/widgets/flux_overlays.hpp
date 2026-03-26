#ifndef FLUX_OVERLAYS_HPP
#define FLUX_OVERLAYS_HPP

#include "flux_app.hpp"
#include "flux_core.hpp"
#include "flux_overlay_host.hpp"   // ← mixin interface (popup edition)
#include <algorithm>

// ============================================================================
// HELPERS
// ============================================================================

// Retrieve the top-level HWND from FluxUI so we can pass it to showPopup().
// Returns nullptr if no instance is active.
static inline HWND getFluxTopLevel()
{
    auto *ui = FluxUI::getCurrentInstance();
    return ui ? ui->getWindow() : nullptr;
}

// Convert a point in FluxUI client coordinates to screen coordinates.
static inline POINT fluxClientToScreen(int cx, int cy)
{
    HWND hw = getFluxTopLevel();
    POINT pt = {cx, cy};
    if (hw) ClientToScreen(hw, &pt);
    return pt;
}

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

  // Geometry stored in *screen* coords while the popup is open.
  int menuX = 0, menuY = 0;
  int menuW = 0, menuH = 0;

  // Window dimensions (client-area, used for boundary clamping)
  int windowW = 0, windowH = 0;

  std::vector<ContextMenuItem> items;
  int hoveredIndex = -1;
  int selectedIndex = -1;

  int itemHeight = 28;
  int separatorHeight = 9;
  int minWidth = 160;
  int paddingH = 12;
  int paddingV = 4;

  COLORREF menuBgColor       = RGB(255, 255, 255);
  COLORREF menuBorderColor   = RGB(180, 180, 180);
  COLORREF itemHoverColor    = RGB(240, 245, 250);
  COLORREF itemTextColor     = RGB(30,  30,  30);
  COLORREF itemDisabledColor = RGB(160, 160, 160);
  COLORREF separatorColor    = RGB(220, 220, 220);

  int menuFontSize     = 13;
  int menuBorderRadius = 6;
  int shadowOffset     = 3;

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

  // OverlayHost
  void setScaffold(ScaffoldWidget *s) override { scaffold = s; }

  void onDetach() override {
    if (isOpen) closeMenu();
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
  // Layout
  // ----------------------------------------------------------------
  void computeLayout(GraphicsContext &ctx, const BoxConstraints &constraints,
                     FontCache &fontCache) override {
    windowW = constraints.maxWidth;
    windowH = constraints.maxHeight;

    if (autoWidth)  width  = constraints.maxWidth;
    if (autoHeight) height = constraints.maxHeight;

    if (!children.empty()) {
      auto &anchor = children[0];
      anchor->computeLayout(ctx, constraints, fontCache);
      if (autoWidth)  width  = anchor->width;
      if (autoHeight) height = anchor->height;
    }

    applyConstraints();
    needsLayout = false;
  }

  void positionChildren(int /*contentX*/, int /*contentY*/,
                        int /*contentWidth*/, int /*contentHeight*/) override {
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
    if (!children.empty()) children[0]->render(ctx, fontCache);
    needsPaint = false;
  }

  // ----------------------------------------------------------------
  // renderPopupContent — called by OverlayHost::paintLayered_
  // ctx is sized (menuW × menuH), pre-cleared to transparent.
  // Draw relative to (0,0) within the popup.
  // ----------------------------------------------------------------
  void renderPopupContent(GraphicsContext &ctx, FontCache &fontCache) override {
    if (!isOpen || items.empty()) return;

    // Shadow
    HBRUSH shadowBrush = CreateSolidBrush(RGB(0, 0, 0));
    HRGN shadowRgn = CreateRoundRectRgn(
        shadowOffset, shadowOffset,
        menuW + shadowOffset, menuH + shadowOffset,
        menuBorderRadius * 2, menuBorderRadius * 2);
    FillRgn(ctx.hdc, shadowRgn, shadowBrush);
    DeleteObject(shadowRgn);
    DeleteObject(shadowBrush);

    // Background + border
    HPEN   pen      = CreatePen(PS_SOLID, 1, menuBorderColor);
    HBRUSH brush    = CreateSolidBrush(menuBgColor);
    HPEN   oldPen   = (HPEN)  SelectObject(ctx.hdc, pen);
    HBRUSH oldBrush = (HBRUSH)SelectObject(ctx.hdc, brush);
    RoundRect(ctx.hdc, 0, 0, menuW, menuH,
              menuBorderRadius * 2, menuBorderRadius * 2);
    SelectObject(ctx.hdc, oldBrush); SelectObject(ctx.hdc, oldPen);
    DeleteObject(brush); DeleteObject(pen);

    // Items
    HFONT  hFont    = fontCache.getFont(menuFontSize, FontWeight::Normal);
    HFONT  hOldFont = (HFONT)SelectObject(ctx.hdc, hFont);
    SetBkMode(ctx.hdc, TRANSPARENT);

    int currentY = paddingV;
    for (int i = 0; i < (int)items.size(); i++) {
      const auto &item = items[i];
      if (item.type == ContextMenuItem::Type::Separator) {
        int sepY = currentY + separatorHeight / 2;
        HPEN sepPen    = CreatePen(PS_SOLID, 1, separatorColor);
        HPEN oldSepPen = (HPEN)SelectObject(ctx.hdc, sepPen);
        MoveToEx(ctx.hdc, paddingH,       sepY, nullptr);
        LineTo  (ctx.hdc, menuW - paddingH, sepY);
        SelectObject(ctx.hdc, oldSepPen);
        DeleteObject(sepPen);
        currentY += separatorHeight;
      } else {
        if (i == hoveredIndex && item.enabled) {
          HBRUSH hoverBrush = CreateSolidBrush(itemHoverColor);
          RECT   hoverRect  = {2, currentY, menuW - 2, currentY + itemHeight};
          FillRect(ctx.hdc, &hoverRect, hoverBrush);
          DeleteObject(hoverBrush);
        }
        SetTextColor(ctx.hdc, item.enabled ? itemTextColor : itemDisabledColor);
        RECT textRect = {paddingH, currentY,
                         menuW - paddingH, currentY + itemHeight};
        DrawText(ctx.hdc, item.label.c_str(), -1, &textRect,
                 DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        currentY += itemHeight;
      }
    }
    SelectObject(ctx.hdc, hOldFont);
  }

  // ----------------------------------------------------------------
  // Mouse Events  (coordinates arrive in main-window client space)
  // ----------------------------------------------------------------
  bool handleMouseDown(int mx, int my) override {
    if (!isOpen) return false;

    // Convert screen-based menuX/Y to client-space for hit-test
    HWND hw = getFluxTopLevel();
    POINT origin = {menuX, menuY};
    if (hw) ScreenToClient(hw, &origin);

    if (mx >= origin.x && mx < origin.x + menuW &&
        my >= origin.y && my < origin.y + menuH) {
      int relativeY = my - origin.y - paddingV;
      int itemIdx   = getItemIndexAtY(relativeY);
      if (itemIdx >= 0 && itemIdx < (int)items.size()) {
        const auto &item = items[itemIdx];
        if (item.type == ContextMenuItem::Type::Action && item.enabled) {
          if (item.action) item.action();
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
    if (!isOpen) return false;

    HWND hw = getFluxTopLevel();
    POINT origin = {menuX, menuY};
    if (hw) ScreenToClient(hw, &origin);

    if (mx >= origin.x && mx < origin.x + menuW &&
        my >= origin.y && my < origin.y + menuH) {
      int relativeY = my - origin.y - paddingV;
      int itemIdx   = getItemIndexAtY(relativeY);
      if (itemIdx != hoveredIndex) {
        hoveredIndex  = itemIdx;
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
    if (!isOpen) return false;
    closeMenu();
    return true;
  }

  bool handleKeyDown(int keyCode) override {
    if (!isOpen || items.empty()) return false;
    switch (keyCode) {
    case VK_ESCAPE: closeMenu(); return true;
    case VK_UP:     moveToPrevious(); return true;
    case VK_DOWN:   moveToNext();     return true;
    case VK_HOME:
      selectedIndex = hoveredIndex = findFirstActionIndex();
      refreshPopupIfOpen_(); return true;
    case VK_END:
      selectedIndex = hoveredIndex = findLastActionIndex();
      refreshPopupIfOpen_(); return true;
    case VK_RETURN:
    case VK_SPACE:
      if (selectedIndex >= 0 && selectedIndex < (int)items.size()) {
        const auto &item = items[selectedIndex];
        if (item.type == ContextMenuItem::Type::Action && item.enabled) {
          if (item.action) item.action();
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
    std::function<bool(int,int)> previous = anchor->onRightClick;
    anchor->onRightClick = [this, anchor, previous](int mx, int my) {
      if (mx >= anchor->x && mx < anchor->x + anchor->width &&
          my >= anchor->y && my < anchor->y + anchor->height) {
        openMenuAt(mx, my);
        return true;
      }
      if (previous) return previous(mx, my);
      return false;
    };
  }

  void openMenuAt(int clientX, int clientY) {
    if (isOpen || items.empty()) return;
    computeMenuGeometry(clientX, clientY);
    isOpen        = true;
    hoveredIndex  = -1;
    selectedIndex = findFirstActionIndex();

    HWND hw = getFluxTopLevel();
    // menuX/Y are already screen coords after computeMenuGeometry
    if (hw) {
      auto *ui = FluxUI::getCurrentInstance();
      FontCache &fc = ui->getFontCache();
      // Inflate popup size by shadowOffset so shadow is visible
      showPopup(hw, menuX, menuY,
                menuW + shadowOffset, menuH + shadowOffset, fc);
    }

    // Also register with scaffold for hit-test / keyboard routing
    if (scaffold) {
      scaffold->addOverlay(
          this,
          [this](HDC /*hdc*/, FontCache &/*fc*/) { /* visual is in popup */ },
          150);
    }
  }

  void closeMenu() {
    if (!isOpen) return;
    isOpen        = false;
    hoveredIndex  = -1;
    selectedIndex = -1;
    hidePopup();
    if (scaffold) scaffold->removeOverlay(this);
  }

  void computeMenuGeometry(int clientX, int clientY) {
    int maxLabelWidth = 0;
    for (const auto &item : items) {
      if (item.type == ContextMenuItem::Type::Action) {
        int lw = (int)item.label.size() * (menuFontSize / 2);
        maxLabelWidth = max(maxLabelWidth, lw);
      }
    }
    menuW = max(minWidth, maxLabelWidth + paddingH * 2);

    int totalH = paddingV * 2;
    for (const auto &item : items)
      totalH += (item.type == ContextMenuItem::Type::Separator)
                    ? separatorHeight : itemHeight;
    menuH = totalH;

    // Convert to screen coordinates and clamp to monitor
    POINT sc = fluxClientToScreen(clientX, clientY);
    menuX = sc.x;
    menuY = sc.y;

    HMONITOR mon = MonitorFromPoint(sc, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(mon, &mi)) {
      if (menuX + menuW > mi.rcWork.right)  menuX = mi.rcWork.right  - menuW;
      if (menuX < mi.rcWork.left)           menuX = mi.rcWork.left;
      if (menuY + menuH > mi.rcWork.bottom) menuY = mi.rcWork.bottom - menuH;
      if (menuY < mi.rcWork.top)            menuY = mi.rcWork.top;
    }
  }

  void refreshPopupIfOpen_() {
    if (!isOpen || !popupVisible()) return;
    auto *ui = FluxUI::getCurrentInstance();
    if (ui) refreshPopup(ui->getFontCache());
  }

  void moveToPrevious() {
    if (selectedIndex < 0) selectedIndex = findFirstActionIndex();
    else {
      int prev = selectedIndex - 1;
      while (prev >= 0) {
        if (items[prev].type == ContextMenuItem::Type::Action &&
            items[prev].enabled) { selectedIndex = prev; break; }
        prev--;
      }
      if (prev < 0) selectedIndex = findLastActionIndex();
    }
    hoveredIndex = selectedIndex;
    refreshPopupIfOpen_();
  }

  void moveToNext() {
    if (selectedIndex < 0) selectedIndex = findFirstActionIndex();
    else {
      int next = selectedIndex + 1;
      while (next < (int)items.size()) {
        if (items[next].type == ContextMenuItem::Type::Action &&
            items[next].enabled) { selectedIndex = next; break; }
        next++;
      }
      if (next >= (int)items.size()) selectedIndex = findFirstActionIndex();
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
                  ? separatorHeight : itemHeight;
      if (relativeY >= currentY && relativeY < currentY + h) {
        if (items[i].type == ContextMenuItem::Type::Separator) return -1;
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
  ScaffoldWidget *scaffold       = nullptr;
  bool            contentLayoutDirty = true;

public:
  bool    isOpen             = false;
  WidgetPtr content;

  int      dialogWidth       = 400;
  int      dialogHeight      = 300;
  COLORREF overlayColor      = RGB(0,   0,   0);
  int      overlayAlpha      = 128;
  COLORREF dialogBgColor     = RGB(255, 255, 255);
  COLORREF dialogBorderColor = RGB(200, 200, 200);
  int      dialogBorderRadius = 8;
  int      dialogPadding      = 24;

  std::function<void()> onClose;
  bool closeOnClickOutside = true;

  DialogWidget() { hasBackground = false; }

  // OverlayHost
  void setScaffold(ScaffoldWidget *s) override { scaffold = s; }

  // ----------------------------------------------------------------
  // Layout
  // ----------------------------------------------------------------
  void computeLayout(GraphicsContext &/*ctx*/, const BoxConstraints &, FontCache &) override {
    width = 0; height = 0;
    needsLayout = false;
  }
  void render(GraphicsContext &/*ctx*/, FontCache &) override { needsPaint = false; }

  // ----------------------------------------------------------------
  // renderPopupContent
  // The popup covers the entire application window so we can draw the
  // dim overlay AND the dialog box inside it.
  // ----------------------------------------------------------------
  void renderPopupContent(GraphicsContext &ctx, FontCache &fontCache) override {
    if (!isOpen) return;

    HWND hw   = getFluxTopLevel();
    int  winW = 800, winH = 600;
    if (hw) {
      RECT cr; GetClientRect(hw, &cr);
      winW = cr.right; winH = cr.bottom;
    }

    // Semi-transparent dim overlay
    {
      HDC     tmpDC  = CreateCompatibleDC(ctx.hdc);
      HBITMAP tmpBmp = CreateCompatibleBitmap(ctx.hdc, winW, winH);
      HBITMAP tmpOld = (HBITMAP)SelectObject(tmpDC, tmpBmp);
      HBRUSH  ovBrush = CreateSolidBrush(overlayColor);
      RECT    all     = {0, 0, winW, winH};
      FillRect(tmpDC, &all, ovBrush);
      DeleteObject(ovBrush);
      BLENDFUNCTION bf = {AC_SRC_OVER, 0, (BYTE)overlayAlpha, 0};
      AlphaBlend(ctx.hdc, 0, 0, winW, winH,
                 tmpDC, 0, 0, winW, winH, bf);
      SelectObject(tmpDC, tmpOld);
      DeleteObject(tmpBmp);
      DeleteDC(tmpDC);
    }

    int dialogX = (winW - dialogWidth)  / 2;
    int dialogY = (winH - dialogHeight) / 2;

    // Dialog box background
    HBRUSH dialogBrush = CreateSolidBrush(dialogBgColor);
    HPEN   dialogPen   = CreatePen(PS_SOLID, 1, dialogBorderColor);
    HBRUSH oldBrush    = (HBRUSH)SelectObject(ctx.hdc, dialogBrush);
    HPEN   oldPen      = (HPEN)  SelectObject(ctx.hdc, dialogPen);
    RoundRect(ctx.hdc, dialogX, dialogY,
              dialogX + dialogWidth, dialogY + dialogHeight,
              dialogBorderRadius * 2, dialogBorderRadius * 2);
    SelectObject(ctx.hdc, oldBrush); SelectObject(ctx.hdc, oldPen);
    DeleteObject(dialogBrush);   DeleteObject(dialogPen);

    // Content
    if (content) {
      int contentX = dialogX + dialogPadding;
      int contentY = dialogY + dialogPadding;
      int contentW = dialogWidth  - dialogPadding * 2;
      int contentH = dialogHeight - dialogPadding * 2;

      if (contentLayoutDirty) {
        content->computeLayout(ctx, BoxConstraints::tight(contentW, contentH),
                               fontCache);
        content->x = contentX;
        content->y = contentY;
        content->positionChildren(
            contentX + content->paddingLeft, contentY + content->paddingTop,
            content->width  - content->paddingLeft - content->paddingRight,
            content->height - content->paddingTop  - content->paddingBottom);
        contentLayoutDirty = false;
      }
      content->render(ctx, fontCache);
    }
  }

  // ----------------------------------------------------------------
  // Mouse Events
  // ----------------------------------------------------------------
  bool handleMouseDown(int mx, int my) override {
    if (!isOpen) return false;

    HWND hw = getFluxTopLevel();
    int  winW = 800, winH = 600;
    if (hw) { RECT cr; GetClientRect(hw, &cr); winW = cr.right; winH = cr.bottom; }

    int dialogX = (winW - dialogWidth)  / 2;
    int dialogY = (winH - dialogHeight) / 2;

    if (mx < dialogX || mx >= dialogX + dialogWidth ||
        my < dialogY || my >= dialogY + dialogHeight) {
      if (closeOnClickOutside) close();
      return true;
    }

    if (content) {
      Widget *toFocus = nullptr;
      if (findAndHandleMouseEvent(content.get(), mx, my,
          [mx, my, &toFocus](Widget *w) {
            bool handled = w->handleMouseDown(mx, my);
            if (!handled && w->onClick &&
                mx >= w->x && mx < w->x + w->width &&
                my >= w->y && my < w->y + w->height) {
              w->onClick(); handled = true;
            }
            if (handled && w->isFocusable) toFocus = w;
            return handled;
          })) {
        if (toFocus && FluxUI::getCurrentInstance())
          FluxUI::getCurrentInstance()->setFocus(toFocus);
        return true;
      }
      Widget *clicked = findWidgetAt(content.get(), mx, my);
      if (clicked) {
        if (clicked->onClick) { clicked->onClick(); return true; }
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

  // ----------------------------------------------------------------
  // Open / Close
  // ----------------------------------------------------------------
  void open() {
    if (isOpen) return;
    isOpen             = true;
    contentLayoutDirty = true;

    HWND hw = getFluxTopLevel();
    int  winW = 800, winH = 600;
    if (hw) { RECT cr; GetClientRect(hw, &cr); winW = cr.right; winH = cr.bottom; }

    // Layout content before first paint
    if (content && hw) {
      HDC hdc = GetDC(hw);
      GraphicsContext ctx(hdc);  
      FontCache &fc = FluxUI::getCurrentInstance()->getFontCache();
      int contentW = dialogWidth  - dialogPadding * 2;
      int contentH = dialogHeight - dialogPadding * 2;
      content->computeLayout(ctx, BoxConstraints::tight(contentW, contentH), fc);

      int dialogX  = (winW - dialogWidth)  / 2;
      int dialogY  = (winH - dialogHeight) / 2;
      int contentX = dialogX + dialogPadding;
      int contentY = dialogY + dialogPadding;
      content->x = contentX;
      content->y = contentY;
      content->positionChildren(
          contentX + content->paddingLeft, contentY + content->paddingTop,
          content->width  - content->paddingLeft - content->paddingRight,
          content->height - content->paddingTop  - content->paddingBottom);
      ReleaseDC(hw, hdc);
      contentLayoutDirty = false;
    }

    if (hw) {
      // The popup covers the whole client area so the dim overlay works
      POINT origin = fluxClientToScreen(0, 0);
      FontCache &fc = FluxUI::getCurrentInstance()->getFontCache();
      showPopup(hw, origin.x, origin.y, winW, winH, fc);
    }

    if (scaffold) {
      scaffold->addOverlay(this,
          [this](HDC, FontCache &) { /* visual in popup */ }, 200);
    }
    markNeedsPaint();
  }

  void close() {
    if (!isOpen) return;
    isOpen             = true; // keep true briefly to let cleanup happen
    contentLayoutDirty = true;

    if (FluxUI::getCurrentInstance()) {
      Widget *focused = FluxUI::getCurrentInstance()->getFocusedWidget();
      if (focused && isDescendantOf(focused, content.get()))
        FluxUI::getCurrentInstance()->setFocus(nullptr);
    }

    isOpen = false;
    hidePopup();
    if (scaffold) scaffold->removeOverlay(this);
    if (onClose) onClose();
    markNeedsPaint();
  }

  // ----------------------------------------------------------------
  // Builder Methods
  // ----------------------------------------------------------------
  std::shared_ptr<DialogWidget> setContent(WidgetPtr child) {
    content = child;
    if (content) content->parent = this;
    contentLayoutDirty = true;
    markNeedsPaint();
    return std::static_pointer_cast<DialogWidget>(shared_from_this());
  }
  std::shared_ptr<DialogWidget> setSize(int w, int h) {
    dialogWidth = w; dialogHeight = h;
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
    if (!candidate || !subtreeRoot) return false;
    Widget *current = candidate->parent;
    while (current) {
      if (current == subtreeRoot) return true;
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

  // Screen coordinates of the popup origin
  int tipScreenX = 0, tipScreenY = 0;
  int tipW = 0, tipH = 0;

  std::string     tipText;
  TooltipPosition preferredPosition = TooltipPosition::Auto;
  COLORREF tipBgColor     = RGB(50,  50,  50);
  COLORREF tipTextColor   = RGB(255, 255, 255);
  COLORREF tipBorderColor = RGB(80,  80,  80);
  int tipFontSize     = 12;
  int tipPadH         = 10;
  int tipPadV         = 6;
  int tipBorderRadius = 4;
  int tipMaxWidth     = 240;

public:
  bool isVisible = false;

  explicit TooltipWidget(WidgetPtr anchor, const std::string &tooltip)
      : tipText(tooltip) {
    if (anchor) {
      addChild(anchor);
      chainAnchorHover(anchor.get());
    }
  }

  // OverlayHost
  void setScaffold(ScaffoldWidget *s) override { scaffold = s; }

  void onDetach() override {
    if (isVisible) closeTooltip();
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
  // Layout
  // ----------------------------------------------------------------
  void computeLayout(GraphicsContext &ctx, const BoxConstraints &constraints,
                     FontCache &fontCache) override {
    if (autoWidth)  width  = constraints.maxWidth;
    if (autoHeight) height = constraints.maxHeight;
    if (!children.empty()) {
      auto &anchor = children[0];
      anchor->computeLayout(ctx, constraints, fontCache);
      if (autoWidth)  width  = anchor->width;
      if (autoHeight) height = anchor->height;
    }
    applyConstraints();
    needsLayout = false;
  }

  void positionChildren(int, int, int, int) override {
    if (!children.empty()) {
      auto &anchor = children[0];
      anchor->x = x; anchor->y = y;
      anchor->positionChildren(
          anchor->x + anchor->paddingLeft, anchor->y + anchor->paddingTop,
          anchor->width - anchor->paddingLeft - anchor->paddingRight,
          anchor->height - anchor->paddingTop - anchor->paddingBottom);
    }
  }

  void render(GraphicsContext &ctx, FontCache &fontCache) override {
    if (!children.empty()) children[0]->render(ctx, fontCache);
    needsPaint = false;
  }

  // ----------------------------------------------------------------
  // renderPopupContent — paint bubble into (tipW × tipH) DC
  // ----------------------------------------------------------------
  void renderPopupContent(GraphicsContext &ctx, FontCache &fontCache) override {
    if (!isVisible || tipText.empty()) return;

    // Shadow
    HBRUSH shadowBrush = CreateSolidBrush(RGB(0, 0, 0));
    HRGN   shadowRgn   = CreateRoundRectRgn(2, 2, tipW + 2, tipH + 2,
                                             tipBorderRadius * 2, tipBorderRadius * 2);
    FillRgn(ctx.hdc, shadowRgn, shadowBrush);
    DeleteObject(shadowRgn); DeleteObject(shadowBrush);

    // Bubble
    HPEN   pen    = CreatePen(PS_SOLID, 1, tipBorderColor);
    HBRUSH brush  = CreateSolidBrush(tipBgColor);
    HPEN   oldPen = (HPEN)  SelectObject(ctx.hdc, pen);
    HBRUSH oldBrush = (HBRUSH)SelectObject(ctx.hdc, brush);
    RoundRect(ctx.hdc, 0, 0, tipW, tipH,
              tipBorderRadius * 2, tipBorderRadius * 2);
    SelectObject(ctx.hdc, oldBrush); SelectObject(ctx.hdc, oldPen);
    DeleteObject(brush); DeleteObject(pen);

    // Text
    HFONT hFont    = fontCache.getFont(tipFontSize, FontWeight::Normal);
    HFONT hOldFont = (HFONT)SelectObject(ctx.hdc, hFont);
    SetTextColor(ctx.hdc, tipTextColor);
    SetBkMode(ctx.hdc, TRANSPARENT);
    RECT textRect = {tipPadH, tipPadV, tipW - tipPadH, tipH - tipPadV};
    DrawText(ctx.hdc, tipText.c_str(), -1, &textRect,
             DT_CENTER | DT_VCENTER | DT_WORDBREAK | DT_END_ELLIPSIS);
    SelectObject(ctx.hdc, hOldFont);
  }

private:
  void chainAnchorHover(Widget *anchor) {
    HoverHandler previous = anchor->onHover;
    anchor->onHover = [this, previous](bool hovered) {
      if (hovered) openTooltip();
      else          closeTooltip();
      if (previous) previous(hovered);
    };
  }

  void openTooltip() {
    if (isVisible || tipText.empty()) return;
    computeBubbleGeometry();
    isVisible = true;

    HWND hw = getFluxTopLevel();
    if (hw) {
      auto *ui = FluxUI::getCurrentInstance();
      // Inflate popup by 2px for shadow
      showPopup(hw, tipScreenX, tipScreenY, tipW + 2, tipH + 2,
                ui->getFontCache());
    }

    if (scaffold) {
      scaffold->addOverlay(this,
          [this](HDC, FontCache &) { /* visual in popup */ }, 50);
    }
  }

  void closeTooltip() {
    if (!isVisible) return;
    isVisible = false;
    hidePopup();
    if (scaffold) scaffold->removeOverlay(this);
  }

  void computeBubbleGeometry() {
    int charW  = (int)(tipFontSize * 0.62);
    int lineH  = tipFontSize + 4;
    int textW  = (int)tipText.size() * charW;
    int maxTW  = tipMaxWidth - tipPadH * 2;
    int lines  = max(1, (textW + maxTW - 1) / maxTW);

    tipW = min(textW + tipPadH * 2, tipMaxWidth);
    tipH = lines * lineH + tipPadV * 2;

    // Anchor centre in client coords
    int anchorCX = x + width  / 2;
    int anchorCY = y;  // top of anchor

    // Convert to screen
    POINT sc = fluxClientToScreen(anchorCX - tipW / 2, anchorCY);
    tipScreenX = sc.x;
    tipScreenY = sc.y - tipH - 6; // try above first

    // If above goes off-screen, place below
    HMONITOR mon = MonitorFromPoint(sc, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(mon, &mi)) {
      bool wantAbove = (preferredPosition != TooltipPosition::Below);
      POINT below = fluxClientToScreen(anchorCX - tipW / 2,
                                        y + height + 6);
      if (wantAbove && tipScreenY >= mi.rcWork.top) {
        // above fits — already set
      } else {
        tipScreenY = below.y;
      }
      // Horizontal clamp
      if (tipScreenX + tipW > mi.rcWork.right)
        tipScreenX = mi.rcWork.right  - tipW;
      if (tipScreenX < mi.rcWork.left)
        tipScreenX = mi.rcWork.left;
    }
  }
};

// ============================================================================
// DROPDOWN WIDGET
// ============================================================================

class DropdownWidget : public Widget, public OverlayHost {
private:
  ScaffoldWidget *scaffold = nullptr;

  // Screen coords of the dropdown list popup
  int listScreenX = 0, listScreenY = 0;
  int listWidth_  = 0;

public:
  std::vector<std::string> options;
  int  selectedIndex    = -1;
  bool isOpen           = false;
  int  hoveredItemIndex = -1;

  int itemHeight       = 32;
  int maxVisibleItems  = 6;
  int arrowSize        = 8;
  int scrollOffset     = 0;

  COLORREF dropdownBgColor            = RGB(255, 255, 255);
  COLORREF dropdownBorderColor        = RGB(180, 180, 180);
  COLORREF dropdownFocusedBorderColor = RGB(33,  150, 243);
  COLORREF placeholderColor           = RGB(150, 150, 150);
  COLORREF itemHoverColor             = RGB(240, 240, 240);
  COLORREF itemSelectedColor          = RGB(230, 245, 255);
  COLORREF listBgColor                = RGB(255, 255, 255);
  COLORREF listBorderColor            = RGB(200, 200, 200);
  COLORREF arrowColor                 = RGB(100, 100, 100);

  std::string placeholder = "Select an option...";
  std::function<void(int, const std::string &)> onSelectionChanged;

  DropdownWidget() {
    isFocusable = true;
    hasBorder = true;
    hasBackground = true;
    backgroundColor = dropdownBgColor;
    borderColor     = dropdownBorderColor;
    borderWidth     = 1;
    borderRadius    = 4;
    paddingLeft     = 12;
    paddingRight    = 30;
    paddingTop = paddingBottom = 8;
    height     = 36;
    autoHeight = false;
  }

  // OverlayHost
  void setScaffold(ScaffoldWidget *s) override { scaffold = s; }

  // ----------------------------------------------------------------
  // Layout
  // ----------------------------------------------------------------
  void computeLayout(GraphicsContext &/*ctx*/, const BoxConstraints &constraints,
                     FontCache &) override {
    if (autoWidth) width = constraints.maxWidth;
    applyConstraints();
    needsLayout = false;
  }

  // ----------------------------------------------------------------
  // Render main box (the list is in the popup)
  // ----------------------------------------------------------------
  void render(GraphicsContext &ctx, FontCache &fontCache) override {
    borderColor = isFocused ? dropdownFocusedBorderColor : dropdownBorderColor;
    drawRoundedRectangle(ctx);

    HFONT  hFont    = fontCache.getFont(fontSize, fontWeight);
    HFONT  hOldFont = (HFONT)SelectObject(ctx.hdc, hFont);
    SetBkMode(ctx.hdc, TRANSPARENT);

    RECT textRect = {x + paddingLeft, y + paddingTop,
                     x + width - paddingRight, y + height - paddingBottom};

    if (selectedIndex >= 0 && selectedIndex < (int)options.size()) {
      SetTextColor(ctx.hdc, getCurrentTextColor());
      DrawText(ctx.hdc, options[selectedIndex].c_str(), -1, &textRect,
               DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    } else {
      SetTextColor(ctx.hdc, placeholderColor);
      DrawText(ctx.hdc, placeholder.c_str(), -1, &textRect,
               DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }

    // Arrow
    int  arrowX = x + width - paddingRight + 10;
    int  arrowY = y + height / 2;
    HPEN arrowPen = CreatePen(PS_SOLID, 2, arrowColor);
    HPEN oldPen   = (HPEN)SelectObject(ctx.hdc, arrowPen);
    if (isOpen) {
      MoveToEx(ctx.hdc, arrowX - arrowSize/2, arrowY + arrowSize/4, nullptr);
      LineTo  (ctx.hdc, arrowX,               arrowY - arrowSize/4);
      LineTo  (ctx.hdc, arrowX + arrowSize/2, arrowY + arrowSize/4);
    } else {
      MoveToEx(ctx.hdc, arrowX - arrowSize/2, arrowY - arrowSize/4, nullptr);
      LineTo  (ctx.hdc, arrowX,               arrowY + arrowSize/4);
      LineTo  (ctx.hdc, arrowX + arrowSize/2, arrowY - arrowSize/4);
    }
    SelectObject(ctx.hdc, oldPen);
    DeleteObject(arrowPen);
    SelectObject(ctx.hdc, hOldFont);
    needsPaint = false;
  }

  // ----------------------------------------------------------------
  // renderPopupContent — list panel, drawn into (listWidth_ × listH) DC
  // ----------------------------------------------------------------
  void renderPopupContent(GraphicsContext &ctx, FontCache &fontCache) override {
    if (!isOpen || options.empty()) return;

    int visibleCount = min((int)options.size(), maxVisibleItems);
    int listH        = visibleCount * itemHeight + 2;

    // Border + background
    HBRUSH listBrush = CreateSolidBrush(listBgColor);
    HPEN   listPen   = CreatePen(PS_SOLID, 1, listBorderColor);
    HBRUSH oldBrush  = (HBRUSH)SelectObject(ctx.hdc, listBrush);
    HPEN   oldPen    = (HPEN)  SelectObject(ctx.hdc, listPen);
    Rectangle(ctx.hdc, 0, 0, listWidth_, listH);
    SelectObject(ctx.hdc, oldBrush); SelectObject(ctx.hdc, oldPen);
    DeleteObject(listBrush);     DeleteObject(listPen);

    // Clip to list interior
    HRGN clipRgn = CreateRectRgn(1, 1, listWidth_ - 1, listH - 1);
    SelectClipRgn(ctx.hdc, clipRgn);

    HFONT hFont    = fontCache.getFont(fontSize, fontWeight);
    HFONT hOldFont = (HFONT)SelectObject(ctx.hdc, hFont);
    SetBkMode(ctx.hdc, TRANSPARENT);

    int endIndex = min((int)options.size(), scrollOffset + visibleCount);
    for (int i = scrollOffset; i < endIndex; i++) {
      int itemY = 1 + (i - scrollOffset) * itemHeight;
      if (i == hoveredItemIndex) {
        HBRUSH hb = CreateSolidBrush(itemHoverColor);
        RECT   ir = {1, itemY, listWidth_ - 1, itemY + itemHeight};
        FillRect(ctx.hdc, &ir, hb);
        DeleteObject(hb);
      } else if (i == selectedIndex) {
        HBRUSH sb = CreateSolidBrush(itemSelectedColor);
        RECT   ir = {1, itemY, listWidth_ - 1, itemY + itemHeight};
        FillRect(ctx.hdc, &ir, sb);
        DeleteObject(sb);
      }
      RECT textRect = {12, itemY, listWidth_ - 12, itemY + itemHeight};
      SetTextColor(ctx.hdc, RGB(30, 30, 30));
      DrawText(ctx.hdc, options[i].c_str(), -1, &textRect,
               DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    SelectObject(ctx.hdc, hOldFont);
    SelectClipRgn(ctx.hdc, nullptr);
    DeleteObject(clipRgn);
  }

  // ----------------------------------------------------------------
  // Mouse Events
  // ----------------------------------------------------------------
  bool handleMouseDown(int mx, int my) override {
    if (isOpen) {
      // Convert list screen origin to client coords for hit-test
      HWND hw = getFluxTopLevel();
      POINT pt = {listScreenX, listScreenY};
      if (hw) ScreenToClient(hw, &pt);

      int visibleCount = min((int)options.size(), maxVisibleItems);
      int listH        = visibleCount * itemHeight + 2;

      if (mx >= pt.x && mx < pt.x + listWidth_ &&
          my >= pt.y && my < pt.y + listH) {
        int itemIndex = scrollOffset + ((my - pt.y - 1) / itemHeight);
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
    if (!isOpen) return false;

    HWND hw = getFluxTopLevel();
    POINT pt = {listScreenX, listScreenY};
    if (hw) ScreenToClient(hw, &pt);

    int visibleCount = min((int)options.size(), maxVisibleItems);
    int listH        = visibleCount * itemHeight + 2;

    if (mx >= pt.x && mx < pt.x + listWidth_ &&
        my >= pt.y && my < pt.y + listH) {
      int itemIndex = scrollOffset + ((my - pt.y - 1) / itemHeight);
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
    if (!isOpen) return false;
    int maxScroll = max(0, (int)options.size() - maxVisibleItems);
    scrollOffset  = (delta > 0)
        ? max(0, scrollOffset - 1)
        : min(maxScroll, scrollOffset + 1);
    refreshDropdownPopup_();
    return true;
  }

  bool handleKeyDown(int keyCode) override {
    if (options.empty()) return false;
    switch (keyCode) {
    case VK_RETURN:
    case VK_SPACE:
      if (isOpen) {
        int idx = (hoveredItemIndex >= 0) ? hoveredItemIndex : selectedIndex;
        if (idx >= 0 && idx < (int)options.size()) selectItem(idx);
        closeDropdown();
      } else {
        openDropdown();
        hoveredItemIndex = selectedIndex;
        if (hoveredItemIndex >= 0) ensureItemVisible(hoveredItemIndex);
      }
      markNeedsPaint();
      return true;
    case VK_ESCAPE:
      if (isOpen) { closeDropdown(); markNeedsPaint(); return true; }
      break;
    case VK_UP:
      if (isOpen) {
        if (hoveredItemIndex < 0)      hoveredItemIndex = max(0, selectedIndex);
        else if (hoveredItemIndex > 0) hoveredItemIndex--;
        ensureItemVisible(hoveredItemIndex);
        refreshDropdownPopup_();
      } else if (selectedIndex > 0) { selectItem(selectedIndex - 1); }
      return true;
    case VK_DOWN:
      if (isOpen) {
        if (hoveredItemIndex < 0) hoveredItemIndex = max(0, selectedIndex);
        else if (hoveredItemIndex < (int)options.size() - 1) hoveredItemIndex++;
        ensureItemVisible(hoveredItemIndex);
        refreshDropdownPopup_();
      } else if (selectedIndex < (int)options.size() - 1) {
        selectItem(selectedIndex + 1);
      }
      return true;
    case VK_HOME:
      if (isOpen) { hoveredItemIndex = 0; scrollOffset = 0; refreshDropdownPopup_(); }
      else        { selectItem(0); }
      return true;
    case VK_END:
      if (isOpen) {
        hoveredItemIndex = (int)options.size() - 1;
        scrollOffset     = max(0, (int)options.size() - maxVisibleItems);
        refreshDropdownPopup_();
      } else { selectItem((int)options.size() - 1); }
      return true;
    }
    return false;
  }

  bool handleFocus(bool focused) override {
    isFocused = focused;
    if (!focused && isOpen) closeDropdown();
    markNeedsPaint();
    return true;
  }

  // ----------------------------------------------------------------
  // Builder Methods
  // ----------------------------------------------------------------
  std::shared_ptr<DropdownWidget> setOptions(const std::vector<std::string> &opts) {
    options = opts;
    if (selectedIndex >= (int)options.size()) selectedIndex = -1;
    markNeedsPaint();
    return std::static_pointer_cast<DropdownWidget>(shared_from_this());
  }
  std::shared_ptr<DropdownWidget> setPlaceholder(const std::string &ph) {
    placeholder = ph; markNeedsPaint();
    return std::static_pointer_cast<DropdownWidget>(shared_from_this());
  }
  std::shared_ptr<DropdownWidget> setItemHeight(int h) {
    itemHeight = h; markNeedsPaint();
    return std::static_pointer_cast<DropdownWidget>(shared_from_this());
  }
  std::shared_ptr<DropdownWidget> setMaxVisibleItems(int count) {
    maxVisibleItems = count; markNeedsPaint();
    return std::static_pointer_cast<DropdownWidget>(shared_from_this());
  }
  std::shared_ptr<DropdownWidget> setOnSelectionChanged(
      std::function<void(int, const std::string &)> callback) {
    onSelectionChanged = callback;
    return std::static_pointer_cast<DropdownWidget>(shared_from_this());
  }
  std::shared_ptr<DropdownWidget> setSelectedIndex(State<int> &state) {
    selectedIndex = state.get();
    state.bindProperty(shared_from_this(),
        [](Widget *w, const int &val) {
          static_cast<DropdownWidget *>(w)->selectedIndex = val;
        }, false);
    boundIntState = &state;
    return std::static_pointer_cast<DropdownWidget>(shared_from_this());
  }
  std::shared_ptr<DropdownWidget> setSelectedValue(State<std::string> &state) {
    selectedIndex = findOptionIndex(state.get());
    state.bindProperty(shared_from_this(),
        [](Widget *w, const std::string &val) {
          static_cast<DropdownWidget *>(w)->selectedIndex =
              static_cast<DropdownWidget *>(w)->findOptionIndex(val);
        }, false);
    boundStringState = &state;
    return std::static_pointer_cast<DropdownWidget>(shared_from_this());
  }
  std::shared_ptr<DropdownWidget> setWidth(int w) {
    width = w; autoWidth = false;
    return std::static_pointer_cast<DropdownWidget>(shared_from_this());
  }

  bool hasOverlay() const { return isOpen && !options.empty(); }

private:
  State<int>         *boundIntState    = nullptr;
  State<std::string> *boundStringState = nullptr;

  void openDropdown() {
    if (isOpen) return;
    isOpen           = true;
    hoveredItemIndex = -1;
    scrollOffset     = 0;

    HWND hw = getFluxTopLevel();
    if (hw) {
      listWidth_ = width;
      int visibleCount = min((int)options.size(), maxVisibleItems);
      int listH        = visibleCount * itemHeight + 2;

      // Position just below the dropdown box
      POINT sc = fluxClientToScreen(x, y + height + 2);
      listScreenX = sc.x;
      listScreenY = sc.y;

      // Clamp to monitor
      HMONITOR mon = MonitorFromPoint(sc, MONITOR_DEFAULTTONEAREST);
      MONITORINFO mi{}; mi.cbSize = sizeof(mi);
      if (GetMonitorInfoW(mon, &mi)) {
        if (listScreenX + listWidth_ > mi.rcWork.right)
          listScreenX = mi.rcWork.right - listWidth_;
        if (listScreenY + listH > mi.rcWork.bottom)
          listScreenY = fluxClientToScreen(x, y - listH - 2).y;
      }

      FontCache &fc = FluxUI::getCurrentInstance()->getFontCache();
      showPopup(hw, listScreenX, listScreenY, listWidth_, listH, fc);
    }

    if (scaffold) {
      scaffold->addOverlay(this,
          [this](HDC, FontCache &) { /* visual in popup */ }, 100);
    }
    markNeedsPaint();
  }

  void closeDropdown() {
    if (!isOpen) return;
    isOpen           = false;
    hoveredItemIndex = -1;
    hidePopup();
    if (scaffold) scaffold->removeOverlay(this);
    markNeedsPaint();
  }

  void refreshDropdownPopup_() {
    if (!isOpen || !popupVisible()) return;
    auto *ui = FluxUI::getCurrentInstance();
    if (ui) refreshPopup(ui->getFontCache());
  }

  void selectItem(int index) {
    if (index < 0 || index >= (int)options.size()) return;
    selectedIndex = index;
    if (onSelectionChanged)
      onSelectionChanged(selectedIndex, options[selectedIndex]);
    if (boundIntState)    boundIntState->set(selectedIndex);
    if (boundStringState) boundStringState->set(options[selectedIndex]);
    markNeedsPaint();
  }

  void ensureItemVisible(int index) {
    if (index < scrollOffset) scrollOffset = index;
    else if (index >= scrollOffset + maxVisibleItems)
      scrollOffset = index - maxVisibleItems + 1;
  }

  int findOptionIndex(const std::string &value) const {
    for (int i = 0; i < (int)options.size(); i++)
      if (options[i] == value) return i;
    return -1;
  }
};

// ============================================================================
// FACTORY FUNCTIONS
// ============================================================================

using DropdownWidgetPtr    = std::shared_ptr<DropdownWidget>;
using TooltipWidgetPtr     = std::shared_ptr<TooltipWidget>;
using DialogWidgetPtr      = std::shared_ptr<DialogWidget>;
using ContextMenuWidgetPtr = std::shared_ptr<ContextMenuWidget>;

inline DropdownWidgetPtr Dropdown(const std::vector<std::string> &options = {}) {
  auto w = std::make_shared<DropdownWidget>();
  if (!options.empty()) w->setOptions(options);
  return w;
}

inline TooltipWidgetPtr Tooltip(WidgetPtr anchor, const std::string &tooltip) {
  return std::make_shared<TooltipWidget>(anchor, tooltip);
}

inline DialogWidgetPtr Dialog(WidgetPtr content = nullptr) {
  auto w = std::make_shared<DialogWidget>();
  if (content) w->setContent(content);
  return w;
}

inline ContextMenuWidgetPtr ContextMenu(WidgetPtr anchor,
                                        const std::vector<ContextMenuItem> &items) {
  return std::make_shared<ContextMenuWidget>(anchor, items);
}

#endif // FLUX_OVERLAYS_HPP