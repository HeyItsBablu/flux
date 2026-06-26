#ifndef FLUX_TEXTAREA_HPP
#define FLUX_TEXTAREA_HPP

#include "../flux_core.hpp"
#include "../flux_state.hpp"
#include "flux_keyboard.hpp"

#include <algorithm>
#include <string>
#include <vector>

// ============================================================================
// TextAreaWidget
//
// A multi-line text input that mirrors HTML <textarea> behaviour:
//   - Wraps text at the widget boundary (soft-wrap on by default)
//   - Vertical scrolling when content overflows
//   - Click-to-place cursor, arrow-key navigation across lines
//   - Home / End (line), Ctrl+Home / Ctrl+End (document)
//   - Backspace / Delete
//   - Enter inserts a real newline (\n)
//   - Tab inserts a configurable number of spaces (default 4)
//   - Mouse-wheel scrolling
//   - Drag-to-resize handle in the bottom-right corner (like HTML resize)
//   - Blinking cursor on focus
//   - Placeholder text when empty
//   - State binding via setInputValue(State<std::string>&)
//   - setRows(n)  / setCols(n)  for initial sizing (like HTML rows/cols)
//   - setMaxLength(n) to cap input
//   - setReadOnly(true) disables editing
//   - setResize(false) disables the resize handle
// ============================================================================

class TextAreaWidget : public Widget
{
public:
    // ── Content ──────────────────────────────────────────────────────────────
    std::string inputValue;
    std::string placeholder;
    int         maxLength  = -1;    // -1 = unlimited
    bool        readOnly   = false;
    int         tabSize    = 4;     // spaces per Tab key press

    // ── Sizing ────────────────────────────────────────────────────────────────
    int rows = 4;   // initial height in text-lines
    int cols = 40;  // initial width  in characters (approximate)

    // ── Resize handle ─────────────────────────────────────────────────────────
    bool resizable       = true;
    int  resizeHandleSize = 14;
    bool isDraggingResize = false;
    int  resizeDragStartX = 0, resizeDragStartY = 0;
    int  resizeDragStartW = 0, resizeDragStartH = 0;

    // ── Visual ────────────────────────────────────────────────────────────────
    Color focusedBorderColor   = Color::fromRGB(33, 150, 243);
    Color unfocusedBorderColor = Color::fromRGB(180, 180, 180);
    Color placeholderColor     = Color::fromRGB(180, 180, 180);
    Color inputTextColor       = Color::fromRGB(30, 30, 30);
    Color selectionColor       = Color::fromRGB(179, 215, 255);  // light blue
    Color resizeHandleColor    = Color::fromRGB(180, 180, 180);
    Color scrollbarTrackColor  = Color::fromRGB(240, 240, 240);
    Color scrollbarThumbColor  = Color::fromRGB(180, 180, 180);
    Color scrollbarThumbHover  = Color::fromRGB(140, 140, 140);

    // ── Cursor ────────────────────────────────────────────────────────────────
    int     cursorPos     = 0;   // byte offset into inputValue
    bool    cursorVisible = true;
    TimerID cursorTimerId = 0;

    // ── Scroll ────────────────────────────────────────────────────────────────
    int scrollY      = 0;  // pixel scroll offset (vertical only)
    int contentHeight= 0;  // measured height of all text lines (px)

    // ── Scrollbar drag ────────────────────────────────────────────────────────
    bool isDraggingScrollbar = false;
    int  scrollDragStartY    = 0;
    int  scrollDragStartScrollY = 0;

    bool isScrollbarHovered  = false;

    // ── Callbacks ─────────────────────────────────────────────────────────────
    std::function<void(const std::string&)> onValueChanged;
    std::function<void()>                   onEnter;   // Ctrl+Enter or Enter (configurable)

    // ──────────────────────────────────────────────────────────────────────────

    TextAreaWidget()
    {
        isFocusable     = true;
        hasBorder       = true;
        hasBackground   = true;
        backgroundColor = Color::fromRGB(255, 255, 255);
        borderColor     = unfocusedBorderColor;
        borderWidth     = 1;
        borderRadius    = 4;
        paddingLeft = paddingRight = 10;
        paddingTop  = paddingBottom = 8;

        // Will be sized properly in computeLayout once we have font metrics
        autoWidth  = true;
        autoHeight = false;
    }

