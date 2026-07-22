#ifndef FLUX_BOX_HPP
#define FLUX_BOX_HPP

// ============================================================================
// Box — the single general-purpose container widget.
//
// Replaces FlexWidget, GridWidget, and FlexBuilderWidget with one widget
// whose layout ALGORITHM is chosen via setDisplay(), the same way CSS
// chooses between `display: flex`, `display: grid`, and normal block flow
// on the same kind of box:
//
//   Box({...})->setDisplay(Display::Flex)->setDirection(FlexDirection::Row)
//   Box({...})->setDisplay(Display::Grid)->setColumns({fr(1), fr(1)})
//   Box({...})                                  // Display::Block (default)
//
// Every display mode shares: padding, background/border, scrollable(),
// gap, and — new in this widget — position:absolute children (see
// flux_widget.hpp's Position enum and layoutAbsoluteChildren()) and Map
// item-source splicing (see flux_map.hpp), so a Map() can sit anywhere in
// a Box's child list regardless of which display mode that Box is using:
//
//   Box({
//       Text("Header"),
//       Map(todos, keyFn, [](int i, const Todo &t){ return Text(t.text); }),
//       Text("Footer"),
//   })->setDisplay(Display::Flex)->setDirection(FlexDirection::Column);
//
// WHY ONE CLASS INSTEAD OF THREE
// ────────────────────────────────
// FlexWidget and GridWidget already shared almost their entire public
// surface (padding, background, border, scrollable, gap, justify/align,
// responsive overrides, scrollbar + fling gesture handling) — the only
// real difference was which placement algorithm ran during computeLayout.
// Keeping them as one class with a dispatch means:
//   • one thing to learn instead of three
//   • position:absolute and Map work everywhere for free
//   • scroll/gesture code (the fiddliest, most bug-prone part) is written
//     and tested exactly once
//
// The tradeoff, stated plainly: this is a big class (roughly the sum of
// the two algorithms it replaces), and every Box instance carries both the
// flex-mode and grid-mode resolved-state members even though only one set
// is ever populated for a given widget. That's a few dozen bytes of waste
// per instance — not a real runtime cost — but it does mean touching flex
// layout logic and touching grid layout logic both go through this one
// file. If that becomes painful, the private computeLayoutFlex_/
// computeLayoutGrid_/computeLayoutBlock_ methods are already cut at clean
// seams for extracting back into separate strategy objects later.
// ============================================================================

#include "flux/flux_core.hpp"
#include "flux/flux_state.hpp"
#include "flux/flux_gesture.hpp"

#include <functional>
#include <vector>
#include <variant>
#include <unordered_map>
#include <unordered_set>
#include <optional>
#include <algorithm>
#include <cmath>
#include <cassert>

struct ScrollbarState
{
    // ── Configuration (set once by the owner) ────────────────────────────
    int size = 6;       // idle thickness — thin, browser-style
    int hoverSize = 10; // thickness while hovered/dragging
    int inset = 2;      // gap between the thumb and the container edge
    bool horizontal = false;

    // Controls whether the bar is DRAWN/HIT-TESTABLE. Independent of
    // isScrollable — content can still overflow and be scrolled (wheel,
    // touch, drag) while this is false, same as CSS `scrollbar-width: none`
    // with `overflow: auto`. Set via BoxWidget::setScrollbarVisible().
    bool userVisible = true;

    // Modern overlay scrollbars usually paint no track at all — just a
    // floating thumb. Flip on for the classic "gutter + thumb" look.
    bool showTrack = false;

    // Classic clickable arrow buttons at each end of the track. Each click
    // steps the scroll offset by arrowStep; holding isn't auto-repeated
    // (no timer here) — a single click per press, like most modern UIs.
    bool showArrows = true;
    int arrowSize = 14; // px, square button at each end
    int arrowStep = 40; // px scrolled per arrow click

    Color colorNormal = Color::fromRGBA(120, 120, 120, 130);
    Color colorHover = Color::fromRGBA(100, 100, 100, 190);
    Color colorActive = Color::fromRGBA(80, 80, 80, 220);
    Color colorTrack = Color::fromRGBA(0, 0, 0, 20);
    Color arrowColorNormal = Color::fromRGBA(90, 90, 90, 160);
    Color arrowColorHover = Color::fromRGBA(60, 60, 60, 220);

    // ── Computed each layout pass ─────────────────────────────────────────
    int contentMain = 0;
    int viewportMain = 0;
    bool isScrollable = false; // true whenever content overflows, regardless of userVisible

    int scrollOffset = 0;
    int thumbLength = 0;
    int thumbOffset = 0;

    // ── Interaction state ─────────────────────────────────────────────────
    bool isDragging = false;
    bool isHovering = false;
    int dragStartPos = 0;
    int dragStartOffset = 0;

    enum class Zone
    {
        None,
        ArrowStart,
        TrackBefore,
        Thumb,
        TrackAfter,
        ArrowEnd
    };
    Zone hoverZone = Zone::None; // which part is under the cursor, for arrow-hover highlight

    // ── Derived helpers ───────────────────────────────────────────────────

    // Should the bar actually be painted / hit-tested this frame?
    bool isVisible() const { return isScrollable && userVisible; }

    // Thickness animates (instantly, no easing here) between idle and
    // hover/drag sizes — same "thin until you touch it" behavior browsers
    // use today.
    int currentThickness() const { return (isHovering || isDragging) ? hoverSize : size; }

    // ── Clamp / update ────────────────────────────────────────────────────

    void clamp()
    {
        int maxScroll = std::max(0, contentMain - viewportMain);
        scrollOffset = std::max(0, std::min(scrollOffset, maxScroll));
    }

    void updateThumb()
    {
        if (!isScrollable)
        {
            thumbLength = thumbOffset = 0;
            return;
        }
        float visRatio = (float)viewportMain / (float)contentMain;
        thumbLength = std::max(30, (int)(viewportMain * visRatio));
        float scrollRatio =
            (contentMain > viewportMain)
                ? (float)scrollOffset / (float)(contentMain - viewportMain)
                : 0.f;
        thumbOffset = (int)(scrollRatio * (viewportMain - thumbLength));
    }

    void setScrollable(bool s)
    {
        if (isScrollable && !s)
        {
            scrollOffset = 0;
            isDragging = false;
            isHovering = false;
            hoverZone = Zone::None;
        }
        isScrollable = s;
    }

    // ── Geometry / hit-testing ──────────────────────────────────────────
    // Track length available for the thumb, after reserving space for the
    // two arrow buttons (if shown). trackLen is the full strip length
    // (ww or wh, minus the two `inset`s) passed in by the caller.
    int usableTrackLen(int trackLen) const
    {
        return showArrows ? std::max(1, trackLen - arrowSize * 2) : trackLen;
    }

    // On-screen thumb start/length within the arrow-reduced track.
    // thumbOffset/thumbLength are computed against viewportMain (the full
    // scrollable area, arrow-agnostic) — this rescales them into the
    // actual pixel track that remains after the arrow buttons.
    void screenThumb(int trackLen, int &outStart, int &outLen) const
    {
        int usable = usableTrackLen(trackLen);
        int lead = showArrows ? arrowSize : 0;
        if (viewportMain <= 0)
        {
            outStart = lead;
            outLen = 0;
            return;
        }
        float scale = (float)usable / (float)std::max(1, viewportMain);
        outLen = std::max(20, (int)(thumbLength * scale));
        outLen = std::min(outLen, usable);
        int range = std::max(0, usable - outLen);
        float ratio = (viewportMain > thumbLength)
                          ? (float)thumbOffset / (float)(viewportMain - thumbLength)
                          : 0.f;
        outStart = lead + (int)(ratio * range);
    }

    // Both use currentThickness()/inset so the clickable strip matches
    // whatever's actually on screen (thin idle strip, fatter on hover).
    Zone hitTest(int mx, int my, int wx, int wy, int ww, int wh) const
    {
        if (!isVisible())
            return Zone::None;
        int thick = currentThickness();
        int trackLen = horizontal ? (ww - inset * 2) : (wh - inset * 2);
        int crossStart = horizontal ? (wy + wh - thick - inset) : (wx + ww - thick - inset);
        int crossEnd = crossStart + thick;
        int mainPos = horizontal ? mx : my;
        int crossPos = horizontal ? my : mx;
        if (crossPos < crossStart || crossPos >= crossEnd)
            return Zone::None;

        int trackStart = horizontal ? (wx + inset) : (wy + inset);
        int along = mainPos - trackStart;
        if (along < 0 || along > trackLen)
            return Zone::None;

        if (showArrows)
        {
            if (along < arrowSize)
                return Zone::ArrowStart;
            if (along > trackLen - arrowSize)
                return Zone::ArrowEnd;
        }

        int ts, tl;
        screenThumb(trackLen, ts, tl);
        if (along < ts)
            return Zone::TrackBefore;
        if (along < ts + tl)
            return Zone::Thumb;
        return Zone::TrackAfter;
    }

    bool isInStrip(int mx, int my, int wx, int wy, int ww, int wh) const
    {
        return hitTest(mx, my, wx, wy, ww, wh) != Zone::None;
    }

    // ── Rendering ─────────────────────────────────────────────────────────
    // Pill-shaped thumb (radius = half thickness). Track is optional and
    // off by default — overlay style floats over content instead of
    // reserving a gutter (see the BoxWidget::render() clip change below).
    //
    // Arrow buttons are simple filled triangles (chevrons) drawn via
    // fillPolygonAlpha, sized relative to arrowSize/thickness so they scale
    // with setScrollbarThickness().

    static void drawChevron(Painter &painter, int boxX, int boxY, int boxW, int boxH,
                            bool horizontal, bool isStart, Color color)
    {
        float cx = boxX + boxW * 0.5f;
        float cy = boxY + boxH * 0.5f;
        float s = std::min(boxW, boxH) * 0.50f;
        std::vector<std::pair<int, int>> pts;
        if (horizontal)
        {
            if (isStart) // ◀
                pts = {{(int)(cx + s), (int)(cy - s)}, {(int)(cx - s), (int)cy}, {(int)(cx + s), (int)(cy + s)}};
            else // ▶
                pts = {{(int)(cx - s), (int)(cy - s)}, {(int)(cx + s), (int)cy}, {(int)(cx - s), (int)(cy + s)}};
        }
        else
        {
            if (isStart) // ▲
                pts = {{(int)(cx - s), (int)(cy + s)}, {(int)cx, (int)(cy - s)}, {(int)(cx + s), (int)(cy + s)}};
            else // ▼
                pts = {{(int)(cx - s), (int)(cy - s)}, {(int)cx, (int)(cy + s)}, {(int)(cx + s), (int)(cy - s)}};
        }
        painter.fillPolygonAlpha(pts, color);
    }

    void render(GraphicsContext &ctx, int wx, int wy, int ww, int wh, Widget *owner) const
    {
        if (!isVisible())
            return;
        Painter painter(ctx, owner);
        int thick = currentThickness();
        int radius = thick / 2;
        Color thumbColor = isDragging   ? colorActive
                           : isHovering ? colorHover
                                        : colorNormal;

        int trackLen = horizontal ? (ww - inset * 2) : (wh - inset * 2);
        int ts, tl;
        screenThumb(trackLen, ts, tl);
        Color startArrowColor = (hoverZone == Zone::ArrowStart) ? arrowColorHover : arrowColorNormal;
        Color endArrowColor = (hoverZone == Zone::ArrowEnd) ? arrowColorHover : arrowColorNormal;

        if (horizontal)
        {
            int sbY = wy + wh - thick - inset;
            int trackX0 = wx + inset;
            if (showTrack)
                painter.fillRoundedRect(trackX0, sbY, trackLen, thick, radius, colorTrack, "sb-track");
            if (showArrows)
            {
                drawChevron(painter, trackX0, sbY, arrowSize, thick, true, true, startArrowColor);
                drawChevron(painter, trackX0 + trackLen - arrowSize, sbY, arrowSize, thick, true, false, endArrowColor);
            }
            painter.fillRoundedRect(trackX0 + ts, sbY, tl, thick, radius, thumbColor, "sb-thumb");
        }
        else
        {
            int sbX = wx + ww - thick - inset;
            int trackY0 = wy + inset;
            if (showTrack)
                painter.fillRoundedRect(sbX, trackY0, thick, trackLen, radius, colorTrack, "sb-track");
            if (showArrows)
            {
                drawChevron(painter, sbX, trackY0, thick, arrowSize, false, true, startArrowColor);
                drawChevron(painter, sbX, trackY0 + trackLen - arrowSize, thick, arrowSize, false, false, endArrowColor);
            }
            painter.fillRoundedRect(sbX, trackY0 + ts, thick, tl, radius, thumbColor, "sb-thumb");
        }
    }

