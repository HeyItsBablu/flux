// flux_audio.hpp
#pragma once
#ifdef __ANDROID__

#include <aaudio/AAudio.h>
#include <android/asset_manager.h>
#include <android/log.h>
#include <functional>
#include <vector>
#include <atomic> 
#include <cstring>
#include <string>
#include <cmath>

#include "dr_mp3.h"
#include "dr_wav.h"

#define AUDIO_LOGI(...) __android_log_print(ANDROID_LOG_INFO,  "FluxAudio", __VA_ARGS__)
#define AUDIO_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "FluxAudio", __VA_ARGS__)

extern AAssetManager* FluxAndroid_getAssetManager();

// ============================================================================
// FluxAudio — AAudio-based playback + recording + file playback
// ============================================================================

class FluxAudio {
public:

    using RecordCallback  = std::function<void(const float* samples, int count)>;
    using PlayCallback    = std::function<int(float* buffer, int maxFrames)>;
    using FinishCallback  = std::function<void()>;

    // ── Singleton ─────────────────────────────────────────────────────────────
    static FluxAudio& get() {
        static FluxAudio instance;
        return instance;
    }

    // =========================================================================
    // VOLUME CONTROL  (0.0 – 1.0, default 1.0)
    // =========================================================================

    void  setVolume(float v) { s_volume.store(std::max(0.f, std::min(1.f, v))); }
    float getVolume()  const { return s_volume.load(); }

    // =========================================================================
    // PROGRESS  (0.0 – 1.0)  — safe to call from UI thread
    // =========================================================================

    float getProgress() const {
        int total = (int)s_playBuffer.size();
        if (total == 0) return 0.f;
        return std::min(1.f, (float)s_playPosition.load() / (float)total);
    }

    // Returns current playback position in seconds
    float getPositionSeconds() const {
        if (s_playSampleRate == 0) return 0.f;
        return (float)s_playPosition.load() / (float)s_playSampleRate;
    }

    // Returns total duration in seconds
    float getDurationSeconds() const {
        if (s_playSampleRate == 0) return 0.f;
        return (float)s_playBuffer.size() / (float)s_playSampleRate;
    }


    void seekToProgress(float progress) {
        int total  = (int)s_playBuffer.size();
        int target = (int)(std::max(0.f, std::min(1.f, progress)) * total);
        s_playPosition.store(target);
    }

    void seekToSeconds(float seconds) {
        int target = (int)(seconds * s_playSampleRate);
        target = std::max(0, std::min((int)s_playBuffer.size(), target));
        s_playPosition.store(target);
    }


    void setOnFinished(FinishCallback cb) { s_finishCallback = std::move(cb); }

    // =========================================================================
    // FILE PLAYBACK
    // =========================================================================

    bool playFromPath(const std::string& path) {
        std::vector<float> samples;
        int                sampleRate = 44100;

        if (!_decodeFile(path, samples, sampleRate)) {
            AUDIO_LOGE("playFromPath: failed to decode '%s'", path.c_str());
            return false;
        }

        AUDIO_LOGI("playFromPath: '%s' — %zu frames @ %dHz",
                   path.c_str(), samples.size(), sampleRate);
        return playPCM(samples, sampleRate);
    }

    // ── Pause / Resume ────────────────────────────────────────────────────────
    void pause() {
        if (s_playStream && s_playing.load()) {
            s_playing = false;
            AAudioStream_requestPause(s_playStream);
        }
    }

    void resume() {
        if (s_playStream && !s_playing.load() && s_playPosition.load() < (int)s_playBuffer.size()) {
            s_playing = true;
            AAudioStream_requestStart(s_playStream);
        }
    }

    bool isPaused() const {
        return s_playStream && !s_playing.load()
               && s_playPosition.load() < (int)s_playBuffer.size();
    }

    // =========================================================================
    // PLAYBACK
    // =========================================================================

