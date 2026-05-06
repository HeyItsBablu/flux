#include "flux/flux.hpp"
#include <atomic>
#include <mutex>

class ImageSurface : public RenderSurface {
public:
  void initialize(int w, int h) override {
    viewW_ = w;
    viewH_ = h;
  }
  void resize(int w, int h) override {
    viewW_ = w;
    viewH_ = h;
  }

  void destroy() override {
    if (img_ && ctx_) {
      ctx_->freeImage(img_);
      img_ = nullptr;
    }
    ctx_ = nullptr;
  }

  void update(double) override {}

  void render(Canvas2D &ctx) override {
    ctx_ = &ctx;

    if (pendingLoad_.load()) {
      pendingLoad_.store(false);
      std::string path;
      {
        std::lock_guard<std::mutex> lk(mx_);
        path = pending_;
      }
      if (img_) {
        ctx_->freeImage(img_);
        img_ = nullptr;
      }
      img_ = ctx_->loadImage(path);
    }

    float cw = float(ctx.width());
    float ch = float(ctx.height());

    if (!img_) {
      // Solid fill first — if this shows, canvas is working
      ctx.setFillColor(Color::fromRGB(30, 30, 40));
      ctx.fillRect(0, 0, cw, ch);
      return;
    }

    ctx.setFillColor(Color::fromRGB(18, 18, 24));
    ctx.fillRect(0, 0, cw, ch);

    float iw = float(img_->width), ih = float(img_->height);
    float scale = std::min(cw / iw, ch / ih);
    float dw = iw * scale, dh = ih * scale;
    ctx.drawImage(img_, (cw - dw) * 0.5f, (ch - dh) * 0.5f, dw, dh);
  }

  void requestLoad(const std::string &path) {
    {
      std::lock_guard<std::mutex> lk(mx_);
      pending_ = path;
    }
    pendingLoad_.store(true);
  }

  bool needsContinuousRedraw() const override { return true; }

private:
  Canvas2D *ctx_ = nullptr;
  Canvas2DImage *img_ = nullptr;
  int viewW_ = 1, viewH_ = 1;
  std::mutex mx_;
  std::string pending_;
  std::atomic<bool> pendingLoad_{false};
};

class ImageApp : public Widget {
  State<std::string> filePath{""};

public:
  WidgetPtr build() override {
    auto canvas = std::make_shared<CanvasWidget>();
    canvas->setViewportEnabled(false);
    canvas->setScrollbarsEnabled(false);
    auto surface = canvas->setSurface<ImageSurface>();

    std::weak_ptr<ImageSurface> weakSurface = surface;

    auto appBar = std::make_shared<AppBarWidget>();
    appBar->hasBackground = true;
    appBar->backgroundColor = Color::fromRGB(33, 150, 243);

    appBar->addChild(
        Row({
                Text("Image Viewer")
                    ->setFontSize(20)
                    ->setFontWeight(FontWeight::Bold)
                    ->setTextColor(Color::fromRGB(255, 255, 255)),
                FilePicker()
                    ->setMode(FilePickerMode::Open)
                    ->addFilter("Images", {"*.png", "*.jpg", "*.jpeg", "*.bmp"})
                    ->addFilter("All Files", {"*.*"})
                    ->setShowPath(true)
                    ->bindPath(filePath)
                    ->setOnChanged([weakSurface](const std::string &path) {
                      if (auto s = weakSurface.lock())
                        s->requestLoad(path);
                    }),
            })
            ->setCrossAxisAlignment(CrossAxisAlignment::Center)
            ->setSpacing(12));

    return Scaffold(appBar, Expanded(canvas), nullptr, nullptr);
  }
};

WidgetPtr createApp(FluxUI *app) {
  return FluxApp("Image Viewer", std::make_shared<ImageApp>(), AppTheme::dark(),
                 false, 900, 700, false, false);
}