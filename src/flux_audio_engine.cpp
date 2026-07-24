// flux_audio_engine.cpp
//
// miniaudio-backed implementation of AudioEngine. See flux_audio_engine.hpp
// for the design summary.
//
// Build note: define MINIAUDIO_IMPLEMENTATION in exactly one translation
// unit in the whole program. This file is that unit.
//
#define MINIAUDIO_IMPLEMENTATION
#include "../external/miniaudio/miniaudio.h"

#include "flux/flux_audio_engine.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

// ============================================================================
// Decoded sample storage
// ============================================================================

struct DecodedSample
{
    std::vector<float> interleaved; // engine format: float32, channelCount interleaved
    uint32_t channels = 0;
    uint32_t sampleRate = 0;
    uint64_t frameCount = 0; // frames, not samples (frame = one sample per channel)
};

// ============================================================================
// Voice pool
// ============================================================================
//
// A Voice's playback fields (samplePtr, framePos, gain, pan, looping) are
// touched ONLY by the audio thread once a StartVoice command has been
// consumed. UI-thread reads of `active`/`progress` are safe because they're
// std::atomic and are only ever written by the audio thread after that point.
//
struct Voice
{
    std::atomic<bool> active{false};
    std::atomic<uint32_t> generation{1}; // bumped every time the slot is reused
    std::atomic<float> progress{0.f};    // 0..1, updated by audio thread each callback
    std::atomic<bool> paused{false};

    // Audio-thread-only fields below (never touched by UI thread directly)
    std::shared_ptr<DecodedSample> sample;
    uint64_t framePos = 0;
    float gain = 1.f;
    float pan = 0.f;
    bool loop = false;

    // Audio-thread-only. Set by applyCommand() when a StartVoice/
    // StartStreamVoice command's targetFrame lands mid-block; consumed
    // (and reset to 0) the next time mix() processes this voice.
    ma_uint32 pendingOffset = 0;

    // ── Streaming voice state (audio-thread-only) ────────────────────────────
    bool isStream = false;
    AudioEngine::StreamCallback streamCb;
    uint32_t streamSampleRate = 0;
    float prevSample = 0.f; // carries continuity across callback blocks
    std::vector<float> streamScratch;
};

// ============================================================================
// Command queue (single-producer / single-consumer ring buffer)
// ============================================================================

enum class CmdType : uint8_t
{
    StartVoice,
    StopVoice,
    StartStreamVoice,
    SetGain,
    SetPan,
    Seek,
    SetMasterVolume,
    PauseVoice,
    ResumeVoice,
};

struct Command
{
    CmdType type;
    uint32_t slot = 0;
    uint32_t generation = 0;
    std::shared_ptr<DecodedSample> sample; // only used by StartVoice
    AudioEngine::StreamCallback streamCb;  // only used by StartStreamVoice
    float floatArg = 0.f;
    uint32_t uintArg = 0; // sourceSampleRate for StartStreamVoice
    bool boolArg = false;

    // Absolute engine sample-time (AudioEngine::currentSampleTime() space)
    // this command should take effect at. 0 = apply as soon as possible,
    // matching every pre-existing call site.
    uint64_t targetFrame = 0;
};

class SpscCommandQueue
{
public:
    explicit SpscCommandQueue(size_t capacity) : m_buf(capacity) {}

    // UI thread only
    bool push(Command &&cmd)
    {
        size_t head = m_head.load(std::memory_order_relaxed);
        size_t nextHead = (head + 1) % m_buf.size();
        if (nextHead == m_tail.load(std::memory_order_acquire))
            return false; // full — caller drops the command (rare with sane capacity)
        m_buf[head] = std::move(cmd);
        m_head.store(nextHead, std::memory_order_release);
        return true;
    }