    bool openPlayback(int sampleRate = 44100, int channels = 1) {
        if (s_playStream) closePlayback();

        AAudioStreamBuilder* builder = nullptr;
        AAudio_createStreamBuilder(&builder);

        AAudioStreamBuilder_setDirection      (builder, AAUDIO_DIRECTION_OUTPUT);
        AAudioStreamBuilder_setSampleRate     (builder, sampleRate);
        AAudioStreamBuilder_setChannelCount   (builder, channels);
        AAudioStreamBuilder_setFormat         (builder, AAUDIO_FORMAT_PCM_FLOAT);
        AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
        AAudioStreamBuilder_setDataCallback   (builder, playDataCallback, this);

        aaudio_result_t result = AAudioStreamBuilder_openStream(builder, &s_playStream);
        AAudioStreamBuilder_delete(builder);

        if (result != AAUDIO_OK) {
            AUDIO_LOGE("openPlayback failed: %s", AAudio_convertResultToText(result));
            s_playStream = nullptr;
            return false;
        }

        s_playSampleRate = AAudioStream_getSampleRate(s_playStream);
        s_playChannels   = AAudioStream_getChannelCount(s_playStream);
        AUDIO_LOGI("Playback opened: %dHz %dch", s_playSampleRate, s_playChannels);
        return true;
    }

    bool playPCM(const std::vector<float>& samples, int sampleRate = 44100) {
        stopPlayback();
        closePlayback();

        s_playBuffer   = samples;
        s_playPosition = 0;
        s_playCallback = nullptr;
        s_didFireFinish = false;

        if (!openPlayback(sampleRate)) return false;
        return startPlayback();
    }

    bool playStream(PlayCallback cb, int sampleRate = 44100) {
        s_playCallback = cb;
        s_playBuffer.clear();
        s_playPosition = 0;
        s_didFireFinish = false;

        if (!s_playStream && !openPlayback(sampleRate)) return false;
        return startPlayback();
    }

    bool startPlayback() {
        if (!s_playStream) return false;
        s_playing = true;
        aaudio_result_t r = AAudioStream_requestStart(s_playStream);
        if (r != AAUDIO_OK) {
            AUDIO_LOGE("startPlayback failed: %s", AAudio_convertResultToText(r));
            s_playing = false;
            return false;
        }
        return true;
    }

    void stopPlayback() {
        s_playing = false;
        if (s_playStream)
            AAudioStream_requestStop(s_playStream);
    }

    void closePlayback() {
        stopPlayback();
        if (s_playStream) {
            AAudioStream_close(s_playStream);
            s_playStream = nullptr;
        }
    }

    bool isPlaying() const { return s_playing.load(); }

    // =========================================================================
    // RECORDING
    // =========================================================================

    bool openRecord(int sampleRate = 44100, int channels = 1) {
        if (s_recStream) closeRecord();

        AAudioStreamBuilder* builder = nullptr;
        AAudio_createStreamBuilder(&builder);

        AAudioStreamBuilder_setDirection      (builder, AAUDIO_DIRECTION_INPUT);
        AAudioStreamBuilder_setSampleRate     (builder, sampleRate);
        AAudioStreamBuilder_setChannelCount   (builder, channels);
        AAudioStreamBuilder_setFormat         (builder, AAUDIO_FORMAT_PCM_FLOAT);
        AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
        AAudioStreamBuilder_setDataCallback   (builder, recDataCallback, this);

        aaudio_result_t result = AAudioStreamBuilder_openStream(builder, &s_recStream);
        AAudioStreamBuilder_delete(builder);

        if (result != AAUDIO_OK) {
            AUDIO_LOGE("openRecord failed: %s", AAudio_convertResultToText(result));
            s_recStream = nullptr;
            return false;
        }

        s_recSampleRate = AAudioStream_getSampleRate(s_recStream);
        AUDIO_LOGI("Record opened: %dHz", s_recSampleRate);
        return true;
    }

    bool startRecord(RecordCallback cb) {
        s_recCallback = cb;
        s_recording   = true;
        s_recBuffer.clear();

        if (!s_recStream && !openRecord()) return false;

        aaudio_result_t r = AAudioStream_requestStart(s_recStream);
        if (r != AAUDIO_OK) {
            AUDIO_LOGE("startRecord failed: %s", AAudio_convertResultToText(r));
            s_recording = false;
            return false;
        }
        return true;
    }

