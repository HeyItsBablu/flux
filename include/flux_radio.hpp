#ifndef FLUX_RADIO_HPP
#define FLUX_RADIO_HPP

#include "flux_core.hpp"
#include "flux_state.hpp"
#include <algorithm>

template <typename T> class State;

class Widget;

using WidgetPtr = std::shared_ptr<Widget>;

// Forward declaration
class RadioGroupWidget;

// ============================================================================
// RADIO BUTTON WIDGET (Individual radio button)
// ============================================================================

class RadioButtonWidget : public Widget {
public:
  bool selected = false;
  int circleSize = 16;
  int innerCircleSize = 8;
  std::string value; // The value this radio represents

  COLORREF circleColor = RGB(150, 150, 150);
  COLORREF selectedCircleColor = RGB(33, 150, 243);
  COLORREF innerCircleColor = RGB(33, 150, 243);
  COLORREF hoverCircleColor = RGB(100, 100, 100);

  RadioGroupWidget *parentGroup = nullptr;

  RadioButtonWidget(const std::string &val = "") : value(val) {
    paddingLeft = paddingRight = 4;
    paddingTop = paddingBottom = 4;
  }

  void computeLayout(HDC hdc, int availableWidth, int availableHeight,
                     FontCache &fontCache) override {
    if (!text.empty()) {
      measureText(hdc, fontCache);
      width = circleSize + 8 + width;
    } else {
      width = circleSize;
    }

    height = max(circleSize, height);

    width += paddingLeft + paddingRight;
    height += paddingTop + paddingBottom;

    applyConstraints();
    needsLayout = false;
  }

  void render(HDC hdc, FontCache &fontCache) override {
    int circleX = x + paddingLeft + circleSize / 2;
    int circleY = y + paddingTop + (height - paddingTop - paddingBottom) / 2;

    // Determine circle color
    COLORREF currentCircleColor =
        selected ? selectedCircleColor
                 : (isHovered ? hoverCircleColor : circleColor);

    // Draw outer circle
    HBRUSH circleBrush = CreateSolidBrush(RGB(255, 255, 255));
    HPEN circlePen = CreatePen(PS_SOLID, 2, currentCircleColor);

    HPEN oldPen = (HPEN)SelectObject(hdc, circlePen);
    HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, circleBrush);

    Ellipse(hdc, circleX - circleSize / 2, circleY - circleSize / 2,
            circleX + circleSize / 2, circleY + circleSize / 2);

    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(circleBrush);
    DeleteObject(circlePen);

    // Draw inner circle if selected
    if (selected) {
      HBRUSH innerBrush = CreateSolidBrush(innerCircleColor);
      HPEN innerPen = CreatePen(PS_NULL, 0, 0);

      oldPen = (HPEN)SelectObject(hdc, innerPen);
      oldBrush = (HBRUSH)SelectObject(hdc, innerBrush);

      Ellipse(hdc, circleX - innerCircleSize / 2, circleY - innerCircleSize / 2,
              circleX + innerCircleSize / 2, circleY + innerCircleSize / 2);

      SelectObject(hdc, oldBrush);
      SelectObject(hdc, oldPen);
      DeleteObject(innerBrush);
      DeleteObject(innerPen);
    }

    // Draw label text
    if (!text.empty()) {
      RECT textRect = {x + paddingLeft + circleSize + 8, y + paddingTop,
                       x + width - paddingRight, y + height - paddingBottom};

      SetTextColor(hdc, getCurrentTextColor());
      SetBkMode(hdc, TRANSPARENT);

      HFONT hFont = fontCache.getFont(fontSize, fontWeight);
      HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);

      DrawText(hdc, text.c_str(), -1, &textRect,
               DT_LEFT | DT_VCENTER | DT_SINGLELINE);

