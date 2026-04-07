// audio_player_widget.hpp
// Drop-in, self-contained audio player widget that mirrors the browser <audio>
// control aesthetic. All playback state is internal — app.cpp just instantiates it.
//
// Usage:
//   AudioPlayer("audio/sample.mp3")
//       ->setWidth(380)
//
#pragma once


#include "flux/flux.hpp"
#include "flux/flux_audio.hpp"


// ============================================================================
// Helpers
// ============================================================================

static std::string AP_formatTime(float secs) {
    if (secs < 0.f) secs = 0.f;
    int s = (int)secs;
    char buf[16];
    snprintf(buf, sizeof(buf), "%d:%02d", s / 60, s % 60);
    return buf;
}

// ============================================================================
// AudioPlayerWidget
// ============================================================================

class AudioPlayerWidget : public Widget {
public:
    // ── Colours ──────────────────────────────────────────────────────────────
    Color colBackground   = Color::fromRGB(240, 240, 240);
    Color colTrackBg      = Color::fromRGB(180, 180, 180);
    Color colTrackFill    = Color::fromRGB(90,  90,  90);
    Color colThumb        = Color::fromRGB(90,  90,  90);
    Color colThumbHover   = Color::fromRGB(50,  50,  50);
    Color colText         = Color::fromRGB(60,  60,  60);
    Color colIconNormal   = Color::fromRGB(60,  60,  60);
    Color colIconHover    = Color::fromRGB(20,  20,  20);
    Color colBorder       = Color::fromRGB(210, 210, 210);

    // ── Config ────────────────────────────────────────────────────────────────
    std::string audioPath;
    int   playerHeight   = 40;
    int   pillarRadius   = 20;   // full pill
    int   trackHeight    = 3;
    int   thumbRadius    = 6;
    int   playBtnSize    = 28;
    int   iconFontSize   = 13;
    int   timeFontSize   = 12;
    int   volSliderW     = 0;    // 0 = hidden; set >0 to show inline volume

    // ── Public fluent setters ─────────────────────────────────────────────────
    std::shared_ptr<AudioPlayerWidget> setPath(const std::string& p) {
        audioPath = p;
        return self();
    }
    std::shared_ptr<AudioPlayerWidget> setWidth(int w) {
        Widget::width = w;
        autoWidth = false;
        return self();
    }

    // ── Constructor ───────────────────────────────────────────────────────────
    AudioPlayerWidget() {
        height    = playerHeight;
        autoHeight = false;
        isFocusable = false;

        // finish callback — fired from AAudio thread, just flips atomics
        FluxAudio::get().setOnFinished([this]() {
            _playing  = false;
            _finished = true;
            _progress = 1.f;
        });
    }

    ~AudioPlayerWidget() {
        _stopTimer();
        FluxAudio::get().setOnFinished(nullptr);
        FluxAudio::get().closePlayback();
    }

    // =========================================================================
    // Layout
    // =========================================================================

    void computeLayout(GraphicsContext& /*ctx*/,
                       const BoxConstraints& constraints,
                       FontCache& /*fontCache*/) override {
        if (autoWidth) width = constraints.maxWidth;
        width  = constraints.clampWidth(width);
        height = playerHeight;
        applyConstraints();
        needsLayout = false;
    }

    // =========================================================================
    // Render — entirely custom, no children
    // =========================================================================