    bool startRecordToBuffer() {
        return startRecord([this](const float* samples, int count) {
            for (int i = 0; i < count; i++)
                s_recBuffer.push_back(samples[i]);
        });
    }

    void stopRecord() {
        s_recording = false;
        if (s_recStream)
            AAudioStream_requestStop(s_recStream);
    }

    void closeRecord() {
        stopRecord();
        if (s_recStream) {
            AAudioStream_close(s_recStream);
            s_recStream = nullptr;
        }
    }

    bool isRecording()                        const { return s_recording.load(); }
    const std::vector<float>& getRecording()  const { return s_recBuffer; }
    void clearRecording()                           { s_recBuffer.clear(); }
    int  getRecordSampleRate()                const { return s_recSampleRate; }

    // =========================================================================
    // WAVEFORM
    // =========================================================================

    static std::vector<std::pair<int,int>> buildWaveform(
            const float* samples, int count,
            int x, int y, int w, int h, int points = 200)
    {
        std::vector<std::pair<int,int>> result;
        if (count == 0 || points <= 0) return result;
        result.reserve(points);

        float step  = static_cast<float>(count) / points;
        int   halfH = h / 2;
        int   midY  = y + halfH;

        for (int i = 0; i < points; i++) {
            int start = static_cast<int>(i * step);
            int end   = std::min(static_cast<int>((i + 1) * step), count);
            float peak = 0.f;
            for (int j = start; j < end; j++)
                peak = std::max(peak, std::abs(samples[j]));
            result.emplace_back(x + (i * w / points),
                                midY - static_cast<int>(peak * halfH));
        }
        return result;
    }

    static std::vector<std::pair<int,int>> buildWaveform(
            const std::vector<float>& samples,
            int x, int y, int w, int h, int points = 200)
    {
        return buildWaveform(samples.data(), (int)samples.size(),
                             x, y, w, h, points);
    }

    float getInputLevel() const { return s_inputLevel.load(); }

    void shutdown() {
        closePlayback();
        closeRecord();
    }

private:
    FluxAudio() = default;

    // ── Playback state ────────────────────────────────────────────────────────
    AAudioStream*      s_playStream     = nullptr;
    std::vector<float> s_playBuffer;
    std::atomic<int>   s_playPosition   {0};
    PlayCallback       s_playCallback;
    FinishCallback     s_finishCallback;
    std::atomic<bool>  s_playing        {false};
    std::atomic<bool>  s_didFireFinish  {false};   // fire finish callback only once
    std::atomic<float> s_volume         {1.0f};    // ← NEW: volume 0.0–1.0
    int                s_playSampleRate  = 44100;
    int                s_playChannels    = 1;

    // ── Record state ──────────────────────────────────────────────────────────
    AAudioStream*      s_recStream      = nullptr;
    RecordCallback     s_recCallback;
    std::vector<float> s_recBuffer;
    std::atomic<bool>  s_recording      {false};
    int                s_recSampleRate   = 44100;
    std::atomic<float> s_inputLevel     {0.f};

    // =========================================================================
    // FILE DECODING
    // =========================================================================

    static bool _isMp3(const std::string& path) {
        if (path.size() < 4) return false;
        std::string ext = path.substr(path.size() - 4);
        for (auto& c : ext) c = (char)tolower(c);
        return ext == ".mp3";
    }
    static bool _isWav(const std::string& path) {
        if (path.size() < 4) return false;
        std::string ext = path.substr(path.size() - 4);
        for (auto& c : ext) c = (char)tolower(c);
        return ext == ".wav";
    }

    static std::vector<uint8_t> _loadBytes(const std::string& path) {
        if (!path.empty() && path[0] == '/') {
            FILE* f = fopen(path.c_str(), "rb");
            if (!f) return {};
            fseek(f, 0, SEEK_END);
            size_t len = (size_t)ftell(f);
            fseek(f, 0, SEEK_SET);
            std::vector<uint8_t> buf(len);
            fread(buf.data(), 1, len, f);
            fclose(f);
            return buf;
        } else {
            AAssetManager* am = FluxAndroid_getAssetManager();
            if (!am) return {};
            AAsset* asset = AAssetManager_open(am, path.c_str(), AASSET_MODE_BUFFER);
            if (!asset) return {};
            size_t len = (size_t)AAsset_getLength(asset);
            std::vector<uint8_t> buf(len);
            AAsset_read(asset, buf.data(), len);
            AAsset_close(asset);
            return buf;
        }
    }

