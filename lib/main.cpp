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


public:
    WidgetPtr build() override
    {
        return Flex(
                   {




                       // ── Toggle ────────────────────────────────────────────────
                       Flex({Text("Toggle")
                                 ->setFontWeight(FontWeight::Bold)
                                 ->setMinWidth(120),
                             Toggle("Enable feature")
                                 ->setValue(toggleState)
                                 ->setOnToggleChanged(
                                     [this](bool v)
                                     { toggleState.set(v); }),
                             Text(toggleState, [](bool v)
                                  { return v ? "On" : "Off"; })
                                 ->setTextColor(Color::fromRGB(100, 100, 100))})
                           ->setGap(12)
                           ->setPadding(16),

                       Divider(),

                       // ── Slider ────────────────────────────────────────────────
                       Flex({Text("Slider")
                                 ->setFontWeight(FontWeight::Bold)
                                 ->setMinWidth(120),
                             Slider(0.0, 100.0, 1.0)
                                 ->setValue(sliderState)
                                 ->setOnValueChanged(
                                     [this](double v)
                                     { sliderState.set(v); }),
                             Text(sliderState,
                                  [](double v)
                                  { return std::to_string((int)v); })
                                 ->setMinWidth(32)
                                 ->setTextColor(Color::fromRGB(100, 100, 100))})
                           ->setGap(12)
                           ->setPadding(16),

                       Divider(),

                       // ── CheckBox ──────────────────────────────────────────────
                       Flex({Text("CheckBox")
                                 ->setFontWeight(FontWeight::Bold)
                                 ->setMinWidth(120),
                             CheckBox("Accept terms")->setInputValue(checkState),
                             Text(checkState,
                                  [](bool v)
                                  { return v ? "Checked" : "Unchecked"; })
                                 ->setTextColor(Color::fromRGB(100, 100, 100))})
                           ->setGap(12)
                           ->setPadding(16),

                       Divider(),

                       // ── Radio ─────────────────────────────────────────────────
                       Flex({Text("Radio")
                                 ->setFontWeight(FontWeight::Bold)
                                 ->setMinWidth(120),
                             RadioGroupWithOptions({"Option A", "Option B", "Option C"})
                                 ->setHorizontal()
                                 ->bindValue(radioState)
                                 ->setOnSelectionChanged([this](const std::string &v)
                                                         { radioState.set(v); }),
                             Text(radioState, [](const std::string &v)
                                  { return v; })
                                 ->setTextColor(Color::fromRGB(100, 100, 100))})
                           ->setGap(12)
                           ->setPadding(16),

                       Divider(),

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

                       Divider(),

 





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
  return FluxApp()
      .setTheme(AppTheme::light())
      .build(std::make_shared<MyApp>());
}