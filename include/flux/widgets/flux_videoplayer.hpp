// flux_videoplayer.hpp
// Self-contained video player widget.
// Blits the FluxVideo OES texture via NanoVG each frame, overlays a browser-
// style control bar at the bottom (identical visual language to
// AudioPlayerWidget).
//
// Usage (path):
//   VideoPlayer("video/sample.mp4")
//       ->setWidth(480)->setHeight(270)
//
// Usage (URL):
//   VideoPlayer()
//       ->setUrl("https://example.com/video.mp4")
//       ->setWidth(480)->setHeight(270)
//
// Usage (memory):
//   VideoPlayer()
//       ->setMemory(bytes)               // std::vector<uint8_t>
//       ->setMemory(ptr, len)            // raw pointer + length
//       ->setWidth(480)->setHeight(270)
//
// The widget manages all FluxVideo state internally — app.cpp just instantiates
// it.
//
#pragma once

#include "../flux_http.hpp"   // FluxHttp + fluxPostToUIThread

// ============================================================================
// VideoSourceType — shared across all three platform backends
// ============================================================================

enum class VideoSourceType { None, Path, Url, Memory };

// ─────────────────────────────────────────────────────────────────────────────
// Shared helper: detect container extension from magic bytes
// ─────────────────────────────────────────────────────────────────────────────
inline std::string VP_detectVideoExtension(const std::vector<uint8_t> &bytes) {
    if (bytes.size() >= 12) {
        // MP4 / MOV / M4V — ftyp box at offset 4
        if (bytes[4] == 'f' && bytes[5] == 't' && bytes[6] == 'y' && bytes[7] == 'p')
            return ".mp4";
        // QuickTime
        if (bytes[4] == 'm' && bytes[5] == 'o' && bytes[6] == 'o' && bytes[7] == 'v')
            return ".mov";
    }
    if (bytes.size() >= 4) {
        // RIFF/AVI
        if (bytes[0]=='R' && bytes[1]=='I' && bytes[2]=='F' && bytes[3]=='F')
            return ".avi";
        // MKV / WebM — EBML header
        if (bytes[0]==0x1A && bytes[1]==0x45 && bytes[2]==0xDF && bytes[3]==0xA3)
            return ".mkv";
    }
    if (bytes.size() >= 3) {
        // FLV
        if (bytes[0]=='F' && bytes[1]=='L' && bytes[2]=='V')
            return ".flv";
    }
    return ".mp4"; // best-guess fallback
}

// Write bytes to a platform temp file with the given extension.
// Returns the final path on success, empty string on failure.
inline std::string VP_writeTempFile(const std::vector<uint8_t> &bytes,
                                    const std::string &ext) {
#ifdef _WIN32
    char tmpDir[MAX_PATH];
    if (GetTempPathA(MAX_PATH, tmpDir) == 0) return {};
    char tmpFile[MAX_PATH];
    if (GetTempFileNameA(tmpDir, "flxv", 0, tmpFile) == 0) return {};
    std::string outPath = std::string(tmpFile) + ext;
    FILE *f = nullptr;
    fopen_s(&f, outPath.c_str(), "wb");
#else
    std::string tmpl = std::string(P_tmpdir) + "/flxvideoXXXXXX";
    std::vector<char> tmplBuf(tmpl.begin(), tmpl.end());
    tmplBuf.push_back('\0');
    int fd = mkstemp(tmplBuf.data());
    if (fd < 0) return {};
    close(fd);
    std::string outPath = std::string(tmplBuf.data()) + ext;
    ::rename(tmplBuf.data(), outPath.c_str());
    FILE *f = fopen(outPath.c_str(), "wb");
#endif
    if (!f) return {};
    fwrite(bytes.data(), 1, bytes.size(), f);
    fclose(f);
    return outPath;
}


// ============================================================================
// VideoPlayerWidget — Android (NanoVG / OES backend)
// ============================================================================
#ifdef __ANDROID__

#include "flux/flux.hpp"
#include "flux/flux_video.hpp"
#include "flux_icons.hpp"

// ── nanovg OES extension ────────────────────────────────────────────────────
extern int  NVG_createImageFromOES(NVGcontext *vg, GLuint oesTexId, int w, int h);
extern void NVG_updateImageFromOES(NVGcontext *vg, int nvgImage, GLuint oesTexId);
extern NVGcontext *FluxAndroid_getVG();

// ============================================================================
class VideoPlayerWidget : public Widget {
public:
    // ── Config ────────────────────────────────────────────────────────────
    std::string videoPath;
    int  barHeight = 40;
    int  pillarR   = 0;
    bool autoPlay  = false;

    // ── Colors ────────────────────────────────────────────────────────────
    Color colBar       = Color::fromRGBA( 20,  20,  20, 220);
    Color colTrackBg   = Color::fromRGB (100, 100, 100);
    Color colTrackFill = Color::fromRGB (220, 220, 220);
    Color colThumb     = Color::fromRGB (255, 255, 255);
    Color colText      = Color::fromRGB (230, 230, 230);
    Color colIcon      = Color::fromRGB (220, 220, 220);
    Color colIconHov   = Color::fromRGB (255, 255, 255);
    Color colOverlay   = Color::fromRGBA(  0,   0,   0,  60);
    Color colLoadingText = Color::fromRGB(180, 180, 180);
    Color colErrorText   = Color::fromRGB(220,  80,  80);

    // ── Fluent setters ────────────────────────────────────────────────────
    std::shared_ptr<VideoPlayerWidget> setPath(const std::string &p) {
        videoPath   = p;
        _sourceType = VideoSourceType::Path;
        _sourceUrl.clear();
        _sourceMemory.clear();
        return self();
    }

    std::shared_ptr<VideoPlayerWidget> setUrl(const std::string &url) {
        _sourceUrl  = url;
        _sourceType = VideoSourceType::Url;
        videoPath.clear();
        _sourceMemory.clear();
        return self();
    }

    std::shared_ptr<VideoPlayerWidget> setMemory(const std::vector<uint8_t> &bytes) {
        _sourceMemory = bytes;
        _sourceType   = VideoSourceType::Memory;
        videoPath.clear();
        _sourceUrl.clear();
        return self();
    }

