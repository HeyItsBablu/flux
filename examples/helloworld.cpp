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

class MyApp : public Widget
{

  State<int> counter{0};

public:
  WidgetPtr build() override
  {

    auto canvas = std::make_shared<CanvasWidget>();
    canvas->setScrollbarsEnabled(false);
    canvas->setViewportEnabled(false); // disable pan/zoom too if not needed
    canvas->setSurface<TriangleSurface>();
    return Scaffold(
        AppBar("Flux App"),
        Expanded(Center(
            Container(
                Column({Text(counter), IconButton(FluxIcons::Delete, [this]
                                                  { std::cout << "Icon button clicked" << std::endl; }),
                        Dropdown({"Apple", "Banana", "Cherry",
                                  "Date", "Elderberry", "Fig",
                                  "Grape", "Honeydew"})

                            ->setPlaceholder("Pick a fruit...")
                            ->setMaxVisibleItems(5)
                            ->setOnSelectionChanged([this](int idx, const std::string &val) {})
                            ->setWidth(260),
                            canvas->setSize(400, 400),
                        Button("Click", [this]
                               { counter++; })})
                    ->setCrossAxisAlignment(CrossAxisAlignment::Center))
                ->setWidth(300)
                ->setHeight(200)

                ->setBorderRadius(10))),

        FAB("+",
            [this]
            {
              std::cout << "FAB pressed!" << std::endl;
              counter++;
            })
            ->setPosition(FABPosition::BottomRight)
            ->setColor(Color::fromRGB(33, 150, 243))
            ->setLabel("Add Item"),
        nullptr);
  }
};

WidgetPtr createApp(FluxUI *app)
{
  return FluxApp("FluxUI - App", std::make_shared<MyApp>(), AppTheme::light(),
                 false, 900, 700, false, false);
}