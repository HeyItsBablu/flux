#ifndef FLUX_TEXT_INPUT_HPP
#define FLUX_TEXT_INPUT_HPP

#include "flux_core.hpp"
#include "flux_state.hpp"
#include <iostream>

template <typename T> class State;

class Widget;

using WidgetPtr = std::shared_ptr<Widget>;
using ClickHandler = std::function<void()>;
using HoverHandler = std::function<void(bool)>;

class TextInputWidget : public Widget {
public:
  std::string inputValue;
  std::string placeholder;
  int cursorPos = 0;
  bool cursorVisible = true;
  UINT cursorTimerId = 1;
  int scrollOffset = 0;

  COLORREF focusedBorderColor = RGB(33, 150, 243);
  COLORREF unfocusedBorderColor = RGB(180, 180, 180);
  COLORREF placeholderColor = RGB(180, 180, 180);
  COLORREF inputTextColor = RGB(30, 30, 30);

  TextInputWidget() {
    isFocusable = true;
    hasBorder = true;
    hasBackground = true;
    backgroundColor = RGB(255, 255, 255);
    borderColor = unfocusedBorderColor;
    borderWidth = 1;
    borderRadius = 4;
    paddingLeft = paddingRight = 10;
    paddingTop = paddingBottom = 8;
    height = 36;
    autoHeight = false;
  }

  void computeLayout(HDC hdc, int availableWidth, int availableHeight,
                     FontCache &fontCache) override {
    if (autoWidth)
      width = availableWidth;

    applyConstraints();
    needsLayout = false;
  }

  void render(HDC hdc, FontCache &fontCache) override {
    borderColor = isFocused ? focusedBorderColor : unfocusedBorderColor;
    drawRoundedRectangle(hdc);

    HFONT hFont = fontCache.getFont(fontSize, fontWeight);
    HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);

    int textX = x + paddingLeft;
    int textY = y + paddingTop;
    int textW = width - paddingLeft - paddingRight;
    int textH = height - paddingTop - paddingBottom;

    RECT clipRect = {x + paddingLeft, y + paddingTop, x + width - paddingRight,
                     y + height - paddingBottom};

    HRGN clipRgn = CreateRectRgn(clipRect.left, clipRect.top, clipRect.right,
                                 clipRect.bottom);
    SelectClipRgn(hdc, clipRgn);

    SetBkMode(hdc, TRANSPARENT);

    if (inputValue.empty() && !placeholder.empty()) {
      SetTextColor(hdc, placeholderColor);
      RECT pr = clipRect;
      DrawText(hdc, placeholder.c_str(), -1, &pr,
               DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    } else {
      SetTextColor(hdc, inputTextColor);
      RECT tr = clipRect;
      tr.left -= scrollOffset;
      DrawText(hdc, inputValue.c_str(), -1, &tr,
               DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);
    }

    if (isFocused && cursorVisible) {
      SIZE textSize = {0};
      if (cursorPos > 0) {
        GetTextExtentPoint32(hdc, inputValue.c_str(), cursorPos, &textSize);
      }

      int cursorX = textX + textSize.cx - scrollOffset;
      int cursorY1 = y + paddingTop + 2;
      int cursorY2 = y + height - paddingBottom - 2;

      HPEN cursorPen = CreatePen(PS_SOLID, 1, RGB(30, 30, 30));
      HPEN oldPen = (HPEN)SelectObject(hdc, cursorPen);

      MoveToEx(hdc, cursorX, cursorY1, nullptr);
      LineTo(hdc, cursorX, cursorY2);

      SelectObject(hdc, oldPen);
      DeleteObject(cursorPen);
    }

    SelectClipRgn(hdc, nullptr);
    DeleteObject(clipRgn);

    SelectObject(hdc, hOldFont);
    needsPaint = false;
  }

  bool handleFocus(bool focused) override {
    isFocused = focused;

    if (focused) {
      HWND hwnd = FluxUI::getCurrentInstance()->getWindow();
      SetTimer(hwnd, cursorTimerId, 530, nullptr);
      cursorVisible = true;
    } else {
      HWND hwnd = FluxUI::getCurrentInstance()->getWindow();
      KillTimer(hwnd, cursorTimerId);
      cursorVisible = false;
    }

    markNeedsPaint();
    return true;
  }

  bool handleTimer(UINT timerId) override {
    if (timerId == cursorTimerId) {
      cursorVisible = !cursorVisible;
      return true;
    }
    return false;
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

    std::string charStr(1, (char)ch);
    inputValue.insert(cursorPos, charStr);
    cursorPos++;
    cursorVisible = true;

    updateScroll();
    notifyStateBinding();
    return true;
  }

