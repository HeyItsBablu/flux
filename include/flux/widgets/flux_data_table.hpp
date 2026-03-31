#ifndef FLUX_DATA_TABLE_HPP
#define FLUX_DATA_TABLE_HPP

#include "../flux_core.hpp"
#include "flux_layout.hpp"
#include "../flux_state.hpp"
#include <algorithm>
#include <functional>
#include <string>
#include <vector>

// ============================================================================
// COLUMN ALIGNMENT
// ============================================================================

enum class ColumnAlign { Left, Center, Right };

// ============================================================================
// DATA ROW
// ============================================================================
//
// A key-value store for a single table row. Keys match DataColumn::key.
//
// Usage:
//   DataRow row("optional-id");
//   row.set("name", "Alice").set("age", "29");
//   row.get("name");  // "Alice"
// ============================================================================

struct DataRow {
  std::string id;
  bool disabled = false;

  explicit DataRow(const std::string &rowId = "") : id(rowId) {}

  DataRow &set(const std::string &key, const std::string &value) {
    data_[key] = value;
    return *this;
  }

  std::string get(const std::string &key) const {
    auto it = data_.find(key);
    return it != data_.end() ? it->second : "";
  }

  bool hasKey(const std::string &key) const {
    return data_.find(key) != data_.end();
  }

private:
  std::map<std::string, std::string> data_;
};

// ============================================================================
// DATA COLUMN
// ============================================================================
//
// Describes a single column: key, header label, width, alignment,
// sortability, resizability, and an optional formatter.
// ============================================================================

struct DataColumn {
  std::string key;
  std::string label;
  int width = 120;
  int minWidth = 40;
  ColumnAlign align = ColumnAlign::Left;
  bool sortable = true;
  bool resizable = true;
  std::function<std::string(const std::string &)> formatter;

  DataColumn(const std::string &k, const std::string &lbl, int w = 120)
      : key(k), label(lbl), width(w) {}

  DataColumn &setAlign(ColumnAlign a) {
    align = a;
    return *this;
  }
  DataColumn &setSortable(bool v) {
    sortable = v;
    return *this;
  }
  DataColumn &setResizable(bool v) {
    resizable = v;
    return *this;
  }
  DataColumn &setMinWidth(int w) {
    minWidth = w;
    return *this;
  }
  DataColumn &setFormatter(std::function<std::string(const std::string &)> fn) {
    formatter = std::move(fn);
    return *this;
  }

  std::string format(const std::string &raw) const {
    return formatter ? formatter(raw) : raw;
  }
};

// ============================================================================
// DATA TABLE WIDGET
// ============================================================================

class DataTableWidget : public Widget {
public:
  // ── Appearance ────────────────────────────────────────────────────────────
  int rowHeight = 30;
  int headerHeight = 36;
  int scrollbarWidth_ = 10;

  Color headerBg = Color::fromRGB(245, 246, 248);
  Color headerTextColor = Color::fromRGB(60, 60, 60);
  Color rowBgColor = Color::fromRGB(255, 255, 255);
  Color rowAltBgColor = Color::fromRGB(249, 250, 251);
  Color rowHoverColor = Color::fromRGB(232, 244, 255);
  Color rowSelectColor = Color::fromRGB(210, 235, 255);
  Color accentColor = Color::fromRGB(33, 150, 243);
  Color borderColor_ = Color::fromRGB(220, 220, 222);
  Color textColor_ = Color::fromRGB(30, 30, 30);
  Color disabledColor_ = Color::fromRGB(170, 170, 170);
  Color dividerColor_ = Color::fromRGB(230, 230, 232);

  bool alternateRows = true;
  bool showColumnDividers = false;
  int fontSize_ = 13;

  // ── Callbacks ─────────────────────────────────────────────────────────────
  std::function<void(int, const DataRow &)> onRowSelected;
  std::function<void(int, const DataRow &)> onRowDoubleClicked;
  std::function<void(const std::string &, bool)> onSortChanged;

  // ── Constructors ──────────────────────────────────────────────────────────

