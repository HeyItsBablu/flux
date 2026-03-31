#ifndef FLUX_TREE_VIEW_HPP
#define FLUX_TREE_VIEW_HPP

#include "flux_core.hpp"
#include "flux_layout.hpp"
#include "flux_state.hpp"
#include <functional>
#include <string>
#include <vector>

// ============================================================================
// TREE NODE
// ============================================================================
//
// Nodes are owned by the caller and passed by value into TreeView.
// Children are stored by index into the same flat vector (like a DOM).
//
// Usage:
//   TreeNode root("src");
//   root.addChild(TreeNode("main.cpp"));
//   auto &components = root.addChild(TreeNode("components"));
//   components.addChild(TreeNode("flux_widget.hpp"));
//   components.addChild(TreeNode("flux_layout.hpp"));
// ============================================================================

struct TreeNode {
  std::string label;
  std::string id;    // optional unique id for selection
  std::wstring icon; // optional Segoe MDL2 glyph e.g. L"\uE8B7"
  bool expanded = false;
  bool disabled = false;
  std::vector<TreeNode> children;

  // User data — attach anything (file path, object pointer, etc.)
  void *userData = nullptr;

  explicit TreeNode(const std::string &lbl, const std::string &nodeId = "")
      : label(lbl), id(nodeId) {}

  TreeNode &addChild(TreeNode child) {
    children.push_back(std::move(child));
    return children.back();
  }

  bool isLeaf() const { return children.empty(); }

  // Expand/collapse all descendants
  void expandAll() {
    expanded = true;
    for (auto &c : children)
      c.expandAll();
  }
  void collapseAll() {
    expanded = false;
    for (auto &c : children)
      c.collapseAll();
  }
};

// ============================================================================
// TREE VIEW WIDGET
// ============================================================================
//
// Scrollable, flat-rendered tree with expand/collapse, single selection,
// keyboard navigation, and optional indent guide lines.
//
// Usage:
//   TreeNode root("Project");
//   root.addChild(TreeNode("src")).addChild(TreeNode("main.cpp"));
//
//   auto tv = TreeView(root)
//                 ->setOnSelectionChanged([](const TreeNode *n) {
//                     std::cout << n->label << std::endl;
//                 });
//
// Multiple roots:
//   auto tv = TreeView({rootA, rootB, rootC});
// ============================================================================

class TreeViewWidget : public Widget {
public:
  // ── Appearance ────────────────────────────────────────────────────────────
  int rowHeight = 28;
  int indentWidth = 20; // pixels per depth level
  int iconSize = 14;
  int arrowSize = 8;
  int scrollbarWidth = 8;

  Color rowBgColor = Color::fromRGB(255, 255, 255);
  Color rowHoverColor = Color::fromRGB(232, 244, 255);
  Color rowSelectColor = Color::fromRGB(210, 235, 255);
  Color rowSelectBorder = Color::fromRGB(33, 150, 243);
  Color textColor = Color::fromRGB(30, 30, 30);
  Color disabledColor = Color::fromRGB(170, 170, 170);
  Color arrowColor = Color::fromRGB(100, 100, 100);
  Color iconColor = Color::fromRGB(80, 80, 80);
  Color guideLineColor = Color::fromRGB(210, 210, 210);
  Color scrollbarColor = Color::fromRGB(180, 180, 180);
  Color scrollbarHover = Color::fromRGB(140, 140, 140);

  bool showGuideLines = true;
  bool showIcons = true;
  int fontSize = 13;

  // ── Callbacks ─────────────────────────────────────────────────────────────
  std::function<void(const TreeNode *)> onSelectionChanged;
  std::function<void(const TreeNode *)> onNodeExpanded;
  std::function<void(const TreeNode *)> onNodeCollapsed;
  std::function<void(const TreeNode *)> onNodeDoubleClicked;
  std::function<bool(const TreeNode *)>
      onRightClickNode; // return true = handled

  // ── Constructor ───────────────────────────────────────────────────────────
  explicit TreeViewWidget(std::vector<TreeNode> roots)
      : roots_(std::move(roots)) {
    _buildFlatList();
  }

  // ── Public API ────────────────────────────────────────────────────────────

