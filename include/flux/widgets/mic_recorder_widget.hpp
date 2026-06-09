// mic_recorder_widget.hpp
// Microphone recorder widget for FluxUI.
//
// Shows a scrolling waveform while recording, plus a record/stop button.
// On stop, saves a timestamped WAV file via FluxMic and fires onSaved.
//
// Supported platforms: Android, Windows, Linux desktop, macOS
//
// Usage:
//   MicRecorder()
//       ->setWidth(320)->setHeight(120)
//       ->setOnSaved([](const std::string& path) {
//           LOGI("Saved to %s", path.c_str());
//       })
//
#pragma once

// Compile on Android, Windows, Linux desktop, and macOS — everywhere FluxMic runs.
#if defined(__ANDROID__) || defined(_WIN32) || \
    (defined(__linux__) && !defined(__ANDROID__)) || \
    (defined(__APPLE__) && defined(TARGET_OS_OSX) && TARGET_OS_OSX)

#include "flux/flux.hpp"
#include "flux/flux_mic.hpp"

// ============================================================================
// Platform helpers
// ============================================================================

// UI font — each platform ships different system fonts.
#if defined(_WIN32)
    static constexpr const char* kMicUIFont = "Segoe UI";
#elif defined(__ANDROID__)
    static constexpr const char* kMicUIFont = "Roboto";
#elif defined(__APPLE__)
    // San Francisco is the system UI font on macOS 10.11+.
    // ".AppleSystemUIFont" is the canonical internal name that always resolves
    // to SF Pro regardless of OS version; "SF Pro Display" and "Helvetica Neue"
    // are reasonable fallbacks for older SDKs.
    static constexpr const char* kMicUIFont = ".AppleSystemUIFont";
#else  // Linux
    static constexpr const char* kMicUIFont = "DejaVu Sans";
#endif

// Extract filename from a full path on any platform.
// Handles both '/' (POSIX) and '\\' (Windows) separators.
static inline std::string _micBasename(const std::string& path) {
    size_t slash = path.find_last_of("/\\");
    return (slash != std::string::npos) ? path.substr(slash + 1) : path;
}

// ============================================================================
// MicRecorderWidget
// ============================================================================

class MicRecorderWidget : public Widget {
public:
    // ── Config ────────────────────────────────────────────────────────────────
    int  barHeight   = 48;  // bottom control bar height
    int  waveColumns = 80;  // number of waveform bars drawn

    // ── Colors ────────────────────────────────────────────────────────────────
    Color colBackground  = Color::fromRGB( 18,  18,  18);
    Color colWaveIdle    = Color::fromRGB( 60,  60,  60);
    Color colWaveActive  = Color::fromRGB( 80, 200, 120);  // green when recording
    Color colWavePeak    = Color::fromRGB(255, 100,  80);  // red when near peak
    Color colBar         = Color::fromRGB( 28,  28,  28);
    Color colText        = Color::fromRGB(200, 200, 200);
    Color colTimecode    = Color::fromRGB(160, 160, 160);

    // Record button states
    Color colBtnRecord   = Color::fromRGB(220,  60,  60);  // red circle  = record
    Color colBtnStop     = Color::fromRGB(220, 100,  60);  // orange square = stop
    Color colBtnHover    = Color::fromRGB(255, 255, 255);

    // ── Fluent setters ────────────────────────────────────────────────────────
    std::shared_ptr<MicRecorderWidget> setWidth(int w) {
        Widget::width = w; autoWidth = false; return self();
    }
    std::shared_ptr<MicRecorderWidget> setHeight(int h) {
        Widget::height = h; autoHeight = false; return self();
    }
    std::shared_ptr<MicRecorderWidget> setOnSaved(
            std::function<void(const std::string&)> cb) {
        _onSaved = std::move(cb); return self();
    }

    // ── Constructor / destructor ───────────────────────────────────────────────
    MicRecorderWidget() {
        autoWidth  = false;
        autoHeight = false;
        width  = 320;
        height = 120;

        FluxMic::get().setOnSaved([this](const std::string& path) {
            _lastPath = path;
            if (_onSaved) _onSaved(path);
            markNeedsPaint();
        });
    }

    ~MicRecorderWidget() {
        _stopTimer();
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
    }

    // =========================================================================
    // Render
    // =========================================================================

