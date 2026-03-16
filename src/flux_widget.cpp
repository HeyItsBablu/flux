#include "flux_widget.hpp"

// ============================================================================
// Widget::measureText
// ============================================================================

void Widget::measureText(HDC hdc, FontCache &fontCache) {
  if (text.empty()) {
    width = 0;
    height = 0;
    return;
  }
  HFONT hFont = fontCache.getFont(fontFamily, fontSize, fontWeight);
  HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);

  int wlen = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
  std::wstring wtext(wlen, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, wtext.data(), wlen);

  SIZE size;
  GetTextExtentPoint32W(hdc, wtext.c_str(), (int)wtext.size() - 1, &size);

  if (autoWidth)
    width = size.cx + paddingLeft + paddingRight;
  if (autoHeight)
    height = size.cy + paddingTop + paddingBottom;

  SelectObject(hdc, hOldFont);
}

// ============================================================================
// Widget::renderText
// ============================================================================

void Widget::renderText(HDC hdc, FontCache &fontCache, UINT format) {
  if (text.empty())
    return;

  SetTextColor(hdc, getCurrentTextColor());
  SetBkMode(hdc, TRANSPARENT);

  HFONT hFont = fontCache.getFont(fontFamily, fontSize, fontWeight);
  HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);

  RECT textRect = {x + paddingLeft, y + paddingTop, x + width - paddingRight,
                   y + height - paddingBottom};

  int wlen = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
  std::wstring wtext(wlen, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, wtext.data(), wlen);

  DrawTextW(hdc, wtext.c_str(), -1, &textRect, format);
  SelectObject(hdc, hOldFont);
}

// ============================================================================
// Widget::computeLayout
// ============================================================================

void Widget::computeLayout(HDC hdc, const BoxConstraints &constraints,
                           FontCache &fontCache) {
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

void Widget::positionChildren(int contentX, int contentY, int contentWidth,
                               int contentHeight) {
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

void Widget::render(HDC hdc, FontCache &fontCache) {
  if (!visible)
    return;
  if (hasBackground)
    drawRoundedRectangle(hdc);
  for (auto &child : children)
    child->render(hdc, fontCache);
  FluxOverflow::render(hdc, overflow, x, y, width, height);
  needsPaint = false;
}

// ============================================================================
// Widget::drawRoundedRectangle
// ============================================================================

void Widget::drawRoundedRectangle(HDC hdc) {
  COLORREF bgColor = getCurrentBackgroundColor();
  COLORREF bdColor = getCurrentBorderColor();

  Gdiplus::Graphics g(hdc);
  g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

  auto makeRoundedPath = [&](Gdiplus::GraphicsPath &path, int left, int top,
                             int w, int h, int r) {
    int d = r * 2;
    path.AddArc(left, top, d, d, 180, 90);
    path.AddArc(left + w - d, top, d, d, 270, 90);
    path.AddArc(left + w - d, top + h - d, d, d, 0, 90);
    path.AddArc(left, top + h - d, d, d, 90, 90);
    path.CloseFigure();
  };

  if (hasBackground) {
    Gdiplus::Color fillColor(backgroundAlpha, GetRValue(bgColor),
                             GetGValue(bgColor), GetBValue(bgColor));
    Gdiplus::SolidBrush brush(fillColor);
    if (borderRadius > 0) {
      Gdiplus::GraphicsPath path;
      makeRoundedPath(path, x, y, width, height, borderRadius);
      g.FillPath(&brush, &path);
    } else {
      Gdiplus::RectF rect((Gdiplus::REAL)x, (Gdiplus::REAL)y,
                          (Gdiplus::REAL)width, (Gdiplus::REAL)height);
      g.FillRectangle(&brush, rect);
    }
  }

  if (hasBorder) {
    Gdiplus::Color strokeColor(borderAlpha, GetRValue(bdColor),
                               GetGValue(bdColor), GetBValue(bdColor));
    Gdiplus::Pen pen(strokeColor, (Gdiplus::REAL)borderWidth);
    if (borderRadius > 0) {
      Gdiplus::GraphicsPath path;
      makeRoundedPath(path, x, y, width, height, borderRadius);
      g.DrawPath(&pen, &path);
    } else {
      Gdiplus::RectF rect((Gdiplus::REAL)x, (Gdiplus::REAL)y,
                          (Gdiplus::REAL)width, (Gdiplus::REAL)height);
      g.DrawRectangle(&pen, rect);
    }
  }
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