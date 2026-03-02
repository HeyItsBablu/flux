// test_paint_viewport.cpp  —  Brush + Select + Text (MS-Paint style)
#include "flux.hpp"
#include <string>
#include <vector>

enum class Tool { Brush = kToolBrush, Select = 1, Text = 2 };

// ============================================================================
// TextInputState — manages the in-progress text entry session
// ============================================================================
struct TextInputState {
    bool      active    = false;
    float     x         = 0, y = 0;   // canvas-space anchor (top-left of text box)
    std::wstring text;                 // current typed string (wide for proper char handling)
    int       fontSize  = 20;
    COLORREF  color     = RGB(0,0,0);
    // Derived metrics updated after each render
    int       lineH     = 0;
    int       cursorX   = 0;          // pixel offset of cursor from x
};

// ============================================================================
// PaintSurface
// ============================================================================
class PaintSurface : public RasterSurface {
public:
    Tool activeTool = Tool::Brush;

    std::function<void()>          onRedrawNeeded;
    std::function<void()>          onStateChanged;
    std::function<void(HCURSOR)>   onCursorChange;

    void setActiveTool(Tool t) {
        if (t == activeTool) return;
        commitFloatingSelect();
        commitText();
        activeTool    = t;
        selDrawing_   = false;
        hasSelection_ = false;
        moving_       = false;
        pixelBuf_.clear();
        grabbed_ = false;
        textState_.active = false;
        scratchClear();
        if (onRedrawNeeded) onRedrawNeeded();
    }

    // Font size / color setters called from the UI
    void setTextFontSize(int sz)    { textState_.fontSize = sz; }
    void setTextColor(COLORREF c)   { textState_.color = c; }
    int  getTextFontSize() const    { return textState_.fontSize; }

    void initialize(int w, int h) override {
        RasterSurface::initialize(w, h);
        StrokeStyle s; s.r=0; s.g=0; s.b=0; s.a=1;
        s.radius=4; s.opacity=1; s.tool=kToolBrush;
        setStrokeStyle(s);

        // GDI+ is already started by the canvas; just init our token
        Gdiplus::GdiplusStartupInput gsi;
        ULONG_PTR tok;
        Gdiplus::GdiplusStartup(&tok, &gsi, nullptr);
        gdiplusToken_ = tok;
    }

    // ── mouse ─────────────────────────────────────────────────────────────────

