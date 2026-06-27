// flux_mic_linux.cpp
// Linux ALSA microphone backend for FluxMic.
//
// Link against: asound
//
#if defined(__linux__) && !defined(__ANDROID__)

#include "flux/flux_mic.hpp"

#include <alsa/asoundlib.h>
#include <pwd.h>
#include <sys/stat.h>

// ============================================================================
// Logging
// ============================================================================
#define MIC_LOGI(fmt, ...) fprintf(stderr, "[FluxMic] " fmt "\n", ##__VA_ARGS__)
#define MIC_LOGE(fmt, ...) MIC_LOGI(fmt, ##__VA_ARGS__)

// ============================================================================
// Save-directory helper
// Default: $HOME/FluxMic  (auto-created).
// Define FLUXMIC_SAVE_DIR and provide your own FluxLinux_getMicSaveDir() to override.
// ============================================================================
#ifndef FLUXMIC_SAVE_DIR
static std::string FluxLinux_getMicSaveDir() {
    const char* home = getenv("HOME");
    if (!home) {
        struct passwd* pw = getpwuid(getuid());
        home = pw ? pw->pw_dir : nullptr;
    }
    std::string dir = home ? (std::string(home) + "/FluxMic") : "./FluxMic";
    mkdir(dir.c_str(), 0755);
    return dir;
}
#endif

// ============================================================================
// FluxMic::Impl — ALSA capture
// ============================================================================
struct FluxMic::Impl {
    // 10 ms period at 48 kHz
    static constexpr snd_pcm_uframes_t kPeriodFrames = 480;

    // ── Ring buffer & meters ─────────────────────────────────────────────────
    std::vector<float>   ring;
    std::atomic<size_t>  writePos  { 0 };
    std::atomic<float>   amplitude { 0.f };

    // ── ALSA state ───────────────────────────────────────────────────────────
    snd_pcm_t*         pcm              = nullptr;
    uint32_t           deviceSampleRate = FluxMic::kSampleRate;
    uint32_t           deviceChannels   = 1;
    snd_pcm_uframes_t  periodActual     = kPeriodFrames;
    bool               useFloat         = true;

    // ── Control ──────────────────────────────────────────────────────────────
    std::thread          pollThread;
    std::atomic<bool>    stopPoll { false };
    std::atomic<State>   state    { State::Idle };
    std::string          lastSavedPath;
    SaveCallback         onSaved;

    // ── Open ALSA capture device ─────────────────────────────────────────────
    //
    // Device priority:
    //   "default"  — honours ~/.asoundrc and PulseAudio/PipeWire ALSA plugins.
    //   "hw:0,0"   — bare ALSA fallback for embedded / headless systems.
    //   FLUXMIC_ALSA_DEVICE — compile-time override.
    //
    bool openDevice() {
#ifdef FLUXMIC_ALSA_DEVICE
        const char* devName = FLUXMIC_ALSA_DEVICE;
        int err = snd_pcm_open(&pcm, devName, SND_PCM_STREAM_CAPTURE, 0);
        if (err < 0) {
            MIC_LOGE("snd_pcm_open(\"%s\") failed: %s", devName, snd_strerror(err));
            pcm = nullptr; return false;
        }
#else
        int err = snd_pcm_open(&pcm, "default", SND_PCM_STREAM_CAPTURE, 0);
        if (err < 0) {
            MIC_LOGI("snd_pcm_open(\"default\") failed (%s) — retrying hw:0,0",
                     snd_strerror(err));
            err = snd_pcm_open(&pcm, "hw:0,0", SND_PCM_STREAM_CAPTURE, 0);
            if (err < 0) {
                MIC_LOGE("snd_pcm_open(\"hw:0,0\") also failed: %s", snd_strerror(err));
                pcm = nullptr; return false;
            }
        }
#endif

        // ── Hardware params ───────────────────────────────────────────────────
        snd_pcm_hw_params_t* hw = nullptr;
        snd_pcm_hw_params_alloca(&hw);
        snd_pcm_hw_params_any(pcm, hw);
        snd_pcm_hw_params_set_access(pcm, hw, SND_PCM_ACCESS_RW_INTERLEAVED);

        useFloat = true;
        err = snd_pcm_hw_params_set_format(pcm, hw, SND_PCM_FORMAT_FLOAT_LE);
        if (err < 0) {
            MIC_LOGI("FLOAT_LE not supported — falling back to S16_LE");
            useFloat = false;
            err = snd_pcm_hw_params_set_format(pcm, hw, SND_PCM_FORMAT_S16_LE);
            if (err < 0) {
                MIC_LOGE("S16_LE also unsupported: %s", snd_strerror(err));
                closeDevice(); return false;
            }
        }

        unsigned int chans = 1;
        snd_pcm_hw_params_set_channels_near(pcm, hw, &chans);
        deviceChannels = chans;
        if (chans != 1)
            MIC_LOGI("Device opened with %u channels — will mix down to mono", chans);

        unsigned int rate = FluxMic::kSampleRate;
        snd_pcm_hw_params_set_rate_near(pcm, hw, &rate, nullptr);
        deviceSampleRate = rate;
        if (rate != (unsigned)FluxMic::kSampleRate)
            MIC_LOGI("Sample rate negotiated to %u Hz (requested %d)", rate, FluxMic::kSampleRate);

        snd_pcm_uframes_t period = kPeriodFrames;
        snd_pcm_hw_params_set_period_size_near(pcm, hw, &period, nullptr);
        periodActual = period;

        snd_pcm_uframes_t bufSize = period * 4;
        snd_pcm_hw_params_set_buffer_size_near(pcm, hw, &bufSize);

        err = snd_pcm_hw_params(pcm, hw);
        if (err < 0) {
            MIC_LOGE("snd_pcm_hw_params failed: %s", snd_strerror(err));
            closeDevice(); return false;
        }

        // ── Software params — start immediately on first read ─────────────────
        snd_pcm_sw_params_t* sw = nullptr;
        snd_pcm_sw_params_alloca(&sw);
        snd_pcm_sw_params_current(pcm, sw);
        snd_pcm_sw_params_set_start_threshold(pcm, sw, 1);
        snd_pcm_sw_params(pcm, sw);

        err = snd_pcm_prepare(pcm);
        if (err < 0) { MIC_LOGE("snd_pcm_prepare: %s", snd_strerror(err)); closeDevice(); return false; }

        err = snd_pcm_start(pcm);
        if (err < 0) { MIC_LOGE("snd_pcm_start: %s", snd_strerror(err)); closeDevice(); return false; }

        MIC_LOGI("ALSA capture opened: %u Hz  %u ch  period=%lu frames  fmt=%s",
                 deviceSampleRate, deviceChannels, periodActual,
                 useFloat ? "FLOAT_LE" : "S16_LE");
        return true;
    }

