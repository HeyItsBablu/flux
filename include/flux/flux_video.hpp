// flux_video.hpp
// MediaCodec + SurfaceTexture + AAudio A/V sync engine for FluxUI on Android.
//
// Pipeline:
//   MediaExtractor  →  MediaCodec (video) → SurfaceTexture →
//   GL_TEXTURE_EXTERNAL_OES
//                  └→  MediaCodec (audio) → PCM float       → AAudio (via
//                  FluxAudio)
//
// All decoding runs on a single background thread (s_decodeThread).
// The main (GL) thread calls updateFrame() once per vsync to latch a new
// texture and retrieve the current A/V-synced position.
//
// Dependencies:
//   - flux_audio.hpp  (AAudio playback — audio path reuses it fully)
//   - libmediandk, libOpenSLES, libEGL, libGLESv2  (add to CMakeLists)
//   - GLES 3.0+ for GL_TEXTURE_EXTERNAL_OES (or GLES 2 + OES extension)
//
#pragma once
// flux_video.hpp  —  Android backend only
// MediaCodec + SurfaceTexture + AAudio A/V sync engine for FluxUI on Android.
//
// Pipeline:
//   MediaExtractor  →  MediaCodec (video) → SurfaceTexture →
//   GL_TEXTURE_EXTERNAL_OES
//                  └→  MediaCodec (audio) → PCM float → AAudio (via FluxAudio)
//
// All decoding runs on a single background thread (s_decodeThread).
// The main (GL) thread calls updateFrame() once per vsync to latch a new
// texture and retrieve the current A/V-synced position.
//
// Dependencies:
//   - flux_audio.hpp  (AAudio playback — audio path reuses it fully)
//   - libmediandk, libOpenSLES, libEGL, libGLESv2  (add to CMakeLists)
//   - GLES 3.0+ for GL_TEXTURE_EXTERNAL_OES (or GLES 2 + OES extension)
//

//
#pragma once
#ifdef __ANDROID__

#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>          // GL_TEXTURE_EXTERNAL_OES

#include <media/NdkMediaCodec.h>
#include <media/NdkMediaExtractor.h>
#include <media/NdkMediaFormat.h>

#include <android/asset_manager.h>
#include <android/log.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "flux_audio.hpp"

// ── External helpers provided by native-lib.cpp ───────────────────────────────
extern EGLDisplay      FluxAndroid_getEGLDisplay();
extern EGLContext      FluxAndroid_getEGLContext();
extern EGLSurface      FluxAndroid_getEGLSurface();
extern AAssetManager*  FluxAndroid_getAssetManager();  

// JNI shims implemented in native-lib.cpp
extern void*           FluxVideo_createSurfaceTexture(GLuint texId);
extern void            FluxVideo_updateTexImage(void* surfaceTexture);
extern ANativeWindow*  FluxVideo_getNativeWindow(void* surfaceTexture);
extern void            FluxVideo_destroySurfaceTexture(void* surfaceTexture);

// ============================================================================
// FluxVideo
// ============================================================================

class FluxVideo {
public:
    using FinishCallback = std::function<void()>;

    // ── Singleton ─────────────────────────────────────────────────────────────
    static FluxVideo& get() {
        static FluxVideo instance;
        return instance;
    }

    // ── State ─────────────────────────────────────────────────────────────────
    enum class State { Idle, Loading, Playing, Paused, Finished, Error };

    State getState()          const { return s_state.load(); }
    bool  isPlaying()         const { return s_state == State::Playing;  }
    bool  isPaused()          const { return s_state == State::Paused;   }
    bool  isFinished()        const { return s_state == State::Finished; }
    float getDurationSeconds()const { return s_durationUs.load() / 1e6f; }
    float getPositionSeconds()const { return s_positionUs.load() / 1e6f; }
    float getProgress() const {
        int64_t dur = s_durationUs.load();
        return (dur > 0) ? std::min(1.f, (float)s_positionUs.load() / dur) : 0.f;
    }
    int getVideoWidth()  const { return s_videoWidth.load();  }
    int getVideoHeight() const { return s_videoHeight.load(); }

    // ── GL texture (OES) — read on the main thread after updateFrame() ────────
    GLuint getTextureId()  const { return s_oesTexture; }
    bool   hasNewFrame()   const { return s_newFrame.load(); }

    // ── Callbacks ─────────────────────────────────────────────────────────────
    void setOnFinished(FinishCallback cb)                      { s_finishCallback = std::move(cb); }
    void setOnReady(std::function<void(int w, int h)> cb)      { s_readyCallback  = std::move(cb); }

    // =========================================================================
    // OPEN — loads file, creates codec + OES texture, starts decode thread.
    // Pass an asset-relative path ("videos/sample.mp4") or an absolute
    // filesystem path ("/sdcard/...").
    // Must be called on the GL thread (creates OES texture + EGL context).
    // =========================================================================

    bool open(const std::string& path) {
        close();   // resets everything, joins any old thread

        s_state = State::Loading;
        s_path  = path;

        // Create OES texture on the GL thread
        if (!_createOESTexture())
            return false;

        // Create offscreen EGL context for the decode thread
        if (!_createDecodeEGLContext())
            return false;

        // Spin up decode thread
        s_stopDecode = false;
        s_decodeThread = std::thread(&FluxVideo::_decodeLoop, this);
        return true;
    }

    // =========================================================================
    // PLAYBACK CONTROL
    // =========================================================================

    void play() {
        if (s_state == State::Paused || s_state == State::Loading) {
            {
                std::lock_guard<std::mutex> lk(s_pauseMutex);
                s_resumeRequested = true;
            }
            s_state = State::Playing;
            s_pauseCV.notify_all();
            FluxAudio::get().resume();
        }
    }

    void pause() {
        if (s_state == State::Playing) {
            s_state = State::Paused;
            FluxAudio::get().pause();
        }
    }

    void seekToProgress(float p) {
        int64_t us = (int64_t)(std::max(0.f, std::min(1.f, p)) * s_durationUs.load());
        _queueSeek(us);
    }

    void seekToSeconds(float secs) {
        int64_t us = (int64_t)(secs * 1e6f);
        us = std::max<int64_t>(0, std::min(us, s_durationUs.load()));
        _queueSeek(us);
    }

    void setVolume(float v) { FluxAudio::get().setVolume(v); }
    float getVolume() const { return FluxAudio::get().getVolume(); }

    // =========================================================================
    // updateFrame() — call once per vsync on the GL thread.
    // Latches the newest video frame into the OES texture via
    // SurfaceTexture::updateTexImage (called through JNI shim).
    // Returns true if the texture changed this vsync.
    // =========================================================================

    bool updateFrame() {
        if (!s_surfaceTexture || !s_pendingFrame.load()) {
            s_newFrame = false;
            return false;
        }
        FluxVideo_updateTexImage(s_surfaceTexture);
        s_pendingFrame = false;
        s_newFrame     = true;
        return true;
    }

    // =========================================================================
    // CLOSE
    // =========================================================================

    void close() {
      
        {
            std::lock_guard<std::mutex> lk(s_pauseMutex);
            s_resumeRequested = true;
        }
        s_stopDecode = true;
        s_pauseCV.notify_all();

        if (s_decodeThread.joinable())
            s_decodeThread.join();

        _destroyAudioCodec();
        _destroyVideoCodec();

        if (s_extractor) {
            AMediaExtractor_delete(s_extractor);
            s_extractor = nullptr;
        }

        if (s_oesTexture) {
            glDeleteTextures(1, &s_oesTexture);
            s_oesTexture = 0;
        }

        if (s_surfaceTexture) {
            FluxVideo_destroySurfaceTexture(s_surfaceTexture);
            s_surfaceTexture = nullptr;
        }

        _destroyDecodeEGLContext();


        s_state           = State::Idle;
        s_positionUs      = 0;
        s_durationUs      = 0;
        s_pendingFrame    = false;
        s_newFrame        = false;
        s_seekPending     = false;
        s_videoWidth      = 0;
        s_videoHeight     = 0;
        s_stopDecode      = false;
        s_resumeRequested = false;
        s_videoTrackIdx   = -1;
        s_audioTrackIdx   = -1;
    }

private:
    FluxVideo()  = default;
    ~FluxVideo() { close(); }

    // ── Path ──────────────────────────────────────────────────────────────────
    std::string s_path;

    // ── State ─────────────────────────────────────────────────────────────────
    std::atomic<State>   s_state{State::Idle};
    std::atomic<int64_t> s_positionUs{0};
    std::atomic<int64_t> s_durationUs{0};
    std::atomic<int>     s_videoWidth{0};
    std::atomic<int>     s_videoHeight{0};

    // ── Frame sync ────────────────────────────────────────────────────────────
    std::atomic<bool> s_pendingFrame{false};   // decode thread → GL thread
    std::atomic<bool> s_newFrame{false};       // set by updateFrame()

    // ── Seek ──────────────────────────────────────────────────────────────────
    std::atomic<bool>    s_seekPending{false};
    std::atomic<int64_t> s_seekTargetUs{0};

