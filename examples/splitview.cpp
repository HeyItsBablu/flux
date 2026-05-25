#include "flux/flux.hpp"

class MyApp : public Widget
{
public:
    WidgetPtr build() override
    {
        return Scaffold(
            AppBar("Vertical SplitView Demo"),
            Expanded(SplitViewVertical(

                         // ── Top pane ──────────────────────────────────────────────────
                         Container(
                             Column({
                                        Text("Top Panel")
                                            ->setFontWeight(FontWeight::Bold)
                                            ->setFontSize(18),
                                        SizedBox(0, 12),
                                        Text("Drag the divider to resize.")
                                            ->setFontSize(13)
                                            ->setTextColor(Color::fromRGB(120, 120, 120)),
                                    })
                                 ->setSpacing(8))
                             ->setPadding(20)
                             ->setBackgroundColor(Color::fromRGB(240, 244, 255)),

                         // ── Bottom pane ───────────────────────────────────────────────
                         Container(
                             Column({
                                        Text("Bottom Panel")
                                            ->setFontWeight(FontWeight::Bold)
                                            ->setFontSize(18),
                                        SizedBox(0, 12),
                                        Text("Content goes here.")
                                            ->setFontSize(13)
                                            ->setTextColor(Color::fromRGB(120, 120, 120)),
                                    })
                                 ->setSpacing(8))
                             ->setPadding(20)
                             ->setBackgroundColor(Color::fromRGB(255, 255, 255)),

                         0.4f // initial ratio — top pane gets 40%
                         )
                         ->setMinPaneWidth(80)
                         ->setDividerColor(Color::fromRGB(210, 210, 210))
                         ->setDividerHoverColor(Color::fromRGB(33, 150, 243))),
            nullptr, nullptr);
    }
};

WidgetPtr createApp(FluxUI *app)
{
    return FluxApp(
        "FluxUI - Paint",
        std::make_shared<MyApp>(),
        AppTheme::light(),
        false, // debugShowWidgetBounds
        900,   // width
        700,   // height
        false, // maximize
        true   // fullscreen
    );
}