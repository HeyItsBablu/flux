#ifndef FLUX_IMAGE_HPP
#define FLUX_IMAGE_HPP

#include "../flux_core.hpp"
#include "../flux_http.hpp"

// ============================================================================
// SHARED DECODE  (stb_image — used by Linux, Android, macOS)
// Win32 uses GDI+ instead and does NOT include stb here.
// ============================================================================

#ifndef _WIN32
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_ONLY_BMP
#define STBI_ONLY_TGA
#define STBI_ONLY_GIF
#include "stb_image.h"
#endif

// ============================================================================
// PLATFORM FORWARD DECLARATIONS
// Each platform file includes its own system headers privately.
// ============================================================================

#ifdef __ANDROID__
#include <android/asset_manager.h>
#include <android/log.h>
#define LOGI_IMG(...) __android_log_print(ANDROID_LOG_INFO, "FluxImage", __VA_ARGS__)
#define LOGW_IMG(...) __android_log_print(ANDROID_LOG_WARN, "FluxImage", __VA_ARGS__)
extern AAssetManager *FluxAndroid_getAssetManager();
#endif

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// ============================================================================
// IMAGE FIT
// ============================================================================

enum class ImageFit
{
  Fill,
  Contain,
  Cover,
  None,
  ScaleDown
};

// ============================================================================
// IMAGE REPEAT
// ============================================================================

enum class ImageRepeat
{
  NoRepeat,
  Repeat,
  RepeatX,
  RepeatY
};

// ============================================================================
// FILTER QUALITY
// ============================================================================

enum class FilterQuality
{
  None,
  Low,
  Medium,
  High
};

// ============================================================================
// IMAGE LOAD STATE
// ============================================================================

enum class ImageLoadState
{
  Idle,
  Loading,
  Loaded,
  Error
};

// ============================================================================
// DECODED IMAGE  — shared staging buffer (stb platforms)
// ============================================================================

struct DecodedImage
{
  std::vector<uint8_t> pixels; // ARGB premultiplied (Cairo/CG) or RGBA (Android)
  int width = 0;
  int height = 0;
  bool ready() const { return !pixels.empty() && width > 0 && height > 0; }
  void clear()
  {
    pixels.clear();
    width = height = 0;
  }
};

// ============================================================================
// IMAGE WIDGET
// ============================================================================

class ImageWidget : public Widget
{
public:
  // ── Public configuration ──────────────────────────────────────────────────
  ImageFit fit = ImageFit::Contain;
  ImageRepeat repeat = ImageRepeat::NoRepeat;
  FilterQuality filterQuality = FilterQuality::Low;
  Alignment imageAlignment = Alignment::Center;

  Color tintColor = Color::fromRGBA(0, 0, 0, 0);
  Color placeholderColor = Color::fromRGB(240, 240, 240);
  Color errorColor = Color::fromRGB(255, 200, 200);

  std::function<WidgetPtr()> loadingBuilder;
  std::function<WidgetPtr()> errorBuilder;

  // Read-only state
  std::string imagePath;
  int imageWidth = 0;
  int imageHeight = 0;
  ImageLoadState loadState = ImageLoadState::Idle;
  bool imageLoaded = false;
  bool hasError = false;

  struct DestRect
  {
    float x, y, w, h;
  };

  // ── Platform member storage ───────────────────────────────────────────────
  // Declarations only — implementations live in platform files.

  // 3. Add explicit destructor declarations for each pimpl type
  // Replace the forward-declare-only structs with custom deleters:

#ifdef _WIN32
  struct Win32State;
  struct Win32StateDeleter
  {
    void operator()(Win32State *) const;
  };
  std::unique_ptr<Win32State, Win32StateDeleter> _win32;
#endif

#if defined(__APPLE__) && !defined(__ANDROID__)
  mutable std::mutex _decodeMutex;
  DecodedImage _pending;       // decode thread -> UI thread
  std::vector<uint8_t> pixels; // UI-thread render store

