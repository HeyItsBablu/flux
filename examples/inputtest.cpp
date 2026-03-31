#include "flux/flux.hpp"

class InputShowcase : public Component {
    State<bool>        toggleState;
    State<double>      sliderState;
    State<bool>        checkState;
    State<std::string> radioState;
    State<std::string> textState;
    State<std::string> textAreaState;
    State<double>      numberState;

public:
    InputShowcase()
        : toggleState  (false,    context),
          sliderState  (50.0,     context),
          checkState   (false,    context),
          radioState   ("Option A", context),
          textState    ("",       context),
          textAreaState("",       context),
          numberState  (42.0,     context) {}

    WidgetPtr build() override {
        return Scaffold(
            AppBar("Input Showcase"),
            ListView({

                // ── Toggle ────────────────────────────────────────────────
                Row({
                    Text("Toggle")
                        ->setFontWeight(FontWeight::Bold)
                        ->setMinWidth(120),
                    Toggle("Enable feature")
                        ->setValue(toggleState)
                        ->setOnToggleChanged([this](bool v) {
                            toggleState.set(v);
                        }),
                    Text(toggleState, [](bool v) {
                        return v ? "On" : "Off";
                    })->setTextColor(Color::fromRGB(100, 100, 100))
                })->setSpacing(12)->setPadding(16),

                Divider(),

                // ── Slider ────────────────────────────────────────────────
                Row({
                    Text("Slider")
                        ->setFontWeight(FontWeight::Bold)
                        ->setMinWidth(120),
                    Expanded(
                        Slider(0.0, 100.0, 1.0)
                            ->setValue(sliderState)
                            ->setOnValueChanged([this](double v) {
                                sliderState.set(v);
                            })
                    ),
                    Text(sliderState, [](double v) {
                        return std::to_string((int)v);
                    })->setMinWidth(32)->setTextColor(Color::fromRGB(100, 100, 100))
                })->setSpacing(12)->setPadding(16),

                Divider(),

                // ── CheckBox ──────────────────────────────────────────────
                Row({
                    Text("CheckBox")
                        ->setFontWeight(FontWeight::Bold)
                        ->setMinWidth(120),
                    CheckBox("Accept terms")
                        ->setInputValue(checkState),
                    Text(checkState, [](bool v) {
                        return v ? "Checked" : "Unchecked";
                    })->setTextColor(Color::fromRGB(100, 100, 100))
                })->setSpacing(12)->setPadding(16),

                Divider(),

                // ── Radio ─────────────────────────────────────────────────
                Row({
                    Text("Radio")
                        ->setFontWeight(FontWeight::Bold)
                        ->setMinWidth(120),
                    RadioGroupWithOptions({"Option A", "Option B", "Option C"})
                        ->setHorizontal()
                        ->bindValue(radioState)
                        ->setOnSelectionChanged([this](const std::string &v) {
                            radioState.set(v);
                        }),
                    Text(radioState, [](const std::string &v) {
                        return v;
                    })->setTextColor(Color::fromRGB(100, 100, 100))
                })->setSpacing(12)->setPadding(16),

                Divider(),

                // ── TextInput ─────────────────────────────────────────────
                Row({
                    Text("TextInput")
                        ->setFontWeight(FontWeight::Bold)
                        ->setMinWidth(120),
                    Expanded(
                        TextInput("Type something...")
                            ->setInputValue(textState)
                    ),
                    Text(textState, [](const std::string &v) {
                        return std::to_string((int)v.size()) + " chars";
                    })->setMinWidth(60)->setTextColor(Color::fromRGB(100, 100, 100))
                })->setSpacing(12)->setPadding(16),

                Divider(),

                // ── TextArea ──────────────────────────────────────────────
                Row({
                    Text("TextArea")
                        ->setFontWeight(FontWeight::Bold)
                        ->setMinWidth(120),
                    Expanded(
                        TextArea("Write multiple lines...")
                            ->setInputValue(textAreaState)
                            ->setHeight(120)
                            ->setLineNumbers(true)
                    ),
                    Text(textAreaState, [](const std::string &v) {
                        int lines = 1;
                        for (char c : v)
                            if (c == '\n') lines++;
                        return std::to_string(lines) + " lines";
                    })->setMinWidth(60)->setTextColor(Color::fromRGB(100, 100, 100))
                })->setSpacing(12)->setPadding(16),

                Divider(),

                // ── NumberInput ───────────────────────────────────────────
                Row({
                    Text("NumberInput")
                        ->setFontWeight(FontWeight::Bold)
                        ->setMinWidth(120),
                    NumberInput(0.0, 999.0, 1.0)
                        ->setValue(numberState)
                        ->setOnValueChanged([this](double v) {
                            numberState.set(v);
                        })
                        ->setWidth(120),
                    Text(numberState, [](double v) {
                        return "= " + std::to_string((int)v);
                    })->setTextColor(Color::fromRGB(100, 100, 100))
                })->setSpacing(12)->setPadding(16),

                Divider(),

                // ── SpinBox (decimal) ─────────────────────────────────────
                Row({
                    Text("SpinBox")
                        ->setFontWeight(FontWeight::Bold)
                        ->setMinWidth(120),
                    SpinBox(0.0, 10.0, 0.1)
                        ->setDecimalPlaces(1)
                        ->setPrefix("x ")
                        ->setSuffix(" kg")
                        ->setValue(numberState)
                        ->setWidth(140),
                    Text(numberState, [](double v) {
                        std::ostringstream oss;
                        oss << std::fixed << std::setprecision(1) << v << " kg";
                        return oss.str();
                    })->setTextColor(Color::fromRGB(100, 100, 100))
                })->setSpacing(12)->setPadding(16),

            })->setSpacing(0)
        );
    }
};

WidgetPtr createApp(FluxUI *app) {
    return FluxApp("Input Showcase", BuildComponent<InputShowcase>(),
                   AppTheme::light());
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    FluxUI app(hInstance);
    app.build([&]() { return createApp(&app); });
    app.createWindow("FluxUI - Input Showcase", 700, 620);
    return app.run();
}