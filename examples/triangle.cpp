#include "flux/flux.hpp"

class TriangleSurface : public RenderSurface {
    float time_ = 0;

public:
    void initialize(int, int) override {}
    void resize(int, int)     override {}
    void destroy()            override {}
    void update(double dt)    override { time_ += float(dt); }

    void render(Canvas2D& ctx) override {
        float w  = float(ctx.width());
        float h  = float(ctx.height());
        float cx = w * 0.5f;
        float cy = h * 0.5f;
        float r  = std::min(w, h) * 0.4f;

        ctx.setFillColor(Color::fromRGB(15, 15, 20));
        ctx.fillRect(0, 0, w, h);

        float hue = time_ * 30.f;
        ctx.beginLinearGradient(cx, cy - r, cx, cy + r);
        ctx.addColorStop(0.f, Color::fromHSV(hue,         0.8f, 1.f));
        ctx.addColorStop(1.f, Color::fromHSV(hue + 120.f, 0.8f, 0.6f));
        ctx.setFillGradient();

        ctx.beginPath();
        ctx.moveTo(cx,               cy - r);
        ctx.lineTo(cx - r * 0.866f,  cy + r * 0.5f);
        ctx.lineTo(cx + r * 0.866f,  cy + r * 0.5f);
        ctx.closePath();
        ctx.fill();
    }

    bool needsContinuousRedraw() const override { return true; }
};
// ============================================================================
// App
// ============================================================================

class TriangleApp : public Widget {
  static constexpr int kCanvasW = 512, kCanvasH = 512;

public:
  WidgetPtr build() override {
    auto canvas = std::make_shared<CanvasWidget>();
    canvas->setSize(kCanvasW, kCanvasH);
    canvas->setCanvasSize(kCanvasW, kCanvasH);
    canvas->setScrollbarsEnabled(false);
    canvas->setSurface<TriangleSurface>();

   return Scaffold(nullptr, Center(canvas));
  }
};

// ============================================================================
// Entry point
// ============================================================================

WidgetPtr createApp(FluxUI *app) {
  return FluxApp("Triangle", std::make_shared<TriangleApp>(), AppTheme::dark(),
                 false,  // debugShowWidgetBounds
                 512,    // width
                 512,    // height
                 false,  // maximize
                 false); // fullscreen
}