#ifndef FLUX_CONDITIONAL_HPP
#define FLUX_CONDITIONAL_HPP

#include "../flux_state.hpp"

#include <functional>
#include <map>

// ============================================================================
// SWITCH-CASE WIDGET
// ============================================================================
//
// Renders the child matching the current state value.
// The active branch is always rebuilt on every state change — so data-driven
// builders (e.g. a MemoryImage fed by the same state) always reflect the
// latest value, even if the matched case key is unchanged.
//
// Usage:
//   State<int> page(0, &app);
//
//   Switch(page)
//       ->Case(0, []() { return Text("Home"); })
//       ->Case(1, []() { return Text("Profile"); })
//       ->Case(2, []() { return Text("Settings"); })
//       ->Default([]() { return Text("Unknown Page"); })
//
// ============================================================================

template <typename T>
class SwitchWidget : public Widget {
private:
    State<T>* boundState = nullptr;

    std::map<T, std::function<WidgetPtr()>> cases;
    std::function<WidgetPtr()>              defaultCase;

    WidgetPtr currentChild = nullptr;

    // ── Change tracking ───────────────────────────────────────────────────
    // lastKey tracks which case branch is active.
    // forceRebuild is set by the state listener so that even when the key
    // does not change (same branch, different data) the child is rebuilt.
    T    lastKey;
    bool forceRebuild = true;   // true on first layout pass

    std::shared_ptr<SwitchWidget<T>> self;

    // ── rebuildChild ──────────────────────────────────────────────────────
    // Always called from the UI thread (computeLayout or state listener).
    void rebuildChild() {
        if (!boundState) return;

        T currentKey = boundState->get();
        bool keyChanged = !(currentKey == lastKey);

        // Rebuild if:
        //   • this is the first build (forceRebuild)
        //   • the active branch switched (keyChanged)
        //   • the state value changed even though the branch is the same
        //     (forceRebuild set by the listener, covers the MemoryImage case)
        if (!forceRebuild && !keyChanged && currentChild)
            return;

        lastKey      = currentKey;
        forceRebuild = false;

        children.clear();
        currentChild = nullptr;

        auto it = cases.find(currentKey);
        if (it != cases.end() && it->second)
            currentChild = it->second();
        else if (defaultCase)
            currentChild = defaultCase();

        if (currentChild)
            addChild(currentChild);

        markNeedsLayout();
    }

public:
    explicit SwitchWidget(State<T>& state) : boundState(&state) {
        lastKey = state.get();

        // Listen: set forceRebuild so the active branch is always re-run,
        // then ask the framework to re-layout this subtree.
        state.listen([this](const T&) {
            forceRebuild = true;
            rebuildChild();

            if (boundState && boundState->hasContext())
                if (auto* ui = boundState->getContext())
                    ui->partialRebuild(this);
        });
    }

    void setSelf(std::shared_ptr<SwitchWidget<T>> ptr) { self = ptr; }

    std::shared_ptr<SwitchWidget<T>> Case(T value,
                                          std::function<WidgetPtr()> builder) {
        cases[value] = std::move(builder);
        return self;
    }

    std::shared_ptr<SwitchWidget<T>> Default(std::function<WidgetPtr()> builder) {
        defaultCase = std::move(builder);
        return self;
    }

    // ── Layout ────────────────────────────────────────────────────────────
    void computeLayout(GraphicsContext& ctx, const BoxConstraints& constraints,
                       FontCache& fontCache) override {
        rebuildChild();

        BoxConstraints self = selfConstraints(constraints);

        if (!children.empty()) {
            int maxW = autoWidth  ? self.maxWidth  : width;
            int maxH = autoHeight ? self.maxHeight : height;

            BoxConstraints childC =
                BoxConstraints(0, maxW, 0, maxH)
                    .deflate(paddingLeft + paddingRight,
                             paddingTop  + paddingBottom);

            children[0]->computeLayout(ctx, childC, fontCache);

            if (autoWidth)
                width  = self.clampWidth(children[0]->width  + paddingLeft + paddingRight);
            if (autoHeight)
                height = self.clampHeight(children[0]->height + paddingTop  + paddingBottom);
        } else {
            width  = autoWidth  ? self.clampWidth(0)  : self.clampWidth(width);
            height = autoHeight ? self.clampHeight(0) : self.clampHeight(height);
        }

        needsLayout = false;
    }

    void positionChildren(int contentX, int contentY,
                          int /*contentWidth*/, int /*contentHeight*/) override {
        if (!children.empty()) {
            auto& child = children[0];
            child->x = contentX;
            child->y = contentY;
            child->positionChildren(
                child->x + child->paddingLeft,
                child->y + child->paddingTop,
                child->width  - child->paddingLeft - child->paddingRight,
                child->height - child->paddingTop  - child->paddingBottom);
        }
    }
};

