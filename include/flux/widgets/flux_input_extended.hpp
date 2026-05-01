#ifndef FLUX_INPUT_EXTENDED_HPP
#define FLUX_INPUT_EXTENDED_HPP

#include "flux_collection.hpp" 
#include "flux_keyboard.hpp"  

#include "../flux_core.hpp"
#include "../flux_state.hpp"
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

// ============================================================================

class TextAreaWidget : public Widget {
public:
  // ── Appearance ────────────────────────────────────────────────────────────
  Color focusedBorderColor   = Color::fromRGB(33,  150, 243);
  Color unfocusedBorderColor = Color::fromRGB(180, 180, 180);
  Color placeholderColor     = Color::fromRGB(180, 180, 180);
  Color inputTextColor       = Color::fromRGB(30,  30,  30);
  Color lineNumBgColor       = Color::fromRGB(245, 246, 248);
  Color lineNumTextColor     = Color::fromRGB(150, 150, 150);
  Color lineNumBorderColor   = Color::fromRGB(220, 220, 220);
  Color selectionColor       = Color::fromRGB(173, 214, 255);
  Color cursorColor          = Color::fromRGB(30,  30,  30);

  // ── Config ────────────────────────────────────────────────────────────────
  std::string placeholder;
  bool showLineNumbers = false;
  bool wordWrap        = false;
  int  tabSpaces       = 4;
  int  lineSpacing     = 2;
  int  maxLength       = 0; // 0 = unlimited

  // ── Constructor ───────────────────────────────────────────────────────────
  TextAreaWidget() {
    isFocusable     = true;
    hasBorder       = true;
    hasBackground   = true;
    backgroundColor = Color::fromRGB(255, 255, 255);
    borderColor     = unfocusedBorderColor;
    borderWidth     = 1;
    borderRadius    = 4;
    paddingLeft = paddingRight = 10;
    paddingTop  = paddingBottom = 8;
    width      = 300;
    height     = 150;
    autoWidth  = false;
    autoHeight = false;
    minWidth   = 40;
    minHeight  = 40;
    lineH_     = 20;

    sbV_.size         = 8;
    sbV_.horizontal   = false;
    sbV_.colorNormal  = Color::fromRGB(180, 180, 180);
    sbV_.colorHover   = Color::fromRGB(140, 140, 140);
    sbV_.colorActive  = Color::fromRGB(100, 100, 100);
    sbV_.colorTrack   = Color::fromRGB(240, 240, 240);

    sbH_.size         = 8;
    sbH_.horizontal   = true;
    sbH_.colorNormal  = Color::fromRGB(180, 180, 180);
    sbH_.colorHover   = Color::fromRGB(140, 140, 140);
    sbH_.colorActive  = Color::fromRGB(100, 100, 100);
    sbH_.colorTrack   = Color::fromRGB(240, 240, 240);

    lines_.push_back("");
  }

  // ── Layout ────────────────────────────────────────────────────────────────
  void computeLayout(GraphicsContext & /*ctx*/,
                     const BoxConstraints &constraints, FontCache &) override {
    autoWidth  = false;
    autoHeight = false;

    if (constraints.maxWidth  < width)  width  = constraints.maxWidth;
    if (constraints.maxHeight < height) height = constraints.maxHeight;
    width  = std::max(minWidth,  width);
    height = std::max(minHeight, height);
    applyConstraints();

    int gutterW = _gutterWidth_cached();
    int hStrip  = sbH_.isScrollable ? sbH_.size : 0;
    int vStrip  = sbV_.isScrollable ? sbV_.size : 0;

    sbV_.viewportMain = height - paddingTop - paddingBottom - hStrip;
    sbH_.viewportMain = width  - paddingLeft - paddingRight - gutterW - vStrip;

    _updateScrollContent();
    needsLayout = false;
  }

  void positionChildren(int, int, int, int) override {}

  // Internal scroll/cursor repaints must not propagate a layout request.
  void markNeedsPaint() override { needsPaint = true; }

