#include "flux.hpp"
#include <windows.h>
#include <iostream>



class ToggleApp : public StatefulComponent
{
private:
    State<bool> isEnabled;
    State<bool> isDarkMode;
    State<std::string> message;

public:
    ToggleApp() 
        : isEnabled(false, context),
          isDarkMode(false, context),
          message("Welcome!", context)
    {
        // React to toggle changes
        isEnabled.listen([this](bool enabled) {
            message.set(enabled ? "Feature Enabled ✓" : "Feature Disabled");
        });
        
        isDarkMode.listen([](bool dark) {
            std::cout << "Dark mode: " << (dark ? "ON" : "OFF") << std::endl;
        });
    }

    void toggleEnabled()
    {
        // Toggle boolean using update
        isEnabled.update([](bool v) { return !v; });
    }

    void toggleDarkMode()
    {
        isDarkMode.update([](bool v) { return !v; });
    }

    void enableAll()
    {
        isEnabled.set(true);
        isDarkMode.set(true);
    }

    void disableAll()
    {
        isEnabled.set(false);
        isDarkMode.set(false);
    }

    WidgetPtr build() override
    {
        auto theme = ThemeProvider::getTheme();
        
        COLORREF bgColor = isDarkMode.get() ? RGB(30, 30, 30) : RGB(250, 250, 250);
        COLORREF textColor = isDarkMode.get() ? RGB(220, 220, 220) : RGB(30, 30, 30);

        return Scaffold(
            ThemedAppBar("Toggle Demo"),
            
            Center(
                Padding(20,
                    Column(
                        Text(message)
                            ->setFontSize(24)
                            ->setFontWeight(FontWeight::Bold)
                            ->setTextColor(textColor),
                        
                        SizedBox(0, 30),
                        
                        ThemedCard(
                            Row(
                                Text("Main Feature")
                                    ->setFontSize(16),
                                SizedBox(10, 0),
                                ThemedButton(
                                    isEnabled.get() ? "ON" : "OFF",
                                    [this]() { toggleEnabled(); }
                                )
                            )
                            ->setSpacing(10)
                            ->setMainAxisAlignment(MainAxisAlignment::SpaceBetween)
                        )->setMinWidth(300),
                        
                        ThemedCard(
                            Row(
                                Text("Dark Mode")
                                    ->setFontSize(16),
                                SizedBox(10, 0),
                                ThemedButton(
                                    isDarkMode.get() ? "ON" : "OFF",
                                    [this]() { toggleDarkMode(); }
                                )
                            )
                            ->setSpacing(10)
                            ->setMainAxisAlignment(MainAxisAlignment::SpaceBetween)
                        )->setMinWidth(300),
                        
                        SizedBox(0, 30),
                        
                        Row(
                            ThemedButton("Enable All", [this]() { enableAll(); }),
                            ThemedButton("Disable All", [this]() { disableAll(); })
                        )
                        ->setSpacing(10)
                        ->setMainAxisAlignment(MainAxisAlignment::Center)
                    )
                    ->setSpacing(15)
                    ->setCrossAlignment(Alignment::Center)
                )
            )->setBackgroundColor(bgColor)
        );
    }
};

WidgetPtr toggleApp(FluxUI *app)
{
    return FluxApp(
        "Toggle Demo",
        BuildComponent<ToggleApp>(),
        AppTheme::materialBlue()
    );
}
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int)
{
    AllocConsole();
    FILE *fp;
    freopen_s(&fp, "CONOUT$", "w", stdout);
    freopen_s(&fp, "CONOUT$", "w", stderr);

    std::cout << "Starting FluxUI Dashboard..." << std::endl;
    
    FluxUI app(hInstance);
    app.build([&]() { return toggleApp(&app); });
    app.createWindow("FluxUI Demo", 900, 650);
    
    return app.run();
}