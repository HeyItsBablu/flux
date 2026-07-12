// src/flux_video_ssr.cpp
//
// Headless stub backend for FluxVideo (FLUX_SSR). Same rationale as
// flux_audio_ssr.cpp: exists only to satisfy the linker. VideoPlayerWidget
// still constructs and lays out on SSR (it paints its own black
// placeholder + bar UI via plain Painter calls, matching whatever the
// client will show before hydration boots real video), it just never
// asks this backend to actually decode or play anything.
//
// The frame-consumption methods gated on `defined(__linux__) &&
// !defined(__ANDROID__)` etc. in flux_video.hpp are declared even on an
// SSR build hosted on Linux (that guard doesn't exclude FLUX_SSR) — they
// are stubbed here too so the symbols resolve on every host, even though
// VideoPlayerWidget::render()'s platform branches already explicitly
// exclude FLUX_SSR from ever calling them.

#ifdef FLUX_SSR

#include "flux/flux_video.hpp"

FluxVideo &FluxVideo::get()
{
    static FluxVideo instance;
    return instance;
}
FluxVideo::~FluxVideo() {}

FluxVideo::State FluxVideo::getState() const { return State::Idle; }
bool FluxVideo::isPlaying() const { return false; }
bool FluxVideo::isPaused() const { return false; }
bool FluxVideo::isFinished() const { return false; }
float FluxVideo::getDurationSeconds() const { return 0.f; }
float FluxVideo::getPositionSeconds() const { return 0.f; }
float FluxVideo::getProgress() const { return 0.f; }
int FluxVideo::getVideoWidth() const { return 0; }
int FluxVideo::getVideoHeight() const { return 0; }

void FluxVideo::setOnFinished(FinishCallback) {}
void FluxVideo::setOnReady(std::function<void(int, int)>) {}

bool FluxVideo::open(const std::string &) { return false; }
void FluxVideo::close() {}

void FluxVideo::play() {}
void FluxVideo::pause() {}
void FluxVideo::seekToProgress(float) {}
void FluxVideo::seekToSeconds(float) {}
void FluxVideo::setVolume(float) {}
float FluxVideo::getVolume() const { return 1.f; }

#if defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || (defined(__APPLE__) && TARGET_OS_OSX)
bool FluxVideo::hasNewFrame() const { return false; }
FluxVideo::FrameLock FluxVideo::lockFrame()
{
    // No real mutex to lock — default-constructed unique_lock is
    // perpetually "not locked," which is fine since data stays nullptr
    // and callers are expected to check hasNewFrame() first anyway.
    return FrameLock{};
}
#endif

#endif // FLUX_SSR