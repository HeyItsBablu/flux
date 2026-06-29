// flux_audio_android.cpp
// Android AAudio audio backend for FluxAudio.
//
// Provides full playback and microphone recording via AAudio callbacks.
// Asset loading delegates to FluxAndroid_getAssetManager() for bundled files
// and fopen() for absolute filesystem paths.
//
// Link against: aaudio  log  android
//
#ifdef __ANDROID__

#include "flux/flux_audio.hpp"

#include <aaudio/AAudio.h>
#include <android/asset_manager.h>
#include <android/log.h>
#include <algorithm>
#include <cstring>
#include <vector>

#include "dr_mp3.h"
#include "dr_wav.h"

#define AUDIO_LOGI(...) __android_log_print(ANDROID_LOG_INFO, "FluxAudio", __VA_ARGS__)
#define AUDIO_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "FluxAudio", __VA_ARGS__)

extern AAssetManager *FluxAndroid_getAssetManager();

// ============================================================================
// FluxAudio::Impl
// ============================================================================
struct FluxAudio::Impl
{
    // ── Playback ──────────────────────────────────────────────────────────────
    AAudioStream *playStream = nullptr;
    std::vector<float> playBuffer;
    std::atomic<int> playPosition{0};
    PlayCallback playCallback;
    FinishCallback finishCallback;
    std::atomic<bool> playing{false};
    std::atomic<bool> didFireFinish{false};
    std::atomic<float> volume{1.0f};
    int playSampleRate = 44100;
    int playChannels = 1;

    // ── Recording ─────────────────────────────────────────────────────────────
    AAudioStream *recStream = nullptr;
    RecordCallback recCallback;
    std::vector<float> recBuffer;
    std::atomic<bool> recording{false};
    int recSampleRate = 44100;
    std::atomic<float> inputLevel{0.f};

    // ── File helpers ──────────────────────────────────────────────────────────
    static bool isMp3(const std::string &p)
    {
        if (p.size() < 4)
            return false;
        std::string e = p.substr(p.size() - 4);
        for (auto &c : e)
            c = (char)tolower(c);
        return e == ".mp3";
    }
    static bool isWav(const std::string &p)
    {
        if (p.size() < 4)
            return false;
        std::string e = p.substr(p.size() - 4);
        for (auto &c : e)
            c = (char)tolower(c);
        return e == ".wav";
    }

    static std::vector<uint8_t> loadBytes(const std::string &path)
    {
        if (!path.empty() && path[0] == '/')
        {
            FILE *f = fopen(path.c_str(), "rb");
            if (!f)
                return {};
            fseek(f, 0, SEEK_END);
            size_t len = (size_t)ftell(f);
            fseek(f, 0, SEEK_SET);
            std::vector<uint8_t> buf(len);
            fread(buf.data(), 1, len, f);
            fclose(f);
            return buf;
        }
        AAssetManager *am = FluxAndroid_getAssetManager();
        if (!am)
            return {};
        AAsset *asset = AAssetManager_open(am, path.c_str(), AASSET_MODE_BUFFER);
        if (!asset)
            return {};
        size_t len = (size_t)AAsset_getLength(asset);
        std::vector<uint8_t> buf(len);
        AAsset_read(asset, buf.data(), len);
        AAsset_close(asset);
        return buf;
    }

    static void mixdownToMono(const float *src, int frames, int channels,
                              std::vector<float> &out)
    {
        out.resize(frames);
        if (channels == 1)
        {
            memcpy(out.data(), src, frames * sizeof(float));
        }
        else
        {
            float inv = 1.f / channels;
            for (int i = 0; i < frames; i++)
            {
                float sum = 0.f;
                for (int c = 0; c < channels; c++)
                    sum += src[i * channels + c];
                out[i] = sum * inv;
            }
        }
    }

