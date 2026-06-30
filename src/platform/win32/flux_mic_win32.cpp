// flux_mic_win32.cpp
// Windows WASAPI capture backend for FluxMic.
// Link against: ole32  mmdevapi
#ifdef _WIN32

#include "flux/flux_mic.hpp"

#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <wrl/client.h>

#include <atomic>
#include <cmath>
#include <cstring>
#include <thread>
#include <vector>

using Microsoft::WRL::ComPtr;

#define MIC_LOGE(fmt, ...)                                          \
    do                                                              \
    {                                                                \
        char _b[256];                                                \
        snprintf(_b, 256, "[FluxMic] " fmt "\n", ##__VA_ARGS__);      \
        OutputDebugStringA(_b);                                       \
    } while (0)

struct FluxMic::Impl
{
    ComPtr<IMMDeviceEnumerator> enumerator;
    ComPtr<IMMDevice>           device;
    ComPtr<IAudioClient>        audioClient;
    ComPtr<IAudioCaptureClient> captureClient;
    WAVEFORMATEX               *mixFormat = nullptr;

    int sampleRate = FluxMic::kDefaultSampleRate;
    int channels   = FluxMic::kDefaultChannels;

    std::atomic<bool> opened{false};
    std::atomic<bool> capturing{false};
    std::atomic<float> level{0.f};

    FrameCallback cb;
    std::thread captureThread;
    std::atomic<bool> threadRunning{false};

    // Scratch mono-float buffer reused every callback.
    std::vector<float> monoBuf;

    bool doOpen(int sr, int ch)
    {
        sampleRate = sr;
        channels   = ch;

        CoInitializeEx(nullptr, COINIT_MULTITHREADED);

        HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                                      CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                                      (void **)enumerator.GetAddressOf());
        if (FAILED(hr)) { MIC_LOGE("CoCreateInstance: 0x%08X", hr); return false; }

        hr = enumerator->GetDefaultAudioEndpoint(eCapture, eConsole,
                                                  device.GetAddressOf());
        if (FAILED(hr)) { MIC_LOGE("GetDefaultAudioEndpoint: 0x%08X", hr); return false; }

        hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                              (void **)audioClient.GetAddressOf());
        if (FAILED(hr)) { MIC_LOGE("Activate: 0x%08X", hr); return false; }

        hr = audioClient->GetMixFormat(&mixFormat);
        if (FAILED(hr)) { MIC_LOGE("GetMixFormat: 0x%08X", hr); return false; }

        // Request the device's native mix format (shared mode auto-converts);
        // we'll downmix to mono float32 ourselves in the capture loop.
        REFERENCE_TIME bufDuration = 20 * 10000; // 20ms in 100ns units
        hr = audioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, 0,
                                     bufDuration, 0, mixFormat, nullptr);
        if (FAILED(hr)) { MIC_LOGE("Initialize: 0x%08X", hr); return false; }

        hr = audioClient->GetService(__uuidof(IAudioCaptureClient),
                                     (void **)captureClient.GetAddressOf());
        if (FAILED(hr)) { MIC_LOGE("GetService: 0x%08X", hr); return false; }

        opened = true;
        return true;
    }

    void downmixToMonoFloat(const BYTE *src, UINT32 frames)
    {
        int srcCh = mixFormat->nChannels;
        bool isFloat = (mixFormat->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) ||
                       (mixFormat->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
                        mixFormat->wBitsPerSample == 32);

        monoBuf.resize(frames);

        if (isFloat)
        {
            const float *fsrc = reinterpret_cast<const float *>(src);
            for (UINT32 i = 0; i < frames; i++)
            {
                float sum = 0.f;
                for (int c = 0; c < srcCh; c++)
                    sum += fsrc[i * srcCh + c];
                monoBuf[i] = sum / (float)srcCh;
            }
        }
        else
        {
            // 16-bit PCM fallback
            const int16_t *isrc = reinterpret_cast<const int16_t *>(src);
            for (UINT32 i = 0; i < frames; i++)
            {
                int32_t sum = 0;
                for (int c = 0; c < srcCh; c++)
                    sum += isrc[i * srcCh + c];
                monoBuf[i] = (float)(sum / srcCh) / 32768.f;
            }
        }
    }

    void captureLoop()
    {
        HRESULT hr = audioClient->Start();
        if (FAILED(hr)) { MIC_LOGE("Start: 0x%08X", hr); capturing = false; return; }

        while (threadRunning.load())
        {
            UINT32 packetLength = 0;
            hr = captureClient->GetNextPacketSize(&packetLength);
            if (FAILED(hr)) break;

            if (packetLength == 0)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                continue;
            }

            BYTE *data = nullptr;
            UINT32 numFrames = 0;
            DWORD flags = 0;
            hr = captureClient->GetBuffer(&data, &numFrames, &flags, nullptr, nullptr);
            if (FAILED(hr)) break;

            if (numFrames > 0)
            {
                if (flags & AUDCLNT_BUFFERFLAGS_SILENT)
                {
                    monoBuf.assign(numFrames, 0.f);
                }
                else
                {
                    downmixToMonoFloat(data, numFrames);
                }

                float rms = 0.f;
                for (float s : monoBuf) rms += s * s;
                level.store(std::sqrt(rms / (float)std::max<UINT32>(1, numFrames)));

                if (cb) cb(monoBuf.data(), monoBuf.size());
            }

            captureClient->ReleaseBuffer(numFrames);
        }

        audioClient->Stop();
    }
};

FluxMic &FluxMic::get() { static FluxMic inst; return inst; }
FluxMic::FluxMic() : m_impl(new Impl()) {}
FluxMic::~FluxMic() { close(); delete m_impl; }

bool FluxMic::open(int sampleRate, int channels)
{
    if (m_impl->opened.load()) return true;
    return m_impl->doOpen(sampleRate, channels);
}

bool FluxMic::start(FrameCallback cb)
{
    if (!m_impl->opened.load() && !open(m_impl->sampleRate, m_impl->channels))
        return false;
    if (m_impl->capturing.load()) return true;

    m_impl->cb = std::move(cb);
    m_impl->capturing = true;
    m_impl->threadRunning = true;
    m_impl->captureThread = std::thread(&Impl::captureLoop, m_impl);
    return true;
}

void FluxMic::stop()
{
    if (!m_impl->capturing.load()) return;
    m_impl->threadRunning = false;
    if (m_impl->captureThread.joinable()) m_impl->captureThread.join();
    m_impl->capturing = false;
    m_impl->cb = nullptr;
    m_impl->level = 0.f;
}

void FluxMic::close()
{
    stop();
    if (m_impl->mixFormat) { CoTaskMemFree(m_impl->mixFormat); m_impl->mixFormat = nullptr; }
    m_impl->captureClient.Reset();
    m_impl->audioClient.Reset();
    m_impl->device.Reset();
    m_impl->enumerator.Reset();
    m_impl->opened = false;
}

FluxMic::State FluxMic::getState() const
{
    if (!m_impl->opened.load()) return State::Idle;
    return m_impl->capturing.load() ? State::Recording : State::Open;
}
bool  FluxMic::isRecording()   const { return m_impl->capturing.load(); }
float FluxMic::getInputLevel() const { return m_impl->level.load(); }
int   FluxMic::getSampleRate() const { return m_impl->sampleRate; }
int   FluxMic::getChannels()   const { return m_impl->channels; }

#endif // _WIN32