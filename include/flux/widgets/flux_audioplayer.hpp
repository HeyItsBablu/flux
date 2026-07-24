// flux_audioplayer.hpp
// Drop-in, self-contained audio player widget that mirrors the browser <audio>
// control aesthetic.
//
// Rewritten for Step 3 of the audio-engine migration:
//   - Native platforms (Windows/Linux/macOS/Android) now play through the
//     shared AudioEngine (flux_audio_engine.hpp) instead of the old
//     one-voice-per-app FluxAudio singleton. Every widget instance gets its
//     own VoiceHandle, so multiple players can run independently and
//     destroying one no longer kills audio in every other widget.
//   - Memory-sourced audio no longer round-trips through a temp file —
//     AudioEngine::loadSampleFromMemory() decodes directly.
//   - Path-sourced audio is decoded once (cached as a SampleID) rather than
//     redecoded on every play().
//   - Web (__EMSCRIPTEN__) and SSR (FLUX_SSR) still use the real <audio>
//     DOM element path — miniaudio-under-Emscripten hasn't been verified
//     yet, so that branch is left as-is per the migration plan and is
//     orthogonal to everything below.
//
#pragma once

#include "flux/flux.hpp"
#include "flux/flux_audio_engine.hpp"
#include "flux/flux_http.hpp"
#include "flux/flux_icons.hpp"
#include "flux_image.hpp"

#if defined(__EMSCRIPTEN__) || defined(FLUX_SSR)
#include "flux/flux_dom_adapter.hpp"
#endif

#if defined(FLUX_SSR)
inline std::string AP_resolveSsrAssetUrl(const std::string &localPath)
{
  return std::string("/assets/") + localPath;
}
#endif

// ============================================================================
// Helpers
// ============================================================================

static std::string AP_formatTime(float secs)
{
  if (secs < 0.f)
    secs = 0.f;
  int s = (int)secs;
  char buf[16];
  snprintf(buf, sizeof(buf), "%d:%02d", s / 60, s % 60);
  return buf;
}

// ============================================================================
// AudioSource — describes where audio comes from
// ============================================================================

enum class AudioSourceType
{
  None,
  Path,
  Url,
  Memory
};

// ============================================================================
// AudioPlayerWidget
// ============================================================================

class AudioPlayerWidget : public Widget
{
public:
  // ── Colours ──────────────────────────────────────────────────────────────
  Color colBackground = Color::fromRGB(240, 240, 240);
  Color colTrackBg = Color::fromRGB(180, 180, 180);
  Color colTrackFill = Color::fromRGB(90, 90, 90);
  Color colThumb = Color::fromRGB(90, 90, 90);
  Color colThumbHover = Color::fromRGB(50, 50, 50);
  Color colText = Color::fromRGB(60, 60, 60);
  Color colIconNormal = Color::fromRGB(60, 60, 60);
  Color colIconHover = Color::fromRGB(20, 20, 20);
  Color colBorder = Color::fromRGB(210, 210, 210);
  Color colLoadingText = Color::fromRGB(140, 140, 140);
  Color colErrorText = Color::fromRGB(180, 60, 60);

  Color colSpinnerTrack = Color::fromRGBA(90, 90, 90, 40);
  Color colSpinnerProgress = Color::fromRGB(90, 90, 90);

  int spinnerDiameter = 18;
  int spinnerStroke = 2;

  // ── Config ────────────────────────────────────────────────────────────────
  std::string audioPath; // display-only now; not used as a scratch path
  int playerHeight = 40;
  int pillarRadius = 20;
  int trackHeight = 3;
  int thumbRadius = 6;
  int playBtnSize = 28;
  int iconFontSize = 14;
  int timeFontSize = 12;
  int volSliderW = 0; // 0 = no slider shown, just the mute-toggle icon

  int artworkSize = 0;

  // ── Public fluent setters ─────────────────────────────────────────────────

  std::shared_ptr<AudioPlayerWidget> setPath(const std::string &p)
  {
    audioPath = p;
    _sourceType = AudioSourceType::Path;
    _sourceUrl.clear();
    _invalidateLoadedSample();
    return self();
  }

