// test_paint_viewport.cpp
#include "flux.hpp"
#include <commdlg.h> // GetSaveFileName
#include <shlobj.h>  // SHGetKnownFolderPath (for default save dir)
#pragma comment(lib, "comdlg32.lib")

// ── App-level tool enum ──────────────────────────────────────────────────────
//
// Values must match the kTool* constants from flux_canvas.hpp for the two
// built-in behaviours.  New tools (Line, Rect, Fill, ...) get fresh IDs ≥ 2
// and are handled entirely in PaintSurface / PaintApp.
//
enum class Tool {
  Brush = kToolBrush,   // 0 — built-in paint
  Eraser = kToolEraser, // 1 — built-in erase
  Line = 2,             // app-handled: draw a straight line on mouse-up
  Rect = 3,             // app-handled: draw a filled/stroked rectangle
  // Add more here freely without touching flux_canvas.hpp
};

// ── Hex ↔ COLORREF helpers ───────────────────────────────────────────────────
static COLORREF hexToRef(const std::string &css) {
  RGBA c = parseHexColor(css);
  return RGB((BYTE)(c.r * 255), (BYTE)(c.g * 255), (BYTE)(c.b * 255));
}
static std::string refToHex(COLORREF c) {
  return ColorToHex(c); // from flux_colorpicker.hpp
}

// ── Save PNG via common dialog ───────────────────────────────────────────────
// Returns wide path chosen by user, or empty string if cancelled.
static std::wstring promptSavePath(HWND owner) {
  wchar_t buf[MAX_PATH] = L"painting.png";
  OPENFILENAMEW ofn{};
  ofn.lStructSize = sizeof(ofn);
  ofn.hwndOwner = owner;
  ofn.lpstrFilter = L"PNG Image\0*.png\0All Files\0*.*\0";
  ofn.lpstrFile = buf;
  ofn.nMaxFile = MAX_PATH;
  ofn.lpstrDefExt = L"png";
  ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
  ofn.lpstrTitle = L"Save as PNG";
  if (GetSaveFileNameW(&ofn))
    return buf;
  return {};
}

// ============================================================================
// PaintSurface
// ============================================================================

class PaintSurface : public RasterSurface {
public:
  const std::vector<std::pair<std::string, std::string>> kPalette = {
      {"#cba6f7", "Purple"}, {"#f38ba8", "Pink"},  {"#fab387", "Peach"},
      {"#f9e2af", "Yellow"}, {"#a6e3a1", "Green"}, {"#89dceb", "Sky"},
      {"#89b4fa", "Blue"},   {"#ffffff", "White"}, {"#6c7086", "Gray"},
      {"#1e1e2e", "Black"},
  };

  std::string activeColor = "#cba6f7";
  float activeSize = 4.f;
  float activeOpacity = 1.0f; // 0..1
  Tool activeTool = Tool::Brush;

  std::function<void()> onStateChanged;
  std::function<void(const std::vector<RGBA> &)> onHistoryChanged;

  void setColor(const std::string &col) {
    activeColor = col;
    syncStyle();
  }
  void setSize(float sz) {
    activeSize = sz;
    syncStyle();
  }
  void setOpacity(float op) {
    activeOpacity = op;
    syncStyle();
  }
  void setActiveTool(Tool t) {
    activeTool = t;
    setTool(static_cast<ToolId>(t)); // cast app enum → infrastructure ToolId
    syncStyle();
  }

  void initialize(int w, int h) override {
    RasterSurface::initialize(w, h);
    syncStyle();
  }

