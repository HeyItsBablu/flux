#ifndef FLUX_FLEX_HPP
#define FLUX_FLEX_HPP

#include "../flux_core.hpp"
#include "../flux_state.hpp"
#include "../flux_gesture.hpp"
#include "flux_collection.hpp"
#include <functional>
#include <vector>
#include <algorithm>

// ============================================================================
// ENUMS
// ============================================================================

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

// ============================================================================
// BREAKPOINTS
// ============================================================================

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
    static Breakpoints &instance()
    {
        static Breakpoints b;
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
// FLEX PROPS — everything about the CONTAINER (not its children's own size)
// ============================================================================

struct FlexProps
{
    FlexDirection direction = FlexDirection::Row;
    FlexWrap wrap = FlexWrap::NoWrap;
    JustifyContent justify = JustifyContent::Start;
    AlignItems alignItems = AlignItems::Stretch;
    AlignContent alignContent = AlignContent::Start;
    int gap = 0;

    int paddingLeft = 0, paddingRight = 0, paddingTop = 0, paddingBottom = 0;

    bool hasBackground = false;
    Color backgroundColor = Color::fromRGB(255, 255, 255);
    bool hasBorder = false;
    Color borderColor = Color::fromRGB(0, 0, 0);
    int borderWidth = 1;
    int borderRadius = 0;

    bool scrollable = false;
};

// ============================================================================
// PER-CHILD FLEX-ITEM HELPERS (mirror Positioned() style — decorate & return)
// ============================================================================

template <typename T>
inline std::shared_ptr<T> WidthMode(std::shared_ptr<T> w, SizeMode m)
{
    w->widthMode = m;
    return w;
}
template <typename T>
inline std::shared_ptr<T> HeightMode(std::shared_ptr<T> w, SizeMode m)
{
    w->heightMode = m;
    return w;
}

// ============================================================================
// FLEX WIDGET
// ============================================================================

class FlexWidget : public Widget
{
private:
    FlexProps baseProps_;
    std::vector<std::pair<Breakpoint, std::function<void(FlexProps &)>>> overrides_;

    // ── scroll state (same struct used by old ScrollView/ListView) ─────────
    ScrollbarState sb_;
    GestureState gesture_;
    TimerID flingTimer_ = 0;
    std::shared_ptr<FlexWidget> self_;

    // ── resolved-this-frame state, used by positionChildren/render ─────────
    struct LineMetric
    {
        std::vector<Widget *> items;
        std::vector<int> resolvedMain; // parallel to items
        int usedMain = 0;
        int crossSize = 0;
    };
    std::vector<LineMetric> lines_;
    FlexProps resolved_;
    bool isRowAxis_ = true;
    bool mainIsReversed_ = false;
    bool wrapReversed_ = false;
    bool scrollAxisIsMain_ = true;
    int containerMainSize_ = 0;
    int containerCrossSize_ = 0;
    int totalCross_ = 0;

    // ── helpers ──────────────────────────────────────────────────────────────

    FlexProps resolveProps() const
    {
        FlexProps p = baseProps_;
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

    static int mainSize(const Widget *w, bool rowAxis) { return rowAxis ? w->width : w->height; }
    static int crossSize(const Widget *w, bool rowAxis) { return rowAxis ? w->height : w->width; }

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
    repositionChildren();  // ← was markNeedsLayout()
    markNeedsPaint();
    if (auto *u = FluxUI::getCurrentInstance()) u->invalidateWidget(this); });
        }
    }

