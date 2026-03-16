#include "flux.hpp"

class OverflowTest : public Component {
public:
  WidgetPtr build() override {
    return Scaffold(
        AppBar("Overflow Test"),
        Column(

            // ── 1. Row overflow (horizontal) ──────────────────────────
            // Three 150px boxes inside a 200px row → 250px overflow
            Text("Row — horizontal overflow")
                ->setFontSize(12)
                ->setTextColor(RGB(120, 120, 120)),

            Row(
                Container(Text("One"))->setWidth(150)->setHeight(60)
                    ->setBackgroundColor(RGB(239, 83, 80)),
                Container(Text("Two"))->setWidth(150)->setHeight(60)
                    ->setBackgroundColor(RGB(66, 165, 245)),
                Container(Text("Three"))->setWidth(150)->setHeight(60)
                    ->setBackgroundColor(RGB(102, 187, 106))
            )
            ->setWidth(200)
            ->setId("row-overflow-demo"),

            SizedBox(0, 16),

            // ── 2. Column overflow (vertical) ─────────────────────────
            // Three 80px boxes inside a 150px column → 90px overflow
            Text("Column — vertical overflow")
                ->setFontSize(12)
                ->setTextColor(RGB(120, 120, 120)),

            Column(
                Container(Text("Four"))->setWidth(200)->setHeight(80)
                    ->setBackgroundColor(RGB(255, 167, 38)),
                Container(Text("Five"))->setWidth(200)->setHeight(80)
                    ->setBackgroundColor(RGB(171, 71, 188)),
                Container(Text("Six"))->setWidth(200)->setHeight(80)
                    ->setBackgroundColor(RGB(38, 198, 218))
            )
            ->setHeight(150)
            ->setId("column-overflow-demo"),

            SizedBox(0, 16),

            // ── 3. Container overflow (both axes) ─────────────────────
            // 300×120 child crammed into a 150×60 container
            Text("Container — both axes overflow")
                ->setFontSize(12)
                ->setTextColor(RGB(120, 120, 120)),

            Container(
                Container(Text("Seven"))
                    ->setWidth(300)->setHeight(120)
                    ->setBackgroundColor(RGB(239, 83, 80))
            )
            ->setWidth(150)
            ->setHeight(60)
            ->setBorderColor(RGB(0, 0, 0))
            ->setBorderWidth(2)
            ->setId("container-overflow-demo")

        )
        ->setSpacing(8)
        ->setPadding(16)
    );
  }
};

WidgetPtr createApp(FluxUI *app) {
  return FluxApp("Overflow Test", BuildComponent<OverflowTest>(),
                 AppTheme::light());
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
  FluxUI app(hInstance);
  app.build([&]() { return createApp(&app); });
  app.createWindow("FluxUI - Overflow Test", 600, 700);
  return app.run();
}