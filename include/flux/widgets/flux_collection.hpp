#ifndef FLUX_COLLECTION_HPP
#define FLUX_COLLECTION_HPP

#include "../flux_core.hpp"
#include "../flux_gesture.hpp"
#include "../flux_state.hpp"
#include "flux_reactive_item.hpp"

#include <functional>
#include <unordered_map>
#include <vector>

// ============================================================================
// INTERNAL — scrollbar helpers shared by all collection widgets
// ============================================================================

struct ScrollbarState {
  // ── Configuration (set once by the owner) ────────────────────────────
  int size = 8;
  bool horizontal = false;

  Color colorNormal  = Color::fromRGB(180, 180, 180);
  Color colorHover   = Color::fromRGB(140, 140, 140);
  Color colorActive  = Color::fromRGB(100, 100, 100);
  Color colorTrack   = Color::fromRGB(245, 245, 245);

  // ── Computed each layout pass ─────────────────────────────────────────
  int contentMain  = 0;
  int viewportMain = 0;
  bool isScrollable = false;

  int scrollOffset = 0;
  int thumbLength  = 0;
  int thumbOffset  = 0;

  // ── Interaction state ─────────────────────────────────────────────────
  bool isDragging    = false;
  bool isHovering    = false;
  int  dragStartPos  = 0;
  int  dragStartOffset = 0;

  // ── Clamp / update ────────────────────────────────────────────────────

  void clamp() {
    int maxScroll = std::max(0, contentMain - viewportMain);
    scrollOffset  = std::max(0, std::min(scrollOffset, maxScroll));
  }

