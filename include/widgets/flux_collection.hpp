#ifndef FLUX_COLLECTION_HPP
#define FLUX_COLLECTION_HPP

#include "flux_core.hpp"
#include "flux_state.hpp"

#include <functional>
#include <vector>

// ============================================================================
// SCROLLABLE LISTVIEW BUILDER WIDGET
// ============================================================================
//
// Vertical (default):   ListView(state)->itemBuilder(...)
// Horizontal:           ListView(state)->setHorizontal(true)->itemBuilder(...)
//
// Scrollbar appears on the right  (vertical mode)
//                   or the bottom (horizontal mode)

template <typename T> class ListViewBuilder : public Widget {
private:
  State<std::vector<T>> *boundState = nullptr;
  std::function<WidgetPtr(int, const T &)> builder;
  std::function<WidgetPtr()> separatorBuilder;
  int itemSpacing = 0;
  int lastItemCount = 0;
  bool horizontal = false; // ← the one flag that changes everything
  std::shared_ptr<ListViewBuilder<T>> self;

  // ── Scroll state ──────────────────────────────────────────────────────
  int scrollOffset = 0;
  int contentMain = 0;  // total length along the scroll axis
  int viewportMain = 0; // visible length along the scroll axis
  bool isScrollable = false;

  // ── Scrollbar ─────────────────────────────────────────────────────────
  int scrollbarSize = 8; // width (vertical) or height (horizontal)
  int scrollbarThumbLength = 0;
  int scrollbarThumbOffset = 0;
  bool isDraggingScrollbar = false;
  int dragStartPos = 0;
  int dragStartOffset = 0;
  bool isHoveringScrollbar = false;

  COLORREF scrollbarColor = RGB(180, 180, 180);
  COLORREF scrollbarHoverColor = RGB(140, 140, 140);
  COLORREF scrollbarActiveColor = RGB(100, 100, 100);
  COLORREF scrollbarTrackColor = RGB(245, 245, 245);

  // ── Helpers ───────────────────────────────────────────────────────────

  void rebuildList() {
    if (!boundState || !builder)
      return;

    const auto &items = boundState->get();
    if ((int)items.size() == lastItemCount && !children.empty())
      return;

    lastItemCount = (int)items.size();
    children.clear();

    for (size_t i = 0; i < items.size(); i++) {
      auto w = builder((int)i, items[i]);
      if (w)
        addChild(w);

      if (separatorBuilder && i < items.size() - 1) {
        auto sep = separatorBuilder();
        if (sep)
          addChild(sep);
      }
    }
    markNeedsLayout();
  }

  void clampScrollOffset() {
    int maxScroll = max(0, contentMain - viewportMain);
    scrollOffset = max(0, min(scrollOffset, maxScroll));
  }

  void updateScrollbar() {
    if (!isScrollable) {
      scrollbarThumbLength = scrollbarThumbOffset = 0;
      return;
    }

    float visRatio = (float)viewportMain / (float)contentMain;
    scrollbarThumbLength = max(30, (int)(viewportMain * visRatio));

    float scrollRatio =
        (float)scrollOffset / (float)(contentMain - viewportMain);
    scrollbarThumbOffset =
        (int)(scrollRatio * (viewportMain - scrollbarThumbLength));
  }

  void releaseDragCapture() {
    if (isDraggingScrollbar) {
      isDraggingScrollbar = false;
      isHoveringScrollbar = false;
      ReleaseCapture();
    }
  }

  bool isMouseOverThumb(int mx, int my) const {
    if (!isScrollable)
      return false;
    if (horizontal) {
      int sbY = y + height - scrollbarSize;
      return mx >= x + scrollbarThumbOffset &&
             mx < x + scrollbarThumbOffset + scrollbarThumbLength &&
             my >= sbY && my < y + height;
    } else {
      int sbX = x + width - scrollbarSize;
      return mx >= sbX && mx < x + width && my >= y + scrollbarThumbOffset &&
             my < y + scrollbarThumbOffset + scrollbarThumbLength;
    }
  }

  void renderScrollbar(HDC hdc) {
    COLORREF thumbColor = isDraggingScrollbar   ? scrollbarActiveColor
                          : isHoveringScrollbar ? scrollbarHoverColor
                                                : scrollbarColor;
    RECT trackRect, thumbRect;

    if (horizontal) {
      int sbY = y + height - scrollbarSize;
      trackRect = {x, sbY, x + width, y + height};
      thumbRect = {x + scrollbarThumbOffset, sbY,
                   x + scrollbarThumbOffset + scrollbarThumbLength, y + height};
    } else {
      int sbX = x + width - scrollbarSize;
      trackRect = {sbX, y, x + width, y + height};
      thumbRect = {sbX, y + scrollbarThumbOffset, x + width,
                   y + scrollbarThumbOffset + scrollbarThumbLength};
    }

    HBRUSH br = CreateSolidBrush(scrollbarTrackColor);
    FillRect(hdc, &trackRect, br);
    DeleteObject(br);

    br = CreateSolidBrush(thumbColor);
    FillRect(hdc, &thumbRect, br);
    DeleteObject(br);
  }

  void repositionChildren() {
    int sbSize = isScrollable ? scrollbarSize : 0;

    if (horizontal) {
      int curX = x + paddingLeft - scrollOffset;
      int contentY = y + paddingTop;
      int availH = height - paddingTop - paddingBottom - sbSize;

      for (size_t i = 0; i < children.size(); i++) {
        auto &child = children[i];
        child->x = curX;
        child->y = contentY;
        child->positionChildren(
            child->x + child->paddingLeft, child->y + child->paddingTop,
            child->width - child->paddingLeft - child->paddingRight,
            child->height - child->paddingTop - child->paddingBottom);
        curX += child->width;
        if (itemSpacing > 0 && i < children.size() - 1)
          curX += itemSpacing;
      }
    } else {
      int curY = y + paddingTop - scrollOffset;
      int contentX = x + paddingLeft;

      for (size_t i = 0; i < children.size(); i++) {
        auto &child = children[i];
        child->x = contentX;
        child->y = curY;
        child->positionChildren(
            child->x + child->paddingLeft, child->y + child->paddingTop,
            child->width - child->paddingLeft - child->paddingRight,
            child->height - child->paddingTop - child->paddingBottom);
        curY += child->height;
        if (itemSpacing > 0 && i < children.size() - 1)
          curY += itemSpacing;
      }
    }
  }

public:
  explicit ListViewBuilder(State<std::vector<T>> &state) : boundState(&state) {
    lastItemCount = (int)state.get().size();

    state.listen([this](const std::vector<T> &) {
      rebuildList();
      if (boundState && boundState->hasContext())
        if (auto *ui = boundState->getContext())
          ui->partialRebuild(this);
    });
  }

  ~ListViewBuilder() override {
    if (isDraggingScrollbar)
      ReleaseCapture();
  }

  void setSelf(std::shared_ptr<ListViewBuilder<T>> ptr) { self = ptr; }

  // ── Configuration ─────────────────────────────────────────────────────

  std::shared_ptr<ListViewBuilder<T>>
  itemBuilder(std::function<WidgetPtr(int, const T &)> fn) {
    builder = fn;
    rebuildList();
    return self;
  }

  std::shared_ptr<ListViewBuilder<T>> separator(std::function<WidgetPtr()> fn) {
    separatorBuilder = fn;
    return self;
  }

  std::shared_ptr<ListViewBuilder<T>> setSpacing(int s) {
    itemSpacing = s;
    return self;
  }

  std::shared_ptr<ListViewBuilder<T>> setHorizontal(bool h) {
    horizontal = h;
    scrollOffset = 0; // reset scroll when direction switches
    markNeedsLayout();
    return self;
  }

  std::shared_ptr<ListViewBuilder<T>> setScrollbarSize(int s) {
    scrollbarSize = s;
    return self;
  }
  std::shared_ptr<ListViewBuilder<T>> setScrollbarColor(COLORREF c) {
    scrollbarColor = c;
    return self;
  }
  std::shared_ptr<ListViewBuilder<T>> setScrollbarHoverColor(COLORREF c) {
    scrollbarHoverColor = c;
    return self;
  }
  std::shared_ptr<ListViewBuilder<T>> setScrollbarActiveColor(COLORREF c) {
    scrollbarActiveColor = c;
    return self;
  }
  std::shared_ptr<ListViewBuilder<T>> setScrollbarTrackColor(COLORREF c) {
    scrollbarTrackColor = c;
    return self;
  }

  // ── Layout ────────────────────────────────────────────────────────────

void computeLayout(HDC hdc, const BoxConstraints &constraints,
                   FontCache &fontCache) override {
  rebuildList();

  if (autoWidth  || width  > constraints.maxWidth)  width  = constraints.maxWidth;
  if (autoHeight || height > constraints.maxHeight) height = constraints.maxHeight;

  int sbSize = isScrollable ? scrollbarSize : 0;

  if (horizontal) {
    viewportMain  = width - paddingLeft - paddingRight;
    int availH    = height - paddingTop - paddingBottom - sbSize;
    int total     = 0;

    for (size_t i = 0; i < children.size(); i++) {
      children[i]->computeLayout(hdc, BoxConstraints::loose(constraints.maxWidth, availH),
                                  fontCache);
      total += children[i]->width;
      if (itemSpacing > 0 && i < children.size() - 1)
        total += itemSpacing;
    }
    contentMain = total;
  } else {
    viewportMain  = height - paddingTop - paddingBottom;
    int availW    = width - paddingLeft - paddingRight - sbSize;
    int total     = 0;

    for (size_t i = 0; i < children.size(); i++) {
      children[i]->computeLayout(hdc, BoxConstraints::loose(availW, constraints.maxHeight),
                                  fontCache);
      total += children[i]->height;
      if (itemSpacing > 0 && i < children.size() - 1)
        total += itemSpacing;
    }
    contentMain = total;
  }

  bool wasScrollable = isScrollable;
  isScrollable = (contentMain > viewportMain);

  if (wasScrollable && !isScrollable) {
    scrollOffset = 0;
    releaseDragCapture();
    isHoveringScrollbar = false;
  } else if (!wasScrollable && isScrollable) {
    clampScrollOffset();
  }

  updateScrollbar();
  applyConstraints();
  needsLayout = false;
}

  void positionChildren(int, int, int, int) override { repositionChildren(); }

  // ── Mouse events ──────────────────────────────────────────────────────

  bool handleMouseWheel(int delta) override {
    if (!isScrollable)
      return false;
    scrollOffset -= (delta / WHEEL_DELTA) * 40;
    clampScrollOffset();
    updateScrollbar();
    repositionChildren();
    markNeedsPaint();
    return true;
  }

  bool handleMouseDown(int mx, int my) override {
    if (!isScrollable)
      return false;

    // Check if click is inside the scrollbar strip
    bool inScrollbarStrip =
        horizontal ? (my >= y + height - scrollbarSize && my < y + height)
                   : (mx >= x + width - scrollbarSize && mx < x + width);

    if (!inScrollbarStrip)
      return false;

    int pos = horizontal ? mx - x : my - y;

    if (pos >= scrollbarThumbOffset &&
        pos < scrollbarThumbOffset + scrollbarThumbLength) {
      // Drag thumb
      isDraggingScrollbar = true;
      dragStartPos = horizontal ? mx : my;
      dragStartOffset = scrollOffset;

      if (boundState && boundState->hasContext())
        if (auto *ui = boundState->getContext())
          if (ui->getWindow())
            SetCapture(ui->getWindow());
      markNeedsPaint();
      return true;
    }

    // Click on track — jump
    float ratio = (float)pos / (float)viewportMain;
    scrollOffset = (int)(ratio * (contentMain - viewportMain));
    clampScrollOffset();
    updateScrollbar();
    repositionChildren();
    markNeedsPaint();
    return true;
  }

  bool handleMouseUp(int mx, int my) override {
    if (isDraggingScrollbar) {
      releaseDragCapture();
      markNeedsPaint();
      return true;
    }
    return false;
  }

  bool handleMouseMove(int mx, int my) override {
    if (isDraggingScrollbar) {
      if (!isScrollable) {
        releaseDragCapture();
        markNeedsPaint();
        return true;
      }

      int curPos = horizontal ? mx : my;
      int delta = curPos - dragStartPos;
      float ratio = (float)delta / (float)(viewportMain - scrollbarThumbLength);
      scrollOffset =
          dragStartOffset + (int)(ratio * (contentMain - viewportMain));
      clampScrollOffset();
      updateScrollbar();
      repositionChildren();
      markNeedsPaint();
      return true;
    }

    bool wasHovering = isHoveringScrollbar;
    isHoveringScrollbar = isScrollable && isMouseOverThumb(mx, my);
    if (wasHovering != isHoveringScrollbar) {
      markNeedsPaint();
      return true;
    }
    return false;
  }

  bool handleMouseLeave() override {
    bool changed = isHoveringScrollbar;
    isHoveringScrollbar = false;
    if (changed) {
      markNeedsPaint();
      return true;
    }
    return false;
  }

  // ── Render ────────────────────────────────────────────────────────────

  void render(HDC hdc, FontCache &fontCache) override {
    updateScrollbar();

    int sbSize = isScrollable ? scrollbarSize : 0;

    // Clip to viewport
    RECT clipRect;
    if (horizontal) {
      clipRect = {x + paddingLeft, y + paddingTop, x + width - paddingRight,
                  y + height - paddingBottom - sbSize};
    } else {
      clipRect = {x + paddingLeft, y + paddingTop,
                  x + width - paddingRight - sbSize,
                  y + height - paddingBottom};
    }

    HRGN clip = CreateRectRgn(clipRect.left, clipRect.top, clipRect.right,
                              clipRect.bottom);
    SelectClipRgn(hdc, clip);

    if (hasBackground)
      drawRoundedRectangle(hdc);

    for (auto &child : children) {
      bool visible = horizontal ? (child->x + child->width >= clipRect.left &&
                                   child->x < clipRect.right)
                                : (child->y + child->height >= clipRect.top &&
                                   child->y < clipRect.bottom);
      if (visible)
        child->render(hdc, fontCache);
    }

    SelectClipRgn(hdc, NULL);
    DeleteObject(clip);

    if (isScrollable)
      renderScrollbar(hdc);

    needsPaint = false;
  }
};