    // ── Decode thread ─────────────────────────────────────────────────────────
    std::thread       s_decodeThread;
    std::atomic<bool> s_stopDecode{false};

    // ── Pause / resume  ───────────────────────────────────────────────
    std::mutex              s_pauseMutex;
    std::condition_variable s_pauseCV;
    std::atomic<bool>       s_resumeRequested{false};

    // ── MediaCodec objects ────────────────────────────────────────────────────
    AMediaExtractor* s_extractor    = nullptr;
    AMediaCodec*     s_videoCodec   = nullptr;
    AMediaCodec*     s_audioCodec   = nullptr;
    int              s_videoTrackIdx = -1;
    int              s_audioTrackIdx = -1;

    // ── GL / EGL ──────────────────────────────────────────────────────────────
    GLuint     s_oesTexture        = 0;
    void*      s_surfaceTexture    = nullptr;




    // ── Callbacks ─────────────────────────────────────────────────────────────
    FinishCallback                      s_finishCallback;
    std::function<void(int w, int h)>   s_readyCallback;

    // ── PCM ring buffer for audio ─────────────────────────────────────────────
    static constexpr int kAudioRingSize = 48000 * 2; // ~2 s at 48 kHz
    std::vector<float>   s_audioBuf;
    std::atomic<int>     s_audioWrite{0};
    std::atomic<int>     s_audioRead{0};
    std::mutex           s_audioMutex;

    // ── Clock ─────────────────────────────────────────────────────────────────
    std::chrono::steady_clock::time_point s_clockBase;
    int64_t s_clockBaseUs = 0;

    // =========================================================================
    // OES TEXTURE — created on GL thread before decode thread starts
    // =========================================================================

    bool _createOESTexture() {
        glGenTextures(1, &s_oesTexture);
        if (!s_oesTexture) {
            __android_log_print(ANDROID_LOG_ERROR, "FluxVideo", "glGenTextures failed");
            return false;
        }
        glBindTexture(GL_TEXTURE_EXTERNAL_OES, s_oesTexture);
        glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_EXTERNAL_OES, 0);

        s_surfaceTexture = FluxVideo_createSurfaceTexture(s_oesTexture);
        if (!s_surfaceTexture) {
            __android_log_print(ANDROID_LOG_ERROR, "FluxVideo", "createSurfaceTexture failed");
            return false;
        }
        return true;
    }

    // =========================================================================
    // EGL CONTEXT — decode thread needs its own context sharing with main
    // =========================================================================

bool _createDecodeEGLContext() {
    return true;  // Not needed — MediaCodec renders via ANativeWindow directly
}

void _destroyDecodeEGLContext() {
    // Nothing to do
}

