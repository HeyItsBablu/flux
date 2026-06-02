#pragma once
// ============================================================================
// flux_cad_surface.hpp  —
// ============================================================================

#include "flux/flux.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <tuple>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ============================================================================
// CadShapeType / CadShape
// ============================================================================

enum class CadShapeType
{
    Path = 0,
    Rect,
    Ellipse,
    Line,
    Text
};

struct CadShape
{
    CadShapeType type = CadShapeType::Line;
    Color strokeColor = Color::fromRGB(200, 200, 200);
    Color fillColor = Color::fromRGBA(80, 140, 220, 50);
    float strokeWidth = 1.5f;
    bool hasFill = false;
    bool hasStroke = true;

    struct Pt
    {
        float x, y;
    };
    std::vector<Pt> pts;
    float x = 0, y = 0, w = 0, h = 0;
    std::string text;
    float fontSize = 14.f;
    bool selected = false;

    float dragOX = 0, dragOY = 0;
    std::vector<Pt> dragPts;
};

// ============================================================================
// CadTool
// ============================================================================

enum class CadTool
{
    Select = 0,
    Line,
    Rect,
    Ellipse,
    Pen,
    Text
};

// ============================================================================
// CadSurface
// ============================================================================

class CadSurface : public RenderSurface
{
public:
    // ── Public config ─────────────────────────────────────────────────────
    CadTool activeTool_ = CadTool::Line;
    float currentZoom_ = 1.f;
    float viewOffsetX_ = 0.f; // updated by CadApp via onViewportChanged
    float viewOffsetY_ = 0.f;
    float viewW_ = 1280.f;
    float viewH_ = 800.f;
    bool snapToGrid_ = false;
    float gridSpacing_ = 64.f; // world units per major grid cell

    Color activeStroke_ = Color::fromRGB(200, 200, 200);
    Color activeFill_ = Color::fromRGBA(80, 140, 220, 50);
    float activeStrokeW_ = 1.5f;
    bool useStroke_ = true;
    bool useFill_ = false;
    float activeTextSize_ = 14.f;

    // ── Callbacks ─────────────────────────────────────────────────────────
    std::function<void(float, float)> onCursorMoved;
    std::function<void()> onShapeCommitted;
    std::function<void(int)> onKeyDownCallback;

    // ── Text edit state (public — accessed by CadApp) ─────────────────────
    struct TextEditState
    {
        float x = 0, y = 0;
        std::string text;
        float fontSize = 14.f;
        Color color = Color::fromRGB(200, 200, 200);
        double blinkTimer = 0.0;
        bool cursorVisible = true;
    };
    bool textEditing_ = false;
    TextEditState textEdit_;

    // ── Undo / redo ───────────────────────────────────────────────────────
    bool canUndo() const { return !undoStack_.empty(); }
    bool canRedo() const { return !redoStack_.empty(); }
    void undo()
    {
        if (!undoStack_.empty())
        {
            redoStack_.push_back(shapes_);
            shapes_ = undoStack_.back();
            undoStack_.pop_back();
        }
    }
    void redo()
    {
        if (!redoStack_.empty())
        {
            undoStack_.push_back(shapes_);
            shapes_ = redoStack_.back();
            redoStack_.pop_back();
        }
    }
    void clear()
    {
        pushUndo();
        shapes_.clear();
        current_ = nullptr;
        drawing_ = false;
        dragSel_ = false;
        textEdit_ = {};
        textEditing_ = false;
    }

    // ── Shape access ──────────────────────────────────────────────────────
    std::vector<CadShape> &shapes() { return shapes_; }

