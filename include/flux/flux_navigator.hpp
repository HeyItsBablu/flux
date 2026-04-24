#pragma once

// ============================================================================
// flux_navigator.hpp
// Flutter-style navigation for FluxUI
//
// Usage:
//   auto nav = Navigator::of(app);
//   nav->push(MyScreen());
//   nav->pop();
//   nav->pushReplacement(OtherScreen());
//   nav->pushNamed("/settings");
//   nav->popUntil("/home");
// ============================================================================

#include "flux/flux_core.hpp"
#include "flux/flux_app.hpp"
#include "flux/flux_state.hpp"

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <optional>
#include <any>

// ============================================================================
// ROUTE SETTINGS  (mirrors Flutter's RouteSettings)
// ============================================================================

struct RouteSettings {
    std::string name;       // e.g. "/home", "/settings"
    std::any    arguments;  // anything you want to pass

    RouteSettings() = default;
    explicit RouteSettings(std::string n, std::any args = {})
        : name(std::move(n)), arguments(std::move(args)) {}
};

// ============================================================================
// ROUTE BASE  (mirrors Flutter's Route<T>)
// ============================================================================

class NavigatorState; // forward

class Route {
public:
    RouteSettings settings;

    explicit Route(RouteSettings s = {}) : settings(std::move(s)) {}
    virtual ~Route() = default;

    // Called when this route becomes the top of the stack
    virtual void didPush(NavigatorState* /*nav*/) {}

    // Called just before this route is popped
    virtual void didPop(NavigatorState* /*nav*/) {}

    // Called when a route above this one is popped (we become visible again)
    virtual void didPopNext(NavigatorState* /*nav*/) {}

    // Build the widget that fills the screen
    virtual WidgetPtr build(FluxUI* app) = 0;
};

using RoutePtr = std::shared_ptr<Route>;

// ============================================================================
// WIDGET ROUTE  — wraps a plain factory lambda (most common case)
// ============================================================================

class WidgetRoute : public Route {
    std::function<WidgetPtr(FluxUI*)> _builder;
public:
    WidgetRoute(std::function<WidgetPtr(FluxUI*)> builder,
                RouteSettings s = {})
        : Route(std::move(s)), _builder(std::move(builder)) {}

    WidgetPtr build(FluxUI* app) override { return _builder(app); }
};

// ============================================================================
// NAMED ROUTE TABLE
// ============================================================================

using RouteFactory = std::function<RoutePtr(const RouteSettings&)>;
using RouteTable   = std::unordered_map<std::string, RouteFactory>;

// ============================================================================
// NAVIGATOR STATE
// ============================================================================

class NavigatorState {
public:
    // Injected by the owning NavigatorWidget
    FluxUI*   app       = nullptr;
    Widget*   hostWidget = nullptr;   // NavigatorWidget* as Widget*
    RouteTable routeTable;
    std::string initialRoute = "/";

    // ── Stack ────────────────────────────────────────────────────────────────
    std::vector<RoutePtr> _stack;

    // ── History (names only, for popUntil) ───────────────────────────────────
    std::vector<std::string> _nameHistory;

    // ── Rebuild callback set by NavigatorWidget ──────────────────────────────
    std::function<void()> _requestRebuild;

    // ────────────────────────────────────────────────────────────────────────
    // push  —  add a route on top of the stack
    // ────────────────────────────────────────────────────────────────────────
    void push(RoutePtr route) {
        route->didPush(this);
        _stack.push_back(route);
        _nameHistory.push_back(route->settings.name);
        _rebuild();
    }

    // Convenience: push from a builder lambda
    void push(std::function<WidgetPtr(FluxUI*)> builder,
              RouteSettings settings = {}) {
        push(std::make_shared<WidgetRoute>(std::move(builder),
                                          std::move(settings)));
    }

    // Convenience: push a widget directly (no FluxUI* needed in builder)
    void pushWidget(WidgetPtr widget, RouteSettings settings = {}) {
        push([w = std::move(widget)](FluxUI*) { return w; },
             std::move(settings));
    }

    // ── Named navigation ─────────────────────────────────────────────────────
    bool pushNamed(const std::string& name, std::any arguments = {}) {
        auto it = routeTable.find(name);
        if (it == routeTable.end()) return false;
        RouteSettings s(name, std::move(arguments));
        push(it->second(s));
        return true;
    }