  // ── Render ────────────────────────────────────────────────────────────────
  void render(GraphicsContext &ctx, FontCache &fontCache) override {
    borderColor = isFocused ? focusedBorderColor : unfocusedBorderColor;
    drawRoundedRectangle(ctx);

    Painter painter(ctx);

    NativeFont hFont = fontCache.getFont(fontSize, fontWeight);
    std::wstring probe = L"Ag";
    int probeW = 0, probeH = 0;
    painter.measureText(probe, hFont, probeW, probeH);
    lineH_ = probeH + lineSpacing;

    int gutterW = _gutterWidth(ctx, fontCache);
    int hStrip  = sbH_.isScrollable ? sbH_.size : 0;
    int vStrip  = sbV_.isScrollable ? sbV_.size : 0;

    int cLeft = x + paddingLeft + gutterW;
    int cTop  = y + paddingTop;
    int cRight = x + width  - paddingRight  - vStrip;
    int cBot   = y + height - paddingBottom - hStrip;
    int cH     = cBot - cTop;

    sbV_.viewportMain = cH;
    sbH_.viewportMain = cRight - cLeft;
    _updateScrollContent();
    sbV_.updateThumb();
    sbH_.updateThumb();

    painter.pushClipRect(x + 1, y + 1, width - 2, height - 2);

    // ── Gutter ────────────────────────────────────────────────────────────
    if (showLineNumbers && gutterW > 0) {
      painter.fillRect(x + paddingLeft, y, gutterW, height, lineNumBgColor);
      painter.drawLine(x + paddingLeft + gutterW - 1, y,
                       x + paddingLeft + gutterW - 1, y + height,
                       lineNumBorderColor, 1);
    }

    // ── Placeholder ───────────────────────────────────────────────────────
    if (lines_.size() == 1 && lines_[0].empty() && !placeholder.empty() && !isFocused) {
      std::wstring wph = toWideString(placeholder);
      painter.drawText(wph, cLeft, cTop, cRight - cLeft, cBot - cTop, hFont,
                       placeholderColor, DT_LEFT | DT_TOP | DT_WORDBREAK);
      painter.popClipRect();
      sbV_.render(ctx, x, y, width, height);
      sbH_.render(ctx, x, y, width, height);
      needsPaint = false;
      return;
    }

    painter.pushClipRect(cLeft, cTop, cRight - cLeft, cBot - cTop);

    int scrollY   = sbV_.scrollOffset;
    int scrollX   = sbH_.scrollOffset;
    int firstLine = scrollY / std::max(1, lineH_);
    int lastLine  = std::min((int)lines_.size() - 1, firstLine + cH / lineH_ + 1);

    for (int li = firstLine; li <= lastLine; li++) {
      int lineY = cTop + li * lineH_ - scrollY;

      // ── Line number ───────────────────────────────────────────────────
      if (showLineNumbers && gutterW > 0) {
        painter.popClipRect();
        painter.pushClipRect(x + 1, y + 1, width - 2, height - 2);

        std::wstring wnum = toWideString(std::to_string(li + 1));
        NativeFont nf     = fontCache.getFont(fontSize - 1, FontWeight::Normal);
        painter.drawText(wnum, x + paddingLeft, lineY, gutterW - 4, lineH_, nf,
                         lineNumTextColor, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

        painter.pushClipRect(cLeft, cTop, cRight - cLeft, cBot - cTop);
      }

      // ── Selection highlight ───────────────────────────────────────────
      if (_hasSelection()) {
        auto [selStart, selEnd] = _normalizedSelection();
        if (li >= selStart.line && li <= selEnd.line) {
          int hStart = (li == selStart.line)
                           ? _textWidth(ctx, lines_[li], 0, selStart.col) : 0;
          int hEnd   = (li == selEnd.line)
                           ? _textWidth(ctx, lines_[li], 0, selEnd.col)
                           : _textWidth(ctx, lines_[li], 0, (int)lines_[li].size());
          painter.fillRect(cLeft + hStart - scrollX, lineY,
                           hEnd - hStart, lineH_, selectionColor);
        }
      }

      // ── Line text ─────────────────────────────────────────────────────
      if (!lines_[li].empty()) {
        std::wstring wline = toWideString(lines_[li]);
        painter.drawText(wline, cLeft - scrollX, lineY, 8000, lineH_, hFont,
                         inputTextColor, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOCLIP);
      }
    }

    // ── Cursor ────────────────────────────────────────────────────────────
    if (isFocused && cursorVisible_) {
      int curX = cLeft + _textWidth(ctx, lines_[cursorLine_], 0, cursorCol_) - scrollX;
      int curY = cTop  + cursorLine_ * lineH_ - scrollY;
      painter.drawLine(curX, curY + 2, curX, curY + lineH_ - 2, cursorColor, 1);
    }

    painter.popClipRect(); // text clip
    painter.popClipRect(); // outer clip

    sbV_.render(ctx, x, y, width, height);
    sbH_.render(ctx, x, y, width, height);
    needsPaint = false;
  }

  // ── Focus ─────────────────────────────────────────────────────────────────
  // VirtualKeyboard::notify* calls are no-ops on Windows/Linux — the guard
  // is inside flux_keyboard.hpp, so no #ifdef is needed here.
  bool handleFocus(bool focused) override {
    isFocused   = focused;
    auto *ui    = FluxUI::getCurrentInstance();

    if (focused) {
      cursorVisible_ = true;
      _clearSelection();
      cursorTimerId_ = ui->setInterval(530, [this]() {
        cursorVisible_ = !cursorVisible_;
        _repaint();
      });
      VirtualKeyboard::notifyFocusGained(this);
    } else {
      if (cursorTimerId_) {
        ui->clearInterval(cursorTimerId_);
        cursorTimerId_ = 0;
      }
      cursorVisible_ = false;
      _clearSelection();
      VirtualKeyboard::notifyFocusLost();
    }

    _repaint();
    return true;
  }

  // ── Mouse ─────────────────────────────────────────────────────────────────
  bool handleMouseDown(int mx, int my) override {
    if (mx < x || mx >= x + width || my < y || my >= y + height) return false;

    auto *ui = FluxUI::getCurrentInstance();

    if (sbV_.onMouseDown(mx, my, x, y, width, height)) {
      if (sbV_.isDragging) ui->captureMouseInput();
      _repaint();
      return true;
    }
    if (sbH_.onMouseDown(mx, my, x, y, width, height)) {
      if (sbH_.isDragging) ui->captureMouseInput();
      _repaint();
      return true;
    }

    auto [line, col] = _posFromMouse(mx, my);
    if (platformShiftDown()) {
      selAnchorLine_ = cursorLine_;
      selAnchorCol_  = cursorCol_;
      cursorLine_    = line;
      cursorCol_     = col;
    } else {
      _clearSelection();
      cursorLine_    = line;
      cursorCol_     = col;
      selAnchorLine_ = line;
      selAnchorCol_  = col;
      mouseSelecting_ = true;
    }
    cursorVisible_ = true;
    _scrollToCursor();
    _repaint();
    return true;
  }

  bool handleMouseMove(int mx, int my) override {
    if (sbV_.isDragging) { sbV_.onMouseMove(mx, my, x, y, width, height); _repaint(); return true; }
    if (sbH_.isDragging) { sbH_.onMouseMove(mx, my, x, y, width, height); _repaint(); return true; }

    bool vHov = sbV_.onMouseMove(mx, my, x, y, width, height);
    bool hHov = sbH_.onMouseMove(mx, my, x, y, width, height);
    if (vHov || hHov) _repaint();

    if (mouseSelecting_) {
      auto [line, col] = _posFromMouse(mx, my);
      cursorLine_    = line;
      cursorCol_     = col;
      cursorVisible_ = true;
      _scrollToCursor();
      _repaint();
    }
    return false;
  }

  bool handleMouseUp(int /*mx*/, int /*my*/) override {
    bool handled = false;
    if (sbV_.onMouseUp()) handled = true;
    if (sbH_.onMouseUp()) handled = true;
    mouseSelecting_ = false;
    if (handled) _repaint();
    return handled;
  }

  bool handleMouseLeave() override {
    bool v = sbV_.onMouseLeave();
    bool h = sbH_.onMouseLeave();
    mouseSelecting_ = false;
    if (v || h) _repaint();
    return false;
  }

  bool handleMouseWheel(int delta) override {
    if (sbV_.onWheel(delta)) { _repaint(); return true; }
    return false;
  }

  // ── Keyboard ──────────────────────────────────────────────────────────────
  bool handleChar(wchar_t ch) override {
    if (ch < 32 && ch != '\t') return false;
    if (maxLength > 0 && _totalChars() >= maxLength) return false;

    if (_hasSelection()) _deleteSelection();

    if (ch == '\t') {
      std::string spaces(tabSpaces, ' ');
      lines_[cursorLine_].insert(cursorCol_, spaces);
      cursorCol_ += tabSpaces;
    } else {
      lines_[cursorLine_].insert(cursorCol_, 1, (char)ch);
      cursorCol_++;
    }
    selAnchorLine_ = cursorLine_;
    selAnchorCol_  = cursorCol_;

    cursorVisible_ = true;
    _scrollToCursor();
    _notifyState();
    _repaint();
    return true;
  }

  bool handleKeyDown(int key) override {
    bool shift = platformShiftDown();
    bool ctrl  = platformCtrlDown(); // use proper ctrl query, not shift

    if (ctrl) {
      if (key == 'A') { _selectAll(); _repaint(); return true; }
      if (key == 'C') { _copyToClipboard(); return true; }
      if (key == 'X') { _cutToClipboard(); _notifyState(); _repaint(); return true; }
      if (key == 'V') { _pasteFromClipboard(); _notifyState(); _repaint(); return true; }
    }

    if (shift && !_hasSelection()) {
      selAnchorLine_ = cursorLine_;
      selAnchorCol_  = cursorCol_;
    }

    switch (key) {
    case Key::Return: {
      if (_hasSelection()) _deleteSelection();
      std::string tail = lines_[cursorLine_].substr(cursorCol_);
      lines_[cursorLine_].erase(cursorCol_);
      lines_.insert(lines_.begin() + cursorLine_ + 1, tail);
      cursorLine_++;
      cursorCol_ = 0;
      _clearSelection();
      break;
    }
    case Key::Backspace:
      if (_hasSelection()) { _deleteSelection(); break; }
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
    case Key::Delete:
      if (_hasSelection()) { _deleteSelection(); break; }
      if (cursorCol_ < (int)lines_[cursorLine_].size())
        lines_[cursorLine_].erase(cursorCol_, 1);
      else if (cursorLine_ < (int)lines_.size() - 1) {
        lines_[cursorLine_] += lines_[cursorLine_ + 1];
        lines_.erase(lines_.begin() + cursorLine_ + 1);
      }
      _clearSelection();
      break;
    case Key::Left:
      if (!shift && _hasSelection()) {
        auto [s, e] = _normalizedSelection(); cursorLine_ = s.line; cursorCol_ = s.col; _clearSelection();
      } else if (cursorCol_ > 0) {
        cursorCol_--;
      } else if (cursorLine_ > 0) {
        cursorLine_--;
        cursorCol_ = (int)lines_[cursorLine_].size();
      }
      if (!shift) _clearSelection();
      break;
    case Key::Right:
      if (!shift && _hasSelection()) {
        auto [s, e] = _normalizedSelection(); cursorLine_ = e.line; cursorCol_ = e.col; _clearSelection();
      } else if (cursorCol_ < (int)lines_[cursorLine_].size()) {
        cursorCol_++;
      } else if (cursorLine_ < (int)lines_.size() - 1) {
        cursorLine_++; cursorCol_ = 0;
      }
      if (!shift) _clearSelection();
      break;
    case Key::Up:
      if (cursorLine_ > 0) { cursorLine_--; cursorCol_ = std::min(cursorCol_, (int)lines_[cursorLine_].size()); }
      if (!shift) _clearSelection();
      break;
    case Key::Down:
      if (cursorLine_ < (int)lines_.size() - 1) { cursorLine_++; cursorCol_ = std::min(cursorCol_, (int)lines_[cursorLine_].size()); }
      if (!shift) _clearSelection();
      break;
    case Key::Home:
      cursorCol_ = 0;
      if (!shift) _clearSelection();
      break;
    case Key::End:
      cursorCol_ = (int)lines_[cursorLine_].size();
      if (!shift) _clearSelection();
      break;
    case Key::PageUp: {
      int page = std::max(1, sbV_.viewportMain / std::max(1, lineH_));
      cursorLine_ = std::max(0, cursorLine_ - page);
      cursorCol_  = std::min(cursorCol_, (int)lines_[cursorLine_].size());
      if (!shift) _clearSelection();
      break;
    }
    case Key::PageDown: {
      int page = std::max(1, sbV_.viewportMain / std::max(1, lineH_));
      cursorLine_ = std::min((int)lines_.size() - 1, cursorLine_ + page);
      cursorCol_  = std::min(cursorCol_, (int)lines_[cursorLine_].size());
      if (!shift) _clearSelection();
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
      if (i > 0) out += '\n';
      out += lines_[i];
    }
    return out;
  }

  void setValue(const std::string &v) {
    if (selfUpdating_) return;

    lines_.clear();
    std::istringstream ss(v);
    std::string line;
    while (std::getline(ss, line)) lines_.push_back(line);
    if (lines_.empty()) lines_.push_back("");

    cursorLine_ = std::min(cursorLine_, (int)lines_.size() - 1);
    cursorCol_  = std::min(cursorCol_,  (int)lines_[cursorLine_].size());
    selAnchorLine_ = cursorLine_;
    selAnchorCol_  = cursorCol_;

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
        }, false);
    boundState_ = &state;
    return self_();
  }
  std::shared_ptr<TextAreaWidget> setPlaceholder(const std::string &ph) {
    placeholder = ph; return self_();
  }
  std::shared_ptr<TextAreaWidget> setLineNumbers(bool v) {
    showLineNumbers = v; gutterW_cached_ = -1; markNeedsLayout(); return self_();
  }
  std::shared_ptr<TextAreaWidget> setWordWrap(bool v) {
    wordWrap = v; markNeedsLayout(); return self_();
  }
  std::shared_ptr<TextAreaWidget> setTabSpaces(int n) {
    tabSpaces = n; return self_();
  }
  std::shared_ptr<TextAreaWidget> setMaxLength(int n) {
    maxLength = n; return self_();
  }
  std::shared_ptr<TextAreaWidget> setFontSize(int s) {
    fontSize = s; markNeedsLayout(); return self_();
  }
  std::shared_ptr<TextAreaWidget> setWidth(int w) {
    width = w; autoWidth = false; markNeedsLayout(); return self_();
  }
  std::shared_ptr<TextAreaWidget> setHeight(int h) {
    height = h; autoHeight = false; markNeedsLayout(); return self_();
  }
  std::shared_ptr<TextAreaWidget> setFlex(int f) {
    flex = f; return self_();
  }
  std::shared_ptr<TextAreaWidget> setScrollbarSize(int s) {
    sbV_.size = sbH_.size = s; return self_();
  }
  std::shared_ptr<TextAreaWidget> setScrollbarColor(Color c) {
    sbV_.colorNormal = sbH_.colorNormal = c; return self_();
  }
  std::shared_ptr<TextAreaWidget> setScrollbarHoverColor(Color c) {
    sbV_.colorHover = sbH_.colorHover = c; return self_();
  }
  std::shared_ptr<TextAreaWidget> setScrollbarTrackColor(Color c) {
    sbV_.colorTrack = sbH_.colorTrack = c; return self_();
  }

private:
  ScrollbarState sbV_; // vertical
  ScrollbarState sbH_; // horizontal

