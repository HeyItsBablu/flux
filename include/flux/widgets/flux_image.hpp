#ifndef FLUX_IMAGE_HPP
#define FLUX_IMAGE_HPP

#include "../flux_core.hpp"
#include "../flux_http.hpp"

// ============================================================================
// PLATFORM INCLUDES
// ============================================================================

#ifdef _WIN32
#include <gdiplus.h>
#include <mutex>
#include <objidl.h>
#endif

#if defined(__linux__) && !defined(__ANDROID__)
#include <cairo/cairo.h>
#include <mutex>
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_ONLY_BMP
#define STBI_ONLY_TGA
#include "stb_image.h"
#endif

#ifdef __ANDROID__
#include "nanovg.h"
#include <android/asset_manager.h>
#include <android/log.h>
#define LOGI_IMG(...)                                                          \
  __android_log_print(ANDROID_LOG_INFO, "FluxImage", __VA_ARGS__)
#define LOGW_IMG(...)                                                          \
  __android_log_print(ANDROID_LOG_WARN, "FluxImage", __VA_ARGS__)
extern NVGcontext *FluxAndroid_getVG();
extern AAssetManager *FluxAndroid_getAssetManager();
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_ONLY_BMP
#define STBI_ONLY_TGA
#define STBI_ONLY_GIF
#include "stb_image.h"
#endif

#include <atomic>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

// ============================================================================
// IMAGE FIT  (mirrors Flutter BoxFit)
// ============================================================================

enum class ImageFit {
  Fill,     // stretch to fill — no aspect ratio preservation
  Contain,  // scale up/down to fit inside box, letterbox if needed
  Cover,    // scale up/down to fill box, clip overflow
  None,     // draw at intrinsic size, centred (or per imageAlignment)
  ScaleDown // like Contain but never scale up
};

// ============================================================================
// IMAGE REPEAT  (mirrors Flutter ImageRepeat)
// ============================================================================

enum class ImageRepeat {
  NoRepeat, // default — draw once
  Repeat,   // tile in both axes
  RepeatX,  // tile horizontally only
  RepeatY   // tile vertically only
};

// ============================================================================
// FILTER QUALITY  (mirrors Flutter FilterQuality)
// ============================================================================

enum class FilterQuality {
  None,   // nearest-neighbour
  Low,    // bilinear
  Medium, // bilinear + mipmaps (treated as High on most backends)
  High    // bicubic
};

// ============================================================================
// IMAGE LOAD STATE
// ============================================================================

enum class ImageLoadState { Idle, Loading, Loaded, Error };

// ============================================================================
// DECODED PIXELS  — thread-safe staging buffer shared across platforms
// ============================================================================

struct DecodedImage {
  std::vector<uint8_t>
      pixels; // ARGB premultiplied (Cairo/GDI+) or RGBA (Android)
  int width = 0;
  int height = 0;
  bool ready() const { return !pixels.empty() && width > 0 && height > 0; }
  void clear() {
    pixels.clear();
    width = height = 0;
  }
};

// ============================================================================
// IMAGE WIDGET
// ============================================================================

class ImageWidget : public Widget {
public:
  // ── Public configuration ──────────────────────────────────────────────
  ImageFit fit = ImageFit::Contain;
  ImageRepeat repeat = ImageRepeat::NoRepeat;
  FilterQuality filterQuality = FilterQuality::Low;
  Alignment imageAlignment = Alignment::Center; // within the widget box

  // Tint / blend (colour overlay; only applied when tintColor.a > 0)
  Color tintColor = Color::fromRGBA(0, 0, 0, 0);

  Color placeholderColor = Color::fromRGB(240, 240, 240);
  Color errorColor = Color::fromRGB(255, 200, 200);

  // Optional builder callbacks (mirror Flutter's loadingBuilder / errorBuilder)
  std::function<WidgetPtr()> loadingBuilder;
  std::function<WidgetPtr()> errorBuilder;

  // Read-only state
  std::string imagePath;
  int imageWidth = 0;
  int imageHeight = 0;
  ImageLoadState loadState = ImageLoadState::Idle;
  bool imageLoaded = false;
  bool hasError = false;

  // ── Platform pixel storage ────────────────────────────────────────────
#ifdef _WIN32
  // Decoded source (written on decode thread, read on render thread — see
  // mutex)
  std::unique_ptr<Gdiplus::Bitmap> bitmap;
  mutable std::mutex _decodeMutex;

  mutable std::unique_ptr<Gdiplus::Bitmap> _scaledBitmap;
  mutable int _scaledW = 0;
  mutable int _scaledH = 0;
  mutable int _scaledFit = -1;
  mutable int _scaledQuality = -1;
#endif

#if defined(__linux__) && !defined(__ANDROID__)
  // Staging: decode thread writes here; render thread promotes on next frame.
  mutable std::mutex _decodeMutex;
  DecodedImage _pending;       // written by decode thread
  std::vector<uint8_t> pixels; // owned by render thread only

  mutable cairo_surface_t *_scaledSurface = nullptr;
  mutable int _scaledW = 0;
  mutable int _scaledH = 0;
  mutable int _scaledFit = -1;
  mutable int _scaledQuality = -1;
#endif

#ifdef __ANDROID__
  int nvgImage = -1;

  struct PendingPixels {
    std::vector<uint8_t> rgba;
    int w = 0, h = 0;
    bool ready() const { return !rgba.empty() && w > 0 && h > 0; }
  } pending;