  // Replace the entire tree
  void setRoots(std::vector<TreeNode> newRoots) {
    roots_ = std::move(newRoots);
    selectedNode_ = nullptr;
    hoveredRow_ = -1;
    scrollOffset_ = 0;
    _buildFlatList();
    markNeedsLayout();
    if (auto *ui = FluxUI::getCurrentInstance())
      ui->updateWidget(this);
  }

  // Select a node by id
  void selectById(const std::string &id) {
    for (auto &row : flatList_) {
      if (row.node->id == id) {
        selectedNode_ = row.node;
        markNeedsPaint();
        return;
      }
    }
  }

  // Expand/collapse all
  void expandAll() {
    for (auto &r : roots_)
      r.expandAll();
    _buildFlatList();
    if (auto *ui = FluxUI::getCurrentInstance())
      ui->updateWidget(this);
  }
  void collapseAll() {
    for (auto &r : roots_)
      r.collapseAll();
    selectedNode_ = nullptr;
    scrollOffset_ = 0;
    _buildFlatList();
    if (auto *ui = FluxUI::getCurrentInstance())
      ui->updateWidget(this);
  }

  const TreeNode *selectedNode() const { return selectedNode_; }

  // ── Layout ────────────────────────────────────────────────────────────────
  void computeLayout(GraphicsContext & /*ctx*/,
                     const BoxConstraints &constraints,
                     FontCache & /*fc*/) override {
    if (autoWidth)
      width = constraints.maxWidth;
    if (autoHeight)
      height = constraints.maxHeight;
    if (width < 1)
      width = 1;
    if (height < 1)
      height = 1;
    applyConstraints();
    needsLayout = false;
  }

  void positionChildren(int, int, int, int) override {}

  // ── Render ────────────────────────────────────────────────────────────────
  void render(GraphicsContext &ctx, FontCache &fontCache) override {
    if (!visible)
      return;
    Painter painter(ctx);

    painter.pushClipRect(x, y, width, height);
    painter.fillRect(x, y, width, height, rowBgColor);

    int visibleH = height;
    int contentW = width - (needsScrollbar_() ? scrollbarWidth + 2 : 0);
    int firstVisible = scrollOffset_ / rowHeight;
    int lastVisible =
        min((int)flatList_.size() - 1, (scrollOffset_ + visibleH) / rowHeight);

    for (int i = firstVisible; i <= lastVisible; i++)
      _renderRow(ctx, fontCache, i, contentW);

    painter.popClipRect();

    if (needsScrollbar_())
      _renderScrollbar(ctx);

    needsPaint = false;
  }
  // ── Mouse events ──────────────────────────────────────────────────────────

  bool handleMouseDown(int mx, int my) override {
    if (!_inBounds(mx, my))
      return false;
    auto *ui = FluxUI::getCurrentInstance();

    if (needsScrollbar_() && mx >= x + width - scrollbarWidth - 2) {
      _scrollDragging = true;
      _scrollDragStartY = my;
      _scrollDragStartOffset = scrollOffset_;
      if (ui)
        ui->captureMouseInput();
      return true;
    }

    int row = _rowAt(my);
    if (row < 0 || row >= (int)flatList_.size())
      return false;

    auto &entry = flatList_[row];
    if (entry.node->disabled)
      return true;

    int indentX = x + entry.depth * indentWidth;
    int arrowEnd = indentX + indentWidth;

    if (!entry.node->isLeaf() && mx >= indentX && mx < arrowEnd) {
      _toggleExpand(entry.node);
      return true;
    }

    _select(entry.node);

    uint32_t now = platformTickCount();
    if (lastClickRow_ == row && (now - lastClickTime_) < 400) {
      if (onNodeDoubleClicked)
        onNodeDoubleClicked(entry.node);
      if (!entry.node->isLeaf())
        _toggleExpand(entry.node);
      lastClickRow_ = -1;
    } else {
      lastClickRow_ = row;
      lastClickTime_ = now;
    }
    return true;
  }

  bool handleMouseMove(int mx, int my) override {
    // Scrollbar drag
    if (_scrollDragging) {
      int dy = my - _scrollDragStartY;
      int totalH = (int)flatList_.size() * rowHeight;
      float ratio = (float)dy / (float)height;
      int newOffset = _scrollDragStartOffset + (int)(ratio * totalH);
      _clampScroll(newOffset);
      if (auto *ui = FluxUI::getCurrentInstance())
        ui->updateWidget(this);
      return true;
    }

    // Scrollbar hover
    bool overSB = needsScrollbar_() && mx >= x + width - scrollbarWidth - 2;
    if (overSB != _scrollbarHovered) {
      _scrollbarHovered = overSB;
      markNeedsPaint();
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
      if (auto *ui = FluxUI::getCurrentInstance())
        ui->updateWidget(this);
    }
    return false;
  }

