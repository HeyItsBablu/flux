// flux_mic_android.cpp
// Android AAudio microphone backend for FluxMic.
//
// Link against: aaudio
// Requires: RECORD_AUDIO permission declared in AndroidManifest.xml
//
#ifdef __ANDROID__

#include "flux/flux_mic.hpp"

#include <aaudio/AAudio.h>
#include <android/log.h>
#include <jni.h>
#include <android/native_activity.h>

// ============================================================================
// Logging
// ============================================================================
#define MIC_LOGI(...) __android_log_print(ANDROID_LOG_INFO,  "FluxMic", __VA_ARGS__)
#define MIC_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "FluxMic", __VA_ARGS__)

// ============================================================================
// Save-directory helper — provided by the app layer (JNI / Kotlin).
// Must be defined exactly once in a .cpp file that is linked into the binary.
// ============================================================================
extern std::string FluxAndroid_getFilesDir();

// ── JNI bridge — provided by the app layer (main_android.cpp) ────────────────
extern JNIEnv *getJNIEnv();
extern ANativeActivity *s_activity;
extern void FluxAndroid_requestPermission(const char *permission);

// ============================================================================
// FluxMic::Impl — AAudio capture
// ============================================================================
struct FluxMic::Impl {
    // 10 ms chunk at 48 kHz — matches kChunkFrames below
    static constexpr int kChunkFrames = FluxMic::kSampleRate / 100; // 480
    static constexpr int kTimeoutNs   = 10'000'000;                 // 10 ms
    static constexpr int kRmsWindow   = FluxMic::kSampleRate / 20;  // 50 ms

    // ── Ring buffer & meters ─────────────────────────────────────────────────
    std::vector<float>   ring;
    std::atomic<size_t>  writePos  { 0 };
    std::atomic<float>   amplitude { 0.f };

    // ── AAudio ───────────────────────────────────────────────────────────────
    AAudioStream* stream = nullptr;

    // ── Control ──────────────────────────────────────────────────────────────
    std::thread       pollThread;
    std::atomic<bool> stopPoll { false };
    std::atomic<State> state   { State::Idle };
    std::string        lastSavedPath;
    SaveCallback       onSaved;

    // ── Open ─────────────────────────────────────────────────────────────────
    bool openStream() {
        AAudioStreamBuilder* builder = nullptr;
        AAudio_createStreamBuilder(&builder);
        AAudioStreamBuilder_setDirection      (builder, AAUDIO_DIRECTION_INPUT);
        AAudioStreamBuilder_setSampleRate     (builder, FluxMic::kSampleRate);
        AAudioStreamBuilder_setChannelCount   (builder, FluxMic::kChannels);
        AAudioStreamBuilder_setFormat         (builder, AAUDIO_FORMAT_PCM_FLOAT);
        AAudioStreamBuilder_setInputPreset    (builder, AAUDIO_INPUT_PRESET_VOICE_RECOGNITION);
        AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
        AAudioStreamBuilder_setSharingMode    (builder, AAUDIO_SHARING_MODE_EXCLUSIVE);

        aaudio_result_t res = AAudioStreamBuilder_openStream(builder, &stream);
        AAudioStreamBuilder_delete(builder);

        if (res != AAUDIO_OK || !stream) {
            MIC_LOGE("AAudio openStream failed: %s", AAudio_convertResultToText(res));
            stream = nullptr;
            return false;
        }

        res = AAudioStream_requestStart(stream);
        if (res != AAUDIO_OK) {
            MIC_LOGE("requestStart failed: %s", AAudio_convertResultToText(res));
            AAudioStream_close(stream);
            stream = nullptr;
            return false;
        }
        return true;
    }

    void closeStream() {
        if (!stream) return;
        AAudioStream_requestStop(stream);
        AAudioStream_close(stream);
        stream = nullptr;
    }

