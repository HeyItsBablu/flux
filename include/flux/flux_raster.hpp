#pragma once

// ============================================================================
// flux_raster.hpp
// Platform-dispatch header for RasterSurface.
// Always include this — never include platform files directly.
//
// Depends on CanvasWidget, so flux_canvas.hpp must be included first.
// That is handled here — callers only need this one header.
// ============================================================================

#include "flux_canvas.hpp"

#ifdef _WIN32
  #include "platform/win32/flux_raster_win32.hpp"
#elif defined(__linux__)
  #include "platform/linux/flux_raster_linux.hpp"
#elif defined(__APPLE__)
  #include "platform/macos/flux_raster_macos.hpp"
#else
  #error "flux_raster.hpp: unsupported platform — implement RasterSurface for your target"
#endif