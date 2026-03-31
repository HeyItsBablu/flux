#include "flux/flux.hpp"

// ============================================================================
// CHILD — TextInput that writes to parent's State<string>
// ============================================================================

class ChildTextInput : public Component {
  State<std::string> *text;

public:
  explicit ChildTextInput(State<std::string> *text) : text(text) {}

  WidgetPtr build() override {
    return Column({
        Text("Child TextInput")->setFontSize(14),
        TextInput("Type something...")
            ->setInputValue(deref(text))
            ->setWidth(300),
        Text(deref(text), [](const std::string &v) { return "Value: " + v; })
  })->setSpacing(8);
  }
};

// ============================================================================
// CHILD — Slider that writes to parent's State<int>
// ============================================================================

class ChildSlider : public Component {
  State<int> *value;

public:
  explicit ChildSlider(State<int> *value) : value(value) {}

  WidgetPtr build() override {
    return Column({
        Text("Child Slider")->setFontSize(14),
        Slider(0, 100, 1)
            ->setValue(deref(value))
            ->setTrackFillColor(Color::fromRGB(99, 102, 241))
            ->setWidth(300),
        Text(deref(value), [](int v) { return "Value: " + std::to_string(v); })
  })->setSpacing(8);
  }
};

// ============================================================================
// CHILD — Toggle that writes to parent's State<bool>
// ============================================================================

class ChildToggle : public Component {
  State<bool> *enabled;

public:
  explicit ChildToggle(State<bool> *enabled) : enabled(enabled) {}

  WidgetPtr build() override {
    return Column({
        Text("Child Toggle")->setFontSize(14),
        Toggle("Enable feature")
            ->setValue(deref(enabled))
            ->setTrackOnColor(Color::fromRGB(76, 175, 80)),
        Text(deref(enabled), [](bool v) { return v ? "ON" : "OFF"; })
  })->setSpacing(8);
  }
};

// ============================================================================
// CHILD — CheckBox that writes to parent's State<bool>
// ============================================================================

class ChildCheckBox : public Component {
  State<bool> *checked;

public:
  explicit ChildCheckBox(State<bool> *checked) : checked(checked) {}

  WidgetPtr build() override {
    return Column({
        Text("Child CheckBox")->setFontSize(14),
        CheckBox("I agree to terms")
            ->setInputValue(deref(checked)),
        Text(deref(checked), [](bool v) { return v ? "Agreed" : "Not agreed"; })
  })->setSpacing(8);
  }
};

// ============================================================================
// CHILD — Dropdown that writes to parent's State<string>
// ============================================================================

class ChildDropdown : public Component {
  State<std::string> *selected;

public:
  explicit ChildDropdown(State<std::string> *selected) : selected(selected) {}

  WidgetPtr build() override {
    return Column({
        Text("Child Dropdown")->setFontSize(14),
        Dropdown({"Nepal", "India", "USA", "UK"})
            ->setPlaceholder("Select country...")
            ->setSelectedValue(deref(selected))
            ->setWidth(300),
        Text(deref(selected), [](const std::string &v) {
            return "Selected: " + (v.empty() ? "none" : v);
        })
  })->setSpacing(8);
  }
};

// ============================================================================
// PARENT — owns all state, displays it, passes pointers to children
// ============================================================================

class ParentForm : public Component {
  State<std::string> text;
  State<int>         sliderValue;
  State<bool>        enabled;
  State<bool>        checked;
  State<std::string> country;

public:
  ParentForm()
      : text("", context),
        sliderValue(50, context),
        enabled(false, context),
        checked(false, context),
        country("", context) {}

  WidgetPtr build() override {
    return Scaffold(
        AppBar("Input Sharing Test"),

        Center(
            Card(
                Column({

                    // Parent reads all state
                    Text("--- Parent View ---")->setFontWeight(FontWeight::Bold),
                    Text(text,        [](const std::string &v) { return "Text: "    + v; }),
                    Text(sliderValue, [](int v)                { return "Slider: "  + std::to_string(v); }),
                    Text(enabled,     [](bool v)               { return "Toggle: "  + std::string(v ? "ON" : "OFF"); }),
                    Text(checked,     [](bool v)               { return "Checked: " + std::string(v ? "Yes" : "No"); }),
                    Text(country,     [](const std::string &v) { return "Country: " + (v.empty() ? "none" : v); }),

                    Divider(),

                    // Children write to parent state via pointer
                    CHILD(ChildTextInput, &text),
                    CHILD(ChildSlider,    &sliderValue),
                    CHILD(ChildToggle,    &enabled),
                    CHILD(ChildCheckBox,  &checked),
                    CHILD(ChildDropdown,  &country)

  })->setSpacing(16)
            )
        )
    );
  }
};



WidgetPtr createApp(FluxUI* app) {
    return FluxApp(
        "FluxUI - Paint",
        BuildComponent<ParentForm>(),
        AppTheme::light(),
        false,   // debugShowWidgetBounds
        900,     // width
        700,     // height
        false,   // maximize
        true     // fullscreen
    );
}

