#include "flux/flux.hpp"


class MyApp : public Widget {
  State<FluxDate> checkin{FluxDate{}};
  State<FluxDate> checkout{FluxDate{}};

public:


  WidgetPtr build() override {
    return Scaffold(
        AppBar("DatePicker Demo"),
        Expanded(Center(
            Card(
                Column({

                    Text("Book a Stay")
                        ->setFontSize(20)
                        ->setFontWeight(FontWeight::Bold),

                    SizedBox(0, 20),

                    // ── Check-in ──────────────────────────────────────────
                    Text("Check-in")
                        ->setFontSize(12)
                        ->setTextColor(Color::fromRGB(100, 100, 100)),
                    SizedBox(0, 4),
                    DatePicker()
                        ->setDate(checkin)
                        ->setPlaceholder("Select check-in date")
                        ->setOnDateChanged([this](FluxDate d) {
                            checkin.set(d);
                        }),

                    SizedBox(0, 16),

                    // ── Check-out ─────────────────────────────────────────
                    Text("Check-out")
                        ->setFontSize(12)
                        ->setTextColor(Color::fromRGB(100, 100, 100)),
                    SizedBox(0, 4),
                    DatePicker()
                        ->setDate(checkout)
                        ->setPlaceholder("Select check-out date")
                        ->setOnDateChanged([this](FluxDate d) {
                            checkout.set(d);
                        }),

                    SizedBox(0, 20),

                    // ── Summary ───────────────────────────────────────────
                    Text(checkin, [](FluxDate d) {
                        return d.isValid()
                            ? "Check-in:  " + d.toString("%d %b %Y")
                            : "Check-in:  —";
                    })->setFontSize(13),

                    SizedBox(0, 4),

                    Text(checkout, [](FluxDate d) {
                        return d.isValid()
                            ? "Check-out: " + d.toString("%d %b %Y")
                            : "Check-out: —";
                    })->setFontSize(13),

                })
                ->setSpacing(0)
            )
            ->setWidth(340)
        )),nullptr,nullptr
    );
  }
};



WidgetPtr createApp(FluxUI* app) {
    return FluxApp(
        "FluxUI - Paint",
        std::make_shared<MyApp>(),
        AppTheme::light(),
        false,   // debugShowWidgetBounds
        900,     // width
        700,     // height
        false,   // maximize
        true     // fullscreen
    );
}