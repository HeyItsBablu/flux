#ifndef FLUX_DROPDOWN_HPP
#define FLUX_DROPDOWN_HPP

#include "flux_structure.hpp"

#include "flux/flux_app.hpp"
#include "flux/flux_core.hpp"
#include <algorithm>

#if (defined(__EMSCRIPTEN__) && defined(FLUX_WEB_RENDERER_DOM)) || defined(FLUX_SSR)
#include "flux/flux_dom_adapter.hpp"
// Declared in flux_painter_dom.cpp — normally invoked automatically from
// Widget::onDetach() when a widget leaves the TREE. ListSurface never
// does that (it's a long-lived overlay widget registered once via
// showOverlay/hideOverlay, never addChild'd/removed from any parent),
// so closeDropdown() below calls this directly instead.
extern void fluxDomEvictWidget(Widget *owner);
#endif

// ============================================================================
// DROPDOWN WIDGET
// ============================================================================

class DropdownWidget : public Widget
{
private:
  int listWidth_ = 0;

  // ── Popup-body surface ─────────────────────────────────────────────────
  // The floating list is now an ordinary Widget with real absolute
  // x/y/width/height, registered with FluxUI's overlay layer. It replaces
  // the old OverlayContent::renderOverlay()/onOverlay* split — this class
  // just forwards render()/handle*() to the owning DropdownWidget's state,
  // since C++11 nested classes have access to the enclosing class's
  // private members through an instance pointer.
  class ListSurface : public Widget
  {
  public:
    DropdownWidget *owner = nullptr;

