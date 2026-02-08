// Your usage code - OPTIMIZED VERSION:

#include "flutter_ui.hpp"

class ReactiveCounter
{
private:
    FlutterUI *ui;
    State<int> counter;

public:
    ReactiveCounter(FlutterUI *app) : ui(app), counter(0, app) {}

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
                Text("Hello World")));
    }
};

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int)
{
    FlutterUI app(hInstance);
    ReactiveCounter counterApp(&app);

    // ✅ Build is called ONCE during initialization
    app.build([&counterApp]()
              { return counterApp.build(); });

    app.createWindow("Reactive Counter - Optimized!", 400, 300);

    return app.run();
}
