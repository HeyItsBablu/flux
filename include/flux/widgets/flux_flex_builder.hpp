#ifndef FLUX_FLEX_BUILDER_HPP
#define FLUX_FLEX_BUILDER_HPP

// ============================================================================
// FlexBuilder — dynamic, key-aware, virtualised Flex widget
//
//
// Key ideas
// ─────────
//   • The caller supplies an itemCount + an itemBuilder callback (index → Widget).
//   • Items are built lazily on-demand and cached by their key (default key =
//     index, but a custom keyFn can be provided for stable reordering).
//   • Only visible items are rendered (viewport culling), though layout is
//     always performed for all items so that scrollbar math stays exact.
//     Set virtualizeLayout = true to also skip layout for off-screen items
//     (faster, but requires every item to declare a fixed main-axis extent via
//     setItemExtent()).
//   • Supports all FlexProps (direction, wrap, justify, alignItems, gap, padding,
//     background, border, scrollable, responsive overrides, …).
//   • Full scrollbar + fling-gesture support (same ScrollbarState / GestureState
//     used by FlexWidget).
//   • Public API is intentionally parallel to FlexWidget — every setter returns
//     shared_ptr<FlexBuilderWidget> so calls can be chained.
//
// Minimal usage
// ─────────────
//   auto list = FlexBuilder(
//       100,                                       // itemCount
//       [](int i) { return Text("Item " + std::to_string(i)); }
//   );
//   list->setDirection(FlexDirection::Column)
//       ->setScrollable(true)
//       ->setGap(8);
//
// With a custom key function (stable across reorder / insert / delete)
// ─────────────────────────────────────────────────────────────────────
//   list->setKeyFn([&data](int i){ return data[i].id; });
//
// Virtualised layout (large homogeneous lists)
// ─────────────────────────────────────────────
//   list->setItemExtent(48)          // every item is exactly 48 px tall/wide
//       ->setVirtualizeLayout(true); // skip layout for off-screen items
//
// ============================================================================

#include "flux/flux_core.hpp"
#include "flux/flux_state.hpp"
#include "flux/flux_gesture.hpp"
#include "flux_flex.hpp" // reuses FlexProps, enums, BreakpointProvider

#include <functional>
#include <unordered_map>
#include <string>
#include <vector>
#include <algorithm>
#include <cmath>
#include <optional>

// ============================================================================
// KEY TYPE
// ============================================================================
//
// WHY KEYS MATTER
// ───────────────
// FlexBuilder caches one Widget per item so that widget state (scroll position,
// text-input content, checkbox value, etc.) survives layout passes.
//
// If keys are index-based (the default) and you DELETE item[2] from a list of
// 10, the cache maps:
//   key(2) → old widget for item[2]   ← now shows at item[2] but data is item[3]
//   key(9) → stale widget for item[9] ← item[9] no longer exists
//
// The CORRECT approach: give every data item a STABLE identity that doesn't
// change when the list is reordered/mutated.  Supply that via setKeyFn().
//
//   list->setKeyFn([&data](int i){ return FlexItemKey::fromString(data[i].id); });
//
// Then delete(2):
//   key("id-of-item-2") is pruned → widget discarded   ✓
//   key("id-of-item-3") still maps to item[3]'s widget ✓
//   key("id-of-item-9") still maps to item[9]'s widget ✓
//
// For STATIC lists that never change, index keys are fine and are the default.

struct FlexItemKey
{
    enum class Kind
    {
        Index,
        String,
        Int64
    } kind = Kind::Index;
    int indexKey = 0;
    std::string stringKey;
    int64_t int64Key = 0;

    static FlexItemKey fromIndex(int i)
    {
        FlexItemKey k;
        k.kind = Kind::Index;
        k.indexKey = i;
        return k;
    }
    static FlexItemKey fromString(std::string s)
    {
        FlexItemKey k;
        k.kind = Kind::String;
        k.stringKey = std::move(s);
        return k;
    }
    static FlexItemKey fromInt64(int64_t id)
    {
        FlexItemKey k;
        k.kind = Kind::Int64;
        k.int64Key = id;
        return k;
    }

    bool operator==(const FlexItemKey &o) const
    {
        if (kind != o.kind)
            return false;
        switch (kind)
        {
        case Kind::Index:
            return indexKey == o.indexKey;
        case Kind::String:
            return stringKey == o.stringKey;
        case Kind::Int64:
            return int64Key == o.int64Key;
        }
        return false;
    }
};

struct FlexItemKeyHash
{
    std::size_t operator()(const FlexItemKey &k) const
    {
        switch (k.kind)
        {
        case FlexItemKey::Kind::Index:
            return std::hash<int>{}(k.indexKey);
        case FlexItemKey::Kind::String:
            return std::hash<std::string>{}(k.stringKey);
        case FlexItemKey::Kind::Int64:
            return std::hash<int64_t>{}(k.int64Key);
        }
        return 0;
    }
};

using ItemBuilderFn = std::function<WidgetPtr(int index)>;
using KeyFn = std::function<FlexItemKey(int index)>;