    // ── pushReplacement  ─────────────────────────────────────────────────────
    void pushReplacement(RoutePtr route) {
        if (!_stack.empty()) {
            _stack.back()->didPop(this);
            _stack.pop_back();
            if (!_nameHistory.empty()) _nameHistory.pop_back();
        }
        push(std::move(route));
    }

    void pushReplacement(std::function<WidgetPtr(FluxUI*)> builder,
                         RouteSettings settings = {}) {
        pushReplacement(std::make_shared<WidgetRoute>(std::move(builder),
                                                      std::move(settings)));
    }

    bool pushReplacementNamed(const std::string& name, std::any arguments = {}) {
        auto it = routeTable.find(name);
        if (it == routeTable.end()) return false;
        RouteSettings s(name, std::move(arguments));
        pushReplacement(it->second(s));
        return true;
    }

    // ── pushAndRemoveUntil  ──────────────────────────────────────────────────
    // Push a new route and remove all routes below it that match the predicate.
    void pushAndRemoveUntil(RoutePtr route,
                            std::function<bool(RoutePtr)> predicate) {
        while (!_stack.empty() && !predicate(_stack.back())) {
            _stack.back()->didPop(this);
            _stack.pop_back();
            if (!_nameHistory.empty()) _nameHistory.pop_back();
        }
        push(std::move(route));
    }

    // Predicate helpers
    static auto removeAll() {
        return [](RoutePtr) { return false; }; // never stop → clears stack
    }
    static auto removeUntilNamed(const std::string& name) {
        return [name](RoutePtr r) { return r->settings.name == name; };
    }

    // ── pop  ─────────────────────────────────────────────────────────────────
    bool canPop() const { return _stack.size() > 1; }

    bool pop() {
        if (!canPop()) return false;
        _stack.back()->didPop(this);
        _stack.pop_back();
        if (!_nameHistory.empty()) _nameHistory.pop_back();
        if (!_stack.empty()) _stack.back()->didPopNext(this);
        _rebuild();
        return true;
    }

    // ── popUntil  ────────────────────────────────────────────────────────────
    void popUntil(std::function<bool(RoutePtr)> predicate) {
        bool changed = false;
        while (canPop() && !predicate(_stack.back())) {
            _stack.back()->didPop(this);
            _stack.pop_back();
            if (!_nameHistory.empty()) _nameHistory.pop_back();
            changed = true;
        }
        if (changed) {
            if (!_stack.empty()) _stack.back()->didPopNext(this);
            _rebuild();
        }
    }

    void popUntilNamed(const std::string& name) {
        popUntil([&name](RoutePtr r) { return r->settings.name == name; });
    }

    // ── maybePop (safe: won't pop if only one route left) ───────────────────
    bool maybePop() { return canPop() ? pop() : false; }

    // ── Accessors ─────────────────────────────────────────────────────────────
    RoutePtr currentRoute() const {
        return _stack.empty() ? nullptr : _stack.back();
    }

    std::string currentRouteName() const {
        auto r = currentRoute();
        return r ? r->settings.name : "";
    }

    int stackDepth() const { return static_cast<int>(_stack.size()); }

    // ── Template argument accessors ──────────────────────────────────────────
    template<typename T>
    std::optional<T> currentArguments() const {
        auto r = currentRoute();
        if (!r || !r->settings.arguments.has_value()) return std::nullopt;
        try { return std::any_cast<T>(r->settings.arguments); }
        catch (...) { return std::nullopt; }
    }

private:
    void _rebuild() {
        if (_requestRebuild) _requestRebuild();
    }
};

using NavigatorStatePtr = std::shared_ptr<NavigatorState>;

// ============================================================================
// NAVIGATOR WIDGET
// Owns the NavigatorState and renders the top-of-stack route.
// Drop this anywhere in your widget tree (usually as the Scaffold body).
// ============================================================================

class NavigatorWidget : public Widget {
    NavigatorStatePtr _state;
    bool              _contentReady = false; // guard: don't rebuild during layout

public:
    explicit NavigatorWidget(NavigatorStatePtr state)
        : _state(std::move(state))
    {
        _state->hostWidget      = this;

        // Called by NavigatorState after every push / pop
        _state->_requestRebuild = [this]() { _swapContent(); };

        // Eagerly install the initial route's widget into children[0]
        // before the first layout pass so LayoutEngine finds it normally.
        _installRoute();
    }

