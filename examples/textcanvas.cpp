#pragma once
#include "flux/flux.hpp"

#include <cmath>
#include <vector>
#include <string>
#include <functional>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ============================================================
//  TextBlock
// ============================================================

// enum class TextAlign
// {
//     Left,
//     Center,
//     Right,
//     Justify
// };

// ============================================================
//  TextSpan  — character-range style override
// ============================================================

struct TextSpan
{
    int start = 0; // byte index into TextBlock::text (inclusive)
    int end = 0;   // byte index (exclusive)

    // std::nullopt = inherit from TextBlock
    std::optional<bool> bold;
    std::optional<bool> italic;
    std::optional<float> fontSize;
    std::optional<Color> color;
    std::optional<float> baselineShift;
    std::optional<std::string> fontFamily;

    // Decorations (false = inherit off)
    bool underline = false;
    bool strikethrough = false;
    bool allCaps = false;
    bool smallCaps = false;
};

struct TextBlock
{
    float x = 0, y = 0; // baseline-left in canvas coords
    float rotation = 0.f;
    std::string text;
    float fontSize = 18.f;
    std::string fontFamily = "sans"; // "sans" | "sans-bold" | "sans-italic" | "sans-bold-italic" | "mono" | "mono-bold"
    bool bold = false;
    bool italic = false;
    Color color = Color::fromRGB(20, 20, 20);
    float wrapWidth = 0.f;
    float lineHeight = 1.3f;
    float letterSpacing = 0.f;
    bool kerning = true;
    TextAlign textAlign = TextAlign::Left;
    float baselineShift = 0.f;
    std::vector<TextSpan> spans;

    // ── Appearance ────────────────────────────────────────────
    float opacity = 1.f; // 0.0 – 1.0

    // Background box
    bool bgEnabled = false;
    Color bgColor = Color::fromRGBA(255, 255, 255, 0);
    float bgPadding = 4.f; // extra space around text box

    // Stroke (outlined text)
    bool strokeEnabled = false;
    Color strokeColor = Color::fromRGB(0, 0, 0);
    float strokeWidth = 1.5f;

    // Drop shadow
    bool shadowEnabled = false;
    Color shadowColor = Color::fromRGBA(0, 0, 0, 120);
    float shadowOffsetX = 2.f;
    float shadowOffsetY = 2.f;
    float shadowBlur = 0.f; // reserved — soft shadow needs offscreen RT
};

struct ResolvedStyle
{
    int start = 0, end = 0;

    bool bold = false;
    bool italic = false;
    float fontSize = 18.f;
    Color color = {20, 20, 20, 255};
    float baselineShift = 0.f;
    std::string fontFamily = "sans";
    bool underline = false;
    bool strikethrough = false;
    bool allCaps = false;
    bool smallCaps = false;

    bool strokeEnabled = false;
    Color strokeColor = Color::fromRGB(0, 0, 0);
    float strokeWidth = 1.5f;
};

// A cubic bezier segment: start, cp1, cp2, end
struct PathSegment
{
    float x0, y0;   // start (shared with prev segment's end)
    float cx0, cy0; // control point 1
    float cx1, cy1; // control point 2
    float x1, y1;   // end
};

struct TextOnPath
{
    std::vector<PathSegment> segments; // the curve
    std::string text;
    float fontSize = 18.f;
    std::string fontFamily = "sans";
    bool bold = false;
    bool italic = false;
    Color color = Color::fromRGB(20, 20, 20);
    float startOffset = 0.f;    // offset in px along path where text begins
    float baselineOffset = 0.f; // lift/lower from path line
    std::vector<TextSpan> spans;
    // bounding / selection
    float x = 0, y = 0; // used only for initial placement

    float opacity = 1.f;
    bool strokeEnabled = false;
    Color strokeColor = Color::fromRGB(0, 0, 0);
    float strokeWidth = 1.5f;
    bool shadowEnabled = false;
    Color shadowColor = Color::fromRGBA(0, 0, 0, 120);
    float shadowOffsetX = 2.f;
    float shadowOffsetY = 2.f;
    // no bgEnabled — skipped for PathText
};

// ============================================================
//  Tool enum
// ============================================================

enum class CanvasTool
{
    Select,
    Text,
    Move,
    Scale,
    Rotate,
    MultiSelect,
    PathText
};

// ============================================================
//  TextSurface
// ============================================================

class TextSurface : public RenderSurface
{
public:
    // ── Public state ──────────────────────────────────────────
    float activeFontSize_ = 18.f;
    std::string activeFontFamily_ = "sans";
    bool activeBold_ = false;
    bool activeItalic_ = false;
    Color activeColor_ = Color::fromRGB(20, 20, 20);
    float currentZoom_ = 1.f;
    CanvasTool activeTool_ = CanvasTool::Select;
    float activeLineHeight_ = 1.3f;
    float activeLetterSpacing_ = 0.f;
    bool activeKerning_ = true;
    TextAlign activeTextAlign_ = TextAlign::Left;
    float activeBaselineShift_ = 0.f;
    bool shiftHeld_ = false;

    // Callbacks
    std::function<void()> onCommitted;
    std::function<void(float, float)> onMousePos;
    std::function<void()> onSelectionChanged; // block selection changed

    // ── Span editing ──────────────────────────────────────────
    bool hasSelection() const { return selStart_ != selEnd_; }
    // Returns font size of the first run overlapping the selection,
    // or 0 if no selection / not editing
    float selectionFontSize() const
    {
        if (!editing_ || !hasSelection())
            return 0.f;
        int lo = std::min(selStart_, selEnd_);
        int hi = std::max(selStart_, selEnd_);
        TextBlock tmp = makeEditTmp();
        auto runs = normalizeSpans(tmp);
        for (auto &rs : runs)
        {
            if (rs.end <= lo || rs.start >= hi)
                continue;
            return rs.fontSize;
        }
        return activeFontSize_;
    }

    // Normalize b.spans in-place — call after any span mutation
    void normalizeBlockSpans(TextBlock &b) const
    {
        int n = int(b.text.size());
        if (n == 0)
        {
            b.spans.clear();
            return;
        }

        // Get the resolved runs
        auto runs = normalizeSpans(b);

        // Convert back to minimal span list —
        // merge adjacent runs that are identical to block defaults
        b.spans.clear();
        for (auto &rs : runs)
        {
            bool matchesBlock =
                rs.bold == b.bold &&
                rs.italic == b.italic &&
                std::abs(rs.fontSize - b.fontSize) < 0.5f &&
                rs.color.r == b.color.r &&
                rs.color.g == b.color.g &&
                rs.color.b == b.color.b &&
                rs.color.a == b.color.a &&
                std::abs(rs.baselineShift - b.baselineShift) < 0.5f &&
                rs.fontFamily == b.fontFamily &&
                !rs.underline && !rs.strikethrough &&
                !rs.allCaps && !rs.smallCaps;

            if (matchesBlock)
                continue; // no span needed

            TextSpan sp;
            sp.start = rs.start;
            sp.end = rs.end;
            sp.bold = rs.bold;
            sp.italic = rs.italic;
            sp.fontSize = rs.fontSize;
            sp.color = rs.color;
            sp.baselineShift = rs.baselineShift;
            sp.fontFamily = rs.fontFamily;
            sp.underline = rs.underline;
            sp.strikethrough = rs.strikethrough;
            sp.allCaps = rs.allCaps;
            sp.smallCaps = rs.smallCaps;
            b.spans.push_back(sp);
        }
    }

    template <typename Fn>
    void applyToSelectedBlock(Fn fn)
    {
        if (selectedIdx_ < 0 || selectedIdx_ >= int(blocks_.size()))
            return;
        pushUndo();
        fn(blocks_[selectedIdx_]);
        if (onCommitted)
            onCommitted();
    }

    template <typename Fn>
    bool selectionBoolQueryPublic(Fn fn) const
    {
        return selectionBoolQuery(fn);
    }

    // Apply a style mutation to the current text selection.
    // fn receives a ResolvedStyle& and modifies it.
    // Works on the edit buffer (editingExisting_ or new block preview).
    template <typename Fn>
    void applyStyleToSelection(Fn fn)
    {
        if (!editing_ || !hasSelection())
            return;

        if (editingExisting_ && selectedIdx_ >= 0 &&
            selectedIdx_ < int(blocks_.size()))
        {
            auto &src = blocks_[selectedIdx_];
            if (src.spans.empty() && (src.bold || src.italic))
            {
                // Bake block-level style into a full-coverage span,
                // then reset block base to defaults
                TextSpan full;
                full.start = 0;
                full.end = int(src.text.size());
                full.bold = src.bold;
                full.italic = src.italic;
                full.color = src.color;
                full.fontSize = src.fontSize;
                full.baselineShift = src.baselineShift;
                full.fontFamily = src.fontFamily;
                src.spans.push_back(full);

                // Reset block base to neutral defaults
                src.bold = false;
                src.italic = false;
            }
        }
        // Also handle new blocks with pendingSpans empty but activeBold set
        else if (!editingExisting_ && pendingSpans_.empty() && activeBold_)
        {
            int n = int(editBuf_.size());
            TextSpan full;
            full.start = 0;
            full.end = n;
            full.bold = true;
            full.italic = activeItalic_;
            full.color = activeColor_;
            full.fontSize = activeFontSize_;
            full.baselineShift = activeBaselineShift_;
            full.fontFamily = activeFontFamily_;
            pendingSpans_.push_back(full);
            activeBold_ = false; // reset base
            activeItalic_ = false;
        }
        // ── End bootstrap ────────────────────────────────────────

        int lo = std::min(selStart_, selEnd_);
        int hi = std::max(selStart_, selEnd_);

        // After the bootstrap block, before makeEditTmp()
        if (editingExisting_ && selectedIdx_ >= 0 && selectedIdx_ < int(blocks_.size()))
        {
            auto &src = blocks_[selectedIdx_];
        }

        TextBlock tmp = makeEditTmp();

        auto runs = normalizeSpans(tmp);

        // Apply fn to every run that overlaps [lo, hi)
        // Split runs at selection boundaries before applying fn
        std::vector<ResolvedStyle> splitRuns;
        for (auto &rs : runs)
        {
            if (rs.end <= lo || rs.start >= hi)
            {
                // Outside selection — keep as-is
                splitRuns.push_back(rs);
                continue;
            }

            // Split into up to 3 parts: before, inside, after selection
            if (rs.start < lo)
            {
                ResolvedStyle pre = rs;
                pre.end = lo;
                splitRuns.push_back(pre);
            }

            // Inside selection — apply fn to this portion
            ResolvedStyle mid = rs;
            mid.start = std::max(rs.start, lo);
            mid.end = std::min(rs.end, hi);
            fn(mid);
            splitRuns.push_back(mid);

            if (rs.end > hi)
            {
                ResolvedStyle post = rs;
                post.start = hi;
                splitRuns.push_back(post);
            }
        }
        runs = splitRuns;

        // Write runs back as spans
        tmp.spans.clear();

        for (auto &rs : runs)
        {

            TextSpan sp;
            sp.start = rs.start;
            sp.end = rs.end;
            sp.bold = rs.bold;
            sp.italic = rs.italic;
            sp.fontSize = rs.fontSize;
            sp.color = rs.color;
            sp.baselineShift = rs.baselineShift;
            sp.fontFamily = rs.fontFamily;
            sp.underline = rs.underline;
            sp.strikethrough = rs.strikethrough;
            sp.allCaps = rs.allCaps;
            sp.smallCaps = rs.smallCaps;
            tmp.spans.push_back(sp);
        }

        normalizeBlockSpans(tmp);

        if (editingExisting_ && selectedIdx_ >= 0 &&
            selectedIdx_ < int(blocks_.size()))
            blocks_[selectedIdx_].spans = tmp.spans;

        pendingSpans_ = tmp.spans;

        if (onCommitted)
            onCommitted();
    }

    // Query: what is the state of a bool property across the selection?
    // Returns true only if ALL runs in selection have the property set.
    bool selectionAllBold() const
    {
        return selectionBoolQuery([](const ResolvedStyle &rs)
                                  { return rs.bold; });
    }
    bool selectionAllItalic() const
    {
        return selectionBoolQuery([](const ResolvedStyle &rs)
                                  { return rs.italic; });
    }
    bool selectionAllUnderline() const
    {
        return selectionBoolQuery([](const ResolvedStyle &rs)
                                  { return rs.underline; });
    }
    bool selectionAllStrikethrough() const
    {
        return selectionBoolQuery([](const ResolvedStyle &rs)
                                  { return rs.strikethrough; });
    }

    // Returns the color if uniform across selection, nullopt if mixed
    std::optional<Color> selectionColor() const
    {
        if (!editing_ || !hasSelection())
            return std::nullopt;
        int lo = std::min(selStart_, selEnd_);
        int hi = std::max(selStart_, selEnd_);
        TextBlock tmp = makeEditTmp();
        auto runs = normalizeSpans(tmp);
        std::optional<Color> result;
        for (auto &rs : runs)
        {
            if (rs.end <= lo || rs.start >= hi)
                continue;
            if (!result)
            {
                result = rs.color;
                continue;
            }
            if (rs.color.r != result->r || rs.color.g != result->g ||
                rs.color.b != result->b)
                return std::nullopt;
        }
        return result;
    }

    // Returns true if more than one block is selected
    bool hasMultiSelection() const { return selectedIndices_.size() > 1; }

    const std::vector<int> &selectedIndices() const { return selectedIndices_; }

    void clearMultiSelection()
    {
        selectedIndices_.clear();
        selectedIdx_ = -1;
    }

    // Apply a style lambda to every selected block
    // Usage: applyToSelected([](TextBlock& b){ b.fontSize = 24; });
    template <typename Fn>
    void applyToSelected(Fn fn)
    {
        if (selectedIndices_.empty())
            return;
        pushUndo();
        for (int idx : selectedIndices_)
        {
            if (idx >= 0 && idx < int(blocks_.size()))
                fn(blocks_[idx]);
        }
        if (onCommitted)
            onCommitted();
    }

    void deleteAllSelected()
    {
        if (selectedIndices_.empty())
            return;
        pushUndo();
        // Sort descending so erasing by index doesn't shift remaining
        auto sorted = selectedIndices_;
        std::sort(sorted.begin(), sorted.end(), std::greater<int>());
        for (int idx : sorted)
            blocks_.erase(blocks_.begin() + idx);
        selectedIndices_.clear();
        selectedIdx_ = -1;
        if (onCommitted)
            onCommitted();
    }

    void resetSelectedRotation()
    {
        if (selectedIdx_ < 0 || selectedIdx_ >= int(blocks_.size()))
            return;
        pushUndo();
        blocks_[selectedIdx_].rotation = 0.f;
        if (onCommitted)
            onCommitted();
    }

    bool hasEditContent() const { return !editBuf_.empty(); }
    void cancelEdit()
    {
        editing_ = false;
        editingExisting_ = false;
        editBuf_.clear();
        selStart_ = selEnd_ = 0;
        pendingSpans_.clear();
    }

    // ── Undo / redo ───────────────────────────────────────────
    bool canUndo() const { return !undoStack_.empty(); }
    bool canRedo() const { return !redoStack_.empty(); }

    void undo()
    {
        if (undoStack_.empty())
            return;
        redoStack_.push_back(blocks_);
        blocks_ = undoStack_.back();
        undoStack_.pop_back();
        pathBlocks_ = undoStackPath_.back();
        undoStackPath_.pop_back();
        cancelEdit();
    }
    void redo()
    {
        if (redoStack_.empty())
            return;
        undoStack_.push_back(blocks_);
        undoStackPath_.push_back(pathBlocks_);
        blocks_ = redoStack_.back();
        redoStack_.pop_back();
        pathBlocks_ = redoStackPath_.back();
        redoStackPath_.pop_back();
        cancelEdit();
    }
    void clearAll()
    {
        pushUndo();
        blocks_.clear();
        cancelEdit();
        selectedIdx_ = -1;
        if (onCommitted)
            onCommitted();
    }

    // ── Selection (for tool UI reflection) ────────────────────
    int selectedBlockIndex() const { return selectedIdx_; }

    // Returns the block currently selected (Select tool) or being edited (Text tool)
    const TextBlock *selectedBlock() const
    {
        if (selectedIdx_ < 0 || selectedIdx_ >= int(blocks_.size()))
            return nullptr;
        return &blocks_[selectedIdx_];
    }

    // ── Text editing ──────────────────────────────────────────
    bool isEditing() const { return editing_; }
    int blockCount() const { return int(blocks_.size()); }

    bool commitEdit()
    {
        if (!editing_)
            return false;
        if (!editBuf_.empty())
        {
            if (editingExisting_ && selectedIdx_ >= 0 &&
                selectedIdx_ < int(blocks_.size()))
            {
                applyEditToBlock(blocks_[selectedIdx_]);
            }
            else
            {
                pushUndo();
                TextBlock b;
                b.x = editX_;
                b.y = editY_;
                applyEditToBlock(b);
                b.spans = pendingSpans_;
                pendingSpans_.clear();
                blocks_.push_back(b);
                selectedIdx_ = int(blocks_.size()) - 1;
            }
            if (onCommitted)
                onCommitted();
        }
        editing_ = false;
        editingExisting_ = false;
        editBuf_.clear();
        selStart_ = selEnd_ = 0;
        return true;
    }

    // Delete selected block (Select tool)
    void deleteSelected()
    {
        if (selectedIdx_ < 0 || selectedIdx_ >= int(blocks_.size()))
            return;
        pushUndo();
        blocks_.erase(blocks_.begin() + selectedIdx_);
        selectedIdx_ = -1;
        if (onCommitted)
            onCommitted();
    }

