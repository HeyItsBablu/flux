// camera_widget_android.cpp
// Android platform implementation for CameraWidget — raw OpenGL ES 2.0.
//
// Preview pipeline: NDK Camera2 → OES texture → blit+rotate → GL_TEXTURE_2D FBO
//                   → Painter::drawCamera (same plain-texture path as
//                   VideoPlayerWidget's Android FBO blit)
// Thumbnail:        last JPEG path → stb_image decode → GL_TEXTURE_2D
//
// Link: android  EGL  GLESv2  camera2ndk  mediandk  stb

#ifdef __ANDROID__

#include "flux/widgets/camera_widget.hpp"
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <android/log.h>

// stb_image is already compiled once (with implementation) in src/stb_impl.cpp
// and linked into every target via the `stb` library — just declare/use it.
#include "stb_image.h"

#define CAMW_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "CameraWidget", __VA_ARGS__)

// ── Permission check (JNI) ────────────────────────────────────────────────────

static bool _hasCameraPermission()
{
    extern JNIEnv *getJNIEnv();
    extern ANativeActivity *s_activity;
    JNIEnv *env = getJNIEnv();
    if (!env || !s_activity)
        return false;

    jobject actObj = s_activity->clazz;
    jclass actCls = env->GetObjectClass(actObj);
    jmethodID check = env->GetMethodID(actCls,
                                       "checkSelfPermission", "(Ljava/lang/String;)I");
    jstring perm = env->NewStringUTF("android.permission.CAMERA");
    jint result = env->CallIntMethod(actObj, check, perm);
    env->DeleteLocalRef(perm);
    return result == 0; // PERMISSION_GRANTED
}

// ── _platformScheduleOpen ─────────────────────────────────────────────────────
// Unchanged — pure permission/timer logic, no rendering involved.

extern void FluxAndroid_requestPermission(const char *permission);

void CameraWidget::_platformScheduleOpen()
{
    if (_permCheckTimer)
        return;

    FluxAndroid_requestPermission("android.permission.CAMERA");

    _permCheckTimer = FluxUI::getCurrentInstance()->setInterval(500, [this]()
                                                                {
        if (_hasCameraPermission()) {
            auto* ui = FluxUI::getCurrentInstance();
            if (ui && _permCheckTimer) {
                ui->clearInterval(_permCheckTimer);
                _permCheckTimer = 0;
            }
            _shouldOpen = true;
            markNeedsPaint();
        } });
}

// ── _platformOnFlip ───────────────────────────────────────────────────────────
// Drop the FBO so the next frame rebuilds it at the new camera's resolution.

void CameraWidget::_platformOnFlip()
{
    if (_android.fbo)
    {
        glDeleteFramebuffers(1, &_android.fbo);
        _android.fbo = 0;
    }
    if (_android.fboTex)
    {
        glDeleteTextures(1, &_android.fboTex);
        _android.fboTex = 0;
    }
    _android.fboW = _android.fboH = 0;
}

// =============================================================================
// GL blit pipeline (OES external texture → rotated GL_TEXTURE_2D)
// Built once, reused for the lifetime of the widget.
// =============================================================================

static const char *kBlitVS = R"(
attribute vec2 aPos;
attribute vec2 aUV;
varying   vec2 vUV;
void main() { vUV = aUV; gl_Position = vec4(aPos, 0.0, 1.0); }
)";

static const char *kBlitFS = R"(
#extension GL_OES_EGL_image_external : require
precision mediump float;
uniform samplerExternalOES uTex;
varying vec2 vUV;
void main() { gl_FragColor = texture2D(uTex, vUV); }
)";

static GLuint _compile(GLenum type, const char *src)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
        char log[512];
        glGetShaderInfoLog(s, 512, nullptr, log);
        CAMW_LOGE("Shader compile error: %s", log);
    }
    return s;
}

