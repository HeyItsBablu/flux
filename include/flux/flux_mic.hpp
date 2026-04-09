// flux_mic.hpp
// Microphone capture engine for FluxUI.
//
// Platform backends:
//   Android : AAudio  (AAUDIO_INPUT_PRESET_VOICE_RECOGNITION, exclusive, float32)
//   Windows : WASAPI  (shared mode, auto format-detect, mono mix-down)
//   Linux   : ALSA    (SND_PCM_FORMAT_FLOAT_LE → S16_LE fallback, default device)
//
// Common pipeline (all platforms):
//   Audio input  →  mono float32 ring buffer (5-min cap, 48 kHz)
//               →  RMS amplitude tracker   (for waveform widget)
//               →  WAV file writer          (timestamped, on stop)
//
// Usage:
//   FluxMic::get().start();            // begin capture
//   FluxMic::get().stop();             // stop + save WAV
//   FluxMic::get().getAmplitude();     // current RMS (0.0–1.0)
//   FluxMic::get().getWaveform(v, 80); // fill vector with 80 RMS bars
//
// CMake linkage required:
//   Android : aaudio
//   Windows : ole32  mmdevapi
//   Linux   : asound
//
#pragma once

// ============================================================================
// Common includes & WAV helpers  (compiled on every platform)
// ============================================================================
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace WavWriter {

#pragma pack(push, 1)
struct Header {
    char     riff[4]       = {'R','I','F','F'};
    uint32_t chunkSize     = 0;
    char     wave[4]       = {'W','A','V','E'};
    char     fmt[4]        = {'f','m','t',' '};
    uint32_t fmtSize       = 16;
    uint16_t audioFormat   = 1;     // PCM int16 — plays everywhere
    uint16_t numChannels   = 1;
    uint32_t sampleRate    = 48000;
    uint32_t byteRate      = 0;
    uint16_t blockAlign    = 2;
    uint16_t bitsPerSample = 16;
    char     data[4]       = {'d','a','t','a'};
    uint32_t dataSize      = 0;
};
#pragma pack(pop)

inline std::string write(const std::string& path,
                         const float*       samples,
                         size_t             numSamples,
                         uint32_t           sampleRate  = 48000,
                         uint16_t           numChannels = 1)
{
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return "";

    Header hdr;
    hdr.numChannels  = numChannels;
    hdr.sampleRate   = sampleRate;
    hdr.blockAlign   = numChannels * 2;
    hdr.byteRate     = sampleRate * hdr.blockAlign;

    uint32_t dataBytes = static_cast<uint32_t>(numSamples * 2);
    hdr.dataSize  = dataBytes;
    hdr.chunkSize = 4 + 8 + 16 + 8 + dataBytes;

    fwrite(&hdr, sizeof(hdr), 1, f);

    std::vector<int16_t> pcm16(numSamples);
    for (size_t i = 0; i < numSamples; i++) {
        float c = std::max(-1.f, std::min(1.f, samples[i]));
        pcm16[i] = static_cast<int16_t>(c * 32767.f);
    }
    fwrite(pcm16.data(), sizeof(int16_t), numSamples, f);
    fclose(f);
    return path;
}

// Platform-correct path separator
inline std::string makeTimestampedPath(const std::string& dir) {
    std::time_t t  = std::time(nullptr);
    std::tm*    tm = std::localtime(&t);
    char buf[64];
    std::strftime(buf, sizeof(buf), "rec_%Y%m%d_%H%M%S.wav", tm);
#ifdef _WIN32
    return dir + "\\" + buf;
#else
    return dir + "/" + buf;
#endif
}

} // namespace WavWriter


// ============================================================================
// Shared waveform helper — identical logic on all three platforms
// ============================================================================
namespace FluxMicDetail {

inline void buildWaveform(const std::vector<float>& ring,
                          size_t                    written,
                          std::vector<float>&        out,
                          size_t                     count)
{
    out.resize(count, 0.f);
    if (written == 0 || count == 0) return;

    size_t windowSize = std::max<size_t>(64, written / count);
    size_t start      = (written > count * windowSize)
                        ? written - count * windowSize : 0;

    for (size_t i = 0; i < count; i++) {
        size_t wStart = start + i * windowSize;
        size_t wEnd   = std::min(wStart + windowSize, written);
        float  sum    = 0.f;
        for (size_t j = wStart; j < wEnd; j++)
            sum += ring[j] * ring[j];
        out[i] = (wEnd > wStart) ? std::sqrt(sum / (wEnd - wStart)) : 0.f;
    }
}

} // namespace FluxMicDetail


// ============================================================================
// Windows — WASAPI backend
// ============================================================================
#ifdef _WIN32

#include <windows.h>
#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>

