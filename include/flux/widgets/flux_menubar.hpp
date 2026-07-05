// #ifndef FLUX_MENU_BAR_HPP
// #define FLUX_MENU_BAR_HPP

// #include "../flux_core.hpp"
// #include "flux_overlays.hpp"

// // ============================================================================
// // MENU BAR WIDGET
// // ============================================================================
// //
// // A horizontal strip of labeled menu buttons. Clicking a button opens a
// // pulldown list below it — identical to ContextMenuWidget but left-click
// // activated, like the File / Edit / View bar in a Windows app.
// //
// // Usage:
// //
// //   auto menuBar = MenuBar({
// //       MenuBarItem("File", {
// //           ContextMenuItem::Action("New",  []{...}),
// //           ContextMenuItem::Action("Open", []{...}),
// //           ContextMenuItem::Separator(),
// //           ContextMenuItem::Action("Exit", []{...}),
// //       }),
// //       MenuBarItem("Edit", {
// //           ContextMenuItem::Action("Cut",   []{...}),
// //           ContextMenuItem::Action("Copy",  []{...}),
// //           ContextMenuItem::Action("Paste", []{...}),
// //       }),
// //   });
// //
// //   // Widget items are supported too:
// //   MenuBarItem("File", {
// //       ContextMenuItem::Widget(
// //           FilePicker("Open file…")
// //               ->setMode(FilePickerMode::Open)
// //               ->addFilter("All files", {"*.*"})
// //               ->setOnChanged([](const std::string &p){ ... })
// //       ),
// //       ContextMenuItem::Separator(),
// //       ContextMenuItem::Action("Exit", []{...}),
// //   })
// //
// //   // Just add it like any other widget — no Scaffold wiring needed, the
// //   // OverlayManager (owned by FluxUI) handles popup mechanics:
// //   Scaffold(menuBar, body)
// // ============================================================================

// // ── One top-level menu entry (label + its drop-down items) ──────────────────
// struct MenuBarItem
// {
//   std::string label;
//   std::vector<ContextMenuItem> items;

//   MenuBarItem(const std::string &lbl, std::vector<ContextMenuItem> its)
//       : label(lbl), items(std::move(its)) {}
// };

// // ============================================================================
// // MENU BAR WIDGET
// // ============================================================================
// // Shares the exact rendering approach as ContextMenuWidget — same shadow,
// // same rounded rect, same item/separator geometry. Only the open trigger
// // differs (left-click on the bar button vs right-click on an anchor), and
// // there's a second open trigger besides: hot-tracking between bar buttons
// // while a menu is already open.
// //
// // renderOverlay()/onOverlay*() all operate in coordinates LOCAL to the
// // popup's own rect — (0,0) is the popup's top-left corner. OverlayManager
// // handles screen-space conversion, monitor clamping, and native popup
// // creation; this widget never touches any of that.

// class MenuBarWidget : public Widget
// {
// public:
//   // ── Popup-body surface ─────────────────────────────────────────────────
//   // The open pulldown list for whichever bar button is active. Same
//   // pattern as ContextMenuWidget::MenuSurface — item.widget positioning
//   // is now genuinely absolute (x(surface) + menuPadH), not "local
//   // coordinates that happened to line up with the screen because
//   // renderOverlay ran inside a translated transform."
//   class MenuSurface : public Widget
//   {
//   public:
//     MenuBarWidget *owner = nullptr;

//     void render(GraphicsContext &ctx, FontCache &fontCache) override
//     {
//       if (!owner || owner->openMenuIndex < 0)
//         return;
//       const auto &items = owner->entries_[owner->openMenuIndex].items;
//       if (items.empty())
//         return;

//       int mW = owner->popupW_, mH = owner->popupH_;
//       Painter painter(ctx);

//       painter.fillRoundedRect(x + owner->shadowOffset, y + owner->shadowOffset,
//                               mW, mH, owner->menuBorderRadius,
//                               Color::fromRGBA(0, 0, 0, 60));
//       painter.fillRoundedRect(x, y, mW, mH, owner->menuBorderRadius,
//                               owner->menuBgColor);
//       painter.drawBorder(x, y, mW, mH, owner->menuBorderRadius,
//                          owner->menuBorderColor, 1);

//       NativeFont hFont = fontCache.getFont(owner->menuFontSize, FontWeight::Normal);
//       int curY = y + owner->menuPadV;

