// vector_example.cpp  —  Adobe Illustrator-style layout
#include "flux.hpp"
#include "widgets/flux_vector.hpp"

// ── Color helpers ────────────────────────────────────────────────────────────
static COLORREF hexToRef(const std::string &css) {
  RGBA c = parseHexColor(css);
  return RGB((BYTE)(c.r * 255), (BYTE)(c.g * 255), (BYTE)(c.b * 255));
}

// ============================================================================
// VectorApp  —  Adobe Illustrator-style chrome
//
//  ┌──────────────────────────────────────────────────────┐
//  │  Menu/App bar (32px)                                  │
//  ├──────────────────────────────────────────────────────┤
//  │  Control bar — context-sensitive options  (36px)      │
//  ├────┬─────────────────────────────────────┬───────────┤
//  │    │                                     │           │
//  │ T  │   Canvas / Artboard                 │ Properties│
//  │ o  │   (grey pasteboard + white paper)   │ Panel     │
//  │ o  │                                     │  (260px)  │
//  │ l  │                                     │           │
//  │ b  │                                     │           │
//  │ a  │                                     │           │
//  │ r  │                                     │           │
//  │(48)│                                     │           │
//  ├────┴─────────────────────────────────────┴───────────┤
//  │  Status bar (22px)                                    │
//  └──────────────────────────────────────────────────────┘
//
// Palette
//   kBg      #323232  — outer chrome / status bar
//   kPanel   #2B2B2B  — panel / toolbar backgrounds
//   kDark    #252526  — sidebar separator / deeper bg
//   kBorder  #1A1A1A  — borders between regions
//   kAccent  #FF6B00  — Illustrator orange accent
//   kText    #CCCCCC  — normal label text
//   kDim     #888888  — secondary label text
// ============================================================================

class VectorApp : public Component {
  // ── State ─────────────────────────────────────────────────────────────────
  State<VTool> activeTool;
  State<std::string> fillColor;
  State<std::string> strokeColor;
  State<double> strokeWidth;
  State<bool> fillNone;
  State<bool> strokeNone;
  State<double> zoomLevel;
  State<bool> canUndo, canRedo;

  std::shared_ptr<VectorSurface> surface_;
  CanvasWidget *canvasPtr_ = nullptr;

  static constexpr double kZoomMin = 55.0, kZoomMax = 800.0;
  static constexpr int kToolbarW = 52; // left narrow tool strip
  static constexpr int kPanelW = 260;  // right properties panel
  static constexpr int kAppBarH = 32;  // top menu/app bar
  static constexpr int kCtrlBarH = 36; // context control bar
  static constexpr int kStatusH = 72;  // bottom status bar

  // ── Illustrator palette ───────────────────────────────────────────────────
  COLORREF kBg = RGB(50, 50, 50);
  COLORREF kPanel = RGB(43, 43, 43);
  COLORREF kDark = RGB(37, 37, 37);
  COLORREF kBorder = RGB(26, 26, 26);
  COLORREF kAccent = RGB(255, 107, 0);
  COLORREF kText = RGB(204, 204, 204);
  COLORREF kDim = RGB(136, 136, 136);
  COLORREF kWhite = RGB(255, 255, 255);
  COLORREF kHigh = RGB(70, 130, 180); // selection blue

  // ────────────────────────────────────────────────────────────────────────

  void refreshUndoRedo() {
    if (!surface_)
      return;
    canUndo.set(surface_->canUndo());
    canRedo.set(surface_->canRedo());
  }

  void syncZoom(float z) {
    double pct = z * 100.0;
    double clamped = max(pct, kZoomMin);
    clamped = min(clamped, kZoomMax);

    // If the viewport zoomed outside our allowed range, push it back
    // immediately
    if (clamped != pct && canvasPtr_) {
      float target = float(clamped / 100.0);
      Viewport &vp = canvasPtr_->viewport();
      float cur = vp.zoom();
      if (std::abs(target - cur) > 0.0001f) {
        vp.zoomToward(vp.viewW() * .5f, vp.viewH() * .5f, target / cur);
        canvasPtr_->redraw();
      }
    }

    if (std::abs(clamped - zoomLevel.get()) > 0.5)
      zoomLevel.set(clamped);
  }