EGLConfig _getEGLConfig() {
    return nullptr;
}

    // =========================================================================
    // FILE LOADING — supports assets and absolute paths
    // =========================================================================

    int _openExtractor() {
        if (!s_path.empty() && s_path[0] == '/') {
            // Absolute filesystem path
            s_extractor = AMediaExtractor_new();
            if (AMediaExtractor_setDataSource(s_extractor, s_path.c_str()) != AMEDIA_OK) {
                __android_log_print(ANDROID_LOG_ERROR, "FluxVideo",
                                    "setDataSource failed: %s", s_path.c_str());
                AMediaExtractor_delete(s_extractor);
                s_extractor = nullptr;
                return -1;
            }
        } else {
            // Asset path (relative, e.g. "videos/sample.mp4")
            AAssetManager* am = FluxAndroid_getAssetManager();
            if (!am) {
                __android_log_print(ANDROID_LOG_ERROR, "FluxVideo", "AAssetManager is null");
                return -1;
            }
            AAsset* asset = AAssetManager_open(am, s_path.c_str(), AASSET_MODE_STREAMING);
            if (!asset) {
                __android_log_print(ANDROID_LOG_ERROR, "FluxVideo",
                                    "AAssetManager_open failed: %s", s_path.c_str());
                return -1;
            }

            off_t start = 0, len = 0;
            int fd = AAsset_openFileDescriptor(asset, &start, &len);
            AAsset_close(asset);
            if (fd < 0) {
                __android_log_print(ANDROID_LOG_ERROR, "FluxVideo",
                                    "AAsset_openFileDescriptor failed");
                return -1;
            }

            s_extractor = AMediaExtractor_new();
            media_status_t st = AMediaExtractor_setDataSourceFd(s_extractor, fd, start, len);
            ::close(fd);
            if (st != AMEDIA_OK) {
                __android_log_print(ANDROID_LOG_ERROR, "FluxVideo",
                                    "setDataSourceFd failed: %d", (int)st);
                AMediaExtractor_delete(s_extractor);
                s_extractor = nullptr;
                return -1;
            }
        }

        // Enumerate tracks
        int trackCount = (int)AMediaExtractor_getTrackCount(s_extractor);
        __android_log_print(ANDROID_LOG_DEBUG, "FluxVideo",
                            "Track count: %d", trackCount);

        for (int i = 0; i < trackCount; i++) {
            AMediaFormat* fmt = AMediaExtractor_getTrackFormat(s_extractor, i);
            const char*   mime = nullptr;
            AMediaFormat_getString(fmt, AMEDIAFORMAT_KEY_MIME, &mime);

            if (mime) {
                __android_log_print(ANDROID_LOG_DEBUG, "FluxVideo",
                                    "Track %d: %s", i, mime);
                if (strncmp(mime, "video/", 6) == 0 && s_videoTrackIdx < 0)
                    s_videoTrackIdx = i;
                if (strncmp(mime, "audio/", 6) == 0 && s_audioTrackIdx < 0)
                    s_audioTrackIdx = i;
            }

            int64_t dur = 0;
            if (AMediaFormat_getInt64(fmt, AMEDIAFORMAT_KEY_DURATION, &dur) && dur > 0)
                s_durationUs.store(dur);

            AMediaFormat_delete(fmt);
        }

        if (s_videoTrackIdx < 0) {
            __android_log_print(ANDROID_LOG_ERROR, "FluxVideo", "No video track found");
            return -1;
        }

        __android_log_print(ANDROID_LOG_DEBUG, "FluxVideo",
                            "Video track: %d, Audio track: %d, Duration: %.2fs",
                            s_videoTrackIdx, s_audioTrackIdx,
                            s_durationUs.load() / 1e6f);
        return 0;
    }

    // =========================================================================
    // CODEC SETUP
    // =========================================================================

    bool _openVideoCodec() {
        AMediaFormat* fmt = AMediaExtractor_getTrackFormat(s_extractor, s_videoTrackIdx);

        const char* mime = nullptr;
        AMediaFormat_getString(fmt, AMEDIAFORMAT_KEY_MIME, &mime);

        int32_t w = 0, h = 0;
        AMediaFormat_getInt32(fmt, AMEDIAFORMAT_KEY_WIDTH, &w);
        AMediaFormat_getInt32(fmt, AMEDIAFORMAT_KEY_HEIGHT, &h);
        s_videoWidth  = w;
        s_videoHeight = h;
        __android_log_print(ANDROID_LOG_DEBUG, "FluxVideo",
                            "Video codec: %s, %dx%d", mime ? mime : "?", w, h);

        s_videoCodec = AMediaCodec_createDecoderByType(mime);
        if (!s_videoCodec) {
            __android_log_print(ANDROID_LOG_ERROR, "FluxVideo",
                                "AMediaCodec_createDecoderByType failed: %s", mime);
            AMediaFormat_delete(fmt);
            return false;
        }

        ANativeWindow* window = FluxVideo_getNativeWindow(s_surfaceTexture);
        if (!window) {
            __android_log_print(ANDROID_LOG_ERROR, "FluxVideo",
                                "FluxVideo_getNativeWindow returned null");
            AMediaFormat_delete(fmt);
            return false;
        }

        media_status_t status = AMediaCodec_configure(s_videoCodec, fmt, window, nullptr, 0);
        AMediaFormat_delete(fmt);

        if (status != AMEDIA_OK) {
            __android_log_print(ANDROID_LOG_ERROR, "FluxVideo",
                                "AMediaCodec_configure failed: %d", (int)status);
            return false;
        }

        AMediaExtractor_selectTrack(s_extractor, s_videoTrackIdx);
        AMediaCodec_start(s_videoCodec);
        return true;
    }

    bool _openAudioCodec(int& outSampleRate, int& outChannels) {
        if (s_audioTrackIdx < 0)
            return false;

        AMediaFormat* fmt = AMediaExtractor_getTrackFormat(s_extractor, s_audioTrackIdx);
        const char*   mime = nullptr;
        AMediaFormat_getString(fmt, AMEDIAFORMAT_KEY_MIME, &mime);

        int32_t sr = 44100, ch = 1;
        AMediaFormat_getInt32(fmt, AMEDIAFORMAT_KEY_SAMPLE_RATE, &sr);
        AMediaFormat_getInt32(fmt, AMEDIAFORMAT_KEY_CHANNEL_COUNT, &ch);
        outSampleRate = sr;
        outChannels   = ch;

        s_audioCodec = AMediaCodec_createDecoderByType(mime);
        if (!s_audioCodec) {
            AMediaFormat_delete(fmt);
            return false;
        }

        AMediaCodec_configure(s_audioCodec, fmt, nullptr, nullptr, 0);
        AMediaFormat_delete(fmt);

        AMediaExtractor_selectTrack(s_extractor, s_audioTrackIdx);
        AMediaCodec_start(s_audioCodec);

        s_audioBuf.assign(kAudioRingSize, 0.f);
        s_audioWrite = 0;
        s_audioRead  = 0;
        return true;
    }

    void _destroyVideoCodec() {
        if (s_videoCodec) {
            AMediaCodec_stop(s_videoCodec);
            AMediaCodec_delete(s_videoCodec);
            s_videoCodec = nullptr;
        }
    }

    void _destroyAudioCodec() {
        if (s_audioCodec) {
            AMediaCodec_stop(s_audioCodec);
            AMediaCodec_delete(s_audioCodec);
            s_audioCodec = nullptr;
        }
    }

    // =========================================================================
    // SEEK
    // =========================================================================

    void _queueSeek(int64_t us) {
        s_seekTargetUs = us;
        s_seekPending  = true;
        // Also wake the pause CV in case we're paused — seek should still work
        s_pauseCV.notify_all();
    }

    void _doSeek(int64_t targetUs) {
        AMediaCodec_flush(s_videoCodec);
        if (s_audioCodec)
            AMediaCodec_flush(s_audioCodec);

        AMediaExtractor_seekTo(s_extractor, targetUs,
                               AMEDIAEXTRACTOR_SEEK_PREVIOUS_SYNC);

        {
            std::lock_guard<std::mutex> lk(s_audioMutex);
            s_audioWrite = 0;
            s_audioRead  = 0;
        }

        s_clockBaseUs = targetUs;
        s_clockBase   = std::chrono::steady_clock::now();
        s_positionUs  = targetUs;

        FluxAudio::get().seekToSeconds(targetUs / 1e6f);
        s_seekPending = false;
    }

    // =========================================================================
    // DECODE LOOP — runs on s_decodeThread
    // =========================================================================

    void _decodeLoop() {


        // Open extractor
        if (_openExtractor() < 0) {
            s_state = State::Error;
            return;
        }

        // Open video codec
        if (!_openVideoCodec()) {
            s_state = State::Error;
            return;
        }

        // Open audio codec (optional)
        int  audioSR = 44100, audioCh = 1;
        bool hasAudio = _openAudioCodec(audioSR, audioCh);

        // Start audio playback
        if (hasAudio) {
            FluxAudio::get().playStream(
                [this, audioCh](float* buf, int frames) -> int {
                    return _pullAudio(buf, frames, audioCh);
                },
                audioSR);
        }

        // Signal ready 
        if (s_readyCallback)
            s_readyCallback(s_videoWidth.load(), s_videoHeight.load());

        // Transition to Paused and wait for play()
        s_state = State::Paused;

        s_clockBase   = std::chrono::steady_clock::now();
        s_clockBaseUs = 0;

        {
            std::unique_lock<std::mutex> lk(s_pauseMutex);
            s_pauseCV.wait(lk, [this] {
                return s_resumeRequested.load() || s_stopDecode.load();
            });
            if (s_stopDecode) {
                _cleanupEGL();
                return;
            }
            s_resumeRequested = false;
        }

        // Re-stamp clock at actual play start
        s_clockBase   = std::chrono::steady_clock::now();
        s_clockBaseUs = 0;

        bool inputEOS  = false;
        bool outputEOS = false;

        static constexpr int64_t kTimeoutUs = 5000;

        while (!s_stopDecode && !outputEOS) {

            // ── Handle seek ──────────────────────────────────────────────
            if (s_seekPending.load()) {
                _doSeek(s_seekTargetUs.load());
                inputEOS  = false;
                outputEOS = false;
            }

            // ── Wait while paused ──────────────
            if (s_state == State::Paused) {
                std::unique_lock<std::mutex> lk(s_pauseMutex);
                s_pauseCV.wait(lk, [this] {
                    return s_resumeRequested.load() ||
                           s_stopDecode.load()      ||
                           s_seekPending.load();
                });
                if (s_stopDecode) break;
                // If woken by seek, loop back to top to handle it
                if (s_seekPending.load()) continue;
                s_resumeRequested = false;
                s_clockBase   = std::chrono::steady_clock::now();
                s_clockBaseUs = s_positionUs.load();
                continue;
            }

            // ── Feed input to video codec ─────────────────────────────────
            if (!inputEOS) {
                ssize_t inBuf = AMediaCodec_dequeueInputBuffer(s_videoCodec, kTimeoutUs);
                if (inBuf >= 0) {
                    size_t   bufSize = 0;
                    uint8_t* data    = AMediaCodec_getInputBuffer(
                                           s_videoCodec, (size_t)inBuf, &bufSize);

                    int trackIdx = (int)AMediaExtractor_getSampleTrackIndex(s_extractor);

                    if (trackIdx == s_videoTrackIdx) {
                        ssize_t sampleSize = AMediaExtractor_readSampleData(
                                                 s_extractor, data, bufSize);
                        int64_t pts = AMediaExtractor_getSampleTime(s_extractor);

                        if (sampleSize > 0) {
                            AMediaCodec_queueInputBuffer(s_videoCodec, (size_t)inBuf,
                                                         0, (size_t)sampleSize, pts, 0);
                            AMediaExtractor_advance(s_extractor);
                        } else {
                            AMediaCodec_queueInputBuffer(s_videoCodec, (size_t)inBuf,
                                                         0, 0, 0,
                                                         AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
                            inputEOS = true;
                        }
                    } else if (trackIdx == s_audioTrackIdx && hasAudio) {
                        _feedAudioCodec();
                        // Return video input buffer unused — will retry next iteration
                        AMediaCodec_queueInputBuffer(s_videoCodec, (size_t)inBuf, 0, 0, 0, 0);
                    } else {
                        // Unknown or EOS track index (-1) — advance and return buffer
                        if (trackIdx >= 0)
                            AMediaExtractor_advance(s_extractor);
                        AMediaCodec_queueInputBuffer(s_videoCodec, (size_t)inBuf, 0, 0, 0, 0);
                    }
                }
            }

            // ── Drain video output ────────────────────────────────────────
            AMediaCodecBufferInfo info{};
            ssize_t outBuf = AMediaCodec_dequeueOutputBuffer(
                                 s_videoCodec, &info, kTimeoutUs);

            if (outBuf >= 0) {
                bool isEOS = (info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) != 0;

                if (info.size > 0) {
                    int64_t framePts = info.presentationTimeUs;
                    _syncToPresentation(framePts);
                    s_positionUs = framePts;

                    // Release to Surface → renders into SurfaceTexture
                    AMediaCodec_releaseOutputBuffer(s_videoCodec, (size_t)outBuf, true);
                    s_pendingFrame = true;
                } else {
                    AMediaCodec_releaseOutputBuffer(s_videoCodec, (size_t)outBuf, false);
                }

                if (isEOS) {
                    outputEOS = true;
                    s_state   = State::Finished;
                    if (s_finishCallback)
                        s_finishCallback();
                }
            } else if (outBuf == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
                AMediaFormat* fmt = AMediaCodec_getOutputFormat(s_videoCodec);
                int32_t w = 0, h = 0;
                AMediaFormat_getInt32(fmt, AMEDIAFORMAT_KEY_WIDTH,  &w);
                AMediaFormat_getInt32(fmt, AMEDIAFORMAT_KEY_HEIGHT, &h);
                if (w > 0 && h > 0) {
                    s_videoWidth  = w;
                    s_videoHeight = h;
                }
                AMediaFormat_delete(fmt);
            }

            // ── Drain audio codec → ring buffer ───────────────────────────
            if (hasAudio)
                _drainAudioCodec(audioCh);
        }

        _cleanupEGL();
    }

void _cleanupEGL() {
    // Nothing to do
}

    // =========================================================================
    // A/V SYNC — interruptible sleep
    // =========================================================================

    void _syncToPresentation(int64_t framePtsUs) {
        auto    now     = std::chrono::steady_clock::now();
        int64_t wallUs  = std::chrono::duration_cast<std::chrono::microseconds>(
                              now - s_clockBase).count();
        int64_t targetUs = framePtsUs - s_clockBaseUs;
        int64_t sleepUs  = targetUs - wallUs;

        // Sleep in 5 ms chunks so stop/seek/pause can interrupt promptly
        while (sleepUs > 2000 && !s_stopDecode &&
               s_state != State::Paused) {
            int64_t chunk = std::min(sleepUs, (int64_t)5000);
            std::this_thread::sleep_for(std::chrono::microseconds(chunk));
            now     = std::chrono::steady_clock::now();
            wallUs  = std::chrono::duration_cast<std::chrono::microseconds>(
                          now - s_clockBase).count();
            sleepUs = targetUs - wallUs;
        }
        // If behind, present immediately (natural frame drop)
    }

    // =========================================================================
    // AUDIO CODEC — feed extractor samples → decode → ring buffer
    // =========================================================================

    void _feedAudioCodec() {
        if (!s_audioCodec) return;

        ssize_t inBuf = AMediaCodec_dequeueInputBuffer(s_audioCodec, 1000);
        if (inBuf < 0) return;

        size_t   bufSize = 0;
        uint8_t* data    = AMediaCodec_getInputBuffer(
                               s_audioCodec, (size_t)inBuf, &bufSize);

        ssize_t sampleSize = AMediaExtractor_readSampleData(
                                 s_extractor, data, bufSize);
        int64_t pts = AMediaExtractor_getSampleTime(s_extractor);

        if (sampleSize > 0) {
            AMediaCodec_queueInputBuffer(s_audioCodec, (size_t)inBuf,
                                         0, (size_t)sampleSize, pts, 0);
            AMediaExtractor_advance(s_extractor);
        } else {
            AMediaCodec_queueInputBuffer(s_audioCodec, (size_t)inBuf, 0, 0, 0,
                                         AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
        }
    }

    void _drainAudioCodec(int channels) {
        if (!s_audioCodec) return;

        AMediaCodecBufferInfo info{};
        ssize_t outBuf = AMediaCodec_dequeueOutputBuffer(s_audioCodec, &info, 0);
        if (outBuf < 0) return;

        if (info.size > 0) {
            size_t   sz   = 0;
            uint8_t* data = AMediaCodec_getOutputBuffer(
                                s_audioCodec, (size_t)outBuf, &sz);

            // MediaCodec PCM output is PCM_16 interleaved
            int            frameCount = info.size / (2 * channels);
            const int16_t* pcm16      = reinterpret_cast<const int16_t*>(
                                            data + info.offset);

            std::lock_guard<std::mutex> lk(s_audioMutex);
            int wr = s_audioWrite.load();
            int rd = s_audioRead.load();

            // Drop if ring is full
            int space = kAudioRingSize - (wr - rd);
            frameCount = std::min(frameCount, space);

            for (int i = 0; i < frameCount; i++) {
                float sum = 0.f;
                for (int c = 0; c < channels; c++)
                    sum += pcm16[i * channels + c] / 32768.f;
                float mono = sum / channels;
                if (mono >  1.f) mono =  1.f;
                if (mono < -1.f) mono = -1.f;
                s_audioBuf[wr % kAudioRingSize] = mono;
                wr++;
            }
            s_audioWrite = wr;
        }

        AMediaCodec_releaseOutputBuffer(s_audioCodec, (size_t)outBuf, false);
    }

    // Pull audio samples — called by FluxAudio stream callback (audio thread)
    int _pullAudio(float* buf, int frames, int /*channels*/) {
        std::lock_guard<std::mutex> lk(s_audioMutex);
        int rd    = s_audioRead.load();
        int wr    = s_audioWrite.load();
        int avail = wr - rd;
        int toCopy = std::min(frames, avail);

        for (int i = 0; i < toCopy; i++)
            buf[i] = s_audioBuf[(rd + i) % kAudioRingSize];
        for (int i = toCopy; i < frames; i++)
            buf[i] = 0.f;  // underrun → silence

        s_audioRead = rd + toCopy;
        // Return frames (not toCopy) to keep stream alive on underrun
        return frames;
    }
};

#endif // __ANDROID__
// ============================================================================
// FluxVideo — Media Foundation backend  (Windows)
// ============================================================================
#ifdef _WIN32

#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mftransform.h>
#include <windows.h>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "propsys.lib")

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "flux_audio.hpp"

struct FluxMFInit {
  FluxMFInit() {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
      fprintf(stderr, "[FluxVideo] CoInitializeEx failed: 0x%08X\n",
              (unsigned)hr);
    }
    hr = MFStartup(MF_VERSION);
    if (FAILED(hr)) {
      fprintf(stderr, "[FluxVideo] MFStartup failed: 0x%08X\n", (unsigned)hr);
    }
  }
  ~FluxMFInit() {
    MFShutdown();
    CoUninitialize();
  }
};