#undef  MIC_LOGI
#undef  MIC_LOGE
#define MIC_LOGI(fmt, ...) \
    do { char _b[256]; snprintf(_b,256,"[FluxMic] " fmt "\n",##__VA_ARGS__); \
         OutputDebugStringA(_b); } while(0)
#define MIC_LOGE(fmt, ...) MIC_LOGI(fmt, ##__VA_ARGS__)

// Returns directory for saved WAV files on Windows.
// Default: %LOCALAPPDATA%\FluxMic  (auto-created).
// Define FLUXMIC_SAVE_DIR and provide your own FluxWin_getMicSaveDir() to override.
#ifndef FLUXMIC_SAVE_DIR
inline std::string FluxWin_getMicSaveDir() {
    char path[MAX_PATH] = {};
    if (GetEnvironmentVariableA("LOCALAPPDATA", path, MAX_PATH) > 0) {
        std::string dir = std::string(path) + "\\FluxMic";
        CreateDirectoryA(dir.c_str(), nullptr);
        return dir;
    }
    return ".";
}
#endif

class FluxMic {
public:
    static FluxMic& get() { static FluxMic inst; return inst; }

    static constexpr int    kSampleRate = 48000;
    static constexpr int    kChannels   = 1;
    static constexpr int    kMaxSeconds = 300;
    static constexpr size_t kRingSize   = kSampleRate * kMaxSeconds;

    enum class State { Idle, Recording, Stopping, Error };

    State  getState()          const { return s_state.load(); }
    bool   isRecording()       const { return s_state == State::Recording; }
    float  getAmplitude()      const { return s_amplitude.load(); }
    float  getProgress()       const {
        return std::min(1.f, (float)s_writePos.load() / (float)kRingSize);
    }
    float  getElapsedSeconds() const {
        return (float)s_writePos.load() / (float)kSampleRate;
    }
    const std::string& getLastSavedPath() const { return s_lastSavedPath; }

    using SaveCallback = std::function<void(const std::string&)>;
    void setOnSaved(SaveCallback cb) { s_onSaved = std::move(cb); }

    bool start() {
        if (s_state == State::Recording) return true;
        if (s_state == State::Stopping)  return false;

        s_ring.assign(kRingSize, 0.f);
        s_writePos = 0; s_amplitude = 0.f;

        CoInitializeEx(nullptr, COINIT_MULTITHREADED);

        if (!_openDevice()) { s_state = State::Error; return false; }

        s_state = State::Recording; s_stopPoll = false;
        s_pollThread = std::thread(&FluxMic::_captureLoop, this);
        MIC_LOGI("Recording started — max %d s", kMaxSeconds);
        return true;
    }

    void stop() {
        if (s_state != State::Recording) return;
        s_state = State::Stopping; s_stopPoll = true;
        if (s_pollThread.joinable()) s_pollThread.join();
        _closeDevice(); _saveWAV();
        s_state = State::Idle;
    }

    void cancel() {
        if (s_state != State::Recording) return;
        s_state = State::Stopping; s_stopPoll = true;
        if (s_pollThread.joinable()) s_pollThread.join();
        _closeDevice();
        s_writePos = 0; s_amplitude = 0.f;
        s_state = State::Idle;
        MIC_LOGI("Recording cancelled");
    }

    void getWaveform(std::vector<float>& out, size_t count) const {
        FluxMicDetail::buildWaveform(s_ring, s_writePos.load(), out, count);
    }

private:
    FluxMic()  { s_ring.reserve(kRingSize); }
    ~FluxMic() { cancel(); }

    std::vector<float>   s_ring;
    std::atomic<size_t>  s_writePos  { 0 };
    std::atomic<float>   s_amplitude { 0.f };

    Microsoft::WRL::ComPtr<IMMDevice>           s_device;
    Microsoft::WRL::ComPtr<IAudioClient>        s_audioClient;
    Microsoft::WRL::ComPtr<IAudioCaptureClient> s_captureClient;
    WAVEFORMATEX*                               s_wfx = nullptr;

    uint32_t s_deviceSampleRate = kSampleRate;
    uint32_t s_deviceChannels   = 1;

    std::thread        s_pollThread;
    std::atomic<bool>  s_stopPoll { false };
    std::atomic<State> s_state    { State::Idle };
    std::string        s_lastSavedPath;
    SaveCallback       s_onSaved;

    bool _openDevice() {
        Microsoft::WRL::ComPtr<IMMDeviceEnumerator> enumerator;
        HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                                      CLSCTX_ALL, IID_PPV_ARGS(&enumerator));
        if (FAILED(hr)) { MIC_LOGE("MMDeviceEnumerator: 0x%08X", hr); return false; }

        hr = enumerator->GetDefaultAudioEndpoint(eCapture, eCommunications, &s_device);
        if (FAILED(hr)) { MIC_LOGE("GetDefaultAudioEndpoint: 0x%08X", hr); return false; }

        hr = s_device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                reinterpret_cast<void**>(s_audioClient.GetAddressOf()));
        if (FAILED(hr)) { MIC_LOGE("Activate IAudioClient: 0x%08X", hr); return false; }

        hr = s_audioClient->GetMixFormat(&s_wfx);
        if (FAILED(hr)) { MIC_LOGE("GetMixFormat: 0x%08X", hr); return false; }

        s_deviceSampleRate = s_wfx->nSamplesPerSec;
        s_deviceChannels   = s_wfx->nChannels;
        MIC_LOGI("WASAPI mix format: %u Hz  %u ch  %u bps",
                 s_deviceSampleRate, s_deviceChannels, s_wfx->wBitsPerSample);

        // 100ms buffer, shared mode, polling (no event handle needed)
        hr = s_audioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, 0,
                                       10'000'000, 0, s_wfx, nullptr);
        if (FAILED(hr)) { MIC_LOGE("AudioClient::Initialize: 0x%08X", hr); return false; }

        hr = s_audioClient->GetService(IID_PPV_ARGS(&s_captureClient));
        if (FAILED(hr)) { MIC_LOGE("GetService CaptureClient: 0x%08X", hr); return false; }

        hr = s_audioClient->Start();
        if (FAILED(hr)) { MIC_LOGE("AudioClient::Start: 0x%08X", hr); return false; }
        return true;
    }

    void _closeDevice() {
        if (s_audioClient) s_audioClient->Stop();
        if (s_wfx) { CoTaskMemFree(s_wfx); s_wfx = nullptr; }
        s_captureClient.Reset(); s_audioClient.Reset(); s_device.Reset();
    }

    void _captureLoop() {
        MIC_LOGI("Capture loop started (%u Hz, %u ch)", s_deviceSampleRate, s_deviceChannels);
        float rmsAccum = 0.f; int rmsSamples = 0;
        const int kRmsWindow = (int)s_deviceSampleRate / 20; // 50ms window

        while (!s_stopPoll) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            if (!s_captureClient) break;

            UINT32 packetSize = 0;
            if (FAILED(s_captureClient->GetNextPacketSize(&packetSize))) break;

            while (packetSize > 0) {
                BYTE* data = nullptr; UINT32 frames = 0; DWORD flags = 0;
                if (FAILED(s_captureClient->GetBuffer(&data, &frames, &flags, nullptr, nullptr)))
                    break;
                if (!(flags & AUDCLNT_BUFFERFLAGS_SILENT) && frames > 0)
                    _processFrames(reinterpret_cast<const float*>(data), frames,
                                   rmsAccum, rmsSamples, kRmsWindow);
                s_captureClient->ReleaseBuffer(frames);
                if (FAILED(s_captureClient->GetNextPacketSize(&packetSize))) packetSize = 0;
            }

            if (s_writePos.load() >= kRingSize) {
                MIC_LOGI("Max duration reached — stopping");
                s_stopPoll = true; s_state = State::Stopping; break;
            }
        }
        MIC_LOGI("Capture loop exited — %zu samples (%.2f s)",
                 s_writePos.load(), (float)s_writePos.load() / kSampleRate);
    }

    void _processFrames(const float* rawData, UINT32 frames,
                        float& rmsAccum, int& rmsSamples, int kRmsWindow)
    {
        if (!s_wfx) return;
        size_t wp = s_writePos.load();
        size_t n  = std::min<size_t>(frames, kRingSize - wp);
        if (n == 0) return;

        uint32_t ch = s_deviceChannels;
        bool isFloat = false, isPCM16 = false, isPCM32 = false;

        if (s_wfx->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) {
            isFloat = true;
        } else if (s_wfx->wFormatTag == WAVE_FORMAT_PCM) {
            isPCM16 = (s_wfx->wBitsPerSample == 16);
            isPCM32 = (s_wfx->wBitsPerSample == 32);
        } else if (s_wfx->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
            const WAVEFORMATEXTENSIBLE* wfex =
                reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(s_wfx);
            static const GUID kFloat =
                {0x00000003,0x0000,0x0010,{0x80,0x00,0x00,0xAA,0x00,0x38,0x9B,0x71}};
            static const GUID kPCM =
                {0x00000001,0x0000,0x0010,{0x80,0x00,0x00,0xAA,0x00,0x38,0x9B,0x71}};
            if (IsEqualGUID(wfex->SubFormat, kFloat))  isFloat = (s_wfx->wBitsPerSample == 32);
            else if (IsEqualGUID(wfex->SubFormat, kPCM)) {
                isPCM16 = (s_wfx->wBitsPerSample == 16);
                isPCM32 = (s_wfx->wBitsPerSample == 32);
            }
        }

        const int16_t* r16 = reinterpret_cast<const int16_t*>(rawData);
        const int32_t* r32 = reinterpret_cast<const int32_t*>(rawData);

        for (size_t i = 0; i < n; i++) {
            float mono = 0.f;
            if (isFloat)      { for (uint32_t c=0;c<ch;c++) mono += rawData[i*ch+c]; mono /= ch; }
            else if (isPCM16) { for (uint32_t c=0;c<ch;c++) mono += r16[i*ch+c]/32768.f; mono /= ch; }
            else if (isPCM32) { for (uint32_t c=0;c<ch;c++) mono += r32[i*ch+c]/(float)INT32_MAX; mono /= ch; }
            s_ring[wp + i] = mono;
            rmsAccum += mono * mono; rmsSamples++;
            if (rmsSamples >= kRmsWindow) {
                float rms = std::sqrt(rmsAccum / rmsSamples);
                s_amplitude.store(s_amplitude.load() * 0.7f + rms * 0.3f);
                rmsAccum = 0.f; rmsSamples = 0;
            }
        }
        s_writePos.store(wp + n);
    }

    void _saveWAV() {
        size_t n = s_writePos.load();
        if (n == 0) { MIC_LOGE("Nothing captured — no WAV written"); return; }
        std::string path = WavWriter::makeTimestampedPath(FluxWin_getMicSaveDir());
        MIC_LOGI("Saving WAV: %s (%zu samples, %.2f s)", path.c_str(), n, (float)n/kSampleRate);
        std::string result = WavWriter::write(path, s_ring.data(), n, s_deviceSampleRate, 1);
        if (!result.empty()) { s_lastSavedPath = result; if (s_onSaved) s_onSaved(result); }
        else MIC_LOGE("WAV write FAILED: %s", path.c_str());
    }
};

