#ifndef FLUX_LAYERS_HPP
#define FLUX_LAYERS_HPP

// ============================================================================
// flux_layers.hpp  —  LayeredSurface  (fixed revision)
// ============================================================================
//
// Bugs fixed vs. original:
//
//   FIX-1  restoreDisplayFBOs() no longer discards active-layer FBO handles.
//          The helper now saves the outgoing handles into activeCFBO_ /
//          activeSFBO_ so that redirectToActive() can always restore them
//          from a known-good location instead of re-reading layers_[].
//
//   FIX-2  buildComposite() now reads the live scratch via the base-class
//          scratch handle (scratchFBOHandle()) rather than
//          layers_[activeIdx_].scratchFBO, which may be stale after a swap.
//
//   FIX-3  Undo stack is flushed when the active layer changes (addLayer,
//          deleteLayer, setActiveLayer) so stale snapshots that belong to
//          a different layer can never be replayed onto the wrong target.
//          Also deleteLayer now flushes before freeing GL textures, which
//          prevents use-after-free in the snapshot pool.
//
//   FIX-4  dispSFBO_ / dispSTex_ removed — the display composite only ever
//          needs a committed texture; allocating a scratch display FBO was
//          wasted memory and the source of the swapped-in blank scratch.
//
//   FIX-5  LayeredSurface::clear() now pushes the undo snapshot BEFORE
//          destroying the pixel data, making Ctrl+Z actually work for clear.
//
//   FIX-6  Dirty flag added to buildComposite(): the composite is only
//          rebuilt (and glReadPixels only called) when something actually
//          changed, avoiding a full GPU→CPU stall on every frame.
//
// Architecture note
// -----------------
//   activeCFBO_ / activeCTex_ / activeSFBO_ / activeSFBO_ mirror the FBO
//   handles that are currently installed in the base class.  They are updated
//   in one place (redirectToActive / swapInDisplay / swapInActive) so there
//   is a single source of truth and no aliasing bugs.
//
// ============================================================================

#include "widgets/flux_raster.hpp"

#include <algorithm>
#include <cassert>
#include <functional>
#include <string>
#include <vector>

// ============================================================================
// §1  PUBLIC DATA TYPES
// ============================================================================

struct LayerDesc {
    std::string name;
    bool        visible  = true;
    bool        isActive = false;
};

struct LayerState {
    std::vector<LayerDesc> layers;   // layers[0] = bottom of stack
    int                    activeIdx = 0;
};

// ============================================================================
// §2  LayeredSurface
// ============================================================================

class LayeredSurface : public RasterSurface {
public:

    std::function<void(const LayerState &)> onLayersChanged;

    explicit LayeredSurface(size_t undoBudget = kDefaultUndoBudgetBytes)
        : RasterSurface(undoBudget) {}

    ~LayeredSurface() { destroyLayers(); }

    void markCompositeDirty() { compositeDirty_ = true; }

    // =========================================================================
    // §2a  Layer management API
    // =========================================================================

    void addLayer(const std::string &name = "") {
        // FIX-3: flush undo before any structural change so old snapshots
        // (which belong to whichever layer was previously active) cannot be
        // replayed onto a new, unrelated layer.
        flushUndoRedo();

        Layer lyr;
        lyr.name    = name.empty()
                      ? ("Layer " + std::to_string(layers_.size() + 1))
                      : name;
        lyr.visible = true;
        allocLayerFBOs(lyr);
        clearLayerContent(lyr, 0, 0, 0, 0);
        layers_.push_back(std::move(lyr));
        setActiveLayerInternal(int(layers_.size()) - 1);
        compositeDirty_ = true;
        fire();
    }

