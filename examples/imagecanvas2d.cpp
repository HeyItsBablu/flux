#include "flux/flux.hpp"
#include "stb_image.h"
#include "stb_image_write.h"

// ============================================================================
// ImageSurface — Canvas2D version
// ============================================================================

class ImageSurface : public RenderSurface {
public:
    std::function<void()>            onLoaded;
    std::function<void(const char*)> onStatus;

    void loadImage(const std::string& path) { pendingPath_ = path; }
    void setExposure(float v) { exposure_ = v; }
    void setContrast(float v) { contrast_ = v; }
    int  imgW() const { return imgW_; }
    int  imgH() const { return imgH_; }
    bool hasImage() const { return img_ != nullptr; }

    // ── Export ────────────────────────────────────────────────────────────────
    bool exportImage(Canvas2D& ctx, const std::string& path) {
        if (!img_) return false;

        // Render at full image resolution into pixel buffer via getImageData
        // First draw the image into a temp canvas at full size
        ctx.save();
        ctx.resetTransform();
        ctx.drawImage(img_, 0, 0, float(imgW_), float(imgH_));
        applyExposureContrast(ctx, 0, 0, imgW_, imgH_);

        std::vector<uint8_t> pixels;
        ctx.getImageData(0, 0, float(imgW_), float(imgH_), pixels);
        ctx.restore();

        if (pixels.empty()) {
            if (onStatus) onStatus("Export failed — no pixel data.");
            return false;
        }

        bool ok = false;
        std::string ext = path.substr(path.rfind('.'));
        for (auto& c : ext) c = char(tolower(c));

        int stride = imgW_ * 4;
        if (ext == ".jpg" || ext == ".jpeg")
            ok = stbi_write_jpg(path.c_str(), imgW_, imgH_, 4, pixels.data(), 95) != 0;
        else
            ok = stbi_write_png(path.c_str(), imgW_, imgH_, 4, pixels.data(), stride) != 0;

        if (onStatus) onStatus(ok ? "Export saved." : "Export failed.");
        return ok;
    }

    // ── RenderSurface ─────────────────────────────────────────────────────────
    void initialize(int /*w*/, int /*h*/) override {}
    void resize(int /*w*/, int /*h*/)     override {}
    void destroy() override {
        // Canvas2DImage is owned by us — free it
        if (ctx_ && img_) {
            ctx_->freeImage(img_);
            img_ = nullptr;
        }
        ctx_ = nullptr;
    }

    void update(double /*dt*/) override {
        if (!pendingPath_.empty() && ctx_) {
            loadFromFile(pendingPath_);
            pendingPath_.clear();
        }
    }

    void render(Canvas2D& ctx) override {
        // Cache ctx pointer so update() and exportImage() can use it
        ctx_ = &ctx;

        // Background
        ctx.setFillColor({20, 20, 30, 255});
        ctx.fillRect(0, 0, float(ctx.width()), float(ctx.height()));

        if (!img_) {
            // No image loaded yet — draw placeholder
            ctx.setFillColor({60, 60, 80, 255});
            ctx.fillRoundedRect(ctx.width()*0.5f - 120, ctx.height()*0.5f - 40,
                                240, 80, 12);
            ctx.setFont("14px Segoe UI");
            ctx.setTextAlign(TextAlign::Center);
            ctx.setTextBaseline(TextBaseline::Middle);
            ctx.setFillColor({140, 140, 180, 255});
            ctx.fillText("Open an image to begin",
                         ctx.width() * 0.5f, ctx.height() * 0.5f);
            return;
        }

        // ── Draw image ────────────────────────────────────────────────────────
        ctx.drawImage(img_, 0, 0, float(imgW_), float(imgH_));

        // ── Exposure / contrast overlay ───────────────────────────────────────
        // Canvas2D can't run GPU shaders, so we approximate with blend modes:
        //   Exposure  → screen blend for bright, multiply for dark
        //   Contrast  → darken shadows, brighten highlights

        applyExposureContrast(ctx, 0, 0, imgW_, imgH_);
    }

private:
    std::string    pendingPath_;
    Canvas2DImage* img_  = nullptr;
    Canvas2D*      ctx_  = nullptr;
    int            imgW_ = 0, imgH_ = 0;
    float          exposure_ = 0.f, contrast_ = 0.f;