    void render(GraphicsContext &ctx, FontCache &fontCache) override
    {

#if (defined(__EMSCRIPTEN__) && defined(FLUX_WEB_RENDERER_DOM)) || defined(FLUX_SSR)
      if (IDomAdapter *adapter = getActiveDomAdapter())
      {
        _renderDom(adapter);
        needsPaint = false;
        return;
      }
#endif
      if (!owner || owner->options.empty())
        return;
      Painter painter(ctx, this);

      int visibleCount = std::min((int)owner->options.size(), owner->maxVisibleItems);
      int listH = visibleCount * owner->itemHeight + 2;

      painter.fillRect(x, y, owner->listWidth_, listH, owner->listBgColor);
      painter.drawRectOutline(x, y, owner->listWidth_, listH, owner->listBorderColor, 1);
      painter.pushClipRect(x + 1, y + 1, owner->listWidth_ - 2, listH - 2);

      NativeFont font = fontCache.getFont(owner->fontSize, owner->fontWeight);
      int endIndex = std::min((int)owner->options.size(),
                              owner->scrollOffset + visibleCount);
      for (int i = owner->scrollOffset; i < endIndex; i++)
      {
        int itemY = y + 1 + (i - owner->scrollOffset) * owner->itemHeight;
        if (i == owner->hoveredItemIndex)
          painter.fillRect(x + 1, itemY, owner->listWidth_ - 2, owner->itemHeight,
                           owner->itemHoverColor);
        else if (i == owner->selectedIndex)
          painter.fillRect(x + 1, itemY, owner->listWidth_ - 2, owner->itemHeight,
                           owner->itemSelectedColor);

        std::wstring wopt = toWideString(owner->options[i]);
        painter.drawText(wopt, x + 12, itemY, owner->listWidth_ - 24, owner->itemHeight,
                         font, Color::fromRGB(30, 30, 30),
                         DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
      }
      painter.popClipRect();
      needsPaint = false;
    }

    bool handleMouseDown(int mx, int my) override
    {
      if (!owner)
        return false;
      int visibleCount = std::min((int)owner->options.size(), owner->maxVisibleItems);
      int listH = visibleCount * owner->itemHeight + 2;
      int localX = mx - x, localY = my - y;
      if (localX < 0 || localX >= owner->listWidth_ || localY < 0 || localY >= listH)
      {
        owner->closeDropdown();
        return true;
      }
      int itemIndex = owner->scrollOffset + ((localY - 1) / owner->itemHeight);
      if (itemIndex >= 0 && itemIndex < (int)owner->options.size())
        owner->selectItem(itemIndex);
      owner->closeDropdown();
      return true;
    }

    bool handleMouseMove(int mx, int my) override
    {
      if (!owner)
        return false;
      int visibleCount = std::min((int)owner->options.size(), owner->maxVisibleItems);
      int listH = visibleCount * owner->itemHeight + 2;
      int localX = mx - x, localY = my - y;
      if (localX >= 0 && localX < owner->listWidth_ && localY >= 0 && localY < listH)
      {
        int itemIndex = owner->scrollOffset + ((localY - 1) / owner->itemHeight);
        if (itemIndex != owner->hoveredItemIndex)
        {
          owner->hoveredItemIndex = itemIndex;
          owner->refreshDropdownPopup_();
          return true;
        }
      }
      else if (owner->hoveredItemIndex != -1)
      {
        owner->hoveredItemIndex = -1;
        owner->refreshDropdownPopup_();
        return true;
      }
      return false;
    }

    bool handleMouseWheel(int delta) override
    {
      if (!owner)
        return false;
      int maxScroll = std::max(0, (int)owner->options.size() - owner->maxVisibleItems);
      owner->scrollOffset = (delta > 0) ? std::max(0, owner->scrollOffset - 1)
                                        : std::min(maxScroll, owner->scrollOffset + 1);
      owner->refreshDropdownPopup_();
      return true;
    }

    bool handleKeyDown(int keyCode) override
    {
      if (!owner || owner->options.empty())
        return false;
      switch (keyCode)
      {
      case Key::Return:
      case Key::Space:
      {
        int idx = (owner->hoveredItemIndex >= 0) ? owner->hoveredItemIndex
                                                 : owner->selectedIndex;
        if (idx >= 0 && idx < (int)owner->options.size())
          owner->selectItem(idx);
        owner->closeDropdown();
        return true;
      }
      case Key::Escape:
        owner->closeDropdown();
        return true;
      case Key::Up:
        if (owner->hoveredItemIndex < 0)
          owner->hoveredItemIndex = std::max(0, owner->selectedIndex);
        else if (owner->hoveredItemIndex > 0)
          owner->hoveredItemIndex--;
        owner->ensureItemVisible(owner->hoveredItemIndex);
        owner->refreshDropdownPopup_();
        return true;
      case Key::Down:
        if (owner->hoveredItemIndex < 0)
          owner->hoveredItemIndex = std::max(0, owner->selectedIndex);
        else if (owner->hoveredItemIndex < (int)owner->options.size() - 1)
          owner->hoveredItemIndex++;
        owner->ensureItemVisible(owner->hoveredItemIndex);
        owner->refreshDropdownPopup_();
        return true;
      case Key::Home:
        owner->hoveredItemIndex = 0;
        owner->scrollOffset = 0;
        owner->refreshDropdownPopup_();
        return true;
      case Key::End:
        owner->hoveredItemIndex = (int)owner->options.size() - 1;
        owner->scrollOffset = std::max(0, (int)owner->options.size() - owner->maxVisibleItems);
        owner->refreshDropdownPopup_();
        return true;
      }
      return false;
    }

    void onOverlayOutsideClick() override
    {
      if (owner)
        owner->closeDropdown();
    }

  private:
#if (defined(__EMSCRIPTEN__) && defined(FLUX_WEB_RENDERER_DOM)) || defined(FLUX_SSR)
    // Pre-allocates owner->maxVisibleItems item slots UNCONDITIONALLY —
    // the actual visible count varies frame to frame (fewer options,
    // scroll position, etc.), but the CEILING (maxVisibleItems) is a
    // fixed config value, not something that grows without bound. Hiding
    // unused slots via display:none (rather than creating/evicting nodes
    // dynamically) avoids needing any per-item node lifecycle tracking —
    // same tradeoff RadioButtonWidget's inner dot already makes for its
    // one binary shown/hidden state, just extended to N slots here.
    void _renderDom(IDomAdapter *adapter)
    {
      if (!owner)
        return;
      char buf[24];
      auto px = [&](int v)
      { snprintf(buf, sizeof(buf), "%dpx", v); return std::string(buf); };
      char colbuf[32];
      auto rgb = [&](Color c)
      { snprintf(colbuf, sizeof(colbuf), "rgb(%d,%d,%d)", c.r, c.g, c.b); return std::string(colbuf); };

      int visibleCount = std::min((int)owner->options.size(), owner->maxVisibleItems);
      int listH = visibleCount * owner->itemHeight + 2;

      // ── Container background + border — one node ───────────────────
      DomNodeHandle bg = fluxDomEnsureNode(this, "div", "bg");
      fluxDomApplyRect(this, x, y, owner->listWidth_, listH, "bg");
      adapter->setStyle(bg, "background-color", rgb(owner->listBgColor));
      adapter->setStyle(bg, "border", "1px solid " + rgb(owner->listBorderColor));
      adapter->setStyle(bg, "box-sizing", "border-box");
      adapter->setStyle(bg, "overflow", "hidden");
      adapter->setStyle(bg, "pointer-events", "none");
      // Sits after the main tree in DOM order (no Widget::parent, see
      // ensureNode's setRoot() branch) — no explicit z-index needed for
      // correct stacking, but set a generous one anyway as a safety
      // margin against any ancestor establishing an unexpected
      // stacking context (e.g. a TextInput's z-index:2 elsewhere).
      adapter->setStyle(bg, "z-index", "10");

      int endIndex = std::min((int)owner->options.size(),
                              owner->scrollOffset + visibleCount);

      for (int slot = 0; slot < owner->maxVisibleItems; ++slot)
      {
        std::string slotName = "item" + std::to_string(slot);
        DomNodeHandle item = fluxDomEnsureNode(this, "div", slotName.c_str());

        int i = owner->scrollOffset + slot; // absolute option index for this row
        bool rowActive = (owner->scrollOffset + slot) < endIndex;

        if (!rowActive)
        {
          adapter->setStyle(item, "display", "none");
          continue;
        }

        int itemY = y + 1 + slot * owner->itemHeight;
        fluxDomApplyRect(this, x + 1, itemY, owner->listWidth_ - 2,
                         owner->itemHeight, slotName.c_str());

        Color bgColor = (i == owner->hoveredItemIndex) ? owner->itemHoverColor
                        : (i == owner->selectedIndex)  ? owner->itemSelectedColor
                                                       : Color::fromRGB(255, 255, 255);
        adapter->setStyle(item, "display", "flex");
        adapter->setStyle(item, "align-items", "center");
        adapter->setStyle(item, "background-color", rgb(bgColor));
        adapter->setStyle(item, "padding-left", px(11)); // matches canvas's x+12 minus the 1px item inset above
        adapter->setStyle(item, "box-sizing", "border-box");
        adapter->setStyle(item, "white-space", "nowrap");
        adapter->setStyle(item, "overflow", "hidden");
        adapter->setStyle(item, "text-overflow", "ellipsis");
        adapter->setStyle(item, "font-size", px(owner->fontSize));
        adapter->setStyle(item, "color", "rgb(30,30,30)");
        adapter->setStyle(item, "pointer-events", "none");
        adapter->setStyle(item, "z-index", "11");
        adapter->setText(item, owner->options[i]);
      }
    }
#endif
  };