    void deleteLayer(int idx) {
        if (int(layers_.size()) <= 1) return;
        idx = clamped(idx);

        // FIX-3: flush BEFORE freeing textures so the undo stack never holds
        // handles to textures we are about to delete.
        flushUndoRedo();

        // If we are about to delete the active layer, first redirect the base
        // class away from it so its FBO is not bound when we free it.
        int newActive = std::clamp(
            activeIdx_ >= idx ? activeIdx_ - 1 : activeIdx_,
            0, int(layers_.size()) - 2);   // -2 because size shrinks by 1
        setActiveLayerInternal(newActive);

        freeLayer(layers_[idx]);
        layers_.erase(layers_.begin() + idx);

        // activeIdx_ may now point past the end if we deleted the last element.
        activeIdx_ = std::clamp(activeIdx_, 0, int(layers_.size()) - 1);
        redirectToActive();
        compositeDirty_ = true;
        fire();
    }

    void moveLayerUp(int idx) {
        idx = clamped(idx);
        if (idx >= int(layers_.size()) - 1) return;
        std::swap(layers_[idx], layers_[idx + 1]);
        if      (activeIdx_ == idx)     activeIdx_++;
        else if (activeIdx_ == idx + 1) activeIdx_--;
        redirectToActive();
        compositeDirty_ = true;
        fire();
    }

    void moveLayerDown(int idx) {
        idx = clamped(idx);
        if (idx <= 0) return;
        std::swap(layers_[idx], layers_[idx - 1]);
        if      (activeIdx_ == idx)     activeIdx_--;
        else if (activeIdx_ == idx - 1) activeIdx_++;
        redirectToActive();
        compositeDirty_ = true;
        fire();
    }

    void setActiveLayer(int idx) {
        idx = clamped(idx);
        if (idx == activeIdx_) return;
        // FIX-3: flush undo when switching layers so we never restore a
        // snapshot from layer A onto layer B.
        flushUndoRedo();
        setActiveLayerInternal(idx);
        fire();
    }

    void setLayerVisible(int idx, bool visible) {
        idx = clamped(idx);
        if (layers_[idx].visible == visible) return;
        layers_[idx].visible = visible;
        compositeDirty_ = true;
        fire();
    }

    void setLayerName(int idx, const std::string &name) {
        layers_[clamped(idx)].name = name;
        fire();
    }

    int getActiveLayerIdx() const { return activeIdx_; }
    int getLayerCount()     const { return int(layers_.size()); }

    // =========================================================================
    // §2b  RenderSurface overrides
    // =========================================================================

    void initialize(int w, int h) override {
        RasterSurface::initialize(w, h);

        // FIX-4: only capture the committed display FBO; we never need a
        // separate display scratch FBO because buildComposite writes a fully
        // composited image into dispCTex_ and the base render() blits only
        // that texture (scratch slot is cleared before the blit).
        dispCFBO_ = committedFBOHandle();
        dispCTex_ = committedTexHandle();

        // Seed layer 0 — white background.
        {
            Layer bg;
            bg.name    = "Background";
            bg.visible = true;
            allocLayerFBOs(bg);
            clearLayerContent(bg, 255, 255, 255, 255);
            layers_.push_back(std::move(bg));
        }
        activeIdx_ = 0;
        redirectToActive();
        compositeDirty_ = true;
        fire();
    }

    void resize(int w, int h) override {
        // Restore the display FBO into base-class slots before the base resize
        // so it reallocates its own texture (not the active layer's).
        swapInDisplay();

        int oldW = canvasWidth(), oldH = canvasHeight();
        RasterSurface::resize(w, h);

        // Re-capture the (possibly reallocated) display FBO handle.
        dispCFBO_ = committedFBOHandle();
        dispCTex_ = committedTexHandle();

        // Resize every layer's committed FBO, preserving content.
        int bw = min(oldW, w), bh = min(oldH, h);
        for (auto &lyr : layers_) {
            GLuint nFBO = 0, nTex = 0;
            allocFBOPair(nFBO, nTex, w, h);
            clearFBOColor(nFBO, w, h, 0, 0, 0, 0);
            if (bw > 0 && bh > 0) blitFBOs(lyr.committedFBO, nFBO, bw, bh);
            freeFBOPair(lyr.committedFBO, lyr.committedTex);
            lyr.committedFBO = nFBO;
            lyr.committedTex = nTex;

            freeFBOPair(lyr.scratchFBO, lyr.scratchTex);
            allocFBOPair(lyr.scratchFBO, lyr.scratchTex, w, h);
            clearFBOColor(lyr.scratchFBO, w, h, 0, 0, 0, 0);
        }

        // Re-install the active layer's FBOs into the base class.
        redirectToActive();
        compositeDirty_ = true;
    }

