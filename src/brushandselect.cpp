// test.cpp  —  Paint app: Brush + Eraser + Select + Text
//
// PaintSurface extends RasterSurface.  All text rasterization lives in
// RasterSurface (flux_canvas.hpp); this file owns only the tool state
// machine, cursor blink timer, and keyboard dispatch.
//
// Tool layout
// ───────────
//   Brush   (B)  — freehand paint strokes
//   Eraser  (E)  — white freehand erase
//   Select  (S)  — rectangular region lift / move
//   Text    (T)  — click to place, type, Enter to commit, Escape to cancel
//
// Viewport controls
// ─────────────────
//   MMB drag         — pan
//   Space + LMB drag — pan
//   Ctrl + Scroll    — zoom in / out
//   Ctrl + +/-/0     — zoom in / out / reset
//   Scrollbars       — pan

#include "flux.hpp"

// ============================================================================
// Tool enum
// ============================================================================

enum class Tool { Brush = kToolBrush, Eraser = kToolEraser, Select = 2, Text = 3 };

// ============================================================================
// TextSession  —  everything that lives only while the user is typing
// ============================================================================

struct TextSession {
    bool         active   = false;
    float        x        = 0, y = 0;   // canvas-space anchor (GL Y-up)
    std::wstring text;                   // accumulated typed characters
    TextStyle    style;                  // font / color — copied from sidebar state
};

// ============================================================================
// PaintSurface
// ============================================================================

class PaintSurface : public RasterSurface {
public:
    // ── Public state observed by PaintApp ────────────────────────────────────
    Tool activeTool = Tool::Brush;

    // Callbacks set by PaintApp
    std::function<void()>          onRedrawNeeded;
    std::function<void()>          onStateChanged;  // tool / style changed
    std::function<void(HCURSOR)>   onCursorChange;

    // ── Tool switching ────────────────────────────────────────────────────────
    void setActiveTool(Tool t) {
        if (t == activeTool) return;
        // Commit any in-progress work before switching
        commitFloatingSelect();
        commitTextSession();
        activeTool      = t;
        selDrawing_     = false;
        hasSelection_   = false;
        moving_         = false;
        grabbed_        = false;
        pixelBuf_.clear();
        textSession_.active = false;
        scratchClear();
        if (onRedrawNeeded) onRedrawNeeded();
    }

    // ── TextStyle setters (called from sidebar) ───────────────────────────────
    void setTextStyle(const TextStyle &s) { pendingTextStyle_ = s; }
    const TextStyle &getTextStyle() const { return pendingTextStyle_; }

    // ── Brush style helper ────────────────────────────────────────────────────
    void setBrushColor(float r, float g, float b) {
        StrokeStyle s = getStrokeStyle();
        s.r = r; s.g = g; s.b = b;
        setStrokeStyle(s);
    }
    void setBrushRadius(float radius) {
        StrokeStyle s = getStrokeStyle();
        s.radius = radius;
        setStrokeStyle(s);
    }

    // ── initialize ────────────────────────────────────────────────────────────
    void initialize(int w, int h) override {
        RasterSurface::initialize(w, h);   // starts GDI+, builds FBOs, shaders
        StrokeStyle s;
        s.r = 0; s.g = 0; s.b = 0; s.a = 1;
        s.radius = 6; s.opacity = 1; s.tool = kToolBrush;
        setStrokeStyle(s);
    }

    // ── needsContinuousRedraw ─────────────────────────────────────────────────
    // Returns true while a text session is active so CanvasWidget arms its
    // 500 ms blink timer.  The timer fires tickAndRender() → render() →
    // advanceTextBlink(), which toggles the cursor every 500 ms.
    // When the session ends this returns false and the timer is not re-armed.
    bool needsContinuousRedraw() const override {
        return textSession_.active;
    }

    // ── Mouse ─────────────────────────────────────────────────────────────────

    void onMouseDown(float x, float y) override {
        switch (activeTool) {

        case Tool::Text: {
            if (textSession_.active) {
                // Commit current text, start a new session at the new position
                commitTextSession();
            }
            textSession_.active = true;
            textSession_.text.clear();
            textSession_.x     = x;
            textSession_.y     = y;
            textSession_.style = pendingTextStyle_;
            cursorVisible_     = true;
            lastBlink_         = std::chrono::steady_clock::now();
            scratchClear();
            renderTextToScratch(textSession_.text, x, y,
                                textSession_.style, /*showCursor=*/true);
            if (onRedrawNeeded) onRedrawNeeded();
            return;
        }

        case Tool::Select: {
            if (hasSelection_ && insideSelection(x, y)) {
                moving_      = true;
                moveAnchorX_ = x;  moveAnchorY_ = y;
                moveCurX_    = x;  moveCurY_    = y;
                if (!grabbed_) {
                    pushUndoSnapshotPublic();
                    grabPixels();
                    eraseSource();
                    grabbed_ = true;
                }
            } else {
                commitFloatingSelect();
                hasSelection_ = false;
                selDrawing_   = true;
                selX0_ = selX1_ = x;
                selY0_ = selY1_ = y;
                scratchClear();
            }
            return;
        }

        default:
            // Brush / Eraser — wire the ToolId into StrokeStyle before delegating
            {
                StrokeStyle s = getStrokeStyle();
                s.tool = (activeTool == Tool::Eraser) ? kToolEraser : kToolBrush;
                setStrokeStyle(s);
                setTool(s.tool);
            }
            RasterSurface::onMouseDown(x, y);
            return;
        }
    }