// ============================================================================
// FLEX BUILDER WIDGET
// ============================================================================

class FlexBuilderWidget : public Widget
{
private:
    // ── configuration ───────────────────────────────────────────────────────
    int itemCount_ = 0;
    ItemBuilderFn itemBuilder_;
    KeyFn keyFn_;                // nullptr → index keys (safe for static lists only)
    bool usingIndexKeys_ = true; // false once setKeyFn() is called

    FlexProps baseProps_;
    std::vector<std::pair<Breakpoint, std::function<void(FlexProps &)>>> overrides_;

    // Optional fixed item extent (enables virtualizeLayout_)
    std::optional<int> itemExtent_;
    bool virtualizeLayout_ = false;

    // State listener token — kept alive as long as this widget is alive so we
    // automatically re-layout when a bound State<vector<T>> changes.
    // We store it as a generic function so the widget doesn't need to know T.
    std::function<void()> stateListenerCleanup_; // called in destructor

    // ── item cache ──────────────────────────────────────────────────────────
    // Widgets are cached by key so they survive partial rebuilds and retain
    // their own state (scroll pos, text-input content, etc.).
    struct CacheEntry
    {
        WidgetPtr widget;
        int lastUsedGeneration = 0;
    };
    using Cache = std::unordered_map<FlexItemKey, CacheEntry, FlexItemKeyHash>;
    Cache cache_;
    int generation_ = 0; // bumped on each full rebuild; stale entries pruned

    // ── scroll / gesture ────────────────────────────────────────────────────
    ScrollbarState sb_;
    GestureState gesture_;
    TimerID flingTimer_ = 0;
    std::shared_ptr<FlexBuilderWidget> self_;

    // ── resolved per-frame state ─────────────────────────────────────────────
    FlexProps resolved_;
    bool isRowAxis_ = false;
    bool mainIsReversed_ = false;
    bool wrapReversed_ = false;
    bool scrollAxisIsMain_ = true;
    int containerMainSize_ = 0;
    int containerCrossSize_ = 0;

    // Per-item resolved geometry (populated in computeLayout)
    struct ItemLayout
    {
        FlexItemKey key;
        Widget *widget = nullptr;
        int mainOffset = 0; // offset from content origin along main axis
        int mainSize = 0;
        int crossSize = 0;
    };
    std::vector<ItemLayout> itemLayouts_;
    int totalMain_ = 0;  // sum of item sizes + gaps
    int totalCross_ = 0; // for single-line: max cross size; for wrapping lines: sum

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
                sb_.clamp(); sb_.updateThumb();
                applyScrollOffset();
                markNeedsPaint();
                if (auto *u = FluxUI::getCurrentInstance()) u->invalidateWidget(this); });
        }
    }

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

    FlexItemKey keyForIndex(int i) const
    {
        if (keyFn_)
            return keyFn_(i);
#ifdef FLUX_DEBUG
        // Index keys are only safe for static lists.  If you mutate the data
        // (insert/delete/reorder) without providing a keyFn, widgets will be
        // matched to the wrong items.  Call setKeyFn() to fix this.
        // static so the warning fires at most once per widget instance.
        static bool warned = false;
        if (!warned)
        {
            warned = true;
            std::cerr << "[FlexBuilder] WARNING: using index keys on a list that may "
                         "be mutated.  Call setKeyFn() with stable item identifiers.\n";
        }
#endif
        return FlexItemKey::fromIndex(i);
    }

    // Retrieve or build the widget for a given index, updating cache.
    Widget *getOrBuildItem(int index, GraphicsContext &ctx,
                           const BoxConstraints &childConstraints,
                           FontCache &fontCache)
    {
        FlexItemKey key = keyForIndex(index);
        auto it = cache_.find(key);
        if (it != cache_.end())
        {
            it->second.lastUsedGeneration = generation_;
            Widget *w = it->second.widget.get();
            if (w->needsLayout)
                w->computeLayout(ctx, childConstraints, fontCache);
            return w;
        }
        // Build new item
        WidgetPtr built = itemBuilder_(index);
        if (!built)
            return nullptr;
        built->computeLayout(ctx, childConstraints, fontCache);
        cache_[key] = CacheEntry{built, generation_};
        // Register as logical child so hit-testing, hover, etc. still work
        built->parent = this;
        return built.get();
    }

    // Remove cache entries that were not used this generation
    void pruneCache()
    {
        for (auto it = cache_.begin(); it != cache_.end();)
        {
            if (it->second.lastUsedGeneration < generation_)
                it = cache_.erase(it);
            else
                ++it;
        }
    }

    static int mainDim(Widget *w, bool rowAxis) { return rowAxis ? w->width : w->height; }
    static int crossDim(Widget *w, bool rowAxis) { return rowAxis ? w->height : w->width; }

    // Recompute child x/y from the current scrollOffset without full relayout.
    // Delegates to positionChildren so there is exactly one positioning code path.
    void applyScrollOffset()
    {
        const FlexProps &P = resolved_;
        positionChildren(
            x + P.paddingLeft,
            y + P.paddingTop,
            width - P.paddingLeft - P.paddingRight,
            height - P.paddingTop - P.paddingBottom);
        if (auto *ui = FluxUI::getCurrentInstance())
            ui->invalidateWidget(this);
    }

