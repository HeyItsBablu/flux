// flux_navigator.hpp
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
//
// PASSING ARGUMENTS:
//   Navigator::navigate("/details", productId); // any type, via std::any
//
//   class DetailsPage : public Widget {
//   public:
//       DetailsPage() { id = Navigator::arguments<int>(); } // ctor only!
//       int id = 0;
//       WidgetPtr build() override { ... }
//   };
//
//   Navigator::arguments<T>()        — read once, in the destination
//                                       widget's CONSTRUCTOR only.
//   Navigator::currentArguments<T>() — safe to read any time the route is
//                                       current (build(), callbacks, etc).
//
//   Arguments are in-memory only — they don't survive a web page refresh
//   or get encoded into the URL hash.
//
// WEB (Emscripten):
//   On web builds, every stack mutation also syncs the browser URL hash
//   (e.g. "#/settings") via history.pushState/replaceState, and the
//   browser's Back/Forward buttons drive the stack back via _onHashChange,
//   which is wired up from main_web.cpp. Deep links (loading the page with
//   a hash already set) are honored in init(). None of this affects native
//   builds — it's compiled out entirely without __EMSCRIPTEN__.
// ============================================================================

#include "flux/flux_core.hpp"
#include "flux/flux_app.hpp"

#include <any>
#include <functional>
#include <initializer_list>
#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

class NavigatorWidget;

struct NavEntry
{
    WidgetPtr widget;
    std::string name;
    std::any arguments;
};

// ============================================================================
// ROUTE TABLE ENTRY
// Passed as an initializer list to Navigator::init().
// ============================================================================

struct RouteDefinition
{
    using Builder = std::function<WidgetPtr()>;

    std::string name;
    Builder builder;

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
    //
    // On web, if the page was loaded with a hash matching a registered route
    // (e.g. navigation.html#/settings), that route is used instead of
    // initialRoute — this is what makes deep links / bookmarks / refresh work.

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

        std::string startRoute = initialRoute;

#ifdef __EMSCRIPTEN__
        std::string fromHash = _getInitialHash();
        if (!fromHash.empty())
        {
            std::string candidate = fromHash[0] == '/' ? fromHash : ("/" + fromHash);
            if (_routes.count(candidate))
                startRoute = candidate;
        }
#endif

        // Push the initial route onto the stack.
        auto it = _routes.find(startRoute);
        if (it == _routes.end())
        {
            std::cerr << "[Navigator] init: initialRoute \""
                      << startRoute << "\" not found in route table.\n";
        }
        else
        {
            _stack.push_back({it->second(), startRoute});
#ifdef __EMSCRIPTEN__
            _setHash(startRoute, /*replace=*/true);
#endif
        }

        return std::static_pointer_cast<Widget>(std::make_shared<NavigatorWidget>());
    }

    // ── Named navigation ─────────────────────────────────────────────────────

    // Push a new instance of the named route onto the stack.
    // `arguments` is readable in the destination widget's constructor via
    // Navigator::arguments<T>(), and any time thereafter via
    // Navigator::currentArguments<T>().
    static bool navigate(const std::string &name, std::any arguments = {})
    {
        auto *builder = findRoute(name, "navigate");
        if (!builder)
            return false;
        if (!checkHost())
            return false;

        _pendingArguments = std::move(arguments);
        WidgetPtr widget = (*builder)();
        _stack.push_back({widget, name, _pendingArguments});
        _pendingArguments.reset();

        _rebuild();
#ifdef __EMSCRIPTEN__
        _setHash(name, /*replace=*/false);
#endif
        return true;
    }

    // Replace the top of the stack with a new instance of the named route.
    static bool pushReplacementNamed(const std::string &name, std::any arguments = {})
    {
        auto *builder = findRoute(name, "pushReplacementNamed");
        if (!builder)
            return false;
        if (!checkHost())
            return false;

        if (!_stack.empty())
            _stack.pop_back();

        _pendingArguments = std::move(arguments);
        WidgetPtr widget = (*builder)();
        _stack.push_back({widget, name, _pendingArguments});
        _pendingArguments.reset();

        _rebuild();
#ifdef __EMSCRIPTEN__
        _setHash(name, /*replace=*/true);
#endif
        return true;
    }

    // Clear the entire stack and push the named route as the new root.
    static bool pushAndRemoveAllNamed(const std::string &name, std::any arguments = {})
    {
        auto *builder = findRoute(name, "pushAndRemoveAllNamed");
        if (!builder)
            return false;
        if (!checkHost())
            return false;

        _stack.clear();

        _pendingArguments = std::move(arguments);
        WidgetPtr widget = (*builder)();
        _stack.push_back({widget, name, _pendingArguments});
        _pendingArguments.reset();

        _rebuild();
#ifdef __EMSCRIPTEN__
        _setHash(name, /*replace=*/true);
#endif
        return true;
    }

    // ── Pop ───────────────────────────────────────────────────────────────────

    static bool pop()
    {
        if (!checkHost() || _stack.size() <= 1)
            return false;
        _stack.pop_back();
        _rebuild();
#ifdef __EMSCRIPTEN__
        // Tell the browser to step back too, so its session history stays in
        // sync. This fires a hashchange asynchronously, which _onHashChange
        // will see as already matching the (already-updated) stack and
        // treat as a no-op.
        EM_ASM({ history.back(); });
#endif
        return true;
    }

    static bool maybePop() { return canPop() ? pop() : false; }

    // Pop until the stack top matches the given route name.
    // If the name is not in the stack, does nothing.
    static void popUntil(const std::string &name)
    {
        if (!checkHost())
            return;
        while (_stack.size() > 1 && _stack.back().name != name)
            _stack.pop_back();
        _rebuild();
#ifdef __EMSCRIPTEN__
        if (!_stack.empty())
            _setHash(_stack.back().name, /*replace=*/true);
#endif
    }

    // ── Queries ───────────────────────────────────────────────────────────────

    static bool canPop() { return _stack.size() > 1; }
    static int stackDepth() { return static_cast<int>(_stack.size()); }
    static std::string currentName() { return _stack.empty() ? "" : _stack.back().name; }

    // Returns true if the given route name exists in the route table.
    static bool hasRoute(const std::string &name)
    {
        return _routes.count(name) > 0;
    }

    // ── Arguments ────────────────────────────────────────────────────────────

    // Call from your destination widget's CONSTRUCTOR (not build()) to read
    // the value passed to navigate()/pushReplacementNamed()/pushAndRemoveAllNamed().
    // Returns defaultValue if no arguments were passed or the type doesn't match.
    template <typename T>
    static T arguments(T defaultValue = T())
    {
        if (auto *p = std::any_cast<T>(&_pendingArguments))
            return *p;
        return defaultValue;
    }

    // Safe to call any time the route is current — from build(), event
    // handlers, nested children, etc. Reads from the stack entry rather
    // than the transient value used only during construction.
    template <typename T>
    static T currentArguments(T defaultValue = T())
    {
        if (_stack.empty())
            return defaultValue;
        if (auto *p = std::any_cast<T>(&_stack.back().arguments))
            return *p;
        return defaultValue;
    }

    // ── Internal ─────────────────────────────────────────────────────────────

    static void _setHost(NavigatorWidget *w) { _host = w; }
    static void _clearHost(NavigatorWidget *w)
    {
        if (_host == w)
            _host = nullptr;
    }
    static WidgetPtr _currentWidget() { return _stack.empty() ? nullptr : _stack.back().widget; }
    static void _rebuild();

