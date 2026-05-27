#pragma once
#include "flux/flux.hpp"
#include <cmath>
#include <vector>

// ============================================================================
// PaintSurface
// ============================================================================

class PaintSurface : public RenderSurface
{
public:
    struct Point
    {
        float x, y;
    };
    struct Stroke
    {
        std::vector<Point> pts;
        Color color;
        float radius;
        bool eraser;
    };

    Color activeColor_ = Color::fromRGB(30, 144, 255);
    float brushRadius_ = 6.f;
    bool eraserMode_ = false;

    void clear()
    {
        strokes_.clear();
        current_ = nullptr;
        drawing_ = false;
    }

    void initialize(int w, int h) override
    {
        w_ = w;
        h_ = h;
    }
    void resize(int w, int h) override
    {
        w_ = w;
        h_ = h;
    }
    void destroy() override { strokes_.clear(); }
    void update(double) override {}
    bool needsContinuousRedraw() const override { return false; }

    void onMouseDown(float x, float y) override
    {
        drawing_ = true;
        strokes_.push_back({{}, activeColor_, brushRadius_, eraserMode_});
        current_ = &strokes_.back();
        current_->pts.push_back({x, y});
    }
    void onMouseMove(float x, float y) override
    {
        if (!drawing_ || !current_)
            return;
        current_->pts.push_back({x, y});
    }
    void onMouseUp(float x, float y) override
    {
        if (current_)
            current_->pts.push_back({x, y});
        drawing_ = false;
        current_ = nullptr;
    }

    void render(Canvas2D &ctx) override
    {
        float fw = float(ctx.width());
        float fh = float(ctx.height());

        ctx.setFillColor(Color::fromRGB(255, 255, 255));
        ctx.fillRect(0, 0, fw, fh);

        for (auto &s : strokes_)
        {
            if (s.pts.empty())
                continue;

            Color col = s.eraser ? Color::fromRGB(255, 255, 255) : s.color;
            ctx.setFillColor(col);

            if (s.pts.size() == 1)
            {
                ctx.fillCircle(s.pts[0].x, s.pts[0].y, s.radius);
                continue;
            }
            for (size_t i = 1; i < s.pts.size(); ++i)
            {
                float ax = s.pts[i - 1].x, ay = s.pts[i - 1].y;
                float bx = s.pts[i].x, by = s.pts[i].y;
                float dx = bx - ax, dy = by - ay;
                float len = std::sqrtf(dx * dx + dy * dy);
                if (len < 0.5f)
                {
                    ctx.fillCircle(ax, ay, s.radius);
                    continue;
                }
                float nx = -dy / len * s.radius, ny = dx / len * s.radius;
                ctx.beginPath();
                ctx.moveTo(ax + nx, ay + ny);
                ctx.lineTo(bx + nx, by + ny);
                ctx.lineTo(bx - nx, by - ny);
                ctx.lineTo(ax - nx, ay - ny);
                ctx.closePath();
                ctx.fill();
                ctx.fillCircle(ax, ay, s.radius);
                ctx.fillCircle(bx, by, s.radius);
            }
        }
    }

private:
    std::vector<Stroke> strokes_;
    Stroke *current_ = nullptr;
    bool drawing_ = false;
    int w_ = 512, h_ = 512;
};

// ============================================================================
// PaintApp
// ============================================================================

class PaintApp : public Widget
{
    std::shared_ptr<CanvasWidget> canvas_;
    std::shared_ptr<PaintSurface> surface_;

    State<int> selectedColor_{0};
    State<int> brushSize_{1}; // 0=S 1=M 2=L
    State<bool> eraserOn_{false};
    State<std::string> zoomLabel_{"100%"};

    struct Swatch
    {
        Color color;
    };
    const std::vector<Swatch> palette_ = {
        {Color::fromRGB(30, 144, 255)},  // Blue    (default)
        {Color::fromRGB(220, 53, 69)},   // Red
        {Color::fromRGB(40, 167, 69)},   // Green
        {Color::fromRGB(255, 193, 7)},   // Yellow
        {Color::fromRGB(111, 66, 193)},  // Purple
        {Color::fromRGB(253, 126, 20)},  // Orange
        {Color::fromRGB(20, 20, 20)},    // Black
        {Color::fromRGB(200, 200, 200)}, // Gray
        {Color::fromRGB(255, 255, 255)}, // White
    };

