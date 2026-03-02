// test_paint_viewport.cpp  —  Brush + Select (scratch FBO, same as rect tool)
#include "flux.hpp"

enum class Tool { Brush = kToolBrush, Select = 1 };

// ── PaintSurface ─────────────────────────────────────────────────────────────

class PaintSurface : public RasterSurface {
public:
    Tool activeTool = Tool::Brush;

    std::function<void()>          onRedrawNeeded;
    std::function<void()>          onStateChanged;
    std::function<void(HCURSOR)>   onCursorChange;   // so app can set cursor

    void setActiveTool(Tool t) {
        if (t != activeTool) commitFloating();   // commit any in-progress move
        activeTool    = t;
        selDrawing_   = false;
        hasSelection_ = false;
        moving_       = false;
        pixelBuf_.clear();
        scratchClear();
        if (onRedrawNeeded) onRedrawNeeded();
    }

    void initialize(int w, int h) override {
        RasterSurface::initialize(w, h);
        StrokeStyle s; s.r=0; s.g=0; s.b=0; s.a=1;
        s.radius=4; s.opacity=1; s.tool=kToolBrush;
        setStrokeStyle(s);
    }

    // ── mouse ─────────────────────────────────────────────────────────────────

    void onMouseDown(float x, float y) override {
        if (activeTool == Tool::Select) {
            if (hasSelection_ && insideSelection(x, y)) {
                // start MOVE
                moving_      = true;
                moveAnchorX_ = x;  moveAnchorY_ = y;
                moveCurX_    = x;  moveCurY_    = y;
                pushUndoSnapshotPublic();
                grabPixels();
                eraseSource();
            } else {
                // commit any previous floating content first
                commitFloating();
                // start new SELECTION
                hasSelection_ = false;
                selDrawing_   = true;
                selX0_ = selX1_ = x;
                selY0_ = selY1_ = y;
                scratchClear();
            }
            return;
        }
        RasterSurface::onMouseDown(x, y);
    }

    void onMouseMove(float x, float y) override {
        if (activeTool == Tool::Select) {
            // update cursor
            if (onCursorChange) {
                bool inside = hasSelection_ && insideSelection(x, y);
                onCursorChange(LoadCursor(nullptr, inside ? IDC_SIZEALL : IDC_CROSS));
            }
            if (moving_) {
                moveCurX_ = x; moveCurY_ = y;
                float dx = x - moveAnchorX_, dy = y - moveAnchorY_;
                scratchClear();
                blitPixelsToScratch(selX0_+dx, selY0_+dy);
                drawDots(selX0_+dx, selY0_+dy, selX1_+dx, selY1_+dy);
                if (onRedrawNeeded) onRedrawNeeded();
            } else if (selDrawing_) {
                selX1_ = x; selY1_ = y;
                scratchClear();
                drawDots(selX0_, selY0_, selX1_, selY1_);
                if (onRedrawNeeded) onRedrawNeeded();
            }
            return;
        }
        RasterSurface::onMouseMove(x, y);
    }

    void onMouseUp(float x, float y) override {
        if (activeTool == Tool::Select) {
            if (moving_) {
                moving_ = false;
                float dx = x - moveAnchorX_, dy = y - moveAnchorY_;
                // commit pixels to committed FBO
                commitPixels(selX0_+dx, selY0_+dy);
                // move the selection rect
                selX0_ += dx; selX1_ += dx;
                selY0_ += dy; selY1_ += dy;
                scratchClear();
                drawDots(selX0_, selY0_, selX1_, selY1_);
                if (onRedrawNeeded) onRedrawNeeded();
            } else if (selDrawing_) {
                selDrawing_ = false;
                selX1_ = x; selY1_ = y;
                hasSelection_ = fabsf(selX1_-selX0_) > 2.f &&
                                fabsf(selY1_-selY0_) > 2.f;
                scratchClear();
                if (hasSelection_)
                    drawDots(selX0_, selY0_, selX1_, selY1_);
                if (onRedrawNeeded) onRedrawNeeded();
                if (onStateChanged) onStateChanged();
            }
            return;
        }
        RasterSurface::onMouseUp(x, y);
        if (onStateChanged) onStateChanged();
    }