  bool handleKeyDown(int keyCode) override {
    switch (keyCode) {
    case VK_BACK:
      if (cursorPos > 0) {
        inputValue.erase(cursorPos - 1, 1);
        cursorPos--;
        cursorVisible = true;
        updateScroll();
        notifyStateBinding();
        return true;
      }
      break;

    case VK_DELETE:
      if (cursorPos < (int)inputValue.size()) {
        inputValue.erase(cursorPos, 1);
        cursorVisible = true;
        updateScroll();
        notifyStateBinding();
        return true;
      }
      break;

    case VK_LEFT:
      if (cursorPos > 0) {
        cursorPos--;
        cursorVisible = true;
        updateScroll();
        return true;
      }
      break;

    case VK_RIGHT:
      if (cursorPos < (int)inputValue.size()) {
        cursorPos++;
        cursorVisible = true;
        updateScroll();
        return true;
      }
      break;

    case VK_HOME:
      cursorPos = 0;
      scrollOffset = 0;
      return true;

    case VK_END:
      cursorPos = (int)inputValue.size();
      updateScroll();
      return true;
    }
    return false;
  }

  std::shared_ptr<TextInputWidget> setInputValue(State<std::string> &state) {
    inputValue = state.get();
    cursorPos = (int)inputValue.size();
    scrollOffset = 0;

    state.bindProperty(
        shared_from_this(),
        [](Widget *w, const std::string &val) {
          auto *input = static_cast<TextInputWidget *>(w);
          input->inputValue = val;
          input->cursorPos = (int)val.size();
        },
        false);

    boundStringState = &state;

    return std::static_pointer_cast<TextInputWidget>(shared_from_this());
  }

  std::shared_ptr<TextInputWidget> setPlaceholder(const std::string &ph) {
    placeholder = ph;
    return std::static_pointer_cast<TextInputWidget>(shared_from_this());
  }

private:
  State<std::string> *boundStringState = nullptr;

  void notifyStateBinding() {
    if (boundStringState)
      boundStringState->set(inputValue);
  }

  int getCursorPosFromX(int pixelX) {
    if (inputValue.empty())
      return 0;

    HWND hwnd = FluxUI::getCurrentInstance()->getWindow();
    HDC hdc = GetDC(hwnd);
    FontCache &fc = FluxUI::getCurrentInstance()->getFontCache();

    HFONT hFont = fc.getFont(fontSize, fontWeight);
    HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);

    int bestPos = 0;
    int bestDist = abs(pixelX);

    for (int i = 1; i <= (int)inputValue.size(); i++) {
      SIZE sz;
      GetTextExtentPoint32(hdc, inputValue.c_str(), i, &sz);
      int dist = abs(sz.cx - pixelX);
      if (dist < bestDist) {
        bestDist = dist;
        bestPos = i;
      }
    }

    SelectObject(hdc, hOldFont);
    ReleaseDC(hwnd, hdc);
    return bestPos;
  }

  void updateScroll() {
    if (inputValue.empty()) {
      scrollOffset = 0;
      return;
    }

    HWND hwnd = FluxUI::getCurrentInstance()->getWindow();
    HDC hdc = GetDC(hwnd);
    FontCache &fc = FluxUI::getCurrentInstance()->getFontCache();

    HFONT hFont = fc.getFont(fontSize, fontWeight);
    HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);

    SIZE sz = {0};
    if (cursorPos > 0)
      GetTextExtentPoint32(hdc, inputValue.c_str(), cursorPos, &sz);

    SelectObject(hdc, hOldFont);
    ReleaseDC(hwnd, hdc);

    int textAreaWidth = width - paddingLeft - paddingRight;
    int cursorX = sz.cx - scrollOffset;

    if (cursorX < 10)
      scrollOffset = max(0, sz.cx - 10);
    else if (cursorX > textAreaWidth - 10)
      scrollOffset = sz.cx - textAreaWidth + 10;
  }
};

// ----------------------------------------------------------------
// Factory Functions
// ----------------------------------------------------------------
using TextInputWidgetPtr = std::shared_ptr<TextInputWidget>;

inline TextInputWidgetPtr TextInput(const std::string &placeholder = "") {
  auto w = std::make_shared<TextInputWidget>();
  if (!placeholder.empty())
    w->setPlaceholder(placeholder);
  return w;
}

#endif