    NavigatorStatePtr getState() const { return _state; }

    // ── Layout: expand to fill, then constrain child tightly ─────────────────
    void computeLayout(GraphicsContext& ctx,
                       const BoxConstraints& constraints,
                       FontCache& fontCache) override
    {
        if (autoWidth)  width  = constraints.maxWidth;
        if (autoHeight) height = constraints.maxHeight;

        if (!children.empty()) {
            children[0]->computeLayout(
                ctx, BoxConstraints::tight(width, height), fontCache);
        }
        applyConstraints();
        needsLayout = false;
    }

    // ── Position: place child at our origin ──────────────────────────────────
    void positionChildren(int cx, int cy, int /*cw*/, int /*ch*/) override {
        if (!children.empty()) {
            auto& child = children[0];
            child->x = cx;
            child->y = cy;
            child->positionChildren(
                cx + child->paddingLeft,
                cy + child->paddingTop,
                child->width  - child->paddingLeft - child->paddingRight,
                child->height - child->paddingTop  - child->paddingBottom);
        }
    }

    // ── Render: delegate to child (Widget::render already does this) ──────────
    // No override needed — base Widget::render iterates children.

private:
    // Install whatever is on top of the stack into children[0].
    // Safe to call before the first layout.
    void _installRoute() {
        // Detach previous child
        if (!children.empty()) {
            children[0]->parent = nullptr;
            children[0]->onDetach();
            children.clear();
        }

        auto route = _state->currentRoute();
        if (!route) return;

        auto content = route->build(_state->app);
        if (content) {
            content->parent = this;
            children.push_back(content);
        }
    }

    // Called after push / pop — swap child and request a full re-layout
    // through FluxUI so positions are recalculated properly.
    void _swapContent() {
        _installRoute();

        // Re-layout through FluxUI if it's running; otherwise markNeedsLayout
        // so the next paint pass picks it up.
        if (auto* ui = FluxUI::getCurrentInstance()) {
            ui->partialRebuild(this);
        } else {
            markNeedsLayout();
        }
    }
};

// ============================================================================
// NAVIGATOR  — static factory / helper (mirrors Flutter's Navigator class)
// ============================================================================

class Navigator {
public:
    // Create a NavigatorState with optional named routes and initial route
    static NavigatorStatePtr create(
        FluxUI*           app,
        RouteTable        routes     = {},
        const std::string initialRoute = "/")
    {
        auto state         = std::make_shared<NavigatorState>();
        state->app         = app;
        state->routeTable  = std::move(routes);
        state->initialRoute = initialRoute;

        // Push initial route if registered
        auto it = state->routeTable.find(initialRoute);
        if (it != state->routeTable.end()) {
            RouteSettings s(initialRoute);
            state->_stack.push_back(it->second(s));
            state->_nameHistory.push_back(initialRoute);
        }
        return state;
    }

    // Build the NavigatorWidget
    static WidgetPtr widget(NavigatorStatePtr state) {
        return std::make_shared<NavigatorWidget>(std::move(state));
    }

    // Convenience: create state + widget in one call
    static std::pair<NavigatorStatePtr, WidgetPtr>
    create_with_widget(FluxUI*           app,
                       RouteTable        routes      = {},
                       const std::string initialRoute = "/")
    {
        auto state = create(app, std::move(routes), initialRoute);
        auto w     = widget(state);
        return { state, w };
    }
};

// ============================================================================
// ROUTE HELPERS 
// ============================================================================

// Build a WidgetRoute from a lambda
inline RoutePtr makeRoute(std::function<WidgetPtr(FluxUI*)> builder,
                          RouteSettings settings = {}) {
    return std::make_shared<WidgetRoute>(std::move(builder), std::move(settings));
}

// Build a WidgetRoute from a pre-built widget (no FluxUI* needed)
inline RoutePtr makeWidgetRoute(WidgetPtr w, RouteSettings settings = {}) {
    return makeRoute([w](FluxUI*) { return w; }, std::move(settings));
}