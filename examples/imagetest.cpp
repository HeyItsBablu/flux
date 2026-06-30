#include "flux/flux.hpp"

class MyApp : public Widget
{
public:
    WidgetPtr build() override
    {
        return Flex(
                   {
                       // ── 1. Cover fit, small radius ──────────────────────────
                       _row(
                           Image("https://picsum.photos/seed/fluxui/600/200")
                               ->setFit(ImageFit::Cover)
                               ->setBorderRadius(8),
                           "Cover Fit",
                           "borderRadius(8)"),

                       // ── 2. Contain fit, large radius, tint overlay ──────────
                       _row(
                           Image("https://picsum.photos/seed/fluxtint/600/200")
                               ->setFit(ImageFit::Contain)
                               ->setBorderRadius(24)
                               ->setTintColor(Color::fromRGBA(0, 100, 200, 80)),
                           "Contain + Tint",
                           "borderRadius(24), tintColor(blue@80)"),

                       // ── 3. Fully circular (radius = half of size) ───────────
                       _row(
                           Image("https://picsum.photos/seed/fluxcircle/200/200")
                               ->setFit(ImageFit::Cover)
                               ->setBorderRadius(40),
                           "Circular",
                           "borderRadius(40) == width/2"),

                       // ── 4. No radius, ScaleDown fit, BottomRight alignment ──
                       _row(
                           Image("https://picsum.photos/seed/fluxscale/120/120")
                               ->setFit(ImageFit::ScaleDown)
                               ->setImageAlignment(Alignment::BottomRight)
                               ->setBorderRadius(0)
                               ->setPlaceholderColor(Color::fromRGB(220, 220, 250)),
                           "ScaleDown + BottomRight",
                           "borderRadius(0), alignment(BottomRight)"),

                       // ── 5. High filter quality, custom padding ──────────────
                       _row(
                           Image("https://picsum.photos/seed/fluxquality/600/200")
                               ->setFit(ImageFit::Cover)
                               ->setFilterQuality(FilterQuality::High)
                               ->setBorderRadius(12)
                               ->setPadding(4),
                           "High Filter Quality",
                           "filterQuality(High), padding(4)"),

                       // ── 6. Asset image, custom error color (path won't ──────
                       //      resolve, so this is also an Error-state test)
                       _row(
                           Image("nonexistent_asset.png")
                               ->setFit(ImageFit::Cover)
                               ->setBorderRadius(8)
                               ->setErrorColor(Color::fromRGB(255, 220, 220)),
                           "Error State Test",
                           "missing asset -> errorColor"),

                       // ── 7. Custom loadingBuilder + errorBuilder ─────────────
                       _row(
                           Image("https://picsum.photos/seed/fluxbuilder/600/200")
                               ->setFit(ImageFit::Cover)
                               ->setBorderRadius(8)
                               ->setLoadingBuilder([]() -> WidgetPtr {
                                   return Text("Loading...")
                                       ->setFontSize(12)
                                       ->setTextColor(Color::fromRGB(150, 150, 150));
                               })
                               ->setErrorBuilder([]() -> WidgetPtr {
                                   return Text("Failed to load")
                                       ->setFontSize(12)
                                       ->setTextColor(Color::fromRGB(200, 50, 50));
                               }),
                           "Custom Builders",
                           "loadingBuilder + errorBuilder"),

                       // ── 8. Repeat / tiling ───────────────────────────────────
                       _row(
                           Image("https://picsum.photos/seed/fluxtile/40/40")
                               ->setFit(ImageFit::None)
                               ->setRepeat(ImageRepeat::Repeat)
                               ->setBorderRadius(8),
                           "Tiled (Repeat)",
                           "fit(None), repeat(Repeat)"),

                       // ── 9. Fixed width/height overriding auto sizing ────────
                       _row(
                           Image("https://picsum.photos/seed/fluxfixed/600/200")
                               ->setFit(ImageFit::Cover)
                               ->setWidth(120)
                               ->setHeight(60)
                               ->setBorderRadius(16),
                           "Fixed 120x60",
                           "setWidth(120), setHeight(60)"),

                       // ── 10. In-memory bytes (placeholder until you load some) ─
                       _row(
                           Image()
                               ->setPlaceholderColor(Color::fromRGB(240, 240, 245))
                               ->setBorderRadius(8),
                           "Empty/Placeholder",
                           "Image() with no source yet"),
                   })
            ->setScrollable(true)
            ->setDirection(FlexDirection::Column)
            ->setGap(16)
            ->setPadding(16)
            ->setAlignItems(AlignItems::Stretch)
            ->setWidthMode(SizeMode::Full)
            ->setHeightMode(SizeMode::Full);
    }

private:
    // Shared row layout: thumbnail + title/subtitle describing what's being tested
    static WidgetPtr _row(WidgetPtr image, const std::string &title,
                          const std::string &subtitle)
    {
        return Flex({
                   Flex({image})
                       ->setWidth(180)
                       ->setHeight(180),

                   Flex({
                            Text(title)
                                ->setFontSize(16)
                                ->setFontWeight(FontWeight::Bold)
                                ->setTextColor(Color::fromRGB(30, 30, 30)),
                            Text(subtitle)
                                ->setFontSize(13)
                                ->setTextColor(Color::fromRGB(100, 100, 100)),
                        })
                       ->setDirection(FlexDirection::Column),
               })
            ->setGap(12);
    }
};

WidgetPtr createApp(FluxUI *app)
{
    return FluxApp("CRUD App")
        .setTheme(AppTheme::light())
        .setFullscreenMode(true)
        .build(std::make_shared<MyApp>());
}