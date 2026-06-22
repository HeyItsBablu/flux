#include "flux/flux.hpp"

class MyApp : public Widget
{
public:
    WidgetPtr build() override
    {

        return Flex({

                        // ── 1. Alignment ──────────────────────────────────────────────
                        Flex({Text("Left aligned text")
                                  ->setFontSize(14)
                                  ->setTextColor(Color::fromRGB(255, 255, 255))
                                  ->setTextAlign(TextAlign::Left)})
                            ->setBackgroundColor(Color::fromRGB(60, 60, 60))
                            ->setPadding(10)
                            ->setAlignItems(AlignItems::Stretch)
                            ->setWidthMode(SizeMode::Full)
                            ->setHeightMode(SizeMode::Fit),

                        Flex({Text("Center aligned text")
                                  ->setFontSize(14)
                                  ->setTextColor(Color::fromRGB(255, 255, 255))
                                  ->setTextAlign(TextAlign::Center)})
                            ->setBackgroundColor(Color::fromRGB(100, 100, 100))
                            ->setPadding(10)
                            ->setAlignItems(AlignItems::Stretch)
                            ->setWidthMode(SizeMode::Full)
                            ->setHeightMode(SizeMode::Fit),

                        Flex({Text("Right aligned text")
                                  ->setFontSize(14)
                                  ->setTextColor(Color::fromRGB(255, 255, 255))
                                  ->setTextAlign(TextAlign::Right)})
                            ->setBackgroundColor(Color::fromRGB(60, 60, 60))
                            ->setPadding(10)
                            ->setAlignItems(AlignItems::Stretch)
                            ->setWidthMode(SizeMode::Full)
                            ->setHeightMode(SizeMode::Fit),

                        // ── 2. Ellipsis ───────────────────────────────────────────────
                        Flex({Text("This is a very long text that should be clipped "
                                   "with an ellipsis at the end of the line")
                                  ->setFontSize(14)
                                  ->setTextColor(Color::fromRGB(30, 30, 30))
                                  ->setOverflow(TextOverflow::Ellipsis)
                                  ->setSoftWrap(false)
                                  ->setMaxLines(1)})
                            ->setBackgroundColor(Color::fromRGB(255, 240, 200))
                            ->setPadding(10)
                            ->setWidth(420)
                            ->setHeightMode(SizeMode::Fit),

                        // ── 3. Clip ───────────────────────────────────────────────────
                        Flex({Text("This is a very long text that gets hard-clipped "
                                   "with no ellipsis shown at the boundary")
                                  ->setFontSize(14)
                                  ->setTextColor(Color::fromRGB(30, 30, 30))
                                  ->setOverflow(TextOverflow::Clip)
                                  ->setSoftWrap(false)
                                  ->setMaxLines(1)})
                            ->setBackgroundColor(Color::fromRGB(200, 240, 255))
                            ->setPadding(10)
                            ->setWidth(420)
                            ->setHeightMode(SizeMode::Fit),

                        // ── 4. Wrap + maxLines ────────────────────────────────────────
                        Flex({Text("This long sentence will wrap across multiple lines "
                                   "but stop after two lines maximum no matter how much "
                                   "content remains here")
                                  ->setFontSize(14)
                                  ->setTextColor(Color::fromRGB(30, 30, 30))
                                  ->setSoftWrap(true)
                                  ->setMaxLines(2)
                                  ->setOverflow(TextOverflow::Ellipsis)})
                            ->setBackgroundColor(Color::fromRGB(220, 255, 220))
                            ->setPadding(10)
                            ->setAlignItems(AlignItems::Stretch)
                            ->setWidthMode(SizeMode::Full)
                            ->setHeightMode(SizeMode::Fit),

                        // ── 5. Letter spacing ─────────────────────────────────────────
                        Flex({Text("Wide letter spacing")
                                  ->setFontSize(14)
                                  ->setTextColor(Color::fromRGB(30, 30, 30))
                                  ->setLetterSpacing(4.0f)})
                            ->setBackgroundColor(Color::fromRGB(255, 220, 255))
                            ->setPadding(10)
                            ->setWidthMode(SizeMode::Fit)
                            ->setHeightMode(SizeMode::Fit),

                        // ── 6. Line height ────────────────────────────────────────────
                        Flex({Text("Line one\nLine two\nLine three")
                                  ->setFontSize(14)
                                  ->setTextColor(Color::fromRGB(30, 30, 30))
                                  ->setHeight(2.0f)
                                  ->setSoftWrap(true)})
                            ->setBackgroundColor(Color::fromRGB(255, 255, 200))
                            ->setPadding(10)
                            ->setWidthMode(SizeMode::Fit)
                            ->setHeightMode(SizeMode::Fit),

                        // ── 7. Decorations ────────────────────────────────────────────
                        Flex({
                                 Flex({Text("Underline")
                                           ->setFontSize(14)
                                           ->setTextColor(Color::fromRGB(30, 30, 30))
                                           ->setDecoration(TextDecoration::Underline)
                                           ->setDecorationColor(Color::fromRGB(200, 0, 0))
                                           ->setDecorationStyle(TextDecorationStyle::Solid)})
                                     ->setBackgroundColor(Color::fromRGB(240, 240, 240))
                                     ->setPadding(10)
                                     ->setWidthMode(SizeMode::Fit)
                                     ->setHeightMode(SizeMode::Fit),

                                 Flex({Text("Strikethrough")
                                           ->setFontSize(14)
                                           ->setTextColor(Color::fromRGB(30, 30, 30))
                                           ->setDecoration(TextDecoration::LineThrough)
                                           ->setDecorationColor(Color::fromRGB(0, 0, 200))})
                                     ->setBackgroundColor(Color::fromRGB(240, 240, 240))
                                     ->setPadding(10)
                                     ->setWidthMode(SizeMode::Fit)
                                     ->setHeightMode(SizeMode::Fit),

                                 Flex({Text("Wavy")
                                           ->setFontSize(14)
                                           ->setTextColor(Color::fromRGB(30, 30, 30))
                                           ->setDecoration(TextDecoration::Underline)
                                           ->setDecorationStyle(TextDecorationStyle::Wavy)
                                           ->setDecorationColor(Color::fromRGB(200, 100, 0))})
                                     ->setBackgroundColor(Color::fromRGB(240, 240, 240))
                                     ->setPadding(10)
                                     ->setWidthMode(SizeMode::Fit)
                                     ->setHeightMode(SizeMode::Fit),
                             })
                            ->setDirection(FlexDirection::Row)
                            ->setGap(8)
                            ->setWidthMode(SizeMode::Fit)
                            ->setHeightMode(SizeMode::Fit),

                        // ── 8. Shadow ─────────────────────────────────────────────────
                        Flex({Text("Drop shadow text")
                                  ->setFontSize(18)
                                  ->setTextColor(Color::fromRGB(255, 255, 255))
                                  ->setShadow(TextShadow(Color::fromRGBA(0, 0, 0, 180), 3, 3, 2))})
                            ->setBackgroundColor(Color::fromRGB(80, 120, 200))
                            ->setPadding(12)
                            ->setWidthMode(SizeMode::Fit)
                            ->setHeightMode(SizeMode::Fit),

                        // ── 9. Vertical alignment ─────────────────────────────────────
                        Flex({
                                 Flex({Text("Top")
                                           ->setFontSize(13)
                                           ->setTextColor(Color::fromRGB(30, 30, 30))
                                           ->setTextAlign(TextAlign::Center)
                                           ->setTextAlignVertical(TextAlignVertical::Top)})
                                     ->setBackgroundColor(Color::fromRGB(200, 230, 255))
                                     ->setPadding(6)
                                     ->setFlexGrow(1)
                                     ->setHeight(70),

                                 Flex({Text("Center")
                                           ->setFontSize(13)
                                           ->setTextColor(Color::fromRGB(30, 30, 30))
                                           ->setTextAlign(TextAlign::Center)
                                           ->setTextAlignVertical(TextAlignVertical::Center)})
                                     ->setBackgroundColor(Color::fromRGB(200, 255, 230))
                                     ->setPadding(6)
                                     ->setFlexGrow(1)
                                     ->setHeight(70),

                                 Flex({Text("Bottom")
                                           ->setFontSize(13)
                                           ->setTextColor(Color::fromRGB(30, 30, 30))
                                           ->setTextAlign(TextAlign::Center)
                                           ->setTextAlignVertical(TextAlignVertical::Bottom)})
                                     ->setBackgroundColor(Color::fromRGB(255, 230, 200))
                                     ->setPadding(6)
                                     ->setFlexGrow(1)
                                     ->setHeight(70),
                             })
                            ->setDirection(FlexDirection::Row)
                            ->setGap(8)
                            ->setAlignItems(AlignItems::Stretch)
                            ->setWidthMode(SizeMode::Full)
                            ->setHeightMode(SizeMode::Fit),

                        // ── 10. StyledText ────────────────────────────────────────────
                        Flex({StyledText(
                                 "Bold with letter spacing and shadow",
                                 TextStyle()
                                     .withFontSize(16)
                                     .withFontWeight(FontWeight::Bold)
                                     .withColor(Color::fromRGB(255, 255, 255))
                                     .withLetterSpacing(2.f)
                                     .withShadow(TextShadow(Color::fromRGBA(0, 0, 0, 160), 2, 2, 1)))})
                            ->setBackgroundColor(Color::fromRGB(50, 50, 100))
                            ->setPadding(12)
                            ->setWidthMode(SizeMode::Fit)
                            ->setHeightMode(SizeMode::Fit),

                    })
            ->setDirection(FlexDirection::Column)
            ->setAlignItems(AlignItems::Start)
            ->setGap(8)
            ->setPadding(16)
            ->setWidthMode(SizeMode::Full)
            ->setHeightMode(SizeMode::Full)
            ->setScrollable(true);
    }
};

WidgetPtr createApp(FluxUI *app)
{
    return FluxApp("Flux - Text Features (Flex only)", std::make_shared<MyApp>(),
                   AppTheme::light(), false, 640, 780, false, false);
}