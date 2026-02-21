#ifndef FLUX_LAYOUT_HPP
#define FLUX_LAYOUT_HPP

#include "flux_core.hpp"
#include "flux_display.hpp"
#include "flux_state.hpp"

#include <iostream>

// ============================================================================
// CONCRETE WIDGET CLASSES
// ============================================================================

// --- Column Widget ---
class ColumnWidget : public Widget {
public:
  void computeLayout(HDC hdc, const BoxConstraints &constraints,
                     FontCache &fontCache) override {

    // Intersect incoming constraints with our own min/max, then strip padding.
    BoxConstraints self = selfConstraints(constraints);
    BoxConstraints content = contentConstraints(self);

    int totalFlex = 0;
    int fixedHeight = 0;
    int visibleChildren = static_cast<int>(children.size());

    // -----------------------------------------------------------------------
    // PASS 1: Measure non-flex children with the full content width but
    //         unconstrained height so they can report their natural size.
    // -----------------------------------------------------------------------
    for (auto &child : children) {
      if (child->isExpanded()) {
        totalFlex += child->flex;
      } else {
        BoxConstraints childConstraints(0, content.maxWidth, 0,
                                        content.maxHeight);
        child->computeLayout(hdc, childConstraints, fontCache);
        fixedHeight += child->height + child->marginTop + child->marginBottom;
      }
    }

    if (visibleChildren > 1)
      fixedHeight += spacing * (visibleChildren - 1);

    // -----------------------------------------------------------------------
    // PASS 2: Distribute remaining height to flex children.
    // -----------------------------------------------------------------------
    int remainingHeight = max(0, content.maxHeight - fixedHeight);

    if (totalFlex > 0 && remainingHeight > 0) {
      int allocatedHeight = 0;
      Widget *lastFlexChild = nullptr;

      for (auto &child : children) {
        if (child->isExpanded()) {
          lastFlexChild = child.get();
          int expandedH = (remainingHeight * child->flex) / totalFlex;
          allocatedHeight += expandedH;

          child->autoWidth = false;
          child->autoHeight = false;
          child->width = content.maxWidth;
          child->height = expandedH;

          child->computeLayout(
              hdc, BoxConstraints::tight(content.maxWidth, expandedH),
              fontCache);
        }
      }

      // Distribute rounding remainder to the last flex child.
      int leftover = remainingHeight - allocatedHeight;
      if (lastFlexChild && leftover > 0)
        lastFlexChild->height += leftover;
    }

    // -----------------------------------------------------------------------
    // PASS 3: Determine our own size.
    // -----------------------------------------------------------------------
    int totalHeight = 0;
    int maxChildWidth = 0;

    for (size_t i = 0; i < children.size(); ++i) {
      auto &child = children[i];
      totalHeight += child->height + child->marginTop + child->marginBottom;
      maxChildWidth = max(maxChildWidth, child->width);
      if (i + 1 < children.size())
        totalHeight += spacing;
    }

    int finalW = self.clampWidth(
        autoWidth ? maxChildWidth + paddingLeft + paddingRight : width);
    int finalH = self.clampHeight(
        autoHeight ? totalHeight + paddingTop + paddingBottom : height);

    // ── Overflow detection ──────────────────────────────────────────────────
    overflow.reset();
    if (!autoHeight) {
      overflow = detectColumnOverflow(totalHeight + paddingTop + paddingBottom,
                                      finalH);
      if (overflow.hasOverflow())
        FluxOverflow::logWarning("Column", overflow, id);
    }

    width = finalW;
    height = finalH;
    needsLayout = false;
  }

