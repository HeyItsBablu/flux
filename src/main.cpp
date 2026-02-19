#include "flux.hpp"
#include "flux_graph.hpp"
#include "flux_image_editor.hpp"
#include <cmath>
#include <windows.h>
#include <commdlg.h>

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

// Opens a native Windows "Save As" dialog and returns the chosen path,
// or an empty string if the user cancelled.
static std::string getSavePath(HWND owner = nullptr) {
  char buf[MAX_PATH] = {};
  OPENFILENAMEA ofn   = {};
  ofn.lStructSize     = sizeof(ofn);
  ofn.hwndOwner       = owner;
  ofn.lpstrFilter     = "PNG Image\0*.png\0JPEG Image\0*.jpg;*.jpeg\0All Files\0*.*\0";
  ofn.lpstrFile       = buf;
  ofn.nMaxFile        = MAX_PATH;
  ofn.lpstrDefExt     = "png";
  ofn.Flags           = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
  if (GetSaveFileNameA(&ofn))
    return buf;
  return {};
}

// Reads back the contents of an OpenGL texture (via a temporary FBO),
// flips it vertically (GL origin is bottom-left), then writes it to disk.
// Supports .png and .jpg/.jpeg extensions; everything else is saved as PNG.
static bool exportTexture(GLuint tex, int w, int h, const std::string &path) {
  if (!tex || w <= 0 || h <= 0 || path.empty())
    return false;

  // ── read pixels ──────────────────────────────────────────────────────────
  std::vector<unsigned char> pixels(w * h * 4);

  GLuint fbo = 0;
  gl2.GenFramebuffers(1, &fbo);
  gl2.BindFramebuffer(GL_FRAMEBUFFER, fbo);
  gl2.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, tex, 0);

  glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

  gl2.BindFramebuffer(GL_FRAMEBUFFER, 0);
  gl2.DeleteFramebuffers(1, &fbo);

  // ── vertical flip ────────────────────────────────────────────────────────
  int rowBytes = w * 4;
  std::vector<unsigned char> rowBuf(rowBytes);
  for (int y = 0; y < h / 2; ++y) {
    unsigned char *top = pixels.data() + y * rowBytes;
    unsigned char *bot = pixels.data() + (h - 1 - y) * rowBytes;
    memcpy(rowBuf.data(), top, rowBytes);
    memcpy(top, bot, rowBytes);
    memcpy(bot, rowBuf.data(), rowBytes);
  }

  // ── write file ───────────────────────────────────────────────────────────
  // Determine format by extension (case-insensitive)
  std::string ext;
  auto dot = path.rfind('.');
  if (dot != std::string::npos) {
    ext = path.substr(dot + 1);
    for (auto &c : ext) c = (char)tolower((unsigned char)c);
  }

  int ok = 0;
  if (ext == "jpg" || ext == "jpeg")
    ok = stbi_write_jpg(path.c_str(), w, h, 4, pixels.data(), 92 /*quality*/);
  else
    ok = stbi_write_png(path.c_str(), w, h, 4, pixels.data(), rowBytes);

  return ok != 0;
}

// ============================================================================
// PHOTO EDITOR COMPONENT
// ============================================================================
class PushGraphComponent : public Component {
private:
  State<ImageAdjustments> adj{{}, context};
  State<bool> showOriginal{false, context};