      SelectObject(hdc, hOldFont);
    }

    needsPaint = false;
  }

  bool handleMouseDown(int mx, int my) override {
    if (mx >= x && mx < x + width && my >= y && my < y + height) {
      selectThis();
      return true;
    }
    return false;
  }

  bool handleKeyDown(int keyCode) override {
    // Space or Enter to select
    if (keyCode == VK_SPACE || keyCode == VK_RETURN) {
      selectThis();
      return true;
    }
    return false;
  }

  void selectThis();

  std::shared_ptr<RadioButtonWidget> setSelected(bool sel) {
    selected = sel;
    markNeedsPaint();
    return std::static_pointer_cast<RadioButtonWidget>(shared_from_this());
  }

  std::shared_ptr<RadioButtonWidget> setValue(const std::string &val) {
    value = val;
    return std::static_pointer_cast<RadioButtonWidget>(shared_from_this());
  }

  std::shared_ptr<RadioButtonWidget> setCircleColor(COLORREF color) {
    circleColor = color;
    markNeedsPaint();
    return std::static_pointer_cast<RadioButtonWidget>(shared_from_this());
  }

  std::shared_ptr<RadioButtonWidget> setSelectedCircleColor(COLORREF color) {
    selectedCircleColor = color;
    innerCircleColor = color;
    markNeedsPaint();
    return std::static_pointer_cast<RadioButtonWidget>(shared_from_this());
  }
};

// ============================================================================
// RADIO GROUP WIDGET (Container for radio buttons)
// ============================================================================

class RadioGroupWidget : public Widget {
public:
  std::string selectedValue;
  std::vector<RadioButtonWidget *> radioButtons;

  bool isVertical = true;

  std::function<void(const std::string &)> onSelectionChanged;

  RadioGroupWidget() { spacing = 8; }

  void computeLayout(HDC hdc, int availableWidth, int availableHeight,
                     FontCache &fontCache) override {
    int totalWidth = 0;
    int totalHeight = 0;
    int maxWidth = 0;
    int maxHeight = 0;

    // First, compute layout for all children
    for (auto &child : children) {
      child->computeLayout(hdc, availableWidth, availableHeight, fontCache);

      if (isVertical) {
        totalHeight += child->height + child->marginTop + child->marginBottom;
        maxWidth = max(maxWidth,
                       child->width + child->marginLeft + child->marginRight);
      } else {
        totalWidth += child->width + child->marginLeft + child->marginRight;
        maxHeight = max(maxHeight,
                        child->height + child->marginTop + child->marginBottom);
      }
    }

    // Add spacing between items
    int spacingTotal =
        children.empty() ? 0 : (int)(children.size() - 1) * spacing;

    if (isVertical) {
      totalHeight += spacingTotal;
      width = autoWidth ? (maxWidth + paddingLeft + paddingRight) : width;
      height = autoHeight ? (totalHeight + paddingTop + paddingBottom) : height;
    } else {
      totalWidth += spacingTotal;
      width = autoWidth ? (totalWidth + paddingLeft + paddingRight) : width;
      height = autoHeight ? (maxHeight + paddingTop + paddingBottom) : height;
    }

    applyConstraints();
    needsLayout = false;
  }

  void positionChildren(int contentX, int contentY, int contentWidth,
                        int contentHeight) override {
    int currentX = contentX;
    int currentY = contentY;

    for (auto &child : children) {
      child->x = currentX + child->marginLeft;
      child->y = currentY + child->marginTop;

      if (isVertical) {
        currentY +=
            child->height + child->marginTop + child->marginBottom + spacing;
      } else {
        currentX +=
            child->width + child->marginLeft + child->marginRight + spacing;
      }

      child->positionChildren(
          child->x + child->paddingLeft, child->y + child->paddingTop,
          child->width - child->paddingLeft - child->paddingRight,
          child->height - child->paddingTop - child->paddingBottom);
    }
  }

  void render(HDC hdc, FontCache &fontCache) override {
    // Render background if needed
    if (hasBackground) {
      drawRoundedRectangle(hdc);
    }

    // Render all children
    for (auto &child : children) {
      child->render(hdc, fontCache);
    }

    needsPaint = false;
  }

  void addRadioButton(std::shared_ptr<RadioButtonWidget> radio) {
    radio->parentGroup = this;
    radioButtons.push_back(radio.get());
    addChild(radio);

    // If this is the first button or it matches selectedValue, select it
    if (radioButtons.size() == 1 || radio->value == selectedValue) {
      radio->selected = true;
      selectedValue = radio->value;
    }
  }

  void selectRadioButton(RadioButtonWidget *selectedRadio) {
    if (!selectedRadio)
      return;

    // Deselect all radio buttons
    for (auto *radio : radioButtons) {
      if (radio != selectedRadio && radio->selected) {
        radio->selected = false;
        radio->markNeedsPaint();
      }
    }

    // Select the clicked one
    if (!selectedRadio->selected) {
      selectedRadio->selected = true;
      selectedRadio->markNeedsPaint();
    }

    // Update selected value
    selectedValue = selectedRadio->value;

    // Notify callback
    if (onSelectionChanged) {
      onSelectionChanged(selectedValue);
    }

    // Notify state binding
    if (boundStringState) {
      boundStringState->set(selectedValue);
    }
  }

