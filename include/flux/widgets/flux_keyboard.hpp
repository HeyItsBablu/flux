#ifndef FLUX_KEYBOARD_HPP
#define FLUX_KEYBOARD_HPP

#include "../flux_core.hpp"
#include "flux_structure.hpp"
#include <functional>
#include <string>
#include <vector>

// ============================================================================
// VirtualKeyboardWidget
// ============================================================================


struct VKey {
  enum class Action {
    Char, // emits a character via handleChar
    Backspace,
    Return,
    Shift,
    SymbolToggle, // switch to symbol/number page
    Space,
  };

  Action action = Action::Char;
  std::string label;      // displayed on key face
  std::string shiftLabel; // label when shift is active (empty = uppercase)
  float flex = 1.f;       // relative width within the row
  int keyCode = 0;        // used for Backspace / Return (VK_* / SDL_SCANCODE_*)

  static VKey Ch(const std::string &ch, const std::string &shifted = "",
                 float flex = 1.f) {
    VKey k;
    k.action = Action::Char;
    k.label = ch;
    k.shiftLabel = shifted;
    k.flex = flex;
    return k;
  }
  static VKey Space(float flex = 4.f) {
    VKey k;
    k.action = Action::Space;
    k.label = "space";
    k.flex = flex;
    return k;
  }
  static VKey Backspace(float flex = 1.5f) {
    VKey k;
    k.action = Action::Backspace;
    k.label = "⌫";
    k.flex = flex;
    k.keyCode = VK_BACK;
    return k;
  }
  static VKey Return(float flex = 1.5f) {
    VKey k;
    k.action = Action::Return;
    k.label = "return";
    k.flex = flex;
    k.keyCode = VK_RETURN;
    return k;
  }
  static VKey Shift(float flex = 1.5f) {
    VKey k;
    k.action = Action::Shift;
    k.label = "⇧";
    k.flex = flex;
    return k;
  }
  static VKey Sym(float flex = 1.5f) {
    VKey k;
    k.action = Action::SymbolToggle;
    k.label = "123";
    k.flex = flex;
    return k;
  }
};

// ── Keyboard layout tables
// ────────────────────────────────────────────────────

namespace KeyboardLayout {

inline std::vector<VKey> numberRow() {
  return {VKey::Ch("1", "!"), VKey::Ch("2", "@"), VKey::Ch("3", "#"),
          VKey::Ch("4", "$"), VKey::Ch("5", "%"), VKey::Ch("6", "^"),
          VKey::Ch("7", "&"), VKey::Ch("8", "*"), VKey::Ch("9", "("),
          VKey::Ch("0", ")")};
}

inline std::vector<std::vector<VKey>> qwerty() {
  return {{VKey::Ch("q"), VKey::Ch("w"), VKey::Ch("e"), VKey::Ch("r"),
           VKey::Ch("t"), VKey::Ch("y"), VKey::Ch("u"), VKey::Ch("i"),
           VKey::Ch("o"), VKey::Ch("p")},
          {VKey::Ch("a"), VKey::Ch("s"), VKey::Ch("d"), VKey::Ch("f"),
           VKey::Ch("g"), VKey::Ch("h"), VKey::Ch("j"), VKey::Ch("k"),
           VKey::Ch("l")},
          {VKey::Shift(), VKey::Ch("z"), VKey::Ch("x"), VKey::Ch("c"),
           VKey::Ch("v"), VKey::Ch("b"), VKey::Ch("n"), VKey::Ch("m"),
           VKey::Backspace()},
          {VKey::Sym(), VKey::Space(), VKey::Return()}};
}

inline std::vector<std::vector<VKey>> symbols() {
  return {{VKey::Ch("1"), VKey::Ch("2"), VKey::Ch("3"), VKey::Ch("4"),
           VKey::Ch("5"), VKey::Ch("6"), VKey::Ch("7"), VKey::Ch("8"),
           VKey::Ch("9"), VKey::Ch("0")},
          {VKey::Ch("@"), VKey::Ch("#"), VKey::Ch("$"), VKey::Ch("%"),
           VKey::Ch("&"), VKey::Ch("-"), VKey::Ch("+"), VKey::Ch("("),
           VKey::Ch(")")},
          {VKey::Ch("!"), VKey::Ch("\""), VKey::Ch("'"), VKey::Ch(":"),
           VKey::Ch(";"), VKey::Ch("/"), VKey::Ch("?"), VKey::Ch("_"),
           VKey::Backspace()},
          {VKey::Sym(1.5f), VKey::Space(), VKey::Return()}};
}

} // namespace KeyboardLayout

