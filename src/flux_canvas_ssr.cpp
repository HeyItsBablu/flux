// src/flux_canvas_ssr.cpp
//
// FLUX_SSR implementation of CanvasWidget — Option A ("placeholder").
//
// SSR is headless: there is no GPU, no window, and therefore nothing to
// run activeSurface_->render(Canvas2D&) against (see
// flux_canvas2d_gl.cpp's header comment — it's compiled only for
// __ANDROID__ || __EMSCRIPTEN__, never FLUX_SSR). This file's only job is
// to emit a real, correctly-sized, correctly-positioned <canvas> element
// into the SSR HTML, carrying the same data-flux-id every other DOM node
// gets, so the client's hydration pass can ADOPT that exact element (see
// flux_dom_adapter_live.cpp's Module._fluxDomAdopt) and only THEN
// construct a live GL-backed CanvasWidget that actually drives
// RenderSurface::render() against it.
//
// Between first paint and hydration completing, the canvas is visually
// blank — the same tradeoff SSR already makes for VideoPlayerWidget /
// AudioPlayerWidget (flux_painter_dom.cpp's drawVideo/drawCamera are
// PERMANENT no-ops for exactly this reason: real pixels only exist once a
// real element is driven by real client-side code, not during a
// string-building pass).
//
// activeSurface_ / pendingSurface_ are deliberately never touched here —
// setSurface<T>() still compiles and still stores a pendingSurface_ (it's
// plain state, defined in the shared, non-platform-gated part of this
// class), it's just never activated or rendered on this backend. The
// live browser build activates and drives it as normal once hydration's
// CanvasWidget::create() runs.
//
// Option B (tracked separately, not attempted here): plug a software
// rasterizer into Canvas2DBackend_create() under FLUX_SSR and actually
// run one render pass server-side, shipping real pixels via a
// content-addressed image URL (reusing the same registry font/asset
// serving already uses) instead of a blank placeholder.

#ifdef FLUX_SSR

#include "flux/flux_canvas.hpp"
#include "flux/flux_dom_adapter.hpp"

#include <string>

// ============================================================================
// Construction / destruction — no GL/D2D backend to create or release on
// this platform (see file header). Every other field (canvasW_/canvasH_,
// vp_, hBar_/vBar_, etc.) already has in-class default initializers in
// flux_canvas.hpp, so there's nothing left for either of these to do.
// ============================================================================

CanvasWidget::CanvasWidget()
    : hBar_(CustomScrollbar::Axis::Horizontal),
      vBar_(CustomScrollbar::Axis::Vertical)
{
}

CanvasWidget::~CanvasWidget() = default;

// ============================================================================
// CanvasWidget::computeLayout
//
// No RenderSurface to consult and no widget children to recurse into
// (activeSurface_ is a RenderSurface, not a Widget — it's never activated
// on this backend anyway). Sizing follows the same intrinsic-size-vs-
// constraints rule every leaf flex item uses elsewhere in the layout
// system: canvasW_/canvasH_ (set via setSize()/setCanvasSize()) are the
// intrinsic size; widthMode/heightMode == SizeMode::Full lets the parent's
// constraints override that, matching how Full-mode widgets behave on
// every other platform's CanvasWidget.
// ============================================================================

void CanvasWidget::computeLayout(GraphicsContext & /*ctx*/,
                                 const BoxConstraints &constraints,
                                 FontCache & /*fontCache*/)
{
    if (!visible)
    {
        width = height = 0;
        needsLayout = false;
        return;
    }

    width = (widthMode == SizeMode::Full) ? constraints.maxWidth : canvasW_;
    height = (heightMode == SizeMode::Full) ? constraints.maxHeight : canvasH_;

    width = constraints.clampWidth(width);
    height = constraints.clampHeight(height);
    applyConstraints();

    needsLayout = false;
}

// ============================================================================
// Configuration setters/getters — declared in flux_canvas.hpp with no
// inline body, so (per that header's own note — "CanvasWidget is a fully
// standalone Widget on every platform," deliberately duplicated rather
// than sharing a base) every platform's .cpp must define these itself.
// ============================================================================

std::shared_ptr<CanvasWidget> CanvasWidget::setViewportEnabled(bool e)
{
    viewportEnabled_ = e;
    return ptr();
}

std::shared_ptr<CanvasWidget> CanvasWidget::setScrollbarsEnabled(bool e)
{
    scrollbarsEnabled_ = e;
    return ptr();
}

bool CanvasWidget::scrollbarsEnabled() const { return scrollbarsEnabled_; }

RenderSurface *CanvasWidget::getSurface() const { return activeSurface_.get(); }