public:
    ~FlexBuilderWidget() override
    {
        stopFling();
        if (stateListenerCleanup_)
            stateListenerCleanup_();
    }

    void setSelf(std::shared_ptr<FlexBuilderWidget> ptr) { self_ = ptr; }
    std::shared_ptr<FlexBuilderWidget> self() { return self_; }

    // ── Core configuration ───────────────────────────────────────────────────

    std::shared_ptr<FlexBuilderWidget> setItemCount(int n)
    {
        itemCount_ = n;
        markNeedsLayout();
        return self();
    }

    std::shared_ptr<FlexBuilderWidget> setItemBuilder(ItemBuilderFn fn)
    {
        itemBuilder_ = std::move(fn);
        markNeedsLayout();
        return self();
    }

    // Custom key function — receives the item index, returns a FlexItemKey.
    // REQUIRED whenever the data can be reordered, inserted, or deleted.
    // Without this, deleting item[2] makes item[3]'s widget appear at item[2]'s
    // slot because the cache is keyed by position, not identity.
    //
    //   // integer ID from a database row
    //   list->setKeyFn([&data](int i){ return FlexItemKey::fromInt64(data[i].id); });
    //
    //   // string UUID
    //   list->setKeyFn([&data](int i){ return FlexItemKey::fromString(data[i].uuid); });
    std::shared_ptr<FlexBuilderWidget> setKeyFn(KeyFn fn)
    {
        keyFn_ = std::move(fn);
        usingIndexKeys_ = false;
        return self();
    }

    // Fixed item extent (pixels along the main axis).
    // Must be set before enabling virtualizeLayout.
    std::shared_ptr<FlexBuilderWidget> setItemExtent(int px)
    {
        itemExtent_ = px;
        markNeedsLayout();
        return self();
    }

    // When true (and itemExtent_ is set) items outside the viewport are
    // neither built nor laid out — O(visible) layout instead of O(total).
    std::shared_ptr<FlexBuilderWidget> setVirtualizeLayout(bool v)
    {
        virtualizeLayout_ = v;
        markNeedsLayout();
        return self();
    }

    // Rebuild all cached items (e.g. after underlying data changes).
    std::shared_ptr<FlexBuilderWidget> invalidateItems()
    {
        cache_.clear();
        generation_++;
        markNeedsLayout();
        return self();
    }

    // Invalidate a single item by index (cheap targeted refresh).
    std::shared_ptr<FlexBuilderWidget> invalidateItem(int index)
    {
        auto key = keyForIndex(index);
        cache_.erase(key);
        markNeedsLayout();
        return self();
    }

    // ── FlexProps setters (mirror FlexWidget) ────────────────────────────────

    std::shared_ptr<FlexBuilderWidget> setDirection(FlexDirection d)
    {
        baseProps_.direction = d;
        markNeedsLayout();
        return self();
    }

    std::shared_ptr<FlexBuilderWidget> setWrap(FlexWrap w)
    {
        baseProps_.wrap = w;
        markNeedsLayout();
        return self();
    }

    std::shared_ptr<FlexBuilderWidget> setJustifyContent(JustifyContent j)
    {
        baseProps_.justify = j;
        markNeedsLayout();
        return self();
    }

    std::shared_ptr<FlexBuilderWidget> setAlignItems(AlignItems a)
    {
        baseProps_.alignItems = a;
        markNeedsLayout();
        return self();
    }

    std::shared_ptr<FlexBuilderWidget> setAlignContent(AlignContent a)
    {
        baseProps_.alignContent = a;
        markNeedsLayout();
        return self();
    }

    std::shared_ptr<FlexBuilderWidget> setGap(int g)
    {
        baseProps_.gap = g;
        markNeedsLayout();
        return self();
    }

    std::shared_ptr<FlexBuilderWidget> setScrollable(bool s)
    {
        baseProps_.scrollable = s;
        markNeedsLayout();
        return self();
    }

    std::shared_ptr<FlexBuilderWidget> setPadding(int p)
    {
        baseProps_.paddingLeft = baseProps_.paddingRight =
            baseProps_.paddingTop = baseProps_.paddingBottom = p;
        markNeedsLayout();
        return self();
    }
    std::shared_ptr<FlexBuilderWidget> setPaddingHV(int h, int v)
    {
        baseProps_.paddingLeft = baseProps_.paddingRight = h;
        baseProps_.paddingTop = baseProps_.paddingBottom = v;
        markNeedsLayout();
        return self();
    }

    std::shared_ptr<FlexBuilderWidget> setBackgroundColor(Color c)
    {
        baseProps_.hasBackground = true;
        baseProps_.backgroundColor = c;
        markNeedsPaint();
        return self();
    }

    std::shared_ptr<FlexBuilderWidget> setBorderColor(Color c)
    {
        baseProps_.hasBorder = true;
        baseProps_.borderColor = c;
        markNeedsPaint();
        return self();
    }

    std::shared_ptr<FlexBuilderWidget> setBorderWidth(int w)
    {
        baseProps_.hasBorder = true;
        baseProps_.borderWidth = w;
        markNeedsPaint();
        return self();
    }

    std::shared_ptr<FlexBuilderWidget> setBorderRadius(int r)
    {
        baseProps_.borderRadius = r;
        markNeedsPaint();
        return self();
    }

    // Self-sizing (when FlexBuilderWidget is itself a flex item in a parent Flex)
    std::shared_ptr<FlexBuilderWidget> setWidthMode(SizeMode m)
    {
        widthMode = m;
        markNeedsLayout();
        return self();
    }

    std::shared_ptr<FlexBuilderWidget> setHeightMode(SizeMode m)
    {
        heightMode = m;
        markNeedsLayout();
        return self();
    }

    std::shared_ptr<FlexBuilderWidget> setWidth(int w)
    {
        width = w;
        widthMode = SizeMode::Fixed;
        markNeedsLayout();
        return self();
    }

    std::shared_ptr<FlexBuilderWidget> setHeight(int h)
    {
        height = h;
        heightMode = SizeMode::Fixed;
        markNeedsLayout();
        return self();
    }

    std::shared_ptr<FlexBuilderWidget> setFlexGrow(int g)
    {
        flexGrow = g;
        markNeedsLayout();
        return self();
    }

    std::shared_ptr<FlexBuilderWidget> setFlexShrink(int s)
    {
        flexShrink = s;
        markNeedsLayout();
        return self();
    }

    std::shared_ptr<FlexBuilderWidget> setFlexBasis(int b)
    {
        flexBasis = b;
        markNeedsLayout();
        return self();
    }

    std::shared_ptr<FlexBuilderWidget> setOrder(int o)
    {
        order = o;
        markNeedsLayout();
        return self();
    }

    // ── Responsive overrides ─────────────────────────────────────────────────

    std::shared_ptr<FlexBuilderWidget> responsive(Breakpoint bp,
                                                  std::function<void(FlexProps &)> fn)
    {
        overrides_.push_back({bp, std::move(fn)});
        markNeedsLayout();
        return self();
    }

    // ── Scroll helpers ────────────────────────────────────────────────────────

    // Programmatically scroll to an item by index.
    void scrollToIndex(int index, bool /*animate*/)
    {
        if (itemLayouts_.empty())
            return;
        index = std::max(0, std::min(index, (int)itemLayouts_.size() - 1));
        sb_.scrollOffset = itemLayouts_[index].mainOffset;
        sb_.clamp();
        sb_.updateThumb();
        applyScrollOffset();
        markNeedsPaint();
    }

    // Programmatically scroll to the top/bottom.
    void scrollToStart()
    {
        sb_.scrollOffset = 0;
        sb_.clamp();
        sb_.updateThumb();
        applyScrollOffset();
        markNeedsPaint();
    }
    void scrollToEnd()
    {
        sb_.scrollOffset = sb_.contentMain;
        sb_.clamp();
        sb_.updateThumb();
        applyScrollOffset();
        markNeedsPaint();
    }

    // ── Layout ───────────────────────────────────────────────────────────────

    void computeLayout(GraphicsContext &ctx,
                       const BoxConstraints &constraints,
                       FontCache &fontCache) override
    {
        if (!itemBuilder_)
        {
            width = height = 0;
            needsLayout = false;
            return;
        }

        ++generation_;
        resolved_ = resolveProps();
        const FlexProps &P = resolved_;

        paddingLeft = P.paddingLeft;
        paddingRight = P.paddingRight;
        paddingTop = P.paddingTop;
        paddingBottom = P.paddingBottom;

        isRowAxis_ = (P.direction == FlexDirection::Row ||
                      P.direction == FlexDirection::RowReverse);
        mainIsReversed_ = (P.direction == FlexDirection::RowReverse ||
                           P.direction == FlexDirection::ColumnReverse);
        wrapReversed_ = (P.wrap == FlexWrap::WrapReverse);

        BoxConstraints self = selfConstraints(constraints);

        // Resolve our own outer box size the same way FlexWidget does:
        //   Fixed → use the stored pixel value (set by setWidth/setHeight)
        //   Full  → fill the incoming constraint (parent told us how big to be)
        //   Fit   → shrink-wrap content; use incoming max as the upper bound
        int outerMaxW, outerMaxH;
        switch (widthMode)
        {
        case SizeMode::Fixed:
            outerMaxW = width;
            break;
        case SizeMode::Full:
            outerMaxW = self.maxWidth;
            break;
        default:
            outerMaxW = self.maxWidth;
            break; // Fit
        }
        switch (heightMode)
        {
        case SizeMode::Fixed:
            outerMaxH = height;
            break;
        case SizeMode::Full:
            outerMaxH = self.maxHeight;
            break;
        default:
            outerMaxH = self.maxHeight;
            break; // Fit
        }

        int padH = P.paddingLeft + P.paddingRight;
        int padV = P.paddingTop + P.paddingBottom;

        // Guard: if outerMax is still kUnbounded (e.g. Fit inside an unbounded
        // parent) clamp to 0 so we don't pass kUnbounded into child constraints
        // on the cross axis.
        int safeOuterW = (outerMaxW >= kUnbounded) ? 0 : outerMaxW;
        int safeOuterH = (outerMaxH >= kUnbounded) ? 0 : outerMaxH;

        int contentMaxW = std::max(0, safeOuterW - padH);
        int contentMaxH = std::max(0, safeOuterH - padV);

        containerMainSize_ = isRowAxis_ ? contentMaxW : contentMaxH;
        containerCrossSize_ = isRowAxis_ ? contentMaxH : contentMaxW;

        // If the cross size is 0 it means we are Fit-on-cross in an unbounded
        // parent — let children report their natural cross size.
        int effectiveCrossForChildren = (containerCrossSize_ > 0)
                                            ? containerCrossSize_
                                            : kUnbounded;

        // ── Virtualised fast-path ───────────────────────────────────────────
        if (virtualizeLayout_ && itemExtent_.has_value() && P.wrap == FlexWrap::NoWrap)
        {
            computeLayoutVirtualized(ctx, constraints, fontCache,
                                     padH, padV, outerMaxW, outerMaxH, self);
            pruneCache();
            needsLayout = false;
            return;
        }

        // ── General path ────────────────────────────────────────────────────
        itemLayouts_.clear();
        itemLayouts_.reserve(itemCount_);

        int cursor = 0;
        int maxCross = 0;
        int count = mainIsReversed_ ? itemCount_ - 1 : 0;
        int step = mainIsReversed_ ? -1 : 1;
        int end = mainIsReversed_ ? -1 : itemCount_;

        // Cross-axis constraints for each item
        SizeMode crossAxisSelfMode = isRowAxis_ ? heightMode : widthMode;
        bool crossIsFit = (crossAxisSelfMode == SizeMode::Fit);



        for (int i = count; i != end; i += step)
        {
            // Main axis is unbounded (scrollable content can exceed viewport).
            // Cross axis uses effectiveCrossForChildren so items always get a
            // non-zero constraint even when our own cross mode is Full/Fit.
            BoxConstraints bc = isRowAxis_
                                    ? BoxConstraints::loose(kUnbounded, effectiveCrossForChildren)
                                    : BoxConstraints::loose(effectiveCrossForChildren, kUnbounded);

            Widget *w = getOrBuildItem(i, ctx, bc, fontCache);
            if (!w)
                continue;

            int mSize = mainDim(w, isRowAxis_);
            int cSize = crossDim(w, isRowAxis_);

            // Stretch pass: if alignItems == Stretch and cross is known, re-layout
            if (P.alignItems == AlignItems::Stretch && !crossIsFit && containerCrossSize_ > 0)
            {
                SizeMode cm = isRowAxis_ ? w->heightMode : w->widthMode;
                if (cm != SizeMode::Fixed && cm != SizeMode::Fit)
                {
                    BoxConstraints stretched = isRowAxis_
                                                   ? BoxConstraints(mSize, mSize, containerCrossSize_, containerCrossSize_)
                                                   : BoxConstraints(containerCrossSize_, containerCrossSize_, mSize, mSize);
                    w->computeLayout(ctx, stretched, fontCache);
                    cSize = crossDim(w, isRowAxis_);
                }
            }

            ItemLayout il;
            il.key = keyForIndex(i);
            il.widget = w;
            il.mainOffset = cursor;
            il.mainSize = mSize;
            il.crossSize = cSize;
            itemLayouts_.push_back(il);

            cursor += mSize + P.gap;
            maxCross = std::max(maxCross, cSize);
        }

        // Remove trailing gap
        totalMain_ = cursor > 0 ? cursor - P.gap : 0;
        totalCross_ = maxCross;

        // ── Compute our own size ────────────────────────────────────────────
        int finalW, finalH;
        if (isRowAxis_)
        {
            finalW = (widthMode == SizeMode::Fit) ? std::min(outerMaxW, totalMain_ + padH) : outerMaxW;
            finalH = (heightMode == SizeMode::Fit) ? (totalCross_ + padV) : outerMaxH;
            finalW = std::max(finalW, padH);
        }
        else
        {
            finalH = (heightMode == SizeMode::Fit) ? std::min(outerMaxH, totalMain_ + padV) : outerMaxH;
            finalW = (widthMode == SizeMode::Fit) ? (totalCross_ + padH) : outerMaxW;
            finalH = std::max(finalH, padV);
        }
        width = self.clampWidth(finalW);
        height = self.clampHeight(finalH);

        // ── Scrollbar ────────────────────────────────────────────────────────
        sb_.horizontal = isRowAxis_;
        sb_.contentMain = totalMain_;
        sb_.viewportMain = containerMainSize_;
        sb_.setScrollable(P.scrollable && sb_.contentMain > sb_.viewportMain);
        sb_.clamp();
        sb_.updateThumb();

        scrollAxisIsMain_ = true; // FlexBuilder always scrolls on main axis

        applyConstraints();
        pruneCache();
        needsLayout = false;
    }

