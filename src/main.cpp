#include "flux.hpp"

#include <windows.h>

class InputTestComponent : public Component {
private:
  State<bool> isActive;

public:
  InputTestComponent() : isActive(false, context) {} // Initialize with "md"

  WidgetPtr build() override {
    return Scaffold(
        AppBar("Input Test App"),
        Center( // Product card
            Container(
                Column(Image("C:/Upwork/c_projects/flux/src/images/main.png")
                           ->setFit(ImageFit::Fill)
                           ->setWidth(200)
                           ->setHeight(200)
                           ->setBorderRadius(100),

                       Text("Product Name")
                           ->setFontWeight(FontWeight::Bold)
                           ->setFontSize(16),

                       Text("$99.99")
                           ->setTextColor(RGB(76, 175, 80))
                           ->setFontSize(18))
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