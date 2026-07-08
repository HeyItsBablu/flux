#ifndef FLUX_DIALOG_HPP
#define FLUX_DIALOG_HPP

#include "flux_structure.hpp"

#include "flux/flux_app.hpp"
#include "flux/flux_core.hpp"
#include <algorithm>

#if (defined(__EMSCRIPTEN__) && defined(FLUX_WEB_RENDERER_DOM)) || defined(FLUX_SSR)
#include "flux/flux_dom_adapter.hpp"
extern void fluxDomEvictWidget(Widget *owner);
#endif




// ============================================================================
// DIALOG WIDGET
// ============================================================================

class DialogWidget : public Widget
{
private:
  bool popupShown_ = false;
  bool contentDirty_ = true;
  int dialogX_ = 0, dialogY_ = 0; // box position within the overlay rect
  int winW_ = 0, winH_ = 0;       // client size captured when opened

  // ── Popup-body surface ─────────────────────────────────────────────────
  // Covers the full client area (x=0, y=0, width=winW_, height=winH_),
  // same footprint the old overlay rect used. Because the surface's own
  // origin is (0,0), every coordinate handleMouseDown/Move/Up receives is
  // already numerically identical to the old "local to the overlay rect"
  // coordinates — dialogX_/dialogY_ math below is untouched.
  class DialogSurface : public Widget
  {
  public:
    DialogWidget *owner = nullptr;

    void render(GraphicsContext &ctx, FontCache &fontCache) override
    {
#if (defined(__EMSCRIPTEN__) && defined(FLUX_WEB_RENDERER_DOM)) || defined(FLUX_SSR)
      if (IDomAdapter *adapter = getActiveDomAdapter())
      {
          _renderDom(adapter, ctx, fontCache);
          needsPaint = false;
          return;
      }
#endif
      if (!owner || !owner->isOpen)
        return;

      Painter painter(ctx, this);

      // Dim scrim across the whole overlay rect (the full client area —
      // see DialogWidget::open()).
      painter.fillRectAlpha(x, y, width, height, owner->overlayColor);

      owner->dialogX_ = x + (width - owner->dialogWidth) / 2;
      owner->dialogY_ = y + (height - owner->dialogHeight) / 2;

      painter.fillRoundedRect(owner->dialogX_, owner->dialogY_,
                              owner->dialogWidth, owner->dialogHeight,
                              owner->dialogBorderRadius, owner->dialogBgColor);
      painter.drawBorder(owner->dialogX_, owner->dialogY_,
                         owner->dialogWidth, owner->dialogHeight,
                         owner->dialogBorderRadius, owner->dialogBorderColor, 1);

      if (owner->content)
      {
        owner->layoutContentIfNeeded(ctx, fontCache);
        owner->content->render(ctx, fontCache);
      }
      needsPaint = false;
    }

    bool handleMouseDown(int mx, int my) override
    {
      if (!owner || !owner->isOpen)
        return false;

      if (mx < owner->dialogX_ || mx >= owner->dialogX_ + owner->dialogWidth ||
          my < owner->dialogY_ || my >= owner->dialogY_ + owner->dialogHeight)
      {
        if (owner->closeOnClickOutside)
          owner->close();
        return true; // modal — swallow regardless of whether we closed
      }

      if (owner->content)
        owner->dispatchContentMouseDown(mx, my);

      return true;
    }

    bool handleMouseUp(int mx, int my) override
    {
      if (!owner || !owner->isOpen || !owner->content)
        return false;
      return broadcastMouseEvent(owner->content.get(), mx, my,
                                 [](Widget *w, int x2, int y2)
                                 { return w->handleMouseUp(x2, y2); });
    }

    bool handleMouseMove(int mx, int my) override
    {
      if (!owner || !owner->isOpen || !owner->content)
        return false;
      // owner's overlay entry sets blocksHoverBelow=true, so FluxUI skips
      // updateHoverStates on root while the dialog is open — the dialog
      // has to drive hover for its own content subtree itself (otherwise
      // buttons inside the dialog would never show hover feedback).
      return updateHoverStates(owner->content.get(), mx, my);
    }

