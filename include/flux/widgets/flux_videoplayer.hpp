// flux_videoplayer.hpp
// Self-contained video player widget.
// Blits the FluxVideo OES texture via NanoVG each frame, overlays a browser-
// style control bar at the bottom (identical visual language to
// AudioPlayerWidget).
//
// Usage:
//   VideoPlayer("video/sample.mp4")
//       ->setWidth(480)->setHeight(270)   // 16:9 recommended
//
// The widget manages all FluxVideo state internally — app.cpp just instantiates
// it.
//
#pragma once
#ifdef __ANDROID__

#include "flux/flux.hpp"
#include "flux/flux_video.hpp"

// ── nanovg OES extension
// ────────────────────────────────────────────────────── NanoVG does not
// natively support GL_TEXTURE_EXTERNAL_OES. We supply a thin helper that wraps
// the OES texture as an NVGimage handle so nvgImagePattern() / nvgFillPaint()
// can blit it. The implementation lives in nanovg_oes_ext.cpp (see below).
extern int NVG_createImageFromOES(NVGcontext *vg, GLuint oesTexId, int w,
                                  int h);
extern void NVG_updateImageFromOES(NVGcontext *vg, int nvgImage,
                                   GLuint oesTexId);
extern NVGcontext *FluxAndroid_getVG();

// ============================================================================
// VideoPlayerWidget
// ============================================================================

class VideoPlayerWidget : public Widget {
public:
  // ── Config ────────────────────────────────────────────────────────────────
  std::string videoPath;
  int barHeight = 40;
  int pillarR = 0; // 0 = sharp corners on the video frame
  bool autoPlay = false;

  // ── Colors (same palette as AudioPlayerWidget) ────────────────────────────
  Color colBar = Color::fromRGBA(20, 20, 20, 220); // dark translucent
  Color colTrackBg = Color::fromRGB(100, 100, 100);
  Color colTrackFill = Color::fromRGB(220, 220, 220);
  Color colThumb = Color::fromRGB(255, 255, 255);
  Color colText = Color::fromRGB(230, 230, 230);
  Color colIcon = Color::fromRGB(220, 220, 220);
  Color colIconHov = Color::fromRGB(255, 255, 255);
  Color colOverlay = Color::fromRGBA(0, 0, 0, 60); // dim when bar visible

  // ── Fluent setters ────────────────────────────────────────────────────────
  std::shared_ptr<VideoPlayerWidget> setPath(const std::string &p) {
    videoPath = p;
    return self();
  }
  std::shared_ptr<VideoPlayerWidget> setWidth(int w) {
    Widget::width = w;
    autoWidth = false;
    return self();
  }
  std::shared_ptr<VideoPlayerWidget> setHeight(int h) {
    Widget::height = h;
    autoHeight = false;
    return self();
  }
  std::shared_ptr<VideoPlayerWidget> setAutoPlay(bool b) {
    autoPlay = b;
    return self();
  }

