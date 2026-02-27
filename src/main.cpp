#include "flux.hpp"

// ============================================================================
// pixel_art.cpp  —  A pixel-art editor built on FluxUI / RasterSurface
// ============================================================================
//
// Changes vs. previous version
// ─────────────────────────────
//  [Z1] Real zoom — viewZoom_ / viewOffsetX_ / viewOffsetY_ + setProjOverride
//  [Z2] updateProjection() — custom orthographic matrix sent via
//  setProjOverride [Z3] centerView() — keeps zoom centred on canvas midpoint
//  (MS Paint style) [Z4] zoomIn() / zoomOut() clamped [0.25×, 16×], centred
//  each time [Z5] Pan — middle-mouse-button drag; also Space+LMB drag [Z6]
//  Mouse-wheel zoom — Ctrl+wheel zooms, plain wheel scrolls vertically [Z7]
//  screenToCanvas() — maps raw mouse coords to canvas pixel coords so
//       pencil / fill / eyedrop all work correctly under any zoom+pan
//  [Z8] Zoom +/- toolbar buttons + current zoom label
//  [Z9] Fit-to-canvas "1:1" reset button
// ============================================================================

// ── Hex ↔ COLORREF helpers ──────────────────────────────────────────────────

static COLORREF hexToRef(const std::string &css) {
  RGBA c = parseHexColor(css);
  return RGB((BYTE)(c.r * 255), (BYTE)(c.g * 255), (BYTE)(c.b * 255));
}

static std::string rgbaToHex(float r, float g, float b) {
  char buf[8];
  _snprintf_s(buf, sizeof(buf), _TRUNCATE, "#%02x%02x%02x",
              (unsigned)(r * 255.f + 0.5f), (unsigned)(g * 255.f + 0.5f),
              (unsigned)(b * 255.f + 0.5f));
  return buf;
}

// ============================================================================
// Tool enum
// ============================================================================

enum class PixelTool { Pencil, Fill, Eyedrop };

// ============================================================================
// PixelArtSurface  —  RasterSurface subclass
// ============================================================================

class PixelArtSurface : public RasterSurface {
public:
  // ── Palette ──────────────────────────────────────────────────────────────
  const std::vector<std::pair<std::string, std::string>> kPalette = {
      {"#272822", "Background"}, {"#75715e", "Comment"},
      {"#f8f8f2", "Foreground"}, {"#f92672", "Red"},
      {"#fd971f", "Orange"},     {"#e6db74", "Yellow"},
      {"#a6e22e", "Green"},      {"#66d9e8", "Cyan"},
      {"#ae81ff", "Purple"},     {"#cc6633", "Brown"},
      {"#f44747", "Error"},      {"#0e7490", "Teal"},
      {"#3b82f6", "Blue"},       {"#ec4899", "Pink"},
      {"#000000", "Black"},      {"#ffffff", "White"},
  };

  // ── Mutable tool state ────────────────────────────────────────────────────
  std::string activeColor = "#f8f8f2";
  PixelTool activeTool = PixelTool::Pencil;
  int pixelSize = 8;
  bool showGrid = true;

  // ── Callbacks ─────────────────────────────────────────────────────────────
  std::function<void()> onStateChanged;
  std::function<void(const std::string &)> onColorPicked;

  // ── Public tool API ───────────────────────────────────────────────────────

  void setColor(const std::string &col) {
    activeColor = col;
    syncStyle();
  }

  void setTool(PixelTool t) {
    activeTool = t;
    syncStyle();
  }

  void setPixelSize(int sz) {
    pixelSize = sz;
    syncStyle();
  }

  void setShowGrid(bool v) { showGrid = v; }

  // ── [Z4] Zoom API ─────────────────────────────────────────────────────────

  void zoomIn() {
    viewZoom_ *= 1.25f;
    viewZoom_ = min(viewZoom_, 16.0f);
    centerView();
  }

  void zoomOut() {
    viewZoom_ *= 0.8f;
    viewZoom_ = max(viewZoom_, 0.25f);
    centerView();
  }