    void deleteSelected()
    {
        pushUndo();
        shapes_.erase(std::remove_if(shapes_.begin(), shapes_.end(), [](const CadShape &s)
                                     { return s.selected; }),
                      shapes_.end());
        notify();
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
    int selectedCount() const
    {
        return int(std::count_if(shapes_.begin(), shapes_.end(), [](const CadShape &s)
                                 { return s.selected; }));
    }

    void applyStrokeToSelected(Color c)
    {
        for (auto &s : shapes_)
            if (s.selected)
                s.strokeColor = c;
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
    void applyStrokeWToSelected(float w)
    {
        for (auto &s : shapes_)
            if (s.selected)
                s.strokeWidth = w;
    }

    // ── Text commit ───────────────────────────────────────────────────────
    bool commitTextEdit()
    {
        if (!textEditing_)
            return false;
        textEditing_ = false;
        if (!textEdit_.text.empty())
        {
            pushUndo();
            CadShape s;
            s.type = CadShapeType::Text;
            s.strokeColor = textEdit_.color;
            s.hasStroke = false;
            s.hasFill = false;
            s.x = textEdit_.x;
            s.y = textEdit_.y;
            s.text = textEdit_.text;
            s.fontSize = textEdit_.fontSize;
            s.w = float(s.text.size()) * s.fontSize * 0.55f;
            s.h = s.fontSize * 1.3f;
            shapes_.push_back(std::move(s));
            redoStack_.clear();
            notify();
        }
        textEdit_.text.clear();
        return true;
    }

    // ── SVG export ────────────────────────────────────────────────────────
    std::string exportSVG() const
    {
        // Compute world bounding box of all shapes to determine SVG viewport
        float bx0 = 1e9f, by0 = 1e9f, bx1 = -1e9f, by1 = -1e9f;
        for (auto &s : shapes_)
        {
            auto [x0, y0, x1, y1] = shapeBounds(s);
            bx0 = std::min(bx0, x0);
            by0 = std::min(by0, y0);
            bx1 = std::max(bx1, x1);
            by1 = std::max(by1, y1);
        }
        if (bx0 > bx1)
        {
            bx0 = 0;
            by0 = 0;
            bx1 = 800;
            by1 = 600;
        }
        float pad = 20.f;
        bx0 -= pad;
        by0 -= pad;
        bx1 += pad;
        by1 += pad;
        float svgW = bx1 - bx0, svgH = by1 - by0;

        std::string svg;
        char buf[512];
        snprintf(buf, sizeof(buf), "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"%.2f %.2f %.2f %.2f\" width=\"%.0f\" height=\"%.0f\">\n", bx0, by0, svgW, svgH, svgW, svgH);
        svg += buf;
        svg += "  <rect x=\"" + std::to_string(int(bx0)) + "\" y=\"" + std::to_string(int(by0)) + "\" width=\"100%\" height=\"100%\" fill=\"#12121a\"/>\n";

        for (const auto &s : shapes_)
        {
            std::string fill = s.hasFill ? colorHex(s.fillColor) : "none";
            std::string stroke = s.hasStroke ? colorHex(s.strokeColor) : "none";
            float fa = s.hasFill ? s.fillColor.a / 255.f : 1.f;
            float sa = s.hasStroke ? s.strokeColor.a / 255.f : 1.f;
            switch (s.type)
            {
            case CadShapeType::Rect:
                snprintf(buf, sizeof(buf), "  <rect x=\"%.2f\" y=\"%.2f\" width=\"%.2f\" height=\"%.2f\" fill=\"%s\" fill-opacity=\"%.3f\" stroke=\"%s\" stroke-opacity=\"%.3f\" stroke-width=\"%.2f\"/>\n", s.x, s.y, s.w, s.h, fill.c_str(), fa, stroke.c_str(), sa, s.strokeWidth);
                svg += buf;
                break;
            case CadShapeType::Ellipse:
                snprintf(buf, sizeof(buf), "  <ellipse cx=\"%.2f\" cy=\"%.2f\" rx=\"%.2f\" ry=\"%.2f\" fill=\"%s\" fill-opacity=\"%.3f\" stroke=\"%s\" stroke-opacity=\"%.3f\" stroke-width=\"%.2f\"/>\n", s.x + s.w * .5f, s.y + s.h * .5f, s.w * .5f, s.h * .5f, fill.c_str(), fa, stroke.c_str(), sa, s.strokeWidth);
                svg += buf;
                break;
            case CadShapeType::Line:
                if (s.pts.size() >= 2)
                {
                    snprintf(buf, sizeof(buf), "  <line x1=\"%.2f\" y1=\"%.2f\" x2=\"%.2f\" y2=\"%.2f\" stroke=\"%s\" stroke-opacity=\"%.3f\" stroke-width=\"%.2f\"/>\n", s.pts[0].x, s.pts[0].y, s.pts[1].x, s.pts[1].y, stroke.c_str(), sa, s.strokeWidth);
                    svg += buf;
                }
                break;
            case CadShapeType::Path:
                if (s.pts.size() >= 2)
                {
                    svg += "  <polyline points=\"";
                    for (auto &p : s.pts)
                    {
                        snprintf(buf, sizeof(buf), "%.2f,%.2f ", p.x, p.y);
                        svg += buf;
                    }
                    snprintf(buf, sizeof(buf), "\" fill=\"%s\" fill-opacity=\"%.3f\" stroke=\"%s\" stroke-opacity=\"%.3f\" stroke-width=\"%.2f\"/>\n", fill.c_str(), fa, stroke.c_str(), sa, s.strokeWidth);
                    svg += buf;
                }
                break;
            case CadShapeType::Text:
                if (!s.text.empty())
                {
                    snprintf(buf, sizeof(buf), "  <text x=\"%.2f\" y=\"%.2f\" font-size=\"%.1f\" fill=\"%s\" fill-opacity=\"%.3f\" font-family=\"monospace\" dominant-baseline=\"hanging\">", s.x, s.y, s.fontSize, stroke.c_str(), sa);
                    svg += buf;
                    for (char c : s.text)
                    {
                        if (c == '<')
                            svg += "&lt;";
                        else if (c == '>')
                            svg += "&gt;";
                        else if (c == '&')
                            svg += "&amp;";
                        else
                            svg += c;
                    }
                    svg += "</text>\n";
                }
                break;
            }
        }
        svg += "</svg>\n";
        return svg;
    }
    bool saveToSVG(const std::string &path) const
    {
        auto s = exportSVG();
        FILE *f = fopen(path.c_str(), "wb");
        if (!f)
            return false;
        fwrite(s.data(), 1, s.size(), f);
        fclose(f);
        return true;
    }

    // ── RenderSurface ─────────────────────────────────────────────────────
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

    void update(double dt) override
    {
        if (!textEditing_)
            return;
        textEdit_.blinkTimer += dt;
        if (textEdit_.blinkTimer >= 0.53)
        {
            textEdit_.blinkTimer = 0.0;
            textEdit_.cursorVisible = !textEdit_.cursorVisible;
        }
    }

    bool needsContinuousRedraw() const override { return rubberBanding_ || dragSel_ || textEditing_; }

    // ── Mouse ─────────────────────────────────────────────────────────────
    void onMouseDown(float x, float y) override
    {
        mouseDownX_ = x;
        mouseDownY_ = y;
        lastX_ = x;
        lastY_ = y;
        if (textEditing_ && activeTool_ != CadTool::Text)
            commitTextEdit();

        if (activeTool_ == CadTool::Select)
        {
            CadShape *hit = hitTest(x, y);
            bool shift = Key::isShiftDown();
            if (hit)
            {
                if (!shift && !hit->selected)
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
                if (!shift)
                    deselectAll();
                rubberBanding_ = true;
                rubberX_ = x;
                rubberY_ = y;
                rubberW_ = 0;
                rubberH_ = 0;
            }
            return;
        }
        if (activeTool_ == CadTool::Text)
        {
            if (textEditing_)
                commitTextEdit();
            textEditing_ = true;
            textEdit_.x = snap(x);
            textEdit_.y = snap(y);
            textEdit_.text.clear();
            textEdit_.fontSize = activeTextSize_;
            textEdit_.color = activeStroke_;
            textEdit_.cursorVisible = true;
            textEdit_.blinkTimer = 0.0;
            return;
        }

        pushUndo();
        drawing_ = true;
        CadShape s;
        s.strokeColor = activeStroke_;
        s.fillColor = activeFill_;
        s.strokeWidth = activeStrokeW_;
        s.hasStroke = useStroke_;
        s.hasFill = useFill_;
        float sx = snap(x), sy = snap(y);
        switch (activeTool_)
        {
        case CadTool::Pen:
            s.type = CadShapeType::Path;
            s.pts.push_back({sx, sy});
            break;
        case CadTool::Line:
            s.type = CadShapeType::Line;
            s.pts.push_back({sx, sy});
            s.pts.push_back({sx, sy});
            break;
        case CadTool::Rect:
            s.type = CadShapeType::Rect;
            s.x = sx;
            s.y = sy;
            s.w = 0;
            s.h = 0;
            break;
        case CadTool::Ellipse:
            s.type = CadShapeType::Ellipse;
            s.x = sx;
            s.y = sy;
            s.w = 0;
            s.h = 0;
            break;
        default:
            break;
        }
        shapes_.push_back(std::move(s));
        current_ = &shapes_.back();
    }

    void onMouseMove(float x, float y) override
    {
        curX_ = x;
        curY_ = y;
        if (onCursorMoved)
            onCursorMoved(x, y);

        if (activeTool_ == CadTool::Select)
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
        if (activeTool_ == CadTool::Text || !drawing_ || !current_)
            return;
        float sx = snap(x), sy = snap(y);
        switch (activeTool_)
        {
        case CadTool::Pen:
            current_->pts.push_back({sx, sy});
            break;
        case CadTool::Line:
            if (current_->pts.size() >= 2)
            {
                if (Key::isShiftDown())
                {
                    float bx = current_->pts[0].x, by = current_->pts[0].y, dx = sx - bx, dy = sy - by;
                    float a = std::roundf(std::atan2f(dy, dx) / (float(M_PI) * 0.25f)) * (float(M_PI) * 0.25f);
                    float len = std::sqrtf(dx * dx + dy * dy);
                    current_->pts[1] = {bx + cosf(a) * len, by + sinf(a) * len};
                }
                else
                    current_->pts[1] = {sx, sy};
            }
            break;
        case CadTool::Rect:
        case CadTool::Ellipse:
        {
            float bx = snap(mouseDownX_), by = snap(mouseDownY_), dx = sx - bx, dy = sy - by;
            if (Key::isShiftDown())
            {
                float sq = std::min(std::abs(dx), std::abs(dy));
                dx = dx < 0 ? -sq : sq;
                dy = dy < 0 ? -sq : sq;
            }
            current_->x = std::min(bx, bx + dx);
            current_->y = std::min(by, by + dy);
            current_->w = std::abs(dx);
            current_->h = std::abs(dy);
        }
        break;
        default:
            break;
        }
    }

    void onMouseUp(float x, float y) override
    {
        if (activeTool_ == CadTool::Select)
        {
            if (dragSel_)
            {
                dragSel_ = false;
                notify();
            }
            else if (rubberBanding_)
            {
                rubberBanding_ = false;
                float rx0 = rubberW_ < 0 ? rubberX_ + rubberW_ : rubberX_, ry0 = rubberH_ < 0 ? rubberY_ + rubberH_ : rubberY_;
                float rx1 = rx0 + std::abs(rubberW_), ry1 = ry0 + std::abs(rubberH_);
                for (auto &s : shapes_)
                {
                    auto [bx0, by0, bx1, by1] = shapeBounds(s);
                    if (bx0 >= rx0 && by0 >= ry0 && bx1 <= rx1 && by1 <= ry1)
                        s.selected = true;
                }
            }
            return;
        }
        if (activeTool_ == CadTool::Text || !drawing_ || !current_)
            return;
        drawing_ = false;
        if (activeTool_ == CadTool::Line && current_->pts.size() >= 2)
            current_->pts[1] = {snap(x), snap(y)};
        if (activeTool_ == CadTool::Pen && current_->pts.size() < 2)
        {
            shapes_.pop_back();
            if (!undoStack_.empty())
                undoStack_.pop_back();
            current_ = nullptr;
            return;
        }
        current_ = nullptr;
        redoStack_.clear();
        notify();
    }

    void onKeyDown(const KeyEvent &e) override
    {
        if (textEditing_)
        {
            if (e.codepoint >= 32 && e.codepoint != 127)
            {
                textEdit_.text += char(e.codepoint);
                textEdit_.cursorVisible = true;
                textEdit_.blinkTimer = 0.0;
                notify();
            }
            else
                switch (e.virtualKey)
                {
                case Key::Backspace:
                    if (!textEdit_.text.empty())
                        textEdit_.text.pop_back();
                    textEdit_.cursorVisible = true;
                    textEdit_.blinkTimer = 0.0;
                    notify();
                    break;
                case Key::Return:
                    commitTextEdit();
                    break;
                case Key::Escape:
                    textEditing_ = false;
                    textEdit_.text.clear();
                    notify();
                    break;
                default:
                    break;
                }
            return;
        }
        if (e.virtualKey == Key::Delete || e.virtualKey == Key::Backspace)
            deleteSelected();
        else if (e.ctrl && e.virtualKey == 'A')
            selectAll();
        else if (e.ctrl && e.virtualKey == 'Z')
        {
            if (e.shift)
                redo();
            else
                undo();
            notify();
        }
        else if (onKeyDownCallback)
            onKeyDownCallback(e.virtualKey);
    }
    void onKeyUp(const KeyEvent &) override {}

    // ── Render ────────────────────────────────────────────────────────────
    void render(Canvas2D &ctx) override
    {
        // ── Infinite background — fill entire visible region ──────────────
        // The canvas is huge (1<<24 world units). Fill a rect that covers
        // the visible world-space window, not just a fixed document size.
        float visX = viewOffsetX_;
        float visY = viewOffsetY_;
        float visW = viewW_ / currentZoom_;
        float visH = viewH_ / currentZoom_;

        ctx.setFillColor(Color::fromRGB(15, 15, 19));
        ctx.fillRect(visX, visY, visW, visH);

        // ── Grid (world-space lines, density adapts to zoom) ──────────────
        drawInfiniteGrid(ctx, visX, visY, visW, visH);

        // ── Axis lines at world origin ────────────────────────────────────
        drawAxes(ctx, visX, visY, visW, visH);

        // ── Shapes ───────────────────────────────────────────────────────
        for (auto &s : shapes_)
            drawShape(ctx, s);
        if (drawing_ && current_)
            drawShape(ctx, *current_);

        // ── Selection handles ─────────────────────────────────────────────
        if (activeTool_ == CadTool::Select)
            for (auto &s : shapes_)
                if (s.selected)
                    drawHandles(ctx, s);

        // ── Text preview ──────────────────────────────────────────────────
        if (textEditing_)
            drawTextPreview(ctx);

        // ── Rubber band ───────────────────────────────────────────────────
        if (rubberBanding_)
            drawRubberBand(ctx);

        // ── Crosshair ─────────────────────────────────────────────────────
        drawCrosshair(ctx);

        // ── Snap indicator ────────────────────────────────────────────────
        if (snapToGrid_ && (drawing_ || activeTool_ != CadTool::Select))
            drawSnapIndicator(ctx);
    }

private:
    // ── Infinite grid — Blender-style adaptive subdividing lines ──────────
    // We draw in world space. The grid step adapts so there are always
    // roughly 4–8 major lines visible, with a finer subdivision layer.
    void drawInfiniteGrid(Canvas2D &ctx,
                          float visX, float visY,
                          float visW, float visH) const
    {
        // Pick a step size so ~6 lines fit across the view
        float targetLines = 6.f;
        float rawStep = visW / targetLines;

        // Round to nearest power-of-2 × {1, 2, 4} multiple of gridSpacing_
        // so the grid snaps to clean world-unit multiples
        float base = gridSpacing_;
        float exp = std::floor(std::log2f(rawStep / base));
        float step = base * std::powf(2.f, exp);

        // Sub-grid: 1/4 of the major step (visible when zoomed in)
        float subStep = step / 4.f;
        float subCellPx = subStep * currentZoom_;

        float lw = 1.f / currentZoom_; // 1 screen pixel wide

        // Sub-grid lines (only when cells are ≥ 8 screen pixels)
        if (subCellPx >= 8.f)
            drawGridLines(ctx, visX, visY, visW, visH,
                          subStep, Color::fromRGBA(40, 40, 52, 255), lw);

        // Major grid lines
        drawGridLines(ctx, visX, visY, visW, visH,
                      step, Color::fromRGBA(55, 55, 70, 255), lw);
    }

    void drawGridLines(Canvas2D &ctx,
                       float visX, float visY,
                       float visW, float visH,
                       float step, Color col, float lw) const
    {
        ctx.setStrokeColor(col);
        ctx.setLineWidth(lw);

        // Vertical lines
        float startX = std::floorf(visX / step) * step;
        for (float gx = startX; gx <= visX + visW + step; gx += step)
        {
            ctx.beginPath();
            ctx.moveTo(gx, visY);
            ctx.lineTo(gx, visY + visH);
            ctx.stroke();
        }
        // Horizontal lines
        float startY = std::floorf(visY / step) * step;
        for (float gy = startY; gy <= visY + visH + step; gy += step)
        {
            ctx.beginPath();
            ctx.moveTo(visX, gy);
            ctx.lineTo(visX + visW, gy);
            ctx.stroke();
        }
    }

    // ── Axis lines ────────────────────────────────────────────────────────
    void drawAxes(Canvas2D &ctx,
                  float visX, float visY,
                  float visW, float visH) const
    {
        float lw = 1.5f / currentZoom_;

        // X axis — only if y=0 is in view
        if (visY <= 0.f && 0.f <= visY + visH)
        {
            ctx.setStrokeColor(Color::fromRGBA(200, 70, 70, 200));
            ctx.setLineWidth(lw);
            ctx.beginPath();
            ctx.moveTo(visX, 0.f);
            ctx.lineTo(visX + visW, 0.f);
            ctx.stroke();
        }
        // Y axis — only if x=0 is in view
        if (visX <= 0.f && 0.f <= visX + visW)
        {
            ctx.setStrokeColor(Color::fromRGBA(70, 200, 70, 200));
            ctx.setLineWidth(lw);
            ctx.beginPath();
            ctx.moveTo(0.f, visY);
            ctx.lineTo(0.f, visY + visH);
            ctx.stroke();
        }
    }

    // ── Crosshair cursor ──────────────────────────────────────────────────
    void drawCrosshair(Canvas2D &ctx) const
    {
        float cx = curX_, cy = curY_;
        float lw = 1.f / currentZoom_;
        float arm = 12.f / currentZoom_;
        float gap = 4.f / currentZoom_;

        ctx.setStrokeColor(Color::fromRGBA(255, 255, 255, 180));
        ctx.setLineWidth(lw);
        ctx.beginPath();
        ctx.moveTo(cx - arm, cy);
        ctx.lineTo(cx - gap, cy);
        ctx.stroke();
        ctx.beginPath();
        ctx.moveTo(cx + gap, cy);
        ctx.lineTo(cx + arm, cy);
        ctx.stroke();
        ctx.beginPath();
        ctx.moveTo(cx, cy - arm);
        ctx.lineTo(cx, cy - gap);
        ctx.stroke();
        ctx.beginPath();
        ctx.moveTo(cx, cy + gap);
        ctx.lineTo(cx, cy + arm);
        ctx.stroke();
        ctx.setFillColor(Color::fromRGBA(255, 255, 255, 200));
        ctx.fillCircle(cx, cy, lw * 1.5f);
    }

    // ── Snap indicator ────────────────────────────────────────────────────
    void drawSnapIndicator(Canvas2D &ctx) const
    {
        float sx = snap(curX_), sy = snap(curY_);
        float r = 5.f / currentZoom_, lw = 1.f / currentZoom_;
        ctx.setStrokeColor(Color::fromRGBA(80, 200, 255, 200));
        ctx.setLineWidth(lw);
        ctx.strokeRect(sx - r, sy - r, r * 2.f, r * 2.f);
    }

    // ── Shape drawing ─────────────────────────────────────────────────────
    void drawShape(Canvas2D &ctx, const CadShape &s) const
    {
        float lw = s.strokeWidth / currentZoom_;
        switch (s.type)
        {
        case CadShapeType::Rect:
            if (s.hasFill)
            {
                ctx.setFillColor(s.fillColor);
                ctx.fillRect(s.x, s.y, s.w, s.h);
            }
            if (s.hasStroke)
            {
                ctx.setStrokeColor(s.strokeColor);
                ctx.setLineWidth(lw);
                ctx.strokeRect(s.x, s.y, s.w, s.h);
            }
            break;
        case CadShapeType::Ellipse:
        {
            float cx2 = s.x + s.w * .5f, cy2 = s.y + s.h * .5f, rx2 = s.w * .5f, ry2 = s.h * .5f;
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
                ctx.setLineWidth(lw);
                ctx.beginPath();
                ctx.ellipse(cx2, cy2, rx2, ry2);
                ctx.stroke();
            }
        }
        break;
        case CadShapeType::Line:
            if (s.pts.size() >= 2 && s.hasStroke)
            {
                ctx.setStrokeColor(s.strokeColor);
                ctx.setLineWidth(lw);
                ctx.beginPath();
                ctx.moveTo(s.pts[0].x, s.pts[0].y);
                ctx.lineTo(s.pts[1].x, s.pts[1].y);
                ctx.stroke();
            }
            break;
        case CadShapeType::Path:
            if (s.pts.size() >= 2)
            {
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
                    ctx.setLineWidth(lw);
                    ctx.beginPath();
                    ctx.moveTo(s.pts[0].x, s.pts[0].y);
                    for (size_t i = 1; i < s.pts.size(); i++)
                        ctx.lineTo(s.pts[i].x, s.pts[i].y);
                    ctx.stroke();
                }
            }
            break;
        case CadShapeType::Text:
            if (!s.text.empty())
            {
                char d[32];
                snprintf(d, sizeof(d), "%.0fpx sans", s.fontSize / currentZoom_);
                ctx.setFont(d);
                ctx.setTextBaseline(TextBaseline::Top);
                ctx.setFillColor(s.strokeColor);
                ctx.fillText(s.text, s.x, s.y);
            }
            break;
        }
    }

