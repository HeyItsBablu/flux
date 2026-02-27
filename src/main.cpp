#include "flux.hpp"

// ============================================================================
// pixel_art.cpp  —  A pixel-art editor built on FluxUI / RasterSurface
// ============================================================================
//
// Changes vs. previous version
// ─────────────────────────────
//  [P1] All zoom/pan state removed from PixelArtSurface.
//       Viewport (owned by CanvasWidget) handles zoom, pan, scrollbars,
//       coordinate transforms, and easing.
//
//  [P2] onMouseDown/Move/Up signatures changed to (float x, float y).
//       Coords arrive in canvas space (y-up) pre-transformed by Viewport.
//       No screenToCanvas() call needed anywhere in this file.
//
//  [P3] render() removed from PixelArtSurface — RasterSurface::render()
//       now accepts the MVP from Viewport directly. No override needed.
//
//  [P4] onColorPicked / onStateChanged callbacks simplified — no zoom
//       state to report. zoomLevel State<float> driven by canvas->viewport().
//
//  [P5] Zoom toolbar buttons call canvas->viewport().zoomIn/Out/resetZoom()
//       directly. Zoom label reads canvas->viewport().zoom().
//
//  [P6] PixelArtApp wires a viewport change callback via a repeating timer
//       or reactive read — simplest approach: zoomLevel updated in
//       onStateChanged which PixelArtSurface fires after any tool action,
//       and the toolbar buttons update it immediately on click.
// ============================================================================

// ── Hex ↔ COLORREF helpers ──────────────────────────────────────────────────

static COLORREF hexToRef(const std::string &css) {
    RGBA c = parseHexColor(css);
    return RGB((BYTE)(c.r * 255), (BYTE)(c.g * 255), (BYTE)(c.b * 255));
}