const Viewport &CanvasWidget::viewport() const { return vp_; }
Viewport &CanvasWidget::viewport() { return vp_; }

std::shared_ptr<CanvasWidget> CanvasWidget::setSize(int w, int h)
{
    canvasW_ = w;
    canvasH_ = h;
    markNeedsLayout();
    return ptr();
}

std::shared_ptr<CanvasWidget> CanvasWidget::setCanvasSize(int w, int h)
{
    docW_ = w;
    docH_ = h;
    return ptr();
}

std::shared_ptr<CanvasWidget> CanvasWidget::redraw()
{
    // No dirty-flag/invalidate machinery on this backend (see
    // flux_window_headless.cpp — invalidate() is already a no-op for the
    // same reason: a one-shot render has nothing to re-trigger).
    return ptr();
}

// ============================================================================
// Shared helpers — same "declared, no inline body, every platform defines
// its own" situation as above. None of these are ever exercised at
// runtime on this backend (no mouse input, no live viewport math), but
// they still need bodies to link if any shared/app code calls them.
// ============================================================================

void CanvasWidget::viewportDims(int glW, int glH, int &vpW, int &vpH) const
{
    // Identity — no pan/zoom viewport concept without a live render loop.
    vpW = glW;
    vpH = glH;
}

void CanvasWidget::updateViewportSize(int, int) {}
void CanvasWidget::updateSBGeometry(int, int) {}

void CanvasWidget::beginPan(int, int) {}
void CanvasWidget::continuePan(int, int) {}

void CanvasWidget::pokeScrollbars()
{
    hBar_.poke();
    vBar_.poke();
}

void CanvasWidget::applyHScrollFraction(float) {}
void CanvasWidget::applyVScrollFraction(float) {}

void CanvasWidget::activatePendingSurface()
{
    if (pendingSurface_)
    {
        activeSurface_ = pendingSurface_;
        pendingSurface_.reset();
        // activeSurface_->initialize(w, h) is deliberately never called
        // here — there is no GPU/window to initialize against (see file
        // header). The live browser build runs the real
        // initialize()+render() sequence once hydration boots a
        // GL-backed CanvasWidget against the adopted <canvas> element.
    }
}

// ============================================================================
// CanvasWidget::render
//
// fluxDomEnsureNode() (declared in flux_dom_adapter.hpp, defined in
// flux_painter_dom.cpp) is the same get-or-create-plus-position entry
// point TextInputWidget uses to get a real DOM element without going
// through any Painter primitive — it already applies this widget's
// current x/y/width/height as the node's CSS box internally (see
// ensureNode()'s trailing applyRect() call), so there's no need to call
// fluxDomApplyRect separately here.
//
// width/height ATTRIBUTES (not CSS) are set explicitly because they're
// independent on <canvas>: the pixel backing-store size defaults to
// 300x150 if left unset, which would stretch/distort whatever the live
// renderer draws after hydration until the next resize event fires.
// ============================================================================

void CanvasWidget::render(GraphicsContext & /*ctx*/, FontCache &fontCache)
{
    if (!visible)
        return;

    activatePendingSurface();

    DomNodeHandle node = fluxDomEnsureNode(this, "canvas");

    // setAttr is a virtual on IDomAdapter, not a free function — go
    // through the active adapter, same as every other SSR/live call site.
    if (IDomAdapter *adapter = getActiveDomAdapter())
    {
        // Backing-store size — NOT CSS. Left unset this defaults to
        // 300x150, which stretches/distorts the surface's draw calls
        // until the next resize event fires post-hydration.
        adapter->setAttr(node, "width", std::to_string(canvasW_));
        adapter->setAttr(node, "height", std::to_string(canvasH_));
    }

    needsPaint = false;
}

// ============================================================================
// CanvasWidget::onDetach
//
// No GL/D2D resources were ever allocated on this backend (see file
// header) — nothing extra to release beyond the normal DOM-node eviction
// Widget::onDetach() already performs for every widget type via
// fluxDomEvictWidget(this).
// ============================================================================

void CanvasWidget::onDetach()
{
    Widget::onDetach();
}

// ============================================================================
// CanvasWidget::markNeedsPaint
//
// No dirty-rect/invalidate machinery to poke on a one-shot render — see
// flux_window_headless.cpp's PlatformWindow::invalidate(), which is
// already a no-op for the same reason. Just keeps the flag itself
// consistent for anything that reads needsPaint directly.
// ============================================================================

void CanvasWidget::markNeedsPaint()
{
    needsPaint = true;
}

#endif // FLUX_SSR