  // [Z9] Reset to 1:1 and re-centre
  void resetZoom() {
    viewZoom_ = 1.0f;
    viewOffsetX_ = 0.0f;
    viewOffsetY_ = 0.0f;
    updateProjection();
    if (onStateChanged)
      onStateChanged();
  }

  float currentZoom() const { return viewZoom_; }

  // ── RenderSurface overrides ───────────────────────────────────────────────

  void initialize(int w, int h) override {
    RasterSurface::initialize(w, h);
    clearToColor(0x27, 0x28, 0x22);
    updateProjection(); // [Z3]
    syncStyle();
  }

  void resize(int w, int h) override {
    RasterSurface::resize(w, h);
    // Re-clamp offsets so we don't pan outside the canvas after a resize
    clampOffset();
    updateProjection();
  }

  // ── Mouse handling with coordinate transform ──────────────────────────────

  void onMouseDown(int sx, int sy) override {
    // [Z5] Middle-button or Space+LMB starts pan
    bool mmb = (GetKeyState(VK_MBUTTON) & 0x8000) != 0;
    bool space = (GetKeyState(VK_SPACE) & 0x8000) != 0;
    bool lmb = (GetKeyState(VK_LBUTTON) & 0x8000) != 0;

    if (mmb || (space && lmb)) {
      panning_ = true;
      panStartSX_ = sx;
      panStartSY_ = sy;
      panStartOX_ = viewOffsetX_;
      panStartOY_ = viewOffsetY_;
      return;
    }

    auto [cx, cy] = screenToCanvas(sx, sy);

    if (activeTool == PixelTool::Eyedrop) {
      pickColor(cx, cy);
      return;
    }
    if (activeTool == PixelTool::Fill) {
      floodFill(cx, cy);
      return;
    }
    RasterSurface::onMouseDown(cx, cy);
  }

  void onMouseMove(int sx, int sy) override {
    // [Z5] Pan drag
    if (panning_) {
      float dx = (sx - panStartSX_) / viewZoom_;
      float dy = (sy - panStartSY_) / viewZoom_;
      viewOffsetX_ = panStartOX_ - dx;
      viewOffsetY_ = panStartOY_ - dy;
      clampOffset();
      updateProjection();
      if (onStateChanged)
        onStateChanged();
      return;
    }

    if (activeTool == PixelTool::Pencil) {
      auto [cx, cy] = screenToCanvas(sx, sy);
      RasterSurface::onMouseMove(cx, cy);
    }
  }

  void onMouseUp(int sx, int sy) override {
    if (panning_) {
      panning_ = false;
      return;
    }

    if (activeTool == PixelTool::Pencil) {
      auto [cx, cy] = screenToCanvas(sx, sy);
      RasterSurface::onMouseUp(cx, cy);
    }
    if (onStateChanged)
      onStateChanged();
  }

  // [Z6] Mouse-wheel: Ctrl+wheel → zoom, plain wheel → pan vertically
  // Called by CanvasWidget's WM_MOUSEWHEEL handler (see note below).
  void onMouseWheel(int sx, int sy, int delta) {
    bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    if (ctrl) {
      // Zoom toward the cursor position
      auto [cx, cy] = screenToCanvas(sx, sy);
      float oldZoom = viewZoom_;

      if (delta > 0)
        viewZoom_ *= 1.25f;
      else
        viewZoom_ *= 0.8f;
      viewZoom_ = max(0.25f, min(16.0f, viewZoom_));

      // Adjust offsets so the canvas pixel under the cursor stays fixed
      float scale = oldZoom / viewZoom_;
      float viewW = canvasWidth() / viewZoom_;
      float viewH = canvasHeight() / viewZoom_;
      viewOffsetX_ = (float)cx - (float)sx / viewZoom_;
      viewOffsetY_ = (float)cy - (float)sy / viewZoom_;
      clampOffset();
      updateProjection();
    } else {
      // Plain scroll — pan vertically
      viewOffsetY_ -= (float)delta * 0.05f / viewZoom_;
      clampOffset();
      updateProjection();
    }
    if (onStateChanged)
      onStateChanged();
  }

