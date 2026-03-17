#ifndef FLUX_COLLECTION_HPP
#define FLUX_COLLECTION_HPP

#include "flux_core.hpp"
#include "flux_state.hpp"
#include "flux_reactive_item.hpp"

#include <functional>
#include <unordered_map>
#include <vector>

// ============================================================================
// INTERNAL — scrollbar helpers shared by all collection widgets
// ============================================================================
//
// Pulled into a standalone struct so ListViewBuilder, ListViewStatic, and
// GridViewBuilder all use identical code paths without copy-paste.
//
// The owning widget supplies a small "callbacks" interface so the helper can
// trigger reposition / repaint without knowing the concrete widget type.

struct ScrollbarState {
    // ── Configuration (set once by the owner) ────────────────────────────
    int  size              = 8;
    bool horizontal        = false;

    COLORREF colorNormal   = RGB(180, 180, 180);
    COLORREF colorHover    = RGB(140, 140, 140);
    COLORREF colorActive   = RGB(100, 100, 100);
    COLORREF colorTrack    = RGB(245, 245, 245);

    // ── Computed each layout pass ─────────────────────────────────────────
    int  contentMain       = 0;   // total length along scroll axis
    int  viewportMain      = 0;   // visible length along scroll axis
    bool isScrollable      = false;

    int  scrollOffset      = 0;
    int  thumbLength       = 0;
    int  thumbOffset       = 0;

    // ── Interaction state ─────────────────────────────────────────────────
    bool isDragging        = false;
    bool isHovering        = false;
    int  dragStartPos      = 0;
    int  dragStartOffset   = 0;

    // ── Clamp / update ────────────────────────────────────────────────────

    void clamp() {
        int maxScroll = max(0, contentMain - viewportMain);
        scrollOffset  = max(0, min(scrollOffset, maxScroll));
    }

    // Call after contentMain / viewportMain / scrollOffset change
    void updateThumb() {
        if (!isScrollable) { thumbLength = thumbOffset = 0; return; }
        float visRatio = (float)viewportMain / (float)contentMain;
        thumbLength    = max(30, (int)(viewportMain * visRatio));
        float scrollRatio = (contentMain > viewportMain)
            ? (float)scrollOffset / (float)(contentMain - viewportMain) : 0.f;
        thumbOffset    = (int)(scrollRatio * (viewportMain - thumbLength));
    }

    void setScrollable(bool s) {
        if (isScrollable && !s) {
            scrollOffset = 0;
            isDragging   = false;
            isHovering   = false;
        }
        isScrollable = s;
    }

    // ── Hit-testing ───────────────────────────────────────────────────────

    // Is (mx,my) over the scrollbar thumb?
    bool isOverThumb(int mx, int my, int wx, int wy, int ww, int wh) const {
        if (!isScrollable) return false;
        if (horizontal) {
            int sbY = wy + wh - size;
            return mx >= wx + thumbOffset &&
                   mx <  wx + thumbOffset + thumbLength &&
                   my >= sbY && my < wy + wh;
        } else {
            int sbX = wx + ww - size;
            return mx >= sbX && mx < wx + ww &&
                   my >= wy + thumbOffset &&
                   my <  wy + thumbOffset + thumbLength;
        }
    }

    // Is (mx,my) anywhere in the scrollbar strip?
    bool isInStrip(int mx, int my, int wx, int wy, int ww, int wh) const {
        if (!isScrollable) return false;
        if (horizontal) return my >= wy + wh - size && my < wy + wh;
        else            return mx >= wx + ww - size && mx < wx + ww;
    }

    // ── Rendering ─────────────────────────────────────────────────────────

    void render(HDC hdc, int wx, int wy, int ww, int wh) const {
        if (!isScrollable) return;
        COLORREF thumbColor = isDragging ? colorActive
                            : isHovering ? colorHover
                                         : colorNormal;
        RECT trackRect, thumbRect;
        if (horizontal) {
            int sbY  = wy + wh - size;
            trackRect = { wx,              sbY, wx + ww,                    wy + wh };
            thumbRect = { wx + thumbOffset, sbY, wx + thumbOffset + thumbLength, wy + wh };
        } else {
            int sbX  = wx + ww - size;
            trackRect = { sbX, wy,              wx + ww, wy + wh               };
            thumbRect = { sbX, wy + thumbOffset, wx + ww, wy + thumbOffset + thumbLength };
        }
        HBRUSH br = CreateSolidBrush(colorTrack);  FillRect(hdc, &trackRect, br); DeleteObject(br);
              br = CreateSolidBrush(thumbColor);   FillRect(hdc, &thumbRect, br); DeleteObject(br);
    }

    // ── Mouse handlers — return true if the event was consumed ────────────

    // Returns the new scrollOffset if dragging, or -1 if not dragging.
    // The caller applies the returned offset.

    bool onWheel(int delta) {
        if (!isScrollable) return false;
        scrollOffset -= (delta / WHEEL_DELTA) * 40;
        clamp(); updateThumb();
        return true;
    }

    // Returns true if the click is inside the scrollbar strip.
    bool onMouseDown(int mx, int my, int wx, int wy, int ww, int wh, HWND hwnd) {
        if (!isInStrip(mx, my, wx, wy, ww, wh)) return false;

        int pos = horizontal ? mx - wx : my - wy;
        if (pos >= thumbOffset && pos < thumbOffset + thumbLength) {
            isDragging    = true;
            dragStartPos  = horizontal ? mx : my;
            dragStartOffset = scrollOffset;
            if (hwnd) SetCapture(hwnd);
        } else {
            float ratio  = (float)pos / (float)viewportMain;
            scrollOffset = (int)(ratio * (contentMain - viewportMain));
            clamp(); updateThumb();
        }
        return true;
    }

    bool onMouseUp() {
        if (!isDragging) return false;
        isDragging = false; isHovering = false;
        ReleaseCapture();
        return true;
    }

    // Returns true if a visual update is needed.
    bool onMouseMove(int mx, int my, int wx, int wy, int ww, int wh) {
        if (isDragging) {
            if (!isScrollable) { onMouseUp(); return true; }
            int curPos   = horizontal ? mx : my;
            int delta    = curPos - dragStartPos;
            float ratio  = (viewportMain > thumbLength)
                ? (float)delta / (float)(viewportMain - thumbLength) : 0.f;
            scrollOffset = dragStartOffset + (int)(ratio * (contentMain - viewportMain));
            clamp(); updateThumb();
            return true;
        }
        bool wasHovering = isHovering;
        isHovering = isOverThumb(mx, my, wx, wy, ww, wh);
        return wasHovering != isHovering;
    }

