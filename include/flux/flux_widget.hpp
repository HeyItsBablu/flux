#ifndef FLUX_WIDGET_HPP
#define FLUX_WIDGET_HPP

#include "flux_font.hpp"
#include "flux_overflow.hpp"
#include "flux_painter.hpp"
#include "flux_platform.hpp"

#include <cassert>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

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

// Use half of INT_MAX so that arithmetic like (maxWidth + maxHeight) never
// overflows a signed 32-bit integer.  Flutter uses double infinity; we use
// this large-but-safe sentinel instead.
static constexpr int kUnbounded = std::numeric_limits<int>::max() / 2;

struct BoxConstraints {
  int minWidth, maxWidth, minHeight, maxHeight;

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

  // Fix 1: use kUnbounded instead of the magic number 10000.
  static BoxConstraints infinite() {
    return BoxConstraints(0, kUnbounded, 0, kUnbounded);
  }

  // Fix 18: assert on inverted constraints in debug builds rather than
  // silently clamping them.  The clamp is kept as a release-mode fallback so
  // shipping builds never crash, but the assert surfaces the real bug early.
  void normalize() {
    minWidth  = std::max(0, minWidth);
    minHeight = std::max(0, minHeight);

    assert(maxWidth  >= minWidth  &&
           "BoxConstraints: maxWidth < minWidth — inverted constraint");
    assert(maxHeight >= minHeight &&
           "BoxConstraints: maxHeight < minHeight — inverted constraint");

    // Release-mode safety net: clamp instead of crashing.
    if (maxWidth  < minWidth)  maxWidth  = minWidth;
    if (maxHeight < minHeight) maxHeight = minHeight;
  }

  int clampWidth(int w) const {
    return std::max(minWidth, std::min(maxWidth, w));
  }
  int clampHeight(int h) const {
    return std::max(minHeight, std::min(maxHeight, h));
  }

  // Fix 20: preserve the incoming minWidth/minHeight after deflation.
  // Deflating padding must not discard a minimum size the parent already
  // established.  The deflated minimum is clamped to [0, deflated-max] so
  // the result is always valid.
  BoxConstraints deflate(int horizontal, int vertical) const {
    int newMaxW = std::max(0, maxWidth  - horizontal);
    int newMaxH = std::max(0, maxHeight - vertical);

    // Shrink the minimum by the same amount but never go negative or above
    // the new maximum.
    int newMinW = std::max(0, std::min(newMaxW, minWidth  - horizontal));
    int newMinH = std::max(0, std::min(newMaxH, minHeight - vertical));

    return BoxConstraints(newMinW, newMaxW, newMinH, newMaxH);
  }

  // Fix 19: log a warning when intersect() produces a result that is wider
  // than either of the two inputs, which indicates a programming error.
  BoxConstraints intersect(int wMin, int wMax, int hMin, int hMax) const {
    int newMinW = std::max(minWidth,  wMin);
    int newMaxW = std::min(maxWidth,  wMax);
    int newMinH = std::max(minHeight, hMin);
    int newMaxH = std::min(maxHeight, hMax);

    // Clamp so normalize() doesn't fire the assert for an empty intersection;
    // the caller's constraints are simply contradictory and we take the max.
    newMaxW = std::max(newMinW, newMaxW);
    newMaxH = std::max(newMinH, newMaxH);

#ifndef NDEBUG
    // Warn when the result exceeds either input range — this should never
    // happen geometrically and almost certainly means a logic error upstream.
    if (newMaxW > maxWidth || newMaxW > wMax) {
      std::cerr << "[BoxConstraints::intersect] WARNING: result maxWidth ("
                << newMaxW << ") exceeds both inputs (" << maxWidth
                << ", " << wMax << ") — check widget constraints.\n";
    }
    if (newMaxH > maxHeight || newMaxH > hMax) {
      std::cerr << "[BoxConstraints::intersect] WARNING: result maxHeight ("
                << newMaxH << ") exceeds both inputs (" << maxHeight
                << ", " << hMax << ") — check widget constraints.\n";
    }
#endif

    return BoxConstraints(newMinW, newMaxW, newMinH, newMaxH);
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
private:
  bool mounted = false;

public:
  std::string id;
  std::string text;

  // Layout
  int x = 0, y = 0;
  int width = 0, height = 0;
  int minWidth = 0, minHeight = 0;

  // Fix 2: use kUnbounded instead of 10000.  Widgets that genuinely want a
  // hard cap should call setMaxWidth()/setMaxHeight() explicitly.
  int maxWidth  = kUnbounded;
  int maxHeight = kUnbounded;

  bool autoWidth = true, autoHeight = true;
  bool visible = true;

  // Focus
  bool isFocusable = false;
  bool isFocused = false;

  // Flex
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

  // Colors — alpha is carried inside Color::a
  Color backgroundColor = Color::fromRGB(255, 255, 255);
  Color textColor = Color::fromRGB(0, 0, 0);
  Color borderColor = Color::fromRGB(0, 0, 0);

  bool hasBackground = false;
  bool hasBorder = false;

  // Hover colors
  Color hoverBackgroundColor = Color::fromRGB(255, 255, 255);
  Color hoverTextColor = Color::fromRGB(0, 0, 0);
  Color hoverBorderColor = Color::fromRGB(0, 0, 0);
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

  // Tree
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

  virtual WidgetPtr build() {
    return nullptr;
  }
  virtual void onMount() {}

  // -----------------------------------------------------------------------
  // Core layout / render virtuals
  // -----------------------------------------------------------------------

  virtual void computeLayout(GraphicsContext &ctx,
                             const BoxConstraints &constraints,
                             FontCache &fontCache);

  virtual void positionChildren(int contentX, int contentY, int contentWidth,
                                int contentHeight);

  virtual void render(GraphicsContext &ctx, FontCache &fontCache);

  void measureText(GraphicsContext &ctx, FontCache &fontCache);
  void renderText(GraphicsContext &ctx, FontCache &fontCache,
                  UINT format = DT_LEFT | DT_VCENTER | DT_SINGLELINE);

  void drawRoundedRectangle(GraphicsContext &ctx);

  virtual bool isTextInput() const { return false; }

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

  Color getCurrentBackgroundColor() const {
    return (isHovered && hasHoverBackground) ? hoverBackgroundColor
                                             : backgroundColor;
  }
  Color getCurrentTextColor() const {
    return (isHovered && hasHoverTextColor) ? hoverTextColor : textColor;
  }
  Color getCurrentBorderColor() const {
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
  // Constraint helpers
  // -----------------------------------------------------------------------

  BoxConstraints selfConstraints(const BoxConstraints &incoming) const {
    return incoming.intersect(minWidth, maxWidth, minHeight, maxHeight);
  }

  BoxConstraints contentConstraints(const BoxConstraints &incoming) const {
    return incoming.deflate(paddingLeft + paddingRight,
                            paddingTop + paddingBottom);
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
    if (width  < minWidth)  width  = minWidth;
    if (height < minHeight) height = minHeight;
    if (width  > maxWidth)  width  = maxWidth;
    if (height > maxHeight) height = maxHeight;
  }
};

// ============================================================================
// HIT TESTING
// ============================================================================

Widget *findWidgetAt(Widget *w, int x, int y);

// ============================================================================
// MOUSE EVENT HELPERS
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