    void onKeyDown(int key) override {
        RasterSurface::onKeyDown(key);
        if (key == 'B')       setActiveTool(Tool::Brush);
        if (key == 'S')       setActiveTool(Tool::Select);
        if (key == VK_ESCAPE) {
            commitFloating();
            hasSelection_ = false;
            selDrawing_   = false;
            scratchClear();
            if (onRedrawNeeded) onRedrawNeeded();
        }
    }

private:
    // ── selection state ───────────────────────────────────────────────────────
    bool  selDrawing_   = false;
    bool  hasSelection_ = false;
    float selX0_=0, selY0_=0, selX1_=0, selY1_=0;

    // ── move state ────────────────────────────────────────────────────────────
    bool  moving_        = false;
    float moveAnchorX_=0, moveAnchorY_=0;
    float moveCurX_=0,    moveCurY_=0;

    // ── pixel buffer ──────────────────────────────────────────────────────────
    std::vector<uint8_t> pixelBuf_;
    int grabW_=0, grabH_=0, grabLX_=0, grabBY_=0;

    // ── helpers ───────────────────────────────────────────────────────────────

    bool insideSelection(float x, float y) const {
        float lx=min(selX0_,selX1_), rx=max(selX0_,selX1_);
        float by=min(selY0_,selY1_), ty=max(selY0_,selY1_);
        return x>=lx && x<=rx && y>=by && y<=ty;
    }

    // commit any floating pixels at their current drag position then reset
    void commitFloating() {
        if (!moving_ || pixelBuf_.empty()) return;
        float dx = moveCurX_ - moveAnchorX_;
        float dy = moveCurY_ - moveAnchorY_;
        commitPixels(selX0_+dx, selY0_+dy);
        selX0_+=dx; selX1_+=dx;
        selY0_+=dy; selY1_+=dy;
        moving_ = false;
        scratchClear();
        if (hasSelection_)
            drawDots(selX0_, selY0_, selX1_, selY1_);
    }

    // read selected region from committed FBO into pixelBuf_
    void grabPixels() {
        int cw=canvasWidth(), ch=canvasHeight();
        grabLX_ = max(0, min((int)min(selX0_,selX1_), cw-1));
        grabBY_ = max(0, min((int)min(selY0_,selY1_), ch-1));
        grabW_  = max(1, min((int)fabsf(selX1_-selX0_), cw-grabLX_));
        grabH_  = max(1, min((int)fabsf(selY1_-selY0_), ch-grabBY_));

        std::vector<uint8_t> full(size_t(cw)*ch*4);
        readCommitted(full.data());
        pixelBuf_.resize(size_t(grabW_)*grabH_*4);
        for (int r=0; r<grabH_; r++)
            memcpy(pixelBuf_.data() + r*grabW_*4,
                   full.data() + ((grabBY_+r)*cw + grabLX_)*4,
                   size_t(grabW_)*4);
    }

    // fill source rect with white in committed FBO
    void eraseSource() {
        int cw=canvasWidth(), ch=canvasHeight();
        std::vector<uint8_t> full(size_t(cw)*ch*4);
        readCommitted(full.data());
        for (int r=0; r<grabH_; r++) {
            uint8_t *p = full.data() + ((grabBY_+r)*cw + grabLX_)*4;
            for (int c=0; c<grabW_; c++) {
                p[c*4+0]=255; p[c*4+1]=255;
                p[c*4+2]=255; p[c*4+3]=255;
            }
        }
        uploadToCommitted(full.data());
    }

