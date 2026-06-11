#pragma once
#include "flux/flux.hpp"

#include <cmath>
#include <vector>
#include <string>
#include <queue>
#include <functional>

#include <stb_image_write.h>
#include <stb_image.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ============================================================
// ShapeType
// ============================================================

enum class ShapeType
{
  Brush = 0,
  Pencil
};

// ============================================================
// PaintSurface
// ============================================================

class PaintSurface : public RenderSurface
{
public:
  struct Point
  {
    float x, y;
  };

  struct Stroke
  {
    std::vector<Point> pts;
    Color color;
    float radius;
    bool eraser;
    ShapeType shape;
    std::vector<uint8_t> imageData;
    int imageW = 0, imageH = 0;
    mutable Canvas2DImage *glImage = nullptr;
  };

  Color activeColor_ = Color::fromRGB(30, 144, 255);
  float brushRadius_ = 6.f;
  float currentZoom_ = 1.f;
  bool eraserMode_ = false;
  ShapeType activeShape_ = ShapeType::Brush; // pre-selected

  // callbacks
  std::function<void()> onStrokeCommitted;
  std::function<void(int)> onKeyDownCallback;

  // undo / redo
  bool canUndo() const { return !undoStack_.empty(); }
  bool canRedo() const { return !redoStack_.empty(); }
  void undo()
  {
    if (undoStack_.empty())
      return;
    redoStack_.push_back(strokes_);
    strokes_ = undoStack_.back();
    undoStack_.pop_back();
  }
  void redo()
  {
    if (redoStack_.empty())
      return;
    undoStack_.push_back(strokes_);
    strokes_ = redoStack_.back();
    redoStack_.pop_back();
  }

  void clear()
  {
    pushUndo();
    freeAllGLImages();
    strokes_.clear();
    current_ = nullptr;
    drawing_ = false;
  }

  std::vector<uint8_t> rasterize(int w, int h) const
  {
    std::vector<uint8_t> pixels(size_t(w) * h * 4, 255);

    auto setPixel = [&](int px, int py, Color c)
    {
      if (px < 0 || py < 0 || px >= w || py >= h)
        return;
      uint8_t *p = pixels.data() + (py * w + px) * 4;
      float a = c.a / 255.f;
      p[0] = uint8_t(p[0] * (1.f - a) + c.r * a);
      p[1] = uint8_t(p[1] * (1.f - a) + c.g * a);
      p[2] = uint8_t(p[2] * (1.f - a) + c.b * a);
      p[3] = 255;
    };

    auto rasterLine = [&](float ax, float ay, float bx, float by, float r, Color c)
    {
      float dx = bx - ax, dy = by - ay;
      float len = std::sqrtf(dx * dx + dy * dy);
      int steps = std::max(1, int(len));
      for (int s = 0; s <= steps; s++)
      {
        float t = steps ? float(s) / steps : 0.f;
        float cx = ax + dx * t, cy = ay + dy * t;
        int ix = int(cx), iy = int(cy);
        int ir = int(std::ceilf(r));
        for (int oy = -ir; oy <= ir; oy++)
          for (int ox = -ir; ox <= ir; ox++)
            if (std::sqrtf(float(ox * ox + oy * oy)) <= r)
              setPixel(ix + ox, iy + oy, c);
      }
    };

    auto rasterRect = [&](float x0, float y0, float x1, float y1, float lw, Color c, bool filled)
    {
      float lx = std::min(x0, x1), rx = std::max(x0, x1);
      float ly = std::min(y0, y1), ry2 = std::max(y0, y1);
      if (filled)
        for (int py = int(ly); py <= int(ry2); py++)
          for (int px2 = int(lx); px2 <= int(rx); px2++)
            setPixel(px2, py, c);
      else
      {
        rasterLine(lx, ly, rx, ly, lw, c);
        rasterLine(rx, ly, rx, ry2, lw, c);
        rasterLine(rx, ry2, lx, ry2, lw, c);
        rasterLine(lx, ry2, lx, ly, lw, c);
      }
    };

    auto rasterEllipse = [&](float cx2, float cy2, float rx2, float ry2, float lw, Color c, bool filled)
    {
      int segs = 64;
      if (filled)
      {
        int x0 = int(cx2 - rx2), x1 = int(cx2 + rx2);
        int y0 = int(cy2 - ry2), y1 = int(cy2 + ry2);
        for (int py = y0; py <= y1; py++)
          for (int px2 = x0; px2 <= x1; px2++)
          {
            float fx = (px2 - cx2) / rx2, fy = (py - cy2) / ry2;
            if (fx * fx + fy * fy <= 1.f)
              setPixel(px2, py, c);
          }
      }
      else
      {
        for (int i = 0; i < segs; i++)
        {
          float a0 = float(i) / segs * 2.f * float(M_PI);
          float a1 = float(i + 1) / segs * 2.f * float(M_PI);
          rasterLine(cx2 + cosf(a0) * rx2, cy2 + sinf(a0) * ry2,
                     cx2 + cosf(a1) * rx2, cy2 + sinf(a1) * ry2, lw, c);
        }
      }
    };

    for (auto &s : strokes_)
    {
      if (s.pts.empty() && s.imageData.empty())
        continue;

      if (!s.imageData.empty() && s.imageW > 0 && s.imageH > 0)
      {
        int sw = std::min(s.imageW, w);
        int sh = std::min(s.imageH, h);
        for (int py = 0; py < sh; py++)
          for (int px2 = 0; px2 < sw; px2++)
          {
            const uint8_t *src = s.imageData.data() + (py * s.imageW + px2) * 4;
            uint8_t *dst = pixels.data() + (py * w + px2) * 4;
            float a = src[3] / 255.f;
            dst[0] = uint8_t(dst[0] * (1 - a) + src[0] * a);
            dst[1] = uint8_t(dst[1] * (1 - a) + src[1] * a);
            dst[2] = uint8_t(dst[2] * (1 - a) + src[2] * a);
            dst[3] = 255;
          }
        continue;
      }

      Color col = s.eraser ? Color::fromRGB(255, 255, 255) : s.color;

      switch (s.shape)
      {
      case ShapeType::Brush:
        if (s.pts.size() == 1)
          rasterLine(s.pts[0].x, s.pts[0].y, s.pts[0].x, s.pts[0].y, s.radius, col);
        else
          for (size_t i = 1; i < s.pts.size(); i++)
            rasterLine(s.pts[i - 1].x, s.pts[i - 1].y, s.pts[i].x, s.pts[i].y, s.radius, col);
        break;

      default:
        break;
      }
    }
    return pixels;
  }