    // Audio thread only
    bool pop(Command &out)
    {
        size_t tail = m_tail.load(std::memory_order_relaxed);
        if (tail == m_head.load(std::memory_order_acquire))
            return false; // empty
        out = std::move(m_buf[tail]);
        m_tail.store((tail + 1) % m_buf.size(), std::memory_order_release);
        return true;
    }

private:
    std::vector<Command> m_buf;
    std::atomic<size_t> m_head{0};
    std::atomic<size_t> m_tail{0};
};

// ============================================================================
// AudioEngine::Impl
// ============================================================================

struct AudioEngine::Impl
{
    ma_device device{};
    bool deviceInitialized = false;

    uint32_t sampleRate = 48000;
    uint32_t channels = 2;

    std::vector<Voice> voices;
    SpscCommandQueue commands{1024};

    // Commands popped off the ring buffer whose targetFrame is beyond the
    // current block. Re-checked every mix() call. Audio-thread-only —
    // never touched from the UI thread.
    std::vector<Command> pendingStarts;

    std::atomic<float> masterVolume{1.0f};
    std::atomic<uint64_t> clockFrames{0};

    // Guards lazy auto-init (ensureInitialized()) so two threads calling
    // loadSample()/play() for the first time simultaneously can't both
    // race into init(). Never touched by the audio callback.
    std::mutex initMutex;

    // Sample bank — protected by a mutex. Only touched from the UI thread
    // (loadSample/unloadSample/getSampleDurationSeconds), never from the
    // audio callback, so this mutex never contends with the audio thread.
    std::mutex bankMutex;
    std::unordered_map<SampleID, std::shared_ptr<DecodedSample>> bank;
    SampleID nextSampleId = 1;

    // ── Handle packing ────────────────────────────────────────────────────────
    static VoiceHandle makeHandle(uint32_t slot, uint32_t generation)
    {
        return (static_cast<uint64_t>(generation) << 32) | slot;
    }
    static uint32_t handleSlot(VoiceHandle h) { return static_cast<uint32_t>(h & 0xFFFFFFFFu); }
    static uint32_t handleGen(VoiceHandle h) { return static_cast<uint32_t>(h >> 32); }

    // ── Decode helper (UI thread) ─────────────────────────────────────────────
    std::shared_ptr<DecodedSample> decode(ma_decoder_config cfg, ma_uint64 *outFrameCount,
                                          const void *memData, size_t memLen,
                                          const char *path)
    {
        ma_decoder decoder;
        ma_result result = memData
                               ? ma_decoder_init_memory(memData, memLen, &cfg, &decoder)
                               : ma_decoder_init_file(path, &cfg, &decoder);
        if (result != MA_SUCCESS)
            return nullptr;

        ma_uint64 frameCount = 0;
        ma_decoder_get_length_in_pcm_frames(&decoder, &frameCount);

        auto out = std::make_shared<DecodedSample>();
        out->channels = cfg.channels;
        out->sampleRate = cfg.sampleRate;
        out->interleaved.resize(static_cast<size_t>(frameCount) * cfg.channels);

        ma_uint64 framesRead = 0;
        ma_decoder_read_pcm_frames(&decoder, out->interleaved.data(), frameCount, &framesRead);
        out->frameCount = framesRead;
        out->interleaved.resize(static_cast<size_t>(framesRead) * cfg.channels);

        ma_decoder_uninit(&decoder);
        if (outFrameCount)
            *outFrameCount = framesRead;
        return (framesRead > 0) ? out : nullptr;
    }

    // ── Audio callback ────────────────────────────────────────────────────────
    static void dataCallback(ma_device *pDevice, void *pOutput, const void *, ma_uint32 frameCount)
    {
        auto *impl = static_cast<Impl *>(pDevice->pUserData);
        impl->mix(static_cast<float *>(pOutput), frameCount);
    }

