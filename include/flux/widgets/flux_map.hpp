#ifndef FLUX_MAP_HPP
#define FLUX_MAP_HPP

// ============================================================================
// Map — the React `list.map()` equivalent, and the direct successor to
// FlexBuilderWidget.
//
// WHAT CHANGED FROM FlexBuilderWidget
// ────────────────────────────────────
// FlexBuilderWidget used to do two jobs at once: (1) decide which widgets
// exist — lazy building, keyed caching, reactivity to State<vector<T>>,
// optional virtualization — and (2) lay those widgets out with its own
// copy of the flex algorithm, plus its own scrollbar/gesture handling.
//
// MapWidget does ONLY job (1). Job (2) now belongs entirely to BoxWidget
// (flux_box.hpp), which already owns flex/grid/block layout, scrolling,
// and gestures for every other kind of content. A MapWidget is never laid
// out or rendered directly — it overrides isItemSource() to opt out of
// normal child handling, and Box calls expandItems() on it during Box's
// own layout pass to get the real widgets to splice into its flow list.
//
// This means:
//   Box({
//       Text("Header"),
//       Map(todos, [](int i, const Todo &t){ return Text(t.text); }),
//       Text("Footer"),
//   })
// works because Box treats "a Text, then whatever Map currently expands
// to, then a Text" as one flat child list — Map itself contributes zero
// pixels and never appears in the render tree.
//
// STATIC USAGE (no reactivity, built once)
// ──────────────────────────────────────────
//   Map(itemCount, [](int i){ return Text("Item " + std::to_string(i)); })
//   Map(myVector, [](int i, const T &item){ return ...; })
//
// REACTIVE USAGE (rebuilds when State<vector<T>> changes)
// ──────────────────────────────────────────────────────────
//   auto todos = app->useState(std::vector<Todo>{});
//   Map(todos,
//       [](int, const Todo &t){ return FlexItemKey::fromInt64(t.id); },
//       [](int i, const Todo &t){ return Text(t.text); })
//
// A stable key function is REQUIRED for reactive/mutable lists — without
// it, deleting item[2] makes item[3]'s cached widget (and its state: typed
// text, scroll position, etc.) appear at item[2]'s slot, because the cache
// is keyed by position instead of identity. Static, append-only, or
// never-mutated lists are fine with the default index keys.
//
// VIRTUALIZATION (large lists inside a scrollable, single-axis Box)
// ─────────────────────────────────────────────────────────────────
//   Map(todos, keyFn, builder)
//       ->setItemExtent(48)      // every item is exactly 48px along the main axis
//       ->setVirtualized(true);  // only build/layout items near the viewport
//
// Only meaningful when the enclosing Box is in Flex or Block display mode
// with FlexWrap::NoWrap and scrollable(true) — that's the only case with a
// single well-defined main axis + scroll offset to virtualize against. In
// any other context (Grid mode, or a non-scrollable container) Box passes
// mainAxisBudget = -1 and MapWidget falls back to building everything.
// ============================================================================

#include "flux/flux_core.hpp"
#include "flux/flux_state.hpp"

#include <functional>
#include <unordered_map>
#include <vector>
#include <string>
#include <cstdint>
#include <algorithm>

// ============================================================================
// KEY TYPE — unchanged from FlexBuilderWidget, just lives here now.
// ============================================================================

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
// MAP WIDGET
// ============================================================================

class MapWidget : public Widget
{
private:
    int itemCount_ = 0;
    ItemBuilderFn itemBuilder_;
    KeyFn keyFn_;                // nullptr → index keys (safe for static/append-only lists)
    bool usingIndexKeys_ = true;

    std::optional<int> itemExtent_; // required for virtualization
    bool virtualized_ = false;

    struct CacheEntry
    {
        WidgetPtr widget;
        int lastUsedGeneration = 0;
    };
    using Cache = std::unordered_map<FlexItemKey, CacheEntry, FlexItemKeyHash>;
    Cache cache_;
    int generation_ = 0;

    // Kept alive so the destructor can unregister from a bound State<T>.
    std::function<void()> stateListenerCleanup_;
    std::weak_ptr<MapWidget> self_;

    FlexItemKey keyForIndex(int i) const
    {
        if (keyFn_)
            return keyFn_(i);
#ifdef FLUX_DEBUG
        static bool warned = false;
        if (!warned)
        {
            warned = true;
            std::cerr << "[Map] WARNING: using index keys on a list that may be "
                         "mutated. Call the setKeyFn()-taking Map(...) overload "
                         "with stable item identifiers.\n";
        }
#endif
        return FlexItemKey::fromIndex(i);
    }

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
        if (!itemBuilder_)
            return nullptr;
        WidgetPtr built = itemBuilder_(index);
        if (!built)
            return nullptr;
        built->computeLayout(ctx, childConstraints, fontCache);
        // Logical parent is whichever container expanded us (set by that
        // container right after expandItems() returns), not this MapWidget —
        // MapWidget itself never appears in the render tree.
        built->parent = parent;
        cache_[key] = CacheEntry{built, generation_};
        return built.get();
    }

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

