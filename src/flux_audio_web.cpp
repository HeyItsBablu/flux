// src/flux_audio_web.cpp
//
// Web Audio API backend for FluxAudio (Emscripten / WebAssembly).
//
// Architecture
// ────────────
// One AudioContext lives for the page lifetime (Module._fluxAC).  It is
// created lazily on the first play call — browsers block AudioContext
// construction until a user gesture, and FluxAudio::get() is called at
// static-init time before any gesture has occurred.
//
// Playback model
//   playFromPath(path)  -> read bytes from Emscripten MEMFS
//                       -> decodeAudioData (async, JS Promise)
//                       -> on success: store AudioBuffer, start source node
//
//   playPCM(samples, sr) -> build an AudioBuffer from the float32 vector,
//                          then start a source node immediately (synchronous)
//
//   playStream(cb, sr)   -> pull-mode: a ScriptProcessorNode (deprecated but
//                          universally supported) calls the C++ StreamCallback
//                          every onaudioprocess event.  AudioWorklet would be
//                          cleaner but requires a separate .js file which is
//                          awkward to bundle with Emscripten.
//
// Seek / pause / resume
//   Web Audio has no seek on a running node.  We track the wall-clock start
//   time and a "pause offset" so resume restarts the node at the right
//   position.  Seeking stops the current node and restarts from the target.
//
// Progress polling
//   getProgress() / getPositionSeconds() compute the playhead from
//   AudioContext.currentTime – startTime + pauseOffset.  No background
//   thread needed.
//
// JS-side state (all on Module):
//   Module._fluxAC          AudioContext singleton
//   Module._fluxAudioBuf    decoded AudioBuffer (last loaded sound)
//   Module._fluxAudioSrc    running AudioBufferSourceNode (nullable)
//   Module._fluxAudioSP     ScriptProcessorNode for stream mode (nullable)
//   Module._fluxAudioStart  AudioContext.currentTime when playback began
//   Module._fluxAudioOffset pause offset in seconds (accumulated paused time)
//   Module._fluxAudioDur    duration of current buffer in seconds
//   Module._fluxAudioDone   bool — true once the source fires onended
//   Module._fluxVolNode     GainNode for volume control
//
// Thread safety
//   All JS calls execute on the main thread.  The C++ side is single-
//   threaded on web (no pthreads by default), so no locking is needed.

#ifdef __EMSCRIPTEN__

#include "flux/flux_audio.hpp"

#include <emscripten.h>
#include <emscripten/fetch.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <string>
#include <vector>

// ============================================================================
// JS bootstrap — called once to set up the AudioContext and helper state.
// Idempotent: re-calling after the context exists is a no-op.
// ============================================================================

static void ensureAudioContext()
{
    EM_ASM({
        if (Module._fluxAC)
            return;

        var AC = window.AudioContext || window.webkitAudioContext;
        if (!AC)
        {
            console.error('[FluxAudio] Web Audio API not available');
            return;
        }
        Module._fluxAC = new AC();
        Module._fluxAudioBuf = null;
        Module._fluxAudioSrc = null;
        Module._fluxAudioSP = null;
        Module._fluxAudioStart = 0.0;
        Module._fluxAudioOffset = 0.0;
        Module._fluxAudioDur = 0.0;
        Module._fluxAudioDone = false;

        // GainNode sits between all sources and the destination.
        Module._fluxVolNode = Module._fluxAC.createGain();
        Module._fluxVolNode.connect(Module._fluxAC.destination);
    });
}

// ============================================================================
// FluxAudio::Impl
// ============================================================================

struct FluxAudio::Impl
{
    // ── State mirrored on the C++ side ───────────────────────────────────────
    std::atomic<bool> playing{false};
    std::atomic<bool> paused{false};
    std::atomic<bool> finished{false};

    float volume = 1.0f;
    int sampleRate = 44100; // used for PCM / stream modes

    FinishCallback finishCallback;
    StreamCallback streamCallback;

    // ── Stop / disconnect any running source node ─────────────────────────────
    void stopSource()
    {
        EM_ASM({
            var src = Module._fluxAudioSrc;
            if (src)
            {
                try
                {
                    src.stop();
                }
                catch (e)
                {
                }
                src.onended = null;
                src.disconnect();
                Module._fluxAudioSrc = null;
            }
            var sp = Module._fluxAudioSP;
            if (sp)
            {
                sp.onaudioprocess = null;
                sp.disconnect();
                Module._fluxAudioSP = null;
            }
            Module._fluxAudioStart = 0.0;
            Module._fluxAudioOffset = 0.0;
            Module._fluxAudioDone = false;
        });
        playing = false;
        paused = false;
        finished = false;
    }