  std::shared_ptr<ListSurface> listSurface_;

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
    listSurface_ = std::make_shared<ListSurface>();
    listSurface_->owner = this;
  }

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
#if (defined(__EMSCRIPTEN__) && defined(FLUX_WEB_RENDERER_DOM)) || defined(FLUX_SSR)
    if (IDomAdapter *adapter = getActiveDomAdapter())
    {
        _renderDom(adapter);
        needsPaint = false;
        return;
    }
#endif
    borderColor = isFocused ? dropdownFocusedBorderColor : dropdownBorderColor;
    drawRoundedRectangle(ctx);

    Painter painter(ctx, this);
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

    listSurface_->x = x;
    listSurface_->y = y + height + 2;
    listSurface_->width = listWidth_;
    listSurface_->height = listH;
    ui->showOverlay(listSurface_.get(), /*zIndex=*/100,
                    /*modal=*/true, /*blocksHoverBelow=*/false,
                    /*capturesKeyboard=*/true);
    markNeedsPaint();
  }

  void closeDropdown()
  {
    if (!isOpen)
      return;
    isOpen = false;
    hoveredItemIndex = -1;
    if (auto *ui = FluxUI::getCurrentInstance())
      ui->hideOverlay(listSurface_.get());
#if (defined(__EMSCRIPTEN__) && defined(FLUX_WEB_RENDERER_DOM)) || defined(FLUX_SSR)
    // hideOverlay() only stops FUTURE render() calls on listSurface_ —
    // it does NOT touch whatever DOM nodes the last successful render
    // already created and attached under #flux-dom-root. Unlike the
    // canvas backend (where "stop drawing" naturally means "gone" on
    // the next cleared frame), DOM nodes are persistent objects that
    // stay exactly as last styled until something explicitly removes
    // them — hence the dropdown list staying visible/stale after close
    // without this. Evicting here forces a full recreate on the next
    // openDropdown(), which is a fine tradeoff for an overlay that only
    // opens/closes on direct user action, not something repainted at
    // high frequency.
    fluxDomEvictWidget(listSurface_.get());
#endif
    markNeedsPaint();
  }

  void refreshDropdownPopup_()
  {
    if (!isOpen)
      return;
    if (auto *ui = FluxUI::getCurrentInstance())
      ui->refreshOverlay(listSurface_.get());
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

#if (defined(__EMSCRIPTEN__) && defined(FLUX_WEB_RENDERER_DOM)) || defined(FLUX_SSR)
  // Two layers under one owner: the box itself (background+border) and
  // the label text; the arrow rides along as a THIRD slot rather than
  // being folded into the box, since it's a distinct piece of text at
  // a fixed position on the box's right edge, not part of the label's
  // own flow.
  void _renderDom(IDomAdapter *adapter)
  {
      char buf[24];
      auto px = [&](int v) { snprintf(buf, sizeof(buf), "%dpx", v); return std::string(buf); };
      char colbuf[32];
      auto rgb = [&](Color c) { snprintf(colbuf, sizeof(colbuf), "rgb(%d,%d,%d)", c.r, c.g, c.b); return std::string(colbuf); };

      // ── Box ───────────────────────────────────────────────────────────
      DomNodeHandle box = fluxDomEnsureNode(this, "div", "box");
      fluxDomApplyRect(this, x, y, width, height, "box");
      adapter->setStyle(box, "background-color", rgb(dropdownBgColor));
      adapter->setStyle(box, "border", "1px solid " +
                        rgb(isFocused ? dropdownFocusedBorderColor : dropdownBorderColor));
      adapter->setStyle(box, "border-radius", px(borderRadius));
      adapter->setStyle(box, "box-sizing", "border-box");
      adapter->setStyle(box, "pointer-events", "none");

      // ── Label ─────────────────────────────────────────────────────────
      bool hasSelection = (selectedIndex >= 0 && selectedIndex < (int)options.size());
      const std::string &label = hasSelection ? options[selectedIndex] : placeholder;
      DomNodeHandle labelNode = fluxDomEnsureNode(this, "div", "label");
      fluxDomApplyRect(this, x + paddingLeft, y + paddingTop,
                      width - paddingLeft - paddingRight,
                      height - paddingTop - paddingBottom, "label");
      adapter->setStyle(labelNode, "display", "flex");
      adapter->setStyle(labelNode, "align-items", "center");
      adapter->setStyle(labelNode, "white-space", "nowrap");
      adapter->setStyle(labelNode, "overflow", "hidden");
      adapter->setStyle(labelNode, "text-overflow", "ellipsis");
      adapter->setStyle(labelNode, "font-size", px(fontSize));
      adapter->setStyle(labelNode, "color", rgb(hasSelection ? getCurrentTextColor() : placeholderColor));
      adapter->setStyle(labelNode, "pointer-events", "none");
      adapter->setText(labelNode, label);

      // ── Arrow — unicode glyph, same trick as CheckBoxWidget's
      // checkmark, in place of two hand-drawn rotated line divs.
      int arrowSizePx = std::max(10, arrowSize + 4);
      DomNodeHandle arrow = fluxDomEnsureNode(this, "div", "arrow");
      fluxDomApplyRect(this, x + width - paddingRight + 4,
                      y + height / 2 - arrowSizePx / 2,
                      arrowSizePx, arrowSizePx, "arrow");
      adapter->setStyle(arrow, "display", "flex");
      adapter->setStyle(arrow, "align-items", "center");
      adapter->setStyle(arrow, "justify-content", "center");
      adapter->setStyle(arrow, "font-size", px(arrowSizePx - 2));
      adapter->setStyle(arrow, "color", rgb(arrowColor));
      adapter->setStyle(arrow, "pointer-events", "none");
      // "\xE2\x96\xB2" = ▲ (U+25B2), "\xE2\x96\xBC" = ▼ (U+25BC)
      adapter->setText(arrow, isOpen ? "\xE2\x96\xB2" : "\xE2\x96\xBC");
  }
#endif

};

using DropdownWidgetPtr = std::shared_ptr<DropdownWidget>;

inline DropdownWidgetPtr
Dropdown(const std::vector<std::string> &options = {})
{
  auto w = std::make_shared<DropdownWidget>();
  if (!options.empty())
    w->setOptions(options);
  return w;
}

#endif // FLUX_DROPDOWN_HPP