// test.cpp  —  Paint app: Brush + Layers  (ReactiveItem revision)
//
// Migration summary:
//
//   BEFORE:  State<vector<LayerDesc>> layerDescs
//            onLayersChanged rebuilds the whole vector every time →
//            ListView tears down and recreates every row on any layer change.
//
//   AFTER:   State<vector<ReactiveItemPtr<LayerDesc>>> layerDescs
//            onLayersChanged diffs the new LayerDesc snapshot against the
//            existing ReactiveItem vector by layer name:
//              • New layer     → allocate ReactiveItemPtr, push to vector, set()
//              • Removed layer → erase from vector, set()
//              • Changed layer → ri->set(newDesc)  ← only that row rebuilds
//              • Reordered     → structural set() triggers keyed diff in ListView,
//                                but unchanged rows are reused from the widget cache
//
//   The ListView is keyed by pointer address (default) which is stable as long as
//   the same ReactiveItemPtr survives across onLayersChanged calls — which the
//   diff below guarantees for all layers that weren't added or removed.
//
//   NOTE: layer identity is keyed by LayerDesc::name.  If your surface ever
//   renames a layer in-place, add a stable integer ID to LayerDesc and key by
//   that instead.

#include "flux.hpp"
#include "widgets/flux_layers.hpp"
#include <unordered_set>

// ============================================================================
// PaintSurface
// ============================================================================

class PaintSurface : public LayeredSurface {
public:
    std::function<void()> onRedrawNeeded;

    void initialize(int w, int h) override {
        LayeredSurface::initialize(w, h);
        StrokeStyle s;
        s.r = 0; s.g = 0; s.b = 0; s.a = 1;
        s.radius    = 6;
        s.hardness  = 0.9f;
        s.opacity   = 1.f;
        s.tool      = kToolBrush;
        setStrokeStyle(s);
    }

    void onMouseUp(float x, float y) override {
        LayeredSurface::onMouseUp(x, y);
        if (onRedrawNeeded) onRedrawNeeded();
    }
};

// ============================================================================
// PaintApp
// ============================================================================

class PaintApp : public Component {
    State<float>    brushRadius;
    State<COLORREF> brushColor;

    // Each entry is a ReactiveItemPtr<LayerDesc>.
    // Structural changes (add/remove/reorder) go through items.set(newVec).
    // Per-layer field changes (visible, isActive) go through ri->set(newDesc).
    using LayerRef = ReactiveItemPtr<LayerDesc>;
    State<std::vector<LayerRef>> layerDescs;

    // Name → ReactiveItemPtr map — used by the diff in onLayersChanged to
    // reuse existing ReactiveItem allocations across callbacks.
    std::unordered_map<std::string, LayerRef> layerRefByName_;

    std::shared_ptr<PaintSurface> surface_;
    CanvasWidget* canvasPtr_ = nullptr;

public:
    PaintApp()
        : brushRadius(6.f,          context)
        , brushColor(RGB(0, 0, 0),  context)
        , layerDescs({},             context)
    {}