    // ── Start an AudioBufferSourceNode from Module._fluxAudioBuf ─────────────
    // offsetSeconds: where in the buffer to begin (for seek / resume).
    // Stores a C++ function pointer as an int so the onended callback can
    // call back into C++ without capturing a lambda (EM_ASM can't capture).
    void startBufferSource(double offsetSeconds, FluxAudio::Impl *self)
    {
        // Store self pointer as a JS number so onended can call back.
        // We use a Module-level slot rather than a closure to stay compatible
        // with EM_ASM (no lambda capture).
        EM_ASM({
            var ac  = Module._fluxAC;
            var buf = Module._fluxAudioBuf;
            if (!ac || !buf) return;

            var src = ac.createBufferSource();
            src.buffer = buf;
            src.connect(Module._fluxVolNode);

            Module._fluxAudioSrc    = src;
            Module._fluxAudioStart  = ac.currentTime - $0;
            Module._fluxAudioOffset = 0.0;
            Module._fluxAudioDone   = false;

            // Store the C++ Impl* so onended can invoke the finish callback.
            Module._fluxAudioImplPtr = $1;

            src.onended = function() {
                // Guard against stop()-triggered onended firing after we
                // already cleared the source.
                if (Module._fluxAudioSrc !== src) return;
                Module._fluxAudioSrc  = null;
                Module._fluxAudioDone = true;
                // Call back into C++ via a ccall to the exported thunk.
                if (Module._fluxAudioImplPtr) {
                    Module._fluxOnAudioEnded(Module._fluxAudioImplPtr);
                }
            };

            src.start(0, $0); }, offsetSeconds, reinterpret_cast<int>(self));

        playing = true;
        paused = false;
        finished = false;
    }

    // ── Build a JS AudioBuffer from a C++ float32 vector ─────────────────────
    // samples: mono interleaved float32, length = count, sample rate = sr.
    static void buildJSAudioBuffer(const float *samples, int count, int sr)
    {
        EM_ASM({
            var ac  = Module._fluxAC;
            if (!ac) return;
            var count = $1;
            var sr    = $2;
            var buf   = ac.createBuffer(1, count, sr);
            var ch    = buf.getChannelData(0);
            var src   = new Float32Array(Module.HEAPF32.buffer, $0, count);
            ch.set(src);
            Module._fluxAudioBuf = buf;
            Module._fluxAudioDur = count / sr; }, samples, count, sr);
    }
};

// ============================================================================
// C-exported thunk — called from JS onended handler.
// Must be exported so JS can invoke it after the WASM module is compiled.
// ============================================================================

extern "C"
{
    EMSCRIPTEN_KEEPALIVE
    void fluxOnAudioEnded(FluxAudio::Impl *impl)
    {
        if (!impl)
            return;
        impl->playing = false;
        impl->finished = true;
        // Store _fluxOnAudioEnded as a Module slot once at startup.
        if (impl->finishCallback)
            impl->finishCallback();
    }
}



// ============================================================================
// FluxAudio public API — web implementation
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

// ── Volume ────────────────────────────────────────────────────────────────────

void FluxAudio::setVolume(float v)
{
    m_impl->volume = std::max(0.f, std::min(1.f, v));
    float vol = m_impl->volume;
    EM_ASM({
        if (Module._fluxVolNode)
            Module._fluxVolNode.gain.setTargetAtTime($0,
                Module._fluxAC ? Module._fluxAC.currentTime : 0, 0.01); }, (double)vol);
}

float FluxAudio::getVolume() const { return m_impl->volume; }

// ── Progress ──────────────────────────────────────────────────────────────────

float FluxAudio::getProgress() const
{
    float dur = (float)EM_ASM_DOUBLE({ return Module._fluxAudioDur || 0.0; });
    if (dur <= 0.f)
        return 0.f;
    float pos = getPositionSeconds();
    return std::min(1.f, std::max(0.f, pos / dur));
}