  std::vector<std::string> lines_ = {""};
  int     cursorLine_ = 0, cursorCol_ = 0;
  bool    cursorVisible_   = false;
  TimerID cursorTimerId_   = 0;
  mutable int lineH_       = 20;
  mutable int gutterW_cached_ = -1;

  int  selAnchorLine_ = 0, selAnchorCol_ = 0;
  bool mouseSelecting_ = false;

  State<std::string> *boundState_  = nullptr;
  bool                selfUpdating_ = false;

  std::shared_ptr<TextAreaWidget> self_() {
    return std::static_pointer_cast<TextAreaWidget>(shared_from_this());
  }

  int _totalChars() const {
    int n = 0;
    for (auto &l : lines_) n += (int)l.size();
    return n + (int)lines_.size() - 1;
  }

  int _gutterWidth(GraphicsContext &ctx, FontCache &fc) const {
    if (!showLineNumbers) { gutterW_cached_ = 0; return 0; }
    int          digits = (int)std::to_string(lines_.size()).size();
    std::wstring probe(digits, L'9');
    NativeFont   nf    = fc.getFont(fontSize - 1, FontWeight::Normal);
    int tw = 0, th = 0;
    Painter(ctx).measureText(probe, nf, tw, th);
    gutterW_cached_ = tw + 16;
    return gutterW_cached_;
  }

