#include "flux/flux.hpp"

class MyApp : public Widget {
public:
  WidgetPtr build() override {
    return Scaffold(
        AppBar("ListView with Rows"),
        ListView(
            {

                // Simple row item
                Row({
                        Container(Text("A")->setTextColor(
                                      Color::fromRGB(255, 255, 255)))
                            ->setBackgroundColor(Color::fromRGB(33, 150, 243))
                            ->setPadding(12),
                        Text("Item A description")
                            ->setFontSize(14)
                            ->setTextColor(Color::fromRGB(50, 50, 50)),
                    })
                    ->setSpacing(12),

                Row({
                        Container(Text("B")->setTextColor(
                                      Color::fromRGB(255, 255, 255)))
                            ->setBackgroundColor(Color::fromRGB(220, 80, 80))
                            ->setPadding(12),
                        Text("Item B description")
                            ->setFontSize(14)
                            ->setTextColor(Color::fromRGB(50, 50, 50)),
                    })
                    ->setSpacing(12),

                Row({
                        Container(Text("C")->setTextColor(
                                      Color::fromRGB(255, 255, 255)))
                            ->setBackgroundColor(Color::fromRGB(80, 180, 80))
                            ->setPadding(12),
                        Text("Item C description")
                            ->setFontSize(14)
                            ->setTextColor(Color::fromRGB(50, 50, 50)),
                    })
                    ->setSpacing(12),
                Row({
                        Container(Text("C")->setTextColor(
                                      Color::fromRGB(255, 255, 255)))
                            ->setBackgroundColor(Color::fromRGB(80, 180, 80))
                            ->setPadding(12),
                        Text("Item C description")
                            ->setFontSize(14)
                            ->setTextColor(Color::fromRGB(50, 50, 50)),
                    })
                    ->setSpacing(12),
                Row({
                        Container(Text("C")->setTextColor(
                                      Color::fromRGB(255, 255, 255)))
                            ->setBackgroundColor(Color::fromRGB(80, 180, 80))
                            ->setPadding(12),
                        Text("Item C description")
                            ->setFontSize(14)
                            ->setTextColor(Color::fromRGB(50, 50, 50)),
                    })
                    ->setSpacing(12),
                Row({
                        Container(Text("C")->setTextColor(
                                      Color::fromRGB(255, 255, 255)))
                            ->setBackgroundColor(Color::fromRGB(80, 180, 80))
                            ->setPadding(12),
                        Text("Item C description")
                            ->setFontSize(14)
                            ->setTextColor(Color::fromRGB(50, 50, 50)),
                    })
                    ->setSpacing(12),
                Row({
                        Container(Text("C")->setTextColor(
                                      Color::fromRGB(255, 255, 255)))
                            ->setBackgroundColor(Color::fromRGB(80, 180, 80))
                            ->setPadding(12),
                        Text("Item C description")
                            ->setFontSize(14)
                            ->setTextColor(Color::fromRGB(50, 50, 50)),
                    })
                    ->setSpacing(12),
                Row({
                        Container(Text("C")->setTextColor(
                                      Color::fromRGB(255, 255, 255)))
                            ->setBackgroundColor(Color::fromRGB(80, 180, 80))
                            ->setPadding(12),
                        Text("Item C description")
                            ->setFontSize(14)
                            ->setTextColor(Color::fromRGB(50, 50, 50)),
                    })
                    ->setSpacing(12),

            })
            ->setSpacing(8)
            ->setPadding(16),
        nullptr, nullptr);
  }
};

WidgetPtr createApp(FluxUI *app) {
  return FluxApp("Flux - ListView Rows", std::make_shared<MyApp>(),
                 AppTheme::light(), false, 600, 400, false, false);
}