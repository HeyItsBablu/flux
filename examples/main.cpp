#include "flux/flux.hpp"
#include <numeric>

class MyApp : public Component {
  State<int> boxCount;
  State<std::vector<int>> boxIndices;

public:
  MyApp() : boxCount(9, context), boxIndices({}, context) {}

  WidgetPtr build() override {
    return Scaffold(AppBar("Image Examples"), Text("Hello"));
  }
};

WidgetPtr createApp(FluxUI *app) {
  return FluxApp("FluxUI - Paint", BuildComponent<MyApp>(), AppTheme::light(),
                 false, // debugShowWidgetBounds
                 900,   // width
                 700,   // height
                 false, // maximize
                 true   // fullscreen
  );
}