    // ── Selection handles ─────────────────────────────────────────────────
    void drawHandles(Canvas2D &ctx, const CadShape &s) const
    {
        auto [x0, y0, x1, y1] = shapeBounds(s);
        float pad = 6.f / currentZoom_;
        x0 -= pad;
        y0 -= pad;
        x1 += pad;
        y1 += pad;
        float bw = x1 - x0, bh = y1 - y0, lw = 1.f / currentZoom_, hs = 4.f / currentZoom_;
        ctx.setStrokeColor(Color::fromRGBA(90, 160, 255, 220));
        ctx.setLineWidth(lw);
        ctx.strokeRect(x0, y0, bw, bh);
        auto handle = [&](float hx, float hy)
        {ctx.setFillColor(Color::fromRGB(30,80,180));ctx.fillRect(hx-hs,hy-hs,hs*2,hs*2);ctx.setStrokeColor(Color::fromRGB(90,160,255));ctx.setLineWidth(lw);ctx.strokeRect(hx-hs,hy-hs,hs*2,hs*2); };
        float mx = x0 + bw * .5f, my = y0 + bh * .5f;
        handle(x0, y0);
        handle(mx, y0);
        handle(x1, y0);
        handle(x0, my);
        handle(x1, my);
        handle(x0, y1);
        handle(mx, y1);
        handle(x1, y1);
    }

