#include "flux/flux.hpp"

class MyApp : public Widget
{
  State<bool> toggleState{false};
  State<double> sliderState{50.0};
  State<bool> checkState{false};
  State<std::string> radioState{"Option A"};
  State<std::string> textState{""};
  State<std::string> textAreaState{""};
  State<double> numberState{42.0};
  State<FluxDate> checkin{FluxDate{}};
  State<FluxDate> checkout{FluxDate{}};

public:
  WidgetPtr build() override
  {
    return Flex(
               {

                   // ── TextArea ──────────────────────────────────────────────
                   Flex({Text("TextArea")
                             ->setFontWeight(FontWeight::Bold)
                             ->setMinWidth(120),
                         TextArea("Write multiple lines...")
                             ->setInputValue(textAreaState)
                             ->setHeight(120)
                             ->setLineNumbers(true),
                         Text(textAreaState,
                              [](const std::string &v)
                              {
                                int lines = 1;
                                for (char c : v)
                                  if (c == '\n')
                                    lines++;
                                return std::to_string(lines) + " lines";
                              })
                             ->setMinWidth(60)
                             ->setTextColor(Color::fromRGB(100, 100, 100))})
                       ->setGap(12)
                       ->setPadding(16),

                   Divider()

               })
        ->setBackgroundColor(Color::fromRGB(280, 180, 180))
        ->setScrollable(false)
        ->setDirection(FlexDirection::Column) // base (mobile): stacked
        ->setGap(8)
        ->setPadding(16)
        ->setAlignItems(AlignItems::Stretch)
        ->setWidthMode(SizeMode::Full)
        ->setHeightMode(SizeMode::Full);
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