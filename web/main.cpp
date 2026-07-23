// web/main.cpp
//
// Web entry point for FluxUI (Emscripten / WASM).
//
// Execution flow:
//   main()
//     └─ FluxUI(nullptr)              — no AppInstance on web
//     └─ app.build(createApp)         — runs builder, wires widget tree
//     └─ app.createWindow(...)        — registers JS event callbacks
//     └─ emscripten_set_main_loop     — hands control back to browser;
//                                       s_tick() called ~60 fps by rAF
//
// Notable differences from Win32 / macOS mains:
//
//   => run() is never called.  On native platforms run() contains the
//     blocking message loop; on web the browser owns the event loop.
//     emscripten_set_main_loop replaces it.
//
//   => Window size:  AppConfig's window width/height/fullscreen/maximize are
//     all ignored on web — irrelevant, not just unread.
//     The canvas always fills the browser viewport.  Physical pixel
//     dimensions are read from Module._fluxPhysicalWidth / _fluxPhysicalHeight
//     (set by shell.html's resizeCanvases() before WASM starts).
//
//   => Title comes from FLUX_APP_NAME, baked in at compile time via
//     target_compile_definitions in CMakeLists.txt (not the generated
//     header used on native platforms — web takes a different path).
//
//   => The FluxUI instance lives for the lifetime of the page (static storage)
//     so Emscripten's main-loop callback can reach it without a global.
//
//   => If the app uses Navigator (flux_navigator.hpp), browser Back/Forward
//     and the URL hash are wired up here: fluxNavigatorHashChanged() is the
//     C-side landing pad for JS's 'hashchange' listener, registered in main()
//     after build() so it doesn't see the initial hash set during init().

#ifdef __EMSCRIPTEN__

#include <emscripten.h>
#include <emscripten/html5.h>
#include <cstdio>
#include <cmath>

#include "flux/flux.hpp"
#include "flux/flux_navigator.hpp"
#include "flux/flux_http_platform.hpp"
#include "flux/flux_hydration.hpp"

// ============================================================================
// Forward declaration — defined by the application (e.g. helloworld.cpp)
// ============================================================================

WidgetPtr createApp(FluxUI *app);
extern "C" void fluxPainterWebInit();
extern "C" EMSCRIPTEN_WEBGL_CONTEXT_HANDLE fluxGetGLContext();

// ============================================================================
// DOM renderer init hooks — only exist when compiled with
// FLUX_WEB_RENDERER_DOM (see root CMakeLists.txt's FLUX_WEB_RENDERER
// switch). Declared here rather than in a shared header since they're
// only ever called from this one place, in main(), once.
// ============================================================================

#ifdef FLUX_WEB_RENDERER_DOM
extern "C" void fluxDomAdapterLiveInit();
extern "C" void fluxDomAdapterLiveActivate();
extern "C" void fluxDomAdapterLiveFinishHydration();
extern "C" void fluxFontDomInit();
#endif

// ============================================================================
// Module-level state
// ============================================================================

namespace
{

    // Owning pointer kept alive for the page lifetime.
    // Using a raw pointer in static storage avoids destructor-order issues
    // at WASM teardown; Emscripten does not guarantee global-destructor ordering.
    FluxUI *s_app = nullptr;

    // ── Fetch physical canvas size from JS ───────────────────────────────────────
    //
    // shell.html stores the devicePixelRatio-scaled dimensions on Module before
    // WASM starts, and updates them whenever the window resizes.  We read them
    // here so createWindow() can pass real pixel counts to the layout engine.

    int canvasPhysicalWidth()
    {
        return EM_ASM_INT({ return Module._fluxPhysicalWidth | 0; });
    }

    int canvasPhysicalHeight()
    {
        return EM_ASM_INT({ return Module._fluxPhysicalHeight | 0; });
    }