    void render(GraphicsContext& ctx, FontCache& fontCache) override {
        Painter p(ctx);
        auto& mic = FluxMic::get();

        // ── Background ────────────────────────────────────────────────────────
        p.fillRect(x, y, width, height, colBackground);

        // ── Waveform area ─────────────────────────────────────────────────────
        int waveH = height - barHeight;

        mic.getWaveform(_waveform, (size_t)waveColumns);

        bool recording = mic.isRecording();
        int  colW = std::max(1, (width - 16) / waveColumns);
        int  gap  = std::max(0, colW - 1);

        for (int i = 0; i < waveColumns; i++) {
            float amp = (i < (int)_waveform.size()) ? _waveform[i] : 0.f;

            int barH = std::max(2, (int)(amp * (waveH - 8) * 2.5f));
            barH = std::min(barH, waveH - 4);

            int bx = x + 8 + i * (colW + gap);
            int by = y + waveH / 2 - barH / 2;

            Color col = colWaveIdle;
            if (recording)
                col = (amp > 0.8f) ? colWavePeak : colWaveActive;

            p.fillRect(bx, by, colW, barH, col);
        }

        // ── Control bar ───────────────────────────────────────────────────────
        int barY = y + waveH;
        p.fillRect(x, barY, width, barHeight, colBar);

        int midY = barY + barHeight / 2;

        // ── Record / Stop button ──────────────────────────────────────────────
        int btnR  = 14;
        int btnCx = x + 20;
        int btnCy = midY;
        _btnRect = {btnCx - btnR, btnCy - btnR, btnR * 2, btnR * 2};

        if (recording) {
            // Orange stop square
            Color bc = _hovBtn ? colBtnHover : colBtnStop;
            p.fillRect(btnCx - 8, btnCy - 8, 16, 16, bc);
        } else {
            // Red record circle
            Color bc = _hovBtn ? colBtnHover : colBtnRecord;
            p.drawEllipse(btnCx - btnR, btnCy - btnR,
                          btnR * 2, btnR * 2, bc, bc, 0);
        }

        // ── Timecode ──────────────────────────────────────────────────────────
        float elapsed = mic.getElapsedSeconds();
        std::string timeStr = _fmtTime(elapsed);

        NativeFont tf = fontCache.getFont(kMicUIFont, 13, FontWeight::Normal);
        p.drawText(toWideString(timeStr),
                   x + 48, barY, 80, barHeight,
                   tf, colTimecode,
                   DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        // ── Status label ──────────────────────────────────────────────────────
        // _micBasename handles both '/' and '\\' so the filename displays
        // correctly whether we're on Windows, Linux, macOS, or Android.
        std::string status;
        if (recording) {
            status = "Recording...";
        } else if (!_lastPath.empty()) {
            status = _micBasename(_lastPath);
        } else {
#if defined(__ANDROID__)
            status = "Tap to record";
#else
            status = "Click to record";
#endif
        }

        int statusX = x + 136;
        int statusW = width - statusX - 8;
        p.drawText(toWideString(status),
                   statusX, barY, statusW, barHeight,
                   tf, colText,
                   DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        // ── Progress line (top of bar) — shows 5-min cap consumption ──────────
        if (recording && mic.getProgress() > 0.f) {
            int progW = (int)(mic.getProgress() * width);
            p.fillRect(x, barY, progW, 2, colWaveActive);
        }

        needsPaint = false;
    }

    // =========================================================================
    // Mouse / touch events
    // =========================================================================

    bool handleMouseDown(int mx, int my) override {
        if (!_inWidget(mx, my)) return false;
        if (_inRect(mx, my, _btnRect)) {
            _toggleRecording();
            markNeedsPaint();
            return true;
        }
        return false;
    }

    bool handleMouseMove(int mx, int my) override {
        bool hov = _inRect(mx, my, _btnRect);
        if (hov != _hovBtn) { _hovBtn = hov; markNeedsPaint(); }
        return false;
    }

    bool handleMouseLeave() override {
        _hovBtn = false;
        markNeedsPaint();
        return true;
    }

private:
    // ── State ─────────────────────────────────────────────────────────────────
    std::vector<float>                      _waveform;
    std::string                             _lastPath;
    std::function<void(const std::string&)> _onSaved;
    bool                                    _hovBtn = false;

    // ── Hit rects ─────────────────────────────────────────────────────────────
    struct Rect { int x, y, w, h; };
    Rect _btnRect {};

    // ── Refresh timer (33ms repaint while recording) ──────────────────────────
    TimerID _refreshTimer = 0;

    void _startTimer() {
        if (_refreshTimer) return;
        _refreshTimer = FluxUI::getCurrentInstance()->setInterval(33, [this]() {
            if (FluxMic::get().isRecording()) markNeedsPaint();
        });
    }

    void _stopTimer() {
        auto* ui = FluxUI::getCurrentInstance();
        if (!ui || !_refreshTimer) return;
        ui->clearInterval(_refreshTimer);
        _refreshTimer = 0;
    }

    // ── Toggle record / stop ──────────────────────────────────────────────────
    void _toggleRecording() {
        auto& mic = FluxMic::get();
        if (mic.isRecording()) {
            _stopTimer();
            mic.stop();       // saves WAV, fires onSaved callback
        } else {
            if (mic.start())
                _startTimer();
        }
    }

    // ── Helpers ───────────────────────────────────────────────────────────────
    std::shared_ptr<MicRecorderWidget> self() {
        return std::static_pointer_cast<MicRecorderWidget>(shared_from_this());
    }
    bool _inWidget(int mx, int my) const {
        return mx >= x && mx < x + width && my >= y && my < y + height;
    }
    static bool _inRect(int mx, int my, const Rect& r) {
        return mx >= r.x && mx < r.x + r.w && my >= r.y && my < r.y + r.h;
    }
    static std::string _fmtTime(float secs) {
        int s = (int)secs;
        char buf[16];
        snprintf(buf, sizeof(buf), "%d:%02d", s / 60, s % 60);
        return buf;
    }
};

using MicRecorderWidgetPtr = std::shared_ptr<MicRecorderWidget>;

// ── Factory ───────────────────────────────────────────────────────────────────
inline MicRecorderWidgetPtr MicRecorder() {
    return std::make_shared<MicRecorderWidget>();
}

#endif // Android || Windows || Linux || macOS