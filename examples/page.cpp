#include "flux/flux.hpp"

class MyApp : public Widget
{
    State<int> counter{0};

public:
    WidgetPtr build() override
    {
        auto header = Flex({Text("My App")->setFontSize(20)})
            ->setAlignItems(AlignItems::Center)
            ->setJustifyContent(JustifyContent::Center)
            ->setHeight(56)
            ->setHeightMode(SizeMode::Fixed)
            ->setWidthMode(SizeMode::Full);

        auto body = Flex({Text(counter)->setFontSize(18),
                          Button("Click", [this]
                                 { counter++; })})
            ->setAlignItems(AlignItems::Center)
            ->setJustifyContent(JustifyContent::Center)
            ->setAlignContent(AlignContent::Center)
            ->setDirection(FlexDirection::Column)
            ->setGap(8)
            ->setWidthMode(SizeMode::Full)
            ->setHeightMode(SizeMode::Full);

        auto footer = Flex({Text("v1.0.0")->setFontSize(12)})
            ->setAlignItems(AlignItems::Center)
            ->setJustifyContent(JustifyContent::Center)
            ->setHeight(32)
            ->setHeightMode(SizeMode::Fixed)
            ->setWidthMode(SizeMode::Full);

        return Page()
            ->setHeader(header)
            ->setBody(body)
            ->setFooter(footer)
            ->setHeaderBackgroundColor(Color::fromRGB(240, 240, 245))
            ->setHeaderElevation(4)
            ->setBodyBackgroundColor(Color::fromRGB(255, 180, 180))
            ->setFooterBackgroundColor(Color::fromRGB(240, 240, 245));
    }
};



// ============================================================
//  Entry point
// ============================================================

WidgetPtr createApp(FluxUI *app)
{
  return FluxApp("Page App")
      .setTheme(AppTheme::light())
      .setFullscreenMode(true)
      .build(std::make_shared<MyApp>());
}