// ============================================================================
// SCROLLABLE GRIDVIEW BUILDER WIDGET
// ============================================================================
//
// Mirrors ListViewBuilder exactly — same State binding, same itemBuilder
// pattern, same scrollbar implementation — but lays items out in a grid.
//
// Two sizing modes:
//   Fixed column count  — GridView(state)->columns(3)
//   Fixed cell width    — GridView(state)->columnWidth(200)  (responsive)
//
// Usage:
//   GridView(myState)
//       ->columns(3)
//       ->itemBuilder([](int i, const MyItem &item) -> WidgetPtr {
//           return Card(Text(item.name));
//       })
//       ->setSpacingH(12)
//       ->setSpacingV(12);

template <typename T> class GridViewBuilder : public Widget {
private:
  State<std::vector<T>> *boundState = nullptr;
  std::function<WidgetPtr(int, const T &)> builder;

  int columnCount = 2;     // fixed col count mode
  int fixedCellWidth = -1; // >0 = responsive mode
  int spacingH = 0;
  int spacingV = 0;
  int lastItemCount = -1;

  std::shared_ptr<GridViewBuilder<T>> self;

  // ── Scroll state (identical to ListViewBuilder) ───────────────────────
  int scrollOffset = 0;
  int contentHeight = 0;
  int viewportHeight = 0;
  bool isScrollable = false;

  int scrollbarWidth = 8;
  int scrollbarThumbHeight = 0;
  int scrollbarThumbY = 0;
  bool isDraggingScrollbar = false;
  int dragStartY = 0;
  int dragStartOffset = 0;
  bool isHoveringScrollbar = false;

  COLORREF scrollbarColor = RGB(180, 180, 180);
  COLORREF scrollbarHoverColor = RGB(140, 140, 140);
  COLORREF scrollbarActiveColor = RGB(100, 100, 100);
  COLORREF scrollbarTrackColor = RGB(245, 245, 245);

  // ── Cached layout data ────────────────────────────────────────────────
  int _cols = 1;
  int _cellW = 0;
  std::vector<int> _rowHeights;

  // ── Internal helpers ──────────────────────────────────────────────────

  void rebuildList() {
    if (!boundState || !builder)
      return;

    const auto &items = boundState->get();
    if ((int)items.size() == lastItemCount && !children.empty())
      return;

    lastItemCount = (int)items.size();
    children.clear();

    for (int i = 0; i < (int)items.size(); i++)
      addChild(builder(i, items[i]));

    markNeedsLayout();
  }

  int resolvedColumnCount(int contentWidth) const {
    if (fixedCellWidth > 0) {
      int cols = (contentWidth + spacingH) / (fixedCellWidth + spacingH);
      return max(1, cols);
    }
    return max(1, columnCount);
  }

  void clampScrollOffset() {
    int maxScroll = max(0, contentHeight - viewportHeight);
    scrollOffset = max(0, min(scrollOffset, maxScroll));
  }

  void updateScrollbar() {
    if (!isScrollable) {
      scrollbarThumbHeight = scrollbarThumbY = 0;
      return;
    }
    float visRatio = (float)viewportHeight / (float)contentHeight;
    scrollbarThumbHeight = max(30, (int)(viewportHeight * visRatio));

    float scrollRatio =
        (float)scrollOffset / (float)(contentHeight - viewportHeight);
    scrollbarThumbY =
        (int)(scrollRatio * (viewportHeight - scrollbarThumbHeight));
  }

  void releaseDragCapture() {
    if (isDraggingScrollbar) {
      isDraggingScrollbar = false;
      isHoveringScrollbar = false;
      ReleaseCapture();
    }
  }

  bool isMouseOverScrollbarThumb(int mx, int my) const {
    if (!isScrollable)
      return false;
    int sx = x + width - scrollbarWidth;
    int top = y + scrollbarThumbY;
    return mx >= sx && mx < x + width && my >= top &&
           my < top + scrollbarThumbHeight;
  }

  void renderScrollbar(HDC hdc) {
    // Track
    HBRUSH trackBrush = CreateSolidBrush(scrollbarTrackColor);
    RECT trackRect = {x + width - scrollbarWidth, y, x + width, y + height};
    FillRect(hdc, &trackRect, trackBrush);
    DeleteObject(trackBrush);

    // Thumb
    COLORREF thumbColor = isDraggingScrollbar   ? scrollbarActiveColor
                          : isHoveringScrollbar ? scrollbarHoverColor
                                                : scrollbarColor;
    HBRUSH thumbBrush = CreateSolidBrush(thumbColor);
    RECT thumbRect = {x + width - scrollbarWidth, y + scrollbarThumbY,
                      x + width, y + scrollbarThumbY + scrollbarThumbHeight};
    FillRect(hdc, &thumbRect, thumbBrush);
    DeleteObject(thumbBrush);
  }

  // Reposition all children using cached _cols / _cellW / _rowHeights
  void repositionChildren() {
    if (children.empty() || _cols == 0)
      return;

    int contentX = x + paddingLeft;
    int cols = _cols;
    int cellW = _cellW;
    int rows = (int)_rowHeights.size();
    int curY = y + paddingTop - scrollOffset;

    for (int row = 0; row < rows; row++) {
      int rowH = _rowHeights[row];

      for (int col = 0; col < cols; col++) {
        int idx = row * cols + col;
        if (idx >= (int)children.size())
          break;

        auto &child = children[idx];
        int cellX = contentX + col * (cellW + spacingH);

        child->x = cellX;
        child->y = curY;

        child->positionChildren(
            child->x + child->paddingLeft, child->y + child->paddingTop,
            child->width - child->paddingLeft - child->paddingRight,
            child->height - child->paddingTop - child->paddingBottom);
      }
      curY += rowH + (row < rows - 1 ? spacingV : 0);
    }
  }

public:
  explicit GridViewBuilder(State<std::vector<T>> &state) : boundState(&state) {
    lastItemCount = (int)state.get().size();

    state.listen([this](const std::vector<T> &) {
      rebuildList();
      if (boundState && boundState->hasContext()) {
        if (auto *ui = boundState->getContext())
          ui->partialRebuild(this);
      }
    });
  }

  ~GridViewBuilder() override {
    if (isDraggingScrollbar)
      ReleaseCapture();
  }

  void setSelf(std::shared_ptr<GridViewBuilder<T>> ptr) { self = ptr; }

  // ── Fluent configuration ──────────────────────────────────────────────

  std::shared_ptr<GridViewBuilder<T>>
  itemBuilder(std::function<WidgetPtr(int, const T &)> fn) {
    builder = fn;
    rebuildList();
    return self;
  }

  std::shared_ptr<GridViewBuilder<T>> columns(int c) {
    columnCount = c;
    fixedCellWidth = -1;
    markNeedsLayout();
    return self;
  }

  // Switch to responsive mode — column count derived from available width
  std::shared_ptr<GridViewBuilder<T>> columnWidth(int w) {
    fixedCellWidth = w;
    markNeedsLayout();
    return self;
  }

  std::shared_ptr<GridViewBuilder<T>> setSpacingH(int s) {
    spacingH = s;
    markNeedsLayout();
    return self;
  }
  std::shared_ptr<GridViewBuilder<T>> setSpacingV(int s) {
    spacingV = s;
    markNeedsLayout();
    return self;
  }
  std::shared_ptr<GridViewBuilder<T>> setSpacing(int s) {
    spacingH = spacingV = s;
    markNeedsLayout();
    return self;
  }

  std::shared_ptr<GridViewBuilder<T>> setScrollbarColor(COLORREF c) {
    scrollbarColor = c;
    return self;
  }
  std::shared_ptr<GridViewBuilder<T>> setScrollbarHoverColor(COLORREF c) {
    scrollbarHoverColor = c;
    return self;
  }
  std::shared_ptr<GridViewBuilder<T>> setScrollbarActiveColor(COLORREF c) {
    scrollbarActiveColor = c;
    return self;
  }
  std::shared_ptr<GridViewBuilder<T>> setScrollbarTrackColor(COLORREF c) {
    scrollbarTrackColor = c;
    return self;
  }
  std::shared_ptr<GridViewBuilder<T>> setScrollbarWidth(int w) {
    scrollbarWidth = w;
    return self;
  }

  // ── Layout ────────────────────────────────────────────────────────────

  void computeLayout(HDC hdc, const BoxConstraints &constraints,
                     FontCache &fontCache) override {
    rebuildList();

    if (autoHeight || height > constraints.maxHeight)
      height = constraints.maxHeight;
    if (autoWidth || width > constraints.maxWidth)
      width = constraints.maxWidth;

    viewportHeight = height - paddingTop - paddingBottom;

    int sbW = isScrollable ? scrollbarWidth : 0;
    int contentW = width - paddingLeft - paddingRight - sbW;

    int cols = resolvedColumnCount(contentW);
    int cellW = cols > 1 ? (contentW - spacingH * (cols - 1)) / cols : contentW;

    int rows = cols > 0 ? ((int)children.size() + cols - 1) / cols : 0;
    std::vector<int> rowHeights(rows, 0);

    for (int i = 0; i < (int)children.size(); i++) {
      int row = i / cols;
      children[i]->computeLayout(
          hdc, BoxConstraints::loose(cellW, viewportHeight), fontCache);
      rowHeights[row] = max(rowHeights[row], children[i]->height);
    }

    int total = 0;
    for (int r = 0; r < rows; r++) {
      total += rowHeights[r];
      if (r < rows - 1)
        total += spacingV;
    }
    contentHeight = total;

    bool wasScrollable = isScrollable;
    isScrollable = (contentHeight > viewportHeight);

    if (wasScrollable && !isScrollable) {
      scrollOffset = 0;
      releaseDragCapture();
      isHoveringScrollbar = false;
    }
    clampScrollOffset();

    // Re-measure with correct scrollbar width now that isScrollable is known
    sbW = isScrollable ? scrollbarWidth : 0;
    contentW = width - paddingLeft - paddingRight - sbW;
    cols = resolvedColumnCount(contentW);
    cellW = cols > 1 ? (contentW - spacingH * (cols - 1)) / cols : contentW;

    _cols = cols;
    _cellW = cellW;
    _rowHeights = rowHeights;

    updateScrollbar();
    applyConstraints();
    needsLayout = false;
  }

  void positionChildren(int /*contentX*/, int /*contentY*/,
                        int /*contentWidth*/, int /*contentHeight*/) override {
    repositionChildren();
  }

  // ── Mouse events (identical logic to ListViewBuilder) ─────────────────

  bool handleMouseWheel(int delta) override {
    if (!isScrollable)
      return false;
    scrollOffset -= (delta / WHEEL_DELTA) * 40;
    clampScrollOffset();
    updateScrollbar();
    repositionChildren();
    markNeedsPaint();
    return true;
  }

  bool handleMouseDown(int mx, int my) override {
    if (!isScrollable)
      return false;

    int sbX = x + width - scrollbarWidth;
    if (mx < sbX)
      return false; // not in scrollbar column

    int thumbTop = y + scrollbarThumbY;
    int thumbBottom = thumbTop + scrollbarThumbHeight;

    if (my >= thumbTop && my < thumbBottom) {
      // Drag thumb
      isDraggingScrollbar = true;
      dragStartY = my;
      dragStartOffset = scrollOffset;

      if (boundState && boundState->hasContext())
        if (auto *ui = boundState->getContext())
          if (ui->getWindow())
            SetCapture(ui->getWindow());

      markNeedsPaint();
      return true;
    }

    // Click on track — jump
    float ratio = (float)(my - y) / (float)viewportHeight;
    scrollOffset = (int)(ratio * (contentHeight - viewportHeight));
    clampScrollOffset();
    updateScrollbar();
    repositionChildren();
    markNeedsPaint();
    return true;
  }

  bool handleMouseUp(int mx, int my) override {
    if (isDraggingScrollbar) {
      releaseDragCapture();
      markNeedsPaint();
      return true;
    }
    return false;
  }

  bool handleMouseMove(int mx, int my) override {
    if (isDraggingScrollbar) {
      if (!isScrollable) {
        releaseDragCapture();
        markNeedsPaint();
        return true;
      }

      int dy = my - dragStartY;
      float ratio = (float)dy / (float)(viewportHeight - scrollbarThumbHeight);
      scrollOffset =
          dragStartOffset + (int)(ratio * (contentHeight - viewportHeight));
      clampScrollOffset();
      updateScrollbar();
      repositionChildren();
      markNeedsPaint();
      return true;
    }

    bool wasHovering = isHoveringScrollbar;
    isHoveringScrollbar = isMouseOverScrollbarThumb(mx, my);
    if (wasHovering != isHoveringScrollbar) {
      markNeedsPaint();
      return true;
    }
    return false;
  }

  bool handleMouseLeave() override {
    bool changed = isHoveringScrollbar;
    isHoveringScrollbar = false;
    if (changed) {
      markNeedsPaint();
      return true;
    }
    return false;
  }

  // ── Render ────────────────────────────────────────────────────────────

  void render(HDC hdc, FontCache &fontCache) override {
    updateScrollbar();

    int sbW = isScrollable ? scrollbarWidth : 0;
    int clipRight = x + width - paddingRight - sbW;

    // Clip to viewport so items don't bleed outside
    HRGN clip = CreateRectRgn(x + paddingLeft, y + paddingTop, clipRight,
                              y + height - paddingBottom);
    SelectClipRgn(hdc, clip);

    if (hasBackground)
      drawRoundedRectangle(hdc);

    int viewTop = y + paddingTop;
    int viewBottom = y + height - paddingBottom;

    for (auto &child : children) {
      // Only render rows visible in the viewport
      if (child->y + child->height >= viewTop && child->y < viewBottom)
        child->render(hdc, fontCache);
    }

    SelectClipRgn(hdc, NULL);
    DeleteObject(clip);

    if (isScrollable)
      renderScrollbar(hdc);

    needsPaint = false;
  }
};

