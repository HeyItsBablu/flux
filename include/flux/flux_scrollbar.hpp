// ============================================================================
// flux_scrollbar.hpp — cross-platform
// ============================================================================
#pragma once

#include "flux_canvas_types.hpp"
#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

#if defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__))
#  include <glad/glad.h>
#  include "flux_glutil.hpp"
#  include <nanovg.h>
#elif defined(__ANDROID__)
#  include <GLES2/gl2.h>
#  include <nanovg.h>
#endif

class CustomScrollbar {
public:
    static constexpr float kTrackThick  = 12.f;
    static constexpr float kThumbThin   = 5.f;
    static constexpr float kThumbFat    = 8.f;
    static constexpr float kArrowSize   = 12.f;
    static constexpr float kThumbMinLen = 24.f;
    static constexpr float kCornerR     = 3.f;
    static constexpr int   kCapSegs     = 8;
    static constexpr float kIdleAlpha   = 0.15f;
    static constexpr float kActiveAlpha = 0.90f;
    static constexpr float kIdleDelay   = 1.5f;
    static constexpr float kFadeSpeed   = 3.5f;
    static constexpr float kExpandSpeed = 10.f;

    enum class Axis { Horizontal, Vertical };
    enum class Zone { None, ArrowStart, TrackBefore, Thumb, TrackAfter, ArrowEnd };

    explicit CustomScrollbar(Axis axis) : axis_(axis) {}

    // ── NanoVG render (Windows + Linux CanvasWidget) ──────────────────────
#if defined(_WIN32) || defined(__linux__) || defined(__ANDROID__)
    void renderNVG(NVGcontext* nvg, int viewW, int viewH) {
        if (!visible_) return;

        float tx, ty, tw, th;
        if (axis_ == Axis::Horizontal) {
            tx = stripX0_;
            ty = float(viewH) - kTrackThick;
            tw = stripLen_;
            th = kTrackThick;
        } else {
            tx = float(viewW) - kTrackThick;
            ty = stripY0_;
            tw = kTrackThick;
            th = stripLen_;
        }

        // Track background
        nvgBeginPath(nvg);
        nvgRect(nvg, tx, ty, tw, th);
        nvgFillColor(nvg, nvgRGBA(20, 20, 20, 180));
        nvgFill(nvg);

        // Thumb
        float ts, tl;
        thumbPixels(ts, tl);

        float thumbX, thumbY, thumbW, thumbH;
        if (axis_ == Axis::Horizontal) {
            thumbX = stripX0_ + ts;
            thumbY = ty + 2.f;
            thumbW = tl;
            thumbH = th - 4.f;
        } else {
            thumbX = tx + 2.f;
            thumbY = stripY0_ + ts;
            thumbW = tw - 4.f;
            thumbH = tl;
        }

        auto a = static_cast<unsigned char>(
            std::clamp(alpha_ * 255.f, 0.f, 255.f));
        nvgBeginPath(nvg);
        nvgRoundedRect(nvg, thumbX, thumbY, thumbW, thumbH, kCornerR);
        nvgFillColor(nvg, nvgRGBA(180, 180, 180, a));
        nvgFill(nvg);
    }

#endif

    // ── Raw GL render (legacy path — kept for any non-NanoVG callers) ─────
#if defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__))
    void render(GLuint prog, GLuint vao, GLuint vbo,
                GLint uMVP, GLint uColor, int glW, int glH) const {
        if (alpha_ < 0.005f) return;

        float mvp[16];
        glutil::ortho(0.f, float(glW), float(glH), 0.f, mvp);

        glUseProgram(prog);
        glUniformMatrix4fv(uMVP, 1, GL_FALSE, mvp);
        glUniform1i(glGetUniformLocation(prog, "uMode"), 1);

        {
            float ta = alpha_ * 0.25f;
            glUniform4f(uColor, 0.12f, 0.12f, 0.12f, ta);
            pushRect(vao, vbo, stripX0_, stripY0_, trackStripW(), stripLen_);
        }
        {
            glUniform4f(uColor, 0.75f, 0.75f, 0.75f, alpha_);
            drawArrow(vao, vbo, true,  glW, glH, prog, uMVP, uColor);
            drawArrow(vao, vbo, false, glW, glH, prog, uMVP, uColor);
        }
        if (thumbMax_ > thumbMin_) {
            float thumbAlpha = dragging_ ? 1.f : alpha_;
            float tc         = dragging_ ? 0.92f : 0.72f;
            glUniform4f(uColor, tc, tc, tc, thumbAlpha);
            drawThumb(vao, vbo);
        }
    }
#endif // _WIN32 || Linux desktop