  std::shared_ptr<AudioPlayerWidget> setUrl(const std::string &url)
  {
    _sourceUrl = url;
    _sourceType = AudioSourceType::Url;
    audioPath.clear();
    _invalidateLoadedSample();
    return self();
  }

  std::shared_ptr<AudioPlayerWidget> setMemory(const std::vector<uint8_t> &bytes)
  {
    _pendingMemory = bytes;
    _sourceType = AudioSourceType::Memory;
    audioPath.clear();
    _sourceUrl.clear();
    _invalidateLoadedSample();
    return self();
  }

  std::shared_ptr<AudioPlayerWidget> setMemory(const uint8_t *data, size_t len)
  {
    _pendingMemory.assign(data, data + len);
    _sourceType = AudioSourceType::Memory;
    audioPath.clear();
    _sourceUrl.clear();
    _invalidateLoadedSample();
    return self();
  }

  std::shared_ptr<AudioPlayerWidget> setWidth(int w)
  {
    _requestedWidth = w;
    autoWidth = false;
    markNeedsLayout();
    return self();
  }

  // Sets the non-muted playback volume (0..1). If currently unmuted and a
  // voice is playing, applies immediately. If muted, takes effect on unmute.
  std::shared_ptr<AudioPlayerWidget> setVolume(float v)
  {
    _currentVolume = std::max(0.f, std::min(1.f, v));

#if defined(__EMSCRIPTEN__) || defined(FLUX_SSR)
    if (IDomAdapter *adapter = getActiveDomAdapter())
    {
      if (_domAudioNode != kInvalidDomNode)
        adapter->setFloatProperty(_domAudioNode, "volume", _currentVolume);
      markNeedsPaint();
      return self();
    }
#endif

    if (!_muted && _voice != kInvalidVoice)
      AudioEngine::get().setVoiceGain(_voice, _currentVolume);
    markNeedsPaint();
    return self();
  }
  float getVolume() const { return _currentVolume; }

  // ── Constructor / destructor ───────────────────────────────────────────────
  AudioPlayerWidget()
  {
    height = playerHeight;
    autoHeight = false;
    autoWidth = true;
    isFocusable = false;
  }

  ~AudioPlayerWidget()
  {
    _stopTimer();
    // Only ever touches this widget's own voice — never global engine state.
    // This is the fix for the old bug where any player's destructor killed
    // playback for every other player in the app.
    if (_voice != kInvalidVoice)
      AudioEngine::get().stopVoice(_voice);
  }

  // =========================================================================
  // Layout
  // =========================================================================

  void computeLayout(GraphicsContext & /*ctx*/, const BoxConstraints &constraints,
                     FontCache & /*fontCache*/) override
  {
    if (autoWidth)
      width = constraints.maxWidth;
    else
      width = std::min(_requestedWidth, constraints.maxWidth);

    height = std::max(playerHeight, artworkSize);
    applyConstraints();
    needsLayout = false;
  }

  // =========================================================================
  // Render
  // =========================================================================