static FluxMFInit s_mfInit;

// ============================================================================
// FluxVideo
// ============================================================================

class FluxVideo {
public:
  using FinishCallback = std::function<void()>;

  enum class State { Idle, Loading, Playing, Paused, Finished, Error };

  static FluxVideo &get() {
    static FluxVideo instance;
    return instance;
  }

  // ── Accessors ─────────────────────────────────────────────────────────────
  State getState() const { return s_state.load(); }
  bool isPlaying() const { return s_state == State::Playing; }
  bool isPaused() const { return s_state == State::Paused; }
  bool isFinished() const { return s_state == State::Finished; }
  float getDurationSeconds() const {
    return (float)s_durationHns.load() / 1e7f;
  }
  float getPositionSeconds() const {
    return (float)s_positionHns.load() / 1e7f;
  }
  float getProgress() const {
    int64_t dur = s_durationHns.load();
    return dur > 0 ? std::min(1.f, (float)s_positionHns.load() / (float)dur)
                   : 0.f;
  }
  int getVideoWidth() const { return s_videoWidth.load(); }
  int getVideoHeight() const { return s_videoHeight.load(); }

  // ── Frame access — called on the UI/paint thread ──────────────────────────
  bool hasNewFrame() const { return s_newFrame.load(); }

  struct FrameLock {
    std::unique_lock<std::mutex> lock;
    const uint8_t *data = nullptr;
    int width = 0;
    int height = 0;
    int stride = 0;
  };

  FrameLock lockFrame() {
    FrameLock fl;
    fl.lock = std::unique_lock<std::mutex>(s_frameMutex);
    fl.data = s_frameData.data();
    fl.width = s_frameWidth;
    fl.height = s_frameHeight;
    fl.stride = s_frameStride;
    s_newFrame = false;
    return fl;
  }

  // ── Callbacks ─────────────────────────────────────────────────────────────
  void setOnFinished(FinishCallback cb) { s_finishCallback = std::move(cb); }
  void setOnReady(std::function<void(int w, int h)> cb) {
    s_readyCallback = std::move(cb);
  }

  // ── Volume ────────────────────────────────────────────────────────────────
  void setVolume(float v) { FluxAudio::get().setVolume(v); }
  float getVolume() const { return FluxAudio::get().getVolume(); }

  bool open(const std::string &path) {
    s_videoEOS = false;
    s_audioEOS = false;
    close();

    s_state = State::Loading;
    s_stopDecode = false;
    s_seekPending = false;
    s_didFireFinish = false;
    s_pauseRequested = false;
    s_resumeRequested = false;

    s_decodeThread = std::thread(&FluxVideo::_decodeLoop, this, path);
    return true;
  }

  // =========================================================================
  // PLAYBACK CONTROL
  // =========================================================================

  void play() {
    std::lock_guard<std::mutex> lk(s_cmdMutex);

    if (s_state == State::Paused || s_state == State::Loading ||
        (s_state == State::Playing && s_pauseRequested)) {
      s_pauseRequested = false;
      s_resumeRequested = true;
      s_pauseCV.notify_all();
    }
  }

  void pause() {
    std::lock_guard<std::mutex> lk(s_cmdMutex);

    if (s_state == State::Playing && !s_pauseRequested) {
      s_pauseRequested = true;
    }
  }

  void seekToProgress(float p) {
    int64_t hns = (int64_t)(std::max(0.f, std::min(1.f, p)) *
                            (float)s_durationHns.load());
    _queueSeek(hns);
  }

  void seekToSeconds(float secs) {
    int64_t hns = (int64_t)(secs * 1e7f);
    hns = std::max<int64_t>(0, std::min(hns, s_durationHns.load()));
    _queueSeek(hns);
  }

  void close() {
    {
      std::lock_guard<std::mutex> lk(s_cmdMutex);
      s_stopDecode = true;
      s_resumeRequested = true; // unblock any pause-wait
    }
    s_cmdCV.notify_all();
    s_pauseCV.notify_all();

    if (s_decodeThread.joinable())
      s_decodeThread.join();

    FluxAudio::get().stopPlayback();

    s_state = State::Idle;
    s_positionHns = 0;
    s_durationHns = 0;
    s_newFrame = false;
    s_videoWidth = 0;
    s_videoHeight = 0;
    s_stopDecode = false;
    s_videoEOS = false;
    s_audioEOS = false;
  }

  void shutdown() { close(); }

private:
  FluxVideo() = default;
  ~FluxVideo() { close(); }

  // ── State ─────────────────────────────────────────────────────────────────
  std::atomic<State> s_state{State::Idle};
  std::atomic<int64_t> s_positionHns{0};
  std::atomic<int64_t> s_durationHns{0};
  std::atomic<int> s_videoWidth{0};
  std::atomic<int> s_videoHeight{0};

  // ── Frame double-buffer ───────────────────────────────────────────────────
  std::mutex s_frameMutex;
  std::vector<uint8_t> s_frameData;
  int s_frameWidth = 0;
  int s_frameHeight = 0;
  int s_frameStride = 0;
  std::atomic<bool> s_newFrame{false};

