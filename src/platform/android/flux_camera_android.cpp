// flux_camera_android.cpp
// NDK Camera2 photo capture engine for FluxUI on Android.
//
// Pipeline:
//   ACameraManager → ACameraDevice → ACaptureSession
//       ├── Preview  → SurfaceTexture → OES texture → NanoVG viewfinder
//       └── Capture  → AImageReader  → JPEG bytes   → MediaStore (Gallery)

#ifdef __ANDROID__

#include "flux/flux_camera.hpp"
#include <EGL/egl.h>
#include <GLES2/gl2ext.h>
#include <camera/NdkCameraMetadataTags.h>
#include <camera/NdkCaptureRequest.h>
#include <android/native_window_jni.h>
#include <android/log.h>

#include <ctime>

#define CAM_LOGI(...) __android_log_print(ANDROID_LOG_INFO,  "FluxCamera", __VA_ARGS__)
#define CAM_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "FluxCamera", __VA_ARGS__)

// ── Externals from native-lib.cpp ───────────────────────────────────────────
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
// Singleton
// ============================================================================

FluxCamera& FluxCamera::get() {
    static FluxCamera instance;
    return instance;
}

bool FluxCamera::isFrontCamera() const { return _useFront.load(); }

// ============================================================================
// FLASH
// ============================================================================

void FluxCamera::setFlash(bool on) {
    _flashOn = on;
    _applyFlash();
}

void FluxCamera::_applyFlash() {
    if (!_previewRequest || _state != State::Previewing) return;
    uint8_t torchMode = _flashOn.load()
                        ? ACAMERA_FLASH_MODE_TORCH
                        : ACAMERA_FLASH_MODE_OFF;
    ACaptureRequest_setEntry_u8(_previewRequest,
                                ACAMERA_FLASH_MODE, 1, &torchMode);
    if (_captureSession)
        // Keep using &_repeatCallbacks here too.
        ACameraCaptureSession_setRepeatingRequest(
                _captureSession,
                &_repeatCallbacks,
                1, &_previewRequest,
                nullptr);
}

// ============================================================================
// OPEN — selects camera, creates OES texture, starts preview
// Must be called from the GL thread.
// ============================================================================

bool FluxCamera::open(bool useFront) {
    if (_state != State::Closed) close();

    _useFront = useFront;
    _state    = State::Opening;

    _manager = ACameraManager_create();
    if (!_manager) {
        CAM_LOGE("ACameraManager_create failed");
        _state = State::Error;
        return false;
    }

    // Select camera ID
    if (!_selectCamera(useFront)) {
        _state = State::Error;
        return false;
    }

    // Query max JPEG size
    _queryMaxJpegSize();

    // Create OES preview texture
    if (!_createOESTexture()) {
        _state = State::Error;
        return false;
    }

    // Create ImageReader for JPEG capture
    if (!_createImageReader()) {
        _state = State::Error;
        return false;
    }

    // Open camera device
    if (!_openDevice()) {
        _state = State::Error;
        return false;
    }

    return true;
}

// ============================================================================
// FLIP — switches between front and back camera
// ============================================================================

void FluxCamera::flipCamera() {
    bool front = !_useFront.load();
    close();
    open(front);
}

// ============================================================================
// UPDATE FRAME — call once per vsync on the GL thread
// Latches new preview frame into OES texture.
// ============================================================================

bool FluxCamera::updateFrame() {
    if (!_surfaceTexture) { _newFrame = false; return false; }
    if (!_pendingFrame.load()) { _newFrame = false; return false; }

    FluxVideo_updateTexImage(_surfaceTexture);
    _pendingFrame = false;
    _newFrame     = true;
    return true;
}

// ============================================================================
// CAPTURE PHOTO — triggers a still JPEG capture
// ============================================================================

void FluxCamera::capturePhoto() {
    if (_state != State::Previewing) return;
    if (!_captureSession || !_captureRequest) return;

    _state = State::Capturing;

    // Apply flash
    uint8_t flashMode = _flashOn.load()
                        ? ACAMERA_FLASH_MODE_SINGLE
                        : ACAMERA_FLASH_MODE_OFF;
    ACaptureRequest_setEntry_u8(_captureRequest,
                                ACAMERA_FLASH_MODE, 1, &flashMode);

    // Trigger AF
    uint8_t afTrigger = ACAMERA_CONTROL_AF_TRIGGER_START;
    ACaptureRequest_setEntry_u8(_captureRequest,
                                ACAMERA_CONTROL_AF_TRIGGER, 1, &afTrigger);

    // Capture — use _captureCallbacks (still capture, not repeating)
    ACameraCaptureSession_capture(
            _captureSession,
            &_captureCallbacks,
            1, &_captureRequest,
            nullptr);

    CAM_LOGI("Capture triggered");
}

