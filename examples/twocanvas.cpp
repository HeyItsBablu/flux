#include "flux/flux.hpp"

class TextSurface : public RenderSurface {
    float time_ = 0;

public:
    void initialize(int, int) override {}
    void resize(int, int)     override {}
    void destroy()            override {}
    void update(double dt)    override { time_ += float(dt); }

void render(Canvas2D& ctx) override {
    std::cout << "render called w=" << ctx.width() << " h=" << ctx.height() << std::endl;
    float w = float(ctx.width());
    float h = float(ctx.height());

    ctx.setFillColor(Color::fromRGB(20, 20, 30));
    ctx.fillRect(0, 0, w, h);

    // Make sure this is NOT commented out
    ctx.setFont("18px sans");
    ctx.setTextAlign(CanvasTextAlign::Left);
    ctx.setTextBaseline(TextBaseline::Top);
    ctx.setFillColor(Color::fromRGB(255, 255, 255));
    ctx.fillText("TEST", 50.f, 50.f);  
}
    bool needsContinuousRedraw() const override { return true; }
};

// ============================================================================
// App
// ============================================================================

class TextApp : public Widget {
public:
    WidgetPtr build() override {
        auto canvas = std::make_shared<CanvasWidget>();

        canvas->setScrollbarsEnabled(false);
        canvas->setSurface<TextSurface>();

        return Scaffold(AppBar("Text Demo"), Expanded(canvas), nullptr, nullptr);
    }
};

// ============================================================================
// Entry point
// ============================================================================

WidgetPtr createApp(FluxUI* app) {
    return FluxApp("Text Demo", std::make_shared<TextApp>(), AppTheme::dark(),
                   false, 600, 480, false, false);
}