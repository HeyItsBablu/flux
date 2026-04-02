#include "flux/flux.hpp"


class MyApp : public Component {

public:
  WidgetPtr build() override {
    return Scaffold(AppBar("Flux App"), Center(Text("Hello World")));
  }
};

WidgetPtr createApp(FluxUI *app) {
  return FluxApp("FluxUI - App", BuildComponent<MyApp>(), AppTheme::light(),
                 false, // debugShowWidgetBounds
                 900,   // width
                 700,   // height
                 false, // maximize
                 false   // fullscreen
  );
}