static std::string rgbaToHex(float r, float g, float b) {
    char buf[8];
    _snprintf_s(buf, sizeof(buf), _TRUNCATE, "#%02x%02x%02x",
        (unsigned)(r * 255.f + 0.5f),
        (unsigned)(g * 255.f + 0.5f),
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
//
// Responsibilities (only):
//   - Palette / tool / pixel-size state
//   - Pencil drawing (delegates to RasterSurface stroke API)
//   - Flood fill (CPU BFS on committed FBO pixels)
//   - Eyedropper (glReadPixels on committed FBO)
//   - Keyboard shortcuts for tool switching
//
// NOT responsible for: zoom, pan, coordinate transforms, projection.

class PixelArtSurface : public RasterSurface {
public:
    // ── Palette ───────────────────────────────────────────────────────────────
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

    // ── Tool state ────────────────────────────────────────────────────────────
    std::string activeColor = "#f8f8f2";
    PixelTool   activeTool  = PixelTool::Pencil;
    int         pixelSize   = 8;
    bool        showGrid    = true;

    // ── Callbacks ─────────────────────────────────────────────────────────────
    std::function<void()>                  onStateChanged;
    std::function<void(const std::string&)> onColorPicked;

    // ── Public API ────────────────────────────────────────────────────────────

    void setColor(const std::string &col) { activeColor = col; syncStyle(); }
    void setTool (PixelTool t)            { activeTool  = t;   syncStyle(); }
    void setPixelSize(int sz)             { pixelSize   = sz;  syncStyle(); }
    void setShowGrid (bool v)             { showGrid    = v; }

    // ── RenderSurface overrides ───────────────────────────────────────────────

    void initialize(int w, int h) override {
        RasterSurface::initialize(w, h);
        clearToBackground();
        syncStyle();
    }

    // [P2] Coords are canvas-space (y-up) — no transform needed here.

    void onMouseDown(float x, float y) override {
        if (activeTool == PixelTool::Eyedrop) { pickColor(x, y);   return; }
        if (activeTool == PixelTool::Fill)    { floodFill(x, y);   return; }
        RasterSurface::onMouseDown(x, y);
    }

    void onMouseMove(float x, float y) override {
        if (activeTool == PixelTool::Pencil)
            RasterSurface::onMouseMove(x, y);
    }

    void onMouseUp(float x, float y) override {
        if (activeTool == PixelTool::Pencil)
            RasterSurface::onMouseUp(x, y);
        if (onStateChanged) onStateChanged();
    }

    void onKeyDown(int key) override {
        // Let RasterSurface handle Ctrl+Z / Ctrl+Y first
        RasterSurface::onKeyDown(key);

        bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        if (!ctrl) {
            if (key == 'P') setTool(PixelTool::Pencil);
            if (key == 'F') setTool(PixelTool::Fill);
            if (key == 'E') setTool(PixelTool::Eyedrop);
            if (key == 'G') setShowGrid(!showGrid);
            // Note: zoom keys (Ctrl+= / Ctrl+-) are handled by CanvasWidget,
            // not here. [P1]
        }
        if (onStateChanged) onStateChanged();
    }

private:
    // ── Sync pencil style ─────────────────────────────────────────────────────
    void syncStyle() {
        RGBA c = parseHexColor(activeColor);
        StrokeStyle s;
        s.r       = c.r;
        s.g       = c.g;
        s.b       = c.b;
        s.a       = 1.f;
        s.radius  = (activeTool == PixelTool::Pencil) ? (float)pixelSize * 0.5f : 0.f;
        s.opacity = 1.f;
        s.hardness= 1.f;
        setStrokeStyle(s);
    }

    void clearToBackground() {
        // Pushes an undo snapshot then fills with the palette background colour
        RasterSurface::clear();
    }

    // ── Eyedropper ────────────────────────────────────────────────────────────
    // [P2] x,y are canvas-space (y-up, origin bottom-left).
    // glReadPixels expects y from the bottom so no flip needed.
    void pickColor(float fx, float fy) {
        int cx = (int)fx, cy = (int)fy;
        int w = canvasWidth(), h = canvasHeight();
        cx = std::clamp(cx, 0, w - 1);
        cy = std::clamp(cy, 0, h - 1);

        GL.bindFramebuffer(GL_READ_FRAMEBUFFER, committedFBOHandle());
        uint8_t pixel[4] = {255, 255, 255, 255};
        // cy is already y-up (0 = bottom), matching GL's readback convention
        glReadPixels(cx, cy, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
        GL.bindFramebuffer(GL_READ_FRAMEBUFFER, 0);

        std::string picked = rgbaToHex(pixel[0]/255.f, pixel[1]/255.f, pixel[2]/255.f);
        activeColor = picked;
        syncStyle();
        if (onColorPicked)  onColorPicked(picked);
        if (onStateChanged) onStateChanged();
    }

    // ── Flood fill ────────────────────────────────────────────────────────────
    // [P2] x,y are canvas-space y-up.
    // glReadPixels returns rows from bottom, so pixel (cx, cy) in canvas space
    // is directly (cx, cy) in GL readback — no flip needed.
    void floodFill(float fx, float fy) {
        int w = canvasWidth(), h = canvasHeight();
        int startX = std::clamp((int)fx, 0, w - 1);
        int startY = std::clamp((int)fy, 0, h - 1);

        std::vector<uint8_t> pixels(w * h * 4);
        GL.bindFramebuffer(GL_READ_FRAMEBUFFER, committedFBOHandle());
        glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        GL.bindFramebuffer(GL_READ_FRAMEBUFFER, 0);

        // pixels[0] is bottom-left row (y=0 in canvas space = y=0 in GL).
        // startY is also y-up, so index directly.
        int baseIdx = (startY * w + startX) * 4;
        uint8_t tR = pixels[baseIdx+0];
        uint8_t tG = pixels[baseIdx+1];
        uint8_t tB = pixels[baseIdx+2];

        RGBA fc = parseHexColor(activeColor);
        uint8_t fR = (uint8_t)(fc.r*255), fG = (uint8_t)(fc.g*255), fB = (uint8_t)(fc.b*255);
        if (tR == fR && tG == fG && tB == fB) return;

        std::vector<bool> visited(w * h, false);
        std::vector<std::pair<int,int>> queue;
        queue.reserve(w * h / 4);
        queue.push_back({startX, startY});
        visited[startY * w + startX] = true;

        const int dx[] = { 1,-1, 0, 0};
        const int dy[] = { 0, 0, 1,-1};

        for (size_t qi = 0; qi < queue.size(); ++qi) {
            auto [cx, cy] = queue[qi];
            int idx = (cy * w + cx) * 4;
            pixels[idx+0] = fR;
            pixels[idx+1] = fG;
            pixels[idx+2] = fB;
            pixels[idx+3] = 255;
            for (int d = 0; d < 4; ++d) {
                int nx = cx+dx[d], ny = cy+dy[d];
                if (nx<0||nx>=w||ny<0||ny>=h) continue;
                int ni = (ny*w+nx)*4;
                if (visited[ny*w+nx]) continue;
                if (pixels[ni+0]!=tR||pixels[ni+1]!=tG||pixels[ni+2]!=tB) continue;
                visited[ny*w+nx] = true;
                queue.push_back({nx, ny});
            }
        }

        pushUndoSnapshotPublic();

        glBindTexture(GL_TEXTURE_2D, committedTexHandle());
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        glBindTexture(GL_TEXTURE_2D, 0);

        if (onStateChanged) onStateChanged();
    }
};

// ============================================================================
// PixelArtApp  —  Component
// ============================================================================

class PixelArtApp : public Component {
    State<std::string> activeColor;
    State<PixelTool>   activeTool;
    State<int>         activePixelSize;
    State<bool>        showGrid;
    State<int>         changeStamp;
    State<float>       zoomLevel;   // drives zoom label in toolbar

    std::shared_ptr<PixelArtSurface> surface_;
    std::shared_ptr<CanvasWidget>    canvas_;

public:
    PixelArtApp()
        : activeColor   ("#f8f8f2",         context)
        , activeTool    (PixelTool::Pencil,  context)
        , activePixelSize(8,                 context)
        , showGrid      (true,               context)
        , changeStamp   (0,                  context)
        , zoomLevel     (1.0f,               context)
    {}

    WidgetPtr build() override {

        // ── Canvas ────────────────────────────────────────────────────────────
        // View window 512×512, canvas 512×512.
        // Canvas size and view size are independent — change either via
        // canvas_->setCanvasSize() or canvas_->setSize().
        canvas_ = Canvas(512, 512);
        canvas_->setCanvasSize(512, 512);
        surface_ = canvas_->setSurface<PixelArtSurface>();

        // ── Wire surface callbacks ────────────────────────────────────────────
        surface_->onStateChanged = [this]() {
            changeStamp.set(changeStamp.get() + 1);
            // Read current zoom from Viewport after any state change
            zoomLevel.set(canvas_->viewport().zoom());
        };
        surface_->onColorPicked = [this](const std::string &col) {
            activeColor.set(col);
        };

        // ── Wire State → surface ──────────────────────────────────────────────
        activeColor.listen([this](const std::string &col) {
            if (surface_) surface_->setColor(col);
        });
        activeTool.listen([this](PixelTool t) {
            if (surface_) surface_->setTool(t);
        });
        activePixelSize.listen([this](int sz) {
            if (surface_) surface_->setPixelSize(sz);
        });
        showGrid.listen([this](bool v) {
            if (surface_) surface_->setShowGrid(v);
        });

        // ── Palette swatches ──────────────────────────────────────────────────
        std::vector<WidgetPtr> swatches;
        for (const auto &[col, label] : surface_->kPalette) {
            std::string c = col;
            swatches.push_back(
                GestureDetector(
                    Container(nullptr)
                        ->setWidth(22)->setHeight(22)
                        ->setBorderRadius(3)
                        ->setBackgroundColor(hexToRef(col))
                        ->setBorderWidth(activeColor, [c](const std::string &a){
                            return a == c ? 2 : 1;
                        })
                        ->setBorderColor(activeColor, [c](const std::string &a){
                            return a == c ? RGB(255,255,255) : RGB(60,60,80);
                        }))
                ->setOnTap([this, c]() {
                    activeColor.set(c);
                    activeTool.set(PixelTool::Pencil);
                }));
        }

        auto paletteCol = std::make_shared<ColumnWidget>();
        {
            auto row1 = std::make_shared<RowWidget>(); row1->setSpacing(3);
            auto row2 = std::make_shared<RowWidget>(); row2->setSpacing(3);
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
                    ->setWidth(36)->setHeight(26)->setBorderRadius(4)
                    ->setBackgroundColor(activeTool, [t](const PixelTool &a){
                        return a==t ? RGB(39,40,34) : RGB(17,17,27);
                    })
                    ->setBorderColor(activeTool, [t](const PixelTool &a){
                        return a==t ? RGB(166,226,46) : RGB(49,50,68);
                    })
                    ->setBorderWidth(activeTool, [t](const PixelTool &a){
                        return a==t ? 2 : 1;
                    })
                    ->setPadding(4))
            ->setOnTap([this, t]() { activeTool.set(t); });
        };

        auto toolRow = Row(
            toolBtn(PixelTool::Pencil,  "Pen"),
            toolBtn(PixelTool::Fill,    "Fill"),
            toolBtn(PixelTool::Eyedrop, "Pick"));
        toolRow->setSpacing(4);
        toolRow->setCrossAxisAlignment(CrossAxisAlignment::Center);

        // ── Pixel size buttons ────────────────────────────────────────────────
        auto sizeBtn = [&](int sz, const std::string &lbl) -> WidgetPtr {
            int s = sz;
            return GestureDetector(
                Container(Text(lbl)->setFontSize(10))
                    ->setWidth(28)->setHeight(26)->setBorderRadius(4)
                    ->setBackgroundColor(activePixelSize, [s](const int &a){
                        return a==s ? RGB(39,40,34) : RGB(17,17,27);
                    })
                    ->setBorderColor(activePixelSize, [s](const int &a){
                        return a==s ? RGB(102,217,232) : RGB(49,50,68);
                    })
                    ->setBorderWidth(activePixelSize, [s](const int &a){
                        return a==s ? 2 : 1;
                    })
                    ->setPadding(4))
            ->setOnTap([this, s]() { activePixelSize.set(s); });
        };

        auto sizeRow = Row(
            Text("Pen:")->setFontSize(11)->setTextColor(RGB(117,113,94)),
            SizedBox(5,0),
            sizeBtn(2,  "1px"),
            sizeBtn(4,  "2px"),
            sizeBtn(8,  "4px"),
            sizeBtn(16, "8px"));
        sizeRow->setSpacing(4);
        sizeRow->setCrossAxisAlignment(CrossAxisAlignment::Center);

        // ── Zoom controls ─────────────────────────────────────────────────────
        // [P5] Buttons call into canvas_->viewport() directly.
        // Zoom label reads zoomLevel State<float> which is updated in
        // onStateChanged and on each button tap.

        auto mkZoomBtn = [&](const std::string &lbl, std::function<void()> cb) {
            return GestureDetector(
                Container(Text(lbl)->setFontSize(14))
                    ->setWidth(28)->setHeight(26)->setBorderRadius(4)
                    ->setBackgroundColor(RGB(17,17,27))
                    ->setBorderColor(RGB(49,50,68))
                    ->setBorderWidth(1)->setPadding(4))
            ->setOnTap(cb);
        };

        auto zoomOutBtn = mkZoomBtn("−", [this]() {
            if (canvas_) {
                canvas_->viewport().zoomOut();
                zoomLevel.set(canvas_->viewport().targetZoom());
                canvas_->redraw();
            }
        });

        auto zoomInBtn = mkZoomBtn("+", [this]() {
            if (canvas_) {
                canvas_->viewport().zoomIn();
                zoomLevel.set(canvas_->viewport().targetZoom());
                canvas_->redraw();
            }
        });

        auto zoomLabel = Text(zoomLevel, [](const float &z) {
                char buf[12];
                _snprintf_s(buf, sizeof(buf), _TRUNCATE, "%d%%", (int)(z*100.f+0.5f));
                return std::string(buf);
            })
            ->setFontSize(11)
            ->setTextColor(RGB(230,219,116))
            ->setMinWidth(42);

        auto resetZoomBtn =
            GestureDetector(
                Container(Text("1:1")->setFontSize(10))
                    ->setWidth(34)->setHeight(26)->setBorderRadius(4)
                    ->setBackgroundColor(RGB(17,17,27))
                    ->setBorderColor(RGB(49,50,68))
                    ->setBorderWidth(1)->setPadding(4))
            ->setOnTap([this]() {
                if (canvas_) {
                    canvas_->viewport().resetZoom();
                    zoomLevel.set(1.0f);
                    canvas_->redraw();
                }
            });

        auto fitBtn =
            GestureDetector(
                Container(Text("Fit")->setFontSize(10))
                    ->setWidth(30)->setHeight(26)->setBorderRadius(4)
                    ->setBackgroundColor(RGB(17,17,27))
                    ->setBorderColor(RGB(49,50,68))
                    ->setBorderWidth(1)->setPadding(4))
            ->setOnTap([this]() {
                if (canvas_) {
                    canvas_->viewport().fitToView();
                    zoomLevel.set(canvas_->viewport().targetZoom());
                    canvas_->redraw();
                }
            });

        auto zoomRow = Row(
            Text("View:")->setFontSize(11)->setTextColor(RGB(117,113,94)),
            SizedBox(4,0),
            zoomOutBtn, SizedBox(3,0),
            zoomLabel,  SizedBox(3,0),
            zoomInBtn,  SizedBox(6,0),
            resetZoomBtn, SizedBox(4,0),
            fitBtn);
        zoomRow->setSpacing(0);
        zoomRow->setCrossAxisAlignment(CrossAxisAlignment::Center);

        // ── Grid toggle ───────────────────────────────────────────────────────
        auto gridBtn =
            GestureDetector(
                Container(Text("Grid")->setFontSize(11))
                    ->setWidth(40)->setHeight(26)->setBorderRadius(4)
                    ->setBackgroundColor(showGrid, [](const bool &on){
                        return on ? RGB(39,40,34) : RGB(17,17,27);
                    })
                    ->setBorderColor(showGrid, [](const bool &on){
                        return on ? RGB(174,129,255) : RGB(49,50,68);
                    })
                    ->setBorderWidth(showGrid, [](const bool &on){ return on ? 2 : 1; })
                    ->setPadding(4))
            ->setOnTap([this]() { showGrid.set(!showGrid.get()); });

        // ── Action buttons ─────────────────────────────────────────────────────
        auto mkActionBtn = [&](const std::string &lbl, COLORREF fg,
                               std::function<void()> cb) {
            return Button(lbl, cb)
                ->setBackgroundColor(RGB(17,17,27))
                ->setTextColor(fg)
                ->setBorderRadius(4)
                ->setWidth(48)->setHeight(26)->setPadding(4);
        };

        auto undoBtn = mkActionBtn("Undo", RGB(102,217,232), [this]() {
            if (surface_ && surface_->canUndo()) {
                surface_->undo();
                changeStamp.set(changeStamp.get() + 1);
            }
        });
        auto redoBtn = mkActionBtn("Redo", RGB(166,226,46), [this]() {
            if (surface_ && surface_->canRedo()) {
                surface_->redo();
                changeStamp.set(changeStamp.get() + 1);
            }
        });
        auto clearBtn = mkActionBtn("New", RGB(249,38,114), [this]() {
            if (surface_) { surface_->clear(); changeStamp.set(0); }
        });
        clearBtn->setWidth(40);

        // ── Active colour preview ─────────────────────────────────────────────
        auto colorPreview =
            Container(nullptr)
                ->setWidth(32)->setHeight(32)->setBorderRadius(4)
                ->setBackgroundColor(activeColor, [](const std::string &col){
                    return hexToRef(col);
                })
                ->setBorderWidth(2)
                ->setBorderColor(RGB(117,113,94));

        // ── Toolbar layout ────────────────────────────────────────────────────
        auto leftSide = Row(colorPreview, SizedBox(10,0), paletteCol);
        leftSide->setCrossAxisAlignment(CrossAxisAlignment::Center);

        auto rightCol = std::make_shared<ColumnWidget>();
        rightCol->setSpacing(5);
        rightCol->addChild(toolRow);
        rightCol->addChild(sizeRow);
        {
            auto zg = Row(zoomRow, SizedBox(8,0), gridBtn);
            zg->setCrossAxisAlignment(CrossAxisAlignment::Center);
            rightCol->addChild(zg);
        }
        {
            auto acts = Row(undoBtn, SizedBox(4,0), redoBtn, SizedBox(4,0), clearBtn);
            acts->setCrossAxisAlignment(CrossAxisAlignment::Center);
            rightCol->addChild(acts);
        }

        auto toolbarRow = Row(leftSide, SizedBox(20,0), rightCol);
        toolbarRow->setCrossAxisAlignment(CrossAxisAlignment::Center);

        auto toolbar = Container(toolbarRow)
            ->setBackgroundColor(RGB(11,11,19))
            ->setPadding(10);

        return Scaffold(AppBar("Pixel Art"),
                        Column(toolbar, canvas_)->setSpacing(0));
    }
};

// ── Entry point ───────────────────────────────────────────────────────────────

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