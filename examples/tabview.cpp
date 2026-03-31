#include "flux/flux.hpp"


class TabViewExample : public Component {
  State<int> activeTab;

public:
  TabViewExample() : activeTab(0, context) {}

  WidgetPtr build() override {
    return Scaffold(
        AppBar("TabView Demo"),

        Column({

            Row({
                Text("Jump to:")
                    ->setFontSize(12)
                    ->setTextColor(Color::fromRGB(100, 100, 100)),
                Button("General",  [this]{ activeTab.set(0); }),
                Button("Display",  [this]{ activeTab.set(1); }),
                Button("Network",  [this]{ activeTab.set(2); }),
                Button("Advanced", [this]{ activeTab.set(3); }),
            })
            ->setSpacing(8)
            ->setPadding(12),

            Expanded(
                TabView({

                    Tab("General",
                        Column({
                            Text("General Settings")
                                ->setFontSize(16)
                                ->setFontWeight(FontWeight::Bold),
                            SizedBox(0, 12),
                            Text("Language: English")->setFontSize(13),
                            SizedBox(0, 8),
                            Text("Theme: Light")->setFontSize(13),
                            SizedBox(0, 8),
                            Text("Auto-save: On")->setFontSize(13),
                        })
                        ->setSpacing(0)
                        ->setPadding(20)
                    ),

                    Tab("Display",
                        Column({
                            Text("Display Settings")
                                ->setFontSize(16)
                                ->setFontWeight(FontWeight::Bold),
                            SizedBox(0, 12),
                            Text("Resolution: 1920 x 1080")->setFontSize(13),
                            SizedBox(0, 8),
                            Text("Refresh rate: 60 Hz")->setFontSize(13),
                            SizedBox(0, 8),
                            Text("Scale: 100%")->setFontSize(13),
                        })
                        ->setSpacing(0)
                        ->setPadding(20)
                    ),

                    Tab("Network",
                        Column({
                            Text("Network Settings")
                                ->setFontSize(16)
                                ->setFontWeight(FontWeight::Bold),
                            SizedBox(0, 12),
                            Text("Status: Connected")
                                ->setFontSize(13)
                                ->setTextColor(Color::fromRGB(67, 160, 71)),
                            SizedBox(0, 8),
                            Text("IP: 192.168.1.42")->setFontSize(13),
                            SizedBox(0, 8),
                            Text("DNS: 8.8.8.8")->setFontSize(13),
                        })
                        ->setSpacing(0)
                        ->setPadding(20)
                    ),

                    Tab("Advanced",
                        Column({
                            Text("Advanced Settings")
                                ->setFontSize(16)
                                ->setFontWeight(FontWeight::Bold),
                            SizedBox(0, 12),
                            Text("Hardware acceleration: On")->setFontSize(13),
                            SizedBox(0, 8),
                            Text("Cache size: 512 MB")->setFontSize(13),
                            SizedBox(0, 8),
                            Text("Log level: Warning")->setFontSize(13),
                        })
                        ->setSpacing(0)
                        ->setPadding(20)
                    ),

                })
                ->setActiveIndex(activeTab)
                ->setOnTabChanged([this](int i) { activeTab.set(i); })
            )

        })
        ->setSpacing(0)
    );
  }
};

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
  FluxUI app(hInstance);
  app.build([&]() {
    return FluxApp("TabView Demo",
                   BuildComponent<TabViewExample>(),
                   AppTheme::light());
  });
  app.createWindow("FluxUI - TabView Demo", 800, 520);
  return app.run();
}