  int _gutterWidth_cached() const {
    return gutterW_cached_ < 0 ? 0 : gutterW_cached_;
  }

  void _updateScrollContent() {
    int totalLines = (int)lines_.size();
    sbV_.contentMain = totalLines * lineH_;
    sbV_.setScrollable(sbV_.contentMain > sbV_.viewportMain);

    if (!wordWrap) {
      int maxW = 0;
      for (auto &l : lines_)
        maxW = std::max(maxW, (int)l.size() * (fontSize / 2 + 1));
      sbH_.contentMain = maxW;
      sbH_.setScrollable(sbH_.contentMain > sbH_.viewportMain);
    } else {
      sbH_.contentMain = 0;
      sbH_.setScrollable(false);
    }

    sbV_.clamp(); sbV_.updateThumb();
    sbH_.clamp(); sbH_.updateThumb();
  }

  void _scrollToCursor() {
    int curTop = cursorLine_ * lineH_;
    int curBot = curTop + lineH_;
    int vp     = sbV_.viewportMain;
    if (curTop < sbV_.scrollOffset)      sbV_.scrollOffset = curTop;
    else if (curBot > sbV_.scrollOffset + vp) sbV_.scrollOffset = curBot - vp;
    sbV_.clamp(); sbV_.updateThumb();

    int curX = cursorCol_ * (fontSize / 2 + 1);
    int hp   = sbH_.viewportMain;
    if (curX < sbH_.scrollOffset)             sbH_.scrollOffset = std::max(0, curX - 10);
    else if (curX > sbH_.scrollOffset + hp - 10) sbH_.scrollOffset = curX - hp + 20;
    sbH_.clamp(); sbH_.updateThumb();
  }

