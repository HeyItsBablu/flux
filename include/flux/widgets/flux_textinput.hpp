#ifndef FLUX_TEXTINPUT_HPP
#define FLUX_TEXTINPUT_HPP

#include "flux/flux_core.hpp"
#include "flux/flux_state.hpp"

#if (defined(__EMSCRIPTEN__) && defined(FLUX_WEB_RENDERER_DOM)) ||             \
    defined(FLUX_SSR)
#include "../flux_dom_adapter.hpp"
#endif

#include "flux_keyboard.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <regex>
#include <string>

template <typename T> class State;

class Widget;
using WidgetPtr = std::shared_ptr<Widget>;

// ============================================================================
// InputType
//
// Controls three independent things per type:
//   - character filtering while typing   (Number only — see acceptsChar)
//   - masked rendering                   (Password only — see displaySubstring)
//   - the DOM backend's native <input type="..."> + keyboard hint
// Validation (see below) is layered on top and applies to any type.
// ============================================================================
enum class InputType {
  Text,
  Password,
  Number, // filters keystrokes to digits/one '.'/leading '-'; still a string,
          // not a numeric State<T> — use NumberInput/SpinBox for that.
  Email,
  Tel,
  Url,
  Search
};

// ============================================================================
// TextInputWidget
// ============================================================================

class TextInputWidget : public Widget {
public:
  std::string inputValue;
  std::string placeholder;
  int cursorPos = 0;
  bool cursorVisible = true;
  TimerID cursorTimerId = 0;
  int scrollOffset = 0;

  InputType inputType = InputType::Text;
  wchar_t maskChar = L'\u2022'; // bullet — used to render Password values

  Color focusedBorderColor = Color::fromRGB(33, 150, 243);
  Color unfocusedBorderColor = Color::fromRGB(180, 180, 180);
  Color invalidBorderColor = Color::fromRGB(220, 53, 69);
  Color placeholderColor = Color::fromRGB(180, 180, 180);
  Color inputTextColor = Color::fromRGB(30, 30, 30);

  // ── Validation ──────────────────────────────────────────────────────
  // `validator`, if set, fully replaces the built-in per-type check.
  // Built-ins only fire for Email / Url / Number; every other type is
  // valid by default unless a custom validator is supplied. An empty
  // value is always treated as valid/neutral — we don't want a fresh,
  // untouched field to render red before the user has typed anything.
  std::function<bool(const std::string &)> validator;
  std::function<void(bool)> onValidationChanged;

  TextInputWidget() {
    isFocusable = true;
    hasBorder = true;
    hasBackground = true;
    backgroundColor = Color::fromRGB(255, 255, 255);
    borderColor = unfocusedBorderColor;
    borderWidth = 1;
    borderRadius = 4;
    paddingLeft = paddingRight = 10;
    paddingTop = paddingBottom = 8;
    height = 36;
    autoHeight = false;
  }

  bool isTextInput() const override { return true; }

  bool isValid() const { return valid_; }
  bool isTouched() const { return touched_; }

  // ------------------------------------------------------------------
  // Layout
  // ------------------------------------------------------------------

  void computeLayout(GraphicsContext &ctx, const BoxConstraints &constraints,
                     FontCache &fontCache) override {
    if (autoWidth) {
      if (constraints.maxWidth >= kUnbounded) {
        const std::string &sample = !inputValue.empty() ? inputValue
                                    : !placeholder.empty()
                                        ? placeholder
                                        : std::string("Type something...");
        NativeFont font = fontCache.getFont(fontSize, fontWeight);
        int tw = 0, th = 0;
        Painter(ctx, this).measureText(toWideString(sample), font, tw, th);
        width = std::max(150, tw + paddingLeft + paddingRight + 24);
      } else {
        width = constraints.maxWidth;
      }
    }
    applyConstraints();
    needsLayout = false;
  }

  // ------------------------------------------------------------------
  // Render
  // ------------------------------------------------------------------

