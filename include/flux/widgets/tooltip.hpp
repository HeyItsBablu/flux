#ifndef FLUX_TOOLTIP_HPP
#define FLUX_TOOLTIP_HPP

#include "flux_structure.hpp"

#include "flux/flux_app.hpp"
#include "flux/flux_core.hpp"
#include <algorithm>

#if (defined(__EMSCRIPTEN__) && defined(FLUX_WEB_RENDERER_DOM)) || defined(FLUX_SSR)
#include "flux/flux_dom_adapter.hpp"
// Declared in flux_painter_dom.cpp — normally invoked automatically from
// Widget::onDetach() when a widget leaves the TREE. TipSurface never
// does that (it's a long-lived overlay widget registered once via
// showOverlay/hideOverlay, never addChild'd/removed from any parent),
// so closeTooltip() below calls this directly instead — same fix and
// same reasoning as DropdownWidget::closeDropdown().
extern void fluxDomEvictWidget(Widget *owner);
#endif


// ============================================================================
// TOOLTIP WIDGET
// ============================================================================

enum class TooltipPosition
{
    Above,
    Below,
    Auto
};

class TooltipWidget : public Widget
{
private:
    int tipW_ = 0, tipH_ = 0;

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
    int tipGap = 6; // space between anchor and bubble
    int shadowOffset = 2;

    // ── Popup-body surface ─────────────────────────────────────────────────
    // Non-modal, never captures keyboard, never blocks hover below — the
    // bubble is purely informational. A click anywhere dismisses it but is
    // never claimed (handleMouseDown returns false) so it still reaches
    // whatever's underneath, matching the old onOverlayMouseDown contract.
    class TipSurface : public Widget
    {
    public:
        TooltipWidget *owner = nullptr;

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
            if (!owner || !owner->isVisible || owner->tipText.empty())
                return;
            Painter painter(ctx, this);

            painter.fillRoundedRect(x + owner->shadowOffset, y + owner->shadowOffset,
                                    width, height, owner->tipBorderRadius,
                                    Color::fromRGBA(0, 0, 0, 60));
            painter.fillRoundedRect(x, y, width, height, owner->tipBorderRadius,
                                    owner->tipBgColor);
            painter.drawBorder(x, y, width, height, owner->tipBorderRadius,
                               owner->tipBorderColor, 1);

            std::wstring wtip = toWideString(owner->tipText);
            NativeFont font = fontCache.getFont(owner->tipFontSize, FontWeight::Normal);
            painter.drawText(wtip, x + owner->tipPadH, y + owner->tipPadV,
                             width - owner->tipPadH * 2, height - owner->tipPadV * 2,
                             font, owner->tipTextColor,
                             DT_CENTER | DT_VCENTER | DT_WORDBREAK | DT_END_ELLIPSIS);
            needsPaint = false;
        }

        bool handleMouseDown(int, int) override
        {
            if (owner)
                owner->closeTooltip();
            return false; // never claim the click — let it fall through
        }

        void onOverlayOutsideClick() override
        {
            if (owner)
                owner->closeTooltip();
        }


