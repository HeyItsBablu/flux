#include "flux/flux.hpp"

class MyApp : public Widget {
public:
  WidgetPtr build() override {

    return Scaffold(
        AppBar("Flux App"),
        Expanded(
            Center(AudioPlayer("screenshots/TumMile.mp3")
                       ->setWidth(280))),
        nullptr, nullptr);
  }
};

WidgetPtr createApp(FluxUI *app) {
  return FluxApp("FluxUI - App", std::make_shared<MyApp>(), AppTheme::light(),
                 false, 900, 700, false, false);
}