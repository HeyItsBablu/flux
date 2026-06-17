#ifndef FLUX_LAYOUT_HPP
#define FLUX_LAYOUT_HPP

#include "../flux_core.hpp"
#include "../flux_state.hpp"
#include "flux_display.hpp"

#include <iostream>

enum class MainAxisSize
{
  Max, // fill available space
  Min, // shrink-wrap children
};

class ExpandedWidget : public Widget
{
public:
  bool isExpanded() const override { return true; }

  void computeLayout(GraphicsContext &ctx, const BoxConstraints &constraints,
                     FontCache &fontCache) override
  {

    int contentW =
        std::max(0, constraints.maxWidth - paddingLeft - paddingRight);
    int contentH =
        std::max(0, constraints.maxHeight - paddingTop - paddingBottom);
    int minW = std::max(0, constraints.minWidth - paddingLeft - paddingRight);
    int minH = std::max(0, constraints.minHeight - paddingTop - paddingBottom);

    if (!children.empty())
    {
      BoxConstraints childConstraints(minW, contentW, minH, contentH);

      children[0]->computeLayout(ctx, childConstraints, fontCache);

      // Our size wraps the child, clamped to what parent allocated.
      width = std::max(constraints.minWidth,
                       children[0]->width + paddingLeft + paddingRight);
      height = std::max(constraints.minHeight,
                        children[0]->height + paddingTop + paddingBottom);
    }
    else
    {
      // No child — fill the tight axis, shrink on the loose axis.
      width = constraints.minWidth > 0 ? constraints.minWidth
                                       : constraints.maxWidth;
      height = constraints.minHeight > 0 ? constraints.minHeight
                                         : constraints.maxHeight;
    }

    needsLayout = false;
  }

  void positionChildren(int contentX, int contentY, int /*contentWidth*/,
                        int /*contentHeight*/) override
  {
    if (!children.empty())
    {
      auto &child = children[0];
      child->x = contentX + child->marginLeft;
      child->y = contentY + child->marginTop;
      child->positionChildren(
          child->x + child->paddingLeft, child->y + child->paddingTop,
          child->width - child->paddingLeft - child->paddingRight,
          child->height - child->paddingTop - child->paddingBottom);
    }
  }

  void render(GraphicsContext &ctx, FontCache &fontCache) override
  {
    if (!visible)
      return;
    Painter painter(ctx);
    if (hasBackground)
      painter.fillRoundedRect(x, y, width, height, borderRadius,
                              getCurrentBackgroundColor());
    if (hasBorder)
      painter.drawBorder(x, y, width, height, borderRadius,
                         getCurrentBorderColor(), borderWidth);
    for (auto &child : children)
      child->render(ctx, fontCache);
    needsPaint = false;
  }

  // ── Fluent setters ────────────────────────────────────────────────────────
  std::shared_ptr<ExpandedWidget> setFlex(int f)
  {
    flex = f;
    markNeedsLayout();
    return std::static_pointer_cast<ExpandedWidget>(shared_from_this());
  }
  std::shared_ptr<ExpandedWidget> setPadding(int p)
  {
    paddingLeft = paddingRight = paddingTop = paddingBottom = p;
    markNeedsLayout();
    return std::static_pointer_cast<ExpandedWidget>(shared_from_this());
  }
  std::shared_ptr<ExpandedWidget> setBackgroundColor(Color color)
  {
    backgroundColor = color;
    hasBackground = true;
    markNeedsPaint();
    return std::static_pointer_cast<ExpandedWidget>(shared_from_this());
  }
};

class RowWidget : public Widget
{
public:
  MainAxisSize mainAxisSize = MainAxisSize::Max;

