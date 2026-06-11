#pragma once
#include "flux/flux.hpp"

#include <cmath>
#include <vector>
#include <string>
#include <functional>
#include <algorithm>
#include <limits>

#include <stb_image_write.h>
#include <stb_image.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ============================================================
// VecShapeType 
// ============================================================

enum class VecShapeType
{
    Path = 0, // freehand pen / brush path (polyline)
    Rect,     // rectangle
    Ellipse,  // ellipse / circle
    Line,     // straight line segment
    Text,     // placed text label
};

// ============================================================
// VecShape — one vector object on the canvas
// ============================================================

struct VecShape
{
    VecShapeType type = VecShapeType::Path;

    // ── Shared style ─────────────────────────────────────────
    Color fillColor = Color::fromRGBA(30, 144, 255, 180);
    Color strokeColor = Color::fromRGB(20, 90, 200);
    float strokeWidth = 2.f;
    bool hasFill = true;
    bool hasStroke = true;

    // ── Path points (used by Path; also start/end for Line) ──
    struct Point
    {
        float x, y;
    };
    std::vector<Point> pts;

    // ── Bounding-box params (Rect, Ellipse, Text) ─────────────────────────
    float x = 0, y = 0, w = 0, h = 0;

    // ── Text fields ───────────────────────────────────────────
    std::string text = "";
    float fontSize = 18.f;
    bool textBold = false;
    bool textItalic = false;

    // ── Selection state ───────────────────────────────────────
    bool selected = false;

    // ── Drag temporaries (not serialized into undo) ───────────
    float dragOX = 0, dragOY = 0;
    std::vector<Point> dragPts;

    // ── Cached GL image (unused for vector, kept for compat) ─
    mutable Canvas2DImage *glImage = nullptr;
};

// ============================================================
// Tool modes
// ============================================================

enum class VecTool
{
    Select = 0,
    Pen,
    Rect,
    Ellipse,
    Line,
    Text,
};

// ============================================================
// VectorSurface
// ============================================================

class VectorSurface : public RenderSurface
{
public:
    // ── Active style (applied to new shapes) ──────────────────
    Color activeFill_ = Color::fromRGBA(0, 0, 0, 0);
    Color activeStroke_ = Color::fromRGB(20, 90, 200);
    float activeStrokeW_ = 2.f;
    bool useFill_ = true;
    bool useStroke_ = true;

    // ── Active text style ─────────────────────────────────────
    float activeTextSize_ = 18.f;
    bool activeTextBold_ = false;
    bool activeTextItalic_ = false;

    VecTool activeTool_ = VecTool::Pen;
    float currentZoom_ = 1.f;

    // ── Text edit state ───────────────────────────────────────
    struct TextEdit
    {
        float x = 0, y = 0;
        std::string text;
        float fontSize = 18.f;
        bool bold = false;
        bool italic = false;
        Color color = Color::fromRGB(0, 0, 0);
        double cursorBlinkTimer = 0.0;
        bool cursorVisible = true;
    };

    bool textEditing_ = false;
    TextEdit textEdit_;

    // ── Callbacks ─────────────────────────────────────────────
    std::function<void()> onShapeCommitted;
    std::function<void(int)> onKeyDownCallback;

    // ── Commit current text edit into a permanent shape ───────
    bool commitTextEdit()
    {
        if (!textEditing_)
            return false;
        textEditing_ = false;

        if (!textEdit_.text.empty())
        {
            VecShape s;
            s.type = VecShapeType::Text;
            s.fillColor = textEdit_.color;
            s.hasFill = true;
            s.hasStroke = false;
            s.x = textEdit_.x;
            s.y = textEdit_.y;
            s.text = textEdit_.text;
            s.fontSize = textEdit_.fontSize;
            s.textBold = textEdit_.bold;
            s.textItalic = textEdit_.italic;
            // Approximate bounds for hit-test / selection handles
            s.w = float(s.text.size()) * s.fontSize * 0.55f;
            s.h = s.fontSize * 1.3f;
            pushUndo();
            shapes_.push_back(std::move(s));
            redoStack_.clear();
            if (onShapeCommitted)
                onShapeCommitted();
        }

        textEdit_.text.clear();
        return true;
    }

    // ── Undo / Redo ───────────────────────────────────────────
    bool canUndo() const { return !undoStack_.empty(); }
    bool canRedo() const { return !redoStack_.empty(); }

    void undo()
    {
        if (undoStack_.empty())
            return;
        redoStack_.push_back(shapes_);
        shapes_ = undoStack_.back();
        undoStack_.pop_back();
    }
    void redo()
    {
        if (redoStack_.empty())
            return;
        undoStack_.push_back(shapes_);
        shapes_ = redoStack_.back();
        redoStack_.pop_back();
    }
    void clear()
    {
        pushUndo();
        shapes_.clear();
        current_ = nullptr;
        drawing_ = false;
        dragSel_ = false;
        textEditing_ = false;
        textEdit_.text.clear();
    }

    // ── Shape list access ─────────────────────────────────────
    std::vector<VecShape> &shapes() { return shapes_; }
    void pushUndo_public() { pushUndo(); }

    // ── Delete selected shapes ────────────────────────────────
    void deleteSelected()
    {
        pushUndo();
        shapes_.erase(
            std::remove_if(shapes_.begin(), shapes_.end(),
                           [](const VecShape &s)
                           { return s.selected; }),
            shapes_.end());
        if (onShapeCommitted)
            onShapeCommitted();
    }

    void selectAll()
    {
        for (auto &s : shapes_)
            s.selected = true;
    }
    void deselectAll()
    {
        for (auto &s : shapes_)
            s.selected = false;
    }

