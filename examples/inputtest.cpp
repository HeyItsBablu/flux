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
                       Text("Book a Stay")
                           ->setFontSize(20)
                           ->setFontWeight(FontWeight::Bold),

                       SizedBox(0, 20),

                       // ── Check-in ──────────────────────────────────────────
                       Text("Check-in")
                           ->setFontSize(12)
                           ->setTextColor(Color::fromRGB(100, 100, 100)),
                       SizedBox(0, 4),
                       DatePicker()
                           ->setDate(checkin)
                           ->setPlaceholder("Select check-in date")
                           ->setOnDateChanged([this](FluxDate d)
                                              { checkin.set(d); }),

                       SizedBox(0, 16),

                       // ── Check-out ─────────────────────────────────────────
                       Text("Check-out")
                           ->setFontSize(12)
                           ->setTextColor(Color::fromRGB(100, 100, 100)),
                       SizedBox(0, 4),
                       DatePicker()
                           ->setDate(checkout)
                           ->setPlaceholder("Select check-out date")
                           ->setOnDateChanged([this](FluxDate d)
                                              { checkout.set(d); }),

                       SizedBox(0, 20),

                       // ── Summary ───────────────────────────────────────────
                       Text(checkin, [](FluxDate d)
                            { return d.isValid()
                                         ? "Check-in:  " + d.toString("%d %b %Y")
                                         : "Check-in:  —"; })
                           ->setFontSize(13),

                       SizedBox(0, 4),

                       Text(checkout, [](FluxDate d)
                            { return d.isValid()
                                         ? "Check-out: " + d.toString("%d %b %Y")
                                         : "Check-out: —"; })
                           ->setFontSize(13),

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

                       Divider(),

                       // ── NumberInput ───────────────────────────────────────────
                       Flex({Text("NumberInput")
                                 ->setFontWeight(FontWeight::Bold)
                                 ->setMinWidth(120),
                             NumberInput(0.0, 999.0, 1.0)
                                 ->setValue(numberState)
                                 ->setOnValueChanged(
                                     [this](double v)
                                     { numberState.set(v); })
                                 ->setWidth(120),
                             Text(
                                 numberState,
                                 [](double v)
                                 { return "= " + std::to_string((int)v); })
                                 ->setTextColor(Color::fromRGB(100, 100, 100))})
                           ->setGap(12)
                           ->setPadding(16),

                       Divider(),

                       // ── SpinBox (decimal) ─────────────────────────────────────
                       Flex({Text("SpinBox")
                                 ->setFontWeight(FontWeight::Bold)
                                 ->setMinWidth(120),
                             SpinBox(0.0, 10.0, 0.1)
                                 ->setDecimalPlaces(1)
                                 ->setPrefix("x ")
                                 ->setSuffix(" kg")
                                 ->setValue(numberState)
                                 ->setWidth(140),
                             Text(numberState,
                                  [](double v)
                                  {
                                      std::ostringstream oss;
                                      oss << std::fixed << std::setprecision(1) << v
                                          << " kg";
                                      return oss.str();
                                  })
                                 ->setTextColor(Color::fromRGB(100, 100, 100))})
                           ->setGap(12)
                           ->setPadding(16),

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

WidgetPtr createApp(FluxUI *app)
{
    return FluxApp("FluxUI - Paint", std::make_shared<MyApp>(), AppTheme::light(),
                   false, // debugShowWidgetBounds
                   900,   // width
                   700,   // height
                   false, // maximize
                   true   // fullscreen
    );
}