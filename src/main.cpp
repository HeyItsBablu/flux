// Your usage code - OPTIMIZED VERSION:

#include "flux_ui.hpp"

class ReactiveCounter
{
private:
    FluxUI *ui;
    State<int> counter;
    State<std::string> username;

public:
    ReactiveCounter(FluxUI *app) : ui(app), username("Guest", app), counter(0, app) {}

    void updateUsername(const std::string &name)
    { 
        username.set(name);
    }

    void increment()
    {
        counter.set(counter.get() + 1);
    }

    void decrement()
    {
        counter.set(counter.get() - 1);
    }

    WidgetPtr build()
    {
        return Scaffold(
            AppBar("My App"),
            Center(
                Container(
                    Text("Hello, World!")
                        ->setFontSize(24)
                        ->setFontWeight(FontWeight::Bold)
                        ->setTextColor(RGB(33, 150, 243)))
                    ->setBackgroundColor(RGB(255, 255, 255))
                    ->setPadding(30)
                    ->setBorderRadius(12)
                    ->setBorderColor(RGB(200, 200, 200))
                    ->setBorderWidth(2)
                    ->setWidth(200)
                    ->setHeight(20)));
    }
};

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int)
{
    FluxUI app(hInstance);
    ReactiveCounter counterApp(&app);

    // ✅ Build is called ONCE during initialization
    app.build([&counterApp]()
              { return counterApp.build(); });

    app.createWindow("Reactive Counter - Optimized!", 400, 300);

    return app.run();
}