// ── Widget ───────────────────────────────────────────────────────────────────
//
// A singleton, screen-anchored overlay (see instance()) rather than a
// normal tree-anchored widget — it has zero size in the widget tree
// (computeLayout always returns 0x0) and shows itself via the
// OverlayManager only when requestShow() is called, typically from a text
// field gaining focus on a touch platform.
//
// renderOverlay()/onOverlay*() all operate in coordinates LOCAL to the
// keyboard's own overlay rect — (0,0) is the keyboard's top-left corner,
// same as every other OverlayContent widget. OverlayManager handles
// screen-space conversion and native popup creation; this widget never
// touches any of that.

class VirtualKeyboardWidget : public Widget, public OverlayContent {
public:
  // Geometry
  int keyboardHeight = 280;
  int numberRowHeight = 42;
  int keyRowHeight = 46;
  int keyHGap = 6;
  int keyVGap = 6;
  int keyRadius = 8;
  int sidePadding = 6;

  // Colors
  Color bgColor = Color::fromRGB(210, 213, 219);
  Color keyBgColor = Color::fromRGB(255, 255, 255);
  Color keyBgDark = Color::fromRGB(172, 177, 185);
  Color keyPressedColor = Color::fromRGB(180, 185, 195);
  Color keyTextColor = Color::fromRGB(20, 20, 20);
  Color keyTextLight = Color::fromRGB(255, 255, 255);
  Color spaceKeyColor = Color::fromRGB(255, 255, 255);
  Color returnKeyColor = Color::fromRGB(59, 130, 246);
  Color returnTextColor = Color::fromRGB(255, 255, 255);
  Color shiftActiveColor = Color::fromRGB(59, 130, 246);
  Color numLabelColor = Color::fromRGB(90, 90, 90);

private:
  bool isVisible_ = false;
  bool shiftActive_ = false;
  bool symbolPage_ = false;
  int pressedRow_ = -1;
  int pressedCol_ = -1;

  // Guards against requestHide() (called externally, e.g. from
  // notifyFocusLost on a stray focus change) dismissing the keyboard
  // mid-press, between handleMouseDown/handleMouseUp landing on a key.
  // Orthogonal to overlay click routing — kept as-is.
  bool suppressHide_ = false;

  struct KeyRect {
    int x, y, w, h, row, col;
  };
  std::vector<KeyRect> keyRects_;
  int kbW_ = 0; // == keyboard overlay width; height is always keyboardHeight

  Widget *targetWidget_ = nullptr;

public:
  static std::shared_ptr<VirtualKeyboardWidget> instance() {
    auto *ui = FluxUI::getCurrentInstance();
    if (!ui)
      return nullptr;
    return ui->getOrCreateSingleton<VirtualKeyboardWidget>(
        [] { return std::make_shared<VirtualKeyboardWidget>(); });
  }

  void requestShow(Widget *target) {
    targetWidget_ = target;
    if (!isVisible_)
      show_();
  }

  void requestHide() {
    if (suppressHide_)
      return;
    hide_();
  }

  void onDetach() override {
    if (isVisible_)
      hide_();
    Widget::onDetach();
  }

  // ── OverlayContent ────────────────────────────────────────────────────────
  OverlayPolicy overlayPolicy() const override {
    // modal=true is what makes OverlayManager deliver onOverlayOutsideClick
    // for taps outside the keyboard, which is how this widget now detects
    // "tap elsewhere to dismiss" (previously done by manually comparing
    // my < kbClientY_ against every window-wide mouse-down). The keyboard
    // doesn't need to swallow clicks elsewhere in the app the way a dialog
    // does, but modal is what wires up the dismiss callback, so it stays
    // true.
    // blocksHoverBelow=false: typing shouldn't pause hover/tooltips
    // elsewhere on screen.
    // capturesKeyboard=false: this widget never goes through
    // onOverlayKeyDown — it injects characters directly into
    // targetWidget_ via handleChar/handleKeyDown instead of routing
    // through the overlay key-event path.
    return {/*modal=*/true, /*blocksHoverBelow=*/false, /*capturesKeyboard=*/false};
  }