//       for (int i = 0; i < (int)items.size(); i++)
//       {
//         const auto &item = items[i];

//         if (item.type == ContextMenuItem::Type::Separator)
//         {
//           int sy = curY + owner->separatorHeight / 2;
//           painter.drawLine(x + owner->menuPadH, sy, x + mW - owner->menuPadH, sy,
//                            owner->separatorColor, 1);
//           curY += owner->separatorHeight;
//         }
//         else if (item.type == ContextMenuItem::Type::Widget && item.widget)
//         {
//           int rowH = owner->_widgetItemHeight(item);

//           if (i == owner->hoveredItem)
//             painter.fillRect(x + 2, curY, mW - 4, rowH, owner->itemHoverColor);

//           auto *ui = FluxUI::getCurrentInstance();
//           if (ui)
//           {
//             if (item.widget->needsLayout)
//             {
//               item.widget->computeLayout(
//                   ctx,
//                   BoxConstraints::tight(mW - owner->menuPadH * 2, rowH),
//                   fontCache);
//             }
//             item.widget->x = x + owner->menuPadH;
//             item.widget->y = curY;
//             item.widget->positionChildren(
//                 item.widget->x + item.widget->paddingLeft,
//                 item.widget->y + item.widget->paddingTop,
//                 item.widget->width - item.widget->paddingLeft - item.widget->paddingRight,
//                 item.widget->height - item.widget->paddingTop - item.widget->paddingBottom);
//             item.widget->render(ctx, fontCache);
//           }

//           curY += rowH;
//         }
//         else
//         {
//           if (i == owner->hoveredItem && item.enabled)
//             painter.fillRect(x + 2, curY, mW - 4, owner->itemHeight,
//                              owner->itemHoverColor);

//           std::wstring wlabel = toWideString(item.label);
//           painter.drawText(
//               wlabel, x + owner->menuPadH, curY, mW - owner->menuPadH * 2,
//               owner->itemHeight, hFont,
//               item.enabled ? owner->itemTextColor : owner->itemDisabledColor,
//               DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
//           curY += owner->itemHeight;
//         }
//       }
//       needsPaint = false;
//     }

//     bool handleMouseDown(int mx, int my) override
//     {
//       if (!owner || owner->openMenuIndex < 0)
//         return false;
//       int localX = mx - x, localY = my - y;

//       int itemIdx = owner->hitTestPopupLocal_(localX, localY);
//       if (itemIdx >= 0)
//       {
//         const auto &item = owner->entries_[owner->openMenuIndex].items[itemIdx];

//         if (item.type == ContextMenuItem::Type::Widget && item.widget)
//         {
//           // NOTE: preserved as-is — closes the menu before the embedded
//           // widget sees the click. Same known caveat carried over from
//           // ContextMenuWidget's migration; fix together if you address it.
//           owner->closeMenu_();
//           item.widget->handleMouseDown(mx, my);
//           return true;
//         }
//         if (item.type == ContextMenuItem::Type::Action && item.enabled)
//         {
//           if (item.action)
//             item.action();
//           owner->closeMenu_();
//           return true;
//         }
//         return true; // absorb click on disabled action
//       }

//       owner->closeMenu_(); // click outside the popup -> close
//       return true;
//     }

//     bool handleMouseMove(int mx, int my) override
//     {
//       if (!owner || owner->openMenuIndex < 0)
//         return false;
//       int localX = mx - x, localY = my - y;

//       int itemIdx = owner->hitTestPopupLocal_(localX, localY);

//       if (itemIdx >= 0 && itemIdx < (int)owner->entries_[owner->openMenuIndex].items.size())
//       {
//         const auto &item = owner->entries_[owner->openMenuIndex].items[itemIdx];
//         if (item.type == ContextMenuItem::Type::Widget && item.widget)
//           item.widget->handleMouseMove(mx, my);
//       }

//       if (itemIdx != owner->hoveredItem)
//       {
//         owner->hoveredItem = itemIdx;
//         owner->refresh_();
//         return true;
//       }
//       return false;
//     }

//     void onOverlayOutsideClick() override
//     {
//       if (owner)
//         owner->closeMenu_();
//     }

//     bool handleKeyDown(int keyCode) override
//     {
//       if (!owner || owner->openMenuIndex < 0)
//         return false;
//       const auto &items = owner->entries_[owner->openMenuIndex].items;

