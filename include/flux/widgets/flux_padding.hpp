#ifndef FLUX_PADDING_HPP
#define FLUX_PADDING_HPP

#include "../flux_core.hpp"
#include "../flux_state.hpp"

// ============================================================================
// EdgeInsets  —  immutable inset descriptor
// ============================================================================

struct EdgeInsets {
    int left   = 0;
    int top    = 0;
    int right  = 0;
    int bottom = 0;

    // ── Constructors ──────────────────────────────────────────────────────────

    EdgeInsets() = default;

    constexpr EdgeInsets(int l, int t, int r, int b)
        : left(l), top(t), right(r), bottom(b) {}



    /// Same inset on every side.
    static constexpr EdgeInsets all(int value) {
        return EdgeInsets(value, value, value, value);
    }

    /// Separate horizontal (left/right) and vertical (top/bottom) insets.
    static constexpr EdgeInsets symmetric(int horizontal = 0, int vertical = 0) {
        return EdgeInsets(horizontal, vertical, horizontal, vertical);
    }

    /// Explicit per-side insets — Flutter's EdgeInsets.only(...)
    static constexpr EdgeInsets only(int left   = 0, int top    = 0,
                                     int right  = 0, int bottom = 0) {
        return EdgeInsets(left, top, right, bottom);
    }

    /// Explicit LTRB order — Flutter's EdgeInsets.fromLTRB(...)
    static constexpr EdgeInsets fromLTRB(int l, int t, int r, int b) {
        return EdgeInsets(l, t, r, b);
    }

    /// Zero insets on all sides.
    static constexpr EdgeInsets zero() {
        return EdgeInsets(0, 0, 0, 0);
    }

    // ── Convenience accessors ─────────────────────────────────────────────────

    int horizontal() const { return left + right; }
    int vertical()   const { return top  + bottom; }

    // ── Arithmetic helpers ────────────────────────────────────────────────────

    EdgeInsets operator+(const EdgeInsets &o) const {
        return EdgeInsets(left   + o.left,   top    + o.top,
                          right  + o.right,  bottom + o.bottom);
    }

    EdgeInsets operator*(int scale) const {
        return EdgeInsets(left * scale, top * scale, right * scale, bottom * scale);
    }

    bool operator==(const EdgeInsets &o) const {
        return left == o.left && top == o.top &&
               right == o.right && bottom == o.bottom;
    }
    bool operator!=(const EdgeInsets &o) const { return !(*this == o); }

    EdgeInsets copyWith(int l = -1, int t = -1, int r = -1, int b = -1) const {
        return EdgeInsets(l < 0 ? left   : l,
                          t < 0 ? top    : t,
                          r < 0 ? right  : r,
                          b < 0 ? bottom : b);
    }

    bool isZero() const { return left == 0 && top == 0 && right == 0 && bottom == 0; }
};

// ============================================================================
// PaddingWidget  —  single-child widget that applies EdgeInsets padding
// ============================================================================
//
//    • Shrink-wraps its child by default (autoWidth / autoHeight = true).
//    • Passes loose child constraints deflated by the edge insets.
//    • Offsets the child by (paddingLeft, paddingTop).
//    • Transparent: no background, no border, no hit-surface of its own.
//
// ============================================================================

class PaddingWidget : public Widget {
public:
    EdgeInsets insets;

    // ── Construction ──────────────────────────────────────────────────────────

    explicit PaddingWidget(const EdgeInsets &e = EdgeInsets::zero())
        : insets(e)
    {
        syncInsets();
    }

    // ── Inset management ──────────────────────────────────────────────────────

    void setInsets(const EdgeInsets &e) {
        insets = e;
        syncInsets();
        markNeedsLayout();
    }

    // ── computeLayout ─────────────────────────────────────────────────────────

    void computeLayout(GraphicsContext &ctx,
                       const BoxConstraints &constraints,
                       FontCache &fontCache) override
    {
        BoxConstraints self = selfConstraints(constraints);

        if (!children.empty()) {
            // Deflate the available space by our insets for the child.
            BoxConstraints childC = self.deflate(insets.horizontal(),
                                                 insets.vertical());
            children[0]->computeLayout(ctx, childC, fontCache);

            // Shrink-wrap: our size = child size + insets (clamped to self).
            if (autoWidth)
                width  = self.clampWidth(children[0]->width  + insets.horizontal());
            if (autoHeight)
                height = self.clampHeight(children[0]->height + insets.vertical());
        } else {
            // No child — size is just the insets themselves (or 0 if insets are 0).
            if (autoWidth)  width  = self.clampWidth(insets.horizontal());
            if (autoHeight) height = self.clampHeight(insets.vertical());
        }

        if (!autoWidth)  width  = self.clampWidth(width);
        if (!autoHeight) height = self.clampHeight(height);

        applyConstraints();
        needsLayout = false;
    }

    // ── positionChildren ──────────────────────────────────────────────────────