  struct MacScaleCache;
  struct MacScaleCacheDeleter
  {
    void operator()(MacScaleCache *) const;
  };
  mutable std::unique_ptr<MacScaleCache, MacScaleCacheDeleter> _macCache;
#endif

#if defined(__linux__) && !defined(__ANDROID__)
  mutable std::mutex _decodeMutex;
  DecodedImage _pending;       // decode thread → UI thread
  std::vector<uint8_t> pixels; // UI-thread render store

  struct LinuxScaleCache;
  struct LinuxScaleCacheDeleter
  {
    void operator()(LinuxScaleCache *) const;
  };
  mutable std::unique_ptr<LinuxScaleCache, LinuxScaleCacheDeleter> _linuxCache;
#endif

#ifdef __ANDROID__
  // NanoVG texture handle + staging
  int nvgImage = -1;

  struct PendingPixels
  {
    std::vector<uint8_t> rgba;
    int w = 0, h = 0;
    bool ready() const { return !rgba.empty() && w > 0 && h > 0; }
  } pending;

  std::vector<uint8_t> uploadBuffer;
  int uploadW = 0, uploadH = 0;
#endif

  // ── Constructors ──────────────────────────────────────────────────────────
  ImageWidget()
  {
    autoWidth = true;
    autoHeight = true;
  }

  ~ImageWidget() override;

  // =========================================================================
  // Named constructors
  // =========================================================================

  static std::shared_ptr<ImageWidget> asset(const std::string &path)
  {
    auto w = std::make_shared<ImageWidget>();
    w->_loadAssetAsync(path);
    return w;
  }

  static std::shared_ptr<ImageWidget> network(const std::string &url,
                                              bool postToUI = true)
  {
    auto w = std::make_shared<ImageWidget>();
    w->loadFromUrl(url, postToUI);
    return w;
  }

  static std::shared_ptr<ImageWidget> memory(const std::vector<uint8_t> &bytes)
  {
    auto w = std::make_shared<ImageWidget>();
    w->_loadFromMemorySync(bytes.data(), (int)bytes.size());
    return w;
  }

  // =========================================================================
  // loadFromUrl
  // =========================================================================
  void loadFromUrl(const std::string &url, bool postToUI = true)
  {
    if (url.empty())
      return;
    imagePath = url;
    _setLoadState(ImageLoadState::Loading);
    markNeedsPaint();

    std::weak_ptr<ImageWidget> weak =
        std::static_pointer_cast<ImageWidget>(shared_from_this());

    FluxHttp::get(url, [weak](HttpResult result)
                  {
            auto self = weak.lock();
            if (!self) return;

            if (!result.success || result.body.empty()) {
                self->_setLoadState(ImageLoadState::Error);
                self->_scheduleRebuild();
                return;
            }

            const auto *data = reinterpret_cast<const uint8_t *>(result.body.data());
            bool ok = self->_decodeIntoStaging(data, (int)result.body.size());
            if (!ok) self->_setLoadState(ImageLoadState::Error);
            self->_scheduleRebuild(); }, postToUI);
  }

  // Backward compat
  bool loadImage(const std::string &path)
  {
    _loadAssetAsync(path);
    return true;
  }

  // =========================================================================
  // computeLayout
  // =========================================================================
  void computeLayout(GraphicsContext & /*ctx*/,
                     const BoxConstraints &constraints,
                     FontCache & /*fontCache*/) override
  {
    _platformPromote();

    const int padW = paddingLeft + paddingRight;
    const int padH = paddingTop + paddingBottom;

    if (loadState == ImageLoadState::Loaded && imageWidth > 0 && imageHeight > 0)
    {
      if (autoWidth)
        width = (constraints.maxWidth < kUnbounded)
                    ? constraints.clampWidth(constraints.maxWidth)
                    : constraints.clampWidth(imageWidth + padW);
      else
        width = constraints.clampWidth(width);

      if (autoHeight)
        height = (constraints.maxHeight < kUnbounded)
                     ? constraints.clampHeight(constraints.maxHeight)
                     : constraints.clampHeight(imageHeight + padH);
      else
        height = constraints.clampHeight(height);
    }
    else
    {
      if (autoWidth)
        width = (constraints.maxWidth < kUnbounded)
                    ? constraints.clampWidth(constraints.maxWidth)
                    : constraints.clampWidth(100 + padW);
      else
        width = constraints.clampWidth(width);

      if (autoHeight)
        height = (constraints.maxHeight < kUnbounded)
                     ? constraints.clampHeight(constraints.maxHeight)
                     : constraints.clampHeight(100 + padH);
      else
        height = constraints.clampHeight(height);
    }

    applyConstraints();
    needsLayout = false;
  }