    bool decodeFile(const std::string &path, std::vector<float> &outSamples,
                    int &outSampleRate)
    {
        std::vector<uint8_t> raw = loadBytes(path);
        if (raw.empty())
        {
            AUDIO_LOGE("decodeFile: could not load '%s'", path.c_str());
            return false;
        }

        if (isMp3(path))
        {
            drmp3_config cfg{};
            drmp3_uint64 frameCount = 0;
            float *pcm = drmp3_open_memory_and_read_pcm_frames_f32(
                raw.data(), raw.size(), &cfg, &frameCount, nullptr);
            if (!pcm)
                return false;
            outSampleRate = (int)cfg.sampleRate;
            mixdownToMono(pcm, (int)frameCount, (int)cfg.channels, outSamples);
            drmp3_free(pcm, nullptr);
            return true;
        }
        if (isWav(path))
        {
            drwav_uint64 frameCount = 0;
            unsigned int channels = 0, sampleRate = 0;
            float *pcm = drwav_open_memory_and_read_pcm_frames_f32(
                raw.data(), raw.size(), &channels, &sampleRate,
                &frameCount, nullptr);
            if (!pcm)
                return false;
            outSampleRate = (int)sampleRate;
            mixdownToMono(pcm, (int)frameCount, (int)channels, outSamples);
            drwav_free(pcm, nullptr);
            return true;
        }
        AUDIO_LOGE("decodeFile: unsupported format '%s'", path.c_str());
        return false;
    }

    // ── AAudio playback callbacks ─────────────────────────────────────────────
    static aaudio_data_callback_result_t playDataCB(
        AAudioStream *, void *userData, void *audioData, int32_t numFrames)
    {
        auto *self = static_cast<Impl *>(userData);
        auto *output = static_cast<float *>(audioData);
        float vol = self->volume.load();

        if (!self->playing)
        {
            memset(output, 0, numFrames * sizeof(float));
            return AAUDIO_CALLBACK_RESULT_STOP;
        }

        bool finished = false;

        if (self->playCallback)
        {
            int written = self->playCallback(output, numFrames);
            for (int i = 0; i < written; i++)
                output[i] *= vol;
            if (written < numFrames)
                memset(output + written, 0, (numFrames - written) * sizeof(float));
            if (written == 0)
            {
                self->playing = false;
                finished = true;
            }
        }
        else
        {
            int pos = self->playPosition.load();
            int avail = (int)self->playBuffer.size() - pos;
            if (avail <= 0)
            {
                memset(output, 0, numFrames * sizeof(float));
                self->playing = false;
                finished = true;
            }
            else
            {
                int toCopy = std::min(avail, numFrames);
                memcpy(output, self->playBuffer.data() + pos,
                       toCopy * sizeof(float));
                for (int i = 0; i < toCopy; i++)
                    output[i] *= vol;
                if (toCopy < numFrames)
                    memset(output + toCopy, 0,
                           (numFrames - toCopy) * sizeof(float));
                self->playPosition.fetch_add(toCopy);
            }
        }

        if (finished)
        {
            bool expected = false;
            if (self->didFireFinish.compare_exchange_strong(expected, true))
                if (self->finishCallback)
                    self->finishCallback();
            return AAUDIO_CALLBACK_RESULT_STOP;
        }
        return AAUDIO_CALLBACK_RESULT_CONTINUE;
    }

    static aaudio_data_callback_result_t recDataCB(
        AAudioStream *, void *userData, void *audioData, int32_t numFrames)
    {
        auto *self = static_cast<Impl *>(userData);
        auto *input = static_cast<const float *>(audioData);

        if (!self->recording)
            return AAUDIO_CALLBACK_RESULT_STOP;

        float rms = 0.f;
        for (int i = 0; i < numFrames; i++)
            rms += input[i] * input[i];
        self->inputLevel.store(std::sqrt(rms / numFrames));

        if (self->recCallback)
            self->recCallback(input, numFrames);
        return AAUDIO_CALLBACK_RESULT_CONTINUE;
    }
};

// ============================================================================
// FluxAudio public API
// ============================================================================

FluxAudio &FluxAudio::get()
{
    static FluxAudio inst;
    return inst;
}
FluxAudio::FluxAudio() : m_impl(new Impl()) {}
FluxAudio::~FluxAudio()
{
    shutdown();
    delete m_impl;
}