  void computeLayout(GraphicsContext &ctx, const BoxConstraints &constraints,
                     FontCache &fontCache) override
  {

    BoxConstraints self = selfConstraints(constraints);
    BoxConstraints content = contentConstraints(self);

    int totalFlex = 0;
    int fixedWidth = 0;  // sum of widths of shrink-wrap non-flex children
    int shrinkWidth = 0; // same, computed in sub-pass 1a for use in 1b

    int visibleCount = 0;
    for (auto &child : children)
      if (child->visible)
        visibleCount++;
    int spacingTotal = visibleCount > 1 ? spacing * (visibleCount - 1) : 0;

    for (auto &child : children)
    {
      if (!child->visible)
        continue;
      if (child->isExpanded())
      {
        totalFlex += child->flex;
        continue;
      }
      BoxConstraints childC(0, kUnbounded, // main axis : truly unbounded
                            0,
                            content.maxHeight // cross axis : bounded by parent
      );
      child->computeLayout(ctx, childC, fontCache);

      if (child->width < kUnbounded)
      {
        // Shrink-wrap child — count it now.
        shrinkWidth += child->width + child->marginLeft + child->marginRight;
      }
    }

    int fillCount = 0;
    for (auto &child : children)
    {
      if (!child->visible || child->isExpanded())
        continue;
      if (child->width >= kUnbounded)
        fillCount++;
    }
    if (fillCount > 0)
    {
      int remaining =
          std::max(0, content.maxWidth - shrinkWidth - spacingTotal);
      int sliceW = remaining / fillCount;
      for (auto &child : children)
      {
        if (!child->visible || child->isExpanded())
          continue;
        if (child->width >= kUnbounded)
        {
          child->computeLayout(
              ctx, BoxConstraints(0, sliceW, 0, content.maxHeight), fontCache);
        }
      }
    }

    // ── Accumulate fixedWidth from all non-flex children ──────────────────
    for (auto &child : children)
    {
      if (!child->visible || child->isExpanded())
        continue;
      fixedWidth += child->width + child->marginLeft + child->marginRight;
    }

    // ── PASS 2 : flex children — divide remaining width ──────────────────
    int remainingWidth =
        std::max(0, content.maxWidth - fixedWidth - spacingTotal);

    if (totalFlex > 0)
    {
      int allocated = 0;
      Widget *lastFlex = nullptr;

      for (auto &child : children)
      {
        if (!child->visible || !child->isExpanded())
          continue;

        lastFlex = child.get();
        int sliceW = (remainingWidth * child->flex) / totalFlex;
        allocated += sliceW;

        BoxConstraints childC(sliceW, sliceW,      // tight width
                              0, content.maxHeight // loose height
        );
        child->computeLayout(ctx, childC, fontCache);
      }

      // Rounding remainder → last flex child gets any leftover pixels.
      // Again: only the constraints carry the size, no field pre-write.
      if (lastFlex)
      {
        int leftover = remainingWidth - allocated;
        if (leftover > 0)
        {
          int correctedW = lastFlex->width + leftover;
          lastFlex->computeLayout(
              ctx, BoxConstraints(correctedW, correctedW, 0, content.maxHeight),
              fontCache);
        }
      }
    }

    // ── PASS 3 : compute Row's own size ──────────────────────────────────
    int totalChildWidth = 0;
    int maxChildHeight = 0;
    bool first = true;

    for (auto &child : children)
    {
      if (!child->visible)
        continue;
      if (!first)
        totalChildWidth += spacing;
      first = false;

      totalChildWidth += child->width + child->marginLeft + child->marginRight;
      maxChildHeight =
          std::max(maxChildHeight,
                   child->height + child->marginTop + child->marginBottom);
    }

    int naturalW = totalChildWidth + paddingLeft + paddingRight;

    // Main axis sizing
    int finalW =
        (mainAxisSize == MainAxisSize::Max)
            ? self.clampWidth(content.maxWidth + paddingLeft + paddingRight)
            : self.clampWidth(naturalW);

    int naturalH = maxChildHeight + paddingTop + paddingBottom;
    int finalH;
    if (self.maxHeight >= kUnbounded)
    {
      // Unbounded parent — shrink-wrap
      finalH = self.clampHeight(naturalH);
    }
    else
    {
      // Finite parent — fill available height (Flutter default)
      finalH = self.clampHeight(self.maxHeight);
    }

    if (crossAxisAlignment == CrossAxisAlignment::Stretch)
    {
      int stretchH = finalH - paddingTop - paddingBottom;
      for (auto &child : children)
      {
        if (!child->visible)
          continue;
        int childStretchH = stretchH - child->marginTop - child->marginBottom;
        if (childStretchH > 0 && child->height != childStretchH)
        {
          child->autoHeight = false;
          child->computeLayout(ctx,
                               BoxConstraints(child->width, child->width,
                                              childStretchH, childStretchH),
                               fontCache);
        }
      }
    }

    // ── Overflow detection ────────────────────────────────────────────────
    overflow.reset();
    int budget = (mainAxisSize == MainAxisSize::Max) ? self.maxWidth : finalW;
    overflow = detectRowOverflow(naturalW, budget);
    if (overflow.hasOverflow())
      FluxOverflow::logWarning("Row", overflow, id);

    width = finalW;
    height = finalH;
    needsLayout = false;
  }

  void positionChildren(int contentX, int contentY, int contentWidth,
                        int contentHeight) override
  {

    // Measure total child width for alignment
    int totalChildWidth = 0;
    bool first = true;
    for (auto &child : children)
    {
      if (!child->visible)
        continue;
      if (!first)
        totalChildWidth += spacing;
      first = false;
      totalChildWidth += child->width + child->marginLeft + child->marginRight;
    }

    int freeSpace = contentWidth - totalChildWidth;
    int currentX = contentX;
    int betweenSpace = 0;

    switch (mainAxisAlignment)
    {
    case MainAxisAlignment::Center:
      currentX += freeSpace / 2;
      break;
    case MainAxisAlignment::End:
      currentX += freeSpace;
      break;
    case MainAxisAlignment::SpaceBetween:
      betweenSpace =
          (visibleChildCount() > 1) ? freeSpace / (visibleChildCount() - 1) : 0;
      break;
    case MainAxisAlignment::SpaceAround:
      betweenSpace =
          visibleChildCount() > 0 ? freeSpace / visibleChildCount() : 0;
      currentX += betweenSpace / 2;
      break;
    case MainAxisAlignment::SpaceEvenly:
      betweenSpace = freeSpace / (visibleChildCount() + 1);
      currentX += betweenSpace;
      break;
    default: // Start
      break;
    }

    for (size_t i = 0; i < children.size(); ++i)
    {
      auto &child = children[i];
      if (!child->visible)
        continue;

      // Cross axis (Y)
      int childY = contentY + child->marginTop;
      switch (crossAxisAlignment)
      {
      case CrossAxisAlignment::Center:
        childY = contentY + (contentHeight - child->height) / 2;
        break;
      case CrossAxisAlignment::End:
        childY = contentY + contentHeight - child->height - child->marginBottom;
        break;
      case CrossAxisAlignment::Stretch:
        child->height = contentHeight - child->marginTop - child->marginBottom;
        break;
      default:
        break;
      }

      child->x = currentX + child->marginLeft;
      child->y = childY;

      child->positionChildren(
          child->x + child->paddingLeft, child->y + child->paddingTop,
          child->width - child->paddingLeft - child->paddingRight,
          child->height - child->paddingTop - child->paddingBottom);

      currentX += child->width + child->marginLeft + child->marginRight +
                  spacing + betweenSpace;
    }
  }

  // ── Fluent setters ────────────────────────────────────────────────────────

  std::shared_ptr<RowWidget> setMainAxisSize(MainAxisSize s)
  {
    mainAxisSize = s;
    markNeedsLayout();
    return std::static_pointer_cast<RowWidget>(shared_from_this());
  }
  std::shared_ptr<RowWidget> setMainAxisAlignment(MainAxisAlignment a)
  {
    mainAxisAlignment = a;
    markNeedsLayout();
    return std::static_pointer_cast<RowWidget>(shared_from_this());
  }
  std::shared_ptr<RowWidget> setCrossAxisAlignment(CrossAxisAlignment a)
  {
    crossAxisAlignment = a;
    markNeedsLayout();
    return std::static_pointer_cast<RowWidget>(shared_from_this());
  }
  std::shared_ptr<RowWidget> setSpacing(int s)
  {
    spacing = s;
    markNeedsLayout();
    return std::static_pointer_cast<RowWidget>(shared_from_this());
  }
  std::shared_ptr<RowWidget> setPadding(int p)
  {
    paddingLeft = paddingRight = paddingTop = paddingBottom = p;
    markNeedsLayout();
    return std::static_pointer_cast<RowWidget>(shared_from_this());
  }
  std::shared_ptr<RowWidget> setBackgroundColor(Color color)
  {
    backgroundColor = color;
    hasBackground = true;
    markNeedsPaint();
    return std::static_pointer_cast<RowWidget>(shared_from_this());
  }
  std::shared_ptr<RowWidget> setFlex(int f)
  {
    // Used when this Row is itself inside an Expanded/Column
    flex = f;
    markNeedsLayout();
    return std::static_pointer_cast<RowWidget>(shared_from_this());
  }

private:
  int visibleChildCount() const
  {
    int n = 0;
    for (auto &c : children)
      if (c->visible)
        n++;
    return n;
  }
};