    // ── RenderSurface ─────────────────────────────────────────
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
        blocks_.clear();
        undoStack_.clear();
        redoStack_.clear();
        cancelEdit();
    }

    void update(double dt) override
    {
        if (editing_)
        {
            blinkTimer_ += dt;
            if (blinkTimer_ >= 0.53)
            {
                blinkTimer_ = 0.0;
                cursorVisible_ = !cursorVisible_;
            }
        }
        wallTime_ += dt;
    }

    bool needsContinuousRedraw() const override { return editing_; }

    // ── Mouse ─────────────────────────────────────────────────
    void onMouseDown(float x, float y) override
    {
        if (onMousePos)
            onMousePos(x, y);
        mouseDown_ = true;
        dragStartX_ = x;
        dragStartY_ = y;

        // ── Wrap handle drag (Select tool, selected block) ────
        if (activeTool_ == CanvasTool::Select &&
            selectedIdx_ >= 0 && selectedIdx_ < int(blocks_.size()))
        {
            auto &b = blocks_[selectedIdx_];
            if (hitWrapHandle(x, y, b))
            {
                draggingWrapHandle_ = true;
                wrapHandleIdx_ = selectedIdx_;
                wrapHandleDragStartX_ = x;
                wrapHandleStartW_ = b.wrapWidth > 0.f
                                        ? b.wrapWidth
                                        : approxTextWidth(b.text, b.fontSize);
                return;
            }
        }

        if (activeTool_ == CanvasTool::Select)
        {
            int hit = hitTestBlock(x, y);
            if (hit != selectedIdx_)
            {
                selectedIdx_ = hit;
                if (onSelectionChanged)
                    onSelectionChanged();
            }
            return;
        }

        if (activeTool_ == CanvasTool::Move)
        {
            int hit = hitTestBlock(x, y);
            if (hit >= 0)
            {
                selectedIdx_ = hit;
                draggingBlock_ = true;
                dragMouseStartX_ = x;
                dragMouseStartY_ = y;
                dragBlockStartX_ = blocks_[hit].x;
                dragBlockStartY_ = blocks_[hit].y;
                pushUndo();
                if (onSelectionChanged)
                    onSelectionChanged();
            }
            else
            {
                selectedIdx_ = -1;
                if (onSelectionChanged)
                    onSelectionChanged();
            }
            return;
        }

        if (activeTool_ == CanvasTool::Scale)
        {
            // Check scale handle first (only if a block is already selected)
            if (selectedIdx_ >= 0 && selectedIdx_ < int(blocks_.size()))
            {
                if (hitScaleHandle(x, y, blocks_[selectedIdx_], selectedIdx_))
                {
                    draggingScale_ = true;
                    scaleMouseStartX_ = x;
                    scaleMouseStartY_ = y;
                    scaleStartWrapW_ = blocks_[selectedIdx_].wrapWidth > 0.f
                                           ? blocks_[selectedIdx_].wrapWidth
                                           : approxTextWidth(blocks_[selectedIdx_].text,
                                                             blocks_[selectedIdx_].fontSize);
                    scaleStartFontSize_ = blocks_[selectedIdx_].fontSize;
                    pushUndo();
                    return;
                }
            }

            // Otherwise select a block
            int hit = hitTestBlock(x, y);
            if (hit != selectedIdx_)
            {
                selectedIdx_ = hit;
                if (onSelectionChanged)
                    onSelectionChanged();
            }
            return;
        }

        if (activeTool_ == CanvasTool::Rotate)
        {
            // Check rotate handle first
            if (selectedIdx_ >= 0 && selectedIdx_ < int(blocks_.size()))
            {
                if (hitRotateHandle(x, y, blocks_[selectedIdx_]))
                {
                    auto &b = blocks_[selectedIdx_];
                    float cx, cy;
                    blockCenter(b, cx, cy);
                    rotateCenterX_ = cx;
                    rotateCenterY_ = cy;
                    rotateStartAngle_ = b.rotation;
                    rotateMouseStartAngle_ = atan2f(y - cy, x - cx);
                    draggingRotate_ = true;
                    pushUndo();
                    return;
                }
            }

            // Otherwise select a block
            int hit = hitTestBlock(x, y);
            if (hit != selectedIdx_)
            {
                selectedIdx_ = hit;
                if (onSelectionChanged)
                    onSelectionChanged();
            }
            return;
        }

        if (activeTool_ == CanvasTool::MultiSelect)
        {
            int hit = hitTestBlock(x, y);

            if (hit >= 0)
            {
                // Shift-click: toggle this block in/out of selection
                if (shiftHeld_)
                {
                    auto it = std::find(selectedIndices_.begin(),
                                        selectedIndices_.end(), hit);
                    if (it != selectedIndices_.end())
                        selectedIndices_.erase(it);
                    else
                        selectedIndices_.push_back(hit);
                }
                else
                {
                    // If clicking an already-selected block, start multi-drag
                    bool alreadySelected = std::find(selectedIndices_.begin(),
                                                     selectedIndices_.end(), hit) != selectedIndices_.end();

                    if (!alreadySelected)
                    {
                        selectedIndices_.clear();
                        selectedIndices_.push_back(hit);
                    }

                    // Start drag for all selected
                    draggingMulti_ = true;
                    multiDragStartX_ = x;
                    multiDragStartY_ = y;
                    multiDragOrigins_.clear();
                    for (int idx : selectedIndices_)
                    {
                        multiDragOrigins_.push_back({blocks_[idx].x,
                                                     blocks_[idx].y});
                    }
                    pushUndo();
                }

                selectedIdx_ = hit; // keep single-select in sync for sidebar
                if (onSelectionChanged)
                    onSelectionChanged();
            }
            else
            {
                // Start rubber band
                selectedIndices_.clear();
                selectedIdx_ = -1;
                rubberBanding_ = true;
                rubberX0_ = rubberX1_ = x;
                rubberY0_ = rubberY1_ = y;
                if (onSelectionChanged)
                    onSelectionChanged();
            }
            return;
        }

        if (activeTool_ == CanvasTool::PathText)
        {
            if (!drawingPath_)
            {
                // Start a new path
                drawingPath_ = true;
                pendingPath_ = TextOnPath{};
                pendingPath_.color = activeColor_;
                pendingPath_.fontSize = activeFontSize_;
                pendingPath_.fontFamily = activeFontFamily_;
                pendingPath_.bold = activeBold_;
                pendingPath_.italic = activeItalic_;
                // Place first anchor; real segment added on second click
                pendingPath_.x = x;
                pendingPath_.y = y;
                // Stash first anchor temporarily in a dummy segment
                PathSegment s{};
                s.x0 = s.cx0 = s.cx1 = s.x1 = x;
                s.y0 = s.cy0 = s.cy1 = s.y1 = y;
                pendingPath_.segments.push_back(s);
            }
            else
            {
                // Extend the path: convert last dummy to real segment
                auto &last = pendingPath_.segments.back();
                // Use ctrl-point heuristic: cp1 = 1/3, cp2 = 2/3 along straight line
                float prevX = last.x0, prevY = last.y0;
                last.cx0 = prevX + (x - prevX) * 0.33f;
                last.cy0 = prevY + (y - prevY) * 0.33f;
                last.cx1 = prevX + (x - prevX) * 0.67f;
                last.cy1 = prevY + (y - prevY) * 0.67f;
                last.x1 = x;
                last.y1 = y;

                // Push next dummy segment starting here
                PathSegment next{};
                next.x0 = next.cx0 = next.cx1 = next.x1 = x;
                next.y0 = next.cy0 = next.cy1 = next.y1 = y;
                pendingPath_.segments.push_back(next);
            }
            return;
        }

        // Text tool
        int hit = hitTestBlock(x, y);
        if (hit >= 0)
        {
            if (editing_)
                commitEdit();
            pushUndo();
            selectedIdx_ = hit;
            editingExisting_ = true;
            editing_ = true;
            const auto &b = blocks_[hit];
            editBuf_ = b.text;
            editX_ = b.x;
            editY_ = b.y;
            activeFontSize_ = b.fontSize;
            activeFontFamily_ = b.fontFamily;
            activeBold_ = b.bold;
            activeItalic_ = b.italic;
            activeColor_ = b.color;
            activeLineHeight_ = b.lineHeight;
            activeLetterSpacing_ = b.letterSpacing;
            activeKerning_ = b.kerning;
            activeTextAlign_ = b.textAlign;
            activeBaselineShift_ = b.baselineShift;

            int clickIdx = cursorIndexAt(x, b);
            selStart_ = selEnd_ = clickIdx;

            double now = wallTime_;
            float ddx = x - lastClickX_;
            float ddy = y - lastClickY_;
            bool sameSpot = (ddx * ddx + ddy * ddy) < 16.f;
            bool doubleClick = sameSpot && (now - lastClickTime_) < 0.4;

            lastClickTime_ = now;
            lastClickX_ = x;
            lastClickY_ = y;

            if (doubleClick)
            {
                auto [lo, hi] = wordBoundsAt(clickIdx);
                selStart_ = lo;
                selEnd_ = hi;
            }
            resetBlink();
        }
        else
        {
            commitEdit();
            pushUndo();
            editing_ = true;
            editingExisting_ = false;
            selectedIdx_ = -1;
            editX_ = x;
            editY_ = y;
            editBuf_.clear();
            selStart_ = selEnd_ = 0;
            resetBlink();
        }
    }

    void onMouseMove(float x, float y) override
    {
        if (onMousePos)
            onMousePos(x, y);
        hoverX_ = x;
        hoverY_ = y;

        // ── Wrap handle drag ──────────────────────────────────
        if (draggingWrapHandle_ && wrapHandleIdx_ >= 0 &&
            wrapHandleIdx_ < int(blocks_.size()))
        {
            float newW = wrapHandleStartW_ + (x - wrapHandleDragStartX_);
            blocks_[wrapHandleIdx_].wrapWidth = std::max(newW, blocks_[wrapHandleIdx_].fontSize * 2.f);
            return;
        }

        if (draggingBlock_ && activeTool_ == CanvasTool::Move &&
            selectedIdx_ >= 0 && selectedIdx_ < int(blocks_.size()))
        {
            blocks_[selectedIdx_].x = dragBlockStartX_ + (x - dragMouseStartX_);
            blocks_[selectedIdx_].y = dragBlockStartY_ + (y - dragMouseStartY_);
            if (onCommitted)
                onCommitted();
            return;
        }

        if (draggingScale_ && selectedIdx_ >= 0 &&
            selectedIdx_ < int(blocks_.size()))
        {
            auto &b = blocks_[selectedIdx_];
            float dx = x - scaleMouseStartX_;
            float dy = y - scaleMouseStartY_;

            // Horizontal drag -> wrapWidth
            float newW = std::max(scaleStartWrapW_ + dx, b.fontSize * 2.f);
            b.wrapWidth = newW;

            // Vertical drag -> fontSize (drag down = bigger, up = smaller)
            float newFs = std::clamp(scaleStartFontSize_ + dy * 0.3f, 6.f, 200.f);
            b.fontSize = newFs;

            // Keep surface active style in sync so sidebar reflects live values
            activeFontSize_ = newFs;
            if (onCommitted)
                onCommitted();
            return;
        }

        if (draggingRotate_ && selectedIdx_ >= 0 &&
            selectedIdx_ < int(blocks_.size()))
        {
            float currentAngle = atan2f(y - rotateCenterY_, x - rotateCenterX_);
            float delta = currentAngle - rotateMouseStartAngle_;
            blocks_[selectedIdx_].rotation = rotateStartAngle_ + delta;
            if (onCommitted)
                onCommitted();
            return;
        }

        // Multi-block drag
        if (draggingMulti_ && activeTool_ == CanvasTool::MultiSelect)
        {
            float dx = x - multiDragStartX_;
            float dy = y - multiDragStartY_;
            for (int i = 0; i < int(selectedIndices_.size()); ++i)
            {
                int idx = selectedIndices_[i];
                if (idx >= 0 && idx < int(blocks_.size()))
                {
                    blocks_[idx].x = multiDragOrigins_[i].first + dx;
                    blocks_[idx].y = multiDragOrigins_[i].second + dy;
                }
            }
            if (onCommitted)
                onCommitted();
            return;
        }

        // Rubber band update
        if (rubberBanding_ && activeTool_ == CanvasTool::MultiSelect)
        {
            rubberX1_ = x;
            rubberY1_ = y;

            // Live-update selection as band grows
            float rx0 = std::min(rubberX0_, rubberX1_);
            float ry0 = std::min(rubberY0_, rubberY1_);
            float rx1 = std::max(rubberX0_, rubberX1_);
            float ry1 = std::max(rubberY0_, rubberY1_);

            selectedIndices_.clear();
            for (int i = 0; i < int(blocks_.size()); ++i)
            {
                const auto &b = blocks_[i];
                float cx, cy;
                blockCenter(b, cx, cy);
                if (cx >= rx0 && cx <= rx1 && cy >= ry0 && cy <= ry1)
                    selectedIndices_.push_back(i);
            }
            if (!selectedIndices_.empty())
                selectedIdx_ = selectedIndices_.back();
            else
                selectedIdx_ = -1;

            if (onSelectionChanged)
                onSelectionChanged();
            return;
        }

        // Drag-select within text
        if (mouseDown_ && editing_ && editingExisting_ &&
            selectedIdx_ >= 0 && selectedIdx_ < int(blocks_.size()))
        {
            int idx = cursorIndexAt(x, blocks_[selectedIdx_]);
            selEnd_ = idx;
            resetBlink();
        }
    }

    void onMouseUp(float x, float y) override
    {
        if (draggingBlock_)
        {
            draggingBlock_ = false;
            return;
        }
        if (draggingScale_)
        {
            draggingScale_ = false;
            // Sync sidebar font size display
            if (onSelectionChanged)
                onSelectionChanged();
            return;
        }

        if (draggingRotate_)
        {
            draggingRotate_ = false;
            return;
        }

        if (rubberBanding_)
        {
            rubberBanding_ = false;
            return;
        }
        if (draggingMulti_)
        {
            draggingMulti_ = false;
            multiDragOrigins_.clear();
            return;
        }
        if (draggingWrapHandle_)
        {
            draggingWrapHandle_ = false;
            wrapHandleIdx_ = -1;
            if (onCommitted)
                onCommitted();
            return;
        }
        mouseDown_ = false;
    }

    // ── Keyboard ──────────────────────────────────────────────
    void onKeyDown(const KeyEvent &e) override
    {
        shiftHeld_ = e.shift;

        // ── PathText tool — fully self-contained input ────────────
        if (activeTool_ == CanvasTool::PathText && drawingPath_)
        {
            if (e.virtualKey == Key::Return)
            {
                if (!pendingPath_.segments.empty())
                    pendingPath_.segments.pop_back();
                if (!pendingPath_.segments.empty() && !editBuf_.empty())
                {
                    pendingPath_.text = editBuf_;
                    pushUndo();
                    pathBlocks_.push_back(pendingPath_);
                }
                drawingPath_ = false;
                pendingPath_ = {};
                editBuf_.clear();
                if (onCommitted)
                    onCommitted();
                return;
            }

            if (e.virtualKey == Key::Escape)
            {
                drawingPath_ = false;
                pendingPath_ = {};
                editBuf_.clear();
                if (onCommitted)
                    onCommitted();
                return;
            }

            if (e.virtualKey == Key::Backspace)
            {
                if (!editBuf_.empty())
                    editBuf_.pop_back();
                pendingPath_.text = editBuf_;
                if (onCommitted)
                    onCommitted();
                return;
            }

            if (e.codepoint >= 32 && e.codepoint != 127)
            {
                editBuf_ += char(e.codepoint);
                pendingPath_.text = editBuf_;
                if (onCommitted)
                    onCommitted();
                return;
            }

            return; // swallow all other keys during path drawing
        }

        // ── Select / MultiSelect — delete key only ────────────────
        if (activeTool_ == CanvasTool::Select ||
            activeTool_ == CanvasTool::MultiSelect)
        {
            if (e.virtualKey == Key::Delete || e.virtualKey == Key::Backspace)
            {
                if (hasMultiSelection())
                    deleteAllSelected();
                else
                    deleteSelected();
            }
            return;
        }

        // ── Everything below requires an active edit session ──────
        if (!editing_)
            return;

        // ── Ctrl shortcuts ────────────────────────────────────────
        if (e.ctrl)
        {
            switch (e.virtualKey)
            {
            case 'A':
                selStart_ = 0;
                selEnd_ = int(editBuf_.size());
                resetBlink();
                return;

            case 'C':
                if (hasSelection())
                {
                    int lo = std::min(selStart_, selEnd_);
                    int hi = std::max(selStart_, selEnd_);
                    copyToClipboard(editBuf_.substr(lo, hi - lo));
                }
                return;

            case 'X':
                if (hasSelection())
                {
                    int lo = std::min(selStart_, selEnd_);
                    int hi = std::max(selStart_, selEnd_);
                    copyToClipboard(editBuf_.substr(lo, hi - lo));
                    deleteSelection();
                    resetBlink();
                }
                return;

            case 'V':
            {
                std::string clip = pasteFromClipboard();
                if (!clip.empty())
                {
                    deleteSelection();
                    int ins = selStart_;
                    editBuf_.insert(ins, clip);
                    selStart_ = selEnd_ = ins + int(clip.size());
                    resetBlink();
                }
                return;
            }

            default:
                return;
            }
        }

        // ── Printable character input ──────────────────────────────
        if (e.codepoint >= 32 && e.codepoint != 127)
        {
            deleteSelection();
            int ins = std::min(selStart_, selEnd_);
            editBuf_.insert(editBuf_.begin() + ins, char(e.codepoint));
            selStart_ = selEnd_ = ins + 1;
            resetBlink();
            return;
        }

        // ── Navigation and editing keys ───────────────────────────
        bool shift = e.shift;

        switch (e.virtualKey)
        {
        case Key::Backspace:
            if (hasSelection())
                deleteSelection();
            else if (!editBuf_.empty() && selStart_ > 0)
            {
                editBuf_.erase(editBuf_.begin() + selStart_ - 1);
                selStart_ = selEnd_ = selStart_ - 1;
            }
            resetBlink();
            break;

        case Key::Delete:
            if (hasSelection())
                deleteSelection();
            else if (selStart_ < int(editBuf_.size()))
                editBuf_.erase(editBuf_.begin() + selStart_);
            resetBlink();
            break;

        case Key::Left:
            if (!shift && hasSelection())
                selStart_ = selEnd_ = std::min(selStart_, selEnd_);
            else
            {
                int newPos = std::max(0, selEnd_ - 1);
                if (shift)
                    selEnd_ = newPos;
                else
                    selStart_ = selEnd_ = newPos;
            }
            resetBlink();
            break;

        case Key::Right:
            if (!shift && hasSelection())
                selStart_ = selEnd_ = std::max(selStart_, selEnd_);
            else
            {
                int newPos = std::min(int(editBuf_.size()), selEnd_ + 1);
                if (shift)
                    selEnd_ = newPos;
                else
                    selStart_ = selEnd_ = newPos;
            }
            resetBlink();
            break;

        case Key::Home:
            if (shift)
                selEnd_ = 0;
            else
                selStart_ = selEnd_ = 0;
            resetBlink();
            break;

        case Key::End:
            if (shift)
                selEnd_ = int(editBuf_.size());
            else
                selStart_ = selEnd_ = int(editBuf_.size());
            resetBlink();
            break;

        case Key::Return:
            deleteSelection();
            {
                int ins = std::min(selStart_, selEnd_);
                editBuf_.insert(editBuf_.begin() + ins, '\n');
                selStart_ = selEnd_ = ins + 1;
            }
            resetBlink();
            break;

        case Key::Escape:
            cancelEdit();
            break;

        default:
            break;
        }
    }

    void onKeyUp(const KeyEvent &e) override { shiftHeld_ = e.shift; }

    // ── Render ────────────────────────────────────────────────
    void render(Canvas2D &ctx) override
    {
        // ── White page ────────────────────────────────────────────
        ctx.setFillColor(Color::fromRGB(255, 255, 255));
        ctx.fillRect(0, 0, float(w_), float(h_));

        // Subtle page shadow
        ctx.setFillColor(Color::fromRGBA(0, 0, 0, 12));
        ctx.fillRect(float(w_) - 4.f, 0, 4.f, float(h_));
        ctx.fillRect(0, float(h_) - 4.f, float(w_), 4.f);

        if (int(blockMeasuredWidths_.size()) != int(blocks_.size()))
            blockMeasuredWidths_.assign(blocks_.size(), 0.f);

        // ── Draw committed TextBlock blocks ───────────────────────
        for (int i = 0; i < int(blocks_.size()); ++i)
        {
            const auto &b = blocks_[i];
            bool isActiveEdit = (editingExisting_ && editing_ && selectedIdx_ == i);
            bool isSelected = (selectedIdx_ == i && !editing_);

            if (isActiveEdit)
            {
                // Active edit — draw the edit preview instead of committed text
                renderEditPreview(ctx, editX_, editY_, editBuf_,
                                  activeFontSize_, resolveFont(), activeColor_, true);
            }
            else
            {
                // ── Compute block center for rotation pivot ───────
                float bcx, bcy;
                blockCenter(b, bcx, bcy);

                // ── Background fill (drawn in rotated block space) ─
                if (b.bgEnabled && b.bgColor.a > 0)
                {
                    auto vlinesBg = getVisualLines(b.text, b.wrapWidth, b.fontSize);
                    int nLinesBg = std::max(1, int(vlinesBg.size()));
                    float lineHBg = b.fontSize * b.lineHeight;

                    float bw = b.wrapWidth > 0.f ? b.wrapWidth : 0.f;
                    if (bw == 0.f && i < int(blockMeasuredWidths_.size()) && blockMeasuredWidths_[i] > 0.f)
                        bw = blockMeasuredWidths_[i];
                    if (bw == 0.f)
                        bw = approxTextWidth(b.text, b.fontSize);

                    float totalHBg = b.fontSize + 10.f + (nLinesBg - 1) * lineHBg;
                    float pad = b.bgPadding;

                    float bgX = b.x - pad;
                    float bgY = b.y - b.baselineShift - b.fontSize - pad;
                    float bgW = bw + pad * 2.f;
                    float bgH = totalHBg + pad * 2.f - 10.f;

                    ctx.save();
                    ctx.translate(bcx, bcy);
                    ctx.rotate(b.rotation);
                    ctx.translate(-bcx, -bcy);
                    ctx.setGlobalAlpha(b.opacity);
                    ctx.setFillColor(b.bgColor);
                    ctx.fillRoundedRect(bgX, bgY, bgW, bgH, 3.f);
                    ctx.setGlobalAlpha(1.f);
                    ctx.restore();
                }

                // ── Shadow pass (flat single-color, offset) ───────
                if (b.shadowEnabled)
                {
                    ctx.save();
                    ctx.translate(bcx, bcy);
                    ctx.rotate(b.rotation);
                    ctx.translate(-bcx, -bcy);
                    ctx.setGlobalAlpha(b.opacity * (b.shadowColor.a / 255.f));

                    auto shadowLines = getVisualLines(b.text, b.wrapWidth, b.fontSize);
                    float lineHSh = b.fontSize * b.lineHeight;

                    char shFontBuf[64];
                    std::snprintf(shFontBuf, sizeof(shFontBuf), "%.0fpx %s",
                                  b.fontSize, resolveFont(b).c_str());
                    ctx.setFont(shFontBuf);
                    ctx.setTextBaseline(TextBaseline::Bottom);

                    // Build container width for alignment
                    float contWSh = blockContainerWidth(ctx, shadowLines,
                                                        b.wrapWidth,
                                                        b.letterSpacing,
                                                        b.kerning);

                    Color flatShadow = b.shadowColor;
                    flatShadow.a = 255; // alpha handled by globalAlpha above
                    ctx.setFillColor(flatShadow);

                    for (int li = 0; li < int(shadowLines.size()); ++li)
                    {
                        float drawYSh = b.y + b.shadowOffsetY + 2.f + li * lineHSh - b.baselineShift;
                        float lineWSh = ctx.measureText(shadowLines[li]);
                        float lineOXSh = alignOffset(lineWSh, contWSh, b.textAlign);
                        ctx.fillText(shadowLines[li],
                                     b.x + b.shadowOffsetX + lineOXSh,
                                     drawYSh);
                    }

                    ctx.setGlobalAlpha(1.f);
                    ctx.restore();
                }

                // ── Main text pass (rotation + opacity + spans) ───
                ctx.save();
                ctx.translate(bcx, bcy);
                ctx.rotate(b.rotation);
                ctx.translate(-bcx, -bcy);
                ctx.setGlobalAlpha(b.opacity);

                {
                    auto lines = getVisualLines(b.text, b.wrapWidth, b.fontSize);
                    float lineH = b.fontSize * b.lineHeight;

                    char blockFontBuf[64];
                    std::snprintf(blockFontBuf, sizeof(blockFontBuf), "%.0fpx %s",
                                  b.fontSize, resolveFont(b).c_str());
                    ctx.setFont(blockFontBuf);
                    ctx.setTextBaseline(TextBaseline::Bottom);

                    float containerW = blockContainerWidth(ctx, lines, b.wrapWidth,
                                                           b.letterSpacing, b.kerning);
                    auto runs = normalizeSpans(b);

                    for (int li = 0; li < int(lines.size()); ++li)
                    {
                        bool isLast = (li == int(lines.size()) - 1);
                        float drawY = b.y + 2.f + li * lineH - b.baselineShift;
                        auto [byteS, byteE] = visualLineByteRange(b.text, lines, li);
                        drawSpanLine(ctx, b, runs,
                                     byteS, byteE,
                                     b.x, drawY,
                                     containerW, isLast);
                    }
                }

                ctx.setGlobalAlpha(1.f);
                ctx.restore();

                // ── Selection / hover outline ─────────────────────
                bool isMultiSelected = std::find(selectedIndices_.begin(),
                                                 selectedIndices_.end(), i) != selectedIndices_.end();
                if (isSelected || isMultiSelected)
                    drawBlockOutline(ctx, b, false);
            }

            // ── Update measured width cache ───────────────────────
            if (!b.text.empty())
            {
                char fontBuf[64];
                std::snprintf(fontBuf, sizeof(fontBuf), "%.0fpx %s",
                              b.fontSize, resolveFont(b).c_str());
                ctx.setFont(fontBuf);
                auto vlines = getVisualLines(b.text, b.wrapWidth, b.fontSize);
                float mw = b.wrapWidth > 0.f ? b.wrapWidth : 0.f;
                if (mw == 0.f)
                    for (auto &l : vlines)
                        mw = std::max(mw, measureTextSpaced(ctx, l,
                                                            b.letterSpacing, b.kerning));
                blockMeasuredWidths_[i] = mw;
            }
        }

        // ── Draw committed TextOnPath blocks ──────────────────────
        for (int i = 0; i < int(pathBlocks_.size()); ++i)
        {
            bool sel = (pathEditIdx_ == i && activeTool_ == CanvasTool::PathText);
            renderTextOnPath(ctx, pathBlocks_[i], sel);
        }

        // ── Draw in-progress PathText ─────────────────────────────
        if (drawingPath_)
        {
            if (!pendingPath_.segments.empty())
            {
                // Path skeleton — always visible from first click
                ctx.setStrokeColor(Color::fromRGBA(30, 120, 255, 120));
                ctx.setLineWidth(1.f);
                ctx.beginPath();
                for (auto &seg : pendingPath_.segments)
                {
                    ctx.moveTo(seg.x0, seg.y0);
                    ctx.bezierCurveTo(seg.cx0, seg.cy0,
                                      seg.cx1, seg.cy1,
                                      seg.x1, seg.y1);
                }
                ctx.stroke();

                // Anchor dots
                ctx.setFillColor(Color::fromRGBA(30, 120, 255, 220));
                for (auto &seg : pendingPath_.segments)
                {
                    ctx.fillCircle(seg.x0, seg.y0, 5.f);
                    ctx.fillCircle(seg.x1, seg.y1, 5.f);
                }

                // Rubber-band line from last anchor to mouse
                auto &last = pendingPath_.segments.back();
                ctx.setStrokeColor(Color::fromRGBA(100, 180, 255, 140));
                ctx.setLineWidth(1.f);
                ctx.beginPath();
                ctx.moveTo(last.x1, last.y1);
                ctx.lineTo(hoverX_, hoverY_);
                ctx.stroke();

                // Live text preview — only once text has been typed
                if (!pendingPath_.text.empty())
                    renderTextOnPath(ctx, pendingPath_, false);
            }

            // Status hint at canvas bottom
            ctx.setFont("12px sans");
            ctx.setFillColor(Color::fromRGBA(30, 120, 255, 200));
            ctx.setTextBaseline(TextBaseline::Bottom);
            std::string hint = editBuf_.empty()
                                   ? "Click to place points  \xC2\xB7  Type text  \xC2\xB7  Enter to finish  \xC2\xB7  Esc to cancel"
                                   : "\"" + editBuf_ + "\"  \xC2\xB7  Click to add points  \xC2\xB7  Enter to finish";
            ctx.fillText(hint, 16.f, float(h_) - 8.f);
        }

        // ── New-block text edit (Text tool, blank canvas click) ───
        if (editing_ && !editingExisting_)
            renderEditPreview(ctx, editX_, editY_, editBuf_,
                              activeFontSize_, resolveFont(), activeColor_, true);

        // ── Hover highlight (Select tool) ─────────────────────────
        if (activeTool_ == CanvasTool::Select && !editing_)
        {
            int hover = hitTestBlock(hoverX_, hoverY_);
            if (hover >= 0 && hover != selectedIdx_)
                drawBlockOutline(ctx, blocks_[hover], false, /*hover=*/true);
        }

        // ── Placeholder hint (top-left corner) ───────────────────
        if (!editing_ && !drawingPath_)
        {
            ctx.setFillColor(Color::fromRGBA(150, 150, 160, 60));
            ctx.setFont("12px sans");
            ctx.setTextBaseline(TextBaseline::Top);
            const char *hint =
                (activeTool_ == CanvasTool::Select)
                    ? "Select tool: click to select"
                : (activeTool_ == CanvasTool::Move)
                    ? "Move tool: drag a block"
                : (activeTool_ == CanvasTool::Scale)
                    ? "Scale tool: select then drag green handle"
                : (activeTool_ == CanvasTool::Rotate)
                    ? "Rotate tool: select then drag pink handle"
                : (activeTool_ == CanvasTool::MultiSelect)
                    ? "Multi-select: drag to band-select, shift+click to toggle"
                : (activeTool_ == CanvasTool::PathText)
                    ? "Path Text: click to place points, type text, Enter to finish"
                    : "Text tool: click anywhere to place text";
            ctx.fillText(hint, 16.f, 14.f);
        }

        // ── Rubber band selection rect ────────────────────────────
        if (rubberBanding_)
        {
            float rx = std::min(rubberX0_, rubberX1_);
            float ry = std::min(rubberY0_, rubberY1_);
            float rw = std::abs(rubberX1_ - rubberX0_);
            float rh = std::abs(rubberY1_ - rubberY0_);

            ctx.setFillColor(Color::fromRGBA(30, 120, 255, 25));
            ctx.fillRect(rx, ry, rw, rh);
            ctx.setStrokeColor(Color::fromRGBA(30, 120, 255, 180));
            ctx.setLineWidth(1.f);
            ctx.strokeRect(rx, ry, rw, rh);
        }
    }

