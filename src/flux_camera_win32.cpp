// flux_camera_win32.cpp
// Windows Media Foundation camera capture engine for FluxUI.
//
// Pipeline:
//   IMFMediaSource → IMFSourceReader (preview frames as BGRA32)
//       └── Preview  → CPU copy → lockFrame() → GDI StretchDIBits
//       └── Capture  → IMFSample → WIC JPEG encode → save to Pictures folder

#ifdef _WIN32

#include "flux/flux_camera.hpp"

#include <mfapi.h>
#include <mferror.h>
#include <mftransform.h>
#include <wmcodecdsp.h>
#include <combaseapi.h>
#include <wincodec.h>
#include <shlobj.h>

#include <chrono>
#include <cstdio>
#include <ctime>

using Microsoft::WRL::ComPtr;

#define CAM_LOGI(...) do { char _buf[512]; snprintf(_buf,sizeof(_buf),__VA_ARGS__); \
    OutputDebugStringA("[FluxCamera] "); OutputDebugStringA(_buf); OutputDebugStringA("\n"); } while(0)
#define CAM_LOGE(...) do { char _buf[512]; snprintf(_buf,sizeof(_buf),__VA_ARGS__); \
    OutputDebugStringA("[FluxCamera][ERR] "); OutputDebugStringA(_buf); OutputDebugStringA("\n"); } while(0)

// Trigger a repaint from within FluxCamera callbacks (e.g. after capture).
// Implement in your FluxUI Windows host (or leave as a no-op stub).
void FluxWin_markNeedsPaint() {}

// Save raw JPEG bytes to the user's Pictures folder, return the full path.
static std::string FluxWin_saveJpegToPictures(const std::string& filename,
                                               const uint8_t* data,
                                               size_t length);

// ============================================================================
// Singleton
// ============================================================================

FluxCamera& FluxCamera::get() {
    static FluxCamera instance;
    return instance;
}

bool FluxCamera::isFrontCamera() const { return _cameraIndex.load() > 0; }

// ── Frame access — call after updateFrame() returns true ───────────────────
FluxCamera::FrameLock FluxCamera::lockFrame() {
    FrameLock fl;
    fl.lock   = std::unique_lock<std::mutex>(_frameMutex);
    fl.data   = _frameData.empty() ? nullptr : _frameData.data();
    fl.width  = _previewW.load();
    fl.height = _previewH.load();
    fl.stride = fl.width * 4;
    return fl;
}

// ── Flash ────────────────────────────────────────────────────────────────
void FluxCamera::setFlash(bool on) { _flashOn = on; }
void FluxCamera::_applyFlash() {}

// =============================================================================
// OPEN — enumerate cameras, start preview thread
// =============================================================================

bool FluxCamera::open(bool useFront) {
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

// =============================================================================
// FLIP — toggle between camera 0 and camera 1
// =============================================================================

void FluxCamera::flipCamera() {
    int next = (_cameraIndex.load() == 0 && (int)_deviceNames.size() > 1) ? 1 : 0;
    close();
    open(next != 0);
}

// =============================================================================
// UPDATE FRAME — call once per render tick.
// Returns true if a new frame is available; call lockFrame() to read pixels.
// =============================================================================

bool FluxCamera::updateFrame() {
    if (!_pendingFrame.load()) { _newFrame = false; return false; }
    _pendingFrame = false;
    _newFrame     = true;
    return true;
}

// =============================================================================
// CAPTURE PHOTO
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

// =============================================================================
// ENUMERATE VIDEO CAPTURE DEVICES
// =============================================================================

bool FluxCamera::_enumerateDevices() {
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

// =============================================================================
// OPEN DEVICE + CREATE SOURCE READER
// =============================================================================

bool FluxCamera::_openDevice(int index) {
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

bool FluxCamera::_setOutputBGRA32() {
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

void FluxCamera::_queryPreviewSize() {
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

// =============================================================================
// READER LOOP — background thread
// Calls IMFSourceReader::ReadSample synchronously, copies BGRA frames into
// _frameData under _frameMutex, sets _pendingFrame for the UI thread.
// =============================================================================

void FluxCamera::_readerLoop() {
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

// =============================================================================
// JPEG ENCODE + SAVE via WIC
// =============================================================================

void FluxCamera::_saveJpegFromBGRA(const uint8_t* bgra, int w, int h) {
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