class ColumnWidget : public Widget
{
public:
  MainAxisSize mainAxisSize = MainAxisSize::Max;

  void computeLayout(GraphicsContext &ctx, const BoxConstraints &constraints,
                     FontCache &fontCache) override
  {

    BoxConstraints self = selfConstraints(constraints);
    BoxConstraints content = contentConstraints(self);

    // ── PASS 1 : non-flex children — unbounded main axis ─────────────────
    int totalFlex = 0;
    int fixedHeight = 0;

    int visibleCount = 0;
    for (auto &child : children)
      if (child->visible)
        visibleCount++;
    int spacingTotal = visibleCount > 1 ? spacing * (visibleCount - 1) : 0;

    for (auto &child : children)
    {
      if (!child->visible)
        continue;

      if (child->isExpanded())
      {
        totalFlex += child->flex;
      }
      else
      {

        BoxConstraints childC(0, content.maxWidth, // cross axis : bounded
                              0, kUnbounded        // main axis  : truly unbounded
        );
        child->computeLayout(ctx, childC, fontCache);
        fixedHeight += child->height + child->marginTop + child->marginBottom;
      }
    }

    // ── PASS 2 : flex children — divide remaining height ─────────────────
    int remainingHeight =
        std::max(0, content.maxHeight - fixedHeight - spacingTotal);

    if (totalFlex > 0)
    {
      int allocated = 0;
      Widget *lastFlex = nullptr;

      for (auto &child : children)
      {
        if (!child->visible || !child->isExpanded())
          continue;

        lastFlex = child.get();
        int sliceH = (remainingHeight * child->flex) / totalFlex;
        allocated += sliceH;

        BoxConstraints childC(0, content.maxWidth, // loose width
                              sliceH, sliceH       // tight height
        );
        child->computeLayout(ctx, childC, fontCache);
      }

      // Rounding remainder → last flex child.
      if (lastFlex)
      {
        int leftover = remainingHeight - allocated;
        if (leftover > 0)
        {
          int correctedH = lastFlex->height + leftover;
          lastFlex->computeLayout(
              ctx, BoxConstraints(0, content.maxWidth, correctedH, correctedH),
              fontCache);
        }
      }
    }

    // ── PASS 3 : compute Column's own size ────────────────────────────────
    int totalChildHeight = 0;
    int maxChildWidth = 0;
    bool first = true;

    for (auto &child : children)
    {
      if (!child->visible)
        continue;
      if (!first)
        totalChildHeight += spacing;
      first = false;

      totalChildHeight +=
          child->height + child->marginTop + child->marginBottom;
      maxChildWidth = std::max(maxChildWidth, child->width + child->marginLeft +
                                                  child->marginRight);
    }

    int naturalH = totalChildHeight + paddingTop + paddingBottom;

    int naturalW = maxChildWidth + paddingLeft + paddingRight;
    int finalW;
    if (self.maxWidth >= kUnbounded)
    {
      // Unbounded parent — shrink-wrap, same as before
      finalW = self.clampWidth(naturalW);
    }
    else
    {
      // Finite parent — fill available width (Flutter default)
      finalW = self.clampWidth(self.maxWidth);
    }

    // Main axis sizing
    int finalH =
        (mainAxisSize == MainAxisSize::Max)
            ? self.clampHeight(content.maxHeight + paddingTop + paddingBottom)
            : self.clampHeight(naturalH);

    if (crossAxisAlignment == CrossAxisAlignment::Stretch)
    {
      int stretchW = finalW - paddingLeft - paddingRight;
      for (auto &child : children)
      {
        if (!child->visible)
          continue;
        int childStretchW = stretchW - child->marginLeft - child->marginRight;
        if (childStretchW > 0 && child->width != childStretchW)
        {
          child->autoWidth = false;
          child->computeLayout(ctx,
                               BoxConstraints(childStretchW, childStretchW,
                                              child->height, child->height),
                               fontCache);
        }
      }
    }

    // ── Overflow detection ────────────────────────────────────────────────
    overflow.reset();
    int budget = (mainAxisSize == MainAxisSize::Max) ? self.maxHeight : finalH;
    overflow = detectColumnOverflow(naturalH, budget);
    if (overflow.hasOverflow())
      FluxOverflow::logWarning("Column", overflow, id);

    width = finalW;
    height = finalH;
    needsLayout = false;
  }

