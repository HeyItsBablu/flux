// test_paint_viewport.cpp
#include "flux.hpp"

// ── Hex ↔ COLORREF helpers ───────────────────────────────────────────────────
static COLORREF hexToRef(const std::string &css) {
  RGBA c = parseHexColor(css);
  return RGB((BYTE)(c.r * 255), (BYTE)(c.g * 255), (BYTE)(c.b * 255));
}
static std::string refToHex(COLORREF c) {
  return ColorToHex(c); // from flux_colorpicker.hpp
}

// ============================================================================
// PaintSurface
// ============================================================================

class PaintSurface : public RasterSurface {
public:
  const std::vector<std::pair<std::string, std::string>> kPalette = {
      {"#cba6f7", "Purple"}, {"#f38ba8", "Pink"},  {"#fab387", "Peach"},
      {"#f9e2af", "Yellow"}, {"#a6e3a1", "Green"}, {"#89dceb", "Sky"},
      {"#89b4fa", "Blue"},   {"#ffffff", "White"},  {"#6c7086", "Gray"},
      {"#1e1e2e", "Black"},
  };

  std::string activeColor = "#cba6f7";
  float activeSize = 4.f;

  std::function<void()> onStateChanged;

  void setColor(const std::string &col) { activeColor = col; syncStyle(); }
  void setSize(float sz)                { activeSize  = sz;  syncStyle(); }

  void initialize(int w, int h) override {
    RasterSurface::initialize(w, h);
    syncStyle();
  }
  void onMouseUp(float x, float y) override {
    RasterSurface::onMouseUp(x, y);
    if (onStateChanged) onStateChanged();
  }
  void onKeyDown(int key) override {
    RasterSurface::onKeyDown(key);
    if (onStateChanged) onStateChanged();
  }

private:
  void syncStyle() {
    RGBA c = parseHexColor(activeColor);
    StrokeStyle s;
    s.r = c.r; s.g = c.g; s.b = c.b; s.a = 1.f;
    s.radius  = activeSize * 0.5f;
    s.opacity = 1.f;
    setStrokeStyle(s);
  }
};

// ============================================================================
// PaintApp
// ============================================================================

class PaintApp : public Component {
  State<std::string> activeColor;
  State<float>       activeSize;
  State<bool>        canUndo, canRedo;
  State<double>      zoomLevel;

  std::shared_ptr<PaintSurface> surface_;
  CanvasWidget *canvasPtr_ = nullptr;

  static constexpr double kZoomMin =   6.25;
  static constexpr double kZoomMax = 200.0;

  // Fixed sidebar + toolbar chrome heights — used to derive the canvas view size
  static constexpr int kSidebarWidth  = 240;
  static constexpr int kToolbarHeight =  42; // zoom bar
  static constexpr int kHintsHeight   =  24; // hint strip
  static constexpr int kAppBarHeight  =  32; // Scaffold AppBar (approx)

  void refreshUndoRedo() {
    if (!surface_) return;
    canUndo.set(surface_->canUndo());
    canRedo.set(surface_->canRedo());
  }
  void syncZoomState(float zoom) {
    double pct = double(zoom) * 100.0;
    if (std::abs(pct - zoomLevel.get()) > 0.5) zoomLevel.set(pct);
  }
  void applyZoomFromSlider(double pct) {
    if (!canvasPtr_) return;
    Viewport &vp = canvasPtr_->viewport();
    float target  = float(pct / 100.0);
    float current = vp.zoom();
    if (std::abs(target - current) < 0.0001f) return;
    vp.zoomToward(vp.viewW() * 0.5f, vp.viewH() * 0.5f, target / current);
    canvasPtr_->redraw();
  }

public:
  PaintApp()
      : activeColor("#cba6f7", context)
      , activeSize (4.f,       context)
      , canUndo    (false,     context)
      , canRedo    (false,     context)
      , zoomLevel  (100.0,     context)
  {}

  WidgetPtr build() override {

    // ── Compute canvas view dimensions from screen size ───────────────────
    //
    //   viewW = screenW - sidebarW
    //   viewH = screenH - appBarH - toolbarH - hintsH
    //
    //   The raster canvas itself (the "paper") is 2× the view size,
    //   giving room to pan and zoom before hitting the edge.
    //
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);