    void onMouseMove(float x, float y) override {
        switch (activeTool) {

        case Tool::Text:
            if (onCursorChange)
                onCursorChange(LoadCursor(nullptr, IDC_IBEAM));
            return;

        case Tool::Select:
            if (onCursorChange) {
                bool inside = hasSelection_ && insideSelection(x, y);
                onCursorChange(LoadCursor(nullptr,
                                         inside ? IDC_SIZEALL : IDC_CROSS));
            }
            if (moving_) {
                moveCurX_ = x; moveCurY_ = y;
                float dx = x - moveAnchorX_, dy = y - moveAnchorY_;
                scratchClear();
                blitPixelsToScratch(selX0_ + dx, selY0_ + dy);
                drawSelectionDots(selX0_+dx, selY0_+dy, selX1_+dx, selY1_+dy);
                if (onRedrawNeeded) onRedrawNeeded();
            } else if (selDrawing_) {
                selX1_ = x; selY1_ = y;
                scratchClear();
                drawSelectionDots(selX0_, selY0_, selX1_, selY1_);
                if (onRedrawNeeded) onRedrawNeeded();
            }
            return;

        default:
            RasterSurface::onMouseMove(x, y);
            return;
        }
    }

    void onMouseUp(float x, float y) override {
        switch (activeTool) {

        case Tool::Text:
            return;  // text tool has no mouse-up action

        case Tool::Select:
            if (moving_) {
                moving_ = false;
                float dx = x - moveAnchorX_, dy = y - moveAnchorY_;
                selX0_ += dx; selX1_ += dx;
                selY0_ += dy; selY1_ += dy;
                scratchClear();
                blitPixelsToScratch(selX0_, selY0_);
                drawSelectionDots(selX0_, selY0_, selX1_, selY1_);
                if (onRedrawNeeded) onRedrawNeeded();
            } else if (selDrawing_) {
                selDrawing_ = false;
                selX1_ = x; selY1_ = y;
                hasSelection_ = fabsf(selX1_ - selX0_) > 2.f &&
                                fabsf(selY1_ - selY0_) > 2.f;
                // Normalise so selX0_/selY0_ are always bottom-left in GL Y-up
                if (selX0_ > selX1_) std::swap(selX0_, selX1_);
                if (selY0_ > selY1_) std::swap(selY0_, selY1_);
                scratchClear();
                if (hasSelection_)
                    drawSelectionDots(selX0_, selY0_, selX1_, selY1_);
                if (onRedrawNeeded) onRedrawNeeded();
            }
            return;

        default:
            RasterSurface::onMouseUp(x, y);
            if (onStateChanged) onStateChanged();
            return;
        }
    }

    void onRightMouseDown(float x, float y) override {
        switch (activeTool) {
        case Tool::Text:
            commitTextSession();
            if (onRedrawNeeded) onRedrawNeeded();
            return;
        case Tool::Select:
            commitFloatingSelect();
            hasSelection_ = false;
            selDrawing_   = false;
            scratchClear();
            if (onRedrawNeeded) onRedrawNeeded();
            return;
        default:
            RasterSurface::onRightMouseDown(x, y);
            return;
        }
    }

    // ── Keyboard ──────────────────────────────────────────────────────────────

    void onKeyDown(int key) override {
        // Text tool intercepts ALL keys while a session is active
        if (activeTool == Tool::Text && textSession_.active) {
            handleTextKey(key);
            return;
        }

        // Global undo/redo handled by base class
        RasterSurface::onKeyDown(key);

        // Tool shortcuts
        switch (key) {
        case 'B': setActiveTool(Tool::Brush);  return;
        case 'E': setActiveTool(Tool::Eraser); return;
        case 'S': setActiveTool(Tool::Select); return;
        case 'T': setActiveTool(Tool::Text);   return;
        case VK_ESCAPE:
            commitFloatingSelect();
            hasSelection_ = false;
            selDrawing_   = false;
            scratchClear();
            if (onRedrawNeeded) onRedrawNeeded();
            return;
        }
    }

    void onKeyUp(int) override {}