  std::shared_ptr<RadioGroupWidget> setOrientation(bool vertical) {
    isVertical = vertical;
    markNeedsLayout();
    return std::static_pointer_cast<RadioGroupWidget>(shared_from_this());
  }

  std::shared_ptr<RadioGroupWidget> setHorizontal() {
    return setOrientation(false);
  }

  std::shared_ptr<RadioGroupWidget> setVertical() {
    return setOrientation(true);
  }

  std::shared_ptr<RadioGroupWidget> setSelectedValue(const std::string &value) {
    selectedValue = value;

    // Update radio buttons to match
    for (auto *radio : radioButtons) {
      radio->selected = (radio->value == value);
      radio->markNeedsPaint();
    }

    return std::static_pointer_cast<RadioGroupWidget>(shared_from_this());
  }

  std::shared_ptr<RadioGroupWidget>
  setOnSelectionChanged(std::function<void(const std::string &)> callback) {
    onSelectionChanged = callback;
    return std::static_pointer_cast<RadioGroupWidget>(shared_from_this());
  }

  std::shared_ptr<RadioGroupWidget> bindValue(State<std::string> &state) {
    selectedValue = state.get();

    // Update radio buttons to match initial state
    for (auto *radio : radioButtons) {
      radio->selected = (radio->value == selectedValue);
      radio->markNeedsPaint();
    }

    state.bindProperty(
        shared_from_this(),
        [](Widget *w, const std::string &val) {
          auto *group = static_cast<RadioGroupWidget *>(w);
          group->selectedValue = val;

          // Update radio buttons
          for (auto *radio : group->radioButtons) {
            radio->selected = (radio->value == val);
            radio->markNeedsPaint();
          }
        },
        false);

    boundStringState = &state;

    return std::static_pointer_cast<RadioGroupWidget>(shared_from_this());
  }

  std::string getSelectedValue() const { return selectedValue; }

private:
  State<std::string> *boundStringState = nullptr;
};

// Now we can implement RadioButtonWidget::selectThis()
inline void RadioButtonWidget::selectThis() {
  if (parentGroup) {
    parentGroup->selectRadioButton(this);
  } else {
    // Standalone radio button (not recommended, but handle it)
    selected = !selected;
    markNeedsPaint();

    if (onClick)
      onClick();
  }
}

// ============================================================================
// FACTORY FUNCTIONS
// ============================================================================

using RadioButtonWidgetPtr = std::shared_ptr<RadioButtonWidget>;
using RadioGroupWidgetPtr = std::shared_ptr<RadioGroupWidget>;

inline RadioButtonWidgetPtr RadioButton(const std::string &value,
                                        const std::string &label = "") {
  auto w = std::make_shared<RadioButtonWidget>(value);
  w->text = label.empty() ? value : label;
  w->textColor = RGB(30, 30, 30);
  return w;
}

inline RadioGroupWidgetPtr RadioGroup() {
  return std::make_shared<RadioGroupWidget>();
}

// ============================================================================
// CONVENIENCE BUILDER FOR RADIO GROUP WITH OPTIONS
// ============================================================================

// Helper struct to distinguish between value-label pairs and simple options
struct RadioOption {
  std::string value;
  std::string label;

  RadioOption(const std::string &v, const std::string &l)
      : value(v), label(l) {}
  RadioOption(const char *v, const char *l) : value(v), label(l) {}
};

inline RadioGroupWidgetPtr
RadioGroupWithOptions(const std::initializer_list<RadioOption> &options) {
  auto group = std::make_shared<RadioGroupWidget>();

  for (const auto &option : options) {
    auto radio = RadioButton(option.value, option.label);
    group->addRadioButton(radio);
  }

  return group;
}

// Overload for simple string options (value = label)
inline RadioGroupWidgetPtr
RadioGroupWithOptions(const std::initializer_list<std::string> &options) {
  auto group = std::make_shared<RadioGroupWidget>();

  for (const auto &option : options) {
    auto radio = RadioButton(option, option);
    group->addRadioButton(radio);
  }

  return group;
}

#endif // FLUX_RADIO_HPP