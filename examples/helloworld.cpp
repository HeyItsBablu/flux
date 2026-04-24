#include "flux/flux.hpp"

class MyApp : public Widget {

public:
  WidgetPtr build() override {
    return Scaffold(AppBar("Flux App"),
                    Center(AudioPlayer("C:/Users/user/Music/Tum Mile Tum Mile Original Motion Picturetrack 128 Kbps.mp3")->setWidth(380)));
  }
};

WidgetPtr createApp(FluxUI *app) {
  return FluxApp("FluxUI - App", std::make_shared<MyApp>(), AppTheme::light(),
                 false, 900, 700, false, false);
}