  // Static rows
  DataTableWidget(std::vector<DataColumn> columns, std::vector<DataRow> rows)
      : columns_(std::move(columns)), staticRows_(std::move(rows)),
        useReactive_(false) {
    colWidths_.resize(columns_.size());
    for (size_t i = 0; i < columns_.size(); i++)
      colWidths_[i] = columns_[i].width;
    displayRows_ = &staticRows_;
  }

  // Reactive rows — binds to State<vector<DataRow>>
  DataTableWidget(std::vector<DataColumn> columns,
                  State<std::vector<DataRow>> &state)
      : columns_(std::move(columns)), reactiveState_(&state),
        useReactive_(true) {
    colWidths_.resize(columns_.size());
    for (size_t i = 0; i < columns_.size(); i++)
      colWidths_[i] = columns_[i].width;

    // bindProperty fires the applier immediately (synchronously) with a
    // const T& that points into State's internal storage — a stable l-value.
    // That first call sets displayRows_ before the constructor returns.
    state.bindProperty(
        shared_from_this(),
        [](Widget *w, const std::vector<DataRow> &rows) {
          auto *self = static_cast<DataTableWidget *>(w);
          self->displayRows_ = &rows; // &rows is always a valid l-value
          self->_rebuildSort();
          self->selectedIndex_ = -1;
          self->scrollOffsetY_ = 0;
          if (auto *ui = FluxUI::getCurrentInstance())
            ui->updateWidget(self);
        },
        true);
  }

  // ── Public API ────────────────────────────────────────────────────────────

  void setRows(std::vector<DataRow> rows) {
    staticRows_ = std::move(rows);
    displayRows_ = &staticRows_;
    selectedIndex_ = -1;
    scrollOffsetY_ = 0;
    _rebuildSort();
    if (auto *ui = FluxUI::getCurrentInstance())
      ui->updateWidget(this);
  }

  void sortBy(const std::string &key, bool ascending) {
    sortKey_ = key;
    sortAscending_ = ascending;
    _rebuildSort();
    if (auto *ui = FluxUI::getCurrentInstance())
      ui->updateWidget(this);
  }

  void clearSort() {
    sortKey_.clear();
    sortedIndices_.clear();
    if (auto *ui = FluxUI::getCurrentInstance())
      ui->updateWidget(this);
  }

  int selectedIndex() const { return selectedIndex_; }

  const DataRow *selectedRow() const {
    if (selectedIndex_ < 0)
      return nullptr;
    int real = _realIndex(selectedIndex_);
    if (real < 0 || real >= (int)displayRows_->size())
      return nullptr;
    return &(*displayRows_)[real];
  }

  // ── Layout ────────────────────────────────────────────────────────────────
  void computeLayout(GraphicsContext & /*ctx*/,
                     const BoxConstraints &constraints,
                     FontCache & /*fc*/) override {
    if (autoWidth)
      width = constraints.maxWidth;
    if (autoHeight)
      height = constraints.maxHeight;
    applyConstraints();
    needsLayout = false;
  }

  void positionChildren(int, int, int, int) override {}

  // ── Render ────────────────────────────────────────────────────────────────
  void render(GraphicsContext &ctx, FontCache &fontCache) override {
    if (!visible || !displayRows_)
      return;
    Painter painter(ctx);

    painter.pushClipRect(x, y, width, height);
    _renderHeader(ctx, fontCache);
    _renderRows(ctx, fontCache);
    _renderScrollbars(ctx);
    painter.drawRectOutline(x, y, width, height, borderColor_, 1);
    painter.popClipRect();

    needsPaint = false;
  }

  // ── Mouse events ──────────────────────────────────────────────────────────