    bool isTextInput() const override { return true; }

    // ──────────────────────────────────────────────────────────────────────────
    // computeLayout
    // ──────────────────────────────────────────────────────────────────────────

    void computeLayout(GraphicsContext& ctx,
                       const BoxConstraints& constraints,
                       FontCache& fontCache) override
    {
        // Measure a single character for approximate col-width
        NativeFont font = fontCache.getFont(fontFamily, fontSize, fontWeight);
        int charW = 8, lineH = 0;
        {
            std::wstring sample = L"M";
            Painter(ctx).measureText(sample, font, charW, lineH);
        }
        if (lineH == 0) lineH = fontSize + 4;
        lineHeight_ = lineH;

        if (autoWidth)
            width = constraints.clampWidth(
                cols * charW + paddingLeft + paddingRight + scrollbarWidth_);
        else
            width = constraints.clampWidth(width);

        height = constraints.clampHeight(
            rows * lineH + paddingTop + paddingBottom);

        applyConstraints();
        needsLayout = false;
    }

    // ──────────────────────────────────────────────────────────────────────────
    // render
    // ──────────────────────────────────────────────────────────────────────────

    void render(GraphicsContext& ctx, FontCache& fontCache) override
    {
        borderColor = isFocused ? focusedBorderColor : unfocusedBorderColor;
        drawRoundedRectangle(ctx);

        Painter painter(ctx);
        NativeFont font = fontCache.getFont(fontFamily, fontSize, fontWeight);

        const int innerX = x + paddingLeft + gutterWidth_;
        const int innerY = y + paddingTop;
        const int innerW = width  - paddingLeft - paddingRight - scrollbarWidth_ - gutterWidth_;
        const int innerH = height - paddingTop  - paddingBottom;

        // ── Line-number gutter ──────────────────────────────────────────────
        if (showLineNumbers_ && gutterWidth_ > 0)
        {
            painter.fillRect(x + paddingLeft, innerY, gutterWidth_, innerH, gutterBg_);

            // Count hard lines (real \n-separated) for numbering
            NativeFont gutterFont = fontCache.getFont(fontFamily, fontSize - 1, FontWeight::Normal);
            int lineNum = 1;
            int gy      = innerY - scrollY;
            bool newLineNext = true;
            for (int ci = 0; ci <= (int)inputValue.size(); ++ci)
            {
                bool isNewLine = (ci == 0) ||
                                 (ci > 0 && inputValue[ci - 1] == '\n');
                if (!isNewLine) continue;
                if (gy + lineHeight_ >= innerY && gy < innerY + innerH)
                {
                    std::wstring numStr = toWideString(std::to_string(lineNum));
                    painter.drawText(numStr,
                                     x + paddingLeft,
                                     gy,
                                     gutterWidth_ - 6,
                                     lineHeight_,
                                     gutterFont,
                                     gutterText_,
                                     DT_RIGHT | DT_TOP | DT_SINGLELINE | DT_NOCLIP);
                }
                gy += lineHeight_;
                lineNum++;
                if (gy > innerY + innerH) break;
            }
        }

        if (lineHeight_ == 0)
        {
            int dummy;
            painter.measureText(L"M", font, lineHeight_, dummy);
            if (lineHeight_ == 0) lineHeight_ = fontSize + 4;
        }

        // ── Clip text area ──────────────────────────────────────────────────
        painter.pushClipRect(innerX, innerY, innerW, innerH);

        if (inputValue.empty() && !placeholder.empty())
        {
            // Placeholder
            std::wstring wph = toWideString(placeholder);
            painter.drawText(wph, innerX, innerY, innerW, innerH, font,
                             placeholderColor,
                             DT_LEFT | DT_TOP | DT_WORDBREAK);
        }
        else
        {
            // Build wrapped lines
            auto lines = buildLines(painter, font, innerW);
            contentHeight = (int)lines.size() * lineHeight_;

            int ly = innerY - scrollY;
            for (int i = 0; i < (int)lines.size(); ++i, ly += lineHeight_)
            {
                if (ly + lineHeight_ < innerY) continue;
                if (ly > innerY + innerH)      break;

                const auto& lineStr = lines[i];
                std::wstring wline  = toWideString(lineStr);

                // Draw text
                painter.drawText(wline, innerX, ly, innerW, lineHeight_,
                                 font, inputTextColor,
                                 DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOCLIP);
            }

            // ── Cursor ──────────────────────────────────────────────────────
            if (isFocused && cursorVisible)
            {
                auto [curLine, curCol] = cursorLineCol(lines);
                int cursorX = innerX + measureStringWidth(
                    painter, font,
                    lines[curLine].substr(0, curCol));
                int cursorY = innerY + curLine * lineHeight_ - scrollY;

                if (cursorY >= innerY - lineHeight_ &&
                    cursorY < innerY + innerH)
                {
                    painter.drawLine(cursorX, cursorY + 2,
                                     cursorX, cursorY + lineHeight_ - 2,
                                     inputTextColor, 1);
                }
            }
        }

        painter.popClipRect();

        // ── Scrollbar ────────────────────────────────────────────────────────
        drawScrollbar(painter, innerY, innerH);

        // ── Resize handle ────────────────────────────────────────────────────
        if (resizable)
        {
            int hx = x + width  - resizeHandleSize;
            int hy = y + height - resizeHandleSize;
            // Three diagonal lines (classic resize grip)
            for (int off = 3; off <= resizeHandleSize - 2; off += 4)
            {
                painter.drawLine(hx + resizeHandleSize - off, hy + resizeHandleSize,
                                 hx + resizeHandleSize,       hy + off,
                                 resizeHandleColor, 1);
            }
        }

        needsPaint = false;
    }