public:
    ~FlexWidget() override { stopFling(); }
    void setSelf(std::shared_ptr<FlexWidget> ptr) { self_ = ptr; }
    std::shared_ptr<FlexWidget> self() { return self_; }

    // ── Base (mobile-first) setters ───────────────────────────────────────

    std::shared_ptr<FlexWidget> setDirection(FlexDirection d)
    {
        baseProps_.direction = d;
        markNeedsLayout();
        return self();
    }
    std::shared_ptr<FlexWidget> setWrap(FlexWrap w)
    {
        baseProps_.wrap = w;
        markNeedsLayout();
        return self();
    }
    std::shared_ptr<FlexWidget> setJustifyContent(JustifyContent j)
    {
        baseProps_.justify = j;
        markNeedsLayout();
        return self();
    }
    std::shared_ptr<FlexWidget> setAlignItems(AlignItems a)
    {
        baseProps_.alignItems = a;
        markNeedsLayout();
        return self();
    }
    std::shared_ptr<FlexWidget> setAlignContent(AlignContent a)
    {
        baseProps_.alignContent = a;
        markNeedsLayout();
        return self();
    }
    std::shared_ptr<FlexWidget> setGap(int g)
    {
        baseProps_.gap = g;
        markNeedsLayout();
        return self();
    }
    std::shared_ptr<FlexWidget> setScrollable(bool s)
    {
        baseProps_.scrollable = s;
        markNeedsLayout();
        return self();
    }

    std::shared_ptr<FlexWidget> setPadding(int p)
    {
        baseProps_.paddingLeft = baseProps_.paddingRight = baseProps_.paddingTop = baseProps_.paddingBottom = p;
        markNeedsLayout();
        return self();
    }
    std::shared_ptr<FlexWidget> setBackgroundColor(Color c)
    {
        baseProps_.hasBackground = true;
        baseProps_.backgroundColor = c;
        markNeedsPaint();
        return self();
    }
    std::shared_ptr<FlexWidget> setBorderColor(Color c)
    {
        baseProps_.hasBorder = true;
        baseProps_.borderColor = c;
        markNeedsPaint();
        return self();
    }
    std::shared_ptr<FlexWidget> setBorderWidth(int w)
    {
        baseProps_.hasBorder = true;
        baseProps_.borderWidth = w;
        markNeedsPaint();
        return self();
    }
    std::shared_ptr<FlexWidget> setBorderRadius(int r)
    {
        baseProps_.borderRadius = r;
        markNeedsPaint();
        return self();
    }

    // self-sizing (this FlexWidget AS a flex item in its parent)
    std::shared_ptr<FlexWidget> setWidthMode(SizeMode m)
    {
        widthMode = m;
        markNeedsLayout();
        return self();
    }
    std::shared_ptr<FlexWidget> setHeightMode(SizeMode m)
    {
        heightMode = m;
        markNeedsLayout();
        return self();
    }
    std::shared_ptr<FlexWidget> setWidth(int w)
    {
        width = w;
        widthMode = SizeMode::Fixed;
        markNeedsLayout();
        return self();
    }
    std::shared_ptr<FlexWidget> setHeight(int h)
    {
        height = h;
        heightMode = SizeMode::Fixed;
        markNeedsLayout();
        return self();
    }

    std::shared_ptr<FlexWidget> setFlexGrow(int g)
    {
        flexGrow = g;
        markNeedsLayout();
        return self();
    }
    std::shared_ptr<FlexWidget> setFlexShrink(int s)
    {
        flexShrink = s;
        markNeedsLayout();
        return self();
    }
    std::shared_ptr<FlexWidget> setFlexBasis(int b)
    {
        flexBasis = b;
        markNeedsLayout();
        return self();
    }
    std::shared_ptr<FlexWidget> setOrder(int o)
    {
        order = o;
        markNeedsLayout();
        return self();
    }

    // ── Responsive overrides ──────────────────────────────────────────────

    std::shared_ptr<FlexWidget> responsive(Breakpoint bp, std::function<void(FlexProps &)> fn)
    {
        overrides_.push_back({bp, std::move(fn)});
        markNeedsLayout();
        return self();
    }

    // ── Layout ─────────────────────────────────────────────────────────────

    void computeLayout(GraphicsContext &ctx, const BoxConstraints &constraints,
                       FontCache &fontCache) override
    {
        resolved_ = resolveProps();
        const FlexProps &P = resolved_;

        paddingLeft = P.paddingLeft;
        paddingRight = P.paddingRight;
        paddingTop = P.paddingTop;
        paddingBottom = P.paddingBottom;

        isRowAxis_ = (P.direction == FlexDirection::Row || P.direction == FlexDirection::RowReverse);
        mainIsReversed_ = (P.direction == FlexDirection::RowReverse || P.direction == FlexDirection::ColumnReverse);
        wrapReversed_ = (P.wrap == FlexWrap::WrapReverse);

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
        scrollAxisIsMain_ = scrollableMain;

        int contentMaxW = std::max(0, outerMaxW - padH);
        int contentMaxH = std::max(0, outerMaxH - padV);

        containerMainSize_ = isRowAxis_ ? contentMaxW : contentMaxH;
        containerCrossSize_ = isRowAxis_ ? contentMaxH : contentMaxW;

        // ── Is THIS Flex's own cross axis being Fit-sized? If so, we must not
        //    force-stretch children to containerCrossSize_, since that size is
        //    just "whatever space was offered", not our actual target size. ────
        SizeMode crossAxisMode = isRowAxis_ ? heightMode : widthMode;
        bool crossIsFit = (crossAxisMode == SizeMode::Fit);

        // ---- STEP 0: order children ----
        std::vector<Widget *> ordered;
        for (auto &c : children)
            if (c->visible)
                ordered.push_back(c.get());
        std::stable_sort(ordered.begin(), ordered.end(),
                         [](Widget *a, Widget *b)
                         { return a->order < b->order; });
        if (mainIsReversed_)
            std::reverse(ordered.begin(), ordered.end());

        // ---- STEP 1: hypothetical main size (flex-basis) per child ----
        std::vector<int> hypoMain(ordered.size());
        for (size_t i = 0; i < ordered.size(); i++)
        {
            Widget *c = ordered[i];
            SizeMode mainMode = isRowAxis_ ? c->widthMode : c->heightMode;

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
                BoxConstraints measureC = isRowAxis_
                                              ? BoxConstraints::loose(kUnbounded, containerCrossSize_)
                                              : BoxConstraints::loose(containerCrossSize_, kUnbounded);
                c->computeLayout(ctx, measureC, fontCache);
                hypoMain[i] = mainSize(c, isRowAxis_);
            }
        }

        // ---- STEP 2: line breaking ----
        lines_.clear();
        if (P.wrap == FlexWrap::NoWrap)
        {
            LineMetric line;
            for (size_t i = 0; i < ordered.size(); i++)
                line.items.push_back(ordered[i]);
            lines_.push_back(std::move(line));
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
                    lines_.push_back(std::move(cur));
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
                lines_.push_back(std::move(cur));
        }
        if (wrapReversed_)
            std::reverse(lines_.begin(), lines_.end());

        auto hypoOf = [&](Widget *w) -> int
        {
            for (size_t i = 0; i < ordered.size(); i++)
                if (ordered[i] == w)
                    return hypoMain[i];
            return 0;
        };

        auto isMainFixed = [&](Widget *c) -> bool
        {
            SizeMode mainMode = isRowAxis_ ? c->widthMode : c->heightMode;
            return mainMode == SizeMode::Fixed && c->flexBasis < 0;
        };

        // ---- STEP 3: per-line flex resolution + final child layout ----
        for (auto &line : lines_)
        {
            int basisSum = 0;
            for (auto *c : line.items)
                basisSum += hypoOf(c);
            int gapSum = (int)(line.items.size() > 1 ? P.gap * (line.items.size() - 1) : 0);
            int lineMainBudget = scrollableMain ? std::max(containerMainSize_, basisSum + gapSum)
                                                : containerMainSize_;
            int freeSpace = lineMainBudget - basisSum - gapSum;

            std::vector<Widget *> active = line.items;
            std::unordered_map<Widget *, int> resolvedMain;
            for (auto *c : line.items)
                resolvedMain[c] = hypoOf(c);

            // Freeze fixed-size items — they never participate in grow/shrink
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

            // ---- first pass: layout each child loosely on cross axis ----
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

            // ---- second pass: stretch items, UNLESS our own cross axis is Fit ----
            int stretchCross = (P.alignItems == AlignItems::Stretch && !crossIsFit)
                                   ? containerCrossSize_
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

            // Recompute lineCrossMax after stretch
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

            // Content-based cross size — NOT forced to containerCrossSize_ ----
            line.crossSize = scrollableCross
                                 ? std::max(containerCrossSize_, lineCrossMax)
                                 : lineCrossMax;

            line.resolvedMain.clear();
            for (auto *c : line.items)
                line.resolvedMain.push_back(resolvedMain[c]);
        }

        // ---- STEP 4: total cross size, our own size ----
        totalCross_ = 0;
        for (size_t i = 0; i < lines_.size(); i++)
        {
            totalCross_ += lines_[i].crossSize;
            if (i + 1 < lines_.size())
                totalCross_ += P.gap;
        }

        int finalW, finalH;
        if (isRowAxis_)
        {
            finalW = (widthMode == SizeMode::Fit) ? std::min(outerMaxW, basisSumOfAllLines(lines_, P.gap)) : outerMaxW;
            finalH = (heightMode == SizeMode::Fit) ? (totalCross_ + padV) : outerMaxH;
            finalW = std::max(finalW, padH);
        }
        else
        {
            finalH = (heightMode == SizeMode::Fit) ? std::min(outerMaxH, basisSumOfAllLines(lines_, P.gap)) : outerMaxH;
            finalW = (widthMode == SizeMode::Fit) ? (totalCross_ + padH) : outerMaxW;
            finalH = std::max(finalH, padV);
        }
        width = self.clampWidth(finalW);
        height = self.clampHeight(finalH);

        // ---- STEP 5: scrollbar feed ----
        if (scrollAxisIsMain_)
        {
            int contentMain = 0;
            for (size_t i = 0; i < lines_.size(); i++)
                contentMain = std::max(contentMain, lines_[i].usedMain);
            sb_.horizontal = isRowAxis_;
            sb_.contentMain = contentMain;
            sb_.viewportMain = containerMainSize_;
        }
        else
        {
            sb_.horizontal = !isRowAxis_;
            sb_.contentMain = totalCross_;
            sb_.viewportMain = containerCrossSize_;
        }
        sb_.setScrollable(P.scrollable && sb_.contentMain > sb_.viewportMain);
        sb_.clamp();
        sb_.updateThumb();

        applyConstraints();
        needsLayout = false;
    }

    static int basisSumOfAllLines(const std::vector<LineMetric> &lines, int gap)
    {
        int maxLine = 0;
        for (auto &l : lines)
            maxLine = std::max(maxLine, l.usedMain);
        return maxLine;
    }

    // ── Position ───────────────────────────────────────────────────────────

    void positionChildren(int contentX, int contentY, int /*cw*/, int /*ch*/) override
    {
        const FlexProps &P = resolved_;
        int crossFree = (P.scrollable && P.wrap != FlexWrap::NoWrap)
                            ? 0
                            : (containerCrossSize_ - totalCross_);

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
            lineGapExtra = lines_.size() > 1 ? (double)crossFree / (lines_.size() - 1) : 0;
            break;
        case AlignContent::SpaceAround:
            lineGapExtra = lines_.empty() ? 0 : (double)crossFree / lines_.size();
            crossOffset = lineGapExtra / 2.0;
            break;
        case AlignContent::SpaceEvenly:
            lineGapExtra = (double)crossFree / (lines_.size() + 1);
            crossOffset = lineGapExtra;
            break;
        case AlignContent::Stretch:
            crossOffset = 0;
            break;
        }

        double cursorCross = crossOffset;
        if (scrollAxisIsMain_ == false && P.scrollable)
            cursorCross -= sb_.scrollOffset;

        for (auto &line : lines_)
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

            for (size_t i = 0; i < line.items.size(); i++)
            {
                Widget *c = line.items[i];
                int cMain = line.resolvedMain[i];
                int cCross = crossSize(c, isRowAxis_);

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

                int mainPx = contentX_or_y(contentX, contentY, true) + (int)std::round(cursorMain);
                int crossPx = contentX_or_y(contentX, contentY, false) + (int)std::round(cursorCross + childCrossOffset);

                if (isRowAxis_)
                {
                    c->x = mainPx;
                    c->y = crossPx;
                }
                else
                {
                    c->x = crossPx;
                    c->y = mainPx;
                }

                c->positionChildren(c->x + c->paddingLeft, c->y + c->paddingTop,
                                    c->width - c->paddingLeft - c->paddingRight,
                                    c->height - c->paddingTop - c->paddingBottom);

                cursorMain += cMain + P.gap + itemGapExtra;
            }

            cursorCross += line.crossSize + P.gap + lineGapExtra;
        }
    }

    void repositionChildren()
    {
        positionChildren(
            x + resolved_.paddingLeft,
            y + resolved_.paddingTop,
            width - resolved_.paddingLeft - resolved_.paddingRight,
            height - resolved_.paddingTop - resolved_.paddingBottom);
        if (auto *ui = FluxUI::getCurrentInstance())
            ui->invalidateWidget(this);
    }

    int contentX_or_y(int contentX, int contentY, bool wantMainAxisOrigin) const
    {
        bool wantX = isRowAxis_ ? wantMainAxisOrigin : !wantMainAxisOrigin;
        return wantX ? contentX : contentY;
    }

    // ── Mouse / scroll ───────────────────────────────────────────────────────

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
        int cbx = x + resolved_.paddingLeft;
        int cby = y + resolved_.paddingTop;
        int cbw = width - resolved_.paddingLeft - resolved_.paddingRight;
        int cbh = height - resolved_.paddingTop - resolved_.paddingBottom;

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
        int cbx = x + resolved_.paddingLeft;
        int cby = y + resolved_.paddingTop;
        int cbw = width - resolved_.paddingLeft - resolved_.paddingRight;
        int cbh = height - resolved_.paddingTop - resolved_.paddingBottom;

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
        if (!sb_.onMouseLeave())
            return false;
        markNeedsPaint();
        return true;
    }

    // ── Render ────────────────────────────────────────────────────────────

    void render(GraphicsContext &ctx, FontCache &fontCache) override
    {
        const FlexProps &P = resolved_;
        Painter painter(ctx);

        if (P.hasBackground)
            painter.fillRoundedRect(x, y, width, height, P.borderRadius, P.backgroundColor);

        int sbSz = sb_.isScrollable ? sb_.size : 0;
        int clipX1 = x, clipY1 = y, clipX2 = x + width, clipY2 = y + height;
        if (sb_.isScrollable)
        {
            if (sb_.horizontal)
                clipY2 -= sbSz;
            else
                clipX2 -= sbSz;
        }
        painter.pushClipRect(clipX1, clipY1, clipX2 - clipX1, clipY2 - clipY1);

        for (auto &child : children)
        {
            if (!child->visible)
                continue;
            bool onScreen = child->x + child->width >= clipX1 && child->x < clipX2 &&
                            child->y + child->height >= clipY1 && child->y < clipY2;
            if (onScreen)
                child->render(ctx, fontCache);
        }

        painter.popClipRect();

        if (P.hasBorder)
            painter.drawBorder(x, y, width, height, P.borderRadius, P.borderColor, P.borderWidth);

        int cbx = x + P.paddingLeft;
        int cby = y + P.paddingTop;
        int cbw = width - P.paddingLeft - P.paddingRight;
        int cbh = height - P.paddingTop - P.paddingBottom;
        sb_.render(ctx, cbx, cby, cbw, cbh);
        needsPaint = false;
    }
};

using FlexWidgetPtr = std::shared_ptr<FlexWidget>;

inline FlexWidgetPtr Flex(std::initializer_list<WidgetPtr> children)
{
    auto w = std::make_shared<FlexWidget>();
    w->setSelf(w);
    for (auto &c : children)
        if (c)
            w->addChild(c);
    return w;
}

#endif // FLUX_FLEX_HPP