  void updateThumb() {
    if (!isScrollable) { thumbLength = thumbOffset = 0; return; }
    float visRatio  = (float)viewportMain / (float)contentMain;
    thumbLength     = std::max(30, (int)(viewportMain * visRatio));
    float scrollRatio =
        (contentMain > viewportMain)
            ? (float)scrollOffset / (float)(contentMain - viewportMain)
            : 0.f;
    thumbOffset = (int)(scrollRatio * (viewportMain - thumbLength));
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

  bool isOverThumb(int mx, int my, int wx, int wy, int ww, int wh) const {
    if (!isScrollable) return false;
    if (horizontal) {
      int sbY = wy + wh - size;
      return mx >= wx + thumbOffset && mx < wx + thumbOffset + thumbLength &&
             my >= sbY && my < wy + wh;
    } else {
      int sbX = wx + ww - size;
      return mx >= sbX && mx < wx + ww &&
             my >= wy + thumbOffset && my < wy + thumbOffset + thumbLength;
    }
  }

  bool isInStrip(int mx, int my, int wx, int wy, int ww, int wh) const {
    if (!isScrollable) return false;
    if (horizontal)
      return my >= wy + wh - size && my < wy + wh;
    else
      return mx >= wx + ww - size && mx < wx + ww;
  }

  // ── Rendering ─────────────────────────────────────────────────────────

  void render(GraphicsContext &ctx, int wx, int wy, int ww, int wh) const {
    if (!isScrollable) return;
    Painter painter(ctx);
    Color thumbColor = isDragging   ? colorActive
                     : isHovering  ? colorHover
                                   : colorNormal;
    if (horizontal) {
      int sbY = wy + wh - size;
      painter.fillRect(wx, sbY, ww, size, colorTrack);
      painter.fillRect(wx + thumbOffset, sbY, thumbLength, size, thumbColor);
    } else {
      int sbX = wx + ww - size;
      painter.fillRect(sbX, wy, size, wh, colorTrack);
      painter.fillRect(sbX, wy + thumbOffset, size, thumbLength, thumbColor);
    }
  }

  // ── Mouse handlers ────────────────────────────────────────────────────

  bool onWheel(int delta) {
    if (!isScrollable) return false;
    scrollOffset -= (delta / WHEEL_DELTA) * 40;
    clamp();
    updateThumb();
    return true;
  }

  bool onMouseDown(int mx, int my, int wx, int wy, int ww, int wh) {
    if (!isInStrip(mx, my, wx, wy, ww, wh)) return false;
    int pos = horizontal ? mx - wx : my - wy;
    if (pos >= thumbOffset && pos < thumbOffset + thumbLength) {
      isDragging       = true;
      dragStartPos     = horizontal ? mx : my;
      dragStartOffset  = scrollOffset;
    } else {
      float ratio  = (float)pos / (float)viewportMain;
      scrollOffset = (int)(ratio * (contentMain - viewportMain));
      clamp();
      updateThumb();
    }
    return true;
  }

  bool onMouseUp() {
    if (!isDragging) return false;
    isDragging = false;
    isHovering = false;
    return true;
  }

  bool onMouseMove(int mx, int my, int wx, int wy, int ww, int wh) {
    if (isDragging) {
      if (!isScrollable) { onMouseUp(); return true; }
      int curPos = horizontal ? mx : my;
      int delta  = curPos - dragStartPos;
      float ratio =
          (viewportMain > thumbLength)
              ? (float)delta / (float)(viewportMain - thumbLength)
              : 0.f;
      scrollOffset =
          dragStartOffset + (int)(ratio * (contentMain - viewportMain));
      clamp();
      updateThumb();
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


class ListViewStatic : public Widget {
private:
  ScrollbarState sb;
  GestureState   gesture;
  TimerID        flingTimer = 0;
  int            itemSpacing = 0;
  std::function<WidgetPtr()> separatorBuilder;
  std::shared_ptr<ListViewStatic> self;

  void stopFling() {
    if (flingTimer) {
      if (auto *ui = FluxUI::getCurrentInstance())
        ui->clearInterval(flingTimer);
      flingTimer = 0;
    }
  }

  void startFling() {
    stopFling();
    if (auto *ui = FluxUI::getCurrentInstance()) {
      flingTimer = ui->setInterval(16, [this]() {
        int delta = gesture.tickFling();
        if (delta == 0) { stopFling(); return; }
        sb.scrollOffset += delta;
        sb.clamp();
        sb.updateThumb();
        repositionChildren();
        markNeedsPaint();
        if (auto *u = FluxUI::getCurrentInstance())
          u->invalidateWidget(this);
      });
    }
  }

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

  void repositionChildren() {
    if (sb.horizontal) {
      int curX = x + paddingLeft - sb.scrollOffset;
      for (size_t i = 0; i < children.size(); i++) {
        auto &c = children[i];
        c->x = curX;
        c->y = y + paddingTop;
        c->positionChildren(c->x + c->paddingLeft, c->y + c->paddingTop,
                            c->width  - c->paddingLeft - c->paddingRight,
                            c->height - c->paddingTop  - c->paddingBottom);
        curX += c->width;
        if (itemSpacing > 0 && i < children.size() - 1)
          curX += itemSpacing;
      }
    } else {
      int curY = y + paddingTop - sb.scrollOffset;
      for (size_t i = 0; i < children.size(); i++) {
        auto &c = children[i];
        c->x = x + paddingLeft;
        c->y = curY;
        c->positionChildren(c->x + c->paddingLeft, c->y + c->paddingTop,
                            c->width  - c->paddingLeft - c->paddingRight,
                            c->height - c->paddingTop  - c->paddingBottom);
        curY += c->height;
        if (itemSpacing > 0 && i < children.size() - 1)
          curY += itemSpacing;
      }
    }
  }

public:
  ~ListViewStatic() override { stopFling(); }
  void setSelf(std::shared_ptr<ListViewStatic> ptr) { self = ptr; }

  // ── Fluent configuration ──────────────────────────────────────────────

  std::shared_ptr<ListViewStatic> setSpacing(int s) { itemSpacing = s; return self; }
  std::shared_ptr<ListViewStatic> setHorizontal(bool h) {
    sb.horizontal = h; sb.scrollOffset = 0; markNeedsLayout(); return self;
  }
  std::shared_ptr<ListViewStatic> separator(std::function<WidgetPtr()> fn) {
    separatorBuilder = fn; applySeparators(); return self;
  }
  std::shared_ptr<ListViewStatic> setHeight(int h) {
    height = h; autoHeight = false; markNeedsLayout(); return self;
  }
  std::shared_ptr<ListViewStatic> setPadding(int p) {
    paddingLeft = paddingRight = paddingTop = paddingBottom = p;
    markNeedsLayout(); return self;
  }
  std::shared_ptr<ListViewStatic> setBackgroundColor(Color c) {
    backgroundColor = c; hasBackground = true; markNeedsPaint(); return self;
  }
  std::shared_ptr<ListViewStatic> setScrollbarSize(int s)  { sb.size = s; return self; }
  std::shared_ptr<ListViewStatic> setScrollbarColor(Color c)       { sb.colorNormal = c; return self; }
  std::shared_ptr<ListViewStatic> setScrollbarHoverColor(Color c)  { sb.colorHover  = c; return self; }
  std::shared_ptr<ListViewStatic> setScrollbarActiveColor(Color c) { sb.colorActive = c; return self; }
  std::shared_ptr<ListViewStatic> setScrollbarTrackColor(Color c)  { sb.colorTrack  = c; return self; }

  // ── Layout ────────────────────────────────────────────────────────────
  //
  // Two-pass algorithm (mirrors Flutter):
  //
  //   Pass A — measure children with the FULL cross-axis space (no scrollbar).
  //   Determine whether the content overflows and a scrollbar is needed.
  //
  //   Pass B — if a scrollbar appeared (and wasn't there before), re-measure
  //   children with the reduced cross-axis space so they don't underlap the bar.
  //   If nothing changed, skip pass B.

  void computeLayout(GraphicsContext &ctx, const BoxConstraints &constraints,
                     FontCache &fontCache) override {

    // ── Step 1: resolve our own size from incoming constraints.
    // Like Flutter's ListView we FILL the available space on both axes.
    if (autoWidth  || width  > constraints.maxWidth)  width  = constraints.maxWidth;
    if (autoHeight || height > constraints.maxHeight) height = constraints.maxHeight;

    const int padH = paddingLeft + paddingRight;
    const int padV = paddingTop  + paddingBottom;

    // ── Step 2: Pass A — measure without scrollbar.
    auto measureChildren = [&](int crossAxisSpace) {
      int total = 0;
      if (sb.horizontal) {
        int availH = std::max(0, crossAxisSpace);
        for (size_t i = 0; i < children.size(); i++) {
          children[i]->computeLayout(
              ctx, BoxConstraints::loose(kUnbounded, availH), fontCache);
          total += children[i]->width;
          if (itemSpacing > 0 && i < children.size() - 1)
            total += itemSpacing;
        }
      } else {
        int availW = std::max(0, crossAxisSpace);
        for (size_t i = 0; i < children.size(); i++) {
          children[i]->computeLayout(
              ctx, BoxConstraints::loose(availW, kUnbounded), fontCache);
          total += children[i]->height;
          if (itemSpacing > 0 && i < children.size() - 1)
            total += itemSpacing;
        }
      }
      return total;
    };

    if (sb.horizontal) {
      // Full cross-axis height (no scrollbar yet)
      int fullCross = height - padV;
      int total = measureChildren(fullCross);
      sb.viewportMain = std::max(0, width - padH);
      sb.contentMain  = total;

      bool needsBar = (total > sb.viewportMain);
      bool hadBar   = sb.isScrollable;
      sb.setScrollable(needsBar);

      // Pass B: re-measure if scrollbar just appeared (cross-axis shrinks)
      if (needsBar && !hadBar) {
        int reducedCross = std::max(0, fullCross - sb.size);
        measureChildren(reducedCross);
      }
    } else {
      // Full cross-axis width (no scrollbar yet)
      int fullCross = width - padH;
      int total = measureChildren(fullCross);
      sb.viewportMain = std::max(0, height - padV);
      sb.contentMain  = total;

      bool needsBar = (total > sb.viewportMain);
      bool hadBar   = sb.isScrollable;
      sb.setScrollable(needsBar);

      // Pass B: re-measure if scrollbar just appeared (cross-axis shrinks)
      if (needsBar && !hadBar) {
        int reducedCross = std::max(0, fullCross - sb.size);
        measureChildren(reducedCross);
        // Recompute total after re-measure (heights may differ)
        total = 0;
        for (size_t i = 0; i < children.size(); i++) {
          total += children[i]->height;
          if (itemSpacing > 0 && i < children.size() - 1)
            total += itemSpacing;
        }
        sb.contentMain = total;
      }
    }

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
    stopFling();
    if (sb.onMouseDown(mx, my, x, y, width, height)) {
      if (sb.isDragging)
        if (auto *ui = FluxUI::getCurrentInstance()) ui->captureMouseInput();
      repositionChildren(); markNeedsPaint(); return true;
    }
    if (sb.isScrollable && mx >= x && mx < x + width && my >= y && my < y + height) {
      gesture.horizontal = sb.horizontal;
      gesture.onDown(mx, my);
      if (auto *ui = FluxUI::getCurrentInstance()) ui->captureMouseInput();
      return true;
    }
    return false;
  }

  bool handleMouseUp(int mx, int my) override {
    if (sb.isDragging) {
      sb.onMouseUp();
      if (auto *ui = FluxUI::getCurrentInstance()) ui->releaseMouseInput();
      markNeedsPaint(); return true;
    }
    if (gesture.isDragging) {
      gesture.onUp(mx, my);
      if (auto *ui = FluxUI::getCurrentInstance()) ui->releaseMouseInput();
      if (gesture.isFling()) startFling();
      markNeedsPaint(); return true;
    }
    return false;
  }

  bool handleMouseMove(int mx, int my) override {
    if (sb.isDragging) {
      if (!sb.onMouseMove(mx, my, x, y, width, height)) return false;
      repositionChildren(); markNeedsPaint(); return true;
    }
    if (gesture.isDragging) {
      int delta = gesture.onMove(mx, my);
      if (delta != 0) {
        sb.scrollOffset += delta;
        sb.clamp(); sb.updateThumb();
        repositionChildren(); markNeedsPaint();
      }
      return true;
    }
    if (sb.onMouseMove(mx, my, x, y, width, height)) { markNeedsPaint(); return true; }
    return false;
  }

  bool handleMouseLeave() override {
    gesture.cancel();
    if (!sb.onMouseLeave()) return false;
    markNeedsPaint(); return true;
  }

  // ── Render ────────────────────────────────────────────────────────────

  void render(GraphicsContext &ctx, FontCache &fontCache) override {

    sb.updateThumb();
    Painter painter(ctx);

    int sbSz = sb.isScrollable ? sb.size : 0;
    int clipX1, clipY1, clipX2, clipY2;

    if (sb.horizontal) {
      clipX1 = x + paddingLeft;
      clipY1 = y + paddingTop;
      clipX2 = x + width  - paddingRight;
      clipY2 = y + height - paddingBottom - sbSz;
    } else {
      clipX1 = x + paddingLeft;
      clipY1 = y + paddingTop;
      clipX2 = x + width  - paddingRight - sbSz;
      clipY2 = y + height - paddingBottom;
    }

    painter.pushClipRect(clipX1, clipY1, clipX2 - clipX1, clipY2 - clipY1);

    if (hasBackground) drawRoundedRectangle(ctx);

    for (auto &child : children) {
      bool vis = sb.horizontal
          ? (child->x + child->width  >= clipX1 && child->x < clipX2)
          : (child->y + child->height >= clipY1 && child->y < clipY2);
      if (vis) child->render(ctx, fontCache);
    }

    painter.popClipRect();
    sb.render(ctx, x, y, width, height);
    needsPaint = false;
  }
};

// ── Factory ───────────────────────────────────────────────────────────────────

using ListViewStaticPtr = std::shared_ptr<ListViewStatic>;

inline ListViewStaticPtr ListView(std::initializer_list<WidgetPtr> items) {
  auto w = std::make_shared<ListViewStatic>();
  w->setSelf(w);
  for (auto &item : items)
    if (item) w->addChild(item);
  return w;
}

// ============================================================================
// SCROLLABLE LISTVIEW — BUILDER  (State<vector<T>> binding)
// ============================================================================

template <typename T>
class ListViewBuilder : public Widget {
public:
  using KeyFn     = std::function<uintptr_t(const T &)>;
  using BuilderFn = std::function<WidgetPtr(int, const T &)>;

private:
  State<std::vector<T>> *boundState = nullptr;
  BuilderFn builderFn;
  std::function<WidgetPtr()> separatorBuilderFn;
  int  itemSpacing = 0;
  bool horizontal  = false;
  std::shared_ptr<ListViewBuilder<T>> self;

  ScrollbarState sb;
  GestureState   gesture;
  TimerID        flingTimer = 0;

  KeyFn keyFn;
  std::unordered_map<uintptr_t, WidgetPtr> widgetCache;
  std::vector<uintptr_t> lastKeys;
  std::unordered_map<uintptr_t, size_t> subscriptionHandles;

  // ── Key derivation ────────────────────────────────────────────────────

  uintptr_t deriveKey(const T &item, int index) const {
    if (keyFn) return keyFn(item);
    return defaultKey(item, index);
  }

  template <typename U = T>
  static uintptr_t defaultKey(const U &item, int index,
      std::enable_if_t<std::is_pointer_v<U>> * = nullptr) {
    return reinterpret_cast<uintptr_t>(item);
  }
  template <typename U = T>
  static uintptr_t defaultKey(
      const std::shared_ptr<typename U::element_type> &item, int,
      std::enable_if_t<!std::is_void_v<typename U::element_type>> * = nullptr) {
    return reinterpret_cast<uintptr_t>(item.get());
  }
  static uintptr_t defaultKey(const T &, int index, ...) {
    return static_cast<uintptr_t>(index);
  }

  // ── ReactiveItem subscription ─────────────────────────────────────────

  template <typename U>
  void maybeSubscribe(const std::shared_ptr<ReactiveItem<U>> &ri,
                      uintptr_t key, int index) {
    auto it = subscriptionHandles.find(key);
    if (it != subscriptionHandles.end()) { ri->unlisten(it->second); subscriptionHandles.erase(it); }
    std::weak_ptr<ListViewBuilder<T>> weakSelf = self;
    size_t handle = ri->listen([weakSelf, key, index](const U &) {
      if (auto s = weakSelf.lock()) s->rebuildSingleRow(key, index);
    });
    subscriptionHandles[key] = handle;
  }
  template <typename U>
  void maybeSubscribe(const U &, uintptr_t, int) {}

  template <typename U>
  void maybeUnsubscribe(const std::shared_ptr<ReactiveItem<U>> &ri, uintptr_t key) {
    auto it = subscriptionHandles.find(key);
    if (it != subscriptionHandles.end()) { ri->unlisten(it->second); subscriptionHandles.erase(it); }
  }
  template <typename U>
  void maybeUnsubscribe(const U &, uintptr_t) {}

  // ── Full list rebuild ─────────────────────────────────────────────────

  void rebuildList() {
    if (!boundState || !builderFn) return;
    const auto &items = boundState->get();

    std::vector<uintptr_t> newKeys;
    newKeys.reserve(items.size());
    for (int i = 0; i < (int)items.size(); i++)
      newKeys.push_back(deriveKey(items[i], i));

    if (newKeys == lastKeys && !children.empty()) return;

    std::unordered_map<uintptr_t, bool> newKeySet;
    for (auto k : newKeys) newKeySet[k] = true;

    std::vector<uintptr_t> evicted;
    for (auto &[k, _] : widgetCache)
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
      if (separatorBuilderFn && i < (int)items.size() - 1) {
        auto sep = separatorBuilderFn();
        if (sep) addChild(sep);
      }
    }
    lastKeys = newKeys;
    markNeedsLayout();
  }

  // ── Per-row rebuild ───────────────────────────────────────────────────

  void rebuildSingleRow(uintptr_t key, int logicalIndex) {
    if (!boundState || !builderFn) return;
    const auto &items = boundState->get();
    if (logicalIndex < 0 || logicalIndex >= (int)items.size()) return;

    WidgetPtr newWidget = builderFn(logicalIndex, items[logicalIndex]);
    if (!newWidget) return;
    widgetCache[key] = newWidget;
    maybeSubscribe(items[logicalIndex], key, logicalIndex);

    int childIdx = separatorBuilderFn ? logicalIndex * 2 : logicalIndex;
    if (childIdx < 0 || childIdx >= (int)children.size()) return;
    children[childIdx] = newWidget;

    if (boundState && boundState->hasContext())
      if (auto *ui = boundState->getContext()) { ui->partialRebuild(this); return; }
    markNeedsLayout();
    markNeedsPaint();
  }

  // ── Child positioning ─────────────────────────────────────────────────

  void repositionChildren() {
    if (sb.horizontal) {
      int curX = x + paddingLeft - sb.scrollOffset;
      for (size_t i = 0; i < children.size(); i++) {
        auto &c = children[i];
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
        auto &c = children[i];
        c->x = x + paddingLeft; c->y = curY;
        c->positionChildren(c->x + c->paddingLeft, c->y + c->paddingTop,
                            c->width  - c->paddingLeft - c->paddingRight,
                            c->height - c->paddingTop  - c->paddingBottom);
        curY += c->height;
        if (itemSpacing > 0 && i < children.size() - 1) curY += itemSpacing;
      }
    }
  }

  void stopFling() {
    if (flingTimer) {
      if (auto *ui = FluxUI::getCurrentInstance()) ui->clearInterval(flingTimer);
      flingTimer = 0;
    }
  }

  void startFling() {
    stopFling();
    if (auto *ui = FluxUI::getCurrentInstance()) {
      flingTimer = ui->setInterval(16, [this]() {
        int delta = gesture.tickFling();
        if (delta == 0) { stopFling(); return; }
        sb.scrollOffset += delta; sb.clamp(); sb.updateThumb();
        repositionChildren(); markNeedsPaint();
        if (auto *u = FluxUI::getCurrentInstance()) u->invalidateWidget(this);
      });
    }
  }

public:
  explicit ListViewBuilder(State<std::vector<T>> &state) : boundState(&state) {
    state.listen([this](const std::vector<T> &) {
      rebuildList();
      if (boundState && boundState->hasContext())
        if (auto *ui = boundState->getContext()) ui->partialRebuild(this);
    });
  }

  ~ListViewBuilder() override {
    stopFling();
    if (sb.isDragging) FluxUI::getCurrentInstance()->releaseMouseInput();
  }

  void setSelf(std::shared_ptr<ListViewBuilder<T>> ptr) { self = ptr; }

  // ── Fluent configuration ──────────────────────────────────────────────

  std::shared_ptr<ListViewBuilder<T>> itemBuilder(BuilderFn fn) {
    builderFn = std::move(fn);
    widgetCache.clear(); subscriptionHandles.clear(); lastKeys.clear();
    rebuildList(); return self;
  }
  std::shared_ptr<ListViewBuilder<T>> separator(std::function<WidgetPtr()> fn) {
    separatorBuilderFn = std::move(fn); lastKeys.clear(); rebuildList(); return self;
  }
  std::shared_ptr<ListViewBuilder<T>> setKeyFn(KeyFn fn) {
    keyFn = std::move(fn); widgetCache.clear(); subscriptionHandles.clear();
    lastKeys.clear(); return self;
  }
  std::shared_ptr<ListViewBuilder<T>> setSpacing(int s) { itemSpacing = s; return self; }
  std::shared_ptr<ListViewBuilder<T>> setHorizontal(bool h) {
    horizontal = h; sb.horizontal = h; sb.scrollOffset = 0;
    markNeedsLayout(); return self;
  }
  std::shared_ptr<ListViewBuilder<T>> setScrollbarSize(int s)       { sb.size        = s; return self; }
  std::shared_ptr<ListViewBuilder<T>> setScrollbarColor(Color c)    { sb.colorNormal = c; return self; }
  std::shared_ptr<ListViewBuilder<T>> setScrollbarHoverColor(Color c)  { sb.colorHover  = c; return self; }
  std::shared_ptr<ListViewBuilder<T>> setScrollbarActiveColor(Color c) { sb.colorActive = c; return self; }
  std::shared_ptr<ListViewBuilder<T>> setScrollbarTrackColor(Color c)  { sb.colorTrack  = c; return self; }

  // ── Layout ────────────────────────────────────────────────────────────

  void computeLayout(GraphicsContext &ctx, const BoxConstraints &constraints,
                     FontCache &fontCache) override {
    rebuildList();

    // Fill available space (Flutter ListView default)
    if (autoWidth  || width  > constraints.maxWidth)  width  = constraints.maxWidth;
    if (autoHeight || height > constraints.maxHeight) height = constraints.maxHeight;

    const int padH = paddingLeft + paddingRight;
    const int padV = paddingTop  + paddingBottom;

    auto measureChildren = [&](int crossAxisSpace) {
      int total = 0;
      if (sb.horizontal) {
        int availH = std::max(0, crossAxisSpace);
        for (size_t i = 0; i < children.size(); i++) {
          children[i]->computeLayout(
              ctx, BoxConstraints::loose(kUnbounded, availH), fontCache);
          total += children[i]->width;
          if (itemSpacing > 0 && i < children.size() - 1) total += itemSpacing;
        }
      } else {
        int availW = std::max(0, crossAxisSpace);
        for (size_t i = 0; i < children.size(); i++) {
          children[i]->computeLayout(
              ctx, BoxConstraints::loose(availW, kUnbounded), fontCache);
          total += children[i]->height;
          if (itemSpacing > 0 && i < children.size() - 1) total += itemSpacing;
        }
      }
      return total;
    };

    if (sb.horizontal) {
      int fullCross = height - padV;
      int total = measureChildren(fullCross);
      sb.viewportMain = std::max(0, width - padH);
      sb.contentMain  = total;
      bool needsBar = (total > sb.viewportMain);
      bool hadBar   = sb.isScrollable;
      sb.setScrollable(needsBar);
      if (needsBar && !hadBar) {
        measureChildren(std::max(0, fullCross - sb.size));
      }
    } else {
      int fullCross = width - padH;
      int total = measureChildren(fullCross);
      sb.viewportMain = std::max(0, height - padV);
      sb.contentMain  = total;
      bool needsBar = (total > sb.viewportMain);
      bool hadBar   = sb.isScrollable;
      sb.setScrollable(needsBar);
      if (needsBar && !hadBar) {
        measureChildren(std::max(0, fullCross - sb.size));
        total = 0;
        for (size_t i = 0; i < children.size(); i++) {
          total += children[i]->height;
          if (itemSpacing > 0 && i < children.size() - 1) total += itemSpacing;
        }
        sb.contentMain = total;
      }
    }

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
    stopFling();
    if (sb.onMouseDown(mx, my, x, y, width, height)) {
      if (sb.isDragging) FluxUI::getCurrentInstance()->captureMouseInput();
      repositionChildren(); markNeedsPaint(); return true;
    }
    if (sb.isScrollable && mx >= x && mx < x + width && my >= y && my < y + height) {
      gesture.horizontal = sb.horizontal;
      gesture.onDown(mx, my);
      FluxUI::getCurrentInstance()->captureMouseInput();
      return true;
    }
    return false;
  }
  bool handleMouseUp(int mx, int my) override {
    if (sb.isDragging) {
      sb.onMouseUp(); FluxUI::getCurrentInstance()->releaseMouseInput();
      markNeedsPaint(); return true;
    }
    if (gesture.isDragging) {
      gesture.onUp(mx, my); FluxUI::getCurrentInstance()->releaseMouseInput();
      if (gesture.isFling()) startFling();
      markNeedsPaint(); return true;
    }
    return false;
  }
  bool handleMouseMove(int mx, int my) override {
    if (sb.isDragging) {
      if (!sb.onMouseMove(mx, my, x, y, width, height)) return false;
      repositionChildren(); markNeedsPaint(); return true;
    }
    if (gesture.isDragging) {
      int delta = gesture.onMove(mx, my);
      if (delta != 0) {
        sb.scrollOffset += delta; sb.clamp(); sb.updateThumb();
        repositionChildren(); markNeedsPaint();
      }
      return true;
    }
    if (sb.onMouseMove(mx, my, x, y, width, height)) { markNeedsPaint(); return true; }
    return false;
  }
  bool handleMouseLeave() override {
    gesture.cancel();
    if (!sb.onMouseLeave()) return false;
    markNeedsPaint(); return true;
  }

  // ── Render ────────────────────────────────────────────────────────────

  void render(GraphicsContext &ctx, FontCache &fontCache) override {
    sb.updateThumb();
    Painter painter(ctx);

    int sbSz = sb.isScrollable ? sb.size : 0;
    int clipX1, clipY1, clipX2, clipY2;
    if (sb.horizontal) {
      clipX1 = x + paddingLeft;  clipY1 = y + paddingTop;
      clipX2 = x + width - paddingRight;
      clipY2 = y + height - paddingBottom - sbSz;
    } else {
      clipX1 = x + paddingLeft;  clipY1 = y + paddingTop;
      clipX2 = x + width  - paddingRight - sbSz;
      clipY2 = y + height - paddingBottom;
    }

    painter.pushClipRect(clipX1, clipY1, clipX2 - clipX1, clipY2 - clipY1);
    if (hasBackground) drawRoundedRectangle(ctx);

    for (auto &child : children) {
      bool vis = sb.horizontal
          ? (child->x + child->width  >= clipX1 && child->x < clipX2)
          : (child->y + child->height >= clipY1 && child->y < clipY2);
      if (vis) child->render(ctx, fontCache);
    }

    painter.popClipRect();
    sb.render(ctx, x, y, width, height);
    needsPaint = false;
  }
};

// ============================================================================
// SCROLLABLE GRIDVIEW — BUILDER  (State<vector<T>> binding)
// ============================================================================

template <typename T>
class GridViewBuilder : public Widget {
public:
  using KeyFn     = std::function<uintptr_t(const T &)>;
  using BuilderFn = std::function<WidgetPtr(int, const T &)>;

private:
  State<std::vector<T>> *boundState = nullptr;
  BuilderFn builderFn;

  int columnCount  = 2;
  int fixedCellWidth = -1;
  int spacingH = 0;
  int spacingV = 0;

  std::shared_ptr<GridViewBuilder<T>> self;
  ScrollbarState sb;   // always vertical
  GestureState   gesture;
  TimerID        flingTimer = 0;

  KeyFn keyFn;
  std::unordered_map<uintptr_t, WidgetPtr> widgetCache;
  std::vector<uintptr_t> lastKeys;
  std::unordered_map<uintptr_t, size_t> subscriptionHandles;

  // Cached layout data
  int _cols = 1;
  int _cellW = 0;
  std::vector<int> _rowHeights;

  // ── Key derivation / subscription ────────────────────────────────────

  uintptr_t deriveKey(const T &item, int index) const {
    if (keyFn) return keyFn(item);
    return defaultKey(item, index);
  }
  template <typename U>
  static uintptr_t defaultKey(const std::shared_ptr<U> &item, int) {
    return reinterpret_cast<uintptr_t>(item.get());
  }
  static uintptr_t defaultKey(const T &, int index, ...) {
    return static_cast<uintptr_t>(index);
  }

  template <typename U>
  void maybeSubscribe(const std::shared_ptr<ReactiveItem<U>> &ri,
                      uintptr_t key, int index) {
    auto it = subscriptionHandles.find(key);
    if (it != subscriptionHandles.end()) { ri->unlisten(it->second); subscriptionHandles.erase(it); }
    std::weak_ptr<GridViewBuilder<T>> weakSelf = self;
    size_t handle = ri->listen([weakSelf, key, index](const U &) {
      if (auto s = weakSelf.lock()) s->rebuildSingleCell(key, index);
    });
    subscriptionHandles[key] = handle;
  }
  template <typename U> void maybeSubscribe(const U &, uintptr_t, int) {}

  // ── Full rebuild ──────────────────────────────────────────────────────

  void rebuildList() {
    if (!boundState || !builderFn) return;
    const auto &items = boundState->get();

    std::vector<uintptr_t> newKeys;
    newKeys.reserve(items.size());
    for (int i = 0; i < (int)items.size(); i++)
      newKeys.push_back(deriveKey(items[i], i));

    if (newKeys == lastKeys && !children.empty()) return;

    std::unordered_map<uintptr_t, bool> newKeySet;
    for (auto k : newKeys) newKeySet[k] = true;
    std::vector<uintptr_t> evicted;
    for (auto &[k, _] : widgetCache)
      if (!newKeySet.count(k)) evicted.push_back(k);
    for (auto k : evicted) { subscriptionHandles.erase(k); widgetCache.erase(k); }

    children.clear();
    for (int i = 0; i < (int)items.size(); i++) {
      uintptr_t key = newKeys[i];
      WidgetPtr w;
      auto cacheIt = widgetCache.find(key);
      if (cacheIt != widgetCache.end()) { w = cacheIt->second; }
      else { w = builderFn(i, items[i]); widgetCache[key] = w; maybeSubscribe(items[i], key, i); }
      if (w) addChild(w);
    }
    lastKeys = newKeys;
    markNeedsLayout();
  }

  // ── Per-cell rebuild ──────────────────────────────────────────────────

  void rebuildSingleCell(uintptr_t key, int logicalIndex) {
    if (!boundState || !builderFn) return;
    const auto &items = boundState->get();
    if (logicalIndex < 0 || logicalIndex >= (int)items.size()) return;

    WidgetPtr newWidget = builderFn(logicalIndex, items[logicalIndex]);
    if (!newWidget) return;
    widgetCache[key] = newWidget;
    maybeSubscribe(items[logicalIndex], key, logicalIndex);

    if (logicalIndex < (int)children.size()) children[logicalIndex] = newWidget;

    if (boundState && boundState->hasContext())
      if (auto *ui = boundState->getContext()) { ui->partialRebuild(this); return; }
    markNeedsLayout(); markNeedsPaint();
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
        auto &c = children[idx];
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
    if (fixedCellWidth > 0)
      return std::max(1, (contentWidth + spacingH) / (fixedCellWidth + spacingH));
    return std::max(1, columnCount);
  }

  void stopFling() {
    if (flingTimer) {
      if (auto *ui = FluxUI::getCurrentInstance()) ui->clearInterval(flingTimer);
      flingTimer = 0;
    }
  }
  void startFling() {
    stopFling();
    if (auto *ui = FluxUI::getCurrentInstance()) {
      flingTimer = ui->setInterval(16, [this]() {
        int delta = gesture.tickFling();
        if (delta == 0) { stopFling(); return; }
        sb.scrollOffset += delta; sb.clamp(); sb.updateThumb();
        repositionChildren(); markNeedsPaint();
        if (auto *u = FluxUI::getCurrentInstance()) u->invalidateWidget(this);
      });
    }
  }

public:
  explicit GridViewBuilder(State<std::vector<T>> &state) : boundState(&state) {
    state.listen([this](const std::vector<T> &) {
      rebuildList();
      if (boundState && boundState->hasContext())
        if (auto *ui = boundState->getContext()) ui->partialRebuild(this);
    });
  }

  ~GridViewBuilder() override {
    stopFling();
    if (sb.isDragging) FluxUI::getCurrentInstance()->releaseMouseInput();
  }

  void setSelf(std::shared_ptr<GridViewBuilder<T>> ptr) { self = ptr; }

  // ── Fluent configuration ──────────────────────────────────────────────

  std::shared_ptr<GridViewBuilder<T>> itemBuilder(BuilderFn fn) {
    builderFn = std::move(fn);
    widgetCache.clear(); subscriptionHandles.clear(); lastKeys.clear();
    rebuildList(); return self;
  }
  std::shared_ptr<GridViewBuilder<T>> setKeyFn(KeyFn fn) {
    keyFn = std::move(fn); widgetCache.clear(); subscriptionHandles.clear();
    lastKeys.clear(); return self;
  }
  std::shared_ptr<GridViewBuilder<T>> columns(int c)    { columnCount = c; fixedCellWidth = -1; markNeedsLayout(); return self; }
  std::shared_ptr<GridViewBuilder<T>> columnWidth(int w){ fixedCellWidth = w; markNeedsLayout(); return self; }
  std::shared_ptr<GridViewBuilder<T>> setSpacingH(int s){ spacingH = s; markNeedsLayout(); return self; }
  std::shared_ptr<GridViewBuilder<T>> setSpacingV(int s){ spacingV = s; markNeedsLayout(); return self; }
  std::shared_ptr<GridViewBuilder<T>> setSpacing(int s) { spacingH = spacingV = s; markNeedsLayout(); return self; }
  std::shared_ptr<GridViewBuilder<T>> setScrollbarColor(Color c)    { sb.colorNormal = c; return self; }
  std::shared_ptr<GridViewBuilder<T>> setScrollbarHoverColor(Color c)  { sb.colorHover  = c; return self; }
  std::shared_ptr<GridViewBuilder<T>> setScrollbarActiveColor(Color c) { sb.colorActive = c; return self; }
  std::shared_ptr<GridViewBuilder<T>> setScrollbarTrackColor(Color c)  { sb.colorTrack  = c; return self; }
  std::shared_ptr<GridViewBuilder<T>> setScrollbarWidth(int w)      { sb.size = w; return self; }

  // ── Layout ────────────────────────────────────────────────────────────
  //
  // Two-pass scrollbar — identical logic to ListViewBuilder/Static, but
  // always vertical.  We measure the full grid without the scrollbar first,
  // then re-measure only the cell WIDTH (cross axis) if the bar appeared.

  void computeLayout(GraphicsContext &ctx, const BoxConstraints &constraints,
                     FontCache &fontCache) override {
    rebuildList();

    // Fill available space
    if (autoHeight || height > constraints.maxHeight) height = constraints.maxHeight;
    if (autoWidth  || width  > constraints.maxWidth)  width  = constraints.maxWidth;

    const int padH = paddingLeft + paddingRight;
    const int padV = paddingTop  + paddingBottom;

    sb.viewportMain = std::max(0, height - padV);

    // Helper: measure all cells into a grid of given contentWidth.
    // Returns total content height and writes _cols, _cellW, _rowHeights.
    auto measureGrid = [&](int contentW) -> int {
      contentW = std::max(0, contentW);
      int cols  = resolvedColumnCount(contentW);
      int cellW = (cols > 1) ? (contentW - spacingH * (cols - 1)) / cols : contentW;
      cellW     = std::max(0, cellW);

      int rows  = (cols > 0) ? ((int)children.size() + cols - 1) / cols : 0;
      std::vector<int> rowHeights(rows, 0);

      for (int i = 0; i < (int)children.size(); i++) {
        int row = i / cols;
        children[i]->computeLayout(
            ctx, BoxConstraints::loose(cellW, kUnbounded), fontCache);
        rowHeights[row] = std::max(rowHeights[row], children[i]->height);
      }

      int total = 0;
      for (int r = 0; r < rows; r++) {
        total += rowHeights[r];
        if (r < rows - 1) total += spacingV;
      }

      _cols       = cols;
      _cellW      = cellW;
      _rowHeights = rowHeights;
      return total;
    };

    // Pass A — full width, no scrollbar
    int fullContentW = width - padH;
    int total = measureGrid(fullContentW);
    sb.contentMain = total;

    bool needsBar = (total > sb.viewportMain);
    bool hadBar   = sb.isScrollable;
    sb.setScrollable(needsBar);

    // Pass B — reduced width if scrollbar just appeared
    if (needsBar && !hadBar) {
      int reducedW = std::max(0, fullContentW - sb.size);
      total = measureGrid(reducedW);
      sb.contentMain = total;
    }

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
    stopFling();
    if (sb.onMouseDown(mx, my, x, y, width, height)) {
      if (sb.isDragging) FluxUI::getCurrentInstance()->captureMouseInput();
      repositionChildren(); markNeedsPaint(); return true;
    }
    if (sb.isScrollable && mx >= x && mx < x + width && my >= y && my < y + height) {
      gesture.horizontal = false;
      gesture.onDown(mx, my);
      FluxUI::getCurrentInstance()->captureMouseInput();
      return true;
    }
    return false;
  }
  bool handleMouseUp(int mx, int my) override {
    if (sb.isDragging) {
      sb.onMouseUp(); FluxUI::getCurrentInstance()->releaseMouseInput();
      markNeedsPaint(); return true;
    }
    if (gesture.isDragging) {
      gesture.onUp(mx, my); FluxUI::getCurrentInstance()->releaseMouseInput();
      if (gesture.isFling()) startFling();
      markNeedsPaint(); return true;
    }
    return false;
  }
  bool handleMouseMove(int mx, int my) override {
    if (sb.isDragging) {
      if (!sb.onMouseMove(mx, my, x, y, width, height)) return false;
      repositionChildren(); markNeedsPaint(); return true;
    }
    if (gesture.isDragging) {
      int delta = gesture.onMove(mx, my);
      if (delta != 0) {
        sb.scrollOffset += delta; sb.clamp(); sb.updateThumb();
        repositionChildren(); markNeedsPaint();
      }
      return true;
    }
    if (sb.onMouseMove(mx, my, x, y, width, height)) { markNeedsPaint(); return true; }
    return false;
  }
  bool handleMouseLeave() override {
    gesture.cancel();
    if (!sb.onMouseLeave()) return false;
    markNeedsPaint(); return true;
  }

  // ── Render ────────────────────────────────────────────────────────────

  void render(GraphicsContext &ctx, FontCache &fontCache) override {
    sb.updateThumb();
    Painter painter(ctx);

    int sbW    = sb.isScrollable ? sb.size : 0;
    int clipX1 = x + paddingLeft;
    int clipY1 = y + paddingTop;
    int clipX2 = x + width  - paddingRight - sbW;
    int clipY2 = y + height - paddingBottom;

    painter.pushClipRect(clipX1, clipY1, clipX2 - clipX1, clipY2 - clipY1);
    if (hasBackground) drawRoundedRectangle(ctx);

    for (auto &child : children)
      if (child->y + child->height >= clipY1 && child->y < clipY2)
        child->render(ctx, fontCache);

    painter.popClipRect();
    sb.render(ctx, x, y, width, height);
    needsPaint = false;
  }
};

// ============================================================================
// GRID WIDGET  (static, non-scrollable)
// ============================================================================

class GridWidget : public Widget {
public:
  int columnCount    = 2;
  int fixedCellWidth = -1;
  int spacingH = 0;
  int spacingV = 0;

  void computeLayout(GraphicsContext &ctx, const BoxConstraints &constraints,
                     FontCache &fontCache) override {
    if (children.empty()) {
      if (autoWidth)  width  = paddingLeft + paddingRight;
      if (autoHeight) height = paddingTop  + paddingBottom;
      applyConstraints(); needsLayout = false; return;
    }

    int contentWidth = std::max(0, constraints.maxWidth - paddingLeft - paddingRight);
    int cols  = std::max(1, resolvedColumnCount(contentWidth));
    int cellW = std::max(0, (contentWidth - spacingH * (cols - 1)) / cols);
    int rows  = ((int)children.size() + cols - 1) / cols;
    std::vector<int> rowHeights(rows, 0);

    for (int i = 0; i < (int)children.size(); i++) {
      int row    = i / cols;
      int childW = (crossAxisAlignment == CrossAxisAlignment::Stretch)
                 ? cellW : std::min(cellW, contentWidth);
      children[i]->computeLayout(
          ctx, BoxConstraints::loose(std::max(0, childW), constraints.maxHeight),
          fontCache);
      rowHeights[row] = std::max(rowHeights[row], children[i]->height);
    }

    int totalH = 0;
    for (int r = 0; r < rows; r++) {
      totalH += rowHeights[r];
      if (r < rows - 1) totalH += spacingV;
    }

    if (autoWidth)  width  = constraints.maxWidth;
    if (autoHeight) height = totalH + paddingTop + paddingBottom;

    applyConstraints();
    needsLayout  = false;
    _cols        = cols;
    _cellW       = cellW;
    _rowHeights  = rowHeights;
  }

  void positionChildren(int contentX, int contentY,
                        int contentWidth, int /*contentHeight*/) override {
    if (children.empty() || _cols == 0) return;

    int totalGridW = _cellW * _cols + spacingH * (_cols - 1);
    int startX = contentX;
    if (mainAxisAlignment == MainAxisAlignment::Center)
      startX += (contentWidth - totalGridW) / 2;
    else if (mainAxisAlignment == MainAxisAlignment::End)
      startX += contentWidth - totalGridW;

    int curY = contentY;
    int rows = (int)_rowHeights.size();

    for (int row = 0; row < rows; row++) {
      for (int col = 0; col < _cols; col++) {
        int idx = row * _cols + col;
        if (idx >= (int)children.size()) break;
        auto &c = children[idx];

        int cellX  = startX + col * (_cellW + spacingH);
        int childX = cellX;
        if (crossAxisAlignment == CrossAxisAlignment::Center)
          childX = cellX + (_cellW - c->width) / 2;
        else if (crossAxisAlignment == CrossAxisAlignment::End)
          childX = cellX + _cellW - c->width;
        else if (crossAxisAlignment == CrossAxisAlignment::Stretch)
          c->width = _cellW;

        c->x = childX; c->y = curY;
        c->positionChildren(c->x + c->paddingLeft, c->y + c->paddingTop,
                            c->width  - c->paddingLeft - c->paddingRight,
                            c->height - c->paddingTop  - c->paddingBottom);
      }
      curY += _rowHeights[row] + (row < rows - 1 ? spacingV : 0);
    }
  }

  void render(GraphicsContext &ctx, FontCache &fontCache) override {
    if (hasBackground) drawRoundedRectangle(ctx);
    for (auto &c : children) c->render(ctx, fontCache);
    needsPaint = false;
  }

  // ── Fluent setters ────────────────────────────────────────────────────

  std::shared_ptr<GridWidget> setColumnCount(int c)  { columnCount = c; fixedCellWidth = -1; markNeedsLayout(); return self(); }
  std::shared_ptr<GridWidget> setColumnWidth(int w)  { fixedCellWidth = w; markNeedsLayout(); return self(); }
  std::shared_ptr<GridWidget> setSpacing(int s)      { spacingH = spacingV = s; markNeedsLayout(); return self(); }
  std::shared_ptr<GridWidget> setSpacingH(int s)     { spacingH = s; markNeedsLayout(); return self(); }
  std::shared_ptr<GridWidget> setSpacingV(int s)     { spacingV = s; markNeedsLayout(); return self(); }
  std::shared_ptr<GridWidget> setCrossAxisAlignment(CrossAxisAlignment a) { crossAxisAlignment = a; markNeedsLayout(); return self(); }
  std::shared_ptr<GridWidget> setMainAxisAlignment(MainAxisAlignment a)   { mainAxisAlignment  = a; markNeedsLayout(); return self(); }
  std::shared_ptr<GridWidget> setPadding(int p)      { paddingLeft = paddingRight = paddingTop = paddingBottom = p; markNeedsLayout(); return self(); }
  std::shared_ptr<GridWidget> setPaddingAll(int l, int t, int r, int b) { paddingLeft=l; paddingTop=t; paddingRight=r; paddingBottom=b; markNeedsLayout(); return self(); }
  std::shared_ptr<GridWidget> setWidth(int w)        { width = w; autoWidth = false; markNeedsLayout(); return self(); }
  std::shared_ptr<GridWidget> setHeight(int h)       { height = h; autoHeight = false; markNeedsLayout(); return self(); }
  std::shared_ptr<GridWidget> setBackgroundColor(Color c) { backgroundColor = c; hasBackground = true; markNeedsPaint(); return self(); }
  std::shared_ptr<GridWidget> setFlex(int f)         { flex = f; markNeedsLayout(); return self(); }

private:
  int _cols = 0, _cellW = 0;
  std::vector<int> _rowHeights;

  int resolvedColumnCount(int contentWidth) const {
    if (fixedCellWidth > 0)
      return std::max(1, (contentWidth + spacingH) / (fixedCellWidth + spacingH));
    return columnCount;
  }
  std::shared_ptr<GridWidget> self() {
    return std::static_pointer_cast<GridWidget>(shared_from_this());
  }
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

inline GridWidgetPtr GridFromList(int columns, const std::vector<WidgetPtr> &items) {
  auto w = std::make_shared<GridWidget>();
  w->columnCount = columns;
  for (auto &item : items) w->addChild(item);
  return w;
}

inline GridWidgetPtr GridFixedWidthFromList(int cellWidth, const std::vector<WidgetPtr> &items) {
  auto w = std::make_shared<GridWidget>();
  w->fixedCellWidth = cellWidth;
  for (auto &item : items) w->addChild(item);
  return w;
}

template <typename T>
inline std::shared_ptr<ListViewBuilder<T>>
ListView(State<std::vector<T>> &state) {
  auto w = std::make_shared<ListViewBuilder<T>>(state);
  w->setSelf(w);
  return w;
}

template <typename T>
inline std::shared_ptr<GridViewBuilder<T>>
GridView(State<std::vector<T>> &state) {
  auto w = std::make_shared<GridViewBuilder<T>>(state);
  w->setSelf(w);
  return w;
}

#endif // FLUX_COLLECTION_HPP