    // ── Per-frame tick ────────────────────────────────────────────────────────────
    //
    // Called by Emscripten at ~60 fps via requestAnimationFrame.
    // flux_window_web.cpp pumps pending JS-side events and triggers a repaint
    // if the dirty flag is set.  The actual Canvas 2D draw calls happen inside
    // WindowCallbacks::onPaint, invoked from PlatformWindow::tick().

    void s_tick()
    {
        fluxDrainHttpQueue();
        if (s_app)
        {
            EMSCRIPTEN_WEBGL_CONTEXT_HANDLE ctx = fluxGetGLContext();
            if (ctx > 0)
                emscripten_webgl_make_context_current(ctx);

#ifndef FLUX_WEB_RENDERER_DOM
            // Canvas surfaces need continuous redraw — the backing store
            // holds no state between frames the way DOM nodes do.
            s_app->getPlatformWindow().invalidate();
#endif

            s_app->getPlatformWindow().tick();

#ifdef FLUX_WEB_RENDERER_DOM
            // The FIRST tick() is the first real paint pass — this is
            // when createNode()/ensureNode() calls actually run and try
            // to adopt server-rendered elements (see
            // flux_dom_adapter_live.cpp's Module._fluxHydrating check).
            // Turn adoption mode off immediately afterward; every
            // subsequent tick is a normal repaint with nothing left to
            // adopt. Guarded so this only ever fires once per page load.
            static bool s_hydrationFinished = false;
            if (!s_hydrationFinished)
            {
                s_hydrationFinished = true;
                fluxDomAdapterLiveFinishHydration();
            }
#endif
        }
    }

} // namespace
// ============================================================================
// applyResize — shared resize logic, taking the LOGICAL size directly.
//
// Split out of fluxOnResize() so the initial boot-time sync (main(), step
// 6c) can apply the exact logical size it already computed from
// Module._fluxLogicalWidth/Height, instead of re-deriving it from physical
// pixels via fluxOnResize()'s lossy physicalWidth/dpr conversion.
//
// That round trip — JS computing physicalWidth = floor(logicalWidth * DPR),
// then this file converting back via (int)(physicalWidth / dpr) — loses a
// pixel whenever DPR isn't an integer (1.25, 1.5, 1.75 are all common on
// scaled displays): e.g. DPR=1.5, logicalWidth=805 -> physicalWidth =
// floor(1207.5) = 1207 -> back-converted = (int)(1207/1.5) = 804, not 805.
// The SSR host never goes through this conversion at all (it lays out
// directly in logical px — see ssr/main.cpp's resolveViewport()), so this
// round trip is exactly what was producing the first-paint-vs-hydration
// pixel mismatch, independent of anything server-side.
//
// fluxOnResize() (below) is still the right entry point for REAL runtime
// resize events (window drag-resize, orientation change) — those only
// ever have physical pixels available from the browser, so some
// conversion there is unavoidable. Only the synthetic startup call in
// main() can and should skip it.
// ============================================================================

static void applyResize(int logicalW, int logicalH)
{
    if (!s_app)
        return;

    PlatformWindow &win = s_app->getPlatformWindow();
    win.cachedWidth = logicalW;
    win.cachedHeight = logicalH;

    // DPR scale transform stays the same
    EM_ASM({
        var dpr = Module._fluxDPR || 1.0;
        var ctx = Module._fluxCtx2D;
        if (ctx)
        {
            ctx.setTransform(1, 0, 0, 1, 0, 0);
            ctx.scale(dpr, dpr);
        }
    });

    if (win.callbacks.onResize)
    {
        GraphicsContext ctx(logicalW, logicalH);
        win.callbacks.onResize(ctx, logicalW, logicalH);
    }
    win.invalidate();
}

// ============================================================================
// Resize callback — registered with JS by flux_window_web.cpp,
// but also callable directly from C++ (e.g. on orientation change).
// Exposed as a C symbol so shell.html can call Module._fluxOnResizeCpp(w,h).
// ============================================================================

