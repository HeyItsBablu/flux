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

struct TextBlock
{
    float x = 0, y = 0; // baseline-left in canvas coords
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
};

// ============================================================
//  Tool enum
// ============================================================

enum class CanvasTool
{
    Select,
    Text
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

    // Callbacks
    std::function<void()> onCommitted;
    std::function<void(float, float)> onMousePos;
    std::function<void()> onSelectionChanged; // block selection changed

    bool hasEditContent() const { return !editBuf_.empty(); }
    void cancelEdit()
    {
        editing_ = false;
        editingExisting_ = false;
        editBuf_.clear();
        selStart_ = selEnd_ = 0;
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
        cancelEdit();
    }
    void redo()
    {
        if (redoStack_.empty())
            return;
        undoStack_.push_back(blocks_);
        blocks_ = redoStack_.back();
        redoStack_.pop_back();
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
            // Place cursor at click position
            selStart_ = selEnd_ = cursorIndexAt(x, b);
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
        if (activeTool_ == CanvasTool::Select)
        {
            if (e.virtualKey == Key::Delete || e.virtualKey == Key::Backspace)
                deleteSelected();
            return;
        }

        if (!editing_)
            return;

        bool shift = e.shift;

        if (e.codepoint >= 32 && e.codepoint != 127)
        {
            deleteSelection();
            int ins = std::min(selStart_, selEnd_);
            editBuf_.insert(editBuf_.begin() + ins, char(e.codepoint));
            selStart_ = selEnd_ = ins + 1;
            resetBlink();
            return;
        }

        switch (e.virtualKey)
        {
        case Key::Backspace:
            if (hasSelection())
            {
                deleteSelection();
            }
            else if (!editBuf_.empty() && selStart_ > 0)
            {
                editBuf_.erase(editBuf_.begin() + selStart_ - 1);
                selStart_ = selEnd_ = selStart_ - 1;
            }
            resetBlink();
            break;

        case Key::Delete:
            if (hasSelection())
            {
                deleteSelection();
            }
            else if (selStart_ < int(editBuf_.size()))
            {
                editBuf_.erase(editBuf_.begin() + selStart_);
            }
            resetBlink();
            break;

        case Key::Left:
            if (!shift && hasSelection())
            {
                selStart_ = selEnd_ = std::min(selStart_, selEnd_);
            }
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
            {
                selStart_ = selEnd_ = std::max(selStart_, selEnd_);
            }
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

    void onKeyUp(const KeyEvent &) override {}

    // ── Render ────────────────────────────────────────────────
    void render(Canvas2D &ctx) override
    {
        // White page
        ctx.setFillColor(Color::fromRGB(255, 255, 255));
        ctx.fillRect(0, 0, float(w_), float(h_));

        // Subtle page shadow
        ctx.setFillColor(Color::fromRGBA(0, 0, 0, 12));
        ctx.fillRect(float(w_) - 4.f, 0, 4.f, float(h_));
        ctx.fillRect(0, float(h_) - 4.f, float(w_), 4.f);

        // ── Draw committed blocks ─────────────────────────────
        for (int i = 0; i < int(blocks_.size()); ++i)
        {
            const auto &b = blocks_[i];
            bool isActiveEdit = (editingExisting_ && editing_ && selectedIdx_ == i);
            bool isSelected = (selectedIdx_ == i && !editing_);

            if (isActiveEdit)
            {
                renderEditPreview(ctx, editX_, editY_, editBuf_,
                                  activeFontSize_, resolveFont(), activeColor_,
                                  true);
            }
            else
            {
                setFont(ctx, b.fontSize, b.fontFamily);
                ctx.setFillColor(b.color);
                ctx.setTextBaseline(TextBaseline::Bottom);
                {
                    auto lines = getVisualLines(b.text, b.wrapWidth, b.fontSize);
                    float lineH = b.fontSize * b.lineHeight;
                    for (int li = 0; li < int(lines.size()); ++li)
                        fillTextSpaced(ctx, lines[li], b.x, b.y + 2.f + li * lineH, b.letterSpacing, b.kerning);
                }

                if (isSelected)
                    drawBlockOutline(ctx, b, /*active=*/false);
            }
        }

        // ── New-block text edit ───────────────────────────────
        if (editing_ && !editingExisting_)
            renderEditPreview(ctx, editX_, editY_, editBuf_,
                              activeFontSize_, resolveFont(), activeColor_, true);

        // ── Hover highlight (Select tool) ─────────────────────
        if (activeTool_ == CanvasTool::Select && !editing_)
        {
            int hover = hitTestBlock(hoverX_, hoverY_);
            if (hover >= 0 && hover != selectedIdx_)
                drawBlockOutline(ctx, blocks_[hover], /*active=*/false, /*hover=*/true);
        }

        // ── Placeholder hint ──────────────────────────────────
        if (!editing_)
        {
            ctx.setFillColor(Color::fromRGBA(150, 150, 160, 60));
            ctx.setFont("12px sans");
            ctx.setTextBaseline(TextBaseline::Top);
            const char *hint = (activeTool_ == CanvasTool::Select)
                                   ? "Select tool: click a text block to select it"
                                   : "Text tool: click anywhere to place text";
            ctx.fillText(hint, 16.f, 14.f);
        }
    }

private:
    // ── Helpers ───────────────────────────────────────────────
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
        if (!editingExisting_)
        {
            b.x = editX_;
            b.y = editY_;
        }
    }

    bool hasSelection() const { return selStart_ != selEnd_; }

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

        // Background highlight box
        if (highlight)
        {
            float maxW = fs * 4.f;
            for (auto &l : lines)
                maxW = std::max(maxW, measureTextSpaced(ctx, l.empty() ? "M" : l, ls, kr) + fs * 1.2f);
            float totalH = nLines * lineH + 10.f;

            ctx.setFillColor(Color::fromRGBA(30, 120, 255, 15));
            ctx.fillRoundedRect(tx - 4.f, ty - fs - 6.f, maxW, totalH, 3.f);
            ctx.setFillColor(Color::fromRGBA(30, 120, 255, 50));
            ctx.fillRect(tx - 2.f, ty + 3.f + (nLines - 1) * lineH, maxW, 1.f);
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
                float selX = tx + measureTextSpaced(ctx, lines[li].substr(0, colStart), ls, kr);
                float selW = measureTextSpaced(ctx, lines[li].substr(colStart, colEnd - colStart), ls, kr);
                if (selW < 2.f)
                    selW = 2.f;
                ctx.setFillColor(Color::fromRGBA(30, 120, 255, 80));
                ctx.fillRect(selX, ty - fs - 1.f + li * lineH, selW, fs + 4.f);
            }
        }