  void positionChildren(int contentX, int contentY, int contentWidth,
                        int contentHeight) override
  {

    int totalChildHeight = 0;
    bool first = true;
    for (auto &child : children)
    {
      if (!child->visible)
        continue;
      if (!first)
        totalChildHeight += spacing;
      first = false;
      totalChildHeight +=
          child->height + child->marginTop + child->marginBottom;
    }

    int freeSpace = contentHeight - totalChildHeight;
    int currentY = contentY;
    int betweenSpace = 0;

    switch (mainAxisAlignment)
    {
    case MainAxisAlignment::Center:
      currentY += freeSpace / 2;
      break;
    case MainAxisAlignment::End:
      currentY += freeSpace;
      break;
    case MainAxisAlignment::SpaceBetween:
      betweenSpace =
          (visibleChildCount() > 1) ? freeSpace / (visibleChildCount() - 1) : 0;
      break;
    case MainAxisAlignment::SpaceAround:
      betweenSpace =
          visibleChildCount() > 0 ? freeSpace / visibleChildCount() : 0;
      currentY += betweenSpace / 2;
      break;
    case MainAxisAlignment::SpaceEvenly:
      betweenSpace = freeSpace / (visibleChildCount() + 1);
      currentY += betweenSpace;
      break;
    default:
      break;
    }

    for (size_t i = 0; i < children.size(); ++i)
    {
      auto &child = children[i];
      if (!child->visible)
        continue;

      // Cross axis (X)
      int childX = contentX + child->marginLeft;
      switch (crossAxisAlignment)
      {
      case CrossAxisAlignment::Center:
        childX = contentX + (contentWidth - child->width) / 2;
        break;
      case CrossAxisAlignment::End:
        childX = contentX + contentWidth - child->width - child->marginRight;
        break;
      case CrossAxisAlignment::Stretch:
        child->width = contentWidth - child->marginLeft - child->marginRight;
        break;
      default:
        break;
      }

      child->x = childX;
      child->y = currentY + child->marginTop;

      child->positionChildren(
          child->x + child->paddingLeft, child->y + child->paddingTop,
          child->width - child->paddingLeft - child->paddingRight,
          child->height - child->paddingTop - child->paddingBottom);

      currentY += child->height + child->marginTop + child->marginBottom +
                  spacing + betweenSpace;
    }
  }

  // ── Fluent setters ────────────────────────────────────────────────────────
  std::shared_ptr<ColumnWidget> setMainAxisSize(MainAxisSize s)
  {
    mainAxisSize = s;
    markNeedsLayout();
    return std::static_pointer_cast<ColumnWidget>(shared_from_this());
  }
  std::shared_ptr<ColumnWidget> setMainAxisAlignment(MainAxisAlignment a)
  {
    mainAxisAlignment = a;
    markNeedsLayout();
    return std::static_pointer_cast<ColumnWidget>(shared_from_this());
  }
  std::shared_ptr<ColumnWidget> setCrossAxisAlignment(CrossAxisAlignment a)
  {
    crossAxisAlignment = a;
    markNeedsLayout();
    return std::static_pointer_cast<ColumnWidget>(shared_from_this());
  }
  std::shared_ptr<ColumnWidget> setSpacing(int s)
  {
    spacing = s;
    markNeedsLayout();
    return std::static_pointer_cast<ColumnWidget>(shared_from_this());
  }
  std::shared_ptr<ColumnWidget> setPadding(int p)
  {
    paddingLeft = paddingRight = paddingTop = paddingBottom = p;
    markNeedsLayout();
    return std::static_pointer_cast<ColumnWidget>(shared_from_this());
  }
  std::shared_ptr<ColumnWidget> setBackgroundColor(Color color)
  {
    backgroundColor = color;
    hasBackground = true;
    markNeedsPaint();
    return std::static_pointer_cast<ColumnWidget>(shared_from_this());
  }
  std::shared_ptr<ColumnWidget> setBorderRadius(int r)
  {
    borderRadius = r;
    markNeedsPaint();
    return std::static_pointer_cast<ColumnWidget>(shared_from_this());
  }
  std::shared_ptr<ColumnWidget> setFlex(int f)
  {
    flex = f;
    markNeedsLayout();
    return std::static_pointer_cast<ColumnWidget>(shared_from_this());
  }
  std::shared_ptr<ColumnWidget> setMinWidth(int w)
  {
    minWidth = w;
    markNeedsLayout();
    return std::static_pointer_cast<ColumnWidget>(shared_from_this());
  }

private:
  int visibleChildCount() const
  {
    int n = 0;
    for (auto &c : children)
      if (c->visible)
        n++;
    return n;
  }
};

class StackWidget : public Widget
{
public:
  bool expand = false;

  void computeLayout(GraphicsContext &ctx, const BoxConstraints &constraints,
                     FontCache &fontCache) override
  {

    BoxConstraints self = selfConstraints(constraints);
    BoxConstraints content = contentConstraints(self);

    for (auto &child : children)
    {
      BoxConstraints childC =
          isPositioned(child)
              ? BoxConstraints::loose(content.maxWidth, content.maxHeight)
              : BoxConstraints(0, content.maxWidth, 0, content.maxHeight);
      child->computeLayout(ctx, childC, fontCache);
    }

    int intrinsicW = 0, intrinsicH = 0;
    if (expand)
    {
      intrinsicW = content.maxWidth;
      intrinsicH = content.maxHeight;
    }
    else
    {
      for (auto &child : children)
      {
        if (!isPositioned(child))
        {
          intrinsicW = std::max(intrinsicW, child->width + child->marginLeft +
                                                child->marginRight);
          intrinsicH = std::max(intrinsicH, child->height + child->marginTop +
                                                child->marginBottom);
        }
      }
    }

    width = self.clampWidth(autoWidth ? intrinsicW + paddingLeft + paddingRight
                                      : width);
    height = self.clampHeight(
        autoHeight ? intrinsicH + paddingTop + paddingBottom : height);
    needsLayout = false;
  }

