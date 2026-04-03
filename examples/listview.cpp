#include "flux/flux.hpp"

class MyApp : public Component {
  State<bool> showBadge;
  State<int> notifCount;

public:
  MyApp() : showBadge(true, context), notifCount(0, context) {}

  WidgetPtr build() override {
    return Scaffold(
        AppBar("Stack Demo"),
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
            ->setPadding(12)

    );
  }
};

WidgetPtr createApp(FluxUI *app) {
  return FluxApp("FluxUI - Paint", BuildComponent<MyApp>(), AppTheme::light(),
                 false, // debugShowWidgetBounds
                 900,   // width
                 700,   // height
                 false, // maximize
                 false  // fullscreen
  );
}