    void render(GraphicsContext& ctx, FontCache& fontCache) override {
        // Sync progress from audio engine every frame (cheap atomic)
        auto& audio = FluxAudio::get();
        if (_playing) {
            _progress = audio.getProgress();
            if (!audio.isPlaying() && !audio.isPaused()) {
                _playing  = false;
                _finished = (audio.getProgress() >= 0.999f);
            }
        }

        Painter p(ctx);

        // ── Pill background ──────────────────────────────────────────────────
        p.fillRoundedRectGDI(x, y, width, height, pillarRadius * 2,
                             colBackground, colBorder, 1);

        int cx = x;   // running left cursor
        int midY = y + height / 2;

        // ── Play / Pause button ──────────────────────────────────────────────
        cx += 6;
        int btnX = cx;
        int btnY = y + (height - playBtnSize) / 2;
        _playBtnRect = {btnX, btnY, playBtnSize, playBtnSize};

        Color btnBg = _hovPlay
                      ? Color::fromRGB(210, 210, 210)
                      : colBackground;
        p.fillRoundedRectGDI(btnX, btnY, playBtnSize, playBtnSize,
                             playBtnSize, btnBg, btnBg, 0);

        // Draw triangle (play) or two bars (pause)
        NativeFont iconFont = fontCache.getFont("Segoe UI", iconFontSize, FontWeight::Normal);
        Color iconCol = _hovPlay ? colIconHover : colIconNormal;

        if (_playing) {
            // Pause: two vertical bars
            int bw = 3, bh = 10, gap = 3;
            int bx = btnX + (playBtnSize - bw*2 - gap) / 2;
            int by = btnY + (playBtnSize - bh) / 2;
            p.fillRect(bx,       by, bw, bh, iconCol);
            p.fillRect(bx+bw+gap, by, bw, bh, iconCol);
        } else {
            // Play: filled triangle via three lines approach — use fillRect trick
            // Draw a right-pointing triangle with decreasing rows
            int tx = btnX + (playBtnSize - 10) / 2 + 1;
            int ty = btnY + (playBtnSize - 14) / 2;
            for (int row = 0; row < 14; row++) {
                int half = row < 7 ? row : 13 - row;
                p.fillRect(tx + row, ty + 7 - half, 1, half * 2 + 1, iconCol);
            }
        }

        cx += playBtnSize + 6;

        // ── Time: current ────────────────────────────────────────────────────
        float dur = audio.getDurationSeconds();
        float pos = _progress * dur;
        std::string timeStr = AP_formatTime(pos) + " / " + AP_formatTime(dur);

        NativeFont timeFont = fontCache.getFont("Segoe UI", timeFontSize, FontWeight::Normal);
        int tw = 0, th = 0;
        p.measureText(toWideString(timeStr), timeFont, tw, th);

        p.drawText(toWideString(timeStr),
                   cx, y, tw + 4, height, timeFont, colText,
                   DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        cx += tw + 8;

        // ── Seek track ───────────────────────────────────────────────────────
        // Right edge: leave room for volume icon + dots icon
        int rightReserve = 48;
        int trackLeft  = cx;
        int trackRight = x + width - rightReserve;
        int trackW     = trackRight - trackLeft;
        if (trackW < 20) trackW = 20;

        _trackRect = {trackLeft, midY - 8, trackW, 16};  // hit area

        // Track background
        p.fillRoundedRectGDI(trackLeft, midY - trackHeight/2,
                             trackW, trackHeight,
                             trackHeight, colTrackBg, colTrackBg, 0);

        // Fill
        int fillW = (int)(_progress * trackW);
        if (fillW > 0) {
            p.fillRoundedRectGDI(trackLeft, midY - trackHeight/2,
                                 fillW, trackHeight,
                                 trackHeight, colTrackFill, colTrackFill, 0);
        }

        // Thumb
        int thumbX = trackLeft + fillW;
        Color thumbCol = _hovTrack ? colThumbHover : colThumb;
        p.drawEllipse(thumbX - thumbRadius, midY - thumbRadius,
                      thumbRadius * 2, thumbRadius * 2,
                      thumbCol, thumbCol, 0);

        cx = trackRight + 4;

        // ── Volume icon ───────────────────────────────────────────────────────
        _volIconRect = {cx, y + (height - 20) / 2, 20, 20};
        Color volCol = _hovVol ? colIconHover : colIconNormal;

        // Speaker glyph drawn with rects/lines
        {
            int sx = cx + 3, sy = midY - 5;
            // Body
            p.fillRect(sx,     sy,     4, 10, volCol);
            // Cone
            p.fillRect(sx + 4, sy - 2, 1, 2,  volCol);
            p.fillRect(sx + 5, sy - 4, 1, 2,  volCol);
            p.fillRect(sx + 6, sy - 6, 1, 2,  volCol);
            p.fillRect(sx + 4, sy+10, 1, 2,   volCol);
            p.fillRect(sx + 5, sy+12, 1, 2,   volCol);
            p.fillRect(sx + 6, sy+14, 1, 2,   volCol);
            // Right side connector
            p.fillRect(sx + 4, sy,    3, 10, volCol);
        }

        cx += 24;

        // ── ⋮ dots icon ───────────────────────────────────────────────────────
        _dotsIconRect = {cx, y + (height - 20) / 2, 18, 20};
        Color dotsCol = _hovDots ? colIconHover : colIconNormal;
        for (int i = 0; i < 3; i++) {
            p.fillRoundedRectGDI(cx + 7, midY - 6 + i * 6, 3, 3, 3,
                                 dotsCol, dotsCol, 0);
        }

        needsPaint = false;
    }

    // =========================================================================
    // Mouse events
    // =========================================================================

    bool handleMouseDown(int mx, int my) override {
        // Play / Pause
        if (_inRect(mx, my, _playBtnRect)) {
            _togglePlayPause();
            markNeedsPaint();
            return true;
        }
        // Seek track — start drag
        if (_inRect(mx, my, _trackRect)) {
            _dragging = true;
            FluxUI::getCurrentInstance()->captureMouseInput();
            _seekFromMouse(mx);
            markNeedsPaint();
            return true;
        }
        return false;
    }

    bool handleMouseUp(int mx, int my) override {
        if (_dragging) {
            _dragging = false;
            FluxUI::getCurrentInstance()->releaseMouseInput();
            markNeedsPaint();
            return true;
        }
        return false;
    }

    bool handleMouseMove(int mx, int my) override {
        bool changed = false;

        if (_dragging) {
            _seekFromMouse(mx);
            return true;
        }

        bool hp = _inRect(mx, my, _playBtnRect);
        bool ht = _inRect(mx, my, _trackRect);
        bool hv = _inRect(mx, my, _volIconRect);
        bool hd = _inRect(mx, my, _dotsIconRect);

        if (hp != _hovPlay)  { _hovPlay  = hp; changed = true; }
        if (ht != _hovTrack) { _hovTrack = ht; changed = true; }
        if (hv != _hovVol)   { _hovVol   = hv; changed = true; }
        if (hd != _hovDots)  { _hovDots  = hd; changed = true; }

        if (changed) markNeedsPaint();
        return changed;
    }

    bool handleMouseLeave() override {
        _hovPlay = _hovTrack = _hovVol = _hovDots = false;
        markNeedsPaint();
        return true;
    }

private:
    // ── Playback state ────────────────────────────────────────────────────────
    std::atomic<bool>  _playing  {false};
    std::atomic<bool>  _finished {false};
    std::atomic<float> _progress {0.f};

    // ── Interaction state ─────────────────────────────────────────────────────
    bool _dragging  = false;
    bool _hovPlay   = false;
    bool _hovTrack  = false;
    bool _hovVol    = false;
    bool _hovDots   = false;

    // ── Hit rects (set each frame in render) ──────────────────────────────────
    struct Rect { int x, y, w, h; };
    Rect _playBtnRect  {};
    Rect _trackRect    {};
    Rect _volIconRect  {};
    Rect _dotsIconRect {};

    // ── Timer ─────────────────────────────────────────────────────────────────
    TimerID _timerId = 0;

    void _startTimer() {
        if (_timerId) return;
        _timerId = FluxUI::getCurrentInstance()->setInterval(33, [this]() {
            if (_playing) markNeedsPaint();
        });
    }
    void _stopTimer() {
        if (_timerId) {
            FluxUI::getCurrentInstance()->clearInterval(_timerId);
            _timerId = 0;
        }
    }

    // ── Helpers ───────────────────────────────────────────────────────────────
    std::shared_ptr<AudioPlayerWidget> self() {
        return std::static_pointer_cast<AudioPlayerWidget>(shared_from_this());
    }
    static bool _inRect(int mx, int my, const Rect& r) {
        return mx >= r.x && mx < r.x + r.w &&
               my >= r.y && my < r.y + r.h;
    }

    void _togglePlayPause() {
        auto& audio = FluxAudio::get();

        if (_finished.load()) {
            // Replay
            _finished = false;
            _progress = 0.f;
            audio.seekToProgress(0.f);
            audio.resume();
            _playing = true;
            _startTimer();
            return;
        }

        if (_playing.load()) {
            audio.pause();
            _playing = false;
            _stopTimer();
        } else if (audio.isPaused()) {
            audio.resume();
            _playing = true;
            _startTimer();
        } else {
            // First play — load file
            if (!audioPath.empty()) {
                audio.playFromPath(audioPath);
                _playing = true;
                _startTimer();
            }
        }
    }

    void _seekFromMouse(int mx) {
        if (_trackRect.w <= 0) return;
        float t = (float)(mx - _trackRect.x) / (float)_trackRect.w;
        t = std::max(0.f, std::min(1.f, t));
        _progress = t;
        FluxAudio::get().seekToProgress(t);

        // If finished and user seeks back, auto-resume
        if (_finished.load() && t < 0.999f) {
            _finished = false;
            FluxAudio::get().resume();
            _playing = true;
            _startTimer();
        }
        markNeedsPaint();
    }
};

using AudioPlayerWidgetPtr = std::shared_ptr<AudioPlayerWidget>;

// ── Factory ───────────────────────────────────────────────────────────────────
inline AudioPlayerWidgetPtr AudioPlayer(const std::string& path = "") {
    auto w = std::make_shared<AudioPlayerWidget>();
    if (!path.empty()) w->setPath(path);
    return w;
}