    // ── Mouse handlers ────────────────────────────────────────────────────
    // Wheel scrolling stays keyed to isScrollable alone (not isVisible) —
    // you can hide the bar and still scroll with the wheel/trackpad, same
    // as CSS scrollbar-width:none behavior.

    bool onWheel(int delta)
    {
        if (!isScrollable)
            return false;
        scrollOffset -= (delta / WHEEL_DELTA) * 40;
        clamp();
        updateThumb();
        return true;
    }

    bool onMouseDown(int mx, int my, int wx, int wy, int ww, int wh)
    {
        Zone z = hitTest(mx, my, wx, wy, ww, wh);
        if (z == Zone::None)
            return false;

        switch (z)
        {
        case Zone::Thumb:
            isDragging = true;
            dragStartPos = horizontal ? mx : my;
            dragStartOffset = scrollOffset;
            break;
        case Zone::ArrowStart:
            scrollOffset -= arrowStep;
            clamp();
            updateThumb();
            break;
        case Zone::ArrowEnd:
            scrollOffset += arrowStep;
            clamp();
            updateThumb();
            break;
        case Zone::TrackBefore:
            scrollOffset -= viewportMain;
            clamp();
            updateThumb();
            break;
        case Zone::TrackAfter:
            scrollOffset += viewportMain;
            clamp();
            updateThumb();
            break;
        default:
            break;
        }
        return true;
    }

    bool onMouseUp()
    {
        if (!isDragging)
            return false;
        isDragging = false;
        isHovering = false;
        return true;
    }

    bool onMouseMove(int mx, int my, int wx, int wy, int ww, int wh)
    {
        if (isDragging)
        {
            if (!isScrollable)
            {
                onMouseUp();
                return true;
            }
            // Map drag delta through the arrow-reduced on-screen track so
            // dragging feels 1:1 with the mouse even with arrows present.
            int trackLen = horizontal ? (ww - inset * 2) : (wh - inset * 2);
            int usable = usableTrackLen(trackLen);
            int ts, tl;
            screenThumb(trackLen, ts, tl);
            int curPos = horizontal ? mx : my;
            int delta = curPos - dragStartPos;
            float ratio = (usable > tl) ? (float)delta / (float)(usable - tl) : 0.f;
            scrollOffset =
                dragStartOffset + (int)(ratio * (contentMain - viewportMain));
            clamp();
            updateThumb();
            return true;
        }
        Zone z = hitTest(mx, my, wx, wy, ww, wh);
        bool wasHovering = isHovering;
        Zone prevZone = hoverZone;
        isHovering = (z != Zone::None);
        hoverZone = z;
        return (wasHovering != isHovering) || (prevZone != hoverZone);
    }

    bool onMouseLeave()
    {
        bool changed = isHovering || hoverZone != Zone::None;
        isHovering = false;
        hoverZone = Zone::None;
        return changed;
    }
};

// ============================================================================
// SHARED ENUMS
// ============================================================================

enum class Display
{
    Block,
    Flex,
    Grid
};

enum class FlexDirection
{
    Row,
    RowReverse,
    Column,
    ColumnReverse
};
enum class FlexWrap
{
    NoWrap,
    Wrap,
    WrapReverse
};
enum class JustifyContent
{
    Start,
    End,
    Center,
    SpaceBetween,
    SpaceAround,
    SpaceEvenly
};
enum class AlignItems
{
    Start,
    End,
    Center,
    Stretch,
    Baseline
};
enum class AlignContent
{
    Start,
    End,
    Center,
    SpaceBetween,
    SpaceAround,
    SpaceEvenly,
    Stretch
};
enum class Breakpoint
{
    Base,
    Sm,
    Md,
    Lg,
    Xl,
    Xxl
};

struct Breakpoints
{
    int sm = 640, md = 768, lg = 1024, xl = 1280, xxl = 1536;
};

class BreakpointProvider
{
public:
    static Breakpoints &get() { return instance(); }
    static void set(const Breakpoints &b) { instance() = b; }

private:
    // thread_local — see FluxUI::currentInstance's rationale: one browser
    // tab / native app has one thread doing layout, so this is unchanged
    // in practice there; it matters once multiple SSR requests lay out
    // concurrently on different threads, each needing its own breakpoints.
    static Breakpoints &instance()
    {
        static thread_local Breakpoints b;
        return b;
    }
};

inline int thresholdFor(Breakpoint bp, const Breakpoints &b)
{
    switch (bp)
    {
    case Breakpoint::Sm:
        return b.sm;
    case Breakpoint::Md:
        return b.md;
    case Breakpoint::Lg:
        return b.lg;
    case Breakpoint::Xl:
        return b.xl;
    case Breakpoint::Xxl:
        return b.xxl;
    default:
        return 0;
    }
}

// ============================================================================
// GRID TRACK DEFINITIONS (unchanged from flux_grid.hpp)
// ============================================================================

enum class TrackSizing
{
    Fixed,
    Fr,
    Fill,
    Auto,
    MinContent,
    MaxContent
};

struct TrackDef
{
    TrackSizing sizing = TrackSizing::Auto;
    int value = 1;
};

inline TrackDef px(int n) { return {TrackSizing::Fixed, n}; }
inline TrackDef fr(int n = 1) { return {TrackSizing::Fr, n}; }
inline TrackDef fillTrack(int n = 1) { return {TrackSizing::Fill, n}; }
inline TrackDef autoTrack() { return {TrackSizing::Auto, 0}; }
inline TrackDef minContent() { return {TrackSizing::MinContent, 0}; }
inline TrackDef maxContent() { return {TrackSizing::MaxContent, 0}; }

inline std::vector<TrackDef> repeat(int count, std::vector<TrackDef> pattern)
{
    std::vector<TrackDef> result;
    result.reserve(count * (int)pattern.size());
    for (int i = 0; i < count; ++i)
        for (auto &t : pattern)
            result.push_back(t);
    return result;
}
inline std::vector<TrackDef> repeat(int count, TrackDef t)
{
    return repeat(count, std::vector<TrackDef>{t});
}

// ============================================================================
// BOX ITEM SPEC — optional per-child placement metadata, meaningful only in
// Display::Grid. Wrap a child in BoxItem(...) to give it explicit
// col/row/span/alignSelf; a bare WidgetPtr in a Box({...}) list is treated
// as auto-placed with default span (1,1) — exactly like a bare widget in
// the old Grid({...}) factory.
// ============================================================================

class BoxItemSpec
{
public:
    WidgetPtr widget;

    int colStart = -1, colEnd = -1; // 1-based; -1 = auto
    int rowStart = -1, rowEnd = -1;

    std::optional<AlignItems> alignSelf;
    std::optional<AlignItems> justifySelf;

    int colSpanPending_ = 1;
    int rowSpanPending_ = 1;

    // weak_ptr — a strong self-reference here creates a cycle (this object
    // holds a shared_ptr to itself via BoxItem()), so the refcount never
    // reaches zero and the spec (and the widget/subtree it holds) leaks on
    // every rebuild. Same rationale as BoxWidget::self_ below.
    std::weak_ptr<BoxItemSpec> self_;

    std::shared_ptr<BoxItemSpec> atCol(int c)
    {
        colStart = c;
        return self_.lock();
    }
    std::shared_ptr<BoxItemSpec> atRow(int r)
    {
        rowStart = r;
        return self_.lock();
    }
    std::shared_ptr<BoxItemSpec> at(int c, int r)
    {
        colStart = c;
        rowStart = r;
        return self_.lock();
    }

    std::shared_ptr<BoxItemSpec> spanCols(int n)
    {
        colSpanPending_ = n;
        if (colStart >= 1)
            colEnd = colStart + n;
        return self_.lock();
    }
    std::shared_ptr<BoxItemSpec> spanRows(int n)
    {
        rowSpanPending_ = n;
        if (rowStart >= 1)
            rowEnd = rowStart + n;
        return self_.lock();
    }
    std::shared_ptr<BoxItemSpec> span(int cols, int rows)
    {
        spanCols(cols);
        spanRows(rows);
        return self_.lock();
    }

    std::shared_ptr<BoxItemSpec> withAlignSelf(AlignItems a)
    {
        alignSelf = a;
        return self_.lock();
    }
    std::shared_ptr<BoxItemSpec> withJustifySelf(AlignItems a)
    {
        justifySelf = a;
        return self_.lock();
    }
};

using BoxItemPtr = std::shared_ptr<BoxItemSpec>;

inline BoxItemPtr BoxItem(WidgetPtr w)
{
    auto s = std::make_shared<BoxItemSpec>();
    s->self_ = s;
    s->widget = w;
    return s;
}

// ============================================================================
// BOX PROPS
// ============================================================================

struct BoxProps
{
    Display display = Display::Block;

    // CSS display:none analog — independent of `display` above (which only
    // chooses the layout ALGORITHM for when we ARE in flow). Resolved through
    // the same responsive() breakpoint pipeline as everything else in this
    // struct, so hideAt()/showAt()/etc. below are just sugar over responsive().
    bool hidden = false;

    // ── shared ───────────────────────────────────────────────────────────
    int paddingLeft = 0, paddingRight = 0, paddingTop = 0, paddingBottom = 0;
    int gap = 0; // used directly by Flex/Block; used as column/row gap
                 // fallback by Grid when columnGap/rowGap are unset (-1)

    bool hasBackground = false;
    Color backgroundColor = Color::fromRGB(255, 255, 255);
    bool hasBorder = false;
    Color borderColor = Color::fromRGB(0, 0, 0);
    int borderWidth = 1;
    int borderRadius = 0;

    bool scrollable = false;

    // ── Scrollbar appearance ────────────────────────────────────────────
    bool scrollbarVisible = true; // draw/hit-test the bar (scrolling still works if false)
    int scrollbarThickness = 6;   // idle thickness, px
    int scrollbarHoverThickness = 10;
    bool scrollbarArrows = true; // classic arrow buttons at each end

    JustifyContent justify = JustifyContent::Start;  // Flex main axis / Grid justifyContent
    AlignItems alignItems = AlignItems::Stretch;     // Flex cross axis / Grid alignItems
    AlignContent alignContent = AlignContent::Start; // Flex multi-line / Grid row distribution

    // ── Flex-only ────────────────────────────────────────────────────────
    FlexDirection direction = FlexDirection::Row;
    FlexWrap wrap = FlexWrap::NoWrap;

    // ── Grid-only ────────────────────────────────────────────────────────
    std::vector<TrackDef> columns;
    std::vector<TrackDef> rows;
    int columnGap = -1; // -1 = fall back to `gap`
    int rowGap = -1;
    AlignItems justifyItems = AlignItems::Stretch; // per-cell horizontal default
};

// ============================================================================
// BOX WIDGET
// ============================================================================

class BoxWidget : public Widget
{
private:
    BoxProps baseProps_;
    BoxProps resolved_;
    std::vector<std::pair<Breakpoint, std::function<void(BoxProps &)>>> overrides_;

    // Grid-mode placement specs, parallel to (a subset of) Widget::children.
    // Populated by addItem(); a plain addChild() (bare WidgetPtr in Box({}))
    // gets an implicit default-span spec here too, created lazily.
    std::vector<BoxItemPtr> gridItems_;

    // weak_ptr — a strong self-reference here creates a cycle (this object
    // holds a shared_ptr to itself), so the refcount never reaches zero.
    // Since rebuild() discards and reconstructs the whole tree on every
    // state change, a strong self_ leaks every BoxWidget on every rebuild.
    std::weak_ptr<BoxWidget> self_;

