// src/flux_canvas2d_ssr.cpp
//
// FLUX_SSR stub implementation of Canvas2D — Option A.
//
// SSR is headless: there is no GL context, no D2D device, nothing to
// actually rasterize against (see flux_canvas2d_gl.cpp's header comment —
// it's compiled only for __ANDROID__ || __EMSCRIPTEN__; there is no
// FLUX_SSR-specific real backend). But lib/main.cpp's app code (e.g.
// TriangleSurface::render(Canvas2D&)) is compiled unconditionally into
// every executable target, flux_ssr included — so every Canvas2D method
// it might call must at least LINK, even though CanvasWidget::render()
// on this backend (flux_canvas_ssr.cpp) never actually constructs a
// Canvas2D or invokes RenderSurface::render() at runtime.
//
// Every method here is a no-op / safe-default. None of them are ever
// exercised at runtime on this backend (dead code, not misbehaving
// code) — the SSR HTML output for a canvas comes entirely from
// flux_canvas_ssr.cpp's placeholder <canvas> node, not from anything in
// this file.
//
// Option B (tracked separately): replace this file's bodies with a real
// software rasterizer so SSR can ship actual pixels.

#ifdef FLUX_SSR

#include "flux/flux_canvas2d.hpp"

// ============================================================================
// Canvas2DBackend — opaque per flux_canvas2d.hpp; empty here since this
// backend never allocates GPU resources, a font atlas, or anything else
// a real backend would need. Nothing outside this TU ever dereferences
// it (flux_canvas_ssr.cpp never creates one).
// ============================================================================

struct Canvas2DBackend
{
};

// ============================================================================
// Canvas2D — construction
// ============================================================================

Canvas2D::Canvas2D(Canvas2DBackend *backend, int canvasW, int canvasH)
    : backend_(backend), w_(canvasW), h_(canvasH)
{
    // No ctm_/Mat3 member exists in this build (that member is only
    // compiled under __ANDROID__ / __EMSCRIPTEN__ / Apple-Metal — see
    // flux_canvas2d.hpp's private section) — nothing else to initialize.
}

// ── State stack ──────────────────────────────────────────────────────────
void Canvas2D::save() {}
void Canvas2D::restore() {}

// ── Transform ─────────────────────────────────────────────────────────────
void Canvas2D::translate(float, float) {}
void Canvas2D::scale(float, float) {}
void Canvas2D::rotate(float) {}
void Canvas2D::resetTransform() {}

// ── Style ─────────────────────────────────────────────────────────────────
void Canvas2D::setFillColor(Color c) { fillColor_ = c; fillIsGrad_ = false; }
void Canvas2D::setStrokeColor(Color c) { strokeColor_ = c; }
void Canvas2D::setLineWidth(float w) { lineWidth_ = w; }
void Canvas2D::setLineCap(LineCap) {}
void Canvas2D::setLineJoin(LineJoin) {}
void Canvas2D::setMiterLimit(float) {}
void Canvas2D::setGlobalAlpha(float a) { globalAlpha_ = a; }
void Canvas2D::setCompositeOp(CompositeOp op) { compositeOp_ = op; }
void Canvas2D::setFillRule(FillRule rule) { fillRule_ = rule; }

// ── Gradient ──────────────────────────────────────────────────────────────
void Canvas2D::beginLinearGradient(float, float, float, float) { gStops_.clear(); }
void Canvas2D::beginRadialGradient(float, float, float, float) { gStops_.clear(); }
void Canvas2D::addColorStop(float t, Color c) { gStops_.push_back({t, c}); }
void Canvas2D::setFillGradient() { fillIsGrad_ = true; }

// ── Primitives ────────────────────────────────────────────────────────────
void Canvas2D::clearRect(float, float, float, float) {}
void Canvas2D::fillRect(float, float, float, float) {}
void Canvas2D::strokeRect(float, float, float, float) {}
void Canvas2D::fillRoundedRect(float, float, float, float, float) {}
void Canvas2D::strokeRoundedRect(float, float, float, float, float) {}
void Canvas2D::fillCircle(float, float, float) {}
void Canvas2D::strokeCircle(float, float, float) {}

// ── Path API ──────────────────────────────────────────────────────────────
void Canvas2D::beginPath() { path_.clear(); }
void Canvas2D::closePath() {}
void Canvas2D::moveTo(float x, float y) { pathStartX_ = x; pathStartY_ = y; curX_ = x; curY_ = y; }
void Canvas2D::lineTo(float x, float y) { curX_ = x; curY_ = y; }
void Canvas2D::arc(float, float, float, float, float, bool) {}
void Canvas2D::arcTo(float, float, float, float, float) {}
void Canvas2D::quadraticCurveTo(float, float, float x, float y) { curX_ = x; curY_ = y; }
void Canvas2D::bezierCurveTo(float, float, float, float, float x, float y) { curX_ = x; curY_ = y; }
void Canvas2D::rect(float, float, float, float) {}
void Canvas2D::ellipse(float, float, float, float, float, float, float, bool) {}
void Canvas2D::fill() {}
void Canvas2D::stroke() {}
void Canvas2D::clip() {}

// ── Font registration ────────────────────────────────────────────────────
bool Canvas2D::registerFont(Canvas2DBackend *, const std::string &, const std::string &)
{
    return false; // no font atlas on this backend — see file header
}

// ── Image ─────────────────────────────────────────────────────────────────
Canvas2DImage *Canvas2D::loadImage(const std::string &) { return nullptr; }
Canvas2DImage *Canvas2D::loadImageFromMemory(const unsigned char *, int) { return nullptr; }
void Canvas2D::updateImage(Canvas2DImage *, const unsigned char *, int, int) {}
void Canvas2D::freeImage(Canvas2DImage *img) { delete img; }
void Canvas2D::drawImage(const Canvas2DImage *, float, float) {}
void Canvas2D::drawImage(const Canvas2DImage *, float, float, float, float) {}
void Canvas2D::drawImage(const Canvas2DImage *, float, float, float, float, float, float, float, float) {}

// ── Text ──────────────────────────────────────────────────────────────────
void Canvas2D::setFont(const std::string &) {}
void Canvas2D::setTextAlign(CanvasTextAlign a) { textAlign_ = a; }
void Canvas2D::setTextBaseline(TextBaseline b) { textBaseline_ = b; }
void Canvas2D::fillText(const std::string &, float, float, float) {}
void Canvas2D::strokeText(const std::string &, float, float, float) {}
float Canvas2D::measureText(const std::string &) { return 0.f; }
int Canvas2D::currentFontIdx() const { return -1; }
float Canvas2D::getKernAdvance(int, int, int, int) const { return 0.f; }

// ── Clip rect ─────────────────────────────────────────────────────────────
void Canvas2D::pushClipRect(float, float, float, float) {}
void Canvas2D::popClipRect() {}

// ── Pixel access ──────────────────────────────────────────────────────────
void Canvas2D::getImageData(float, float, float w, float h, std::vector<uint8_t> &out)
{
    out.assign(size_t(std::max(0.f, w)) * size_t(std::max(0.f, h)) * 4, 0);
}
void Canvas2D::putImageData(const std::vector<uint8_t> &, int, int, float, float) {}

#endif // FLUX_SSR