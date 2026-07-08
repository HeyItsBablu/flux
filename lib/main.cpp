#include "flux/flux.hpp"

class MyApp : public Widget
{
    State<bool> toggleState{false};
    State<double> sliderState{50.0};
    State<bool> checkState{false};
    State<std::string> radioState{"Option A"};
    State<std::string> textState{""};
    State<double> numberState{42.0};
    State<std::string> textAreaState{""};
    State<int> dropdownIndex{-1};
    State<std::string> lastAction{"(none)"};

public:
    WidgetPtr build() override
    {
        return Flex(
                   {
                       Tooltip(Button("Hover me for tooltip", nullptr),
                               "Fixed: close ordering collapses two repaints into one"),
                       // ── Toggle ────────────────────────────────────────────────
                       Flex({Text("DropDown")
                                 ->setFontWeight(FontWeight::Bold)
                                 ->setMinWidth(120),

                             Dropdown({"Apple", "Banana", "Cherry",
                                       "Date", "Elderberry", "Fig",
                                       "Grape", "Honeydew"})
                                 ->setSelectedIndex(dropdownIndex)
                                 ->setPlaceholder("Pick a fruit...")
                                 ->setMaxVisibleItems(5)
                                 ->setOnSelectionChanged([this](int idx, const std::string &val)
                                                         {
          dropdownIndex.set(idx);
          lastAction.set("Dropdown: " + val); })
                                 ->setWidth(260),
                             Text(toggleState, [](bool v)
                                  { return v ? "On" : "Off"; })
                                 ->setTextColor(Color::fromRGB(100, 100, 100))})
                           ->setGap(12)
                           ->setPadding(16),

                       Divider(),

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