    // ── scroll / gesture (shared across all three display modes) ───────────
    ScrollbarState sb_;
    GestureState gesture_;
    TimerID flingTimer_ = 0;

    // ── Flex-mode resolved-this-frame state ─────────────────────────────────
    struct LineMetric
    {
        std::vector<Widget *> items;
        std::vector<int> resolvedMain;
        int usedMain = 0;
        int crossSize = 0;
    };
    std::vector<LineMetric> flexLines_;
    bool isRowAxis_ = true;
    bool mainIsReversed_ = false;
    bool wrapReversed_ = false;
    int containerMainSize_ = 0;
    int containerCrossSize_ = 0;
    int flexTotalCross_ = 0;

    // ── Grid-mode resolved-this-frame state ─────────────────────────────────
    std::vector<int> colSizes_, rowSizes_, colOffsets_, rowOffsets_;
    int gridTotalContentW_ = 0, gridTotalContentH_ = 0;
    int gridContainerW_ = 0, gridContainerH_ = 0;

    struct PlacedItem
    {
        BoxItemSpec *spec = nullptr;
        int colStart = 0, colEnd = 0;
        int rowStart = 0, rowEnd = 0;
        int pixX = 0, pixY = 0, pixW = 0, pixH = 0;
    };
    std::vector<PlacedItem> gridPlaced_;

    // scrollAxisIsMain_: true when the scroll axis coincides with Flex's
    // main axis (Flex/Block, NoWrap) — used by Map to decide whether it can
    // virtualize. Grid mode always scrolls vertically only and does not
    // offer virtualization through this path (see class doc comment).
    bool scrollAxisIsMain_ = true;

    // ── click tracking ───────────────────────────────────────────────────
    // Separate from sb_/gesture_'s drag state: a Box only becomes a click
    // target once onClick is set via setOnClick(). Press must both START
    // and END inside our bounds to fire — same "drag off before release
    // cancels" behavior as ButtonWidget/IconButtonWidget.
    bool _pressed = false;

    bool _hit(int mx, int my) const
    {
        return mx >= x && mx < x + width && my >= y && my < y + height;
    }

    // ── helpers ──────────────────────────────────────────────────────────

    BoxProps resolveProps(const GraphicsContext &ctx) const
    {
        BoxProps p = baseProps_;
        int viewportW = (ctx.fluxViewportWidth > 0) ? ctx.fluxViewportWidth : kUnbounded;

        for (auto &[obp, fn] : overrides_)
            if (obp == Breakpoint::Base)
                fn(p);

        auto &bps = BreakpointProvider::get();
        for (Breakpoint bp : {Breakpoint::Sm, Breakpoint::Md, Breakpoint::Lg,
                              Breakpoint::Xl, Breakpoint::Xxl})
        {
            int threshold = thresholdFor(bp, bps);
            if (viewportW < threshold)
                continue;
            for (auto &[obp, fn] : overrides_)
                if (obp == bp)
                    fn(p);
        }
        return p;
    }

    static int mainSize(const Widget *w, bool rowAxis) { return rowAxis ? w->width : w->height; }
    static int crossSize(const Widget *w, bool rowAxis) { return rowAxis ? w->height : w->width; }

    // ── Scrollbar gutter reservation ─────────────────────────────────────
    // One place for this so layout (which shrinks content), clipping, hit-
    // testing, and drawing can never disagree about where the bar's own
    // track lives. The track is carved out of the box's OWN bounds — NOT
    // the padded content box — so it behaves like a real non-overlay
    // scrollbar: padding never affects it, only content does.
    //
    //   kScrollbarTrackInset — mirrors ScrollbarState::inset's default (2px
    //                          gap the thumb keeps from the container edge).
    //   kScrollbarGutterGap  — a few extra px of deliberate breathing room
    //                          between the last bit of content and the
    //                          bar's own track, so they never look flush.
    //
    // Sized against scrollbarHoverThickness (not the idle thickness) so the
    // bar growing on hover/drag never has to eat into content — it just has
    // a little unused space beside it while idle, and less (but still a
    // few px) once grown.
    static constexpr int kScrollbarTrackInset = 2;
    static constexpr int kScrollbarGutterGap = 4;

    int scrollbarGutter() const
    {
        if (!resolved_.scrollable || !resolved_.scrollbarVisible)
            return 0;
        return resolved_.scrollbarHoverThickness + kScrollbarTrackInset + kScrollbarGutterGap;
    }

    void stopFling()
    {
        if (flingTimer_)
        {
            if (auto *ui = FluxUI::getCurrentInstance())
                ui->clearInterval(flingTimer_);
            flingTimer_ = 0;
        }
    }
    void startFling()
    {
        stopFling();
        if (auto *ui = FluxUI::getCurrentInstance())
        {
            flingTimer_ = ui->setInterval(16, [this]()
                                          {
                int delta = gesture_.tickFling();
                if (delta == 0) { stopFling(); return; }
                sb_.scrollOffset += delta;
                sb_.clamp(); sb_.updateThumb();
                repositionChildren();
                markNeedsPaint();
                if (auto *u = FluxUI::getCurrentInstance()) u->invalidateWidget(this); });
        }
    }

    // ── Flow-child collection: skips position:absolute children and
    //    splices in whatever any Map (isItemSource()) child currently
    //    expands to. Shared by all three display modes. ─────────────────────
    std::vector<Widget *> collectFlowChildren(GraphicsContext &ctx, FontCache &fontCache,
                                              const BoxConstraints &itemConstraints,
                                              int scrollOffset, int mainAxisBudget)
    {
        std::vector<Widget *> flat;
        flat.reserve(children.size());
        for (auto &c : children)
        {
            if (!c->isVisibleForLayout(ctx))
                continue;
            if (c->position == Position::Absolute)
                continue;
            if (c->isItemSource())
            {
                auto expanded = c->expandItems(ctx, fontCache, itemConstraints,
                                               scrollOffset, mainAxisBudget);
                flat.insert(flat.end(), expanded.begin(), expanded.end());
            }
            else
            {
                flat.push_back(c.get());
            }
        }
        return flat;
    }

    // ============================================================================
    // FLEX MODE  (ported from FlexWidget::computeLayout / positionChildren)
    // ============================================================================