  bool handleMouseDown(int mx, int my) override {
    if (!_inBounds(mx, my))
      return false;
    auto *ui = FluxUI::getCurrentInstance();

    if (my >= y && my < y + headerHeight) {
      int col = _resizeHandleAt(mx);
      if (col >= 0) {
        _resizingCol_ = col;
        _resizeDragX_ = mx;
        _resizeDragWidth_ = colWidths_[col];
        if (ui)
          ui->captureMouseInput();
        return true;
      }
      int hcol = _headerColAt(mx);
      if (hcol >= 0 && columns_[hcol].sortable) {
        if (sortKey_ == columns_[hcol].key)
          sortAscending_ = !sortAscending_;
        else {
          sortKey_ = columns_[hcol].key;
          sortAscending_ = true;
        }
        _rebuildSort();
        if (onSortChanged)
          onSortChanged(sortKey_, sortAscending_);
        if (ui)
          ui->updateWidget(this);
      }
      return true;
    }

    if (_needsVScrollbar() && mx >= x + width - scrollbarWidth_ - 1) {
      _vScrollDragging_ = true;
      _vScrollDragStartY_ = my;
      _vScrollDragStart_ = scrollOffsetY_;
      if (ui)
        ui->captureMouseInput();
      return true;
    }

    if (_needsHScrollbar() && my >= y + height - scrollbarWidth_ - 1) {
      _hScrollDragging_ = true;
      _hScrollDragStartX_ = mx;
      _hScrollDragStart_ = scrollOffsetX_;
      if (ui)
        ui->captureMouseInput();
      return true;
    }

    int row = _rowAt(my);
    if (row < 0)
      return true;
    int realIdx = _realIndex(row);
    if (realIdx < 0 || realIdx >= (int)displayRows_->size())
      return true;
    if ((*displayRows_)[realIdx].disabled)
      return true;

    selectedIndex_ = row;
    markNeedsPaint();
    if (onRowSelected)
      onRowSelected(row, (*displayRows_)[realIdx]);

    uint32_t now = platformTickCount();
    if (lastClickRow_ == row && (now - lastClickTime_) < 400) {
      if (onRowDoubleClicked)
        onRowDoubleClicked(row, (*displayRows_)[realIdx]);
      lastClickRow_ = -1;
    } else {
      lastClickRow_ = row;
      lastClickTime_ = now;
    }

    if (ui)
      ui->updateWidget(this);
    return true;
  }

  bool handleMouseMove(int mx, int my) override {
    auto *ui = FluxUI::getCurrentInstance();

    if (_resizingCol_ >= 0) {
      int dx = mx - _resizeDragX_;
      int newW = std::max(columns_[_resizingCol_].minWidth, _resizeDragWidth_ + dx);
      colWidths_[_resizingCol_] = newW;
      if (ui)
        ui->updateWidget(this);
      return true;
    }

    if (_vScrollDragging_) {
      int totalH = _totalContentHeight();
      int viewH = _contentAreaHeight();
      float ratio = (float)(my - _vScrollDragStartY_) / (float)viewH;
      int newOff = _vScrollDragStart_ + (int)(ratio * totalH);
      _clampVScroll(newOff);
      if (ui)
        ui->updateWidget(this);
      return true;
    }

    if (_hScrollDragging_) {
      int totalW = _totalColumnsWidth();
      int viewW = _contentAreaWidth();
      float ratio = (float)(mx - _hScrollDragStartX_) / (float)viewW;
      int newOff = _hScrollDragStart_ + (int)(ratio * totalW);
      _clampHScroll(newOff);
      if (ui)
        ui->updateWidget(this);
      return true;
    }

    bool onHandle = _inBounds(mx, my) && _resizeHandleAt(mx) >= 0;
    if (onHandle != _showResizeCursor_) {
      _showResizeCursor_ = onHandle;
      if (ui) {
        if (onHandle)
          ui->setResizeCursorH();
        else
          ui->setDefaultCursor();
      }
    }

    if (!_inBounds(mx, my)) {
      if (hoveredRow_ != -1) {
        hoveredRow_ = -1;
        markNeedsPaint();
      }
      return false;
    }
    int row = _rowAt(my);
    if (row != hoveredRow_) {
      hoveredRow_ = row;
      if (ui)
        ui->updateWidget(this);
    }
    return false;
  }

