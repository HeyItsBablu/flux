#ifndef FLUX_ACCORDION_HPP
#define FLUX_ACCORDION_HPP

#include "flux_core.hpp"
#include "flux_layout.hpp"
#include "flux_state.hpp"
#include <functional>
#include <string>
#include <vector>

// ============================================================================
// ACCORDION PANEL
// ============================================================================
//
// A single collapsible section with a header and optional body content.
//
// Usage:
//   AccordionPanel panel("General Settings");
//   panel.expanded = true;
//   panel.body = VStack({ Text("Item 1"), Text("Item 2") });
//
// Or with a subtitle and icon:
//   AccordionPanel p("Appearance", "Theme & colors");
//   p.icon = L"\uE771";
//   p.body = mySettingsWidget;
// ============================================================================

struct AccordionPanel {
  std::string title;
  std::string subtitle; // optional — shown below title in smaller text
  std::wstring icon;    // optional Segoe MDL2 glyph
  bool expanded = false;
  bool disabled = false;
  WidgetPtr body; // any widget subtree rendered when expanded

  explicit AccordionPanel(const std::string &t, const std::string &sub = "")
      : title(t), subtitle(sub) {}
};

// ============================================================================
// ACCORDION WIDGET
// ============================================================================

class AccordionWidget : public Widget {
public:
  // ── Appearance ────────────────────────────────────────────────────────────
  int headerHeight = 48;
  int iconSize = 16;
  int arrowSize = 8;
  int bodyPadding = 12; // padding inside body area
  int separatorHeight = 1;
  int borderRadius = 6;
  int animationSteps = 8; // expand/collapse animation frames

  Color headerBgColor = Color::fromRGB(248, 249, 250);
  Color headerHoverColor = Color::fromRGB(237, 242, 247);
  Color headerActiveColor = Color::fromRGB(224, 235, 248);
  Color bodyBgColor = Color::fromRGB(255, 255, 255);
  Color titleColor = Color::fromRGB(30, 30, 30);
  Color subtitleColor = Color::fromRGB(110, 110, 120);
  Color disabledColor = Color::fromRGB(180, 180, 180);
  Color arrowColor = Color::fromRGB(90, 90, 90);
  Color iconColor = Color::fromRGB(70, 120, 200);
  Color separatorColor = Color::fromRGB(220, 220, 225);
  Color borderColor = Color::fromRGB(210, 214, 220);
  Color accentColor = Color::fromRGB(33, 150, 243);

  int titleFontSize = 13;
  int subtitleFontSize = 11;
  bool showBorder = true;
  bool showSeparators = true;

  // ── Callbacks ─────────────────────────────────────────────────────────────
  std::function<void(int /*panelIndex*/, bool /*expanded*/)> onChanged;

  // ── Constructor ───────────────────────────────────────────────────────────
  explicit AccordionWidget(std::vector<AccordionPanel> panels)
      : panels_(std::move(panels)) {
    autoHeight = true;
    _initAnimStates();
  }

  // ── Public API ────────────────────────────────────────────────────────────

  // Replace all panels
  void setPanels(std::vector<AccordionPanel> newPanels) {
    panels_ = std::move(newPanels);
    _initAnimStates();
    markNeedsLayout();
    if (auto *ui = FluxUI::getCurrentInstance())
      ui->updateWidget(this);
  }

  // Expand a panel by index
  void expand(int idx) {
    if (idx < 0 || idx >= (int)panels_.size())
      return;
    if (panels_[idx].disabled)
      return;
    if (singleExpand_)
      _collapseAllExcept(idx);
    panels_[idx].expanded = true;
    animStates_[idx].animating = true;
    if (onChanged)
      onChanged(idx, true);
    markNeedsLayout();
    if (auto *ui = FluxUI::getCurrentInstance())
      ui->updateWidget(this);
  }

  // Collapse a panel by index
  void collapse(int idx) {
    if (idx < 0 || idx >= (int)panels_.size())
      return;
    panels_[idx].expanded = false;
    animStates_[idx].animating = true;
    if (onChanged)
      onChanged(idx, false);
    markNeedsLayout();
    if (auto *ui = FluxUI::getCurrentInstance())
      ui->updateWidget(this);
  }

  // Toggle by index
  void toggle(int idx) {
    if (idx < 0 || idx >= (int)panels_.size())
      return;
    if (panels_[idx].expanded)
      collapse(idx);
    else
      expand(idx);
  }