    void computeLayoutFlex_(GraphicsContext &ctx, const BoxConstraints &constraints,
                            FontCache &fontCache)
    {
        const BoxProps &P = resolved_;

        isRowAxis_ = (P.direction == FlexDirection::Row || P.direction == FlexDirection::RowReverse);
        mainIsReversed_ = (P.direction == FlexDirection::RowReverse || P.direction == FlexDirection::ColumnReverse);
        wrapReversed_ = (P.wrap == FlexWrap::WrapReverse);
        scrollAxisIsMain_ = (P.wrap == FlexWrap::NoWrap);

        BoxConstraints self = selfConstraints(constraints);

        int outerMaxW = (widthMode == SizeMode::Fixed) ? width : self.maxWidth;
        int outerMaxH = (heightMode == SizeMode::Fixed) ? height : self.maxHeight;
        if (widthMode == SizeMode::Full)
            outerMaxW = self.maxWidth;
        if (heightMode == SizeMode::Full)
            outerMaxH = self.maxHeight;

        int padH = P.paddingLeft + P.paddingRight;
        int padV = P.paddingTop + P.paddingBottom;

        bool scrollableMain = P.scrollable && (P.wrap == FlexWrap::NoWrap);
        bool scrollableCross = P.scrollable && (P.wrap != FlexWrap::NoWrap);

        // Scrollbar sits on whichever screen axis is perpendicular to the
        // scroll axis — same "horizontal bar reduces height, vertical bar
        // reduces width" rule computed at the bottom of this function for
        // sb_.horizontal, just done early enough here to affect measurement.
        bool willBeHorizontalScrollbar = scrollAxisIsMain_ ? isRowAxis_ : !isRowAxis_;
        int gutter = scrollbarGutter();

        int contentMaxW = std::max(0, outerMaxW - padH - (willBeHorizontalScrollbar ? 0 : gutter));
        int contentMaxH = std::max(0, outerMaxH - padV - (willBeHorizontalScrollbar ? gutter : 0));

        containerMainSize_ = isRowAxis_ ? contentMaxW : contentMaxH;
        containerCrossSize_ = isRowAxis_ ? contentMaxH : contentMaxW;

        SizeMode crossAxisMode = isRowAxis_ ? heightMode : widthMode;
        bool crossIsFit = (crossAxisMode == SizeMode::Fit);

        // ---- collect flow children (skips absolute, splices Map output) ----
        BoxConstraints looseForSource = isRowAxis_
                                            ? BoxConstraints::loose(kUnbounded, containerCrossSize_)
                                            : BoxConstraints::loose(containerCrossSize_, kUnbounded);
        std::vector<Widget *> ordered = collectFlowChildren(
            ctx, fontCache, looseForSource, sb_.scrollOffset, containerMainSize_);

        std::stable_sort(ordered.begin(), ordered.end(),
                         [](Widget *a, Widget *b)
                         { return a->order < b->order; });
        if (mainIsReversed_)
            std::reverse(ordered.begin(), ordered.end());

        // ---- hypothetical main size per child ----
        std::vector<int> hypoMain(ordered.size());
        for (size_t i = 0; i < ordered.size(); i++)
        {
            Widget *c = ordered[i];
            SizeMode mainMode = isRowAxis_ ? c->widthMode : c->heightMode;
            SizeMode crossMode = isRowAxis_ ? c->heightMode : c->widthMode;
            int fixedCross = isRowAxis_ ? c->height : c->width;

            if (c->flexBasis >= 0)
            {
                hypoMain[i] = c->flexBasis;
            }
            else if (mainMode == SizeMode::Fixed)
            {
                hypoMain[i] = isRowAxis_ ? c->width : c->height;
            }
            else
            {
                int crossForMeasure = (crossMode == SizeMode::Fixed) ? fixedCross : containerCrossSize_;
                bool mainFull = (mainMode == SizeMode::Full);
                int mainBudget = mainFull ? containerMainSize_ : kUnbounded;

                BoxConstraints measureC = isRowAxis_
                                              ? BoxConstraints::loose(mainBudget, crossForMeasure)
                                              : BoxConstraints::loose(crossForMeasure, mainBudget);
                c->computeLayout(ctx, measureC, fontCache);
                hypoMain[i] = mainSize(c, isRowAxis_);
            }
        }

        // ---- line breaking ----
        flexLines_.clear();
        if (P.wrap == FlexWrap::NoWrap)
        {
            LineMetric line;
            for (size_t i = 0; i < ordered.size(); i++)
                line.items.push_back(ordered[i]);
            flexLines_.push_back(std::move(line));
        }
        else
        {
            LineMetric cur;
            int curMain = 0;
            for (size_t i = 0; i < ordered.size(); i++)
            {
                int itemMain = hypoMain[i] + (cur.items.empty() ? 0 : P.gap);
                if (!cur.items.empty() && (curMain + itemMain) > containerMainSize_)
                {
                    flexLines_.push_back(std::move(cur));
                    cur = LineMetric{};
                    cur.items.push_back(ordered[i]);
                    curMain = hypoMain[i];
                }
                else
                {
                    cur.items.push_back(ordered[i]);
                    curMain += itemMain;
                }
            }
            if (!cur.items.empty())
                flexLines_.push_back(std::move(cur));
        }
        if (wrapReversed_)
            std::reverse(flexLines_.begin(), flexLines_.end());

        std::unordered_map<Widget *, int> hypoIndex;
        hypoIndex.reserve(ordered.size());
        for (size_t i = 0; i < ordered.size(); i++)
            hypoIndex[ordered[i]] = hypoMain[i];

        auto hypoOf = [&](Widget *w) -> int
        {
            auto it = hypoIndex.find(w);
            return it != hypoIndex.end() ? it->second : 0;
        };
        auto isMainFixed = [&](Widget *c) -> bool
        {
            SizeMode mainMode = isRowAxis_ ? c->widthMode : c->heightMode;
            return mainMode == SizeMode::Fixed && c->flexBasis < 0;
        };

        // ---- per-line flex resolution + final child layout ----
        for (auto &line : flexLines_)
        {
            int basisSum = 0;
            for (auto *c : line.items)
                basisSum += hypoOf(c);
            int gapSum = (int)(line.items.size() > 1 ? P.gap * (line.items.size() - 1) : 0);
            int lineMainBudget = scrollableMain ? std::max(containerMainSize_, basisSum + gapSum)
                                                : containerMainSize_;
            bool unboundedBudget = lineMainBudget >= kUnbounded;
            int freeSpace = unboundedBudget ? 0 : (lineMainBudget - basisSum - gapSum);

            std::vector<Widget *> active = line.items;
            std::unordered_map<Widget *, int> resolvedMain;
            for (auto *c : line.items)
                resolvedMain[c] = hypoOf(c);

            {
                std::vector<Widget *> nonFixed;
                for (auto *c : active)
                    if (!isMainFixed(c))
                        nonFixed.push_back(c);
                active = nonFixed;
            }

            for (int iter = 0; iter < 8; iter++)
            {
                if (freeSpace > 0)
                {
                    int totalGrow = 0;
                    for (auto *c : active)
                        totalGrow += c->flexGrow;
                    if (totalGrow == 0)
                        break;
                    for (auto *c : active)
                        resolvedMain[c] = hypoOf(c) + (freeSpace * c->flexGrow) / totalGrow;
                }
                else if (freeSpace < 0)
                {
                    long totalShrink = 0;
                    for (auto *c : active)
                        totalShrink += (long)c->flexShrink * hypoOf(c);
                    if (totalShrink == 0)
                        break;
                    for (auto *c : active)
                    {
                        double weight = (double)(c->flexShrink * hypoOf(c)) / (double)totalShrink;
                        resolvedMain[c] = hypoOf(c) + (int)(freeSpace * weight);
                    }
                }
                else
                    break;

                std::vector<Widget *> violated;
                for (auto *c : active)
                {
                    int minM = isRowAxis_ ? c->minWidth : c->minHeight;
                    int maxM = isRowAxis_ ? c->maxWidth : c->maxHeight;
                    int clamped = std::max(minM, std::min(maxM, resolvedMain[c]));
                    if (clamped != resolvedMain[c])
                    {
                        resolvedMain[c] = clamped;
                        violated.push_back(c);
                    }
                }
                if (violated.empty())
                    break;

                int usedFrozen = 0;
                for (auto *c : line.items)
                    if (std::find(active.begin(), active.end(), c) == active.end() ||
                        std::find(violated.begin(), violated.end(), c) != violated.end())
                        usedFrozen += resolvedMain[c];
                std::vector<Widget *> nextActive;
                for (auto *c : active)
                    if (std::find(violated.begin(), violated.end(), c) == violated.end())
                        nextActive.push_back(c);
                active = nextActive;
                freeSpace = lineMainBudget - usedFrozen - gapSum;
                if (active.empty())
                    break;
            }

            int lineCrossMax = 0;
            for (auto *c : line.items)
            {
                int mainC = std::max(0, resolvedMain[c]);
                SizeMode crossMode = isRowAxis_ ? c->heightMode : c->widthMode;

                if (crossMode == SizeMode::Fixed)
                {
                    int fixedCross = isRowAxis_ ? c->height : c->width;
                    BoxConstraints childC = isRowAxis_
                                                ? BoxConstraints(mainC, mainC, fixedCross, fixedCross)
                                                : BoxConstraints(fixedCross, fixedCross, mainC, mainC);
                    c->computeLayout(ctx, childC, fontCache);
                }
                else
                {
                    BoxConstraints childC = isRowAxis_
                                                ? BoxConstraints(mainC, mainC, 0, containerCrossSize_)
                                                : BoxConstraints(0, containerCrossSize_, mainC, mainC);
                    c->computeLayout(ctx, childC, fontCache);
                }
                lineCrossMax = std::max(lineCrossMax, crossSize(c, isRowAxis_));
            }

            int stretchCross = (P.alignItems == AlignItems::Stretch && !crossIsFit)
                                   ? (P.wrap == FlexWrap::NoWrap ? containerCrossSize_ : lineCrossMax)
                                   : lineCrossMax;

            for (auto *c : line.items)
            {
                SizeMode crossMode = isRowAxis_ ? c->heightMode : c->widthMode;
                if (crossMode == SizeMode::Fixed)
                    continue;

                bool wantsStretch = (crossMode == SizeMode::Full) ||
                                    (P.alignItems == AlignItems::Stretch && !crossIsFit);
                if (!wantsStretch)
                    continue;

                int mainC = std::max(0, resolvedMain[c]);
                BoxConstraints childC = isRowAxis_
                                            ? BoxConstraints(mainC, mainC, stretchCross, stretchCross)
                                            : BoxConstraints(stretchCross, stretchCross, mainC, mainC);
                c->computeLayout(ctx, childC, fontCache);
            }

            lineCrossMax = 0;
            for (auto *c : line.items)
                lineCrossMax = std::max(lineCrossMax, crossSize(c, isRowAxis_));

            int usedMainFinal = 0;
            for (size_t i = 0; i < line.items.size(); i++)
            {
                usedMainFinal += resolvedMain[line.items[i]];
                if (i + 1 < line.items.size())
                    usedMainFinal += P.gap;
            }
            line.usedMain = usedMainFinal;
            bool crossFillsContainer = !crossIsFit && ((P.wrap == FlexWrap::NoWrap) || scrollableCross);
            line.crossSize = crossFillsContainer
                                 ? std::max(containerCrossSize_, lineCrossMax)
                                 : lineCrossMax;

            line.resolvedMain.clear();
            for (auto *c : line.items)
                line.resolvedMain.push_back(resolvedMain[c]);
        }

        flexTotalCross_ = 0;
        for (size_t i = 0; i < flexLines_.size(); i++)
        {
            flexTotalCross_ += flexLines_[i].crossSize;
            if (i + 1 < flexLines_.size())
                flexTotalCross_ += P.gap;
        }

        int lineBasisMax = 0;
        for (auto &l : flexLines_)
            lineBasisMax = std::max(lineBasisMax, l.usedMain);

        int finalW, finalH;
        if (isRowAxis_)
        {
            finalW = (widthMode == SizeMode::Fit) ? std::min(outerMaxW, lineBasisMax + padH) : outerMaxW;
            finalH = (heightMode == SizeMode::Fit) ? (flexTotalCross_ + padV) : outerMaxH;
            finalW = std::max(finalW, padH);
        }
        else
        {
            finalH = (heightMode == SizeMode::Fit) ? std::min(outerMaxH, lineBasisMax + padV) : outerMaxH;
            finalW = (widthMode == SizeMode::Fit) ? (flexTotalCross_ + padH) : outerMaxW;
            finalH = std::max(finalH, padV);
        }
        width = self.clampWidth(finalW);
        height = self.clampHeight(finalH);

        if (scrollAxisIsMain_)
        {
            int contentMain = 0;
            for (size_t i = 0; i < flexLines_.size(); i++)
                contentMain = std::max(contentMain, flexLines_[i].usedMain);
            sb_.horizontal = isRowAxis_;
            sb_.contentMain = contentMain;
            sb_.viewportMain = containerMainSize_;
        }
        else
        {
            sb_.horizontal = !isRowAxis_;
            sb_.contentMain = flexTotalCross_;
            sb_.viewportMain = containerCrossSize_;
        }
        sb_.setScrollable(P.scrollable && sb_.contentMain > sb_.viewportMain);
        sb_.clamp();
        sb_.updateThumb();
    }

    void positionChildrenFlex_(int contentX, int contentY, int /*cw*/, int /*ch*/)
    {
        const BoxProps &P = resolved_;
        int crossFree = (P.scrollable && P.wrap != FlexWrap::NoWrap)
                            ? 0
                            : (containerCrossSize_ - flexTotalCross_);

        double lineGapExtra = 0;
        double crossOffset = 0;
        switch (P.alignContent)
        {
        case AlignContent::Start:
            crossOffset = 0;
            break;
        case AlignContent::End:
            crossOffset = crossFree;
            break;
        case AlignContent::Center:
            crossOffset = crossFree / 2.0;
            break;
        case AlignContent::SpaceBetween:
            lineGapExtra = flexLines_.size() > 1 ? (double)crossFree / (flexLines_.size() - 1) : 0;
            break;
        case AlignContent::SpaceAround:
            lineGapExtra = flexLines_.empty() ? 0 : (double)crossFree / flexLines_.size();
            crossOffset = lineGapExtra / 2.0;
            break;
        case AlignContent::SpaceEvenly:
            lineGapExtra = (double)crossFree / (flexLines_.size() + 1);
            crossOffset = lineGapExtra;
            break;
        case AlignContent::Stretch:
            crossOffset = 0;
            break;
        }

        double cursorCross = crossOffset;
        if (!scrollAxisIsMain_ && P.scrollable)
            cursorCross -= sb_.scrollOffset;

        for (auto &line : flexLines_)
        {
            int mainFree = containerMainSize_ - line.usedMain;
            double itemGapExtra = 0;
            double mainOffset = 0;
            switch (P.justify)
            {
            case JustifyContent::Start:
                mainOffset = 0;
                break;
            case JustifyContent::End:
                mainOffset = mainFree;
                break;
            case JustifyContent::Center:
                mainOffset = mainFree / 2.0;
                break;
            case JustifyContent::SpaceBetween:
                itemGapExtra = line.items.size() > 1 ? (double)mainFree / (line.items.size() - 1) : 0;
                break;
            case JustifyContent::SpaceAround:
                itemGapExtra = line.items.empty() ? 0 : (double)mainFree / line.items.size();
                mainOffset = itemGapExtra / 2.0;
                break;
            case JustifyContent::SpaceEvenly:
                itemGapExtra = (double)mainFree / (line.items.size() + 1);
                mainOffset = itemGapExtra;
                break;
            }

            double cursorMain = mainOffset;
            if (scrollAxisIsMain_ && P.scrollable)
                cursorMain -= sb_.scrollOffset;

            // NOTE: margins are applied here at position time only. hypoMain /
            // line-breaking / flex-grow-shrink resolution above does NOT know
            // about margins yet, so a child with large margins can still be
            // measured/wrapped as if the margin weren't there. Fine for
            // fixed-size children with modest margins; revisit if you need
            // margin to participate in wrap decisions or grow/shrink math.

            for (size_t i = 0; i < line.items.size(); i++)
            {
                Widget *c = line.items[i];
                int cMain = line.resolvedMain[i];
                int cCross = crossSize(c, isRowAxis_);

                int marginMainLead = isRowAxis_ ? c->marginLeft : c->marginTop;
                int marginMainTrail = isRowAxis_ ? c->marginRight : c->marginBottom;
                int marginCrossLead = isRowAxis_ ? c->marginTop : c->marginLeft;

                double childCrossOffset = 0;
                switch (P.alignItems)
                {
                case AlignItems::Start:
                case AlignItems::Baseline:
                    childCrossOffset = 0;
                    break;
                case AlignItems::End:
                    childCrossOffset = line.crossSize - cCross;
                    break;
                case AlignItems::Center:
                    childCrossOffset = (line.crossSize - cCross) / 2.0;
                    break;
                case AlignItems::Stretch:
                    childCrossOffset = 0;
                    break;
                }

                int mainPx, crossPx;
                if (isRowAxis_)
                {
                    mainPx = contentX + (int)std::round(cursorMain) + marginMainLead;
                    crossPx = contentY + (int)std::round(cursorCross + childCrossOffset) + marginCrossLead;
                    c->x = mainPx;
                    c->y = crossPx;
                }
                else
                {
                    mainPx = contentY + (int)std::round(cursorMain) + marginMainLead;
                    crossPx = contentX + (int)std::round(cursorCross + childCrossOffset) + marginCrossLead;
                    c->x = crossPx;
                    c->y = mainPx;
                }

                c->positionChildren(c->x + c->paddingLeft, c->y + c->paddingTop,
                                    c->width - c->paddingLeft - c->paddingRight,
                                    c->height - c->paddingTop - c->paddingBottom);

                cursorMain += marginMainLead + cMain + marginMainTrail + P.gap + itemGapExtra;
            }
            cursorCross += line.crossSize + P.gap + lineGapExtra;
        }
    }