    std::shared_ptr<VideoPlayerWidget> setMemory(const uint8_t *data, size_t len) {
        _sourceMemory.assign(data, data + len);
        _sourceType = VideoSourceType::Memory;
        videoPath.clear();
        _sourceUrl.clear();
        return self();
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

    // ── Constructor / destructor ──────────────────────────────────────────
    VideoPlayerWidget() {
        autoWidth = autoHeight = false;
        width = 320; height = 180;

        auto &vid = FluxVideo::get();
        vid.setOnFinished([this]() {
            _playing  = false;
            _finished = true;
            _progress = 1.f;
            markNeedsPaint();
        });
        vid.setOnReady([this](int w, int h) {
            _vidW = w; _vidH = h;
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
    void computeLayout(GraphicsContext & /*ctx*/, const BoxConstraints &constraints,
                       FontCache & /*fontCache*/) override {
        if (autoWidth)  width  = constraints.maxWidth;
        if (autoHeight) height = constraints.maxHeight;
        applyConstraints();
        needsLayout = false;

        if (!_opened) {
            _opened = true;
            _openVideoSource();
        }
    }

    // =========================================================================
    // Render
    // =========================================================================
    void render(GraphicsContext &ctx, FontCache &fontCache) override {
        auto &vid = FluxVideo::get();
        NVGcontext *vg = FluxAndroid_getVG();

        if (vid.updateFrame() && _nvgImage >= 0) {
            NVG_updateImageFromOES(vg, _nvgImage, vid.getTextureId());
            _progress = vid.getProgress();
        }

        Painter p(ctx);

        // ── Loading / error overlay ──────────────────────────────────
        if (_netState == NetState::Loading) {
            _renderStatusOverlay(p, fontCache, "\xe2\x80\xa6 Loading\xe2\x80\xa6", colLoadingText);
            needsPaint = false; return;
        }
        if (_netState == NetState::Error) {
            _renderStatusOverlay(p, fontCache, "Error loading video", colErrorText);
            needsPaint = false; return;
        }

        // ── Video frame ───────────────────────────────────────────────
        if (_nvgImage >= 0 && _vidW > 0 && _vidH > 0) {
            float vidAR = (float)_vidW / _vidH;
            float widAR = (float)width  / (height - barHeight);
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

            if (_barVisible)
                p.fillRect(x, y, width, height - barHeight, colOverlay);
        } else {
            p.fillRect(x, y, width, height, Color::fromRGB(20, 20, 20));
        }

        if (_barVisible) _renderBar(ctx, fontCache, p);
        if (!_playing && _barVisible) _renderCenterPlay(p, fontCache);

        needsPaint = false;
    }

    // =========================================================================
    // Mouse events
    // =========================================================================
    bool handleMouseDown(int mx, int my) override {
        if (!_inWidget(mx, my)) return false;
        if (!_inRect(mx, my, _barRect)) {
            _barVisible = true; _resetBarTimer(); _togglePlayPause(); markNeedsPaint(); return true;
        }
        if (_inRect(mx, my, _playBtnRect)) { _togglePlayPause(); markNeedsPaint(); return true; }
        if (_inRect(mx, my, _trackRect)) {
            _dragging = true;
            FluxUI::getCurrentInstance()->captureMouseInput();
            _seekFromMouse(mx); markNeedsPaint(); return true;
        }
        return true;
    }

    bool handleMouseUp(int, int) override {
        if (_dragging) { _dragging = false; FluxUI::getCurrentInstance()->releaseMouseInput(); markNeedsPaint(); }
        return false;
    }

    bool handleMouseMove(int mx, int my) override {
        if (_dragging) { _seekFromMouse(mx); return true; }
        bool inW = _inWidget(mx, my);
        if (inW != _barVisible) { _barVisible = inW; markNeedsPaint(); }
        if (inW) _resetBarTimer();
        bool hp = _inRect(mx, my, _playBtnRect);
        if (hp != _hovPlay) { _hovPlay = hp; markNeedsPaint(); }
        return false;
    }

    bool handleMouseLeave() override {
        _barVisible = _hovPlay = false; markNeedsPaint(); return true;
    }

private:
    // ── Source ────────────────────────────────────────────────────────────
    VideoSourceType      _sourceType   = VideoSourceType::None;
    std::string          _sourceUrl;
    std::vector<uint8_t> _sourceMemory;

    enum class NetState { Idle, Loading, Error };
    NetState _netState = NetState::Idle;

    // ── State ─────────────────────────────────────────────────────────────
    bool  _opened   = false;
    bool  _playing  = false;
    bool  _finished = false;
    float _progress = 0.f;
    int   _vidW = 0, _vidH = 0;
    int   _nvgImage = -1;
    bool  _dragging = false;
    bool  _barVisible = true;
    bool  _hovPlay  = false;

    struct Rect { int x, y, w, h; };
    Rect _barRect{}, _playBtnRect{}, _trackRect{};

    TimerID _progressTimer = 0;
    TimerID _barHideTimer  = 0;

    // ── Source dispatch ───────────────────────────────────────────────────
    void _openVideoSource() {
        if (_sourceType == VideoSourceType::Url && !_sourceUrl.empty()) {
            _loadFromUrl(); return;
        }
        if (_sourceType == VideoSourceType::Memory && !_sourceMemory.empty()) {
            _playFromMemory(); return;
        }
        if (!videoPath.empty()) _openVideo();
    }

    void _loadFromUrl() {
        _netState = NetState::Loading; markNeedsPaint();
        std::weak_ptr<VideoPlayerWidget> weak = self();
        std::string url = _sourceUrl;
        FluxHttp::get(url, [weak](HttpResult result) {
            auto s = weak.lock();
            if (!s) return;
            if (!result.success || result.body.empty()) {
                s->_netState = NetState::Error; s->markNeedsPaint(); return;
            }
            const auto *d = reinterpret_cast<const uint8_t *>(result.body.data());
            s->_sourceMemory.assign(d, d + result.body.size());
            s->_netState = NetState::Idle;
            s->_playFromMemory();
        });
    }

    void _playFromMemory() {
        if (_sourceMemory.empty()) return;
        std::string ext  = VP_detectVideoExtension(_sourceMemory);
        std::string path = VP_writeTempFile(_sourceMemory, ext);
        if (path.empty()) { _netState = NetState::Error; markNeedsPaint(); return; }
        videoPath = path;
        _openVideo();
    }

    void _openVideo() {
        FluxVideo::get().open(videoPath);
        if (autoPlay) { FluxVideo::get().play(); _playing = true; _startProgressTimer(); }
    }

    void _buildNVGImage() {
        NVGcontext *vg = FluxAndroid_getVG();
        if (!vg || _vidW <= 0) return;
        if (_nvgImage >= 0) nvgDeleteImage(vg, _nvgImage);
        _nvgImage = NVG_createImageFromOES(vg, FluxVideo::get().getTextureId(), _vidW, _vidH);
    }

    void _togglePlayPause() {
        auto &vid = FluxVideo::get();
        if (_finished) {
            _finished = false; _progress = 0.f;
            vid.seekToProgress(0.f); vid.play(); _playing = true; _startProgressTimer(); return;
        }
        if (_playing) { vid.pause(); _playing = false; }
        else          { vid.play();  _playing = true; _startProgressTimer(); }
    }

    void _seekFromMouse(int mx) {
        if (_trackRect.w <= 0) return;
        float t = std::max(0.f, std::min(1.f, (float)(mx - _trackRect.x) / (float)_trackRect.w));
        _progress = t; FluxVideo::get().seekToProgress(t);
        if (_finished && t < 0.999f) { _finished = false; FluxVideo::get().play(); _playing = true; _startProgressTimer(); }
        markNeedsPaint();
    }

    void _startProgressTimer() {
        if (_progressTimer) return;
        _progressTimer = FluxUI::getCurrentInstance()->setInterval(33, [this]() {
            if (_playing) { _progress = FluxVideo::get().getProgress(); markNeedsPaint(); }
        });
    }

    void _stopTimer() {
        auto *ui = FluxUI::getCurrentInstance(); if (!ui) return;
        if (_progressTimer) { ui->clearInterval(_progressTimer); _progressTimer = 0; }
        if (_barHideTimer)  { ui->clearInterval(_barHideTimer);  _barHideTimer  = 0; }
    }

    void _resetBarTimer() {
        auto *ui = FluxUI::getCurrentInstance(); if (!ui) return;
        if (_barHideTimer) { ui->clearInterval(_barHideTimer); _barHideTimer = 0; }
        _barHideTimer = ui->setInterval(3000, [this]() {
            auto *u = FluxUI::getCurrentInstance();
            if (u && _barHideTimer) { u->clearInterval(_barHideTimer); _barHideTimer = 0; }
            if (_playing) { _barVisible = false; markNeedsPaint(); }
        });
    }

    std::shared_ptr<VideoPlayerWidget> self() {
        return std::static_pointer_cast<VideoPlayerWidget>(shared_from_this());
    }
    bool _inWidget(int mx, int my) const {
        return mx >= x && mx < x + width && my >= y && my < y + height;
    }
    static bool _inRect(int mx, int my, const Rect &r) {
        return mx >= r.x && mx < r.x + r.w && my >= r.y && my < r.y + r.h;
    }

    static std::string _fmtTime(float s) {
        int si = (int)s; char buf[16];
        snprintf(buf, sizeof(buf), "%d:%02d", si / 60, si % 60); return buf;
    }

    void _renderStatusOverlay(Painter &p, FontCache &fontCache,
                               const std::string &msg, Color col) {
        p.fillRect(x, y, width, height, Color::fromRGB(20, 20, 20));
        NativeFont tf = fontCache.getFont("Segoe UI", 14, FontWeight::Normal);
        p.drawTextA(msg, x, y, width, height, tf, col,
                    DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    void _renderBar(GraphicsContext &ctx, FontCache &fontCache, Painter &p) {
        int barY = y + height - barHeight;
        _barRect = {x, barY, width, barHeight};
        p.fillRect(x, barY, width, barHeight, colBar);

        int midY = barY + barHeight / 2;
        int cx   = x + 6;
        int btnSz = 28;
        _playBtnRect = {cx, barY + (barHeight - btnSz) / 2, btnSz, btnSz};

        {
            NativeFont iconFont = fontCache.getFont(kIconFont, 18, FontWeight::Normal);
            std::wstring g(1, FluxIcons::glyph(_playing ? FluxIcons::Pause : FluxIcons::Play));
            Color c = _hovPlay ? colIconHov : colIcon;
            p.drawText(g, _playBtnRect.x, _playBtnRect.y, _playBtnRect.w, _playBtnRect.h,
                       iconFont, c, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        cx += btnSz + 6;

        float dur = FluxVideo::get().getDurationSeconds();
        std::string ts = _fmtTime(_progress * dur) + " / " + _fmtTime(dur);
        NativeFont tf = fontCache.getFont("Segoe UI", 12, FontWeight::Normal);
        int tw = 0, th = 0;
        Painter(ctx).measureText(toWideString(ts), tf, tw, th);
        p.drawText(toWideString(ts), cx, barY, tw + 4, barHeight, tf, colText,
                   DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        cx += tw + 8;

        int trackW = std::max(20, x + width - 12 - cx);
        _trackRect = {cx, midY - 8, trackW, 16};
        p.fillRoundedRectGDI(cx, midY - 1, trackW, 3, 3, colTrackBg, colTrackBg, 0);
        int fillW = (int)(_progress * trackW);
        if (fillW > 0)
            p.fillRoundedRectGDI(cx, midY - 1, fillW, 3, 3, colTrackFill, colTrackFill, 0);
        p.drawEllipse(cx + fillW - 6, midY - 6, 12, 12, colThumb, colThumb, 0);
    }

    void _renderCenterPlay(Painter &p, FontCache &fontCache) {
        int cx = x + width / 2, cy = y + (height - barHeight) / 2, r = 28;
        p.drawEllipse(cx - r, cy - r, r * 2, r * 2,
                      Color::fromRGBA(0,0,0,160), Color::fromRGBA(0,0,0,0), 0);
        NativeFont iconFont = fontCache.getFont(kIconFont, 32, FontWeight::Normal);
        std::wstring g(1, FluxIcons::glyph(FluxIcons::Play));
        p.drawText(g, cx - r, cy - r, r * 2, r * 2, iconFont,
                   Color::fromRGB(255,255,255), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
};

using VideoPlayerWidgetPtr = std::shared_ptr<VideoPlayerWidget>;

inline VideoPlayerWidgetPtr VideoPlayer(const std::string &path = "") {
    auto w = std::make_shared<VideoPlayerWidget>();
    if (!path.empty()) w->setPath(path);
    return w;
}

inline VideoPlayerWidgetPtr VideoPlayerFromUrl(const std::string &url) {
    return std::make_shared<VideoPlayerWidget>()->setUrl(url);
}

inline VideoPlayerWidgetPtr VideoPlayerFromMemory(const std::vector<uint8_t> &bytes) {
    return std::make_shared<VideoPlayerWidget>()->setMemory(bytes);
}

inline VideoPlayerWidgetPtr VideoPlayerFromMemory(const uint8_t *data, size_t len) {
    return std::make_shared<VideoPlayerWidget>()->setMemory(data, len);
}

#endif // __ANDROID__


// ============================================================================
// VideoPlayerWidget — Windows (GDI backend)
// ============================================================================
#ifdef _WIN32

#include "flux/flux.hpp"
#include "flux/flux_video.hpp"
#include "flux_icons.hpp"

class VideoPlayerWidget : public Widget {
public:
    std::string videoPath;
    int  barHeight = 40;
    bool autoPlay  = false;

    Color colBar       = Color::fromRGBA( 20,  20,  20, 220);
    Color colTrackBg   = Color::fromRGB (100, 100, 100);
    Color colTrackFill = Color::fromRGB (220, 220, 220);
    Color colThumb     = Color::fromRGB (255, 255, 255);
    Color colText      = Color::fromRGB (230, 230, 230);
    Color colIcon      = Color::fromRGB (220, 220, 220);
    Color colIconHov   = Color::fromRGB (255, 255, 255);
    Color colBg        = Color::fromRGB (  0,   0,   0);
    Color colLoadingText = Color::fromRGB(180, 180, 180);
    Color colErrorText   = Color::fromRGB(220,  80,  80);

    std::shared_ptr<VideoPlayerWidget> setPath(const std::string &p) {
        videoPath   = p;
        _sourceType = VideoSourceType::Path;
        _sourceUrl.clear();
        _sourceMemory.clear();
        return self();
    }

    std::shared_ptr<VideoPlayerWidget> setUrl(const std::string &url) {
        _sourceUrl  = url;
        _sourceType = VideoSourceType::Url;
        videoPath.clear();
        _sourceMemory.clear();
        return self();
    }

    std::shared_ptr<VideoPlayerWidget> setMemory(const std::vector<uint8_t> &bytes) {
        _sourceMemory = bytes;
        _sourceType   = VideoSourceType::Memory;
        videoPath.clear();
        _sourceUrl.clear();
        return self();
    }

    std::shared_ptr<VideoPlayerWidget> setMemory(const uint8_t *data, size_t len) {
        _sourceMemory.assign(data, data + len);
        _sourceType = VideoSourceType::Memory;
        videoPath.clear();
        _sourceUrl.clear();
        return self();
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

    VideoPlayerWidget() {
        autoWidth = autoHeight = false;
        width = 320; height = 180;

        FluxVideo::get().setOnFinished([this]() {
            _finishedPending = true;
            if (auto *ui = FluxUI::getCurrentInstance())
                ui->invalidateWidget(x, y, width, height);
        });
        FluxVideo::get().setOnReady([this](int, int) { markNeedsPaint(); });
    }

    ~VideoPlayerWidget() {
        _destroyed = true;
        _stopTimers();
        FluxVideo::get().close();
        FluxVideo::get().setOnFinished(nullptr);
        FluxVideo::get().setOnReady(nullptr);
    }

    // ── Layout ────────────────────────────────────────────────────────────
    void computeLayout(GraphicsContext & /*ctx*/, const BoxConstraints &constraints,
                       FontCache & /*fontCache*/) override {
        if (autoWidth)  width  = constraints.maxWidth;
        if (autoHeight) height = constraints.maxHeight;
        applyConstraints();
        needsLayout = false;

        if (!_opened) {
            _opened = true;
            _openVideoSource();
        }
    }

    // ── Render ────────────────────────────────────────────────────────────
    void render(GraphicsContext &ctx, FontCache &fontCache) override {
        if (_finishedPending.exchange(false)) {
            _playing = false; _finished = true; _progress = 1.f;
        }

        Painter p(ctx);
        p.fillRect(x, y, width, height, colBg);

        // ── Loading / error overlay ──────────────────────────────────
        if (_netState == NetState::Loading) {
            _renderStatusOverlay(p, fontCache, "... Loading ...", colLoadingText);
            needsPaint = false; return;
        }
        if (_netState == NetState::Error) {
            _renderStatusOverlay(p, fontCache, "Error loading video", colErrorText);
            needsPaint = false; return;
        }

        auto letterbox = [&](int srcW, int srcH,
                             int &dstX, int &dstY, int &dstW, int &dstH) {
            float vidAR = (float)srcW / (float)srcH;
            float widAR = (float)width  / (float)(height - barHeight);
            if (vidAR > widAR) {
                dstW = width; dstH = (int)(dstW / vidAR);
                dstX = x;    dstY = y + (height - barHeight - dstH) / 2;
            } else {
                dstH = height - barHeight; dstW = (int)(dstH * vidAR);
                dstX = x + (width - dstW) / 2; dstY = y;
            }
        };

        if (FluxVideo::get().hasNewFrame()) {
            auto frame = FluxVideo::get().lockFrame();
            if (frame.data && frame.width > 0 && frame.height > 0) {
                int byteCount = frame.stride * frame.height;
                _frameCache.assign(frame.data, frame.data + byteCount);
                _cachedSrcW = frame.width; _cachedSrcH = frame.height;
                _cachedBmi = {};
                _cachedBmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
                _cachedBmi.bmiHeader.biWidth       = frame.width;
                _cachedBmi.bmiHeader.biHeight      = -frame.height;
                _cachedBmi.bmiHeader.biPlanes      = 1;
                _cachedBmi.bmiHeader.biBitCount    = 24;
                _cachedBmi.bmiHeader.biCompression = BI_RGB;
            }
            _progress = FluxVideo::get().getProgress();
        }

        if (!_frameCache.empty() && _cachedSrcW > 0) {
            int dstX, dstY, dstW, dstH;
            letterbox(_cachedSrcW, _cachedSrcH, dstX, dstY, dstW, dstH);
            ::SetStretchBltMode(ctx.hdc, HALFTONE);
            ::SetBrushOrgEx(ctx.hdc, 0, 0, nullptr);
            ::StretchDIBits(ctx.hdc, dstX, dstY, dstW, dstH,
                            0, 0, _cachedSrcW, _cachedSrcH,
                            _frameCache.data(), &_cachedBmi, DIB_RGB_COLORS, SRCCOPY);
        }

        if (_barVisible) _renderBar(ctx, fontCache, p);
        if (!_playing && _barVisible) _renderCenterPlay(p, fontCache);
        needsPaint = false;
    }

    // ── Mouse events ──────────────────────────────────────────────────────
    bool handleMouseDown(int mx, int my) override {
        if (!_inWidget(mx, my)) return false;

        int barY  = y + height - barHeight;
        int btnSz = 28;
        Rect barArea  = {x, barY, width, barHeight};
        Rect playBtn  = {x + 6, barY + (barHeight - btnSz) / 2, btnSz, btnSz};

        // Tap on the video area (above the bar): show bar + toggle play/pause
        if (!_inRect(mx, my, barArea)) {
            _barVisible = true;
            _resetBarTimer();
            _togglePlayPause();
            markNeedsPaint();
            return true;
        }

        // Tap inside the bar
        _barVisible = true;
        _resetBarTimer();
        markNeedsPaint();

        if (_inRect(mx, my, playBtn)) { _togglePlayPause(); return true; }
        if (_inRect(mx, my, _trackRect)) {
            _dragging = true;
            FluxUI::getCurrentInstance()->captureMouseInput();
            _seekFromMouse(mx); return true;
        }
        return true;
    }

    bool handleMouseUp(int, int) override {
        if (_dragging) { _dragging = false; FluxUI::getCurrentInstance()->releaseMouseInput(); }
        return false;
    }

    bool handleMouseMove(int mx, int my) override {
        if (_dragging) { _seekFromMouse(mx); return true; }
        bool inW = _inWidget(mx, my);
        if (inW != _barVisible) { _barVisible = inW; markNeedsPaint(); }
        if (inW) _resetBarTimer();
        bool hp = _inRect(mx, my, _playBtnRect);
        if (hp != _hovPlay) { _hovPlay = hp; markNeedsPaint(); }
        return false;
    }

    bool handleMouseLeave() override {
        _barVisible = _hovPlay = false; markNeedsPaint(); return true;
    }

private:
    // ── Source ────────────────────────────────────────────────────────────
    VideoSourceType      _sourceType   = VideoSourceType::None;
    std::string          _sourceUrl;
    std::vector<uint8_t> _sourceMemory;

    enum class NetState { Idle, Loading, Error };
    NetState _netState = NetState::Idle;

    bool  _opened   = false;
    bool  _playing  = false;
    bool  _finished = false;
    float _progress = 0.f;
    bool  _dragging = false;
    bool  _barVisible = true;
    bool  _hovPlay  = false;

    std::vector<uint8_t> _frameCache;
    BITMAPINFO _cachedBmi{};
    int _cachedSrcW = 0, _cachedSrcH = 0;

    struct Rect { int x, y, w, h; };
    Rect _barRect{}, _playBtnRect{}, _trackRect{};

    TimerID _progressTimer = 0, _barHideTimer = 0;
    std::atomic<bool> _destroyed      {false};
    std::atomic<bool> _finishedPending{false};

    // ── Source dispatch ───────────────────────────────────────────────────
    void _openVideoSource() {
        if (_sourceType == VideoSourceType::Url && !_sourceUrl.empty()) {
            _loadFromUrl(); return;
        }
        if (_sourceType == VideoSourceType::Memory && !_sourceMemory.empty()) {
            _playFromMemory(); return;
        }
        if (!videoPath.empty()) _openVideo();
    }

    void _loadFromUrl() {
        _netState = NetState::Loading; markNeedsPaint();
        std::weak_ptr<VideoPlayerWidget> weak = self();
        std::string url = _sourceUrl;
        FluxHttp::get(url, [weak](HttpResult result) {
            auto s = weak.lock();
            if (!s) return;
            if (!result.success || result.body.empty()) {
                s->_netState = NetState::Error; s->markNeedsPaint(); return;
            }
            const auto *d = reinterpret_cast<const uint8_t *>(result.body.data());
            s->_sourceMemory.assign(d, d + result.body.size());
            s->_netState = NetState::Idle;
            s->_playFromMemory();
        });
    }

    void _playFromMemory() {
        if (_sourceMemory.empty()) return;
        std::string ext  = VP_detectVideoExtension(_sourceMemory);
        std::string path = VP_writeTempFile(_sourceMemory, ext);
        if (path.empty()) { _netState = NetState::Error; markNeedsPaint(); return; }
        videoPath = path;
        _openVideo();
    }

    void _openVideo() {
        FluxVideo::get().open(videoPath);
        if (autoPlay) { FluxVideo::get().play(); _playing = true; _startProgressTimer(); }
    }

    void _togglePlayPause() {
        auto &vid = FluxVideo::get();
        if (_finished) {
            _finished = false; _progress = 0.f;
            vid.seekToProgress(0.f); vid.play(); _playing = true; _startProgressTimer(); return;
        }
        if (_playing) { vid.pause(); _playing = false; }
        else          { vid.play();  _playing = true; _startProgressTimer(); }
    }

    void _seekFromMouse(int mx) {
        if (_trackRect.w <= 0) return;
        float t = std::max(0.f, std::min(1.f, (float)(mx - _trackRect.x) / (float)_trackRect.w));
        _progress = t; FluxVideo::get().seekToProgress(t);
        if (_finished && t < 0.999f) {
            _finished = false; FluxVideo::get().play(); _playing = true; _startProgressTimer();
        }
        markNeedsPaint();
    }

    void _startProgressTimer() {
        if (_progressTimer) return;
        _progressTimer = FluxUI::getCurrentInstance()->setInterval(33, [this]() {
            if (_destroyed) return;
            if (_playing) { _progress = FluxVideo::get().getProgress(); markNeedsPaint(); }
        });
    }

    void _resetBarTimer() {
        auto *ui = FluxUI::getCurrentInstance(); if (!ui) return;
        if (_barHideTimer) { ui->clearInterval(_barHideTimer); _barHideTimer = 0; }
        _barHideTimer = ui->setInterval(3000, [this]() {
            if (_destroyed) return;
            auto *u = FluxUI::getCurrentInstance();
            if (u && _barHideTimer) { u->clearInterval(_barHideTimer); _barHideTimer = 0; }
            if (_playing) { _barVisible = false; markNeedsPaint(); }
        });
    }

    void _stopTimers() {
        auto *ui = FluxUI::getCurrentInstance(); if (!ui) return;
        if (_progressTimer) { ui->clearInterval(_progressTimer); _progressTimer = 0; }
        if (_barHideTimer)  { ui->clearInterval(_barHideTimer);  _barHideTimer  = 0; }
    }

    std::shared_ptr<VideoPlayerWidget> self() {
        return std::static_pointer_cast<VideoPlayerWidget>(shared_from_this());
    }
    bool _inWidget(int mx, int my) const {
        return mx >= x && mx < x + width && my >= y && my < y + height;
    }
    static bool _inRect(int mx, int my, const Rect &r) {
        return mx >= r.x && mx < r.x + r.w && my >= r.y && my < r.y + r.h;
    }

    static std::string _fmtTime(float s) {
        int si = (int)s; char buf[16];
        snprintf(buf, sizeof(buf), "%d:%02d", si / 60, si % 60); return buf;
    }

    void _renderStatusOverlay(Painter &p, FontCache &fontCache,
                               const std::string &msg, Color col) {
        NativeFont tf = fontCache.getFont("Segoe UI", 14, FontWeight::Normal);
        p.drawTextA(msg, x, y, width, height, tf, col,
                    DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    void _renderBar(GraphicsContext & /*ctx*/, FontCache &fontCache, Painter &p) {
        int barY = y + height - barHeight;
        _barRect = {x, barY, width, barHeight};
        p.fillRect(x, barY, width, barHeight, colBar);

        int midY = barY + barHeight / 2;
        int cx   = x + 6;
        int btnSz = 28;
        _playBtnRect = {cx, barY + (barHeight - btnSz) / 2, btnSz, btnSz};

        {
            NativeFont iconFont = fontCache.getFont(kIconFont, 18, FontWeight::Normal);
            std::wstring g(1, FluxIcons::glyph(_playing ? FluxIcons::Pause : FluxIcons::Play));
            Color c = _hovPlay ? colIconHov : colIcon;
            p.drawText(g, _playBtnRect.x, _playBtnRect.y, _playBtnRect.w, _playBtnRect.h,
                       iconFont, c, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        cx += btnSz + 6;

        float dur = FluxVideo::get().getDurationSeconds();
        std::string ts = _fmtTime(_progress * dur) + " / " + _fmtTime(dur);
        NativeFont tf = fontCache.getFont("Segoe UI", 12, FontWeight::Normal);
        int tw = 0, th = 0;
        p.measureText(toWideString(ts), tf, tw, th);
        p.drawText(toWideString(ts), cx, barY, tw + 4, barHeight, tf, colText,
                   DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        cx += tw + 8;

        int trackW = std::max(20, x + width - 12 - cx);
        _trackRect = {cx, midY - 8, trackW, 16};
        p.fillRoundedRectGDI(cx, midY - 1, trackW, 3, 3, colTrackBg, colTrackBg, 0);
        int fillW = (int)(_progress * trackW);
        if (fillW > 0)
            p.fillRoundedRectGDI(cx, midY - 1, fillW, 3, 3, colTrackFill, colTrackFill, 0);
        p.drawEllipse(cx + fillW - 6, midY - 6, 12, 12, colThumb, colThumb, 0);
    }

    void _renderCenterPlay(Painter &p, FontCache &fontCache) {
        int cx = x + width / 2, cy = y + (height - barHeight) / 2, r = 28;
        p.drawEllipse(cx - r, cy - r, r * 2, r * 2,
                      Color::fromRGBA(0,0,0,160), Color::fromRGBA(0,0,0,0), 0);
        NativeFont iconFont = fontCache.getFont(kIconFont, 32, FontWeight::Normal);
        std::wstring g(1, FluxIcons::glyph(FluxIcons::Play));
        p.drawText(g, cx - r, cy - r, r * 2, r * 2, iconFont,
                   Color::fromRGB(255,255,255), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
};

using VideoPlayerWidgetPtr = std::shared_ptr<VideoPlayerWidget>;

inline VideoPlayerWidgetPtr VideoPlayer(const std::string &path = "") {
    auto w = std::make_shared<VideoPlayerWidget>();
    if (!path.empty()) w->setPath(path);
    return w;
}

inline VideoPlayerWidgetPtr VideoPlayerFromUrl(const std::string &url) {
    return std::make_shared<VideoPlayerWidget>()->setUrl(url);
}

inline VideoPlayerWidgetPtr VideoPlayerFromMemory(const std::vector<uint8_t> &bytes) {
    return std::make_shared<VideoPlayerWidget>()->setMemory(bytes);
}

inline VideoPlayerWidgetPtr VideoPlayerFromMemory(const uint8_t *data, size_t len) {
    return std::make_shared<VideoPlayerWidget>()->setMemory(data, len);
}

#endif // _WIN32


// ============================================================================
// VideoPlayerWidget — Linux (Cairo / SDL backend)
// ============================================================================
#if defined(__linux__) && !defined(__ANDROID__)

#include "flux/flux.hpp"
#include "flux/flux_video.hpp"
#include "flux_icons.hpp"
#include <cairo/cairo.h>

class VideoPlayerWidget : public Widget {
public:
    std::string videoPath;
    int  barHeight = 40;
    bool autoPlay  = false;

    Color colBar       = Color::fromRGBA( 20,  20,  20, 220);
    Color colTrackBg   = Color::fromRGB (100, 100, 100);
    Color colTrackFill = Color::fromRGB (220, 220, 220);
    Color colThumb     = Color::fromRGB (255, 255, 255);
    Color colText      = Color::fromRGB (230, 230, 230);
    Color colIcon      = Color::fromRGB (220, 220, 220);
    Color colIconHov   = Color::fromRGB (255, 255, 255);
    Color colBg        = Color::fromRGB (  0,   0,   0);
    Color colLoadingText = Color::fromRGB(180, 180, 180);
    Color colErrorText   = Color::fromRGB(220,  80,  80);

    std::shared_ptr<VideoPlayerWidget> setPath(const std::string &p) {
        videoPath   = p;
        _sourceType = VideoSourceType::Path;
        _sourceUrl.clear();
        _sourceMemory.clear();
        return self();
    }

    std::shared_ptr<VideoPlayerWidget> setUrl(const std::string &url) {
        _sourceUrl  = url;
        _sourceType = VideoSourceType::Url;
        videoPath.clear();
        _sourceMemory.clear();
        return self();
    }

    std::shared_ptr<VideoPlayerWidget> setMemory(const std::vector<uint8_t> &bytes) {
        _sourceMemory = bytes;
        _sourceType   = VideoSourceType::Memory;
        videoPath.clear();
        _sourceUrl.clear();
        return self();
    }

    std::shared_ptr<VideoPlayerWidget> setMemory(const uint8_t *data, size_t len) {
        _sourceMemory.assign(data, data + len);
        _sourceType = VideoSourceType::Memory;
        videoPath.clear();
        _sourceUrl.clear();
        return self();
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

    VideoPlayerWidget() {
        autoWidth = autoHeight = false;
        width = 320; height = 180;

        FluxVideo::get().setOnFinished([this]() {
            _finishedPending = true;
            if (auto *ui = FluxUI::getCurrentInstance())
                ui->invalidateWidget(x, y, width, height);
        });
        FluxVideo::get().setOnReady([this](int, int) { markNeedsPaint(); });
    }

    ~VideoPlayerWidget() {
        _destroyed = true;
        _stopTimers();
        FluxVideo::get().close();
        FluxVideo::get().setOnFinished(nullptr);
        FluxVideo::get().setOnReady(nullptr);
        _freeCairoSurface();
    }

    // ── Layout ────────────────────────────────────────────────────────────
    void computeLayout(GraphicsContext & /*ctx*/, const BoxConstraints &constraints,
                       FontCache & /*fontCache*/) override {
        if (autoWidth)  width  = constraints.maxWidth;
        if (autoHeight) height = constraints.maxHeight;
        applyConstraints();
        needsLayout = false;

        if (!_opened) {
            _opened = true;
            _openVideoSource();
        }
    }

    // ── Render ────────────────────────────────────────────────────────────
    void render(GraphicsContext &ctx, FontCache &fontCache) override {
        if (_finishedPending.exchange(false)) {
            _playing = false; _finished = true; _progress = 1.f;
        }

        Painter p(ctx);
        p.fillRect(x, y, width, height, colBg);

        // ── Loading / error overlay ──────────────────────────────────
        if (_netState == NetState::Loading) {
            _renderStatusOverlay(p, fontCache, "... Loading ...", colLoadingText);
            needsPaint = false; return;
        }
        if (_netState == NetState::Error) {
            _renderStatusOverlay(p, fontCache, "Error loading video", colErrorText);
            needsPaint = false; return;
        }

        if (FluxVideo::get().hasNewFrame()) {
            auto frame = FluxVideo::get().lockFrame();
            if (frame.data && frame.width > 0 && frame.height > 0) {
                int byteCount = frame.stride * frame.height;
                _frameCache.assign(frame.data, frame.data + byteCount);
                _cachedSrcW = frame.width; _cachedSrcH = frame.height;
                if (_cachedSrcW != _cairoSurfW || _cachedSrcH != _cairoSurfH)
                    _rebuildCairoSurface();
            }
            _progress = FluxVideo::get().getProgress();
        }

        if (_cairoSurf && !_frameCache.empty() && _cachedSrcW > 0 && ctx.cr) {
            _updateCairoPixels();
            cairo_surface_mark_dirty(_cairoSurf);

            float vidAR = (float)_cachedSrcW / (float)_cachedSrcH;
            float widAR = (float)width  / (float)(height - barHeight);
            int dstX, dstY, dstW, dstH;
            if (vidAR > widAR) {
                dstW = width; dstH = (int)((float)dstW / vidAR);
                dstX = x;    dstY = y + (height - barHeight - dstH) / 2;
            } else {
                dstH = height - barHeight; dstW = (int)((float)dstH * vidAR);
                dstX = x + (width - dstW) / 2; dstY = y;
            }

            cairo_save(ctx.cr);
            cairo_translate(ctx.cr, dstX, dstY);
            cairo_scale(ctx.cr,
                        (double)dstW / (double)_cachedSrcW,
                        (double)dstH / (double)_cachedSrcH);
            cairo_set_source_surface(ctx.cr, _cairoSurf, 0, 0);
            cairo_pattern_set_filter(cairo_get_source(ctx.cr), CAIRO_FILTER_BILINEAR);
            cairo_rectangle(ctx.cr, 0, 0, _cachedSrcW, _cachedSrcH);
            cairo_fill(ctx.cr);
            cairo_restore(ctx.cr);
        }

        if (_barVisible) _renderBar(ctx, fontCache, p);
        if (!_playing && _barVisible) _renderCenterPlay(p, fontCache);
        needsPaint = false;
    }

    // ── Mouse events ──────────────────────────────────────────────────────
    bool handleMouseDown(int mx, int my) override {
        if (!_inWidget(mx, my)) return false;
        if (!_inRect(mx, my, _barRect)) {
            _barVisible = true; _resetBarTimer(); _togglePlayPause(); markNeedsPaint(); return true;
        }
        if (_inRect(mx, my, _playBtnRect)) { _togglePlayPause(); markNeedsPaint(); return true; }
        if (_inRect(mx, my, _trackRect)) {
            _dragging = true;
            FluxUI::getCurrentInstance()->captureMouseInput();
            _seekFromMouse(mx); markNeedsPaint(); return true;
        }
        return true;
    }

    bool handleMouseUp(int, int) override {
        if (_dragging) { _dragging = false; FluxUI::getCurrentInstance()->releaseMouseInput(); }
        return false;
    }

    bool handleMouseMove(int mx, int my) override {
        if (_dragging) { _seekFromMouse(mx); return true; }
        bool inW = _inWidget(mx, my);
        if (inW != _barVisible) { _barVisible = inW; markNeedsPaint(); }
        if (inW) _resetBarTimer();
        bool hp = _inRect(mx, my, _playBtnRect);
        if (hp != _hovPlay) { _hovPlay = hp; markNeedsPaint(); }
        return false;
    }

    bool handleMouseLeave() override {
        _barVisible = _hovPlay = false; markNeedsPaint(); return true;
    }

private:
    // ── Source ────────────────────────────────────────────────────────────
    VideoSourceType      _sourceType   = VideoSourceType::None;
    std::string          _sourceUrl;
    std::vector<uint8_t> _sourceMemory;

    enum class NetState { Idle, Loading, Error };
    NetState _netState = NetState::Idle;

    bool  _opened   = false;
    bool  _playing  = false;
    bool  _finished = false;
    float _progress = 0.f;
    bool  _dragging = false;
    bool  _barVisible = true;
    bool  _hovPlay  = false;

    std::vector<uint8_t> _frameCache;
    int _cachedSrcW = 0, _cachedSrcH = 0;

    std::vector<uint8_t> _cairoPixels;
    cairo_surface_t     *_cairoSurf  = nullptr;
    int                  _cairoSurfW = 0, _cairoSurfH = 0;

    struct Rect { int x, y, w, h; };
    Rect _barRect{}, _playBtnRect{}, _trackRect{};

    TimerID _progressTimer = 0, _barHideTimer = 0;
    std::atomic<bool> _destroyed      {false};
    std::atomic<bool> _finishedPending{false};

    // ── Cairo helpers ──────────────────────────────────────────────────────
    void _rebuildCairoSurface() {
        _freeCairoSurface();
        if (_cachedSrcW <= 0 || _cachedSrcH <= 0) return;
        _cairoPixels.resize((size_t)(_cachedSrcW * _cachedSrcH * 4));
        _cairoSurf = cairo_image_surface_create_for_data(
            _cairoPixels.data(), CAIRO_FORMAT_RGB24,
            _cachedSrcW, _cachedSrcH, _cachedSrcW * 4);
        _cairoSurfW = _cachedSrcW; _cairoSurfH = _cachedSrcH;
    }

    void _freeCairoSurface() {
        if (_cairoSurf) { cairo_surface_destroy(_cairoSurf); _cairoSurf = nullptr; }
        _cairoSurfW = _cairoSurfH = 0;
    }

    void _updateCairoPixels() {
        int n = _cachedSrcW * _cachedSrcH;
        const uint8_t *src = _frameCache.data();
        uint8_t       *dst = _cairoPixels.data();
        for (int i = 0; i < n; i++) {
            dst[0] = src[2]; dst[1] = src[1]; dst[2] = src[0]; dst[3] = 0xFF;
            src += 3; dst += 4;
        }
    }

    // ── Source dispatch ────────────────────────────────────────────────────
    void _openVideoSource() {
        if (_sourceType == VideoSourceType::Url && !_sourceUrl.empty()) {
            _loadFromUrl(); return;
        }
        if (_sourceType == VideoSourceType::Memory && !_sourceMemory.empty()) {
            _playFromMemory(); return;
        }
        if (!videoPath.empty()) _openVideo();
    }

    void _loadFromUrl() {
        _netState = NetState::Loading; markNeedsPaint();
        std::weak_ptr<VideoPlayerWidget> weak = self();
        std::string url = _sourceUrl;
        FluxHttp::get(url, [weak](HttpResult result) {
            auto s = weak.lock();
            if (!s) return;
            if (!result.success || result.body.empty()) {
                s->_netState = NetState::Error; s->markNeedsPaint(); return;
            }
            const auto *d = reinterpret_cast<const uint8_t *>(result.body.data());
            s->_sourceMemory.assign(d, d + result.body.size());
            s->_netState = NetState::Idle;
            s->_playFromMemory();
        });
    }

    void _playFromMemory() {
        if (_sourceMemory.empty()) return;
        std::string ext  = VP_detectVideoExtension(_sourceMemory);
        std::string path = VP_writeTempFile(_sourceMemory, ext);
        if (path.empty()) { _netState = NetState::Error; markNeedsPaint(); return; }
        videoPath = path;
        _openVideo();
    }

    void _openVideo() {
        FluxVideo::get().open(videoPath);
        if (autoPlay) { FluxVideo::get().play(); _playing = true; _startProgressTimer(); }
    }

    void _togglePlayPause() {
        auto &vid = FluxVideo::get();
        if (_finished) {
            _finished = false; _progress = 0.f;
            vid.seekToProgress(0.f); vid.play(); _playing = true; _startProgressTimer(); return;
        }
        if (_playing) { vid.pause(); _playing = false; }
        else          { vid.play();  _playing = true; _startProgressTimer(); }
    }

    void _seekFromMouse(int mx) {
        if (_trackRect.w <= 0) return;
        float t = std::max(0.f, std::min(1.f, (float)(mx - _trackRect.x) / (float)_trackRect.w));
        _progress = t; FluxVideo::get().seekToProgress(t);
        if (_finished && t < 0.999f) {
            _finished = false; FluxVideo::get().play(); _playing = true; _startProgressTimer();
        }
        markNeedsPaint();
    }

    void _startProgressTimer() {
        if (_progressTimer) return;
        _progressTimer = FluxUI::getCurrentInstance()->setInterval(33, [this]() {
            if (_destroyed) return;
            if (_playing) { _progress = FluxVideo::get().getProgress(); markNeedsPaint(); }
        });
    }

    void _resetBarTimer() {
        auto *ui = FluxUI::getCurrentInstance(); if (!ui) return;
        if (_barHideTimer) { ui->clearInterval(_barHideTimer); _barHideTimer = 0; }
        _barHideTimer = ui->setInterval(3000, [this]() {
            if (_destroyed) return;
            auto *u = FluxUI::getCurrentInstance();
            if (u && _barHideTimer) { u->clearInterval(_barHideTimer); _barHideTimer = 0; }
            if (_playing) { _barVisible = false; markNeedsPaint(); }
        });
    }

    void _stopTimers() {
        auto *ui = FluxUI::getCurrentInstance(); if (!ui) return;
        if (_progressTimer) { ui->clearInterval(_progressTimer); _progressTimer = 0; }
        if (_barHideTimer)  { ui->clearInterval(_barHideTimer);  _barHideTimer  = 0; }
    }

    std::shared_ptr<VideoPlayerWidget> self() {
        return std::static_pointer_cast<VideoPlayerWidget>(shared_from_this());
    }
    bool _inWidget(int mx, int my) const {
        return mx >= x && mx < x + width && my >= y && my < y + height;
    }
    static bool _inRect(int mx, int my, const Rect &r) {
        return mx >= r.x && mx < r.x + r.w && my >= r.y && my < r.y + r.h;
    }

    static std::string _fmtTime(float s) {
        int si = (int)s; char buf[16];
        snprintf(buf, sizeof(buf), "%d:%02d", si / 60, si % 60); return buf;
    }

    void _renderStatusOverlay(Painter &p, FontCache &fontCache,
                               const std::string &msg, Color col) {
        NativeFont tf = fontCache.getFont("Sans", 14, FontWeight::Normal);
        p.drawTextA(msg, x, y, width, height, tf, col,
                    DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    void _renderBar(GraphicsContext &ctx, FontCache &fontCache, Painter &p) {
        int barY = y + height - barHeight;
        _barRect = {x, barY, width, barHeight};
        p.fillRect(x, barY, width, barHeight, colBar);

        int midY = barY + barHeight / 2;
        int cx   = x + 6;
        int btnSz = 28;
        _playBtnRect = {cx, barY + (barHeight - btnSz) / 2, btnSz, btnSz};

        {
            NativeFont iconFont = fontCache.getFont(kIconFont, 18, FontWeight::Normal);
            std::wstring g(1, FluxIcons::glyph(_playing ? FluxIcons::Pause : FluxIcons::Play));
            Color c = _hovPlay ? colIconHov : colIcon;
            p.drawText(g, _playBtnRect.x, _playBtnRect.y, _playBtnRect.w, _playBtnRect.h,
                       iconFont, c, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        cx += btnSz + 6;

        float dur = FluxVideo::get().getDurationSeconds();
        std::string ts = _fmtTime(_progress * dur) + " / " + _fmtTime(dur);
        NativeFont tf = fontCache.getFont("Sans", 12, FontWeight::Normal);
        int tw = 0, th = 0;
        p.measureText(toWideString(ts), tf, tw, th);
        p.drawText(toWideString(ts), cx, barY, tw + 4, barHeight, tf, colText,
                   DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        cx += tw + 8;

        int trackW = std::max(20, x + width - 12 - cx);
        _trackRect = {cx, midY - 8, trackW, 16};
        p.fillRoundedRectGDI(cx, midY - 1, trackW, 3, 3, colTrackBg, colTrackBg, 0);
        int fillW = (int)(_progress * trackW);
        if (fillW > 0)
            p.fillRoundedRectGDI(cx, midY - 1, fillW, 3, 3, colTrackFill, colTrackFill, 0);
        p.drawEllipse(cx + fillW - 6, midY - 6, 12, 12, colThumb, colThumb, 0);
    }

    void _renderCenterPlay(Painter &p, FontCache &fontCache) {
        int cx = x + width / 2, cy = y + (height - barHeight) / 2, r = 28;
        p.drawEllipse(cx - r, cy - r, r * 2, r * 2,
                      Color::fromRGBA(0,0,0,160), Color::fromRGBA(0,0,0,0), 0);
        NativeFont iconFont = fontCache.getFont(kIconFont, 32, FontWeight::Normal);
        std::wstring g(1, FluxIcons::glyph(FluxIcons::Play));
        p.drawText(g, cx - r, cy - r, r * 2, r * 2, iconFont,
                   Color::fromRGB(255,255,255), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
};

using VideoPlayerWidgetPtr = std::shared_ptr<VideoPlayerWidget>;

inline VideoPlayerWidgetPtr VideoPlayer(const std::string &path = "") {
    auto w = std::make_shared<VideoPlayerWidget>();
    if (!path.empty()) w->setPath(path);
    return w;
}

inline VideoPlayerWidgetPtr VideoPlayerFromUrl(const std::string &url) {
    return std::make_shared<VideoPlayerWidget>()->setUrl(url);
}

inline VideoPlayerWidgetPtr VideoPlayerFromMemory(const std::vector<uint8_t> &bytes) {
    return std::make_shared<VideoPlayerWidget>()->setMemory(bytes);
}

inline VideoPlayerWidgetPtr VideoPlayerFromMemory(const uint8_t *data, size_t len) {
    return std::make_shared<VideoPlayerWidget>()->setMemory(data, len);
}

#endif // __linux__ && !__ANDROID__