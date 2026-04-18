// test_canvas_image_surface.hpp
#pragma once
#include "flux/flux.hpp"
#include <cmath>
#include <string>

// ============================================================================
// ImageTestSurface
// Demonstrates all three drawImage() overloads:
//   1. drawImage(img, dx, dy)                   — natural size
//   2. drawImage(img, dx, dy, dw, dh)            — stretched to rect
//   3. drawImage(img, sx,sy,sw,sh, dx,dy,dw,dh) — source crop → dest rect
//
// Also shows:
//   • alpha fade via setGlobalAlpha()
//   • rotation via save/translate/rotate/restore
//   • pulse-scale animation
//   • tiled repeat (manual loop)
// ============================================================================

class ImageTestSurface : public RenderSurface {
public:
    // ── Lifecycle ─────────────────────────────────────────────────────────────

    void initialize(int w, int h) override {
        (void)w; (void)h;

        // ── Load images via Canvas2D (needs a live NVG context).
        // We can't call ctx.loadImage() here because Canvas2D is only
        // available inside render(). Instead we store the paths and load
        // lazily on the first render() call.
        //
        // Alternatively, if you have a NVGcontext* pointer (e.g. stored on
        // the surface by CanvasWidget), you can load here. For maximum
        // portability we use the lazy pattern below.
    }

    void resize(int w, int h) override { (void)w; (void)h; }

    void destroy() override {
        // freeImage must be called while the GL context is still current.
        // CanvasWidget calls destroy() before tearing down the context, so
        // this is the right place.
        if (ctx_) {
            if (photo_)   { ctx_->freeImage(photo_);   photo_   = nullptr; }
            if (logo_)    { ctx_->freeImage(logo_);    logo_    = nullptr; }
            if (tile_)    { ctx_->freeImage(tile_);    tile_    = nullptr; }
            ctx_ = nullptr;
        }
    }

    void update(double dt) override {
        time_ += dt;
    }

    // ── Render ────────────────────────────────────────────────────────────────

