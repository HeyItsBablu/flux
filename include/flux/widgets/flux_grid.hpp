#ifndef FLUX_GRID_HPP
#define FLUX_GRID_HPP

// ============================================================================
// FluxGrid — CSS Grid layout widget
//
// Key ideas
// ─────────
//   • Column and row tracks are defined via TrackDef (Fixed, Fr, Auto,
//     MinContent, MaxContent).  repeat() expands patterns into flat vectors.
//   • Children are wrapped in GridItemSpec to carry placement metadata
//     (colStart/colEnd/rowStart/rowEnd), plus optional per-cell alignment
//     overrides (alignSelf / justifySelf).
//   • Auto-placement follows CSS grid-auto-flow: row — fills left-to-right,
//     top-to-bottom, skipping occupied cells.
//   • Explicit children placed out-of-bounds on the ROW axis implicitly
//     generate Auto rows.  Column out-of-bounds is clamped with an assert.
//   • Scrollable grids scroll vertically only (v1).  On the scroll axis
//     (rows), Fr tracks collapse to Auto — they resolve against the viewport
//     width on the column axis as usual.
//   • justifyContent / alignContent distribute free space between tracks
//     (only meaningful when tracks don't fill the container).
//   • Full scrollbar + fling-gesture support (same ScrollbarState /
//     GestureState used by FlexWidget / FlexBuilderWidget).
//   • Responsive overrides follow the same Breakpoint pattern as FlexWidget.
//

//
// Minimal usage — bare widgets, auto-placed
// ──────────────────────────────────────────
//   auto g = Grid({
//       Flex({Text("A")}),
//       Flex({Text("B")}),
//       Flex({Text("C")}),
//   });
//   g->setColumns({fr(1), fr(1), fr(1)})
//    ->setGap(8)
//    ->setScrollable(true);
//
// Mixing bare widgets with explicit placement
// ────────────────────────────────────────────
//   Grid({
//       Flex({Text("Card A")}),                       // auto-placed
//       Flex({Text("Card B")}),                       // auto-placed
//       GridItem(Flex({Text("Wide")}))->spanCols(2),  // explicit span
//       Flex({Text("Card C")}),                       // auto-placed
//   })
//
// Explicit placement (GridItem only)
// ────────────────────────────────────
//   Grid({
//       GridItem(Header())->spanCols(3),
//       GridItem(Sidebar())->atCol(1)->spanRows(2),
//       GridItem(Main())  ->at(2, 1)->span(2, 2),
//       GridItem(Footer())->spanCols(3)->atRow(3),
//   })
//
// repeat() helper
// ───────────────
//   grid->setColumns(repeat(4, fr(1)));               // four equal columns
//   grid->setColumns(repeat(3, {px(120), fr(1)}));    // alternating pairs
// ============================================================================

#include "flux/flux_core.hpp"
#include "flux/flux_state.hpp"
#include "flux/flux_gesture.hpp"
#include "flux_flex.hpp"  // reuses JustifyContent, AlignItems, AlignContent,
                          // Breakpoint, BreakpointProvider, thresholdFor,
                          // ScrollbarState, GestureState

#include <functional>
#include <variant>
#include <vector>
#include <unordered_set>
#include <algorithm>
#include <optional>
#include <cassert>
#include <cmath>
#include <string>

// ============================================================================
// TRACK DEFINITION
// ============================================================================

enum class TrackSizing
{
    Fixed,      // exact pixel size
    Fr,         // fractional share of remaining space (suppressed on scroll axis)
    Fill,       // like Fr but resolves against the container even on the scroll
                // axis — opt-in stretch for non-scrollable row tracks
    Auto,       // shrink to largest child (MinContent on row axis when scrollable)
    MinContent, // smallest the content can be without overflow
    MaxContent  // largest the content prefers to be
};

struct TrackDef
{
    TrackSizing sizing = TrackSizing::Auto;
    int         value  = 1;   // px for Fixed; weight for Fr; unused otherwise
};

// ── Track factories ──────────────────────────────────────────────────────────

inline TrackDef px(int n)          { return {TrackSizing::Fixed,      n}; }
inline TrackDef fr(int n = 1)      { return {TrackSizing::Fr,         n}; }
inline TrackDef fillTrack(int n=1) { return {TrackSizing::Fill,       n}; }
inline TrackDef autoTrack()        { return {TrackSizing::Auto,       0}; }
inline TrackDef minContent()       { return {TrackSizing::MinContent, 0}; }
inline TrackDef maxContent()       { return {TrackSizing::MaxContent, 0}; }
// fillTrack(n) — fractional track that always resolves against the container
// size, even on the vertical (scroll) axis.  Use this instead of fr() on rows
// when the grid is non-scrollable and you want rows to fill the available height.
//
//   ->setRows({px(140), fillTrack(1)})   // row 1 fixed, row 2 fills the rest

// ── repeat() ────────────────────────────────────────────────────────────────
//
// Expands a pattern (single track or multi-track) N times into a flat vector.
//
//   repeat(3, fr(1))                   → {fr,fr,fr}
//   repeat(3, {px(60), fr(1)})         → {px,fr, px,fr, px,fr}

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
// GRID PROPS — container-level settings
// ============================================================================

