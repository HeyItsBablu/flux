#include "flux/flux.hpp"

class MyApp : public Component {
    State<int> count;
    ToastWidgetPtr toast;
public:
    MyApp() : count(0, context) {}

    WidgetPtr build() override {
        toast = Toast()
            ->setPosition(ToastPosition::BottomRight)
            ->setMaxVisible(3);

        return Scaffold(
            AppBar("My App"),
            Column({
                Button("Save",    [this]{ toast->show("Saved!",         ToastType::Success); }),
                Button("Warning", [this]{ toast->show("Low disk space", ToastType::Warning); }),
                Button("Error",   [this]{
                    toast->showEntry({
                        .message     = "Upload failed — check your connection",
                        .title       = "Network Error",
                        .type        = ToastType::Error,
                        .durationMs  = 0,          // sticky
                        .actionLabel = "Retry",
                        .onAction    = [this]{ /* retry logic */ },
                    });
                }),
                toast,             // ← zero-size, place anywhere in tree
            })->setSpacing(8)
        );
    }
};


int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
  FluxUI app(hInstance);
  app.build([&]() {
    return FluxApp("Toast Demo",
                   BuildComponent<MyApp>(),
                   AppTheme::light());
  });
  app.createWindow("FluxUI - TabView Demo", 800, 520);
  return app.run();
}