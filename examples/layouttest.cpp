#include "flux/flux.hpp"

class MyApp : public Widget {
public:
    WidgetPtr build() override {
        return Scaffold(
            AppBar("Centered Title"),
            Column({

                Container(
                    Text("Column fills full width")
                        ->setFontSize(16)
                        ->setTextColor(Color::fromRGB(255, 255, 255))
                )
                ->setBackgroundColor(Color::fromRGB(33, 150, 243))
                ->setPadding(12),

                Expanded(
                    Row({

                        Expanded(
                            Container(
                                Text("Red")
                                    ->setFontSize(14)
                                    ->setTextColor(Color::fromRGB(255, 255, 255))
                            )
                            ->setBackgroundColor(Color::fromRGB(220, 80, 80))
                            ->setPadding(8)
                        ),
                        Expanded(
                            Container(
                                Text("Green")
                                    ->setFontSize(14)
                                    ->setTextColor(Color::fromRGB(255, 255, 255))
                            )
                            ->setBackgroundColor(Color::fromRGB(80, 180, 80))
                            ->setPadding(8)
                        ),
                        Expanded(
                            Container(
                                Text("Blue")
                                    ->setFontSize(14)
                                    ->setTextColor(Color::fromRGB(255, 255, 255))
                            )
                            ->setBackgroundColor(Color::fromRGB(80, 80, 220))
                            ->setPadding(8)
                        ),
                    })
                    ->setCrossAxisAlignment(CrossAxisAlignment::Stretch)
                ),
            })
            ->setCrossAxisAlignment(CrossAxisAlignment::Stretch)  
        );
    }
};

WidgetPtr createApp(FluxUI *app) {
    return FluxApp(
        "Flux - Fix Verification",
        std::make_shared<MyApp>(),
        AppTheme::light(),
        false, 600, 400, false, false
    );
}