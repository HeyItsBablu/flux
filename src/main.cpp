#include "flux.hpp"

#include <windows.h>

class InputTestComponent : public Component {
private:
  State<bool> isActive;
  State<int> selectedIndex;
  State<std::vector<std::string>> leftItems;

public:
  InputTestComponent()
      : isActive(false, context), selectedIndex(-1, context),
        leftItems({}, context) {
    // Initialize with many items to demonstrate scrolling
    std::vector<std::string> leftData;

    for (int i = 1; i <= 20; i++) {
      leftData.push_back("Left Item " + std::to_string(i));
    }

    leftItems.set(leftData);

  } // Initialize with "md"

  WidgetPtr build() override {
    return Scaffold(
        AppBar("Input Test App"),
        Center( // Product card
            Container(
                Column(

                    // Expanded(
                    //     Container(
                    //         ListView(leftItems)
                    //             ->itemBuilder([this](int i,
                    //                                  const std::string &item) {
                    //               return Card(Row(Column(
                    //                                   Text(item)
                    //                                       ->setFontWeight(
                    //                                           FontWeight::Bold)
                    //                                       ->setFontSize(14),
                    //                                   Text("Index: " +
                    //                                        std::to_string(i))
                    //                                       ->setFontSize(12)
                    //                                       ->setTextColor(RGB(
                    //                                           120, 120, 120)))
                    //                                   ->setFlex(1)
                    //                                   ->setSpacing(4),

                    //                               Button(
                    //                                   "×",
                    //                                   [this, i]() {
                    //                                     leftItems.update(
                    //                                         [i](std::vector<
                    //                                             std::string>
                    //                                                 v) {
                    //                                           if (i < v.size())
                    //                                             v.erase(
                    //                                                 v.begin() +
                    //                                                 i);
                    //                                           return v;
                    //                                         });
                    //                                   })

                    //                                   ->setPadding(8)
                    //                                   )
                    //                               ->setSpacing(8)
                    //                               ->setCrossAlignment(
                    //                                   Alignment::Center))
                    //                   ->setPadding(12);
                    //             })
                    //             ->separator([]() { return SizedBox(0, 4); })
                    //             ->spacing(0))
                    //         ->setPadding(8)),

                    Text("Product Name")
                        ->setFontWeight(FontWeight::Bold)
                        ->setFontSize(16),

                    Text("$99.99")
                        ->setTextColor(RGB(76, 175, 80))
                        ->setFontSize(18),
                    Dropdown({"Apple", "Banana", "Cherry"})
                        ->setPlaceholder("Select a fruit")
                        ->setSelectedIndex(selectedIndex),
                    Button("Click me",
                           [] { std::cout << "Button clicked" << std::endl; }))
                    ->setSpacing(12))
                ->setPadding(16)

                ));
  }
};

WidgetPtr createApp(FluxUI *app) {
  return FluxApp("Input Test App", BuildComponent<InputTestComponent>(),
                 AppTheme::materialGreen());
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
  AllocConsole();
  FILE *fp;
  freopen_s(&fp, "CONOUT$", "w", stdout);

  std::cout << "=== Input Test Demo ===" << std::endl;

  GdiplusInitializer gdiplusInit;

  FluxUI app(hInstance);
  app.build([&]() { return createApp(&app); });
  app.createWindow("FluxUI - Input Test App", 800, 800);

  return app.run();
}