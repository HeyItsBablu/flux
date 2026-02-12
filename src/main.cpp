#include "flux.hpp"
#include <windows.h>

// Simple test to verify no crashes on button click

class SimpleCounter : public StatefulComponent
{
    State<int> count;

public:
    SimpleCounter() : count(0, context)
    {
        std::cout << "SimpleCounter created" << std::endl;
    }

    ~SimpleCounter()
    {
        std::cout << "SimpleCounter destroyed" << std::endl;
    }

    void initState() override
    {
        std::cout << "SimpleCounter initState()" << std::endl;
    }

    WidgetPtr build() override
    {
        std::cout << "SimpleCounter build() - count = " << count.get() << std::endl;

        return Card(
                   Column(
                       Text("Counter Test")
                           ->setFontSize(18)
                           ->setFontWeight(FontWeight::Bold),

                       SizedBox(0, 10),

                       Text(count)
                           ->setFontSize(48)
                           ->setTextColor(RGB(33, 150, 243)),

                       SizedBox(0, 10),

                       Row(
                           Button("+", [this]()
                                  { 
                        std::cout << "Button + clicked, count was: " << count.get() << std::endl;
                        count++; 
                        std::cout << "Button + clicked, count now: " << count.get() << std::endl; })
                               ->setBackgroundColor(RGB(76, 175, 80)),

                           Button("-", [this]()
                                  { 
                        std::cout << "Button - clicked, count was: " << count.get() << std::endl;
                        if (count.get() > 0) count--; 
                        std::cout << "Button - clicked, count now: " << count.get() << std::endl; })
                               ->setBackgroundColor(RGB(244, 67, 54)),

                           Button("Reset", [this]()
                                  { 
                        std::cout << "Button Reset clicked" << std::endl;
                        count.set(0); })
                               ->setBackgroundColor(RGB(128, 128, 128)))
                           ->setSpacing(10)
                           ->setMainAxisAlignment(MainAxisAlignment::Center))
                       ->setSpacing(0))
            ->setPadding(20);
    }
};

WidgetPtr app(FluxUI *fluxApp)
{
    std::cout << "Building app..." << std::endl;

    return Scaffold(

        Center(
            Column(
                BuildComponent<SimpleCounter>(),
                    BuildComponent<SimpleCounter>())));
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int)
{
    // Enable console for debugging
    AllocConsole();
    FILE *fp;
    freopen_s(&fp, "CONOUT$", "w", stdout);
    freopen_s(&fp, "CONOUT$", "w", stderr);

    std::cout << "Starting FluxUI..." << std::endl;

    FluxUI fluxApp(hInstance);
    fluxApp.build([&]()
                  { return app(&fluxApp); });
    fluxApp.createWindow("Crash Test", 400, 400);

    std::cout << "Window created, entering message loop..." << std::endl;

    int result = fluxApp.run();

    std::cout << "Exiting..." << std::endl;
    return result;
}