#ifndef FLUX_MENU_BAR_HPP
#define FLUX_MENU_BAR_HPP

#include "flux_core.hpp"
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
    std::string                  label;
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
    int      barHeight        = 28;
    int      buttonPadH       = 12;   // horizontal padding inside each button
    COLORREF barBgColor       = RGB(245, 245, 245);
    COLORREF barBorderColor   = RGB(210, 210, 210);
    COLORREF btnHoverColor    = RGB(225, 235, 245);
    COLORREF btnOpenColor     = RGB(210, 228, 248);
    COLORREF btnTextColor     = RGB(30,  30,  30);

    // Drop-down list appearance (mirrors ContextMenuWidget)
    int      itemHeight       = 28;
    int      separatorHeight  = 9;
    int      minMenuWidth     = 160;
    int      menuPadH         = 12;
    int      menuPadV         = 4;
    int      menuBorderRadius = 6;
    int      menuFontSize     = 13;
    int      shadowOffset     = 3;

    COLORREF menuBgColor       = RGB(255, 255, 255);
    COLORREF menuBorderColor   = RGB(180, 180, 180);
    COLORREF itemHoverColor    = RGB(240, 245, 250);
    COLORREF itemTextColor     = RGB(30,  30,  30);
    COLORREF itemDisabledColor = RGB(160, 160, 160);
    COLORREF separatorColor    = RGB(220, 220, 220);

    // ── State ─────────────────────────────────────────────────────────────────
    int  openMenuIndex  = -1;   // which top-level entry is open (-1 = none)
    int  hoveredBtn     = -1;   // which button the mouse is over
    int  hoveredItem    = -1;   // which drop-down item is hovered

    explicit MenuBarWidget(std::vector<MenuBarItem> entries)
        : entries_(std::move(entries)) {}

    // OverlayHost
    void setScaffold(ScaffoldWidget *s) override { scaffold_ = s; }

    void onDetach() override {
        if (openMenuIndex >= 0) closeMenu_();
        Widget::onDetach();
    }

    // ── Layout ────────────────────────────────────────────────────────────────
    void computeLayout(GraphicsContext &ctx, const BoxConstraints &constraints,
                       FontCache &fontCache) override {
        if (autoWidth)  width  = constraints.maxWidth;
        height     = barHeight;
        autoHeight = false;

        // Measure button widths
        buttonRects_.resize(entries_.size());
        int curX = 0;
        for (int i = 0; i < (int)entries_.size(); i++) {
            int textW = _measureLabel(ctx, fontCache, entries_[i].label);
            int btnW  = textW + buttonPadH * 2;
            buttonRects_[i] = {curX, 0, curX + btnW, barHeight};
            curX += btnW;
        }

        applyConstraints();
        needsLayout = false;
    }

    void positionChildren(int, int, int, int) override {}

    // ── Render the bar ────────────────────────────────────────────────────────
    void render(GraphicsContext &ctx, FontCache &fontCache) override {
        if (!visible) return;

        // Flush any deferred hot-track switch (safe here — not inside event dispatch)
        if (pendingSwitch_ >= 0) {
            int target = pendingSwitch_;
            pendingSwitch_ = -1;
            closeMenu_();
            openMenu_(target);
            // fall through — keep painting the bar normally
        }

        // Bar background
        {
            Gdiplus::Graphics g(ctx.hdc);
            Gdiplus::Color bg(255, GetRValue(barBgColor),
                              GetGValue(barBgColor), GetBValue(barBgColor));
            Gdiplus::SolidBrush brush(bg);
            g.FillRectangle(&brush, x, y, width, height);

            // Bottom border line
            Gdiplus::Color bc(255, GetRValue(barBorderColor),
                              GetGValue(barBorderColor), GetBValue(barBorderColor));
            Gdiplus::Pen pen(bc, 1.0f);
            g.DrawLine(&pen, x, y + height - 1, x + width, y + height - 1);
        }

        // Buttons
        HFONT  hFont    = fontCache.getFont(menuFontSize, FontWeight::Normal);
        HFONT  hOldFont = (HFONT)SelectObject(ctx.hdc, hFont);
        SetBkMode(ctx.hdc, TRANSPARENT);

        for (int i = 0; i < (int)entries_.size(); i++) {
            RECT &r = buttonRects_[i];
            RECT  absR = {x + r.left, y + r.top, x + r.right, y + r.bottom};

            bool isOpen   = (i == openMenuIndex);
            bool isHover  = (i == hoveredBtn);

            if (isOpen || isHover) {
                COLORREF fill = isOpen ? btnOpenColor : btnHoverColor;
                HBRUSH hb = CreateSolidBrush(fill);
                FillRect(ctx.hdc, &absR, hb);
                DeleteObject(hb);
            }

            SetTextColor(ctx.hdc, btnTextColor);
            DrawTextA(ctx.hdc, entries_[i].label.c_str(), -1, &absR,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }

        SelectObject(ctx.hdc, hOldFont);
        needsPaint = false;
    }

    // ── renderPopupContent ────────────────────────────────────────────────────
    // Draws the open drop-down list into the layered popup DC.
    void renderPopupContent(GraphicsContext &ctx, FontCache &fontCache) override {
        if (openMenuIndex < 0) return;
        const auto &items = entries_[openMenuIndex].items;
        if (items.empty()) return;

        int mW = popupW_;
        int mH = popupH_;

        // Shadow
        {
            HBRUSH sb = CreateSolidBrush(RGB(0, 0, 0));
            HRGN   sr = CreateRoundRectRgn(shadowOffset, shadowOffset,
                                           mW + shadowOffset, mH + shadowOffset,
                                           menuBorderRadius * 2, menuBorderRadius * 2);
            FillRgn(ctx.hdc, sr, sb);
            DeleteObject(sr); DeleteObject(sb);
        }

        // Background + border
        {
            HPEN   pen   = CreatePen(PS_SOLID, 1, menuBorderColor);
            HBRUSH brush = CreateSolidBrush(menuBgColor);
            HPEN   op    = (HPEN)  SelectObject(ctx.hdc, pen);
            HBRUSH ob    = (HBRUSH)SelectObject(ctx.hdc, brush);
            RoundRect(ctx.hdc, 0, 0, mW, mH,
                      menuBorderRadius * 2, menuBorderRadius * 2);
            SelectObject(ctx.hdc, ob); SelectObject(ctx.hdc, op);
            DeleteObject(brush);   DeleteObject(pen);
        }

        // Items
        HFONT hFont    = fontCache.getFont(menuFontSize, FontWeight::Normal);
        HFONT hOldFont = (HFONT)SelectObject(ctx.hdc, hFont);
        SetBkMode(ctx.hdc, TRANSPARENT);

        int curY = menuPadV;
        for (int i = 0; i < (int)items.size(); i++) {
            const auto &item = items[i];
            if (item.type == ContextMenuItem::Type::Separator) {
                int sy = curY + separatorHeight / 2;
                HPEN sp  = CreatePen(PS_SOLID, 1, separatorColor);
                HPEN osp = (HPEN)SelectObject(ctx.hdc, sp);
                MoveToEx(ctx.hdc, menuPadH,       sy, nullptr);
                LineTo  (ctx.hdc, mW - menuPadH,  sy);
                SelectObject(ctx.hdc, osp); DeleteObject(sp);
                curY += separatorHeight;
            } else {
                if (i == hoveredItem && item.enabled) {
                    HBRUSH hb = CreateSolidBrush(itemHoverColor);
                    RECT   ir = {2, curY, mW - 2, curY + itemHeight};
                    FillRect(ctx.hdc, &ir, hb); DeleteObject(hb);
                }
                SetTextColor(ctx.hdc, item.enabled ? itemTextColor : itemDisabledColor);
                RECT tr = {menuPadH, curY, mW - menuPadH, curY + itemHeight};
                DrawTextA(ctx.hdc, item.label.c_str(), -1, &tr,
                          DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
                curY += itemHeight;
            }
        }
        SelectObject(ctx.hdc, hOldFont);
    }

    // ── Mouse events ──────────────────────────────────────────────────────────

    bool handleMouseDown(int mx, int my) override {
        // Click on a bar button
        int btnIdx = hitTestBar_(mx, my);
        if (btnIdx >= 0) {
            if (openMenuIndex == btnIdx) {
                closeMenu_();           // toggle closed
            } else {
                if (openMenuIndex >= 0) closeMenu_();
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
                    if (item.action) item.action();
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
        if (openMenuIndex < 0) return false;
        const auto &items = entries_[openMenuIndex].items;

        switch (keyCode) {
        case VK_ESCAPE:
            closeMenu_();
            return true;

        case VK_LEFT:
            closeMenu_();
            openMenu_((openMenuIndex - 1 + (int)entries_.size()) % (int)entries_.size());
            return true;

        case VK_RIGHT:
            closeMenu_();
            openMenu_((openMenuIndex + 1) % (int)entries_.size());
            return true;

        case VK_UP: {
            int prev = (hoveredItem <= 0) ? (int)items.size() - 1 : hoveredItem - 1;
            while (prev >= 0 && items[prev].type == ContextMenuItem::Type::Separator)
                prev--;
            hoveredItem = (prev < 0) ? (int)items.size() - 1 : prev;
            refresh_();
            return true;
        }
        case VK_DOWN: {
            int next = hoveredItem + 1;
            while (next < (int)items.size() &&
                   items[next].type == ContextMenuItem::Type::Separator)
                next++;
            hoveredItem = (next >= (int)items.size()) ? 0 : next;
            refresh_();
            return true;
        }
        case VK_RETURN:
        case VK_SPACE:
            if (hoveredItem >= 0 && hoveredItem < (int)items.size()) {
                const auto &item = items[hoveredItem];
                if (item.type == ContextMenuItem::Type::Action && item.enabled) {
                    if (item.action) item.action();
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
        barHeight = h; markNeedsLayout(); return self_();
    }
    std::shared_ptr<MenuBarWidget> setBarBackground(COLORREF c) {
        barBgColor = c; markNeedsPaint(); return self_();
    }
    std::shared_ptr<MenuBarWidget> setItemHeight(int h) {
        itemHeight = h; return self_();
    }
    std::shared_ptr<MenuBarWidget> setMinMenuWidth(int w) {
        minMenuWidth = w; return self_();
    }
    std::shared_ptr<MenuBarWidget> setScaffoldPtr(ScaffoldWidget *s) {
        scaffold_ = s; return self_();
    }

private:
    std::vector<MenuBarItem> entries_;
    std::vector<RECT>        buttonRects_; // bar-local coords
    ScaffoldWidget          *scaffold_ = nullptr;
    int                      pendingSwitch_ = -1; // deferred hot-track switch

    // Popup geometry (screen coords)
    int popupScreenX_ = 0, popupScreenY_ = 0;
    int popupW_ = 0, popupH_ = 0;

    std::shared_ptr<MenuBarWidget> self_() {
        return std::static_pointer_cast<MenuBarWidget>(shared_from_this());
    }

    // ── Open / close ──────────────────────────────────────────────────────────

    void openMenu_(int idx) {
        if (idx < 0 || idx >= (int)entries_.size()) return;
        openMenuIndex = idx;
        hoveredItem   = -1;

        _computePopupGeometry(idx);

        HWND hw = getFluxTopLevel();
        if (hw) {
            FontCache &fc = FluxUI::getCurrentInstance()->getFontCache();
            showPopup(hw, popupScreenX_, popupScreenY_,
                      popupW_ + shadowOffset, popupH_ + shadowOffset, fc);
        }

        if (scaffold_)
            scaffold_->addOverlay(this,
                [this](HDC, FontCache &) {}, 150);

        markNeedsPaint();
    }

    void closeMenu_() {
        if (openMenuIndex < 0) return;
        openMenuIndex = -1;
        hoveredItem   = -1;
        hidePopup();
        if (scaffold_) scaffold_->removeOverlay(this);
        markNeedsPaint();
    }

    void refresh_() {
        if (!popupVisible()) return;
        if (auto *ui = FluxUI::getCurrentInstance())
            refreshPopup(ui->getFontCache());
    }

    // ── Geometry ──────────────────────────────────────────────────────────────

    void _computePopupGeometry(int idx) {
        const auto &items = entries_[idx].items;

        // Width: widest label + padding
        int maxLabelW = 0;
        for (const auto &item : items) {
            if (item.type == ContextMenuItem::Type::Action) {
                int lw = (int)item.label.size() * (menuFontSize / 2 + 1);
                maxLabelW = max(maxLabelW, lw);
            }
        }
        popupW_ = max(minMenuWidth, maxLabelW + menuPadH * 2);

        // Height: sum of item / separator slots
        int totalH = menuPadV * 2;
        for (const auto &item : items)
            totalH += (item.type == ContextMenuItem::Type::Separator)
                          ? separatorHeight : itemHeight;
        popupH_ = totalH;

        // Screen position: just below the button's bottom-left corner
        RECT &br = buttonRects_[idx];
        POINT sc = fluxClientToScreen(x + br.left, y + barHeight);
        popupScreenX_ = sc.x;
        popupScreenY_ = sc.y;

        // Clamp to monitor
        HMONITOR mon = MonitorFromPoint(sc, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi{}; mi.cbSize = sizeof(mi);
        if (GetMonitorInfoW(mon, &mi)) {
            if (popupScreenX_ + popupW_ > mi.rcWork.right)
                popupScreenX_ = mi.rcWork.right - popupW_;
            if (popupScreenY_ + popupH_ > mi.rcWork.bottom)
                popupScreenY_ = sc.y - barHeight - popupH_; // flip above
        }
    }

    // ── Hit testing ───────────────────────────────────────────────────────────

    // Returns button index if (mx,my) is inside the bar, else -1
    int hitTestBar_(int mx, int my) const {
        if (my < y || my >= y + barHeight) return -1;
        for (int i = 0; i < (int)buttonRects_.size(); i++) {
            const RECT &r = buttonRects_[i];
            if (mx >= x + r.left && mx < x + r.right) return i;
        }
        return -1;
    }

    // Returns item index inside the open popup, else -1
    int hitTestPopup_(int mx, int my) const {
        if (openMenuIndex < 0) return -1;

        HWND hw = getFluxTopLevel();
        POINT origin = {popupScreenX_, popupScreenY_};
        if (hw) ScreenToClient(hw, &origin);

        if (mx < origin.x || mx >= origin.x + popupW_ ||
            my < origin.y || my >= origin.y + popupH_)
            return -1;

        const auto &items = entries_[openMenuIndex].items;
        int relY  = my - origin.y - menuPadV;
        int curY  = 0;
        for (int i = 0; i < (int)items.size(); i++) {
            int h = (items[i].type == ContextMenuItem::Type::Separator)
                        ? separatorHeight : itemHeight;
            if (relY >= curY && relY < curY + h) {
                if (items[i].type == ContextMenuItem::Type::Separator) return -1;
                return i;
            }
            curY += h;
        }
        return -1;
    }

    // ── Text measurement ──────────────────────────────────────────────────────

    int _measureLabel(GraphicsContext &ctx, FontCache &fc, const std::string &label) const {
        HFONT hFont    = fc.getFont(menuFontSize, FontWeight::Normal);
        HFONT hOldFont = (HFONT)SelectObject(ctx.hdc, hFont);
        SIZE  sz       = {};
        GetTextExtentPoint32A(ctx.hdc, label.c_str(), (int)label.size(), &sz);
        SelectObject(ctx.hdc, hOldFont);
        return sz.cx;
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