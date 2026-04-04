// video_player_widget.hpp
// Self-contained video player widget.
// Blits the FluxVideo OES texture via NanoVG each frame, overlays a browser-
// style control bar at the bottom (identical visual language to AudioPlayerWidget).
//
// Usage:
//   VideoPlayer("video/sample.mp4")
//       ->setWidth(480)->setHeight(270)   // 16:9 recommended
//
// The widget manages all FluxVideo state internally — app.cpp just instantiates it.
//
#pragma once

#include "flux/flux.hpp"
#include "flux/flux_video.hpp"

// ── nanovg OES extension ──────────────────────────────────────────────────────
// NanoVG does not natively support GL_TEXTURE_EXTERNAL_OES.
// We supply a thin helper that wraps the OES texture as an NVGimage handle
// so nvgImagePattern() / nvgFillPaint() can blit it.
// The implementation lives in nanovg_oes_ext.cpp (see below).
extern int  NVG_createImageFromOES(NVGcontext* vg, GLuint oesTexId, int w, int h);
extern void NVG_updateImageFromOES(NVGcontext* vg, int nvgImage, GLuint oesTexId);
extern NVGcontext* FluxAndroid_getVG();

// ============================================================================
// VideoPlayerWidget
// ============================================================================

class VideoPlayerWidget : public Widget {
public:
    // ── Config ────────────────────────────────────────────────────────────────
    std::string videoPath;
    int   barHeight  = 40;
    int   pillarR    = 0;    // 0 = sharp corners on the video frame
    bool  autoPlay   = false;

    // ── Colors (same palette as AudioPlayerWidget) ────────────────────────────
    Color colBar       = Color::fromRGBA( 20,  20,  20,220);   // dark translucent
    Color colTrackBg   = Color::fromRGB(100, 100, 100);
    Color colTrackFill = Color::fromRGB(220, 220, 220);
    Color colThumb     = Color::fromRGB(255, 255, 255);
    Color colText      = Color::fromRGB(230, 230, 230);
    Color colIcon      = Color::fromRGB(220, 220, 220);
    Color colIconHov   = Color::fromRGB(255, 255, 255);
    Color colOverlay   = Color::fromRGBA( 0, 0, 0,60);   // dim when bar visible

    // ── Fluent setters ────────────────────────────────────────────────────────
    std::shared_ptr<VideoPlayerWidget> setPath(const std::string& p) {
        videoPath = p; return self();
    }
    std::shared_ptr<VideoPlayerWidget> setWidth(int w) {
        Widget::width = w; autoWidth = false; return self();
    }
    std::shared_ptr<VideoPlayerWidget> setHeight(int h) {
        Widget::height = h; autoHeight = false; return self();
    }
    std::shared_ptr<VideoPlayerWidget> setAutoPlay(bool b) {
        autoPlay = b; return self();
    }

    // ── Constructor / destructor ───────────────────────────────────────────────
    VideoPlayerWidget() {
        autoWidth  = false;
        autoHeight = false;
        width  = 320;
        height = 180;

        auto& vid = FluxVideo::get();

        vid.setOnFinished([this]() {
            _playing  = false;
            _finished = true;
            _progress = 1.f;
            markNeedsPaint();
        });

        vid.setOnReady([this](int w, int h) {
            _vidW = w;
            _vidH = h;
            _buildNVGImage();
        });
    }

    ~VideoPlayerWidget() {
        _stopTimer();
        FluxVideo::get().close();
        if (_nvgImage >= 0 && FluxAndroid_getVG())
            nvgDeleteImage(FluxAndroid_getVG(), _nvgImage);
    }

    // =========================================================================
    // Layout
    // =========================================================================

    void computeLayout(GraphicsContext& /*ctx*/,
                       const BoxConstraints& constraints,
                       FontCache& /*fontCache*/) override {
        if (autoWidth)  width  = constraints.maxWidth;
        if (autoHeight) height = constraints.maxHeight;
        applyConstraints();
        needsLayout = false;

        // Open video on first layout (we have a valid context by then)
        if (!_opened && !videoPath.empty()) {
            _opened = true;
            _openVideo();
        }
    }

    // =========================================================================
    // Render
    // =========================================================================