    // ── Render override — drives cursor blink and keeps overlays alive ────────
    //
    // The base render() blits committed + scratch every frame.
    // We hook in after to:
    //   1. Redraw selection dots (they live in scratch and get wiped by brush
    //      endStroke(), so we restore them each frame).
    //   2. Advance the text cursor blink — every 500 ms toggle cursor
    //      visibility, scratchClear, and re-render the text preview.
    void render(const float mvp[16]) override {
        RasterSurface::render(mvp);
        redrawSelectionOverlay();
        advanceTextBlink();
    }

private:
    // ══════════════════════════════════════════════════════════════════════════
    // TEXT SESSION
    // ══════════════════════════════════════════════════════════════════════════

    TextSession   textSession_;
    TextStyle     pendingTextStyle_;   // set from sidebar, copied into session on click

    bool          cursorVisible_ = true;
    std::chrono::steady_clock::time_point lastBlink_ =
        std::chrono::steady_clock::now();

    // Called every frame from render() — toggles cursor every 500 ms.
    // CanvasWidget's 500 ms timer ensures render() is called at the right
    // cadence; advanceTextBlink() just checks elapsed time defensively.
    void advanceTextBlink() {
        if (!textSession_.active) return;
        auto now = std::chrono::steady_clock::now();
        double elapsed =
            std::chrono::duration<double>(now - lastBlink_).count();
        if (elapsed >= 0.5) {
            cursorVisible_ = !cursorVisible_;
            lastBlink_     = now;
            scratchClear();
            renderTextToScratch(textSession_.text,
                                textSession_.x, textSession_.y,
                                textSession_.style,
                                /*showCursor=*/cursorVisible_);
            if (onRedrawNeeded) onRedrawNeeded();
        }
    }

    // Route a VK code to text editing actions while a session is open
    void handleTextKey(int vk) {
        const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;

        if (vk == VK_RETURN) {
            commitTextSession();
            if (onRedrawNeeded) onRedrawNeeded();
            return;
        }
        if (vk == VK_ESCAPE) {
            // Discard — clear scratch and close session without writing to canvas
            textSession_.active = false;
            textSession_.text.clear();
            scratchClear();
            if (onRedrawNeeded) onRedrawNeeded();
            return;
        }
        if (vk == VK_BACK) {
            if (!textSession_.text.empty())
                textSession_.text.pop_back();
            refreshTextPreview();
            return;
        }
        if (ctrl && vk == 'A') {
            textSession_.text.clear();
            refreshTextPreview();
            return;
        }

        // Translate VK → Unicode character via the current keyboard layout
        BYTE ks[256] = {};
        GetKeyboardState(ks);
        wchar_t buf[4] = {};
        int n = ToUnicode(vk, MapVirtualKey(vk, MAPVK_VK_TO_VSC),
                          ks, buf, 4, 0);
        if (n == 1 && buf[0] >= 0x20) {   // printable character
            textSession_.text += buf[0];
            refreshTextPreview();
        }
    }

    // Redraw text preview after any edit (resets blink so cursor is visible)
    void refreshTextPreview() {
        cursorVisible_ = true;
        lastBlink_     = std::chrono::steady_clock::now();
        scratchClear();
        renderTextToScratch(textSession_.text,
                            textSession_.x, textSession_.y,
                            textSession_.style,
                            /*showCursor=*/true);
        if (onRedrawNeeded) onRedrawNeeded();
    }

    // Write text permanently to canvas and close the session
    void commitTextSession() {
        if (!textSession_.active) return;
        if (!textSession_.text.empty()) {
            // commitTextToCanvas pushes undo snapshot internally
            commitTextToCanvas(textSession_.text,
                               textSession_.x, textSession_.y,
                               textSession_.style);
        }
        textSession_.active = false;
        textSession_.text.clear();
        scratchClear();
        if (onStateChanged) onStateChanged();
    }

    // ══════════════════════════════════════════════════════════════════════════
    // SELECTION TOOL
    // ══════════════════════════════════════════════════════════════════════════

    bool  selDrawing_   = false;
    bool  hasSelection_ = false;
    float selX0_ = 0, selY0_ = 0, selX1_ = 0, selY1_ = 0;

    bool  moving_        = false;
    bool  grabbed_       = false;
    float moveAnchorX_   = 0, moveAnchorY_ = 0;
    float moveCurX_      = 0, moveCurY_    = 0;

    std::vector<uint8_t> pixelBuf_;
    int grabW_ = 0, grabH_ = 0, grabLX_ = 0, grabBY_ = 0;

    bool insideSelection(float x, float y) const {
        return x >= min(selX0_, selX1_) && x <= max(selX0_, selX1_) &&
               y >= min(selY0_, selY1_) && y <= max(selY0_, selY1_);
    }

    // Commit any floating (moved but not stamped) selection to canvas
    void commitFloatingSelect() {
        if (pixelBuf_.empty()) return;
        if (moving_) {
            float dx = moveCurX_ - moveAnchorX_;
            float dy = moveCurY_ - moveAnchorY_;
            selX0_ += dx; selX1_ += dx;
            selY0_ += dy; selY1_ += dy;
            moving_ = false;
        }
        commitPixels(selX0_, selY0_);
        grabbed_ = false;
        scratchClear();
    }