  void positionChildren(int contentX, int contentY, int contentWidth,
                        int contentHeight) override
  {
    for (auto &child : children)
    {
      if (isPositioned(child))
      {
        int cx = contentX + child->marginLeft;
        int cy = contentY + child->marginTop;

        if (child->marginLeft == 0 && child->marginRight > 0)
          cx = contentX + contentWidth - child->width - child->marginRight;
        if (child->marginTop == 0 && child->marginBottom > 0)
          cy = contentY + contentHeight - child->height - child->marginBottom;

        child->x = cx;
        child->y = cy;
      }
      else
      {
        int cx = contentX + child->marginLeft;
        int cy = contentY + child->marginTop;

        switch (alignment)
        {
        case Alignment::Center:
          cx = contentX + (contentWidth - child->width) / 2;
          cy = contentY + (contentHeight - child->height) / 2;
          break;
        case Alignment::TopCenter:
          cx = contentX + (contentWidth - child->width) / 2;
          break;
        case Alignment::BottomCenter:
          cx = contentX + (contentWidth - child->width) / 2;
          cy = contentY + contentHeight - child->height - child->marginBottom;
          break;
        case Alignment::CenterLeft:
          cy = contentY + (contentHeight - child->height) / 2;
          break;
        case Alignment::CenterRight:
          cx = contentX + contentWidth - child->width - child->marginRight;
          cy = contentY + (contentHeight - child->height) / 2;
          break;
        case Alignment::TopRight:
          cx = contentX + contentWidth - child->width - child->marginRight;
          break;
        case Alignment::BottomLeft:
          cy = contentY + contentHeight - child->height - child->marginBottom;
          break;
        case Alignment::BottomRight:
          cx = contentX + contentWidth - child->width - child->marginRight;
          cy = contentY + contentHeight - child->height - child->marginBottom;
          break;
        default:
          break;
        }

        child->x = cx;
        child->y = cy;
      }

      child->positionChildren(
          child->x + child->paddingLeft, child->y + child->paddingTop,
          child->width - child->paddingLeft - child->paddingRight,
          child->height - child->paddingTop - child->paddingBottom);
    }
  }

  // ── Fluent setters ────────────────────────────────────────────────────────
  std::shared_ptr<StackWidget> setAlignment(Alignment a)
  {
    alignment = a;
    markNeedsLayout();
    return std::static_pointer_cast<StackWidget>(shared_from_this());
  }
  std::shared_ptr<StackWidget> setExpand(bool e)
  {
    expand = e;
    markNeedsLayout();
    return std::static_pointer_cast<StackWidget>(shared_from_this());
  }
  std::shared_ptr<StackWidget> setWidth(int w)
  {
    width = w;
    autoWidth = false;
    markNeedsLayout();
    return std::static_pointer_cast<StackWidget>(shared_from_this());
  }
  std::shared_ptr<StackWidget> setHeight(int h)
  {
    height = h;
    autoHeight = false;
    markNeedsLayout();
    return std::static_pointer_cast<StackWidget>(shared_from_this());
  }
  std::shared_ptr<StackWidget> setPadding(int p)
  {
    paddingLeft = paddingRight = paddingTop = paddingBottom = p;
    markNeedsLayout();
    return std::static_pointer_cast<StackWidget>(shared_from_this());
  }
  std::shared_ptr<StackWidget> setBackgroundColor(Color color)
  {
    backgroundColor = color;
    hasBackground = true;
    markNeedsPaint();
    return std::static_pointer_cast<StackWidget>(shared_from_this());
  }
  std::shared_ptr<StackWidget> setBorderRadius(int r)
  {
    borderRadius = r;
    markNeedsPaint();
    return std::static_pointer_cast<StackWidget>(shared_from_this());
  }
  std::shared_ptr<StackWidget> setFlex(int f)
  {
    flex = f;
    markNeedsLayout();
    return std::static_pointer_cast<StackWidget>(shared_from_this());
  }

  // Reactive margin setters (for Positioned animations)
  template <typename T, typename F>
  std::shared_ptr<Widget> setMarginLeft(State<T> &state, F transform)
  {
    std::function<int(const T &)> fn = transform;
    marginLeft = fn(state.get());
    state.bindProperty(
        shared_from_this(),
        [fn](Widget *w, const T &val)
        {
          w->marginLeft = fn(val);
          w->markNeedsLayout();
        },
        true);
    return shared_from_this();
  }
  template <typename T, typename F>
  std::shared_ptr<Widget> setMarginTop(State<T> &state, F transform)
  {
    std::function<int(const T &)> fn = transform;
    marginTop = fn(state.get());
    state.bindProperty(
        shared_from_this(),
        [fn](Widget *w, const T &val)
        {
          w->marginTop = fn(val);
          w->markNeedsLayout();
        },
        true);
    return shared_from_this();
  }
  template <typename T, typename F>
  std::shared_ptr<Widget> setMarginRight(State<T> &state, F transform)
  {
    std::function<int(const T &)> fn = transform;
    marginRight = fn(state.get());
    state.bindProperty(
        shared_from_this(),
        [fn](Widget *w, const T &val)
        {
          w->marginRight = fn(val);
          w->markNeedsLayout();
        },
        true);
    return shared_from_this();
  }
  template <typename T, typename F>
  std::shared_ptr<Widget> setMarginBottom(State<T> &state, F transform)
  {
    std::function<int(const T &)> fn = transform;
    marginBottom = fn(state.get());
    state.bindProperty(
        shared_from_this(),
        [fn](Widget *w, const T &val)
        {
          w->marginBottom = fn(val);
          w->markNeedsLayout();
        },
        true);
    return shared_from_this();
  }

private:
  static bool isPositioned(const std::shared_ptr<Widget> &child)
  {
    return child->marginLeft != 0 || child->marginTop != 0 ||
           child->marginRight != 0 || child->marginBottom != 0;
  }
};

class ContainerWidget : public Widget
{
public:
  void computeLayout(GraphicsContext &ctx, const BoxConstraints &constraints,
                     FontCache &fontCache) override
  {

    BoxConstraints self = selfConstraints(constraints);

    if (!children.empty())
    {
      int maxW = autoWidth ? self.maxWidth : width;
      int maxH = autoHeight ? self.maxHeight : height;
      // If a maxWidth/maxHeight cap was set by the user, enforce it
      if (autoWidth && maxWidth > 0)
        maxW = std::min(maxW, maxWidth);
      if (autoHeight && maxHeight > 0)
        maxH = std::min(maxH, maxHeight);
      int minW = autoWidth ? 0 : maxW;
      int minH = autoHeight ? 0 : maxH;
      BoxConstraints childC =
          BoxConstraints(minW, maxW, minH, maxH)
              .deflate(paddingLeft + paddingRight, paddingTop + paddingBottom);

      children[0]->computeLayout(ctx, childC, fontCache);

      if (autoWidth)
        width =
            self.clampWidth(children[0]->width + paddingLeft + paddingRight);

      if (autoWidth && maxWidth > 0 && width > maxWidth)
        width = maxWidth;
      if (autoHeight)
        height =
            self.clampHeight(children[0]->height + paddingTop + paddingBottom);
    }
    else
    {
      width = autoWidth ? self.clampWidth(0) : width;     // explicit size wins
      height = autoHeight ? self.clampHeight(0) : height; // explicit size wins
    }

    // ── Overflow detection ────────────────────────────────────────────────
    overflow.reset();
    if (!children.empty())
    {
      int contentW = width - paddingLeft - paddingRight;
      int contentH = height - paddingTop - paddingBottom;

      if (!autoWidth && children[0]->width > contentW + kOverflowThreshold)
      {
        overflow.overflowX = children[0]->width - contentW;
        overflow.axis = OverflowAxis::Horizontal;
      }
      if (!autoHeight && children[0]->height > contentH + kOverflowThreshold)
      {
        overflow.overflowY = children[0]->height - contentH;
        overflow.axis = (overflow.axis == OverflowAxis::Horizontal)
                            ? OverflowAxis::Both
                            : OverflowAxis::Vertical;
      }
    }
    if (overflow.hasOverflow())
      FluxOverflow::logWarning("Container", overflow, id);

    needsLayout = false;
  }

