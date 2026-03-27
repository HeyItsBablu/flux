#ifndef FLUX_INPUT_EXTENDED_HPP
#define FLUX_INPUT_EXTENDED_HPP

#include "flux_collection.hpp" // for ScrollbarState
#include "flux_core.hpp"
#include "flux_state.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

// ============================================================================
// TEXT AREA WIDGET  — multiline text input
// ============================================================================
//
// Usage:
//   TextArea("Type your message...")
//       ->setInputValue(bodyState)
//       ->setWidth(400)
//       ->setHeight(200)
//       ->setLineNumbers(true);
//
// Scrollbars use the same ScrollbarState helper as ListView / GridView so
// all scroll behaviour (thumb sizing, drag, wheel, hover) is identical.
// ============================================================================

class TextAreaWidget : public Widget {
public:
  // ── Appearance ────────────────────────────────────────────────────────────
  COLORREF focusedBorderColor = RGB(33, 150, 243);
  COLORREF unfocusedBorderColor = RGB(180, 180, 180);
  COLORREF placeholderColor = RGB(180, 180, 180);
  COLORREF inputTextColor = RGB(30, 30, 30);
  COLORREF lineNumBgColor = RGB(245, 246, 248);
  COLORREF lineNumTextColor = RGB(150, 150, 150);
  COLORREF lineNumBorderColor = RGB(220, 220, 220);
  COLORREF selectionColor = RGB(173, 214, 255);
  COLORREF cursorColor = RGB(30, 30, 30);

  // ── Config ────────────────────────────────────────────────────────────────
  std::string placeholder;
  bool showLineNumbers = false;
  bool wordWrap = false;
  int tabSpaces = 4;
  int lineSpacing = 2;
  int maxLength = 0; // 0 = unlimited

  // ── Constructor ───────────────────────────────────────────────────────────
  TextAreaWidget() {
    isFocusable = true;
    hasBorder = true;
    hasBackground = true;
    backgroundColor = RGB(255, 255, 255);
    borderColor = unfocusedBorderColor;
    borderWidth = 1;
    borderRadius = 4;
    paddingLeft = paddingRight = 10;
    paddingTop = paddingBottom = 8;
    width = 300;
    height = 150;
    autoWidth = false;
    autoHeight = false;
    minWidth = 40;
    minHeight = 40;
    lineH_ = 20;

    // Vertical scrollbar (right edge)
    sbV_.size = 8;
    sbV_.horizontal = false;
    sbV_.colorNormal = RGB(180, 180, 180);
    sbV_.colorHover = RGB(140, 140, 140);
    sbV_.colorActive = RGB(100, 100, 100);
    sbV_.colorTrack = RGB(240, 240, 240);

    // Horizontal scrollbar (bottom edge)
    sbH_.size = 8;
    sbH_.horizontal = true;
    sbH_.colorNormal = RGB(180, 180, 180);
    sbH_.colorHover = RGB(140, 140, 140);
    sbH_.colorActive = RGB(100, 100, 100);
    sbH_.colorTrack = RGB(240, 240, 240);

    lines_.push_back("");
  }

  // ── Layout ────────────────────────────────────────────────────────────────
  void computeLayout(GraphicsContext & /*ctx*/,
                     const BoxConstraints &constraints, FontCache &) override {
    autoWidth = false;
    autoHeight = false;
    width = max(minWidth, min(width, constraints.maxWidth));
    height = max(minHeight, min(height, constraints.maxHeight));
    applyConstraints();

    // Update scrollbar viewports
    int gutterW = _gutterWidth_cached();
    int hStrip = sbH_.isScrollable ? sbH_.size : 0;
    int vStrip = sbV_.isScrollable ? sbV_.size : 0;

    sbV_.viewportMain = height - paddingTop - paddingBottom - hStrip;
    sbH_.viewportMain = width - paddingLeft - paddingRight - gutterW - vStrip;

    _updateScrollContent();

    needsLayout = false;
  }

  void positionChildren(int, int, int, int) override {}

  // Stop internal scroll/cursor repaints from bubbling a layout request up
  void markNeedsPaint() override { needsPaint = true; }

