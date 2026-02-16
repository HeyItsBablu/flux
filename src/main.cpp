#include "flux.hpp"

#include <windows.h>

class InputTestComponent : public Component
{
private:
    State<bool> isActive;
    State<std::string> newText;
    State<std::string> passwordText;
    State<double> valueState;
    State<std::string> selectedCountry;
    State<bool> isEnabled;
    State<std::string> selectedSize;

public:
    InputTestComponent() : isActive(false, context),
                           newText("", context),
                           passwordText("Hello there", context),
                           valueState(50.0, context),
                           selectedCountry("United States", context),
                           isEnabled(false, context),
                           selectedSize("md", context) {} // Initialize with "md"

    void updateState()
    {
        std::cout << "Button isActive" << isActive.get() << std::endl;
        isActive.set(!isActive.get());
    }

    void login()
    {
        std::cout << "Check box " << isActive.get() << std::endl;
        std::cout << "Username " << newText.get() << std::endl;
        std::cout << "Password " << passwordText.get() << std::endl;
        std::cout << "Country " << selectedCountry.get() << std::endl;
        std::cout << "Size " << selectedSize.get() << std::endl;
        std::cout << "Dark Mode " << isEnabled.get() << std::endl;
    }

    WidgetPtr build() override
    {
        return Scaffold(
            AppBar("Input Test App"),
            Center(
                Column(
                    Text("Select Your Country:")
                        ->setFontSize(14)
                        ->setFontWeight(FontWeight::Bold),

                    Dropdown({"United States",
                              "Canada",
                              "United Kingdom",
                              "Germany",
                              "France",
                              "Japan",
                              "Australia",
                              "Brazil",
                              "India",
                              "China"})
                        ->setPlaceholder("Choose a country...")
                        ->setSelectedValue(selectedCountry)
                        ->setWidth(300),

                    Text(selectedCountry)
                        ->setFontSize(12)
                        ->setTextColor(RGB(100, 100, 100)),
                    SizedBox(0, 20),
                    Text("")
                        ->setText(isEnabled, "Dark Mode", "Light Mode")
                        ->setFontSize(24)
                        ->setTextColor(isEnabled, RGB(100, 100, 100), RGB(200, 100, 100)),

                    SizedBox(0, 20),

                    Text("Select Size:")
                        ->setFontSize(24)
                        ->setFontWeight(FontWeight::Bold)
                        ->setHoverTextColor(RGB(200, 100, 100)),

                    RadioGroupWithOptions({RadioOption("xs", "Extra Small"),
                                           RadioOption("sm", "Small"),
                                           RadioOption("md", "Medium"),
                                           RadioOption("lg", "Large"),
                                           RadioOption("xl", "Extra Large")})
                        ->bindValue(selectedSize)
                        ->setSpacing(8),

                    Text(selectedSize)
                        ->setFontSize(12)
                        ->setTextColor(RGB(100, 100, 100)),

                    SizedBox(0, 20),

                    Text(valueState),

                    TextInput("Enter username...")
                        ->setInputValue(newText)
                        ->setWidth(300),

                    CheckBox("Enable feature")
                        ->setInputValue(isActive),

                    Slider(0, 100, 5)
                        ->setValue(valueState)
                        ->setTrackColor(RGB(220, 220, 220))
                        ->setTrackFillColor(RGB(76, 175, 80))
                        ->setThumbColor(RGB(76, 175, 80))
                        ->setWidth(300),

                    TextInput("Enter PASSWORD...")
                        ->setInputValue(passwordText)
                        ->setWidth(300),

                    Toggle("Dark mode")
                        ->setValue(isEnabled)
                        ->setTrackOnColor(RGB(100, 150, 250)),

                    Button(Text("Click")
                               ->setText(isEnabled, "Dark Mode", "Light Mode")
                               ->setFontSize(12)
                               ->setFontWeight(FontWeight::Bold)
                               ->setTextColor(RGB(220, 220, 220)),
                           [&]
                           { login(); })

                        )
                    ->setSpacing(12)
                    ->setPadding(20)));
    }
};

WidgetPtr createApp(FluxUI *app)
{
    return FluxApp(
        "Input Test App",
        BuildComponent<InputTestComponent>(),
        AppTheme::materialGreen());
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int)
{
    AllocConsole();
    FILE *fp;
    freopen_s(&fp, "CONOUT$", "w", stdout);

    std::cout << "=== Input Test Demo ===" << std::endl;

    FluxUI app(hInstance);
    app.build([&]()
              { return createApp(&app); });
    app.createWindow("FluxUI - Input Test App", 800, 800);

    return app.run();
}