  void positionChildren(int contentX, int contentY, int /*cW*/,
                        int /*cH*/) override
  {
    if (!children.empty())
    {
      auto &child = children[0];
      child->x = contentX;
      child->y = contentY;
      child->positionChildren(
          child->x + child->paddingLeft, child->y + child->paddingTop,
          child->width - child->paddingLeft - child->paddingRight,
          child->height - child->paddingTop - child->paddingBottom);
    }
  }

  void render(GraphicsContext &ctx, FontCache &fontCache) override
  {
    if (!visible)
      return;
    Painter painter(ctx);
    if (hasBackground)
      painter.fillRoundedRect(x, y, width, height, borderRadius,
                              getCurrentBackgroundColor());
    if (hasBorder)
    {
      painter.pushClipRect(x, y, width, height);
      for (auto &child : children)
        child->render(ctx, fontCache);
      painter.popClipRect();
      painter.drawBorder(x, y, width, height, borderRadius,
                         getCurrentBorderColor(), borderWidth);
    }
    else
    {
      for (auto &child : children)
        child->render(ctx, fontCache);
    }

    if (overflow.hasOverflow())
      FluxOverflow::render(painter, fontCache, overflow, x, y, width, height);

    needsPaint = false;
  }

  // ── Fluent setters ────────────────────────────────────────────────────────
  std::shared_ptr<ContainerWidget> setWidth(int w)
  {
    width = w;
    autoWidth = false;
    markNeedsLayout();
    return std::static_pointer_cast<ContainerWidget>(shared_from_this());
  }
  std::shared_ptr<ContainerWidget> setHeight(int h)
  {
    height = h;
    autoHeight = false;
    markNeedsLayout();
    return std::static_pointer_cast<ContainerWidget>(shared_from_this());
  }
  std::shared_ptr<ContainerWidget> setMinWidth(int w)
  {
    minWidth = w;
    markNeedsLayout();
    return std::static_pointer_cast<ContainerWidget>(shared_from_this());
  }
  std::shared_ptr<ContainerWidget> setMinHeight(int h)
  {
    minHeight = h;
    markNeedsLayout();
    return std::static_pointer_cast<ContainerWidget>(shared_from_this());
  }
  std::shared_ptr<ContainerWidget> setMaxWidth(int w)
  {
    maxWidth = w;
    markNeedsLayout();
    return std::static_pointer_cast<ContainerWidget>(shared_from_this());
  }
  std::shared_ptr<ContainerWidget> setMaxHeight(int h)
  {
    maxHeight = h;
    markNeedsLayout();
    return std::static_pointer_cast<ContainerWidget>(shared_from_this());
  }
  std::shared_ptr<ContainerWidget> setBackgroundColor(Color color)
  {
    backgroundColor = color;
    hasBackground = true;
    markNeedsPaint();
    return std::static_pointer_cast<ContainerWidget>(shared_from_this());
  }
  std::shared_ptr<ContainerWidget> setBorderColor(Color color)
  {
    borderColor = color;
    hasBorder = true;
    markNeedsPaint();
    return std::static_pointer_cast<ContainerWidget>(shared_from_this());
  }
  std::shared_ptr<ContainerWidget> setBorderWidth(int w)
  {
    borderWidth = w;
    hasBorder = true;
    markNeedsLayout();
    return std::static_pointer_cast<ContainerWidget>(shared_from_this());
  }
  std::shared_ptr<ContainerWidget> setBorderRadius(int r)
  {
    borderRadius = r;
    markNeedsPaint();
    return std::static_pointer_cast<ContainerWidget>(shared_from_this());
  }
  std::shared_ptr<ContainerWidget> setHoverBackgroundColor(Color color)
  {
    hoverBackgroundColor = color;
    hasHoverBackground = true;
    markNeedsPaint();
    return std::static_pointer_cast<ContainerWidget>(shared_from_this());
  }
  std::shared_ptr<ContainerWidget> setHoverBorderColor(Color color)
  {
    hoverBorderColor = color;
    hasHoverBorderColor = true;
    markNeedsPaint();
    return std::static_pointer_cast<ContainerWidget>(shared_from_this());
  }
  std::shared_ptr<ContainerWidget> setPadding(int p)
  {
    paddingLeft = paddingRight = paddingTop = paddingBottom = p;
    markNeedsLayout();
    return std::static_pointer_cast<ContainerWidget>(shared_from_this());
  }
  std::shared_ptr<ContainerWidget> setPaddingAll(int l, int t, int r, int b)
  {
    paddingLeft = l;
    paddingTop = t;
    paddingRight = r;
    paddingBottom = b;
    markNeedsLayout();
    return std::static_pointer_cast<ContainerWidget>(shared_from_this());
  }
  std::shared_ptr<ContainerWidget> setMargin(int m)
  {
    marginLeft = marginRight = marginTop = marginBottom = m;
    markNeedsLayout();
    return std::static_pointer_cast<ContainerWidget>(shared_from_this());
  }
  std::shared_ptr<ContainerWidget> setMarginAll(int l, int t, int r, int b)
  {
    marginLeft = l;
    marginTop = t;
    marginRight = r;
    marginBottom = b;
    markNeedsLayout();
    return std::static_pointer_cast<ContainerWidget>(shared_from_this());
  }
  std::shared_ptr<ContainerWidget> setFlex(int f)
  {
    flex = f;
    markNeedsLayout();
    return std::static_pointer_cast<ContainerWidget>(shared_from_this());
  }
  std::shared_ptr<ContainerWidget> setOnHover(HoverHandler h)
  {
    onHover = h;
    return std::static_pointer_cast<ContainerWidget>(shared_from_this());
  }
  std::shared_ptr<ContainerWidget> setVisible(bool v)
  {
    visible = v;
    markNeedsLayout();
    return std::static_pointer_cast<ContainerWidget>(shared_from_this());
  }

