#include "flux.hpp"

// ── Helpers ──────────────────────────────────────────────────────────────────

// Convert a CSS hex color string (e.g. "#cba6f7") to a Win32 COLORREF
// so we can use it with Container / Button APIs that take COLORREF.
static COLORREF hexToRef(const std::string &css) {
  RGBA c = detail::parseColor(css);
  return RGB((BYTE)(c.r * 255), (BYTE)(c.g * 255), (BYTE)(c.b * 255));
}

// ── Data ─────────────────────────────────────────────────────────────────────

struct PaintPoint {
  int x, y;
};

struct Stroke {
  std::vector<PaintPoint> points;
  std::string color;
  float width = 4.f;
};

// ── Component ────────────────────────────────────────────────────────────────

class PaintApp : public Component {

  // Reactive state — only used to poke the canvas into redrawing
  State<bool> drawing; // true while mouse is held
  State<int> revision; // bumped on mouseUp / undo / clear

  // Non-reactive drawing data
  std::vector<Stroke> strokes;
  Stroke currentStroke;

  // Tool settings (rebuilt on change via markNeedsRebuild)
  std::string brushColor = "#cba6f7";
  float brushSize = 4.f;

  // Palette: {css color, tooltip label}
  const std::vector<std::pair<std::string, std::string>> kPalette = {
      {"#cba6f7", "Purple"}, {"#f38ba8", "Pink"},  {"#fab387", "Peach"},
      {"#f9e2af", "Yellow"}, {"#a6e3a1", "Green"}, {"#89dceb", "Sky"},
      {"#89b4fa", "Blue"},   {"#ffffff", "White"}, {"#6c7086", "Gray"},
      {"#1e1e2e", "Eraser"},
  };

public:
  PaintApp() : drawing(false, context), revision(0, context) {}

