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

public:
  WidgetTestComponent()
      : inputText("Hello", context), counter(0, context),
        toggleState(false, context), hoverFlag(false, context),
        listItems({}, context), activeTab(0, context) {

    std::vector<std::string> items;
    for (int i = 1; i <= 30; i++)
      items.push_back("Item " + std::to_string(i));
    listItems.set(items);
  }

  // ------------------------------------------------------------------
  // TAB 1 — Text & Container
  // ------------------------------------------------------------------
  WidgetPtr buildTextTab() {
    return Padding(
        24,
        Column(
            // 1a. Plain text, font sizes
            Text("Text Widget Tests")
                ->setFontSize(20)
                ->setFontWeight(FontWeight::Bold),
            SizedBox(0, 16),

            // 1b. Font sizes
            Text("fontSize 10")->setFontSize(10),
            Text("fontSize 14")->setFontSize(14),
            Text("fontSize 18")->setFontSize(18),
            Text("fontSize 24")
                ->setFontSize(24)
                ->setFontWeight(FontWeight::Bold),
            SizedBox(0, 16),

            // 1c. Text colors
            Row(Text("Red   ")->setTextColor(RGB(220, 50, 50))->setFontSize(14),
                Text("Green ")->setTextColor(RGB(50, 180, 50))->setFontSize(14),
                Text("Blue  ")
                    ->setTextColor(RGB(50, 100, 220))
                    ->setFontSize(14))
                ->setSpacing(8),
            SizedBox(0, 16),

            // 1d. Text with background + borderRadius
            Text("Pill label")
                ->setFontSize(12)
                ->setTextColor(RGB(255, 255, 255))
                ->setBackgroundColor(RGB(33, 150, 243))
                ->setBorderRadius(12)
                ->setPadding(8),
            SizedBox(0, 16),

            // 1e. State-bound text
            Row(Text(counter),                 // shows raw int
                Text(toggleState, "ON", "OFF") // bool -> string
                )
                ->setSpacing(16),
            SizedBox(0, 16),

            // 1f. State-bound text color
            Text(toggleState, "Active", "Inactive")
                ->setTextColor(toggleState, RGB(50, 180, 50), RGB(180, 50, 50))
                ->setFontSize(14),
            SizedBox(0, 16),

            // 1g. setWidth / setHeight (fixed size box)
            Text("Fixed 200x30")
                ->setWidth(200)
                ->setHeight(30)
                ->setBackgroundColor(RGB(240, 240, 240))
                ->setBorderRadius(4)
                ->setPadding(4),
            SizedBox(0, 16),

            // 1h. setMinWidth
            Text("min-width 300")
                ->setMinWidth(300)
                ->setBackgroundColor(RGB(255, 243, 205))
                ->setBorderRadius(4)
                ->setPadding(8))
            ->setSpacing(0));
  }

  // ------------------------------------------------------------------
  // TAB 2 — Button
  // ------------------------------------------------------------------
  WidgetPtr buildButtonTab() {
    return Padding(
        24,
        Column(
            Text("Button Widget Tests")
                ->setFontSize(20)
                ->setFontWeight(FontWeight::Bold),
            SizedBox(0, 16),

            // 2a. Default Button (factory)
            Button("Default Button",
                   [this]() { counter.set(counter.get() + 1); }),
            SizedBox(0, 12),

            // 2b. Custom colors via builder
            std::static_pointer_cast<ButtonWidget>(
                std::make_shared<ButtonWidget>()), // skip — use factory + cast
                                                   // pattern below

            // Proper way: make_shared then chain
            []() -> WidgetPtr {
              auto b = std::make_shared<ButtonWidget>();
              b->text = "Custom Red Button";
              b->hasBackground = true;
              b->setBackgroundColor(RGB(200, 50, 50))
                  ->setHoverBackgroundColor(RGB(230, 80, 80))
                  ->setTextColor(RGB(255, 255, 255))
                  ->setBorderRadius(8)
                  ->setPadding(12);
              return b;
            }(),
            SizedBox(0, 12),

            // 2c. setWidth / setHeight on button
            []() -> WidgetPtr {
              auto b = std::make_shared<ButtonWidget>();
              b->text = "Wide 260px";
              b->hasBackground = true;
              b->setBackgroundColor(RGB(76, 175, 80))
                  ->setTextColor(RGB(255, 255, 255))
                  ->setWidth(260)
                  ->setHeight(44)
                  ->setBorderRadius(6);
              return b;
            }(),
            SizedBox(0, 12),

            // 2d. Widget-child button (icon + text via Row child)
            Button(Row(Text("+ Add Item")
                           ->setTextColor(RGB(255, 255, 255))
                           ->setFontSize(14)
                           ->setFontWeight(FontWeight::Bold)),
                   [this]() {
                     listItems.update([](std::vector<std::string> v) {
                       v.push_back("New Item " + std::to_string(v.size() + 1));
                       return v;
                     });
                   }),
            SizedBox(0, 12),

            // 2e. setOnClick after construction
            []() -> WidgetPtr {
              auto b = std::make_shared<ButtonWidget>();
              b->text = "setOnClick Test";
              b->hasBackground = true;
              b->setBackgroundColor(RGB(100, 100, 200))
                  ->setTextColor(RGB(255, 255, 255))
                  ->setPadding(10)
                  ->setBorderRadius(4)
                  ->setOnClick([]() {
                    MessageBoxA(nullptr, "setOnClick works!", "Test", MB_OK);
                  });
              return b;
            }())
            ->setSpacing(0));
  }

  // ------------------------------------------------------------------
  // TAB 3 — Row / Column / alignment
  // ------------------------------------------------------------------
  WidgetPtr buildLayoutTab() {
    return Padding(
        24,
        Column(
            Text("Row & Column Layout Tests")
                ->setFontSize(20)
                ->setFontWeight(FontWeight::Bold),
            SizedBox(0, 16),

            // 3a. Row with spacing
            Text("Row spacing=8:")->setFontSize(13),
            Row(Text("A")
                    ->setBackgroundColor(RGB(255, 200, 200))
                    ->setPadding(8),
                Text("B")
                    ->setBackgroundColor(RGB(200, 255, 200))
                    ->setPadding(8),
                Text("C")
                    ->setBackgroundColor(RGB(200, 200, 255))
                    ->setPadding(8))
                ->setSpacing(8),
            SizedBox(0, 16),

            // 3b. Row crossAlignment Center
            Text("Row crossAlignment Center:")->setFontSize(13),
            Row(Text("Tall")
                    ->setBackgroundColor(RGB(255, 220, 180))
                    ->setPadding(8)
                    ->setHeight(60),
                Text("Short")
                    ->setBackgroundColor(RGB(180, 220, 255))
                    ->setPadding(8))
                ->setSpacing(8)
                ->setCrossAlignment(Alignment::Center),
            SizedBox(0, 16),

            // 3c. Row mainAxisAlignment Center
            Text("Row mainAxis Center (in 400px):")->setFontSize(13),
            Container(Row(Text("X")
                              ->setBackgroundColor(RGB(255, 200, 200))
                              ->setPadding(8),
                          Text("Y")
                              ->setBackgroundColor(RGB(200, 255, 200))
                              ->setPadding(8))
                          ->setSpacing(8)
                          ->setMainAxisAlignment(MainAxisAlignment::Center))
                ->setBackgroundColor(RGB(245, 245, 245))
                ->setWidth(400)
                ->setHeight(40),
            SizedBox(0, 16),

            // 3d. Column with mainAxisAlignment Center
            Text("Column mainAxis Center:")->setFontSize(13),
            Container(Column(Text("Top")
                                 ->setBackgroundColor(RGB(255, 200, 200))
                                 ->setPadding(6),
                             Text("Bot")
                                 ->setBackgroundColor(RGB(200, 200, 255))
                                 ->setPadding(6))
                          ->setSpacing(4)
                          ->setMainAxisAlignment(MainAxisAlignment::Center))
                ->setBackgroundColor(RGB(245, 245, 245))
                ->setWidth(120)
                ->setHeight(120),
            SizedBox(0, 16),

            // 3e. Expanded inside Row
            Text("Expanded in Row:")->setFontSize(13),
            Container(Row(Text("Fixed")
                              ->setBackgroundColor(RGB(255, 220, 180))
                              ->setPadding(8),
                          Expanded(Container(Text("Expanded fills rest"))
                                       ->setBackgroundColor(RGB(180, 255, 200))
                                       ->setPadding(8)))
                          ->setSpacing(8))
                ->setBackgroundColor(RGB(230, 230, 230))
                ->setWidth(400)
                ->setHeight(40),
            SizedBox(0, 16),

            // 3f. Column setBackgroundColor + setPadding
            Column(Text("Column with bg + padding")->setFontSize(13),
                   Text("Second row")
                       ->setFontSize(12)
                       ->setTextColor(RGB(100, 100, 100)))
                ->setSpacing(4)
                ->setBackgroundColor(RGB(255, 243, 205))
                ->setPadding(12))
            ->setSpacing(0));
  }

  // ------------------------------------------------------------------
  // TAB 4 — Container deep tests
  // ------------------------------------------------------------------
  WidgetPtr buildContainerTab() {
    return Padding(
        24,
        Column(
            Text("Container Tests")
                ->setFontSize(20)
                ->setFontWeight(FontWeight::Bold),
            SizedBox(0, 16),

            // 4a. borderRadius
            Text("borderRadius 4 / 12 / 24:")->setFontSize(13),
            Row(Container()
                    ->setBackgroundColor(RGB(33, 150, 243))
                    ->setWidth(60)
                    ->setHeight(40)
                    ->setBorderRadius(4),
                Container()
                    ->setBackgroundColor(RGB(76, 175, 80))
                    ->setWidth(60)
                    ->setHeight(40)
                    ->setBorderRadius(12),
                Container()
                    ->setBackgroundColor(RGB(244, 67, 54))
                    ->setWidth(60)
                    ->setHeight(40)
                    ->setBorderRadius(24))
                ->setSpacing(12),
            SizedBox(0, 16),

            // 4b. border color + width
            Text("setBorderColor + setBorderWidth:")->setFontSize(13),
            Container(Text("Outlined box")->setPadding(4))
                ->setBorderColor(RGB(33, 150, 243))
                ->setBorderWidth(2)
                ->setBorderRadius(6)
                ->setPadding(12),
            SizedBox(0, 16),

            // 4c. hover background
            Text("Hover me (bg changes):")->setFontSize(13),
            Container(Text("Hover target")->setPadding(4))
                ->setBackgroundColor(RGB(240, 240, 240))
                ->setHoverBackgroundColor(RGB(33, 150, 243))
                ->setBorderRadius(6)
                ->setPadding(12),
            SizedBox(0, 16),

            // 4d. setOnClick
            Container(Text("Click me")
                          ->setTextColor(RGB(255, 255, 255))
                          ->setPadding(4))
                ->setBackgroundColor(RGB(76, 175, 80))
                ->setHoverBackgroundColor(RGB(56, 142, 60))
                ->setBorderRadius(6)
                ->setPadding(12)
                ->setOnClick([]() {
                  MessageBoxA(nullptr, "Container onClick works!", "Test",
                              MB_OK);
                }),
            SizedBox(0, 16),

            // 4e. setMinWidth / setMaxWidth
            Text("maxWidth=200 (even if content wider):")->setFontSize(13),
            Container(Text("This text is quite long but container is capped"))
                ->setBackgroundColor(RGB(255, 243, 205))
                ->setMaxWidth(200)
                ->setBorderRadius(4)
                ->setPadding(8),
            SizedBox(0, 16),

            // 4f. setPaddingAll asymmetric
            Text("setPaddingAll(left=4, top=16, right=4, bottom=16):")
                ->setFontSize(13),
            Container(Text("Asymmetric padding"))
                ->setBackgroundColor(RGB(230, 230, 255))
                ->setPaddingAll(4, 16, 4, 16)
                ->setBorderRadius(4),
            SizedBox(0, 16),

            // 4g. State-bound background
            Text("State-bound bg (toggle to change):")->setFontSize(13),
            Container(Text("Background follows toggleState"))
                ->setBackgroundColor(toggleState, RGB(76, 175, 80),
                                     RGB(244, 67, 54))
                ->setPadding(12)
                ->setBorderRadius(6))
            ->setSpacing(0));
  }

  // ------------------------------------------------------------------
  // TAB 5 — ListView
  // ------------------------------------------------------------------
  WidgetPtr buildListTab() {
    return Padding(
        24,
        Column(Text("ListView Tests")
                   ->setFontSize(20)
                   ->setFontWeight(FontWeight::Bold),
               SizedBox(0, 8),
               Row(Button("+ Add",
                          [this]() {
                            listItems.update([](std::vector<std::string> v) {
                              v.push_back("Item " +
                                          std::to_string(v.size() + 1));
                              return v;
                            });
                          }),
                   Button("Clear", [this]() { listItems.set({}); }))
                   ->setSpacing(8),
               SizedBox(0, 12),

               // ListView inside a fixed Container — hFit fills the 400px
               Container(
                   ListView(listItems)
                       ->itemBuilder([this](int i, const std::string &item) {
                         return Card(
                             Row(Column(Text(item)
                                            ->setFontWeight(FontWeight::Bold)
                                            ->setFontSize(13),
                                        Text("idx " + std::to_string(i))
                                            ->setFontSize(11)
                                            ->setTextColor(RGB(150, 150, 150)))
                                     ->setSpacing(2)
                                     ->setFlex(1),
                                 Button("\xD7",
                                        [this, i]() {
                                          listItems.update(
                                              [i](std::vector<std::string> v) {
                                                if (i < (int)v.size())
                                                  v.erase(v.begin() + i);
                                                return v;
                                              });
                                        }))
                                 ->setSpacing(8)
                                 ->setCrossAlignment(Alignment::Center));
                       })
                       ->separator([]() { return SizedBox(0, 4); })

                       )
                   ->setBackgroundColor(RGB(248, 248, 248))
                   ->setBorderRadius(8)
                   ->setPadding(12)
                   ->setHeight(400)
                   ->setMaxWidth(500))
            ->setSpacing(0));
  }

  // ------------------------------------------------------------------
  // TAB 6 — Misc (Card, Divider, Center, SizedBox, Expanded)
  // ------------------------------------------------------------------
  WidgetPtr buildMiscTab() {
    return Padding(
        24,
        Column(
            Text("Misc Widget Tests")
                ->setFontSize(20)
                ->setFontWeight(FontWeight::Bold),
            SizedBox(0, 16),

            // Card
            Text("Card:")->setFontSize(13),
            Card(Column(Text("Card Title")
                            ->setFontSize(15)
                            ->setFontWeight(FontWeight::Bold),
                        Text("Card subtitle")
                            ->setFontSize(12)
                            ->setTextColor(RGB(120, 120, 120)))
                     ->setSpacing(4)),
            SizedBox(0, 16),

            // Divider
            Text("Divider:")->setFontSize(13), Divider(), SizedBox(0, 16),

            // SizedBox as spacer and as sized container
            Text("SizedBox 200x80 with child:")->setFontSize(13),
            SizedBox(200, 80, Center(Text("inside SizedBox"))), SizedBox(0, 16),

            // Center widget
            Text("Center widget (300x60 box):")->setFontSize(13),
            Container(Center(Text("Centered!")))
                ->setBackgroundColor(RGB(230, 230, 255))
                ->setWidth(300)
                ->setHeight(60),
            SizedBox(0, 16),

            // Expanded inside Column
            Text("Expanded in Column (200px tall column):")->setFontSize(13),
            Container(Column(Text("Fixed top")
                                 ->setBackgroundColor(RGB(255, 220, 180))
                                 ->setPadding(6),
                             Expanded(Container()->setBackgroundColor(
                                 RGB(180, 220, 255))),
                             Text("Fixed bot")
                                 ->setBackgroundColor(RGB(220, 255, 180))
                                 ->setPadding(6))
                          ->setSpacing(4))
                ->setWidth(200)
                ->setHeight(150)
                ->setBackgroundColor(RGB(240, 240, 240)),
            SizedBox(0, 16),

            // Nested padding
            Text("Nested padding:")->setFontSize(13),
            Padding(16, Padding(8, Text("Double-padded")
                                       ->setBackgroundColor(RGB(255, 243, 205))
                                       ->setPadding(4))))
            ->setSpacing(0));
  }

  // ------------------------------------------------------------------
  // TAB BAR + BODY
  // ------------------------------------------------------------------
  WidgetPtr buildTabBar() {
    const char *labels[] = {"Text",      "Button",   "Layout",
                            "Container", "ListView", "Misc"};

    auto row = std::make_shared<RowWidget>();
    for (int idx = 0; idx < 6; idx++) {
      // Each cell is its own Container that reacts to activeTab state
      auto cell = Container(Text(labels[idx])->setFontSize(13))
                      ->setBackgroundColor(activeTab, RGB(232, 244, 253),
                                           RGB(255, 255, 255))
                      ->setPaddingAll(16, 10, 16, 10)
                      ->setOnClick([this, idx]() { activeTab.set(idx); });

      row->addChild(cell);
    }
    return Container(row)
        ->setBackgroundColor(RGB(255, 255, 255))
        ->setBorderColor(RGB(224, 224, 224))
        ->setBorderWidth(1);
  }

  WidgetPtr build() override {
    return Scaffold(
        AppBar("FluxUI Widget Tests"),
        Column(
            Tooltip(Button("Hover me", [] {}), "Click to submit"),
            // Controls bar
            Container(
                Row(Text("Counter: ")->setFontSize(13),
                    Text(counter)->setFontSize(13)->setFontWeight(
                        FontWeight::Bold),
                    SizedBox(16, 0),
                    Button("Inc", [this]() { counter.set(counter.get() + 1); }),
                    Button("Dec", [this]() { counter.set(counter.get() - 1); }),
                    SizedBox(16, 0),
                    Button("Toggle",
                           [this]() { toggleState.set(!toggleState.get()); }))
                    ->setSpacing(8)
                    ->setCrossAlignment(Alignment::Center))
                ->setPadding(12)
                ->setBackgroundColor(RGB(245, 245, 245)),

            buildTabBar(),

            // ✅ Switch drives body — rebuilds automatically on activeTab
            // change
            Expanded(Switch(activeTab)
                         ->Case(0, [this]() { return buildTextTab(); })
                         ->Case(1, [this]() { return buildButtonTab(); })
                         ->Case(2, [this]() { return buildLayoutTab(); })
                         ->Case(3, [this]() { return buildContainerTab(); })
                         ->Case(4, [this]() { return buildListTab(); })
                         ->Case(5, [this]() { return buildMiscTab(); })
                         ->Default([this]() { return buildTextTab(); })))
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