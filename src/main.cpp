// test_paint_viewport.cpp
#include "flux.hpp"
#include <commdlg.h>
#include <shlobj.h>
#pragma comment(lib, "comdlg32.lib")

// ── App-level tool enum ──────────────────────────────────────────────────────
enum class Tool {
  Brush = kToolBrush,   // 0 — built-in paint
  Eraser = kToolEraser, // 1 — built-in erase
  Line = 2,
  Rect = 3,
  Fill = 4,
};

// ── Hex helpers
// ───────────────────────────────────────────────────────────────
static COLORREF hexToRef(const std::string &css) {
  RGBA c = parseHexColor(css);
  return RGB((BYTE)(c.r * 255), (BYTE)(c.g * 255), (BYTE)(c.b * 255));
}
static std::string refToHex(COLORREF c) { return ColorToHex(c); }

// ── Save dialog
// ───────────────────────────────────────────────────────────────
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
  return GetSaveFileNameW(&ofn) ? buf : L"";
}

// ── Geometry helpers
// ──────────────────────────────────────────────────────────

// Thick line as two triangles (GL_TRIANGLES, 6 verts, 12 floats)
static int makeThickLineVerts(float x0, float y0, float x1, float y1,
                              float half, float out[12]) {
  float dx = x1 - x0, dy = y1 - y0, len = std::sqrt(dx * dx + dy * dy);
  if (len < 0.001f) {
    out[0] = x0 - half;
    out[1] = y0 - half;
    out[2] = x0 + half;
    out[3] = y0 - half;
    out[4] = x0 + half;
    out[5] = y0 + half;
    out[6] = x0 + half;
    out[7] = y0 + half;
    out[8] = x0 - half;
    out[9] = y0 + half;
    out[10] = x0 - half;
    out[11] = y0 - half;
    return 6;
  }
  float nx = -dy / len * half, ny = dx / len * half;
  out[0] = x0 + nx;
  out[1] = y0 + ny;
  out[2] = x0 - nx;
  out[3] = y0 - ny;
  out[4] = x1 - nx;
  out[5] = y1 - ny;
  out[6] = x1 - nx;
  out[7] = y1 - ny;
  out[8] = x1 + nx;
  out[9] = y1 + ny;
  out[10] = x0 + nx;
  out[11] = y0 + ny;
  return 6;
}

// Rect corner coords for outline (4 corners, 8 floats)
static void makeRectCorners(float x0, float y0, float x1, float y1,
                            float out[8]) {
  float lx = min(x0, x1), rx = max(x0, x1);
  float by = min(y0, y1), ty = max(y0, y1);
  out[0] = lx;
  out[1] = by;
  out[2] = rx;
  out[3] = by;
  out[4] = rx;
  out[5] = ty;
  out[6] = lx;
  out[7] = ty;
}