  void positionChildren(int contentX, int contentY, int contentWidth,
                        int contentHeight) override {
    int totalChildHeight = 0;
    for (auto &child : children)
      totalChildHeight +=
          child->height + child->marginTop + child->marginBottom;
    totalChildHeight +=
        spacing * (children.empty() ? 0 : (int)children.size() - 1);

    int currentY = contentY;

    if (mainAxisAlignment == MainAxisAlignment::Center)
      currentY += (contentHeight - totalChildHeight) / 2;
    else if (mainAxisAlignment == MainAxisAlignment::End)
      currentY += contentHeight - totalChildHeight;

    for (size_t i = 0; i < children.size(); ++i) {
      auto &child = children[i];
      int childX = contentX + child->marginLeft;

      if (crossAlignment == Alignment::Center)
        childX = contentX + (contentWidth - child->width) / 2;
      else if (crossAlignment == Alignment::End)
        childX = contentX + contentWidth - child->width - child->marginRight;
      else if (crossAlignment == Alignment::Stretch)
        child->width = contentWidth - child->marginLeft - child->marginRight;

      child->x = childX;
      child->y = currentY + child->marginTop;

      child->positionChildren(
          child->x + child->paddingLeft, child->y + child->paddingTop,
          child->width - child->paddingLeft - child->paddingRight,
          child->height - child->paddingTop - child->paddingBottom);

      currentY += child->height + child->marginTop + child->marginBottom +
                  (i + 1 < children.size() ? spacing : 0);
    }
  }

  // ------------------------------------------------------------------
  // Fluent setters
  // ------------------------------------------------------------------

  std::shared_ptr<ColumnWidget> setAlignment(Alignment align) {
    if (alignment != align) {
      alignment = align;
      markNeedsLayout();
    }
    return std::static_pointer_cast<ColumnWidget>(shared_from_this());
  }
  std::shared_ptr<ColumnWidget> setCrossAlignment(Alignment align) {
    if (crossAlignment != align) {
      crossAlignment = align;
      markNeedsLayout();
    }
    return std::static_pointer_cast<ColumnWidget>(shared_from_this());
  }
  std::shared_ptr<ColumnWidget> setMainAxisAlignment(MainAxisAlignment align) {
    if (mainAxisAlignment != align) {
      mainAxisAlignment = align;
      markNeedsLayout();
    }
    return std::static_pointer_cast<ColumnWidget>(shared_from_this());
  }
  std::shared_ptr<ColumnWidget> setSpacing(int s) {
    if (spacing != s) {
      spacing = s;
      markNeedsLayout();
    }
    return std::static_pointer_cast<ColumnWidget>(shared_from_this());
  }
  std::shared_ptr<ColumnWidget> setFlex(int f) {
    if (flex != f) {
      flex = f;
      markNeedsLayout();
    }
    return std::static_pointer_cast<ColumnWidget>(shared_from_this());
  }
  std::shared_ptr<ColumnWidget> setWidth(int w) {
    width = w;
    autoWidth = false;
    markNeedsLayout();
    return std::static_pointer_cast<ColumnWidget>(shared_from_this());
  }
  std::shared_ptr<ColumnWidget> setHeight(int h) {
    height = h;
    autoHeight = false;
    markNeedsLayout();
    return std::static_pointer_cast<ColumnWidget>(shared_from_this());
  }
  std::shared_ptr<ColumnWidget> setPadding(int p) {
    paddingLeft = paddingRight = paddingTop = paddingBottom = p;
    markNeedsLayout();
    return std::static_pointer_cast<ColumnWidget>(shared_from_this());
  }
  std::shared_ptr<ColumnWidget> setBackgroundColor(COLORREF color) {
    backgroundColor = color;
    hasBackground = true;
    markNeedsPaint();
    return std::static_pointer_cast<ColumnWidget>(shared_from_this());
  }
  std::shared_ptr<ColumnWidget> setBorderRadius(int r) {
    if (borderRadius != r) {
      borderRadius = r;
      markNeedsPaint();
    }
    return std::static_pointer_cast<ColumnWidget>(shared_from_this());
  }
  std::shared_ptr<ColumnWidget> setMinWidth(int w) {
    minWidth = w;
    markNeedsLayout();
    return std::static_pointer_cast<ColumnWidget>(shared_from_this());
  }
};

