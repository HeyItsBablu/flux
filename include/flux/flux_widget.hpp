#ifndef FLUX_WIDGET_HPP
#define FLUX_WIDGET_HPP

#include "flux_font.hpp"
#include "flux_overflow.hpp"
#include <functional>
#include <gdiplus.h>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>
#include "flux_platform.hpp"

// ============================================================================
// FORWARD DECLARATIONS
// ============================================================================

template <typename T> class State;

class Widget;

using WidgetPtr = std::shared_ptr<Widget>;
using ClickHandler = std::function<void()>;
using HoverHandler = std::function<void(bool)>;

// ============================================================================
// BOX CONSTRAINTS
// ============================================================================

struct BoxConstraints {
  int minWidth;
  int maxWidth;
  int minHeight;
  int maxHeight;

  BoxConstraints(int minW, int maxW, int minH, int maxH)
      : minWidth(minW), maxWidth(maxW), minHeight(minH), maxHeight(maxH) {
    normalize();
  }

  static BoxConstraints tight(int w, int h) {
    return BoxConstraints(w, w, h, h);
  }

  static BoxConstraints loose(int w, int h) {
    return BoxConstraints(0, w, 0, h);
  }

  static BoxConstraints infinite() {
    return BoxConstraints(0, 10000, 0, 10000);
  }

  void normalize() {
    minWidth = max(0, minWidth);
    minHeight = max(0, minHeight);
    if (maxWidth < minWidth)
      maxWidth = minWidth;
    if (maxHeight < minHeight)
      maxHeight = minHeight;
  }

  int clampWidth(int w) const { return max(minWidth, min(maxWidth, w)); }
  int clampHeight(int h) const { return max(minHeight, min(maxHeight, h)); }

  BoxConstraints deflate(int horizontal, int vertical) const {
    return BoxConstraints(0, max(0, maxWidth - horizontal), 0,
                          max(0, maxHeight - vertical));
  }

  BoxConstraints intersect(int wMin, int wMax, int hMin, int hMax) const {
    int newMinW = max(minWidth, wMin);
    int newMaxW = min(maxWidth, wMax);
    int newMinH = max(minHeight, hMin);
    int newMaxH = min(maxHeight, hMax);
    return BoxConstraints(newMinW, max(newMinW, newMaxW), newMinH,
                          max(newMinH, newMaxH));
  }
};

// ============================================================================
// ENUMS
// ============================================================================

enum class Alignment {
  Start,
  Center,
  End,
  Stretch,
  TopCenter,
  BottomCenter,
  CenterLeft,
  CenterRight,
  TopRight,
  BottomLeft,
  BottomRight
};

enum class CrossAxisAlignment {
  Start,
  Center,
  End,
  SpaceBetween,
  SpaceAround,
  SpaceEvenly,
  Stretch
};

enum class MainAxisAlignment {
  Start,
  Center,
  End,
  SpaceBetween,
  SpaceAround,
  SpaceEvenly,
  Stretch
};

// ============================================================================
// WIDGET BASE CLASS
// ============================================================================

class Widget : public std::enable_shared_from_this<Widget> {
public:
  std::string id;
  std::string text;

  // Layout properties
  int x = 0, y = 0;
  int width = 0, height = 0;
  int minWidth = 0, minHeight = 0;
  int maxWidth = 10000, maxHeight = 10000;
  bool autoWidth = true, autoHeight = true;
  bool visible = true;

  // Focus support
  bool isFocusable = false;
  bool isFocused = false;

  // Flex property for Expanded widget
  int flex = 1;

  // Spacing
  int padding = 0;
  int paddingLeft = 0, paddingRight = 0, paddingTop = 0, paddingBottom = 0;
  int margin = 0;
  int marginLeft = 0, marginRight = 0, marginTop = 0, marginBottom = 0;

  // Alignment
  Alignment alignment = Alignment::Start;
  CrossAxisAlignment crossAxisAlignment = CrossAxisAlignment::Start;
  MainAxisAlignment mainAxisAlignment = MainAxisAlignment::Start;
  int spacing = 0;

  OverflowInfo overflow;

  // Colors
  COLORREF backgroundColor = RGB(255, 255, 255);
  COLORREF textColor = RGB(0, 0, 0);
  COLORREF borderColor = RGB(0, 0, 0);

  BYTE backgroundAlpha = 255;
  BYTE borderAlpha = 255;
  BYTE textAlpha = 255;

  bool hasBackground = false;
  bool hasBorder = false;

  // Hover colors
  COLORREF hoverBackgroundColor = RGB(255, 255, 255);
  COLORREF hoverTextColor = RGB(0, 0, 0);
  COLORREF hoverBorderColor = RGB(0, 0, 0);
  bool hasHoverBackground = false;
  bool hasHoverTextColor = false;
  bool hasHoverBorderColor = false;

  // Border
  int borderWidth = 1;
  int borderRadius = 0;

  // Text styling
  int fontSize = 14;
  FontWeight fontWeight = FontWeight::Normal;
  std::string fontFamily = "Segoe UI";

  // Events
  ClickHandler onClick;
  HoverHandler onHover;
  std::function<bool(int, int)> onRightClick;

  // Hover state
  bool isHovered = false;

  // Dirty flags
  bool needsLayout = true;
  bool needsPaint = true;

  // Children
  std::vector<WidgetPtr> children;
  Widget *parent = nullptr;

  virtual ~Widget() = default;

  virtual bool isExpanded() const { return false; }

  virtual void onDetach() {
    for (auto &child : children) {
      child->parent = nullptr;
      child->onDetach();
    }
  }

