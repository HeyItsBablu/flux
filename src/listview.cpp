#include "flux.hpp"

class ListViewDemo : public Component {
  State<bool> showBadge;
  State<int> notifCount;

public:
  ListViewDemo() : showBadge(true, context), notifCount(0, context) {}

  WidgetPtr build() override {
    return Scaffold(AppBar("Stack Demo"), ListView({
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
                                                   })
                                              ->setSpacing(8)
                                              ->setPadding(12)

    );
  }
};

WidgetPtr createApp(FluxUI *app) {
  return FluxApp("List View Demo", BuildComponent<ListViewDemo>(), AppTheme::light());
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
  FluxUI app(hInstance);
  app.build([&]() { return createApp(&app); });
  app.createWindow("FluxUI - List View Demo", 600, 600);
  return app.run();
}