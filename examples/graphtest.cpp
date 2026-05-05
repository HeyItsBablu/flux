#include "flux/flux.hpp"

class MyApp : public Widget {
public:
  WidgetPtr build() override {
    return Scaffold(
        AppBar("Sales Chart"),
        Center(

            Graph(500, 300)
                ->setTitle("Monthly Sales")
                ->addSeries("Revenue",
                            {12.f, 19.f, 8.f, 24.f, 17.f, 31.f, 28.f},
                            Color::fromRGB(51, 153, 255))
                ->setXLabels({"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul"})
                ->setXAxisTitle("Month")
                ->setYAxisTitle("$k")
                ->setType(GraphType::Line)
                ->setShowLegend(false)),
        nullptr, nullptr);
  }
};

WidgetPtr createApp(FluxUI *app) {
  return FluxApp("FluxUI - Graph", std::make_shared<MyApp>(), AppTheme::light(),
                 false, 700, 500, false, false);
}