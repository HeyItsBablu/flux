#include "flux/flux.hpp"

class MyApp : public Widget {

public:
  WidgetPtr build() override {
    return Scaffold(
        AppBar("List view Demo"),
        ListView({Text("Item 1"),
                  Text("Item 2"),
                  Text("Item 3"),
                  Text("Item 1"),
                  Text("Item 2"),
                  Text("Item 3"),
                  Text("Item 1"),
                  Text("Item 2"),
                  Text("Item 3"),
                  Text("Item 1"),
                  Text("Item 2"),
                  Text("Item 3"),
                  Text("Item 1"),
                  Text("Item 2"),
                  Text("Item 3"),
                  Button("Click",
                         []() { std::cout << "Button clicked" << std::endl; }),
                  Text("Item 1"),
                  Text("Item 2"),
                  Text("Item 3"),
                  Text("Item 1"),
                  Text("Item 2"),
                  Text("Item 3"),
                  Text("Item 1"),
                  Text("Item 2"),
                  Text("Item 3"),
                  Text("Item 1"),
                  Text("Item 2"),
                  Text("Item 3"),
                  Text("Item 1"),
                  Text("Item 2"),
                  Text("Item 3"),
                  Button("Click",
                         []() { std::cout << "Button clicked" << std::endl; })})
            ->setSpacing(8)
            ->setPadding(12),
        nullptr, nullptr

    );
  }
};

WidgetPtr createApp(FluxUI *app) {
  return FluxApp("FluxUI - List", std::make_shared<MyApp>(), AppTheme::light(),
                 false, // debugShowWidgetBounds
                 900,   // width
                 700,   // height
                 false, // maximize
                 false  // fullscreen
  );
}