#include "flux/flux.hpp"

class MyApp : public Widget
{
public:
    WidgetPtr build() override
    {
        auto header = Flex({Text("Wrap Demo")->setFontSize(20)})
                          ->setAlignItems(AlignItems::Center)
                          ->setJustifyContent(JustifyContent::Center)
                          ->setHeight(56)
                          ->setHeightMode(SizeMode::Fixed)
                          ->setWidthMode(SizeMode::Full);

        // 9 fixed-size boxes, each 120x80, in a Row that wraps.
        std::vector<WidgetPtr> boxes;
        for (int i = 1; i <= 9; ++i)
        {
            boxes.push_back(
                Flex({Text(std::to_string(i))->setFontSize(16)})
                    ->setAlignItems(AlignItems::Center)
                    ->setJustifyContent(JustifyContent::Center)
                    ->setWidth(120)
                    ->setHeight(80)
                    ->setWidthMode(SizeMode::Fixed)
                    ->setHeightMode(SizeMode::Fixed)
                    ->setBackgroundColor(Color::fromRGB(180, 200, 255))
                    ->setBorderRadius(8));
        }

        auto body = Flex({})
                        ->setDirection(FlexDirection::Row)
                        ->setWrap(FlexWrap::Wrap)
                        
                        ->setGap(12)
                        ->setPadding(16)
                        ->setWidthMode(SizeMode::Full)
                        ->setHeightMode(SizeMode::Full)
                        ->setBackgroundColor(Color::fromRGB(250, 250, 250));

        for (auto &box : boxes)
            body->addChild(box);

        return Page()
            ->setHeader(header)
            ->setBody(body)
            ->setHeaderBackgroundColor(Color::fromRGB(240, 240, 245))
            ->setHeaderElevation(4);
    }
};


// ============================================================
//  Entry point
// ============================================================

WidgetPtr createApp(FluxUI *app)
{
  return FluxApp("Wrap App")
      .setTheme(AppTheme::light())
      .setFullscreenMode(true)
      .build(std::make_shared<MyApp>());
}