// flux/flux_graphics_context.hpp
#pragma once
#include "flux_platform.hpp"

struct GraphicsContext {
#ifdef _WIN32
  NativeContext hdc;
  explicit GraphicsContext(NativeContext h) : hdc(h) {}
#endif
};