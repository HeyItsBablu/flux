#include "flux/flux.hpp"

class CounterDisplay : public Widget {
  State<int> &counter; // reference to parent's state

public:
  CounterDisplay(State<int> &counter) : counter(counter) {}

  WidgetPtr build() override {
    return Container(Column({Text("Current count:"), Text(counter),
                             Text(counter,
                                  [](int v) {
                                    return v % 2 == 0 ? "Even" : "Odd";
                                  })}))
        ->setHeight(400);
  }
};

class CounterControls : public Widget {
  State<int> &counter; // reference to parent's state

public:
  CounterControls(State<int> &counter) : counter(counter) {}

  WidgetPtr build() override {
    return Row({Button("Increment", [this]() { counter++; }),
                Button("Decrement", [this]() { counter--; }),
                Button("Reset", [this]() { counter.set(0); })});
  }
};

class MyApp : public Widget {
  State<int> counter{0};

public:
  WidgetPtr build() override {
    return Scaffold(
        AppBar("Flux App"),
        Expanded(Center(Column({std::make_shared<CounterDisplay>(counter),
                                std::make_shared<CounterControls>(counter)}))),
        nullptr, nullptr);
  }
};

WidgetPtr createApp(FluxUI *app) {
  return FluxApp("FluxUI - App", std::make_shared<MyApp>(), AppTheme::light(),
                 false, 900, 700, false, false);
}