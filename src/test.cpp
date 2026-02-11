#include "flux.hpp"

#include <windows.h>

// Counter component using class-based inheritance
class CounterComponent : public StatefulComponent
{
private:
    State<int> counter;

public:
    CounterComponent(FluxUI *ctx)
        : StatefulComponent(ctx),
          counter(0, ctx) // Initialize counter state
    {
    }

    void initState() override
    {
        // Optional: Add listener to log changes
        counter.listen([](int value)
                       {
                           // This runs whenever counter changes
                           // OutputDebugString(("Counter changed to: " + std::to_string(value) + "\n").c_str());
                       });
    }

    WidgetPtr build() override
    {
        return Scaffold(

            Center(
                Column(
                    // Display the counter value
                    Text(counter)
                        ->setFontSize(48)
                        ->setFontWeight(FontWeight::Bold)
                        ->setTextColor(RGB(33, 150, 243)),

                    SizedBox(0, 20), // Spacer

                    // Row of buttons
                    Row(
                        Button("Decrement", [this]()
                               {
                                   counter--; // Decrease counter
                               }),

                        SizedBox(20, 0), // Horizontal spacer

                        Button("Increment", [this]()
                               {
                                   counter++; // Increase counter
                               })))
                    ->setPadding(20)));
    }

    void dispose() override
    {
        // Optional: Cleanup when component is destroyed
        counter.clearListeners();
        StatefulComponent::dispose();
    }
};

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    // Create the FluxUI application
    FluxUI app(hInstance);

    // Build the UI using the component
    auto component = ComponentBuilder::create<CounterComponent>(&app);
    app.build([component]()
              {
                  return component->build();
                  // ✅ Component stays alive!
              });

    // Create and show the window
    app.createWindow("Counter Example - Component Based", 400, 300);

    // Run the application
    return app.run();
}