    bool onMouseLeave() {
        bool changed = isHovering;
        isHovering = false;
        return changed;
    }
};


// ============================================================================
// SCROLLABLE LISTVIEW — STATIC (initializer_list construction)
// ============================================================================
//
//   ListView({ widget1, widget2, widget3 })
//       ->setSpacing(8)
//       ->setHorizontal(true)
//
// No state binding — children are fixed at construction time.
// Scrollbar appears on the right (vertical) or bottom (horizontal).

class ListViewStatic : public Widget {
private:
    ScrollbarState sb;
    int  itemSpacing = 0;
    std::function<WidgetPtr()> separatorBuilder;
    std::shared_ptr<ListViewStatic> self;

    // ── Separator injection (called once after children are added) ────────

    void applySeparators() {
        if (!separatorBuilder || children.size() < 2) return;
        std::vector<std::shared_ptr<Widget>> expanded;
        expanded.reserve(children.size() * 2 - 1);
        for (size_t i = 0; i < children.size(); i++) {
            expanded.push_back(children[i]);
            if (i < children.size() - 1) {
                auto sep = separatorBuilder();
                if (sep) expanded.push_back(sep);
            }
        }
        children = std::move(expanded);
    }

    // ── Child positioning ─────────────────────────────────────────────────

    void repositionChildren() {

        if (sb.horizontal) {
            int curX = x + paddingLeft - sb.scrollOffset;
            for (size_t i = 0; i < children.size(); i++) {
                auto& c = children[i];
                c->x = curX; c->y = y + paddingTop;
                c->positionChildren(c->x + c->paddingLeft, c->y + c->paddingTop,
                                    c->width  - c->paddingLeft - c->paddingRight,
                                    c->height - c->paddingTop  - c->paddingBottom);
                curX += c->width;
                if (itemSpacing > 0 && i < children.size() - 1) curX += itemSpacing;
            }
        } else {
            int curY = y + paddingTop - sb.scrollOffset;
            for (size_t i = 0; i < children.size(); i++) {
                auto& c = children[i];
                c->x = x + paddingLeft; c->y = curY;
                c->positionChildren(c->x + c->paddingLeft, c->y + c->paddingTop,
                                    c->width  - c->paddingLeft - c->paddingRight,
                                    c->height - c->paddingTop  - c->paddingBottom);
                curY += c->height;
                if (itemSpacing > 0 && i < children.size() - 1) curY += itemSpacing;
            }
        }
    }

public:
    void setSelf(std::shared_ptr<ListViewStatic> ptr) { self = ptr; }

    // ── Fluent configuration ──────────────────────────────────────────────

    std::shared_ptr<ListViewStatic> setSpacing(int s)        { itemSpacing = s; return self; }
    std::shared_ptr<ListViewStatic> setHorizontal(bool h)    { sb.horizontal = h; sb.scrollOffset = 0; markNeedsLayout(); return self; }
    std::shared_ptr<ListViewStatic> separator(std::function<WidgetPtr()> fn) { separatorBuilder = fn; applySeparators(); return self; }
    std::shared_ptr<ListViewStatic> setHeight(int h)         { height = h; autoHeight = false; markNeedsLayout(); return self; }  // ← ADD THIS
    std::shared_ptr<ListViewStatic> setPadding(int p)        { paddingLeft = paddingRight = paddingTop = paddingBottom = p; markNeedsLayout(); return self; }
    std::shared_ptr<ListViewStatic> setBackgroundColor(COLORREF c) { backgroundColor = c; hasBackground = true; markNeedsPaint(); return self; }
    std::shared_ptr<ListViewStatic> setScrollbarSize(int s)        { sb.size = s; return self; }
    std::shared_ptr<ListViewStatic> setScrollbarColor(COLORREF c)        { sb.colorNormal = c; return self; }
    std::shared_ptr<ListViewStatic> setScrollbarHoverColor(COLORREF c)   { sb.colorHover  = c; return self; }
    std::shared_ptr<ListViewStatic> setScrollbarActiveColor(COLORREF c)  { sb.colorActive = c; return self; }
    std::shared_ptr<ListViewStatic> setScrollbarTrackColor(COLORREF c)   { sb.colorTrack  = c; return self; }

    // ── Layout ────────────────────────────────────────────────────────────

    void computeLayout(HDC hdc, const BoxConstraints& constraints, FontCache& fontCache) override {
        if (autoWidth  || width  > constraints.maxWidth)  width  = constraints.maxWidth;
        if (autoHeight || height > constraints.maxHeight) height = constraints.maxHeight;

        int sbSz = sb.isScrollable ? sb.size : 0;

        if (sb.horizontal) {
            sb.viewportMain = width  - paddingLeft - paddingRight;
            int availH      = height - paddingTop  - paddingBottom - sbSz;
            int total       = 0;
            for (size_t i = 0; i < children.size(); i++) {
                children[i]->computeLayout(hdc, BoxConstraints::loose(constraints.maxWidth, availH), fontCache);
                total += children[i]->width;
                if (itemSpacing > 0 && i < children.size() - 1) total += itemSpacing;
            }
            sb.contentMain = total;
        } else {
            sb.viewportMain = height - paddingTop  - paddingBottom;
            int availW      = width  - paddingLeft - paddingRight - sbSz;
            int total       = 0;
            for (size_t i = 0; i < children.size(); i++) {
                children[i]->computeLayout(hdc, BoxConstraints::loose(availW, constraints.maxHeight), fontCache);
                total += children[i]->height;
                if (itemSpacing > 0 && i < children.size() - 1) total += itemSpacing;
            }
            sb.contentMain = total;
        }

        sb.setScrollable(sb.contentMain > sb.viewportMain);
        sb.clamp();
        sb.updateThumb();
        applyConstraints();
        needsLayout = false;
    }

    void positionChildren(int, int, int, int) override { repositionChildren(); }

    // ── Mouse events ──────────────────────────────────────────────────────

    bool handleMouseWheel(int delta) override {
        if (!sb.onWheel(delta)) return false;
        repositionChildren(); markNeedsPaint(); return true;
    }

    bool handleMouseDown(int mx, int my) override {
        if (!sb.onMouseDown(mx, my, x, y, width, height, nullptr)) return false;
        repositionChildren(); markNeedsPaint(); return true;
    }

    bool handleMouseUp(int /*mx*/, int /*my*/) override {
        if (!sb.onMouseUp()) return false;
        markNeedsPaint(); return true;
    }

