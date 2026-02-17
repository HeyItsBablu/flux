#include "flux.hpp"
#include "flux_dialog.hpp"
#include <windows.h>

class DemoComponent : public Component {
private:
  State<std::string> userName;
  State<std::string> userEmail;
  State<int> itemCount;
  State<bool> isActive;
  State<std::vector<std::string>> listItems;

  // ✅ Store dialog as member
  DialogWidgetPtr formDialog;

public:
  DemoComponent()
      : userName("", context), userEmail("", context), itemCount(0, context),
        listItems({}, context), isActive(false, context) {

    // Initialize with many items to demonstrate scrolling
    std::vector<std::string> leftData;

    for (int i = 1; i <= 20; i++) {
      leftData.push_back("Left Item " + std::to_string(i));
    }

    listItems.set(leftData);
  }

  WidgetPtr build() override {
    // ================================================================
    // MAIN UI
    // ================================================================
    return Scaffold(
        AppBar("Dialog Demo"),
        Center(
            Container(
                ListView(listItems)
                    ->itemBuilder([this](int i, const std::string &item) {
                      return Card(
                                 Row(Column(
                                         Text(item)
                                             ->setFontWeight(FontWeight::Bold)
                                             ->setFontSize(14),
                                         Text("Index: " + std::to_string(i))
                                             ->setFontSize(12)
                                             ->setTextColor(RGB(120, 120, 120)))
                                         ->setFlex(1)
                                         ->setSpacing(4),

                                     Button(
                                         "×",
                                         [this, i]() {
                                           listItems.update(
                                               [i](std::vector<std::string> v) {
                                                 if (i < v.size())
                                                   v.erase(v.begin() + i);
                                                 return v;
                                               });
                                         })

                                         ->setPadding(8))
                                     ->setSpacing(8)
                                     ->setCrossAlignment(Alignment::Center))
                          ->setPadding(12);
                    })
                    ->separator([]() { return SizedBox(0, 4); })
                    ->spacing(0)

                    )
                ->setBackgroundColor(RGB(100, 100, 100))
                ->setBorderRadius(8)
                ->setPadding(32)
                ->setMaxWidth(600)
                ->setHeight(400)
                ->setWidth(300)));
  }
};

WidgetPtr createApp(FluxUI *app) {
  return FluxApp("Demo App", BuildComponent<DemoComponent>(),
                 AppTheme::materialBlue());
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
  AllocConsole();
  FILE *fp;
  freopen_s(&fp, "CONOUT$", "w", stdout);

  std::cout << "=== Dialog Demo ===" << std::endl;

  GdiplusInitializer gdiplusInit;

  FluxUI app(hInstance);
  app.build([&]() { return createApp(&app); });
  app.createWindow("FluxUI - Dialog Demo", 900, 700);

  return app.run();
}