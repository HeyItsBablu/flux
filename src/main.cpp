// test_paint_viewport.cpp
#include "flux.hpp"

// ── Hex → COLORREF helper ────────────────────────────────────────────────────
static COLORREF hexToRef(const std::string &css) {
    RGBA c = parseHexColor(css);
    return RGB((BYTE)(c.r*255), (BYTE)(c.g*255), (BYTE)(c.b*255));
}

// ============================================================================
// PaintSurface
// ============================================================================

class PaintSurface : public RasterSurface {
public:
    const std::vector<std::pair<std::string,std::string>> kPalette = {
        {"#cba6f7","Purple"}, {"#f38ba8","Pink"},  {"#fab387","Peach"},
        {"#f9e2af","Yellow"}, {"#a6e3a1","Green"}, {"#89dceb","Sky"},
        {"#89b4fa","Blue"},   {"#ffffff","White"},  {"#6c7086","Gray"},
        {"#1e1e2e","Eraser"},
    };

    std::string activeColor = "#cba6f7";
    float       activeSize  = 4.f;

    std::function<void()> onStateChanged;

    void setColor(const std::string &col) { activeColor=col; syncStyle(); }
    void setSize (float sz)               { activeSize=sz;   syncStyle(); }

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
        s.r=c.r; s.g=c.g; s.b=c.b; s.a=1.f;
        s.radius=activeSize*0.5f; s.opacity=1.f;
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

    void refreshUndoRedo() {
        if (!surface_) return;
        canUndo.set(surface_->canUndo());
        canRedo.set(surface_->canRedo());
    }

    void syncZoomState(float zoom) {
        double pct = double(zoom)*100.0;
        if (std::abs(pct - zoomLevel.get()) > 0.5)
            zoomLevel.set(pct);
    }