void FluxAudio::setVolume(float v)
{
    m_impl->volume.store(std::max(0.f, std::min(1.f, v)));
}
float FluxAudio::getVolume() const { return m_impl->volume.load(); }

float FluxAudio::getProgress() const
{
    int total = (int)m_impl->playBuffer.size();
    if (total == 0)
        return 0.f;
    return std::min(1.f, (float)m_impl->playPosition.load() / (float)total);
}
float FluxAudio::getPositionSeconds() const
{
    if (m_impl->playSampleRate == 0)
        return 0.f;
    return (float)m_impl->playPosition.load() / (float)m_impl->playSampleRate;
}
float FluxAudio::getDurationSeconds() const
{
    if (m_impl->playSampleRate == 0)
        return 0.f;
    return (float)m_impl->playBuffer.size() / (float)m_impl->playSampleRate;
}

void FluxAudio::seekToProgress(float progress)
{
    int total = (int)m_impl->playBuffer.size();
    int target = (int)(std::max(0.f, std::min(1.f, progress)) * total);
    m_impl->playPosition.store(target);
}
void FluxAudio::seekToSeconds(float seconds)
{
    int target = (int)(seconds * m_impl->playSampleRate);
    target = std::max(0, std::min((int)m_impl->playBuffer.size(), target));
    m_impl->playPosition.store(target);
}

void FluxAudio::setOnFinished(FinishCallback cb)
{
    m_impl->finishCallback = std::move(cb);
}

bool FluxAudio::playFromPath(const std::string &path)
{
    std::vector<float> samples;
    int sampleRate = 44100;
    if (!m_impl->decodeFile(path, samples, sampleRate))
    {
        AUDIO_LOGE("playFromPath: failed to decode '%s'", path.c_str());
        return false;
    }
    return playPCM(samples, sampleRate);
}

void FluxAudio::pause()
{
    if (m_impl->playStream && m_impl->playing.load())
    {
        m_impl->playing = false;
        AAudioStream_requestPause(m_impl->playStream);
    }
}
void FluxAudio::resume()
{
    if (m_impl->playStream && !m_impl->playing.load() &&
        m_impl->playPosition.load() < (int)m_impl->playBuffer.size())
    {
        m_impl->playing = true;
        AAudioStream_requestStart(m_impl->playStream);
    }
}
bool FluxAudio::isPaused() const
{
    return m_impl->playStream &&
           !m_impl->playing.load() &&
           m_impl->playPosition.load() < (int)m_impl->playBuffer.size();
}
bool FluxAudio::isPlaying() const { return m_impl->playing.load(); }

bool FluxAudio::openPlayback(int sampleRate, int channels)
{
    if (m_impl->playStream)
        closePlayback();

    AAudioStreamBuilder *builder = nullptr;
    AAudio_createStreamBuilder(&builder);
    AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_OUTPUT);
    AAudioStreamBuilder_setSampleRate(builder, sampleRate);
    AAudioStreamBuilder_setChannelCount(builder, channels);
    AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_FLOAT);
    AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
    // Pass Impl* as user data so the static callback can reach it
    AAudioStreamBuilder_setDataCallback(builder, Impl::playDataCB, m_impl);

    aaudio_result_t result = AAudioStreamBuilder_openStream(builder, &m_impl->playStream);
    AAudioStreamBuilder_delete(builder);

    if (result != AAUDIO_OK)
    {
        AUDIO_LOGE("openPlayback failed: %s", AAudio_convertResultToText(result));
        m_impl->playStream = nullptr;
        return false;
    }
    m_impl->playSampleRate = AAudioStream_getSampleRate(m_impl->playStream);
    m_impl->playChannels = AAudioStream_getChannelCount(m_impl->playStream);
    return true;
}

bool FluxAudio::playPCM(const std::vector<float> &samples, int sampleRate)
{
    stopPlayback();
    closePlayback();

    m_impl->playBuffer = samples;
    m_impl->playPosition = 0;
    m_impl->playCallback = nullptr;
    m_impl->didFireFinish = false;

    if (!openPlayback(sampleRate))
        return false;
    return startPlayback();
}