  // ── Per-stream EOS tracking ────────────────────────────────────────────────
  bool s_videoEOS = false;
  bool s_audioEOS = false;

  // ── Audio ring buffer ─────────────────────────────────────────────────────
  static constexpr int kAudioRing = 48000 * 4; // ~4 s at 48 kHz mono
  std::vector<float> s_audioBuf;

  int s_audioWrite = 0;
  int s_audioRead = 0;
  std::mutex s_audioMutex;
  int s_audioChannels = 1;
  int s_audioSampleRate = 44100;

  // ── Command channel ───────────────────────────────────────────────────────
  std::mutex s_cmdMutex;
  std::condition_variable s_cmdCV;
  std::condition_variable s_pauseCV;
  bool s_pauseRequested = false;
  bool s_resumeRequested = false;
  bool s_stopDecode = false;
  bool s_seekPending = false;
  int64_t s_seekTargetHns = 0;

  // ── Thread ────────────────────────────────────────────────────────────────
  std::thread s_decodeThread;

  // ── Callbacks ─────────────────────────────────────────────────────────────
  FinishCallback s_finishCallback;
  std::function<void(int w, int h)> s_readyCallback;
  std::atomic<bool> s_didFireFinish{false};

  // ── Clock ─────────────────────────────────────────────────────────────────
  std::chrono::steady_clock::time_point s_clockBase;
  int64_t s_clockBaseHns = 0;

  void _queueSeek(int64_t hns) {
    std::lock_guard<std::mutex> lk(s_cmdMutex);
    s_seekTargetHns = hns;
    s_seekPending = true;
    s_cmdCV.notify_one();
  }

  // =========================================================================
  // DECODE LOOP
  // =========================================================================

  void _decodeLoop(std::string path) {

    // ── Create IMFSourceReader ────────────────────────────────────────────
    IMFAttributes *attrs = nullptr;
    MFCreateAttributes(&attrs, 2);
    attrs->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
    attrs->SetUINT32(MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING, TRUE);

    std::wstring wpath(path.begin(), path.end());
    IMFSourceReader *reader = nullptr;
    HRESULT hr = MFCreateSourceReaderFromURL(wpath.c_str(), attrs, &reader);
    attrs->Release();

    if (FAILED(hr)) {

      s_state = State::Error;
      return;
    }

    // ── Configure video output: RGB24, native size ─────────────────────
    IMFMediaType *videoType = nullptr;
    MFCreateMediaType(&videoType);
    videoType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    videoType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB24);
    reader->SetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                                nullptr, videoType);
    videoType->Release();

    // ── Read video dimensions and duration ────────────────────────────────
    {
      IMFMediaType *actual = nullptr;
      reader->GetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                                  &actual);
      if (actual) {
        UINT32 w = 0, h = 0;
        MFGetAttributeSize(actual, MF_MT_FRAME_SIZE, &w, &h);
        s_videoWidth = (int)w;
        s_videoHeight = (int)h;
        actual->Release();
      }

      PROPVARIANT pv;
      PropVariantInit(&pv);
      if (SUCCEEDED(reader->GetPresentationAttribute(
              (DWORD)MF_SOURCE_READER_MEDIASOURCE, MF_PD_DURATION, &pv)) &&
          pv.vt == VT_UI8)
        s_durationHns = (int64_t)pv.uhVal.QuadPart;
      PropVariantClear(&pv);
    }

    // ── Configure audio ───────────────────────────────────────────────────
    bool hasAudio = _configureAudio(reader);

    // ── Discover actual stream indices ────────────────────────────────────
    DWORD videoStreamIdx = 0xFFFFFFFF;
    DWORD audioStreamIdx = 0xFFFFFFFF;
    {
      for (DWORD i = 0; i < 8; i++) {
        IMFMediaType *mt = nullptr;
        if (SUCCEEDED(reader->GetCurrentMediaType(i, &mt)) && mt) {
          GUID major = GUID_NULL;
          mt->GetMajorType(&major);
          if (major == MFMediaType_Video && videoStreamIdx == 0xFFFFFFFF) {
            videoStreamIdx = i;

          } else if (major == MFMediaType_Audio &&
                     audioStreamIdx == 0xFFFFFFFF) {
            audioStreamIdx = i;
          }
          mt->Release();
        }
      }
    }

    // ── Allocate frame buffer ─────────────────────────────────────────────
    {
      std::lock_guard<std::mutex> lk(s_frameMutex);
      int w = s_videoWidth.load(), h = s_videoHeight.load();
      s_frameWidth = w;
      s_frameHeight = h;
      s_frameStride = w * 3;
      s_frameData.assign((size_t)(w * h * 3), 0);
    }

    // ── Allocate audio ring ───────────────────────────────────────────────
    if (hasAudio) {
      {
        std::lock_guard<std::mutex> lk(s_audioMutex);
        s_audioBuf.assign(kAudioRing, 0.f);
        s_audioWrite = 0;
        s_audioRead = 0;
      }
      FluxAudio::get().playStream(
          [this](float *buf, int frames) -> int {
            return _pullAudio(buf, frames);
          },
          s_audioSampleRate);
    }

    // ── Signal ready ──────────────────────────────────────────────────────
    if (s_readyCallback)
      s_readyCallback(s_videoWidth.load(), s_videoHeight.load());

    s_state = State::Paused;
    FluxAudio::get().pause();

    {
      std::unique_lock<std::mutex> lk(s_cmdMutex);
      s_pauseCV.wait(lk, [this] { return s_resumeRequested || s_stopDecode; });
      if (s_stopDecode) {

        reader->Release();
        return;
      }
      s_resumeRequested = false;
      s_state = State::Playing;
      FluxAudio::get().resume();
    }

    // Reset clock only once we actually start playing
    s_clockBase = std::chrono::steady_clock::now();
    s_clockBaseHns = 0;

    // ── Main read loop ────────────────────────────────────────────────────
    int frameCount = 0;
    int audioSampleCount = 0;
    int loopIter = 0;
    bool videoEOS = false;
    bool audioEOS = false;

    while (true) {
      loopIter++;

      // ── Command processing ────────────────────────────────────────────
      {
        std::unique_lock<std::mutex> lk(s_cmdMutex);

        if (s_stopDecode) {

          break;
        }

        if (s_seekPending) {
          int64_t targetHns = s_seekTargetHns;
          s_seekPending = false;
          lk.unlock();

          _doSeek(reader, targetHns);
          videoEOS = false;
          audioEOS = false;
          continue;
        }

        if (s_pauseRequested) {

          s_pauseRequested = false;
          s_state = State::Paused;
          FluxAudio::get().pause();

          // wait — not wait_for — so we block until play() or close() actually
          // arrives
          s_pauseCV.wait(lk,
                         [this] { return s_resumeRequested || s_stopDecode; });

          if (s_stopDecode) {
            break;
          }

          s_resumeRequested = false;
          s_state = State::Playing;
          FluxAudio::get().resume();
          s_clockBase = std::chrono::steady_clock::now();
          s_clockBaseHns = s_positionHns.load();
        }
      }

      // ── Read one sample ───────────────────────────────────────────────
      DWORD streamIndex = 0, flags = 0;
      LONGLONG timestamp = 0;
      IMFSample *sample = nullptr;

      hr = reader->ReadSample((DWORD)MF_SOURCE_READER_ANY_STREAM, 0,
                              &streamIndex, &flags, &timestamp, &sample);

      if (FAILED(hr)) {

        break;
      }

      // ── EOS handling ──────────────────────────────────────────────────
      if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
        if (sample) {
          sample->Release();
          sample = nullptr;
        }

        if (streamIndex == videoStreamIdx) {
          videoEOS = true;

          s_state = State::Finished;
          bool expected = false;
          if (s_didFireFinish.compare_exchange_strong(expected, true))
            if (s_finishCallback)
              s_finishCallback();
          break;
        } else {

          audioEOS = true;

          reader->SetStreamSelection(streamIndex, FALSE);
          continue;
        }
      }

      if (flags & MF_SOURCE_READERF_STREAMTICK) {

        if (sample) {
          sample->Release();
          sample = nullptr;
        }
        continue;
      }

      if (!sample) {

        continue;
      }

      // ── Route by stream ───────────────────────────────────────────────
      if (streamIndex == videoStreamIdx) {
        frameCount++;
        if (frameCount <= 5 || frameCount % 60 == 0) {
        }
        _processVideoSample(sample, timestamp);
      } else if (streamIndex == audioStreamIdx && hasAudio && !audioEOS) {
        audioSampleCount++;
        if (audioSampleCount <= 3 || audioSampleCount % 200 == 0) {
        }
        _processAudioSample(sample);
      }

      sample->Release();
    }

    reader->Release();
  }

  // =========================================================================
  // AUDIO CONFIG
  // =========================================================================

  bool _configureAudio(IMFSourceReader *reader) {
    reader->SetStreamSelection((DWORD)MF_SOURCE_READER_ALL_STREAMS, FALSE);
    reader->SetStreamSelection((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                               TRUE);
    reader->SetStreamSelection((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM,
                               TRUE);

    // Request PCM float — do NOT force channel count or sample rate here;
    // let MF pick the native values and read them back afterward.
    // Forcing mono can fail on codecs that only decode to their native layout.
    IMFMediaType *audioType = nullptr;
    MFCreateMediaType(&audioType);
    audioType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    audioType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_Float);
    audioType->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 32);

    HRESULT hr = reader->SetCurrentMediaType(
        (DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, audioType);
    audioType->Release();

    if (FAILED(hr)) {

      reader->SetStreamSelection((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM,
                                 FALSE);
      return false;
    }

    // Read back the actual negotiated format
    IMFMediaType *actual = nullptr;
    reader->GetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM,
                                &actual);
    if (actual) {
      UINT32 sr = 44100, ch = 2;
      actual->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &sr);
      actual->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &ch);
      s_audioSampleRate = (int)sr;
      s_audioChannels = (int)ch;

      actual->Release();
    }
    return true;
  }

  // =========================================================================
  // SEEK
  // =========================================================================

  void _doSeek(IMFSourceReader *reader, int64_t targetHns) {
    PROPVARIANT pv;
    PropVariantInit(&pv);
    pv.vt = VT_I8;
    pv.hVal.QuadPart = targetHns;
    reader->SetCurrentPosition(GUID_NULL, pv);
    PropVariantClear(&pv);

    // Flush audio ring
    {
      std::lock_guard<std::mutex> lk(s_audioMutex);
      s_audioWrite = 0;
      s_audioRead = 0;
    }
    FluxAudio::get().seekToSeconds((float)targetHns / 1e7f);

    // Re-sync clock
    s_clockBase = std::chrono::steady_clock::now();
    s_clockBaseHns = targetHns;
    s_positionHns = targetHns;
    s_videoEOS = false;
    s_audioEOS = false;

    s_didFireFinish = false;
  }

  // =========================================================================
  // VIDEO SAMPLE → frame buffer
  // =========================================================================

  void _processVideoSample(IMFSample *sample, LONGLONG timestamp) {
    _syncToPresentation(timestamp);
    s_positionHns = timestamp;

    IMFMediaBuffer *buf = nullptr;
    if (FAILED(sample->ConvertToContiguousBuffer(&buf)))
      return;

    BYTE *data = nullptr;
    DWORD maxLen = 0, curLen = 0;
    if (SUCCEEDED(buf->Lock(&data, &maxLen, &curLen))) {
      int w = s_frameWidth, h = s_frameHeight;
      int expected = w * h * 3;

      if ((int)curLen >= expected && expected > 0) {
        std::vector<uint8_t> local(expected);
        memcpy(local.data(), data, expected);
        buf->Unlock();
        buf->Release();

        {
          std::lock_guard<std::mutex> lk(s_frameMutex);
          s_frameData = std::move(local);
          s_newFrame = true;
        }
        return;
      }
      buf->Unlock();
    }
    buf->Release();
  }

  // =========================================================================
  // AUDIO SAMPLE → ring buffer
  // =========================================================================

  void _processAudioSample(IMFSample *sample) {
    IMFMediaBuffer *buf = nullptr;
    if (FAILED(sample->ConvertToContiguousBuffer(&buf)))
      return;

    BYTE *data = nullptr;
    DWORD maxLen = 0, curLen = 0;
    if (SUCCEEDED(buf->Lock(&data, &maxLen, &curLen))) {
      int floatCount = (int)(curLen / sizeof(float));
      const float *pcm = reinterpret_cast<const float *>(data);

      std::lock_guard<std::mutex> lk(s_audioMutex);
      int ch = s_audioChannels;
      int frames = floatCount / ch;
      int wr = s_audioWrite;
      int rd = s_audioRead;

      // Drop frames if ring is full — prevents silent overwrite / heap
      // corruption
      int available_space = kAudioRing - (wr - rd);
      frames = std::min(frames, available_space);

      for (int i = 0; i < frames; i++) {
        float sum = 0.f;
        for (int c = 0; c < ch; c++)
          sum += pcm[i * ch + c];
        // Clamp to prevent clipping on stereo centre signals
        float mono = sum / (float)ch;
        if (mono > 1.f)
          mono = 1.f;
        if (mono < -1.f)
          mono = -1.f;
        s_audioBuf[wr % kAudioRing] = mono;
        wr++;
      }
      s_audioWrite = wr;
      buf->Unlock();
    }
    buf->Release();
  }

  // =========================================================================
  // A/V SYNC — interruptible sleep in 5ms chunks
  // =========================================================================

  void _syncToPresentation(int64_t framePtsHns) {
    auto now = std::chrono::steady_clock::now();
    int64_t wallHns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(now - s_clockBase)
            .count() /
        100;
    int64_t targetHns = framePtsHns - s_clockBaseHns;
    int64_t sleepHns = targetHns - wallHns;

    // Sleep in 5ms chunks so stop/seek can interrupt promptly.
    // 1 HNS = 100 ns, so 5ms = 50,000 HNS.
    while (sleepHns > 20000 && !s_stopDecode && !s_pauseRequested) {
      int64_t chunk = std::min(sleepHns, (int64_t)50000); // 5ms max per chunk
      std::this_thread::sleep_for(std::chrono::nanoseconds(chunk * 100));

      now = std::chrono::steady_clock::now();
      wallHns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                    now - s_clockBase)
                    .count() /
                100;
      sleepHns = targetHns - wallHns;

      // Log first few frames and any huge sleeps
      static int syncCount = 0;
      syncCount++;
    }
    // If we're behind, present immediately (frames will be dropped naturally)
  }

  // =========================================================================
  // AUDIO PULL (called by FluxAudio stream callback on the audio thread)
  // =========================================================================

  int _pullAudio(float *buf, int frames) {
    std::lock_guard<std::mutex> lk(s_audioMutex);
    int rd = s_audioRead;
    int wr = s_audioWrite;
    int avail = wr - rd;
    int toCopy = std::min(frames, avail);

    for (int i = 0; i < toCopy; i++)
      buf[i] = s_audioBuf[(size_t)((rd + i) % kAudioRing)];

    // Fill any underrun with silence — do NOT stop the stream
    for (int i = toCopy; i < frames; i++)
      buf[i] = 0.f;

    s_audioRead = rd + toCopy;

    return frames;
  }
};