    // upload pixelBuf_ to scratch texture at canvas position (dstLX, dstBY)
    // only copies the portion that fits on the canvas — fixes the edge clamp bug
    void blitPixelsToScratch(float x0, float y0) {
        if (pixelBuf_.empty()) return;
        int cw=canvasWidth(), ch=canvasHeight();

        int dstLX = (int)min(x0, x0 + (float)grabW_);
        int dstBY = (int)min(y0, y0 + (float)grabH_);

        // how many source rows/cols actually fit on canvas
        int srcOffX = 0, srcOffY = 0;
        if (dstLX < 0) { srcOffX = -dstLX; dstLX = 0; }
        if (dstBY < 0) { srcOffY = -dstBY; dstBY = 0; }
        int dstW = min(grabW_ - srcOffX, cw - dstLX);
        int dstH = min(grabH_ - srcOffY, ch - dstBY);
        if (dstW <= 0 || dstH <= 0) return;

        // build sub-region
        std::vector<uint8_t> sub(size_t(dstW)*dstH*4);
        for (int r=0; r<dstH; r++)
            memcpy(sub.data() + r*dstW*4,
                   pixelBuf_.data() + ((srcOffY+r)*grabW_ + srcOffX)*4,
                   size_t(dstW)*4);

        glBindTexture(GL_TEXTURE_2D, scratchTexHandle());
        glTexSubImage2D(GL_TEXTURE_2D, 0, dstLX, dstBY, dstW, dstH,
                        GL_RGBA, GL_UNSIGNED_BYTE, sub.data());
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    // write pixelBuf_ into committed FBO at (x0,y0), clipped correctly
    void commitPixels(float x0, float y0) {
        if (pixelBuf_.empty()) return;
        int cw=canvasWidth(), ch=canvasHeight();

        int dstLX = (int)x0, dstBY = (int)y0;
        int srcOffX=0, srcOffY=0;
        if (dstLX < 0) { srcOffX=-dstLX; dstLX=0; }
        if (dstBY < 0) { srcOffY=-dstBY; dstBY=0; }
        int dstW = min(grabW_-srcOffX, cw-dstLX);
        int dstH = min(grabH_-srcOffY, ch-dstBY);
        if (dstW<=0 || dstH<=0) { pixelBuf_.clear(); return; }

        std::vector<uint8_t> full(size_t(cw)*ch*4);
        readCommitted(full.data());
        for (int r=0; r<dstH; r++)
            memcpy(full.data() + ((dstBY+r)*cw + dstLX)*4,
                   pixelBuf_.data() + ((srcOffY+r)*grabW_ + srcOffX)*4,
                   size_t(dstW)*4);
        uploadToCommitted(full.data());
        pixelBuf_.clear();
    }

    // dotted rect outline into scratch FBO
    void drawDots(float x0, float y0, float x1, float y1) {
        float lx=min(x0,x1), rx=max(x0,x1);
        float by=min(y0,y1), ty=max(y0,y1);
        const float dotSize=1.5f, gap=6.f;
        std::vector<float> buf;
        auto dotEdge=[&](float ax,float ay,float bx,float by2){
            float dx=bx-ax, dy=by2-ay, len=sqrtf(dx*dx+dy*dy);
            if (len<1.f) return;
            float ux=dx/len, uy=dy/len;
            for (float d=0.f; d<len; d+=gap) {
                float cx=ax+ux*d, cy=ay+uy*d;
                buf.insert(buf.end(),{cx-dotSize,cy-dotSize, cx+dotSize,cy-dotSize,
                                      cx+dotSize,cy+dotSize, cx+dotSize,cy+dotSize,
                                      cx-dotSize,cy+dotSize, cx-dotSize,cy-dotSize});
            }
        };
        dotEdge(lx,by,rx,by); dotEdge(rx,by,rx,ty);
        dotEdge(rx,ty,lx,ty); dotEdge(lx,ty,lx,by);
        if (!buf.empty())
            drawVertsToFBO(scratchFBOHandle(), buf.data(), int(buf.size()/2),
                           GL_TRIANGLES, 0.f,0.f,0.f,1.f);
    }
};

// ── PaintApp ─────────────────────────────────────────────────────────────────

class PaintApp : public Component {
    State<Tool> activeTool;

    std::shared_ptr<PaintSurface> surface_;
    CanvasWidget *canvasPtr_ = nullptr;

public:
    PaintApp() : activeTool(Tool::Brush, context) {}

