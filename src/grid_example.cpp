#include "flux.hpp"
#include "flux_grid.hpp"

class GridDemoComponent : public Component {
public:
  WidgetPtr build() override {

    return Scaffold(
        AppBar("Grid Demo"),
        GridFromList(3, {
            Card(Text("Item 1")),
            Card(Text("Item 2")),
            Card(Text("Item 3")),
            Card(Text("Item 4")),
            Card(Text("Item 5")),
            Card(Text("Item 6")),
        })
        ->setSpacing(12)
        ->setPadding(20)
        ->setCrossAlignment(Alignment::Stretch)
    );
  }
};

WidgetPtr createApp(FluxUI *app) {
  return FluxApp("Grid Demo", BuildComponent<GridDemoComponent>(),
                 AppTheme::materialBlue());
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
  FluxUI app(hInstance);
  app.build([&]() { return createApp(&app); });
  app.createWindow("FluxUI - Grid Demo", 900, 700);
  return app.run();
}