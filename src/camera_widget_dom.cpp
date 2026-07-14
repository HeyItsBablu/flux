// camera_widget_dom.cpp
//
// DOM-renderer / SSR platform implementation for CameraWidget. Compiled
// for __EMSCRIPTEN__ and FLUX_SSR both — same pairing VideoPlayerWidget's
// render() already uses (see its `#elif defined(__EMSCRIPTEN__) ||
// defined(FLUX_SSR)` branch), for the same reason: SSR paints exactly
// the placeholder + control-bar UI the client shows pre-permission, and
// neither backend has a CPU pixel buffer or GL texture to blit into —
// everything real goes through IDomAdapter or doesn't happen at all.
//
// REPLACES camera_widget_web.cpp, which targeted the older Canvas2D
// renderer (Module._fluxCtx2D) via Painter::drawCamera — a permanent
// no-op on the DOM renderer (see flux_painter_dom.cpp). Remove
// camera_widget_web.cpp from the web build's source list; this file
// takes over both roles, mirroring VideoPlayerWidget's video/SSR split
// (there is no separate "web" video-widget file either — only
// flux_video_web.cpp, the ENGINE, survives as a distinct file).
//
// Also replaces the standalone camera_widget_ssr.cpp stub from the
// previous pass — folded in here instead of kept as a second file, same
// consolidation reasoning.
//
// Preview
// ───────
// flux_camera_web.cpp already maintains a hidden <video> element
// (Module._fluxCameraVideoEl) wired to the live getUserMedia()
// MediaStream, used purely as an engine-side capture surface
// (hasNewFrame/renderFrame/capturePhoto) — deliberately NOT positioned
// inside FluxUI's layout tree.
//
// This file creates a SEPARATE, widget-owned <video> node via the usual
// fluxDomEnsureNode()/fluxDomApplyRect() path every other DOM-backed
// widget follows, and attaches the SAME MediaStream to it via
// IDomAdapter::setCameraPreviewSource(). Costs nothing extra from the
// browser's decode pipeline — a MediaStream is a shared source, not
// something that gets re-decoded per consumer.
//
// On FLUX_SSR, setCameraPreviewSource() is never even called (see the
// #else branch below) — the node still gets created so hydration ids
// line up between the SSR pass and the client's first DOM pass, but it
// never gets a stream, so it stays the plain dark background color
// until the client's own open() call attaches the real thing
// post-hydration.
//
// Mirroring
// ─────────
// CSS `transform: scaleX(-1)` on the preview node when
// cam.isFrontCamera() — same "rendering decision, not an engine
// concern" split every other backend uses (see flux_camera_web.cpp's
// header comment on renderFrame's `mirror` flag). The captured photo
// itself is never mirrored (see flux_camera_web.cpp's capturePhoto()) —
// unaffected by this, since it never goes through this node at all.
//
// Thumbnail
// ─────────
// A second widget-owned node (a plain <img>) pointed at a Blob URL via
// IDomAdapter::setImageSourceFromFile(), reading the same MEMFS JPEG
// path flux_camera_web.cpp's capturePhoto()/_onPhotoBytes() pipeline
// already writes. No off-screen element + manual canvas drawImage()
// indirection needed, unlike the old Canvas2D implementation.
//
// Flash overlay
// ─────────────
// Plain Painter::fillRect() with an alpha color — the DOM backend's
// cssColor() already encodes Color::a as rgba(...), so this needs no
// adapter-specific code at all, unlike the preview/thumbnail nodes.

#if defined(__EMSCRIPTEN__) || defined(FLUX_SSR)

#include "flux/widgets/camera_widget.hpp"
#include "flux/flux_dom_adapter.hpp"

// ── _platformScheduleOpen ────────────────────────────────────────────
void CameraWidget::_platformScheduleOpen()
{
#if defined(__EMSCRIPTEN__)
    // getUserMedia() handles permission gating itself — no pre-flight
    // timer needed, unlike Android. Next render() call opens the camera.
    _shouldOpen = true;
#else
    // FLUX_SSR — never actually open. No camera hardware server-side,
    // and no permission dialog makes sense outside a real browser tab.
    _shouldOpen = false;
#endif
}

// ── _platformOnFlip ──────────────────────────────────────────────────
void CameraWidget::_platformOnFlip()
{
#if defined(__EMSCRIPTEN__)
    // FluxCamera::flipCamera() (called right after this by
    // handleMouseDown) tears down and reopens the stream, producing a
    // NEW MediaStream object. Force _platformRenderPreview's next call
    // to re-attach it even though setCameraPreviewSource() also
    // self-guards — without this the widget would otherwise wait for
    // some OTHER code path to flip _domPreviewSrcApplied back off.
    _domPreviewSrcApplied = false;
#endif
}

// ── _platformRenderPreview ───────────────────────────────────────────

