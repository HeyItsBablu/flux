#include "flux/flux.hpp"


class VerticalSplitExample : public Component {
public:
  WidgetPtr build() override {
    return Scaffold(
        AppBar("Vertical SplitView Demo"),
        SplitViewVertical(

            // ── Top pane ──────────────────────────────────────────────────
            Container(
                Column({
                    Text("Top Panel")
                        ->setFontWeight(FontWeight::Bold)
                        ->setFontSize(18),
                    SizedBox(0, 12),
                    Text("Drag the divider to resize.")
                        ->setFontSize(13)
                        ->setTextColor(Color::fromRGB(120, 120, 120)),
                })
                ->setSpacing(8))
                ->setPadding(20)
                ->setBackgroundColor(Color::fromRGB(240, 244, 255)),

            // ── Bottom pane ───────────────────────────────────────────────
            Container(
                Column({
                    Text("Bottom Panel")
                        ->setFontWeight(FontWeight::Bold)
                        ->setFontSize(18),
                    SizedBox(0, 12),
                    Text("Content goes here.")
                        ->setFontSize(13)
                        ->setTextColor(Color::fromRGB(120, 120, 120)),
                })
                ->setSpacing(8))
                ->setPadding(20)
                ->setBackgroundColor(Color::fromRGB(255, 255, 255)),

            0.4f // initial ratio — top pane gets 40%
        )
        ->setMinPaneWidth(80)
        ->setDividerColor(Color::fromRGB(210, 210, 210))
        ->setDividerHoverColor(Color::fromRGB(33, 150, 243)));
  }
};

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
  FluxUI app(hInstance);
  app.build([&]() {
    return FluxApp("Vertical SplitView Demo",
                   BuildComponent<VerticalSplitExample>(),
                   AppTheme::light());
  });
  app.createWindow("FluxUI - Vertical SplitView Demo", 900, 600);
  return app.run();
}