// ============================================================================
// CLOSE
// ============================================================================

void FluxCamera::close() {
    _state = State::Closed;

    if (_captureSession) {
        ACameraCaptureSession_stopRepeating(_captureSession);
        ACameraCaptureSession_close(_captureSession);
        _captureSession = nullptr;
    }
    if (_captureRequest) {
        ACaptureRequest_free(_captureRequest);
        _captureRequest = nullptr;
    }
    if (_previewRequest) {
        ACaptureRequest_free(_previewRequest);
        _previewRequest = nullptr;
    }
    if (_previewOutput) {
        ACameraOutputTarget_free(_previewOutput);
        _previewOutput = nullptr;
    }
    if (_captureOutput) {
        ACameraOutputTarget_free(_captureOutput);
        _captureOutput = nullptr;
    }
    if (_sessionOutputContainer) {
        ACaptureSessionOutputContainer_free(_sessionOutputContainer);
        _sessionOutputContainer = nullptr;
    }
    if (_previewSessionOutput) {
        ACaptureSessionOutput_free(_previewSessionOutput);
        _previewSessionOutput = nullptr;
    }
    if (_captureSessionOutput) {
        ACaptureSessionOutput_free(_captureSessionOutput);
        _captureSessionOutput = nullptr;
    }
    if (_cameraDevice) {
        ACameraDevice_close(_cameraDevice);
        _cameraDevice = nullptr;
    }
    if (_imageReader) {
        AImageReader_delete(_imageReader);
        _imageReader = nullptr;
        _imageReaderWindow = nullptr;  // owned by reader, already freed
    }
    if (_previewWindow) {
        ANativeWindow_release(_previewWindow);
        _previewWindow = nullptr;
    }
    if (_surfaceTexture) {
        FluxVideo_destroySurfaceTexture(_surfaceTexture);
        _surfaceTexture = nullptr;
    }
    if (_oesTexture) {
        glDeleteTextures(1, &_oesTexture);
        _oesTexture = 0;
    }
    if (_cameraIdList) {
        ACameraManager_deleteCameraIdList(_cameraIdList);
        _cameraIdList = nullptr;
    }
    if (_manager) {
        ACameraManager_delete(_manager);
        _manager = nullptr;
    }

    _selectedCameraId.clear();
    _pendingFrame = false;
    _newFrame     = false;
    _previewW     = 0;
    _previewH     = 0;

    CAM_LOGI("Camera closed");
}

// =============================================================================
// CAMERA SELECTION
// =============================================================================

bool FluxCamera::_selectCamera(bool useFront) {
    ACameraManager_getCameraIdList(_manager, &_cameraIdList);
    if (!_cameraIdList || _cameraIdList->numCameras == 0) {
        CAM_LOGE("No cameras found");
        return false;
    }

    uint8_t wantFacing = useFront
                         ? ACAMERA_LENS_FACING_FRONT
                         : ACAMERA_LENS_FACING_BACK;

    for (int i = 0; i < _cameraIdList->numCameras; i++) {
        const char* id = _cameraIdList->cameraIds[i];
        ACameraMetadata* meta = nullptr;
        ACameraManager_getCameraCharacteristics(_manager, id, &meta);

        ACameraMetadata_const_entry entry{};
        ACameraMetadata_getConstEntry(meta, ACAMERA_LENS_FACING, &entry);
        uint8_t facing = entry.data.u8[0];
        ACameraMetadata_free(meta);

        if (facing == wantFacing) {
            _selectedCameraId = id;
            CAM_LOGI("Selected camera: %s (facing=%d)", id, facing);
            return true;
        }
    }

    // Fallback: use first camera
    _selectedCameraId = _cameraIdList->cameraIds[0];
    CAM_LOGI("Fallback camera: %s", _selectedCameraId.c_str());
    return true;
}

// =============================================================================
// QUERY MAX JPEG SIZE
// =============================================================================

void FluxCamera::_queryMaxJpegSize() {
    ACameraMetadata* meta = nullptr;
    ACameraManager_getCameraCharacteristics(
            _manager, _selectedCameraId.c_str(), &meta);
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
            _jpegW = maxW;
            _jpegH = maxH;
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
        _previewW = bestW;
        _previewH = bestH;
        CAM_LOGI("Preview size: %dx%d", bestW, bestH);
    }

    ACameraMetadata_free(meta);
}

