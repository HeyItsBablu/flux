#ifndef FLUX_WIDGET_HPP
#define FLUX_WIDGET_HPP

#include "flux_font.hpp"
#include <functional>
#include <gdiplus.h>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>
#include <windows.h>
#pragma comment(lib, "gdiplus.lib")

// ============================================================================
// FORWARD DECLARATIONS
// ============================================================================

template <typename T> class State;

class Widget;

using WidgetPtr = std::shared_ptr<Widget>;
using ClickHandler = std::function<void()>;
using HoverHandler = std::function<void(bool)>;

// ============================================================================
// ENUMS
// ============================================================================

enum class Alignment { Start, Center, End, Stretch };

enum class MainAxisAlignment {
  Start,
  Center,
  End,
  SpaceBetween,
  SpaceAround,
  SpaceEvenly
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
  Alignment crossAlignment = Alignment::Start;
  MainAxisAlignment mainAxisAlignment = MainAxisAlignment::Start;
  int spacing = 0;

  // Colors
  COLORREF backgroundColor = RGB(255, 255, 255);
  COLORREF textColor = RGB(0, 0, 0);
  COLORREF borderColor = RGB(0, 0, 0);

  BYTE backgroundAlpha = 255;
  BYTE borderAlpha = 255;
  BYTE textAlpha = 255;

  bool hasBackground = false;
  bool hasBorder = false;

  // Hover colors (only used if hasHover* flags are set)
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

  // State binding
  void *boundState = nullptr;

  virtual ~Widget() = default;

  virtual bool isExpanded() const { return false; }

  virtual void onDetach() {
    // Propagate to children so the whole sub-tree is cleaned up
    for (auto &child : children)
      child->onDetach();
  }

  // Virtual methods - Override these in subclasses
  virtual void computeLayout(HDC hdc, int availableWidth, int availableHeight,
                             FontCache &fontCache);
  virtual void positionChildren(int contentX, int contentY, int contentWidth,
                                int contentHeight);
  virtual void render(HDC hdc, FontCache &fontCache);
  void measureText(HDC hdc, FontCache &fontCache);
  void renderText(HDC hdc, FontCache &fontCache,
                  UINT format = DT_LEFT | DT_VCENTER | DT_SINGLELINE);

  // Mouse event handlers - Override these for interactive widgets
  virtual bool handleMouseWheel(int delta) { return false; }
  virtual bool handleMouseDown(int mx, int my) { return false; }
  virtual bool handleMouseUp(int mx, int my) { return false; }
  virtual bool handleMouseMove(int mx, int my) { return false; }
  virtual bool handleMouseLeave() { return false; }

  virtual bool handleRightClick(int mx, int my) {
    if (onRightClick)
      return onRightClick(mx, my);
    return false;
  }

  // New virtual handlers
  virtual bool handleKeyDown(int keyCode) { return false; }
  virtual bool handleChar(wchar_t ch) { return false; }
  virtual bool handleFocus(bool focused) {
    isFocused = focused;
    markNeedsPaint();
    return true;
  }
  virtual bool handleTimer(UINT timerId) { return false; }

  // Hover handling
  bool updateHoverState(int mouseX, int mouseY) {
    bool nowHovered = (mouseX >= x && mouseX < x + width && mouseY >= y &&
                       mouseY < y + height);

    if (nowHovered != isHovered) {
      isHovered = nowHovered;
      if (onHover) {
        onHover(isHovered);
      }
      markNeedsPaint();
      return true;
    }
    return false;
  }

  void clearHoverState() {
    if (isHovered) {
      isHovered = false;
      if (onHover) {
        onHover(false);
      }
      markNeedsPaint();
    }
    for (auto &child : children) {
      child->clearHoverState();
    }
  }

  // Get current colors (applying hover if active)
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

  // Mark this widget and all parents as needing layout
  void markNeedsLayout() {
    needsLayout = true;
    needsPaint = true;
    if (parent) {
      parent->markNeedsLayout();
    }
  }

  void markNeedsPaint() { needsPaint = true; }

  WidgetPtr setId(const std::string &i) {
    id = i;
    return shared_from_this();
  }

  void addChild(WidgetPtr child) {
    children.push_back(child);
    child->parent = this;
    markNeedsLayout();
  }

  const std::string &getText() const { return text; }
  const std::string &getId() const { return id; }

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

  void drawRoundedRectangle(HDC hdc) {
    COLORREF bgColor = getCurrentBackgroundColor();
    COLORREF bdColor = getCurrentBorderColor();

    Gdiplus::Graphics g(hdc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

    auto makeRoundedPath = [&](Gdiplus::GraphicsPath &path, int left, int top,
                               int w, int h, int r) {
      int d = r * 2;
      path.AddArc(left, top, d, d, 180, 90);
      path.AddArc(left + w - d, top, d, d, 270, 90);
      path.AddArc(left + w - d, top + h - d, d, d, 0, 90);
      path.AddArc(left, top + h - d, d, d, 90, 90);
      path.CloseFigure();
    };

    // Fill background
    if (hasBackground) {
      Gdiplus::Color fillColor(backgroundAlpha, GetRValue(bgColor),
                               GetGValue(bgColor), GetBValue(bgColor));
      Gdiplus::SolidBrush brush(fillColor);

      if (borderRadius > 0) {
        Gdiplus::GraphicsPath path;
        makeRoundedPath(path, x, y, width, height, borderRadius);
        g.FillPath(&brush, &path);
      } else {
        Gdiplus::RectF rect((Gdiplus::REAL)x, (Gdiplus::REAL)y,
                            (Gdiplus::REAL)width, (Gdiplus::REAL)height);
        g.FillRectangle(&brush, rect);
      }
    }

    // Draw border
    if (hasBorder) {
      Gdiplus::Color strokeColor(borderAlpha, GetRValue(bdColor),
                                 GetGValue(bdColor), GetBValue(bdColor));
      Gdiplus::Pen pen(strokeColor, (Gdiplus::REAL)borderWidth);

      if (borderRadius > 0) {
        Gdiplus::GraphicsPath path;
        makeRoundedPath(path, x, y, width, height, borderRadius);
        g.DrawPath(&pen, &path);
      } else {
        Gdiplus::RectF rect((Gdiplus::REAL)x, (Gdiplus::REAL)y,
                            (Gdiplus::REAL)width, (Gdiplus::REAL)height);
        g.DrawRectangle(&pen, rect);
      }
    }
  }
};

// ============================================================================
// VIRTUAL METHOD IMPLEMENTATIONS (need FontCache declaration)
// ============================================================================

inline void Widget::measureText(HDC hdc, FontCache &fontCache) {
  if (text.empty()) {
    width = 0;
    height = 0;
    return;
  }

  HFONT hFont = fontCache.getFont(fontSize, fontWeight);
  HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);