    // Read the selected rectangle from the committed FBO into pixelBuf_.
    // Background-white pixels get alpha=0 so they composite transparently.
    void grabPixels() {
        int cw = canvasWidth(), ch = canvasHeight();
        grabLX_ = max(0, min((int)min(selX0_, selX1_), cw - 1));
        grabBY_ = max(0, min((int)min(selY0_, selY1_), ch - 1));
        grabW_  = max(1, min((int)fabsf(selX1_ - selX0_), cw - grabLX_));
        grabH_  = max(1, min((int)fabsf(selY1_ - selY0_), ch - grabBY_));

        std::vector<uint8_t> full(size_t(cw) * ch * 4);
        readCommitted(full.data());
        pixelBuf_.resize(size_t(grabW_) * grabH_ * 4);
        for (int r = 0; r < grabH_; r++) {
            for (int c = 0; c < grabW_; c++) {
                const uint8_t *src =
                    full.data() + ((grabBY_ + r) * cw + grabLX_ + c) * 4;
                uint8_t *dst = pixelBuf_.data() + (r * grabW_ + c) * 4;
                dst[0] = src[0]; dst[1] = src[1]; dst[2] = src[2];
                bool isBg = (src[0] >= 250 && src[1] >= 250 && src[2] >= 250);
                dst[3] = isBg ? 0 : src[3];
            }
        }
    }

    // Fill the source rectangle in committed with white (leaves a "hole")
    void eraseSource() {
        int cw = canvasWidth(), ch = canvasHeight();
        std::vector<uint8_t> full(size_t(cw) * ch * 4);
        readCommitted(full.data());
        for (int r = 0; r < grabH_; r++) {
            uint8_t *p = full.data() + ((grabBY_ + r) * cw + grabLX_) * 4;
            for (int c = 0; c < grabW_; c++) {
                p[c*4+0] = 255; p[c*4+1] = 255;
                p[c*4+2] = 255; p[c*4+3] = 255;
            }
        }
        uploadToCommitted(full.data());
    }

