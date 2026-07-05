// include/flux/flux_dom_adapter.hpp
#pragma once

#include <cstdint>
#include <string>

// ============================================================================
// DomNodeHandle
//
// An opaque reference to "a node in whatever tree the active adapter is
// building." Deliberately NOT a real pointer or JS object reference — an
// integer handle works identically whether the adapter is:
//   - live-DOM (handle indexes into a JS-side registry of real elements), or
//   - string-builder (handle indexes into an in-memory list of pending
//     open tags, used purely to know which "element" a later setStyle/
//     setAttr/setText call is still talking about before it's serialized)
//
// This is what lets flux_painter_dom.cpp be written once and used by both.
// ============================================================================

using DomNodeHandle = uint32_t;
constexpr DomNodeHandle kInvalidDomNode = 0;

// ============================================================================
// IDomAdapter
//
// The ONLY interface flux_painter_dom.cpp is allowed to call into. Never
// EM_ASM, never a live `document` reference, never anything backend-
// specific — that discipline is what makes the SSR string-builder adapter
// (added in Phase 4) a drop-in replacement with no changes to the Painter
// code that uses it.
// ============================================================================

class IDomAdapter
{
public:
    virtual ~IDomAdapter() = default;

    // Create a new node of the given HTML tag ("div", "span", "input", ...).
    // Returns kInvalidDomNode on failure (adapter not ready, etc).
    virtual DomNodeHandle createNode(const char *tag) = 0;

    // Set a single CSS property (e.g. "background-color", "12px").
    // Setting the SAME property on the SAME node repeatedly (e.g. every
    // frame) must be idempotent and cheap — callers rely on this instead
    // of diffing themselves.
    virtual void setStyle(DomNodeHandle node, const char *prop,
                          const std::string &value) = 0;

    // Set an HTML attribute (e.g. "data-flux-id", "42"). Used for hydration
    // markers (Phase 5) and for anything that isn't styling (input value,
    // href, etc).
    virtual void setAttr(DomNodeHandle node, const char *name,
                        const std::string &value) = 0;

    // Replace the node's text content. UTF-8 in; the adapter is responsible
    // for correct encoding on its own side (JS strings are UTF-16, but
    // browsers handle UTF-8 -> UTF-16 conversion for us at the API boundary
    // the live adapter uses).
    virtual void setText(DomNodeHandle node, const std::string &utf8Text) = 0;

    // Append `child` as the last child of `parent`. Calling this again with
    // the same (parent, child) pair when child is already parent's last
    // child should be a cheap no-op, not a re-append — repeated calls are
    // expected every time a widget's render() runs.
    virtual void appendChild(DomNodeHandle parent, DomNodeHandle child) = 0;

    // Detach and destroy a node (and, implicitly, everything still
    // parented under it that nothing else references). Called from
    // Widget::onDetach()'s broadcast — see the DOM Painter cache eviction
    // logic in flux_painter_dom.cpp.
    virtual void removeNode(DomNodeHandle node) = 0;

    // Designates `node` as the single top-level node the adapter's output
    // attaches under (the live document's mount point, or the
    // string-builder's root element). Called once, early, not per-frame.
    virtual void setRoot(DomNodeHandle node) = 0;
};

// ============================================================================
// Active adapter accessor
//
// thread_local, matching the pattern already applied throughout Phase 0
// (FluxUI::currentInstance, ThemeProvider::current_, FluxAppWidget::instance_,
// BreakpointProvider). One browser tab has exactly one thread doing UI work,
// so this behaves as a plain global there. It matters once an SSR host
// renders multiple requests concurrently on different threads — each must
// resolve its OWN active adapter (a string-builder instance, one per
// request), never another thread's.
//
// The adapter itself is NOT owned here — whoever constructs it (main.cpp
// for live-DOM, the SSR host for string-builder) is responsible for its
// lifetime and for calling setActiveDomAdapter(nullptr) before it's
// destroyed.
// ============================================================================

inline thread_local IDomAdapter *g_activeDomAdapter = nullptr;

inline void setActiveDomAdapter(IDomAdapter *adapter)
{
    g_activeDomAdapter = adapter;
}

inline IDomAdapter *getActiveDomAdapter()
{
    return g_activeDomAdapter;
}