  void applyZoom(double pct) {
    if (!canvasPtr_)
      return;
    pct = max(pct, kZoomMin);
    pct = min(pct, kZoomMax);
    Viewport &vp = canvasPtr_->viewport();
    float t = float(pct / 100.0), cur = vp.zoom();
    if (std::abs(t - cur) < 0.0001f)
      return;
    vp.zoomToward(vp.viewW() * .5f, vp.viewH() * .5f, t / cur);
    canvasPtr_->redraw();
  }

  void pushStyleToSurface() {
    if (!surface_)
      return;
    RGBA fc = parseHexColor(fillColor.get());
    VFill fill;
    fill.color = fc;
    fill.none = fillNone.get();
    surface_->setActiveFill(fill);

    RGBA sc = parseHexColor(strokeColor.get());
    VStroke stroke;
    stroke.color = sc;
    stroke.width = float(strokeWidth.get());
    stroke.none = strokeNone.get();
    surface_->setActiveStroke(stroke);
  }

public:
  VectorApp()
      : activeTool(VTool::Select, context),
        fillColor("#FFFFFF", context) // white fill (Illustrator default)
        ,
        strokeColor("#000000", context) // black stroke
        ,
        strokeWidth(4.0, context), fillNone(false, context),
        strokeNone(false, context), zoomLevel(100.0, context),
        canUndo(false, context), canRedo(false, context) {}

  WidgetPtr build() override {
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);

    int canvasAreaW = screenW - kToolbarW - kPanelW;
    int canvasAreaH = screenH - kAppBarH - kCtrlBarH - kStatusH;

    // A3 artboard (297×420mm at 96dpi ≈ 1123×1587px)
    static constexpr int kPaperW = 1123, kPaperH = 1587;

    auto canvas = VectorCanvas(canvasAreaW, canvasAreaH, kPaperW, kPaperH);
    surface_ = canvas->setSurface<VectorSurface>();
    canvasPtr_ = canvas.get();
    canvas->setScrollbarsEnabled(false);

    // ── Push initial style immediately so the surface starts with white fill
    //    and black stroke rather than whatever the VectorSurface default is.
    pushStyleToSurface();

    canvas->onViewportChanged = [this](float z) {
      syncZoom(z);
      if (surface_)
        surface_->setZoom(z);
    };

    // ── Wire state → surface ───────────────────────────────────────────────
    activeTool.listen([this](VTool t) {
      if (surface_)
        surface_->setTool(t);
    });
    fillColor.listen([this](const std::string &) { pushStyleToSurface(); });
    strokeColor.listen([this](const std::string &) { pushStyleToSurface(); });
    strokeWidth.listen([this](double) { pushStyleToSurface(); });
    fillNone.listen([this](bool) { pushStyleToSurface(); });
    strokeNone.listen([this](bool) { pushStyleToSurface(); });
    zoomLevel.listen([this](double pct) { applyZoom(pct); });

    // =========================================================================
    // §A  APP / MENU BAR
    // =========================================================================
    auto menuItem = [&](const std::string &label) -> WidgetPtr {
      return Container(Text(label)->setFontSize(11)->setTextColor(kText))
          ->setPaddingAll(12, 0, 12, 0)
          ->setHeight(kAppBarH);
    };

    auto appBar =
        Container(Row(
                      // Illustrator-style "Ai" logo mark
                      Container(Text("Ai")
                                    ->setFontSize(13)
                                    ->setFontWeight(FontWeight::Bold)
                                    ->setTextColor(kAccent))
                          ->setWidth(52)
                          ->setPaddingAll(0, 0, 4, 0),
                      menuItem("File"), menuItem("Edit"), menuItem("Object"),
                      menuItem("Type"), menuItem("Select"), menuItem("Effect"),
                      menuItem("View"), menuItem("Window"), menuItem("Help"))
                      ->setSpacing(0)
                      ->setCrossAxisAlignment(CrossAxisAlignment::Center))
            ->setBackgroundColor(kDark)
            ->setBorderWidth(1)
            ->setBorderColor(kBorder)
            ->setHeight(kAppBarH);

    // =========================================================================
    // §B  CONTROL BAR  (context-sensitive, top)
    // =========================================================================