    bool handleMouseMove(int mx, int my) override {
        if (!sb.onMouseMove(mx, my, x, y, width, height)) return false;
        if (sb.isDragging) repositionChildren();
        markNeedsPaint(); return true;
    }

    bool handleMouseLeave() override {
        if (!sb.onMouseLeave()) return false;
        markNeedsPaint(); return true;
    }

    // ── Render ────────────────────────────────────────────────────────────

    void render(HDC hdc, FontCache& fontCache) override {
        sb.updateThumb();
        int sbSz = sb.isScrollable ? sb.size : 0;
        RECT clipRect;
        if (sb.horizontal)
            clipRect = { x + paddingLeft, y + paddingTop, x + width - paddingRight,       y + height - paddingBottom - sbSz };
        else
            clipRect = { x + paddingLeft, y + paddingTop, x + width - paddingRight - sbSz, y + height - paddingBottom };

        HRGN clip = CreateRectRgn(clipRect.left, clipRect.top, clipRect.right, clipRect.bottom);
        SelectClipRgn(hdc, clip);

        if (hasBackground) drawRoundedRectangle(hdc);

        for (auto& child : children) {
            bool vis = sb.horizontal
                ? (child->x + child->width  >= clipRect.left && child->x < clipRect.right)
                : (child->y + child->height >= clipRect.top  && child->y < clipRect.bottom);
            if (vis) child->render(hdc, fontCache);
        }

        SelectClipRgn(hdc, NULL); DeleteObject(clip);
        sb.render(hdc, x, y, width, height);
        needsPaint = false;
    }
};

// ── Factory ───────────────────────────────────────────────────────────────────

using ListViewStaticPtr = std::shared_ptr<ListViewStatic>;

inline ListViewStaticPtr ListView(std::initializer_list<WidgetPtr> items) {
    auto w = std::make_shared<ListViewStatic>();
    w->setSelf(w);
    for (auto& item : items) if (item) w->addChild(item);
    return w;
}


// ============================================================================
// SCROLLABLE LISTVIEW — BUILDER  (State<vector<T>> binding)
// ============================================================================
//
// Supports two item types:
//
//   A) Plain values / shared_ptr to plain structs:
//      State<vector<MyStruct>> items;
//      ListView(items)->itemBuilder([](int i, const MyStruct& v){ … })
//
//   B) ReactiveItem — per-item mutation without full-list rebuilds:
//      State<vector<shared_ptr<ReactiveItem<Task>>>> items;
//      ListView(items)->itemBuilder([](int i, const shared_ptr<ReactiveItem<Task>>& r){ … })
//
//      When an item calls r->update(…) or r->set(…), only the row widget for
//      that item is rebuilt and repainted.  The rest of the list is untouched.
//
// ── Keying ────────────────────────────────────────────────────────────────
//
// By default items are keyed by the value of a uintptr_t derived from the
// item pointer (for shared_ptr types) or item index (for value types).
// Supply a custom key function to key by a data field instead:
//
//   ListView(items)
//       ->setKeyFn([](const TaskRef& r) -> uintptr_t { return r->get().id; })
//       ->itemBuilder(…);
//
// With correct keying, add/delete operations only build/destroy the affected
// row widgets — all other rows are reused from the widget cache.
//
// ── Per-item rebuild ──────────────────────────────────────────────────────
//
// When a ReactiveItem notifies its listeners, ListViewBuilder:
//   1. Calls itemBuilder again for that one index
//   2. Replaces the single child in `children`
//   3. Runs a targeted computeLayout + reposition pass
//   4. Calls markNeedsPaint()
//
// ── Separator ─────────────────────────────────────────────────────────────
//
//   ->separator([] { return SizedBox(0, 6); })
//
// Separators are inserted between content items after keyed diffing, so
// they are never in the key cache and always rebuilt with their neighbours.

template <typename T>
class ListViewBuilder : public Widget {
public:
    // ── Key function type ─────────────────────────────────────────────────
    using KeyFn = std::function<uintptr_t(const T&)>;

    // ── Item builder type ─────────────────────────────────────────────────
    using BuilderFn = std::function<WidgetPtr(int, const T&)>;

private:
    State<std::vector<T>>* boundState = nullptr;
    BuilderFn              builderFn;
    std::function<WidgetPtr()> separatorBuilderFn;
    int  itemSpacing  = 0;
    bool horizontal   = false;
    std::shared_ptr<ListViewBuilder<T>> self;

    ScrollbarState sb;

    // ── Keyed widget cache ────────────────────────────────────────────────
    //
    // Maps item key → built widget for that item (no separator).
    // Survives across rebuildList() calls so unchanged rows are reused.

    KeyFn                                           keyFn;
    std::unordered_map<uintptr_t, WidgetPtr>        widgetCache;
    std::vector<uintptr_t>                          lastKeys;

    // Subscription handles for ReactiveItem listeners, keyed by item key.
    // Stored so we can unsubscribe when an item is evicted from the cache.
    std::unordered_map<uintptr_t, size_t>           subscriptionHandles;

    // ── Key derivation ────────────────────────────────────────────────────

    uintptr_t deriveKey(const T& item, int index) const {
        if (keyFn) return keyFn(item);
        return defaultKey(item, index);
    }

    // Default: use pointer address for shared_ptr types, index for values.
    template <typename U = T>
    static uintptr_t defaultKey(const U& item, int index,
        std::enable_if_t<std::is_pointer_v<U>>* = nullptr) {
        return reinterpret_cast<uintptr_t>(item);
    }

    template <typename U = T>
    static uintptr_t defaultKey(const std::shared_ptr<typename U::element_type>& item, int,
        std::enable_if_t<!std::is_void_v<typename U::element_type>>* = nullptr) {
        return reinterpret_cast<uintptr_t>(item.get());
    }

    // Fallback: index-based key for non-pointer value types
    static uintptr_t defaultKey(const T&, int index, ...) {
        return static_cast<uintptr_t>(index);
    }

    // ── ReactiveItem subscription wiring ─────────────────────────────────
    //
    // This overload is selected only when T = shared_ptr<ReactiveItem<U>>.
    // It subscribes the rebuilt widget to the item's change notifier so that
    // a single call to ri->update(…) rebuilds only that one row.

    template <typename U>
    void maybeSubscribe(const std::shared_ptr<ReactiveItem<U>>& ri,
                        uintptr_t key, int index) {
        // Unsubscribe any stale handle for this key
        auto it = subscriptionHandles.find(key);
        if (it != subscriptionHandles.end()) {
            ri->unlisten(it->second);
            subscriptionHandles.erase(it);
        }

        // Capture weak_ptr to self so the lambda doesn't keep the widget alive
        std::weak_ptr<ListViewBuilder<T>> weakSelf = self;

        size_t handle = ri->listen([weakSelf, key, index](const U&) {
            if (auto s = weakSelf.lock()) {
                s->rebuildSingleRow(key, index);
            }
        });
        subscriptionHandles[key] = handle;
    }