float FluxAudio::getPositionSeconds() const
{
    if (m_impl->paused.load())
    {
        // While paused, position = offset accumulated at pause time.
        return (float)EM_ASM_DOUBLE({ return Module._fluxAudioOffset || 0.0; });
    }
    if (!m_impl->playing.load())
    {
        return m_impl->finished.load()
                   ? (float)EM_ASM_DOUBLE({ return Module._fluxAudioDur || 0.0; })
                   : 0.f;
    }
    // Playing: currentTime – startTime gives elapsed seconds since (re)start.
    double pos = EM_ASM_DOUBLE({
        var ac = Module._fluxAC;
        if (!ac)
            return 0.0;
        return ac.currentTime - (Module._fluxAudioStart || 0.0);
    });
    float dur = (float)EM_ASM_DOUBLE({ return Module._fluxAudioDur || 0.0; });
    return std::min((float)pos, dur);
}

float FluxAudio::getDurationSeconds() const
{
    return (float)EM_ASM_DOUBLE({ return Module._fluxAudioDur || 0.0; });
}

// ── Seek ──────────────────────────────────────────────────────────────────────

void FluxAudio::seekToProgress(float progress)
{
    float dur = getDurationSeconds();
    if (dur <= 0.f)
        return;
    seekToSeconds(std::max(0.f, std::min(1.f, progress)) * dur);
}

void FluxAudio::seekToSeconds(float seconds)
{
    float dur = getDurationSeconds();
    float target = std::max(0.f, std::min(dur > 0.f ? dur : seconds, seconds));
    bool wasPlay = m_impl->playing.load();
    bool wasPause = m_impl->paused.load();

    // Stop current source, restart from target position.
    m_impl->stopSource();

    // Update the JS offset so getPositionSeconds() is correct before startBufferSource.
    EM_ASM({ Module._fluxAudioOffset = $0; }, (double)target);

    if (wasPlay || wasPause)
    {
        m_impl->startBufferSource((double)target, m_impl);
        if (wasPause)
        {
            // Re-pause immediately: suspend the context.
            EM_ASM({
                if (Module._fluxAC && Module._fluxAC.state == = 'running')
                    Module._fluxAC.suspend();
            });
            m_impl->playing = false;
            m_impl->paused = true;
        }
    }
}

// ── Callbacks ─────────────────────────────────────────────────────────────────

void FluxAudio::setOnFinished(FinishCallback cb)
{
    m_impl->finishCallback = std::move(cb);
}

// ── playFromPath ──────────────────────────────────────────────────────────────
//
// On web, "path" is a path inside Emscripten's in-memory virtual filesystem
// (MEMFS).  AudioPlayerWidget writes bytes there via _writeTempFile (which
// uses mkstemp on MEMFS) before calling playFromPath.
//
// We read the bytes back out, hand them to decodeAudioData (async), and start
// playback in the success callback.  The C++ side sets playing=true immediately
// so the UI timer keeps ticking and the progress bar updates once decoding is
// done (usually < 100 ms for a short clip).
//
// If the file doesn't exist in MEMFS (e.g. a URL string was passed directly),
// we fall through to a JS fetch so streaming URLs work too.

bool FluxAudio::playFromPath(const std::string &path)
{
    closePlayback();
    ensureAudioContext();

    // Try to read from MEMFS first.
    FILE *f = fopen(path.c_str(), "rb");
    if (f)
    {
        fseek(f, 0, SEEK_END);
        long len = ftell(f);
        rewind(f);
        if (len > 0)
        {
            std::vector<uint8_t> bytes((size_t)len);
            fread(bytes.data(), 1, (size_t)len, f);
            fclose(f);
            return _playFromMemoryInternal(bytes.data(), bytes.size());
        }
        fclose(f);
    }

    // Fall back: treat path as a URL and fetch it.
    m_impl->playing = true; // optimistic — we'll clear on error
    m_impl->finished = false;

    std::string url = path; // copy for lambda capture via EM_ASM workaround
    Impl *impl = m_impl;

    // Pass url and impl pointer to JS via EM_ASM args.
    EM_ASM({
        var ac  = Module._fluxAC;
        if (!ac) return;
        var url = UTF8ToString($0);
        var ptr = $1;
        fetch(url)
            .then(function(r) { return r.arrayBuffer(); })
            .then(function(ab) {
                return ac.decodeAudioData(ab);
            })
            .then(function(buf) {
                Module._fluxAudioBuf = buf;
                Module._fluxAudioDur = buf.duration;
                // Start via a small C function call so we stay on the right Impl.
                Module._fluxStartBufferSource(ptr, 0.0);
            })
            .catch(function(e) {
                console.error('[FluxAudio] fetch/decode failed:', e);
                Module._fluxOnLoadError(ptr);
            }); }, url.c_str(), reinterpret_cast<int>(impl));

    return true; // async — caller must wait for playing state to stabilise
}