    // Zoom cluster
    auto zoomCluster =
        Row(Text("Zoom:")->setFontSize(10)->setTextColor(kDim), SizedBox(4, 0),
            Slider(kZoomMin, kZoomMax, 0.25)
                ->setValue(zoomLevel)
                ->setTrackFillColor(kAccent)
                ->setWidth(90),
            SizedBox(4, 0),
            Text(zoomLevel,
                 [](double v) {
                   return std::to_string(int(std::round(v))) + "%";
                 })
                ->setFontSize(10)
                ->setTextColor(kText)
                ->setMinWidth(42),
            Button("100%",
                   [this] {
                     if (canvasPtr_) {
                       canvasPtr_->viewport().resetZoom();
                       canvasPtr_->redraw();
                       syncZoom(1.f);
                     }
                   })
                ->setBackgroundColor(RGB(60, 60, 60))
                ->setTextColor(kText)
                ->setBorderRadius(3)
                ->setHeight(22)
                ->setPadding(6),
            Button("Fit",
                   [this] {
                     if (canvasPtr_) {
                       canvasPtr_->viewport().fitToView();
                       canvasPtr_->redraw();
                       syncZoom(canvasPtr_->viewport().zoom());
                     }
                   })
                ->setBackgroundColor(RGB(60, 60, 60))
                ->setTextColor(kText)
                ->setBorderRadius(3)
                ->setHeight(22)
                ->setPadding(6))
            ->setSpacing(4)
            ->setCrossAxisAlignment(CrossAxisAlignment::Center);

    // Undo/Redo/Clear
    auto editCluster = Row(Button("↩",
                                  [this] {
                                    if (surface_) {
                                      surface_->undo();
                                      refreshUndoRedo();
                                      if (canvasPtr_)
                                        canvasPtr_->redraw();
                                    }
                                  })
                               ->setBackgroundColor(RGB(60, 60, 60))
                               ->setTextColor(RGB(100, 160, 255))
                               ->setBorderRadius(3)
                               ->setHeight(22)
                               ->setWidth(28)
                               ->setPadding(4),
                           Button("↪",
                                  [this] {
                                    if (surface_) {
                                      surface_->redo();
                                      refreshUndoRedo();
                                      if (canvasPtr_)
                                        canvasPtr_->redraw();
                                    }
                                  })
                               ->setBackgroundColor(RGB(60, 60, 60))
                               ->setTextColor(RGB(100, 200, 80))
                               ->setBorderRadius(3)
                               ->setHeight(22)
                               ->setWidth(28)
                               ->setPadding(4),
                           Button("Clear",
                                  [this] {
                                    if (surface_) {
                                      auto ids = surface_->scene();
                                      for (auto &s : ids)
                                        surface_->removeShape(s.id);
                                      refreshUndoRedo();
                                      if (canvasPtr_)
                                        canvasPtr_->redraw();
                                    }
                                  })
                               ->setBackgroundColor(RGB(60, 60, 60))
                               ->setTextColor(RGB(220, 80, 80))
                               ->setBorderRadius(3)
                               ->setHeight(22)
                               ->setPadding(6))
                           ->setSpacing(4)
                           ->setCrossAxisAlignment(CrossAxisAlignment::Center);

    auto controlBar =
        Container(Row(zoomCluster, SizedBox(16, 0),
                      Container(nullptr)
                          ->setWidth(1)
                          ->setHeight(20)
                          ->setBackgroundColor(kBorder),
                      SizedBox(16, 0), editCluster)
                      ->setSpacing(0)
                      ->setCrossAxisAlignment(CrossAxisAlignment::Center))
            ->setBackgroundColor(kPanel)
            ->setPaddingAll(8, 7, 8, 7)
            ->setBorderWidth(1)
            ->setBorderColor(kBorder)
            ->setHeight(kCtrlBarH);

    // =========================================================================
    // §C  LEFT TOOL STRIP  (narrow, icon buttons only, like Illustrator)
    // =========================================================================

    auto makeTool = [&](VTool tv, const std::string &icon,
                        const std::string &tip) -> WidgetPtr {
      return Tooltip(
          GestureDetector(
              Container(Center(Text(icon)->setFontSize(15)->setTextColor(
                            activeTool,
                            [tv](const VTool &at) {
                              return at == tv ? RGB(255, 255, 255)
                                              : RGB(170, 170, 170);
                            })))
                  ->setWidth(kToolbarW - 4)
                  ->setHeight(36)
                  ->setBorderRadius(4)
                  ->setBackgroundColor(activeTool,
                                       [tv](const VTool &at) {
                                         return at == tv ? RGB(255, 107, 0)
                                                         : RGB(43, 43, 43);
                                       }))
              ->setOnTap([this, tv]() { activeTool.set(tv); }),
          tip);
    };