  // ── Layout / render (zero size in the normal widget tree) ────────────────

  void computeLayout(GraphicsContext &, const BoxConstraints &,
                     FontCache &) override {
    width = height = 0;
    needsLayout = false;
  }

  void render(GraphicsContext &, FontCache &) override { needsPaint = false; }

  // ── renderOverlay ─────────────────────────────────────────────────────────
  // Already fully local-coordinate — (0,0) is the keyboard's own top-left
  // corner, unchanged from the old renderPopupContent body.

  void renderOverlay(GraphicsContext &ctx, FontCache &fontCache) override {
    if (!isVisible_)
      return;

    Painter painter(ctx);
    painter.fillRect(0, 0, kbW_, keyboardHeight, bgColor);

    const auto &layout =
        symbolPage_ ? KeyboardLayout::symbols() : KeyboardLayout::qwerty();

    NativeFont fontNormal =
        fontCache.getFont("Segoe UI", 15, FontWeight::Normal);
    NativeFont fontSmall =
        fontCache.getFont("Segoe UI", 11, FontWeight::Normal);

    int numRowY = keyVGap;
    drawRow_(painter, fontNormal, fontSmall, KeyboardLayout::numberRow(),
             numRowY, numberRowHeight, -1);

    int curY = keyVGap + numberRowHeight + keyVGap;
    for (int r = 0; r < (int)layout.size(); r++) {
      drawRow_(painter, fontNormal, fontSmall, layout[r], curY, keyRowHeight,
               r);
      curY += keyRowHeight + keyVGap;
    }
  }

  // ── OverlayContent input handlers (overlay-local coordinates) ───────────
  // localX/localY are already relative to the keyboard's own top-left —
  // no more manual `mx - kbClientX_` subtraction needed, and no more
  // manual `my < kbClientY_` check either: a click that lands outside
  // this overlay's rect never reaches onOverlayMouseDown at all, it goes
  // to onOverlayOutsideClick below instead.

  bool onOverlayMouseDown(int localX, int localY) override {
    if (!isVisible_)
      return false;

    suppressHide_ = true;

    for (auto &kr : keyRects_) {
      if (localX >= kr.x && localX < kr.x + kr.w && localY >= kr.y &&
          localY < kr.y + kr.h) {
        pressedRow_ = kr.row;
        pressedCol_ = kr.col;
        refreshPopupIfOpen_();
        return true;
      }
    }
    return true;
  }

  bool onOverlayMouseUp(int localX, int localY) override {
    if (!isVisible_)
      return false;

    for (auto &kr : keyRects_) {
      if (localX >= kr.x && localX < kr.x + kr.w && localY >= kr.y &&
          localY < kr.y + kr.h && kr.row == pressedRow_ &&
          kr.col == pressedCol_) {
        fireKey_(kr.row, kr.col);
        break;
      }
    }

    pressedRow_ = pressedCol_ = -1;
    suppressHide_ = false;
    refreshPopupIfOpen_();
    return true;
  }

  bool onOverlayMouseMove(int, int) override { return isVisible_; }

  // Tap above/outside the keyboard -> dismiss it and clear focus. This is
  // the direct replacement for the old `if (my < kbClientY_) hide_()`
  // check inside handleMouseDown, now driven by the manager's own
  // inside/outside hit-test against the registered overlay rect instead
  // of a manual y-coordinate comparison.
  void onOverlayOutsideClick() override {
    hide_();
    if (auto *ui = FluxUI::getCurrentInstance())
      ui->setFocus(nullptr);
  }

  bool handleMouseLeave() override {
    if (suppressHide_) {
      suppressHide_ = false;
      pressedRow_ = pressedCol_ = -1;
    }
    return false;
  }

private:
  // ── Show / hide ───────────────────────────────────────────────────────────