// =============================================================================
// OES TEXTURE + SURFACE TEXTURE (preview output)
// =============================================================================

bool FluxCamera::_createOESTexture() {
    glGenTextures(1, &_oesTexture);
    if (!_oesTexture) return false;

    glBindTexture(GL_TEXTURE_EXTERNAL_OES, _oesTexture);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, 0);

    _surfaceTexture = FluxVideo_createSurfaceTexture(_oesTexture);
    if (!_surfaceTexture) {
        CAM_LOGE("SurfaceTexture creation failed");
        return false;
    }

    _previewWindow = FluxVideo_getNativeWindow(_surfaceTexture);
    if (!_previewWindow) {
        CAM_LOGE("getNativeWindow failed");
        return false;
    }

    ANativeWindow_setBuffersGeometry(
            _previewWindow,
            _previewW.load(), _previewH.load(),
            0);

    CAM_LOGI("OES texture %u + SurfaceTexture created", _oesTexture);
    return true;
}

// =============================================================================
// IMAGE READER (JPEG still capture output)
// =============================================================================

bool FluxCamera::_createImageReader() {
    media_status_t status = AImageReader_new(
            _jpegW.load(), _jpegH.load(),
            AIMAGE_FORMAT_JPEG,
            2,
            &_imageReader);

    if (status != AMEDIA_OK || !_imageReader) {
        CAM_LOGE("AImageReader_new failed: %d", status);
        return false;
    }

    _imageListener.context          = this;
    _imageListener.onImageAvailable = _onImageAvailable;
    AImageReader_setImageListener(_imageReader, &_imageListener);

    AImageReader_getWindow(_imageReader, &_imageReaderWindow);
    CAM_LOGI("ImageReader created %dx%d JPEG",
             _jpegW.load(), _jpegH.load());
    return true;
}

// =============================================================================
// OPEN DEVICE
// =============================================================================

bool FluxCamera::_openDevice() {
    _deviceCallbacks.context        = this;
    _deviceCallbacks.onDisconnected = _onDeviceDisconnected;
    _deviceCallbacks.onError        = _onDeviceError;

    camera_status_t status = ACameraManager_openCamera(
            _manager,
            _selectedCameraId.c_str(),
            &_deviceCallbacks,
            &_cameraDevice);

    if (status != ACAMERA_OK || !_cameraDevice) {
        CAM_LOGE("ACameraManager_openCamera failed: %d", status);
        return false;
    }

    CAM_LOGI("Camera device opened");
    _createSession();
    return true;
}

// =============================================================================
// CREATE SESSION
// =============================================================================

void FluxCamera::_createSession() {
    // Build output container
    ACaptureSessionOutputContainer_create(&_sessionOutputContainer);

    // Preview output
    ACaptureSessionOutput_create(_previewWindow, &_previewSessionOutput);
    ACaptureSessionOutputContainer_add(
            _sessionOutputContainer, _previewSessionOutput);

    // Capture output
    ACaptureSessionOutput_create(_imageReaderWindow, &_captureSessionOutput);
    ACaptureSessionOutputContainer_add(
            _sessionOutputContainer, _captureSessionOutput);

    // Session state callbacks
    _sessionCallbacks.context  = this;
    _sessionCallbacks.onReady  = _onSessionReady;
    _sessionCallbacks.onActive = _onSessionActive;
    _sessionCallbacks.onClosed = _onSessionClosed;

    camera_status_t status = ACameraDevice_createCaptureSession(
            _cameraDevice,
            _sessionOutputContainer,
            &_sessionCallbacks,
            &_captureSession);

    if (status != ACAMERA_OK) {
        CAM_LOGE("createCaptureSession failed: %d", status);
        _state = State::Error;
        return;
    }

    // ── Build preview request ────────────────────────────────────────────
    ACameraDevice_createCaptureRequest(
            _cameraDevice, TEMPLATE_PREVIEW, &_previewRequest);
    ACameraOutputTarget_create(_previewWindow, &_previewOutput);
    ACaptureRequest_addTarget(_previewRequest, _previewOutput);

    uint8_t afMode = ACAMERA_CONTROL_AF_MODE_CONTINUOUS_PICTURE;
    uint8_t aeMode = ACAMERA_CONTROL_AE_MODE_ON;
    ACaptureRequest_setEntry_u8(_previewRequest,
                                ACAMERA_CONTROL_AF_MODE, 1, &afMode);
    ACaptureRequest_setEntry_u8(_previewRequest,
                                ACAMERA_CONTROL_AE_MODE, 1, &aeMode);

    // ── Build still capture request ──────────────────────────────────────
    ACameraDevice_createCaptureRequest(
            _cameraDevice, TEMPLATE_STILL_CAPTURE, &_captureRequest);
    ACameraOutputTarget_create(_previewWindow,     &_previewOutput);
    ACameraOutputTarget_create(_imageReaderWindow, &_captureOutput);
    ACaptureRequest_addTarget(_captureRequest, _previewOutput);
    ACaptureRequest_addTarget(_captureRequest, _captureOutput);

    ACaptureRequest_setEntry_u8(_captureRequest,
                                ACAMERA_CONTROL_AF_MODE, 1, &afMode);
    ACaptureRequest_setEntry_u8(_captureRequest,
                                ACAMERA_CONTROL_AE_MODE, 1, &aeMode);

    // ── Repeating preview callbacks — fires every preview frame ───────────
    // _repeatCallbacks drives _pendingFrame for the GL thread.
    // _captureCallbacks is used only for one-shot still captures.
    _repeatCallbacks.context            = this;
    _repeatCallbacks.onCaptureCompleted = _onPreviewFrameCompleted;
    _repeatCallbacks.onCaptureFailed    = nullptr;

    // ── Still capture callbacks ────────────────────────────────────────────
    _captureCallbacks.context            = this;
    _captureCallbacks.onCaptureCompleted = _onCaptureCompleted;
    _captureCallbacks.onCaptureFailed    = _onCaptureFailed;

    // ── Pass &_repeatCallbacks so every preview frame sets pendingFrame ────
    ACameraCaptureSession_setRepeatingRequest(
            _captureSession,
            &_repeatCallbacks,   // not nullptr — preview frames must be signalled
            1, &_previewRequest,
            nullptr);

    _state = State::Previewing;
    CAM_LOGI("Preview started");
}

