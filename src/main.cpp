#include "flux.hpp"
#include "flux_graph.hpp"
#include <windows.h>
// ============================================================================
// PARTIAL REBUILD TEST
//
// Layout of the window:
//
//  ┌─────────────────────────────────────────────────────┐
//  │  AppBar                                             │
//  ├──────────────────────┬──────────────────────────────┤
//  │  LEFT PANEL          │  RIGHT PANEL                 │
//  │  (fixed 380px wide)  │  (fixed 380px wide)          │
//  │                      │                              │
//  │  [Counter Card]      │  [Text Input Card]           │
//  │   count state        │   inputText state            │
//  │                      │                              │
//  │  [Toggle Card]       │  [Slider Card]               │
//  │   toggleState        │   sliderValue state          │
//  │                      │                              │
//  │  [Repaint Log]       │  [Progress Card]             │
//  │   shows which zone   │   driven by slider           │
//  │   last changed       │                              │
//  └──────────────────────┴──────────────────────────────┘
//
//  Each card is a fixed-size Container (autoWidth=false, autoHeight=false)
//  so it acts as a layout boundary.  When its state changes only its own
//  rect should be invalidated — the opposite panel must NOT flicker.
//
//  To visually verify: watch the window while clicking/typing.  The areas
//  that do NOT have focus should stay perfectly still (no full-window flash).
// ============================================================================

class PartialRebuildTestComponent : public Component {
private:
  State<int> counter;
  State<bool> toggleState;
  State<std::string> inputText;
  State<double> sliderValue;
  State<std::string> lastChanged; // tracks which zone was touched

public:
  PartialRebuildTestComponent()
      : counter(0, context), toggleState(false, context),
        inputText("Edit me", context), sliderValue(0.4, context),
        lastChanged("none", context) {}

  WidgetPtr build() override {

    // ── ROOT ──────────────────────────────────────────────────────────────

    return Scaffold(
        AppBar("Partial Rebuild Test — each card is an independent layout "
               "boundary"),

        Container(
            Center(
                Column(Graph(300, 200)
                           ->addSeries("Temperature",
                                       {22, 24, 27, 23, 19, 21, 26}, 1.0f, 0.4f,
                                       0.2f)
                           ->setTitle("Daily Temps")
                           ->setXLabels({"Mon", "Tue", "Wed", "Thu", "Fri",
                                         "Sat", "Sun"}),
                       Graph(300, 200)
                           ->setType(GraphType::Bar)
                           ->addSeries("Sales", {120, 95, 180, 75, 200}, 0.2f,
                                       0.7f, 1.0f)
                           ->addSeries("Target", {100, 100, 150, 100, 180},
                                       1.0f, 0.6f, 0.2f)
                           ->setXLabels({"Q1", "Q2", "Q3", "Q4", "Q5"}),
                       Graph(300, 200)
                           ->setType(GraphType::Area)
                           ->addSeries("Revenue", {10, 40, 30, 80, 60, 95, 110},
                                       0.3f, 1.0f, 0.5f)
                           ->setYRange(0, 130))
                    ->setSpacing(16)
                    ->setCrossAlignment(Alignment::Center))));
  }
};

// ============================================================================
// ENTRY POINT
// ============================================================================

WidgetPtr createApp(FluxUI *app) {
  return FluxApp("Graph Test", BuildComponent<PartialRebuildTestComponent>(),
                 AppTheme::materialBlue());
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
  AllocConsole();
  FILE *fp;
  freopen_s(&fp, "CONOUT$", "w", stdout);
  std::cout << "=== Graph Test ===" << std::endl;

  GdiplusInitializer gdiplusInit;
  FluxUI app(hInstance);
  app.build([&]() { return createApp(&app); });
  app.createWindow("FluxUI - Graph Test", 820, 700);
  return app.run();
}