// --- Row Widget ---
class RowWidget : public Widget {
public:
  void computeLayout(HDC hdc, const BoxConstraints &constraints,
                     FontCache &fontCache) override {

    BoxConstraints self = selfConstraints(constraints);
    BoxConstraints content = contentConstraints(self);

    int totalFlex = 0;
    int fixedWidth = 0;

    // -----------------------------------------------------------------------
    // PASS 1: Measure non-flex children.
    // -----------------------------------------------------------------------
    for (auto &child : children) {
      if (child->isExpanded()) {
        totalFlex += child->flex;
      } else {
        BoxConstraints childConstraints(0, content.maxWidth, 0,
                                        content.maxHeight);
        child->computeLayout(hdc, childConstraints, fontCache);
        fixedWidth += child->width + child->marginLeft + child->marginRight;
      }
    }

    if (!children.empty())
      fixedWidth += spacing * ((int)children.size() - 1);

    // -----------------------------------------------------------------------
    // PASS 2: Distribute remaining width to flex children.
    // -----------------------------------------------------------------------
    int remainingWidth = max(0, content.maxWidth - fixedWidth);

    if (totalFlex > 0 && remainingWidth > 0) {
      int allocated = 0;
      Widget *lastFlex = nullptr;

      for (auto &child : children) {
        if (child->isExpanded()) {
          lastFlex = child.get();
          int expandedW = (remainingWidth * child->flex) / totalFlex;
          allocated += expandedW;

          child->autoWidth = false;
          child->autoHeight = false;
          child->width = expandedW;
          child->height = content.maxHeight;

          child->computeLayout(
              hdc, BoxConstraints::tight(expandedW, content.maxHeight),
              fontCache);
        }
      }

      int leftover = remainingWidth - allocated;
      if (lastFlex && leftover > 0)
        lastFlex->width += leftover;
    }

    // -----------------------------------------------------------------------
    // PASS 3: Determine our own size.
    // -----------------------------------------------------------------------
    int totalWidth = 0;
    int maxChildHeight = 0;

    for (size_t i = 0; i < children.size(); ++i) {
      auto &child = children[i];
      totalWidth += child->width + child->marginLeft + child->marginRight;
      maxChildHeight = max(maxChildHeight, child->height + child->marginTop +
                                               child->marginBottom);
      if (i + 1 < children.size())
        totalWidth += spacing;
    }

    int finalW = self.clampWidth(
        autoWidth ? totalWidth + paddingLeft + paddingRight : width);
    int finalH = self.clampHeight(
        autoHeight ? maxChildHeight + paddingTop + paddingBottom : height);

    // ── Overflow detection ──────────────────────────────────────────────────
    overflow.reset();
    if (!autoWidth) {
      overflow =
          detectRowOverflow(totalWidth + paddingLeft + paddingRight, finalW);
      if (overflow.hasOverflow())
        FluxOverflow::logWarning("Row", overflow, id);
    }

    width = finalW;
    height = finalH;
    needsLayout = false;
  }

  void positionChildren(int contentX, int contentY, int contentWidth,
                        int contentHeight) override {
    int totalChildWidth = 0;
    for (auto &child : children)
      totalChildWidth += child->width + child->marginLeft + child->marginRight;
    totalChildWidth +=
        spacing * (children.empty() ? 0 : (int)children.size() - 1);

    int currentX = contentX;

    if (mainAxisAlignment == MainAxisAlignment::Center)
      currentX += (contentWidth - totalChildWidth) / 2;
    else if (mainAxisAlignment == MainAxisAlignment::End)
      currentX += contentWidth - totalChildWidth;

    for (size_t i = 0; i < children.size(); ++i) {
      auto &child = children[i];
      int childY = contentY + child->marginTop;

      if (crossAlignment == Alignment::Center)
        childY = contentY + (contentHeight - child->height) / 2;
      else if (crossAlignment == Alignment::End)
        childY = contentY + contentHeight - child->height - child->marginBottom;
      else if (crossAlignment == Alignment::Stretch)
        child->height = contentHeight - child->marginTop - child->marginBottom;

      child->x = currentX + child->marginLeft;
      child->y = childY;

      child->positionChildren(
          child->x + child->paddingLeft, child->y + child->paddingTop,
          child->width - child->paddingLeft - child->paddingRight,
          child->height - child->paddingTop - child->paddingBottom);

      currentX += child->width + child->marginLeft + child->marginRight +
                  (i + 1 < children.size() ? spacing : 0);
    }
  }