    void render(Canvas2D& ctx) override {
        // ── Lazy image load (first frame only) ────────────────────────────────
        if (!loaded_) {
            ctx_ = &ctx;   // stash pointer for destroy()
            loadImages(ctx);
            loaded_ = true;
        }

        const float W = float(ctx.width());
        const float H = float(ctx.height());
        const float t = float(time_);

        // ── Background ────────────────────────────────────────────────────────
        ctx.setFillColor({15, 15, 25, 255});
        ctx.fillRect(0, 0, W, H);

        // ── Title ─────────────────────────────────────────────────────────────
        ctx.setFont("bold 20px Segoe UI");
        ctx.setTextAlign(TextAlign::Center);
        ctx.setTextBaseline(TextBaseline::Top);
        ctx.setFillColor({220, 220, 255, 255});
        ctx.fillText("Canvas2D — Image Rendering Demo", W * 0.5f, 12.f);

        // ─────────────────────────────────────────────────────────────────────
        // Guard: draw placeholder boxes when images failed to load.
        // ─────────────────────────────────────────────────────────────────────
        if (!photo_ && !logo_ && !tile_) {
            drawMissingPlaceholder(ctx, W, H);
            return;
        }

        float col1 = 30.f;
        float col2 = W * 0.5f + 10.f;
        float row1 = 50.f;
        float row2 = H * 0.5f + 10.f;
        float cellW = W * 0.5f - 40.f;
        float cellH = H * 0.5f - 70.f;

        // ── Cell labels ───────────────────────────────────────────────────────
        ctx.setFont("11px Segoe UI");
        ctx.setTextAlign(TextAlign::Left);
        ctx.setFillColor({100, 120, 160, 255});
        ctx.fillText("1. Natural size + alpha fade",  col1, row1);
        ctx.fillText("2. Stretch to rect",            col2, row1);
        ctx.fillText("3. Source crop (sprite sheet)", col1, row2);
        ctx.fillText("4. Rotate + scale animation",   col2, row2);

        float cy1 = row1 + 18.f;
        float cy2 = row2 + 18.f;

        // ╔════════════════════════════════════════╗
        // ║  TOP-LEFT  — Natural size + alpha fade ║
        // ╚════════════════════════════════════════╝
        ctx.pushClipRect(col1, cy1, cellW, cellH);
        if (photo_) {
            // Pulse alpha between 0.3 and 1.0
            float alpha = 0.65f + 0.35f * std::sin(t * 1.4f);
            ctx.setGlobalAlpha(alpha);

            // drawImage overload 1: natural pixel size, top-left at (col1, cy1)
            ctx.drawImage(photo_, col1, cy1);

            ctx.setGlobalAlpha(1.f);

            // Label
            ctx.setFont("10px Consolas");
            ctx.setFillColor({180, 220, 255, 200});
            ctx.setTextAlign(TextAlign::Left);
            ctx.setTextBaseline(TextBaseline::Bottom);
            ctx.fillText("drawImage(img, x, y)  alpha=" +
                         std::to_string(int(alpha * 100)) + "%",
                         col1 + 4, cy1 + cellH - 2);
        }
        ctx.popClipRect();

        // ╔════════════════════════════════════════╗
        // ║  TOP-RIGHT — Stretch to rect           ║
        // ╚════════════════════════════════════════╝
        ctx.pushClipRect(col2, cy1, cellW, cellH);
        if (photo_) {
            // drawImage overload 2: fill the entire cell, ignoring aspect ratio
            ctx.drawImage(photo_, col2, cy1, cellW, cellH);

            // Thin border to show the rect boundary
            ctx.setStrokeColor({100, 160, 255, 160});
            ctx.setLineWidth(1.5f);
            ctx.strokeRect(col2, cy1, cellW, cellH);

            ctx.setFont("10px Consolas");
            ctx.setFillColor({180, 220, 255, 200});
            ctx.setTextAlign(TextAlign::Left);
            ctx.setTextBaseline(TextBaseline::Bottom);
            ctx.fillText("drawImage(img, x,y,w,h)", col2 + 4, cy1 + cellH - 2);
        }
        ctx.popClipRect();

        // ╔════════════════════════════════════════╗
        // ║  BOTTOM-LEFT — Source crop             ║
        // ╚════════════════════════════════════════╝
        ctx.pushClipRect(col1, cy2, cellW, cellH);
        if (logo_) {
            // Animate which "quadrant" of the source image we crop from.
            // (Works great for sprite sheets — just set sx/sy to your frame.)
            int   frame = int(t * 2.f) % 4;
            float hw = logo_->width  * 0.5f;
            float hh = logo_->height * 0.5f;
            float sx = (frame & 1) ? hw : 0.f;
            float sy = (frame & 2) ? hh : 0.f;

            // Draw that quadrant stretched to fill half the cell
            float dw = cellW * 0.6f;
            float dh = cellH * 0.6f;
            float dx = col1 + (cellW - dw) * 0.5f;
            float dy = cy2  + (cellH - dh) * 0.5f;

            // drawImage overload 3: (src crop) → (dest rect)
            ctx.drawImage(logo_,
                          sx, sy, hw, hh,   // source region (pixels)
                          dx, dy, dw, dh);  // dest region (canvas units)

            // Show crop region info
            ctx.setFont("10px Consolas");
            ctx.setFillColor({180, 220, 255, 200});
            ctx.setTextAlign(TextAlign::Left);
            ctx.setTextBaseline(TextBaseline::Bottom);
            ctx.fillText("drawImage(img, sx,sy,sw,sh, dx,dy,dw,dh)  frame=" +
                         std::to_string(frame),
                         col1 + 4, cy2 + cellH - 2);

            // Draw tiny source preview in corner with crop rect highlighted
            float prevW = 80.f, prevH = 60.f;
            ctx.drawImage(logo_, col1 + 4, cy2 + 4, prevW, prevH);
            ctx.setStrokeColor({255, 220, 40, 220});
            ctx.setLineWidth(1.5f);
            ctx.strokeRect(col1 + 4 + sx / logo_->width  * prevW,
                           cy2  + 4 + sy / logo_->height * prevH,
                           hw / logo_->width  * prevW,
                           hh / logo_->height * prevH);
        }
        ctx.popClipRect();

        // ╔════════════════════════════════════════╗
        // ║  BOTTOM-RIGHT — Rotate + scale anim   ║
        // ╚════════════════════════════════════════╝
        ctx.pushClipRect(col2, cy2, cellW, cellH);
        if (tile_) {
            // ── Tiled background (manual loop) ────────────────────────────────
            int tw = tile_->width  > 0 ? tile_->width  : 32;
            int th = tile_->height > 0 ? tile_->height : 32;

            // Scroll offset so tiles appear to drift
            float offX = std::fmod(t * 20.f, float(tw));
            float offY = std::fmod(t * 12.f, float(th));

            ctx.setGlobalAlpha(0.25f);
            for (float ty = cy2 - th + offY; ty < cy2 + cellH; ty += th) {
                for (float tx = col2 - tw + offX; tx < col2 + cellW; tx += tw) {
                    ctx.drawImage(tile_, tx, ty, float(tw), float(th));
                }
            }
            ctx.setGlobalAlpha(1.f);

            // ── Spinning, pulsing logo in the centre ─────────────────────────
            if (logo_) {
                float cx = col2 + cellW * 0.5f;
                float cy = cy2  + cellH * 0.5f;

                float scale = 0.55f + 0.15f * std::sin(t * 2.3f);
                float dw    = logo_->width  * scale;
                float dh    = logo_->height * scale;

                // save → translate to centre → rotate → draw offset by half size
                ctx.save();
                ctx.translate(cx, cy);
                ctx.rotate(t * 0.7f);
                ctx.translate(-dw * 0.5f, -dh * 0.5f);

                ctx.drawImage(logo_, 0.f, 0.f, dw, dh);

                ctx.restore();

                // Rotating ring decoration
                ctx.setStrokeColor({180, 140, 255, 140});
                ctx.setLineWidth(1.5f);
                ctx.beginPath();
                ctx.arc(cx, cy, dw * 0.65f, 0, 6.2832f);
                ctx.stroke();
            }

            ctx.setFont("10px Consolas");
            ctx.setFillColor({180, 220, 255, 200});
            ctx.setTextAlign(TextAlign::Left);
            ctx.setTextBaseline(TextBaseline::Bottom);
            ctx.fillText("save/translate/rotate/drawImage/restore",
                         col2 + 4, cy2 + cellH - 2);
        }
        ctx.popClipRect();

        // ── Dividers ──────────────────────────────────────────────────────────
        ctx.setStrokeColor({50, 55, 80, 255});
        ctx.setLineWidth(1.f);
        ctx.beginPath();
        ctx.moveTo(W * 0.5f, 42.f);
        ctx.lineTo(W * 0.5f, H - 8.f);
        ctx.stroke();
        ctx.beginPath();
        ctx.moveTo(18.f,     H * 0.5f);
        ctx.lineTo(W - 18.f, H * 0.5f);
        ctx.stroke();
    }