  int _textWidth(GraphicsContext &ctx, const std::string &s, int from, int to) const {
    if (from >= to || s.empty()) return 0;
    to = std::min(to, (int)s.size());
    std::wstring ws = toWideString(s.c_str() + from, to - from);
    NativeFont font =
        const_cast<FontCache *>(&FluxUI::getCurrentInstance()->getFontCache())
            ->getFont(fontSize, fontWeight);
    int tw = 0, th = 0;
    Painter(ctx).measureText(ws, font, tw, th);
    return tw;
  }

  std::pair<int, int> _posFromMouse(int mx, int my) const {
    int gutterW = _gutterWidth_cached();
    int cLeft   = x + paddingLeft + gutterW;
    int cTop    = y + paddingTop;

    int line = (my - cTop + sbV_.scrollOffset) / std::max(1, lineH_);
    line = std::max(0, std::min((int)lines_.size() - 1, line));

    int relX = mx - cLeft + sbH_.scrollOffset;
    int col  = 0;

    if (!lines_[line].empty() && relX > 0) {
      auto *ui = FluxUI::getCurrentInstance();
      if (ui) {
        MeasureContext mc = ui->getMeasureContext();
        NativeFont     hf = ui->getFontCache().getFont(fontSize, fontWeight);
        int best = 0, bestDist = abs(relX);
        for (int i = 1; i <= (int)lines_[line].size(); i++) {
          std::wstring ws = toWideString(lines_[line].c_str(), i);
          int tw = 0, th = 0;
          Painter(mc.ctx).measureText(ws, hf, tw, th);
          int d = abs(tw - relX);
          if (d < bestDist) { bestDist = d; best = i; }
        }
        col = best;
      }
    }
    return {line, col};
  }