    void render(GraphicsContext& ctx, FontCache& fontCache) override {
        auto& vid = FluxVideo::get();
        NVGcontext* vg = FluxAndroid_getVG();

        // ── Latch new frame into OES texture ──────────────────────────────────
        if (vid.updateFrame() && _nvgImage >= 0) {
            NVG_updateImageFromOES(vg, _nvgImage, vid.getTextureId());
            _progress = vid.getProgress();
        }

        Painter p(ctx);

        // ── Video frame ───────────────────────────────────────────────────────
        if (_nvgImage >= 0 && _vidW > 0 && _vidH > 0) {
            // Fit video into widget rect, letterbox if needed
            float vidAR   = (float)_vidW / _vidH;
            float widAR   = (float)width  / (height - barHeight);
            float dstW, dstH, dstX, dstY;

            if (vidAR > widAR) {
                dstW = (float)width;
                dstH = dstW / vidAR;
                dstX = (float)x;
                dstY = (float)y + (height - barHeight - dstH) * 0.5f;
            } else {
                dstH = (float)(height - barHeight);
                dstW = dstH * vidAR;
                dstX = (float)x + ((float)width - dstW) * 0.5f;
                dstY = (float)y;
            }

            NVGpaint imgPaint = nvgImagePattern(vg, dstX, dstY, dstW, dstH, 0.f, _nvgImage, 1.f);
            nvgBeginPath(vg);
            nvgRect(vg, dstX, dstY, dstW, dstH);
            nvgFillPaint(vg, imgPaint);
            nvgFill(vg);

            // Dim overlay when bar is showing
            if (_barVisible) {
                p.fillRect(x, y, width, height - barHeight, colOverlay);
            }
        } else {
            // Placeholder before video loads
            p.fillRect(x, y, width, height, Color::fromRGB(20, 20, 20));
        }

        // ── Show/hide bar ─────────────────────────────────────────────────────
        if (_barVisible) _renderBar(ctx, fontCache, p);

        // ── Center play button overlay (large, shown when paused/not started) ──
        if (!_playing && _barVisible) {
            _renderCenterPlay(p);
        }

        needsPaint = false;
    }

    // =========================================================================
    // Mouse events
    // =========================================================================

    bool handleMouseDown(int mx, int my) override {
        if (!_inWidget(mx, my)) return false;

        // Tap video area → show bar / toggle play
        if (!_inRect(mx, my, _barRect)) {
            _barVisible = true;
            _resetBarTimer();
            _togglePlayPause();
            markNeedsPaint();
            return true;
        }

        // Play button
        if (_inRect(mx, my, _playBtnRect)) {
            _togglePlayPause();
            markNeedsPaint();
            return true;
        }

        // Seek track
        if (_inRect(mx, my, _trackRect)) {
            _dragging = true;
            FluxUI::getCurrentInstance()->captureMouseInput();
            _seekFromMouse(mx);
            markNeedsPaint();
            return true;
        }

        return true;  // consume all clicks inside widget
    }

    bool handleMouseUp(int /*mx*/, int /*my*/) override {
        if (_dragging) {
            _dragging = false;
            FluxUI::getCurrentInstance()->releaseMouseInput();
            markNeedsPaint();
        }
        return false;
    }

    bool handleMouseMove(int mx, int my) override {
        if (_dragging) { _seekFromMouse(mx); return true; }

        bool inW = _inWidget(mx, my);
        if (inW != _barVisible) {
            _barVisible = inW;
            markNeedsPaint();
        }
        if (inW) _resetBarTimer();

        bool hp = _inRect(mx, my, _playBtnRect);
        if (hp != _hovPlay) { _hovPlay = hp; markNeedsPaint(); }
        return false;
    }

    bool handleMouseLeave() override {
        _barVisible = false;
        _hovPlay    = false;
        markNeedsPaint();
        return true;
    }

private:
    // ── State ─────────────────────────────────────────────────────────────────
    bool  _opened   = false;
    bool  _playing  = false;
    bool  _finished = false;
    float _progress = 0.f;
    int   _vidW     = 0;
    int   _vidH     = 0;
    int   _nvgImage = -1;
    bool  _dragging = false;
    bool  _barVisible = true;
    bool  _hovPlay  = false;

