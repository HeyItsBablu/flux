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

// ============================================================================
// Shape types
// ============================================================================

enum class ShapeType
{
    Brush = 0,
    Line,
    Rectangle,
    FilledRect,
    RoundedRect,
    Ellipse,
    FilledEllipse,
    Triangle,
    Diamond,
    Star,
    Arrow,
    DoubleArrow,
    Pencil,
    Spray,
    Select,
    Eyedropper,
    FillBucket,
    Text,
};

// ============================================================================
// PaintSurface
// ============================================================================

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
        float radius; // brush radius, or font size for Text
        bool eraser;
        ShapeType shape;
        std::string text; // for ShapeType::Text
        // For background image (loaded file or flood-fill result)
        std::vector<uint8_t> imageData;
        int imageW = 0, imageH = 0;
        Canvas2DImage *glImage = nullptr;
    };

    Color activeColor_ = Color::fromRGB(30, 144, 255);
    float brushRadius_ = 6.f;
    float fontSize_ = 24.f; // text tool font size
    bool eraserMode_ = false;
    ShapeType activeShape_ = ShapeType::Brush;

    // ── Text tool state ────────────────────────────────────────────────────
    bool textEditing_ = false;
    float textX_ = 0.f, textY_ = 0.f;
    std::string textBuffer_;
    double textCursorTimer_ = 0.0;
    bool textCursorVis_ = true;

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

    // ── Callbacks ─────────────────────────────────────────────────────────
    std::function<void(Color)> onColorSampled;
    std::function<void()> onStrokeCommitted;
    std::function<void(int)> onKeyDownCallback;
    std::function<void(float, float)> onMousePosChanged;

    // ── Undo / Redo ────────────────────────────────────────────────────────
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
        commitText(); // commit any in-progress text first
        pushUndo();
        freeAllGLImages();
        strokes_.clear();
        current_ = nullptr;
        drawing_ = false;
    }

    // ── Commit in-progress text as a stroke ───────────────────────────────
    void commitText()
    {
        if (!textEditing_)
            return;
        textEditing_ = false;

        if (!textBuffer_.empty())
        {
            pushUndo();
            Stroke s;
            s.shape = ShapeType::Text;
            s.color = activeColor_;
            s.radius = fontSize_;
            s.eraser = false;
            s.text = textBuffer_;
            s.pts.push_back({textX_, textY_});
            strokes_.push_back(std::move(s));
            if (onStrokeCommitted)
                onStrokeCommitted();
        }
        textBuffer_.clear();
    }

    // ── Commit a moved selection back onto the canvas ─────────────────────
    void commitSelection()
    {
        if (!hasSelection_ || selPixels_.empty())
        {
            hasSelection_ = false;
            return;
        }

        // Flatten the lifted pixels back into a rasterized canvas image.
        // First rasterize everything except the hole (already punched when
        // we lifted), then blit the floating pixels at their new position.
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

    // ── Save ──────────────────────────────────────────────────────────────
    bool saveToFile(const std::string &path, int w, int h)
    {
        commitText();
        std::vector<uint8_t> pixels = rasterize(w, h);

        std::string lpath = path;
        for (char &c : lpath)
            c = char(tolower(c));
        bool isJpg = (lpath.size() >= 4 &&
                      (lpath.substr(lpath.size() - 4) == ".jpg" ||
                       (lpath.size() >= 5 && lpath.substr(lpath.size() - 5) == ".jpeg")));

        if (isJpg)
            return stbi_write_jpg(path.c_str(), w, h, 4, pixels.data(), 92) != 0;
        else
            return stbi_write_png(path.c_str(), w, h, 4, pixels.data(), w * 4) != 0;
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

    // ── Rasterise ─────────────────────────────────────────────────────────
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

            // Text strokes: skip CPU rasterise (GPU-only)
            if (s.shape == ShapeType::Text)
                continue;

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
            case ShapeType::Line:
                if (s.pts.size() >= 2)
                    rasterLine(s.pts.front().x, s.pts.front().y, s.pts.back().x, s.pts.back().y, s.radius, col);
                break;
            case ShapeType::Rectangle:
            case ShapeType::RoundedRect:
                if (s.pts.size() >= 2)
                    rasterRect(s.pts.front().x, s.pts.front().y, s.pts.back().x, s.pts.back().y, s.radius, col, false);
                break;
            case ShapeType::FilledRect:
                if (s.pts.size() >= 2)
                    rasterRect(s.pts.front().x, s.pts.front().y, s.pts.back().x, s.pts.back().y, s.radius, col, true);
                break;
            case ShapeType::Ellipse:
                if (s.pts.size() >= 2)
                {
                    float ecx = (s.pts.front().x + s.pts.back().x) * .5f, ecy = (s.pts.front().y + s.pts.back().y) * .5f;
                    rasterEllipse(ecx, ecy, std::abs(s.pts.back().x - s.pts.front().x) * .5f,
                                  std::abs(s.pts.back().y - s.pts.front().y) * .5f, s.radius, col, false);
                }
                break;
            case ShapeType::FilledEllipse:
                if (s.pts.size() >= 2)
                {
                    float ecx = (s.pts.front().x + s.pts.back().x) * .5f, ecy = (s.pts.front().y + s.pts.back().y) * .5f;
                    rasterEllipse(ecx, ecy, std::abs(s.pts.back().x - s.pts.front().x) * .5f,
                                  std::abs(s.pts.back().y - s.pts.front().y) * .5f, s.radius, col, true);
                }
                break;
            case ShapeType::Triangle:
                if (s.pts.size() >= 2)
                {
                    float tx0 = s.pts.front().x, ty0 = s.pts.front().y, tx1 = s.pts.back().x, ty1 = s.pts.back().y;
                    float tcx = (tx0 + tx1) * .5f;
                    rasterLine(tcx, ty0, tx1, ty1, s.radius, col);
                    rasterLine(tx1, ty1, tx0, ty1, s.radius, col);
                    rasterLine(tx0, ty1, tcx, ty0, s.radius, col);
                }
                break;
            case ShapeType::Diamond:
                if (s.pts.size() >= 2)
                {
                    float dx0 = s.pts.front().x, dy0 = s.pts.front().y, dx1 = s.pts.back().x, dy1 = s.pts.back().y;
                    float dcx = (dx0 + dx1) * .5f, dcy = (dy0 + dy1) * .5f;
                    rasterLine(dcx, dy0, dx1, dcy, s.radius, col);
                    rasterLine(dx1, dcy, dcx, dy1, s.radius, col);
                    rasterLine(dcx, dy1, dx0, dcy, s.radius, col);
                    rasterLine(dx0, dcy, dcx, dy0, s.radius, col);
                }
                break;
            case ShapeType::Star:
                if (s.pts.size() >= 2)
                {
                    float sx0 = s.pts.front().x, sy0 = s.pts.front().y, sx1 = s.pts.back().x, sy1 = s.pts.back().y;
                    float scx = (sx0 + sx1) * .5f, scy = (sy0 + sy1) * .5f;
                    float outerR = std::min(std::abs(sx1 - sx0), std::abs(sy1 - sy0)) * .5f, innerR = outerR * .45f;
                    for (int si = 0; si < 10; si++)
                    {
                        float a0 = float(si) * float(M_PI) / 5.f - float(M_PI) / 2.f;
                        float a1 = float(si + 1) * float(M_PI) / 5.f - float(M_PI) / 2.f;
                        float r0 = (si % 2 == 0) ? outerR : innerR, r1 = (si % 2 == 1) ? outerR : innerR;
                        rasterLine(scx + cosf(a0) * r0, scy + sinf(a0) * r0, scx + cosf(a1) * r1, scy + sinf(a1) * r1, s.radius, col);
                    }
                }
                break;
            case ShapeType::Arrow:
            case ShapeType::DoubleArrow:
                if (s.pts.size() >= 2)
                {
                    float ax0 = s.pts.front().x, ay0 = s.pts.front().y, ax1 = s.pts.back().x, ay1 = s.pts.back().y;
                    float adx = ax1 - ax0, ady = ay1 - ay0, alen = std::sqrtf(adx * adx + ady * ady);
                    if (alen > 1.f)
                    {
                        float ux = adx / alen, uy = ady / alen;
                        float headLen = std::min(20.f, alen * .35f), headW = headLen * .5f;
                        float shaftStart = (s.shape == ShapeType::DoubleArrow) ? headLen : 0.f;
                        rasterLine(ax0 + ux * shaftStart, ay0 + uy * shaftStart, ax1 - ux * headLen, ay1 - uy * headLen, s.radius, col);
                        rasterLine(ax1, ay1, ax1 - ux * headLen - uy * headW, ay1 - uy * headLen + ux * headW, s.radius, col);
                        rasterLine(ax1, ay1, ax1 - ux * headLen + uy * headW, ay1 - uy * headLen - ux * headW, s.radius, col);
                        if (s.shape == ShapeType::DoubleArrow)
                        {
                            rasterLine(ax0, ay0, ax0 + ux * headLen - uy * headW, ay0 + uy * headLen + ux * headW, s.radius, col);
                            rasterLine(ax0, ay0, ax0 + ux * headLen + uy * headW, ay0 + uy * headLen - ux * headW, s.radius, col);
                        }
                    }
                }
                break;
            default:
                break;
            }
        }
        return pixels;
    }

    // ── Flood fill ─────────────────────────────────────────────────────────
    void floodFill(float canvasX, float canvasY, Color fillColor)
    {
        std::vector<uint8_t> pixels = rasterize(w_, h_);

        int sx = int(canvasX), sy = int(canvasY);
        if (sx < 0 || sy < 0 || sx >= w_ || sy >= h_)
            return;

        uint8_t *seed = pixels.data() + (sy * w_ + sx) * 4;
        uint8_t tr = seed[0], tg = seed[1], tb = seed[2];
        if (tr == fillColor.r && tg == fillColor.g && tb == fillColor.b)
            return;

        auto colorMatch = [&](int px, int py) -> bool
        {
            uint8_t *p = pixels.data() + (py * w_ + px) * 4;
            return p[0] == tr && p[1] == tg && p[2] == tb;
        };
        auto paintPixel = [&](int px, int py)
        {
            uint8_t *p = pixels.data() + (py * w_ + px) * 4;
            p[0] = fillColor.r;
            p[1] = fillColor.g;
            p[2] = fillColor.b;
            p[3] = 255;
        };

        std::queue<std::pair<int, int>> q;
        q.push({sx, sy});
        paintPixel(sx, sy);
        while (!q.empty())
        {
            auto [x, y] = q.front();
            q.pop();
            int left = x, right = x;
            while (left - 1 >= 0 && colorMatch(left - 1, y))
            {
                left--;
                paintPixel(left, y);
            }
            while (right + 1 < w_ && colorMatch(right + 1, y))
            {
                right++;
                paintPixel(right, y);
            }
            for (int i = left; i <= right; i++)
            {
                if (y - 1 >= 0 && colorMatch(i, y - 1))
                {
                    paintPixel(i, y - 1);
                    q.push({i, y - 1});
                }
                if (y + 1 < h_ && colorMatch(i, y + 1))
                {
                    paintPixel(i, y + 1);
                    q.push({i, y + 1});
                }
            }
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
        s.imageData = std::move(pixels);
        strokes_.push_back(std::move(s));
        if (onStrokeCommitted)
            onStrokeCommitted();
    }

    // ── Eyedropper ────────────────────────────────────────────────────────
    void sampleColorAccurate(float x, float y)
    {
        if (x < 0 || y < 0 || x >= w_ || y >= h_)
            return;
        std::vector<uint8_t> pixels = rasterize(w_, h_);
        int ix = int(x), iy = int(y);
        uint8_t *p = pixels.data() + (iy * w_ + ix) * 4;
        Color c = Color::fromRGB(p[0], p[1], p[2]);
        if (onColorSampled)
            onColorSampled(c);
    }

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
        textBuffer_.clear();
    }
    void update(double dt) override
    {
        if (textEditing_)
        {
            textCursorTimer_ += dt;
            if (textCursorTimer_ >= 0.5)
            {
                textCursorTimer_ = 0.0;
                textCursorVis_ = !textCursorVis_;
            }
        }
        if (hasSelection_ || selDragging_)
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
        // ── Spray: emit dots while mouse is held ──────────────────────────
        if (sprayActive_ && activeShape_ == ShapeType::Spray)
        {
            sprayTimer_ += dt;
            while (sprayTimer_ >= kSprayInterval)
            {
                sprayTimer_ -= kSprayInterval;
                sprayDot(sprayMouseX_, sprayMouseY_);
            }
        }
    }
    bool needsContinuousRedraw() const override
    {
        return textEditing_ || hasSelection_ || selDragging_ || sprayActive_;
    }

    void onMouseDown(float x, float y) override
    {
        mouseCanvasX_ = x;
        mouseCanvasY_ = y;
        if (onMousePosChanged)
            onMousePosChanged(x, y);

        if (activeShape_ == ShapeType::Eyedropper)
        {
            sampleColorAccurate(x, y);
            return;
        }
        if (activeShape_ == ShapeType::FillBucket)
        {
            floodFill(x, y, activeColor_);
            return;
        }

        if (activeShape_ == ShapeType::Text)
        {
            commitText();
            textEditing_ = true;
            textX_ = x;
            textY_ = y;
            textBuffer_.clear();
            textCursorTimer_ = 0.0;
            textCursorVis_ = true;
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

        commitText();
        if (hasSelection_)
            commitSelection();

        pushUndo();
        drawing_ = true;

        if (activeShape_ == ShapeType::Pencil)
        {
            // Pencil: store as Brush stroke but radius=0.5 (1px)
            strokes_.push_back({{}, activeColor_, 0.5f, false, ShapeType::Brush});
            current_ = &strokes_.back();
            current_->pts.push_back({x, y});
        }
        else if (activeShape_ == ShapeType::Spray)
        {
            strokes_.push_back({{}, activeColor_, 0.f, false, ShapeType::Brush});
            current_ = &strokes_.back();
            sprayActive_ = true;
            sprayMouseX_ = x;
            sprayMouseY_ = y;
            sprayTimer_ = kSprayInterval; // emit first dot immediately
        }
        else
        {
            strokes_.push_back({{}, activeColor_, brushRadius_, eraserMode_, activeShape_});
            current_ = &strokes_.back();
            current_->pts.push_back({x, y});
        }
    }

    void onMouseMove(float x, float y) override
    {
        mouseCanvasX_ = x;
        mouseCanvasY_ = y;
        if (onMousePosChanged)
            onMousePosChanged(x, y);

        if (activeShape_ == ShapeType::Eyedropper ||
            activeShape_ == ShapeType::FillBucket ||
            activeShape_ == ShapeType::Text)
            return;

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
            }
            return;
        }

        if (activeShape_ == ShapeType::Spray)
        {
            sprayMouseX_ = x;
            sprayMouseY_ = y;
            return;
        }

        if (!drawing_ || !current_)
            return;
        if (activeShape_ == ShapeType::Brush ||
            activeShape_ == ShapeType::Pencil ||
            activeShape_ == ShapeType::Line)
            current_->pts.push_back({x, y});
        else
        {
            if (current_->pts.size() < 2)
                current_->pts.push_back({x, y});
            else
                current_->pts[1] = {x, y};
        }
    }

    void onMouseUp(float x, float y) override
    {
        if (activeShape_ == ShapeType::Eyedropper ||
            activeShape_ == ShapeType::FillBucket ||
            activeShape_ == ShapeType::Text)
            return;

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

        // Stop spray
        if (activeShape_ == ShapeType::Spray)
        {
            sprayActive_ = false;
            sprayTimer_ = 0.0;
            drawing_ = false;
            current_ = nullptr;
            redoStack_.clear();
            if (onStrokeCommitted)
                onStrokeCommitted();
            return;
        }

        if (current_)
        {
            if (activeShape_ != ShapeType::Brush && activeShape_ != ShapeType::Pencil)
                current_->pts.push_back({x, y});
        }
        drawing_ = false;
        current_ = nullptr;
        redoStack_.clear();
        if (onStrokeCommitted)
            onStrokeCommitted();
    }

    void onKeyDown(int key) override
    {
        if (textEditing_)
        {
            handleTextKey(key);
            return;
        }

        // Selection: Escape deselects, Delete clears the lifted pixels
#ifdef _WIN32
        if (activeShape_ == ShapeType::Select)
        {
            if (key == VK_ESCAPE && hasSelection_)
            {
                commitSelection();
                return;
            }
            if (key == VK_DELETE && hasSelection_)
            {
                selPixels_.clear();
                hasSelection_ = false;
                if (onStrokeCommitted)
                    onStrokeCommitted();
                return;
            }
        }
#endif
        if (onKeyDownCallback)
            onKeyDownCallback(key);
    }

    void render(Canvas2D &ctx) override
    {
        ctx.setFillColor(Color::fromRGB(255, 255, 255));
        ctx.fillRect(0, 0, float(ctx.width()), float(ctx.height()));

        for (auto &s : strokes_)
            renderStroke(ctx, s);

        if (current_)
            renderStroke(ctx, *current_);

        // ── Live text preview ─────────────────────────────────────────────
        if (textEditing_)
        {
            std::string display = textBuffer_;
            if (textCursorVis_)
                display += '|';

            char fontDesc[32];
            std::snprintf(fontDesc, sizeof(fontDesc), "%.0fpx sans", fontSize_);
            ctx.setFont(fontDesc);
            ctx.setTextBaseline(TextBaseline::Top);
            ctx.setTextAlign(CanvasTextAlign::Left);
            ctx.setFillColor(activeColor_);
            ctx.fillText(display, textX_, textY_);

            // Underline the editing region
            float tw = ctx.measureText(textBuffer_);
            ctx.setFillColor(Color{activeColor_.r, activeColor_.g, activeColor_.b, 120});
            ctx.fillRect(textX_, textY_ + fontSize_ + 2.f, tw + 2.f, 1.5f);
        }

        // ── Selection overlay ─────────────────────────────────────────────
        if (activeShape_ == ShapeType::Select)
        {
            // Draw floating selection pixels
            if (hasSelection_ && !selPixels_.empty())
            {
                // Upload as a temporary GL texture each frame
                // (cheap for selection-size tiles; could cache)
                GLuint tmpTex = 0;
                glGenTextures(1, &tmpTex);
                glBindTexture(GL_TEXTURE_2D, tmpTex);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, selPixW_, selPixH_, 0,
                             GL_RGBA, GL_UNSIGNED_BYTE, selPixels_.data());
                glBindTexture(GL_TEXTURE_2D, 0);
                Canvas2DImage *tmp = ctx.wrapTexture(tmpTex, selPixW_, selPixH_);
                ctx.drawImage(tmp, selX_, selY_, selW_, selH_);
                delete tmp;
                glDeleteTextures(1, &tmpTex);
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
                // Draw dashes along each edge — declared as std::function
                // so MSVC doesn't complain about self-capture in auto lambda
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

    int canvasW() const { return w_; }
    int canvasH() const { return h_; }

    // ── Public helpers for resize dialog ──────────────────────────────────
    void pushUndo_public() { pushUndo(); }
    void freeAllGLImages_public() { freeAllGLImages(); }
    std::vector<Stroke> &strokes_public() { return strokes_; }
    void resize_public(int w, int h)
    {
        w_ = w;
        h_ = h;
    }

private:
    static constexpr int kMaxUndo = 50;

    // ── Spray dot emitter ─────────────────────────────────────────────────
    void sprayDot(float cx, float cy)
    {
        if (!current_)
            return;
        // Use brushRadius_ as spray radius; density ~ radius/2 dots per tick
        float r = brushRadius_ * 3.f;
        int n = std::max(3, int(brushRadius_));
        // Simple LCG-style cheap random — no stdlib random needed
        static uint32_t seed = 12345;
        auto rnd = [&]() -> float
        {
            seed = seed * 1664525u + 1013904223u;
            return float(seed & 0xFFFF) / 65535.f; // [0,1]
        };
        for (int i = 0; i < n; ++i)
        {
            // Uniform disc sample
            float angle = rnd() * 2.f * float(M_PI);
            float dist = rnd() * r;
            float px = cx + cosf(angle) * dist;
            float py = cy + sinf(angle) * dist;
            current_->pts.push_back({px, py});
        }
    }

    // ── Text key handler ──────────────────────────────────────────────────
    void handleTextKey(int key)
    {
#ifdef _WIN32
        if (key < 0)
        {
            char ch = char(-key);
            if (ch >= 32 && ch < 127) // already guarded, but be explicit
                textBuffer_ += ch;
            return;
        }
        switch (key)
        {
        case VK_RETURN:
            commitText();
            break;
        case VK_ESCAPE:
            textEditing_ = false;
            textBuffer_.clear();
            break;
        case VK_BACK:
            if (!textBuffer_.empty())
                textBuffer_.pop_back();
            break;
        // DO NOT fall through to default for printable chars —
        // they arrive via WM_CHAR as negative values above.
        default:
            break; // ← remove the old "if (key >= 32)" branch entirely
        }
#else
        // Linux/SDL path unchanged
#endif
        textCursorTimer_ = 0.0;
        textCursorVis_ = true;
    }

    void pushUndo()
    {
        undoStack_.push_back(strokes_);
        if (int(undoStack_.size()) > kMaxUndo)
        {
            freeGLImages(undoStack_.front());
            undoStack_.erase(undoStack_.begin());
        }
        redoStack_.clear();
    }

    void freeGLImages(std::vector<Stroke> &vec)
    {
        for (auto &s : vec)
            if (s.glImage)
            {
                delete s.glImage;
                s.glImage = nullptr;
            }
    }

    void freeAllGLImages()
    {
        for (auto &s : strokes_)
            if (s.glImage)
            {
                delete s.glImage;
                s.glImage = nullptr;
            }
    }

    // ── GL render helpers ─────────────────────────────────────────────────
    void renderStroke(Canvas2D &ctx, Stroke &s)
    {
        if (s.pts.empty() && s.imageData.empty())
            return;

        if (!s.imageData.empty() && s.imageW > 0 && s.imageH > 0)
        {
            if (!s.glImage)
            {
                bool isPNGorJPG = s.imageData.size() >= 2 &&
                                  (s.imageData[0] == 0x89 || (s.imageData[0] == 0xFF && s.imageData[1] == 0xD8));
                if (isPNGorJPG)
                {
                    s.glImage = ctx.loadImageFromMemory(s.imageData.data(), int(s.imageData.size()));
                }
                else
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
            }
            if (s.glImage)
                ctx.drawImage(s.glImage, 0.f, 0.f, float(s.imageW), float(s.imageH));
            return;
        }

        // ── Text stroke ───────────────────────────────────────────────────
        if (s.shape == ShapeType::Text && !s.pts.empty())
        {
            char fontDesc[32];
            std::snprintf(fontDesc, sizeof(fontDesc), "%.0fpx sans", s.radius);
            ctx.setFont(fontDesc);
            ctx.setTextBaseline(TextBaseline::Top);
            ctx.setTextAlign(CanvasTextAlign::Left);
            ctx.setFillColor(s.color);
            ctx.fillText(s.text, s.pts[0].x, s.pts[0].y);
            return;
        }

        Color col = s.eraser ? Color::fromRGB(255, 255, 255) : s.color;
        switch (s.shape)
        {
        case ShapeType::Brush:
            renderBrush(ctx, s, col);
            break;
        case ShapeType::Line:
            renderLine(ctx, s, col);
            break;
        case ShapeType::Rectangle:
            renderRect(ctx, s, col, false, false);
            break;
        case ShapeType::FilledRect:
            renderRect(ctx, s, col, true, false);
            break;
        case ShapeType::RoundedRect:
            renderRect(ctx, s, col, false, true);
            break;
        case ShapeType::Ellipse:
            renderEllipse(ctx, s, col, false);
            break;
        case ShapeType::FilledEllipse:
            renderEllipse(ctx, s, col, true);
            break;
        case ShapeType::Triangle:
            renderTriangle(ctx, s, col);
            break;
        case ShapeType::Diamond:
            renderDiamond(ctx, s, col);
            break;
        case ShapeType::Star:
            renderStar(ctx, s, col);
            break;
        case ShapeType::Arrow:
            renderArrow(ctx, s, col, false);
            break;
        case ShapeType::DoubleArrow:
            renderArrow(ctx, s, col, true);
            break;
        default:
            break;
        }
    }

    void renderBrush(Canvas2D &ctx, const Stroke &s, Color col)
    {
        ctx.setFillColor(col);
        if (s.pts.size() == 1)
        {
            ctx.fillCircle(s.pts[0].x, s.pts[0].y, s.radius);
            return;
        }
        for (size_t i = 1; i < s.pts.size(); ++i)
        {
            float ax = s.pts[i - 1].x, ay = s.pts[i - 1].y, bx = s.pts[i].x, by = s.pts[i].y;
            float dx = bx - ax, dy = by - ay, len = std::sqrtf(dx * dx + dy * dy);
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

    void renderLine(Canvas2D &ctx, const Stroke &s, Color col)
    {
        ctx.setFillColor(col);
        for (size_t i = 1; i < s.pts.size(); ++i)
        {
            float ax = s.pts[i - 1].x, ay = s.pts[i - 1].y, bx = s.pts[i].x, by = s.pts[i].y;
            float dx = bx - ax, dy = by - ay, len = std::sqrtf(dx * dx + dy * dy);
            if (len < 0.5f)
                continue;
            float nx = -dy / len * s.radius, ny = dx / len * s.radius;
            ctx.beginPath();
            ctx.moveTo(ax + nx, ay + ny);
            ctx.lineTo(bx + nx, by + ny);
            ctx.lineTo(bx - nx, by - ny);
            ctx.lineTo(ax - nx, ay - ny);
            ctx.closePath();
            ctx.fill();
        }
    }

    void renderRect(Canvas2D &ctx, const Stroke &s, Color col, bool filled, bool rounded)
    {
        if (s.pts.size() < 2)
            return;
        float x0 = s.pts[0].x, y0 = s.pts[0].y, x1 = s.pts.back().x, y1 = s.pts.back().y;
        float lx = std::min(x0, x1), ly = std::min(y0, y1);
        float rw = std::abs(x1 - x0), rh = std::abs(y1 - y0);
        if (filled)
        {
            ctx.setFillColor(col);
            if (rounded)
                ctx.fillRoundedRect(lx, ly, rw, rh, 10.f);
            else
                ctx.fillRect(lx, ly, rw, rh);
        }
        else
        {
            ctx.setStrokeColor(col);
            ctx.setLineWidth(s.radius);
            if (rounded)
                ctx.strokeRoundedRect(lx, ly, rw, rh, 10.f);
            else
                ctx.strokeRect(lx, ly, rw, rh);
        }
    }

    void renderEllipse(Canvas2D &ctx, const Stroke &s, Color col, bool filled)
    {
        if (s.pts.size() < 2)
            return;
        float x0 = s.pts[0].x, y0 = s.pts[0].y, x1 = s.pts.back().x, y1 = s.pts.back().y;
        float cx = (x0 + x1) * .5f, cy = (y0 + y1) * .5f;
        float rx = std::abs(x1 - x0) * .5f, ry = std::abs(y1 - y0) * .5f;
        if (filled)
        {
            ctx.setFillColor(col);
            ctx.beginPath();
            ctx.ellipse(cx, cy, rx, ry);
            ctx.fill();
        }
        else
        {
            ctx.setStrokeColor(col);
            ctx.setLineWidth(s.radius);
            ctx.beginPath();
            ctx.ellipse(cx, cy, rx, ry);
            ctx.stroke();
        }
    }

    void renderTriangle(Canvas2D &ctx, const Stroke &s, Color col)
    {
        if (s.pts.size() < 2)
            return;
        float x0 = s.pts[0].x, y0 = s.pts[0].y, x1 = s.pts.back().x, y1 = s.pts.back().y;
        float cx = (x0 + x1) * .5f;
        ctx.setStrokeColor(col);
        ctx.setLineWidth(s.radius);
        ctx.beginPath();
        ctx.moveTo(cx, y0);
        ctx.lineTo(x1, y1);
        ctx.lineTo(x0, y1);
        ctx.closePath();
        ctx.stroke();
    }

    void renderDiamond(Canvas2D &ctx, const Stroke &s, Color col)
    {
        if (s.pts.size() < 2)
            return;
        float x0 = s.pts[0].x, y0 = s.pts[0].y, x1 = s.pts.back().x, y1 = s.pts.back().y;
        float cx = (x0 + x1) * .5f, cy = (y0 + y1) * .5f;
        ctx.setStrokeColor(col);
        ctx.setLineWidth(s.radius);
        ctx.beginPath();
        ctx.moveTo(cx, y0);
        ctx.lineTo(x1, cy);
        ctx.lineTo(cx, y1);
        ctx.lineTo(x0, cy);
        ctx.closePath();
        ctx.stroke();
    }

    void renderStar(Canvas2D &ctx, const Stroke &s, Color col)
    {
        if (s.pts.size() < 2)
            return;
        float x0 = s.pts[0].x, y0 = s.pts[0].y, x1 = s.pts.back().x, y1 = s.pts.back().y;
        float cx = (x0 + x1) * .5f, cy = (y0 + y1) * .5f;
        float outerR = std::min(std::abs(x1 - x0), std::abs(y1 - y0)) * .5f, innerR = outerR * .45f;
        ctx.setStrokeColor(col);
        ctx.setLineWidth(s.radius);
        ctx.beginPath();
        for (int i = 0; i < 10; ++i)
        {
            float a = float(i) * float(M_PI) / 5.f - float(M_PI) / 2.f;
            float r = (i % 2 == 0) ? outerR : innerR;
            if (i == 0)
                ctx.moveTo(cx + cosf(a) * r, cy + sinf(a) * r);
            else
                ctx.lineTo(cx + cosf(a) * r, cy + sinf(a) * r);
        }
        ctx.closePath();
        ctx.stroke();
    }

    void renderArrow(Canvas2D &ctx, const Stroke &s, Color col, bool doubleArrow)
    {
        if (s.pts.size() < 2)
            return;
        float x0 = s.pts[0].x, y0 = s.pts[0].y, x1 = s.pts.back().x, y1 = s.pts.back().y;
        float dx = x1 - x0, dy = y1 - y0, len = std::sqrtf(dx * dx + dy * dy);
        if (len < 1.f)
            return;
        float ux = dx / len, uy = dy / len;
        float headLen = std::min(20.f, len * .35f), headW = headLen * .5f;
        ctx.setStrokeColor(col);
        ctx.setLineWidth(s.radius);
        ctx.setFillColor(col);
        ctx.beginPath();
        ctx.moveTo(x0 + ux * (doubleArrow ? headLen : 0), y0 + uy * (doubleArrow ? headLen : 0));
        ctx.lineTo(x1 - ux * headLen, y1 - uy * headLen);
        ctx.stroke();
        auto drawHead = [&](float tx, float ty, float dirX, float dirY)
        {
            ctx.beginPath();
            ctx.moveTo(tx, ty);
            ctx.lineTo(tx - dirX * headLen - dirY * headW, ty - dirY * headLen + dirX * headW);
            ctx.lineTo(tx - dirX * headLen + dirY * headW, ty - dirY * headLen - dirX * headW);
            ctx.closePath();
            ctx.fill();
        };
        drawHead(x1, y1, ux, uy);
        if (doubleArrow)
            drawHead(x0, y0, -ux, -uy);
    }

    // ── Spray tool state ──────────────────────────────────────────────────
    double sprayTimer_ = 0.0;
    float sprayMouseX_ = 0.f;
    float sprayMouseY_ = 0.f;
    bool sprayActive_ = false;
    static constexpr double kSprayInterval = 0.025;

    std::vector<Stroke> strokes_;
    std::vector<std::vector<Stroke>> undoStack_;
    std::vector<std::vector<Stroke>> redoStack_;
    Stroke *current_ = nullptr;
    bool drawing_ = false;
    int w_ = 512, h_ = 512;
};

// ============================================================================
// PaintApp
// ============================================================================

class PaintApp : public Widget
{
    std::shared_ptr<CanvasWidget> canvas_;
    std::shared_ptr<PaintSurface> surface_;
    std::shared_ptr<ColorPickerWidget> colorPicker_;

    // Tool buttons stored so we can highlight active one
    std::vector<std::shared_ptr<ButtonWidget>> shapeButtons_;
    std::vector<std::shared_ptr<ButtonWidget>> sizeButtons_;
    std::shared_ptr<ButtonWidget> eraserBtn_;

    State<int> resizeW_{512};
    State<int> resizeH_{512};

    State<int> selectedColor_{0};
    State<int> brushSize_{1};
    State<bool> eraserOn_{false};
    State<int> selectedShape_{0};
    State<std::string> zoomLabel_{"100%"};
    State<bool> canUndo_{false};
    State<bool> canRedo_{false};

    // #5 Status bar states
    State<std::string> cursorPosLabel_{"0, 0"};
    State<std::string> canvasSizeLabel_{"512 × 512"};

    State<std::string> fontSizeLabel_{"24"};
    std::vector<std::shared_ptr<ButtonWidget>> fontSizeButtons_;

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

    struct ShapeTool
    {
        const char *label;
        const char *tooltip;
        ShapeType type;
    };
    const std::vector<ShapeTool> shapeTools_ = {
        {"/", "Brush / Pencil", ShapeType::Brush},
        {"—", "Line", ShapeType::Line},
        {"▭", "Rectangle (outline)", ShapeType::Rectangle},
        {"▬", "Rectangle (filled)", ShapeType::FilledRect},
        {"▢", "Rounded Rect", ShapeType::RoundedRect},
        {"◯", "Ellipse (outline)", ShapeType::Ellipse},
        {"●", "Ellipse (filled)", ShapeType::FilledEllipse},
        {"△", "Triangle", ShapeType::Triangle},
        {"◇", "Diamond", ShapeType::Diamond},
        {"★", "Star", ShapeType::Star},
        {"→", "Arrow", ShapeType::Arrow},
        {"↔", "Double Arrow", ShapeType::DoubleArrow},

        {"✏", "Pencil (1px)", ShapeType::Pencil},
        {"💨", "Spray / Airbrush", ShapeType::Spray},
        {"⬚", "Select & Move", ShapeType::Select},
        {"✦", "Eyedropper", ShapeType::Eyedropper},
        {"🪣", "Fill Bucket", ShapeType::FillBucket},
        {"T", "Text", ShapeType::Text},
    };

    static constexpr float kBrushSizes[3] = {3.f, 7.f, 16.f};

    // ── Active tool highlight helpers ──────────────────────────────────────
    static constexpr Color kActiveBg = {60, 120, 220, 255};
    static constexpr Color kInactiveBg = {50, 50, 54, 255};

    void highlightShapeButton(int idx)
    {
        for (int i = 0; i < int(shapeButtons_.size()); i++)
            if (shapeButtons_[i])
                shapeButtons_[i]->setBackgroundColor(i == idx ? kActiveBg : kInactiveBg);
        if (eraserBtn_)
            eraserBtn_->setBackgroundColor(kInactiveBg);
    }

    void highlightSizeButton(int idx)
    {
        for (int i = 0; i < int(sizeButtons_.size()); i++)
            if (sizeButtons_[i])
                sizeButtons_[i]->setBackgroundColor(i == idx ? kActiveBg : kInactiveBg);
    }

    void highlightEraserButton()
    {
        for (auto &b : shapeButtons_)
            if (b)
                b->setBackgroundColor(kInactiveBg);
        if (eraserBtn_)
            eraserBtn_->setBackgroundColor(kActiveBg);
    }

    void refreshUndoState()
    {
        if (surface_)
        {
            canUndo_.set(surface_->canUndo());
            canRedo_.set(surface_->canRedo());
        }
    }

    // #5 Update canvas size label
    void updateCanvasSizeLabel()
    {
        if (!surface_)
            return;
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%d × %d",
                      surface_->canvasW(), surface_->canvasH());
        canvasSizeLabel_.set(buf);
    }

void applyResize()
    {
        if (!surface_ || !canvas_) return;
        int nw = std::max(1, resizeW_.get());
        int nh = std::max(1, resizeH_.get());

        std::vector<uint8_t> oldPx = surface_->rasterize(surface_->canvasW(), surface_->canvasH());
        int oldW = surface_->canvasW(), oldH = surface_->canvasH();
        std::vector<uint8_t> newPx(size_t(nw) * nh * 4, 255);
        int cpW = std::min(oldW, nw), cpH = std::min(oldH, nh);
        for (int py = 0; py < cpH; ++py)
            for (int px = 0; px < cpW; ++px)
            {
                const uint8_t* src = oldPx.data() + (py * oldW + px) * 4;
                uint8_t*       dst = newPx.data() + (py * nw  + px) * 4;
                dst[0]=src[0]; dst[1]=src[1]; dst[2]=src[2]; dst[3]=src[3];
            }

        surface_->pushUndo_public();
        surface_->freeAllGLImages_public();
        surface_->strokes_public().clear();

        PaintSurface::Stroke st;
        st.shape     = ShapeType::Brush;
        st.color     = Color::fromRGB(0, 0, 0);
        st.radius    = 0.f;
        st.eraser    = false;
        st.imageW    = nw;
        st.imageH    = nh;
        st.imageData = std::move(newPx);
        surface_->strokes_public().push_back(std::move(st));
        surface_->resize_public(nw, nh);

        // setCanvasSize updates vp_ canvas dims, then fitToView uses them
        canvas_->setCanvasSize(nw, nh);
        canvas_->viewport().setCanvasSize(nw, nh);  // ← ensure vp_ is in sync
        canvas_->viewport().fitToView();
        canvas_->redraw();
        updateCanvasSizeLabel();
        refreshUndoState();
    }
public:
    WidgetPtr build() override
    {
        canvas_ = std::make_shared<CanvasWidget>();
        canvas_->setViewportEnabled(true);
        canvas_->setScrollbarsEnabled(false);
        surface_ = canvas_->setSurface<PaintSurface>();

        surface_->activeColor_ = palette_[0].color;
        surface_->brushRadius_ = kBrushSizes[1];
        surface_->activeShape_ = shapeTools_[0].type;

        resizeW_.set(surface_->canvasW());
        resizeH_.set(surface_->canvasH());

        canvas_->viewport().fitToView();

        surface_->onStrokeCommitted = [this]()
        { refreshUndoState(); };

        canvas_->onViewportChanged = [this](float zoom)
        {
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%.0f%%", zoom * 100.f);
            zoomLabel_.set(buf);
        };

        // #5 Status bar: mouse position callback
        surface_->onMousePosChanged = [this](float x, float y)
        {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.0f, %.0f", x, y);
            cursorPosLabel_.set(buf);
        };

        std::weak_ptr<PaintSurface> ws = surface_;
        std::weak_ptr<CanvasWidget> wc = canvas_;

        // Eyedropper callback (now pixel-accurate via sampleColorAccurate)
        surface_->onColorSampled = [this, ws](Color c)
        {
            if (auto s = ws.lock())
            {
                s->activeColor_ = c;
                s->eraserMode_ = false;
            }
            selectedColor_.set(-1);
            eraserOn_.set(false);
            if (colorPicker_)
                colorPicker_->setColor(c);
        };

        // ── Shape sidebar ─────────────────────────────────────────────────
        shapeButtons_.resize(shapeTools_.size());

        auto shapeGrid = Column({});
        shapeGrid->setSpacing(3);
        shapeGrid->setPadding(6);

        for (int row = 0; row < int(shapeTools_.size()); row += 2)
        {
            auto rowW = Row({});
            rowW->setSpacing(3);
            for (int col = 0; col < 2 && row + col < int(shapeTools_.size()); ++col)
            {
                int idx = row + col;
                const auto &st = shapeTools_[idx];
                auto btn = Button(st.label)
                               ->setHeight(28)
                               ->setWidth(52)
                               ->setBackgroundColor(idx == 0 ? kActiveBg : kInactiveBg) // #2 highlight
                               ->setOnClick([this, ws, idx, st]()
                                            {
                                                selectedShape_.set(idx);
                                                eraserOn_.set(false);
                                                if (auto s = ws.lock())
                                                {
                                                    s->activeShape_ = st.type;
                                                    s->eraserMode_ = false;
                                                }
                                                highlightShapeButton(idx); // #2
                                            });
                shapeButtons_[idx] = btn;
                rowW->addChild(btn);
            }
            shapeGrid->addChild(rowW);
        }

        // ── Draw label + brush size ────────────────────────────────────────
        auto drawLabel = Text("DRAW")
                             ->setFontSize(9)
                             ->setTextColor(Color::fromRGB(140, 140, 160))
                             ->setPaddingLRTB(8, 8, 6, 2);

        auto makeSizeBtn = [&](const char *lbl, int idx, float r) -> WidgetPtr
        {
            auto btn = Button(lbl)
                           ->setHeight(26)
                           ->setWidth(30)
                           ->setBackgroundColor(idx == 1 ? kActiveBg : kInactiveBg) // M active by default
                           ->setOnClick([this, ws, idx, r]()
                                        {
                                            brushSize_.set(idx);
                                            if (auto s = ws.lock())
                                                s->brushRadius_ = r;
                                            highlightSizeButton(idx); // #2
                                        });
            sizeButtons_.push_back(btn);
            return btn;
        };

        auto sizeRow = Row({
            makeSizeBtn("S", 0, kBrushSizes[0]),
            makeSizeBtn("M", 1, kBrushSizes[1]),
            makeSizeBtn("L", 2, kBrushSizes[2]),
        });
        sizeRow->setSpacing(3);

        // ── Undo / Redo ────────────────────────────────────────────────────
        auto undoBtn = Button("↩")
                           ->setHeight(26)
                           ->setWidth(36)
                           ->setOnClick([this, ws, wc]()
                                        {
                if (auto s = ws.lock()) s->undo();
                refreshUndoState();
                if (auto c = wc.lock()) c->redraw(); });
        auto redoBtn = Button("↪")
                           ->setHeight(26)
                           ->setWidth(36)
                           ->setOnClick([this, ws, wc]()
                                        {
                if (auto s = ws.lock()) s->redo();
                refreshUndoState();
                if (auto c = wc.lock()) c->redraw(); });
        auto undoRedoRow = Row({undoBtn, redoBtn});
        undoRedoRow->setSpacing(4);

        // Ctrl+Z / Ctrl+Y
        surface_->onKeyDownCallback = [this, ws, wc](int key)
        {
#ifdef _WIN32
            bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
#else
            bool ctrl = false;
#endif
            if (ctrl && key == 'Z')
            {
                if (auto s = ws.lock())
                    s->undo();
                refreshUndoState();
                if (auto c = wc.lock())
                    c->redraw();
            }
            else if (ctrl && key == 'Y')
            {
                if (auto s = ws.lock())
                    s->redo();
                refreshUndoState();
                if (auto c = wc.lock())
                    c->redraw();
            }
        };

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
        auto swatchGrid = Column({});
        swatchGrid->setSpacing(3);

        for (int row = 0; row < int(palette_.size()); row += 3)
        {
            auto rowW = Row({});
            rowW->setSpacing(3);
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

        // ── Eraser + Clear ─────────────────────────────────────────────────
        eraserBtn_ = Button("Eraser")
                         ->setHeight(26)
                         ->setWidth(54)
                         ->setBackgroundColor(kInactiveBg)
                         ->setOnClick([this, ws]()
                                      {
                                          eraserOn_.set(true);
                                          if (auto s = ws.lock())
                                          {
                                              s->eraserMode_ = true;
                                              s->activeShape_ = ShapeType::Brush;
                                          }
                                          highlightEraserButton(); // #2
                                      });

        auto clearBtn = Button("Clear")
                            ->setHeight(26)
                            ->setWidth(48)
                            ->setOnClick([this, ws, wc]()
                                         {
                if (auto s = ws.lock()) s->clear();
                refreshUndoState();
                if (auto c = wc.lock()) c->redraw(); });

        auto toolRow = Row({eraserBtn_, clearBtn});
        toolRow->setSpacing(4);

        // ── Text font size ─────────────────────────────────────────────────────
        auto textLabel = Text("TEXT SIZE")
                             ->setFontSize(9)
                             ->setTextColor(Color::fromRGB(140, 140, 160))
                             ->setPaddingLRTB(8, 8, 6, 2);

        static constexpr float kFontSizes[3] = {16.f, 24.f, 36.f};
        auto makeFontSizeBtn = [&](const char *lbl, int idx, float sz) -> WidgetPtr
        {
            auto btn = Button(lbl)
                           ->setHeight(26)
                           ->setWidth(30)
                           ->setBackgroundColor(idx == 1 ? kActiveBg : kInactiveBg)
                           ->setOnClick([this, ws, idx, sz]()
                                        {
            if (auto s = ws.lock()) s->fontSize_ = sz;
            for (int i=0;i<int(fontSizeButtons_.size());i++)
                if (fontSizeButtons_[i])
                    fontSizeButtons_[i]->setBackgroundColor(
                        i==idx ? kActiveBg : kInactiveBg); });
            fontSizeButtons_.push_back(btn);
            return btn;
        };
        auto fontSizeRow = Row({
            makeFontSizeBtn("S", 0, kFontSizes[0]),
            makeFontSizeBtn("M", 1, kFontSizes[1]),
            makeFontSizeBtn("L", 2, kFontSizes[2]),
        });
        fontSizeRow->setSpacing(3);

        // ── #6 Open image ──────────────────────────────────────────────────
        auto openLabel = Text("FILE")
                             ->setFontSize(9)
                             ->setTextColor(Color::fromRGB(140, 140, 160))
                             ->setPaddingLRTB(8, 8, 8, 4);

        auto openBtn = Button("Open")
                           ->setHeight(26)
                           ->setWidth(50)
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

        // ── RESIZE ────────────────────────────────────────────────────────
        auto resizeLabel = Text("RESIZE")
                               ->setFontSize(9)
                               ->setTextColor(Color::fromRGB(140, 140, 160))
                               ->setPaddingLRTB(8, 8, 6, 2);

        auto wInput = NumberInput(1, 8192, 1)
                          ->setValue(resizeW_)
                          ->setWidth(104);
        wInput->setOnValueChanged([this](double v)
                                  { resizeW_.set(int(v)); });

        auto hInput = NumberInput(1, 8192, 1)
                          ->setValue(resizeH_)
                          ->setWidth(104);
        hInput->setOnValueChanged([this](double v)
                                  { resizeH_.set(int(v)); });

        auto applyResizeBtn = Button("Apply")
                                  ->setHeight(26)
                                  ->setWidth(104)
                                  ->setOnClick([this]()
                                               { applyResize(); });

        // ── Save / Export ──────────────────────────────────────────────────
        auto saveLabel = Text("EXPORT")
                             ->setFontSize(9)
                             ->setTextColor(Color::fromRGB(140, 140, 160))
                             ->setPaddingLRTB(8, 8, 8, 4);

        auto savePngBtn = Button("PNG")
                              ->setHeight(26)
                              ->setWidth(50)
                              ->setOnClick([this, ws]()
                                           {
                auto picker = FilePicker("Save PNG");
                picker->setMode(FilePickerMode::Save)
                       ->setTitle("Export as PNG")
                       ->setDefaultFilename("painting.png")
                       ->setDefaultExtension("png")
                       ->addFilter("PNG Image", {"*.png"})
                       ->setOnChanged([this, ws](const std::string &path){
                           if (path.empty()) return;
                           if (auto s = ws.lock())
                               s->saveToFile(path, s->canvasW(), s->canvasH());
                       });
                picker->open(); });

        auto saveJpgBtn = Button("JPG")
                              ->setHeight(26)
                              ->setWidth(50)
                              ->setOnClick([this, ws]()
                                           {
                auto picker = FilePicker("Save JPG");
                picker->setMode(FilePickerMode::Save)
                       ->setTitle("Export as JPG")
                       ->setDefaultFilename("painting.jpg")
                       ->setDefaultExtension("jpg")
                       ->addFilter("JPEG Image", {"*.jpg","*.jpeg"})
                       ->setOnChanged([this, ws](const std::string &path){
                           if (path.empty()) return;
                           if (auto s = ws.lock())
                               s->saveToFile(path, s->canvasW(), s->canvasH());
                       });
                picker->open(); });

        auto fileRow = Row({openBtn});
        fileRow->setSpacing(4);

        auto saveRow = Row({savePngBtn, saveJpgBtn});
        saveRow->setSpacing(4);

        // ── Zoom controls ──────────────────────────────────────────────────
        auto zoomLabel = Text("ZOOM")
                             ->setFontSize(9)
                             ->setTextColor(Color::fromRGB(140, 140, 160))
                             ->setPaddingLRTB(8, 8, 8, 4);

        auto fitBtn = Button("Fit")
                          ->setHeight(24)
                          ->setWidth(40)
                          ->setOnClick([wc]()
                                       { if (auto c=wc.lock()) c->viewport().fitToView(); });

        auto oneBtn = Button("1:1")
                          ->setHeight(24)
                          ->setWidth(36)
                          ->setOnClick([wc]()
                                       { if (auto c=wc.lock()) c->viewport().resetZoom(); });

        auto zoomPct = Text(zoomLabel_, [](const std::string &s)
                            { return s; })
                           ->setFontSize(10)
                           ->setTextColor(Color::fromRGB(140, 140, 160));

        auto zoomRow = Row({fitBtn, oneBtn, zoomPct});
        zoomRow->setSpacing(4);

        // ── Sidebar assembly ───────────────────────────────────────────────
        auto sidebarContent = ListView({
            drawLabel,
            sizeRow,
            SizedBox(0, 4),
            Text("UNDO/REDO")
                ->setFontSize(9)
                ->setTextColor(Color::fromRGB(140, 140, 160))
                ->setPaddingLRTB(8, 8, 6, 2),
            undoRedoRow,
            SizedBox(0, 6),
            Text("SHAPES")
                ->setFontSize(9)
                ->setTextColor(Color::fromRGB(140, 140, 160))
                ->setPaddingLRTB(8, 8, 8, 4),
            Container(shapeGrid)->setHeight(258),
            SizedBox(0, 4),
            colorLabel,
            colorPicker_,
            SizedBox(0, 4),
            Container(swatchGrid)->setHeight(120),
            SizedBox(0, 6),
            toolRow,
            SizedBox(0, 6),
            textLabel,
            fontSizeRow,
            SizedBox(0, 6),
            openLabel,
            openBtn,
            SizedBox(0, 4),
            resizeLabel,
            Text("W")->setFontSize(9)->setTextColor(Color::fromRGB(140, 140, 160))->setPaddingLRTB(8, 8, 2, 1),
            wInput,
            Text("H")->setFontSize(9)->setTextColor(Color::fromRGB(140, 140, 160))->setPaddingLRTB(8, 8, 2, 1),
            hInput,
            SizedBox(0, 2),
            applyResizeBtn,
            SizedBox(0, 6),
            saveLabel,
            saveRow,
            SizedBox(0, 4),
            zoomLabel,
            zoomRow,
            SizedBox(0, 8),
        });
        sidebarContent->setSpacing(0);

        auto sidebar = Container(sidebarContent)
                           ->setWidth(120)
                           ->setBackgroundColor(Color::fromRGB(28, 28, 30));

        // ── Top toolbar ────────────────────────────────────────────────────
        auto toolbar = Container(
                           Row({
                                   Text("Paint")
                                       ->setFontSize(13)
                                       ->setTextColor(Color::fromRGB(220, 220, 220)),
                                   SizedBox(8, 0),
                               })
                               ->setPadding(8)
                               ->setSpacing(6)
                               ->setCrossAxisAlignment(CrossAxisAlignment::Center))
                           ->setHeight(44)
                           ->setBackgroundColor(Color::fromRGB(28, 28, 30));

        // ── #5 Status bar ──────────────────────────────────────────────────
        updateCanvasSizeLabel();

        auto statusBar = Container(
                             Row({
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
                                 ->setSpacing(0)
                                 ->setCrossAxisAlignment(CrossAxisAlignment::Center))
                             ->setHeight(24)
                             ->setBackgroundColor(Color::fromRGB(22, 22, 24));

        // ── Root layout ────────────────────────────────────────────────────
        return Scaffold(
            nullptr,
            Expanded(Column({
                toolbar,
                Expanded(Row({
                    sidebar,
                    Expanded(canvas_),
                })),
                statusBar,
            })),
            nullptr,
            nullptr);
    }
};

// ============================================================================
// Entry point
// ============================================================================

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