    void moveSelected(float dx, float dy)
    {
        for (auto &s : shapes_)
        {
            if (!s.selected)
                continue;
            s.x += dx;
            s.y += dy;
            for (auto &p : s.pts)
            {
                p.x += dx;
                p.y += dy;
            }
        }
    }

    void bringToFront()
    {
        std::stable_partition(shapes_.begin(), shapes_.end(),
                              [](const VecShape &s)
                              { return !s.selected; });
    }
    void sendToBack()
    {
        std::stable_partition(shapes_.begin(), shapes_.end(),
                              [](const VecShape &s)
                              { return s.selected; });
    }

    int selectedCount() const
    {
        return int(std::count_if(shapes_.begin(), shapes_.end(),
                                 [](const VecShape &s)
                                 { return s.selected; }));
    }

    void applyFillToSelected(Color c)
    {
        for (auto &s : shapes_)
            if (s.selected)
            {
                s.fillColor = c;
                s.hasFill = true;
            }
    }
    void applyStrokeToSelected(Color c)
    {
        for (auto &s : shapes_)
            if (s.selected)
            {
                s.strokeColor = c;
                s.hasStroke = true;
            }
    }
    void applyStrokeWidthToSelected(float w)
    {
        for (auto &s : shapes_)
            if (s.selected)
                s.strokeWidth = w;
    }

    int canvasW() const { return w_; }
    int canvasH() const { return h_; }