    void onMouseDown(float x, float y) override {
        if (activeTool == Tool::Text) {
            if (textState_.active) {
                // Clicking somewhere else commits current text and starts new
                commitText();
            }
            textState_.active = true;
            textState_.text.clear();
            textState_.x = x;
            textState_.y = y;
            scratchClear();
            renderTextToScratch(true);
            if (onRedrawNeeded) onRedrawNeeded();
            return;
        }
        if (activeTool == Tool::Select) {
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
        RasterSurface::onMouseDown(x, y);
    }

    void onMouseMove(float x, float y) override {
        if (activeTool == Tool::Text) {
            if (onCursorChange)
                onCursorChange(LoadCursor(nullptr, IDC_IBEAM));
            return;
        }
        if (activeTool == Tool::Select) {
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
        if (activeTool == Tool::Text) return;
        if (activeTool == Tool::Select) {
            if (moving_) {
                moving_ = false;
                float dx = x - moveAnchorX_, dy = y - moveAnchorY_;
                selX0_ += dx; selX1_ += dx;
                selY0_ += dy; selY1_ += dy;
                scratchClear();
                blitPixelsToScratch(selX0_, selY0_);
                drawDots(selX0_, selY0_, selX1_, selY1_);
                if (onRedrawNeeded) onRedrawNeeded();
            } else if (selDrawing_) {
                selDrawing_ = false;
                selX1_ = x; selY1_ = y;
                hasSelection_ = fabsf(selX1_-selX0_) > 2.f &&
                                fabsf(selY1_-selY0_) > 2.f;
                if (selX0_ > selX1_) std::swap(selX0_, selX1_);
                if (selY0_ > selY1_) std::swap(selY0_, selY1_);
                scratchClear();
                if (hasSelection_)
                    drawDots(selX0_, selY0_, selX1_, selY1_);
                if (onRedrawNeeded) onRedrawNeeded();
            }
            return;
        }
        RasterSurface::onMouseUp(x, y);
        if (onStateChanged) onStateChanged();
    }

    // ── keyboard ──────────────────────────────────────────────────────────────

    void onKeyDown(int key) override {
        if (activeTool == Tool::Text && textState_.active) {
            handleTextKey(key);
            return;
        }
        RasterSurface::onKeyDown(key);
        if (key == 'B')       setActiveTool(Tool::Brush);
        if (key == 'S')       setActiveTool(Tool::Select);
        if (key == 'T')       setActiveTool(Tool::Text);
        if (key == VK_ESCAPE) {
            commitFloatingSelect();
            hasSelection_ = false;
            selDrawing_   = false;
            scratchClear();
            if (onRedrawNeeded) onRedrawNeeded();
        }
    }

    void onKeyUp(int) override {}

    void onRightMouseDown(float x, float y) override {
        if (activeTool == Tool::Text) {
            commitText();
            if (onRedrawNeeded) onRedrawNeeded();
            return;
        }
        if (activeTool == Tool::Select) {
            commitFloatingSelect();
            hasSelection_ = false;
            selDrawing_   = false;
            scratchClear();
            if (onRedrawNeeded) onRedrawNeeded();
            return;
        }
        RasterSurface::onRightMouseDown(x, y);
    }

    void render(const float mvp[16]) override {
        RasterSurface::render(mvp);
        redrawSelectionOverlay();
        // Text preview is already in scratch; cursor blink handled here
        if (activeTool == Tool::Text && textState_.active) {
            // Blink cursor: re-render to scratch every ~500ms
            auto now = std::chrono::steady_clock::now();
            double elapsed = std::chrono::duration<double>(now - lastBlink_).count();
            if (elapsed > 0.5) {
                cursorVisible_ = !cursorVisible_;
                lastBlink_ = now;
                scratchClear();
                renderTextToScratch(cursorVisible_);
            }
        }
    }

    void destroy() override {
        RasterSurface::destroy();
        if (gdiplusToken_) {
            Gdiplus::GdiplusShutdown(gdiplusToken_);
            gdiplusToken_ = 0;
        }
    }

private:
    ULONG_PTR gdiplusToken_ = 0;

    // ── Text state ────────────────────────────────────────────────────────────
    TextInputState textState_;
    bool  cursorVisible_ = true;
    std::chrono::steady_clock::time_point lastBlink_ = std::chrono::steady_clock::now();

    // ── Selection state ───────────────────────────────────────────────────────
    bool  selDrawing_   = false;
    bool  hasSelection_ = false;
    float selX0_=0, selY0_=0, selX1_=0, selY1_=0;
    bool  moving_        = false;
    bool  grabbed_       = false;
    float moveAnchorX_=0, moveAnchorY_=0;
    float moveCurX_=0,    moveCurY_=0;
    std::vector<uint8_t> pixelBuf_;
    int grabW_=0, grabH_=0, grabLX_=0, grabBY_=0;

    // ============================================================
    // TEXT TOOL IMPLEMENTATION
    // ============================================================

    void handleTextKey(int vk) {
        bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        bool ctrl  = (GetKeyState(VK_CONTROL) & 0x8000) != 0;

        if (vk == VK_ESCAPE || vk == VK_RETURN) {
            // Enter = commit; Escape = discard
            if (vk == VK_RETURN && !textState_.text.empty())
                commitText();
            else {
                textState_.active = false;
                textState_.text.clear();
                scratchClear();
            }
            if (onRedrawNeeded) onRedrawNeeded();
            return;
        }
        if (vk == VK_BACK) {
            if (!textState_.text.empty())
                textState_.text.pop_back();
            scratchClear();
            cursorVisible_ = true;
            renderTextToScratch(true);
            if (onRedrawNeeded) onRedrawNeeded();
            return;
        }
        if (ctrl && vk == 'A') {
            textState_.text.clear();
            scratchClear();
            cursorVisible_ = true;
            renderTextToScratch(true);
            if (onRedrawNeeded) onRedrawNeeded();
            return;
        }

        // Convert VK to a printable wide char via ToUnicode
        BYTE keyState[256] = {};
        GetKeyboardState(keyState);
        wchar_t buf[4] = {};
        int n = ToUnicode(vk, MapVirtualKey(vk, MAPVK_VK_TO_VSC), keyState, buf, 4, 0);
        if (n == 1 && buf[0] >= 0x20) {
            textState_.text += buf[0];
            cursorVisible_ = true;
            lastBlink_ = std::chrono::steady_clock::now();
            scratchClear();
            renderTextToScratch(true);
            if (onRedrawNeeded) onRedrawNeeded();
        }
    }

    // Render the current text (and optional cursor) into the scratch FBO using
    // GDI offscreen rendering then uploading as a texture sub-image.
    //
    // Strategy: allocate an RGBA DIB the same size as the canvas, render text
    // with GDI in black on a white background, then composite onto the scratch
    // texture treating white as transparent.
    void renderTextToScratch(bool showCursor) {
        if (!textState_.active) return;

        int cw = canvasWidth(), ch = canvasHeight();
        int fontSize = textState_.fontSize;

        // ── Create a memory DC with a 32-bit DIB ──────────────────────────────
        HDC hdcMem = CreateCompatibleDC(nullptr);
        BITMAPINFO bmi = {};
        bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth       = cw;
        bmi.bmiHeader.biHeight      = -ch;   // top-down
        bmi.bmiHeader.biPlanes      = 1;
        bmi.bmiHeader.biBitCount    = 32;
        bmi.bmiHeader.biCompression = BI_RGB;
        void *bits = nullptr;
        HBITMAP hBmp = CreateDIBSection(hdcMem, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
        HGDIOBJ oldBmp = SelectObject(hdcMem, hBmp);

        // Fill with white (transparent background sentinel)
        RECT rc = {0, 0, cw, ch};
        HBRUSH whiteBrush = CreateSolidBrush(RGB(255,255,255));
        FillRect(hdcMem, &rc, whiteBrush);
        DeleteObject(whiteBrush);

        // ── Create font ────────────────────────────────────────────────────────
        // GL canvas is Y-up; textState_.y is Y-up canvas coord.
        // GDI DIB is top-down, so topY = ch - textState_.y - fontSize (approx).
        HFONT hFont = CreateFontW(
            -fontSize, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Arial");
        HGDIOBJ oldFont = SelectObject(hdcMem, hFont);

        SetTextColor(hdcMem, textState_.color);
        SetBkMode(hdcMem, TRANSPARENT);

        // Convert anchor: GL Y-up → GDI top-down
        int gdiY = ch - (int)textState_.y - fontSize;
        int gdiX = (int)textState_.x;

        // Draw text
        std::wstring display = textState_.text;
        if (showCursor) display += L'|';

        if (!display.empty())
            TextOutW(hdcMem, gdiX, gdiY, display.c_str(), (int)display.size());

        // ── Extract RGBA pixels and upload to scratch ──────────────────────────
        GdiFlush();

        // bits is BGRA (GDI DIB is BGR + pad byte).
        // We upload only the bounding box around the text to keep it fast.
        // Measure the text width to find the bounding box.
        SIZE sz = {};
        GetTextExtentPoint32W(hdcMem, display.c_str(), (int)display.size(), &sz);

        int rx0 = max(0, gdiX - 2);
        int ry0 = max(0, gdiY - 2);
        int rx1 = min(cw, gdiX + sz.cx + 4);
        int ry1 = min(ch, gdiY + sz.cy + 4);
        int rw  = rx1 - rx0, rh = ry1 - ry0;

        if (rw > 0 && rh > 0) {
            std::vector<uint8_t> rgba(size_t(rw)*rh*4);
            const uint8_t *src = reinterpret_cast<const uint8_t *>(bits);

            for (int row = 0; row < rh; row++) {
                for (int col = 0; col < rw; col++) {
                    const uint8_t *p = src + ((ry0 + row)*cw + (rx0 + col))*4;
                    uint8_t b = p[0], g = p[1], r = p[2];
                    // White → transparent (background); anything else → opaque text
                    bool isWhite = (r >= 250 && g >= 250 && b >= 250);
                    uint8_t *d = rgba.data() + (row*rw + col)*4;
                    // Store as RGBA for GL upload
                    d[0] = r; d[1] = g; d[2] = b;
                    d[3] = isWhite ? 0 : 255;
                }
            }

            // Upload into the scratch texture at the sub-rect position.
            // Scratch texture origin is bottom-left (GL), so flip Y:
            // GDI row ry0 (top) maps to GL row (ch - ry0 - rh).
            int glSrcY = ch - ry0 - rh;
            if (glSrcY >= 0 && glSrcY + rh <= ch) {
                // Flip rows for GL Y-up upload
                std::vector<uint8_t> flipped(rgba.size());
                for (int row = 0; row < rh; row++)
                    memcpy(flipped.data() + row*rw*4,
                           rgba.data() + (rh-1-row)*rw*4,
                           size_t(rw)*4);

                glBindTexture(GL_TEXTURE_2D, scratchTexHandle());
                glTexSubImage2D(GL_TEXTURE_2D, 0, rx0, glSrcY, rw, rh,
                                GL_RGBA, GL_UNSIGNED_BYTE, flipped.data());
                glBindTexture(GL_TEXTURE_2D, 0);
            }
        }

        // ── Cleanup ────────────────────────────────────────────────────────────
        SelectObject(hdcMem, oldFont);
        DeleteObject(hFont);
        SelectObject(hdcMem, oldBmp);
        DeleteObject(hBmp);
        DeleteDC(hdcMem);
    }

    // Commit text to committed FBO and reset text state
    void commitText() {
        if (!textState_.active || textState_.text.empty()) {
            textState_.active = false;
            textState_.text.clear();
            scratchClear();
            return;
        }

        pushUndoSnapshotPublic();

        int cw = canvasWidth(), ch = canvasHeight();
        int fontSize = textState_.fontSize;

        HDC hdcMem = CreateCompatibleDC(nullptr);
        BITMAPINFO bmi = {};
        bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth       = cw;
        bmi.bmiHeader.biHeight      = -ch;
        bmi.bmiHeader.biPlanes      = 1;
        bmi.bmiHeader.biBitCount    = 32;
        bmi.bmiHeader.biCompression = BI_RGB;
        void *bits = nullptr;
        HBITMAP hBmp = CreateDIBSection(hdcMem, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
        HGDIOBJ oldBmp = SelectObject(hdcMem, hBmp);

        RECT rc = {0, 0, cw, ch};
        HBRUSH whiteBrush = CreateSolidBrush(RGB(255,255,255));
        FillRect(hdcMem, &rc, whiteBrush);
        DeleteObject(whiteBrush);

        HFONT hFont = CreateFontW(
            -fontSize, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Arial");
        HGDIOBJ oldFont = SelectObject(hdcMem, hFont);

        SetTextColor(hdcMem, textState_.color);
        SetBkMode(hdcMem, TRANSPARENT);

        int gdiY = ch - (int)textState_.y - fontSize;
        int gdiX = (int)textState_.x;
        TextOutW(hdcMem, gdiX, gdiY, textState_.text.c_str(), (int)textState_.text.size());

        GdiFlush();

        // Read current committed pixels
        std::vector<uint8_t> committed(size_t(cw)*ch*4);
        readCommitted(committed.data());

        // Composite text over committed (white pixels = transparent)
        const uint8_t *src = reinterpret_cast<const uint8_t *>(bits);
        // DIB is top-down; committed (from glReadPixels) is bottom-up
        for (int gdiRow = 0; gdiRow < ch; gdiRow++) {
            int glRow = ch - 1 - gdiRow;  // flip for GL Y-up
            for (int col = 0; col < cw; col++) {
                const uint8_t *p = src + (gdiRow*cw + col)*4;
                uint8_t b = p[0], g = p[1], r = p[2];
                bool isWhite = (r >= 250 && g >= 250 && b >= 250);
                if (!isWhite) {
                    uint8_t *d = committed.data() + (glRow*cw + col)*4;
                    d[0] = r; d[1] = g; d[2] = b; d[3] = 255;
                }
            }
        }

        uploadToCommitted(committed.data());

        SelectObject(hdcMem, oldFont);
        DeleteObject(hFont);
        SelectObject(hdcMem, oldBmp);
        DeleteObject(hBmp);
        DeleteDC(hdcMem);

        textState_.active = false;
        textState_.text.clear();
        scratchClear();
        if (onStateChanged) onStateChanged();
    }

    // ============================================================
    // SELECTION TOOL (unchanged from original)
    // ============================================================

    void commitFloatingSelect() {
        if (pixelBuf_.empty()) return;
        if (moving_) {
            float dx = moveCurX_ - moveAnchorX_;
            float dy = moveCurY_ - moveAnchorY_;
            selX0_+=dx; selX1_+=dx;
            selY0_+=dy; selY1_+=dy;
            moving_ = false;
        }
        commitPixels(selX0_, selY0_);
        grabbed_ = false;
        scratchClear();
    }

    bool insideSelection(float x, float y) const {
        float lx=min(selX0_,selX1_), rx=max(selX0_,selX1_);
        float by=min(selY0_,selY1_), ty=max(selY0_,selY1_);
        return x>=lx && x<=rx && y>=by && y<=ty;
    }

    void grabPixels() {
        int cw=canvasWidth(), ch=canvasHeight();
        grabLX_ = max(0, min((int)min(selX0_,selX1_), cw-1));
        grabBY_ = max(0, min((int)min(selY0_,selY1_), ch-1));
        grabW_  = max(1, min((int)fabsf(selX1_-selX0_), cw-grabLX_));
        grabH_  = max(1, min((int)fabsf(selY1_-selY0_), ch-grabBY_));

        std::vector<uint8_t> full(size_t(cw)*ch*4);
        readCommitted(full.data());
        pixelBuf_.resize(size_t(grabW_)*grabH_*4);
        for (int r=0; r<grabH_; r++) {
            for (int c=0; c<grabW_; c++) {
                const uint8_t *src = full.data() + ((grabBY_+r)*cw + grabLX_+c)*4;
                uint8_t *dst = pixelBuf_.data() + (r*grabW_+c)*4;
                dst[0] = src[0]; dst[1] = src[1]; dst[2] = src[2];
                bool isBackground = (src[0] >= 250 && src[1] >= 250 && src[2] >= 250);
                dst[3] = isBackground ? 0 : src[3];
            }
        }
    }

    void eraseSource() {
        int cw=canvasWidth(), ch=canvasHeight();
        std::vector<uint8_t> full(size_t(cw)*ch*4);
        readCommitted(full.data());
        for (int r=0; r<grabH_; r++) {
            uint8_t *p = full.data() + ((grabBY_+r)*cw + grabLX_)*4;
            for (int c=0; c<grabW_; c++) {
                p[c*4+0]=255; p[c*4+1]=255; p[c*4+2]=255; p[c*4+3]=255;
            }
        }
        uploadToCommitted(full.data());
    }

    void blitPixelsToScratch(float x0, float y0) {
        if (pixelBuf_.empty()) return;
        int cw=canvasWidth(), ch=canvasHeight();
        int dstLX = (int)x0, dstBY = (int)y0;
        int srcOffX=0, srcOffY=0;
        if (dstLX < 0) { srcOffX=-dstLX; dstLX=0; }
        if (dstBY < 0) { srcOffY=-dstBY; dstBY=0; }
        int dstW = min(grabW_-srcOffX, cw-dstLX);
        int dstH = min(grabH_-srcOffY, ch-dstBY);
        if (dstW<=0 || dstH<=0) return;
        std::vector<uint8_t> sub(size_t(dstW)*dstH*4);
        for (int r=0; r<dstH; r++)
            memcpy(sub.data()+r*dstW*4,
                   pixelBuf_.data()+((srcOffY+r)*grabW_+srcOffX)*4,
                   size_t(dstW)*4);
        glBindTexture(GL_TEXTURE_2D, scratchTexHandle());
        glTexSubImage2D(GL_TEXTURE_2D, 0, dstLX, dstBY, dstW, dstH,
                        GL_RGBA, GL_UNSIGNED_BYTE, sub.data());
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    void commitPixels(float x0, float y0) {
        if (pixelBuf_.empty()) return;
        int cw=canvasWidth(), ch=canvasHeight();
        int dstLX=(int)x0, dstBY=(int)y0;
        int srcOffX=0, srcOffY=0;
        if (dstLX<0){srcOffX=-dstLX; dstLX=0;}
        if (dstBY<0){srcOffY=-dstBY; dstBY=0;}
        int dstW=min(grabW_-srcOffX, cw-dstLX);
        int dstH=min(grabH_-srcOffY, ch-dstBY);
        if (dstW<=0||dstH<=0){pixelBuf_.clear(); return;}
        std::vector<uint8_t> full(size_t(cw)*ch*4);
        readCommitted(full.data());
        for (int r=0; r<dstH; r++) {
            for (int c=0; c<dstW; c++) {
                const uint8_t *src=pixelBuf_.data()+((srcOffY+r)*grabW_+srcOffX+c)*4;
                uint8_t *dst=full.data()+((dstBY+r)*cw+dstLX+c)*4;
                float sa=src[3]/255.f;
                if (sa<=0.f) continue;
                if (sa>=1.f){dst[0]=src[0];dst[1]=src[1];dst[2]=src[2];dst[3]=src[3];}
                else {
                    float da=dst[3]/255.f, oa=sa+da*(1.f-sa);
                    if (oa>0.f){
                        dst[0]=(uint8_t)((src[0]*sa+dst[0]*da*(1.f-sa))/oa);
                        dst[1]=(uint8_t)((src[1]*sa+dst[1]*da*(1.f-sa))/oa);
                        dst[2]=(uint8_t)((src[2]*sa+dst[2]*da*(1.f-sa))/oa);
                        dst[3]=(uint8_t)(oa*255.f);
                    }
                }
            }
        }
        uploadToCommitted(full.data());
        pixelBuf_.clear();
    }

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

    void redrawSelectionOverlay() {
        if (!hasSelection_ && !selDrawing_) return;
        float x0,y0,x1,y1;
        if (moving_) {
            float dx=moveCurX_-moveAnchorX_, dy=moveCurY_-moveAnchorY_;
            x0=selX0_+dx; y0=selY0_+dy; x1=selX1_+dx; y1=selY1_+dy;
        } else { x0=selX0_; y0=selY0_; x1=selX1_; y1=selY1_; }
        if (grabbed_ && !moving_) blitPixelsToScratch(x0, y0);
        drawDots(x0, y0, x1, y1);
    }
};

// ============================================================================
// PaintApp
// ============================================================================
class PaintApp : public Component {
    State<Tool> activeTool;
    State<int>  fontSize;

    std::shared_ptr<PaintSurface> surface_;
    CanvasWidget *canvasPtr_ = nullptr;

public:
    PaintApp() : activeTool(Tool::Brush, context), fontSize(20, context) {}

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
        fontSize.listen([this](int sz) { if (surface_) surface_->setTextFontSize(sz); });

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

        // Font size buttons for text tool
        auto fontSizeControls = Container(
            Column(
                Text("SIZE")->setFontSize(8)->setTextColor(RGB(110,115,140))
                             ->setFontWeight(FontWeight::Bold),
                SizedBox(0,6),
                Row(
                    GestureDetector(
                        Container(Text("–")->setFontSize(14)->setTextColor(RGB(180,180,200)))
                            ->setWidth(26)->setHeight(26)->setBorderRadius(4)
                            ->setBackgroundColor(RGB(35,35,55))
                            ->setBorderWidth(1)->setBorderColor(RGB(50,52,70))
                    )->setOnTap([this](){ fontSize.set(max(8, fontSize.get()-2)); }),
                    SizedBox(4,0),
                    Container(
                        Text(fontSize, [](const int &sz){ return std::to_string(sz); })
                            ->setFontSize(10)->setTextColor(RGB(200,200,220))
                    )->setWidth(28)->setHeight(26)->setBorderRadius(4)
                     ->setBackgroundColor(RGB(22,22,38))
                     ->setBorderWidth(1)->setBorderColor(RGB(50,52,70)),
                    SizedBox(4,0),
                    GestureDetector(
                        Container(Text("+")->setFontSize(14)->setTextColor(RGB(180,180,200)))
                            ->setWidth(26)->setHeight(26)->setBorderRadius(4)
                            ->setBackgroundColor(RGB(35,35,55))
                            ->setBorderWidth(1)->setBorderColor(RGB(50,52,70))
                    )->setOnTap([this](){ fontSize.set(min(96, fontSize.get()+2)); })
                )->setSpacing(0)
            )->setSpacing(0)
        );

        auto sidebar = Container(
            card(Column(
                Text("TOOLS")->setFontSize(9)->setTextColor(RGB(110,115,140))
                             ->setFontWeight(FontWeight::Bold),
                SizedBox(0,10),
                Row(toolBtn(Tool::Brush,"🖌","Brush"), SizedBox(8,0),
                    toolBtn(Tool::Select,"⬚","Select"))->setSpacing(0),
                SizedBox(0,8),
                Row(toolBtn(Tool::Text,"𝐓","Text"))->setSpacing(0),
                SizedBox(0,12),
                // Show font size controls only when Text is active
                Container(fontSizeControls)
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
                Text("B=Brush  S=Select  T=Text  ESC=Cancel  Enter=Commit  Ctrl+Z/Y=Undo  MMB=Pan  Ctrl+Scroll=Zoom")
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