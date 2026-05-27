#include "flux/flux_widget.hpp"

// ============================================================================
// Widget::measureText
// ============================================================================

void Widget::measureText(GraphicsContext &ctx, FontCache &fontCache) {
    if (text.empty()) { width = height = 0; return; }

    NativeFont font = fontCache.getFont(fontFamily, fontSize, fontWeight);
    Painter p(ctx);
    int tw = 0, th = 0;
    p.measureText(toWideString(text), font, tw, th);

    if (autoWidth)  width  = tw;   
    if (autoHeight) height = th;
}

// ============================================================================
// Widget::renderText
// ============================================================================

void Widget::renderText(GraphicsContext &ctx, FontCache &fontCache,
                        UINT format) {
  if (text.empty())
    return;

  NativeFont font = fontCache.getFont(fontFamily, fontSize, fontWeight);

  Painter(ctx).drawText(toWideString(text), x + paddingLeft, y + paddingTop,
                        width - paddingLeft - paddingRight,
                        height - paddingTop - paddingBottom, font,
                        getCurrentTextColor(), format);
}

// ============================================================================
// Widget::computeLayout
// ============================================================================

void Widget::computeLayout(GraphicsContext &ctx,
                           const BoxConstraints &constraints,
                           FontCache &fontCache) {
    if (!mounted && children.empty()) {
        if (auto built = build()) {
            addChild(built);
        }
    }

    if (!visible) { width = height = 0; needsLayout = false; return; }

    if (!children.empty()) {
        children[0]->computeLayout(ctx,
            BoxConstraints::loose(
                constraints.maxWidth  - paddingLeft - paddingRight,
                constraints.maxHeight - paddingTop  - paddingBottom),
            fontCache);
        if (autoWidth)  width  = children[0]->width  + paddingLeft + paddingRight;
        if (autoHeight) height = children[0]->height + paddingTop  + paddingBottom;
    }

    if (!autoWidth)  width  = constraints.clampWidth(width);
    if (!autoHeight) height = constraints.clampHeight(height);
    applyConstraints();
    needsLayout = false;

    if (!mounted) {
        mounted = true;
        onMount();
    }
}

// ============================================================================
// Widget::positionChildren
// ============================================================================

void Widget::positionChildren(int contentX, int contentY, int /*contentWidth*/,
                              int /*contentHeight*/) {
  if (!children.empty()) {
    auto &child = children[0];
    child->x = contentX + child->marginLeft;
    child->y = contentY + child->marginTop;
    child->positionChildren(
        child->x + child->paddingLeft, child->y + child->paddingTop,
        child->width - child->paddingLeft - child->paddingRight,
        child->height - child->paddingTop - child->paddingBottom);
  }
}



void Widget::render(GraphicsContext &ctx, FontCache &fontCache) {
  if (!visible)
    return;

  Painter painter(ctx);

  if (hasBackground)
    painter.fillRoundedRect(x, y, width, height, borderRadius,
                            getCurrentBackgroundColor());

  if (hasBorder) {
    // Clip children to our bounds so they cannot bleed over the border.
    painter.pushClipRect(x, y, width, height);
    for (auto &child : children)
      child->render(ctx, fontCache);
    painter.popClipRect();

    // Draw border after children so the stroke is always on top.
    painter.drawBorder(x, y, width, height, borderRadius,
                       getCurrentBorderColor(), borderWidth);
  } else {
    // No border — no clip needed, render children directly.
    for (auto &child : children)
      child->render(ctx, fontCache);
  }

  needsPaint = false;
}

// ============================================================================
// Widget::drawRoundedRectangle
// ============================================================================

void Widget::drawRoundedRectangle(GraphicsContext &ctx) {
  Painter painter(ctx);

  if (hasBackground)
    painter.fillRoundedRect(x, y, width, height, borderRadius,
                            getCurrentBackgroundColor());
  if (hasBorder)
    painter.drawBorder(x, y, width, height, borderRadius,
                       getCurrentBorderColor(), borderWidth);
}

// ============================================================================
// HIT TESTING
// ============================================================================

Widget *findWidgetAt(Widget *w, int x, int y) {
  if (!w || !w->visible)
    return nullptr;
  for (auto it = w->children.rbegin(); it != w->children.rend(); ++it) {
    Widget *found = findWidgetAt(it->get(), x, y);
    if (found)
      return found;
  }
  if (x >= w->x && x < w->x + w->width && y >= w->y && y < w->y + w->height)
    return w;
  return nullptr;
}

// ============================================================================
// MOUSE EVENT HELPERS
// ============================================================================

bool updateHoverStates(Widget *widget, int mouseX, int mouseY) {
  if (!widget || !widget->visible)
    return false;

  bool changed = false;
  bool isOver = (mouseX >= widget->x && mouseX < widget->x + widget->width &&
                 mouseY >= widget->y && mouseY < widget->y + widget->height);

  if (isOver) {
    changed |= widget->updateHoverState(mouseX, mouseY);
    for (auto &child : widget->children)
      changed |= updateHoverStates(child.get(), mouseX, mouseY);
  } else if (widget->isHovered) {
    widget->clearHoverState();
    changed = true;
  }

  return changed;
}