private:
    // ── Helpers ───────────────────────────────────────────────
    std::vector<float> buildArcTable(const std::vector<PathSegment> &segs) const
    {
        std::vector<float> table;
        float cumulative = 0.f;
        for (auto &seg : segs)
        {
            cumulative += segmentLength(seg);
            table.push_back(cumulative);
        }
        return table;
    }

    void renderTextOnPath(Canvas2D &ctx, const TextOnPath &tp, bool selected = false)
    {
        if (tp.segments.empty() || tp.text.empty())
            return;

        auto arcTable = buildArcTable(tp.segments);
        float totalLen = arcTable.empty() ? 0.f : arcTable.back();

        // ── Draw path skeleton + handles when selected ────────────
        if (selected)
        {
            ctx.setStrokeColor(Color::fromRGBA(30, 120, 255, 120));
            ctx.setLineWidth(1.f);
            ctx.beginPath();
            for (auto &seg : tp.segments)
            {
                ctx.moveTo(seg.x0, seg.y0);
                ctx.bezierCurveTo(seg.cx0, seg.cy0,
                                  seg.cx1, seg.cy1,
                                  seg.x1, seg.y1);
            }
            ctx.stroke();

            for (auto &seg : tp.segments)
            {
                ctx.setStrokeColor(Color::fromRGBA(180, 80, 220, 120));
                ctx.setLineWidth(0.8f);
                ctx.beginPath();
                ctx.moveTo(seg.x0, seg.y0);
                ctx.lineTo(seg.cx0, seg.cy0);
                ctx.stroke();
                ctx.beginPath();
                ctx.moveTo(seg.x1, seg.y1);
                ctx.lineTo(seg.cx1, seg.cy1);
                ctx.stroke();

                ctx.setFillColor(Color::fromRGBA(180, 80, 220, 200));
                ctx.fillCircle(seg.cx0, seg.cy0, 4.f);
                ctx.fillCircle(seg.cx1, seg.cy1, 4.f);
                ctx.setFillColor(Color::fromRGBA(30, 120, 255, 220));
                ctx.fillCircle(seg.x0, seg.y0, 5.f);
                ctx.fillCircle(seg.x1, seg.y1, 5.f);
            }
        }

        // ── Resolve font string ───────────────────────────────────
        std::string fontKey = tp.fontFamily;
        if (tp.bold && tp.italic)
            fontKey += "-bold-italic";
        else if (tp.bold)
            fontKey += "-bold";
        else if (tp.italic)
            fontKey += "-italic";

        char fontBuf[64];
        std::snprintf(fontBuf, sizeof(fontBuf), "%.0fpx %s", tp.fontSize, fontKey.c_str());

        // ── Apply block opacity ───────────────────────────────────
        ctx.setGlobalAlpha(tp.opacity);

        // ── Shadow pass ───────────────────────────────────────────
        if (tp.shadowEnabled)
        {
            ctx.setFont(fontBuf);
            ctx.setTextBaseline(TextBaseline::Bottom);

            float shadowCursor = tp.startOffset;
            for (int ci = 0; ci < int(tp.text.size()); ++ci)
            {
                std::string ch = tp.text.substr(ci, 1);
                if (ch == "\n")
                    continue;

                float charW = ctx.measureText(ch);
                float placeDist = shadowCursor + charW * 0.5f;
                if (placeDist > totalLen)
                    break;

                float px, py, tx2, ty2;
                if (!pathPointAtDist(tp.segments, arcTable, placeDist,
                                     px, py, tx2, ty2))
                    break;

                float angle = atan2f(ty2, tx2);

                ctx.save();
                ctx.translate(px + tp.shadowOffsetX, py + tp.shadowOffsetY);
                ctx.rotate(angle);
                ctx.translate(0.f, -tp.baselineOffset);
                ctx.setFillColor(tp.shadowColor);
                ctx.fillText(ch, -charW * 0.5f, 0.f);
                ctx.restore();

                shadowCursor += charW;
            }
        }

        // ── Main character pass ───────────────────────────────────
        ctx.setFont(fontBuf);
        ctx.setTextBaseline(TextBaseline::Bottom);

        float cursor = tp.startOffset;
        for (int ci = 0; ci < int(tp.text.size()); ++ci)
        {
            std::string ch = tp.text.substr(ci, 1);
            if (ch == "\n")
                continue;

            float charW = ctx.measureText(ch);
            float placeDist = cursor + charW * 0.5f;
            if (placeDist > totalLen)
                break;

            float px, py, tx2, ty2;
            if (!pathPointAtDist(tp.segments, arcTable, placeDist,
                                 px, py, tx2, ty2))
                break;

            float angle = atan2f(ty2, tx2);

            ctx.save();
            ctx.translate(px, py);
            ctx.rotate(angle);
            ctx.translate(0.f, -tp.baselineOffset);

            // Stroke pass first (renders behind fill)
            if (tp.strokeEnabled)
            {
                ctx.setStrokeColor(tp.strokeColor);
                ctx.setLineWidth(tp.strokeWidth);
                ctx.strokeText(ch, -charW * 0.5f, 0.f);
            }

            // Fill pass
            ctx.setFillColor(tp.color);
            ctx.fillText(ch, -charW * 0.5f, 0.f);

            ctx.restore();

            cursor += charW;
        }

        // ── Reset opacity ─────────────────────────────────────────
        ctx.setGlobalAlpha(1.f);
    }

    // Evaluate cubic bezier position at t in [0,1]
    inline void cubicBezierPoint(const PathSegment &seg, float t,
                                 float &outX, float &outY) const
    {
        float mt = 1.f - t;
        outX = mt * mt * mt * seg.x0 + 3 * mt * mt * t * seg.cx0 + 3 * mt * t * t * seg.cx1 + t * t * t * seg.x1;
        outY = mt * mt * mt * seg.y0 + 3 * mt * mt * t * seg.cy0 + 3 * mt * t * t * seg.cy1 + t * t * t * seg.y1;
    }

    // Evaluate tangent direction (normalized) at t
    inline void cubicBezierTangent(const PathSegment &seg, float t,
                                   float &tx, float &ty) const
    {
        float mt = 1.f - t;
        float dx = 3 * (mt * mt * (seg.cx0 - seg.x0) + 2 * mt * t * (seg.cx1 - seg.cx0) + t * t * (seg.x1 - seg.cx1));
        float dy = 3 * (mt * mt * (seg.cy0 - seg.y0) + 2 * mt * t * (seg.cy1 - seg.cy0) + t * t * (seg.y1 - seg.cy1));
        float len = sqrtf(dx * dx + dy * dy);
        if (len < 1e-5f)
        {
            tx = 1;
            ty = 0;
            return;
        }
        tx = dx / len;
        ty = dy / len;
    }

    // Approximate arc length of one segment using fixed subdivisions
    inline float segmentLength(const PathSegment &seg, int steps = 64) const
    {
        float len = 0.f, px, py, qx, qy;
        cubicBezierPoint(seg, 0.f, px, py);
        for (int i = 1; i <= steps; ++i)
        {
            cubicBezierPoint(seg, float(i) / steps, qx, qy);
            float dx = qx - px, dy = qy - py;
            len += sqrtf(dx * dx + dy * dy);
            px = qx;
            py = qy;
        }
        return len;
    }

    // Given a distance along the full multi-segment path, return
    // which segment + t value corresponds to that distance.
    // arcTable must be pre-built: arcTable[i] = cumulative length at end of segment i
    inline bool pathPointAtDist(const std::vector<PathSegment> &segs,
                                const std::vector<float> &arcTable,
                                float dist,
                                float &outX, float &outY,
                                float &outTx, float &outTy) const
    {
        if (segs.empty())
            return false;
        float totalLen = arcTable.back();
        dist = std::clamp(dist, 0.f, totalLen);

        int si = 0;
        float segStart = 0.f;
        for (int i = 0; i < int(arcTable.size()); ++i)
        {
            if (dist <= arcTable[i] || i == int(arcTable.size()) - 1)
            {
                si = i;
                segStart = (i == 0) ? 0.f : arcTable[i - 1];
                break;
            }
        }
        float segLen = arcTable[si] - segStart;
        float t = (segLen < 1e-5f) ? 0.f : (dist - segStart) / segLen;
        t = std::clamp(t, 0.f, 1.f);
        cubicBezierPoint(segs[si], t, outX, outY);
        cubicBezierTangent(segs[si], t, outTx, outTy);
        return true;
    }

    // Build a temporary TextBlock from the current edit state
    TextBlock makeEditTmp() const
    {
        TextBlock tmp;
        tmp.text = editBuf_;
        tmp.lineHeight = activeLineHeight_;
        tmp.letterSpacing = activeLetterSpacing_;
        tmp.kerning = activeKerning_;
        tmp.textAlign = activeTextAlign_;

        // Always use committed block as style base when editing existing
        if (editingExisting_ && selectedIdx_ >= 0 &&
            selectedIdx_ < int(blocks_.size()))
        {
            const auto &src = blocks_[selectedIdx_];
            tmp.fontSize = src.fontSize;
            tmp.fontFamily = src.fontFamily;
            tmp.bold = src.bold;
            tmp.italic = src.italic;
            tmp.color = src.color;
            tmp.baselineShift = src.baselineShift;
            tmp.spans = src.spans;
        }
        else
        {
            tmp.fontSize = activeFontSize_;
            tmp.fontFamily = activeFontFamily_;
            tmp.bold = activeBold_;
            tmp.italic = activeItalic_;
            tmp.color = activeColor_;
            tmp.baselineShift = activeBaselineShift_;
        }
        return tmp;
    }

    template <typename Fn>
    bool selectionBoolQuery(Fn fn) const
    {
        if (!editing_ || !hasSelection())
            return false;
        int lo = std::min(selStart_, selEnd_);
        int hi = std::max(selStart_, selEnd_);
        TextBlock tmp = makeEditTmp();
        auto runs = normalizeSpans(tmp);
        for (auto &rs : runs)
        {
            if (rs.end <= lo || rs.start >= hi)
                continue;
            if (!fn(rs))
                return false;
        }
        return true;
    }

    void copyToClipboard(const std::string &text) const
    {
#ifdef _WIN32
        if (text.empty())
            return;
        if (!OpenClipboard(nullptr))
            return;
        EmptyClipboard();

        // Convert to wide string for CF_UNICODETEXT
        int wlen = MultiByteToWideChar(CP_UTF8, 0,
                                       text.c_str(), int(text.size()),
                                       nullptr, 0);
        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE,
                                   (wlen + 1) * sizeof(wchar_t));
        if (hMem)
        {
            wchar_t *dst = reinterpret_cast<wchar_t *>(GlobalLock(hMem));
            MultiByteToWideChar(CP_UTF8, 0,
                                text.c_str(), int(text.size()),
                                dst, wlen);
            dst[wlen] = L'\0';
            GlobalUnlock(hMem);
            SetClipboardData(CF_UNICODETEXT, hMem);
        }
        CloseClipboard();
