#include "flux.hpp"
#include "flux_graph.hpp"
#include "flux_image_editor.hpp"
#include <cmath>
#include <windows.h>

class PushGraphComponent : public Component {
private:
  State<ImageAdjustments> adj{{}, context};
  State<bool> showOriginal{false, context};

public:
  WidgetPtr build() override {

    auto image =
        EditableImage("C:/Upwork/c_projects/flux/src/images/main.png", 800, 600)
            ->setAdjustments(adj)
            ->setFitMode(EditableImageWidget::FitMode::Contain);

    auto controls =
        Container(
            Column(
                // Exposure
                Row(Text("Exposure")
                        ->setWidth(100)
                        ->setTextColor(RGB(255, 255, 255)),
                    Slider(-5.0, 5.0, 0.01)

                        ->setOnValueChanged([&](double v) {
                          auto a = adj.get();
                          a.exposure = (float)v;
                          adj.set(a);
                        })),
                // Contrast
                Row(Text("Contrast")
                        ->setWidth(100)
                        ->setTextColor(RGB(255, 255, 255)),
                    Slider(-100.0, 100.0, 1.0)

                        ->setTrackFillColor(RGB(130, 100, 255))

                        ->setOnValueChanged([&](double v) {
                          auto a = adj.get();
                          a.contrast = (float)v;
                          adj.set(a);
                        })),
                // Highlights
                Row(Text("Highlights")
                        ->setWidth(100)
                        ->setTextColor(RGB(255, 255, 255)),
                    Slider(-100.0, 100.0, 1.0)

                        ->setTrackFillColor(RGB(130, 100, 255))

                        ->setOnValueChanged([&](double v) {
                          auto a = adj.get();
                          a.highlights = (float)v;
                          adj.set(a);
                        })),
                // Shadows
                Row(Text("Shadows")->setWidth(100)->setTextColor(
                        RGB(255, 255, 255)),
                    Slider(-100.0, 100.0, 1.0)

                        ->setTrackFillColor(RGB(130, 100, 255))

                        ->setOnValueChanged([&](double v) {
                          auto a = adj.get();
                          a.shadows = (float)v;
                          adj.set(a);
                        })),
                // Whites
                Row(Text("Whites")->setWidth(100)->setTextColor(
                        RGB(255, 255, 255)),
                    Slider(-100.0, 100.0, 1.0)

                        ->setTrackFillColor(RGB(130, 100, 255))

                        ->setOnValueChanged([&](double v) {
                          auto a = adj.get();
                          a.whites = (float)v;
                          adj.set(a);
                        })),
                // Blacks
                Row(Text("Blacks")->setWidth(100)->setTextColor(
                        RGB(255, 255, 255)),
                    Slider(-100.0, 100.0, 1.0)

                        ->setTrackFillColor(RGB(130, 100, 255))

                        ->setOnValueChanged([&](double v) {
                          auto a = adj.get();
                          a.blacks = (float)v;
                          adj.set(a);
                        })),
                // Clarity
                Row(Text("Clarity")->setWidth(100)->setTextColor(
                        RGB(255, 255, 255)),
                    Slider(-100.0, 100.0, 1.0)

                        ->setTrackFillColor(RGB(130, 100, 255))

                        ->setOnValueChanged([&](double v) {
                          auto a = adj.get();
                          a.clarity = (float)v;
                          adj.set(a);
                        })),
                // Saturation
                Row(Text("Saturation")
                        ->setWidth(100)
                        ->setTextColor(RGB(255, 255, 255)),
                    Slider(-100.0, 100.0, 1.0)

                        ->setTrackFillColor(RGB(130, 100, 255))

                        ->setOnValueChanged([&](double v) {
                          auto a = adj.get();
                          a.saturation = (float)v;
                          adj.set(a);
                        })),
                // Vibrance
                Row(Text("Vibrance")
                        ->setWidth(100)
                        ->setTextColor(RGB(255, 255, 255)),
                    Slider(-100.0, 100.0, 1.0)

                        ->setTrackFillColor(RGB(130, 100, 255))

                        ->setOnValueChanged([&](double v) {
                          auto a = adj.get();
                          a.vibrance = (float)v;
                          adj.set(a);
                        })),
                // Temperature
                Row(Text("Temperature")
                        ->setWidth(100)
                        ->setTextColor(RGB(255, 255, 255)),
                    Slider(-100.0, 100.0, 1.0)

                        ->setTrackFillColor(RGB(130, 100, 255))

                        ->setOnValueChanged([&](double v) {
                          auto a = adj.get();
                          a.temperature = (float)v;
                          adj.set(a);
                        })),
                // Tint
                Row(Text("Tint")->setWidth(100)->setTextColor(
                        RGB(255, 255, 255)),
                    Slider(-100.0, 100.0, 1.0)

                        ->setTrackFillColor(RGB(130, 100, 255))

                        ->setOnValueChanged([&](double v) {
                          auto a = adj.get();
                          a.tint = (float)v;
                          adj.set(a);
                        })),
                // Gamma
                Row(Text("Gamma")->setWidth(100)->setTextColor(
                        RGB(255, 255, 255)),
                    Slider(0.1, 5.0, 0.01)

                        ->setOnValueChanged([&](double v) {
                          auto a = adj.get();
                          a.gamma = (float)v;
                          adj.set(a);
                        })),

                Button(Text("Reset"), [&] { adj.set(ImageAdjustments{}); }))
                ->setSpacing(10))
            ->setWidth(200);

    return Scaffold(
        AppBar("Photo Editor"),
        Container(Center(Row(image, controls)
                             ->setSpacing(20)
                             ->setCrossAlignment(Alignment::Center)))
            ->setBackgroundColor(RGB(20, 24, 36))
            ->setPadding(30));
  }
};

WidgetPtr createApp(FluxUI *app) {
  return FluxApp("Photo Editor", BuildComponent<PushGraphComponent>(),
                 AppTheme::materialBlue());
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
  GdiplusInitializer gdiplusInit;
  FluxUI app(hInstance);
  app.build([&]() { return createApp(&app); });
  app.createWindow("FluxUI - Photo Editor", 1000, 1000);
  return app.run();
}