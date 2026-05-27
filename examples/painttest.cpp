#pragma once
#include "flux/flux.hpp"
#include <cmath>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ============================================================================
// Shape types
// ============================================================================

enum class ShapeType
{
    Brush = 0,
    Line,
    Rectangle,
    FilledRect,
    RoundedRect,
    Ellipse,
    FilledEllipse,
    Triangle,
    Diamond,
    Star,
    Arrow,
    DoubleArrow,
};

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
        ShapeType shape;
    };

    Color activeColor_ = Color::fromRGB(30, 144, 255);
    float brushRadius_ = 6.f;
    bool eraserMode_ = false;
    ShapeType activeShape_ = ShapeType::Brush;

    void clear()
    {
        strokes_.clear();
        current_ = nullptr;
        drawing_ = false;
    }

    void initialize(int w, int h) override { w_ = w; h_ = h; }
    void resize(int w, int h) override { w_ = w; h_ = h; }
    void destroy() override { strokes_.clear(); }
    void update(double) override {}
    bool needsContinuousRedraw() const override { return false; }

    void onMouseDown(float x, float y) override
    {
        drawing_ = true;
        strokes_.push_back({{}, activeColor_, brushRadius_, eraserMode_, activeShape_});
        current_ = &strokes_.back();
        current_->pts.push_back({x, y});
    }
    void onMouseMove(float x, float y) override
    {
        if (!drawing_ || !current_) return;
        if (activeShape_ == ShapeType::Brush || activeShape_ == ShapeType::Line)
        {
            current_->pts.push_back({x, y});
        }
        else
        {
            if (current_->pts.size() < 2)
                current_->pts.push_back({x, y});
            else
                current_->pts[1] = {x, y};
        }
    }
    void onMouseUp(float x, float y) override
    {
        if (current_)
        {
            if (activeShape_ != ShapeType::Brush)
                current_->pts.push_back({x, y});
        }
        drawing_ = false;
        current_ = nullptr;
    }

    void render(Canvas2D &ctx) override
    {
        ctx.setFillColor(Color::fromRGB(255, 255, 255));
        ctx.fillRect(0, 0, float(ctx.width()), float(ctx.height()));
        for (auto &s : strokes_)
            renderStroke(ctx, s);
    }

