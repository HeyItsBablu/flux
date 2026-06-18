// flux_camera.hpp
// Cross-platform photo capture engine for FluxUI.
//
// Platform implementations:
//   flux_camera_android.cpp — NDK Camera2 (ACameraManager / AImageReader)
//   flux_camera_win32.cpp   — Windows Media Foundation (IMFSourceReader)
//   flux_camera_linux.cpp   — V4L2 (/dev/videoN)
//   flux_camera_web.cpp     — getUserMedia() + hidden <video> element
//
// Usage:
//   FluxCamera::get().open();           // open back (default) camera + start preview
//   FluxCamera::get().capturePhoto();   // take a photo
//   FluxCamera::get().flipCamera();     // toggle/cycle camera
//   FluxCamera::get().setFlash(true);   // flash/torch on/off (no-op where unsupported)
//   FluxCamera::get().close();          // release all resources

#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#if defined(__ANDROID__)
#include <GLES2/gl2.h>
#include <camera/NdkCameraDevice.h>
#include <camera/NdkCameraManager.h>
#include <camera/NdkCameraCaptureSession.h>
#include <media/NdkImageReader.h>
#include <android/native_window.h>
#elif defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>
#elif defined(__linux__)
// V4L2 headers are only needed in the .cpp; no platform handles leak
// into the public header on Linux.
#endif
// Web (Emscripten) needs no platform headers here either — everything
// lives behind getUserMedia()/the DOM in flux_camera_web.cpp.

// ============================================================================
// FluxCamera
// ============================================================================

class FluxCamera
{
public:
    // ── Singleton ─────────────────────────────────────────────────────────
    static FluxCamera &get();

    // ── State ─────────────────────────────────────────────────────────────
    enum class State
    {
        Closed,
        Opening,
        Previewing,
        Capturing,
        Error
    };

    State getState() const { return _state.load(); }
    bool isPreviewing() const { return _state == State::Previewing; }
    bool isCapturing() const { return _state == State::Capturing; }
    bool isFlashOn() const { return _flashOn.load(); }
#ifndef __EMSCRIPTEN__
    // Web overrides this below: there's no decode thread flipping
    // _newFrame, so it queries the <video> element's readyState directly
    // instead (see the Emscripten branch a little further down).
    bool hasNewFrame() const { return _newFrame.load(); }
#endif
    bool isFrontCamera() const;

    int getPreviewWidth() const { return _previewW.load(); }
    int getPreviewHeight() const { return _previewH.load(); }

    const std::string &getLastPhotoPath() const { return _lastPhotoPath; }

#if defined(__ANDROID__)
    // Android renders preview via an OES texture bound to a SurfaceTexture.
    GLuint getTextureId() const { return _oesTexture; }
#elif defined(__EMSCRIPTEN__)
    // Web: the browser owns capture + decode end-to-end (getUserMedia →
    // hidden <video> element). There's no CPU pixel buffer to lock, and
    // since FluxUI's web painter is Canvas2D rather than WebGL (the WebGL
    // canvas is reserved for CanvasWidget — see flux_painter_web.cpp /
    // flux_window_web.cpp), there's no texture to upload into either.
    // This mirrors FluxVideo's web surface exactly: hasNewFrame() +
    // renderFrame() blit the live <video> element straight into the 2D
    // canvas each tick. `mirror` lets the caller flip the image
    // horizontally for a front-camera "selfie" preview — the same
    // decision camera_widget_win32.cpp makes itself via isFrontCamera()
    // rather than something the engine imposes.
    bool hasNewFrame() const;
    void renderFrame(int dstX, int dstY, int dstW, int dstH,
                     bool mirror = false) const;
#else
    // Windows/Linux/macOS expose CPU-side BGRA32/RGB24 frame buffers.
    struct FrameLock
    {
        std::unique_lock<std::mutex> lock;
        const uint8_t *data = nullptr;
        int width = 0;
        int height = 0;
        int stride = 0; // bytes per row
    };
    FrameLock lockFrame();
#endif

    // ── Callbacks ─────────────────────────────────────────────────────────
    using PhotoCallback = std::function<void(const std::string &path)>;
    void setOnPhoto(PhotoCallback cb) { _onPhoto = std::move(cb); }

    // ── Flash ─────────────────────────────────────────────────────────────
    void setFlash(bool on);
    void toggleFlash() { setFlash(!_flashOn.load()); }

    // ── Lifecycle ─────────────────────────────────────────────────────────
    bool open(bool useFront = false);
    void flipCamera();
    bool updateFrame();
    void capturePhoto();
    void close();

private:
    FluxCamera() = default;
    ~FluxCamera() { close(); }

    void _applyFlash();

    // ── Shared state ──────────────────────────────────────────────────────
    std::atomic<State> _state{State::Closed};
    std::atomic<bool> _flashOn{false};
    std::atomic<bool> _pendingFrame{false};
    std::atomic<bool> _newFrame{false};
    std::atomic<bool> _captureRequested{false};
    std::atomic<bool> _stopThread{false};
    std::atomic<int> _previewW{0};
    std::atomic<int> _previewH{0};

