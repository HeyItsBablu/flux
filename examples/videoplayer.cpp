
#include "flux/flux.hpp"

class MyApp : public Widget {
public:
  WidgetPtr build() override {

    return Scaffold(
        AppBar("Flux App"),
        Expanded(
            Center(VideoPlayer("screenshots/sample.mp4")
            ->setWidth(380)
            ->setHeight(270)    // 16:9
            ->setAutoPlay(false))),
        nullptr, nullptr);
  }
};

WidgetPtr createApp(FluxUI *app) {
  return FluxApp("FluxUI - App", std::make_shared<MyApp>(), AppTheme::light(),
                 false, 900, 700, false, false);
}