#endif
    }

    std::string pasteFromClipboard() const
    {
#ifdef _WIN32
        if (!OpenClipboard(nullptr))
            return {};
        HANDLE hData = GetClipboardData(CF_UNICODETEXT);
        if (!hData)
        {
            CloseClipboard();
            return {};
        }

        wchar_t *wstr = reinterpret_cast<wchar_t *>(GlobalLock(hData));
        if (!wstr)
        {
            CloseClipboard();
            return {};
        }

        int len = WideCharToMultiByte(CP_UTF8, 0,
                                      wstr, -1,
                                      nullptr, 0, nullptr, nullptr);
        std::string result(len - 1, '\0');
        WideCharToMultiByte(CP_UTF8, 0,
                            wstr, -1,
                            result.data(), len, nullptr, nullptr);

        GlobalUnlock(hData);
        CloseClipboard();

        // Strip \r, keep \n
        result.erase(std::remove(result.begin(), result.end(), '\r'),
                     result.end());
        return result;
#else
        return {};
#endif
    }

    // Returns {wordStart, wordEnd} for the word containing buffer position pos.
    // Word characters: alphanumeric + underscore. Everything else is a boundary.
    std::pair<int, int> wordBoundsAt(int pos) const
    {
        int n = int(editBuf_.size());
        if (n == 0)
            return {0, 0};
        pos = std::clamp(pos, 0, n - 1);

        auto isWord = [](char c)
        {
            return std::isalnum((unsigned char)c) || c == '_';
        };

        // If we're sitting on a non-word char, select just that char
        if (!isWord(editBuf_[pos]))
            return {pos, pos + 1};

        int lo = pos;
        while (lo > 0 && isWord(editBuf_[lo - 1]))
            --lo;

        int hi = pos;
        while (hi < n && isWord(editBuf_[hi]))
            ++hi;

        return {lo, hi};
    }
    // Computes the reference container width for alignment.
    // Uses wrapWidth if set; otherwise measures the widest visual line.
    float blockContainerWidth(Canvas2D &ctx,
                              const std::vector<std::string> &lines,
                              float wrapWidth,
                              float letterSpacing,
                              bool kerning) const
    {
        if (wrapWidth > 0.f)
            return wrapWidth;
        float maxW = 0.f;
        for (auto &l : lines)
            maxW = std::max(maxW, measureTextSpaced(ctx, l, letterSpacing, kerning));
        return maxW;
    }
    // Draws one visual line with justify spacing.
    // isLastLine: if true, falls back to left (normal spacing).
    void fillTextJustified(Canvas2D &ctx,
                           const std::string &line,
                           float x, float y,
                           float containerW,
                           float letterSpacing,
                           bool kerning,
                           bool isLastLine) const
    {
        // Last line or no spaces -> draw normally
        auto spacePos = line.find(' ');
        if (isLastLine || spacePos == std::string::npos)
        {
            fillTextSpaced(ctx, line, x, y, letterSpacing, kerning);
            return;
        }

        // Split into words
        std::vector<std::string> words;
        std::string cur;
        for (char c : line)
        {
            if (c == ' ')
            {
                if (!cur.empty())
                {
                    words.push_back(cur);
                    cur.clear();
                }
            }
            else
                cur += c;
        }
        if (!cur.empty())
            words.push_back(cur);
        if (words.size() < 2)
        {
            fillTextSpaced(ctx, line, x, y, letterSpacing, kerning);
            return;
        }

        // Measure total word widths
        float totalWordW = 0.f;
        for (auto &w : words)
            totalWordW += measureTextSpaced(ctx, w, letterSpacing, kerning);

        // Distribute remaining space evenly between gaps
        float totalGap = containerW - totalWordW;
        float gapPerWord = totalGap / float(int(words.size()) - 1);

        float cx = x;
        for (int i = 0; i < int(words.size()); ++i)
        {
            fillTextSpaced(ctx, words[i], cx, y, letterSpacing, kerning);
            if (i < int(words.size()) - 1)
                cx += measureTextSpaced(ctx, words[i], letterSpacing, kerning) + gapPerWord;
        }
    }
    // Returns the x offset to apply before drawing a line, given alignment.
    // containerW is the reference width (wrapWidth if set, else max line width).
    // lineW is the measured width of this specific line.
    float alignOffset(float lineW, float containerW, TextAlign align) const
    {
        switch (align)
        {
        case TextAlign::Center:
            return (containerW - lineW) * 0.5f;
        case TextAlign::Right:
            return containerW - lineW;
        case TextAlign::Left:
        default:
            return 0.f;
        }
    }

    // Measures text width respecting letterSpacing
    float measureTextSpaced(Canvas2D &ctx,
                            const std::string &text,
                            float letterSpacing,
                            bool kerning) const
    {
        if (text.empty())
            return 0.f;

        int fontIdx = ctx.currentFontIdx();
        int ps = std::max(4, int(ctx.currentFontSize() + 0.5f));

        float total = 0.f;
        for (int i = 0; i < int(text.size()); ++i)
        {
            total += ctx.measureText(text.substr(i, 1));
            if (i < int(text.size()) - 1)
            {
                total += letterSpacing;
                if (kerning && fontIdx >= 0 && ctx.gl_)
                    total += ctx.gl_->getKernAdvance(fontIdx,
                                                     (unsigned char)text[i],
                                                     (unsigned char)text[i + 1],
                                                     ps);
            }
        }
        return total;
    }

    // Draws text char-by-char respecting letterSpacing
    void fillTextSpaced(Canvas2D &ctx,
                        const std::string &text,
                        float x, float y,
                        float letterSpacing,
                        bool kerning) const
    {
        if (text.empty())
            return;

        if (letterSpacing == 0.f && !kerning)
        {
            ctx.fillText(text, x, y);
            return;
        }

        int fontIdx = ctx.currentFontIdx();
        int ps = std::max(4, int(ctx.currentFontSize() + 0.5f));

        float cx = x;
        for (int i = 0; i < int(text.size()); ++i)
        {
            std::string ch = text.substr(i, 1);
            ctx.fillText(ch, cx, y);
            float advance = ctx.measureText(ch);

            if (i < int(text.size()) - 1)
            {
                advance += letterSpacing;
                if (kerning && fontIdx >= 0 && ctx.gl_)
                    advance += ctx.gl_->getKernAdvance(fontIdx,
                                                       (unsigned char)text[i],
                                                       (unsigned char)text[i + 1],
                                                       ps);
            }
            cx += advance;
        }
    }
    // Wraps a single line of text into visual lines given a max pixel width.
    // Uses approximate char width (no ctx available here).
    std::vector<std::string> wrapLine(const std::string &line,
                                      float wrapW, float fontSize) const
    {
        if (wrapW <= 0.f || line.empty())
            return {line};

        float charW = fontSize * 0.55f;
        std::vector<std::string> result;
        std::string current;
        std::string word;

        auto flushWord = [&]()
        {
            if (word.empty())
                return;
            std::string candidate = current.empty() ? word : current + " " + word;
            if (float(candidate.size()) * charW > wrapW && !current.empty())
            {
                result.push_back(current);
                current = word;
            }
            else
            {
                current = candidate;
            }
            word.clear();
        };

        for (char c : line)
        {
            if (c == ' ')
                flushWord();
            else
                word += c;
        }
        flushWord();
        if (!current.empty())
            result.push_back(current);
        if (result.empty())
            result.push_back("");
        return result;
    }

    std::vector<std::string> getVisualLines(const std::string &text,
                                            float wrapW, float fontSize) const
    {
        auto hardLines = splitLines(text);
        if (wrapW <= 0.f)
            return hardLines;

        std::vector<std::string> result;
        for (auto &hl : hardLines)
        {
            auto wrapped = wrapLine(hl, wrapW, fontSize);
            for (auto &vl : wrapped)
                result.push_back(vl);
        }
        return result;
    }

    std::pair<int, int> visualLineByteRange(
        const std::string &text,
        const std::vector<std::string> &visualLines,
        int li) const
    {
        int pos = 0;
        for (int i = 0; i < li; ++i)
        {
            pos += int(visualLines[i].size());
            // Account for the '\n' or space that was consumed
            if (pos < int(text.size()) &&
                (text[pos] == '\n' || text[pos] == ' '))
                ++pos;
        }
        int lineEnd = pos + int(visualLines[li].size());
        return {pos, lineEnd};
    }

    std::vector<std::string> splitLines(const std::string &text) const
    {
        std::vector<std::string> lines;
        std::string cur;
        for (char c : text)
        {
            if (c == '\n')
            {
                lines.push_back(cur);
                cur.clear();
            }
            else
            {
                cur += c;
            }
        }
        lines.push_back(cur);
        return lines;
    }

    std::string resolveFont() const
    {
        std::string base = activeFontFamily_; // "sans" or "mono"
        if (activeBold_ && activeItalic_)
            return base + "-bold-italic";
        if (activeBold_)
            return base + "-bold";
        if (activeItalic_)
            return base + "-italic";
        return base;
    }

    std::string resolveFont(const TextBlock &b) const
    {
        std::string base = b.fontFamily;
        if (b.bold && b.italic)
            return base + "-bold-italic";
        if (b.bold)
            return base + "-bold";
        if (b.italic)
            return base + "-italic";
        return base;
    }

    void setFont(Canvas2D &ctx, float fs, const std::string &family)
    {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.0fpx %s", fs, family.c_str());
        ctx.setFont(buf);
    }

    void setFont(Canvas2D &ctx, float fs, const std::string &family,
                 bool bold, bool italic)
    {
        std::string resolved = family;
        if (bold && italic)
            resolved += "-bold-italic";
        else if (bold)
            resolved += "-bold";
        else if (italic)
            resolved += "-italic";
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.0fpx %s", fs, resolved.c_str());
        ctx.setFont(buf);
    }

    std::string resolveRunFont(const ResolvedStyle &rs) const
    {
        std::string base = rs.fontFamily;
        if (rs.bold && rs.italic)
            return base + "-bold-italic";
        if (rs.bold)
            return base + "-bold";
        if (rs.italic)
            return base + "-italic";
        return base;
    }

    // Returns the string to actually render for a run.
    // smallCaps: lowercase chars rendered as uppercase at reduced fontSize.
    // We handle smallCaps by splitting into sub-runs at render time (Step D).
    // For now just allCaps.
    std::string displayText(const std::string &text,
                            int start, int end,
                            const ResolvedStyle &rs) const
    {
        std::string s = text.substr(start, end - start);
        if (rs.allCaps || rs.smallCaps)
        {
            for (auto &c : s)
                c = char(std::toupper((unsigned char)c));
        }
        return s;
    }

    void resetBlink()
    {
        blinkTimer_ = 0.0;
        cursorVisible_ = true;
    }

    void applyEditToBlock(TextBlock &b) const
    {
        b.text = editBuf_;
        b.fontSize = activeFontSize_;
        b.fontFamily = activeFontFamily_;
        b.bold = activeBold_;
        b.italic = activeItalic_;
        b.color = activeColor_;
        b.lineHeight = activeLineHeight_;
        b.letterSpacing = activeLetterSpacing_;
        b.kerning = activeKerning_;
        b.textAlign = activeTextAlign_;
        b.baselineShift = activeBaselineShift_;
        if (!editingExisting_)
        {
            b.x = editX_;
            b.y = editY_;
        }
    }

    void deleteSelection()
    {
        if (!hasSelection())
            return;
        int lo = std::min(selStart_, selEnd_);
        int hi = std::max(selStart_, selEnd_);
        editBuf_.erase(editBuf_.begin() + lo, editBuf_.begin() + hi);
        selStart_ = selEnd_ = lo;
    }

    // Estimate cursor index from x coordinate within a block (canvas space)
    int cursorIndexAt(float x, const TextBlock &b) const
    {
        // For now approximate on the first line only (full line-click support comes with wrapping)
        float charW = b.fontSize * 0.55f;
        float relX = x - b.x;
        int col = int(relX / charW + 0.5f);
        col = std::max(0, std::min(col, int(b.text.size())));
        return col;
    }

    // Measure text width using approximate char width (can't call ctx here)
    float approxTextWidth(const std::string &text, float fontSize, float letterSpacing = 0.f) const
    {
        float maxW = 0;
        float charW = fontSize * 0.55f;
        std::string cur;
        for (char c : text)
        {
            if (c == '\n')
            {
                float w = float(cur.size()) * charW;
                if (letterSpacing != 0.f && !cur.empty())
                    w += letterSpacing * float(cur.size() - 1);
                maxW = std::max(maxW, w);
                cur.clear();
            }
            else
                cur += c;
        }
        float w = float(cur.size()) * charW;
        if (letterSpacing != 0.f && !cur.empty())
            w += letterSpacing * float(cur.size() - 1);
        maxW = std::max(maxW, w);
        return maxW;
    }

    void renderEditPreview(Canvas2D &ctx,
                           float tx, float ty,
                           const std::string &text,
                           float fs, const std::string &font,
                           Color color, bool highlight)
    {
        char fontBuf[64];
        std::snprintf(fontBuf, sizeof(fontBuf), "%.0fpx %s", fs, font.c_str());
        ctx.setFont(fontBuf);
        ctx.setTextBaseline(TextBaseline::Bottom);

        float lineH = fs * activeLineHeight_;

        float wrapW = (editingExisting_ && selectedIdx_ >= 0 &&
                       selectedIdx_ < int(blocks_.size()))
                          ? blocks_[selectedIdx_].wrapWidth
                          : 0.f;

        float ls = (editingExisting_ && selectedIdx_ >= 0 &&
                    selectedIdx_ < int(blocks_.size()))
                       ? blocks_[selectedIdx_].letterSpacing
                       : activeLetterSpacing_;
        bool kr = (editingExisting_ && selectedIdx_ >= 0 &&
                   selectedIdx_ < int(blocks_.size()))
                      ? blocks_[selectedIdx_].kerning
                      : activeKerning_;
        auto lines = getVisualLines(text, wrapW, fs);
        int nLines = int(lines.size());
        float containerW = blockContainerWidth(ctx, lines, wrapW, ls, kr);

        TextAlign ta = (editingExisting_ && selectedIdx_ >= 0 &&
                        selectedIdx_ < int(blocks_.size()))
                           ? blocks_[selectedIdx_].textAlign
                           : activeTextAlign_;

        float bs = (editingExisting_ && selectedIdx_ >= 0 &&
                    selectedIdx_ < int(blocks_.size()))
                       ? blocks_[selectedIdx_].baselineShift
                       : activeBaselineShift_;

        // Background highlight box
        if (highlight)
        {
            float maxW = fs * 4.f;
            for (auto &l : lines)
                maxW = std::max(maxW, measureTextSpaced(ctx, l.empty() ? "M" : l, ls, kr) + fs * 1.2f);
            float totalH = nLines * lineH + 10.f;

            ctx.setFillColor(Color::fromRGBA(30, 120, 255, 15));
            ctx.fillRoundedRect(tx - 4.f, ty - fs - 6.f - bs, maxW, totalH, 3.f);
            ctx.setFillColor(Color::fromRGBA(30, 120, 255, 50));
            ctx.fillRect(tx - 2.f, ty + 3.f + (nLines - 1) * lineH - bs, maxW, 1.f);
        }

        // Figure out which line/col the cursor and selection endpoints are on
        auto posToLineCol = [&](int pos) -> std::pair<int, int>
        {
            int p = 0;
            for (int li = 0; li < nLines; ++li)
            {
                int lineLen = int(lines[li].size());
                // +1 for the '\n' except on last line
                int advance = lineLen + (li < nLines - 1 ? 1 : 0);
                if (p + advance > pos || li == nLines - 1)
                    return {li, std::min(pos - p, lineLen)};
                p += advance;
            }
            return {nLines - 1, int(lines.back().size())};
        };

        // Draw selection highlight
        if (hasSelection())
        {
            int lo = std::min(selStart_, selEnd_);
            int hi = std::max(selStart_, selEnd_);
            auto [loLine, loCol] = posToLineCol(lo);
            auto [hiLine, hiCol] = posToLineCol(hi);

            for (int li = loLine; li <= hiLine; ++li)
            {
                int colStart = (li == loLine) ? loCol : 0;
                int colEnd = (li == hiLine) ? hiCol : int(lines[li].size());

                // Compute this line's draw offset
                float lineW = measureTextSpaced(ctx, lines[li], ls, kr);
                float lineOX = (ta == TextAlign::Justify)
                                   ? 0.f
                                   : alignOffset(lineW, containerW, ta);

                float selX = tx + lineOX +
                             measureTextSpaced(ctx, lines[li].substr(0, colStart), ls, kr);
                float selW = measureTextSpaced(ctx,
                                               lines[li].substr(colStart, colEnd - colStart), ls, kr);
                if (selW < 2.f)
                    selW = 2.f;
                ctx.setFillColor(Color::fromRGBA(30, 120, 255, 80));
                ctx.fillRect(selX, ty - fs - 1.f + li * lineH - bs, selW, fs + 4.f);
            }
        }

        // Draw each line with span awareness
        {
            // Build a tmp block for span resolution
            TextBlock tmp = makeEditTmp();
            tmp.text = text; // may be editBuf_ or passed-in text
            auto runs = normalizeSpans(tmp);

            for (int li = 0; li < nLines; ++li)
            {
                bool isLast = (li == nLines - 1);
                float drawY2 = tx > 0 ? ty + 2.f + li * lineH - bs : ty;
                // Reuse drawY from outer scope
                float drawY_line = ty + 2.f + li * lineH - bs;

                auto [byteS, byteE] = visualLineByteRange(text, lines, li);
                drawSpanLine(ctx, tmp, runs,
                             byteS, byteE,
                             tx, drawY_line,
                             containerW, isLast);
            }
        }

        // Blinking cursor
        if (cursorVisible_)
        {
            auto [curLine, curCol] = posToLineCol(selEnd_);
            float lineW = measureTextSpaced(ctx, lines[curLine], ls, kr);
            float lineOX = (ta == TextAlign::Justify)
                               ? 0.f
                               : alignOffset(lineW, containerW, ta);
            float cursorX = tx + lineOX +
                            measureTextSpaced(ctx,
                                              lines[curLine].substr(0, curCol), ls, kr);
            float cursorY = ty - fs - 1.f + curLine * lineH - bs;
            ctx.setFillColor(color);
            ctx.fillRect(cursorX + 1.f, cursorY, 1.5f, fs + 4.f);
        }
    }

    // Draw one visual line using span runs.
    // lineStart/lineEnd are byte offsets into b.text for this visual line.
    // drawX/drawY are the baseline position.
    // containerW used for justify.
    void drawSpanLine(Canvas2D &ctx,
                      const TextBlock &b,
                      const std::vector<ResolvedStyle> &runs,
                      int lineStart, int lineEnd,
                      float drawX, float drawY,
                      float containerW,
                      bool isLastLine)
    {
        // ── Collect per-character x positions for decorations ────
        // We'll build a list of {runIdx, subText, x, width, rs}
        struct RunSegment
        {
            const ResolvedStyle *rs;
            std::string text;
            float x = 0, width = 0;
            float baselineY = 0;
            float fontSize = 0;
        };
        std::vector<RunSegment> segs;

        // ── Pre-pass: measure each run segment on this line ──────
        float totalW = 0.f;
        for (auto &rs : runs)
        {
            int s = std::max(rs.start, lineStart);
            int e = std::min(rs.end, lineEnd);
            if (s >= e)
                continue;

            std::string txt = displayText(b.text, s, e, rs);

            // Set font for measurement
            char fontBuf[64];
            std::snprintf(fontBuf, sizeof(fontBuf), "%.0fpx %s",
                          rs.fontSize, resolveRunFont(rs).c_str());
            ctx.setFont(fontBuf);

            float w = measureTextSpaced(ctx, txt, b.letterSpacing, b.kerning);
            segs.push_back({&rs, txt, 0.f, w,
                            drawY - rs.baselineShift,
                            rs.fontSize});
            totalW += w;
        }
        if (segs.empty())
            return;

        // ── Compute starting x based on alignment ────────────────
        float startX = drawX;
        if (b.textAlign == TextAlign::Center)
            startX = drawX + (containerW - totalW) * 0.5f;
        else if (b.textAlign == TextAlign::Right)
            startX = drawX + containerW - totalW;
        else if (b.textAlign == TextAlign::Justify && !isLastLine)
        {
            // justify: handled per-word below — use left for now
            // full justify with mixed runs is complex; use left align fallback
            startX = drawX;
        }

        // ── Assign x positions ───────────────────────────────────
        float cx = startX;
        for (auto &seg : segs)
        {
            seg.x = cx;
            cx += seg.width;
        }

        // ── Draw each segment ────────────────────────────────────────
        ctx.setTextBaseline(TextBaseline::Bottom);
        for (auto &seg : segs)
        {
            const ResolvedStyle &rs = *seg.rs;

            if (rs.smallCaps)
            {
                // Split into sub-runs: uppercase chars at full size,
                // lowercase chars at 70% size (rendered as uppercase)
                float cx2 = seg.x;
                float fullSize = rs.fontSize;
                float smallSize = rs.fontSize * 0.7f;

                // Re-fetch original (non-transformed) text for this segment
                // seg.text is already all-caps via displayText; we need original
                int segStart = -1, segEnd = -1;
                // Find which run this seg belongs to by matching pointer
                for (auto &r : runs)
                {
                    if (&r == seg.rs)
                    {
                        // runs is local — find start/end from lineStart context
                        break;
                    }
                }
                // Simpler: walk the original b.text for this seg range
                // We stored start/end in the run — access via seg.rs
                int rStart = std::max(seg.rs->start, lineStart);
                int rEnd = std::min(seg.rs->end, lineEnd);
                const std::string &origText = b.text;

                for (int ci = rStart; ci < rEnd;)
                {
                    char ch = origText[ci];
                    bool isLower = (ch >= 'a' && ch <= 'z');
                    // Find run of same case
                    int runEnd = ci;
                    while (runEnd < rEnd)
                    {
                        bool nextLower = (origText[runEnd] >= 'a' && origText[runEnd] <= 'z');
                        if (nextLower != isLower)
                            break;
                        ++runEnd;
                    }

                    std::string sub = origText.substr(ci, runEnd - ci);
                    float subSize = isLower ? smallSize : fullSize;

                    // Uppercase the lowercase portion
                    if (isLower)
                        for (auto &c2 : sub)
                            c2 = char(std::toupper((unsigned char)c2));

                    char fontBuf2[64];
                    std::snprintf(fontBuf2, sizeof(fontBuf2), "%.0fpx %s",
                                  subSize, resolveRunFont(rs).c_str());
                    ctx.setFont(fontBuf2);
                    ctx.setFillColor(rs.color);

                    // Align lowercase to baseline bottom of full-size chars
                    float subY = isLower
                                     ? seg.baselineY + (fullSize - smallSize) * 0.0f
                                     : seg.baselineY;

                    fillTextSpaced(ctx, sub, cx2, subY, b.letterSpacing, b.kerning);

                    ctx.setFont(fontBuf2); // keep set for measurement
                    cx2 += measureTextSpaced(ctx, sub, b.letterSpacing, b.kerning);
                    ci = runEnd;
                }

                // Underline / strikethrough on full seg width
                if (rs.underline)
                {
                    float uy = seg.baselineY + rs.fontSize * 0.1f;
                    ctx.setFillColor(rs.color);
                    ctx.fillRect(seg.x, uy, seg.width, std::max(1.f, rs.fontSize * 0.07f));
                }
                if (rs.strikethrough)
                {
                    float sy = seg.baselineY - rs.fontSize * 0.35f;
                    ctx.setFillColor(rs.color);
                    ctx.fillRect(seg.x, sy, seg.width, std::max(1.f, rs.fontSize * 0.07f));
                }
            }
            else
            {
                // Normal draw
                char fontBuf[64];
                std::snprintf(fontBuf, sizeof(fontBuf), "%.0fpx %s",
                              rs.fontSize, resolveRunFont(rs).c_str());
                ctx.setFont(fontBuf);
                ctx.setFillColor(rs.color);
                if (rs.strokeEnabled)
                {
                    char fontBufSt[64];
                    std::snprintf(fontBufSt, sizeof(fontBufSt), "%.0fpx %s",
                                  rs.fontSize, resolveRunFont(rs).c_str());
                    ctx.setFont(fontBufSt);
                    ctx.setStrokeColor(rs.strokeColor);
                    ctx.setLineWidth(rs.strokeWidth);
                    // strokeText draws outline only
                    // draw char by char to match fillTextSpaced positioning
                    float cxSt = seg.x;
                    for (int si = 0; si < int(seg.text.size()); ++si)
                    {
                        std::string ch = seg.text.substr(si, 1);
                        ctx.strokeText(ch, cxSt, seg.baselineY);
                        cxSt += ctx.measureText(ch) + b.letterSpacing;
                    }
                }
                fillTextSpaced(ctx, seg.text, seg.x, seg.baselineY,
                               b.letterSpacing, b.kerning);

                if (rs.underline)
                {
                    float uy = seg.baselineY + rs.fontSize * 0.1f;
                    ctx.setFillColor(rs.color);
                    ctx.fillRect(seg.x, uy, seg.width, std::max(1.f, rs.fontSize * 0.07f));
                }
                if (rs.strikethrough)
                {
                    float sy = seg.baselineY - rs.fontSize * 0.35f;
                    ctx.setFillColor(rs.color);
                    ctx.fillRect(seg.x, sy, seg.width, std::max(1.f, rs.fontSize * 0.07f));
                }
            }
        }
    }

    void drawBlockOutline(Canvas2D &ctx, const TextBlock &b,
                          bool /*active*/, bool hover = false)
    {
        char fontBuf[64];
        std::snprintf(fontBuf, sizeof(fontBuf), "%.0fpx %s", b.fontSize,
                      resolveFont(b).c_str());
        ctx.setFont(fontBuf);
        ctx.setTextBaseline(TextBaseline::Bottom);

        auto vlines = getVisualLines(b.text, b.wrapWidth, b.fontSize);
        int nLines = std::max(1, int(vlines.size()));
        float lineH = b.fontSize * b.lineHeight;

        float tw = b.wrapWidth > 0.f ? b.wrapWidth : 0.f;
        if (tw == 0.f)
        {
            // Try cache first
            int bidx = int(&b - blocks_.data()); // pointer arithmetic to get index
            if (bidx >= 0 && bidx < int(blockMeasuredWidths_.size()) && blockMeasuredWidths_[bidx] > 0.f)
                tw = blockMeasuredWidths_[bidx];
            else
                for (auto &l : vlines)
                    tw = std::max(tw, measureTextSpaced(ctx, l,
                                                        b.letterSpacing, b.kerning));
        }

        float totalH = b.fontSize + 10.f + (nLines - 1) * lineH;

        // Block bounding box top-left in local space
        float boxX = b.x - 4.f;
        float boxY = b.y - b.baselineShift - b.fontSize - 4.f;

        // Block center (pivot for rotation)
        float cx, cy;
        blockCenter(b, cx, cy);

        // ── Apply rotation around block center ────────────────────
        ctx.save();
        ctx.translate(cx, cy);
        ctx.rotate(b.rotation);
        ctx.translate(-cx, -cy);

        // Outline rect
        if (hover)
            ctx.setStrokeColor(Color::fromRGBA(30, 120, 255, 60));
        else
            ctx.setStrokeColor(Color::fromRGBA(30, 120, 255, 160));
        ctx.setLineWidth(1.f);
        ctx.strokeRoundedRect(boxX, boxY, tw + 8.f, totalH, 2.f);

        // Corner handles (selected only)
        if (!hover)
        {
            float hx[] = {boxX, boxX + tw + 8.f};
            float hy[] = {boxY, boxY + totalH};
            ctx.setFillColor(Color::fromRGBA(30, 120, 255, 200));
            for (float hxx : hx)
                for (float hyy : hy)
                    ctx.fillRoundedRect(hxx - 3.f, hyy - 3.f, 6.f, 6.f, 2.f);
        }

        // Wrap handle (orange, right edge mid)
        float rx = boxX + tw + 8.f;
        ctx.setStrokeColor(Color::fromRGBA(30, 120, 255, 100));
        ctx.setLineWidth(1.f);
        ctx.beginPath();
        for (float dy2 = 0.f; dy2 < totalH; dy2 += 8.f)
        {
            ctx.moveTo(rx, boxY + dy2);
            ctx.lineTo(rx, boxY + std::min(dy2 + 4.f, totalH));
        }
        ctx.stroke();
        ctx.setFillColor(Color::fromRGBA(255, 160, 30, 220));
        ctx.fillRoundedRect(rx - 4.f, cy - 4.f, 8.f, 8.f, 2.f);

        // Scale handle (green, bottom-right) — Scale tool only
        if (activeTool_ == CanvasTool::Scale)
        {
            ctx.setFillColor(Color::fromRGBA(50, 210, 100, 220));
            ctx.fillRoundedRect(rx - 5.f, boxY + totalH - 5.f, 10.f, 10.f, 2.f);
            ctx.setStrokeColor(Color::fromRGBA(255, 255, 255, 180));
            ctx.setLineWidth(1.f);
            ctx.strokeRoundedRect(rx - 5.f, boxY + totalH - 5.f, 10.f, 10.f, 2.f);
        }

        ctx.restore(); // end rotation transform

        // ── Rotate handle — drawn in screen space (no rotation applied) ──
        if (activeTool_ == CanvasTool::Rotate && !hover)
        {
            float hx, hy;
            rotateHandlePos(b, hx, hy);

            // Stem line from top-center of box to handle
            float topCX = cx;
            float topCY = b.y - b.baselineShift - b.fontSize - 4.f;
            // Rotate the top-center point
            float cos_r = cosf(b.rotation), sin_r = sinf(b.rotation);
            float dx = topCX - cx, dy2 = topCY - cy;
            float rotTopX = cx + dx * cos_r - dy2 * sin_r;
            float rotTopY = cy + dx * sin_r + dy2 * cos_r;

            ctx.setStrokeColor(Color::fromRGBA(30, 120, 255, 120));
            ctx.setLineWidth(1.f);
            ctx.beginPath();
            ctx.moveTo(rotTopX, rotTopY);
            ctx.lineTo(hx, hy);
            ctx.stroke();

            // Circle handle
            ctx.setFillColor(Color::fromRGBA(255, 80, 180, 220));
            ctx.fillCircle(hx, hy, 7.f);
            ctx.setStrokeColor(Color::fromRGBA(255, 255, 255, 200));
            ctx.setLineWidth(1.5f);
            ctx.strokeCircle(hx, hy, 7.f);
        }
    }

    int hitTestBlock(float x, float y) const
    {
        for (int i = int(blocks_.size()) - 1; i >= 0; --i)
        {
            const auto &b = blocks_[i];
            float cx, cy;
            blockCenter(b, cx, cy);

            // Transform mouse point into block's local (unrotated) space
            float dx = x - cx, dy = y - cy;
            float cos_r = cosf(-b.rotation), sin_r = sinf(-b.rotation);
            float lx = cx + dx * cos_r - dy * sin_r;
            float ly = cy + dx * sin_r + dy * cos_r;

            float approxW = b.wrapWidth > 0.f
                                ? b.wrapWidth
                                : approxTextWidth(b.text, b.fontSize);
            auto lines = splitLines(b.text);
            int numLines = std::max(1, int(lines.size()));
            float lineH = b.fontSize * b.lineHeight;
            float shiftedY = b.y - b.baselineShift;

            float boxX0 = b.x - 4.f;
            float boxY0 = shiftedY - b.fontSize - 4.f;
            float boxX1 = b.x + approxW + 4.f;
            float boxY1 = shiftedY + (numLines - 1) * lineH + 6.f;

            if (lx >= boxX0 && lx <= boxX1 && ly >= boxY0 && ly <= boxY1)
                return i;
        }
        return -1;
    }

    void pushUndo()
    {
        undoStack_.push_back(blocks_);         // vector<TextBlock>
        undoStackPath_.push_back(pathBlocks_); // vector<TextOnPath>
        if (undoStack_.size() > 50)
        {
            undoStack_.erase(undoStack_.begin());
            undoStackPath_.erase(undoStackPath_.begin());
        }
        redoStack_.clear();
        redoStackPath_.clear(); // ADD if missing
    }

    // The rotate handle floats above the block center
    void rotateHandlePos(const TextBlock &b, float &hx, float &hy) const
    {
        float w = b.wrapWidth > 0.f
                      ? b.wrapWidth
                      : approxTextWidth(b.text, b.fontSize);
        auto lines = splitLines(b.text);
        int numLines = std::max(1, int(lines.size()));
        float lineH = b.fontSize * b.lineHeight;
        float totalH = b.fontSize + 10.f + (numLines - 1) * lineH;

        // Center of bounding box (unrotated local space)
        float localCX = b.x + w * 0.5f;
        float localCY = b.y - b.baselineShift - b.fontSize - 4.f + totalH * 0.5f;

        // Handle sits 28px above center, then rotated around center
        float handleOffsetY = -totalH * 0.5f - 28.f;
        float cos_r = cosf(b.rotation), sin_r = sinf(b.rotation);
        hx = localCX + sin_r * (-handleOffsetY); // rotate offset vector
        hy = localCY + cos_r * handleOffsetY;
    }

    void blockCenter(const TextBlock &b, float &cx, float &cy) const
    {
        float w = b.wrapWidth > 0.f
                      ? b.wrapWidth
                      : approxTextWidth(b.text, b.fontSize);
        auto lines = splitLines(b.text);
        int numLines = std::max(1, int(lines.size()));
        float lineH = b.fontSize * b.lineHeight;
        float totalH = b.fontSize + 10.f + (numLines - 1) * lineH;
        cx = b.x + w * 0.5f;
        cy = b.y - b.baselineShift - b.fontSize - 4.f + totalH * 0.5f;
    }

    bool hitRotateHandle(float x, float y, const TextBlock &b) const
    {
        float hx, hy;
        rotateHandlePos(b, hx, hy);
        float dx = x - hx, dy = y - hy;
        return dx * dx + dy * dy <= 8.f * 8.f; // circle hit, 8px radius
    }
    // Bottom-right corner of the block bounding box
    void scaleHandlePos(const TextBlock &b, float &hx, float &hy,
                        int blockIdx = -1) const
    {
        auto lines = getVisualLines(b.text, b.wrapWidth, b.fontSize);
        int numLines = std::max(1, int(lines.size()));
        float lineH = b.fontSize * b.lineHeight;
        float totalH = b.fontSize + 10.f + (numLines - 1) * lineH;

        float w;
        if (b.wrapWidth > 0.f)
            w = b.wrapWidth;
        else if (blockIdx >= 0 && blockIdx < int(blockMeasuredWidths_.size()) && blockMeasuredWidths_[blockIdx] > 0.f)
            w = blockMeasuredWidths_[blockIdx];
        else
            w = approxTextWidth(b.text, b.fontSize);

        float boxX = b.x - 4.f;
        float boxY = b.y - b.baselineShift - b.fontSize - 4.f;
        hx = boxX + w + 8.f;
        hy = boxY + totalH;
    }

    bool hitScaleHandle(float x, float y, const TextBlock &b,
                        int blockIdx = -1) const
    {
        float cx, cy;
        blockCenter(b, cx, cy);
        float dx = x - cx, dy = y - cy;
        float cos_r = cosf(-b.rotation), sin_r = sinf(-b.rotation);
        float lx = cx + dx * cos_r - dy * sin_r;
        float ly = cy + dx * sin_r + dy * cos_r;

        float hx, hy;
        scaleHandlePos(b, hx, hy, blockIdx);
        return lx >= hx - 7.f && lx <= hx + 7.f &&
               ly >= hy - 7.f && ly <= hy + 7.f;
    }
    // Returns the x position of the right-edge wrap handle for a selected block.
    // The handle is a small square on the right edge of the wrap box.
    float wrapHandleX(const TextBlock &b) const
    {
        return b.x + (b.wrapWidth > 0.f ? b.wrapWidth : approxTextWidth(b.text, b.fontSize)) + 4.f;
    }

    bool hitWrapHandle(float x, float y, const TextBlock &b) const
    {
        // Inverse-rotate mouse into local space
        float cx, cy;
        blockCenter(b, cx, cy);
        float dx = x - cx, dy = y - cy;
        float cos_r = cosf(-b.rotation), sin_r = sinf(-b.rotation);
        float lx = cx + dx * cos_r - dy * sin_r;
        float ly = cy + dx * sin_r + dy * cos_r;

        float hx = wrapHandleX(b);
        float hy = b.y - b.fontSize * 0.5f;
        return lx >= hx - 6.f && lx <= hx + 6.f &&
               ly >= hy - 6.f && ly <= hy + 6.f;
    }

    // Produce a normalized, non-overlapping, fully-resolved run list
    // covering [0, textLen).  Runs are sorted by start, no gaps.
    std::vector<ResolvedStyle> normalizeSpans(const TextBlock &b) const
    {
        int n = int(b.text.size());
        if (n == 0)
            return {};

        // ── 1. Collect all split points ──────────────────────────
        std::vector<int> pts = {0, n};
        for (auto &sp : b.spans)
        {
            int s = std::clamp(sp.start, 0, n);
            int e = std::clamp(sp.end, 0, n);
            if (s < e)
            {
                pts.push_back(s);
                pts.push_back(e);
            }
        }
        std::sort(pts.begin(), pts.end());
        pts.erase(std::unique(pts.begin(), pts.end()), pts.end());

        // ── For each interval resolve style ───────────────────
        std::vector<ResolvedStyle> runs;
        for (int pi = 0; pi + 1 < int(pts.size()); ++pi)
        {
            int s = pts[pi], e = pts[pi + 1];

            ResolvedStyle rs;
            rs.start = s;
            rs.end = e;
            // Defaults from block
            rs.bold = b.bold;
            rs.italic = b.italic;
            rs.fontSize = b.fontSize;
            rs.color = b.color;
            rs.baselineShift = b.baselineShift;
            rs.fontFamily = b.fontFamily;
            rs.strokeEnabled = b.strokeEnabled;
            rs.strokeColor = b.strokeColor;
            rs.strokeWidth = b.strokeWidth;

            // Apply every span that covers this interval (last writer wins)
            for (auto &sp : b.spans)
            {
                int ss = std::clamp(sp.start, 0, n);
                int se = std::clamp(sp.end, 0, n);
                if (ss > s || se < e)
                    continue; // doesn't fully cover

                if (sp.bold)
                    rs.bold = *sp.bold;
                if (sp.italic)
                    rs.italic = *sp.italic;
                if (sp.fontSize)
                    rs.fontSize = *sp.fontSize;
                if (sp.color)
                    rs.color = *sp.color;
                if (sp.baselineShift)
                    rs.baselineShift = *sp.baselineShift;
                if (sp.fontFamily)
                    rs.fontFamily = *sp.fontFamily;
                if (sp.underline)
                    rs.underline = true;
                if (sp.strikethrough)
                    rs.strikethrough = true;
                if (sp.allCaps)
                    rs.allCaps = true;
                if (sp.smallCaps)
                    rs.smallCaps = true;
            }
            runs.push_back(rs);
        }
        return runs;
    }

    // ── State ─────────────────────────────────────────────────
    std::vector<TextBlock> blocks_;
    std::vector<std::vector<TextBlock>> undoStack_, redoStack_;
    std::vector<float> blockMeasuredWidths_;

    std::vector<TextSpan> pendingSpans_;

    bool draggingWrapHandle_ = false;
    int wrapHandleIdx_ = -1;
    float wrapHandleDragStartX_ = 0.f;
    float wrapHandleStartW_ = 0.f;

    // Edit session
    bool editing_ = false;
    bool editingExisting_ = false;
    int selectedIdx_ = -1;
    float editX_ = 0, editY_ = 0;
    std::string editBuf_;

    double lastClickTime_ = 0.0;
    float lastClickX_ = 0.f;
    float lastClickY_ = 0.f;

    // Text selection within edit buffer
    int selStart_ = 0, selEnd_ = 0;

    // Mouse drag
    bool mouseDown_ = false;
    float dragStartX_ = 0, dragStartY_ = 0;
    float hoverX_ = 0, hoverY_ = 0;

    // Cursor blink
    double blinkTimer_ = 0.0;
    bool cursorVisible_ = true;

    int w_ = 850, h_ = 1100;

    double wallTime_ = 0.0;

    // Move-drag state
    bool draggingBlock_ = false;
    float dragBlockStartX_ = 0.f, dragBlockStartY_ = 0.f; // block origin at drag start
    float dragMouseStartX_ = 0.f, dragMouseStartY_ = 0.f; // mouse pos at drag start

    // Scale-drag state
    bool draggingScale_ = false;
    float scaleMouseStartX_ = 0.f, scaleMouseStartY_ = 0.f;
    float scaleStartWrapW_ = 0.f;
    float scaleStartFontSize_ = 0.f;

    // Rotate-drag state
    bool draggingRotate_ = false;
    float rotateCenterX_ = 0.f, rotateCenterY_ = 0.f; // block center in canvas space
    float rotateStartAngle_ = 0.f;                    // block rotation at drag start
    float rotateMouseStartAngle_ = 0.f;               // atan2 of mouse at drag start

    // Multi-select state
    std::vector<int> selectedIndices_;      // all selected block indices
    bool rubberBanding_ = false;            // currently drawing selection rect
    float rubberX0_ = 0.f, rubberY0_ = 0.f; // rubber band origin
    float rubberX1_ = 0.f, rubberY1_ = 0.f; // rubber band current corner

    // Multi-block drag
    bool draggingMulti_ = false;
    float multiDragStartX_ = 0.f, multiDragStartY_ = 0.f;
    std::vector<std::pair<float, float>> multiDragOrigins_; // per-block {x,y} at drag start

    // ── TextOnPath blocks ──────────────────────────────────────
    std::vector<TextOnPath> pathBlocks_;
    std::vector<std::vector<TextOnPath>> undoStackPath_;
    std::vector<std::vector<TextOnPath>> redoStackPath_;

    // Active path-drawing state
    bool drawingPath_ = false; // user is placing control points
    TextOnPath pendingPath_;   // being constructed
    int pathEditIdx_ = -1;     // which pathBlock is selected for editing
    int pathDragPt_ = -1;      // which control point handle is being dragged
    //  0 = anchor, 1 = cp1, 2 = cp2 per segment encoded as segIdx*3 + 0/1/2
};

