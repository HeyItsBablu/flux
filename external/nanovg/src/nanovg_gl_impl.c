// nanovg_gl_impl.c

// Prerequisites that nanovg_gl.h depends on but does not include itself
#include <GLES2/gl2.h>   // GLuint, GLint, etc.
#include "nanovg.h"      // NVGcontext

// Now the implementation
#define NANOVG_GLES2_IMPLEMENTATION
#include "nanovg_gl.h"