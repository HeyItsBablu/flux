#pragma once

// ============================================================================
// flux_navigator.hpp
//
// All routes must be declared upfront in Navigator::init().
// Navigation is only possible via named routes (except pop/maybePop).
//
// Setup — one change in createApp():
//   return FluxApp("Title",
//     Navigator::init({
//       {"/"         , [] { return std::make_shared<HomePage>();    }},
//       {"/settings" , [] { return std::make_shared<Settings>();    }},
//       {"/dashboard", [] { return std::make_shared<Dashboard>();   }},
//     }, "/")   // <-- initial route name
//   );
//
// Then anywhere in your app:
//   Navigator::navigate("/settings");
//   Navigator::pushReplacementNamed("/dashboard");
//   Navigator::pushAndRemoveAllNamed("/");
//   Navigator::popUntil("/");
//   Navigator::pop();
//   Navigator::maybePop();
//
// NOTE: Navigator::push(widget) and Navigator::pushReplacement(widget)
//       are intentionally removed. All navigation must use named routes.
// ============================================================================

#include "flux/flux_core.hpp"
#include "flux/flux_app.hpp"

#include <functional>
#include <initializer_list>
#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class NavigatorWidget;

struct NavEntry
{
    WidgetPtr   widget;
    std::string name;
};

// ============================================================================
// ROUTE TABLE ENTRY
// Passed as an initializer list to Navigator::init().
// ============================================================================

struct RouteDefinition
{
    using Builder = std::function<WidgetPtr()>;

    std::string name;
    Builder     builder;

    // Allows brace-list syntax: { "/path", [] { return ...; } }
    RouteDefinition(std::string n, Builder b)
        : name(std::move(n)), builder(std::move(b)) {}
};

// ============================================================================
// NAVIGATOR
// ============================================================================

class Navigator
{
public:
    using RouteBuilder = std::function<WidgetPtr()>;

    // ── One-time setup inside createApp() ────────────────────────────────────
    //
    // All routes must be registered here. No route can be pushed that was not
    // declared in this call. The initialRoute must match one of the entries.

    static WidgetPtr init(
        std::initializer_list<RouteDefinition> routes,
        const std::string &initialRoute = "/")
    {
        _routes.clear();
        _stack.clear();

        for (auto &def : routes)
        {
#ifdef FLUX_DEBUG
            if (_routes.count(def.name))
                std::cerr << "[Navigator] init: duplicate route \""
                          << def.name << "\" — last definition wins.\n";
#endif
            _routes[def.name] = def.builder;
        }

        // Push the initial route onto the stack.
        auto it = _routes.find(initialRoute);
        if (it == _routes.end())
        {
            std::cerr << "[Navigator] init: initialRoute \""
                      << initialRoute << "\" not found in route table.\n";
        }
        else
        {
            _stack.push_back({it->second(), initialRoute});
        }

        return std::static_pointer_cast<Widget>(std::make_shared<NavigatorWidget>());
    }

    // ── Named navigation ─────────────────────────────────────────────────────

    // Push a new instance of the named route onto the stack.
    static bool navigate(const std::string &name)
    {
        auto *builder = findRoute(name, "navigate");
        if (!builder) return false;
        if (!checkHost()) return false;

        _stack.push_back({(*builder)(), name});
        _rebuild();
        return true;
    }

    // Replace the top of the stack with a new instance of the named route.
    static bool pushReplacementNamed(const std::string &name)
    {
        auto *builder = findRoute(name, "pushReplacementNamed");
        if (!builder) return false;
        if (!checkHost()) return false;

        if (!_stack.empty()) _stack.pop_back();
        _stack.push_back({(*builder)(), name});
        _rebuild();
        return true;
    }

    // Clear the entire stack and push the named route as the new root.
    static bool pushAndRemoveAllNamed(const std::string &name)
    {
        auto *builder = findRoute(name, "pushAndRemoveAllNamed");
        if (!builder) return false;
        if (!checkHost()) return false;

        _stack.clear();
        _stack.push_back({(*builder)(), name});
        _rebuild();
        return true;
    }

    // ── Pop ───────────────────────────────────────────────────────────────────

    static bool pop()
    {
        if (!checkHost() || _stack.size() <= 1) return false;
        _stack.pop_back();
        _rebuild();
        return true;
    }

    static bool maybePop() { return canPop() ? pop() : false; }

