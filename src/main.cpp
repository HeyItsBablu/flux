#include "flux.hpp"
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
  State<int>         counter;
  State<bool>        toggleState;
  State<std::string> inputText;
  State<double>      sliderValue;
  State<std::string> lastChanged;   // tracks which zone was touched

  // ── helpers ────────────────────────────────────────────────────────────

  // A clearly-bordered fixed-size card — gives layout boundary
  WidgetPtr BoundaryCard(const std::string &title,
                         WidgetPtr             content,
                         int                   w = 320,
                         int                   h = 130) {
    return Container(
               Column(
                   // Title bar
                   Container(Text(title)
                                 ->setFontSize(11)
                                 ->setFontWeight(FontWeight::Bold)
                                 ->setTextColor(RGB(255, 255, 255)))
                       ->setBackgroundColor(RGB(60, 100, 180))
                       ->setPaddingAll(10, 6, 10, 6),

                   // Body
                   Container(content)->setPadding(14))
               ->setSpacing(0))
        ->setBackgroundColor(RGB(245, 248, 255))
        ->setBorderColor(RGB(180, 200, 240))
        ->setBorderWidth(1)
        ->setBorderRadius(10)
        ->setWidth(w)
        ->setHeight(h);   // <-- fixed size = layout boundary
  }

  // Small pill badge showing which zone last fired
  WidgetPtr ZoneBadge(const std::string &zone, COLORREF color) {
    return Container(Text(zone)
                         ->setFontSize(11)
                         ->setTextColor(RGB(255, 255, 255)))
        ->setBackgroundColor(color)
        ->setBorderRadius(12)
        ->setPaddingAll(10, 4, 10, 4);
  }