  std::vector<uint8_t> uploadBuffer;
  int uploadW = 0, uploadH = 0;
#endif

  // ── Constructors ──────────────────────────────────────────────────────
  ImageWidget() {
    autoWidth = true;
    autoHeight = true;
  }

  ~ImageWidget() override {
#if defined(__linux__) && !defined(__ANDROID__)
    _invalidateScaledCache();
#endif
#ifdef _WIN32
    _invalidateScaledCache();
#endif
#ifdef __ANDROID__
    _freeNvgImage();
#endif
  }

  // =========================================================================
  // Named constructors (Flutter-style)
  // =========================================================================

  // Image::asset — loads a local file asynchronously
  static std::shared_ptr<ImageWidget> asset(const std::string &path) {
    auto w = std::make_shared<ImageWidget>();
    w->_loadAssetAsync(path);
    return w;
  }

  // Image::network — loads from URL asynchronously
  static std::shared_ptr<ImageWidget> network(const std::string &url,
                                              bool postToUI = true) {
    auto w = std::make_shared<ImageWidget>();
    w->loadFromUrl(url, postToUI);
    return w;
  }

  // Image::memory — decode from an in-memory byte buffer
  static std::shared_ptr<ImageWidget>
  memory(const std::vector<uint8_t> &bytes) {
    auto w = std::make_shared<ImageWidget>();
    w->_loadFromMemorySync(bytes.data(), (int)bytes.size());
    return w;
  }

  // =========================================================================
  // loadFromUrl  (async, safe)
  // =========================================================================
  void loadFromUrl(const std::string &url, bool postToUI = true) {
    if (url.empty())
      return;
    imagePath = url;
    _setLoadState(ImageLoadState::Loading);
    markNeedsPaint();

    std::weak_ptr<ImageWidget> weak =
        std::static_pointer_cast<ImageWidget>(shared_from_this());

    FluxHttp::get(
        url,
        [weak](HttpResult result) {
          auto self = weak.lock();
          if (!self)
            return;

          if (!result.success || result.body.empty()) {
            self->_setLoadState(ImageLoadState::Error);
            self->_scheduleRebuild();
            return;
          }

          const auto *data =
              reinterpret_cast<const uint8_t *>(result.body.data());
          bool ok = self->_decodeIntoStaging(data, (int)result.body.size());
          if (!ok) {
            self->_setLoadState(ImageLoadState::Error);
          }
          self->_scheduleRebuild();
        },
        postToUI);
  }

  // =========================================================================
  // loadImage — kept for backward-compat; prefer Image::asset()
  // =========================================================================
  bool loadImage(const std::string &path) {
    _loadAssetAsync(path);
    return true; // actual result delivered asynchronously
  }

