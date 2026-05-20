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

    static void _onSessionReady(void* /*ctx*/, ACameraCaptureSession* /*session*/) {
        CAM_LOGI("Session ready");
    }

    static void _onSessionActive(void* ctx, ACameraCaptureSession* /*session*/) {
        CAM_LOGI("Session active");
        // Prime the pump — first frame signal so GL thread doesn't stall
        static_cast<FluxCamera*>(ctx)->s_pendingFrame = true;
    }

    static void _onSessionClosed(void* /*ctx*/, ACameraCaptureSession* /*session*/) {
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
    static void _onCaptureCompleted(void* /*ctx*/,
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


// flux_camera_win.hpp
// Windows Media Foundation camera capture engine for FluxUI on Windows.
//
// Pipeline:
//   IMFMediaSource → IMFSourceReader (preview frames as BGRA32)
//       └── Preview  → CPU copy → lockFrame() → GDI StretchDIBits
//       └── Capture  → IMFSample → WIC JPEG encode → save to Pictures folder
//
// Usage:
//   FluxCamera::get().open();           // open back (default) camera + start preview
//   FluxCamera::get().capturePhoto();   // take a photo
//   FluxCamera::get().flipCamera();     // toggle front/back (index 0/1)
//   FluxCamera::get().setFlash(true);   // flash on/off (torch if supported)
//   FluxCamera::get().close();          // release all resources
//

#pragma once
#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <mftransform.h>
#include <wmcodecdsp.h>
#include <combaseapi.h>
#include <wincodec.h>      
#include <shlobj.h>        
#include <wrl/client.h>    

#include <atomic>
#include <chrono>
#include <ctime>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using Microsoft::WRL::ComPtr;

#define CAM_LOGI(...) do { char _buf[512]; snprintf(_buf,sizeof(_buf),__VA_ARGS__); \
    OutputDebugStringA("[FluxCamera] "); OutputDebugStringA(_buf); OutputDebugStringA("\n"); } while(0)
#define CAM_LOGE(...) do { char _buf[512]; snprintf(_buf,sizeof(_buf),__VA_ARGS__); \
    OutputDebugStringA("[FluxCamera][ERR] "); OutputDebugStringA(_buf); OutputDebugStringA("\n"); } while(0)

// Trigger a repaint from within FluxCamera callbacks (e.g. after capture).
// Implement in your FluxUI Windows host (or leave as a no-op stub).
extern void FluxWin_markNeedsPaint();

// Save raw JPEG bytes to the user's Pictures folder, return the full path.
// Provided below as a default implementation — override if needed.
static std::string FluxWin_saveJpegToPictures(const std::string& filename,
                                               const uint8_t* data,
                                               size_t length);

// ============================================================================
// FluxCamera  (Windows implementation)
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

    State  getState()         const { return _state.load(); }
    bool   isPreviewing()     const { return _state == State::Previewing; }
    bool   isCapturing()      const { return _state == State::Capturing;  }
    bool   isFlashOn()        const { return _flashOn.load(); }
    bool   hasNewFrame()      const { return _newFrame.load(); }
    int    getPreviewWidth()  const { return _previewW.load(); }
    int    getPreviewHeight() const { return _previewH.load(); }
    bool   isFrontCamera()    const { return _cameraIndex.load() > 0; }

    const std::string& getLastPhotoPath() const { return _lastPhotoPath; }

    // ── Frame access — call after updateFrame() returns true ──────────────────
    // Holds _frameMutex for its lifetime; copy pixels and release promptly.
    struct FrameLock {
        std::unique_lock<std::mutex> lock;
        const uint8_t* data   = nullptr;
        int            width  = 0;
        int            height = 0;
        int            stride = 0;   // bytes per row (width * 4 for BGRA32)
    };

    FrameLock lockFrame() {
        FrameLock fl;
        fl.lock   = std::unique_lock<std::mutex>(_frameMutex);
        fl.data   = _frameData.empty() ? nullptr : _frameData.data();
        fl.width  = _previewW.load();
        fl.height = _previewH.load();
        fl.stride = fl.width * 4;
        return fl;
    }

    // ── Callbacks ─────────────────────────────────────────────────────────────
    using PhotoCallback = std::function<void(const std::string& path)>;
    void setOnPhoto(PhotoCallback cb) { _onPhoto = std::move(cb); }

    // ── Flash ─────────────────────────────────────────────────────────────────
    void setFlash(bool on)   { _flashOn = on; }
    void toggleFlash()       { setFlash(!_flashOn.load()); }

    // =========================================================================
    // OPEN — enumerate cameras, start preview thread
    // =========================================================================
    bool open(bool useFront = false) {
        if (_state != State::Closed) close();

        _state = State::Opening;

        HRESULT hr = MFStartup(MF_VERSION);
        if (FAILED(hr)) {
            CAM_LOGE("MFStartup failed: 0x%08X", hr);
            _state = State::Error;
            return false;
        }
        _mfStarted = true;

        if (!_enumerateDevices()) {
            _state = State::Error;
            return false;
        }

        int idx = 0;
        if (useFront && (int)_deviceNames.size() > 1) idx = 1;
        _cameraIndex = idx;

        if (!_openDevice(idx)) {
            _state = State::Error;
            return false;
        }

        // Start background reader thread
        _stopThread = false;
        _readerThread = std::thread(&FluxCamera::_readerLoop, this);

        _state = State::Previewing;
        CAM_LOGI("Preview started (camera %d)", idx);
        return true;
    }

    // =========================================================================
    // FLIP — toggle between camera 0 and camera 1
    // =========================================================================
    void flipCamera() {
        int next = (_cameraIndex.load() == 0 && (int)_deviceNames.size() > 1) ? 1 : 0;
        close();
        open(next != 0);
    }

    // =========================================================================
    // UPDATE FRAME — call once per render tick.
    // Returns true if a new frame is available; call lockFrame() to read pixels.
    // =========================================================================
    bool updateFrame() {
        if (!_pendingFrame.load()) { _newFrame = false; return false; }
        _pendingFrame = false;
        _newFrame     = true;
        return true;
    }

    // =========================================================================
    // CAPTURE PHOTO
    // =========================================================================
    void capturePhoto() {
        if (_state != State::Previewing) return;
        _state = State::Capturing;
        _captureRequested = true;
        CAM_LOGI("Capture requested");
    }

    // =========================================================================
    // CLOSE
    // =========================================================================
    void close() {
        _state = State::Closed;

        _stopThread = true;
        if (_readerThread.joinable()) _readerThread.join();

        _reader.Reset();
        _mediaSource.Reset();

        if (_mfStarted) {
            MFShutdown();
            _mfStarted = false;
        }

        _deviceNames.clear();
        _deviceSymLinks.clear();
        _pendingFrame     = false;
        _newFrame         = false;
        _captureRequested = false;
        _previewW         = 0;
        _previewH         = 0;
        _frameData.clear();

        CAM_LOGI("Camera closed");
    }

