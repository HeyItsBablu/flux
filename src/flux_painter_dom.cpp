// src/flux_painter_dom.cpp
//
// DOM Painter implementation for Emscripten / WebAssembly ("dom" renderer).
// Compiled instead of flux_painter_web.cpp when FLUX_WEB_RENDERER=dom.
//
// Every method here talks ONLY to IDomAdapter (flux_dom_adapter.hpp) — never
// EM_ASM, never a live `document` reference directly. That discipline is
// what lets this exact file drive both:
//   - the live browser (via LiveDomAdapter, flux_dom_adapter_live.cpp), and
//   - a server-side HTML string builder (added in Phase 4)
// with zero changes to anything below.
//
// Node ownership
// ──────────────
// Every geometry-producing call is tagged with Painter::owner (a Widget*,
// added in Phase 0). ensureNode() below maps owner -> one persistent DOM
// node, reused across every call and every frame for that widget, and
// keeps it correctly parented under owner->parent's node automatically —
// no widget code needs to manage DOM structure explicitly.
//
// Known scope limits (see notes below the code): a handful of Painter
// methods used by only a few widgets are stubbed as documented no-ops
// for now (drawArc, drawWavyLine, fillPolygonAlpha, beginLayer/endLayer,
// drawShadow, drawScrollbar, drawPage). drawVideo/drawCamera are
// PERMANENTLY no-ops on this backend by design — VideoPlayerWidget/
// AudioPlayerWidget get dedicated real elements instead (a later,
// separate change to those two widget files, not to Painter).

// FLUX_SSR added alongside __EMSCRIPTEN__: this file has NO direct EM_ASM
// calls anywhere in it — every browser touch already goes through
// IDomAdapter — so the exact same compiled code works against
// flux_dom_adapter_stringbuilder.cpp (FLUX_SSR) with zero changes. This
// is the payoff of Phase 1's adapter-interface discipline.
#if defined(__EMSCRIPTEN__) || defined(FLUX_SSR)

#include "flux/flux_painter.hpp"
#include "flux/flux_dom_adapter.hpp"
#include "flux/flux_widget.hpp"
#include "flux/flux_font.hpp"
#include "flux/flux_text_style.hpp"

#include <unordered_map>
#include <cstdio>
#include <cmath>
#include <string>

// ============================================================================
// Forward declarations — implemented in flux_font_dom.cpp (next file).
// Mirrors the exact split flux_painter_web.cpp / flux_font_web.cpp already
// use: Painter's measure methods are thin wrappers around functions defined
// in the font file, so font-measurement logic lives in one place.
// ============================================================================

void measureDomText(const char *cssFont, const std::wstring &wtext,
                    int &outWidth, int &outHeight);
void measureDomRichText(const std::wstring &wtext, const TextStyle &style,
                        FontCache &fontCache, int maxWidth, bool softWrap,
                        int maxLines, int &outWidth, int &outHeight);
// Implemented per-backend: flux_font_ssr.cpp (stb_truetype metrics) and,
// for the live web-DOM renderer, flux_font_dom.cpp (CSS-pixel-size-based
// approximation matching that file's own font string).
int fluxDomLineHeightPx(const std::string &fontFamily, int fontSize, FontWeight weight);

// Implemented per-backend. NativeFont is NOT safely castable to
// `const char*` on every backend — on live web (flux_font_dom.cpp) it IS
// already a CSS font string, but on SSR (flux_font_ssr.cpp) it's a
// pointer to an SsrNativeFont struct (stbtt_fontinfo + friends), and
// reinterpreting that as a C-string is undefined behavior. This function
// is the one seam that turns whatever NativeFont actually is into a
// proper CSS `font` shorthand string for the given backend.
std::string fluxDomCssFontString(NativeFont font, const std::string &fontFamily,
                                 int fontSize, FontWeight weight);

// ============================================================================
// Internal helpers
// ============================================================================

namespace
{
    // ── Per-thread widget -> DOM node cache ──────────────────────────────────
    //
    // thread_local, matching every other piece of shared state fixed in
    // Phase 0 (FluxUI::currentInstance, ThemeProvider::current_, etc). This
    // matters here for a subtle reason beyond the usual "one SSR request per
    // thread" argument: Widget* is a heap address, and after a Widget is
    // destroyed its memory can be reused by an unrelated Widget later (even
    // in a completely different request's tree, on a different thread). A
    // process-wide cache could return a STALE, WRONG node for a brand-new
    // widget that happens to land on a freed widget's old address. Per-thread
    // storage plus the eviction hook below (wired into Widget::onDetach in a
    // follow-up edit) closes that gap.
    // Widget* -> (slot -> node). A widget with no slotted nodes just has
    // one entry under the default key "" — the common case, unchanged
    // cost from before.
    thread_local std::unordered_map<Widget *, std::unordered_map<std::string, DomNodeHandle>>
        g_domNodeCache;
    // ── Per-node hydration id counter ────────────────────────────────────────
    //
    // Mirrors flux_hydration.hpp's fluxHydrationNextId() pattern exactly,
    // but at DOM-NODE granularity — every widget's node gets one, not just
    // hydration-aware widgets. As long as createApp()/build() run the same
    // deterministic code path on the SSR host and the client (same tree,
    // same order — the same requirement fluxHydrationNextId() already
    // relies on), the Nth node CREATED on the server and the Nth node
    // CREATED on the client are "the same" node, and
    // flux_dom_adapter_live.cpp uses this id to adopt the server's
    // existing element instead of duplicating it.
    thread_local int g_domNodeIdCounter = 0;