extern "C" EMSCRIPTEN_KEEPALIVE void fluxOnResize(int physicalWidth, int physicalHeight)
{
    if (!s_app)
        return;

    // Real runtime resize events only ever hand us physical pixels (the
    // browser's ResizeObserver/window 'resize' reports device pixels
    // via Module._fluxPhysicalWidth/Height) — round instead of truncate
    // to minimize (not eliminate) drift here. This path is NOT used for
    // the initial boot-time sync anymore — see applyResize() above and
    // main()'s step 6c.
    double dpr = EM_ASM_DOUBLE({ return Module._fluxDPR || 1.0; });
    int logicalW = (int)std::round(physicalWidth / dpr);
    int logicalH = (int)std::round(physicalHeight / dpr);
    applyResize(logicalW, logicalH);
}

// ============================================================================
// Navigator path-change callback — exposed as a C symbol so shell-registered
// JS ('popstate' listener, wired up in main() below) can call back into
// Navigator::_onPathChange when the browser Back/Forward buttons fire, or
// the user edits the URL path directly. Renamed from
// fluxNavigatorHashChanged (Phase 2: real paths, not hash fragments).
// ============================================================================

extern "C" EMSCRIPTEN_KEEPALIVE void fluxNavigatorPathChanged(const char *path)
{
    Navigator::_onPathChange(path);
}

// ============================================================================
// main
// ============================================================================