    bool _decodeFile(const std::string& path,
                     std::vector<float>& outSamples,
                     int& outSampleRate)
    {
        std::vector<uint8_t> raw = _loadBytes(path);
        if (raw.empty()) {
            AUDIO_LOGE("_decodeFile: could not load '%s'", path.c_str());
            return false;
        }

        if (_isMp3(path)) {
            drmp3_config cfg{};
            drmp3_uint64 frameCount = 0;
            float* pcm = drmp3_open_memory_and_read_pcm_frames_f32(
                    raw.data(), raw.size(), &cfg, &frameCount, nullptr);
            if (!pcm) return false;
            outSampleRate = (int)cfg.sampleRate;
            _mixdownToMono(pcm, (int)frameCount, (int)cfg.channels, outSamples);
            drmp3_free(pcm, nullptr);
            return true;
        }

        if (_isWav(path)) {
            drwav_uint64 frameCount = 0;
            unsigned int channels = 0, sampleRate = 0;
            float* pcm = drwav_open_memory_and_read_pcm_frames_f32(
                    raw.data(), raw.size(), &channels, &sampleRate, &frameCount, nullptr);
            if (!pcm) return false;
            outSampleRate = (int)sampleRate;
            _mixdownToMono(pcm, (int)frameCount, (int)channels, outSamples);
            drwav_free(pcm, nullptr);
            return true;
        }

        AUDIO_LOGE("_decodeFile: unsupported format '%s'", path.c_str());
        return false;
    }

    static void _mixdownToMono(const float* src, int frames, int channels,
                               std::vector<float>& out)
    {
        out.resize(frames);
        if (channels == 1) {
            memcpy(out.data(), src, frames * sizeof(float));
        } else {
            float inv = 1.f / channels;
            for (int i = 0; i < frames; i++) {
                float sum = 0.f;
                for (int c = 0; c < channels; c++)
                    sum += src[i * channels + c];
                out[i] = sum * inv;
            }
        }
    }

    // =========================================================================
    // AAudio callbacks
    // =========================================================================

    static aaudio_data_callback_result_t playDataCallback(
            AAudioStream*, void* userData, void* audioData, int32_t numFrames)
    {
        auto* self   = static_cast<FluxAudio*>(userData);
        auto* output = static_cast<float*>(audioData);
        float vol    = self->s_volume.load();           // ← read volume once

        if (!self->s_playing) {
            memset(output, 0, numFrames * sizeof(float));
            return AAUDIO_CALLBACK_RESULT_STOP;
        }

        bool finished = false;

        if (self->s_playCallback) {
            int written = self->s_playCallback(output, numFrames);
            // Apply volume
            for (int i = 0; i < written; i++) output[i] *= vol;
            if (written < numFrames)
                memset(output + written, 0, (numFrames - written) * sizeof(float));
            if (written == 0) {
                self->s_playing = false;
                finished = true;
            }
        } else {
            int pos   = self->s_playPosition.load();
            int avail = (int)self->s_playBuffer.size() - pos;

            if (avail <= 0) {
                memset(output, 0, numFrames * sizeof(float));
                self->s_playing = false;
                finished = true;
            } else {
                int toCopy = std::min(avail, numFrames);
                memcpy(output, self->s_playBuffer.data() + pos, toCopy * sizeof(float));

                // Apply volume in-place
                for (int i = 0; i < toCopy; i++) output[i] *= vol;

                if (toCopy < numFrames)
                    memset(output + toCopy, 0, (numFrames - toCopy) * sizeof(float));
                self->s_playPosition.fetch_add(toCopy);
            }
        }

        if (finished) {
            // Fire finish callback exactly once
            bool expected = false;
            if (self->s_didFireFinish.compare_exchange_strong(expected, true)) {
                if (self->s_finishCallback) self->s_finishCallback();
            }
            return AAUDIO_CALLBACK_RESULT_STOP;
        }

        return AAUDIO_CALLBACK_RESULT_CONTINUE;
    }