void CameraWidget::_initGLBlitPipeline()
{
    if (_android.glResourcesReady)
        return;
    _android.glResourcesReady = true;

    GLuint vs = _compile(GL_VERTEX_SHADER, kBlitVS);
    GLuint fs = _compile(GL_FRAGMENT_SHADER, kBlitFS);
    _android.blitProgram = glCreateProgram();
    glAttachShader(_android.blitProgram, vs);
    glAttachShader(_android.blitProgram, fs);
    glBindAttribLocation(_android.blitProgram, 0, "aPos");
    glBindAttribLocation(_android.blitProgram, 1, "aUV");
    glLinkProgram(_android.blitProgram);
    glDeleteShader(vs);
    glDeleteShader(fs);

    // Full-screen NDC quad with UVs pre-rotated 90° so the FBO — allocated
    // sensorH x sensorW — comes out portrait-correct in one blit, with no
    // rotation step needed at draw time.
    //
    // TUNING NOTE: if the preview comes out upside-down or mirrored on your
    // test devices, swap/flip the UV pairs below. Sensor mounting angle
    // varies slightly by OEM, same as the old `cp.rotationDeg = 90.f` did

    static const float quad[] = {
        // x,    y,     u,    v
        -1.f,
        -1.f,
        0.f,
        0.f,
        1.f,
        -1.f,
        0.f,
        1.f,
        -1.f,
        1.f,
        1.f,
        0.f,
        1.f,
        1.f,
        1.f,
        1.f,
    };
    glGenBuffers(1, &_android.blitVBO);
    glBindBuffer(GL_ARRAY_BUFFER, _android.blitVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void CameraWidget::_rebuildFBO(int sensorW, int sensorH)
{
    if (_android.fbo)
    {
        glDeleteFramebuffers(1, &_android.fbo);
        _android.fbo = 0;
    }
    if (_android.fboTex)
    {
        glDeleteTextures(1, &_android.fboTex);
        _android.fboTex = 0;
    }

    // Rotated 90° — output texture is portrait (sensorH x sensorW)
    _android.fboW = sensorH;
    _android.fboH = sensorW;
    if (_android.fboW <= 0 || _android.fboH <= 0)
        return;

    glGenTextures(1, &_android.fboTex);
    glBindTexture(GL_TEXTURE_2D, _android.fboTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, _android.fboW, _android.fboH, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    glGenFramebuffers(1, &_android.fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, _android.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, _android.fboTex, 0);
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    if (status != GL_FRAMEBUFFER_COMPLETE)
    {
        CAMW_LOGE("Camera FBO incomplete: 0x%x", status);
        glDeleteTextures(1, &_android.fboTex);
        _android.fboTex = 0;
        glDeleteFramebuffers(1, &_android.fbo);
        _android.fbo = 0;
    }
}

void CameraWidget::_blitOESToFBO(GLuint oesTexId)
{
    if (!_android.fbo || !_android.blitProgram)
        return;

    GLint prevFBO = 0, prevProg = 0, prevVP[4] = {};
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);
    glGetIntegerv(GL_CURRENT_PROGRAM, &prevProg);
    glGetIntegerv(GL_VIEWPORT, prevVP);

    glBindFramebuffer(GL_FRAMEBUFFER, _android.fbo);
    glViewport(0, 0, _android.fboW, _android.fboH);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);

    glUseProgram(_android.blitProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, oesTexId);
    glUniform1i(glGetUniformLocation(_android.blitProgram, "uTex"), 0);

    glBindBuffer(GL_ARRAY_BUFFER, _android.blitVBO);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)(2 * sizeof(float)));
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, 0);

    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prevFBO);
    glViewport(prevVP[0], prevVP[1], prevVP[2], prevVP[3]);
    glUseProgram((GLuint)prevProg);
    glEnable(GL_BLEND);
}

// ── _platformRenderPreview ────────────────────────────────────────────────────