  void onKeyDown(int key) override {
    RasterSurface::onKeyDown(key);

    bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    if (!ctrl) {
      if (key == 'P')
        setTool(PixelTool::Pencil);
      if (key == 'F')
        setTool(PixelTool::Fill);
      if (key == 'E')
        setTool(PixelTool::Eyedrop);
      if (key == 'G')
        setShowGrid(!showGrid);
      // Keyboard zoom: + / -
      if (key == VK_OEM_PLUS || key == VK_ADD)
        zoomIn();
      if (key == VK_OEM_MINUS || key == VK_SUBTRACT)
        zoomOut();
      if (key == '0')
        resetZoom();
    }
    if (onStateChanged)
      onStateChanged();
  }

private:
  // ── [Z1] View state ───────────────────────────────────────────────────────
  float viewZoom_ = 1.0f;
  float viewOffsetX_ = 0.0f;
  float viewOffsetY_ = 0.0f;

  float projMatrix_[16]{}; // ← ADD THIS: persistent storage for the matrix

  // Pan drag state
  bool panning_ = false;
  int panStartSX_ = 0, panStartSY_ = 0;
  float panStartOX_ = 0.f, panStartOY_ = 0.f;

  // ── [Z2] Projection builder ────────────────────────────────────────────────
void updateProjection() {
    float viewW = canvasWidth()  / viewZoom_;
    float viewH = canvasHeight() / viewZoom_;

    float l = viewOffsetX_;
    float r = viewOffsetX_ + viewW;
    float t = viewOffsetY_;
    float b = viewOffsetY_ + viewH;

    // Write into the persistent member array, not a local —
    // setProjOverride stores the raw pointer so it must remain valid.
    projMatrix_[0]  =  2.f / (r - l);
    projMatrix_[1]  =  0;
    projMatrix_[2]  =  0;
    projMatrix_[3]  =  0;
    projMatrix_[4]  =  0;
    projMatrix_[5]  = -2.f / (b - t);
    projMatrix_[6]  =  0;
    projMatrix_[7]  =  0;
    projMatrix_[8]  =  0;
    projMatrix_[9]  =  0;
    projMatrix_[10] = -1.f;
    projMatrix_[11] =  0;
    projMatrix_[12] = -(r + l) / (r - l);
    projMatrix_[13] =  (b + t) / (b - t);
    projMatrix_[14] =  0;
    projMatrix_[15] =  1.f;

    setProjOverride(projMatrix_);  // pointer stays valid — member lives as long as the surface
}

  // ── [Z3] Centre the view after a zoom change ───────────────────────────────
  void centerView() {
    float viewW = canvasWidth() / viewZoom_;
    float viewH = canvasHeight() / viewZoom_;
    viewOffsetX_ = (canvasWidth() - viewW) * 0.5f;
    viewOffsetY_ = (canvasHeight() - viewH) * 0.5f;
    updateProjection();
    if (onStateChanged)
      onStateChanged();
  }

  // Prevent panning the canvas entirely out of view
  void clampOffset() {
    float viewW = canvasWidth() / viewZoom_;
    float viewH = canvasHeight() / viewZoom_;
    // Allow up to half a view-width of overscroll so you can see the edges
    float marginX = viewW * 0.5f;
    float marginY = viewH * 0.5f;
    viewOffsetX_ =
        max(-marginX, min(viewOffsetX_, (float)canvasWidth() - marginX));
    viewOffsetY_ =
        max(-marginY, min(viewOffsetY_, (float)canvasHeight() - marginY));
  }

  // ── [Z7] Map screen coords → canvas pixel coords ──────────────────────────
  std::pair<int, int> screenToCanvas(int sx, int sy) const {
    float cx = viewOffsetX_ + (float)sx / viewZoom_;
    float cy = viewOffsetY_ + (float)sy / viewZoom_;
    return {(int)cx, (int)cy};
  }

  // ── Sync pencil style ─────────────────────────────────────────────────────
  void syncStyle() {
    RGBA c = parseHexColor(activeColor);
    StrokeStyle s;
    s.r = c.r;
    s.g = c.g;
    s.b = c.b;
    s.a = 1.f;
    s.radius =
        (activeTool == PixelTool::Pencil) ? (float)pixelSize * 0.5f : 0.f;
    s.opacity = 1.f;
    s.hardness = 1.f;
    setStrokeStyle(s);
  }