  // ── Constructor / destructor ───────────────────────────────────────────────
  VideoPlayerWidget() {
    autoWidth = false;
    autoHeight = false;
    width = 320;
    height = 180;

    auto &vid = FluxVideo::get();

    vid.setOnFinished([this]() {
      _playing = false;
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

  void computeLayout(GraphicsContext & /*ctx*/,
                     const BoxConstraints &constraints,
                     FontCache & /*fontCache*/) override {
    if (autoWidth)
      width = constraints.maxWidth;
    if (autoHeight)
      height = constraints.maxHeight;
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

  void render(GraphicsContext &ctx, FontCache &fontCache) override {
    auto &vid = FluxVideo::get();
    NVGcontext *vg = FluxAndroid_getVG();

    // ── Latch new frame into OES texture ──────────────────────────────────
    if (vid.updateFrame() && _nvgImage >= 0) {
      NVG_updateImageFromOES(vg, _nvgImage, vid.getTextureId());
      _progress = vid.getProgress();
    }

    Painter p(ctx);

    // ── Video frame ───────────────────────────────────────────────────────
    if (_nvgImage >= 0 && _vidW > 0 && _vidH > 0) {
      // Fit video into widget rect, letterbox if needed
      float vidAR = (float)_vidW / _vidH;
      float widAR = (float)width / (height - barHeight);
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

      NVGpaint imgPaint =
          nvgImagePattern(vg, dstX, dstY, dstW, dstH, 0.f, _nvgImage, 1.f);
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
    if (_barVisible)
      _renderBar(ctx, fontCache, p);

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
    if (!_inWidget(mx, my))
      return false;

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

    return true; // consume all clicks inside widget
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
    if (_dragging) {
      _seekFromMouse(mx);
      return true;
    }

    bool inW = _inWidget(mx, my);
    if (inW != _barVisible) {
      _barVisible = inW;
      markNeedsPaint();
    }
    if (inW)
      _resetBarTimer();

    bool hp = _inRect(mx, my, _playBtnRect);
    if (hp != _hovPlay) {
      _hovPlay = hp;
      markNeedsPaint();
    }
    return false;
  }

  bool handleMouseLeave() override {
    _barVisible = false;
    _hovPlay = false;
    markNeedsPaint();
    return true;
  }

private:
  // ── State ─────────────────────────────────────────────────────────────────
  bool _opened = false;
  bool _playing = false;
  bool _finished = false;
  float _progress = 0.f;
  int _vidW = 0;
  int _vidH = 0;
  int _nvgImage = -1;
  bool _dragging = false;
  bool _barVisible = true;
  bool _hovPlay = false;

  // ── Hit rects ─────────────────────────────────────────────────────────────
  struct Rect {
    int x, y, w, h;
  };
  Rect _barRect{};
  Rect _playBtnRect{};
  Rect _trackRect{};

  // ── Timers ────────────────────────────────────────────────────────────────
  TimerID _progressTimer = 0; // 33ms — update progress while playing
  TimerID _barHideTimer = 0;  // 3s   — auto-hide bar after inactivity

  void _startProgressTimer() {
    if (_progressTimer)
      return;
    _progressTimer = FluxUI::getCurrentInstance()->setInterval(33, [this]() {
      if (_playing) {
        _progress = FluxVideo::get().getProgress();
        markNeedsPaint();
      }
    });
  }

  void _stopTimer() {
    auto *ui = FluxUI::getCurrentInstance();
    if (!ui)
      return;
    if (_progressTimer) {
      ui->clearInterval(_progressTimer);
      _progressTimer = 0;
    }
    if (_barHideTimer) {
      ui->clearInterval(_barHideTimer);
      _barHideTimer = 0;
    }
  }

  void _resetBarTimer() {
    auto *ui = FluxUI::getCurrentInstance();
    if (!ui)
      return;
    if (_barHideTimer) {
      ui->clearInterval(_barHideTimer);
      _barHideTimer = 0;
    }
    // Simulate setTimeout: setInterval fires every 3000ms; we cancel after
    // the first tick, making it a one-shot delay.
    _barHideTimer = ui->setInterval(3000, [this]() {
      auto *u = FluxUI::getCurrentInstance();
      if (u && _barHideTimer) {
        u->clearInterval(_barHideTimer);
        _barHideTimer = 0;
      }
      if (_playing) {
        _barVisible = false;
        markNeedsPaint();
      }
    });
  }

  // ── Helpers ───────────────────────────────────────────────────────────────
  std::shared_ptr<VideoPlayerWidget> self() {
    return std::static_pointer_cast<VideoPlayerWidget>(shared_from_this());
  }
  bool _inWidget(int mx, int my) const {
    return mx >= x && mx < x + width && my >= y && my < y + height;
  }
  static bool _inRect(int mx, int my, const Rect &r) {
    return mx >= r.x && mx < r.x + r.w && my >= r.y && my < r.y + r.h;
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
    NVGcontext *vg = FluxAndroid_getVG();
    if (!vg || _vidW <= 0)
      return;
    if (_nvgImage >= 0)
      nvgDeleteImage(vg, _nvgImage);
    _nvgImage = NVG_createImageFromOES(vg, FluxVideo::get().getTextureId(),
                                       _vidW, _vidH);
    VIDEO_LOGI("NVG OES image created: id=%d %dx%d", _nvgImage, _vidW, _vidH);
  }

  // ── Play / Pause toggle ───────────────────────────────────────────────────
  void _togglePlayPause() {
    auto &vid = FluxVideo::get();

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
    if (_trackRect.w <= 0)
      return;
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
  void _renderBar(GraphicsContext &ctx, FontCache &fontCache, Painter &p) {
    int barY = y + height - barHeight;
    _barRect = {x, barY, width, barHeight};

    // Dark translucent background
    p.fillRect(x, barY, width, barHeight, colBar);

    int midY = barY + barHeight / 2;
    int cx = x + 6;

    // ── Play / Pause button ───────────────────────────────────────────────
    int btnSz = 28;
    _playBtnRect = {cx, barY + (barHeight - btnSz) / 2, btnSz, btnSz};
    Color iconCol = _hovPlay ? colIconHov : colIcon;

    if (_playing) {
      int bw = 3, bh = 10, gap = 3;
      int bx = _playBtnRect.x + (btnSz - bw * 2 - gap) / 2;
      int by = _playBtnRect.y + (btnSz - bh) / 2;
      p.fillRect(bx, by, bw, bh, iconCol);
      p.fillRect(bx + bw + gap, by, bw, bh, iconCol);
    } else {
      int tx = _playBtnRect.x + (btnSz - 10) / 2 + 1;
      int ty = _playBtnRect.y + (btnSz - 14) / 2;
      for (int row = 0; row < 14; row++) {
        int half = row < 7 ? row : 13 - row;
        p.fillRect(tx + row, ty + 7 - half, 1, half * 2 + 1, iconCol);
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
    int trackLeft = cx;
    int trackRight = x + width - rightReserve;
    int trackW = std::max(20, trackRight - trackLeft);
    int trackH = 3;
    _trackRect = {trackLeft, midY - 8, trackW, 16};

    p.fillRoundedRectGDI(trackLeft, midY - trackH / 2, trackW, trackH, trackH,
                         colTrackBg, colTrackBg, 0);

    int fillW = (int)(_progress * trackW);
    if (fillW > 0)
      p.fillRoundedRectGDI(trackLeft, midY - trackH / 2, fillW, trackH, trackH,
                           colTrackFill, colTrackFill, 0);

    // Thumb
    int thumbR = 6;
    int thumbX = trackLeft + fillW;
    p.drawEllipse(thumbX - thumbR, midY - thumbR, thumbR * 2, thumbR * 2,
                  colThumb, colThumb, 0);
  }

  // ── Center play icon overlay ───────────────────────────────────────────────
  void _renderCenterPlay(Painter &p) {
    int cx = x + width / 2;
    int cy = y + (height - barHeight) / 2;
    int r = 28;

    // Circle background
    p.drawEllipse(cx - r, cy - r, r * 2, r * 2, Color::fromRGBA(0, 0, 0, 160),
                  Color::fromRGBA(0, 0, 0, 0), 0);

    // Play triangle
    Color white = Color::fromRGB(255, 255, 255);
    int tx = cx - 7 + 2;
    int ty = cy - 10;
    for (int row = 0; row < 20; row++) {
      int half = row < 10 ? row : 19 - row;
      p.fillRect(tx + row, ty + 10 - half, 1, half * 2 + 1, white);
    }
  }
};

using VideoPlayerWidgetPtr = std::shared_ptr<VideoPlayerWidget>;

// ── Factory
// ───────────────────────────────────────────────────────────────────
inline VideoPlayerWidgetPtr VideoPlayer(const std::string &path = "") {
  auto w = std::make_shared<VideoPlayerWidget>();
  if (!path.empty())
    w->setPath(path);
  return w;
}

#endif // __ANDROID__

// ============================================================================
// VideoPlayerWidget — Windows (GDI backend)
// ============================================================================
#ifdef _WIN32

#include "flux/flux.hpp"
#include "flux/flux_video.hpp"

class VideoPlayerWidget : public Widget {
public:
  std::string videoPath;
  int barHeight = 40;
  bool autoPlay = false;

  // Same color palette as AudioPlayerWidget
  Color colBar = Color::fromRGBA(20, 20, 20, 220);
  Color colTrackBg = Color::fromRGB(100, 100, 100);
  Color colTrackFill = Color::fromRGB(220, 220, 220);
  Color colThumb = Color::fromRGB(255, 255, 255);
  Color colText = Color::fromRGB(230, 230, 230);
  Color colIcon = Color::fromRGB(220, 220, 220);
  Color colIconHov = Color::fromRGB(255, 255, 255);
  Color colBg = Color::fromRGB(0, 0, 0);

  std::shared_ptr<VideoPlayerWidget> setPath(const std::string &p) {
    videoPath = p;
    return self();
  }
  std::shared_ptr<VideoPlayerWidget> setWidth(int w) {
    Widget::width = w;
    autoWidth = false;
    return self();
  }
  std::shared_ptr<VideoPlayerWidget> setHeight(int h) {
    Widget::height = h;
    autoHeight = false;
    return self();
  }
  std::shared_ptr<VideoPlayerWidget> setAutoPlay(bool b) {
    autoPlay = b;
    return self();
  }

  VideoPlayerWidget() {
    autoWidth = false;
    autoHeight = false;
    width = 320;
    height = 180;

    FluxVideo::get().setOnFinished([this]() {
      _finishedPending = true;

      if (auto *ui = FluxUI::getCurrentInstance())
        ui->invalidateWidget(x, y, width, height);
    });

    FluxVideo::get().setOnReady(
        [this](int /*w*/, int /*h*/) { markNeedsPaint(); });
  }

  ~VideoPlayerWidget() {
    _destroyed = true;
    _stopTimers();
    FluxVideo::get().close();
    FluxVideo::get().setOnFinished(nullptr);
    FluxVideo::get().setOnReady(nullptr);
  }

  // ── Layout ────────────────────────────────────────────────────────────────
  void computeLayout(GraphicsContext & /*ctx*/,
                     const BoxConstraints &constraints,
                     FontCache & /*fontCache*/) override {
    if (autoWidth)
      width = constraints.maxWidth;
    if (autoHeight)
      height = constraints.maxHeight;
    applyConstraints();
    needsLayout = false;

    if (!_opened && !videoPath.empty()) {
      _opened = true;
      FluxVideo::get().open(videoPath);
      if (autoPlay) {
        FluxVideo::get().play();
        _playing = true;
        _startProgressTimer();
      }
    }
  }

  // ── Render ────────────────────────────────────────────────────────────────
  void render(GraphicsContext &ctx, FontCache &fontCache) override {

    if (_finishedPending.exchange(false)) {
      _playing = false;
      _finished = true;
      _progress = 1.f;
    }

    Painter p(ctx);
    p.fillRect(x, y, width, height, colBg);

    auto letterbox = [&](int srcW, int srcH, int &dstX, int &dstY, int &dstW,
                         int &dstH) {
      float vidAR = (float)srcW / (float)srcH;
      float widAR = (float)width / (float)(height - barHeight);
      if (vidAR > widAR) {
        dstW = width;
        dstH = (int)(dstW / vidAR);
        dstX = x;
        dstY = y + (height - barHeight - dstH) / 2;
      } else {
        dstH = height - barHeight;
        dstW = (int)(dstH * vidAR);
        dstX = x + (width - dstW) / 2;
        dstY = y;
      }
    };

    if (FluxVideo::get().hasNewFrame()) {
      auto frame = FluxVideo::get().lockFrame();

      if (frame.data && frame.width > 0 && frame.height > 0) {

        int byteCount = frame.stride * frame.height;
        _frameCache.assign(frame.data, frame.data + byteCount);
        _cachedSrcW = frame.width;
        _cachedSrcH = frame.height;

        _cachedBmi = {};
        _cachedBmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        _cachedBmi.bmiHeader.biWidth = frame.width;
        _cachedBmi.bmiHeader.biHeight = -frame.height;
        _cachedBmi.bmiHeader.biPlanes = 1;
        _cachedBmi.bmiHeader.biBitCount = 24;
        _cachedBmi.bmiHeader.biCompression = BI_RGB;
      }

      _progress = FluxVideo::get().getProgress();
    }

    if (!_frameCache.empty() && _cachedSrcW > 0) {
      int dstX, dstY, dstW, dstH;
      letterbox(_cachedSrcW, _cachedSrcH, dstX, dstY, dstW, dstH);
      ::SetStretchBltMode(ctx.hdc, HALFTONE);
      ::SetBrushOrgEx(ctx.hdc, 0, 0, nullptr);
      ::StretchDIBits(ctx.hdc, dstX, dstY, dstW, dstH, 0, 0, _cachedSrcW,
                      _cachedSrcH, _frameCache.data(), &_cachedBmi,
                      DIB_RGB_COLORS, SRCCOPY);
    }

    if (_barVisible)
      _renderBar(ctx, fontCache, p);
    if (!_playing && _barVisible)
      _renderCenterPlay(p);
    needsPaint = false;
  }

  // ── Mouse events — identical logic to Android widget ─────────────────────
  bool handleMouseDown(int mx, int my) override {
    if (!_inWidget(mx, my))
      return false;
    if (!_inRect(mx, my, _barRect)) {
      _barVisible = true;
      _resetBarTimer();
      _togglePlayPause();
      markNeedsPaint();
      return true;
    }
    if (_inRect(mx, my, _playBtnRect)) {
      _togglePlayPause();
      markNeedsPaint();
      return true;
    }
    if (_inRect(mx, my, _trackRect)) {
      _dragging = true;
      FluxUI::getCurrentInstance()->captureMouseInput();
      _seekFromMouse(mx);
      markNeedsPaint();
      return true;
    }
    return true;
  }

  bool handleMouseUp(int /*mx*/, int /*my*/) override {
    if (_dragging) {
      _dragging = false;
      FluxUI::getCurrentInstance()->releaseMouseInput();
    }
    return false;
  }

  bool handleMouseMove(int mx, int my) override {
    if (_dragging) {
      _seekFromMouse(mx);
      return true;
    }
    bool inW = _inWidget(mx, my);
    if (inW != _barVisible) {
      _barVisible = inW;
      markNeedsPaint();
    }
    if (inW)
      _resetBarTimer();
    bool hp = _inRect(mx, my, _playBtnRect);
    if (hp != _hovPlay) {
      _hovPlay = hp;
      markNeedsPaint();
    }
    return false;
  }

  bool handleMouseLeave() override {
    _barVisible = false;
    _hovPlay = false;
    markNeedsPaint();
    return true;
  }

private:
  bool _opened = false;
  bool _playing = false;
  bool _finished = false;
  float _progress = 0.f;
  bool _dragging = false;
  bool _barVisible = true;
  bool _hovPlay = false;

  std::vector<uint8_t> _frameCache;
  BITMAPINFO _cachedBmi{};
  int _cachedSrcW = 0;
  int _cachedSrcH = 0;

  struct Rect {
    int x, y, w, h;
  };
  Rect _barRect{}, _playBtnRect{}, _trackRect{};

  TimerID _progressTimer = 0;
  TimerID _barHideTimer = 0;

  std::atomic<bool> _destroyed{false};
  std::atomic<bool> _finishedPending{false};

  std::shared_ptr<VideoPlayerWidget> self() {
    return std::static_pointer_cast<VideoPlayerWidget>(shared_from_this());
  }
  bool _inWidget(int mx, int my) const {
    return mx >= x && mx < x + width && my >= y && my < y + height;
  }
  static bool _inRect(int mx, int my, const Rect &r) {
    return mx >= r.x && mx < r.x + r.w && my >= r.y && my < r.y + r.h;
  }

  void _startProgressTimer() {
    if (_progressTimer)
      return;
    _progressTimer = FluxUI::getCurrentInstance()->setInterval(33, [this]() {
      if (_destroyed)
        return;
      if (_playing) {
        _progress = FluxVideo::get().getProgress();
        markNeedsPaint();
      }
    });
  }

  void _resetBarTimer() {
    auto *ui = FluxUI::getCurrentInstance();
    if (!ui)
      return;
    if (_barHideTimer) {
      ui->clearInterval(_barHideTimer);
      _barHideTimer = 0;
    }
    _barHideTimer = ui->setInterval(3000, [this]() {
      if (_destroyed)
        return;
      auto *u = FluxUI::getCurrentInstance();
      if (u && _barHideTimer) {
        u->clearInterval(_barHideTimer);
        _barHideTimer = 0;
      }
      if (_playing) {
        _barVisible = false;
        markNeedsPaint();
      }
    });
  }

  void _stopTimers() {
    auto *ui = FluxUI::getCurrentInstance();
    if (!ui)
      return;
    if (_progressTimer) {
      ui->clearInterval(_progressTimer);
      _progressTimer = 0;
    }
    if (_barHideTimer) {
      ui->clearInterval(_barHideTimer);
      _barHideTimer = 0;
    }
  }

  void _togglePlayPause() {
    auto &vid = FluxVideo::get();
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

  void _seekFromMouse(int mx) {
    if (_trackRect.w <= 0)
      return;
    float t = std::max(
        0.f, std::min(1.f, (float)(mx - _trackRect.x) / (float)_trackRect.w));
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

  void _renderBar(GraphicsContext &/*ctx*/, FontCache &fontCache, Painter &p) {
    int barY = y + height - barHeight;
    _barRect = {x, barY, width, barHeight};
    p.fillRect(x, barY, width, barHeight, colBar);

    int midY = barY + barHeight / 2;
    int cx = x + 6;
    int btnSz = 28;
    _playBtnRect = {cx, barY + (barHeight - btnSz) / 2, btnSz, btnSz};
    Color iconCol = _hovPlay ? colIconHov : colIcon;

    if (_playing) {
      int bw = 3, bh = 10, gap = 3;
      int bx = _playBtnRect.x + (btnSz - bw * 2 - gap) / 2;
      int by = _playBtnRect.y + (btnSz - bh) / 2;
      p.fillRect(bx, by, bw, bh, iconCol);
      p.fillRect(bx + bw + gap, by, bw, bh, iconCol);
    } else {
      int tx = _playBtnRect.x + (btnSz - 10) / 2 + 1;
      int ty = _playBtnRect.y + (btnSz - 14) / 2;
      for (int row = 0; row < 14; row++) {
        int half = row < 7 ? row : 13 - row;
        p.fillRect(tx + row, ty + 7 - half, 1, half * 2 + 1, iconCol);
      }
    }
    cx += btnSz + 6;

    float dur = FluxVideo::get().getDurationSeconds();
    std::string timeStr = _fmtTime(_progress * dur) + " / " + _fmtTime(dur);
    NativeFont tf = fontCache.getFont("Segoe UI", 12, FontWeight::Normal);
    int tw = 0, th = 0;
    p.measureText(toWideString(timeStr), tf, tw, th);
    p.drawText(toWideString(timeStr), cx, barY, tw + 4, barHeight, tf, colText,
               DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    cx += tw + 8;

    int trackW = std::max(20, x + width - 12 - cx);
    _trackRect = {cx, midY - 8, trackW, 16};
    p.fillRoundedRectGDI(cx, midY - 1, trackW, 3, 3, colTrackBg, colTrackBg, 0);
    int fillW = (int)(_progress * trackW);
    if (fillW > 0)
      p.fillRoundedRectGDI(cx, midY - 1, fillW, 3, 3, colTrackFill,
                           colTrackFill, 0);
    p.drawEllipse(cx + fillW - 6, midY - 6, 12, 12, colThumb, colThumb, 0);
  }

  void _renderCenterPlay(Painter &p) {
    int cx = x + width / 2;
    int cy = y + (height - barHeight) / 2;
    p.drawEllipse(cx - 28, cy - 28, 56, 56, Color::fromRGBA(0, 0, 0, 160),
                  Color::fromRGBA(0, 0, 0, 0), 0);
    int tx = cx - 5, ty = cy - 10;
    for (int row = 0; row < 20; row++) {
      int half = row < 10 ? row : 19 - row;
      p.fillRect(tx + row, ty + 10 - half, 1, half * 2 + 1,
                 Color::fromRGB(255, 255, 255));
    }
  }
};

using VideoPlayerWidgetPtr = std::shared_ptr<VideoPlayerWidget>;

inline VideoPlayerWidgetPtr VideoPlayer(const std::string &path = "") {
  auto w = std::make_shared<VideoPlayerWidget>();
  if (!path.empty())
    w->setPath(path);
  return w;
}

#endif // _WIN32

// ============================================================================
// VideoPlayerWidget — Linux (Cairo / SDL backend)
// ============================================================================
#if defined(__linux__) && !defined(__ANDROID__)
 
#include "flux/flux.hpp"
#include "flux/flux_video.hpp"
 
#include <cairo/cairo.h>
 
class VideoPlayerWidget : public Widget {
public:
  std::string videoPath;
  int  barHeight = 40;
  bool autoPlay  = false;
 
  // Same color palette as Windows / Android widgets
  Color colBar       = Color::fromRGBA( 20,  20,  20, 220);
  Color colTrackBg   = Color::fromRGB (100, 100, 100);
  Color colTrackFill = Color::fromRGB (220, 220, 220);
  Color colThumb     = Color::fromRGB (255, 255, 255);
  Color colText      = Color::fromRGB (230, 230, 230);
  Color colIcon      = Color::fromRGB (220, 220, 220);
  Color colIconHov   = Color::fromRGB (255, 255, 255);
  Color colBg        = Color::fromRGB (  0,   0,   0);
 
  std::shared_ptr<VideoPlayerWidget> setPath(const std::string &p) {
    videoPath = p;
    return self();
  }
  std::shared_ptr<VideoPlayerWidget> setWidth(int w) {
    Widget::width = w;
    autoWidth = false;
    return self();
  }
  std::shared_ptr<VideoPlayerWidget> setHeight(int h) {
    Widget::height = h;
    autoHeight = false;
    return self();
  }
  std::shared_ptr<VideoPlayerWidget> setAutoPlay(bool b) {
    autoPlay = b;
    return self();
  }
 
  VideoPlayerWidget() {
    autoWidth  = false;
    autoHeight = false;
    width  = 320;
    height = 180;
 
    FluxVideo::get().setOnFinished([this]() {
      _finishedPending = true;
      if (auto *ui = FluxUI::getCurrentInstance())
        ui->invalidateWidget(x, y, width, height);
    });
 
    FluxVideo::get().setOnReady([this](int /*w*/, int /*h*/) {
      markNeedsPaint();
    });
  }
 
  ~VideoPlayerWidget() {
    _destroyed = true;
    _stopTimers();
    FluxVideo::get().close();
    FluxVideo::get().setOnFinished(nullptr);
    FluxVideo::get().setOnReady(nullptr);
    _freeCairoSurface();
  }
 
  // ── Layout ────────────────────────────────────────────────────────────────
  void computeLayout(GraphicsContext & /*ctx*/,
                     const BoxConstraints &constraints,
                     FontCache & /*fontCache*/) override {
    if (autoWidth)  width  = constraints.maxWidth;
    if (autoHeight) height = constraints.maxHeight;
    applyConstraints();
    needsLayout = false;
 
    if (!_opened && !videoPath.empty()) {
      _opened = true;
      FluxVideo::get().open(videoPath);
      if (autoPlay) {
        FluxVideo::get().play();
        _playing = true;
        _startProgressTimer();
      }
    }
  }
 
  // ── Render ────────────────────────────────────────────────────────────────
  void render(GraphicsContext &ctx, FontCache &fontCache) override {
    if (_finishedPending.exchange(false)) {
      _playing  = false;
      _finished = true;
      _progress = 1.f;
    }
 
    Painter p(ctx);
 
    // Black letterbox background
    p.fillRect(x, y, width, height, colBg);
 
    // ── Consume new frame ─────────────────────────────────────────────────
    if (FluxVideo::get().hasNewFrame()) {
      auto frame = FluxVideo::get().lockFrame();
 
      if (frame.data && frame.width > 0 && frame.height > 0) {
        int byteCount = frame.stride * frame.height;
        _frameCache.assign(frame.data, frame.data + byteCount);
        _cachedSrcW = frame.width;
        _cachedSrcH = frame.height;
 
        // Rebuild Cairo surface whenever video dimensions change
        if (_cachedSrcW != _cairoSurfW || _cachedSrcH != _cairoSurfH)
          _rebuildCairoSurface();
      }
      _progress = FluxVideo::get().getProgress();
    }
 
    // ── Cairo blit ────────────────────────────────────────────────────────
    // FFmpeg delivers packed RGB24 (3 bpp); CAIRO_FORMAT_RGB24 is BGRX (4 bpp).
    // _updateCairoPixels() converts between the two formats in-place.
    if (_cairoSurf && !_frameCache.empty() && _cachedSrcW > 0 && ctx.cr) {
      _updateCairoPixels();
      cairo_surface_mark_dirty(_cairoSurf);
 
      // Compute letterbox destination rect
      float vidAR = (float)_cachedSrcW / (float)_cachedSrcH;
      float widAR = (float)width / (float)(height - barHeight);
      int dstX, dstY, dstW, dstH;
 
      if (vidAR > widAR) {
        dstW = width;
        dstH = (int)((float)dstW / vidAR);
        dstX = x;
        dstY = y + (height - barHeight - dstH) / 2;
      } else {
        dstH = height - barHeight;
        dstW = (int)((float)dstH * vidAR);
        dstX = x + (width - dstW) / 2;
        dstY = y;
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
 
    if (_barVisible)
      _renderBar(ctx, fontCache, p);
    if (!_playing && _barVisible)
      _renderCenterPlay(p);
 
    needsPaint = false;
  }
 
  // ── Mouse events — identical logic to Windows widget ─────────────────────
  bool handleMouseDown(int mx, int my) override {
    if (!_inWidget(mx, my))
      return false;
    if (!_inRect(mx, my, _barRect)) {
      _barVisible = true;
      _resetBarTimer();
      _togglePlayPause();
      markNeedsPaint();
      return true;
    }
    if (_inRect(mx, my, _playBtnRect)) {
      _togglePlayPause();
      markNeedsPaint();
      return true;
    }
    if (_inRect(mx, my, _trackRect)) {
      _dragging = true;
      FluxUI::getCurrentInstance()->captureMouseInput();
      _seekFromMouse(mx);
      markNeedsPaint();
      return true;
    }
    return true;
  }
 
  bool handleMouseUp(int /*mx*/, int /*my*/) override {
    if (_dragging) {
      _dragging = false;
      FluxUI::getCurrentInstance()->releaseMouseInput();
    }
    return false;
  }
 
  bool handleMouseMove(int mx, int my) override {
    if (_dragging) {
      _seekFromMouse(mx);
      return true;
    }
    bool inW = _inWidget(mx, my);
    if (inW != _barVisible) {
      _barVisible = inW;
      markNeedsPaint();
    }
    if (inW)
      _resetBarTimer();
    bool hp = _inRect(mx, my, _playBtnRect);
    if (hp != _hovPlay) {
      _hovPlay = hp;
      markNeedsPaint();
    }
    return false;
  }
 
  bool handleMouseLeave() override {
    _barVisible = false;
    _hovPlay    = false;
    markNeedsPaint();
    return true;
  }
 
private:
  bool  _opened    = false;
  bool  _playing   = false;
  bool  _finished  = false;
  float _progress  = 0.f;
  bool  _dragging  = false;
  bool  _barVisible = true;
  bool  _hovPlay   = false;
 
  // ── Frame cache ───────────────────────────────────────────────────────────
  // Packed RGB24 from FFmpeg (3 bpp × w × h)
  std::vector<uint8_t> _frameCache;
  int _cachedSrcW = 0;
  int _cachedSrcH = 0;
 
  // BGRX pixel store for Cairo (CAIRO_FORMAT_RGB24 = 4 bpp)
  std::vector<uint8_t> _cairoPixels;
  cairo_surface_t     *_cairoSurf  = nullptr;
  int                  _cairoSurfW = 0;
  int                  _cairoSurfH = 0;
 
  struct Rect { int x, y, w, h; };
  Rect _barRect{}, _playBtnRect{}, _trackRect{};
 
  TimerID _progressTimer = 0;
  TimerID _barHideTimer  = 0;
 
  std::atomic<bool> _destroyed      {false};
  std::atomic<bool> _finishedPending{false};
 
  // ── Cairo surface helpers ─────────────────────────────────────────────────
 
  // Build a Cairo image surface backed by _cairoPixels.
  // Called once per video (or on resolution change).
  void _rebuildCairoSurface() {
    _freeCairoSurface();
    if (_cachedSrcW <= 0 || _cachedSrcH <= 0) return;
 
    _cairoPixels.resize((size_t)(_cachedSrcW * _cachedSrcH * 4));
 
    // cairo_image_surface_create_for_data does NOT copy — it references the
    // buffer directly, so _cairoPixels must stay alive for the surface's life.
    _cairoSurf = cairo_image_surface_create_for_data(
        _cairoPixels.data(),
        CAIRO_FORMAT_RGB24,     // 0xffRRGGBB stored as B G R X (little-endian)
        _cachedSrcW, _cachedSrcH,
        _cachedSrcW * 4);       // stride
 
    _cairoSurfW = _cachedSrcW;
    _cairoSurfH = _cachedSrcH;
  }
 
  void _freeCairoSurface() {
    if (_cairoSurf) {
      cairo_surface_destroy(_cairoSurf);
      _cairoSurf  = nullptr;
      _cairoSurfW = 0;
      _cairoSurfH = 0;
    }
  }
 
  // Convert packed RGB24 (_frameCache) → BGRX (_cairoPixels).
  // Must be called every time a new frame lands, before cairo_surface_mark_dirty.
  void _updateCairoPixels() {
    int n = _cachedSrcW * _cachedSrcH;
    const uint8_t *src = _frameCache.data();
    uint8_t       *dst = _cairoPixels.data();
    for (int i = 0; i < n; i++) {
      // FFmpeg RGB24: R G B
      // Cairo RGB24 (little-endian): B G R X
      dst[0] = src[2]; // B
      dst[1] = src[1]; // G
      dst[2] = src[0]; // R
      dst[3] = 0xFF;   // X (padding)
      src += 3;
      dst += 4;
    }
  }
 
  // ── Shared helpers ────────────────────────────────────────────────────────
  std::shared_ptr<VideoPlayerWidget> self() {
    return std::static_pointer_cast<VideoPlayerWidget>(shared_from_this());
  }
  bool _inWidget(int mx, int my) const {
    return mx >= x && mx < x + width && my >= y && my < y + height;
  }
  static bool _inRect(int mx, int my, const Rect &r) {
    return mx >= r.x && mx < r.x + r.w && my >= r.y && my < r.y + r.h;
  }
 
  void _startProgressTimer() {
    if (_progressTimer) return;
    _progressTimer = FluxUI::getCurrentInstance()->setInterval(33, [this]() {
      if (_destroyed) return;
      if (_playing) {
        _progress = FluxVideo::get().getProgress();
        markNeedsPaint();
      }
    });
  }
 
  void _resetBarTimer() {
    auto *ui = FluxUI::getCurrentInstance();
    if (!ui) return;
    if (_barHideTimer) { ui->clearInterval(_barHideTimer); _barHideTimer = 0; }
    _barHideTimer = ui->setInterval(3000, [this]() {
      if (_destroyed) return;
      auto *u = FluxUI::getCurrentInstance();
      if (u && _barHideTimer) { u->clearInterval(_barHideTimer); _barHideTimer = 0; }
      if (_playing) { _barVisible = false; markNeedsPaint(); }
    });
  }
 
  void _stopTimers() {
    auto *ui = FluxUI::getCurrentInstance();
    if (!ui) return;
    if (_progressTimer) { ui->clearInterval(_progressTimer); _progressTimer = 0; }
    if (_barHideTimer)  { ui->clearInterval(_barHideTimer);  _barHideTimer  = 0; }
  }
 
  void _togglePlayPause() {
    auto &vid = FluxVideo::get();
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
 
  void _seekFromMouse(int mx) {
    if (_trackRect.w <= 0) return;
    float t = std::max(0.f, std::min(1.f,
        (float)(mx - _trackRect.x) / (float)_trackRect.w));
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
 
  // ── Control bar ───────────────────────────────────────────────────────────
  void _renderBar(GraphicsContext &ctx, FontCache &fontCache, Painter &p) {
    int barY = y + height - barHeight;
    _barRect = {x, barY, width, barHeight};
    p.fillRect(x, barY, width, barHeight, colBar);
 
    int midY = barY + barHeight / 2;
    int cx   = x + 6;
    int btnSz = 28;
    _playBtnRect = {cx, barY + (barHeight - btnSz) / 2, btnSz, btnSz};
    Color iconCol = _hovPlay ? colIconHov : colIcon;
 
    if (_playing) {
      int bw = 3, bh = 10, gap = 3;
      int bx = _playBtnRect.x + (btnSz - bw * 2 - gap) / 2;
      int by = _playBtnRect.y + (btnSz - bh) / 2;
      p.fillRect(bx,           by, bw, bh, iconCol);
      p.fillRect(bx + bw + gap, by, bw, bh, iconCol);
    } else {
      int tx = _playBtnRect.x + (btnSz - 10) / 2 + 1;
      int ty = _playBtnRect.y + (btnSz - 14) / 2;
      for (int row = 0; row < 14; row++) {
        int half = row < 7 ? row : 13 - row;
        p.fillRect(tx + row, ty + 7 - half, 1, half * 2 + 1, iconCol);
      }
    }
    cx += btnSz + 6;
 
    float dur = FluxVideo::get().getDurationSeconds();
    std::string timeStr = _fmtTime(_progress * dur) + " / " + _fmtTime(dur);
    NativeFont tf = fontCache.getFont("Sans", 12, FontWeight::Normal);
    int tw = 0, th = 0;
    p.measureText(toWideString(timeStr), tf, tw, th);
    p.drawText(toWideString(timeStr), cx, barY, tw + 4, barHeight, tf, colText,
               DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    cx += tw + 8;
 
    int trackW = std::max(20, x + width - 12 - cx);
    _trackRect = {cx, midY - 8, trackW, 16};
    p.fillRoundedRectGDI(cx, midY - 1, trackW, 3, 3, colTrackBg,   colTrackBg,   0);
    int fillW = (int)(_progress * trackW);
    if (fillW > 0)
      p.fillRoundedRectGDI(cx, midY - 1, fillW, 3, 3, colTrackFill, colTrackFill, 0);
    p.drawEllipse(cx + fillW - 6, midY - 6, 12, 12, colThumb, colThumb, 0);
  }
 
  // ── Center play overlay ───────────────────────────────────────────────────
  void _renderCenterPlay(Painter &p) {
    int cx = x + width  / 2;
    int cy = y + (height - barHeight) / 2;
    p.drawEllipse(cx - 28, cy - 28, 56, 56,
                  Color::fromRGBA(0, 0, 0, 160),
                  Color::fromRGBA(0, 0, 0,   0), 0);
    int tx = cx - 5, ty = cy - 10;
    for (int row = 0; row < 20; row++) {
      int half = row < 10 ? row : 19 - row;
      p.fillRect(tx + row, ty + 10 - half, 1, half * 2 + 1,
                 Color::fromRGB(255, 255, 255));
    }
  }
};
 
using VideoPlayerWidgetPtr = std::shared_ptr<VideoPlayerWidget>;
 
inline VideoPlayerWidgetPtr VideoPlayer(const std::string &path = "") {
  auto w = std::make_shared<VideoPlayerWidget>();
  if (!path.empty())
    w->setPath(path);
  return w;
}
 
#endif // __linux__ && !__ANDROID__