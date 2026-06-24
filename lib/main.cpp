
#include "flux/flux.hpp"

class MyApp : public Widget
{

    State<int> counter{0};

public:
    WidgetPtr build() override
    {

        return Flex({Text(counter)
                         ->setFontSize(18),
                     Button("Click", [this]
                            { counter++; })})
            ->setBackgroundColor(Color::fromRGB(255, 180, 180))
            ->setAlignItems(AlignItems::Center)
            ->setJustifyContent(JustifyContent::Center)
            ->setAlignContent(AlignContent::Center)
            ->setWidthMode(SizeMode::Full)
            ->setHeightMode(SizeMode::Full)
            ->setDirection(FlexDirection::Column)
            ->setGap(8);
    }
};

WidgetPtr createApp(FluxUI *app)
{
    return FluxApp(std::make_shared<MyApp>(), {
                                                  .title = "FluxUI App",
                                                  .theme = AppTheme::dark(),
                                                  .width = 1280,
                                                  .height = 720,
                                              });
}
