#include "flux/flux.hpp"

class CounterDisplay : public Widget {
    State<int>& counter;  // reference to parent's state

public:
    CounterDisplay(State<int>& counter) : counter(counter) {}

    WidgetPtr build() override {
        return Column({
            Text("Current count:"),
            Text(counter),
            Text(counter, [](int v) {
                return v % 2 == 0 ? "Even" : "Odd";
            })
        });
    }
};

class CounterControls : public Widget {
    State<int>& counter;  // reference to parent's state

public:
    CounterControls(State<int>& counter) : counter(counter) {}

    WidgetPtr build() override {
        return Row({
            Button("Increment", [this]() {
                counter.set(counter.get() + 1);
            }),
            Button("Decrement", [this]() {
                counter.set(counter.get() - 1);
            }),
            Button("Reset", [this]() {
                counter.set(0);
            })
        });
    }
};

class MyApp : public Widget {
    State<int> counter{0};

public:
    WidgetPtr build() override {
        return Scaffold(
            AppBar("Flux App"),
            Center(
                Column({
                    std::make_shared<CounterDisplay>(counter),
                    std::make_shared<CounterControls>(counter)
                })
            )
        );
    }
};

WidgetPtr createApp(FluxUI *app) {
    return FluxApp("FluxUI - App",
                   std::make_shared<MyApp>(),
                   AppTheme::light(), false,
                   900, 700, false, false);
}