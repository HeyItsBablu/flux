// flux_audioplayer.hpp
// Drop-in, self-contained audio player widget that mirrors the browser <audio>
// control aesthetic. All playback state is internal — app.cpp just instantiates
// it.
//
// Usage:
//   AudioPlayer("audio/sample.mp3")
//       ->setWidth(380)
//
//   AudioPlayer()
//       ->setUrl("https://example.com/audio.mp3")
//       ->setArtwork(ImageWidget::network("https://example.com/cover.jpg"))
//       ->setWidth(380)
//
//   AudioPlayer()
//       ->setMemory(bytes)               // play from std::vector<uint8_t>
//       ->setMemory(ptr, len)            // play from raw pointer + length
//       ->setWidth(380)
//
#pragma once

#include "flux/flux.hpp"
#include "flux/flux_audio.hpp"
#include "../flux_http.hpp"
#include "flux_icons.hpp"
#include "flux_image.hpp"

#ifdef __ANDROID__
std::string FluxAndroid_getFilesDir();
#endif

#ifndef _WIN32
#include <unistd.h> // close(), used by the Linux/Android temp-file path below
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

  // ── Config ────────────────────────────────────────────────────────────────
  std::string audioPath;
  int playerHeight = 40;
  int pillarRadius = 20; // full pill
  int trackHeight = 3;
  int thumbRadius = 6;
  int playBtnSize = 28;
  int iconFontSize = 14; // font icon size for play/pause/vol/dots
  int timeFontSize = 12;
  int volSliderW = 0; // 0 = hidden; set >0 to show inline volume

  // ── Artwork ───────────────────────────────────────────────────────────────
  // When set, a square thumbnail is drawn on the left side of the player.
  // The widget expands its height to accommodate the art (playerArtSize).
  int artworkSize = 0; // 0 = no artwork; set via setArtwork() / setArtworkSize()

  // ── Public fluent setters ─────────────────────────────────────────────────

  /// Play a local file path (same as passing path to AudioPlayer())
  std::shared_ptr<AudioPlayerWidget> setPath(const std::string &p)
  {
    audioPath = p;
    _sourceType = AudioSourceType::Path;
    _sourceUrl.clear();
    _sourceMemory.clear();
    return self();
  }

  /// Stream audio from an HTTP/HTTPS URL.
  /// The bytes are downloaded on a background thread, then handed to FluxAudio.
  std::shared_ptr<AudioPlayerWidget> setUrl(const std::string &url)
  {
    _sourceUrl = url;
    _sourceType = AudioSourceType::Url;
    audioPath.clear();
    _sourceMemory.clear();
    return self();
  }

  /// Play audio from an in-memory byte buffer (copy overload).
  std::shared_ptr<AudioPlayerWidget>
  setMemory(const std::vector<uint8_t> &bytes)
  {
    _sourceMemory = bytes;
    _sourceType = AudioSourceType::Memory;
    audioPath.clear();
    _sourceUrl.clear();
    return self();
  }

  /// Play audio from a raw pointer + length (copies into internal buffer).
  std::shared_ptr<AudioPlayerWidget> setMemory(const uint8_t *data,
                                               size_t len)
  {
    _sourceMemory.assign(data, data + len);
    _sourceType = AudioSourceType::Memory;
    audioPath.clear();
    _sourceUrl.clear();
    return self();
  }

  std::shared_ptr<AudioPlayerWidget> setWidth(int w)
  {
    _requestedWidth = w;
    autoWidth = false;
    markNeedsLayout();
    return self();
  }

  // ── Constructor ───────────────────────────────────────────────────────────
  AudioPlayerWidget()
  {
    height = playerHeight;
    autoHeight = false;
    autoWidth = true;
    isFocusable = false;

    FluxAudio::get().setOnFinished([this]()
                                   {
      _playing  = false;
      _finished = true;
      _progress = 1.f; });
  }

  ~AudioPlayerWidget()
  {
    _stopTimer();
    FluxAudio::get().setOnFinished(nullptr);
    FluxAudio::get().closePlayback();
  }

  // =========================================================================
  // Layout
  // =========================================================================

  void computeLayout(GraphicsContext & /*ctx*/, const BoxConstraints &constraints,
                     FontCache & /*fontCache*/) override
  {
    if (autoWidth)
    {
      width = constraints.maxWidth;
    }
    else
    {
      width = std::min(_requestedWidth, constraints.maxWidth);
    }

    // Height grows when artwork is taller than the bar
    height = std::max(playerHeight, artworkSize);
    applyConstraints();

    needsLayout = false;
  }

  // =========================================================================
  // Render
  // =========================================================================

  void render(GraphicsContext &ctx, FontCache &fontCache) override
  {

    auto &audio = FluxAudio::get();
    if (_playing)
    {
      _progress = audio.getProgress();
      if (!audio.isPlaying() && !audio.isPaused())
      {
        _playing = false;
        _finished = (audio.getProgress() >= 0.999f);
      }
    }

    Painter p(ctx);

    // ── Pill background ──────────────────────────────────────────────────
    p.fillRoundedRectGDI(x, y, width, height, pillarRadius * 2, colBackground,
                         colBorder, 1);

    int cx = x;
    int midY = y + height / 2;

    // ── Loading / error overlay ──────────────────────────────────────────
    if (_netState == NetState::Loading)
    {
      _renderStatusText(p, fontCache, cx, "\xe2\x80\xa6 Loading\xe2\x80\xa6",
                        colLoadingText);
      needsPaint = false;
      return;
    }
    if (_netState == NetState::Error)
    {
      _renderStatusText(p, fontCache, cx, "Error loading audio", colErrorText);
      needsPaint = false;
      return;
    }

    // ── Play / Pause button (font icon) ──────────────────────────────────
    cx += 6;
    int btnX = cx;
    int btnY = y + (height - playBtnSize) / 2;
    _playBtnRect = {btnX, btnY, playBtnSize, playBtnSize};

    Color btnBg = _hovPlay ? Color::fromRGB(210, 210, 210) : colBackground;
    p.fillRoundedRectGDI(btnX, btnY, playBtnSize, playBtnSize, playBtnSize,
                         btnBg, btnBg, 0);

    {
      Color iconCol = _hovPlay ? colIconHover : colIconNormal;
      NativeFont iconFont =
          fontCache.getFont(kIconFont, iconFontSize, FontWeight::Normal);

      wchar_t glyph = FluxIcons::glyph(_playing ? FluxIcons::Pause
                                                : FluxIcons::Play);
      std::wstring glyphStr(1, glyph);

      p.drawText(glyphStr, btnX, btnY, playBtnSize, playBtnSize,
                 iconFont, iconCol, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    cx += playBtnSize + 6;

    // ── Time display ─────────────────────────────────────────────────────
    float dur = audio.getDurationSeconds();
    float pos = _progress * dur;
    std::string timeStr = AP_formatTime(pos) + " / " + AP_formatTime(dur);

    NativeFont timeFont =
        fontCache.getFont("Segoe UI", timeFontSize, FontWeight::Normal);
    int tw = 0, th = 0;
    p.measureText(toWideString(timeStr), timeFont, tw, th);

    p.drawText(toWideString(timeStr), cx, y, tw + 4, height, timeFont, colText,
               DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    cx += tw + 8;

    // ── Seek track ───────────────────────────────────────────────────────
    int rightReserve = 48;
    int trackLeft = cx;
    int trackRight = x + width - rightReserve;
    int trackW = std::max(20, trackRight - trackLeft);

    _trackRect = {trackLeft, midY - 8, trackW, 16};

    p.fillRoundedRectGDI(trackLeft, midY - trackHeight / 2, trackW, trackHeight,
                         trackHeight, colTrackBg, colTrackBg, 0);

    int fillW = (int)(_progress * trackW);
    if (fillW > 0)
    {
      p.fillRoundedRectGDI(trackLeft, midY - trackHeight / 2, fillW,
                           trackHeight, trackHeight, colTrackFill, colTrackFill, 0);
    }

    int thumbX = trackLeft + fillW;
    Color thumbCol = _hovTrack ? colThumbHover : colThumb;
    p.drawEllipse(thumbX - thumbRadius, midY - thumbRadius,
                  thumbRadius * 2, thumbRadius * 2, thumbCol, thumbCol, 0);

    cx = trackRight + 4;

    // ── Volume icon (font icon) ───────────────────────────────────────────
    {
      int iconW = 20, iconH = 20;
      _volIconRect = {cx, y + (height - iconH) / 2, iconW, iconH};

      Color volCol = _hovVol ? colIconHover : colIconNormal;
      NativeFont iconFont =
          fontCache.getFont(kIconFont, iconFontSize, FontWeight::Normal);

      wchar_t glyph = FluxIcons::glyph(_muted ? FluxIcons::Mute
                                              : FluxIcons::Volume);
      std::wstring glyphStr(1, glyph);

      p.drawText(glyphStr, _volIconRect.x, _volIconRect.y,
                 _volIconRect.w, _volIconRect.h,
                 iconFont, volCol, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

      cx += iconW + 4;
    }

    // ── More / dots icon (font icon) ─────────────────────────────────────
    {
      int iconW = 18, iconH = 20;
      _dotsIconRect = {cx, y + (height - iconH) / 2, iconW, iconH};

      Color dotsCol = _hovDots ? colIconHover : colIconNormal;
      NativeFont iconFont =
          fontCache.getFont(kIconFont, iconFontSize, FontWeight::Normal);

      wchar_t glyph = FluxIcons::glyph(FluxIcons::More);
      std::wstring glyphStr(1, glyph);

      p.drawText(glyphStr, _dotsIconRect.x, _dotsIconRect.y,
                 _dotsIconRect.w, _dotsIconRect.h,
                 iconFont, dotsCol, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
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
    return false;
  }

  bool handleMouseMove(int mx, int my) override
  {
    if (_dragging)
    {
      _seekFromMouse(mx);
      return true;
    }

    bool hp = _inRect(mx, my, _playBtnRect);
    bool ht = _inRect(mx, my, _trackRect);
    bool hv = _inRect(mx, my, _volIconRect);
    bool hd = _inRect(mx, my, _dotsIconRect);

    bool changed = (hp != _hovPlay || ht != _hovTrack ||
                    hv != _hovVol || hd != _hovDots);
    _hovPlay = hp;
    _hovTrack = ht;
    _hovVol = hv;
    _hovDots = hd;

    if (changed)
      markNeedsPaint();
    return changed;
  }

  bool handleMouseLeave() override
  {
    _hovPlay = _hovTrack = _hovVol = _hovDots = false;
    markNeedsPaint();
    return true;
  }

private:
  int _requestedWidth = 0;

  // ── Source ────────────────────────────────────────────────────────────────
  AudioSourceType _sourceType = AudioSourceType::None;
  std::string _sourceUrl;
  std::vector<uint8_t> _sourceMemory;

  // ── Network loading state ─────────────────────────────────────────────────
  enum class NetState
  {
    Idle,
    Loading,
    Error
  };
  NetState _netState = NetState::Idle;

  // ── Playback state ────────────────────────────────────────────────────────
  std::atomic<bool> _playing{false};
  std::atomic<bool> _finished{false};
  std::atomic<float> _progress{0.f};
  bool _muted = false;
  float _premuteVolume = 1.f;

  // ── Interaction state ─────────────────────────────────────────────────────
  bool _dragging = false;
  bool _hovPlay = false, _hovTrack = false;
  bool _hovVol = false, _hovDots = false;

  // ── Hit rects ────────────────────────────────────────────────────────────
  struct Rect
  {
    int x, y, w, h;
  };
  Rect _playBtnRect{}, _trackRect{}, _volIconRect{}, _dotsIconRect{};

  // ── Timer ─────────────────────────────────────────────────────────────────
  TimerID _timerId = 0;

  void _startTimer()
  {
    if (_timerId)
      return;
    _timerId = FluxUI::getCurrentInstance()->setInterval(33, [this]()
                                                         {
      if (_playing) markNeedsPaint(); });
  }
  void _stopTimer()
  {
    if (_timerId)
    {
      FluxUI::getCurrentInstance()->clearInterval(_timerId);
      _timerId = 0;
    }
  }

  // ── Helpers ───────────────────────────────────────────────────────────────
  std::shared_ptr<AudioPlayerWidget> self()
  {
    return std::static_pointer_cast<AudioPlayerWidget>(shared_from_this());
  }
  static bool _inRect(int mx, int my, const Rect &r)
  {
    return mx >= r.x && mx < r.x + r.w && my >= r.y && my < r.y + r.h;
  }

  // Draw a centred status string (loading / error) inside the bar area
  void _renderStatusText(Painter &p, FontCache &fontCache,
                         int barStartX, const std::string &msg, Color col)
  {
    NativeFont font = fontCache.getFont("Segoe UI", timeFontSize, FontWeight::Normal);
    int avail = (x + width) - barStartX - 8;
    p.drawTextA(msg, barStartX + 4, y, avail, height, font, col,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE);
  }

  // ── Play / Pause toggle ────────────────────────────────────────────────────
  void _togglePlayPause()
  {
    auto &audio = FluxAudio::get();

    // If a URL or memory source hasn't been loaded yet, kick off loading first
    if (!_playing.load() && !audio.isPaused() && !_finished.load())
    {
      if (_sourceType == AudioSourceType::Url && !_sourceUrl.empty())
      {
        _loadFromUrl();
        return;
      }
      if (_sourceType == AudioSourceType::Memory && !_sourceMemory.empty())
      {
        _playFromMemory();
        return;
      }
    }

    if (_finished.load())
    {
      _finished = false;
      _progress = 0.f;
      audio.seekToProgress(0.f);
      audio.resume();
      _playing = true;
      _startTimer();
      return;
    }

    if (_playing.load())
    {
      audio.pause();
      _playing = false;
      _stopTimer();
    }
    else if (audio.isPaused())
    {
      audio.resume();
      _playing = true;
      _startTimer();
    }
    else
    {
      if (!audioPath.empty())
      {
        audio.playFromPath(audioPath);
        _playing = true;
        _startTimer();
      }
    }
  }

  // ── URL loading ────────────────────────────────────────────────────────────
  // Downloads the audio bytes on a background thread (via FluxHttp),
  // then hands the raw PCM to FluxAudio on the UI thread.
  void _loadFromUrl()
  {
    _netState = NetState::Loading;
    markNeedsPaint();

    std::weak_ptr<AudioPlayerWidget> weak = self();
    std::string url = _sourceUrl;

    FluxHttp::get(url, [weak](HttpResult result)
                  {
      auto self = weak.lock();
      if (!self) return;

      if (!result.success || result.body.empty()) {
        self->_netState = NetState::Error;
        self->markNeedsPaint();
        return;
      }

      // Copy downloaded bytes into memory source and play
      const auto *data = reinterpret_cast<const uint8_t *>(result.body.data());
      self->_sourceMemory.assign(data, data + result.body.size());
      self->_netState = NetState::Idle;
      self->_playFromMemory(); });
  }

  // ── Memory playback ────────────────────────────────────────────────────────
  // Decodes and plays audio from _sourceMemory.
  // FluxAudio::playFromPath only accepts file paths, so we write the bytes to
  // a temporary file and let the existing decode pipeline handle it.
  // An alternative (when FluxAudio exposes playFromMemory) would call that
  // directly — the #ifdef block below shows both approaches.
  void _playFromMemory()
  {
    if (_sourceMemory.empty())
      return;

    auto &audio = FluxAudio::get();

#if defined(FLUXAUDIO_HAS_PLAY_FROM_MEMORY)
    // Future API — if FluxAudio gains a playFromMemory overload, use it here:
    // audio.playFromMemory(_sourceMemory.data(), _sourceMemory.size());
#else
    // Current approach: write to a temp file with an extension hint so the
    // dr_mp3 / dr_wav decoder inside FluxAudio can pick the right codec.
    // We detect the format from the magic bytes to choose the right extension.
    std::string ext = _detectAudioExtension(_sourceMemory);
    std::string tmpPath = _writeTempFile(_sourceMemory, ext);
    if (tmpPath.empty())
    {
      _netState = NetState::Error;
      markNeedsPaint();
      return;
    }
    audioPath = tmpPath;
    audio.playFromPath(audioPath);
#endif

    _playing = true;
    _finished = false;
    _progress = 0.f;
    _startTimer();
    markNeedsPaint();
  }

  // ── Format detection from magic bytes ─────────────────────────────────────
  static std::string _detectAudioExtension(const std::vector<uint8_t> &bytes)
  {
    if (bytes.size() >= 3)
    {
      // MP3: ID3 tag or sync word
      if (bytes[0] == 0x49 && bytes[1] == 0x44 && bytes[2] == 0x33)
        return ".mp3"; // "ID3"
      if ((bytes[0] == 0xFF) && (bytes[1] & 0xE0) == 0xE0)
        return ".mp3"; // MPEG sync
    }
    if (bytes.size() >= 4)
    {
      // WAV: "RIFF"
      if (bytes[0] == 0x52 && bytes[1] == 0x49 &&
          bytes[2] == 0x46 && bytes[3] == 0x46)
        return ".wav";
      // OGG
      if (bytes[0] == 0x4F && bytes[1] == 0x67 &&
          bytes[2] == 0x67 && bytes[3] == 0x53)
        return ".ogg";
      // FLAC
      if (bytes[0] == 0x66 && bytes[1] == 0x4C &&
          bytes[2] == 0x61 && bytes[3] == 0x43)
        return ".flac";
    }
    return ".mp3"; // best-guess fallback
  }

  // ── Temp file writer ───────────────────────────────────────────────────────

  static std::string _writeTempFile(const std::vector<uint8_t> &bytes,
                                    const std::string &ext)
  {
#ifdef _WIN32
    char tmpDir[MAX_PATH];
    if (GetTempPathA(MAX_PATH, tmpDir) == 0)
      return {};
    char tmpFile[MAX_PATH];
    if (GetTempFileNameA(tmpDir, "flx", 0, tmpFile) == 0)
      return {};
    std::string outPath = std::string(tmpFile) + ext;
    FILE *f = nullptr;
    fopen_s(&f, outPath.c_str(), "wb");

#elif defined(__ANDROID__)

    std::string dir = FluxAndroid_getFilesDir();
    if (dir.empty())
      return {};
    std::string outPath = dir + "/flxaudio_XXXXXX" + ext;

    // mkstemps needs a writable char buffer
    std::vector<char> tmpl(outPath.begin(), outPath.end());
    tmpl.push_back('\0');
    int fd = mkstemps(tmpl.data(), (int)ext.size());
    if (fd < 0)
      return {};
    // close(fd);
    outPath = std::string(tmpl.data());
    FILE *f = fopen(outPath.c_str(), "wb");

#else
    // Linux desktop
    std::string tmpl = std::string(P_tmpdir) + "/flxaudioXXXXXX";
    std::vector<char> tmplBuf(tmpl.begin(), tmpl.end());
    tmplBuf.push_back('\0');
    int fd = mkstemp(tmplBuf.data());
    if (fd < 0)
      return {};
    close(fd);
    std::string outPath = std::string(tmplBuf.data()) + ext;
    ::rename(tmplBuf.data(), outPath.c_str());
    FILE *f = fopen(outPath.c_str(), "wb");
#endif

    if (!f)
      return {};
    fwrite(bytes.data(), 1, bytes.size(), f);
    fclose(f);
    return outPath;
  }

  void _seekFromMouse(int mx)
  {
    if (_trackRect.w <= 0)
      return;
    float t = (float)(mx - _trackRect.x) / (float)_trackRect.w;
    t = std::max(0.f, std::min(1.f, t));
    _progress = t;

    auto &audio = FluxAudio::get();
    bool wasPaused = audio.isPaused();
    audio.seekToProgress(t);
    if (wasPaused)
      audio.pause();
    if (_finished.load() && t < 0.999f)
      _finished = false;
    markNeedsPaint();
  }

  // ── Mute / unmute ─────────────────────────────────────────────────────────
  void _toggleMute()
  {
    auto &audio = FluxAudio::get();
    if (_muted)
    {
      audio.setVolume(_premuteVolume);
      _muted = false;
    }
    else
    {
      _premuteVolume = audio.getVolume();
      audio.setVolume(0.f);
      _muted = true;
    }
  }

  // ── Three-dot menu ────────────────────────────────────────────────────────
  std::function<void()> _dotsCallback;

  void _onDotsClicked()
  {
    if (_dotsCallback)
      _dotsCallback();
  }

public:
  //   AudioPlayer("a.mp3")->setOnDotsClicked([]{ /* show menu */ });
  std::shared_ptr<AudioPlayerWidget> setOnDotsClicked(std::function<void()> cb)
  {
    _dotsCallback = std::move(cb);
    return self();
  }
};

using AudioPlayerWidgetPtr = std::shared_ptr<AudioPlayerWidget>;

// ============================================================================
// Factory functions
// ============================================================================

/// Empty player — no source set yet. Configure via setPath/setUrl/setMemory.
inline AudioPlayerWidgetPtr AudioPlayer()
{
  return std::make_shared<AudioPlayerWidget>();
}

/// Path or URL, auto-detected by scheme prefix.
///
///   AudioPlayer("audio/sample.mp3")              -> local path
///   AudioPlayer("https://example.com/song.mp3")  -> streamed URL
///
inline AudioPlayerWidgetPtr AudioPlayer(const std::string &pathOrUrl)
{
  auto w = std::make_shared<AudioPlayerWidget>();
  if (pathOrUrl.empty())
    return w;

  bool isUrl = pathOrUrl.rfind("http://", 0) == 0 ||
               pathOrUrl.rfind("https://", 0) == 0;

  return isUrl ? w->setUrl(pathOrUrl) : w->setPath(pathOrUrl);
}

/// In-memory byte buffer (copy overload).
inline AudioPlayerWidgetPtr AudioPlayer(const std::vector<uint8_t> &bytes)
{
  return std::make_shared<AudioPlayerWidget>()->setMemory(bytes);
}

/// In-memory raw pointer + length.
inline AudioPlayerWidgetPtr AudioPlayer(const uint8_t *data, size_t len)
{
  return std::make_shared<AudioPlayerWidget>()->setMemory(data, len);
}