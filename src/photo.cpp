#include "flux.hpp"
#include "flux_image_editor.hpp"
#include <windows.h>
#include <cmath>

class PhotoEditorComponent : public Component {
    State<ImageAdjustments> adj{ {}, context };
    State<bool> showOriginal{ false, context };

    WidgetPtr build() override {

        auto image = EditableImage("photo.jpg", 800, 600)
            ->setAdjustments(adj)
            ->setFitMode(EditableImageWidget::FitMode::Contain);

        // Bind showOriginal to the backslash key externally:
        //   showOriginal.set(true/false) → blit toggles automatically

        auto panel = Column(
            // Exposure
            Row(Text("Exposure")->setWidth(100),
                Slider(-5.0, 5.0, 0.01)
                    ->setOnValueChanged([&](double v){
                        auto a = adj.get(); a.exposure = v; adj.set(a);
                    })),
            // Contrast
            Row(Text("Contrast")->setWidth(100),
                Slider(-100.0, 100.0, 1.0)
                    ->setOnValueChanged([&](double v){
                        auto a = adj.get(); a.contrast = v; adj.set(a);
                    })),
            // Highlights
            Row(Text("Highlights")->setWidth(100),
                Slider(-100.0, 100.0, 1.0)
                    ->setOnValueChanged([&](double v){
                        auto a = adj.get(); a.highlights = v; adj.set(a);
                    })),
            // ... same pattern for all other fields ...

            Button(Text("Reset"), [&]{ adj.set({}); })
        )->setSpacing(12);

        return Scaffold(
            AppBar("Photo Editor"),
            Row(image, panel->setWidth(280))
        );
    }
};

WidgetPtr createApp(FluxUI *app) {
  return FluxApp("Push Graph",
                 BuildComponent<PhotoEditorComponent>(),
                 AppTheme::materialBlue());
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
  GdiplusInitializer gdiplusInit;
  FluxUI app(hInstance);
  app.build([&]() { return createApp(&app); });
  app.createWindow("FluxUI - Push Graph", 700, 500);
  return app.run();
}