  // =========================================================================
  // render
  // =========================================================================
  void render(GraphicsContext &ctx, FontCache &fontCache) override
  {
    Painter painter(ctx);

    if (hasBackground)
      drawRoundedRectangle(ctx);
    if (hasBorder)
      painter.drawRoundedRectOutline(x, y, width, height,
                                     borderRadius * 2,
                                     getCurrentBorderColor(), borderWidth);

    const int cx = x + paddingLeft;
    const int cy = y + paddingTop;
    const int cw = width - paddingLeft - paddingRight;
    const int ch = height - paddingTop - paddingBottom;

    _platformPromote();

    switch (loadState)
    {
    case ImageLoadState::Loaded:
      _platformRender(ctx, cx, cy, cw, ch);
      _applyTint(painter, cx, cy, cw, ch);
      break;

    case ImageLoadState::Loading:
#ifdef __ANDROID__
      if (pending.ready())
      {
        _platformRender(ctx, cx, cy, cw, ch);
        _applyTint(painter, cx, cy, cw, ch);
        if (loadState == ImageLoadState::Loaded)
          break;
      }
#endif
      _renderLoadingWidget(ctx, fontCache, painter, cx, cy, cw, ch);
      break;

    case ImageLoadState::Error:
      _renderErrorWidget(ctx, fontCache, painter, cx, cy, cw, ch);
      break;

    default:
      painter.fillRect(cx, cy, cw, ch, placeholderColor);
      break;
    }

    needsPaint = false;
  }

  // =========================================================================
  // Fluent setters
  // =========================================================================
  std::shared_ptr<ImageWidget> setFit(ImageFit f)
  {
    if (fit != f)
    {
      fit = f;
      _platformInvalidateCache();
      markNeedsPaint();
    }
    return self();
  }
  std::shared_ptr<ImageWidget> setRepeat(ImageRepeat r)
  {
    repeat = r;
    markNeedsPaint();
    return self();
  }
  std::shared_ptr<ImageWidget> setFilterQuality(FilterQuality q)
  {
    if (filterQuality != q)
    {
      filterQuality = q;
      _platformInvalidateCache();
      markNeedsPaint();
    }
    return self();
  }
  std::shared_ptr<ImageWidget> setImageAlignment(Alignment a)
  {
    imageAlignment = a;
    markNeedsPaint();
    return self();
  }
  std::shared_ptr<ImageWidget> setTintColor(Color c)
  {
    tintColor = c;
    markNeedsPaint();
    return self();
  }
  std::shared_ptr<ImageWidget> setPlaceholderColor(Color c)
  {
    placeholderColor = c;
    markNeedsPaint();
    return self();
  }
  std::shared_ptr<ImageWidget> setErrorColor(Color c)
  {
    errorColor = c;
    markNeedsPaint();
    return self();
  }
  std::shared_ptr<ImageWidget> setLoadingBuilder(std::function<WidgetPtr()> fn)
  {
    loadingBuilder = std::move(fn);
    return self();
  }
  std::shared_ptr<ImageWidget> setErrorBuilder(std::function<WidgetPtr()> fn)
  {
    errorBuilder = std::move(fn);
    return self();
  }
  std::shared_ptr<ImageWidget> setWidth(int w)
  {
    width = w;
    autoWidth = false;
    markNeedsLayout();
    return self();
  }
  std::shared_ptr<ImageWidget> setHeight(int h)
  {
    height = h;
    autoHeight = false;
    markNeedsLayout();
    return self();
  }
  std::shared_ptr<ImageWidget> setBorderRadius(int r)
  {
    borderRadius = r;
    markNeedsPaint();
    return self();
  }
  std::shared_ptr<ImageWidget> setPadding(int p)
  {
    paddingLeft = paddingRight = paddingTop = paddingBottom = p;
    markNeedsLayout();
    return self();
  }