  void render(GraphicsContext &ctx, FontCache &fontCache) override
  {
#if !defined(__EMSCRIPTEN__) && !defined(FLUX_SSR)
    // Pull live progress from the engine every frame while playing.
    if (_voice != kInvalidVoice)
    {
      auto &engine = AudioEngine::get();
      if (!engine.isVoiceActive(_voice))
      {
        // Voice finished naturally (reached end, not looping).
        if (_playing)
        {
          _playing = false;
          _finished = true;
          _progress = 1.f;
          _stopTimer();
        }
      }
      else
      {
        _progress = engine.getVoiceProgress(_voice);
      }
    }
#endif

    Painter p(ctx, this);

#if defined(__EMSCRIPTEN__) || defined(FLUX_SSR)
    // ── Real <audio> element — unchanged from the previous implementation.
    // See the migration notes at the top of this file: this path stays as-is
    // until miniaudio-under-Emscripten is verified.
    if (IDomAdapter *adapter = getActiveDomAdapter())
    {
      DomNodeHandle anode = fluxDomEnsureNode(this, "audio", "audioEl");
      adapter->setStyle(anode, "display", "none");
      adapter->bindMediaEvents(anode, this);
      _domAudioNode = anode;

      std::string resolvedUrl;
      if (_sourceType == AudioSourceType::Url && !_sourceUrl.empty())
        resolvedUrl = _sourceUrl;
#if defined(FLUX_SSR)
      else if (_sourceType == AudioSourceType::Path && !audioPath.empty())
        resolvedUrl = AP_resolveSsrAssetUrl(audioPath);
#endif
      if (!resolvedUrl.empty() && resolvedUrl != _domAudioSrcApplied)
      {
        adapter->setAttr(anode, "src", resolvedUrl);
        _domAudioSrcApplied = resolvedUrl;
      }
    }
#endif

    // ── Pill background ──────────────────────────────────────────────────
    p.fillRoundedRectGDI(x, y, width, height, pillarRadius * 2, colBackground,
                         colBorder, 1, "pillBg");

    int cx = x;
    int midY = y + height / 2;

    if (_netState == NetState::Loading)
    {
      _renderLoadingSpinner(p, cx, midY);
      needsPaint = false;
      return;
    }
    if (_netState == NetState::Error)
    {
      _renderErrorIcon(p, cx, midY);
      needsPaint = false;
      return;
    }

    // ── Play / Pause button ────────────────────────────────────────────────
    cx += 6;
    int btnX = cx;
    int btnY = y + (height - playBtnSize) / 2;
    _playBtnRect = {btnX, btnY, playBtnSize, playBtnSize};

    Color btnBg = _hovPlay ? Color::fromRGB(210, 210, 210) : colBackground;
    p.fillRoundedRectGDI(btnX, btnY, playBtnSize, playBtnSize, playBtnSize,
                         btnBg, btnBg, 0, "playBtnBg");

    {
      Color iconCol = _hovPlay ? colIconHover : colIconNormal;
      NativeFont iconFont = fontCache.getFont(kIconFont, iconFontSize, FontWeight::Normal);
      wchar_t glyph = FluxIcons::glyph(_playing ? FluxIcons::Pause : FluxIcons::Play);
      std::wstring glyphStr(1, glyph);
      p.drawText(glyphStr, btnX, btnY, playBtnSize, playBtnSize,
                 iconFont, iconCol, DT_CENTER | DT_VCENTER | DT_SINGLELINE, "playIcon");
    }

    cx += playBtnSize + 6;

    // ── Time display ─────────────────────────────────────────────────────
#if defined(__EMSCRIPTEN__) || defined(FLUX_SSR)
    float dur = _domAudioDuration;
#else
    float dur = (_sampleId != kInvalidSample)
                    ? AudioEngine::get().getSampleDurationSeconds(_sampleId)
                    : 0.f;
#endif
    float pos = _progress * dur;
    std::string timeStr = AP_formatTime(pos) + " / " + AP_formatTime(dur);

    NativeFont timeFont = fontCache.getFont("Segoe UI", timeFontSize, FontWeight::Normal);
    int tw = 0, th = 0;
    p.measureText(toWideString(timeStr), timeFont, tw, th);
    p.drawText(toWideString(timeStr), cx, y, tw + 4, height, timeFont, colText,
               DT_LEFT | DT_VCENTER | DT_SINGLELINE, "timestamp");
    cx += tw + 8;

    // ── Seek track ───────────────────────────────────────────────────────
    int sliderReserve = (volSliderW > 0) ? (volSliderW + 8) : 0;
    int rightReserve = 48 + sliderReserve;
    int trackLeft = cx;
    int trackRight = x + width - rightReserve;
    int trackW = std::max(20, trackRight - trackLeft);

    _trackRect = {trackLeft, midY - 8, trackW, 16};

    p.fillRoundedRectGDI(trackLeft, midY - trackHeight / 2, trackW, trackHeight,
                         trackHeight, colTrackBg, colTrackBg, 0, "trackBg");

    int fillW = (int)(_progress * trackW);
    if (fillW > 0)
      p.fillRoundedRectGDI(trackLeft, midY - trackHeight / 2, fillW, trackHeight,
                           trackHeight, colTrackFill, colTrackFill, 0, "trackFill");

    int thumbX = trackLeft + fillW;
    Color thumbCol = _hovTrack ? colThumbHover : colThumb;
    p.drawEllipse(thumbX - thumbRadius, midY - thumbRadius,
                  thumbRadius * 2, thumbRadius * 2, thumbCol, thumbCol, 0, "thumb");

    cx = trackRight + 4;

    // ── Volume icon ────────────────────────────────────────────────────────
    {
      int iconW = 20, iconH = 20;
      _volIconRect = {cx, y + (height - iconH) / 2, iconW, iconH};
      Color volCol = _hovVol ? colIconHover : colIconNormal;
      NativeFont iconFont = fontCache.getFont(kIconFont, iconFontSize, FontWeight::Normal);
      wchar_t glyph = FluxIcons::glyph(_muted ? FluxIcons::Mute : FluxIcons::Volume);
      std::wstring glyphStr(1, glyph);
      p.drawText(glyphStr, _volIconRect.x, _volIconRect.y, _volIconRect.w, _volIconRect.h,
                 iconFont, volCol, DT_CENTER | DT_VCENTER | DT_SINGLELINE, "volIcon");
      cx += iconW + 4;
    }

    // ── Volume slider (only when volSliderW > 0) ───────────────────────────
    if (volSliderW > 0)
    {
      int sliderH = 4;
      _volSliderRect = {cx, midY - 8, volSliderW, 16};

      p.fillRoundedRectGDI(cx, midY - sliderH / 2, volSliderW, sliderH,
                           sliderH, colTrackBg, colTrackBg, 0, "volSliderBg");

      float displayedVol = _muted ? 0.f : _currentVolume;
      int volFillW = (int)(displayedVol * volSliderW);
      if (volFillW > 0)
        p.fillRoundedRectGDI(cx, midY - sliderH / 2, volFillW, sliderH,
                             sliderH, colTrackFill, colTrackFill, 0, "volSliderFill");

      Color volThumbCol = _hovVolSlider ? colThumbHover : colThumb;
      int vThumbX = cx + volFillW;
      p.drawEllipse(vThumbX - 4, midY - 4, 8, 8, volThumbCol, volThumbCol, 0, "volSliderThumb");

      cx += volSliderW + 8;
    }

    // ── More / dots icon ──────────────────────────────────────────────────
    {
      int iconW = 18, iconH = 20;
      _dotsIconRect = {cx, y + (height - iconH) / 2, iconW, iconH};
      Color dotsCol = _hovDots ? colIconHover : colIconNormal;
      NativeFont iconFont = fontCache.getFont(kIconFont, iconFontSize, FontWeight::Normal);
      wchar_t glyph = FluxIcons::glyph(FluxIcons::More);
      std::wstring glyphStr(1, glyph);
      p.drawText(glyphStr, _dotsIconRect.x, _dotsIconRect.y, _dotsIconRect.w, _dotsIconRect.h,
                 iconFont, dotsCol, DT_CENTER | DT_VCENTER | DT_SINGLELINE, "dotsIcon");
    }

    needsPaint = false;
  }