// ============================================================================
// FACTORY
// ============================================================================

template <typename T>
inline std::shared_ptr<GridViewBuilder<T>>
GridView(State<std::vector<T>> &state) {
  auto w = std::make_shared<GridViewBuilder<T>>(state);
  w->setSelf(w);
  return w;
}

// ============================================================================
// GRID WIDGET
// ============================================================================
//
// Lays children out in a fixed column count grid, left-to-right, top-to-bottom.
//
// Two sizing modes (set via factory or setColumnCount / setColumnWidth):
//
//   Fixed column count  — Grid(children, columns: 3)
//     Each column is (availableWidth - padding - gaps) / columnCount wide.
//     Row height = tallest cell in that row.
//
//   Fixed column width  — GridFixedWidth(children, cellWidth: 200)
//     Column count is derived from available width.
//     Useful for responsive wrapping layouts.
//
// Alignment mirrors Row/Column:
//   setCrossAlignment  — horizontal alignment of each cell within its column
//                        (Start | Center | End | Stretch)
//   setMainAxisAlignment — horizontal distribution of columns within the row
//                          (Start | Center | End)
//
// Usage:
//   Grid(3,
//       Card(Text("A")),
//       Card(Text("B")),
//       Card(Text("C")),
//       Card(Text("D"))
//   )
//   ->setSpacingH(16)
//   ->setSpacingV(16)
//   ->setCrossAlignment(Alignment::Stretch);