// ── Flood fill (BFS, 4-connected, with tolerance)
// ─────────────────────────────
static void floodFill(uint8_t *pixels, int w, int h, int px, int py, uint8_t tr,
                      uint8_t tg, uint8_t tb, uint8_t fr, uint8_t fg,
                      uint8_t fb) {
  if (px < 0 || py < 0 || px >= w || py >= h)
    return;
  if (tr == fr && tg == fg && tb == fb)
    return;
  auto match = [&](int x, int y) -> bool {
    const uint8_t *p = pixels + (y * w + x) * 4;
    return std::abs((int)p[0] - (int)tr) <= 10 &&
           std::abs((int)p[1] - (int)tg) <= 10 &&
           std::abs((int)p[2] - (int)tb) <= 10;
  };
  if (!match(px, py))
    return;
  std::vector<std::pair<int, int>> stack;
  stack.reserve(w * h / 8);
  stack.push_back({px, py});
  while (!stack.empty()) {
    auto [x, y] = stack.back();
    stack.pop_back();
    if (x < 0 || x >= w || y < 0 || y >= h || !match(x, y))
      continue;
    uint8_t *p = pixels + (y * w + x) * 4;
    p[0] = fr;
    p[1] = fg;
    p[2] = fb;
    p[3] = 255;
    stack.push_back({x + 1, y});
    stack.push_back({x - 1, y});
    stack.push_back({x, y + 1});
    stack.push_back({x, y - 1});
  }
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
  float activeOpacity = 1.0f;
  Tool activeTool = Tool::Brush;

  std::function<void()> onStateChanged;
  std::function<void(const std::vector<RGBA> &)> onHistoryChanged;
  std::function<void(const std::string &)> onStatusMessage;
  std::function<void()> onRedrawNeeded;

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
    setTool(static_cast<ToolId>(t));
    syncStyle();
    // Cancel any in-progress shape
    shapeDrawing_ = false;
    scratchClear();
  }

  void initialize(int w, int h) override {
    RasterSurface::initialize(w, h);
    syncStyle();
  }

  // ── Mouse ─────────────────────────────────────────────────────────────────
  void onMouseDown(float x, float y) override {
    if (isShapeTool())
      shapeStart(x, y);
    else
      RasterSurface::onMouseDown(x, y);
  }
  void onMouseMove(float x, float y) override {
    if (isShapeTool()) {
      if (shapeDrawing_)
        shapePreview(x, y);
    } else
      RasterSurface::onMouseMove(x, y);
  }
  void onMouseUp(float x, float y) override {
    if (isShapeTool()) {
      if (shapeDrawing_)
        shapeCommit(x, y);
    } else {
      RasterSurface::onMouseUp(x, y);
      if (onStateChanged)
        onStateChanged();
      if (onHistoryChanged)
        onHistoryChanged(colorHistory());
    }
  }

  void onKeyDown(int key) override {
    RasterSurface::onKeyDown(key);
    switch (key) {
    case 'B':
      activeTool = Tool::Brush;
      setTool(kToolBrush);
      syncStyle();
      break;
    case 'E':
      activeTool = Tool::Eraser;
      setTool(kToolEraser);
      break;
    case 'L':
      activeTool = Tool::Line;
      setTool(2);
      break;
    case 'R':
      activeTool = Tool::Rect;
      setTool(3);
      break;
    case 'F':
      activeTool = Tool::Fill;
      setTool(4);
      break;
    }
    if (onStateChanged)
      onStateChanged();
  }
  void onKeyUp(int) override {}