  // ── Selection ─────────────────────────────────────────────────────────────
  struct CursorPos { int line, col; };

  bool _hasSelection() const {
    return cursorLine_ != selAnchorLine_ || cursorCol_ != selAnchorCol_;
  }
  void _clearSelection() {
    selAnchorLine_ = cursorLine_;
    selAnchorCol_  = cursorCol_;
  }
  std::pair<CursorPos, CursorPos> _normalizedSelection() const {
    CursorPos a{selAnchorLine_, selAnchorCol_};
    CursorPos b{cursorLine_,    cursorCol_};
    if (a.line > b.line || (a.line == b.line && a.col > b.col)) std::swap(a, b);
    return {a, b};
  }
  void _selectAll() {
    selAnchorLine_ = 0;
    selAnchorCol_  = 0;
    cursorLine_    = (int)lines_.size() - 1;
    cursorCol_     = (int)lines_[cursorLine_].size();
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
    cursorCol_  = s.col;
    _clearSelection();
  }

  // ── Clipboard ─────────────────────────────────────────────────────────────
  std::string _selectedText() const {
    if (!_hasSelection()) return "";
    auto [s, e] = _normalizedSelection();
    if (s.line == e.line) return lines_[s.line].substr(s.col, e.col - s.col);
    std::string out = lines_[s.line].substr(s.col) + "\n";
    for (int li = s.line + 1; li < e.line; li++) out += lines_[li] + "\n";
    out += lines_[e.line].substr(0, e.col);
    return out;
  }
  void _copyToClipboard() const {
    auto *ui = FluxUI::getCurrentInstance();
    if (!ui) return;
    std::string sel = _hasSelection() ? _selectedText() : getValue();
    if (!sel.empty()) ui->setClipboardText(sel);
  }
  void _cutToClipboard() {
    _copyToClipboard();
    if (_hasSelection()) {
      _deleteSelection();
    } else {
      lines_      = {""};
      cursorLine_ = cursorCol_ = 0;
      _clearSelection();
    }
  }
  void _pasteFromClipboard() {
    auto *ui = FluxUI::getCurrentInstance();
    if (!ui) return;
    std::string text = ui->getClipboardText();
    if (_hasSelection()) _deleteSelection();
    for (char ch : text) {
      if (ch == '\r') continue;
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
    if (!boundState_) return;
    selfUpdating_ = true;
    boundState_->set(getValue());
    selfUpdating_ = false;
  }

  void _repaint() {
    needsPaint = true;
    auto *ui   = FluxUI::getCurrentInstance();
    if (ui) ui->invalidateWidget(x, y, width, height);
  }
};

using TextAreaWidgetPtr = std::shared_ptr<TextAreaWidget>;

inline TextAreaWidgetPtr TextArea(const std::string &placeholder = "") {
  auto w = std::make_shared<TextAreaWidget>();
  if (!placeholder.empty()) w->setPlaceholder(placeholder);
  return w;
}

// ============================================================================
// NUMBER INPUT / SPIN BOX WIDGET
// ============================================================================

class NumberInputWidget : public Widget {
public:
  double value       = 0.0;
  double minValue    = 0.0;
  double maxValue    = 100.0;
  double step        = 1.0;
  int    decimalPlaces = 0;

  std::string prefix;
  std::string suffix;

  Color focusedBorderColor   = Color::fromRGB(33,  150, 243);
  Color unfocusedBorderColor = Color::fromRGB(180, 180, 180);
  Color buttonBgColor        = Color::fromRGB(245, 246, 248);
  Color buttonHoverColor     = Color::fromRGB(225, 235, 248);
  Color buttonArrowColor     = Color::fromRGB(80,  80,  80);
  Color inputTextColor       = Color::fromRGB(30,  30,  30);
  Color disabledColor        = Color::fromRGB(180, 180, 180);

  std::function<void(double)> onValueChanged;

  NumberInputWidget() {
    isFocusable     = true;
    hasBorder       = true;
    hasBackground   = true;
    backgroundColor = Color::fromRGB(255, 255, 255);
    borderColor     = unfocusedBorderColor;
    borderWidth     = 1;
    borderRadius    = 4;
    paddingLeft     = 10;
    paddingRight    = 28 + 4;
    paddingTop      = paddingBottom = 8;
    height          = 36;
    autoHeight      = false;
    width           = 120;
    autoWidth       = false;
  }

  // ── Layout ────────────────────────────────────────────────────────────────
  void computeLayout(GraphicsContext & /*ctx*/,
                     const BoxConstraints &constraints, FontCache &) override {
    if (autoWidth) width = constraints.clampWidth(width);
    applyConstraints();
    needsLayout = false;
    if (!editing_) editBuffer_ = _formatValue(value);
  }

