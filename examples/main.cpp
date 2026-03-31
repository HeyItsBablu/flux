#include "flux/flux.hpp"
#include <numeric>

class LayoutStressTest : public Component {
  State<int> boxCount;
  State<std::vector<int>> boxIndices;

public:
  LayoutStressTest() : boxCount(9, context), boxIndices({}, context) {}

  WidgetPtr build() override {
    return Scaffold(
        AppBar("Image Examples"),
        Center(
            Column(
                {
                    // ── Title //──────────────────────────────────────────────
                    Text("Image Widget Examples")
                        ->setFontSize(20)
                        ->setFontWeight(FontWeight::Bold)
                        ->setTextColor(Color::fromHex(0x1a1a1aff)),

                    // ── Fit modes row //──────────────────────────────────────
                    Text("Fit Modes")
                        ->setFontSize(14)
                        ->setTextColor(Color::fromHex(0x666666ff)),

                    Row({
                            // Fill — stretches, may distort
                            Column({
                                       Image("screenshots/682809.jpg")
                                           ->setWidth(140)
                                           ->setHeight(100)
                                           ->setFit(ImageFit::Fill)
                                           ->setBorderRadius(6),
                                       Text("Fill")
                                           ->setTextColor(
                                               Color::fromRGB(100, 100, 100))
                                           ->setFontSize(12),
                                   })
                                ->setSpacing(4),

                            // Contain — fits inside, may have empty space
                            Column({
                                       Image("screenshots/682809.jpg")
                                           ->setWidth(140)
                                           ->setHeight(100)
                                           ->setFit(ImageFit::Contain)
                                           ->setBorderRadius(6),
                                       Text("Contain")
                                           ->setTextColor(
                                               Color::fromRGB(100, 100, 100))
                                           ->setFontSize(12),
                                   })
                                ->setSpacing(4),

                            // Cover — fills area, may crop
                            Column({
                                       Image("screenshots/682809.jpg")
                                           ->setWidth(140)
                                           ->setHeight(100)
                                           ->setFit(ImageFit::Cover)
                                           ->setBorderRadius(6),
                                       Text("Cover")
                                           ->setTextColor(
                                               Color::fromRGB(100, 100, 100))
                                           ->setFontSize(12),
                                   })
                                ->setSpacing(4),
                        })
                        ->setSpacing(16),

                    // ── Avatar / circle
                    //────────────────────────────────────
                    Text("Circle Avatar")
                        ->setFontSize(14)
                        ->setTextColor(Color::fromHex(0x666666ff)),

                    Row({
                        Image("screenshots/counter.png")
                            ->setWidth(64)
                            ->setHeight(64)
                            ->setFit(ImageFit::Cover)
                            ->setBorderRadius(32),   // full circle

                        Image("screenshots/counter.png")
                            ->setWidth(96)
                            ->setHeight(96)
                            ->setFit(ImageFit::Cover)
                            ->setBorderRadius(48),

                        Image("screenshots/counter.png")
                            ->setWidth(128)
                            ->setHeight(128)
                            ->setFit(ImageFit::Cover)
                            ->setBorderRadius(64),
                    })->setSpacing(16),

                    // ── Card with image
                    //────────────────────────────────────
                    Text("Image Card")
                        ->setFontSize(14)
                        ->setTextColor(Color::fromHex(0x666666ff)),

                    Container(
                        Column({
                            Image("screenshots/layout.png")
                                ->setWidth(320)
                                ->setHeight(180)
                                ->setFit(ImageFit::Cover)
                                ->setBorderRadius(8),

                            Text("Mountain Landscape")
                                ->setFontSize(16)
                                ->setFontWeight(FontWeight::Bold)
                                ->setTextColor(Color::fromRGB(30, 30, 30)),

                            Text("A beautiful view from the summit.")
                                ->setFontSize(13)
                                ->setTextColor(Color::fromRGB(100, 100,
                                100)),
                        })->setSpacing(8)
                    )
                    ->setPadding(12)
                    ->setBackgroundColor(Color::fromRGB(255, 255, 255))
                    ->setBorderRadius(12),

                    // ── Error / placeholder states
                    //─────────────────────────
                    Text("Error & Placeholder States")
                        ->setFontSize(14)
                        ->setTextColor(Color::fromHex(0x666666ff)),

                    Row({
                        // Placeholder — no path set
                        Column({
                            Image()
                                ->setWidth(120)
                                ->setHeight(90)
                                ->setBorderRadius(6),
                            Text("No image")
                                ->setFontSize(12)
                                ->setTextColor(Color::fromRGB(100, 100,
                                100)),
                        })->setSpacing(4),

                        // Error — bad path
                        Column({
                            Image("screenshots/does_not_exist.jpg")
                                ->setWidth(120)
                                ->setHeight(90)
                                ->setErrorColor(Color::fromRGB(255, 220,
                                220))
                                ->setBorderRadius(6),
                            Text("Bad path")
                                ->setFontSize(12)
                                ->setTextColor(Color::fromRGB(100, 100,
                                100)),
                        })->setSpacing(4),
                    })->setSpacing(16),
                })
                ->setSpacing(20)
                ->setPadding(24)));
  }
};

WidgetPtr createApp(FluxUI *app) {
  return FluxApp("Image Examples", BuildComponent<LayoutStressTest>(),
                 AppTheme::light());
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    AllocConsole();
    FILE *f;
    freopen_s(&f, "CONOUT$", "w", stdout);

    FluxUI app(hInstance);
    app.createWindow("FluxUI - Image Examples", 900, 800); // window first
    app.build([&]() { return createApp(&app); });          // build second
    return app.run();
}