  // =========================================================================
  // computeLayout
  // =========================================================================
  void computeLayout(GraphicsContext & /*ctx*/,
                     const BoxConstraints &constraints,
                     FontCache & /*fontCache*/) override {
    _promoteStaging(); // safe: runs on UI thread

    const int padW = paddingLeft + paddingRight;
    const int padH = paddingTop + paddingBottom;

    if (loadState == ImageLoadState::Loaded && imageWidth > 0 &&
        imageHeight > 0) {
      //
      // sizing rules:
      //   bounded axis  → fill available space
      //   unbounded axis → use intrinsic image size
      //
      // These two rules apply independently per axis.
      //
      if (autoWidth) {
        width = (constraints.maxWidth < kUnbounded)
                    ? constraints.clampWidth(constraints.maxWidth)
                    : constraints.clampWidth(imageWidth + padW);
      } else {
        width = constraints.clampWidth(width);
      }

      if (autoHeight) {
        height = (constraints.maxHeight < kUnbounded)
                     ? constraints.clampHeight(constraints.maxHeight)
                     : constraints.clampHeight(imageHeight + padH);
      } else {
        height = constraints.clampHeight(height);
      }
    } else {
      // Placeholder / loading / error — fill if bounded, else 100×100
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
  void render(GraphicsContext &ctx, FontCache &fontCache) override {
    Painter painter(ctx);

    if (hasBackground)
      drawRoundedRectangle(ctx);
    if (hasBorder)
      painter.drawRoundedRectOutline(x, y, width, height, borderRadius * 2,
                                     getCurrentBorderColor(), borderWidth);

    const int cx = x + paddingLeft;
    const int cy = y + paddingTop;
    const int cw = width - paddingLeft - paddingRight;
    const int ch = height - paddingTop - paddingBottom;

#ifdef __ANDROID__
    // Deferred load — NanoVG context required
    if (loadState == ImageLoadState::Idle && !imagePath.empty())
      _loadAssetAsync(imagePath);
#endif

    _promoteStaging();

    switch (loadState) {
    case ImageLoadState::Loaded:
#ifdef _WIN32
      _renderGDIPlus(ctx, cx, cy, cw, ch);
#elif defined(__ANDROID__)
      _renderNanoVG(cx, cy, cw, ch);
#else
      _renderCairo(ctx, cx, cy, cw, ch);
#endif
      _applyTint(painter, cx, cy, cw, ch);
      break;

    case ImageLoadState::Loading:
#ifdef __ANDROID__
      if (pending.ready()) {
        _renderNanoVG(cx, cy, cw, ch);
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
  std::shared_ptr<ImageWidget> setFit(ImageFit f) {
    if (fit != f) {
      fit = f;
      _invalidateScaledCache();
      markNeedsPaint();
    }
    return self();
  }
  std::shared_ptr<ImageWidget> setRepeat(ImageRepeat r) {
    repeat = r;
    markNeedsPaint();
    return self();
  }
  std::shared_ptr<ImageWidget> setFilterQuality(FilterQuality q) {
    if (filterQuality != q) {
      filterQuality = q;
      _invalidateScaledCache();
      markNeedsPaint();
    }
    return self();
  }
  std::shared_ptr<ImageWidget> setImageAlignment(Alignment a) {
    imageAlignment = a;
    markNeedsPaint();
    return self();
  }
  std::shared_ptr<ImageWidget> setTintColor(Color c) {
    tintColor = c;
    markNeedsPaint();
    return self();
  }
  std::shared_ptr<ImageWidget> setPlaceholderColor(Color c) {
    placeholderColor = c;
    markNeedsPaint();
    return self();
  }
  std::shared_ptr<ImageWidget> setErrorColor(Color c) {
    errorColor = c;
    markNeedsPaint();
    return self();
  }
  std::shared_ptr<ImageWidget>
  setLoadingBuilder(std::function<WidgetPtr()> fn) {
    loadingBuilder = std::move(fn);
    return self();
  }
  std::shared_ptr<ImageWidget> setErrorBuilder(std::function<WidgetPtr()> fn) {
    errorBuilder = std::move(fn);
    return self();
  }
  std::shared_ptr<ImageWidget> setWidth(int w) {
    width = w;
    autoWidth = false;
    markNeedsLayout();
    return self();
  }
  std::shared_ptr<ImageWidget> setHeight(int h) {
    height = h;
    autoHeight = false;
    markNeedsLayout();
    return self();
  }
  std::shared_ptr<ImageWidget> setBorderRadius(int r) {
    borderRadius = r;
    markNeedsPaint();
    return self();
  }
  std::shared_ptr<ImageWidget> setPadding(int p) {
    paddingLeft = paddingRight = paddingTop = paddingBottom = p;
    markNeedsLayout();
    return self();
  }

  // Backward-compat aliases
  std::shared_ptr<ImageWidget> setImagePath(const std::string &path) {
    _loadAssetAsync(path);
    return self();
  }
  std::shared_ptr<ImageWidget> setUrl(const std::string &url,
                                      bool postToUI = true) {
    loadFromUrl(url, postToUI);
    return self();
  }

  // =========================================================================
  // Android deferred reload
  // =========================================================================
  void reloadIfDeferred() {
    if (!imagePath.empty() && loadState == ImageLoadState::Idle)
      _loadAssetAsync(imagePath);
  }

private:
  std::shared_ptr<ImageWidget> self() {
    return std::static_pointer_cast<ImageWidget>(shared_from_this());
  }

  void _setLoadState(ImageLoadState s) {
    loadState = s;
    imageLoaded = (s == ImageLoadState::Loaded);
    hasError = (s == ImageLoadState::Error);
  }

  void _scheduleRebuild() {
    if (auto *ui = FluxUI::getCurrentInstance())
      ui->partialRebuild(this);
    else
      markNeedsLayout();
  }

  // =========================================================================
  // _loadAssetAsync  — reads file on a detached background thread, decodes,
  // and stages the result for promotion on the next UI-thread frame.
  // Uses std::thread::detach, the same pattern as FluxHttp::send.
  // =========================================================================
  void _loadAssetAsync(const std::string &path) {
    if (path.empty()) {
      _setLoadState(ImageLoadState::Idle);
      return;
    }
    imagePath = path;
    _setLoadState(ImageLoadState::Loading);
    markNeedsPaint();

    std::weak_ptr<ImageWidget> weak =
        std::static_pointer_cast<ImageWidget>(shared_from_this());

    std::thread([weak, path]() {
      auto self = weak.lock();
      if (!self)
        return;

#ifdef __ANDROID__
      // Android: NanoVG texture upload must happen on the GL/render thread.
      // Set state back to Idle so render() picks it up as a deferred load.
      self->_setLoadState(ImageLoadState::Idle);
      self->_scheduleRebuild();
      return;
#endif
      // ── Read the file into memory ─────────────────────────────────────
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
      std::vector<uint8_t> buf((size_t)sz);
      size_t nread = fread(buf.data(), 1, (size_t)sz, f);
      fclose(f);
      if ((long)nread != sz) {
        self->_setLoadState(ImageLoadState::Error);
        self->_scheduleRebuild();
        return;
      }

      // ── Decode into staging buffer ────────────────────────────────────
      bool ok = self->_decodeIntoStaging(buf.data(), (int)buf.size());
      if (!ok)
        self->_setLoadState(ImageLoadState::Error);
      self->_scheduleRebuild();
    }).detach();
  }

  // =========================================================================
  // _loadFromMemorySync  — decode synchronously (Image::memory)
  // Caller is responsible for calling from the UI thread.
  // =========================================================================
  void _loadFromMemorySync(const uint8_t *data, int len) {
    _setLoadState(ImageLoadState::Loading);
    bool ok = _decodeIntoStaging(data, len);
    _promoteStaging();
    if (!ok) {
      _setLoadState(ImageLoadState::Error);
      return;
    }
    _setLoadState(ImageLoadState::Loaded);
    markNeedsLayout();
  }

  // =========================================================================
  // _decodeIntoStaging
  // Called from a background thread. Writes to _pending / bitmap under mutex.
  // Returns true on success.
  // =========================================================================
  bool _decodeIntoStaging(const uint8_t *data, int len) {
#ifdef _WIN32
    IStream *stream = nullptr;
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, (SIZE_T)len);
    if (!hMem)
      return false;
    void *ptr = GlobalLock(hMem);
    if (!ptr) {
      GlobalFree(hMem);
      return false;
    }
    memcpy(ptr, data, len);
    GlobalUnlock(hMem);
    if (CreateStreamOnHGlobal(hMem, TRUE, &stream) != S_OK) {
      GlobalFree(hMem);
      return false;
    }
    std::unique_ptr<Gdiplus::Bitmap> decoded;
    try {
      decoded = std::make_unique<Gdiplus::Bitmap>(stream);
      stream->Release();
      if (!decoded || decoded->GetLastStatus() != Gdiplus::Ok)
        return false;
    } catch (...) {
      stream->Release();
      return false;
    }
    int w = (int)decoded->GetWidth();
    int h = (int)decoded->GetHeight();
    if (w == 0 || h == 0)
      return false;

    {
      std::lock_guard<std::mutex> lock(_decodeMutex);
      bitmap = std::move(decoded);
      imageWidth = w;
      imageHeight = h;
      // Signal UI thread to flush the scaled cache (_scaledW == -1 is the dirty
      // flag).
      _scaledW = -1;
    }
    _setLoadState(ImageLoadState::Loaded);
    return true;
#endif

#if defined(__linux__) && !defined(__ANDROID__)
    int w = 0, h = 0, channels = 0;
    stbi_set_flip_vertically_on_load(0);
    unsigned char *raw = stbi_load_from_memory(data, len, &w, &h, &channels, 4);
    if (!raw)
      return false;

    DecodedImage img;
    img.width = w;
    img.height = h;
    _convertStbiToCairoPremul(raw, w, h, img.pixels);
    stbi_image_free(raw);

    {
      std::lock_guard<std::mutex> lock(_decodeMutex);
      _pending = std::move(img);
    }
    return true;
#endif

#ifdef __ANDROID__
    int w = 0, h = 0, ch = 0;
    stbi_set_flip_vertically_on_load(0);
    unsigned char *rgba = stbi_load_from_memory(data, len, &w, &h, &ch, 4);
    if (!rgba) {
      LOGW_IMG("stbi decode failed: %s", stbi_failure_reason());
      return false;
    }
    pending.rgba.assign(rgba, rgba + (size_t)w * h * 4);
    pending.w = w;
    pending.h = h;
    stbi_image_free(rgba);
    return true;
#endif

    return false;
  }

  // =========================================================================
  // _promoteStaging  — UI-thread only.
  // Moves pending decoded data into the render-ready store and invalidates
  // the scaled cache so it rebuilds at the new source.
  // =========================================================================
  void _promoteStaging() {
#if defined(__linux__) && !defined(__ANDROID__)
    DecodedImage staged;
    {
      std::lock_guard<std::mutex> lock(_decodeMutex);
      if (!_pending.ready())
        return;
      staged = std::move(_pending);
      _pending.clear();
    }
    // Now we are on the UI thread — safe to touch pixels and cache.
    pixels = std::move(staged.pixels);
    imageWidth = staged.width;
    imageHeight = staged.height;
    _invalidateScaledCache();
    _setLoadState(ImageLoadState::Loaded);
    markNeedsLayout();
#endif
#ifdef _WIN32
    // GDI+ decode writes to `bitmap` under mutex and sets loadState = Loaded
    // on the decode thread. We only need to flush the scaled cache on the
    // UI thread if the decode thread flagged it as dirty (_scaledW reset to
    // -1).
    {
      std::lock_guard<std::mutex> lock(_decodeMutex);
      if (_scaledW == -1) {
        // Decode thread signalled a new bitmap arrived — reset properly.
        _scaledW = _scaledH = 0;
        _scaledFit = _scaledQuality = -1;
        _scaledBitmap.reset();
      }
    }
#endif
    // Android: promotion handled inside _renderNanoVG (GL thread required)
  }

  // =========================================================================
  // _invalidateScaledCache
  // =========================================================================
  void _invalidateScaledCache() {
#if defined(__linux__) && !defined(__ANDROID__)
    if (_scaledSurface) {
      cairo_surface_destroy(_scaledSurface);
      _scaledSurface = nullptr;
    }
    _scaledW = _scaledH = 0;
    _scaledFit = _scaledQuality = -1;
#endif
#ifdef _WIN32
    _invalidateScaledCacheNoLock();
#endif
  }

#ifdef _WIN32
  void _invalidateScaledCacheNoLock() {
    _scaledBitmap.reset();
    _scaledW = _scaledH = 0;
    _scaledFit = _scaledQuality = -1;
  }
#endif

  // =========================================================================
  // Destination-rect calculation (respects imageAlignment)
  // =========================================================================
  struct DestRect {
    float x, y, w, h;
  };

  DestRect _calculateDestRect(int cx, int cy, int cw, int ch) const {
    const float imgAspect = (float)imageWidth / (float)imageHeight;
    const float ctnAspect = (float)cw / (float)ch;

    float dw, dh;
    switch (fit) {
    case ImageFit::Fill:
      return {(float)cx, (float)cy, (float)cw, (float)ch};

    case ImageFit::Contain:
      if (imgAspect > ctnAspect) {
        dw = (float)cw;
        dh = dw / imgAspect;
      } else {
        dh = (float)ch;
        dw = dh * imgAspect;
      }
      break;

    case ImageFit::Cover:
      if (imgAspect > ctnAspect) {
        dh = (float)ch;
        dw = dh * imgAspect;
      } else {
        dw = (float)cw;
        dh = dw / imgAspect;
      }
      break;

    case ImageFit::None:
      dw = (float)imageWidth;
      dh = (float)imageHeight;
      break;

    case ImageFit::ScaleDown:
      if (imageWidth <= cw && imageHeight <= ch) {
        dw = (float)imageWidth;
        dh = (float)imageHeight;
      } else if (imgAspect > ctnAspect) {
        dw = (float)cw;
        dh = dw / imgAspect;
      } else {
        dh = (float)ch;
        dw = dh * imgAspect;
      }
      break;

    default:
      return {(float)cx, (float)cy, (float)cw, (float)ch};
    }

    // Apply imageAlignment for offset within container
    float ox = 0, oy = 0;
    float freeX = (float)cw - dw;
    float freeY = (float)ch - dh;

    switch (imageAlignment) {
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
    case Alignment::Start: // TopLeft
      ox = 0.f;
      oy = 0.f;
      break;
    case Alignment::End: // BottomRight
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
  // Tint overlay (all platforms)
  // =========================================================================
  void _applyTint(Painter &painter, int cx, int cy, int cw, int ch) const {
    if (tintColor.a == 0)
      return;
    painter.fillRect(cx, cy, cw, ch, tintColor);
  }

  // =========================================================================
  // Loading / error widget rendering
  // =========================================================================
  void _renderLoadingWidget(GraphicsContext &ctx, FontCache &fontCache,
                            Painter &painter, int cx, int cy, int cw, int ch) {
    if (loadingBuilder) {
      WidgetPtr w = loadingBuilder();
      if (w) {
        BoxConstraints c = BoxConstraints::tight(cw, ch);
        w->computeLayout(ctx, c, fontCache);
        w->x = cx;
        w->y = cy;
        w->positionChildren(cx + w->paddingLeft, cy + w->paddingTop,
                            cw - w->paddingLeft - w->paddingRight,
                            ch - w->paddingTop - w->paddingBottom);
        w->render(ctx, fontCache);
        return;
      }
    }
    // Default shimmer placeholder
    painter.fillRect(cx, cy, cw, ch, placeholderColor);
    painter.drawTextA(
        "\xe2\x80\xa6", cx, cy, cw, ch, fontCache.getFont(fontSize, fontWeight),
        Color::fromRGB(160, 160, 160), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  }

  void _renderErrorWidget(GraphicsContext &ctx, FontCache &fontCache,
                          Painter &painter, int cx, int cy, int cw, int ch) {
    if (errorBuilder) {
      WidgetPtr w = errorBuilder();
      if (w) {
        BoxConstraints c = BoxConstraints::tight(cw, ch);
        w->computeLayout(ctx, c, fontCache);
        w->x = cx;
        w->y = cy;
        w->positionChildren(cx + w->paddingLeft, cy + w->paddingTop,
                            cw - w->paddingLeft - w->paddingRight,
                            ch - w->paddingTop - w->paddingBottom);
        w->render(ctx, fontCache);
        return;
      }
    }
    painter.fillRect(cx, cy, cw, ch, errorColor);
    painter.drawTextA(
        "\xe2\x9c\x95", cx, cy, cw, ch, fontCache.getFont(fontSize, fontWeight),
        Color::fromRGB(150, 0, 0), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  }

  // =========================================================================
  // GDI+ rendering (Windows)
  // =========================================================================
#ifdef _WIN32
  // Map FilterQuality → GDI+ InterpolationMode
  Gdiplus::InterpolationMode _gdiInterpolation() const {
    switch (filterQuality) {
    case FilterQuality::None:
      return Gdiplus::InterpolationModeNearestNeighbor;
    case FilterQuality::Low:
      return Gdiplus::InterpolationModeBilinear;
    case FilterQuality::Medium:
      return Gdiplus::InterpolationModeHighQualityBilinear;
    case FilterQuality::High:
      return Gdiplus::InterpolationModeHighQualityBicubic;
    }
    return Gdiplus::InterpolationModeBilinear;
  }

  void _renderGDIPlus(GraphicsContext &ctx, int cx, int cy, int cw,
                      int ch) const {
    std::lock_guard<std::mutex> lock(_decodeMutex);
    if (!bitmap)
      return;

    DestRect d = _calculateDestRect(cx, cy, cw, ch);
    int dw = std::max(1, (int)d.w);
    int dh = std::max(1, (int)d.h);

    // ── 1. Build / reuse scaled source bitmap ────────────────────────────
    bool cacheStale = !_scaledBitmap || _scaledW != dw || _scaledH != dh ||
                      _scaledFit != (int)fit ||
                      _scaledQuality != (int)filterQuality;

    if (cacheStale) {
      _scaledBitmap =
          std::make_unique<Gdiplus::Bitmap>(dw, dh, PixelFormat32bppPARGB);
      if (_scaledBitmap && _scaledBitmap->GetLastStatus() == Gdiplus::Ok) {
        Gdiplus::Graphics sg(_scaledBitmap.get());
        sg.SetInterpolationMode(_gdiInterpolation());
        sg.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
        sg.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
        sg.DrawImage(bitmap.get(),
                     Gdiplus::RectF(0.f, 0.f, (float)dw, (float)dh));
        _scaledW = dw;
        _scaledH = dh;
        _scaledFit = (int)fit;
        _scaledQuality = (int)filterQuality;
      } else {
        _scaledBitmap.reset();
        _scaledW = _scaledH = 0;
        _scaledFit = _scaledQuality = -1;
      }
    }

    Gdiplus::Bitmap *src = _scaledBitmap ? _scaledBitmap.get() : bitmap.get();

    // ── 2. If no border radius, draw directly to HDC (fast path) ────────
    if (borderRadius <= 0) {
      Gdiplus::Graphics g(ctx.hdc);
      g.SetInterpolationMode(_scaledBitmap
                                 ? Gdiplus::InterpolationModeNearestNeighbor
                                 : _gdiInterpolation());
      g.SetClip(Gdiplus::Rect(cx, cy, cw, ch));

      if (repeat != ImageRepeat::NoRepeat && _scaledBitmap)
        _renderGDIPlusRepeat(g, src, d, cx, cy, cw, ch);
      else
        g.DrawImage(src, Gdiplus::RectF(d.x, d.y, (float)dw, (float)dh));
      return;
    }

    // ── 3. Rounded path — render to offscreen ARGB bitmap, then alpha-blit
    //       This sidesteps HDC/GDI clip conflicts entirely.
    // Offscreen canvas covers the full widget bounds (x,y,width,height).
    int ow = width;
    int oh = height;

    Gdiplus::Bitmap offscreen(ow, oh, PixelFormat32bppPARGB);
    if (offscreen.GetLastStatus() != Gdiplus::Ok)
      return;

    {
      Gdiplus::Graphics og(&offscreen);
      og.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
      og.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
      og.SetInterpolationMode(_scaledBitmap
                                  ? Gdiplus::InterpolationModeNearestNeighbor
                                  : _gdiInterpolation());

      // 3a. Clear to transparent
      og.Clear(Gdiplus::Color(0, 0, 0, 0));

      // 3b. Build rounded-rect clip path in offscreen-local coords
      //     (offscreen origin = widget origin x,y  →  offset by -x, -y)
      float r = (float)borderRadius;
      float diam = r * 2.0f;
      float fx = 0.f; // local origin
      float fy = 0.f;
      float fw = (float)ow;
      float fh = (float)oh;

      Gdiplus::GraphicsPath path;
      path.AddArc(fx, fy, diam, diam, 180.0f, 90.0f);
      path.AddArc(fx + fw - diam, fy, diam, diam, 270.0f, 90.0f);
      path.AddArc(fx + fw - diam, fy + fh - diam, diam, diam, 0.0f, 90.0f);
      path.AddArc(fx, fy + fh - diam, diam, diam, 90.0f, 90.0f);
      path.CloseFigure();

      Gdiplus::Region region(&path);
      og.SetClip(&region);

      // 3c. Draw image at offscreen-local position (subtract widget origin)
      float lx = d.x - (float)x;
      float ly = d.y - (float)y;

      if (repeat != ImageRepeat::NoRepeat && _scaledBitmap) {
        // Repeat tiles — adjust coords to local space
        float tileW = d.w, tileH = d.h;
        float startX = (repeat == ImageRepeat::RepeatY) ? lx : 0.f;
        float startY = (repeat == ImageRepeat::RepeatX) ? ly : 0.f;
        float endX = (repeat == ImageRepeat::RepeatY) ? lx + tileW : (float)ow;
        float endY = (repeat == ImageRepeat::RepeatX) ? ly + tileH : (float)oh;
        for (float ty = startY; ty < endY; ty += tileH)
          for (float tx = startX; tx < endX; tx += tileW)
            og.DrawImage(src, Gdiplus::RectF(tx, ty, tileW, tileH));
      } else {
        og.DrawImage(src, Gdiplus::RectF(lx, ly, (float)dw, (float)dh));
      }
    }

    // ── 4. Alpha-blit offscreen → HDC at widget position ─────────────────
    Gdiplus::Graphics g(ctx.hdc);
    g.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
    g.DrawImage(&offscreen,
                Gdiplus::RectF((float)x, (float)y, (float)ow, (float)oh));
  }
  void _renderGDIPlusRepeat(Gdiplus::Graphics &g, Gdiplus::Bitmap *src,
                            const DestRect &d, int cx, int cy, int cw,
                            int ch) const {
    float tileW = d.w, tileH = d.h;
    float startX = (repeat == ImageRepeat::RepeatY) ? d.x : (float)cx;
    float startY = (repeat == ImageRepeat::RepeatX) ? d.y : (float)cy;
    float endX =
        (repeat == ImageRepeat::RepeatY) ? d.x + tileW : (float)(cx + cw);
    float endY =
        (repeat == ImageRepeat::RepeatX) ? d.y + tileH : (float)(cy + ch);

    for (float ty = startY; ty < endY; ty += tileH)
      for (float tx = startX; tx < endX; tx += tileW)
        g.DrawImage(src, Gdiplus::RectF(tx, ty, tileW, tileH));
  }
#endif

  // =========================================================================
  // Cairo rendering (Linux)
  // =========================================================================
#if defined(__linux__) && !defined(__ANDROID__)
  static void _convertStbiToCairoPremul(unsigned char *data, int w, int h,
                                        std::vector<uint8_t> &out) {
    const size_t nPx = (size_t)w * (size_t)h;
    out.resize(nPx * 4);
    for (size_t i = 0; i < nPx; ++i) {
      uint8_t r = data[i * 4 + 0], g = data[i * 4 + 1], b = data[i * 4 + 2],
              a = data[i * 4 + 3];
      out[i * 4 + 0] = (uint8_t)((uint32_t)b * a / 255u);
      out[i * 4 + 1] = (uint8_t)((uint32_t)g * a / 255u);
      out[i * 4 + 2] = (uint8_t)((uint32_t)r * a / 255u);
      out[i * 4 + 3] = a;
    }
  }

  cairo_filter_t _cairoFilter() const {
    switch (filterQuality) {
    case FilterQuality::None:
      return CAIRO_FILTER_NEAREST;
    case FilterQuality::Low:
      return CAIRO_FILTER_BILINEAR;
    case FilterQuality::Medium:
      return CAIRO_FILTER_BILINEAR;
    case FilterQuality::High:
      return CAIRO_FILTER_BEST;
    }
    return CAIRO_FILTER_BILINEAR;
  }

  void _renderCairo(GraphicsContext &ctx, int cx, int cy, int cw,
                    int ch) const {
    if (pixels.empty() || !ctx.cr)
      return;

    DestRect d = _calculateDestRect(cx, cy, cw, ch);
    int dw = std::max(1, (int)d.w);
    int dh = std::max(1, (int)d.h);

    bool cacheStale = !_scaledSurface || _scaledW != dw || _scaledH != dh ||
                      _scaledFit != (int)fit ||
                      _scaledQuality != (int)filterQuality;

    if (cacheStale) {
      if (_scaledSurface) {
        cairo_surface_destroy(_scaledSurface);
        _scaledSurface = nullptr;
      }

      cairo_surface_t *surf =
          cairo_image_surface_create(CAIRO_FORMAT_ARGB32, dw, dh);

      if (cairo_surface_status(surf) == CAIRO_STATUS_SUCCESS) {
        cairo_t *scr = cairo_create(surf);
        cairo_surface_t *src = cairo_image_surface_create_for_data(
            const_cast<uint8_t *>(pixels.data()), CAIRO_FORMAT_ARGB32,
            imageWidth, imageHeight, imageWidth * 4);

        if (cairo_surface_status(src) == CAIRO_STATUS_SUCCESS) {
          cairo_scale(scr, (double)dw / imageWidth, (double)dh / imageHeight);
          cairo_set_source_surface(scr, src, 0.0, 0.0);
          cairo_pattern_set_filter(cairo_get_source(scr), _cairoFilter());
          cairo_paint(scr);
        }
        cairo_surface_destroy(src);
        cairo_destroy(scr);

        _scaledSurface = surf;
        _scaledW = dw;
        _scaledH = dh;
        _scaledFit = (int)fit;
        _scaledQuality = (int)filterQuality;
      } else {
        cairo_surface_destroy(surf);
      }
    }

    cairo_t *cr = ctx.cr;
    cairo_save(cr);

    if (borderRadius > 0) {
      _cairoRoundedRectPath(cr, x, y, width, height, borderRadius);
      cairo_clip(cr);
    } else {
      cairo_rectangle(cr, cx, cy, cw, ch);
      cairo_clip(cr);
    }

    if (_scaledSurface) {
      if (repeat != ImageRepeat::NoRepeat) {
        _renderCairoRepeat(cr, d, cx, cy, cw, ch);
      } else {
        cairo_set_source_surface(cr, _scaledSurface, d.x, d.y);
        cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_NEAREST);
        cairo_paint(cr);
      }
    } else {
      // Slow fallback (no pre-scaled cache)
      cairo_surface_t *src = cairo_image_surface_create_for_data(
          const_cast<uint8_t *>(pixels.data()), CAIRO_FORMAT_ARGB32, imageWidth,
          imageHeight, imageWidth * 4);
      if (cairo_surface_status(src) == CAIRO_STATUS_SUCCESS) {
        cairo_translate(cr, d.x, d.y);
        cairo_scale(cr, d.w / imageWidth, d.h / imageHeight);
        cairo_set_source_surface(cr, src, 0.0, 0.0);
        cairo_pattern_set_filter(cairo_get_source(cr), _cairoFilter());
        cairo_paint(cr);
      }
      cairo_surface_destroy(src);
    }

    cairo_restore(cr);
  }

  void _renderCairoRepeat(cairo_t *cr, const DestRect &d, int cx, int cy,
                          int cw, int ch) const {
    float tileW = d.w, tileH = d.h;
    float startX = (repeat == ImageRepeat::RepeatY) ? d.x : (float)cx;
    float startY = (repeat == ImageRepeat::RepeatX) ? d.y : (float)cy;
    float endX =
        (repeat == ImageRepeat::RepeatY) ? d.x + tileW : (float)(cx + cw);
    float endY =
        (repeat == ImageRepeat::RepeatX) ? d.y + tileH : (float)(cy + ch);

    for (float ty = startY; ty < endY; ty += tileH) {
      for (float tx = startX; tx < endX; tx += tileW) {
        cairo_set_source_surface(cr, _scaledSurface, tx, ty);
        cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_NEAREST);
        cairo_rectangle(cr, tx, ty, tileW, tileH);
        cairo_fill(cr);
      }
    }
  }

  static void _cairoRoundedRectPath(cairo_t *cr, double rx, double ry,
                                    double rw, double rh, double radius) {
    double r = radius;
    if (r > rw * 0.5)
      r = rw * 0.5;
    if (r > rh * 0.5)
      r = rh * 0.5;
    cairo_new_path(cr);
    cairo_arc(cr, rx + rw - r, ry + r, r, -M_PI * 0.5, 0.0);
    cairo_arc(cr, rx + rw - r, ry + rh - r, r, 0.0, M_PI * 0.5);
    cairo_arc(cr, rx + r, ry + rh - r, r, M_PI * 0.5, M_PI);
    cairo_arc(cr, rx + r, ry + r, r, M_PI, -M_PI * 0.5);
    cairo_close_path(cr);
  }
#endif

  // =========================================================================
  // NanoVG rendering (Android)
  // =========================================================================
#ifdef __ANDROID__
  void _renderNanoVG(int cx, int cy, int cw, int ch) {
    NVGcontext *vg = FluxAndroid_getVG();
    if (!vg)
      return;

    // Promote pending pixels → GPU texture (must run on GL thread)
    if (pending.ready()) {
      _deleteNvgTexture();
      uploadBuffer = std::move(pending.rgba);
      uploadW = pending.w;
      uploadH = pending.h;
      pending.rgba.clear();
      pending.w = pending.h = 0;

      nvgImage = nvgCreateImageRGBA(
          vg, uploadW, uploadH, NVG_IMAGE_PREMULTIPLIED, uploadBuffer.data());
      if (nvgImage == -1) {
        LOGW_IMG("nvgCreateImageRGBA FAILED");
        _setLoadState(ImageLoadState::Error);
        return;
      }
      nvgImageSize(vg, nvgImage, &imageWidth, &imageHeight);
      if (imageWidth == 0 || imageHeight == 0) {
        _deleteNvgTexture();
        _setLoadState(ImageLoadState::Error);
        return;
      }
      _setLoadState(ImageLoadState::Loaded);
      markNeedsLayout();
    }

    if (nvgImage == -1)
      return;

    DestRect d = _calculateDestRect(cx, cy, cw, ch);

    nvgSave(vg);
    nvgBeginPath(vg);
    if (borderRadius > 0)
      nvgRoundedRect(vg, (float)cx, (float)cy, (float)cw, (float)ch,
                     (float)borderRadius);
    else
      nvgRect(vg, (float)cx, (float)cy, (float)cw, (float)ch);
    nvgClip(vg); // requires NanoVG fork with clip support, else skip

    if (repeat != ImageRepeat::NoRepeat) {
      _renderNanoVGRepeat(vg, d, cx, cy, cw, ch);
    } else {
      NVGpaint paint =
          nvgImagePattern(vg, d.x, d.y, d.w, d.h, 0.0f, nvgImage, 1.0f);
      nvgBeginPath(vg);
      nvgRect(vg, d.x, d.y, d.w, d.h);
      nvgFillPaint(vg, paint);
      nvgFill(vg);
    }

    nvgRestore(vg);
  }

  void _renderNanoVGRepeat(NVGcontext *vg, const DestRect &d, int cx, int cy,
                           int cw, int ch) {
    float tileW = d.w, tileH = d.h;
    float startX = (repeat == ImageRepeat::RepeatY) ? d.x : (float)cx;
    float startY = (repeat == ImageRepeat::RepeatX) ? d.y : (float)cy;
    float endX =
        (repeat == ImageRepeat::RepeatY) ? d.x + tileW : (float)(cx + cw);
    float endY =
        (repeat == ImageRepeat::RepeatX) ? d.y + tileH : (float)(cy + ch);

    for (float ty = startY; ty < endY; ty += tileH) {
      for (float tx = startX; tx < endX; tx += tileW) {
        NVGpaint p =
            nvgImagePattern(vg, tx, ty, tileW, tileH, 0.0f, nvgImage, 1.0f);
        nvgBeginPath(vg);
        nvgRect(vg, tx, ty, tileW, tileH);
        nvgFillPaint(vg, p);
        nvgFill(vg);
      }
    }
  }

  void _deleteNvgTexture() {
    if (nvgImage != -1) {
      NVGcontext *vg = FluxAndroid_getVG();
      if (vg)
        nvgDeleteImage(vg, nvgImage);
      nvgImage = -1;
    }
  }

  void _freeNvgImage() {
    _deleteNvgTexture();
    pending.rgba.clear();
    pending.w = pending.h = 0;
    uploadBuffer.clear();
    uploadW = uploadH = 0;
  }
#endif // __ANDROID__
};

// ============================================================================
// FACTORY FUNCTIONS
// ============================================================================

using ImageWidgetPtr = std::shared_ptr<ImageWidget>;

// Named constructors
// Image::asset("path/to/file.png")
// Image::network("https://...")
// Image::memory(bytes)
// — these are static methods on ImageWidget above —


inline ImageWidgetPtr AssetImage(const std::string &path) {
  return ImageWidget::asset(path);
}

inline ImageWidgetPtr Image() { return std::make_shared<ImageWidget>(); }

inline ImageWidgetPtr NetworkImage(const std::string &url,
                                   bool postToUI = true) {
  return ImageWidget::network(url, postToUI);
}

inline ImageWidgetPtr MemoryImage(const std::vector<uint8_t> &bytes) {
  return ImageWidget::memory(bytes);
}

#endif // FLUX_IMAGE_HPP