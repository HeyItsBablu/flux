#include "flux/flux.hpp"

class MyApp : public Widget {

public:
  WidgetPtr build() override {
    return Scaffold(
        AppBar("List view Demo"),
        ListView(
            {Row({
                 Text("Item 1"),
                 Text("Item 2"),
             }),
             Container(Text("Hello"))
                 ->setWidth(80)
                 ->setHeight(80)
                 ->setBorderRadius(8)
                 ->setBackgroundColor(Color::fromRGB(100, 100, 100)),
             Row({

                     Column(
                         {
                             Text("Image Title")
                                 ->setFontSize(16)
                                 ->setFontWeight(FontWeight::Bold)
                                 ->setTextColor(Color::fromRGB(30, 30, 30)),
                             Text("Some subtitle text here")
                                 ->setFontSize(13)
                                 ->setTextColor(Color::fromRGB(100, 100, 100)),
                         })
                         ->setSpacing(4)
                         ->setMainAxisSize(MainAxisSize::Min),
                 })
                 ->setSpacing(12)
                 ->setCrossAxisAlignment(CrossAxisAlignment::Center),
             Text("Item 3"),
             Text("Item 1"),
             Text("Item 2"),
             Text("Item 3"),
             Text("Item 1"),
             Text("Item 2"),
             Text("Item 3"),
             Text("Item 1"),
             Text("Item 2"),
             Text("Item 3"),
             Text("Item 1"),
             Text("Item 2"),
             Text("Item 3"),
             Button("Click",
                    []() { std::cout << "Button clicked" << std::endl; }),
             Text("Item 1"),
             Text("Item 2"),
             Text("Item 3"),
             Text("Item 1"),
             Text("Item 2"),
             Text("Item 3"),
             Text("Item 1"),
             Text("Item 2"),
             Text("Item 3"),
             Text("Item 1"),
             Text("Item 2"),
             Text("Item 3"),
             Text("Item 1"),
             Text("Item 2"),
             Text("Item 3"),
             Button("Click",
                    []() { std::cout << "Button clicked" << std::endl; })})
            ->setSpacing(8)
            ->setPadding(12),
        nullptr, nullptr

    );
  }
};

WidgetPtr createApp(FluxUI *app) {
  return FluxApp("FluxUI - List", std::make_shared<MyApp>(), AppTheme::light(),
                 false, // debugShowWidgetBounds
                 900,   // width
                 700,   // height
                 false, // maximize
                 false  // fullscreen
  );
}