#ifndef FLUX_KEYBOARD_HPP
#define FLUX_KEYBOARD_HPP

#include "../flux_core.hpp"
#include "../flux_overlay_host.hpp"
#include "flux_structure.hpp"
#include <functional>
#include <string>
#include <vector>

// ============================================================================
// VirtualKeyboardWidget
// ============================================================================
//
// Android-style QWERTY overlay. Attaches to the overlay stack exactly like
// DialogWidget — full-width, pinned to the bottom of the window.
//
// Lifecycle:
//   FluxUI::setFocus(textInput)  →  TextInputWidget::handleFocus(true)
//                                →  keyboard.show(textInput)
//   tap outside keys             →  keyboard.hide()
//   FluxUI::setFocus(nullptr)    →  TextInputWidget::handleFocus(false)
//                                →  keyboard.hide()
//

//
// That hooks the FluxUI focus-change callback so the keyboard auto-shows
// and auto-hides whenever a TextInputWidget gains / loses focus.

// ── Key descriptor
// ────────────────────────────────────────────────────────────

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

  // Char key
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

// Numbers + symbols row always shown at top
inline std::vector<VKey> numberRow() {
  return {VKey::Ch("1", "!"), VKey::Ch("2", "@"), VKey::Ch("3", "#"),
          VKey::Ch("4", "$"), VKey::Ch("5", "%"), VKey::Ch("6", "^"),
          VKey::Ch("7", "&"), VKey::Ch("8", "*"), VKey::Ch("9", "("),
          VKey::Ch("0", ")")};
}

inline std::vector<std::vector<VKey>> qwerty() {
  return {{// row 1
           VKey::Ch("q"), VKey::Ch("w"), VKey::Ch("e"), VKey::Ch("r"),
           VKey::Ch("t"), VKey::Ch("y"), VKey::Ch("u"), VKey::Ch("i"),
           VKey::Ch("o"), VKey::Ch("p")},
          {// row 2
           VKey::Ch("a"), VKey::Ch("s"), VKey::Ch("d"), VKey::Ch("f"),
           VKey::Ch("g"), VKey::Ch("h"), VKey::Ch("j"), VKey::Ch("k"),
           VKey::Ch("l")},
          {// row 3 — shift | z..m | backspace
           VKey::Shift(), VKey::Ch("z"), VKey::Ch("x"), VKey::Ch("c"),
           VKey::Ch("v"), VKey::Ch("b"), VKey::Ch("n"), VKey::Ch("m"),
           VKey::Backspace()},
          {// row 4 — sym | space | return
           VKey::Sym(), VKey::Space(), VKey::Return()}};
}

// Symbol / number page shown when user taps "123"
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
          {VKey::Sym(1.5f), // label becomes "ABC" on symbol page
           VKey::Space(), VKey::Return()}};
}

} // namespace KeyboardLayout

// ── Widget
// ────────────────────────────────────────────────────────────────────

class VirtualKeyboardWidget : public Widget, public OverlayHost {
public:
  // Geometry
  int keyboardHeight = 280; // total overlay height
  int numberRowHeight = 42;
  int keyRowHeight = 46;
  int keyHGap = 6; // horizontal gap between keys
  int keyVGap = 6; // vertical gap between rows
  int keyRadius = 8;
  int sidePadding = 6;

  // Colors — Android Material-ish
  Color bgColor = Color::fromRGB(210, 213, 219);
  Color keyBgColor = Color::fromRGB(255, 255, 255);
  Color keyBgDark = Color::fromRGB(172, 177, 185); // shift/sym/backspace
  Color keyPressedColor = Color::fromRGB(180, 185, 195);
  Color keyTextColor = Color::fromRGB(20, 20, 20);
  Color keyTextLight = Color::fromRGB(255, 255, 255); // on dark keys
  Color spaceKeyColor = Color::fromRGB(255, 255, 255);
  Color returnKeyColor = Color::fromRGB(59, 130, 246);
  Color returnTextColor = Color::fromRGB(255, 255, 255);
  Color shiftActiveColor = Color::fromRGB(59, 130, 246);
  Color numLabelColor = Color::fromRGB(90, 90, 90);

private:
  ScaffoldWidget *scaffold = nullptr;

  bool isVisible_ = false;
  bool shiftActive_ = false;
  bool symbolPage_ = false;
  int pressedRow_ = -1;
  int pressedCol_ = -1;
  bool suppressHide_ = false; // true while a key tap is in flight