    int viewW = screenW - kSidebarWidth;
    int viewH = screenH - kAppBarHeight - kToolbarHeight - kHintsHeight;

    // Canvas paper = A4 at 150 DPI  (210mm × 297mm)
    //   150 DPI × (210 / 25.4) ≈ 1240 px wide
    //   150 DPI × (297 / 25.4) ≈ 1754 px tall
    static constexpr int kPaperW = 1240;
    static constexpr int kPaperH = 1754;
    int paperW = kPaperW;
    int paperH = kPaperH;

    // ── Canvas ────────────────────────────────────────────────────────────
    auto canvas = RasterCanvas(viewW, viewH, paperW, paperH);
    surface_    = canvas->setSurface<PaintSurface>();
    canvasPtr_  = canvas.get();

    surface_->onStateChanged  = [this]()       { refreshUndoRedo(); };
    canvas->onViewportChanged = [this](float z) { syncZoomState(z); };

    activeColor.listen([this](const std::string &col) { surface_->setColor(col); });
    activeSize .listen([this](float sz)               { surface_->setSize(sz);   });
    zoomLevel  .listen([this](double pct)             { applyZoomFromSlider(pct); });

    // =====================================================================
    // LEFT SIDEBAR
    // =====================================================================

    auto sectionLabel = [](const std::string &txt) -> WidgetPtr {
      return Text(txt)
          ->setFontSize(9)
          ->setTextColor(RGB(100, 105, 125))
          ->setFontWeight(FontWeight::Bold);
    };

    // ── Selected color display ────────────────────────────────────────────
    auto selectedSwatch =
        Container(
            Row(
                Container(nullptr)
                    ->setWidth(40)->setHeight(40)
                    ->setBorderRadius(6)
                    ->setBackgroundColor(activeColor,
                        [](const std::string &c) { return hexToRef(c); })
                    ->setBorderWidth(1)
                    ->setBorderColor(RGB(69, 71, 90)),

                SizedBox(10, 0),

                Column(
                    Text("Selected")
                        ->setFontSize(10)
                        ->setTextColor(RGB(100, 105, 125)),
                    SizedBox(0, 4),
                    Text(activeColor,
                         [](const std::string &c) { return c; })
                        ->setFontSize(12)
                        ->setTextColor(RGB(205, 214, 244))
                )->setSpacing(0)
            )->setSpacing(0)->setCrossAxisAlignment(CrossAxisAlignment::Center)
        )
        ->setBackgroundColor(RGB(24, 24, 37))
        ->setBorderRadius(8)
        ->setBorderWidth(1)
        ->setBorderColor(RGB(49, 50, 68))
        ->setPaddingAll(10, 10, 10, 10);

    // ── Inline color picker ───────────────────────────────────────────────
    auto picker = ColorPicker(hexToRef(activeColor.get()))
        ->setShowAlpha(false)
        ->setOnColorChanged([this](COLORREF c) { activeColor.set(refToHex(c)); });

    auto pickerSection = Container(picker)
        ->setBackgroundColor(RGB(24, 24, 37))
        ->setBorderRadius(8)
        ->setBorderWidth(1)
        ->setBorderColor(RGB(49, 50, 68));

    // ── Palette quick-select — 2 columns ──────────────────────────────────
    auto pcol1 = std::make_shared<ColumnWidget>();
    auto pcol2 = std::make_shared<ColumnWidget>();
    pcol1->setSpacing(4);
    pcol2->setSpacing(4);

    for (int i = 0; i < (int)surface_->kPalette.size(); i++) {
      const auto &[col, label] = surface_->kPalette[i];
      std::string c = col;

      auto sw = GestureDetector(
          Container(nullptr)
              ->setWidth(22)->setHeight(22)
              ->setBorderRadius(11)
              ->setBackgroundColor(hexToRef(col))
              ->setBorderWidth(activeColor, [c](const std::string &a) {
                  return a == c ? 2 : 0;
              })
              ->setBorderColor(activeColor, [c](const std::string &a) {
                  return a == c ? RGB(255, 255, 255) : RGB(0, 0, 0);
              })
      )->setOnTap([this, c]() { activeColor.set(c); });

      if (i % 2 == 0) pcol1->addChild(sw);
      else             pcol2->addChild(sw);
    }

