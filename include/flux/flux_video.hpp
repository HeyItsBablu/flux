#pragma once
// flux_video.hpp
// Platform-agnostic FluxVideo interface.
//
// Backends:
//   flux_video_android.cpp  — MediaCodec + SurfaceTexture + AAudio
//   flux_video_win32.cpp    — Media Foundation + FluxAudio
//   flux_video_linux.cpp    — FFmpeg (libav*) + FluxAudio
//
// Add the matching .cpp to your CMakeLists platform block; never include
// more than one backend in the same build.
//
// ── Threading model ──────────────────────────────────────────────────────────
//   All decoding runs on a single background thread spun up by open().
//   The main (GL/UI) thread calls updateFrame() once per vsync (Android) or
//   lockFrame() after hasNewFrame() (Win32/Linux) to consume decoded pixels.
//
// ── Audio ────────────────────────────────────────────────────────────────────
//   Audio is routed through FluxAudio on every platform.  The backend feeds
//   a PCM ring buffer; FluxAudio pulls from it via a stream callback.
//
// ── Usage ────────────────────────────────────────────────────────────────────
//   FluxVideo::get().open("path/to/file.mp4");
//   FluxVideo::get().play();
//   // per-vsync (Android):
//   if (FluxVideo::get().updateFrame()) { /* texture updated */ }
//   // per-vsync (Win32 / Linux):
//   if (FluxVideo::get().hasNewFrame()) {
//       auto f = FluxVideo::get().lockFrame();
//       // blit f.data (RGB24, f.width × f.height, stride = f.stride)
//   }

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#ifdef __ANDROID__
#include <GLES2/gl2.h> // GLuint
#endif

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif
// ============================================================================
// FluxVideo
// ============================================================================

class FluxVideo
{
public:
    using FinishCallback = std::function<void()>;

    // ── Singleton ─────────────────────────────────────────────────────────────
    static FluxVideo &get();

    // ── Playback state ─────────────────────────────────────────────────────────
    enum class State
    {
        Idle,
        Loading,
        Playing,
        Paused,
        Finished,
        Error
    };

    State getState() const;
    bool isPlaying() const;
    bool isPaused() const;
    bool isFinished() const;
    float getDurationSeconds() const;
    float getPositionSeconds() const;
    float getProgress() const;
    int getVideoWidth() const;
    int getVideoHeight() const;

    // ── Callbacks ──────────────────────────────────────────────────────────────
    void setOnFinished(FinishCallback cb);
    void setOnReady(std::function<void(int w, int h)> cb);

    // ── Open / close ───────────────────────────────────────────────────────────
    // open() is non-blocking: it starts the decode thread and returns
    // immediately. Listen for onReady to know when the first frame is decoded.
    // Must be called on the GL thread on Android (creates the OES texture).
    bool open(const std::string &path);
    void close();

    // ── Playback control ───────────────────────────────────────────────────────
    void play();
    void pause();
    void seekToProgress(float p); // [0, 1]
    void seekToSeconds(float secs);
    void setVolume(float v);
    float getVolume() const;

    // ── Frame consumption ──────────────────────────────────────────────────────

    // Android — OES texture path.
    // Call once per vsync on the GL thread.  Returns true if the texture
    // was updated this vsync.
#ifdef __ANDROID__
    bool updateFrame();
    bool hasNewFrame() const;
    GLuint getTextureId() const;
#endif

    // Win32 / Linux — CPU pixel buffer path.
    // Check hasNewFrame(), then lock to read the RGB24 pixels.
    // The lock releases the mutex when it goes out of scope.
#if defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || (defined(__APPLE__) && TARGET_OS_OSX)
    bool hasNewFrame() const;

    struct FrameLock
    {
        std::unique_lock<std::mutex> lock;
        const uint8_t *data = nullptr;
        int width = 0;
        int height = 0;
        int stride = 0;
    };
    FrameLock lockFrame();
#endif

private:
    // No data members — all state lives in an anonymous-namespace struct
    // inside the platform .cpp.  The singleton is a zero-size token object.
    FluxVideo() = default;
    ~FluxVideo() = default;

    FluxVideo(const FluxVideo &) = delete;
    FluxVideo &operator=(const FluxVideo &) = delete;
};