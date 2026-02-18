#include "flux.hpp"
#include <windows.h>

// ============================================================================
// WIDGET TEST COMPONENT
// ============================================================================
// Each section tests a specific widget or builder method.
// Scroll through the tabs to see all tests.
// ============================================================================

class WidgetTestComponent : public Component {
private:
  // --- State for interactive tests ---
  State<std::string> inputText;
  State<int> counter;
  State<bool> toggleState;
  State<bool> hoverFlag;
  State<std::vector<std::string>> listItems;
  State<int> activeTab;
  State<double> progress;

public:
  WidgetTestComponent()
      : inputText("Hello", context), counter(0, context),
        toggleState(true, context), hoverFlag(false, context),
        listItems({}, context), activeTab(0, context), progress(0.5, context) {

    std::vector<std::string> items;
    for (int i = 1; i <= 30; i++)
      items.push_back("Item " + std::to_string(i));
    listItems.set(items);
  }

  void callingFrom() { std::cout << "Calling from the menu" << std::endl; }

  WidgetPtr build() override {
    return Scaffold(
        AppBar("FluxUI Widget Tests"),
        Column(
            Tooltip(Button("Hover me", [] {}), "Click to submit"),
            ContextMenu(
                Text("Right-click me!")->setFontSize(16)->setPadding(12),
                {ContextMenuItem::Action("Cut", [this] { callingFrom(); }),
                 ContextMenuItem::Action(
                     "Copy", [] { MessageBox(NULL, "Copy", "Action", MB_OK); }),
                 ContextMenuItem::Action(
                     "Paste",
                     [] { MessageBox(NULL, "Paste", "Action", MB_OK); }),
                 ContextMenuItem::Separator(),
                 ContextMenuItem::Action("Disabled Item", nullptr,
                                         toggleState.get())}),

            ProgressBar()
                ->setValue(progress)
                ->setHeight(16)
                ->setBackgroundColor(RGB(230, 230, 230))
                ->setProgressColors(
                    {RGB(56, 189, 248), RGB(99, 102, 241), RGB(168, 85, 247)})
                ->setBorderColor(RGB(180, 180, 180))
                ->setBorderRadius(8),
            Slider(0.0, 1.0, 0.01)
                ->setValue(progress)
                ->setTrackFillColor(RGB(99, 102, 241))
                ->setOnValueChanged([&](double v) {
                  std::cout << progress.get() << std::endl;
                  progress.set(v);
                })

                )
            ->setSpacing(0));
  }
};

// ============================================================================
// ENTRY POINT
// ============================================================================

WidgetPtr createApp(FluxUI *app) {
  return FluxApp("Widget Tests", BuildComponent<WidgetTestComponent>(),
                 AppTheme::materialBlue());
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
  AllocConsole();
  FILE *fp;
  freopen_s(&fp, "CONOUT$", "w", stdout);
  std::cout << "=== FluxUI Widget Tests ===" << std::endl;

  GdiplusInitializer gdiplusInit;
  FluxUI app(hInstance);
  app.build([&]() { return createApp(&app); });
  app.createWindow("FluxUI - Widget Tests", 960, 720);
  return app.run();
}