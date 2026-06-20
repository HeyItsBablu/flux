#ifndef FLUX_STRUCTURE_HPP
#define FLUX_STRUCTURE_HPP

#include "../flux_core.hpp"

#include "flux_layout.hpp"
#include "../flux_state.hpp"
#include <iostream>


class ToastWidget;  

// ============================================================================
// CONCRETE WIDGET CLASSES
// ============================================================================
class ScaffoldWidget : public Widget {
private:
  std::shared_ptr<FABWidget> fab_;

public:
  void setFAB(std::shared_ptr<FABWidget> f) {
    fab_ = f;
    if (f) addChild(f);
    markNeedsLayout();
  }

  void computeLayout(GraphicsContext &ctx, const BoxConstraints &constraints, FontCache &fontCache) override {
    if (autoWidth)  width  = constraints.maxWidth;
    if (autoHeight) height = constraints.maxHeight;
    BoxConstraints childConstraints = BoxConstraints::tight(width, height);
    for (auto &child : children) {
      if (child.get() == fab_.get()) continue;
      child->computeLayout(ctx, childConstraints, fontCache);
    }
    if (fab_) {
      fab_->computeLayout(ctx, childConstraints, fontCache);
      fab_->positionInScaffold(x, y, width, height);
    }
    applyConstraints();
    needsLayout = false;
  }

  void render(GraphicsContext &ctx, FontCache &fontCache) override {
    for (auto &child : children) {
      if (child.get() == fab_.get()) continue;
      child->render(ctx, fontCache);
    }
    if (fab_) fab_->render(ctx, fontCache);
    needsPaint = false;
  }
};


// --- AppBar Widget ---
class AppBarWidget : public Widget {
public:

  static constexpr int kDefaultHeight = 56;

  AppBarWidget() {
    height     = kDefaultHeight;
    autoHeight = false;   // AppBar height is always fixed — never shrink-wrap
  }

  void computeLayout(GraphicsContext &ctx, const BoxConstraints &constraints,
                     FontCache &fontCache) override {
    if (autoWidth)
      width = constraints.maxWidth;

    if (!children.empty()) {
      children[0]->computeLayout(
          ctx,
          BoxConstraints::loose(width  - paddingLeft - paddingRight,
                                height - paddingTop  - paddingBottom),
          fontCache);
    }

    applyConstraints();
    needsLayout = false;
  }

  void positionChildren(int contentX, int contentY,
                        int contentWidth, int contentHeight) override {
    if (!children.empty()) {
      auto &child = children[0];

      // Center horizontally; fall back to left if title is too wide.
      int cx = (child->width <= contentWidth)
             ? contentX + (contentWidth - child->width) / 2
             : contentX;

      // Center vertically.
      int cy = contentY + (contentHeight - child->height) / 2;

      child->x = cx;
      child->y = cy;

      child->positionChildren(
          child->x + child->paddingLeft,
          child->y + child->paddingTop,
          child->width  - child->paddingLeft - child->paddingRight,
          child->height - child->paddingTop  - child->paddingBottom);
    }
  }
};

using ContainerWidgetPtr = std::shared_ptr<ContainerWidget>;

inline ContainerWidgetPtr Card(WidgetPtr child) {
  auto w = std::make_shared<ContainerWidget>();
  w->hasBackground = true;
  w->backgroundColor = Color::fromRGB(255, 255, 255);
  w->hasBorder = true;
  w->borderColor = Color::fromRGB(224, 224, 224);
  w->borderWidth = 1;
  w->borderRadius = 8;
  w->paddingLeft = w->paddingRight = w->paddingTop = w->paddingBottom = 16;
  if (child)
    w->addChild(child);
  return w;
}

inline WidgetPtr AppBar(const std::string &title) {
  auto w = std::make_shared<AppBarWidget>();

  w->hasBackground = true;
  w->backgroundColor = Color::fromRGB(33, 150, 243);

  auto titleWidget = Text(title)
                         ->setFontSize(20)
                         ->setFontWeight(FontWeight::Bold)
                         ->setTextColor(Color::fromRGB(255, 255, 255));

  w->addChild(titleWidget);
  return w;
}

inline WidgetPtr Scaffold(WidgetPtr appBar  = nullptr,
                          WidgetPtr body    = nullptr,
                          std::shared_ptr<FABWidget>   fab   = nullptr,
                          std::shared_ptr<ToastWidget> toast = nullptr) {
    auto w = std::make_shared<ScaffoldWidget>();
    w->hasBackground   = true;
    w->backgroundColor = Color::fromRGB(250, 250, 250);

    auto column = std::make_shared<ColumnWidget>();
    column->setSpacing(0);
    if (appBar) column->addChild(appBar);
    if (body)   column->addChild(body);
    w->addChild(column);

    if (fab)   w->setFAB(fab);
    if (toast) w->addChild(std::static_pointer_cast<Widget>(toast));

    return w;
}

inline WidgetPtr Scaffold(WidgetPtr appBar = nullptr,
                          WidgetPtr body   = nullptr,
                          std::shared_ptr<FABWidget> fab = nullptr) {
    auto w = std::make_shared<ScaffoldWidget>();
    w->hasBackground    = true;
    w->backgroundColor  = Color::fromRGB(250, 250, 250);

    auto column = std::make_shared<ColumnWidget>();
    column->setSpacing(0);
    if (appBar) column->addChild(appBar);
    if (body)   column->addChild(body);
    w->addChild(column);

    if (fab) w->setFAB(fab);  // ADD THIS

    return w;
}

inline WidgetPtr Scaffold(WidgetPtr appBar = nullptr,
                          WidgetPtr body   = nullptr) {
  auto w = std::make_shared<ScaffoldWidget>();

  w->hasBackground = true;
  w->backgroundColor = Color::fromRGB(250, 250, 250);

  auto column = std::make_shared<ColumnWidget>();
  column->setSpacing(0);

  if (appBar)
    column->addChild(appBar);

  if (body)
    column->addChild(body);

  w->addChild(column);
  return w;
}

#endif