    // ──────────────────────────────────────────────────────────────────────────
    // Focus
    // ──────────────────────────────────────────────────────────────────────────

    bool handleFocus(bool focused) override
    {
        isFocused = focused;
        auto* ui  = FluxUI::getCurrentInstance();

        if (focused)
        {
            cursorVisible = true;
            cursorTimerId = ui->setInterval(530, [this]()
            {
                cursorVisible = !cursorVisible;
                markNeedsPaint();
            });
            VirtualKeyboard::notifyFocusGained(this);
        }
        else
        {
            if (cursorTimerId)
            {
                ui->clearInterval(cursorTimerId);
                cursorTimerId = 0;
            }
            cursorVisible = false;
            VirtualKeyboard::notifyFocusLost();
        }

        markNeedsPaint();
        return true;
    }

    // ──────────────────────────────────────────────────────────────────────────
    // Mouse
    // ──────────────────────────────────────────────────────────────────────────

    bool handleMouseDown(int mx, int my) override
    {
        if (!hitTest(mx, my)) return false;

        // Resize handle?
        if (resizable && isInResizeHandle(mx, my))
        {
            isDraggingResize  = true;
            resizeDragStartX  = mx;
            resizeDragStartY  = my;
            resizeDragStartW  = width;
            resizeDragStartH  = height;
            FluxUI::getCurrentInstance()->captureMouseInput();
            return true;
        }

        // Scrollbar thumb?
        if (isInScrollbarThumb(mx, my))
        {
            isDraggingScrollbar    = true;
            scrollDragStartY       = my;
            scrollDragStartScrollY = scrollY;
            FluxUI::getCurrentInstance()->captureMouseInput();
            return true;
        }

        // Place cursor from click position
        if (!inputValue.empty())
        {
            auto* ui  = FluxUI::getCurrentInstance();
            auto  mc  = ui->getMeasureContext();
            NativeFont font = ui->getFontCache().getFont(fontFamily, fontSize, fontWeight);
            Painter painter(mc.ctx);

            const int innerX = x + paddingLeft;
            const int innerY = y + paddingTop;
            const int innerW = width - paddingLeft - paddingRight - scrollbarWidth_ - gutterWidth_;

            auto lines = buildLines(painter, font, innerW);

            int clickY  = my - innerY + scrollY;
            int lineIdx = std::max(0, std::min(
                (int)lines.size() - 1, clickY / lineHeight_));

            const std::string& lineStr = lines[lineIdx];
            int clickX = mx - innerX;
            int col    = getColFromPixelX(painter, font, lineStr, clickX);

            cursorPos = lineColToByteOffset(lines, lineIdx, col);
        }
        else
        {
            cursorPos = 0;
        }

        cursorVisible = true;
        markNeedsPaint();
        return true;
    }