private:
    // ── Virtualised layout (fixed-extent only) ────────────────────────────────
    void computeLayoutVirtualized(GraphicsContext &ctx,
                                  const BoxConstraints & /*constraints*/,
                                  FontCache &fontCache,
                                  int padH, int padV,
                                  int outerMaxW, int outerMaxH,
                                  const BoxConstraints &self)
    {
        const FlexProps &P = resolved_;
        int extent = itemExtent_.value();

        // Total main size = N * extent + (N-1) * gap
        totalMain_ = itemCount_ > 0
                         ? itemCount_ * extent + std::max(0, itemCount_ - 1) * P.gap
                         : 0;
        totalCross_ = containerCrossSize_ > 0 ? containerCrossSize_ : 0;
        int effectiveCross = (containerCrossSize_ > 0) ? containerCrossSize_ : kUnbounded;

        // Scrollbar first so we know the scroll offset
        sb_.horizontal = isRowAxis_;
        sb_.contentMain = totalMain_;
        sb_.viewportMain = containerMainSize_;
        sb_.setScrollable(P.scrollable && sb_.contentMain > sb_.viewportMain);
        sb_.clamp();
        sb_.updateThumb();

        // Determine visible range
        int scroll = sb_.scrollOffset;
        int firstVisible = std::max(0, (scroll) / (extent + P.gap));
        int lastVisible = std::min(itemCount_ - 1,
                                   (scroll + containerMainSize_) / (extent + P.gap) + 1);

        itemLayouts_.clear();
        itemLayouts_.reserve(itemCount_);

        // Populate ALL layout slots so scrollToIndex() always works correctly,
        // but only actually build/layout widgets in the visible band.
        for (int i = 0; i < itemCount_; i++)
        {
            int mainOffset = i * (extent + P.gap);
            ItemLayout il;
            il.key = keyForIndex(i);
            il.mainOffset = mainOffset;
            il.mainSize = extent;
            il.crossSize = containerCrossSize_;
            il.widget = nullptr;

            if (i >= firstVisible && i <= lastVisible)
            {
                BoxConstraints bc = isRowAxis_
                                        ? BoxConstraints(extent, extent, effectiveCross, effectiveCross)
                                        : BoxConstraints(effectiveCross, effectiveCross, extent, extent);
                il.widget = getOrBuildItem(i, ctx, bc, fontCache);
            }
            itemLayouts_.push_back(il);
        }

        int finalW, finalH;
        if (isRowAxis_)
        {
            finalW = (widthMode == SizeMode::Fit) ? std::min(outerMaxW, totalMain_ + padH) : outerMaxW;
            finalH = (heightMode == SizeMode::Fit) ? totalCross_ + padV : outerMaxH;
        }
        else
        {
            finalH = (heightMode == SizeMode::Fit) ? std::min(outerMaxH, totalMain_ + padV) : outerMaxH;
            finalW = (widthMode == SizeMode::Fit) ? totalCross_ + padH : outerMaxW;
        }
        width = self.clampWidth(finalW);
        height = self.clampHeight(finalH);
    }