//       switch (keyCode)
//       {
//       case Key::Escape:
//         owner->closeMenu_();
//         return true;

//       case Key::Left:
//         owner->closeMenu_();
//         owner->openMenu_((owner->openMenuIndex - 1 + (int)owner->entries_.size()) %
//                          (int)owner->entries_.size());
//         return true;

//       case Key::Right:
//         owner->closeMenu_();
//         owner->openMenu_((owner->openMenuIndex + 1) % (int)owner->entries_.size());
//         return true;

//       case Key::Up:
//       {
//         int prev = (owner->hoveredItem <= 0) ? (int)items.size() - 1 : owner->hoveredItem - 1;
//         while (prev >= 0 && items[prev].type == ContextMenuItem::Type::Separator)
//           prev--;
//         owner->hoveredItem = (prev < 0) ? (int)items.size() - 1 : prev;
//         owner->refresh_();
//         return true;
//       }
//       case Key::Down:
//       {
//         int next = owner->hoveredItem + 1;
//         while (next < (int)items.size() &&
//                items[next].type == ContextMenuItem::Type::Separator)
//           next++;
//         owner->hoveredItem = (next >= (int)items.size()) ? 0 : next;
//         owner->refresh_();
//         return true;
//       }
//       case Key::Return:
//       case Key::Space:
//         if (owner->hoveredItem >= 0 && owner->hoveredItem < (int)items.size())
//         {
//           const auto &item = items[owner->hoveredItem];

//           if (item.type == ContextMenuItem::Type::Widget && item.widget)
//           {
//             int cx = item.widget->x + item.widget->width / 2;
//             int cy = item.widget->y + item.widget->height / 2;
//             item.widget->handleMouseDown(cx, cy);
//             return true;
//           }
//           if (item.type == ContextMenuItem::Type::Action && item.enabled)
//           {
//             if (item.action)
//               item.action();
//             owner->closeMenu_();
//             return true;
//           }
//         }
//         return true;
//       }
//       return false;
//     }
//   };

//   std::shared_ptr<MenuSurface> menuSurface_;

//   // ── Appearance ───────────────────────────────────────────────────────────
//   int barHeight = 28;
//   int buttonPadH = 12; // horizontal padding inside each button
//   Color barBgColor = Color::fromRGBA(0, 0, 0, 0);
//   Color barBorderColor = Color::fromRGBA(0, 0, 0, 0);
//   Color btnHoverColor = Color::fromRGB(225, 235, 245);
//   Color btnOpenColor = Color::fromRGB(210, 228, 248);
//   Color btnTextColor = Color::fromRGB(0, 0, 0);

//   // Drop-down list appearance (mirrors ContextMenuWidget)
//   int itemHeight = 28;
//   int separatorHeight = 9;
//   int minMenuWidth = 160;
//   int menuPadH = 12;
//   int menuPadV = 4;
//   int menuBorderRadius = 6;
//   int menuFontSize = 13;
//   int shadowOffset = 3;

//   Color menuBgColor = Color::fromRGB(255, 255, 255);
//   Color menuBorderColor = Color::fromRGB(180, 180, 180);
//   Color itemHoverColor = Color::fromRGB(240, 245, 250);
//   Color itemTextColor = Color::fromRGB(30, 30, 30);
//   Color itemDisabledColor = Color::fromRGB(160, 160, 160);
//   Color separatorColor = Color::fromRGB(220, 220, 220);

//   // ── State ─────────────────────────────────────────────────────────────────
//   int openMenuIndex = -1; // which top-level entry is open (-1 = none)
//   int hoveredBtn = -1;    // which button the mouse is over
//   int hoveredItem = -1;   // which drop-down item is hovered (popup-local)

//   explicit MenuBarWidget(std::vector<MenuBarItem> entries)
//       : entries_(std::move(entries))
//   {
//     menuSurface_ = std::make_shared<MenuSurface>();
//     menuSurface_->owner = this;
//   }

//   void onDetach() override
//   {
//     if (openMenuIndex >= 0)
//       closeMenu_();
//     Widget::onDetach();
//   }

