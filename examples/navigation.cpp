#include "flux/flux.hpp"
#include "flux/flux_navigator.hpp"

class PageTwo : public Widget {
public:
  WidgetPtr build() override {
    return Scaffold(
        AppBar("Page Two"),
        Expanded(Center(Button("Go Back", [] { Navigator::pop(); }))),
        nullptr, nullptr);
  }
};

class PageOne : public Widget {
public:
  WidgetPtr build() override {
    return Scaffold(
        AppBar("Page One"),
        Expanded(Center(Button("Go to Page Two", [] {
          Navigator::push(std::make_shared<PageTwo>());
        }))),
        nullptr, nullptr);
  }
};

WidgetPtr createApp(FluxUI* app) {
  return FluxApp("My App", Navigator::init(std::make_shared<PageOne>()));
}