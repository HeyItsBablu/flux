// flux_camera.hpp
// NDK Camera2 photo capture engine for FluxUI on Android.
//
// Pipeline:
//   ACameraManager → ACameraDevice → ACaptureSession
//       ├── Preview  → SurfaceTexture → OES texture → NanoVG viewfinder
//       └── Capture  → AImageReader  → JPEG bytes   → MediaStore (Gallery)
//
// Usage:
//   FluxCamera::get().open();           // open back camera + start preview
//   FluxCamera::get().capturePhoto();   // take a photo
//   FluxCamera::get().flipCamera();     // toggle front/back
//   FluxCamera::get().setFlash(true);   // flash on/off
//   FluxCamera::get().close();          // release all resources
//
// Requires in AndroidManifest.xml:
//   <uses-permission android:name="android.permission.CAMERA"/>
//
// Dependencies (add to CMakeLists target_link_libraries):
//   camera2ndk   mediandk   android   EGL   GLESv2
//
#pragma once
#ifdef __ANDROID__

#include <camera/NdkCameraManager.h>
#include <camera/NdkCameraDevice.h>
#include <camera/NdkCameraCaptureSession.h>
#include <camera/NdkCameraMetadataTags.h>
#include <camera/NdkCaptureRequest.h>
#include <media/NdkImageReader.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <android/log.h>

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#define CAM_LOGI(...) __android_log_print(ANDROID_LOG_INFO,  "FluxCamera", __VA_ARGS__)
#define CAM_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "FluxCamera", __VA_ARGS__)

// ── Externals from native-lib.cpp ─────────────────────────────────────────────
extern EGLDisplay FluxAndroid_getEGLDisplay();
extern EGLContext FluxAndroid_getEGLContext();

// JNI shims (same pattern as FluxVideo)
extern void*          FluxVideo_createSurfaceTexture(GLuint texId);
extern void           FluxVideo_updateTexImage(void* surfaceTexture);
extern ANativeWindow* FluxVideo_getNativeWindow(void* surfaceTexture);
extern void           FluxVideo_destroySurfaceTexture(void* surfaceTexture);

// MediaStore save — implemented in native-lib.cpp
extern std::string FluxAndroid_saveToMediaStore(
        const std::string& filename,
        const std::string& mimeType,
        const std::string& relativePath,
        const uint8_t*     data,
        size_t             dataSize);

// ============================================================================
// FluxCamera
// ============================================================================

class FluxCamera {
public:
    // ── Singleton ─────────────────────────────────────────────────────────────
    static FluxCamera& get() {
        static FluxCamera instance;
        return instance;
    }

    // ── State ─────────────────────────────────────────────────────────────────
    enum class State { Closed, Opening, Previewing, Capturing, Error };

    State  getState()         const { return s_state.load(); }
    bool   isPreviewing()     const { return s_state == State::Previewing; }
    bool   isCapturing()      const { return s_state == State::Capturing; }
    bool   isFrontCamera()    const { return s_useFront.load(); }
    bool   isFlashOn()        const { return s_flashOn.load(); }
    bool   hasNewFrame()      const { return s_newFrame.load(); }
    GLuint getTextureId()     const { return s_oesTexture; }
    int    getPreviewWidth()  const { return s_previewW.load(); }
    int    getPreviewHeight() const { return s_previewH.load(); }

    // Last captured JPEG path in MediaStore
    const std::string& getLastPhotoPath() const { return s_lastPhotoPath; }

    // ── Callbacks ─────────────────────────────────────────────────────────────
    using PhotoCallback = std::function<void(const std::string& mediaStorePath)>;
    void setOnPhoto(PhotoCallback cb) { s_onPhoto = std::move(cb); }

    // ── Flash ─────────────────────────────────────────────────────────────────
    void setFlash(bool on) {
        s_flashOn = on;
        _applyFlash();
    }
    void toggleFlash() { setFlash(!s_flashOn.load()); }

    // =========================================================================
    // OPEN — selects camera, creates OES texture, starts preview
    // Must be called from the GL thread.
    // =========================================================================
    bool open(bool useFront = false) {
        if (s_state != State::Closed) close();

        s_useFront = useFront;
        s_state    = State::Opening;

        s_manager = ACameraManager_create();
        if (!s_manager) {
            CAM_LOGE("ACameraManager_create failed");
            s_state = State::Error;
            return false;
        }

        // Select camera ID
        if (!_selectCamera(useFront)) {
            s_state = State::Error;
            return false;
        }

        // Query max JPEG size
        _queryMaxJpegSize();

        // Create OES preview texture
        if (!_createOESTexture()) {
            s_state = State::Error;
            return false;
        }

        // Create ImageReader for JPEG capture
        if (!_createImageReader()) {
            s_state = State::Error;
            return false;
        }

        // Open camera device
        if (!_openDevice()) {
            s_state = State::Error;
            return false;
        }

        return true;
    }

