#ifndef FLUX_PAGE_HPP
#define FLUX_PAGE_HPP

#include "flux/flux_core.hpp"
#include "flux/flux_widget.hpp"
#include <memory>

// ============================================================================
// PageWidget
//
// HTML-page-shaped container: header (auto-height) / body (fills remainder)
// / footer (auto-height), stacked vertically — the single-column special
// case of FlexWidget's Column direction, without wrap/flex-grow resolution.
//
// Each slot is independently optional. If a slot widget is null, its height
// is 0 and the remaining slots absorb the space.
//
// Future-proofing note (mirrors drawImage/drawCamera/drawVideo):
// PageWidget::render() calls Painter::drawPage() once to draw page/region
// chrome (backgrounds, separators, elevation shadows), then renders each
// slot's widget subtree itself. drawPage owns the *containers* (today: just
// rects; later: could reconcile real <header>/<main>/<footer> DOM wrapper
// elements via params.widgetId) — it never owns the children's content,
// since those are full widget subtrees, not opaque leaf content.
// ============================================================================

class PageWidget : public Widget
{
private:
    WidgetPtr headerWidget_;
    WidgetPtr bodyWidget_;
    WidgetPtr footerWidget_;

    std::shared_ptr<PageWidget> self_;

    // ── resolved-this-frame region rects, used by positionChildren/render ──
    struct Region
    {
        bool present = false;
        int x = 0, y = 0, w = 0, h = 0;
    };
    Region headerRegion_, bodyRegion_, footerRegion_;

public:
    // ── Page-level chrome ────────────────────────────────────────────────
    bool hasPageBackground = false;
    Color pageBackgroundColor = Color::fromRGB(255, 255, 255);

    // ── Per-region chrome ────────────────────────────────────────────────
    bool headerHasBackground = false;
    Color headerBackgroundColor = Color::fromRGB(255, 255, 255);
    int headerElevation = 0; // drop-shadow depth under header (0 = none)

    bool footerHasBackground = false;
    Color footerBackgroundColor = Color::fromRGB(255, 255, 255);
    int footerElevation = 0; // drop-shadow depth above footer (0 = none)

    bool bodyHasBackground = false;
    Color bodyBackgroundColor = Color::fromRGB(255, 255, 255);

    void setSelf(std::shared_ptr<PageWidget> ptr) { self_ = ptr; }
    std::shared_ptr<PageWidget> self() { return self_; }

    // ── Slot setters — replace previous slot widget if called again ───────

    std::shared_ptr<PageWidget> setHeader(WidgetPtr w)
    {
        replaceSlot(headerWidget_, w);
        markNeedsLayout();
        return self();
    }
    std::shared_ptr<PageWidget> setBody(WidgetPtr w)
    {
        replaceSlot(bodyWidget_, w);
        markNeedsLayout();
        return self();
    }
    std::shared_ptr<PageWidget> setFooter(WidgetPtr w)
    {
        replaceSlot(footerWidget_, w);
        markNeedsLayout();
        return self();
    }

    WidgetPtr getHeader() const { return headerWidget_; }
    WidgetPtr getBody() const { return bodyWidget_; }
    WidgetPtr getFooter() const { return footerWidget_; }

    // ── Chrome setters ───────────────────────────────────────────────────

    std::shared_ptr<PageWidget> setPageBackgroundColor(Color c)
    {
        hasPageBackground = true;
        pageBackgroundColor = c;
        markNeedsPaint();
        return self();
    }
    std::shared_ptr<PageWidget> setHeaderBackgroundColor(Color c)
    {
        headerHasBackground = true;
        headerBackgroundColor = c;
        markNeedsPaint();
        return self();
    }
    std::shared_ptr<PageWidget> setFooterBackgroundColor(Color c)
    {
        footerHasBackground = true;
        footerBackgroundColor = c;
        markNeedsPaint();
        return self();
    }
    std::shared_ptr<PageWidget> setBodyBackgroundColor(Color c)
    {
        bodyHasBackground = true;
        bodyBackgroundColor = c;
        markNeedsPaint();
        return self();
    }
    std::shared_ptr<PageWidget> setHeaderElevation(int e)
    {
        headerElevation = e;
        markNeedsPaint();
        return self();
    }
    std::shared_ptr<PageWidget> setFooterElevation(int e)
    {
        footerElevation = e;
        markNeedsPaint();
        return self();
    }