    // render():
    //   1. Build composite (CPU, skipped if nothing changed — FIX-6).
    //   2. Swap display FBOs in; render via base class (blit composite).
    //   3. Restore active-layer FBOs.
    void render(const float mvp[16]) override {
        buildComposite();   // FIX-6: no-op if compositeDirty_ == false

        // FIX-1/FIX-4: use the two-function swap pair so active-layer handles
        // are always saved before display FBOs are installed.
        swapInDisplay();
        RasterSurface::render(mvp);
        swapInActive();     // restore active layer FBOs for painting
    }

    void destroy() override {
        destroyLayers();
        RasterSurface::destroy();
    }

    void flattenToSingle() {
        int cw = canvasWidth(), ch = canvasHeight();

        // Build the composite first (uses current active-layer FBOs — correct).
        buildComposite(/*force=*/true);

        // Read the composite back from dispCTex_ via a temporary FBO.
        std::vector<uint8_t> pixels(size_t(cw) * ch * 4);
        {
            GLuint rf = 0;
            GL.genFramebuffers(1, &rf);
            GL.bindFramebuffer(GL_READ_FRAMEBUFFER, rf);
            GL.framebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                    GL_TEXTURE_2D, dispCTex_, 0);
            glReadPixels(0, 0, cw, ch, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
            GL.bindFramebuffer(GL_READ_FRAMEBUFFER, 0);
            GL.deleteFramebuffers(1, &rf);
        }

        // Redirect base class away from the active layer before we free layers.
        swapInDisplay();

        // Delete all layers; we'll rebuild layer 0 from scratch.
        destroyLayers();

        // Rebuild a single background layer.
        {
            Layer bg;
            bg.name    = "Background";
            bg.visible = true;
            allocLayerFBOs(bg);
            clearLayerContent(bg, 255, 255, 255, 255);
            layers_.push_back(std::move(bg));
        }

        // Upload the composite pixels into layer 0's committed texture.
        glBindTexture(GL_TEXTURE_2D, layers_[0].committedTex);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, cw, ch,
                        GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        glBindTexture(GL_TEXTURE_2D, 0);

        clearFBOColor(layers_[0].scratchFBO, cw, ch, 0, 0, 0, 0);

        activeIdx_ = 0;
        redirectToActive();
        flushUndoRedo();
        compositeDirty_ = true;
        fire();
    }

    // FIX-5: push undo snapshot BEFORE the destructive clear.
    void clear() {
        pushUndoSnapshotPublic();   // snapshot FIRST
        bool isBg = (activeIdx_ == 0);
        uint8_t v = isBg ? 255 : 0;
        clearLayerContent(layers_[activeIdx_], v, v, v, isBg ? 255 : 0);
        scratchClear();
        compositeDirty_ = true;
    }

    // Mark composite dirty whenever a stroke finishes (called by PaintApp).
    void onMouseUp(float x, float y) override {
        RasterSurface::onMouseUp(x, y);
        compositeDirty_ = true;
    }

private:

    // =========================================================================
    // §3  Internal layer struct
    // =========================================================================

    struct Layer {
        std::string name;
        bool        visible      = true;
        GLuint      committedFBO = 0, committedTex = 0;
        GLuint      scratchFBO   = 0, scratchTex   = 0;
    };

