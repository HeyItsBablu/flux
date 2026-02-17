#include "flux.hpp"

#include <windows.h>

class ScrollableListApp : public Component {
private:
  State<std::vector<std::string>> leftItems;
  State<std::vector<std::string>> rightItems;
  State<int> counter;

public:
  ScrollableListApp()
      : counter(0, context), leftItems({}, context), rightItems({}, context) {
    // Initialize with many items to demonstrate scrolling
    std::vector<std::string> leftData;
    std::vector<std::string> rightData;

    for (int i = 1; i <= 5; i++) {
      leftData.push_back("Left Item " + std::to_string(i));
      rightData.push_back("Right Item " + std::to_string(i));
    }

    leftItems.set(leftData);
    rightItems.set(rightData);
  }

  WidgetPtr build() override {
    return Scaffold(
        AppBar("Scrollable ListView Demo"),

        Column(
            // Header with add button
            Container(
                Row(Text("Scroll with mouse wheel or drag scrollbar!")
                        ->setFontSize(14)
                        ->setTextColor(RGB(100, 100, 100))
                        ->setFlex(1),

                    Button("Add Items",
                           [this]() {
                             // Add items to both lists
                             leftItems.update([](std::vector<std::string> v) {
                               int next = v.size() + 1;
                               v.push_back("Left Item " + std::to_string(next));
                               return v;
                             });

                             rightItems.update([](std::vector<std::string> v) {
                               int next = v.size() + 1;
                               v.push_back("Right Item " +
                                           std::to_string(next));
                               return v;
                             });
                           })
                        ->setPadding(12),

                    Button("Clear",
                           [this]() {
                             leftItems.set({});
                             rightItems.set({});
                           })
                        ->setPadding(12))
                    ->setSpacing(8))
                ->setPadding(16),

            Divider(),

            // Two scrollable lists side by side
            Expanded(
                Row(
                    // Left scrollable list
                    Expanded(
                        Container(
                            ListView(leftItems)
                                ->itemBuilder([this](int i,
                                                     const std::string &item) {
                                  return Card(Row(Column(
                                                      Text(item)
                                                          ->setFontWeight(
                                                              FontWeight::Bold)
                                                          ->setFontSize(14),
                                                      Text("Index: " +
                                                           std::to_string(i))
                                                          ->setFontSize(12)
                                                          ->setTextColor(RGB(
                                                              120, 120, 120)))
                                                      ->setFlex(1)
                                                      ->setSpacing(4),

                                                  Button(
                                                      "×",
                                                      [this, i]() {
                                                        leftItems.update(
                                                            [i](std::vector<
                                                                std::string>
                                                                    v) {
                                                              if (i < v.size())
                                                                v.erase(
                                                                    v.begin() +
                                                                    i);
                                                              return v;
                                                            });
                                                      })
                                                      ->setPadding(8))
                                                  ->setSpacing(8)
                                                  ->setCrossAlignment(
                                                      Alignment::Center))
                                      ->setPadding(12);
                                })
                                ->separator([]() { return SizedBox(0, 4); })
                                ->spacing(0))
                            ->setBackgroundColor(RGB(250, 250, 250))
                            ->setPadding(8)),

                    // Vertical divider
                    Container(nullptr)
                        ->setBackgroundColor(RGB(224, 224, 224))
                        ->setWidth(2),

                    // Right scrollable list
                    Expanded(
                        Container(
                            ListView(rightItems)
                                ->itemBuilder([this](int i,
                                                     const std::string &item) {
                                  // Alternate colors for visual variety
                                  COLORREF bgColor =
                                      (i % 2 == 0)
                                          ? RGB(240, 248, 255)  // Light blue
                                          : RGB(255, 250, 240); // Light orange

                                  return Container(
                                             Row(Text(std::to_string(i + 1) +
                                                      ".")
                                                     ->setFontWeight(
                                                         FontWeight::Bold)
                                                     ->setTextColor(
                                                         RGB(103, 58, 183))
                                                     ->setFontSize(14),

                                                 Text(item)
                                                     ->setFontSize(14)
                                                     ->setFlex(1),

                                                 Button(
                                                     "Delete",
                                                     [this, i]() {
                                                       rightItems.update(
                                                           [i](std::vector<
                                                               std::string>
                                                                   v) {
                                                             if (i < v.size())
                                                               v.erase(
                                                                   v.begin() +
                                                                   i);
                                                             return v;
                                                           });
                                                     })

                                                     ->setPadding(6))
                                                 ->setSpacing(12)
                                                 ->setCrossAlignment(
                                                     Alignment::Center))
                                      ->setPadding(12);
                                })
                                ->spacing(4))
                            ->setPadding(8)))
                    ->setSpacing(0)))
            ->setSpacing(0));
  }
};

WidgetPtr createApp(FluxUI *app) {
  return FluxApp("Scrollable ListView", BuildComponent<ScrollableListApp>(),
                 AppTheme::materialBlue());
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
  AllocConsole();
  FILE *fp;
  freopen_s(&fp, "CONOUT$", "w", stdout);

  std::cout << "=== Scrollable ListView Demo ===" << std::endl;
  std::cout << "• Scroll with mouse wheel" << std::endl;
  std::cout << "• Drag the scrollbar" << std::endl;
  std::cout << "• Click scrollbar track to jump" << std::endl;
  std::cout << "• Add/remove items dynamically" << std::endl;

  FluxUI app(hInstance);
  app.build([&]() { return createApp(&app); });
  app.createWindow("FluxUI - Scrollable ListView", 1000, 700);

  return app.run();
}