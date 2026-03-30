// flux/flux_canvas.hpp
#pragma once

// ============================================================================
// flux_canvas.hpp
// Platform-dispatch header for CanvasWidget and RenderSurface.
// Always include this — never include platform files directly.
// ============================================================================

// These two have zero GL and zero platform dependencies — safe here.
#include "flux_render_surface.hpp"
#include "flux_canvas_types.hpp"

// flux_glutil.hpp and flux_scrollbar.hpp need GLAD which is platform-specific.
// They are included inside each platform file AFTER glad is included there.

#ifdef _WIN32
  #include "platform/win32/flux_canvas_win32.hpp"
#elif defined(__linux__)
  #include "platform/linux/flux_canvas_linux.hpp"
#elif defined(__APPLE__)
  #include "platform/macos/flux_canvas_macos.hpp"
#else
  #error "flux_canvas.hpp: unsupported platform"
#endif