#endif // _WIN32


// ============================================================================
// Linux — ALSA backend
// ============================================================================
#if defined(__linux__) && !defined(__ANDROID__)

#include <alsa/asoundlib.h>
#include <pwd.h>      // getpwuid
#include <sys/stat.h> // mkdir

#undef  MIC_LOGI
#undef  MIC_LOGE
#define MIC_LOGI(fmt, ...) fprintf(stderr, "[FluxMic] " fmt "\n", ##__VA_ARGS__)
#define MIC_LOGE(fmt, ...) MIC_LOGI(fmt, ##__VA_ARGS__)

// Returns directory for saved WAV files on Linux.
// Default: $HOME/FluxMic  (auto-created).
// Define FLUXMIC_SAVE_DIR and provide your own FluxLinux_getMicSaveDir() to override.
#ifndef FLUXMIC_SAVE_DIR
inline std::string FluxLinux_getMicSaveDir() {
    const char* home = getenv("HOME");
    if (!home) {
        struct passwd* pw = getpwuid(getuid());
        home = pw ? pw->pw_dir : nullptr;
    }
    std::string dir = home ? (std::string(home) + "/FluxMic") : "./FluxMic";
    mkdir(dir.c_str(), 0755); // no-op if already exists
    return dir;
}
#endif

