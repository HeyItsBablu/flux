#ifndef FLUX_COMPONENTS_HPP
#define FLUX_COMPONENTS_HPP

#include "flux_core.hpp"
#include "flux_state.hpp"
#include <cassert>
#include <memory>
#include <vector>

// ============================================================================
// COMPONENT BASE CLASS
// ============================================================================

class Component {
protected:
  FluxUI *context;
  bool initialized = false;
  bool disposed = false;

public:
  Component() : context(FluxUI::getCurrentInstance()) {
#ifdef FLUX_DEBUG
    if (!context) {
      std::cerr
          << "[FluxUI] Warning: Component created without active FluxUI context"
          << std::endl;
    }
#endif
  }

  Component(FluxUI *ctx) : context(ctx) {
    assert(ctx != nullptr && "Component requires valid FluxUI context");
  }

  virtual ~Component() {
    if (!disposed) {
      dispose();
    }
  }

  Component(const Component &) = delete;
  Component &operator=(const Component &) = delete;
  Component(Component &&) = delete;
  Component &operator=(Component &&) = delete;

  virtual WidgetPtr build() = 0;
  virtual void initState() {}
  virtual void dispose() { disposed = true; }

  template <typename T> State<T> useState(T initialValue) {
    assert(context != nullptr && "Component context is null");
    return State<T>(initialValue, context);
  }

  void rebuild() {
    if (disposed)
      return;
    if (context)
      context->rebuild();
  }

  bool isInitialized() const { return initialized; }
  bool isDisposed() const { return disposed; }
  FluxUI *getContext() const { return context; }

protected:
  void markInitialized() { initialized = true; }
  friend class ComponentBuilder;
};

// ============================================================================
// COMPONENT HOLDER WIDGET - PREVENTS CRASHES BY KEEPING COMPONENT ALIVE
// ============================================================================

class ComponentHolderWidget : public Widget {
private:
  std::shared_ptr<Component> component; // CRITICAL: Keeps component alive
  WidgetPtr childWidget;

public:
  ComponentHolderWidget(std::shared_ptr<Component> comp, WidgetPtr child)
      : component(comp), childWidget(child) {
    if (childWidget) {
      addChild(childWidget);
    }
  }

  void computeLayout(HDC hdc, int availableWidth, int availableHeight,
                     FontCache &fontCache) override {
    if (!children.empty()) {
      children[0]->computeLayout(hdc, availableWidth, availableHeight,
                                 fontCache);
      if (autoWidth)
        width = children[0]->width;
      if (autoHeight)
        height = children[0]->height;
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

  void render(HDC hdc, FontCache &fontCache) override {
    if (!children.empty()) {
      children[0]->render(hdc, fontCache);
    }
    needsPaint = false;
  }
};

// ============================================================================
// COMPONENT BUILDER
// ============================================================================

class ComponentBuilder {
public:
  template <typename TComponent, typename... Args>
  static WidgetPtr build(Args &&...args) {
    static_assert(std::is_base_of<Component, TComponent>::value,
                  "TComponent must derive from Component");

    auto component = std::make_shared<TComponent>(std::forward<Args>(args)...);

    if (!component->isInitialized()) {
      component->initState();
      component->markInitialized();
    }

    auto childWidget = component->build();

    // Wrap in holder to keep component alive
    return std::make_shared<ComponentHolderWidget>(component, childWidget);
  }

  template <typename TComponent, typename... Args>
  static std::shared_ptr<TComponent> create(Args &&...args) {
    static_assert(std::is_base_of<Component, TComponent>::value,
                  "TComponent must derive from Component");

    auto component = std::make_shared<TComponent>(std::forward<Args>(args)...);

    if (!component->isInitialized()) {
      component->initState();
      component->markInitialized();
    }

    return component;
  }
};

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

template <typename TComponent, typename... Args>
WidgetPtr BuildComponent(Args &&...args) {
  return ComponentBuilder::build<TComponent>(std::forward<Args>(args)...);
}

template <typename TComponent, typename... Args>
std::pair<WidgetPtr, std::shared_ptr<TComponent>>
ComponentWithRef(Args &&...args) {
  auto comp = ComponentBuilder::create<TComponent>(std::forward<Args>(args)...);
  auto childWidget = comp->build();
  auto holderWidget =
      std::make_shared<ComponentHolderWidget>(comp, childWidget);
  return {holderWidget, comp};
}

// ============================================================================
// MACROS
// ============================================================================

#define COMPONENT(ComponentType, ...) BuildComponent<ComponentType>(__VA_ARGS__)

#define COMPONENT_REF(ComponentType, ...)                                      \
  ComponentWithRef<ComponentType>(__VA_ARGS__)

#endif // FLUX_COMPONENTS_HPP