    bool handleKeyDown(int keyCode) override
    {
      if (!owner || !owner->isOpen)
        return false;
      if (owner->closeOnEscape && keyCode == Key::Escape)
      {
        owner->close();
        return true;
      }
      return true; // modal — swallow all keys while open, handled or not
    }

    void onOverlayOutsideClick() override
    {
      // The dialog's overlay rect covers the full client area, so
      // handleMouseDown already handles "inside the scrim, outside the
      // box". This only fires if a click somehow lands outside the
      // surface's own rect entirely — shouldn't happen for a full-window
      // overlay, but close defensively if it ever does.
      if (owner && owner->closeOnClickOutside)
        owner->close();
    }

  private:
#if (defined(__EMSCRIPTEN__) && defined(FLUX_WEB_RENDERER_DOM)) || defined(FLUX_SSR)
    // Two nodes: "scrim" (the dim backdrop, full overlay rect) and "box"
    // (the dialog panel background+border). content, if present, renders
    // through its OWN widget tree's render() calls, unmodified — the
    // only thing that needed fixing for content to place correctly is
    // its Widget::parent (see DialogWidget::open()/close() below), NOT
    // anything in this method.
    void _renderDom(IDomAdapter *adapter, GraphicsContext &ctx, FontCache &fontCache)
    {
        if (!owner || !owner->isOpen)
            return;
        char colbuf[48];
        auto rgba = [&](Color c) {
            snprintf(colbuf, sizeof(colbuf), "rgba(%d,%d,%d,%.3f)", c.r, c.g, c.b, c.a / 255.f);
            return std::string(colbuf);
        };
        char buf[24];
        auto px = [&](int v) { snprintf(buf, sizeof(buf), "%dpx", v); return std::string(buf); };

        DomNodeHandle scrim = fluxDomEnsureNode(this, "div", "scrim");
        fluxDomApplyRect(this, x, y, width, height, "scrim");
        adapter->setStyle(scrim, "background-color", rgba(owner->overlayColor));
        adapter->setStyle(scrim, "pointer-events", "none");
        adapter->setStyle(scrim, "z-index", "20");

        owner->dialogX_ = x + (width - owner->dialogWidth) / 2;
        owner->dialogY_ = y + (height - owner->dialogHeight) / 2;

        DomNodeHandle box = fluxDomEnsureNode(this, "div", "box");
        fluxDomApplyRect(this, owner->dialogX_, owner->dialogY_,
                        owner->dialogWidth, owner->dialogHeight, "box");
        adapter->setStyle(box, "background-color", rgba(owner->dialogBgColor));
        adapter->setStyle(box, "border", "1px solid " + rgba(owner->dialogBorderColor));
        adapter->setStyle(box, "border-radius", px(owner->dialogBorderRadius));
        adapter->setStyle(box, "box-sizing", "border-box");
        adapter->setStyle(box, "pointer-events", "none");
        adapter->setStyle(box, "z-index", "21");


        // Explicit default-slot ("") node for the surface itself. This is
        // exactly the node ensureNode() implicitly creates/reuses as
        // content's DOM PARENT the moment content (or any of its
        // descendants) paints anything — see ensureNode's
        // `owner->parent` branch, reached because DialogWidget::open()
        // sets content->parent = dialogSurface_. Left unstyled, that
        // implicit node gets no z-index at all, which defaults to
        // z-index:auto (~0) — and a positioned sibling with NO explicit
        // z-index always stacks BELOW any sibling that has a positive
        // one, regardless of DOM insertion order. Since "scrim" (20) and
        // "box" (21) both have explicit z-indexes, content's container
        // was silently painting UNDERNEATH the opaque dialog box —
        // fully positioned and correct, just invisible. Pre-creating it
        // here (before content->render() runs) means the later implicit
        // creation is a cache hit on this same, now-correctly-stacked
        // node.
        DomNodeHandle contentLayer = fluxDomEnsureNode(this, "div", "");
        adapter->setStyle(contentLayer, "z-index", "22");
        adapter->setStyle(contentLayer, "pointer-events", "none");

        if (owner->content)
        {
            owner->layoutContentIfNeeded(ctx, fontCache);
            owner->content->render(ctx, fontCache);
        }
    }
#endif
  };

