// src/flux_image_web.cpp
//
// ImageWidget platform implementation for Emscripten / WebAssembly.
//
// Decode path
// ───────────
// All decoding goes through stb_image (already compiled into the WASM binary
// via stb_impl.cpp).  The shared _decodeIntoStaging() in flux_image.hpp calls
// stbi_load_from_memory() and passes the raw RGBA bytes here via
// _platformStorePixels().
//
// _platformDecode() is a stub returning false — we never use GDI+ or any
// other native decoder on web, so the stb path always runs.
//
// Staging / promote 
// ─────────────────
// Because decoding may happen on a background thread (asset loads, network
// images), we follow the same two-phase pattern used on Linux / macOS:
//
//   Background thread  → _platformStorePixels()
//     writes RGBA bytes + dimensions into _pending  (mutex-protected)
//
//   UI thread (render/computeLayout) → _platformPromote()
//     moves _pending into the JS offscreen canvas and marks the widget Loaded.
//
// Rendering
// ─────────
// _platformRender() uses drawImage() on an OffscreenCanvas to blit + scale
// pixels according to the DestRect calculated by _calculateDestRect().
// putImageData() is only used during upload (no scaling); drawImage() handles
// all fit/alignment scaling at draw time.
//
// EM_ASM comma rule
// ─────────────────
// The EM_ASM macro parser treats commas as C++ argument separators, so JS
// "var a = $0, b = $1" is illegal inside an EM_ASM block.
// Rule: ONE var declaration per line inside every EM_ASM block.
// See flux_painter_web.cpp header comment for the full explanation.

#ifdef __EMSCRIPTEN__

#include "flux/widgets/flux_image.hpp"

#include <emscripten.h>
#include <mutex>
#include <vector>
#include <cstring>
#include <algorithm>
#include <unordered_map>

// ============================================================================
// JS-side image store
//
// Module._fluxImgStore  — Map<int, OffscreenCanvas>
// Module._fluxImgNextId — next free key (starts at 1; 0 = no image)
//
// C++ holds an integer key per ImageWidget via WebImgState.
// ============================================================================

static void ensureImgStore()
{
    EM_ASM({
        if (!Module._fluxImgStore)
        {
            Module._fluxImgStore = new Map();
            Module._fluxImgNextId = 1;
        }
    });
}

static int allocImgKey()
{
    ensureImgStore();
    return EM_ASM_INT({ return Module._fluxImgNextId++; });
}

static void freeImgKey(int key)
{
    if (key <= 0)
        return;
    EM_ASM({ Module._fluxImgStore.delete($0); }, key);
}

// Upload RGBA pixels into the OffscreenCanvas stored under key.
// Creates or resizes the canvas as needed.
static void uploadPixels(int key, const uint8_t *rgba, int w, int h)
{
    if (key <= 0 || !rgba || w <= 0 || h <= 0)
        return;

    // NOTE: no comma-separated var declarations — each var on its own line.
    EM_ASM({
        var key   = $0;
        var ptr   = $1;
        var w     = $2;
        var h     = $3;
        var store = Module._fluxImgStore;
        var oc    = store.get(key);
        if (!oc || oc.width !== w || oc.height !== h) {
            oc = new OffscreenCanvas(w, h);
            store.set(key, oc);
        }
        var octx = oc.getContext('2d');
        var src  = new Uint8ClampedArray(Module.HEAPU8.buffer, ptr, w * h * 4);
        var id   = new ImageData(src.slice(), w, h);
        octx.putImageData(id, 0, 0); }, key, rgba, w, h);
}

// ============================================================================
// Per-widget web state (stored outside the widget to avoid touching the header)
// ============================================================================

struct WebImgState
{
    int key = 0; // OffscreenCanvas handle; 0 = not yet allocated
    int uploadW = 0;
    int uploadH = 0;
};

static std::unordered_map<ImageWidget *, WebImgState> s_webImg;

static WebImgState &webImg(ImageWidget *w) { return s_webImg[w]; }

// ============================================================================
// ImageWidget destructor
// ============================================================================

ImageWidget::~ImageWidget()
{
    _platformDestroy();
}

// ============================================================================
// _platformDecode  — stub; stb_image path handles all decoding on web
// ============================================================================

bool ImageWidget::_platformDecode(const uint8_t * /*data*/, int /*len*/)
{
    return false;
}

// ============================================================================
// _platformStorePixels
//
// Called from the background decode thread with raw RGBA bytes from stb.
// Stores them in _pending under the decode mutex.
// ============================================================================

bool ImageWidget::_platformStorePixels(unsigned char *rgba, int w, int h)
{
    if (!rgba || w <= 0 || h <= 0)
        return false;

    std::lock_guard<std::mutex> lock(_decodeMutex);
    _pending.pixels.assign(rgba, rgba + (size_t)(w * h * 4));
    _pending.width = w;
    _pending.height = h;
    return true;
}

// ============================================================================
// _platformPromote
//
// Called on the UI thread (from computeLayout and render).
// Moves _pending into the JS OffscreenCanvas.
// ============================================================================

void ImageWidget::_platformPromote()
{
    std::unique_lock<std::mutex> lock(_decodeMutex);
    if (!_pending.ready())
        return;

    DecodedImage local;
    local.pixels = std::move(_pending.pixels);
    local.width = _pending.width;
    local.height = _pending.height;
    _pending.clear();
    lock.unlock();

    imageWidth = local.width;
    imageHeight = local.height;

    auto &ws = webImg(this);
    if (ws.key <= 0)
        ws.key = allocImgKey();

    uploadPixels(ws.key, local.pixels.data(), local.width, local.height);
    ws.uploadW = local.width;
    ws.uploadH = local.height;

    // Keep CPU copy for context-loss re-upload
    pixels = std::move(local.pixels);

    _setLoadState(ImageLoadState::Loaded);
    markNeedsLayout();
}

