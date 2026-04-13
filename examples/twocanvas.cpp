// test_canvas_surface.hpp
#pragma once
#include "flux/flux.hpp"


#include <cmath>
#include <string>

class TestCanvasSurface : public RenderSurface {
public:
    void initialize(int w, int h) override {
        (void)w; (void)h;
        // loadImage would go here if we had a test image
    }
    void resize(int w, int h) override { (void)w; (void)h; }
    void destroy() override {}
    void update(double dt) override {
        time_ += dt;
    }

    void render(Canvas2D& ctx) override {
        const float W = (float)ctx.width();
        const float H = (float)ctx.height();
        const float t = (float)time_;

        // ── Background ───────────────────────────────────────────────────
        ctx.setFillColor({18, 18, 28, 255});
        ctx.fillRect(0, 0, W, H);

        // ── Title ────────────────────────────────────────────────────────
        ctx.setFont("bold 22px Segoe UI");
        ctx.setTextAlign(TextAlign::Center);
        ctx.setTextBaseline(TextBaseline::Top);
        ctx.setFillColor({220, 220, 255, 255});
        ctx.fillText("Canvas2D Test Surface", W * 0.5f, 14.f);

        float col1 = 30.f;
        float col2 = W * 0.5f + 10.f;
        float row1 = 60.f;
        float row2 = H * 0.5f + 10.f;
        float cellW = W * 0.5f - 40.f;
        float cellH = H * 0.5f - 80.f;

        // ── Cell labels ───────────────────────────────────────────────────
        ctx.setFont("11px Segoe UI");
        ctx.setTextAlign(TextAlign::Left);
        ctx.setTextBaseline(TextBaseline::Top);
        ctx.setFillColor({120, 120, 160, 255});
        ctx.fillText("Primitives",      col1,  row1);
        ctx.fillText("Paths & Bezier",  col2,  row1);
        ctx.fillText("Text & Gradient", col1,  row2);
        ctx.fillText("Animation",       col2,  row2);

        float cy1 = row1 + 20.f;
        float cy2 = row2 + 20.f;

        // ╔══════════════════════════════════╗
        // ║  TOP-LEFT — Primitives           ║
        // ╚══════════════════════════════════╝
        ctx.pushClipRect(col1, cy1, cellW, cellH);
        {
            // Solid rects
            ctx.setFillColor({220, 60, 60, 255});
            ctx.fillRect(col1, cy1, 60, 40);

            ctx.setFillColor({60, 180, 60, 255});
            ctx.fillRect(col1 + 70, cy1, 60, 40);

            ctx.setFillColor({60, 120, 220, 255});
            ctx.fillRect(col1 + 140, cy1, 60, 40);

            // Rounded rects
            ctx.setFillColor({220, 160, 40, 220});
            ctx.fillRoundedRect(col1, cy1 + 55, 80, 44, 10);

            ctx.setStrokeColor({180, 80, 220, 255});
            ctx.setLineWidth(2.5f);
            ctx.strokeRoundedRect(col1 + 95, cy1 + 55, 80, 44, 10);

            // Ellipse via path
            ctx.setFillColor({40, 200, 200, 180});
            ctx.beginPath();
            ctx.arc(col1 + 30, cy1 + 140, 26, 0, 6.2832f);
            ctx.fill();

            ctx.setStrokeColor({255, 255, 100, 255});
            ctx.setLineWidth(2.f);
            ctx.beginPath();
            ctx.arc(col1 + 100, cy1 + 140, 26, 0, 6.2832f);
            ctx.stroke();

            // Semi-transparent alpha rect
            ctx.setFillColor({255, 80, 80, 100});
            ctx.fillRect(col1 + 60, cy1 + 120, 80, 40);
            ctx.setFillColor({80, 80, 255, 100});
            ctx.fillRect(col1 + 90, cy1 + 130, 80, 40);

            // strokeRect
            ctx.setStrokeColor({200, 200, 200, 180});
            ctx.setLineWidth(1.f);
            ctx.strokeRect(col1 + 150, cy1 + 115, 50, 50);
        }
        ctx.popClipRect();

        // ╔══════════════════════════════════╗
        // ║  TOP-RIGHT — Paths & Bezier      ║
        // ╚══════════════════════════════════╝
        ctx.pushClipRect(col2, cy1, cellW, cellH);
        {
            // Triangle via path
            ctx.setFillColor({240, 100, 100, 255});
            ctx.beginPath();
            ctx.moveTo(col2 + cellW * 0.2f, cy1 + 10);
            ctx.lineTo(col2 + cellW * 0.05f, cy1 + 70);
            ctx.lineTo(col2 + cellW * 0.35f, cy1 + 70);
            ctx.closePath();
            ctx.fill();

            // RGB triangle (3 separate filled triangles blended)
            float cx = col2 + cellW * 0.72f, bcy = cy1 + 40.f, r = 36.f;
            // red
            ctx.setFillColor({255, 60, 60, 160});
            ctx.beginPath();
            ctx.moveTo(cx,       bcy - r);
            ctx.lineTo(cx - r,   bcy + r * 0.5f);
            ctx.lineTo(cx + r,   bcy + r * 0.5f);
            ctx.closePath();
            ctx.fill();
            // green offset
            ctx.setFillColor({60, 220, 60, 160});
            ctx.beginPath();
            ctx.moveTo(cx - 14,  bcy - r + 14);
            ctx.lineTo(cx - r - 14, bcy + r * 0.5f + 8);
            ctx.lineTo(cx + r - 14, bcy + r * 0.5f + 8);
            ctx.closePath();
            ctx.fill();
            // blue offset
            ctx.setFillColor({60, 60, 255, 160});
            ctx.beginPath();
            ctx.moveTo(cx + 14,  bcy - r + 14);
            ctx.lineTo(cx - r + 14, bcy + r * 0.5f + 8);
            ctx.lineTo(cx + r + 14, bcy + r * 0.5f + 8);
            ctx.closePath();
            ctx.fill();

            // Bezier S-curve
            ctx.setStrokeColor({100, 200, 255, 255});
            ctx.setLineWidth(2.5f);
            ctx.beginPath();
            ctx.moveTo(col2 + 10, cy1 + 100);
            ctx.bezierCurveTo(col2 + 60,  cy1 + 90,
                               col2 + 80,  cy1 + 150,
                               col2 + 130, cy1 + 140);
            ctx.bezierCurveTo(col2 + 180, cy1 + 130,
                               col2 + 200, cy1 + 190,
                               col2 + 250, cy1 + 180);
            ctx.stroke();

            // Quadratic wave
            ctx.setStrokeColor({255, 180, 60, 255});
            ctx.setLineWidth(2.f);
            ctx.beginPath();
            ctx.moveTo(col2 + 10, cy1 + 220);
            for (int i = 0; i < 5; i++) {
                float bx = col2 + 10 + i * 50;
                ctx.quadraticCurveTo(bx + 25, cy1 + 200 + (i%2==0 ? -30.f : 30.f),
                                     bx + 50, cy1 + 220);
            }
            ctx.stroke();

            // Star polygon
            ctx.setFillColor({255, 220, 40, 200});
            ctx.beginPath();
            float sx = col2 + cellW * 0.8f, sy = cy1 + cellH * 0.75f, sr = 28.f;
            for (int i = 0; i < 10; i++) {
                float angle = i * 3.14159f / 5.f - 3.14159f / 2.f;
                float rad   = (i % 2 == 0) ? sr : sr * 0.45f;
                float px    = sx + std::cos(angle) * rad;
                float py    = sy + std::sin(angle) * rad;
                if (i == 0) ctx.moveTo(px, py);
                else        ctx.lineTo(px, py);
            }
            ctx.closePath();
            ctx.fill();
        }
        ctx.popClipRect();

        // ╔══════════════════════════════════╗
        // ║  BOTTOM-LEFT — Text & Gradient   ║
        // ╚══════════════════════════════════╝
        ctx.pushClipRect(col1, cy2, cellW, cellH);
        {
            // Horizontal gradient bar
            ctx.beginLinearGradient(col1, 0, col1 + cellW, 0);
            ctx.addColorStop(0.f,  {220, 40,  40,  255});
            ctx.addColorStop(0.33f,{220, 180, 40,  255});
            ctx.addColorStop(0.66f,{40,  180, 220, 255});
            ctx.addColorStop(1.f,  {140, 40,  220, 255});
            ctx.setFillGradient();
            ctx.fillRect(col1, cy2, cellW, 24);

            // Vertical gradient bar
            ctx.beginLinearGradient(0, cy2 + 34, 0, cy2 + 34 + 60);
            ctx.addColorStop(0.f, {40, 220, 120, 255});
            ctx.addColorStop(1.f, {40,  80, 220, 255});
            ctx.setFillGradient();
            ctx.fillRect(col1, cy2 + 34, cellW, 60);

            // Text samples
            ctx.setFillColor({230, 230, 255, 255});
            ctx.setTextAlign(TextAlign::Left);
            ctx.setTextBaseline(TextBaseline::Top);

            ctx.setFont("bold 18px Segoe UI");
            ctx.fillText("Bold 18px", col1, cy2 + 104);

            ctx.setFont("14px Segoe UI");
            ctx.fillText("Regular 14px — Hello, Canvas2D!", col1, cy2 + 130);

            ctx.setFont("11px Consolas");
            ctx.fillText("ctx.fillText(\"Monospace 11px\", x, y)", col1, cy2 + 152);

            // Measure + highlight
            ctx.setFont("14px Segoe UI");
            std::string sample = "Measured text";
            float mw = ctx.measureText(sample);
            ctx.setFillColor({255, 220, 40, 60});
            ctx.fillRect(col1, cy2 + 172, mw, 18);
            ctx.setFillColor({255, 255, 200, 255});
            ctx.fillText(sample, col1, cy2 + 172);

            // Text align demo
            float mx = col1 + cellW * 0.5f;
            ctx.setStrokeColor({80, 80, 120, 200});
            ctx.setLineWidth(1.f);
            ctx.beginPath();
            ctx.moveTo(mx, cy2 + 196);
            ctx.lineTo(mx, cy2 + 196 + 60);
            ctx.stroke();
            ctx.setFont("11px Segoe UI");
            ctx.setFillColor({180, 220, 255, 255});
            ctx.setTextAlign(TextAlign::Right);
            ctx.fillText("right aligned", mx, cy2 + 200);
            ctx.setTextAlign(TextAlign::Center);
            ctx.fillText("centered", mx, cy2 + 218);
            ctx.setTextAlign(TextAlign::Left);
            ctx.fillText("left aligned", mx, cy2 + 236);
        }
        ctx.popClipRect();

        // ╔══════════════════════════════════╗
        // ║  BOTTOM-RIGHT — Animation        ║
        // ╚══════════════════════════════════╝
        ctx.pushClipRect(col2, cy2, cellW, cellH);
        {
            float ax = col2 + cellW * 0.5f;
            float ay = cy2  + cellH * 0.5f;

            // Rotating spokes
            ctx.setLineWidth(1.5f);
            for (int i = 0; i < 12; i++) {
                float angle = t * 0.8f + i * (6.2832f / 12.f);
                float r0 = 20.f, r1 = 55.f;
                uint8_t alpha = (uint8_t)(80 + 170 * (0.5f + 0.5f * std::sin(t * 2.f + i)));
                ctx.setStrokeColor({160, 200, 255, alpha});
                ctx.beginPath();
                ctx.moveTo(ax + std::cos(angle) * r0, ay + std::sin(angle) * r0);
                ctx.lineTo(ax + std::cos(angle) * r1, ay + std::sin(angle) * r1);
                ctx.stroke();
            }

            // Orbiting circles
            for (int i = 0; i < 5; i++) {
                float angle  = t * 1.2f + i * (6.2832f / 5.f);
                float orbit  = 70.f;
                float px     = ax + std::cos(angle) * orbit;
                float py     = ay + std::sin(angle) * orbit;
                float pulse  = 6.f + 4.f * std::sin(t * 3.f + i);
                uint8_t r    = (uint8_t)(100 + 100 * std::sin(i * 1.3f));
                uint8_t g2   = (uint8_t)(100 + 100 * std::sin(i * 1.3f + 2.f));
                uint8_t b    = (uint8_t)(100 + 100 * std::sin(i * 1.3f + 4.f));
                ctx.setFillColor({r, g2, b, 220});
                ctx.beginPath();
                ctx.arc(px, py, pulse, 0, 6.2832f);
                ctx.fill();
            }

            // Pulsing centre ring
            float ringR = 14.f + 6.f * std::sin(t * 2.5f);
            ctx.setStrokeColor({220, 180, 255, 200});
            ctx.setLineWidth(2.f);
            ctx.beginPath();
            ctx.arc(ax, ay, ringR, 0, 6.2832f);
            ctx.stroke();

            // Lissajous trail
            ctx.setLineWidth(1.f);
            float prevX = 0, prevY = 0;
            int steps = 120;
            for (int i = 0; i <= steps; i++) {
                float ft    = t - (steps - i) * 0.016f;
                float lx    = ax + std::cos(ft * 1.7f) * 85.f;
                float ly    = ay + std::sin(ft * 2.3f) * 50.f;
                uint8_t alp = (uint8_t)(20 + 200 * i / steps);
                ctx.setStrokeColor({100, 220, 180, alp});
                if (i > 0) {
                    ctx.beginPath();
                    ctx.moveTo(prevX, prevY);
                    ctx.lineTo(lx, ly);
                    ctx.stroke();
                }
                prevX = lx; prevY = ly;
            }

            // FPS-style counter
            ctx.setFont("10px Consolas");
            ctx.setTextAlign(TextAlign::Left);
            ctx.setTextBaseline(TextBaseline::Top);
            ctx.setFillColor({100, 200, 100, 180});
            char buf[32];
            snprintf(buf, sizeof(buf), "t = %.2fs", (double)t);
            ctx.fillText(buf, col2 + 6, cy2 + 6);
        }
        ctx.popClipRect();

        // ── Grid dividers ─────────────────────────────────────────────────
        ctx.setStrokeColor({60, 60, 90, 255});
        ctx.setLineWidth(1.f);
        // vertical centre
        ctx.beginPath();
        ctx.moveTo(W * 0.5f, 50.f);
        ctx.lineTo(W * 0.5f, H - 10.f);
        ctx.stroke();
        // horizontal centre
        ctx.beginPath();
        ctx.moveTo(20.f,     H * 0.5f);
        ctx.lineTo(W - 20.f, H * 0.5f);
        ctx.stroke();
    }

    bool needsContinuousRedraw() const override { return true; }

private:
    double time_ = 0.0;
};

class TestApp : public Widget {
    CanvasWidget* canvasPtr_ = nullptr;
public:
    WidgetPtr build() override {
        auto canvas = std::make_shared<CanvasWidget>();
        canvas->setSize(800, 600);
        canvas->setCanvasSize(800, 600);
        canvas->setScrollbarsEnabled(false);
        canvasPtr_ = canvas.get();
        canvas->setSurface<TestCanvasSurface>();
        return Scaffold(nullptr, Center(canvas));
    }
};

WidgetPtr createApp(FluxUI* app) {
    return FluxApp("Canvas2D Test",
                   std::make_shared<TestApp>(),
                   AppTheme::dark(),
                   false, 860, 660, false, false);
}