    WidgetPtr build() override {
        const int sw = GetSystemMetrics(SM_CXSCREEN);
        const int sh = GetSystemMetrics(SM_CYSCREEN);

        static constexpr int kSideW   = 192;
        static constexpr int kLayerW  = 192;
        static constexpr int kBarH    = 48;
        static constexpr int kCanvasW = 1240;
        static constexpr int kCanvasH = 1754;

        // ── Canvas ────────────────────────────────────────────────────────
        auto canvas = LayeredCanvas(sw - kSideW - kLayerW, sh - kBarH,
                                    kCanvasW, kCanvasH);
        surface_    = canvas->setSurface<PaintSurface>();
        canvasPtr_  = canvas.get();

        surface_->onRedrawNeeded = [this]() {
            if (canvasPtr_) canvasPtr_->redraw();
        };

        // ── Layer change handler — diff new snapshot against existing refs ──
        //
        // Goal: for every LayerDesc that already exists (same name), call
        // ri->set(newDesc) so only the affected row widget rebuilds.
        // For new / removed layers, update the outer vector so the ListView
        // can diff structurally (add/remove rows via the widget cache).

        surface_->onLayersChanged = [this](const LayerState& ls) {
            int count = int(ls.layers.size());

            // Build display-order list: top layer (highest stack index) first.
            std::vector<LayerDesc> ordered;
            ordered.reserve(count);
            for (int i = count - 1; i >= 0; i--)
                ordered.push_back(ls.layers[i]);

            // Track which names are still present so we can evict stale entries.
            std::unordered_set<std::string> activeNames;
            for (auto& ld : ordered) activeNames.insert(ld.name);

            // Evict refs for layers that no longer exist.
            for (auto it = layerRefByName_.begin(); it != layerRefByName_.end(); ) {
                if (!activeNames.count(it->first)) it = layerRefByName_.erase(it);
                else ++it;
            }

            // Build the new display vector, reusing or creating ReactiveItemPtrs.
            bool structuralChange = false;
            std::vector<LayerRef> newVec;
            newVec.reserve(count);

            for (auto& ld : ordered) {
                auto it = layerRefByName_.find(ld.name);
                if (it != layerRefByName_.end()) {
                    // Existing layer — update in place (only this row rebuilds).
                    it->second->set(ld);
                    newVec.push_back(it->second);
                } else {
                    // New layer — allocate a fresh ReactiveItemPtr.
                    auto ref = MakeReactive(ld);
                    layerRefByName_[ld.name] = ref;
                    newVec.push_back(ref);
                    structuralChange = true;
                }
            }

            // Structural change: count changed, order changed, or a layer was evicted.
            // Named bool avoids MSVC C3878 (lambda in short-circuit || condition).
            bool orderChanged = false;
            {
                const auto& cur = layerDescs.get();
                if (cur.size() != newVec.size()) {
                    orderChanged = true;
                } else {
                    for (size_t i = 0; i < cur.size(); i++) {
                        if (cur[i].get() != newVec[i].get()) { orderChanged = true; break; }
                    }
                }
            }
            if (structuralChange || int(layerDescs.get().size()) != count || orderChanged) {
                layerDescs.set(newVec);
            }
            // If only field values changed, ri->set() above already triggered
            // per-row rebuilds — no need to touch layerDescs at all.

            if (canvasPtr_) canvasPtr_->redraw();
        };

        brushColor.listen([this](COLORREF cr) {
            if (!surface_) return;
            StrokeStyle s = surface_->getStrokeStyle();
            s.r = GetRValue(cr) / 255.f;
            s.g = GetGValue(cr) / 255.f;
            s.b = GetBValue(cr) / 255.f;
            surface_->setStrokeStyle(s);
        });

        brushRadius.listen([this](float r) {
            if (!surface_) return;
            StrokeStyle s = surface_->getStrokeStyle();
            s.radius = r;
            surface_->setStrokeStyle(s);
        });

        // ── Theme ─────────────────────────────────────────────────────────
        const COLORREF kBg      = RGB(18,  18,  28 );
        const COLORREF kPanel   = RGB(24,  24,  36 );
        const COLORREF kCard    = RGB(30,  30,  46 );
        const COLORREF kBorder  = RGB(48,  50,  70 );
        const COLORREF kAccent  = RGB(130, 100, 220);
        const COLORREF kDim     = RGB(100, 105, 130);
        const COLORREF kText    = RGB(210, 210, 230);
        const COLORREF kSubtext = RGB(130, 132, 155);

        auto sectionLabel = [&](const std::string& t) -> WidgetPtr {
            return Text(t)->setFontSize(8)->setFontWeight(FontWeight::Bold)->setTextColor(kDim);
        };

        auto iconBtn = [&](const std::string& icon, const std::string& tip,
                           COLORREF fg, std::function<void()> cb) -> WidgetPtr {
            return Tooltip(
                GestureDetector(
                    Container(Text(icon)->setFontSize(13)->setTextColor(fg))
                        ->setWidth(32)->setHeight(32)->setBorderRadius(6)
                        ->setBackgroundColor(kCard)->setBorderWidth(1)
                        ->setBorderColor(kBorder)
                        ->setHoverBackgroundColor(RGB(40, 38, 60)))
                    ->setOnTap(std::move(cb)),
                tip);
        };

        auto redraw = [this]() { if (canvasPtr_) canvasPtr_->redraw(); };

        // =====================================================================
        // TOOLBAR
        // =====================================================================

        auto toolbar = Container(
            Row(
                iconBtn("↩", "Undo  Ctrl+Z", RGB(137, 180, 250), [this, redraw]() {
                    if (surface_) { surface_->undo(); redraw(); }
                }),
                SizedBox(4, 0),
                iconBtn("↪", "Redo  Ctrl+Y", RGB(166, 227, 161), [this, redraw]() {
                    if (surface_) { surface_->redo(); redraw(); }
                }),
                SizedBox(4, 0),
                iconBtn("⊘", "Clear active layer", RGB(243, 139, 168), [this, redraw]() {
                    if (surface_) { surface_->clear(); redraw(); }
                }),
                SizedBox(16, 0),
                iconBtn("💾", "Save PNG", RGB(200, 200, 220), [this]() {
                    if (!surface_) return;
                    wchar_t path[MAX_PATH] = {};
                    OPENFILENAMEW ofn = {};
                    ofn.lStructSize = sizeof(ofn);
                    ofn.lpstrFilter = L"PNG Image\0*.png\0";
                    ofn.lpstrFile   = path;
                    ofn.nMaxFile    = MAX_PATH;
                    ofn.Flags       = OFN_OVERWRITEPROMPT;
                    ofn.lpstrDefExt = L"png";
                    if (GetSaveFileNameW(&ofn)) surface_->savePNG(path);
                }),
                SizedBox(20, 0),
                Text("Ctrl+Z/Y  Undo/Redo     MMB/Space  Pan     Ctrl+Scroll  Zoom")
                    ->setFontSize(9)->setTextColor(RGB(55, 58, 80))
            )->setSpacing(0)->setCrossAxisAlignment(CrossAxisAlignment::Center))
            ->setHeight(kBarH)
            ->setBackgroundColor(kBg)
            ->setPaddingAll(8, 0, 8, 0);

        // =====================================================================
        // LEFT SIDEBAR
        // =====================================================================

        static const COLORREF kPalette[] = {
            RGB(0,0,0),       RGB(64,64,64),    RGB(128,128,128), RGB(255,255,255),
            RGB(220,50,50),   RGB(230,120,0),   RGB(220,200,0),   RGB(40,160,40),
            RGB(0,140,210),   RGB(30,40,200),   RGB(140,30,200),  RGB(210,40,130),
            RGB(150,90,40),   RGB(0,160,145),   RGB(245,160,180), RGB(80,200,160),
        };

        auto makeSwatch = [&](COLORREF cr) -> WidgetPtr {
            return GestureDetector(
                Container(nullptr)
                    ->setWidth(22)->setHeight(22)->setBorderRadius(4)
                    ->setBackgroundColor(cr)->setBorderWidth(1)->setBorderColor(kBorder))
                ->setOnTap([this, cr]() { brushColor.set(cr); });
        };

        auto paletteRow1 = Row()->setSpacing(0);
        auto paletteRow2 = Row()->setSpacing(0);
        for (int i = 0; i < 8;  i++) { paletteRow1->addChild(makeSwatch(kPalette[i]));     if (i < 7)  paletteRow1->addChild(SizedBox(3,0)); }
        for (int i = 8; i < 16; i++) { paletteRow2->addChild(makeSwatch(kPalette[i]));     if (i < 15) paletteRow2->addChild(SizedBox(3,0)); }

        auto picker = ColorPicker(RGB(0, 0, 0))
            ->setShowAlpha(false)
            ->bindValue(brushColor)
            ->setOnColorChanged([this](COLORREF cr) { brushColor.set(cr); });

        auto sizeRow = Row(
            GestureDetector(
                Container(Text("–")->setFontSize(14)->setTextColor(kText))
                    ->setWidth(26)->setHeight(26)->setBorderRadius(5)
                    ->setBackgroundColor(kCard)->setBorderWidth(1)->setBorderColor(kBorder))
                ->setOnTap([this]() { brushRadius.set(max(1.f, brushRadius.get() - 1.f)); }),
            SizedBox(6, 0),
            Container(
                Text(brushRadius, [](const float& r) { return std::to_string(int(r)) + "px"; })
                    ->setFontSize(10)->setTextColor(kText))
                ->setWidth(38)->setHeight(26)->setBorderRadius(5)
                ->setBackgroundColor(RGB(20, 20, 34))->setBorderWidth(1)->setBorderColor(kBorder),
            SizedBox(6, 0),
            GestureDetector(
                Container(Text("+")->setFontSize(14)->setTextColor(kText))
                    ->setWidth(26)->setHeight(26)->setBorderRadius(5)
                    ->setBackgroundColor(kCard)->setBorderWidth(1)->setBorderColor(kBorder))
                ->setOnTap([this]() { brushRadius.set(min(120.f, brushRadius.get() + 1.f)); })
        )->setSpacing(0)->setCrossAxisAlignment(CrossAxisAlignment::Center);

        State<double> opacityState(1.0, context);
        opacityState.listen([this](double v) {
            if (!surface_) return;
            StrokeStyle s = surface_->getStrokeStyle();
            s.opacity = float(v);
            surface_->setStrokeStyle(s);
        });

        auto colorPreview = Container(nullptr)
            ->setWidth(40)->setHeight(40)->setBorderRadius(8)
            ->setBackgroundColor(brushColor, [](const COLORREF& c) { return c; })
            ->setBorderWidth(2)->setBorderColor(kBorder);

        auto sidebar = Container(
            Column(
                Row(colorPreview, SizedBox(10, 0),
                    Column(
                        sectionLabel("ACTIVE COLOR"), SizedBox(0, 4),
                        Text(brushColor, [](const COLORREF& c) {
                            char buf[8];
                            _snprintf_s(buf, sizeof(buf), _TRUNCATE, "#%02X%02X%02X",
                                        GetRValue(c), GetGValue(c), GetBValue(c));
                            return std::string(buf);
                        })->setFontSize(10)->setTextColor(kSubtext)
                    )->setSpacing(0)
                )->setSpacing(0)->setCrossAxisAlignment(CrossAxisAlignment::Center),

                SizedBox(0, 12), SizedBox(0, 12),
                sectionLabel("COLOR PICKER"), SizedBox(0, 8), picker,

                SizedBox(0, 12), SizedBox(0, 12),
                sectionLabel("PALETTE"), SizedBox(0, 8),
                paletteRow1, SizedBox(0, 4), paletteRow2,

                SizedBox(0, 12), SizedBox(0, 12),
                sectionLabel("BRUSH SIZE"), SizedBox(0, 8), sizeRow,

                SizedBox(0, 12), SizedBox(0, 12),
                sectionLabel("OPACITY"), SizedBox(0, 8),
                Slider(0.0, 1.0, 0.01)
                    ->setValue(opacityState)
                    ->setTrackFillColor(kAccent)
                    ->setTrackColor(kBorder)
            )->setSpacing(0))
            ->setWidth(kSideW)
            ->setBackgroundColor(kPanel)
            ->setPaddingAll(12, 12, 12, 12);

        // =====================================================================
        // RIGHT PANEL  —  Layer stack
        // =====================================================================
        //
        // itemBuilder receives a ReactiveItemPtr<LayerDesc>.
        // It reads the current field values from ri->get() at build time.
        // When onLayersChanged calls ri->set(newDesc), only the row that
        // holds this specific ri is rebuilt — the rest are untouched.
        //
        // Index derivation: di is the display index (0 = topmost).
        // At tap time we re-derive the real stack index from the live
        // layerDescs snapshot to guard against stale captures.

        auto layerList =
            ListView(layerDescs)
                ->itemBuilder([this, kCard, kBorder, kAccent, kDim, kSubtext]
                              (int di, const LayerRef& ri) -> WidgetPtr {

                    // Read current field values directly from the item.
                    // This closure is re-run only when ri notifies a change.
                    const LayerDesc& ld  = ri->get();
                    const bool       act = ld.isActive;

                    const COLORREF rowBg   = act ? RGB(38, 30, 58) : kCard;
                    const COLORREF rowBord = act ? kAccent : kBorder;
                    const int      bw      = act ? 2 : 1;

                    // Helper: derive current real stack index at tap time
                    // from the live snapshot — never from a stale capture.
                    auto liveStackIndex = [this](int displayIdx) -> int {
                        const auto& snap = layerDescs.get();
                        if (displayIdx >= int(snap.size())) return -1;
                        return int(snap.size()) - 1 - displayIdx;
                    };

                    auto eye = Tooltip(
                        GestureDetector(
                            Container(
                                Text(ld.visible ? "👁" : "🚫")
                                    ->setFontSize(11)
                                    ->setTextColor(ld.visible
                                        ? RGB(180, 180, 210) : RGB(80, 80, 100)))
                                ->setWidth(24)->setHeight(24)->setBorderRadius(4)
                                ->setBackgroundColor(ld.visible
                                    ? RGB(30, 30, 48) : RGB(22, 22, 32))
                                ->setBorderWidth(1)->setBorderColor(kBorder))
                            ->setOnTap([this, di, liveStackIndex]() {
                                if (!surface_) return;
                                int li = liveStackIndex(di);
                                if (li < 0) return;
                                const auto& snap = layerDescs.get();
                                surface_->setLayerVisible(li, !snap[di]->get().visible);
                            }),
                        ld.visible ? "Hide layer" : "Show layer");

                    auto name =
                        GestureDetector(
                            Text(ld.name)
                                ->setFontSize(10)
                                ->setTextColor(act ? RGB(220, 200, 255) : kSubtext)
                                ->setMinWidth(56))
                            ->setOnTap([this, di, liveStackIndex]() {
                                if (!surface_) return;
                                int li = liveStackIndex(di);
                                if (li < 0) return;
                                surface_->setActiveLayer(li);
                            });

                    auto upBtn = Tooltip(
                        GestureDetector(
                            Container(Text("▲")->setFontSize(8)->setTextColor(kDim))
                                ->setWidth(20)->setHeight(20)->setBorderRadius(3)
                                ->setBackgroundColor(kCard)->setBorderWidth(1)->setBorderColor(kBorder))
                            ->setOnTap([this, di, liveStackIndex]() {
                                if (!surface_) return;
                                int li = liveStackIndex(di);
                                if (li < 0) return;
                                surface_->moveLayerUp(li);
                            }),
                        "Move up");

                    auto dnBtn = Tooltip(
                        GestureDetector(
                            Container(Text("▼")->setFontSize(8)->setTextColor(kDim))
                                ->setWidth(20)->setHeight(20)->setBorderRadius(3)
                                ->setBackgroundColor(kCard)->setBorderWidth(1)->setBorderColor(kBorder))
                            ->setOnTap([this, di, liveStackIndex]() {
                                if (!surface_) return;
                                int li = liveStackIndex(di);
                                if (li < 0) return;
                                surface_->moveLayerDown(li);
                            }),
                        "Move down");

                    auto delBtn = Tooltip(
                        GestureDetector(
                            Container(Text("✕")->setFontSize(9)->setTextColor(RGB(190, 70, 70)))
                                ->setWidth(20)->setHeight(20)->setBorderRadius(3)
                                ->setBackgroundColor(RGB(36, 20, 20))->setBorderWidth(1)->setBorderColor(kBorder))
                            ->setOnTap([this, di, liveStackIndex]() {
                                if (!surface_) return;
                                int li = liveStackIndex(di);
                                if (li < 0) return;
                                surface_->deleteLayer(li);
                            }),
                        "Delete layer");

                    return Container(
                        Row(eye, SizedBox(5, 0), name,
                            upBtn, SizedBox(2, 0), dnBtn, SizedBox(3, 0), delBtn)
                            ->setSpacing(0)
                            ->setCrossAxisAlignment(CrossAxisAlignment::Center))
                        ->setBackgroundColor(rowBg)
                        ->setBorderRadius(6)
                        ->setBorderWidth(bw)
                        ->setBorderColor(rowBord)
                        ->setPaddingAll(6, 5, 6, 5)
                        ->setHeight(34);
                })
                ->setSpacing(4)
                ->setScrollbarColor(RGB(60, 60, 90))
                ->setScrollbarHoverColor(RGB(90, 90, 130));

        auto layerPanel = Container(
            Column(
                Row(
                    Text("LAYERS")->setFontSize(8)->setFontWeight(FontWeight::Bold)->setTextColor(kDim),
                    Tooltip(
                        GestureDetector(
                            Container(Text("+")->setFontSize(16)->setTextColor(RGB(160, 220, 160)))
                                ->setWidth(28)->setHeight(28)->setBorderRadius(6)
                                ->setBackgroundColor(RGB(24, 40, 24))->setBorderWidth(1)
                                ->setBorderColor(RGB(60, 100, 60))
                                ->setHoverBackgroundColor(RGB(30, 52, 30)))
                            ->setOnTap([this]() { if (surface_) surface_->addLayer(); }),
                        "Add layer")
                )->setSpacing(0)->setCrossAxisAlignment(CrossAxisAlignment::Center),

                SizedBox(0, 10),
                Expanded(layerList),
                SizedBox(0, 8), SizedBox(0, 8),

                Tooltip(
                    GestureDetector(
                        Container(
                            Row(Text("⊞")->setFontSize(12)->setTextColor(RGB(170, 170, 200)),
                                SizedBox(5, 0),
                                Text("Flatten All")->setFontSize(10)->setTextColor(RGB(160, 160, 185)))
                                ->setSpacing(0)->setCrossAxisAlignment(CrossAxisAlignment::Center))
                            ->setBackgroundColor(kCard)->setBorderRadius(6)->setBorderWidth(1)
                            ->setBorderColor(kBorder)->setHoverBackgroundColor(RGB(36, 36, 56))
                            ->setPaddingAll(8, 6, 8, 6))
                        ->setOnTap([this]() { if (surface_) surface_->flattenToSingle(); }),
                    "Merge all visible layers into one")
            )->setSpacing(0))
            ->setWidth(kLayerW)
            ->setBackgroundColor(kPanel)
            ->setPaddingAll(12, 12, 12, 12);

        // =====================================================================
        // ROOT LAYOUT
        // =====================================================================

        return Scaffold(
            Row(sidebar, Column(toolbar, canvas)->setSpacing(0), layerPanel)
                ->setSpacing(0));
    }
};

// ============================================================================
// WinMain
// ============================================================================

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    FluxUI app(hInstance);
    app.build([&]() {
        return FluxApp("Paint", BuildComponent<PaintApp>(), AppTheme::dark());
    });
    RECT wa;
    SystemParametersInfo(SPI_GETWORKAREA, 0, &wa, 0);
    app.createWindow("Paint", wa.right - wa.left, wa.bottom - wa.top);
    return app.run();
}