  void show_() {
    if (isVisible_)
      return;

    auto *ui = FluxUI::getCurrentInstance();
    if (!ui)
      return;

    isVisible_ = true;
    shiftActive_ = true;
    symbolPage_ = false;
    pressedRow_ = pressedCol_ = -1;

    auto sz = ui->getClientSize();
    kbW_ = sz.width;
    int kbClientX = 0;
    int kbClientY = sz.height - keyboardHeight;

    buildKeyRects_();

    // zIndex 300: above everything else (dropdowns 100, tooltips 50,
    // context menus / menu-bar pulldowns 150) — the on-screen keyboard
    // should never be obscured by another overlay.
    ui->overlays().show(this, kbClientX, kbClientY, kbW_, keyboardHeight,
                        300, ui->getFontCache());
  }

  void hide_() {
    if (!isVisible_)
      return;
    isVisible_ = false;
    targetWidget_ = nullptr;
    if (auto *ui = FluxUI::getCurrentInstance())
      ui->overlays().hide(this);
  }

  // ── Key rect cache ────────────────────────────────────────────────────────

  void buildKeyRects_() {
    keyRects_.clear();
    const auto &layout =
        symbolPage_ ? KeyboardLayout::symbols() : KeyboardLayout::qwerty();

    buildRowRects_(KeyboardLayout::numberRow(), keyVGap, numberRowHeight, -1);

    int curY = keyVGap + numberRowHeight + keyVGap;
    for (int r = 0; r < (int)layout.size(); r++) {
      buildRowRects_(layout[r], curY, keyRowHeight, r);
      curY += keyRowHeight + keyVGap;
    }
  }

  void buildRowRects_(const std::vector<VKey> &row, int rowY, int rowH,
                      int rowIdx) {
    float totalFlex = 0.f;
    for (auto &k : row)
      totalFlex += k.flex;

    int availW = kbW_ - sidePadding * 2;
    float unitW = (availW - keyHGap * ((int)row.size() - 1)) / totalFlex;
    float curX = (float)sidePadding;

    for (int c = 0; c < (int)row.size(); c++) {
      KeyRect kr;
      kr.x = (int)curX;
      kr.y = rowY;
      kr.w = (int)(row[c].flex * unitW);
      kr.h = rowH;
      kr.row = rowIdx;
      kr.col = c;
      keyRects_.push_back(kr);
      curX += kr.w + keyHGap;
    }
  }

  // ── Draw one row ──────────────────────────────────────────────────────────

  void drawRow_(Painter &painter, NativeFont fontNormal, NativeFont fontSmall,
                const std::vector<VKey> &row, int rowY, int rowH, int rowIdx) {
    float totalFlex = 0.f;
    for (auto &k : row)
      totalFlex += k.flex;

    int availW = kbW_ - sidePadding * 2;
    float unitW = (availW - keyHGap * ((int)row.size() - 1)) / totalFlex;
    float curX = (float)sidePadding;

    for (int c = 0; c < (int)row.size(); c++) {
      const VKey &k = row[c];
      int kw = (int)(k.flex * unitW);
      int kx = (int)curX;

      bool isPressed = (pressedRow_ == rowIdx && pressedCol_ == c);
      bool isDark = (k.action == VKey::Action::Shift ||
                     k.action == VKey::Action::SymbolToggle ||
                     k.action == VKey::Action::Backspace);
      bool isReturn = (k.action == VKey::Action::Return);
      bool isShiftOn = (k.action == VKey::Action::Shift && shiftActive_);

      Color bg = isReturn    ? returnKeyColor
                 : isShiftOn ? shiftActiveColor
                 : isDark    ? keyBgDark
                 : isPressed ? keyPressedColor
                             : keyBgColor;

      if (isPressed && !isReturn)
        bg = keyPressedColor;
      painter.fillRoundedRect(kx, rowY, kw, rowH, keyRadius, bg);

      // Resolve label
      std::string rawLabel = k.label;
      if (k.action == VKey::Action::Char && shiftActive_)
        rawLabel = k.shiftLabel.empty() ? toUpper_(k.label) : k.shiftLabel;
      else if (k.action == VKey::Action::SymbolToggle)
        rawLabel = symbolPage_ ? "ABC" : "123";

      Color textCol = (isReturn || isShiftOn) ? returnTextColor
                      : isDark                ? keyTextLight
                                              : keyTextColor;

    
      if (rowIdx == -1 && k.action == VKey::Action::Char &&
          !k.shiftLabel.empty()) {
        std::wstring wsym = toWideString(k.shiftLabel);
        painter.drawText(wsym, kx, rowY + 3, kw, rowH / 2, fontSmall,
                         numLabelColor, DT_CENTER | DT_SINGLELINE);
        std::wstring wdig = toWideString(k.label);
        painter.drawText(wdig, kx, rowY + rowH / 4, kw, rowH * 3 / 4,
                         fontNormal, textCol,
                         DT_CENTER | DT_VCENTER | DT_SINGLELINE);
      } else {
        std::wstring wlabel = toWideString(rawLabel);
        painter.drawText(wlabel, kx, rowY, kw, rowH, fontNormal, textCol,
                         DT_CENTER | DT_VCENTER | DT_SINGLELINE);
      }

      curX += kw + keyHGap;
    }
  }