    // ── RenderSurface overrides ───────────────────────────────
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
        shapes_.clear();
        undoStack_.clear();
        redoStack_.clear();
        textEditing_ = false;
        textEdit_.text.clear();
    }

    // ── Update — drives cursor blink ──────────────────────────
    void update(double dt) override
    {
        if (!textEditing_)
            return;
        textEdit_.cursorBlinkTimer += dt;
        if (textEdit_.cursorBlinkTimer >= 0.53)
        {
            textEdit_.cursorBlinkTimer = 0.0;
            textEdit_.cursorVisible = !textEdit_.cursorVisible;
        }
    }

    bool needsContinuousRedraw() const override
    {
        return rubberBanding_ || dragSel_ || textEditing_;
    }

    // ── Mouse ─────────────────────────────────────────────────
    void onMouseDown(float x, float y) override
    {
        mouseDownX_ = x;
        mouseDownY_ = y;

        // Commit any open text edit when clicking with a different tool
        if (textEditing_ && activeTool_ != VecTool::Text)
            commitTextEdit();

        // ── Select tool ───────────────────────────────────────
        if (activeTool_ == VecTool::Select)
        {
            VecShape *hit = hitTest(x, y);
            bool shiftHeld = Key::isShiftDown();
            if (hit)
            {
                if (!shiftHeld && !hit->selected)
                    deselectAll();
                hit->selected = true;
                dragSel_ = true;
                dragStartX_ = x;
                dragStartY_ = y;
                for (auto &s : shapes_)
                {
                    s.dragOX = s.x;
                    s.dragOY = s.y;
                    s.dragPts = s.pts;
                }
            }
            else
            {
                if (!shiftHeld)
                    deselectAll();
                rubberBanding_ = true;
                rubberX_ = x;
                rubberY_ = y;
                rubberW_ = 0;
                rubberH_ = 0;
            }
            return;
        }

        // ── Text tool ─────────────────────────────────────────
        if (activeTool_ == VecTool::Text)
        {
            // Clicking while editing commits the previous text
            if (textEditing_)
                commitTextEdit();

            textEditing_ = true;
            textEdit_.x = x;
            textEdit_.y = y;
            textEdit_.text = "";
            textEdit_.fontSize = activeTextSize_;
            textEdit_.bold = activeTextBold_;
            textEdit_.italic = activeTextItalic_;
            // Use fill colour if it has reasonable alpha, else stroke colour
            textEdit_.color = (activeFill_.a > 20) ? activeFill_ : activeStroke_;
            textEdit_.cursorVisible = true;
            textEdit_.cursorBlinkTimer = 0.0;
            return;
        }

        // ── Drawing tools ─────────────────────────────────────
        pushUndo();
        drawing_ = true;
        VecShape s;
        s.fillColor = activeFill_;
        s.strokeColor = activeStroke_;
        s.strokeWidth = activeStrokeW_;
        s.hasFill = useFill_;
        s.hasStroke = useStroke_;

        switch (activeTool_)
        {
        case VecTool::Pen:
            s.type = VecShapeType::Path;
            s.pts.push_back({x, y});
            break;
        case VecTool::Rect:
            s.type = VecShapeType::Rect;
            s.x = x;
            s.y = y;
            s.w = 0;
            s.h = 0;
            break;
        case VecTool::Ellipse:
            s.type = VecShapeType::Ellipse;
            s.x = x;
            s.y = y;
            s.w = 0;
            s.h = 0;
            break;
        case VecTool::Line:
            s.type = VecShapeType::Line;
            s.pts.push_back({x, y});
            s.pts.push_back({x, y});
            break;
        default:
            break;
        }
        shapes_.push_back(std::move(s));
        current_ = &shapes_.back();
    }

    void onMouseMove(float x, float y) override
    {
        if (activeTool_ == VecTool::Select)
        {
            if (dragSel_)
            {
                float dx = x - dragStartX_, dy = y - dragStartY_;
                for (auto &s : shapes_)
                {
                    if (!s.selected)
                        continue;
                    s.x = s.dragOX + dx;
                    s.y = s.dragOY + dy;
                    for (int i = 0; i < int(s.pts.size()); i++)
                    {
                        s.pts[i].x = s.dragPts[i].x + dx;
                        s.pts[i].y = s.dragPts[i].y + dy;
                    }
                }
            }
            else if (rubberBanding_)
            {
                rubberW_ = x - rubberX_;
                rubberH_ = y - rubberY_;
            }
            return;
        }

        if (activeTool_ == VecTool::Text)
            return;

        if (!drawing_ || !current_)
            return;

        switch (activeTool_)
        {
        case VecTool::Pen:
            current_->pts.push_back({x, y});
            break;
        case VecTool::Rect:
        case VecTool::Ellipse:
        {
            float bx = mouseDownX_, by = mouseDownY_;
            float dx = x - bx, dy2 = y - by;
            if (Key::isShiftDown())
            {
                float sq = std::min(std::abs(dx), std::abs(dy2));
                dx = dx < 0 ? -sq : sq;
                dy2 = dy2 < 0 ? -sq : sq;
            }
            current_->x = std::min(bx, bx + dx);
            current_->y = std::min(by, by + dy2);
            current_->w = std::abs(dx);
            current_->h = std::abs(dy2);
            break;
        }
        case VecTool::Line:
            if (current_->pts.size() >= 2)
                current_->pts[1] = {x, y};
            break;
        default:
            break;
        }
    }

    void onMouseUp(float x, float y) override
    {
        if (activeTool_ == VecTool::Select)
        {
            if (dragSel_)
            {
                dragSel_ = false;
                if (onShapeCommitted)
                    onShapeCommitted();
            }
            else if (rubberBanding_)
            {
                rubberBanding_ = false;
                float rx0 = rubberW_ < 0 ? rubberX_ + rubberW_ : rubberX_;
                float ry0 = rubberH_ < 0 ? rubberY_ + rubberH_ : rubberY_;
                float rx1 = rx0 + std::abs(rubberW_);
                float ry1 = ry0 + std::abs(rubberH_);
                for (auto &s : shapes_)
                {
                    auto [bx0, by0, bx1, by1] = shapeBounds(s);
                    if (bx0 >= rx0 && by0 >= ry0 && bx1 <= rx1 && by1 <= ry1)
                        s.selected = true;
                }
            }
            return;
        }

        if (activeTool_ == VecTool::Text)
            return;

        if (!drawing_ || !current_)
            return;
        drawing_ = false;

        switch (activeTool_)
        {
        case VecTool::Line:
            if (current_->pts.size() >= 2)
                current_->pts[1] = {x, y};
            break;
        case VecTool::Pen:
            if (current_->pts.size() < 2)
            {
                shapes_.pop_back();
                undoStack_.pop_back();
                current_ = nullptr;
                return;
            }
            break;
        default:
            break;
        }

        current_ = nullptr;
        redoStack_.clear();
        if (onShapeCommitted)
            onShapeCommitted();
    }

    void onKeyDown(const KeyEvent &e) override
    {
        // ── Text editing mode ─────────────────────────────────
        if (textEditing_)
        {
            if (e.codepoint >= 32 && e.codepoint != 127)
            {
                textEdit_.text += char(e.codepoint);
                textEdit_.cursorVisible = true;
                textEdit_.cursorBlinkTimer = 0.0;
                // Trigger redraw via onShapeCommitted (canvas watches this)
                if (onShapeCommitted)
                    onShapeCommitted();
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
                    if (onShapeCommitted)
                        onShapeCommitted();
                    break;
                case Key::Return:
                    commitTextEdit();
                    break;
                case Key::Escape:
                    textEditing_ = false;
                    textEdit_.text.clear();
                    if (onShapeCommitted)
                        onShapeCommitted();
                    break;
                default:
                    break;
                }
            }
            return; // swallow all keys while editing
        }

        // ── Normal shortcuts ──────────────────────────────────
        if (e.virtualKey == Key::Delete || e.virtualKey == Key::Backspace)
            deleteSelected();
        else if (e.ctrl && e.virtualKey == 'A')
            selectAll();
        else if (onKeyDownCallback)
            onKeyDownCallback(e.virtualKey);
    }

    void onKeyUp(const KeyEvent &) override {}

    // ── Render ────────────────────────────────────────────────
    void render(Canvas2D &ctx) override
    {
        // White canvas background
        ctx.setFillColor(Color::fromRGB(255, 255, 255));
        ctx.fillRect(0, 0, float(w_), float(h_));

        // Draw all committed shapes
        for (auto &s : shapes_)
            drawShape(ctx, s);

        // Draw in-progress shape
        if (drawing_ && current_)
            drawShape(ctx, *current_);

        // Selection handles
        if (activeTool_ == VecTool::Select)
            for (auto &s : shapes_)
                if (s.selected)
                    drawSelectionHandles(ctx, s);

        // ── Live text-edit preview ────────────────────────────
        if (textEditing_)
        {
            float fs = textEdit_.fontSize;
            float tx = textEdit_.x;
            float ty = textEdit_.y;

            // Build font descriptor
            char fontDesc[64];
            std::string prefix;
            if (textEdit_.bold && textEdit_.italic)
                prefix = "bold italic ";
            else if (textEdit_.bold)
                prefix = "bold ";
            else if (textEdit_.italic)
                prefix = "italic ";
            std::snprintf(fontDesc, sizeof(fontDesc), "%s%.0fpx sans",
                          prefix.c_str(), fs);
            ctx.setFont(fontDesc);
            ctx.setTextBaseline(TextBaseline::Top);

            float textW = ctx.measureText(textEdit_.text);
            float boxW = std::max(fs * 3.f, textW + fs * 1.2f);
            float boxH = fs * 1.4f;

            // Soft highlight box behind text
            ctx.setFillColor(Color::fromRGBA(60, 120, 220, 18));
            ctx.fillRoundedRect(tx - 3.f, ty - 3.f, boxW, boxH + 6.f, 3.f);

            // Thin underline showing baseline
            ctx.setFillColor(Color::fromRGBA(60, 120, 220, 60));
            ctx.fillRect(tx - 2.f, ty + boxH + 2.f, boxW, 1.f);

            // Typed text
            ctx.setFillColor(textEdit_.color);
            ctx.fillText(textEdit_.text, tx, ty);

            // Blinking cursor — sits right of last character
            if (textEdit_.cursorVisible)
            {
                float cursorX = tx + textW + 1.5f;
                ctx.setFillColor(textEdit_.color);
                ctx.fillRect(cursorX, ty - 1.f, 1.5f, fs + 4.f);
            }
        }

        // Rubber-band rectangle
        if (rubberBanding_)
        {
            float rx0 = rubberW_ < 0 ? rubberX_ + rubberW_ : rubberX_;
            float ry0 = rubberH_ < 0 ? rubberY_ + rubberH_ : rubberY_;
            float rw = std::abs(rubberW_);
            float rh = std::abs(rubberH_);
            ctx.setStrokeColor(Color::fromRGBA(60, 120, 220, 200));
            ctx.setLineWidth(1.f);
            ctx.setFillColor(Color::fromRGBA(60, 120, 220, 30));
            ctx.fillRect(rx0, ry0, rw, rh);
            ctx.strokeRect(rx0, ry0, rw, rh);
        }
    }

    // ── SVG export ────────────────────────────────────────────
    std::string exportSVG() const
    {
        std::string svg;
        char buf[512];
        snprintf(buf, sizeof(buf),
                 "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%d\" height=\"%d\">\n",
                 w_, h_);
        svg += buf;

        for (const auto &s : shapes_)
        {
            std::string fill = s.hasFill ? colorToHex(s.fillColor) : "none";
            std::string stroke = s.hasStroke ? colorToHex(s.strokeColor) : "none";
            float sw = s.strokeWidth;
            float fa = s.hasFill ? s.fillColor.a / 255.f : 1.f;
            float sa = s.hasStroke ? s.strokeColor.a / 255.f : 1.f;

            switch (s.type)
            {
            case VecShapeType::Rect:
                snprintf(buf, sizeof(buf),
                         "  <rect x=\"%.2f\" y=\"%.2f\" width=\"%.2f\" height=\"%.2f\" "
                         "fill=\"%s\" fill-opacity=\"%.3f\" stroke=\"%s\" stroke-opacity=\"%.3f\" stroke-width=\"%.2f\"/>\n",
                         s.x, s.y, s.w, s.h, fill.c_str(), fa, stroke.c_str(), sa, sw);
                svg += buf;
                break;

            case VecShapeType::Ellipse:
                snprintf(buf, sizeof(buf),
                         "  <ellipse cx=\"%.2f\" cy=\"%.2f\" rx=\"%.2f\" ry=\"%.2f\" "
                         "fill=\"%s\" fill-opacity=\"%.3f\" stroke=\"%s\" stroke-opacity=\"%.3f\" stroke-width=\"%.2f\"/>\n",
                         s.x + s.w * 0.5f, s.y + s.h * 0.5f, s.w * 0.5f, s.h * 0.5f,
                         fill.c_str(), fa, stroke.c_str(), sa, sw);
                svg += buf;
                break;

            case VecShapeType::Line:
                if (s.pts.size() >= 2)
                {
                    snprintf(buf, sizeof(buf),
                             "  <line x1=\"%.2f\" y1=\"%.2f\" x2=\"%.2f\" y2=\"%.2f\" "
                             "stroke=\"%s\" stroke-opacity=\"%.3f\" stroke-width=\"%.2f\"/>\n",
                             s.pts[0].x, s.pts[0].y, s.pts[1].x, s.pts[1].y,
                             stroke.c_str(), sa, sw);
                    svg += buf;
                }
                break;

            case VecShapeType::Path:
                if (s.pts.size() >= 2)
                {
                    svg += "  <polyline points=\"";
                    for (const auto &p : s.pts)
                    {
                        snprintf(buf, sizeof(buf), "%.2f,%.2f ", p.x, p.y);
                        svg += buf;
                    }
                    snprintf(buf, sizeof(buf),
                             "\" fill=\"%s\" fill-opacity=\"%.3f\" stroke=\"%s\" stroke-opacity=\"%.3f\" stroke-width=\"%.2f\"/>\n",
                             fill.c_str(), fa, stroke.c_str(), sa, sw);
                    svg += buf;
                }
                break;

            case VecShapeType::Text:
            {
                if (s.text.empty())
                    break;
                std::string styleAttr;
                if (s.textBold)
                    styleAttr += "font-weight:bold;";
                if (s.textItalic)
                    styleAttr += "font-style:italic;";
                snprintf(buf, sizeof(buf),
                         "  <text x=\"%.2f\" y=\"%.2f\" font-size=\"%.1f\" "
                         "fill=\"%s\" fill-opacity=\"%.3f\" "
                         "font-family=\"sans-serif\" dominant-baseline=\"hanging\" style=\"%s\">",
                         s.x, s.y, s.fontSize, fill.c_str(), fa, styleAttr.c_str());
                svg += buf;
                // XML-escape the text content
                for (char c : s.text)
                {
                    if (c == '<')
                        svg += "&lt;";
                    else if (c == '>')
                        svg += "&gt;";
                    else if (c == '&')
                        svg += "&amp;";
                    else if (c == '"')
                        svg += "&quot;";
                    else
                        svg += c;
                }
                svg += "</text>\n";
                break;
            }
            }
        }
        svg += "</svg>\n";
        return svg;
    }

    bool saveToSVG(const std::string &path) const
    {
        std::string svg = exportSVG();
        FILE *f = fopen(path.c_str(), "wb");
        if (!f)
            return false;
        fwrite(svg.data(), 1, svg.size(), f);
        fclose(f);
        return true;
    }