    // ── Geometry / thumb ──────────────────────────────────────────────────

    void setGeometry(float stripX0, float stripY0, float stripLen) {
        stripX0_  = stripX0;
        stripY0_  = stripY0;
        stripLen_ = stripLen;
    }

    void setThumb(float thumbMin, float thumbMax, bool visible) {
        visible_  = visible;
        thumbMin_ = std::clamp(thumbMin, 0.f, 1.f);
        thumbMax_ = std::clamp(thumbMax, 0.f, 1.f);
        if (thumbMin_ > thumbMax_) std::swap(thumbMin_, thumbMax_);
    }

    // ── Animation tick ────────────────────────────────────────────────────
    // Returns true if alpha is still animating (caller should schedule repaint)

    bool tick(double dt) {
        float f = float(dt);
        float targetW = (hovered_ || dragging_) ? kThumbFat : kThumbThin;
        currentW_ += (targetW - currentW_) * std::min(1.f, kExpandSpeed * f);

        if (!visible_) {
            alpha_    += (0.f - alpha_) * std::min(1.f, kFadeSpeed * f);
            idleTimer_ = 0.f;
            return std::abs(alpha_) > 0.005f;
        }

        float targetAlpha;
        if (hovered_ || dragging_) {
            targetAlpha = kActiveAlpha;
            idleTimer_  = 0.f;
        } else {
            idleTimer_ += f;
            targetAlpha = (idleTimer_ < kIdleDelay) ? kActiveAlpha : kIdleAlpha;
        }
        float prev = alpha_;
        alpha_ += (targetAlpha - alpha_) * std::min(1.f, kFadeSpeed * f);
        return std::abs(alpha_ - prev) > 0.002f;
    }

    bool needsRedraw() const { return visible_ && alpha_ > 0.005f; }
    bool isVisible()   const { return visible_; }
    void poke()              { idleTimer_ = 0.f; }

    // ── Mouse events ──────────────────────────────────────────────────────

    bool onMouseDown(int sx, int sy, std::function<void(float)> scrollTo) {
        Zone z = hitTest(sx, sy);
        if (z == Zone::None) return false;
        hovered_   = true;
        idleTimer_ = 0.f;
        scrollToFn_ = scrollTo;
        if (z == Zone::Thumb) {
            dragging_     = true;
            dragStartPx_  = axis_ == Axis::Horizontal ? float(sx) : float(sy);
            dragStartMin_ = thumbMin_;
            return true;
        }
        if (z == Zone::ArrowStart)  { scrollBy(-kArrowStep, scrollTo); return true; }
        if (z == Zone::ArrowEnd)    { scrollBy(+kArrowStep, scrollTo); return true; }
        if (z == Zone::TrackBefore) { scrollBy(-(thumbMax_ - thumbMin_), scrollTo); return true; }
        if (z == Zone::TrackAfter)  { scrollBy(+(thumbMax_ - thumbMin_), scrollTo); return true; }
        return false;
    }

    bool onMouseMove(int sx, int sy) {
        Zone z = hitTest(sx, sy);
        bool wasHover = hovered_;
        hovered_ = (z != Zone::None);
        if (hovered_ != wasHover) idleTimer_ = 0.f;
        if (dragging_ && scrollToFn_) {
            float pos    = axis_ == Axis::Horizontal ? float(sx) : float(sy);
            float delta  = pos - dragStartPx_;
            float usable = usableLen();
            float thumbPx = (thumbMax_ - thumbMin_) * usable;
            float range   = usable - thumbPx;
            if (range > 0.f) {
                float newMin = std::clamp(
                    dragStartMin_ + delta / range,
                    0.f, 1.f - (thumbMax_ - thumbMin_));
                scrollToFn_(newMin);
            }
            return true;
        }
        return false;
    }

