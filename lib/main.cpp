#include "flux/flux.hpp"

class MyApp : public Widget
{
public:
  WidgetPtr build() override
  {
    return Flex(
               {VideoPlayer("7199171-hd_1080_1920_25fps.mp4")
                    ->setWidth(380)
                    ->setHeight(270) // 16:9
                    ->setAutoPlay(false)

               })
        ->setScrollable(true)
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
  return FluxApp()
      .setTheme(AppTheme::light())
      .build(std::make_shared<MyApp>());
}