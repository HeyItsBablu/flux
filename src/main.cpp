#include "flux.hpp"

#include <windows.h>

class ConditionalListApp : public Component
{
private:
    State<bool> isActive;

public:
    ConditionalListApp() : isActive(false, context) {} // Use useState instead

    void updateState()
    {
        std::cout << "Button isActive" << isActive.get() << std::endl;
        isActive.set(!isActive.get());
    }

    WidgetPtr build() override
    {
        return Scaffold(
            AppBar("Conditional App"),
            Center(
                Column(

                    Container(
                        Text("Hello")
                            ->setText(isActive, "Active", "Inactive")
                            ->setTextColor(isActive, RGB(255, 255, 255), RGB(0, 0, 0))

                            )
                        ->setBackgroundColor(isActive, RGB(76, 175, 80), RGB(200, 200, 200))
                        ->setPadding(16),

                    Button("Toggle", [&]
                           { isActive.update([](bool v)
                                             { return !v; }); })

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