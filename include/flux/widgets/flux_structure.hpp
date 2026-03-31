#ifndef FLUX_STRUCTURE_HPP
#define FLUX_STRUCTURE_HPP

#include "../flux_core.hpp"

#include "flux_layout.hpp"
#include "../flux_state.hpp"
#include <iostream>

// ============================================================================
// CONCRETE WIDGET CLASSES
// ============================================================================

struct OverlayEntry {
  Widget *widget = nullptr;
  std::function<void(GraphicsContext &, FontCache &)>
      renderer; // nullptr for popup overlays
  int zIndex = 0;
 
  // Full entry — widget renders into back-buffer
  OverlayEntry(Widget *w, std::function<void(GraphicsContext &, FontCache &)> r,
               int z = 0)
      : widget(w), renderer(r), zIndex(z) {}

  // Hit-target only — visual is in a popup window
  OverlayEntry(Widget *w, int z = 0)
      : widget(w), renderer(nullptr), zIndex(z) {}
};

// --- Scaffold Widget ---
class ScaffoldWidget : public Widget {
private:
  std::vector<OverlayEntry> overlayStack;

  //   // Overlays in ascending zIndex order (lowest first, highest last = on
  //   top)
  // for (const auto &entry : overlayStack) {
  //   if (entry.renderer)
  //     entry.renderer(hdc, fontCache);
  // }

  void sortOverlayStack() {
    std::stable_sort(overlayStack.begin(), overlayStack.end(),
                     [](const OverlayEntry &a, const OverlayEntry &b) {
                       return a.zIndex < b.zIndex;
                     });
  }

public:
  void addOverlayHitTarget(Widget *widget, int zIndex = 0) {
    for (auto &entry : overlayStack) {
      if (entry.widget == widget) {
        entry.zIndex = zIndex;
        sortOverlayStack();
        return;
      }
    }
    overlayStack.emplace_back(widget, zIndex);
    sortOverlayStack();
    markNeedsPaint();
  }

  // --- Overlay Management (moved from FluxAppWidget) ---
  void addOverlay(Widget *widget,
                  std::function<void(GraphicsContext &, FontCache &)> renderer,
                  int zIndex = 0) {
    for (auto &entry : overlayStack) {
      if (entry.widget == widget) {
        entry.renderer = renderer;
        entry.zIndex = zIndex;
        sortOverlayStack();
        markNeedsPaint();
        return;
      }
    }
    overlayStack.emplace_back(widget, renderer, zIndex);
    sortOverlayStack();
    markNeedsPaint();
  }

  void removeOverlay(Widget *widget) {
    overlayStack.erase(std::remove_if(overlayStack.begin(), overlayStack.end(),
                                      [widget](const OverlayEntry &e) {
                                        return e.widget == widget;
                                      }),
                       overlayStack.end());
    markNeedsPaint();
  }

  void clearOverlays() {
    overlayStack.clear();
    markNeedsPaint();
  }
  bool hasOverlays() const { return !overlayStack.empty(); }

  const std::vector<OverlayEntry> &getOverlayStack() const {
    return overlayStack;
  }

  Widget *getTopmostOverlay() const {
    return overlayStack.empty() ? nullptr : overlayStack.back().widget;
  }

  // --- Layout ---
  void computeLayout(GraphicsContext &ctx, const BoxConstraints &constraints,
                     FontCache &fontCache) override {
    if (autoWidth)
      width = constraints.maxWidth;
    if (autoHeight)
      height = constraints.maxHeight;

    BoxConstraints childConstraints = BoxConstraints::tight(width, height);
    for (auto &child : children)
      child->computeLayout(ctx, childConstraints, fontCache);

    applyConstraints();
    needsLayout = false;
  }

  // --- Render (overlays painted after normal tree) ---
  void render(GraphicsContext &ctx, FontCache &fontCache) override {
    for (auto &child : children)
      child->render(ctx, fontCache);

    // only call renderer if present — popup overlays have nullptr here
    for (const auto &entry : overlayStack)
      if (entry.renderer)
        entry.renderer(ctx, fontCache);

    needsPaint = false;
  }
};

// --- AppBar Widget ---
class AppBarWidget : public Widget {
public:
  void computeLayout(GraphicsContext &ctx, const BoxConstraints &constraints,
                     FontCache &fontCache) override {
    if (autoWidth)
      width = constraints.maxWidth;

    if (!children.empty()) {
      children[0]->computeLayout(
          ctx,
          BoxConstraints::loose(width - paddingLeft - paddingRight,
                                height - paddingTop - paddingBottom),
          fontCache);
    }

    applyConstraints();
    needsLayout = false;
  }

  void positionChildren(int contentX, int contentY, int /*contentWidth*/,
                        int /*contentHeight*/) override {
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
  w->height = 56;
  w->autoHeight = false;

  auto titleWidget = Text(title)
                         ->setFontSize(20)
                         ->setFontWeight(FontWeight::Bold)
                         ->setTextColor(Color::fromRGB(255, 255, 255))
                         ->setPadding(20);

  w->addChild(titleWidget);

  return w;
}

inline WidgetPtr Scaffold(WidgetPtr appBar = nullptr,
                          WidgetPtr body = nullptr) {
  auto w = std::make_shared<ScaffoldWidget>();

  w->hasBackground = true;
  w->backgroundColor = Color::fromRGB(250, 250, 250);

  auto column = std::make_shared<ColumnWidget>();
  column->setSpacing(0);

  if (appBar) {
    column->addChild(appBar);
  }

  if (body) {
    column->addChild(Expanded(body));
  }

  w->addChild(column);

  return w;
}

#endif