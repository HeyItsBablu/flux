#include "flux/flux.hpp"

class MyApp : public Component {
    State<bool> isDark;
public:
    MyApp() : isDark(false, context) {}

    WidgetPtr build() override {
        return Scaffold(
            AppBar("Theme Toggle"),
            Column({
                Toggle("Dark mode")
                    ->setValue(isDark)
                    ->setTrackOnColor(RGB(99, 102, 241))
                    ->setOnToggleChanged([this](bool v){
                        FluxAppWidget::getInstance()->setTheme(
                            v ? AppTheme::dark() : AppTheme::light()
                        );
                    }),
            })->setSpacing(8)->setPadding(16)
        );
    }
};
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    FluxUI app(hInstance);
    app.build([&]() {
        return FluxApp("Theme Toggle Demo",
                       BuildComponent<MyApp>(),
                       AppTheme::light());
    });
    app.createWindow("FluxUI - Theme Toggle", 800, 520);
    return app.run();
}