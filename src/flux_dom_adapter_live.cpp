// src/flux_dom_adapter_live.cpp
//
// Live-browser implementation of IDomAdapter (flux_dom_adapter.hpp).
//
// Every DomNodeHandle is an index into Module._fluxDomNodes, a JS array of
// real DOM elements. C++ never holds a pointer to a JS Element — same
// discipline flux_painter_web.cpp already follows for CanvasRenderingContext2D
// (Module._fluxCtx2D) and flux_painter_web.cpp's image store
// (Module._fluxImgStore).
//
// This file is intentionally the ONLY place that touches real `document`
// APIs. flux_painter_dom.cpp (added after this file) never does — it only
// calls through IDomAdapter, which is what lets the exact same Painter code
// run against this file (browser) and against the string-builder adapter
// added in Phase 4 (SSR host) with zero changes.

#ifdef __EMSCRIPTEN__

#include "flux/flux_dom_adapter.hpp"

#include <emscripten.h>
#include <cstdio>

// ============================================================================
// One-time JS-side registry setup
//
// Call once at startup (from main.cpp, alongside the existing
// fluxPainterWebInit() call). Registers Module._fluxDomNodes (handle ->
// element) and a couple of small helpers used by every method below.
// ============================================================================

extern "C" void fluxDomAdapterLiveInit()
{
    EM_ASM({
        // Slot 0 is reserved (kInvalidDomNode) — never assigned a real node.
        Module._fluxDomNodes = [ null ];

        // Module._fluxDomAlloc(tagName) -> handle (int)
        // Creates a real element, stores it, returns its handle.
        Module._fluxDomAlloc = function(tag)
        {
            var el = document.createElement(tag);
            var handle = Module._fluxDomNodes.length;
            Module._fluxDomNodes.push(el);
            return handle;
        };
    });
}

// ============================================================================
// LiveDomAdapter
// ============================================================================

class LiveDomAdapter : public IDomAdapter
{
public:
    DomNodeHandle createNode(const char *tag) override
    {
        int handle = EM_ASM_INT({
            return Module._fluxDomAlloc(UTF8ToString($0));
        }, tag);
        return (DomNodeHandle)handle;
    }

    void setStyle(DomNodeHandle node, const char *prop,
                 const std::string &value) override
    {
        if (node == kInvalidDomNode)
            return;
        EM_ASM({
            var el = Module._fluxDomNodes[$0];
            if (!el) return;
            el.style.setProperty(UTF8ToString($1), UTF8ToString($2));
        }, node, prop, value.c_str());
    }

    void setAttr(DomNodeHandle node, const char *name,
                const std::string &value) override
    {
        if (node == kInvalidDomNode)
            return;
        EM_ASM({
            var el = Module._fluxDomNodes[$0];
            if (!el) return;
            el.setAttribute(UTF8ToString($1), UTF8ToString($2));
        }, node, name, value.c_str());
    }

    void setText(DomNodeHandle node, const std::string &utf8Text) override
    {
        if (node == kInvalidDomNode)
            return;
        EM_ASM({
            var el = Module._fluxDomNodes[$0];
            if (!el) return;
            el.textContent = UTF8ToString($1);
        }, node, utf8Text.c_str());
    }

    void appendChild(DomNodeHandle parent, DomNodeHandle child) override
    {
        if (parent == kInvalidDomNode || child == kInvalidDomNode)
            return;
        EM_ASM({
            var p = Module._fluxDomNodes[$0];
            var c = Module._fluxDomNodes[$1];
            if (!p || !c) return;
            // Idempotent, per the IDomAdapter contract — render() calls this
            // every frame for every parent/child pair whether or not the
            // structure actually changed. Avoid redundant reordering/reflow
            // when the child is already correctly the last child.
            if (p.lastElementChild === c) return;
            p.appendChild(c); // moves c if it already has a different parent
        }, parent, child);
    }

    void removeNode(DomNodeHandle node) override
    {
        if (node == kInvalidDomNode)
            return;
        EM_ASM({
            var el = Module._fluxDomNodes[$0];
            if (!el) return;
            if (el.parentNode) el.parentNode.removeChild(el);
            Module._fluxDomNodes[$0] = null; // free the slot
        }, node);
    }

    void setRoot(DomNodeHandle node) override
    {
        if (node == kInvalidDomNode)
            return;
        EM_ASM({
            var el = Module._fluxDomNodes[$0];
            var mount = document.getElementById('flux-dom-root');
            if (el && mount) mount.appendChild(el);
        }, node);
    }
};

// ============================================================================
// Singleton instance + wiring into the active-adapter accessor
//
// One instance for the whole page lifetime (static storage), following the
// same "lives as long as the page does" pattern as s_app in web/main.cpp.
// ============================================================================

namespace
{
    LiveDomAdapter s_liveDomAdapter;
}

extern "C" void fluxDomAdapterLiveActivate()
{
    setActiveDomAdapter(&s_liveDomAdapter);
}

#endif // __EMSCRIPTEN__