    std::string nextDomNodeHydrationId() { return "n" + std::to_string(g_domNodeIdCounter++); }

    // ── CSS colour string ─────────────────────────────────────────────────────
    void cssColor(Color c, char *buf, int bufLen)
    {
        float a = c.a / 255.f;
        snprintf(buf, bufLen, "rgba(%d,%d,%d,%.3f)", c.r, c.g, c.b, a);
    }

    std::string pxStr(int v)
    {
        char buf[24];
        snprintf(buf, sizeof(buf), "%dpx", v);
        return buf;
    }
    // Forward declaration — ensureNode() (below) needs to call this before
    // its full definition is reached later in this same anonymous
    // namespace; C++ doesn't hoist function bodies, only declarations.
    void applyRect(IDomAdapter *adapter, DomNodeHandle node, Widget *owner,
                   int x, int y, int w, int h);

    // ── ensureNode ────────────────────────────────────────────────────────────
    //
    // Get-or-create the persistent DOM node for `owner`, and make sure it's
    // correctly parented. Recurses up owner->parent so ancestors always
    // exist before descendants ask to be appended to them — cheap in
    // practice because every already-created ancestor is a single map
    // lookup, not a re-creation.
    //
    // Known cost: this walks the parent chain and calls appendChild (an
    // idempotent no-op per the IDomAdapter contract when nothing changed)
    // on every single geometry call, for every widget, every frame. For very
    // deep trees this is more redundant work than strictly necessary — a
    // reasonable target for a later optimization (e.g. only re-verify
    // parentage when a widget's actual Widget::parent pointer changes) but
    // not a correctness problem, so left as-is for this first pass.
    DomNodeHandle ensureNode(Widget *owner, const char *tag = "div",
                             const char *slot = "")
    {
        if (!owner)
            return kInvalidDomNode;
        IDomAdapter *adapter = getActiveDomAdapter();
        if (!adapter)
            return kInvalidDomNode;

        auto &slotMap = g_domNodeCache[owner];
        auto it = slotMap.find(slot);

        DomNodeHandle handle;
        bool created = false;

        if (it != slotMap.end())
        {
            handle = it->second;
        }
        else
        {

            handle = adapter->createNode(tag, nextDomNodeHydrationId());
            slotMap[slot] = handle;
            created = true;
            adapter->setStyle(handle, "position", "absolute");
        }

        if (owner->parent)
        {
            // Slotted nodes (box/label/etc.) are siblings parented under
            // the OWNER'S PARENT's default node — same as the owner's own
            // default node would be. There's no per-widget "wrapper" node
            // that slots nest inside. This keeps applyRect()'s coordinate
            // math (which only ever looks at owner->parent, never at
            // slot) correct for every slot without special-casing.
            DomNodeHandle parentHandle = ensureNode(owner->parent, "div");
            if (parentHandle != kInvalidDomNode)
                adapter->appendChild(parentHandle, handle);
        }
        else if (created)
        {
            // A parentless (root) widget — no owner->parent to append
            // under. EVERY node it creates, in EVERY slot, needs SOME
            // attachment point or it never becomes visible at all, even
            // though its styles/text are set correctly (exactly the
            // "clickable but invisible" symptom this produces when
            // missed). setRoot() is safe to call once per DISTINCT node
            // — it's only reached here when `created` is true, i.e. the
            // first time this exact (owner, slot) pair is seen — and the
            // live adapter's setRoot() is itself just an appendChild
            // under the mount point, so calling it once per slot simply
            // makes each slot's node a sibling under #flux-dom-root,
            // which is exactly the right behavior for e.g. ListSurface's
            // "bg" node plus each of its "item0".."itemN" nodes.
            adapter->setRoot(handle);
        }

        // Keep this node's own geometry in sync unconditionally, not only
        // when a Painter method happens to target this exact widget. A
        // plain layout container (Column/Row/Padding/a bare Container with
        // no background or border) never calls a Painter method on itself
        // — Widget::render() only recurses into children in that case —
        // so without this, its node is left as a bare
        // `position:absolute` div with no left/top/width/height at all.
        // CSS then falls back to static-position flow layout for it,
        // which is essentially never where the widget tree says it
        // actually is, and every descendant's applyRect() subtraction is
        // computed against a DOM ancestor that isn't really there.
        applyRect(adapter, handle, owner, owner->x, owner->y, owner->width, owner->height);

        return handle;
    }