//   // ── Layout ────────────────────────────────────────────────────────────────
//   void computeLayout(GraphicsContext &ctx, const BoxConstraints & /*constraints*/,
//                      FontCache &fontCache) override
//   {
//     // Measure button widths first
//     buttonRects_.resize(entries_.size());
//     int curX = 0;
//     for (int i = 0; i < (int)entries_.size(); i++)
//     {
//       int textW = _measureLabel(ctx, fontCache, entries_[i].label);
//       int btnW = textW + buttonPadH * 2;
//       buttonRects_[i] = {curX, 0, curX + btnW, barHeight};
//       curX += btnW;
//     }

//     // Fit to content width instead of expanding to max
//     if (autoWidth)
//       width = curX;
//     height = barHeight;
//     autoHeight = false;

//     applyConstraints();
//     needsLayout = false;
//   }

//   void positionChildren(int, int, int, int) override {}

//   // ── Render the bar ────────────────────────────────────────────────────────
//   void render(GraphicsContext &ctx, FontCache &fontCache) override
//   {
//     if (!visible)
//       return;
//     Painter painter(ctx);

//     if (pendingSwitch_ >= 0)
//     {
//       int target = pendingSwitch_;
//       pendingSwitch_ = -1;
//       closeMenu_();
//       openMenu_(target);
//     }

//     // Bar background + bottom border
//     if (barBgColor.a > 0)
//       painter.fillRect(x, y, width, barHeight, barBgColor);
//     painter.drawHLine(x, y + barHeight - 1, width, barBorderColor, 1);

//     // Buttons
//     NativeFont hFont = fontCache.getFont(menuFontSize, FontWeight::Normal);

//     for (int i = 0; i < (int)entries_.size(); i++)
//     {
//       auto &r = buttonRects_[i];
//       int ax = x + r.left, ay = y + r.top;
//       int aw = r.right - r.left, ah = r.bottom - r.top;
//       bool isOpen = (i == openMenuIndex);
//       bool isHover = (i == hoveredBtn);

//       if (isOpen || isHover)
//         painter.fillRect(ax, ay, aw, ah, isOpen ? btnOpenColor : btnHoverColor);

//       painter.drawTextA(entries_[i].label, ax, ay, aw, ah, hFont, btnTextColor,
//                         DT_CENTER | DT_VCENTER | DT_SINGLELINE);
//     }

//     needsPaint = false;
//   }

//   // ── Bar mouse events (normal widget-tree dispatch) ───────────────────────
//   // These only ever see bar-button hits now. Hit-testing the open popup is
//   // handled by MenuSurface::handleMouseDown/handleMouseMove above — the
//   // overlay layer dispatches directly to the popup's handlers while it's
//   // open and never reaches here for popup-area coordinates.

//   bool handleMouseDown(int mx, int my) override
//   {
//     int btnIdx = hitTestBar_(mx, my);
//     if (btnIdx >= 0)
//     {
//       if (openMenuIndex == btnIdx)
//       {
//         closeMenu_(); // toggle closed
//       }
//       else
//       {
//         if (openMenuIndex >= 0)
//           closeMenu_();
//         openMenu_(btnIdx);
//       }
//       return true;
//     }
//     return false;
//   }

//   bool handleMouseMove(int mx, int my) override
//   {
//     // Hover over bar buttons. This keeps running even while a menu is
//     // open because blocksHoverBelow is false (see overlayPolicy()) — the
//     // normal tree walk in FluxUI::wireCallbacks' onMouseMove still
//     // reaches this widget.
//     int btnIdx = hitTestBar_(mx, my);
//     if (btnIdx != hoveredBtn)
//     {
//       hoveredBtn = btnIdx;
//       if (auto *ui = FluxUI::getCurrentInstance())
//         ui->updateWidget(this); // force immediate redraw so highlight moves
//     }

//     // Hot-tracking: if a menu is already open and cursor moved to a
//     // different button, schedule the switch AFTER this event returns so
//     // we never close/reopen overlays (and mutate OverlayManager's entry
//     // list) while something is still iterating it.
//     if (openMenuIndex >= 0 && btnIdx >= 0 && btnIdx != openMenuIndex)
//     {
//       pendingSwitch_ = btnIdx;
//       if (auto *ui = FluxUI::getCurrentInstance())
//         ui->updateWidget(this);
//     }

//     return false;
//   }

//   bool handleMouseLeave() override
//   {
//     hoveredBtn = -1;
//     markNeedsPaint();
//     return false;
//   }

