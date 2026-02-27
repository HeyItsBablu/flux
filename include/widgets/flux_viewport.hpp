#pragma once

// ============================================================================
// flux_viewport.hpp  —  Zoom / pan / scrollbar sync
// ============================================================================
//
// Coordinate contract
// ───────────────────
//   Canvas space : origin bottom-left, y-up  (OpenGL convention)
//   Screen space : origin top-left,    y-down (Win32 convention)
//
//   The matrix produced by buildMatrix() maps canvas → NDC with y-flip
//   baked in, so RenderSurface implementations can simply use canvas-space
//   coordinates without caring about Win32's y-down origin.
//
// Ownership contract
// ──────────────────
//   CanvasWidget owns one Viewport.
//   RenderSurface never touches Viewport — it receives already-transformed
//   canvas-space coordinates from CanvasWidget's input handlers.
//
// Zoom snap points  (Figma style)
// ───────────────────────────────
//   A fixed set of "clean" zoom levels is defined in kSnapZooms[].
//   When the zoom lands within kSnapTolerance of a snap level the value
//   is silently nudged onto that level.
//
// Scrollbar helpers
// ─────────────────
//   scrollbarH() / scrollbarV() return a ScrollbarInfo struct containing
//   the thumb position and size as normalised [0,1] values ready to pass
//   directly to SetScrollInfo().
//
// ============================================================================

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

// ── Constants ────────────────────────────────────────────────────────────────

static constexpr float kViewportMinZoom = 0.0625f;  // 1/16 ×
static constexpr float kViewportMaxZoom = 32.0f;    // 32 ×
static constexpr float kSnapTolerance   = 0.04f;    // 4 % relative error

// Snap levels that match Figma / Photoshop muscle memory
static constexpr std::array<float, 12> kSnapZooms = {
    0.0625f, 0.125f, 0.25f, 0.5f, 0.75f,
    1.0f,    1.5f,   2.0f,  4.0f, 8.0f, 16.0f, 32.0f
};

// ============================================================================
// ScrollbarInfo  —  normalised thumb geometry for Win32 SCROLLINFO
// ============================================================================

struct ScrollbarInfo {
    float thumbMin;   // [0,1] position of thumb leading edge
    float thumbMax;   // [0,1] position of thumb trailing edge
    bool  visible;    // false when the entire canvas fits in the view
};

// ============================================================================
// Viewport
// ============================================================================

class Viewport {
public:
    Viewport() = default;

    // ── Setup ─────────────────────────────────────────────────────────────────

    void init(int viewW, int viewH, int canvasW, int canvasH) {
        viewW_   = (float)viewW;
        viewH_   = (float)viewH;
        canvasW_ = (float)canvasW;
        canvasH_ = (float)canvasH;
        zoom_    = 1.0f;
        offsetX_ = 0.0f;
        offsetY_ = 0.0f;
        dirty_   = true;
    }

    void setViewSize(int w, int h) {
        viewW_ = (float)w;
        viewH_ = (float)h;
        clamp();
        dirty_ = true;
    }

    void setCanvasSize(int w, int h) {
        canvasW_ = (float)w;
        canvasH_ = (float)h;
        clamp();
        dirty_ = true;
    }

    // ── Coordinate transforms ─────────────────────────────────────────────────

    // Screen pixel (Win32 y-down) → canvas pixel (y-up).
    std::pair<float, float> screenToCanvas(float sx, float sy) const {
        float l, r, b, t;
        visibleRect(l, r, b, t);
        float ndcX =  2.0f * sx / viewW_ - 1.0f;
        float ndcY = -2.0f * sy / viewH_ + 1.0f;
        float cx = l + (ndcX + 1.0f) * 0.5f * (r - l);
        float cy = b + (ndcY + 1.0f) * 0.5f * (t - b);
        return { cx, cy };
    }

    // Canvas pixel (y-up) → screen pixel (Win32 y-down).
    std::pair<float, float> canvasToScreen(float cx, float cy) const {
        float l, r, b, t;
        visibleRect(l, r, b, t);
        float ndcX = 2.0f * (cx - l) / (r - l) - 1.0f;
        float ndcY = 2.0f * (cy - b) / (t - b) - 1.0f;
        float sx =  (ndcX + 1.0f) * 0.5f * viewW_;
        float sy = -(ndcY - 1.0f) * 0.5f * viewH_;
        return { sx, sy };
    }

    // ── Zoom ──────────────────────────────────────────────────────────────────

    // Zoom toward a screen-space pivot (e.g. mouse cursor).
    // factor > 1 zooms in, factor < 1 zooms out.
    void zoomToward(float screenPivotX, float screenPivotY, float factor) {
        auto [cpx, cpy] = screenToCanvas(screenPivotX, screenPivotY);

        float newZoom = std::clamp(zoom_ * factor, kViewportMinZoom, kViewportMaxZoom);
        newZoom = applySnap(newZoom);

        zoom_    = newZoom;
        offsetX_ = cpx - screenPivotX / newZoom;
        offsetY_ = cpy - (viewH_ - screenPivotY) / newZoom;

        clamp();
        dirty_ = true;
    }