    // Pop until the stack top matches the given route name.
    // If the name is not in the stack, does nothing.
    static void popUntil(const std::string &name)
    {
        if (!checkHost()) return;
        while (_stack.size() > 1 && _stack.back().name != name)
            _stack.pop_back();
        _rebuild();
    }

    // ── Queries ───────────────────────────────────────────────────────────────

    static bool        canPop()      { return _stack.size() > 1; }
    static int         stackDepth()  { return static_cast<int>(_stack.size()); }
    static std::string currentName() { return _stack.empty() ? "" : _stack.back().name; }

    // Returns true if the given route name exists in the route table.
    static bool hasRoute(const std::string &name)
    {
        return _routes.count(name) > 0;
    }

    // ── Internal ─────────────────────────────────────────────────────────────

    static void    _setHost(NavigatorWidget *w) { _host = w; }
    static void    _clearHost(NavigatorWidget *w) { if (_host == w) _host = nullptr; }
    static WidgetPtr _currentWidget() { return _stack.empty() ? nullptr : _stack.back().widget; }
    static void    _rebuild();

private:
    static NavigatorWidget                              *_host;
    static std::vector<NavEntry>                         _stack;
    static std::unordered_map<std::string, RouteBuilder> _routes;

    // Returns a pointer to the builder for the given route, or nullptr.
    static const RouteBuilder *findRoute(const std::string &name,
                                         const char *callerName)
    {
        auto it = _routes.find(name);
        if (it != _routes.end()) return &it->second;

#ifdef FLUX_DEBUG
        std::cerr << "[Navigator] " << callerName
                  << ": unknown route \"" << name << "\". "
                     "Did you register it in Navigator::init()?\n";
#endif
        return nullptr;
    }

    static bool checkHost()
    {
        if (!_host)
        {
#ifdef FLUX_DEBUG
            std::cerr << "[Navigator] No active NavigatorWidget. "
                         "Did you pass Navigator::init(...) as home to FluxApp?\n";
#endif
            return false;
        }
        return true;
    }
};

inline NavigatorWidget                              *Navigator::_host   = nullptr;
inline std::vector<NavEntry>                         Navigator::_stack  = {};
inline std::unordered_map<std::string, Navigator::RouteBuilder>
                                                     Navigator::_routes = {};

// ============================================================================
// NAVIGATOR WIDGET  (unchanged from original)
// ============================================================================

class NavigatorWidget : public Widget
{
public:
    NavigatorWidget()
    {
        Navigator::_setHost(this);
        _installCurrent();
    }

    void onDetach() override
    {
        Navigator::_clearHost(this);
        Widget::onDetach();
    }

    void swapContent()
    {
        _installCurrent();
        if (auto *ui = FluxUI::getCurrentInstance())
            ui->partialRebuild(this);
        else
            markNeedsLayout();
    }

    void computeLayout(GraphicsContext &ctx,
                       const BoxConstraints &constraints,
                       FontCache &fontCache) override
    {
        if (autoWidth)  width  = constraints.maxWidth;
        if (autoHeight) height = constraints.maxHeight;

        if (!children.empty())
            children[0]->computeLayout(
                ctx, BoxConstraints::tight(width, height), fontCache);

        applyConstraints();
        needsLayout = false;
    }

    void positionChildren(int cx, int cy, int, int) override
    {
        if (!children.empty())
        {
            auto &c = children[0];
            c->x = cx;
            c->y = cy;
            c->positionChildren(
                cx + c->paddingLeft, cy + c->paddingTop,
                c->width  - c->paddingLeft - c->paddingRight,
                c->height - c->paddingTop  - c->paddingBottom);
        }
    }

    bool handleKeyDown(int keyCode) override
    {
#ifdef _WIN32
        if (keyCode == VK_ESCAPE) return Navigator::maybePop();
#else
        if (keyCode == 27)        return Navigator::maybePop();
#endif
        return !children.empty() && children[0]->handleKeyDown(keyCode);
    }

private:
    void _installCurrent()
    {
        if (!children.empty())
        {
            children[0]->parent = nullptr;
            children[0]->onDetach();
            children.clear();
        }
        auto w = Navigator::_currentWidget();
        if (w)
        {
            w->parent = this;
            children.push_back(w);
        }
    }
};

inline void Navigator::_rebuild()
{
    if (_host) _host->swapContent();
}