    // ── Text preview ──────────────────────────────────────────────────────
    void drawTextPreview(Canvas2D &ctx) const
    {
        float fs = textEdit_.fontSize / currentZoom_, tx = textEdit_.x, ty = textEdit_.y;
        char d[32];
        snprintf(d, sizeof(d), "%.0fpx sans", fs);
        ctx.setFont(d);
        ctx.setTextBaseline(TextBaseline::Top);
        float tw = ctx.measureText(textEdit_.text);
        ctx.setFillColor(Color::fromRGBA(90, 160, 255, 60));
        ctx.fillRect(tx, ty + fs * 1.1f, std::max(fs * 2.f, tw + fs), 1.f / currentZoom_);
        ctx.setFillColor(textEdit_.color);
        ctx.fillText(textEdit_.text, tx, ty);
        if (textEdit_.cursorVisible)
        {
            ctx.setFillColor(textEdit_.color);
            ctx.fillRect(tx + tw + 1.f / currentZoom_, ty - 1.f / currentZoom_, 1.5f / currentZoom_, fs + 4.f / currentZoom_);
        }
    }

    // ── Rubber band ───────────────────────────────────────────────────────
    void drawRubberBand(Canvas2D &ctx) const
    {
        float rx0 = rubberW_ < 0 ? rubberX_ + rubberW_ : rubberX_, ry0 = rubberH_ < 0 ? rubberY_ + rubberH_ : rubberY_;
        float rw = std::abs(rubberW_), rh = std::abs(rubberH_), lw = 1.f / currentZoom_;
        ctx.setFillColor(Color::fromRGBA(60, 120, 220, 25));
        ctx.fillRect(rx0, ry0, rw, rh);
        ctx.setStrokeColor(Color::fromRGBA(90, 160, 255, 200));
        ctx.setLineWidth(lw);
        ctx.strokeRect(rx0, ry0, rw, rh);
    }

