// lib/main.cpp
#include "flux/flux.hpp"

class MyApp : public Widget
{
    State<int> counter = 0;
    State<std::string> textState{""};

public:
    WidgetPtr build() override
    {

        return Flex({Text("Hello World")->setTextColor(Color::fromRGB(100, 100, 100)),
                     Text(counter)->setTextColor(Color::fromRGB(100, 100, 100)),
                     Text(textState)->setTextColor(Color::fromRGB(100, 100, 100)),
                     // ── TextInput ─────────────────────────────────────────────
                     Flex({Text("TextInput")
                               ->setFontWeight(FontWeight::Bold)
                               ->setMinWidth(120),

                           TextInput("Type something...")->setInputValue(textState),

                           Text(textState,
                                [](const std::string &v)
                                {
                                    return std::to_string((int)v.size()) + " chars";
                                })
                               ->setMinWidth(60)
                               ->setTextColor(Color::fromRGB(100, 100, 100))})
                         ->setGap(12)
                         ->setPadding(16),
                     Button("Click Me", [this]()
                            { counter++; })})
            ->setBackgroundColor(Color::fromRGB(255, 180, 180))
            ->setAlignItems(AlignItems::Center)
            ->setJustifyContent(JustifyContent::Center)
            ->setAlignContent(AlignContent::Center)
            ->setWidthMode(SizeMode::Full)
            ->setHeightMode(SizeMode::Full)
            ->setDirection(FlexDirection::Column);
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