    bool needsContinuousRedraw() const override { return true; }

private:
    // ── Image handles ─────────────────────────────────────────────────────────
    Canvas2DImage* photo_ = nullptr;  // any photo / texture
    Canvas2DImage* logo_  = nullptr;  // any square-ish image (acts as sprite sheet)
    Canvas2DImage* tile_  = nullptr;  // small tileable pattern
    Canvas2D*      ctx_   = nullptr;  // stashed for destroy()
    bool           loaded_= false;
    double         time_  = 0.0;

    // ── Image loading ─────────────────────────────────────────────────────────
    void loadImages(Canvas2D& ctx) {
        // ── Option A: load from file paths ────────────────────────────────────
        // Adjust paths to match your project's asset layout.
        photo_ = ctx.loadImage("screenshots/batman.jpg");
        logo_  = ctx.loadImage("screenshots/counter.png");
        tile_  = ctx.loadImage("screenshots/graph.png");

        // ── Option B: load from memory (e.g. embedded byte arrays) ───────────
        // #include "assets/logo_data.h"   // extern const unsigned char logo_png[];
        //                                 // extern const int logo_png_len;
        // logo_ = ctx.loadImageFromMemory(logo_png, logo_png_len);

        // ── Fallback: generate a 64×64 procedural image via stb_image ────────
        // If any image failed, synthesise a simple checkerboard so the demo
        // still shows something without asset files present.
        if (!photo_) photo_ = makeCheckerboard(ctx, 200, 150, {220,80,80,255}, {40,20,20,255});
        if (!logo_)  logo_  = makeCheckerboard(ctx, 128, 128, {80,160,220,255},{10,30,50,255});
        if (!tile_)  tile_  = makeCheckerboard(ctx,  32,  32, {60,60,80,255},  {30,30,45,255});
    }

