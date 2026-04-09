#include "flux/flux.hpp"

class MyApp : public Component {

public:
  WidgetPtr build() override {
    return Scaffold(AppBar("Flux App"),
                    Center(VideoPlayer("/home/roshan/Downloads/Maamla.Legal.Hai.S02E08.720p.Hindi.WEB-DL.5.1.ESub.x264-.mkv")
            ->setWidth(380)
            ->setHeight(270)    // 16:9
            ->setAutoPlay(false)));
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