  bool handleMouseUp(int /*mx*/, int /*my*/) override {
    bool handled = _resizingCol_ >= 0 || _vScrollDragging_ || _hScrollDragging_;
    _resizingCol_ = -1;
    _vScrollDragging_ = false;
    _hScrollDragging_ = false;
    if (handled)
      if (auto *ui = FluxUI::getCurrentInstance())
        ui->releaseMouseInput();
    return handled;
  }

  bool handleMouseLeave() override {
    hoveredRow_ = -1;
    markNeedsPaint();
    return false;
  }

  bool handleMouseWheel(int delta) override {
    int newOff = scrollOffsetY_ - (delta / 120) * rowHeight * 3;
    _clampVScroll(newOff);
    if (auto *ui = FluxUI::getCurrentInstance())
      ui->updateWidget(this);
    return true;
  }

  bool handleKeyDown(int keyCode) override {
    if (!displayRows_ || displayRows_->empty())
      return false;
    int count = (int)displayRows_->size();

    switch (keyCode) {
    case Key::Up:
      if (selectedIndex_ > 0) {
        selectedIndex_--;
        _ensureRowVisible(selectedIndex_);
        _fireSelection();
        if (auto *ui = FluxUI::getCurrentInstance())
          ui->updateWidget(this);
      }
      return true;
    case Key::Down:
      if (selectedIndex_ < count - 1) {
        selectedIndex_++;
        _ensureRowVisible(selectedIndex_);
        _fireSelection();
        if (auto *ui = FluxUI::getCurrentInstance())
          ui->updateWidget(this);
      }
      return true;
    case Key::Home:
      selectedIndex_ = 0;
      _ensureRowVisible(0);
      _fireSelection();
      if (auto *ui = FluxUI::getCurrentInstance())
        ui->updateWidget(this);
      return true;
    case Key::End:
      selectedIndex_ = count - 1;
      _ensureRowVisible(selectedIndex_);
      _fireSelection();
      if (auto *ui = FluxUI::getCurrentInstance())
        ui->updateWidget(this);
      return true;
    case Key::PageUp: { // Page Up
      int page = std::max(1, _contentAreaHeight() / rowHeight);
      selectedIndex_ = std::max(0, selectedIndex_ - page);
      _ensureRowVisible(selectedIndex_);
      _fireSelection();
      if (auto *ui = FluxUI::getCurrentInstance())
        ui->updateWidget(this);
      return true;
    }
    case Key::PageDown: { // Page Down
      int page = std::max(1, _contentAreaHeight() / rowHeight);
      selectedIndex_ = std::min(count - 1, selectedIndex_ + page);
      _ensureRowVisible(selectedIndex_);
      _fireSelection();
      if (auto *ui = FluxUI::getCurrentInstance())
        ui->updateWidget(this);
      return true;
    }
    }
    return false;
  }

  // ── Fluent setters ────────────────────────────────────────────────────────

  std::shared_ptr<DataTableWidget> setAlternateRows(bool v) {
    alternateRows = v;
    markNeedsPaint();
    return self_();
  }
  std::shared_ptr<DataTableWidget> setShowColumnDividers(bool v) {
    showColumnDividers = v;
    markNeedsPaint();
    return self_();
  }
  std::shared_ptr<DataTableWidget> setRowHeight(int h) {
    rowHeight = h;
    markNeedsLayout();
    return self_();
  }
  std::shared_ptr<DataTableWidget> setHeaderHeight(int h) {
    headerHeight = h;
    markNeedsLayout();
    return self_();
  }
  std::shared_ptr<DataTableWidget> setHeaderBackground(Color c) {
    headerBg = c;
    markNeedsPaint();
    return self_();
  }
  std::shared_ptr<DataTableWidget> setAccentColor(Color c) {
    accentColor = c;
    rowSelectColor  = Color::fromRGB(
        (uint8_t)(c.r / 5 * 4),
        (uint8_t)(std::min(255, c.g / 5 * 4 + 20)),
        (uint8_t)(std::min(255, c.b / 5 * 4 + 40))
    );
    markNeedsPaint();
    return self_();
  }
  std::shared_ptr<DataTableWidget>
  setOnRowSelected(std::function<void(int, const DataRow &)> fn) {
    onRowSelected = std::move(fn);
    return self_();
  }
  std::shared_ptr<DataTableWidget>
  setOnRowDoubleClicked(std::function<void(int, const DataRow &)> fn) {
    onRowDoubleClicked = std::move(fn);
    return self_();
  }
  std::shared_ptr<DataTableWidget>
  setOnSortChanged(std::function<void(const std::string &, bool)> fn) {
    onSortChanged = std::move(fn);
    return self_();
  }
  std::shared_ptr<DataTableWidget> setFlex(int f) {
    flex = f;
    return self_();
  }
  std::shared_ptr<DataTableWidget> setWidth(int w) {
    width = w;
    autoWidth = false;
    markNeedsLayout();
    return self_();
  }
  std::shared_ptr<DataTableWidget> setHeight(int h) {
    height = h;
    autoHeight = false;
    markNeedsLayout();
    return self_();
  }

private:
  // ── Data ──────────────────────────────────────────────────────────────────
  std::vector<DataColumn> columns_;
  std::vector<int> colWidths_; // live widths (may be resized)