    // ── Shared geometry application ──────────────────────────────────────────
    // Every fill/border call needs left/top/width/height set the same way;
    // centralised here so each Painter method stays a couple of lines.
    //
    // IMPORTANT: x/y here are the widget's ABSOLUTE page coordinates (per
    // the layout engine). CSS position:absolute resolves left/top against
    // the nearest POSITIONED ANCESTOR, not the page — and every node this
    // file creates is itself position:absolute (see ensureNode). Once a
    // widget is nested two or more levels deep, its DOM parent is ALSO a
    // positioned ancestor sitting at a non-zero offset, so writing
    // absolute page coordinates double-counts that offset and the node
    // renders outside its parent's clip region — invisible.
    //
    // Fix: subtract the OWNER'S PARENT WIDGET's absolute x/y (0 if no
    // parent) before writing left/top. Safe because ensureNode() always
    // makes a widget's DOM parent equal to Widget::parent's own node, so
    // "owner's parent widget" and "this node's DOM parent" are always the
    // same coordinate origin.
    void applyRect(IDomAdapter *adapter, DomNodeHandle node, Widget *owner,
                   int x, int y, int w, int h)
    {
        int localX = x, localY = y;
        if (owner && owner->parent)
        {
            localX -= owner->parent->x;
            localY -= owner->parent->y;
        }
        adapter->setStyle(node, "left", pxStr(localX));
        adapter->setStyle(node, "top", pxStr(localY));
        adapter->setStyle(node, "width", pxStr(w));
        adapter->setStyle(node, "height", pxStr(h));
    }

    // ── Enum -> CSS mappers ───────────────────────────────────────────────────

    const char *cssTextAlign(TextAlign a)
    {
        switch (a)
        {
        case TextAlign::Center:
            return "center";
        case TextAlign::Right:
        case TextAlign::End:
            return "right";
        case TextAlign::Justify:
            return "justify";
        case TextAlign::Left:
        case TextAlign::Start:
        default:
            return "left";
        }
    }

    const char *cssDecorationLine(TextDecoration d)
    {
        // CSS text-decoration-line accepts a space-separated combination;
        // build the common cases used across the codebase's TextStyle.
        bool u = hasDecoration(d, TextDecoration::Underline);
        bool o = hasDecoration(d, TextDecoration::Overline);
        bool s = hasDecoration(d, TextDecoration::LineThrough);
        if (!u && !o && !s)
            return "none";
        if (u && !o && !s)
            return "underline";
        if (!u && o && !s)
            return "overline";
        if (!u && !o && s)
            return "line-through";
        if (u && s && !o)
            return "underline line-through";
        return "underline overline line-through"; // all three — rare, but valid CSS
    }

    const char *cssDecorationStyle(TextDecorationStyle s)
    {
        switch (s)
        {
        case TextDecorationStyle::Double:
            return "double";
        case TextDecorationStyle::Dotted:
            return "dotted";
        case TextDecorationStyle::Dashed:
            return "dashed";
        case TextDecorationStyle::Wavy:
            return "wavy";
        case TextDecorationStyle::Solid:
        default:
            return "solid";
        }
    }

} // namespace

// ============================================================================
// fluxDomApplyRect — external entry point into the same parent-relative
// geometry math applyRect() (above, internal-linkage) uses for every other
// Painter call. TextInputWidget::_renderDom() (flux_input.hpp) needs this
// so its real <input> node is positioned the same way as every other node
// in the tree — writing raw page-absolute x/y here (as it previously did)
// double-counts the DOM parent's own offset the moment the input is nested
// more than one level deep, since CSS position:absolute resolves against
// the nearest positioned ancestor, not the page.
// ============================================================================

void fluxDomApplyRect(Widget *owner, int x, int y, int w, int h, const char *slot)
{
    IDomAdapter *adapter = getActiveDomAdapter();
    if (!adapter || !owner)
        return;
    DomNodeHandle node = ensureNode(owner, "div", slot);
    applyRect(adapter, node, owner, x, y, w, h);
}

// ============================================================================
// fluxDomEnsureNode — external entry point into the same widget->node cache
// ensureNode() (above, internal-linkage) maintains. TextInputWidget
// (flux_input.hpp) needs this to create/reuse its real <input> node
// directly, bypassing every Painter primitive — it never calls fillRect/
// drawBorder/etc. on this backend at all, per the "dedicated real
// element" design decision.
// ============================================================================

DomNodeHandle fluxDomEnsureNode(Widget *owner, const char *tag, const char *slot)
{
    return ensureNode(owner, tag, slot);
}

// ============================================================================
// Widget eviction hook — called from Widget::onDetach() (wired in a small
// follow-up edit to flux_widget.cpp, not part of this file). Removes the
// cached node (and, transitively via the adapter's real DOM removal, every
// still-attached child under it) when a widget subtree is torn down —
// e.g. Navigator swapping pages, or a FlexBuilderWidget item scrolling out
// of its keyed cache.
// ============================================================================

void fluxDomEvictWidget(Widget *owner)
{
    auto it = g_domNodeCache.find(owner);
    if (it == g_domNodeCache.end())
        return;
    // Remove EVERY slot's node, not just one — a widget that used
    // multiple slots (box + label, etc.) must have all of them cleaned
    // up on detach, or the unused ones leak in the live document.
    IDomAdapter *adapter = getActiveDomAdapter();
    for (auto &[slot, handle] : it->second)
        if (adapter)
            adapter->removeNode(handle);
    g_domNodeCache.erase(it);
}

// ============================================================================
// fluxDomClearCacheForNewRequest — SSR-only. A live browser page never
// needs this (the cache naturally lives exactly as long as the page
// does); an SSR host's single server thread renders MANY requests
// sequentially on the SAME thread_local storage, and Widget* addresses
// get reused across requests the moment one request's tree is
// destroyed — without this, a brand-new widget in request N+1 could
// collide with a freed widget's stale cache entry from request N,
// returning a handle into an ALREADY-DESTROYED StringBuilderDomAdapter.
// ============================================================================