// ============================================================================
// _platformRender
//
// Draws the OffscreenCanvas onto Module._fluxCtx2D.
// All fit/alignment math is already done by _calculateDestRect().
// Supports ImageRepeat via createPattern() and borderRadius via clip path.
//
// EM_ASM rule enforced throughout: one var per line, no comma-chained decls.
// ============================================================================

void ImageWidget::_platformRender(GraphicsContext & /*ctx*/,
                                  int cx, int cy, int cw, int ch)
{
    auto &ws = webImg(this);
    if (ws.key <= 0 || imageWidth <= 0 || imageHeight <= 0)
        return;

    DestRect d = _calculateDestRect(cx, cy, cw, ch);

    const char *patternRepeat = "no-repeat";
    if (repeat == ImageRepeat::Repeat)
        patternRepeat = "repeat";
    if (repeat == ImageRepeat::RepeatX)
        patternRepeat = "repeat-x";
    if (repeat == ImageRepeat::RepeatY)
        patternRepeat = "repeat-y";

    // FilterQuality → int: None=0 Low=1 Medium=2 High=3
    int quality = (int)filterQuality;

    if (borderRadius > 0)
    {
        // ── Rounded corners: save → clip rounded rect → draw → restore ────
        // All parameters passed as separate positional args ($0..$14).
        // No comma-chained var declarations inside the block.
        EM_ASM({
            var c = Module._fluxCtx2D;
            if (!c) return;
            var oc = Module._fluxImgStore && Module._fluxImgStore.get($0);
            if (!oc) return;
            var cx = $1;
            var cy = $2;
            var cw = $3;
            var ch = $4;
            var dx = $5;
            var dy = $6;
            var dw = $7;
            var dh = $8;
            var wx = $9;
            var wy = $10;
            var fw = $11;
            var fh = $12;
            var r  = $13;
            var q  = $14;
            var rp = UTF8ToString($15);
            c.imageSmoothingEnabled = (q > 0);
            c.imageSmoothingQuality = (q >= 3) ? 'high' : (q >= 2) ? 'medium' : 'low';
            c.save();
            if (Module._fluxRRect) {
                Module._fluxRRect(c, wx, wy, fw, fh, r);
            } else {
                c.beginPath();
                c.moveTo(wx + r, wy);
                c.lineTo(wx + fw - r, wy);
                c.arcTo(wx + fw, wy, wx + fw, wy + r, r);
                c.lineTo(wx + fw, wy + fh - r);
                c.arcTo(wx + fw, wy + fh, wx + fw - r, wy + fh, r);
                c.lineTo(wx + r, wy + fh);
                c.arcTo(wx, wy + fh, wx, wy + fh - r, r);
                c.lineTo(wx, wy + r);
                c.arcTo(wx, wy, wx + r, wy, r);
                c.closePath();
            }
            c.clip();
            c.beginPath();
            c.rect(cx, cy, cw, ch);
            c.clip();
            if (rp !== 'no-repeat') {
                var pat = c.createPattern(oc, rp);
                if (pat) {
                    c.translate(dx, dy);
                    c.fillStyle = pat;
                    c.fillRect(cx - dx, cy - dy, cw, ch);
                }
            } else {
                c.drawImage(oc, dx, dy, dw, dh);
            }
            c.restore(); }, ws.key, cx, cy, cw, ch, (int)d.x, (int)d.y, (int)d.w, (int)d.h, x, y, (int)width, (int)height, (int)borderRadius, quality, patternRepeat);
    }
    else
    {
        // ── No border radius — clip to content rect and draw ──────────────
        EM_ASM({
            var c = Module._fluxCtx2D;
            if (!c) return;
            var oc = Module._fluxImgStore && Module._fluxImgStore.get($0);
            if (!oc) return;
            var cx = $1;
            var cy = $2;
            var cw = $3;
            var ch = $4;
            var dx = $5;
            var dy = $6;
            var dw = $7;
            var dh = $8;
            var q  = $9;
            var rp = UTF8ToString($10);
            c.imageSmoothingEnabled = (q > 0);
            c.imageSmoothingQuality = (q >= 3) ? 'high' : (q >= 2) ? 'medium' : 'low';
            c.save();
            c.beginPath();
            c.rect(cx, cy, cw, ch);
            c.clip();
            if (rp !== 'no-repeat') {
                var pat = c.createPattern(oc, rp);
                if (pat) {
                    c.translate(dx, dy);
                    c.fillStyle = pat;
                    c.fillRect(cx - dx, cy - dy, cw, ch);
                }
            } else {
                c.drawImage(oc, dx, dy, dw, dh);
            }
            c.restore(); }, ws.key, cx, cy, cw, ch, (int)d.x, (int)d.y, (int)d.w, (int)d.h, quality, patternRepeat);
    }
}

// ============================================================================
// _platformInvalidateCache
//
// No C++-side scale cache exists on web — the browser scales via drawImage().
// Just trigger a repaint.
// ============================================================================

void ImageWidget::_platformInvalidateCache()
{
    markNeedsPaint();
}

// ============================================================================
// _platformDestroy
// ============================================================================

void ImageWidget::_platformDestroy()
{
    auto it = s_webImg.find(this);
    if (it != s_webImg.end())
    {
        freeImgKey(it->second.key);
        s_webImg.erase(it);
    }
    pixels.clear();
}

#endif // __EMSCRIPTEN__