    std::vector<Layer> layers_;
    int                activeIdx_ = 0;

    // Display composite target — the base-class original committed FBO/tex.
    // FIX-4: we only need the committed pair; display scratch is not needed.
    GLuint dispCFBO_ = 0, dispCTex_ = 0;

    // FIX-1: mirrors of the FBO handles currently installed in the base class.
    // Updated by redirectToActive() and swapInDisplay()/swapInActive().
    GLuint activeCFBO_ = 0, activeCTex_ = 0;
    GLuint activeSFBO_ = 0, activeSTex_ = 0;

    // FIX-6: composite is only rebuilt when something has changed.
    bool compositeDirty_ = true;

    // Per-frame pixel buffers — avoid heap alloc inside buildComposite().
    std::vector<uint8_t> flatBuf_;
    std::vector<uint8_t> layBuf_;
    std::vector<uint8_t> scrBuf_;

    // =========================================================================
    // §4  Display / active FBO swap helpers  (FIX-1, FIX-4)
    //
    //  swapInDisplay()  — installs dispCFBO_/dispCTex_ into base class;
    //                     saves the outgoing handles into activeCFBO_ etc.
    //  swapInActive()   — reverses the swap, restoring active layer FBOs.
    //  redirectToActive() — unconditionally installs active-layer FBOs,
    //                       always safe to call; refreshes active* mirrors.
    // =========================================================================

    void swapInDisplay() {
        // Save whatever is currently in the base class (the active layer's FBOs)
        // then install the display FBOs.
        GLuint outCF, outCT, outSF, outST;
        swapActiveFBOs(dispCFBO_, dispCTex_,
                       activeSFBO_, activeSTex_,   // keep same scratch
                       outCF, outCT, outSF, outST);
        // outCF/CT are the active layer FBOs — persist them.
        activeCFBO_ = outCF;  activeCTex_ = outCT;
        // outSF/ST are the same scratch we just put back — no change needed.
    }

    void swapInActive() {
        GLuint outCF, outCT, outSF, outST;
        swapActiveFBOs(activeCFBO_, activeCTex_,
                       activeSFBO_, activeSTex_,
                       outCF, outCT, outSF, outST);
        // The display FBO is now safely back out of the base class.
    }

    void redirectToActive() {
        if (layers_.empty()) return;
        Layer &lyr = layers_[activeIdx_];

        // Save the mirrors so swapInDisplay/swapInActive always have
        // up-to-date values.
        activeCFBO_ = lyr.committedFBO;  activeCTex_ = lyr.committedTex;
        activeSFBO_ = lyr.scratchFBO;    activeSTex_ = lyr.scratchTex;

        GLuint d1, d2, d3, d4;
        swapActiveFBOs(activeCFBO_, activeCTex_,
                       activeSFBO_, activeSTex_,
                       d1, d2, d3, d4);
    }

    // =========================================================================
    // §5  CPU composite  (FIX-2, FIX-6)
    // =========================================================================