    // ============================================================================
    // BLOCK MODE  (new — plain top-to-bottom document flow, the <div> default)
    //
    // Each non-absolute, non-item-source-passthrough child is stacked
    // vertically. A child fills the container's content width unless it has
    // an explicit Fixed widthMode; height is always the child's own natural
    // (Fit) or Fixed height — Block never stretches height. gap adds
    // uniform spacing between stacked children, same meaning as Flex's gap
    // in Column direction. Scrolling, when enabled, is always vertical.
    // ============================================================================

    void computeLayoutBlock_(GraphicsContext &ctx, const BoxConstraints &constraints,
                             FontCache &fontCache)
    {
        const BoxProps &P = resolved_;

        // Reuse the flex-mode bookkeeping fields so render()/scroll handlers
        // (which are shared across modes) don't need a third set of names —
        // Block behaves like Flex Column/NoWrap for their purposes.
        isRowAxis_ = false;
        mainIsReversed_ = false;
        wrapReversed_ = false;
        scrollAxisIsMain_ = true;

        BoxConstraints self = selfConstraints(constraints);

        int outerMaxW = (widthMode == SizeMode::Fixed) ? width : self.maxWidth;
        int outerMaxH = (heightMode == SizeMode::Fixed) ? height : self.maxHeight;
        if (widthMode == SizeMode::Full)
            outerMaxW = self.maxWidth;
        if (heightMode == SizeMode::Full)
            outerMaxH = self.maxHeight;

        int padH = P.paddingLeft + P.paddingRight;
        int padV = P.paddingTop + P.paddingBottom;

        // Block always scrolls vertically, so the bar is always on the
        // right, always reducing available width.
        int gutter = scrollbarGutter();

        // Two cases for an unbounded outerMaxW:
        //  - widthMode == Full: we're SUPPOSED to fill whatever's
        //    available, and "available" is genuinely unbounded (rare, but
        //    matches Flex's own behavior) — stay unbounded, same as before.
        //  - widthMode == Fit: we're supposed to shrink-to-fit our
        //    content. Filling to "unbounded" here is wrong — it's exactly
        //    what breaks a Fit Block nested inside a Flex/Wrap parent's
        //    hypothetical-size measurement pass (parent asks "how wide do
        //    you want to be with no constraint?" and we'd answer "2
        //    billion pixels"). Instead, measure each child at its own
        //    natural width and use the largest as our content width —
        //    CSS shrink-to-fit / max-content sizing for a block box.
        bool unboundedFit = (widthMode != SizeMode::Fixed &&
                             widthMode != SizeMode::Full &&
                             outerMaxW >= kUnbounded);

        int contentMaxW;
        std::vector<Widget *> ordered;
        if (unboundedFit)
        {
            BoxConstraints looseNatural = BoxConstraints::loose(kUnbounded, kUnbounded);
            ordered = collectFlowChildren(ctx, fontCache, looseNatural,
                                          sb_.scrollOffset, kUnbounded);
            int maxNatural = 0;
            for (auto *c : ordered)
            {
                int marginH = c->marginLeft + c->marginRight;
                BoxConstraints naturalC = (c->widthMode == SizeMode::Fixed)
                                              ? BoxConstraints(c->width, c->width, 0, kUnbounded)
                                              : BoxConstraints(0, kUnbounded, 0, kUnbounded);
                c->computeLayout(ctx, naturalC, fontCache);
                maxNatural = std::max(maxNatural, c->width + marginH);
            }
            // Fit-sizing content width doesn't need the gutter subtracted —
            // it's shrink-to-fit against the children's own natural widths,
            // which don't know about the scrollbar at all. (Fit + scrollable
            // is an unusual combination in practice; Fixed/Full is the
            // common scrollable-list case handled in the `else` branch.)
            contentMaxW = maxNatural; // Fit-sizing: no gutter subtraction, see comment above
        }
        else
        {
            contentMaxW = std::max(0, outerMaxW - padH - gutter);
        }
        containerMainSize_ = kUnbounded; // block content is unbounded vertically
        containerCrossSize_ = contentMaxW;

        if (!unboundedFit)
        {
            BoxConstraints looseForSource = BoxConstraints::loose(contentMaxW, kUnbounded);
            ordered = collectFlowChildren(ctx, fontCache, looseForSource,
                                          sb_.scrollOffset, kUnbounded);
        }
        // else: `ordered` was already collected above during the natural-
        // width measurement pass; re-collecting here would call
        // expandItems() on any Map child a second time for no benefit.

        LineMetric line; // single "line" — Block never wraps
        int cursor = 0;
        for (auto *c : ordered)
        {
            int marginH = c->marginLeft + c->marginRight;
            int availW = std::max(0, contentMaxW - marginH);
            BoxConstraints childC = (c->widthMode == SizeMode::Fixed)
                                        ? BoxConstraints(c->width, c->width, 0, kUnbounded)
                                        : BoxConstraints(availW, availW, 0, kUnbounded);
            if (c->heightMode == SizeMode::Fixed)
                childC = BoxConstraints(childC.minWidth, childC.maxWidth, c->height, c->height);

            c->computeLayout(ctx, childC, fontCache);
            line.items.push_back(c);
            line.resolvedMain.push_back(c->height);
            cursor += c->marginTop + c->height + c->marginBottom;
            if (c != ordered.back())
                cursor += P.gap;
        }
        line.usedMain = cursor;
        line.crossSize = contentMaxW;
        flexLines_.clear();
        flexLines_.push_back(std::move(line));
        flexTotalCross_ = contentMaxW;

        int finalW = (widthMode == SizeMode::Fit)
                         ? (unboundedFit ? (contentMaxW + padH) : std::min(outerMaxW, contentMaxW + padH))
                         : (outerMaxW >= kUnbounded ? contentMaxW + padH : outerMaxW);
        int finalH = (heightMode == SizeMode::Fit || outerMaxH >= kUnbounded)
                         ? cursor + padV
                         : outerMaxH;

        width = self.clampWidth(std::max(finalW, padH));
        height = self.clampHeight(std::max(finalH, padV));

        sb_.horizontal = false;
        sb_.contentMain = cursor;
        sb_.viewportMain = std::max(0, height - padV);
        sb_.setScrollable(P.scrollable && sb_.contentMain > sb_.viewportMain);
        sb_.clamp();
        sb_.updateThumb();
    }

    void positionChildrenBlock_(int contentX, int contentY, int /*cw*/, int /*ch*/)
    {
        const BoxProps &P = resolved_;
        if (flexLines_.empty())
            return;
        int cursorY = contentY - (P.scrollable ? sb_.scrollOffset : 0);
        for (auto *c : flexLines_[0].items)
        {
            cursorY += c->marginTop;
            c->x = contentX + c->marginLeft;
            c->y = cursorY;
            c->positionChildren(c->x + c->paddingLeft, c->y + c->paddingTop,
                                c->width - c->paddingLeft - c->paddingRight,
                                c->height - c->paddingTop - c->paddingBottom);
            cursorY += c->height + c->marginBottom + P.gap;
        }
    }

    // ============================================================================
    // GRID MODE  (ported from GridWidget::computeLayout / positionChildren)
    // ============================================================================

    BoxItemSpec *specForWidget(Widget *w)
    {
        for (auto &s : gridItems_)
            if (s->widget.get() == w)
                return s.get();
        return nullptr;
    }

    int measureChildAxis(BoxItemSpec *spec, bool horizontal, GraphicsContext &ctx,
                         FontCache &fontCache, int crossSize)
    {
        if (!spec || !spec->widget)
            return 0;
        BoxConstraints bc = horizontal
                                ? BoxConstraints::loose(kUnbounded, crossSize)
                                : BoxConstraints::loose(crossSize, kUnbounded);
        spec->widget->computeLayout(ctx, bc, fontCache);
        return horizontal ? spec->widget->width : spec->widget->height;
    }

    std::vector<int> resolveTracks(std::vector<TrackDef> tracks, int containerSize, int gap,
                                   bool isScrollAxis,
                                   const std::vector<std::vector<BoxItemSpec *>> &singleSpanners,
                                   GraphicsContext &ctx, FontCache &fontCache, int crossSize)
    {
        const int n = (int)tracks.size();
        std::vector<int> sizes(n, 0);
        if (n == 0)
            return sizes;

        int totalGap = gap * std::max(0, n - 1);

        for (int i = 0; i < n; ++i)
            if (tracks[i].sizing == TrackSizing::Fixed)
                sizes[i] = tracks[i].value;

        for (int i = 0; i < n; ++i)
        {
            auto sz = tracks[i].sizing;
            bool needsMeasure = (sz == TrackSizing::Auto || sz == TrackSizing::MinContent ||
                                 sz == TrackSizing::MaxContent ||
                                 (sz == TrackSizing::Fr && isScrollAxis));
            if (!needsMeasure)
                continue;
            int best = 0;
            for (auto *spec : singleSpanners[i])
                best = std::max(best, measureChildAxis(spec, !isScrollAxis, ctx, fontCache, crossSize));
            sizes[i] = best;
        }

        if (!isScrollAxis)
        {
            int usedFixed = totalGap;
            int totalFr = 0;
            for (int i = 0; i < n; ++i)
            {
                if (tracks[i].sizing != TrackSizing::Fr)
                    usedFixed += sizes[i];
                else
                    totalFr += tracks[i].value;
            }
            int remaining = std::max(0, containerSize - usedFixed);
            if (totalFr > 0)
                for (int i = 0; i < n; ++i)
                    if (tracks[i].sizing == TrackSizing::Fr)
                        sizes[i] = (remaining * tracks[i].value) / totalFr;
        }
        {
            int usedFixed = totalGap;
            int totalFill = 0;
            for (int i = 0; i < n; ++i)
            {
                if (tracks[i].sizing != TrackSizing::Fill)
                    usedFixed += sizes[i];
                else
                    totalFill += tracks[i].value;
            }
            int remaining = std::max(0, containerSize - usedFixed);
            if (totalFill > 0)
                for (int i = 0; i < n; ++i)
                    if (tracks[i].sizing == TrackSizing::Fill)
                        sizes[i] = (remaining * tracks[i].value) / totalFill;
        }
        return sizes;
    }

    static std::vector<int> makeOffsets(const std::vector<int> &sizes, int gap)
    {
        std::vector<int> off;
        off.reserve(sizes.size());
        int cursor = 0;
        for (int sz : sizes)
        {
            off.push_back(cursor);
            cursor += sz + gap;
        }
        return off;
    }

    static void distributeSpace(int freeSpace, int trackCount, JustifyContent justify,
                                int &outLeading, int &outBetween)
    {
        outLeading = outBetween = 0;
        if (freeSpace <= 0 || trackCount <= 0)
            return;
        switch (justify)
        {
        case JustifyContent::End:
            outLeading = freeSpace;
            break;
        case JustifyContent::Center:
            outLeading = freeSpace / 2;
            break;
        case JustifyContent::SpaceBetween:
            outBetween = trackCount > 1 ? freeSpace / (trackCount - 1) : 0;
            break;
        case JustifyContent::SpaceAround:
            outBetween = trackCount > 0 ? freeSpace / trackCount : 0;
            outLeading = outBetween / 2;
            break;
        case JustifyContent::SpaceEvenly:
            outBetween = freeSpace / (trackCount + 1);
            outLeading = outBetween;
            break;
        default:
            break;
        }
    }
    static void distributeSpace(int freeSpace, int trackCount, AlignContent align,
                                int &outLeading, int &outBetween)
    {
        JustifyContent j = JustifyContent::Start;
        switch (align)
        {
        case AlignContent::End:
            j = JustifyContent::End;
            break;
        case AlignContent::Center:
            j = JustifyContent::Center;
            break;
        case AlignContent::SpaceBetween:
            j = JustifyContent::SpaceBetween;
            break;
        case AlignContent::SpaceAround:
            j = JustifyContent::SpaceAround;
            break;
        case AlignContent::SpaceEvenly:
            j = JustifyContent::SpaceEvenly;
            break;
        default:
            break;
        }
        distributeSpace(freeSpace, trackCount, j, outLeading, outBetween);
    }