  // Resolved layout for current page (rebuilt on open / page switch)
  struct KeyRect {
    int x, y, w, h;
    int row, col;
  };
  std::vector<KeyRect> keyRects_;
  int kbClientX_ = 0; // keyboard's top-left in client coords
  int kbClientY_ = 0;
  int kbW_ = 0;

  // The TextInputWidget that triggered us (weak — don't own it)
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

  // setScaffold still needed — call it from ScaffoldWidget's computeLayout
  void setScaffold(ScaffoldWidget *s) override { scaffold = s; }

  // ── Attach to FluxUI focus system ──────────────────────────────────────
  //
  // Call once after the widget tree is built.  Replaces nothing — just
  // registers a focus-change observer.  Keyboard shows for TextInputWidget,
  // hides for everything else.

  // ── Layout / render (keyboard has zero size in the normal tree) ────────

  void computeLayout(GraphicsContext &, const BoxConstraints &,
                     FontCache &) override {
    width = height = 0;
    needsLayout = false;
  }

  void render(GraphicsContext &, FontCache &) override { needsPaint = false; }

  // ── renderPopupContent ─────────────────────────────────────────────────

  void renderPopupContent(GraphicsContext &ctx, FontCache &fontCache) override {
    if (!isVisible_)
      return;

    Painter painter(ctx);

    // Background
    painter.fillRect(0, 0, kbW_, keyboardHeight, bgColor);

    // Draw all keys
    const auto &layout =
        symbolPage_ ? KeyboardLayout::symbols() : KeyboardLayout::qwerty();

    NativeFont fontNormal =
        fontCache.getFont("Segoe UI", 15, FontWeight::Normal);
    NativeFont fontSmall =
        fontCache.getFont("Segoe UI", 11, FontWeight::Normal);

    // Number row
    const auto &numRow = KeyboardLayout::numberRow();
    int numRowY = keyVGap;
    drawRow_(painter, fontNormal, fontSmall, numRow, numRowY, numberRowHeight,
             -1);

    // QWERTY / symbol rows
    int curY = keyVGap + numberRowHeight + keyVGap;
    for (int r = 0; r < (int)layout.size(); r++) {
      drawRow_(painter, fontNormal, fontSmall, layout[r], curY, keyRowHeight,
               r);
      curY += keyRowHeight + keyVGap;
    }
  }

  // ── Mouse / touch events ───────────────────────────────────────────────

  bool handleMouseDown(int mx, int my) override {
    if (!isVisible_)
      return false;
    if (my < kbClientY_) {
      hide_();
      if (auto *ui = FluxUI::getCurrentInstance())
        ui->setFocus(nullptr);
      return true;
    }
    suppressHide_ = true; // hold off requestHide() until mouseUp

    int lx = mx - kbClientX_;
    int ly = my - kbClientY_;
    for (auto &kr : keyRects_) {
      if (lx >= kr.x && lx < kr.x + kr.w && ly >= kr.y && ly < kr.y + kr.h) {
        pressedRow_ = kr.row;
        pressedCol_ = kr.col;
        refreshPopupIfOpen_();
        return true;
      }
    }
    return true;
  }

  bool handleMouseUp(int mx, int my) override {
    if (!isVisible_)
      return false;

    int lx = mx - kbClientX_;
    int ly = my - kbClientY_;

    for (auto &kr : keyRects_) {
      if (lx >= kr.x && lx < kr.x + kr.w && ly >= kr.y && ly < kr.y + kr.h &&
          kr.row == pressedRow_ && kr.col == pressedCol_) {
        fireKey_(kr.row, kr.col);
        break;
      }
    }

    pressedRow_ = pressedCol_ = -1;
    suppressHide_ = false;
    refreshPopupIfOpen_();
    return true;
  }
  bool handleMouseMove(int /*mx*/, int /*my*/) override { return isVisible_; }

  bool handleMouseLeave() override {
    if (suppressHide_) {
      suppressHide_ = false;
      pressedRow_ = pressedCol_ = -1;
    }
    return false;
  }

private:
  // ── Show / hide ────────────────────────────────────────────────────────

  void show_() {
    if (isVisible_)
      return;

    if (auto *ui = FluxUI::getCurrentInstance())
      scaffold = ui->getRootScaffold(); // see Step 3

    isVisible_ = true;
    shiftActive_ = true; // start capitalised like Android
    symbolPage_ = false;
    pressedRow_ = pressedCol_ = -1;

    auto sz = FluxUI::getCurrentInstance()->getClientSize();
    kbW_ = sz.width;
    kbClientX_ = 0;
    kbClientY_ = sz.height - keyboardHeight;

    buildKeyRects_();

    auto origin = FluxUI::getCurrentInstance()->clientToScreen(0, kbClientY_);

    FontCache &fc = FluxUI::getCurrentInstance()->getFontCache();
    NativeWindow hw = FluxUI::getCurrentInstance()->getWindow();
    if (hw)
      showPopup(hw, origin.x, origin.y, kbW_, keyboardHeight, fc);
      

    if (scaffold)
      scaffold->addOverlayHitTarget(this, 300); // higher z than dialogs
  }