    void mix(float *output, ma_uint32 frameCount)
    {
        uint64_t blockStart = clockFrames.load(std::memory_order_relaxed);
        uint64_t blockEnd = blockStart + frameCount;

        // Drain queued commands. Anything whose targetFrame falls beyond
        // this block is held in pendingStarts instead of applied now.
        Command cmd;
        while (commands.pop(cmd))
        {
            if (cmd.targetFrame >= blockEnd)
                pendingStarts.push_back(std::move(cmd));
            else
                applyCommand(cmd, blockStart);
        }

        // Sweep previously-held commands that are now due.
        for (size_t i = 0; i < pendingStarts.size();)
        {
            if (pendingStarts[i].targetFrame < blockEnd)
            {
                applyCommand(pendingStarts[i], blockStart);
                pendingStarts.erase(pendingStarts.begin() + i);
            }
            else
            {
                i++;
            }
        }

        std::memset(output, 0, sizeof(float) * frameCount * channels);

        float master = masterVolume.load(std::memory_order_relaxed);

        for (auto &v : voices)
        {
            if (!v.active.load(std::memory_order_relaxed))
                continue;
            if (v.paused.load(std::memory_order_relaxed))
                continue;

            // Consume this voice's start offset (0 for every voice except
            // one that was just started mid-block by applyCommand()).
            ma_uint32 startOffset = v.pendingOffset;
            v.pendingOffset = 0;

            // Equal-power pan law.
            float panClamped = std::max(-1.f, std::min(1.f, v.pan));
            float angle = (panClamped + 1.f) * 0.25f * 3.14159265f; // 0..pi/2
            float gainL = std::cos(angle) * v.gain * master;
            float gainR = std::sin(angle) * v.gain * master;

            if (v.isStream)
            {
                if (!v.streamCb)
                    continue; // reserved by UI thread, StartStreamVoice not yet applied

                uint32_t srcRate = v.streamSampleRate > 0 ? v.streamSampleRate : sampleRate;
                double ratio = (double)srcRate / (double)sampleRate;

                size_t neededSrc = (size_t)((double)frameCount * ratio) + 2;
                if (v.streamScratch.size() < neededSrc)
                    v.streamScratch.resize(neededSrc);

                int got = v.streamCb(v.streamScratch.data(), (int)neededSrc);
                if (got < 0)
                {
                    // Callback signaled completion — free the slot instead of
                    // idling here forever consuming a voice from the pool.
                    v.active.store(false, std::memory_order_release);
                    v.streamCb = nullptr;
                    continue;
                }
                for (size_t i = (size_t)std::max(got, 0); i < neededSrc; i++)
                    v.streamScratch[i] = 0.f;

                // Linear resample from source rate to engine rate, sample-and-hold
                // continuity (prevSample) carried across callback block boundaries.
                double pos = 0.0;

                for (ma_uint32 i = startOffset; i < frameCount; i++)
                {
                    size_t idx0 = (size_t)pos;
                    float frac = (float)(pos - (double)idx0);
                    float s0 = (idx0 == 0) ? v.prevSample : v.streamScratch[idx0 - 1];
                    float s1 = (idx0 < neededSrc) ? v.streamScratch[idx0] : s0;
                    float mono = s0 + (s1 - s0) * frac;

                    output[i * channels + 0] += mono * gainL;
                    if (channels > 1)
                        output[i * channels + 1] += mono * gainR;

                    pos += ratio;
                }

                size_t consumed = (size_t)pos;
                v.prevSample = (consumed > 0 && consumed <= neededSrc)
                                   ? v.streamScratch[consumed - 1]
                                   : v.streamScratch[neededSrc - 1];
                continue;
            }

            if (!v.sample)
                continue; // reserved by UI thread, StartVoice command not yet applied

            const DecodedSample &s = *v.sample;
            if (s.channels == 0 || s.frameCount == 0)
                continue;

            for (ma_uint32 i = startOffset; i < frameCount; i++)
            {
                if (v.framePos >= s.frameCount)
                {
                    if (v.loop)
                    {
                        v.framePos = 0;
                    }
                    else
                    {
                        v.active.store(false, std::memory_order_release);
                        v.sample.reset(); // release the shared_ptr on the audio thread —
                                          // acceptable here since this happens at most
                                          // once per voice lifetime, not per-sample.
                        break;
                    }
                }

                const float *frame = &s.interleaved[static_cast<size_t>(v.framePos) * s.channels];
                float sampL = frame[0];
                float sampR = (s.channels > 1) ? frame[1] : frame[0];

                output[i * channels + 0] += sampL * gainL;
                if (channels > 1)
                    output[i * channels + 1] += sampR * gainR;

                v.framePos++;
            }

            if (s.frameCount > 0)
                v.progress.store(
                    std::min(1.f, static_cast<float>(v.framePos) / static_cast<float>(s.frameCount)),
                    std::memory_order_relaxed);
        }

        clockFrames.fetch_add(frameCount, std::memory_order_relaxed);
    }