private:
    // ── Build font descriptor string ──────────────────────────
    static std::string makeFontDesc(float fontSize, bool bold, bool italic)
    {
        char buf[64];
        std::string prefix;
        if (bold && italic)
            prefix = "bold italic ";
        else if (bold)
            prefix = "bold ";
        else if (italic)
            prefix = "italic ";
        std::snprintf(buf, sizeof(buf), "%s%.0fpx sans", prefix.c_str(), fontSize);
        return buf;
    }

    // ── Draw a single VecShape ────────────────────────────────
    void drawShape(Canvas2D &ctx, const VecShape &s) const
    {
        switch (s.type)
        {
        case VecShapeType::Rect:
        {
            if (s.hasFill)
            {
                ctx.setFillColor(s.fillColor);
                ctx.fillRect(s.x, s.y, s.w, s.h);
            }
            if (s.hasStroke)
            {
                ctx.setStrokeColor(s.strokeColor);
                ctx.setLineWidth(s.strokeWidth);
                ctx.strokeRect(s.x, s.y, s.w, s.h);
            }
            break;
        }
        case VecShapeType::Ellipse:
        {
            float cx2 = s.x + s.w * 0.5f;
            float cy2 = s.y + s.h * 0.5f;
            float rx2 = s.w * 0.5f;
            float ry2 = s.h * 0.5f;
            if (s.hasFill)
            {
                ctx.setFillColor(s.fillColor);
                ctx.beginPath();
                ctx.ellipse(cx2, cy2, rx2, ry2);
                ctx.fill();
            }
            if (s.hasStroke)
            {
                ctx.setStrokeColor(s.strokeColor);
                ctx.setLineWidth(s.strokeWidth);
                ctx.beginPath();
                ctx.ellipse(cx2, cy2, rx2, ry2);
                ctx.stroke();
            }
            break;
        }
        case VecShapeType::Line:
        {
            if (s.pts.size() < 2 || !s.hasStroke)
                break;
            ctx.setStrokeColor(s.strokeColor);
            ctx.setLineWidth(s.strokeWidth);
            ctx.beginPath();
            ctx.moveTo(s.pts[0].x, s.pts[0].y);
            ctx.lineTo(s.pts[1].x, s.pts[1].y);
            ctx.stroke();
            break;
        }
        case VecShapeType::Path:
        {
            if (s.pts.size() < 2)
                break;
            if (s.hasFill)
            {
                ctx.setFillColor(s.fillColor);
                ctx.beginPath();
                ctx.moveTo(s.pts[0].x, s.pts[0].y);
                for (size_t i = 1; i < s.pts.size(); i++)
                    ctx.lineTo(s.pts[i].x, s.pts[i].y);
                ctx.fill();
            }
            if (s.hasStroke)
            {
                ctx.setStrokeColor(s.strokeColor);
                ctx.setLineWidth(s.strokeWidth);
                ctx.beginPath();
                ctx.moveTo(s.pts[0].x, s.pts[0].y);
                for (size_t i = 1; i < s.pts.size(); i++)
                    ctx.lineTo(s.pts[i].x, s.pts[i].y);
                ctx.stroke();
            }
            break;
        }
        case VecShapeType::Text:
        {
            if (s.text.empty())
                break;
            ctx.setFont(makeFontDesc(s.fontSize, s.textBold, s.textItalic));
            ctx.setTextBaseline(TextBaseline::Top);
            ctx.setFillColor(s.fillColor);
            ctx.fillText(s.text, s.x, s.y);
            break;
        }
        }
    }

    // ── Draw selection handles around a shape ─────────────────
    void drawSelectionHandles(Canvas2D &ctx, const VecShape &s) const
    {
        auto [x0, y0, x1, y1] = shapeBounds(s);
        float pad = 4.f;
        x0 -= pad;
        y0 -= pad;
        x1 += pad;
        y1 += pad;
        float bw = x1 - x0, bh = y1 - y0;

        ctx.setStrokeColor(Color::fromRGBA(60, 120, 220, 220));
        ctx.setLineWidth(1.f);
        ctx.strokeRect(x0, y0, bw, bh);

        const float hs = 5.f;
        auto drawHandle = [&](float hx, float hy)
        {
            ctx.setFillColor(Color::fromRGB(255, 255, 255));
            ctx.fillRect(hx - hs, hy - hs, hs * 2, hs * 2);
            ctx.setStrokeColor(Color::fromRGB(60, 120, 220));
            ctx.setLineWidth(1.f);
            ctx.strokeRect(hx - hs, hy - hs, hs * 2, hs * 2);
        };
        float mx = x0 + bw * 0.5f, my = y0 + bh * 0.5f;
        drawHandle(x0, y0);
        drawHandle(mx, y0);
        drawHandle(x1, y0);
        drawHandle(x0, my);
        drawHandle(x1, my);
        drawHandle(x0, y1);
        drawHandle(mx, y1);
        drawHandle(x1, y1);
    }

    // ── Bounding box of a shape ───────────────────────────────
    std::tuple<float, float, float, float> shapeBounds(const VecShape &s) const
    {
        switch (s.type)
        {
        case VecShapeType::Rect:
        case VecShapeType::Ellipse:
            return {s.x, s.y, s.x + s.w, s.y + s.h};
        case VecShapeType::Text:
            return {s.x, s.y, s.x + s.w, s.y + s.h};
        case VecShapeType::Line:
        case VecShapeType::Path:
        {
            float x0 = 1e9f, y0 = 1e9f;
            float x1 = -1e9f, y1 = -1e9f;
            for (auto &p : s.pts)
            {
                x0 = std::min(x0, p.x);
                y0 = std::min(y0, p.y);
                x1 = std::max(x1, p.x);
                y1 = std::max(y1, p.y);
            }
            return {x0, y0, x1, y1};
        }
        }
        return {0, 0, 0, 0};
    }

    // ── Hit test: returns topmost shape under (x,y) ──────────
    VecShape *hitTest(float x, float y)
    {
        float pad = 4.f;
        for (int i = int(shapes_.size()) - 1; i >= 0; i--)
        {
            auto &s = shapes_[i];
            auto [x0, y0, x1, y1] = shapeBounds(s);
            if (x >= x0 - pad && x <= x1 + pad && y >= y0 - pad && y <= y1 + pad)
                return &s;
        }
        return nullptr;
    }

    // ── Color to hex string ───────────────────────────────────
    static std::string colorToHex(Color c)
    {
        char buf[16];
        snprintf(buf, sizeof(buf), "#%02X%02X%02X", c.r, c.g, c.b);
        return buf;
    }

    // ── Undo ──────────────────────────────────────────────────
    void pushUndo()
    {
        undoStack_.push_back(shapes_);
        if (undoStack_.size() > 50)
            undoStack_.erase(undoStack_.begin());
        redoStack_.clear();
    }

    // ── State ─────────────────────────────────────────────────
    std::vector<VecShape> shapes_;
    std::vector<std::vector<VecShape>> undoStack_, redoStack_;
    VecShape *current_ = nullptr;
    bool drawing_ = false;
    float mouseDownX_ = 0, mouseDownY_ = 0;

    bool dragSel_ = false;
    float dragStartX_ = 0, dragStartY_ = 0;

    bool rubberBanding_ = false;
    float rubberX_ = 0, rubberY_ = 0, rubberW_ = 0, rubberH_ = 0;

    int w_ = 800, h_ = 600;
};

