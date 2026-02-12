#include "flux.hpp"
#include "flux_app.hpp"
#include <windows.h>

// ============================================================================
// EXAMPLE 1: Basic FluxApp with Light Theme
// ============================================================================

class SimpleCounter : public StatefulComponent {
    State<int> count;
public:
    SimpleCounter() : count(0, context) {}
    
    WidgetPtr build() override {
        return Scaffold(
            ThemedAppBar("Counter App"),  // Uses theme colors!
            
            Center(
                Column(
                    Text("Count: " + count.toString())
                        ->setFontSize(32)
                        ->setTextColor(ThemeProvider::getTheme().primaryColor),
                    
                    SizedBox(0, 20),
                    
                    ThemedButton("Increment", [this]() { count++; })
                )
                ->setSpacing(10)
            )
        );
    }
};

WidgetPtr lightThemeApp(FluxUI* app) {
    return FluxApp(
        "Light Theme App",                // Title
        BuildComponent<SimpleCounter>(),  // Home widget
        AppTheme::light()                 // Theme
    );
}

// ============================================================================
// EXAMPLE 2: Dark Theme App
// ============================================================================

class DarkThemeCounter : public StatefulComponent {
    State<int> count;
public:
    DarkThemeCounter() : count(0, context) {}
    
    WidgetPtr build() override {
        auto theme = ThemeProvider::getTheme();
        
        return Scaffold(
            ThemedAppBar("Dark Theme"),
            
            Center(
                ThemedCard(
                    Column(
                        Text("Dark Mode Counter")
                            ->setFontSize(18)
                            ->setFontWeight(FontWeight::Bold)
                            ->setTextColor(theme.textColor),
                        
                        SizedBox(0, 10),
                        
                        Text(count)
                            ->setFontSize(48)
                            ->setTextColor(theme.primaryColor),
                        
                        SizedBox(0, 10),
                        
                        Row(
                            ThemedButton("+", [this]() { count++; }),
                            ThemedButton("-", [this]() { if (count.get() > 0) count--; })
                        )
                        ->setSpacing(10)
                        ->setMainAxisAlignment(MainAxisAlignment::Center)
                    )
                    ->setSpacing(0)
                )
            )
        );
    }
};

WidgetPtr darkThemeApp(FluxUI* app) {
    return FluxApp(
        "Dark Theme App",
        BuildComponent<DarkThemeCounter>(),
        AppTheme::dark()  // Dark theme!
    );
}

// ============================================================================
// EXAMPLE 3: Custom Theme with Material Colors
// ============================================================================

WidgetPtr customThemeApp(FluxUI* app) {
    // Create custom theme
    AppTheme customTheme;
    customTheme.primaryColor = RGB(156, 39, 176);    // Purple
    customTheme.accentColor = RGB(255, 235, 59);     // Yellow
    customTheme.backgroundColor = RGB(245, 245, 245);
    customTheme.appBarColor = RGB(156, 39, 176);
    customTheme.buttonColor = RGB(156, 39, 176);
    
    return FluxApp(
        "Custom Theme",
        BuildComponent<SimpleCounter>(),
        customTheme
    );
}

// ============================================================================
// EXAMPLE 4: Material Color Themes
// ============================================================================

WidgetPtr materialRedApp(FluxUI* app) {
    return FluxApp(
        "Material Red",
        BuildComponent<SimpleCounter>(),
        AppTheme::materialRed()
    );
}

WidgetPtr materialGreenApp(FluxUI* app) {
    return FluxApp(
        "Material Green",
        BuildComponent<SimpleCounter>(),
        AppTheme::materialGreen()
    );
}

// ============================================================================
// EXAMPLE 5: Debug Mode (Show Widget Bounds)
// ============================================================================

WidgetPtr debugApp(FluxUI* app) {
    return FluxApp(
        "Debug Mode",
        BuildComponent<SimpleCounter>(),
        AppTheme::light(),
        true  // debugShowWidgetBounds = true (shows red outlines)
    );
}

// ============================================================================
// EXAMPLE 6: Complex App with Multiple Themed Widgets
// ============================================================================

class ThemedDashboard : public StatefulComponent {
    State<int> total;
    State<int> active;
    State<int> completed;
    
public:
    ThemedDashboard() 
        : total(0, context),
          active(0, context),
          completed(0, context) {}
    