    void buildComposite(bool force = false) {
        // FIX-6: skip if nothing has changed.
        if (!force && !compositeDirty_) return;
        compositeDirty_ = false;

        int cw = canvasWidth(), ch = canvasHeight();
        int N  = cw * ch;

        flatBuf_.assign(size_t(N) * 4, 0);
        layBuf_.resize (size_t(N) * 4);
        scrBuf_.resize (size_t(N) * 4);

        for (int li = 0; li < int(layers_.size()); li++) {
            const Layer &lyr = layers_[li];
            if (!lyr.visible) continue;

            GL.bindFramebuffer(GL_READ_FRAMEBUFFER, lyr.committedFBO);
            glReadPixels(0, 0, cw, ch, GL_RGBA, GL_UNSIGNED_BYTE, layBuf_.data());
            GL.bindFramebuffer(GL_READ_FRAMEBUFFER, 0);

            // FIX-2: read live scratch via the BASE CLASS handle, not
            // layers_[activeIdx_].scratchFBO, which may be the stale value
            // that was swapped out when we installed the display FBOs.
            bool hasScratch = (li == activeIdx_);
            if (hasScratch) {
                // scratchFBOHandle() always returns the currently installed
                // scratch in the base class, i.e. the active layer's scratch.
                GL.bindFramebuffer(GL_READ_FRAMEBUFFER, scratchFBOHandle());
                glReadPixels(0, 0, cw, ch, GL_RGBA, GL_UNSIGNED_BYTE, scrBuf_.data());
                GL.bindFramebuffer(GL_READ_FRAMEBUFFER, 0);
            }

            uint8_t *dst       = flatBuf_.data();
            const uint8_t *lay = layBuf_.data();
            const uint8_t *scr = hasScratch ? scrBuf_.data() : nullptr;

            for (int i = 0; i < N; i++, dst += 4, lay += 4) {
                float lr = lay[0]/255.f, lg = lay[1]/255.f;
                float lb = lay[2]/255.f, la = lay[3]/255.f;

                if (hasScratch) {
                    const uint8_t *sp = scr + i * 4;
                    float sa = sp[3] / 255.f;
                    if (sa > 0.f) {
                        float oa = sa + la * (1.f - sa);
                        if (oa > 0.f) {
                            lr = (sp[0]/255.f * sa + lr * la * (1.f-sa)) / oa;
                            lg = (sp[1]/255.f * sa + lg * la * (1.f-sa)) / oa;
                            lb = (sp[2]/255.f * sa + lb * la * (1.f-sa)) / oa;
                        }
                        la = oa;
                    }
                }

                if (la <= 0.f) continue;

                float dr = dst[0]/255.f, dg = dst[1]/255.f;
                float db = dst[2]/255.f, da = dst[3]/255.f;
                float oa = la + da * (1.f - la);
                if (oa > 0.f) {
                    dst[0] = u8((lr*la + dr*da*(1.f-la)) / oa);
                    dst[1] = u8((lg*la + dg*da*(1.f-la)) / oa);
                    dst[2] = u8((lb*la + db*da*(1.f-la)) / oa);
                    dst[3] = u8(oa);
                }
            }
        }

        glBindTexture(GL_TEXTURE_2D, dispCTex_);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, cw, ch,
                        GL_RGBA, GL_UNSIGNED_BYTE, flatBuf_.data());
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    // =========================================================================
    // §6  FBO helpers
    // =========================================================================