  void onMouseDown(float x, float y) override
  {
    pushUndo();
    drawing_ = true;
    float r = brushRadius_ / currentZoom_;
    if (activeShape_ == ShapeType::Pencil)
      r = 1.f / currentZoom_;
    strokes_.push_back({{}, activeColor_, r, eraserMode_, ShapeType::Brush});
    current_ = &strokes_.back();
    x = std::max(0.f, std::min(x, float(w_)));
    y = std::max(0.f, std::min(y, float(h_)));
    current_->pts.push_back({x, y});
  }

  void onMouseMove(float x, float y) override
  {
    if (!drawing_ || !current_)
      return;
    x = std::max(0.f, std::min(x, float(w_)));
    y = std::max(0.f, std::min(y, float(h_)));
    current_->pts.push_back({x, y});
  }

  void onMouseUp(float x, float y) override
  {
    x = std::max(0.f, std::min(x, float(w_)));
    y = std::max(0.f, std::min(y, float(h_)));
    if (current_)
      current_->pts.push_back({x, y});
    drawing_ = false;
    current_ = nullptr;
    redoStack_.clear();
    if (onStrokeCommitted)
      onStrokeCommitted();
  }

  void render(Canvas2D &ctx) override
  {

    ctx.setFillColor(Color::fromRGB(255, 255, 255));
    ctx.fillRect(0, 0, float(w_), float(h_));

    for (auto &s : strokes_)
      renderBrushStroke(ctx, s);
    if (current_)
      renderBrushStroke(ctx, *current_);
  }
  void onKeyDown(const KeyEvent &e) override
  {
    if (onKeyDownCallback)
      onKeyDownCallback(e.virtualKey);
  }

  // public helpers for resize
  void pushUndo_public() { pushUndo(); }
  void freeAllGLImages_public() { freeAllGLImages(); }
  std::vector<Stroke> &strokes_public() { return strokes_; }
  void resize_public(int w, int h)
  {
    w_ = w;
    h_ = h;
  }
  int canvasW() const { return w_; }
  int canvasH() const { return h_; }