class FluxMic {
public:
    static FluxMic& get() { static FluxMic inst; return inst; }

    static constexpr int    kSampleRate = 48000;
    static constexpr int    kChannels   = 1;
    static constexpr int    kMaxSeconds = 300;
    static constexpr size_t kRingSize   = kSampleRate * kMaxSeconds;

    // 10ms period at 48 kHz — mirrors Android chunk size for consistency
    static constexpr snd_pcm_uframes_t kPeriodFrames = 480;

    enum class State { Idle, Recording, Stopping, Error };

    State  getState()          const { return s_state.load(); }
    bool   isRecording()       const { return s_state == State::Recording; }
    float  getAmplitude()      const { return s_amplitude.load(); }
    float  getProgress()       const {
        return std::min(1.f, (float)s_writePos.load() / (float)kRingSize);
    }
    float  getElapsedSeconds() const {
        return (float)s_writePos.load() / (float)kSampleRate;
    }
    const std::string& getLastSavedPath() const { return s_lastSavedPath; }

    using SaveCallback = std::function<void(const std::string&)>;
    void setOnSaved(SaveCallback cb) { s_onSaved = std::move(cb); }

    // ── Start ─────────────────────────────────────────────────────────────────
    bool start() {
        if (s_state == State::Recording) return true;
        if (s_state == State::Stopping)  return false;

        s_ring.assign(kRingSize, 0.f);
        s_writePos = 0; s_amplitude = 0.f;

        if (!_openDevice()) { s_state = State::Error; return false; }

        s_state = State::Recording; s_stopPoll = false;
        s_pollThread = std::thread(&FluxMic::_captureLoop, this);
        MIC_LOGI("Recording started — max %d s", kMaxSeconds);
        return true;
    }