    // No-op overload for plain value types
    template <typename U>
    void maybeSubscribe(const U&, uintptr_t, int) { /* plain value — no subscription */ }

    // ── Unsubscribe when evicting a cached item ───────────────────────────

    template <typename U>
    void maybeUnsubscribe(const std::shared_ptr<ReactiveItem<U>>& ri, uintptr_t key) {
        auto it = subscriptionHandles.find(key);
        if (it != subscriptionHandles.end()) {
            ri->unlisten(it->second);
            subscriptionHandles.erase(it);
        }
    }

    template <typename U>
    void maybeUnsubscribe(const U&, uintptr_t) {}

    // ── Full list rebuild (structural change: add / delete / reorder) ─────

    void rebuildList() {
        if (!boundState || !builderFn) return;

        const auto& items = boundState->get();

        // Compute new key vector
        std::vector<uintptr_t> newKeys;
        newKeys.reserve(items.size());
        for (int i = 0; i < (int)items.size(); i++)
            newKeys.push_back(deriveKey(items[i], i));

        // Early-out: nothing changed structurally
        if (newKeys == lastKeys && !children.empty()) return;

        // Evict cache entries for items no longer present
        std::unordered_map<uintptr_t, bool> newKeySet;
        for (auto k : newKeys) newKeySet[k] = true;

        std::vector<uintptr_t> evicted;
        for (auto& [k, _] : widgetCache)
            if (!newKeySet.count(k)) evicted.push_back(k);

        for (auto k : evicted) {
            // Unsubscribe ReactiveItem listener if applicable
            auto itemIt = std::find_if(items.begin(), items.end(),
                [&](const T& item){ return deriveKey(item, 0) == k; });
            // (already removed from items — look in old list via lastKeys)
            // We can't easily get the old item here, so we just clean up the handle
            auto hIt = subscriptionHandles.find(k);
            if (hIt != subscriptionHandles.end())
                subscriptionHandles.erase(hIt);
            widgetCache.erase(k);
        }

        // Build / reuse widgets for each item
        children.clear();
        for (int i = 0; i < (int)items.size(); i++) {
            uintptr_t key = newKeys[i];
            WidgetPtr w;

            auto cacheIt = widgetCache.find(key);
            if (cacheIt != widgetCache.end()) {
                w = cacheIt->second; // reuse
            } else {
                w = builderFn(i, items[i]);
                widgetCache[key] = w;
                maybeSubscribe(items[i], key, i);
            }

            if (w) addChild(w);

            // Separator (never cached — always rebuilt between content items)
            if (separatorBuilderFn && i < (int)items.size() - 1) {
                auto sep = separatorBuilderFn();
                if (sep) addChild(sep);
            }
        }

        lastKeys = newKeys;
        markNeedsLayout();
    }

    // ── Per-item rebuild (ReactiveItem mutation — no structural change) ────
    //
    // Rebuilds exactly one content child at logical index `logicalIndex`
    // (i.e. the index into the items vector, ignoring separators).
    // The separator widgets surrounding it are left untouched.

    void rebuildSingleRow(uintptr_t key, int logicalIndex) {
        if (!boundState || !builderFn) return;

        const auto& items = boundState->get();
        if (logicalIndex < 0 || logicalIndex >= (int)items.size()) return;

        // Build replacement widget
        WidgetPtr newWidget = builderFn(logicalIndex, items[logicalIndex]);
        if (!newWidget) return;

        // Update cache
        widgetCache[key] = newWidget;

        // Re-subscribe with the new widget's closure
        maybeSubscribe(items[logicalIndex], key, logicalIndex);

        // Find the child index in `children` (accounting for separators).
        // Content items are at even indices when separators are present,
        // so content child index = logicalIndex, child array index:
        //   with separators: logicalIndex * 2
        //   without:         logicalIndex
        int childIdx = separatorBuilderFn ? logicalIndex * 2 : logicalIndex;
        if (childIdx < 0 || childIdx >= (int)children.size()) return;

        children[childIdx] = newWidget;

        // Targeted layout + reposition (no full reflow needed)
        if (boundState && boundState->hasContext()) {
            if (auto* ui = boundState->getContext()) {
                ui->partialRebuild(this);
                return;
            }
        }
        markNeedsLayout();
        markNeedsPaint();
    }

    // ── Child positioning ─────────────────────────────────────────────────

    void repositionChildren() {
        if (sb.horizontal) {
            int curX = x + paddingLeft - sb.scrollOffset;
            for (size_t i = 0; i < children.size(); i++) {
                auto& c = children[i];
                c->x = curX; c->y = y + paddingTop;
                c->positionChildren(c->x + c->paddingLeft, c->y + c->paddingTop,
                                    c->width  - c->paddingLeft - c->paddingRight,
                                    c->height - c->paddingTop  - c->paddingBottom);
                curX += c->width;
                if (itemSpacing > 0 && i < children.size() - 1) curX += itemSpacing;
            }
        } else {
            int curY = y + paddingTop - sb.scrollOffset;
            for (size_t i = 0; i < children.size(); i++) {
                auto& c = children[i];
                c->x = x + paddingLeft; c->y = curY;
                c->positionChildren(c->x + c->paddingLeft, c->y + c->paddingTop,
                                    c->width  - c->paddingLeft - c->paddingRight,
                                    c->height - c->paddingTop  - c->paddingBottom);
                curY += c->height;
                if (itemSpacing > 0 && i < children.size() - 1) curY += itemSpacing;
            }
        }
    }

    // ── HWND helper ───────────────────────────────────────────────────────

    HWND getWindow() const {
        if (boundState && boundState->hasContext())
            if (auto* ui = boundState->getContext())
                return ui->getWindow();
        return nullptr;
    }

public:
    explicit ListViewBuilder(State<std::vector<T>>& state) : boundState(&state) {
        state.listen([this](const std::vector<T>&) {
            rebuildList();
            if (boundState && boundState->hasContext())
                if (auto* ui = boundState->getContext())
                    ui->partialRebuild(this);
        });
    }

    ~ListViewBuilder() override {
        if (sb.isDragging) ReleaseCapture();
    }

    void setSelf(std::shared_ptr<ListViewBuilder<T>> ptr) { self = ptr; }

