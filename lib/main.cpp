#include "flux/flux.hpp"

class MyApp : public Widget
{
public:
  WidgetPtr build() override
  {
    return Flex(
               {VideoPlayer("https://avtshare01.rz.tu-ilmenau.de/avt-vqdb-uhd-1/test_1/segments/bigbuck_bunny_8bit_2000kbps_720p_60.0fps_h264.mp4")
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