    // ── Approximate exposure + contrast via alpha-blended overlays ────────────
    //
    // This is a CPU-side approximation — not identical to the GPU shader
    // but visually close for moderate values.
    //
    // Exposure > 0 → white overlay at low alpha (brightens)
    // Exposure < 0 → black overlay at low alpha (darkens)
    // Contrast > 0 → dark overlay on shadows + bright on highlights (S-curve approx)
    // Contrast < 0 → grey overlay (flattens)

    void applyExposureContrast(Canvas2D& ctx, float x, float y, int w, int h) {
        if (std::abs(exposure_) < 0.01f && std::abs(contrast_) < 0.01f)
            return;

        ctx.save();
        ctx.pushClipRect(x, y, float(w), float(h));

        // Exposure
        if (exposure_ > 0.f) {
            // Brighten — white overlay, alpha proportional to exposure
            uint8_t a = (uint8_t)std::min(240.f, exposure_ / 5.f * 200.f);
            ctx.setFillColor({255, 255, 255, a});
            ctx.setCompositeOp(CompositeOp::Screen);
            ctx.fillRect(x, y, float(w), float(h));
            ctx.setCompositeOp(CompositeOp::SourceOver);
        } else if (exposure_ < 0.f) {
            // Darken — black overlay
            uint8_t a = (uint8_t)std::min(220.f, -exposure_ / 5.f * 200.f);
            ctx.setFillColor({0, 0, 0, a});
            ctx.fillRect(x, y, float(w), float(h));
        }

        // Contrast — darken bottom half, brighten top half of tonal range
        if (contrast_ > 0.f) {
            uint8_t a = (uint8_t)(contrast_ * 80.f);
            // Shadow crush — dark overlay at multiply
            ctx.setFillColor({0, 0, 0, a});
            ctx.setCompositeOp(CompositeOp::Multiply);
            ctx.fillRect(x, y, float(w), float(h));
            ctx.setCompositeOp(CompositeOp::SourceOver);
            // Highlight lift — white overlay at screen
            ctx.setFillColor({255, 255, 255, (uint8_t)(a / 2)});
            ctx.setCompositeOp(CompositeOp::Screen);
            ctx.fillRect(x, y, float(w), float(h));
            ctx.setCompositeOp(CompositeOp::SourceOver);
        } else if (contrast_ < 0.f) {
            // Flatten — grey overlay
            uint8_t a = (uint8_t)(-contrast_ * 80.f);
            ctx.setFillColor({128, 128, 128, a});
            ctx.fillRect(x, y, float(w), float(h));
        }

        ctx.popClipRect();
        ctx.restore();
    }

    // ── Load via stb → Canvas2DImage ─────────────────────────────────────────
    void loadFromFile(const std::string& path) {
        if (!ctx_) return;

        // Free previous image
        if (img_) { ctx_->freeImage(img_); img_ = nullptr; }

        // loadImage() on Canvas2D uses GDI+ Bitmap internally
        img_ = ctx_->loadImage(path);
        if (!img_) {
            if (onStatus) onStatus("Failed to load image.");
            return;
        }

        imgW_ = img_->width;
        imgH_ = img_->height;

        if (onLoaded) onLoaded();

        char buf[64];
        snprintf(buf, sizeof(buf), "Loaded %dx%d", imgW_, imgH_);
        if (onStatus) onStatus(buf);
    }
};

// ============================================================================
// App — identical to before, exportImage signature updated
// ============================================================================

