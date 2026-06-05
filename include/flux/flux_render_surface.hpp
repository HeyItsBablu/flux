#pragma once
#include "flux_canvas2d.hpp"

struct KeyEvent
{
  int codepoint;  // printable char (Unicode), 0 if non-printable
  int virtualKey; // platform-normalized key (use your Key:: enum)
  bool ctrl;
  bool shift;
  bool alt;
};

class RenderSurface
{
public:
  virtual ~RenderSurface() = default;

  static bool mvpValid(const float mvp[16])
  {
    return std::isfinite(mvp[0]) && std::isfinite(mvp[5]) && mvp[0] != 0.f && mvp[5] != 0.f;
  }

  virtual void initialize(int w, int h) = 0;
  virtual void resize(int w, int h) = 0;
  virtual void destroy() = 0;

  virtual void update(double dt) = 0;

  // Called BEFORE nvgBeginFrame — safe to use raw GL here.
  // Default is no-op; override in surfaces that run their own GL passes.
  virtual void preRender() {}

  // Called INSIDE nvgBeginFrame/nvgEndFrame — NanoVG only here.
  virtual void render(Canvas2D &ctx) = 0;

  virtual void onMouseDown(float, float) {}
  virtual void onMouseMove(float, float) {}
  virtual void onMouseUp(float, float) {}
  virtual void onRightMouseDown(float, float) {}
  virtual void onKeyDown(const KeyEvent &) {}
  virtual void onKeyUp(const KeyEvent &) {}

  virtual bool needsContinuousRedraw() const { return false; }
};