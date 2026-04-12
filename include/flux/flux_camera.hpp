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