  // Backward-compat aliases
  std::shared_ptr<ImageWidget> setImagePath(const std::string &path)
  {
    _loadAssetAsync(path);
    return self();
  }
  std::shared_ptr<ImageWidget> setUrl(const std::string &url,
                                      bool postToUI = true)
  {
    loadFromUrl(url, postToUI);
    return self();
  }

  // Android deferred reload
  void reloadIfDeferred()
  {
    if (!imagePath.empty() && loadState == ImageLoadState::Idle)
      _loadAssetAsync(imagePath);
  }

private:
  std::shared_ptr<ImageWidget> self()
  {
    return std::static_pointer_cast<ImageWidget>(shared_from_this());
  }

  void _setLoadState(ImageLoadState s)
  {
    loadState = s;
    imageLoaded = (s == ImageLoadState::Loaded);
    hasError = (s == ImageLoadState::Error);
  }

  void _scheduleRebuild()
  {
    if (auto *ui = FluxUI::getCurrentInstance())
      ui->partialRebuild(this);
    else
      markNeedsLayout();
  }

  // =========================================================================
  // _loadAssetAsync
  // =========================================================================
  void _loadAssetAsync(const std::string &path)
  {
    if (path.empty())
    {
      _setLoadState(ImageLoadState::Idle);
      return;
    }
    imagePath = path;
    _setLoadState(ImageLoadState::Loading);
    markNeedsPaint();

    std::weak_ptr<ImageWidget> weak =
        std::static_pointer_cast<ImageWidget>(shared_from_this());

    std::thread([weak, path]()
                {
            auto self = weak.lock();
            if (!self) return;

#ifdef __ANDROID__
            AAssetManager *am = FluxAndroid_getAssetManager();
            if (!am) {
                self->_setLoadState(ImageLoadState::Error);
                self->_scheduleRebuild();
                return;
            }
            AAsset *asset = AAssetManager_open(am, path.c_str(), AASSET_MODE_BUFFER);
            if (!asset) {
                LOGW_IMG("Asset not found: %s", path.c_str());
                self->_setLoadState(ImageLoadState::Error);
                self->_scheduleRebuild();
                return;
            }
            const void *buf = AAsset_getBuffer(asset);
            int len = (int)AAsset_getLength(asset);
            bool ok = self->_decodeIntoStaging(
                reinterpret_cast<const uint8_t *>(buf), len);
            AAsset_close(asset);
#else
#ifdef _WIN32
            FILE *f = nullptr;
            fopen_s(&f, path.c_str(), "rb");
#else
            FILE *f = fopen(path.c_str(), "rb");
#endif
            if (!f) {
                self->_setLoadState(ImageLoadState::Error);
                self->_scheduleRebuild();
                return;
            }
            fseek(f, 0, SEEK_END);
            long sz = ftell(f);
            fseek(f, 0, SEEK_SET);
            if (sz <= 0) {
                fclose(f);
                self->_setLoadState(ImageLoadState::Error);
                self->_scheduleRebuild();
                return;
            }
            std::vector<uint8_t> buf2((size_t)sz);
            size_t nread = fread(buf2.data(), 1, (size_t)sz, f);
            fclose(f);
            if ((long)nread != sz) {
                self->_setLoadState(ImageLoadState::Error);
                self->_scheduleRebuild();
                return;
            }
            bool ok = self->_decodeIntoStaging(buf2.data(), (int)buf2.size());
#endif
            if (!ok) self->_setLoadState(ImageLoadState::Error);
            self->_scheduleRebuild(); })
        .detach();
  }