        // Draw each line of text
        ctx.setFillColor(color);
        for (int li = 0; li < nLines; ++li)
            fillTextSpaced(ctx, lines[li], tx, ty + 2.f + li * lineH, ls,kr);

        // Blinking cursor
        if (cursorVisible_)
        {
            auto [curLine, curCol] = posToLineCol(selEnd_);
            float cursorX = tx + measureTextSpaced(ctx, lines[curLine].substr(0, curCol), ls, kr);
            float cursorY = ty - fs - 1.f + curLine * lineH;
            ctx.setFillColor(color);
            ctx.fillRect(cursorX + 1.f, cursorY, 1.5f, fs + 4.f);
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
            for (auto &l : vlines)
                tw = std::max(tw, measureTextSpaced(ctx, l, b.letterSpacing, b.kerning));

        float totalH = b.fontSize + 10.f + (nLines - 1) * lineH;

        // ── Outline rect ──────────────────────────────────────
        if (hover)
            ctx.setStrokeColor(Color::fromRGBA(30, 120, 255, 60));
        else
            ctx.setStrokeColor(Color::fromRGBA(30, 120, 255, 160));
        ctx.setLineWidth(1.f);
        ctx.strokeRoundedRect(b.x - 4.f, b.y - b.fontSize - 4.f,
                              tw + 8.f, totalH, 2.f);

        // ── Corner handles (selected only) ────────────────────
        if (!hover)
        {
            float hx[] = {b.x - 4.f, b.x + tw + 4.f};
            float hy[] = {b.y - b.fontSize - 4.f,
                          b.y - b.fontSize - 4.f + totalH};
            ctx.setFillColor(Color::fromRGBA(30, 120, 255, 200));
            for (float hxx : hx)
                for (float hyy : hy)
                    ctx.fillRoundedRect(hxx - 3.f, hyy - 3.f, 6.f, 6.f, 2.f);
        }

        // ── Wrap boundary + drag handle ───────────────────────
        float displayW = (b.wrapWidth > 0.f)
                             ? b.wrapWidth
                             : tw;

        float rx = b.x + displayW + 4.f;
        float outlineTop = b.y - b.fontSize - 4.f;

        // Dashed right-edge line
        ctx.setStrokeColor(Color::fromRGBA(30, 120, 255, 100));
        ctx.setLineWidth(1.f);
        ctx.beginPath();
        for (float dy2 = 0.f; dy2 < totalH; dy2 += 8.f)
        {
            ctx.moveTo(rx, outlineTop + dy2);
            ctx.lineTo(rx, outlineTop + std::min(dy2 + 4.f, totalH));
        }
        ctx.stroke();

        // Orange wrap handle square
        ctx.setFillColor(Color::fromRGBA(255, 160, 30, 220));
        ctx.fillRoundedRect(rx - 4.f, b.y - b.fontSize * 0.5f - 4.f, 8.f, 8.f, 2.f);
    }

