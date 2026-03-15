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
// ============================================================================

class VectorApp : public Component {
  // ── State ─────────────────────────────────────────────────────────────────
  State<VTool> activeTool;
  State<std::string> fillColor;
  State<std::string> strokeColor;
  State<double> strokeWidth;
  State<bool> fillNone;
  State<bool> strokeNone;
  // Gradient state
  State<bool> useGradient;
  State<int> selectedStop; // which stop is selected (0–3)
  State<int> stopCount;    // total stops (2–4)
  // Per-stop color stored as hex strings
  State<std::string> stopColor0, stopColor1, stopColor2, stopColor3;
  State<double> stopPos0, stopPos1, stopPos2, stopPos3;
  State<double> zoomLevel;
  State<bool> canUndo, canRedo;

  State<std::string> transformMode;
  State<bool> hasSelection;

  // ── Text style state ──────────────────────────────────────────────────────
  State<int> fontSize;
  State<bool> fontBold;
  State<bool> fontItalic;
  State<bool> fontUnderline;
  State<std::string> fontFace;  // font family name
  State<double> currentStopPos; // add to class state declarations

  std::shared_ptr<VectorSurface> surface_;
  CanvasWidget *canvasPtr_ = nullptr;

  static constexpr double kZoomMin = 55.0, kZoomMax = 800.0;
  static constexpr int kToolbarW = 52;
  static constexpr int kPanelW = 260;
  static constexpr int kAppBarH = 32;
  static constexpr int kCtrlBarH = 44;
  static constexpr int kStatusH = 72;

  // ── Palette ──────────────────────────────────────────────────────────────
  COLORREF kBg = RGB(50, 50, 50);
  COLORREF kPanel = RGB(43, 43, 43);
  COLORREF kDark = RGB(37, 37, 37);
  COLORREF kBorder = RGB(26, 26, 26);
  COLORREF kAccent = RGB(255, 107, 0);
  COLORREF kText = RGB(204, 204, 204);
  COLORREF kDim = RGB(136, 136, 136);
  COLORREF kHigh = RGB(70, 130, 180);
  COLORREF kOrange = RGB(255, 107, 0);

