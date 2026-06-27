// flux_mic_win32.cpp
// Windows WASAPI microphone backend for FluxMic.
//
// Link against: ole32  mmdevapi
//
#if defined(_WIN32)

#include "flux/flux_mic.hpp"

// Windows SDK headers must be included in this order:
//   windows.h first, then COM/audio headers that depend on its typedefs.
// functiondiscoverykeys_devpkey.h is intentionally omitted — it is not used
// by the capture path and requires a specific include-order preamble that
// conflicts with /permissive- builds.
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <wrl/client.h>

// ============================================================================
// Logging
// ============================================================================
#define MIC_LOGI(fmt, ...) \
    do { char _b[256]; snprintf(_b, 256, "[FluxMic] " fmt "\n", ##__VA_ARGS__); \
         OutputDebugStringA(_b); } while(0)
#define MIC_LOGE(fmt, ...) MIC_LOGI(fmt, ##__VA_ARGS__)

// ============================================================================
// Save-directory helper
// Default: %LOCALAPPDATA%\FluxMic  (auto-created).
// Define FLUXMIC_SAVE_DIR and provide your own FluxWin_getMicSaveDir() to override.
// ============================================================================
#ifndef FLUXMIC_SAVE_DIR
static std::string FluxWin_getMicSaveDir() {
    char path[MAX_PATH] = {};
    if (GetEnvironmentVariableA("LOCALAPPDATA", path, MAX_PATH) > 0) {
        std::string dir = std::string(path) + "\\FluxMic";
        CreateDirectoryA(dir.c_str(), nullptr);
        return dir;
    }
    return ".";
}
#endif

// ============================================================================
// FluxMic::Impl — WASAPI capture
// ============================================================================
struct FluxMic::Impl {
    // ── Ring buffer & meters ─────────────────────────────────────────────────
    std::vector<float>   ring;
    std::atomic<size_t>  writePos  { 0 };
    std::atomic<float>   amplitude { 0.f };

    // ── WASAPI handles ───────────────────────────────────────────────────────
    Microsoft::WRL::ComPtr<IMMDevice>           device;
    Microsoft::WRL::ComPtr<IAudioClient>        audioClient;
    Microsoft::WRL::ComPtr<IAudioCaptureClient> captureClient;
    WAVEFORMATEX*                               wfx             = nullptr;
    uint32_t                                    deviceSampleRate = FluxMic::kSampleRate;
    uint32_t                                    deviceChannels   = 1;

    // ── Control ──────────────────────────────────────────────────────────────
    std::thread        pollThread;
    std::atomic<bool>  stopPoll { false };
    std::atomic<State> state    { State::Idle };
    std::string        lastSavedPath;
    SaveCallback       onSaved;

    // ── Open ─────────────────────────────────────────────────────────────────
    bool openDevice() {
        Microsoft::WRL::ComPtr<IMMDeviceEnumerator> enumerator;
        HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                                      CLSCTX_ALL, IID_PPV_ARGS(&enumerator));
        if (FAILED(hr)) { MIC_LOGE("MMDeviceEnumerator: 0x%08X", hr); return false; }

        hr = enumerator->GetDefaultAudioEndpoint(eCapture, eCommunications, &device);
        if (FAILED(hr)) { MIC_LOGE("GetDefaultAudioEndpoint: 0x%08X", hr); return false; }

        hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                              reinterpret_cast<void**>(audioClient.GetAddressOf()));
        if (FAILED(hr)) { MIC_LOGE("Activate IAudioClient: 0x%08X", hr); return false; }

        hr = audioClient->GetMixFormat(&wfx);
        if (FAILED(hr)) { MIC_LOGE("GetMixFormat: 0x%08X", hr); return false; }

        deviceSampleRate = wfx->nSamplesPerSec;
        deviceChannels   = wfx->nChannels;
        MIC_LOGI("WASAPI mix format: %u Hz  %u ch  %u bps",
                 deviceSampleRate, deviceChannels, wfx->wBitsPerSample);

        // 100 ms buffer, shared mode, polling
        hr = audioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, 0,
                                     10'000'000, 0, wfx, nullptr);
        if (FAILED(hr)) { MIC_LOGE("AudioClient::Initialize: 0x%08X", hr); return false; }

        hr = audioClient->GetService(IID_PPV_ARGS(&captureClient));
        if (FAILED(hr)) { MIC_LOGE("GetService CaptureClient: 0x%08X", hr); return false; }

        hr = audioClient->Start();
        if (FAILED(hr)) { MIC_LOGE("AudioClient::Start: 0x%08X", hr); return false; }
        return true;
    }

    void closeDevice() {
        if (audioClient) audioClient->Stop();
        if (wfx) { CoTaskMemFree(wfx); wfx = nullptr; }
        captureClient.Reset();
        audioClient.Reset();
        device.Reset();
    }

    // ── Capture loop ─────────────────────────────────────────────────────────
    void captureLoop() {
        MIC_LOGI("Capture loop started (%u Hz, %u ch)", deviceSampleRate, deviceChannels);
        float rmsAccum   = 0.f;
        int   rmsSamples = 0;
        const int kRmsWindow = (int)deviceSampleRate / 20; // 50 ms window

        while (!stopPoll) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            if (!captureClient) break;

            UINT32 packetSize = 0;
            if (FAILED(captureClient->GetNextPacketSize(&packetSize))) break;

            while (packetSize > 0) {
                BYTE* data = nullptr; UINT32 frames = 0; DWORD flags = 0;
                if (FAILED(captureClient->GetBuffer(&data, &frames, &flags, nullptr, nullptr)))
                    break;

                if (!(flags & AUDCLNT_BUFFERFLAGS_SILENT) && frames > 0)
                    processFrames(reinterpret_cast<const float*>(data), frames,
                                  rmsAccum, rmsSamples, kRmsWindow);

                captureClient->ReleaseBuffer(frames);
                if (FAILED(captureClient->GetNextPacketSize(&packetSize))) packetSize = 0;
            }

            if (writePos.load() >= FluxMic::kRingSize) {
                MIC_LOGI("Max duration reached — stopping");
                stopPoll = true;
                state    = State::Stopping;
                break;
            }
        }

        MIC_LOGI("Capture loop exited — %zu samples (%.2f s)",
                 writePos.load(),
                 (float)writePos.load() / FluxMic::kSampleRate);
    }

    // ── Process a packet of interleaved frames ────────────────────────────────
    void processFrames(const float* rawData, UINT32 frames,
                       float& rmsAccum, int& rmsSamples, int kRmsWindow)
    {
        if (!wfx) return;
        size_t wp = writePos.load();
        size_t n  = std::min<size_t>(frames, FluxMic::kRingSize - wp);
        if (n == 0) return;

        uint32_t ch = deviceChannels;

        bool isFloat  = false;
        bool isPCM16  = false;
        bool isPCM32  = false;

        if (wfx->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) {
            isFloat = true;
        } else if (wfx->wFormatTag == WAVE_FORMAT_PCM) {
            isPCM16 = (wfx->wBitsPerSample == 16);
            isPCM32 = (wfx->wBitsPerSample == 32);
        } else if (wfx->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
            const WAVEFORMATEXTENSIBLE* wfex =
                reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(wfx);
            static const GUID kFloat =
                {0x00000003,0x0000,0x0010,{0x80,0x00,0x00,0xAA,0x00,0x38,0x9B,0x71}};
            static const GUID kPCM =
                {0x00000001,0x0000,0x0010,{0x80,0x00,0x00,0xAA,0x00,0x38,0x9B,0x71}};
            if      (IsEqualGUID(wfex->SubFormat, kFloat)) isFloat = (wfx->wBitsPerSample == 32);
            else if (IsEqualGUID(wfex->SubFormat, kPCM))  {
                isPCM16 = (wfx->wBitsPerSample == 16);
                isPCM32 = (wfx->wBitsPerSample == 32);
            }
        }

        const int16_t* r16 = reinterpret_cast<const int16_t*>(rawData);
        const int32_t* r32 = reinterpret_cast<const int32_t*>(rawData);

        for (size_t i = 0; i < n; i++) {
            float mono = 0.f;
            if      (isFloat)  { for (uint32_t c = 0; c < ch; c++) mono += rawData[i*ch+c]; mono /= ch; }
            else if (isPCM16)  { for (uint32_t c = 0; c < ch; c++) mono += r16[i*ch+c] / 32768.f; mono /= ch; }
            else if (isPCM32)  { for (uint32_t c = 0; c < ch; c++) mono += r32[i*ch+c] / (float)INT32_MAX; mono /= ch; }

            ring[wp + i]  = mono;
            rmsAccum     += mono * mono;
            rmsSamples++;
            if (rmsSamples >= kRmsWindow) {
                float rms = std::sqrt(rmsAccum / rmsSamples);
                amplitude.store(amplitude.load() * 0.7f + rms * 0.3f);
                rmsAccum   = 0.f;
                rmsSamples = 0;
            }
        }
        writePos.store(wp + n);
    }

    // ── Save ──────────────────────────────────────────────────────────────────
    void saveWAV() {
        size_t n = writePos.load();
        if (n == 0) { MIC_LOGE("Nothing captured — no WAV written"); return; }
        std::string path = WavWriter::makeTimestampedPath(FluxWin_getMicSaveDir());
        MIC_LOGI("Saving WAV: %s (%zu samples, %.2f s)",
                 path.c_str(), n, (float)n / FluxMic::kSampleRate);
        std::string result = WavWriter::write(path, ring.data(), n, deviceSampleRate, 1);
        if (!result.empty()) {
            lastSavedPath = result;
            if (onSaved) onSaved(result);
        } else {
            MIC_LOGE("WAV write FAILED: %s", path.c_str());
        }
    }
};

