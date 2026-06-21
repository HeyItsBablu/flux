#include "flux/flux.hpp"

class MyApp : public Widget
{
public:
  WidgetPtr build() override
  {

    return Flex({Text("Hello, Flux!")})
        ->setBackgroundColor(Color::fromRGB(280, 180, 180))
        ->setAlignItems(AlignItems::Center)
        ->setJustifyContent(JustifyContent::Center)
        ->setWidthMode(SizeMode::Full)
        ->setHeightMode(SizeMode::Full);
  }
};

WidgetPtr createApp(FluxUI *app)
{
  return FluxApp("FluxUI - App", std::make_shared<MyApp>(), AppTheme::light(),
                 false, 900, 700, false, false);
}