    // =========================================================================
    // FLIP — switches between front and back camera
    // =========================================================================
    void flipCamera() {
        bool front = !s_useFront.load();
        close();
        open(front);
    }

    // =========================================================================
    // UPDATE FRAME — call once per vsync on the GL thread
    // Latches new preview frame into OES texture.
    // =========================================================================
    bool updateFrame() {
        if (!s_surfaceTexture) { s_newFrame = false; return false; }
        if (!s_pendingFrame.load()) { s_newFrame = false; return false; }

        FluxVideo_updateTexImage(s_surfaceTexture);
        s_pendingFrame = false;
        s_newFrame     = true;
        return true;
    }

    // =========================================================================
    // CAPTURE PHOTO — triggers a still JPEG capture
    // =========================================================================
    void capturePhoto() {
        if (s_state != State::Previewing) return;
        if (!s_captureSession || !s_captureRequest) return;

        s_state = State::Capturing;

        // Apply flash
        uint8_t flashMode = s_flashOn.load()
                            ? ACAMERA_FLASH_MODE_SINGLE
                            : ACAMERA_FLASH_MODE_OFF;
        ACaptureRequest_setEntry_u8(s_captureRequest,
                                    ACAMERA_FLASH_MODE, 1, &flashMode);

        // Trigger AF
        uint8_t afTrigger = ACAMERA_CONTROL_AF_TRIGGER_START;
        ACaptureRequest_setEntry_u8(s_captureRequest,
                                    ACAMERA_CONTROL_AF_TRIGGER, 1, &afTrigger);

        // Capture — use s_captureCallbacks (still capture, not repeating)
        ACameraCaptureSession_capture(
                s_captureSession,
                &s_captureCallbacks,
                1, &s_captureRequest,
                nullptr);

        CAM_LOGI("Capture triggered");
    }

    // =========================================================================
    // CLOSE
    // =========================================================================
    void close() {
        s_state = State::Closed;

        if (s_captureSession) {
            ACameraCaptureSession_stopRepeating(s_captureSession);
            ACameraCaptureSession_close(s_captureSession);
            s_captureSession = nullptr;
        }
        if (s_captureRequest) {
            ACaptureRequest_free(s_captureRequest);
            s_captureRequest = nullptr;
        }
        if (s_previewRequest) {
            ACaptureRequest_free(s_previewRequest);
            s_previewRequest = nullptr;
        }
        if (s_previewOutput) {
            ACameraOutputTarget_free(s_previewOutput);
            s_previewOutput = nullptr;
        }
        if (s_captureOutput) {
            ACameraOutputTarget_free(s_captureOutput);
            s_captureOutput = nullptr;
        }
        if (s_sessionOutputContainer) {
            ACaptureSessionOutputContainer_free(s_sessionOutputContainer);
            s_sessionOutputContainer = nullptr;
        }
        if (s_previewSessionOutput) {
            ACaptureSessionOutput_free(s_previewSessionOutput);
            s_previewSessionOutput = nullptr;
        }
        if (s_captureSessionOutput) {
            ACaptureSessionOutput_free(s_captureSessionOutput);
            s_captureSessionOutput = nullptr;
        }
        if (s_cameraDevice) {
            ACameraDevice_close(s_cameraDevice);
            s_cameraDevice = nullptr;
        }
        if (s_imageReader) {
            AImageReader_delete(s_imageReader);
            s_imageReader = nullptr;
            s_imageReaderWindow = nullptr;  // owned by reader, already freed
        }
        if (s_previewWindow) {
            ANativeWindow_release(s_previewWindow);
            s_previewWindow = nullptr;
        }
        if (s_surfaceTexture) {
            FluxVideo_destroySurfaceTexture(s_surfaceTexture);
            s_surfaceTexture = nullptr;
        }
        if (s_oesTexture) {
            glDeleteTextures(1, &s_oesTexture);
            s_oesTexture = 0;
        }
        if (s_cameraIdList) {
            ACameraManager_deleteCameraIdList(s_cameraIdList);
            s_cameraIdList = nullptr;
        }
        if (s_manager) {
            ACameraManager_delete(s_manager);
            s_manager = nullptr;
        }

        s_selectedCameraId.clear();
        s_pendingFrame = false;
        s_newFrame     = false;
        s_previewW     = 0;
        s_previewH     = 0;

        CAM_LOGI("Camera closed");
    }

private:
    FluxCamera()  = default;
    ~FluxCamera() { close(); }