    // Upload pixelBuf_ into the scratch texture at canvas position (x0, y0)
    void blitPixelsToScratch(float x0, float y0) {
        if (pixelBuf_.empty()) return;
        int cw = canvasWidth(), ch = canvasHeight();
        int dstLX = (int)x0, dstBY = (int)y0;
        int srcOffX = 0, srcOffY = 0;
        if (dstLX < 0) { srcOffX = -dstLX; dstLX = 0; }
        if (dstBY < 0) { srcOffY = -dstBY; dstBY = 0; }
        int dstW = min(grabW_ - srcOffX, cw - dstLX);
        int dstH = min(grabH_ - srcOffY, ch - dstBY);
        if (dstW <= 0 || dstH <= 0) return;

        std::vector<uint8_t> sub(size_t(dstW) * dstH * 4);
        for (int r = 0; r < dstH; r++)
            memcpy(sub.data() + r * dstW * 4,
                   pixelBuf_.data() + ((srcOffY + r) * grabW_ + srcOffX) * 4,
                   size_t(dstW) * 4);

        glBindTexture(GL_TEXTURE_2D, scratchTexHandle());
        glTexSubImage2D(GL_TEXTURE_2D, 0, dstLX, dstBY, dstW, dstH,
                        GL_RGBA, GL_UNSIGNED_BYTE, sub.data());
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    // SRC_OVER composite pixelBuf_ onto committed at (x0, y0)
    void commitPixels(float x0, float y0) {
        if (pixelBuf_.empty()) return;
        int cw = canvasWidth(), ch = canvasHeight();
        int dstLX = (int)x0, dstBY = (int)y0;
        int srcOffX = 0, srcOffY = 0;
        if (dstLX < 0) { srcOffX = -dstLX; dstLX = 0; }
        if (dstBY < 0) { srcOffY = -dstBY; dstBY = 0; }
        int dstW = min(grabW_ - srcOffX, cw - dstLX);
        int dstH = min(grabH_ - srcOffY, ch - dstBY);
        if (dstW <= 0 || dstH <= 0) { pixelBuf_.clear(); return; }

        std::vector<uint8_t> full(size_t(cw) * ch * 4);
        readCommitted(full.data());
        for (int r = 0; r < dstH; r++) {
            for (int c = 0; c < dstW; c++) {
                const uint8_t *s =
                    pixelBuf_.data() +
                    ((srcOffY + r) * grabW_ + srcOffX + c) * 4;
                uint8_t *d =
                    full.data() + ((dstBY + r) * cw + dstLX + c) * 4;
                float sa = s[3] / 255.f;
                if (sa <= 0.f) continue;
                if (sa >= 1.f) {
                    d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = s[3];
                } else {
                    float da = d[3] / 255.f;
                    float oa = sa + da * (1.f - sa);
                    if (oa > 0.f) {
                        d[0] = (uint8_t)((s[0]*sa + d[0]*da*(1.f-sa)) / oa);
                        d[1] = (uint8_t)((s[1]*sa + d[1]*da*(1.f-sa)) / oa);
                        d[2] = (uint8_t)((s[2]*sa + d[2]*da*(1.f-sa)) / oa);
                        d[3] = (uint8_t)(oa * 255.f);
                    }
                }
            }
        }
        uploadToCommitted(full.data());
        pixelBuf_.clear();
    }

    // Dotted rectangle outline drawn into scratch FBO
    void drawSelectionDots(float x0, float y0, float x1, float y1) {
        float lx = min(x0, x1), rx = max(x0, x1);
        float by = min(y0, y1), ty = max(y0, y1);
        const float dotSz = 1.5f, gap = 6.f;
        std::vector<float> buf;
        auto edge = [&](float ax, float ay, float bx, float by2) {
            float dx = bx-ax, dy = by2-ay;
            float len = sqrtf(dx*dx + dy*dy);
            if (len < 1.f) return;
            float ux = dx/len, uy = dy/len;
            for (float d = 0.f; d < len; d += gap) {
                float cx = ax + ux*d, cy = ay + uy*d;
                buf.insert(buf.end(), {
                    cx-dotSz, cy-dotSz,  cx+dotSz, cy-dotSz,
                    cx+dotSz, cy+dotSz,  cx+dotSz, cy+dotSz,
                    cx-dotSz, cy+dotSz,  cx-dotSz, cy-dotSz
                });
            }
        };
        edge(lx, by, rx, by);  edge(rx, by, rx, ty);
        edge(rx, ty, lx, ty);  edge(lx, ty, lx, by);
        if (!buf.empty())
            drawVertsToFBO(scratchFBOHandle(), buf.data(),
                           (int)(buf.size() / 2),
                           GL_TRIANGLES, 0.f, 0.f, 0.f, 1.f);
    }

    // Called every frame — redraws selection dots so brush strokes don't
    // wipe them permanently (endStroke merges scratch then clears it)
    void redrawSelectionOverlay() {
        if (!hasSelection_ && !selDrawing_) return;
        float x0, y0, x1, y1;
        if (moving_) {
            float dx = moveCurX_ - moveAnchorX_;
            float dy = moveCurY_ - moveAnchorY_;
            x0 = selX0_+dx; y0 = selY0_+dy;
            x1 = selX1_+dx; y1 = selY1_+dy;
        } else {
            x0 = selX0_; y0 = selY0_; x1 = selX1_; y1 = selY1_;
        }
        if (grabbed_ && !moving_)
            blitPixelsToScratch(x0, y0);
        drawSelectionDots(x0, y0, x1, y1);
    }
};

// ============================================================================
// ColorSwatch  —  a clickable color square used in the palette row
// ============================================================================

static WidgetPtr makeColorSwatch(float r, float g, float b,
                                 std::function<void()> onTap) {
    COLORREF cr = RGB((int)(r*255), (int)(g*255), (int)(b*255));
    return GestureDetector(
        Container(nullptr)
            ->setWidth(20)->setHeight(20)
            ->setBorderRadius(3)
            ->setBackgroundColor(cr)
            ->setBorderWidth(1)
            ->setBorderColor(RGB(60, 62, 80))
    )->setOnTap(std::move(onTap));
}

// ============================================================================
// PaintApp
// ============================================================================

class PaintApp : public Component {
    State<Tool>  activeTool;
    State<int>   fontSize;
    State<float> brushRadius;
    State<float> colorR, colorG, colorB;

    std::shared_ptr<PaintSurface> surface_;
    CanvasWidget                 *canvasPtr_ = nullptr;

public:
    PaintApp()
        : activeTool (Tool::Brush, context)
        , fontSize   (20,          context)
        , brushRadius(6.f,         context)
        , colorR     (0.f,         context)
        , colorG     (0.f,         context)
        , colorB     (0.f,         context)
    {}