  // ------------------------------------------------------------------
  // Fluent setters
  // ------------------------------------------------------------------

  std::shared_ptr<RowWidget> setAlignment(Alignment align) {
    if (alignment != align) {
      alignment = align;
      markNeedsLayout();
    }
    return std::static_pointer_cast<RowWidget>(shared_from_this());
  }
  std::shared_ptr<RowWidget> setCrossAlignment(Alignment align) {
    if (crossAlignment != align) {
      crossAlignment = align;
      markNeedsLayout();
    }
    return std::static_pointer_cast<RowWidget>(shared_from_this());
  }
  std::shared_ptr<RowWidget> setMainAxisAlignment(MainAxisAlignment align) {
    if (mainAxisAlignment != align) {
      mainAxisAlignment = align;
      markNeedsLayout();
    }
    return std::static_pointer_cast<RowWidget>(shared_from_this());
  }
  std::shared_ptr<RowWidget> setSpacing(int s) {
    if (spacing != s) {
      spacing = s;
      markNeedsLayout();
    }
    return std::static_pointer_cast<RowWidget>(shared_from_this());
  }
  std::shared_ptr<RowWidget> setFlex(int f) {
    if (flex != f) {
      flex = f;
      markNeedsLayout();
    }
    return std::static_pointer_cast<RowWidget>(shared_from_this());
  }
  std::shared_ptr<RowWidget> setWidth(int w) {
    width = w;
    autoWidth = false;
    markNeedsLayout();
    return std::static_pointer_cast<RowWidget>(shared_from_this());
  }
  std::shared_ptr<RowWidget> setHeight(int h) {
    height = h;
    autoHeight = false;
    markNeedsLayout();
    return std::static_pointer_cast<RowWidget>(shared_from_this());
  }
  std::shared_ptr<RowWidget> setPadding(int p) {
    paddingLeft = paddingRight = paddingTop = paddingBottom = p;
    markNeedsLayout();
    return std::static_pointer_cast<RowWidget>(shared_from_this());
  }
  std::shared_ptr<RowWidget> setBackgroundColor(COLORREF color) {
    backgroundColor = color;
    hasBackground = true;
    markNeedsPaint();
    return std::static_pointer_cast<RowWidget>(shared_from_this());
  }
};

// --- Container Widget ---
class ContainerWidget : public Widget {
public:
  void computeLayout(HDC hdc, const BoxConstraints &constraints,
                     FontCache &fontCache) override {

    BoxConstraints self = selfConstraints(constraints);

    if (!children.empty()) {
      // Content constraints: remove our padding from the available space.
      // If we have a fixed size, use that as the maximum for the child.
      int maxW = autoWidth ? self.maxWidth : width;
      int maxH = autoHeight ? self.maxHeight : height;
      BoxConstraints childConstraints =
          BoxConstraints(0, maxW, 0, maxH)
              .deflate(paddingLeft + paddingRight, paddingTop + paddingBottom);

      children[0]->computeLayout(hdc, childConstraints, fontCache);

      if (autoWidth)
        width =
            self.clampWidth(children[0]->width + paddingLeft + paddingRight);
      if (autoHeight)
        height =
            self.clampHeight(children[0]->height + paddingTop + paddingBottom);
    } else {
      width = autoWidth ? self.clampWidth(0) : self.clampWidth(width);
      height = autoHeight ? self.clampHeight(0) : self.clampHeight(height);
    }

    // ── Overflow detection ──────────────────────────────────────────────────
    overflow.reset();
    int contentW = width - paddingLeft - paddingRight;
    int contentH = height - paddingTop - paddingBottom;

    // Only check an axis that the user explicitly fixed.
    // Auto-sized axes grow to fit, so overflow is impossible on them.
    if (!autoWidth && children[0]->width > contentW + kOverflowThreshold) {
      overflow.overflowX = children[0]->width - contentW;
      overflow.axis = OverflowAxis::Horizontal;
    }
    if (!autoHeight && children[0]->height > contentH + kOverflowThreshold) {
      overflow.overflowY = children[0]->height - contentH;
      overflow.axis = (overflow.axis == OverflowAxis::Horizontal)
                          ? OverflowAxis::Both
                          : OverflowAxis::Vertical;
    }
    if (overflow.hasOverflow())
      FluxOverflow::logWarning("Container", overflow, id);

    needsLayout = false;
  }