  // =========================================================================
  // Mouse events
  // =========================================================================

  bool handleMouseDown(int mx, int my) override
  {
    if (_inRect(mx, my, _playBtnRect))
    {
      _togglePlayPause();
      markNeedsPaint();
      return true;
    }
    if (_inRect(mx, my, _trackRect))
    {
      _dragging = true;
      FluxUI::getCurrentInstance()->captureMouseInput();
      _seekFromMouse(mx);
      markNeedsPaint();
      return true;
    }
    if (_inRect(mx, my, _volIconRect))
    {
      _toggleMute();
      markNeedsPaint();
      return true;
    }
    if (volSliderW > 0 && _inRect(mx, my, _volSliderRect))
    {
      _draggingVolSlider = true;
      FluxUI::getCurrentInstance()->captureMouseInput();
      _setVolumeFromMouse(mx);
      markNeedsPaint();
      return true;
    }
    if (_inRect(mx, my, _dotsIconRect))
    {
      _onDotsClicked();
      markNeedsPaint();
      return true;
    }
    return false;
  }

  bool handleMouseUp(int, int) override
  {
    if (_dragging)
    {
      _dragging = false;
      FluxUI::getCurrentInstance()->releaseMouseInput();
      markNeedsPaint();
      return true;
    }
    if (_draggingVolSlider)
    {
      _draggingVolSlider = false;
      FluxUI::getCurrentInstance()->releaseMouseInput();
      markNeedsPaint();
      return true;
    }
    return false;
  }

