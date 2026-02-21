#include "flux.hpp"


struct LogEntry {
  int id;
  std::string message;
  COLORREF color;
};

class ScrollStressTest : public Component {
  State<std::vector<LogEntry>> logItems;
  State<std::vector<int>>      gridItems;
  State<int>                   nextId;
  State<bool>                  showGrid;

  UINT_PTR timerId = 0;
  int autoAddCount = 0;

  const std::vector<std::string> messages = {
    "Constraint propagation OK",
    "BoxConstraints::tight applied",
    "Flex child allocated",
    "Scrollbar thumb updated",
    "Layout pass complete",
    "Viewport clamped",
    "Child remeasured",
    "Padding deflated",
  };

  const COLORREF tagColors[6] = {
    RGB(239, 83,  80),
    RGB(66,  165, 245),
    RGB(102, 187, 106),
    RGB(255, 167, 38),
    RGB(171, 71,  188),
    RGB(38,  198, 218),
  };

public:
  ScrollStressTest()
    : logItems({}, context),
      gridItems({}, context),
      nextId(1, context),
      showGrid(false, context) {}

  void initState() override {
    for (int i = 0; i < 12; i++) addOne();

    timerId = context->setInterval(1000, [this]() {
      if (autoAddCount >= 20) {
        context->clearInterval(timerId);
        timerId = 0;
        return;
      }
      addOne();
      autoAddCount++;
    });
  }

  void dispose() override {
    if (timerId) { context->clearInterval(timerId); timerId = 0; }
    Component::dispose();
  }

  void addOne() {
    int id = nextId.get();

    auto lg = logItems.get();
    lg.push_back({ id, "#" + std::to_string(id) + " — " + messages[id % 8], tagColors[id % 6] });
    logItems.set(lg);

    auto gi = gridItems.get();
    gi.push_back(id);
    gridItems.set(gi);

    nextId.set(id + 1);
  }

  void removeFirst() {
    auto lg = logItems.get();
    if (!lg.empty()) { lg.erase(lg.begin()); logItems.set(lg); }
    auto gi = gridItems.get();
    if (!gi.empty()) { gi.erase(gi.begin()); gridItems.set(gi); }
  }

  void clearAll() {
    logItems.set({});
    gridItems.set({});
    nextId.set(1);
    autoAddCount = 0;
  }

  WidgetPtr build() override {
    return Scaffold(
        AppBar("Scroll Stress Test"),
        Column(

            // ── Toolbar ───────────────────────────────────────────────
            Row(
                Button("+ Add",    [this]{ addOne(); }),
                Button("- Remove", [this]{ removeFirst(); }),
                Button("Clear",    [this]{ clearAll(); }),
                SizedBox(16, 0),
                Text(logItems, [](const std::vector<LogEntry> &v) {
                    return std::to_string(v.size()) + " items";
                })->setTextColor(RGB(100, 100, 100)),
                SizedBox(16, 0),
                Button("List", [this]{ showGrid.set(false); }),
                Button("Grid", [this]{ showGrid.set(true); })
            )
            ->setSpacing(8)
            ->setPadding(12),

            Divider(),

            // ── Switch between list and grid via bool state ───────────
            Expanded(
                Conditional(showGrid)
                ->Then([this]() -> WidgetPtr {
                    return Padding(12,
                        Column(
                            Text("GridView — 3 columns")
                                ->setFontSize(12)
                                ->setTextColor(RGB(120, 120, 120)),
                            SizedBox(0, 6),
                            Expanded(
                                GridView(gridItems)
                                    ->columns(3)
                                    ->setSpacing(10)
                                    ->itemBuilder([this](int i, const int &id) -> WidgetPtr {
                                        COLORREF c = tagColors[id % 6];
                                        return Container(
                                            Center(
                                                Text(std::to_string(id))
                                                    ->setFontSize(22)
                                                    ->setFontWeight(FontWeight::Bold)
                                                    ->setTextColor(RGB(255, 255, 255))
                                            )
                                        )
                                        ->setHeight(90)
                                        ->setBackgroundColor(c)
                                        ->setBorderRadius(8);
                                    })
                            )
                        )->setSpacing(0)
                    );
                })
                ->Else([this]() -> WidgetPtr {
                    return Row(

                        // Vertical ListView
                        Expanded(
                            Column(
                                Text("Vertical List")
                                    ->setFontSize(12)
                                    ->setTextColor(RGB(120, 120, 120)),
                                SizedBox(0, 6),
                                Expanded(
                                    ListView(logItems)
                                        ->itemBuilder([this](int i, const LogEntry &e) -> WidgetPtr {
                                            return Row(
                                                Container()
                                                    ->setWidth(4)
                                                    ->setBackgroundColor(e.color)
                                                    ->setMinHeight(36),
                                                SizedBox(8, 0),
                                                Text(e.message)->setFontSize(12)
                                            )
                                            ->setCrossAlignment(Alignment::Center);
                                        })
                                        ->separator([]() -> WidgetPtr { return Divider(); })
                                        ->setScrollbarColor(RGB(180, 180, 200))
                                )
                            )
                            ->setSpacing(0)
                            ->setPadding(12)
                        ),

                        Container()->setWidth(1)->setBackgroundColor(RGB(220, 220, 220)),

                        // Horizontal ListView
                        Expanded(
                            Column(
                                Text("Horizontal List")
                                    ->setFontSize(12)
                                    ->setTextColor(RGB(120, 120, 120)),
                                SizedBox(0, 6),
                                Container(
                                    ListView(logItems)
                                        ->setHorizontal(true)
                                        ->itemBuilder([this](int i, const LogEntry &e) -> WidgetPtr {
                                            return Container(
                                                Center(
                                                    Text(std::to_string(e.id))
                                                        ->setFontSize(20)
                                                        ->setFontWeight(FontWeight::Bold)
                                                        ->setTextColor(RGB(255, 255, 255))
                                                )
                                            )
                                            ->setWidth(80)
                                            ->setHeight(80)
                                            ->setBackgroundColor(e.color)
                                            ->setBorderRadius(8);
                                        })
                                        ->setSpacing(8)
                                )
                                ->setHeight(110)
                            )
                            ->setSpacing(0)
                            ->setPadding(12)
                        )

                    )->setSpacing(0);
                })
            )

        )->setSpacing(0)
    );
  }
};

WidgetPtr createApp(FluxUI *app) {
  return FluxApp("Scroll Stress Test", BuildComponent<ScrollStressTest>(),
                 AppTheme::light());
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
  FluxUI app(hInstance);
  app.build([&]() { return createApp(&app); });
  app.createWindow("FluxUI - Scroll Stress Test", 900, 700);
  return app.run();
}