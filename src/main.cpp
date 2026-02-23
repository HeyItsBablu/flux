#include "flux.hpp"

class CanvasDemo : public Component {

    State<int> dotX;
    State<int> dotY;
    State<bool> dragging;
    State<float> time;

public:
    CanvasDemo()
        : dotX(300, context), dotY(200, context),
          dragging(false, context), time(0.f, context) {}

    WidgetPtr build() override {

        int dx = dotX.get(), dy = dotY.get();
        float t  = time.get();

        // Bar chart data
        std::vector<float> bars = { 0.6f, 0.85f, 0.4f, 0.95f, 0.55f, 0.75f, 0.3f };
        std::vector<std::string> labels = { "Mon","Tue","Wed","Thu","Fri","Sat","Sun" };

        auto canvas = Canvas(700, 480, [=](CanvasContext& ctx) {

            // ── Background ───────────────────────────────────────────────
            ctx.fillStyle("#0d1117").fillRect(0, 0, 700, 480);

            // ── Panel borders ────────────────────────────────────────────
            ctx.strokeStyle("#30363d").lineWidth(1);
            ctx.strokeRect(16, 16, 320, 200);   // sine panel
            ctx.strokeRect(364, 16, 320, 200);  // bar chart panel
            ctx.strokeRect(16, 236, 668, 228);  // drag panel

            // ── Panel labels ─────────────────────────────────────────────
            ctx.fillStyle("#8b949e").font("11px", "Consolas");
            ctx.fillText("SINE WAVE", 24, 34);
            ctx.fillText("WEEKLY STATS", 372, 34);
            ctx.fillText("DRAG THE DOT", 24, 254);

            // ── Sine wave ────────────────────────────────────────────────
            ctx.save();
            ctx.strokeStyle("#58a6ff").lineWidth(2).beginPath();
            for (int i = 0; i <= 300; i++) {
                float x = 24.f + i;
                float y = 108.f + std::sin((i * 0.05f) + t) * 60.f;
                if (i == 0) ctx.moveTo(x, y);
                else        ctx.lineTo(x, y);
            }
            ctx.stroke();

            // Moving dot on wave
            float wdx = 24.f + 150.f;
            float wdy = 108.f + std::sin((150.f * 0.05f) + t) * 60.f;
            ctx.fillStyle("#f78166").fillCircle(wdx, wdy, 5);
            ctx.restore();

            // ── Bar chart ────────────────────────────────────────────────
            ctx.save();
            float bx0 = 372, by0 = 44, bw = 320, bh = 160;
            float barW = bw / bars.size();
            for (int i = 0; i < (int)bars.size(); i++) {
                float barH = bars[i] * (bh - 20);
                float bx = bx0 + i * barW + barW * 0.15f;
                float bW = barW * 0.7f;

                // Bar fill — highlight tallest
                if (bars[i] >= 0.9f)
                    ctx.fillStyle("#3fb950");
                else
                    ctx.fillStyle("#1f6feb");

                ctx.beginPath()
                   .roundRect(bx, by0 + bh - barH, bW, barH, 4)
                   .fill();

                // Value label
                ctx.fillStyle("#e6edf3").font("10px", "Consolas");
                char buf[8]; std::snprintf(buf, sizeof(buf), "%.0f%%", bars[i]*100);
                ctx.fillText(buf, bx + bW*0.5f - 10, by0 + bh - barH - 6);

                // Day label
                ctx.fillStyle("#8b949e").font("10px", "Consolas");
                ctx.fillText(labels[i], bx + bW*0.5f - 8, by0 + bh + 14);
            }
            ctx.restore();

            // ── Drag panel grid ──────────────────────────────────────────
            ctx.save();
            ctx.strokeStyle("#161b22").lineWidth(1);
            for (int gx = 16; gx < 684; gx += 32) {
                ctx.beginPath().moveTo(gx, 236).lineTo(gx, 464).stroke();
            }
            for (int gy = 236; gy < 464; gy += 32) {
                ctx.beginPath().moveTo(16, gy).lineTo(684, gy).stroke();
            }
            ctx.restore();

            // Crosshair lines from dot
            ctx.strokeStyle("#30363d").lineWidth(1);
            ctx.beginPath().moveTo(dx, 236).lineTo(dx, 464).stroke();
            ctx.beginPath().moveTo(16, dy).lineTo(684, dy).stroke();

            // Shadow ring
            ctx.strokeStyle("rgba(88,166,255,0.2)").lineWidth(12);
            ctx.strokeCircle(dx, dy, 18);

            // Draggable dot
            ctx.fillStyle("#58a6ff").fillCircle(dx, dy, 10);
            ctx.strokeStyle("#cae8ff").lineWidth(2).strokeCircle(dx, dy, 10);

            // Coordinate readout
            ctx.fillStyle("#58a6ff").font("11px", "Consolas");
            char coord[32];
            std::snprintf(coord, sizeof(coord), "x: %d  y: %d", dx - 16, dy - 236);
            ctx.fillText(coord, 24, 458);
        });

        // // Mouse events for dragging
        // canvas->onMouseDown([this](int mx, int my) {
        //     int dx = dotX.get(), dy = dotY.get();
        //     if (std::abs(mx - dx) < 14 && std::abs(my - dy) < 14)
        //         dragging.set(true);
        // });
        // canvas->onMouseMove([this](int mx, int my) {
        //     if (!dragging.get()) return;
        //     dotX.set(std::clamp(mx, 24, 676));
        //     dotY.set(std::clamp(my, 244, 456));
        // });
        // canvas->onMouseUp([this](int, int) {
        //     dragging.set(false);
        // });

        // Advance time for sine animation (hook into your timer/tick system)
        // e.g. setInterval([this]{ time.set(time.get() + 0.05f); }, 16);

        return Scaffold(
            AppBar("Canvas Demo"),
            canvas
        );
    }
};

WidgetPtr createApp(FluxUI *app) {
  return FluxApp("List View Demo", BuildComponent<CanvasDemo>(),
                 AppTheme::light());
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
  FluxUI app(hInstance);
  app.build([&]() { return createApp(&app); });
  app.createWindow("FluxUI - List View Demo", 600, 600);
  return app.run();
}