  bool handleMouseMove(int mx, int my) override
  {
    if (_draggingVolSlider)
    {
      _setVolumeFromMouse(mx);
      return true;
    }

    bool hp = _inRect(mx, my, _playBtnRect);
    bool ht = _inRect(mx, my, _trackRect);
    bool hv = _inRect(mx, my, _volIconRect);
    bool hd = _inRect(mx, my, _dotsIconRect);
    bool hvs = volSliderW > 0 && _inRect(mx, my, _volSliderRect);

    bool changed = (hp != _hovPlay || ht != _hovTrack || hv != _hovVol ||
                    hd != _hovDots || hvs != _hovVolSlider);
    _hovPlay = hp;
    _hovTrack = ht;
    _hovVol = hv;
    _hovDots = hd;
    _hovVolSlider = hvs;

    if (changed)
      markNeedsPaint();
    return changed;
  }

  bool handleMouseLeave() override
  {
    _hovPlay = _hovTrack = _hovVol = _hovDots = _hovVolSlider = false;
    markNeedsPaint();
    return true;
  }

#if defined(__EMSCRIPTEN__) || defined(FLUX_SSR)
  // Unchanged DOM event bridge for the web/SSR path.
  void onDomMediaTimeUpdate(float currentTimeSec, float durationSec) override
  {
    _domAudioDuration = durationSec;
    if (durationSec > 0.f)
      _progress = std::max(0.f, std::min(1.f, currentTimeSec / durationSec));
    _requestRepaint();
  }
  void onDomMediaPlay() override
  {
    _playing = true;
    _finished = false;
    _requestRepaint();
  }
  void onDomMediaPause() override
  {
    _playing = false;
    _requestRepaint();
  }
  void onDomMediaEnded() override
  {
    _playing = false;
    _finished = true;
    _progress = 1.f;
    _requestRepaint();
  }
#endif

private:
#if defined(__EMSCRIPTEN__) || defined(FLUX_SSR)
  void _requestRepaint()
  {
    markNeedsPaint();
    if (auto *ui = FluxUI::getCurrentInstance())
      ui->invalidateWidget(x, y, width, height);
  }
#endif

private:
  int _requestedWidth = 0;

  // ── Source ────────────────────────────────────────────────────────────────
  AudioSourceType _sourceType = AudioSourceType::None;
  std::string _sourceUrl;
  std::vector<uint8_t> _pendingMemory; // set via setMemory(), consumed on first play()

  // ── Engine-side identity ────────────────────────────────────────────────
  // _sampleId: this widget's decoded audio, cached so replay doesn't redecode.
  // _voice:    this widget's own playback instance. Never touches any other
  //            widget's voice or global engine state.
  SampleID _sampleId = kInvalidSample;
  VoiceHandle _voice = kInvalidVoice;

  enum class NetState
  {
    Idle,
    Loading,
    Error
  };
  NetState _netState = NetState::Idle;

  bool _playing = false;
  bool _finished = false;
  float _progress = 0.f;
  bool _muted = false;
  float _currentVolume = 1.f; // non-muted volume level; source of truth for mute restore + slider fill

  bool _dragging = false;
  bool _hovPlay = false, _hovTrack = false;
  bool _hovVol = false, _hovDots = false;
  bool _hovVolSlider = false;
  bool _draggingVolSlider = false;
  float _spinAngle = -1.57079632f;

