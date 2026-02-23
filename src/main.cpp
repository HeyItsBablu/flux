#include "flux.hpp"

class MouseTest : public Component {

  State<int> dotX;
  State<int> dotY;
  State<bool> dragging;

public:
  MouseTest()
      : dotX(200, context), dotY(150, context), dragging(false, context) {}

  WidgetPtr build() override {

    auto canvas =
        Canvas(400, 300)
            ->bindState(dotX)
            ->bindState(dotY)
            ->bindState(dragging)
            ->onDraw([=](CanvasContext &ctx) {
              // Background
              ctx.fillStyle("#1e1e2e").fillRect(0, 0, 400, 300);

              // Crosshair lines from dot
              ctx.strokeStyle("#313244").lineWidth(1);
              ctx.beginPath()
                  .moveTo(dotX.get(), 0)
                  .lineTo(dotX.get(), 300)
                  .stroke();
              ctx.beginPath()
                  .moveTo(0, dotY.get())
                  .lineTo(400, dotY.get())
                  .stroke();

              // Outer glow when dragging
              if (dragging.get()) {

                ctx.strokeStyle("rgba(243,139,168,0.3)").lineWidth(16);
                ctx.strokeCircle(dotX.get(), dotY.get(), 18);
              }

              // The dot
              ctx.fillStyle(dragging.get() ? "#f38ba8" : "#cba6f7")
                  .fillCircle(dotX.get(), dotY.get(), 12);
              ctx.strokeStyle("#ffffff").lineWidth(1.5f).strokeCircle(
                  dotX.get(), dotY.get(), 12);

              // Label
              ctx.fillStyle("#cdd6f4").font("12px", "Consolas");
              ctx.fillText(dragging.get() ? "dragging..." : "drag the dot", 10,
                           20);

              char buf[32];
              std::snprintf(buf, sizeof(buf), "x:%d  y:%d", dotX.get(),
                            dotY.get());
              ctx.fillStyle("#6c7086").font("11px", "Consolas");
              ctx.fillText(buf, 10, 290);
            })
            ->onMouseDown([this](int mx, int my) {
              // Start drag only if click is within 16px of the dot
              int dx = dotX.get(), dy = dotY.get();
              if (std::abs(mx - dx) < 16 && std::abs(my - dy) < 16) {

                dragging.set(true);
              }
            })
            ->onMouseMove([this](int mx, int my) {
              if (!dragging.get())
                return;
              dotX.set(std::clamp(mx, 0, 400));
              dotY.set(std::clamp(my, 0, 300));
            })
            ->onMouseUp([this](int, int) { dragging.set(false); });

    return Scaffold(AppBar("Canvas Mouse Test"), canvas);
  }
};

WidgetPtr createApp(FluxUI *app) {
  return FluxApp("Canvas Demo", BuildComponent<MouseTest>(), AppTheme::light());
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
  AllocConsole();
  FILE *fp;
  freopen_s(&fp, "CONOUT$", "w", stdout);

  std::cout << "=== FluxUI - Canvas Demo ===" << std::endl;

  FluxUI app(hInstance);
  app.build([&]() { return createApp(&app); });
  app.createWindow("FluxUI - Canvas Demo", 600, 600);
  return app.run();
}