  std::shared_ptr<DialogSurface> dialogSurface_;

public:
  bool isOpen = false;
  WidgetPtr content;

  int dialogWidth = 400;
  int dialogHeight = 300;
  Color overlayColor = Color::fromRGBA(0, 0, 0, 128);
  Color dialogBgColor = Color::fromRGBA(255, 255, 255, 255);
  Color dialogBorderColor = Color::fromRGBA(200, 200, 200, 255);
  int dialogBorderRadius = 8;
  int dialogPadding = 24;

  std::function<void()> onClose;
  bool closeOnClickOutside = true;
  bool closeOnEscape = true;

  DialogWidget()
  {
    hasBackground = false;
    dialogSurface_ = std::make_shared<DialogSurface>();
    dialogSurface_->owner = this;
  }

  void onDetach() override
  {
    if (isOpen)
      close();
    Widget::onDetach();
  }

  // ── Builder API ───────────────────────────────────────────────────────
  std::shared_ptr<DialogWidget> setContent(WidgetPtr child)
  {
    content = child;
    if (content)
      content->parent = this;
    contentDirty_ = true;
    return self_();
  }
  std::shared_ptr<DialogWidget> setSize(int w, int h)
  {
    dialogWidth = w;
    dialogHeight = h;
    contentDirty_ = true;
    return self_();
  }
  std::shared_ptr<DialogWidget> setCloseOnClickOutside(bool value)
  {
    closeOnClickOutside = value;
    return self_();
  }
  std::shared_ptr<DialogWidget> setCloseOnEscape(bool value)
  {
    closeOnEscape = value;
    return self_();
  }
  std::shared_ptr<DialogWidget> setOnClose(std::function<void()> cb)
  {
    onClose = cb;
    return self_();
  }
  std::shared_ptr<DialogWidget> setOverlayColor(Color c)
  {
    overlayColor = c;
    return self_();
  }

  // ── Normal Widget — zero-size anchor, purely so onDetach() fires ──────
  void computeLayout(GraphicsContext &, const BoxConstraints &, FontCache &) override
  {
    width = 0;
    height = 0;
    needsLayout = false;
  }
  void positionChildren(int, int, int, int) override {}
  void render(GraphicsContext &, FontCache &) override { needsPaint = false; }

  // ── Open / Close ──────────────────────────────────────────────────────
  void open()
  {
    if (isOpen)
      return;

    auto *ui = FluxUI::getCurrentInstance();
    if (!ui)
      return;

    isOpen = true;
    contentDirty_ = true;

    auto sz = ui->getClientSize();
    winW_ = sz.width;
    winH_ = sz.height;
    dialogX_ = (winW_ - dialogWidth) / 2;
    dialogY_ = (winH_ - dialogHeight) / 2;
    // content's real visual container, while the dialog is open, is the
    // FLOATING overlay surface — not the inert zero-size anchor stub
    // sitting wherever this DialogWidget happens to live in the normal
    // tree (see class comment above and setContent()). content->x/y are
    // always real absolute overlay coordinates (set in
    // layoutContentIfNeeded below); DOM positioning needs
    // content->parent to actually be its true visual ancestor so that
    // ancestor-relative math resolves correctly — dialogSurface_ is
    // parentless (registered via the overlay system, not addChild'd),
    // so this makes content's whole subtree root-absolute, matching its
    // real coordinates. Reset back to `this` in close() below since
    // content->parent = this is setContent()'s original, and nothing
    // should observe a stale dialogSurface_ pointer while closed.
    if (content)
        content->parent = dialogSurface_.get();


    dialogSurface_->x = 0;
    dialogSurface_->y = 0;
    dialogSurface_->width = winW_;
    dialogSurface_->height = winH_;
    // A dialog owns the whole screen while open — nothing below it should
    // see clicks, hover, or keys.
    ui->showOverlay(dialogSurface_.get(), /*zIndex=*/200,
                    /*modal=*/true, /*blocksHoverBelow=*/true,
                    /*capturesKeyboard=*/true);
    popupShown_ = true;
  }

