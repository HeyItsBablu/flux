#include "flux/flux.hpp"

#include <cmath>

class SystemMonitorApp : public Component {
private:
  State<std::vector<float>> cpuData;
  State<std::vector<float>> memData;
  State<std::vector<float>> networkData;
  State<int> elapsed;
  State<std::string> statusText;

  UINT_PTR timerId = 0;
  static constexpr int MAX_POINTS = 60;
  float time = 0.0f;

public:
  SystemMonitorApp()
      : cpuData({}, context), memData({}, context), networkData({}, context),
        elapsed(0, context), statusText("Running...", context) {}

  void initState() override {
    // Seed with initial flat data
    cpuData.set(std::vector<float>(MAX_POINTS, 0.f));
    memData.set(std::vector<float>(MAX_POINTS, 0.f));
    networkData.set(std::vector<float>(MAX_POINTS, 0.f));

    timerId = context->setInterval(1000, [this]() {
      time += 1.0f;
      elapsed.set(elapsed.get() + 1);

      // Simulate CPU — sine wave + noise
      pushValue(cpuData, 30.f + 20.f * std::sin(time * 0.3f) +
                             5.f * std::sin(time * 1.7f));

      // Simulate Memory — slow climb
      pushValue(memData, 40.f + 15.f * std::sin(time * 0.1f) +
                             3.f * std::cos(time * 0.5f));

      // Simulate Network — spiky
      float spike = (std::fmod(time, 7.f) < 1.f) ? 60.f : 0.f;
      pushValue(networkData,
                10.f + 8.f * std::abs(std::sin(time * 0.8f)) + spike);

      // Update status
      std::string s = "Uptime: " + std::to_string(elapsed.get()) + "s";
      statusText.set(s);
    });
  }

  void dispose() override {
    if (timerId) {
      context->clearInterval(timerId);
      timerId = 0;
    }
    Component::dispose();
  }

  WidgetPtr build() override {
    return Scaffold(
        AppBar("System Monitor"),
        Center(
            Column({

                       // ── Status bar ──────────────────────────────────────
                       Row({Text("● LIVE")
                                ->setTextColor(Color::fromRGB(76, 175, 80))
                                ->setFontSize(12),
                            Text(statusText)
                                ->setTextColor(Color::fromRGB(150, 150, 150))
                                ->setFontSize(12)})
                           ->setSpacing(12),

                       SizedBox(0, 16),

                       // ── CPU graph ───────────────────────────────────────
                       Text("CPU Usage %")
                           ->setTextColor(Color::fromRGB(200, 200, 200))
                           ->setFontSize(13),
                       SizedBox(0, 6),
                       Graph(700, 200)
                           ->addSeries("CPU", cpuData, 1.f, 0.45f, 0.2f)
                           ->addSeries("Memory", memData, 0.2f, 0.7f, 1.f)
                           ->setType(GraphType::Area)
                           ->setYRange(0.f, 100.f)
                           ->setShowGrid(true),

                       SizedBox(0, 20),

                       // ── Memory graph ────────────────────────────────────
                       Text("Memory Usage %")
                           ->setTextColor(Color::fromRGB(200, 200, 200))
                           ->setFontSize(13),
                       SizedBox(0, 6),
                       Graph(700, 200)
                           ->addSeries("Memory", memData, 0.2f, 0.7f, 1.f)
                           ->setType(GraphType::Area)
                           ->setYRange(0.f, 100.f)
                           ->setShowGrid(true),

                       SizedBox(0, 20),

                       // ── Network — multi-series bar chart ────────────────
                       Text("Network MB/s")
                           ->setTextColor(Color::fromRGB(200, 200, 200))
                           ->setFontSize(13),
                       SizedBox(0, 6),
                       Graph(700, 200)
                           ->addSeries("Network", networkData, 0.4f, 1.f, 0.6f)
                           ->setType(GraphType::Bar)
                           ->setYRange(0.f, 100.f)
                           ->setShowGrid(true)

                   })
                ->setSpacing(0)));
  }

private:
  void pushValue(State<std::vector<float>> &state, float val) {
    auto data = state.get();
    data.push_back(std::clamp(val, 0.f, 100.f));
    if ((int)data.size() > MAX_POINTS)
      data.erase(data.begin());
    state.set(data);
  }
};



WidgetPtr createApp(FluxUI* app) {
    return FluxApp(
        "FluxUI - Paint",
        BuildComponent<SystemMonitorApp>(),
        AppTheme::dark(),
        false,   // debugShowWidgetBounds
        900,     // width
        700,     // height
        false,   // maximize
        true     // fullscreen
    );
}