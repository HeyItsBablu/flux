#include "flux/flux.hpp"
#include <numeric>

class LayoutStressTest : public Component {
  State<int> boxCount;
  State<double> fillRatio;
  State<std::vector<int>> boxIndices;

  static constexpr int kMax = 30;

  void rebuildIndices(int count) {
    std::vector<int> v(count);
    std::iota(v.begin(), v.end(), 0);
    boxIndices.set(v);
    fillRatio.set((double)count / kMax);
  }

public:
  LayoutStressTest()
      : boxCount(9, context), fillRatio(9.0 / kMax, context),
        boxIndices({}, context) {}

  void initState() override { rebuildIndices(boxCount.get()); }

  WidgetPtr build() override {
    return Scaffold(
        AppBar("Layout Stress Test"),
        Column(
            {

                // ── Controls ─────────────────────────────────────────────
                Row({

                        Row({
                                Icon(FluxIcons::Remove, "Segoe MDL2 Assets", 14)
                                    ->setColor(RGB(255, 255, 255)),
                                Text("  -3"),
                            })
                            ->setSpacing(0),

                        Button(Icon(FluxIcons::Remove)
                                   ->setColor(RGB(255, 255, 255)),
                               [this] {
                                 int n = max(1, boxCount.get() - 3);
                                 boxCount.set(n);
                                 rebuildIndices(n);
                               })
                            ->setBackgroundColor(RGB(76, 175, 80)),

                        Text(
                            boxCount,
                            [](int v) { return std::to_string(v) + " boxes"; }),

                        Button(
                            Icon(FluxIcons::Add)->setColor(RGB(255, 255, 255)),
                            [this] {
                              int n = min(kMax, boxCount.get() + 3);
                              boxCount.set(n);
                              rebuildIndices(n);
                            })
                            ->setBackgroundColor(RGB(76, 175, 80)),

                        Row({
                                Icon(FluxIcons::Add, "Segoe MDL2 Assets", 14)
                                    ->setColor(RGB(255, 255, 255)),
                                Text("  +3"),
                            })
                            ->setSpacing(0),

                    })
                    ->setSpacing(8)
                    ->setPadding(12),

                // ── Progress ─────────────────────────────────────────────
                Column({
                           Row({
                                   Icon(FluxIcons::ChartBar,
                                        "Segoe MDL2 Assets", 13)
                                       ->setColor(RGB(100, 100, 100)),

                                   Text(boxCount,
                                        [](int v) {
                                          return "  " + std::to_string(v) +
                                                 " / " + std::to_string(kMax) +
                                                 " boxes";
                                        })
                                       ->setFontSize(12)
                                       ->setTextColor(RGB(100, 100, 100)),
                               })
                               ->setSpacing(0),

                           SizedBox(0, 4),

                           ProgressBar()
                               ->setValue(fillRatio)
                               ->setHeight(8)
                               ->setProgressColors(
                                   {RGB(66, 165, 245), RGB(102, 187, 106)})
                               ->setBorderRadius(4),

                       })
                    ->setSpacing(0)
                    ->setPadding(12),

                Divider(),

                // ── Body ────────────────────────────────────────────────
                Expanded(
                    Row({

                            // ── Left Panel
                            Container(
                                Column(
                                    {

                                        Row({
                                                Icon(FluxIcons::Dashboard,
                                                     "Segoe MDL2 Assets", 14)
                                                    ->setColor(
                                                        RGB(33, 150, 243)),

                                                Text("  Left Panel")
                                                    ->setFontWeight(
                                                        FontWeight::Bold),
                                            })
                                            ->setSpacing(0),

                                        SizedBox(0, 8), Divider(),
                                        SizedBox(0, 8),

                                        Text("Active boxes")
                                            ->setFontSize(11)
                                            ->setTextColor(RGB(120, 120, 120)),

                                        Text(boxCount,
                                             [](int v) {
                                               return std::to_string(v);
                                             })
                                            ->setFontSize(28)
                                            ->setFontWeight(FontWeight::Bold),

                                        SizedBox(0, 8),

                                        Text("Capacity")
                                            ->setFontSize(11)
                                            ->setTextColor(RGB(120, 120, 120)),

                                        SizedBox(0, 4),

                                        ProgressBar()
                                            ->setValue(fillRatio)
                                            ->setHeight(6)
                                            ->setProgressColors(
                                                {RGB(66, 165, 245),
                                                 RGB(102, 187, 106)})
                                            ->setBorderRadius(3),

                                        SizedBox(0, 12),

                                        Row({
                                                Icon(FluxIcons::Check,
                                                     "Segoe MDL2 Assets", 14)
                                                    ->setColor(
                                                        RGB(76, 175, 80)),

                                                Text("  Running")
                                                    ->setFontSize(12)
                                                    ->setTextColor(
                                                        RGB(76, 175, 80)),
                                            })
                                            ->setSpacing(0),
                                        GestureDetector(Text("Hello Gesture"))
                                            ->setOnTap([] {
                                              std::cout << "Tapped box " << +1
                                                        << std::endl;
                                            })
                                            ->setOnDoubleTap([] {
                                              std::cout << "Double tapped box "
                                                        << +1 << std::endl;
                                            })
                                            ->setOnSecondaryTap([] {
                                              std::cout << "Right click box "
                                                        << +1 << std::endl;
                                            })

                                    })
                                    ->setSpacing(4))
                                ->setWidth(200)
                                ->setPadding(16)
                                ->setBackgroundColor(RGB(245, 245, 250))
                                ->setBorderColor(RGB(220, 220, 230))
                                ->setBorderWidth(1),

                            // ── Right Panel
                            Expanded(
                                Column(
                                    {

                                        Row({
                                                Icon(FluxIcons::Grid,
                                                     "Segoe MDL2 Assets", 13)
                                                    ->setColor(
                                                        RGB(120, 120, 120)),

                                                Text("  GridView (reactive)")
                                                    ->setFontSize(12)
                                                    ->setTextColor(
                                                        RGB(120, 120, 120)),
                                            })
                                            ->setSpacing(0),

                                        SizedBox(0, 8),

                                        Expanded(
                                            Column(
                                                {Text("GridView (reactive)")
                                                     ->setFontSize(12)
                                                     ->setTextColor(
                                                         RGB(120, 120, 120)),
                                                 SizedBox(0, 8),
                                                 Expanded(
                                                     GridView(boxIndices)
                                                         ->columns(3)
                                                         ->itemBuilder([](int i,
                                                                          const int &idx)
                                                                           -> WidgetPtr {
                                                           static const COLORREF colors[] = {
                                                               RGB(239, 83, 80),
                                                               RGB(66, 165, 245),
                                                               RGB(102, 187, 106),
                                                               RGB(255, 167, 38),
                                                               RGB(171, 71, 188),
                                                               RGB(38, 198, 218),
                                                           };
                                                           COLORREF c =
                                                               colors[idx % 6];
                                                           return Container(
                                                                      Center(
                                                                          Text(
                                                                              std::to_string(
                                                                                  idx +
                                                                                  1))
                                                                              ->setTextColor(RGB(
                                                                                  255,
                                                                                  255,
                                                                                  255))
                                                                              ->setFontWeight(
                                                                                  FontWeight::
                                                                                      Bold)))
                                                               ->setHeight(80)
                                                               ->setBackgroundColor(
                                                                   c)
                                                               ->setBorderRadius(
                                                                   6);
                                                         })
                                                         ->setSpacing(8))})
                                                ->setSpacing(0)
                                                ->setPadding(12))

                                    })
                                    ->setSpacing(0)
                                    ->setPadding(12)),

                        })
                        ->setSpacing(0)),

            })
            ->setSpacing(0));
  }
};

WidgetPtr createApp(FluxUI *app) {
  return FluxApp("Layout Stress Test", BuildComponent<LayoutStressTest>(),
                 AppTheme::light());
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
  AllocConsole();
  FILE *fp;
  freopen_s(&fp, "CONOUT$", "w", stdout);
  FluxUI app(hInstance);
  app.build([&]() { return createApp(&app); });
  app.createWindow("FluxUI - Layout Stress Test", 900, 700);
  return app.run();
}