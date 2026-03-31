#ifndef FLUX_IMAGE_HPP
#define FLUX_IMAGE_HPP

#include "flux_core.hpp"
#include <gdiplus.h>
#include <memory>
#include <string>

// ============================================================================
// IMAGE FIT MODES
// ============================================================================

enum class ImageFit {
  Fill,    // Stretch to fill entire widget (may distort)
  Contain, // Scale to fit inside widget (maintains aspect ratio, may have empty
           // space)
  Cover,   // Scale to cover entire widget (maintains aspect ratio, may crop)
  None,    // Display at original size
  ScaleDown // Like None, but scales down if image is larger than widget
};

// ============================================================================
// IMAGE WIDGET
// ============================================================================

class ImageWidget : public Widget {
public:
  std::string imagePath;
  std::unique_ptr<Gdiplus::Bitmap> bitmap;
  ImageFit fit = ImageFit::Contain;

  int imageWidth = 0;
  int imageHeight = 0;
  bool imageLoaded = false;
  bool hasError = false;

  Color placeholderColor = Color::fromRGB(240, 240, 240);
  Color errorColor = Color::fromRGB(255, 200, 200);

  ImageWidget() {
    autoWidth = true;
    autoHeight = true;
  }

  explicit ImageWidget(const std::string &path) : ImageWidget() {
    loadImage(path);
  }

bool loadImage(const std::string &path) {
    if (path.empty()) {
        std::cout << "[ImageWidget] loadImage: empty path\n";
        hasError    = false;
        imageLoaded = false;
        return false;
    }

    imagePath = path;
    hasError  = false;

    // Resolve to absolute path
    wchar_t absPath[MAX_PATH] = {};
    std::wstring wpath = toWideString(path);
    if (!GetFullPathNameW(wpath.c_str(), MAX_PATH, absPath, nullptr)) {
        std::cout << "[ImageWidget] GetFullPathNameW failed for: " << path << "\n";
        hasError    = true;
        imageLoaded = false;
        return false;
    }

    // Log the resolved path so we can see exactly what GDI+ is trying to open
    std::wstring resolvedW(absPath);
    std::string  resolved(resolvedW.begin(), resolvedW.end());
    std::cout << "[ImageWidget] loading: " << resolved << "\n";

    try {
        bitmap = std::make_unique<Gdiplus::Bitmap>(absPath);

        if (!bitmap) {
            std::cout << "[ImageWidget] Bitmap allocation failed: " << resolved << "\n";
            hasError    = true;
            imageLoaded = false;
            return false;
        }

        Gdiplus::Status status = bitmap->GetLastStatus();
        if (status != Gdiplus::Ok) {
            std::cout << "[ImageWidget] GDI+ status " << (int)status
                      << " for: " << resolved << "\n";
            hasError    = true;
            imageLoaded = false;
            bitmap.reset();
            return false;
        }

    } catch (const std::exception &e) {
        std::cout << "[ImageWidget] exception: " << e.what()
                  << " loading: " << resolved << "\n";
        hasError    = true;
        imageLoaded = false;
        bitmap.reset();
        return false;
    } catch (...) {
        std::cout << "[ImageWidget] unknown exception loading: " << resolved << "\n";
        hasError    = true;
        imageLoaded = false;
        bitmap.reset();
        return false;
    }

    imageWidth  = (int)bitmap->GetWidth();
    imageHeight = (int)bitmap->GetHeight();

    std::cout << "[ImageWidget] loaded OK: " << resolved
              << "  " << imageWidth << "x" << imageHeight << "\n";

    if (imageWidth == 0 || imageHeight == 0) {
        std::cout << "[ImageWidget] zero dimensions, rejecting: " << resolved << "\n";
        hasError    = true;
        imageLoaded = false;
        bitmap.reset();
        return false;
    }

    imageLoaded = true;
    markNeedsLayout();
    return true;
}

