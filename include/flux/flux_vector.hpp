#pragma once

// ============================================================================
// flux_vector.hpp
// Platform-dispatch header for VectorSurface and related types.
// Always include this — never include platform files directly.
//
// Depends on CanvasWidget, so flux_canvas.hpp must be included first.
// That is handled here — callers only need this one header.
// ============================================================================

#include "flux_canvas.hpp"

#ifdef _WIN32
  #include "platform/win32/flux_vector_win32.hpp"
#elif defined(__linux__)
  #include "platform/linux/flux_vector_linux.hpp"
#elif defined(__APPLE__)
  #include "platform/macos/flux_vector_macos.hpp"
#else
  #error "flux_vector.hpp: unsupported platform — implement VectorSurface for your target"
#endif