// ============================================================
//  TextApp
// ============================================================

class TextApp : public Widget
{
    std::shared_ptr<CanvasWidget> canvas_;
    std::shared_ptr<TextSurface> surface_;
    std::shared_ptr<ColorPickerWidget> colorPicker_;

    // Tool buttons
    std::shared_ptr<ButtonWidget> selectToolBtn_;
    std::shared_ptr<ButtonWidget> textToolBtn_;

    // Font controls
    std::shared_ptr<DropdownWidget> fontFamilyDropdown_;
    std::shared_ptr<DropdownWidget> fontSizeDropdown_;
    std::shared_ptr<TextInputWidget> fontSizeInput_; // numeric override
    State<std::string> fontSizeState_{"18"};         // backing state for fontSizeInput_
    std::shared_ptr<ButtonWidget> boldBtn_;
    std::shared_ptr<ButtonWidget> italicBtn_;

    std::shared_ptr<ButtonWidget> underlineBtn_;
    std::shared_ptr<ButtonWidget> strikethroughBtn_;

    std::shared_ptr<ButtonWidget> allCapsBtn_;
    std::shared_ptr<ButtonWidget> smallCapsBtn_;

    std::shared_ptr<SliderWidget> baselineShiftSlider_;
    std::shared_ptr<ButtonWidget> superscriptBtn_;
    std::shared_ptr<ButtonWidget> subscriptBtn_;
    std::shared_ptr<ButtonWidget> baselineResetBtn_;

