#include "flux/flux.hpp"

class MyApp : public Widget
{
public:
    WidgetPtr build() override
    {
        return Flex(
                   {

                       Flex({
                                Flex({NetworkImage("https://picsum.photos/"
                                                   "seed/fluxui/600/200")
                                          ->setFit(ImageFit::Cover)})
                                    ->setWidth(80)
                                    ->setHeight(80)
                                    ->setBorderRadius(8),

                                Flex({
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
                                    ->setDirection(FlexDirection::Column),
                            })
                           ->setGap(12),

                          Flex({
                                   Flex({AssetImage("counter.png")
                                             ->setFit(ImageFit::Cover)})
                                       ->setWidth(80)
                                       ->setHeight(80)
                                       ->setBorderRadius(8),

                                   Flex({
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
                                       ->setDirection(FlexDirection::Column),
                               })
                              ->setGap(12),
                       //    Flex({
                       //             Flex({AssetImage("assets/batman.jpg")
                       //                       ->setFit(ImageFit::Cover)})
                       //                 ->setWidth(80)
                       //                 ->setHeight(80)
                       //                 ->setBorderRadius(8),

                       //             Flex({
                       //                      Text("Another Title")
                       //                          ->setFontSize(16)
                       //                          ->setFontWeight(FontWeight::Bold)
                       //                          ->setTextColor(
                       //                              Color::fromRGB(30, 30, 30)),
                       //                      Text("Another subtitle here")
                       //                          ->setFontSize(13)
                       //                          ->setTextColor(
                       //                              Color::fromRGB(100, 100, 100)),
                       //                  })
                       //                 ->setDirection(FlexDirection::Column),
                       //         })
                       //        ->setGap(12),

                   })
            ->setScrollable(true)
            ->setDirection(FlexDirection::Column) // base (mobile): stacked
            ->setGap(8)
            ->setPadding(16)
            ->setAlignItems(AlignItems::Stretch)
            ->setWidthMode(SizeMode::Full)
            ->setHeightMode(SizeMode::Full);
    }
};

WidgetPtr createApp(FluxUI *app)
{
    return FluxApp("CRUD App")
        .setTheme(AppTheme::light())
        .setFullscreenMode(true)
        .build(std::make_shared<MyApp>());
}