    void zoomIn()  { zoomToward(viewW_ * 0.5f, viewH_ * 0.5f, 1.25f); }
    void zoomOut() { zoomToward(viewW_ * 0.5f, viewH_ * 0.5f, 0.8f);  }

    void setZoom(float z) {
        zoom_ = applySnap(std::clamp(z, kViewportMinZoom, kViewportMaxZoom));
        center();
        dirty_ = true;
    }

    void resetZoom() {
        zoom_ = 1.0f;
        center();
        dirty_ = true;
    }

    void fitToView() {
        float fit = min(viewW_ / canvasW_, viewH_ / canvasH_);
        zoom_ = applySnap(std::clamp(fit, kViewportMinZoom, kViewportMaxZoom));
        center();
        dirty_ = true;
    }

    // ── Pan ───────────────────────────────────────────────────────────────────

    void panByScreen(float dsx, float dsy) {
        offsetX_ -= dsx / zoom_;
        offsetY_ += dsy / zoom_;
        clamp();
        dirty_ = true;
    }

    void setOffset(float cx, float cy) {
        offsetX_ = cx;
        offsetY_ = cy;
        clamp();
        dirty_ = true;
    }

    // ── Update ────────────────────────────────────────────────────────────────

    // No animation — always returns false (nothing still running).
    // Signature kept so CanvasWidget::tickAndRender() compiles unchanged.
    bool update(double /*dt*/) {
        dirty_ = false;
        return false;
    }

    // ── Matrix ────────────────────────────────────────────────────────────────

    // Column-major 4×4 ortho MVP — pass to glUniformMatrix4fv(..., GL_FALSE, out).
    void buildMatrix(float out[16]) const {
        float l, r, b, t;
        visibleRect(l, r, b, t);
        float rml = r - l, tmb = t - b;
        out[0]  =  2.0f / rml;    out[1]  = 0;              out[2]  = 0;  out[3]  = 0;
        out[4]  =  0;              out[5]  = 2.0f / tmb;     out[6]  = 0;  out[7]  = 0;
        out[8]  =  0;              out[9]  = 0;              out[10] = -1; out[11] = 0;
        out[12] = -(r + l) / rml; out[13] = -(t + b) / tmb; out[14] = 0;  out[15] = 1;
    }

    // ── Scrollbars ────────────────────────────────────────────────────────────

    ScrollbarInfo scrollbarH() const {
        float viewCanvas = viewW_ / zoom_;
        if (viewCanvas >= canvasW_) return { 0, 1, false };
        float start = offsetX_;
        return {
            std::clamp(start / canvasW_,                      0.0f, 1.0f),
            std::clamp((start + viewCanvas) / canvasW_,       0.0f, 1.0f),
            true
        };
    }

    ScrollbarInfo scrollbarV() const {
        float viewCanvas = viewH_ / zoom_;
        if (viewCanvas >= canvasH_) return { 0, 1, false };
        float start = offsetY_;
        float end   = start + viewCanvas;
        // Flip: scrollbar 0 = top of canvas = high canvas-y
        return {
            std::clamp(1.0f - end   / canvasH_, 0.0f, 1.0f),
            std::clamp(1.0f - start / canvasH_, 0.0f, 1.0f),
            true
        };
    }

    // ── Accessors ─────────────────────────────────────────────────────────────

    float zoom()       const { return zoom_; }
    float targetZoom() const { return zoom_; }   // compat alias
    float offsetX()    const { return offsetX_; }
    float offsetY()    const { return offsetY_; }
    float viewW()      const { return viewW_; }
    float viewH()      const { return viewH_; }
    float canvasW()    const { return canvasW_; }
    float canvasH()    const { return canvasH_; }
    bool  isDirty()    const { return dirty_; }

private:
    float viewW_   = 1, viewH_   = 1;
    float canvasW_ = 1, canvasH_ = 1;
    float zoom_    = 1.0f;
    float offsetX_ = 0.0f;   // canvas-space left edge of view
    float offsetY_ = 0.0f;   // canvas-space bottom edge of view (y-up)
    bool  dirty_   = false;

    // ── Helpers ───────────────────────────────────────────────────────────────

    void visibleRect(float &l, float &r, float &b, float &t) const {
        l = offsetX_;
        r = offsetX_ + viewW_ / zoom_;
        b = offsetY_;
        t = offsetY_ + viewH_ / zoom_;
    }

    static float applySnap(float z) {
        for (float snap : kSnapZooms)
            if (std::abs(z - snap) / snap < kSnapTolerance) return snap;
        return z;
    }

    void center() {
        offsetX_ = (canvasW_ - viewW_ / zoom_) * 0.5f;
        offsetY_ = (canvasH_ - viewH_ / zoom_) * 0.5f;
        clamp();
    }

    void clamp() {
        float marginX = (viewW_ / zoom_) * 0.5f;
        float marginY = (viewH_ / zoom_) * 0.5f;
        offsetX_ = std::clamp(offsetX_, -marginX, canvasW_ - marginX);
        offsetY_ = std::clamp(offsetY_, -marginY, canvasH_ - marginY);
    }
};