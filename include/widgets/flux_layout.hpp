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
  void computeLayout(HDC hdc, int availableWidth, int availableHeight,
                     FontCache &fontCache) override {
    int contentWidth = availableWidth - paddingLeft - paddingRight;
    int contentHeight = availableHeight - paddingTop - paddingBottom;

    int totalFlex = 0;
    int fixedHeight = 0;

    // First pass: compute fixed-size children
    for (auto &child : children) {
      if (child->isExpanded()) {
        totalFlex += child->flex;
      } else {
        child->computeLayout(hdc, contentWidth, contentHeight, fontCache);
        fixedHeight += child->height;
      }
    }

    if (!children.empty()) {
      fixedHeight += spacing * (children.size() - 1);
    }

    // Second pass: distribute remaining space to flex children
    int remainingHeight = contentHeight - fixedHeight;
    if (totalFlex > 0 && remainingHeight > 0) {
      for (auto &child : children) {
        if (child->isExpanded()) {
          int expandedHeight = (remainingHeight * child->flex) / totalFlex;
          child->height = expandedHeight;
          child->width = contentWidth;
          child->autoHeight = false;
          child->autoWidth = false;
          child->computeLayout(hdc, contentWidth, expandedHeight, fontCache);
        }
      }
    }

    // Calculate final size
    int totalHeight = 0;
    int maxWidth = 0;

    for (size_t i = 0; i < children.size(); i++) {
      auto &child = children[i];
      if (child->width > maxWidth)
        maxWidth = child->width;
      totalHeight += child->height;
      if (i < children.size() - 1)
        totalHeight += spacing;
    }

    if (autoWidth)
      width = maxWidth + paddingLeft + paddingRight;
    if (autoHeight)
      height = totalHeight + paddingTop + paddingBottom;

    applyConstraints();
    needsLayout = false;
  }

  void positionChildren(int contentX, int contentY, int contentWidth,
                        int contentHeight) override {
    int totalChildHeight = 0;
    for (auto &child : children) {
      totalChildHeight += child->height;
    }
    totalChildHeight += spacing * (children.empty() ? 0 : children.size() - 1);

    int currentY = contentY;

    if (mainAxisAlignment == MainAxisAlignment::Center) {
      currentY += (contentHeight - totalChildHeight) / 2;
    } else if (mainAxisAlignment == MainAxisAlignment::End) {
      currentY += contentHeight - totalChildHeight;
    }

    for (size_t i = 0; i < children.size(); i++) {
      auto &child = children[i];
      int childX = contentX;

      if (crossAlignment == Alignment::Center) {
        childX = contentX + (contentWidth - child->width) / 2;
      } else if (crossAlignment == Alignment::End) {
        childX = contentX + contentWidth - child->width;
      } else if (crossAlignment == Alignment::Stretch) {
        child->width = contentWidth;
      }

      child->x = childX;
      child->y = currentY;

      child->positionChildren(
          child->x + child->paddingLeft, child->y + child->paddingTop,
          child->width - child->paddingLeft - child->paddingRight,
          child->height - child->paddingTop - child->paddingBottom);

      currentY += child->height + (i < children.size() - 1 ? spacing : 0);
    }
  }

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
};

// --- Row Widget ---
class RowWidget : public Widget {
public:
  void computeLayout(HDC hdc, int availableWidth, int availableHeight,
                     FontCache &fontCache) override {
    int contentWidth = availableWidth - paddingLeft - paddingRight;
    int contentHeight = availableHeight - paddingTop - paddingBottom;

    int totalFlex = 0;
    int fixedWidth = 0;

    // First pass: compute fixed-size children
    for (auto &child : children) {
      if (child->isExpanded()) {
        totalFlex += child->flex;
      } else {
        child->computeLayout(hdc, contentWidth, contentHeight, fontCache);
        fixedWidth += child->width;
      }
    }

    if (!children.empty()) {
      fixedWidth += spacing * (children.size() - 1);
    }

    int remainingWidth = contentWidth - fixedWidth;
    if (totalFlex > 0 && remainingWidth > 0) {
      for (auto &child : children) {
        if (child->isExpanded()) {
          int expandedWidth = (remainingWidth * child->flex) / totalFlex;
          child->width = expandedWidth;
          child->height = contentHeight;
          child->autoWidth = false;
          child->autoHeight = false;

          child->computeLayout(hdc, expandedWidth, contentHeight, fontCache);
        }
      }
    }

    int totalWidth = 0;
    int maxHeight = 0;

    for (size_t i = 0; i < children.size(); i++) {
      auto &child = children[i];
      totalWidth += child->width;
      if (child->height > maxHeight)
        maxHeight = child->height;
      if (i < children.size() - 1)
        totalWidth += spacing;
    }

    if (autoWidth)
      width = totalWidth + paddingLeft + paddingRight;
    if (autoHeight)
      height = maxHeight + paddingTop + paddingBottom;

    applyConstraints();
    needsLayout = false;
  }