  WidgetPtr build() override {

    // ── Canvas ────────────────────────────────────────────────────────────
    auto canvas =
        Canvas(560, 400)
            ->bindState(drawing)
            ->bindState(revision)
            ->onDraw([=](CanvasContext &ctx) {
              ctx.fillStyle("#1e1e2e").fillRect(0, 0, 560, 400);

              // Completed strokes
              for (const auto &s : strokes) {
                if (s.points.size() < 2)
                  continue;
                ctx.strokeStyle(s.color)
                    .lineWidth(s.width)
                    .lineCap("round")
                    .lineJoin("round");
                ctx.beginPath().moveTo((float)s.points[0].x,
                                       (float)s.points[0].y);
                for (size_t i = 1; i < s.points.size(); i++)
                  ctx.lineTo((float)s.points[i].x, (float)s.points[i].y);
                ctx.stroke();
              }

              // In-progress stroke
              if (drawing.get() && currentStroke.points.size() >= 2) {
                auto &s = currentStroke;
                ctx.strokeStyle(s.color)
                    .lineWidth(s.width)
                    .lineCap("round")
                    .lineJoin("round");
                ctx.beginPath().moveTo((float)s.points[0].x,
                                       (float)s.points[0].y);
                for (size_t i = 1; i < s.points.size(); i++)
                  ctx.lineTo((float)s.points[i].x, (float)s.points[i].y);
                ctx.stroke();
              }

              // Cursor preview dot
              if (!currentStroke.points.empty()) {
                auto &p = currentStroke.points.back();
                ctx.fillStyle(brushColor)
                    .fillCircle((float)p.x, (float)p.y, brushSize * 0.5f + 1.f);
              }

              // Stroke counter
              char buf[32];
              std::snprintf(buf, sizeof(buf), "%d strokes",
                            (int)strokes.size());
              ctx.fillStyle("#45475a").font("11", "Consolas");
              ctx.fillText(buf, 8, 396);
            })
            ->onMouseDown([this](int mx, int my) {
              currentStroke = {};
              currentStroke.color = brushColor;
              currentStroke.width = brushSize;
              currentStroke.points.push_back({mx, my});
              drawing.set(true);
            })
            ->onMouseMove([this](int mx, int my) {
              currentStroke.points.push_back(
                  {std::clamp(mx, 0, 560), std::clamp(my, 0, 400)});
              drawing.set(drawing.get()); // re-arm redraw
            })
            ->onMouseUp([this](int, int) {
              if (currentStroke.points.size() >= 2)
                strokes.push_back(std::move(currentStroke));
              currentStroke = {};
              drawing.set(false);
              revision.set(revision.get() + 1);
            });

    // ── Palette swatches ──────────────────────────────────────────────────
    std::vector<WidgetPtr> swatches;
    for (const auto &[col, label] : kPalette) {
      bool active = (col == brushColor);
      std::string c = col;
      swatches.push_back(
          GestureDetector(Container(nullptr)
                              ->setWidth(26)
                              ->setHeight(26)
                              ->setBorderRadius(13)
                              ->setBackgroundColor(hexToRef(col))
                              ->setBorderColor(active ? RGB(255, 255, 255)
                                                      : RGB(49, 50, 68))
                              ->setBorderWidth(active ? 3 : 1))
              ->setOnTap([this, c]() {
                brushColor = c;
              }));
    }

    // ── Brush-size buttons ────────────────────────────────────────────────
    auto sizeBtn = [&](float sz, const std::string &label) -> WidgetPtr {
      bool active = (brushSize == sz);
      float s = sz;
      return GestureDetector(
                 Container(
                     Text(label)->setFontSize(11)->setTextColor(
                         active ? RGB(203, 166, 247) : RGB(108, 112, 134)))
                     ->setWidth(32)
                     ->setHeight(26)
                     ->setBorderRadius(5)
                     ->setBackgroundColor(active ? RGB(49, 50, 68)
                                                 : RGB(24, 24, 37))
                     ->setBorderColor(active ? RGB(203, 166, 247)
                                             : RGB(49, 50, 68))
                     ->setBorderWidth(1)
                     ->setPadding(4))
          ->setOnTap([this, s]() {
            brushSize = s;
          });
    };

    // ── Action buttons ────────────────────────────────────────────────────
    auto undoBtn = Button("Undo",
                          [this]() {
                            if (!strokes.empty()) {
                              strokes.pop_back();
                              revision.set(revision.get() + 1);
                            }
                          })
                       ->setBackgroundColor(RGB(24, 24, 37))
                       ->setTextColor(RGB(137, 180, 250))
                       ->setBorderRadius(5)
                       ->setWidth(54)
                       ->setHeight(26)
                       ->setPadding(4);

    auto clearBtn = Button("Clear",
                           [this]() {
                             strokes.clear();
                             currentStroke = {};
                             drawing.set(false);
                             revision.set(revision.get() + 1);
                           })
                        ->setBackgroundColor(RGB(24, 24, 37))
                        ->setTextColor(RGB(243, 139, 168))
                        ->setBorderRadius(5)
                        ->setWidth(54)
                        ->setHeight(26)
                        ->setPadding(4);

    // ── Toolbar ───────────────────────────────────────────────────────────
    // Row has no vector overload — add children manually for dynamic lists
    auto paletteRow = Row(swatches);
    for (auto &s : swatches)
      paletteRow->addChild(s);
    paletteRow->setSpacing(5);
    paletteRow->setCrossAxisAlignment(CrossAxisAlignment::Center);

    auto sizeRow = Row({sizeBtn(2.f, "S"), sizeBtn(5.f, "M"),
                        sizeBtn(10.f, "L"), sizeBtn(20.f, "XL")});
    sizeRow->setSpacing(4);
    sizeRow->setCrossAxisAlignment(CrossAxisAlignment::Center);

    auto actionRow = Row({undoBtn, SizedBox(4, 0), clearBtn});
    actionRow->setCrossAxisAlignment(CrossAxisAlignment::Center);

    auto toolbarRow =
        Row({paletteRow, SizedBox(14, 0), sizeRow, SizedBox(14, 0), actionRow});
    toolbarRow->setSpacing(0);
    toolbarRow->setCrossAxisAlignment(CrossAxisAlignment::Center);

    auto toolbar = Container(toolbarRow)
                       ->setBackgroundColor(RGB(17, 17, 27))
                       ->setPadding(10)
                       ->setHeight(50);

    return Scaffold(AppBar("Paint"), Column({toolbar, canvas})->setSpacing(0));
  }
};

// ── Entry point ──────────────────────────────────────────────────────────────

WidgetPtr createApp(FluxUI *app) {
  return FluxApp("Paint", BuildComponent<PaintApp>(), AppTheme::dark());
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
  AllocConsole();
  FILE *fp;
  freopen_s(&fp, "CONOUT$", "w", stdout);
  std::cout << "=== FluxUI - Paint ===" << std::endl;
  FluxUI app(hInstance);
  app.build([&]() { return createApp(&app); });
  app.createWindow("FluxUI - Paint", 580, 510);
  return app.run();
}