// test_canvas_simple.cpp
#include "flux.hpp"

class SimplePaintSurface : public RasterSurface {
public:
    std::function<void()> onStateChanged;

    SimplePaintSurface() {
        StrokeStyle s;
        s.r=0.f; s.g=0.f; s.b=0.f; s.a=1.f;
        s.radius=10.f; s.opacity=1.f; s.hardness=0.9f;
        setStrokeStyle(s);
    }

    void onMouseUp(float x, float y) override {
        RasterSurface::onMouseUp(x, y);
        if (onStateChanged) onStateChanged();
    }
};

class SimpleCanvasApp : public Component {
    State<bool> canUndo, canRedo;
    std::shared_ptr<SimplePaintSurface> surface_;

    void refreshUndoRedo() {
        if (!surface_) return;
        canUndo.set(surface_->canUndo());
        canRedo.set(surface_->canRedo());
    }

public:
    SimpleCanvasApp()
        : canUndo(false, context)
        , canRedo(false, context)
    {}

    WidgetPtr build() override {
        // Canvas and canvas size are the same — no scrollbars, no deadzone
        auto canvas = RasterCanvas(900, 580);
        surface_ = canvas->setSurface<SimplePaintSurface>();
        surface_->onStateChanged = [this]() { refreshUndoRedo(); };

        // Disable all zoom/pan by intercepting viewport changes and doing nothing
        canvas->onViewportChanged = nullptr;

        // Lock viewport to 1:1 after creation
        canvas->viewport().resetZoom();

        auto undoBtn = Button("↩ Undo", [this]{
            if (surface_) { surface_->undo(); refreshUndoRedo(); }
        });
        auto redoBtn = Button("Redo ↪", [this]{
            if (surface_) { surface_->redo(); refreshUndoRedo(); }
        });
        auto clearBtn = Button("✕ Clear", [this]{
            if (surface_) { surface_->clear(); refreshUndoRedo(); }
        });

        auto toolbar = Container(
            Row(undoBtn, SizedBox(6,0), redoBtn, SizedBox(6,0), clearBtn)->setSpacing(0)
        )->setBackgroundColor(RGB(16,16,24))->setPaddingAll(10,8,10,8);

        return Scaffold(
            AppBar("SimpleCanvas — No Viewport"),
            Column(toolbar, canvas)->setSpacing(0)
        );
    }
};

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    FluxUI app(hInstance);
    app.build([&](){
        return FluxApp("SimpleCanvas", BuildComponent<SimpleCanvasApp>(), AppTheme::dark());
    });
    app.createWindow("SimpleCanvas", 960, 680);
    return app.run();
}