    // Separator line between tool groups
    auto sep = Container(nullptr)
                   ->setWidth(kToolbarW - 16)
                   ->setHeight(1)
                   ->setBackgroundColor(RGB(60, 60, 60));

    auto toolStrip =
        Container(
            Column(SizedBox(0, 6),
                   // Selection group
                   makeTool(VTool::Select, "↖", "Selection (V)"),
                   SizedBox(0, 2),
                   makeTool(VTool::Node, "◆", "Direct Selection (A)"),
                   SizedBox(0, 8), sep, SizedBox(0, 8),
                   // Drawing group
                   makeTool(VTool::Pen, "✒", "Pen (P)"), SizedBox(0, 2),
                   makeTool(VTool::Rect, "▭", "Rectangle (M)"), SizedBox(0, 2),
                   makeTool(VTool::Ellipse, "⬭", "Ellipse (L)"), SizedBox(0, 2),
                   makeTool(VTool::Line, "╱", "Line Segment (\\)"),
                   SizedBox(0, 8), sep)
                ->setSpacing(0)
                ->setCrossAxisAlignment(CrossAxisAlignment::Center))
            ->setWidth(kToolbarW)
            ->setBackgroundColor(kPanel)
            ->setBorderWidth(1)
            ->setBorderColor(kBorder);

    // =========================================================================
    // §D  CANVAS with context menu (grey pasteboard is drawn by VectorSurface)
    // =========================================================================

    auto canvasWithMenu = ContextMenu(
        canvas, {
                    {"Select", [this] { activeTool.set(VTool::Select); }},
                    {"Rectangle", [this] { activeTool.set(VTool::Rect); }},
                    {"Ellipse", [this] { activeTool.set(VTool::Ellipse); }},
                    {"Line", [this] { activeTool.set(VTool::Line); }},
                    {"Pen", [this] { activeTool.set(VTool::Pen); }},
                    {"Node", [this] { activeTool.set(VTool::Node); }},
                    ContextMenuItem::Separator(),
                    {"Undo",
                     [this] {
                       if (surface_) {
                         surface_->undo();
                         refreshUndoRedo();
                         if (canvasPtr_)
                           canvasPtr_->redraw();
                       }
                     }},
                    {"Redo",
                     [this] {
                       if (surface_) {
                         surface_->redo();
                         refreshUndoRedo();
                         if (canvasPtr_)
                           canvasPtr_->redraw();
                       }
                     }},
                    ContextMenuItem::Separator(),
                    {"Zoom 100%",
                     [this] {
                       if (canvasPtr_) {
                         canvasPtr_->viewport().resetZoom();
                         canvasPtr_->redraw();
                         syncZoom(1.f);
                       }
                     }},
                    {"Fit to Window",
                     [this] {
                       if (canvasPtr_) {
                         canvasPtr_->viewport().fitToView();
                         canvasPtr_->redraw();
                         syncZoom(canvasPtr_->viewport().zoom());
                       }
                     }},
                });

    // =========================================================================
    // §E  RIGHT PROPERTIES PANEL
    // =========================================================================

    auto sectionHeader = [&](const std::string &t) -> WidgetPtr {
      return Container(Row(Text(t)
                               ->setFontSize(10)
                               ->setFontWeight(FontWeight::Bold)
                               ->setTextColor(kText),
                           SizedBox(6, 0),
                           // small chevron placeholder
                           Text("▾")->setFontSize(9)->setTextColor(kDim))
                           ->setSpacing(0)
                           ->setCrossAxisAlignment(CrossAxisAlignment::Center))
          ->setBackgroundColor(kDark)
          ->setPaddingAll(10, 6, 10, 6)
          ->setBorderWidth(1)
          ->setBorderColor(kBorder);
    };

    auto propRow = [&](const std::string &label,
                       WidgetPtr control) -> WidgetPtr {
      return Container(Row(Text(label)
                               ->setFontSize(10)
                               ->setTextColor(kDim)
                               ->setMinWidth(70),
                           SizedBox(6, 0), control)
                           ->setSpacing(0)
                           ->setCrossAxisAlignment(CrossAxisAlignment::Center))
          ->setPaddingAll(10, 4, 10, 4);
    };