  void close()
  {
    if (!isOpen)
      return;
    isOpen = false;

    auto *ui = FluxUI::getCurrentInstance();
    if (ui)
    {
      Widget *focused = ui->getFocusedWidget();
      if (focused && content && isDescendantOf(focused, content.get()))
        ui->setFocus(nullptr);
      if (popupShown_)
        ui->hideOverlay(dialogSurface_.get());
    }
    popupShown_ = false;

#if (defined(__EMSCRIPTEN__) && defined(FLUX_WEB_RENDERER_DOM)) || defined(FLUX_SSR)
    // DialogSurface's own slots ("scrim"/"box") — same eviction pattern
    // as every other overlay widget fixed so far.
    fluxDomEvictWidget(dialogSurface_.get());
    // content is NEVER reached by Widget::onDetach()'s normal child-walk
    // (it's manually rendered, not addChild'd into `children` — see
    // setContent()), so its own DOM nodes — and every node any of ITS
    // descendants created — were never evicted through the usual
    // mechanism at all, even before this dialog had any DOM awareness.
    // Walk the subtree explicitly here instead.
    if (content)
    {
        evictWidgetTreeDom_(content.get());
        content->parent = this; // restore setContent()'s original parent
    }
#endif


    if (onClose)
      onClose();
  }

private:
  friend class DialogSurface;
  std::shared_ptr<DialogWidget> self_()
  {
    return std::static_pointer_cast<DialogWidget>(shared_from_this());
  }

#if (defined(__EMSCRIPTEN__) && defined(FLUX_WEB_RENDERER_DOM)) || defined(FLUX_SSR)
  static void evictWidgetTreeDom_(Widget *w)
  {
      if (!w) return;
      fluxDomEvictWidget(w);
      for (auto &child : w->children)
          evictWidgetTreeDom_(child.get());
  }
#endif

  void layoutContentIfNeeded(GraphicsContext &ctx, FontCache &fontCache)
  {
    int contentX = dialogX_ + dialogPadding;
    int contentY = dialogY_ + dialogPadding;
    int contentW = dialogWidth - dialogPadding * 2;
    int contentH = dialogHeight - dialogPadding * 2;

    if (contentDirty_ || content->needsLayout)
    {
      content->computeLayout(ctx, BoxConstraints::tight(contentW, contentH), fontCache);
      contentDirty_ = false;
    }

    content->x = contentX;
    content->y = contentY;
    content->positionChildren(
        contentX + content->paddingLeft,
        contentY + content->paddingTop,
        content->width - content->paddingLeft - content->paddingRight,
        content->height - content->paddingTop - content->paddingBottom);
  }

  bool dispatchContentMouseDown(int mx, int my)
  {
    auto *ui = FluxUI::getCurrentInstance();
    Widget *toFocus = nullptr;

    bool handled = findAndHandleMouseEvent(
        content.get(), mx, my,
        [mx, my, &toFocus](Widget *w)
        {
          bool h = w->handleMouseDown(mx, my);
          if (!h && w->onClick && mx >= w->x && mx < w->x + w->width &&
              my >= w->y && my < w->y + w->height)
          {
            w->onClick();
            h = true;
          }
          if (h && w->isFocusable)
            toFocus = w;
          return h;
        });

    if (handled)
    {
      if (toFocus && ui)
        ui->setFocus(toFocus);
      return true;
    }

    Widget *clicked = findWidgetAt(content.get(), mx, my);
    if (clicked)
    {
      if (clicked->onClick)
      {
        clicked->onClick();
        return true;
      }
      if (clicked->isFocusable)
      {
        clicked->handleMouseDown(mx, my);
        if (ui)
          ui->setFocus(clicked);
        return true;
      }
    }
    return false;
  }

  bool isDescendantOf(Widget *candidate, Widget *subtreeRoot)
  {
    Widget *current = candidate;
    while (current)
    {
      if (current == subtreeRoot)
        return true;
      current = current->parent;
    }
    return false;
  }
};




// ============================================================================
// FACTORY FUNCTIONS
// ============================================================================

using DialogWidgetPtr = std::shared_ptr<DialogWidget>;



inline DialogWidgetPtr Dialog(WidgetPtr content = nullptr)
{
  auto w = std::make_shared<DialogWidget>();
  if (content)
    w->setContent(content);
  return w;
}


#endif // FLUX_DIALOG_HPP