  std::vector<DataRow> staticRows_;
  State<std::vector<DataRow>> *reactiveState_ = nullptr; // owning state
  const std::vector<DataRow> *displayRows_ = nullptr;
  bool useReactive_ = false;

  // Sorted index mapping: sortedIndices_[visualRow] = originalIndex
  // If empty, no sort is active.
  std::vector<int> sortedIndices_;
  std::string sortKey_;
  bool sortAscending_ = true;

  // ── Interaction state ─────────────────────────────────────────────────────
  int selectedIndex_ = -1;
  int hoveredRow_ = -1;
  int scrollOffsetY_ = 0;
  int scrollOffsetX_ = 0;

  // Column resize
  int _resizingCol_ = -1;
  int _resizeDragX_ = 0;
  int _resizeDragWidth_ = 0;
  bool _showResizeCursor_ = false;

  // V-scroll drag
  bool _vScrollDragging_ = false;
  int _vScrollDragStartY_ = 0;
  int _vScrollDragStart_ = 0;

  // H-scroll drag
  bool _hScrollDragging_ = false;
  int _hScrollDragStartX_ = 0;
  int _hScrollDragStart_ = 0;

  // Double-click
  int lastClickRow_ = -1;
  uint32_t lastClickTime_ = 0;

  // ── Helpers ───────────────────────────────────────────────────────────────

  std::shared_ptr<DataTableWidget> self_() {
    return std::static_pointer_cast<DataTableWidget>(shared_from_this());
  }

  HWND _getHWND() const {
    if (auto *ui = FluxUI::getCurrentInstance())
      return ui->getWindow();
    return nullptr;
  }

  bool _inBounds(int mx, int my) const {
    return mx >= x && mx < x + width && my >= y && my < y + height;
  }

  // Content area excludes scrollbars
  int _contentAreaWidth() const {
    return width - (_needsVScrollbar() ? scrollbarWidth_ + 1 : 0);
  }
  int _contentAreaHeight() const {
    return height - headerHeight -
           (_needsHScrollbar() ? scrollbarWidth_ + 1 : 0);
  }

  int _totalColumnsWidth() const {
    int total = 0;
    for (int w : colWidths_)
      total += w;
    return total;
  }
  int _totalContentHeight() const {
    return displayRows_ ? (int)displayRows_->size() * rowHeight : 0;
  }

  bool _needsVScrollbar() const {
    return _totalContentHeight() > height - headerHeight;
  }
  bool _needsHScrollbar() const { return _totalColumnsWidth() > width; }

  void _clampVScroll(int off) {
    int maxOff = std::max(0, _totalContentHeight() - _contentAreaHeight());
    scrollOffsetY_ = std::max(0, std::min(maxOff, off));
    markNeedsPaint();
  }
  void _clampHScroll(int off) {
    int maxOff = std::max(0, _totalColumnsWidth() - _contentAreaWidth());
    scrollOffsetX_ = std::max(0, std::min(maxOff, off));
    markNeedsPaint();
  }