    // ── Fill swatch + toggle ──────────────────────────────────────────────
    auto fillPicker =
        ColorPicker(hexToRef(fillColor.get()))
            ->setShowAlpha(false)
            ->setOnColorChanged([this](COLORREF c) {
              char buf[8];
              _snprintf_s(buf, sizeof(buf), _TRUNCATE, "#%02x%02x%02x",
                          GetRValue(c), GetGValue(c), GetBValue(c));
              fillColor.set(buf);
            });

    auto fillNoneBtn =
        GestureDetector(
            Container(Row(Container(nullptr)
                              ->setWidth(12)
                              ->setHeight(12)
                              ->setBorderRadius(2)
                              ->setBackgroundColor(fillNone,
                                                   [](const bool &f) {
                                                     return f ? RGB(255, 107, 0)
                                                              : RGB(37, 37, 37);
                                                   })
                              ->setBorderWidth(1)
                              ->setBorderColor(RGB(100, 100, 100)),
                          SizedBox(5, 0),
                          Text("None")->setFontSize(10)->setTextColor(kDim))
                          ->setSpacing(0)
                          ->setCrossAxisAlignment(CrossAxisAlignment::Center))
                ->setPaddingAll(6, 3, 6, 3)
                ->setBorderRadius(3)
                ->setBackgroundColor(RGB(55, 55, 55))
                ->setBorderWidth(1)
                ->setBorderColor(kBorder))
            ->setOnTap([this]() { fillNone.set(!fillNone.get()); });

    // ── Stroke swatch + toggle ────────────────────────────────────────────
    auto strokePicker =
        ColorPicker(hexToRef(strokeColor.get()))
            ->setShowAlpha(false)
            ->setOnColorChanged([this](COLORREF c) {
              char buf[8];
              _snprintf_s(buf, sizeof(buf), _TRUNCATE, "#%02x%02x%02x",
                          GetRValue(c), GetGValue(c), GetBValue(c));
              strokeColor.set(buf);
            });

    auto strokeNoneBtn =
        GestureDetector(
            Container(Row(Container(nullptr)
                              ->setWidth(12)
                              ->setHeight(12)
                              ->setBorderRadius(2)
                              ->setBackgroundColor(strokeNone,
                                                   [](const bool &f) {
                                                     return f ? RGB(255, 107, 0)
                                                              : RGB(37, 37, 37);
                                                   })
                              ->setBorderWidth(1)
                              ->setBorderColor(RGB(100, 100, 100)),
                          SizedBox(5, 0),
                          Text("None")->setFontSize(10)->setTextColor(kDim))
                          ->setSpacing(0)
                          ->setCrossAxisAlignment(CrossAxisAlignment::Center))
                ->setPaddingAll(6, 3, 6, 3)
                ->setBorderRadius(3)
                ->setBackgroundColor(RGB(55, 55, 55))
                ->setBorderWidth(1)
                ->setBorderColor(kBorder))
            ->setOnTap([this]() { strokeNone.set(!strokeNone.get()); });

    // ── Stroke weight slider (single, integers only) ──────────────────────
    auto strokeWSlider =
        Column(Row(Text("Weight")
                       ->setFontSize(10)
                       ->setTextColor(kDim)
                       ->setMinWidth(70),
                   SizedBox(6, 0),
                   Text(strokeWidth,
                        [](double v) {
                          return std::to_string(int(std::round(v))) + " pt";
                        })
                       ->setFontSize(10)
                       ->setTextColor(kText))
                   ->setSpacing(0)
                   ->setCrossAxisAlignment(CrossAxisAlignment::Center),
               SizedBox(0, 6),
               Slider(1.0, 100.0, 1.0)
                   ->setValue(strokeWidth)
                   ->setTrackFillColor(kAccent)
                   ->setWidth(kPanelW - 24))
            ->setSpacing(0);

