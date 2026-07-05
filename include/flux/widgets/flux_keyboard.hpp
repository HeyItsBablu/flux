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

class VirtualKeyboardWidget : public Widget {
public:
  // ── Popup-body surface ─────────────────────────────────────────────────
  // Spans the bottom strip of the screen (x=0, y=winH-keyboardHeight).
  // keyRects_ stay stored in keyboard-local coordinates (unchanged) since
  // hit-testing here subtracts the surface's own x/y first — matching the
  // old contract where OverlayManager delivered already-local coordinates.
  class KeyboardSurface : public Widget {
  public:
    VirtualKeyboardWidget *owner = nullptr;

    void render(GraphicsContext &ctx, FontCache &fontCache) override {
      if (!owner || !owner->isVisible_)
        return;

      Painter painter(ctx, this);
      painter.fillRect(x, y, owner->kbW_, owner->keyboardHeight, owner->bgColor);

      const auto &layout = owner->symbolPage_ ? KeyboardLayout::symbols()
                                              : KeyboardLayout::qwerty();

      NativeFont fontNormal = fontCache.getFont("Segoe UI", 15, FontWeight::Normal);
      NativeFont fontSmall = fontCache.getFont("Segoe UI", 11, FontWeight::Normal);

      int numRowY = owner->keyVGap;
      owner->drawRow_(painter, fontNormal, fontSmall, KeyboardLayout::numberRow(),
                      y + numRowY, owner->numberRowHeight, -1, x);

      int curY = owner->keyVGap + owner->numberRowHeight + owner->keyVGap;
      for (int r = 0; r < (int)layout.size(); r++) {
        owner->drawRow_(painter, fontNormal, fontSmall, layout[r],
                        y + curY, owner->keyRowHeight, r, x);
        curY += owner->keyRowHeight + owner->keyVGap;
      }
      needsPaint = false;
    }

    bool handleMouseDown(int mx, int my) override {
      if (!owner || !owner->isVisible_)
        return false;
      int localX = mx - x, localY = my - y;

      owner->suppressHide_ = true;

      for (auto &kr : owner->keyRects_) {
        if (localX >= kr.x && localX < kr.x + kr.w && localY >= kr.y &&
            localY < kr.y + kr.h) {
          owner->pressedRow_ = kr.row;
          owner->pressedCol_ = kr.col;
          owner->refreshPopupIfOpen_();
          return true;
        }
      }
      return true;
    }

    bool handleMouseUp(int mx, int my) override {
      if (!owner || !owner->isVisible_)
        return false;
      int localX = mx - x, localY = my - y;

      for (auto &kr : owner->keyRects_) {
        if (localX >= kr.x && localX < kr.x + kr.w && localY >= kr.y &&
            localY < kr.y + kr.h && kr.row == owner->pressedRow_ &&
            kr.col == owner->pressedCol_) {
          owner->fireKey_(kr.row, kr.col);
          break;
        }
      }

      owner->pressedRow_ = owner->pressedCol_ = -1;
      owner->suppressHide_ = false;
      owner->refreshPopupIfOpen_();
      return true;
    }

    bool handleMouseMove(int, int) override { return owner && owner->isVisible_; }

    // Tap above/outside the keyboard -> dismiss it and clear focus. Direct
    // replacement for the old `if (my < kbClientY_) hide_()` check, now
    // driven by dispatchOverlayMouseDown's own inside/outside hit-test
    // against this surface's registered rect.
    void onOverlayOutsideClick() override {
      if (!owner)
        return;
      owner->hide_();
      if (auto *ui = FluxUI::getCurrentInstance())
        ui->setFocus(nullptr);
    }

    bool handleMouseLeave() override {
      if (!owner)
        return false;
      if (owner->suppressHide_) {
        owner->suppressHide_ = false;
        owner->pressedRow_ = owner->pressedCol_ = -1;
      }
      return false;
    }
  };

  std::shared_ptr<KeyboardSurface> keyboardSurface_;

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

  VirtualKeyboardWidget() {
    keyboardSurface_ = std::make_shared<KeyboardSurface>();
    keyboardSurface_->owner = this;
  }


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



  // ── Layout / render (zero size in the normal widget tree) ────────────────

  void computeLayout(GraphicsContext &, const BoxConstraints &,
                     FontCache &) override {
    width = height = 0;
    needsLayout = false;
  }

  void render(GraphicsContext &, FontCache &) override { needsPaint = false; }




private:
friend class KeyboardSurface;
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

    keyboardSurface_->x = kbClientX;
    keyboardSurface_->y = kbClientY;
    keyboardSurface_->width = kbW_;
    keyboardSurface_->height = keyboardHeight;
    // modal=true is what makes dispatchOverlayMouseDown deliver
    // onOverlayOutsideClick for taps outside the keyboard. blocksHoverBelow
    // =false: typing shouldn't pause hover/tooltips elsewhere on screen.
    // capturesKeyboard=false: this widget never routes through
    // handleKeyDown — it injects characters directly into targetWidget_
    // via handleChar/handleKeyDown on the *target*, not the surface.
    // zIndex 300: above everything else (dropdowns 100, tooltips 50,
    // context menus / menu-bar pulldowns 150) — the on-screen keyboard
    // should never be obscured by another overlay.
    ui->showOverlay(keyboardSurface_.get(), /*zIndex=*/300,
                    /*modal=*/true, /*blocksHoverBelow=*/false,
                    /*capturesKeyboard=*/false);
  }

  void hide_() {
    if (!isVisible_)
      return;
    isVisible_ = false;
    targetWidget_ = nullptr;
    if (auto *ui = FluxUI::getCurrentInstance())
      ui->hideOverlay(keyboardSurface_.get());
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
                const std::vector<VKey> &row, int rowY, int rowH, int rowIdx,
                int ox) {
    float totalFlex = 0.f;
    for (auto &k : row)
      totalFlex += k.flex;

    int availW = kbW_ - sidePadding * 2;
    float unitW = (availW - keyHGap * ((int)row.size() - 1)) / totalFlex;
    float curX = (float)sidePadding + ox;

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
      ui->refreshOverlay(keyboardSurface_.get());
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