#include "flux/flux.hpp"

class MyApp : public Widget
{
public:
    WidgetPtr build() override
    {

        return Scaffold(
            AppBar("Flux App"),
            Expanded(
                Center(MicRecorder()->setWidth(320)->setHeight(120)->setOnSaved([](const std::string &path)
                                                                                { std::cout << "Saved to " << path << std::endl; }))),
            nullptr, nullptr);
    }
};

WidgetPtr createApp(FluxUI *app)
{
  return FluxApp()
      .setTheme(AppTheme::light())
      .build(std::make_shared<MyApp>());
}