    void applyCommand(Command &cmd, uint64_t blockStart)
    {
        if (cmd.type == CmdType::SetMasterVolume)
        {
            masterVolume.store(cmd.floatArg, std::memory_order_relaxed);
            return;
        }

        if (cmd.slot >= voices.size())
            return;
        Voice &v = voices[cmd.slot];
        if (v.generation.load(std::memory_order_relaxed) != cmd.generation)
            return; // stale — voice slot was reused for something else

        // Clamp: if targetFrame is at/before this block's start (already
        // passed, or "ASAP"/0), start immediately with no offset.
        ma_uint32 offset = (cmd.targetFrame > blockStart)
                               ? static_cast<ma_uint32>(cmd.targetFrame - blockStart)
                               : 0;

        switch (cmd.type)
        {
        case CmdType::StartVoice:
            v.isStream = false;
            v.streamCb = nullptr;
            v.sample = std::move(cmd.sample);
            v.framePos = 0;
            v.gain = cmd.floatArg;
            v.loop = cmd.boolArg;
            v.progress.store(0.f, std::memory_order_relaxed);
            v.pendingOffset = offset;
            // v.active was already set true by play() on the UI thread.
            break;
        case CmdType::StartStreamVoice:
            v.sample.reset();
            v.isStream = true;
            v.streamCb = std::move(cmd.streamCb);
            v.streamSampleRate = cmd.uintArg;
            v.prevSample = 0.f;
            v.gain = cmd.floatArg;
            v.loop = false;
            v.progress.store(0.f, std::memory_order_relaxed);
            v.pendingOffset = offset;
            // v.active was already set true by playStream() on the UI thread.
            break;
        case CmdType::StopVoice:
            v.active.store(false, std::memory_order_release);
            v.sample.reset();
            v.isStream = false;
            v.streamCb = nullptr;
            break;
        case CmdType::SetGain:
            v.gain = cmd.floatArg;
            break;
        case CmdType::SetPan:
            v.pan = cmd.floatArg;
            break;
        case CmdType::Seek:
            if (v.sample)
                v.framePos = static_cast<uint64_t>(
                    std::max(0.f, std::min(1.f, cmd.floatArg)) * v.sample->frameCount);
            break;
        case CmdType::PauseVoice:
            v.paused.store(true, std::memory_order_relaxed);
            break;
        case CmdType::ResumeVoice:
            v.paused.store(false, std::memory_order_relaxed);
            break;
        default:
            break;
        }
    }
};

// ============================================================================
// AudioEngine public API
// ============================================================================

AudioEngine &AudioEngine::get()
{
    static AudioEngine inst;
    return inst;
}

AudioEngine::AudioEngine() : m_impl(new Impl()) {}
AudioEngine::~AudioEngine()
{
    shutdown();
    delete m_impl;
}