class GridWidget : public Widget {
public:
  // ── Configuration ─────────────────────────────────────────────────────────
  int columnCount = 2;     // fixed col count mode (default)
  int fixedCellWidth = -1; // -1 = use columnCount; >0 = fixed width mode
  int spacingH = 0;        // horizontal gap between columns
  int spacingV = 0;        // vertical gap between rows

  // ── Layout ────────────────────────────────────────────────────────────────
  void computeLayout(HDC hdc, const BoxConstraints &constraints,
                     FontCache &fontCache) override {
    if (children.empty()) {
      if (autoWidth)
        width = paddingLeft + paddingRight;
      if (autoHeight)
        height = paddingTop + paddingBottom;
      applyConstraints();
      needsLayout = false;
      return;
    }

    int contentWidth = constraints.maxWidth - paddingLeft - paddingRight;

    int cols = resolvedColumnCount(contentWidth);
    if (cols < 1)
      cols = 1;

    int cellW = (contentWidth - spacingH * (cols - 1)) / cols;

    int rows = ((int)children.size() + cols - 1) / cols;
    std::vector<int> rowHeights(rows, 0);

    for (int i = 0; i < (int)children.size(); i++) {
      int row = i / cols;
      auto &child = children[i];

      int childW = (crossAlignment == Alignment::Stretch)
                       ? cellW
                       : min(cellW, contentWidth);
      child->computeLayout(
          hdc, BoxConstraints::loose(childW, constraints.maxHeight), fontCache);
      rowHeights[row] = max(rowHeights[row], child->height);
    }

    int totalH = 0;
    for (int r = 0; r < rows; r++) {
      totalH += rowHeights[r];
      if (r < rows - 1)
        totalH += spacingV;
    }

    if (autoWidth)
      width = constraints.maxWidth;
    if (autoHeight)
      height = totalH + paddingTop + paddingBottom;

    applyConstraints();
    needsLayout = false;

    _cols = cols;
    _cellW = cellW;
    _rowHeights = rowHeights;
  }