bool CameraWidget::_platformRenderPreview(GraphicsContext &, Painter &,
                                          FontCache &, int viewH)
{
    IDomAdapter *adapter = getActiveDomAdapter();
    if (!adapter)
        return false;

    // Unconditional on BOTH SSR and client — this exact call, this exact
    // order, every single render(), is what keeps hydration ids in
    // lockstep. SSR and the client's first pass MUST create the same
    // node here even though SSR never attaches a real stream to it —
    // skipping it server-side (as a previous version of this file did)
    // desyncs every id created afterward, not just this one.
    DomNodeHandle node = fluxDomEnsureNode(this, "video", "previewEl");
    fluxDomApplyRect(this, x, y, width, viewH, "previewEl");

    adapter->setStyle(node, "object-fit", "cover");
    adapter->setStyle(node, "background-color", "#141414");
    adapter->setStyle(node, "pointer-events", "none");

#if defined(__EMSCRIPTEN__)
    adapter->setBoolProperty(node, "muted", true);
    adapter->setBoolProperty(node, "autoplay", true);

    auto &cam = FluxCamera::get();
    adapter->setStyle(node, "transform",
                      cam.isFrontCamera() ? "scaleX(-1)" : "none");

    // Only latch "applied" once the stream actually exists — open() is
    // async, so the first render() after open() almost always runs
    // before Module._fluxCameraStream is populated. Gating on
    // cam.isPreviewing() means we keep retrying every ~33ms (the
    // _startTimer() cadence) until the stream is genuinely ready,
    // instead of giving up permanently on one premature attempt.
    if (!_domPreviewSrcApplied && cam.isPreviewing())
    {
        adapter->setCameraPreviewSource(node);
        _domPreviewSrcApplied = true;
    }

    if (cam.isPreviewing() || cam.isCapturing())
    {
        // Hide any leftover "Opening camera..." placeholder nodes from
        // earlier frames — once we start returning true here, render()
        // stops touching them, so they'd otherwise stay visible at their
        // last-painted size forever, sitting on top of the real feed.
        DomNodeHandle bg = fluxDomEnsureNode(this, "div", "previewBg");
        adapter->setStyle(bg, "display", "none");
        DomNodeHandle txt = fluxDomEnsureNode(this, "div", "previewText");
        adapter->setStyle(txt, "display", "none");
    }

    return cam.isPreviewing() || cam.isCapturing();
#else
    // FLUX_SSR — node created (ids stay in sync), but never attached to
    // a stream (setCameraPreviewSource is never called here). Always
    // false so render() still draws "Opening camera..." over it.
    return false;
#endif
}

// ── _platformRenderFlash ─────────────────────────────────────────────
void CameraWidget::_platformRenderFlash(GraphicsContext &, Painter &p, int viewH)
{
    int alpha = (int)(_flashAlpha * 255.f);
    if (alpha <= 0)
        return;
    p.fillRect(x, y, width, viewH, colFlash.withAlpha((uint8_t)alpha), "flashOverlay");
}

// ── _platformRenderThumb ─────────────────────────────────────────────
bool CameraWidget::_platformRenderThumb(GraphicsContext &, int thumbX, int thumbY,
                                        int thumbW, int thumbH)
{
    IDomAdapter *adapter = getActiveDomAdapter();
    if (!adapter)
        return false;

    DomNodeHandle node = fluxDomEnsureNode(this, "img", "thumbImg");
    fluxDomApplyRect(this, thumbX, thumbY, thumbW, thumbH, "thumbImg");
    adapter->setStyle(node, "object-fit", "cover");

#if defined(__EMSCRIPTEN__)
    return true;
#else
    return false; // SSR: node exists for id parity, never gets a src
#endif
}

// ── _platformLoadThumb ───────────────────────────────────────────────
void CameraWidget::_platformLoadThumb(const std::string &path)
{
#if defined(__EMSCRIPTEN__)
    IDomAdapter *adapter = getActiveDomAdapter();
    if (!adapter)
        return;
    // render() calls this BEFORE _platformRenderThumb on the same
    // frame a new photo arrives (see render()'s _thumbDirty check) —
    // ensureNode here too rather than assuming the node already
    // exists; it's a plain get-or-create, so this is a no-op lookup on
    // every subsequent photo.
    DomNodeHandle node = fluxDomEnsureNode(this, "img", "thumbImg");
    adapter->setImageSourceFromFile(node, path);
#else
    (void)path; // FLUX_SSR — unreachable; _lastPhotoPath never gets set server-side
#endif
}

// ── _platformDestroy ─────────────────────────────────────────────────
void CameraWidget::_platformDestroy()
{
    // Nothing to do explicitly — both owned nodes (previewEl, thumbImg)
    // are cleaned up by the normal Widget::onDetach() ->
    // fluxDomEvictWidget(this) -> IDomAdapter::removeNode() path, which
    // now also revokes any outstanding thumbnail Blob URL (see the
    // removeNode() fix in flux_dom_adapter_live.cpp).
}

#endif // __EMSCRIPTEN__ || FLUX_SSR