#endif // _WIN32

#pragma once
#if defined(__linux__) && !defined(__ANDROID__)

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "flux_audio.hpp"

// ============================================================================
// FluxVideo
// ============================================================================

class FluxVideo {
public:
  using FinishCallback = std::function<void()>;

  enum class State { Idle, Loading, Playing, Paused, Finished, Error };

  static FluxVideo &get() {
    static FluxVideo instance;
    return instance;
  }

  // ── Accessors ─────────────────────────────────────────────────────────────
  State getState() const { return s_state.load(); }
  bool isPlaying() const { return s_state == State::Playing; }
  bool isPaused() const { return s_state == State::Paused; }
  bool isFinished() const { return s_state == State::Finished; }
  float getDurationSeconds() const { return s_durationUs.load() / 1e6f; }
  float getPositionSeconds() const { return s_positionUs.load() / 1e6f; }
  float getProgress() const {
    int64_t dur = s_durationUs.load();
    return dur > 0 ? std::min(1.f, (float)s_positionUs.load() / (float)dur)
                   : 0.f;
  }
  int getVideoWidth() const { return s_videoWidth.load(); }
  int getVideoHeight() const { return s_videoHeight.load(); }
  bool hasNewFrame() const { return s_newFrame.load(); }

  // ── Frame access (main / paint thread) ────────────────────────────────────
  struct FrameLock {
    std::unique_lock<std::mutex> lock;
    const uint8_t *data = nullptr;
    int width = 0;
    int height = 0;
    int stride = 0;
  };

  FrameLock lockFrame() {
    FrameLock fl;
    fl.lock = std::unique_lock<std::mutex>(s_frameMutex);
    fl.data = s_frameData.data();
    fl.width = s_frameWidth;
    fl.height = s_frameHeight;
    fl.stride = s_frameStride;
    s_newFrame = false;
    return fl;
  }

  // ── Callbacks ─────────────────────────────────────────────────────────────
  void setOnFinished(FinishCallback cb) { s_finishCallback = std::move(cb); }
  void setOnReady(std::function<void(int w, int h)> cb) {
    s_readyCallback = std::move(cb);
  }

  // ── Volume ────────────────────────────────────────────────────────────────
  void setVolume(float v) { FluxAudio::get().setVolume(v); }
  float getVolume() const { return FluxAudio::get().getVolume(); }

  // =========================================================================
  // OPEN
  // =========================================================================

  bool open(const std::string &path) {
    close();
    s_state = State::Loading;
    s_stopDecode = false;
    s_seekPending = false;
    s_didFireFinish = false;
    s_pauseRequested = false;
    s_resumeRequested = false;

    s_decodeThread = std::thread(&FluxVideo::_decodeLoop, this, path);
    return true;
  }

  // =========================================================================
  // PLAYBACK CONTROL
  // =========================================================================