  // ── Fire a key action ─────────────────────────────────────────────────────

  void fireKey_(int rowIdx, int colIdx) {
    const auto &layout =
        symbolPage_ ? KeyboardLayout::symbols() : KeyboardLayout::qwerty();

    const VKey *key = nullptr;
    if (rowIdx == -1) {
      const auto &nr = KeyboardLayout::numberRow();
      if (colIdx >= 0 && colIdx < (int)nr.size())
        key = &nr[colIdx];
    } else if (rowIdx >= 0 && rowIdx < (int)layout.size() && colIdx >= 0 &&
               colIdx < (int)layout[rowIdx].size()) {
      key = &layout[rowIdx][colIdx];
    }
    if (!key)
      return;

    auto *ui = FluxUI::getCurrentInstance();
    Widget *target =
        targetWidget_ ? targetWidget_ : (ui ? ui->getFocusedWidget() : nullptr);

    switch (key->action) {
    case VKey::Action::Char: {
      std::string ch = key->label;
      if (shiftActive_) {
        ch = key->shiftLabel.empty() ? toUpper_(key->label) : key->shiftLabel;
        shiftActive_ = false;
        refreshPopupIfOpen_();
      }
      if (target && !ch.empty())
        target->handleChar((wchar_t)ch[0]);
      break;
    }
    case VKey::Action::Space:
      if (target)
        target->handleChar(L' ');
      if (shiftActive_) {
        shiftActive_ = false;
        refreshPopupIfOpen_();
      }
      break;
    case VKey::Action::Backspace:
      if (target)
        target->handleKeyDown(VK_BACK);
      break;
    case VKey::Action::Return:
      if (target)
        target->handleKeyDown(VK_RETURN);
      break;
    case VKey::Action::Shift:
      shiftActive_ = !shiftActive_;
      refreshPopupIfOpen_();
      break;
    case VKey::Action::SymbolToggle:
      symbolPage_ = !symbolPage_;
      buildKeyRects_();
      refreshPopupIfOpen_();
      break;
    }

    if (target && ui)
      ui->invalidateWidget(target);
  }

  // ── Helpers ───────────────────────────────────────────────────────────────

  void refreshPopupIfOpen_() {
    if (!isVisible_)
      return;
    if (auto *ui = FluxUI::getCurrentInstance())
      ui->overlays().refresh(this, ui->getFontCache());
  }

  static std::string toUpper_(const std::string &s) {
    std::string out = s;
    for (char &c : out)
      c = (char)std::toupper((unsigned char)c);
    return out;
  }
};



namespace VirtualKeyboard {

// Show the keyboard and associate it with `target`.
// No-op on Windows and Linux; active on Android.
inline void notifyFocusGained(Widget *target) {
#ifdef __ANDROID__

  if (auto kb = VirtualKeyboardWidget::instance())
    kb->requestShow(target);
#else
  (void)target;
#endif
}

inline void notifyFocusLost() {
#ifdef __ANDROID__

  if (auto kb = VirtualKeyboardWidget::instance())
    kb->requestHide();
#endif
}

}

// ── Factory
// ───────────────────────────────────────────────────────────────────

using VirtualKeyboardPtr = std::shared_ptr<VirtualKeyboardWidget>;

#endif // FLUX_KEYBOARD_HPP