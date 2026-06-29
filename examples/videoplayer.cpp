#include "flux/flux.hpp"

class MyApp : public Widget
{
public:
  WidgetPtr build() override
  {
    return Flex(
               {VideoPlayer("assets/sample.mp4")
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
  return FluxApp("Video Player App")
      .setTheme(AppTheme::light())
      .setFullscreenMode(true)
      .build(std::make_shared<MyApp>());
}