bool CameraWidget::_platformRenderPreview(GraphicsContext & /*ctx*/, Painter &p,
                                          FontCache & /*fontCache*/, int viewH)
{
    auto &cam = FluxCamera::get();

    _initGLBlitPipeline();

    int sensorW = cam.getPreviewWidth();
    int sensorH = cam.getPreviewHeight();
    if (sensorW <= 0 || sensorH <= 0)
        return false;

    if (_android.fboW != sensorH || _android.fboH != sensorW)
        _rebuildFBO(sensorW, sensorH);

    if (cam.updateFrame())
        _blitOESToFBO(cam.getTextureId());

    if (!_android.fboTex)
        return false;

    // fboW/fboH are already the rotated (portrait) dimensions, so this
    // aspect ratio is equivalent to the old `sensorH/sensorW` calc.
    float portraitAR = (float)_android.fboW / (float)_android.fboH;
    float widgetAR = (float)width / (float)viewH;

    float drawW, drawH;
    if (portraitAR > widgetAR)
    {
        drawH = (float)viewH;
        drawW = drawH * portraitAR;
    }
    else
    {
        drawW = (float)width;
        drawH = drawW / portraitAR;
    }

    float cx = (float)x + (float)width * 0.5f;
    float cy = (float)y + (float)viewH * 0.5f;
    float dstX = cx - drawW * 0.5f;
    float dstY = cy - drawH * 0.5f;

    p.pushClipRect(x, y, width, viewH);

    Painter::CameraDrawParams cp;
    cp.frame = (NativeImage)_android.fboTex; // already rotated — no rotationDeg needed
    cp.dstX = (int)dstX;
    cp.dstY = (int)dstY;
    cp.dstW = (int)drawW;
    cp.dstH = (int)drawH;
    cp.mirror = cam.isFrontCamera(); // front camera still wants the selfie flip
    p.drawCamera(cp);

    p.popClipRect();
    return true;
}

// ── _platformRenderFlash ──────────────────────────────────────────────────────
// Unchanged — plain fillRect.

void CameraWidget::_platformRenderFlash(GraphicsContext & /*ctx*/, Painter &p,
                                        int viewH)
{
    Color fc = Color::fromRGBA(255, 255, 255, (uint8_t)(_flashAlpha * 255));
    p.fillRect(x, y, width, viewH, fc);
}

// ── _platformRenderThumb ──────────────────────────────────────────────────────

bool CameraWidget::_platformRenderThumb(GraphicsContext &ctx,
                                        int thumbX, int thumbY,
                                        int thumbW, int thumbH)
{
    if (!_android.thumbTex)
        return false;

    Painter p(ctx);
    Painter::ImageDrawParams ip;
    ip.image = (NativeImage)_android.thumbTex;
    ip.srcWidth = _android.thumbW;
    ip.srcHeight = _android.thumbH;
    ip.clipX = thumbX;
    ip.clipY = thumbY;
    ip.clipW = thumbW;
    ip.clipH = thumbH;
    ip.destX = (float)thumbX;
    ip.destY = (float)thumbY;
    ip.destW = (float)thumbW;
    ip.destH = (float)thumbH;
    p.drawImage(ip);
    return true;
}

// ── _platformLoadThumb ────────────────────────────────────────────────────────

void CameraWidget::_platformLoadThumb(const std::string &path)
{
    if (_android.thumbTex)
    {
        glDeleteTextures(1, &_android.thumbTex);
        _android.thumbTex = 0;
    }

    int w = 0, h = 0, channels = 0;
    stbi_uc *pixels = stbi_load(path.c_str(), &w, &h, &channels, 4); // force RGBA
    if (!pixels)
    {
        CAMW_LOGE("Thumbnail decode failed: %s", path.c_str());
        return;
    }

    glGenTextures(1, &_android.thumbTex);
    glBindTexture(GL_TEXTURE_2D, _android.thumbTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    stbi_image_free(pixels);
    _android.thumbW = w;
    _android.thumbH = h;
}

// ── _platformDestroy ──────────────────────────────────────────────────────────

void CameraWidget::_platformDestroy()
{
    if (_android.fbo)
    {
        glDeleteFramebuffers(1, &_android.fbo);
        _android.fbo = 0;
    }
    if (_android.fboTex)
    {
        glDeleteTextures(1, &_android.fboTex);
        _android.fboTex = 0;
    }
    if (_android.thumbTex)
    {
        glDeleteTextures(1, &_android.thumbTex);
        _android.thumbTex = 0;
    }
    if (_android.blitProgram)
    {
        glDeleteProgram(_android.blitProgram);
        _android.blitProgram = 0;
    }
    if (_android.blitVBO)
    {
        glDeleteBuffers(1, &_android.blitVBO);
        _android.blitVBO = 0;
    }
    _android.glResourcesReady = false;
    _android.fboW = _android.fboH = 0;
    _android.thumbW = _android.thumbH = 0;
}

#endif // __ANDROID__