  void play() {
    if (s_state == State::Paused || s_state == State::Loading) {
      {
        std::lock_guard<std::mutex> lk(s_cmdMutex);
        s_resumeRequested = true;
      }
      s_pauseCV.notify_all();
    }
  }

  void pause() {
    if (s_state == State::Playing) {
      std::lock_guard<std::mutex> lk(s_cmdMutex);
      s_pauseRequested = true;
    }
  }

  void seekToProgress(float p) {
    int64_t us =
        (int64_t)(std::max(0.f, std::min(1.f, p)) * s_durationUs.load());
    _queueSeek(us);
  }

  void seekToSeconds(float secs) {
    int64_t us = (int64_t)(secs * 1e6f);
    us = std::max<int64_t>(0, std::min(us, s_durationUs.load()));
    _queueSeek(us);
  }

  // =========================================================================
  // CLOSE
  // =========================================================================

  void close() {
    {
      std::lock_guard<std::mutex> lk(s_cmdMutex);
      s_stopDecode = true;
      s_resumeRequested = true; // unblock any pause-wait
    }
    s_pauseCV.notify_all();

    if (s_decodeThread.joinable())
      s_decodeThread.join();

    FluxAudio::get().stopPlayback();

    s_state = State::Idle;
    s_positionUs = 0;
    s_durationUs = 0;
    s_newFrame = false;
    s_videoWidth = 0;
    s_videoHeight = 0;
    s_stopDecode = false;
  }

  void shutdown() { close(); }

private:
  FluxVideo() = default;
  ~FluxVideo() { close(); }

  // ── State ─────────────────────────────────────────────────────────────────
  std::atomic<State> s_state{State::Idle};
  std::atomic<int64_t> s_positionUs{0};
  std::atomic<int64_t> s_durationUs{0};
  std::atomic<int> s_videoWidth{0};
  std::atomic<int> s_videoHeight{0};
  std::atomic<bool> s_newFrame{false};

  // ── Frame double-buffer ───────────────────────────────────────────────────
  std::mutex s_frameMutex;
  std::vector<uint8_t> s_frameData;
  int s_frameWidth = 0;
  int s_frameHeight = 0;
  int s_frameStride = 0;

  // ── Audio ring buffer ─────────────────────────────────────────────────────
  // ~4 s at 48 kHz stereo (stored as interleaved float pairs → mixed to mono
  // by _pullAudio).  We allocate per-open in _decodeLoop.
  static constexpr int kAudioRing = 48000 * 4 * 2; // 2 channels worst case
  std::vector<float> s_audioBuf;
  int s_audioWrite = 0;
  int s_audioRead = 0;
  std::mutex s_audioMutex;
  int s_audioChannels = 1;
  int s_audioSampleRate = 44100;

  // ── Command channel ───────────────────────────────────────────────────────
  std::mutex s_cmdMutex;
  std::condition_variable s_pauseCV;
  bool s_pauseRequested = false;
  bool s_resumeRequested = false;
  bool s_stopDecode = false;
  bool s_seekPending = false;
  int64_t s_seekTargetUs = 0;

  // ── Thread ────────────────────────────────────────────────────────────────
  std::thread s_decodeThread;
  std::atomic<bool> s_didFireFinish{false};

  // ── Callbacks ─────────────────────────────────────────────────────────────
  FinishCallback s_finishCallback;
  std::function<void(int w, int h)> s_readyCallback;

  // ── Clock ─────────────────────────────────────────────────────────────────
  std::chrono::steady_clock::time_point s_clockBase;
  int64_t s_clockBaseUs = 0;

  void _queueSeek(int64_t us) {
    std::lock_guard<std::mutex> lk(s_cmdMutex);
    s_seekTargetUs = us;
    s_seekPending = true;
    s_pauseCV.notify_all();
  }

  // =========================================================================
  // DECODE LOOP
  // =========================================================================

  void _decodeLoop(std::string path) {

    // ── Open container ────────────────────────────────────────────────────
    AVFormatContext *fmtCtx = nullptr;
    if (avformat_open_input(&fmtCtx, path.c_str(), nullptr, nullptr) < 0) {

      s_state = State::Error;
      return;
    }
    if (avformat_find_stream_info(fmtCtx, nullptr) < 0) {

      avformat_close_input(&fmtCtx);
      s_state = State::Error;
      return;
    }

    // ── Find streams ──────────────────────────────────────────────────────
    int videoStreamIdx = -1, audioStreamIdx = -1;
    for (unsigned i = 0; i < fmtCtx->nb_streams; i++) {
      AVMediaType type = fmtCtx->streams[i]->codecpar->codec_type;
      if (type == AVMEDIA_TYPE_VIDEO && videoStreamIdx < 0)
        videoStreamIdx = (int)i;
      if (type == AVMEDIA_TYPE_AUDIO && audioStreamIdx < 0)
        audioStreamIdx = (int)i;
    }
    if (videoStreamIdx < 0) {

      avformat_close_input(&fmtCtx);
      s_state = State::Error;
      return;
    }

    // Duration (microseconds)
    if (fmtCtx->duration != AV_NOPTS_VALUE)
      s_durationUs = fmtCtx->duration; // AV_TIME_BASE = 1 000 000 = µs

    // ── Open video codec ──────────────────────────────────────────────────
    AVStream *vStream = fmtCtx->streams[videoStreamIdx];
    const AVCodec *vCodec = avcodec_find_decoder(vStream->codecpar->codec_id);
    AVCodecContext *vCtx = avcodec_alloc_context3(vCodec);
    avcodec_parameters_to_context(vCtx, vStream->codecpar);
    if (avcodec_open2(vCtx, vCodec, nullptr) < 0) {

      avcodec_free_context(&vCtx);
      avformat_close_input(&fmtCtx);
      s_state = State::Error;
      return;
    }

    int vidW = vCtx->width, vidH = vCtx->height;
    s_videoWidth = vidW;
    s_videoHeight = vidH;

    // ── SwsContext: YUV → RGB24 ───────────────────────────────────────────
    SwsContext *swsCtx =
        sws_getContext(vidW, vidH, vCtx->pix_fmt, vidW, vidH, AV_PIX_FMT_RGB24,
                       SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!swsCtx) {

      avcodec_free_context(&vCtx);
      avformat_close_input(&fmtCtx);
      s_state = State::Error;
      return;
    }

    // ── Open audio codec (optional) ───────────────────────────────────────
    AVCodecContext *aCtx = nullptr;
    bool hasAudio = false;
    if (audioStreamIdx >= 0) {
      AVStream *aStream = fmtCtx->streams[audioStreamIdx];
      const AVCodec *aCodec = avcodec_find_decoder(aStream->codecpar->codec_id);
      aCtx = avcodec_alloc_context3(aCodec);
      avcodec_parameters_to_context(aCtx, aStream->codecpar);
      if (avcodec_open2(aCtx, aCodec, nullptr) == 0) {
        s_audioSampleRate = aCtx->sample_rate;
        s_audioChannels = aCtx->ch_layout.nb_channels;
        hasAudio = true;

      } else {
        avcodec_free_context(&aCtx);
        aCtx = nullptr;
      }
    }

    // ── Allocate frame buffer ─────────────────────────────────────────────
    {
      std::lock_guard<std::mutex> lk(s_frameMutex);
      s_frameWidth = vidW;
      s_frameHeight = vidH;
      s_frameStride = vidW * 3; // RGB24
      s_frameData.assign((size_t)(vidW * vidH * 3), 0);
    }

    // ── Allocate audio ring ───────────────────────────────────────────────
    if (hasAudio) {
      std::lock_guard<std::mutex> lk(s_audioMutex);
      s_audioBuf.assign(kAudioRing, 0.f);
      s_audioWrite = 0;
      s_audioRead = 0;
    }

    // ── Reusable AVFrames & packet ────────────────────────────────────────
    AVFrame *vFrame = av_frame_alloc();
    AVFrame *aFrame = av_frame_alloc();
    AVPacket *pkt = av_packet_alloc();

    // RGB destination for sws_scale
    std::vector<uint8_t> rgbBuf((size_t)(vidW * vidH * 3));
    uint8_t *rgbData[1] = {rgbBuf.data()};
    int rgbLinesize[1] = {vidW * 3};

    // ── Start audio stream ────────────────────────────────────────────────
    if (hasAudio) {
      FluxAudio::get().playStream(
          [this](float *buf, int frames) -> int {
            return _pullAudio(buf, frames);
          },
          s_audioSampleRate);
    }

    // ── Signal ready ──────────────────────────────────────────────────────
    if (s_readyCallback)
      s_readyCallback(vidW, vidH);

    // ── Wait for first play() call ────────────────────────────────────────
    s_state = State::Paused;
    FluxAudio::get().pause();
    {
      std::unique_lock<std::mutex> lk(s_cmdMutex);
      s_pauseCV.wait(lk, [this] { return s_resumeRequested || s_stopDecode; });
      if (s_stopDecode)
        goto cleanup;
      s_resumeRequested = false;
      s_state = State::Playing;
      FluxAudio::get().resume();
    }

    // Reset clock
    s_clockBase = std::chrono::steady_clock::now();
    s_clockBaseUs = 0;

    // ── Main decode loop ──────────────────────────────────────────────────
    while (true) {

      // ── Command processing ────────────────────────────────────────────
      {
        std::unique_lock<std::mutex> lk(s_cmdMutex);

        if (s_stopDecode)
          break;

        if (s_seekPending) {
          int64_t target = s_seekTargetUs;
          s_seekPending = false;
          lk.unlock();
          _doSeek(fmtCtx, vCtx, aCtx, videoStreamIdx, audioStreamIdx, target);
          continue;
        }

        if (s_pauseRequested) {
          s_pauseRequested = false;
          s_state = State::Paused;
          FluxAudio::get().pause();

          s_pauseCV.wait(lk, [this] {
            return s_resumeRequested || s_stopDecode || s_seekPending;
          });

          if (s_stopDecode)
            break;

          if (s_seekPending) {
            int64_t target = s_seekTargetUs;
            s_seekPending = false;
            lk.unlock();
            _doSeek(fmtCtx, vCtx, aCtx, videoStreamIdx, audioStreamIdx, target);
            continue;
          }

          s_resumeRequested = false;
          s_state = State::Playing;
          FluxAudio::get().resume();
          s_clockBase = std::chrono::steady_clock::now();
          s_clockBaseUs = s_positionUs.load();
        }
      }

      // ── Read one packet ───────────────────────────────────────────────
      int ret = av_read_frame(fmtCtx, pkt);
      if (ret == AVERROR_EOF) {
        // Flush video decoder
        avcodec_send_packet(vCtx, nullptr);
        while (avcodec_receive_frame(vCtx, vFrame) == 0)
          _processVideoFrame(vFrame, vCtx, swsCtx, rgbData, rgbLinesize,
                             vStream);
        // EOS
        s_state = State::Finished;

        {
          bool expected = false;
          if (s_didFireFinish.compare_exchange_strong(expected, true))
            if (s_finishCallback)
              s_finishCallback();
        }
        break;
      }
      if (ret < 0) {

        break;
      }

      // ── Route by stream ───────────────────────────────────────────────
      if (pkt->stream_index == videoStreamIdx) {
        avcodec_send_packet(vCtx, pkt);
        while (avcodec_receive_frame(vCtx, vFrame) == 0)
          _processVideoFrame(vFrame, vCtx, swsCtx, rgbData, rgbLinesize,
                             vStream);
      } else if (pkt->stream_index == audioStreamIdx && hasAudio) {
        avcodec_send_packet(aCtx, pkt);
        while (avcodec_receive_frame(aCtx, aFrame) == 0)
          _processAudioFrame(aFrame);
      }

      av_packet_unref(pkt);
    }

  cleanup:
    av_packet_free(&pkt);
    av_frame_free(&vFrame);
    av_frame_free(&aFrame);
    sws_freeContext(swsCtx);
    avcodec_free_context(&vCtx);
    if (aCtx)
      avcodec_free_context(&aCtx);
    avformat_close_input(&fmtCtx);
  }

