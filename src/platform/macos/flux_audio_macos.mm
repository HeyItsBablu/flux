// flux_audio_macos.mm
// macOS AVAudioEngine audio backend for FluxAudio.
//
// This file MUST be compiled as Objective-C++ (.mm).
// It is the only file in the project that may include AVFoundation headers.
//
// Current status: PCM + file playback, seek, pause/resume, volume,
//                 stream playback (used by FluxVideo).
//                 Recording not implemented; stubs are in flux_audio.hpp.
//
// Link against: AVFoundation  AudioToolbox  (already in CMakeLists Apple block)
//
#if defined(__APPLE__)
#include <TargetConditionals.h>
#if TARGET_OS_OSX

#import <AVFoundation/AVFoundation.h>
#import <Foundation/Foundation.h>

#include "flux/flux_audio.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

#include "../../external/dr/dr_mp3.h"
#include "../../external/dr/dr_wav.h"

#define AUDIO_LOGI(fmt, ...) fprintf(stderr, "[FluxAudio] " fmt "\n", ##__VA_ARGS__)
#define AUDIO_LOGE(fmt, ...) AUDIO_LOGI(fmt, ##__VA_ARGS__)

// ============================================================================
// FluxAudio::Impl
// ============================================================================
struct FluxAudio::Impl {
    // ── AVAudioEngine graph ───────────────────────────────────────────────────
    AVAudioEngine*       engine       = nil;
    AVAudioPlayerNode*   playerNode   = nil;
    AVAudioMixerNode*    mixerNode    = nil;

    // ── Playback state ────────────────────────────────────────────────────────
    std::vector<float>   playBuffer;
    std::atomic<int>     playPosition    { 0 };
    int                  playSampleRate  = 44100;
    std::atomic<float>   volume          { 1.0f };
    std::atomic<bool>    playing         { false };
    std::atomic<bool>    paused          { false };
    std::atomic<bool>    didFireFinish   { false };
    FinishCallback       finishCallback;

    // ── Stream callback (used by FluxVideo) ───────────────────────────────────
    StreamCallback       streamCallback;
    std::thread          streamThread;
    std::atomic<bool>    streamRunning   { false };

    // ── Progress polling ──────────────────────────────────────────────────────
    std::thread          progressThread;
    std::atomic<bool>    progressRunning { false };

    // ── Guard for AVAudioEngine mutations ────────────────────────────────────
    std::mutex           engineMutex;

    // ── File helpers ──────────────────────────────────────────────────────────
    static bool isMp3(const std::string& p) {
        if (p.size() < 4) return false;
        std::string e = p.substr(p.size() - 4);
        for (auto& c : e) c = (char)tolower(c);
        return e == ".mp3";
    }
    static bool isWav(const std::string& p) {
        if (p.size() < 4) return false;
        std::string e = p.substr(p.size() - 4);
        for (auto& c : e) c = (char)tolower(c);
        return e == ".wav";
    }

    static std::vector<uint8_t> loadBytes(const std::string& path) {
        FILE* f = fopen(path.c_str(), "rb");
        if (!f) return {};
        fseek(f, 0, SEEK_END);
        size_t len = (size_t)ftell(f);
        rewind(f);
        std::vector<uint8_t> buf(len);
        fread(buf.data(), 1, len, f);
        fclose(f);
        return buf;
    }

    static void mixdownToMono(const float* src, int frames, int channels,
                               std::vector<float>& out)
    {
        out.resize(frames);
        if (channels == 1) {
            memcpy(out.data(), src, frames * sizeof(float));
        } else {
            float inv = 1.f / (float)channels;
            for (int i = 0; i < frames; i++) {
                float sum = 0.f;
                for (int c = 0; c < channels; c++)
                    sum += src[i * channels + c];
                out[i] = sum * inv;
            }
        }
    }