  // ── Render ────────────────────────────────────────────────────────────────
  void render(GraphicsContext &ctx, FontCache &fontCache) override {
    borderColor = isFocused ? focusedBorderColor : unfocusedBorderColor;
    drawRoundedRectangle(ctx);

    HFONT hFont = fontCache.getFont(fontSize, fontWeight);
    HFONT hOldFont = (HFONT)SelectObject(ctx.hdc, hFont);

    // Measure line height
    SIZE charSz;
    GetTextExtentPoint32(ctx.hdc, "Ag", 2, &charSz);
    lineH_ = charSz.cy + lineSpacing;

    int gutterW = _gutterWidth(ctx, fontCache);
    int hStrip = sbH_.isScrollable ? sbH_.size : 0;
    int vStrip = sbV_.isScrollable ? sbV_.size : 0;

    int cLeft = x + paddingLeft + gutterW;
    int cTop = y + paddingTop;
    int cRight = x + width - paddingRight - vStrip;
    int cBot = y + height - paddingBottom - hStrip;
    int cH = cBot - cTop;

    // Update viewport sizes now that we have lineH_
    sbV_.viewportMain = cH;
    sbH_.viewportMain = cRight - cLeft;
    _updateScrollContent();
    sbV_.updateThumb();
    sbH_.updateThumb();

    // Outer clip
    HRGN outerClip = CreateRectRgn(x + 1, y + 1, x + width - 1, y + height - 1);
    SelectClipRgn(ctx.hdc, outerClip);

    // Gutter
    if (showLineNumbers && gutterW > 0) {
      HBRUSH gb = CreateSolidBrush(lineNumBgColor);
      RECT gr = {x + paddingLeft, y, x + paddingLeft + gutterW, y + height};
      FillRect(ctx.hdc, &gr, gb);
      DeleteObject(gb);
      HPEN gp = CreatePen(PS_SOLID, 1, lineNumBorderColor);
      HPEN old = (HPEN)SelectObject(ctx.hdc, gp);
      MoveToEx(ctx.hdc, x + paddingLeft + gutterW - 1, y, nullptr);
      LineTo(ctx.hdc, x + paddingLeft + gutterW - 1, y + height);
      SelectObject(ctx.hdc, old);
      DeleteObject(gp);
    }

    // Placeholder
    if (lines_.size() == 1 && lines_[0].empty() && !placeholder.empty() &&
        !isFocused) {
      SetBkMode(ctx.hdc, TRANSPARENT);
      SetTextColor(ctx.hdc, placeholderColor);
      RECT pr = {cLeft, cTop, cRight, cBot};
      DrawTextA(ctx.hdc, placeholder.c_str(), -1, &pr,
                DT_LEFT | DT_TOP | DT_WORDBREAK);
      SelectObject(ctx.hdc, hOldFont);
      SelectClipRgn(ctx.hdc, nullptr);
      DeleteObject(outerClip);
      needsPaint = false;
      return;
    }

    // Text clip
    HRGN textClip = CreateRectRgn(cLeft, cTop, cRight, cBot);
    SelectClipRgn(ctx.hdc, textClip);
    SetBkMode(ctx.hdc, TRANSPARENT);

    int scrollY = sbV_.scrollOffset;
    int scrollX = sbH_.scrollOffset;

    int firstLine = scrollY / max(1, lineH_);
    int lastLine = min((int)lines_.size() - 1, firstLine + cH / lineH_ + 1);

    for (int li = firstLine; li <= lastLine; li++) {
      int lineY = cTop + li * lineH_ - scrollY;

      // Line number
      if (showLineNumbers && gutterW > 0) {
        SelectClipRgn(ctx.hdc, outerClip);
        std::string num = std::to_string(li + 1);
        HFONT nf = fontCache.getFont(fontSize - 1, FontWeight::Normal);
        HFONT onf = (HFONT)SelectObject(ctx.hdc, nf);
        SetTextColor(ctx.hdc, lineNumTextColor);
        RECT nr = {x + paddingLeft, lineY, x + paddingLeft + gutterW - 4,
                   lineY + lineH_};
        DrawTextA(ctx.hdc, num.c_str(), -1, &nr,
                  DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        SelectObject(ctx.hdc, onf);
        SelectClipRgn(ctx.hdc, textClip);
        SelectObject(ctx.hdc, hFont);
      }

      // Selection highlight
      if (_hasSelection()) {
        auto [selStart, selEnd] = _normalizedSelection();
        if (li >= selStart.line && li <= selEnd.line) {
          int hStart = (li == selStart.line)
                           ? _textWidth(ctx, lines_[li], 0, selStart.col)
                           : 0;
          int hEnd =
              (li == selEnd.line)
                  ? _textWidth(ctx, lines_[li], 0, selEnd.col)
                  : _textWidth(ctx, lines_[li], 0, (int)lines_[li].size());
          HBRUSH sb2 = CreateSolidBrush(selectionColor);
          RECT sr = {cLeft + hStart - scrollX, lineY, cLeft + hEnd - scrollX,
                     lineY + lineH_};
          FillRect(ctx.hdc, &sr, sb2);
          DeleteObject(sb2);
        }
      }

      // Text
      SetTextColor(ctx.hdc, inputTextColor);
      RECT lr = {cLeft - scrollX, lineY, cLeft + 8000, lineY + lineH_};
      DrawTextA(ctx.hdc, lines_[li].c_str(), -1, &lr,
                DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOCLIP);
    }

    // Cursor
    if (isFocused && cursorVisible_) {
      int curX =
          cLeft + _textWidth(ctx, lines_[cursorLine_], 0, cursorCol_) - scrollX;
      int curY = cTop + cursorLine_ * lineH_ - scrollY;
      HPEN cp = CreatePen(PS_SOLID, 1, cursorColor);
      HPEN ocp = (HPEN)SelectObject(ctx.hdc, cp);
      MoveToEx(ctx.hdc, curX, curY + 2, nullptr);
      LineTo(ctx.hdc, curX, curY + lineH_ - 2);
      SelectObject(ctx.hdc, ocp);
      DeleteObject(cp);
    }

    // Restore outer clip, render scrollbars
    SelectClipRgn(ctx.hdc, outerClip);
    DeleteObject(textClip);

    sbV_.render(ctx, x, y, width, height);
    sbH_.render(ctx, x, y, width, height);

    SelectClipRgn(ctx.hdc, nullptr);
    DeleteObject(outerClip);
    SelectObject(ctx.hdc, hOldFont);
    needsPaint = false;
  }

  // ── Focus ─────────────────────────────────────────────────────────────────
  // REMOVE this helper
  // HWND _hwnd() const { ... }

  // handleFocus
  bool handleFocus(bool focused) override {
    isFocused = focused;
    auto *ui = FluxUI::getCurrentInstance();
    if (focused) {
      cursorVisible_ = true;
      _clearSelection();
      cursorTimerId_ = ui->setInterval(530, [this]() {
        cursorVisible_ = !cursorVisible_;
        _repaint();
      });
    } else {
      if (cursorTimerId_) {
        ui->clearInterval(cursorTimerId_);
        cursorTimerId_ = 0;
      }
      cursorVisible_ = false;
      _clearSelection();
    }
    _repaint();
    return true;
  }

  // ── Mouse ─────────────────────────────────────────────────────────────────
  bool handleMouseDown(int mx, int my) override {
    if (mx < x || mx >= x + width || my < y || my >= y + height)
      return false;

    auto *ui = FluxUI::getCurrentInstance();

    // V-scrollbar
    if (sbV_.onMouseDown(mx, my, x, y, width, height)) {
      if (sbV_.isDragging)
        ui->captureMouseInput();
      _repaint();
      return true;
    }
    // H-scrollbar
    if (sbH_.onMouseDown(mx, my, x, y, width, height)) {
      if (sbH_.isDragging)
        ui->captureMouseInput();
      _repaint();
      return true;
    }

    // Text area click
    auto [line, col] = _posFromMouse(mx, my);
    if (GetKeyState(VK_SHIFT) & 0x8000) {
      selAnchorLine_ = cursorLine_;
      selAnchorCol_ = cursorCol_;
      cursorLine_ = line;
      cursorCol_ = col;
    } else {
      _clearSelection();
      cursorLine_ = line;
      cursorCol_ = col;
      selAnchorLine_ = line;
      selAnchorCol_ = col;
      mouseSelecting_ = true;
    }
    cursorVisible_ = true;
    _scrollToCursor();
    _repaint();
    return true;
  }

  bool handleMouseMove(int mx, int my) override {
    // Scrollbar drags
    if (sbV_.isDragging) {
      sbV_.onMouseMove(mx, my, x, y, width, height);
      _repaint();
      return true;
    }
    if (sbH_.isDragging) {
      sbH_.onMouseMove(mx, my, x, y, width, height);
      _repaint();
      return true;
    }

    // Scrollbar hover
    bool vHov = sbV_.onMouseMove(mx, my, x, y, width, height);
    bool hHov = sbH_.onMouseMove(mx, my, x, y, width, height);
    if (vHov || hHov) {
      _repaint();
    }

    // Selection drag
    if (mouseSelecting_) {
      auto [line, col] = _posFromMouse(mx, my);
      cursorLine_ = line;
      cursorCol_ = col;
      cursorVisible_ = true;
      _scrollToCursor();
      _repaint();
    }
    return false;
  }

  bool handleMouseUp(int mx, int my) override {
    bool handled = false;
    if (sbV_.onMouseUp())
      handled = true;
    if (sbH_.onMouseUp())
      handled = true;
    mouseSelecting_ = false;
    if (handled)
      _repaint();
    return handled;
  }

  bool handleMouseLeave() override {
    bool v = sbV_.onMouseLeave();
    bool h = sbH_.onMouseLeave();
    mouseSelecting_ = false;
    if (v || h)
      _repaint();
    return false;
  }

  bool handleMouseWheel(int delta) override {
    if (sbV_.onWheel(delta)) {
      _repaint();
      return true;
    }
    return false;
  }

  // ── Keyboard ──────────────────────────────────────────────────────────────
  bool handleChar(wchar_t ch) override {
    if (ch < 32 && ch != '\t')
      return false;
    if (maxLength > 0 && _totalChars() >= maxLength)
      return false;

    if (_hasSelection())
      _deleteSelection();

    if (ch == '\t') {
      std::string spaces(tabSpaces, ' ');
      lines_[cursorLine_].insert(cursorCol_, spaces);
      cursorCol_ += tabSpaces;
    } else {
      lines_[cursorLine_].insert(cursorCol_, 1, (char)ch);
      cursorCol_++;
    }
    // FIX: keep anchor in sync so the newly typed char is never
    // considered selected on the next keystroke.
    selAnchorLine_ = cursorLine_;
    selAnchorCol_ = cursorCol_;

    cursorVisible_ = true;
    _scrollToCursor();
    _notifyState();
    _repaint();
    return true;
  }

  bool handleKeyDown(int key) override {
    bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;

    if (ctrl) {
      if (key == 'A') {
        _selectAll();
        _repaint();
        return true;
      }
      if (key == 'C') {
        _copyToClipboard();
        return true;
      }
      if (key == 'X') {
        _cutToClipboard();
        _notifyState();
        _repaint();
        return true;
      }
      if (key == 'V') {
        _pasteFromClipboard();
        _notifyState();
        _repaint();
        return true;
      }
    }

    if (shift && !_hasSelection()) {
      selAnchorLine_ = cursorLine_;
      selAnchorCol_ = cursorCol_;
    }

    switch (key) {
    case VK_RETURN: {
      if (_hasSelection())
        _deleteSelection();
      std::string tail = lines_[cursorLine_].substr(cursorCol_);
      lines_[cursorLine_].erase(cursorCol_);
      lines_.insert(lines_.begin() + cursorLine_ + 1, tail);
      cursorLine_++;
      cursorCol_ = 0;
      _clearSelection();
      break;
    }
    case VK_BACK:
      if (_hasSelection()) {
        _deleteSelection();
        break;
      }
      if (cursorCol_ > 0) {
        lines_[cursorLine_].erase(--cursorCol_, 1);
      } else if (cursorLine_ > 0) {
        int prevLen = (int)lines_[cursorLine_ - 1].size();
        lines_[cursorLine_ - 1] += lines_[cursorLine_];
        lines_.erase(lines_.begin() + cursorLine_--);
        cursorCol_ = prevLen;
      }
      _clearSelection();
      break;
    case VK_DELETE:
      if (_hasSelection()) {
        _deleteSelection();
        break;
      }
      if (cursorCol_ < (int)lines_[cursorLine_].size())
        lines_[cursorLine_].erase(cursorCol_, 1);
      else if (cursorLine_ < (int)lines_.size() - 1) {
        lines_[cursorLine_] += lines_[cursorLine_ + 1];
        lines_.erase(lines_.begin() + cursorLine_ + 1);
      }
      _clearSelection();
      break;
    case VK_LEFT:
      if (!shift && _hasSelection()) {
        auto [s, e] = _normalizedSelection();
        cursorLine_ = s.line;
        cursorCol_ = s.col;
        _clearSelection();
      } else if (cursorCol_ > 0)
        cursorCol_--;
      else if (cursorLine_ > 0) {
        cursorLine_--;
        cursorCol_ = (int)lines_[cursorLine_].size();
      }
      if (!shift)
        _clearSelection();
      break;
    case VK_RIGHT:
      if (!shift && _hasSelection()) {
        auto [s, e] = _normalizedSelection();
        cursorLine_ = e.line;
        cursorCol_ = e.col;
        _clearSelection();
      } else if (cursorCol_ < (int)lines_[cursorLine_].size())
        cursorCol_++;
      else if (cursorLine_ < (int)lines_.size() - 1) {
        cursorLine_++;
        cursorCol_ = 0;
      }
      if (!shift)
        _clearSelection();
      break;
    case VK_UP:
      if (cursorLine_ > 0) {
        cursorLine_--;
        cursorCol_ = min(cursorCol_, (int)lines_[cursorLine_].size());
      }
      if (!shift)
        _clearSelection();
      break;
    case VK_DOWN:
      if (cursorLine_ < (int)lines_.size() - 1) {
        cursorLine_++;
        cursorCol_ = min(cursorCol_, (int)lines_[cursorLine_].size());
      }
      if (!shift)
        _clearSelection();
      break;
    case VK_HOME:
      cursorCol_ = 0;
      if (!shift)
        _clearSelection();
      break;
    case VK_END:
      cursorCol_ = (int)lines_[cursorLine_].size();
      if (!shift)
        _clearSelection();
      break;
    case VK_PRIOR: {
      int page = max(1, sbV_.viewportMain / max(1, lineH_));
      cursorLine_ = max(0, cursorLine_ - page);
      cursorCol_ = min(cursorCol_, (int)lines_[cursorLine_].size());
      if (!shift)
        _clearSelection();
      break;
    }
    case VK_NEXT: {
      int page = max(1, sbV_.viewportMain / max(1, lineH_));
      cursorLine_ = min((int)lines_.size() - 1, cursorLine_ + page);
      cursorCol_ = min(cursorCol_, (int)lines_[cursorLine_].size());
      if (!shift)
        _clearSelection();
      break;
    }
    default:
      return false;
    }

    cursorVisible_ = true;
    _scrollToCursor();
    _notifyState();
    _repaint();
    return true;
  }

  // ── Public value API ──────────────────────────────────────────────────────
  std::string getValue() const {
    std::string out;
    for (int i = 0; i < (int)lines_.size(); i++) {
      if (i > 0)
        out += '\n';
      out += lines_[i];
    }
    return out;
  }

  void setValue(const std::string &v) {
    // If this call originated from our own _notifyState (i.e. the widget
    // itself made an edit and is pushing the value back to state), skip
    // the full rebuild — the lines_ vector is already correct and the
    // cursor position is where the user left it.
    if (selfUpdating_)
      return;

    lines_.clear();
    std::istringstream ss(v);
    std::string line;
    while (std::getline(ss, line))
      lines_.push_back(line);
    if (lines_.empty())
      lines_.push_back("");

    // Clamp cursor to valid bounds after external content change.
    cursorLine_ = min(cursorLine_, (int)lines_.size() - 1);
    cursorCol_ = min(cursorCol_, (int)lines_[cursorLine_].size());

    // FIX: keep selection anchor in sync with cursor so that no phantom
    // selection is created between the old anchor position and the newly
    // clamped cursor.  Without this, the first character typed after an
    // external setValue() call would be preceded by _deleteSelection(),
    // wiping out whatever was between anchor and cursor.
    selAnchorLine_ = cursorLine_;
    selAnchorCol_ = cursorCol_;

    _updateScrollContent();
    sbV_.clamp();
    sbH_.clamp();
    _repaint();
  }

  // ── Fluent setters ────────────────────────────────────────────────────────
  std::shared_ptr<TextAreaWidget> setInputValue(State<std::string> &state) {
    setValue(state.get());
    state.bindProperty(
        shared_from_this(),
        [](Widget *w, const std::string &v) {
          static_cast<TextAreaWidget *>(w)->setValue(v);
        },
        false);
    boundState_ = &state;
    return std::static_pointer_cast<TextAreaWidget>(shared_from_this());
  }
  std::shared_ptr<TextAreaWidget> setPlaceholder(const std::string &ph) {
    placeholder = ph;
    return self_();
  }
  std::shared_ptr<TextAreaWidget> setLineNumbers(bool v) {
    showLineNumbers = v;
    gutterW_cached_ = -1;
    markNeedsLayout();
    return self_();
  }
  std::shared_ptr<TextAreaWidget> setWordWrap(bool v) {
    wordWrap = v;
    markNeedsLayout();
    return self_();
  }
  std::shared_ptr<TextAreaWidget> setTabSpaces(int n) {
    tabSpaces = n;
    return self_();
  }
  std::shared_ptr<TextAreaWidget> setMaxLength(int n) {
    maxLength = n;
    return self_();
  }
  std::shared_ptr<TextAreaWidget> setFontSize(int s) {
    fontSize = s;
    markNeedsLayout();
    return self_();
  }
  std::shared_ptr<TextAreaWidget> setWidth(int w) {
    width = w;
    autoWidth = false;
    markNeedsLayout();
    return self_();
  }
  std::shared_ptr<TextAreaWidget> setHeight(int h) {
    height = h;
    autoHeight = false;
    markNeedsLayout();
    return self_();
  }
  std::shared_ptr<TextAreaWidget> setFlex(int f) {
    flex = f;
    return self_();
  }
  std::shared_ptr<TextAreaWidget> setScrollbarSize(int s) {
    sbV_.size = sbH_.size = s;
    return self_();
  }
  std::shared_ptr<TextAreaWidget> setScrollbarColor(COLORREF c) {
    sbV_.colorNormal = sbH_.colorNormal = c;
    return self_();
  }
  std::shared_ptr<TextAreaWidget> setScrollbarHoverColor(COLORREF c) {
    sbV_.colorHover = sbH_.colorHover = c;
    return self_();
  }
  std::shared_ptr<TextAreaWidget> setScrollbarTrackColor(COLORREF c) {
    sbV_.colorTrack = sbH_.colorTrack = c;
    return self_();
  }

private:
  // ── Scroll state (ScrollbarState from flux_collection.hpp) ────────────────
  ScrollbarState sbV_; // vertical
  ScrollbarState sbH_; // horizontal

  // ── Text state ────────────────────────────────────────────────────────────
  std::vector<std::string> lines_ = {""};
  int cursorLine_ = 0, cursorCol_ = 0;
  bool cursorVisible_ = false;
  TimerID cursorTimerId_ = 0;
  mutable int lineH_ = 20;
  mutable int gutterW_cached_ = -1;

  // Selection
  int selAnchorLine_ = 0, selAnchorCol_ = 0;
  bool mouseSelecting_ = false;

  State<std::string> *boundState_ = nullptr;
  bool selfUpdating_ = false; // true while pushing our own edit to state

  std::shared_ptr<TextAreaWidget> self_() {
    return std::static_pointer_cast<TextAreaWidget>(shared_from_this());
  }

  int _totalChars() const {
    int n = 0;
    for (auto &l : lines_)
      n += (int)l.size();
    return n + (int)lines_.size() - 1;
  }

  // Compute gutter width and cache it (needs HDC for font measurement)
  int _gutterWidth(GraphicsContext &ctx, FontCache &fc) const {
    if (!showLineNumbers) {
      gutterW_cached_ = 0;
      return 0;
    }
    int digits = (int)std::to_string(lines_.size()).size();
    SIZE sz;
    HFONT nf = fc.getFont(fontSize - 1, FontWeight::Normal);
    HFONT onf = (HFONT)SelectObject(ctx.hdc, nf);
    GetTextExtentPoint32(ctx.hdc, std::string(digits, '9').c_str(), digits,
                         &sz);
    SelectObject(ctx.hdc, onf);
    gutterW_cached_ = sz.cx + 16;
    return gutterW_cached_;
  }

  int _gutterWidth_cached() const {
    return gutterW_cached_ < 0 ? 0 : gutterW_cached_;
  }

  // Recompute content extents and push them into ScrollbarState
  void _updateScrollContent() {
    int totalLines = (int)lines_.size();
    sbV_.contentMain = totalLines * lineH_;
    sbV_.setScrollable(sbV_.contentMain > sbV_.viewportMain);

    if (!wordWrap) {
      int maxW = 0;
      for (auto &l : lines_)
        maxW = max(maxW, (int)l.size() * (fontSize / 2 + 1));
      sbH_.contentMain = maxW;
      sbH_.setScrollable(!wordWrap && sbH_.contentMain > sbH_.viewportMain);
    } else {
      sbH_.contentMain = 0;
      sbH_.setScrollable(false);
    }

    sbV_.clamp();
    sbV_.updateThumb();
    sbH_.clamp();
    sbH_.updateThumb();
  }

  void _scrollToCursor() {
    // Vertical
    int curTop = cursorLine_ * lineH_;
    int curBot = curTop + lineH_;
    int vp = sbV_.viewportMain;
    if (curTop < sbV_.scrollOffset)
      sbV_.scrollOffset = curTop;
    else if (curBot > sbV_.scrollOffset + vp)
      sbV_.scrollOffset = curBot - vp;
    sbV_.clamp();
    sbV_.updateThumb();

    // Horizontal (approximate — no HDC here)
    int curX = cursorCol_ * (fontSize / 2 + 1);
    int hp = sbH_.viewportMain;
    if (curX < sbH_.scrollOffset)
      sbH_.scrollOffset = max(0, curX - 10);
    else if (curX > sbH_.scrollOffset + hp - 10)
      sbH_.scrollOffset = curX - hp + 20;
    sbH_.clamp();
    sbH_.updateThumb();
  }

  int _textWidth(GraphicsContext &ctx, const std::string &s, int from,
                 int to) const {
    if (from >= to || s.empty())
      return 0;
    to = min(to, (int)s.size());
    SIZE sz;
    GetTextExtentPoint32(ctx.hdc, s.c_str() + from, to - from, &sz);
    return sz.cx;
  }

  std::pair<int, int> _posFromMouse(int mx, int my) const {
    int gutterW = _gutterWidth_cached();
    int cLeft = x + paddingLeft + gutterW;
    int cTop = y + paddingTop;

    int line = (my - cTop + sbV_.scrollOffset) / max(1, lineH_);
    line = max(0, min((int)lines_.size() - 1, line));

    int relX = mx - cLeft + sbH_.scrollOffset;
    int col = 0;

    if (!lines_[line].empty() && relX > 0) {
      auto *ui = FluxUI::getCurrentInstance();
      if (ui) {
        MeasureContext mc = ui->getMeasureContext();
        HFONT hf = ui->getFontCache().getFont(fontSize, fontWeight);
        HFONT ohf = (HFONT)SelectObject(mc.ctx.hdc, hf);
        int best = 0, bestDist = abs(relX);
        for (int i = 1; i <= (int)lines_[line].size(); i++) {
          SIZE sz;
          GetTextExtentPoint32(mc.ctx.hdc, lines_[line].c_str(), i, &sz);
          int d = abs(sz.cx - relX);
          if (d < bestDist) {
            bestDist = d;
            best = i;
          }
        }
        col = best;
        SelectObject(mc.ctx.hdc, ohf);
        // MeasureContext destructor releases DC
      }
    }
    return {line, col};
  }

  // ── Selection ─────────────────────────────────────────────────────────────
  struct CursorPos {
    int line, col;
  };

  bool _hasSelection() const {
    return cursorLine_ != selAnchorLine_ || cursorCol_ != selAnchorCol_;
  }

  // FIX: _clearSelection always brings anchor to the current cursor position.
  // This is the canonical way to collapse a selection — both anchor and cursor
  // must agree, otherwise _hasSelection() returns true spuriously.
  void _clearSelection() {
    selAnchorLine_ = cursorLine_;
    selAnchorCol_ = cursorCol_;
  }

  std::pair<CursorPos, CursorPos> _normalizedSelection() const {
    CursorPos a{selAnchorLine_, selAnchorCol_};
    CursorPos b{cursorLine_, cursorCol_};
    if (a.line > b.line || (a.line == b.line && a.col > b.col))
      std::swap(a, b);
    return {a, b};
  }

  void _selectAll() {
    selAnchorLine_ = 0;
    selAnchorCol_ = 0;
    cursorLine_ = (int)lines_.size() - 1;
    cursorCol_ = (int)lines_[cursorLine_].size();
  }

  void _deleteSelection() {
    auto [s, e] = _normalizedSelection();
    if (s.line == e.line) {
      lines_[s.line].erase(s.col, e.col - s.col);
    } else {
      lines_[s.line].erase(s.col);
      lines_[s.line] += lines_[e.line].substr(e.col);
      lines_.erase(lines_.begin() + s.line + 1, lines_.begin() + e.line + 1);
    }
    cursorLine_ = s.line;
    cursorCol_ = s.col;
    _clearSelection();
  }

  // ── Clipboard ─────────────────────────────────────────────────────────────
  std::string _selectedText() const {
    if (!_hasSelection())
      return "";
    auto [s, e] = _normalizedSelection();
    if (s.line == e.line)
      return lines_[s.line].substr(s.col, e.col - s.col);
    std::string out = lines_[s.line].substr(s.col) + "\n";
    for (int li = s.line + 1; li < e.line; li++)
      out += lines_[li] + "\n";
    out += lines_[e.line].substr(0, e.col);
    return out;
  }

  void _copyToClipboard() const {
    auto *ui = FluxUI::getCurrentInstance();
    if (!ui)
      return;
    std::string sel = _hasSelection() ? _selectedText() : getValue();
    if (!sel.empty())
      ui->setClipboardText(sel);
  }

  void _cutToClipboard() {
    _copyToClipboard();
    if (_hasSelection())
      _deleteSelection();
    else {
      lines_ = {""};
      cursorLine_ = 0;
      cursorCol_ = 0;
      _clearSelection();
    }
  }

  void _pasteFromClipboard() {
    auto *ui = FluxUI::getCurrentInstance();
    if (!ui)
      return;
    std::string text = ui->getClipboardText();
    if (_hasSelection())
      _deleteSelection();
    for (char ch : text) {
      if (ch == '\r')
        continue;
      if (ch == '\n') {
        std::string tail = lines_[cursorLine_].substr(cursorCol_);
        lines_[cursorLine_].erase(cursorCol_);
        lines_.insert(lines_.begin() + cursorLine_ + 1, tail);
        cursorLine_++;
        cursorCol_ = 0;
      } else {
        lines_[cursorLine_].insert(cursorCol_++, 1, ch);
      }
    }
    _clearSelection();
  }

  void _notifyState() {
    if (!boundState_)
      return;
    selfUpdating_ = true;
    boundState_->set(getValue());
    selfUpdating_ = false;
  }

  // Paint only this widget's rect without triggering a layout pass.
  // Used for all interactive updates (typing, scrolling, cursor blink)
  // so that parent layout is never re-run and the widget never collapses.
  void _repaint() {
    needsPaint = true;
    auto *ui = FluxUI::getCurrentInstance();
    if (ui)
      ui->invalidateWidget(x, y, width, height);
  }
};

// ── Factory
// ───────────────────────────────────────────────────────────────────

using TextAreaWidgetPtr = std::shared_ptr<TextAreaWidget>;

inline TextAreaWidgetPtr TextArea(const std::string &placeholder = "") {
  auto w = std::make_shared<TextAreaWidget>();
  if (!placeholder.empty())
    w->setPlaceholder(placeholder);
  return w;
}

// ============================================================================
// NUMBER INPUT / SPIN BOX WIDGET
// ============================================================================
//
// Usage:
//   NumberInput(0.0, 100.0, 1.0)
//       ->setValue(countState)
//       ->setPrefix("$")
//       ->setSuffix(" kg")
//       ->setDecimalPlaces(2)
//       ->setWidth(120);
//
// Features:
//   • Up/down arrow buttons on the right edge
//   • Mouse wheel increments/decrements
//   • Direct keyboard editing with validation on commit (Enter / focus-loss)
//   • Clamped to [min, max]; step snapping
//   • Optional prefix / suffix strings
//   • Two-way binding to State<double> or State<int>
//   • Configurable decimal places (0 = integer display)
// ============================================================================

class NumberInputWidget : public Widget {
public:
  double value = 0.0;
  double minValue = 0.0;
  double maxValue = 100.0;
  double step = 1.0;
  int decimalPlaces = 0;