  // =========================================================================
  // _loadFromMemorySync
  // =========================================================================
  void _loadFromMemorySync(const uint8_t *data, int len)
  {
    _setLoadState(ImageLoadState::Loading);
    bool ok = _decodeIntoStaging(data, len);
    _platformPromote();
    if (!ok)
    {
      _setLoadState(ImageLoadState::Error);
      return;
    }
    _setLoadState(ImageLoadState::Loaded);
    markNeedsLayout();
  }

  // =========================================================================
  // _decodeIntoStaging
  //
  // Win32   → delegates entirely to _platformDecode (GDI+ stream)
  // Others  → shared stb_image path, then _platformStorePixels for
  //           format conversion + staging write
  // =========================================================================
  bool _decodeIntoStaging(const uint8_t *data, int len)
  {
#ifdef _WIN32
    return _platformDecode(data, len);
#else
    int w = 0, h = 0, ch = 0;
    stbi_set_flip_vertically_on_load(0);
    unsigned char *raw = stbi_load_from_memory(data, len, &w, &h, &ch, 4);
    if (!raw)
    {
#ifdef __ANDROID__
      LOGW_IMG("stbi decode failed: %s", stbi_failure_reason());
#endif
      return false;
    }
    bool ok = _platformStorePixels(raw, w, h); // platform converts + stages
    stbi_image_free(raw);
    return ok;
#endif
  }

  // =========================================================================
  // Destination-rect calculation
  // =========================================================================

  DestRect _calculateDestRect(int cx, int cy, int cw, int ch) const
  {
    const float imgAspect = (float)imageWidth / (float)imageHeight;
    const float ctnAspect = (float)cw / (float)ch;

    float dw = 0, dh = 0;
    switch (fit)
    {
    case ImageFit::Fill:
      return {(float)cx, (float)cy, (float)cw, (float)ch};

    case ImageFit::Contain:
      if (imgAspect > ctnAspect)
      {
        dw = (float)cw;
        dh = dw / imgAspect;
      }
      else
      {
        dh = (float)ch;
        dw = dh * imgAspect;
      }
      break;

    case ImageFit::Cover:
      if (imgAspect > ctnAspect)
      {
        dh = (float)ch;
        dw = dh * imgAspect;
      }
      else
      {
        dw = (float)cw;
        dh = dw / imgAspect;
      }
      break;

    case ImageFit::None:
      dw = (float)imageWidth;
      dh = (float)imageHeight;
      break;

    case ImageFit::ScaleDown:
      if (imageWidth <= cw && imageHeight <= ch)
      {
        dw = (float)imageWidth;
        dh = (float)imageHeight;
      }
      else if (imgAspect > ctnAspect)
      {
        dw = (float)cw;
        dh = dw / imgAspect;
      }
      else
      {
        dh = (float)ch;
        dw = dh * imgAspect;
      }
      break;

    default:
      return {(float)cx, (float)cy, (float)cw, (float)ch};
    }

    float ox = 0, oy = 0;
    float freeX = (float)cw - dw;
    float freeY = (float)ch - dh;

    switch (imageAlignment)
    {
    case Alignment::Center:
      ox = freeX * 0.5f;
      oy = freeY * 0.5f;
      break;
    case Alignment::TopCenter:
      ox = freeX * 0.5f;
      oy = 0.f;
      break;
    case Alignment::BottomCenter:
      ox = freeX * 0.5f;
      oy = freeY;
      break;
    case Alignment::CenterLeft:
      ox = 0.f;
      oy = freeY * 0.5f;
      break;
    case Alignment::CenterRight:
      ox = freeX;
      oy = freeY * 0.5f;
      break;
    case Alignment::Start:
      ox = 0.f;
      oy = 0.f;
      break;
    case Alignment::End:
      ox = freeX;
      oy = freeY;
      break;
    case Alignment::TopRight:
      ox = freeX;
      oy = 0.f;
      break;
    case Alignment::BottomLeft:
      ox = 0.f;
      oy = freeY;
      break;
    case Alignment::BottomRight:
      ox = freeX;
      oy = freeY;
      break;
    default:
      ox = freeX * 0.5f;
      oy = freeY * 0.5f;
      break;
    }

    return {(float)cx + ox, (float)cy + oy, dw, dh};
  }

