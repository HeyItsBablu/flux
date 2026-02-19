#include "flux.hpp"
#include "flux_graph.hpp"
#include "flux_image_editor.hpp"
#include <cmath>
#include <commdlg.h>
#include <windows.h>

#pragma comment(lib, "comdlg32.lib")

// ============================================================================
// stb_image_write — define implementation in exactly ONE .cpp file
// ============================================================================
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

// ============================================================================
// EXPORT HELPERS
// ============================================================================

static std::string getSavePath(HWND owner = nullptr) {
  char buf[MAX_PATH] = {};
  OPENFILENAMEA ofn = {};
  ofn.lStructSize = sizeof(ofn);
  ofn.hwndOwner = owner;
  ofn.lpstrFilter =
      "PNG Image\0*.png\0JPEG Image\0*.jpg;*.jpeg\0All Files\0*.*\0";
  ofn.lpstrFile = buf;
  ofn.nMaxFile = MAX_PATH;
  ofn.lpstrDefExt = "png";
  ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
  if (GetSaveFileNameA(&ofn))
    return buf;
  return {};
}

static std::string getOpenPath(HWND owner = nullptr) {
  char buf[MAX_PATH] = {};
  OPENFILENAMEA ofn = {};
  ofn.lStructSize = sizeof(ofn);
  ofn.hwndOwner = owner;
  ofn.lpstrFilter = "Image Files\0*.png;*.jpg;*.jpeg;*.bmp\0All Files\0*.*\0";
  ofn.lpstrFile = buf;
  ofn.nMaxFile = MAX_PATH;
  ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
  if (GetOpenFileNameA(&ofn))
    return buf;
  return {};
}

static bool exportTexture(GLuint tex, int w, int h, const std::string &path) {
  if (!tex || w <= 0 || h <= 0 || path.empty())
    return false;

  std::vector<unsigned char> pixels(w * h * 4);

  GLuint fbo = 0;
  gl2.GenFramebuffers(1, &fbo);
  gl2.BindFramebuffer(GL_FRAMEBUFFER, fbo);
  gl2.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           tex, 0);

  glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

  gl2.BindFramebuffer(GL_FRAMEBUFFER, 0);
  gl2.DeleteFramebuffers(1, &fbo);

  int rowBytes = w * 4;
  std::vector<unsigned char> rowBuf(rowBytes);
  for (int y = 0; y < h / 2; ++y) {
    unsigned char *top = pixels.data() + y * rowBytes;
    unsigned char *bot = pixels.data() + (h - 1 - y) * rowBytes;
    memcpy(rowBuf.data(), top, rowBytes);
    memcpy(top, bot, rowBytes);
    memcpy(bot, rowBuf.data(), rowBytes);
  }

  std::string ext;
  auto dot = path.rfind('.');
  if (dot != std::string::npos) {
    ext = path.substr(dot + 1);
    for (auto &c : ext)
      c = (char)tolower((unsigned char)c);
  }

  int ok = 0;
  if (ext == "jpg" || ext == "jpeg")
    ok = stbi_write_jpg(path.c_str(), w, h, 4, pixels.data(), 92);
  else
    ok = stbi_write_png(path.c_str(), w, h, 4, pixels.data(), rowBytes);

  return ok != 0;
}

// ============================================================================
// HELPER — builds one labeled slider row
// ============================================================================
static WidgetPtr makeSlider(const std::string &label, double minVal,
                            double maxVal, double step, COLORREF trackColor,
                            std::function<void(double)> onChange) {
  return Column(Text(label)->setWidth(100)->setTextColor(RGB(255, 255, 255)),
                Slider(minVal, maxVal, step)
                    ->setTrackFillColor(trackColor)
                    ->setOnValueChanged(onChange));
}

// ============================================================================
// PHOTO EDITOR COMPONENT
// ============================================================================
class PushGraphComponent : public Component {
private:
  State<ImageAdjustments> adj{{}, context};
  State<std::string> imagePath{"", context};