public:
    // ── Position ─────────────────────────────────────────────────────────────

    void positionChildren(int contentX, int contentY,
                          int contentW, int contentH) override
    {
        const FlexProps &P = resolved_;
        int scroll = sb_.scrollOffset;

        // Use the actual content dimensions passed in (already padding-deflated).
        // This is correct even when widthMode/heightMode is Full because the
        // caller (FlexWidget or LayoutEngine) supplies the real pixel sizes.
        int actualCrossSize = isRowAxis_ ? contentH : contentW;

        // Justify offset (only meaningful when content < viewport, i.e. no scroll)
        int freeMain = std::max(0, containerMainSize_ - totalMain_);
        int mainOriginExtra = 0;
        switch (P.justify)
        {
        case JustifyContent::End:
            mainOriginExtra = freeMain;
            break;
        case JustifyContent::Center:
            mainOriginExtra = freeMain / 2;
            break;
        default:
            mainOriginExtra = 0;
            break;
        }

        for (auto &il : itemLayouts_)
        {
            if (!il.widget)
                continue;

            int mainPx = mainOriginExtra + il.mainOffset - scroll;

            int crossFree = std::max(0, actualCrossSize - il.crossSize);
            int crossOff = 0;
            switch (P.alignItems)
            {
            case AlignItems::End:
                crossOff = crossFree;
                break;
            case AlignItems::Center:
                crossOff = crossFree / 2;
                break;
            default:
                crossOff = 0;
                break;
            }

            if (isRowAxis_)
            {
                il.widget->x = contentX + mainPx;
                il.widget->y = contentY + crossOff;
            }
            else
            {
                il.widget->x = contentX + crossOff;
                il.widget->y = contentY + mainPx;
            }

            il.widget->positionChildren(
                il.widget->x + il.widget->paddingLeft,
                il.widget->y + il.widget->paddingTop,
                il.widget->width - il.widget->paddingLeft - il.widget->paddingRight,
                il.widget->height - il.widget->paddingTop - il.widget->paddingBottom);
        }
    }

    // ── Mouse / scroll ────────────────────────────────────────────────────────

    bool handleMouseWheel(int delta) override
    {
        if (!sb_.onWheel(delta))
            return false;
        applyScrollOffset();
        markNeedsPaint();
        return true;
    }

    bool handleMouseDown(int mx, int my) override
    {
        stopFling();
        int cbx = x + resolved_.paddingLeft, cby = y + resolved_.paddingTop;
        int cbw = width - resolved_.paddingLeft - resolved_.paddingRight;
        int cbh = height - resolved_.paddingTop - resolved_.paddingBottom;

        if (sb_.onMouseDown(mx, my, cbx, cby, cbw, cbh))
        {
            if (sb_.isDragging)
                if (auto *ui = FluxUI::getCurrentInstance())
                    ui->captureMouseInput();
            applyScrollOffset();
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

    bool handleMouseMove(int mx, int my) override
    {
        int cbx = x + resolved_.paddingLeft, cby = y + resolved_.paddingTop;
        int cbw = width - resolved_.paddingLeft - resolved_.paddingRight;
        int cbh = height - resolved_.paddingTop - resolved_.paddingBottom;

        if (sb_.isDragging)
        {
            if (!sb_.onMouseMove(mx, my, cbx, cby, cbw, cbh))
                return false;
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
        if (!sb_.onMouseLeave())
            return false;
        markNeedsPaint();
        return true;
    }

    // ── Render ────────────────────────────────────────────────────────────────

    void render(GraphicsContext &ctx, FontCache &fontCache) override
    {
        const FlexProps &P = resolved_;
        Painter painter(ctx);

        if (P.hasBackground)
            painter.fillRoundedRect(x, y, width, height,
                                    P.borderRadius, P.backgroundColor);

        int sbSz = sb_.isScrollable ? sb_.size : 0;
        int clipX1 = x, clipY1 = y;
        int clipX2 = x + width, clipY2 = y + height;
        if (sb_.isScrollable)
        {
            if (sb_.horizontal)
                clipY2 -= sbSz;
            else
                clipX2 -= sbSz;
        }
        painter.pushClipRect(clipX1, clipY1, clipX2 - clipX1, clipY2 - clipY1);

        for (auto &il : itemLayouts_)
        {
            if (!il.widget || !il.widget->visible)
                continue;
            bool onScreen =
                il.widget->x + il.widget->width >= clipX1 && il.widget->x < clipX2 &&
                il.widget->y + il.widget->height >= clipY1 && il.widget->y < clipY2;
            if (onScreen)
                il.widget->render(ctx, fontCache);
        }

        painter.popClipRect();

        if (P.hasBorder)
            painter.drawBorder(x, y, width, height,
                               P.borderRadius, P.borderColor, P.borderWidth);

        int cbx = x + P.paddingLeft, cby = y + P.paddingTop;
        int cbw = width - P.paddingLeft - P.paddingRight;
        int cbh = height - P.paddingTop - P.paddingBottom;
        sb_.render(ctx, cbx, cby, cbw, cbh);

        needsPaint = false;
    }

    // ── Cache inspection (debugging / testing) ────────────────────────────────

    int cachedItemCount() const { return (int)cache_.size(); }
    int itemCount() const { return itemCount_; }
    bool hasKey(int index) const { return cache_.count(keyForIndex(index)) > 0; }
};

using FlexBuilderWidgetPtr = std::shared_ptr<FlexBuilderWidget>;

// ============================================================================
// FACTORY FUNCTIONS
// ============================================================================

// ── FlexBuilder From List ───────────────────────────────────────────────────────
//
// Convenience wrapper around a plain std::vector<T>.
// The vector is captured BY VALUE — mutations to the original after construction
// are NOT reflected.  Use FlexBuilderFromState() for live data.
//
// A keySelector is REQUIRED so that insert/delete/reorder work correctly.
// The selector receives (index, item) and must return a stable FlexItemKey.
//
//   struct Todo { int64_t id; std::string text; };
//   std::vector<Todo> todos = ...;
//
//   auto list = FlexBuilder(todos,
//       [](int, const Todo &t){ return FlexItemKey::fromInt64(t.id); },
//       [](int i, const Todo &t){ return Text(t.text); }
//   );

template <typename T, typename KeySel, typename Builder>
inline FlexBuilderWidgetPtr FlexBuilder(
    const std::vector<T> &items,
    KeySel keySelector,  // (int, const T&) -> FlexItemKey
    Builder itemBuilder) // (int, const T&) -> WidgetPtr
{
    auto snapshot = std::make_shared<std::vector<T>>(items);
    int count = (int)snapshot->size();

    auto w = std::make_shared<FlexBuilderWidget>();
    w->setSelf(w);
    w->setItemCount(count);
    w->setItemBuilder([snapshot, itemBuilder](int i) -> WidgetPtr
                      { return itemBuilder(i, (*snapshot)[i]); });
    w->setKeyFn([snapshot, keySelector](int i) -> FlexItemKey
                { return keySelector(i, (*snapshot)[i]); });
    return w;
}

// Backwards-compat overload without key selector (index keys, static lists only)
template <typename T, typename Builder>
inline FlexBuilderWidgetPtr FlexBuilder(
    const std::vector<T> &items,
    Builder itemBuilder) // (int, const T&) -> WidgetPtr
{
    auto snapshot = std::make_shared<std::vector<T>>(items);
    int count = (int)snapshot->size();
    auto w = std::make_shared<FlexBuilderWidget>();
    w->setSelf(w);
    w->setItemCount(count);
    w->setItemBuilder([snapshot, itemBuilder](int i) -> WidgetPtr
                      { return itemBuilder(i, (*snapshot)[i]); });
    // No keyFn — index keys.  Debug warning fires on first use.
    return w;
}

// ── FlexBuilder From State ──────────────────────────────────────────────────────
//
// The CORRECT way to render a mutable list.
//
// • Binds to a State<std::vector<T>> — any mutation (push_back, erase, mutate,
//   set, etc.) automatically triggers invalidateItems() + markNeedsLayout().
// • keySelector extracts a STABLE identity from each item (e.g. a database id).
//   Without a stable key, deleting item[2] would show item[3]'s widget at
//   item[2]'s position because the cache is keyed by identity, not position.
//
// Usage:
//
//   struct Todo { int64_t id; std::string text; bool done; };
//   auto todos = app->useState(std::vector<Todo>{});
//
//   auto list = FlexBuilder(todos,
//       // key: stable int64 id
//       [](int, const Todo &t){ return FlexItemKey::fromInt64(t.id); },
//       // builder: one widget per item
//       [](int i, const Todo &t){
//           return Text(t.text);
//       }
//   )->setDirection(FlexDirection::Column)
//    ->setScrollable(true);
//
//   // Later — all three mutations correctly add/remove/reorder widgets:
//   todos.push_back({nextId++, "Buy milk", false});
//   todos.erase(2);
//   todos.sort([](auto &a, auto &b){ return a.text < b.text; });

template <typename T, typename KeySel, typename Builder>
inline FlexBuilderWidgetPtr FlexBuilder(
    State<std::vector<T>> &state,
    KeySel keySelector,  // (int, const T&) -> FlexItemKey
    Builder itemBuilder) // (int, const T&) -> WidgetPtr
{
    auto w = std::make_shared<FlexBuilderWidget>();
    w->setSelf(w);

    // ── Initial sync ────────────────────────────────────────────────────────
    {
        auto current = state.get();
        w->setItemCount((int)current.size());
    }

    // itemBuilder closure reads live from state so it always sees the
    // current snapshot at the moment computeLayout calls it.
    w->setItemBuilder([&state, itemBuilder](int i) -> WidgetPtr
                      {
        auto current = state.get();
        if (i < 0 || i >= (int)current.size()) return nullptr;
        return itemBuilder(i, current[i]); });

    w->setKeyFn([&state, keySelector](int i) -> FlexItemKey
                {
        auto current = state.get();
        if (i < 0 || i >= (int)current.size())
            return FlexItemKey::fromIndex(i);
        return keySelector(i, current[i]); });

    // ── Live binding ─────────────────────────────────────────────────────────
    std::weak_ptr<FlexBuilderWidget> weakW = w;
    state.listen([weakW](const std::vector<T> &newData)
                 {
        if (auto sp = weakW.lock())
        {
            sp->setItemCount((int)newData.size());
            sp->invalidateItems();
            sp->markNeedsLayout();
            if (auto *ui = FluxUI::getCurrentInstance())
                ui->partialRebuild(sp.get());
        } });

    return w;
}

// Overload without key selector — index keys, emits debug warning on mutation.
template <typename T, typename Builder>
inline FlexBuilderWidgetPtr FlexBuilder(
    State<std::vector<T>> &state,
    Builder itemBuilder) // (int, const T&) -> WidgetPtr
{
    auto w = std::make_shared<FlexBuilderWidget>();
    w->setSelf(w);

    {
        auto current = state.get();
        w->setItemCount((int)current.size());
    }

    w->setItemBuilder([&state, itemBuilder](int i) -> WidgetPtr
                      {
        auto current = state.get();
        if (i < 0 || i >= (int)current.size()) return nullptr;
        return itemBuilder(i, current[i]); });
    // No keyFn — index keys only.  Safe for append-only lists.

    std::weak_ptr<FlexBuilderWidget> weakW = w;
    state.listen([weakW](const std::vector<T> &newData)
                 {
        if (auto sp = weakW.lock())
        {
            sp->setItemCount((int)newData.size());
            sp->invalidateItems();
            sp->markNeedsLayout();
            if (auto *ui = FluxUI::getCurrentInstance())
                ui->partialRebuild(sp.get());
        } });

    return w;
}

#endif // FLUX_FLEX_BUILDER_HPP