  // ── Layout ────────────────────────────────────────────────────────────
  void computeLayout(GraphicsContext & /*ctx*/,
                     const BoxConstraints &constraints,
                     FontCache & /*fontCache*/) override {
    if (imageLoaded) {
      if (autoWidth && autoHeight) {
        width = imageWidth;
        height = imageHeight;
      } else if (autoWidth) {
        float aspect = (float)imageWidth / imageHeight;
        width = (int)(height * aspect);
      } else if (autoHeight) {
        float aspect = (float)imageHeight / imageWidth;
        height = (int)(width * aspect);
      }
    } else {
      if (autoWidth)
        width = 100;
      if (autoHeight)
        height = 100;
    }

    width = constraints.clampWidth(width + paddingLeft + paddingRight);
    height = constraints.clampHeight(height + paddingTop + paddingBottom);

    applyConstraints();
    needsLayout = false;
  }

  // ── Render ────────────────────────────────────────────────────────────
  void render(GraphicsContext &ctx, FontCache &fontCache) override {
    Painter painter(ctx);

    // Background
    if (hasBackground)
      drawRoundedRectangle(ctx);

    // Border
    if (hasBorder)
      painter.drawRoundedRectOutline(x, y, width, height, borderRadius * 2,
                                     getCurrentBorderColor(), borderWidth);

    int contentX = x + paddingLeft;
    int contentY = y + paddingTop;
    int contentWidth = width - paddingLeft - paddingRight;
    int contentHeight = height - paddingTop - paddingBottom;

    if (imageLoaded && bitmap) {
      Gdiplus::Graphics graphics(ctx.hdc);
      graphics.SetInterpolationMode(
          Gdiplus::InterpolationModeHighQualityBicubic);
      graphics.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);

      Gdiplus::RectF destRect =
          _calculateDestRect(contentX, contentY, contentWidth, contentHeight);

      // Clip to rounded content area if needed
      if (borderRadius > 0) {
        Gdiplus::GraphicsPath path;
        int d = borderRadius * 2;
        path.AddArc(Gdiplus::Rect(x, y, d, d), 180, 90);
        path.AddArc(Gdiplus::Rect(x + width - d, y, d, d), 270, 90);
        path.AddArc(Gdiplus::Rect(x + width - d, y + height - d, d, d), 0, 90);
        path.AddArc(Gdiplus::Rect(x, y + height - d, d, d), 90, 90);
        path.CloseFigure();
        graphics.SetClip(&path);
      } else {
        graphics.SetClip(
            Gdiplus::Rect(contentX, contentY, contentWidth, contentHeight));
      }

      graphics.DrawImage(bitmap.get(), destRect);

    } else if (hasError) {
      painter.fillRect(contentX, contentY, contentWidth, contentHeight,
                       errorColor);
      painter.drawTextA("x", contentX, contentY, contentWidth, contentHeight,
                        fontCache.getFont(fontSize, fontWeight),
                        Color::fromRGB(150, 0, 0),
                        DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    } else {
      painter.fillRect(contentX, contentY, contentWidth, contentHeight,
                       placeholderColor);
      painter.drawTextA("[ ]", contentX, contentY, contentWidth, contentHeight,
                        fontCache.getFont(fontSize, fontWeight),
                        Color::fromRGB(180, 180, 180),
                        DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    needsPaint = false;
  }

  // ── Fluent setters ────────────────────────────────────────────────────

  std::shared_ptr<ImageWidget> setImagePath(const std::string &path) {
    loadImage(path);
    return std::static_pointer_cast<ImageWidget>(shared_from_this());
  }

  std::shared_ptr<ImageWidget> setFit(ImageFit fitMode) {
    fit = fitMode;
    markNeedsPaint();
    return std::static_pointer_cast<ImageWidget>(shared_from_this());
  }

  std::shared_ptr<ImageWidget> setPlaceholderColor(Color color) {
    placeholderColor = color;
    markNeedsPaint();
    return std::static_pointer_cast<ImageWidget>(shared_from_this());
  }

  std::shared_ptr<ImageWidget> setErrorColor(Color color) {
    errorColor = color;
    markNeedsPaint();
    return std::static_pointer_cast<ImageWidget>(shared_from_this());
  }

  std::shared_ptr<ImageWidget> setWidth(int w) {
    if (width != w) {
      width = w;
      autoWidth = false;
      markNeedsLayout();
    }
    return std::static_pointer_cast<ImageWidget>(shared_from_this());
  }

  std::shared_ptr<ImageWidget> setHeight(int h) {
    if (height != h) {
      height = h;
      autoHeight = false;
      markNeedsLayout();
    }
    return std::static_pointer_cast<ImageWidget>(shared_from_this());
  }

  std::shared_ptr<ImageWidget> setBorderRadius(int r) {
    if (borderRadius != r) {
      borderRadius = r;
      markNeedsPaint();
    }
    return std::static_pointer_cast<ImageWidget>(shared_from_this());
  }

  std::shared_ptr<ImageWidget> setPadding(int p) {
    padding = p;
    paddingLeft = paddingRight = paddingTop = paddingBottom = p;
    markNeedsLayout();
    return std::static_pointer_cast<ImageWidget>(shared_from_this());
  }

private:
  Gdiplus::RectF _calculateDestRect(int contentX, int contentY,
                                    int contentWidth, int contentHeight) const {
    float imgAspect = (float)imageWidth / imageHeight;
    float containerAspect = (float)contentWidth / contentHeight;

    switch (fit) {
    case ImageFit::Fill:
      return Gdiplus::RectF((float)contentX, (float)contentY,
                            (float)contentWidth, (float)contentHeight);

    case ImageFit::Contain: {
      float scale, destW, destH;
      if (imgAspect > containerAspect) {
        scale = (float)contentWidth / imageWidth;
        destW = (float)contentWidth;
        destH = imageHeight * scale;
      } else {
        scale = (float)contentHeight / imageHeight;
        destW = imageWidth * scale;
        destH = (float)contentHeight;
      }
      float offX = (contentWidth - destW) / 2;
      float offY = (contentHeight - destH) / 2;
      return Gdiplus::RectF(contentX + offX, contentY + offY, destW, destH);
    }

    case ImageFit::Cover: {
      float scale, destW, destH;
      if (imgAspect > containerAspect) {
        scale = (float)contentHeight / imageHeight;
        destW = imageWidth * scale;
        destH = (float)contentHeight;
      } else {
        scale = (float)contentWidth / imageWidth;
        destW = (float)contentWidth;
        destH = imageHeight * scale;
      }
      float offX = (contentWidth - destW) / 2;
      float offY = (contentHeight - destH) / 2;
      return Gdiplus::RectF(contentX + offX, contentY + offY, destW, destH);
    }

    case ImageFit::None: {
      float offX = (contentWidth - imageWidth) / 2.0f;
      float offY = (contentHeight - imageHeight) / 2.0f;
      return Gdiplus::RectF(contentX + offX, contentY + offY, (float)imageWidth,
                            (float)imageHeight);
    }

    case ImageFit::ScaleDown: {
      if (imageWidth <= contentWidth && imageHeight <= contentHeight) {
        float offX = (contentWidth - imageWidth) / 2.0f;
        float offY = (contentHeight - imageHeight) / 2.0f;
        return Gdiplus::RectF(contentX + offX, contentY + offY,
                              (float)imageWidth, (float)imageHeight);
      }
      // Fall through to Contain behaviour
      float scale, destW, destH;
      if (imgAspect > containerAspect) {
        scale = (float)contentWidth / imageWidth;
        destW = (float)contentWidth;
        destH = imageHeight * scale;
      } else {
        scale = (float)contentHeight / imageHeight;
        destW = imageWidth * scale;
        destH = (float)contentHeight;
      }
      float offX = (contentWidth - destW) / 2;
      float offY = (contentHeight - destH) / 2;
      return Gdiplus::RectF(contentX + offX, contentY + offY, destW, destH);
    }

    default:
      return Gdiplus::RectF((float)contentX, (float)contentY,
                            (float)contentWidth, (float)contentHeight);
    }
  }
};

// ============================================================================
// FACTORY FUNCTIONS
// ============================================================================

using ImageWidgetPtr = std::shared_ptr<ImageWidget>;

inline ImageWidgetPtr Image(const std::string &path) {
  return std::make_shared<ImageWidget>(path);
}

inline ImageWidgetPtr Image() { return std::make_shared<ImageWidget>(); }

#endif // FLUX_IMAGE_HPP