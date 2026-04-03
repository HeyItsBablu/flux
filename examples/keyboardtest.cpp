#include "flux/flux.hpp"

class MyApp : public Component {
    State<std::string> searchText;
    State<std::string> emailText;
public:
    MyApp() : searchText("", context), emailText("", context) {}

    WidgetPtr build() override {
        return Scaffold(
            AppBar("Keyboard Demo"),
            Column({
                TextInput("Search...")->setInputValue(searchText),
                TextInput("your@email.com")->setInputValue(emailText),
            })->setPadding(20)->setSpacing(12)
        );
    }
};
WidgetPtr createApp(FluxUI *app) {
  return FluxApp("FluxUI - Paint", BuildComponent<MyApp>(), AppTheme::light(),
                 false, // debugShowWidgetBounds
                 900,   // width
                 700,   // height
                 false, // maximize
                 false  // fullscreen
  );
}