    bool handleMouseUp(int /*mx*/, int /*my*/) override
    {
        if (isDraggingResize || isDraggingScrollbar)
        {
            isDraggingResize    = false;
            isDraggingScrollbar = false;
            FluxUI::getCurrentInstance()->releaseMouseInput();
            return true;
        }
        return false;
    }

    bool handleMouseMove(int mx, int my) override
    {
        if (isDraggingResize)
        {
            int newW = resizeDragStartW + (mx - resizeDragStartX);
            int newH = resizeDragStartH + (my - resizeDragStartY);
            width    = std::max(minWidth  > 0 ? minWidth  : 80, newW);
            height   = std::max(minHeight > 0 ? minHeight : 40, newH);
            autoWidth  = false;
            autoHeight = false;
            markNeedsLayout();
            return true;
        }

        if (isDraggingScrollbar)
        {
            int dy         = my - scrollDragStartY;
            int viewH      = height - paddingTop - paddingBottom;
            int maxScroll  = std::max(0, contentHeight - viewH);
            if (contentHeight > viewH)
            {
                float ratio = (float)dy / (viewH - thumbHeight(viewH));
                scrollY = std::clamp(
                    (int)(scrollDragStartScrollY + ratio * maxScroll),
                    0, maxScroll);
            }
            markNeedsPaint();
            return true;
        }

        // Scrollbar hover
        bool nowHovered = isInScrollbar(mx, my);
        if (nowHovered != isScrollbarHovered)
        {
            isScrollbarHovered = nowHovered;
            markNeedsPaint();
        }
        return false;
    }

    bool handleMouseWheel(int delta) override
    {
        if (!hitTest(mouseX_, mouseY_) && !isFocused) return false;
        int viewH     = height - paddingTop - paddingBottom;
        int maxScroll = std::max(0, contentHeight - viewH);
        scrollY = std::clamp(scrollY - delta * lineHeight_, 0, maxScroll);
        markNeedsPaint();
        return true;
    }

    // ──────────────────────────────────────────────────────────────────────────
    // Keyboard
    // ──────────────────────────────────────────────────────────────────────────

    bool handleChar(wchar_t ch) override
    {
        if (readOnly)       return false;
        if (ch < 32)        return false;   // control chars handled in handleKeyDown
        if (ch == 127)      return false;   // DEL
        if (maxLength >= 0 && (int)inputValue.size() >= maxLength) return false;

        std::string ins(1, (char)ch);  // ASCII path; for full Unicode extend here
        inputValue.insert(cursorPos, ins);
        cursorPos += (int)ins.size();
        cursorVisible = true;
        clampScroll();
        notifyStateBinding();
        markNeedsPaint();
        return true;
    }

