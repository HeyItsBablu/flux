#pragma once

// ============================================================================
// flux_navigator.hpp
//
//
// Setup — one change in createApp():
//   return FluxApp("Title", Navigator::init(std::make_shared<MyScreen>()), ...);
//
// Then anywhere in your app:
//   Navigator::push(std::make_shared<MyScreen>());
//   Navigator::pop();
//   Navigator::pushReplacement(std::make_shared<OtherScreen>());
//   Navigator::pushNamed("/settings");
//   Navigator::popUntil("/home");
// ============================================================================

#include "flux/flux_core.hpp"
#include "flux/flux_app.hpp"

#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class NavigatorWidget;

struct NavEntry {
    WidgetPtr   widget;
    std::string name;
};

// ============================================================================
// NAVIGATOR
// ============================================================================

class Navigator {
public:

    // ── One-time setup inside createApp() ────────────────────────────────────

    static WidgetPtr init(WidgetPtr home, const std::string& name = "");

    // ── Navigation ────────────────────────────────────────────────────────────

    static void push(WidgetPtr screen, const std::string& name = "") {
        if (!checkHost()) return;
        _stack.push_back({ std::move(screen), name });
        _rebuild();
    }

    static bool pop() {
        if (!checkHost() || _stack.size() <= 1) return false;
        _stack.pop_back();
        _rebuild();
        return true;
    }

    static bool maybePop() { return canPop() ? pop() : false; }

    static void pushReplacement(WidgetPtr screen, const std::string& name = "") {
        if (!checkHost()) return;
        if (!_stack.empty()) _stack.pop_back();
        _stack.push_back({ std::move(screen), name });
        _rebuild();
    }

    static void pushAndRemoveAll(WidgetPtr screen, const std::string& name = "") {
        if (!checkHost()) return;
        _stack.clear();
        _stack.push_back({ std::move(screen), name });
        _rebuild();
    }

    static void popUntil(const std::string& name) {
        if (!checkHost()) return;
        while (_stack.size() > 1 && _stack.back().name != name)
            _stack.pop_back();
        _rebuild();
    }

    // ── Named routes ─────────────────────────────────────────────────────────

    using RouteBuilder = std::function<WidgetPtr()>;

    static void registerRoute(const std::string& name, RouteBuilder builder) {
        _routes[name] = std::move(builder);
    }

    static bool pushNamed(const std::string& name) {
        auto it = _routes.find(name);
        if (it == _routes.end()) {
#ifdef FLUX_DEBUG
            std::cerr << "[Navigator] pushNamed: unknown route \"" << name << "\"\n";
#endif
            return false;
        }
        push(it->second(), name);
        return true;
    }

    static bool pushReplacementNamed(const std::string& name) {
        auto it = _routes.find(name);
        if (it == _routes.end()) {
#ifdef FLUX_DEBUG
            std::cerr << "[Navigator] pushReplacementNamed: unknown route \"" << name << "\"\n";
#endif
            return false;
        }
        pushReplacement(it->second(), name);
        return true;
    }

    // ── Queries ───────────────────────────────────────────────────────────────

    static bool        canPop()      { return _stack.size() > 1; }
    static int         stackDepth()  { return (int)_stack.size(); }
    static std::string currentName() { return _stack.empty() ? "" : _stack.back().name; }

    // ── Internal ─────────────────────────────────────────────────────────────

    static void      _setHost(NavigatorWidget* w)   { _host = w; }
    static void      _clearHost(NavigatorWidget* w) { if (_host == w) _host = nullptr; }
    static WidgetPtr _currentWidget()               { return _stack.empty() ? nullptr : _stack.back().widget; }
    static void      _rebuild();

private:
    static NavigatorWidget* _host;
    static std::vector<NavEntry> _stack;
    static std::unordered_map<std::string, RouteBuilder> _routes;

    static bool checkHost() {
        if (!_host) {
#ifdef FLUX_DEBUG
            std::cerr << "[Navigator] No active NavigatorWidget. "
                         "Did you pass Navigator::init(...) as home to FluxApp?\n";
#endif
            return false;
        }
        return true;
    }
};

inline NavigatorWidget* Navigator::_host = nullptr;
inline std::vector<NavEntry> Navigator::_stack = {};
inline std::unordered_map<std::string, Navigator::RouteBuilder> Navigator::_routes = {};

// ============================================================================
// NAVIGATOR WIDGET
// ============================================================================

class NavigatorWidget : public Widget {
public:
    NavigatorWidget() {
        Navigator::_setHost(this);

        _installCurrent();
    }

    void onDetach() override {
        Navigator::_clearHost(this);
        Widget::onDetach();
    }

    // swapContent is called by Navigator after every push/pop.
    void swapContent() {
        _installCurrent();
        if (auto* ui = FluxUI::getCurrentInstance())
            ui->partialRebuild(this);
        else
            markNeedsLayout();
    }

    void computeLayout(GraphicsContext& ctx,
                       const BoxConstraints& constraints,
                       FontCache& fontCache) override {
        if (autoWidth)  width  = constraints.maxWidth;
        if (autoHeight) height = constraints.maxHeight;

        if (!children.empty())
            children[0]->computeLayout(
                ctx, BoxConstraints::tight(width, height), fontCache);

        applyConstraints();
        needsLayout = false;
    }

    void positionChildren(int cx, int cy, int, int) override {
        if (!children.empty()) {
            auto& c = children[0];
            c->x = cx;
            c->y = cy;
            c->positionChildren(
                cx + c->paddingLeft, cy + c->paddingTop,
                c->width  - c->paddingLeft - c->paddingRight,
                c->height - c->paddingTop  - c->paddingBottom);
        }
    }

    bool handleKeyDown(int keyCode) override {
#ifdef _WIN32
        if (keyCode == VK_ESCAPE) return Navigator::maybePop();
#else
        if (keyCode == 27)        return Navigator::maybePop();
#endif
        return !children.empty() && children[0]->handleKeyDown(keyCode);
    }

private:
    void _installCurrent() {
        if (!children.empty()) {
            children[0]->parent = nullptr;
            children[0]->onDetach();
            children.clear();
        }
        auto w = Navigator::_currentWidget();
        if (w) {
            w->parent = this;
            children.push_back(w);
        }
    }
};

inline void Navigator::_rebuild() {
    if (_host) _host->swapContent();
}

inline WidgetPtr Navigator::init(WidgetPtr home, const std::string& name) {
    _stack.clear();
    if (home) _stack.push_back({ std::move(home), name });
    return std::make_shared<NavigatorWidget>();
}