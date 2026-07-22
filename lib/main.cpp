#include "flux/flux.hpp"

class MyApp : public Widget
{
public:
  WidgetPtr build() override
  {
    std::vector<BoxChild> items;
    for (int i = 1; i <= 20; i++)
    {
      items.push_back(
          Box({
                  Box({
                          Image("https://picsum.photos/seed/list" + std::to_string(i) + "/100/100")
                              ->setFit(ImageFit::Cover)
                              ->setBorderRadius(8),
                      })
                      ->setWidthMode(SizeMode::Fixed)
                      ->setWidth(60)
                      ->setHeightMode(SizeMode::Fixed)
                      ->setHeight(60),

                  Text("Item " + std::to_string(i))
                      ->setFontSize(15)
                      ->setFontWeight(FontWeight::Bold),
              })
              ->setDisplay(Display::Flex)
              ->setDirection(FlexDirection::Row)
              ->setGap(12)
              ->setPadding(10)
              ->setAlignItems(AlignItems::Center)
              ->setBackgroundColor(Color::fromRGB(255, 255, 255))
              ->setBorderColor(Color::fromRGB(230, 230, 230))
              ->setBorderWidth(1)
              ->setBorderRadius(8)
              ->setWidthMode(SizeMode::Full));
    }

    return Box(items)
        ->setDisplay(Display::Block)
        ->setGap(8)
        ->setPadding(16)
        ->setWidthMode(SizeMode::Full)
        ->setHeightMode(SizeMode::Full)
        ->setScrollable(true);
  }
};

WidgetPtr createApp(FluxUI *app)
{
  return FluxApp().setTheme(AppTheme::light()).build(std::make_shared<MyApp>());
}