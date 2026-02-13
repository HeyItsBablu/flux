// Simple example using your exact code style

#include "flux.hpp"
#include <windows.h>

class SimpleConditionalApp : public StatefulComponent
{
private:
    State<bool> showGreen; // The boolean state
    State<int> counter;

public:
    SimpleConditionalApp()
        : showGreen(true, context), counter(0, context)
    {
    }

    WidgetPtr build() override
    {
        return Scaffold(
            ThemedAppBar("Conditional Rendering Test"),

            Center(
                Column(
                    // Counter display
                    Text(counter)
                        ->setFontSize(24)
                        ->setTextColor(RGB(30, 30, 30)),

                    // Toggle button
                    ThemedButton("Toggle Container", [this]()
                                 { showGreen.update([](bool v)
                                                    { return !v; }); }),

                    // Increment button
                    ThemedButton("+ Counter", [this]()
                                 { counter.update([](int v)
                                                  { return v + 1; }); }),

                          Switch(counter)
                        ->Case(0, []()
                               { return Text("Home"); })
                        ->Case(1, []()
                               { return Text("Profile"); })
                        ->Case(2, []()
                               { return Text("Settings"); })
                        ->Default([]()
                                  { return Text("Unknown"); }))
                    ->setSpacing(20)));
    }
};

WidgetPtr createApp(FluxUI *app)
{
    return FluxApp(
        "Conditional Test",
        BuildComponent<SimpleConditionalApp>(),
        AppTheme::materialBlue());
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int)
{
    AllocConsole();
    FILE *fp;
    freopen_s(&fp, "CONOUT$", "w", stdout);

    std::cout << "=== Conditional Rendering Test ===" << std::endl;
    std::cout << "Click 'Toggle Container' to switch between layouts!" << std::endl;

    FluxUI app(hInstance);
    app.build([&]()
              { return createApp(&app); });
    app.createWindow("FluxUI Conditional Test", 1000, 700);

    return app.run();
}