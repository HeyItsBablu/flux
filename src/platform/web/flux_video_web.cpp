// src/flux_video_web.cpp
//
// Web video backend for FluxVideo (Emscripten / WebAssembly).
//
// Architecture
// ────────────
// Unlike every native backend (Win32/Linux: manual demux+decode via
// MediaFoundation/FFmpeg, feeding RGB24 frames + a PCM ring buffer into
// FluxAudio), the web backend does none of that. A single hidden
// HTMLVideoElement does ALL of the work — demuxing, decoding, A/V sync, and
// audio output — exactly the way a plain <video> tag would on any web page.
// FluxAudio is not used here at all; the element's own audio track plays
// through its `volume` property.
//
// This means there is no decode thread, no ring buffer, and no frame
// double-buffer: every render() call just asks the browser to draw whatever
// frame the element currently holds straight into the page's 2D canvas
// context via CanvasRenderingContext2D.drawImage(). That mirrors how
// flux_image_web.cpp blits decoded images (Canvas2D, not a WebGL texture
// upload) — see that file's header comment for why this codebase's web
// renderer is Canvas2D-based.
//
// Source loading
// ──────────────
// VideoPlayerWidget (flux_videoplayer.hpp) is platform-agnostic: for Url and
// Memory sources it always downloads/copies bytes and writes them to a real
// file on disk (MEMFS, on web) before calling FluxVideo::open(path). So by
// the time open() runs here, "path" is always an MEMFS path — either a
// preloaded asset (e.g. "screenshots/clip.mp4", mounted via --preload-file)
// or a temp file written by VP_writeTempFile(). We read those bytes out with
// FS.readFile(), wrap them in a Blob with a guessed MIME type, and point the
// <video> element at a Blob URL via URL.createObjectURL(). The previous Blob
// URL (if any) is revoked first so we don't leak memory across opens.
//
// Element placement
// ──────────────────
// The element is appended to the DOM (off-screen via fixed positioning,
// NOT display:none) rather than left detached. Some browsers — particularly
// older WebKit/Safari — throttle or stop decoding entirely for video
// elements that are display:none or never attached to the document, even
// though playback was started programmatically.
//
// JS-side state (all on Module):
//   Module._fluxVideoEl       the hidden HTMLVideoElement singleton
//   Module._fluxVideoBlobUrl  current Blob URL (revoked on close()/reopen)
//
// Thread safety
//   All JS calls run on the main thread. The C++ side is single-threaded on
//   web (no pthreads by default), so the anonymous-namespace VideoState
//   below needs no locking — std::atomic is used only so getters can be
//   called from render() without surprises if that ever changes.
//
// EM_ASM comma rule
//   One var declaration per line inside every EM_ASM block — see
//   flux_image_web.cpp's header comment for the full explanation.

#ifdef __EMSCRIPTEN__

#include "flux/flux_video.hpp"

#include <emscripten.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <string>

// ============================================================================
// Internal state — anonymous namespace, accessed only through impl().
// FluxVideo itself holds no data members (see flux_video.hpp); the
// singleton is a zero-size token object, matching every other backend.
// ============================================================================

namespace
{
    struct VideoState
    {
        std::atomic<FluxVideo::State> state{FluxVideo::State::Idle};
        std::atomic<int> videoWidth{0};
        std::atomic<int> videoHeight{0};
        std::atomic<bool> didFireFinish{false};

        FluxVideo::FinishCallback finishCallback;
        std::function<void(int w, int h)> readyCallback;
    };
} // namespace

static VideoState &impl()
{
    static VideoState s;
    return s;
}

// ============================================================================
// JS bootstrap — creates the hidden <video> element + its event listeners.
// Idempotent: re-calling after the element exists is a no-op.
// ============================================================================

static void ensureVideoElement()
{
    EM_ASM({
        if (Module._fluxVideoEl)
            return;

        var v = document.createElement('video');
        // Off-screen, but still attached + "visible enough" that browsers
        // keep decoding it (display:none can pause decode on some WebKit
        // builds even when playback was started from script).
        v.style.position = 'fixed';
        v.style.left = '-9999px';
        v.style.top = '-9999px';
        v.style.width = '1px';
        v.style.height = '1px';
        v.playsInline = true;
        v.preload = 'auto';
        document.body.appendChild(v);

        Module._fluxVideoEl = v;
        Module._fluxVideoBlobUrl = null;

        v.addEventListener('loadedmetadata', function () {
            Module._fluxVideoOnReady(v.videoWidth, v.videoHeight);
        });
        v.addEventListener('ended', function () {
            Module._fluxVideoOnEnded();
        });
        v.addEventListener('error', function () {
            console.error('[FluxVideo] element error:', v.error);
            Module._fluxVideoOnError();
        });
    });
}