  bool handleMouseUp(int /*mx*/, int /*my*/) override {
    if (_scrollDragging) {
      _scrollDragging = false;
      if (auto *ui = FluxUI::getCurrentInstance())
        ui->releaseMouseInput();
      return true;
    }
    return false;
  }

  bool handleMouseLeave() override {
    hoveredRow_ = -1;
    markNeedsPaint();
    return false;
  }

  bool handleMouseWheel(int delta) override {
    int newOffset = scrollOffset_ - (delta / 120) * rowHeight * 3;
    _clampScroll(newOffset);
    if (auto *ui = FluxUI::getCurrentInstance())
      ui->updateWidget(this);
    return true;
  }

  bool handleKeyDown(int keyCode) override {
    if (flatList_.empty())
      return false;

    int cur = _selectedRowIndex();

    switch (keyCode) {
    case Key::Up:
      if (cur > 0) {
        _select(flatList_[cur - 1].node);
        _ensureVisible(cur - 1);
      }
      return true;
    case Key::Down:
      if (cur < (int)flatList_.size() - 1) {
        _select(flatList_[cur + 1].node);
        _ensureVisible(cur + 1);
      }
      return true;
    case Key::Left:
      if (cur >= 0) {
        auto *n = flatList_[cur].node;
        if (!n->isLeaf() && n->expanded)
          _toggleExpand(n);
        else if (flatList_[cur].depth > 0) {
          // Jump to parent
          int d = flatList_[cur].depth;
          for (int i = cur - 1; i >= 0; i--) {
            if (flatList_[i].depth == d - 1) {
              _select(flatList_[i].node);
              _ensureVisible(i);
              break;
            }
          }
        }
      }
      return true;
    case Key::Right:
      if (cur >= 0) {
        auto *n = flatList_[cur].node;
        if (!n->isLeaf() && !n->expanded)
          _toggleExpand(n);
        else if (!n->isLeaf() && n->expanded &&
                 cur + 1 < (int)flatList_.size()) {
          _select(flatList_[cur + 1].node);
          _ensureVisible(cur + 1);
        }
      }
      return true;
    case Key::Home:
      if (!flatList_.empty()) {
        _select(flatList_[0].node);
        _ensureVisible(0);
      }
      return true;
    case Key::End: {
      int last = (int)flatList_.size() - 1;
      _select(flatList_[last].node);
      _ensureVisible(last);
      return true;
    }
    case Key::Return:
    case Key::Space:
      if (cur >= 0 && !flatList_[cur].node->isLeaf())
        _toggleExpand(flatList_[cur].node);
      return true;
    }
    return false;
  }

  // ── Fluent setters ────────────────────────────────────────────────────────

  std::shared_ptr<TreeViewWidget>
  setOnSelectionChanged(std::function<void(const TreeNode *)> cb) {
    onSelectionChanged = std::move(cb);
    return self_();
  }
  std::shared_ptr<TreeViewWidget>
  setOnNodeExpanded(std::function<void(const TreeNode *)> cb) {
    onNodeExpanded = std::move(cb);
    return self_();
  }
  std::shared_ptr<TreeViewWidget>
  setOnNodeCollapsed(std::function<void(const TreeNode *)> cb) {
    onNodeCollapsed = std::move(cb);
    return self_();
  }
  std::shared_ptr<TreeViewWidget>
  setOnNodeDoubleClicked(std::function<void(const TreeNode *)> cb) {
    onNodeDoubleClicked = std::move(cb);
    return self_();
  }
  std::shared_ptr<TreeViewWidget> setRowHeight(int h) {
    rowHeight = h;
    markNeedsLayout();
    return self_();
  }
  std::shared_ptr<TreeViewWidget> setIndentWidth(int w) {
    indentWidth = w;
    markNeedsLayout();
    return self_();
  }
  std::shared_ptr<TreeViewWidget> setShowGuideLines(bool v) {
    showGuideLines = v;
    markNeedsPaint();
    return self_();
  }
  std::shared_ptr<TreeViewWidget> setFontSize(int s) {
    fontSize = s;
    markNeedsLayout();
    return self_();
  }
std::shared_ptr<TreeViewWidget> setAccentColor(Color c) {
    // Derive a lighter tint for the selection background
    rowSelectColor  = Color::fromRGB(
        (uint8_t)(c.r / 5 * 4),
        (uint8_t)(min(255, c.g / 5 * 4 + 20)),
        (uint8_t)(min(255, c.b / 5 * 4 + 40))
    );
    rowSelectBorder = c;
    markNeedsPaint();
    return self_();
}
  std::shared_ptr<TreeViewWidget> setWidth(int w) {
    width = w;
    autoWidth = false;
    markNeedsLayout();
    return self_();
  }
  std::shared_ptr<TreeViewWidget> setHeight(int h) {
    height = h;
    autoHeight = false;
    markNeedsLayout();
    return self_();
  }
  std::shared_ptr<TreeViewWidget> setFlex(int f) {
    flex = f;
    return self_();
  }

private:
  // ── Flat list entry ───────────────────────────────────────────────────────
  struct FlatEntry {
    TreeNode *node = nullptr;
    int depth = 0;
  };