    auto paletteSection =
        Container(
            Column(
                sectionLabel("PRESETS"),
                SizedBox(0, 8),
                Row(pcol1, SizedBox(4, 0), pcol2)->setSpacing(0)
            )->setSpacing(0)
        )
        ->setBackgroundColor(RGB(24, 24, 37))
        ->setBorderRadius(8)
        ->setBorderWidth(1)
        ->setBorderColor(RGB(49, 50, 68))
        ->setPaddingAll(10, 10, 10, 10);

    // ── Brush size ────────────────────────────────────────────────────────
    auto sizeBtn = [&](float sz, const std::string &lbl) -> WidgetPtr {
      float s = sz;
      return GestureDetector(
          Container(Text(lbl)->setFontSize(11))
              ->setWidth(38)->setHeight(28)
              ->setBorderRadius(5)
              ->setBackgroundColor(activeSize, [s](const float &a) {
                  return a == s ? RGB(49, 50, 68) : RGB(30, 30, 46);
              })
              ->setBorderColor(activeSize, [s](const float &a) {
                  return a == s ? RGB(203, 166, 247) : RGB(49, 50, 68);
              })
              ->setBorderWidth(activeSize, [s](const float &a) {
                  return a == s ? 2 : 1;
              })
              ->setPadding(4)
      )->setOnTap([this, s]() { activeSize.set(s); });
    };

    auto sizeSection =
        Container(
            Column(
                sectionLabel("BRUSH SIZE"),
                SizedBox(0, 8),
                Row(sizeBtn(2.f, "S"), sizeBtn(5.f, "M"),
                    sizeBtn(10.f, "L"), sizeBtn(20.f, "XL"))->setSpacing(4)
            )->setSpacing(0)
        )
        ->setBackgroundColor(RGB(24, 24, 37))
        ->setBorderRadius(8)
        ->setBorderWidth(1)
        ->setBorderColor(RGB(49, 50, 68))
        ->setPaddingAll(10, 10, 10, 10);

    // ── Undo / Redo / Clear ───────────────────────────────────────────────
    auto makeActionBtn = [](const std::string &lbl, COLORREF txtCol,
                            std::function<void()> fn) -> WidgetPtr {
      return Button(lbl, fn)
          ->setBackgroundColor(RGB(30, 30, 46))
          ->setTextColor(txtCol)
          ->setBorderRadius(5)
          ->setHeight(28)
          ->setPadding(4);
    };

    auto actionSection =
        Container(
            Column(
                sectionLabel("ACTIONS"),
                SizedBox(0, 8),
                makeActionBtn("↩  Undo", RGB(137, 180, 250), [this] {
                    if (surface_) { surface_->undo(); refreshUndoRedo(); }
                }),
                SizedBox(0, 4),
                makeActionBtn("Redo  ↪", RGB(166, 226, 46), [this] {
                    if (surface_) { surface_->redo(); refreshUndoRedo(); }
                }),
                SizedBox(0, 4),
                makeActionBtn("✕  Clear", RGB(243, 139, 168), [this] {
                    if (surface_) { surface_->clear(); refreshUndoRedo(); }
                })
            )->setSpacing(0)->setCrossAxisAlignment(CrossAxisAlignment::Stretch)
        )
        ->setBackgroundColor(RGB(24, 24, 37))
        ->setBorderRadius(8)
        ->setBorderWidth(1)
        ->setBorderColor(RGB(49, 50, 68))
        ->setPaddingAll(10, 10, 10, 10);

    // ── Sidebar assembly ──────────────────────────────────────────────────
    auto sidebar =
        Container(
            Column(
                selectedSwatch, SizedBox(0, 8),
                pickerSection,  SizedBox(0, 8),
                paletteSection, SizedBox(0, 8),
                sizeSection,    SizedBox(0, 8),
                actionSection
            )->setSpacing(0)
        )
        ->setWidth(kSidebarWidth)
        ->setBackgroundColor(RGB(17, 17, 27))
        ->setPaddingAll(12, 12, 12, 12);

    // =====================================================================
    // TOP TOOLBAR  (zoom only)
    // =====================================================================

    auto resetBtn = Button("1:1", [this] {
        if (canvasPtr_) {
          canvasPtr_->viewport().resetZoom();
          canvasPtr_->redraw();
          syncZoomState(1.f);
        }
      })
      ->setBackgroundColor(RGB(24, 24, 37))
      ->setTextColor(RGB(174, 129, 255))
      ->setBorderRadius(5)->setWidth(32)->setHeight(24)->setPadding(4);

