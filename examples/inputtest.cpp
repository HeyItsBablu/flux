#include "flux/flux.hpp"

class InputShowcase : public Component {
    State<bool>        toggleState;
    State<double>      sliderState;
    State<bool>        checkState;
    State<std::string> radioState;
    State<std::string> textState;

public:
    InputShowcase()
        : toggleState(false, context),
          sliderState(50.0, context),
          checkState(false, context),
          radioState("b", context),
          textState("", context) {}

    WidgetPtr build() override {
        return Scaffold(
            AppBar("Input Showcase"),
            ListView({

                // ── Toggle ────────────────────────────────────────────────
                Row({
                    Text("Toggle")->setFontWeight(FontWeight::Bold)->setMinWidth(120),
                    Toggle("Enable feature")
                        ->setValue(toggleState)
                        ->setOnToggleChanged([this](bool v) {
                            toggleState.set(v);
                        }),
                    Text(toggleState, [](bool v) {
                        return v ? "On" : "Off";
                    })->setTextColor(RGB(100, 100, 100))
                })->setSpacing(12)->setPadding(16),

                Divider(),

                // ── Slider ────────────────────────────────────────────────
                Row({
                    Text("Slider")->setFontWeight(FontWeight::Bold)->setMinWidth(120),
                    Expanded(
                        Slider(0.0, 100.0, 1.0)
                            ->setValue(sliderState)
                            ->setOnValueChanged([this](double v) {
                                sliderState.set(v);
                            })
                    ),
                    Text(sliderState, [](double v) {
                        return std::to_string((int)v);
                    })->setMinWidth(32)->setTextColor(RGB(100, 100, 100))
                })->setSpacing(12)->setPadding(16),

                Divider(),

                // ── CheckBox ──────────────────────────────────────────────
                Row({
                    Text("CheckBox")->setFontWeight(FontWeight::Bold)->setMinWidth(120),
                    CheckBox("Accept terms")
                        ->setInputValue(checkState),
                    Text(checkState, [](bool v) {
                        return v ? "Checked" : "Unchecked";
                    })->setTextColor(RGB(100, 100, 100))
                })->setSpacing(12)->setPadding(16),

                Divider(),

                // ── Radio ─────────────────────────────────────────────────
                Row({
                    Text("Radio")->setFontWeight(FontWeight::Bold)->setMinWidth(120),
                    RadioGroupWithOptions({"Option A", "Option B", "Option C"})
                        ->setHorizontal()
                        ->bindValue(radioState)
                        ->setOnSelectionChanged([this](const std::string &v) {
                            radioState.set(v);
                        }),
                    Text(radioState, [](const std::string &v) {
                        return v;
                    })->setTextColor(RGB(100, 100, 100))
                })->setSpacing(12)->setPadding(16),

                Divider(),

                // ── TextInput ─────────────────────────────────────────────
                Row({
                    Text("TextInput")->setFontWeight(FontWeight::Bold)->setMinWidth(120),
                    Expanded(
                        TextInput("Type something...")
                            ->setInputValue(textState)
                    ),
                    Text(textState, [](const std::string &v) {
                        return std::to_string((int)v.size()) + " chars";
                    })->setMinWidth(60)->setTextColor(RGB(100, 100, 100))
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
    app.createWindow("FluxUI - Input Showcase", 700, 500);
    return app.run();
}