    static aaudio_data_callback_result_t recDataCallback(
            AAudioStream*, void* userData, void* audioData, int32_t numFrames)
    {
        auto* self  = static_cast<FluxAudio*>(userData);
        auto* input = static_cast<const float*>(audioData);

        if (!self->s_recording) return AAUDIO_CALLBACK_RESULT_STOP;

        float rms = 0.f;
        for (int i = 0; i < numFrames; i++) rms += input[i] * input[i];
        self->s_inputLevel.store(std::sqrt(rms / numFrames));

        if (self->s_recCallback) self->s_recCallback(input, numFrames);
        return AAUDIO_CALLBACK_RESULT_CONTINUE;
    }
};

#endif // __ANDROID__

// ============================================================================
// FluxAudio — XAudio2-based backend  (Windows)
// ============================================================================
#ifdef _WIN32

#include <xaudio2.h>
#include <wrl/client.h>          // Microsoft::WRL::ComPtr

#include <functional>
#include <vector>
#include <atomic>
#include <cstring>
#include <string>
#include <cmath>
#include <algorithm>

#include "dr_mp3.h"
#include "dr_wav.h"

#undef  AUDIO_LOGI
#undef  AUDIO_LOGE
#define AUDIO_LOGI(fmt, ...) do { char _b[256]; snprintf(_b,256,"[FluxAudio] " fmt "\n",##__VA_ARGS__); OutputDebugStringA(_b); } while(0)
#define AUDIO_LOGE(fmt, ...) AUDIO_LOGI(fmt, ##__VA_ARGS__)

// ── VoiceCallback — fires OnStreamEnd when the source voice drains ───────────
class FLXVoiceCallback : public IXAudio2VoiceCallback {
public:
    std::function<void()> onStreamEnd;

    void STDMETHODCALLTYPE OnStreamEnd() noexcept override {
        if (onStreamEnd) onStreamEnd();
    }
    // Required no-ops
    void STDMETHODCALLTYPE OnVoiceProcessingPassStart(UINT32) noexcept override {}
    void STDMETHODCALLTYPE OnVoiceProcessingPassEnd()         noexcept override {}
    void STDMETHODCALLTYPE OnBufferStart(void*)               noexcept override {}
    void STDMETHODCALLTYPE OnBufferEnd(void*)                 noexcept override {}
    void STDMETHODCALLTYPE OnLoopEnd(void*)                   noexcept override {}
    void STDMETHODCALLTYPE OnVoiceError(void*, HRESULT)       noexcept override {}
};

class FluxAudio {
public:
    using FinishCallback = std::function<void()>;

    // ── Singleton ─────────────────────────────────────────────────────────────
    static FluxAudio& get() {
        static FluxAudio instance;
        return instance;
    }

    // ── Volume (0.0–1.0) ─────────────────────────────────────────────────────
    void  setVolume(float v) {
        s_volume.store(std::max(0.f, std::min(1.f, v)));
        if (s_sourceVoice) s_sourceVoice->SetVolume(s_volume.load());
    }
    float getVolume() const { return s_volume.load(); }

    // ── Progress ──────────────────────────────────────────────────────────────
    float getProgress() const {
        int total = (int)s_playBuffer.size();
        if (total == 0) return 0.f;
  
        return std::min(1.f, (float)s_playPosition.load() / (float)total);
    }

    float getPositionSeconds() const {
        if (s_playSampleRate == 0) return 0.f;
        return (float)s_playPosition.load() / (float)s_playSampleRate;
    }

    float getDurationSeconds() const {
        if (s_playSampleRate == 0) return 0.f;
        return (float)s_playBuffer.size() / (float)s_playSampleRate;
    }

