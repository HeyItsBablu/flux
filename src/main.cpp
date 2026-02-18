#include "flux.hpp"
#include "flux_graph.hpp"
#include <windows.h>
#include <cmath>

class PushGraphComponent : public Component {
private:
  // State owns the data — the graph subscribes to it automatically
  State<std::vector<float>> data{ {10, 25, 18, 40, 33}, context };
  float phase = 0.0f;

public:
  WidgetPtr build() override {

    auto pushBtn =
        Button(
            Text("Push Value")->setTextColor(RGB(255, 255, 255))->setFontSize(14),
            [&] {
              phase += 0.4f;
              auto v = data.get();
              v.push_back(std::sin(phase) * 40.0f + 50.0f);
              if ((int)v.size() > 40)
                v.erase(v.begin());
              data.set(v); // <-- chart repaints itself, nothing else needed
            })
            ->setBackgroundColor(RGB(50, 130, 220))
            ->setHoverBackgroundColor(RGB(30, 110, 200))
            ->setBorderRadius(8)
            ->setPaddingAll(20, 10, 20, 10);

    auto clearBtn =
        Button(
            Text("Clear")->setTextColor(RGB(255, 255, 255))->setFontSize(14),
            [&] {
              phase = 0.0f;
              data.set({}); // <-- same, just set the state
            })
            ->setBackgroundColor(RGB(180, 60, 60))
            ->setHoverBackgroundColor(RGB(160, 40, 40))
            ->setBorderRadius(8)
            ->setPaddingAll(20, 10, 20, 10);

    return Scaffold(
        AppBar("Push Graph"),
        Container(
            Center(
                Column(
                    // addSeries() now accepts State<vector<float>> directly
                    Graph(600, 300)
                        ->addSeries("Data", data, 0.2f, 0.7f, 1.0f)
                        ->setShowGrid(true),
                    Row(pushBtn, clearBtn)
                        ->setSpacing(12)
                        ->setCrossAlignment(Alignment::Center))
                    ->setSpacing(20)
                    ->setCrossAlignment(Alignment::Center)))
            ->setBackgroundColor(RGB(20, 24, 36))
            ->setPadding(30));
  }
};

WidgetPtr createApp(FluxUI *app) {
  return FluxApp("Push Graph",
                 BuildComponent<PushGraphComponent>(),
                 AppTheme::materialBlue());
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
  GdiplusInitializer gdiplusInit;
  FluxUI app(hInstance);
  app.build([&]() { return createApp(&app); });
  app.createWindow("FluxUI - Push Graph", 700, 500);
  return app.run();
}