// ============================================================================
// MIME-type guess from the file extension — browsers are picky and
// sometimes refuse to decode a Blob with no/incorrect MIME type even when
// the underlying codec is supported.
// ============================================================================

static std::string guessMimeType(const std::string &path)
{
    auto hasExt = [&](const char *ext) -> bool
    {
        size_t n = strlen(ext);
        return path.size() >= n &&
               path.compare(path.size() - n, n, ext) == 0;
    };

    if (hasExt(".mp4") || hasExt(".m4v"))
        return "video/mp4";
    if (hasExt(".webm"))
        return "video/webm";
    if (hasExt(".ogv") || hasExt(".ogg"))
        return "video/ogg";
    if (hasExt(".mov"))
        return "video/quicktime";
    if (hasExt(".avi"))
        return "video/x-msvideo";
    if (hasExt(".mkv"))
        return "video/x-matroska";
    if (hasExt(".flv"))
        return "video/x-flv";
    return "video/mp4"; // best-guess fallback — matches VP_detectVideoExtension's default
}

// ============================================================================
// C-exported thunks — called from JS event listeners above.
// The singleton state lives in impl(), so unlike FluxAudio's web backend
// (which passes an Impl* because it isn't necessarily a singleton-only
// design) these take no pointer argument at all — there is nothing to pass.
// ============================================================================

extern "C"
{
    EMSCRIPTEN_KEEPALIVE
    void fluxVideoOnReady(int w, int h)
    {
        auto &s = impl();
        s.videoWidth = w;
        s.videoHeight = h;
        if (s.state.load() == FluxVideo::State::Loading)
            s.state = FluxVideo::State::Paused;
        if (s.readyCallback)
            s.readyCallback(w, h);
    }

    EMSCRIPTEN_KEEPALIVE
    void fluxVideoOnEnded()
    {
        auto &s = impl();
        s.state = FluxVideo::State::Finished;
        bool expected = false;
        if (s.didFireFinish.compare_exchange_strong(expected, true))
            if (s.finishCallback)
                s.finishCallback();
    }

    EMSCRIPTEN_KEEPALIVE
    void fluxVideoOnError()
    {
        impl().state = FluxVideo::State::Error;
    }
}

// ============================================================================
// fluxVideoWebInit — wire up the JS ↔ C function pointers.
// Call this once from main_web.cpp after the WASM module is ready, the same
// way fluxAudioWebInit() is called for the audio backend.
// ============================================================================

extern "C" void fluxVideoWebInit()
{
    EM_ASM({
        Module._fluxVideoOnReady = Module.cwrap('fluxVideoOnReady', null, [ 'number', 'number' ]);
        Module._fluxVideoOnEnded = Module.cwrap('fluxVideoOnEnded', null, []);
        Module._fluxVideoOnError = Module.cwrap('fluxVideoOnError', null, []);
    });
}

// ============================================================================
// FluxVideo public API — web implementation
// ============================================================================

FluxVideo &FluxVideo::get()
{
    static FluxVideo instance;
    return instance;
}
FluxVideo::~FluxVideo() { close(); }

FluxVideo::State FluxVideo::getState() const { return impl().state.load(); }
bool FluxVideo::isPlaying() const { return impl().state.load() == State::Playing; }
bool FluxVideo::isPaused() const { return impl().state.load() == State::Paused; }
bool FluxVideo::isFinished() const { return impl().state.load() == State::Finished; }
int FluxVideo::getVideoWidth() const { return impl().videoWidth.load(); }
int FluxVideo::getVideoHeight() const { return impl().videoHeight.load(); }

void FluxVideo::setOnFinished(FinishCallback cb) { impl().finishCallback = std::move(cb); }
void FluxVideo::setOnReady(std::function<void(int, int)> cb) { impl().readyCallback = std::move(cb); }

// ── Duration / position / progress ─────────────────────────────────────────

float FluxVideo::getDurationSeconds() const
{
    return (float)EM_ASM_DOUBLE({
        var v = Module._fluxVideoEl;
        return (v && !isNaN(v.duration) && isFinite(v.duration)) ? v.duration : 0.0;
    });
}

float FluxVideo::getPositionSeconds() const
{
    return (float)EM_ASM_DOUBLE({
        var v = Module._fluxVideoEl;
        return v ? v.currentTime : 0.0;
    });
}

float FluxVideo::getProgress() const
{
    float dur = getDurationSeconds();
    if (dur <= 0.f)
        return 0.f;
    return std::min(1.f, std::max(0.f, getPositionSeconds() / dur));
}

// ── Open / close ──────────────────────────────────────────────────────────