class ImageApp : public Widget {
    std::shared_ptr<ImageSurface> surface_;
    CanvasWidget* canvasPtr_ = nullptr;
    State<std::string> status_{"Open an image."};
    State<double> sExposure{0.0};
    State<double> sContrast{0.0};

public:
    WidgetPtr build() override {
        auto canvas = std::make_shared<CanvasWidget>();
        canvas->setSize(800, 600);
        canvas->setCanvasSize(800, 600);
        canvas->setScrollbarsEnabled(true);
        canvasPtr_ = canvas.get();

        surface_ = canvas->setSurface<ImageSurface>();

        surface_->onLoaded = [this]() {
            canvasPtr_->setCanvasSize(surface_->imgW(), surface_->imgH());
            canvasPtr_->viewport().fitToView();
            canvasPtr_->redraw();
        };
        surface_->onStatus = [this](const char* m) { status_.set(m); };

        sExposure.listen([this](double v) {
            surface_->setExposure(float(v));
            canvasPtr_->redraw();
        });
        sContrast.listen([this](double v) {
            surface_->setContrast(float(v));
            canvasPtr_->redraw();
        });

        auto toolbar = Container(
            Row({
                FilePicker("📂 Open")
                    ->setMode(FilePickerMode::Open)
                    ->addFilter("Images", {"*.jpg","*.jpeg","*.png","*.bmp"})
                    ->addFilter("All Files", {"*.*"})
                    ->setOnChanged([this](const std::string& path) {
                        status_.set("Loading...");
                        surface_->loadImage(path);
                        canvasPtr_->redraw();
                    })
                    ->setShowPath(false)
                    ->setHeight(28)->setWidth(100),

                SizedBox(8, 0),

                // Export button — needs canvas context so we grab it from surface
                FilePicker("💾 Export")
                    ->setMode(FilePickerMode::Save)
                    ->setTitle("Export Image")
                    ->setDefaultFilename("edited.png")
                    ->addFilter("PNG",  {"*.png"})
                    ->addFilter("JPEG", {"*.jpg","*.jpeg"})
                    ->setDefaultExtension("png")
                    ->setOnChanged([this](const std::string& path) {
                        // Export is triggered on next render() call via flag
                        exportPath_ = path;
                        canvasPtr_->redraw();
                    })
                    ->setShowPath(false)
                    ->setHeight(28)->setWidth(100),

                SizedBox(16, 0),
                Text("Exposure")->setFontSize(10)
                    ->setTextColor(Color::fromRGB(160,160,180)),
                SizedBox(6, 0),
                Slider(-5.0, 5.0, 0.05)
                    ->setValue(sExposure)
                    ->setTrackFillColor(Color::fromRGB(250,220,100))
                    ->setWidth(140),
                SizedBox(6, 0),
                Text(sExposure, [](double v) {
                    char b[16]; snprintf(b, sizeof(b), "%.2f", v);
                    return std::string(b);
                })->setFontSize(10)->setTextColor(Color::fromRGB(200,200,220))
                  ->setMinWidth(36),

                SizedBox(16, 0),
                Text("Contrast")->setFontSize(10)
                    ->setTextColor(Color::fromRGB(160,160,180)),
                SizedBox(6, 0),
                Slider(-1.0, 1.0, 0.01)
                    ->setValue(sContrast)
                    ->setTrackFillColor(Color::fromRGB(174,129,255))
                    ->setWidth(140),
                SizedBox(6, 0),
                Text(sContrast, [](double v) {
                    char b[16]; snprintf(b, sizeof(b), "%.2f", v);
                    return std::string(b);
                })->setFontSize(10)->setTextColor(Color::fromRGB(200,200,220))
                  ->setMinWidth(36),

                SizedBox(16, 0),
                Text(status_, [](const std::string& s) { return s; })
                    ->setFontSize(10)
                    ->setTextColor(Color::fromRGB(148,226,213)),
            })
            ->setSpacing(0)
            ->setCrossAxisAlignment(CrossAxisAlignment::Center))
            ->setBackgroundColor(Color::fromRGB(17,17,27))
            ->setPaddingAll(10, 8, 10, 8);

        return Scaffold(nullptr,
            Column({toolbar, canvas})->setSpacing(0));
    }

private:
    std::string exportPath_;
};

WidgetPtr createApp(FluxUI* app) {
    return FluxApp("Image Editor",
                   std::make_shared<ImageApp>(),
                   AppTheme::dark(),
                   false, 900, 680, false, false);
}