  std::string prefix;
  std::string suffix;

  COLORREF focusedBorderColor = RGB(33, 150, 243);
  COLORREF unfocusedBorderColor = RGB(180, 180, 180);
  COLORREF buttonBgColor = RGB(245, 246, 248);
  COLORREF buttonHoverColor = RGB(225, 235, 248);
  COLORREF buttonArrowColor = RGB(80, 80, 80);
  COLORREF inputTextColor = RGB(30, 30, 30);
  COLORREF disabledColor = RGB(180, 180, 180);

  std::function<void(double)> onValueChanged;

  NumberInputWidget() {
    isFocusable = true;
    hasBorder = true;
    hasBackground = true;
    backgroundColor = RGB(255, 255, 255);
    borderColor = unfocusedBorderColor;
    borderWidth = 1;
    borderRadius = 4;
    paddingLeft = 10;
    paddingRight = 28 + 4; // reserve space for buttons
    paddingTop = paddingBottom = 8;
    height = 36;
    autoHeight = false;
    width = 120;
    autoWidth = false;
  }

  // ── Layout ────────────────────────────────────────────────────────────────
  void computeLayout(GraphicsContext & /*ctx*/,
                     const BoxConstraints &constraints, FontCache &) override {
    if (autoWidth)
      width = constraints.clampWidth(width);
    applyConstraints();
    needsLayout = false;
    // Sync editing buffer if not currently editing
    if (!editing_)
      editBuffer_ = _formatValue(value);
  }

