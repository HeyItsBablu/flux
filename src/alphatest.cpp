#include "flux.hpp"
#include <windows.h>

// ============================================================================
// GLASS DEMO COMPONENT
// ============================================================================

class GlassDemoComponent : public Component {
private:
  State<double> sliderValue;
  State<bool> toggleState;
  State<int> activeCard;
  State<std::string> inputText;

public:
  GlassDemoComponent()
      : sliderValue(0.7, context), toggleState(false, context),
        activeCard(0, context), inputText("Type here...", context) {}

  // Helper: creates a glass panel container
  WidgetPtr GlassPanel(WidgetPtr child, int alpha = 60) {
    return Container(child)
        ->setBackgroundColor(RGB(255, 255, 255))
        ->setBackgroundAlpha(alpha)
        ->setBorderColor(RGB(255, 255, 255))
        ->setBorderAlpha(120)
        ->setBorderWidth(1)
        ->setBorderRadius(20)
        ->setPadding(24);
  }

  // Helper: frosted label
  WidgetPtr GlassLabel(const std::string &label) {
    return Container(Text(label)
                         ->setFontSize(11)
                         ->setFontWeight(FontWeight::Bold)
                         ->setTextColor(RGB(180, 210, 255)))
        ->setBackgroundColor(RGB(100, 160, 255))
        ->setBackgroundAlpha(40)
        ->setBorderColor(RGB(150, 200, 255))
        ->setBorderAlpha(80)
        ->setBorderWidth(1)
        ->setBorderRadius(20)
        ->setPaddingAll(6, 3, 6, 3);
  }

  WidgetPtr build() override {
    return Scaffold(
        AppBar("Glassmorphism Demo"),

        // Deep blue-purple gradient background simulation
        Container(
            Center(
                Column(

                    // ── Hero glass card ──────────────────────────────────
                    Container(
                        Column(
                            Row(GlassLabel("LIVE"),
                                Text("Glass Dashboard")
                                    ->setFontSize(22)
                                    ->setFontWeight(FontWeight::Bold)
                                    ->setTextColor(RGB(230, 240, 255)))
                                ->setSpacing(10)
                                ->setCrossAlignment(Alignment::Center),

                            Divider(),

                            Text("Alpha-blended surfaces with GDI+ rendering")
                                ->setFontSize(13)
                                ->setTextColor(RGB(180, 200, 240)),

                            // Progress bar inside glass
                            Column(Text("Signal Strength")
                                       ->setFontSize(12)
                                       ->setTextColor(RGB(160, 190, 230)),
                                   ProgressBar()
                                       ->setValue(sliderValue)
                                       ->setHeight(8)
                                       ->setBackgroundColor(RGB(255, 255, 255))
                                       ->setProgressColors({RGB(100, 180, 255),
                                                            RGB(130, 100, 255)})
                                       ->setBorderRadius(4))
                                ->setSpacing(8))
                            ->setSpacing(16))
                        ->setBackgroundColor(RGB(255, 255, 255))
                        ->setBackgroundAlpha(10)
                        ->setBorderColor(RGB(255, 255, 255))
                        ->setBorderAlpha(120)
                        ->setBorderWidth(1)
                        ->setBorderRadius(20)
                        ->setPadding(24),

                    // ── Three stat cards in a row ─────────────────────────
                    Row(
                        // Card 1
                        GlassPanel(
                            Column(Text("CPU")->setFontSize(11)->setTextColor(
                                       RGB(150, 190, 255)),
                                   Text("72%")
                                       ->setFontSize(28)
                                       ->setFontWeight(FontWeight::Bold)
                                       ->setTextColor(RGB(255, 255, 255)))
                                ->setSpacing(4)
                                ->setCrossAlignment(Alignment::Center),
                            60),

                        // Card 2
                        GlassPanel(
                            Column(Text("RAM")->setFontSize(11)->setTextColor(
                                       RGB(150, 255, 200)),
                                   Text("4.2 GB")
                                       ->setFontSize(28)
                                       ->setFontWeight(FontWeight::Bold)
                                       ->setTextColor(RGB(255, 255, 255)))
                                ->setSpacing(4)
                                ->setCrossAlignment(Alignment::Center),
                            60),

                        // Card 3
                        GlassPanel(
                            Column(Text("FPS")->setFontSize(11)->setTextColor(
                                       RGB(255, 200, 130)),
                                   Text("144")
                                       ->setFontSize(28)
                                       ->setFontWeight(FontWeight::Bold)
                                       ->setTextColor(RGB(255, 255, 255)))
                                ->setSpacing(4)
                                ->setCrossAlignment(Alignment::Center),
                            60))
                        ->setSpacing(16),

                    // ── Controls glass card ───────────────────────────────
                    GlassPanel(
                        Column(
                            Text("Controls")
                                ->setFontSize(16)
                                ->setFontWeight(FontWeight::Bold)
                                ->setTextColor(RGB(220, 235, 255)),

                            // Slider controlling opacity feel
                            Column(
                                Text("Opacity")->setFontSize(12)->setTextColor(
                                    RGB(160, 190, 230)),
                                Slider(0.0, 1.0, 0.01)
                                    ->setValue(sliderValue)
                                    ->setTrackFillColor(RGB(130, 100, 255))
                                    ->setOnValueChanged(
                                        [&](double v) { sliderValue.set(v); }))
                                ->setSpacing(8),

                            // Toggle row
                            Row(Text("Enable effect")
                                    ->setFontSize(13)
                                    ->setTextColor(RGB(200, 220, 255)),
                                Button(Text(toggleState, "ON", "OFF")
                                           ->setFontSize(12)
                                           ->setTextColor(RGB(255, 255, 255)),
                                       [&] {
                                         toggleState.set(!toggleState.get());
                                       }))
                                ->setSpacing(16)
                                ->setCrossAlignment(Alignment::Center))
                            ->setSpacing(16),
                        70))
                    ->setSpacing(20)
                    ->setCrossAlignment(Alignment::Stretch)
                    ->setMainAxisAlignment(MainAxisAlignment::Center)))
            ->setBackgroundColor(RGB(15, 25, 60))
            ->setBackgroundAlpha(255)
            ->setPadding(40));
  }
};

// ============================================================================
// ENTRY POINT
// ============================================================================

WidgetPtr createApp(FluxUI *app) {
  return FluxApp("Glass Demo", BuildComponent<GlassDemoComponent>(),
                 AppTheme::materialBlue());
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
  AllocConsole();
  FILE *fp;
  freopen_s(&fp, "CONOUT$", "w", stdout);
  std::cout << "=== Glass Demo ===" << std::endl;

  GdiplusInitializer gdiplusInit;
  FluxUI app(hInstance);
  HWND hwnd = app.getWindow(); // however you access the HWND

  // Client area (excludes title bar and borders — usually what you want for
  // rendering)
  RECT clientRect;
  GetClientRect(hwnd, &clientRect);
  int width = clientRect.right - clientRect.left;  // left is always 0
  int height = clientRect.bottom - clientRect.top; // top is always 0

  app.build([&]() { return createApp(&app); });
  app.createWindow("FluxUI - Glass Demo", height, width);
  return app.run();
}