    bool handleKeyDown(int keyCode) override
    {
        auto* ui   = FluxUI::getCurrentInstance();
        auto  mc   = ui->getMeasureContext();
        NativeFont font = ui->getFontCache().getFont(fontFamily, fontSize, fontWeight);
        Painter painter(mc.ctx);

        const int innerW = width - paddingLeft - paddingRight - scrollbarWidth_ - gutterWidth_;
        auto lines = buildLines(painter, font, innerW);
        auto [curLine, curCol] = cursorLineCol(lines);

        switch (keyCode)
        {
        // ── Enter / newline ───────────────────────────────────────────────────
        case Key::Return:
            if (readOnly) return false;
            if (maxLength >= 0 && (int)inputValue.size() >= maxLength) return false;
            inputValue.insert(cursorPos, "\n");
            cursorPos++;
            cursorVisible = true;
            clampScroll();
            notifyStateBinding();
            markNeedsPaint();
            return true;

        // ── Tab ───────────────────────────────────────────────────────────────
        case Key::Tab:
        {
            if (readOnly) return false;
            std::string spaces(tabSize, ' ');
            if (maxLength >= 0 &&
                (int)(inputValue.size() + spaces.size()) > maxLength)
                return false;
            inputValue.insert(cursorPos, spaces);
            cursorPos += (int)spaces.size();
            cursorVisible = true;
            clampScroll();
            notifyStateBinding();
            markNeedsPaint();
            return true;
        }

        // ── Backspace ─────────────────────────────────────────────────────────
        case Key::Backspace:
            if (readOnly || cursorPos == 0) return false;
            inputValue.erase(cursorPos - 1, 1);
            cursorPos--;
            cursorVisible = true;
            clampScroll();
            notifyStateBinding();
            markNeedsPaint();
            return true;

        // ── Delete ────────────────────────────────────────────────────────────
        case Key::Delete:
            if (readOnly || cursorPos >= (int)inputValue.size()) return false;
            inputValue.erase(cursorPos, 1);
            cursorVisible = true;
            clampScroll();
            notifyStateBinding();
            markNeedsPaint();
            return true;

        // ── Arrow Left ────────────────────────────────────────────────────────
        case Key::Left:
            if (cursorPos > 0) cursorPos--;
            cursorVisible = true;
            scrollToCursor(lines);
            markNeedsPaint();
            return true;

        // ── Arrow Right ───────────────────────────────────────────────────────
        case Key::Right:
            if (cursorPos < (int)inputValue.size()) cursorPos++;
            cursorVisible = true;
            scrollToCursor(lines);
            markNeedsPaint();
            return true;

        // ── Arrow Up ──────────────────────────────────────────────────────────
        case Key::Up:
        {
            if (curLine == 0)
            {
                cursorPos = 0;
            }
            else
            {
                int targetLine = curLine - 1;
                int col = std::min(curCol, (int)lines[targetLine].size());
                cursorPos = lineColToByteOffset(lines, targetLine, col);
            }
            cursorVisible = true;
            scrollToCursor(lines);
            markNeedsPaint();
            return true;
        }

        // ── Arrow Down ────────────────────────────────────────────────────────
        case Key::Down:
        {
            if (curLine >= (int)lines.size() - 1)
            {
                cursorPos = (int)inputValue.size();
            }
            else
            {
                int targetLine = curLine + 1;
                int col = std::min(curCol, (int)lines[targetLine].size());
                cursorPos = lineColToByteOffset(lines, targetLine, col);
            }
            cursorVisible = true;
            scrollToCursor(lines);
            markNeedsPaint();
            return true;
        }

        // ── Home ──────────────────────────────────────────────────────────────
        case Key::Home:
            cursorPos     = lineColToByteOffset(lines, curLine, 0);
            cursorVisible = true;
            scrollToCursor(lines);
            markNeedsPaint();
            return true;

        // ── End ───────────────────────────────────────────────────────────────
        case Key::End:
            cursorPos     = lineColToByteOffset(lines, curLine,
                                                (int)lines[curLine].size());
            cursorVisible = true;
            scrollToCursor(lines);
            markNeedsPaint();
            return true;

        // ── Page Up / Page Down ───────────────────────────────────────────────
        case Key::PageUp:
        {
            int viewH = height - paddingTop - paddingBottom;
            scrollY = std::max(0, scrollY - viewH);
            markNeedsPaint();
            return true;
        }
        case Key::PageDown:
        {
            int viewH     = height - paddingTop - paddingBottom;
            int maxScroll = std::max(0, contentHeight - viewH);
            scrollY = std::min(maxScroll, scrollY + viewH);
            markNeedsPaint();
            return true;
        }

        default:
            return false;
        }
    }

    // ──────────────────────────────────────────────────────────────────────────
    // Public builder API
    // ──────────────────────────────────────────────────────────────────────────

    std::shared_ptr<TextAreaWidget> setInputValue(State<std::string>& state)
    {
        inputValue = state.get();
        cursorPos  = (int)inputValue.size();
        scrollY    = 0;
        state.bindProperty(
            shared_from_this(),
            [](Widget* w, const std::string& val)
            {
                auto* ta   = static_cast<TextAreaWidget*>(w);
                ta->inputValue = val;
                ta->cursorPos  = (int)val.size();
            },
            false);
        boundStringState_ = &state;
        return std::static_pointer_cast<TextAreaWidget>(shared_from_this());
    }