    std::string _lastPhotoPath;
    PhotoCallback _onPhoto;

#if defined(__ANDROID__)
    // ── Android: NDK Camera2 objects ─────────────────────────────────────
    ACameraManager *_manager = nullptr;
    ACameraIdList *_cameraIdList = nullptr;
    ACameraDevice *_cameraDevice = nullptr;
    ACameraCaptureSession *_captureSession = nullptr;
    ACaptureRequest *_previewRequest = nullptr;
    ACaptureRequest *_captureRequest = nullptr;
    ACameraOutputTarget *_previewOutput = nullptr;
    ACameraOutputTarget *_captureOutput = nullptr;
    ACaptureSessionOutputContainer *_sessionOutputContainer = nullptr;
    ACaptureSessionOutput *_previewSessionOutput = nullptr;
    ACaptureSessionOutput *_captureSessionOutput = nullptr;
    AImageReader *_imageReader = nullptr;
    ANativeWindow *_imageReaderWindow = nullptr;
    ANativeWindow *_previewWindow = nullptr;

    GLuint _oesTexture = 0;
    void *_surfaceTexture = nullptr;

    std::atomic<bool> _useFront{false};
    std::atomic<int> _jpegW{4000};
    std::atomic<int> _jpegH{3000};

    std::string _selectedCameraId;

    ACameraDevice_StateCallbacks _deviceCallbacks{};
    ACameraCaptureSession_stateCallbacks _sessionCallbacks{};
    ACameraCaptureSession_captureCallbacks _repeatCallbacks{};  // repeating preview
    ACameraCaptureSession_captureCallbacks _captureCallbacks{}; // still capture
    AImageReader_ImageListener _imageListener{};

    bool _selectCamera(bool useFront);
    void _queryMaxJpegSize();
    bool _createOESTexture();
    bool _createImageReader();
    bool _openDevice();
    void _createSession();

    static void _onImageAvailable(void *ctx, AImageReader *reader);
    static void _onDeviceDisconnected(void *ctx, ACameraDevice *dev);
    static void _onDeviceError(void *ctx, ACameraDevice *dev, int err);
    static void _onSessionReady(void *ctx, ACameraCaptureSession *session);
    static void _onSessionActive(void *ctx, ACameraCaptureSession *session);
    static void _onSessionClosed(void *ctx, ACameraCaptureSession *session);
    static void _onPreviewFrameCompleted(void *ctx,
                                         ACameraCaptureSession *session,
                                         ACaptureRequest *request,
                                         const ACameraMetadata *result);
    static void _onCaptureCompleted(void *ctx,
                                    ACameraCaptureSession *session,
                                    ACaptureRequest *request,
                                    const ACameraMetadata *result);
    static void _onCaptureFailed(void *ctx,
                                 ACameraCaptureSession *session,
                                 ACaptureRequest *request,
                                 ACameraCaptureFailure *failure);

#elif defined(_WIN32)
    // ── Windows: Media Foundation objects ────────────────────────────────
    Microsoft::WRL::ComPtr<IMFMediaSource> _mediaSource;
    Microsoft::WRL::ComPtr<IMFSourceReader> _reader;
    bool _mfStarted = false;

    std::atomic<int> _cameraIndex{0};

    std::mutex _frameMutex;
    std::vector<uint8_t> _frameData; // BGRA32

    std::thread _readerThread;

    std::vector<std::wstring> _deviceNames;
    std::vector<std::wstring> _deviceSymLinks;

    bool _enumerateDevices();
    bool _openDevice(int index);
    bool _setOutputBGRA32();
    void _queryPreviewSize();
    void _readerLoop();
    void _saveJpegFromBGRA(const uint8_t *bgra, int w, int h);

#elif defined(__linux__)
    // ── Linux: V4L2 objects ───────────────────────────────────────────────
    int _fd = -1;
    int _deviceIndex = 0;
    bool _isMJPEG = false; // true = MJPEG, false = YUYV

    struct MmapBuffer
    {
        void *start = nullptr;
        size_t length = 0;
    };
    std::vector<MmapBuffer> _buffers;

    std::vector<std::string> _devices;

    std::mutex _frameMutex;
    std::vector<uint8_t> _frameData; // RGB24

    std::thread _captureThread;

    void _enumerateDevices();
    bool _openDevice(int index);
    bool _trySetFormat(uint32_t pixfmt);
    bool _initMmap();
    void _freeMmapBuffers();
    bool _startStreaming();
    void _stopStreaming();
    void _captureLoop();
    bool _decodeMJPEG(const uint8_t *data, size_t len, std::vector<uint8_t> &rgb);
    bool _decodeYUYV(const uint8_t *src, size_t len, std::vector<uint8_t> &rgb);
    void _saveJpeg(const std::vector<uint8_t> &rgb, int w, int h);

#elif defined(__APPLE__)
    // ── macOS: AVFoundation objects (opaque pointer to MacOSImpl) ────────
    void *_macosImpl = nullptr;

    std::atomic<int> _cameraIndex{0};
    std::mutex _frameMutex;

    bool _openDeviceAtIndex(int index);
    void _saveJpegFromBGRA(const uint8_t *bgra, int w, int h);

    // Pure C++ bridge struct — grants the macOS implementation file access
    // to private members without involving any ObjC class names here.
    friend struct FluxCameraMacOSAccess;

#elif defined(__EMSCRIPTEN__)
    std::atomic<bool> _useFront{false};

// Public so extern "C" thunks in flux_camera_web.cpp can reach them.
// Not part of the stable API — call only from flux_camera_web.cpp.
public:
    static void _onStreamReady(int width, int height);
    static void _onStreamError();
    static void _onCaptureError();
    static void _onPhotoBytes(const uint8_t *data, int len);

private:
#endif
};