    // ── Hit rects ─────────────────────────────────────────────────────────────
    struct Rect { int x, y, w, h; };
    Rect _barRect     {};
    Rect _playBtnRect {};
    Rect _trackRect   {};

    // ── Timers ────────────────────────────────────────────────────────────────
    TimerID _progressTimer = 0;  // 33ms — update progress while playing
    TimerID _barHideTimer  = 0;  // 3s   — auto-hide bar after inactivity

    void _startProgressTimer() {
        if (_progressTimer) return;
        _progressTimer = FluxUI::getCurrentInstance()->setInterval(33, [this]() {
            if (_playing) {
                _progress = FluxVideo::get().getProgress();
                markNeedsPaint();
            }
        });
    }

    void _stopTimer() {
        auto* ui = FluxUI::getCurrentInstance();
        if (!ui) return;
        if (_progressTimer) { ui->clearInterval(_progressTimer); _progressTimer = 0; }
        if (_barHideTimer)  { ui->clearInterval(_barHideTimer);  _barHideTimer  = 0; }
    }

    void _resetBarTimer() {
        auto* ui = FluxUI::getCurrentInstance();
        if (!ui) return;
        if (_barHideTimer) { ui->clearInterval(_barHideTimer); _barHideTimer = 0; }
        // Simulate setTimeout: setInterval fires every 3000ms; we cancel after
        // the first tick, making it a one-shot delay.
        _barHideTimer = ui->setInterval(3000, [this]() {
            auto* u = FluxUI::getCurrentInstance();
            if (u && _barHideTimer) { u->clearInterval(_barHideTimer); _barHideTimer = 0; }
            if (_playing) { _barVisible = false; markNeedsPaint(); }
        });
    }

    // ── Helpers ───────────────────────────────────────────────────────────────
    std::shared_ptr<VideoPlayerWidget> self() {
        return std::static_pointer_cast<VideoPlayerWidget>(shared_from_this());
    }
    bool _inWidget(int mx, int my) const {
        return mx >= x && mx < x+width && my >= y && my < y+height;
    }
    static bool _inRect(int mx, int my, const Rect& r) {
        return mx >= r.x && mx < r.x+r.w && my >= r.y && my < r.y+r.h;
    }

    // ── Open video ────────────────────────────────────────────────────────────
    void _openVideo() {
        FluxVideo::get().open(videoPath);
        if (autoPlay) {
            FluxVideo::get().play();
            _playing = true;
            _startProgressTimer();
        }
    }

    // ── NVG image (OES wrapper) ───────────────────────────────────────────────
    void _buildNVGImage() {
        NVGcontext* vg = FluxAndroid_getVG();
        if (!vg || _vidW <= 0) return;
        if (_nvgImage >= 0) nvgDeleteImage(vg, _nvgImage);
        _nvgImage = NVG_createImageFromOES(vg, FluxVideo::get().getTextureId(), _vidW, _vidH);
        VIDEO_LOGI("NVG OES image created: id=%d %dx%d", _nvgImage, _vidW, _vidH);
    }

    // ── Play / Pause toggle ───────────────────────────────────────────────────
    void _togglePlayPause() {
        auto& vid = FluxVideo::get();

        if (_finished) {
            _finished = false;
            _progress = 0.f;
            vid.seekToProgress(0.f);
            vid.play();
            _playing = true;
            _startProgressTimer();
            return;
        }

        if (_playing) {
            vid.pause();
            _playing = false;
        } else {
            vid.play();
            _playing = true;
            _startProgressTimer();
        }
    }

    // ── Seek ──────────────────────────────────────────────────────────────────
    void _seekFromMouse(int mx) {
        if (_trackRect.w <= 0) return;
        float t = (float)(mx - _trackRect.x) / (float)_trackRect.w;
        t = std::max(0.f, std::min(1.f, t));
        _progress = t;
        FluxVideo::get().seekToProgress(t);

        if (_finished && t < 0.999f) {
            _finished = false;
            FluxVideo::get().play();
            _playing = true;
            _startProgressTimer();
        }
        markNeedsPaint();
    }

    static std::string _fmtTime(float s) {
        int si = (int)s;
        char buf[16];
        snprintf(buf, sizeof(buf), "%d:%02d", si / 60, si % 60);
        return buf;
    }

