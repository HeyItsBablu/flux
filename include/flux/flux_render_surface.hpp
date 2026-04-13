#pragma once
#include "flux_canvas2d.hpp"

// ============================================================================
// flux_render_surface.hpp
// Pure abstract RenderSurface interface — zero platform dependencies.
// Implement this per-platform in platform/win32/flux_canvas_win32.hpp etc.
// ============================================================================

class RenderSurface {
public:
  virtual ~RenderSurface() = default;

  static bool mvpValid(const float mvp[16]) {
    return std::isfinite(mvp[0]) && std::isfinite(mvp[5]) && mvp[0] != 0.f &&
           mvp[5] != 0.f;
  }

  // ── Lifecycle ─────────────────────────────────────────────────────────────
  // initialize() is called once after the GL context is ready.
  // destroy()    is called before the GL context is torn down.
  virtual void initialize(int w, int h) = 0;
  virtual void resize(int w, int h) = 0;
  virtual void destroy() = 0;

  // ── Per-frame ─────────────────────────────────────────────────────────────
  // update() is called before render() each frame, with wall-clock dt in
  // seconds. render() receives a column-major orthographic MVP matrix.
  virtual void update(double dt) = 0;
  virtual void render(Canvas2D &ctx) = 0;

  // ── Input — all coordinates are in canvas space (post-viewport transform) ─
  virtual void onMouseDown(float /*x*/, float /*y*/) {}
  virtual void onMouseMove(float /*x*/, float /*y*/) {}
  virtual void onMouseUp(float /*x*/, float /*y*/) {}
  virtual void onRightMouseDown(float /*x*/, float /*y*/) {}

  // key is a Key:: constant from flux_keys.hpp — never a raw VK_* value.
  virtual void onKeyDown(int /*key*/) {}
  virtual void onKeyUp(int /*key*/) {}

  // ── Hints ─────────────────────────────────────────────────────────────────
  // Return true when the surface needs a steady repaint loop (e.g. animation,
  // cursor blink). When false, repaints are driven by invalidation only.
  virtual bool needsContinuousRedraw() const { return false; }
};