  // =========================================================================
  // Tint overlay
  // =========================================================================
  void _applyTint(Painter &painter, int cx, int cy, int cw, int ch) const
  {
    if (tintColor.a == 0)
      return;
    painter.fillRect(cx, cy, cw, ch, tintColor);
  }

  // =========================================================================
  // Loading / error widget rendering
  // =========================================================================
  void _renderLoadingWidget(GraphicsContext &ctx, FontCache &fontCache,
                            Painter &painter, int cx, int cy, int cw, int ch)
  {
    if (loadingBuilder)
    {
      WidgetPtr w = loadingBuilder();
      if (w)
      {
        _renderSubWidget(ctx, fontCache, w, cx, cy, cw, ch);
        return;
      }
    }
    painter.fillRect(cx, cy, cw, ch, placeholderColor);
    painter.drawTextA("\xe2\x80\xa6", cx, cy, cw, ch,
                      fontCache.getFont(fontSize, fontWeight),
                      Color::fromRGB(160, 160, 160),
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  }

  void _renderErrorWidget(GraphicsContext &ctx, FontCache &fontCache,
                          Painter &painter, int cx, int cy, int cw, int ch)
  {
    if (errorBuilder)
    {
      WidgetPtr w = errorBuilder();
      if (w)
      {
        _renderSubWidget(ctx, fontCache, w, cx, cy, cw, ch);
        return;
      }
    }
    painter.fillRect(cx, cy, cw, ch, errorColor);
    painter.drawTextA("\xe2\x9c\x95", cx, cy, cw, ch,
                      fontCache.getFont(fontSize, fontWeight),
                      Color::fromRGB(150, 0, 0),
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  }

  static void _renderSubWidget(GraphicsContext &ctx, FontCache &fontCache,
                               WidgetPtr &w, int cx, int cy, int cw, int ch)
  {
    BoxConstraints c = BoxConstraints::tight(cw, ch);
    w->computeLayout(ctx, c, fontCache);
    w->x = cx;
    w->y = cy;
    w->positionChildren(cx + w->paddingLeft, cy + w->paddingTop,
                        cw - w->paddingLeft - w->paddingRight,
                        ch - w->paddingTop - w->paddingBottom);
    w->render(ctx, fontCache);
  }

  // =========================================================================
  // Platform interface — implemented in platform .cpp / .mm files
  // =========================================================================

#ifdef _WIN32
  bool _platformDecode(const uint8_t *data, int len);
  bool _platformStorePixels(unsigned char *rgba, int w, int h); // stub, returns false
#else
  bool _platformDecode(const uint8_t *data, int len); // stub, returns false
  bool _platformStorePixels(unsigned char *rgba, int w, int h);
#endif
  void _platformPromote();
  void _platformRender(GraphicsContext &ctx, int cx, int cy, int cw, int ch);
  void _platformInvalidateCache();
  void _platformDestroy();
};

// ============================================================================
// FACTORY FUNCTIONS
// ============================================================================

using ImageWidgetPtr = std::shared_ptr<ImageWidget>;

inline ImageWidgetPtr AssetImage(const std::string &path)
{
  return ImageWidget::asset(path);
}
inline ImageWidgetPtr Image()
{
  return std::make_shared<ImageWidget>();
}
inline ImageWidgetPtr NetworkImage(const std::string &url, bool postToUI = true)
{
  return ImageWidget::network(url, postToUI);
}
inline ImageWidgetPtr MemoryImage(const std::vector<uint8_t> &bytes)
{
  return ImageWidget::memory(bytes);
}

#endif // FLUX_IMAGE_HPP