    // ── NDK Camera objects ────────────────────────────────────────────────────
    ACameraManager*              s_manager              = nullptr;
    ACameraIdList*               s_cameraIdList         = nullptr;
    ACameraDevice*               s_cameraDevice         = nullptr;
    ACameraCaptureSession*       s_captureSession       = nullptr;
    ACaptureRequest*             s_previewRequest       = nullptr;
    ACaptureRequest*             s_captureRequest       = nullptr;
    ACameraOutputTarget*         s_previewOutput        = nullptr;
    ACameraOutputTarget*         s_captureOutput        = nullptr;
    ACaptureSessionOutputContainer* s_sessionOutputContainer = nullptr;
    ACaptureSessionOutput*       s_previewSessionOutput = nullptr;
    ACaptureSessionOutput*       s_captureSessionOutput = nullptr;
    AImageReader*                s_imageReader          = nullptr;
    ANativeWindow*               s_imageReaderWindow    = nullptr;
    ANativeWindow*               s_previewWindow        = nullptr;

    // ── GL / EGL ──────────────────────────────────────────────────────────────
    GLuint  s_oesTexture     = 0;
    void*   s_surfaceTexture = nullptr;

    // ── State ─────────────────────────────────────────────────────────────────
    std::atomic<State> s_state        { State::Closed };
    std::atomic<bool>  s_useFront     { false };
    std::atomic<bool>  s_flashOn      { false };
    std::atomic<bool>  s_pendingFrame { false };
    std::atomic<bool>  s_newFrame     { false };
    std::atomic<int>   s_previewW     { 0 };
    std::atomic<int>   s_previewH     { 0 };
    std::atomic<int>   s_jpegW        { 4000 };
    std::atomic<int>   s_jpegH        { 3000 };

    std::string        s_selectedCameraId;
    std::string        s_lastPhotoPath;
    PhotoCallback      s_onPhoto;

    // ── Callback structs ──────────────────────────────────────────────────────
    ACameraDevice_StateCallbacks                   s_deviceCallbacks   {};
    ACameraCaptureSession_stateCallbacks           s_sessionCallbacks  {};
    ACameraCaptureSession_captureCallbacks         s_repeatCallbacks   {}; // FIX: repeating preview callbacks
    ACameraCaptureSession_captureCallbacks         s_captureCallbacks  {}; // still capture callbacks
    AImageReader_ImageListener                     s_imageListener     {};

    // =========================================================================
    // CAMERA SELECTION
    // =========================================================================

    bool _selectCamera(bool useFront) {
        ACameraManager_getCameraIdList(s_manager, &s_cameraIdList);
        if (!s_cameraIdList || s_cameraIdList->numCameras == 0) {
            CAM_LOGE("No cameras found");
            return false;
        }

        uint8_t wantFacing = useFront
                             ? ACAMERA_LENS_FACING_FRONT
                             : ACAMERA_LENS_FACING_BACK;

        for (int i = 0; i < s_cameraIdList->numCameras; i++) {
            const char* id = s_cameraIdList->cameraIds[i];
            ACameraMetadata* meta = nullptr;
            ACameraManager_getCameraCharacteristics(s_manager, id, &meta);

            ACameraMetadata_const_entry entry{};
            ACameraMetadata_getConstEntry(meta, ACAMERA_LENS_FACING, &entry);
            uint8_t facing = entry.data.u8[0];
            ACameraMetadata_free(meta);

            if (facing == wantFacing) {
                s_selectedCameraId = id;
                CAM_LOGI("Selected camera: %s (facing=%d)", id, facing);
                return true;
            }
        }

        // Fallback: use first camera
        s_selectedCameraId = s_cameraIdList->cameraIds[0];
        CAM_LOGI("Fallback camera: %s", s_selectedCameraId.c_str());
        return true;
    }

    // =========================================================================
    // QUERY MAX JPEG SIZE
    // =========================================================================

