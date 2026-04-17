// test_image_viewer.hpp
#pragma once
#include "flux/flux.hpp"
#include "stb_image.h"

// ============================================================================
// SimpleImageSurface — loads and displays a single image, nothing else.
// ============================================================================

class SimpleImageSurface : public RenderSurface {
public:
    void initialize(int w, int h) override { (void)w; (void)h; }
    void resize(int w, int h)     override { (void)w; (void)h; }
    void destroy()                override {
        // nvgImage_ dies with the NVG context — just null the pointer.
        nvgImage_ = nullptr;
    }
    void update(double) override {}

    // Called by the app after the user picks a file.
    bool loadImage(const std::string& path) {
        pendingPath_ = path;
        return true; // actual load happens on next render() when NVG is live
    }

    bool hasImage() const { return nvgImage_ != nullptr; }
    int  imgW()     const { return imgW_; }
    int  imgH()     const { return imgH_; }

    void render(Canvas2D& ctx) override {
        const float W = float(ctx.width());
        const float H = float(ctx.height());

        // Background
        ctx.setFillColor({20, 20, 28, 255});
        ctx.fillRect(0, 0, W, H);

        // Lazy load from pending path
        if (!pendingPath_.empty()) {
            doLoad(ctx, pendingPath_);
            pendingPath_.clear();
        }

        if (!nvgImage_) {
            // No image yet — hint text
            ctx.setFont("16px Segoe UI");
            ctx.setTextAlign(TextAlign::Center);
            ctx.setTextBaseline(TextBaseline::Middle);
            ctx.setFillColor({80, 80, 100, 255});
            ctx.fillText("Click 'Open Image' to load a file", W * 0.5f, H * 0.5f);
            return;
        }

        // Draw image to fill canvas (CanvasWidget handles pan/zoom)
        ctx.drawImage(nvgImage_, 0.f, 0.f, float(imgW_), float(imgH_));
    }

    bool needsContinuousRedraw() const override { return false; }

private:
    std::string    pendingPath_;
    Canvas2DImage* nvgImage_ = nullptr;
    int            imgW_ = 0, imgH_ = 0;

    void doLoad(Canvas2D& ctx, const std::string& path) {
        // Free previous image
        if (nvgImage_) {
            ctx.freeImage(nvgImage_);
            nvgImage_ = nullptr;
        }

        // Load pixels with stb_image
        int w, h, ch;
        stbi_set_flip_vertically_on_load(0);
        unsigned char* data = stbi_load(path.c_str(), &w, &h, &ch, 4);
        if (!data) return;

        // Upload directly as raw RGBA via NVG escape hatch
        int handle = nvgCreateImageRGBA(ctx.nvg(), w, h, 0, data);
        stbi_image_free(data);
        if (handle <= 0) return;

        auto* img      = new Canvas2DImage();
        img->nvgHandle = handle;
        img->width     = w;
        img->height    = h;
        nvgImage_ = img;
        imgW_ = w;
        imgH_ = h;
    }
};

// ============================================================================
// SimpleImageViewerApp
// ============================================================================

class SimpleImageViewerApp : public Widget {
    State<std::string> statusMsg{"Click 'Open Image' to load a file."};

    std::shared_ptr<SimpleImageSurface> surface_;
    CanvasWidget* canvasPtr_ = nullptr;

public:
    WidgetPtr build() override {
        // Canvas fills the whole window minus toolbar
        auto canvas = std::make_shared<CanvasWidget>();
        canvas->setSize(900, 640);
        canvas->setCanvasSize(1, 1);
        canvas->setScrollbarsEnabled(true);
        surface_   = canvas->setSurface<SimpleImageSurface>();
        canvasPtr_ = canvas.get();

        // ── Toolbar ───────────────────────────────────────────────────────
        auto toolbar = Container(
            Row({
                FilePicker("📂 Open Image")
                    ->setMode(FilePickerMode::Open)
                    ->addFilter("Images", {"*.jpg","*.jpeg","*.png","*.bmp","*.tga"})
                    ->addFilter("All Files", {"*.*"})
                    ->setShowPath(false)
                    ->setHeight(28)
                    ->setWidth(130)
                    ->setOnChanged([this](const std::string& path) {
                        if (path.empty()) return;

                        // Load into surface
                        surface_->loadImage(path);

                        // Resize canvas to image dimensions and fit view
                        // We don't know size yet (lazy load), so trigger a
                        // redraw first then fit after the first render sets
                        // imgW_/imgH_.
                        canvasPtr_->redraw();

                        // One-shot: after first render we know the size
                        // Use a small helper lambda triggered by redraw.
                        fitAfterLoad_ = true;

                        // Extract filename for status
                        size_t pos = path.find_last_of("\\/");
                        std::string name = (pos != std::string::npos)
                            ? path.substr(pos + 1) : path;
                        statusMsg.set("Loaded: " + name);
                    }),

                SizedBox(12, 0),

                Button("Fit", [this]() {
                    if (!canvasPtr_) return;
                    canvasPtr_->viewport().fitToView();
                    canvasPtr_->redraw();
                })->setBackgroundColor(Color::fromRGB(30, 30, 46))
                  ->setTextColor(Color::fromRGB(174, 129, 255))
                  ->setBorderRadius(5)
                  ->setWidth(40)->setHeight(28)->setPadding(4),

                Button("1:1", [this]() {
                    if (!canvasPtr_) return;
                    canvasPtr_->viewport().resetZoom();
                    canvasPtr_->redraw();
                })->setBackgroundColor(Color::fromRGB(30, 30, 46))
                  ->setTextColor(Color::fromRGB(174, 129, 255))
                  ->setBorderRadius(5)
                  ->setWidth(40)->setHeight(28)->setPadding(4),

                SizedBox(16, 0),

                Text(statusMsg, [](const std::string& s){ return s; })
                    ->setFontSize(10)
                    ->setTextColor(Color::fromRGB(148, 226, 213)),
            })->setSpacing(4)->setCrossAxisAlignment(CrossAxisAlignment::Center))
            ->setBackgroundColor(Color::fromRGB(17, 17, 27))
            ->setPaddingAll(10, 7, 10, 7)
            ->setHeight(44);

        // Hook viewport changes to detect when image is ready to fit
        canvasPtr_->onViewportChanged = [this](float) {
            if (fitAfterLoad_ && surface_->hasImage()) {
                fitAfterLoad_ = false;
                canvasPtr_->setCanvasSize(surface_->imgW(), surface_->imgH());
                canvasPtr_->viewport().fitToView();
                canvasPtr_->redraw();
            }
        };

        return Scaffold(nullptr,
            Column({ toolbar, canvas })->setSpacing(0));
    }

private:
    bool fitAfterLoad_ = false;
};

// ============================================================================
// Entry point
// ============================================================================

WidgetPtr createApp(FluxUI* app) {
    return FluxApp("Simple Image Viewer",
                   std::make_shared<SimpleImageViewerApp>(),
                   AppTheme::dark(),
                   false, 960, 700, false, false);
}