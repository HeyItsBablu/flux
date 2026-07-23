#include "flux/flux.hpp"

class MyApp : public Widget
{
    State<int> counter = 0;

public:
    WidgetPtr build() override
    {
        constexpr int kFabSize = 56;
        constexpr int kAppBarHeight = 56;

        auto appBar =
            Box({
                    Text("My App")
                        ->setFontSize(20)
                        ->setFontWeight(FontWeight::Bold)
                        ->setTextColor(Color::fromRGB(255, 255, 255)),
                })
                ->setDisplay(Display::Flex)
                ->setDirection(FlexDirection::Row)
                ->setAlignItems(AlignItems::Center)
                ->setJustifyContent(JustifyContent::Start)
                ->setPaddingHV(16, 0)
                ->setWidthMode(SizeMode::Full)
                ->setHeightMode(SizeMode::Fixed)
                ->setHeight(kAppBarHeight)
                ->setBackgroundColor(Color::fromRGB(33, 150, 243));

        auto fab =
            Box({
                    Text("+")
                        ->setFontSize(28)
                        ->setFontWeight(FontWeight::Bold)
                        ->setTextColor(Color::fromRGB(255, 255, 255)),
                })
                ->setDisplay(Display::Flex)
                ->setAlignItems(AlignItems::Center)
                ->setJustifyContent(JustifyContent::Center)
                ->setWidthMode(SizeMode::Fixed)
                ->setWidth(kFabSize)
                ->setHeightMode(SizeMode::Fixed)
                ->setHeight(kFabSize)
                ->setBorderRadius(kFabSize / 2)
                ->setBackgroundColor(Color::fromRGB(33, 150, 243))
                ->setPositionMode(Position::Absolute)
                ->setBottomPx(24)
                ->setRightPx(24)
                ->setZIndexVal(1)
                ->setOnClick([this]
                             { counter++; });

        auto body =
            Box({
                    Text("You have pushed the button this many times:")
                        ->setFontSize(14)
                        ->setTextColor(Color::fromRGB(90, 90, 90)),
                    Text(counter)
                        ->setFontSize(40)
                        ->setFontWeight(FontWeight::Bold),
                })
                ->setDisplay(Display::Flex)
                ->setDirection(FlexDirection::Column)
                ->setAlignItems(AlignItems::Center)
                ->setJustifyContent(JustifyContent::Center)
                ->setGap(8)
                ->setWidthMode(SizeMode::Full)
                ->setHeightMode(SizeMode::Full);


        return Box({
                       appBar,
                       body,
                       fab,
                   })
            ->setDisplay(Display::Flex)
            ->setDirection(FlexDirection::Column)
            ->setWidthMode(SizeMode::Full)
            ->setHeight(500)
            ->setBackgroundColor(Color::fromRGB(215, 245, 250));
    }
};

// ============================================================
//  Entry point
// ============================================================

WidgetPtr createApp(FluxUI *app)
{
    return FluxApp()
        .setTheme(AppTheme::light())
        .build(std::make_shared<MyApp>());
}