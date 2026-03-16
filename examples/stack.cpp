#include "flux.hpp"

class StackDemo : public Component {
  State<bool> showBadge;
  State<int> notifCount;

public:
  StackDemo() : showBadge(true, context), notifCount(0, context) {}

  WidgetPtr build() override {
    return Scaffold(
        AppBar("Stack Demo"),
        Column(

            // ── Controls ──────────────────────────────────────────────
            Row(Button("Toggle Badge",
                       [this] { showBadge.set(!showBadge.get()); }),
                Button("+1", [this] { notifCount.set(notifCount.get() + 1); }),
                Button("Reset",
                       [this] {
                         notifCount.set(0);
                        
                       }))
                ->setSpacing(8)
                ->setPadding(12),

            Divider(),

            // ── Stack Examples ────────────────────────────────────────
            Expanded(
                Column(

                    // 1. Bell icon with a notification badge
                    Text("Icon with badge")
                        ->setFontSize(12)
                        ->setTextColor(RGB(120, 120, 120)),
                    SizedBox(0, 8),
                    Stack(Container()
                              ->setWidth(56)
                              ->setHeight(56)
                              ->setBackgroundColor(RGB(66, 165, 245))
                              ->setBorderRadius(28),
                          Conditional(showBadge)
                              ->Then([this]() {
                                return Positioned(
                                    Container(Center(Text(notifCount)
                                                         ->setFontSize(10)
                                                         ->setTextColor(
                                                             RGB(255, 255, 255))
                                                         ->setFontWeight(
                                                             FontWeight::Bold)))
                                        ->setWidth(20)
                                        ->setHeight(20)
                                        ->setBackgroundColor(RGB(239, 83, 80))
                                        ->setBorderRadius(10),
                                    /*left*/ 36, /*top*/ 0, /*right*/ 0,
                                    /*bottom*/ 0);
                              })
                              ->Else([]() { return SizedBox(0, 0); })
                         
                          )
                        ->setWidth(56)
                        ->setHeight(56),

                    SizedBox(0, 24),

                    // 2. Image-style card with a text overlay at the bottom
                    Text("Card with overlay")
                        ->setFontSize(12)
                        ->setTextColor(RGB(120, 120, 120)),
                    SizedBox(0, 8),
                    Stack(
                        // Background
                        Container()
                            ->setWidth(280)
                            ->setHeight(160)
                            ->setBackgroundColor(RGB(102, 187, 106))
                            ->setBorderRadius(10),
                        // Centered label
                        Positioned(Text("Background")
                                       ->setTextColor(RGB(255, 255, 255))
                                       ->setFontWeight(FontWeight::Bold),
                                   /*left*/ 0, /*top*/ 60, /*right*/ 0,
                                   /*bottom*/ 0),
                        // Bottom bar overlay
                        Positioned(
                            Container(Text("Overlay Label")
                                          ->setFontSize(12)
                                          ->setTextColor(RGB(255, 255, 255)))
                                ->setWidth(280)
                                ->setHeight(36)
                                ->setBackgroundColor(RGB(0, 0, 0))
                                ->setPadding(8),
                            /*left*/ 0, /*top*/ 0, /*right*/ 0, /*bottom*/ 0))
                        ->setWidth(280)
                        ->setHeight(160)
                        ->setBorderRadius(10),

                    SizedBox(0, 24),

                    // 3. Layered colored boxes — shows z-ordering
                    Text("Z-order layers")
                        ->setFontSize(12)
                        ->setTextColor(RGB(120, 120, 120)),
                    SizedBox(0, 8),
                    Stack(Container()
                              ->setWidth(120)
                              ->setHeight(120)
                              ->setBackgroundColor(RGB(239, 83, 80))
                              ->setBorderRadius(6),
                          Positioned(Container()
                                         ->setWidth(90)
                                         ->setHeight(90)
                                         ->setBackgroundColor(RGB(66, 165, 245))
                                         ->setBorderRadius(6),
                                     /*left*/ 18, /*top*/ 18),
                          Positioned(Container()
                                         ->setWidth(60)
                                         ->setHeight(60)
                                         ->setBackgroundColor(RGB(255, 167, 38))
                                         ->setBorderRadius(6),
                                     /*left*/ 36, /*top*/ 36))
                        ->setWidth(120)
                        ->setHeight(120))

                    ->setSpacing(0)
                    ->setPadding(24)
                    ->setCrossAxisAlignment(CrossAxisAlignment::Start)))

            ->setSpacing(0));
  }
};

WidgetPtr createApp(FluxUI *app) {
  return FluxApp("Stack Demo", BuildComponent<StackDemo>(), AppTheme::light());
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
  FluxUI app(hInstance);
  app.build([&]() { return createApp(&app); });
  app.createWindow("FluxUI - Stack Demo", 600, 600);
  return app.run();
}