    void closeDevice() {
        if (!pcm) return;
        snd_pcm_drop(pcm);
        snd_pcm_close(pcm);
        pcm = nullptr;
    }

    // ── Capture loop ─────────────────────────────────────────────────────────
    //
    // Uses blocking snd_pcm_readi — ALSA blocks until a full period is ready.
    //
    // Xrun recovery:
    //   EPIPE    — overrun:  drop data, recover, restart
    //   ESTRPIPE — suspend:  poll for resume, re-prepare, restart
    //
    void captureLoop() {
        MIC_LOGI("Capture loop started");

        size_t channelFrames = periodActual * deviceChannels;
        std::vector<float>   periodBuf(channelFrames);
        std::vector<int16_t> pcm16Buf;
        if (!useFloat) pcm16Buf.resize(channelFrames);

        float rmsAccum   = 0.f;
        int   rmsSamples = 0;
        const int kRmsWindow = (int)deviceSampleRate / 20; // 50 ms

        while (!stopPoll) {
            snd_pcm_sframes_t framesRead;

            if (useFloat)
                framesRead = snd_pcm_readi(pcm, periodBuf.data(), periodActual);
            else
                framesRead = snd_pcm_readi(pcm, pcm16Buf.data(), periodActual);

            if (framesRead < 0) {
                if (framesRead == -EPIPE) {
                    MIC_LOGI("ALSA overrun — recovering (data lost)");
                    snd_pcm_recover(pcm, (int)framesRead, 1);
                    snd_pcm_start(pcm);
                } else if (framesRead == -ESTRPIPE) {
                    MIC_LOGI("ALSA suspended — waiting for resume");
                    int r;
                    while ((r = snd_pcm_resume(pcm)) == -EAGAIN)
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    if (r < 0) snd_pcm_prepare(pcm);
                    snd_pcm_start(pcm);
                } else {
                    MIC_LOGE("snd_pcm_readi error: %s", snd_strerror((int)framesRead));
                    snd_pcm_recover(pcm, (int)framesRead, 1);
                }
                continue;
            }

            if (framesRead == 0) continue;

            // Convert S16 → float32 if needed
            if (!useFloat) {
                size_t total = (size_t)framesRead * deviceChannels;
                for (size_t k = 0; k < total; k++)
                    periodBuf[k] = pcm16Buf[k] / 32768.f;
            }

            // Mix interleaved channels → mono, write to ring
            size_t   wp    = writePos.load();
            size_t   space = FluxMic::kRingSize - wp;
            size_t   n     = std::min<size_t>((size_t)framesRead, space);
            uint32_t ch    = deviceChannels;

            for (size_t i = 0; i < n; i++) {
                float mono = 0.f;
                for (uint32_t c = 0; c < ch; c++)
                    mono += periodBuf[i * ch + c];
                if (ch > 1) mono /= (float)ch;

                ring[wp + i] = mono;
                rmsAccum    += mono * mono;
                rmsSamples++;
                if (rmsSamples >= kRmsWindow) {
                    float rms = std::sqrt(rmsAccum / rmsSamples);
                    amplitude.store(amplitude.load() * 0.7f + rms * 0.3f);
                    rmsAccum   = 0.f;
                    rmsSamples = 0;
                }
            }
            writePos.store(wp + n);

            if (writePos.load() >= FluxMic::kRingSize) {
                MIC_LOGI("Max recording duration reached — stopping");
                stopPoll = true;
                state    = State::Stopping;
                break;
            }
        }

        MIC_LOGI("Capture loop exited — %zu samples (%.2f s)",
                 writePos.load(),
                 (float)writePos.load() / FluxMic::kSampleRate);
    }

    // ── Save ──────────────────────────────────────────────────────────────────
    void saveWAV() {
        size_t n = writePos.load();
        if (n == 0) { MIC_LOGE("Nothing captured — no WAV written"); return; }
        std::string path = WavWriter::makeTimestampedPath(FluxLinux_getMicSaveDir());
        MIC_LOGI("Saving WAV: %s (%zu samples, %.2f s)",
                 path.c_str(), n, (float)n / deviceSampleRate);
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

#endif // __linux__ && !__ANDROID__