// ============================================================
// VectorApp — full UI
// ============================================================

class VectorApp : public Widget
{
    std::shared_ptr<CanvasWidget> canvas_;
    std::shared_ptr<VectorSurface> surface_;
    std::shared_ptr<ColorPickerWidget> fillPicker_;
    std::shared_ptr<ColorPickerWidget> strokePicker_;
    std::shared_ptr<FilePickerWidget> activePicker_;

    State<int> selectedTool_{0};
    State<bool> canUndo_{false}, canRedo_{false};
    State<bool> hasFill_{true}, hasStroke_{true};
    State<std::string> zoomLabel_{"100%"};
    State<std::string> selLabel_{"No selection"};
    State<std::string> canvasSizeLabel_{"800 × 600"};

    // Bold / italic button refs so we can toggle their highlight
    std::shared_ptr<ButtonWidget> boldBtn_;
    std::shared_ptr<ButtonWidget> italicBtn_;

    std::vector<std::shared_ptr<ButtonWidget>> toolBtns_;

    static constexpr Color kActiveBg = {60, 120, 220, 255};
    static constexpr Color kInactiveBg = {45, 45, 50, 255};

    void refreshUndoState()
    {
        if (surface_)
        {
            canUndo_.set(surface_->canUndo());
            canRedo_.set(surface_->canRedo());
        }
    }

