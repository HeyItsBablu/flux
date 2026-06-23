//app/src/main/cpp/main.cpp
#include "flux/flux.hpp"

class MyApp : public Widget
{
public:
    WidgetPtr build() override
    {

        return Flex({Text("Hello World")})
                ->setBackgroundColor(Color::fromRGB(255, 180, 180))
                ->setAlignItems(AlignItems::Center)
                ->setJustifyContent(JustifyContent::Center)
                ->setAlignContent(AlignContent::Center)
                ->setWidthMode(SizeMode::Full)
                ->setHeightMode(SizeMode::Full)->setDirection(FlexDirection::Column);
    }
};

WidgetPtr createApp(FluxUI *app)
{
    return FluxApp("FluxUI - App", std::make_shared<MyApp>(), AppTheme::light(),
                   false, 900, 700, false, false);
}