    static void allocFBOPair(GLuint &fbo, GLuint &tex, int w, int h) {
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glBindTexture(GL_TEXTURE_2D, 0);
        GL.genFramebuffers(1, &fbo);
        GL.bindFramebuffer(GL_FRAMEBUFFER, fbo);
        GL.framebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                GL_TEXTURE_2D, tex, 0);
        assert(GL.checkFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
        GL.bindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    static void freeFBOPair(GLuint &fbo, GLuint &tex) {
        if (fbo) { GL.deleteFramebuffers(1, &fbo); fbo = 0; }
        if (tex) { glDeleteTextures(1, &tex);       tex = 0; }
    }

    static void clearFBOColor(GLuint fbo, int w, int h,
                               uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
        GL.bindFramebuffer(GL_FRAMEBUFFER, fbo);
        glViewport(0, 0, w, h);
        glClearColor(r/255.f, g/255.f, b/255.f, a/255.f);
        glClear(GL_COLOR_BUFFER_BIT);
        GL.bindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    static void blitFBOs(GLuint src, GLuint dst, int w, int h) {
        GL.bindFramebuffer(GL_READ_FRAMEBUFFER, src);
        GL.bindFramebuffer(GL_DRAW_FRAMEBUFFER, dst);
        GL.blitFramebuffer(0, 0, w, h, 0, 0, w, h,
                           GL_COLOR_BUFFER_BIT, GL_NEAREST);
        GL.bindFramebuffer(GL_READ_FRAMEBUFFER, 0);
        GL.bindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    }

    void allocLayerFBOs(Layer &lyr) {
        int cw = canvasWidth(), ch = canvasHeight();
        allocFBOPair(lyr.committedFBO, lyr.committedTex, cw, ch);
        allocFBOPair(lyr.scratchFBO,   lyr.scratchTex,   cw, ch);
    }

    void clearLayerContent(Layer &lyr,
                           uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
        int cw = canvasWidth(), ch = canvasHeight();
        clearFBOColor(lyr.committedFBO, cw, ch, r, g, b, a);
        clearFBOColor(lyr.scratchFBO,   cw, ch, 0, 0, 0, 0);
    }

    void freeLayer(Layer &lyr) {
        freeFBOPair(lyr.committedFBO, lyr.committedTex);
        freeFBOPair(lyr.scratchFBO,   lyr.scratchTex);
    }

    void destroyLayers() {
        for (auto &l : layers_) freeLayer(l);
        layers_.clear();
    }

    // =========================================================================
    // §7  Internal helpers
    // =========================================================================

    void setActiveLayerInternal(int idx) {
        activeIdx_ = std::clamp(idx, 0, int(layers_.size()) - 1);
        redirectToActive();
    }

    // FIX-3: flush both undo and redo stacks via the protected helper added
    // to RasterSurface (see flux_canvas_patch.hpp).  This ensures stale
    // snapshots from one layer are never replayed onto a different layer.
    void flushUndoRedo() {
        flushUndoRedoPublic();
    }

    int clamped(int idx) const {
        return std::clamp(idx, 0, int(layers_.size()) - 1);
    }

    static uint8_t u8(float v) {
        int i = int(v * 255.f + 0.5f);
        return uint8_t(i < 0 ? 0 : i > 255 ? 255 : i);
    }

    void fire() {
        if (!onLayersChanged) return;
        LayerState ls;
        ls.activeIdx = activeIdx_;
        ls.layers.reserve(layers_.size());
        for (int i = 0; i < int(layers_.size()); i++) {
            LayerDesc d;
            d.name     = layers_[i].name;
            d.visible  = layers_[i].visible;
            d.isActive = (i == activeIdx_);
            ls.layers.push_back(std::move(d));
        }
        onLayersChanged(ls);
    }
};

// ============================================================================
// §8  FACTORY HELPER
// ============================================================================

inline std::shared_ptr<CanvasWidget>
LayeredCanvas(int viewW, int viewH, int canvasW, int canvasH) {
    auto c = std::make_shared<CanvasWidget>()->setSize(viewW, viewH);
    c->setCanvasSize(canvasW, canvasH);
    c->setSurface<LayeredSurface>();
    return c;
}

// ============================================================================
// §9  PATCH REQUIRED IN flux_canvas.hpp  (unchanged from original)
// ============================================================================
//
// In protected: section of RasterSurface, after void scratchClear():
//
//   void swapActiveFBOs(GLuint  newCFBO, GLuint  newCTex,
//                       GLuint  newSFBO, GLuint  newSTex,
//                       GLuint &oldCFBO, GLuint &oldCTex,
//                       GLuint &oldSFBO, GLuint &oldSTex) {
//       oldCFBO = committedFBO_; oldCTex = committedTex_;
//       oldSFBO = scratchFBO_;   oldSTex = scratchTex_;
//       committedFBO_ = newCFBO; committedTex_ = newCTex;
//       scratchFBO_   = newSFBO; scratchTex_   = newSTex;
//   }
//
// ADDITIONAL patch for flushUndoRedo support — add after scratchClear():
//
//   void flushUndoRedoPublic() {
//       flushDeque(undoStack_, undoBytes_);
//       flushDeque(redoStack_, redoBytes_);
//   }
//
// ============================================================================

#endif // FLUX_LAYERS_HPP