    bool onMouseUp(int, int) {
        bool was   = dragging_;
        dragging_  = false;
        idleTimer_ = 0.f;
        return was;
    }

    void onMouseLeave() {
        hovered_  = false;
        dragging_ = false;
    }

private:
    Axis  axis_;
    float stripX0_ = 0, stripY0_ = 0, stripLen_ = 100.f;
    float thumbMin_ = 0.f, thumbMax_ = 1.f;
    bool  visible_  = false;
    bool  hovered_  = false;
    bool  dragging_ = false;
    float alpha_    = kIdleAlpha;
    float idleTimer_    = 0.f;
    float currentW_     = kThumbThin;
    float dragStartPx_  = 0.f;
    float dragStartMin_ = 0.f;
    std::function<void(float)> scrollToFn_;
    static constexpr float kArrowStep = 0.05f;

    float trackStripW() const { return kTrackThick; }
    float usableLen()   const { return std::max(1.f, stripLen_ - kArrowSize * 2.f); }

    void thumbPixels(float& pxStart, float& pxLen) const {
        float ul  = usableLen();
        pxLen     = std::max(kThumbMinLen, (thumbMax_ - thumbMin_) * ul);
        float range = ul - pxLen;
        pxStart   = kArrowSize
                  + thumbMin_ / (1.f - (thumbMax_ - thumbMin_)) * range;
        if (!std::isfinite(pxStart)) pxStart = kArrowSize;
        pxStart = std::clamp(pxStart, kArrowSize, kArrowSize + range);
    }

    Zone hitTest(int sx, int sy) const {
        if (!visible_) return Zone::None;
        float px = axis_ == Axis::Horizontal ? float(sx) : float(sy);
        float py = axis_ == Axis::Horizontal ? float(sy) : float(sx);
        float trackCross = axis_ == Axis::Horizontal ? stripY0_ : stripX0_;
        if (py < trackCross || py >= trackCross + kTrackThick) return Zone::None;
        float along = axis_ == Axis::Horizontal ? px - stripX0_ : px - stripY0_;
        if (along < 0 || along > stripLen_) return Zone::None;
        if (along < kArrowSize)               return Zone::ArrowStart;
        if (along > stripLen_ - kArrowSize)   return Zone::ArrowEnd;
        float ts, tl;
        thumbPixels(ts, tl);
        if (along < ts)      return Zone::TrackBefore;
        if (along < ts + tl) return Zone::Thumb;
        return Zone::TrackAfter;
    }

    void scrollBy(float delta, std::function<void(float)> fn) {
        if (!fn) return;
        float span = thumbMax_ - thumbMin_;
        fn(std::clamp(thumbMin_ + delta, 0.f, 1.f - span));
    }