  // Map visual row index → original data index (accounting for sort)
  int _realIndex(int visualRow) const {
    if (sortedIndices_.empty())
      return visualRow;
    if (visualRow < 0 || visualRow >= (int)sortedIndices_.size())
      return visualRow;
    return sortedIndices_[visualRow];
  }

  // Row visual index from screen Y
  int _rowAt(int screenY) const {
    int relY = screenY - y - headerHeight + scrollOffsetY_;
    if (relY < 0)
      return -1;
    int row = relY / rowHeight;
    if (!displayRows_ || row >= (int)displayRows_->size())
      return -1;
    return row;
  }

  // Which column header was clicked?
  int _headerColAt(int screenX) const {
    int cx = x - scrollOffsetX_;
    for (int i = 0; i < (int)columns_.size(); i++) {
      if (screenX >= cx && screenX < cx + colWidths_[i])
        return i;
      cx += colWidths_[i];
    }
    return -1;
  }

  // Returns column index whose right edge is within 4px, for resize handle
  int _resizeHandleAt(int screenX) const {
    if (!columns_.empty() && !columns_[0].resizable && columns_.size() == 1)
      return -1;
    int cx = x - scrollOffsetX_;
    for (int i = 0; i < (int)columns_.size(); i++) {
      cx += colWidths_[i];
      if (abs(screenX - cx) <= 4 && columns_[i].resizable)
        return i;
    }
    return -1;
  }

  void _ensureRowVisible(int row) {
    int rowTop = row * rowHeight;
    int rowBot = rowTop + rowHeight;
    if (rowTop < scrollOffsetY_)
      _clampVScroll(rowTop);
    else if (rowBot > scrollOffsetY_ + _contentAreaHeight())
      _clampVScroll(rowBot - _contentAreaHeight());
  }

  void _fireSelection() {
    if (!displayRows_ || selectedIndex_ < 0)
      return;
    int real = _realIndex(selectedIndex_);
    if (real < 0 || real >= (int)displayRows_->size())
      return;
    if (onRowSelected)
      onRowSelected(selectedIndex_, (*displayRows_)[real]);
  }

  // ── Sort ──────────────────────────────────────────────────────────────────

  // Returns true if a string is purely numeric (integer or float)
  static bool _isNumeric(const std::string &s) {
    if (s.empty())
      return false;
    char *end = nullptr;
    std::strtod(s.c_str(), &end);
    return end == s.c_str() + s.size();
  }

  void _rebuildSort() {
    if (!displayRows_)
      return;
    if (sortKey_.empty()) {
      sortedIndices_.clear();
      return;
    }

    int n = (int)displayRows_->size();
    sortedIndices_.resize(n);
    for (int i = 0; i < n; i++)
      sortedIndices_[i] = i;

    const std::string &key = sortKey_;
    bool ascending = sortAscending_;
    const auto &rows = *displayRows_;

    std::stable_sort(
        sortedIndices_.begin(), sortedIndices_.end(), [&](int a, int b) {
          const std::string &va = rows[a].get(key);
          const std::string &vb = rows[b].get(key);

          // Numeric comparison when both values are numbers
          if (_isNumeric(va) && _isNumeric(vb)) {
            double da = std::stod(va);
            double db = std::stod(vb);
            return ascending ? da < db : da > db;
          }

          // Case-insensitive lexicographic comparison
          std::string la = va, lb = vb;
          std::transform(la.begin(), la.end(), la.begin(), ::tolower);
          std::transform(lb.begin(), lb.end(), lb.begin(), ::tolower);
          return ascending ? la < lb : la > lb;
        });
  }

  // ── Rendering ─────────────────────────────────────────────────────────────

