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
#include "flux/flux_widget.hpp"

#include <emscripten.h>
#include <cstdio>
#include <unordered_map>

// ============================================================================
// Input-event reverse lookup + dispatch
//
// A SEPARATE map from flux_painter_dom.cpp's Widget*->handle cache (that
// one maps ownership for painting; this one maps a specific input node's
// handle back to the Widget* that should receive its native events).
// thread_local for the same reason as every other piece of shared state
// since Phase 0 — one active mapping per rendering thread.
// ============================================================================

namespace
{
    thread_local std::unordered_map<DomNodeHandle, Widget *> g_inputEventTargets;
}

extern "C" EMSCRIPTEN_KEEPALIVE void fluxDomOnInputEvent(int handle, const char *value)
{
    auto it = g_inputEventTargets.find((DomNodeHandle)handle);
    if (it != g_inputEventTargets.end() && it->second)
        it->second->onDomInputChanged(value ? value : "");
}

extern "C" EMSCRIPTEN_KEEPALIVE void fluxDomOnFocusEvent(int handle, int focused)
{
    auto it = g_inputEventTargets.find((DomNodeHandle)handle);
    if (it != g_inputEventTargets.end() && it->second)
        it->second->onDomFocusChanged(focused != 0);
}

extern "C" EMSCRIPTEN_KEEPALIVE void fluxDomOnScrollEvent(int handle, int scrollTop)
{
    auto it = g_inputEventTargets.find((DomNodeHandle)handle);
    if (it != g_inputEventTargets.end() && it->second)
        it->second->onDomScrollChanged(scrollTop);
}

extern "C" EMSCRIPTEN_KEEPALIVE void fluxDomOnMediaTimeUpdate(int handle, float currentTime, float duration)
{
    auto it = g_inputEventTargets.find((DomNodeHandle)handle);
    if (it != g_inputEventTargets.end() && it->second)
        it->second->onDomMediaTimeUpdate(currentTime, duration);
}

extern "C" EMSCRIPTEN_KEEPALIVE void fluxDomOnMediaPlay(int handle)
{
    auto it = g_inputEventTargets.find((DomNodeHandle)handle);
    if (it != g_inputEventTargets.end() && it->second)
        it->second->onDomMediaPlay();
}

extern "C" EMSCRIPTEN_KEEPALIVE void fluxDomOnMediaPause(int handle)
{
    auto it = g_inputEventTargets.find((DomNodeHandle)handle);
    if (it != g_inputEventTargets.end() && it->second)
        it->second->onDomMediaPause();
}

extern "C" EMSCRIPTEN_KEEPALIVE void fluxDomOnMediaEnded(int handle)
{
    auto it = g_inputEventTargets.find((DomNodeHandle)handle);
    if (it != g_inputEventTargets.end() && it->second)
        it->second->onDomMediaEnded();
}

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
        Module._fluxDomNodes = [null];

        // Module._fluxDomAlloc(tagName) -> handle (int)
        // Creates a real element, stores it, returns its handle.
        Module._fluxDomAlloc = function(tag)
        {
            var el = document.createElement(tag);
            var handle = Module._fluxDomNodes.length;
            Module._fluxDomNodes.push(el);
            return handle;
        };

        // ── Hydration state ──────────────────────────────────────────────
        //
        // True only during the client's very first build+paint pass, right
        // after loading a page the SSR host rendered. main.cpp flips this
        // off (fluxDomAdapterLiveFinishHydration, below) immediately after
        // that first pass completes — every node created after that point
        // is genuinely new (a later Navigator page swap, a dropdown
        // opening, etc.) and has no server-rendered counterpart to look
        // for, so skipping the lookup avoids a wasted querySelector on
        // every single node creation for the rest of the page's life.
        Module._fluxHydrating = true;

        // Module._fluxDomAdopt(hydrationId) -> Element | null
        // Finds the server-rendered element carrying this exact
        // data-flux-id, if one exists and hasn't already been adopted.
        // Scoped to #flux-dom-root — the SSR mount point — so this never
        // accidentally matches something unrelated elsewhere in the page.
        Module._fluxDomAdopt = function(hydrationId)
        {
            var mount = document.getElementById('flux-dom-root');
            if (!mount)
                return null;
            var el = mount.querySelector('[data-flux-id="' + hydrationId + '"]');
            if (!el || el._fluxAdopted)
                return null;
            el._fluxAdopted = true; // guards against a (shouldn't-happen) double match
            return el;
        };
    });
}

