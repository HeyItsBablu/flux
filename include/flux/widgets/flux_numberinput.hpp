#ifndef FLUX_NUMBER_INPUT_HPP
#define FLUX_NUMBER_INPUT_HPP

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

#endif // FLUX_NUMBER_INPUT_HPP