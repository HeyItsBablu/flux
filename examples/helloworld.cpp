#include "flux/flux.hpp"

class MyApp : public Component {

public:
  WidgetPtr build() override {
    return Scaffold(AppBar("Flux App"),
                    Center(AudioPlayer("C:/Users/user/Music/Tum Mile Tum Mile Original Motion Picturetrack 128 Kbps.mp3")->setWidth(380)));
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