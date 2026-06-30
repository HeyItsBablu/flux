// camera_widget_web.cpp
// Web (Emscripten) platform implementation for CameraWidget.
//
// Preview: FluxCamera::renderFrame() blits the hidden <video> element
//          straight into the shared Canvas2D context (_fluxCtx2D).
//          No CPU pixel buffer, no texture upload — mirrors exactly how
//          camera_widget_win32.cpp calls StretchDIBits, except the browser
//          owns the decode pipeline.
//
// Thumbnail: The saved JPEG lives on MEMFS under /photos/. We read it back
//            with FS.readFile(), wrap it in a Blob URL, and assign it to a
//            hidden <img> element.  The actual draw call is a second
//            CanvasRenderingContext2D.drawImage() on the same ctx2D used for
//            the preview — consistent with the rest of the web renderer.
//
// Flash: A plain fillRect() over the view area with the current _flashAlpha,
//        matching how flux_painter_web.cpp exposes p.fillRect().
//
// Letterbox / pillarbox: Identical logic to camera_widget_win32.cpp — compute
//                        a destination rect that preserves the stream's aspect
//                        ratio within (x, y, width, viewH), then fill the bars
//                        with colPlaceholder before blitting.
//
// Mirroring: renderFrame() on the web engine accepts a `mirror` flag that
//            applies ctx.scale(-1,1) inside a save/restore pair.  We pass
//            isFrontCamera() here, mirroring camera_widget_win32.cpp's
//            negative-width StretchDIBits trick for the front camera.
//
// Threading: Single-threaded like all other web backends.  _platformScheduleOpen
//            sets _shouldOpen immediately; no permission-check timer is needed
//            (getUserMedia itself triggers the browser permission UI).

#ifdef __EMSCRIPTEN__

#include "flux/widgets/camera_widget.hpp"

#include <emscripten.h>

// ── _platformScheduleOpen ─────────────────────────────────────────────────────
// getUserMedia handles permission gating itself, so no pre-flight timer is
// needed.  Mirror what Win32 does: set the flag and let the next render() call
// open the camera.

void CameraWidget::_platformScheduleOpen()
{
    _shouldOpen = true;
}

// ── _platformOnFlip ───────────────────────────────────────────────────────────
// No CPU frame cache or GPU texture to invalidate — the <video> element is
// always live and renderFrame() blits whatever the browser currently holds.
// The only thing we need to do is tear down the thumbnail <img> so a stale
// photo from the previous camera orientation isn't shown while the new stream
// hasn't produced a capture yet.

void CameraWidget::_platformOnFlip()
{
    EM_ASM({
        if (Module._fluxCameraThumbEl)
        {
            Module._fluxCameraThumbEl.src = "";
            if (Module._fluxCameraThumbBlobUrl)
            {
                URL.revokeObjectURL(Module._fluxCameraThumbBlobUrl);
                Module._fluxCameraThumbBlobUrl = null;
            }
        }
    });
}

// ── _platformRenderPreview ────────────────────────────────────────────────────

bool CameraWidget::_platformRenderPreview(GraphicsContext & /*ctx*/, Painter &p,
                                          FontCache & /*fontCache*/, int viewH)
{
    auto &cam = FluxCamera::get();

    if (!cam.hasNewFrame())
        return false;

    int srcW = cam.getPreviewWidth();
    int srcH = cam.getPreviewHeight();
    if (srcW <= 0 || srcH <= 0)
        return false;

    // ── Letterbox / pillarbox ──────────────────────────────────────────────
    float camAR = (float)srcW / (float)srcH;
    float widgetAR = (float)width / (float)viewH;
    int dstW, dstH, dstX, dstY;
    if (camAR > widgetAR)
    {
        dstW = width;
        dstH = (int)((float)width / camAR);
        dstX = x;
        dstY = y + (viewH - dstH) / 2;
    }
    else
    {
        dstH = viewH;
        dstW = (int)((float)viewH * camAR);
        dstX = x + (width - dstW) / 2;
        dstY = y;
    }

    // Fill letterbox bars with the placeholder colour
    if (dstX > x)
        p.fillRect(x, y, dstX - x, viewH, colPlaceholder);
    if (dstX + dstW < x + width)
        p.fillRect(dstX + dstW, y, (x + width) - (dstX + dstW), viewH, colPlaceholder);
    if (dstY > y)
        p.fillRect(x, y, width, dstY - y, colPlaceholder);
    if (dstY + dstH < y + viewH)
        p.fillRect(x, dstY + dstH, width, (y + viewH) - (dstY + dstH), colPlaceholder);

    Painter::CameraDrawParams cp;
    cp.dstX = dstX;
    cp.dstY = dstY;
    cp.dstW = dstW;
    cp.dstH = dstH;
    cp.mirror = cam.isFrontCamera();
    p.drawCamera(cp);

    return true;
}