    bool decodeFile(const std::string& path, std::vector<float>& out,
                    int& outSR)
    {
        auto raw = loadBytes(path);
        if (raw.empty()) return false;
        if (isMp3(path)) {
            drmp3_config cfg{}; drmp3_uint64 frames = 0;
            float* pcm = drmp3_open_memory_and_read_pcm_frames_f32(
                    raw.data(), raw.size(), &cfg, &frames, nullptr);
            if (!pcm) return false;
            outSR = (int)cfg.sampleRate;
            mixdownToMono(pcm, (int)frames, (int)cfg.channels, out);
            drmp3_free(pcm, nullptr); return true;
        }
        if (isWav(path)) {
            drwav_uint64 frames = 0; unsigned ch = 0, sr = 0;
            float* pcm = drwav_open_memory_and_read_pcm_frames_f32(
                    raw.data(), raw.size(), &ch, &sr, &frames, nullptr);
            if (!pcm) return false;
            outSR = (int)sr;
            mixdownToMono(pcm, (int)frames, (int)ch, out);
            drwav_free(pcm, nullptr); return true;
        }
        return false;
    }

    // ── Engine setup ──────────────────────────────────────────────────────────
    bool ensureEngine(int sampleRate) {
        std::lock_guard<std::mutex> lk(engineMutex);
        if (engine && [engine isRunning]) return true;

        engine     = [[AVAudioEngine alloc] init];
        playerNode = [[AVAudioPlayerNode alloc] init];
        mixerNode  = engine.mainMixerNode;

        [engine attachNode:playerNode];

        AVAudioFormat* fmt = [[AVAudioFormat alloc]
                initWithCommonFormat:AVAudioPCMFormatFloat32
                          sampleRate:(double)sampleRate
                            channels:1
                         interleaved:NO];

        [engine connect:playerNode to:mixerNode format:fmt];
        mixerNode.outputVolume = volume.load();

        NSError* err = nil;
        if (![engine startAndReturnError:&err]) {
            AUDIO_LOGE("AVAudioEngine start failed: %s",
                       err.localizedDescription.UTF8String ?: "unknown");
            engine     = nil;
            playerNode = nil;
            return false;
        }
        return true;
    }

    void teardownEngine() {
        std::lock_guard<std::mutex> lk(engineMutex);
        if (!engine) return;
        if ([engine isRunning]) [engine stop];
        if (playerNode) [engine detachNode:playerNode];
        engine     = nil;
        playerNode = nil;
    }

    // ── Submit the current playBuffer from playPosition as an AVAudioPCMBuffer ─
    void submitPCMBuffer(bool completion) {
        if (!playerNode || !engine) return;

        int start = playPosition.load();
        int total = (int)playBuffer.size();
        if (start >= total) return;

        int frames = total - start;

        AVAudioFormat* fmt = [[AVAudioFormat alloc]
                initWithCommonFormat:AVAudioPCMFormatFloat32
                          sampleRate:(double)playSampleRate
                            channels:1
                         interleaved:NO];

        AVAudioPCMBuffer* buf =
            [[AVAudioPCMBuffer alloc] initWithPCMFormat:fmt
                                          frameCapacity:(AVAudioFrameCount)frames];
        buf.frameLength = (AVAudioFrameCount)frames;

        float* dst = buf.floatChannelData[0];
        float  vol = volume.load();
        const float* src = playBuffer.data() + start;
        for (int i = 0; i < frames; i++) dst[i] = src[i] * vol;

        if (completion) {
            __weak AVAudioPlayerNode* weakNode = playerNode;
            [playerNode scheduleBuffer:buf
                     completionHandler:^{
                // This fires on an AVAudioEngine internal thread.
                // Check the node is still valid before using it.
                if (!weakNode) return;
                playing = false;
                playPosition.store(total);
                bool expected = false;
                if (didFireFinish.compare_exchange_strong(expected, true))
                    if (finishCallback) finishCallback();
            }];
        } else {
            [playerNode scheduleBuffer:buf completionHandler:nil];
        }
    }

    // ── Stream feed thread (for FluxVideo) ────────────────────────────────────
    //
    // Keeps the AVAudioPlayerNode fed by scheduling short PCM buffers.
    // Stops when streamCallback returns 0 frames.
    //
    static constexpr int kStreamFrames = 2048;