bool AudioEngine::init(uint32_t sampleRate, uint32_t channels, uint32_t maxVoices)
{
    if (m_impl->deviceInitialized)
        return true;

    m_impl->sampleRate = sampleRate;
    m_impl->channels = channels;
    m_impl->voices = std::vector<Voice>(maxVoices); // fixed pool, no runtime growth

    ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
    cfg.playback.format = ma_format_f32;
    cfg.playback.channels = channels;
    cfg.sampleRate = sampleRate;
    cfg.dataCallback = Impl::dataCallback;
    cfg.pUserData = m_impl;

    if (ma_device_init(nullptr, &cfg, &m_impl->device) != MA_SUCCESS)
        return false;

    if (ma_device_start(&m_impl->device) != MA_SUCCESS)
    {
        ma_device_uninit(&m_impl->device);
        return false;
    }

    // Reflect the device's actual negotiated format/rate (may differ from request).
    m_impl->sampleRate = m_impl->device.sampleRate;
    m_impl->channels = m_impl->device.playback.channels;

    m_impl->deviceInitialized = true;
    return true;
}

void AudioEngine::shutdown()
{
    if (!m_impl->deviceInitialized)
        return;
    ma_device_uninit(&m_impl->device);
    m_impl->deviceInitialized = false;

    std::lock_guard<std::mutex> lk(m_impl->bankMutex);
    m_impl->bank.clear();
    m_impl->voices.clear();
    m_impl->pendingStarts.clear();
}

bool AudioEngine::isInitialized() const { return m_impl->deviceInitialized; }

void AudioEngine::ensureInitialized()
{
    std::lock_guard<std::mutex> lk(m_impl->initMutex);
    if (!m_impl->deviceInitialized)
        init(); // falls back to the documented defaults (48kHz, stereo, 64 voices)
}

// ── Sample bank ────────────────────────────────────────────────────────────

SampleID AudioEngine::loadSample(const std::string &path)
{
    ensureInitialized();
    if (!m_impl->deviceInitialized)
        return kInvalidSample;

    ma_decoder_config cfg =
        ma_decoder_config_init(ma_format_f32, m_impl->channels, m_impl->sampleRate);
    auto decoded = m_impl->decode(cfg, nullptr, nullptr, 0, path.c_str());
    if (!decoded)
        return kInvalidSample;

    std::lock_guard<std::mutex> lk(m_impl->bankMutex);
    SampleID id = m_impl->nextSampleId++;
    m_impl->bank[id] = decoded;
    return id;
}

SampleID AudioEngine::loadSampleFromMemory(const uint8_t *data, size_t len)
{
    ensureInitialized();
    if (!m_impl->deviceInitialized || !data || len == 0)
        return kInvalidSample;

    ma_decoder_config cfg =
        ma_decoder_config_init(ma_format_f32, m_impl->channels, m_impl->sampleRate);
    auto decoded = m_impl->decode(cfg, nullptr, data, len, nullptr);
    if (!decoded)
        return kInvalidSample;

    std::lock_guard<std::mutex> lk(m_impl->bankMutex);
    SampleID id = m_impl->nextSampleId++;
    m_impl->bank[id] = decoded;
    return id;
}

void AudioEngine::unloadSample(SampleID id)
{
    std::lock_guard<std::mutex> lk(m_impl->bankMutex);
    // Any voice currently playing this sample holds its own shared_ptr, so
    // erasing here just removes the bank's reference — playback in progress
    // is unaffected and the memory is freed once that voice finishes.
    m_impl->bank.erase(id);
}

float AudioEngine::getSampleDurationSeconds(SampleID id) const
{
    std::lock_guard<std::mutex> lk(m_impl->bankMutex);
    auto it = m_impl->bank.find(id);
    if (it == m_impl->bank.end() || it->second->sampleRate == 0)
        return 0.f;
    return static_cast<float>(it->second->frameCount) / static_cast<float>(it->second->sampleRate);
}

bool AudioEngine::isSampleValid(SampleID id) const
{
    std::lock_guard<std::mutex> lk(m_impl->bankMutex);
    return m_impl->bank.find(id) != m_impl->bank.end();
}

// ── Voices ─────────────────────────────────────────────────────────────────