    std::shared_ptr<SliderWidget> lineHeightSlider_;
    State<double> lineHeightState_{1.3};
    std::shared_ptr<SliderWidget> letterSpacingSlider_;
    std::shared_ptr<ButtonWidget> kerningBtn_;

    std::shared_ptr<ButtonWidget> alignLeftBtn_;
    std::shared_ptr<ButtonWidget> alignCenterBtn_;
    std::shared_ptr<ButtonWidget> alignRightBtn_;
    std::shared_ptr<ButtonWidget> alignJustifyBtn_;

    std::shared_ptr<ButtonWidget> moveToolBtn_;
    std::shared_ptr<ButtonWidget> scaleToolBtn_;
    std::shared_ptr<ButtonWidget> rotateToolBtn_;
    std::shared_ptr<ButtonWidget> rotateResetBtn_;

    std::shared_ptr<ButtonWidget> multiSelectBtn_;
    std::shared_ptr<ButtonWidget> deleteAllBtn_;
    std::shared_ptr<ButtonWidget> bulkBoldBtn_;
    std::shared_ptr<ButtonWidget> bulkSizeUpBtn_;
    std::shared_ptr<ButtonWidget> bulkSizeDownBtn_;

    std::shared_ptr<SliderWidget> opacitySlider_;
    std::shared_ptr<ButtonWidget> bgToggleBtn_;
    std::shared_ptr<ColorPickerWidget> bgColorPicker_;
    std::shared_ptr<ButtonWidget> strokeToggleBtn_;
    std::shared_ptr<ColorPickerWidget> strokeColorPicker_;
    std::shared_ptr<SliderWidget> strokeWidthSlider_;
    std::shared_ptr<ButtonWidget> shadowToggleBtn_;
    std::shared_ptr<SliderWidget> shadowXSlider_;
    std::shared_ptr<SliderWidget> shadowYSlider_;

    // Reactive state
    State<bool> canUndo_{false};
    State<bool> canRedo_{false};
    State<std::string> posLabel_{"0, 0"};
    State<std::string> zoomLabel_{"100%"};
    State<std::string> blockCountLabel_{"0 blocks"};
    State<std::string> toolLabel_{"Select"};

    // Font size presets
    static constexpr float kFontSizes[] = {10, 12, 14, 16, 18, 20, 24, 28, 32, 40, 48, 64, 72, 96};
    static constexpr int kNumSizes = int(std::size(kFontSizes));

    // Font families: display label -> font key used in TextBlock
    struct FontEntry
    {
        std::string label;
        std::string key;
    };
    std::vector<FontEntry> fontEntries_ = {
        {"Sans Serif", "sans"},
        {"Monospace", "mono"},
    };

    static constexpr Color kToolActiveBg = {40, 100, 220, 255};
    static constexpr Color kToolInactiveBg = {50, 50, 54, 255};
    static constexpr Color kStyleActiveBg = {60, 130, 240, 255};
    static constexpr Color kStyleInactiveBg = {50, 50, 54, 255};

    // ── Helpers ───────────────────────────────────────────────

    void updateTransformButtons()
    {
        auto lo = kToolInactiveBg;
        auto hi = kToolActiveBg;
        if (!surface_)
            return;
        auto t = surface_->activeTool_;
        if (moveToolBtn_)
            moveToolBtn_->setBackgroundColor(
                t == CanvasTool::Move ? hi : lo);
        if (scaleToolBtn_)
            scaleToolBtn_->setBackgroundColor(
                t == CanvasTool::Scale ? hi : lo);
        if (rotateToolBtn_)
            rotateToolBtn_->setBackgroundColor(
                t == CanvasTool::Rotate ? hi : lo);
        if (multiSelectBtn_)
            multiSelectBtn_->setBackgroundColor(
                t == CanvasTool::MultiSelect ? hi : lo);
    }

    void refreshState()
    {
        if (!surface_)
            return;
        canUndo_.set(surface_->canUndo());
        canRedo_.set(surface_->canRedo());
        int n = surface_->blockCount();
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%d block%s", n, n == 1 ? "" : "s");
        blockCountLabel_.set(buf);
    }

    void setActiveTool(CanvasTool t)
    {
        if (!surface_)
            return;

        if (surface_->isEditing())
        {
            if (surface_->hasEditContent())
                surface_->commitEdit();
            else
                surface_->cancelEdit();
            refreshState();
            if (canvas_)
                canvas_->redraw();
        }

        surface_->activeTool_ = t;
        bool isSel = (t == CanvasTool::Select);
        if (selectToolBtn_)
            selectToolBtn_->setBackgroundColor(isSel ? kToolActiveBg : kToolInactiveBg);
        if (textToolBtn_)
            textToolBtn_->setBackgroundColor(
                t == CanvasTool::Text ? kToolActiveBg : kToolInactiveBg);
        updateTransformButtons();
        std::string label = "Select";
        if (t == CanvasTool::Text)
            label = "Text";
        else if (t == CanvasTool::Move)
            label = "Move";
        else if (t == CanvasTool::Scale)
            label = "Scale";
        else if (t == CanvasTool::Rotate)
            label = "Rotate";
        else if (t == CanvasTool::MultiSelect)
            label = "Multi-Select";
        else if (t == CanvasTool::PathText)
            label = "Path Text";
        toolLabel_.set(label);
    }

    void updateStyleButtons()
    {
        if (!surface_)
            return;

        bool bold = surface_->isEditing() && surface_->hasSelection()
                        ? surface_->selectionAllBold()
                        : surface_->activeBold_;
        bool italic = surface_->isEditing() && surface_->hasSelection()
                          ? surface_->selectionAllItalic()
                          : surface_->activeItalic_;
        bool ul = surface_->isEditing() && surface_->hasSelection() && surface_->selectionAllUnderline();
        bool st = surface_->isEditing() && surface_->hasSelection() && surface_->selectionAllStrikethrough();

        if (boldBtn_)
            boldBtn_->setBackgroundColor(bold ? kStyleActiveBg : kStyleInactiveBg);
        if (italicBtn_)
            italicBtn_->setBackgroundColor(italic ? kStyleActiveBg : kStyleInactiveBg);
        if (underlineBtn_)
            underlineBtn_->setBackgroundColor(ul ? kStyleActiveBg : kStyleInactiveBg);
        if (strikethroughBtn_)
            strikethroughBtn_->setBackgroundColor(st ? kStyleActiveBg : kStyleInactiveBg);

        // Sync font size display to selection's font size
        if (surface_ && surface_->isEditing() && surface_->hasSelection())
        {
            // Get font size of the first run in selection
            float selFs = surface_->selectionFontSize();
            if (selFs > 0.f)
            {
                char buf[16];
                std::snprintf(buf, sizeof(buf), "%.0f", selFs);
                fontSizeState_.set(buf);
            }
        }
    }

    void syncBaselineShiftSlider()
    {
        if (!baselineShiftSlider_ || !surface_)
            return;
        baselineShiftSlider_->value = double(surface_->activeBaselineShift_);
        baselineShiftSlider_->markNeedsPaint();
    }

    void syncAlignButtons()
    {
        if (!surface_)
            return;
        auto hi = kStyleActiveBg;
        auto lo = kStyleInactiveBg;
        TextAlign ta = surface_->activeTextAlign_;
        if (alignLeftBtn_)
            alignLeftBtn_->setBackgroundColor(ta == TextAlign::Left ? hi : lo);
        if (alignCenterBtn_)
            alignCenterBtn_->setBackgroundColor(ta == TextAlign::Center ? hi : lo);
        if (alignRightBtn_)
            alignRightBtn_->setBackgroundColor(ta == TextAlign::Right ? hi : lo);
        if (alignJustifyBtn_)
            alignJustifyBtn_->setBackgroundColor(ta == TextAlign::Justify ? hi : lo);
    }

    void syncKerningButton()
    {
        if (!kerningBtn_ || !surface_)
            return;
        kerningBtn_->setBackgroundColor(
            surface_->activeKerning_ ? kStyleActiveBg : kStyleInactiveBg);
    }