public:
  PartialRebuildTestComponent()
      : counter(0, context),
        toggleState(false, context),
        inputText("Edit me", context),
        sliderValue(0.4, context),
        lastChanged("none", context) {}

  WidgetPtr build() override {

    // ── LEFT PANEL ────────────────────────────────────────────────────────

    // Card 1 — counter (int state, triggers layout when digits change width)
    auto counterCard = BoundaryCard(
        "Counter  (int state — layout boundary test)",
        Row(
            Button(Text("-")->setFontSize(18)->setTextColor(RGB(255,255,255)),
                   [&] {
                     counter.update([](int v){ return v - 1; });
                     lastChanged.set("Counter");
                   })
                ->setBackgroundColor(RGB(220, 60, 60))
                ->setHoverBackgroundColor(RGB(200, 40, 40))
                ->setBorderRadius(8)
                ->setPadding(10),

            Container(Text(counter)->setFontSize(28)->setFontWeight(FontWeight::Bold))
                ->setMinWidth(70)
                ->setPadding(8),

            Button(Text("+")->setFontSize(18)->setTextColor(RGB(255,255,255)),
                   [&] {
                     counter.update([](int v){ return v + 1; });
                     lastChanged.set("Counter");
                   })
                ->setBackgroundColor(RGB(60, 160, 60))
                ->setHoverBackgroundColor(RGB(40, 140, 40))
                ->setBorderRadius(8)
                ->setPadding(10))
            ->setSpacing(10)
            ->setCrossAlignment(Alignment::Center),
        320, 130);

    // Card 2 — toggle (bool state, paint-only)
    auto toggleCard = BoundaryCard(
        "Toggle  (bool state — paint-only boundary test)",
        Row(
            Text(toggleState, "● ON", "○ OFF")
                ->setFontSize(16)
                ->setFontWeight(FontWeight::Bold),
            Button(Text("Toggle")->setTextColor(RGB(255,255,255)),
                   [&] {
                     toggleState.set(!toggleState.get());
                     lastChanged.set("Toggle");
                   })
                ->setBackgroundColor(RGB(100, 80, 200))
                ->setHoverBackgroundColor(RGB(80, 60, 180))
                ->setBorderRadius(8)
                ->setPadding(10))
            ->setSpacing(14)
            ->setCrossAlignment(Alignment::Center),
        320, 110);

    // Card 3 — repaint log (string state)
    auto logCard = BoundaryCard(
        "Last Changed  (observer log)",
        Row(
            Text("Zone: ")->setFontSize(13)->setTextColor(RGB(100,100,120)),
            Text(lastChanged)
                ->setFontSize(13)
                ->setFontWeight(FontWeight::Bold)
                ->setTextColor(RGB(60, 120, 200)))
            ->setSpacing(4)
            ->setCrossAlignment(Alignment::Center),
        320, 90);

    auto leftPanel =
        Container(
            Column(counterCard, toggleCard, logCard)->setSpacing(16))
            ->setPadding(20)
            ->setWidth(380);

    // ── RIGHT PANEL ───────────────────────────────────────────────────────

    // Card 4 — text input (string state)
    auto inputCard = BoundaryCard(
        "Text Input  (string state — layout boundary test)",
        Column(
            TextInput()
                ->setInputValue(inputText),
            Row(Text("Length: ")->setFontSize(12)->setTextColor(RGB(120,120,140)),
                Text(inputText)->setFontSize(12))
                ->setSpacing(4))
            ->setSpacing(10),
        320, 140);

    // Card 5 — slider (double state)
    auto sliderCard = BoundaryCard(
        "Slider  (double state — progress boundary test)",
        Column(
            Slider(0.0, 1.0, 0.01)
                ->setValue(sliderValue)
                ->setTrackFillColor(RGB(80, 160, 255))
                ->setOnValueChanged([&](double v) {
                    sliderValue.set(v);
                    lastChanged.set("Slider");
                  }),
            Row(Text("Value: ")->setFontSize(12)->setTextColor(RGB(120,120,140)),
                Text(sliderValue)->setFontSize(12))
                ->setSpacing(4))
            ->setSpacing(10),
        320, 130);

    // Card 6 — progress bar driven by slider (proves cross-card state link
    //          still only repaints this card's rect, not the slider card)
    auto progressCard = BoundaryCard(
        "Progress  (reads sliderValue — separate boundary)",
        Column(
            ProgressBar()
                ->setValue(sliderValue)
                ->setHeight(12)
                ->setBackgroundColor(RGB(220, 230, 255))
                ->setProgressColors({RGB(80, 160, 255), RGB(140, 80, 255)})
                ->setBorderRadius(6),
            Text("Move the slider — only this card + slider card should repaint")
                ->setFontSize(11)
                ->setTextColor(RGB(140, 140, 160)))
            ->setSpacing(10),
        320, 120);

    auto rightPanel =
        Container(
            Column(inputCard, sliderCard, progressCard)->setSpacing(16))
            ->setPadding(20)
            ->setWidth(380);

    // ── ROOT ──────────────────────────────────────────────────────────────

    return Scaffold(
        AppBar("Partial Rebuild Test — each card is an independent layout boundary"),

        Container(
            Center(
                Column(
                    // Instruction banner
                    Container(
                        Text("Interact with any card. "
                             "Only that card's region should invalidate — "
                             "watch for full-window flicker.")
                            ->setFontSize(12)
                            ->setTextColor(RGB(60, 100, 60)))
                        ->setBackgroundColor(RGB(220, 255, 220))
                        ->setBorderColor(RGB(100, 200, 100))
                        ->setBorderWidth(1)
                        ->setBorderRadius(8)
                        ->setPadding(12),

                    // Zone badge row — each badge only repaints when its
                    // own state fires (they share lastChanged string state)
                    Row(ZoneBadge("Counter",   RGB(220, 60,  60)),
                        ZoneBadge("Toggle",    RGB(100, 80,  200)),
                        ZoneBadge("TextInput", RGB(60,  160, 120)),
                        ZoneBadge("Slider",    RGB(60,  120, 220)))
                        ->setSpacing(8),

                    // Two-column layout
                    Row(leftPanel, rightPanel)->setSpacing(0))
                    ->setSpacing(16)
                    ->setCrossAlignment(Alignment::Center)
                ))
            ->setBackgroundColor(RGB(235, 240, 252))
            ->setPadding(24));
  }
};

// ============================================================================
// ENTRY POINT
// ============================================================================

WidgetPtr createApp(FluxUI *app) {
  return FluxApp("Partial Rebuild Test",
                 BuildComponent<PartialRebuildTestComponent>(),
                 AppTheme::materialBlue());
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
  AllocConsole();
  FILE *fp;
  freopen_s(&fp, "CONOUT$", "w", stdout);
  std::cout << "=== Partial Rebuild Test ===" << std::endl;

  GdiplusInitializer gdiplusInit;
  FluxUI app(hInstance);
  app.build([&]() { return createApp(&app); });
  app.createWindow("FluxUI - Partial Rebuild Test", 820, 700);
  return app.run();
}