  // ── Render ────────────────────────────────────────────────────────────────
  void render(GraphicsContext &ctx, FontCache &fontCache) override {
    borderColor = isFocused ? focusedBorderColor : unfocusedBorderColor;
    drawRoundedRectangle(ctx);

    int btnW = 24;
    int btnX = x + width - btnW - 1;

    // ── Button column background ──────────────────────────────────────────
    {
      HBRUSH bb = CreateSolidBrush(buttonBgColor);
      HPEN np = CreatePen(PS_NULL, 0, 0);
      HPEN op = (HPEN)SelectObject(ctx.hdc, np);
      HBRUSH ob = (HBRUSH)SelectObject(ctx.hdc, bb);
      RECT br = {btnX, y + 1, btnX + btnW, y + height - 1};
      FillRect(ctx.hdc, &br, bb);
      SelectObject(ctx.hdc, op);
      SelectObject(ctx.hdc, ob);
      DeleteObject(bb);
      DeleteObject(np);
    }

    // Divider between input and buttons
    {
      HPEN dp = CreatePen(PS_SOLID, 1, unfocusedBorderColor);
      HPEN old = (HPEN)SelectObject(ctx.hdc, dp);
      MoveToEx(ctx.hdc, btnX, y + 1, nullptr);
      LineTo(ctx.hdc, btnX, y + height - 1);
      // Mid divider between up/down buttons
      int midY = y + height / 2;
      MoveToEx(ctx.hdc, btnX, midY, nullptr);
      LineTo(ctx.hdc, btnX + btnW - 1, midY);
      SelectObject(ctx.hdc, old);
      DeleteObject(dp);
    }

    // ── Arrow buttons ─────────────────────────────────────────────────────
    _drawArrow(ctx, btnX, y + 1, btnW, height / 2 - 1, true, upHovered_);
    _drawArrow(ctx, btnX, y + height / 2, btnW, height - height / 2 - 1, false,
               downHovered_);

    // ── Text ──────────────────────────────────────────────────────────────
    HFONT hf = fontCache.getFont(fontSize, fontWeight);
    HFONT ohf = (HFONT)SelectObject(ctx.hdc, hf);
    SetBkMode(ctx.hdc, TRANSPARENT);
    SetTextColor(ctx.hdc, inputTextColor);

    std::string display = editing_ ? editBuffer_ : _formatValue(value);
    if (!prefix.empty())
      display = prefix + display;
    if (!suffix.empty() && !editing_)
      display += suffix;

    // Clip text to input area
    HRGN clip = CreateRectRgn(x + 1, y + 1, btnX - 1, y + height - 1);
    SelectClipRgn(ctx.hdc, clip);

    RECT tr = {x + paddingLeft, y + paddingTop, btnX - 2,
               y + height - paddingBottom};
    DrawTextA(ctx.hdc, display.c_str(), -1, &tr,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    // Cursor when editing
    if (isFocused && editing_ && cursorVisible_) {
      std::string beforeCursor =
          display.substr(0, (int)prefix.size() + editCursorPos_);
      SIZE sz;
      GetTextExtentPoint32(ctx.hdc, beforeCursor.c_str(),
                           (int)beforeCursor.size(), &sz);
      int curX = x + paddingLeft + sz.cx;
      HPEN cp = CreatePen(PS_SOLID, 1, inputTextColor);
      HPEN ocp = (HPEN)SelectObject(ctx.hdc, cp);
      MoveToEx(ctx.hdc, curX, y + paddingTop + 2, nullptr);
      LineTo(ctx.hdc, curX, y + height - paddingBottom - 2);
      SelectObject(ctx.hdc, ocp);
      DeleteObject(cp);
    }

    SelectClipRgn(ctx.hdc, nullptr);
    DeleteObject(clip);
    SelectObject(ctx.hdc, ohf);
    needsPaint = false;
  }

  // ── Focus ─────────────────────────────────────────────────────────────────
  bool handleFocus(bool focused) override {
    isFocused = focused;
    auto *ui = FluxUI::getCurrentInstance();
    if (focused) {
      editing_ = true;
      editBuffer_ = _formatValue(value);
      editCursorPos_ = (int)editBuffer_.size();
      cursorVisible_ = true;
      cursorTimerId_ = ui->setInterval(530, [this]() {
        cursorVisible_ = !cursorVisible_;
        markNeedsPaint();
      });
    } else {
      _commitEdit();
      editing_ = false;
      cursorVisible_ = false;
      if (cursorTimerId_) {
        ui->clearInterval(cursorTimerId_);
        cursorTimerId_ = 0;
      }
    }
    markNeedsPaint();
    return true;
  }

  // ── Mouse ─────────────────────────────────────────────────────────────────
  bool handleMouseDown(int mx, int my) override {
    if (mx < x || mx >= x + width || my < y || my >= y + height)
      return false;

    int btnX = x + width - 24 - 1;
    if (mx >= btnX) {
      int midY = y + height / 2;
      if (my < midY)
        _increment();
      else
        _decrement();
      _commitImmediate();
      return true;
    }
    // Click in text area — position cursor
    if (editing_) {
      editCursorPos_ = min((int)editBuffer_.size(),
                           max(0, mx - x - paddingLeft) / max(1, fontSize / 2));
    }
    return true;
  }

  bool handleMouseMove(int mx, int my) override {
    int btnX = x + width - 24 - 1;
    int midY = y + height / 2;
    bool overBtn = mx >= btnX && mx < x + width && my >= y && my < y + height;
    bool newUp = overBtn && my < midY;
    bool newDown = overBtn && my >= midY;
    if (newUp != upHovered_ || newDown != downHovered_) {
      upHovered_ = newUp;
      downHovered_ = newDown;
      markNeedsPaint();
    }
    return false;
  }

  bool handleMouseLeave() override {
    upHovered_ = downHovered_ = false;
    markNeedsPaint();
    return false;
  }

  bool handleMouseWheel(int delta) override {
    if (delta > 0)
      _increment();
    else
      _decrement();
    _commitImmediate();
    return true;
  }

  // ── Keyboard ──────────────────────────────────────────────────────────────
  bool handleChar(wchar_t ch) override {
    if (!editing_)
      return false;
    // Allow digits, minus, dot
    if ((ch >= '0' && ch <= '9') ||
        (ch == '-' && editCursorPos_ == 0 && minValue < 0) ||
        (ch == '.' && decimalPlaces > 0 &&
         editBuffer_.find('.') == std::string::npos)) {
      editBuffer_.insert(editCursorPos_++, 1, (char)ch);
      cursorVisible_ = true;
      markNeedsPaint();
      return true;
    }
    return false;
  }

  bool handleKeyDown(int key) override {
    switch (key) {
    case VK_UP:
      _increment();
      _commitImmediate();
      return true;
    case VK_DOWN:
      _decrement();
      _commitImmediate();
      return true;
    case VK_PRIOR:
      value = min(maxValue, value + step * 10);
      _commitImmediate();
      return true;
    case VK_NEXT:
      value = max(minValue, value - step * 10);
      _commitImmediate();
      return true;
    case VK_HOME:
      value = minValue;
      _commitImmediate();
      return true;
    case VK_END:
      value = maxValue;
      _commitImmediate();
      return true;
    case VK_RETURN:
      _commitEdit();
      return true;
    case VK_ESCAPE:
      editing_ = false;
      editBuffer_ = _formatValue(value);
      markNeedsPaint();
      return true;
    case VK_BACK:
      if (editing_ && editCursorPos_ > 0) {
        editBuffer_.erase(--editCursorPos_, 1);
        cursorVisible_ = true;
        markNeedsPaint();
      }
      return true;
    case VK_DELETE:
      if (editing_ && editCursorPos_ < (int)editBuffer_.size()) {
        editBuffer_.erase(editCursorPos_, 1);
        cursorVisible_ = true;
        markNeedsPaint();
      }
      return true;
    case VK_LEFT:
      if (editCursorPos_ > 0) {
        editCursorPos_--;
        cursorVisible_ = true;
        markNeedsPaint();
      }
      return true;
    case VK_RIGHT:
      if (editCursorPos_ < (int)editBuffer_.size()) {
        editCursorPos_++;
        cursorVisible_ = true;
        markNeedsPaint();
      }
      return true;
    }
    return false;
  }

  // ── Public API ────────────────────────────────────────────────────────────

  double getValue() const { return value; }

  std::shared_ptr<NumberInputWidget> setValue(double v) {
    value = _clamp(_snap(v));
    editBuffer_ = _formatValue(value);
    markNeedsPaint();
    return std::static_pointer_cast<NumberInputWidget>(shared_from_this());
  }

  std::shared_ptr<NumberInputWidget> setValue(State<double> &state) {
    value = _clamp(_snap(state.get()));
    editBuffer_ = _formatValue(value);
    state.bindProperty(
        shared_from_this(),
        [](Widget *w, const double &v) {
          auto *self = static_cast<NumberInputWidget *>(w);
          self->value = self->_clamp(self->_snap(v));
          self->editBuffer_ = self->_formatValue(self->value);
          self->markNeedsPaint();
        },
        false);
    boundDoubleState_ = &state;
    return std::static_pointer_cast<NumberInputWidget>(shared_from_this());
  }

  std::shared_ptr<NumberInputWidget> setValue(State<int> &state) {
    value = _clamp(_snap((double)state.get()));
    editBuffer_ = _formatValue(value);
    state.bindProperty(
        shared_from_this(),
        [](Widget *w, const int &v) {
          auto *self = static_cast<NumberInputWidget *>(w);
          self->value = self->_clamp(self->_snap((double)v));
          self->editBuffer_ = self->_formatValue(self->value);
          self->markNeedsPaint();
        },
        false);
    boundIntState_ = &state;
    return std::static_pointer_cast<NumberInputWidget>(shared_from_this());
  }

  std::shared_ptr<NumberInputWidget> setMin(double v) {
    minValue = v;
    return self_();
  }
  std::shared_ptr<NumberInputWidget> setMax(double v) {
    maxValue = v;
    return self_();
  }
  std::shared_ptr<NumberInputWidget> setStep(double v) {
    step = v;
    return self_();
  }
  std::shared_ptr<NumberInputWidget> setDecimalPlaces(int n) {
    decimalPlaces = n;
    return self_();
  }
  std::shared_ptr<NumberInputWidget> setPrefix(const std::string &p) {
    prefix = p;
    return self_();
  }
  std::shared_ptr<NumberInputWidget> setSuffix(const std::string &s) {
    suffix = s;
    return self_();
  }
  std::shared_ptr<NumberInputWidget>
  setOnValueChanged(std::function<void(double)> fn) {
    onValueChanged = std::move(fn);
    return self_();
  }
  std::shared_ptr<NumberInputWidget> setWidth(int w) {
    width = w;
    autoWidth = false;
    return self_();
  }
  std::shared_ptr<NumberInputWidget> setFlex(int f) {
    flex = f;
    return self_();
  }

private:
  std::string editBuffer_;
  int editCursorPos_ = 0;
  bool editing_ = false;
  bool cursorVisible_ = false;
  TimerID cursorTimerId_ = 0;
  bool upHovered_ = false;
  bool downHovered_ = false;

  State<double> *boundDoubleState_ = nullptr;
  State<int> *boundIntState_ = nullptr;

  std::shared_ptr<NumberInputWidget> self_() {
    return std::static_pointer_cast<NumberInputWidget>(shared_from_this());
  }

  double _snap(double v) const {
    if (step <= 0)
      return v;
    return std::round(v / step) * step;
  }
  double _clamp(double v) const { return max(minValue, min(maxValue, v)); }

  std::string _formatValue(double v) const {
    if (decimalPlaces <= 0)
      return std::to_string((long long)std::round(v));
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(decimalPlaces) << v;
    return oss.str();
  }

  void _increment() {
    value = _clamp(_snap(value + step));
    editBuffer_ = _formatValue(value);
    markNeedsPaint();
  }
  void _decrement() {
    value = _clamp(_snap(value - step));
    editBuffer_ = _formatValue(value);
    markNeedsPaint();
  }

  void _commitEdit() {
    if (!editBuffer_.empty()) {
      try {
        value = _clamp(_snap(std::stod(editBuffer_)));
      } catch (...) { /* invalid input — revert */
      }
    }
    editBuffer_ = _formatValue(value);
    editCursorPos_ = (int)editBuffer_.size();
    _fireChange();
    markNeedsPaint();
  }

  void _commitImmediate() {
    editBuffer_ = _formatValue(value);
    _fireChange();
    markNeedsPaint();
  }

  void _fireChange() {
    if (onValueChanged)
      onValueChanged(value);
    if (boundDoubleState_)
      boundDoubleState_->set(value);
    if (boundIntState_)
      boundIntState_->set((int)std::round(value));
  }

  void _drawArrow(GraphicsContext &ctx, int bx, int by, int bw, int bh, bool up,
                  bool hovered) const {
    if (hovered) {
      HBRUSH hb = CreateSolidBrush(buttonHoverColor);
      RECT hr = {bx + 1, by + 1, bx + bw - 1, by + bh - 1};
      FillRect(ctx.hdc, &hr, hb);
      DeleteObject(hb);
    }
    HPEN ap = CreatePen(PS_SOLID, 1, buttonArrowColor);
    HPEN oap = (HPEN)SelectObject(ctx.hdc, ap);
    int cx = bx + bw / 2;
    int cy = by + bh / 2;
    int hs = 4;
    if (up) {
      MoveToEx(ctx.hdc, cx - hs, cy + hs / 2, nullptr);
      LineTo(ctx.hdc, cx, cy - hs / 2);
      LineTo(ctx.hdc, cx + hs, cy + hs / 2);
    } else {
      MoveToEx(ctx.hdc, cx - hs, cy - hs / 2, nullptr);
      LineTo(ctx.hdc, cx, cy + hs / 2);
      LineTo(ctx.hdc, cx + hs, cy - hs / 2);
    }
    SelectObject(ctx.hdc, oap);
    DeleteObject(ap);
  }
};

// ── Factory
// ───────────────────────────────────────────────────────────────────

using NumberInputWidgetPtr = std::shared_ptr<NumberInputWidget>;

inline NumberInputWidgetPtr
NumberInput(double minVal = 0.0, double maxVal = 100.0, double step = 1.0) {
  auto w = std::make_shared<NumberInputWidget>();
  w->minValue = minVal;
  w->maxValue = maxVal;
  w->step = step;
  return w;
}

// Alias — same widget, different name per README
inline NumberInputWidgetPtr SpinBox(double minVal = 0.0, double maxVal = 100.0,
                                    double step = 1.0) {
  return NumberInput(minVal, maxVal, step);
}

#endif // FLUX_INPUT_EXTENDED_HPP