    // Sync font family dropdown to current surface state
    void syncFontFamilyDropdown()
    {
        if (!fontFamilyDropdown_ || !surface_)
            return;
        const std::string &key = surface_->activeFontFamily_;
        for (int i = 0; i < int(fontEntries_.size()); ++i)
        {
            if (fontEntries_[i].key == key)
            {
                fontFamilyDropdown_->selectedIndex = i;
                fontFamilyDropdown_->markNeedsPaint();
                break;
            }
        }
    }

    void syncLetterSpacingSlider()
    {
        if (!letterSpacingSlider_ || !surface_)
            return;
        letterSpacingSlider_->value = double(surface_->activeLetterSpacing_);
        letterSpacingSlider_->markNeedsPaint();
    }

    // Sync font size dropdown / input to current surface state
    void syncFontSizeControls()
    {
        if (!surface_)
            return;
        float fs = surface_->activeFontSize_;

        // Update numeric input — write to the backing State so the widget reflects it
        {
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%.0f", fs);
            fontSizeState_.set(buf);
        }

        // Update size dropdown
        if (fontSizeDropdown_)
        {
            for (int i = 0; i < kNumSizes; ++i)
            {
                if (std::abs(kFontSizes[i] - fs) < 0.5f)
                {
                    fontSizeDropdown_->selectedIndex = i;
                    break;
                }
            }
        }
    }

    void syncLineHeightSlider()
    {
        if (!lineHeightSlider_ || !surface_)
            return;
        lineHeightSlider_->value = double(surface_->activeLineHeight_);
        lineHeightSlider_->markNeedsPaint();
    }