    // ── Full right panel ──────────────────────────────────────────────────
    auto rightPanel =
        Container(
            Column(
                // — Transform placeholder header
                sectionHeader("Transform"),
                Container(
                    Column(
                        propRow("X:", Text("—")->setFontSize(10)->setTextColor(
                                          kText)),
                        propRow("Y:", Text("—")->setFontSize(10)->setTextColor(
                                          kText)),
                        propRow("W:", Text("—")->setFontSize(10)->setTextColor(
                                          kText)),
                        propRow("H:", Text("—")->setFontSize(10)->setTextColor(
                                          kText)))
                        ->setSpacing(0))
                    ->setBackgroundColor(kPanel)
                    ->setPaddingAll(0, 0, 0, 0),

                // — Appearance section
                sectionHeader("Appearance"),
                Container(
                    Column(
                        // Fill row
                        Container(
                            Column(Row(Text("Fill")
                                           ->setFontSize(10)
                                           ->setFontWeight(FontWeight::Bold)
                                           ->setTextColor(kText),
                                       SizedBox(6, 0), fillNoneBtn)
                                       ->setSpacing(0)
                                       ->setCrossAxisAlignment(
                                           CrossAxisAlignment::Center),
                                   SizedBox(0, 6), fillPicker)
                                ->setSpacing(0))
                            ->setPaddingAll(10, 8, 10, 8),

                        Container(nullptr)->setHeight(1)->setBackgroundColor(
                            kBorder),

                        // Stroke row
                        Container(
                            Column(Row(Text("Stroke")
                                           ->setFontSize(10)
                                           ->setFontWeight(FontWeight::Bold)
                                           ->setTextColor(kText),
                                       SizedBox(6, 0), strokeNoneBtn)
                                       ->setSpacing(0)
                                       ->setCrossAxisAlignment(
                                           CrossAxisAlignment::Center),
                                   SizedBox(0, 6), strokePicker,
                                   SizedBox(0, 10), strokeWSlider)
                                ->setSpacing(0))
                            ->setPaddingAll(10, 8, 10, 8))
                        ->setSpacing(0))
                    ->setBackgroundColor(kPanel))
                ->setSpacing(0))
            ->setWidth(kPanelW)
            ->setBackgroundColor(kPanel)
            ->setBorderWidth(1)
            ->setBorderColor(kBorder);

    // =========================================================================
    // §F  STATUS BAR
    // =========================================================================

    auto statusBar =
        Container(Row(Text("Ready")->setFontSize(9)->setTextColor(kDim),
                      SizedBox(16, 0),
                      Text("A3 Portrait | 1123 × 1587 px")
                          ->setFontSize(9)
                          ->setTextColor(kDim),
                      SizedBox(16, 0),
                      Text(activeTool,
                           [](const VTool &t) -> std::string {
                             switch (t) {
                             case VTool::Select:
                               return "Tool: Selection";
                             case VTool::Node:
                               return "Tool: Direct Selection";
                             case VTool::Pen:
                               return "Tool: Pen";
                             case VTool::Rect:
                               return "Tool: Rectangle";
                             case VTool::Ellipse:
                               return "Tool: Ellipse";
                             case VTool::Line:
                               return "Tool: Line Segment";
                             default:
                               return "";
                             }
                           })
                          ->setFontSize(9)
                          ->setTextColor(kDim))
                      ->setSpacing(0)
                      ->setCrossAxisAlignment(CrossAxisAlignment::Center))
            ->setBackgroundColor(kDark)
            ->setPaddingAll(10, 4, 10, 4)
            ->setBorderWidth(1)
            ->setBorderColor(kBorder)
            ->setHeight(kStatusH);



    auto centerRow = Row(toolStrip,
                         Container(canvasWithMenu)
                             ->setBackgroundColor(
                                 kBg), // grey pasteboard bg visible at edges
                         rightPanel)
                         ->setSpacing(0);

    auto root = Column(appBar, controlBar, centerRow, statusBar)->setSpacing(0);

    return Scaffold(root);
  }
};

// ── Entry point
// ───────────────────────────────────────────────────────────────
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
  FluxUI app(hInstance);
  app.build([&]() {
    return FluxApp("Illustrator-Style Vector Draw", BuildComponent<VectorApp>(),
                   AppTheme::dark());
  });
  int W = GetSystemMetrics(SM_CXSCREEN);
  int H = GetSystemMetrics(SM_CYSCREEN);
  app.createWindow("FluxUI — Vector", W, H);
  ShowWindow(GetActiveWindow(), SW_MAXIMIZE);
  return app.run();
}