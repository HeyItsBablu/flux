#include "flux.hpp"

// ── Hex → COLORREF helper ────────────────────────────────────────────────────
static COLORREF hexToRef(const std::string &css) {
    RGBA c = parseHexColor(css);
    return RGB((BYTE)(c.r * 255), (BYTE)(c.g * 255), (BYTE)(c.b * 255));
}

// ============================================================================
// PaintSurface  —  RenderSurface subclass
// ============================================================================
//
// Owns all drawing state (strokes, palette selection, brush size).
// CanvasWidget forwards every mouse / key event here automatically.

class PaintSurface : public RasterSurface {
public:
    // ── Palette ──────────────────────────────────────────────────────────────
    const std::vector<std::pair<std::string, std::string>> kPalette = {
        {"#cba6f7", "Purple"}, {"#f38ba8", "Pink"},  {"#fab387", "Peach"},
        {"#f9e2af", "Yellow"}, {"#a6e3a1", "Green"}, {"#89dceb", "Sky"},
        {"#89b4fa", "Blue"},   {"#ffffff", "White"}, {"#6c7086", "Gray"},
        {"#1e1e2e", "Eraser"},
    };

    // Active style — kept in sync with the toolbar widgets via callbacks
    std::string activeColor = "#cba6f7";
    float       activeSize  = 4.f;

    // Callbacks so the toolbar can be notified when the surface changes state
    // (e.g. after Ctrl+Z undo, so the undo button can refresh its enabled state)
    std::function<void()> onStateChanged;

    // ── Public helpers called from the toolbar ────────────────────────────────

    void setColor(const std::string &col) {
        activeColor = col;
        syncStyle();
    }

    void setSize(float sz) {
        activeSize = sz;
        syncStyle();
    }

    // ── RenderSurface overrides ───────────────────────────────────────────────

    void initialize(int w, int h) override {
        RasterSurface::initialize(w, h);
        syncStyle();
    }

    // Ctrl+Z / Ctrl+Y are handled inside RasterSurface::onKeyDown already.
    // We override to also fire the state-change callback so the toolbar can
    // update button states.
    void onKeyDown(int key) override {
        RasterSurface::onKeyDown(key); // undo / redo logic lives there
        if (onStateChanged) onStateChanged();
    }

private:
    void syncStyle() {
        RGBA c = parseHexColor(activeColor);
        StrokeStyle s;
        s.r       = c.r;
        s.g       = c.g;
        s.b       = c.b;
        s.a       = 1.f;
        s.radius  = activeSize * 0.5f;
        s.opacity = 1.f;
        setStrokeStyle(s);
    }
};

// ============================================================================
// PaintApp  —  Component
// ============================================================================

class PaintApp : public Component {
    State<std::string> activeColor;
    State<float>       activeSize;
    State<int>         strokeCount; // drives undo-button repaint

    std::shared_ptr<PaintSurface> surface_;

public:
    PaintApp()
        : activeColor("#cba6f7", context),
          activeSize(4.f, context),
          strokeCount(0, context) {}