    // Called when a selected block changes — reflect its style in the sidebar
    void onBlockSelected()
    {
        if (!surface_)
            return;
        const TextBlock *b = surface_->selectedBlock();
        if (!b)
            return;

        surface_->activeFontSize_ = b->fontSize;
        surface_->activeFontFamily_ = b->fontFamily;
        surface_->activeBold_ = b->bold;
        surface_->activeItalic_ = b->italic;
        surface_->activeColor_ = b->color;
        surface_->activeLineHeight_ = b->lineHeight;
        surface_->activeKerning_ = b->kerning;
        surface_->activeTextAlign_ = b->textAlign;
        surface_->activeBaselineShift_ = b->baselineShift;

        syncBaselineShiftSlider();
        syncAlignButtons();
        syncKerningButton();
        syncLetterSpacingSlider();
        syncLineHeightSlider();
        syncFontFamilyDropdown();
        syncFontSizeControls();
        updateStyleButtons();
        if (colorPicker_)
            colorPicker_->setColor(b->color);

        // ── Sync appearance controls ──────────────────────────────
        if (opacitySlider_)
        {
            opacitySlider_->value = double(b->opacity);
            opacitySlider_->markNeedsPaint();
        }
        if (bgToggleBtn_)
            bgToggleBtn_->setBackgroundColor(b->bgEnabled ? kStyleActiveBg : kStyleInactiveBg);
        if (bgColorPicker_)
            bgColorPicker_->setColor(b->bgColor);

        if (strokeToggleBtn_)
            strokeToggleBtn_->setBackgroundColor(b->strokeEnabled ? kStyleActiveBg : kStyleInactiveBg);
        if (strokeWidthSlider_)
        {
            strokeWidthSlider_->value = double(b->strokeWidth);
            strokeWidthSlider_->markNeedsPaint();
        }
        if (strokeColorPicker_)
            strokeColorPicker_->setColor(b->strokeColor);

        if (shadowToggleBtn_)
            shadowToggleBtn_->setBackgroundColor(b->shadowEnabled ? kStyleActiveBg : kStyleInactiveBg);
        if (shadowXSlider_)
        {
            shadowXSlider_->value = double(b->shadowOffsetX);
            shadowXSlider_->markNeedsPaint();
        }
        if (shadowYSlider_)
        {
            shadowYSlider_->value = double(b->shadowOffsetY);
            shadowYSlider_->markNeedsPaint();
        }
    }

public:
    WidgetPtr build() override
    {
        // ── Canvas ────────────────────────────────────────────
        canvas_ = std::make_shared<CanvasWidget>();
        canvas_->setViewportEnabled(true);
        canvas_->setScrollbarsEnabled(true);
        canvas_->setCanvasSize(850, 1100);

        canvas_->onViewportChanged = [this](float zoom)
        {
            if (surface_)
                surface_->currentZoom_ = zoom;
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%.0f%%", zoom * 100.f);
            zoomLabel_.set(buf);
        };

        surface_ = canvas_->setSurface<TextSurface>();

        surface_->onMousePos = [this](float x, float y)
        {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.0f, %.0f", x, y);
            posLabel_.set(buf);
        };
        surface_->onCommitted = [this]()
        { refreshState(); canvas_->redraw(); };
        surface_->onSelectionChanged = [this]()
        { onBlockSelected(); updateStyleButtons(); canvas_->redraw(); };

        std::weak_ptr<TextSurface> ws = surface_;
        std::weak_ptr<CanvasWidget> wc = canvas_;

        // ── Tool buttons ──────────────────────────────────────
        selectToolBtn_ = Button("Select")
                             ->setHeight(30)
                             ->setWidth(100)
                             ->setBackgroundColor(kToolActiveBg) // default tool
                             ->setOnClick([this]()
                                          { setActiveTool(CanvasTool::Select); });

        textToolBtn_ = Button("Text")
                           ->setHeight(30)
                           ->setWidth(100)
                           ->setBackgroundColor(kToolInactiveBg)
                           ->setOnClick([this]()
                                        { setActiveTool(CanvasTool::Text); });

        auto pathTextBtn_ = Button("PathText")
                                ->setHeight(30)
                                ->setWidth(100)
                                ->setBackgroundColor(kToolInactiveBg)
                                ->setOnClick([this]
                                             { setActiveTool(CanvasTool::PathText); });

        letterSpacingSlider_ = Slider(-5.0, 20.0, 0.5);
        letterSpacingSlider_->value = 0.0;
        letterSpacingSlider_->setOnValueChanged([ws, wc](double val)
                                                {
    if (auto s = ws.lock())
        s->activeLetterSpacing_ = float(val);
    if (auto c = wc.lock()) c->redraw(); });

        // ── Line Height slider ────────────────────────────────────────
        lineHeightSlider_ = Slider(0.8, 3.0, 0.05);
        lineHeightSlider_->value = 1.3;
        lineHeightSlider_->setOnValueChanged([ws, wc](double val)
                                             {
    if (auto s = ws.lock()) {
        s->activeLineHeight_ = float(val);
        // If editing an existing block, update it live
        if (s->isEditing() && s->selectedBlockIndex() >= 0) {
            // will apply on commit; redraw for preview
        }
    }
    if (auto c = wc.lock()) c->redraw(); });

        // ── Font family dropdown ───────────────────────────────
        std::vector<std::string> familyLabels;
        for (auto &e : fontEntries_)
            familyLabels.push_back(e.label);

        fontFamilyDropdown_ = Dropdown(familyLabels);
        fontFamilyDropdown_->selectedIndex = 0;

        fontFamilyDropdown_->setOnSelectionChanged([this, ws](int idx, const std::string &)
                                                   {
            if (auto s = ws.lock())
            {
                if (idx >= 0 && idx < int(fontEntries_.size()))
                    s->activeFontFamily_ = fontEntries_[idx].key;
            } });

        // ── Font size dropdown + numeric input ────────────────
        std::vector<std::string> sizeLabels;
        for (int i = 0; i < kNumSizes; ++i)
        {
            char buf[8];
            std::snprintf(buf, sizeof(buf), "%.0f", kFontSizes[i]);
            sizeLabels.push_back(buf);
        }
        fontSizeDropdown_ = Dropdown(sizeLabels);

        fontSizeDropdown_->selectedIndex = 4; // 18px default
        fontSizeDropdown_->setOnSelectionChanged([this, ws, wc](int idx, const std::string &)
                                                 {
    if (idx < 0 || idx >= kNumSizes) return;
    float fs = kFontSizes[idx];
    if (auto s = ws.lock())
    {
        if (s->isEditing() && s->hasSelection())
        {
            s->applyStyleToSelection([fs](ResolvedStyle &rs){
                rs.fontSize = fs;
            });
        }
        else
        {
            s->activeFontSize_ = fs;
            syncFontSizeControls();
        }
    }
    if (auto c = wc.lock()) c->redraw(); });

        fontSizeState_.listen([this, ws, wc](const std::string &t)
                              {
    try
    {
        float fs = std::stof(t);
        fs = std::clamp(fs, 6.f, 200.f);
        if (auto s = ws.lock())
        {
            if (s->isEditing() && s->hasSelection())
            {
                s->applyStyleToSelection([fs](ResolvedStyle &rs){
                    rs.fontSize = fs;
                });
            }
            else
            {
                s->activeFontSize_ = fs;
            }
        }
        if (auto c = wc.lock()) c->redraw();
    }
    catch(...) {} });

        fontSizeInput_ = TextInput(); // no placeholder — shows the live value

        fontSizeInput_->setWidth(52);
        fontSizeInput_->setInputValue(fontSizeState_);

        baselineShiftSlider_ = Slider(-60.0, 60.0, 0.5);
        baselineShiftSlider_->value = 0.0;
        baselineShiftSlider_->setOnValueChanged([ws, wc](double val)
                                                {
    if (auto s = ws.lock())
        s->activeBaselineShift_ = float(val);
    if (auto c = wc.lock()) c->redraw(); });

        // Preset: superscript — raise by ~40% of current font size
        superscriptBtn_ = Button("Sup")
                              ->setHeight(26)

                              ->setBackgroundColor(kStyleInactiveBg)
                              ->setOnClick([this, ws, wc]()
                                           {
        if (auto s = ws.lock()) {
            s->activeBaselineShift_ = s->activeFontSize_ * 0.4f;
            syncBaselineShiftSlider();
        }
        if (auto c = wc.lock()) c->redraw(); });

        // Preset: subscript — lower by ~25% of current font size
        subscriptBtn_ = Button("Sub")
                            ->setHeight(26)

                            ->setBackgroundColor(kStyleInactiveBg)
                            ->setOnClick([this, ws, wc]()
                                         {
        if (auto s = ws.lock()) {
            s->activeBaselineShift_ = -s->activeFontSize_ * 0.25f;
            syncBaselineShiftSlider();
        }
        if (auto c = wc.lock()) c->redraw(); });

        // Reset to zero
        baselineResetBtn_ = Button("0")
                                ->setHeight(26)

                                ->setBackgroundColor(kStyleInactiveBg)
                                ->setOnClick([this, ws, wc]()
                                             {
        if (auto s = ws.lock()) {
            s->activeBaselineShift_ = 0.f;
            syncBaselineShiftSlider();
        }
        if (auto c = wc.lock()) c->redraw(); });

        underlineBtn_ = Button("U")
                            ->setHeight(28)
                            ->setWidth(34)
                            ->setBackgroundColor(kStyleInactiveBg)
                            ->setOnClick([this, ws, wc]()
                                         {
        if (auto s = ws.lock()) {
            if (s->isEditing() && s->hasSelection())
            {
                bool turnOn = !s->selectionAllUnderline();
                s->applyStyleToSelection([turnOn](ResolvedStyle &rs){
                    rs.underline = turnOn;
                });
            }
        }
        if (auto c = wc.lock()) c->redraw(); });

        strikethroughBtn_ = Button("S")
                                ->setHeight(28)
                                ->setWidth(34)
                                ->setBackgroundColor(kStyleInactiveBg)
                                ->setOnClick([this, ws, wc]()
                                             {
        if (auto s = ws.lock()) {
            if (s->isEditing() && s->hasSelection())
            {
                bool turnOn = !s->selectionAllStrikethrough();
                s->applyStyleToSelection([turnOn](ResolvedStyle &rs){
                    rs.strikethrough = turnOn;
                });
            }
        }
        if (auto c = wc.lock()) c->redraw(); });

        // ── Bold / Italic ─────────────────────────────────────
        boldBtn_ = Button("B")
                       ->setHeight(28)
                       ->setWidth(34)
                       ->setBackgroundColor(kStyleInactiveBg)
                       ->setOnClick([this, ws, wc]()
                                    {
        if (auto s = ws.lock()) {
            if (s->isEditing() && s->hasSelection())
            {
                // Determine toggle state — if all bold, turn off; else turn on
                bool turnOn = !s->selectionAllBold();
                s->applyStyleToSelection([turnOn](ResolvedStyle &rs){
                    rs.bold = turnOn;
                });
            }
            else
            {
                s->activeBold_ = !s->activeBold_;
            }
            updateStyleButtons();
        }
        if (auto c = wc.lock()) c->redraw(); });

        allCapsBtn_ = Button("AC")
                          ->setHeight(28)

                          ->setBackgroundColor(kStyleInactiveBg)
                          ->setOnClick([this, ws, wc]()
                                       {
        if (auto s = ws.lock()) {
            if (s->isEditing() && s->hasSelection()) {
                bool turnOn = !s->selectionBoolQueryPublic(
                    [](const ResolvedStyle &rs){ return rs.allCaps; });
                s->applyStyleToSelection([turnOn](ResolvedStyle &rs){
                    rs.allCaps = turnOn;
                    if (turnOn) rs.smallCaps = false;
                });
            }
        }
        if (auto c = wc.lock()) c->redraw(); });

        smallCapsBtn_ = Button("SC")
                            ->setHeight(28)

                            ->setBackgroundColor(kStyleInactiveBg)
                            ->setOnClick([this, ws, wc]()
                                         {
        if (auto s = ws.lock()) {
            if (s->isEditing() && s->hasSelection()) {
                bool turnOn = !s->selectionBoolQueryPublic(
                    [](const ResolvedStyle &rs){ return rs.smallCaps; });
                s->applyStyleToSelection([turnOn](ResolvedStyle &rs){
                    rs.smallCaps = turnOn;
                    if (turnOn) rs.allCaps = false;
                });
            }
        }
        if (auto c = wc.lock()) c->redraw(); });

        italicBtn_ = Button("I")
                         ->setHeight(28)
                         ->setWidth(34)
                         ->setBackgroundColor(kStyleInactiveBg)
                         ->setOnClick([this, ws, wc]()
                                      {
        if (auto s = ws.lock()) {
            if (s->isEditing() && s->hasSelection())
            {
                bool turnOn = !s->selectionAllItalic();
                s->applyStyleToSelection([turnOn](ResolvedStyle &rs){
                    rs.italic = turnOn;
                });
            }
            else
            {
                s->activeItalic_ = !s->activeItalic_;
            }
            updateStyleButtons();
        }
        if (auto c = wc.lock()) c->redraw(); });

        auto makeAlignBtn = [&](const char *label, TextAlign ta)
        {
            return Button(label)
                ->setHeight(28)
                ->setWidth(40)
                ->setBackgroundColor(ta == TextAlign::Left ? kStyleActiveBg : kStyleInactiveBg)
                ->setOnClick([this, ws, wc, ta]()
                             {
            if (auto s = ws.lock()) s->activeTextAlign_ = ta;
            syncAlignButtons();
            if (auto c = wc.lock()) c->redraw(); });
        };

        alignLeftBtn_ = makeAlignBtn("L", TextAlign::Left);
        alignCenterBtn_ = makeAlignBtn("C", TextAlign::Center);
        alignRightBtn_ = makeAlignBtn("R", TextAlign::Right);
        alignJustifyBtn_ = makeAlignBtn("J", TextAlign::Justify);

        // ── Color picker ──────────────────────────────────────
        colorPicker_ = ColorPicker(Color::fromRGB(20, 20, 20));
        colorPicker_->pickerSize = 104;
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
        colorPicker_->setOnColorChanged([ws, wc](Color c)
                                        {
    if (auto s = ws.lock()) {
        s->activeColor_ = c;
        if (s->isEditing() && s->hasSelection())
        {
            s->applyStyleToSelection([c](ResolvedStyle &rs){
                rs.color = c;
            });
        }
    }
    if (auto c2 = wc.lock()) c2->redraw(); });

        // ── Quick swatches ────────────────────────────────────
        const std::vector<Color> swatches = {
            Color::fromRGB(20, 20, 20),
            Color::fromRGB(80, 80, 80),
            Color::fromRGB(160, 160, 160),
            Color::fromRGB(220, 50, 50),
            Color::fromRGB(30, 100, 220),
            Color::fromRGB(30, 160, 80),
            Color::fromRGB(200, 140, 0),
            Color::fromRGB(140, 50, 200),
            Color::fromRGB(255, 255, 255),
        };
        auto swatchRow = Row({});
        swatchRow->setSpacing(3);
        for (auto &c : swatches)
        {
            auto btn = Button("")
                           ->setHeight(20)
                           ->setWidth(20)
                           ->setBackgroundColor(c)
                           ->setBorderRadius(10)
                           ->setOnClick([this, ws, wc, c]()
                                        {
    if (auto s = ws.lock()) {
        s->activeColor_ = c;
        if (s->isEditing() && s->hasSelection())
        {
            s->applyStyleToSelection([c](ResolvedStyle &rs){
                rs.color = c;
            });
        }
    }
    if (colorPicker_) colorPicker_->setColor(c);
    if (auto cv = wc.lock()) cv->redraw(); });
            swatchRow->addChild(btn);
        }

        kerningBtn_ = Button("Kern")
                          ->setHeight(28)
                          ->setWidth(48)
                          ->setBackgroundColor(kStyleActiveBg) // on by default
                          ->setOnClick([this, ws, wc]()
                                       {
        if (auto s = ws.lock()) {
            s->activeKerning_ = !s->activeKerning_;
            // update button color
            kerningBtn_->setBackgroundColor(
                s->activeKerning_ ? kStyleActiveBg : kStyleInactiveBg);
        }
        if (auto c = wc.lock()) c->redraw(); });

        // ── Undo / Redo / Clear ───────────────────────────────
        auto undoBtn = Button("↩")
                           ->setHeight(26)
                           ->setWidth(36)
                           ->setOnClick([this, ws, wc]()
                                        {
                if (auto s = ws.lock()) s->undo();
                refreshState();
                if (auto c = wc.lock()) c->redraw(); });

        auto redoBtn = Button("↪")
                           ->setHeight(26)
                           ->setWidth(36)
                           ->setOnClick([this, ws, wc]()
                                        {
                if (auto s = ws.lock()) s->redo();
                refreshState();
                if (auto c = wc.lock()) c->redraw(); });

        auto clearBtn = Button("Clear All")
                            ->setHeight(26)
                            ->setOnClick([this, ws, wc]()
                                         {
                if (auto s = ws.lock()) s->clearAll();
                refreshState();
                if (auto c = wc.lock()) c->redraw(); });

        auto commitBtn = Button("Commit  ↵")
                             ->setHeight(26)
                             ->setOnClick([this, ws, wc]()
                                          {
                if (auto s = ws.lock()) s->commitEdit();
                refreshState();
                if (auto c = wc.lock()) c->redraw(); });

        auto deleteBtn = Button("Delete Block")
                             ->setHeight(26)
                             ->setOnClick([this, ws, wc]()
                                          {
                if (auto s = ws.lock()) s->deleteSelected();
                refreshState();
                if (auto c = wc.lock()) c->redraw(); });

        // ── Zoom controls ─────────────────────────────────────
        auto zoomInBtn = Button("+")
                             ->setHeight(26)
                             ->setWidth(30)
                             ->setOnClick([wc]()
                                          { if (auto c = wc.lock()) c->viewport().zoomIn(); });

        auto zoomOutBtn = Button("−")
                              ->setHeight(26)
                              ->setWidth(30)
                              ->setOnClick([wc]()
                                           { if (auto c = wc.lock()) c->viewport().zoomOut(); });

        auto zoomFitBtn = Button("Fit")
                              ->setHeight(26)
                              ->setWidth(36)
                              ->setOnClick([wc]()
                                           { if (auto c = wc.lock()) c->viewport().fitToView(); });

        // ── Sidebar label ─────────────────────────────────────
        auto sideLabel = [](const char *s)
        {
            return Text(s)
                ->setFontSize(9)
                ->setTextColor(Color::fromRGB(140, 140, 160))
                ->setPadding(8);
        };

        moveToolBtn_ = Button("Move")
                           ->setHeight(30)
                           ->setWidth(100)
                           ->setBackgroundColor(kToolInactiveBg)
                           ->setOnClick([this]
                                        { setActiveTool(CanvasTool::Move); });

        scaleToolBtn_ = Button("Scale")
                            ->setHeight(30)
                            ->setWidth(100)
                            ->setBackgroundColor(kToolInactiveBg)
                            ->setOnClick([this]
                                         { setActiveTool(CanvasTool::Scale); });

        rotateToolBtn_ = Button("Rotate")
                             ->setHeight(30)
                             ->setWidth(100)
                             ->setBackgroundColor(kToolInactiveBg)
                             ->setOnClick([this]
                                          { setActiveTool(CanvasTool::Rotate); });

        rotateResetBtn_ = Button("Reset")
                              ->setHeight(26)
                              ->setWidth(100)
                              ->setBackgroundColor(kStyleInactiveBg)
                              ->setOnClick([this, ws, wc]()
                                           {
        if (auto s = ws.lock()) {
            if (s->selectedBlockIndex() >= 0 &&
                s->selectedBlockIndex() < s->blockCount())
            {
                // Access via public method — add one (see below)
                s->resetSelectedRotation();
            }
        }
        if (auto c = wc.lock()) c->redraw(); });

        multiSelectBtn_ = Button("Multi")
                              ->setHeight(30)
                              ->setWidth(100)
                              ->setBackgroundColor(kToolInactiveBg)
                              ->setOnClick([this]
                                           { setActiveTool(CanvasTool::MultiSelect); });

        deleteAllBtn_ = Button("Del All")
                            ->setHeight(26)
                            ->setWidth(100)
                            ->setBackgroundColor(Color::fromRGB(160, 40, 40))
                            ->setOnClick([this, ws, wc]()
                                         {
        if (auto s = ws.lock()) s->deleteAllSelected();
        refreshState();
        if (auto c = wc.lock()) c->redraw(); });

        bulkBoldBtn_ = Button("Bold All")
                           ->setHeight(26)
                           ->setWidth(100)
                           ->setBackgroundColor(kStyleInactiveBg)
                           ->setOnClick([this, ws, wc]()
                                        {
        if (auto s = ws.lock())
            s->applyToSelected([](TextBlock &b){ b.bold = !b.bold; });
        if (auto c = wc.lock()) c->redraw(); });

        bulkSizeUpBtn_ = Button("Size +4")
                             ->setHeight(26)
                             ->setWidth(100)
                             ->setBackgroundColor(kStyleInactiveBg)
                             ->setOnClick([this, ws, wc]()
                                          {
        if (auto s = ws.lock())
            s->applyToSelected([](TextBlock &b){
                b.fontSize = std::clamp(b.fontSize + 4.f, 6.f, 200.f);
            });
        if (auto c = wc.lock()) c->redraw(); });

        bulkSizeDownBtn_ = Button("Size -4")
                               ->setHeight(26)
                               ->setWidth(100)
                               ->setBackgroundColor(kStyleInactiveBg)
                               ->setOnClick([this, ws, wc]()
                                            {
        if (auto s = ws.lock())
            s->applyToSelected([](TextBlock &b){
                b.fontSize = std::clamp(b.fontSize - 4.f, 6.f, 200.f);
            });
        if (auto c = wc.lock()) c->redraw(); });

        opacitySlider_ = Slider(0.0, 1.0, 0.01);
        opacitySlider_->value = 1.0;
        opacitySlider_->setOnValueChanged([ws, wc](double val)
                                          {
    if (auto s = ws.lock()) {
        if (s->selectedBlock()) {
            // apply to selected block directly
            s->applyToSelectedBlock([val](TextBlock& b){ b.opacity = float(val); });
        }
    }
    if (auto c = wc.lock()) c->redraw(); });

        // ── Sidebar ───────────────────────────────────────────
        auto sidebar = Container(
                           ListView({
                               // TOOLS
                               sideLabel("TOOLS"),

                               Row({selectToolBtn_, SizedBox(4, 0), textToolBtn_})->setPadding(6),
                               Row({pathTextBtn_, SizedBox(4, 0), multiSelectBtn_})->setPadding(6),
                               SizedBox(0, 4),
                               sideLabel("TRANSFORM"),
                               Container(moveToolBtn_)->setPadding(6),
                               Container(scaleToolBtn_)->setPadding(6),
                               Container(rotateToolBtn_)->setPadding(6),
                               sideLabel("ROTATION"),
                               Container(rotateResetBtn_)->setPadding(6)->setHeight(40),

                               sideLabel("MULTI-EDIT"),
                               Container(bulkBoldBtn_)->setPadding(6)->setHeight(38),
                               Container(bulkSizeUpBtn_)->setPadding(6)->setHeight(38),
                               Container(bulkSizeDownBtn_)->setPadding(6)->setHeight(38),
                               Container(deleteAllBtn_)->setPadding(6)->setHeight(38),

                           }))
                           ->setWidth(220)
                           ->setBackgroundColor(Color::fromRGB(28, 28, 30));

        // ── Background toggle + color picker ─────────────────────
        bgToggleBtn_ = Button("BG")
                           ->setHeight(28)

                           ->setBackgroundColor(kStyleInactiveBg)
                           ->setOnClick([this, ws, wc]()
                                        {
        if (auto s = ws.lock()) {
            s->applyToSelectedBlock([](TextBlock &b) {
                b.bgEnabled = !b.bgEnabled;
            });
        }
        // Sync button color
        if (auto s = ws.lock()) {
            const TextBlock *b = s->selectedBlock();
            if (bgToggleBtn_)
                bgToggleBtn_->setBackgroundColor(
                    (b && b->bgEnabled) ? kStyleActiveBg : kStyleInactiveBg);
        }
        if (auto c = wc.lock()) c->redraw(); });

        bgColorPicker_ = ColorPicker(Color::fromRGBA(255, 255, 255, 180));
        bgColorPicker_->pickerSize = 90;
        bgColorPicker_->hueBarHeight = 10;
        bgColorPicker_->alphaBarHeight = 10;
        bgColorPicker_->barSpacing = 4;
        bgColorPicker_->previewSize = 16;
        bgColorPicker_->hexInputHeight = 18;
        bgColorPicker_->paddingLeft = 4;
        bgColorPicker_->paddingRight = 4;
        bgColorPicker_->paddingTop = 3;
        bgColorPicker_->paddingBottom = 3;
        bgColorPicker_->showAlpha = true; // background alpha matters
        bgColorPicker_->width = bgColorPicker_->pickerSize + bgColorPicker_->paddingLeft + bgColorPicker_->paddingRight;
        bgColorPicker_->setOnColorChanged([ws, wc](Color c)
                                          {
    if (auto s = ws.lock()) {
        s->applyToSelectedBlock([c](TextBlock &b) {
            b.bgColor = c;
            // Auto-enable bg when color is picked with visible alpha
            if (c.a > 0) b.bgEnabled = true;
        });
    }
    if (auto cv = wc.lock()) cv->redraw(); });

        // ── Stroke toggle + width slider ─────────────────────────
        strokeToggleBtn_ = Button("Stroke")
                               ->setHeight(28)

                               ->setBackgroundColor(kStyleInactiveBg)
                               ->setOnClick([this, ws, wc]()
                                            {
        if (auto s = ws.lock()) {
            s->applyToSelectedBlock([](TextBlock &b) {
                b.strokeEnabled = !b.strokeEnabled;
            });
        }
        if (auto s = ws.lock()) {
            const TextBlock *b = s->selectedBlock();
            if (strokeToggleBtn_)
                strokeToggleBtn_->setBackgroundColor(
                    (b && b->strokeEnabled) ? kStyleActiveBg : kStyleInactiveBg);
        }
        if (auto c = wc.lock()) c->redraw(); });

        strokeWidthSlider_ = Slider(0.5, 12.0, 0.25);
        strokeWidthSlider_->value = 1.5;
        strokeWidthSlider_->setOnValueChanged([ws, wc](double val)
                                              {
    if (auto s = ws.lock()) {
        s->applyToSelectedBlock([val](TextBlock &b) {
            b.strokeWidth = float(val);
        });
    }
    if (auto c = wc.lock()) c->redraw(); });

        // ── Stroke color picker ───────────────────────────────────
        strokeColorPicker_ = ColorPicker(Color::fromRGB(0, 0, 0));
        strokeColorPicker_->pickerSize = 90;
        strokeColorPicker_->hueBarHeight = 10;
        strokeColorPicker_->alphaBarHeight = 10;
        strokeColorPicker_->barSpacing = 4;
        strokeColorPicker_->previewSize = 16;
        strokeColorPicker_->hexInputHeight = 18;
        strokeColorPicker_->paddingLeft = 4;
        strokeColorPicker_->paddingRight = 4;
        strokeColorPicker_->paddingTop = 3;
        strokeColorPicker_->paddingBottom = 3;
        strokeColorPicker_->showAlpha = false;
        strokeColorPicker_->width = strokeColorPicker_->pickerSize + strokeColorPicker_->paddingLeft + strokeColorPicker_->paddingRight;
        strokeColorPicker_->setOnColorChanged([ws, wc](Color c)
                                              {
    if (auto s = ws.lock()) {
        s->applyToSelectedBlock([c](TextBlock &b) {
            b.strokeColor = c;
        });
    }
    if (auto cv = wc.lock()) cv->redraw(); });

        // ── Shadow toggle ─────────────────────────────────────────
        shadowToggleBtn_ = Button("Shadow")
                               ->setHeight(28)

                               ->setBackgroundColor(kStyleInactiveBg)
                               ->setOnClick([this, ws, wc]()
                                            {
        if (auto s = ws.lock()) {
            s->applyToSelectedBlock([](TextBlock &b) {
                b.shadowEnabled = !b.shadowEnabled;
            });
        }
        if (auto s = ws.lock()) {
            const TextBlock *b = s->selectedBlock();
            if (shadowToggleBtn_)
                shadowToggleBtn_->setBackgroundColor(
                    (b && b->shadowEnabled) ? kStyleActiveBg : kStyleInactiveBg);
        }
        if (auto c = wc.lock()) c->redraw(); });

        // ── Shadow X offset slider ────────────────────────────────
        shadowXSlider_ = Slider(-20.0, 20.0, 0.5);
        shadowXSlider_->value = 2.0;
        shadowXSlider_->setOnValueChanged([ws, wc](double val)
                                          {
    if (auto s = ws.lock()) {
        s->applyToSelectedBlock([val](TextBlock &b) {
            b.shadowOffsetX = float(val);
        });
    }
    if (auto c = wc.lock()) c->redraw(); });

        // ── Shadow Y offset slider ────────────────────────────────
        shadowYSlider_ = Slider(-20.0, 20.0, 0.5);
        shadowYSlider_->value = 2.0;
        shadowYSlider_->setOnValueChanged([ws, wc](double val)
                                          {
    if (auto s = ws.lock()) {
        s->applyToSelectedBlock([val](TextBlock &b) {
            b.shadowOffsetY = float(val);
        });
    }
    if (auto c = wc.lock()) c->redraw(); });

        auto rightSidebar = Container(
                                ListView({

                                    // FONT
                                    sideLabel("FONT FAMILY"),
                                    Padding(EdgeInsets::all(6),fontFamilyDropdown_),

                                    sideLabel("SIZE"),
                                    Row({
                                            Container(fontSizeDropdown_)->setWidth(80),
                                            SizedBox(4, 0),
                                            fontSizeInput_,

                                        })
                                        ->setPadding(6),
                                    sideLabel("STYLE"),
                                    Row({boldBtn_, SizedBox(2, 0), italicBtn_, SizedBox(2, 0),
                                         underlineBtn_, SizedBox(2, 0), strikethroughBtn_})
                                        ->setPadding(6),

                                    Row({allCapsBtn_, SizedBox(2, 0), smallCapsBtn_,
                                         SizedBox(2, 0), kerningBtn_})
                                        ->setPadding(6),

                                    sideLabel("ALIGNMENT"),
                                    Row({alignLeftBtn_,
                                         SizedBox(2, 0), alignCenterBtn_,
                                         SizedBox(2, 0), alignRightBtn_,
                                         SizedBox(2, 0), alignJustifyBtn_})
                                        ->setPadding(6),

                                    SizedBox(0, 4),

                                    sideLabel("BASELINE SHIFT"),
                                    Padding(EdgeInsets::all(6),baselineShiftSlider_),
                                    Row({superscriptBtn_,
                                         SizedBox(3, 0), subscriptBtn_,
                                         SizedBox(3, 0), baselineResetBtn_})
                                        ->setPadding(6),

                                    // COLOR
                                    sideLabel("COLOR"),
                                    colorPicker_,
                                    swatchRow,

                                    SizedBox(0, 4),
                                    sideLabel("LINE HEIGHT"),
                                    Padding(EdgeInsets::all(6),lineHeightSlider_),
                                    SizedBox(0, 4),
                                    sideLabel("LETTER SPACING"),
                                    Padding(EdgeInsets::all(6),letterSpacingSlider_),

                                    sideLabel("OPACITY"),
                                    Padding(EdgeInsets::all(6),opacitySlider_),

                                    sideLabel("BACKGROUND"),
                                    Row({bgToggleBtn_, SizedBox(4, 0), bgColorPicker_})->setPadding(6),

                                    sideLabel("STROKE"),
                                    Row({strokeToggleBtn_, SizedBox(4, 0), strokeWidthSlider_})->setPadding(6),
                                    strokeColorPicker_,

                                    sideLabel("SHADOW"),
                                    Row({shadowToggleBtn_})->setPadding(6),
                                    Row({Text("X")->setFontSize(9), shadowXSlider_})->setPadding(6),
                                    Row({Text("Y")->setFontSize(9), shadowYSlider_})->setPadding(6),
                                }))
                                ->setWidth(280)
                                ->setBackgroundColor(Color::fromRGB(28, 28, 30));

        // ── Toolbar ───────────────────────────────────────────
        auto toolbar = Container(
                           Row({
                                   Text("TextCanvas")
                                       ->setFontSize(13)
                                       ->setTextColor(Color::fromRGB(220, 220, 220)),
                                   SizedBox(12, 0),
                                   Text(toolLabel_, [](const std::string &s)
                                        { return "Tool: " + s; })
                                       ->setFontSize(11)
                                       ->setTextColor(Color::fromRGB(140, 140, 160)),
                                   SizedBox(8, 0),
                                   Text(blockCountLabel_, [](const std::string &s)
                                        { return s; })
                                       ->setFontSize(11)
                                       ->setTextColor(Color::fromRGB(140, 140, 160)),
                                   SizedBox(8, 0),
                                   Text(zoomLabel_, [](const std::string &s)
                                        { return "Zoom: " + s; })
                                       ->setFontSize(11)
                                       ->setTextColor(Color::fromRGB(140, 140, 160)),

                                   Row({
                                           undoBtn,
                                           SizedBox(4, 0),
                                           redoBtn,
                                           SizedBox(8, 0),
                                           commitBtn,
                                           SizedBox(4, 0),
                                           deleteBtn,
                                           SizedBox(4, 0),
                                           clearBtn,
                                       })
                                       ->setSpacing(0)
                                       ->setPadding(6)
                                       ->setCrossAxisAlignment(CrossAxisAlignment::Center),

                               })
                               ->setPadding(10)
                               ->setSpacing(0)
                               ->setCrossAxisAlignment(CrossAxisAlignment::Center))
                           ->setHeight(42)
                           ->setBackgroundColor(Color::fromRGB(24, 24, 26));

        // ── Status bar ────────────────────────────────────────
        auto statusBar = Container(
                             Row({
                                     Text(posLabel_, [](const std::string &s)
                                          { return "x,y  " + s; })
                                         ->setFontSize(10)
                                         ->setTextColor(Color::fromRGB(150, 150, 160))
                                         ->setMinWidth(110),
                                     SizedBox(16, 0),
                                     Text("Enter = commit  •  Esc = cancel  •  Shift+ -> = select  •  Ctrl+scroll = zoom")
                                         ->setFontSize(10)
                                         ->setTextColor(Color::fromRGB(110, 110, 120)),
                                 })
                                 ->setPadding(4)
                                 ->setSpacing(0)
                                 ->setCrossAxisAlignment(CrossAxisAlignment::Center))
                             ->setHeight(24)
                             ->setBackgroundColor(Color::fromRGB(20, 20, 22));

        // ── Root ─────────────────────────────────────────────
        return Scaffold(
            nullptr,
            Expanded(Column({
                toolbar,
                Expanded(Row({sidebar,
                              Expanded(canvas_),
                              rightSidebar})),
                statusBar,
            })),
            nullptr, nullptr);
    }
};

// ============================================================
//  Entry point
// ============================================================

WidgetPtr createApp(FluxUI *app)
{
    return FluxApp(
        "TextCanvas",
        std::make_shared<TextApp>(),
        AppTheme::dark(),
        false, // debugShowWidgetBounds
        1180,  // window width (wider to accommodate bigger sidebar)
        720,   // window height
        false, // maximize
        true); // fullscreen
}