  struct Rect
  {
    int x, y, w, h;
  };
  Rect _playBtnRect{}, _trackRect{}, _volIconRect{}, _volSliderRect{}, _dotsIconRect{};

  TimerID _timerId = 0;

  void _startTimer()
  {
    if (_timerId)
      return;
    _timerId = FluxUI::getCurrentInstance()->setInterval(33, [this]()
                                                         {
      if (_playing || _netState == NetState::Loading)
        markNeedsPaint(); });
  }
  void _stopTimer()
  {
    if (_timerId)
    {
      FluxUI::getCurrentInstance()->clearInterval(_timerId);
      _timerId = 0;
    }
  }

#if defined(__EMSCRIPTEN__) || defined(FLUX_SSR)
  DomNodeHandle _domAudioNode = kInvalidDomNode;
  std::string _domAudioSrcApplied;
  float _domAudioDuration = 0.f;
#endif

  std::shared_ptr<AudioPlayerWidget> self()
  {
    return std::static_pointer_cast<AudioPlayerWidget>(shared_from_this());
  }
  static bool _inRect(int mx, int my, const Rect &r)
  {
    return mx >= r.x && mx < r.x + r.w && my >= r.y && my < r.y + r.h;
  }

  // Called whenever the source changes — this widget's old voice/sample are
  // no longer valid, but nothing else in the app is affected.
  void _invalidateLoadedSample()
  {
    if (_voice != kInvalidVoice)
    {
      AudioEngine::get().stopVoice(_voice);
      _voice = kInvalidVoice;
    }
    _sampleId = kInvalidSample; // note: not unloaded from the bank here —
                                // call AudioEngine::get().unloadSample(id)
                                // yourself if you need to free memory eagerly.
    _playing = false;
    _finished = false;
    _progress = 0.f;
    _stopTimer();
  }

  void _renderLoadingSpinner(Painter &p, int barStartX, int midY)
  {
    constexpr float kTwoPi = 6.28318530f;
    constexpr float kSpinStep = 0.07f;
    float cx = float(barStartX) + float(spinnerDiameter) * 0.5f + 4.f;
    float cy = float(midY);
    float r = float(spinnerDiameter) * 0.5f - float(spinnerStroke) * 0.5f;
    p.drawArc(cx, cy, r, spinnerStroke, 0.0f, kTwoPi, colSpinnerTrack, false);
    p.drawArc(cx, cy, r, spinnerStroke, _spinAngle, kTwoPi * 0.75f, colSpinnerProgress, true);
    _spinAngle += kSpinStep;
    if (_spinAngle >= kTwoPi)
      _spinAngle -= kTwoPi;
  }

  void _renderErrorIcon(Painter &p, int barStartX, int midY)
  {
    int cx = barStartX + spinnerDiameter / 2 + 4;
    int r = spinnerDiameter / 2;
    p.drawEllipse(cx - r, midY - r, r * 2, r * 2, Color::fromRGBA(0, 0, 0, 0), colErrorText, 2, "errorRing");
    p.fillRect(cx - 2, midY - r + 4, 3, r, colErrorText, "errorStem");
    p.fillRect(cx - 2, midY + r - 5, 3, 3, colErrorText, "errorDot");
  }