void fluxDomClearCacheForNewRequest()
{
    g_domNodeCache.clear();
}

// ============================================================================
// Painter::fillRect / fillRoundedRect / fillRectAlpha / fillRoundedRegion
// ============================================================================

void Painter::fillRect(int x, int y, int w, int h, Color color, const char *slot)
{
    if (!owner)
        return;
    IDomAdapter *adapter = getActiveDomAdapter();
    if (!adapter)
        return;
    DomNodeHandle node = ensureNode(owner, "div", slot);
    applyRect(adapter, node, owner, x, y, w, h);
    char col[32];
    cssColor(color, col, sizeof(col));
    adapter->setStyle(node, "background-color", col);
    // A slotted node is reused ONLY by this same slot's calls going
    // forward, but a stale border-radius from a previous frame where
    // this exact slot was (mis-)used for a rounded shape must not leak
    // forward. fillRect always means "plain rectangle" — reset it.
    adapter->setStyle(node, "border-radius", "0px");
}

void Painter::fillRoundedRect(int x, int y, int w, int h, int radius, Color color,
                              const char *slot)
{
    if (!owner)
        return;
    IDomAdapter *adapter = getActiveDomAdapter();
    if (!adapter)
        return;
    DomNodeHandle node = ensureNode(owner, "div", slot);
    applyRect(adapter, node, owner, x, y, w, h);
    char col[32];
    cssColor(color, col, sizeof(col));
    adapter->setStyle(node, "background-color", col);
    adapter->setStyle(node, "border-radius", pxStr(radius));
}

void Painter::fillRectAlpha(int x, int y, int w, int h, Color color)
{
    // Alpha already lives in Color::a and cssColor() already encodes it as
    // rgba(...) — no separate compositing step needed the way canvas
    // sometimes requires.
    fillRect(x, y, w, h, color);
}

void Painter::fillRoundedRegion(int x, int y, int w, int h, int cornerRadius, Color color)
{
    fillRoundedRect(x, y, w, h, cornerRadius, color);
}

void Painter::fillRoundedRectGDI(int x, int y, int w, int h, int radius,
                                 Color fill, Color stroke, int strokeWidth,
                                 const char *slot)
{
    fillRoundedRect(x, y, w, h, radius, fill, slot);
    if (strokeWidth > 0)
        drawRoundedRectOutline(x, y, w, h, radius * 2, stroke, strokeWidth);
    // NOTE: the stroke path still targets the DEFAULT slot ("") — see
    // the follow-up comment below. Not exercised by VideoPlayerWidget
    // today (its fillRoundedRectGDI calls always pass strokeWidth=0),
    // so left as a known gap rather than plumbing slot through
    // drawBorder/drawRoundedRectOutline in this pass.
}

// ============================================================================
// Painter::drawBorder / drawRectOutline / drawRoundedRectOutline
// ============================================================================

void Painter::drawBorder(int x, int y, int w, int h, int radius,
                         Color color, int borderWidth)
{
    if (!owner)
        return;
    IDomAdapter *adapter = getActiveDomAdapter();
    if (!adapter)
        return;
    DomNodeHandle node = ensureNode(owner);
    applyRect(adapter, node, owner, x, y, w, h);
    char col[32];
    cssColor(color, col, sizeof(col));
    adapter->setStyle(node, "border", pxStr(borderWidth) + " solid " + col);
    if (radius > 0)
        adapter->setStyle(node, "border-radius", pxStr(radius));
}

void Painter::drawRectOutline(int x, int y, int w, int h, Color color, int strokeWidth)
{
    drawBorder(x, y, w, h, 0, color, strokeWidth);
}

void Painter::drawRoundedRectOutline(int x, int y, int w, int h,
                                     int cornerDiameter, Color stroke, int strokeWidth)
{
    drawBorder(x, y, w, h, cornerDiameter / 2, stroke, strokeWidth);
}

// ============================================================================
// Painter::drawEllipse
// ============================================================================
void Painter::drawEllipse(int x, int y, int w, int h, Color fill, Color stroke,
                          int strokeWidth, const char *slot)
{
    if (!owner)
        return;
    IDomAdapter *adapter = getActiveDomAdapter();
    if (!adapter)
        return;
    DomNodeHandle node = ensureNode(owner, "div", slot);
    applyRect(adapter, node, owner, x, y, w, h);
    adapter->setStyle(node, "border-radius", "50%");
    char fcol[32];
    cssColor(fill, fcol, sizeof(fcol));
    adapter->setStyle(node, "background-color", fcol);
    if (strokeWidth > 0)
    {
        char scol[32];
        cssColor(stroke, scol, sizeof(scol));
        adapter->setStyle(node, "border", pxStr(strokeWidth) + " solid " + scol);
    }
    else
    {
        // Same leak-forward concern as fillRect: if this slot's node was
        // previously used for a bordered shape, an explicit reset is
        // needed since "no border" was never actively re-asserted before.
        adapter->setStyle(node, "border", "none");
    }
}

// ============================================================================
// Painter::drawLine / drawHLine / drawVLine
//
// A "line" in DOM terms is a thin absolutely-positioned filled rect —
// same trick used by plenty of CSS-based UI kits, no canvas/SVG needed.
// ============================================================================