// ── Internal: play from raw bytes already in C++ memory ───────────────────────

bool FluxAudio::_playFromMemoryInternal(const uint8_t *data, size_t len)
{
    ensureAudioContext();

    Impl *impl = m_impl;
    impl->playing = true;
    impl->finished = false;

    // Hand the bytes to JS for decodeAudioData (async Promise).
    EM_ASM({
        var ac  = Module._fluxAC;
        if (!ac) return;
        // Copy into a JS ArrayBuffer — we can't keep a HEAP pointer alive
        // across the async decode.
        var heap   = new Uint8Array(Module.HEAPU8.buffer, $0, $1);
        var ab     = heap.buffer.slice($0, $0 + $1);
        var ptr    = $2;

        ac.decodeAudioData(ab,
            function(buf) {
                Module._fluxAudioBuf = buf;
                Module._fluxAudioDur = buf.duration;
                Module._fluxStartBufferSource(ptr, 0.0);
            },
            function(e) {
                console.error('[FluxAudio] decodeAudioData failed:', e);
                Module._fluxOnLoadError(ptr);
            }
        ); }, data, (int)len, reinterpret_cast<int>(impl));

    return true;
}

// ── playPCM ───────────────────────────────────────────────────────────────────

bool FluxAudio::playPCM(const std::vector<float> &samples, int sampleRate)
{
    closePlayback();
    ensureAudioContext();

    if (samples.empty())
        return false;

    m_impl->sampleRate = sampleRate;
    Impl::buildJSAudioBuffer(samples.data(), (int)samples.size(), sampleRate);
    m_impl->startBufferSource(0.0, m_impl);
    return true;
}

// ── playStream ────────────────────────────────────────────────────────────────
//
// Uses a ScriptProcessorNode (bufferSize=2048) to pull audio from the C++
// StreamCallback on every onaudioprocess event.
//
// Limitation: ScriptProcessorNode is deprecated in favour of AudioWorklet,
// but AudioWorklet requires a separate .js file.  For the streaming use case
// (FluxVideo), the latency (~46 ms) is acceptable.

bool FluxAudio::playStream(StreamCallback cb, int sampleRate)
{
    closePlayback();
    ensureAudioContext();

    m_impl->streamCallback = std::move(cb);
    m_impl->sampleRate = sampleRate;
    m_impl->playing = true;
    m_impl->finished = false;

    Impl *impl = m_impl;

    EM_ASM({
        var ac = Module._fluxAC;
        if (!ac)
            return;
        var ptr = $0;
        var bufSz = 2048;

        var sp = ac.createScriptProcessor(bufSz, 0, 1);
        Module._fluxAudioSP = sp;

        // Allocate a persistent HEAP buffer for the StreamCallback output.
        var heapBuf = Module._malloc(bufSz * 4); // float32
        Module._fluxStreamHeapBuf = heapBuf;

        sp.onaudioprocess = function(e)
        {
            // Call the C++ StreamCallback: fills heapBuf with float32 samples.
            var got = Module._fluxStreamFill(ptr, heapBuf, bufSz);
            var out = e.outputBuffer.getChannelData(0);
            if (got > 0)
            {
                var src = new Float32Array(Module.HEAPF32.buffer,
                                           heapBuf, bufSz);
                out.set(src);
            }
            else
            {
                out.fill(0);
                // StreamCallback returned 0 -> stream ended.
                sp.onaudioprocess = null;
                sp.disconnect();
                Module._fluxAudioSP = null;
                Module._free(heapBuf);
                Module._fluxStreamHeapBuf = null;
                Module._fluxOnAudioEnded(ptr);
            }
        };

        sp.connect(Module._fluxVolNode);
        Module._fluxAudioStart = ac.currentTime;
        Module._fluxAudioOffset = 0.0;
        Module._fluxAudioDur = 0.0; // unbounded stream
    },
           reinterpret_cast<int>(impl));

    return true;
}