  void initialize(int w, int h) override
  {
    w_ = w;
    h_ = h;
  }
  void resize(int w, int h) override
  {
    w_ = w;
    h_ = h;
  }
  void destroy() override
  {
    freeAllGLImages();
    strokes_.clear();
    undoStack_.clear();
    redoStack_.clear();
  }
  void update(double) override {}

private:
  void renderBrushStroke(Canvas2D &ctx, const Stroke &s)
  {
    if (s.pts.empty() && s.imageData.empty())
      return;

    if (!s.imageData.empty() && s.imageW > 0 && s.imageH > 0)
    {
      if (!s.glImage)
      {
        GLuint tex = 0;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, s.imageW, s.imageH, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, s.imageData.data());
        glBindTexture(GL_TEXTURE_2D, 0);
        s.glImage = ctx.wrapTexture(tex, s.imageW, s.imageH);
      }
      if (s.glImage)
        ctx.drawImage(s.glImage, 0.f, 0.f, float(s.imageW), float(s.imageH));
      return;
    }

    Color col = s.eraser ? Color::fromRGB(255, 255, 255) : s.color;
    ctx.setFillColor(col);

    if (s.pts.size() == 1)
    {
      ctx.fillCircle(s.pts[0].x, s.pts[0].y, s.radius);
      return;
    }
    for (size_t i = 1; i < s.pts.size(); ++i)
    {
      float ax = s.pts[i - 1].x, ay = s.pts[i - 1].y;
      float bx = s.pts[i].x, by = s.pts[i].y;
      float dx = bx - ax, dy = by - ay;
      float len = std::sqrtf(dx * dx + dy * dy);
      if (len < 0.5f)
      {
        ctx.fillCircle(ax, ay, s.radius);
        continue;
      }
      float nx = -dy / len * s.radius, ny = dx / len * s.radius;
      ctx.beginPath();
      ctx.moveTo(ax + nx, ay + ny);
      ctx.lineTo(bx + nx, by + ny);
      ctx.lineTo(bx - nx, by - ny);
      ctx.lineTo(ax - nx, ay - ny);
      ctx.closePath();
      ctx.fill();
      ctx.fillCircle(ax, ay, s.radius);
      ctx.fillCircle(bx, by, s.radius);
    }
  }
  void pushUndo()
  {
    undoStack_.push_back(strokes_);
    if (undoStack_.size() > 50)
    {
      freeGLImages(undoStack_.front());
      undoStack_.erase(undoStack_.begin());
    }
    redoStack_.clear();
  }
  void freeGLImages(std::vector<Stroke> &v)
  {
    for (auto &s : v)
      if (s.glImage)
      {
        delete s.glImage;
        s.glImage = nullptr;
      }
  }
  void freeAllGLImages() { freeGLImages(strokes_); }

  std::vector<Stroke> strokes_;
  std::vector<std::vector<Stroke>> undoStack_, redoStack_;
  Stroke *current_ = nullptr;
  bool drawing_ = false;
  int w_ = 512, h_ = 512;
};

// ============================================================
// PaintApp — minimal UI
// ============================================================

class PaintApp : public Widget
{
  std::shared_ptr<CanvasWidget> canvas_;
  std::shared_ptr<PaintSurface> surface_;

  State<int> resizeW_{512}, resizeH_{512};
  State<bool> canUndo_{false}, canRedo_{false};

  std::vector<std::shared_ptr<ButtonWidget>> toolBtns_;

  static constexpr Color kActiveBg = {60, 120, 220, 255};
  static constexpr Color kInactiveBg = {50, 50, 54, 255};
  static constexpr float kSizes[3] = {3.f, 7.f, 16.f};