    static int alignOffset(int cellSize, int childSize, AlignItems align)
    {
        switch (align)
        {
        case AlignItems::End:
        case AlignItems::Baseline:
            return std::max(0, cellSize - childSize);
        case AlignItems::Center:
            return std::max(0, (cellSize - childSize) / 2);
        case AlignItems::Stretch:
        case AlignItems::Start:
        default:
            return 0;
        }
    }

    void computeLayoutGrid_(GraphicsContext &ctx, const BoxConstraints &constraints,
                            FontCache &fontCache)
    {
        const BoxProps &P = resolved_;
        int columnGap = (P.columnGap >= 0) ? P.columnGap : P.gap;
        int rowGap = (P.rowGap >= 0) ? P.rowGap : P.gap;

        BoxConstraints self = selfConstraints(constraints);

        int outerMaxW = (widthMode == SizeMode::Fixed) ? width : self.maxWidth;
        int outerMaxH = (heightMode == SizeMode::Fixed) ? height : self.maxHeight;
        if (widthMode == SizeMode::Full)
            outerMaxW = self.maxWidth;
        if (heightMode == SizeMode::Full)
            outerMaxH = self.maxHeight;

        int padH = P.paddingLeft + P.paddingRight;
        int padV = P.paddingTop + P.paddingBottom;

        // Grid always scrolls vertically only (see class doc comment
        // above), so the bar always sits on the right and always reduces
        // available column width — reserve it so Fr/Fill columns size
        // against the true usable width instead of running under the bar.
        gridContainerW_ = std::max(0, outerMaxW - padH - scrollbarGutter());
        gridContainerH_ = std::max(0, outerMaxH - padV);

        // Drop specs whose widget is gone or no longer actually parented
        // under this Box — covers both a removed BoxItem() child and an
        // auto-created spec (see below) for a Map-expanded item that
        // existed on a previous frame but doesn't this frame. Without this,
        // gridItems_ only ever grows, holding widgets alive indefinitely.
        gridItems_.erase(
            std::remove_if(gridItems_.begin(), gridItems_.end(),
                           [this](const BoxItemPtr &s)
                           { return !s->widget || s->widget->parent != this; }),
            gridItems_.end());

        // Ensure every non-absolute, non-item-source-owned child has a spec.
        // (Map's expanded items are not addressable for explicit grid
        // placement — they always auto-place; wrap individually built
        // widgets in BoxItem() ahead of time if you need explicit Grid
        // placement for programmatically-generated cells.)
        BoxConstraints looseForSource = BoxConstraints::loose(gridContainerW_, gridContainerH_);
        std::vector<Widget *> flowChildren = collectFlowChildren(
            ctx, fontCache, looseForSource, 0, -1);

        std::vector<BoxItemSpec *> specs;
        specs.reserve(flowChildren.size());
        for (auto *w : flowChildren)
        {
            BoxItemSpec *s = specForWidget(w);
            if (!s)
            {
                auto owned = BoxItem(WidgetPtr()); // placeholder; widget set below
                owned->widget = w->shared_from_this();
                gridItems_.push_back(owned);
                s = owned.get();
            }
            specs.push_back(s);
        }

        int numCols = (int)P.columns.size();
        if (numCols == 0)
            numCols = 1;

        gridPlaced_.clear();
        gridPlaced_.resize(specs.size());

        std::unordered_set<int> occupied;
        auto occupyCell = [&](int c, int r)
        { occupied.insert(c * 10000 + r); };
        auto isOccupied = [&](int c, int r)
        { return occupied.count(c * 10000 + r) > 0; };

        for (int i = 0; i < (int)specs.size(); ++i)
        {
            auto *spec = specs[i];
            PlacedItem &pi = gridPlaced_[i];
            pi.spec = spec;

            bool hasCol = (spec->colStart >= 1);
            bool hasRow = (spec->rowStart >= 1);

            if (hasCol)
            {
                pi.colStart = spec->colStart - 1;
                pi.colEnd = (spec->colEnd >= 1) ? spec->colEnd - 1 : pi.colStart + spec->colSpanPending_;
                pi.colEnd = std::min(pi.colEnd, numCols);
                if (pi.colStart >= numCols)
                {
                    pi.colStart = numCols - 1;
                    pi.colEnd = numCols;
                }
            }
            if (hasRow)
            {
                pi.rowStart = spec->rowStart - 1;
                pi.rowEnd = (spec->rowEnd >= 1) ? spec->rowEnd - 1 : pi.rowStart + spec->rowSpanPending_;
            }
            if (hasCol && hasRow)
                for (int c = pi.colStart; c < pi.colEnd; ++c)
                    for (int r = pi.rowStart; r < pi.rowEnd; ++r)
                        occupyCell(c, r);
        }

        int autoCursorCol = 0, autoCursorRow = 0;
        for (int i = 0; i < (int)specs.size(); ++i)
        {
            auto *spec = specs[i];
            PlacedItem &pi = gridPlaced_[i];
            bool hasCol = (spec->colStart >= 1);
            bool hasRow = (spec->rowStart >= 1);
            if (hasCol && hasRow)
                continue;

            int spanC = spec->colSpanPending_;
            int spanR = spec->rowSpanPending_;

            if (hasCol)
            {
                int col = pi.colStart, row = autoCursorRow;
                while (true)
                {
                    bool fits = true;
                    for (int c = col; c < col + spanC && fits; ++c)
                        for (int r = row; r < row + spanR && fits; ++r)
                            if (isOccupied(c, r))
                                fits = false;
                    if (fits)
                        break;
                    ++row;
                }
                pi.rowStart = row;
                pi.rowEnd = row + spanR;
                pi.colEnd = col + spanC;
                for (int c = pi.colStart; c < pi.colEnd; ++c)
                    for (int r = pi.rowStart; r < pi.rowEnd; ++r)
                        occupyCell(c, r);
            }
            else if (hasRow)
            {
                int row = pi.rowStart, col = 0;
                while (col + spanC <= numCols)
                {
                    bool fits = true;
                    for (int c = col; c < col + spanC && fits; ++c)
                        for (int r = row; r < row + spanR && fits; ++r)
                            if (isOccupied(c, r))
                                fits = false;
                    if (fits)
                        break;
                    ++col;
                    if (col + spanC > numCols)
                    {
                        col = 0;
                        ++row;
                    }
                }
                pi.colStart = col;
                pi.colEnd = col + spanC;
                pi.rowEnd = pi.rowStart + spanR;
                for (int c = pi.colStart; c < pi.colEnd; ++c)
                    for (int r = pi.rowStart; r < pi.rowEnd; ++r)
                        occupyCell(c, r);
            }
            else
            {
                if (autoCursorCol + spanC > numCols)
                {
                    autoCursorCol = 0;
                    ++autoCursorRow;
                }
                while (true)
                {
                    bool fits = true;
                    for (int c = autoCursorCol; c < autoCursorCol + spanC && fits; ++c)
                        for (int r = autoCursorRow; r < autoCursorRow + spanR && fits; ++r)
                            if (isOccupied(c, r))
                                fits = false;
                    if (fits)
                        break;
                    ++autoCursorCol;
                    if (autoCursorCol + spanC > numCols)
                    {
                        autoCursorCol = 0;
                        ++autoCursorRow;
                    }
                }
                pi.colStart = autoCursorCol;
                pi.colEnd = autoCursorCol + spanC;
                pi.rowStart = autoCursorRow;
                pi.rowEnd = autoCursorRow + spanR;
                for (int c = pi.colStart; c < pi.colEnd; ++c)
                    for (int r = pi.rowStart; r < pi.rowEnd; ++r)
                        occupyCell(c, r);
                autoCursorCol += spanC;
                if (autoCursorCol >= numCols)
                {
                    autoCursorCol = 0;
                    ++autoCursorRow;
                }
            }
        }

        int maxRow = 0;
        for (auto &pi : gridPlaced_)
            maxRow = std::max(maxRow, pi.rowEnd);

        std::vector<TrackDef> effectiveRows = P.rows;
        while ((int)effectiveRows.size() < maxRow)
            effectiveRows.push_back(autoTrack());

        std::vector<std::vector<BoxItemSpec *>> colSingleSpan(numCols);
        std::vector<std::vector<BoxItemSpec *>> rowSingleSpan(effectiveRows.size());
        for (auto &pi : gridPlaced_)
        {
            if (!pi.spec)
                continue;
            if (pi.colEnd - pi.colStart == 1 && pi.colStart < numCols)
                colSingleSpan[pi.colStart].push_back(pi.spec);
            if (pi.rowEnd - pi.rowStart == 1 && pi.rowStart < (int)effectiveRows.size())
                rowSingleSpan[pi.rowStart].push_back(pi.spec);
        }

        colSizes_ = resolveTracks(P.columns, gridContainerW_, columnGap, false,
                                  colSingleSpan, ctx, fontCache, gridContainerH_);
        colOffsets_ = makeOffsets(colSizes_, columnGap);
        int colGapTotal = columnGap * std::max(0, numCols - 1);
        gridTotalContentW_ = 0;
        for (int s : colSizes_)
            gridTotalContentW_ += s;
        gridTotalContentW_ += colGapTotal;

        rowSizes_ = resolveTracks(effectiveRows, gridContainerH_, rowGap, P.scrollable,
                                  rowSingleSpan, ctx, fontCache, gridContainerW_);
        rowOffsets_ = makeOffsets(rowSizes_, rowGap);
        int rowGapTotal = rowGap * std::max(0, (int)rowSizes_.size() - 1);
        gridTotalContentH_ = 0;
        for (int s : rowSizes_)
            gridTotalContentH_ += s;
        gridTotalContentH_ += rowGapTotal;

        for (auto &pi : gridPlaced_)
        {
            if (!pi.spec)
                continue;
            int cs = std::min(pi.colStart, (int)colOffsets_.size() - 1);
            int ce = std::min(pi.colEnd, (int)colSizes_.size());
            int rs = std::min(pi.rowStart, (int)rowOffsets_.size() - 1);
            int re = std::min(pi.rowEnd, (int)rowSizes_.size());

            pi.pixX = (cs >= 0 && cs < (int)colOffsets_.size()) ? colOffsets_[cs] : 0;
            pi.pixY = (rs >= 0 && rs < (int)rowOffsets_.size()) ? rowOffsets_[rs] : 0;

            pi.pixW = 0;
            for (int c = cs; c < ce; ++c)
            {
                pi.pixW += colSizes_[c];
                if (c + 1 < ce)
                    pi.pixW += columnGap;
            }
            pi.pixH = 0;
            for (int r = rs; r < re; ++r)
            {
                pi.pixH += rowSizes_[r];
                if (r + 1 < re)
                    pi.pixH += rowGap;
            }
        }

        for (auto &pi : gridPlaced_)
        {
            if (!pi.spec || !pi.spec->widget)
                continue;
            Widget *w = pi.spec->widget.get();
            AlignItems hAlign = pi.spec->justifySelf.value_or(P.justifyItems);
            AlignItems vAlign = pi.spec->alignSelf.value_or(P.alignItems);
            int minW = (hAlign == AlignItems::Stretch) ? pi.pixW : 0;
            int minH = (vAlign == AlignItems::Stretch) ? pi.pixH : 0;
            BoxConstraints childC(minW, pi.pixW, minH, pi.pixH);
            w->computeLayout(ctx, childC, fontCache);
        }

        int finalW, finalH;
        finalW = (widthMode == SizeMode::Fit) ? std::min(outerMaxW, gridTotalContentW_ + padH)
                                              : (outerMaxW >= kUnbounded ? gridTotalContentW_ + padH : outerMaxW);
        finalH = (heightMode == SizeMode::Fit) ? gridTotalContentH_ + padV
                                               : (outerMaxH >= kUnbounded ? gridTotalContentH_ + padV : outerMaxH);
        finalW = std::max(finalW, padH);
        finalH = std::max(finalH, padV);
        width = self.clampWidth(finalW);
        height = self.clampHeight(finalH);

        sb_.horizontal = false;
        sb_.contentMain = gridTotalContentH_;
        sb_.viewportMain = gridContainerH_;
        sb_.setScrollable(P.scrollable && sb_.contentMain > sb_.viewportMain);
        sb_.clamp();
        sb_.updateThumb();
    }