//   // Keyboard while open is handled by MenuSurface::handleKeyDown (above)
//   // via FluxUI::dispatchOverlayKeyDown, since the entry is registered with
//   // capturesKeyboard=true. No handleKeyDown override needed on the bar
//   // itself.

//   // ── Fluent setters ────────────────────────────────────────────────────────

//   std::shared_ptr<MenuBarWidget> setBarHeight(int h)
//   {
//     barHeight = h;
//     markNeedsLayout();
//     return self_();
//   }
//   std::shared_ptr<MenuBarWidget> setBarBackground(Color c)
//   {
//     barBgColor = c;
//     markNeedsPaint();
//     return self_();
//   }
//   std::shared_ptr<MenuBarWidget> setItemHeight(int h)
//   {
//     itemHeight = h;
//     return self_();
//   }
//   std::shared_ptr<MenuBarWidget> setMinMenuWidth(int w)
//   {
//     minMenuWidth = w;
//     return self_();
//   }
//   std::shared_ptr<MenuBarWidget> setBtnTextColor(Color c)
//   {
//     btnTextColor = c;
//     markNeedsPaint();
//     return self_();
//   }

// private:
//   friend class MenuSurface;
//   std::vector<MenuBarItem> entries_;
//   struct BtnRect
//   {
//     int left, top, right, bottom;
//   };
//   std::vector<BtnRect> buttonRects_;
//   int pendingSwitch_ = -1; // deferred hot-track switch

//   // Popup geometry — CLIENT coordinates now (not screen). OverlayManager
//   // does its own screen-space conversion and monitor clamping internally.
//   int popupClientX_ = 0, popupClientY_ = 0;
//   int popupW_ = 0, popupH_ = 0;

//   std::shared_ptr<MenuBarWidget> self_()
//   {
//     return std::static_pointer_cast<MenuBarWidget>(shared_from_this());
//   }

//   // ── Height helpers ────────────────────────────────────────────────────────

//   int _itemHeight(const ContextMenuItem &item) const
//   {
//     if (item.type == ContextMenuItem::Type::Separator)
//       return separatorHeight;
//     if (item.type == ContextMenuItem::Type::Widget && item.widget)
//       return _widgetItemHeight(item);
//     return itemHeight;
//   }

//   int _widgetItemHeight(const ContextMenuItem &item) const
//   {
//     if (!item.widget)
//       return itemHeight;
//     int h = item.widget->height > 0 ? item.widget->height : item.widget->minHeight;
//     return h > 0 ? h : itemHeight;
//   }

//   // ── Open / close ──────────────────────────────────────────────────────────

//   void openMenu_(int idx)
//   {
//     if (idx < 0 || idx >= (int)entries_.size())
//       return;

//     auto *ui = FluxUI::getCurrentInstance();
//     if (!ui)
//       return;

//     openMenuIndex = idx;
//     hoveredItem = -1;

//     // Pre-layout all widget items so their heights are known before geometry.
//     _layoutWidgetItems(idx, ui);

//     _computePopupGeometry(idx); // size + client-space position only

//     menuSurface_->x = popupClientX_;
//     menuSurface_->y = popupClientY_;
//     menuSurface_->width = popupW_ + shadowOffset;
//     menuSurface_->height = popupH_ + shadowOffset;
//     // modal=true: every click while open is consumed (including outside
//     // clicks, which close it). blocksHoverBelow stays false — it must NOT
//     // block hover reaching this same widget's bar buttons, or
//     // hot-tracking between File/Edit/View would stop working while a
//     // menu is open.
//     ui->showOverlay(menuSurface_.get(), /*zIndex=*/150,
//                     /*modal=*/true, /*blocksHoverBelow=*/false,
//                     /*capturesKeyboard=*/true);

//     markNeedsPaint();
//   }

//   void closeMenu_()
//   {
//     if (openMenuIndex < 0)
//       return;
//     openMenuIndex = -1;
//     hoveredItem = -1;
//     if (auto *ui = FluxUI::getCurrentInstance())
//       ui->hideOverlay(menuSurface_.get());
//     markNeedsPaint();
//   }

//   void refresh_()
//   {
//     if (openMenuIndex < 0)
//       return;
//     if (auto *ui = FluxUI::getCurrentInstance())
//       ui->refreshOverlay(menuSurface_.get());
//   }

//   // ── Widget pre-layout ─────────────────────────────────────────────────────

