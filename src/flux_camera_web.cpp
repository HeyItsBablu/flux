// src/flux_camera_web.cpp
//
// Camera capture backend for Emscripten / WebAssembly.
//
// Architecture
// ────────────
// Like flux_video_web.cpp, this backend does none of the manual device
// enumeration / frame-pump work the native backends do (MediaFoundation on
// Win32, V4L2 on Linux, Camera2 on Android). A single hidden
// HTMLVideoElement does the capture: getUserMedia() hands us a MediaStream,
// we point the element's srcObject at it, and the browser keeps it fed with
// decoded frames the same way it would for a plain <video> tag wired up to
// a webcam.
//
// There is no WebGL texture here, even though that's the obvious-looking
// parallel to Android's OES-texture preview path. FluxUI's web renderer is
// Canvas2D-based — the WebGL canvas (#flux-gl) is reserved for CanvasWidget
// only (see flux_painter_web.cpp / flux_window_web.cpp) — so uploading
// camera frames into a texture would mean standing up a GL pipeline nothing
// else on this path uses. Mirroring flux_video_web.cpp's existing choice,
// preview frames are blitted straight into the page's 2D canvas context via
// CanvasRenderingContext2D.drawImage(), which works directly on a
// <video> element and needs no intermediate buffer at all. The public
// surface (hasNewFrame() + renderFrame()) is intentionally identical to
// FluxVideo's web surface for the same reason.
//
// Photo capture
// ─────────────
// capturePhoto() draws the current video frame into an offscreen <canvas>,
// asks the browser to encode it as a JPEG Blob (canvas.toBlob, async), pulls
// the encoded bytes back into the WASM heap via FileReader, and writes them
// to a MEMFS path under /photos — the same MEMFS-as-"disk" convention
// flux_mic_web.cpp uses for saved recordings. getLastPhotoPath() therefore
// returns a path any later code can hand to FS.readFile() / an <img> Blob
// URL / a WIC-style decoder running in WASM, exactly like
// VideoPlayerWidget's memory-source path already does for video bytes.
//
// Mirroring
// ─────────
// The engine never mirrors anything on its own — same as every native
// backend, where mirroring the front camera is a *rendering* decision made
// by the widget (see camera_widget_win32.cpp's `if (cam.isFrontCamera())
// StretchDIBits with a negative width`). renderFrame() takes an explicit
// `mirror` flag so a future camera_widget_web.cpp can make the same call.
// The saved photo itself is never mirrored, matching how most camera apps
// behave (what gets saved should read correctly, even if the live preview
// was flipped for a more natural "selfie" feel).
//
// Flash / torch
// ─────────────
// MediaStreamTrack's `torch` constraint is real but spottily supported
// (mostly some Android Chrome builds; absent on desktop and iOS Safari).
// setFlash() applies it best-effort and silently no-ops when the active
// track doesn't advertise the capability — consistent with the "no-op on
// Linux/Win32 webcams" comment already in camera_widget.hpp.
//
// Threading
// ─────────
// Single-threaded, like every other web backend in this codebase (no
// pthreads by default) — all JS callbacks land on the main thread, so the
// handful of atomics inherited from the shared FluxCamera state exist only
// for interface parity with the native backends, not for real synchronization.
//
// EM_ASM comma rule
// ──────────────────
// As elsewhere in this codebase, every EM_ASM block below avoids top-level
// commas outside of real parentheses (e.g. never `var a = $0, b = $1;`) —
// see flux_painter_web.cpp's header comment for why the macro parser chokes
// on that. Object literals like `{ video: {...}, audio: false }` are fine
// because their commas sit inside the enclosing call's already-open `(`.

#ifdef __EMSCRIPTEN__

#include "flux/flux_camera.hpp"

#include <emscripten.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>

// ============================================================================
// Internal helpers
// ============================================================================

