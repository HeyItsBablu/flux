#include "flux/flux.hpp"

class MyApp : public Widget {

public:
  WidgetPtr build() override {
    return Scaffold(
        AppBar("Flux App"),
        Expanded(Center(CameraView()->setWidth(380)->setHeight(270)->setOnPhoto(
            [](const std::string &path) { std::cout << path << std::endl; }))),nullptr,nullptr);
  }
};

WidgetPtr createApp(FluxUI *app) {
  return FluxApp("FluxUI - App", std::make_shared<MyApp>(), AppTheme::light(),
                 false, 900, 700, false, false);
}