private:
    FluxCamera()  = default;
    ~FluxCamera() { close(); }

    // ── MF objects ────────────────────────────────────────────────────────────
    ComPtr<IMFMediaSource>  _mediaSource;
    ComPtr<IMFSourceReader> _reader;
    bool                    _mfStarted = false;

    // ── State ─────────────────────────────────────────────────────────────────
    std::atomic<State> _state            { State::Closed };
    std::atomic<int>   _cameraIndex      { 0 };
    std::atomic<bool>  _flashOn          { false };
    std::atomic<bool>  _pendingFrame     { false };
    std::atomic<bool>  _newFrame         { false };
    std::atomic<bool>  _captureRequested { false };
    std::atomic<bool>  _stopThread       { false };
    std::atomic<int>   _previewW         { 0 };
    std::atomic<int>   _previewH         { 0 };

    std::string   _lastPhotoPath;
    PhotoCallback _onPhoto;

    // ── Frame buffer (writer: reader thread  /  reader: UI thread via lockFrame)
    std::mutex           _frameMutex;
    std::vector<uint8_t> _frameData;   // BGRA32, _previewW * _previewH * 4

    // ── Reader thread ─────────────────────────────────────────────────────────
    std::thread _readerThread;

    // ── Device list ───────────────────────────────────────────────────────────
    std::vector<std::wstring> _deviceNames;
    std::vector<std::wstring> _deviceSymLinks;

    // =========================================================================
    // ENUMERATE VIDEO CAPTURE DEVICES
    // =========================================================================
    bool _enumerateDevices() {
        _deviceNames.clear();
        _deviceSymLinks.clear();

        ComPtr<IMFAttributes> attrs;
        HRESULT hr = MFCreateAttributes(&attrs, 1);
        if (FAILED(hr)) return false;

        attrs->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                       MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);

        IMFActivate** ppDevices = nullptr;
        UINT32 count = 0;
        hr = MFEnumDeviceSources(attrs.Get(), &ppDevices, &count);
        if (FAILED(hr) || count == 0) {
            CAM_LOGE("No video capture devices found");
            return false;
        }

        for (UINT32 i = 0; i < count; i++) {
            WCHAR* name    = nullptr;
            WCHAR* symlink = nullptr;
            UINT32 nameLen = 0, symLen = 0;

            ppDevices[i]->GetAllocatedString(
                    MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &name, &nameLen);
            ppDevices[i]->GetAllocatedString(
                    MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK,
                    &symlink, &symLen);

            if (name)    _deviceNames.push_back(name);
            if (symlink) _deviceSymLinks.push_back(symlink);

            CoTaskMemFree(name);
            CoTaskMemFree(symlink);
            ppDevices[i]->Release();
        }
        CoTaskMemFree(ppDevices);

        CAM_LOGI("Found %u camera(s)", count);
        return true;
    }

    // =========================================================================
    // OPEN DEVICE + CREATE SOURCE READER
    // =========================================================================
    bool _openDevice(int index) {
        if (index < 0 || index >= (int)_deviceNames.size()) index = 0;

        ComPtr<IMFAttributes> attrs;
        MFCreateAttributes(&attrs, 1);
        attrs->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                       MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);

        IMFActivate** ppDevices = nullptr;
        UINT32 count = 0;
        HRESULT hr = MFEnumDeviceSources(attrs.Get(), &ppDevices, &count);
        if (FAILED(hr) || (int)count <= index) {
            CAM_LOGE("Device %d not available", index);
            return false;
        }

        hr = ppDevices[index]->ActivateObject(IID_PPV_ARGS(&_mediaSource));
        for (UINT32 i = 0; i < count; i++) ppDevices[i]->Release();
        CoTaskMemFree(ppDevices);

        if (FAILED(hr) || !_mediaSource) {
            CAM_LOGE("ActivateObject failed: 0x%08X", hr);
            return false;
        }

        ComPtr<IMFAttributes> readerAttrs;
        MFCreateAttributes(&readerAttrs, 1);
        readerAttrs->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);

        hr = MFCreateSourceReaderFromMediaSource(
                _mediaSource.Get(), readerAttrs.Get(), &_reader);
        if (FAILED(hr)) {
            CAM_LOGE("MFCreateSourceReaderFromMediaSource failed: 0x%08X", hr);
            return false;
        }

        if (!_setOutputBGRA32())
            CAM_LOGI("BGRA32 negotiation failed — using device default");

        _queryPreviewSize();
        CAM_LOGI("Device %d opened (%dx%d)", index, _previewW.load(), _previewH.load());
        return true;
    }

    bool _setOutputBGRA32() {
        ComPtr<IMFMediaType> type;
        HRESULT hr = MFCreateMediaType(&type);
        if (FAILED(hr)) return false;

        type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        type->SetGUID(MF_MT_SUBTYPE,    MFVideoFormat_RGB32);  // BGRA on Windows

        hr = _reader->SetCurrentMediaType(
                (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                nullptr, type.Get());
        return SUCCEEDED(hr);
    }

    void _queryPreviewSize() {
        ComPtr<IMFMediaType> type;
        HRESULT hr = _reader->GetCurrentMediaType(
                (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, &type);
        if (FAILED(hr)) { _previewW = 640; _previewH = 480; return; }

        UINT32 w = 0, h = 0;
        MFGetAttributeSize(type.Get(), MF_MT_FRAME_SIZE, &w, &h);
        if (w == 0 || h == 0) { w = 640; h = 480; }
        _previewW = (int)w;
        _previewH = (int)h;
    }

    // =========================================================================
    // READER LOOP — background thread
    // Calls IMFSourceReader::ReadSample synchronously, copies BGRA frames into
    // _frameData under _frameMutex, sets _pendingFrame for the UI thread.
    // =========================================================================
    void _readerLoop() {
        while (!_stopThread.load()) {
            if (_state == State::Closed) break;

            DWORD            streamIndex = 0;
            DWORD            flags       = 0;
            LONGLONG         timestamp   = 0;
            ComPtr<IMFSample> sample;

            HRESULT hr = _reader->ReadSample(
                    (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                    0, &streamIndex, &flags, &timestamp, &sample);

            if (FAILED(hr)) {
                CAM_LOGE("ReadSample failed: 0x%08X", hr);
                std::this_thread::sleep_for(std::chrono::milliseconds(33));
                continue;
            }
            if (flags & MF_SOURCE_READERF_ENDOFSTREAM) break;
            if (flags & MF_SOURCE_READERF_STREAMTICK)  continue;
            if (!sample) continue;

            ComPtr<IMFMediaBuffer> buffer;
            hr = sample->ConvertToContiguousBuffer(&buffer);
            if (FAILED(hr)) continue;

            BYTE*  bytes  = nullptr;
            DWORD  maxLen = 0, curLen = 0;
            hr = buffer->Lock(&bytes, &maxLen, &curLen);
            if (FAILED(hr)) continue;

            int    w        = _previewW.load();
            int    h        = _previewH.load();
            size_t expected = (size_t)(w * h * 4);

            if (curLen >= expected && bytes) {
                if (_captureRequested.exchange(false))
                    _saveJpegFromBGRA(bytes, w, h);

                {
                    std::lock_guard<std::mutex> lock(_frameMutex);
                    _frameData.assign(bytes, bytes + expected);
                }
                _pendingFrame = true;
                FluxWin_markNeedsPaint();
            }

            buffer->Unlock();
        }

        CAM_LOGI("Reader loop exited");
    }

    // =========================================================================
    // JPEG ENCODE + SAVE via WIC
    // =========================================================================
    void _saveJpegFromBGRA(const uint8_t* bgra, int w, int h) {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);

        ComPtr<IWICImagingFactory> wicFactory;
        HRESULT hr = CoCreateInstance(
                CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                IID_PPV_ARGS(&wicFactory));
        if (FAILED(hr)) { CAM_LOGE("WICImagingFactory failed: 0x%08X", hr); return; }

        // Build timestamped filename  (use localtime_s to avoid C4996 warning)
        std::time_t t = std::time(nullptr);
        std::tm     tm{};
        localtime_s(&tm, &t);
        char fname[64];
        std::strftime(fname, sizeof(fname), "IMG_%Y%m%d_%H%M%S.jpg", &tm);

        // In-memory IStream for encoded JPEG bytes
        ComPtr<IStream> stream;
        hr = CreateStreamOnHGlobal(nullptr, TRUE, &stream);
        if (FAILED(hr)) return;

        ComPtr<IWICBitmapEncoder> encoder;
        hr = wicFactory->CreateEncoder(GUID_ContainerFormatJpeg, nullptr, &encoder);
        if (FAILED(hr)) return;
        encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);

        ComPtr<IWICBitmapFrameEncode> frame;
        ComPtr<IPropertyBag2>         props;
        hr = encoder->CreateNewFrame(&frame, &props);
        if (FAILED(hr)) return;

        // JPEG quality = 90
        PROPBAG2 opt  = {};
        opt.pstrName  = const_cast<LPOLESTR>(L"ImageQuality");
        VARIANT var   = {};
        var.vt        = VT_R4;
        var.fltVal    = 0.90f;
        props->Write(1, &opt, &var);

        frame->Initialize(props.Get());
        frame->SetSize((UINT)w, (UINT)h);

        WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppBGR;
        frame->SetPixelFormat(&fmt);

        UINT stride = (UINT)(w * 4);
        frame->WritePixels((UINT)h, stride, stride * (UINT)h,
                           const_cast<BYTE*>(bgra));
        frame->Commit();
        encoder->Commit();

        // Extract encoded bytes from the HGLOBAL-backed stream
        HGLOBAL hg    = nullptr;
        GetHGlobalFromStream(stream.Get(), &hg);
        SIZE_T  size  = GlobalSize(hg);
        void*   pData = GlobalLock(hg);

        std::string path = FluxWin_saveJpegToPictures(
                fname,
                static_cast<const uint8_t*>(pData),
                size);

        GlobalUnlock(hg);

        if (!path.empty()) {
            _lastPhotoPath = path;
            CAM_LOGI("Photo saved: %s (%zu bytes)", fname, size);
            if (_onPhoto) _onPhoto(path);
        }
    }
};