  void positionChildren(int contentX, int contentY, int contentWidth,
                        int contentHeight) override {
    int totalChildWidth = 0;
    for (auto &child : children) {
      totalChildWidth += child->width;
    }
    totalChildWidth += spacing * (children.empty() ? 0 : children.size() - 1);

    int currentX = contentX;

    if (mainAxisAlignment == MainAxisAlignment::Center) {
      currentX += (contentWidth - totalChildWidth) / 2;
    } else if (mainAxisAlignment == MainAxisAlignment::End) {
      currentX += contentWidth - totalChildWidth;
    }

    for (size_t i = 0; i < children.size(); i++) {
      auto &child = children[i];
      int childY = contentY;

      if (crossAlignment == Alignment::Center) {
        childY = contentY + (contentHeight - child->height) / 2;
      } else if (crossAlignment == Alignment::End) {
        childY = contentY + contentHeight - child->height;
      } else if (crossAlignment == Alignment::Stretch) {
        child->height = contentHeight;
      }

      child->x = currentX;
      child->y = childY;

      child->positionChildren(
          child->x + child->paddingLeft, child->y + child->paddingTop,
          child->width - child->paddingLeft - child->paddingRight,
          child->height - child->paddingTop - child->paddingBottom);

      currentX += child->width + (i < children.size() - 1 ? spacing : 0);
    }
  }

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

// --- Container/Padding/Card Widgets ---
class ContainerWidget : public Widget {
public:
  void computeLayout(HDC hdc, int availableWidth, int availableHeight,
                     FontCache &fontCache) override {
    // Resolve own size first
    if (autoWidth)
      width = availableWidth;
    if (autoHeight)
      height = availableHeight;

    // Now content is derived from OUR size, not parent's offer
    int contentWidth = width - paddingLeft - paddingRight;
    int contentHeight = height - paddingTop - paddingBottom;

    if (!children.empty()) {
      children[0]->computeLayout(hdc, contentWidth, contentHeight, fontCache);

      // Only shrink-wrap if auto-sizing
      if (autoWidth)
        width = children[0]->width + paddingLeft + paddingRight;
      if (autoHeight)
        height = children[0]->height + paddingTop + paddingBottom;
    }

    applyConstraints();
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
    // Set initial value immediately
    backgroundColor = state.get() ? trueColor : falseColor;
    hasBackground = true;

    // Capture colors by value, bind to state
    state.bindProperty(
        shared_from_this(),
        [trueColor, falseColor](Widget *w, const T &val) {
          w->backgroundColor = val ? trueColor : falseColor;
          w->hasBackground = true;
        },
        false // paint only, no layout needed
    );

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

  // std::shared_ptr<ContainerWidget> setOnClick(ClickHandler handler) {
  //   onClick = handler;
  //   return std::static_pointer_cast<ContainerWidget>(shared_from_this());
  // }

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
  void computeLayout(HDC hdc, int availableWidth, int availableHeight,
                     FontCache &fontCache) override {
    // Resolve own size first
    if (autoWidth)
      width = availableWidth;
    if (autoHeight)
      height = availableHeight;

    int contentWidth = width - paddingLeft - paddingRight;
    int contentHeight = height - paddingTop - paddingBottom;

    if (!children.empty()) {
      children[0]->computeLayout(hdc, contentWidth, contentHeight, fontCache);
    }

    applyConstraints();
    needsLayout = false;
  }

  void positionChildren(int contentX, int contentY, int contentWidth,
                        int contentHeight) override {
    if (!children.empty()) {
      auto &child = children[0];

      // Center the child within the content area
      int childX = contentX + (contentWidth - child->width) / 2;
      int childY = contentY + (contentHeight - child->height) / 2;

      child->x = childX;
      child->y = childY;

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
  void computeLayout(HDC hdc, int availableWidth, int availableHeight,
                     FontCache &fontCache) override {
    if (!children.empty()) {
      children[0]->computeLayout(hdc, width - paddingLeft - paddingRight,
                                 height - paddingTop - paddingBottom,
                                 fontCache);
    }
    applyConstraints();
    needsLayout = false;
  }
};

// --- Expanded Widget ---
class ExpandedWidget : public Widget {
public:
  bool isExpanded() const override { return true; }

  void computeLayout(HDC hdc, int availableWidth, int availableHeight,
                     FontCache &fontCache) override {
    // Width/height were pre-set by Row/Column — don't shrink-wrap
    int contentWidth = width - paddingLeft - paddingRight;
    int contentHeight = height - paddingTop - paddingBottom;

    if (!children.empty()) {
      children[0]->computeLayout(hdc, contentWidth, contentHeight, fontCache);
      // Do NOT re-assign width/height here — keep the expanded size
    }

    applyConstraints();
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

inline WidgetPtr Padding(int padding, WidgetPtr child) {
  auto w = std::make_shared<ContainerWidget>();
  w->padding = padding;
  w->paddingLeft = w->paddingRight = w->paddingTop = w->paddingBottom = padding;
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



#endif // FLUX_WIDGET_LIST_HPP