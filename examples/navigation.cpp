#include "flux/flux.hpp"
#include "flux/flux_navigator.hpp"

class PageTwo : public Widget
{
public:
  WidgetPtr build() override
  {
    return Scaffold(
        AppBar("Page Two"),
        Expanded(Center(Button("Go Back", []
                               { Navigator::pop(); }))),
        nullptr, nullptr);
  }
};

class PageOne : public Widget
{
public:
  WidgetPtr build() override
  {
    return Scaffold(
        AppBar("Page One"),
        Expanded(Center(Button("Go to Page Two", []
                               { Navigator::navigate("/settings"); }))),
        nullptr, nullptr);
  }
};

WidgetPtr createApp(FluxUI *app)
{
  return FluxApp("Title",
                 Navigator::init({
                                     {"/", []
                                      { return std::make_shared<PageOne>(); }},
                                     {"/settings", []
                                      { return std::make_shared<PageTwo>(); }},
                                 },
                                 "/") // <-- initial route name
  );
}