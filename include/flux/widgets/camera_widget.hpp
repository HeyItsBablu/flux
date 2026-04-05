// camera_widget.hpp
// Fixed-size camera viewfinder widget for FluxUI on Android.
//
// Controls:
//   - Shutter button (center bottom) — tap to capture photo
//   - Flash toggle  (left bottom)    — on/off
//   - Flip button   (right bottom)   — front/back camera
//   - Flash overlay — brief white flash on capture
//   - Thumbnail     — last photo shown in bottom-left corner
//
// Usage:
//   CameraView()
//       ->setWidth(380)->setHeight(270)
//       ->setOnPhoto([](const std::string& path) { ... })
//
#pragma once
#ifdef __ANDROID__

// Declare NanoVG GLES2 handle import
#ifndef NANOVG_GLES2
#define NANOVG_GLES2
#endif
#include "nanovg_gl.h"

// Thin wrapper — creates NVG image from an existing GL_TEXTURE_2D handle
static inline int nvgCreateImageGLES2(NVGcontext* vg, GLuint texId,
                                      int w, int h, int flags) {
    return nvglCreateImageFromHandleGLES2(vg, texId, w, h, flags);
}
#include "flux/flux.hpp"
#include "flux/flux_camera.hpp"

extern int  NVG_createImageFromOES(NVGcontext* vg, GLuint oesTexId, int w, int h); 
extern void NVG_updateImageFromOES(NVGcontext* vg, int nvgImage, GLuint oesTexId);
extern GLuint NVG_blitOESToTex2D(GLuint oesTexId, int w, int h);
extern NVGcontext* FluxAndroid_getVG();

// ============================================================================
// CameraWidget
// ============================================================================

class CameraWidget : public Widget {
public:
    // ── Config ────────────────────────────────────────────────────────────────
    int  barHeight   = 56;
    bool startFront  = false;

    // ── Colors ────────────────────────────────────────────────────────────────
    Color colBar          = Color::fromRGBA( 15,  15,  15, 210);
    Color colShutter      = Color::fromRGB(255, 255, 255);
    Color colShutterRing  = Color::fromRGBA(255, 255, 255, 180);
    Color colShutterHov   = Color::fromRGBA(220, 220, 220, 255);
    Color colIcon         = Color::fromRGB(220, 220, 220);
    Color colIconHov      = Color::fromRGB(255, 255, 255);
    Color colIconActive   = Color::fromRGB(255, 210,  60);  // flash on = yellow
    Color colFlash        = Color::fromRGBA(255, 255, 255, 200);
    Color colPlaceholder  = Color::fromRGB( 20,  20,  20);
    Color colThumbBorder  = Color::fromRGB(255, 255, 255);

    // ── Fluent setters ────────────────────────────────────────────────────────
    std::shared_ptr<CameraWidget> setWidth(int w) {
        Widget::width = w; autoWidth = false; return self();
    }
    std::shared_ptr<CameraWidget> setHeight(int h) {
        Widget::height = h; autoHeight = false; return self();
    }
    std::shared_ptr<CameraWidget> setOnPhoto(
            std::function<void(const std::string&)> cb) {
        _onPhoto = std::move(cb); return self();
    }
    std::shared_ptr<CameraWidget> setStartFront(bool f) {
        startFront = f; return self();
    }

    // ── Constructor / destructor ───────────────────────────────────────────────
    CameraWidget() {
        autoWidth  = false;
        autoHeight = false;
        width  = 380;
        height = 270;

        FluxCamera::get().setOnPhoto([this](const std::string& path) {
            _lastPhotoPath = path;
            _thumbDirty    = true;
            markNeedsPaint();
            if (_onPhoto) _onPhoto(path);
        });
    }

    ~CameraWidget() {
        _stopTimer();
        FluxCamera::get().close();
        NVGcontext* vg = FluxAndroid_getVG();
        if (vg) {
            if (_nvgImage  >= 0) nvgDeleteImage(vg, _nvgImage);
            if (_thumbImage >= 0) nvgDeleteImage(vg, _thumbImage);
        }
    }

    // =========================================================================
    // Layout
    // =========================================================================

