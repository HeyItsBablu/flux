// app.cpp — full navigation example
// Demonstrates: pushNamed, push, pop, pushReplacement,
//               pushAndRemoveUntil, arguments passing
#pragma once
#include "flux/flux.hpp"
#include "flux/flux_navigator.hpp"

// ── Forward-declare the navigator state so screens can share it ──────────────
static NavigatorStatePtr nav;

// ============================================================================
// SCREENS
// ============================================================================

// ── Home Screen ──────────────────────────────────────────────────────────────
WidgetPtr HomeScreen(FluxUI* app) {
    auto col = Column({
        Text("Home Screen")
            ->setFontSize(24)
            ->setFontWeight(FontWeight::Bold)
            ->setPadding(16),

        Text("Navigate to any screen below")
            ->setFontSize(14)
            ->setTextColor(Color::fromRGB(120, 120, 120))
            ->setPadding(16),

        // ── push by lambda ───────────────────────────────────────────────────
        Button("Go to Settings", [app]() {
            nav->pushNamed("/settings");
        })->setPadding(16),

        // ── push with arguments ──────────────────────────────────────────────
        Button("Open Profile (pass username)", [app]() {
            nav->pushNamed("/profile",
                std::string("Alice"));          // argument: std::string
        })->setPadding(16),

        // ── push an inline widget (no route table needed) ────────────────────
        Button("Quick Detail Page", [app]() {
            nav->push([](FluxUI*) {
                return Column({
                    Text("Detail Page")->setFontSize(22)->setPadding(16),
                    Button("Back", []() { nav->pop(); })->setPadding(16),
                });
            }, RouteSettings("/detail"));
        })->setPadding(16),
    });
    col->setSpacing(4);
    return col;
}

// ── Settings Screen ───────────────────────────────────────────────────────────
WidgetPtr SettingsScreen(FluxUI* app) {
    return Column({
        Text("Settings")->setFontSize(24)->setFontWeight(FontWeight::Bold)->setPadding(16),

        // pop() goes back
        Button("< Back", []() { nav->pop(); })->setPadding(16),

        Text("Theme")->setFontSize(16)->setPadding(16),

        Button("Switch to Dark Mode", [app]() {
            FluxAppWidget::getInstance()->setTheme(AppTheme::dark());
            // push a confirmation screen, replacing settings
            nav->pushReplacement([](FluxUI*) {
                return Column({
                    Text("Dark mode applied!")
                        ->setFontSize(20)->setPadding(20),
                    Button("Go Home", []() {
                        // Pop entire stack back to home
                        nav->popUntilNamed("/");
                    })->setPadding(20),
                });
            }, RouteSettings("/settings/confirm"));
        })->setPadding(16),

        Button("Clear stack & Go Home", []() {
            // pushAndRemoveUntil with removeAll() resets stack to one entry
            nav->pushAndRemoveUntil(
                makeRoute([](FluxUI*) { return HomeScreen(nullptr); },
                          RouteSettings("/")),
                NavigatorState::removeAll()
            );
        })->setPadding(16),
    });
}

// ── Profile Screen (reads argument passed via pushNamed) ─────────────────────
WidgetPtr ProfileScreen(FluxUI* app, const RouteSettings& settings) {
    // Safely retrieve the passed std::string argument
    std::string username = "Unknown";
    if (settings.arguments.has_value()) {
        try { username = std::any_cast<std::string>(settings.arguments); }
        catch (...) {}
    }

    return Column({
        Text("Profile: " + username)
            ->setFontSize(24)->setFontWeight(FontWeight::Bold)->setPadding(16),

        Text("Stack depth: " + std::to_string(nav->stackDepth()))
            ->setFontSize(13)
            ->setTextColor(Color::fromRGB(120, 120, 120))
            ->setPadding(16),

        Button("< Back", []() { nav->pop(); })->setPadding(16),

        Button("Push another Profile (Bob)", []() {
            nav->pushNamed("/profile", std::string("Bob"));
        })->setPadding(16),

        Button("Go Home (pop all)", []() {
            nav->popUntilNamed("/");
        })->setPadding(16),
    });
}

// ============================================================================
// APP ENTRY POINT
// ============================================================================

WidgetPtr createApp(FluxUI* app) {

    // ── Route table ──────────────────────────────────────────────────────────
    RouteTable routes = {
        { "/", [](const RouteSettings& s) {
            return makeRoute([](FluxUI* a) { return HomeScreen(a); }, s);
        }},
        { "/settings", [](const RouteSettings& s) {
            return makeRoute([](FluxUI* a) { return SettingsScreen(a); }, s);
        }},
        { "/profile", [](const RouteSettings& s) {
            // Capture settings so ProfileScreen can read arguments
            return makeRoute([s](FluxUI* a) { return ProfileScreen(a, s); }, s);
        }},
    };

    // ── Create navigator ─────────────────────────────────────────────────────
    auto [navState, navWidget] = Navigator::create_with_widget(app, routes, "/");
    nav = navState; // store globally so screens can access it

    // ── Wrap in Scaffold ─────────────────────────────────────────────────────
    return FluxApp("Navigator Demo",
        Scaffold(
            AppBar("FluxUI Navigator"),
            navWidget           // NavigatorWidget fills the body
        )
    );
}