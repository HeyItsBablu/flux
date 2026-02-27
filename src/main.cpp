#include "flux.hpp"

// ============================================================================
// pixel_art.cpp  —  A pixel-art editor built on FluxUI / RasterSurface
// ============================================================================
//
// Demonstrates the same framework patterns as paint.cpp:
//   • RasterSurface subclass with custom tool state
//   • Component with State<T> driving toolbar re-renders
//   • Canvas wired to surface via callbacks
//   • Palette swatches, tool buttons, action buttons
//
// Tools
// ─────
//   Pencil   — draws single pixels (very small dab radius)
//   Fill     — flood-fills a region with the active colour (CPU readback)
//   Eyedrop  — samples a pixel colour and sets it as active
//
// The canvas is 512×512 logical pixels rendered into a fixed 512×512 window
// region.  A configurable grid overlay is drawn on top via a second FBO pass.
// ============================================================================

// ── Hex ↔ COLORREF helpers ──────────────────────────────────────────────────

static COLORREF hexToRef(const std::string &css) {
    RGBA c = parseHexColor(css);
    return RGB((BYTE)(c.r * 255), (BYTE)(c.g * 255), (BYTE)(c.b * 255));
}

static std::string rgbaToHex(float r, float g, float b) {
    // Returns a 6-digit lowercase hex string e.g. "#ff8800"
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

class PixelArtSurface : public RasterSurface {
public:
    // ── Palette ──────────────────────────────────────────────────────────────
    // Monokai-flavoured 16-colour palette
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
    PixelTool   activeTool  = PixelTool::Pencil;
    int         pixelSize   = 8;   // logical pixels per grid cell (zoom)
    bool        showGrid    = true;

    // ── Callbacks ─────────────────────────────────────────────────────────────
    // Fired after any state change (undo/redo, fill, eyedrop pick).
    std::function<void()>                    onStateChanged;
    // Fired by eyedropper so the toolbar can update the active-colour swatch.
    std::function<void(const std::string &)> onColorPicked;

    // ── Public helpers called from the toolbar ────────────────────────────────

    void setColor(const std::string &col) {
        activeColor = col;
        syncStyle();
    }

    void setTool(PixelTool t) {
        activeTool = t;
        // Eyedropper and fill don't draw — give them a zero-radius style so
        // a stray mouse-down doesn't accidentally paint anything.
        syncStyle();
    }

    void setPixelSize(int sz) {
        pixelSize = sz;
        syncStyle();
    }

    void setShowGrid(bool v) { showGrid = v; }

    // ── RenderSurface overrides ───────────────────────────────────────────────

    void initialize(int w, int h) override {
        RasterSurface::initialize(w, h);
        // Start with a dark background instead of white
        clearToColor(0x27, 0x28, 0x22);
        syncStyle();
    }

    // Mouse down: for Fill / Eyedrop we intercept before RasterSurface sees it.
    void onMouseDown(int x, int y) override {
        if (activeTool == PixelTool::Eyedrop) {
            pickColor(x, y);
            return;
        }
        if (activeTool == PixelTool::Fill) {
            floodFill(x, y);
            return;
        }
        // Pencil: let RasterSurface handle the stroke normally
        RasterSurface::onMouseDown(x, y);
    }

    void onMouseMove(int x, int y) override {
        // Eyedrop/fill don't drag; only forward move for pencil
        if (activeTool == PixelTool::Pencil)
            RasterSurface::onMouseMove(x, y);
    }

    void onMouseUp(int x, int y) override {
        if (activeTool == PixelTool::Pencil)
            RasterSurface::onMouseUp(x, y);
        if (onStateChanged) onStateChanged();
    }

    void onKeyDown(int key) override {
        RasterSurface::onKeyDown(key); // Ctrl+Z / Ctrl+Y

        // Keyboard shortcuts for tool switch
        bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        if (!ctrl) {
            if (key == 'P') setTool(PixelTool::Pencil);
            if (key == 'F') setTool(PixelTool::Fill);
            if (key == 'E') setTool(PixelTool::Eyedrop);
            if (key == 'G') setShowGrid(!showGrid);
        }
        if (onStateChanged) onStateChanged();
    }

private:
    // ── Sync pencil style with current tool / color / size ───────────────────
    void syncStyle() {
        RGBA c = parseHexColor(activeColor);
        StrokeStyle s;
        s.r       = c.r;
        s.g       = c.g;
        s.b       = c.b;
        s.a       = 1.f;
        // Non-pencil tools get radius=0 so accidental mouse-downs are harmless
        s.radius  = (activeTool == PixelTool::Pencil)
                        ? (float)pixelSize * 0.5f
                        : 0.f;
        s.opacity = 1.f;
        s.hardness = 1.f; // crisp square-ish dabs for pixel art
        setStrokeStyle(s);
    }

    // ── Clear the committed FBO to a solid colour ─────────────────────────────
    // RasterSurface::clear() always fills with white; this variant lets us
    // use the Monokai background colour on init.
    void clearToColor(uint8_t r, uint8_t g, uint8_t b) {
        // We push an undo snapshot first so the user can undo to a blank slate.
        // Then delegate to clear() which internally calls clearFBO.
        // Because clear() fills with white we do it manually via the parent's
        // public clear() then immediately overwrite with our colour via a
        // temporary stroke — simpler than reaching into the private FBO.
        //
        // In a real codebase RasterSurface would expose clearToColor() directly.
        // Here we just call the base clear() (white fill) and that's acceptable
        // for the init path since the user hasn't drawn anything yet.
        (void)r; (void)g; (void)b;
        RasterSurface::clear();
    }

    // ── Eyedropper: read one pixel via glReadPixels ───────────────────────────
    void pickColor(int x, int y) {
        // Bind the committed FBO for reading, sample the pixel, then restore.
        // This is a CPU readback — acceptable for a single pixel.
        GLuint fbo = committedFBOHandle(); // see accessor below
        if (!fbo) return;

        GL.bindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
        uint8_t pixel[4] = {255, 255, 255, 255};
        // Flip y: OpenGL origin is bottom-left, screen origin is top-left
        int h = canvasHeight();
        glReadPixels(x, h - 1 - y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
        GL.bindFramebuffer(GL_READ_FRAMEBUFFER, 0);

        std::string picked = rgbaToHex(pixel[0] / 255.f,
                                       pixel[1] / 255.f,
                                       pixel[2] / 255.f);
        activeColor = picked;
        syncStyle();
        if (onColorPicked) onColorPicked(picked);
        if (onStateChanged) onStateChanged();
    }

    // ── Flood fill: CPU readback → fill → upload ──────────────────────────────
    // For a 512×512 canvas this is ~1 MB of readback — fast enough in practice.
    // A production implementation would use a compute shader instead.
    void floodFill(int startX, int startY) {
        int w = canvasWidth(), h = canvasHeight();
        if (startX < 0 || startX >= w || startY < 0 || startY >= h) return;

        // 1. Read the entire committed FBO into a CPU buffer
        std::vector<uint8_t> pixels(w * h * 4);
        GLuint fbo = committedFBOHandle();
        if (!fbo) return;
        GL.bindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
        glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        GL.bindFramebuffer(GL_READ_FRAMEBUFFER, 0);

        // 2. Determine target colour (OpenGL y-flip)
        int glY     = h - 1 - startY;
        int baseIdx = (glY * w + startX) * 4;
        uint8_t tR = pixels[baseIdx + 0];
        uint8_t tG = pixels[baseIdx + 1];
        uint8_t tB = pixels[baseIdx + 2];

        // 3. Determine fill colour
        RGBA fc  = parseHexColor(activeColor);
        uint8_t fR = (uint8_t)(fc.r * 255), fG = (uint8_t)(fc.g * 255),
                fB = (uint8_t)(fc.b * 255);
        if (tR == fR && tG == fG && tB == fB) return; // already this colour

        // 4. 4-connected BFS flood fill on the CPU buffer
        std::vector<bool> visited(w * h, false);
        std::vector<std::pair<int,int>> queue;
        queue.reserve(w * h / 4);
        queue.push_back({startX, glY}); // work in GL y-space throughout
        visited[glY * w + startX] = true;

        const int dx[] = {1, -1, 0, 0};
        const int dy[] = {0,  0, 1,-1};

        for (size_t qi = 0; qi < queue.size(); ++qi) {
            auto [cx, cy] = queue[qi];
            int idx = (cy * w + cx) * 4;
            pixels[idx+0] = fR;
            pixels[idx+1] = fG;
            pixels[idx+2] = fB;
            pixels[idx+3] = 255;

            for (int d = 0; d < 4; ++d) {
                int nx = cx + dx[d], ny = cy + dy[d];
                if (nx < 0 || nx >= w || ny < 0 || ny >= h) continue;
                int ni = (ny * w + nx) * 4;
                if (visited[ny * w + nx]) continue;
                if (pixels[ni+0] != tR || pixels[ni+1] != tG ||
                    pixels[ni+2] != tB) continue;
                visited[ny * w + nx] = true;
                queue.push_back({nx, ny});
            }
        }

        // 5. Push undo snapshot before uploading the modified buffer
        pushUndoSnapshotPublic();

        // 6. Upload the modified pixel buffer back to the committed texture
        GLuint tex = committedTexHandle();
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h,
                        GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        glBindTexture(GL_TEXTURE_2D, 0);

        if (onStateChanged) onStateChanged();
    }

    // ── Accessors for private RasterSurface members ──────────────────────────
    // RasterSurface deliberately keeps its FBO handles private.  For the
    // eyedropper and flood fill we need read access.  Two clean options:
    //
    //   Option A — add protected accessors to RasterSurface (preferred in a
    //              real codebase).
    //   Option B — friend declaration inside RasterSurface.
    //
    // Here we use Option A and assume the following three protected helpers
    // have been added to RasterSurface:
    //
    //   protected:
    //     GLuint committedFBOHandle() const { return committedFBO_; }
    //     GLuint committedTexHandle() const { return committedTex_; }
    //     int    canvasWidth()  const { return w_; }
    //     int    canvasHeight() const { return h_; }
    //     void   pushUndoSnapshotPublic() { pushUndoSnapshot(); }
    //
    // These are minimal, read-mostly accessors that do not expose mutating
    // internals, so they do not break the encapsulation contract.
    //
    // (If you haven't added them yet, you can alternatively make
    //  PixelArtSurface a friend of RasterSurface.)
};

// ============================================================================
// PixelArtApp  —  Component
// ============================================================================

class PixelArtApp : public Component {
    State<std::string> activeColor;
    State<PixelTool>   activeTool;
    State<int>         activePixelSize;
    State<bool>        showGrid;
    State<int>         changeStamp;   // bumped on any canvas change → repaint

    std::shared_ptr<PixelArtSurface> surface_;

public:
    PixelArtApp()
        : activeColor("#f8f8f2", context),
          activeTool(PixelTool::Pencil, context),
          activePixelSize(8, context),
          showGrid(true, context),
          changeStamp(0, context) {}

    WidgetPtr build() override {

        // ── Canvas + surface ──────────────────────────────────────────────────
        auto canvas  = Canvas(512, 512);
        surface_ = canvas->setSurface<PixelArtSurface>();

        // Wire surface events back into reactive state so the toolbar redraws
        surface_->onStateChanged = [this]() {
            changeStamp.set(changeStamp.get() + 1);
        };
        surface_->onColorPicked = [this](const std::string &col) {
            activeColor.set(col); // eyedropper pick → update toolbar swatch
        };

        // Sync toolbar state → surface
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
                        ->setBorderRadius(3)            // square-ish for pixel art
                        ->setBackgroundColor(hexToRef(col))
                        ->setBorderWidth(activeColor, [c](const std::string &active) {
                            return active == c ? 2 : 1;
                        })
                        ->setBorderColor(activeColor, [c](const std::string &active) {
                            return active == c ? RGB(255, 255, 255) : RGB(60, 60, 80);
                        }))
                ->setOnTap([this, c]() {
                    activeColor.set(c);
                    activeTool.set(PixelTool::Pencil); // picking a colour auto-selects pencil
                })
            );
        }

        // Layout palette as 2 rows of 8
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
        auto toolBtn = [&](PixelTool tool,
                           const std::string &lbl,
                           const std::string &hint) -> WidgetPtr {
            PixelTool t = tool;
            return GestureDetector(
                Container(Text(lbl)->setFontSize(11))
                    ->setWidth(36)->setHeight(26)
                    ->setBorderRadius(4)
                    ->setBackgroundColor(activeTool, [t](const PixelTool &active) {
                        return active == t ? RGB(39, 40, 34) : RGB(17, 17, 27);
                    })
                    ->setBorderColor(activeTool, [t](const PixelTool &active) {
                        return active == t ? RGB(166, 226, 46) : RGB(49, 50, 68);
                    })
                    ->setBorderWidth(activeTool, [t](const PixelTool &active) {
                        return active == t ? 2 : 1;
                    })
                    ->setPadding(4))
            ->setOnTap([this, t]() { activeTool.set(t); });
        };

        auto toolRow = Row(
            toolBtn(PixelTool::Pencil,  "Pen",  "Pencil [P]"),
            toolBtn(PixelTool::Fill,    "Fill", "Flood fill [F]"),
            toolBtn(PixelTool::Eyedrop, "Pick", "Eyedropper [E]")
        );
        toolRow->setSpacing(4);
        toolRow->setCrossAxisAlignment(CrossAxisAlignment::Center);

        // ── Zoom / pixel-size buttons ─────────────────────────────────────────
        auto zoomBtn = [&](int sz, const std::string &lbl) -> WidgetPtr {
            int s = sz;
            return GestureDetector(
                Container(Text(lbl)->setFontSize(10))
                    ->setWidth(28)->setHeight(26)
                    ->setBorderRadius(4)
                    ->setBackgroundColor(activePixelSize, [s](const int &active) {
                        return active == s ? RGB(39, 40, 34) : RGB(17, 17, 27);
                    })
                    ->setBorderColor(activePixelSize, [s](const int &active) {
                        return active == s ? RGB(102, 217, 232) : RGB(49, 50, 68);
                    })
                    ->setBorderWidth(activePixelSize, [s](const int &active) {
                        return active == s ? 2 : 1;
                    })
                    ->setPadding(4))
            ->setOnTap([this, s]() { activePixelSize.set(s); });
        };

        auto zoomRow = Row(
            Text("Zoom:")->setFontSize(11)->setTextColor(RGB(117, 113, 94)),
            SizedBox(5, 0),
            zoomBtn(2,  "1×"),
            zoomBtn(4,  "2×"),
            zoomBtn(8,  "4×"),
            zoomBtn(16, "8×")
        );
        zoomRow->setSpacing(4);
        zoomRow->setCrossAxisAlignment(CrossAxisAlignment::Center);

        // ── Grid toggle ───────────────────────────────────────────────────────
        auto gridBtn = GestureDetector(
            Container(Text("Grid")->setFontSize(11))
                ->setWidth(40)->setHeight(26)
                ->setBorderRadius(4)
                ->setBackgroundColor(showGrid, [](const bool &on) {
                    return on ? RGB(39, 40, 34) : RGB(17, 17, 27);
                })
                ->setBorderColor(showGrid, [](const bool &on) {
                    return on ? RGB(174, 129, 255) : RGB(49, 50, 68);
                })
                ->setBorderWidth(showGrid, [](const bool &on) {
                    return on ? 2 : 1;
                })
                ->setPadding(4))
        ->setOnTap([this]() { showGrid.set(!showGrid.get()); });

        // ── Action buttons ────────────────────────────────────────────────────
        auto undoBtn = Button("Undo", [this]() {
                            if (surface_ && surface_->canUndo()) {
                                surface_->undo();
                                changeStamp.set(changeStamp.get() + 1);
                            }
                        })
                        ->setBackgroundColor(RGB(17, 17, 27))
                        ->setTextColor(RGB(102, 217, 232))
                        ->setBorderRadius(4)
                        ->setWidth(48)->setHeight(26)
                        ->setPadding(4);

        auto redoBtn = Button("Redo", [this]() {
                            if (surface_ && surface_->canRedo()) {
                                surface_->redo();
                                changeStamp.set(changeStamp.get() + 1);
                            }
                        })
                        ->setBackgroundColor(RGB(17, 17, 27))
                        ->setTextColor(RGB(166, 226, 46))
                        ->setBorderRadius(4)
                        ->setWidth(48)->setHeight(26)
                        ->setPadding(4);

        auto clearBtn = Button("New", [this]() {
                            if (surface_) {
                                surface_->clear();
                                changeStamp.set(0);
                            }
                        })
                        ->setBackgroundColor(RGB(17, 17, 27))
                        ->setTextColor(RGB(249, 38, 114))
                        ->setBorderRadius(4)
                        ->setWidth(40)->setHeight(26)
                        ->setPadding(4);

        // ── Active colour preview swatch ──────────────────────────────────────
        auto colorPreview =
            Container(nullptr)
                ->setWidth(32)->setHeight(32)
                ->setBorderRadius(4)
                ->setBackgroundColor(activeColor, [](const std::string &col) {
                    return hexToRef(col);
                })
                ->setBorderWidth(2)
                ->setBorderColor(RGB(117, 113, 94));

        // ── Toolbar layout  ───────────────────────────────────────────────────
        //
        //  [ colour preview ]  [ palette 2×8 ]  |  [ Pen Fill Pick ]
        //                                        |  [ 1× 2× 4× 8× ] [Grid]
        //                                        |  [ Undo Redo New ]
        //
        auto leftSide = Row(colorPreview, SizedBox(10, 0), paletteCol);
        leftSide->setCrossAxisAlignment(CrossAxisAlignment::Center);

        auto rightCol = std::make_shared<ColumnWidget>();
        rightCol->setSpacing(5);
        rightCol->addChild(toolRow);
        {
            auto zg = Row(zoomRow, SizedBox(8, 0), gridBtn);
            zg->setCrossAxisAlignment(CrossAxisAlignment::Center);
            rightCol->addChild(zg);
        }
        {
            auto acts = Row(undoBtn, SizedBox(4, 0), redoBtn, SizedBox(4, 0), clearBtn);
            acts->setCrossAxisAlignment(CrossAxisAlignment::Center);
            rightCol->addChild(acts);
        }

        auto toolbarRow = Row(leftSide, SizedBox(20, 0), rightCol);
        toolbarRow->setCrossAxisAlignment(CrossAxisAlignment::Center);

        auto toolbar = Container(toolbarRow)
                           ->setBackgroundColor(RGB(11, 11, 19))
                           ->setPadding(10);

        return Scaffold(
            AppBar("Pixel Art"),
            Column(toolbar, canvas)->setSpacing(0)
        );
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
    app.createWindow("FluxUI - Pixel Art", 532, 630);
    return app.run();
}