  void onMouseDown(float x, float y) override {
    RasterSurface::onMouseDown(x, y);
  }
  void onMouseUp(float x, float y) override {
    RasterSurface::onMouseUp(x, y);
    if (onStateChanged)
      onStateChanged();
    if (onHistoryChanged)
      onHistoryChanged(colorHistory());
  }
  void onKeyDown(int key) override {
    RasterSurface::onKeyDown(key);
    if (onStateChanged)
      onStateChanged();

    // E = eraser, B = brush (quick-switch shortcuts)
    if (key == 'E') {
      activeTool = Tool::Eraser;
      setTool(static_cast<ToolId>(Tool::Eraser));
    } else if (key == 'B') {
      activeTool = Tool::Brush;
      setTool(static_cast<ToolId>(Tool::Brush));
      syncStyle();
    }
    if (onStateChanged)
      onStateChanged();
  }

private:
  void syncStyle() {
    RGBA c = parseHexColor(activeColor);
    StrokeStyle s;
    s.r = c.r;
    s.g = c.g;
    s.b = c.b;
    s.a = 1.f;
    s.radius = activeSize * 0.5f;
    s.opacity = activeOpacity;
    s.tool = static_cast<ToolId>(activeTool);
    setStrokeStyle(s);
    setTool(static_cast<ToolId>(activeTool));
    RasterSurface::setOpacity(activeOpacity);
  }
};

// ============================================================================
// PaintApp
// ============================================================================

class PaintApp : public Component {
  State<std::string> activeColor;
  State<float> activeSize;
  State<double> activeOpacity;
  State<Tool> activeTool;
  State<bool> canUndo, canRedo;
  State<double> zoomLevel;
  State<std::vector<RGBA>> colorHistory;

  std::shared_ptr<PaintSurface> surface_;
  CanvasWidget *canvasPtr_ = nullptr;
  HWND mainHwnd_ = nullptr; // for save dialog owner

  static constexpr double kZoomMin = 6.25;
  static constexpr double kZoomMax = 200.0;

  static constexpr int kSidebarWidth = 240;
  static constexpr int kToolbarHeight = 58;
  static constexpr int kHintsHeight = 24;
  static constexpr int kAppBarHeight = 32;

  void refreshUndoRedo() {
    if (!surface_)
      return;
    canUndo.set(surface_->canUndo());
    canRedo.set(surface_->canRedo());
  }
  void syncZoomState(float zoom) {
    double pct = double(zoom) * 100.0;
    if (std::abs(pct - zoomLevel.get()) > 0.5)
      zoomLevel.set(pct);
  }
  void applyZoomFromSlider(double pct) {
    if (!canvasPtr_)
      return;
    Viewport &vp = canvasPtr_->viewport();
    float target = float(pct / 100.0);
    float current = vp.zoom();
    if (std::abs(target - current) < 0.0001f)
      return;
    vp.zoomToward(vp.viewW() * 0.5f, vp.viewH() * 0.5f, target / current);
    canvasPtr_->redraw();
  }

  void doSavePNG() {
    if (!surface_ || !canvasPtr_)
      return;
    // Ensure GL context is current before reading pixels
    std::wstring path = promptSavePath(mainHwnd_);
    if (path.empty())
      return;
    bool ok = surface_->savePNG(path);
    if (!ok) {
      MessageBoxW(mainHwnd_, L"Failed to save PNG.", L"Error",
                  MB_ICONERROR | MB_OK);
    }
  }

public:
  PaintApp()
      : activeColor("#cba6f7", context), activeSize(4.f, context),
        activeOpacity(1.0f, context), activeTool(Tool::Brush, context),
        canUndo(false, context), canRedo(false, context),
        zoomLevel(100.0, context), colorHistory({}, context) {}

