// flux_camera_linux.cpp
// V4L2 camera capture engine for FluxUI on Linux.
//
// Pipeline:
//   V4L2 (/dev/videoN) → MJPEG or YUYV → libjpeg / software YUV→RGB24 →
//       CPU frame buffer → lockFrame() → Cairo StretchBlit → SDL window

#if defined(__linux__) && !defined(__ANDROID__)

#include "flux/flux_camera.hpp"

#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <filesystem>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

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
void FluxWin_markNeedsPaint() {}

// ============================================================================
// Singleton
// ============================================================================

FluxCamera& FluxCamera::get() {
    static FluxCamera instance;
    return instance;
}

bool FluxCamera::isFrontCamera() const { return false; } // V4L2 has no facing concept

// ── Frame access — call after hasNewFrame() returns true ───────────────────
FluxCamera::FrameLock FluxCamera::lockFrame() {
    FrameLock fl;
    fl.lock   = std::unique_lock<std::mutex>(_frameMutex);
    fl.data   = _frameData.empty() ? nullptr : _frameData.data();
    fl.width  = _previewW.load();
    fl.height = _previewH.load();
    fl.stride = fl.width * 3;
    _newFrame = false;
    return fl;
}

// ── Flash — no-op on most webcams; kept for widget API compatibility ───────
void FluxCamera::setFlash(bool on) { _flashOn = on; }
void FluxCamera::_applyFlash() {}

// =============================================================================
// OPEN — enumerate /dev/videoN, open first usable, start capture thread
// =============================================================================

bool FluxCamera::open(bool /*useFront*/) {
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

// =============================================================================
// FLIP — cycle to next /dev/videoN
// =============================================================================

void FluxCamera::flipCamera() {
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

// =============================================================================
// UPDATE FRAME — call once per render tick on the paint thread.
// Returns true when a new frame is ready; call lockFrame() to read pixels.
// =============================================================================

bool FluxCamera::updateFrame() {
    if (!_pendingFrame.load()) { _newFrame = false; return false; }
    _pendingFrame = false;
    _newFrame     = true;
    return true;
}

// =============================================================================
// CAPTURE PHOTO — saves the current frame as JPEG to ~/Pictures/FluxCam/
// =============================================================================

void FluxCamera::capturePhoto() {
    if (_state != State::Previewing) return;
    _state = State::Capturing;
    _captureRequested = true;
    CAM_LOGI("Capture requested");
}

// =============================================================================
// CLOSE
// =============================================================================

void FluxCamera::close() {
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

// =============================================================================
// DEVICE ENUMERATION
// =============================================================================

void FluxCamera::_enumerateDevices() {
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

// =============================================================================
// OPEN DEVICE + NEGOTIATE FORMAT
// =============================================================================

bool FluxCamera::_openDevice(int index) {
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

bool FluxCamera::_trySetFormat(uint32_t pixfmt) {
    v4l2_format fmt{};
    fmt.type                = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width       = 1280;
    fmt.fmt.pix.height      = 720;
    fmt.fmt.pix.pixelformat = pixfmt;
    fmt.fmt.pix.field       = V4L2_FIELD_ANY;

    if (ioctl(_fd, VIDIOC_S_FMT, &fmt) < 0) return false;
    return fmt.fmt.pix.pixelformat == pixfmt;
}

// =============================================================================
// MMAP BUFFER MANAGEMENT
// =============================================================================

bool FluxCamera::_initMmap() {
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

void FluxCamera::_freeMmapBuffers() {
    for (auto& b : _buffers) {
        if (b.start && b.start != MAP_FAILED)
            munmap(b.start, b.length);
    }
    _buffers.clear();
}

bool FluxCamera::_startStreaming() {
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

void FluxCamera::_stopStreaming() {
    if (_fd < 0) return;
    v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ioctl(_fd, VIDIOC_STREAMOFF, &type);
}

// =============================================================================
// CAPTURE LOOP — background thread
// =============================================================================

void FluxCamera::_captureLoop() {
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

// =============================================================================
// MJPEG DECODE — libjpeg → RGB24
// =============================================================================

namespace {

// Custom libjpeg error handler that longjmps instead of exit()
struct JpegErrorMgr {
    jpeg_error_mgr pub;
    jmp_buf        jmpBuf;
};

void jpegErrorExit(j_common_ptr cinfo) {
    auto* mgr = reinterpret_cast<JpegErrorMgr*>(cinfo->err);
    longjmp(mgr->jmpBuf, 1);
}

} // namespace

bool FluxCamera::_decodeMJPEG(const uint8_t* data, size_t len,
                  std::vector<uint8_t>& rgb) {
    jpeg_decompress_struct cinfo{};
    JpegErrorMgr           jerr{};

    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = jpegErrorExit;

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

// =============================================================================
// YUYV DECODE — software YUV422 → RGB24
// Clamp helper avoids per-pixel branching in the inner loop.
// =============================================================================

bool FluxCamera::_decodeYUYV(const uint8_t* src, size_t /*len*/,
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

// =============================================================================
// JPEG SAVE — encode RGB24 → JPEG via libjpeg, write to ~/Pictures/FluxCam
// =============================================================================

void FluxCamera::_saveJpeg(const std::vector<uint8_t>& rgb, int w, int h) {
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

#endif // __linux__ && !__ANDROID__