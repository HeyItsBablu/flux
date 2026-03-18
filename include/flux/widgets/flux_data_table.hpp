#ifndef FLUX_DATA_TABLE_HPP
#define FLUX_DATA_TABLE_HPP

#include "flux_core.hpp"
#include "flux_layout.hpp"
#include "flux_state.hpp"
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
    bool        disabled = false;

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
    int         width      = 120;
    int         minWidth   = 40;
    ColumnAlign align      = ColumnAlign::Left;
    bool        sortable   = true;
    bool        resizable  = true;
    std::function<std::string(const std::string &)> formatter;

    DataColumn(const std::string &k, const std::string &lbl, int w = 120)
        : key(k), label(lbl), width(w) {}

    DataColumn &setAlign(ColumnAlign a)   { align     = a;  return *this; }
    DataColumn &setSortable(bool v)       { sortable  = v;  return *this; }
    DataColumn &setResizable(bool v)      { resizable = v;  return *this; }
    DataColumn &setMinWidth(int w)        { minWidth  = w;  return *this; }
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
    int      rowHeight        = 30;
    int      headerHeight     = 36;
    int      scrollbarWidth_  = 10;

    COLORREF headerBg         = RGB(245, 246, 248);
    COLORREF headerTextColor  = RGB(60,  60,  60);
    COLORREF rowBgColor       = RGB(255, 255, 255);
    COLORREF rowAltBgColor    = RGB(249, 250, 251);
    COLORREF rowHoverColor    = RGB(232, 244, 255);
    COLORREF rowSelectColor   = RGB(210, 235, 255);
    COLORREF accentColor      = RGB(33,  150, 243);
    COLORREF borderColor_     = RGB(220, 220, 222);
    COLORREF textColor_       = RGB(30,  30,  30);
    COLORREF disabledColor_   = RGB(170, 170, 170);
    COLORREF dividerColor_    = RGB(230, 230, 232);

    bool alternateRows        = true;
    bool showColumnDividers   = false;
    int  fontSize_            = 13;

    // ── Callbacks ─────────────────────────────────────────────────────────────
    std::function<void(int, const DataRow &)> onRowSelected;
    std::function<void(int, const DataRow &)> onRowDoubleClicked;
    std::function<void(const std::string &, bool)> onSortChanged;

    // ── Constructors ──────────────────────────────────────────────────────────

    // Static rows
    DataTableWidget(std::vector<DataColumn> columns,
                    std::vector<DataRow>    rows)
        : columns_(std::move(columns))
        , staticRows_(std::move(rows))
        , useReactive_(false)
    {
        colWidths_.resize(columns_.size());
        for (size_t i = 0; i < columns_.size(); i++)
            colWidths_[i] = columns_[i].width;
        displayRows_ = &staticRows_;
    }

    // Reactive rows — binds to State<vector<DataRow>>
    DataTableWidget(std::vector<DataColumn>         columns,
                    State<std::vector<DataRow>>     &state)
        : columns_(std::move(columns))
        , reactiveState_(&state)
        , useReactive_(true)
    {
        colWidths_.resize(columns_.size());
        for (size_t i = 0; i < columns_.size(); i++)
            colWidths_[i] = columns_[i].width;

        // bindProperty fires the applier immediately (synchronously) with a
        // const T& that points into State's internal storage — a stable l-value.
        // That first call sets displayRows_ before the constructor returns.
        state.bindProperty(
            shared_from_this(),
            [](Widget *w, const std::vector<DataRow> &rows) {
                auto *self         = static_cast<DataTableWidget *>(w);
                self->displayRows_ = &rows;  // &rows is always a valid l-value
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
        staticRows_   = std::move(rows);
        displayRows_  = &staticRows_;
        selectedIndex_ = -1;
        scrollOffsetY_ = 0;
        _rebuildSort();
        if (auto *ui = FluxUI::getCurrentInstance())
            ui->updateWidget(this);
    }

    void sortBy(const std::string &key, bool ascending) {
        sortKey_       = key;
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
        if (selectedIndex_ < 0) return nullptr;
        int real = _realIndex(selectedIndex_);
        if (real < 0 || real >= (int)displayRows_->size()) return nullptr;
        return &(*displayRows_)[real];
    }

    // ── Layout ────────────────────────────────────────────────────────────────
    void computeLayout(HDC /*hdc*/, const BoxConstraints &constraints,
                       FontCache & /*fc*/) override {
        if (autoWidth)  width  = constraints.maxWidth;
        if (autoHeight) height = constraints.maxHeight;
        applyConstraints();
        needsLayout = false;
    }

    void positionChildren(int, int, int, int) override {}

    // ── Render ────────────────────────────────────────────────────────────────
    void render(HDC hdc, FontCache &fontCache) override {
        if (!visible) return;
        if (!displayRows_) return;

        HRGN clip = CreateRectRgn(x, y, x + width, y + height);
        SelectClipRgn(hdc, clip);
        DeleteObject(clip);

        _renderHeader(hdc, fontCache);
        _renderRows(hdc, fontCache);
        _renderScrollbars(hdc);

        // Outer border
        {
            HPEN   pen = CreatePen(PS_SOLID, 1, borderColor_);
            HPEN   old = (HPEN)SelectObject(hdc, pen);
            HBRUSH nb  = (HBRUSH)GetStockObject(NULL_BRUSH);
            HBRUSH ob  = (HBRUSH)SelectObject(hdc, nb);
            Rectangle(hdc, x, y, x + width, y + height);
            SelectObject(hdc, ob);
            SelectObject(hdc, old);
            DeleteObject(pen);
        }

        SelectClipRgn(hdc, nullptr);
        needsPaint = false;
    }

    // ── Mouse events ──────────────────────────────────────────────────────────

    bool handleMouseDown(int mx, int my) override {
        if (!_inBounds(mx, my)) return false;

        // ── Column resize drag ─────────────────────────────────────────────
        if (my >= y && my < y + headerHeight) {
            int col = _resizeHandleAt(mx);
            if (col >= 0) {
                _resizingCol_     = col;
                _resizeDragX_     = mx;
                _resizeDragWidth_ = colWidths_[col];
                if (HWND hw = _getHWND()) SetCapture(hw);
                return true;
            }

            // ── Header click → sort ──────────────────────────────────────
            int hcol = _headerColAt(mx);
            if (hcol >= 0 && columns_[hcol].sortable) {
                if (sortKey_ == columns_[hcol].key)
                    sortAscending_ = !sortAscending_;
                else {
                    sortKey_       = columns_[hcol].key;
                    sortAscending_ = true;
                }
                _rebuildSort();
                if (onSortChanged) onSortChanged(sortKey_, sortAscending_);
                if (auto *ui = FluxUI::getCurrentInstance())
                    ui->updateWidget(this);
            }
            return true;
        }

        // ── V-scrollbar drag ──────────────────────────────────────────────
        if (_needsVScrollbar() && mx >= x + width - scrollbarWidth_ - 1) {
            _vScrollDragging_   = true;
            _vScrollDragStartY_ = my;
            _vScrollDragStart_  = scrollOffsetY_;
            if (HWND hw = _getHWND()) SetCapture(hw);
            return true;
        }

        // ── H-scrollbar drag ──────────────────────────────────────────────
        if (_needsHScrollbar() && my >= y + height - scrollbarWidth_ - 1) {
            _hScrollDragging_   = true;
            _hScrollDragStartX_ = mx;
            _hScrollDragStart_  = scrollOffsetX_;
            if (HWND hw = _getHWND()) SetCapture(hw);
            return true;
        }

        // ── Row click ─────────────────────────────────────────────────────
        int row = _rowAt(my);
        if (row < 0) return true;

        int realIdx = _realIndex(row);
        if (realIdx < 0 || realIdx >= (int)displayRows_->size()) return true;
        if ((*displayRows_)[realIdx].disabled) return true;

        selectedIndex_ = row;
        markNeedsPaint();

        if (onRowSelected)
            onRowSelected(row, (*displayRows_)[realIdx]);

        // Double-click
        DWORD now = GetTickCount();
        if (lastClickRow_ == row && (now - lastClickTime_) < 400) {
            if (onRowDoubleClicked)
                onRowDoubleClicked(row, (*displayRows_)[realIdx]);
            lastClickRow_ = -1;
        } else {
            lastClickRow_  = row;
            lastClickTime_ = now;
        }

        if (auto *ui = FluxUI::getCurrentInstance())
            ui->updateWidget(this);
        return true;
    }

    bool handleMouseMove(int mx, int my) override {
        // Column resize drag
        if (_resizingCol_ >= 0) {
            int dx = mx - _resizeDragX_;
            int newW = max(columns_[_resizingCol_].minWidth,
                           _resizeDragWidth_ + dx);
            colWidths_[_resizingCol_] = newW;
            if (auto *ui = FluxUI::getCurrentInstance())
                ui->updateWidget(this);
            return true;
        }

        // V-scrollbar drag
        if (_vScrollDragging_) {
            int totalH   = _totalContentHeight();
            int viewH    = _contentAreaHeight();
            float ratio  = (float)(my - _vScrollDragStartY_) / (float)viewH;
            int newOff   = _vScrollDragStart_ + (int)(ratio * totalH);
            _clampVScroll(newOff);
            if (auto *ui = FluxUI::getCurrentInstance())
                ui->updateWidget(this);
            return true;
        }

        // H-scrollbar drag
        if (_hScrollDragging_) {
            int totalW   = _totalColumnsWidth();
            int viewW    = _contentAreaWidth();
            float ratio  = (float)(mx - _hScrollDragStartX_) / (float)viewW;
            int newOff   = _hScrollDragStart_ + (int)(ratio * totalW);
            _clampHScroll(newOff);
            if (auto *ui = FluxUI::getCurrentInstance())
                ui->updateWidget(this);
            return true;
        }

        // Resize cursor over column edge
        bool onHandle = _inBounds(mx, my) && _resizeHandleAt(mx) >= 0;
        if (onHandle != _showResizeCursor_) {
            _showResizeCursor_ = onHandle;
            if (HWND hw = _getHWND())
                SetCursor(LoadCursor(nullptr,
                    onHandle ? IDC_SIZEWE : IDC_ARROW));
        }

        // Row hover
        if (!_inBounds(mx, my)) {
            if (hoveredRow_ != -1) { hoveredRow_ = -1; markNeedsPaint(); }
            return false;
        }
        int row = _rowAt(my);
        if (row != hoveredRow_) {
            hoveredRow_ = row;
            if (auto *ui = FluxUI::getCurrentInstance())
                ui->updateWidget(this);
        }
        return false;
    }

    bool handleMouseUp(int /*mx*/, int /*my*/) override {
        bool handled = _resizingCol_ >= 0 || _vScrollDragging_ || _hScrollDragging_;
        _resizingCol_    = -1;
        _vScrollDragging_ = false;
        _hScrollDragging_ = false;
        if (handled) ReleaseCapture();
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
        if (!displayRows_ || displayRows_->empty()) return false;
        int count = (int)displayRows_->size();

        switch (keyCode) {
        case VK_UP:
            if (selectedIndex_ > 0) {
                selectedIndex_--;
                _ensureRowVisible(selectedIndex_);
                _fireSelection();
                if (auto *ui = FluxUI::getCurrentInstance())
                    ui->updateWidget(this);
            }
            return true;
        case VK_DOWN:
            if (selectedIndex_ < count - 1) {
                selectedIndex_++;
                _ensureRowVisible(selectedIndex_);
                _fireSelection();
                if (auto *ui = FluxUI::getCurrentInstance())
                    ui->updateWidget(this);
            }
            return true;
        case VK_HOME:
            selectedIndex_ = 0;
            _ensureRowVisible(0);
            _fireSelection();
            if (auto *ui = FluxUI::getCurrentInstance())
                ui->updateWidget(this);
            return true;
        case VK_END:
            selectedIndex_ = count - 1;
            _ensureRowVisible(selectedIndex_);
            _fireSelection();
            if (auto *ui = FluxUI::getCurrentInstance())
                ui->updateWidget(this);
            return true;
        case VK_PRIOR: { // Page Up
            int page = max(1, _contentAreaHeight() / rowHeight);
            selectedIndex_ = max(0, selectedIndex_ - page);
            _ensureRowVisible(selectedIndex_);
            _fireSelection();
            if (auto *ui = FluxUI::getCurrentInstance())
                ui->updateWidget(this);
            return true;
        }
        case VK_NEXT: { // Page Down
            int page = max(1, _contentAreaHeight() / rowHeight);
            selectedIndex_ = min(count - 1, selectedIndex_ + page);
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
        alternateRows = v; markNeedsPaint(); return self_();
    }
    std::shared_ptr<DataTableWidget> setShowColumnDividers(bool v) {
        showColumnDividers = v; markNeedsPaint(); return self_();
    }
    std::shared_ptr<DataTableWidget> setRowHeight(int h) {
        rowHeight = h; markNeedsLayout(); return self_();
    }
    std::shared_ptr<DataTableWidget> setHeaderHeight(int h) {
        headerHeight = h; markNeedsLayout(); return self_();
    }
    std::shared_ptr<DataTableWidget> setHeaderBackground(COLORREF c) {
        headerBg = c; markNeedsPaint(); return self_();
    }
    std::shared_ptr<DataTableWidget> setAccentColor(COLORREF c) {
        accentColor     = c;
        rowSelectColor  = RGB(
            (int)(GetRValue(c) * 0.82f + 255 * 0.18f),
            (int)(GetGValue(c) * 0.82f + 255 * 0.18f),
            (int)(GetBValue(c) * 0.82f + 255 * 0.18f)
        );
        markNeedsPaint();
        return self_();
    }
    std::shared_ptr<DataTableWidget> setOnRowSelected(
            std::function<void(int, const DataRow &)> fn) {
        onRowSelected = std::move(fn); return self_();
    }
    std::shared_ptr<DataTableWidget> setOnRowDoubleClicked(
            std::function<void(int, const DataRow &)> fn) {
        onRowDoubleClicked = std::move(fn); return self_();
    }
    std::shared_ptr<DataTableWidget> setOnSortChanged(
            std::function<void(const std::string &, bool)> fn) {
        onSortChanged = std::move(fn); return self_();
    }
    std::shared_ptr<DataTableWidget> setFlex(int f) {
        flex = f; return self_();
    }
    std::shared_ptr<DataTableWidget> setWidth(int w) {
        width = w; autoWidth = false; markNeedsLayout(); return self_();
    }
    std::shared_ptr<DataTableWidget> setHeight(int h) {
        height = h; autoHeight = false; markNeedsLayout(); return self_();
    }

private:
    // ── Data ──────────────────────────────────────────────────────────────────
    std::vector<DataColumn>      columns_;
    std::vector<int>             colWidths_;   // live widths (may be resized)

    std::vector<DataRow>              staticRows_;
    State<std::vector<DataRow>>      *reactiveState_ = nullptr; // owning state
    const std::vector<DataRow>       *displayRows_   = nullptr;
    bool                              useReactive_   = false;

    // Sorted index mapping: sortedIndices_[visualRow] = originalIndex
    // If empty, no sort is active.
    std::vector<int>             sortedIndices_;
    std::string                  sortKey_;
    bool                         sortAscending_ = true;

    // ── Interaction state ─────────────────────────────────────────────────────
    int   selectedIndex_    = -1;
    int   hoveredRow_       = -1;
    int   scrollOffsetY_    = 0;
    int   scrollOffsetX_    = 0;

    // Column resize
    int   _resizingCol_       = -1;
    int   _resizeDragX_       = 0;
    int   _resizeDragWidth_   = 0;
    bool  _showResizeCursor_  = false;

    // V-scroll drag
    bool  _vScrollDragging_   = false;
    int   _vScrollDragStartY_ = 0;
    int   _vScrollDragStart_  = 0;

    // H-scroll drag
    bool  _hScrollDragging_   = false;
    int   _hScrollDragStartX_ = 0;
    int   _hScrollDragStart_  = 0;

    // Double-click
    int   lastClickRow_   = -1;
    DWORD lastClickTime_  = 0;

    // ── Helpers ───────────────────────────────────────────────────────────────

    std::shared_ptr<DataTableWidget> self_() {
        return std::static_pointer_cast<DataTableWidget>(shared_from_this());
    }

    HWND _getHWND() const {
        if (auto *ui = FluxUI::getCurrentInstance()) return ui->getWindow();
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
        return height - headerHeight - (_needsHScrollbar() ? scrollbarWidth_ + 1 : 0);
    }

    int _totalColumnsWidth() const {
        int total = 0;
        for (int w : colWidths_) total += w;
        return total;
    }
    int _totalContentHeight() const {
        return displayRows_ ? (int)displayRows_->size() * rowHeight : 0;
    }

    bool _needsVScrollbar() const {
        return _totalContentHeight() > height - headerHeight;
    }
    bool _needsHScrollbar() const {
        return _totalColumnsWidth() > width;
    }

    void _clampVScroll(int off) {
        int maxOff = max(0, _totalContentHeight() - _contentAreaHeight());
        scrollOffsetY_ = max(0, min(maxOff, off));
        markNeedsPaint();
    }
    void _clampHScroll(int off) {
        int maxOff = max(0, _totalColumnsWidth() - _contentAreaWidth());
        scrollOffsetX_ = max(0, min(maxOff, off));
        markNeedsPaint();
    }

    // Map visual row index → original data index (accounting for sort)
    int _realIndex(int visualRow) const {
        if (sortedIndices_.empty()) return visualRow;
        if (visualRow < 0 || visualRow >= (int)sortedIndices_.size())
            return visualRow;
        return sortedIndices_[visualRow];
    }

    // Row visual index from screen Y
    int _rowAt(int screenY) const {
        int relY = screenY - y - headerHeight + scrollOffsetY_;
        if (relY < 0) return -1;
        int row = relY / rowHeight;
        if (!displayRows_ || row >= (int)displayRows_->size()) return -1;
        return row;
    }

    // Which column header was clicked?
    int _headerColAt(int screenX) const {
        int cx = x - scrollOffsetX_;
        for (int i = 0; i < (int)columns_.size(); i++) {
            if (screenX >= cx && screenX < cx + colWidths_[i]) return i;
            cx += colWidths_[i];
        }
        return -1;
    }

    // Returns column index whose right edge is within 4px, for resize handle
    int _resizeHandleAt(int screenX) const {
        if (!columns_.empty() && !columns_[0].resizable &&
            columns_.size() == 1) return -1;
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
        if (!displayRows_ || selectedIndex_ < 0) return;
        int real = _realIndex(selectedIndex_);
        if (real < 0 || real >= (int)displayRows_->size()) return;
        if (onRowSelected)
            onRowSelected(selectedIndex_, (*displayRows_)[real]);
    }

    // ── Sort ──────────────────────────────────────────────────────────────────

    // Returns true if a string is purely numeric (integer or float)
    static bool _isNumeric(const std::string &s) {
        if (s.empty()) return false;
        char *end = nullptr;
        std::strtod(s.c_str(), &end);
        return end == s.c_str() + s.size();
    }

    void _rebuildSort() {
        if (!displayRows_) return;
        if (sortKey_.empty()) {
            sortedIndices_.clear();
            return;
        }

        int n = (int)displayRows_->size();
        sortedIndices_.resize(n);
        for (int i = 0; i < n; i++) sortedIndices_[i] = i;

        const std::string &key = sortKey_;
        bool ascending         = sortAscending_;
        const auto &rows       = *displayRows_;

        std::stable_sort(sortedIndices_.begin(), sortedIndices_.end(),
            [&](int a, int b) {
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

    void _renderHeader(HDC hdc, FontCache &fontCache) const {
        int contentW = _contentAreaWidth();

        // Header background
        {
            HBRUSH hb = CreateSolidBrush(headerBg);
            RECT   hr = {x, y, x + contentW, y + headerHeight};
            FillRect(hdc, &hr, hb);
            DeleteObject(hb);
        }

        // Bottom border under header
        {
            HPEN pen = CreatePen(PS_SOLID, 1, borderColor_);
            HPEN old = (HPEN)SelectObject(hdc, pen);
            MoveToEx(hdc, x,           y + headerHeight, nullptr);
            LineTo  (hdc, x + contentW, y + headerHeight);
            SelectObject(hdc, old);
            DeleteObject(pen);
        }

        HFONT hFont    = fontCache.getFont(fontSize_, FontWeight::Bold);
        HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, headerTextColor);

        int cx = x - scrollOffsetX_;

        for (int i = 0; i < (int)columns_.size(); i++) {
            int cw   = colWidths_[i];
            int cRight = cx + cw;
            int cLeft  = max(cx, x);

            if (cLeft < x + contentW && cRight > x) {
                // Clip label to visible portion
                HRGN colClip = CreateRectRgn(cLeft, y, min(cRight, x + contentW), y + headerHeight);
                SelectClipRgn(hdc, colClip);
                DeleteObject(colClip);

                // Sort indicator
                bool isSortCol = (sortKey_ == columns_[i].key);
                std::string label = columns_[i].label;
                if (isSortCol && columns_[i].sortable)
                    label += (sortAscending_ ? "  \u25B2" : "  \u25BC");

                RECT lr = {cLeft + 8, y, min(cRight - 8, x + contentW), y + headerHeight};
                UINT fmt = DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS;
                switch (columns_[i].align) {
                case ColumnAlign::Center: fmt |= DT_CENTER; break;
                case ColumnAlign::Right:  fmt |= DT_RIGHT;  break;
                default:                  fmt |= DT_LEFT;   break;
                }

                // Draw sort accent line for active column
                if (isSortCol) {
                    HBRUSH ab = CreateSolidBrush(accentColor);
                    RECT   ar = {cLeft, y + headerHeight - 2,
                                 min(cRight, x + contentW), y + headerHeight};
                    FillRect(hdc, &ar, ab);
                    DeleteObject(ab);
                    SetTextColor(hdc, accentColor);
                } else {
                    SetTextColor(hdc, headerTextColor);
                }

                DrawTextA(hdc, label.c_str(), -1, &lr, fmt);

                SelectClipRgn(hdc, nullptr);

                // Column dividers
                if (showColumnDividers && i < (int)columns_.size() - 1) {
                    HPEN dp  = CreatePen(PS_SOLID, 1, dividerColor_);
                    HPEN old = (HPEN)SelectObject(hdc, dp);
                    int  lx  = min(cRight, x + contentW);
                    MoveToEx(hdc, lx, y + 6,              nullptr);
                    LineTo  (hdc, lx, y + headerHeight - 6);
                    SelectObject(hdc, old);
                    DeleteObject(dp);
                }
            }

            cx += cw;
        }

        SelectObject(hdc, hOldFont);
        // Restore full clip
        HRGN fullClip = CreateRectRgn(x, y, x + width, y + height);
        SelectClipRgn(hdc, fullClip);
        DeleteObject(fullClip);
    }

    void _renderRows(HDC hdc, FontCache &fontCache) const {
        if (!displayRows_ || displayRows_->empty()) return;

        int contentW = _contentAreaWidth();
        int contentH = _contentAreaHeight();
        int bodyY    = y + headerHeight;

        // Clip to body
        HRGN bodyClip = CreateRectRgn(x, bodyY, x + contentW, bodyY + contentH);
        SelectClipRgn(hdc, bodyClip);
        DeleteObject(bodyClip);

        HFONT hFont    = fontCache.getFont(fontSize_, FontWeight::Normal);
        HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);
        SetBkMode(hdc, TRANSPARENT);

        int firstRow = scrollOffsetY_ / rowHeight;
        int lastRow  = min((int)displayRows_->size() - 1,
                            (scrollOffsetY_ + contentH) / rowHeight);

        for (int vi = firstRow; vi <= lastRow; vi++) {
            int real = _realIndex(vi);
            if (real < 0 || real >= (int)displayRows_->size()) continue;

            const DataRow &row   = (*displayRows_)[real];
            int            rowY  = bodyY + vi * rowHeight - scrollOffsetY_;
            bool           isSel = (vi == selectedIndex_);
            bool           isHov = (vi == hoveredRow_ && !isSel);

            // Row background
            COLORREF bg;
            if (isSel)
                bg = rowSelectColor;
            else if (isHov)
                bg = rowHoverColor;
            else if (alternateRows && vi % 2 == 1)
                bg = rowAltBgColor;
            else
                bg = rowBgColor;

            {
                HBRUSH rb = CreateSolidBrush(bg);
                RECT   rr = {x, rowY, x + contentW, rowY + rowHeight};
                FillRect(hdc, &rr, rb);
                DeleteObject(rb);
            }

            // Selection accent bar
            if (isSel) {
                HBRUSH ab = CreateSolidBrush(accentColor);
                RECT   ar = {x, rowY, x + 3, rowY + rowHeight};
                FillRect(hdc, &ar, ab);
                DeleteObject(ab);
            }

            // Row cells
            int cx = x - scrollOffsetX_;
            for (int ci = 0; ci < (int)columns_.size(); ci++) {
                int cw     = colWidths_[ci];
                int cRight = cx + cw;
                int cLeft  = max(cx, x);

                if (cLeft < x + contentW && cRight > x) {
                    HRGN cellClip = CreateRectRgn(cLeft, rowY,
                                                   min(cRight, x + contentW),
                                                   rowY + rowHeight);
                    SelectClipRgn(hdc, cellClip);
                    DeleteObject(cellClip);

                    std::string val = columns_[ci].format(row.get(columns_[ci].key));

                    SetTextColor(hdc, row.disabled ? disabledColor_ : textColor_);

                    RECT lr = {cLeft + 8, rowY,
                               min(cRight - 8, x + contentW), rowY + rowHeight};
                    UINT fmt = DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS;
                    switch (columns_[ci].align) {
                    case ColumnAlign::Center: fmt |= DT_CENTER; break;
                    case ColumnAlign::Right:  fmt |= DT_RIGHT;  break;
                    default:                  fmt |= DT_LEFT;   break;
                    }
                    DrawTextA(hdc, val.c_str(), -1, &lr, fmt);

                    SelectClipRgn(hdc, bodyClip = CreateRectRgn(
                        x, bodyY, x + contentW, bodyY + contentH));
                    DeleteObject(bodyClip);

                    // Column dividers
                    if (showColumnDividers && ci < (int)columns_.size() - 1) {
                        HPEN dp  = CreatePen(PS_SOLID, 1, dividerColor_);
                        HPEN old = (HPEN)SelectObject(hdc, dp);
                        int  lx  = min(cRight, x + contentW - 1);
                        MoveToEx(hdc, lx, rowY + 4,              nullptr);
                        LineTo  (hdc, lx, rowY + rowHeight - 4);
                        SelectObject(hdc, old);
                        DeleteObject(dp);
                    }
                }

                cx += cw;
            }

            // Bottom row separator
            {
                HPEN sep = CreatePen(PS_SOLID, 1, borderColor_);
                HPEN old = (HPEN)SelectObject(hdc, sep);
                MoveToEx(hdc, x,           rowY + rowHeight - 1, nullptr);
                LineTo  (hdc, x + contentW, rowY + rowHeight - 1);
                SelectObject(hdc, old);
                DeleteObject(sep);
            }
        }

        SelectObject(hdc, hOldFont);

        // Fill empty area below rows
        {
            int lastRowBottom = bodyY + ((int)displayRows_->size()) * rowHeight - scrollOffsetY_;
            if (lastRowBottom < bodyY + contentH) {
                HBRUSH eb = CreateSolidBrush(rowBgColor);
                RECT   er = {x, lastRowBottom, x + contentW, bodyY + contentH};
                FillRect(hdc, &er, eb);
                DeleteObject(eb);
            }
        }

        // Restore clip to widget bounds
        HRGN fullClip = CreateRectRgn(x, y, x + width, y + height);
        SelectClipRgn(hdc, fullClip);
        DeleteObject(fullClip);
    }

    void _renderScrollbars(HDC hdc) const {
        bool needsV = _needsVScrollbar();
        bool needsH = _needsHScrollbar();

        // Vertical scrollbar
        if (needsV) {
            int sbX  = x + width - scrollbarWidth_ - 1;
            int sbY  = y + headerHeight;
            int sbH  = height - headerHeight - (needsH ? scrollbarWidth_ + 1 : 0);
            int total = _totalContentHeight();

            // Track
            HBRUSH tb = CreateSolidBrush(RGB(240, 240, 240));
            RECT   tr = {sbX, sbY, sbX + scrollbarWidth_, sbY + sbH};
            FillRect(hdc, &tr, tb);
            DeleteObject(tb);

            float thumbRatio  = (float)sbH / (float)total;
            int   thumbH      = max(20, (int)(sbH * thumbRatio));
            float scrollRatio = (total > sbH)
                ? (float)scrollOffsetY_ / (float)(total - sbH) : 0.f;
            int   thumbY      = sbY + (int)(scrollRatio * (sbH - thumbH));

            HBRUSH thb = CreateSolidBrush(accentColor);
            HRGN   rg  = CreateRoundRectRgn(sbX + 2, thumbY,
                                             sbX + scrollbarWidth_ - 1,
                                             thumbY + thumbH, 4, 4);
            FillRgn(hdc, rg, thb);
            DeleteObject(rg);
            DeleteObject(thb);
        }

        // Horizontal scrollbar
        if (needsH) {
            int sbX  = x;
            int sbY  = y + height - scrollbarWidth_ - 1;
            int sbW  = width - (needsV ? scrollbarWidth_ + 1 : 0);
            int total = _totalColumnsWidth();

            HBRUSH tb = CreateSolidBrush(RGB(240, 240, 240));
            RECT   tr = {sbX, sbY, sbX + sbW, sbY + scrollbarWidth_};
            FillRect(hdc, &tr, tb);
            DeleteObject(tb);

            float thumbRatio  = (float)sbW / (float)total;
            int   thumbW      = max(20, (int)(sbW * thumbRatio));
            float scrollRatio = (total > sbW)
                ? (float)scrollOffsetX_ / (float)(total - sbW) : 0.f;
            int   thumbX      = sbX + (int)(scrollRatio * (sbW - thumbW));

            HBRUSH thb = CreateSolidBrush(accentColor);
            HRGN   rg  = CreateRoundRectRgn(thumbX, sbY + 2,
                                             thumbX + thumbW,
                                             sbY + scrollbarWidth_ - 1, 4, 4);
            FillRgn(hdc, rg, thb);
            DeleteObject(rg);
            DeleteObject(thb);
        }

        // Corner fill when both scrollbars are present
        if (needsV && needsH) {
            HBRUSH cb = CreateSolidBrush(RGB(240, 240, 240));
            RECT   cr = {x + width - scrollbarWidth_ - 1,
                         y + height - scrollbarWidth_ - 1,
                         x + width,
                         y + height};
            FillRect(hdc, &cr, cb);
            DeleteObject(cb);
        }
    }
};

// ============================================================================
// FACTORY FUNCTIONS
// ============================================================================

using DataTableWidgetPtr = std::shared_ptr<DataTableWidget>;

// Static rows
inline DataTableWidgetPtr DataTable(std::vector<DataColumn> columns,
                                    std::vector<DataRow>    rows) {
    return std::make_shared<DataTableWidget>(std::move(columns),
                                             std::move(rows));
}

// Reactive rows
inline DataTableWidgetPtr DataTable(std::vector<DataColumn>         columns,
                                    State<std::vector<DataRow>>     &state) {
    return std::make_shared<DataTableWidget>(std::move(columns), state);
}

#endif // FLUX_DATA_TABLE_HPP