    auto fitBtn = Button("Fit", [this] {
        if (canvasPtr_) {
          canvasPtr_->viewport().fitToView();
          canvasPtr_->redraw();
          syncZoomState(canvasPtr_->viewport().zoom());
        }
      })
      ->setBackgroundColor(RGB(24, 24, 37))
      ->setTextColor(RGB(174, 129, 255))
      ->setBorderRadius(5)->setWidth(32)->setHeight(24)->setPadding(4);

    auto zoomRow = Row(
        Text("Zoom")->setFontSize(11)->setTextColor(RGB(140, 140, 160)),
        SizedBox(6, 0),
        Slider(kZoomMin, kZoomMax, 0.25)
            ->setValue(zoomLevel)
            ->setTrackFillColor(RGB(174, 129, 255))
            ->setWidth(120),
        SizedBox(6, 0),
        Text(zoomLevel, [](double v) {
            return std::to_string(int(std::round(v))) + "%";
        })->setFontSize(11)->setTextColor(RGB(180, 180, 200))->setMinWidth(40),
        SizedBox(4, 0), resetBtn,
        SizedBox(4, 0), fitBtn
    );
    zoomRow->setSpacing(0);
    zoomRow->setCrossAxisAlignment(CrossAxisAlignment::Center);

    auto toolbar = Container(zoomRow)
        ->setBackgroundColor(RGB(17, 17, 27))
        ->setPaddingAll(10, 7, 10, 7)
        ->setHeight(kToolbarHeight);

    // ── Context menu ──────────────────────────────────────────────────────
    auto canvasWithMenu = ContextMenu(canvas, {
        { "Undo",  [this] { if (surface_) { surface_->undo();  refreshUndoRedo(); } } },
        { "Redo",  [this] { if (surface_) { surface_->redo();  refreshUndoRedo(); } } },
        ContextMenuItem::Separator(),
        { "Zoom 1:1",    [this] {
            if (canvasPtr_) { canvasPtr_->viewport().resetZoom();
                              canvasPtr_->redraw(); syncZoomState(1.f); }
        }},
        { "Fit to view", [this] {
            if (canvasPtr_) { canvasPtr_->viewport().fitToView();
                              canvasPtr_->redraw();
                              syncZoomState(canvasPtr_->viewport().zoom()); }
        }},
        ContextMenuItem::Separator(),
        { "Clear", [this] { if (surface_) { surface_->clear(); refreshUndoRedo(); } } },
    });

    // ── Hints strip ───────────────────────────────────────────────────────
    auto hints =
        Container(
            Row(
                Text("MMB / Space+LMB: Pan") ->setFontSize(10)->setTextColor(RGB(75, 75, 95)),
                SizedBox(10, 0),
                Text("Ctrl+Scroll: Zoom")    ->setFontSize(10)->setTextColor(RGB(75, 75, 95)),
                SizedBox(10, 0),
                Text("Ctrl+Z/Y: Undo/Redo") ->setFontSize(10)->setTextColor(RGB(75, 75, 95))
            )->setSpacing(0)
        )
        ->setBackgroundColor(RGB(17, 17, 27))
        ->setPaddingAll(10, 3, 10, 3)
        ->setHeight(kHintsHeight);

    // ── Canvas column ─────────────────────────────────────────────────────
    auto canvasColumn = Column(toolbar, hints, canvasWithMenu)->setSpacing(0);

    // ── Root: sidebar | canvas ────────────────────────────────────────────
    auto root = Row(sidebar, canvasColumn)->setSpacing(0);

    return Scaffold(root);
  }
};

// ── Entry point ──────────────────────────────────────────────────────────────

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
  FluxUI app(hInstance);
  app.build([&]() {
    return FluxApp("Paint", BuildComponent<PaintApp>(), AppTheme::dark());
  });

  RECT workArea;
  SystemParametersInfo(SPI_GETWORKAREA, 0, &workArea, 0);

  int screenW = workArea.right  - workArea.left;
  int screenH = workArea.bottom - workArea.top;

  app.createWindow("FluxUI - Paint", screenW, screenH);
  return app.run();
}