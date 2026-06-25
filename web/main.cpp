// src/main_web.cpp
//
// Web entry point for FluxUI (Emscripten / WASM).
//
// Execution flow:
//   main()
//     └─ FluxUI(nullptr)              — no AppInstance on web
//     └─ app.build(createApp)         — runs builder, wires widget tree
//     └─ FluxAppWidget::getInstance() — reads title / size config
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
//   => Window size:  FluxAppWidget::windowWidth / windowHeight are ignored.
//     The canvas always fills the browser viewport.  Physical pixel
//     dimensions are read from Module._fluxPhysicalWidth / _fluxPhysicalHeight
//     (set by shell.html's resizeCanvases() before WASM starts).
//
//   => fullscreen / maximize flags are also ignored — the CSS already makes
//     both canvases 100 % width/height.
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

#include "flux/flux.hpp"
#include "flux/flux_navigator.hpp"
#include "flux/flux_http_platform.hpp"

// ============================================================================
// Forward declaration — defined by the application (e.g. helloworld.cpp)
// ============================================================================

WidgetPtr createApp(FluxUI *app);
extern "C" void fluxPainterWebInit();
extern "C" EMSCRIPTEN_WEBGL_CONTEXT_HANDLE fluxGetGLContext();

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

            // Always invalidate — canvas surfaces need continuous redraw
            s_app->getPlatformWindow().invalidate();

            s_app->getPlatformWindow().tick();
        }
    }

} // namespace

// ============================================================================
// Resize callback — registered with JS by flux_window_web.cpp,
// but also callable directly from C++ (e.g. on orientation change).
// Exposed as a C symbol so shell.html can call Module._fluxOnResizeCpp(w,h).
// ============================================================================

extern "C" EMSCRIPTEN_KEEPALIVE void fluxOnResize(int physicalWidth, int physicalHeight)
{
    if (!s_app)
        return;

    double dpr = EM_ASM_DOUBLE({ return Module._fluxDPR || 1.0; });
    int logicalW = (int)(physicalWidth / dpr);
    int logicalH = (int)(physicalHeight / dpr);

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
// Navigator hash-change callback — exposed as a C symbol so shell-registered
// JS ('hashchange' listener, wired up in main() below) can call back into
// Navigator::_onHashChange when the browser Back/Forward buttons fire, or
// the user edits the URL hash directly.
// ============================================================================

extern "C" EMSCRIPTEN_KEEPALIVE void fluxNavigatorHashChanged(const char *hash)
{
    Navigator::_onHashChange(hash);
}

// ============================================================================
// main
// ============================================================================

int main()
{
    s_app = new FluxUI(nullptr);

    // ── 1. Read physical canvas size ──────────────────────────────────────
    int physW = canvasPhysicalWidth();
    int physH = canvasPhysicalHeight();
    double dpr = EM_ASM_DOUBLE({ return Module._fluxDPR || 1.0; });
    int logicalW = (int)(physW / dpr); // 666 logical
    int logicalH = (int)(physH / dpr); // 734 logical

    auto cfg = FluxAppWidget::getInstance(); // may be null before build, see below
    const std::string title = "FluxUI";      // temp title, updated after build

    // ── 2. Create window FIRST so valid() returns true during build ───────
    s_app->createWindow(title, logicalW, logicalH);

    // ── 3. Now build — wireCallbacks + rebuild will run layout correctly ──
    s_app->build([&]()
                 { return createApp(s_app); });

    // ── 4. Read actual app config after build ─────────────────────────────
    cfg = FluxAppWidget::getInstance();
    // (title is already set; window is live)

    // ── 5. Register painter helpers ───────────────────────────────────────
    fluxPainterWebInit();

    // ── 6. Register C resize callback with JS ─────────────────────────────
    EM_ASM({
        Module._fluxOnResizeCpp = Module.cwrap('fluxOnResize', null, [ 'number', 'number' ]);
        Module._fluxOnResize = function(w, h)
        {
            Module._fluxOnResizeCpp(w, h);
        };
    });

    // ── 6b. Wire browser Back/Forward (URL hash) to the Navigator ─────────
    // Registered after build() so it doesn't see the hashchange (if any)
    // that Navigator::init() may have triggered while establishing the
    // initial route's hash.
    EM_ASM({
        window.addEventListener('hashchange', function() {
            var h = location.hash.length > 1 ? location.hash.slice(1) : "";
            Module.ccall('fluxNavigatorHashChanged', null, [ 'string' ], [ h ]); });
    });

    // ── 6c. Fire an initial resize so cachedWidth/Height match the real canvas ──
    fluxOnResize(physW, physH);

    // ── 7. Hand control to browser ────────────────────────────────────────
    // Force initial repaint
    s_app->getPlatformWindow().invalidate();
    emscripten_set_main_loop(s_tick, 0, 0);

    return 0;
}

#endif // __EMSCRIPTEN__