// ============================================================================
// FluxWin_saveJpegToPictures — default implementation
// Saves JPEG to %USERPROFILE%\Pictures\FluxCam\<filename>
// ============================================================================
static std::string FluxWin_saveJpegToPictures(const std::string& filename,
                                               const uint8_t* data,
                                               size_t length) {
    PWSTR picturesPath = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_Pictures, 0, nullptr, &picturesPath)))
        return {};

    std::wstring dir = std::wstring(picturesPath) + L"\\FluxCam";
    CoTaskMemFree(picturesPath);

    CreateDirectoryW(dir.c_str(), nullptr);   // ok if already exists

    std::wstring wname(filename.begin(), filename.end());
    std::wstring wpath = dir + L"\\" + wname;

    HANDLE hFile = CreateFileW(wpath.c_str(), GENERIC_WRITE, 0, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return {};

    DWORD written = 0;
    WriteFile(hFile, data, (DWORD)length, &written, nullptr);
    CloseHandle(hFile);

    // Return as narrow UTF-8 string
    int needed = WideCharToMultiByte(CP_UTF8, 0, wpath.c_str(), -1,
                                     nullptr, 0, nullptr, nullptr);
    std::string result(needed - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wpath.c_str(), -1,
                        result.data(), needed, nullptr, nullptr);
    return result;
}