    void onDetach() override
    {
        Widget::onDetach();
    }

    // ── Layout ───────────────────────────────────────────────────────────
    //
    // header: measured loosely at full page width, unbounded height -> its
    //         own intrinsic/auto height (mirrors HTML <header> shrink-to-fit).
    // footer: same.
    // body:   gets whatever vertical space remains, tight on both axes
    //         (mirrors HTML <main> filling remaining viewport height).
    //
    // If header+footer alone exceed the available height, body is clamped
    // to 0 and footer is allowed to render past the bottom edge (same
    // failure mode a real HTML page has with overflow:hidden ancestors).

    void computeLayout(GraphicsContext &ctx, const BoxConstraints &constraints,
                       FontCache &fontCache) override
    {
        if (!visible)
        {
            width = height = 0;
            needsLayout = false;
            return;
        }

        BoxConstraints self = selfConstraints(constraints);

        int outerMaxW = (widthMode == SizeMode::Fixed) ? width : self.maxWidth;
        int outerMaxH = (heightMode == SizeMode::Fixed) ? height : self.maxHeight;
        if (widthMode == SizeMode::Full)
            outerMaxW = self.maxWidth;
        if (heightMode == SizeMode::Full)
            outerMaxH = self.maxHeight;

        int pageW = std::max(0, outerMaxW);
        int pageH = std::max(0, outerMaxH);

        int headerH = 0, footerH = 0;

        if (headerWidget_ && headerWidget_->visible)
        {
            headerWidget_->computeLayout(
                ctx, BoxConstraints::loose(pageW, kUnbounded), fontCache);
            headerH = std::min(headerWidget_->height, pageH);
        }

        if (footerWidget_ && footerWidget_->visible)
        {
            footerWidget_->computeLayout(
                ctx, BoxConstraints::loose(pageW, kUnbounded), fontCache);
            footerH = std::min(footerWidget_->height, std::max(0, pageH - headerH));
        }

        int bodyH = std::max(0, pageH - headerH - footerH);

        if (bodyWidget_ && bodyWidget_->visible)
        {
            BoxConstraints bodyC = (bodyWidget_->heightMode == SizeMode::Fixed)
                                       ? BoxConstraints::loose(pageW, bodyH)
                                       : BoxConstraints(pageW, pageW, bodyH, bodyH);
            bodyWidget_->computeLayout(ctx, bodyC, fontCache);
        }

        // ── resolve final widget/header/footer/body rects (top-left will be
        //    filled in by positionChildren once x/y are known) ─────────────
        headerRegion_ = {headerWidget_ && headerWidget_->visible, 0, 0, pageW, headerH};
        bodyRegion_ = {bodyWidget_ && bodyWidget_->visible, 0, headerH, pageW, bodyH};
        footerRegion_ = {footerWidget_ && footerWidget_->visible, 0, headerH + bodyH, pageW, footerH};

        width = (widthMode == SizeMode::Fit) ? pageW : self.clampWidth(outerMaxW);
        height = (heightMode == SizeMode::Fit)
                     ? (headerH + bodyH + footerH)
                     : self.clampHeight(outerMaxH);

        applyConstraints();
        needsLayout = false;
    }

    // ── Position ─────────────────────────────────────────────────────────

