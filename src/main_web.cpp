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

#ifdef __EMSCRIPTEN__

#include <emscripten.h>
#include <emscripten/html5.h>
#include <cstdio>

#include "flux/flux.hpp"

// ============================================================================
// Forward declaration — defined by the application (e.g. helloworld.cpp)
// ============================================================================

WidgetPtr createApp(FluxUI *app);

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
        if (s_app)
        {
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

    PlatformWindow &win = s_app->getPlatformWindow();
    win.cachedWidth = physicalWidth;
    win.cachedHeight = physicalHeight;

    // Trigger a full layout pass with the new dimensions, then repaint.
    if (win.callbacks.onResize)
    {
        GraphicsContext ctx(physicalWidth, physicalHeight);
        win.callbacks.onResize(ctx, physicalWidth, physicalHeight);
    }
    win.invalidate();
}

// ============================================================================
// main
// ============================================================================

int main()
{

#ifdef FLUX_DEBUG
    // Unbuffered stdout so printf/puts appear in the browser console immediately.
    setvbuf(stdout, nullptr, _IONBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);
#endif

    // ── 1. Create FluxUI ──────────────────────────────────────────────────────
    //
    // AppInstance is nullptr on web — there is no platform handle equivalent.
    s_app = new FluxUI(nullptr);

    // ── 2. Build widget tree ──────────────────────────────────────────────────
    //
    // build() calls startupGdiplus() (no-op on web), runs the builder lambda,
    // and wires the widget tree.  createApp() is user-supplied.
    s_app->build([&]()
                 { return createApp(s_app); });

    // ── 3. Read app config ────────────────────────────────────────────────────
    //
    // FluxAppWidget stores the title the app author declared.  Window size and
    // fullscreen flags are ignored on web — the canvas fills the viewport.
    auto cfg = FluxAppWidget::getInstance();

    const std::string title = cfg ? cfg->title : "FluxUI";

    // Physical pixel dimensions — already DPR-scaled by shell.html.
    int w = canvasPhysicalWidth();
    int h = canvasPhysicalHeight();

    // Fallback: if JS hasn't set dimensions yet (shouldn't happen in normal
    // shell.html flow), use a sensible default and let the resize callback
    // correct it on the first rAF.
    if (w <= 0)
        w = 800;
    if (h <= 0)
        h = 600;

    // ── 4. Create window (registers JS event listeners) ───────────────────────
    //
    // On web "create window" means:
    //   • Storing the canvas selector ("#flux-ui") as the NativeWindow handle
    //   • Registering keyboard / mouse / touch / resize callbacks via
    //     emscripten_set_*_callback in flux_window_web.cpp
    //   • Running the initial layout pass with (w, h)
    s_app->createWindow(title, w, h);

    // ── 5. Register the C resize callback with JS ─────────────────────────────
    //
    // shell.html checks Module._fluxOnResize after onRuntimeInitialized.
    // We point it at our C wrapper so the JS resize handler can call back into
    // C++ when the browser window changes size.
    EM_ASM({
        Module._fluxOnResize = function(w, h)
        {
            // w and h are physical pixels passed from shell.html's resizeCanvases().
            Module._fluxOnResizeCpp(w, h);
        };
    });

    // ── 6. Hand control to the browser ───────────────────────────────────────
    //
    // simulate_infinite_loop = 0: main() returns normally; Emscripten keeps
    // calling s_tick() via rAF until the page is closed.
    // fps = 0: use requestAnimationFrame (matches display refresh rate).
    //
    // Do NOT call s_app->run() — that would enter a blocking loop and freeze
    // the browser tab.
    emscripten_set_main_loop(s_tick, /*fps=*/0, /*simulate_infinite_loop=*/0);

    // main() returns here.  s_app stays alive (static storage, never deleted).
    // Emscripten continues to drive s_tick() via rAF.
    return 0;
}

#endif // __EMSCRIPTEN__