    WidgetPtr build() override {

        // ── Canvas + surface ──────────────────────────────────────────────────
        auto canvas = Canvas(560, 400);
        surface_ = canvas->setSurface<PaintSurface>();

        // Wire surface callbacks back into reactive state so toolbar redraws
        surface_->onStateChanged = [this]() {
            strokeCount.set(strokeCount.get() + 1); // any change → repaint
        };

        // Keep surface style in sync whenever toolbar state changes
        activeColor.listen([this](const std::string &col) {
            surface_->setColor(col);
        });
        activeSize.listen([this](float sz) {
            surface_->setSize(sz);
        });

        // ── Palette swatches ──────────────────────────────────────────────────
        std::vector<WidgetPtr> swatches;
        for (const auto &[col, label] : surface_->kPalette) {
            std::string c = col;
            swatches.push_back(
                GestureDetector(
                    Container(nullptr)
                        ->setWidth(26)->setHeight(26)
                        ->setBorderRadius(13)
                        ->setBackgroundColor(hexToRef(col))
                        ->setBorderWidth(activeColor, [c](const std::string &active) {
                            return active == c ? 3 : 0;
                        })
                        ->setBorderColor(activeColor, [c](const std::string &active) {
                            return active == c ? RGB(255, 255, 255) : RGB(0, 0, 0);
                        }))
                ->setOnTap([this, c]() { activeColor.set(c); })
            );
        }

        // ── Brush-size buttons ────────────────────────────────────────────────
        auto sizeBtn = [&](float sz, const std::string &lbl) -> WidgetPtr {
            float s = sz;
            return GestureDetector(
                Container(Text(lbl)->setFontSize(11))
                    ->setWidth(32)->setHeight(26)
                    ->setBorderRadius(5)
                    ->setBackgroundColor(activeSize, [s](const float &active) {
                        return active == s ? RGB(49, 50, 68) : RGB(24, 24, 37);
                    })
                    ->setBorderColor(activeSize, [s](const float &active) {
                        return active == s ? RGB(203, 166, 247) : RGB(49, 50, 68);
                    })
                    ->setBorderWidth(activeSize, [s](const float &active) {
                        return active == s ? 2 : 1;
                    })
                    ->setPadding(4))
            ->setOnTap([this, s]() { activeSize.set(s); });
        };

        // ── Action buttons ────────────────────────────────────────────────────
        auto undoBtn = Button("Undo", [this]() {
                            if (surface_ && surface_->canUndo()) {
                                surface_->undo();
                                strokeCount.set(strokeCount.get() - 1);
                            }
                        })
                        ->setBackgroundColor(RGB(24, 24, 37))
                        ->setTextColor(RGB(137, 180, 250))
                        ->setBorderRadius(5)
                        ->setWidth(54)->setHeight(26)
                        ->setPadding(4);

        auto clearBtn = Button("Clear", [this]() {
                            if (surface_) {
                                surface_->clear();
                                strokeCount.set(0);
                            }
                        })
                        ->setBackgroundColor(RGB(24, 24, 37))
                        ->setTextColor(RGB(243, 139, 168))
                        ->setBorderRadius(5)
                        ->setWidth(54)->setHeight(26)
                        ->setPadding(4);

        // ── Toolbar layout ────────────────────────────────────────────────────
        auto paletteRow = std::make_shared<RowWidget>();
        for (auto &sw : swatches) paletteRow->addChild(sw);
        paletteRow->setSpacing(5);
        paletteRow->setCrossAxisAlignment(CrossAxisAlignment::Center);

        auto sizeRow = Row(sizeBtn(2.f, "S"), sizeBtn(5.f, "M"),
                           sizeBtn(10.f, "L"), sizeBtn(20.f, "XL"));
        sizeRow->setSpacing(4);
        sizeRow->setCrossAxisAlignment(CrossAxisAlignment::Center);

        auto actionRow = Row(undoBtn, SizedBox(4, 0), clearBtn);
        actionRow->setCrossAxisAlignment(CrossAxisAlignment::Center);

        auto toolbarRow = Row(paletteRow, SizedBox(14, 0),
                              sizeRow,    SizedBox(14, 0),
                              actionRow);
        toolbarRow->setSpacing(0);
        toolbarRow->setCrossAxisAlignment(CrossAxisAlignment::Center);

        auto toolbar = Container(toolbarRow)
                           ->setBackgroundColor(RGB(17, 17, 27))
                           ->setPadding(10)
                           ->setHeight(50);

        return Scaffold(AppBar("Paint"), Column(toolbar, canvas)->setSpacing(0));
    }
};

// ── Entry point ──────────────────────────────────────────────────────────────

WidgetPtr createApp(FluxUI *) {
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