    void positionChildren(int contentX, int contentY, int /*cw*/, int /*ch*/) override
    {
        headerRegion_.x = contentX;
        headerRegion_.y = contentY;
        bodyRegion_.x = contentX;
        bodyRegion_.y = contentY + headerRegion_.h;
        footerRegion_.x = contentX;
        footerRegion_.y = contentY + headerRegion_.h + bodyRegion_.h;

        if (headerRegion_.present)
        {
            headerWidget_->x = headerRegion_.x;
            headerWidget_->y = headerRegion_.y;
            headerWidget_->positionChildren(
                headerWidget_->x + headerWidget_->paddingLeft,
                headerWidget_->y + headerWidget_->paddingTop,
                headerWidget_->width - headerWidget_->paddingLeft - headerWidget_->paddingRight,
                headerWidget_->height - headerWidget_->paddingTop - headerWidget_->paddingBottom);
        }
        if (bodyRegion_.present)
        {
            bodyWidget_->x = bodyRegion_.x;
            bodyWidget_->y = bodyRegion_.y;
            bodyWidget_->positionChildren(
                bodyWidget_->x + bodyWidget_->paddingLeft,
                bodyWidget_->y + bodyWidget_->paddingTop,
                bodyWidget_->width - bodyWidget_->paddingLeft - bodyWidget_->paddingRight,
                bodyWidget_->height - bodyWidget_->paddingTop - bodyWidget_->paddingBottom);
        }
        if (footerRegion_.present)
        {
            footerWidget_->x = footerRegion_.x;
            footerWidget_->y = footerRegion_.y;
            footerWidget_->positionChildren(
                footerWidget_->x + footerWidget_->paddingLeft,
                footerWidget_->y + footerWidget_->paddingTop,
                footerWidget_->width - footerWidget_->paddingLeft - footerWidget_->paddingRight,
                footerWidget_->height - footerWidget_->paddingTop - footerWidget_->paddingBottom);
        }
    }

    // ── Render ───────────────────────────────────────────────────────────

    void render(GraphicsContext &ctx, FontCache &fontCache) override
    {
        if (!visible)
            return;

        Painter painter(ctx);

        Painter::PageDrawParams params;
        params.x = x;
        params.y = y;
        params.w = width;
        params.h = height;
        params.widgetId = id; // reserved for a future DOM backend

        params.hasPageBackground = hasPageBackground;
        params.pageBackground = pageBackgroundColor;

        params.header.present = headerRegion_.present;
        params.header.x = headerRegion_.x;
        params.header.y = headerRegion_.y;
        params.header.w = headerRegion_.w;
        params.header.h = headerRegion_.h;
        params.header.hasBackground = headerHasBackground;
        params.header.background = headerBackgroundColor;
        params.header.elevation = headerElevation;

        params.body.present = bodyRegion_.present;
        params.body.x = bodyRegion_.x;
        params.body.y = bodyRegion_.y;
        params.body.w = bodyRegion_.w;
        params.body.h = bodyRegion_.h;
        params.body.hasBackground = bodyHasBackground;
        params.body.background = bodyBackgroundColor;

        params.footer.present = footerRegion_.present;
        params.footer.x = footerRegion_.x;
        params.footer.y = footerRegion_.y;
        params.footer.w = footerRegion_.w;
        params.footer.h = footerRegion_.h;
        params.footer.hasBackground = footerHasBackground;
        params.footer.background = footerBackgroundColor;
        params.footer.elevation = footerElevation;

        painter.drawPage(params);

        if (headerRegion_.present)
        {
            painter.pushClipRect(headerRegion_.x, headerRegion_.y, headerRegion_.w, headerRegion_.h);
            headerWidget_->render(ctx, fontCache);
            painter.popClipRect();
        }
        if (bodyRegion_.present)
        {
            painter.pushClipRect(bodyRegion_.x, bodyRegion_.y, bodyRegion_.w, bodyRegion_.h);
            bodyWidget_->render(ctx, fontCache);
            painter.popClipRect();
        }
        if (footerRegion_.present)
        {
            painter.pushClipRect(footerRegion_.x, footerRegion_.y, footerRegion_.w, footerRegion_.h);
            footerWidget_->render(ctx, fontCache);
            painter.popClipRect();
        }

        needsPaint = false;
    }

private:
    void replaceSlot(WidgetPtr &slot, WidgetPtr newWidget)
    {
        if (slot)
        {
            slot->parent = nullptr;
            auto it = std::find(children.begin(), children.end(), slot);
            if (it != children.end())
                children.erase(it);
        }
        slot = newWidget;
        if (slot)
        {
            slot->parent = this;
            children.push_back(slot);
        }
    }
};

using PageWidgetPtr = std::shared_ptr<PageWidget>;

inline PageWidgetPtr Page()
{
    auto w = std::make_shared<PageWidget>();
    w->setSelf(w);
    w->widthMode = SizeMode::Full;
    w->heightMode = SizeMode::Full;
    return w;
}

#endif // FLUX_PAGE_HPP