    // ── Stop + save ───────────────────────────────────────────────────────────
    void stop() {
        if (s_state != State::Recording) return;
        s_state = State::Stopping; s_stopPoll = true;
        if (s_pollThread.joinable()) s_pollThread.join();
        _closeDevice(); _saveWAV();
        s_state = State::Idle;
    }

    // ── Cancel (discard) ──────────────────────────────────────────────────────
    void cancel() {
        if (s_state != State::Recording) return;
        s_state = State::Stopping; s_stopPoll = true;
        if (s_pollThread.joinable()) s_pollThread.join();
        _closeDevice();
        s_writePos = 0; s_amplitude = 0.f;
        s_state = State::Idle;
        MIC_LOGI("Recording cancelled");
    }

    void getWaveform(std::vector<float>& out, size_t count) const {
        FluxMicDetail::buildWaveform(s_ring, s_writePos.load(), out, count);
    }

private:
    FluxMic()  { s_ring.reserve(kRingSize); }
    ~FluxMic() { cancel(); }

    // ── State ─────────────────────────────────────────────────────────────────
    std::vector<float>   s_ring;
    std::atomic<size_t>  s_writePos  { 0 };
    std::atomic<float>   s_amplitude { 0.f };

    snd_pcm_t*           s_pcm             = nullptr;
    uint32_t             s_deviceSampleRate = kSampleRate;
    uint32_t             s_deviceChannels   = 1;
    snd_pcm_uframes_t    s_periodActual     = kPeriodFrames;
    bool                 s_useFloat         = true; // FLOAT_LE vs S16_LE

    std::thread          s_pollThread;
    std::atomic<bool>    s_stopPoll { false };
    std::atomic<State>   s_state    { State::Idle };
    std::string          s_lastSavedPath;
    SaveCallback         s_onSaved;

    // ── Open ALSA capture device ───────────────────────────────────────────────
    //
    // Device priority:
    //   "default" — honours ~/.asoundrc and PulseAudio/PipeWire ALSA plugins,
    //               routing automatically through whatever sound server is active.
    //               This is the right choice for desktop Linux in 2024+.
    //   "hw:0,0"  — bare ALSA fallback for embedded / headless systems.
    //               Triggered automatically if "default" fails.
    //   FLUXMIC_ALSA_DEVICE — compile-time override for custom hardware.
    //
    bool _openDevice() {
#ifdef FLUXMIC_ALSA_DEVICE
        const char* device = FLUXMIC_ALSA_DEVICE;
        int err = snd_pcm_open(&s_pcm, device, SND_PCM_STREAM_CAPTURE, 0);
        if (err < 0) {
            MIC_LOGE("snd_pcm_open(\"%s\") failed: %s", device, snd_strerror(err));
            s_pcm = nullptr; return false;
        }
#else
        int err = snd_pcm_open(&s_pcm, "default", SND_PCM_STREAM_CAPTURE, 0);
        if (err < 0) {
            // "default" may not exist on minimal systems — fall back to hw:0,0
            MIC_LOGI("snd_pcm_open(\"default\") failed (%s) — retrying hw:0,0",
                     snd_strerror(err));
            err = snd_pcm_open(&s_pcm, "hw:0,0", SND_PCM_STREAM_CAPTURE, 0);
            if (err < 0) {
                MIC_LOGE("snd_pcm_open(\"hw:0,0\") also failed: %s", snd_strerror(err));
                s_pcm = nullptr; return false;
            }
        }
#endif

        // ── Hardware params ───────────────────────────────────────────────────
        snd_pcm_hw_params_t* hw = nullptr;
        snd_pcm_hw_params_alloca(&hw);
        snd_pcm_hw_params_any(s_pcm, hw);

        snd_pcm_hw_params_set_access(s_pcm, hw, SND_PCM_ACCESS_RW_INTERLEAVED);


        s_useFloat = true;
        err = snd_pcm_hw_params_set_format(s_pcm, hw, SND_PCM_FORMAT_FLOAT_LE);
        if (err < 0) {
            MIC_LOGI("FLOAT_LE not supported, falling back to S16_LE");
            s_useFloat = false;
            err = snd_pcm_hw_params_set_format(s_pcm, hw, SND_PCM_FORMAT_S16_LE);
            if (err < 0) {
                MIC_LOGE("S16_LE also unsupported: %s", snd_strerror(err));
                _closeDevice(); return false;
            }
        }

        // Mono — most microphones are mono at the driver level.
        // If the device doesn't support mono, snd_pcm_hw_params_set_channels_near
        // will round up to stereo and we mix down in the capture loop.
        unsigned int chans = 1;
        snd_pcm_hw_params_set_channels_near(s_pcm, hw, &chans);
        s_deviceChannels = chans;
        if (chans != 1)
            MIC_LOGI("Device opened with %u channels — will mix down to mono", chans);

        // Sample rate — request 48 kHz, accept driver negotiation
        unsigned int rate = kSampleRate;
        snd_pcm_hw_params_set_rate_near(s_pcm, hw, &rate, nullptr);
        s_deviceSampleRate = rate;
        if (rate != (unsigned)kSampleRate)
            MIC_LOGI("Sample rate negotiated to %u Hz (requested %d)", rate, kSampleRate);

        // Period = 10ms; ring buffer = 4× period (40ms) — balanced latency/safety
        snd_pcm_uframes_t period = kPeriodFrames;
        snd_pcm_hw_params_set_period_size_near(s_pcm, hw, &period, nullptr);
        s_periodActual = period;

        snd_pcm_uframes_t bufSize = period * 4;
        snd_pcm_hw_params_set_buffer_size_near(s_pcm, hw, &bufSize);

        err = snd_pcm_hw_params(s_pcm, hw);
        if (err < 0) {
            MIC_LOGE("snd_pcm_hw_params failed: %s", snd_strerror(err));
            _closeDevice(); return false;
        }

        // ── Software params — start capture immediately on first readi ────────
        snd_pcm_sw_params_t* sw = nullptr;
        snd_pcm_sw_params_alloca(&sw);
        snd_pcm_sw_params_current(s_pcm, sw);
        snd_pcm_sw_params_set_start_threshold(s_pcm, sw, 1);
        snd_pcm_sw_params(s_pcm, sw);

        err = snd_pcm_prepare(s_pcm);
        if (err < 0) { MIC_LOGE("snd_pcm_prepare: %s", snd_strerror(err)); _closeDevice(); return false; }

        err = snd_pcm_start(s_pcm);
        if (err < 0) { MIC_LOGE("snd_pcm_start: %s", snd_strerror(err)); _closeDevice(); return false; }

        MIC_LOGI("ALSA capture opened: %u Hz  %u ch  period=%lu frames  fmt=%s",
                 s_deviceSampleRate, s_deviceChannels, s_periodActual,
                 s_useFloat ? "FLOAT_LE" : "S16_LE");
        return true;
    }