private:
  bool shapeDrawing_ = false;
  float shapeX0_ = 0, shapeY0_ = 0;

  bool isShapeTool() const {
    return activeTool == Tool::Line || activeTool == Tool::Rect ||
           activeTool == Tool::Fill;
  }

  // ── Shape lifecycle ────────────────────────────────────────────────────────
  void shapeStart(float x, float y) {
    if (activeTool == Tool::Fill) {
      doFill(x, y);
      return;
    }
    shapeX0_ = x;
    shapeY0_ = y;
    shapeDrawing_ = true;
    scratchClear();
  }

  void shapePreview(float x, float y) {
    scratchClear();
    drawShapeInto(scratchFBOHandle(), shapeX0_, shapeY0_, x, y);
    if (onRedrawNeeded)
      onRedrawNeeded();
  }

  void shapeCommit(float x, float y) {
    shapeDrawing_ = false;
    scratchClear();
    pushUndoSnapshotPublic();
    drawShapeInto(committedFBOHandle(), shapeX0_, shapeY0_, x, y);
    if (onStateChanged)
      onStateChanged();
    if (onRedrawNeeded)
      onRedrawNeeded();
  }

  void drawShapeInto(GLuint fbo, float x0, float y0, float x1, float y1) {
    RGBA c = parseHexColor(activeColor);
    float r = c.r, g = c.g, b = c.b, a = activeOpacity;
    float half = max(0.5f, activeSize * 0.5f);

    if (activeTool == Tool::Line) {
      float v[12];
      int n = makeThickLineVerts(x0, y0, x1, y1, half, v);
      drawVertsToFBO(fbo, v, n, GL_TRIANGLES, r, g, b, a);
    } else if (activeTool == Tool::Rect) {
      // Outline only: draw 4 thick edge lines using the active stroke size
      float cv[8];
      makeRectCorners(x0, y0, x1, y1, cv);
      float ov[12];
      for (int i = 0; i < 4; i++) {
        int j = (i + 1) % 4;
        makeThickLineVerts(cv[i * 2], cv[i * 2 + 1], cv[j * 2], cv[j * 2 + 1],
                           half, ov);
        drawVertsToFBO(fbo, ov, 6, GL_TRIANGLES, r, g, b, a);
      }
    }
  }

  // ── Flood fill ─────────────────────────────────────────────────────────────
  void doFill(float cx, float cy) {
    int w = canvasWidth(), h = canvasHeight();
    int px = int(std::floor(cx)), py = int(std::floor(cy));
    if (px < 0 || py < 0 || px >= w || py >= h)
      return;
    std::vector<uint8_t> buf(size_t(w) * h * 4);
    readCommitted(buf.data());
    uint8_t *seed = buf.data() + (py * w + px) * 4;
    uint8_t tr = seed[0], tg = seed[1], tb = seed[2];
    RGBA fc = parseHexColor(activeColor);
    pushUndoSnapshotPublic();
    floodFill(buf.data(), w, h, px, py, tr, tg, tb, uint8_t(fc.r * 255),
              uint8_t(fc.g * 255), uint8_t(fc.b * 255));
    uploadToCommitted(buf.data());
    if (onStateChanged)
      onStateChanged();
    if (onRedrawNeeded)
      onRedrawNeeded();
  }

  // ── Style ──────────────────────────────────────────────────────────────────
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
  State<double> activeSize;
  State<double> activeOpacity;
  State<Tool> activeTool;
  State<bool> canUndo, canRedo;
  State<double> zoomLevel;
  State<std::vector<RGBA>> colorHistory;
  State<std::string> statusMsg;

  std::shared_ptr<PaintSurface> surface_;
  CanvasWidget *canvasPtr_ = nullptr;
  HWND mainHwnd_ = nullptr;

  static constexpr double kZoomMin = 6.25, kZoomMax = 200.0;
  static constexpr int kSidebarWidth = 256, kToolbarHeight = 48;
  static constexpr int kHintsHeight = 24, kAppBarHeight = 32;

  void refreshUndoRedo() {
    if (!surface_)
      return;
    canUndo.set(surface_->canUndo());
    canRedo.set(surface_->canRedo());
  }
  void syncZoomState(float z) {
    double pct = z * 100.0;
    if (std::abs(pct - zoomLevel.get()) > 0.5)
      zoomLevel.set(pct);
  }
  void applyZoomFromSlider(double pct) {
    if (!canvasPtr_)
      return;
    Viewport &vp = canvasPtr_->viewport();
    float t = float(pct / 100.0), cur = vp.zoom();
    if (std::abs(t - cur) < 0.0001f)
      return;
    vp.zoomToward(vp.viewW() * .5f, vp.viewH() * .5f, t / cur);
    canvasPtr_->redraw();
  }
  void doSavePNG() {
    if (!surface_ || !canvasPtr_)
      return;
    std::wstring path = promptSavePath(mainHwnd_);
    if (path.empty())
      return;
    if (!surface_->savePNG(path))
      MessageBoxW(mainHwnd_, L"Failed to save PNG.", L"Error",
                  MB_ICONERROR | MB_OK);
  }