    void refreshSelLabel()
    {
        if (!surface_)
            return;
        int n = surface_->selectedCount();
        if (n == 0)
            selLabel_.set("No selection");
        else if (n == 1)
            selLabel_.set("1 object selected");
        else
        {
            char buf[32];
            snprintf(buf, sizeof(buf), "%d objects selected", n);
            selLabel_.set(buf);
        }
    }

    void highlightTool(int idx)
    {
        for (int i = 0; i < int(toolBtns_.size()); i++)
            if (toolBtns_[i])
                toolBtns_[i]->setBackgroundColor(i == idx ? kActiveBg : kInactiveBg);
    }

public:
    WidgetPtr build() override
    {
        canvas_ = std::make_shared<CanvasWidget>();
        canvas_->setViewportEnabled(true);
        canvas_->setCanvasSize(800, 600);

        canvas_->onViewportChanged = [this](float zoom)
        {
            if (surface_)
                surface_->currentZoom_ = zoom;
            char buf[16];
            snprintf(buf, sizeof(buf), "%.0f%%", zoom * 100.f);
            zoomLabel_.set(buf);
        };

        surface_ = canvas_->setSurface<VectorSurface>();

        surface_->onShapeCommitted = [this]()
        {
            refreshUndoState();
            refreshSelLabel();
            canvas_->redraw();
        };

        std::weak_ptr<VectorSurface> ws = surface_;
        std::weak_ptr<CanvasWidget> wc = canvas_;

        // ── Sidebar label helper ──────────────────────────────
        auto label = [](const char *t)
        {
            return Text(t)->setFontSize(9)->setTextColor(Color::fromRGB(140, 140, 160))->setPaddingLRTB(8, 8, 8, 3);
        };

        // ── Tool definitions ──────────────────────────────────
        struct ToolDef
        {
            const char *lbl;
            VecTool tool;
            const char *tip;
        };
        const std::vector<ToolDef> toolDefs = {
            {"↖", VecTool::Select, "Select"},
            {"✏", VecTool::Pen, "Pen (freehand)"},
            {"▭", VecTool::Rect, "Rectangle"},
            {"⬭", VecTool::Ellipse, "Ellipse"},
            {"╱", VecTool::Line, "Line"},
            {"T", VecTool::Text, "Text"},
        };
        toolBtns_.resize(toolDefs.size());

        auto toolCol = Column({});
        toolCol->setSpacing(3);
        toolCol->setPadding(6);

        for (int i = 0; i < int(toolDefs.size()); i++)
        {
            auto &td = toolDefs[i];
            auto btn = Button(td.lbl)
                           ->setHeight(32)
                           ->setWidth(52)
                           ->setBackgroundColor(i == 0 ? kActiveBg : kInactiveBg)
                           ->setOnClick([this, ws, wc, i, td]()
                                        {
                    selectedTool_.set(i);
                    if (auto s = ws.lock())
                    {
                        // Commit any open text edit when switching tools
                        if (s->textEditing_) s->commitTextEdit();
                        s->activeTool_ = td.tool;
                    }
                    highlightTool(i);
                    if (auto c = wc.lock()) c->redraw(); });
            toolBtns_[i] = btn;
            toolCol->addChild(btn);
        }

        // ── Fill / stroke color pickers ───────────────────────
        Color initFill = Color::fromRGBA(0, 0, 0, 0);
        Color initStroke = Color::fromRGB(20, 90, 200);

        fillPicker_ = ColorPicker(initFill);
        fillPicker_->pickerSize = 90;
        fillPicker_->hueBarHeight = 10;
        fillPicker_->alphaBarHeight = 10;
        fillPicker_->showAlpha = true;
        fillPicker_->paddingLeft = fillPicker_->paddingRight = 6;
        fillPicker_->paddingTop = fillPicker_->paddingBottom = 4;
        fillPicker_->width = fillPicker_->pickerSize + 12;
        fillPicker_->setOnColorChanged([ws, wc](Color c)
                                       {
            if (auto s = ws.lock())
            {
                s->activeFill_ = c;
                s->applyFillToSelected(c);
            }
            if (auto ca = wc.lock()) ca->redraw(); });

        strokePicker_ = ColorPicker(initStroke);
        strokePicker_->pickerSize = 90;
        strokePicker_->hueBarHeight = 10;
        strokePicker_->alphaBarHeight = 10;
        strokePicker_->showAlpha = false;
        strokePicker_->paddingLeft = strokePicker_->paddingRight = 6;
        strokePicker_->paddingTop = strokePicker_->paddingBottom = 4;
        strokePicker_->width = strokePicker_->pickerSize + 12;
        strokePicker_->setOnColorChanged([ws, wc](Color c)
                                         {
            if (auto s = ws.lock())
            {
                s->activeStroke_ = c;
                s->applyStrokeToSelected(c);
            }
            if (auto ca = wc.lock()) ca->redraw(); });

        // ── Stroke width slider ───────────────────────────────
        auto swSlider = Slider(0.5, 30.0, 0.5);
        swSlider->value = 2.f;
        swSlider->setWidth(102);
        swSlider->setOnValueChanged([ws, wc](double v)
                                    {
            if (auto s = ws.lock())
            {
                s->activeStrokeW_ = float(v);
                s->applyStrokeWidthToSelected(float(v));
            }
            if (auto ca = wc.lock()) ca->redraw(); });

        // ── Fill / stroke toggle buttons ──────────────────────
        auto fillToggle = Button("Fill ●")
                              ->setHeight(24)
                              ->setWidth(52)
                              ->setBackgroundColor(Color::fromRGB(30, 144, 255))
                              ->setOnClick([ws, wc, this]()
                                           {
                if (auto s = ws.lock())
                {
                    s->useFill_ = !s->useFill_;
                    hasFill_.set(s->useFill_);
                }
                if (auto ca = wc.lock()) ca->redraw(); });
        auto strokeToggle = Button("Stroke ─")
                                ->setHeight(24)
                                ->setWidth(60)
                                ->setBackgroundColor(Color::fromRGB(20, 90, 200))
                                ->setOnClick([ws, wc, this]()
                                             {
                if (auto s = ws.lock())
                {
                    s->useStroke_ = !s->useStroke_;
                    hasStroke_.set(s->useStroke_);
                }
                if (auto ca = wc.lock()) ca->redraw(); });

        auto toggleRow = Row({fillToggle, strokeToggle});
        toggleRow->setSpacing(4)->setPadding(6);

        // ── Font size slider (text tool) ──────────────────────
        auto fsSlider = Slider(8.0, 96.0, 1.0);
        fsSlider->value = 18.f;
        fsSlider->setWidth(102);
        fsSlider->setOnValueChanged([ws](double v)
                                    {
            if (auto s = ws.lock()) s->activeTextSize_ = float(v); });

        // ── Bold / Italic toggles ─────────────────────────────
        boldBtn_ = Button("B")
                       ->setHeight(24)
                       ->setWidth(48)
                       ->setBackgroundColor(kInactiveBg)
                       ->setOnClick([ws, this]()
                                    {
                if (auto s = ws.lock())
                {
                    s->activeTextBold_ = !s->activeTextBold_;
                    boldBtn_->setBackgroundColor(
                        s->activeTextBold_ ? kActiveBg : kInactiveBg);
                } });

        italicBtn_ = Button("I")
                         ->setHeight(24)
                         ->setWidth(48)
                         ->setBackgroundColor(kInactiveBg)
                         ->setOnClick([ws, this]()
                                      {
                if (auto s = ws.lock())
                {
                    s->activeTextItalic_ = !s->activeTextItalic_;
                    italicBtn_->setBackgroundColor(
                        s->activeTextItalic_ ? kActiveBg : kInactiveBg);
                } });

        auto textStyleRow = Row({boldBtn_, italicBtn_});
        textStyleRow->setSpacing(4)->setPadding(6);

        // ── Arrange buttons ───────────────────────────────────
        auto bringFrontBtn = Button("↑ Front")
                                 ->setHeight(24)
                                 ->setWidth(52)
                                 ->setOnClick([ws, wc]()
                                              {
                if (auto s = ws.lock()) s->bringToFront();
                if (auto ca = wc.lock()) ca->redraw(); });
        auto sendBackBtn = Button("↓ Back")
                               ->setHeight(24)
                               ->setWidth(52)
                               ->setOnClick([ws, wc]()
                                            {
                if (auto s = ws.lock()) s->sendToBack();
                if (auto ca = wc.lock()) ca->redraw(); });
        auto deleteBtn = Button("🗑 Del")
                             ->setHeight(24)
                             ->setWidth(52)
                             ->setOnClick([ws, wc, this]()
                                          {
                if (auto s = ws.lock()) s->deleteSelected();
                refreshUndoState();
                refreshSelLabel();
                if (auto ca = wc.lock()) ca->redraw(); });

        auto arrangeRow = Row({bringFrontBtn, sendBackBtn});
        arrangeRow->setSpacing(4)->setPadding(6);

        // ── Undo / Redo ───────────────────────────────────────
        auto undoBtn = Button("↩")->setHeight(26)->setWidth(36)->setOnClick([this, ws, wc]()
                                                                            {
                if (auto s = ws.lock())
                {
                    if (s->textEditing_) s->commitTextEdit();
                    s->undo();
                }
                refreshUndoState();
                refreshSelLabel();
                if (auto ca = wc.lock()) ca->redraw(); });
        auto redoBtn = Button("↪")->setHeight(26)->setWidth(36)->setOnClick([this, ws, wc]()
                                                                            {
                if (auto s = ws.lock())
                {
                    if (s->textEditing_) s->commitTextEdit();
                    s->redo();
                }
                refreshUndoState();
                refreshSelLabel();
                if (auto ca = wc.lock()) ca->redraw(); });
        auto clearBtn = Button("Clear")->setHeight(26)->setOnClick([this, ws, wc]()
                                                                   {
                if (auto s = ws.lock()) s->clear();
                refreshUndoState();
                refreshSelLabel();
                if (auto ca = wc.lock()) ca->redraw(); });

        // ── SVG export ────────────────────────────────────────
        auto exportBtn = Button("SVG")->setHeight(26)->setWidth(50)->setOnClick([this, ws]()
                                                                                {
                // Commit any open text before exporting
                if (auto s = ws.lock()) s->commitTextEdit();

                activePicker_ = FilePicker("Export SVG");
                activePicker_->setMode(FilePickerMode::Save)
                    ->setTitle("Export as SVG")
                    ->setDefaultFilename("drawing.svg")
                    ->setDefaultExtension("svg")
                    ->addFilter("SVG File", {"*.svg"})
                    ->setOnChanged([ws](const std::string& path)
                    {
                        if (path.empty()) return;
                        if (auto s = ws.lock()) s->saveToSVG(path);
                    });
                activePicker_->open(); });

        // ── Sidebar ───────────────────────────────────────────
        auto sidebar = Container(ScrollView({
                                     label("TOOLS"),
                                     Container(toolCol)->setHeight(int(toolDefs.size()) * 38),
                                     SizedBox(0, 4),
                                     label("FILL"),
                                     fillPicker_,
                                     label("STROKE"),
                                     strokePicker_,
                                     label("STROKE WIDTH"),
                                     Container(swSlider),
                                     SizedBox(0, 2),
                                     toggleRow,
                                     label("FONT SIZE"),
                                     Container(fsSlider),
                                     label("FONT STYLE"),
                                     textStyleRow,
                                     label("ARRANGE"),
                                     arrangeRow,
                                     Container(deleteBtn),
                                 }))
                           ->setWidth(120)
                           ->setBackgroundColor(Color::fromRGB(26, 26, 28));

        // ── Toolbar ───────────────────────────────────────────
        auto toolbar = Container(
                           Row({
                                   Text("Vector")->setFontSize(13)->setTextColor(Color::fromRGB(220, 220, 220)),
                                   SizedBox(8, 0),
                                   exportBtn,
                                   SizedBox(8, 0),
                                   clearBtn,
                                   SizedBox(8, 0),
                                   undoBtn,
                                   SizedBox(4, 0),
                                   redoBtn,
                               })
                               ->setPadding(8)
                               ->setSpacing(6)
                               ->setCrossAxisAlignment(CrossAxisAlignment::Center))
                           ->setHeight(44)
                           ->setBackgroundColor(Color::fromRGB(26, 26, 28));

        // ── Status bar ────────────────────────────────────────
        auto statusBar = Container(
                             Row({
                                     Text(selLabel_, [](const std::string &s)
                                          { return s; })
                                         ->setFontSize(11)
                                         ->setTextColor(Color::fromRGB(160, 160, 160))
                                         ->setMinWidth(160),
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
                             ->setBackgroundColor(Color::fromRGB(20, 20, 22));

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
            nullptr, nullptr);
    }
};

WidgetPtr createApp(FluxUI *app)
{
    return FluxApp(
        "Vector",
        std::make_shared<VectorApp>(),
        AppTheme::dark(),
        false,
        1024, 720,
        false, true);
}