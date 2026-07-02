#include "flux/flux.hpp"

class MyApp : public Widget
{
public:
    WidgetPtr build() override
    {
        auto header = Flex({Text("fillTrack() Test")->setFontSize(20)})
                          ->setAlignItems(AlignItems::Center)
                          ->setJustifyContent(JustifyContent::Center)
                          ->setHeight(56)
                          ->setHeightMode(SizeMode::Fixed)
                          ->setWidthMode(SizeMode::Full);

        auto footer = Flex({Text("v1.0.0")->setFontSize(12)})
                          ->setAlignItems(AlignItems::Center)
                          ->setJustifyContent(JustifyContent::Center)
                          ->setHeight(32)
                          ->setHeightMode(SizeMode::Fixed)
                          ->setWidthMode(SizeMode::Full);

        auto cell = [](const std::string &label, Color bg)
        {
            return Flex({Text(label)->setFontSize(13)})
                ->setAlignItems(AlignItems::Center)
                ->setJustifyContent(JustifyContent::Center)
                ->setWidthMode(SizeMode::Full)
                ->setHeightMode(SizeMode::Full)
                ->setBackgroundColor(bg)
                ->setBorderRadius(6);
        };

        auto testA = Grid({
                              cell("px(80)", Color::fromRGB(220, 235, 255)),
                              cell("px(80)", Color::fromRGB(220, 235, 255)),
                              cell("fillTrack(1)", Color::fromRGB(180, 220, 180)),
                              cell("fillTrack(1)", Color::fromRGB(180, 220, 180)),
                          })
                         ->setColumns({fr(1), fr(1)})
                         ->setRows({px(80), fillTrack(1)})
                         ->setGap(8)
                         ->setPadding(8)
                         ->setWidthMode(SizeMode::Full)
                         ->setHeightMode(SizeMode::Full)
                         ->setBackgroundColor(Color::fromRGB(245, 245, 248))
                         ->setBorderRadius(8);

        auto testB = Grid({
                              cell("px(60)", Color::fromRGB(255, 230, 200)),
                              cell("px(60)", Color::fromRGB(255, 230, 200)),
                              cell("fill(1) — 1/3", Color::fromRGB(255, 200, 180)),
                              cell("fill(1) — 1/3", Color::fromRGB(255, 200, 180)),
                              cell("fill(2) — 2/3", Color::fromRGB(240, 160, 140)),
                              cell("fill(2) — 2/3", Color::fromRGB(240, 160, 140)),
                          })
                         ->setColumns({fr(1), fr(1)})
                         ->setRows({px(60), fillTrack(1), fillTrack(2)})
                         ->setGap(8)
                         ->setPadding(8)
                         ->setWidthMode(SizeMode::Full)
                         ->setHeightMode(SizeMode::Full)
                         ->setBackgroundColor(Color::fromRGB(245, 245, 248))
                         ->setBorderRadius(8);

        auto testC = Grid({
                              cell("fill(1)", Color::fromRGB(230, 210, 255)),
                              cell("fill(1)", Color::fromRGB(230, 210, 255)),
                              cell("fill(1)", Color::fromRGB(210, 185, 255)),
                              cell("fill(1)", Color::fromRGB(210, 185, 255)),
                              cell("fill(1)", Color::fromRGB(190, 160, 255)),
                              cell("fill(1)", Color::fromRGB(190, 160, 255)),
                          })
                         ->setColumns({fr(1), fr(1)})
                         ->setRows({fillTrack(1), fillTrack(1), fillTrack(1)})
                         ->setGap(8)
                         ->setPadding(8)
                         ->setWidthMode(SizeMode::Full)
                         ->setHeightMode(SizeMode::Full)
                         ->setBackgroundColor(Color::fromRGB(245, 245, 248))
                         ->setBorderRadius(8);

        // Declare labels and panels before body
        auto labels = Flex({
                               Flex({Text("A: px + fill(1)")->setFontSize(11)})
                                   ->setJustifyContent(JustifyContent::Center)
                                   ->setWidthMode(SizeMode::Full),
                               Flex({Text("B: px + fill(1) + fill(2)")->setFontSize(11)})
                                   ->setJustifyContent(JustifyContent::Center)
                                   ->setWidthMode(SizeMode::Full),
                               Flex({Text("C: fill(1) × 3")->setFontSize(11)})
                                   ->setJustifyContent(JustifyContent::Center)
                                   ->setWidthMode(SizeMode::Full),
                           })
                          ->setGap(12)
                          ->setPaddingHV(16, 0)
                          ->setWidthMode(SizeMode::Full)
                          ->setHeight(28)
                          ->setHeightMode(SizeMode::Fixed);

        auto panels = Flex({testA, testB, testC})
                          ->setGap(12)
                          ->setPadding(16)
                          ->setWidthMode(SizeMode::Full)
                          ->setHeightMode(SizeMode::Full);

        auto body = Flex({labels, panels})
                        ->setDirection(FlexDirection::Column)
                        ->setWidthMode(SizeMode::Full)
                        ->setHeightMode(SizeMode::Full);

        return Page()
            ->setHeader(header)
            ->setBody(body)
            ->setFooter(footer)
            ->setHeaderBackgroundColor(Color::fromRGB(240, 240, 245))
            ->setHeaderElevation(4)
            ->setBodyBackgroundColor(Color::fromRGB(255, 255, 255))
            ->setFooterBackgroundColor(Color::fromRGB(240, 240, 245));
    }
};

WidgetPtr createApp(FluxUI *app)
{
    return FluxApp("FluxUI Grid Demo")
        .setTheme(AppTheme::light())
        .setFullscreenMode(true)
        .build(std::make_shared<MyApp>());
}