    void positionChildrenGrid_(int contentX, int contentY, int /*cw*/, int /*ch*/)
    {
        const BoxProps &P = resolved_;
        int freeW = std::max(0, gridContainerW_ - gridTotalContentW_);
        int freeH = std::max(0, gridContainerH_ - gridTotalContentH_);

        int colLeading = 0, colBetween = 0;
        distributeSpace(freeW, (int)colSizes_.size(), P.justify, colLeading, colBetween);
        int rowLeading = 0, rowBetween = 0;
        distributeSpace(freeH, (int)rowSizes_.size(), P.alignContent, rowLeading, rowBetween);

        int scroll = sb_.scrollOffset;
        for (auto &pi : gridPlaced_)
        {
            if (!pi.spec || !pi.spec->widget)
                continue;
            int cellX = contentX + colLeading + pi.pixX + colBetween * pi.colStart;
            int cellY = contentY + rowLeading + pi.pixY + rowBetween * pi.rowStart - scroll;

            Widget *w = pi.spec->widget.get();
            AlignItems hAlign = pi.spec->justifySelf.value_or(P.justifyItems);
            AlignItems vAlign = pi.spec->alignSelf.value_or(P.alignItems);

            int childW = (hAlign == AlignItems::Stretch) ? pi.pixW : w->width;
            int childH = (vAlign == AlignItems::Stretch) ? pi.pixH : w->height;

            w->x = cellX + alignOffset(pi.pixW, childW, hAlign);
            w->y = cellY + alignOffset(pi.pixH, childH, vAlign);

            w->positionChildren(w->x + w->paddingLeft, w->y + w->paddingTop,
                                w->width - w->paddingLeft - w->paddingRight,
                                w->height - w->paddingTop - w->paddingBottom);
        }
    }

    void repositionChildren()
    {
        int cx = x + resolved_.paddingLeft;
        int cy = y + resolved_.paddingTop;
        int cw = width - resolved_.paddingLeft - resolved_.paddingRight;
        int ch = height - resolved_.paddingTop - resolved_.paddingBottom;
        positionChildren(cx, cy, cw, ch);
        if (auto *ui = FluxUI::getCurrentInstance())
            ui->invalidateWidget(this);
    }

public:
    ~BoxWidget() override { stopFling(); }
    void setSelf(std::shared_ptr<BoxWidget> ptr) { self_ = ptr; }
    std::shared_ptr<BoxWidget> self() { return self_.lock(); }

    // ── Item registration ────────────────────────────────────────────────

    // Plain widget — auto-placed in Grid mode, normal flow child otherwise.
    void addChildPlain(WidgetPtr child)
    {
        if (!child)
            return;
        addChild(child); // Widget::addChild — appends to `children`, sets parent
    }

    // Explicit-placement widget (only meaningful in Grid mode; ignored — but
    // harmless — in Flex/Block modes, where it behaves like a plain child).
    std::shared_ptr<BoxWidget> addItem(BoxItemPtr item)
    {
        if (!item || !item->widget)
            return self();
        gridItems_.push_back(item);
        addChild(item->widget);
        return self();
    }

    // ── Display mode ─────────────────────────────────────────────────────

    std::shared_ptr<BoxWidget> setDisplay(Display d)
    {
        baseProps_.display = d;
        markNeedsLayout();
        return self();
    }

    // Covariant override of Widget::setId — the base version returns
    // WidgetPtr, which would silently downcast the chain and hide every
    // BoxWidget-only setter (setHidden, hideAt, setColumns, ...) called
    // after it. Shadowing it here keeps the fluent chain typed as
    // shared_ptr<BoxWidget> no matter where setId() appears in the chain.
    std::shared_ptr<BoxWidget> setId(const std::string &i)
    {
        id = i;
        return self();
    }

    // ── Visibility (CSS display:none-style, breakpoint-aware) ──────────────
    // Goes through BoxProps/responsive() rather than touching Widget::visible
    // directly, so it composes with breakpoint overrides the same way every
    // other Box prop does.
    std::shared_ptr<BoxWidget> setHidden(bool h)
    {
        baseProps_.hidden = h;
        markNeedsLayout();
        return self();
    }

    // hideAt(Md)    → hidden from Md and up      (Tailwind: `md:hidden`)
    // showAt(Md)    → visible from Md and up     (pairs with setHidden(true))
    // hideBelow(Md) → hidden until Md, visible Md+   (`hidden md:block`)
    // hideAbove(Md) → visible until Md, hidden Md+   (`block md:hidden`)
    std::shared_ptr<BoxWidget> hideAt(Breakpoint bp)
    {
        return responsive(bp, [](BoxProps &p)
                          { p.hidden = true; });
    }
    std::shared_ptr<BoxWidget> showAt(Breakpoint bp)
    {
        return responsive(bp, [](BoxProps &p)
                          { p.hidden = false; });
    }
    std::shared_ptr<BoxWidget> hideBelow(Breakpoint bp)
    {
        setHidden(true);
        return showAt(bp);
    }
    std::shared_ptr<BoxWidget> hideAbove(Breakpoint bp)
    {
        setHidden(false);
        return hideAt(bp);
    }

    // ── Shared setters ───────────────────────────────────────────────────

    std::shared_ptr<BoxWidget> setPadding(int p)
    {
        baseProps_.paddingLeft = baseProps_.paddingRight = baseProps_.paddingTop = baseProps_.paddingBottom = p;
        markNeedsLayout();
        return self();
    }
    std::shared_ptr<BoxWidget> setPaddingHV(int h, int v)
    {
        baseProps_.paddingLeft = baseProps_.paddingRight = h;
        baseProps_.paddingTop = baseProps_.paddingBottom = v;
        markNeedsLayout();
        return self();
    }
    std::shared_ptr<BoxWidget> setGap(int g)
    {
        baseProps_.gap = g;
        markNeedsLayout();
        return self();
    }
    std::shared_ptr<BoxWidget> setScrollable(bool s)
    {
        baseProps_.scrollable = s;
        markNeedsLayout();
        return self();
    }
    std::shared_ptr<BoxWidget> setBackgroundColor(Color c)
    {
        baseProps_.hasBackground = true;
        baseProps_.backgroundColor = c;
        markNeedsPaint();
        return self();
    }
    std::shared_ptr<BoxWidget> setBorderColor(Color c)
    {
        baseProps_.hasBorder = true;
        baseProps_.borderColor = c;
        markNeedsPaint();
        return self();
    }
    std::shared_ptr<BoxWidget> setBorderWidth(int w)
    {
        baseProps_.hasBorder = true;
        baseProps_.borderWidth = w;
        markNeedsPaint();
        return self();
    }
    std::shared_ptr<BoxWidget> setBorderRadius(int r)
    {
        baseProps_.borderRadius = r;
        markNeedsPaint();
        return self();
    }
    std::shared_ptr<BoxWidget> setJustifyContent(JustifyContent j)
    {
        baseProps_.justify = j;
        markNeedsLayout();
        return self();
    }
    std::shared_ptr<BoxWidget> setAlignItems(AlignItems a)
    {
        baseProps_.alignItems = a;
        markNeedsLayout();
        return self();
    }
    std::shared_ptr<BoxWidget> setAlignContent(AlignContent a)
    {
        baseProps_.alignContent = a;
        markNeedsLayout();
        return self();
    }
    // ── Click handling ───────────────────────────────────────────────────
    // Makes this Box a click target. Setting a handler here is what turns
    // handleMouseDown/handleMouseUp's click detection on (see the private
    // click-tracking block above) — a Box with no onClick set stays fully
    // non-consuming for mouse events, same as before this feature existed.
    std::shared_ptr<BoxWidget> setOnClick(ClickHandler h)
    {
        onClick = std::move(h);
        return self();
    }

    // ── Flex-only setters ────────────────────────────────────────────────

    std::shared_ptr<BoxWidget> setDirection(FlexDirection d)
    {
        baseProps_.direction = d;
        markNeedsLayout();
        return self();
    }
    std::shared_ptr<BoxWidget> setWrap(FlexWrap w)
    {
        baseProps_.wrap = w;
        markNeedsLayout();
        return self();
    }

    std::shared_ptr<BoxWidget> setScrollbarVisible(bool v)
    {
        baseProps_.scrollbarVisible = v;
        markNeedsPaint();
        return self();
    }
    std::shared_ptr<BoxWidget> setScrollbarThickness(int idle, int hover = -1)
    {
        baseProps_.scrollbarThickness = idle;
        baseProps_.scrollbarHoverThickness = (hover >= 0) ? hover : idle + 4;
        markNeedsPaint();
        return self();
    }
    std::shared_ptr<BoxWidget> setScrollbarArrows(bool v)
    {
        baseProps_.scrollbarArrows = v;
        markNeedsPaint();
        return self();
    }

    // ── Grid-only setters ────────────────────────────────────────────────

    std::shared_ptr<BoxWidget> setColumns(std::vector<TrackDef> c)
    {
        baseProps_.columns = std::move(c);
        markNeedsLayout();
        return self();
    }
    std::shared_ptr<BoxWidget> setRows(std::vector<TrackDef> r)
    {
        baseProps_.rows = std::move(r);
        markNeedsLayout();
        return self();
    }
    std::shared_ptr<BoxWidget> setColumnGap(int g)
    {
        baseProps_.columnGap = g;
        markNeedsLayout();
        return self();
    }
    std::shared_ptr<BoxWidget> setRowGap(int g)
    {
        baseProps_.rowGap = g;
        markNeedsLayout();
        return self();
    }
    std::shared_ptr<BoxWidget> setJustifyItems(AlignItems a)
    {
        baseProps_.justifyItems = a;
        markNeedsLayout();
        return self();
    }

    // ── Self-sizing ──────────────────────────────────────────────────────

    std::shared_ptr<BoxWidget> setWidthMode(SizeMode m)
    {
        widthMode = m;
        markNeedsLayout();
        return self();
    }
    std::shared_ptr<BoxWidget> setHeightMode(SizeMode m)
    {
        heightMode = m;
        markNeedsLayout();
        return self();
    }
    std::shared_ptr<BoxWidget> setWidth(int w)
    {
        width = w;
        widthMode = SizeMode::Fixed;
        markNeedsLayout();
        return self();
    }
    std::shared_ptr<BoxWidget> setHeight(int h)
    {
        height = h;
        heightMode = SizeMode::Fixed;
        markNeedsLayout();
        return self();
    }
    std::shared_ptr<BoxWidget> setFlexGrow(int g)
    {
        flexGrow = g;
        markNeedsLayout();
        return self();
    }
    std::shared_ptr<BoxWidget> setFlexShrink(int s)
    {
        flexShrink = s;
        markNeedsLayout();
        return self();
    }
    std::shared_ptr<BoxWidget> setFlexBasis(int b)
    {
        flexBasis = b;
        markNeedsLayout();
        return self();
    }
    std::shared_ptr<BoxWidget> setOrder(int o)
    {
        order = o;
        markNeedsLayout();
        return self();
    }

    // ── Position (thin pass-through to the Widget base setters, kept here
    //    too so chains like Box({...})->setPosition(...)->setTop(...) read
    //    naturally without an explicit upcast) ───────────────────────────

    std::shared_ptr<BoxWidget> setPositionMode(Position p)
    {
        position = p;
        markNeedsLayout();
        return self();
    }
    std::shared_ptr<BoxWidget> setTopPx(int v)
    {
        top = v;
        hasTop = true;
        markNeedsLayout();
        return self();
    }
    std::shared_ptr<BoxWidget> setRightPx(int v)
    {
        right = v;
        hasRight = true;
        markNeedsLayout();
        return self();
    }
    std::shared_ptr<BoxWidget> setBottomPx(int v)
    {
        bottom = v;
        hasBottom = true;
        markNeedsLayout();
        return self();
    }
    std::shared_ptr<BoxWidget> setLeftPx(int v)
    {
        left = v;
        hasLeft = true;
        markNeedsLayout();
        return self();
    }
    std::shared_ptr<BoxWidget> setZIndexVal(int z)
    {
        zIndex = z;
        markNeedsPaint();
        return self();
    }