  void _renderHeader(GraphicsContext &ctx, FontCache &fontCache) const {
    Painter painter(ctx);
    int contentW = _contentAreaWidth();

    painter.fillRect(x, y, contentW, headerHeight, headerBg);
    painter.drawHLine(x, y + headerHeight, contentW, borderColor_, 1);

    NativeFont hFont = fontCache.getFont(fontSize_, FontWeight::Bold);
    int cx = x - scrollOffsetX_;

    for (int i = 0; i < (int)columns_.size(); i++) {
      int cw = colWidths_[i];
      int cRight = cx + cw;
      int cLeft = std::max(cx, x);

      if (cLeft < x + contentW && cRight > x) {
        painter.pushClipRect(cLeft, y, std::min(cRight, x + contentW) - cLeft,
                             headerHeight);

        bool isSortCol = (sortKey_ == columns_[i].key);
        std::string label = columns_[i].label;
        if (isSortCol && columns_[i].sortable)
          label += (sortAscending_ ? "  \u25B2" : "  \u25BC");

        if (isSortCol) {
          painter.fillRect(cLeft, y + headerHeight - 2,
                           std::min(cRight, x + contentW) - cLeft, 2, accentColor);
        }

        UINT fmt = DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS;
        switch (columns_[i].align) {
        case ColumnAlign::Center:
          fmt |= DT_CENTER;
          break;
        case ColumnAlign::Right:
          fmt |= DT_RIGHT;
          break;
        default:
          fmt |= DT_LEFT;
          break;
        }

        painter.drawTextA(label, cLeft + 8, y,
                          std::min(cRight - 8, x + contentW) - cLeft - 8,
                          headerHeight, hFont,
                          isSortCol ? accentColor : headerTextColor, fmt);

        painter.popClipRect();

        if (showColumnDividers && i < (int)columns_.size() - 1) {
          int lx = std::min(cRight, x + contentW);
          painter.drawVLine(lx, y + 6, headerHeight - 12, dividerColor_, 1);
        }
      }
      cx += cw;
    }

    // Restore full clip
    painter.pushClipRect(x, y, width, height);
  }

