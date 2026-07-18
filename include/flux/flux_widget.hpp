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

template <typename T>
class State;

class Widget;

using WidgetPtr = std::shared_ptr<Widget>;
using ClickHandler = std::function<void()>;
using HoverHandler = std::function<void(bool)>;

// ============================================================================
// BOX CONSTRAINTS
// ============================================================================

static constexpr int kUnbounded = std::numeric_limits<int>::max() / 2;

struct BoxConstraints
{
  int minWidth, maxWidth, minHeight, maxHeight;

  BoxConstraints(int minW, int maxW, int minH, int maxH)
      : minWidth(minW), maxWidth(maxW), minHeight(minH), maxHeight(maxH)
  {
    normalize();
  }

  static BoxConstraints tight(int w, int h)
  {
    return BoxConstraints(w, w, h, h);
  }
  static BoxConstraints loose(int w, int h)
  {
    return BoxConstraints(0, w, 0, h);
  }

  static BoxConstraints infinite()
  {
    return BoxConstraints(0, kUnbounded, 0, kUnbounded);
  }

  void normalize()
  {
    minWidth = std::max(0, minWidth);
    minHeight = std::max(0, minHeight);

    assert(maxWidth >= minWidth &&
           "BoxConstraints: maxWidth < minWidth — inverted constraint");
    assert(maxHeight >= minHeight &&
           "BoxConstraints: maxHeight < minHeight — inverted constraint");

    // Release-mode safety net: clamp instead of crashing.
    if (maxWidth < minWidth)
      maxWidth = minWidth;
    if (maxHeight < minHeight)
      maxHeight = minHeight;
  }

  int clampWidth(int w) const
  {
    return std::max(minWidth, std::min(maxWidth, w));
  }
  int clampHeight(int h) const
  {
    return std::max(minHeight, std::min(maxHeight, h));
  }

  BoxConstraints deflate(int horizontal, int vertical) const
  {
    int newMaxW = std::max(0, maxWidth - horizontal);
    int newMaxH = std::max(0, maxHeight - vertical);

    int newMinW = std::max(0, std::min(newMaxW, minWidth - horizontal));
    int newMinH = std::max(0, std::min(newMaxH, minHeight - vertical));

    return BoxConstraints(newMinW, newMaxW, newMinH, newMaxH);
  }