    WidgetPtr build() override {
        auto theme = ThemeProvider::getTheme();
        
        auto createStatCard = [&](const std::string& title, State<int>& value, COLORREF color) {
            return ThemedCard(
                Column(
                    Text(title)
                        ->setFontSize(14)
                        ->setTextColor(theme.secondaryTextColor),
                    
                    SizedBox(0, 10),
                    
                    Text(value)
                        ->setFontSize(32)
                        ->setFontWeight(FontWeight::Bold)
                        ->setTextColor(color),
                    
                    SizedBox(0, 10),
                    
                    ThemedButton("Add", [&value]() { value++; })
                )
                ->setSpacing(0)
                ->setCrossAlignment(Alignment::Center)
            )
            ->setMinWidth(150);
        };
        
        return Scaffold(
            ThemedAppBar("Dashboard"),
            
            Padding(20,
                Column(
                    Text("Task Statistics")
                        ->setFontSize(theme.titleFontSize)
                        ->setFontWeight(theme.titleFontWeight)
                        ->setTextColor(theme.textColor),
                    
                    SizedBox(0, 20),
                    
                    Row(
                        createStatCard("Total", total, theme.primaryColor),
                        createStatCard("Active", active, RGB(255, 152, 0)),
                        createStatCard("Done", completed, RGB(76, 175, 80))
                    )
                    ->setSpacing(15)
                    ->setMainAxisAlignment(MainAxisAlignment::Center)
                )
                ->setSpacing(0)
            )
        );
    }
};

WidgetPtr dashboardApp(FluxUI* app) {
    return FluxApp(
        "Themed Dashboard",
        BuildComponent<ThemedDashboard>(),
        AppTheme::materialBlue()
    );
}

// ============================================================================
// EXAMPLE 7: Theme Switcher
// ============================================================================

class ThemeSwitcherApp : public StatefulComponent {
    State<int> themeIndex;
    
public:
    ThemeSwitcherApp() : themeIndex(0, context) {}
    
    void switchTheme() {
        themeIndex.set((themeIndex.get() + 1) % 3);
        
        // Update global theme
        switch (themeIndex.get()) {
            case 0: ThemeProvider::setTheme(AppTheme::light()); break;
            case 1: ThemeProvider::setTheme(AppTheme::dark()); break;
            case 2: ThemeProvider::setTheme(AppTheme::materialGreen()); break;
        }
        
        // Rebuild to apply new theme
        rebuild();
    }
    
    WidgetPtr build() override {
        auto theme = ThemeProvider::getTheme();
        
        std::string themeName;
        switch (themeIndex.get()) {
            case 0: themeName = "Light Theme"; break;
            case 1: themeName = "Dark Theme"; break;
            case 2: themeName = "Green Theme"; break;
        }
        
        return Scaffold(
            ThemedAppBar("Theme Switcher"),
            
            Center(
                ThemedCard(
                    Column(
                        Text("Current Theme")
                            ->setFontSize(14)
                            ->setTextColor(theme.secondaryTextColor),
                        
                        SizedBox(0, 10),
                        
                        Text(themeName)
                            ->setFontSize(24)
                            ->setFontWeight(FontWeight::Bold)
                            ->setTextColor(theme.primaryColor),
                        
                        SizedBox(0, 20),
                        
                        ThemedButton("Switch Theme", [this]() { switchTheme(); })
                    )
                    ->setSpacing(0)
                    ->setCrossAlignment(Alignment::Center)
                )
            )
        );
    }
};

WidgetPtr themeSwitcherApp(FluxUI* app) {
    return FluxApp(
        "Theme Switcher",
        BuildComponent<ThemeSwitcherApp>(),
        AppTheme::light()
    );
}

// ============================================================================
// ENTRY POINT - Choose which demo to run
// ============================================================================

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int)
{
    FluxUI app(hInstance);
    
    // Choose which example to run:
    
    // Basic examples:
    // app.build([&]() { return lightThemeApp(&app); });
    // app.build([&]() { return darkThemeApp(&app); });
    // app.build([&]() { return customThemeApp(&app); });
    
    // Material color themes:
    // app.build([&]() { return materialRedApp(&app); });
    // app.build([&]() { return materialGreenApp(&app); });
    
    // Debug mode:
    // app.build([&]() { return debugApp(&app); });
    
    // Complex examples:
    app.build([&]() { return dashboardApp(&app); });
    
    // Theme switcher:
    // app.build([&]() { return themeSwitcherApp(&app); });
    
    app.createWindow("FluxApp Demo", 800, 600);
    return app.run();
}