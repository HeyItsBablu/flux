// Simple example using your exact code style

#include "flux.hpp"
#include <windows.h>

class SimpleConditionalApp : public StatefulComponent
{
private:
    State<std::vector<std::string>> items;
    State<bool> showGreen; // The boolean state
    State<int> counter;

public:
    SimpleConditionalApp()
        : showGreen(true, context), counter(0, context), items({"A", "B", "C", "Hello", "Nice"}, context)
    {
    }

    WidgetPtr build() override
    {
        return Scaffold(
            ThemedAppBar("Conditional Rendering Test"),

            Center(

                Row(
                    ListView(items)
                        ->itemBuilder([](int i, const std::string &item)
                                      { return Card(Text(item)); })
                        ->separator([]()
                                    { return Divider(); })
                        ->spacing(8),
                    ListView(items)
                        ->itemBuilder([](int i, const std::string &item)
                                      { return Card(Text(item)); })
                        ->separator([]()
                                    { return Divider(); })
                        ->spacing(8))));
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