#include "flux.hpp"


class ListViewDemoComponent : public Component {
private:
  State<std::vector<std::string>> items{ {
      "Apple", "Banana", "Cherry", "Dragonfruit",
      "Elderberry", "Fig", "Grape", "Honeydew",
      "Kiwi", "Lemon", "Mango", "Nectarine",
  }, context };

public:
  WidgetPtr build() override {
    return Scaffold(
        AppBar("ListView Demo"),
        Column(
            // Vertical list — takes 2/3 of the space
            Expanded(
                ListView(items)
                    ->setSpacing(8)
                    ->itemBuilder([](int i, const std::string &name) -> WidgetPtr {
                        return Container(
                            Row(
                                Container(
                                    Center(Text(std::to_string(i + 1))
                                        ->setFontSize(12)
                                        ->setTextColor(RGB(160, 160, 160)))
                                )
                                ->setWidth(32)->setHeight(32)
                                ->setBorderRadius(16)
                                ->setBackgroundColor(RGB(240, 240, 245)),

                                Text(name)
                                    ->setFontSize(14)
                                    ->setTextColor(RGB(30, 30, 30))
                            )
                            ->setSpacing(12)
                            ->setCrossAlignment(Alignment::Center)
                        )
                        ->setPaddingAll(12, 10, 12, 10)
                        ->setBackgroundColor(RGB(255, 255, 255))
                        ->setBorderColor(RGB(230, 230, 235))
                        ->setBorderWidth(1)
                        ->setBorderRadius(8);
                    }),
                2
            ),

            Divider(),

            // Horizontal list — takes 1/3 of the space
            Expanded(
                ListView(items)
                    ->setHorizontal(true)
                    ->setSpacing(10)
                    ->itemBuilder([](int i, const std::string &name) -> WidgetPtr {
                        return Container(
                            Column(
                                Text("🍎")->setFontSize(28),
                                Text(name)
                                    ->setFontSize(12)
                                    ->setTextColor(RGB(30, 30, 30))
                            )
                            ->setSpacing(6)
                            ->setCrossAlignment(Alignment::Center)
                        )
                        ->setWidth(100)
                        ->setPadding(12)
                        ->setBorderRadius(10)
                        ->setBackgroundColor(RGB(255, 255, 255))
                        ->setBorderColor(RGB(230, 230, 235))
                        ->setBorderWidth(1);
                    }),
                1
            )
        )
        ->setSpacing(0)
        ->setPadding(16)
    );
  }
};

WidgetPtr createApp(FluxUI *app) {
  return FluxApp("ListView Demo", BuildComponent<ListViewDemoComponent>(),
                 AppTheme::materialBlue());
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
  FluxUI app(hInstance);
  app.build([&]() { return createApp(&app); });
  app.createWindow("FluxUI - ListView Demo", 600, 700);
  return app.run();
}