    std::shared_ptr<TextAreaWidget> setPlaceholder(const std::string& ph)
    {
        placeholder = ph;
        return std::static_pointer_cast<TextAreaWidget>(shared_from_this());
    }

    std::shared_ptr<TextAreaWidget> setRows(int r)
    {
        rows = std::max(1, r);
        return std::static_pointer_cast<TextAreaWidget>(shared_from_this());
    }

    std::shared_ptr<TextAreaWidget> setCols(int c)
    {
        cols = std::max(1, c);
        return std::static_pointer_cast<TextAreaWidget>(shared_from_this());
    }

    std::shared_ptr<TextAreaWidget> setWidth(int w)
    {
        width     = w;
        autoWidth = false;
        return std::static_pointer_cast<TextAreaWidget>(shared_from_this());
    }

    std::shared_ptr<TextAreaWidget> setHeight(int h)
    {
        height     = h;
        autoHeight = false;
        return std::static_pointer_cast<TextAreaWidget>(shared_from_this());
    }

    std::shared_ptr<TextAreaWidget> setMaxLength(int n)
    {
        maxLength = n;
        return std::static_pointer_cast<TextAreaWidget>(shared_from_this());
    }

    std::shared_ptr<TextAreaWidget> setReadOnly(bool ro)
    {
        readOnly = ro;
        return std::static_pointer_cast<TextAreaWidget>(shared_from_this());
    }

    std::shared_ptr<TextAreaWidget> setResize(bool r)
    {
        resizable = r;
        return std::static_pointer_cast<TextAreaWidget>(shared_from_this());
    }

    std::shared_ptr<TextAreaWidget> setTabSize(int t)
    {
        tabSize = std::max(1, t);
        return std::static_pointer_cast<TextAreaWidget>(shared_from_this());
    }

    std::shared_ptr<TextAreaWidget>
    setOnValueChanged(std::function<void(const std::string&)> cb)
    {
        onValueChanged = std::move(cb);
        return std::static_pointer_cast<TextAreaWidget>(shared_from_this());
    }

    std::shared_ptr<TextAreaWidget> setFocusedBorderColor(Color c)
    {
        focusedBorderColor = c;
        return std::static_pointer_cast<TextAreaWidget>(shared_from_this());
    }

    std::shared_ptr<TextAreaWidget> setTextColor(Color c)
    {
        inputTextColor = c;
        return std::static_pointer_cast<TextAreaWidget>(shared_from_this());
    }

    // Toggle a line-number gutter (useful for code-editor style usage).
    std::shared_ptr<TextAreaWidget> setLineNumbers(bool show)
    {
        showLineNumbers_ = show;
        gutterWidth_     = show ? 48 : 0;
        markNeedsLayout();
        return std::static_pointer_cast<TextAreaWidget>(shared_from_this());
    }

    std::shared_ptr<TextAreaWidget> setGutterColor(Color bg, Color text)
    {
        gutterBg_   = bg;
        gutterText_ = text;
        markNeedsPaint();
        return std::static_pointer_cast<TextAreaWidget>(shared_from_this());
    }

// ──────────────────────────────────────────────────────────────────────────────
private:
// ──────────────────────────────────────────────────────────────────────────────

    State<std::string>* boundStringState_ = nullptr;
    int lineHeight_     = 0;
    int scrollbarWidth_ = 12;   // px reserved for scrollbar

    // Line-number gutter
    bool  showLineNumbers_ = false;
    int   gutterWidth_     = 0;
    Color gutterBg_        = Color::fromRGB(245, 245, 245);
    Color gutterText_      = Color::fromRGB(150, 150, 150);

    // Tiny stash for mousewheel hit-testing without capturing every move
    int mouseX_ = 0, mouseY_ = 0;

    // ── buildLines ─────────────────────────────────────────────────────────────
    // Splits inputValue into display lines respecting \n and soft-wrapping
    // within innerW pixels.