  // ── Reactive overloads ────────────────────────────────────────────────────
  template <typename T, typename F>
  std::shared_ptr<ContainerWidget> setWidth(State<T> &state, F transform)
  {
    std::function<int(const T &)> fn = transform;
    width = fn(state.get());
    autoWidth = false;
    markNeedsLayout();
    state.bindProperty(
        shared_from_this(),
        [fn](Widget *w, const T &val)
        {
          auto *s = static_cast<ContainerWidget *>(w);
          s->width = fn(val);
          s->autoWidth = false;
          s->markNeedsLayout();
        },
        true);
    return std::static_pointer_cast<ContainerWidget>(shared_from_this());
  }
  template <typename T, typename F>
  std::shared_ptr<ContainerWidget> setHeight(State<T> &state, F transform)
  {
    std::function<int(const T &)> fn = transform;
    height = fn(state.get());
    autoHeight = false;
    markNeedsLayout();
    state.bindProperty(
        shared_from_this(),
        [fn](Widget *w, const T &val)
        {
          auto *s = static_cast<ContainerWidget *>(w);
          s->height = fn(val);
          s->autoHeight = false;
          s->markNeedsLayout();
        },
        true);
    return std::static_pointer_cast<ContainerWidget>(shared_from_this());
  }
  template <typename T, typename F>
  std::shared_ptr<ContainerWidget> setBackgroundColor(State<T> &state,
                                                      F transform)
  {
    std::function<Color(const T &)> fn = transform;
    backgroundColor = fn(state.get());
    hasBackground = true;
    markNeedsPaint();
    state.bindProperty(
        shared_from_this(),
        [fn](Widget *w, const T &val)
        {
          auto *s = static_cast<ContainerWidget *>(w);
          s->backgroundColor = fn(val);
          s->hasBackground = true;
          s->markNeedsPaint();
        },
        true);
    return std::static_pointer_cast<ContainerWidget>(shared_from_this());
  }
  template <typename T, typename F>
  std::shared_ptr<ContainerWidget> setBorderColor(State<T> &state,
                                                  F transform)
  {
    std::function<Color(const T &)> fn = transform;
    borderColor = fn(state.get());
    hasBorder = true;
    markNeedsPaint();
    state.bindProperty(
        shared_from_this(),
        [fn](Widget *w, const T &val)
        {
          auto *s = static_cast<ContainerWidget *>(w);
          s->borderColor = fn(val);
          s->hasBorder = true;
          s->markNeedsPaint();
        },
        true);
    return std::static_pointer_cast<ContainerWidget>(shared_from_this());
  }
  template <typename T, typename F>
  std::shared_ptr<ContainerWidget> setBorderWidth(State<T> &state,
                                                  F transform)
  {
    std::function<int(const T &)> fn = transform;
    borderWidth = fn(state.get());
    hasBorder = true;
    markNeedsLayout();
    state.bindProperty(
        shared_from_this(),
        [fn](Widget *w, const T &val)
        {
          auto *s = static_cast<ContainerWidget *>(w);
          s->borderWidth = fn(val);
          s->hasBorder = true;
          s->markNeedsLayout();
        },
        true);
    return std::static_pointer_cast<ContainerWidget>(shared_from_this());
  }
  template <typename T, typename F>
  std::shared_ptr<ContainerWidget> setBorderRadius(State<T> &state,
                                                   F transform)
  {
    std::function<int(const T &)> fn = transform;
    borderRadius = fn(state.get());
    markNeedsPaint();
    state.bindProperty(
        shared_from_this(),
        [fn](Widget *w, const T &val)
        {
          static_cast<ContainerWidget *>(w)->borderRadius = fn(val);
          w->markNeedsPaint();
        },
        true);
    return std::static_pointer_cast<ContainerWidget>(shared_from_this());
  }
  template <typename T, typename F>
  std::shared_ptr<ContainerWidget> setVisible(State<T> &state, F transform)
  {
    std::function<bool(const T &)> fn = transform;
    visible = fn(state.get());
    markNeedsLayout();
    state.bindProperty(
        shared_from_this(),
        [fn](Widget *w, const T &val)
        {
          auto *s = static_cast<ContainerWidget *>(w);
          s->visible = fn(val);
          s->markNeedsLayout();
        },
        true);
    return std::static_pointer_cast<ContainerWidget>(shared_from_this());
  }
};

// ============================================================================
// CenterWidget  — unchanged, centers its single child
// ============================================================================

class CenterWidget : public Widget
{
public:
  void computeLayout(GraphicsContext &ctx, const BoxConstraints &constraints,
                     FontCache &fontCache) override
  {
    BoxConstraints self = selfConstraints(constraints);
    if (autoWidth)
      width = self.maxWidth;
    if (autoHeight)
      height = self.maxHeight;

    if (!children.empty())
      children[0]->computeLayout(ctx, contentConstraints(self), fontCache);

    needsLayout = false;
  }