  // Keep a pointer to the image widget so we can read its texture on export.
  std::shared_ptr<EditableImageWidget> imageWidget;

public:
  WidgetPtr build() override {

    imageWidget =
        EditableImage("C:/Upwork/c_projects/flux/src/images/main.png", 800, 600)
            ->setAdjustments(adj)
            ->setFitMode(EditableImageWidget::FitMode::Contain);

    auto controls =
        Container(
            Column(
                // ── Exposure ──────────────────────────────────────
                Column(Text("Exposure")
                           ->setWidth(100)
                           ->setTextColor(RGB(255, 255, 255)),
                       Slider(-5.0, 5.0, 0.01)
                           ->setOnValueChanged([&](double v) {
                             auto a = adj.get();
                             a.exposure = (float)v;
                             adj.set(a);
                           })),
                // ── Contrast ──────────────────────────────────────
                Column(Text("Contrast")
                           ->setWidth(100)
                           ->setTextColor(RGB(255, 255, 255)),
                       Slider(-100.0, 100.0, 1.0)
                           ->setTrackFillColor(RGB(130, 100, 255))
                           ->setOnValueChanged([&](double v) {
                             auto a = adj.get();
                             a.contrast = (float)v;
                             adj.set(a);
                           })),
                // ── Highlights ────────────────────────────────────
                Column(Text("Highlights")
                           ->setWidth(100)
                           ->setTextColor(RGB(255, 255, 255)),
                       Slider(-100.0, 100.0, 1.0)
                           ->setTrackFillColor(RGB(130, 100, 255))
                           ->setOnValueChanged([&](double v) {
                             auto a = adj.get();
                             a.highlights = (float)v;
                             adj.set(a);
                           })),
                // ── Shadows ───────────────────────────────────────
                Column(Text("Shadows")
                           ->setWidth(100)
                           ->setTextColor(RGB(255, 255, 255)),
                       Slider(-100.0, 100.0, 1.0)
                           ->setTrackFillColor(RGB(130, 100, 255))
                           ->setOnValueChanged([&](double v) {
                             auto a = adj.get();
                             a.shadows = (float)v;
                             adj.set(a);
                           })),
                // ── Whites ────────────────────────────────────────
                Column(Text("Whites")
                           ->setWidth(100)
                           ->setTextColor(RGB(255, 255, 255)),
                       Slider(-100.0, 100.0, 1.0)
                           ->setTrackFillColor(RGB(130, 100, 255))
                           ->setOnValueChanged([&](double v) {
                             auto a = adj.get();
                             a.whites = (float)v;
                             adj.set(a);
                           })),
                // ── Blacks ────────────────────────────────────────
                Column(Text("Blacks")
                           ->setWidth(100)
                           ->setTextColor(RGB(255, 255, 255)),
                       Slider(-100.0, 100.0, 1.0)
                           ->setTrackFillColor(RGB(130, 100, 255))
                           ->setOnValueChanged([&](double v) {
                             auto a = adj.get();
                             a.blacks = (float)v;
                             adj.set(a);
                           })),
                // ── Clarity ───────────────────────────────────────
                Column(Text("Clarity")
                           ->setWidth(100)
                           ->setTextColor(RGB(255, 255, 255)),
                       Slider(-100.0, 100.0, 1.0)
                           ->setTrackFillColor(RGB(130, 100, 255))
                           ->setOnValueChanged([&](double v) {
                             auto a = adj.get();
                             a.clarity = (float)v;
                             adj.set(a);
                           })),
                // ── Saturation ────────────────────────────────────
                Column(Text("Saturation")
                           ->setWidth(100)
                           ->setTextColor(RGB(255, 255, 255)),
                       Slider(-100.0, 100.0, 1.0)
                           ->setTrackFillColor(RGB(130, 100, 255))
                           ->setOnValueChanged([&](double v) {
                             auto a = adj.get();
                             a.saturation = (float)v;
                             adj.set(a);
                           })),
                // ── Vibrance ──────────────────────────────────────
                Column(Text("Vibrance")
                           ->setWidth(100)
                           ->setTextColor(RGB(255, 255, 255)),
                       Slider(-100.0, 100.0, 1.0)
                           ->setTrackFillColor(RGB(130, 100, 255))
                           ->setOnValueChanged([&](double v) {
                             auto a = adj.get();
                             a.vibrance = (float)v;
                             adj.set(a);
                           })),
                // ── Temperature ───────────────────────────────────
                Column(Text("Temperature")
                           ->setWidth(100)
                           ->setTextColor(RGB(255, 255, 255)),
                       Slider(-100.0, 100.0, 1.0)
                           ->setTrackFillColor(RGB(130, 100, 255))
                           ->setOnValueChanged([&](double v) {
                             auto a = adj.get();
                             a.temperature = (float)v;
                             adj.set(a);
                           })),
                // ── Tint ──────────────────────────────────────────
                Column(Text("Tint")
                           ->setWidth(100)
                           ->setTextColor(RGB(255, 255, 255)),
                       Slider(-100.0, 100.0, 1.0)
                           ->setTrackFillColor(RGB(130, 100, 255))
                           ->setOnValueChanged([&](double v) {
                             auto a = adj.get();
                             a.tint = (float)v;
                             adj.set(a);
                           })),
                // ── Gamma ─────────────────────────────────────────
                Column(Text("Gamma")
                           ->setWidth(100)
                           ->setTextColor(RGB(255, 255, 255)),
                       Slider(0.1, 5.0, 0.01)
                           ->setOnValueChanged([&](double v) {
                             auto a = adj.get();
                             a.gamma = (float)v;
                             adj.set(a);
                           })),

                // ── Action buttons ────────────────────────────────
                Button(Text("Reset"), [&] {
                  adj.set(ImageAdjustments{});
                }),

                Button(Text("Export"), [&] {
                  if (!imageWidget)
                    return;

                  // Open save dialog
                  std::string path = getSavePath();
                  if (path.empty())
                    return;

                  // Make sure our GL context is current before reading pixels
                  wglMakeCurrent(imageWidget->getGLDC(), imageWidget->getGLRC());

                  bool ok = exportTexture(
                      imageWidget->getOutputTexture(),
                      imageWidget->imageWidth(),
                      imageWidget->imageHeight(),
                      path);

                  if (ok)
                    MessageBoxA(nullptr, ("Saved to:\n" + path).c_str(),
                                "Export successful", MB_OK | MB_ICONINFORMATION);
                  else
                    MessageBoxA(nullptr, "Failed to write image file.",
                                "Export failed", MB_OK | MB_ICONERROR);
                }))
                ->setSpacing(10))
            ->setWidth(200);

    return Scaffold(
        AppBar("Photo Editor"),
        Container(Center(Row(imageWidget, controls)
                             ->setSpacing(20)
                             ->setCrossAlignment(Alignment::Center)))
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
  GdiplusInitializer gdiplusInit;
  FluxUI app(hInstance);
  app.build([&]() { return createApp(&app); });
  app.createWindow("FluxUI - Photo Editor", 1000, 1000);
  return app.run();
}