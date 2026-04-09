#include "flux/flux.hpp"

class MyApp : public Component {

public:
  WidgetPtr build() override {
    return Scaffold(
        AppBar("Flux App"),
        Center(MicRecorder()->setWidth(320)->setHeight(120)->setOnSaved(
            [](const std::string &path) {
             std::cout <<"Saved to %s" << path.c_str()<< std::endl;
            })));
  }
};

WidgetPtr createApp(FluxUI *app) {
  return FluxApp("FluxUI - App", BuildComponent<MyApp>(), AppTheme::light(),
                 false, // debugShowWidgetBounds
                 900,   // width
                 700,   // height
                 false, // maximize
                 false  // fullscreen
  );
}