  WidgetPtr build() override {

    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int viewW = screenW - kSidebarWidth;
    int viewH = screenH - kAppBarHeight - kToolbarHeight - kHintsHeight;

    static constexpr int kPaperW = 1240;
    static constexpr int kPaperH = 1754;

    // ── Canvas ────────────────────────────────────────────────────────────
    auto canvas = RasterCanvas(viewW, viewH, kPaperW, kPaperH);
    surface_ = canvas->setSurface<PaintSurface>();
    canvasPtr_ = canvas.get();

    // Grab main HWND for save dialog — GetActiveWindow() works before first
    // paint
    mainHwnd_ = GetActiveWindow();

    surface_->onStateChanged = [this]() { refreshUndoRedo(); };
    surface_->onHistoryChanged = [this](const std::vector<RGBA> &h) {
      colorHistory.set(h);
    };
    canvas->onViewportChanged = [this](float z) { syncZoomState(z); };

    // State → surface bindings
    activeColor.listen([this](const std::string &col) {
      if (surface_)
        surface_->setColor(col);
    });
    activeSize.listen([this](float sz) {
      if (surface_)
        surface_->setSize(sz);
    });
    activeOpacity.listen([this](float op) {
      if (surface_)
        surface_->setOpacity(op);
    });
    activeTool.listen([this](Tool t) {
      if (surface_)
        surface_->setActiveTool(t);
    });
    zoomLevel.listen([this](double pct) { applyZoomFromSlider(pct); });

    // =====================================================================
    // LEFT SIDEBAR
    // =====================================================================

    auto sectionLabel = [](const std::string &txt) -> WidgetPtr {
      return Text(txt)
          ->setFontSize(9)
          ->setTextColor(RGB(100, 105, 125))
          ->setFontWeight(FontWeight::Bold);
    };

    // ── Tool selector (Brush / Eraser) ────────────────────────────────────
    auto toolBtn = [&](Tool t, const std::string &icon,
                       const std::string &label) -> WidgetPtr {
      Tool tv = t;
      return GestureDetector(
                 Container(
                     Column(Text(icon)->setFontSize(18)->setTextColor(
                                activeTool,
                                [tv](const Tool &at) {
                                  return at == tv ? RGB(203, 166, 247)
                                                  : RGB(150, 150, 170);
                                }),
                            SizedBox(0, 2),
                            Text(label)->setFontSize(9)->setTextColor(
                                activeTool,
                                [tv](const Tool &at) {
                                  return at == tv ? RGB(220, 200, 255)
                                                  : RGB(110, 110, 130);
                                }))
                         ->setSpacing(0)
                         ->setCrossAxisAlignment(CrossAxisAlignment::Center))
                     ->setWidth(72)
                     ->setHeight(52)
                     ->setBorderRadius(8)
                     ->setBackgroundColor(activeTool,
                                          [tv](const Tool &at) {
                                            return at == tv ? RGB(40, 35, 58)
                                                            : RGB(24, 24, 37);
                                          })
                     ->setBorderWidth(
                         activeTool,
                         [tv](const Tool &at) { return at == tv ? 2 : 1; })
                     ->setBorderColor(activeTool,
                                      [tv](const Tool &at) {
                                        return at == tv ? RGB(147, 112, 219)
                                                        : RGB(49, 50, 68);
                                      }))
          ->setOnTap([this, tv]() { activeTool.set(tv); });
    };

    auto toolSection =
        Container(
            Column(sectionLabel("TOOL"), SizedBox(0, 8),
                   Row(toolBtn(Tool::Brush, "🖌", "Brush"), SizedBox(8, 0),
                       toolBtn(Tool::Eraser, "⬜", "Eraser"))
                       ->setSpacing(0))
                ->setSpacing(0))
            ->setBackgroundColor(RGB(24, 24, 37))
            ->setBorderRadius(8)
            ->setBorderWidth(1)
            ->setBorderColor(RGB(49, 50, 68))
            ->setPaddingAll(10, 10, 10, 10);

    // ── Selected color display ────────────────────────────────────────────
    auto selectedSwatch =
        Container(
            Row(Container(nullptr)
                    ->setWidth(40)
                    ->setHeight(40)
                    ->setBorderRadius(6)
                    ->setBackgroundColor(
                        activeColor,
                        [](const std::string &c) { return hexToRef(c); })
                    ->setBorderWidth(1)
                    ->setBorderColor(RGB(69, 71, 90)),
                SizedBox(10, 0),
                Column(Text("Selected")
                           ->setFontSize(10)
                           ->setTextColor(RGB(100, 105, 125)),
                       SizedBox(0, 4),
                       Text(activeColor, [](const std::string &c) { return c; })
                           ->setFontSize(12)
                           ->setTextColor(RGB(205, 214, 244)))
                    ->setSpacing(0))
                ->setSpacing(0)
                ->setCrossAxisAlignment(CrossAxisAlignment::Center))
            ->setBackgroundColor(RGB(24, 24, 37))
            ->setBorderRadius(8)
            ->setBorderWidth(1)
            ->setBorderColor(RGB(49, 50, 68))
            ->setPaddingAll(10, 10, 10, 10);

    // ── Inline color picker ───────────────────────────────────────────────
    auto picker = ColorPicker(hexToRef(activeColor.get()))
                      ->setShowAlpha(false)
                      ->setOnColorChanged(
                          [this](COLORREF c) { activeColor.set(refToHex(c)); });

    auto pickerSection = Container(picker)
                             ->setBackgroundColor(RGB(24, 24, 37))
                             ->setBorderRadius(8)
                             ->setBorderWidth(1)
                             ->setBorderColor(RGB(49, 50, 68));

    // ── Color history ─────────────────────────────────────────────────────
    // Horizontal ListView driven by colorHistory state.
    // Each swatch is 22×22; the list is fixed-height at 26px so it sits
    // flush inside the section card without a visible scrollbar track.
    auto historyList =
        ListView(colorHistory)
            ->itemBuilder([this](int /*i*/, const RGBA &c) -> WidgetPtr {
              char hex[8];
              _snprintf_s(hex, sizeof(hex), _TRUNCATE, "#%02x%02x%02x",
                          (int)(c.r * 255), (int)(c.g * 255), (int)(c.b * 255));
              std::string col = hex;
              return GestureDetector(Container(nullptr)
                                         ->setWidth(22)
                                         ->setHeight(22)
                                         ->setBorderRadius(11)
                                         ->setBackgroundColor(hexToRef(col))
                                         ->setBorderWidth(1)
                                         ->setBorderColor(RGB(80, 80, 100)))
                  ->setOnTap([this, col]() { activeColor.set(col); });
            })
            ->setHorizontal(true)
            ->setSpacing(4)
            ->setScrollbarSize(3)
            ->setScrollbarColor(RGB(60, 62, 80))
            ->setScrollbarHoverColor(RGB(100, 102, 120));

    auto historySection =
        Container(
            Column(sectionLabel("RECENT COLORS"), SizedBox(0, 8),
                   Conditional(
                       colorHistory,
                       [](const std::vector<RGBA> &h) { return !h.empty(); })
                       ->Then([&] { return SizedBox(196, 26, historyList); })
                       ->Else([&] {
                         return Text("Paint a stroke to record colors")
                             ->setFontSize(9)
                             ->setTextColor(RGB(70, 72, 90));
                       }))
                ->setSpacing(0))
            ->setBackgroundColor(RGB(24, 24, 37))
            ->setBorderRadius(8)
            ->setBorderWidth(1)
            ->setBorderColor(RGB(49, 50, 68))
            ->setPaddingAll(10, 10, 10, 10);

    // ── Palette ───────────────────────────────────────────────────────────
    auto pcol1 = std::make_shared<ColumnWidget>();
    auto pcol2 = std::make_shared<ColumnWidget>();
    pcol1->setSpacing(4);
    pcol2->setSpacing(4);

    for (int i = 0; i < (int)surface_->kPalette.size(); i++) {
      const auto &[col, label] = surface_->kPalette[i];
      std::string c = col;
      auto sw =
          GestureDetector(Container(nullptr)
                              ->setWidth(22)
                              ->setHeight(22)
                              ->setBorderRadius(11)
                              ->setBackgroundColor(hexToRef(col))
                              ->setBorderWidth(activeColor,
                                               [c](const std::string &a) {
                                                 return a == c ? 2 : 0;
                                               })
                              ->setBorderColor(activeColor,
                                               [c](const std::string &a) {
                                                 return a == c
                                                            ? RGB(255, 255, 255)
                                                            : RGB(0, 0, 0);
                                               }))
              ->setOnTap([this, c]() { activeColor.set(c); });
      if (i % 2 == 0)
        pcol1->addChild(sw);
      else
        pcol2->addChild(sw);
    }

    auto paletteSection =
        Container(Column(sectionLabel("PRESETS"), SizedBox(0, 8),
                         Row(pcol1, SizedBox(4, 0), pcol2)->setSpacing(0))
                      ->setSpacing(0))
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
                     ->setWidth(38)
                     ->setHeight(28)
                     ->setBorderRadius(5)
                     ->setBackgroundColor(activeSize,
                                          [s](const float &a) {
                                            return a == s ? RGB(49, 50, 68)
                                                          : RGB(30, 30, 46);
                                          })
                     ->setBorderColor(activeSize,
                                      [s](const float &a) {
                                        return a == s ? RGB(203, 166, 247)
                                                      : RGB(49, 50, 68);
                                      })
                     ->setBorderWidth(
                         activeSize,
                         [s](const float &a) { return a == s ? 2 : 1; })
                     ->setPadding(4))
          ->setOnTap([this, s]() { activeSize.set(s); });
    };

    auto sizeSection =
        Container(Column(sectionLabel("BRUSH SIZE"), SizedBox(0, 8),
                         Row(sizeBtn(2.f, "S"), sizeBtn(5.f, "M"),
                             sizeBtn(10.f, "L"), sizeBtn(20.f, "XL"))
                             ->setSpacing(4))
                      ->setSpacing(0))
            ->setBackgroundColor(RGB(24, 24, 37))
            ->setBorderRadius(8)
            ->setBorderWidth(1)
            ->setBorderColor(RGB(49, 50, 68))
            ->setPaddingAll(10, 10, 10, 10);

    // ── Opacity slider ────────────────────────────────────────────────────
    auto opacitySection =
        Container(
            Column(Row(sectionLabel("OPACITY"), SizedBox(6, 0),
                       Text(activeOpacity,
                            [](const float &op) {
                              return std::to_string(int(std::round(op * 100))) +
                                     "%";
                            })
                           ->setFontSize(9)
                           ->setTextColor(RGB(160, 160, 180)))
                       ->setSpacing(0)
                       ->setCrossAxisAlignment(CrossAxisAlignment::Center),
                   SizedBox(0, 8),
                   Slider(0.0, 1.0, 0.01)
                       ->setValue(activeOpacity)
                       ->setTrackFillColor(RGB(174, 129, 255))
                       ->setWidth(196))
                ->setSpacing(0))
            ->setBackgroundColor(RGB(24, 24, 37))
            ->setBorderRadius(8)
            ->setBorderWidth(1)
            ->setBorderColor(RGB(49, 50, 68))
            ->setPaddingAll(10, 10, 10, 10);

    // ── Undo / Redo / Clear / Save ────────────────────────────────────────
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
        Container(Row(makeActionBtn("↩  Undo", RGB(137, 180, 250),
                                    [this] {
                                      if (surface_) {
                                        surface_->undo();
                                        refreshUndoRedo();
                                      }
                                    }),

                      makeActionBtn("Redo  ↪", RGB(166, 226, 46),
                                    [this] {
                                      if (surface_) {
                                        surface_->redo();
                                        refreshUndoRedo();
                                      }
                                    }),

                      makeActionBtn("✕  Clear", RGB(243, 139, 168),
                                    [this] {
                                      if (surface_) {
                                        surface_->clear();
                                        refreshUndoRedo();
                                      }
                                    }),

                      makeActionBtn("💾  Save PNG", RGB(148, 226, 213),
                                    [this] { doSavePNG(); }))
                      ->setSpacing(10)
                      ->setCrossAxisAlignment(CrossAxisAlignment::Stretch))
            ->setBackgroundColor(RGB(24, 24, 37))
            ->setBorderRadius(8)
            ->setBorderWidth(1)
            ->setBorderColor(RGB(49, 50, 68))
            ->setPaddingAll(10, 10, 10, 10);

    // ── Sidebar assembly ──────────────────────────────────────────────────
    auto sidebar =
        Container(Column(toolSection, SizedBox(0, 8), selectedSwatch,
                         SizedBox(0, 8), pickerSection, SizedBox(0, 8),
                         historySection, SizedBox(0, 8), paletteSection,
                         SizedBox(0, 8), sizeSection, SizedBox(0, 8),
                         opacitySection, SizedBox(0, 8))
                      ->setSpacing(0))
            ->setWidth(kSidebarWidth)
            ->setBackgroundColor(RGB(17, 17, 27))
            ->setPaddingAll(12, 12, 12, 12);

    // =====================================================================
    // TOP TOOLBAR  (zoom only)
    // =====================================================================

    auto resetBtn = Button("1:1",
                           [this] {
                             if (canvasPtr_) {
                               canvasPtr_->viewport().resetZoom();
                               canvasPtr_->redraw();
                               syncZoomState(1.f);
                             }
                           })
                        ->setBackgroundColor(RGB(24, 24, 37))
                        ->setTextColor(RGB(174, 129, 255))
                        ->setBorderRadius(5)
                        ->setWidth(32)
                        ->setHeight(24)
                        ->setPadding(4);

    auto fitBtn = Button("Fit",
                         [this] {
                           if (canvasPtr_) {
                             canvasPtr_->viewport().fitToView();
                             canvasPtr_->redraw();
                             syncZoomState(canvasPtr_->viewport().zoom());
                           }
                         })
                      ->setBackgroundColor(RGB(24, 24, 37))
                      ->setTextColor(RGB(174, 129, 255))
                      ->setBorderRadius(5)
                      ->setWidth(32)
                      ->setHeight(24)
                      ->setPadding(4);

    auto zoomRow = Row(
        Text("Zoom")->setFontSize(11)->setTextColor(RGB(140, 140, 160)),

        Slider(kZoomMin, kZoomMax, 0.25)
            ->setValue(zoomLevel)
            ->setTrackFillColor(RGB(174, 129, 255))
            ->setWidth(120),

        Text(zoomLevel,
             [](double v) { return std::to_string(int(std::round(v))) + "%"; })
            ->setFontSize(11)
            ->setTextColor(RGB(180, 180, 200))
            ->setMinWidth(40),
        resetBtn, fitBtn, actionSection);
    zoomRow->setSpacing(10);
    zoomRow->setCrossAxisAlignment(CrossAxisAlignment::Center);

    auto toolbar = Container(zoomRow)
                       ->setBackgroundColor(RGB(17, 17, 27))
                       ->setPaddingAll(10, 7, 10, 7)
                       ->setHeight(kToolbarHeight);

    // ── Context menu ──────────────────────────────────────────────────────
    auto canvasWithMenu = ContextMenu(
        canvas, {
                    {"Brush  (B)", [this] { activeTool.set(Tool::Brush); }},
                    {"Eraser (E)", [this] { activeTool.set(Tool::Eraser); }},
                    ContextMenuItem::Separator(),
                    {"Undo",
                     [this] {
                       if (surface_) {
                         surface_->undo();
                         refreshUndoRedo();
                       }
                     }},
                    {"Redo",
                     [this] {
                       if (surface_) {
                         surface_->redo();
                         refreshUndoRedo();
                       }
                     }},
                    ContextMenuItem::Separator(),
                    {"Zoom 1:1",
                     [this] {
                       if (canvasPtr_) {
                         canvasPtr_->viewport().resetZoom();
                         canvasPtr_->redraw();
                         syncZoomState(1.f);
                       }
                     }},
                    {"Fit to view",
                     [this] {
                       if (canvasPtr_) {
                         canvasPtr_->viewport().fitToView();
                         canvasPtr_->redraw();
                         syncZoomState(canvasPtr_->viewport().zoom());
                       }
                     }},
                    ContextMenuItem::Separator(),
                    {"Save as PNG", [this] { doSavePNG(); }},
                    {"Clear",
                     [this] {
                       if (surface_) {
                         surface_->clear();
                         refreshUndoRedo();
                       }
                     }},
                });

    // ── Hints strip ───────────────────────────────────────────────────────
    auto hints = Container(Row(Text("MMB / Space+LMB: Pan")
                                   ->setFontSize(10)
                                   ->setTextColor(RGB(75, 75, 95)),
                               SizedBox(10, 0),
                               Text("Ctrl+Scroll: Zoom")
                                   ->setFontSize(10)
                                   ->setTextColor(RGB(75, 75, 95)),
                               SizedBox(10, 0),
                               Text("Ctrl+Z/Y: Undo/Redo")
                                   ->setFontSize(10)
                                   ->setTextColor(RGB(75, 75, 95)),
                               SizedBox(10, 0),
                               Text("B: Brush  E: Eraser")
                                   ->setFontSize(10)
                                   ->setTextColor(RGB(75, 75, 95)))
                               ->setSpacing(0))
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
  int screenW = workArea.right - workArea.left;
  int screenH = workArea.bottom - workArea.top;

  app.createWindow("FluxUI - Paint", screenW, screenH);
  return app.run();
}