// ============================================================================
// FluxMic public API
// ============================================================================

FluxMic& FluxMic::get() { static FluxMic inst; return inst; }

FluxMic::FluxMic()  : m_impl(new Impl()) { m_impl->ring.reserve(kRingSize); }
FluxMic::~FluxMic() { cancel(); delete m_impl; }

FluxMic::State FluxMic::getState()     const { return m_impl->state.load(); }
bool           FluxMic::isRecording()  const { return m_impl->state == State::Recording; }
float          FluxMic::getAmplitude() const { return m_impl->amplitude.load(); }
float          FluxMic::getProgress()  const {
    return std::min(1.f, (float)m_impl->writePos.load() / (float)kRingSize);
}
float  FluxMic::getElapsedSeconds() const {
    return (float)m_impl->writePos.load() / (float)kSampleRate;
}
const std::string& FluxMic::getLastSavedPath() const { return m_impl->lastSavedPath; }
void FluxMic::setOnSaved(SaveCallback cb) { m_impl->onSaved = std::move(cb); }

bool FluxMic::start() {
    if (m_impl->state == State::Recording) return true;
    if (m_impl->state == State::Stopping)  return false;

    m_impl->ring.assign(kRingSize, 0.f);
    m_impl->writePos  = 0;
    m_impl->amplitude = 0.f;

    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    if (!m_impl->openDevice()) { m_impl->state = State::Error; return false; }

    m_impl->state    = State::Recording;
    m_impl->stopPoll = false;
    m_impl->pollThread = std::thread(&Impl::captureLoop, m_impl);
    MIC_LOGI("Recording started — max %d s", kMaxSeconds);
    return true;
}

void FluxMic::stop() {
    if (m_impl->state != State::Recording) return;
    m_impl->state    = State::Stopping;
    m_impl->stopPoll = true;
    if (m_impl->pollThread.joinable()) m_impl->pollThread.join();
    m_impl->closeDevice();
    m_impl->saveWAV();
    m_impl->state = State::Idle;
}

void FluxMic::cancel() {
    if (m_impl->state != State::Recording) return;
    m_impl->state    = State::Stopping;
    m_impl->stopPoll = true;
    if (m_impl->pollThread.joinable()) m_impl->pollThread.join();
    m_impl->closeDevice();
    m_impl->writePos  = 0;
    m_impl->amplitude = 0.f;
    m_impl->state     = State::Idle;
    MIC_LOGI("Recording cancelled");
}

void FluxMic::getWaveform(std::vector<float>& out, size_t count) const {
    FluxMicDetail::buildWaveform(m_impl->ring, m_impl->writePos.load(), out, count);
}

#endif // _WIN32