bool FluxVideo::open(const std::string &path)
{
    close();
    ensureVideoElement();

    auto &s = impl();
    s.state = State::Loading;
    s.didFireFinish = false;

    std::string mime = guessMimeType(path);

    EM_ASM({
        var path = UTF8ToString($0);
        var mime = UTF8ToString($1);
        var v = Module._fluxVideoEl;
        if (!v)
            return;

        var bytes;
        try
        {
            bytes = FS.readFile(path);
        }
        catch (e)
        {
            console.error('[FluxVideo] FS.readFile failed for', path, e);
            Module._fluxVideoOnError();
            return;
        }

        if (Module._fluxVideoBlobUrl)
        {
            URL.revokeObjectURL(Module._fluxVideoBlobUrl);
            Module._fluxVideoBlobUrl = null;
        }

        var blob = new Blob([ bytes ], { type: mime });
        var url = URL.createObjectURL(blob);
        Module._fluxVideoBlobUrl = url;

        v.pause();
        v.src = url;
        v.load(); }, path.c_str(), mime.c_str());

    return true;
}

void FluxVideo::close()
{
    auto &s = impl();

    EM_ASM({
        var v = Module._fluxVideoEl;
        if (v)
        {
            v.pause();
            v.removeAttribute('src');
            v.load();
        }
        if (Module._fluxVideoBlobUrl)
        {
            URL.revokeObjectURL(Module._fluxVideoBlobUrl);
            Module._fluxVideoBlobUrl = null;
        }
    });

    s.state = State::Idle;
    s.videoWidth = 0;
    s.videoHeight = 0;
    s.didFireFinish = false;
}

// ── Playback control ─────────────────────────────────────────────────────

void FluxVideo::play()
{
    auto &s = impl();
    if (s.state.load() == State::Error)
        return;

    EM_ASM({
        var v = Module._fluxVideoEl;
        if (!v)
            return;
        var p = v.play();
        if (p && p.catch)
            p.catch(function (e) { console.error('[FluxVideo] play() rejected:', e); });
    });

    s.state = State::Playing;
}

void FluxVideo::pause()
{
    auto &s = impl();
    if (s.state.load() != State::Playing)
        return;

    EM_ASM({
        var v = Module._fluxVideoEl;
        if (v)
            v.pause();
    });

    s.state = State::Paused;
}

void FluxVideo::seekToSeconds(float secs)
{
    auto &s = impl();
    double dur = (double)getDurationSeconds();
    double target = std::max(0.0, dur > 0.0 ? std::min((double)secs, dur) : (double)secs);

    EM_ASM({
        var v = Module._fluxVideoEl;
        if (v)
            v.currentTime = $0;
    }, target);

    // A seek away from the very end un-finishes playback so the widget's
    // "tap to replay" UI doesn't stay stuck once the user drags the scrubber
    // back from the end.
    if (s.state.load() == State::Finished && (dur <= 0.0 || target < dur - 0.05))
    {
        s.didFireFinish = false;
        s.state = State::Paused;
    }
}

void FluxVideo::seekToProgress(float p)
{
    float dur = getDurationSeconds();
    if (dur <= 0.f)
        return;
    seekToSeconds(std::max(0.f, std::min(1.f, p)) * dur);
}

void FluxVideo::setVolume(float v)
{
    double vol = (double)std::max(0.f, std::min(1.f, v));
    EM_ASM({
        var el = Module._fluxVideoEl;
        if (el)
            el.volume = $0;
    }, vol);
}

float FluxVideo::getVolume() const
{
    return (float)EM_ASM_DOUBLE({
        var v = Module._fluxVideoEl;
        return v ? v.volume : 1.0;
    });
}

// ── Frame consumption (Canvas2D blit — see flux_video.hpp) ─────────────────

bool FluxVideo::hasNewFrame() const
{
    // No CPU-side double-buffering on web: drawImage() always blits
    // whatever frame the browser's decoder currently holds. "New" here just
    // means "ready to draw something" — readyState >= HAVE_CURRENT_DATA(2).
    return EM_ASM_INT({
        var v = Module._fluxVideoEl;
        return (v && v.readyState >= 2) ? 1 : 0;
    }) != 0;
}

void FluxVideo::renderFrame(int dstX, int dstY, int dstW, int dstH) const
{
    EM_ASM({
        var c = Module._fluxCtx2D;
        var v = Module._fluxVideoEl;
        if (!c || !v || v.readyState < 2)
            return;
        var dx = $0;
        var dy = $1;
        var dw = $2;
        var dh = $3;
        c.drawImage(v, dx, dy, dw, dh);
    }, dstX, dstY, dstW, dstH);
}

#endif // __EMSCRIPTEN__