    // ── Procedural checkerboard (no asset files needed) ───────────────────────
    // Generates RGBA pixel data with stb_image-compatible layout, then uploads
    // it via loadImageFromMemory (which calls nvgCreateImageMem internally).
    // Useful for tests and fallbacks.
    static Canvas2DImage* makeCheckerboard(Canvas2D& ctx,
                                           int w, int h,
                                           Color a, Color b,
                                           int squareSize = 16) {
        // Build raw RGBA — stb's nvgCreateImageMem expects a valid PNG/JPG,
        // so we use nvgCreateImageRGBA via the escape hatch ctx.nvg() instead.
        std::vector<uint8_t> pixels(size_t(w) * h * 4);
        for (int row = 0; row < h; ++row) {
            for (int col = 0; col < w; ++col) {
                bool even = ((col / squareSize) + (row / squareSize)) % 2 == 0;
                Color c   = even ? a : b;
                size_t i  = size_t(row * w + col) * 4;
                pixels[i+0] = c.r;
                pixels[i+1] = c.g;
                pixels[i+2] = c.b;
                pixels[i+3] = c.a;
            }
        }
        // Use nvgCreateImageRGBA directly through the NVG escape hatch.
        int handle = nvgCreateImageRGBA(ctx.nvg(), w, h, 0, pixels.data());
        if (handle <= 0) return nullptr;

        auto* img      = new Canvas2DImage();
        img->nvgHandle = handle;
        img->width     = w;
        img->height    = h;
        return img;
    }

    // ── Placeholder when all loads failed ─────────────────────────────────────
    static void drawMissingPlaceholder(Canvas2D& ctx, float W, float H) {
        ctx.setFont("14px Segoe UI");
        ctx.setTextAlign(TextAlign::Center);
        ctx.setTextBaseline(TextBaseline::Middle);
        ctx.setFillColor({200, 100, 100, 255});
        ctx.fillText("No images loaded — add assets/ folder", W*0.5f, H*0.5f - 20.f);
        ctx.setFont("11px Consolas");
        ctx.setFillColor({140, 140, 180, 200});
        ctx.fillText("photo.jpg  logo.png  tile.png", W*0.5f, H*0.5f + 10.f);
    }
};

// ── App entry point ───────────────────────────────────────────────────────────

class ImageTestApp : public Widget {
public:
    WidgetPtr build() override {
        auto canvas = std::make_shared<CanvasWidget>();
        canvas->setSize(860, 660);
        canvas->setCanvasSize(860, 660);
        canvas->setScrollbarsEnabled(false);
        canvas->setSurface<ImageTestSurface>();
        return Scaffold(nullptr, Center(canvas));
    }
};

WidgetPtr createApp(FluxUI* app) {
    return FluxApp("Canvas2D — Image Test",
                   std::make_shared<ImageTestApp>(),
                   AppTheme::dark(),
                   false, 900, 700, false, false);
}