    WidgetPtr build() override {
        const int sw = GetSystemMetrics(SM_CXSCREEN);
        const int sh = GetSystemMetrics(SM_CYSCREEN);

        static constexpr int kSideW   = 176;
        static constexpr int kBarH    = 44;
        static constexpr int kCanvasW = 1240;
        static constexpr int kCanvasH = 1754;

        auto canvas  = RasterCanvas(sw - kSideW, sh - kBarH, kCanvasW, kCanvasH);
        surface_     = canvas->setSurface<PaintSurface>();
        canvasPtr_   = canvas.get();

        surface_->onRedrawNeeded = [this]() {
            if (canvasPtr_) canvasPtr_->redraw();
        };
        surface_->onStateChanged = [this]() {
            activeTool.set(surface_->activeTool);
        };
        surface_->onCursorChange = [](HCURSOR c) { SetCursor(c); };

        activeTool.listen([this](Tool t) {
            if (surface_) surface_->setActiveTool(t);
        });
        fontSize.listen([this](int sz) {
            if (!surface_) return;
            TextStyle s = surface_->getTextStyle();
            s.fontSize = sz;
            surface_->setTextStyle(s);
        });
        brushRadius.listen([this](float r) {
            if (surface_) surface_->setBrushRadius(r);
        });
        auto syncColor = [this]() {
            if (!surface_) return;
            surface_->setBrushColor(colorR.get(), colorG.get(), colorB.get());
            TextStyle ts = surface_->getTextStyle();
            ts.r = colorR.get(); ts.g = colorG.get(); ts.b = colorB.get();
            surface_->setTextStyle(ts);
        };
        colorR.listen([syncColor](float) { syncColor(); });
        colorG.listen([syncColor](float) { syncColor(); });
        colorB.listen([syncColor](float) { syncColor(); });

        const COLORREF kBg     = RGB(20,  20,  30 );
        const COLORREF kCard   = RGB(28,  28,  42 );
        const COLORREF kBorder = RGB(50,  52,  70 );
        const COLORREF kDim    = RGB(110, 115, 140);

        auto sectionLabel = [&](const std::string &label) -> WidgetPtr {
            return Text(label)
                ->setFontSize(8)
                ->setTextColor(kDim)
                ->setFontWeight(FontWeight::Bold);
        };

        auto card = [&](WidgetPtr child) -> WidgetPtr {
            return Container(child)
                ->setBackgroundColor(kCard)
                ->setBorderRadius(8)
                ->setBorderWidth(1)
                ->setBorderColor(kBorder)
                ->setPaddingAll(10, 10, 10, 10);
        };

        auto toolBtn = [&](Tool tv,
                           const std::string &icon,
                           const std::string &lbl) -> WidgetPtr {
            return GestureDetector(
                Container(
                    Column(
                        Text(icon)
                            ->setFontSize(16)
                            ->setTextColor(activeTool, [tv](const Tool &at) {
                                return at == tv ? RGB(200,160,255)
                                               : RGB(120,120,145);
                            }),
                        SizedBox(0, 2),
                        Text(lbl)
                            ->setFontSize(8)
                            ->setTextColor(activeTool, [tv](const Tool &at) {
                                return at == tv ? RGB(210,190,255)
                                               : RGB(85, 85, 105);
                            })
                    )->setSpacing(0)
                     ->setCrossAxisAlignment(CrossAxisAlignment::Center)
                )->setWidth(56)->setHeight(46)
                 ->setBorderRadius(7)
                 ->setBackgroundColor(activeTool, [tv](const Tool &at) {
                     return at == tv ? RGB(45,35,65) : RGB(28,28,42);
                 })
                 ->setBorderWidth(activeTool, [tv](const Tool &at) {
                     return at == tv ? 2 : 1;
                 })
                 ->setBorderColor(activeTool, [tv](const Tool &at) {
                     return at == tv ? RGB(150,110,220) : RGB(50,52,70);
                 })
            )->setOnTap([this, tv]() { activeTool.set(tv); });
        };

        auto toolsCard = card(
            Column(
                sectionLabel("TOOLS"),
                SizedBox(0, 8),
                Row(
                    toolBtn(Tool::Brush,  "🖌",  "Brush"),
                    SizedBox(6, 0),
                    toolBtn(Tool::Eraser, "◻",  "Eraser")
                )->setSpacing(0),
                SizedBox(0, 6),
                Row(
                    toolBtn(Tool::Select, "⬚",  "Select"),
                    SizedBox(6, 0),
                    toolBtn(Tool::Text,   "𝐓",  "Text")
                )->setSpacing(0)
            )->setSpacing(0)
        );

        auto brushCard = card(
            Column(
                sectionLabel("BRUSH SIZE"),
                SizedBox(0, 6),
                Row(
                    GestureDetector(
                        Container(Text("–")->setFontSize(14)
                                           ->setTextColor(RGB(180,180,200)))
                            ->setWidth(24)->setHeight(24)->setBorderRadius(4)
                            ->setBackgroundColor(RGB(35,35,55))
                            ->setBorderWidth(1)->setBorderColor(kBorder)
                    )->setOnTap([this]() {
                        brushRadius.set(max(1.f, brushRadius.get() - 1.f));
                    }),
                    SizedBox(4, 0),
                    Container(
                        Text(brushRadius, [](const float &r) {
                            return std::to_string((int)r);
                        })->setFontSize(10)->setTextColor(RGB(200,200,220))
                    )->setWidth(28)->setHeight(24)->setBorderRadius(4)
                     ->setBackgroundColor(RGB(22,22,38))
                     ->setBorderWidth(1)->setBorderColor(kBorder),
                    SizedBox(4, 0),
                    GestureDetector(
                        Container(Text("+")->setFontSize(14)
                                           ->setTextColor(RGB(180,180,200)))
                            ->setWidth(24)->setHeight(24)->setBorderRadius(4)
                            ->setBackgroundColor(RGB(35,35,55))
                            ->setBorderWidth(1)->setBorderColor(kBorder)
                    )->setOnTap([this]() {
                        brushRadius.set(min(64.f, brushRadius.get() + 1.f));
                    })
                )->setSpacing(0)
            )->setSpacing(0)
        );

        auto fontCard = card(
            Column(
                sectionLabel("FONT SIZE"),
                SizedBox(0, 6),
                Row(
                    GestureDetector(
                        Container(Text("–")->setFontSize(14)
                                           ->setTextColor(RGB(180,180,200)))
                            ->setWidth(24)->setHeight(24)->setBorderRadius(4)
                            ->setBackgroundColor(RGB(35,35,55))
                            ->setBorderWidth(1)->setBorderColor(kBorder)
                    )->setOnTap([this]() {
                        fontSize.set(max(8, fontSize.get() - 2));
                    }),
                    SizedBox(4, 0),
                    Container(
                        Text(fontSize, [](const int &sz) {
                            return std::to_string(sz);
                        })->setFontSize(10)->setTextColor(RGB(200,200,220))
                    )->setWidth(28)->setHeight(24)->setBorderRadius(4)
                     ->setBackgroundColor(RGB(22,22,38))
                     ->setBorderWidth(1)->setBorderColor(kBorder),
                    SizedBox(4, 0),
                    GestureDetector(
                        Container(Text("+")->setFontSize(14)
                                           ->setTextColor(RGB(180,180,200)))
                            ->setWidth(24)->setHeight(24)->setBorderRadius(4)
                            ->setBackgroundColor(RGB(35,35,55))
                            ->setBorderWidth(1)->setBorderColor(kBorder)
                    )->setOnTap([this]() {
                        fontSize.set(min(96, fontSize.get() + 2));
                    })
                )->setSpacing(0)
            )->setSpacing(0)
        );

        struct Swatch { float r, g, b; };
        static const Swatch kPalette[] = {
            {0.00f, 0.00f, 0.00f}, {0.25f, 0.25f, 0.25f},
            {0.50f, 0.50f, 0.50f}, {0.75f, 0.75f, 0.75f},
            {0.80f, 0.10f, 0.10f}, {0.85f, 0.45f, 0.00f},
            {0.80f, 0.75f, 0.00f}, {0.10f, 0.60f, 0.10f},
            {0.00f, 0.55f, 0.80f}, {0.10f, 0.10f, 0.80f},
            {0.55f, 0.10f, 0.80f}, {0.85f, 0.15f, 0.55f},
            {0.60f, 0.35f, 0.15f}, {0.00f, 0.60f, 0.55f},
            {0.95f, 0.60f, 0.70f}, {1.00f, 1.00f, 1.00f},
        };

        auto paletteCard = card(
            Column(
                sectionLabel("COLOR"),
                SizedBox(0, 6),
                Row(
                    Column(
                        Row(makeColorSwatch(kPalette[0].r, kPalette[0].g, kPalette[0].b, [this,i=0](){ colorR.set(kPalette[i].r); colorG.set(kPalette[i].g); colorB.set(kPalette[i].b); }),
                            SizedBox(2,0),
                            makeColorSwatch(kPalette[1].r, kPalette[1].g, kPalette[1].b, [this,i=1](){ colorR.set(kPalette[i].r); colorG.set(kPalette[i].g); colorB.set(kPalette[i].b); }),
                            SizedBox(2,0),
                            makeColorSwatch(kPalette[2].r, kPalette[2].g, kPalette[2].b, [this,i=2](){ colorR.set(kPalette[i].r); colorG.set(kPalette[i].g); colorB.set(kPalette[i].b); }),
                            SizedBox(2,0),
                            makeColorSwatch(kPalette[3].r, kPalette[3].g, kPalette[3].b, [this,i=3](){ colorR.set(kPalette[i].r); colorG.set(kPalette[i].g); colorB.set(kPalette[i].b); }),
                            SizedBox(2,0),
                            makeColorSwatch(kPalette[4].r, kPalette[4].g, kPalette[4].b, [this,i=4](){ colorR.set(kPalette[i].r); colorG.set(kPalette[i].g); colorB.set(kPalette[i].b); }),
                            SizedBox(2,0),
                            makeColorSwatch(kPalette[5].r, kPalette[5].g, kPalette[5].b, [this,i=5](){ colorR.set(kPalette[i].r); colorG.set(kPalette[i].g); colorB.set(kPalette[i].b); }),
                            SizedBox(2,0),
                            makeColorSwatch(kPalette[6].r, kPalette[6].g, kPalette[6].b, [this,i=6](){ colorR.set(kPalette[i].r); colorG.set(kPalette[i].g); colorB.set(kPalette[i].b); }),
                            SizedBox(2,0),
                            makeColorSwatch(kPalette[7].r, kPalette[7].g, kPalette[7].b, [this,i=7](){ colorR.set(kPalette[i].r); colorG.set(kPalette[i].g); colorB.set(kPalette[i].b); })
                        )->setSpacing(0),
                        SizedBox(0, 2),
                        Row(makeColorSwatch(kPalette[8].r,  kPalette[8].g,  kPalette[8].b,  [this,i=8] (){ colorR.set(kPalette[i].r); colorG.set(kPalette[i].g); colorB.set(kPalette[i].b); }),
                            SizedBox(2,0),
                            makeColorSwatch(kPalette[9].r,  kPalette[9].g,  kPalette[9].b,  [this,i=9] (){ colorR.set(kPalette[i].r); colorG.set(kPalette[i].g); colorB.set(kPalette[i].b); }),
                            SizedBox(2,0),
                            makeColorSwatch(kPalette[10].r, kPalette[10].g, kPalette[10].b, [this,i=10](){ colorR.set(kPalette[i].r); colorG.set(kPalette[i].g); colorB.set(kPalette[i].b); }),
                            SizedBox(2,0),
                            makeColorSwatch(kPalette[11].r, kPalette[11].g, kPalette[11].b, [this,i=11](){ colorR.set(kPalette[i].r); colorG.set(kPalette[i].g); colorB.set(kPalette[i].b); }),
                            SizedBox(2,0),
                            makeColorSwatch(kPalette[12].r, kPalette[12].g, kPalette[12].b, [this,i=12](){ colorR.set(kPalette[i].r); colorG.set(kPalette[i].g); colorB.set(kPalette[i].b); }),
                            SizedBox(2,0),
                            makeColorSwatch(kPalette[13].r, kPalette[13].g, kPalette[13].b, [this,i=13](){ colorR.set(kPalette[i].r); colorG.set(kPalette[i].g); colorB.set(kPalette[i].b); }),
                            SizedBox(2,0),
                            makeColorSwatch(kPalette[14].r, kPalette[14].g, kPalette[14].b, [this,i=14](){ colorR.set(kPalette[i].r); colorG.set(kPalette[i].g); colorB.set(kPalette[i].b); }),
                            SizedBox(2,0),
                            makeColorSwatch(kPalette[15].r, kPalette[15].g, kPalette[15].b, [this,i=15](){ colorR.set(kPalette[i].r); colorG.set(kPalette[i].g); colorB.set(kPalette[i].b); })
                        )->setSpacing(0)
                    )->setSpacing(0)
                )->setSpacing(0)
            )->setSpacing(0)
        );

        auto sidebar = Container(
            Column(
                toolsCard,
                SizedBox(0, 8),
                brushCard,
                SizedBox(0, 8),
                fontCard,
                SizedBox(0, 8),
                paletteCard
            )->setSpacing(0)
        )->setWidth(kSideW)
         ->setBackgroundColor(kBg)
         ->setPaddingAll(10, 10, 10, 10);

        auto redraw = [this]() { if (canvasPtr_) canvasPtr_->redraw(); };
        auto toolbar = Container(
            Row(
                Button("↩ Undo", [this, redraw]() {
                    if (surface_) { surface_->undo(); redraw(); }
                })->setBackgroundColor(RGB(30,30,48))
                  ->setTextColor(RGB(137,180,250))
                  ->setBorderRadius(5)->setHeight(28)->setPadding(6),
                SizedBox(6, 0),
                Button("Redo ↪", [this, redraw]() {
                    if (surface_) { surface_->redo(); redraw(); }
                })->setBackgroundColor(RGB(30,30,48))
                  ->setTextColor(RGB(166,227,161))
                  ->setBorderRadius(5)->setHeight(28)->setPadding(6),
                SizedBox(6, 0),
                Button("Clear", [this, redraw]() {
                    if (surface_) { surface_->clear(); redraw(); }
                })->setBackgroundColor(RGB(30,30,48))
                  ->setTextColor(RGB(243,139,168))
                  ->setBorderRadius(5)->setHeight(28)->setPadding(6),
                SizedBox(20, 0),
                Text("B=Brush  E=Eraser  S=Select  T=Text  "
                     "Enter=Commit  Esc=Cancel  "
                     "Ctrl+Z/Y=Undo  MMB/Space=Pan  Ctrl+Scroll=Zoom")
                    ->setFontSize(10)
                    ->setTextColor(RGB(65, 70, 95))
            )->setSpacing(0)
             ->setCrossAxisAlignment(CrossAxisAlignment::Center)
        )->setBackgroundColor(kBg)
         ->setPaddingAll(10, 8, 10, 8)
         ->setHeight(kBarH);

        return Scaffold(
            Row(
                sidebar,
                Column(toolbar, canvas)->setSpacing(0)
            )->setSpacing(0)
        );
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