// ============================================================================
// CONDITIONAL WIDGET  (ternary style)
// ============================================================================
//
// Evaluates a predicate against a State<T> and renders the Then or Else branch.
// The active branch is always rebuilt on every state change — so builders that
// read the same (or any other) state always produce fresh widgets.
//
// Usage:
//   State<bool> isLoggedIn(false, &app);
//
//   Conditional(isLoggedIn)
//       ->Then([]() { return Text("Welcome back!"); })
//       ->Else([]() { return Text("Please log in."); })
//
//   // With a custom predicate:
//   State<std::vector<uint8_t>> bytes({}, &app);
//
//   Conditional(bytes, [](const auto& v) { return !v.empty(); })
//       ->Then([&]() { return MemoryImage(bytes.get())->setFit(...); })
//       ->Else([]()  { return Placeholder(); })
//
// ============================================================================

template <typename T>
class ConditionalWidget : public Widget {
private:
    State<T>*               boundState = nullptr;
    std::function<bool(const T&)> predicate;

    std::function<WidgetPtr()> thenBuilder;
    std::function<WidgetPtr()> elseBuilder;

    WidgetPtr currentChild = nullptr;

    // ── Change tracking ───────────────────────────────────────────────────
    bool lastCondition = false;
    bool forceRebuild  = true;  // true on first layout pass

    std::shared_ptr<ConditionalWidget<T>> self;

    // ── rebuildChild ──────────────────────────────────────────────────────
    void rebuildChild() {
        if (!boundState) return;

        bool condition    = predicate(boundState->get());
        bool branchChanged = (condition != lastCondition);

        // Rebuild if:
        //   • first build
        //   • active branch switched (Then ↔ Else)
        //   • forceRebuild set by listener (same branch, new data)
        if (!forceRebuild && !branchChanged && currentChild)
            return;

        lastCondition = condition;
        forceRebuild  = false;

        children.clear();
        currentChild = nullptr;

        auto& builder = condition ? thenBuilder : elseBuilder;
        if (builder)
            currentChild = builder();

        if (currentChild)
            addChild(currentChild);

        markNeedsLayout();
    }

public:
    ConditionalWidget(State<T>& state, std::function<bool(const T&)> pred)
        : boundState(&state), predicate(std::move(pred)) {
        lastCondition = predicate(state.get());

        // Listen: always force a rebuild of the active branch.
        state.listen([this](const T&) {
            forceRebuild = true;
            rebuildChild();

            if (boundState && boundState->hasContext())
                if (auto* ui = boundState->getContext())
                    ui->partialRebuild(this);
        });
    }

    void setSelf(std::shared_ptr<ConditionalWidget<T>> ptr) { self = ptr; }

    std::shared_ptr<ConditionalWidget<T>> Then(std::function<WidgetPtr()> builder) {
        thenBuilder = std::move(builder);
        return self;
    }

    std::shared_ptr<ConditionalWidget<T>> Else(std::function<WidgetPtr()> builder) {
        elseBuilder = std::move(builder);
        return self;
    }

    // ── Layout ────────────────────────────────────────────────────────────
    void computeLayout(GraphicsContext& ctx, const BoxConstraints& constraints,
                       FontCache& fontCache) override {
        rebuildChild();

        if (!children.empty()) {
            children[0]->computeLayout(
                ctx,
                constraints.deflate(paddingLeft + paddingRight,
                                    paddingTop  + paddingBottom),
                fontCache);

            if (autoWidth)
                width  = children[0]->width  + paddingLeft + paddingRight;
            if (autoHeight)
                height = children[0]->height + paddingTop  + paddingBottom;
        }

        applyConstraints();
        needsLayout = false;
    }

    void positionChildren(int contentX, int contentY,
                          int /*contentWidth*/, int /*contentHeight*/) override {
        if (!children.empty()) {
            auto& child = children[0];
            child->x = contentX;
            child->y = contentY;
            child->positionChildren(
                child->x + child->paddingLeft,
                child->y + child->paddingTop,
                child->width  - child->paddingLeft - child->paddingRight,
                child->height - child->paddingTop  - child->paddingBottom);
        }
    }
};

// ============================================================================
// FACTORY FUNCTIONS
// ============================================================================

// Bool state — no predicate needed
inline std::shared_ptr<ConditionalWidget<bool>>
Conditional(State<bool>& state) {
    auto w = std::make_shared<ConditionalWidget<bool>>(
        state, [](const bool& v) { return v; });
    w->setSelf(w);
    return w;
}

// Any State<T> with a custom predicate (const T& overload — preferred)
template <typename T, typename Pred>
inline std::shared_ptr<ConditionalWidget<T>>
Conditional(State<T>& state, Pred predicate) {
    auto w = std::make_shared<ConditionalWidget<T>>(
        state, std::function<bool(const T&)>(predicate));
    w->setSelf(w);
    return w;
}

// Switch factory
template <typename T>
inline std::shared_ptr<SwitchWidget<T>>
Switch(State<T>& state) {
    auto w = std::make_shared<SwitchWidget<T>>(state);
    w->setSelf(w);
    return w;
}

#endif // FLUX_CONDITIONAL_HPP