    void computeLayout(GraphicsContext& /*ctx*/,
                       const BoxConstraints& constraints,
                       FontCache& /*fontCache*/) override {
        if (autoWidth)  width  = constraints.maxWidth;
        if (autoHeight) height = constraints.maxHeight;
        applyConstraints();
        needsLayout = false;

        if (!_opened) {
            _opened = true;
            // Don't open camera here — wait for permission via tryOpen()
            _startPermissionCheckTimer();
        }
    }

    // =========================================================================
    // Render
    // =========================================================================

    void render(GraphicsContext& ctx, FontCache& fontCache) override {

        if (_shouldOpen) {
            _shouldOpen = false;
            FluxCamera::get().open(startFront);
            _startTimer();
        }
        auto& cam = FluxCamera::get();
        NVGcontext* vg = FluxAndroid_getVG();
        Painter p(ctx);

        int viewH = height - barHeight;

// ── Latch new preview frame ───────────────────────────────────────────────
        if (cam.updateFrame() && cam.getPreviewWidth() > 0) {
            int blitW = cam.getPreviewWidth();
            int blitH = cam.getPreviewHeight();

            if (_nvgImage < 0) {
                _tex2dHandle = NVG_blitOESToTex2D(cam.getTextureId(), blitW, blitH);
                _nvgImage = nvgCreateImageGLES2(vg, _tex2dHandle, blitW, blitH, 0);
            } else {
                NVG_blitOESToTex2D(cam.getTextureId(), blitW, blitH);
                nvgUpdateImage(vg, _nvgImage, nullptr);
            }
        }


        if (_nvgImage >= 0 && cam.getPreviewWidth() > 0) {
            int sensorW = cam.getPreviewWidth();
            int sensorH = cam.getPreviewHeight();

            // After 90° rotation: portrait AR = sensorH/sensorW
            float rotatedAR = (float)sensorH / (float)sensorW;
            float widgetAR  = (float)width   / (float)viewH;

            float drawW, drawH;
            if (rotatedAR > widgetAR) {
                drawH = (float)viewH;
                drawW = drawH * rotatedAR;
            } else {
                drawW = (float)width;
                drawH = drawW / rotatedAR;
            }

            float cx = (float)x + (float)width  * 0.5f;
            float cy = (float)y + (float)viewH  * 0.5f;

            nvgSave(vg);
            nvgScissor(vg, x, y, width, viewH);

            nvgSave(vg);
            nvgTranslate(vg, cx, cy);
            nvgRotate(vg, NVG_PI * 0.5f);
            nvgTranslate(vg, -cx, -cy);


            float patX = cx - drawW * 0.5f;
            float patY = cy - drawH * 0.5f;

            NVGpaint imgPaint = nvgImagePattern(
                    vg, patX, patY, drawW, drawH, 0.f, _nvgImage, 1.f);

            nvgBeginPath(vg);
            nvgRect(vg, patX, patY, drawW, drawH);
            nvgFillPaint(vg, imgPaint);
            nvgFill(vg);

            nvgRestore(vg);
            nvgRestore(vg);

        } else {
            // Placeholder
            p.fillRect(x, y, width, viewH, colPlaceholder);
            NativeFont tf = fontCache.getFont("Roboto", 13, FontWeight::Normal);
            p.drawText(toWideString("Opening camera..."),
                       x, y, width, viewH, tf,
                       Color::fromRGB(120, 120, 120),
                       DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }

        // ── Flash overlay (white flash on capture) ────────────────────────────
        if (_flashAlpha > 0.f) {
            Color fc = Color::fromRGBA(255, 255, 255,
                                       (uint8_t)(_flashAlpha * 255));
            p.fillRect(x, y, width, viewH, fc);
            _flashAlpha -= 0.15f;
            if (_flashAlpha < 0.f) _flashAlpha = 0.f;
            markNeedsPaint();
        }

        // ── Thumbnail (bottom-left corner) ────────────────────────────────────
        if (!_lastPhotoPath.empty()) {
            int thumbW = 44, thumbH = 44;
            int thumbX = x + 8;
            int thumbY = y + viewH - thumbH - 8;

            // Border
            p.fillRect(thumbX - 2, thumbY - 2,
                       thumbW + 4, thumbH + 4, colThumbBorder);

            if (_thumbImage >= 0) {
                NVGpaint tp = nvgImagePattern(vg,
                                              (float)thumbX, (float)thumbY,
                                              (float)thumbW, (float)thumbH,
                                              0.f, _thumbImage, 1.f);
                nvgBeginPath(vg);
                nvgRect(vg, (float)thumbX, (float)thumbY,
                        (float)thumbW, (float)thumbH);
                nvgFillPaint(vg, tp);
                nvgFill(vg);
            } else {
                p.fillRect(thumbX, thumbY, thumbW, thumbH,
                           Color::fromRGB(60, 60, 60));
            }
        }

        // ── Control bar ───────────────────────────────────────────────────────
        int barY = y + viewH;
        p.fillRect(x, barY, width, barHeight, colBar);
        int midY = barY + barHeight / 2;

        // ── Flash button (left) ───────────────────────────────────────────────
        int iconR  = 16;
        int flashCx = x + 36;
        _flashBtnRect = {flashCx - iconR, barY + (barHeight - iconR*2)/2,
                         iconR*2, iconR*2};
        bool flashOn = cam.isFlashOn();
        Color flashCol = _hovFlash
                         ? colIconHov
                         : (flashOn ? colIconActive : colIcon);

        // Lightning bolt icon (simplified)
        _drawFlashIcon(p, flashCx, midY, 12, flashCol);

        // ── Shutter button (center) ───────────────────────────────────────────
        int shutterR = 22;
        int shutterCx = x + width / 2;
        _shutterRect = {shutterCx - shutterR, barY + (barHeight - shutterR*2)/2,
                        shutterR*2, shutterR*2};

        // Outer ring
        p.drawEllipse(shutterCx - shutterR - 3,
                      midY - shutterR - 3,
                      (shutterR + 3)*2, (shutterR + 3)*2,
                      Color::fromRGBA(0,0,0,0),
                      colShutterRing, 2);
        // Inner fill
        Color sc = cam.isCapturing()
                   ? Color::fromRGB(200, 200, 200)
                   : (_hovShutter ? colShutterHov : colShutter);
        p.drawEllipse(shutterCx - shutterR, midY - shutterR,
                      shutterR*2, shutterR*2, sc, sc, 0);

        // ── Flip button (right) ───────────────────────────────────────────────
        int flipCx = x + width - 36;
        _flipBtnRect = {flipCx - iconR, barY + (barHeight - iconR*2)/2,
                        iconR*2, iconR*2};
        Color flipCol = _hovFlip ? colIconHov : colIcon;
        _drawFlipIcon(p, flipCx, midY, 12, flipCol);

        needsPaint = false;
    }

    // =========================================================================
    // Mouse events
    // =========================================================================

    bool handleMouseDown(int mx, int my) override {
        if (!_inWidget(mx, my)) return false;

        if (_inRect(mx, my, _shutterRect)) {
            _triggerCapture();
            return true;
        }
        if (_inRect(mx, my, _flashBtnRect)) {
            FluxCamera::get().toggleFlash();
            markNeedsPaint();
            return true;
        }
        if (_inRect(mx, my, _flipBtnRect)) {
            _nvgImage = -1;   // reset preview image — new camera different format
            FluxCamera::get().flipCamera();
            _startTimer();
            markNeedsPaint();
            return true;
        }
        return false;
    }

    bool handleMouseMove(int mx, int my) override {
        bool hs = _inRect(mx, my, _shutterRect);
        bool hf = _inRect(mx, my, _flashBtnRect);
        bool hfl = _inRect(mx, my, _flipBtnRect);
        if (hs != _hovShutter || hf != _hovFlash || hfl != _hovFlip) {
            _hovShutter = hs; _hovFlash = hf; _hovFlip = hfl;
            markNeedsPaint();
        }
        return false;
    }

    bool handleMouseLeave() override {
        _hovShutter = _hovFlash = _hovFlip = false;
        markNeedsPaint();
        return true;
    }

private:
    // ── State ─────────────────────────────────────────────────────────────────
    bool        _opened        = false;
    int         _nvgImage      = -1;
    int         _thumbImage    = -1;
    bool        _thumbDirty    = false;
    std::string _lastPhotoPath;
    float       _flashAlpha    = 0.f;
    bool        _hovShutter    = false;
    bool        _hovFlash      = false;
    bool        _hovFlip       = false;
    GLuint _tex2dHandle = 0;

    std::function<void(const std::string&)> _onPhoto;

    // ── Hit rects ─────────────────────────────────────────────────────────────
    struct Rect { int x, y, w, h; };
    Rect _shutterRect  {};
    Rect _flashBtnRect {};
    Rect _flipBtnRect  {};

    // ── Timer ─────────────────────────────────────────────────────────────────
    TimerID _frameTimer = 0;
    TimerID _permCheckTimer = 0;

    bool _shouldOpen = false;

    void _startPermissionCheckTimer() {
        if (_permCheckTimer) return;
        _permCheckTimer = FluxUI::getCurrentInstance()->setInterval(500, [this]() {
            if (_hasCameraPermission()) {
                // Cancel timer first, then set flag — open happens next render
                auto* ui = FluxUI::getCurrentInstance();
                if (ui && _permCheckTimer) {
                    ui->clearInterval(_permCheckTimer);
                    _permCheckTimer = 0;
                }
                _shouldOpen = true;   // ← flag, don't open here
            }
        });
    }

    static bool _hasCameraPermission() {
        extern JNIEnv* getJNIEnv();
        extern ANativeActivity* s_activity;
        JNIEnv* env = getJNIEnv();
        if (!env || !s_activity) return false;

        jobject   actObj  = s_activity->clazz;
        jclass    actCls  = env->GetObjectClass(actObj);
        jmethodID check   = env->GetMethodID(actCls,
                                             "checkSelfPermission", "(Ljava/lang/String;)I");
        jstring   perm    = env->NewStringUTF("android.permission.CAMERA");
        jint      result  = env->CallIntMethod(actObj, check, perm);
        env->DeleteLocalRef(perm);
        return result == 0;  // PERMISSION_GRANTED
    }

    void _startTimer() {
        if (_frameTimer) return;
        _frameTimer = FluxUI::getCurrentInstance()->setInterval(33, [this]() {
            if (FluxCamera::get().isPreviewing() ||
                FluxCamera::get().isCapturing())
                markNeedsPaint();
        });
    }

    void _stopTimer() {
        auto* ui = FluxUI::getCurrentInstance();
        if (!ui) return;
        if (_frameTimer)    { ui->clearInterval(_frameTimer);    _frameTimer    = 0; }
        if (_permCheckTimer){ ui->clearInterval(_permCheckTimer); _permCheckTimer = 0; }
    }

    // ── Capture ───────────────────────────────────────────────────────────────
    void _triggerCapture() {
        if (!FluxCamera::get().isPreviewing()) return;
        _flashAlpha = 1.f;   // start flash overlay
        FluxCamera::get().capturePhoto();
        markNeedsPaint();
    }

    // ── Icon drawing ──────────────────────────────────────────────────────────
    static void _drawFlashIcon(Painter& p, int cx, int cy, int sz, Color col) {
        // Simple lightning bolt: two triangles
        int x1 = cx - sz/2,  y1 = cy - sz;
        int x2 = cx + sz/4,  y2 = cy - 2;
        int x3 = cx - sz/4,  y3 = cy + 2;
        int x4 = cx + sz/2,  y4 = cy + sz;
        // Vertical bar approximation using fillRect strips
        p.fillRect(cx - 2, cy - sz,     4, sz*2, col);
        p.fillRect(cx - sz/2, cy - 2,   sz, 4,   col);
    }

    static void _drawFlipIcon(Painter& p, int cx, int cy, int sz, Color col) {
        // Two curved arrows — approximated as arcs with rects
        p.fillRect(cx - sz, cy - 2, sz*2, 4, col);
        p.fillRect(cx - sz, cy - sz/2, 4, sz, col);
        p.fillRect(cx + sz - 4, cy - sz/2, 4, sz, col);
    }

    // ── Helpers ───────────────────────────────────────────────────────────────
    std::shared_ptr<CameraWidget> self() {
        return std::static_pointer_cast<CameraWidget>(shared_from_this());
    }
    bool _inWidget(int mx, int my) const {
        return mx >= x && mx < x+width && my >= y && my < y+height;
    }
    static bool _inRect(int mx, int my, const Rect& r) {
        return mx >= r.x && mx < r.x+r.w && my >= r.y && my < r.y+r.h;
    }
};

using CameraWidgetPtr = std::shared_ptr<CameraWidget>;

// ── Factory ───────────────────────────────────────────────────────────────────
inline CameraWidgetPtr CameraView() {
    return std::make_shared<CameraWidget>();
}

#endif // __ANDROID__