    void streamFeedLoop() {
        if (!playerNode || !engine) return;

        AVAudioFormat* fmt = [[AVAudioFormat alloc]
                initWithCommonFormat:AVAudioPCMFormatFloat32
                          sampleRate:(double)playSampleRate
                            channels:1
                         interleaved:NO];

        // Pre-schedule two buffers before starting playback
        for (int pre = 0; pre < 2; pre++) {
            AVAudioPCMBuffer* buf =
                [[AVAudioPCMBuffer alloc] initWithPCMFormat:fmt
                                              frameCapacity:kStreamFrames];
            int got = streamCallback
                    ? streamCallback(buf.floatChannelData[0], kStreamFrames) : 0;
            buf.frameLength = (AVAudioFrameCount)(got > 0 ? got : kStreamFrames);
            if (got == 0) memset(buf.floatChannelData[0], 0,
                                 kStreamFrames * sizeof(float));
            [playerNode scheduleBuffer:buf completionHandler:nil];
        }

        [playerNode play];

        while (streamRunning.load()) {
            if (paused.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }

            AVAudioPCMBuffer* buf =
                [[AVAudioPCMBuffer alloc] initWithPCMFormat:fmt
                                              frameCapacity:kStreamFrames];
            int got = streamCallback
                    ? streamCallback(buf.floatChannelData[0], kStreamFrames) : 0;

            if (got == 0) {
                // End of stream — let the queued buffers drain
                buf.frameLength = kStreamFrames;
                memset(buf.floatChannelData[0], 0, kStreamFrames * sizeof(float));
                [playerNode scheduleBuffer:buf completionHandler:nil];
                break;
            }

            buf.frameLength = (AVAudioFrameCount)got;
            [playerNode scheduleBuffer:buf completionHandler:nil];

            // Throttle: don't get too far ahead of playback
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        streamRunning = false;
        playing       = false;
        bool expected = false;
        if (didFireFinish.compare_exchange_strong(expected, true))
            if (finishCallback) finishCallback();
    }

    // ── Progress polling ──────────────────────────────────────────────────────
    void startProgressThread() {
        if (progressRunning.load()) return;
        progressRunning = true;
        progressThread  = std::thread([this]() {
            while (progressRunning.load()) {
                if (playing.load() && playerNode) {
                    // AVAudioPlayerNode.lastRenderTime / playerTime gives sample position
                    AVAudioTime* nodeTime = playerNode.lastRenderTime;
                    if (nodeTime) {
                        AVAudioTime* playerTime =
                            [playerNode playerTimeForNodeTime:nodeTime];
                        if (playerTime)
                            playPosition.store(
                                    (int)playerTime.sampleTime +
                                    (int)(playBuffer.size() == 0
                                          ? 0 : /* stream */ 0));
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(33));
            }
        });
        progressThread.detach();
    }

    void stopProgressThread() { progressRunning = false; }

    void stopAllThreads() {
        // Stop stream thread
        streamRunning = false;
        if (streamThread.joinable()) streamThread.join();
        streamCallback = nullptr;

        stopProgressThread();
    }
};

// ============================================================================
// FluxAudio public API
// ============================================================================

FluxAudio& FluxAudio::get() { static FluxAudio inst; return inst; }
FluxAudio::FluxAudio()  : m_impl(new Impl()) {}
FluxAudio::~FluxAudio() { shutdown(); delete m_impl; }

void FluxAudio::setVolume(float v) {
    m_impl->volume.store(std::max(0.f, std::min(1.f, v)));
    if (m_impl->mixerNode)
        m_impl->mixerNode.outputVolume = m_impl->volume.load();
}
float FluxAudio::getVolume() const { return m_impl->volume.load(); }

float FluxAudio::getProgress() const {
    int total = (int)m_impl->playBuffer.size();
    if (total == 0) return 0.f;
    return std::min(1.f, (float)m_impl->playPosition.load() / (float)total);
}
float FluxAudio::getPositionSeconds() const {
    if (m_impl->playSampleRate == 0) return 0.f;
    return (float)m_impl->playPosition.load() / (float)m_impl->playSampleRate;
}
float FluxAudio::getDurationSeconds() const {
    if (m_impl->playSampleRate == 0) return 0.f;
    return (float)m_impl->playBuffer.size() / (float)m_impl->playSampleRate;
}

void FluxAudio::seekToProgress(float progress) {
    int total  = (int)m_impl->playBuffer.size();
    int target = (int)(std::max(0.f, std::min(1.f, progress)) * total);
    bool wasPlaying = m_impl->playing.load();

    // Stop and re-schedule from new position
    if (m_impl->playerNode) [m_impl->playerNode stop];
    m_impl->playPosition.store(target);

    if (wasPlaying || m_impl->paused.load()) {
        m_impl->paused = false;
        m_impl->submitPCMBuffer(true);
        [m_impl->playerNode play];
        m_impl->playing = true;
    }
}
void FluxAudio::seekToSeconds(float seconds) {
    int target = (int)(seconds * m_impl->playSampleRate);
    target = std::max(0, std::min((int)m_impl->playBuffer.size(), target));
    seekToProgress((float)target /
                   (float)std::max(1, (int)m_impl->playBuffer.size()));
}

void FluxAudio::setOnFinished(FinishCallback cb) {
    m_impl->finishCallback = std::move(cb);
}

bool FluxAudio::playFromPath(const std::string& path) {
    std::vector<float> samples; int sr = 44100;
    if (!m_impl->decodeFile(path, samples, sr)) {
        AUDIO_LOGE("playFromPath: failed to decode '%s'", path.c_str());
        return false;
    }
    return playPCM(samples, sr);
}

void FluxAudio::pause() {
    if (!m_impl->playing.load()) return;
    if (m_impl->playerNode) [m_impl->playerNode pause];
    m_impl->playing = false;
    m_impl->paused  = true;
}
void FluxAudio::resume() {
    if (!m_impl->paused.load()) return;
    if (m_impl->playerNode) [m_impl->playerNode play];
    m_impl->paused  = false;
    m_impl->playing = true;
}
bool FluxAudio::isPaused()  const { return m_impl->paused.load(); }
bool FluxAudio::isPlaying() const { return m_impl->playing.load(); }

bool FluxAudio::playPCM(const std::vector<float>& samples, int sampleRate) {
    closePlayback();

    m_impl->playBuffer      = samples;
    m_impl->playPosition    = 0;
    m_impl->playSampleRate  = sampleRate;
    m_impl->didFireFinish   = false;
    m_impl->paused          = false;

    if (!m_impl->ensureEngine(sampleRate)) return false;

    m_impl->submitPCMBuffer(true);
    [m_impl->playerNode play];
    m_impl->playing = true;
    m_impl->startProgressThread();
    return true;
}

bool FluxAudio::playStream(StreamCallback cb, int sampleRate) {
    closePlayback();

    m_impl->streamCallback = std::move(cb);
    m_impl->playSampleRate = sampleRate;
    m_impl->didFireFinish  = false;
    m_impl->paused         = false;

    if (!m_impl->ensureEngine(sampleRate)) return false;

    m_impl->playing       = true;
    m_impl->streamRunning = true;
    m_impl->streamThread  = std::thread(&Impl::streamFeedLoop, m_impl);
    return true;
}

bool FluxAudio::startPlayback() { return true; } // managed by playPCM/playStream

void FluxAudio::stopPlayback() {
    if (m_impl->playerNode) [m_impl->playerNode stop];
    m_impl->playing = false;
    m_impl->paused  = false;
}

void FluxAudio::closePlayback() {
    m_impl->stopAllThreads();
    stopPlayback();
    m_impl->teardownEngine();
}

void FluxAudio::shutdown() { closePlayback(); }

#endif // TARGET_OS_OSX
#endif // __APPLE__