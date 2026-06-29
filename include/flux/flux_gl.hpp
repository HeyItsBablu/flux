// flux_gl.hpp
// Public API for the OpenGL ES 2D renderer (Android).

#pragma once
#ifdef __ANDROID__

#include <string>

struct FluxGL; // opaque to callers

// ── Lifecycle (call from flux_android_main.cpp) ───────────────────────────────

// Call once after EGL context is created (APP_CMD_INIT_WINDOW, first launch).
void FluxGL_init();

// Call after surface/context is re-created (APP_CMD_INIT_WINDOW reconnect).
// Re-compiles shaders and re-uploads atlas; font data survives.
void FluxGL_reinit();

// Call on APP_CMD_TERM_WINDOW — releases all GL objects (shaders, textures, VBO).
void FluxGL_destroy();

// Call at the start of every frame, before Renderer::renderWidget.
// w/h are logical pixels; dpi converts to physical.
void FluxGL_beginFrame(float w, float h, float dpi);

// ── Font registration (call from flux_font_android.cpp / main) ────────────────

// Register a TTF font by name and file path.
// Returns a font index (>=0) on success, -1 on failure.
// If the same name is registered twice, returns the existing index.
int FluxGL_registerFont(const std::string& name, const std::string& path);

// Look up an already-registered font by name. Returns -1 if not found.
int FluxGL_findFont(const std::string& name);

// Internal: access the singleton (used by Painter implementation).
FluxGL* FluxGL_get();

#endif // __ANDROID__