//   void _layoutWidgetItems(int idx, FluxUI *ui)
//   {
//     auto mc = ui->getMeasureContext();
//     FontCache &fc = ui->getFontCache();

//     for (auto &item : entries_[idx].items)
//     {
//       if (item.type == ContextMenuItem::Type::Widget && item.widget)
//       {
//         if (item.widget->needsLayout || item.widget->height == 0)
//         {
//           item.widget->computeLayout(
//               mc.ctx,
//               BoxConstraints::loose(minMenuWidth - menuPadH * 2, kUnbounded),
//               fc);
//         }
//       }
//     }
//   }

//   // ── Geometry ──────────────────────────────────────────────────────────────
//   // Pure size + client-space position now. No screen coordinates, no
//   // monitor clamping — OverlayManager::show() handles all of that.
//   void _computePopupGeometry(int idx)
//   {
//     const auto &items = entries_[idx].items;

//     // Determine required width: longest action label OR widest widget.
//     int maxLabelW = 0;
//     for (const auto &item : items)
//     {
//       if (item.type == ContextMenuItem::Type::Action)
//       {
//         int lw = (int)item.label.size() * (menuFontSize / 2 + 1);
//         maxLabelW = std::max(maxLabelW, lw);
//       }
//       else if (item.type == ContextMenuItem::Type::Widget && item.widget)
//       {
//         int lw = item.widget->width > 0 ? item.widget->width : item.widget->minWidth;
//         maxLabelW = std::max(maxLabelW, lw);
//       }
//     }
//     popupW_ = std::max(minMenuWidth, maxLabelW + menuPadH * 2);

//     // Determine required height: sum of all item heights.
//     int totalH = menuPadV * 2;
//     for (const auto &item : items)
//       totalH += _itemHeight(item);
//     popupH_ = totalH;

//     // Position below the button, in this widget's own (client-space)
//     // coordinate system — x/y here are already client coordinates since
//     // every widget lays itself out in client space.
//     auto &br = buttonRects_[idx];
//     popupClientX_ = x + br.left;
//     popupClientY_ = y + barHeight;
//   }

//   // ── Hit testing ───────────────────────────────────────────────────────────

//   // Returns button index if (mx,my) is inside the bar, else -1.
//   // mx/my here are normal client coordinates (bar dispatch only).
//   int hitTestBar_(int mx, int my) const
//   {
//     if (my < y || my >= y + barHeight)
//       return -1;
//     for (int i = 0; i < (int)buttonRects_.size(); i++)
//     {
//       const BtnRect &r = buttonRects_[i];
//       if (mx >= x + r.left && mx < x + r.right)
//         return i;
//     }
//     return -1;
//   }

//   // Returns item index inside the open popup, else -1. localX/localY are
//   // already popup-local (0,0 = popup top-left) — no client/screen
//   // conversion needed, unlike the old version.
//   int hitTestPopupLocal_(int localX, int localY) const
//   {
//     if (openMenuIndex < 0)
//       return -1;
//     if (localX < 0 || localX >= popupW_ || localY < 0 || localY >= popupH_)
//       return -1;

//     const auto &items = entries_[openMenuIndex].items;
//     int relY = localY - menuPadV;
//     int curY = 0;

//     for (int i = 0; i < (int)items.size(); i++)
//     {
//       int h = _itemHeight(items[i]);
//       if (relY >= curY && relY < curY + h)
//       {
//         if (items[i].type == ContextMenuItem::Type::Separator)
//           return -1;
//         return i;
//       }
//       curY += h;
//     }
//     return -1;
//   }

//   // ── Text measurement ──────────────────────────────────────────────────────

//   int _measureLabel(GraphicsContext &ctx, FontCache &fc,
//                     const std::string &label) const
//   {
//     NativeFont hFont = fc.getFont(menuFontSize, FontWeight::Normal);
//     std::wstring wlabel = toWideString(label);
//     int w = 0, h = 0;
//     Painter(ctx, this).measureText(wlabel, hFont, w, h);
//     return w;
//   }
// };

// // ============================================================================
// // FACTORY
// // ============================================================================

// using MenuBarWidgetPtr = std::shared_ptr<MenuBarWidget>;

// inline MenuBarWidgetPtr MenuBar(std::vector<MenuBarItem> items)
// {
//   return std::make_shared<MenuBarWidget>(std::move(items));
// }

// #endif // FLUX_MENU_BAR_HPP