    // ── Fluent configuration ──────────────────────────────────────────────

    std::shared_ptr<ListViewBuilder<T>> itemBuilder(BuilderFn fn) {
        builderFn = std::move(fn);
        // Invalidate cache so all rows are built with the new builder
        widgetCache.clear(); subscriptionHandles.clear(); lastKeys.clear();
        rebuildList();
        return self;
    }

    std::shared_ptr<ListViewBuilder<T>> separator(std::function<WidgetPtr()> fn) {
        separatorBuilderFn = std::move(fn);
        // Invalidate so separators are inserted on next rebuild
        lastKeys.clear(); rebuildList();
        return self;
    }

    std::shared_ptr<ListViewBuilder<T>> setKeyFn(KeyFn fn) {
        keyFn = std::move(fn);
        widgetCache.clear(); subscriptionHandles.clear(); lastKeys.clear();
        return self;
    }

    std::shared_ptr<ListViewBuilder<T>> setSpacing(int s)     { itemSpacing = s; return self; }
    std::shared_ptr<ListViewBuilder<T>> setHorizontal(bool h) { horizontal = h; sb.horizontal = h; sb.scrollOffset = 0; markNeedsLayout(); return self; }
    std::shared_ptr<ListViewBuilder<T>> setScrollbarSize(int s)       { sb.size        = s; return self; }
    std::shared_ptr<ListViewBuilder<T>> setScrollbarColor(COLORREF c)       { sb.colorNormal = c; return self; }
    std::shared_ptr<ListViewBuilder<T>> setScrollbarHoverColor(COLORREF c)  { sb.colorHover  = c; return self; }
    std::shared_ptr<ListViewBuilder<T>> setScrollbarActiveColor(COLORREF c) { sb.colorActive = c; return self; }
    std::shared_ptr<ListViewBuilder<T>> setScrollbarTrackColor(COLORREF c)  { sb.colorTrack  = c; return self; }

    // ── Layout ────────────────────────────────────────────────────────────

    void computeLayout(HDC hdc, const BoxConstraints& constraints, FontCache& fontCache) override {
        rebuildList();

        if (autoWidth  || width  > constraints.maxWidth)  width  = constraints.maxWidth;
        if (autoHeight || height > constraints.maxHeight) height = constraints.maxHeight;

        int sbSz = sb.isScrollable ? sb.size : 0;

        if (sb.horizontal) {
            sb.viewportMain = width  - paddingLeft - paddingRight;
            int availH      = height - paddingTop  - paddingBottom - sbSz;
            int total       = 0;
            for (size_t i = 0; i < children.size(); i++) {
                children[i]->computeLayout(hdc, BoxConstraints::loose(constraints.maxWidth, availH), fontCache);
                total += children[i]->width;
                if (itemSpacing > 0 && i < children.size() - 1) total += itemSpacing;
            }
            sb.contentMain = total;
        } else {
            sb.viewportMain = height - paddingTop  - paddingBottom;
            int availW      = width  - paddingLeft - paddingRight - sbSz;
            int total       = 0;
            for (size_t i = 0; i < children.size(); i++) {
                children[i]->computeLayout(hdc, BoxConstraints::loose(availW, constraints.maxHeight), fontCache);
                total += children[i]->height;
                if (itemSpacing > 0 && i < children.size() - 1) total += itemSpacing;
            }
            sb.contentMain = total;
        }

        bool wasScrollable = sb.isScrollable;
        sb.setScrollable(sb.contentMain > sb.viewportMain);

        if (!wasScrollable && sb.isScrollable) sb.clamp();

        sb.updateThumb();
        applyConstraints();
        needsLayout = false;
    }

    void positionChildren(int, int, int, int) override { repositionChildren(); }

    // ── Mouse events ──────────────────────────────────────────────────────

    bool handleMouseWheel(int delta) override {
        if (!sb.onWheel(delta)) return false;
        repositionChildren(); markNeedsPaint(); return true;
    }

    bool handleMouseDown(int mx, int my) override {
        if (!sb.onMouseDown(mx, my, x, y, width, height, getWindow())) return false;
        repositionChildren(); markNeedsPaint(); return true;
    }

    bool handleMouseUp(int mx, int my) override {
        if (!sb.onMouseUp()) return false;
        markNeedsPaint(); return true;
    }

    bool handleMouseMove(int mx, int my) override {
        if (!sb.onMouseMove(mx, my, x, y, width, height)) return false;
        if (sb.isDragging) repositionChildren();
        markNeedsPaint(); return true;
    }

    bool handleMouseLeave() override {
        if (!sb.onMouseLeave()) return false;
        markNeedsPaint(); return true;
    }

    // ── Render ────────────────────────────────────────────────────────────

    void render(HDC hdc, FontCache& fontCache) override {
        sb.updateThumb();
        int sbSz = sb.isScrollable ? sb.size : 0;
        RECT clipRect;
        if (sb.horizontal)
            clipRect = { x + paddingLeft, y + paddingTop, x + width - paddingRight,       y + height - paddingBottom - sbSz };
        else
            clipRect = { x + paddingLeft, y + paddingTop, x + width - paddingRight - sbSz, y + height - paddingBottom };

        HRGN clip = CreateRectRgn(clipRect.left, clipRect.top, clipRect.right, clipRect.bottom);
        SelectClipRgn(hdc, clip);

        if (hasBackground) drawRoundedRectangle(hdc);

        for (auto& child : children) {
            bool vis = sb.horizontal
                ? (child->x + child->width  >= clipRect.left && child->x < clipRect.right)
                : (child->y + child->height >= clipRect.top  && child->y < clipRect.bottom);
            if (vis) child->render(hdc, fontCache);
        }

        SelectClipRgn(hdc, NULL); DeleteObject(clip);
        sb.render(hdc, x, y, width, height);
        needsPaint = false;
    }
};


// ============================================================================
// SCROLLABLE GRIDVIEW — BUILDER  (State<vector<T>> binding)
// ============================================================================
//
// Identical reactive model to ListViewBuilder — same keyed diff, same
// ReactiveItem per-item subscription, same ScrollbarState helper.
//
// Two sizing modes:
//   Fixed column count  — GridView(state)->columns(3)
//   Fixed cell width    — GridView(state)->columnWidth(200)   (responsive)

template <typename T>
class GridViewBuilder : public Widget {
public:
    using KeyFn     = std::function<uintptr_t(const T&)>;
    using BuilderFn = std::function<WidgetPtr(int, const T&)>;

private:
    State<std::vector<T>>* boundState = nullptr;
    BuilderFn              builderFn;