  void hide_() {
    if (!isVisible_)
      return;
    isVisible_ = false;
    targetWidget_ = nullptr;
    hidePopup();
    if (scaffold)
      scaffold->removeOverlay(this);
  }

  // ── Build key rect cache (local coords, origin = keyboard top-left) ────

  void buildKeyRects_() {
    keyRects_.clear();

    const auto &layout =
        symbolPage_ ? KeyboardLayout::symbols() : KeyboardLayout::qwerty();

    // Number row (row index = -1, col = 0..9)
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
      int kw = (int)(row[c].flex * unitW);
      KeyRect kr;
      kr.x = (int)curX;
      kr.y = rowY;
      kr.w = kw;
      kr.h = rowH;
      kr.row = rowIdx;
      kr.col = c;
      keyRects_.push_back(kr);
      curX += kw + keyHGap;
    }
  }

  // ── Draw one row ───────────────────────────────────────────────────────

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

      // Label
      std::string rawLabel = k.label;
      if (k.action == VKey::Action::Char) {
        bool useShift = shiftActive_;
        if (useShift) {
          rawLabel = k.shiftLabel.empty() ? toUpper_(k.label) : k.shiftLabel;
        }
      } else if (k.action == VKey::Action::SymbolToggle) {
        rawLabel = symbolPage_ ? "ABC" : "123";
      }

      Color textCol = (isReturn || isShiftOn) ? returnTextColor
                      : isDark                ? keyTextLight
                                              : keyTextColor;

      // Number row: show shift-label as secondary hint above main label
      if (rowIdx == -1 && k.action == VKey::Action::Char &&
          !k.shiftLabel.empty()) {
        // secondary symbol (smaller, top)
        std::wstring wsym = toWideString(k.shiftLabel);
        painter.drawText(wsym, kx, rowY + 3, kw, rowH / 2, fontSmall,
                         numLabelColor, DT_CENTER | DT_SINGLELINE);
        // primary digit (larger, center-bottom)
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

  // ── Fire a key action ──────────────────────────────────────────────────

  void fireKey_(int rowIdx, int colIdx) {

    const auto &layout =
        symbolPage_ ? KeyboardLayout::symbols() : KeyboardLayout::qwerty();

    // Resolve which row/key
    const VKey *key = nullptr;
    if (rowIdx == -1) {
      const auto &nr = KeyboardLayout::numberRow();
      if (colIdx >= 0 && colIdx < (int)nr.size())
        key = &nr[colIdx];
    } else {
      if (rowIdx >= 0 && rowIdx < (int)layout.size() && colIdx >= 0 &&
          colIdx < (int)layout[rowIdx].size())
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
        shiftActive_ = false; // one-shot shift like Android
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
      buildKeyRects_(); // rects change between pages
      refreshPopupIfOpen_();
      break;
    }

    // Invalidate the target input so it repaints with the new char
    if (target && ui)
      ui->invalidateWidget(target);
  }

  // ── Helpers ────────────────────────────────────────────────────────────

  void refreshPopupIfOpen_() {
    if (!isVisible_ || !popupVisible())
      return;
    auto *ui = FluxUI::getCurrentInstance();
    if (ui)
      refreshPopup(ui->getFontCache());
  }

  static std::string toUpper_(const std::string &s) {
    std::string out = s;
    for (char &c : out)
      c = (char)std::toupper((unsigned char)c);
    return out;
  }
};

// At bottom of flux_keyboard.hpp, after the class definition:
namespace VirtualKeyboard {

inline void notifyFocusGained(Widget *target) {
  if (auto kb = VirtualKeyboardWidget::instance())
    kb->requestShow(target);
}

inline void notifyFocusLost() {
  if (auto kb = VirtualKeyboardWidget::instance())
    kb->requestHide();
}

} // namespace VirtualKeyboard

// ── Factory
// ───────────────────────────────────────────────────────────────────

using VirtualKeyboardPtr = std::shared_ptr<VirtualKeyboardWidget>;

// inline VirtualKeyboardPtr VirtualKeyboard() {
//   return std::make_shared<VirtualKeyboardWidget>();
// }

#endif // FLUX_KEYBOARD_HPP