  // ── Play / Pause toggle ────────────────────────────────────────────────────
  void _togglePlayPause()
  {
#if defined(__EMSCRIPTEN__) || defined(FLUX_SSR)
    if (IDomAdapter *adapter = getActiveDomAdapter())
    {
      if (_domAudioNode == kInvalidDomNode)
        return;
      if (_finished)
      {
        adapter->seekNode(_domAudioNode, 0.f);
        adapter->playNode(_domAudioNode);
        return;
      }
      if (_playing)
        adapter->pauseNode(_domAudioNode);
      else
        adapter->playNode(_domAudioNode);
      return;
    }
#endif
    auto &engine = AudioEngine::get();

    // Nothing loaded yet — kick off loading/decoding first.
    if (_sampleId == kInvalidSample)
    {
      if (_sourceType == AudioSourceType::Url && !_sourceUrl.empty())
      {
        _loadFromUrl();
        return;
      }
      if (_sourceType == AudioSourceType::Memory && !_pendingMemory.empty())
      {
        _loadFromMemory();
        return;
      }
      if (_sourceType == AudioSourceType::Path && !audioPath.empty())
      {
        _loadFromPath();
        return;
      }
      return; // nothing to play
    }

    if (_finished)
    {
      _finished = false;
      _progress = 0.f;
      if (_voice != kInvalidVoice)
        engine.stopVoice(_voice);
      _voice = engine.play(_sampleId, _muted ? 0.f : _currentVolume, 0.f, false);
      _playing = (_voice != kInvalidVoice);
      if (_playing)
        _startTimer();
      return;
    }

    if (_playing)
    {
      engine.pauseVoice(_voice);
      _playing = false;
      _stopTimer();
    }
    else if (_voice != kInvalidVoice && engine.isVoicePaused(_voice))
    {
      engine.resumeVoice(_voice);
      _playing = true;
      _startTimer();
    }
    else
    {
      _voice = engine.play(_sampleId, _muted ? 0.f : _currentVolume, 0.f, false);
      _playing = (_voice != kInvalidVoice);
      if (_playing)
        _startTimer();
    }
  }

  // ── Loading paths ──────────────────────────────────────────────────────────

  void _loadFromPath()
  {
    // Synchronous decode via AudioEngine — matches the old behaviour of
    // playFromPath() doing the decode inline. If this needs to be async for
    // large files, move it to a background thread and post the result back,
    // same pattern as _loadFromUrl() below.
    _sampleId = AudioEngine::get().loadSample(audioPath);
    if (_sampleId == kInvalidSample)
    {
      _netState = NetState::Error;
      markNeedsPaint();
      return;
    }
    _startPlaybackOfLoadedSample();
  }

  void _loadFromUrl()
  {
    _netState = NetState::Loading;
    _startTimer();
    markNeedsPaint();

    std::weak_ptr<AudioPlayerWidget> weak = self();
    std::string url = _sourceUrl;

    FluxHttp::get(url, [weak](HttpResult result)
                  {
      auto self = weak.lock();
      if (!self) return;

      if (!result.success || result.body.empty()) {
        self->_netState = NetState::Error;
        self->_stopTimer();
        self->markNeedsPaint();
        return;
      }

      const auto *data = reinterpret_cast<const uint8_t *>(result.body.data());
      self->_sampleId = AudioEngine::get().loadSampleFromMemory(data, result.body.size());
      self->_netState = NetState::Idle;

      if (self->_sampleId == kInvalidSample) {
        self->_netState = NetState::Error;
        self->_stopTimer();
        self->markNeedsPaint();
        return;
      }
      self->_startPlaybackOfLoadedSample(); });
  }

  void _loadFromMemory()
  {
    if (_pendingMemory.empty())
      return;

    // Direct decode — no temp file, no disk I/O, no leaked scratch files.
    _sampleId = AudioEngine::get().loadSampleFromMemory(_pendingMemory.data(), _pendingMemory.size());
    _pendingMemory.clear(); // consumed; sample now lives in the engine's bank

    if (_sampleId == kInvalidSample)
    {
      _netState = NetState::Error;
      markNeedsPaint();
      return;
    }
    _startPlaybackOfLoadedSample();
  }

  void _startPlaybackOfLoadedSample()
  {
    _voice = AudioEngine::get().play(_sampleId, _muted ? 0.f : _currentVolume, 0.f, false);
    _playing = (_voice != kInvalidVoice);
    _finished = false;
    _progress = 0.f;
    if (_playing)
      _startTimer();
    markNeedsPaint();
  }

