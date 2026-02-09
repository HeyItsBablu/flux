#include "flux.hpp"

class TestComponent
{
private:
    FluxUI *ui;
    State<int> counter;
    State<std::string> message;

public:
    TestComponent(FluxUI *app)
        : ui(app),
          counter(0, app),
          message("Welcome! Click a button.", app) {}

    void increment()
    {
        counter++;
        message = "Counter incremented to " + std::to_string(counter.get());
    }

    void decrement()
    {
        counter--;
        message = "Counter decremented to " + std::to_string(counter.get());
    }

    void reset()
    {
        counter = 0;
        message = "Counter has been reset!";
    }

    WidgetPtr build()
    {
        return Center(
            Card(
                Column(
                    // Title
                    Text("Test Center Widget")
                        ->setFontSize(24)
                        ->setFontWeight(FontWeight::Bold)
                        ->setTextColor(RGB(33, 150, 243)),

                    Divider(),

                    // Counter display
                    Text(counter)
                        ->setFontSize(48)
                        ->setFontWeight(FontWeight::Bold)
                        ->setTextColor(RGB(76, 175, 80)),

                    // Message display
                    Text(message)
                        ->setFontSize(14)
                        ->setTextColor(RGB(100, 100, 100)),
                    Divider(),

                    Row(
                        Button("Send Message", [this]()
                               { increment(); })
                            ->setBackgroundColor(RGB(76, 175, 80))
                            ->setMargin(5),
                        Button("Read Message", [this]()
                               { decrement(); })
                            ->setBackgroundColor(RGB(76, 175, 80))
                            ->setMargin(5),
                        Button("Clear Messages", [this]()
                               { reset(); })
                            ->setBackgroundColor(RGB(244, 67, 54))
                            ->setMargin(5))
                        ->setMainAxisAlignment(MainAxisAlignment::Center)
                        ->setSpacing(10),

                    Divider(),

                    // Control buttons
                    Row(
                        Button("-", [this]()
                               { decrement(); })
                            ->setBackgroundColor(RGB(244, 67, 54))
                            ->setWidth(80),

                        Button("Reset", [this]()
                               { reset(); })
                            ->setBackgroundColor(RGB(158, 158, 158))
                            ->setWidth(80),

                        Button("+", [this]()
                               { increment(); })
                            ->setBackgroundColor(RGB(76, 175, 80))
                            ->setWidth(80))
                        ->setSpacing(10)
                        ->setMainAxisAlignment(MainAxisAlignment::Center))
                    ->setSpacing(15)
                    ->setCrossAlignment(Alignment::Center))

        );
    }
};

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int)
{
    FluxUI app(hInstance);
    TestComponent test(&app);

    app.build([&test]()
              { return test.build(); });

    app.createWindow("Testing Center Fix", 600, 400);

    return app.run();
}