    int  columnCount   = 2;
    int  fixedCellWidth = -1;
    int  spacingH      = 0;
    int  spacingV      = 0;

    std::shared_ptr<GridViewBuilder<T>> self;
    ScrollbarState sb;   // always vertical for GridView

    // ── Keyed cache (same design as ListViewBuilder) ──────────────────────
    KeyFn                                       keyFn;
    std::unordered_map<uintptr_t, WidgetPtr>    widgetCache;
    std::vector<uintptr_t>                      lastKeys;
    std::unordered_map<uintptr_t, size_t>       subscriptionHandles;

    // ── Cached layout data ────────────────────────────────────────────────
    int              _cols      = 1;
    int              _cellW     = 0;
    std::vector<int> _rowHeights;

    // ── Key derivation / subscription — same helpers as ListViewBuilder ───

    uintptr_t deriveKey(const T& item, int index) const {
        if (keyFn) return keyFn(item);
        return defaultKey(item, index);
    }

    template <typename U>
    static uintptr_t defaultKey(const std::shared_ptr<U>& item, int) {
        return reinterpret_cast<uintptr_t>(item.get());
    }
    static uintptr_t defaultKey(const T&, int index, ...) {
        return static_cast<uintptr_t>(index);
    }

    template <typename U>
    void maybeSubscribe(const std::shared_ptr<ReactiveItem<U>>& ri, uintptr_t key, int index) {
        auto it = subscriptionHandles.find(key);
        if (it != subscriptionHandles.end()) { ri->unlisten(it->second); subscriptionHandles.erase(it); }
        std::weak_ptr<GridViewBuilder<T>> weakSelf = self;
        size_t handle = ri->listen([weakSelf, key, index](const U&) {
            if (auto s = weakSelf.lock()) s->rebuildSingleCell(key, index);
        });
        subscriptionHandles[key] = handle;
    }
    template <typename U> void maybeSubscribe(const U&, uintptr_t, int) {}

    // ── Full rebuild ──────────────────────────────────────────────────────

    void rebuildList() {
        if (!boundState || !builderFn) return;

        const auto& items = boundState->get();

        std::vector<uintptr_t> newKeys;
        newKeys.reserve(items.size());
        for (int i = 0; i < (int)items.size(); i++)
            newKeys.push_back(deriveKey(items[i], i));

        if (newKeys == lastKeys && !children.empty()) return;

        // Evict stale cache entries
        std::unordered_map<uintptr_t, bool> newKeySet;
        for (auto k : newKeys) newKeySet[k] = true;
        std::vector<uintptr_t> evicted;
        for (auto& [k, _] : widgetCache)
            if (!newKeySet.count(k)) evicted.push_back(k);
        for (auto k : evicted) {
            subscriptionHandles.erase(k);
            widgetCache.erase(k);
        }

        children.clear();
        for (int i = 0; i < (int)items.size(); i++) {
            uintptr_t key = newKeys[i];
            WidgetPtr w;
            auto cacheIt = widgetCache.find(key);
            if (cacheIt != widgetCache.end()) {
                w = cacheIt->second;
            } else {
                w = builderFn(i, items[i]);
                widgetCache[key] = w;
                maybeSubscribe(items[i], key, i);
            }
            if (w) addChild(w);
        }

        lastKeys = newKeys;
        markNeedsLayout();
    }

    // ── Per-cell rebuild ──────────────────────────────────────────────────

    void rebuildSingleCell(uintptr_t key, int logicalIndex) {
        if (!boundState || !builderFn) return;
        const auto& items = boundState->get();
        if (logicalIndex < 0 || logicalIndex >= (int)items.size()) return;

        WidgetPtr newWidget = builderFn(logicalIndex, items[logicalIndex]);
        if (!newWidget) return;

        widgetCache[key] = newWidget;
        maybeSubscribe(items[logicalIndex], key, logicalIndex);

        if (logicalIndex < (int)children.size())
            children[logicalIndex] = newWidget;

        if (boundState && boundState->hasContext())
            if (auto* ui = boundState->getContext()) {
                ui->partialRebuild(this);
                return;
            }
        markNeedsLayout();
        markNeedsPaint();
    }

    // ── Grid positioning ──────────────────────────────────────────────────

    void repositionChildren() {
        if (children.empty() || _cols == 0) return;
        int contentX = x + paddingLeft;
        int rows     = (int)_rowHeights.size();
        int curY     = y + paddingTop - sb.scrollOffset;

        for (int row = 0; row < rows; row++) {
            for (int col = 0; col < _cols; col++) {
                int idx = row * _cols + col;
                if (idx >= (int)children.size()) break;
                auto& c  = children[idx];
                c->x = contentX + col * (_cellW + spacingH);
                c->y = curY;
                c->positionChildren(c->x + c->paddingLeft, c->y + c->paddingTop,
                                    c->width  - c->paddingLeft - c->paddingRight,
                                    c->height - c->paddingTop  - c->paddingBottom);
            }
            curY += _rowHeights[row] + (row < rows - 1 ? spacingV : 0);
        }
    }

    int resolvedColumnCount(int contentWidth) const {
        if (fixedCellWidth > 0) return max(1, (contentWidth + spacingH) / (fixedCellWidth + spacingH));
        return max(1, columnCount);
    }

    HWND getWindow() const {
        if (boundState && boundState->hasContext())
            if (auto* ui = boundState->getContext()) return ui->getWindow();
        return nullptr;
    }

public:
    explicit GridViewBuilder(State<std::vector<T>>& state) : boundState(&state) {
        state.listen([this](const std::vector<T>&) {
            rebuildList();
            if (boundState && boundState->hasContext())
                if (auto* ui = boundState->getContext())
                    ui->partialRebuild(this);
        });
    }

    ~GridViewBuilder() override { if (sb.isDragging) ReleaseCapture(); }

    void setSelf(std::shared_ptr<GridViewBuilder<T>> ptr) { self = ptr; }

    // ── Fluent configuration ──────────────────────────────────────────────

    std::shared_ptr<GridViewBuilder<T>> itemBuilder(BuilderFn fn) {
        builderFn = std::move(fn);
        widgetCache.clear(); subscriptionHandles.clear(); lastKeys.clear();
        rebuildList();
        return self;
    }

    std::shared_ptr<GridViewBuilder<T>> setKeyFn(KeyFn fn) {
        keyFn = std::move(fn);
        widgetCache.clear(); subscriptionHandles.clear(); lastKeys.clear();
        return self;
    }