namespace
{
    // ── JS bootstrap — creates the hidden <video> element once ──────────────
    //
    // Off-screen via fixed positioning rather than display:none, for the
    // same reason flux_video_web.cpp avoids display:none: some WebKit builds
    // throttle or stop decoding hidden video elements even when playback was
    // started from script.

    void ensureCameraElement()
    {
        EM_ASM({
            if (Module._fluxCameraVideoEl)
                return;

            var v = document.createElement('video');
            v.style.position = 'fixed';
            v.style.left = '-9999px';
            v.style.top = '-9999px';
            v.style.width = '1px';
            v.style.height = '1px';
            v.playsInline = true;
            v.muted = true; // camera streams never carry audio we want played back
            document.body.appendChild(v);

            Module._fluxCameraVideoEl = v;
            Module._fluxCameraStream = null;
            Module._fluxCameraCaptureCanvas = null;

            v.addEventListener('loadedmetadata', function() { Module._fluxCameraOnReady(v.videoWidth, v.videoHeight); });
        });
    }

    // ── Builds "/photos/IMG_YYYYMMDD_HHMMSS_NNN.jpg" ─────────────────────────
    //
    // Mirrors flux_camera_win32.cpp's IMG_%Y%m%d_%H%M%S.jpg naming, rooted in
    // MEMFS instead of the real Pictures folder. The trailing counter guards
    // against two captures landing in the same wall-clock second, which is
    // far more plausible here than on native given how fast a JS callback
    // loop can fire.

    std::string makePhotoPath()
    {
        static int s_counter = 0;

        std::time_t t = std::time(nullptr);
        std::tm tmv{};
#if defined(_WIN32)
        localtime_s(&tmv, &t);
#else
        localtime_r(&t, &tmv);
#endif
        char name[80];
        std::snprintf(name, sizeof(name), "IMG_%04d%02d%02d_%02d%02d%02d_%03d.jpg",
                      tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
                      tmv.tm_hour, tmv.tm_min, tmv.tm_sec, (++s_counter) % 1000);
        return std::string("/photos/") + name;
    }
} // namespace

// ============================================================================
// extern "C" thunks — JS event listeners / promise callbacks call these via
// cwrap(). Camera is a true singleton, so unlike flux_mic_web.cpp there's no
// impl pointer to marshal through — every thunk just reaches FluxCamera::get().
// ============================================================================

extern "C"
{
    EMSCRIPTEN_KEEPALIVE
    void fluxCameraOnReady(int w, int h)
    {
        FluxCamera::_onStreamReady(w, h);
    }

    EMSCRIPTEN_KEEPALIVE
    void fluxCameraOnError()
    {
        FluxCamera::_onStreamError();
    }

    EMSCRIPTEN_KEEPALIVE
    void fluxCameraOnCaptureError()
    {
        FluxCamera::_onCaptureError();
    }

    EMSCRIPTEN_KEEPALIVE
    void fluxCameraOnPhotoBytes(int ptr, int len)
    {
        FluxCamera::_onPhotoBytes(
            reinterpret_cast<const uint8_t *>(static_cast<uintptr_t>(ptr)), len);
    }
}

// ============================================================================
// One-time JS ↔ C function-pointer wiring. Idempotent; safe to call from
// open() every time since most calls after the first are no-ops.
// ============================================================================

static void fluxCameraWebInit()
{
    EM_ASM({
        if (Module._fluxCameraWebInited)
            return;
        Module._fluxCameraWebInited = true;

        Module._fluxCameraOnReady = Module.cwrap('fluxCameraOnReady', null, [ 'number', 'number' ]);
        Module._fluxCameraOnError = Module.cwrap('fluxCameraOnError', null, []);
        Module._fluxCameraOnCaptureError = Module.cwrap('fluxCameraOnCaptureError', null, []);
        Module._fluxCameraOnPhotoBytes = Module.cwrap('fluxCameraOnPhotoBytes', null, [ 'number', 'number' ]);
    });
}

// ============================================================================
// FluxCamera public API — Web implementation
// ============================================================================