    std::vector<std::string> buildLines(Painter& painter,
                                        NativeFont font, int innerW) const
    {
        std::vector<std::string> result;
        if (inputValue.empty()) { result.push_back(""); return result; }

        // Split on hard newlines first
        std::vector<std::string> hardLines;
        {
            std::string cur;
            for (char c : inputValue)
            {
                if (c == '\n') { hardLines.push_back(cur); cur.clear(); }
                else           cur += c;
            }
            hardLines.push_back(cur);
        }

        // Soft-wrap each hard line
        for (const auto& hard : hardLines)
        {
            if (hard.empty()) { result.push_back(""); continue; }

            int start = 0;
            while (start < (int)hard.size())
            {
                // Binary-search for how many chars fit
                int lo = 1, hi = (int)hard.size() - start, fit = 1;
                while (lo <= hi)
                {
                    int mid = (lo + hi) / 2;
                    int tw = measureStringWidth(
                        painter, font, hard.substr(start, mid));
                    if (tw <= innerW) { fit = mid; lo = mid + 1; }
                    else              { hi  = mid - 1; }
                }

                // Try to break at last space
                int breakAt = fit;
                if (start + fit < (int)hard.size())
                {
                    int wb = fit;
                    while (wb > 1 && hard[start + wb - 1] != ' ') --wb;
                    if (wb > 1) breakAt = wb;
                }

                result.push_back(hard.substr(start, breakAt));
                start += breakAt;
            }
        }

        return result;
    }

    // ── cursorLineCol ──────────────────────────────────────────────────────────
    // Given the display lines, return (lineIndex, colIndex) of cursorPos.

    std::pair<int,int> cursorLineCol(
        const std::vector<std::string>& lines) const
    {
        int remaining = cursorPos;
        int pos       = 0;

        for (int i = 0; i < (int)lines.size(); ++i)
        {
            int lineLen = (int)lines[i].size();

            // Account for the implicit newline that joins hard lines
            bool lastLine = (i == (int)lines.size() - 1);

            // Determine if this display line ends with a real \n in the source
            // The source byte at pos+lineLen (if not last) is either '\n' (hard wrap)
            // or NOT '\n' (soft wrap). If soft wrap, the cursor doesn't consume
            // the extra character — no +1 for the newline.
            bool hardEnd = !lastLine &&
                           (pos + lineLen < (int)inputValue.size()) &&
                           inputValue[pos + lineLen] == '\n';

            if (remaining <= lineLen)
                return {i, remaining};

            remaining -= lineLen;
            pos       += lineLen;

            if (hardEnd) { remaining--; pos++; }  // consume '\n'
        }

        // Clamp to last position
        return {(int)lines.size() - 1,
                (int)lines.back().size()};
    }

    // ── lineColToByteOffset ───────────────────────────────────────────────────

    int lineColToByteOffset(const std::vector<std::string>& lines,
                            int lineIdx, int col) const
    {
        int offset = 0;
        for (int i = 0; i < lineIdx; ++i)
        {
            int lineLen = (int)lines[i].size();
            offset += lineLen;

            bool lastLine = (i == (int)lines.size() - 1);
            bool hardEnd  = !lastLine &&
                            (offset < (int)inputValue.size()) &&
                            inputValue[offset] == '\n';
            if (hardEnd) offset++;  // skip '\n'
        }
        return std::min(offset + col, (int)inputValue.size());
    }

    // ── measureStringWidth ────────────────────────────────────────────────────

    static int measureStringWidth(Painter& painter, NativeFont font,
                                  const std::string& s)
    {
        if (s.empty()) return 0;
        int w = 0, h = 0;
        painter.measureText(toWideString(s), font, w, h);
        return w;
    }

    // ── getColFromPixelX ──────────────────────────────────────────────────────
    // Returns the column index in lineStr closest to pixelX.

    static int getColFromPixelX(Painter& painter, NativeFont font,
                                 const std::string& lineStr, int pixelX)
    {
        if (lineStr.empty() || pixelX <= 0) return 0;

        int bestCol  = 0;
        int bestDist = abs(pixelX);

        for (int i = 1; i <= (int)lineStr.size(); ++i)
        {
            int tw = measureStringWidth(painter, font, lineStr.substr(0, i));
            int dist = abs(tw - pixelX);
            if (dist < bestDist) { bestDist = dist; bestCol = i; }
        }
        return bestCol;
    }

