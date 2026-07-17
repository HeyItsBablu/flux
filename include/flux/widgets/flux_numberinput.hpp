#ifndef FLUX_NUMBER_INPUT_HPP
#define FLUX_NUMBER_INPUT_HPP


#include "flux_keyboard.hpp"  

#include "../flux_core.hpp"
#include "../flux_state.hpp"

#if (defined(__EMSCRIPTEN__) && defined(FLUX_WEB_RENDERER_DOM)) || defined(FLUX_SSR)
#include "../flux_dom_adapter.hpp"
#endif

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>


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

#if (defined(__EMSCRIPTEN__) && defined(FLUX_WEB_RENDERER_DOM)) || defined(FLUX_SSR)
    if (IDomAdapter *adapter = getActiveDomAdapter()) {
        _renderDom(adapter);
        needsPaint = false;
        return;
    }
#endif
    borderColor = isFocused ? focusedBorderColor : unfocusedBorderColor;
    drawRoundedRectangle(ctx);

    Painter painter(ctx, this);
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

#if (defined(__EMSCRIPTEN__) && defined(FLUX_WEB_RENDERER_DOM)) || defined(FLUX_SSR)
  // Real <input> owns its own native caret/blink — no hand-rolled cursor
  // timer needed on this backend, and no VirtualKeyboard::notify*()
  // either (the real element triggers the OS's own on-screen keyboard
  // natively). _commitEdit() still runs on blur so value gets parsed/
  // clamped/snapped/reformatted the same way it does on every other
  // backend — see onDomFocusChanged below for how this gets called.
  bool handleFocus(bool focused) override {
    isFocused = focused;
    if (focused) {
      editing_ = true;
      if (editBuffer_.empty())
        editBuffer_ = _formatValue(value);
    } else {
      _commitEdit();
      editing_ = false;
    }
    markNeedsPaint();
    return true;
  }
#else
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
#endif

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