private:
    void renderStroke(Canvas2D &ctx, const Stroke &s)
    {
        if (s.pts.empty()) return;
        Color col = s.eraser ? Color::fromRGB(255, 255, 255) : s.color;
        switch (s.shape)
        {
        case ShapeType::Brush:        renderBrush(ctx, s, col);            break;
        case ShapeType::Line:         renderLine(ctx, s, col);             break;
        case ShapeType::Rectangle:    renderRect(ctx, s, col, false, false); break;
        case ShapeType::FilledRect:   renderRect(ctx, s, col, true, false);  break;
        case ShapeType::RoundedRect:  renderRect(ctx, s, col, false, true);  break;
        case ShapeType::Ellipse:      renderEllipse(ctx, s, col, false);   break;
        case ShapeType::FilledEllipse:renderEllipse(ctx, s, col, true);    break;
        case ShapeType::Triangle:     renderTriangle(ctx, s, col);         break;
        case ShapeType::Diamond:      renderDiamond(ctx, s, col);          break;
        case ShapeType::Star:         renderStar(ctx, s, col);             break;
        case ShapeType::Arrow:        renderArrow(ctx, s, col, false);     break;
        case ShapeType::DoubleArrow:  renderArrow(ctx, s, col, true);      break;
        }
    }

    void renderBrush(Canvas2D &ctx, const Stroke &s, Color col)
    {
        ctx.setFillColor(col);
        if (s.pts.size() == 1)
        {
            ctx.fillCircle(s.pts[0].x, s.pts[0].y, s.radius);
            return;
        }
        for (size_t i = 1; i < s.pts.size(); ++i)
        {
            float ax = s.pts[i-1].x, ay = s.pts[i-1].y;
            float bx = s.pts[i].x,   by = s.pts[i].y;
            float dx = bx-ax, dy = by-ay;
            float len = std::sqrtf(dx*dx + dy*dy);
            if (len < 0.5f) { ctx.fillCircle(ax, ay, s.radius); continue; }
            float nx = -dy/len*s.radius, ny = dx/len*s.radius;
            ctx.beginPath();
            ctx.moveTo(ax+nx, ay+ny);
            ctx.lineTo(bx+nx, by+ny);
            ctx.lineTo(bx-nx, by-ny);
            ctx.lineTo(ax-nx, ay-ny);
            ctx.closePath();
            ctx.fill();
            ctx.fillCircle(ax, ay, s.radius);
            ctx.fillCircle(bx, by, s.radius);
        }
    }

    void renderLine(Canvas2D &ctx, const Stroke &s, Color col)
    {
        ctx.setFillColor(col);
        for (size_t i = 1; i < s.pts.size(); ++i)
        {
            float ax = s.pts[i-1].x, ay = s.pts[i-1].y;
            float bx = s.pts[i].x,   by = s.pts[i].y;
            float dx = bx-ax, dy = by-ay, len = std::sqrtf(dx*dx+dy*dy);
            if (len < 0.5f) continue;
            float nx = -dy/len*s.radius, ny = dx/len*s.radius;
            ctx.beginPath();
            ctx.moveTo(ax+nx, ay+ny);
            ctx.lineTo(bx+nx, by+ny);
            ctx.lineTo(bx-nx, by-ny);
            ctx.lineTo(ax-nx, ay-ny);
            ctx.closePath();
            ctx.fill();
        }
    }

    void renderRect(Canvas2D &ctx, const Stroke &s, Color col, bool filled, bool rounded)
    {
        if (s.pts.size() < 2) return;
        float x0 = s.pts[0].x, y0 = s.pts[0].y;
        float x1 = s.pts.back().x, y1 = s.pts.back().y;
        float lx = std::min(x0,x1), ly = std::min(y0,y1);
        float rw = std::abs(x1-x0), rh = std::abs(y1-y0);
        if (filled)
        {
            ctx.setFillColor(col);
            if (rounded) ctx.fillRoundedRect(lx, ly, rw, rh, 10.f);
            else         ctx.fillRect(lx, ly, rw, rh);
        }
        else
        {
            ctx.setStrokeColor(col);
            ctx.setLineWidth(s.radius);
            if (rounded) ctx.strokeRoundedRect(lx, ly, rw, rh, 10.f);
            else         ctx.strokeRect(lx, ly, rw, rh);
        }
    }

    void renderEllipse(Canvas2D &ctx, const Stroke &s, Color col, bool filled)
    {
        if (s.pts.size() < 2) return;
        float x0 = s.pts[0].x, y0 = s.pts[0].y;
        float x1 = s.pts.back().x, y1 = s.pts.back().y;
        float cx = (x0+x1)*0.5f, cy = (y0+y1)*0.5f;
        float rx = std::abs(x1-x0)*0.5f, ry = std::abs(y1-y0)*0.5f;
        if (filled)
        {
            ctx.setFillColor(col);
            ctx.beginPath();
            ctx.ellipse(cx, cy, rx, ry);
            ctx.fill();
        }
        else
        {
            ctx.setStrokeColor(col);
            ctx.setLineWidth(s.radius);
            ctx.beginPath();
            ctx.ellipse(cx, cy, rx, ry);
            ctx.stroke();
        }
    }

    void renderTriangle(Canvas2D &ctx, const Stroke &s, Color col)
    {
        if (s.pts.size() < 2) return;
        float x0 = s.pts[0].x, y0 = s.pts[0].y;
        float x1 = s.pts.back().x, y1 = s.pts.back().y;
        float cx = (x0+x1)*0.5f;
        ctx.setStrokeColor(col);
        ctx.setLineWidth(s.radius);
        ctx.beginPath();
        ctx.moveTo(cx, y0);
        ctx.lineTo(x1, y1);
        ctx.lineTo(x0, y1);
        ctx.closePath();
        ctx.stroke();
    }

    void renderDiamond(Canvas2D &ctx, const Stroke &s, Color col)
    {
        if (s.pts.size() < 2) return;
        float x0 = s.pts[0].x, y0 = s.pts[0].y;
        float x1 = s.pts.back().x, y1 = s.pts.back().y;
        float cx = (x0+x1)*0.5f, cy = (y0+y1)*0.5f;
        ctx.setStrokeColor(col);
        ctx.setLineWidth(s.radius);
        ctx.beginPath();
        ctx.moveTo(cx, y0);
        ctx.lineTo(x1, cy);
        ctx.lineTo(cx, y1);
        ctx.lineTo(x0, cy);
        ctx.closePath();
        ctx.stroke();
    }

    void renderStar(Canvas2D &ctx, const Stroke &s, Color col)
    {
        if (s.pts.size() < 2) return;
        float x0 = s.pts[0].x, y0 = s.pts[0].y;
        float x1 = s.pts.back().x, y1 = s.pts.back().y;
        float cx = (x0+x1)*0.5f, cy = (y0+y1)*0.5f;
        float outerR = std::min(std::abs(x1-x0), std::abs(y1-y0))*0.5f;
        float innerR = outerR*0.45f;
        int points = 5;
        ctx.setStrokeColor(col);
        ctx.setLineWidth(s.radius);
        ctx.beginPath();
        for (int i = 0; i < points*2; ++i)
        {
            float a = float(i)*float(M_PI)/float(points) - float(M_PI)/2.f;
            float r = (i%2==0) ? outerR : innerR;
            float px = cx+cosf(a)*r, py = cy+sinf(a)*r;
            if (i==0) ctx.moveTo(px, py);
            else      ctx.lineTo(px, py);
        }
        ctx.closePath();
        ctx.stroke();
    }

    void renderArrow(Canvas2D &ctx, const Stroke &s, Color col, bool doubleArrow)
    {
        if (s.pts.size() < 2) return;
        float x0 = s.pts[0].x, y0 = s.pts[0].y;
        float x1 = s.pts.back().x, y1 = s.pts.back().y;
        float dx = x1-x0, dy = y1-y0;
        float len = std::sqrtf(dx*dx+dy*dy);
        if (len < 1.f) return;
        float ux = dx/len, uy = dy/len;
        float headLen = std::min(20.f, len*0.35f);
        float headW = headLen*0.5f;
        ctx.setStrokeColor(col);
        ctx.setLineWidth(s.radius);
        ctx.setFillColor(col);
        ctx.beginPath();
        ctx.moveTo(x0 + ux*(doubleArrow ? headLen : 0), y0 + uy*(doubleArrow ? headLen : 0));
        ctx.lineTo(x1 - ux*headLen, y1 - uy*headLen);
        ctx.stroke();
        auto drawHead = [&](float tx, float ty, float dirX, float dirY)
        {
            ctx.beginPath();
            ctx.moveTo(tx, ty);
            ctx.lineTo(tx - dirX*headLen - dirY*headW, ty - dirY*headLen + dirX*headW);
            ctx.lineTo(tx - dirX*headLen + dirY*headW, ty - dirY*headLen - dirX*headW);
            ctx.closePath();
            ctx.fill();
        };
        drawHead(x1, y1, ux, uy);
        if (doubleArrow) drawHead(x0, y0, -ux, -uy);
    }

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
    std::shared_ptr<CanvasWidget>      canvas_;
    std::shared_ptr<PaintSurface>      surface_;
    std::shared_ptr<ColorPickerWidget> colorPicker_;  

    State<int>         selectedColor_{0};
    State<int>         brushSize_{1};
    State<bool>        eraserOn_{false};
    State<int>         selectedShape_{0};
    State<std::string> zoomLabel_{"100%"};

    struct ColorSwatch { Color color; };
    const std::vector<ColorSwatch> palette_ = {
        {Color::fromRGB(30,  144, 255)},  // Blue  (default)
        {Color::fromRGB(220,  53,  69)},  // Red
        {Color::fromRGB( 40, 167,  69)},  // Green
        {Color::fromRGB(255, 193,   7)},  // Yellow
        {Color::fromRGB(111,  66, 193)},  // Purple
        {Color::fromRGB(253, 126,  20)},  // Orange
        {Color::fromRGB( 20,  20,  20)},  // Black
        {Color::fromRGB(200, 200, 200)},  // Gray
        {Color::fromRGB(255, 255, 255)},  // White
    };

    struct ShapeTool { const char *label; const char *tooltip; ShapeType type; };
    const std::vector<ShapeTool> shapeTools_ = {
        {"/",  "Brush / Pencil",      ShapeType::Brush},
        {"—",  "Line",                ShapeType::Line},
        {"▭",  "Rectangle (outline)", ShapeType::Rectangle},
        {"▬",  "Rectangle (filled)",  ShapeType::FilledRect},
        {"▢",  "Rounded Rect",        ShapeType::RoundedRect},
        {"◯",  "Ellipse (outline)",   ShapeType::Ellipse},
        {"●",  "Ellipse (filled)",    ShapeType::FilledEllipse},
        {"△",  "Triangle",            ShapeType::Triangle},
        {"◇",  "Diamond",             ShapeType::Diamond},
        {"★",  "Star",                ShapeType::Star},
        {"→",  "Arrow",               ShapeType::Arrow},
        {"↔",  "Double Arrow",        ShapeType::DoubleArrow},
    };

    static constexpr float kBrushSizes[3] = {3.f, 7.f, 16.f};