  void positionChildren(int contentX, int contentY, int contentWidth,
                        int contentHeight) override {
    if (children.empty() || _cols == 0)
      return;

    int cols = _cols;
    int cellW = _cellW;
    int rows = (int)_rowHeights.size();

    // Main axis offset for Center / End alignment of the whole column block
    int totalGridW = cellW * cols + spacingH * (cols - 1);
    int startX = contentX;
    if (mainAxisAlignment == MainAxisAlignment::Center)
      startX += (contentWidth - totalGridW) / 2;
    else if (mainAxisAlignment == MainAxisAlignment::End)
      startX += contentWidth - totalGridW;

    int curY = contentY;

    for (int row = 0; row < rows; row++) {
      int rowH = _rowHeights[row];

      for (int col = 0; col < cols; col++) {
        int idx = row * cols + col;
        if (idx >= (int)children.size())
          break;

        auto &child = children[idx];

        // Horizontal position within cell
        int cellX = startX + col * (cellW + spacingH);
        int childX = cellX;

        if (crossAlignment == Alignment::Center)
          childX = cellX + (cellW - child->width) / 2;
        else if (crossAlignment == Alignment::End)
          childX = cellX + cellW - child->width;
        else if (crossAlignment == Alignment::Stretch)
          child->width = cellW;

        // Vertical position within row — top-align by default
        int childY = curY;
        // Could add row cross-axis alignment here if needed

        child->x = childX;
        child->y = childY;

        child->positionChildren(
            child->x + child->paddingLeft, child->y + child->paddingTop,
            child->width - child->paddingLeft - child->paddingRight,
            child->height - child->paddingTop - child->paddingBottom);
      }

      curY += rowH + (row < rows - 1 ? spacingV : 0);
    }
  }