// ── pause / resume ────────────────────────────────────────────────────────────
//
// We suspend/resume the AudioContext itself.  This is simpler than stopping
// and restarting the source node and works correctly for both buffer and
// stream modes.  The downside is that it pauses ALL audio on the page, which
// is fine for a single-player app.

void FluxAudio::pause()
{
    if (!m_impl->playing.load())
        return;

    // Record elapsed time before suspend so we can resume from the right spot.
    EM_ASM({
        var ac = Module._fluxAC;
        if (!ac)
            return;
        Module._fluxAudioOffset =
            ac.currentTime - (Module._fluxAudioStart || 0.0);
        ac.suspend();
    });

    m_impl->playing = false;
    m_impl->paused = true;
}

void FluxAudio::resume()
{
    if (!m_impl->paused.load())
        return;

    EM_ASM({
        var ac = Module._fluxAC;
        if (!ac)
            return;
        ac.resume().then(function() {
            // Update startTime so elapsed = currentTime – startTime is correct.
            Module._fluxAudioStart =
                ac.currentTime - (Module._fluxAudioOffset || 0.0);
        });
    });

    m_impl->paused = false;
    m_impl->playing = true;
}

bool FluxAudio::isPaused() const { return m_impl->paused.load(); }
bool FluxAudio::isPlaying() const { return m_impl->playing.load(); }

// ── stopPlayback / closePlayback / shutdown ───────────────────────────────────

bool FluxAudio::startPlayback() { return true; } // no-op on web

void FluxAudio::stopPlayback()
{
    m_impl->stopSource();
}

void FluxAudio::closePlayback()
{
    m_impl->streamCallback = nullptr;
    m_impl->stopSource();
    // Clear JS audio buffer so stale audio doesn't replay.
    EM_ASM({
        Module._fluxAudioBuf = null;
        Module._fluxAudioDur = 0.0;
        if (Module._fluxStreamHeapBuf)
        {
            Module._free(Module._fluxStreamHeapBuf);
            Module._fluxStreamHeapBuf = null;
        }
    });
}

void FluxAudio::shutdown()
{
    closePlayback();
    EM_ASM({
        if (Module._fluxAC)
        {
            Module._fluxAC.close();
            Module._fluxAC = null;
        }
    });
}



// ============================================================================
// C-exported helpers called from JS
//
// _fluxStartBufferSource(implPtr, offsetSecs)
//   Called from the decodeAudioData success callback so async decodes can
//   kick off playback without going through ccall overhead every frame.
//
// _fluxStreamFill(implPtr, heapBufPtr, frameCount)
//   Called from ScriptProcessorNode.onaudioprocess to pull samples from the
//   C++ StreamCallback.
//
// _fluxOnLoadError(implPtr)
//   Called when fetch or decodeAudioData rejects.
// ============================================================================

extern "C"
{

    EMSCRIPTEN_KEEPALIVE
    void fluxStartBufferSource(FluxAudio::Impl *impl, double offsetSecs)
    {
        if (!impl)
            return;
        impl->startBufferSource(offsetSecs, impl);
    }

    EMSCRIPTEN_KEEPALIVE
    int fluxStreamFill(FluxAudio::Impl *impl, float *buf, int frames)
    {
        if (!impl || !impl->streamCallback)
            return 0;
        return impl->streamCallback(buf, frames);
    }

    EMSCRIPTEN_KEEPALIVE
    void fluxOnLoadError(FluxAudio::Impl *impl)
    {
        if (!impl)
            return;
        impl->playing = false;
        impl->finished = true;
        // Optionally fire the finish callback so the UI updates.
        if (impl->finishCallback)
            impl->finishCallback();
    }

} // extern "C"

// ============================================================================
// fluxAudioWebInit  — wire up the JS ↔ C function pointers.
// Call this once from main_web.cpp after the WASM module is ready.
// ============================================================================

extern "C" void fluxAudioWebInit()
{
    EM_ASM({
        Module._fluxOnAudioEnded = Module.cwrap('fluxOnAudioEnded', null, ['number']);
        Module._fluxStartBufferSource = Module.cwrap('fluxStartBufferSource', null, [ 'number', 'number' ]);
        Module._fluxStreamFill = Module.cwrap('fluxStreamFill', 'number', [ 'number', 'number', 'number' ]);
        Module._fluxOnLoadError = Module.cwrap('fluxOnLoadError', null, ['number']);
    });
}

#endif // __EMSCRIPTEN__