  void applyResize()
  {
    if (!surface_ || !canvas_)
      return;
    int nw = std::max(1, resizeW_.get());
    int nh = std::max(1, resizeH_.get());

    std::vector<uint8_t> old = surface_->rasterize(surface_->canvasW(), surface_->canvasH());
    int ow = surface_->canvasW(), oh = surface_->canvasH();
    std::vector<uint8_t> px((size_t)nw * nh * 4, 255);
    int cpW = std::min(ow, nw), cpH = std::min(oh, nh);
    for (int y = 0; y < cpH; y++)
      for (int x = 0; x < cpW; x++)
      {
        auto *s = old.data() + (y * ow + x) * 4;
        auto *d = px.data() + (y * nw + x) * 4;
        d[0] = s[0];
        d[1] = s[1];
        d[2] = s[2];
        d[3] = s[3];
      }
    surface_->pushUndo_public();
    surface_->freeAllGLImages_public();
    surface_->strokes_public().clear();

    PaintSurface::Stroke st;
    st.shape = ShapeType::Brush;
    st.imageW = nw;
    st.imageH = nh;
    st.imageData = std::move(px);
    surface_->strokes_public().push_back(std::move(st));
    surface_->resize_public(nw, nh);
    canvas_->setCanvasSize(nw, nh);
    canvas_->viewport().fitToView();
    canvas_->redraw();
  }

public:
  WidgetPtr build() override
  {
    canvas_ = std::make_shared<CanvasWidget>();
    canvas_->setViewportEnabled(true);
    canvas_->setCanvasSize(512, 512);

    canvas_->onViewportChanged = [this](float zoom)
    {
      if (surface_)
        surface_->currentZoom_ = zoom;
    };
    surface_ = canvas_->setSurface<PaintSurface>();

    surface_->onStrokeCommitted = [this]()
    {
      canUndo_.set(surface_->canUndo());
      canRedo_.set(surface_->canRedo());
    };

    std::weak_ptr<PaintSurface> ws = surface_;
    std::weak_ptr<CanvasWidget> wc = canvas_;

    // ── tool buttons ─────────────────────────────────────────
    struct Tool
    {
      const char *lbl;
      ShapeType type;
      bool eraser;
    };
    const std::vector<Tool> tools = {
        {"/", ShapeType::Brush, false},  // brush (pre-selected)
        {"✏", ShapeType::Pencil, false}, // pencil
        {"⬜", ShapeType::Brush, true},  // eraser
    };
    toolBtns_.resize(tools.size());

    auto toolRow = Row({});
    toolRow->setSpacing(4);
    for (int i = 0; i < int(tools.size()); i++)
    {
      auto &t = tools[i];
      auto btn = Button(t.lbl)
                     ->setHeight(28)
                     ->setWidth(36)
                     ->setBackgroundColor(i == 0 ? kActiveBg : kInactiveBg)
                     ->setOnClick([this, ws, i, t]()
                                  {
                    if(auto s=ws.lock()){ s->activeShape_=t.type; s->eraserMode_=t.eraser; }
                    for(int j=0;j<int(toolBtns_.size());j++)
                        if(toolBtns_[j]) toolBtns_[j]->setBackgroundColor(j==i?kActiveBg:kInactiveBg); });
      toolBtns_[i] = btn;
      toolRow->addChild(btn);
    }

    // ── resize inputs ────────────────────────────────────────
    auto wInput = NumberInput(1, 8192, 1)->setValue(resizeW_)->setWidth(104);
    wInput->setOnValueChanged([this](double v)
                              { resizeW_.set((int)v); });
    auto hInput = NumberInput(1, 8192, 1)->setValue(resizeH_)->setWidth(104);
    hInput->setOnValueChanged([this](double v)
                              { resizeH_.set((int)v); });
    auto applyBtn = Button("Apply")->setHeight(26)->setWidth(104)->setOnClick([this]()
                                                                              { applyResize(); });

    // ── sidebar ──────────────────────────────────────────────
    auto label = [](const char *s)
    { return Text(s)->setFontSize(9)->setTextColor(Color::fromRGB(140, 140, 160))->setPaddingLRTB(8, 8, 6, 2); };

    auto sidebar = Container(ScrollView({
                                 label("TOOL"),
                                 toolRow,
                                 SizedBox(0, 4),
                                 label("RESIZE"),
                                 Text("H")->setFontSize(9)->setTextColor(Color::fromRGB(140, 140, 160))->setPaddingLRTB(8, 8, 2, 1),
                                 hInput,
                                 Text("W")->setFontSize(9)->setTextColor(Color::fromRGB(140, 140, 160))->setPaddingLRTB(8, 8, 2, 1),
                                 wInput,
                                 SizedBox(0, 2),
                                 applyBtn,
                                 SizedBox(0, 8),
                             }))
                       ->setWidth(120)
                       ->setBackgroundColor(Color::fromRGB(28, 28, 30));

    return Scaffold(
        nullptr,
        Expanded(Row({sidebar, Expanded(canvas_)})),
        nullptr, nullptr);
  }
};

WidgetPtr createApp(FluxUI *app)
{
  return FluxApp(
      "Paint",
      std::make_shared<PaintApp>(),
      AppTheme::dark(),
      false,
      960, 680,
      false, true);
}