    int hitTestBlock(float x, float y) const
    {
        for (int i = int(blocks_.size()) - 1; i >= 0; --i)
        {
            const auto &b = blocks_[i];
            auto lines = splitLines(b.text);
            int numLines = std::max(1, int(lines.size()));
            float approxW = b.wrapWidth > 0.f ? b.wrapWidth : approxTextWidth(b.text, b.fontSize);
            float lineH = b.fontSize * b.lineHeight;
            float boxX0 = b.x - 4.f;
            float boxY0 = b.y - b.fontSize - 4.f;
            float boxX1 = b.x + approxW + 4.f;
            float boxY1 = b.y + (numLines - 1) * lineH + 6.f; // <-- multi-line
            if (x >= boxX0 && x <= boxX1 && y >= boxY0 && y <= boxY1)
                return i;
        }
        return -1;
    }

    void pushUndo()
    {
        undoStack_.push_back(blocks_);
        if (undoStack_.size() > 50)
            undoStack_.erase(undoStack_.begin());
        redoStack_.clear();
    }

    // Returns the x position of the right-edge wrap handle for a selected block.
    // The handle is a small square on the right edge of the wrap box.
    float wrapHandleX(const TextBlock &b) const
    {
        return b.x + (b.wrapWidth > 0.f ? b.wrapWidth : approxTextWidth(b.text, b.fontSize)) + 4.f;
    }

    bool hitWrapHandle(float x, float y, const TextBlock &b) const
    {
        float hx = wrapHandleX(b);
        float hy = b.y - b.fontSize * 0.5f;
        return x >= hx - 6.f && x <= hx + 6.f &&
               y >= hy - 6.f && y <= hy + 6.f;
    }

    // ── State ─────────────────────────────────────────────────
    std::vector<TextBlock> blocks_;
    std::vector<std::vector<TextBlock>> undoStack_, redoStack_;

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

    std::shared_ptr<SliderWidget> lineHeightSlider_;
    State<double> lineHeightState_{1.3};
    std::shared_ptr<SliderWidget> letterSpacingSlider_;
    std::shared_ptr<ButtonWidget> kerningBtn_;

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

