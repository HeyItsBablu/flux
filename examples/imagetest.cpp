#include "flux/flux.hpp"

class MyApp : public Widget {
public:
  WidgetPtr build() override {
    return Scaffold(
        AppBar("ScrollView with Images"),
        Expanded(
            ScrollView(
                {

                    Row({
                            Container(NetworkImage("https://picsum.photos/"
                                                   "seed/fluxui/600/200")
                                          ->setFit(ImageFit::Cover))
                                ->setWidth(80)
                                ->setHeight(80)
                                ->setBorderRadius(8),

                            Column({
                                       Text("Image Title")
                                           ->setFontSize(16)
                                           ->setFontWeight(FontWeight::Bold)
                                           ->setTextColor(
                                               Color::fromRGB(30, 30, 30)),
                                       Text("Some subtitle text here")
                                           ->setFontSize(13)
                                           ->setTextColor(
                                               Color::fromRGB(100, 100, 100)),
                                   })
                                ->setSpacing(4)
                                ->setMainAxisSize(MainAxisSize::Min),
                        })
                        ->setSpacing(12)
                        ->setCrossAxisAlignment(CrossAxisAlignment::Center),

                    Row({
                            Container(AssetImage("screenshots/counter.png")
                                          ->setFit(ImageFit::Cover))
                                ->setWidth(80)
                                ->setHeight(80)
                                ->setBorderRadius(8),

                            Column({
                                       Text("Another Title")
                                           ->setFontSize(16)
                                           ->setFontWeight(FontWeight::Bold)
                                           ->setTextColor(
                                               Color::fromRGB(30, 30, 30)),
                                       Text("Another subtitle here")
                                           ->setFontSize(13)
                                           ->setTextColor(
                                               Color::fromRGB(100, 100, 100)),
                                   })
                                ->setSpacing(4)
                                ->setMainAxisSize(MainAxisSize::Min),
                        })
                        ->setSpacing(12)
                        ->setCrossAxisAlignment(CrossAxisAlignment::Center),
                    Row({
                            Container(AssetImage("screenshots/batman.jpg")
                                          ->setFit(ImageFit::Cover))
                                ->setWidth(80)
                                ->setHeight(80)
                                ->setBorderRadius(8),

                            Column({
                                       Text("Another Title")
                                           ->setFontSize(16)
                                           ->setFontWeight(FontWeight::Bold)
                                           ->setTextColor(
                                               Color::fromRGB(30, 30, 30)),
                                       Text("Another subtitle here")
                                           ->setFontSize(13)
                                           ->setTextColor(
                                               Color::fromRGB(100, 100, 100)),
                                   })
                                ->setSpacing(4)
                                ->setMainAxisSize(MainAxisSize::Min),
                        })
                        ->setSpacing(12)
                        ->setCrossAxisAlignment(CrossAxisAlignment::Center),
                    Row({
                            Container(AssetImage("screenshots/layout.png")
                                          ->setFit(ImageFit::Cover))
                                ->setWidth(80)
                                ->setHeight(80)
                                ->setBorderRadius(8),

                            Column({
                                       Text("Another Title")
                                           ->setFontSize(16)
                                           ->setFontWeight(FontWeight::Bold)
                                           ->setTextColor(
                                               Color::fromRGB(30, 30, 30)),
                                       Text("Another subtitle here")
                                           ->setFontSize(13)
                                           ->setTextColor(
                                               Color::fromRGB(100, 100, 100)),
                                   })
                                ->setSpacing(4)
                                ->setMainAxisSize(MainAxisSize::Min),
                        })
                        ->setSpacing(12)
                        ->setCrossAxisAlignment(CrossAxisAlignment::Center),
                    Row({
                            Container(AssetImage("screenshots/illustrator.png")
                                          ->setFit(ImageFit::Cover))
                                ->setWidth(80)
                                ->setHeight(80)
                                ->setBorderRadius(8),

                            Column({
                                       Text("Another Title")
                                           ->setFontSize(16)
                                           ->setFontWeight(FontWeight::Bold)
                                           ->setTextColor(
                                               Color::fromRGB(30, 30, 30)),
                                       Text("Another subtitle here")
                                           ->setFontSize(13)
                                           ->setTextColor(
                                               Color::fromRGB(100, 100, 100)),
                                   })
                                ->setSpacing(4)
                                ->setMainAxisSize(MainAxisSize::Min),
                        })
                        ->setSpacing(12)
                        ->setCrossAxisAlignment(CrossAxisAlignment::Center),
                    Row({
                            Container(AssetImage("screenshots/logic_sim.png")
                                          ->setFit(ImageFit::Cover))
                                ->setWidth(80)
                                ->setHeight(80)
                                ->setBorderRadius(8),

                            Column({
                                       Text("Another Title")
                                           ->setFontSize(16)
                                           ->setFontWeight(FontWeight::Bold)
                                           ->setTextColor(
                                               Color::fromRGB(30, 30, 30)),
                                       Text("Another subtitle here")
                                           ->setFontSize(13)
                                           ->setTextColor(
                                               Color::fromRGB(100, 100, 100)),
                                   })
                                ->setSpacing(4)
                                ->setMainAxisSize(MainAxisSize::Min),
                        })
                        ->setSpacing(12)
                        ->setCrossAxisAlignment(CrossAxisAlignment::Center),
                    Row({
                            Container(AssetImage("screenshots/graph.png")
                                          ->setFit(ImageFit::Cover))
                                ->setWidth(80)
                                ->setHeight(80)
                                ->setBorderRadius(8),

                            Column({
                                       Text("Another Title")
                                           ->setFontSize(16)
                                           ->setFontWeight(FontWeight::Bold)
                                           ->setTextColor(
                                               Color::fromRGB(30, 30, 30)),
                                       Text("Another subtitle here")
                                           ->setFontSize(13)
                                           ->setTextColor(
                                               Color::fromRGB(100, 100, 100)),
                                   })
                                ->setSpacing(4)
                                ->setMainAxisSize(MainAxisSize::Min),
                        })
                        ->setSpacing(12)
                        ->setCrossAxisAlignment(CrossAxisAlignment::Center),

                })
                ->setSpacing(12)
                ->setPadding(16)),
        nullptr, nullptr);
  }
};

WidgetPtr createApp(FluxUI *app) {
  return FluxApp("Flux - ScrollView Images", std::make_shared<MyApp>(),
                 AppTheme::light(), false, 600, 400, false, false);
}