  // ── Helpers ───────────────────────────────────────────────────────────────
  void refreshUndoRedo() {
    if (!surface_)
      return;
    canUndo.set(surface_->canUndo());
    canRedo.set(surface_->canRedo());
  }
  void refreshTransformState() {
    if (!surface_)
      return;
    hasSelection.set(!surface_->selection().empty());
  }
  void syncZoom(float z) {
    double pct = z * 100.0;
    double clamped = max(pct, kZoomMin);
    clamped = min(clamped, kZoomMax);
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
  void pushTextStyleToSurface() {
    if (!surface_)
      return;
    TextStyle ts = surface_->getTextStyle();
    ts.fontSize = fontSize.get();
    ts.bold = fontBold.get();
    ts.italic = fontItalic.get();
    ts.underline = fontUnderline.get();
    const std::string &ff = fontFace.get();
    if (!ff.empty())
      ts.fontFace = std::wstring(ff.begin(), ff.end());
    // Sync color from fill
    RGBA fc = parseHexColor(fillColor.get());
    ts.r = fc.r;
    ts.g = fc.g;
    ts.b = fc.b;
    surface_->setTextStyle(ts);
  }
  void toggleTransformMode() {
    if (!surface_ || surface_->selection().empty())
      return;
    vmath::AABB bb;
    for (auto id : surface_->selection()) {
      auto *s = surface_->beginEdit(id);
      if (s) {
        auto sb = vtess::shapeAABB(*s);
        if (sb.valid()) {
          bb.expand({sb.x0, sb.y0});
          bb.expand({sb.x1, sb.y1});
        }
      }
    }
    if (!bb.valid())
      return;
    vmath::Vec2 c = bb.center();
    surface_->onMouseDown(c.x, c.y);
    surface_->onMouseUp(c.x, c.y);
    transformMode.set(transformMode.get() == "rotate" ? "scale" : "rotate");
    if (canvasPtr_)
      canvasPtr_->redraw();
  }

  void pushGradientToSurface() {
    if (!surface_)
      return;
    VFill fill;
    fill.none = fillNone.get();
    fill.color = parseHexColor(fillColor.get());

    if (useGradient.get()) {
      fill.mode = VFillMode::LinearGradient;
      fill.gx0 = 0.f;
      fill.gy0 = 0.5f;
      fill.gx1 = 1.f;
      fill.gy1 = 0.5f;

      auto makeStop = [](const std::string &hex, double pos) -> GradientStop {
        return {parseHexColor(hex), float(pos)};
      };
      fill.stops.push_back(makeStop(stopColor0.get(), stopPos0.get()));
      fill.stops.push_back(makeStop(stopColor1.get(), stopPos1.get()));
      int cnt = stopCount.get();
      if (cnt >= 3)
        fill.stops.push_back(makeStop(stopColor2.get(), stopPos2.get()));
      if (cnt >= 4)
        fill.stops.push_back(makeStop(stopColor3.get(), stopPos3.get()));

      // Sort stops by position
      std::sort(fill.stops.begin(), fill.stops.end(),
                [](const GradientStop &a, const GradientStop &b) {
                  return a.position < b.position;
                });
    }
    surface_->setActiveFill(fill);
  }

  void setCurrentStopPos(double v) {
    switch (selectedStop.get()) {
    case 0:
      stopPos0.set(v);
      break;
    case 1:
      stopPos1.set(v);
      break;
    case 2:
      stopPos2.set(v);
      break;
    case 3:
      stopPos3.set(v);
      break;
    }
  }
  double getCurrentStopPos() const {
    switch (selectedStop.get()) {
    case 0:
      return stopPos0.get();
    case 1:
      return stopPos1.get();
    case 2:
      return stopPos2.get();
    default:
      return stopPos3.get();
    }
  }

public:
  VectorApp()
      : activeTool(VTool::Select, context), fillColor("#FFFFFF", context),
        strokeColor("#000000", context), strokeWidth(4.0, context),
        fillNone(false, context), strokeNone(false, context),
        zoomLevel(100.0, context), canUndo(false, context),
        canRedo(false, context), transformMode("scale", context),
        hasSelection(false, context), fontSize(36, context),
        fontBold(false, context), fontItalic(false, context),
        fontUnderline(false, context), fontFace("Arial", context),
        useGradient(false, context), selectedStop(0, context),
        stopCount(2, context), stopColor0("#FFFFFF", context),
        stopColor1("#000000", context), stopColor2("#888888", context),
        stopColor3("#444444", context), stopPos0(0.0, context),
        stopPos1(1.0, context), stopPos2(0.33, context),
        stopPos3(0.66, context), currentStopPos(0.0, context) {}

  WidgetPtr build() override {
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);

    int canvasAreaW = screenW - kToolbarW - kPanelW;
    int canvasAreaH = screenH - kAppBarH - kCtrlBarH - kStatusH;
    static constexpr int kPaperW = 1123, kPaperH = 1587; // A3 @ 96 dpi

    auto canvas = VectorCanvas(canvasAreaW, canvasAreaH, kPaperW, kPaperH);
    surface_ = canvas->setSurface<VectorSurface>();
    canvasPtr_ = canvas.get();
    canvas->setScrollbarsEnabled(false);
    pushStyleToSurface();
    pushTextStyleToSurface();

    canvas->onViewportChanged = [this](float z) {
      syncZoom(z);
      if (surface_)
        surface_->setZoom(z);
    };

    activeTool.listen([this](VTool t) {
      if (surface_)
        surface_->setTool(t);
      if (t != VTool::Select) {
        transformMode.set("scale");
        hasSelection.set(false);
      }
    });
    fillColor.listen([this](const std::string &) {
      pushStyleToSurface();
      pushTextStyleToSurface();
    });
    strokeColor.listen([this](const std::string &) { pushStyleToSurface(); });
    strokeWidth.listen([this](double) { pushStyleToSurface(); });
    fillNone.listen([this](bool) {
      pushGradientToSurface();
      pushTextStyleToSurface();
    });
    useGradient.listen([this](bool) { pushGradientToSurface(); });
    selectedStop.listen([this](int) { /* just UI update */ });
    stopCount.listen([this](int) { pushGradientToSurface(); });
    stopColor0.listen([this](const std::string &) { pushGradientToSurface(); });
    stopColor1.listen([this](const std::string &) { pushGradientToSurface(); });
    stopColor2.listen([this](const std::string &) { pushGradientToSurface(); });
    stopColor3.listen([this](const std::string &) { pushGradientToSurface(); });
    stopPos0.listen([this](double) { pushGradientToSurface(); });
    stopPos1.listen([this](double) { pushGradientToSurface(); });
    stopPos2.listen([this](double) { pushGradientToSurface(); });
    stopPos3.listen([this](double) { pushGradientToSurface(); });
    strokeNone.listen([this](bool) { pushStyleToSurface(); });
    zoomLevel.listen([this](double pct) { applyZoom(pct); });
    fontSize.listen([this](int) { pushTextStyleToSurface(); });
    fontBold.listen([this](bool) { pushTextStyleToSurface(); });
    fontItalic.listen([this](bool) { pushTextStyleToSurface(); });
    fontUnderline.listen([this](bool) { pushTextStyleToSurface(); });
    fontFace.listen([this](const std::string &) { pushTextStyleToSurface(); });
    // When selected stop changes, sync currentStopPos to that stop's value
    selectedStop.listen(
        [this](int s) { currentStopPos.set(getCurrentStopPos()); });
    // When currentStopPos changes, write back to the right stop
    currentStopPos.listen([this](double v) { setCurrentStopPos(v); });

    // =========================================================================
    // §A  APP / MENU BAR
    // =========================================================================
    auto menuItem = [&](const std::string &label) -> WidgetPtr {
      return Container(Text(label)->setFontSize(11)->setTextColor(kText))
          ->setPaddingAll(12, 0, 12, 0)
          ->setHeight(kAppBarH);
    };
    auto appBar =
        Container(Row(Container(Text("Ai")
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
    // §B  CONTROL BAR
    // =========================================================================
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

    auto editCluster = Row(Button("↩",
                                  [this] {
                                    if (surface_) {
                                      surface_->undo();
                                      refreshUndoRedo();
                                      refreshTransformState();
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
                                      refreshTransformState();
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
                                      for (auto &s : surface_->scene())
                                        surface_->removeShape(s.id);
                                      refreshUndoRedo();
                                      refreshTransformState();
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
        Container(Row(zoomCluster, SizedBox(14, 0),
                      Container(nullptr)
                          ->setWidth(1)
                          ->setHeight(20)
                          ->setBackgroundColor(kBorder),
                      SizedBox(14, 0), editCluster)
                      ->setSpacing(0)
                      ->setCrossAxisAlignment(CrossAxisAlignment::Center))
            ->setBackgroundColor(kPanel)
            ->setPaddingAll(8, 6, 8, 6)
            ->setBorderWidth(1)
            ->setBorderColor(kBorder)
            ->setHeight(kCtrlBarH);

    // =========================================================================
    // §C  LEFT TOOL STRIP
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
              ->setOnTap([this, tv]() {
                activeTool.set(tv);
                if (tv != VTool::Select) {
                  transformMode.set("scale");
                  hasSelection.set(false);
                }
              }),
          tip);
    };
    auto sep = Container(nullptr)
                   ->setWidth(kToolbarW - 16)
                   ->setHeight(1)
                   ->setBackgroundColor(RGB(60, 60, 60));

    auto toolStrip =
        Container(
            Column(SizedBox(0, 6),
                   makeTool(VTool::Select, "↖", "Selection (V)"),
                   SizedBox(0, 2),
                   makeTool(VTool::Node, "◆", "Direct Selection (A)"),
                   SizedBox(0, 8), sep, SizedBox(0, 8),
                   makeTool(VTool::Pen, "✒", "Pen (P)"), SizedBox(0, 2),
                   makeTool(VTool::Text, "T", "Type (T)"), SizedBox(0, 2),
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
    // §D  CANVAS with context menu
    // =========================================================================
    auto canvasWithMenu = ContextMenu(
        canvas,
        {
            {"Select", [this] { activeTool.set(VTool::Select); }},
            {"Type (T)", [this] { activeTool.set(VTool::Text); }},
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
                 refreshTransformState();
                 if (canvasPtr_)
                   canvasPtr_->redraw();
               }
             }},
            {"Redo",
             [this] {
               if (surface_) {
                 surface_->redo();
                 refreshUndoRedo();
                 refreshTransformState();
                 if (canvasPtr_)
                   canvasPtr_->redraw();
               }
             }},
            ContextMenuItem::Separator(),
            {"Bring to Front",
             [this] {
               if (!surface_)
                 return;
               for (auto id : surface_->selection())
                 surface_->bringToFront(id);
               if (canvasPtr_)
                 canvasPtr_->redraw();
             }},
            {"Send to Back",
             [this] {
               if (!surface_)
                 return;
               for (auto id : surface_->selection())
                 surface_->sendToBack(id);
               if (canvasPtr_)
                 canvasPtr_->redraw();
             }},
            ContextMenuItem::Separator(),
            {"Select All",
             [this] {
               if (surface_) {
                 surface_->selectAll();
                 hasSelection.set(!surface_->selection().empty());
                 if (canvasPtr_)
                   canvasPtr_->redraw();
               }
             }},
            {"Deselect All",
             [this] {
               if (surface_) {
                 surface_->deselectAll();
                 transformMode.set("scale");
                 hasSelection.set(false);
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
            ContextMenuItem::Separator(),
            {"Unite",
             [this] {
               if (surface_) {
                 surface_->booleanOp(VectorSurface::BooleanOp::Unite);
                 refreshUndoRedo();
                 refreshTransformState();
                 if (canvasPtr_)
                   canvasPtr_->redraw();
               }
             }},
            {"Subtract",
             [this] {
               if (surface_) {
                 surface_->booleanOp(VectorSurface::BooleanOp::Subtract);
                 refreshUndoRedo();
                 refreshTransformState();
                 if (canvasPtr_)
                   canvasPtr_->redraw();
               }
             }},
            {"Intersect",
             [this] {
               if (surface_) {
                 surface_->booleanOp(VectorSurface::BooleanOp::Intersect);
                 refreshUndoRedo();
                 refreshTransformState();
                 if (canvasPtr_)
                   canvasPtr_->redraw();
               }
             }},
            {"Xor",
             [this] {
               if (surface_) {
                 surface_->booleanOp(VectorSurface::BooleanOp::Xor);
                 refreshUndoRedo();
                 refreshTransformState();
                 if (canvasPtr_)
                   canvasPtr_->redraw();
               }
             }},
            {"Outline Stroke",
             [this] {
               if (surface_ && !surface_->selection().empty()) {
                 surface_->outlineStroke(surface_->selection().front());
                 refreshUndoRedo();
                 refreshTransformState();
                 if (canvasPtr_)
                   canvasPtr_->redraw();
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

    // ── Fill ─────────────────────────────────────────────────────────────────
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

    // ── Stroke
    // ────────────────────────────────────────────────────────────────
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

    // ── Selection & Transform
    // ─────────────────────────────────────────────────
    auto modeBadgeButton = Tooltip(
        GestureDetector(
            Container(Row(Text(transformMode,
                               [](const std::string &m) -> std::string {
                                 return m == "rotate" ? "⟳" : "⇲";
                               })
                              ->setFontSize(18)
                              ->setTextColor(transformMode,
                                             [](const std::string &m) {
                                               return m == "rotate"
                                                          ? RGB(255, 200, 60)
                                                          : RGB(100, 160, 255);
                                             }),
                          SizedBox(10, 0),
                          Column(Text(transformMode,
                                      [](const std::string &m) -> std::string {
                                        return m == "rotate" ? "Rotate Mode"
                                                             : "Scale Mode";
                                      })
                                     ->setFontSize(11)
                                     ->setFontWeight(FontWeight::Bold)
                                     ->setTextColor(transformMode,
                                                    [](const std::string &m) {
                                                      return m == "rotate"
                                                                 ? RGB(255, 200,
                                                                       60)
                                                                 : RGB(100, 160,
                                                                       255);
                                                    }),
                                 SizedBox(0, 2),
                                 Text("Click to switch mode")
                                     ->setFontSize(9)
                                     ->setTextColor(RGB(110, 110, 110)))
                              ->setSpacing(0))
                          ->setSpacing(0)
                          ->setCrossAxisAlignment(CrossAxisAlignment::Center))
                ->setPaddingAll(10, 9, 10, 9)
                ->setBorderRadius(5)
                ->setBackgroundColor(transformMode,
                                     [](const std::string &m) {
                                       return m == "rotate" ? RGB(58, 42, 14)
                                                            : RGB(20, 38, 66);
                                     })
                ->setBorderWidth(1)
                ->setBorderColor(transformMode,
                                 [](const std::string &m) {
                                   return m == "rotate" ? RGB(155, 108, 22)
                                                        : RGB(42, 82, 152);
                                 }))
            ->setOnTap([this] { toggleTransformMode(); }),
        "Toggle Scale / Rotate mode");

    auto transformSection =
        Container(
            Column(
                modeBadgeButton, SizedBox(0, 8),
                Text(
                    transformMode,
                    [](const std::string &m) -> std::string {
                      return m == "rotate"
                                 ? "Drag orange circle handles to "
                                   "rotate.\nDrag the crosshair pivot to "
                                   "move\nthe rotation origin.\nShift snaps "
                                   "rotation to 45°."
                                 : "Drag white square handles to scale.\nShift "
                                   "= uniform scale.\nClick the shape again on "
                                   "canvas\nto switch to Rotate mode.";
                    })
                    ->setFontSize(9)
                    ->setTextColor(kDim),
                SizedBox(0, 12),
                Row(Text("Order:")
                        ->setFontSize(10)
                        ->setTextColor(kDim)
                        ->setMinWidth(50),
                    SizedBox(6, 0),
                    Tooltip(Button("▲ Front",
                                   [this] {
                                     if (!surface_)
                                       return;
                                     for (auto id : surface_->selection())
                                       surface_->bringToFront(id);
                                     if (canvasPtr_)
                                       canvasPtr_->redraw();
                                   })
                                ->setBackgroundColor(RGB(55, 55, 55))
                                ->setTextColor(kText)
                                ->setBorderRadius(3)
                                ->setHeight(22)
                                ->setPadding(6),
                            "Bring to Front"),
                    SizedBox(4, 0),
                    Tooltip(Button("▼ Back",
                                   [this] {
                                     if (!surface_)
                                       return;
                                     for (auto id : surface_->selection())
                                       surface_->sendToBack(id);
                                     if (canvasPtr_)
                                       canvasPtr_->redraw();
                                   })
                                ->setBackgroundColor(RGB(55, 55, 55))
                                ->setTextColor(kText)
                                ->setBorderRadius(3)
                                ->setHeight(22)
                                ->setPadding(6),
                            "Send to Back"))
                    ->setSpacing(0)
                    ->setCrossAxisAlignment(CrossAxisAlignment::Center),
                SizedBox(0, 8),
                Row(Tooltip(Button("Select All",
                                   [this] {
                                     if (surface_) {
                                       surface_->selectAll();
                                       hasSelection.set(
                                           !surface_->selection().empty());
                                       if (canvasPtr_)
                                         canvasPtr_->redraw();
                                     }
                                   })
                                ->setBackgroundColor(RGB(55, 55, 55))
                                ->setTextColor(kText)
                                ->setBorderRadius(3)
                                ->setHeight(22)
                                ->setPadding(6),
                            "Select All  (Ctrl+A)"),
                    SizedBox(4, 0),
                    Tooltip(Button("Deselect",
                                   [this] {
                                     if (surface_) {
                                       surface_->deselectAll();
                                       transformMode.set("scale");
                                       hasSelection.set(false);
                                       if (canvasPtr_)
                                         canvasPtr_->redraw();
                                     }
                                   })
                                ->setBackgroundColor(RGB(55, 55, 55))
                                ->setTextColor(kDim)
                                ->setBorderRadius(3)
                                ->setHeight(22)
                                ->setPadding(6),
                            "Deselect  (Escape)"))
                    ->setSpacing(0)
                    ->setCrossAxisAlignment(CrossAxisAlignment::Center))
                ->setSpacing(0))
            ->setPaddingAll(10, 8, 10, 8);

    // =========================================================================
    // §E2  TEXT PROPERTIES PANEL
    // =========================================================================

    // Font size spinner
    auto fontSizeRow =
        Row(Text("Size")->setFontSize(10)->setTextColor(kDim)->setMinWidth(50),
            SizedBox(6, 0),
            GestureDetector(
                Container(Text("−")->setFontSize(13)->setTextColor(kText))
                    ->setWidth(22)
                    ->setHeight(22)
                    ->setBorderRadius(3)
                    ->setBackgroundColor(RGB(55, 55, 55))
                    ->setBorderWidth(1)
                    ->setBorderColor(kBorder))
                ->setOnTap(
                    [this]() { fontSize.set(max(6, fontSize.get() - 2)); }),
            SizedBox(3, 0),
            Container(
                Center(Text(fontSize,
                            [](int v) { return std::to_string(v) + " pt"; })
                           ->setFontSize(10)
                           ->setTextColor(kText)))
                ->setWidth(52)
                ->setHeight(22)
                ->setBorderRadius(3)
                ->setBackgroundColor(RGB(37, 37, 37))
                ->setBorderWidth(1)
                ->setBorderColor(kBorder),
            SizedBox(3, 0),
            GestureDetector(
                Container(Text("+")->setFontSize(13)->setTextColor(kText))
                    ->setWidth(22)
                    ->setHeight(22)
                    ->setBorderRadius(3)
                    ->setBackgroundColor(RGB(55, 55, 55))
                    ->setBorderWidth(1)
                    ->setBorderColor(kBorder))
                ->setOnTap(
                    [this]() { fontSize.set(min(288, fontSize.get() + 2)); }))
            ->setSpacing(0)
            ->setCrossAxisAlignment(CrossAxisAlignment::Center);

    // Preset size chips
    auto makePresetSize = [&](int sz) -> WidgetPtr {
      return GestureDetector(
                 Container(Center(Text(std::to_string(sz))
                                      ->setFontSize(9)
                                      ->setTextColor(
                                          fontSize,
                                          [sz](int cur) {
                                            return cur == sz
                                                       ? RGB(255, 255, 255)
                                                       : RGB(170, 170, 170);
                                          })))
                     ->setWidth(32)
                     ->setHeight(20)
                     ->setBorderRadius(3)
                     ->setBackgroundColor(fontSize,
                                          [sz](int cur) {
                                            return cur == sz ? RGB(80, 50, 20)
                                                             : RGB(50, 50, 50);
                                          })
                     ->setBorderWidth(1)
                     ->setBorderColor(fontSize,
                                      [sz](int cur) {
                                        return cur == sz ? RGB(255, 107, 0)
                                                         : RGB(70, 70, 70);
                                      }))
          ->setOnTap([this, sz]() { fontSize.set(sz); });
    };
    auto sizePresets =
        Row(makePresetSize(12), SizedBox(3, 0), makePresetSize(18),
            SizedBox(3, 0), makePresetSize(24), SizedBox(3, 0),
            makePresetSize(36), SizedBox(3, 0), makePresetSize(48),
            SizedBox(3, 0), makePresetSize(72))
            ->setSpacing(0)
            ->setCrossAxisAlignment(CrossAxisAlignment::Center);

    // Bold / Italic / Underline toggles
    auto makeStyleToggle = [&](const std::string &label, State<bool> &state,
                               const std::string &tip) -> WidgetPtr {
      return Tooltip(
          GestureDetector(
              Container(Center(Text(label)
                                   ->setFontSize(11)
                                   ->setTextColor(state,
                                                  [](const bool &on) {
                                                    return on ? RGB(255, 255,
                                                                    255)
                                                              : RGB(150, 150,
                                                                    150);
                                                  })
                                   ->setFontWeight(FontWeight::Bold)))
                  ->setWidth(32)
                  ->setHeight(26)
                  ->setBorderRadius(3)
                  ->setBackgroundColor(state,
                                       [](const bool &on) {
                                         return on ? RGB(255, 107, 0)
                                                   : RGB(50, 50, 50);
                                       })
                  ->setBorderWidth(1)
                  ->setBorderColor(state,
                                   [](const bool &on) {
                                     return on ? RGB(255, 150, 50)
                                               : RGB(70, 70, 70);
                                   }))
              ->setOnTap([this, &state]() { state.set(!state.get()); }),
          tip);
    };

    auto styleRow =
        Row(makeStyleToggle("B", fontBold, "Bold"), SizedBox(4, 0),
            makeStyleToggle("I", fontItalic, "Italic"), SizedBox(4, 0),
            makeStyleToggle("U̲", fontUnderline, "Underline"))
            ->setSpacing(0)
            ->setCrossAxisAlignment(CrossAxisAlignment::Center);

    // Alignment hint (visual only — actual kerning is system-handled)
    auto hintRow = Row(Text("Click canvas to place text.")
                           ->setFontSize(9)
                           ->setTextColor(kDim))
                       ->setSpacing(0);

    auto keyHintRow =
        Column(Text("Type to enter text.")->setFontSize(9)->setTextColor(kDim),
               SizedBox(0, 2),
               Text("Enter = commit    Esc = cancel")
                   ->setFontSize(9)
                   ->setTextColor(kDim),
               SizedBox(0, 2),
               Text("Right-click = cancel")->setFontSize(9)->setTextColor(kDim))
            ->setSpacing(0);

    auto textPropsSection =
        Container(Column(fontSizeRow, SizedBox(0, 8), sizePresets,
                         SizedBox(0, 10), styleRow, SizedBox(0, 10), hintRow,
                         SizedBox(0, 4), keyHintRow)
                      ->setSpacing(0))
            ->setPaddingAll(10, 8, 10, 8);

    // ── Full right panel
    // ──────────────────────────────────────────────────────
    auto rightPanel =
        Container(
            ListView(
                {sectionHeader("Transform"),
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
                     ->setBackgroundColor(kPanel),
                 sectionHeader("Pathfinder"),
                 Container(
                     Column(
                         Row(Tooltip(
                                 Button(
                                     "Unite",
                                     [this] {
                                       if (surface_) {
                                         surface_->booleanOp(
                                             VectorSurface::BooleanOp::Unite);
                                         refreshUndoRedo();
                                         refreshTransformState();
                                         if (canvasPtr_)
                                           canvasPtr_->redraw();
                                       }
                                     })
                                     ->setBackgroundColor(RGB(55, 55, 55))
                                     ->setTextColor(kText)
                                     ->setBorderRadius(3)
                                     ->setHeight(22)
                                     ->setPadding(6),
                                 "Unite — merge selected shapes"),
                             SizedBox(4, 0),
                             Tooltip(Button("Subtract",
                                            [this] {
                                              if (surface_) {
                                                surface_->booleanOp(
                                                    VectorSurface::BooleanOp::
                                                        Subtract);
                                                refreshUndoRedo();
                                                refreshTransformState();
                                                if (canvasPtr_)
                                                  canvasPtr_->redraw();
                                              }
                                            })
                                         ->setBackgroundColor(RGB(55, 55, 55))
                                         ->setTextColor(kText)
                                         ->setBorderRadius(3)
                                         ->setHeight(22)
                                         ->setPadding(6),
                                     "Subtract — cut clip from subject"))
                             ->setSpacing(0)
                             ->setCrossAxisAlignment(
                                 CrossAxisAlignment::Center),
                         SizedBox(0, 6),
                         Row(Tooltip(Button("Intersect",
                                            [this] {
                                              if (surface_) {
                                                surface_->booleanOp(
                                                    VectorSurface::BooleanOp::
                                                        Intersect);
                                                refreshUndoRedo();
                                                refreshTransformState();
                                                if (canvasPtr_)
                                                  canvasPtr_->redraw();
                                              }
                                            })
                                         ->setBackgroundColor(RGB(55, 55, 55))
                                         ->setTextColor(kText)
                                         ->setBorderRadius(3)
                                         ->setHeight(22)
                                         ->setPadding(6),
                                     "Intersect — keep overlapping area only"),
                             SizedBox(4, 0),
                             Tooltip(
                                 Button("Xor",
                                        [this] {
                                          if (surface_) {
                                            surface_->booleanOp(
                                                VectorSurface::BooleanOp::Xor);
                                            refreshUndoRedo();
                                            refreshTransformState();
                                            if (canvasPtr_)
                                              canvasPtr_->redraw();
                                          }
                                        })
                                     ->setBackgroundColor(RGB(55, 55, 55))
                                     ->setTextColor(kText)
                                     ->setBorderRadius(3)
                                     ->setHeight(22)
                                     ->setPadding(6),
                                 "Xor — exclude overlapping area"))
                             ->setSpacing(0)
                             ->setCrossAxisAlignment(
                                 CrossAxisAlignment::Center),
                         SizedBox(0, 6),
                         Tooltip(
                             Button("Outline Stroke",
                                    [this] {
                                      if (surface_ &&
                                          !surface_->selection().empty()) {
                                        surface_->outlineStroke(
                                            surface_->selection().front());
                                        refreshUndoRedo();
                                        refreshTransformState();
                                        if (canvasPtr_)
                                          canvasPtr_->redraw();
                                      }
                                    })
                                 ->setBackgroundColor(RGB(55, 55, 55))
                                 ->setTextColor(kText)
                                 ->setBorderRadius(3)
                                 ->setHeight(22)
                                 ->setPadding(6),
                             "Outline Stroke — convert stroke to filled shape"))
                         ->setSpacing(0))
                     ->setPaddingAll(10, 8, 10, 8)
                     ->setBackgroundColor(kPanel),

                 sectionHeader("Selection & Transform"),
                 Container(transformSection)->setBackgroundColor(kPanel),

                 sectionHeader("Type"),
                 Container(textPropsSection)->setBackgroundColor(kPanel),

                 sectionHeader("Appearance"),
                 Container(
                     Column(
                         Container(
                             Column(
                                 // ── Fill header row
                                 // ───────────────────────────────────
                                 Row(Text("Fill")
                                         ->setFontSize(10)
                                         ->setFontWeight(FontWeight::Bold)
                                         ->setTextColor(kText),
                                     SizedBox(6, 0), fillNoneBtn,
                                     SizedBox(8, 0),
                                     // Solid / Gradient toggle
                                     GestureDetector(
                                         Container(
                                             Text(useGradient,
                                                  [](const bool &g) {
                                                    return g ? "Gradient"
                                                             : "Solid";
                                                  })
                                                 ->setFontSize(9)
                                                 ->setTextColor(
                                                     useGradient,
                                                     [](const bool &g) {
                                                       return g ? RGB(255, 200,
                                                                      60)
                                                                : RGB(150, 150,
                                                                      150);
                                                     }))
                                             ->setPaddingAll(5, 2, 5, 2)
                                             ->setBorderRadius(3)
                                             ->setBackgroundColor(
                                                 useGradient,
                                                 [](const bool &g) {
                                                   return g ? RGB(60, 45, 10)
                                                            : RGB(50, 50, 50);
                                                 })
                                             ->setBorderWidth(1)
                                             ->setBorderColor(
                                                 useGradient,
                                                 [](const bool &g) {
                                                   return g ? RGB(180, 130, 30)
                                                            : RGB(70, 70, 70);
                                                 }))
                                         ->setOnTap([this]() {
                                           useGradient.set(!useGradient.get());
                                         }))
                                     ->setSpacing(0)
                                     ->setCrossAxisAlignment(
                                         CrossAxisAlignment::Center),
                                 SizedBox(0, 8),

                                 // ── Solid color picker (shown when not
                                 // gradient) ──────
                                 Container(fillPicker)
                                     ->setVisible(
                                         useGradient,
                                         [](const bool &g) { return !g; }),

                                 // ── Gradient editor (shown when gradient)
                                 // ─────────────
                                 Container(
                                     Column(
                                         // Stop selector tabs
                                         Row(GestureDetector(
                                                 Container(
                                                     Text("1")->setFontSize(9)->setTextColor(
                                                         selectedStop,
                                                         [](const int &s) {
                                                           return s == 0
                                                                      ? RGB(255,
                                                                            255,
                                                                            255)
                                                                      : RGB(150,
                                                                            150,
                                                                            150);
                                                         }))
                                                     ->setWidth(24)
                                                     ->setHeight(20)
                                                     ->setBorderRadius(3)
                                                     ->setBackgroundColor(
                                                         selectedStop,
                                                         [](const int &s) {
                                                           return s == 0
                                                                      ? RGB(80,
                                                                            50,
                                                                            10)
                                                                      : RGB(50,
                                                                            50,
                                                                            50);
                                                         })
                                                     ->setBorderWidth(1)
                                                     ->setBorderColor(
                                                         selectedStop,
                                                         [](const int &s) {
                                                           return s == 0
                                                                      ? RGB(255,
                                                                            150,
                                                                            30)
                                                                      : RGB(70,
                                                                            70,
                                                                            70);
                                                         }))
                                                 ->setOnTap([this]() {
                                                   selectedStop.set(0);
                                                 }),
                                             SizedBox(3, 0),
                                             GestureDetector(
                                                 Container(
                                                     Text("2")->setFontSize(9)->setTextColor(
                                                         selectedStop,
                                                         [](const int &s) {
                                                           return s == 1
                                                                      ? RGB(255,
                                                                            255,
                                                                            255)
                                                                      : RGB(150,
                                                                            150,
                                                                            150);
                                                         }))
                                                     ->setWidth(24)
                                                     ->setHeight(20)
                                                     ->setBorderRadius(3)
                                                     ->setBackgroundColor(
                                                         selectedStop,
                                                         [](const int &s) {
                                                           return s == 1
                                                                      ? RGB(80,
                                                                            50,
                                                                            10)
                                                                      : RGB(50,
                                                                            50,
                                                                            50);
                                                         })
                                                     ->setBorderWidth(1)
                                                     ->setBorderColor(
                                                         selectedStop,
                                                         [](const int &s) {
                                                           return s == 1
                                                                      ? RGB(255,
                                                                            150,
                                                                            30)
                                                                      : RGB(70,
                                                                            70,
                                                                            70);
                                                         }))
                                                 ->setOnTap([this]() {
                                                   selectedStop.set(1);
                                                 }),
                                             SizedBox(3, 0),
                                             // Stop 3 — only visible when
                                             // stopCount >= 3
                                             Container(
                                                 GestureDetector(
                                                     Container(
                                                         Text("3")
                                                             ->setFontSize(9)
                                                             ->setTextColor(
                                                                 selectedStop,
                                                                 [](const int
                                                                        &s) {
                                                                   return s == 2
                                                                              ? RGB(255,
                                                                                    255,
                                                                                    255)
                                                                              : RGB(150,
                                                                                    150,
                                                                                    150);
                                                                 }))
                                                         ->setWidth(24)
                                                         ->setHeight(20)
                                                         ->setBorderRadius(3)
                                                         ->setBackgroundColor(
                                                             selectedStop,
                                                             [](const int &s) {
                                                               return s == 2
                                                                          ? RGB(80,
                                                                                50,
                                                                                10)
                                                                          : RGB(50,
                                                                                50,
                                                                                50);
                                                             })
                                                         ->setBorderWidth(1)
                                                         ->setBorderColor(
                                                             selectedStop,
                                                             [](const int &s) {
                                                               return s == 2
                                                                          ? RGB(255,
                                                                                150,
                                                                                30)
                                                                          : RGB(70,
                                                                                70,
                                                                                70);
                                                             }))
                                                     ->setOnTap([this]() {
                                                       selectedStop.set(2);
                                                     }))
                                                 ->setVisible(stopCount,
                                                              [](const int &n) {
                                                                return n >= 3;
                                                              }),
                                             SizedBox(3, 0),
                                             // Stop 4
                                             Container(
                                                 GestureDetector(
                                                     Container(
                                                         Text("4")
                                                             ->setFontSize(9)
                                                             ->setTextColor(
                                                                 selectedStop,
                                                                 [](const int
                                                                        &s) {
                                                                   return s == 3
                                                                              ? RGB(255,
                                                                                    255,
                                                                                    255)
                                                                              : RGB(150,
                                                                                    150,
                                                                                    150);
                                                                 }))
                                                         ->setWidth(24)
                                                         ->setHeight(20)
                                                         ->setBorderRadius(3)
                                                         ->setBackgroundColor(
                                                             selectedStop,
                                                             [](const int &s) {
                                                               return s == 3
                                                                          ? RGB(80,
                                                                                50,
                                                                                10)
                                                                          : RGB(50,
                                                                                50,
                                                                                50);
                                                             })
                                                         ->setBorderWidth(1)
                                                         ->setBorderColor(
                                                             selectedStop,
                                                             [](const int &s) {
                                                               return s == 3
                                                                          ? RGB(255,
                                                                                150,
                                                                                30)
                                                                          : RGB(70,
                                                                                70,
                                                                                70);
                                                             }))
                                                     ->setOnTap([this]() {
                                                       selectedStop.set(3);
                                                     }))
                                                 ->setVisible(stopCount,
                                                              [](const int &n) {
                                                                return n >= 4;
                                                              }),
                                             SizedBox(8, 0),
                                             // Add / Remove stop buttons
                                             Tooltip(
                                                 GestureDetector(
                                                     Container(
                                                         Text("+")
                                                             ->setFontSize(11)
                                                             ->setTextColor(
                                                                 kText))
                                                         ->setWidth(20)
                                                         ->setHeight(20)
                                                         ->setBorderRadius(3)
                                                         ->setBackgroundColor(
                                                             RGB(55, 55, 55))
                                                         ->setBorderWidth(1)
                                                         ->setBorderColor(
                                                             kBorder))
                                                     ->setOnTap([this]() {
                                                       if (stopCount.get() <
                                                           4) {
                                                         stopCount.set(
                                                             stopCount.get() +
                                                             1);
                                                         selectedStop.set(
                                                             stopCount.get() -
                                                             1);
                                                       }
                                                     }),
                                                 "Add stop"),
                                             SizedBox(3, 0),
                                             Tooltip(
                                                 GestureDetector(
                                                     Container(
                                                         Text("−")
                                                             ->setFontSize(11)
                                                             ->setTextColor(
                                                                 kText))
                                                         ->setWidth(20)
                                                         ->setHeight(20)
                                                         ->setBorderRadius(3)
                                                         ->setBackgroundColor(
                                                             RGB(55, 55, 55))
                                                         ->setBorderWidth(1)
                                                         ->setBorderColor(
                                                             kBorder))
                                                     ->setOnTap([this]() {
                                                       if (stopCount.get() >
                                                           2) {
                                                         if (selectedStop
                                                                 .get() >=
                                                             stopCount.get() -
                                                                 1)
                                                           selectedStop.set(
                                                               stopCount.get() -
                                                               2);
                                                         stopCount.set(
                                                             stopCount.get() -
                                                             1);
                                                       }
                                                     }),
                                                 "Remove stop"))
                                             ->setSpacing(0)
                                             ->setCrossAxisAlignment(
                                                 CrossAxisAlignment::Center),
                                         SizedBox(0, 8),

                                         // Color picker for selected stop
                                         ColorPicker(hexToRef(stopColor0.get()))
                                             ->setShowAlpha(false)
                                             ->setOnColorChanged(
                                                 [this](COLORREF c) {
                                                   char buf[8];
                                                   _snprintf_s(buf, sizeof(buf),
                                                               _TRUNCATE,
                                                               "#%02x%02x%02x",
                                                               GetRValue(c),
                                                               GetGValue(c),
                                                               GetBValue(c));
                                                   switch (selectedStop.get()) {
                                                   case 0:
                                                     stopColor0.set(buf);
                                                     break;
                                                   case 1:
                                                     stopColor1.set(buf);
                                                     break;
                                                   case 2:
                                                     stopColor2.set(buf);
                                                     break;
                                                   case 3:
                                                     stopColor3.set(buf);
                                                     break;
                                                   }
                                                 }),
                                         SizedBox(0, 8),

                                         // Position slider for selected stop
                                         Row(Text("Pos")
                                                 ->setFontSize(9)
                                                 ->setTextColor(kDim)
                                                 ->setMinWidth(26),
                                             SizedBox(4, 0),
                                             Slider(0.0, 1.0, 0.01)
                                                 ->setValue(currentStopPos)
                                                 ->setTrackFillColor(kAccent)
                                                 ->setWidth(kPanelW - 80),
                                             SizedBox(4, 0),
                                             Text(currentStopPos,
                                                  [](double v) {
                                                    char b[8];
                                                    snprintf(b, 8, "%.2f", v);
                                                    return std::string(b);
                                                  })

                                                 ->setTextColor(kText)

                                                 ->setFontSize(9)

                                                 ->setMinWidth(30))
                                             ->setSpacing(0)
                                             ->setCrossAxisAlignment(
                                                 CrossAxisAlignment::Center))
                                         ->setSpacing(0))
                                     ->setVisible(
                                         useGradient,
                                         [](const bool &g) { return g; }))
                                 ->setSpacing(0))
                             ->setPaddingAll(10, 8, 10, 8),
                         Container(nullptr)->setHeight(1)->setBackgroundColor(
                             kBorder),
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
                     ->setBackgroundColor(kPanel)})
                ->setSpacing(0))
            ->setWidth(kPanelW)
            ->setBackgroundColor(kPanel)
            ->setBorderWidth(1)
            ->setBorderColor(kBorder);

    // =========================================================================
    // §F  STATUS BAR
    // =========================================================================
    auto statusBar =
        Container(
            Row(Text("Ready")->setFontSize(9)->setTextColor(kDim),
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
                       case VTool::Text:
                         return "Tool: Type  [click canvas → type → Enter]";
                       default:
                         return "";
                       }
                     })
                    ->setFontSize(9)
                    ->setTextColor(activeTool,
                                   [](const VTool &t) {
                                     return t == VTool::Text
                                                ? RGB(100, 200, 255)
                                                : RGB(136, 136, 136);
                                   }),
                SizedBox(16, 0),
                Text(transformMode,
                     [](const std::string &m) -> std::string {
                       return m == "rotate" ? "| ⟳ Rotate mode"
                                            : "| ⇲ Scale mode";
                     })
                    ->setFontSize(9)
                    ->setTextColor(transformMode,
                                   [this](const std::string &m) {
                                     return m == "rotate" ? RGB(255, 200, 60)
                                                          : kDim;
                                   }),
                SizedBox(16, 0),
                Text(hasSelection,
                     [](const bool &sel) -> std::string {
                       return sel ? "| Pathfinder: select 2+ shapes" : "";
                     })
                    ->setFontSize(9)
                    ->setTextColor(RGB(100, 180, 100)))
                ->setSpacing(0)
                ->setCrossAxisAlignment(CrossAxisAlignment::Center))
            ->setBackgroundColor(kDark)
            ->setPaddingAll(10, 4, 10, 4)
            ->setBorderWidth(1)
            ->setBorderColor(kBorder)
            ->setHeight(kStatusH);

    // =========================================================================
    // §G  FINAL LAYOUT
    // =========================================================================
    auto centerRow =
        Row(toolStrip, Container(canvasWithMenu)->setBackgroundColor(kBg),
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