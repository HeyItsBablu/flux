#include "flux/flux.hpp"

class DragDemo : public Component {

  State<int> posX;
  State<int> posY;

public:
  DragDemo() : posX(50, context), posY(50, context) {}

  WidgetPtr build() override {
    return Scaffold(
        AppBar("Drag Demo"),
        Stack(
            Positioned(
                GestureDetector(
                    Container()
                        ->setWidth(100)
                        ->setHeight(100)
                        ->setBackgroundColor(RGB(70, 130, 180))
                        ->setBorderRadius(12),
                    [this](int dx, int dy) {
                        posX.set(posX.get() + dx);
                        posY.set(posY.get() + dy);
                    }
                ),
                posX, [](int x) { return x; },
                posY, [](int y) { return y; }
            )
        )->setExpand(true)
    );
  }
};

WidgetPtr createApp(FluxUI *app) {
  return FluxApp("List View Demo", BuildComponent<DragDemo>(),
                 AppTheme::light());
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
  FluxUI app(hInstance);
  app.build([&]() { return createApp(&app); });
  app.createWindow("FluxUI - List View Demo", 600, 600);
  return app.run();
}