FluxCamera &FluxCamera::get()
{
    static FluxCamera instance;
    return instance;
}

bool FluxCamera::isFrontCamera() const { return _useFront.load(); }

// ── open ──────────────────────────────────────────────────────────────────
//
// facingMode is passed as a bare string (an "ideal" constraint per spec),
// not {exact: ...}, so a desktop browser with only one camera still
// resolves successfully instead of rejecting the whole request.

bool FluxCamera::open(bool useFront)
{
    if (_state != State::Closed)
        close();

    fluxCameraWebInit();
    ensureCameraElement();

    _state = State::Opening;
    _useFront = useFront;

    int wantFront = useFront ? 1 : 0;
    EM_ASM({
        var wantFront = $0;

        if (!navigator.mediaDevices || !navigator.mediaDevices.getUserMedia) {
            console.error('[FluxCamera] getUserMedia unsupported in this browser');
            Module._fluxCameraOnError();
            return;
        }

        navigator.mediaDevices.getUserMedia({
            video: { facingMode: wantFront ? 'user' : 'environment' },
            audio: false
        }).then(function (stream) {
            var v = Module._fluxCameraVideoEl;
            Module._fluxCameraStream = stream;
            v.srcObject = stream;
            var p = v.play();
            if (p && p.catch)
                p.catch(function (e) { console.error('[FluxCamera] play() rejected:', e); });
        }).catch(function (err) {
            console.error('[FluxCamera] getUserMedia failed:', err);
            Module._fluxCameraOnError();
        }); }, wantFront);

    return true;
}

// ── flipCamera ────────────────────────────────────────────────────────────
//
// No real device enumeration on web (see header comment) — flipping just
// toggles which facingMode we ask for and lets the browser pick a matching
// device, the same graceful-degradation behaviour open() relies on.

void FluxCamera::flipCamera()
{
    bool next = !_useFront.load();
    close();
    open(next);
}

// ── updateFrame / hasNewFrame ────────────────────────────────────────────
//
// There's no decode thread setting a pending-frame flag the way Win32's
// _readerLoop() does, so both of these just ask the element directly
// whether it currently has decoded data ready (readyState >=
// HAVE_CURRENT_DATA). updateFrame() also mirrors the result into the shared
// _newFrame atomic so any caller using the native pull idiom still works.

bool FluxCamera::updateFrame()
{
    bool ready = EM_ASM_INT({
                     var v = Module._fluxCameraVideoEl;
                     return (v && v.readyState >= 2) ? 1 : 0;
                 }) != 0;
    _newFrame = ready;
    return ready;
}

bool FluxCamera::hasNewFrame() const
{
    return EM_ASM_INT({
               var v = Module._fluxCameraVideoEl;
               return (v && v.readyState >= 2) ? 1 : 0;
           }) != 0;
}

// ── renderFrame ───────────────────────────────────────────────────────────

void FluxCamera::renderFrame(int dstX, int dstY, int dstW, int dstH,
                             bool mirror) const
{
    int mir = mirror ? 1 : 0;
    EM_ASM({
        var c = Module._fluxCtx2D;
        var v = Module._fluxCameraVideoEl;
        if (!c || !v || v.readyState < 2)
            return;
        if ($4) {
            c.save();
            c.translate($0 + $2, $1);
            c.scale(-1, 1);
            c.drawImage(v, 0, 0, $2, $3);
            c.restore();
        } else {
            c.drawImage(v, $0, $1, $2, $3);
        } }, dstX, dstY, dstW, dstH, mir);
}

// ── capturePhoto ─────────────────────────────────────────────────────────
//
// Draws the current frame into a reusable offscreen canvas, then hands off
// to canvas.toBlob() for JPEG encoding. The blob comes back asynchronously,
// so _state stays Capturing until fluxCameraOnPhotoBytes (success) or
// fluxCameraOnCaptureError (failure) fires and flips it back.