void Painter::drawLine(int x1, int y1, int x2, int y2, Color color, int width)
{
    if (!owner)
        return;
    IDomAdapter *adapter = getActiveDomAdapter();
    if (!adapter)
        return;
    DomNodeHandle node = ensureNode(owner);

    int dx = x2 - x1, dy = y2 - y1;
    double length = std::sqrt((double)(dx * dx + dy * dy));
    double angleDeg = std::atan2((double)dy, (double)dx) * 180.0 / M_PI;

    // Position a `width`-thick, `length`-long bar at (x1,y1), rotated to
    // point at (x2,y2) — the standard CSS "line via rotated div" trick.
    applyRect(adapter, node, owner, x1, y1 - width / 2, (int)std::round(length), width);
    char col[32];
    cssColor(color, col, sizeof(col));
    adapter->setStyle(node, "background-color", col);
    char rot[48];
    snprintf(rot, sizeof(rot), "rotate(%.3fdeg)", angleDeg);
    adapter->setStyle(node, "transform-origin", "0 50%");
    adapter->setStyle(node, "transform", rot);
}

void Painter::drawHLine(int x, int y, int len, Color color, int strokeWidth)
{
    drawLine(x, y, x + len, y, color, strokeWidth);
}

void Painter::drawVLine(int x, int y, int len, Color color, int strokeWidth)
{
    drawLine(x, y, x, y + len, color, strokeWidth);
}

// ============================================================================
// Painter::drawPolyline
//
// Same "rotated thin div per segment" trick drawLine() uses above, but one
// segment needs one node each — reusing a single default-slot node the way
// drawLine does would just have each segment overwrite the last. Each
// segment gets its own slot key ("poly0", "poly1", ...) so ensureNode()
// gives it a stable, reused node across frames instead of creating new
// DOM nodes every repaint.
//
// Known limitation: if a later call passes FEWER points than a previous
// call did (variable-length polylines on the same owner), the extra
// slotted nodes from the longer call are left stale/visible rather than
// removed — no different in kind from the existing "leak forward" caveats
// noted elsewhere in this file (see fillRect's border-radius reset
// comment). Not an issue for SvgWidget, which redraws a fixed shape list
// every frame.
// ============================================================================

void Painter::drawPolyline(const std::vector<std::pair<int, int>> &points,
                           Color color, int strokeWidth)
{
    if (!owner || points.size() < 2)
        return;
    IDomAdapter *adapter = getActiveDomAdapter();
    if (!adapter)
        return;

    char col[32];
    cssColor(color, col, sizeof(col));

    for (size_t i = 0; i + 1 < points.size(); ++i)
    {
        char slotBuf[32];
        snprintf(slotBuf, sizeof(slotBuf), "poly%zu", i);
        DomNodeHandle node = ensureNode(owner, "div", slotBuf);

        int x1 = points[i].first, y1 = points[i].second;
        int x2 = points[i + 1].first, y2 = points[i + 1].second;
        int dx = x2 - x1, dy = y2 - y1;
        double length = std::sqrt((double)(dx * dx + dy * dy));
        double angleDeg = std::atan2((double)dy, (double)dx) * 180.0 / M_PI;

        applyRect(adapter, node, owner, x1, y1 - strokeWidth / 2,
                 (int)std::round(length), strokeWidth);
        adapter->setStyle(node, "background-color", col);
        char rot[48];
        snprintf(rot, sizeof(rot), "rotate(%.3fdeg)", angleDeg);
        adapter->setStyle(node, "transform-origin", "0 50%");
        adapter->setStyle(node, "transform", rot);
    }
}

// ============================================================================
// Painter::pushClipRect / popClipRect / pushClipRoundedRect
//
// Clipping in DOM is persistent CSS state on a node (overflow: hidden),
// not a stack-based save/restore the way Canvas2D/D2D/Cairo need. So
// pushClipRect just sets the property; popClipRect (which — note its
// signature — takes no arguments at all, so it has no way to know which
// node to "unclip" even if that concept applied) is correctly a no-op here.
// ============================================================================

void Painter::pushClipRect(int x, int y, int w, int h, int cornerRadius)
{
    if (!owner)
        return;
    IDomAdapter *adapter = getActiveDomAdapter();
    if (!adapter)
        return;
    DomNodeHandle node = ensureNode(owner);
    applyRect(adapter, node, owner, x, y, w, h);
    adapter->setStyle(node, "overflow", "hidden");
    if (cornerRadius > 0)
        adapter->setStyle(node, "border-radius", pxStr(cornerRadius));
}

void Painter::popClipRect()
{
    // Intentionally empty — see comment above.
}

void Painter::pushClipRoundedRect(int x, int y, int w, int h, int cornerDiameter)
{
    pushClipRect(x, y, w, h, cornerDiameter / 2);
}

// ============================================================================
// Painter::drawText / drawTextA  (plain, single-style text)
// ============================================================================