    std::shared_ptr<GridViewBuilder<T>> columns(int c)       { columnCount = c; fixedCellWidth = -1; markNeedsLayout(); return self; }
    std::shared_ptr<GridViewBuilder<T>> columnWidth(int w)   { fixedCellWidth = w; markNeedsLayout(); return self; }
    std::shared_ptr<GridViewBuilder<T>> setSpacingH(int s)   { spacingH = s; markNeedsLayout(); return self; }
    std::shared_ptr<GridViewBuilder<T>> setSpacingV(int s)   { spacingV = s; markNeedsLayout(); return self; }
    std::shared_ptr<GridViewBuilder<T>> setSpacing(int s)    { spacingH = spacingV = s; markNeedsLayout(); return self; }
    std::shared_ptr<GridViewBuilder<T>> setScrollbarColor(COLORREF c)        { sb.colorNormal = c; return self; }
    std::shared_ptr<GridViewBuilder<T>> setScrollbarHoverColor(COLORREF c)   { sb.colorHover  = c; return self; }
    std::shared_ptr<GridViewBuilder<T>> setScrollbarActiveColor(COLORREF c)  { sb.colorActive = c; return self; }
    std::shared_ptr<GridViewBuilder<T>> setScrollbarTrackColor(COLORREF c)   { sb.colorTrack  = c; return self; }
    std::shared_ptr<GridViewBuilder<T>> setScrollbarWidth(int w)             { sb.size = w; return self; }

    // ── Layout ────────────────────────────────────────────────────────────

    void computeLayout(HDC hdc, const BoxConstraints& constraints, FontCache& fontCache) override {
        rebuildList();

        if (autoHeight || height > constraints.maxHeight) height = constraints.maxHeight;
        if (autoWidth  || width  > constraints.maxWidth)  width  = constraints.maxWidth;

        sb.viewportMain = height - paddingTop - paddingBottom;

        int sbW      = sb.isScrollable ? sb.size : 0;
        int contentW = width - paddingLeft - paddingRight - sbW;

        int cols  = resolvedColumnCount(contentW);
        int cellW = cols > 1 ? (contentW - spacingH * (cols - 1)) / cols : contentW;

        int rows = cols > 0 ? ((int)children.size() + cols - 1) / cols : 0;
        std::vector<int> rowHeights(rows, 0);

        for (int i = 0; i < (int)children.size(); i++) {
            int row = i / cols;
            children[i]->computeLayout(hdc, BoxConstraints::loose(cellW, sb.viewportMain), fontCache);
            rowHeights[row] = max(rowHeights[row], children[i]->height);
        }

        int total = 0;
        for (int r = 0; r < rows; r++) { total += rowHeights[r]; if (r < rows - 1) total += spacingV; }
        sb.contentMain = total;

        bool wasScrollable = sb.isScrollable;
        sb.setScrollable(sb.contentMain > sb.viewportMain);
        if (!wasScrollable && sb.isScrollable) sb.clamp();
        sb.clamp();

        // Re-measure with definitive scrollbar width
        sbW      = sb.isScrollable ? sb.size : 0;
        contentW = width - paddingLeft - paddingRight - sbW;
        cols     = resolvedColumnCount(contentW);
        cellW    = cols > 1 ? (contentW - spacingH * (cols - 1)) / cols : contentW;

        _cols      = cols;
        _cellW     = cellW;
        _rowHeights = rowHeights;

        sb.updateThumb();
        applyConstraints();
        needsLayout = false;
    }

    void positionChildren(int, int, int, int) override { repositionChildren(); }

    // ── Mouse events ──────────────────────────────────────────────────────

    bool handleMouseWheel(int delta) override {
        if (!sb.onWheel(delta)) return false;
        repositionChildren(); markNeedsPaint(); return true;
    }

    bool handleMouseDown(int mx, int my) override {
        if (!sb.onMouseDown(mx, my, x, y, width, height, getWindow())) return false;
        repositionChildren(); markNeedsPaint(); return true;
    }

    bool handleMouseUp(int mx, int my) override {
        if (!sb.onMouseUp()) return false;
        markNeedsPaint(); return true;
    }

    bool handleMouseMove(int mx, int my) override {
        if (!sb.onMouseMove(mx, my, x, y, width, height)) return false;
        if (sb.isDragging) repositionChildren();
        markNeedsPaint(); return true;
    }

    bool handleMouseLeave() override {
        if (!sb.onMouseLeave()) return false;
        markNeedsPaint(); return true;
    }

    // ── Render ────────────────────────────────────────────────────────────

    void render(HDC hdc, FontCache& fontCache) override {
        sb.updateThumb();
        int sbW      = sb.isScrollable ? sb.size : 0;
        int clipRight = x + width - paddingRight - sbW;

        HRGN clip = CreateRectRgn(x + paddingLeft, y + paddingTop, clipRight, y + height - paddingBottom);
        SelectClipRgn(hdc, clip);

        if (hasBackground) drawRoundedRectangle(hdc);

        int viewTop    = y + paddingTop;
        int viewBottom = y + height - paddingBottom;
        for (auto& child : children)
            if (child->y + child->height >= viewTop && child->y < viewBottom)
                child->render(hdc, fontCache);

        SelectClipRgn(hdc, NULL); DeleteObject(clip);
        sb.render(hdc, x, y, width, height);
        needsPaint = false;
    }
};


// ============================================================================
// GRID WIDGET  (static, non-scrollable)
// ============================================================================
//
// Unchanged from the original except it now uses the shared ScrollbarState
// struct for internal consistency.  API is identical.

class GridWidget : public Widget {
public:
    int columnCount   = 2;
    int fixedCellWidth = -1;
    int spacingH      = 0;
    int spacingV      = 0;

    void computeLayout(HDC hdc, const BoxConstraints& constraints, FontCache& fontCache) override {
        if (children.empty()) {
            if (autoWidth)  width  = paddingLeft + paddingRight;
            if (autoHeight) height = paddingTop  + paddingBottom;
            applyConstraints(); needsLayout = false; return;
        }

        int contentWidth = constraints.maxWidth - paddingLeft - paddingRight;
        int cols  = max(1, resolvedColumnCount(contentWidth));
        int cellW = (contentWidth - spacingH * (cols - 1)) / cols;
        int rows  = ((int)children.size() + cols - 1) / cols;
        std::vector<int> rowHeights(rows, 0);

        for (int i = 0; i < (int)children.size(); i++) {
            int row = i / cols;
            int childW = (crossAxisAlignment == CrossAxisAlignment::Stretch) ? cellW : min(cellW, contentWidth);
            children[i]->computeLayout(hdc, BoxConstraints::loose(childW, constraints.maxHeight), fontCache);
            rowHeights[row] = max(rowHeights[row], children[i]->height);
        }

        int totalH = 0;
        for (int r = 0; r < rows; r++) { totalH += rowHeights[r]; if (r < rows - 1) totalH += spacingV; }

        if (autoWidth)  width  = constraints.maxWidth;
        if (autoHeight) height = totalH + paddingTop + paddingBottom;

        applyConstraints();
        needsLayout = false;
        _cols = cols; _cellW = cellW; _rowHeights = rowHeights;
    }

