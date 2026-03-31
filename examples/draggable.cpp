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
                        ->setBackgroundColor(Color::fromRGB(70, 130, 180))
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


WidgetPtr createApp(FluxUI* app) {
    return FluxApp(
        "FluxUI - Draggable",
        BuildComponent<DragDemo>(),
        AppTheme::dark(),
        false,   // debugShowWidgetBounds
        900,     // width
        700,     // height
        false,   // maximize
        true     // fullscreen
    );
}