  std::vector<TreeNode> roots_;
  std::vector<FlatEntry> flatList_;
  TreeNode *selectedNode_ = nullptr;
  int hoveredRow_ = -1;
  int scrollOffset_ = 0;

  // Double-click tracking
  int lastClickRow_ = -1;
  uint32_t lastClickTime_ = 0;

  // Scrollbar drag
  bool _scrollDragging = false;
  bool _scrollbarHovered = false;
  int _scrollDragStartY = 0;
  int _scrollDragStartOffset = 0;

  std::shared_ptr<TreeViewWidget> self_() {
    return std::static_pointer_cast<TreeViewWidget>(shared_from_this());
  }

  HWND _getHWND() const {
    if (auto *ui = FluxUI::getCurrentInstance())
      return ui->getWindow();
    return nullptr;
  }

  bool _inBounds(int mx, int my) const {
    return mx >= x && mx < x + width && my >= y && my < y + height;
  }

  // ── Flat list builder ─────────────────────────────────────────────────────
  void _buildFlatList() {
    flatList_.clear();
    for (auto &root : roots_)
      _appendNode(&root, 0);
  }

  void _appendNode(TreeNode *node, int depth) {
    flatList_.push_back({node, depth});
    if (node->expanded) {
      for (auto &child : node->children)
        _appendNode(&child, depth + 1);
    }
  }

  // ── Geometry helpers ──────────────────────────────────────────────────────

  int _rowAt(int screenY) const {
    int relY = screenY - y + scrollOffset_;
    int row = relY / rowHeight;
    if (row < 0 || row >= (int)flatList_.size())
      return -1;
    return row;
  }

  bool needsScrollbar_() const {
    return (int)flatList_.size() * rowHeight > height;
  }

  void _clampScroll(int newOffset) {
    int maxScroll = max(0, (int)flatList_.size() * rowHeight - height);
    scrollOffset_ = max(0, min(maxScroll, newOffset));
    markNeedsPaint();
  }

  void _ensureVisible(int row) {
    int rowTop = row * rowHeight;
    int rowBot = rowTop + rowHeight;
    if (rowTop < scrollOffset_)
      _clampScroll(rowTop);
    else if (rowBot > scrollOffset_ + height)
      _clampScroll(rowBot - height);
  }

  int _selectedRowIndex() const {
    for (int i = 0; i < (int)flatList_.size(); i++)
      if (flatList_[i].node == selectedNode_)
        return i;
    return -1;
  }

  // ── Actions ───────────────────────────────────────────────────────────────

  void _select(TreeNode *node) {
    if (selectedNode_ == node)
      return;
    selectedNode_ = node;
    if (onSelectionChanged)
      onSelectionChanged(node);
    markNeedsPaint();
  }

  void _toggleExpand(TreeNode *node) {
    node->expanded = !node->expanded;
    _buildFlatList();
    if (node->expanded) {
      if (onNodeExpanded)
        onNodeExpanded(node);
    } else {
      if (onNodeCollapsed)
        onNodeCollapsed(node);
    }
    if (auto *ui = FluxUI::getCurrentInstance())
      ui->updateWidget(this);
  }

  // ── Rendering ─────────────────────────────────────────────────────────────

