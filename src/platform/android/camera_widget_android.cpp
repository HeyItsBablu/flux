// camera_widget_android.cpp
// Android platform implementation for CameraWidget.
//
// Preview pipeline: NDK Camera2 → OES texture → NVG_blitOESToTex2D → NanoVG
// Thumbnail:        last JPEG path → nvgCreateImage (file-backed NVG image)
//
// Link: android  EGL  GLESv2  camera2ndk  mediandk  nanovg

#ifdef __ANDROID__

#include "flux/widgets/camera_widget.hpp"

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
// Starts a 500 ms permission-check timer.
// Sets _shouldOpen once permission is granted; the render() path picks it up.

extern void FluxAndroid_requestPermission(const char* permission);

void CameraWidget::_platformScheduleOpen()
{
    if (_permCheckTimer)
        return;

    // Request camera permission the first time this widget appears
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
        }
    });
}

// ── _platformOnFlip ───────────────────────────────────────────────────────────
// Reset the NVG image handle so the next frame creates a fresh one for the
// new camera (different sensor format / resolution).

void CameraWidget::_platformOnFlip()
{
    _android.nvgImage = -1;
}

// ── _platformRenderPreview ────────────────────────────────────────────────────

bool CameraWidget::_platformRenderPreview(GraphicsContext & /*ctx*/, Painter &p,
                                          FontCache & /*fontCache*/, int viewH)
{
    auto &cam = FluxCamera::get();
    NVGcontext *vg = FluxAndroid_getVG();

    // Latch new OES frame
    if (cam.updateFrame() && cam.getPreviewWidth() > 0)
    {
        int blitW = cam.getPreviewWidth();
        int blitH = cam.getPreviewHeight();

        if (_android.nvgImage < 0)
        {
            _android.tex2dHandle = NVG_blitOESToTex2D(
                cam.getTextureId(), blitW, blitH);
            _android.nvgImage = nvgCreateImageGLES2(vg, _android.tex2dHandle,
                                                    blitW, blitH, 0);
        }
        else
        {
            NVG_blitOESToTex2D(cam.getTextureId(), blitW, blitH);
            nvgUpdateImage(vg, _android.nvgImage, nullptr);
        }
    }

    if (_android.nvgImage < 0 || cam.getPreviewWidth() <= 0)
        return false;

    int sensorW = cam.getPreviewWidth();
    int sensorH = cam.getPreviewHeight();

    // Camera sensor is landscape; after 90° rotation the effective AR flips.
    float rotatedAR = (float)sensorH / (float)sensorW;
    float widgetAR = (float)width / (float)viewH;

    float drawW, drawH;
    if (rotatedAR > widgetAR)
    {
        drawH = (float)viewH;
        drawW = drawH * rotatedAR;
    }
    else
    {
        drawW = (float)width;
        drawH = drawW / rotatedAR;
    }

    float cx = (float)x + (float)width * 0.5f;
    float cy = (float)y + (float)viewH * 0.5f;
    float patX = cx - drawW * 0.5f;
    float patY = cy - drawH * 0.5f;

    // Clip to view area
    nvgSave(vg);
    nvgScissor(vg, (float)x, (float)y, (float)width, (float)viewH);

    Painter::CameraDrawParams cp;
    cp.frame = (NativeImage)_android.nvgImage;
    cp.srcW = cam.getPreviewWidth();
    cp.srcH = cam.getPreviewHeight();
    cp.dstX = (int)patX;
    cp.dstY = (int)patY;
    cp.dstW = (int)drawW;
    cp.dstH = (int)drawH;
    cp.mirror = false; // Android handles orientation via rotation
    cp.rotationDeg = 90.f;
    cp.rotCenterX = cx;
    cp.rotCenterY = cy;
    p.drawCamera(cp);

    nvgRestore(vg);
    return true;
}

// ── _platformRenderFlash ──────────────────────────────────────────────────────

void CameraWidget::_platformRenderFlash(GraphicsContext & /*ctx*/, Painter &p,
                                        int viewH)
{
    Color fc = Color::fromRGBA(255, 255, 255, (uint8_t)(_flashAlpha * 255));
    p.fillRect(x, y, width, viewH, fc);
}

// ── _platformRenderThumb ──────────────────────────────────────────────────────

bool CameraWidget::_platformRenderThumb(GraphicsContext & /*ctx*/,
                                        int thumbX, int thumbY,
                                        int thumbW, int thumbH)
{
    if (_android.thumbImage < 0)
        return false;
    NVGcontext *vg = FluxAndroid_getVG();
    NVGpaint tp = nvgImagePattern(vg,
                                  (float)thumbX, (float)thumbY,
                                  (float)thumbW, (float)thumbH,
                                  0.f, _android.thumbImage, 1.f);
    nvgBeginPath(vg);
    nvgRect(vg, (float)thumbX, (float)thumbY, (float)thumbW, (float)thumbH);
    nvgFillPaint(vg, tp);
    nvgFill(vg);
    return true;
}

// ── _platformLoadThumb ────────────────────────────────────────────────────────

void CameraWidget::_platformLoadThumb(const std::string &path)
{
    NVGcontext *vg = FluxAndroid_getVG();
    if (!vg)
        return;
    if (_android.thumbImage >= 0)
    {
        nvgDeleteImage(vg, _android.thumbImage);
        _android.thumbImage = -1;
    }
    _android.thumbImage = nvgCreateImage(vg, path.c_str(), 0);
}

// ── _platformDestroy ──────────────────────────────────────────────────────────

void CameraWidget::_platformDestroy()
{
    NVGcontext *vg = FluxAndroid_getVG();
    if (vg)
    {
        if (_android.nvgImage >= 0)
            nvgDeleteImage(vg, _android.nvgImage);
        if (_android.thumbImage >= 0)
            nvgDeleteImage(vg, _android.thumbImage);
    }
    _android.nvgImage = -1;
    _android.thumbImage = -1;
    _android.tex2dHandle = 0;
}

#endif // __ANDROID__