struct GridProps
{
    std::vector<TrackDef> columns;
    std::vector<TrackDef> rows;

    int columnGap = 0;
    int rowGap    = 0;

    // How free space between tracks is distributed
    JustifyContent justifyContent = JustifyContent::Start;
    AlignContent   alignContent   = AlignContent::Start;

    // Default alignment of children WITHIN their cell
    AlignItems justifyItems = AlignItems::Stretch;  // horizontal
    AlignItems alignItems   = AlignItems::Stretch;  // vertical

    bool scrollable = false;  // vertical scroll; horizontal tracks still use fr

    int paddingLeft = 0, paddingRight = 0, paddingTop = 0, paddingBottom = 0;

    bool  hasBackground    = false;
    Color backgroundColor  = Color::fromRGB(255, 255, 255);
    bool  hasBorder        = false;
    Color borderColor      = Color::fromRGB(0, 0, 0);
    int   borderWidth      = 1;
    int   borderRadius     = 0;
};

// ============================================================================
// GRID ITEM SPEC — wraps a child widget with placement + alignment metadata
// ============================================================================

class GridItemSpec
{
public:
    WidgetPtr widget;

    // 1-based, -1 = auto.  colEnd / rowEnd are exclusive (like CSS span end).
    int colStart = -1, colEnd = -1;
    int rowStart = -1, rowEnd = -1;

    // Per-cell overrides; empty = use container's alignItems / justifyItems
    std::optional<AlignItems> alignSelf;    // vertical within cell
    std::optional<AlignItems> justifySelf;  // horizontal within cell

    std::shared_ptr<GridItemSpec> self_;

    // ── Placement setters ─────────────────────────────────────────────────

    // Place at explicit 1-based column
    std::shared_ptr<GridItemSpec> atCol(int c)
    {
        colStart = c;
        return self_;
    }

    // Place at explicit 1-based row
    std::shared_ptr<GridItemSpec> atRow(int r)
    {
        rowStart = r;
        return self_;
    }

    // Shorthand: place at (col, row)
    std::shared_ptr<GridItemSpec> at(int c, int r)
    {
        colStart = c;
        rowStart = r;
        return self_;
    }

    // Span N columns from colStart (colStart must be set first, or call
    // atCol() before spanCols(), otherwise span is stored as a relative delta
    // and resolved after auto-placement assigns colStart).
    std::shared_ptr<GridItemSpec> spanCols(int n)
    {
        colSpanPending_ = n;   // resolved in GridWidget::resolveSpans()
        if (colStart >= 1)
            colEnd = colStart + n;
        return self_;
    }

    std::shared_ptr<GridItemSpec> spanRows(int n)
    {
        rowSpanPending_ = n;
        if (rowStart >= 1)
            rowEnd = rowStart + n;
        return self_;
    }

    // Span both axes
    std::shared_ptr<GridItemSpec> span(int cols, int rows)
    {
        spanCols(cols);
        spanRows(rows);
        return self_;
    }

    // Per-cell alignment overrides
    std::shared_ptr<GridItemSpec> withAlignSelf(AlignItems a)
    {
        alignSelf = a;
        return self_;
    }
    std::shared_ptr<GridItemSpec> withJustifySelf(AlignItems a)
    {
        justifySelf = a;
        return self_;
    }

    // Pending span values (set before colStart/rowStart is known)
    int colSpanPending_ = 1;
    int rowSpanPending_ = 1;
};

using GridItemPtr = std::shared_ptr<GridItemSpec>;

// Factory
inline GridItemPtr GridItem(WidgetPtr w)
{
    auto s     = std::make_shared<GridItemSpec>();
    s->self_   = s;
    s->widget  = w;
    return s;
}

// ============================================================================
// GRID WIDGET
// ============================================================================

class GridWidget : public Widget
{
private:
    // ── configuration ────────────────────────────────────────────────────────
    GridProps baseProps_;
    std::vector<GridItemPtr> items_;
    std::vector<std::pair<Breakpoint, std::function<void(GridProps &)>>> overrides_;

    std::shared_ptr<GridWidget> self_;

    // ── scroll / gesture ─────────────────────────────────────────────────────
    ScrollbarState sb_;
    GestureState   gesture_;
    TimerID        flingTimer_ = 0;

    // ── resolved per-frame ───────────────────────────────────────────────────
    GridProps resolved_;

    // Resolved track sizes and prefix-sum offsets (content-relative pixels)
    std::vector<int> colSizes_;     // pixel width of each column track
    std::vector<int> rowSizes_;     // pixel height of each row track
    std::vector<int> colOffsets_;   // colOffsets_[i] = x-start of column i (0-based)
    std::vector<int> rowOffsets_;   // rowOffsets_[i] = y-start of row i (0-based)

    int totalContentW_ = 0;
    int totalContentH_ = 0;
    int containerW_    = 0;
    int containerH_    = 0;

    // Per-item resolved placement + pixel geometry
    struct PlacedItem
    {
        GridItemSpec *spec   = nullptr;
        int colStart = 0, colEnd = 0;   // 0-based, exclusive end
        int rowStart = 0, rowEnd = 0;
        int pixX = 0, pixY = 0;         // top-left in content coords
        int pixW = 0, pixH = 0;         // full cell rect
    };
    std::vector<PlacedItem> placed_;

