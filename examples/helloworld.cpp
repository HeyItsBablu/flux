#include "flux/flux.hpp"

class MyApp : public Widget {

  State<int> counter{0};

public:
  WidgetPtr build() override {
    return Scaffold(
        AppBar("Flux App"),
        Expanded(Center(
            Container(
                Column({Text(counter), Button("Click", [this] { counter++; })})
                    ->setCrossAxisAlignment(CrossAxisAlignment::Center))
                ->setWidth(300)
                ->setHeight(200)

                ->setBorderRadius(10))),

        FAB("+",
            [this] {
              std::cout << "FAB pressed!" << std::endl;
              counter++;
            })
            ->setPosition(FABPosition::BottomRight)
            ->setColor(Color::fromRGB(33, 150, 243))
            ->setLabel("Add Item"),
        nullptr);
  }
};

WidgetPtr createApp(FluxUI *app) {
  return FluxApp("FluxUI - App", std::make_shared<MyApp>(), AppTheme::light(),
                 false, 900, 700, false, false);
}