    private:
#if (defined(__EMSCRIPTEN__) && defined(FLUX_WEB_RENDERER_DOM)) || defined(FLUX_SSR)
        // Single node: background + border + drop shadow are all
        // expressible as CSS properties on one element (box-shadow
        // replaces the separate offset-rect the canvas backend draws for
        // its shadow), and the tip text can live as that same node's own
        // text content — same "checkmark rides along as text content"
        // trick CheckBoxWidget's box node already uses, since nothing
        // here needs to overlap the background independently.
        void _renderDom(IDomAdapter *adapter)
        {
            if (!owner || owner->tipText.empty())
                return;
            char buf[24];
            auto px = [&](int v) { snprintf(buf, sizeof(buf), "%dpx", v); return std::string(buf); };
            char colbuf[48];
            auto rgba = [&](Color c) {
                snprintf(colbuf, sizeof(colbuf), "rgba(%d,%d,%d,%.3f)", c.r, c.g, c.b, c.a / 255.f);
                return std::string(colbuf);
            };

            DomNodeHandle bubble = fluxDomEnsureNode(this, "div", "bubble");
            fluxDomApplyRect(this, x, y, width, height, "bubble");
            adapter->setStyle(bubble, "background-color", rgba(owner->tipBgColor));
            adapter->setStyle(bubble, "border", "1px solid " + rgba(owner->tipBorderColor));
            adapter->setStyle(bubble, "border-radius", px(owner->tipBorderRadius));
            adapter->setStyle(bubble, "box-shadow",
                              px(owner->shadowOffset) + " " + px(owner->shadowOffset) +
                              " 0 rgba(0,0,0,0.235)"); // matches fromRGBA(0,0,0,60) alpha
            adapter->setStyle(bubble, "box-sizing", "border-box");
            adapter->setStyle(bubble, "display", "flex");
            adapter->setStyle(bubble, "align-items", "center");
            adapter->setStyle(bubble, "justify-content", "center");
            adapter->setStyle(bubble, "text-align", "center");
            adapter->setStyle(bubble, "padding", px(owner->tipPadV) + " " + px(owner->tipPadH));
            adapter->setStyle(bubble, "font-size", px(owner->tipFontSize));
            adapter->setStyle(bubble, "color", rgba(owner->tipTextColor));
            adapter->setStyle(bubble, "white-space", "normal");
            adapter->setStyle(bubble, "overflow", "hidden");
            adapter->setStyle(bubble, "text-overflow", "ellipsis");
            adapter->setStyle(bubble, "pointer-events", "none");
            adapter->setStyle(bubble, "z-index", "10");
            adapter->setText(bubble, owner->tipText);
        }
#endif
    };

    std::shared_ptr<TipSurface> tipSurface_;