    // Font families: display label → font key used in TextBlock
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
            textToolBtn_->setBackgroundColor(!isSel ? kToolActiveBg : kToolInactiveBg);
        toolLabel_.set(isSel ? "Select" : "Text");
    }

    void updateStyleButtons()
    {
        if (!surface_)
            return;
        if (boldBtn_)
            boldBtn_->setBackgroundColor(surface_->activeBold_ ? kStyleActiveBg : kStyleInactiveBg);
        if (italicBtn_)
            italicBtn_->setBackgroundColor(surface_->activeItalic_ ? kStyleActiveBg : kStyleInactiveBg);
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

        syncKerningButton();
        syncLetterSpacingSlider();
        syncLineHeightSlider();
        syncFontFamilyDropdown();
        syncFontSizeControls();
        updateStyleButtons();
        if (colorPicker_)
            colorPicker_->setColor(b->color);
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
        { onBlockSelected(); canvas_->redraw(); };

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
                s->activeFontSize_ = fs;
                syncFontSizeControls();
            }
            if (auto c = wc.lock()) c->redraw(); });

        // fontSizeState_ is declared as a member ("18" initial value).
        // Observe it to update activeFontSize_ whenever the user types.
        fontSizeState_.listen([this, ws, wc](const std::string &t)
                              {
            try
            {
                float fs = std::stof(t);
                fs = std::clamp(fs, 6.f, 200.f);
                if (auto s = ws.lock()) s->activeFontSize_ = fs;
                if (auto c = wc.lock()) c->redraw();
            }
            catch(...) {} });

        fontSizeInput_ = TextInput(); // no placeholder — shows the live value

        fontSizeInput_->setWidth(52);
        fontSizeInput_->setInputValue(fontSizeState_);

        // ── Bold / Italic ─────────────────────────────────────
        boldBtn_ = Button("B")
                       ->setHeight(28)
                       ->setWidth(34)
                       ->setBackgroundColor(kStyleInactiveBg)
                       ->setOnClick([this, ws, wc]()
                                    {
                if (auto s = ws.lock())
                {
                    s->activeBold_ = !s->activeBold_;
                    updateStyleButtons();
                }
                if (auto c = wc.lock()) c->redraw(); });

        italicBtn_ = Button("I")
                         ->setHeight(28)
                         ->setWidth(34)
                         ->setBackgroundColor(kStyleInactiveBg)
                         ->setOnClick([this, ws, wc]()
                                      {
                if (auto s = ws.lock())
                {
                    s->activeItalic_ = !s->activeItalic_;
                    updateStyleButtons();
                }
                if (auto c = wc.lock()) c->redraw(); });

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
        colorPicker_->setOnColorChanged([ws](Color c)
                                        {
            if (auto s = ws.lock()) s->activeColor_ = c; });

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
                           ->setOnClick([this, ws, c]()
                                        {
                    if (auto s = ws.lock()) s->activeColor_ = c;
                    if (colorPicker_) colorPicker_->setColor(c); });
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

        // ── Sidebar ───────────────────────────────────────────
        auto sidebar = Container(
                           ListView({
                               // TOOLS
                               sideLabel("TOOLS"),
                               Container(
                                   Row({selectToolBtn_, SizedBox(4, 0), textToolBtn_}))
                                   ->setPadding(6)
                                   ->setHeight(50),

                               SizedBox(0, 4),

                               // FONT
                               sideLabel("FONT FAMILY"),
                               Container(fontFamilyDropdown_)->setPadding(6)->setHeight(50),

                               sideLabel("SIZE"),
                               Container(
                                   Row({
                                       Container(fontSizeDropdown_)->setWidth(80),
                                       SizedBox(4, 0),
                                       fontSizeInput_,

                                   }))
                                   ->setPadding(6)
                                   ->setHeight(50),
                               sideLabel("STYLE"),
                               Container(
                                   Row({boldBtn_, SizedBox(2, 0), italicBtn_, SizedBox(2, 0), kerningBtn_}))
                                   ->setPadding(6)
                                   ->setHeight(30),

                               SizedBox(0, 4),

                               // COLOR
                               sideLabel("COLOR"),
                               colorPicker_,
                               Container(swatchRow)->setPadding(6)->setHeight(20),

                               SizedBox(0, 4),
                               sideLabel("LINE HEIGHT"),
                               Container(lineHeightSlider_)->setPadding(6)->setHeight(55),
                               SizedBox(0, 4),
                               sideLabel("LETTER SPACING"),
                               Container(letterSpacingSlider_)->setPadding(6)->setHeight(55),
                               // ACTIONS
                               sideLabel("ACTIONS"),
                               Container(
                                   Column({
                                       Row({undoBtn, SizedBox(4, 0), redoBtn})->setSpacing(0),

                                   }))
                                   ->setPadding(6)
                                   ->setHeight(40),

                               Container(
                                   Column({

                                       commitBtn,
                                       SizedBox(0, 4),
                                       deleteBtn,
                                       SizedBox(0, 4),
                                       clearBtn,
                                   }))
                                   ->setPadding(6)
                                   ->setHeight(40),

                           }))
                           ->setWidth(220)
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
                                     Text("Enter = commit  •  Esc = cancel  •  Shift+← → = select  •  Ctrl+scroll = zoom")
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
                Expanded(Row({
                    sidebar,
                    Expanded(canvas_),
                })),
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