    // ── Seek ──────────────────────────────────────────────────────────────────
    void seekToProgress(float progress) {
        int total  = (int)s_playBuffer.size();
        int target = (int)(std::max(0.f, std::min(1.f, progress)) * total);
        bool wasPlaying = s_playing.load();

        _stopVoice();

        s_playPosition.store(target);

        if (wasPlaying || s_paused.load()) {
            s_paused = false;
            _submitAndStart();
            s_playing = true;
        }
    }

    void seekToSeconds(float seconds) {
        int target = (int)(seconds * s_playSampleRate);
        target = std::max(0, std::min((int)s_playBuffer.size(), target));
        seekToProgress((float)target / (float)std::max(1, (int)s_playBuffer.size()));
    }

    // ── Finish callback ───────────────────────────────────────────────────────
    void setOnFinished(FinishCallback cb) { s_finishCallback = std::move(cb); }

    // ── File playback ─────────────────────────────────────────────────────────
    bool playFromPath(const std::string& path) {
        std::vector<float> samples;
        int sampleRate = 44100;
        if (!_decodeFile(path, samples, sampleRate)) {
            AUDIO_LOGE("playFromPath: failed to decode '%s'", path.c_str());
            return false;
        }
        return playPCM(samples, sampleRate);
    }

    // ── Pause / Resume ────────────────────────────────────────────────────────
    void pause() {
        if (!s_playing.load()) return;
        if (s_sourceVoice) {
            // Capture position before stopping
            XAUDIO2_VOICE_STATE vs{};
            s_sourceVoice->GetState(&vs);
            // samplesPlayed is from the start of the last SubmitSourceBuffer
            int consumed = (int)vs.SamplesPlayed;
            int newPos = std::min(s_submitOffset + consumed,
                                  (int)s_playBuffer.size());
            s_playPosition.store(newPos);
            s_sourceVoice->Stop();
        }
        s_playing = false;
        s_paused  = true;
    }

    void resume() {
        if (!s_paused.load()) return;
        s_paused = false;
        _submitAndStart();
        s_playing = true;
    }

    bool isPaused() const { return s_paused.load(); }
    bool isPlaying() const { return s_playing.load(); }

    // ── PCM playback ──────────────────────────────────────────────────────────
    bool playPCM(const std::vector<float>& samples, int sampleRate = 44100) {
        closePlayback();
        s_playBuffer    = samples;
        s_playPosition  = 0;
        s_submitOffset  = 0;
        s_playSampleRate = sampleRate;
        s_didFireFinish = false;
        s_paused        = false;

        if (!_ensureEngine()) return false;
        if (!_createSourceVoice(sampleRate)) return false;
        _submitAndStart();
        s_playing = true;
        return true;
    }

    void stopPlayback() {
        _stopVoice();
        s_playing = false;
        s_paused  = false;
    }

    void closePlayback() {
        stopPlayback();
        if (s_sourceVoice) {
            s_sourceVoice->DestroyVoice();
            s_sourceVoice = nullptr;
        }
    }

    void shutdown() { closePlayback(); }

    // ── Waveform (shared utility) ─────────────────────────────────────────────
    static std::vector<std::pair<int,int>> buildWaveform(
            const float* samples, int count,
            int x, int y, int w, int h, int points = 200)
    {
        std::vector<std::pair<int,int>> result;
        if (count == 0 || points <= 0) return result;
        result.reserve(points);
        float step  = (float)count / points;
        int   halfH = h / 2;
        int   midY  = y + halfH;
        for (int i = 0; i < points; i++) {
            int start = (int)(i * step);
            int end   = std::min((int)((i+1)*step), count);
            float peak = 0.f;
            for (int j = start; j < end; j++)
                peak = std::max(peak, std::abs(samples[j]));
            result.emplace_back(x + (i * w / points),
                                midY - (int)(peak * halfH));
        }
        return result;
    }

    static std::vector<std::pair<int,int>> buildWaveform(
            const std::vector<float>& samples,
            int x, int y, int w, int h, int points = 200)
    {
        return buildWaveform(samples.data(), (int)samples.size(),
                             x, y, w, h, points);
    }

    // stubs so AudioPlayerWidget compiles (recording not implemented on Win32 yet)
    bool isRecording()                        const { return false; }
    float getInputLevel()                     const { return 0.f; }

private:
    FluxAudio() = default;
    ~FluxAudio() { shutdown(); }