    // ── Close ─────────────────────────────────────────────────────────────────
    void _closeDevice() {
        if (!s_pcm) return;
        snd_pcm_drop(s_pcm);
        snd_pcm_close(s_pcm);
        s_pcm = nullptr;
    }

    // ── Capture loop ──────────────────────────────────────────────────────────
    //
    // Uses blocking snd_pcm_readi — ALSA blocks until a full period is ready,
    // so no explicit sleep is needed (and none is added to avoid drift).
    //
    // Xrun recovery matches the pattern in flux_audio.hpp _writeLoop:
    //   EPIPE  → overrun  : drop overrun data, recover, restart
    //   ESTRPIPE → suspend: wait for resume, re-prepare, restart
    //
    void _captureLoop() {
        MIC_LOGI("Capture loop started");

        size_t channelFrames = s_periodActual * s_deviceChannels;
        std::vector<float>   periodBuf(channelFrames);
        std::vector<int16_t> pcm16Buf;
        if (!s_useFloat) pcm16Buf.resize(channelFrames);

        float rmsAccum   = 0.f;
        int   rmsSamples = 0;
        // 50ms RMS window at the negotiated device rate
        const int kRmsWindow = (int)s_deviceSampleRate / 20;

        while (!s_stopPoll) {
            snd_pcm_sframes_t framesRead;

            if (s_useFloat) {
                framesRead = snd_pcm_readi(s_pcm, periodBuf.data(), s_periodActual);
            } else {
                framesRead = snd_pcm_readi(s_pcm, pcm16Buf.data(), s_periodActual);
            }

            if (framesRead < 0) {
                if (framesRead == -EPIPE) {
                    // Overrun — hardware ring filled before we drained it.
                    // Recover silently and restart; the overrun'd audio is lost.
                    MIC_LOGI("ALSA overrun — recovering (data lost)");
                    snd_pcm_recover(s_pcm, (int)framesRead, 1 /*silent*/);
                    snd_pcm_start(s_pcm);
                } else if (framesRead == -ESTRPIPE) {
                    // Suspend (e.g. power management).  Poll resume.
                    MIC_LOGI("ALSA suspended — waiting for resume");
                    int r;
                    while ((r = snd_pcm_resume(s_pcm)) == -EAGAIN)
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    if (r < 0) snd_pcm_prepare(s_pcm);
                    snd_pcm_start(s_pcm);
                } else {
                    MIC_LOGE("snd_pcm_readi error: %s", snd_strerror((int)framesRead));
                    snd_pcm_recover(s_pcm, (int)framesRead, 1);
                }
                continue;
            }

            if (framesRead == 0) continue;

            // Convert S16 → float32 if the device doesn't support FLOAT_LE
            if (!s_useFloat) {
                size_t total = (size_t)framesRead * s_deviceChannels;
                for (size_t k = 0; k < total; k++)
                    periodBuf[k] = pcm16Buf[k] / 32768.f;
            }

            // Mix interleaved channels down to mono, write to ring
            size_t wp    = s_writePos.load();
            size_t space = kRingSize - wp;
            size_t n     = std::min<size_t>((size_t)framesRead, space);
            uint32_t ch  = s_deviceChannels;

            for (size_t i = 0; i < n; i++) {
                float mono = 0.f;
                for (uint32_t c = 0; c < ch; c++)
                    mono += periodBuf[i * ch + c];
                if (ch > 1) mono /= (float)ch;

                s_ring[wp + i] = mono;

                rmsAccum  += mono * mono;
                rmsSamples++;
                if (rmsSamples >= kRmsWindow) {
                    float rms = std::sqrt(rmsAccum / rmsSamples);
                    s_amplitude.store(s_amplitude.load() * 0.7f + rms * 0.3f);
                    rmsAccum   = 0.f;
                    rmsSamples = 0;
                }
            }
            s_writePos.store(wp + n);

            if (s_writePos.load() >= kRingSize) {
                MIC_LOGI("Max recording duration reached — stopping");
                s_stopPoll = true; s_state = State::Stopping; break;
            }
        }

        MIC_LOGI("Capture loop exited — %zu samples (%.2f s)",
                 s_writePos.load(), (float)s_writePos.load() / kSampleRate);
    }

