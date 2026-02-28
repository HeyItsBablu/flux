// test_canvas_viewport.cpp
#include "flux.hpp"

// ============================================================================
// PaintSurface
// ============================================================================

class PaintSurface : public RasterSurface {
public:
    std::function<void()> onStateChanged;

    PaintSurface() {
        StrokeStyle s;
        s.r=0.f; s.g=0.f; s.b=0.f; s.a=1.f;
        s.radius=10.f; s.opacity=1.f; s.hardness=0.9f;
        setStrokeStyle(s);
    }

    void onMouseUp(float x, float y) override {
        RasterSurface::onMouseUp(x, y);
        if (onStateChanged) onStateChanged();
    }

    void onKeyDown(int key) override {
        RasterSurface::onKeyDown(key);
        if (onStateChanged) onStateChanged();
    }
};

// ============================================================================
// ViewportCanvasApp
// ============================================================================

class ViewportCanvasApp : public Component {
    State<bool>   canUndo, canRedo;
    State<double> zoomLevel;   // percent: 6.25 – 3200

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
        double pct = double(zoom) * 100.0;
        if (std::abs(pct - zoomLevel.get()) > 0.5)
            zoomLevel.set(pct);
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
    ViewportCanvasApp()
        : canUndo   (false,   context)
        , canRedo   (false,   context)
        , zoomLevel (100.0,   context)
    {}