    // ── GL helpers (desktop only) ─────────────────────────────────────────
#if defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__))
    static void pushRect(GLuint vao, GLuint vbo,
                         float x, float y, float w, float h) {
        float v[] = { x,   y,   x+w, y,   x+w, y+h,
                      x+w, y+h, x,   y+h, x,   y   };
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(v), v, GL_DYNAMIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE,
                              sizeof(float) * 2, nullptr);
        glDisableVertexAttribArray(1);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindVertexArray(0);
    }

    void drawThumb(GLuint vao, GLuint vbo) const {
        float ts, tl;
        thumbPixels(ts, tl);
        float halfW = currentW_ * 0.5f;
        float crossCenter = (axis_ == Axis::Horizontal)
            ? stripY0_ + kTrackThick * 0.5f
            : stripX0_ + kTrackThick * 0.5f;
        const float cr = std::min(kCornerR, halfW);
        float rLen = tl - cr * 2.f;
        if (rLen > 0.f) {
            float rx, ry, rw, rh;
            if (axis_ == Axis::Horizontal) {
                rx = stripX0_ + ts + cr; ry = crossCenter - halfW;
                rw = rLen;               rh = currentW_;
            } else {
                rx = crossCenter - halfW; ry = stripY0_ + ts + cr;
                rw = currentW_;           rh = rLen;
            }
            pushRect(vao, vbo, rx, ry, rw, rh);
        }
        for (int cap = 0; cap < 2; ++cap) {
            float capAlong, capCross = crossCenter;
            if (axis_ == Axis::Horizontal)
                capAlong = (cap == 0) ? (stripX0_ + ts + cr)
                                      : (stripX0_ + ts + tl - cr);
            else
                capAlong = (cap == 0) ? (stripY0_ + ts + cr)
                                      : (stripY0_ + ts + tl - cr);
            float startAngle = (axis_ == Axis::Horizontal)
                ? (cap == 0 ?  3.14159265f * 0.5f : -3.14159265f * 0.5f)
                : (cap == 0 ?  3.14159265f         :  0.f);
            float sweep = 3.14159265f;
            std::vector<float> fan;
            fan.reserve((kCapSegs + 2) * 2);
            if (axis_ == Axis::Horizontal) {
                fan.push_back(capAlong); fan.push_back(capCross);
            } else {
                fan.push_back(capCross); fan.push_back(capAlong);
            }
            for (int i = 0; i <= kCapSegs; ++i) {
                float a  = startAngle + sweep * float(i) / kCapSegs;
                float dx = cosf(a) * cr, dy = sinf(a) * cr;
                if (axis_ == Axis::Horizontal) {
                    fan.push_back(capAlong + dx); fan.push_back(capCross + dy);
                } else {
                    fan.push_back(capCross + dx); fan.push_back(capAlong + dy);
                }
            }
            glBindVertexArray(vao);
            glBindBuffer(GL_ARRAY_BUFFER, vbo);
            glBufferData(GL_ARRAY_BUFFER,
                         GLsizeiptr(fan.size() * sizeof(float)),
                         fan.data(), GL_DYNAMIC_DRAW);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE,
                                  sizeof(float) * 2, nullptr);
            glDisableVertexAttribArray(1);
            glDrawArrays(GL_TRIANGLE_FAN, 0, int(fan.size() / 2));
            glBindVertexArray(0);
        }
    }

    void drawArrow(GLuint vao, GLuint vbo, bool isStart,
                   int, int, GLuint, GLint, GLint) const {
        float ax, ay;
        if (axis_ == Axis::Horizontal) {
            ax = isStart ? stripX0_ : stripX0_ + stripLen_ - kArrowSize;
            ay = stripY0_;
        } else {
            ax = stripX0_;
            ay = isStart ? stripY0_ : stripY0_ + stripLen_ - kArrowSize;
        }
        pushRect(vao, vbo, ax, ay, kArrowSize,
                 axis_ == Axis::Horizontal ? kTrackThick : kArrowSize);
        float cx = ax + kArrowSize * 0.5f;
        float cy = (axis_ == Axis::Vertical)
                 ? ay + kArrowSize  * 0.5f
                 : ay + kTrackThick * 0.5f;
        float hs = 3.f;
        float v[6];
        if (axis_ == Axis::Horizontal) {
            if (isStart) {
                v[0]=cx+hs; v[1]=cy-hs; v[2]=cx-hs;
                v[3]=cy;    v[4]=cx+hs; v[5]=cy+hs;
            } else {
                v[0]=cx-hs; v[1]=cy-hs; v[2]=cx+hs;
                v[3]=cy;    v[4]=cx-hs; v[5]=cy+hs;
            }
        } else {
            if (isStart) {
                v[0]=cx-hs; v[1]=cy+hs; v[2]=cx;
                v[3]=cy-hs; v[4]=cx+hs; v[5]=cy+hs;
            } else {
                v[0]=cx-hs; v[1]=cy-hs; v[2]=cx;
                v[3]=cy+hs; v[4]=cx+hs; v[5]=cy-hs;
            }
        }
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(v), v, GL_DYNAMIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE,
                              sizeof(float) * 2, nullptr);
        glDisableVertexAttribArray(1);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindVertexArray(0);
    }
#endif // desktop GL
};