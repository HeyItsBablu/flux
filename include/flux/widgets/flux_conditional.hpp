#ifndef FLUX_CONDITIONAL_HPP
#define FLUX_CONDITIONAL_HPP

#include "../flux_state.hpp"

#include <functional>
#include <map>

// ============================================================================
// SWITCH-CASE WIDGET - C++ STYLE
// ============================================================================

/**
 * SwitchWidget: C++-style switch-case conditional rendering
 *
 * Usage:
 *   State<int> page(0, &app);
 *
 *   Switch(page)
 *       ->Case(0, []() { return Text("Home"); })
 *       ->Case(1, []() { return Text("Profile"); })
 *       ->Case(2, []() { return Text("Settings"); })
 *       ->Default([]() { return Text("Unknown Page"); })
 */
template <typename T> class SwitchWidget : public Widget {
private:
  State<T> *boundState = nullptr;
  std::map<T, std::function<WidgetPtr()>> cases;
  std::function<WidgetPtr()> defaultCase;
  WidgetPtr currentChild = nullptr;
  T lastValue;
  std::shared_ptr<SwitchWidget<T>> self; // Store self reference

  void rebuildChild() {
    if (!boundState)
      return;

    T currentValue = boundState->get();

    // Only rebuild if value changed
    if (currentChild && currentValue == lastValue)
      return;

    lastValue = currentValue;

    // Clear old child
    children.clear();
    currentChild = nullptr;

    // Find matching case
    auto it = cases.find(currentValue);
    if (it != cases.end() && it->second) {
      currentChild = it->second();
    } else if (defaultCase) {
      currentChild = defaultCase();
    }

    if (currentChild) {
      addChild(currentChild);
    }

    markNeedsLayout();
  }

public:
  SwitchWidget(State<T> &state) : boundState(&state) {
    lastValue = state.get();

    // Listen for state changes
    state.listen([this](T newValue) {
      rebuildChild();

      if (boundState && boundState->hasContext()) {
        auto *ui = boundState->getContext();
        if (ui) {
          ui->partialRebuild(this);
        }
      }
    });
  }

  // Store the shared_ptr to self for chaining
  void setSelf(std::shared_ptr<SwitchWidget<T>> ptr) { self = ptr; }

  /**
   * Add a case
   *
   * @example
   * Switch(state)
   *   ->Case(0, []() { return Text("Zero"); })
   *   ->Case(1, []() { return Text("One"); })
   */
  std::shared_ptr<SwitchWidget<T>> Case(T value,
                                        std::function<WidgetPtr()> builder) {
    cases[value] = builder;
    return self;
  }

  /**
   * Add default case (like default: in C++)
   *
   * @example
   * Switch(state)
   *   ->Case(0, builder0)
   *   ->Default([]() { return Text("Unknown"); })
   */
  std::shared_ptr<SwitchWidget<T>> Default(std::function<WidgetPtr()> builder) {
    defaultCase = builder;
    return self;
  }

  void computeLayout(GraphicsContext &ctx, const BoxConstraints &constraints,
                     FontCache &fontCache) override {
    rebuildChild();


    BoxConstraints self = selfConstraints(constraints);

    if (!children.empty()) {

      int maxW = autoWidth ? self.maxWidth : width;
      int maxH = autoHeight ? self.maxHeight : height;

      BoxConstraints childConstraints =
          BoxConstraints(0, maxW, 0, maxH)
              .deflate(paddingLeft + paddingRight, paddingTop + paddingBottom);


      children[0]->computeLayout(ctx, childConstraints, fontCache);


      if (autoWidth) {
        width =
            self.clampWidth(children[0]->width + paddingLeft + paddingRight);
      }
      if (autoHeight) {
        height =
            self.clampHeight(children[0]->height + paddingTop + paddingBottom);
      }
    } else {

      width = autoWidth ? self.clampWidth(0) : self.clampWidth(width);
      height = autoHeight ? self.clampHeight(0) : self.clampHeight(height);
    }

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
};

// ============================================================================
// CONDITIONAL WIDGET - TERNARY STYLE
// ============================================================================

/**
 * ConditionalWidget: Ternary-style conditional rendering
 *
 * Usage:
 *   State<bool> isLoggedIn(false, &app);
 *
 *   Conditional(isLoggedIn)
 *       ->Then([]() { return Text("Welcome back!"); })
 *       ->Else([]() { return Text("Please log in."); })
 *
 * Also works with any State<T> and a custom predicate:
 *
 *   State<int> count(0, &app);
 *
 *   Conditional(count, [](int v) { return v > 0; })
 *       ->Then([]() { return Text("Has items"); })
 *       ->Else([]() { return Text("Empty"); })
 */
template <typename T> class ConditionalWidget : public Widget {
private:
  State<T> *boundState = nullptr;
  std::function<bool(T)> predicate;
  std::function<WidgetPtr()> thenBuilder;
  std::function<WidgetPtr()> elseBuilder;
  WidgetPtr currentChild = nullptr;
  bool lastCondition;
  std::shared_ptr<ConditionalWidget<T>> self;

  void rebuildChild() {
    if (!boundState)
      return;

    bool condition = predicate(boundState->get());

    // Only rebuild if condition changed
    if (currentChild && condition == lastCondition)
      return;

    lastCondition = condition;

    // Clear old child
    children.clear();
    currentChild = nullptr;

    // Pick the appropriate branch
    auto &builder = condition ? thenBuilder : elseBuilder;
    if (builder) {
      currentChild = builder();
    }

    if (currentChild) {
      addChild(currentChild);
    }

    markNeedsLayout();
  }

public:
  ConditionalWidget(State<T> &state, std::function<bool(T)> pred)
      : boundState(&state), predicate(pred) {
    lastCondition = predicate(state.get());

    state.listen([this](T /*newValue*/) {
      rebuildChild();

      if (boundState && boundState->hasContext()) {
        auto *ui = boundState->getContext();
        if (ui) {
          ui->partialRebuild(this);
        }
      }
    });
  }

  void setSelf(std::shared_ptr<ConditionalWidget<T>> ptr) { self = ptr; }

  /**
   * Builder to render when the condition is true
   *
   * @example
   * Conditional(isLoggedIn)
   *   ->Then([]() { return Dashboard(); })
   */
  std::shared_ptr<ConditionalWidget<T>>
  Then(std::function<WidgetPtr()> builder) {
    thenBuilder = builder;
    return self;
  }

  /**
   * Builder to render when the condition is false
   *
   * @example
   * Conditional(isLoggedIn)
   *   ->Then([]() { return Dashboard(); })
   *   ->Else([]() { return LoginPage(); })
   */
  std::shared_ptr<ConditionalWidget<T>>
  Else(std::function<WidgetPtr()> builder) {
    elseBuilder = builder;
    return self;
  }

  void computeLayout(GraphicsContext &ctx, const BoxConstraints &constraints,
                     FontCache &fontCache) override {
    rebuildChild();

    if (!children.empty()) {
      children[0]->computeLayout(
          ctx,
          constraints.deflate(paddingLeft + paddingRight,
                              paddingTop + paddingBottom),
          fontCache);

      if (autoWidth)
        width = children[0]->width + paddingLeft + paddingRight;
      if (autoHeight)
        height = children[0]->height + paddingTop + paddingBottom;
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

// ============================================================================
// FACTORY FUNCTIONS
// ============================================================================

/**
 * Create a conditional widget from a bool state (no predicate needed)
 *
 * @param state State<bool> to evaluate directly
 * @return std::shared_ptr<ConditionalWidget<bool>> Chainable widget pointer
 *
 * @example
 * State<bool> isVisible(true, &app);
 *
 * Conditional(isVisible)
 *   ->Then([]() { return Text("Visible!"); })
 *   ->Else([]() { return Text("Hidden!"); })
 */
inline std::shared_ptr<ConditionalWidget<bool>>
Conditional(State<bool> &state) {
  auto widget = std::make_shared<ConditionalWidget<bool>>(
      state, [](bool v) { return v; });
  widget->setSelf(widget);
  return widget;
}

/**
 * Create a conditional widget from any State<T> with a custom predicate
 *
 * @param state State to observe
 * @param predicate Function mapping T -> bool
 * @return std::shared_ptr<ConditionalWidget<T>> Chainable widget pointer
 *
 * @example
 * State<int> itemCount(0, &app);
 *
 * Conditional(itemCount, [](int v) { return v > 0; })
 *   ->Then([]() { return ItemList(); })
 *   ->Else([]() { return EmptyState(); })
 */
inline std::shared_ptr<ConditionalWidget<std::string>>
Conditional(State<std::string> &state,
            std::function<bool(const std::string &)> predicate) {
  auto widget =
      std::make_shared<ConditionalWidget<std::string>>(state, predicate);
  widget->setSelf(widget);
  return widget;
}

template <typename T, typename Pred>
inline std::shared_ptr<ConditionalWidget<T>> Conditional(State<T> &state,
                                                         Pred predicate) {
  auto widget = std::make_shared<ConditionalWidget<T>>(
      state, std::function<bool(T)>(predicate));
  widget->setSelf(widget);
  return widget;
}
// ============================================================================
// FACTORY FUNCTION
// ============================================================================

/**
 * Create a switch-case widget (C++ style)
 *
 * @param state State to switch on
 * @return std::shared_ptr<SwitchWidget<T>> Chainable widget pointer
 *
 * @example
 * State<int> tabIndex(0, &app);
 *
 * Switch(tabIndex)
 *   ->Case(0, []() { return HomePage(); })
 *   ->Case(1, []() { return ProfilePage(); })
 *   ->Case(2, []() { return SettingsPage(); })
 *   ->Default([]() { return ErrorPage(); })
 */
template <typename T>
inline std::shared_ptr<SwitchWidget<T>> Switch(State<T> &state) {
  auto widget = std::make_shared<SwitchWidget<T>>(state);
  widget->setSelf(widget); // Store self reference for chaining
  return widget;
}

#endif // FLUX_SWITCH_HPP