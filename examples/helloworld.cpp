#include "flux/flux.hpp"

class MyApp : public Widget {

public:
  WidgetPtr build() override {
    return Scaffold(
        AppBar("Flux App"),
        Center(Container(Center(Container(Center(Text("Hello world")))
                                    ->setBorderRadius(10)
                                    ->setWidth(100)
                                    ->setHeight(100)
                                    ->setBackgroundColor(
                                        Color::fromRGB(20, 20, 100))))
                   ->setWidth(300)
                   ->setHeight(200)
                   ->setBackgroundColor(Color::fromRGB(10, 10, 200))->setBorderRadius(10)));
  }
};

WidgetPtr createApp(FluxUI *app) {
  return FluxApp("FluxUI - App", std::make_shared<MyApp>(), AppTheme::light(),
                 false, 900, 700, false, false);
}