    // ── Responsive overrides ─────────────────────────────────────────────

    std::shared_ptr<BoxWidget> responsive(Breakpoint bp, std::function<void(BoxProps &)> fn)
    {
        overrides_.push_back({bp, std::move(fn)});
        markNeedsLayout();
        return self();
    }

    // ── Layout dispatch ──────────────────────────────────────────────────

    void computeLayout(GraphicsContext &ctx, const BoxConstraints &constraints,
                       FontCache &fontCache) override
    {
        resolved_ = resolveProps(ctx);
        visible = !resolved_.hidden;

        if (!visible)
        {
            // display:none — zero size, no children laid out, scrollbar/
            // gesture state untouched. Matches CSS: display:none removes the
            // whole subtree from flow, not just this box.
            width = 0;
            height = 0;
            needsLayout = false;
            return;
        }

        sb_.userVisible = resolved_.scrollbarVisible;
        sb_.size = resolved_.scrollbarThickness;
        sb_.hoverSize = resolved_.scrollbarHoverThickness;
        sb_.showArrows = resolved_.scrollbarArrows;
        paddingLeft = resolved_.paddingLeft;
        paddingRight = resolved_.paddingRight;
        paddingTop = resolved_.paddingTop;
        paddingBottom = resolved_.paddingBottom;

        switch (resolved_.display)
        {
        case Display::Flex:
            computeLayoutFlex_(ctx, constraints, fontCache);
            break;
        case Display::Grid:
            computeLayoutGrid_(ctx, constraints, fontCache);
            break;
        case Display::Block:
            computeLayoutBlock_(ctx, constraints, fontCache);
            break;
        }

        applyConstraints();

        // position:absolute children, resolved against OUR just-finalized
        // box, regardless of which display mode placed our flow children.
        layoutAbsoluteChildren(this, ctx, fontCache);

        needsLayout = false;
    }

    // See Widget::isVisibleForLayout — lets a PARENT Box resolve our
    // breakpoint-driven `hidden` state before it decides whether to include
    // us in its flow this frame, without doing a full computeLayout() pass.
    bool isVisibleForLayout(GraphicsContext &ctx) override
    {
        resolved_ = resolveProps(ctx);
        visible = !resolved_.hidden;
        return visible;
    }

    void positionChildren(int contentX, int contentY, int contentW, int contentH) override
    {
        switch (resolved_.display)
        {
        case Display::Flex:
            positionChildrenFlex_(contentX, contentY, contentW, contentH);
            break;
        case Display::Grid:
            positionChildrenGrid_(contentX, contentY, contentW, contentH);
            break;
        case Display::Block:
            positionChildrenBlock_(contentX, contentY, contentW, contentH);
            break;
        }
        // Absolute children keep the x/y layoutAbsoluteChildren already gave
        // them in computeLayout(); nothing to redo here.
    }

    // ── Mouse / scroll (shared across all display modes — sb_/gesture_
    //    only care about scroll offset + orientation, not which algorithm
    //    produced containerMainSize_) ────────────────────────────────────

    bool handleMouseWheel(int delta) override
    {
        if (!sb_.onWheel(delta))
            return false;
        repositionChildren();
        markNeedsPaint();
        return true;
    }
    bool handleMouseMove(int mx, int my) override
    {
        // The bar's own track lives in the box's raw bounds, not the
        // padded content box — see scrollbarGutter()/render() below. Hit-
        // testing has to agree with wherever it's actually drawn.
        int cbx = x, cby = y, cbw = width, cbh = height;

        if (sb_.isDragging)
        {
            if (!sb_.onMouseMove(mx, my, cbx, cby, cbw, cbh))
                return false;
            repositionChildren();
            markNeedsPaint();
            return true;
        }
        if (gesture_.isDragging)
        {
            int delta = gesture_.onMove(mx, my);
            if (delta != 0)
            {
                sb_.scrollOffset += delta;
                sb_.clamp();
                sb_.updateThumb();
                repositionChildren();
                markNeedsPaint();
            }
            return true;
        }
        if (sb_.onMouseMove(mx, my, cbx, cby, cbw, cbh))
        {
            markNeedsPaint();
            return true;
        }
        return false;
    }
    bool handleMouseDown(int mx, int my) override
    {
        stopFling();
        int cbx = x, cby = y, cbw = width, cbh = height;

        if (sb_.onMouseDown(mx, my, cbx, cby, cbw, cbh))
        {
            if (sb_.isDragging)
                if (auto *ui = FluxUI::getCurrentInstance())
                    ui->captureMouseInput();
            repositionChildren();
            markNeedsPaint();
            return true;
        }
        if (sb_.isScrollable && mx >= x && mx < x + width && my >= y && my < y + height)
        {
            gesture_.horizontal = sb_.horizontal;
            gesture_.onDown(mx, my);
            if (auto *ui = FluxUI::getCurrentInstance())
                ui->captureMouseInput();
            return true;
        }
        // Only a click target if the caller opted in via setOnClick() —
        // otherwise stay non-consuming so non-interactive Box containers
        // (the overwhelming majority) behave exactly as before and don't
        // block scroll/drag/hover on any ancestor.
        if (onClick && _hit(mx, my))
        {
            _pressed = true;
            markNeedsPaint();
            return true;
        }
        return false;
    }
    bool handleMouseUp(int mx, int my) override
    {
        if (sb_.isDragging)
        {
            sb_.onMouseUp();
            if (auto *ui = FluxUI::getCurrentInstance())
                ui->releaseMouseInput();
            markNeedsPaint();
            return true;
        }
        if (gesture_.isDragging)
        {
            gesture_.onUp(mx, my);
            if (auto *ui = FluxUI::getCurrentInstance())
                ui->releaseMouseInput();
            if (gesture_.isFling())
                startFling();
            markNeedsPaint();
            return true;
        }
        if (_pressed)
        {
            _pressed = false;
            markNeedsPaint();
            // Fire only if release also lands inside bounds — dragging the
            // pointer off the Box before releasing cancels the click, same
            // convention as ButtonWidget::handleMouseUp.
            if (onClick && _hit(mx, my))
                onClick();
            return true;
        }
        return false;
    }
    bool handleMouseLeave() override
    {
        gesture_.cancel();
        // A press that leaves the widget's bounds entirely (mouse capture
        // lost, e.g. window blur mid-drag) shouldn't leave _pressed stuck
        // true forever — clear it defensively, matching IconButtonWidget's
        // handleMouseLeave doing the same for _pressed there.
        _pressed = false;
        if (!sb_.onMouseLeave())
            return false;
        markNeedsPaint();
        return true;
    }

    // ── Render ───────────────────────────────────────────────────────────

    void render(GraphicsContext &ctx, FontCache &fontCache) override
    {
        if (!visible)
            return;

        const BoxProps &P = resolved_;
        Painter painter(ctx, this);

        if (P.hasBackground)
            painter.fillRoundedRect(x, y, width, height, P.borderRadius, P.backgroundColor);
        // Same gutter the compute-layout pass already reserved (see
        // scrollbarGutter()) — keying off resolved_ rather than
        // sb_.isScrollable avoids the reserved space jumping right at the
        // moment content starts/stops overflowing.
        int gutter = scrollbarGutter();
        int clipX1 = x, clipY1 = y, clipX2 = x + width, clipY2 = y + height;
        if (gutter > 0)
        {
            if (sb_.horizontal)
                clipY2 -= gutter;
            else
                clipX2 -= gutter;
        }
        painter.pushClipRect(clipX1, clipY1, clipX2 - clipX1, clipY2 - clipY1);

        // Flow children first, in tree order (item sources render via
        // whatever widgets they expanded to — those are ordinary children
        // by the time render() walks the tree).
        for (auto &child : children)
        {
            if (!child->visible || child->position == Position::Absolute)
                continue;
            bool onScreen = child->x + child->width >= clipX1 && child->x < clipX2 &&
                            child->y + child->height >= clipY1 && child->y < clipY2;
            if (onScreen)
                child->render(ctx, fontCache);
        }

        painter.popClipRect();

        // Absolute children paint on top, in zIndex order, un-clipped by
        // our own scroll clip (matches CSS: an absolutely positioned
        // element escapes overflow:hidden on a plain block ancestor too,
        // though real CSS ties this to `overflow` specifically — treating
        // it as always-escaping here is the simpler, predictable default).
        std::vector<Widget *> abs;
        for (auto &child : children)
            if (child->visible && child->position == Position::Absolute)
                abs.push_back(child.get());
        std::stable_sort(abs.begin(), abs.end(),
                         [](Widget *a, Widget *b)
                         { return a->zIndex < b->zIndex; });
        for (auto *child : abs)
            child->render(ctx, fontCache);

        if (P.hasBorder)
            painter.drawBorder(x, y, width, height, P.borderRadius, P.borderColor, P.borderWidth);

        // Full box bounds, not the padded content box — the bar's own
        // track sits in the reserved gutter outside the padding, same as
        // a real non-overlay scrollbar. sb_'s own thick/inset math then
        // draws the actual thumb a couple px in from that true edge.
        sb_.render(ctx, x, y, width, height, this);

        needsPaint = false;
    }
};

using BoxWidgetPtr = std::shared_ptr<BoxWidget>;

// ============================================================================
// FACTORY
// ============================================================================
//
// BoxChild accepts a bare WidgetPtr, a vector<WidgetPtr> (what Map(...) and
// any hand-rolled loop-building helper returns), or a BoxItemPtr (explicit
// Grid placement). All three can be freely mixed in one initializer list:
//
//   Box({
//       Text("Header"),                                  // WidgetPtr
//       Map(todos, keyFn, [](int i, const Todo &t){...}), // MapWidgetPtr -> WidgetPtr
//       BoxItem(Text("Wide"))->spanCols(2),                // BoxItemPtr (Grid mode)
//   })
//
// Note Map(...) itself returns a MapWidgetPtr, which converts to WidgetPtr
// (MapWidget derives from Widget) — it's added as a single ordinary child
// that happens to answer isItemSource()==true; Box's own collectFlowChildren
// is what actually expands it at layout time, not this factory.
// ============================================================================

using BoxChild = std::variant<WidgetPtr, BoxItemPtr, std::vector<WidgetPtr>>;

namespace flux_box_detail
{
    inline void addBoxChild(BoxWidget *w, const BoxChild &child)
    {
        std::visit([&](auto &&arg)
                   {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, WidgetPtr>)
            {
                if (arg) w->addChildPlain(arg);
            }
            else if constexpr (std::is_same_v<T, std::vector<WidgetPtr>>)
            {
                for (auto &wp : arg)
                    if (wp) w->addChildPlain(wp);
            }
            else
            {
                w->addItem(arg);
            } }, child);
    }
} // namespace flux_box_detail

inline BoxWidgetPtr Box(std::initializer_list<BoxChild> children = {})
{
    auto w = std::make_shared<BoxWidget>();
    w->setSelf(w);
    for (auto &c : children)
        flux_box_detail::addBoxChild(w.get(), c);
    return w;
}

inline BoxWidgetPtr Box(std::vector<BoxChild> children)
{
    auto w = std::make_shared<BoxWidget>();
    w->setSelf(w);
    for (auto &c : children)
        flux_box_detail::addBoxChild(w.get(), c);
    return w;
}

// ── RN/web-flavored convenience aliases — same widget, friendlier defaults
//    and names for people coming from React Native / HTML+CSS. ─────────────

inline BoxWidgetPtr Flex(std::initializer_list<BoxChild> children = {})
{
    auto w = Box(children);
    w->setDisplay(Display::Flex)->setDirection(FlexDirection::Row);
    return w;
}

// Row / Column: explicit one-axis flex shorthand for the common case where
// you don't need any other Flex/Grid configuration.
inline BoxWidgetPtr Row(std::initializer_list<BoxChild> children = {})
{
    auto w = Box(children);
    w->setDisplay(Display::Flex)->setDirection(FlexDirection::Row);
    return w;
}
inline BoxWidgetPtr Column(std::initializer_list<BoxChild> children = {})
{
    auto w = Box(children);
    w->setDisplay(Display::Flex)->setDirection(FlexDirection::Column);
    return w;
}

#endif // FLUX_BOX_HPP