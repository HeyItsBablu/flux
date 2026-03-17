#include "flux/flux.hpp"
#include <numeric>

class LayoutStressTest : public Component {
  State<int> boxCount;
  State<std::vector<int>> boxIndices; // drive the list

public:
  LayoutStressTest() : boxCount(9, context), boxIndices({}, context) {}

  void initState() override { rebuildIndices(boxCount.get()); }

  void rebuildIndices(int count) {
    std::vector<int> v(count);
    std::iota(v.begin(), v.end(), 0);
    boxIndices.set(v);
  }

  WidgetPtr build() override {
    return Scaffold(
        AppBar("Layout Stress Test"),
        Column(
            {

                // ── Controls ──────────────────────────────────────────────
                Row({Button("-3",
                            [this] {
                              int n = max(1, boxCount.get() - 3);
                              boxCount.set(n);
                              rebuildIndices(n);
                            }),
                     Text(boxCount,
                          [](int v) { return std::to_string(v) + " boxes"; }),
                     Button("+3",
                            [this] {
                              int n = boxCount.get() + 3;
                              boxCount.set(n);
                              rebuildIndices(n);
                            })})
                    ->setSpacing(8)
                    ->setPadding(12),

                Divider(),

                // ── Body ──────────────────────────────────────────────────
                Expanded(
                    Row({

                            // Left panel
                            Container(
                                Column({Text("Left Panel")
                                            ->setFontWeight(FontWeight::Bold),
                                        SizedBox(0, 8), Divider(),
                                        SizedBox(0, 8),
                                        Text("Boxes:")->setFontSize(12),
                                        Text(boxCount,
                                             [](int v) {
                                               return std::to_string(v);
                                             })
                                            ->setFontSize(28)
                                            ->setFontWeight(FontWeight::Bold)})
                                    ->setSpacing(4))
                                ->setWidth(200)
                                ->setPadding(16)
                                ->setBackgroundColor(RGB(245, 245, 250))
                                ->setBorderColor(RGB(220, 220, 230))
                                ->setBorderWidth(1),

                            // Right panel — GridView reacts to boxIndices state
                            Expanded(
                                Column(
                                    {Text("GridView (reactive)")
                                         ->setFontSize(12)
                                         ->setTextColor(RGB(120, 120, 120)),
                                     SizedBox(0, 8),
                                     Expanded(
                                         GridView(boxIndices)
                                             ->columns(3)
                                             ->itemBuilder([](int i,
                                                              const int &idx)
                                                               -> WidgetPtr {
                                               static const COLORREF colors[] =
                                                   {
                                                       RGB(239, 83, 80),
                                                       RGB(66, 165, 245),
                                                       RGB(102, 187, 106),
                                                       RGB(255, 167, 38),
                                                       RGB(171, 71, 188),
                                                       RGB(38, 198, 218),
                                                   };
                                               COLORREF c = colors[idx % 6];
                                               return Container(
                                                          Center(
                                                              Text(
                                                                  std::
                                                                      to_string(
                                                                          idx +
                                                                          1))
                                                                  ->setTextColor(
                                                                      RGB(255,
                                                                          255,
                                                                          255))
                                                                  ->setFontWeight(
                                                                      FontWeight::
                                                                          Bold)))
                                                   ->setHeight(80)
                                                   ->setBackgroundColor(c)
                                                   ->setBorderRadius(6);
                                             })
                                             ->setSpacing(8))})
                                    ->setSpacing(0)
                                    ->setPadding(12))})
                        ->setSpacing(0))

            })
            ->setSpacing(0));
  }
};

WidgetPtr createApp(FluxUI *app) {
  return FluxApp("Layout Stress Test", BuildComponent<LayoutStressTest>(),
                 AppTheme::light());
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
  FluxUI app(hInstance);
  app.build([&]() { return createApp(&app); });
  app.createWindow("FluxUI - Layout Stress Test", 900, 700);
  return app.run();
}