  void positionChildren(int contentX, int contentY, int contentWidth,
                        int contentHeight) override
  {
    if (!children.empty())
    {
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

class SizedBoxWidget : public Widget
{
public:
  void computeLayout(GraphicsContext &ctx, const BoxConstraints &constraints,
                     FontCache &fontCache) override
  {

    BoxConstraints self = selfConstraints(constraints);

    // Clamp our fixed dimensions to incoming constraints
    int finalW = autoWidth ? self.maxWidth : self.clampWidth(width);
    int finalH = autoHeight ? self.maxHeight : self.clampHeight(height);

    if (!children.empty())
    {
      // Child gets tight constraints on fixed axes, loose on auto axes
      int minW = autoWidth ? 0 : finalW - paddingLeft - paddingRight;
      int maxW = finalW - paddingLeft - paddingRight;
      int minH = autoHeight ? 0 : finalH - paddingTop - paddingBottom;
      int maxH = finalH - paddingTop - paddingBottom;

      BoxConstraints childC(std::max(0, minW), std::max(0, maxW),
                            std::max(0, minH), std::max(0, maxH));
      children[0]->computeLayout(ctx, childC, fontCache);

      // Auto axes shrink to child
      if (autoWidth)
        finalW = children[0]->width + paddingLeft + paddingRight;
      if (autoHeight)
        finalH = children[0]->height + paddingTop + paddingBottom;
    }

    width = finalW;
    height = finalH;
    needsLayout = false;
  }

  void positionChildren(int contentX, int contentY, int /*cW*/,
                        int /*cH*/) override
  {
    if (!children.empty())
    {
      auto &child = children[0];
      child->x = contentX;
      child->y = contentY;
      child->positionChildren(
          child->x + child->paddingLeft, child->y + child->paddingTop,
          child->width - child->paddingLeft - child->paddingRight,
          child->height - child->paddingTop - child->paddingBottom);
    }
  }
};

// ============================================================================
// FACTORY FUNCTIONS
// ============================================================================

using ContainerWidgetPtr = std::shared_ptr<ContainerWidget>;
using StackWidgetPtr = std::shared_ptr<StackWidget>;
using RowWidgetPtr = std::shared_ptr<RowWidget>;
using ColumnWidgetPtr = std::shared_ptr<ColumnWidget>;
using ExpandedWidgetPtr = std::shared_ptr<ExpandedWidget>;

// Container — single-child box with optional sizing, padding, background
inline ContainerWidgetPtr Container(WidgetPtr child = nullptr)
{
  auto w = std::make_shared<ContainerWidget>();
  if (child)
    w->addChild(child);
  return w;
}

// Stack — children layered on top of each other
inline StackWidgetPtr Stack(std::initializer_list<WidgetPtr> children)
{
  auto w = std::make_shared<StackWidget>();
  for (auto &child : children)
    w->addChild(child);
  return w;
}
template <typename... Widgets>
StackWidgetPtr Stack(Widgets... widgets)
{
  auto w = std::make_shared<StackWidget>();
  (w->addChild(widgets), ...);
  return w;
}

// Row — horizontal flex layout  (width fills parent, height shrinks to tallest
// child)
inline RowWidgetPtr Row(std::initializer_list<WidgetPtr> children)
{
  auto w = std::make_shared<RowWidget>();
  for (auto &child : children)
    w->addChild(child);
  return w;
}

// Column — vertical flex layout  (height fills parent, width shrinks to widest
// child)
inline ColumnWidgetPtr Column(std::initializer_list<WidgetPtr> children)
{
  auto w = std::make_shared<ColumnWidget>();
  for (auto &child : children)
    w->addChild(child);
  return w;
}

inline ExpandedWidgetPtr Expanded(WidgetPtr child, int flex = 1)
{
  auto w = std::make_shared<ExpandedWidget>();
  w->flex = flex;
  if (child)
    w->addChild(child);
  return w;
}

inline std::shared_ptr<SizedBoxWidget> SizedBox(int w, int h,
                                                WidgetPtr child = nullptr)
{
  auto box = std::make_shared<SizedBoxWidget>();
  if (w >= 0)
  {
    box->width = w;
    box->autoWidth = false;
  }
  if (h >= 0)
  {
    box->height = h;
    box->autoHeight = false;
  }
  if (child)
    box->addChild(child);
  return box;
}

// Center — centers its child within available space
inline std::shared_ptr<CenterWidget> Center(WidgetPtr child = nullptr)
{
  auto w = std::make_shared<CenterWidget>();
  w->alignment = Alignment::Center;
  if (child)
    w->addChild(child);
  return w;
}

// Positioned — stamps absolute offsets onto a child for use inside Stack
inline WidgetPtr Positioned(WidgetPtr child, int left = 0, int top = 0,
                            int right = 0, int bottom = 0)
{
  child->marginLeft = left;
  child->marginTop = top;
  child->marginRight = right;
  child->marginBottom = bottom;
  return child;
}

// Reactive Positioned — single state, separate x/y transforms
template <typename T, typename FX, typename FY>
inline WidgetPtr Positioned(WidgetPtr child, State<T> &state, FX xTransform,
                            FY yTransform)
{
  std::function<int(const T &)> xFn = xTransform;
  std::function<int(const T &)> yFn = yTransform;
  child->marginLeft = xFn(state.get());
  child->marginTop = yFn(state.get());
  state.bindProperty(
      child,
      [xFn](Widget *w, const T &val)
      {
        w->marginLeft = xFn(val);
        w->markNeedsLayout();
      },
      true);
  state.bindProperty(
      child,
      [yFn](Widget *w, const T &val)
      {
        w->marginTop = yFn(val);
        w->markNeedsLayout();
      },
      true);
  return child;
}

// Reactive Positioned — two independent states
template <typename TX, typename TY, typename FX, typename FY>
inline WidgetPtr Positioned(WidgetPtr child, State<TX> &xState, FX xTransform,
                            State<TY> &yState, FY yTransform)
{
  std::function<int(const TX &)> xFn = xTransform;
  std::function<int(const TY &)> yFn = yTransform;
  child->marginLeft = xFn(xState.get());
  child->marginTop = yFn(yState.get());
  xState.bindProperty(
      child,
      [xFn](Widget *w, const TX &val)
      {
        w->marginLeft = xFn(val);
        w->markNeedsLayout();
      },
      true);
  yState.bindProperty(
      child,
      [yFn](Widget *w, const TY &val)
      {
        w->marginTop = yFn(val);
        w->markNeedsLayout();
      },
      true);
  return child;
}

#endif // FLUX_LAYOUT_HPP