    // ── scrollToCursor ────────────────────────────────────────────────────────

    void scrollToCursor(const std::vector<std::string>& lines)
    {
        auto [curLine, curCol] = cursorLineCol(lines);
        int cursorY   = curLine * lineHeight_;
        int viewH     = height - paddingTop - paddingBottom;
        int maxScroll = std::max(0, contentHeight - viewH);

        if (cursorY < scrollY)
            scrollY = cursorY;
        else if (cursorY + lineHeight_ > scrollY + viewH)
            scrollY = cursorY + lineHeight_ - viewH;

        scrollY = std::clamp(scrollY, 0, maxScroll);
    }

    void clampScroll()
    {
        auto* ui   = FluxUI::getCurrentInstance();
        auto  mc   = ui->getMeasureContext();
        NativeFont font = ui->getFontCache().getFont(fontFamily, fontSize, fontWeight);
        Painter painter(mc.ctx);
        const int innerW = width - paddingLeft - paddingRight - scrollbarWidth_ - gutterWidth_;
        auto lines = buildLines(painter, font, innerW);
        contentHeight = (int)lines.size() * lineHeight_;
        scrollToCursor(lines);
    }

    // ── Scrollbar helpers ─────────────────────────────────────────────────────

    int thumbHeight(int viewH) const
    {
        if (contentHeight <= 0) return viewH;
        float ratio = (float)viewH / contentHeight;
        return std::max(20, (int)(ratio * viewH));
    }

    int thumbTop(int viewH) const
    {
        int maxScroll = std::max(1, contentHeight - viewH);
        float ratio   = (float)scrollY / maxScroll;
        int  track    = viewH - thumbHeight(viewH);
        return (int)(ratio * track);
    }

    bool isInScrollbar(int mx, int my) const
    {
        int sbX = x + width - scrollbarWidth_;
        return (mx >= sbX && mx < x + width &&
                my >= y   && my < y + height);
    }

    bool isInScrollbarThumb(int mx, int my) const
    {
        int viewH = height - paddingTop - paddingBottom;
        if (contentHeight <= viewH) return false;

        int sbX  = x + width - scrollbarWidth_;
        int th   = thumbHeight(viewH);
        int ty   = y + paddingTop + thumbTop(viewH);

        return (mx >= sbX && mx < x + width &&
                my >= ty  && my < ty + th);
    }

    bool isInResizeHandle(int mx, int my) const
    {
        return (mx >= x + width  - resizeHandleSize &&
                my >= y + height - resizeHandleSize);
    }

    bool hitTest(int mx, int my) const
    {
        return (mx >= x && mx < x + width &&
                my >= y && my < y + height);
    }

    void drawScrollbar(Painter& painter, int innerY, int viewH) const
    {
        // Only draw when content overflows
        if (contentHeight <= viewH) return;

        int sbX   = x + width - scrollbarWidth_;
        int sbH   = height;

        // Track
        painter.fillRect(sbX, y, scrollbarWidth_, sbH, scrollbarTrackColor);

        // Thumb
        int th   = thumbHeight(viewH);
        int ty   = y + paddingTop + thumbTop(viewH);
        Color tc = isScrollbarHovered ? scrollbarThumbHover : scrollbarThumbColor;
        painter.fillRoundedRect(sbX + 2, ty, scrollbarWidth_ - 4, th,
                                (scrollbarWidth_ - 4) / 2, tc);
    }

    void notifyStateBinding()
    {
        if (boundStringState_)
            boundStringState_->set(inputValue);
        if (onValueChanged)
            onValueChanged(inputValue);
    }
};

// ============================================================================
// Factory function
// ============================================================================

using TextAreaWidgetPtr = std::shared_ptr<TextAreaWidget>;

inline TextAreaWidgetPtr TextArea(const std::string& placeholder = "",
                                  int rows = 4, int cols = 40)
{
    auto w = std::make_shared<TextAreaWidget>();
    w->rows = rows;
    w->cols = cols;
    if (!placeholder.empty())
        w->setPlaceholder(placeholder);
    return w;
}

#endif // FLUX_TEXTAREA_HPP