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
                            ->setWidth(420)
                            ->setHeight(100),

                        Flex({Text("Nav")})
                            ->setBackgroundColor(Color::fromRGB(50, 50, 150))
                            ->setPadding(12)
                            ->setWidthMode(SizeMode::Full)
                            ->setHeightMode(SizeMode::Fit),

                        Flex({Text("Nav")})
                            ->setBackgroundColor(Color::fromRGB(50, 50, 150))
                            ->setPadding(12)
                            ->setWidthMode(SizeMode::Fit)
                            ->setHeightMode(SizeMode::Fit),
                        Flex({Text("Nav")})
                            ->setBackgroundColor(Color::fromRGB(50, 50, 150))
                            ->setPadding(12)
                            ->setWidth(420)
                            ->setHeightMode(SizeMode::Fit),

                        Flex({Text("Nav")})
                            ->setBackgroundColor(Color::fromRGB(50, 50, 150))
                            ->setPadding(12)
                            ->setWidthMode(SizeMode::Fit)
                            ->setHeight(100),

                    })
            ->setBackgroundColor(Color::fromRGB(280, 180, 180))
            ->setScrollable(false)
            ->setDirection(FlexDirection::Column) // base (mobile): stacked
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