  void clearToColor(uint8_t /*r*/, uint8_t /*g*/, uint8_t /*b*/) {
    RasterSurface::clear();
  }

  // ── Eyedropper ────────────────────────────────────────────────────────────
  void pickColor(int cx, int cy) {
    GLuint fbo = committedFBOHandle();
    if (!fbo)
      return;
    int h = canvasHeight();
    // Clamp to canvas bounds
    cx = max(0, min(cx, canvasWidth() - 1));
    cy = max(0, min(cy, h - 1));

    GL.bindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
    uint8_t pixel[4] = {255, 255, 255, 255};
    glReadPixels(cx, h - 1 - cy, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    GL.bindFramebuffer(GL_READ_FRAMEBUFFER, 0);

    std::string picked =
        rgbaToHex(pixel[0] / 255.f, pixel[1] / 255.f, pixel[2] / 255.f);
    activeColor = picked;
    syncStyle();
    if (onColorPicked)
      onColorPicked(picked);
    if (onStateChanged)
      onStateChanged();
  }

  // ── Flood fill ────────────────────────────────────────────────────────────
  void floodFill(int startX, int startY) {
    int w = canvasWidth(), h = canvasHeight();
    startX = max(0, min(startX, w - 1));
    startY = max(0, min(startY, h - 1));

    std::vector<uint8_t> pixels(w * h * 4);
    GLuint fbo = committedFBOHandle();
    if (!fbo)
      return;
    GL.bindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    GL.bindFramebuffer(GL_READ_FRAMEBUFFER, 0);

    int glY = h - 1 - startY;
    int baseIdx = (glY * w + startX) * 4;
    uint8_t tR = pixels[baseIdx + 0];
    uint8_t tG = pixels[baseIdx + 1];
    uint8_t tB = pixels[baseIdx + 2];

    RGBA fc = parseHexColor(activeColor);
    uint8_t fR = (uint8_t)(fc.r * 255), fG = (uint8_t)(fc.g * 255),
            fB = (uint8_t)(fc.b * 255);
    if (tR == fR && tG == fG && tB == fB)
      return;

    std::vector<bool> visited(w * h, false);
    std::vector<std::pair<int, int>> queue;
    queue.reserve(w * h / 4);
    queue.push_back({startX, glY});
    visited[glY * w + startX] = true;

    const int dx[] = {1, -1, 0, 0};
    const int dy[] = {0, 0, 1, -1};

    for (size_t qi = 0; qi < queue.size(); ++qi) {
      auto [cx, cy] = queue[qi];
      int idx = (cy * w + cx) * 4;
      pixels[idx + 0] = fR;
      pixels[idx + 1] = fG;
      pixels[idx + 2] = fB;
      pixels[idx + 3] = 255;

      for (int d = 0; d < 4; ++d) {
        int nx = cx + dx[d], ny = cy + dy[d];
        if (nx < 0 || nx >= w || ny < 0 || ny >= h)
          continue;
        int ni = (ny * w + nx) * 4;
        if (visited[ny * w + nx])
          continue;
        if (pixels[ni + 0] != tR || pixels[ni + 1] != tG ||
            pixels[ni + 2] != tB)
          continue;
        visited[ny * w + nx] = true;
        queue.push_back({nx, ny});
      }
    }

    pushUndoSnapshotPublic();

    GLuint tex = committedTexHandle();
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE,
                    pixels.data());
    glBindTexture(GL_TEXTURE_2D, 0);

    if (onStateChanged)
      onStateChanged();
  }
};

// ============================================================================
// PixelArtApp  —  Component
// ============================================================================

class PixelArtApp : public Component {
  State<std::string> activeColor;
  State<PixelTool> activeTool;
  State<int> activePixelSize;
  State<bool> showGrid;
  State<int> changeStamp;
  State<float> zoomLevel; // [Z8] drives zoom label

  std::shared_ptr<PixelArtSurface> surface_;

public:
  PixelArtApp()
      : activeColor("#f8f8f2", context), activeTool(PixelTool::Pencil, context),
        activePixelSize(8, context), showGrid(true, context),
        changeStamp(0, context), zoomLevel(1.0f, context) {}