namespace
{
    void applyDtAlignment(IDomAdapter *adapter, DomNodeHandle node, UINT format)
    {
        adapter->setStyle(node, "display", "flex");
        adapter->setStyle(node, "align-items",
                          (format & DT_VCENTER) ? "center" : "flex-start");
        adapter->setStyle(node, "justify-content",
                          (format & DT_CENTER)  ? "center"
                          : (format & DT_RIGHT) ? "flex-end"
                                                : "flex-start");
        adapter->setStyle(node, "white-space",
                          (format & DT_SINGLELINE) ? "nowrap" : "normal");
        if (format & DT_END_ELLIPSIS)
        {
            adapter->setStyle(node, "overflow", "hidden");
            adapter->setStyle(node, "text-overflow", "ellipsis");
        }
    }
}

void Painter::drawText(const std::wstring &text, int x, int y, int w, int h,
                       NativeFont font, Color color, UINT format, const char *slot)
{
    if (!owner || text.empty())
        return;
    IDomAdapter *adapter = getActiveDomAdapter();
    if (!adapter)
        return;
    DomNodeHandle node = ensureNode(owner, "div", slot);
    applyRect(adapter, node, owner, x, y, w, h);
    // Text nodes must not inherit a stale background/border-radius from
    // a shape previously (mis-)painted onto this same slot.
    adapter->setStyle(node, "background-color", "transparent");
    adapter->setStyle(node, "border-radius", "0px");

    // drawText's plain-font callers (IconWidget, cursor math, etc.) don't
    // carry family/size/weight alongside `font` — pass empty/0 defaults;
    // the SSR implementation falls back to its own "Sans"/Normal default
    // in that case, matching FontCache::getFont(size, weight)'s behavior.
    std::string cssFont = fluxDomCssFontString(font, "", 0, FontWeight::Normal);
    if (!cssFont.empty())
        adapter->setStyle(node, "font", cssFont);

    char col[32];
    cssColor(color, col, sizeof(col));
    adapter->setStyle(node, "color", col);
    applyDtAlignment(adapter, node, format);

    // wstring -> UTF-8. BMP-only is fine here (matches toWideString()'s
    // own byte-for-byte scheme on non-Win32 web builds).
    std::string utf8;
    utf8.reserve(text.size());
    for (wchar_t wc : text)
        utf8 += static_cast<char>(static_cast<unsigned char>(wc));
    adapter->setText(node, utf8);
}

void Painter::drawTextA(const std::string &text, int x, int y, int w, int h,
                        NativeFont font, Color color, UINT format, const char *slot)
{
    if (text.empty())
        return;
    std::wstring ws(text.begin(), text.end());
    drawText(ws, x, y, w, h, font, color, format, slot);
}

// ============================================================================
// Painter::measureText — thin forwarder (see flux_font_dom.cpp)
// ============================================================================

void Painter::measureText(const std::wstring &text, NativeFont font,
                          int &outWidth, int &outHeight)
{
    if (text.empty())
    {
        outWidth = outHeight = 0;
        return;
    }
    const char *cssFont = static_cast<const char *>(font);
    if (!cssFont)
    {
        outWidth = outHeight = 0;
        return;
    }
    measureDomText(cssFont, text, outWidth, outHeight);
}

// ============================================================================
// Painter::drawRichText / drawRichTextA / measureRichText
// ============================================================================

