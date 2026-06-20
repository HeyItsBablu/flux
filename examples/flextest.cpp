#include "flux/flux.hpp"

class MyApp : public Widget
{

public:
    WidgetPtr build() override
    {
        return Flex({
                        Flex({Text("1x")})
                            ->setBackgroundColor(Color::fromRGB(220, 80, 80))
                            ->setPadding(12)
                            ->setFlexGrow(1),

                        Flex({Text("2x")})
                            ->setBackgroundColor(Color::fromRGB(80, 180, 80))
                            ->setPadding(12)
                            ->setFlexGrow(2),

                        Flex({Text("1x")})
                            ->setBackgroundColor(Color::fromRGB(80, 80, 220))
                            ->setPadding(12)
                            ->setFlexGrow(1),
                    })
            ->setDirection(FlexDirection::Row)
            ->setGap(8)
            ->setPadding(16)
            ->setAlignItems(AlignItems::Stretch)
            ->setWidthMode(SizeMode::Full)
            ->setHeightMode(SizeMode::Full);
    }
};

WidgetPtr createApp(FluxUI *app)
{
    return FluxApp("FluxUI - App", std::make_shared<MyApp>(), AppTheme::light(),
                   false, 900, 700, false, false);
}