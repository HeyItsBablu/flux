#include "flux/flux_widget.hpp"

// ============================================================================
// Widget::measureText
// ============================================================================

void Widget::measureText(GraphicsContext &ctx, FontCache &fontCache) {
  if (text.empty()) {
    width = 0;
    height = 0;
    return;
  }
  HFONT hFont = fontCache.getFont(fontFamily, fontSize, fontWeight);
  HFONT hOldFont = (HFONT)SelectObject(ctx.hdc, hFont);

  int wlen = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
  std::wstring wtext(wlen, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, wtext.data(), wlen);

  SIZE size;
  GetTextExtentPoint32W(ctx.hdc, wtext.c_str(), (int)wtext.size() - 1, &size);

  if (autoWidth)
    width = size.cx + paddingLeft + paddingRight;
  if (autoHeight)
    height = size.cy + paddingTop + paddingBottom;

  SelectObject(ctx.hdc, hOldFont);
}

// ============================================================================
// Widget::renderText
// ============================================================================

void Widget::renderText(GraphicsContext &ctx, FontCache &fontCache, UINT format) {
    if (text.empty())
        return;

    // Convert UTF-8 to wide
    int wlen = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    std::wstring wtext(wlen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, wtext.data(), wlen);

    NativeFont font = fontCache.getFont(fontFamily, fontSize, fontWeight);

    Painter painter(ctx);
    painter.drawText(wtext,
                     x + paddingLeft,
                     y + paddingTop,
                     width  - paddingLeft - paddingRight,
                     height - paddingTop  - paddingBottom,
                     font,
                     getCurrentTextColor(),
                     format);
}

// ============================================================================
// Widget::computeLayout
// ============================================================================

void Widget::computeLayout(GraphicsContext &/*ctx*/, const BoxConstraints &constraints,
                           FontCache &/*fontCache*/) {
  if (!visible) {
    width = 0;
    height = 0;
    needsLayout = false;
    return;
  }
  if (!autoWidth)
    width = constraints.clampWidth(width);
  if (!autoHeight)
    height = constraints.clampHeight(height);
  applyConstraints();
  needsLayout = false;
}

// ============================================================================
// Widget::positionChildren
// ============================================================================

void Widget::positionChildren(int contentX, int contentY, 
                      int /*contentWidth*/, int /*contentHeight*/) {
  for (auto &child : children) {
    child->x = contentX + child->marginLeft;
    child->y = contentY + child->marginTop;
    child->positionChildren(
        child->x + child->paddingLeft, child->y + child->paddingTop,
        child->width - child->paddingLeft - child->paddingRight,
        child->height - child->paddingTop - child->paddingBottom);
  }
}

// ============================================================================
// Widget::render
// ============================================================================

void Widget::render(GraphicsContext &ctx, FontCache &fontCache) {
    if (!visible)
        return;

    Painter painter(ctx);

    if (hasBackground)
        painter.fillRoundedRect(x, y, width, height, borderRadius,
                                getCurrentBackgroundColor(), backgroundAlpha);

    if (hasBorder)
        painter.drawBorder(x, y, width, height, borderRadius,
                           getCurrentBorderColor(), borderWidth, borderAlpha);

    for (auto &child : children)
        child->render(ctx, fontCache);

    FluxOverflow::render(ctx.hdc, overflow, x, y, width, height);
    needsPaint = false;
}


// ============================================================================
// Widget::drawRoundedRectangle
// ============================================================================

void Widget::drawRoundedRectangle(GraphicsContext &ctx) {
    Painter painter(ctx);

    if (hasBackground)
        painter.fillRoundedRect(x, y, width, height, borderRadius,
                                getCurrentBackgroundColor(), backgroundAlpha);
    if (hasBorder)
        painter.drawBorder(x, y, width, height, borderRadius,
                           getCurrentBorderColor(), borderWidth, borderAlpha);
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
// MOUSE EVENT HELPER FUNCTIONS
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
  } else {
    if (widget->isHovered) {
      widget->clearHoverState();
      changed = true;
    }
  }
  return changed;
}