VoiceHandle AudioEngine::play(SampleID sample, float gain, float pan, bool loop)
{
    return play(sample, gain, pan, loop, 0);
}

VoiceHandle AudioEngine::play(SampleID sample, float gain, float pan, bool loop,
                              uint64_t targetSampleTime)
{
    ensureInitialized();
    if (!m_impl->deviceInitialized)
        return kInvalidVoice;

    std::shared_ptr<DecodedSample> decoded;
    {
        std::lock_guard<std::mutex> lk(m_impl->bankMutex);
        auto it = m_impl->bank.find(sample);
        if (it == m_impl->bank.end())
            return kInvalidVoice;
        decoded = it->second;
    }

    // Find a free slot. Single producer (UI thread calls play()), so a plain
    // CAS against `active` is enough to avoid stomping a slot the audio
    // thread is concurrently freeing.
    for (uint32_t i = 0; i < m_impl->voices.size(); i++)
    {
        Voice &v = m_impl->voices[i];
        bool expected = false;
        if (v.active.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
        {
            uint32_t gen = v.generation.fetch_add(1, std::memory_order_relaxed) + 1;

            Command cmd;
            cmd.type = CmdType::StartVoice;
            cmd.slot = i;
            cmd.generation = gen;
            cmd.sample = decoded;
            cmd.floatArg = gain;
            cmd.boolArg = loop;
            cmd.targetFrame = targetSampleTime;

            if (!m_impl->commands.push(std::move(cmd)))
            {
                // Queue full (shouldn't happen with sane capacity) — back out.
                v.active.store(false, std::memory_order_release);
                return kInvalidVoice;
            }

            setVoicePan(Impl::makeHandle(i, gen), pan);
            return Impl::makeHandle(i, gen);
        }
    }
    return kInvalidVoice; // pool exhausted
}

VoiceHandle AudioEngine::playStream(StreamCallback cb, uint32_t sourceSampleRate,
                                    float gain, float pan)
{
    return playStream(std::move(cb), sourceSampleRate, gain, pan, 0);
}

VoiceHandle AudioEngine::playStream(StreamCallback cb, uint32_t sourceSampleRate,
                                    float gain, float pan, uint64_t targetSampleTime)
{
    ensureInitialized();
    if (!m_impl->deviceInitialized || !cb)
        return kInvalidVoice;

    // Same single-producer slot-claim strategy as play(): find a free voice,
    // CAS it active, then hand the actual payload to the audio thread via
    // the command queue rather than touching audio-thread-only fields here.
    for (uint32_t i = 0; i < m_impl->voices.size(); i++)
    {
        Voice &v = m_impl->voices[i];
        bool expected = false;
        if (v.active.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
        {
            uint32_t gen = v.generation.fetch_add(1, std::memory_order_relaxed) + 1;

            Command cmd;
            cmd.type = CmdType::StartStreamVoice;
            cmd.slot = i;
            cmd.generation = gen;
            cmd.streamCb = std::move(cb);
            cmd.uintArg = sourceSampleRate;
            cmd.floatArg = gain;
            cmd.targetFrame = targetSampleTime;

            if (!m_impl->commands.push(std::move(cmd)))
            {
                v.active.store(false, std::memory_order_release);
                return kInvalidVoice;
            }

            setVoicePan(Impl::makeHandle(i, gen), pan);
            return Impl::makeHandle(i, gen);
        }
    }
    return kInvalidVoice; // pool exhausted
}

void AudioEngine::stopVoice(VoiceHandle voice)
{
    if (voice == kInvalidVoice)
        return;
    Command cmd;
    cmd.type = CmdType::StopVoice;
    cmd.slot = Impl::handleSlot(voice);
    cmd.generation = Impl::handleGen(voice);
    m_impl->commands.push(std::move(cmd));
}

void AudioEngine::setVoiceGain(VoiceHandle voice, float gain)
{
    if (voice == kInvalidVoice)
        return;
    Command cmd;
    cmd.type = CmdType::SetGain;
    cmd.slot = Impl::handleSlot(voice);
    cmd.generation = Impl::handleGen(voice);
    cmd.floatArg = gain;
    m_impl->commands.push(std::move(cmd));
}

void AudioEngine::setVoicePan(VoiceHandle voice, float pan)
{
    if (voice == kInvalidVoice)
        return;
    Command cmd;
    cmd.type = CmdType::SetPan;
    cmd.slot = Impl::handleSlot(voice);
    cmd.generation = Impl::handleGen(voice);
    cmd.floatArg = pan;
    m_impl->commands.push(std::move(cmd));
}

void AudioEngine::seekVoice(VoiceHandle voice, float progress01)
{
    if (voice == kInvalidVoice)
        return;
    Command cmd;
    cmd.type = CmdType::Seek;
    cmd.slot = Impl::handleSlot(voice);
    cmd.generation = Impl::handleGen(voice);
    cmd.floatArg = progress01;
    m_impl->commands.push(std::move(cmd));
}

bool AudioEngine::isVoiceActive(VoiceHandle voice) const
{
    if (voice == kInvalidVoice)
        return false;
    uint32_t slot = Impl::handleSlot(voice);
    if (slot >= m_impl->voices.size())
        return false;
    const Voice &v = m_impl->voices[slot];
    if (v.generation.load(std::memory_order_relaxed) != Impl::handleGen(voice))
        return false; // stale handle — slot reused since
    return v.active.load(std::memory_order_relaxed);
}

float AudioEngine::getVoiceProgress(VoiceHandle voice) const
{
    if (voice == kInvalidVoice)
        return 0.f;
    uint32_t slot = Impl::handleSlot(voice);
    if (slot >= m_impl->voices.size())
        return 0.f;
    const Voice &v = m_impl->voices[slot];
    if (v.generation.load(std::memory_order_relaxed) != Impl::handleGen(voice))
        return 0.f;
    return v.progress.load(std::memory_order_relaxed);
}

void AudioEngine::pauseVoice(VoiceHandle voice)
{
    if (voice == kInvalidVoice)
        return;
    Command cmd;
    cmd.type = CmdType::PauseVoice;
    cmd.slot = Impl::handleSlot(voice);
    cmd.generation = Impl::handleGen(voice);
    m_impl->commands.push(std::move(cmd));
}

void AudioEngine::resumeVoice(VoiceHandle voice)
{
    if (voice == kInvalidVoice)
        return;
    Command cmd;
    cmd.type = CmdType::ResumeVoice;
    cmd.slot = Impl::handleSlot(voice);
    cmd.generation = Impl::handleGen(voice);
    m_impl->commands.push(std::move(cmd));
}

bool AudioEngine::isVoicePaused(VoiceHandle voice) const
{
    if (voice == kInvalidVoice)
        return false;
    uint32_t slot = Impl::handleSlot(voice);
    if (slot >= m_impl->voices.size())
        return false;
    const Voice &v = m_impl->voices[slot];
    if (v.generation.load(std::memory_order_relaxed) != Impl::handleGen(voice))
        return false;
    return v.paused.load(std::memory_order_relaxed);
}

// ── Master ─────────────────────────────────────────────────────────────────

void AudioEngine::setMasterVolume(float v)
{
    Command cmd;
    cmd.type = CmdType::SetMasterVolume;
    cmd.floatArg = std::max(0.f, std::min(1.f, v));
    m_impl->commands.push(std::move(cmd));
}

float AudioEngine::getMasterVolume() const
{
    return m_impl->masterVolume.load(std::memory_order_relaxed);
}

// ── Clock ──────────────────────────────────────────────────────────────────

uint64_t AudioEngine::currentSampleTime() const
{
    return m_impl->clockFrames.load(std::memory_order_relaxed);
}
uint32_t AudioEngine::sampleRate() const { return m_impl->sampleRate; }
uint32_t AudioEngine::channelCount() const { return m_impl->channels; }