    WidgetPtr build() override {

        // ── Canvas — view smaller than canvas so scrollbars appear ────────────
        auto canvas = RasterCanvas(900, 580, 1400, 1050);
        surface_    = canvas->setSurface<PaintSurface>();
        canvasPtr_  = canvas.get();

        surface_->onStateChanged      = [this]()        { refreshUndoRedo(); };
        canvas->onViewportChanged     = [this](float z)  { syncZoomState(z); };

        zoomLevel.listen([this](double pct) { applyZoomFromSlider(pct); });

        // ── Zoom controls ─────────────────────────────────────────────────────
        auto zoomSlider = Slider(kZoomMin, kZoomMax, 0.25)
            ->setValue(zoomLevel)
            ->setTrackFillColor(RGB(174,129,255))
            ->setWidth(140);

        auto zoomLabel = Text(zoomLevel, [](double v){
            return std::to_string(int(std::round(v))) + "%";
        })->setFontSize(11)->setTextColor(RGB(180,180,200))->setMinWidth(44);

        auto resetBtn = Tooltip(
            Button("1:1", [this]{
                if (canvasPtr_) {
                    canvasPtr_->viewport().resetZoom();
                    canvasPtr_->redraw();
                    syncZoomState(1.f);
                }
            })
            ->setBackgroundColor(RGB(35,35,50))
            ->setHoverBackgroundColor(RGB(55,55,75))
            ->setTextColor(RGB(174,129,255))
            ->setBorderRadius(6)
            ->setWidth(36)->setHeight(26),
            "Reset zoom  Ctrl+0"
        );

        auto fitBtn = Tooltip(
            Button("Fit", [this]{
                if (canvasPtr_) {
                    canvasPtr_->viewport().fitToView();
                    canvasPtr_->redraw();
                    syncZoomState(canvasPtr_->viewport().zoom());
                }
            })
            ->setBackgroundColor(RGB(35,35,50))
            ->setHoverBackgroundColor(RGB(55,55,75))
            ->setTextColor(RGB(174,129,255))
            ->setBorderRadius(6)
            ->setWidth(36)->setHeight(26),
            "Fit canvas to view"
        );

        auto zoomRow = Row(
            Text("Zoom")->setFontSize(11)->setTextColor(RGB(140,140,160)),
            SizedBox(8,0),
            zoomSlider,
            SizedBox(8,0),
            zoomLabel,
            SizedBox(6,0),
            resetBtn,
            SizedBox(4,0),
            fitBtn
        );
        zoomRow->setCrossAxisAlignment(CrossAxisAlignment::Center);
        zoomRow->setSpacing(0);

        // ── Undo / Redo / Clear ───────────────────────────────────────────────
        auto undoBtn = Tooltip(
            Button("↩ Undo", [this]{
                if (surface_) { surface_->undo(); refreshUndoRedo(); }
            })
            ->setBackgroundColor(RGB(35,35,50))
            ->setHoverBackgroundColor(RGB(55,55,75))
            ->setTextColor(RGB(102,217,232))
            ->setBorderRadius(6)->setWidth(78)->setHeight(28),
            "Undo  Ctrl+Z"
        );
        auto redoBtn = Tooltip(
            Button("Redo ↪", [this]{
                if (surface_) { surface_->redo(); refreshUndoRedo(); }
            })
            ->setBackgroundColor(RGB(35,35,50))
            ->setHoverBackgroundColor(RGB(55,55,75))
            ->setTextColor(RGB(166,226,46))
            ->setBorderRadius(6)->setWidth(78)->setHeight(28),
            "Redo  Ctrl+Y"
        );
        auto clearBtn = Tooltip(
            Button("✕ Clear", [this]{
                if (surface_) { surface_->clear(); refreshUndoRedo(); }
            })
            ->setBackgroundColor(RGB(35,35,50))
            ->setHoverBackgroundColor(RGB(70,25,25))
            ->setTextColor(RGB(249,38,114))
            ->setBorderRadius(6)->setWidth(78)->setHeight(28),
            "Clear canvas"
        );
        auto actionsRow = Row(undoBtn, SizedBox(5,0), redoBtn, SizedBox(5,0), clearBtn);
        actionsRow->setCrossAxisAlignment(CrossAxisAlignment::Center);
        actionsRow->setSpacing(0);

        // ── Divider helper ────────────────────────────────────────────────────
        auto vdiv = []() -> WidgetPtr {
            return Container(nullptr)
                ->setWidth(1)->setHeight(28)
                ->setBackgroundColor(RGB(50,50,68));
        };

        // ── Hint row ──────────────────────────────────────────────────────────
        auto hintRow = Row(
            Text("MMB / Space+LMB: Pan") ->setFontSize(10)->setTextColor(RGB(75,75,95)),
            SizedBox(10,0),
            Text("Ctrl+Scroll: Zoom")    ->setFontSize(10)->setTextColor(RGB(75,75,95)),
            SizedBox(10,0),
            Text("Scroll: Pan vertical") ->setFontSize(10)->setTextColor(RGB(75,75,95)),
            SizedBox(10,0),
            Text("Shift+Scroll: Pan horizontal")->setFontSize(10)->setTextColor(RGB(75,75,95))
        );
        hintRow->setSpacing(0);

        // ── Toolbar ───────────────────────────────────────────────────────────
        auto topRow = Row(
            zoomRow,
            SizedBox(12,0), vdiv(), SizedBox(12,0),
            actionsRow
        );
        topRow->setCrossAxisAlignment(CrossAxisAlignment::Center);
        topRow->setSpacing(0);

        auto toolbar = Container(
            Column(topRow, SizedBox(0,4), hintRow)->setSpacing(0)
        )
        ->setBackgroundColor(RGB(16,16,24))
        ->setPaddingAll(12, 8, 12, 6);

        // ── Context menu ──────────────────────────────────────────────────────
        auto canvasWithMenu = ContextMenu(
            canvas,
            {
                { "Undo",  [this]{ if(surface_){ surface_->undo();  refreshUndoRedo(); } } },
                { "Redo",  [this]{ if(surface_){ surface_->redo();  refreshUndoRedo(); } } },
                ContextMenuItem::Separator(),
                { "Zoom 1:1", [this]{
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
            }
        );

        // ── Root ──────────────────────────────────────────────────────────────
        return Scaffold(
            AppBar("FluxCanvas — Viewport"),
            Column(toolbar, canvasWithMenu)->setSpacing(0)
        );
    }
};

// ============================================================================
// Entry point
// ============================================================================

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    FluxUI app(hInstance);
    app.build([&](){
        return FluxApp("FluxCanvas", BuildComponent<ViewportCanvasApp>(), AppTheme::dark());
    });
    app.createWindow("FluxCanvas — Viewport", 980, 730);
    return app.run();
}