  void _renderRows(GraphicsContext &ctx, FontCache &fontCache) const {
    if (!displayRows_ || displayRows_->empty())
      return;
    Painter painter(ctx);

    int contentW = _contentAreaWidth();
    int contentH = _contentAreaHeight();
    int bodyY = y + headerHeight;

    painter.pushClipRect(x, bodyY, contentW, contentH);

    NativeFont hFont = fontCache.getFont(fontSize_, FontWeight::Normal);

    int firstRow = scrollOffsetY_ / rowHeight;
    int lastRow = std::min((int)displayRows_->size() - 1,
                      (scrollOffsetY_ + contentH) / rowHeight);

    for (int vi = firstRow; vi <= lastRow; vi++) {
      int real = _realIndex(vi);
      if (real < 0 || real >= (int)displayRows_->size())
        continue;

      const DataRow &row = (*displayRows_)[real];
      int rowY = bodyY + vi * rowHeight - scrollOffsetY_;
      bool isSel = (vi == selectedIndex_);
      bool isHov = (vi == hoveredRow_ && !isSel);

      Color bg;
      if (isSel)
        bg = rowSelectColor;
      else if (isHov)
        bg = rowHoverColor;
      else if (alternateRows && vi % 2 == 1)
        bg = rowAltBgColor;
      else
        bg = rowBgColor;

      if (isSel)
        painter.fillRectWithLeftAccent(x, rowY, contentW, rowHeight, bg,
                                       accentColor, 3);
      else
        painter.fillRect(x, rowY, contentW, rowHeight, bg);

      int cx = x - scrollOffsetX_;
      for (int ci = 0; ci < (int)columns_.size(); ci++) {
        int cw = colWidths_[ci];
        int cRight = cx + cw;
        int cLeft = std::max(cx, x);

        if (cLeft < x + contentW && cRight > x) {
          painter.pushClipRect(cLeft, rowY, std::min(cRight, x + contentW) - cLeft,
                               rowHeight);

          std::string val = columns_[ci].format(row.get(columns_[ci].key));
          UINT fmt = DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS;
          switch (columns_[ci].align) {
          case ColumnAlign::Center:
            fmt |= DT_CENTER;
            break;
          case ColumnAlign::Right:
            fmt |= DT_RIGHT;
            break;
          default:
            fmt |= DT_LEFT;
            break;
          }

          painter.drawTextA(val, cLeft + 8, rowY,
                            std::min(cRight - 8, x + contentW) - cLeft - 8,
                            rowHeight, hFont,
                            row.disabled ? disabledColor_ : textColor_, fmt);

          painter.popClipRect();
          // Re-apply body clip after popping cell clip
          painter.pushClipRect(x, bodyY, contentW, contentH);

          if (showColumnDividers && ci < (int)columns_.size() - 1) {
            int lx = std::min(cRight, x + contentW - 1);
            painter.drawVLine(lx, rowY + 4, rowHeight - 8, dividerColor_, 1);
          }
        }
        cx += cw;
      }

      // Row separator
      painter.drawHLine(x, rowY + rowHeight - 1, contentW, borderColor_, 1);
    }

    // Fill empty space below rows
    int lastRowBottom =
        bodyY + ((int)displayRows_->size()) * rowHeight - scrollOffsetY_;
    if (lastRowBottom < bodyY + contentH)
      painter.fillRect(x, lastRowBottom, contentW,
                       bodyY + contentH - lastRowBottom, rowBgColor);

    // Restore to full widget clip
    painter.popClipRect();
    painter.pushClipRect(x, y, width, height);
  }
  void _renderScrollbars(GraphicsContext &ctx) const {
    Painter painter(ctx);
    bool needsV = _needsVScrollbar();
    bool needsH = _needsHScrollbar();

    if (needsV) {
      int sbX = x + width - scrollbarWidth_ - 1;
      int sbY = y + headerHeight;
      int sbH = height - headerHeight - (needsH ? scrollbarWidth_ + 1 : 0);
      int total = _totalContentHeight();

      painter.fillRect(sbX, sbY, scrollbarWidth_, sbH, Color::fromRGB(240, 240, 240));

      float thumbRatio = (float)sbH / (float)total;
      int thumbH = std::max(20, (int)(sbH * thumbRatio));
      float scrollRatio =
          (total > sbH) ? (float)scrollOffsetY_ / (float)(total - sbH) : 0.f;
      int thumbY = sbY + (int)(scrollRatio * (sbH - thumbH));

      painter.fillRoundedRegion(sbX + 2, thumbY, scrollbarWidth_ - 3, thumbH, 4,
                                accentColor);
    }

    if (needsH) {
      int sbX = x;
      int sbY = y + height - scrollbarWidth_ - 1;
      int sbW = width - (needsV ? scrollbarWidth_ + 1 : 0);
      int total = _totalColumnsWidth();

      painter.fillRect(sbX, sbY, sbW, scrollbarWidth_, Color::fromRGB(240, 240, 240));

      float thumbRatio = (float)sbW / (float)total;
      int thumbW = std::max(20, (int)(sbW * thumbRatio));
      float scrollRatio =
          (total > sbW) ? (float)scrollOffsetX_ / (float)(total - sbW) : 0.f;
      int thumbX = sbX + (int)(scrollRatio * (sbW - thumbW));

      painter.fillRoundedRegion(thumbX, sbY + 2, thumbW, scrollbarWidth_ - 3, 4,
                                accentColor);
    }

    if (needsV && needsH)
      painter.fillRect(x + width - scrollbarWidth_ - 1,
                       y + height - scrollbarWidth_ - 1, scrollbarWidth_ + 1,
                       scrollbarWidth_ + 1, Color::fromRGB(240, 240, 240));
  }
};

// ============================================================================
// FACTORY FUNCTIONS
// ============================================================================

using DataTableWidgetPtr = std::shared_ptr<DataTableWidget>;

// Static rows
inline DataTableWidgetPtr DataTable(std::vector<DataColumn> columns,
                                    std::vector<DataRow> rows) {
  return std::make_shared<DataTableWidget>(std::move(columns), std::move(rows));
}

// Reactive rows
inline DataTableWidgetPtr DataTable(std::vector<DataColumn> columns,
                                    State<std::vector<DataRow>> &state) {
  return std::make_shared<DataTableWidget>(std::move(columns), state);
}

#endif // FLUX_DATA_TABLE_HPP