#endif // _WIN32



// V4L2 camera capture engine for FluxUI on Linux.
//
// Pipeline:
//   V4L2 (/dev/videoN) → MJPEG or YUYV → libjpeg / software YUV→RGB24 →
//       CPU frame buffer → lockFrame() → Cairo StretchBlit → SDL window
//
// Usage:
//   FluxCamera::get().open();           // open /dev/video0 + start capture
//   FluxCamera::get().capturePhoto();   // save current frame as JPEG
//   FluxCamera::get().flipCamera();     // cycle to next /dev/videoN
//   FluxCamera::get().setFlash(bool);   // no-op on most webcams; kept for API compat
//   FluxCamera::get().close();          // release all resources
//

#pragma once
#if defined(__linux__) && !defined(__ANDROID__)

#include <atomic>
#include <chrono>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <vector>

// V4L2
#include <linux/videodev2.h>

// libjpeg for MJPEG decode and JPEG save
#include <jpeglib.h>
#include <setjmp.h>

#define CAM_LOGI(fmt, ...) \
    do { fprintf(stderr, "[FluxCamera] INFO  " fmt "\n", ##__VA_ARGS__); fflush(stderr); } while(0)
#define CAM_LOGE(fmt, ...) \
    do { fprintf(stderr, "[FluxCamera] ERROR " fmt "\n", ##__VA_ARGS__); fflush(stderr); } while(0)

// Trigger a repaint from the capture thread — same pattern as Windows.
// Implemented in flux_window_linux.cpp via SDL_PushEvent (exposed below).
extern void FluxWin_markNeedsPaint();

// ============================================================================
// FluxCamera  (Linux / V4L2 implementation)
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

    State  getState()         const { return _state.load(); }
    bool   isPreviewing()     const { return _state == State::Previewing; }
    bool   isCapturing()      const { return _state == State::Capturing;  }
    bool   isFlashOn()        const { return _flashOn.load(); }
    bool   hasNewFrame()      const { return _newFrame.load(); }
    int    getPreviewWidth()  const { return _previewW.load(); }
    int    getPreviewHeight() const { return _previewH.load(); }
    bool   isFrontCamera()    const { return false; } // V4L2 has no facing concept

    const std::string& getLastPhotoPath() const { return _lastPhotoPath; }

    // ── Frame access — call after hasNewFrame() returns true ──────────────────
    // Locks _frameMutex for its lifetime; copy pixels then release promptly.
    struct FrameLock {
        std::unique_lock<std::mutex> lock;
        const uint8_t* data   = nullptr;
        int            width  = 0;
        int            height = 0;
        int            stride = 0; // bytes per row (width * 3 for RGB24)
    };

    FrameLock lockFrame() {
        FrameLock fl;
        fl.lock   = std::unique_lock<std::mutex>(_frameMutex);
        fl.data   = _frameData.empty() ? nullptr : _frameData.data();
        fl.width  = _previewW.load();
        fl.height = _previewH.load();
        fl.stride = fl.width * 3;
        _newFrame = false;
        return fl;
    }

    // ── Callbacks ─────────────────────────────────────────────────────────────
    using PhotoCallback = std::function<void(const std::string& path)>;
    void setOnPhoto(PhotoCallback cb) { _onPhoto = std::move(cb); }

    // ── Flash — no-op on most webcams; kept for widget API compatibility ───────
    void setFlash(bool on) { _flashOn = on; }
    void toggleFlash()     { setFlash(!_flashOn.load()); }

    // =========================================================================
    // OPEN — enumerate /dev/videoN, open first usable, start capture thread
    // =========================================================================
    bool open(bool /*useFront*/ = false) {
        if (_state != State::Closed) close();

        _state = State::Opening;

        // Enumerate available video devices
        _enumerateDevices();
        if (_devices.empty()) {
            CAM_LOGE("No V4L2 video devices found");
            _state = State::Error;
            return false;
        }

        _deviceIndex = 0;
        if (!_openDevice(_deviceIndex)) {
            _state = State::Error;
            return false;
        }

        // Start background capture thread
        _stopThread = false;
        _captureThread = std::thread(&FluxCamera::_captureLoop, this);

        _state = State::Previewing;
        CAM_LOGI("Preview started on %s", _devices[_deviceIndex].c_str());
        return true;
    }

    // =========================================================================
    // FLIP — cycle to next /dev/videoN
    // =========================================================================
    void flipCamera() {
        if (_devices.size() < 2) return;
        int next = (_deviceIndex + 1) % (int)_devices.size();
        close();
        _deviceIndex = next;
        _state = State::Opening;
        if (!_openDevice(_deviceIndex)) {
            _state = State::Error;
            return;
        }
        _stopThread = false;
        _captureThread = std::thread(&FluxCamera::_captureLoop, this);
        _state = State::Previewing;
    }

    // =========================================================================
    // UPDATE FRAME — call once per render tick on the paint thread.
    // Returns true when a new frame is ready; call lockFrame() to read pixels.
    // =========================================================================
    bool updateFrame() {
        if (!_pendingFrame.load()) { _newFrame = false; return false; }
        _pendingFrame = false;
        _newFrame     = true;
        return true;
    }

    // =========================================================================
    // CAPTURE PHOTO — saves the current frame as JPEG to ~/Pictures/FluxCam/
    // =========================================================================
    void capturePhoto() {
        if (_state != State::Previewing) return;
        _state = State::Capturing;
        _captureRequested = true;
        CAM_LOGI("Capture requested");
    }

    // =========================================================================
    // CLOSE
    // =========================================================================
    void close() {
        _state = State::Closed;

        _stopThread = true;
        if (_captureThread.joinable())
            _captureThread.join();

        _stopStreaming();
        _freeMmapBuffers();

        if (_fd >= 0) {
            ::close(_fd);
            _fd = -1;
        }

        _pendingFrame     = false;
        _newFrame         = false;
        _captureRequested = false;
        _previewW         = 0;
        _previewH         = 0;

        {
            std::lock_guard<std::mutex> lk(_frameMutex);
            _frameData.clear();
        }

        CAM_LOGI("Camera closed");
    }

private:
    FluxCamera()  = default;
    ~FluxCamera() { close(); }

    // ── V4L2 state ────────────────────────────────────────────────────────────
    int         _fd          = -1;
    int         _deviceIndex = 0;
    bool        _isMJPEG     = false; // true = MJPEG, false = YUYV

    struct MmapBuffer { void* start = nullptr; size_t length = 0; };
    std::vector<MmapBuffer> _buffers;

    // ── Device list ───────────────────────────────────────────────────────────
    std::vector<std::string> _devices;

    // ── State ─────────────────────────────────────────────────────────────────
    std::atomic<State> _state            { State::Closed };
    std::atomic<bool>  _flashOn          { false };
    std::atomic<bool>  _pendingFrame     { false };
    std::atomic<bool>  _newFrame         { false };
    std::atomic<bool>  _captureRequested { false };
    std::atomic<bool>  _stopThread       { false };
    std::atomic<int>   _previewW         { 0 };
    std::atomic<int>   _previewH         { 0 };

    std::string   _lastPhotoPath;
    PhotoCallback _onPhoto;

    // ── Frame double-buffer ───────────────────────────────────────────────────
    // Writer: capture thread.  Reader: paint thread via lockFrame().
    std::mutex           _frameMutex;
    std::vector<uint8_t> _frameData; // RGB24

    // ── Thread ────────────────────────────────────────────────────────────────
    std::thread _captureThread;

    // =========================================================================
    // DEVICE ENUMERATION
    // =========================================================================
    void _enumerateDevices() {
        _devices.clear();
        for (int i = 0; i < 16; i++) {
            std::string path = "/dev/video" + std::to_string(i);
            int fd = ::open(path.c_str(), O_RDWR | O_NONBLOCK);
            if (fd < 0) continue;

            v4l2_capability cap{};
            if (ioctl(fd, VIDIOC_QUERYCAP, &cap) == 0 &&
                (cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) &&
                (cap.capabilities & (V4L2_CAP_STREAMING | V4L2_CAP_READWRITE))) {
                _devices.push_back(path);
                CAM_LOGI("Found device: %s (%s)", path.c_str(), cap.card);
            }
            ::close(fd);
        }
    }

    // =========================================================================
    // OPEN DEVICE + NEGOTIATE FORMAT
    // =========================================================================
    bool _openDevice(int index) {
        if (index < 0 || index >= (int)_devices.size()) return false;

        _fd = ::open(_devices[index].c_str(), O_RDWR | O_NONBLOCK);
        if (_fd < 0) {
            CAM_LOGE("open(%s) failed: %s", _devices[index].c_str(), strerror(errno));
            return false;
        }

        v4l2_capability cap{};
        if (ioctl(_fd, VIDIOC_QUERYCAP, &cap) < 0) {
            CAM_LOGE("VIDIOC_QUERYCAP failed");
            return false;
        }

        // Try MJPEG first (much less CPU than YUYV decode)
        _isMJPEG = _trySetFormat(V4L2_PIX_FMT_MJPEG);
        if (!_isMJPEG) {
            if (!_trySetFormat(V4L2_PIX_FMT_YUYV)) {
                CAM_LOGE("Neither MJPEG nor YUYV supported");
                return false;
            }
        }
        CAM_LOGI("Format: %s", _isMJPEG ? "MJPEG" : "YUYV");

        // Read back negotiated size
        v4l2_format fmt{};
        fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        ioctl(_fd, VIDIOC_G_FMT, &fmt);
        _previewW = (int)fmt.fmt.pix.width;
        _previewH = (int)fmt.fmt.pix.height;
        CAM_LOGI("Preview size: %dx%d", _previewW.load(), _previewH.load());

        if (!_initMmap()) return false;
        if (!_startStreaming()) return false;

        return true;
    }

    bool _trySetFormat(uint32_t pixfmt) {
        v4l2_format fmt{};
        fmt.type                = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        fmt.fmt.pix.width       = 1280;
        fmt.fmt.pix.height      = 720;
        fmt.fmt.pix.pixelformat = pixfmt;
        fmt.fmt.pix.field       = V4L2_FIELD_ANY;

        if (ioctl(_fd, VIDIOC_S_FMT, &fmt) < 0) return false;
        return fmt.fmt.pix.pixelformat == pixfmt;
    }

    // =========================================================================
    // MMAP BUFFER MANAGEMENT
    // =========================================================================
    bool _initMmap() {
        v4l2_requestbuffers req{};
        req.count  = 4;
        req.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        req.memory = V4L2_MEMORY_MMAP;

        if (ioctl(_fd, VIDIOC_REQBUFS, &req) < 0 || req.count < 2) {
            CAM_LOGE("VIDIOC_REQBUFS failed");
            return false;
        }

        _buffers.resize(req.count);
        for (uint32_t i = 0; i < req.count; i++) {
            v4l2_buffer buf{};
            buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buf.memory = V4L2_MEMORY_MMAP;
            buf.index  = i;

            if (ioctl(_fd, VIDIOC_QUERYBUF, &buf) < 0) {
                CAM_LOGE("VIDIOC_QUERYBUF[%u] failed", i);
                return false;
            }

            _buffers[i].length = buf.length;
            _buffers[i].start  = mmap(nullptr, buf.length,
                                      PROT_READ | PROT_WRITE,
                                      MAP_SHARED, _fd, buf.m.offset);

            if (_buffers[i].start == MAP_FAILED) {
                CAM_LOGE("mmap[%u] failed: %s", i, strerror(errno));
                return false;
            }
        }
        return true;
    }

    void _freeMmapBuffers() {
        for (auto& b : _buffers) {
            if (b.start && b.start != MAP_FAILED)
                munmap(b.start, b.length);
        }
        _buffers.clear();
    }

    bool _startStreaming() {
        // Enqueue all buffers
        for (uint32_t i = 0; i < _buffers.size(); i++) {
            v4l2_buffer buf{};
            buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buf.memory = V4L2_MEMORY_MMAP;
            buf.index  = i;
            if (ioctl(_fd, VIDIOC_QBUF, &buf) < 0) {
                CAM_LOGE("VIDIOC_QBUF[%u] failed", i);
                return false;
            }
        }

        v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (ioctl(_fd, VIDIOC_STREAMON, &type) < 0) {
            CAM_LOGE("VIDIOC_STREAMON failed: %s", strerror(errno));
            return false;
        }
        CAM_LOGI("Streaming started");
        return true;
    }

    void _stopStreaming() {
        if (_fd < 0) return;
        v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        ioctl(_fd, VIDIOC_STREAMOFF, &type);
    }

    // =========================================================================
    // CAPTURE LOOP — background thread
    // =========================================================================
    void _captureLoop() {
        while (!_stopThread.load()) {
            if (_state == State::Closed) break;

            // select() with 100ms timeout so _stopThread is checked regularly
            fd_set fds;
            FD_ZERO(&fds);
            FD_SET(_fd, &fds);
            timeval tv{ 0, 100000 }; // 100ms

            int ret = select(_fd + 1, &fds, nullptr, nullptr, &tv);
            if (ret < 0) {
                if (errno == EINTR) continue;
                CAM_LOGE("select() error: %s", strerror(errno));
                break;
            }
            if (ret == 0) continue; // timeout — loop and re-check _stopThread

            // Dequeue filled buffer
            v4l2_buffer buf{};
            buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buf.memory = V4L2_MEMORY_MMAP;

            if (ioctl(_fd, VIDIOC_DQBUF, &buf) < 0) {
                if (errno == EAGAIN) continue;
                CAM_LOGE("VIDIOC_DQBUF failed: %s", strerror(errno));
                break;
            }

            const uint8_t* raw    = (const uint8_t*)_buffers[buf.index].start;
            size_t         rawLen = buf.bytesused;

            // Decode frame to RGB24
            std::vector<uint8_t> rgb;
            bool decoded = _isMJPEG
                ? _decodeMJPEG(raw, rawLen, rgb)
                : _decodeYUYV(raw, rawLen, rgb);

            if (decoded && !rgb.empty()) {
                // Save photo if requested
                if (_captureRequested.exchange(false)) {
                    _saveJpeg(rgb, _previewW.load(), _previewH.load());
                    _state = State::Previewing;
                }

                // Publish frame to paint thread
                {
                    std::lock_guard<std::mutex> lk(_frameMutex);
                    _frameData = std::move(rgb);
                }
                _pendingFrame = true;
                FluxWin_markNeedsPaint();
            }

            // Re-enqueue buffer
            if (ioctl(_fd, VIDIOC_QBUF, &buf) < 0)
                CAM_LOGE("VIDIOC_QBUF failed: %s", strerror(errno));
        }

        CAM_LOGI("Capture loop exited");
    }

    // =========================================================================
    // MJPEG DECODE — libjpeg → RGB24
    // =========================================================================

    // Custom libjpeg error handler that longjmps instead of exit()
    struct JpegErrorMgr {
        jpeg_error_mgr pub;
        jmp_buf        jmpBuf;
    };

    static void _jpegErrorExit(j_common_ptr cinfo) {
        auto* mgr = reinterpret_cast<JpegErrorMgr*>(cinfo->err);
        longjmp(mgr->jmpBuf, 1);
    }

    bool _decodeMJPEG(const uint8_t* data, size_t len,
                      std::vector<uint8_t>& rgb) {
        jpeg_decompress_struct cinfo{};
        JpegErrorMgr           jerr{};

        cinfo.err = jpeg_std_error(&jerr.pub);
        jerr.pub.error_exit = _jpegErrorExit;

        if (setjmp(jerr.jmpBuf)) {
            jpeg_destroy_decompress(&cinfo);
            return false;
        }

        jpeg_create_decompress(&cinfo);
        jpeg_mem_src(&cinfo, data, (unsigned long)len);

        if (jpeg_read_header(&cinfo, TRUE) != JPEG_HEADER_OK) {
            jpeg_destroy_decompress(&cinfo);
            return false;
        }

        cinfo.out_color_space = JCS_RGB;
        jpeg_start_decompress(&cinfo);

        int w = (int)cinfo.output_width;
        int h = (int)cinfo.output_height;
        rgb.resize((size_t)(w * h * 3));

        JSAMPROW rowPtr[1];
        while ((int)cinfo.output_scanline < h) {
            rowPtr[0] = rgb.data() + cinfo.output_scanline * w * 3;
            jpeg_read_scanlines(&cinfo, rowPtr, 1);
        }

        jpeg_finish_decompress(&cinfo);
        jpeg_destroy_decompress(&cinfo);

        // Update preview dimensions if they changed
        _previewW = w;
        _previewH = h;
        return true;
    }

    // =========================================================================
    // YUYV DECODE — software YUV422 → RGB24
    // Clamp helper avoids per-pixel branching in the inner loop.
    // =========================================================================
    bool _decodeYUYV(const uint8_t* src, size_t /*len*/,
                     std::vector<uint8_t>& rgb) {
        int w = _previewW.load();
        int h = _previewH.load();
        if (w <= 0 || h <= 0) return false;

        rgb.resize((size_t)(w * h * 3));
        uint8_t* dst = rgb.data();

        // YUYV: 4 bytes per 2 pixels — Y0 U Y1 V
        for (int i = 0; i < w * h / 2; i++) {
            int y0 = src[0], u = src[1], y1 = src[2], v = src[3];
            src += 4;

            int c0 = y0 - 16, c1 = y1 - 16;
            int d  = u  - 128;
            int e  = v  - 128;

            // ITU-R BT.601 coefficients (integer approximation)
            auto clamp = [](int x) -> uint8_t {
                return x < 0 ? 0 : x > 255 ? 255 : (uint8_t)x;
            };

            dst[0] = clamp((298 * c0 + 409 * e + 128) >> 8);
            dst[1] = clamp((298 * c0 - 100 * d - 208 * e + 128) >> 8);
            dst[2] = clamp((298 * c0 + 516 * d + 128) >> 8);
            dst += 3;

            dst[0] = clamp((298 * c1 + 409 * e + 128) >> 8);
            dst[1] = clamp((298 * c1 - 100 * d - 208 * e + 128) >> 8);
            dst[2] = clamp((298 * c1 + 516 * d + 128) >> 8);
            dst += 3;
        }
        return true;
    }

    // =========================================================================
    // JPEG SAVE — encode RGB24 → JPEG via libjpeg, write to ~/Pictures/FluxCam
    // =========================================================================
    void _saveJpeg(const std::vector<uint8_t>& rgb, int w, int h) {
        // Build output directory: ~/Pictures/FluxCam/
        const char* home = getenv("HOME");
        std::string dir  = home ? std::string(home) + "/Pictures/FluxCam" : "/tmp/FluxCam";
        std::filesystem::create_directories(dir);

        // Timestamped filename
        std::time_t t  = std::time(nullptr);
        std::tm*    tm = std::localtime(&t);
        char fname[64];
        std::strftime(fname, sizeof(fname), "IMG_%Y%m%d_%H%M%S.jpg", tm);

        std::string path = dir + "/" + fname;

        // Encode
        jpeg_compress_struct cinfo{};
        jpeg_error_mgr       jerr{};
        cinfo.err = jpeg_std_error(&jerr);
        jpeg_create_compress(&cinfo);

        FILE* fp = fopen(path.c_str(), "wb");
        if (!fp) {
            CAM_LOGE("Cannot open %s for writing", path.c_str());
            jpeg_destroy_compress(&cinfo);
            return;
        }

        jpeg_stdio_dest(&cinfo, fp);
        cinfo.image_width      = (JDIMENSION)w;
        cinfo.image_height     = (JDIMENSION)h;
        cinfo.input_components = 3;
        cinfo.in_color_space   = JCS_RGB;
        jpeg_set_defaults(&cinfo);
        jpeg_set_quality(&cinfo, 90, TRUE);
        jpeg_start_compress(&cinfo, TRUE);

        JSAMPROW rowPtr[1];
        while ((int)cinfo.next_scanline < h) {
            rowPtr[0] = const_cast<uint8_t*>(
                rgb.data() + cinfo.next_scanline * w * 3);
            jpeg_write_scanlines(&cinfo, rowPtr, 1);
        }

        jpeg_finish_compress(&cinfo);
        fclose(fp);
        jpeg_destroy_compress(&cinfo);

        _lastPhotoPath = path;
        CAM_LOGI("Photo saved: %s", path.c_str());
        if (_onPhoto) _onPhoto(path);
    }
};


#endif // __linux__ && !__ANDROID__