#if (defined(__EMSCRIPTEN__) && defined(FLUX_WEB_RENDERER_DOM)) || defined(FLUX_SSR)
  // Real <input> owns typing; the up/down buttons stay ordinary painted
  // divs since their click handling already flows through the existing
  // C++ handleMouseDown (same "dedicated real element only where genuine
  // native editing is needed, everything else painted" split
  // TextInputWidget established) — hit-testing for them is unchanged by
  // any of this.

  void onDomInputChanged(const std::string &val) override {
    // Track the raw typed text only — do NOT clamp/snap/reformat here.
    // Doing so would fight the user mid-keystroke (e.g. silently
    // truncating "1." back to "1", or clamping "5" to minValue while
    // they're still in the middle of typing "50"). This matches the
    // widget's own canvas semantics: handleChar() only ever edits
    // editBuffer_, and committing (parse/clamp/snap/reformat) happens on
    // blur/Enter/Escape, never per character.
    editBuffer_    = val;
    editCursorPos_ = (int)editBuffer_.size();
  }

  void onDomFocusChanged(bool focused) override {
    // Route through FluxUI's own focus tracking, same as
    // TextInputWidget::onDomFocusChanged — this is what causes
    // handleFocus(false) (above) to run and actually commit the value on
    // blur, and keeps FluxUI::getFocusedWidget() correct for keyboard
    // routing regardless of which side (native blur vs. a programmatic
    // setFocus elsewhere) initiated the change.
    if (auto *ui = FluxUI::getCurrentInstance())
      ui->setFocus(focused ? this : nullptr);
    else
      isFocused = focused;
  }

  void _renderDom(IDomAdapter *adapter) {
    char buf[24];
    auto px = [&](int v) { snprintf(buf, sizeof(buf), "%dpx", v); return std::string(buf); };
    char colbuf[32];
    auto rgb = [&](Color c) { snprintf(colbuf, sizeof(colbuf), "rgb(%d,%d,%d)", c.r, c.g, c.b); return std::string(colbuf); };

    int btnW = 24;
    int btnX = x + width - btnW - 1;

    // ── Outer box — background + focus-aware border. Purely visual; the
    // real <input> and the two button divs are separate slots layered
    // on top of it, same "one owner, several slotted layers" pattern as
    // ToggleWidget/SliderWidget/RadioButtonWidget above.
    DomNodeHandle box = fluxDomEnsureNode(this, "div", "box");
    fluxDomApplyRect(this, x, y, width, height, "box");
    adapter->setStyle(box, "background-color", rgb(backgroundColor));
    adapter->setStyle(box, "border", "1px solid " +
                       rgb(isFocused ? focusedBorderColor : unfocusedBorderColor));
    adapter->setStyle(box, "box-sizing", "border-box");
    adapter->setStyle(box, "border-radius", px(borderRadius));
    adapter->setStyle(box, "pointer-events", "none");

    // ── Up button ───────────────────────────────────────────────────────
    DomNodeHandle up = fluxDomEnsureNode(this, "div", "btnUp");
    fluxDomApplyRect(this, btnX, y + 1, btnW, height / 2 - 1, "btnUp");
    adapter->setStyle(up, "background-color", rgb(upHovered_ ? buttonHoverColor : buttonBgColor));
    adapter->setStyle(up, "border-left",   "1px solid " + rgb(unfocusedBorderColor));
    adapter->setStyle(up, "border-bottom", "1px solid " + rgb(unfocusedBorderColor));
    adapter->setStyle(up, "box-sizing", "border-box");
    adapter->setStyle(up, "display", "flex");
    adapter->setStyle(up, "align-items", "center");
    adapter->setStyle(up, "justify-content", "center");
    adapter->setStyle(up, "color", rgb(buttonArrowColor));
    adapter->setStyle(up, "font-size", "9px");
    adapter->setStyle(up, "line-height", "1");
    // Hit-testing stays entirely in C++ (handleMouseDown computes this
    // same btnX/midY split directly against x/y/width/height) — these
    // nodes are purely visual, same reasoning as every other
    // non-native-input widget on this backend.
    adapter->setStyle(up, "pointer-events", "none");
    adapter->setText(up, "\xE2\x96\xB2"); // ▲, approximates the two
    // hand-drawn chevron lines the canvas backend strokes — same
    // cross-backend visual tolerance already accepted elsewhere in this
    // codebase (see CheckBoxWidget's checkmark).

    // ── Down button ─────────────────────────────────────────────────────
    DomNodeHandle down = fluxDomEnsureNode(this, "div", "btnDown");
    int downH = height - height / 2 - 1;
    fluxDomApplyRect(this, btnX, y + height / 2, btnW, downH, "btnDown");
    adapter->setStyle(down, "background-color", rgb(downHovered_ ? buttonHoverColor : buttonBgColor));
    adapter->setStyle(down, "border-left", "1px solid " + rgb(unfocusedBorderColor));
    adapter->setStyle(down, "box-sizing", "border-box");
    adapter->setStyle(down, "display", "flex");
    adapter->setStyle(down, "align-items", "center");
    adapter->setStyle(down, "justify-content", "center");
    adapter->setStyle(down, "color", rgb(buttonArrowColor));
    adapter->setStyle(down, "font-size", "9px");
    adapter->setStyle(down, "line-height", "1");
    adapter->setStyle(down, "pointer-events", "none");
    adapter->setText(down, "\xE2\x96\xBC"); // ▼

    // ── Prefix / suffix — small non-editable text slots either side of
    // the input. Widths are a rough character-count estimate (no text
    // measurement is available here — _renderDom gets no GraphicsContext,
    // same constraint every other widget's _renderDom already lives
    // with) rather than an exact measured width; good enough for short
    // symbols like "$" or "kg", the only realistic use of this field.
    int prefixW = prefix.empty() ? 0 : (int)(prefix.size() * fontSize * 0.62) + 2;
    int suffixW = (suffix.empty() || editing_) ? 0 : (int)(suffix.size() * fontSize * 0.62) + 2;

    if (!prefix.empty()) {
      DomNodeHandle pfx = fluxDomEnsureNode(this, "div", "prefix");
      fluxDomApplyRect(this, x + paddingLeft, y + 1, prefixW, height - 2, "prefix");
      adapter->setStyle(pfx, "display", "flex");
      adapter->setStyle(pfx, "align-items", "center");
      adapter->setStyle(pfx, "white-space", "nowrap");
      adapter->setStyle(pfx, "font-size", px(fontSize));
      adapter->setStyle(pfx, "color", rgb(inputTextColor));
      adapter->setStyle(pfx, "pointer-events", "none");
      adapter->setText(pfx, prefix);
    }
    if (!suffix.empty() && !editing_) {
      DomNodeHandle sfx = fluxDomEnsureNode(this, "div", "suffix");
      fluxDomApplyRect(this, btnX - suffixW - 4, y + 1, suffixW, height - 2, "suffix");
      adapter->setStyle(sfx, "display", "flex");
      adapter->setStyle(sfx, "align-items", "center");
      adapter->setStyle(sfx, "white-space", "nowrap");
      adapter->setStyle(sfx, "font-size", px(fontSize));
      adapter->setStyle(sfx, "color", rgb(inputTextColor));
      adapter->setStyle(sfx, "pointer-events", "none");
      adapter->setText(sfx, suffix);
    }

    // ── Text input — a real <input>, same "dedicated native element"
    // pattern TextInputWidget uses, so typing/caret/selection are all
    // genuinely native instead of hand-rolled. Deliberate exception to
    // "capture div owns all input" (see flux_window_dom.cpp), same one
    // TextInputWidget already documents — it needs real pointer events
    // and a real native focus target, so it stacks ABOVE the capture div
    // rather than beneath it.
    DomNodeHandle input = fluxDomEnsureNode(this, "input", "input");
    int inputX = x + paddingLeft + prefixW;
    int inputW = btnX - inputX - 2 - suffixW;
    fluxDomApplyRect(this, inputX, y + 1, std::max(0, inputW), height - 2, "input");
    adapter->setStyle(input, "box-sizing", "border-box");
    adapter->setStyle(input, "padding", "0 2px");
    adapter->setStyle(input, "font-size", px(fontSize));
    adapter->setStyle(input, "border", "none");
    adapter->setStyle(input, "outline", "none");
    adapter->setStyle(input, "background-color", "transparent");
    adapter->setStyle(input, "color", rgb(inputTextColor));
    // "text" + inputmode, not type="number" — a native number input
    // draws its OWN spinner, which would visually double up with (and
    // fight the hit-testing of) the custom up/down divs drawn above.
    adapter->setAttr(input, "inputmode",
                      (decimalPlaces > 0 || minValue < 0) ? "decimal" : "numeric");
    adapter->setStyle(input, "pointer-events", "auto");
    adapter->setStyle(input, "z-index", "2");

    std::string display = editing_ ? editBuffer_ : _formatValue(value);
    adapter->setInputValue(input, display);
    adapter->bindInputEvents(input, this);
  }
#endif

  void _drawArrow(GraphicsContext &ctx, int bx, int by, int bw, int bh,
                  bool up, bool hovered)  {
    Painter painter(ctx, this);
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

#endif // FLUX_NUMBER_INPUT_HPP