    WidgetPtr build() override {
        int sw = GetSystemMetrics(SM_CXSCREEN);
        int sh = GetSystemMetrics(SM_CYSCREEN);

        static constexpr int kSideW=160, kBarH=44, kCanvasW=1240, kCanvasH=1754;

        auto canvas = RasterCanvas(sw-kSideW, sh-kBarH, kCanvasW, kCanvasH);
        surface_    = canvas->setSurface<PaintSurface>();
        canvasPtr_  = canvas.get();

        surface_->onRedrawNeeded = [this]() { if (canvasPtr_) canvasPtr_->redraw(); };
        surface_->onStateChanged = [this]() { activeTool.set(surface_->activeTool); };
        surface_->onCursorChange = [](HCURSOR c) { SetCursor(c); };

        activeTool.listen([this](Tool t) { if (surface_) surface_->setActiveTool(t); });

        COLORREF kBg=RGB(20,20,30), kCard=RGB(28,28,42), kBorder=RGB(50,52,70);

        auto card = [&](WidgetPtr child) -> WidgetPtr {
            return Container(child)->setBackgroundColor(kCard)
                ->setBorderRadius(8)->setBorderWidth(1)->setBorderColor(kBorder)
                ->setPaddingAll(10,10,10,10);
        };
        auto toolBtn = [&](Tool tv, const std::string &icon,
                           const std::string &lbl) -> WidgetPtr {
            return GestureDetector(
                Container(
                    Column(
                        Text(icon)->setFontSize(18)->setTextColor(activeTool,
                            [tv](const Tool &at){ return at==tv?RGB(200,160,255):RGB(120,120,145); }),
                        SizedBox(0,3),
                        Text(lbl)->setFontSize(8)->setTextColor(activeTool,
                            [tv](const Tool &at){ return at==tv?RGB(210,190,255):RGB(85,85,105); })
                    )->setSpacing(0)->setCrossAxisAlignment(CrossAxisAlignment::Center)
                )->setWidth(60)->setHeight(50)->setBorderRadius(8)
                 ->setBackgroundColor(activeTool,[tv](const Tool&at){ return at==tv?RGB(45,35,65):RGB(28,28,42); })
                 ->setBorderWidth(activeTool,   [tv](const Tool&at){ return at==tv?2:1; })
                 ->setBorderColor(activeTool,   [tv](const Tool&at){ return at==tv?RGB(150,110,220):RGB(50,52,70); })
            )->setOnTap([this,tv](){ activeTool.set(tv); });
        };

        auto sidebar = Container(
            card(Column(
                Text("TOOLS")->setFontSize(9)->setTextColor(RGB(110,115,140))
                             ->setFontWeight(FontWeight::Bold),
                SizedBox(0,10),
                Row(toolBtn(Tool::Brush,"🖌","Brush"), SizedBox(8,0),
                    toolBtn(Tool::Select,"⬚","Select"))->setSpacing(0)
            )->setSpacing(0))
        )->setWidth(kSideW)->setBackgroundColor(kBg)->setPaddingAll(12,12,12,12);

        auto toolbar = Container(
            Row(
                Button("↩ Undo",[this](){ if(surface_){ surface_->undo(); if(canvasPtr_) canvasPtr_->redraw(); } })
                    ->setBackgroundColor(RGB(30,30,48))->setTextColor(RGB(137,180,250))
                    ->setBorderRadius(5)->setHeight(28)->setPadding(6),
                SizedBox(6,0),
                Button("Redo ↪",[this](){ if(surface_){ surface_->redo(); if(canvasPtr_) canvasPtr_->redraw(); } })
                    ->setBackgroundColor(RGB(30,30,48))->setTextColor(RGB(166,227,161))
                    ->setBorderRadius(5)->setHeight(28)->setPadding(6),
                SizedBox(6,0),
                Button("Clear",[this](){ if(surface_){ surface_->clear(); if(canvasPtr_) canvasPtr_->redraw(); } })
                    ->setBackgroundColor(RGB(30,30,48))->setTextColor(RGB(243,139,168))
                    ->setBorderRadius(5)->setHeight(28)->setPadding(6),
                SizedBox(20,0),
                Text("B=Brush  S=Select  ESC=Clear  Ctrl+Z/Y=Undo  MMB=Pan  Ctrl+Scroll=Zoom")
                    ->setFontSize(10)->setTextColor(RGB(65,70,95))
            )->setSpacing(0)->setCrossAxisAlignment(CrossAxisAlignment::Center)
        )->setBackgroundColor(kBg)->setPaddingAll(10,8,10,8)->setHeight(kBarH);

        return Scaffold(Row(sidebar, Column(toolbar,canvas)->setSpacing(0))->setSpacing(0));
    }
};

// ── WinMain ───────────────────────────────────────────────────────────────────

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    FluxUI app(hInstance);
    app.build([&]() {
        return FluxApp("Paint", BuildComponent<PaintApp>(), AppTheme::dark());
    });
    RECT wa; SystemParametersInfo(SPI_GETWORKAREA, 0, &wa, 0);
    app.createWindow("Paint", wa.right-wa.left, wa.bottom-wa.top);
    return app.run();
}