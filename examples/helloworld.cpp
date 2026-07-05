//app/src/main/cpp/main.cpp
#include "flux/flux.hpp"

class MyApp : public Widget
{
    State<int> counter = 0;
public:
    WidgetPtr build() override
    {

        return Flex({Text("Hello World"),Text(counter),Button("Click Me",[this]{counter++})})
                ->setBackgroundColor(Color::fromRGB(255, 180, 180))
                ->setAlignItems(AlignItems::Center)
                ->setJustifyContent(JustifyContent::Center)
                ->setAlignContent(AlignContent::Center)
                ->setWidthMode(SizeMode::Full)
                ->setHeightMode(SizeMode::Full)->setDirection(FlexDirection::Column);
    }
};


// ============================================================
//  Entry point
// ============================================================

WidgetPtr createApp(FluxUI *app)
{
  return FluxApp()
      .setTheme(AppTheme::light())
      .build(std::make_shared<MyApp>());
}