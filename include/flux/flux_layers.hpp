#pragma once

// ============================================================================
// flux_layers.hpp
// Platform-dispatch header for LayeredSurface.
// Always include this — never include platform files directly.
//
// LayeredSurface extends RasterSurface, so flux_raster.hpp must be included
// first. That is handled here — callers only need this one header.
// ============================================================================

#include "flux_raster.hpp"

#ifdef _WIN32
  #include "platform/win32/flux_layers_win32.hpp"
#elif defined(__linux__)
  #include "platform/linux/flux_layers_linux.hpp"
#elif defined(__APPLE__)
  #include "platform/macos/flux_layers_macos.hpp"
#else
  #error "flux_layers.hpp: unsupported platform — implement LayeredSurface for your target"
#endif
