// dev/lib/main.cpp
//
// createApp() — the one function every flux platform main.cpp forward-
// declares and calls. This is the whole "app" for the charts dev sandbox:
// a simple screen exercising FluxPieChartWidget in both pie and donut mode,
// so chart rendering/hit-testing can actually be seen and clicked rather
// than reasoned about from headers alone.

#include "flux/flux.hpp"
#include "flux_charts/flux_charts.hpp"

#include <cstdio>

class ChartsDevApp : public Widget
{
public:
  WidgetPtr build() override
  {
    auto pie = PieChart(360, 320)
                   ->setTitle("Browser Share")
                   ->addSlice("Chrome", 65, Color::fromRGB(66, 133, 244))
                   ->addSlice("Safari", 18, Color::fromRGB(255, 149, 0))
                   ->addSlice("Firefox", 9, Color::fromRGB(255, 90, 95))
                   ->addSlice("Edge", 5, Color::fromRGB(0, 178, 148))
                   ->addSlice("Other", 3, Color::fromRGB(140, 140, 150))
                   ->setOnSliceClick([](int idx)
                                     { std::printf("[pie] slice %d clicked\n", idx); });

    auto donut = DonutChart(360, 320, 0.6f)
                     ->setTitle("Storage Used")
                     ->addSlice("Photos", 42, Color::fromRGB(255, 193, 7))
                     ->addSlice("Videos", 30, Color::fromRGB(33, 150, 243))
                     ->addSlice("Docs", 12, Color::fromRGB(76, 175, 80))
                     ->addSlice("Free", 16, Color::fromRGB(60, 60, 70));
    auto candles = CandlestickChart(420, 320)
                       ->setTitle("Price Action")
                       ->addCandle("Mon", 100, 108, 97, 105)
                       ->addCandle("Tue", 105, 110, 103, 102)
                       ->addCandle("Wed", 102, 106, 99, 104)
                       ->addCandle("Thu", 104, 112, 103, 111)
                       ->addCandle("Fri", 111, 113, 106, 108);

    return Box({
                   Text("FluxCharts Dev Sandbox")
                       ->setFontSize(20)
                       ->setPadding(4),
                   Box({pie, donut, candles})
                       ->setDisplay(Display::Flex)
                       ->setDirection(FlexDirection::Row)
                       ->setGap(16),
               })
        ->setDisplay(Display::Flex)
        ->setDirection(FlexDirection::Column)
        ->setGap(12)
        ->setPadding(16);
  }
};

WidgetPtr createApp(FluxUI * /*app*/)
{
  return FluxApp().setTheme(AppTheme::dark()).build(std::make_shared<ChartsDevApp>());
}