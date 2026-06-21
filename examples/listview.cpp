#include "flux/flux.hpp"

class MyApp : public Widget
{

public:
    WidgetPtr build() override
    {

        return Flex({Flex({Text("App Bar")})
                         ->setBackgroundColor(Color::fromRGB(50, 50, 150))
                         ->setPadding(12)
                         ->setWidthMode(SizeMode::Full)
                         ->setHeight(50),
                     Flex(
                         {

                             Flex({Text("Hello")})
                                 ->setAlignItems(AlignItems::Center)
                                 ->setJustifyContent(JustifyContent::Center)
                                 ->setWidth(80)
                                 ->setHeight(80)
                                 ->setBorderRadius(8)
                                 ->setBackgroundColor(Color::fromRGB(100, 100, 100)),

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
                             Text("Item 3")})
                         ->setBackgroundColor(Color::fromRGB(280, 180, 180))
                         ->setScrollable(true)
                         ->setDirection(FlexDirection::Column)
                         ->setGap(8)
                         ->setAlignItems(AlignItems::Stretch)
                         ->setWidthMode(SizeMode::Full)
                         ->setHeightMode(SizeMode::Full)})
            ->setWidthMode(SizeMode::Full)
            ->setHeightMode(SizeMode::Full)
            ->setDirection(FlexDirection::Column);
    }
};

WidgetPtr createApp(FluxUI *app)
{
    return FluxApp("FluxUI - List", std::make_shared<MyApp>(), AppTheme::light(),
                   false, // debugShowWidgetBounds
                   900,   // width
                   700,   // height
                   false, // maximize
                   false  // fullscreen
    );
}