void FluxCamera::capturePhoto()
{
    if (_state != State::Previewing)
        return;
    _state = State::Capturing;

    EM_ASM({
        var v = Module._fluxCameraVideoEl;
        var w = v ? v.videoWidth : 0;
        var h = v ? v.videoHeight : 0;
        if (!v || !w || !h)
        {
            Module._fluxCameraOnCaptureError();
            return;
        }

        var canvas = Module._fluxCameraCaptureCanvas;
        if (!canvas)
        {
            canvas = document.createElement('canvas');
            Module._fluxCameraCaptureCanvas = canvas;
        }
        canvas.width = w;
        canvas.height = h;

        var cctx = canvas.getContext('2d');
        cctx.drawImage(v, 0, 0, w, h); // saved photo is never mirrored

        canvas.toBlob(function(blob) {
            if (!blob) {
                Module._fluxCameraOnCaptureError();
                return;
            }
            var reader = new FileReader();
            reader.onload = function () {
                var bytes = new Uint8Array(reader.result);
                var ptr = Module._malloc(bytes.length);
                Module.HEAPU8.set(bytes, ptr);
                Module._fluxCameraOnPhotoBytes(ptr, bytes.length);
                Module._free(ptr);
            };
            reader.onerror = function () {
                Module._fluxCameraOnCaptureError();
            };
            reader.readAsArrayBuffer(blob); }, 'image/jpeg', 0.92);
    });
}

// ── close ─────────────────────────────────────────────────────────────────

void FluxCamera::close()
{
    _state = State::Closed;

    EM_ASM({
        var v = Module._fluxCameraVideoEl;
        if (v)
        {
            v.pause();
            v.srcObject = null;
        }
        var stream = Module._fluxCameraStream;
        if (stream)
        {
            stream.getTracks().forEach(function(t) { t.stop(); });
            Module._fluxCameraStream = null;
        }
    });

    _previewW = 0;
    _previewH = 0;
    _newFrame = false;
    _pendingFrame = false;
    _captureRequested = false;
}

// ── Flash / torch ─────────────────────────────────────────────────────────

void FluxCamera::setFlash(bool on)
{
    _flashOn = on;
    _applyFlash();
}

void FluxCamera::_applyFlash()
{
    int on = _flashOn.load() ? 1 : 0;
    EM_ASM({
        var stream = Module._fluxCameraStream;
        if (!stream)
            return;
        var tracks = stream.getVideoTracks();
        if (!tracks.length)
            return;
        var track = tracks[0];
        var caps = track.getCapabilities ? track.getCapabilities() : {};
        if (!caps.torch)
            return; // unsupported on most desktop browsers and iOS Safari
        track.applyConstraints({ advanced: [ { torch: !!$0 } ] })
            .catch(function (e) { console.warn('[FluxCamera] torch constraint rejected:', e); }); }, on);
}

// ============================================================================
// Static callbacks — invoked only from the extern "C" thunks above.
// ============================================================================

void FluxCamera::_onStreamReady(int w, int h)
{
    auto &cam = get();
    cam._previewW = w;
    cam._previewH = h;
    if (cam._state.load() == State::Opening)
        cam._state = State::Previewing;
}

void FluxCamera::_onStreamError()
{
    get()._state = State::Error;
}

void FluxCamera::_onCaptureError()
{
    auto &cam = get();
    if (cam._state.load() == State::Capturing)
        cam._state = State::Previewing;
}

void FluxCamera::_onPhotoBytes(const uint8_t *data, int len)
{
    auto &cam = get();
    cam._state = State::Previewing;

    if (!data || len <= 0)
        return;

    EM_ASM({ try { FS.mkdir('/photos'); } catch (e) {} });

    std::string path = makePhotoPath();
    FILE *f = fopen(path.c_str(), "wb");
    if (!f)
        return;
    fwrite(data, 1, (size_t)len, f);
    fclose(f);

    cam._lastPhotoPath = path;
    if (cam._onPhoto)
        cam._onPhoto(path);
}

#endif // __EMSCRIPTEN__