    void applyZoomFromSlider(double pct) {
        if (!canvasPtr_) return;
        Viewport &vp = canvasPtr_->viewport();
        float target  = float(pct/100.0);
        float current = vp.zoom();
        if (std::abs(target-current) < 0.0001f) return;
        vp.zoomToward(vp.viewW()*0.5f, vp.viewH()*0.5f, target/current);
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

        // ── Canvas ────────────────────────────────────────────────────────────
        auto canvas = RasterCanvas(860, 540, 1400, 1050);
        surface_    = canvas->setSurface<PaintSurface>();
        canvasPtr_  = canvas.get();

        surface_->onStateChanged  = [this]()       { refreshUndoRedo(); };
        canvas->onViewportChanged = [this](float z) { syncZoomState(z); };

        activeColor.listen([this](const std::string &col) { surface_->setColor(col); });
        activeSize .listen([this](float sz)               { surface_->setSize(sz);   });
        zoomLevel  .listen([this](double pct)             { applyZoomFromSlider(pct); });

        // ── Palette swatches ──────────────────────────────────────────────────
        auto paletteRow = std::make_shared<RowWidget>();
        for (const auto &[col, label] : surface_->kPalette) {
            std::string c = col;
            paletteRow->addChild(
                GestureDetector(
                    Container(nullptr)
                        ->setWidth(24)->setHeight(24)
                        ->setBorderRadius(12)
                        ->setBackgroundColor(hexToRef(col))
                        ->setBorderWidth(activeColor, [c](const std::string &a){
                            return a==c ? 3 : 0;
                        })
                        ->setBorderColor(activeColor, [c](const std::string &a){
                            return a==c ? RGB(255,255,255) : RGB(0,0,0);
                        })
                )->setOnTap([this,c](){ activeColor.set(c); })
            );
        }
        paletteRow->setSpacing(4);
        paletteRow->setCrossAxisAlignment(CrossAxisAlignment::Center);

        // ── Brush size buttons ────────────────────────────────────────────────
        auto sizeBtn = [&](float sz, const std::string &lbl) -> WidgetPtr {
            float s = sz;
            return GestureDetector(
                Container(Text(lbl)->setFontSize(11))
                    ->setWidth(30)->setHeight(24)
                    ->setBorderRadius(5)
                    ->setBackgroundColor(activeSize, [s](const float &a){
                        return a==s ? RGB(49,50,68) : RGB(24,24,37);
                    })
                    ->setBorderColor(activeSize, [s](const float &a){
                        return a==s ? RGB(203,166,247) : RGB(49,50,68);
                    })
                    ->setBorderWidth(activeSize, [s](const float &a){
                        return a==s ? 2 : 1;
                    })
                    ->setPadding(4)
            )->setOnTap([this,s](){ activeSize.set(s); });
        };

        auto sizeRow = Row(sizeBtn(2.f,"S"), sizeBtn(5.f,"M"),
                           sizeBtn(10.f,"L"), sizeBtn(20.f,"XL"));
        sizeRow->setSpacing(4);
        sizeRow->setCrossAxisAlignment(CrossAxisAlignment::Center);

        // ── Undo / Redo / Clear ───────────────────────────────────────────────
        auto undoBtn = Button("↩ Undo", [this]{
            if (surface_) { surface_->undo(); refreshUndoRedo(); }
        })
        ->setBackgroundColor(RGB(24,24,37))
        ->setTextColor(RGB(137,180,250))
        ->setBorderRadius(5)->setWidth(62)->setHeight(26)->setPadding(4);

        auto redoBtn = Button("Redo ↪", [this]{
            if (surface_) { surface_->redo(); refreshUndoRedo(); }
        })
        ->setBackgroundColor(RGB(24,24,37))
        ->setTextColor(RGB(166,226,46))
        ->setBorderRadius(5)->setWidth(62)->setHeight(26)->setPadding(4);

        auto clearBtn = Button("✕ Clear", [this]{
            if (surface_) { surface_->clear(); refreshUndoRedo(); }
        })
        ->setBackgroundColor(RGB(24,24,37))
        ->setTextColor(RGB(243,139,168))
        ->setBorderRadius(5)->setWidth(62)->setHeight(26)->setPadding(4);

        auto actionRow = Row(undoBtn, SizedBox(4,0), redoBtn, SizedBox(4,0), clearBtn);
        actionRow->setSpacing(0);
        actionRow->setCrossAxisAlignment(CrossAxisAlignment::Center);

        // ── Zoom controls ─────────────────────────────────────────────────────
        auto resetBtn = Button("1:1", [this]{
            if (canvasPtr_) {
                canvasPtr_->viewport().resetZoom();
                canvasPtr_->redraw();
                syncZoomState(1.f);
            }
        })
        ->setBackgroundColor(RGB(24,24,37))
        ->setTextColor(RGB(174,129,255))
        ->setBorderRadius(5)->setWidth(32)->setHeight(24)->setPadding(4);

        auto fitBtn = Button("Fit", [this]{
            if (canvasPtr_) {
                canvasPtr_->viewport().fitToView();
                canvasPtr_->redraw();
                syncZoomState(canvasPtr_->viewport().zoom());
            }
        })
        ->setBackgroundColor(RGB(24,24,37))
        ->setTextColor(RGB(174,129,255))
        ->setBorderRadius(5)->setWidth(32)->setHeight(24)->setPadding(4);

        auto zoomRow = Row(
            Text("Zoom")->setFontSize(11)->setTextColor(RGB(140,140,160)),
            SizedBox(6,0),
            Slider(kZoomMin, kZoomMax, 0.25)
                ->setValue(zoomLevel)
                ->setTrackFillColor(RGB(174,129,255))
                ->setWidth(120),
            SizedBox(6,0),
            Text(zoomLevel, [](double v){
                return std::to_string(int(std::round(v))) + "%";
            })->setFontSize(11)->setTextColor(RGB(180,180,200))->setMinWidth(40),
            SizedBox(4,0),
            resetBtn,
            SizedBox(4,0),
            fitBtn
        );
        zoomRow->setSpacing(0);
        zoomRow->setCrossAxisAlignment(CrossAxisAlignment::Center);

        // ── Divider ───────────────────────────────────────────────────────────
        auto vdiv = []() -> WidgetPtr {
            return Container(nullptr)
                ->setWidth(1)->setHeight(26)
                ->setBackgroundColor(RGB(49,50,68));
        };

        // ── Toolbar ───────────────────────────────────────────────────────────
        auto toolbarRow = Row(
            paletteRow,
            SizedBox(10,0), vdiv(), SizedBox(10,0),
            sizeRow,
            SizedBox(10,0), vdiv(), SizedBox(10,0),
            zoomRow,
            SizedBox(10,0), vdiv(), SizedBox(10,0),
            actionRow
        );
        toolbarRow->setSpacing(0);
        toolbarRow->setCrossAxisAlignment(CrossAxisAlignment::Center);

        auto toolbar = Container(toolbarRow)
            ->setBackgroundColor(RGB(17,17,27))
            ->setPadding(10)
            ->setHeight(52);

        // ── Context menu ──────────────────────────────────────────────────────
        auto canvasWithMenu = ContextMenu(canvas, {
            { "Undo",  [this]{ if(surface_){ surface_->undo();  refreshUndoRedo(); } } },
            { "Redo",  [this]{ if(surface_){ surface_->redo();  refreshUndoRedo(); } } },
            ContextMenuItem::Separator(),
            { "Zoom 1:1",    [this]{
                if(canvasPtr_){ canvasPtr_->viewport().resetZoom();
                                canvasPtr_->redraw(); syncZoomState(1.f); }
            }},
            { "Fit to view", [this]{
                if(canvasPtr_){ canvasPtr_->viewport().fitToView();
                                canvasPtr_->redraw();
                                syncZoomState(canvasPtr_->viewport().zoom()); }
            }},
            ContextMenuItem::Separator(),
            { "Clear", [this]{ if(surface_){ surface_->clear(); refreshUndoRedo(); } } },
        });

        // ── Hints ─────────────────────────────────────────────────────────────
        auto hints = Container(
            Row(
                Text("MMB / Space+LMB: Pan")->setFontSize(10)->setTextColor(RGB(75,75,95)),
                SizedBox(10,0),
                Text("Ctrl+Scroll: Zoom")   ->setFontSize(10)->setTextColor(RGB(75,75,95)),
                SizedBox(10,0),
                Text("Ctrl+Z/Y: Undo/Redo") ->setFontSize(10)->setTextColor(RGB(75,75,95))
            )->setSpacing(0)
        )->setBackgroundColor(RGB(17,17,27))->setPaddingAll(10,3,10,3);

        return Scaffold(
            AppBar("Paint — Viewport"),
            Column(toolbar, hints, canvasWithMenu)->setSpacing(0)
        );
    }
};

// ── Entry point ──────────────────────────────────────────────────────────────

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    FluxUI app(hInstance);
    app.build([&](){
        return FluxApp("Paint", BuildComponent<PaintApp>(), AppTheme::dark());
    });
    app.createWindow("Paint — Viewport", 920, 680);
    return app.run();
}