public:
    WidgetPtr build() override
    {
        canvas_  = std::make_shared<CanvasWidget>();
        canvas_->setViewportEnabled(true);
        canvas_->setScrollbarsEnabled(false);
        surface_ = canvas_->setSurface<PaintSurface>();

        surface_->activeColor_ = palette_[0].color;
        surface_->brushRadius_ = kBrushSizes[1];
        surface_->activeShape_ = shapeTools_[0].type;

        canvas_->onViewportChanged = [this](float zoom)
        {
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%.0f%%", zoom * 100.f);
            zoomLabel_.set(buf);
        };

        std::weak_ptr<PaintSurface>  ws = surface_;
        std::weak_ptr<CanvasWidget>  wc = canvas_;

        // ── Shape sidebar ─────────────────────────────────────────────────

        auto shapesLabel = Text("SHAPES")
                               ->setFontSize(9)
                               ->setTextColor(Color::fromRGB(140, 140, 160))
                               ->setPaddingLRTB(8, 8, 8, 4);

        auto shapeGrid = Column({});
        shapeGrid->setSpacing(3);
        shapeGrid->setPadding(6);

        for (int row = 0; row < (int)shapeTools_.size(); row += 2)
        {
            auto rowW = Row({});
            rowW->setSpacing(3);
            for (int col = 0; col < 2 && row+col < (int)shapeTools_.size(); ++col)
            {
                int idx = row+col;
                const auto &st = shapeTools_[idx];
                auto btn = Button(st.label)
                               ->setHeight(28)
                               ->setWidth(52)
                               ->setOnClick([this, ws, wc, idx, st]()
                               {
                                   selectedShape_.set(idx);
                                   eraserOn_.set(false);
                                   if (auto s = ws.lock()) {
                                       s->activeShape_ = st.type;
                                       s->eraserMode_  = false;
                                   }
                               });
                rowW->addChild(btn);
            }
            shapeGrid->addChild(rowW);
        }

        // ── Draw label + brush size ───────────────────────────────────────

        auto drawLabel = Text("DRAW")
                             ->setFontSize(9)
                             ->setTextColor(Color::fromRGB(140, 140, 160))
                             ->setPaddingLRTB(8, 8, 6, 2);

        auto makeSizeBtn = [&](const char *lbl, int idx, float r) -> WidgetPtr
        {
            return Button(lbl)
                ->setHeight(26)
                ->setWidth(30)
                ->setOnClick([this, ws, idx, r]()
                {
                    brushSize_.set(idx);
                    if (auto s = ws.lock()) s->brushRadius_ = r;
                });
        };

        auto sizeRow = Row({
            makeSizeBtn("S", 0, kBrushSizes[0]),
            makeSizeBtn("M", 1, kBrushSizes[1]),
            makeSizeBtn("L", 2, kBrushSizes[2]),
        });
        sizeRow->setSpacing(3);

        // ── Color label ───────────────────────────────────────────────────

        auto colorLabel = Text("COLOR")
                              ->setFontSize(9)
                              ->setTextColor(Color::fromRGB(140, 140, 160))
                              ->setPaddingLRTB(8, 8, 8, 4);

        // ── Color picker ──────────────────────────────────────────────────
        // Initialise to the first palette swatch (dodger blue).
        // pickerSize is shrunk to 96 so it fits the 120-px sidebar;
        // alpha bar is hidden since the paint surface doesn't use alpha.

        colorPicker_ = ColorPicker(palette_[0].color);
        colorPicker_->pickerSize      = 96;
        colorPicker_->hueBarHeight    = 12;
        colorPicker_->alphaBarHeight  = 12;
        colorPicker_->barSpacing      = 5;
        colorPicker_->previewSize     = 20;
        colorPicker_->hexInputHeight  = 20;
        colorPicker_->paddingLeft     = 6;
        colorPicker_->paddingRight    = 6;
        colorPicker_->paddingTop      = 4;
        colorPicker_->paddingBottom   = 4;
        colorPicker_->showAlpha       = false;
        // Recompute height after tweaking layout constants
        colorPicker_->width  = colorPicker_->pickerSize
                             + colorPicker_->paddingLeft
                             + colorPicker_->paddingRight;

        // When the user drags inside the picker, push the new colour to the
        // surface immediately so it is live while drawing.
        colorPicker_->setOnColorChanged([this, ws](Color c)
        {
            selectedColor_.set(-1);   // deselect palette swatch highlight
            eraserOn_.set(false);
            if (auto s = ws.lock())
            {
                s->activeColor_ = c;
                s->eraserMode_  = false;
            }
        });

        // ── Palette swatches ──────────────────────────────────────────────

        auto swatchGrid = Column({});
        swatchGrid->setSpacing(3);

        for (int row = 0; row < (int)palette_.size(); row += 3)
        {
            auto rowW = Row({});
            rowW->setSpacing(3);
            for (int col = 0; col < 3 && row+col < (int)palette_.size(); ++col)
            {
                int   idx   = row+col;
                Color color = palette_[idx].color;
                auto btn = Button("")
                               ->setHeight(22)
                               ->setWidth(22)
                               ->setBackgroundColor(color)
                               ->setBorderRadius(11)
                               ->setOnClick([this, ws, idx, color]()
                               {
                                   selectedColor_.set(idx);
                                   eraserOn_.set(false);
                                   if (auto s = ws.lock()) {
                                       s->activeColor_ = color;
                                       s->eraserMode_  = false;
                                   }
                                   // Keep the picker in sync with the swatch
                                   if (colorPicker_)
                                       colorPicker_->setColor(color);
                               });
                rowW->addChild(btn);
            }
            swatchGrid->addChild(rowW);
        }

        // ── Eraser + Clear ────────────────────────────────────────────────

        auto eraserBtn = Button("Eraser")
                             ->setHeight(26)
                             ->setWidth(54)
                             ->setOnClick([this, ws]()
                             {
                                 eraserOn_.set(true);
                                 if (auto s = ws.lock()) s->eraserMode_ = true;
                             });

        auto clearBtn = Button("Clear")
                            ->setHeight(26)
                            ->setWidth(48)
                            ->setOnClick([ws, wc]()
                            {
                                if (auto s = ws.lock()) s->clear();
                                if (auto c = wc.lock()) c->redraw();
                            });

        auto toolRow = Row({eraserBtn, clearBtn});
        toolRow->setSpacing(4);

        // ── Zoom controls ─────────────────────────────────────────────────

        auto zoomLabel = Text("ZOOM")
                             ->setFontSize(9)
                             ->setTextColor(Color::fromRGB(140, 140, 160))
                             ->setPaddingLRTB(8, 8, 8, 4);

        auto fitBtn = Button("Fit")
                          ->setHeight(24)
                          ->setWidth(40)
                          ->setOnClick([wc]()
                          {
                              if (auto c = wc.lock()) c->viewport().fitToView();
                          });

        auto oneBtn = Button("1:1")
                          ->setHeight(24)
                          ->setWidth(36)
                          ->setOnClick([wc]()
                          {
                              if (auto c = wc.lock()) c->viewport().resetZoom();
                          });

        auto zoomPct = Text(zoomLabel_, [](const std::string &s){ return s; })
                           ->setFontSize(10)
                           ->setTextColor(Color::fromRGB(140, 140, 160));

        auto zoomRow = Row({fitBtn, oneBtn, zoomPct});
        zoomRow->setSpacing(4);

        // ── Sidebar assembly ──────────────────────────────────────────────

        auto sidebarContent = ListView({
            drawLabel,
            sizeRow,
            SizedBox(0, 6),
            Container(shapesLabel),
            Container(shapeGrid)->setHeight(200),
            SizedBox(0, 4),
            colorLabel,
            colorPicker_,              
            SizedBox(0, 4),
            Container(swatchGrid)->setHeight(120),
            SizedBox(0, 6),
            toolRow,
            SizedBox(0, 4),
            zoomLabel,
            zoomRow,
            SizedBox(0, 8),
        });
        sidebarContent->setSpacing(0);

        auto sidebar = Container(sidebarContent)
                           ->setWidth(120)
                           ->setBackgroundColor(Color::fromRGB(28, 28, 30));

        // ── Top toolbar ───────────────────────────────────────────────────

        auto toolbar = Container(
                           Row({
                               Text("Paint")->setFontSize(13)->setTextColor(Color::fromRGB(220, 220, 220)),
                               SizedBox(8, 0),
                               Button("Hello there", []() {})->setHeight(28),
                           })
                           ->setPadding(8)
                           ->setSpacing(6)
                           ->setCrossAxisAlignment(CrossAxisAlignment::Center))
                           ->setHeight(44)
                           ->setBackgroundColor(Color::fromRGB(28, 28, 30));

        // ── Root layout ───────────────────────────────────────────────────

        return Scaffold(
            nullptr,
            Expanded(Column({
                toolbar,
                Expanded(Row({
                    sidebar,
                    Expanded(canvas_),
                })),
            })),
            nullptr,
            nullptr);
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
        960, 680,
        false, true);
}