    // ── Poll loop ────────────────────────────────────────────────────────────
    void pollLoop() {
        MIC_LOGI("Poll loop started");

        std::vector<float> tmp(kChunkFrames);
        float rmsAccum   = 0.f;
        int   rmsSamples = 0;

        while (!stopPoll) {
            if (!stream) break;

            aaudio_result_t fr =
                AAudioStream_read(stream, tmp.data(), kChunkFrames, kTimeoutNs);

            if (fr < 0) {
                if (fr != AAUDIO_ERROR_TIMEOUT)
                    MIC_LOGE("read error: %s", AAudio_convertResultToText(fr));
                continue;
            }
            if (fr == 0) continue;

            size_t wp = writePos.load();
            if (wp >= FluxMic::kRingSize) {
                MIC_LOGI("Max duration reached");
                stopPoll = true;
                state    = State::Stopping;
                break;
            }

            size_t n = std::min<size_t>((size_t)fr, FluxMic::kRingSize - wp);
            memcpy(ring.data() + wp, tmp.data(), n * sizeof(float));
            writePos.store(wp + n);

            for (size_t i = 0; i < n; i++) {
                rmsAccum += tmp[i] * tmp[i];
                rmsSamples++;
                if (rmsSamples >= kRmsWindow) {
                    float rms = std::sqrt(rmsAccum / rmsSamples);
                    amplitude.store(amplitude.load() * 0.7f + rms * 0.3f);
                    rmsAccum   = 0.f;
                    rmsSamples = 0;
                }
            }
        }

        MIC_LOGI("Poll loop exited — %zu samples (%.2f s)",
                 writePos.load(),
                 (float)writePos.load() / FluxMic::kSampleRate);
    }

    // ── Save ──────────────────────────────────────────────────────────────────
    void saveWAV() {
        size_t n = writePos.load();
        if (n == 0) { MIC_LOGE("Nothing captured — no WAV written"); return; }
        std::string path = WavWriter::makeTimestampedPath(FluxAndroid_getFilesDir());
        std::string result = WavWriter::write(path, ring.data(), n,
                                              FluxMic::kSampleRate, 1);
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

extern void FluxAndroid_requestPermission(const char* permission);

static bool _hasMicPermission()
{
    JNIEnv *env = getJNIEnv();
    if (!env || !s_activity) return false;

    jobject actObj = s_activity->clazz;
    jclass actCls = env->GetObjectClass(actObj);
    jmethodID check = env->GetMethodID(actCls,
                                       "checkSelfPermission", "(Ljava/lang/String;)I");
    jstring perm = env->NewStringUTF("android.permission.RECORD_AUDIO");
    jint result = env->CallIntMethod(actObj, check, perm);
    env->DeleteLocalRef(perm);
    return result == 0;
}

bool FluxMic::start() {
    if (m_impl->state == State::Recording) return true;
    if (m_impl->state == State::Stopping)  return false;

    if (!_hasMicPermission()) {
        FluxAndroid_requestPermission("android.permission.RECORD_AUDIO");
        MIC_LOGI("RECORD_AUDIO not granted — requested, returning false");
        return false;
    }

    m_impl->ring.assign(kRingSize, 0.f);
    m_impl->writePos  = 0;
    m_impl->amplitude = 0.f;

    if (!m_impl->openStream()) { m_impl->state = State::Error; return false; }

    m_impl->state    = State::Recording;
    m_impl->stopPoll = false;
    m_impl->pollThread = std::thread(&Impl::pollLoop, m_impl);
    MIC_LOGI("Recording started — max %d s", kMaxSeconds);
    return true;
}

void FluxMic::stop() {
    if (m_impl->state != State::Recording) return;
    m_impl->state    = State::Stopping;
    m_impl->stopPoll = true;
    if (m_impl->pollThread.joinable()) m_impl->pollThread.join();
    m_impl->closeStream();
    m_impl->saveWAV();
    m_impl->state = State::Idle;
}

void FluxMic::cancel() {
    if (m_impl->state != State::Recording) return;
    m_impl->state    = State::Stopping;
    m_impl->stopPoll = true;
    if (m_impl->pollThread.joinable()) m_impl->pollThread.join();
    m_impl->closeStream();
    m_impl->writePos  = 0;
    m_impl->amplitude = 0.f;
    m_impl->state     = State::Idle;
    MIC_LOGI("Recording cancelled");
}

void FluxMic::getWaveform(std::vector<float>& out, size_t count) const {
    FluxMicDetail::buildWaveform(m_impl->ring, m_impl->writePos.load(), out, count);
}

#endif // __ANDROID__