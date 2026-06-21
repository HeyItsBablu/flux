#include "flux/flux.hpp"

class MyApp : public Widget
{

public:
    WidgetPtr build() override
    {
        return Flex({
                        Flex({Text("Nav")})
                            ->setBackgroundColor(Color::fromRGB(50, 50, 150))
                            ->setPadding(12)
                            ->setWidth(200)
                            ->setHeight(500),

                        Flex({Text("Content")})
                            ->setBackgroundColor(Color::fromRGB(80, 180, 80))
                            ->setPadding(12)
                            ->setWidth(500)
                            ->setHeight(500),

                        Flex({Text("Sidebar")})
                            ->setBackgroundColor(Color::fromRGB(180, 80, 80))
                            ->setPadding(12)
                            ->setWidth(300)
                            ->setHeight(500),
                    })
            ->setBackgroundColor(Color::fromRGB(280, 180, 180))
            ->setScrollable(true)
            ->setDirection(FlexDirection::Column) // base (mobile): stacked
            ->setGap(8)
            ->setPadding(16)
            ->setAlignItems(AlignItems::Stretch)
            ->setWidthMode(SizeMode::Full)
            ->setHeightMode(SizeMode::Full)
            ->responsive(Breakpoint::Md, [](FlexProps &p)
                         {
                             p.direction = FlexDirection::Row; // >= 768px: side by side
                         });
    }
};

WidgetPtr createApp(FluxUI *app)
{
    return FluxApp("FluxUI - App", std::make_shared<MyApp>(), AppTheme::light(),
                   false, 900, 700, false, false);
}