// ============================================================================
// fluxDomAdapterLiveFinishHydration — call once, right after the client's
// FIRST build+paint pass completes (main.cpp). Turns off the adoption
// lookup in createNode() below for the rest of the page's lifetime.
// ============================================================================

extern "C" void fluxDomAdapterLiveFinishHydration()
{
    EM_ASM({ Module._fluxHydrating = false; });
}

// ============================================================================
// LiveDomAdapter
// ============================================================================

class LiveDomAdapter : public IDomAdapter
{
public:
DomNodeHandle createNode(const char *tag, const std::string &hydrationId) override
{
    int handle = EM_ASM_INT({
        var tagStr = UTF8ToString($0);
        var hid = UTF8ToString($1);
        if (Module._fluxHydrating && hid.length > 0)
        {
            var existing = Module._fluxDomAdopt(hid);
            if (existing)
            {
                var h = Module._fluxDomNodes.length;
                Module._fluxDomNodes.push(existing);
                return h;
            }
            console.warn('[flux] hydration MISS id="' + hid + '" tag=' + tagStr);
        }
        return Module._fluxDomAlloc(tagStr); }, tag, hydrationId.c_str());
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
            el.style.setProperty(UTF8ToString($1), UTF8ToString($2)); }, node, prop, value.c_str());
    }

    void setAttr(DomNodeHandle node, const char *name,
                 const std::string &value) override
    {
        if (node == kInvalidDomNode)
            return;
        EM_ASM({
            var el = Module._fluxDomNodes[$0];
            if (!el) return;
            el.setAttribute(UTF8ToString($1), UTF8ToString($2)); }, node, name, value.c_str());
    }

    void setText(DomNodeHandle node, const std::string &utf8Text) override
    {
        if (node == kInvalidDomNode)
            return;
        EM_ASM({
            var el = Module._fluxDomNodes[$0];
            if (!el) return;
            el.textContent = UTF8ToString($1); }, node, utf8Text.c_str());
    }

    void appendChild(DomNodeHandle parent, DomNodeHandle child) override
    {
        if (parent == kInvalidDomNode || child == kInvalidDomNode)
            return;
        EM_ASM({
            var p = Module._fluxDomNodes[$0];
            var c = Module._fluxDomNodes[$1];
            if (!p || !c)
                return;
            // Idempotent, per the IDomAdapter contract — render() calls this
            // every frame for every parent/child pair whether or not the
            // structure actually changed, in a FIXED sibling order. Checking
            // only "is c the last child" means every sibling except the one
            // rendered last this frame fails the check and gets physically
            // re-appended — including nodes that already have real, correct
            // parentage, just not "last child" position. A moved node is
            // detached and reattached by the browser, which unconditionally
            // blurs it if it currently holds focus (e.g. a live <input> —
            // see TextInputWidget) — so a full-tree render pass would blur
            // any focused input on every single frame. Checking actual
            // parentage instead of sibling position avoids ALL of that
            // churn: we only touch the DOM when c doesn't already live
            // under p, which is the only case that actually needs fixing.
            // Sibling order among absolutely-positioned nodes has no visual
            // effect here (no z-index-free overlapping stacking to worry
            // about), so leaving existing children in whatever order they
            // were first inserted is safe.
            if (c.parentNode === p)
                return;
            p.appendChild(c); // moves c if it already has a different parent
        },
               parent, child);
    }

    void removeNode(DomNodeHandle node) override
    {
        if (node == kInvalidDomNode)
            return;
        g_inputEventTargets.erase(node);
        EM_ASM({
            var el = Module._fluxDomNodes[$0];
            if (!el) return;
            if (el._fluxBlobUrl) {
                URL.revokeObjectURL(el._fluxBlobUrl);
                el._fluxBlobUrl = null;
            }
            if (el.parentNode) el.parentNode.removeChild(el);
            Module._fluxDomNodes[$0] = null; }, node);
    }

    void setInputValue(DomNodeHandle node, const std::string &value) override
    {
        if (node == kInvalidDomNode)
            return;
        EM_ASM({
            var el = Module._fluxDomNodes[$0];
            if (!el) return;
            var v = UTF8ToString($1);
            // Only touch .value if it actually differs — assigning it
            // unconditionally every frame would reset the user's cursor
            // position mid-typing, even when the value didn't change.
            if (el.value !== v) el.value = v; }, node, value.c_str());
    }

    void focusNode(DomNodeHandle node) override
    {
        if (node == kInvalidDomNode)
            return;
        EM_ASM({ var el = Module._fluxDomNodes[$0]; if (el && el.focus) el.focus(); }, node);
    }

    void blurNode(DomNodeHandle node) override
    {
        if (node == kInvalidDomNode)
            return;
        EM_ASM({ var el = Module._fluxDomNodes[$0]; if (el && el.blur) el.blur(); }, node);
    }

    void bindInputEvents(DomNodeHandle node, Widget *owner) override
    {
        if (node == kInvalidDomNode)
            return;
        g_inputEventTargets[node] = owner;
        EM_ASM({
            var el = Module._fluxDomNodes[$0];
            if (!el) return;
            if (el._fluxBound) return; // idempotent — render() calls this every frame
            el._fluxBound = true;
            var handle = $0;
            el.addEventListener('input', function () {
                var len = lengthBytesUTF8(el.value) + 1;
                var buf = _malloc(len);
                stringToUTF8(el.value, buf, len);
                Module.ccall('fluxDomOnInputEvent', null, ['number', 'number'], [handle, buf]);
                _free(buf);
            });
            el.addEventListener('focus', function () {
                Module.ccall('fluxDomOnFocusEvent', null, ['number', 'number'], [handle, 1]);
            });
            el.addEventListener('blur', function () {
                Module.ccall('fluxDomOnFocusEvent', null, ['number', 'number'], [handle, 0]);
            }); }, node);
    }

    void bindScrollEvent(DomNodeHandle node, Widget *owner) override
    {
        if (node == kInvalidDomNode)
            return;
        g_inputEventTargets[node] = owner;
        EM_ASM({
            var el = Module._fluxDomNodes[$0];
            if (!el) return;
            if (el._fluxScrollBound) return; // idempotent, same as bindInputEvents' _fluxBound guard
            el._fluxScrollBound = true;
            var handle = $0;
            el.addEventListener('scroll', function () {
                Module.ccall('fluxDomOnScrollEvent', null, ['number', 'number'], [handle, el.scrollTop | 0]);
            }); }, node);
    }

    void setBoolProperty(DomNodeHandle node, const char *name, bool value) override
    {
        if (node == kInvalidDomNode)
            return;
        EM_ASM({
             var el = Module._fluxDomNodes[$0];
             if (!el) return;
             el[UTF8ToString($1)] = $2 ? true : false; }, node, name, value ? 1 : 0);
    }

    void setFloatProperty(DomNodeHandle node, const char *name, float value) override
    {
        if (node == kInvalidDomNode)
            return;
        EM_ASM({
             var el = Module._fluxDomNodes[$0];
             if (!el) return;
             el[UTF8ToString($1)] = $2; }, node, name, value);
    }


    void playNode(DomNodeHandle node) override
    {
        if (node == kInvalidDomNode)
            return;
        EM_ASM({
            var el = Module._fluxDomNodes[$0];
            // .play() returns a Promise that rejects if autoplay policy
            // blocks it (e.g. no prior user gesture) — swallow that
            // rejection rather than letting it surface as an unhandled
            // promise rejection in the console. The widget's own
            // 'pause' event (still fired in that case, since play()
            // never actually started) is what keeps _playing accurate;
            // see onDomMediaPause() wiring.
            if (el && el.play) { var p = el.play(); if (p && p.catch) p.catch(function(){}); } }, node);
    }

    void pauseNode(DomNodeHandle node) override
    {
        if (node == kInvalidDomNode)
            return;
        EM_ASM({ var el = Module._fluxDomNodes[$0]; if (el && el.pause) el.pause(); }, node);
    }

    void seekNode(DomNodeHandle node, float seconds) override
    {
        if (node == kInvalidDomNode)
            return;
        EM_ASM({
            var el = Module._fluxDomNodes[$0];
            if (!el) return;
            // Clamp into [0, duration] when duration is already known —
            // an out-of-range currentTime write is a no-op in most
            // browsers but some clamp differently; doing it ourselves
            // keeps behavior consistent.
            var d = isFinite(el.duration) ? el.duration : $1;
            var t = Math.max(0, Math.min(d, $1));
            el.currentTime = t; }, node, seconds);
    }

    void bindMediaEvents(DomNodeHandle node, Widget *owner) override
    {
        if (node == kInvalidDomNode)
            return;
        g_inputEventTargets[node] = owner;
        EM_ASM({
            var el = Module._fluxDomNodes[$0];
            if (!el) return;
            if (el._fluxMediaBound) return; // idempotent — render() calls this every frame
            el._fluxMediaBound = true;
            var handle = $0;
            el.addEventListener('timeupdate', function () {
                var d = isFinite(el.duration) ? el.duration : 0;
                Module.ccall('fluxDomOnMediaTimeUpdate', null,
                    ['number', 'number', 'number'], [handle, el.currentTime || 0, d]);
            });
            // loadedmetadata fires once duration/dimensions are known,
            // often BEFORE the first timeupdate — without this too, the
            // displayed duration stays "0:00" until playback actually
            // starts advancing currentTime, which looks broken on a
            // paused, just-loaded video.
            el.addEventListener('loadedmetadata', function () {
                var d = isFinite(el.duration) ? el.duration : 0;
                Module.ccall('fluxDomOnMediaTimeUpdate', null,
                    ['number', 'number', 'number'], [handle, el.currentTime || 0, d]);
            });
            el.addEventListener('play', function () {
                Module.ccall('fluxDomOnMediaPlay', null, ['number'], [handle]);
            });
            el.addEventListener('pause', function () {
                Module.ccall('fluxDomOnMediaPause', null, ['number'], [handle]);
            });
            el.addEventListener('ended', function () {
                Module.ccall('fluxDomOnMediaEnded', null, ['number'], [handle]);
            }); }, node);
    }

    void setCameraPreviewSource(DomNodeHandle node) override
    {
        if (node == kInvalidDomNode)
            return;
        EM_ASM({
            var el = Module._fluxDomNodes[$0];
            if (!el) return;
            var stream = Module._fluxCameraStream;
            if (!stream) return;
            // A MediaStream can feed any number of independent <video>
            // elements at once — this is a SECOND sink alongside
            // flux_camera_web.cpp's own hidden engine element, not a
            // duplicate decode. Guard is still needed: reassigning
            // srcObject to the SAME stream restarts it.
            if (el.srcObject !== stream) {
                el.srcObject = stream;
                var p = el.play();
                if (p && p.catch) p.catch(function(){});
            } }, node);
    }

    void setImageSourceFromFile(DomNodeHandle node, const std::string &path) override
    {
        if (node == kInvalidDomNode)
            return;
        EM_ASM({
            var el = Module._fluxDomNodes[$0];
            if (!el) return;
            var path = UTF8ToString($1);

            if (el._fluxBlobUrl) {
                URL.revokeObjectURL(el._fluxBlobUrl);
                el._fluxBlobUrl = null;
            }
            var bytes;
            try { bytes = FS.readFile(path); }
            catch (e) {
                console.error('[flux] setImageSourceFromFile: FS.readFile failed for', path, e);
                return;
            }
            var blob = new Blob([bytes], { type: 'image/jpeg' });
            var url = URL.createObjectURL(blob);
            el._fluxBlobUrl = url;
            el.src = url; }, node, path.c_str());
    }

    void setRoot(DomNodeHandle node) override
    {
        if (node == kInvalidDomNode)
            return;
        EM_ASM({
            var el = Module._fluxDomNodes[$0];
            var mount = document.getElementById('flux-dom-root');
            if (el && mount) mount.appendChild(el); }, node);
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