// flux_video.hpp
// MediaCodec + SurfaceTexture + AAudio A/V sync engine for FluxUI on Android.
//
// Pipeline:
//   MediaExtractor  →  MediaCodec (video) → SurfaceTexture → GL_TEXTURE_EXTERNAL_OES
//                  └→  MediaCodec (audio) → PCM float       → AAudio (via FluxAudio)
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
#ifdef __ANDROID__

#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>                 // GL_TEXTURE_EXTERNAL_OES

#include <media/NdkMediaExtractor.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaFormat.h>
#include <media/NdkImage.h>               // not used directly but useful for reference

#include <android/asset_manager.h>
#include <android/log.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "flux_audio.hpp"                 // PCM playback + FluxAndroid_getAssetManager

#define VIDEO_LOGI(...) __android_log_print(ANDROID_LOG_INFO,  "FluxVideo", __VA_ARGS__)
#define VIDEO_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "FluxVideo", __VA_ARGS__)

// ── External helpers provided by native-lib.cpp ───────────────────────────────
extern EGLDisplay FluxAndroid_getEGLDisplay();
extern EGLContext FluxAndroid_getEGLContext();
extern EGLSurface FluxAndroid_getEGLSurface();

// JNI shims -- forward-declared here so the FluxVideo class body can call them
extern void*          FluxVideo_createSurfaceTexture(GLuint texId);
extern void           FluxVideo_updateTexImage(void* surfaceTexture);
extern ANativeWindow* FluxVideo_getNativeWindow(void* surfaceTexture);
extern void           FluxVideo_destroySurfaceTexture(void* surfaceTexture);

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

    State     getState()            const { return s_state.load(); }
    bool      isPlaying()           const { return s_state == State::Playing; }
    bool      isPaused()            const { return s_state == State::Paused; }
    bool      isFinished()          const { return s_state == State::Finished; }
    float     getDurationSeconds()  const { return s_durationUs.load() / 1e6f; }
    float     getPositionSeconds()  const { return s_positionUs.load() / 1e6f; }
    float     getProgress()         const {
        int64_t dur = s_durationUs.load();
        return (dur > 0) ? std::min(1.f, (float)s_positionUs.load() / dur) : 0.f;
    }
    int       getVideoWidth()       const { return s_videoWidth.load(); }
    int       getVideoHeight()      const { return s_videoHeight.load(); }

    // ── GL texture (OES) — read on the main thread after updateFrame() ────────
    GLuint    getTextureId()        const { return s_oesTexture; }
    // True after updateFrame() latched a new frame this vsync
    bool      hasNewFrame()         const { return s_newFrame.load(); }

    // ── Callbacks ─────────────────────────────────────────────────────────────
    void setOnFinished(FinishCallback cb)  { s_finishCallback = std::move(cb); }
    void setOnReady(std::function<void(int w, int h)> cb) { s_readyCallback = std::move(cb); }

    // =========================================================================
    // OPEN — loads file, creates codec + OES texture, starts decode thread
    // "video/sample.mp4" → asset; "/sdcard/..." → filesystem
    // =========================================================================

    bool open(const std::string& path) {
        close();

        s_state = State::Loading;
        s_path  = path;

        // ── Create OES texture on the GL thread (caller must be on GL thread) ─
        if (!_createOESTexture()) return false;

        // ── Create offscreen EGL context for the decode thread ────────────────
        if (!_createDecodeEGLContext()) return false;

        // ── Spin up decode thread ─────────────────────────────────────────────
        s_stopDecode = false;
        s_decodeThread = std::thread(&FluxVideo::_decodeLoop, this);

        return true;
    }

    // =========================================================================
    // PLAYBACK CONTROL
    // =========================================================================

    void play() {
        if (s_state == State::Paused || s_state == State::Loading) {
            s_state = State::Playing;
            FluxAudio::get().resume();
        }
    }

    void pause() {
        if (s_state == State::Playing) {
            s_state = State::Paused;
            FluxAudio::get().pause();
        }
    }

    // Seek to progress 0.0–1.0.  Queues a seek; decode thread picks it up.
    void seekToProgress(float p) {
        int64_t us = (int64_t)(std::max(0.f, std::min(1.f, p)) * s_durationUs.load());
        _queueSeek(us);
    }

    void seekToSeconds(float secs) {
        int64_t us = (int64_t)(secs * 1e6f);
        us = std::max<int64_t>(0, std::min(us, s_durationUs.load()));
        _queueSeek(us);
    }

    // Volume (passed to FluxAudio)
    void  setVolume(float v) { FluxAudio::get().setVolume(v); }
    float getVolume()  const { return FluxAudio::get().getVolume(); }

    // =========================================================================
    // updateFrame() — call once per vsync on the GL thread.
    // Latches the newest video frame into the OES texture via
    // SurfaceTexture::updateTexImage (called through JNI shim below).
    // Returns true if the texture changed.
    // =========================================================================

    bool updateFrame() {
        if (!s_surfaceTexture) { s_newFrame = false; return false; }
        if (!s_pendingFrame.load()) { s_newFrame = false; return false; }

        // updateTexImage must be called on a thread that holds the EGL context
        // bound to the surface — that is the main/GL thread here.
        //
        // We call the JNI shim FluxVideo_updateTexImage() which is implemented
        // in native-lib.cpp and simply calls:
        //   surfaceTextureObj.updateTexImage();
        //
        // Alternatively on API 28+ you can use AHardwareBuffer + glEGLImageTargetTexture2DOES
        // but the JNI shim is simpler and covers API 21+.
        FluxVideo_updateTexImage(s_surfaceTexture);

        s_pendingFrame = false;
        s_newFrame     = true;
        return true;
    }

    // =========================================================================
    // CLOSE
    // =========================================================================

    void close() {
        s_stopDecode = true;
        if (s_decodeThread.joinable()) s_decodeThread.join();

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

        _destroyDecodeEGLContext();

        s_state       = State::Idle;
        s_positionUs  = 0;
        s_durationUs  = 0;
        s_pendingFrame = false;
        s_newFrame     = false;
        s_seekPending  = false;
        s_videoWidth   = 0;
        s_videoHeight  = 0;
        s_surfaceTexture = nullptr;
    }