  void render(HDC hdc, FontCache &fontCache) override {
    if (hasBackground)
      drawRoundedRectangle(hdc);

    for (auto &child : children)
      child->render(hdc, fontCache);

    needsPaint = false;
  }

  // ── Fluent setters ────────────────────────────────────────────────────────

  std::shared_ptr<GridWidget> setColumnCount(int c) {
    columnCount = c;
    fixedCellWidth = -1;
    markNeedsLayout();
    return self();
  }

  // Switch to fixed-cell-width (responsive) mode
  std::shared_ptr<GridWidget> setColumnWidth(int w) {
    fixedCellWidth = w;
    markNeedsLayout();
    return self();
  }

  std::shared_ptr<GridWidget> setSpacing(int s) {
    spacingH = spacingV = s;
    markNeedsLayout();
    return self();
  }

  std::shared_ptr<GridWidget> setSpacingH(int s) {
    spacingH = s;
    markNeedsLayout();
    return self();
  }

  std::shared_ptr<GridWidget> setSpacingV(int s) {
    spacingV = s;
    markNeedsLayout();
    return self();
  }

  std::shared_ptr<GridWidget> setCrossAlignment(Alignment a) {
    crossAlignment = a;
    markNeedsLayout();
    return self();
  }

  std::shared_ptr<GridWidget> setMainAxisAlignment(MainAxisAlignment a) {
    mainAxisAlignment = a;
    markNeedsLayout();
    return self();
  }

