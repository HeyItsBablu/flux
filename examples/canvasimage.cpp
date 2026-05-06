#include "flux/flux.hpp"
#include "stb_image.h"

class ImageSurface : public RenderSurface {
public:
    void initialize(int w, int h) override {
        viewW_ = w; viewH_ = h;
        loadFromFile("C:/Upwork/c_projects/flux/screenshots/batman.jpg"); // change path as needed
    }

    void resize(int w, int h) override {
        viewW_ = w; viewH_ = h;
    }

    void destroy() override {
        if (img_) { ctx_->freeImage(img_); img_ = nullptr; }
    }

    void update(double) override {}

    void render(Canvas2D& ctx) override {
        ctx_ = &ctx;

        // Background
        ctx.setFillColor(Color::fromRGB(20, 20, 26));
        ctx.fillRect(0, 0, float(ctx.width()), float(ctx.height()));

        if (!img_) {
            // Try loading if not yet loaded
            loadFromFile("C:/Upwork/c_projects/flux/screenshots/batman.jpg");
            if (!img_) return;
        }

        // Letterbox fit
        float iw = float(img_->width);
        float ih = float(img_->height);
        float cw = float(ctx.width());
        float ch = float(ctx.height());

        float scale = std::min(cw / iw, ch / ih);
        float dw    = iw * scale;
        float dh    = ih * scale;
        float dx    = (cw - dw) * 0.5f;
        float dy    = (ch - dh) * 0.5f;

        ctx.drawImage(img_, dx, dy, dw, dh);
    }

private:
    Canvas2D*      ctx_  = nullptr;
    Canvas2DImage* img_  = nullptr;
    int viewW_ = 1, viewH_ = 1;

    void loadFromFile(const char* path) {
        if (!ctx_) return;
        if (img_) { ctx_->freeImage(img_); img_ = nullptr; }
        img_ = ctx_->loadImage(path);
    }
};

class ImageApp : public Widget {
public:
    WidgetPtr build() override {
        auto canvas = std::make_shared<CanvasWidget>();
        canvas->setViewportEnabled(false);
        canvas->setScrollbarsEnabled(false);
        canvas->setSurface<ImageSurface>();

        return Scaffold(
            AppBar("Image Viewer"),
            Expanded(canvas),
            nullptr, nullptr
        );
    }
};

WidgetPtr createApp(FluxUI* app) {
    return FluxApp("Image Viewer",
                   std::make_shared<ImageApp>(),
                   AppTheme::dark(),
                   false, 800, 600, false, false);
}