    static constexpr float kBrushSizes[3] = {3.f, 7.f, 16.f};

public:
    WidgetPtr build() override
    {
        canvas_ = std::make_shared<CanvasWidget>();
        canvas_->setViewportEnabled(true);
        canvas_->setScrollbarsEnabled(false);
        surface_ = canvas_->setSurface<PaintSurface>();

        // default selection
        surface_->activeColor_ = palette_[0].color;
        surface_->brushRadius_ = kBrushSizes[1];

        canvas_->onViewportChanged = [this](float zoom)
        {
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%.0f%%", zoom * 100.f);
            zoomLabel_.set(buf);
        };

        std::weak_ptr<PaintSurface> ws = surface_;
        std::weak_ptr<CanvasWidget> wc = canvas_;

        // ── Palette swatches ─────────────────────────────────────────────
        auto swatchRow = Row({});
        swatchRow->setSpacing(4);
        for (int i = 0; i < (int)palette_.size(); ++i)
        {
            auto idx = i;
            auto color = palette_[i].color;
            auto btn = Button("")
                           ->setHeight(26)
                           ->setWidth(26)
                           ->setBackgroundColor(color)
                           ->setBorderRadius(13)
                           ->setOnClick([this, ws, idx, color]()
                                        {
                    selectedColor_.set(idx);
                    eraserOn_.set(false);
                    if (auto s = ws.lock()) {
                        s->activeColor_ = color;
                        s->eraserMode_  = false;
                    } });
            swatchRow->addChild(btn);
        }

        // ── Brush size buttons ───────────────────────────────────────────
        auto makeSizeBtn = [&](const char *label, int idx, float r) -> WidgetPtr
        {
            return Button(label)
                ->setHeight(30)
                ->setWidth(32)
                ->setOnClick([this, ws, idx, r]()
                             {
                    brushSize_.set(idx);
                    if (auto s = ws.lock()) s->brushRadius_ = r; });
        };

        auto sizeRow = Row({
            makeSizeBtn("S", 0, kBrushSizes[0]),
            makeSizeBtn("M", 1, kBrushSizes[1]),
            makeSizeBtn("L", 2, kBrushSizes[2]),
        });
        sizeRow->setSpacing(4);

        // ── Eraser ───────────────────────────────────────────────────────
        auto eraserBtn = Button("Eraser")
                             ->setHeight(28)
                             ->setWidth(58)
                             ->setOnClick([this, ws]()
                                          {
                eraserOn_.set(true);
                if (auto s = ws.lock()) s->eraserMode_ = true; });

        // ── Clear ────────────────────────────────────────────────────────
        auto clearBtn = Button("Clear")
                            ->setHeight(28)
                            ->setWidth(50)
                            ->setOnClick([ws, wc]()
                                         {
                if (auto s = ws.lock()) s->clear();
                if (auto c = wc.lock()) c->redraw(); });

        // ── Fit / 1:1 ────────────────────────────────────────────────────
        auto fitBtn = Button("Fit")
                          ->setHeight(28)
                          ->setWidth(40)
                          ->setOnClick([wc]()
                                       {
                if (auto c = wc.lock()) c->viewport().fitToView(); });

        auto oneToOneBtn = Button("1:1")
                               ->setHeight(28)
                               ->setWidth(40)
                               ->setOnClick([wc]()
                                            {
                if (auto c = wc.lock()) c->viewport().resetZoom(); });

        // ── Toolbar row ───────────────────────────────────────────────────
        auto toolbar = Container(Row({
                                         Container(swatchRow),
                                         SizedBox(12, 0),
                                         sizeRow,
                                         SizedBox(8, 0),
                                         eraserBtn,
                                         SizedBox(8, 0),
                                         clearBtn,
                                         SizedBox(12, 0),
                                         Button("Hello there",[](){})->setHeight(32),
                                         fitBtn,
                                         oneToOneBtn,
                                         Text(zoomLabel_, [](const std::string &s)
                                              { return s; })
                                             ->setFontSize(10)
                                             ->setTextColor(Color::fromRGB(140, 140, 160)),
                                     })
                                     ->setPadding(8)
                                     ->setSpacing(0)
                                     ->setCrossAxisAlignment(CrossAxisAlignment::Center))
                           ->setHeight(50);

        toolbar->setBackgroundColor(Color::fromRGB(28, 28, 30));

        return Scaffold(nullptr, Expanded(Column({toolbar, Expanded(canvas_)})), nullptr, nullptr);
    }
};

// ============================================================================
// Entry point
// ============================================================================

WidgetPtr createApp(FluxUI *app)
{
    return FluxApp(
        "Paint",
        std::make_shared<PaintApp>(),
        AppTheme::dark(),
        false,
        900, 650,
        false, true);
}