  // =========================================================================
  // VIDEO FRAME PROCESSING
  // =========================================================================

  void _processVideoFrame(AVFrame *frame, AVCodecContext *vCtx,
                          SwsContext *swsCtx, uint8_t **rgbData,
                          int *rgbLinesize, AVStream *vStream) {
    // Convert PTS to microseconds
    int64_t pts = frame->best_effort_timestamp;
    if (pts == AV_NOPTS_VALUE)
      pts = frame->pts;
    int64_t ptsUs = av_rescale_q(pts, vStream->time_base, {1, 1000000});

    // A/V sync sleep
    _syncToPresentation(ptsUs);
    s_positionUs = ptsUs;

    // YUV → RGB24
    sws_scale(swsCtx, frame->data, frame->linesize, 0, vCtx->height, rgbData,
              rgbLinesize);

    // Copy into double-buffer
    int w = s_frameWidth, h = s_frameHeight;
    int expected = w * h * 3;
    if (expected > 0) {
      std::vector<uint8_t> local(rgbData[0], rgbData[0] + expected);
      std::lock_guard<std::mutex> lk(s_frameMutex);
      s_frameData = std::move(local);
      s_newFrame = true;
    }
  }

  // =========================================================================
  // AUDIO FRAME PROCESSING
  // =========================================================================

  void _processAudioFrame(AVFrame *frame) {
    // FFmpeg may give us planar (FLTP) or packed (FLT/S16 etc.) audio.
    // We resample everything to float mono manually.
    int ch = s_audioChannels;
    int frames = frame->nb_samples;
    bool isPlanar = av_sample_fmt_is_planar((AVSampleFormat)frame->format) != 0;
    bool isFloat = (frame->format == AV_SAMPLE_FMT_FLTP ||
                    frame->format == AV_SAMPLE_FMT_FLT);
    bool isS16 = (frame->format == AV_SAMPLE_FMT_S16P ||
                  frame->format == AV_SAMPLE_FMT_S16);

    std::lock_guard<std::mutex> lk(s_audioMutex);
    int wr = s_audioWrite;
    int rd = s_audioRead;
    int space = kAudioRing - (wr - rd);
    frames = std::min(frames, space); // drop if ring is full

    for (int i = 0; i < frames; i++) {
      float sum = 0.f;
      for (int c = 0; c < ch; c++) {
        if (isFloat) {
          if (isPlanar) {
            const float *plane = (const float *)frame->data[c];
            sum += plane[i];
          } else {
            const float *packed = (const float *)frame->data[0];
            sum += packed[i * ch + c];
          }
        } else if (isS16) {
          if (isPlanar) {
            const int16_t *plane = (const int16_t *)frame->data[c];
            sum += plane[i] / 32768.f;
          } else {
            const int16_t *packed = (const int16_t *)frame->data[0];
            sum += packed[i * ch + c] / 32768.f;
          }
        }
        // Other formats: silence (extend as needed)
      }
      float mono = sum / (float)ch;
      if (mono > 1.f)
        mono = 1.f;
      if (mono < -1.f)
        mono = -1.f;
      s_audioBuf[wr % kAudioRing] = mono;
      wr++;
    }
    s_audioWrite = wr;
  }

  // =========================================================================
  // AUDIO PULL (FluxAudio stream callback — audio thread)
  // =========================================================================

  int _pullAudio(float *buf, int frames) {
    std::lock_guard<std::mutex> lk(s_audioMutex);
    int rd = s_audioRead;
    int wr = s_audioWrite;
    int avail = wr - rd;
    int toCopy = std::min(frames, avail);

    for (int i = 0; i < toCopy; i++)
      buf[i] = s_audioBuf[(size_t)((rd + i) % kAudioRing)];
    for (int i = toCopy; i < frames; i++)
      buf[i] = 0.f; // underrun: silence

    s_audioRead = rd + toCopy;
    // Keep stream alive even on underrun (return `frames` not `toCopy`)
    return frames;
  }

  // =========================================================================
  // SEEK
  // =========================================================================

  void _doSeek(AVFormatContext *fmtCtx, AVCodecContext *vCtx,
               AVCodecContext *aCtx, int videoStreamIdx, int audioStreamIdx,
               int64_t targetUs) {
    // av_seek_frame wants PTS in stream time base
    int64_t ts = av_rescale_q(targetUs, {1, 1000000},
                              fmtCtx->streams[videoStreamIdx]->time_base);
    av_seek_frame(fmtCtx, videoStreamIdx, ts, AVSEEK_FLAG_BACKWARD);

    avcodec_flush_buffers(vCtx);
    if (aCtx)
      avcodec_flush_buffers(aCtx);

    // Flush audio ring
    {
      std::lock_guard<std::mutex> lk(s_audioMutex);
      s_audioWrite = 0;
      s_audioRead = 0;
    }
    FluxAudio::get().seekToSeconds((float)targetUs / 1e6f);

    // Re-sync clock
    s_clockBase = std::chrono::steady_clock::now();
    s_clockBaseUs = targetUs;
    s_positionUs = targetUs;
    s_didFireFinish = false;
  }

  // =========================================================================
  // A/V SYNC — interruptible sleep in 5 ms chunks
  // =========================================================================

  void _syncToPresentation(int64_t framePtsUs) {
    auto now = std::chrono::steady_clock::now();
    int64_t wallUs =
        std::chrono::duration_cast<std::chrono::microseconds>(now - s_clockBase)
            .count();
    int64_t targetUs = framePtsUs - s_clockBaseUs;
    int64_t sleepUs = targetUs - wallUs;

    while (sleepUs > 2000 && !s_stopDecode) {
      int64_t chunk = std::min(sleepUs, (int64_t)5000); // 5 ms max
      std::this_thread::sleep_for(std::chrono::microseconds(chunk));

      now = std::chrono::steady_clock::now();
      wallUs = std::chrono::duration_cast<std::chrono::microseconds>(
                   now - s_clockBase)
                   .count();
      sleepUs = targetUs - wallUs;
    }
    // Behind schedule → present immediately (natural frame drop)
  }
};

#endif // __linux__ && !__ANDROID__