  // Expand all (ignored in singleExpand mode)
  void expandAll() {
    for (int i = 0; i < (int)panels_.size(); i++) {
      if (!panels_[i].disabled && !panels_[i].expanded) {
        panels_[i].expanded = true;
        animStates_[i].animating = true;
      }
    }
    markNeedsLayout();
    if (auto *ui = FluxUI::getCurrentInstance())
      ui->updateWidget(this);
  }

  // Collapse all
  void collapseAll() {
    for (int i = 0; i < (int)panels_.size(); i++) {
      if (panels_[i].expanded) {
        panels_[i].expanded = false;
        animStates_[i].animating = true;
      }
    }
    markNeedsLayout();
    if (auto *ui = FluxUI::getCurrentInstance())
      ui->updateWidget(this);
  }

  // Get a panel by index (mutable — update body widget etc.)
  AccordionPanel *panelAt(int idx) {
    if (idx < 0 || idx >= (int)panels_.size())
      return nullptr;
    return &panels_[idx];
  }

  int panelCount() const { return (int)panels_.size(); }

  // ── Layout ────────────────────────────────────────────────────────────────
  void computeLayout(GraphicsContext &ctx, const BoxConstraints &constraints,
                     FontCache &fontCache) override {
    if (autoWidth)
      width = constraints.maxWidth;
    applyConstraints();

    // Pass 1 — measure every panel body. Always zero first (fix #3: no stale
    // heights).
    for (int i = 0; i < (int)panels_.size(); i++) {
      panelBodyHeights_[i] = 0; // reset unconditionally
      auto &p = panels_[i];
      if (p.expanded && p.body) {
        int innerW = std::max(0, width - bodyPadding * 2);
        BoxConstraints bc =
            BoxConstraints::loose(innerW, constraints.maxHeight);
        p.body->computeLayout(ctx, bc, fontCache);
        panelBodyHeights_[i] = p.body->height + bodyPadding * 2;
      }
    }

    // Pass 2 — accumulate total height and stamp body x/y now that we
    // have correct heights (fix #1) AND accordion x/y are valid.
    // Also call positionChildren on each body so nested layouts (Column,
    // Row, etc.) position their own children correctly (fix #2).
    int curY = y + (showBorder ? 1 : 0);
    int totalH = showBorder ? 2 : 0;

    for (int i = 0; i < (int)panels_.size(); i++) {
      curY += headerHeight;
      totalH += headerHeight;

      if (i < (int)panels_.size() - 1 && showSeparators) {
        curY += separatorHeight;
        totalH += separatorHeight;
      }

      auto &p = panels_[i];
      int bodyH = panelBodyHeights_[i];

      if (p.expanded && p.body && bodyH > 0) {
        int bodyX = x + bodyPadding;
        int bodyY = curY + bodyPadding;
        p.body->x = bodyX;
        p.body->y = bodyY;
        // Let the body position its own children recursively
        p.body->positionChildren(bodyX, bodyY, p.body->width, p.body->height);
      }

      curY += bodyH;
      totalH += bodyH;
    }

    height = totalH;
    needsLayout = false;
  }

  void positionChildren(int /*cx*/, int /*cy*/, int /*cw*/,
                        int /*ch*/) override {
    // Accordion positions its body children inside computeLayout (above)
    // so the layout system's separate positionChildren call is a no-op.
  }

  // ── Render ────────────────────────────────────────────────────────────────
  void render(GraphicsContext &ctx, FontCache &fontCache) override {
    if (!visible)
      return;

    Painter painter(ctx);

    // Outer border
    if (showBorder)
      painter.drawRoundedRectOutline(x, y, width, height, borderRadius * 2,
                                     borderColor, 1);

    // Clip to widget bounds
    painter.pushClipRoundedRect(x, y, width, height, borderRadius * 2);

    int curY = y + (showBorder ? 1 : 0);

    for (int i = 0; i < (int)panels_.size(); i++) {
      auto &p = panels_[i];
      _renderHeader(ctx, fontCache, i, curY);
      curY += headerHeight;

      // Separator between panels (not after the last one)
      if (i < (int)panels_.size() - 1 && showSeparators) {
        painter.drawHLine(x + 1, curY, width - 2, separatorColor, 1);
        curY += separatorHeight;
      }

      // Body area
      int bodyH = panelBodyHeights_[i];
      if (bodyH > 0) {
        painter.fillRect(x + 1, curY, width - 2, bodyH, bodyBgColor);
        if (p.expanded && p.body)
          p.body->render(ctx, fontCache);
      }
      curY += bodyH;
    }

    painter.popClipRect();
    needsPaint = false;
  }

