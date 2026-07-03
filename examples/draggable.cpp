#include "flux/flux.hpp"

class DragDemo : public Widget
{

    State<int> posX{50};
    State<int> posY{50};

public:
    WidgetPtr build() override
    {
        return Scaffold(
            AppBar("Drag Demo"),
            Expanded(Stack(
                         Positioned(
                             GestureDetector(
                                 Container()
                                     ->setWidth(100)
                                     ->setHeight(100)
                                     ->setBackgroundColor(Color::fromRGB(70, 130, 180))
                                     ->setBorderRadius(12),
                                 [this](int dx, int dy)
                                 {
                                     posX.set(posX.get() + dx);
                                     posY.set(posY.get() + dy);
                                 }),
                             posX, [](int x)
                             { return x; },
                             posY, [](int y)
                             { return y; }))
                         ->setExpand(true)),
            nullptr, nullptr);
    }
};

WidgetPtr createApp(FluxUI *app)
{
  return FluxApp()
      .setTheme(AppTheme::light())
      .build(std::make_shared<DragDemo>());
}