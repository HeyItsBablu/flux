#ifndef FLUX_IMAGE_HPP
#define FLUX_IMAGE_HPP

#include "flux_core.hpp"
#include <gdiplus.h>
#include <memory>
#include <string>

#pragma comment(lib, "gdiplus.lib")

// Initialize GDI+ (call this once at app startup)
class GdiplusInitializer {
private:
  ULONG_PTR gdiplusToken;

public:
  GdiplusInitializer() {
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, nullptr);
  }

  ~GdiplusInitializer() { Gdiplus::GdiplusShutdown(gdiplusToken); }
};

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

  COLORREF placeholderColor = RGB(240, 240, 240);
  COLORREF errorColor = RGB(255, 200, 200);
  bool hasError = false;

  ImageWidget() {
    autoWidth = true;
    autoHeight = true;
  }

  ImageWidget(const std::string &path) : ImageWidget() { loadImage(path); }

  bool loadImage(const std::string &path) {
    imagePath = path;
    hasError = false;

std::wstring wpath = toWideString(path);

    // Load bitmap
    bitmap = std::make_unique<Gdiplus::Bitmap>(wpath.c_str());

    if (bitmap->GetLastStatus() != Gdiplus::Ok) {
      hasError = true;
      imageLoaded = false;
      bitmap.reset();
      return false;
    }

    imageWidth = bitmap->GetWidth();
    imageHeight = bitmap->GetHeight();
    imageLoaded = true;

    markNeedsLayout();
    return true;
  }

  void computeLayout(GraphicsContext &/*ctx*/, const BoxConstraints &constraints,
                     FontCache &/*fontCache*/) override {
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

  void render(GraphicsContext &ctx, FontCache &/*fontCache*/) override {
    // Draw background
    if (hasBackground) {
      drawRoundedRectangle(ctx);
    }

    // Draw border
    if (hasBorder) {
      HPEN pen = CreatePen(PS_SOLID, borderWidth, borderColor);
      HPEN oldPen = (HPEN)SelectObject(ctx.hdc, pen);
      HBRUSH oldBrush = (HBRUSH)SelectObject(ctx.hdc, GetStockObject(NULL_BRUSH));

      if (borderRadius > 0) {
        RoundRect(ctx.hdc, x, y, x + width, y + height, borderRadius * 2,
                  borderRadius * 2);
      } else {
        Rectangle(ctx.hdc, x, y, x + width, y + height);
      }

      SelectObject(ctx.hdc, oldBrush);
      SelectObject(ctx.hdc, oldPen);
      DeleteObject(pen);
    }

    int contentX = x + paddingLeft;
    int contentY = y + paddingTop;
    int contentWidth = width - paddingLeft - paddingRight;
    int contentHeight = height - paddingTop - paddingBottom;

    if (imageLoaded && bitmap) {
      Gdiplus::Graphics graphics(ctx.hdc);
      graphics.SetInterpolationMode(
          Gdiplus::InterpolationModeHighQualityBicubic);
      graphics.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);

      // Calculate destination rectangle based on fit mode
      Gdiplus::RectF destRect =
          calculateDestRect(contentX, contentY, contentWidth, contentHeight);

      // Clip to content area if border radius is set
      if (borderRadius > 0) {
        Gdiplus::GraphicsPath path;
        path.AddArc(Gdiplus::Rect(x, y, borderRadius * 2, borderRadius * 2),
                    180, 90);
        path.AddArc(Gdiplus::Rect(x + width - borderRadius * 2, y,
                                  borderRadius * 2, borderRadius * 2),
                    270, 90);
        path.AddArc(Gdiplus::Rect(x + width - borderRadius * 2,
                                  y + height - borderRadius * 2,
                                  borderRadius * 2, borderRadius * 2),
                    0, 90);
        path.AddArc(Gdiplus::Rect(x, y + height - borderRadius * 2,
                                  borderRadius * 2, borderRadius * 2),
                    90, 90);
        path.CloseFigure();

        graphics.SetClip(&path);
      } else {
        graphics.SetClip(
            Gdiplus::Rect(contentX, contentY, contentWidth, contentHeight));
      }

      graphics.DrawImage(bitmap.get(), destRect);
    } else if (hasError) {
      // Draw error state
      HBRUSH errorBrush = CreateSolidBrush(errorColor);
      RECT errorRect = {contentX, contentY, contentX + contentWidth,
                        contentY + contentHeight};
      FillRect(ctx.hdc, &errorRect, errorBrush);
      DeleteObject(errorBrush);

      // Draw error icon/text
      SetTextColor(ctx.hdc, RGB(150, 0, 0));
      SetBkMode(ctx.hdc, TRANSPARENT);
      DrawText(ctx.hdc, "✖", -1, &errorRect,
               DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    } else {
      // Draw placeholder
      HBRUSH placeholderBrush = CreateSolidBrush(placeholderColor);
      RECT placeholderRect = {contentX, contentY, contentX + contentWidth,
                              contentY + contentHeight};
      FillRect(ctx.hdc, &placeholderRect, placeholderBrush);
      DeleteObject(placeholderBrush);

      // Draw placeholder icon
      SetTextColor(ctx.hdc, RGB(180, 180, 180));
      SetBkMode(ctx.hdc, TRANSPARENT);
      DrawText(ctx.hdc, "🖼", -1, &placeholderRect,
               DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    needsPaint = false;
  }

  std::shared_ptr<ImageWidget> setImagePath(const std::string &path) {
    loadImage(path);
    return std::static_pointer_cast<ImageWidget>(shared_from_this());
  }

  std::shared_ptr<ImageWidget> setFit(ImageFit fitMode) {
    fit = fitMode;
    markNeedsPaint();
    return std::static_pointer_cast<ImageWidget>(shared_from_this());
  }

  std::shared_ptr<ImageWidget> setPlaceholderColor(COLORREF color) {
    placeholderColor = color;
    markNeedsPaint();
    return std::static_pointer_cast<ImageWidget>(shared_from_this());
  }

  std::shared_ptr<ImageWidget> setErrorColor(COLORREF color) {
    errorColor = color;
    markNeedsPaint();
    return std::static_pointer_cast<ImageWidget>(shared_from_this());
  }

  // Fluent interface wrappers to return ImageWidget* for method chaining
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

  //   std::shared_ptr<ImageWidget> setBorderColor(COLORREF color) {
  //     Widget::setBorderColor(color);
  //     return std::static_pointer_cast<ImageWidget>(shared_from_this());
  //   }

  //   std::shared_ptr<ImageWidget> setBorderWidth(int w) {
  //     Widget::setBorderWidth(w);
  //     return std::static_pointer_cast<ImageWidget>(shared_from_this());
  //   }

  std::shared_ptr<ImageWidget> setPadding(int p) {
    padding = p;
    paddingLeft = paddingRight = paddingTop = paddingBottom = p;
    markNeedsLayout();
    return std::static_pointer_cast<ImageWidget>(shared_from_this());
  }

  //   std::shared_ptr<ImageWidget> setBackgroundColor(COLORREF color) {
  //     Widget::setBackgroundColor(color);
  //     return std::static_pointer_cast<ImageWidget>(shared_from_this());
  //   }

private:
  Gdiplus::RectF calculateDestRect(int contentX, int contentY, int contentWidth,
                                   int contentHeight) {
    float imgAspect = (float)imageWidth / imageHeight;
    float containerAspect = (float)contentWidth / contentHeight;

    switch (fit) {
    case ImageFit::Fill:
      // Stretch to fill (may distort)
      return Gdiplus::RectF((float)contentX, (float)contentY,
                            (float)contentWidth, (float)contentHeight);

    case ImageFit::Contain: {
      // Fit inside container (maintain aspect ratio)
      float scale;
      float destWidth, destHeight;

      if (imgAspect > containerAspect) {
        // Image is wider - fit to width
        scale = (float)contentWidth / imageWidth;
        destWidth = (float)contentWidth;
        destHeight = imageHeight * scale;
      } else {
        // Image is taller - fit to height
        scale = (float)contentHeight / imageHeight;
        destWidth = imageWidth * scale;
        destHeight = (float)contentHeight;
      }

      float offsetX = (contentWidth - destWidth) / 2;
      float offsetY = (contentHeight - destHeight) / 2;

      return Gdiplus::RectF(contentX + offsetX, contentY + offsetY, destWidth,
                            destHeight);
    }

    case ImageFit::Cover: {
      // Cover entire container (may crop)
      float scale;
      float destWidth, destHeight;

      if (imgAspect > containerAspect) {
        // Image is wider - fit to height
        scale = (float)contentHeight / imageHeight;
        destWidth = imageWidth * scale;
        destHeight = (float)contentHeight;
      } else {
        // Image is taller - fit to width
        scale = (float)contentWidth / imageWidth;
        destWidth = (float)contentWidth;
        destHeight = imageHeight * scale;
      }

      float offsetX = (contentWidth - destWidth) / 2;
      float offsetY = (contentHeight - destHeight) / 2;

      return Gdiplus::RectF(contentX + offsetX, contentY + offsetY, destWidth,
                            destHeight);
    }

    case ImageFit::None: {
      // Display at original size, centered
      float offsetX = (contentWidth - imageWidth) / 2.0f;
      float offsetY = (contentHeight - imageHeight) / 2.0f;

      return Gdiplus::RectF(contentX + offsetX, contentY + offsetY,
                            (float)imageWidth, (float)imageHeight);
    }

    case ImageFit::ScaleDown: {
      // Like None, but scale down if too large
      if (imageWidth <= contentWidth && imageHeight <= contentHeight) {
        // Image fits - display at original size
        float offsetX = (contentWidth - imageWidth) / 2.0f;
        float offsetY = (contentHeight - imageHeight) / 2.0f;

        return Gdiplus::RectF(contentX + offsetX, contentY + offsetY,
                              (float)imageWidth, (float)imageHeight);
      } else {
        // Image too large - scale down like Contain
        float scale;
        float destWidth, destHeight;

        if (imgAspect > containerAspect) {
          scale = (float)contentWidth / imageWidth;
          destWidth = (float)contentWidth;
          destHeight = imageHeight * scale;
        } else {
          scale = (float)contentHeight / imageHeight;
          destWidth = imageWidth * scale;
          destHeight = (float)contentHeight;
        }

        float offsetX = (contentWidth - destWidth) / 2;
        float offsetY = (contentHeight - destHeight) / 2;

        return Gdiplus::RectF(contentX + offsetX, contentY + offsetY, destWidth,
                              destHeight);
      }
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

// ============================================================================
// USAGE EXAMPLES
// ============================================================================

/*

// 1. Basic image with auto size
Image("photo.jpg");

// 2. Image with fixed size
Image("photo.jpg")
    ->setWidth(200)
    ->setHeight(150);

// 3. Image with fit modes
Image("photo.jpg")
    ->setWidth(300)
    ->setHeight(200)
    ->setFit(ImageFit::Cover);

// 4. Rounded image
Image("avatar.png")
    ->setWidth(100)
    ->setHeight(100)
    ->setBorderRadius(50);  // Circle

// 5. Image with border
Image("photo.jpg")
    ->setWidth(400)
    ->setHeight(300)
    ->setBorderColor(RGB(200, 200, 200))
    ->setBorderWidth(2)
    ->setBorderRadius(8);

// 6. Image in a card layout
Container(
    Column(
        Image("product.jpg")
            ->setWidth(300)
            ->setHeight(200)
            ->setFit(ImageFit::Cover)
            ->setBorderRadius(8),
        Text("Product Name")
            ->setFontWeight(FontWeight::Bold),
        Text("$99.99")
            ->setTextColor(RGB(76, 175, 80))
    )->setSpacing(12)
)
->setPadding(16)
->setBackgroundColor(RGB(255, 255, 255))
->setBorderRadius(12);

// 7. Image with different fit modes
Row(
    Image("photo.jpg")->setWidth(150)->setHeight(150)->setFit(ImageFit::Fill),
    Image("photo.jpg")->setWidth(150)->setHeight(150)->setFit(ImageFit::Contain),
    Image("photo.jpg")->setWidth(150)->setHeight(150)->setFit(ImageFit::Cover)
)->setSpacing(10);

// 8. Lazy load image
auto img = Image();
img->setWidth(200)->setHeight(200);
// Later...
img->setImagePath("loaded.jpg");

*/

#endif // FLUX_IMAGE_HPP