    void _queryMaxJpegSize() {
        ACameraMetadata* meta = nullptr;
        ACameraManager_getCameraCharacteristics(
                s_manager, s_selectedCameraId.c_str(), &meta);
        if (!meta) return;

        ACameraMetadata_const_entry entry{};
        if (ACameraMetadata_getConstEntry(meta,
                                          ACAMERA_SCALER_AVAILABLE_STREAM_CONFIGURATIONS, &entry) == ACAMERA_OK) {
            int32_t maxW = 0, maxH = 0;
            for (uint32_t i = 0; i + 3 < entry.count; i += 4) {
                int32_t fmt   = entry.data.i32[i];
                int32_t w     = entry.data.i32[i + 1];
                int32_t h     = entry.data.i32[i + 2];
                int32_t input = entry.data.i32[i + 3];
                // AIMAGE_FORMAT_JPEG = 0x100
                if (fmt == 0x100 && input == 0 && w * h > maxW * maxH) {
                    maxW = w; maxH = h;
                }
            }
            if (maxW > 0) {
                s_jpegW = maxW;
                s_jpegH = maxH;
                CAM_LOGI("Max JPEG size: %dx%d", maxW, maxH);
            }
        }

        // Preview size — pick largest that fits 1920x1080
        if (ACameraMetadata_getConstEntry(meta,
                                          ACAMERA_SCALER_AVAILABLE_STREAM_CONFIGURATIONS, &entry) == ACAMERA_OK) {
            int32_t bestW = 1280, bestH = 720;
            for (uint32_t i = 0; i + 3 < entry.count; i += 4) {
                int32_t fmt   = entry.data.i32[i];
                int32_t w     = entry.data.i32[i + 1];
                int32_t h     = entry.data.i32[i + 2];
                int32_t input = entry.data.i32[i + 3];
                // AIMAGE_FORMAT_PRIVATE = 0x22 (SurfaceTexture)
                if (fmt == 0x22 && input == 0 &&
                    w <= 1920 && h <= 1080 &&
                    w * h > bestW * bestH) {
                    bestW = w; bestH = h;
                }
            }
            s_previewW = bestW;
            s_previewH = bestH;
            CAM_LOGI("Preview size: %dx%d", bestW, bestH);
        }

        ACameraMetadata_free(meta);
    }

    // =========================================================================
    // OES TEXTURE + SURFACE TEXTURE (preview output)
    // =========================================================================

    bool _createOESTexture() {
        glGenTextures(1, &s_oesTexture);
        if (!s_oesTexture) return false;

        glBindTexture(GL_TEXTURE_EXTERNAL_OES, s_oesTexture);
        glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_EXTERNAL_OES, 0);

        s_surfaceTexture = FluxVideo_createSurfaceTexture(s_oesTexture);
        if (!s_surfaceTexture) {
            CAM_LOGE("SurfaceTexture creation failed");
            return false;
        }

        s_previewWindow = FluxVideo_getNativeWindow(s_surfaceTexture);
        if (!s_previewWindow) {
            CAM_LOGE("getNativeWindow failed");
            return false;
        }

        ANativeWindow_setBuffersGeometry(
                s_previewWindow,
                s_previewW.load(), s_previewH.load(),
                0);