  void _renderRow(GraphicsContext &ctx, FontCache &fc, int rowIdx,
                  int contentW) const {
    const auto &entry = flatList_[rowIdx];
    const TreeNode *node = entry.node;
    Painter painter(ctx);

    int rowY = y + rowIdx * rowHeight - scrollOffset_;
    int rowX = x;
    bool isSel = (node == selectedNode_);
    bool isHov = (rowIdx == hoveredRow_ && !isSel);

    // Row background
    if (isSel)
      painter.fillRectWithLeftAccent(rowX, rowY, contentW, rowHeight,
                                     rowSelectColor, rowSelectBorder, 3);
    else if (isHov)
      painter.fillRect(rowX, rowY, contentW, rowHeight, rowHoverColor);

    // Indent guide lines
    if (showGuideLines && entry.depth > 0) {
      for (int d = 1; d <= entry.depth; d++) {
        int gx = rowX + d * indentWidth - indentWidth / 2;
        painter.drawVLine(gx, rowY, rowHeight, guideLineColor, 1);
      }
    }

    int textX = rowX + entry.depth * indentWidth + indentWidth;

    // Expand/collapse arrow
    if (!node->isLeaf())
      _drawArrow(ctx, rowX + entry.depth * indentWidth + indentWidth / 2,
                 rowY + rowHeight / 2, node->expanded);

    // Icon (optional)
    int iconOffset = 0;
    if (showIcons && !node->icon.empty()) {
      NativeFont iconFont =
          fc.getFont("Segoe MDL2 Assets", iconSize, FontWeight::Normal);
      painter.drawText(node->icon, textX, rowY, iconSize + 4, rowHeight,
                       iconFont, node->disabled ? disabledColor : iconColor,
                       DT_LEFT | DT_VCENTER | DT_SINGLELINE);
      iconOffset = iconSize + 6;
    }

    // Label
    NativeFont hFont = isSel ? fc.getFont(fontSize, FontWeight::Bold)
                             : fc.getFont(fontSize, FontWeight::Normal);

    painter.drawTextA(node->label, textX + iconOffset, rowY,
                      rowX + contentW - 4 - textX - iconOffset, rowHeight,
                      hFont, node->disabled ? disabledColor : textColor,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
  }

  void _drawArrow(GraphicsContext &ctx, int cx, int cy, bool expanded) const {
    Painter painter(ctx);
    int s = arrowSize / 2;
    if (expanded) {
      painter.drawLine(cx - s, cy - s / 2, cx, cy + s / 2, arrowColor, 1);
      painter.drawLine(cx, cy + s / 2, cx + s, cy - s / 2, arrowColor, 1);
    } else {
      painter.drawLine(cx - s / 2, cy - s, cx + s / 2, cy, arrowColor, 1);
      painter.drawLine(cx + s / 2, cy, cx - s / 2, cy + s, arrowColor, 1);
    }
  }

  void _renderScrollbar(GraphicsContext &ctx) const {
    Painter painter(ctx);
    int totalH = (int)flatList_.size() * rowHeight;
    int sbX = x + width - scrollbarWidth - 1;

    painter.fillRect(sbX, y, scrollbarWidth, height, Color::fromRGB(240, 240, 240));

    float thumbRatio = (float)height / (float)totalH;
    int thumbH = max(20, (int)(height * thumbRatio));
    float scrollRatio = (float)scrollOffset_ / (float)(totalH - height);
    int thumbY = y + (int)(scrollRatio * (height - thumbH));

    Color thumbCol = _scrollbarHovered ? scrollbarHover : scrollbarColor;
    painter.fillRoundedRegion(sbX + 1, thumbY, scrollbarWidth - 2, thumbH, 4,
                              thumbCol);
  }
};

// ============================================================================
// FACTORY
// ============================================================================

using TreeViewWidgetPtr = std::shared_ptr<TreeViewWidget>;

// Single root
inline TreeViewWidgetPtr TreeView(TreeNode root) {
  std::vector<TreeNode> roots;
  roots.push_back(std::move(root));
  return std::make_shared<TreeViewWidget>(std::move(roots));
}

// Multiple roots
inline TreeViewWidgetPtr TreeView(std::vector<TreeNode> roots) {
  return std::make_shared<TreeViewWidget>(std::move(roots));
}

#endif // FLUX_TREE_VIEW_HPP