  // ── Render ────────────────────────────────────────────────────────────────
  void render(GraphicsContext &ctx, FontCache &fontCache) override {
    borderColor = isFocused ? focusedBorderColor : unfocusedBorderColor;
    drawRoundedRectangle(ctx);

    Painter painter(ctx);
    int btnW = 24;
    int btnX = x + width - btnW - 1;

    painter.fillRect(btnX, y + 1, btnW, height - 2, buttonBgColor);
    painter.drawLine(btnX,         y + 1,         btnX,         y + height - 1, unfocusedBorderColor, 1);
    painter.drawLine(btnX,         y + height / 2, btnX + btnW - 1, y + height / 2, unfocusedBorderColor, 1);

    _drawArrow(ctx, btnX, y + 1,            btnW, height / 2 - 1,          true,  upHovered_);
    _drawArrow(ctx, btnX, y + height / 2,   btnW, height - height / 2 - 1, false, downHovered_);

    NativeFont  hf      = fontCache.getFont(fontSize, fontWeight);
    std::string display = editing_ ? editBuffer_ : _formatValue(value);
    if (!prefix.empty()) display = prefix + display;
    if (!suffix.empty() && !editing_) display += suffix;

    std::wstring wdisplay = toWideString(display);

    painter.pushClipRect(x + 1, y + 1, btnX - x - 2, height - 2);
    painter.drawText(wdisplay, x + paddingLeft, y + paddingTop,
                     btnX - x - paddingLeft - 2,
                     height - paddingTop - paddingBottom,
                     hf, inputTextColor,
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    if (isFocused && editing_ && cursorVisible_) {
      std::wstring wbefore = wdisplay.substr(0, (int)prefix.size() + editCursorPos_);
      int tw = 0, th = 0;
      painter.measureText(wbefore, hf, tw, th);
      int curX = x + paddingLeft + tw;
      painter.drawLine(curX, y + paddingTop + 2,
                       curX, y + height - paddingBottom - 2, inputTextColor, 1);
    }

    painter.popClipRect();
    needsPaint = false;
  }

  // ── Focus ─────────────────────────────────────────────────────────────────

  bool handleFocus(bool focused) override {
    isFocused = focused;
    auto *ui  = FluxUI::getCurrentInstance();

    if (focused) {
      editing_       = true;
      editBuffer_    = _formatValue(value);
      editCursorPos_ = (int)editBuffer_.size();
      cursorVisible_ = true;
      cursorTimerId_ = ui->setInterval(530, [this]() {
        cursorVisible_ = !cursorVisible_;
        markNeedsPaint();
      });
      VirtualKeyboard::notifyFocusGained(this);
    } else {
      _commitEdit();
      editing_       = false;
      cursorVisible_ = false;
      if (cursorTimerId_) {
        ui->clearInterval(cursorTimerId_);
        cursorTimerId_ = 0;
      }
      VirtualKeyboard::notifyFocusLost();
    }

    markNeedsPaint();
    return true;
  }

  // ── Mouse ─────────────────────────────────────────────────────────────────
  bool handleMouseDown(int mx, int my) override {
    if (mx < x || mx >= x + width || my < y || my >= y + height) return false;

    int btnX = x + width - 24 - 1;
    if (mx >= btnX) {
      int midY = y + height / 2;
      if (my < midY) _increment(); else _decrement();
      _commitImmediate();
      return true;
    }
    if (editing_) {
      editCursorPos_ = std::min((int)editBuffer_.size(),
                           std::max(0, mx - x - paddingLeft) / std::max(1, fontSize / 2));
    }
    return true;
  }

  bool handleMouseMove(int mx, int my) override {
    int  btnX  = x + width - 24 - 1;
    int  midY  = y + height / 2;
    bool overBtn = mx >= btnX && mx < x + width && my >= y && my < y + height;
    bool newUp   = overBtn && my < midY;
    bool newDown = overBtn && my >= midY;
    if (newUp != upHovered_ || newDown != downHovered_) {
      upHovered_   = newUp;
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
    if (delta > 0) _increment(); else _decrement();
    _commitImmediate();
    return true;
  }

  // ── Keyboard ──────────────────────────────────────────────────────────────
  bool handleChar(wchar_t ch) override {
    if (!editing_) return false;
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
    case Key::Up:       _increment(); _commitImmediate(); return true;
    case Key::Down:     _decrement(); _commitImmediate(); return true;
    case Key::PageUp:   value = std::min(maxValue, value + step * 10); _commitImmediate(); return true;
    case Key::PageDown: value = std::max(minValue, value - step * 10); _commitImmediate(); return true;
    case Key::Home:     value = minValue; _commitImmediate(); return true;
    case Key::End:      value = maxValue; _commitImmediate(); return true;
    case Key::Return:   _commitEdit(); return true;
    case Key::Escape:
      editing_    = false;
      editBuffer_ = _formatValue(value);
      markNeedsPaint();
      return true;
    case Key::Backspace:
      if (editing_ && editCursorPos_ > 0) {
        editBuffer_.erase(--editCursorPos_, 1);
        cursorVisible_ = true;
        markNeedsPaint();
      }
      return true;
    case Key::Delete:
      if (editing_ && editCursorPos_ < (int)editBuffer_.size()) {
        editBuffer_.erase(editCursorPos_, 1);
        cursorVisible_ = true;
        markNeedsPaint();
      }
      return true;
    case Key::Left:
      if (editCursorPos_ > 0) { editCursorPos_--; cursorVisible_ = true; markNeedsPaint(); }
      return true;
    case Key::Right:
      if (editCursorPos_ < (int)editBuffer_.size()) { editCursorPos_++; cursorVisible_ = true; markNeedsPaint(); }
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
        }, false);
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
        }, false);
    boundIntState_ = &state;
    return std::static_pointer_cast<NumberInputWidget>(shared_from_this());
  }

  std::shared_ptr<NumberInputWidget> setMin(double v)    { minValue = v; return self_(); }
  std::shared_ptr<NumberInputWidget> setMax(double v)    { maxValue = v; return self_(); }
  std::shared_ptr<NumberInputWidget> setStep(double v)   { step = v; return self_(); }
  std::shared_ptr<NumberInputWidget> setDecimalPlaces(int n) { decimalPlaces = n; return self_(); }
  std::shared_ptr<NumberInputWidget> setPrefix(const std::string &p) { prefix = p; return self_(); }
  std::shared_ptr<NumberInputWidget> setSuffix(const std::string &s) { suffix = s; return self_(); }
  std::shared_ptr<NumberInputWidget> setOnValueChanged(std::function<void(double)> fn) {
    onValueChanged = std::move(fn); return self_();
  }
  std::shared_ptr<NumberInputWidget> setWidth(int w) {
    width = w; autoWidth = false; return self_();
  }
  std::shared_ptr<NumberInputWidget> setFlex(int f) {
    flex = f; return self_();
  }

