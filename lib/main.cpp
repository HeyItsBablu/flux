#pragma once
#include "flux/flux.hpp"

#include <cmath>
#include <vector>
#include <string>
#include <queue>
#include <functional>

#include <stb_image_write.h>
#include <stb_image.h>
#include <stb_truetype.h>

#include <fstream>
#include <iterator>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ============================================================
// ShapeType
// ============================================================

enum class ShapeType
{
  Brush = 0,
  Pencil,
  Text,
  Select
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
    std::vector<uint8_t> imageData; // for images: RGBA pixels; for text: UTF-8 bytes (imageH==0)
    int imageW = 0, imageH = 0;     // for images: dimensions; for text: strlen, 0
    mutable Canvas2DImage *glImage = nullptr;
  };

  // ── Text edit state ───────────────────────────────────────────────────────
  struct TextEdit
  {
    float x = 0, y = 0;    // canvas baseline-left origin
    std::string text;      // current typed buffer
    float fontSize = 18.f; // in canvas pixels
    double cursorBlinkTimer = 0;
    bool cursorVisible = true;
  };

  Color activeColor_ = Color::fromRGB(30, 144, 255);
  float brushRadius_ = 6.f;
  float currentZoom_ = 1.f;
  bool eraserMode_ = false;
  ShapeType activeShape_ = ShapeType::Brush;

  bool textEditing_ = false;
  TextEdit textEdit_;

  // ── Selection tool state ───────────────────────────────────────────────
  bool hasSelection_ = false;
  float selX_ = 0, selY_ = 0;                   // top-left in canvas coords
  float selW_ = 0, selH_ = 0;                   // width / height
  bool selDragging_ = false;                    // currently drawing the rect
  bool selMoving_ = false;                      // dragging the contents
  float selMoveStartX_ = 0, selMoveStartY_ = 0; // mouse anchor
  float selMoveOrigX_ = 0, selMoveOrigY_ = 0;   // sel origin at drag start
  std::vector<uint8_t> selPixels_;              // lifted RGBA pixels
  int selPixW_ = 0, selPixH_ = 0;
  double selMarchTimer_ = 0.0;
  float selMarchOffset_ = 0.f;

  // Mouse position in canvas coords (for status bar)
  float mouseCanvasX_ = 0.f;
  float mouseCanvasY_ = 0.f;

  // callbacks
  std::function<void()> onStrokeCommitted;
  std::function<void(int)> onKeyDownCallback;
  std::function<void(float, float)> onMousePosChanged;

  // ── Undo / Redo ───────────────────────────────────────────────────────────
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
    textEditing_ = false;
    textEdit_.text.clear();
  }

  // ── Commit current text edit into a stroke ────────────────────────────────
  // Returns true if a stroke was committed.
  bool commitTextEdit()
  {
    if (!textEditing_)
      return false;

    if (!textEdit_.text.empty())
    {
      Stroke s;
      s.shape = ShapeType::Text;
      s.color = activeColor_;
      s.radius = textEdit_.fontSize;
      s.eraser = false;
      s.pts.push_back({textEdit_.x, textEdit_.y});
      // Pack string into imageData; mark as text by setting imageH = 0, imageW = strlen
      s.imageW = int(textEdit_.text.size());
      s.imageH = 0;
      s.imageData.assign(textEdit_.text.begin(), textEdit_.text.end());
      strokes_.push_back(std::move(s));
      if (onStrokeCommitted)
        onStrokeCommitted();
    }

    textEditing_ = false;
    textEdit_.text.clear();
    return true;
  }

  // ── Commit a moved selection back onto the canvas ─────────────────────
  void commitSelection()
  {
    if (!hasSelection_ || selPixels_.empty())
    {
      hasSelection_ = false;
      return;
    }

    std::vector<uint8_t> base = rasterize(w_, h_);

    int dx = int(selX_), dy = int(selY_);
    for (int py = 0; py < selPixH_; ++py)
    {
      for (int px = 0; px < selPixW_; ++px)
      {
        int cx = dx + px, cy = dy + py;
        if (cx < 0 || cy < 0 || cx >= w_ || cy >= h_)
          continue;
        const uint8_t *src = selPixels_.data() + (py * selPixW_ + px) * 4;
        uint8_t *dst = base.data() + (cy * w_ + cx) * 4;
        float a = src[3] / 255.f;
        dst[0] = uint8_t(dst[0] * (1 - a) + src[0] * a);
        dst[1] = uint8_t(dst[1] * (1 - a) + src[1] * a);
        dst[2] = uint8_t(dst[2] * (1 - a) + src[2] * a);
        dst[3] = 255;
      }
    }

    pushUndo();
    redoStack_.clear();
    freeAllGLImages();
    strokes_.clear();

    Stroke s;
    s.shape = ShapeType::Brush;
    s.color = Color::fromRGB(0, 0, 0);
    s.radius = 0.f;
    s.eraser = false;
    s.imageW = w_;
    s.imageH = h_;
    s.imageData = std::move(base);
    strokes_.push_back(std::move(s));

    selPixels_.clear();
    hasSelection_ = false;
    selMoving_ = false;
    selDragging_ = false;
    if (onStrokeCommitted)
      onStrokeCommitted();
  }

  // ── Save ──────────────────────────────────────────────────────────────────
  bool saveToFile(const std::string &path, int w, int h)
  {
    commitTextEdit();
    std::vector<uint8_t> pixels = rasterize(w, h);

    std::string lpath = path;
    for (char &c : lpath)
      c = char(tolower(c));

    bool isJpg = (lpath.size() >= 4 &&
                  (lpath.substr(lpath.size() - 4) == ".jpg" ||
                   (lpath.size() >= 5 && lpath.substr(lpath.size() - 5) == ".jpeg")));

    if (isJpg)
    {
      const int npx = w * h;
      std::vector<uint8_t> rgb(size_t(npx) * 3);
      for (int i = 0; i < npx; i++)
      {
        rgb[i * 3 + 0] = pixels[i * 4 + 0];
        rgb[i * 3 + 1] = pixels[i * 4 + 1];
        rgb[i * 3 + 2] = pixels[i * 4 + 2];
      }
      return stbi_write_jpg(path.c_str(), w, h, 3, rgb.data(), 92) != 0;
    }
    else
    {
      return stbi_write_png(path.c_str(), w, h, 4, pixels.data(), w * 4) != 0;
    }
  }

  // ── Open image file ───────────────────────────────────────────────────
  bool loadImageFile(const std::string &path)
  {
    int imgW, imgH, ch;
    unsigned char *data = stbi_load(path.c_str(), &imgW, &imgH, &ch, 4);
    if (!data)
      return false;

    pushUndo();

    Stroke s;
    s.shape = ShapeType::Brush;
    s.color = Color::fromRGB(0, 0, 0);
    s.radius = 0.f;
    s.eraser = false;
    s.imageW = imgW;
    s.imageH = imgH;
    s.imageData.assign(data, data + imgW * imgH * 4);
    stbi_image_free(data);

    strokes_.insert(strokes_.begin(), std::move(s));
    return true;
  }

  // ── Rasterize to RGBA pixels (for export / resize) ───────────────────────
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

    for (auto &s : strokes_)
    {
      if (s.pts.empty() && s.imageData.empty())
        continue;

      // ── Pasted / resized image ────────────────────────────────────────
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

      // ── Text stroke (imageH == 0, imageData holds UTF-8) ─────────────
      if (s.shape == ShapeType::Text && !s.pts.empty() && !s.imageData.empty() && s.imageH == 0)
      {
        std::string txt(s.imageData.begin(), s.imageData.end());
        float fs = s.radius; // font size in pixels
        float tx = s.pts[0].x;
        float ty = s.pts[0].y; // baseline-bottom (matches Canvas2D TextBaseline::Bottom + 2)
        Color col = s.color;

        static std::vector<uint8_t> s_fontBuf;
        static stbtt_fontinfo s_font;
        static bool s_fontLoaded = false;
        static bool s_fontTried = false;

        if (!s_fontTried)
        {
          s_fontTried = true;
          const char *candidates[] = {
              // Windows
              "C:/Windows/Fonts/arial.ttf",
              "C:/Windows/Fonts/segoeui.ttf",
              "C:/Windows/Fonts/verdana.ttf",
              // macOS
              "/System/Library/Fonts/Helvetica.ttc",
              "/System/Library/Fonts/Arial.ttf",
              "/Library/Fonts/Arial.ttf",
              // Linux
              "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
              "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
              "/usr/share/fonts/truetype/freefont/FreeSans.ttf",
              "/usr/share/fonts/TTF/DejaVuSans.ttf",
              nullptr};
          for (int ci = 0; candidates[ci]; ci++)
          {
            std::ifstream f(candidates[ci], std::ios::binary);
            if (!f)
              continue;
            s_fontBuf.assign(std::istreambuf_iterator<char>(f),
                             std::istreambuf_iterator<char>());
            if (stbtt_InitFont(&s_font, s_fontBuf.data(), 0))
            {
              s_fontLoaded = true;
              break;
            }
            s_fontBuf.clear();
          }
        }

        if (s_fontLoaded)
        {
          // ── stb_truetype path ─────────────────────────────────────────
          float scale = stbtt_ScaleForPixelHeight(&s_font, fs);
          int ascent, descent, lineGap;
          stbtt_GetFontVMetrics(&s_font, &ascent, &descent, &lineGap);
          // ty is baseline-bottom; convert to true baseline (ascent from top)
          float baseline = ty - 2.f; // matches the +2 offset used in renderStroke

          float cursorX = tx;
          for (unsigned char ch : txt)
          {
            if (ch < 32)
            {
              cursorX += fs * 0.3f;
              continue;
            }

            int glyph = stbtt_FindGlyphIndex(&s_font, ch);

            int bx0, by0, bx1, by1;
            stbtt_GetGlyphBitmapBox(&s_font, glyph, scale, scale,
                                    &bx0, &by0, &bx1, &by1);
            int gw = bx1 - bx0, gh = by1 - by0;
            if (gw > 0 && gh > 0)
            {
              std::vector<uint8_t> gbuf(size_t(gw) * gh);
              stbtt_MakeGlyphBitmap(&s_font, gbuf.data(), gw, gh, gw,
                                    scale, scale, glyph);

              int dstX = int(cursorX) + bx0;
              int dstY = int(baseline) + by0; // by0 is negative (above baseline)

              for (int gy = 0; gy < gh; gy++)
                for (int gx = 0; gx < gw; gx++)
                {
                  uint8_t alpha = gbuf[gy * gw + gx];
                  if (alpha == 0)
                    continue;
                  // Blend glyph pixel with the text colour
                  Color gc = col;
                  gc.a = alpha;
                  setPixel(dstX + gx, dstY + gy, gc);
                }
            }

            int advanceWidth, leftBearing;
            stbtt_GetGlyphHMetrics(&s_font, glyph, &advanceWidth, &leftBearing);
            cursorX += advanceWidth * scale;

            // Kern with next character
            if (&ch != (unsigned char *)txt.data() + txt.size() - 1)
            {
              int nextCh = *((&ch) + 1);
              cursorX += stbtt_GetCodepointKernAdvance(&s_font, ch, nextCh) * scale;
            }
          }
        }
        else
        {
          // ── Fallback: 5×7 pixel font drawn as tiny dots ───────────────

          static const uint8_t kFont5x7[][7] = {
              {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 32 space
              {0x04, 0x04, 0x04, 0x04, 0x00, 0x04, 0x00}, // 33 !
              {0x0A, 0x0A, 0x00, 0x00, 0x00, 0x00, 0x00}, // 34 "
              {0x0A, 0x1F, 0x0A, 0x0A, 0x1F, 0x0A, 0x00}, // 35 #
              {0x04, 0x0F, 0x14, 0x0E, 0x05, 0x1E, 0x04}, // 36 $
              {0x18, 0x19, 0x02, 0x04, 0x08, 0x13, 0x03}, // 37 %
              {0x0C, 0x12, 0x14, 0x08, 0x15, 0x12, 0x0D}, // 38 &
              {0x04, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00}, // 39 '
              {0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02}, // 40 (
              {0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08}, // 41 )
              {0x00, 0x04, 0x15, 0x0E, 0x15, 0x04, 0x00}, // 42 *
              {0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00}, // 43 +
              {0x00, 0x00, 0x00, 0x00, 0x06, 0x04, 0x08}, // 44 ,
              {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00}, // 45 -
              {0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x00}, // 46 .
              {0x01, 0x01, 0x02, 0x04, 0x08, 0x10, 0x10}, // 47 /
              {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}, // 48 0
              {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}, // 49 1
              {0x0E, 0x11, 0x01, 0x06, 0x08, 0x10, 0x1F}, // 50 2
              {0x1F, 0x01, 0x02, 0x06, 0x01, 0x11, 0x0E}, // 51 3
              {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}, // 52 4
              {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E}, // 53 5
              {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}, // 54 6
              {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}, // 55 7
              {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}, // 56 8
              {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C}, // 57 9
              {0x00, 0x06, 0x00, 0x00, 0x06, 0x00, 0x00}, // 58 :
              {0x00, 0x06, 0x00, 0x00, 0x06, 0x04, 0x08}, // 59 ;
              {0x02, 0x04, 0x08, 0x10, 0x08, 0x04, 0x02}, // 60 <
              {0x00, 0x00, 0x1F, 0x00, 0x1F, 0x00, 0x00}, // 61 =
              {0x08, 0x04, 0x02, 0x01, 0x02, 0x04, 0x08}, // 62 >
              {0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04}, // 63 ?
              {0x0E, 0x11, 0x01, 0x0D, 0x15, 0x15, 0x0E}, // 64 @
              {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}, // 65 A
              {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E}, // 66 B
              {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}, // 67 C
              {0x1C, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1C}, // 68 D
              {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}, // 69 E
              {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10}, // 70 F
              {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F}, // 71 G
              {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}, // 72 H
              {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E}, // 73 I
              {0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C}, // 74 J
              {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}, // 75 K
              {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}, // 76 L
              {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}, // 77 M
              {0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11}, // 78 N
              {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}, // 79 O
              {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}, // 80 P
              {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D}, // 81 Q
              {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}, // 82 R
              {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}, // 83 S
              {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}, // 84 T
              {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}, // 85 U
              {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04}, // 86 V
              {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A}, // 87 W
              {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11}, // 88 X
              {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04}, // 89 Y
              {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F}, // 90 Z
              {0x0E, 0x08, 0x08, 0x08, 0x08, 0x08, 0x0E}, // 91 [
              {0x10, 0x10, 0x08, 0x04, 0x02, 0x01, 0x01}, // 92 backslash
              {0x0E, 0x02, 0x02, 0x02, 0x02, 0x02, 0x0E}, // 93 ]
              {0x04, 0x0A, 0x11, 0x00, 0x00, 0x00, 0x00}, // 94 ^
              {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F}, // 95 _
              {0x08, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00}, // 96 `
              {0x00, 0x00, 0x0E, 0x01, 0x0F, 0x11, 0x0F}, // 97 a
              {0x10, 0x10, 0x1E, 0x11, 0x11, 0x11, 0x1E}, // 98 b
              {0x00, 0x00, 0x0E, 0x10, 0x10, 0x11, 0x0E}, // 99 c
              {0x01, 0x01, 0x0F, 0x11, 0x11, 0x11, 0x0F}, // 100 d
              {0x00, 0x00, 0x0E, 0x11, 0x1F, 0x10, 0x0E}, // 101 e
              {0x06, 0x09, 0x08, 0x1C, 0x08, 0x08, 0x08}, // 102 f
              {0x00, 0x00, 0x0F, 0x11, 0x0F, 0x01, 0x0E}, // 103 g
              {0x10, 0x10, 0x16, 0x19, 0x11, 0x11, 0x11}, // 104 h
              {0x04, 0x00, 0x0C, 0x04, 0x04, 0x04, 0x0E}, // 105 i
              {0x02, 0x00, 0x06, 0x02, 0x02, 0x12, 0x0C}, // 106 j
              {0x10, 0x10, 0x12, 0x14, 0x18, 0x14, 0x12}, // 107 k
              {0x0C, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E}, // 108 l
              {0x00, 0x00, 0x1A, 0x15, 0x15, 0x11, 0x11}, // 109 m
              {0x00, 0x00, 0x16, 0x19, 0x11, 0x11, 0x11}, // 110 n
              {0x00, 0x00, 0x0E, 0x11, 0x11, 0x11, 0x0E}, // 111 o
              {0x00, 0x00, 0x1E, 0x11, 0x1E, 0x10, 0x10}, // 112 p
              {0x00, 0x00, 0x0F, 0x11, 0x0F, 0x01, 0x01}, // 113 q
              {0x00, 0x00, 0x16, 0x19, 0x10, 0x10, 0x10}, // 114 r
              {0x00, 0x00, 0x0E, 0x10, 0x0E, 0x01, 0x1E}, // 115 s
              {0x08, 0x08, 0x1C, 0x08, 0x08, 0x09, 0x06}, // 116 t
              {0x00, 0x00, 0x11, 0x11, 0x11, 0x13, 0x0D}, // 117 u
              {0x00, 0x00, 0x11, 0x11, 0x11, 0x0A, 0x04}, // 118 v
              {0x00, 0x00, 0x11, 0x15, 0x15, 0x15, 0x0A}, // 119 w
              {0x00, 0x00, 0x11, 0x0A, 0x04, 0x0A, 0x11}, // 120 x
              {0x00, 0x00, 0x11, 0x11, 0x0F, 0x01, 0x0E}, // 121 y
              {0x00, 0x00, 0x1F, 0x02, 0x04, 0x08, 0x1F}, // 122 z
              {0x06, 0x08, 0x08, 0x18, 0x08, 0x08, 0x06}, // 123 {
              {0x04, 0x04, 0x04, 0x00, 0x04, 0x04, 0x04}, // 124 |
              {0x0C, 0x02, 0x02, 0x03, 0x02, 0x02, 0x0C}, // 125 }
              {0x08, 0x15, 0x02, 0x00, 0x00, 0x00, 0x00}, // 126 ~
          };

          // Scale the 5×7 bitmap to match the requested font size.
          // Each pixel of the glyph maps to a (pixScale × pixScale) block.
          float pixScaleF = fs / 9.f; // 7 rows + 2 leading ≈ 9 units tall
          int pixScale = std::max(1, int(pixScaleF + 0.5f));
          float charW = (5 * pixScale) + pixScale; // 5 cols + 1 gap
          float baseline = ty - 2.f;

          float cx = tx;
          for (unsigned char ch : txt)
          {
            if (ch < 32 || ch > 126)
            {
              cx += charW;
              continue;
            }
            const uint8_t *glyph = kFont5x7[ch - 32];
            int topY = int(baseline) - 7 * pixScale;
            for (int row = 0; row < 7; row++)
              for (int gc = 0; gc < 5; gc++)
                if (glyph[row] & (0x10 >> gc))
                  for (int dy = 0; dy < pixScale; dy++)
                    for (int dx = 0; dx < pixScale; dx++)
                      setPixel(int(cx) + gc * pixScale + dx,
                               topY + row * pixScale + dy, col);
            cx += charW;
          }
        }
        continue;
      }

      // ── Brush / Pencil / Eraser ───────────────────────────────────────
      Color col = s.eraser ? Color::fromRGB(255, 255, 255) : s.color;

      switch (s.shape)
      {
      case ShapeType::Brush:
      case ShapeType::Pencil:
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

  // ── Mouse events ──────────────────────────────────────────────────────────
  void onMouseDown(float x, float y) override
  {
    mouseCanvasX_ = x;
    mouseCanvasY_ = y;
    if (onMousePosChanged)
      onMousePosChanged(x, y);

    if (activeShape_ == ShapeType::Text)
    {
      // Commit any in-progress text, then open a new cursor at click pos.
      if (textEditing_)
        commitTextEdit();
      pushUndo();
      textEditing_ = true;
      textEdit_.x = x;
      textEdit_.y = y;
      textEdit_.text = "";
      textEdit_.fontSize = std::max(8.f, brushRadius_ * 2.5f);
      textEdit_.cursorBlinkTimer = 0.0;
      textEdit_.cursorVisible = true;
      return;
    }

    if (activeShape_ == ShapeType::Select)
    {
      if (hasSelection_ && !selPixels_.empty() &&
          x >= selX_ && x < selX_ + selW_ &&
          y >= selY_ && y < selY_ + selH_)
      {
        selMoving_ = true;
        selMoveStartX_ = x;
        selMoveStartY_ = y;
        selMoveOrigX_ = selX_;
        selMoveOrigY_ = selY_;
      }
      else
      {
        if (hasSelection_)
          commitSelection();
        selDragging_ = true;
        selMoving_ = false;
        hasSelection_ = false;
        selX_ = x;
        selY_ = y;
        selW_ = 0;
        selH_ = 0;
      }
      return;
    }
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
    mouseCanvasX_ = x;
    mouseCanvasY_ = y;
    if (onMousePosChanged)
      onMousePosChanged(x, y);

    if (activeShape_ == ShapeType::Text)
      return; // no drag in text mode

    if (activeShape_ == ShapeType::Select)
    {
      if (selDragging_)
      {
        selW_ = x - selX_;
        selH_ = y - selY_;
      }
      else if (selMoving_)
      {
        selX_ = selMoveOrigX_ + (x - selMoveStartX_);
        selY_ = selMoveOrigY_ + (y - selMoveStartY_);
        selX_ = std::max(float(-(selPixW_ / 2)), std::min(selX_, float(w_)));
        selY_ = std::max(float(-(selPixH_ / 2)), std::min(selY_, float(h_)));
      }
      return;
    }

    if (!drawing_ || !current_)
      return;
    x = std::max(0.f, std::min(x, float(w_)));
    y = std::max(0.f, std::min(y, float(h_)));
    current_->pts.push_back({x, y});
  }

  void onMouseUp(float x, float y) override
  {
    if (activeShape_ == ShapeType::Text)
      return; // handled entirely in onMouseDown

    if (activeShape_ == ShapeType::Select)
    {
      if (selDragging_)
      {
        selDragging_ = false;
        if (selW_ < 0)
        {
          selX_ += selW_;
          selW_ = -selW_;
        }
        if (selH_ < 0)
        {
          selY_ += selH_;
          selH_ = -selH_;
        }
        if (selW_ >= 2 && selH_ >= 2)
        {
          std::vector<uint8_t> base = rasterize(w_, h_);
          int ix = int(selX_), iy = int(selY_);
          int iw = int(selW_), ih = int(selH_);
          iw = std::min(iw, w_ - ix);
          ih = std::min(ih, h_ - iy);
          if (ix < 0)
          {
            iw += ix;
            ix = 0;
          }
          if (iy < 0)
          {
            ih += iy;
            iy = 0;
          }
          selPixW_ = iw;
          selPixH_ = ih;
          selPixels_.resize(size_t(iw) * ih * 4);
          for (int py = 0; py < ih; ++py)
            for (int px = 0; px < iw; ++px)
            {
              const uint8_t *src = base.data() + ((iy + py) * w_ + (ix + px)) * 4;
              uint8_t *dst = selPixels_.data() + (py * iw + px) * 4;
              dst[0] = src[0];
              dst[1] = src[1];
              dst[2] = src[2];
              dst[3] = src[3];
              uint8_t *hole = base.data() + ((iy + py) * w_ + (ix + px)) * 4;
              hole[0] = 255;
              hole[1] = 255;
              hole[2] = 255;
              hole[3] = 255;
            }
          pushUndo();
          freeAllGLImages();
          strokes_.clear();
          Stroke s;
          s.shape = ShapeType::Brush;
          s.color = Color::fromRGB(0, 0, 0);
          s.radius = 0.f;
          s.eraser = false;
          s.imageW = w_;
          s.imageH = h_;
          s.imageData = std::move(base);
          strokes_.push_back(std::move(s));
          selX_ = float(ix);
          selY_ = float(iy);
          selW_ = float(iw);
          selH_ = float(ih);
          hasSelection_ = true;
        }
        else
        {
          hasSelection_ = false;
        }
      }
      else if (selMoving_)
      {
        selMoving_ = false;
      }
      return;
    }

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

  // ── Keyboard ──────────────────────────────────────────────────────────────
  void onKeyDown(const KeyEvent &e) override
  {
    if (textEditing_)
    {
      if (e.codepoint >= 32 && e.codepoint != 127)
      {
        textEdit_.text += char(e.codepoint);
        textEdit_.cursorVisible = true;
        textEdit_.cursorBlinkTimer = 0.0;
      }
      else
      {
        switch (e.virtualKey)
        {
        case Key::Backspace:
          if (!textEdit_.text.empty())
            textEdit_.text.pop_back();
          textEdit_.cursorVisible = true;
          textEdit_.cursorBlinkTimer = 0.0;
          break;
        case Key::Return:
          commitTextEdit();
          break;
        case Key::Escape:
          textEditing_ = false;
          textEdit_.text.clear();
          break;
        default:
          break;
        }
      }
      return;
    }

    if (onKeyDownCallback)
      onKeyDownCallback(e.virtualKey);
  }

  void onKeyUp(const KeyEvent &) override {}

  // ── Render ────────────────────────────────────────────────────────────────
  void render(Canvas2D &ctx) override
  {
    ctx.setFillColor(Color::fromRGB(255, 255, 255));
    ctx.fillRect(0, 0, float(w_), float(h_));

    for (auto &s : strokes_)
      renderStroke(ctx, s);

    if (current_)
      renderStroke(ctx, *current_);

    // ── Live text-edit preview ────────────────────────────────────────────
    if (textEditing_)
    {
      float fs = textEdit_.fontSize;
      float tx = textEdit_.x;
      float ty = textEdit_.y;

      // Soft blue highlight behind the active text area
      float previewW = std::max(fs * 3.f, ctx.measureText(textEdit_.text) + fs * 1.2f);
      ctx.setFillColor(Color::fromRGBA(30, 144, 255, 18));
      ctx.fillRoundedRect(tx - 3.f, ty - fs - 4.f, previewW, fs + 10.f, 3.f);

      // Thin underline showing the baseline
      ctx.setFillColor(Color::fromRGBA(30, 144, 255, 60));
      ctx.fillRect(tx - 2.f, ty + 3.f, previewW, 1.f);

      // Typed text
      ctx.setFillColor(activeColor_);
      char fontDesc[48];
      std::snprintf(fontDesc, sizeof(fontDesc), "%.0fpx sans", fs);
      ctx.setFont(fontDesc);
      ctx.setTextBaseline(TextBaseline::Bottom);
      ctx.fillText(textEdit_.text, tx, ty + 2.f);

      // Blinking cursor — drawn after measureText so it sits right of the last char
      if (textEdit_.cursorVisible)
      {
        float tw = ctx.measureText(textEdit_.text);
        float cursorH = fs + 4.f;
        ctx.setFillColor(activeColor_);
        ctx.fillRect(tx + tw + 1.f, ty - fs - 1.f, 1.5f, cursorH);
      }
    }

    // ── Selection overlay ─────────────────────────────────────────────
    if (activeShape_ == ShapeType::Select)
    {
      // Draw floating selection pixels
      if (hasSelection_ && !selPixels_.empty())
      {
        // // Upload as a temporary GL texture each frame
        // // (cheap for selection-size tiles; could cache)
        // GLuint tmpTex = 0;
        // glGenTextures(1, &tmpTex);
        // glBindTexture(GL_TEXTURE_2D, tmpTex);
        // glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        // glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        // glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        // glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        // glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, selPixW_, selPixH_, 0,
        //              GL_RGBA, GL_UNSIGNED_BYTE, selPixels_.data());
        // glBindTexture(GL_TEXTURE_2D, 0);
        // Canvas2DImage *tmp = ctx.wrapTexture(tmpTex, selPixW_, selPixH_);
        // ctx.drawImage(tmp, selX_, selY_, selW_, selH_);
        // delete tmp;
        // glDeleteTextures(1, &tmpTex);
      }

      // Marching-ants dashed border
      float rx = hasSelection_ ? selX_ : selX_;
      float ry = hasSelection_ ? selY_ : selY_;
      float rw2 = (selDragging_ && selW_ < 0) ? -selW_ : std::abs(selW_);
      float rh2 = (selDragging_ && selH_ < 0) ? -selH_ : std::abs(selH_);
      float rx0 = selDragging_ && selW_ < 0 ? selX_ + selW_ : selX_;
      float ry0 = selDragging_ && selH_ < 0 ? selY_ + selH_ : selY_;

      if (rw2 > 0 && rh2 > 0)
      {
        // White base line
        ctx.setStrokeColor(Color::fromRGB(255, 255, 255));
        ctx.setLineWidth(1.f);
        ctx.strokeRect(rx0, ry0, rw2, rh2);

        // Black dashes offset by march timer
        ctx.setStrokeColor(Color::fromRGB(0, 0, 0));
        float dash = 6.f;
        float off = selMarchOffset_;

        std::function<void(float, float, float, float)> drawDashedLine =
            [&](float ax2, float ay2, float bx2, float by2)
        {
          float edx = bx2 - ax2, edy = by2 - ay2;
          float elen = std::sqrtf(edx * edx + edy * edy);
          if (elen < 0.5f)
            return;
          float ux2 = edx / elen, uy2 = edy / elen;
          float t = -std::fmod(off, dash * 2.f);
          while (t < elen)
          {
            float segStart = std::max(t, 0.f);
            float segEnd = std::min(t + dash, elen);
            if (segStart < segEnd)
            {
              ctx.beginPath();
              ctx.moveTo(ax2 + ux2 * segStart, ay2 + uy2 * segStart);
              ctx.lineTo(ax2 + ux2 * segEnd, ay2 + uy2 * segEnd);
              ctx.stroke();
            }
            t += dash * 2.f;
          }
        };
        drawDashedLine(rx0, ry0, rx0 + rw2, ry0);
        drawDashedLine(rx0 + rw2, ry0, rx0 + rw2, ry0 + rh2);
        drawDashedLine(rx0 + rw2, ry0 + rh2, rx0, ry0 + rh2);
        drawDashedLine(rx0, ry0 + rh2, rx0, ry0);
      }
    }
  }

  // ── Update (drives cursor blink and continuous repaint while editing) ─────
  void update(double dt) override
  {
    if (textEditing_)
    {
      textEdit_.cursorBlinkTimer += dt;
      if (textEdit_.cursorBlinkTimer >= 0.53)
      {
        textEdit_.cursorBlinkTimer = 0.0;
        textEdit_.cursorVisible = !textEdit_.cursorVisible;
      }
    }

    if (hasSelection_ || selDragging_ || selMoving_)
    {
      selMarchTimer_ += dt;
      if (selMarchTimer_ >= 0.05)
      {
        selMarchTimer_ = 0.0;
        selMarchOffset_ += 1.f;
        if (selMarchOffset_ >= 8.f)
          selMarchOffset_ = 0.f;
      }
    }
  }

  bool needsContinuousRedraw() const override
  {
    return textEditing_ || hasSelection_ || selDragging_ || selMoving_;
  }

  // ── Public helpers (used by PaintApp) ─────────────────────────────────────
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

  // ── RenderSurface overrides ───────────────────────────────────────────────
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
    textEditing_ = false;
    textEdit_.text.clear();
  }

private:
  // ── Render a single committed stroke ─────────────────────────────────────
  void renderStroke(Canvas2D &ctx, const Stroke &s)
  {
    if (s.pts.empty() && s.imageData.empty())
      return;

    // ── Pasted / resized image (imageH > 0) ──────────────────────────────
    if (!s.imageData.empty() && s.imageW > 0 && s.imageH > 0)
    {
      if (!s.glImage)
      {
        // GLuint tex = 0;
        // glGenTextures(1, &tex);
        // glBindTexture(GL_TEXTURE_2D, tex);
        // glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        // glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        // glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        // glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        // glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, s.imageW, s.imageH, 0,
        //              GL_RGBA, GL_UNSIGNED_BYTE, s.imageData.data());
        // glBindTexture(GL_TEXTURE_2D, 0);
        // s.glImage = ctx.wrapTexture(tex, s.imageW, s.imageH);
      }
      if (s.glImage)
        ctx.drawImage(s.glImage, 0.f, 0.f, float(s.imageW), float(s.imageH));
      return;
    }

    // ── Text stroke (imageH == 0, imageData holds UTF-8 bytes) ───────────
    if (s.shape == ShapeType::Text && !s.pts.empty() && !s.imageData.empty() && s.imageH == 0)
    {
      std::string txt(s.imageData.begin(), s.imageData.end());
      float fs = s.radius;
      char fontDesc[48];
      std::snprintf(fontDesc, sizeof(fontDesc), "%.0fpx sans", fs);
      ctx.setFont(fontDesc);
      ctx.setTextBaseline(TextBaseline::Bottom);
      ctx.setFillColor(s.color);
      ctx.fillText(txt, s.pts[0].x, s.pts[0].y + 2.f);
      return;
    }

    // ── Brush / Pencil / Eraser ───────────────────────────────────────────
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

  // ── Undo stack management ─────────────────────────────────────────────────
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

  // ── State ─────────────────────────────────────────────────────────────────
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
  std::shared_ptr<ColorPickerWidget> colorPicker_;

  State<int> resizeW_{512}, resizeH_{512};
  State<bool> canUndo_{false}, canRedo_{false};
  State<int> selectedShape_{0};
  State<int> selectedColor_{0};

  State<std::string> cursorPosLabel_{"0, 0"};
  State<std::string> canvasSizeLabel_{"512 × 512"};

  State<std::string> zoomLabel_{"100%"};
  State<bool> eraserOn_{false};

  std::vector<std::shared_ptr<ButtonWidget>> toolBtns_;

  std::vector<std::shared_ptr<ButtonWidget>> shapeButtons_;

  std::shared_ptr<SliderWidget> brushSlider_;
  std::shared_ptr<FilePickerWidget> activePicker_;

  static constexpr Color kActiveBg = {60, 120, 220, 255};
  static constexpr Color kInactiveBg = {50, 50, 54, 255};

  void updateCanvasSizeLabel()
  {
    if (!surface_)
      return;
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%d × %d",
                  surface_->canvasW(), surface_->canvasH());
    canvasSizeLabel_.set(buf);
  }

  // ── Resize canvas ─────────────────────────────────────────────────────────
  void applyResize()
  {
    if (!surface_ || !canvas_)
      return;
    int nw = std::max(1, resizeW_.get());
    int nh = std::max(1, resizeH_.get());

    std::vector<uint8_t> old = surface_->rasterize(surface_->canvasW(), surface_->canvasH());
    int ow = surface_->canvasW(), oh = surface_->canvasH();
    std::vector<uint8_t> px(size_t(nw) * nh * 4, 255);
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

  void refreshUndoState()
  {
    if (surface_)
    {
      canUndo_.set(surface_->canUndo());
      canRedo_.set(surface_->canRedo());
    }
  }

  void highlightShapeButton(int idx)
  {
    for (int i = 0; i < int(shapeButtons_.size()); i++)
      if (shapeButtons_[i])
        shapeButtons_[i]->setBackgroundColor(i == idx ? kActiveBg : kInactiveBg);
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
      char buf[16];
      std::snprintf(buf, sizeof(buf), "%.0f%%", zoom * 100.f);
      zoomLabel_.set(buf);
    };

    surface_ = canvas_->setSurface<PaintSurface>();
    surface_->brushRadius_ = 7.f;

    surface_->onMousePosChanged = [this](float x, float y)
    {
      char buf[32];
      std::snprintf(buf, sizeof(buf), "%.0f, %.0f", x, y);
      cursorPosLabel_.set(buf);
    };

    surface_->onStrokeCommitted = [this]()
    {
      canUndo_.set(surface_->canUndo());
      canRedo_.set(surface_->canRedo());
      canvas_->redraw();
    };

    std::weak_ptr<PaintSurface> ws = surface_;
    std::weak_ptr<CanvasWidget> wc = canvas_;

    struct ColorSwatch
    {
      Color color;
    };
    const std::vector<ColorSwatch> palette_ = {
        {Color::fromRGB(30, 144, 255)},
        {Color::fromRGB(220, 53, 69)},
        {Color::fromRGB(40, 167, 69)},
        {Color::fromRGB(255, 193, 7)},
        {Color::fromRGB(111, 66, 193)},
        {Color::fromRGB(253, 126, 20)},
        {Color::fromRGB(20, 20, 20)},
        {Color::fromRGB(200, 200, 200)},
        {Color::fromRGB(255, 255, 255)},
    };

    // ── Tool definitions ──────────────────────────────────────────────────
    struct Tool
    {
      const char *label;
      ShapeType type;
      bool eraser;
    };
    const std::vector<Tool> shapeTools_ = {
        {"/", ShapeType::Brush, false},  // brush  (index 0, pre-selected)
        {"✏", ShapeType::Pencil, false}, // pencil (index 1)
        {"⬜", ShapeType::Brush, true},  // eraser (index 2)
        {"T", ShapeType::Text, false},   // text   (index 3)
        {"⬚", ShapeType::Select, false},
    };

    // ── Resize inputs ─────────────────────────────────────────────────────
    auto wInput = NumberInput(1, 8192, 1)->setValue(resizeW_)->setWidth(104);
    wInput->setOnValueChanged([this](double v)
                              { resizeW_.set(int(v)); });
    auto hInput = NumberInput(1, 8192, 1)->setValue(resizeH_)->setWidth(104);
    hInput->setOnValueChanged([this](double v)
                              { resizeH_.set(int(v)); });
    auto applyBtn = Button("Apply")->setHeight(26)->setWidth(104)->setOnClick([this]()
                                                                              { applyResize(); });

    // ── Save / Export ──────────────────────────────────────────────────────
    auto saveLabel = Text("EXPORT")
                         ->setFontSize(9)
                         ->setTextColor(Color::fromRGB(140, 140, 160))
                         ->setPaddingLRTB(8, 8, 8, 4);

    auto savePngBtn = Button("PNG")
                          ->setHeight(26)
                          ->setWidth(50)
                          ->setOnClick([this, ws]()
                                       {
        activePicker_ = FilePicker("Save PNG");
        activePicker_->setMode(FilePickerMode::Save)
                      ->setTitle("Export as PNG")
                      ->setDefaultFilename("painting.png")
                      ->setDefaultExtension("png")
                      ->addFilter("PNG Image", {"*.png"})
                      ->setOnChanged([this, ws](const std::string &path) {
                          if (path.empty()) return;
                          if (auto s = ws.lock())
                              s->saveToFile(path, s->canvasW(), s->canvasH());
                         
                      })
                      ->setOnCancelled([this]() {
                         
                      });
        activePicker_->open(); });

    auto saveJpgBtn = Button("JPG")
                          ->setHeight(26)
                          ->setWidth(50)
                          ->setOnClick([this, ws]()
                                       {
        activePicker_ = FilePicker("Save JPG");
        activePicker_->setMode(FilePickerMode::Save)
                      ->setTitle("Export as JPG")
                      ->setDefaultFilename("painting.jpg")
                      ->setDefaultExtension("jpg")
                      ->addFilter("JPEG Image", {"*.jpg", "*.jpeg"})
                      ->setOnChanged([this, ws](const std::string &path) {
                          if (path.empty()) return;
                          if (auto s = ws.lock())
                              s->saveToFile(path, s->canvasW(), s->canvasH());
                          
                      })
                      ->setOnCancelled([this]() {
                          
                      });
        activePicker_->open(); });

    auto saveRow = Flex({savePngBtn, saveJpgBtn});
    saveRow->setGap(4);

    // ── Sidebar label helper ──────────────────────────────────────────────
    auto label = [](const char *s)
    {
      return Text(s)->setFontSize(9)->setTextColor(Color::fromRGB(140, 140, 160))->setPaddingLRTB(8, 8, 6, 2);
    };

    // ── Shape / tool buttons (2-column grid) ─────────────────────────────
    shapeButtons_.resize(shapeTools_.size());
    auto shapeGrid = Flex({});
    shapeGrid->setGap(3);
    shapeGrid->setPadding(6)->setHeightMode(SizeMode::Fit)->setDirection(FlexDirection::Column);

    for (int row = 0; row < int(shapeTools_.size()); row += 2)
    {
      auto rowW = Flex({});
      rowW->setGap(3);
      for (int col = 0; col < 2 && row + col < int(shapeTools_.size()); ++col)
      {
        int idx = row + col;
        const auto &st = shapeTools_[idx];
        auto btn = Button(st.label)
                       ->setHeight(28)
                       ->setWidth(52)
                       ->setBackgroundColor(idx == 0 ? kActiveBg : kInactiveBg)
                       ->setOnClick([this, ws, wc, idx, st]()
                                    {
            selectedShape_.set(idx);
            eraserOn_.set(st.eraser);

            if (auto s = ws.lock())
            {
              // Commit any open text edit when switching tools
              if (s->textEditing_) { s->commitTextEdit(); }
              s->activeShape_  = st.type;
              s->eraserMode_   = st.eraser;
            }
            highlightShapeButton(idx);
            if (auto c = wc.lock()) c->redraw(); });
        shapeButtons_[idx] = btn;
        rowW->addChild(btn);
      }
      shapeGrid->addChild(rowW);
    }

    // ── Text-size hint label (shown in sidebar under shapes) ─────────────
    auto textHintLabel = Text("Font size tracks brush size")
                             ->setFontSize(8)
                             ->setTextColor(Color::fromRGB(100, 100, 120))
                             ->setPaddingLRTB(8, 8, 2, 4);

    auto openLabel = Text("FILE")
                         ->setFontSize(9)
                         ->setTextColor(Color::fromRGB(140, 140, 160))
                         ->setPaddingLRTB(8, 8, 8, 4);

    auto openBtn = Button("Open")
                       ->setHeight(26)
                       ->setOnClick([this, ws, wc]()
                                    {
                auto picker = FilePicker("Open Image");
                picker->setMode(FilePickerMode::Open)
                       ->setTitle("Open Image")
                       ->addFilter("Images", {"*.png","*.jpg","*.jpeg","*.bmp"})
                       ->addFilter("All files", {"*.*"})
                       ->setOnChanged([this, ws, wc](const std::string &path){
                           if (path.empty()) return;
                           if (auto s = ws.lock()) {
                               s->loadImageFile(path);
                               updateCanvasSizeLabel();
                           }
                           if (auto c = wc.lock()) c->redraw();
                           refreshUndoState();
                       });
                picker->open(); });

    // ── Color label + picker ───────────────────────────────────────────
    auto colorLabel = Text("COLOR")
                          ->setFontSize(9)
                          ->setTextColor(Color::fromRGB(140, 140, 160))
                          ->setPaddingLRTB(8, 8, 8, 4);

    colorPicker_ = ColorPicker(palette_[0].color);
    colorPicker_->pickerSize = 96;
    colorPicker_->hueBarHeight = 12;
    colorPicker_->alphaBarHeight = 12;
    colorPicker_->barSpacing = 5;
    colorPicker_->previewSize = 20;
    colorPicker_->hexInputHeight = 20;
    colorPicker_->paddingLeft = 6;
    colorPicker_->paddingRight = 6;
    colorPicker_->paddingTop = 4;
    colorPicker_->paddingBottom = 4;
    colorPicker_->showAlpha = false;
    colorPicker_->width = colorPicker_->pickerSize +
                          colorPicker_->paddingLeft +
                          colorPicker_->paddingRight;

    colorPicker_->setOnColorChanged([this, ws](Color c)
                                    {
            selectedColor_.set(-1);
            eraserOn_.set(false);
            if (auto s = ws.lock()) {
                s->activeColor_ = c;
                s->eraserMode_  = false;
            } });

    // ── Palette swatches ───────────────────────────────────────────────
    auto swatchGrid = Flex({});
    swatchGrid->setGap(3)->setDirection(FlexDirection::Column);

    for (int row = 0; row < int(palette_.size()); row += 3)
    {
      auto rowW = Flex({});
      rowW->setGap(3);
      for (int col = 0; col < 3 && row + col < int(palette_.size()); ++col)
      {
        int idx = row + col;
        Color color = palette_[idx].color;
        auto btn = Button("")
                       ->setHeight(22)
                       ->setWidth(22)
                       ->setBackgroundColor(color)
                       ->setBorderRadius(11)
                       ->setOnClick([this, ws, idx, color]()
                                    {
                        selectedColor_.set(idx);
                        eraserOn_.set(false);
                        if (auto s = ws.lock()) {
                            s->activeColor_ = color;
                            s->eraserMode_  = false;
                        }
                        if (colorPicker_) colorPicker_->setColor(color); });
        rowW->addChild(btn);
      }
      swatchGrid->addChild(rowW);
    }

    // ── Sidebar assembly ──────────────────────────────────────────────────
    auto sidebar = Flex({
                            Text("SHAPES")
                                ->setFontSize(9)
                                ->setTextColor(Color::fromRGB(140, 140, 160))
                                ->setPaddingLRTB(8, 8, 8, 4),
                            shapeGrid,
                            textHintLabel,
                            SizedBox(0, 4),
                            colorLabel,
                            colorPicker_,
                            SizedBox(0, 4),
                            swatchGrid,
                            SizedBox(0, 6),
                            label("RESIZE"),
                            Text("H")->setFontSize(9)->setTextColor(Color::fromRGB(140, 140, 160))->setPaddingLRTB(8, 8, 2, 1),
                            hInput,
                            Text("W")->setFontSize(9)->setTextColor(Color::fromRGB(140, 140, 160))->setPaddingLRTB(8, 8, 2, 1),
                            wInput,
                            SizedBox(0, 2),
                            applyBtn,

                        })
                       ->setScrollable(true)
                       ->setWidth(120)
                       ->setBackgroundColor(Color::fromRGB(28, 28, 30))
                       ->setDirection(FlexDirection::Column)
                       ->setHeightMode(SizeMode::Full);

    // ── Undo / Redo buttons ───────────────────────────────────────────────
    auto undoBtn = Button("↩")
                       ->setHeight(26)
                       ->setWidth(36)
                       ->setOnClick([this, ws, wc]()
                                    {
        if (auto s = ws.lock()) { s->commitTextEdit(); s->undo(); }
        refreshUndoState();
        if (auto c = wc.lock()) c->redraw(); });

    auto redoBtn = Button("↪")
                       ->setHeight(26)
                       ->setWidth(36)
                       ->setOnClick([this, ws, wc]()
                                    {
        if (auto s = ws.lock()) { s->commitTextEdit(); s->redo(); }
        refreshUndoState();
        if (auto c = wc.lock()) c->redraw(); });

    // ── Eraser button ─────────────────────────────────────────────────────
    auto eraserBtn_ = Button("Eraser")
                          ->setHeight(26)
                          ->setBackgroundColor(kInactiveBg)
                          ->setOnClick([this, ws, wc]()
                                       {
        eraserOn_.set(true);
        selectedShape_.set(2); // eraser is index 2
        if (auto s = ws.lock())
        {
          if (s->textEditing_) s->commitTextEdit();
          s->eraserMode_  = true;
          s->activeShape_ = ShapeType::Brush;
        }
        highlightShapeButton(2);
        if (auto c = wc.lock()) c->redraw(); });

    // ── Clear button ──────────────────────────────────────────────────────
    auto clearBtn = Button("Clear")
                        ->setHeight(26)
                        ->setOnClick([this, ws, wc]()
                                     {
        if (auto s = ws.lock()) s->clear();
        refreshUndoState();
        if (auto c = wc.lock()) c->redraw(); });

    brushSlider_ = Slider(1.0, 30.0, 0.5);
    brushSlider_->value = surface_->brushRadius_;
    brushSlider_->setWidth(120);
    brushSlider_->setOnValueChanged([ws](double v)

                                    {
                                      if (auto s = ws.lock())

                                        s->brushRadius_ = float(v); });

    // ── Toolbar ───────────────────────────────────────────────────────────
    auto toolbar =
        Flex({
                 Text("Paint")
                     ->setFontSize(13)
                     ->setTextColor(Color::fromRGB(220, 220, 220)),
                 SizedBox(8, 0),
                 openBtn,
                 saveRow,
                 brushSlider_,
                 SizedBox(8, 0),
                 eraserBtn_,
                 SizedBox(8, 0),
                 clearBtn,
                 SizedBox(8, 0),
                 undoBtn,
                 SizedBox(8, 0),
                 redoBtn,
             })
            ->setPadding(8)
            ->setHeight(44)
            ->setBackgroundColor(Color::fromRGB(28, 28, 30));

    // ── Status bar ────────────────────────────────────────────────────────
    auto statusBar =
        Flex({
                 Text(cursorPosLabel_, [](const std::string &s)
                      { return s; })
                     ->setFontSize(11)
                     ->setTextColor(Color::fromRGB(160, 160, 160))
                     ->setMinWidth(80),
                 SizedBox(16, 0),
                 Text(canvasSizeLabel_, [](const std::string &s)
                      { return s; })
                     ->setFontSize(11)
                     ->setTextColor(Color::fromRGB(160, 160, 160)),
                 SizedBox(16, 0),
                 Text(zoomLabel_, [](const std::string &s)
                      { return "Zoom: " + s; })
                     ->setFontSize(11)
                     ->setTextColor(Color::fromRGB(160, 160, 160)),
             })
            ->setPadding(4)
            ->setHeight(24)
            ->setBackgroundColor(Color::fromRGB(22, 22, 24));

    return Flex({
                    toolbar,
                    Flex({
                             sidebar,
                             canvas_->setFlexGrow(1),
                         })
                        ->setDirection(FlexDirection::Row)
                        ->setHeightMode(SizeMode::Full)
                        ->setWidthMode(SizeMode::Full),

                    statusBar,
                })
        ->setDirection(FlexDirection::Column)
        ->setHeightMode(SizeMode::Full)
        ->setWidthMode(SizeMode::Full);
  }
};

// ============================================================
//  Entry point
// ============================================================

WidgetPtr createApp(FluxUI *app)
{
  return FluxApp("Paint App")
      .setTheme(AppTheme::dark())
      .setFullscreenMode(true)
      .build(std::make_shared<PaintApp>());
}