    // ── State ─────────────────────────────────────────────────────────────────
    Microsoft::WRL::ComPtr<IXAudio2>        s_xaudio2;
    IXAudio2MasteringVoice*                 s_masterVoice  = nullptr;
    IXAudio2SourceVoice*                    s_sourceVoice  = nullptr;
    FLXVoiceCallback                        s_voiceCB;

    std::vector<float>   s_playBuffer;
    std::atomic<int>     s_playPosition  {0};
    int                  s_submitOffset  = 0;   // buffer offset of last SubmitSourceBuffer
    int                  s_playSampleRate = 44100;

    std::atomic<bool>    s_playing       {false};
    std::atomic<bool>    s_paused        {false};
    std::atomic<bool>    s_didFireFinish {false};
    std::atomic<float>   s_volume        {1.0f};

    FinishCallback       s_finishCallback;

    // ── XAudio2 init ──────────────────────────────────────────────────────────
    bool _ensureEngine() {
        if (s_xaudio2) return true;

        CoInitializeEx(nullptr, COINIT_MULTITHREADED);   // safe to call multiple times

        UINT32 flags = 0;
        HRESULT hr = XAudio2Create(s_xaudio2.GetAddressOf(), flags);
        if (FAILED(hr)) { AUDIO_LOGE("XAudio2Create failed: 0x%08X", hr); return false; }

        hr = s_xaudio2->CreateMasteringVoice(&s_masterVoice);
        if (FAILED(hr)) { AUDIO_LOGE("CreateMasteringVoice failed: 0x%08X", hr); return false; }

        return true;
    }

    bool _createSourceVoice(int sampleRate) {
        if (s_sourceVoice) {
            s_sourceVoice->DestroyVoice();
            s_sourceVoice = nullptr;
        }

        WAVEFORMATEX wfx{};
        wfx.wFormatTag      = WAVE_FORMAT_IEEE_FLOAT;
        wfx.nChannels       = 1;
        wfx.nSamplesPerSec  = (DWORD)sampleRate;
        wfx.wBitsPerSample  = 32;
        wfx.nBlockAlign     = (wfx.nChannels * wfx.wBitsPerSample) / 8;
        wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;
        wfx.cbSize          = 0;

        // Wire up the finish callback via the voice callback object
        s_voiceCB.onStreamEnd = [this]() {
            s_playing = false;
            s_playPosition.store((int)s_playBuffer.size());  // mark fully played
            bool expected = false;
            if (s_didFireFinish.compare_exchange_strong(expected, true))
                if (s_finishCallback) s_finishCallback();
        };

        HRESULT hr = s_xaudio2->CreateSourceVoice(
            &s_sourceVoice, &wfx,
            0, XAUDIO2_DEFAULT_FREQ_RATIO,
            &s_voiceCB);
        if (FAILED(hr)) {
            AUDIO_LOGE("CreateSourceVoice failed: 0x%08X", hr);
            s_sourceVoice = nullptr;
            return false;
        }

        s_sourceVoice->SetVolume(s_volume.load());
        return true;
    }

    // Submit from s_playPosition to end of buffer, then start the voice
    void _submitAndStart() {
        if (!s_sourceVoice) return;

        s_sourceVoice->FlushSourceBuffers();

        int start = s_playPosition.load();
        int total = (int)s_playBuffer.size();
        if (start >= total) return;

        s_submitOffset = start;

        XAUDIO2_BUFFER buf{};
        buf.AudioBytes = (UINT32)((total - start) * sizeof(float));
        buf.pAudioData = reinterpret_cast<const BYTE*>(s_playBuffer.data() + start);
        buf.Flags      = XAUDIO2_END_OF_STREAM;
        // We drive s_playPosition ourselves via a periodic query in getProgress(),
        // so no pContext tricks needed.

        s_sourceVoice->SubmitSourceBuffer(&buf);
        s_sourceVoice->Start();

        // Kick off a background thread to update s_playPosition at ~30 Hz
        // so that AudioPlayerWidget::render() sees smooth progress.
        _startProgressThread();
    }

