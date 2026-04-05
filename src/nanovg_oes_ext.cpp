
#ifdef __ANDROID__
#include "nanovg.h"
#include "nanovg_gl.h"
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>


struct GLNVGtexture {
    int  id;
    GLuint tex;
    int  width, height;
    int  type;
    int  flags;
};

struct GLNVGcontext {

    GLNVGtexture* textures;
    int           ntextures;
    int           ctextures;

};


extern "C" {
int  nvglCreateImageFromHandleGLES2(NVGcontext* ctx, GLuint textureId,
                                    int w, int h, int flags);
GLuint nvglImageHandleGLES2(NVGcontext* ctx, int image);
}


int NVG_createImageFromOES(NVGcontext* vg, GLuint oesTexId, int w, int h) {

    int img = nvglCreateImageFromHandleGLES2(vg, oesTexId, w, h,
                                             NVG_IMAGE_NEAREST);
    if (img <= 0) return -1;


    glBindTexture(GL_TEXTURE_EXTERNAL_OES, oesTexId);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, 0);

    return img;
}


void NVG_updateImageFromOES(NVGcontext* vg, int nvgImage, GLuint /*oesTexId*/) {

    (void)vg;
    (void)nvgImage;

}



#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

// ── FBO blit (Option B) ───────────────────────────────────────────────────────

static GLuint s_fboBlitFBO     = 0;
static GLuint s_fboBlitTex     = 0;
static int    s_fboBlitW       = 0;
static int    s_fboBlitH       = 0;
static GLuint s_oesBlitProgram = 0;
static GLuint s_oesBlitVBO     = 0;

static const char* kBlitVS =
        "attribute vec2 aPos;\n"
        "attribute vec2 aUV;\n"
        "varying vec2 vUV;\n"
        "void main() { vUV = aUV; gl_Position = vec4(aPos, 0.0, 1.0); }\n";

static const char* kBlitFS =
        "#extension GL_OES_EGL_image_external : require\n"
        "precision mediump float;\n"
        "uniform samplerExternalOES uTex;\n"
        "varying vec2 vUV;\n"
        "void main() { gl_FragColor = texture2D(uTex, vUV); }\n";

static GLuint _compileShader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    return s;
}

// Call once after GL context is ready, before any video is opened.
void NVG_initOESBlit(int maxW, int maxH) {
    s_fboBlitW = maxW;
    s_fboBlitH = maxH;

    // FBO target texture (regular 2D — NanoVG friendly)
    glGenTextures(1, &s_fboBlitTex);
    glBindTexture(GL_TEXTURE_2D, s_fboBlitTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, maxW, maxH, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // FBO
    glGenFramebuffers(1, &s_fboBlitFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, s_fboBlitFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, s_fboBlitTex, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // Blit shader
    GLuint vs = _compileShader(GL_VERTEX_SHADER,   kBlitVS);
    GLuint fs = _compileShader(GL_FRAGMENT_SHADER, kBlitFS);
    s_oesBlitProgram = glCreateProgram();
    glAttachShader(s_oesBlitProgram, vs);
    glAttachShader(s_oesBlitProgram, fs);
    glBindAttribLocation(s_oesBlitProgram, 0, "aPos");
    glBindAttribLocation(s_oesBlitProgram, 1, "aUV");
    glLinkProgram(s_oesBlitProgram);

    // Full-screen quad
    float quad[] = {
            -1.f, -1.f,  0.f, 0.f,
            1.f, -1.f,  1.f, 0.f,
            -1.f,  1.f,  0.f, 1.f,
            1.f,  1.f,  1.f, 1.f,
    };
    glGenBuffers(1, &s_oesBlitVBO);
    glBindBuffer(GL_ARRAY_BUFFER, s_oesBlitVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}


GLuint NVG_blitOESToTex2D(GLuint oesTexId, int w, int h) {


    GLint prevFBO, prevProgram, prevViewport[4];
    GLint prevArrayBuffer, prevActiveTex, prevTex2D, prevTexOES;

    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);
    glGetIntegerv(GL_CURRENT_PROGRAM, &prevProgram);
    glGetIntegerv(GL_VIEWPORT, prevViewport);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prevArrayBuffer);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &prevActiveTex);

    // Save bound textures
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex2D);
#ifdef GL_TEXTURE_EXTERNAL_OES
    glGetIntegerv(GL_TEXTURE_BINDING_EXTERNAL_OES, &prevTexOES);
#endif


    glBindFramebuffer(GL_FRAMEBUFFER, s_fboBlitFBO);
    glViewport(0, 0, w, h);

    glUseProgram(s_oesBlitProgram);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, oesTexId);

    GLint loc = glGetUniformLocation(s_oesBlitProgram, "uTex");
    if (loc >= 0) glUniform1i(loc, 0);

    glBindBuffer(GL_ARRAY_BUFFER, s_oesBlitVBO);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE,
                          4 * sizeof(float), (void*)0);

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE,
                          4 * sizeof(float), (void*)(2 * sizeof(float)));

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);


    // Restore buffers & program
    glBindBuffer(GL_ARRAY_BUFFER, prevArrayBuffer);
    glUseProgram(prevProgram);

    // Restore framebuffer + viewport
    glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
    glViewport(prevViewport[0], prevViewport[1],
               prevViewport[2], prevViewport[3]);

    // Restore textures
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, prevTex2D);
#ifdef GL_TEXTURE_EXTERNAL_OES
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, prevTexOES);
#endif

    // Restore active texture unit
    glActiveTexture(prevActiveTex);

    return s_fboBlitTex;  // NanoVG-safe texture
}

#endif