void Painter::drawRichText(const std::wstring &wtext,
                           const RichTextParams &params,
                           FontCache &fontCache)
{
    if (!owner || wtext.empty() || params.w <= 0 || params.h <= 0)
        return;
    IDomAdapter *adapter = getActiveDomAdapter();
    if (!adapter)
        return;

    const TextStyle &style = params.style;
    bool underline = hasDecoration(style.decoration, TextDecoration::Underline);
    bool strikeOut = hasDecoration(style.decoration, TextDecoration::LineThrough);

    NativeFont fnt = fontCache.getFont(style.fontFamily, style.scaledFontSize(),
                                       style.fontWeight, underline, strikeOut);
    std::string cssFont = fluxDomCssFontString(fnt, style.fontFamily,
                                               style.scaledFontSize(), style.fontWeight);

    DomNodeHandle node = ensureNode(owner);
    applyRect(adapter, node, owner, params.x, params.y, params.w, params.h);

    if (!cssFont.empty())
        adapter->setStyle(node, "font", cssFont);

    char col[32];
    cssColor(style.color, col, sizeof(col));
    adapter->setStyle(node, "color", col);
    // Pin line-height to the EXACT value measureDomRichText() used for
    // layout — see fluxDomLineHeightPx's comment for why the browser's own
    // default leading can't be trusted to match stb_truetype's numbers.
    int lineHeightPx = fluxDomLineHeightPx(style.fontFamily, style.scaledFontSize(), style.fontWeight);
    adapter->setStyle(node, "line-height", pxStr(lineHeightPx));

    adapter->setStyle(node, "text-align", cssTextAlign(params.textAlign));
    adapter->setStyle(node, "direction",
                      params.direction == TextDirection::RTL ? "rtl" : "ltr");
    adapter->setStyle(node, "white-space", params.softWrap ? "normal" : "nowrap");

    // Vertical alignment within the box — CSS has no direct "vertical-align"
    // for block content, so use flex the same way plain drawText does.
    adapter->setStyle(node, "display", "flex");
    adapter->setStyle(node, "flex-direction", "column");
    adapter->setStyle(node, "justify-content",
                      params.textAlignVertical == TextAlignVertical::Center   ? "center"
                      : params.textAlignVertical == TextAlignVertical::Bottom ? "flex-end"
                                                                              : "flex-start");

    // Overflow / ellipsis / fade.
    // NOTE: this covers the single-line ellipsis case correctly. True
    // multi-line ellipsis (maxLines > 1 with Ellipsis overflow) needs the
    // -webkit-line-clamp family of properties — left as a documented
    // follow-up since none of the widgets reviewed so far rely on it yet.
    if (params.overflow == TextOverflow::Ellipsis)
    {
        adapter->setStyle(node, "overflow", "hidden");
        adapter->setStyle(node, "text-overflow", "ellipsis");
        if (params.maxLines == 1 || !params.softWrap)
            adapter->setStyle(node, "white-space", "nowrap");
    }
    else if (params.overflow == TextOverflow::Clip)
    {
        adapter->setStyle(node, "overflow", "hidden");
    }
    if (params.maxLines > 0)
        adapter->setStyle(node, "max-height", pxStr((int)(params.maxLines * style.scaledFontSize() * style.height * 1.2f)));

    adapter->setStyle(node, "text-decoration-line", cssDecorationLine(style.decoration));
    if (style.decoration != TextDecoration::None)
    {
        adapter->setStyle(node, "text-decoration-style", cssDecorationStyle(style.decorationStyle));
        char dcol[32];
        cssColor(style.decorationColor, dcol, sizeof(dcol));
        adapter->setStyle(node, "text-decoration-color", dcol);
        adapter->setStyle(node, "text-decoration-thickness", pxStr(style.decorationThickness));
    }

    if (!style.shadows.empty())
    {
        std::string shadowCss;
        for (size_t i = 0; i < style.shadows.size(); ++i)
        {
            const auto &sh = style.shadows[i];
            char shc[32];
            cssColor(sh.color, shc, sizeof(shc));
            char part[80];
            snprintf(part, sizeof(part), "%dpx %dpx %s", sh.offsetX, sh.offsetY, shc);
            if (i)
                shadowCss += ", ";
            shadowCss += part;
        }
        adapter->setStyle(node, "text-shadow", shadowCss);
    }

    if (style.backgroundColor.has_value())
    {
        char bcol[32];
        cssColor(*style.backgroundColor, bcol, sizeof(bcol));
        adapter->setStyle(node, "background-color", bcol);
    }

    std::string utf8;
    utf8.reserve(wtext.size());
    for (wchar_t wc : wtext)
        utf8 += static_cast<char>(static_cast<unsigned char>(wc));
    adapter->setText(node, utf8);
}

void Painter::drawRichTextA(const std::string &text, const RichTextParams &params, FontCache &fontCache)
{
    if (text.empty())
        return;
    std::wstring ws(text.begin(), text.end());
    drawRichText(ws, params, fontCache);
}

void Painter::measureRichText(const std::wstring &wtext, const TextStyle &style,
                              FontCache &fontCache, int maxWidth, bool softWrap,
                              int maxLines, int &outWidth, int &outHeight)
{
    if (wtext.empty())
    {
        outWidth = outHeight = 0;
        return;
    }
    measureDomRichText(wtext, style, fontCache, maxWidth, softWrap, maxLines, outWidth, outHeight);
}

// ============================================================================
// Painter::drawFadeOverlay / drawTextDecorationLine
//
// Both delegate to primitives already implemented above — no direct
// adapter access needed here, matching how flux_painter_web.cpp handles
// them (drawFadeOverlay -> fillGradientRect; drawTextDecorationLine ->
// drawHLine/drawWavyLine).
// ============================================================================

void Painter::drawFadeOverlay(int x, int y, int w, int h, int fadeWidth, Color bg)
{
    if (fadeWidth <= 0 || w <= 0 || h <= 0)
        return;
    int startX = x + w - fadeWidth;
    if (startX < x)
        startX = x;
    fillGradientRect(startX, y, fadeWidth, h, {bg.withAlpha(0), bg.withAlpha(255)});
}

void Painter::drawTextDecorationLine(int lineX, int lineY, int lineW,
                                     const TextStyle &style, TextDecoration which)
{
    // drawRichText already applies CSS text-decoration-* directly on the
    // text node (the correct, native way to do this in DOM) — this method
    // exists for the canvas backend's manual-geometry approach and has no
    // DOM equivalent to perform. Left intentionally empty.
    (void)lineX;
    (void)lineY;
    (void)lineW;
    (void)style;
    (void)which;
}

// ============================================================================
// Painter::fillGradientRect
// ============================================================================

void Painter::fillGradientRect(int x, int y, int w, int h, const std::vector<Color> &colors)
{
    if (!owner || colors.empty() || w <= 0 || h <= 0)
        return;
    IDomAdapter *adapter = getActiveDomAdapter();
    if (!adapter)
        return;
    if (colors.size() == 1)
    {
        fillRect(x, y, w, h, colors[0]);
        return;
    }

    DomNodeHandle node = ensureNode(owner);
    applyRect(adapter, node, owner, x, y, w, h);

    std::string css = "linear-gradient(to right";
    for (auto &c : colors)
    {
        char col[32];
        cssColor(c, col, sizeof(col));
        css += ", ";
        css += col;
    }
    css += ")";
    adapter->setStyle(node, "background-image", css);
}