  // ── Mouse events ──────────────────────────────────────────────────────────

  bool handleMouseDown(int mx, int my) override {
    if (!_inBounds(mx, my))
      return false;

    int idx = _headerAt(my);
    if (idx >= 0 && !panels_[idx].disabled) {
      toggle(idx);
      return true;
    }

    for (auto &p : panels_) {
      if (p.expanded && p.body) {
        if (findAndHandleMouseEvent(p.body.get(), mx, my, [mx, my](Widget *w) {
              return w->handleMouseDown(mx, my);
            }))
          return true;
      }
    }
    return false;
  }

  bool handleMouseMove(int mx, int my) override {
    if (!_inBounds(mx, my)) {
      if (hoveredHeader_ != -1) {
        hoveredHeader_ = -1;
        markNeedsPaint();
      }
      return false;
    }

    int idx = _headerAt(my);
    if (idx != hoveredHeader_) {
      hoveredHeader_ = idx;
      markNeedsPaint();
    }
    return false;
  }

  bool handleMouseLeave() override {
    if (hoveredHeader_ != -1) {
      hoveredHeader_ = -1;
      markNeedsPaint();
    }
    return false;
  }

  // ── Fluent setters ────────────────────────────────────────────────────────

  std::shared_ptr<AccordionWidget> setSingleExpand(bool v) {
    singleExpand_ = v;
    return self_();
  }
  std::shared_ptr<AccordionWidget>
  setOnChanged(std::function<void(int, bool)> cb) {
    onChanged = std::move(cb);
    return self_();
  }
  std::shared_ptr<AccordionWidget> setHeaderHeight(int h) {
    headerHeight = h;
    markNeedsLayout();
    return self_();
  }
  std::shared_ptr<AccordionWidget> setBodyPadding(int p) {
    bodyPadding = p;
    markNeedsLayout();
    return self_();
  }
  std::shared_ptr<AccordionWidget> setShowBorder(bool v) {
    showBorder = v;
    markNeedsLayout();
    return self_();
  }
  std::shared_ptr<AccordionWidget> setShowSeparators(bool v) {
    showSeparators = v;
    markNeedsLayout();
    return self_();
  }
  std::shared_ptr<AccordionWidget> setAccentColor(Color c) {
    accentColor = c;
headerActiveColor = c.interpolate(Color::fromRGB(255, 255, 255), 0.15);
    markNeedsPaint();
    return self_();
  }
  std::shared_ptr<AccordionWidget> setTitleFontSize(int s) {
    titleFontSize = s;
    markNeedsLayout();
    return self_();
  }
  std::shared_ptr<AccordionWidget> setWidth(int w) {
    width = w;
    autoWidth = false;
    markNeedsLayout();
    return self_();
  }
  std::shared_ptr<AccordionWidget> setFlex(int f) {
    flex = f;
    return self_();
  }

private:
  // ── State ─────────────────────────────────────────────────────────────────
  struct AnimState {
    bool animating = false;
    int frame = 0;
  };

  std::vector<AccordionPanel> panels_;
  std::vector<int> panelBodyHeights_; // resolved during layout
  std::vector<AnimState> animStates_;

  bool singleExpand_ = false;
  int hoveredHeader_ = -1;

  // ── Helpers ───────────────────────────────────────────────────────────────

  std::shared_ptr<AccordionWidget> self_() {
    return std::static_pointer_cast<AccordionWidget>(shared_from_this());
  }

  bool _inBounds(int mx, int my) const {
    return mx >= x && mx < x + width && my >= y && my < y + height;
  }

  void _initAnimStates() {
    panelBodyHeights_.assign(panels_.size(), 0);
    animStates_.assign(panels_.size(), AnimState{});
  }

  void _collapseAllExcept(int keepIdx) {
    for (int i = 0; i < (int)panels_.size(); i++) {
      if (i != keepIdx && panels_[i].expanded) {
        panels_[i].expanded = false;
        animStates_[i].animating = true;
        if (onChanged)
          onChanged(i, false);
      }
    }
  }

