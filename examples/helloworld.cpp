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


// ============================================================
//  Entry point
// ============================================================

WidgetPtr createApp(FluxUI *app)
{
    return FluxApp("Wrap App")
        .setTheme(AppTheme::light())
        .setFullscreenMode(true)
        .build(std::make_shared<MyApp>());
}