    void _stopVoice() {
        if (!s_sourceVoice) return;
        // Snapshot position before stopping
        XAUDIO2_VOICE_STATE vs{};
        s_sourceVoice->GetState(&vs);
        int newPos = std::min(s_submitOffset + (int)vs.SamplesPlayed,
                              (int)s_playBuffer.size());
        s_playPosition.store(newPos);
        s_sourceVoice->Stop();
        s_sourceVoice->FlushSourceBuffers();
    }

    // ── Progress polling thread ───────────────────────────────────────────────
    // XAudio2 doesn't have a per-frame callback like AAudio, so we poll.
    std::thread           s_progressThread;
    std::atomic<bool>     s_progressRunning {false};

    void _startProgressThread() {
        if (s_progressRunning.load()) return;
        s_progressRunning = true;

        s_progressThread = std::thread([this]() {
            while (s_progressRunning.load()) {
                if (s_sourceVoice && s_playing.load()) {
                    XAUDIO2_VOICE_STATE vs{};
                    s_sourceVoice->GetState(&vs);
                    int newPos = std::min(s_submitOffset + (int)vs.SamplesPlayed,
                                         (int)s_playBuffer.size());
                    s_playPosition.store(newPos);
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(33));  // ~30 Hz
            }
        });
        s_progressThread.detach();
    }

    void _stopProgressThread() {
        s_progressRunning = false;
        // thread is detached — it will exit on its own within 33 ms
    }

    // ── File decode (identical logic to Android) ──────────────────────────────
    static bool _isMp3(const std::string& p) {
        if (p.size() < 4) return false;
        std::string e = p.substr(p.size()-4);
        for (auto& c : e) c=(char)tolower(c);
        return e == ".mp3";
    }
    static bool _isWav(const std::string& p) {
        if (p.size() < 4) return false;
        std::string e = p.substr(p.size()-4);
        for (auto& c : e) c=(char)tolower(c);
        return e == ".wav";
    }

    static std::vector<uint8_t> _loadBytes(const std::string& path) {
        FILE* f = fopen(path.c_str(), "rb");
        if (!f) return {};
        fseek(f, 0, SEEK_END);
        size_t len = (size_t)ftell(f);
        fseek(f, 0, SEEK_SET);
        std::vector<uint8_t> buf(len);
        fread(buf.data(), 1, len, f);
        fclose(f);
        return buf;
    }

    bool _decodeFile(const std::string& path,
                     std::vector<float>& out, int& outSR)
    {
        auto raw = _loadBytes(path);
        if (raw.empty()) return false;

        if (_isMp3(path)) {
            drmp3_config cfg{};
            drmp3_uint64 frames = 0;
            float* pcm = drmp3_open_memory_and_read_pcm_frames_f32(
                raw.data(), raw.size(), &cfg, &frames, nullptr);
            if (!pcm) return false;
            outSR = (int)cfg.sampleRate;
            _mixdownToMono(pcm, (int)frames, (int)cfg.channels, out);
            drmp3_free(pcm, nullptr);
            return true;
        }
        if (_isWav(path)) {
            drwav_uint64 frames = 0;
            unsigned ch = 0, sr = 0;
            float* pcm = drwav_open_memory_and_read_pcm_frames_f32(
                raw.data(), raw.size(), &ch, &sr, &frames, nullptr);
            if (!pcm) return false;
            outSR = (int)sr;
            _mixdownToMono(pcm, (int)frames, (int)ch, out);
            drwav_free(pcm, nullptr);
            return true;
        }
        return false;
    }

    static void _mixdownToMono(const float* src, int frames, int channels,
                               std::vector<float>& out)
    {
        out.resize(frames);
        if (channels == 1) {
            memcpy(out.data(), src, frames * sizeof(float));
        } else {
            float inv = 1.f / channels;
            for (int i = 0; i < frames; i++) {
                float sum = 0.f;
                for (int c = 0; c < channels; c++) sum += src[i*channels+c];
                out[i] = sum * inv;
            }
        }
    }
};

#endif // _WIN32