public:
    ~MapWidget() override
    {
        if (stateListenerCleanup_)
            stateListenerCleanup_();
    }

    void setSelf(std::shared_ptr<MapWidget> ptr) { self_ = ptr; }
    std::shared_ptr<MapWidget> self() { return self_.lock(); }

    bool isItemSource() const override { return true; }

    // ── Configuration ────────────────────────────────────────────────────

    std::shared_ptr<MapWidget> setItemCount(int n)
    {
        itemCount_ = n;
        markNeedsLayout();
        return self();
    }

    std::shared_ptr<MapWidget> setItemBuilder(ItemBuilderFn fn)
    {
        itemBuilder_ = std::move(fn);
        markNeedsLayout();
        return self();
    }

    // REQUIRED whenever the underlying data can be reordered, inserted, or
    // deleted. Without it, deleting item[2] shows item[3]'s cached widget
    // (with item[3]'s scroll/typed state still attached) at item[2]'s slot.
    std::shared_ptr<MapWidget> setKeyFn(KeyFn fn)
    {
        keyFn_ = std::move(fn);
        usingIndexKeys_ = false;
        return self();
    }

    // Fixed main-axis extent per item, in pixels. Required before enabling
    // setVirtualized(true).
    std::shared_ptr<MapWidget> setItemExtent(int px)
    {
        itemExtent_ = px;
        markNeedsLayout();
        return self();
    }

    // When true (and itemExtent_ is set), expandItems() only builds/lays out
    // items near the visible band instead of the entire list — O(visible)
    // instead of O(itemCount_). Only takes effect when the calling container
    // passes a real mainAxisBudget (>= 0) into expandItems(); otherwise this
    // silently has no effect and every item is built, same as false.
    std::shared_ptr<MapWidget> setVirtualized(bool v)
    {
        virtualized_ = v;
        markNeedsLayout();
        return self();
    }

    // Rebuild every cached item (e.g. after data changed some other way
    // than through a bound State<T>'s own listener).
    std::shared_ptr<MapWidget> invalidateItems()
    {
        cache_.clear();
        ++generation_;
        markNeedsLayout();
        return self();
    }

    std::shared_ptr<MapWidget> invalidateItem(int index)
    {
        cache_.erase(keyForIndex(index));
        markNeedsLayout();
        return self();
    }

    // Called by the Map(State<...>, ...) factory to wire cleanup.
    void setStateListenerCleanup(std::function<void()> fn)
    {
        stateListenerCleanup_ = std::move(fn);
    }

    // ── Widget overrides ─────────────────────────────────────────────────
    // MapWidget contributes zero pixels on its own — its parent container
    // never calls these because it filters MapWidget (and any isItemSource()
    // widget) out of its normal child list before doing flow layout. They're
    // implemented defensively anyway in case a MapWidget ever ends up as a
    // literal, undispatched child somewhere (e.g. added directly instead of
    // through a container's item-source path).

    void computeLayout(GraphicsContext &, const BoxConstraints &, FontCache &) override
    {
        width = height = 0;
        needsLayout = false;
    }
    void positionChildren(int, int, int, int) override {}
    void render(GraphicsContext &, FontCache &) override {}

    // ── Item-source expansion ────────────────────────────────────────────

    std::vector<Widget *> expandItems(GraphicsContext &ctx, FontCache &fontCache,
                                      const BoxConstraints &itemConstraints,
                                      int scrollOffset, int mainAxisBudget) override
    {
        ++generation_;
        std::vector<Widget *> out;

        if (itemCount_ <= 0 || !itemBuilder_)
        {
            pruneCache();
            return out;
        }
        out.reserve(itemCount_);

        bool canVirtualize = virtualized_ && itemExtent_.has_value() && mainAxisBudget >= 0;

        if (canVirtualize)
        {
            int extent = std::max(1, *itemExtent_);
            int first = std::max(0, scrollOffset / extent);
            int last = std::min(itemCount_ - 1, (scrollOffset + mainAxisBudget) / extent + 1);
            for (int i = first; i <= last; ++i)
                if (auto *w = getOrBuildItem(i, ctx, itemConstraints, fontCache))
                    out.push_back(w);
        }
        else
        {
            for (int i = 0; i < itemCount_; ++i)
                if (auto *w = getOrBuildItem(i, ctx, itemConstraints, fontCache))
                    out.push_back(w);
        }

        pruneCache();
        return out;
    }

    // ── Debug / inspection ───────────────────────────────────────────────

    int cachedItemCount() const { return (int)cache_.size(); }
    int itemCount() const { return itemCount_; }
    bool hasKey(int index) const { return cache_.count(keyForIndex(index)) > 0; }
};