    // ── helpers ──────────────────────────────────────────────────────────────

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
                sb_.clamp();
                sb_.updateThumb();
                applyScrollOffset();
                markNeedsPaint();
                if (auto *u = FluxUI::getCurrentInstance())
                    u->invalidateWidget(this);
            });
        }
    }

    GridProps resolveProps() const
    {
        GridProps p = baseProps_;
        int viewportW = kUnbounded;
        if (auto *ui = FluxUI::getCurrentInstance())
            viewportW = ui->getClientSize().width;
        auto &bps = BreakpointProvider::get();
        for (Breakpoint bp : {Breakpoint::Sm, Breakpoint::Md, Breakpoint::Lg,
                              Breakpoint::Xl, Breakpoint::Xxl})
        {
            if (viewportW < thresholdFor(bp, bps))
                continue;
            for (auto &[obp, fn] : overrides_)
                if (obp == bp)
                    fn(p);
        }
        return p;
    }

    // Resolve pending spans now that colStart / rowStart are known.
    // Called after auto-placement assigns positions to every item.
    void resolveSpans(PlacedItem &pi, GridItemSpec *spec)
    {
        // Column span
        if (pi.colEnd <= pi.colStart)
            pi.colEnd = pi.colStart + std::max(1, spec->colSpanPending_);
        // Row span
        if (pi.rowEnd <= pi.rowStart)
            pi.rowEnd = pi.rowStart + std::max(1, spec->rowSpanPending_);
    }

    // ── Track resolution ──────────────────────────────────────────────────────

    // Measure a child widget's intrinsic size on one axis.
    // Used for Auto / MinContent / MaxContent track sizing.
    int measureChildAxis(GridItemSpec *spec, bool horizontal,
                         GraphicsContext &ctx, FontCache &fontCache,
                         int crossSize)
    {
        if (!spec->widget) return 0;
        BoxConstraints bc = horizontal
            ? BoxConstraints::loose(kUnbounded, crossSize)
            : BoxConstraints::loose(crossSize, kUnbounded);
        spec->widget->computeLayout(ctx, bc, fontCache);
        return horizontal ? spec->widget->width : spec->widget->height;
    }

    // Resolve a set of tracks given the available container size on that axis.
    // When isScrollAxis = true, Fr tracks are treated as Auto.
    std::vector<int> resolveTracks(
        std::vector<TrackDef> tracks,
        int containerSize,
        int gap,
        bool isScrollAxis,
        // items that span exactly one track on this axis (for Auto sizing)
        // outer index = track index (0-based), inner = child specs spanning it
        const std::vector<std::vector<GridItemSpec *>> &singleSpanners,
        GraphicsContext &ctx, FontCache &fontCache,
        int crossSize)
    {
        const int n = (int)tracks.size();
        std::vector<int> sizes(n, 0);

        if (n == 0) return sizes;

        int totalGap = gap * std::max(0, n - 1);

        // ── Pass 1: Fixed tracks ────────────────────────────────────────────
        for (int i = 0; i < n; ++i)
            if (tracks[i].sizing == TrackSizing::Fixed)
                sizes[i] = tracks[i].value;

        // ── Pass 2: Auto / MinContent / MaxContent tracks ───────────────────
        for (int i = 0; i < n; ++i)
        {
            auto sz = tracks[i].sizing;
            bool needsMeasure = (sz == TrackSizing::Auto       ||
                                 sz == TrackSizing::MinContent  ||
                                 sz == TrackSizing::MaxContent  ||
                                 (sz == TrackSizing::Fr && isScrollAxis));
            if (!needsMeasure) continue;

            int best = 0;
            for (auto *spec : singleSpanners[i])
                best = std::max(best, measureChildAxis(spec, !isScrollAxis,
                                                       ctx, fontCache, crossSize));
            sizes[i] = best;
        }

        // ── Pass 3: Fr tracks (column axis only, or when not scroll axis) ───
        if (!isScrollAxis)
        {
            int usedFixed = totalGap;
            int totalFr   = 0;
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

        // ── Pass 4: Fill tracks — always resolve against containerSize ───────
        // Fill behaves identically to Fr except it is never suppressed on the
        // scroll axis.  It is the caller's responsibility not to use fillTrack()
        // on a scrollable grid's row axis (the result would be bounded by the
        // viewport height, making the rows fixed-size and preventing scrolling).
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

    // Compute prefix-sum offsets from sizes + gap.
    // offsets[i] = pixel start of track i (content-relative).
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

    // Distribute free space between tracks according to a JustifyContent /
    // AlignContent value.  Returns the leading offset and per-gap extra.
    static void distributeSpace(int freeSpace, int trackCount,
                                JustifyContent justify,
                                int &outLeading, int &outBetween)
    {
        outLeading = outBetween = 0;
        if (freeSpace <= 0 || trackCount <= 0) return;
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

    // AlignContent overload
    static void distributeSpace(int freeSpace, int trackCount,
                                AlignContent align,
                                int &outLeading, int &outBetween)
    {
        // Map AlignContent → JustifyContent for the shared implementation
        JustifyContent j = JustifyContent::Start;
        switch (align)
        {
        case AlignContent::End:          j = JustifyContent::End;         break;
        case AlignContent::Center:       j = JustifyContent::Center;      break;
        case AlignContent::SpaceBetween: j = JustifyContent::SpaceBetween;break;
        case AlignContent::SpaceAround:  j = JustifyContent::SpaceAround; break;
        case AlignContent::SpaceEvenly:  j = JustifyContent::SpaceEvenly; break;
        default:                                                            break;
        }
        distributeSpace(freeSpace, trackCount, j, outLeading, outBetween);
    }

    // Pixel position of a child within its cell, given alignment
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

    // Recompute child x/y from current scrollOffset without full relayout.
    void applyScrollOffset()
    {
        const GridProps &P = resolved_;
        int contentX = x + P.paddingLeft;
        int contentY = y + P.paddingTop;

        // justify-content / align-content leading offsets (recomputed cheaply)
        int freeW = std::max(0, containerW_ - totalContentW_);
        int freeH = std::max(0, containerH_ - totalContentH_);

        int colLeading = 0, colBetween = 0;
        distributeSpace(freeW, (int)colSizes_.size(),
                        resolved_.justifyContent, colLeading, colBetween);

        int rowLeading = 0, rowBetween = 0;
        distributeSpace(freeH, (int)rowSizes_.size(),
                        resolved_.alignContent, rowLeading, rowBetween);

        int scroll = sb_.scrollOffset;

        for (auto &pi : placed_)
        {
            if (!pi.spec || !pi.spec->widget) continue;

            // Cell top-left in content coords, adjusted for content distribution
            int cellX = contentX + colLeading + pi.pixX
                        + colBetween * pi.colStart;
            int cellY = contentY + rowLeading + pi.pixY
                        + rowBetween * pi.rowStart - scroll;

            Widget *w = pi.spec->widget.get();

            // Alignment within cell
            AlignItems hAlign = pi.spec->justifySelf.value_or(resolved_.justifyItems);
            AlignItems vAlign = pi.spec->alignSelf.value_or(resolved_.alignItems);

            int childW = (hAlign == AlignItems::Stretch) ? pi.pixW : w->width;
            int childH = (vAlign == AlignItems::Stretch) ? pi.pixH : w->height;

            w->x = cellX + alignOffset(pi.pixW, childW, hAlign);
            w->y = cellY + alignOffset(pi.pixH, childH, vAlign);

            w->positionChildren(
                w->x + w->paddingLeft,
                w->y + w->paddingTop,
                w->width  - w->paddingLeft - w->paddingRight,
                w->height - w->paddingTop  - w->paddingBottom);
        }

        if (auto *ui = FluxUI::getCurrentInstance())
            ui->invalidateWidget(this);
    }

public:
    ~GridWidget() override { stopFling(); }

    void setSelf(std::shared_ptr<GridWidget> p) { self_ = p; }
    std::shared_ptr<GridWidget> self() { return self_; }

    // ── Items ─────────────────────────────────────────────────────────────────

    std::shared_ptr<GridWidget> addItem(GridItemPtr item)
    {
        if (!item) return self();
        items_.push_back(item);
        if (item->widget)
        {
            item->widget->parent = this;
            // Also register as a logical child for hit-testing
            Widget::children.push_back(item->widget);
        }
        markNeedsLayout();
        return self();
    }

    // ── GridProps setters ─────────────────────────────────────────────────────

    std::shared_ptr<GridWidget> setColumns(std::vector<TrackDef> cols)
    {
        baseProps_.columns = std::move(cols);
        markNeedsLayout();
        return self();
    }

    std::shared_ptr<GridWidget> setRows(std::vector<TrackDef> rows)
    {
        baseProps_.rows = std::move(rows);
        markNeedsLayout();
        return self();
    }

    std::shared_ptr<GridWidget> setColumnGap(int g)
    {
        baseProps_.columnGap = g;
        markNeedsLayout();
        return self();
    }

    std::shared_ptr<GridWidget> setRowGap(int g)
    {
        baseProps_.rowGap = g;
        markNeedsLayout();
        return self();
    }

    std::shared_ptr<GridWidget> setGap(int g)
    {
        baseProps_.columnGap = baseProps_.rowGap = g;
        markNeedsLayout();
        return self();
    }

    std::shared_ptr<GridWidget> setJustifyContent(JustifyContent j)
    {
        baseProps_.justifyContent = j;
        markNeedsLayout();
        return self();
    }

    std::shared_ptr<GridWidget> setAlignContent(AlignContent a)
    {
        baseProps_.alignContent = a;
        markNeedsLayout();
        return self();
    }

    std::shared_ptr<GridWidget> setJustifyItems(AlignItems a)
    {
        baseProps_.justifyItems = a;
        markNeedsLayout();
        return self();
    }

    std::shared_ptr<GridWidget> setAlignItems(AlignItems a)
    {
        baseProps_.alignItems = a;
        markNeedsLayout();
        return self();
    }

    std::shared_ptr<GridWidget> setScrollable(bool s)
    {
        baseProps_.scrollable = s;
        markNeedsLayout();
        return self();
    }

    std::shared_ptr<GridWidget> setPadding(int p)
    {
        baseProps_.paddingLeft = baseProps_.paddingRight =
            baseProps_.paddingTop = baseProps_.paddingBottom = p;
        markNeedsLayout();
        return self();
    }

    std::shared_ptr<GridWidget> setPaddingHV(int h, int v)
    {
        baseProps_.paddingLeft = baseProps_.paddingRight = h;
        baseProps_.paddingTop  = baseProps_.paddingBottom = v;
        markNeedsLayout();
        return self();
    }

    std::shared_ptr<GridWidget> setBackgroundColor(Color c)
    {
        baseProps_.hasBackground   = true;
        baseProps_.backgroundColor = c;
        markNeedsPaint();
        return self();
    }

    std::shared_ptr<GridWidget> setBorderColor(Color c)
    {
        baseProps_.hasBorder   = true;
        baseProps_.borderColor = c;
        markNeedsPaint();
        return self();
    }

    std::shared_ptr<GridWidget> setBorderWidth(int w)
    {
        baseProps_.hasBorder   = true;
        baseProps_.borderWidth = w;
        markNeedsPaint();
        return self();
    }

    std::shared_ptr<GridWidget> setBorderRadius(int r)
    {
        baseProps_.borderRadius = r;
        markNeedsPaint();
        return self();
    }

    // Self-sizing (when GridWidget is a flex item in a parent Flex)
    std::shared_ptr<GridWidget> setWidthMode(SizeMode m)
    {
        widthMode = m;
        markNeedsLayout();
        return self();
    }

    std::shared_ptr<GridWidget> setHeightMode(SizeMode m)
    {
        heightMode = m;
        markNeedsLayout();
        return self();
    }

    std::shared_ptr<GridWidget> setWidth(int w)
    {
        width     = w;
        widthMode = SizeMode::Fixed;
        markNeedsLayout();
        return self();
    }

    std::shared_ptr<GridWidget> setHeight(int h)
    {
        height     = h;
        heightMode = SizeMode::Fixed;
        markNeedsLayout();
        return self();
    }

    std::shared_ptr<GridWidget> setFlexGrow(int g)
    {
        flexGrow = g;
        markNeedsLayout();
        return self();
    }

    // ── Responsive overrides ──────────────────────────────────────────────────

    std::shared_ptr<GridWidget> responsive(Breakpoint bp,
                                           std::function<void(GridProps &)> fn)
    {
        overrides_.push_back({bp, std::move(fn)});
        markNeedsLayout();
        return self();
    }

    // ── Layout ────────────────────────────────────────────────────────────────

    void computeLayout(GraphicsContext &ctx,
                       const BoxConstraints &constraints,
                       FontCache &fontCache) override
    {
        resolved_ = resolveProps();
        const GridProps &P = resolved_;

        paddingLeft   = P.paddingLeft;
        paddingRight  = P.paddingRight;
        paddingTop    = P.paddingTop;
        paddingBottom = P.paddingBottom;

        BoxConstraints self = selfConstraints(constraints);

        int outerMaxW = (widthMode  == SizeMode::Fixed) ? width  : self.maxWidth;
        int outerMaxH = (heightMode == SizeMode::Fixed) ? height : self.maxHeight;
        if (widthMode  == SizeMode::Full) outerMaxW = self.maxWidth;
        if (heightMode == SizeMode::Full) outerMaxH = self.maxHeight;

        int padH = P.paddingLeft + P.paddingRight;
        int padV = P.paddingTop  + P.paddingBottom;

        containerW_ = std::max(0, (outerMaxW >= kUnbounded ? 0 : outerMaxW) - padH);
        containerH_ = std::max(0, (outerMaxH >= kUnbounded ? 0 : outerMaxH) - padV);

        // ── Step 1: auto-placement ────────────────────────────────────────────
        int numCols = (int)P.columns.size();
        if (numCols == 0) numCols = 1;  // guard: at least 1 implicit column

        placed_.clear();
        placed_.reserve(items_.size());

        // First pass: register all explicitly placed items to build occupancy map
        // Key: col * 10000 + row  (supports grids up to 10000 rows)
        std::unordered_set<int> occupied;

        auto occupyCell = [&](int c, int r) { occupied.insert(c * 10000 + r); };
        auto isOccupied = [&](int c, int r) { return occupied.count(c * 10000 + r) > 0; };

        // Reserve space for placed_ (indices match items_ order)
        placed_.resize(items_.size());
        for (int i = 0; i < (int)items_.size(); ++i)
        {
            auto *spec = items_[i].get();
            PlacedItem &pi = placed_[i];
            pi.spec = spec;

            // Convert 1-based user API to 0-based internal
            bool hasCol = (spec->colStart >= 1);
            bool hasRow = (spec->rowStart >= 1);

            if (hasCol)
            {
                pi.colStart = spec->colStart - 1;
                pi.colEnd   = (spec->colEnd >= 1) ? spec->colEnd - 1
                                                   : pi.colStart + spec->colSpanPending_;
                // Clamp to defined columns (implicit cols not supported)
                pi.colEnd = std::min(pi.colEnd, numCols);
#ifdef FLUX_DEBUG
                if (pi.colStart >= numCols)
                {
                    std::cerr << "[GridWidget] WARNING: item colStart("
                              << spec->colStart << ") exceeds defined columns("
                              << numCols << "). Clamping.\n";
                    pi.colStart = numCols - 1;
                    pi.colEnd   = numCols;
                }
#endif
            }
            if (hasRow)
            {
                pi.rowStart = spec->rowStart - 1;
                pi.rowEnd   = (spec->rowEnd >= 1) ? spec->rowEnd - 1
                                                   : pi.rowStart + spec->rowSpanPending_;
            }

            // Pre-mark occupied cells for explicitly placed items
            if (hasCol && hasRow)
            {
                for (int c = pi.colStart; c < pi.colEnd; ++c)
                    for (int r = pi.rowStart; r < pi.rowEnd; ++r)
                        occupyCell(c, r);
            }
        }

        // Second pass: auto-place items without full explicit placement.
        // Row-flow: advance left-to-right, top-to-bottom.
        int autoCursorCol = 0, autoCursorRow = 0;

        for (int i = 0; i < (int)items_.size(); ++i)
        {
            auto *spec = items_[i].get();
            PlacedItem &pi = placed_[i];

            bool hasCol = (spec->colStart >= 1);
            bool hasRow = (spec->rowStart >= 1);

            if (hasCol && hasRow)
                continue;  // already fully placed above

            int spanC = spec->colSpanPending_;
            int spanR = spec->rowSpanPending_;

            if (hasCol)
            {
                // Column fixed, find next free row
                int col = pi.colStart;
                int row = autoCursorRow;
                while (true)
                {
                    bool fits = true;
                    for (int c = col; c < col + spanC && fits; ++c)
                        for (int r = row; r < row + spanR && fits; ++r)
                            if (isOccupied(c, r))
                                fits = false;
                    if (fits) break;
                    ++row;
                }
                pi.rowStart = row;
                pi.rowEnd   = row + spanR;
                pi.colEnd   = col + spanC;
                for (int c = pi.colStart; c < pi.colEnd; ++c)
                    for (int r = pi.rowStart; r < pi.rowEnd; ++r)
                        occupyCell(c, r);
            }
            else if (hasRow)
            {
                // Row fixed, find next free column
                int row = pi.rowStart;
                int col = 0;
                while (col + spanC <= numCols)
                {
                    bool fits = true;
                    for (int c = col; c < col + spanC && fits; ++c)
                        for (int r = row; r < row + spanR && fits; ++r)
                            if (isOccupied(c, r))
                                fits = false;
                    if (fits) break;
                    ++col;
                    if (col + spanC > numCols) { col = 0; ++row; }
                }
                pi.colStart = col;
                pi.colEnd   = col + spanC;
                pi.rowEnd   = pi.rowStart + spanR;
                for (int c = pi.colStart; c < pi.colEnd; ++c)
                    for (int r = pi.rowStart; r < pi.rowEnd; ++r)
                        occupyCell(c, r);
            }
            else
            {
                // Fully auto: advance cursor
                // Wrap at column boundary
                if (autoCursorCol + spanC > numCols)
                {
                    autoCursorCol = 0;
                    ++autoCursorRow;
                }
                // Skip occupied cells
                while (true)
                {
                    bool fits = true;
                    for (int c = autoCursorCol; c < autoCursorCol + spanC && fits; ++c)
                        for (int r = autoCursorRow; r < autoCursorRow + spanR && fits; ++r)
                            if (isOccupied(c, r))
                                fits = false;
                    if (fits) break;
                    ++autoCursorCol;
                    if (autoCursorCol + spanC > numCols)
                    {
                        autoCursorCol = 0;
                        ++autoCursorRow;
                    }
                }

                pi.colStart = autoCursorCol;
                pi.colEnd   = autoCursorCol + spanC;
                pi.rowStart = autoCursorRow;
                pi.rowEnd   = autoCursorRow + spanR;

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

        // ── Step 2: determine implicit row count ──────────────────────────────
        int maxRow = 0;
        for (auto &pi : placed_)
            maxRow = std::max(maxRow, pi.rowEnd);

        // Build explicit + implicit row track list
        std::vector<TrackDef> effectiveRows = P.rows;
        while ((int)effectiveRows.size() < maxRow)
            effectiveRows.push_back(autoTrack());  // implicit rows are Auto

        // ── Step 3: build single-spanner lookup tables ─────────────────────
        // For each track, collect items that span exactly that one track
        // (used by Auto / MinContent / MaxContent sizing).
        std::vector<std::vector<GridItemSpec *>> colSingleSpan(numCols);
        std::vector<std::vector<GridItemSpec *>> rowSingleSpan(effectiveRows.size());

        for (auto &pi : placed_)
        {
            if (!pi.spec) continue;
            if (pi.colEnd - pi.colStart == 1)
                if (pi.colStart < numCols)
                    colSingleSpan[pi.colStart].push_back(pi.spec);
            if (pi.rowEnd - pi.rowStart == 1)
                if (pi.rowStart < (int)effectiveRows.size())
                    rowSingleSpan[pi.rowStart].push_back(pi.spec);
        }

        // ── Step 4: resolve column tracks (cross axis — fr works normally) ────
        colSizes_ = resolveTracks(P.columns, containerW_, P.columnGap,
                                  /*isScrollAxis=*/false,
                                  colSingleSpan, ctx, fontCache, containerH_);
        colOffsets_ = makeOffsets(colSizes_, P.columnGap);

        int colGapTotal = P.columnGap * std::max(0, numCols - 1);
        totalContentW_ = 0;
        for (int s : colSizes_) totalContentW_ += s;
        totalContentW_ += colGapTotal;

        // ── Step 5: resolve row tracks (scroll axis — fr → Auto) ─────────────
        rowSizes_ = resolveTracks(effectiveRows, containerH_, P.rowGap,
                                  /*isScrollAxis=*/P.scrollable,
                                  rowSingleSpan, ctx, fontCache, containerW_);
        rowOffsets_ = makeOffsets(rowSizes_, P.rowGap);

        int rowGapTotal = P.rowGap * std::max(0, (int)rowSizes_.size() - 1);
        totalContentH_ = 0;
        for (int s : rowSizes_) totalContentH_ += s;
        totalContentH_ += rowGapTotal;

        // ── Step 6: compute pixel rects for each placed item ──────────────────
        for (auto &pi : placed_)
        {
            if (!pi.spec) continue;

            // Guard bounds
            int cs = std::min(pi.colStart, (int)colOffsets_.size() - 1);
            int ce = std::min(pi.colEnd,   (int)colSizes_.size());
            int rs = std::min(pi.rowStart, (int)rowOffsets_.size() - 1);
            int re = std::min(pi.rowEnd,   (int)rowSizes_.size());

            pi.pixX = (cs >= 0 && cs < (int)colOffsets_.size())
                      ? colOffsets_[cs] : 0;
            pi.pixY = (rs >= 0 && rs < (int)rowOffsets_.size())
                      ? rowOffsets_[rs] : 0;

            // Width = sum of spanned column sizes + inner gaps
            pi.pixW = 0;
            for (int c = cs; c < ce; ++c)
            {
                pi.pixW += colSizes_[c];
                if (c + 1 < ce) pi.pixW += P.columnGap;
            }
            // Height = sum of spanned row sizes + inner gaps
            pi.pixH = 0;
            for (int r = rs; r < re; ++r)
            {
                pi.pixH += rowSizes_[r];
                if (r + 1 < re) pi.pixH += P.rowGap;
            }
        }

        // ── Step 7: lay out each child into its resolved cell ─────────────────
        for (auto &pi : placed_)
        {
            if (!pi.spec || !pi.spec->widget) continue;
            Widget *w = pi.spec->widget.get();

            AlignItems hAlign = pi.spec->justifySelf.value_or(P.justifyItems);
            AlignItems vAlign = pi.spec->alignSelf.value_or(P.alignItems);

            // Tight constraints when stretching; loose when not
            int minW = (hAlign == AlignItems::Stretch) ? pi.pixW : 0;
            int minH = (vAlign == AlignItems::Stretch) ? pi.pixH : 0;

            BoxConstraints childC(minW, pi.pixW, minH, pi.pixH);
            w->computeLayout(ctx, childC, fontCache);
        }

        // ── Step 8: our own size ──────────────────────────────────────────────
        int finalW, finalH;
        if (widthMode == SizeMode::Fit)
            finalW = std::min(outerMaxW, totalContentW_ + padH);
        else
            finalW = outerMaxW >= kUnbounded ? totalContentW_ + padH : outerMaxW;

        if (heightMode == SizeMode::Fit)
            finalH = totalContentH_ + padV;
        else
            finalH = outerMaxH >= kUnbounded ? totalContentH_ + padV : outerMaxH;

        finalW = std::max(finalW, padH);
        finalH = std::max(finalH, padV);

        width  = self.clampWidth(finalW);
        height = self.clampHeight(finalH);

        // ── Step 9: scrollbar ─────────────────────────────────────────────────
        sb_.horizontal    = false;   // vertical scroll only
        sb_.contentMain   = totalContentH_;
        sb_.viewportMain  = containerH_;
        sb_.setScrollable(P.scrollable && sb_.contentMain > sb_.viewportMain);
        sb_.clamp();
        sb_.updateThumb();

        applyConstraints();
        needsLayout = false;
    }

    // ── Position ──────────────────────────────────────────────────────────────

    void positionChildren(int contentX, int contentY,
                          int /*contentW*/, int /*contentH*/) override
    {
        applyScrollOffset();
        (void)contentX; (void)contentY;
        // applyScrollOffset uses this->x + padding directly, which is
        // equivalent — we keep the override for the Widget protocol.
    }

    // ── Mouse / scroll ────────────────────────────────────────────────────────

    bool handleMouseWheel(int delta) override
    {
        if (!sb_.onWheel(delta)) return false;
        applyScrollOffset();
        markNeedsPaint();
        return true;
    }

    bool handleMouseDown(int mx, int my) override
    {
        stopFling();
        int cbx = x + resolved_.paddingLeft,  cby = y + resolved_.paddingTop;
        int cbw = width  - resolved_.paddingLeft - resolved_.paddingRight;
        int cbh = height - resolved_.paddingTop  - resolved_.paddingBottom;

        if (sb_.onMouseDown(mx, my, cbx, cby, cbw, cbh))
        {
            if (sb_.isDragging)
                if (auto *ui = FluxUI::getCurrentInstance())
                    ui->captureMouseInput();
            applyScrollOffset();
            markNeedsPaint();
            return true;
        }
        if (sb_.isScrollable &&
            mx >= x && mx < x + width && my >= y && my < y + height)
        {
            gesture_.horizontal = false;
            gesture_.onDown(mx, my);
            if (auto *ui = FluxUI::getCurrentInstance())
                ui->captureMouseInput();
            return true;
        }
        return false;
    }

    bool handleMouseMove(int mx, int my) override
    {
        int cbx = x + resolved_.paddingLeft,  cby = y + resolved_.paddingTop;
        int cbw = width  - resolved_.paddingLeft - resolved_.paddingRight;
        int cbh = height - resolved_.paddingTop  - resolved_.paddingBottom;

        if (sb_.isDragging)
        {
            if (!sb_.onMouseMove(mx, my, cbx, cby, cbw, cbh)) return false;
            applyScrollOffset();
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
                applyScrollOffset();
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
        return false;
    }

    bool handleMouseLeave() override
    {
        gesture_.cancel();
        if (!sb_.onMouseLeave()) return false;
        markNeedsPaint();
        return true;
    }

    // ── Render ────────────────────────────────────────────────────────────────

    void render(GraphicsContext &ctx, FontCache &fontCache) override
    {
        const GridProps &P = resolved_;
        Painter painter(ctx);

        if (P.hasBackground)
            painter.fillRoundedRect(x, y, width, height,
                                    P.borderRadius, P.backgroundColor);

        int sbSz  = sb_.isScrollable ? sb_.size : 0;
        int clipX1 = x,           clipY1 = y;
        int clipX2 = x + width,   clipY2 = y + height;
        if (sb_.isScrollable)
            clipX2 -= sbSz;

        painter.pushClipRect(clipX1, clipY1, clipX2 - clipX1, clipY2 - clipY1);

        for (auto &pi : placed_)
        {
            if (!pi.spec || !pi.spec->widget) continue;
            Widget *w = pi.spec->widget.get();
            if (!w->visible) continue;
            bool onScreen =
                w->x + w->width  >= clipX1 && w->x < clipX2 &&
                w->y + w->height >= clipY1 && w->y < clipY2;
            if (onScreen)
                w->render(ctx, fontCache);
        }

        painter.popClipRect();

        if (P.hasBorder)
            painter.drawBorder(x, y, width, height,
                               P.borderRadius, P.borderColor, P.borderWidth);

        int cbx = x + P.paddingLeft, cby = y + P.paddingTop;
        int cbw = width  - P.paddingLeft - P.paddingRight;
        int cbh = height - P.paddingTop  - P.paddingBottom;
        sb_.render(ctx, cbx, cby, cbw, cbh);

        needsPaint = false;
    }

    // ── Debug helpers ─────────────────────────────────────────────────────────

    int resolvedColCount() const { return (int)colSizes_.size(); }
    int resolvedRowCount() const { return (int)rowSizes_.size(); }
    int colSize(int i)     const { return (i < (int)colSizes_.size()) ? colSizes_[i] : 0; }
    int rowSize(int i)     const { return (i < (int)rowSizes_.size()) ? rowSizes_[i] : 0; }
};

using GridWidgetPtr = std::shared_ptr<GridWidget>;

// ============================================================================
// FACTORY
// ============================================================================
//
// GridChild is a variant that accepts either a bare WidgetPtr (auto-placed,
// no explicit positioning) or a GridItemPtr (explicit placement / span /
// alignment overrides).  Both convert implicitly at the call site so you can
// mix them freely in a single initializer list:
//
//   Grid({
//       Flex({Text("A")}),                           // WidgetPtr  — auto
//       Flex({Text("B")}),                           // WidgetPtr  — auto
//       GridItem(Flex({Text("Wide")}))->spanCols(2), // GridItemPtr — explicit
//       Flex({Text("C")}),                           // WidgetPtr  — auto
//   })
//
// ============================================================================

using GridChild = std::variant<WidgetPtr, GridItemPtr>;

namespace detail
{
    inline void addGridChild(GridWidget *w, const GridChild &child)
    {
        std::visit([&](auto &&arg)
        {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, WidgetPtr>)
                w->addItem(GridItem(arg));
            else
                w->addItem(arg);
        }, child);
    }
} // namespace detail

inline GridWidgetPtr Grid(std::initializer_list<GridChild> children)
{
    auto w = std::make_shared<GridWidget>();
    w->setSelf(w);
    for (auto &child : children)
        detail::addGridChild(w.get(), child);
    return w;
}

inline GridWidgetPtr Grid(std::vector<GridChild> children)
{
    auto w = std::make_shared<GridWidget>();
    w->setSelf(w);
    for (auto &child : children)
        detail::addGridChild(w.get(), child);
    return w;
}

// Convenience overload for the all-GridItemPtr case (explicit-placement-heavy
// layouts where every child has positioning metadata).
inline GridWidgetPtr Grid(std::vector<GridItemPtr> items)
{
    auto w = std::make_shared<GridWidget>();
    w->setSelf(w);
    for (auto &item : items)
        w->addItem(item);
    return w;
}

#endif // FLUX_GRID_HPP