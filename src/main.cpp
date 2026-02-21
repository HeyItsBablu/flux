#include "flux.hpp"

#include <windows.h>

class ConditionalListApp : public Component {
private:
  State<bool> isActive;
  State<std::string> newText;
  State<int> elapsed;
  UINT_PTR timerId = 0;

public:
  ConditionalListApp()
      : isActive(false, context), newText("Hello", context),
        elapsed(0, context) {} // Use useState instead

  void updateState() {
    std::cout << "Button isActive" << isActive.get() << std::endl;
    isActive.set(!isActive.get());
  }

  void login() {
    std::cout << "Check box" << isActive.get() << std::endl;
    std::cout << "Username" << newText.get() << std::endl;
  }

  WidgetPtr build() override {
    return Scaffold(
        AppBar("Conditional App"),
        Center(Column(Text(elapsed)->setTextColor(RGB(100, 100, 100)),
                      TextInput("Enter username...")->setInputValue(newText),

                      CheckBox("Enable feature")->setInputValue(isActive),

                      Button("Login", [&] { login(); })

                          )));
  }

  void initState() override {

    timerId = context->setInterval(
        1000, [this]() { elapsed.set(elapsed.get() + 1); });
  }

  void dispose() override {

    if (timerId) {
      context->clearInterval(timerId);
      timerId = 0;
    }
    Component::dispose();
  }
};

WidgetPtr createApp(FluxUI *app) {
  return FluxApp("Conditional App", BuildComponent<ConditionalListApp>(),
                 AppTheme::materialGreen());
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
  AllocConsole();
  FILE *fp;
  freopen_s(&fp, "CONOUT$", "w", stdout);

  std::cout << "=== Conditional List Demo ===" << std::endl;

  FluxUI app(hInstance);
  app.build([&]() { return createApp(&app); });
  app.createWindow("FluxUI - Conditional App", 800, 700);

  return app.run();
}