    void positionChildren(int contentX, int contentY,
                          int /*contentWidth*/, int /*contentHeight*/) override
    {
        if (children.empty()) return;

        auto &child = children[0];

        child->x = contentX + insets.left + child->marginLeft;
        child->y = contentY + insets.top  + child->marginTop;

        child->positionChildren(
            child->x + child->paddingLeft,
            child->y + child->paddingTop,
            child->width  - child->paddingLeft - child->paddingRight,
            child->height - child->paddingTop  - child->paddingBottom);
    }

    // ── render ────────────────────────────────────────────────────────────────

    void render(GraphicsContext &ctx, FontCache &fontCache) override {
        if (!visible) return;

        for (auto &child : children)
            child->render(ctx, fontCache);

        needsPaint = false;
    }

    // ── Fluent setters ────────────────────────────────────────────────────────

    std::shared_ptr<PaddingWidget> setEdgeInsets(const EdgeInsets &e) {
        insets = e;
        syncInsets();
        markNeedsLayout();
        return std::static_pointer_cast<PaddingWidget>(shared_from_this());
    }

    /// Uniform padding on all sides.
    std::shared_ptr<PaddingWidget> setAll(int value) {
        return setEdgeInsets(EdgeInsets::all(value));
    }

    /// Separate horizontal and vertical padding.
    std::shared_ptr<PaddingWidget> setSymmetric(int horizontal = 0,
                                                int vertical   = 0) {
        return setEdgeInsets(EdgeInsets::symmetric(horizontal, vertical));
    }

    /// Per-side padding (any side can be omitted / left at 0).
    std::shared_ptr<PaddingWidget> setOnly(int left   = 0, int top    = 0,
                                           int right  = 0, int bottom = 0) {
        return setEdgeInsets(EdgeInsets::only(left, top, right, bottom));
    }

    /// Explicit LTRB padding.
    std::shared_ptr<PaddingWidget> setLTRB(int l, int t, int r, int b) {
        return setEdgeInsets(EdgeInsets::fromLTRB(l, t, r, b));
    }

    std::shared_ptr<PaddingWidget> setVisible(bool v) {
        visible = v;
        markNeedsLayout();
        return std::static_pointer_cast<PaddingWidget>(shared_from_this());
    }

    // ── Reactive overload (binds insets to a State<EdgeInsets>) ───────────────

    template <typename T, typename F>
    std::shared_ptr<PaddingWidget> setEdgeInsets(State<T> &state, F transform) {
        std::function<EdgeInsets(const T &)> fn = transform;
        insets = fn(state.get());
        syncInsets();
        markNeedsLayout();

        state.bindProperty(
            shared_from_this(),
            [fn](Widget *w, const T &val) {
                auto *p = static_cast<PaddingWidget *>(w);
                p->insets = fn(val);
                p->syncInsets();
                p->markNeedsLayout();
            },
            true);

        return std::static_pointer_cast<PaddingWidget>(shared_from_this());
    }

private:

    void syncInsets() {
        paddingLeft   = insets.left;
        paddingTop    = insets.top;
        paddingRight  = insets.right;
        paddingBottom = insets.bottom;
    }
};

// ============================================================================
// FACTORY FUNCTIONS
// ============================================================================

using PaddingWidgetPtr = std::shared_ptr<PaddingWidget>;

/// Padding(EdgeInsets::all(16), child)
inline PaddingWidgetPtr Padding(const EdgeInsets &insets,
                                WidgetPtr child = nullptr) {
    auto w = std::make_shared<PaddingWidget>(insets);
    if (child)
        w->addChild(child);
    return w;
}

/// Padding(16, child)  — uniform shorthand
inline PaddingWidgetPtr Padding(int all, WidgetPtr child = nullptr) {
    return Padding(EdgeInsets::all(all), child);
}

/// PaddingH / PaddingV — horizontal-only and vertical-only shorthands
inline PaddingWidgetPtr PaddingH(int horizontal, WidgetPtr child = nullptr) {
    return Padding(EdgeInsets::symmetric(horizontal, 0), child);
}

inline PaddingWidgetPtr PaddingV(int vertical, WidgetPtr child = nullptr) {
    return Padding(EdgeInsets::symmetric(0, vertical), child);
}

/// PaddingOnly — per-side shorthand
inline PaddingWidgetPtr PaddingOnly(int left   = 0, int top    = 0,
                                    int right  = 0, int bottom = 0,
                                    WidgetPtr child = nullptr) {
    return Padding(EdgeInsets::only(left, top, right, bottom), child);
}

/// PaddingLTRB — explicit order shorthand
inline PaddingWidgetPtr PaddingLTRB(int l, int t, int r, int b,
                                    WidgetPtr child = nullptr) {
    return Padding(EdgeInsets::fromLTRB(l, t, r, b), child);
}

#endif // FLUX_PADDING_HPP