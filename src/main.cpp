#include "flux.hpp"

#include <windows.h>

class ConditionalListApp : public Component
{
private:
    State<bool> isActive;
    State<std::string> newText;

public:
    ConditionalListApp() : isActive(false, context), newText("Hello", context) {} // Use useState instead

    void updateState()
    {
        std::cout << "Button isActive" << isActive.get() << std::endl;
        isActive.set(!isActive.get());
    }

    void updateTextState()
    {
        std::cout << "Button isActive" << isActive.get() << std::endl;

        newText.set(newText.get() + "World");
    }

    WidgetPtr build() override
    {
        return Scaffold(
            AppBar("Conditional App"),
            Center(
                Column(

                    Container(
                        Text("Hello")
                            ->setText(newText)
                            ->setTextColor(isActive, RGB(255, 255, 255), RGB(0, 0, 0))

                            )
                        ->setBackgroundColor(isActive, RGB(76, 175, 80), RGB(200, 200, 200))
                        ->setPadding(16),

                    CheckBox("Enable feature")
                        ->setInputValue(isActive),

                            Button("Toggle", [&]
                                   { updateTextState(); })

                        )));
    }
};

WidgetPtr createApp(FluxUI *app)
{
    return FluxApp(
        "Conditional App",
        BuildComponent<ConditionalListApp>(),
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