    // ── Save WAV ──────────────────────────────────────────────────────────────
    void _saveWAV() {
        size_t n = s_writePos.load();
        if (n == 0) { MIC_LOGE("Nothing captured — no WAV written"); return; }
        std::string path = WavWriter::makeTimestampedPath(FluxLinux_getMicSaveDir());
        MIC_LOGI("Saving WAV: %s (%zu samples, %.2f s)",
                 path.c_str(), n, (float)n / s_deviceSampleRate);
        std::string result = WavWriter::write(path, s_ring.data(), n, s_deviceSampleRate, 1);
        if (!result.empty()) { s_lastSavedPath = result; if (s_onSaved) s_onSaved(result); }
        else MIC_LOGE("WAV write FAILED: %s", path.c_str());
    }
};

#endif // __linux__ && !__ANDROID__


// ============================================================================
// Android — AAudio backend  (unchanged)
// ============================================================================
#ifdef __ANDROID__

#include <aaudio/AAudio.h>
#include <android/log.h>

#undef  MIC_LOGI
#undef  MIC_LOGE
#define MIC_LOGI(...) __android_log_print(ANDROID_LOG_INFO,  "FluxMic", __VA_ARGS__)
#define MIC_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "FluxMic", __VA_ARGS__)

extern std::string FluxAndroid_getFilesDir();

class FluxMic {
public:
    static FluxMic& get() { static FluxMic inst; return inst; }

    static constexpr int    kSampleRate = 48000;
    static constexpr int    kChannels   = 1;
    static constexpr int    kMaxSeconds = 300;
    static constexpr size_t kRingSize   = kSampleRate * kMaxSeconds;

    enum class State { Idle, Recording, Stopping, Error };

    State  getState()          const { return s_state.load(); }
    bool   isRecording()       const { return s_state == State::Recording; }
    float  getAmplitude()      const { return s_amplitude.load(); }
    float  getProgress()       const {
        return std::min(1.f, (float)s_writePos.load() / (float)kRingSize);
    }
    float  getElapsedSeconds() const {
        return (float)s_writePos.load() / (float)kSampleRate;
    }
    const std::string& getLastSavedPath() const { return s_lastSavedPath; }

    using SaveCallback = std::function<void(const std::string&)>;
    void setOnSaved(SaveCallback cb) { s_onSaved = std::move(cb); }

    bool start() {
        if (s_state == State::Recording) return true;
        if (s_state == State::Stopping)  return false;

        s_ring.assign(kRingSize, 0.f);
        s_writePos = 0; s_amplitude = 0.f;

        AAudioStreamBuilder* builder = nullptr;
        AAudio_createStreamBuilder(&builder);
        AAudioStreamBuilder_setDirection(builder,       AAUDIO_DIRECTION_INPUT);
        AAudioStreamBuilder_setSampleRate(builder,      kSampleRate);
        AAudioStreamBuilder_setChannelCount(builder,    kChannels);
        AAudioStreamBuilder_setFormat(builder,          AAUDIO_FORMAT_PCM_FLOAT);
        AAudioStreamBuilder_setInputPreset(builder,     AAUDIO_INPUT_PRESET_VOICE_RECOGNITION);
        AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
        AAudioStreamBuilder_setSharingMode(builder,     AAUDIO_SHARING_MODE_EXCLUSIVE);

        aaudio_result_t res = AAudioStreamBuilder_openStream(builder, &s_stream);
        AAudioStreamBuilder_delete(builder);

        if (res != AAUDIO_OK || !s_stream) {
            MIC_LOGE("AAudio openStream failed: %s", AAudio_convertResultToText(res));
            s_state = State::Error; return false;
        }
        res = AAudioStream_requestStart(s_stream);
        if (res != AAUDIO_OK) {
            MIC_LOGE("requestStart failed: %s", AAudio_convertResultToText(res));
            AAudioStream_close(s_stream); s_stream = nullptr;
            s_state = State::Error; return false;
        }

        s_state = State::Recording; s_stopPoll = false;
        s_pollThread = std::thread(&FluxMic::_pollLoop, this);
        MIC_LOGI("Recording started — max %d s", kMaxSeconds);
        return true;
    }