  void positionChildren(int contentX, int contentY, int contentWidth,
                        int contentHeight) override {
    if (!children.empty()) {
      auto &child = children[0];
      child->x = contentX;
      child->y = contentY;
      child->positionChildren(
          child->x + child->paddingLeft, child->y + child->paddingTop,
          child->width - child->paddingLeft - child->paddingRight,
          child->height - child->paddingTop - child->paddingBottom);
    }
  }

  // ------------------------------------------------------------------
  // Fluent setters (unchanged from original)
  // ------------------------------------------------------------------

  std::shared_ptr<ContainerWidget> setHoverBackgroundColor(COLORREF color) {
    hoverBackgroundColor = color;
    hasHoverBackground = true;
    markNeedsPaint();
    return std::static_pointer_cast<ContainerWidget>(shared_from_this());
  }
  std::shared_ptr<ContainerWidget> setHoverBorderColor(COLORREF color) {
    hoverBorderColor = color;
    hasHoverBorderColor = true;
    markNeedsPaint();
    return std::static_pointer_cast<ContainerWidget>(shared_from_this());
  }
  std::shared_ptr<ContainerWidget> setBorderWidth(int w) {
    if (borderWidth != w) {
      borderWidth = w;
      hasBorder = true;
      markNeedsLayout();
    }
    return std::static_pointer_cast<ContainerWidget>(shared_from_this());
  }
  std::shared_ptr<ContainerWidget> setBorderRadius(int r) {
    if (borderRadius != r) {
      borderRadius = r;
      markNeedsPaint();
    }
    return std::static_pointer_cast<ContainerWidget>(shared_from_this());
  }
  std::shared_ptr<ContainerWidget> setBackgroundColor(COLORREF color) {
    backgroundColor = color;
    hasBackground = true;
    markNeedsPaint();
    return std::static_pointer_cast<ContainerWidget>(shared_from_this());
  }
  template <typename T>
  std::shared_ptr<ContainerWidget>
  setBackgroundColor(State<T> &state, COLORREF trueColor, COLORREF falseColor) {
    backgroundColor = state.get() ? trueColor : falseColor;
    hasBackground = true;
    state.bindProperty(
        shared_from_this(),
        [trueColor, falseColor](Widget *w, const T &val) {
          w->backgroundColor = val ? trueColor : falseColor;
          w->hasBackground = true;
        },
        false);
    return std::static_pointer_cast<ContainerWidget>(shared_from_this());
  }
  std::shared_ptr<ContainerWidget> setBorderColor(COLORREF color) {
    borderColor = color;
    hasBorder = true;
    markNeedsPaint();
    return std::static_pointer_cast<ContainerWidget>(shared_from_this());
  }
  std::shared_ptr<ContainerWidget> setWidth(int w) {
    if (width != w) {
      width = w;
      autoWidth = false;
      markNeedsLayout();
    }
    return std::static_pointer_cast<ContainerWidget>(shared_from_this());
  }
  std::shared_ptr<ContainerWidget> setHeight(int h) {
    if (height != h) {
      height = h;
      autoHeight = false;
      markNeedsLayout();
    }
    return std::static_pointer_cast<ContainerWidget>(shared_from_this());
  }
  std::shared_ptr<ContainerWidget> setMinWidth(int w) {
    if (minWidth != w) {
      minWidth = w;
      markNeedsLayout();
    }
    return std::static_pointer_cast<ContainerWidget>(shared_from_this());
  }
  std::shared_ptr<ContainerWidget> setMinHeight(int h) {
    if (minHeight != h) {
      minHeight = h;
      markNeedsLayout();
    }
    return std::static_pointer_cast<ContainerWidget>(shared_from_this());
  }
  std::shared_ptr<ContainerWidget> setMaxWidth(int w) {
    if (maxWidth != w) {
      maxWidth = w;
      markNeedsLayout();
    }
    return std::static_pointer_cast<ContainerWidget>(shared_from_this());
  }
  std::shared_ptr<ContainerWidget> setMaxHeight(int h) {
    if (maxHeight != h) {
      maxHeight = h;
      markNeedsLayout();
    }
    return std::static_pointer_cast<ContainerWidget>(shared_from_this());
  }
  std::shared_ptr<ContainerWidget> setFlex(int f) {
    if (flex != f) {
      flex = f;
      markNeedsLayout();
    }
    return std::static_pointer_cast<ContainerWidget>(shared_from_this());
  }
  std::shared_ptr<ContainerWidget> setPadding(int p) {
    padding = p;
    paddingLeft = paddingRight = paddingTop = paddingBottom = p;
    markNeedsLayout();
    return std::static_pointer_cast<ContainerWidget>(shared_from_this());
  }
  std::shared_ptr<ContainerWidget> setPaddingAll(int left, int top, int right,
                                                 int bottom) {
    paddingLeft = left;
    paddingTop = top;
    paddingRight = right;
    paddingBottom = bottom;
    padding = -1;
    markNeedsLayout();
    return std::static_pointer_cast<ContainerWidget>(shared_from_this());
  }
  std::shared_ptr<ContainerWidget> setMargin(int m) {
    margin = m;
    marginLeft = marginRight = marginTop = marginBottom = m;
    markNeedsLayout();
    return std::static_pointer_cast<ContainerWidget>(shared_from_this());
  }
  std::shared_ptr<ContainerWidget> setMarginAll(int left, int top, int right,
                                                int bottom) {
    marginLeft = left;
    marginTop = top;
    marginRight = right;
    marginBottom = bottom;
    margin = -1;
    markNeedsLayout();
    return std::static_pointer_cast<ContainerWidget>(shared_from_this());
  }
  std::shared_ptr<ContainerWidget> setOnHover(HoverHandler handler) {
    onHover = handler;
    return std::static_pointer_cast<ContainerWidget>(shared_from_this());
  }
  std::shared_ptr<ContainerWidget> setBackgroundAlpha(BYTE alpha) {
    backgroundAlpha = alpha;
    markNeedsPaint();
    return std::static_pointer_cast<ContainerWidget>(shared_from_this());
  }
  std::shared_ptr<ContainerWidget> setBorderAlpha(BYTE alpha) {
    borderAlpha = alpha;
    markNeedsPaint();
    return std::static_pointer_cast<ContainerWidget>(shared_from_this());
  }
};

