#include "flux/flux.hpp"

class TriangleSurface : public RenderSurface
{
    float time_ = 0;

public:
    void initialize(int, int) override {}
    void resize(int, int) override {}
    void destroy() override {}
    void update(double dt) override { time_ += float(dt); }

    void render(Canvas2D &ctx) override
    {
        ctx.setFillColor({15, 15, 20, 255});
        ctx.fillRect(0, 0, ctx.width(), ctx.height());

        ctx.setFillColor({255, 80, 80, 255}); // solid red — no HSV/gradient
        ctx.beginPath();
        float cx = ctx.width() * 0.5f;
        float cy = ctx.height() * 0.5f;
        float r = std::min(ctx.width(), ctx.height()) * 0.4f;
        ctx.moveTo(cx, cy - r);
        ctx.lineTo(cx - r * 0.866f, cy + r * 0.5f);
        ctx.lineTo(cx + r * 0.866f, cy + r * 0.5f);
        ctx.closePath();
        ctx.fill();
    }

    bool needsContinuousRedraw() const override { return true; }
};
// ============================================================================
// App
// ============================================================================

class TriangleApp : public Widget
{
    static constexpr int kCanvasW = 512, kCanvasH = 512;

public:
    WidgetPtr build() override
    {
        auto canvas = std::make_shared<CanvasWidget>();
        canvas->setScrollbarsEnabled(false)
            ->setViewportEnabled(false)
            ->setFlexGrow(1)
            ->setSurface<TriangleSurface>();

        auto appBar =
            Box({
                    Text("My App")
                        ->setFontSize(20)
                        ->setFontWeight(FontWeight::Bold)
                        ->setTextColor(Color::fromRGB(255, 255, 255)),
                })
                ->setDisplay(Display::Flex)
                ->setDirection(FlexDirection::Row)
                ->setAlignItems(AlignItems::Center)
                ->setJustifyContent(JustifyContent::Start)
                ->setPaddingHV(16, 0)
                ->setWidthMode(SizeMode::Full)
                ->setHeightMode(SizeMode::Fixed)
                ->setHeight(56)
                ->setBackgroundColor(Color::fromRGB(33, 150, 243));

        return Box({
                       appBar,
                       canvas,
                       
                   })
            ->setDisplay(Display::Flex)
            ->setDirection(FlexDirection::Column)
            ->setWidthMode(SizeMode::Full)
            ->setHeightMode(SizeMode::Full)
            ->setBackgroundColor(Color::fromRGB(245, 245, 250));
    }
};

// ============================================================
//  Entry point
// ============================================================

WidgetPtr createApp(FluxUI *app)
{
    return FluxApp()
        .setTheme(AppTheme::light())
        .build(std::make_shared<TriangleApp>());
}