public:
  PaintApp()
      : activeColor("#cba6f7", context), activeSize(4.0, context),
        activeOpacity(1.0, context), activeTool(Tool::Brush, context),
        canUndo(false, context), canRedo(false, context),
        zoomLevel(100.0, context), colorHistory({}, context),
        statusMsg("", context) {}

  WidgetPtr build() override {
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int viewW = screenW - kSidebarWidth;
    int viewH = screenH - kAppBarHeight - kToolbarHeight - kHintsHeight;

    static constexpr int kPaperW = 1240, kPaperH = 1754;

    auto canvas = RasterCanvas(viewW, viewH, kPaperW, kPaperH);
    surface_ = canvas->setSurface<PaintSurface>();
    canvasPtr_ = canvas.get();
    mainHwnd_ = GetActiveWindow();

    surface_->onStateChanged = [this]() { refreshUndoRedo(); };
    surface_->onHistoryChanged = [this](const std::vector<RGBA> &h) {
      colorHistory.set(h);
    };
    surface_->onStatusMessage = [this](const std::string &m) {
      statusMsg.set(m);
    };
    canvas->onViewportChanged = [this](float z) { syncZoomState(z); };
    surface_->onRedrawNeeded = [this]() {
      if (canvasPtr_)
        canvasPtr_->redraw();
    };

    activeColor.listen([this](const std::string &col) {
      if (surface_)
        surface_->setColor(col);
    });
    activeSize.listen([this](double sz) {
      if (surface_)
        surface_->setSize(float(sz));
    });
    activeOpacity.listen([this](double op) {
      if (surface_)
        surface_->setOpacity(float(op));
    });
    activeTool.listen([this](Tool t) {
      if (surface_)
        surface_->setActiveTool(t);
    });
    zoomLevel.listen([this](double pct) { applyZoomFromSlider(pct); });

    // ===================================================================
    // SIDEBAR
    // ===================================================================

    auto sectionLabel = [](const std::string &txt) -> WidgetPtr {
      return Text(txt)
          ->setFontSize(9)
          ->setTextColor(RGB(100, 105, 125))
          ->setFontWeight(FontWeight::Bold);
    };

    // ── Tools (3-column grid, now 5 tools in two rows: 3 + 2) ─────────────
    struct TD {
      Tool t;
      std::string icon;
      std::string label;
    };
    const std::vector<TD> tds = {
        {Tool::Brush, "🖌", "Brush"}, {Tool::Eraser, "⬜", "Eraser"},
        {Tool::Line, "╱", "Line"},    {Tool::Rect, "▭", "Rect"},
        {Tool::Fill, "🪣", "Fill"},
    };

    auto r0 = std::make_shared<RowWidget>();
    r0->setSpacing(6);
    auto r1 = std::make_shared<RowWidget>();
    r1->setSpacing(6);

    for (int i = 0; i < (int)tds.size(); i++) {
      Tool tv = tds[i].t;
      std::string icon = tds[i].icon, lbl = tds[i].label;
      auto btn =
          GestureDetector(
              Container(Column(Text(icon)->setFontSize(18)->setTextColor(
                                   activeTool,
                                   [tv](const Tool &at) {
                                     return at == tv ? RGB(203, 166, 247)
                                                     : RGB(140, 140, 160);
                                   }),
                               SizedBox(0, 2),
                               Text(lbl)->setFontSize(8)->setTextColor(
                                   activeTool,
                                   [tv](const Tool &at) {
                                     return at == tv ? RGB(220, 200, 255)
                                                     : RGB(100, 100, 120);
                                   }))
                            ->setSpacing(0)
                            ->setCrossAxisAlignment(CrossAxisAlignment::Center))
                  ->setWidth(68)
                  ->setHeight(48)
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
      if (i < 3)
        r0->addChild(btn);
      else
        r1->addChild(btn);
    }

    auto toolSection = Container(Column(sectionLabel("TOOLS"), SizedBox(0, 8),
                                        r0, SizedBox(0, 6), r1)
                                     ->setSpacing(0))
                           ->setBackgroundColor(RGB(24, 24, 37))
                           ->setBorderRadius(8)
                           ->setBorderWidth(1)
                           ->setBorderColor(RGB(49, 50, 68))
                           ->setPaddingAll(10, 10, 10, 10);

    // ── Selected colour ────────────────────────────────────────────────────
    auto selectedSwatch =
        Container(
            Row(Container(nullptr)
                    ->setWidth(36)
                    ->setHeight(36)
                    ->setBorderRadius(6)
                    ->setBackgroundColor(
                        activeColor,
                        [](const std::string &c) { return hexToRef(c); })
                    ->setBorderWidth(1)
                    ->setBorderColor(RGB(69, 71, 90)),
                SizedBox(8, 0),
                Column(Text("Selected")
                           ->setFontSize(9)
                           ->setTextColor(RGB(100, 105, 125)),
                       SizedBox(0, 3),
                       Text(activeColor, [](const std::string &c) { return c; })
                           ->setFontSize(11)
                           ->setTextColor(RGB(205, 214, 244)))
                    ->setSpacing(0))
                ->setSpacing(0)
                ->setCrossAxisAlignment(CrossAxisAlignment::Center))
            ->setBackgroundColor(RGB(24, 24, 37))
            ->setBorderRadius(8)
            ->setBorderWidth(1)
            ->setBorderColor(RGB(49, 50, 68))
            ->setPaddingAll(10, 10, 10, 10);

    // ── Color picker ──────────────────────────────────────────────────────
    auto picker = ColorPicker(hexToRef(activeColor.get()))
                      ->setShowAlpha(false)
                      ->setOnColorChanged(
                          [this](COLORREF c) { activeColor.set(refToHex(c)); });
    auto pickerSection = Container(picker)
                             ->setBackgroundColor(RGB(24, 24, 37))
                             ->setBorderRadius(8)
                             ->setBorderWidth(1)
                             ->setBorderColor(RGB(49, 50, 68));

    // ── Color history ──────────────────────────────────────────────────────
    auto historyList =
        ListView(colorHistory)
            ->itemBuilder([this](int, const RGBA &c) -> WidgetPtr {
              char hex[8];
              _snprintf_s(hex, sizeof(hex), _TRUNCATE, "#%02x%02x%02x",
                          int(c.r * 255), int(c.g * 255), int(c.b * 255));
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
                       ->Then([&] { return SizedBox(210, 26, historyList); })
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

    // ── Palette ────────────────────────────────────────────────────────────
    auto pc1 = std::make_shared<RowWidget>();
    pc1->setSpacing(4);
    auto pc2 = std::make_shared<RowWidget>();
    pc2->setSpacing(4);
    for (int i = 0; i < (int)surface_->kPalette.size(); i++) {
      const auto &[col, lbl] = surface_->kPalette[i];
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
        pc1->addChild(sw);
      else
        pc2->addChild(sw);
    }
    auto paletteSection =
        Container(Column(sectionLabel("PRESETS"), SizedBox(0, 8),
                         Column(pc1, SizedBox(0, 4), pc2)->setSpacing(0))
                      ->setSpacing(0))
            ->setBackgroundColor(RGB(24, 24, 37))
            ->setBorderRadius(8)
            ->setBorderWidth(1)
            ->setBorderColor(RGB(49, 50, 68))
            ->setPaddingAll(10, 10, 10, 10);

    // ── Size slider ────────────────────────────────────────────────────────
    auto sizeSection =
        Container(
            Column(Row(sectionLabel("SIZE"), SizedBox(6, 0),
                       Text(activeSize,
                            [](double v) {
                              return std::to_string(int(std::round(v))) + "px";
                            })
                           ->setFontSize(9)
                           ->setTextColor(RGB(160, 160, 180)))
                       ->setSpacing(0)
                       ->setCrossAxisAlignment(CrossAxisAlignment::Center),
                   SizedBox(0, 8),
                   Slider(1.0, 40.0, 0.5)
                       ->setValue(activeSize)
                       ->setTrackFillColor(RGB(174, 129, 255))
                       ->setWidth(210))
                ->setSpacing(0))
            ->setBackgroundColor(RGB(24, 24, 37))
            ->setBorderRadius(8)
            ->setBorderWidth(1)
            ->setBorderColor(RGB(49, 50, 68))
            ->setPaddingAll(10, 10, 10, 10);

    // ── Opacity slider ─────────────────────────────────────────────────────
    auto opacitySection =
        Container(
            Column(Row(sectionLabel("OPACITY"), SizedBox(6, 0),
                       Text(activeOpacity,
                            [](double op) {
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
                       ->setWidth(210))
                ->setSpacing(0))
            ->setBackgroundColor(RGB(24, 24, 37))
            ->setBorderRadius(8)
            ->setBorderWidth(1)
            ->setBorderColor(RGB(49, 50, 68))
            ->setPaddingAll(10, 10, 10, 10);

    // ── Sidebar assembly ───────────────────────────────────────────────────
    auto sidebar =
        Container(Column(toolSection, SizedBox(0, 8), selectedSwatch,
                         SizedBox(0, 8), pickerSection, SizedBox(0, 8),
                         historySection, SizedBox(0, 8), paletteSection,
                         SizedBox(0, 8), sizeSection, SizedBox(0, 8),
                         opacitySection)
                      ->setSpacing(0))
            ->setWidth(kSidebarWidth)
            ->setBackgroundColor(RGB(17, 17, 27))
            ->setPaddingAll(12, 12, 12, 12);

    // ===================================================================
    // TOOLBAR
    // ===================================================================

    auto makeBtn = [](const std::string &lbl, COLORREF txt,
                      std::function<void()> fn) -> WidgetPtr {
      return Button(lbl, fn)
          ->setBackgroundColor(RGB(30, 30, 46))
          ->setTextColor(txt)
          ->setBorderRadius(5)
          ->setHeight(26)
          ->setPadding(4);
    };

    auto toolbar =
        Container(
            Row(Text("Zoom")->setFontSize(11)->setTextColor(RGB(140, 140, 160)),
                Slider(kZoomMin, kZoomMax, 0.25)
                    ->setValue(zoomLevel)
                    ->setTrackFillColor(RGB(174, 129, 255))
                    ->setWidth(110),
                Text(zoomLevel,
                     [](double v) {
                       return std::to_string(int(std::round(v))) + "%";
                     })
                    ->setFontSize(11)
                    ->setTextColor(RGB(180, 180, 200))
                    ->setMinWidth(38),
                Button("1:1",
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
                    ->setWidth(30)
                    ->setHeight(26)
                    ->setPadding(4),
                Button("Fit",
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
                    ->setWidth(30)
                    ->setHeight(26)
                    ->setPadding(4),
                SizedBox(12, 0),
                makeBtn("↩ Undo", RGB(137, 180, 250),
                        [this] {
                          if (surface_) {
                            surface_->undo();
                            refreshUndoRedo();
                            if (canvasPtr_)
                              canvasPtr_->redraw();
                          }
                        }),
                makeBtn("Redo ↪", RGB(166, 226, 46),
                        [this] {
                          if (surface_) {
                            surface_->redo();
                            refreshUndoRedo();
                            if (canvasPtr_)
                              canvasPtr_->redraw();
                          }
                        }),
                makeBtn("✕ Clear", RGB(243, 139, 168),
                        [this] {
                          if (surface_) {
                            surface_->clear();
                            refreshUndoRedo();
                            if (canvasPtr_)
                              canvasPtr_->redraw();
                          }
                        }),
                makeBtn("💾 Save", RGB(148, 226, 213), [this] { doSavePNG(); }))
                ->setSpacing(8)
                ->setCrossAxisAlignment(CrossAxisAlignment::Center))
            ->setBackgroundColor(RGB(17, 17, 27))
            ->setPaddingAll(10, 7, 10, 7)
            ->setHeight(kToolbarHeight);

    // ── Context menu ───────────────────────────────────────────────────────
    auto canvasWithMenu = ContextMenu(
        canvas, {
                    {"Brush (B)", [this] { activeTool.set(Tool::Brush); }},
                    {"Eraser (E)", [this] { activeTool.set(Tool::Eraser); }},
                    {"Line (L)", [this] { activeTool.set(Tool::Line); }},
                    {"Rect (R)", [this] { activeTool.set(Tool::Rect); }},
                    {"Fill (F)", [this] { activeTool.set(Tool::Fill); }},
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
                         if (canvasPtr_)
                           canvasPtr_->redraw();
                       }
                     }},
                });

    // ── Hints strip ────────────────────────────────────────────────────────
    auto hints =
        Container(Conditional(statusMsg,
                              [](const std::string &s) { return !s.empty(); })
                      ->Then([&] {
                        return Text(statusMsg,
                                    [](const std::string &s) { return s; })
                            ->setFontSize(10)
                            ->setTextColor(RGB(148, 226, 213));
                      })
                      ->Else([&] {
                        return Row(Text("MMB/Space: Pan")
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
                                   Text("B E L R F: Tools")
                                       ->setFontSize(10)
                                       ->setTextColor(RGB(75, 75, 95)))
                            ->setSpacing(0);
                      }))
            ->setBackgroundColor(RGB(17, 17, 27))
            ->setPaddingAll(10, 3, 10, 3)
            ->setHeight(kHintsHeight);

    auto canvasColumn = Column(toolbar, hints, canvasWithMenu)->setSpacing(0);
    auto root = Row(sidebar, canvasColumn)->setSpacing(0);
    return Scaffold(root);
  }
};

// ── Entry point
// ────────────────────────────────────────────────────────────────
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
  FluxUI app(hInstance);
  app.build([&]() {
    return FluxApp("Paint", BuildComponent<PaintApp>(), AppTheme::dark());
  });
  RECT wa;
  SystemParametersInfo(SPI_GETWORKAREA, 0, &wa, 0);
  app.createWindow("FluxUI - Paint", wa.right - wa.left, wa.bottom - wa.top);
  return app.run();
}