  WidgetPtr build() override {

    // ── Canvas + surface ──────────────────────────────────────────────────
    auto canvas = Canvas(512, 512);
    surface_ = canvas->setSurface<PixelArtSurface>();

    surface_->onStateChanged = [this]() {
      changeStamp.set(changeStamp.get() + 1);
      if (surface_)
        zoomLevel.set(surface_->currentZoom());
    };
    surface_->onColorPicked = [this](const std::string &col) {
      activeColor.set(col);
    };

    activeColor.listen([this](const std::string &col) {
      if (surface_)
        surface_->setColor(col);
    });
    activeTool.listen([this](PixelTool t) {
      if (surface_)
        surface_->setTool(t);
    });
    activePixelSize.listen([this](int sz) {
      if (surface_)
        surface_->setPixelSize(sz);
    });
    showGrid.listen([this](bool v) {
      if (surface_)
        surface_->setShowGrid(v);
    });

    // ── Palette swatches ──────────────────────────────────────────────────
    std::vector<WidgetPtr> swatches;
    for (const auto &[col, label] : surface_->kPalette) {
      std::string c = col;
      swatches.push_back(
          GestureDetector(Container(nullptr)
                              ->setWidth(22)
                              ->setHeight(22)
                              ->setBorderRadius(3)
                              ->setBackgroundColor(hexToRef(col))
                              ->setBorderWidth(activeColor,
                                               [c](const std::string &active) {
                                                 return active == c ? 2 : 1;
                                               })
                              ->setBorderColor(activeColor,
                                               [c](const std::string &active) {
                                                 return active == c
                                                            ? RGB(255, 255, 255)
                                                            : RGB(60, 60, 80);
                                               }))
              ->setOnTap([this, c]() {
                activeColor.set(c);
                activeTool.set(PixelTool::Pencil);
              }));
    }

    auto paletteCol = std::make_shared<ColumnWidget>();
    {
      auto row1 = std::make_shared<RowWidget>();
      row1->setSpacing(3);
      auto row2 = std::make_shared<RowWidget>();
      row2->setSpacing(3);
      for (int i = 0; i < (int)swatches.size(); ++i)
        (i < 8 ? row1 : row2)->addChild(swatches[i]);
      paletteCol->addChild(row1);
      paletteCol->addChild(SizedBox(0, 3));
      paletteCol->addChild(row2);
    }

    // ── Tool buttons ──────────────────────────────────────────────────────
    auto toolBtn = [&](PixelTool tool, const std::string &lbl) -> WidgetPtr {
      PixelTool t = tool;
      return GestureDetector(
                 Container(Text(lbl)->setFontSize(11))
                     ->setWidth(36)
                     ->setHeight(26)
                     ->setBorderRadius(4)
                     ->setBackgroundColor(activeTool,
                                          [t](const PixelTool &a) {
                                            return a == t ? RGB(39, 40, 34)
                                                          : RGB(17, 17, 27);
                                          })
                     ->setBorderColor(activeTool,
                                      [t](const PixelTool &a) {
                                        return a == t ? RGB(166, 226, 46)
                                                      : RGB(49, 50, 68);
                                      })
                     ->setBorderWidth(
                         activeTool,
                         [t](const PixelTool &a) { return a == t ? 2 : 1; })
                     ->setPadding(4))
          ->setOnTap([this, t]() { activeTool.set(t); });
    };

    auto toolRow =
        Row(toolBtn(PixelTool::Pencil, "Pen"), toolBtn(PixelTool::Fill, "Fill"),
            toolBtn(PixelTool::Eyedrop, "Pick"));
    toolRow->setSpacing(4);
    toolRow->setCrossAxisAlignment(CrossAxisAlignment::Center);

    // ── Zoom / pixel-size buttons ─────────────────────────────────────────
    auto sizeBtn = [&](int sz, const std::string &lbl) -> WidgetPtr {
      int s = sz;
      return GestureDetector(
                 Container(Text(lbl)->setFontSize(10))
                     ->setWidth(28)
                     ->setHeight(26)
                     ->setBorderRadius(4)
                     ->setBackgroundColor(activePixelSize,
                                          [s](const int &a) {
                                            return a == s ? RGB(39, 40, 34)
                                                          : RGB(17, 17, 27);
                                          })
                     ->setBorderColor(activePixelSize,
                                      [s](const int &a) {
                                        return a == s ? RGB(102, 217, 232)
                                                      : RGB(49, 50, 68);
                                      })
                     ->setBorderWidth(
                         activePixelSize,
                         [s](const int &a) { return a == s ? 2 : 1; })
                     ->setPadding(4))
          ->setOnTap([this, s]() { activePixelSize.set(s); });
    };

    auto sizeRow =
        Row(Text("Pen:")->setFontSize(11)->setTextColor(RGB(117, 113, 94)),
            SizedBox(5, 0), sizeBtn(2, "1px"), sizeBtn(4, "2px"),
            sizeBtn(8, "4px"), sizeBtn(16, "8px"));
    sizeRow->setSpacing(4);
    sizeRow->setCrossAxisAlignment(CrossAxisAlignment::Center);

    // ── [Z8] View-zoom controls ───────────────────────────────────────────
    //   [ − ]  [ 100% ]  [ + ]  [ 1:1 ]
    //   Ctrl+Wheel or +/- keys also work; middle-mouse / Space+drag to pan.

    auto zoomOutBtn = GestureDetector(Container(Text("−")->setFontSize(14))
                                          ->setWidth(28)
                                          ->setHeight(26)
                                          ->setBorderRadius(4)
                                          ->setBackgroundColor(RGB(17, 17, 27))
                                          ->setBorderColor(RGB(49, 50, 68))
                                          ->setBorderWidth(1)
                                          ->setPadding(4))
                          ->setOnTap([this]() {
                            if (surface_)
                              surface_->zoomOut();
                          });

    auto zoomInBtn = GestureDetector(Container(Text("+")->setFontSize(14))
                                         ->setWidth(28)
                                         ->setHeight(26)
                                         ->setBorderRadius(4)
                                         ->setBackgroundColor(RGB(17, 17, 27))
                                         ->setBorderColor(RGB(49, 50, 68))
                                         ->setBorderWidth(1)
                                         ->setPadding(4))
                         ->setOnTap([this]() {
                           if (surface_)
                             surface_->zoomIn();
                         });

    // Reactive label showing e.g. "100%" / "125%" / "25%"
    auto zoomLabel = Text(zoomLevel,
                          [](const float &z) {
                            char buf[12];
                            _snprintf_s(buf, sizeof(buf), _TRUNCATE, "%d%%",
                                        (int)(z * 100.f + 0.5f));
                            return std::string(buf);
                          })
                         ->setFontSize(11)
                         ->setTextColor(RGB(230, 219, 116))
                         ->setMinWidth(42);

    // [Z9] 1:1 reset
    auto resetZoomBtn =
        GestureDetector(Container(Text("1:1")->setFontSize(10))
                            ->setWidth(34)
                            ->setHeight(26)
                            ->setBorderRadius(4)
                            ->setBackgroundColor(RGB(17, 17, 27))
                            ->setBorderColor(RGB(49, 50, 68))
                            ->setBorderWidth(1)
                            ->setPadding(4))
            ->setOnTap([this]() {
              if (surface_) {
                surface_->resetZoom();
                zoomLevel.set(1.0f);
              }
            });

    auto zoomRow =
        Row(Text("View:")->setFontSize(11)->setTextColor(RGB(117, 113, 94)),
            SizedBox(4, 0), zoomOutBtn, SizedBox(3, 0), zoomLabel,
            SizedBox(3, 0), zoomInBtn, SizedBox(6, 0), resetZoomBtn);
    zoomRow->setSpacing(0);
    zoomRow->setCrossAxisAlignment(CrossAxisAlignment::Center);

    // ── Grid toggle ───────────────────────────────────────────────────────
    auto gridBtn =
        GestureDetector(
            Container(Text("Grid")->setFontSize(11))
                ->setWidth(40)
                ->setHeight(26)
                ->setBorderRadius(4)
                ->setBackgroundColor(showGrid,
                                     [](const bool &on) {
                                       return on ? RGB(39, 40, 34)
                                                 : RGB(17, 17, 27);
                                     })
                ->setBorderColor(showGrid,
                                 [](const bool &on) {
                                   return on ? RGB(174, 129, 255)
                                             : RGB(49, 50, 68);
                                 })
                ->setBorderWidth(showGrid,
                                 [](const bool &on) { return on ? 2 : 1; })
                ->setPadding(4))
            ->setOnTap([this]() { showGrid.set(!showGrid.get()); });

    // ── Action buttons ────────────────────────────────────────────────────
    auto mkActionBtn = [&](const std::string &lbl, COLORREF fg,
                           std::function<void()> cb) {
      return Button(lbl, cb)
          ->setBackgroundColor(RGB(17, 17, 27))
          ->setTextColor(fg)
          ->setBorderRadius(4)
          ->setWidth(48)
          ->setHeight(26)
          ->setPadding(4);
    };

    auto undoBtn = mkActionBtn("Undo", RGB(102, 217, 232), [this]() {
      if (surface_ && surface_->canUndo()) {
        surface_->undo();
        changeStamp.set(changeStamp.get() + 1);
      }
    });
    auto redoBtn = mkActionBtn("Redo", RGB(166, 226, 46), [this]() {
      if (surface_ && surface_->canRedo()) {
        surface_->redo();
        changeStamp.set(changeStamp.get() + 1);
      }
    });
    auto clearBtn = mkActionBtn("New", RGB(249, 38, 114), [this]() {
      if (surface_) {
        surface_->clear();
        changeStamp.set(0);
      }
    });
    clearBtn->setWidth(40);

    // ── Active colour preview ──────────────────────────────────────────────
    auto colorPreview = Container(nullptr)
                            ->setWidth(32)
                            ->setHeight(32)
                            ->setBorderRadius(4)
                            ->setBackgroundColor(activeColor,
                                                 [](const std::string &col) {
                                                   return hexToRef(col);
                                                 })
                            ->setBorderWidth(2)
                            ->setBorderColor(RGB(117, 113, 94));

    // ── Toolbar layout ─────────────────────────────────────────────────────
    //
    //  [ colour preview ]  [ palette 2×8 ]  |  [ Pen Fill Pick ]
    //                                        |  [ Pen size row ]
    //                                        |  [ View zoom row ] [Grid]
    //                                        |  [ Undo Redo New ]
    //
    auto leftSide = Row(colorPreview, SizedBox(10, 0), paletteCol);
    leftSide->setCrossAxisAlignment(CrossAxisAlignment::Center);

    auto rightCol = std::make_shared<ColumnWidget>();
    rightCol->setSpacing(5);
    rightCol->addChild(toolRow);
    rightCol->addChild(sizeRow);
    {
      auto zg = Row(zoomRow, SizedBox(8, 0), gridBtn);
      zg->setCrossAxisAlignment(CrossAxisAlignment::Center);
      rightCol->addChild(zg);
    }
    {
      auto acts =
          Row(undoBtn, SizedBox(4, 0), redoBtn, SizedBox(4, 0), clearBtn);
      acts->setCrossAxisAlignment(CrossAxisAlignment::Center);
      rightCol->addChild(acts);
    }

    auto toolbarRow = Row(leftSide, SizedBox(20, 0), rightCol);
    toolbarRow->setCrossAxisAlignment(CrossAxisAlignment::Center);

    auto toolbar = Container(toolbarRow)
                       ->setBackgroundColor(RGB(11, 11, 19))
                       ->setPadding(10);

    return Scaffold(AppBar("Pixel Art"),
                    Column(toolbar, canvas)->setSpacing(0));
  }
};

// ── Entry point ──────────────────────────────────────────────────────────────

WidgetPtr createApp(FluxUI *) {
  return FluxApp("Pixel Art", BuildComponent<PixelArtApp>(), AppTheme::dark());
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
  AllocConsole();
  FILE *fp;
  freopen_s(&fp, "CONOUT$", "w", stdout);
  std::cout << "=== FluxUI - Pixel Art ===" << std::endl;

  FluxUI app(hInstance);
  app.build([&]() { return createApp(&app); });
  app.createWindow("FluxUI - Pixel Art", 532, 680);
  return app.run();
}