// ── _platformRenderFlash ──────────────────────────────────────────────────────
// A plain white fillRect at the current alpha — Canvas2D globalAlpha handles
// the blend, so no separate bitmap or AlphaBlend call is needed.

void CameraWidget::_platformRenderFlash(GraphicsContext & /*ctx*/, Painter &/*p*/,
                                        int viewH)
{
    // Temporarily override the painter's global alpha for this one rect.
    // The alpha is already decaying in render() so we just use whatever
    // colFlash carries, with a per-draw alpha set on the canvas context.
    int alpha = (int)(_flashAlpha * 255.f);
    if (alpha <= 0)
        return;

    EM_ASM({
        var c = Module._fluxCtx2D;
        if (!c) return;
        var prev = c.globalAlpha;
        c.globalAlpha = $4 / 255.0;
        c.fillStyle = 'rgb(255,255,255)';
        c.fillRect($0, $1, $2, $3);
        c.globalAlpha = prev; }, x, y, width, viewH, alpha);
}

// ── _platformRenderThumb ──────────────────────────────────────────────────────
// Draws the thumbnail <img> element (loaded by _platformLoadThumb) into the
// Canvas2D context via drawImage().  Returns false if no image is ready yet.

bool CameraWidget::_platformRenderThumb(GraphicsContext & /*ctx*/,
                                        int thumbX, int thumbY,
                                        int thumbW, int thumbH)
{
    return EM_ASM_INT({
        var img = Module._fluxCameraThumbEl;
        if (!img || !img.complete || img.naturalWidth === 0)
            return 0;
        var c = Module._fluxCtx2D;
        if (!c) return 0;
        c.drawImage(img, $0, $1, $2, $3);
        return 1; }, thumbX, thumbY, thumbW, thumbH) != 0;
}

// ── _platformLoadThumb ────────────────────────────────────────────────────────
// The JPEG was written to MEMFS by FluxCamera::_onPhotoBytes.  Read it back
// with FS.readFile(), wrap in a Blob, and point a hidden <img> at a Blob URL.
// The previous Blob URL is revoked first to avoid memory leaks on rapid-fire
// captures.  _platformRenderThumb checks img.complete before drawing, so
// partial loads never produce torn frames.

void CameraWidget::_platformLoadThumb(const std::string &path)
{
    EM_ASM({
        var path = UTF8ToString($0);

        // Lazy-create the off-screen <img> element
        if (!Module._fluxCameraThumbEl) {
            var img = document.createElement('img');
            img.style.display = 'none';
            document.body.appendChild(img);
            Module._fluxCameraThumbEl    = img;
            Module._fluxCameraThumbBlobUrl = null;
        }

        // Revoke any previous Blob URL to free memory
        if (Module._fluxCameraThumbBlobUrl) {
            URL.revokeObjectURL(Module._fluxCameraThumbBlobUrl);
            Module._fluxCameraThumbBlobUrl = null;
        }

        var bytes;
        try {
            bytes = FS.readFile(path);
        } catch (e) {
            console.error('[CameraWidget] FS.readFile failed for', path, e);
            return;
        }

        var blob = new Blob([bytes], { type: 'image/jpeg' });
        var url  = URL.createObjectURL(blob);
        Module._fluxCameraThumbBlobUrl  = url;
        Module._fluxCameraThumbEl.src   = url; }, path.c_str());
}

// ── _platformDestroy ──────────────────────────────────────────────────────────
// Revoke any outstanding Blob URL and detach the thumbnail element.
// The <video> element and MediaStream belong to FluxCamera, not to us; they
// are torn down by FluxCamera::close() which CameraWidget::~CameraWidget()
// calls after _platformDestroy().

void CameraWidget::_platformDestroy()
{
    EM_ASM({
        if (Module._fluxCameraThumbEl)
        {
            Module._fluxCameraThumbEl.src = "";
            if (Module._fluxCameraThumbEl.parentNode)
                Module._fluxCameraThumbEl.parentNode.removeChild(Module._fluxCameraThumbEl);
            Module._fluxCameraThumbEl = null;
        }
        if (Module._fluxCameraThumbBlobUrl)
        {
            URL.revokeObjectURL(Module._fluxCameraThumbBlobUrl);
            Module._fluxCameraThumbBlobUrl = null;
        }
    });
}

#endif // __EMSCRIPTEN__