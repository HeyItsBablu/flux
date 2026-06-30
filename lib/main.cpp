#include "flux/flux.hpp"

class MyApp : public Widget
{
public:
    WidgetPtr build() override
    {

        return Flex({
                        AudioPlayer("TumMile.mp3")
                            ->setWidth(280),
                    })
            ->setBackgroundColor(Color::fromRGB(280, 180, 180))
            ->setAlignItems(AlignItems::Center)
            ->setJustifyContent(JustifyContent::Center)
            ->setWidthMode(SizeMode::Full)
            ->setHeightMode(SizeMode::Full);
    }
};

WidgetPtr createApp(FluxUI *app)
{
    return FluxApp("Audio Player App")
        .setTheme(AppTheme::light())
        .setFullscreenMode(true)
        .build(std::make_shared<MyApp>());
}