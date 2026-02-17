#ifndef FLUX_TOOLTIP_HPP
#define FLUX_TOOLTIP_HPP

#include "flux_app.hpp"
#include "flux_core.hpp"

// ============================================================================
// TOOLTIP WIDGET
//
// Design:
//   - TooltipWidget wraps an anchor child widget.
//   - It hijacks (chains) the anchor's onHover callback to open/close itself.
//   - When open it registers a renderer with FluxAppWidget at zIndex 50
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
  FluxAppWidget *fluxApp = nullptr;

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

  void setFluxApp(FluxAppWidget *app) { fluxApp = app; }

  // ----------------------------------------------------------------
  // onDetach — called by FluxUI::rebuild() before old tree is dropped.
  // Ensures the overlay stack never holds a dangling pointer.
  // ----------------------------------------------------------------
  void onDetach() override {
    if (isVisible && fluxApp) {
      fluxApp->removeOverlay(this);
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
    if (isVisible || !fluxApp || tipText.empty())
      return;

    computeBubbleGeometry();
    isVisible = true;

    fluxApp->addOverlay(
        this,
        [this](HDC hdc, FontCache &fc) { renderBubble(hdc, fc); },
        50 // zIndex: below Dropdown(100) and Dialog(200)
    );
  }

  // ----------------------------------------------------------------
  // closeTooltip
  // ----------------------------------------------------------------
  void closeTooltip() {
    if (!isVisible || !fluxApp)
      return;

    isVisible = false;
    fluxApp->removeOverlay(this);
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
  // renderBubble — called by the FluxApp overlay renderer
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

using TooltipWidgetPtr = std::shared_ptr<TooltipWidget>;

// ============================================================================
// FACTORY
// ============================================================================

inline TooltipWidgetPtr Tooltip(WidgetPtr anchor, const std::string &tooltip) {
  return std::make_shared<TooltipWidget>(anchor, tooltip);
}

#endif // FLUX_TOOLTIP_HPP