int main()
{
    s_app = new FluxUI(nullptr);

    // ── 0. Re-sync viewport globals before using them ────────────────────
    // wrapFullPage()'s inline bootstrap script captured Module._fluxLogical*/
    // _fluxPhysical* immediately after the SSR HTML was parsed — well
    // BEFORE this point. document.fonts.ready + fetching/instantiating this
    // WASM module both have to finish before main() ever runs, and that gap
    // is long enough for the real viewport to have moved on underneath us
    // (most commonly: a mobile browser's address bar auto-collapsing/
    // expanding, which changes window.innerHeight with no guarantee a
    // 'resize' event has fired and been handled yet). Using the stale
    // captured values reproduces the same class of bug as the earlier
    // Win32 swap-chain desync: layout (and, on the canvas backend, the
    // canvas's own backing store) gets sized against a snapshot that no
    // longer matches the real, current viewport, with nothing to resync it
    // before first paint.
    EM_ASM({
        var dpr = window.devicePixelRatio || 1;
        Module._fluxDPR = dpr;
        Module._fluxLogicalWidth = window.innerWidth;
        Module._fluxLogicalHeight = window.innerHeight;
        Module._fluxPhysicalWidth = Math.floor(window.innerWidth * dpr);
        Module._fluxPhysicalHeight = Math.floor(window.innerHeight * dpr);
        if (Module.canvas) {
            Module.canvas.width = Module._fluxPhysicalWidth;
            Module.canvas.height = Module._fluxPhysicalHeight;
        }
    });

    // ── 1. Read physical + logical canvas size ──────────────────────────
    int physW = canvasPhysicalWidth();
    int physH = canvasPhysicalHeight();
    // Read logical (CSS px) size directly rather than deriving it via
    // physW/dpr — avoids a float divide that could round differently
    // than the SSR host's own logical-px viewport (Sec-CH-Viewport-*
    // headers / flux_vw,vh cookies are always integer CSS px, see
    // ssr/main.cpp's resolveViewport()), which this value must agree
    // with for the DOM renderer to boot at the same size hydration is
    // replacing.
    int logicalW = EM_ASM_INT({ return Module._fluxLogicalWidth || (Module._fluxPhysicalWidth / (Module._fluxDPR || 1)) | 0; });
    int logicalH = EM_ASM_INT({ return Module._fluxLogicalHeight || (Module._fluxPhysicalHeight / (Module._fluxDPR || 1)) | 0; });

    // ── 2. Create window FIRST so valid() returns true during build ───────
    // Title comes straight from config (baked in at compile time via
    // target_compile_definitions) — no need to wait on FluxAppWidget,
    // which no longer tracks title/size anyway.
    s_app->createWindow(FLUX_APP_NAME, logicalW, logicalH);

#ifdef FLUX_WEB_RENDERER_DOM
    // Must happen BEFORE build() below — build()'s initial layout pass
    // calls into Painter::measureText/measureRichText immediately, and
    // those need getActiveDomAdapter() to already be non-null (this is
    // the DOM-renderer equivalent of Module._fluxCtx2D already being set
    // up by shell.html before WASM starts, for the canvas renderer).
    fluxDomAdapterLiveInit();
    fluxDomAdapterLiveActivate();
    fluxFontDomInit();
#endif

    // ── 2b. Read + parse the hydration blob, BEFORE build() ────────────────
    // Must happen before build() constructs the widget tree: FutureBuilder-
    // Widget's constructor (see flux_future_builder.hpp) calls
    // fluxHydrationNextId() and immediately checks fluxHydrationGetWidgetData
    // for that id — both need the blob already parsed and the ID counter
    // freshly reset to zero, or IDs/data would be out of sync with what a
    // server (Phase 4) or a manual test harness actually populated.
    fluxHydrationResetIdCounter();
    {
        char *raw = (char *)EM_ASM_PTR({
            var s = Module._fluxHydrationData || "";
            var len = lengthBytesUTF8(s) + 1;
            var buf = _malloc(len);
            stringToUTF8(s, buf, len);
            return buf;
        });
        fluxHydrationParseBlob(raw ? raw : "");
        free(raw);
    }

    // ── 3. Now build — wireCallbacks + rebuild will run layout correctly ──
    s_app->build([&]()
                 { return createApp(s_app); });

    // ── 4. Register painter helpers ───────────────────────────────────────
#ifndef FLUX_WEB_RENDERER_DOM
    fluxPainterWebInit();
#endif

    // ── 5. Register C resize callback with JS ─────────────────────────────
    EM_ASM({
        Module._fluxOnResizeCpp = Module.cwrap('fluxOnResize', null, [ 'number', 'number' ]);
        Module._fluxOnResize = function(w, h)
        {
            Module._fluxOnResizeCpp(w, h);
        };
    });

    // ── 6b. Wire browser Back/Forward (URL path) to the Navigator ──────────
    // Registered after build() so it doesn't see the popstate (if any)
    // that Navigator::init() may have triggered while establishing the
    // initial route's hash.
    EM_ASM({
        // popstate fires on Back/Forward navigation between history
        // entries pushed via history.pushState/replaceState (see
        // Navigator::_setPath) — the real-path equivalent of the old
        // 'hashchange' listener. It does NOT fire for the very first
        // page load, which is intentional: init() above already read the
        // initial path directly via _getInitialPath().
        window.addEventListener('popstate', function() {
            var p = location.pathname || "/";
            Module.ccall('fluxNavigatorPathChanged', null, [ 'string' ], [ p ]); });
    });

    // ── 6c. Fire an initial resize so cachedWidth/Height match the real canvas ──
    // Use the logical size already read in step 1 directly — NOT
    // fluxOnResize(physW, physH), which would re-derive logical size
    // from physical pixels via a lossy round trip (see applyResize()'s
    // header comment) and reintroduce a 1px drift against what SSR (and
    // this file's own initial build() pass a few lines above) already
    // laid out at. physW/physH remain used only for
    // Module._fluxPhysicalWidth-derived concerns elsewhere, not for
    // deriving logical size here.
    applyResize(logicalW, logicalH);

    // ── 7. Hand control to browser ────────────────────────────────────────
    // Force initial repaint
    s_app->getPlatformWindow().invalidate();
    emscripten_set_main_loop(s_tick, 0, 0);

    return 0;
}

#endif // __EMSCRIPTEN__