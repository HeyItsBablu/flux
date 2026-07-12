// src/flux_audio_ssr.cpp
//
// Headless stub backend for FluxAudio (FLUX_SSR). No decode, no playback,
// no I/O — this exists purely so the SSR build LINKS when an app's widget
// tree includes AudioPlayerWidget, whose constructor unconditionally calls
// FluxAudio::get().setOnFinished(...) regardless of whether audio ever
// actually plays. SSR has no speakers, no event loop to deliver an async
// decode result to, and (per VideoPlayerWidget's render(), which has no
// FLUX_SSR blit branch) no paint path that would need real playback state
// anyway — every method here is a safe, inert no-op.

#ifdef FLUX_SSR

#include "flux/flux_audio.hpp"

struct FluxAudio::Impl
{
    float volume = 1.0f;
    FinishCallback finishCallback;
};

FluxAudio &FluxAudio::get()
{
    static FluxAudio inst;
    return inst;
}

FluxAudio::FluxAudio() : m_impl(new Impl()) {}
FluxAudio::~FluxAudio() { delete m_impl; }

void FluxAudio::setVolume(float v) { m_impl->volume = v; }
float FluxAudio::getVolume() const { return m_impl->volume; }

float FluxAudio::getProgress() const { return 0.f; }
float FluxAudio::getPositionSeconds() const { return 0.f; }
float FluxAudio::getDurationSeconds() const { return 0.f; }

void FluxAudio::seekToProgress(float) {}
void FluxAudio::seekToSeconds(float) {}

void FluxAudio::setOnFinished(FinishCallback cb) { m_impl->finishCallback = std::move(cb); }

bool FluxAudio::playFromPath(const std::string &) { return false; }

void FluxAudio::pause() {}
void FluxAudio::resume() {}
bool FluxAudio::isPaused() const { return false; }
bool FluxAudio::isPlaying() const { return false; }

bool FluxAudio::playPCM(const std::vector<float> &, int) { return false; }
bool FluxAudio::playStream(StreamCallback, int) { return false; }
bool FluxAudio::startPlayback() { return false; }
void FluxAudio::stopPlayback() {}
void FluxAudio::closePlayback() {}

void FluxAudio::shutdown() {}

bool FluxAudio::_playFromMemoryInternal(const uint8_t *, size_t) { return false; }

#endif // FLUX_SSR