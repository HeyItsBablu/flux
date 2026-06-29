// flux_oes_blit.hpp
// OES external texture → GL_TEXTURE_2D blit helper (Android only).
//
//   It is the only place the
// GL_OES_EGL_image_external extension is used.  All other rendering
// goes through FluxGL (flux_gl.hpp).
//
// Usage pattern (each frame, before FluxGL draws the UI):
//
//   GLuint tex2D = FluxOESBlit_blit(myOESTexId, videoW, videoH);
//   // tex2D is a regular GL_TEXTURE_2D — pass as NativeImage / GLuint
//   // to Painter::drawVideo / drawCamera.
//
#pragma once
#ifdef __ANDROID__

#include <GLES2/gl2.h>

// Call once after the EGL context is created (before any video is opened).
// maxW / maxH set the size of the internal blit FBO.
void FluxOESBlit_init(int maxW, int maxH);

// Call on APP_CMD_TERM_WINDOW to release GL objects.
void FluxOESBlit_destroy();

// Call on surface reconnect — re-creates shaders and FBO with the same maxW/maxH.
void FluxOESBlit_reinit();

// Blit an OES external texture into the shared GL_TEXTURE_2D and return it.
// Call every frame that a video/camera frame has been updated, BEFORE
// FluxGL_beginFrame so the texture is ready when Painter::drawVideo runs.
// Returns the internal GL_TEXTURE_2D handle (valid until FluxOESBlit_destroy).
GLuint FluxOESBlit_blit(GLuint oesTexId, int w, int h);

// Returns the GL_TEXTURE_2D without blitting (just the handle).
GLuint FluxOESBlit_getTex2D();

#endif // __ANDROID__