    // ── Snap helper ───────────────────────────────────────────────────────
    float snap(float v) const { return snapToGrid_ ? std::roundf(v / gridSpacing_) * gridSpacing_ : v; }

    // ── Hit test ──────────────────────────────────────────────────────────
    CadShape *hitTest(float x, float y)
    {
        float pad = 6.f / currentZoom_;
        for (int i = int(shapes_.size()) - 1; i >= 0; --i)
        {
            auto &s = shapes_[i];
            auto [x0, y0, x1, y1] = shapeBounds(s);
            if (x >= x0 - pad && x <= x1 + pad && y >= y0 - pad && y <= y1 + pad)
                return &s;
        }
        return nullptr;
    }

    // ── Shape bounds ──────────────────────────────────────────────────────
    std::tuple<float, float, float, float> shapeBounds(const CadShape &s) const
    {
        switch (s.type)
        {
        case CadShapeType::Rect:
        case CadShapeType::Ellipse:
        case CadShapeType::Text:
            return {s.x, s.y, s.x + s.w, s.y + s.h};
        case CadShapeType::Line:
        case CadShapeType::Path:
        {
            float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
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
        return {0.f, 0.f, 0.f, 0.f};
    }

    void pushUndo()
    {
        undoStack_.push_back(shapes_);
        if (undoStack_.size() > 50)
            undoStack_.erase(undoStack_.begin());
        redoStack_.clear();
    }
    void notify()
    {
        if (onShapeCommitted)
            onShapeCommitted();
    }
    static std::string colorHex(Color c)
    {
        char b[16];
        snprintf(b, sizeof(b), "#%02X%02X%02X", c.r, c.g, c.b);
        return b;
    }

    // ── State ─────────────────────────────────────────────────────────────
    std::vector<CadShape> shapes_;
    std::vector<std::vector<CadShape>> undoStack_, redoStack_;
    CadShape *current_ = nullptr;
    bool drawing_ = false;
    float mouseDownX_ = 0, mouseDownY_ = 0, curX_ = 0, curY_ = 0, lastX_ = 0, lastY_ = 0;
    bool dragSel_ = false;
    float dragStartX_ = 0, dragStartY_ = 0;
    bool rubberBanding_ = false;
    float rubberX_ = 0, rubberY_ = 0, rubberW_ = 0, rubberH_ = 0;
    int w_ = 1280, h_ = 800;
};

// ============================================================================
// CadApp
// ============================================================================

class CadApp : public Widget
{
    std::shared_ptr<CanvasWidget> canvas_;
    std::shared_ptr<CadSurface> surface_;
    std::shared_ptr<FilePickerWidget> activePicker_;

    State<std::string> zoomLabel_{"100%"};
    State<std::string> cursorLabel_{"0.0,  0.0"};
    State<std::string> selLabel_{"Ready"};

    std::vector<std::shared_ptr<ButtonWidget>> toolBtns_;
    std::shared_ptr<ButtonWidget> snapBtn_;

    static constexpr Color kActiveBg = {40, 100, 220, 255};
    static constexpr Color kInactiveBg = {36, 36, 44, 255};
    static constexpr Color kSidebarBg = {22, 22, 28, 255};

    void highlightTool(int idx)
    {
        for (int i = 0; i < int(toolBtns_.size()); i++)
            if (toolBtns_[i])
                toolBtns_[i]->setBackgroundColor(i == idx ? kActiveBg : kInactiveBg);
    }

    void refreshStatus()
    {
        if (!surface_)
            return;
        int n = surface_->selectedCount();
        if (n == 0)
            selLabel_.set("No selection");
        else
        {
            char b[32];
            snprintf(b, sizeof(b), "%d object%s", n, n == 1 ? "" : "s");
            selLabel_.set(b);
        }
    }

public:
    WidgetPtr build() override
    {
        canvas_ = std::make_shared<CanvasWidget>();
        canvas_->setViewportEnabled(true);
        canvas_->setScrollbarsEnabled(false);

        // Effectively infinite world: 2^24 × 2^24 world units.
        // The Viewport clips to this, but it's large enough to never feel bounded.
        static constexpr int kWorldSize = 1 << 24;
        canvas_->setCanvasSize(kWorldSize, kWorldSize);

        canvas_->onGLResize = [this](int w, int h)
        {
            if (surface_)
            {
                surface_->viewW_ = float(w);
                surface_->viewH_ = float(h);
            }
        };

        canvas_->onViewportChanged = [this](float zoom)
        {
            if (surface_)
            {
                surface_->currentZoom_ = zoom;
                surface_->viewOffsetX_ = canvas_->viewport().offsetX();
                surface_->viewOffsetY_ = canvas_->viewport().offsetY();
            }
            char buf[16];
            snprintf(buf, sizeof(buf), "%.0f%%", zoom * 100.f);
            zoomLabel_.set(buf);
            canvas_->redraw();
        };

        surface_ = canvas_->setSurface<CadSurface>();

        surface_->onShapeCommitted = [this]()
        { refreshStatus(); canvas_->redraw(); };
        surface_->onCursorMoved = [this](float wx, float wy)
        {
            // Also update offsets every mouse move for grid accuracy
            if (surface_)
            {
                surface_->viewOffsetX_ = canvas_->viewport().offsetX();
                surface_->viewOffsetY_ = canvas_->viewport().offsetY();
            }
            char buf[48];
            snprintf(buf, sizeof(buf), "%.1f,  %.1f", wx, wy);
            cursorLabel_.set(buf);
            canvas_->redraw();
        };

        std::weak_ptr<CadSurface> ws = surface_;
        std::weak_ptr<CanvasWidget> wc = canvas_;

        // ── Tools ─────────────────────────────────────────────────────────
        struct TD
        {
            const char *lbl;
            CadTool tool;
        };
        const std::vector<TD> toolDefs = {
            {"↖", CadTool::Select},
            {"╱", CadTool::Line},
            {"▭", CadTool::Rect},
            {"⬭", CadTool::Ellipse},
            {"✏", CadTool::Pen},
            {"T", CadTool::Text},
        };
        toolBtns_.resize(toolDefs.size());
        auto toolCol = Column({});
        toolCol->setSpacing(2)->setPadding(6);
        for (int i = 0; i < int(toolDefs.size()); i++)
        {
            auto &td = toolDefs[i];
            auto btn = Button(td.lbl)->setHeight(34)->setWidth(44)->setBackgroundColor(i == 1 ? kActiveBg : kInactiveBg)->setOnClick([this, ws, wc, i, td]()
                                                                                                                                     {
                    if(auto s=ws.lock()){if(s->textEditing_)s->commitTextEdit();s->activeTool_=td.tool;}
                    highlightTool(i);
                    if(auto c=wc.lock())c->redraw(); });
            toolBtns_[i] = btn;
            toolCol->addChild(btn);
        }

        // ── Snap toggle ───────────────────────────────────────────────────
        snapBtn_ = Button("Snap")->setHeight(28)->setWidth(44)->setBackgroundColor(kInactiveBg)->setOnClick([this, ws, wc]()
                                                                                                            {
                if(auto s=ws.lock()){
                    s->snapToGrid_=!s->snapToGrid_;
                    snapBtn_->setBackgroundColor(s->snapToGrid_?kActiveBg:kInactiveBg);
                }
                if(auto c=wc.lock())c->redraw(); });

        // ── Toolbar buttons ───────────────────────────────────────────────
        auto mkBtn = [&](const char *lbl, int w, auto fn)
        { return Button(lbl)->setHeight(28)->setWidth(w)->setOnClick(fn); };

        auto undoBtn = mkBtn("↩", 30, [this, ws, wc]()
                             {if(auto s=ws.lock()){s->commitTextEdit();s->undo();}refreshStatus();if(auto c=wc.lock())c->redraw(); });
        auto redoBtn = mkBtn("↪", 30, [this, ws, wc]()
                             {if(auto s=ws.lock()){s->commitTextEdit();s->redo();}refreshStatus();if(auto c=wc.lock())c->redraw(); });
        auto clrBtn = mkBtn("Clear", 50, [this, ws, wc]()
                            {if(auto s=ws.lock())s->clear();refreshStatus();if(auto c=wc.lock())c->redraw(); });
        auto fitBtn = mkBtn("Fit", 36, [wc]()
                            {if(auto c=wc.lock()){c->viewport().fitToView();c->redraw();} });
        auto rstBtn = mkBtn("1:1", 36, [wc]()
                            {if(auto c=wc.lock()){c->viewport().resetZoom();c->redraw();} });
        auto delBtn = mkBtn("🗑", 30, [this, ws, wc]()
                            {if(auto s=ws.lock())s->deleteSelected();refreshStatus();if(auto c=wc.lock())c->redraw(); });

        auto svgBtn = mkBtn("SVG", 40, [this, ws]()
                            {
            if(auto s=ws.lock()) s->commitTextEdit();
            activePicker_=FilePicker("Export SVG");
            activePicker_->setMode(FilePickerMode::Save)->setTitle("Export SVG")
                ->setDefaultFilename("drawing.svg")->setDefaultExtension("svg")
                ->addFilter("SVG",{"*.svg"})
                ->setOnChanged([ws](const std::string&p){if(!p.empty())if(auto s=ws.lock())s->saveToSVG(p);});
            activePicker_->open(); });

        // ── Toolbar ───────────────────────────────────────────────────────
        auto toolbar = Container(
                           Row({
                                   Text("CAD")->setFontSize(12)->setTextColor(Color::fromRGB(180, 180, 200)),
                                   SizedBox(10, 0),
                                   undoBtn,
                                   SizedBox(2, 0),
                                   redoBtn,
                                   SizedBox(6, 0),
                                   clrBtn,
                                   SizedBox(6, 0),
                                   fitBtn,
                                   SizedBox(2, 0),
                                   rstBtn,
                                   SizedBox(6, 0),
                                   snapBtn_,
                                   SizedBox(6, 0),
                                   svgBtn,
                                   SizedBox(6, 0),
                                   delBtn,
                               })
                               ->setPadding(8)
                               ->setSpacing(3)
                               ->setCrossAxisAlignment(CrossAxisAlignment::Center))
                           ->setHeight(40)
                           ->setBackgroundColor(Color::fromRGB(20, 20, 26));

        // ── Sidebar ───────────────────────────────────────────────────────
        auto sidebar = Container(Column({Container(toolCol)->setHeight(int(toolDefs.size()) * 38 + 12)}))
                           ->setWidth(54)
                           ->setBackgroundColor(kSidebarBg);

        // ── Status bar ────────────────────────────────────────────────────
        auto statusBar = Container(
                             Row({
                                     Text(selLabel_, [](const std::string &s)
                                          { return s; })
                                         ->setFontSize(11)
                                         ->setTextColor(Color::fromRGB(140, 140, 160))
                                         ->setMinWidth(110),
                                     SizedBox(16, 0),
                                     Text(cursorLabel_, [](const std::string &s)
                                          { return "XY " + s; })
                                         ->setFontSize(11)
                                         ->setTextColor(Color::fromRGB(80, 200, 120)),
                                     SizedBox(16, 0),
                                     Text(zoomLabel_, [](const std::string &s)
                                          { return "Zoom " + s; })
                                         ->setFontSize(11)
                                         ->setTextColor(Color::fromRGB(140, 140, 160)),
                                     SizedBox(16, 0),
                                     Text("Mid-drag / Space: pan  ·  Ctrl+scroll: zoom  ·  Shift: constrain")
                                         ->setFontSize(10)
                                         ->setTextColor(Color::fromRGB(70, 70, 85)),
                                 })
                                 ->setPadding(5)
                                 ->setSpacing(0)
                                 ->setCrossAxisAlignment(CrossAxisAlignment::Center))
                             ->setHeight(24)
                             ->setBackgroundColor(Color::fromRGB(14, 14, 18));

        return Scaffold(nullptr,
                        Expanded(Column({toolbar, Expanded(Row({sidebar, Expanded(canvas_)})), statusBar})),
                        nullptr, nullptr);
    }
};

// ── Entry point ───────────────────────────────────────────────────────────────

WidgetPtr createApp(FluxUI *app)
{
    return FluxApp(
        "CAD Canvas",
        std::make_shared<CadApp>(),
        AppTheme::dark(),
        false,
        1280, 800,
        false, true);
}