    void positionChildren(int contentX, int contentY, int contentWidth, int /*contentHeight*/) override {
        if (children.empty() || _cols == 0) return;

        int totalGridW = _cellW * _cols + spacingH * (_cols - 1);
        int startX = contentX;
        if (mainAxisAlignment == MainAxisAlignment::Center)  startX += (contentWidth - totalGridW) / 2;
        else if (mainAxisAlignment == MainAxisAlignment::End) startX += contentWidth - totalGridW;

        int curY = contentY;
        int rows = (int)_rowHeights.size();

        for (int row = 0; row < rows; row++) {
            for (int col = 0; col < _cols; col++) {
                int idx = row * _cols + col;
                if (idx >= (int)children.size()) break;
                auto& c = children[idx];

                int cellX  = startX + col * (_cellW + spacingH);
                int childX = cellX;
                if      (crossAxisAlignment == CrossAxisAlignment::Center)  childX = cellX + (_cellW - c->width) / 2;
                else if (crossAxisAlignment == CrossAxisAlignment::End)     childX = cellX + _cellW - c->width;
                else if (crossAxisAlignment == CrossAxisAlignment::Stretch) c->width = _cellW;

                c->x = childX; c->y = curY;
                c->positionChildren(c->x + c->paddingLeft, c->y + c->paddingTop,
                                    c->width  - c->paddingLeft - c->paddingRight,
                                    c->height - c->paddingTop  - c->paddingBottom);
            }
            curY += _rowHeights[row] + (row < rows - 1 ? spacingV : 0);
        }
    }

    void render(HDC hdc, FontCache& fontCache) override {
        if (hasBackground) drawRoundedRectangle(hdc);
        for (auto& c : children) c->render(hdc, fontCache);
        needsPaint = false;
    }

    // ── Fluent setters ────────────────────────────────────────────────────

    std::shared_ptr<GridWidget> setColumnCount(int c)          { columnCount = c; fixedCellWidth = -1; markNeedsLayout(); return self(); }
    std::shared_ptr<GridWidget> setColumnWidth(int w)          { fixedCellWidth = w; markNeedsLayout(); return self(); }
    std::shared_ptr<GridWidget> setSpacing(int s)              { spacingH = spacingV = s; markNeedsLayout(); return self(); }
    std::shared_ptr<GridWidget> setSpacingH(int s)             { spacingH = s; markNeedsLayout(); return self(); }
    std::shared_ptr<GridWidget> setSpacingV(int s)             { spacingV = s; markNeedsLayout(); return self(); }
    std::shared_ptr<GridWidget> setCrossAxisAlignment(CrossAxisAlignment a) { crossAxisAlignment = a; markNeedsLayout(); return self(); }
    std::shared_ptr<GridWidget> setMainAxisAlignment(MainAxisAlignment a)   { mainAxisAlignment  = a; markNeedsLayout(); return self(); }
    std::shared_ptr<GridWidget> setPadding(int p)              { paddingLeft = paddingRight = paddingTop = paddingBottom = p; markNeedsLayout(); return self(); }
    std::shared_ptr<GridWidget> setPaddingAll(int l, int t, int r, int b) { paddingLeft=l; paddingTop=t; paddingRight=r; paddingBottom=b; markNeedsLayout(); return self(); }
    std::shared_ptr<GridWidget> setWidth(int w)                { width = w; autoWidth = false; markNeedsLayout(); return self(); }
    std::shared_ptr<GridWidget> setHeight(int h)               { height = h; autoHeight = false; markNeedsLayout(); return self(); }
    std::shared_ptr<GridWidget> setBackgroundColor(COLORREF c) { backgroundColor = c; hasBackground = true; markNeedsPaint(); return self(); }
    std::shared_ptr<GridWidget> setFlex(int f)                 { flex = f; markNeedsLayout(); return self(); }

private:
    int              _cols = 0, _cellW = 0;
    std::vector<int> _rowHeights;

    int resolvedColumnCount(int contentWidth) const {
        if (fixedCellWidth > 0) return max(1, (contentWidth + spacingH) / (fixedCellWidth + spacingH));
        return columnCount;
    }
    std::shared_ptr<GridWidget> self() { return std::static_pointer_cast<GridWidget>(shared_from_this()); }
};


// ============================================================================
// FACTORY FUNCTIONS
// ============================================================================

using GridWidgetPtr = std::shared_ptr<GridWidget>;

template <typename... Widgets>
GridWidgetPtr Grid(int columns, Widgets... widgets) {
    auto w = std::make_shared<GridWidget>();
    w->columnCount = columns;
    (w->addChild(widgets), ...);
    return w;
}

template <typename... Widgets>
GridWidgetPtr GridFixedWidth(int cellWidth, Widgets... widgets) {
    auto w = std::make_shared<GridWidget>();
    w->fixedCellWidth = cellWidth;
    (w->addChild(widgets), ...);
    return w;
}

inline GridWidgetPtr GridFromList(int columns, const std::vector<WidgetPtr>& items) {
    auto w = std::make_shared<GridWidget>();
    w->columnCount = columns;
    for (auto& item : items) w->addChild(item);
    return w;
}

inline GridWidgetPtr GridFixedWidthFromList(int cellWidth, const std::vector<WidgetPtr>& items) {
    auto w = std::make_shared<GridWidget>();
    w->fixedCellWidth = cellWidth;
    for (auto& item : items) w->addChild(item);
    return w;
}

// State-bound ListView — plain value type or ReactiveItemPtr
template <typename T>
inline std::shared_ptr<ListViewBuilder<T>> ListView(State<std::vector<T>>& state) {
    auto w = std::make_shared<ListViewBuilder<T>>(state);
    w->setSelf(w);
    return w;
}

// State-bound GridView — plain value type or ReactiveItemPtr
template <typename T>
inline std::shared_ptr<GridViewBuilder<T>> GridView(State<std::vector<T>>& state) {
    auto w = std::make_shared<GridViewBuilder<T>>(state);
    w->setSelf(w);
    return w;
}

#endif // FLUX_COLLECTION_HPP