// --- Center Widget ---
class CenterWidget : public Widget {
public:
  void computeLayout(HDC hdc, const BoxConstraints &constraints,
                     FontCache &fontCache) override {
    BoxConstraints self = selfConstraints(constraints);

    // Take all available space (like a tight constraint on max).
    if (autoWidth)
      width = self.maxWidth;
    if (autoHeight)
      height = self.maxHeight;

    BoxConstraints childConstraints = contentConstraints(self);

    if (!children.empty())
      children[0]->computeLayout(hdc, childConstraints, fontCache);

    needsLayout = false;
  }

  void positionChildren(int contentX, int contentY, int contentWidth,
                        int contentHeight) override {
    if (!children.empty()) {
      auto &child = children[0];
      child->x = contentX + (contentWidth - child->width) / 2;
      child->y = contentY + (contentHeight - child->height) / 2;
      child->positionChildren(
          child->x + child->paddingLeft, child->y + child->paddingTop,
          child->width - child->paddingLeft - child->paddingRight,
          child->height - child->paddingTop - child->paddingBottom);
    }
  }
};

// --- SizedBox Widget ---
class SizedBoxWidget : public Widget {
public:
  void computeLayout(HDC hdc, const BoxConstraints &constraints,
                     FontCache &fontCache) override {
    // width/height are pre-set; clamp to constraints but respect fixed size.
    BoxConstraints self = selfConstraints(constraints);
    width = self.clampWidth(width);
    height = self.clampHeight(height);

    if (!children.empty()) {
      BoxConstraints childConstraints(0, width - paddingLeft - paddingRight, 0,
                                      height - paddingTop - paddingBottom);
      children[0]->computeLayout(hdc, childConstraints, fontCache);
    }
    needsLayout = false;
  }
};