private:
  std::string editBuffer_;
  int         editCursorPos_ = 0;
  bool        editing_       = false;
  bool        cursorVisible_ = false;
  TimerID     cursorTimerId_ = 0;
  bool        upHovered_     = false;
  bool        downHovered_   = false;

  State<double> *boundDoubleState_ = nullptr;
  State<int>    *boundIntState_    = nullptr;

  std::shared_ptr<NumberInputWidget> self_() {
    return std::static_pointer_cast<NumberInputWidget>(shared_from_this());
  }

  double _snap(double v) const {
    if (step <= 0) return v;
    return std::round(v / step) * step;
  }
  double _clamp(double v) const {
    return std::max(minValue, std::min(maxValue, v));
  }
  std::string _formatValue(double v) const {
    if (decimalPlaces <= 0) return std::to_string((long long)std::round(v));
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(decimalPlaces) << v;
    return oss.str();
  }

  void _increment() { value = _clamp(_snap(value + step)); editBuffer_ = _formatValue(value); markNeedsPaint(); }
  void _decrement() { value = _clamp(_snap(value - step)); editBuffer_ = _formatValue(value); markNeedsPaint(); }

  void _commitEdit() {
    if (!editBuffer_.empty()) {
      try { value = _clamp(_snap(std::stod(editBuffer_))); } catch (...) {}
    }
    editBuffer_    = _formatValue(value);
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
    if (onValueChanged)   onValueChanged(value);
    if (boundDoubleState_) boundDoubleState_->set(value);
    if (boundIntState_)    boundIntState_->set((int)std::round(value));
  }

  void _drawArrow(GraphicsContext &ctx, int bx, int by, int bw, int bh,
                  bool up, bool hovered) const {
    Painter painter(ctx);
    if (hovered) painter.fillRect(bx + 1, by + 1, bw - 2, bh - 2, buttonHoverColor);

    int cx = bx + bw / 2;
    int cy = by + bh / 2;
    int hs = 4;

    if (up) {
      painter.drawLine(cx - hs, cy + hs / 2, cx,       cy - hs / 2, buttonArrowColor, 1);
      painter.drawLine(cx,       cy - hs / 2, cx + hs, cy + hs / 2, buttonArrowColor, 1);
    } else {
      painter.drawLine(cx - hs, cy - hs / 2, cx,       cy + hs / 2, buttonArrowColor, 1);
      painter.drawLine(cx,       cy + hs / 2, cx + hs, cy - hs / 2, buttonArrowColor, 1);
    }
  }
};

using NumberInputWidgetPtr = std::shared_ptr<NumberInputWidget>;

inline NumberInputWidgetPtr
NumberInput(double minVal = 0.0, double maxVal = 100.0, double step = 1.0) {
  auto w     = std::make_shared<NumberInputWidget>();
  w->minValue = minVal;
  w->maxValue = maxVal;
  w->step     = step;
  return w;
}

inline NumberInputWidgetPtr SpinBox(double minVal = 0.0, double maxVal = 100.0,
                                    double step = 1.0) {
  return NumberInput(minVal, maxVal, step);
}

#endif // FLUX_INPUT_EXTENDED_HPP