bool FluxAudio::playStream(StreamCallback cb, int sampleRate)
{
    m_impl->playCallback = std::move(cb);
    m_impl->playBuffer.clear();
    m_impl->playPosition = 0;
    m_impl->didFireFinish = false;

    if (!m_impl->playStream && !openPlayback(sampleRate))
        return false;
    return startPlayback();
}

bool FluxAudio::startPlayback()
{
    if (!m_impl->playStream)
        return false;
    m_impl->playing = true;
    aaudio_result_t r = AAudioStream_requestStart(m_impl->playStream);
    if (r != AAUDIO_OK)
    {
        AUDIO_LOGE("startPlayback failed: %s", AAudio_convertResultToText(r));
        m_impl->playing = false;
        return false;
    }
    return true;
}

void FluxAudio::stopPlayback()
{
    m_impl->playing = false;
    if (m_impl->playStream)
        AAudioStream_requestStop(m_impl->playStream);
}
void FluxAudio::closePlayback()
{
    stopPlayback();
    if (m_impl->playStream)
    {
        AAudioStream_close(m_impl->playStream);
        m_impl->playStream = nullptr;
    }
}

void FluxAudio::shutdown()
{
    closePlayback();
    closeRecord();
}

// ── Recording ─────────────────────────────────────────────────────────────────

bool FluxAudio::openRecord(int sampleRate, int channels)
{
    if (m_impl->recStream)
        closeRecord();

    AAudioStreamBuilder *builder = nullptr;
    AAudio_createStreamBuilder(&builder);
    AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_INPUT);
    AAudioStreamBuilder_setSampleRate(builder, sampleRate);
    AAudioStreamBuilder_setChannelCount(builder, channels);
    AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_FLOAT);
    AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
    AAudioStreamBuilder_setDataCallback(builder, Impl::recDataCB, m_impl);

    aaudio_result_t result = AAudioStreamBuilder_openStream(builder, &m_impl->recStream);
    AAudioStreamBuilder_delete(builder);

    if (result != AAUDIO_OK)
    {
        AUDIO_LOGE("openRecord failed: %s", AAudio_convertResultToText(result));
        m_impl->recStream = nullptr;
        return false;
    }
    m_impl->recSampleRate = AAudioStream_getSampleRate(m_impl->recStream);
    return true;
}

bool FluxAudio::startRecord(RecordCallback cb)
{
    m_impl->recCallback = std::move(cb);
    m_impl->recording = true;
    m_impl->recBuffer.clear();

    if (!m_impl->recStream && !openRecord())
        return false;

    aaudio_result_t r = AAudioStream_requestStart(m_impl->recStream);
    if (r != AAUDIO_OK)
    {
        AUDIO_LOGE("startRecord failed: %s", AAudio_convertResultToText(r));
        m_impl->recording = false;
        return false;
    }
    return true;
}

bool FluxAudio::startRecordToBuffer()
{
    return startRecord([this](const float *samples, int count)
                       {
        for (int i = 0; i < count; i++)
            m_impl->recBuffer.push_back(samples[i]); });
}

void FluxAudio::stopRecord()
{
    m_impl->recording = false;
    if (m_impl->recStream)
        AAudioStream_requestStop(m_impl->recStream);
}
void FluxAudio::closeRecord()
{
    stopRecord();
    if (m_impl->recStream)
    {
        AAudioStream_close(m_impl->recStream);
        m_impl->recStream = nullptr;
    }
}

bool FluxAudio::isRecording() const { return m_impl->recording.load(); }
float FluxAudio::getInputLevel() const { return m_impl->inputLevel.load(); }
const std::vector<float> &FluxAudio::getRecording() const { return m_impl->recBuffer; }
void FluxAudio::clearRecording() { m_impl->recBuffer.clear(); }
int FluxAudio::getRecordSampleRate() const { return m_impl->recSampleRate; }

#endif // __ANDROID__