#ifdef __EMSCRIPTEN__
    // Called from JS (via main_web.cpp's exported wrapper) whenever the
    // browser hash changes — covers Back/Forward buttons, manual URL edits,
    // and deep links. Idempotent: if the stack already matches, no-op.
    static void _onHashChange(const char *hash)
    {
        std::string name = hash ? hash : "";
        if (name.empty())
            return;
        if (name[0] != '/')
            name = "/" + name;

        if (!_stack.empty() && _stack.back().name == name)
            return;

        // Already somewhere in the stack? (Back button case.) Trim to it.
        for (int i = (int)_stack.size() - 1; i >= 0; --i)
        {
            if (_stack[i].name == name)
            {
                _stack.resize(i + 1);
                _rebuild();
                return;
            }
        }

        // Not in the stack — forward navigation or a deep link to a known route.
        // Note: no arguments are available on this path (see header comment).
        auto it = _routes.find(name);
        if (it != _routes.end())
        {
            _stack.push_back({it->second(), name});
            _rebuild();
        }
#ifdef FLUX_DEBUG
        else
        {
            std::cerr << "[Navigator] _onHashChange: unknown route \""
                      << name << "\" ignored.\n";
        }
#endif
    }
#endif

private:
    static NavigatorWidget *_host;
    static std::vector<NavEntry> _stack;
    static std::unordered_map<std::string, RouteBuilder> _routes;
    static std::any _pendingArguments;

    // Returns a pointer to the builder for the given route, or nullptr.
    static const RouteBuilder *findRoute(const std::string &name,
                                         const char *callerName)
    {
        auto it = _routes.find(name);
        if (it != _routes.end())
            return &it->second;

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

#ifdef __EMSCRIPTEN__
    // Push (replace=false) or replace (replace=true) the browser's URL hash.
    static void _setHash(const std::string &name, bool replace)
    {
        EM_ASM({
            var name = UTF8ToString($0);
            if ($1) {
                history.replaceState(null, '', '#' + name);
            } else {
                location.hash = name;
            }
        }, name.c_str(), replace ? 1 : 0);
    }

    // Reads location.hash at startup, for deep-linking
    // (e.g. navigation.html#/settings).
    static std::string _getInitialHash()
    {
        char buf[256] = {0};
        EM_ASM({
            var h = location.hash.length > 1 ? location.hash.slice(1) : '';
            stringToUTF8(h, $0, $1);
        }, buf, (int)sizeof(buf));
        return std::string(buf);
    }
#endif
};

inline NavigatorWidget *Navigator::_host = nullptr;
inline std::vector<NavEntry> Navigator::_stack = {};
inline std::unordered_map<std::string, Navigator::RouteBuilder>
    Navigator::_routes = {};
inline std::any Navigator::_pendingArguments = {};

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
        if (autoWidth)
            width = constraints.maxWidth;
        if (autoHeight)
            height = constraints.maxHeight;

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
                c->width - c->paddingLeft - c->paddingRight,
                c->height - c->paddingTop - c->paddingBottom);
        }
    }

    bool handleKeyDown(int keyCode) override
    {
#ifdef _WIN32
        if (keyCode == VK_ESCAPE)
            return Navigator::maybePop();
#else
        if (keyCode == 27)
            return Navigator::maybePop();
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
    if (_host)
        _host->swapContent();
}