  // -----------------------------------------------------------------------
  // Core layout / render virtuals
  // -----------------------------------------------------------------------

  virtual void computeLayout(HDC hdc, const BoxConstraints &constraints,
                             FontCache &fontCache);

  virtual void positionChildren(int contentX, int contentY, int contentWidth,
                                int contentHeight);

  virtual void render(HDC hdc, FontCache &fontCache);

  void measureText(HDC hdc, FontCache &fontCache);
  void renderText(HDC hdc, FontCache &fontCache,
                  UINT format = DT_LEFT | DT_VCENTER | DT_SINGLELINE);

  // -----------------------------------------------------------------------
  // Mouse / keyboard event handlers
  // -----------------------------------------------------------------------

  virtual bool handleMouseWheel(int /*delta*/) { return false; }
  virtual bool handleMouseDown(int /*mx*/, int /*my*/) { return false; }
  virtual bool handleMouseUp(int /*mx*/, int /*my*/) { return false; }
  virtual bool handleMouseMove(int /*mx*/, int /*my*/) { return false; }
  virtual bool handleMouseLeave() { return false; }

  virtual bool handleRightClick(int mx, int my) {
    if (onRightClick)
      return onRightClick(mx, my);
    return false;
  }

  virtual bool handleKeyDown(int /*keyCode*/) { return false; }
  virtual bool handleChar(wchar_t /*ch*/) { return false; }
  virtual bool handleTimer(UINT /*timerId*/) { return false; }
  virtual bool handleFocus(bool focused) {
    isFocused = focused;
    markNeedsPaint();
    return true;
  }

  // -----------------------------------------------------------------------
  // Hover helpers
  // -----------------------------------------------------------------------

  bool updateHoverState(int mouseX, int mouseY) {
    bool nowHovered = (mouseX >= x && mouseX < x + width && mouseY >= y &&
                       mouseY < y + height);
    if (nowHovered != isHovered) {
      isHovered = nowHovered;
      if (onHover)
        onHover(isHovered);
      markNeedsPaint();
      return true;
    }
    return false;
  }

  void clearHoverState() {
    if (isHovered) {
      isHovered = false;
      if (onHover)
        onHover(false);
      markNeedsPaint();
    }
    for (auto &child : children)
      child->clearHoverState();
  }

  COLORREF getCurrentBackgroundColor() const {
    return (isHovered && hasHoverBackground) ? hoverBackgroundColor
                                             : backgroundColor;
  }
  COLORREF getCurrentTextColor() const {
    return (isHovered && hasHoverTextColor) ? hoverTextColor : textColor;
  }
  COLORREF getCurrentBorderColor() const {
    return (isHovered && hasHoverBorderColor) ? hoverBorderColor : borderColor;
  }

  // -----------------------------------------------------------------------
  // Dirty tracking
  // -----------------------------------------------------------------------

  void markNeedsLayout() {
    needsLayout = true;
    needsPaint = true;
    if (parent)
      parent->markNeedsLayout();
  }

  virtual void markNeedsPaint() { needsPaint = true; }

  // -----------------------------------------------------------------------
  // Tree helpers
  // -----------------------------------------------------------------------

  WidgetPtr setId(const std::string &i) {
    id = i;
    return shared_from_this();
  }

  void addChild(WidgetPtr child) {
    if (!child)
      return;
    children.push_back(child);
    child->parent = this;
    markNeedsLayout();
  }

  const std::string &getText() const { return text; }
  const std::string &getId() const { return id; }

  // -----------------------------------------------------------------------
  // Constraint helpers for subclasses
  // -----------------------------------------------------------------------

  BoxConstraints selfConstraints(const BoxConstraints &incoming) const {
    return incoming.intersect(minWidth, maxWidth, minHeight, maxHeight);
  }

  BoxConstraints contentConstraints(const BoxConstraints &incoming) const {
    int padH = paddingLeft + paddingRight;
    int padV = paddingTop + paddingBottom;
    return incoming.deflate(padH, padV);
  }

protected:
  template <typename T> static std::string valueToString(const T &val) {
    if constexpr (std::is_same_v<T, std::string>)
      return val;
    else if constexpr (std::is_same_v<T, bool>)
      return val ? "true" : "false";
    else if constexpr (std::is_floating_point_v<T>) {
      std::ostringstream oss;
      oss << std::fixed << std::setprecision(2) << val;
      return oss.str();
    } else if constexpr (std::is_integral_v<T>)
      return std::to_string(val);
    else
      return "[unsupported type]";
  }

  void applyConstraints() {
    if (width < minWidth)
      width = minWidth;
    if (height < minHeight)
      height = minHeight;
    if (width > maxWidth)
      width = maxWidth;
    if (height > maxHeight)
      height = maxHeight;
  }

  void drawRoundedRectangle(HDC hdc);
};

// ============================================================================
// HIT TESTING
// ============================================================================

Widget *findWidgetAt(Widget *w, int x, int y);

// ============================================================================
// MOUSE EVENT HELPER FUNCTIONS
// ============================================================================

template <typename Handler>
inline bool findAndHandleMouseEvent(Widget *widget, int x, int y,
                                    Handler handler) {
  if (!widget || !widget->visible)
    return false;
  if (x >= widget->x && x < widget->x + widget->width && y >= widget->y &&
      y < widget->y + widget->height) {
    for (auto it = widget->children.rbegin(); it != widget->children.rend();
         ++it) {
      if (findAndHandleMouseEvent(it->get(), x, y, handler))
        return true;
    }
    if (handler(widget))
      return true;
  }
  return false;
}

bool updateHoverStates(Widget *widget, int mouseX, int mouseY);

#endif // FLUX_WIDGET_HPP