        CAM_LOGI("OES texture %u + SurfaceTexture created", s_oesTexture);
        return true;
    }

    // =========================================================================
    // IMAGE READER (JPEG still capture output)
    // =========================================================================

    bool _createImageReader() {
        media_status_t status = AImageReader_new(
                s_jpegW.load(), s_jpegH.load(),
                AIMAGE_FORMAT_JPEG,
                2,
                &s_imageReader);

        if (status != AMEDIA_OK || !s_imageReader) {
            CAM_LOGE("AImageReader_new failed: %d", status);
            return false;
        }

        s_imageListener.context          = this;
        s_imageListener.onImageAvailable = _onImageAvailable;
        AImageReader_setImageListener(s_imageReader, &s_imageListener);

        AImageReader_getWindow(s_imageReader, &s_imageReaderWindow);
        CAM_LOGI("ImageReader created %dx%d JPEG",
                 s_jpegW.load(), s_jpegH.load());
        return true;
    }

    // =========================================================================
    // OPEN DEVICE
    // =========================================================================

    bool _openDevice() {
        s_deviceCallbacks.context        = this;
        s_deviceCallbacks.onDisconnected = _onDeviceDisconnected;
        s_deviceCallbacks.onError        = _onDeviceError;

        camera_status_t status = ACameraManager_openCamera(
                s_manager,
                s_selectedCameraId.c_str(),
                &s_deviceCallbacks,
                &s_cameraDevice);

        if (status != ACAMERA_OK || !s_cameraDevice) {
            CAM_LOGE("ACameraManager_openCamera failed: %d", status);
            return false;
        }

        CAM_LOGI("Camera device opened");
        _createSession();
        return true;
    }

    // =========================================================================
    // CREATE SESSION
    // =========================================================================

    void _createSession() {
        // Build output container
        ACaptureSessionOutputContainer_create(&s_sessionOutputContainer);

        // Preview output
        ACaptureSessionOutput_create(s_previewWindow, &s_previewSessionOutput);
        ACaptureSessionOutputContainer_add(
                s_sessionOutputContainer, s_previewSessionOutput);

        // Capture output
        ACaptureSessionOutput_create(s_imageReaderWindow, &s_captureSessionOutput);
        ACaptureSessionOutputContainer_add(
                s_sessionOutputContainer, s_captureSessionOutput);

        // Session state callbacks
        s_sessionCallbacks.context  = this;
        s_sessionCallbacks.onReady  = _onSessionReady;
        s_sessionCallbacks.onActive = _onSessionActive;
        s_sessionCallbacks.onClosed = _onSessionClosed;

        camera_status_t status = ACameraDevice_createCaptureSession(
                s_cameraDevice,
                s_sessionOutputContainer,
                &s_sessionCallbacks,
                &s_captureSession);

        if (status != ACAMERA_OK) {
            CAM_LOGE("createCaptureSession failed: %d", status);
            s_state = State::Error;
            return;
        }

        // ── Build preview request ─────────────────────────────────────────────
        ACameraDevice_createCaptureRequest(
                s_cameraDevice, TEMPLATE_PREVIEW, &s_previewRequest);
        ACameraOutputTarget_create(s_previewWindow, &s_previewOutput);
        ACaptureRequest_addTarget(s_previewRequest, s_previewOutput);

        uint8_t afMode = ACAMERA_CONTROL_AF_MODE_CONTINUOUS_PICTURE;
        uint8_t aeMode = ACAMERA_CONTROL_AE_MODE_ON;
        ACaptureRequest_setEntry_u8(s_previewRequest,
                                    ACAMERA_CONTROL_AF_MODE, 1, &afMode);
        ACaptureRequest_setEntry_u8(s_previewRequest,
                                    ACAMERA_CONTROL_AE_MODE, 1, &aeMode);

        // ── Build still capture request ───────────────────────────────────────
        ACameraDevice_createCaptureRequest(
                s_cameraDevice, TEMPLATE_STILL_CAPTURE, &s_captureRequest);
        ACameraOutputTarget_create(s_previewWindow,     &s_previewOutput);
        ACameraOutputTarget_create(s_imageReaderWindow, &s_captureOutput);
        ACaptureRequest_addTarget(s_captureRequest, s_previewOutput);
        ACaptureRequest_addTarget(s_captureRequest, s_captureOutput);

        ACaptureRequest_setEntry_u8(s_captureRequest,
                                    ACAMERA_CONTROL_AF_MODE, 1, &afMode);
        ACaptureRequest_setEntry_u8(s_captureRequest,
                                    ACAMERA_CONTROL_AE_MODE, 1, &aeMode);

        // ── FIX: Repeating preview callbacks — fires every preview frame ──────
        // s_repeatCallbacks drives s_pendingFrame for the GL thread.
        // s_captureCallbacks is used only for one-shot still captures.
        s_repeatCallbacks.context            = this;
        s_repeatCallbacks.onCaptureCompleted = _onPreviewFrameCompleted;
        s_repeatCallbacks.onCaptureFailed    = nullptr;

        // ── Still capture callbacks ───────────────────────────────────────────
        s_captureCallbacks.context            = this;
        s_captureCallbacks.onCaptureCompleted = _onCaptureCompleted;
        s_captureCallbacks.onCaptureFailed    = _onCaptureFailed;

        // ── FIX: Pass &s_repeatCallbacks so every preview frame sets pendingFrame
        ACameraCaptureSession_setRepeatingRequest(
                s_captureSession,
                &s_repeatCallbacks,   // was nullptr — preview frames were never signalled
                1, &s_previewRequest,
                nullptr);

        s_state = State::Previewing;
        CAM_LOGI("Preview started");
    }

    // =========================================================================
    // FLASH
    // =========================================================================

    void _applyFlash() {
        if (!s_previewRequest || s_state != State::Previewing) return;
        uint8_t torchMode = s_flashOn.load()
                            ? ACAMERA_FLASH_MODE_TORCH
                            : ACAMERA_FLASH_MODE_OFF;
        ACaptureRequest_setEntry_u8(s_previewRequest,
                                    ACAMERA_FLASH_MODE, 1, &torchMode);
        if (s_captureSession)
            // FIX: keep using &s_repeatCallbacks here too
            ACameraCaptureSession_setRepeatingRequest(
                    s_captureSession,
                    &s_repeatCallbacks,
                    1, &s_previewRequest,
                    nullptr);
    }

    // =========================================================================
    // IMAGE AVAILABLE — called when JPEG is ready
    // =========================================================================

    static void _onImageAvailable(void* ctx, AImageReader* reader) {
        auto* self = static_cast<FluxCamera*>(ctx);

        AImage* image = nullptr;
        if (AImageReader_acquireLatestImage(reader, &image) != AMEDIA_OK || !image)
            return;

        uint8_t* data   = nullptr;
        int      length = 0;
        AImage_getPlaneData(image, 0, &data, &length);

        if (data && length > 0) {
            std::time_t t  = std::time(nullptr);
            std::tm*    tm = std::localtime(&t);
            char buf[64];
            std::strftime(buf, sizeof(buf), "IMG_%Y%m%d_%H%M%S.jpg", tm);

            std::string path = FluxAndroid_saveToMediaStore(
                    buf,
                    "image/jpeg",
                    "Pictures/FluxCam/",
                    data, (size_t)length);

            if (!path.empty()) {
                self->s_lastPhotoPath = path;
                CAM_LOGI("Photo saved: %s (%d bytes)", buf, length);
                if (self->s_onPhoto) self->s_onPhoto(path); 
            }
        }

        AImage_delete(image);
        self->s_state = State::Previewing;
    }

    // =========================================================================
    // NDK CALLBACKS
    // =========================================================================

    static void _onDeviceDisconnected(void* ctx, ACameraDevice* /*dev*/) {
        CAM_LOGI("Camera disconnected");
        static_cast<FluxCamera*>(ctx)->s_state = State::Closed;
    }

    static void _onDeviceError(void* ctx, ACameraDevice* /*dev*/, int err) {
        CAM_LOGE("Camera device error: %d", err);
        static_cast<FluxCamera*>(ctx)->s_state = State::Error;
    }

    static void _onSessionReady(void* ctx, ACameraCaptureSession* /*session*/) {
        CAM_LOGI("Session ready");
    }

    static void _onSessionActive(void* ctx, ACameraCaptureSession* /*session*/) {
        CAM_LOGI("Session active");
        // Prime the pump — first frame signal so GL thread doesn't stall
        static_cast<FluxCamera*>(ctx)->s_pendingFrame = true;
    }

    static void _onSessionClosed(void* ctx, ACameraCaptureSession* /*session*/) {
        CAM_LOGI("Session closed");
    }

    // FIX: New callback — fires once per repeating preview frame.
    // This is the correct place to signal s_pendingFrame for live preview.
    static void _onPreviewFrameCompleted(void* ctx,
                                         ACameraCaptureSession* /*session*/,
                                         ACaptureRequest* /*request*/,
                                         const ACameraMetadata* /*result*/) {
        static_cast<FluxCamera*>(ctx)->s_pendingFrame = true;
    }

    // Fires only for one-shot still captures triggered by capturePhoto()
    static void _onCaptureCompleted(void* ctx,
                                    ACameraCaptureSession* /*session*/,
                                    ACaptureRequest* /*request*/,
                                    const ACameraMetadata* /*result*/) {
        CAM_LOGI("Still capture completed");
        // Still capture done — pendingFrame will resume via repeating callback
    }

    static void _onCaptureFailed(void* ctx,
                                 ACameraCaptureSession* /*session*/,
                                 ACaptureRequest* /*request*/,
                                 ACameraCaptureFailure* failure) {
        CAM_LOGE("Capture failed: reason=%d", failure ? failure->reason : -1);
        static_cast<FluxCamera*>(ctx)->s_state = State::Previewing;
    }
};

#endif // __ANDROID__