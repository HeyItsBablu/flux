#include "flux.hpp"

#include <windows.h>

class InputTestComponent : public Component
{
private:
    State<bool> isActive;
    State<std::string> newText;
    State<std::string> passwordText;

public:
    InputTestComponent() : isActive(false, context), newText("", context), passwordText("Hello there", context) {} // Use useState instead

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
    }

    WidgetPtr build() override
    {
        return Scaffold(
            AppBar("Conditional App"),
            Center(
                Column(

                    // TextInput("Enter username...")
                    //     ->setInputValue(newText)
                    //     ->setWidth(300),

                    // CheckBox("Enable feature")
                    //     ->setInputValue(isActive),

                    TextInput("Enter PASSWORD...")
                        ->setInputValue(passwordText)
                        ->setWidth(300),

                    Button("Login", [&]
                           { login(); })

                        )));
    }
};

WidgetPtr createApp(FluxUI *app)
{
    return FluxApp(
        "Conditional App",
        BuildComponent<InputTestComponent>(),
        AppTheme::materialGreen());
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int)
{
    AllocConsole();
    FILE *fp;
    freopen_s(&fp, "CONOUT$", "w", stdout);

    std::cout << "=== Conditional List Demo ===" << std::endl;

    FluxUI app(hInstance);
    app.build([&]()
              { return createApp(&app); });
    app.createWindow("FluxUI - Conditional App", 800, 700);

    return app.run();
}