  // Returns the panel index whose header contains screen-y coordinate `my`,
  // or -1 if none.
  int _headerAt(int my) const {
    int curY = y + (showBorder ? 1 : 0);
    for (int i = 0; i < (int)panels_.size(); i++) {
      if (my >= curY && my < curY + headerHeight)
        return i;
      curY += headerHeight;
      if (i < (int)panels_.size() - 1 && showSeparators)
        curY += separatorHeight;
      curY += panelBodyHeights_[i];
    }
    return -1;
  }

  // ── Header rendering ──────────────────────────────────────────────────────

  void _renderHeader(GraphicsContext &ctx, FontCache &fontCache, int idx,
                     int topY) const {
    const auto &p = panels_[idx];
    bool isHov = (hoveredHeader_ == idx && !p.disabled);
    bool isExp = p.expanded;

    Painter painter(ctx);

    // Header background (with optional left accent when expanded)
    Color bg = isExp   ? headerActiveColor
                  : isHov ? headerHoverColor
                          : headerBgColor;

    if (isExp)
      painter.fillRectWithLeftAccent(x + 1, topY, width - 2, headerHeight, bg,
                                     accentColor, 3);
    else
      painter.fillRect(x + 1, topY, width - 2, headerHeight, bg);

    int cx = x + 16;

    // Icon (optional)
    if (!p.icon.empty()) {
      NativeFont iconFont =
          fontCache.getFont("Segoe MDL2 Assets", iconSize, FontWeight::Normal);
      painter.drawText(p.icon, cx, topY, iconSize + 6, headerHeight, iconFont,
                       p.disabled ? disabledColor : iconColor,
                       DT_LEFT | DT_VCENTER | DT_SINGLELINE);
      cx += iconSize + 8;
    }

    // Title + optional subtitle
    int textRight = x + width - 36;

    if (p.subtitle.empty()) {
      NativeFont titleFont = fontCache.getFont(
          titleFontSize, isExp ? FontWeight::Light : FontWeight::Normal);
      painter.drawTextA(p.title, cx, topY, textRight - cx, headerHeight,
                        titleFont, p.disabled ? disabledColor : titleColor,
                        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    } else {
      int halfH = headerHeight / 2;

      NativeFont titleFont = fontCache.getFont(
          titleFontSize, isExp ? FontWeight::Light : FontWeight::Normal);
      painter.drawTextA(p.title, cx, topY + 4, textRight - cx, halfH - 2,
                        titleFont, p.disabled ? disabledColor : titleColor,
                        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

      NativeFont subFont =
          fontCache.getFont(subtitleFontSize, FontWeight::Normal);
      painter.drawTextA(p.subtitle, cx, topY + halfH - 2, textRight - cx,
                        headerHeight - halfH - 2, subFont,
                        p.disabled ? disabledColor : subtitleColor,
                        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }

    // Chevron
    int arrowX = x + width - 20;
    int arrowY = topY + headerHeight / 2;
    _drawChevron(ctx, arrowX, arrowY, isExp,
                 p.disabled ? disabledColor : arrowColor);
  }

  void _drawChevron(GraphicsContext &ctx, int cx, int cy, bool pointUp,
                    Color color) const {
    Painter painter(ctx);
    int s = arrowSize / 2;
    if (pointUp) {
      painter.drawLine(cx - s, cy + s / 2, cx, cy - s / 2, color, 2);
      painter.drawLine(cx, cy - s / 2, cx + s, cy + s / 2, color, 2);
    } else {
      painter.drawLine(cx - s, cy - s / 2, cx, cy + s / 2, color, 2);
      painter.drawLine(cx, cy + s / 2, cx + s, cy - s / 2, color, 2);
    }
  }


};

// ============================================================================
// FACTORY
// ============================================================================

using AccordionWidgetPtr = std::shared_ptr<AccordionWidget>;

// Initializer-list / vector of panels
inline AccordionWidgetPtr Accordion(std::vector<AccordionPanel> panels) {
  return std::make_shared<AccordionWidget>(std::move(panels));
}

// Convenience: single panel
inline AccordionWidgetPtr Accordion(AccordionPanel panel) {
  std::vector<AccordionPanel> v;
  v.push_back(std::move(panel));
  return std::make_shared<AccordionWidget>(std::move(v));
}

#endif // FLUX_ACCORDION_HPP