public:
    bool isVisible = false;

    explicit TooltipWidget(WidgetPtr anchor, const std::string &tooltip)
        : tipText(tooltip)
    {

        tipSurface_ = std::make_shared<TipSurface>();
        tipSurface_->owner = this;

        if (anchor)
        {
            addChild(anchor);
            chainAnchorHover(anchor.get());
        }
    }

    void onDetach() override
    {
        if (isVisible)
            closeTooltip();
        Widget::onDetach();
    }

    // ── Builder API ───────────────────────────────────────────────────────
    std::shared_ptr<TooltipWidget> setTooltipText(const std::string &t)
    {
        tipText = t;
        return self_();
    }
    std::shared_ptr<TooltipWidget> setPosition(TooltipPosition pos)
    {
        preferredPosition = pos;
        return self_();
    }
    std::shared_ptr<TooltipWidget> setTooltipBackground(Color color)
    {
        tipBgColor = color;
        return self_();
    }
    std::shared_ptr<TooltipWidget> setTooltipTextColor(Color color)
    {
        tipTextColor = color;
        return self_();
    }
    std::shared_ptr<TooltipWidget> setTooltipFontSize(int size)
    {
        tipFontSize = size;
        return self_();
    }
    std::shared_ptr<TooltipWidget> setTooltipMaxWidth(int w)
    {
        tipMaxWidth = w;
        return self_();
    }

    // ── Layout (anchor only — tooltip itself has no in-tree size) ─────────
    void computeLayout(GraphicsContext &ctx, const BoxConstraints &constraints,
                       FontCache &fontCache) override
    {
        if (autoWidth)
            width = constraints.maxWidth;
        if (autoHeight)
            height = constraints.maxHeight;

        if (!children.empty())
        {
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

    void positionChildren(int, int, int, int) override
    {
        if (!children.empty())
        {
            auto &anchor = children[0];
            anchor->x = x;
            anchor->y = y;
            anchor->positionChildren(
                anchor->x + anchor->paddingLeft,
                anchor->y + anchor->paddingTop,
                anchor->width - anchor->paddingLeft - anchor->paddingRight,
                anchor->height - anchor->paddingTop - anchor->paddingBottom);
        }
    }

    void render(GraphicsContext &ctx, FontCache &fontCache) override
    {


        if (!children.empty())
            children[0]->render(ctx, fontCache);
        needsPaint = false;
    }

private:
    std::shared_ptr<TooltipWidget> self_()
    {
        return std::static_pointer_cast<TooltipWidget>(shared_from_this());
    }

    void chainAnchorHover(Widget *anchor)
    {
        HoverHandler previous = anchor->onHover;
        anchor->onHover = [this, previous](bool hovered)
        {
            if (hovered)
                openTooltip();
            else
                closeTooltip();
            if (previous)
                previous(hovered);
        };
    }

    // clientX/clientY are MAIN-WINDOW CLIENT coordinates, same space show()
    // expects. Win32's show() does its own screen-space monitor clamping on
    // top of this; the clamp here just keeps the bubble inside the window
    // itself, which matters on every platform including non-Win32 (where
    // show() does no clamping at all).
    void computeBubbleGeometry(FluxUI *ui, int &outClientX, int &outClientY)
    {
        std::wstring wtip = toWideString(tipText);
        int charW = static_cast<int>(tipFontSize * 0.62f);
        int lineH = tipFontSize + 4;
        int textW = static_cast<int>(wtip.size()) * charW;
        int maxTW = tipMaxWidth - tipPadH * 2;
        int lines = std::max(1, (textW + maxTW - 1) / maxTW);

        tipW_ = std::min(textW + tipPadH * 2, tipMaxWidth);
        tipH_ = lines * lineH + tipPadV * 2;

        int anchorCX = x + width / 2;
        int above = y - tipH_ - tipGap;
        int below = y + height + tipGap;
        bool fitsAbove = above >= 0;

        int clientX = anchorCX - tipW_ / 2;
        int clientY;
        if (preferredPosition == TooltipPosition::Above)
            clientY = fitsAbove ? above : 0;
        else if (preferredPosition == TooltipPosition::Below)
            clientY = below;
        else // Auto
            clientY = fitsAbove ? above : below;

        auto sz = ui->getClientSize();
        if (clientX + tipW_ > sz.width)
            clientX = sz.width - tipW_;
        if (clientX < 0)
            clientX = 0;
        if (clientY + tipH_ > sz.height)
            clientY = sz.height - tipH_;
        if (clientY < 0)
            clientY = 0;

        outClientX = clientX;
        outClientY = clientY;
    }

    void openTooltip()
    {
        if (isVisible || tipText.empty())
            return;

        auto *ui = FluxUI::getCurrentInstance();
        if (!ui)
            return;

        int clientX, clientY;
        computeBubbleGeometry(ui, clientX, clientY);

        isVisible = true;

        tipSurface_->x = clientX;
        tipSurface_->y = clientY;
        tipSurface_->width = tipW_;
        tipSurface_->height = tipH_;

        ui->showOverlay(tipSurface_.get(), /*zIndex=*/50,
                        /*modal=*/false, /*blocksHoverBelow=*/false,
                        /*capturesKeyboard=*/false);
    }

    void closeTooltip()
    {
        if (!isVisible)
            return;
        isVisible = false;
        if (auto *ui = FluxUI::getCurrentInstance())
            ui->hideOverlay(tipSurface_.get());
#if (defined(__EMSCRIPTEN__) && defined(FLUX_WEB_RENDERER_DOM)) || defined(FLUX_SSR)
        // Same rationale as DropdownWidget::closeDropdown() — hideOverlay()
        // only stops FUTURE render() calls; it doesn't touch whatever DOM
        // node the last successful render already attached under
        // #flux-dom-root. Without this, the tooltip bubble would stay
        // visible/stale on screen after the anchor is un-hovered.
        fluxDomEvictWidget(tipSurface_.get());
#endif
    }
};

// ============================================================================
// FACTORY FUNCTIONS
// ============================================================================

using TooltipWidgetPtr = std::shared_ptr<TooltipWidget>;

inline TooltipWidgetPtr Tooltip(WidgetPtr anchor, const std::string &tooltip)
{
    return std::make_shared<TooltipWidget>(anchor, tooltip);
}

#endif // FLUX_TOOLTIP_HPP