private:
    FluxVideo() = default;
    ~FluxVideo() { close(); }

    // ── Path ──────────────────────────────────────────────────────────────────
    std::string s_path;

    // ── State ─────────────────────────────────────────────────────────────────
    std::atomic<State>   s_state       { State::Idle };
    std::atomic<int64_t> s_positionUs  { 0 };
    std::atomic<int64_t> s_durationUs  { 0 };
    std::atomic<int>     s_videoWidth  { 0 };
    std::atomic<int>     s_videoHeight { 0 };

    // ── Frame sync ────────────────────────────────────────────────────────────
    std::atomic<bool>    s_pendingFrame { false };  // decode thread → GL thread
    std::atomic<bool>    s_newFrame     { false };  // set by updateFrame()

    // ── Seek ──────────────────────────────────────────────────────────────────
    std::atomic<bool>    s_seekPending  { false };
    std::atomic<int64_t> s_seekTargetUs { 0 };

    // ── Decode thread ─────────────────────────────────────────────────────────
    std::thread          s_decodeThread;
    std::atomic<bool>    s_stopDecode   { false };

    // ── MediaCodec objects ────────────────────────────────────────────────────
    AMediaExtractor*     s_extractor       = nullptr;
    AMediaCodec*         s_videoCodec      = nullptr;
    AMediaCodec*         s_audioCodec      = nullptr;
    int                  s_videoTrackIdx   = -1;
    int                  s_audioTrackIdx   = -1;

    // ── GL / EGL ──────────────────────────────────────────────────────────────
    GLuint               s_oesTexture      = 0;
    void*                s_surfaceTexture   = nullptr;   // jobject (global ref)
    EGLDisplay           s_decodeEGLDisplay = EGL_NO_DISPLAY;
    EGLContext           s_decodeEGLContext = EGL_NO_CONTEXT;
    EGLSurface           s_decodeEGLSurface = EGL_NO_SURFACE;

    // ── Callbacks ─────────────────────────────────────────────────────────────
    FinishCallback                       s_finishCallback;
    std::function<void(int w, int h)>    s_readyCallback;

    // ── PCM ring buffer for audio ──────────────────────────────────────────────
    static constexpr int kAudioRingSize = 48000 * 2;   // ~2 s at 48kHz
    std::vector<float>   s_audioBuf;
    std::atomic<int>     s_audioWrite  { 0 };
    std::atomic<int>     s_audioRead   { 0 };
    std::mutex           s_audioMutex;

    // ── Clock ─────────────────────────────────────────────────────────────────
    // Driven by audio position (most accurate) or wall clock fallback
    std::chrono::steady_clock::time_point s_clockBase;
    int64_t s_clockBaseUs = 0;

    // =========================================================================
    // OES TEXTURE — created on GL thread before decode thread starts
    // =========================================================================

    bool _createOESTexture() {
        glGenTextures(1, &s_oesTexture);
        if (!s_oesTexture) {
            VIDEO_LOGE("glGenTextures failed");
            return false;
        }
        glBindTexture(GL_TEXTURE_EXTERNAL_OES, s_oesTexture);
        glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_EXTERNAL_OES, 0);

        // Create Android SurfaceTexture wrapping this OES texture via JNI shim
        s_surfaceTexture = FluxVideo_createSurfaceTexture(s_oesTexture);
        if (!s_surfaceTexture) {
            VIDEO_LOGE("FluxVideo_createSurfaceTexture failed");
            return false;
        }
        VIDEO_LOGI("OES texture %u + SurfaceTexture created", s_oesTexture);
        return true;
    }

    // =========================================================================
    // EGL CONTEXT — decode thread needs its own context sharing with main
    // =========================================================================

    bool _createDecodeEGLContext() {
        s_decodeEGLDisplay = FluxAndroid_getEGLDisplay();
        if (s_decodeEGLDisplay == EGL_NO_DISPLAY) return false;

        EGLint attribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
        s_decodeEGLContext = eglCreateContext(
                s_decodeEGLDisplay,
                _getEGLConfig(),
                FluxAndroid_getEGLContext(),   // share with main context
                attribs);

        if (s_decodeEGLContext == EGL_NO_CONTEXT) {
            VIDEO_LOGE("eglCreateContext for decode thread failed: %d", eglGetError());
            return false;
        }

        // Pbuffer surface (1×1) — decode thread only signals the SurfaceTexture,
        // actual rendering happens on the main thread
        EGLint pbAttribs[] = { EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE };
        s_decodeEGLSurface = eglCreatePbufferSurface(
                s_decodeEGLDisplay, _getEGLConfig(), pbAttribs);

        return true;
    }

    void _destroyDecodeEGLContext() {
        if (s_decodeEGLDisplay != EGL_NO_DISPLAY) {
            if (s_decodeEGLContext != EGL_NO_CONTEXT)
                eglDestroyContext(s_decodeEGLDisplay, s_decodeEGLContext);
            if (s_decodeEGLSurface != EGL_NO_SURFACE)
                eglDestroySurface(s_decodeEGLDisplay, s_decodeEGLSurface);
        }
        s_decodeEGLContext = EGL_NO_CONTEXT;
        s_decodeEGLSurface = EGL_NO_SURFACE;
    }

    EGLConfig _getEGLConfig() {
        EGLint attribs[] = {
                EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
                EGL_SURFACE_TYPE,    EGL_PBUFFER_BIT,
                EGL_NONE
        };
        EGLConfig config;
        EGLint    numConfigs;
        eglChooseConfig(s_decodeEGLDisplay, attribs, &config, 1, &numConfigs);
        return config;
    }

    // =========================================================================
    // FILE LOADING — supports assets and absolute paths
    // =========================================================================

    int _openExtractor() {
        // Open via asset or file
        if (!s_path.empty() && s_path[0] == '/') {
            s_extractor = AMediaExtractor_new();
            if (AMediaExtractor_setDataSource(s_extractor, s_path.c_str()) != AMEDIA_OK) {
                VIDEO_LOGE("setDataSource failed for %s", s_path.c_str());
                return -1;
            }
        } else {
            AAssetManager* am = FluxAndroid_getAssetManager();
            if (!am) return -1;
            AAsset* asset = AAssetManager_open(am, s_path.c_str(), AASSET_MODE_STREAMING);
            if (!asset) { VIDEO_LOGE("asset not found: %s", s_path.c_str()); return -1; }

            // AMediaExtractor can consume an AAsset directly via its file descriptor
            off_t start, len;
            int fd = AAsset_openFileDescriptor(asset, &start, &len);
            AAsset_close(asset);
            if (fd < 0) return -1;

            s_extractor = AMediaExtractor_new();
            AMediaExtractor_setDataSourceFd(s_extractor, fd, start, len);
            ::close(fd);
        }

        // Find video and audio tracks
        int trackCount = (int)AMediaExtractor_getTrackCount(s_extractor);
        for (int i = 0; i < trackCount; i++) {
            AMediaFormat* fmt = AMediaExtractor_getTrackFormat(s_extractor, i);
            const char* mime = nullptr;
            AMediaFormat_getString(fmt, AMEDIAFORMAT_KEY_MIME, &mime);
            if (mime) {
                if (strncmp(mime, "video/", 6) == 0 && s_videoTrackIdx < 0)
                    s_videoTrackIdx = i;
                if (strncmp(mime, "audio/", 6) == 0 && s_audioTrackIdx < 0)
                    s_audioTrackIdx = i;
            }

            // Get duration from the first track that has it
            int64_t dur = 0;
            if (AMediaFormat_getInt64(fmt, AMEDIAFORMAT_KEY_DURATION, &dur) && dur > 0)
                s_durationUs.store(dur);

            AMediaFormat_delete(fmt);
        }

        if (s_videoTrackIdx < 0) { VIDEO_LOGE("no video track"); return -1; }
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
        AMediaFormat_getInt32(fmt, AMEDIAFORMAT_KEY_WIDTH,  &w);
        AMediaFormat_getInt32(fmt, AMEDIAFORMAT_KEY_HEIGHT, &h);
        s_videoWidth  = w;
        s_videoHeight = h;
        VIDEO_LOGI("Video: %s %dx%d", mime ? mime : "?", w, h);

        s_videoCodec = AMediaCodec_createDecoderByType(mime);
        if (!s_videoCodec) {
            VIDEO_LOGE("Cannot create video codec for %s", mime);
            AMediaFormat_delete(fmt);
            return false;
        }

        // Configure codec to output to our SurfaceTexture's Surface
        ANativeWindow* window = FluxVideo_getNativeWindow(s_surfaceTexture);
        media_status_t status = AMediaCodec_configure(
                s_videoCodec, fmt, window, nullptr, 0);

        AMediaFormat_delete(fmt);

        if (status != AMEDIA_OK) {
            VIDEO_LOGE("Video codec configure failed: %d", status);
            return false;
        }

        AMediaExtractor_selectTrack(s_extractor, s_videoTrackIdx);
        AMediaCodec_start(s_videoCodec);
        return true;
    }

    bool _openAudioCodec(int& outSampleRate, int& outChannels) {
        if (s_audioTrackIdx < 0) return false;

        AMediaFormat* fmt = AMediaExtractor_getTrackFormat(s_extractor, s_audioTrackIdx);
        const char* mime = nullptr;
        AMediaFormat_getString(fmt, AMEDIAFORMAT_KEY_MIME, &mime);

        int32_t sr = 44100, ch = 1;
        AMediaFormat_getInt32(fmt, AMEDIAFORMAT_KEY_SAMPLE_RATE,  &sr);
        AMediaFormat_getInt32(fmt, AMEDIAFORMAT_KEY_CHANNEL_COUNT, &ch);
        outSampleRate = sr;
        outChannels   = ch;
        VIDEO_LOGI("Audio: %s %dHz %dch", mime ? mime : "?", sr, ch);

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
        if (s_videoCodec) { AMediaCodec_stop(s_videoCodec); AMediaCodec_delete(s_videoCodec); s_videoCodec = nullptr; }
    }
    void _destroyAudioCodec() {
        if (s_audioCodec) { AMediaCodec_stop(s_audioCodec); AMediaCodec_delete(s_audioCodec); s_audioCodec = nullptr; }
    }

    // =========================================================================
    // SEEK helper
    // =========================================================================

    void _queueSeek(int64_t us) {
        s_seekTargetUs = us;
        s_seekPending  = true;
    }

    void _doSeek(int64_t targetUs) {
        // Flush both codecs
        AMediaCodec_flush(s_videoCodec);
        if (s_audioCodec) AMediaCodec_flush(s_audioCodec);

        // Seek extractor — SEEK_TO_PREVIOUS_SYNC to avoid green frames
        AMediaExtractor_seekTo(s_extractor, targetUs, AMEDIAEXTRACTOR_SEEK_PREVIOUS_SYNC);

        // Reset audio ring buffer
        {
            std::lock_guard<std::mutex> lk(s_audioMutex);
            s_audioWrite = 0;
            s_audioRead  = 0;
        }

        // Reset clock
        s_clockBaseUs = targetUs;
        s_clockBase   = std::chrono::steady_clock::now();
        s_positionUs  = targetUs;

        // Resume audio at new position
        FluxAudio::get().seekToSeconds(targetUs / 1e6f);

        s_seekPending = false;
        VIDEO_LOGI("Seeked to %.2f s", targetUs / 1e6f);
    }

    // =========================================================================
    // DECODE LOOP — runs on s_decodeThread
    // =========================================================================

    void _decodeLoop() {
        // Bind decode EGL context (needed for SurfaceTexture::releaseTexImage)
        eglMakeCurrent(s_decodeEGLDisplay, s_decodeEGLSurface,
                       s_decodeEGLSurface, s_decodeEGLContext);

        // Open extractor
        if (_openExtractor() < 0) { s_state = State::Error; return; }

        // Open codecs
        if (!_openVideoCodec()) { s_state = State::Error; return; }

        int audioSR = 44100, audioCh = 1;
        bool hasAudio = _openAudioCodec(audioSR, audioCh);

        // Start audio playback through FluxAudio stream callback
        if (hasAudio) {
            FluxAudio::get().playStream([this, audioCh](float* buf, int frames) -> int {
                return _pullAudio(buf, frames, audioCh);
            }, audioSR);
        }

        // Signal ready
        if (s_readyCallback)
            s_readyCallback(s_videoWidth.load(), s_videoHeight.load());

        s_state = State::Playing;

        // Reset clock
        s_clockBase   = std::chrono::steady_clock::now();
        s_clockBaseUs = 0;

        bool inputEOS = false;
        bool outputEOS = false;

        static constexpr int64_t kTimeoutUs = 5000;

        while (!s_stopDecode && !outputEOS) {

            // ── Handle seek ──────────────────────────────────────────────────
            if (s_seekPending.load()) {
                _doSeek(s_seekTargetUs.load());
                inputEOS = false;
                outputEOS = false;
            }

            // ── Wait while paused ────────────────────────────────────────────
            if (s_state == State::Paused) {
                std::this_thread::sleep_for(std::chrono::milliseconds(16));
                // Advance clock when we resume
                s_clockBase = std::chrono::steady_clock::now();
                s_clockBaseUs = s_positionUs.load();
                continue;
            }

            // ── Feed input to video codec ─────────────────────────────────────
            if (!inputEOS) {
                ssize_t inBuf = AMediaCodec_dequeueInputBuffer(s_videoCodec, kTimeoutUs);
                if (inBuf >= 0) {
                    size_t bufSize = 0;
                    uint8_t* data = AMediaCodec_getInputBuffer(s_videoCodec, (size_t)inBuf, &bufSize);

                    // Read from the extractor — select correct track
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
                                                         0, 0, 0, AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
                            inputEOS = true;
                        }
                    } else if (trackIdx == s_audioTrackIdx && hasAudio) {
                        // Push audio data to audio codec inline
                        _feedAudioCodec();
                        // Return video input buffer unused (will retry next loop)
                        AMediaCodec_queueInputBuffer(s_videoCodec, (size_t)inBuf,
                                                     0, 0, 0, 0);
                    } else {
                        AMediaExtractor_advance(s_extractor);
                        AMediaCodec_queueInputBuffer(s_videoCodec, (size_t)inBuf,
                                                     0, 0, 0, 0);
                    }
                }
            }

            // ── Drain video output ────────────────────────────────────────────
            AMediaCodecBufferInfo info{};
            ssize_t outBuf = AMediaCodec_dequeueOutputBuffer(s_videoCodec, &info, kTimeoutUs);
            if (outBuf >= 0) {
                bool isEOS = (info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) != 0;

                if (info.size > 0) {
                    int64_t framePts = info.presentationTimeUs;

                    // A/V sync: sleep until it's time to present this frame
                    _syncToPresentation(framePts);
                    s_positionUs = framePts;

                    // Release to Surface (renders into SurfaceTexture)
                    AMediaCodec_releaseOutputBuffer(s_videoCodec, (size_t)outBuf, true);
                    s_pendingFrame = true;   // main thread will call updateTexImage
                } else {
                    AMediaCodec_releaseOutputBuffer(s_videoCodec, (size_t)outBuf, false);
                }

                if (isEOS) {
                    outputEOS = true;
                    s_state = State::Finished;
                    if (s_finishCallback) s_finishCallback();
                }
            } else if (outBuf == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
                AMediaFormat* fmt = AMediaCodec_getOutputFormat(s_videoCodec);
                int32_t w = 0, h = 0;
                AMediaFormat_getInt32(fmt, AMEDIAFORMAT_KEY_WIDTH,  &w);
                AMediaFormat_getInt32(fmt, AMEDIAFORMAT_KEY_HEIGHT, &h);
                s_videoWidth  = w;
                s_videoHeight = h;
                AMediaFormat_delete(fmt);
            }

            // ── Drain audio codec → ring buffer ───────────────────────────────
            if (hasAudio) _drainAudioCodec(audioCh);
        }

        eglMakeCurrent(s_decodeEGLDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        VIDEO_LOGI("Decode loop exited");
    }

    // =========================================================================
    // A/V SYNC — sleep until presentation time
    // =========================================================================

    void _syncToPresentation(int64_t framePtsUs) {
        auto now     = std::chrono::steady_clock::now();
        int64_t wallUs = std::chrono::duration_cast<std::chrono::microseconds>(
                now - s_clockBase).count();
        int64_t targetUs = framePtsUs - s_clockBaseUs;
        int64_t sleepUs  = targetUs - wallUs;

        if (sleepUs > 2000) {
            // Don't sleep longer than ~100ms — allows seek/stop to interrupt
            sleepUs = std::min(sleepUs, (int64_t)100000);
            std::this_thread::sleep_for(std::chrono::microseconds(sleepUs));
        }
        // If we're behind, just render immediately (drop tolerance)
    }

    // =========================================================================
    // AUDIO CODEC — feed extractor samples → decode → ring buffer
    // =========================================================================

    void _feedAudioCodec() {
        if (!s_audioCodec) return;
        ssize_t inBuf = AMediaCodec_dequeueInputBuffer(s_audioCodec, 1000);
        if (inBuf < 0) return;

        size_t bufSize = 0;
        uint8_t* data = AMediaCodec_getInputBuffer(s_audioCodec, (size_t)inBuf, &bufSize);

        // We only call this when the extractor is on the audio track
        ssize_t sampleSize = AMediaExtractor_readSampleData(s_extractor, data, bufSize);
        int64_t pts = AMediaExtractor_getSampleTime(s_extractor);

        if (sampleSize > 0) {
            AMediaCodec_queueInputBuffer(s_audioCodec, (size_t)inBuf,
                                         0, (size_t)sampleSize, pts, 0);
            AMediaExtractor_advance(s_extractor);
        } else {
            AMediaCodec_queueInputBuffer(s_audioCodec, (size_t)inBuf,
                                         0, 0, 0, AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
        }
    }

    void _drainAudioCodec(int channels) {
        if (!s_audioCodec) return;

        AMediaCodecBufferInfo info{};
        ssize_t outBuf = AMediaCodec_dequeueOutputBuffer(s_audioCodec, &info, 0);
        if (outBuf < 0) return;

        if (info.size > 0) {
            size_t sz = 0;
            uint8_t* data = AMediaCodec_getOutputBuffer(s_audioCodec, (size_t)outBuf, &sz);

            // Audio output from MediaCodec is PCM_16 interleaved
            int frameCount = info.size / (2 * channels);
            const int16_t* pcm16 = reinterpret_cast<const int16_t*>(data + info.offset);

            std::lock_guard<std::mutex> lk(s_audioMutex);
            int wr = s_audioWrite.load();
            for (int i = 0; i < frameCount; i++) {
                // Mix channels down to mono for ring buffer (FluxAudio is mono)
                float sum = 0.f;
                for (int c = 0; c < channels; c++)
                    sum += pcm16[i * channels + c] / 32768.f;
                s_audioBuf[wr % kAudioRingSize] = sum / channels;
                wr++;
            }
            s_audioWrite = wr;
        }

        AMediaCodec_releaseOutputBuffer(s_audioCodec, (size_t)outBuf, false);
    }

    // Pull audio samples from ring buffer — called by FluxAudio stream callback
    int _pullAudio(float* buf, int frames, int /*channels*/) {
        std::lock_guard<std::mutex> lk(s_audioMutex);
        int rd  = s_audioRead.load();
        int wr  = s_audioWrite.load();
        int avail = wr - rd;
        int toCopy = std::min(frames, avail);

        for (int i = 0; i < toCopy; i++)
            buf[i] = s_audioBuf[(rd + i) % kAudioRingSize];
        for (int i = toCopy; i < frames; i++)
            buf[i] = 0.f;  // underrun: silence

        s_audioRead = rd + toCopy;
        return toCopy > 0 ? frames : 0;  // returning 0 stops the stream
    }
};


#endif // __ANDROID__