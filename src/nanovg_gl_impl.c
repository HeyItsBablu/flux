#if defined(NANOVG_GL2_IMPLEMENTATION)
  #include <glad/glad.h>
  #include "nanovg_gl.h"
#elif defined(NANOVG_GL3_IMPLEMENTATION)
  #include <glad/glad.h>
  #include "nanovg.h"  
  #include "nanovg_gl.h"
#elif defined(NANOVG_GLES2_IMPLEMENTATION) || defined(NANOVG_GLES3_IMPLEMENTATION)
  #include <GLES2/gl2.h>
  #include "nanovg_gl.h"
#endif