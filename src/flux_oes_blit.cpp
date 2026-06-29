// flux_oes_blit.cpp
// OES external texture → GL_TEXTURE_2D blit.
// Self-contained GL ES 2 code.
#ifdef __ANDROID__

#include "flux/flux_oes_blit.hpp"

#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>   // GL_TEXTURE_EXTERNAL_OES
#include <android/log.h>
#include <cstring>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  "FluxOES", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "FluxOES", __VA_ARGS__)

// ============================================================================ 
// SHADER SOURCE
// Samples GL_TEXTURE_EXTERNAL_OES and writes to the FBO color attachment.
// ============================================================================

static const char* kBlitVS = R"(
attribute vec2 aPos;
attribute vec2 aUV;
varying   vec2 vUV;
void main() {
    vUV         = aUV;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

// The extension pragma MUST be at the very top of the fragment shader.
static const char* kBlitFS = R"(#extension GL_OES_EGL_image_external : require
precision mediump float;
uniform samplerExternalOES uTex;
varying vec2 vUV;
void main() {
    gl_FragColor = texture2D(uTex, vUV);
}
)";

// ============================================================================
// STATE
// ============================================================================

struct OESBlitState
{
    GLuint fbo      = 0;
    GLuint tex2D    = 0;    // render target — regular GL_TEXTURE_2D
    GLuint program  = 0;
    GLuint vbo      = 0;
    int    maxW     = 0;
    int    maxH     = 0;
    bool   ready    = false;
};

static OESBlitState s_blit;

// ============================================================================
// INTERNAL HELPERS
// ============================================================================

static GLuint compileShader(GLenum type, const char* src)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char buf[512];
        glGetShaderInfoLog(s, sizeof(buf), nullptr, buf);
        LOGE("Shader compile error: %s", buf);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

static GLuint buildProgram()
{
    GLuint vs = compileShader(GL_VERTEX_SHADER,   kBlitVS);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, kBlitFS);
    if (!vs || !fs) { glDeleteShader(vs); glDeleteShader(fs); return 0; }

    GLuint p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    // Bind attribute locations before linking so we can use fixed indices.
    glBindAttribLocation(p, 0, "aPos");
    glBindAttribLocation(p, 1, "aUV");
    glLinkProgram(p);

    glDetachShader(p, vs); glDeleteShader(vs);
    glDetachShader(p, fs); glDeleteShader(fs);

    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char buf[512];
        glGetProgramInfoLog(p, sizeof(buf), nullptr, buf);
        LOGE("Program link error: %s", buf);
        glDeleteProgram(p);
        return 0;
    }
    return p;
}

// Full-screen NDC quad (triangle strip): pos(xy) + uv(xy), interleaved.
// Covers [-1,1] × [-1,1] in clip space — fills the FBO viewport exactly.
static const float kQuadVerts[] = {
    //  x      y     u     v
    -1.f, -1.f,  0.f,  0.f,
     1.f, -1.f,  1.f,  0.f,
    -1.f,  1.f,  0.f,  1.f,
     1.f,  1.f,  1.f,  1.f,
};

static void createGLObjects(int maxW, int maxH)
{
    // ── Render-target texture (regular 2D — sampled by FluxGL Painter) ────────
    glGenTextures(1, &s_blit.tex2D);
    glBindTexture(GL_TEXTURE_2D, s_blit.tex2D);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, maxW, maxH, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    // ── FBO ───────────────────────────────────────────────────────────────────
    glGenFramebuffers(1, &s_blit.fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, s_blit.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, s_blit.tex2D, 0);

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE)
        LOGE("FBO incomplete: 0x%x", status);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // ── Blit shader ───────────────────────────────────────────────────────────
    s_blit.program = buildProgram();

    // ── Quad VBO ──────────────────────────────────────────────────────────────
    glGenBuffers(1, &s_blit.vbo);
    glBindBuffer(GL_ARRAY_BUFFER, s_blit.vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kQuadVerts), kQuadVerts, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    s_blit.maxW  = maxW;
    s_blit.maxH  = maxH;
    s_blit.ready = (s_blit.tex2D && s_blit.fbo && s_blit.program && s_blit.vbo);

    if (s_blit.ready)
        LOGI("OES blit ready (%dx%d)", maxW, maxH);
    else
        LOGE("OES blit init failed");
}