using MapWidgetPtr = std::shared_ptr<MapWidget>;

// ============================================================================
// FACTORY FUNCTIONS
// ============================================================================

// ── Index-count form ─────────────────────────────────────────────────────
//   Map(10, [](int i){ return Text("Item " + std::to_string(i)); })
inline MapWidgetPtr Map(int itemCount, std::function<WidgetPtr(int)> builder)
{
    auto w = std::make_shared<MapWidget>();
    w->setSelf(w);
    w->setItemCount(itemCount);
    w->setItemBuilder(std::move(builder));
    return w;
}

// ── Static vector form (snapshot at call time — later mutation of `items`
//    is NOT reflected; use the State<vector<T>> overload below for that) ──
//   Map(myVector, [](int i, const T &item){ return ...; })
template <typename T>
inline MapWidgetPtr Map(const std::vector<T> &items,
                        std::function<WidgetPtr(int, const T &)> builder)
{
    auto snapshot = std::make_shared<std::vector<T>>(items);
    int count = (int)snapshot->size();

    auto w = std::make_shared<MapWidget>();
    w->setSelf(w);
    w->setItemCount(count);
    w->setItemBuilder([snapshot, builder](int i) -> WidgetPtr
                      { return builder(i, (*snapshot)[i]); });
    // No keyFn — index keys. Fine for static/append-only snapshots; the
    // debug build will warn if it detects reuse patterns suggesting mutation.
    return w;
}

// ── Reactive form, WITHOUT explicit keys ─────────────────────────────────
// Convenience for append-only / never-reordered reactive lists. Prefer the
// keyed overload below for anything that can insert/delete/reorder.
template <typename T>
inline MapWidgetPtr Map(State<std::vector<T>> &state,
                        std::function<WidgetPtr(int, const T &)> builder)
{
    auto w = std::make_shared<MapWidget>();
    w->setSelf(w);
    w->setItemCount((int)state.get().size());

    w->setItemBuilder([&state, builder](int i) -> WidgetPtr
                      {
        auto current = state.get();
        if (i < 0 || i >= (int)current.size()) return nullptr;
        return builder(i, current[i]); });

    std::weak_ptr<MapWidget> weakW = w;
    state.listen([weakW](const std::vector<T> &newData)
                 {
        if (auto sp = weakW.lock())
        {
            sp->setItemCount((int)newData.size());
            sp->invalidateItems();
            if (auto *ui = FluxUI::getCurrentInstance())
            {
                // Rebuild starts from the nearest ancestor that actually
                // does layout — MapWidget itself has no layout of its own,
                // so walk up to whatever container is currently its parent.
                Widget *target = sp->parent ? sp->parent : sp.get();
                ui->partialRebuild(target);
            }
        } });

    return w;
}

// ── Reactive form, WITH explicit stable keys — the recommended form for
//    any list that can be mutated (insert/delete/reorder). ────────────────
//   Map(todos,
//       [](int, const Todo &t){ return FlexItemKey::fromInt64(t.id); },
//       [](int i, const Todo &t){ return Text(t.text); })
template <typename T>
inline MapWidgetPtr Map(State<std::vector<T>> &state,
                        std::function<FlexItemKey(int, const T &)> keySelector,
                        std::function<WidgetPtr(int, const T &)> builder)
{
    auto w = std::make_shared<MapWidget>();
    w->setSelf(w);
    w->setItemCount((int)state.get().size());

    w->setItemBuilder([&state, builder](int i) -> WidgetPtr
                      {
        auto current = state.get();
        if (i < 0 || i >= (int)current.size()) return nullptr;
        return builder(i, current[i]); });

    w->setKeyFn([&state, keySelector](int i) -> FlexItemKey
                {
        auto current = state.get();
        if (i < 0 || i >= (int)current.size())
            return FlexItemKey::fromIndex(i);
        return keySelector(i, current[i]); });

    std::weak_ptr<MapWidget> weakW = w;
    state.listen([weakW](const std::vector<T> &newData)
                 {
        if (auto sp = weakW.lock())
        {
            sp->setItemCount((int)newData.size());
            sp->invalidateItems();
            if (auto *ui = FluxUI::getCurrentInstance())
            {
                Widget *target = sp->parent ? sp->parent : sp.get();
                ui->partialRebuild(target);
            }
        } });

    return w;
}

#endif // FLUX_MAP_HPP