// --- Expanded Widget ---
class ExpandedWidget : public Widget {
public:
  bool isExpanded() const override { return true; }

  void computeLayout(HDC hdc, const BoxConstraints &constraints,
                     FontCache &fontCache) override {
    // Parent (Row/Column) pre-sets width and height to the allocated flex size;
    // pass that down as tight constraints to children.
    int contentW = width - paddingLeft - paddingRight;
    int contentH = height - paddingTop - paddingBottom;

    if (!children.empty()) {
      children[0]->computeLayout(hdc, BoxConstraints::loose(contentW, contentH),
                                 fontCache);
    }

    needsLayout = false;
  }

  std::shared_ptr<ExpandedWidget> setPadding(int p) {
    paddingLeft = paddingRight = paddingTop = paddingBottom = p;
    markNeedsLayout();
    return std::static_pointer_cast<ExpandedWidget>(shared_from_this());
  }
  std::shared_ptr<ExpandedWidget> setBackgroundColor(COLORREF color) {
    backgroundColor = color;
    hasBackground = true;
    markNeedsPaint();
    return std::static_pointer_cast<ExpandedWidget>(shared_from_this());
  }
};

// ============================================================================
// WIDGET FACTORY FUNCTIONS
// ============================================================================

using ContainerWidgetPtr = std::shared_ptr<ContainerWidget>;

inline ContainerWidgetPtr Container(WidgetPtr child = nullptr) {
  auto w = std::make_shared<ContainerWidget>();
  if (child)
    w->addChild(child);
  return w;
}

using RowWidgetPtr = std::shared_ptr<RowWidget>;

template <typename... Widgets> RowWidgetPtr Row(Widgets... widgets) {
  auto w = std::make_shared<RowWidget>();
  (w->addChild(widgets), ...);
  return w;
}

using ColumnWidgetPtr = std::shared_ptr<ColumnWidget>;

template <typename... Widgets> ColumnWidgetPtr Column(Widgets... widgets) {
  auto w = std::make_shared<ColumnWidget>();
  (w->addChild(widgets), ...);
  return w;
}

inline WidgetPtr Padding(int p, WidgetPtr child) {
  auto w = std::make_shared<ContainerWidget>();
  w->padding = p;
  w->paddingLeft = w->paddingRight = w->paddingTop = w->paddingBottom = p;
  if (child)
    w->addChild(child);
  return w;
}

inline WidgetPtr Center(WidgetPtr child) {
  auto w = std::make_shared<CenterWidget>();
  w->alignment = Alignment::Center;
  if (child)
    w->addChild(child);
  return w;
}

inline WidgetPtr SizedBox(int width, int height, WidgetPtr child = nullptr) {
  auto w = std::make_shared<SizedBoxWidget>();
  w->width = width;
  w->height = height;
  w->autoWidth = false;
  w->autoHeight = false;
  if (child)
    w->addChild(child);
  return w;
}

inline WidgetPtr Expanded(WidgetPtr child, int flex = 1) {
  auto w = std::make_shared<ExpandedWidget>();
  w->flex = flex;
  if (child)
    w->addChild(child);
  return w;
}

#endif // FLUX_LAYOUT_HPP