  BoxConstraints intersect(int wMin, int wMax, int hMin, int hMax) const
  {
    int newMinW = std::max(minWidth, wMin);
    int newMaxW = std::min(maxWidth, wMax);
    int newMinH = std::max(minHeight, hMin);
    int newMaxH = std::min(maxHeight, hMax);

    newMaxW = std::max(newMinW, newMaxW);
    newMaxH = std::max(newMinH, newMaxH);

#ifndef NDEBUG

    if (newMaxW > maxWidth || newMaxW > wMax)
    {
      std::cerr << "[BoxConstraints::intersect] WARNING: result maxWidth ("
                << newMaxW << ") exceeds both inputs (" << maxWidth
                << ", " << wMax << ") — check widget constraints.\n";
    }
    if (newMaxH > maxHeight || newMaxH > hMax)
    {
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

enum class Alignment
{
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

enum class CrossAxisAlignment
{
  Start,
  Center,
  End,
  SpaceBetween,
  SpaceAround,
  SpaceEvenly,
  Stretch
};

enum class MainAxisAlignment
{
  Start,
  Center,
  End,
  SpaceBetween,
  SpaceAround,
  SpaceEvenly,
  Stretch
};

enum class SizeMode
{
  Fixed,
  Fit,
  Full
};

// ============================================================================
// POSITION — pulls a widget out of (or keeps it in) normal flow.
//
//   Static   — default. Participates in whatever layout algorithm its parent
//              container runs (Flex / Grid / Block main-axis flow).
//   Relative — same as Static for sizing/flow purposes; reserved so callers
//              can later add left/top-style nudges without leaving flow.
//              Currently behaves identically to Static (no offset applied) —
//              see flux_absolute.hpp if/when relative offsetting is added.
//   Absolute — removed from its parent's flow entirely. Positioned via
//              top/right/bottom/left against the DIRECT parent container's
//              content box (not the nearest positioned ancestor — see
//              flux_absolute.hpp for the rationale). Every FlexWidget/
//              GridWidget/BoxWidget skips Absolute children when building
//              their flow list, then calls layoutAbsoluteChildren() once at
//              the end of computeLayout() to place them.
// ============================================================================

enum class Position
{
  Static,
  Relative,
  Absolute
};

#if (defined(__EMSCRIPTEN__) && defined(FLUX_WEB_RENDERER_DOM)) || defined(FLUX_SSR)
extern void fluxDomEvictWidget(Widget *owner);
#endif


// ============================================================================
// WIDGET BASE CLASS
// ============================================================================

class Widget : public std::enable_shared_from_this<Widget>
{
private:
  bool mounted = false;

public:
  std::string id;
  std::string text;

  // Layout
  int x = 0, y = 0;
  int width = 0, height = 0;
  int minWidth = 0, minHeight = 0;

  int maxWidth = kUnbounded;
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

  // Flex-item properties (read by whatever Flex/Box container is this
  // widget's parent)
  SizeMode widthMode = SizeMode::Fit;
  SizeMode heightMode = SizeMode::Fit;
  int flexGrow = 0;   // 0 = don't grow (CSS default)
  int flexShrink = 1; // 1 = shrink by default (CSS default)
  int flexBasis = -1; // -1 = auto (use intrinsic/measured size)
  int order = 0;

  // ── Position (see enum above) ─────────────────────────────────────────
  Position position = Position::Static;
  bool hasTop = false, hasRight = false, hasBottom = false, hasLeft = false;
  int top = 0, right = 0, bottom = 0, left = 0;
  // Paint/hit-test order among Position::Absolute siblings only (stable
  // sort — ties keep insertion order). Widgets in normal flow are always
  // painted before absolute siblings, matching CSS stacking of a plain
  // `position: static` box vs its positioned children.
  int zIndex = 0;

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

  virtual void onDetach()
  {
    for (auto &child : children)
    {
      child->parent = nullptr;
      child->onDetach();
    }

#if (defined(__EMSCRIPTEN__) && defined(FLUX_WEB_RENDERER_DOM)) || defined(FLUX_SSR)
    // Remove this widget's cached DOM node (and, via the adapter's real
    // DOM removal, everything still parented under it) — children are
    // evicted first via the recursion above, so this runs post-order,
    // same as the rest of onDetach()'s teardown semantics.
    fluxDomEvictWidget(this);
#endif

  }



  // GL context loss (Android: EGL surface/context destroyed and later
  // recreated, e.g. app backgrounded). Default is a plain tree-walk so
  // every widget gets this for free; only GL-resource-owning widgets
  // (CanvasWidget on Android) need to override it, clean up their own
  // GL-owned state (CPU-side only — the context is already gone, so no
  // glDelete* calls), then call Widget::onGLContextLost() to propagate.
  virtual void onGLContextLost()
  {
    for (auto &child : children)
      child->onGLContextLost();
  }


  virtual WidgetPtr build()
  {
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
  // Item-source hook — used by Map (see flux_map.hpp) and consumed by
  // BoxWidget (see flux_box.hpp). A widget that overrides isItemSource() to
  // return true is NEVER laid out or rendered directly by its parent
  // container; instead the container calls expandItems() to obtain the
  // real widgets it should treat as flow children for that frame, splices
  // them into its normal child list, and lays those out instead. This is
  // how `Box({ Text(...), Map(items, builder), Text(...) })` works without
  // Box needing to know anything about Map's caching/virtualization.
  //
  // Base implementation returns false / empty — a plain Widget is never an
  // item source, so every existing widget type is unaffected by this hook
  // unless it explicitly opts in (only MapWidget does today).
  // -----------------------------------------------------------------------

  virtual bool isItemSource() const { return false; }

  // Breakpoint/props-aware visibility check, used by container layout code
  // (BoxWidget::collectFlowChildren, layoutAbsoluteChildren) INSTEAD OF the
  // plain `visible` field when deciding whether to include a child in this
  // frame's flow. Default just mirrors `visible` — only widgets whose
  // visibility depends on something resolved at layout time (e.g. BoxWidget's
  // breakpoint-driven `hidden` prop) need to override this. Overriding it is
  // also expected to update `visible` as a side effect, since render(),
  // hit-testing, and hover code all still read the plain field afterward.
  virtual bool isVisibleForLayout(GraphicsContext & /*ctx*/) { return visible; }

  // scrollOffset / mainAxisBudget let an item source virtualize when its
  // container is a scrollable single-axis flow (Box in Flex/Block mode
  // with FlexWrap::NoWrap). Pass -1 for both when the caller has no single
  // main axis to virtualize against (e.g. Box in Grid mode) — the item
  // source should then just build everything.
  virtual std::vector<Widget *> expandItems(GraphicsContext & /*ctx*/,
                                            FontCache & /*fontCache*/,
                                            const BoxConstraints & /*itemConstraints*/,
                                            int /*scrollOffset*/,
                                            int /*mainAxisBudget*/)
  {
    return {};
  }

  // -----------------------------------------------------------------------
  // Mouse / keyboard event handlers
  // -----------------------------------------------------------------------

  virtual bool handleMouseWheel(int /*delta*/) { return false; }
  virtual bool handleMouseDown(int /*mx*/, int /*my*/) { return false; }
  virtual bool handleMouseUp(int /*mx*/, int /*my*/) { return false; }
  virtual bool handleMouseMove(int /*mx*/, int /*my*/) { return false; }
  virtual bool handleMouseLeave() { return false; }

  virtual bool handleRightClick(int mx, int my)
  {
    if (onRightClick)
      return onRightClick(mx, my);
    return false;
  }

  virtual bool handleKeyDown(int /*keyCode*/) { return false; }
  virtual bool handleChar(wchar_t /*ch*/) { return false; }
  virtual bool handleTimer(UINT /*timerId*/) { return false; }

  virtual bool handleFocus(bool focused)
  {
    isFocused = focused;
    markNeedsPaint();
    return true;
  }

  // DOM-backend only. Called when a real <input>/<textarea> element this
  // widget owns (see TextInputWidget) fires a native browser 'input' or
  // 'focus'/'blur' event. Default no-op — only widgets that get a
  // dedicated real DOM element override these (per the "no hand-painted
  // text editors" design decision). Declared on the base class, not a
  // separate interface, so flux_dom_adapter_live.cpp's event dispatch can
  // call through a plain Widget* without knowing the concrete type —
  // same reasoning as onOverlayOutsideClick() a few lines up.
  virtual void onDomInputChanged(const std::string & /*value*/) {}
  virtual void onDomFocusChanged(bool /*focused*/) {}

  // DOM-backend only, same family as the two above. Fired when a real
  // scrollable element this widget owns (TextAreaWidget's <textarea>)
  // reports a native 'scroll' event via IDomAdapter::bindScrollEvent —
  // lets a sibling overlay node (a line-number gutter) stay positioned
  // in lockstep with real native scrolling instead of our own
  // hand-rolled scrollY field, which native scrolling bypasses entirely.
  virtual void onDomScrollChanged(int /*scrollTop*/) {}


  // DOM-backend only, same family as the three above. Fired when a real
  // <video>/<audio> element this widget owns reports native
  // timeupdate/play/pause/ended/loadedmetadata events via
  // IDomAdapter::bindMediaEvents. Lets a widget's own custom controls
  // (VideoPlayerWidget's bar) reflect the REAL element's playback state
  // instead of maintaining a separate, unsynchronized copy of it.
  virtual void onDomMediaTimeUpdate(float /*currentTimeSec*/, float /*durationSec*/) {}
  virtual void onDomMediaPlay() {}
  virtual void onDomMediaPause() {}
  virtual void onDomMediaEnded() {}
  // Called by FluxUI's overlay dispatch when a click lands outside this
  // widget's bounds while it's registered as an open overlay (see
  // FluxUI::showOverlay / dispatchOverlayMouseDown). No-op for every widget
  // that isn't an overlay; overlay widgets override this to self-close.
  virtual void onOverlayOutsideClick() {}


  // -----------------------------------------------------------------------
  // Hover helpers
  // -----------------------------------------------------------------------

  bool updateHoverState(int mouseX, int mouseY)
  {
    bool nowHovered = (mouseX >= x && mouseX < x + width && mouseY >= y &&
                       mouseY < y + height);
    if (nowHovered != isHovered)
    {
      isHovered = nowHovered;
      if (onHover)
        onHover(isHovered);
      markNeedsPaint();
      return true;
    }
    return false;
  }

  void clearHoverState()
  {
    if (isHovered)
    {
      isHovered = false;
      if (onHover)
        onHover(false);
      markNeedsPaint();
    }
    for (auto &child : children)
      child->clearHoverState();
  }

  Color getCurrentBackgroundColor() const
  {
    return (isHovered && hasHoverBackground) ? hoverBackgroundColor
                                             : backgroundColor;
  }
  Color getCurrentTextColor() const
  {
    return (isHovered && hasHoverTextColor) ? hoverTextColor : textColor;
  }
  Color getCurrentBorderColor() const
  {
    return (isHovered && hasHoverBorderColor) ? hoverBorderColor : borderColor;
  }

  // -----------------------------------------------------------------------
  // Dirty tracking
  // -----------------------------------------------------------------------

  void markNeedsLayout()
  {
    needsLayout = true;
    needsPaint = true;
    if (parent)
      parent->markNeedsLayout();
  }

  virtual void markNeedsPaint() { needsPaint = true; }

  // -----------------------------------------------------------------------
  // Tree helpers
  // -----------------------------------------------------------------------

  WidgetPtr setId(const std::string &i)
  {
    id = i;
    return shared_from_this();
  }

  void addChild(WidgetPtr child)
  {
    if (!child)
      return;
    children.push_back(child);
    child->parent = this;
    markNeedsLayout();
  }

  const std::string &getText() const { return text; }
  const std::string &getId() const { return id; }

  // -----------------------------------------------------------------------
  // Position helpers (chainable, mirrors the rest of the widget setters)
  // -----------------------------------------------------------------------

  WidgetPtr setPosition(Position p)
  {
    position = p;
    markNeedsLayout();
    return shared_from_this();
  }
  WidgetPtr setTop(int v)
  {
    top = v;
    hasTop = true;
    markNeedsLayout();
    return shared_from_this();
  }
  WidgetPtr setRight(int v)
  {
    right = v;
    hasRight = true;
    markNeedsLayout();
    return shared_from_this();
  }
  WidgetPtr setBottom(int v)
  {
    bottom = v;
    hasBottom = true;
    markNeedsLayout();
    return shared_from_this();
  }
  WidgetPtr setLeft(int v)
  {
    left = v;
    hasLeft = true;
    markNeedsLayout();
    return shared_from_this();
  }
  WidgetPtr setZIndex(int z)
  {
    zIndex = z;
    markNeedsPaint();
    return shared_from_this();
  }

  // -----------------------------------------------------------------------
  // Constraint helpers
  // -----------------------------------------------------------------------

  BoxConstraints selfConstraints(const BoxConstraints &incoming) const
  {
    return incoming.intersect(minWidth, maxWidth, minHeight, maxHeight);
  }

  BoxConstraints contentConstraints(const BoxConstraints &incoming) const
  {
    return incoming.deflate(paddingLeft + paddingRight,
                            paddingTop + paddingBottom);
  }

protected:
  template <typename T>
  static std::string valueToString(const T &val)
  {
    if constexpr (std::is_same_v<T, std::string>)
      return val;
    else if constexpr (std::is_same_v<T, bool>)
      return val ? "true" : "false";
    else if constexpr (std::is_floating_point_v<T>)
    {
      std::ostringstream oss;
      oss << std::fixed << std::setprecision(2) << val;
      return oss.str();
    }
    else if constexpr (std::is_integral_v<T>)
      return std::to_string(val);
    else
      return "[unsupported type]";
  }

  void applyConstraints()
  {
    if (width < minWidth)
      width = minWidth;
    if (height < minHeight)
      height = minHeight;
    if (width > maxWidth)
      width = maxWidth;
    if (height > maxHeight)
      height = maxHeight;
  }
};

// ============================================================================
// ABSOLUTE-POSITIONED CHILD LAYOUT
//
// Shared by every container (BoxWidget in all three display modes, and any
// future container type) so "position: absolute" behaves identically
// everywhere instead of each container re-implementing it slightly
// differently. Called once at the END of a container's computeLayout(),
// after its own x/y/width/height are final.
//
// Deliberately resolves against the DIRECT parent's content box, not the
// nearest "positioned" (relative/absolute) ancestor the way CSS does —
// walking an arbitrary-depth ancestor chain at layout time is a real cost
// and a real source of "why is this 400px off" bugs for comparatively
// little benefit here. If you need to position against a further-up
// ancestor, wrap that ancestor's subtree in an extra Box and anchor there.
// ============================================================================

inline void layoutAbsoluteChildren(Widget *container, GraphicsContext &ctx,
                                   FontCache &fontCache)
{
  if (!container)
    return;

  int cx = container->x + container->paddingLeft;
  int cy = container->y + container->paddingTop;
  int cw = container->width - container->paddingLeft - container->paddingRight;
  int ch = container->height - container->paddingTop - container->paddingBottom;
  cw = std::max(0, cw);
  ch = std::max(0, ch);

  std::vector<Widget *> abs;
  for (auto &c : container->children)
    if (c->isVisibleForLayout(ctx) && c->position == Position::Absolute)
      abs.push_back(c.get());

  if (abs.empty())
    return;

  // Stable sort — ties keep insertion (children-vector) order, matching
  // how CSS resolves equal z-index by document order.
  std::stable_sort(abs.begin(), abs.end(), [](Widget *a, Widget *b)
                   { return a->zIndex < b->zIndex; });

  for (auto *child : abs)
  {
    int minW = 0, maxW = cw, minH = 0, maxH = ch;

    if (child->hasLeft && child->hasRight)
      minW = maxW = std::max(0, cw - child->left - child->right);
    if (child->hasTop && child->hasBottom)
      minH = maxH = std::max(0, ch - child->top - child->bottom);

    // widthMode/heightMode == Fixed still wins over left+right sizing,
    // same precedence Fixed always has elsewhere in the layout system.
    if (child->widthMode == SizeMode::Fixed)
      minW = maxW = child->width;
    if (child->heightMode == SizeMode::Fixed)
      minH = maxH = child->height;

    child->computeLayout(ctx, BoxConstraints(minW, maxW, minH, maxH), fontCache);

    int px = child->hasLeft ? cx + child->left
             : child->hasRight ? cx + cw - child->right - child->width
                               : cx;
    int py = child->hasTop ? cy + child->top
             : child->hasBottom ? cy + ch - child->bottom - child->height
                                : cy;

    child->x = px;
    child->y = py;
    child->positionChildren(child->x + child->paddingLeft,
                            child->y + child->paddingTop,
                            child->width - child->paddingLeft - child->paddingRight,
                            child->height - child->paddingTop - child->paddingBottom);
  }
}

// ============================================================================
// HIT TESTING
// ============================================================================

Widget *findWidgetAt(Widget *w, int x, int y);

// ============================================================================
// MOUSE EVENT HELPERS
// ============================================================================

template <typename Handler>
inline bool findAndHandleMouseEvent(Widget *widget, int x, int y,
                                    Handler handler)
{
  if (!widget || !widget->visible)
    return false;
  if (x >= widget->x && x < widget->x + widget->width && y >= widget->y &&
      y < widget->y + widget->height)
  {
    for (auto it = widget->children.rbegin(); it != widget->children.rend();
         ++it)
    {
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