  // UTF-8 → UTF-16
  int wlen = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
  std::wstring wtext(wlen, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, wtext.data(), wlen);

  SIZE size;
  GetTextExtentPoint32W(hdc, wtext.c_str(), (int)wtext.size() - 1, &size);

  if (autoWidth)
    width = size.cx + paddingLeft + paddingRight;
  if (autoHeight)
    height = size.cy + paddingTop + paddingBottom;

  SelectObject(hdc, hOldFont);
}
inline void Widget::renderText(HDC hdc, FontCache &fontCache, UINT format) {
  if (text.empty())
    return;

  SetTextColor(hdc, getCurrentTextColor());
  SetBkMode(hdc, TRANSPARENT);

  HFONT hFont = fontCache.getFont(fontSize, fontWeight);
  HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);

  RECT textRect = {x + paddingLeft, y + paddingTop, x + width - paddingRight,
                   y + height - paddingBottom};

  // Convert UTF-8 → UTF-16 so DrawTextW handles all unicode correctly
  int wlen = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
  std::wstring wtext(wlen, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, wtext.data(), wlen);

  DrawTextW(hdc, wtext.c_str(), -1, &textRect, format);

  SelectObject(hdc, hOldFont);
}

inline void Widget::computeLayout(HDC hdc, int availableWidth,
                                  int availableHeight, FontCache &fontCache) {
  // Default: just apply constraints
  applyConstraints();
  needsLayout = false;
}

inline void Widget::positionChildren(int contentX, int contentY,
                                     int contentWidth, int contentHeight) {
  // Default: position children at content origin
  for (auto &child : children) {
    child->x = contentX + child->marginLeft;
    child->y = contentY + child->marginTop;

    child->positionChildren(
        child->x + child->paddingLeft, child->y + child->paddingTop,
        child->width - child->paddingLeft - child->paddingRight,
        child->height - child->paddingTop - child->paddingBottom);
  }
}

inline void Widget::render(HDC hdc, FontCache &fontCache) {
  // Default: draw background if has one
  if (hasBackground) {
    drawRoundedRectangle(hdc);
  }

  // Render all children
  for (auto &child : children) {
    child->render(hdc, fontCache);
  }

  needsPaint = false;
}

// ============================================================================
// HIT TESTING
// ============================================================================

inline Widget *findWidgetAt(Widget *w, int x, int y) {
  if (!w)
    return nullptr;

  for (auto it = w->children.rbegin(); it != w->children.rend(); ++it) {
    Widget *found = findWidgetAt(it->get(), x, y);
    if (found)
      return found;
  }

  if (x >= w->x && x < w->x + w->width && y >= w->y && y < w->y + w->height) {
    return w;
  }

  return nullptr;
}

// ============================================================================
// MOUSE EVENT HELPER FUNCTIONS
// ============================================================================

/**
 * Find widget at position and dispatch mouse event
 * Returns true if event was handled
 */
template <typename Handler>
inline bool findAndHandleMouseEvent(Widget *widget, int x, int y,
                                    Handler handler) {
  if (!widget)
    return false;

  // Check if point is within widget bounds
  if (x >= widget->x && x < widget->x + widget->width && y >= widget->y &&
      y < widget->y + widget->height) {
    // Try children first (they're on top)
    for (auto it = widget->children.rbegin(); it != widget->children.rend();
         ++it) {
      if (findAndHandleMouseEvent(it->get(), x, y, handler))
        return true;
    }

    // Then try this widget
    if (handler(widget))
      return true;
  }

  return false;
}

/**
 * Update hover state for all widgets in tree
 * Returns true if any widget changed hover state
 */
inline bool updateHoverStates(Widget *widget, int mouseX, int mouseY) {
  if (!widget)
    return false;

  bool changed = false;

  // Check if mouse is over this widget
  bool isOver = (mouseX >= widget->x && mouseX < widget->x + widget->width &&
                 mouseY >= widget->y && mouseY < widget->y + widget->height);

  if (isOver) {
    // Update this widget's hover state
    changed |= widget->updateHoverState(mouseX, mouseY);

    // Update children
    for (auto &child : widget->children) {
      changed |= updateHoverStates(child.get(), mouseX, mouseY);
    }
  } else {
    // Mouse not over this widget - clear hover state
    if (widget->isHovered) {
      widget->clearHoverState();
      changed = true;
    }
  }

  return changed;
}

#endif // FLUX_WIDGET_HPP