  std::shared_ptr<GridWidget> setPadding(int p) {
    paddingLeft = paddingRight = paddingTop = paddingBottom = p;
    markNeedsLayout();
    return self();
  }

  std::shared_ptr<GridWidget> setPaddingAll(int l, int t, int r, int b) {
    paddingLeft = l;
    paddingTop = t;
    paddingRight = r;
    paddingBottom = b;
    markNeedsLayout();
    return self();
  }

  std::shared_ptr<GridWidget> setWidth(int w) {
    width = w;
    autoWidth = false;
    markNeedsLayout();
    return self();
  }

  std::shared_ptr<GridWidget> setHeight(int h) {
    height = h;
    autoHeight = false;
    markNeedsLayout();
    return self();
  }

  std::shared_ptr<GridWidget> setBackgroundColor(COLORREF color) {
    backgroundColor = color;
    hasBackground = true;
    markNeedsPaint();
    return self();
  }

  std::shared_ptr<GridWidget> setFlex(int f) {
    flex = f;
    markNeedsLayout();
    return self();
  }

private:
  // Cached by computeLayout, consumed by positionChildren
  int _cols = 0;
  int _cellW = 0;
  std::vector<int> _rowHeights;

  int resolvedColumnCount(int contentWidth) const {
    if (fixedCellWidth > 0) {
      // How many columns fit? At least 1.
      int cols = (contentWidth + spacingH) / (fixedCellWidth + spacingH);
      return max(1, cols);
    }
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

// Fixed column count
template <typename... Widgets>
GridWidgetPtr Grid(int columns, Widgets... widgets) {
  auto w = std::make_shared<GridWidget>();
  w->columnCount = columns;
  (w->addChild(widgets), ...);
  return w;
}

// Fixed cell width — column count adapts to available space
template <typename... Widgets>
GridWidgetPtr GridFixedWidth(int cellWidth, Widgets... widgets) {
  auto w = std::make_shared<GridWidget>();
  w->fixedCellWidth = cellWidth;
  (w->addChild(widgets), ...);
  return w;
}

// Build from a runtime vector (useful for dynamic lists)
inline GridWidgetPtr GridFromList(int columns,
                                  const std::vector<WidgetPtr> &items) {
  auto w = std::make_shared<GridWidget>();
  w->columnCount = columns;
  for (auto &item : items)
    w->addChild(item);
  return w;
}

inline GridWidgetPtr
GridFixedWidthFromList(int cellWidth, const std::vector<WidgetPtr> &items) {
  auto w = std::make_shared<GridWidget>();
  w->fixedCellWidth = cellWidth;
  for (auto &item : items)
    w->addChild(item);
  return w;
}

// ============================================================================
// FACTORY
// ============================================================================

template <typename T>
inline std::shared_ptr<ListViewBuilder<T>>
ListView(State<std::vector<T>> &state) {
  auto w = std::make_shared<ListViewBuilder<T>>(state);
  w->setSelf(w);
  return w;
}

#endif // FLUX_LISTVIEW_HPP