// =============================================================================
// IMAGE AVAILABLE — called when JPEG is ready
// =============================================================================

void FluxCamera::_onImageAvailable(void* ctx, AImageReader* reader) {
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
            self->_lastPhotoPath = path;
            CAM_LOGI("Photo saved: %s (%d bytes)", buf, length);
            if (self->_onPhoto) self->_onPhoto(path);
        }
    }

    AImage_delete(image);
    self->_state = State::Previewing;
}

// =============================================================================
// NDK CALLBACKS
// =============================================================================

void FluxCamera::_onDeviceDisconnected(void* ctx, ACameraDevice* /*dev*/) {
    CAM_LOGI("Camera disconnected");
    static_cast<FluxCamera*>(ctx)->_state = State::Closed;
}

void FluxCamera::_onDeviceError(void* ctx, ACameraDevice* /*dev*/, int err) {
    CAM_LOGE("Camera device error: %d", err);
    static_cast<FluxCamera*>(ctx)->_state = State::Error;
}

void FluxCamera::_onSessionReady(void* /*ctx*/, ACameraCaptureSession* /*session*/) {
    CAM_LOGI("Session ready");
}

void FluxCamera::_onSessionActive(void* ctx, ACameraCaptureSession* /*session*/) {
    CAM_LOGI("Session active");
    // Prime the pump — first frame signal so GL thread doesn't stall
    static_cast<FluxCamera*>(ctx)->_pendingFrame = true;
}

void FluxCamera::_onSessionClosed(void* /*ctx*/, ACameraCaptureSession* /*session*/) {
    CAM_LOGI("Session closed");
}

// Fires once per repeating preview frame.
// This is the correct place to signal _pendingFrame for live preview.
void FluxCamera::_onPreviewFrameCompleted(void* ctx,
                                     ACameraCaptureSession* /*session*/,
                                     ACaptureRequest* /*request*/,
                                     const ACameraMetadata* /*result*/) {
    static_cast<FluxCamera*>(ctx)->_pendingFrame = true;
}

// Fires only for one-shot still captures triggered by capturePhoto()
void FluxCamera::_onCaptureCompleted(void* /*ctx*/,
                                ACameraCaptureSession* /*session*/,
                                ACaptureRequest* /*request*/,
                                const ACameraMetadata* /*result*/) {
    CAM_LOGI("Still capture completed");
    // Still capture done — pendingFrame will resume via repeating callback
}

void FluxCamera::_onCaptureFailed(void* ctx,
                             ACameraCaptureSession* /*session*/,
                             ACaptureRequest* /*request*/,
                             ACameraCaptureFailure* failure) {
    CAM_LOGE("Capture failed: reason=%d", failure ? failure->reason : -1);
    static_cast<FluxCamera*>(ctx)->_state = State::Previewing;
}

#endif // __ANDROID__