// ============================================================================
// Painter::drawImage
//
// Best-effort for this first pass: reuses the same idea as CSS background
// images. Requires the image-loading glue (wherever NativeImage handles are
// produced for web today) to also expose a usable CSS url()/data-URI string
// per handle — that lookup helper (analogous to Module._fluxImgStore in
// flux_painter_web.cpp) needs a small addition to return a URL instead of
// just a canvas/Image reference. Flagged as a follow-up wiring step, not
// solved inside this file, since it touches the image-loading path rather
// than Painter itself.
// ============================================================================

void Painter::drawImage(const ImageDrawParams &params)
{
    if (!owner || !params.image || params.clipW <= 0 || params.clipH <= 0)
        return;
    IDomAdapter *adapter = getActiveDomAdapter();
    if (!adapter)
        return;

    DomNodeHandle node = ensureNode(owner);
    applyRect(adapter, node, owner, params.clipX, params.clipY, params.clipW, params.clipH);
    if (params.borderRadius > 0)
        adapter->setStyle(node, "border-radius", pxStr(params.borderRadius));
    adapter->setStyle(node, "overflow", "hidden");

#if defined(__EMSCRIPTEN__)
    // See comment above — imageUrlForHandle() is the pending seam.
    extern std::string imageUrlForHandle(NativeImage handle); // TODO wiring
    std::string url = imageUrlForHandle(params.image);
#elif defined(FLUX_SSR)
    // params.image is an ImageWidget::SsrNativeImage* (flux_image_ssr.cpp)
    // — either the widget's ORIGINAL network URL, or a content-addressed
    // /img/<hash> path this process registered for an asset/memory image.
    // Either way it's a URL the browser fetches itself during hydration;
    // SSR never decodes pixels server-side.
    struct SsrNativeImageShape
    {
        int width;
        int height;
        std::string url;
    };
    std::string url = reinterpret_cast<const SsrNativeImageShape *>(params.image)->url;
#else
    std::string url;
#endif
#if defined(__EMSCRIPTEN__) || defined(FLUX_SSR)
    if (!url.empty())
    {
        adapter->setStyle(node, "background-image", "url(" + url + ")");
        const char *repeat =
            params.repeat == ImageRepeat::Repeat    ? "repeat"
            : params.repeat == ImageRepeat::RepeatX ? "repeat-x"
            : params.repeat == ImageRepeat::RepeatY ? "repeat-y"
                                                    : "no-repeat";
        adapter->setStyle(node, "background-repeat", repeat);
        adapter->setStyle(node, "background-size",
                          params.repeat == ImageRepeat::NoRepeat ? "cover" : "auto");
        adapter->setStyle(node, "background-position", "center");
    }
#endif
}

// ============================================================================
// Deferred for later widget-specific migration passes — documented no-ops.
// ============================================================================

void Painter::fillRectWithLeftAccent(int x, int y, int w, int h, Color bg, Color accent, int stripWidth)
{
    fillRect(x, y, w, h, bg);
    // Left accent strip as a second sub-node is exactly the "composite
    // widget, multiple visual layers under one owner" case flagged when
    // ProgressBarWidget was reviewed — deferred pending that design
    // decision rather than solved ad hoc here.
    (void)accent;
    (void)stripWidth;
}

void Painter::fillColumnBars(int, int, int, int, const std::vector<int> &, Color) { /* deferred */ }
void Painter::fillPolygonAlpha(const std::vector<std::pair<int, int>> &, Color) { /* deferred */ }
void Painter::drawArc(float, float, float, int, float, float, Color, bool) { /* deferred */ }
void Painter::drawWavyLine(int, int, int, Color, int) { /* deferred */ }
void Painter::drawShadow(int, int, int, int, int, int, Color, int, int) { /* deferred — CSS box-shadow candidate */ }
void Painter::beginLayer(float) { /* deferred — see notes on canvas-native compositing-layer semantics */ }
void Painter::endLayer() { /* deferred */ }
void Painter::drawScrollbar(const CustomScrollbar &, int, int) { /* deferred — CustomScrollbar not yet reviewed */ }
void Painter::drawPage(const PageDrawParams &) { /* deferred */ }

// drawVideo / drawCamera — PERMANENTLY no-op on this backend by design.
// VideoPlayerWidget / AudioPlayerWidget get dedicated <video>/<canvas>
// elements instead of routing through Painter at all (a small, separate
// change to those two widget files, tracked as its own Phase 1 item).
void Painter::drawVideo(const VideoDrawParams &) {}
void Painter::drawCamera(const CameraDrawParams &) {}

// ============================================================================
// fluxDomResetNodeIdCounter — reset the hydration-id counter above to 0.
//
// Two callers, two different reasons:
//   - ssr/main.cpp calls this every request, alongside
//     fluxDomClearCacheForNewRequest() — a new request's node ids must
//     start from 0, not continue from wherever the previous request left
//     off (this thread renders many requests sequentially).
//   - web/main.cpp calls this ONCE, right before the very first build()
//     of the page — never on later Navigator page swaps, which are
//     client-only transitions with no server counterpart to match ids
//     against (same rule fluxHydrationResetIdCounter() already documents).
// ============================================================================

void fluxDomResetNodeIdCounter() { g_domNodeIdCounter = 0; }

#endif // __EMSCRIPTEN__ || FLUX_SSR
