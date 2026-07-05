#ifndef FLUX_DROPDOWN_HPP
#define FLUX_DROPDOWN_HPP

#include "flux_structure.hpp"

#include "flux/flux_app.hpp"
#include "flux/flux_core.hpp"
#include <algorithm>

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