    void stop() {
        if (s_state != State::Recording) return;
        s_state = State::Stopping; s_stopPoll = true;
        if (s_pollThread.joinable()) s_pollThread.join();
        _closeStream(); _saveWAV();
        s_state = State::Idle;
    }

    void cancel() {
        if (s_state != State::Recording) return;
        s_state = State::Stopping; s_stopPoll = true;
        if (s_pollThread.joinable()) s_pollThread.join();
        _closeStream();
        s_writePos = 0; s_amplitude = 0.f;
        s_state = State::Idle;
        MIC_LOGI("Recording cancelled");
    }

    void getWaveform(std::vector<float>& out, size_t count) const {
        FluxMicDetail::buildWaveform(s_ring, s_writePos.load(), out, count);
    }

private:
    FluxMic()  { s_ring.reserve(kRingSize); }
    ~FluxMic() { cancel(); }

    std::vector<float>   s_ring;
    std::atomic<size_t>  s_writePos  { 0 };
    std::atomic<float>   s_amplitude { 0.f };
    AAudioStream*        s_stream    = nullptr;
    std::thread          s_pollThread;
    std::atomic<bool>    s_stopPoll  { false };
    std::atomic<State>   s_state     { State::Idle };
    std::string          s_lastSavedPath;
    SaveCallback         s_onSaved;

    void _pollLoop() {
        MIC_LOGI("Poll loop started");
        static constexpr int kChunkFrames = kSampleRate / 100; // 480 frames / 10ms
        static constexpr int kTimeoutNs   = 10'000'000;

        std::vector<float> tmp(kChunkFrames);
        float rmsAccum = 0.f; int rmsSamples = 0;
        static constexpr int kRmsWindow = kSampleRate / 20; // 50ms

        while (!s_stopPoll) {
            if (!s_stream) break;
            aaudio_result_t fr =
                AAudioStream_read(s_stream, tmp.data(), kChunkFrames, kTimeoutNs);
            if (fr < 0) {
                if (fr != AAUDIO_ERROR_TIMEOUT)
                    MIC_LOGE("read error: %s", AAudio_convertResultToText(fr));
                continue;
            }
            if (fr == 0) continue;

            size_t wp = s_writePos.load();
            if (wp >= kRingSize) {
                MIC_LOGI("Max duration reached");
                s_stopPoll = true; s_state = State::Stopping; break;
            }
            size_t n = std::min<size_t>((size_t)fr, kRingSize - wp);
            memcpy(s_ring.data() + wp, tmp.data(), n * sizeof(float));
            s_writePos.store(wp + n);

            for (size_t i = 0; i < n; i++) {
                rmsAccum += tmp[i] * tmp[i]; rmsSamples++;
                if (rmsSamples >= kRmsWindow) {
                    float rms = std::sqrt(rmsAccum / rmsSamples);
                    s_amplitude.store(s_amplitude.load() * 0.7f + rms * 0.3f);
                    rmsAccum = 0.f; rmsSamples = 0;
                }
            }
        }
        MIC_LOGI("Poll loop exited — %zu samples (%.2f s)",
                 s_writePos.load(), (float)s_writePos.load() / kSampleRate);
    }

    void _closeStream() {
        if (!s_stream) return;
        AAudioStream_requestStop(s_stream);
        AAudioStream_close(s_stream);
        s_stream = nullptr;
    }

    void _saveWAV() {
        size_t n = s_writePos.load();
        if (n == 0) { MIC_LOGE("Nothing captured — no WAV written"); return; }
        std::string path = WavWriter::makeTimestampedPath(FluxAndroid_getFilesDir());
        std::string result = WavWriter::write(path, s_ring.data(), n, kSampleRate, 1);
        if (!result.empty()) { s_lastSavedPath = result; if (s_onSaved) s_onSaved(result); }
        else MIC_LOGE("WAV write FAILED: %s", path.c_str());
    }
};

#endif // __ANDROID__