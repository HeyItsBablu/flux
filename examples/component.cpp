#include "flux/flux.hpp"

class CounterDisplay : public Widget
{
  State<int> &counter; // reference to parent's state

public:
  CounterDisplay(State<int> &counter) : counter(counter) {}

  WidgetPtr build() override
  {
    return Flex({Text("Current count:"), Text(counter),
                 Text(counter,
                      [](int v)
                      {
                        return v % 2 == 0 ? "Even" : "Odd";
                      })})
        ->setHeightMode(SizeMode::Fit);
        //doesn't shows with setHeight but shows with mode
  }
};

class CounterControls : public Widget
{
  State<int> &counter; // reference to parent's state

public:
  CounterControls(State<int> &counter) : counter(counter) {}

  WidgetPtr build() override
  {
    return Flex({Button("Increment", [this]()
                        { counter++; }),
                 Button("Decrement", [this]()
                        { counter--; }),
                 Button("Reset", [this]()
                        { counter.set(0); })});
  }
};

class MyApp : public Widget
{
  State<int> counter{0};

public:
  WidgetPtr build() override
  {

    return Flex({Flex({Text("Nav")})
                     ->setBackgroundColor(Color::fromRGB(50, 50, 150))
                     ->setPadding(12)
                     ->setWidthMode(SizeMode::Full)
                     ->setHeight(50)
                     ->setAlignItems(AlignItems::Center)
                     ->setJustifyContent(JustifyContent::Center)
                     ->setAlignContent(AlignContent::Center),

                 Flex({std::make_shared<CounterDisplay>(counter),
                       std::make_shared<CounterControls>(counter)})
                     ->setDirection(FlexDirection::Column)})

        ->setScrollable(false)
        ->setDirection(FlexDirection::Column) // base (mobile): stacked
        ->setGap(8)
        ->setPadding(16)
        ->setAlignItems(AlignItems::Stretch)
        ->setWidthMode(SizeMode::Full)
        ->setHeightMode(SizeMode::Full);
  }
};

WidgetPtr createApp(FluxUI *app)
{
  return FluxApp("FluxUI - App", std::make_shared<MyApp>(), AppTheme::light(),
                 false, 900, 700, false, false);
}