  void render(GraphicsContext &ctx, FontCache &fontCache) override {

#if (defined(__EMSCRIPTEN__) && defined(FLUX_WEB_RENDERER_DOM)) ||             \
    defined(FLUX_SSR)
    if (IDomAdapter *adapter = getActiveDomAdapter()) {
      _renderDom(adapter);
      needsPaint = false;
      return;
    }
#endif
    borderColor = currentBorderColor();
    drawRoundedRectangle(ctx);

    Painter painter(ctx, this);

    int textX = x + paddingLeft;
    int clipW = width - paddingLeft - paddingRight;
    int clipH = height - paddingTop - paddingBottom;

    painter.pushClipRect(x + paddingLeft, y + paddingTop, clipW, clipH);

    NativeFont font = fontCache.getFont(fontSize, fontWeight);

    if (inputValue.empty() && !placeholder.empty()) {
      std::wstring wph = toWideString(placeholder);
      painter.drawText(wph, textX, y + paddingTop, clipW, clipH, font,
                       placeholderColor, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    } else {
      std::wstring winput = displaySubstring((int)inputValue.size());
      painter.drawText(winput, textX - scrollOffset, y + paddingTop,
                       clipW + scrollOffset, clipH, font, inputTextColor,
                       DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);
    }

    if (isFocused && cursorVisible) {
      int tw = 0, th = 0;
      if (cursorPos > 0) {
        std::wstring wpre = displaySubstring(cursorPos);
        painter.measureText(wpre, font, tw, th);
      }
      int cursorX = textX + tw - scrollOffset;
      painter.drawLine(cursorX, y + paddingTop + 2, cursorX,
                       y + height - paddingBottom - 2, inputTextColor, 1);
    }

    painter.popClipRect();
    needsPaint = false;
  }

  // ------------------------------------------------------------------
  // Focus / input events
  // ------------------------------------------------------------------

  bool handleFocus(bool focused) override {

#if (defined(__EMSCRIPTEN__) && defined(FLUX_WEB_RENDERER_DOM)) ||             \
    defined(FLUX_SSR)
    isFocused = focused;
    if (!focused) {
      touched_ = true;
      revalidate();
    }
    return true;
#else
    isFocused = focused;
    auto *ui = FluxUI::getCurrentInstance();

    if (focused) {
      cursorVisible = true;
      cursorTimerId = ui->setInterval(530, [this]() {
        cursorVisible = !cursorVisible;
        markNeedsPaint();
      });
      VirtualKeyboard::notifyFocusGained(this);
    } else {
      if (cursorTimerId) {
        ui->clearInterval(cursorTimerId);
        cursorTimerId = 0;
      }
      cursorVisible = false;
      VirtualKeyboard::notifyFocusLost();
      touched_ = true;
      revalidate();
    }

    markNeedsPaint();
    return true;
#endif
  }

  bool handleMouseDown(int mx, int my) override {
    if (mx >= x && mx < x + width && my >= y && my < y + height) {
      cursorPos = getCursorPosFromX(mx - x - paddingLeft + scrollOffset);
      return true;
    }
    return false;
  }

  bool handleChar(wchar_t ch) override {
    if (ch < 32)
      return false;
    if (!acceptsChar(ch, cursorPos))
      return false;
    inputValue.insert(cursorPos, std::string(1, (char)ch));
    cursorPos++;
    cursorVisible = true;
    touched_ = true;
    updateScroll();
    revalidate();
    notifyStateBinding();
    return true;
  }

  bool handleKeyDown(int keyCode) override {
    switch (keyCode) {
    case Key::Backspace:
      if (cursorPos > 0) {
        inputValue.erase(cursorPos - 1, 1);
        cursorPos--;
        cursorVisible = true;
        touched_ = true;
        updateScroll();
        revalidate();
        notifyStateBinding();
        return true;
      }
      break;
    case Key::Delete:
      if (cursorPos < (int)inputValue.size()) {
        inputValue.erase(cursorPos, 1);
        cursorVisible = true;
        touched_ = true;
        updateScroll();
        revalidate();
        notifyStateBinding();
        return true;
      }
      break;
    case Key::Left:
      if (cursorPos > 0) {
        cursorPos--;
        cursorVisible = true;
        updateScroll();
        return true;
      }
      break;
    case Key::Right:
      if (cursorPos < (int)inputValue.size()) {
        cursorPos++;
        cursorVisible = true;
        updateScroll();
        return true;
      }
      break;
    case Key::Home:
      cursorPos = 0;
      scrollOffset = 0;
      return true;
    case Key::End:
      cursorPos = (int)inputValue.size();
      updateScroll();
      return true;
    }
    return false;
  }

  // ------------------------------------------------------------------
  // Setters (chainable)
  // ------------------------------------------------------------------

  std::shared_ptr<TextInputWidget> setInputValue(State<std::string> &state) {
    inputValue = state.get();
    cursorPos = (int)inputValue.size();
    scrollOffset = 0;
    revalidate();
    state.bindProperty(
        shared_from_this(),
        [](Widget *w, const std::string &val) {
          auto *input = static_cast<TextInputWidget *>(w);
          input->inputValue = val;
          input->cursorPos = (int)val.size();
          input->revalidate();
        },
        false);
    boundStringState_ = &state;
    return std::static_pointer_cast<TextInputWidget>(shared_from_this());
  }

  std::shared_ptr<TextInputWidget> setPlaceholder(const std::string &ph) {
    placeholder = ph;
    return std::static_pointer_cast<TextInputWidget>(shared_from_this());
  }

  std::shared_ptr<TextInputWidget> setWidth(int w) {
    width = w;
    autoWidth = false;
    return std::static_pointer_cast<TextInputWidget>(shared_from_this());
  }

  std::shared_ptr<TextInputWidget> setType(InputType t) {
    inputType = t;
    cursorPos = std::min(cursorPos, (int)inputValue.size());
    revalidate();
    markNeedsPaint();
    return std::static_pointer_cast<TextInputWidget>(shared_from_this());
  }

  std::shared_ptr<TextInputWidget> setMaskChar(wchar_t c) {
    maskChar = c;
    markNeedsPaint();
    return std::static_pointer_cast<TextInputWidget>(shared_from_this());
  }

  std::shared_ptr<TextInputWidget> setInvalidBorderColor(Color c) {
    invalidBorderColor = c;
    markNeedsPaint();
    return std::static_pointer_cast<TextInputWidget>(shared_from_this());
  }

  // Custom validator overrides the built-in per-type check entirely.
  std::shared_ptr<TextInputWidget>
  setValidator(std::function<bool(const std::string &)> fn) {
    validator = std::move(fn);
    revalidate();
    return std::static_pointer_cast<TextInputWidget>(shared_from_this());
  }

  std::shared_ptr<TextInputWidget>
  setOnValidationChanged(std::function<void(bool)> fn) {
    onValidationChanged = std::move(fn);
    return std::static_pointer_cast<TextInputWidget>(shared_from_this());
  }

private:
  State<std::string> *boundStringState_ = nullptr;
  bool touched_ = false; // becomes true after first edit or on blur
  bool valid_ = true;    // last computed validity; empty value == valid

  // ── Character filtering ────────────────────────────────────────────
  // Only Number restricts keystrokes; every other type accepts anything
  // and relies on validation (below) to flag bad input instead.
  bool acceptsChar(wchar_t ch, int atPos) const {
    if (inputType != InputType::Number)
      return true;
    if (ch >= L'0' && ch <= L'9')
      return true;
    if (ch == L'-' && atPos == 0 && inputValue.find('-') == std::string::npos)
      return true;
    if (ch == L'.' && inputValue.find('.') == std::string::npos)
      return true;
    return false;
  }

  // ── Masked display ───────────────────────────────────────────────
  // Password renders `len` mask glyphs instead of the real characters;
  // every other type renders the real substring. Used everywhere text
  // width is measured (render, cursor placement, scroll) so the caret
  // always lines up with what's actually drawn.
  std::wstring displaySubstring(int len) const {
    len = std::clamp(len, 0, (int)inputValue.size());
    if (inputType == InputType::Password)
      return std::wstring((size_t)len, maskChar);
    return toWideString(inputValue.c_str(), len);
  }

  // ── Validation ────────────────────────────────────────────────────
  bool builtinValidate(const std::string &s) const {
    if (s.empty())
      return true; // neutral — don't flag an empty/untouched field

    switch (inputType) {
    case InputType::Email: {
      static const std::regex re(R"(^[^\s@]+@[^\s@]+\.[^\s@]+$)");
      return std::regex_match(s, re);
    }
    case InputType::Url: {
      static const std::regex re(R"(^[a-zA-Z][a-zA-Z\d+\-.]*://\S+$)");
      return std::regex_match(s, re);
    }
    case InputType::Number: {
      try {
        size_t pos = 0;
        std::stod(s, &pos);
        return pos == s.size();
      } catch (...) {
        return false;
      }
    }
    default:
      return true;
    }
  }

  void revalidate() {
    bool newValid =
        validator ? validator(inputValue) : builtinValidate(inputValue);
    if (newValid != valid_) {
      valid_ = newValid;
      if (onValidationChanged)
        onValidationChanged(valid_);
      markNeedsPaint();
    }
  }

  Color currentBorderColor() const {
    if (touched_ && !valid_)
      return invalidBorderColor;
    return isFocused ? focusedBorderColor : unfocusedBorderColor;
  }

  void notifyStateBinding() {
    if (boundStringState_)
      boundStringState_->set(inputValue);
  }

  int getCursorPosFromX(int pixelX) {
    if (inputValue.empty())
      return 0;
    auto *ui = FluxUI::getCurrentInstance();
    MeasureContext mc = ui->getMeasureContext();
    NativeFont font =
        ui->getFontCache().getFont(fontFamily, fontSize, fontWeight);
    int bestPos = 0, bestDist = std::abs(pixelX);
    for (int i = 1; i <= (int)inputValue.size(); i++) {
      std::wstring w = displaySubstring(i);
      int tw = 0, th = 0;
      Painter(mc.ctx, this).measureText(w, font, tw, th);
      int dist = std::abs(tw - pixelX);
      if (dist < bestDist) {
        bestDist = dist;
        bestPos = i;
      }
    }
    return bestPos;
  }

  void updateScroll() {
    if (inputValue.empty()) {
      scrollOffset = 0;
      return;
    }
    auto *ui = FluxUI::getCurrentInstance();
    MeasureContext mc = ui->getMeasureContext();
    NativeFont font =
        ui->getFontCache().getFont(fontFamily, fontSize, fontWeight);
    int tw = 0, th = 0;
    if (cursorPos > 0) {
      std::wstring wpre = displaySubstring(cursorPos);
      Painter(mc.ctx, this).measureText(wpre, font, tw, th);
    }
    int textAreaWidth = width - paddingLeft - paddingRight;
    int cursorX = tw - scrollOffset;
    if (cursorX < 10)
      scrollOffset = std::max(0, tw - 10);
    else if (cursorX > textAreaWidth - 10)
      scrollOffset = tw - textAreaWidth + 10;
  }

#if (defined(__EMSCRIPTEN__) && defined(FLUX_WEB_RENDERER_DOM)) ||             \
    defined(FLUX_SSR)
  // Number maps to type="text" + inputmode="numeric" rather than
  // type="number" — avoids the browser's spinner arrows and silent
  // scientific-notation coercion, while still prompting the numeric
  // mobile keyboard. Every other type maps directly.
  static const char *domTypeAttr(InputType t) {
    switch (t) {
    case InputType::Password:
      return "password";
    case InputType::Email:
      return "email";
    case InputType::Tel:
      return "tel";
    case InputType::Url:
      return "url";
    case InputType::Search:
      return "search";
    case InputType::Number:
    case InputType::Text:
    default:
      return "text";
    }
  }

  void onDomInputChanged(const std::string &value) override {
    inputValue = value;
    cursorPos = (int)inputValue.size();
    touched_ = true;
    revalidate();
    notifyStateBinding();
  }

  void onDomFocusChanged(bool focused) override {
    if (auto *ui = FluxUI::getCurrentInstance())
      ui->setFocus(focused ? this : nullptr);
    else
      isFocused = focused;
  }

  void _renderDom(IDomAdapter *adapter) {
    DomNodeHandle node = fluxDomEnsureNode(this, "input");
    char buf[24];
    auto px = [&](int v) {
      snprintf(buf, sizeof(buf), "%dpx", v);
      return std::string(buf);
    };

    fluxDomApplyRect(this, x, y, width, height);
    adapter->setStyle(node, "box-sizing", "border-box");
    adapter->setStyle(node, "padding-left", px(paddingLeft));
    adapter->setStyle(node, "padding-right", px(paddingRight));
    adapter->setStyle(node, "font-size", px(fontSize));
    adapter->setStyle(node, "border-radius", px(borderRadius));

    Color border = currentBorderColor();
    char colbuf[48];
    snprintf(colbuf, sizeof(colbuf), "1px solid rgb(%d,%d,%d)", border.r,
             border.g, border.b);
    adapter->setStyle(node, "border", colbuf);

    adapter->setStyle(node, "background-color", "rgb(255,255,255)");
    adapter->setStyle(node, "color", "rgb(30,30,30)");
    adapter->setStyle(node, "outline", "none");

    // Deliberate exception to "capture div owns all input" — a real
    // <input> needs real pointer events and a real focus target.
    adapter->setStyle(node, "pointer-events", "auto");
    adapter->setStyle(node, "z-index", "2");

    adapter->setAttr(node, "type", domTypeAttr(inputType));
    if (inputType == InputType::Number) {
      adapter->setAttr(node, "inputmode", "numeric");
      adapter->setAttr(node, "pattern", "-?[0-9]*\\.?[0-9]*");
    }
    adapter->setAttr(node, "placeholder", placeholder);
    adapter->setInputValue(node, inputValue);
    adapter->bindInputEvents(node, this);
  }
#endif
};

using TextInputWidgetPtr = std::shared_ptr<TextInputWidget>;

inline TextInputWidgetPtr TextInput(const std::string &placeholder = "") {
  auto w = std::make_shared<TextInputWidget>();
  if (!placeholder.empty())
    w->setPlaceholder(placeholder);
  return w;
}

// Convenience factories mirroring the common HTML input types.
inline TextInputWidgetPtr PasswordInput(const std::string &placeholder = "") {
  return TextInput(placeholder)->setType(InputType::Password);
}
inline TextInputWidgetPtr EmailInput(const std::string &placeholder = "") {
  return TextInput(placeholder)->setType(InputType::Email);
}
inline TextInputWidgetPtr NumberTextInput(const std::string &placeholder = "") {
  return TextInput(placeholder)->setType(InputType::Number);
}

#endif // FLUX_TEXTINPUT_HPP