static void destroyGLObjects()
{
    if (s_blit.vbo)     { glDeleteBuffers(1,      &s_blit.vbo);     s_blit.vbo     = 0; }
    if (s_blit.program) { glDeleteProgram(s_blit.program);          s_blit.program = 0; }
    if (s_blit.fbo)     { glDeleteFramebuffers(1, &s_blit.fbo);     s_blit.fbo     = 0; }
    if (s_blit.tex2D)   { glDeleteTextures(1,     &s_blit.tex2D);   s_blit.tex2D   = 0; }
    s_blit.ready = false;
}

// ============================================================================
// PUBLIC API
// ============================================================================

void FluxOESBlit_init(int maxW, int maxH)
{
    if (s_blit.ready) return; // already initialised
    createGLObjects(maxW, maxH);
}

void FluxOESBlit_destroy()
{
    destroyGLObjects();
}

void FluxOESBlit_reinit()
{
    int w = s_blit.maxW ? s_blit.maxW : 1920;
    int h = s_blit.maxH ? s_blit.maxH : 1080;
    destroyGLObjects();
    createGLObjects(w, h);
}

GLuint FluxOESBlit_getTex2D()
{
    return s_blit.tex2D;
}

GLuint FluxOESBlit_blit(GLuint oesTexId, int w, int h)
{
    if (!s_blit.ready) return 0;

    // ── Save GL state we will clobber ─────────────────────────────────────────
    GLint prevFBO       = 0;
    GLint prevProgram   = 0;
    GLint prevVBO       = 0;
    GLint prevViewport[4] = {};
    GLint prevActiveTex = 0;
    GLint prevTex2D     = 0;
    GLint prevTexOES    = 0;
    GLboolean prevBlend = GL_FALSE;

    glGetIntegerv(GL_FRAMEBUFFER_BINDING,           &prevFBO);
    glGetIntegerv(GL_CURRENT_PROGRAM,               &prevProgram);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING,          &prevVBO);
    glGetIntegerv(GL_VIEWPORT,                       prevViewport);
    glGetIntegerv(GL_ACTIVE_TEXTURE,                &prevActiveTex);
    glGetBooleanv(GL_BLEND,                         &prevBlend);

    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D,            &prevTex2D);
    glGetIntegerv(GL_TEXTURE_BINDING_EXTERNAL_OES,  &prevTexOES);

    // ── Blit pass ─────────────────────────────────────────────────────────────
    glDisable(GL_BLEND);        // no blending for a straight copy
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);

    glBindFramebuffer(GL_FRAMEBUFFER, s_blit.fbo);
    glViewport(0, 0, w, h);

    glUseProgram(s_blit.program);

    // Bind OES texture to unit 0 and set the sampler uniform
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, oesTexId);
    GLint uTex = glGetUniformLocation(s_blit.program, "uTex");
    if (uTex >= 0) glUniform1i(uTex, 0);

    // Draw the full-screen quad
    glBindBuffer(GL_ARRAY_BUFFER, s_blit.vbo);

    glEnableVertexAttribArray(0);   // aPos
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE,
                          4 * sizeof(float), (void*)0);

    glEnableVertexAttribArray(1);   // aUV
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE,
                          4 * sizeof(float), (void*)(2 * sizeof(float)));

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);

    // ── Restore GL state ──────────────────────────────────────────────────────
    glBindBuffer(GL_ARRAY_BUFFER,           prevVBO);
    glUseProgram(prevProgram);
    glBindFramebuffer(GL_FRAMEBUFFER,       prevFBO);
    glViewport(prevViewport[0], prevViewport[1],
               prevViewport[2], prevViewport[3]);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D,            prevTex2D);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES,  prevTexOES);
    glActiveTexture(prevActiveTex);

    if (prevBlend) glEnable(GL_BLEND);

    return s_blit.tex2D;
}

// ============================================================================
// LEGACY ENTRY POINTS
// These thin wrappers keep existing call sites in video/camera code
// (FluxVideo, FluxCamera) compiling without changes.
// Remove them once those call sites are updated to use FluxOESBlit_* directly.
// ============================================================================

extern "C" {

void NVG_initOESBlit(int maxW, int maxH)
{
    FluxOESBlit_init(maxW, maxH);
}

GLuint NVG_blitOESToTex2D(GLuint oesTexId, int w, int h)
{
    return FluxOESBlit_blit(oesTexId, w, h);
}

} // extern "C"

#endif // __ANDROID__