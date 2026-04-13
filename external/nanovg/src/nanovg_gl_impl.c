//nanovg_gl_impel.c
#if defined(NANOVG_GL2_IMPLEMENTATION) || defined(NANOVG_GL3_IMPLEMENTATION)
#include <glad/glad.h>
#elif defined(NANOVG_GLES2_IMPLEMENTATION) || defined(NANOVG_GLES3_IMPLEMENTATION)
#include <GLES2/gl2.h>
#endif

// nanovg.h must always come before nanovg_gl.h
#include "nanovg.h"
#include "nanovg_gl.h"