#include "flux.hpp"


class GridViewDemoComponent : public Component {
private:
  State<std::vector<std::string>> items{ {
      "Apple", "Banana", "Cherry", "Dragonfruit",
      "Elderberry", "Fig", "Grape", "Honeydew",
      "Kiwi", "Lemon", "Mango", "Nectarine",
  }, context };

public:
  WidgetPtr build() override {
    return Scaffold(
        AppBar("GridView Demo"),
        GridView(items)
            ->columns(3)
            ->setSpacing(12)
            ->itemBuilder([](int i, const std::string &name) -> WidgetPtr {
                return Card(
                    Column(
                        Text(std::to_string(i + 1))
                            ->setFontSize(11)
                            ->setTextColor(RGB(160, 160, 160)),
                        Text(name)
                            ->setFontSize(14)
                            ->setFontWeight(FontWeight::Bold)
                    )->setSpacing(4)
                );
            })
    );
  }
};

WidgetPtr createApp(FluxUI *app) {
  return FluxApp("GridView Demo", BuildComponent<GridViewDemoComponent>(),
                 AppTheme::materialBlue());
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
  FluxUI app(hInstance);
  app.build([&]() { return createApp(&app); });
  app.createWindow("FluxUI - GridView Demo", 900, 700);
  return app.run();
}