  void _seekFromMouse(int mx)
  {
    if (_trackRect.w <= 0)
      return;
    float t = (float)(mx - _trackRect.x) / (float)_trackRect.w;
    t = std::max(0.f, std::min(1.f, t));
    _progress = t;

#if defined(__EMSCRIPTEN__) || defined(FLUX_SSR)
    if (IDomAdapter *adapter = getActiveDomAdapter())
    {
      if (_domAudioNode != kInvalidDomNode)
      {
        adapter->seekNode(_domAudioNode, t * _domAudioDuration);
        if (_finished && t < 0.999f)
        {
          _finished = false;
          adapter->playNode(_domAudioNode);
        }
      }
      markNeedsPaint();
      return;
    }
#endif

    if (_voice != kInvalidVoice)
      AudioEngine::get().seekVoice(_voice, t);
    if (_finished && t < 0.999f)
      _finished = false;
    markNeedsPaint();
  }

  // ── Volume slider drag ────────────────────────────────────────────────────
  void _setVolumeFromMouse(int mx)
  {
    if (_volSliderRect.w <= 0)
      return;
    float t = (float)(mx - _volSliderRect.x) / (float)_volSliderRect.w;
    t = std::max(0.f, std::min(1.f, t));
    _currentVolume = t;
    if (_muted && t > 0.f)
      _muted = false; // dragging the slider up implicitly unmutes

#if defined(__EMSCRIPTEN__) || defined(FLUX_SSR)
    if (IDomAdapter *adapter = getActiveDomAdapter())
    {
      if (_domAudioNode != kInvalidDomNode)
      {
        adapter->setFloatProperty(_domAudioNode, "volume", _currentVolume);
        if (_muted == false)
          adapter->setBoolProperty(_domAudioNode, "muted", false);
      }
      markNeedsPaint();
      return;
    }
#endif

    if (_voice != kInvalidVoice)
      AudioEngine::get().setVoiceGain(_voice, _muted ? 0.f : _currentVolume);
    markNeedsPaint();
  }

  // ── Mute / unmute ─────────────────────────────────────────────────────────
  void _toggleMute()
  {
#if defined(__EMSCRIPTEN__) || defined(FLUX_SSR)
    if (IDomAdapter *adapter = getActiveDomAdapter())
    {
      _muted = !_muted;
      if (_domAudioNode != kInvalidDomNode)
        adapter->setBoolProperty(_domAudioNode, "muted", _muted);
      return;
    }
#endif
    _muted = !_muted;
    // Gain always restores to _currentVolume on unmute — no separate
    // "pre-mute" snapshot needed since _currentVolume is never overwritten
    // by the mute toggle itself, only by setVolume()/_setVolumeFromMouse().
    if (_voice != kInvalidVoice)
      AudioEngine::get().setVoiceGain(_voice, _muted ? 0.f : _currentVolume);
  }

  // ── Three-dot menu ────────────────────────────────────────────────────────
  std::function<void()> _dotsCallback;
  void _onDotsClicked()
  {
    if (_dotsCallback)
      _dotsCallback();
  }

public:
  std::shared_ptr<AudioPlayerWidget> setOnDotsClicked(std::function<void()> cb)
  {
    _dotsCallback = std::move(cb);
    return self();
  }
};

using AudioPlayerWidgetPtr = std::shared_ptr<AudioPlayerWidget>;

// ============================================================================
// Factory functions — unchanged signatures, same call sites still work
// ============================================================================

inline AudioPlayerWidgetPtr AudioPlayer()
{
  return std::make_shared<AudioPlayerWidget>();
}

inline AudioPlayerWidgetPtr AudioPlayer(const std::string &pathOrUrl)
{
  auto w = std::make_shared<AudioPlayerWidget>();
  if (pathOrUrl.empty())
    return w;
  bool isUrl = pathOrUrl.rfind("http://", 0) == 0 || pathOrUrl.rfind("https://", 0) == 0;
  return isUrl ? w->setUrl(pathOrUrl) : w->setPath(pathOrUrl);
}

inline AudioPlayerWidgetPtr AudioPlayer(const std::vector<uint8_t> &bytes)
{
  return std::make_shared<AudioPlayerWidget>()->setMemory(bytes);
}

inline AudioPlayerWidgetPtr AudioPlayer(const uint8_t *data, size_t len)
{
  return std::make_shared<AudioPlayerWidget>()->setMemory(data, len);
}