    // ── Render control bar ────────────────────────────────────────────────────
    void _renderBar(GraphicsContext& ctx, FontCache& fontCache, Painter& p) {
        int barY = y + height - barHeight;
        _barRect = {x, barY, width, barHeight};

        // Dark translucent background
        p.fillRect(x, barY, width, barHeight, colBar);

        int midY = barY + barHeight / 2;
        int cx   = x + 6;

        // ── Play / Pause button ───────────────────────────────────────────────
        int btnSz = 28;
        _playBtnRect = {cx, barY + (barHeight - btnSz) / 2, btnSz, btnSz};
        Color iconCol = _hovPlay ? colIconHov : colIcon;

        if (_playing) {
            int bw = 3, bh = 10, gap = 3;
            int bx = _playBtnRect.x + (btnSz - bw*2 - gap) / 2;
            int by = _playBtnRect.y + (btnSz - bh) / 2;
            p.fillRect(bx,       by, bw, bh, iconCol);
            p.fillRect(bx+bw+gap, by, bw, bh, iconCol);
        } else {
            int tx = _playBtnRect.x + (btnSz - 10) / 2 + 1;
            int ty = _playBtnRect.y + (btnSz - 14) / 2;
            for (int row = 0; row < 14; row++) {
                int half = row < 7 ? row : 13 - row;
                p.fillRect(tx + row, ty + 7 - half, 1, half*2 + 1, iconCol);
            }
        }
        cx += btnSz + 6;

        // ── Time display ──────────────────────────────────────────────────────
        float dur = FluxVideo::get().getDurationSeconds();
        float pos = _progress * dur;
        std::string timeStr = _fmtTime(pos) + " / " + _fmtTime(dur);

        NativeFont tf = fontCache.getFont("Segoe UI", 12, FontWeight::Normal);
        int tw = 0, th = 0;
        Painter(ctx).measureText(toWideString(timeStr), tf, tw, th);
        p.drawText(toWideString(timeStr), cx, barY, tw + 4, barHeight, tf, colText,
                   DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        cx += tw + 8;

        // ── Seek track ────────────────────────────────────────────────────────
        int rightReserve = 12;
        int trackLeft  = cx;
        int trackRight = x + width - rightReserve;
        int trackW     = std::max(20, trackRight - trackLeft);
        int trackH     = 3;
        _trackRect = {trackLeft, midY - 8, trackW, 16};

        p.fillRoundedRectGDI(trackLeft, midY - trackH/2, trackW, trackH,
                             trackH, colTrackBg, colTrackBg, 0);

        int fillW = (int)(_progress * trackW);
        if (fillW > 0)
            p.fillRoundedRectGDI(trackLeft, midY - trackH/2, fillW, trackH,
                                 trackH, colTrackFill, colTrackFill, 0);

        // Thumb
        int thumbR = 6;
        int thumbX = trackLeft + fillW;
        p.drawEllipse(thumbX - thumbR, midY - thumbR,
                      thumbR*2, thumbR*2, colThumb, colThumb, 0);
    }

    // ── Center play icon overlay ───────────────────────────────────────────────
    void _renderCenterPlay(Painter& p) {
        int cx = x + width  / 2;
        int cy = y + (height - barHeight) / 2;
        int r  = 28;

        // Circle background
        p.drawEllipse(cx - r, cy - r, r*2, r*2,
                      Color::fromRGBA( 0, 0, 0,160),
                      Color::fromRGBA(0, 0, 0, 0), 0);

        // Play triangle
        Color white = Color::fromRGB(255, 255, 255);
        int tx = cx - 7 + 2;
        int ty = cy - 10;
        for (int row = 0; row < 20; row++) {
            int half = row < 10 ? row : 19 - row;
            p.fillRect(tx + row, ty + 10 - half, 1, half*2 + 1, white);
        }
    }
};

using VideoPlayerWidgetPtr = std::shared_ptr<VideoPlayerWidget>;

// ── Factory ───────────────────────────────────────────────────────────────────
inline VideoPlayerWidgetPtr VideoPlayer(const std::string& path = "") {
    auto w = std::make_shared<VideoPlayerWidget>();
    if (!path.empty()) w->setPath(path);
    return w;
}