  // Only valid after Then() has been rendered at least once
  std::shared_ptr<EditableImageWidget> imageWidget;

public:
  WidgetPtr build() override {

    return Scaffold(
        AppBar("Photo Editor"),
        Container(
            Center(
                Conditional(imagePath,
                            [](const std::string &v) { return !v.empty(); })
                    ->Then([this]() -> WidgetPtr {
                      // Build imageWidget fresh each time Then() is entered
                      imageWidget =
                          EditableImage(imagePath.get(), 800, 600)
                              ->setAdjustments(adj)
                              ->setFitMode(
                                  EditableImageWidget::FitMode::Contain);

                      const COLORREF purple = RGB(130, 100, 255);

                      auto sliders =
                          Column(
                              makeSlider("Exposure", -5.0, 5.0, 0.01,
                                         RGB(255, 200, 80),
                                         [this](double v) {
                                           auto a = adj.get();
                                           a.exposure = (float)v;
                                           adj.set(a);
                                         }),
                              makeSlider("Contrast", -100.0, 100.0, 1.0, purple,
                                         [this](double v) {
                                           auto a = adj.get();
                                           a.contrast = (float)v;
                                           adj.set(a);
                                         }),
                              makeSlider("Highlights", -100.0, 100.0, 1.0,
                                         purple,
                                         [this](double v) {
                                           auto a = adj.get();
                                           a.highlights = (float)v;
                                           adj.set(a);
                                         }),
                              makeSlider("Shadows", -100.0, 100.0, 1.0, purple,
                                         [this](double v) {
                                           auto a = adj.get();
                                           a.shadows = (float)v;
                                           adj.set(a);
                                         }),
                              makeSlider("Whites", -100.0, 100.0, 1.0, purple,
                                         [this](double v) {
                                           auto a = adj.get();
                                           a.whites = (float)v;
                                           adj.set(a);
                                         }),
                              makeSlider("Blacks", -100.0, 100.0, 1.0, purple,
                                         [this](double v) {
                                           auto a = adj.get();
                                           a.blacks = (float)v;
                                           adj.set(a);
                                         }),
                              makeSlider("Clarity", -100.0, 100.0, 1.0, purple,
                                         [this](double v) {
                                           auto a = adj.get();
                                           a.clarity = (float)v;
                                           adj.set(a);
                                         }),
                              makeSlider("Saturation", -100.0, 100.0, 1.0,
                                         purple,
                                         [this](double v) {
                                           auto a = adj.get();
                                           a.saturation = (float)v;
                                           adj.set(a);
                                         }),
                              makeSlider("Vibrance", -100.0, 100.0, 1.0, purple,
                                         [this](double v) {
                                           auto a = adj.get();
                                           a.vibrance = (float)v;
                                           adj.set(a);
                                         }),
                              makeSlider("Temperature", -100.0, 100.0, 1.0,
                                         purple,
                                         [this](double v) {
                                           auto a = adj.get();
                                           a.temperature = (float)v;
                                           adj.set(a);
                                         }),
                              makeSlider("Tint", -100.0, 100.0, 1.0, purple,
                                         [this](double v) {
                                           auto a = adj.get();
                                           a.tint = (float)v;
                                           adj.set(a);
                                         }),
                              makeSlider("Gamma", 0.1, 5.0, 0.01, purple,
                                         [this](double v) {
                                           auto a = adj.get();
                                           a.gamma = (float)v;
                                           adj.set(a);
                                         }),

                              // ── Action buttons ─────────────────────────
                              Button(Text("Open New Image"),
                                     [this] {
                                       std::string path = getOpenPath();
                                       if (!path.empty()) {
                                         imagePath.set(path);
                                         if (imageWidget)
                                           imageWidget->loadImage(path);
                                       }
                                     }),

                              Button(Text("Export"),
                                     [this] {
                                       if (!imageWidget)
                                         return;

                                       std::string path = getSavePath();
                                       if (path.empty())
                                         return;

                                       wglMakeCurrent(imageWidget->getGLDC(),
                                                      imageWidget->getGLRC());

                                       bool ok = exportTexture(
                                           imageWidget->getOutputTexture(),
                                           imageWidget->imageWidth(),
                                           imageWidget->imageHeight(), path);

                                       if (ok)
                                         MessageBoxA(
                                             nullptr,
                                             ("Saved to:\n" + path).c_str(),
                                             "Export successful",
                                             MB_OK | MB_ICONINFORMATION);
                                       else
                                         MessageBoxA(
                                             nullptr,
                                             "Failed to write image file.",
                                             "Export failed",
                                             MB_OK | MB_ICONERROR);
                                     }))
                              ->setSpacing(10);

                      return Row(imageWidget, Container(sliders)->setWidth(200))
                          ->setSpacing(20)
                          ->setCrossAlignment(Alignment::Center);
                    })
                    ->Else([this]() -> WidgetPtr {
                      return Container(
                                 Column(
                                     // Icon area
                                     GestureDetector(
                                         Container(
                                             Center(
                                                 Column(
                                                     Text("⬆")
                                                         ->setFontSize(48)
                                                         ->setTextColor(RGB(
                                                             130, 100, 255)),
                                                     Text("Drop image here")
                                                         ->setFontSize(18)
                                                         ->setFontWeight(
                                                             FontWeight::Bold)
                                                         ->setTextColor(RGB(
                                                             255, 255, 255)),
                                                     Text("or click to browse")
                                                         ->setFontSize(13)
                                                         ->setTextColor(RGB(
                                                             140, 140, 160)))
                                                     ->setSpacing(10)
                                                     ->setCrossAlignment(
                                                         Alignment::Center)))
                                             ->setWidth(420)
                                             ->setHeight(260)
                                             ->setBackgroundColor(
                                                 RGB(30, 34, 52))
                                             ->setBorderColor(
                                                 RGB(130, 100, 255))
                                             ->setBorderWidth(2)
                                             ->setBorderRadius(16)
                                             ->setHoverBackgroundColor(
                                                 RGB(38, 42, 64))
                                             ->setHoverBorderColor(
                                                 RGB(170, 140, 255)))
                                         ->setOnTap([this] {
                                           std::cout << "Button Clicked"
                                                     << std ::endl;
                                           std::string path = getOpenPath();

                                           if (!path.empty())
                                             imagePath.set(path);
                                         }),

                                     // Divider row
                                     Row(Container()
                                             ->setHeight(1)
                                             ->setFlex(1)
                                             ->setBackgroundColor(
                                                 RGB(50, 54, 72)),
                                         Text("supported formats")
                                             ->setFontSize(11)
                                             ->setTextColor(RGB(80, 84, 100)),
                                         Container()
                                             ->setHeight(1)
                                             ->setFlex(1)
                                             ->setBackgroundColor(
                                                 RGB(50, 54, 72)))
                                         ->setSpacing(12)
                                         ->setCrossAlignment(Alignment::Center)
                                         ->setWidth(420),

                                     // Format badges
                                     Row(Container(Text("PNG")
                                                       ->setFontSize(11)
                                                       ->setTextColor(
                                                           RGB(130, 100, 255)))
                                             ->setPadding(6)
                                             ->setBorderRadius(6)
                                             ->setBackgroundColor(
                                                 RGB(130, 100, 255, 25))
                                             ->setBorderColor(
                                                 RGB(130, 100, 255))
                                             ->setBorderWidth(1),
                                         Container(Text("JPG")
                                                       ->setFontSize(11)
                                                       ->setTextColor(
                                                           RGB(130, 100, 255)))
                                             ->setPadding(6)
                                             ->setBorderRadius(6)
                                             ->setBackgroundColor(
                                                 RGB(130, 100, 255, 25))
                                             ->setBorderColor(
                                                 RGB(130, 100, 255))
                                             ->setBorderWidth(1),
                                         Container(Text("BMP")
                                                       ->setFontSize(11)
                                                       ->setTextColor(
                                                           RGB(130, 100, 255)))
                                             ->setPadding(6)
                                             ->setBorderRadius(6)
                                             ->setBackgroundColor(
                                                 RGB(130, 100, 255, 25))
                                             ->setBorderColor(
                                                 RGB(130, 100, 255))
                                             ->setBorderWidth(1),
                                         Container(Text("JPEG")
                                                       ->setFontSize(11)
                                                       ->setTextColor(
                                                           RGB(130, 100, 255)))
                                             ->setPadding(6)
                                             ->setBorderRadius(6)
                                             ->setBackgroundColor(
                                                 RGB(130, 100, 255, 25))
                                             ->setBorderColor(
                                                 RGB(130, 100, 255))
                                             ->setBorderWidth(1))
                                         ->setSpacing(8))
                                     ->setSpacing(24)
                                     ->setCrossAlignment(Alignment::Center))
                          ->setBackgroundColor(RGB(20, 24, 36))
                          ->setPadding(60);
                    })))
            ->setBackgroundColor(RGB(20, 24, 36))
            ->setPadding(30));
  }
};

// ============================================================================
// ENTRY POINT
// ============================================================================
WidgetPtr createApp(FluxUI *app) {
  return FluxApp("Photo Editor", BuildComponent<PushGraphComponent>(),
                 AppTheme::materialBlue());
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
  AllocConsole();
  FILE *fp;
  freopen_s(&fp, "CONOUT$", "w", stdout);

  GdiplusInitializer gdiplusInit;
  FluxUI app(hInstance);
  app.build([&]() { return createApp(&app); });
  int screenWidth = GetSystemMetrics(SM_CXSCREEN);
  int screenHeight = GetSystemMetrics(SM_CYSCREEN);

  app.createWindow("FluxUI - Photo Editor", screenWidth, screenHeight);

  return app.run();
}